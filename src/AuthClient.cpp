#include "AuthClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

QString AuthClient::normalizeServerUrl(QString url)
{
    url = url.trimmed();
    // Strip trailing slashes — but never the scheme's own "//" ("http://"
    // stays "http://"; degenerate input stays degenerate rather than being
    // silently rewritten into something we invented).
    while (url.endsWith(QLatin1Char('/'))
           && !url.endsWith(QLatin1String("://")))
        url.chop(1);
    return url;
}

AuthClient::AuthClient(const QString& serverUrl, QObject* parent)
    : QObject(parent)
    , m_serverUrl(normalizeServerUrl(serverUrl))
{
}

namespace
{
// The credential half of a login/register body, built in one place so the
// two routes cannot disagree about the opt-in's spelling.
QJsonObject credentials(const QString& username, const QString& password,
                        bool remember, const QString& deviceLabel)
{
    QJsonObject body;
    body[QStringLiteral("username")] = username;
    body[QStringLiteral("password")] = password;
    // Only SAY "remember" when we mean it. An always-present `false` would
    // work identically, but this keeps a request from a client that never
    // heard of devices byte-identical to what it always sent.
    if (remember) {
        body[QStringLiteral("remember")] = true;
        body[QStringLiteral("device")]   = deviceLabel;
    }
    return body;
}
} // namespace

void AuthClient::registerUser(const QString& username, const QString& password,
                              bool remember, const QString& deviceLabel,
                              const QString& invite)
{
    QJsonObject body = credentials(username, password, remember, deviceLabel);
    // Same rule as `remember`: say it only when there is something to say, so
    // a request to a server that never asks stays exactly what it always was.
    if (!invite.trimmed().isEmpty())
        body[QStringLiteral("invite")] = invite.trimmed();
    post(QStringLiteral("/register"), body, username);
}

void AuthClient::login(const QString& username, const QString& password,
                       bool remember, const QString& deviceLabel)
{
    post(QStringLiteral("/login"),
         credentials(username, password, remember, deviceLabel), username);
}

void AuthClient::resumeSession(const QString& deviceToken)
{
    QJsonObject body;
    body[QStringLiteral("deviceToken")] = deviceToken;
    // No username hint: we genuinely do not know whose token this is until
    // the server says so, and guessing would only create a way to be wrong.
    post(QStringLiteral("/session"), body, QString());
}

void AuthClient::revokeDevice(const QString& deviceToken)
{
    if (deviceToken.isEmpty())
        return;
    QJsonObject body;
    body[QStringLiteral("deviceToken")] = deviceToken;
    post(QStringLiteral("/session/revoke"), body, QString());
}

void AuthClient::post(const QString& path, const QJsonObject& body,
                      const QString& usernameHint)
{
    // Drop any pooled connections before every request. Our server is
    // one-request-per-connection (it closes the socket after responding), but
    // QNetworkAccessManager caches and REUSES connections — and when a request
    // is driven from inside a nested event loop (exactly how a modal login
    // dialog and our tests both work), reusing a socket the server already
    // closed surfaces as a bogus NetworkError on alternating requests. Clearing
    // the cache forces a fresh, clean connection every time. Correctness over a
    // few microseconds of pooling, against a server that never wanted pooling.
    // (This was the single hardest bug of the session — QB M tells the story.)
    m_net.clearConnectionCache();

    QNetworkRequest request(QUrl(m_serverUrl + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    const QByteArray payload =
        QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_net.post(request, payload);

    // Handle THIS reply's completion inline. Capturing `username` lets us echo
    // it back with the result; capturing `reply` lets us read + delete it. The
    // lambda fires whenever the server answers (or the attempt fails), on the
    // UI thread, so it can safely drive the dialog.
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, usernameHint]() {
        // Read everything we need, THEN schedule the reply for deletion, THEN
        // emit on a fresh trip through the event loop. Emitting synchronously
        // here lets a caller fire the NEXT request from inside this handler —
        // while `reply` is still half-alive and QNAM's connection state is
        // mid-teardown — which surfaced as spurious NetworkErrors on every
        // second request. QTimer::singleShot(0, …) defers the emit until the
        // stack has unwound and the reply is gone. (Two red tests to find;
        // QB M has the full story.)
        // Distinguish "couldn't reach the server" from "server answered with
        // an error status" (409 taken, 401 bad creds). QNetworkReply::error()
        // is set NON-zero for HTTP 4xx too — and for benign conditions like
        // content already delivered — so testing it alone misreports a valid
        // 409 as a NetworkError and throws the body away. The reliable signal
        // is whether an HTTP STATUS CODE came back at all: if it did, the
        // server answered and we parse the body; if it didn't, the transport
        // truly failed. (This misdiagnosis cost the most debugging time this
        // session — see QB M.)
        const QVariant statusVar = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute);
        Outcome outcome;
        QString token;
        QString deviceToken;
        QString username = usernameHint;
        if (!statusVar.isValid()) {
            outcome = Outcome::NetworkError; // no HTTP reply → real failure
        } else {
            const QJsonObject obj =
                QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.value(QStringLiteral("ok")).toBool()) {
                outcome     = Outcome::Success;
                token       = obj.value(QStringLiteral("token")).toString();
                deviceToken =
                    obj.value(QStringLiteral("deviceToken")).toString();
                // A resume knows the account only because the server said so.
                // Absent on login/register, where the hint is already right.
                const QString said =
                    obj.value(QStringLiteral("username")).toString();
                if (!said.isEmpty())
                    username = said;
            } else {
                const QString error =
                    obj.value(QStringLiteral("error")).toString();
                if (error == QLatin1String("username_taken"))
                    outcome = Outcome::UsernameTaken;
                else if (error == QLatin1String("bad_credentials")
                         // v30.2: a refused device token. The server says
                         // "auth" here, the same word /planner uses for a
                         // dead session. Mapped to BadCredentials because
                         // the honest advice is identical — the credential
                         // did not work, log in properly — and routing it to
                         // UnknownServerReply would tell someone to check
                         // their server address over a revoked phone.
                         || error == QLatin1String("auth"))
                    outcome = Outcome::BadCredentials;
                else if (error == QLatin1String("invite_required"))
                    outcome = Outcome::InviteRequired;
                else if (error == QLatin1String("too_many"))
                    // Not a verdict on the password. Telling someone their
                    // details are wrong here would have them retyping a
                    // CORRECT password, which is the worst possible advice.
                    outcome = Outcome::TooManyAttempts;
                else if (error == QLatin1String("invalid_input"))
                    outcome = Outcome::InvalidInput;
                else
                    // v29.0.1: an error token we don't recognise is NOT the
                    // owner's typo — naming the difference is the fix that
                    // matters most here (the code fix merely removes one
                    // way to reach it).
                    outcome = Outcome::UnknownServerReply;
            }
        }
        reply->deleteLater();
        emit resultReady(outcome, username, token, deviceToken);
    });
}
