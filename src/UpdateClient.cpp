#include "UpdateClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

UpdateClient::UpdateClient(const QString& serverUrl, QObject* parent)
    : QObject(parent)
    , m_url(serverUrl)
{
}

void UpdateClient::checkForUpdate()
{
    m_net.clearConnectionCache(); // one-request-per-connection server (QB M3)

    QNetworkRequest request(QUrl(m_url + QStringLiteral("/version")));
    // No Authorization header — /version is the one public route. An update
    // check must outlive any particular login scheme: the whole point is
    // reaching apps that are OUT of date.
    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QVariant status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        if (!status.isValid()) {
            emit checkFinished(Outcome::NetworkError, {}, {}, {});
            return;
        }
        if (status.toInt() != 200
            || !obj.value(QStringLiteral("ok")).toBool()
            || obj.value(QStringLiteral("latest")).toString().isEmpty()) {
            // 404 not_configured, weird statuses, empty payloads — all one
            // bucket, because the caller does the same thing with each of
            // them: nothing.
            emit checkFinished(Outcome::Unavailable, {}, {}, {});
            return;
        }
        emit checkFinished(Outcome::Success,
                           obj.value(QStringLiteral("latest")).toString(),
                           obj.value(QStringLiteral("url")).toString(),
                           obj.value(QStringLiteral("notes")).toString());
    });
}
