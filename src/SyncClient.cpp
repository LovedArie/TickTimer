#include "SyncClient.h"

#include "AuthClient.h" // normalizeServerUrl — one rule for every consumer of the base

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

SyncClient::SyncClient(const QString& serverUrl, const QString& token,
                       QObject* parent)
    : QObject(parent)
    , m_url(AuthClient::normalizeServerUrl(serverUrl)) // same landmine, same defusal (v29.0.1)
    , m_token(token)
{
}

QNetworkRequest SyncClient::makeRequest() const
{
    QNetworkRequest request(QUrl(m_url + QStringLiteral("/planner")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    // The session token rides in the standard Authorization header. This is
    // WHY tokens exist: the password proved who we are once, at login; every
    // call after that carries this short-lived stand-in instead, so the
    // password never crosses the wire again.
    request.setRawHeader("Authorization",
                         "Bearer " + m_token.toLatin1());
    return request;
}

void SyncClient::pull()
{
    // QB M3's lesson, applied on day one instead of rediscovered: our server
    // is one-request-per-connection, and QNAM's pooled connections against it
    // produce phantom NetworkErrors. Fresh connection every time.
    m_net.clearConnectionCache();

    QNetworkReply* reply = m_net.get(makeRequest());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QVariant status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        // No HTTP status at all = the transport failed; anything else is the
        // server ANSWERING, even if the answer is an error (the QB M1 bug —
        // reply->error() is non-zero for valid 4xx too, so we don't ask it).
        if (!status.isValid()) {
            emit pullFinished(Outcome::NetworkError, 0, {});
            return;
        }
        if (status.toInt() == 401) {
            emit pullFinished(Outcome::AuthError, 0, {});
            return;
        }
        if (!obj.value(QStringLiteral("ok")).toBool()) {
            emit pullFinished(Outcome::NetworkError, 0, {}); // fail closed
            return;
        }
        emit pullFinished(Outcome::Success,
                          obj.value(QStringLiteral("revision")).toInt(0),
                          obj.value(QStringLiteral("data")).toObject());
    });
}

void SyncClient::push(const QJsonObject& data, int baseRevision, bool force)
{
    m_net.clearConnectionCache();

    QJsonObject body;
    body[QStringLiteral("baseRevision")] = baseRevision;
    body[QStringLiteral("force")]        = force;
    body[QStringLiteral("data")]         = data;

    QNetworkReply* reply = m_net.put(
        makeRequest(),
        QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QVariant status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        if (!status.isValid()) {
            emit pushFinished(Outcome::NetworkError, 0);
            return;
        }
        if (status.toInt() == 401) {
            emit pushFinished(Outcome::AuthError, 0);
            return;
        }
        if (status.toInt() == 409) {
            // The optimistic-concurrency refusal doing its job: the server
            // moved past our base revision. It tells us where it is now, so
            // the caller can pull and let the human decide.
            emit pushFinished(Outcome::Conflict,
                              obj.value(QStringLiteral("revision")).toInt(0));
            return;
        }
        if (!obj.value(QStringLiteral("ok")).toBool()) {
            emit pushFinished(Outcome::NetworkError, 0);
            return;
        }
        emit pushFinished(Outcome::Success,
                          obj.value(QStringLiteral("revision")).toInt(0));
    });
}
