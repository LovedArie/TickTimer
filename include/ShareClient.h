#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

// ---------------------------------------------------------------------------
// ShareClient — the wire layer of share & compare: grant/revoke access,
// list who shares with whom, and fetch a peer's planner blob (read-only).
//
// Third client in the family (AuthClient, SyncClient, now this), and it
// keeps the family rules without exception:
//   - async QNetworkAccessManager, one typed signal per operation;
//   - clearConnectionCache() before every request (our server is
//     one-request-per-connection — QB M3);
//   - branch on HttpStatusCodeAttribute, never reply->error(), which is
//     non-zero for perfectly valid 4xx answers (QB M1).
//
// One NEW status appears on this wire: 403 Forbidden. 401 means "I don't
// know who you are" (bad/expired token — go log in again); 403 means "I know
// exactly who you are, and the answer is no" (no grant). Different problems,
// different fixes, so they are different Outcomes — the same reasoning that
// made AccountStore::Result an enum instead of a bool.
//
// Knows nothing about AppData: blobs in, blobs out. What a peer's blob MEANS
// is the CompareDialog's job (via JsonStore + stats). Wire below, glass above.
// ---------------------------------------------------------------------------

class ShareClient : public QObject
{
    Q_OBJECT
public:
    ShareClient(const QString& serverUrl, const QString& token,
                QObject* parent = nullptr);

    enum class Outcome {
        Success,
        NotFound,     // shared with a username that doesn't exist (typo)
        Forbidden,    // asked for a planner nobody granted us
        AuthError,    // token invalid/expired — re-login
        NetworkError, // couldn't reach the server at all
        UnexpectedReply // v29.0.2 — the server ANSWERED, but not to the
                        // question we thought we asked (a route-level 404,
                        // a future error token). Split from NotFound
                        // because "no such USER" is the owner's typo and
                        // "no such ROUTE" is OUR bug — one message blaming
                        // her spelling for the app's URL cost a live
                        // debugging session (TROUBLESHOOTING, second entry
                        // of the same evening).
    };
    Q_ENUM(Outcome)

    void fetchShares();                          // -> sharesReady
    void share(const QString& username);         // -> shareUpdated
    void unshare(const QString& username);       // -> shareUpdated
    void fetchPeerPlanner(const QString& username); // -> peerPlannerReady

signals:
    void sharesReady(ShareClient::Outcome outcome,
                     const QStringList& iShareWith,
                     const QStringList& sharedWithMe);
    void shareUpdated(ShareClient::Outcome outcome);
    void peerPlannerReady(ShareClient::Outcome outcome, const QString& user,
                          const QJsonObject& data);

private:
    QNetworkRequest makeRequest(const QString& path) const;
    // The shared "read the status honestly" step every reply goes through.
    Outcome classify(class QNetworkReply* reply, QJsonObject* bodyOut) const;

    QNetworkAccessManager m_net;
    QString               m_url;
    QString               m_token;
};
