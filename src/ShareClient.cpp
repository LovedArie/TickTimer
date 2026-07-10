#include "ShareClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ShareClient::ShareClient(const QString& serverUrl, const QString& token,
                         QObject* parent)
    : QObject(parent)
    , m_url(serverUrl)
    , m_token(token)
{
}

QNetworkRequest ShareClient::makeRequest(const QString& path) const
{
    QNetworkRequest request(QUrl(m_url + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_token.toLatin1());
    return request;
}

// One classifier instead of four copies of the same if-ladder. AuthClient and
// SyncClient each hand-rolled this; third time is when duplication becomes a
// function (the rule of three — twice might be coincidence, three times is a
// pattern asking to be named).
ShareClient::Outcome ShareClient::classify(QNetworkReply* reply,
                                           QJsonObject* bodyOut) const
{
    const QVariant status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QJsonObject obj =
        QJsonDocument::fromJson(reply->readAll()).object();
    if (bodyOut)
        *bodyOut = obj;

    if (!status.isValid())
        return Outcome::NetworkError; // transport failed — no HTTP happened
    switch (status.toInt()) {
    case 401: return Outcome::AuthError;
    case 403: return Outcome::Forbidden;
    case 404:
        // Two different 404s travel this wire: "no such route" and "no such
        // user". Both mean the thing you named doesn't exist; NotFound
        // covers them and the dialog words it for a human.
        return Outcome::NotFound;
    default:  break;
    }
    if (!obj.value(QStringLiteral("ok")).toBool())
        return Outcome::NetworkError; // fail closed on anything unexpected
    return Outcome::Success;
}

void ShareClient::fetchShares()
{
    m_net.clearConnectionCache(); // one-request-per-connection server (QB M3)

    QNetworkReply* reply = m_net.get(makeRequest(QStringLiteral("/shares")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject obj;
        const Outcome outcome = classify(reply, &obj);
        reply->deleteLater();

        QStringList iShareWith, sharedWithMe;
        for (const QJsonValue& v :
             obj.value(QStringLiteral("iShareWith")).toArray())
            iShareWith << v.toString();
        for (const QJsonValue& v :
             obj.value(QStringLiteral("sharedWithMe")).toArray())
            sharedWithMe << v.toString();
        emit sharesReady(outcome, iShareWith, sharedWithMe);
    });
}

void ShareClient::share(const QString& username)
{
    m_net.clearConnectionCache();

    QJsonObject body;
    body[QStringLiteral("with")] = username;
    QNetworkReply* reply =
        m_net.post(makeRequest(QStringLiteral("/share")),
                   QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const Outcome outcome = classify(reply, nullptr);
        reply->deleteLater();
        emit shareUpdated(outcome);
    });
}

void ShareClient::unshare(const QString& username)
{
    m_net.clearConnectionCache();

    QJsonObject body;
    body[QStringLiteral("with")] = username;
    QNetworkReply* reply =
        m_net.post(makeRequest(QStringLiteral("/unshare")),
                   QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const Outcome outcome = classify(reply, nullptr);
        reply->deleteLater();
        emit shareUpdated(outcome);
    });
}

void ShareClient::fetchPeerPlanner(const QString& username)
{
    m_net.clearConnectionCache();

    // The peer's name rides in the PATH — /planner/alice — the same way the
    // web addresses "a thing": nouns in the URL, verbs in the method. GET
    // because this is a pure read; the server enforces that no write route
    // exists for someone else's planner at all.
    const QString user = username.trimmed().toLower();
    QNetworkReply* reply =
        m_net.get(makeRequest(QStringLiteral("/planner/") + user));
    connect(reply, &QNetworkReply::finished, this, [this, reply, user]() {
        QJsonObject obj;
        const Outcome outcome = classify(reply, &obj);
        reply->deleteLater();
        emit peerPlannerReady(outcome, user,
                              obj.value(QStringLiteral("data")).toObject());
    });
}
