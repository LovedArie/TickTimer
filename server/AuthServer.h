#pragma once

#include "AccountStore.h"
#include "DeviceStore.h"
#include "PlannerStore.h"
#include "ShareStore.h"

#include <QHash>
#include <QVector>
#include <QObject>
#include <QHostAddress>
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
    //
    // v30.2.1 — `bindAddress` defaults to LOCALHOST, which is a deliberate
    // change from the old QHostAddress::Any. This is a hand-rolled HTTP
    // parser: on a public box it must sit behind a real one (Caddy, nginx)
    // and see only requests that server already validated. Binding to every
    // interface by default meant one forgotten flag stood between the parser
    // and the open internet.
    //
    // The cost is real and is paid on purpose: a phone on the same Wi-Fi can
    // no longer reach a laptop-hosted server until you pass `--bind any`. That
    // failure is LOUD (the phone cannot connect) and one flag from fixed. The
    // failure of the old default is silent and not.
    bool start(quint16 port = 8080,
               const QHostAddress& bindAddress = QHostAddress::LocalHost);

    // Require an invite code on /register (v30.2.1). Empty means registration
    // is open, which is right for a laptop on your own LAN and wrong for
    // anything with a public address — an open signup endpoint on the
    // internet is a bigger risk than the parser everyone worries about.
    //
    // Not a per-account invite, deliberately: one shared code the owner hands
    // to a friend. Tracking single-use invites would need a store, an expiry
    // policy and a UI to mint them, for a handful of people who can be told a
    // word over a message.
    void setInviteCode(const QString& code) { m_inviteCode = code.trimmed(); }
    bool registrationIsOpen() const { return m_inviteCode.isEmpty(); }

private slots:
    void onNewConnection();

private:
    // The request, parsed down to what our handlers care about.
    struct Request {
        QString method;   // "POST", "GET", "PUT"
        QString path;     // "/login", "/planner"
        QString bearer;   // session token from the Authorization header
        QString clientId; // who to rate-limit (v30.2.1) — see clientIdFor()
        QByteArray body;  // raw JSON
    };

    void handle(class QTcpSocket* socket, const Request& req);
    void sendJson(class QTcpSocket* socket, int status,
                  const QJsonObject& body);

    // v30.2 — add a freshly minted device token to a login/register reply,
    // but only if the client asked to be remembered. Takes `out` by value and
    // returns it: the caller writes `sendJson(..., withDeviceToken(in, user,
    // {...}))`, so the opt-in cannot be forgotten at one of the two call
    // sites while being honoured at the other.
    QJsonObject withDeviceToken(const QJsonObject& in, const QString& username,
                                QJsonObject out);

    AccountStore m_accounts;
    PlannerStore m_planners;
    ShareStore   m_shares;   // who may READ whose planner (share & compare)
    // v30.2 — remembered devices. NOT a persisted session token: a separate,
    // revocable credential that buys exactly one thing, a fresh session
    // token. See DeviceStore.h for why that does not contradict m_tokens.
    DeviceStore  m_devices;
    QString      m_dataDir;  // where version.json is looked up per request
    // token -> canonical username. IN MEMORY on purpose: tokens are session
    // state, not records. A server restart forgets them all, every client
    // gets a 401, and logging in again mints fresh ones — which the app
    // already does on every launch. Persisting tokens would be persisting
    // open doors.
    QHash<QString, QString> m_tokens;

    // ---- v30.2.1: credential-stuffing brake --------------------------------
    //
    // Done HERE rather than at the reverse proxy, which is where the plan
    // first put it, for a practical reason worth writing down: stock Caddy
    // has no rate-limit directive — it needs a plugin and a custom build via
    // xcaddy. Making the safe deployment depend on compiling your own web
    // server is how the safe deployment does not happen. Twenty lines here
    // work behind any proxy, or none.
    //
    // Only FAILURES count. A person legitimately signing in ten times in a
    // morning must never lock themselves out; someone guessing passwords
    // produces nothing but failures, which is the signal.
    bool   throttled(const QString& clientId);
    void   noteFailure(const QString& clientId);
    void   clearFailures(const QString& clientId);

    // Who a request is FROM, for throttling purposes.
    //
    // Behind a reverse proxy every request arrives from 127.0.0.1, so a
    // per-peer counter would throttle the whole world as one client — the
    // first person to fumble a password would lock out everybody. So
    // X-Forwarded-For is honoured, but ONLY when the peer is loopback (i.e.
    // the proxy is on this box). Trusting that header from an arbitrary peer
    // would let anyone forge a fresh identity per attempt and defeat the
    // brake entirely.
    static QString clientIdFor(const class QTcpSocket* socket,
                               const QByteArray& forwardedFor);

    QString m_inviteCode; // empty = registration open
    // clientId -> the moments of its recent failed attempts.
    QHash<QString, QVector<qint64>> m_failures;
    QTcpServer   m_server;
};
