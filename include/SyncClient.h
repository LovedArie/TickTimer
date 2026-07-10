#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>

// ---------------------------------------------------------------------------
// SyncClient — the wire layer of sync: GET /planner (pull) and PUT /planner
// (push), authenticated with the session token the login handed us. Same
// shape as AuthClient — async QNetworkAccessManager, one clean typed signal
// per operation — and it INHERITS the three hard-won lessons from the login
// session (QB section M): clear the connection cache before every request,
// branch on HttpStatusCodeAttribute (not reply->error(), which is non-zero
// for perfectly valid 4xx answers), and expect real reason phrases.
//
// Knows nothing about AppData: it moves QJsonObjects. What those objects
// MEAN — and what to do when revisions disagree — is SyncService's job.
// Wire below, policy above.
// ---------------------------------------------------------------------------

class SyncClient : public QObject
{
    Q_OBJECT
public:
    SyncClient(const QString& serverUrl, const QString& token,
               QObject* parent = nullptr);

    enum class Outcome {
        Success,
        Conflict,     // push refused: server moved past our base revision
        AuthError,    // token invalid/expired (server restarted) — re-login
        NetworkError  // couldn't reach the server at all
    };
    Q_ENUM(Outcome)

    void pull();
    void push(const QJsonObject& data, int baseRevision, bool force);

signals:
    void pullFinished(SyncClient::Outcome outcome, int revision,
                      const QJsonObject& data);
    void pushFinished(SyncClient::Outcome outcome, int revision);

private:
    QNetworkRequest makeRequest() const;

    QNetworkAccessManager m_net;
    QString               m_url;
    QString               m_token;
};
