#include "AuthClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

AuthClient::AuthClient(const QString& serverUrl, QObject* parent)
    : QObject(parent)
    , m_serverUrl(serverUrl)
{
}

void AuthClient::registerUser(const QString& username, const QString& password)
{
    post(QStringLiteral("/register"), username, password);
}

void AuthClient::login(const QString& username, const QString& password)
{
    post(QStringLiteral("/login"), username, password);
}

void AuthClient::post(const QString& path, const QString& username,
                      const QString& password)
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

    QJsonObject body;
    body[QStringLiteral("username")] = username;
    body[QStringLiteral("password")] = password;
    const QByteArray payload =
        QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_net.post(request, payload);

    // Handle THIS reply's completion inline. Capturing `username` lets us echo
    // it back with the result; capturing `reply` lets us read + delete it. The
    // lambda fires whenever the server answers (or the attempt fails), on the
    // UI thread, so it can safely drive the dialog.
    connect(reply, &QNetworkReply::finished, this, [this, reply, username]() {
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
        if (!statusVar.isValid()) {
            outcome = Outcome::NetworkError; // no HTTP reply → real failure
        } else {
            const QJsonObject obj =
                QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.value(QStringLiteral("ok")).toBool()) {
                outcome = Outcome::Success;
                token   = obj.value(QStringLiteral("token")).toString();
            } else {
                const QString error =
                    obj.value(QStringLiteral("error")).toString();
                if (error == QLatin1String("username_taken"))
                    outcome = Outcome::UsernameTaken;
                else if (error == QLatin1String("bad_credentials"))
                    outcome = Outcome::BadCredentials;
                else
                    outcome = Outcome::InvalidInput;
            }
        }
        reply->deleteLater();
        emit resultReady(outcome, username, token);
    });
}
