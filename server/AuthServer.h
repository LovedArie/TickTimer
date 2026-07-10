#pragma once

#include "AccountStore.h"
#include "PlannerStore.h"
#include "ShareStore.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>

// ---------------------------------------------------------------------------
// AuthServer — a tiny HTTP/JSON server over QTcpServer.
//
// Why QTcpServer and a hand-rolled HTTP layer, rather than Qt HttpServer?
// Two reasons, one practical, one pedagogical:
//   - Qt::HttpServer is an OPTIONAL module not present in every Qt install
//     (it isn't in this build environment, and may not be in yours). Depending
//     on it would make the project fail to configure on some machines.
//   - QTcpServer + QTcpSocket is in every Qt install, and parsing the request
//     ourselves means you SEE what HTTP actually is: a request line, headers,
//     a blank line, then a body. No magic. When you later meet a real
//     framework, you'll know what it's doing for you.
//
// The protocol we support is deliberately minimal — just enough to register
// and log in — and speaks JSON both ways so the desktop client's existing
// JSON habits carry over. This is NOT a hardened public server; it's a
// private home-network service (see docs/SERVER.md).
// ---------------------------------------------------------------------------

class AuthServer : public QObject
{
    Q_OBJECT
public:
    // dataDir: where accounts.json lives (the server's own data directory,
    // separate from any user's planner data). Passed in, not hard-coded, so
    // tests can point it at a temp dir — the same testability-by-injection
    // choice as the tracker's nowProvider.
    explicit AuthServer(const QString& dataDir, QObject* parent = nullptr);

    // Start listening. Returns false if the port is busy (another copy already
    // running is the usual cause). Prints the reachable addresses on success —
    // solving the "what's my laptop's IP?" problem the moment the server
    // starts, so a phone knows where to point.
    bool start(quint16 port = 8080);

private slots:
    void onNewConnection();

private:
    // The request, parsed down to what our handlers care about.
    struct Request {
        QString method;   // "POST", "GET", "PUT"
        QString path;     // "/login", "/planner"
        QString bearer;   // session token from the Authorization header
        QByteArray body;  // raw JSON
    };

    void handle(class QTcpSocket* socket, const Request& req);
    void sendJson(class QTcpSocket* socket, int status,
                  const QJsonObject& body);

    AccountStore m_accounts;
    PlannerStore m_planners;
    ShareStore   m_shares;   // who may READ whose planner (share & compare)
    QString      m_dataDir;  // where version.json is looked up per request
    // token -> canonical username. IN MEMORY on purpose: tokens are session
    // state, not records. A server restart forgets them all, every client
    // gets a 401, and logging in again mints fresh ones — which the app
    // already does on every launch. Persisting tokens would be persisting
    // open doors.
    QHash<QString, QString> m_tokens;
    QTcpServer   m_server;
};
