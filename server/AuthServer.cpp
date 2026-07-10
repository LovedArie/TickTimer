#include "AuthServer.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTextStream>

AuthServer::AuthServer(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_accounts(QDir(dataDir).filePath(QStringLiteral("accounts.json")))
    , m_planners(QDir(dataDir).filePath(QStringLiteral("planners")))
    , m_shares(QDir(dataDir).filePath(QStringLiteral("shares.json")))
    , m_dataDir(dataDir)
{
    connect(&m_server, &QTcpServer::newConnection,
            this, &AuthServer::onNewConnection);
}

bool AuthServer::start(quint16 port)
{
    // QHostAddress::Any = listen on every network interface, so both
    // localhost (the app on this same laptop) and the LAN IP (a phone on the
    // same Wi-Fi) can reach us. On a public box you'd bind more narrowly;
    // on a home network this is what "reachable from my other devices" means.
    if (!m_server.listen(QHostAddress::Any, port)) {
        QTextStream(stderr)
            << "TickTimer server: could not listen on port " << port
            << " — is another copy already running?\n";
        return false;
    }

    // Print every address a client could use — the fix for "my laptop's IP
    // keeps changing / I don't know what it is." The user reads this line off
    // their own terminal and types it into the phone.
    QTextStream out(stdout);
    out << "TickTimer server listening on port " << port << "\n";
    out << "  from THIS computer:      http://localhost:" << port << "\n";
    const auto addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress& a : addresses) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol
            && !a.isLoopback()) {
            out << "  from another device:     http://" << a.toString()
                << ":" << port << "\n";
        }
    }
    out << "Press Ctrl+C to stop.\n";
    out.flush();
    return true;
}

void AuthServer::onNewConnection()
{
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        // One connection, one request, one response, then close. This is
        // HTTP/1.0-style (no keep-alive) — the simplest correct thing, and
        // fine for a login that happens once. readyRead may fire several
        // times for a large body, so we buffer until we've got headers +
        // the full Content-Length.
        auto* buffer = new QByteArray;
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, buffer]() {
                    buffer->append(socket->readAll());

                    const int headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return; // headers not complete yet, wait for more

                    const QByteArray head = buffer->left(headerEnd);
                    const QByteArray body = buffer->mid(headerEnd + 4);

                    // Content-Length tells us how much body to expect; keep
                    // waiting until the buffer holds all of it.
                    int contentLength = 0;
                    QByteArray bearer;
                    for (const QByteArray& line : head.split('\n')) {
                        const QByteArray l = line.trimmed().toLower();
                        if (l.startsWith("content-length:"))
                            contentLength = l.mid(15).trimmed().toInt();
                        // Case-insensitive HEADER NAME, case-preserving
                        // VALUE: detect on the lowered copy, but cut the
                        // token from the original line — header names are
                        // case-insensitive by spec, their values are not.
                        if (l.startsWith("authorization:")) {
                            QByteArray v = line.trimmed();
                            v = v.mid(v.indexOf(':') + 1).trimmed();
                            if (v.left(7).toLower() == "bearer ")
                                v = v.mid(7).trimmed();
                            bearer = v;
                        }
                    }
                    if (body.size() < contentLength)
                        return; // body still arriving

                    // First line: "POST /login HTTP/1.1"
                    const QByteArray requestLine = head.left(head.indexOf('\n'));
                    const QList<QByteArray> tokens = requestLine.trimmed().split(' ');

                    Request req;
                    if (tokens.size() >= 2) {
                        req.method = QString::fromLatin1(tokens[0]);
                        req.path   = QString::fromLatin1(tokens[1]);
                    }
                    req.bearer = QString::fromLatin1(bearer);
                    req.body   = body.left(contentLength);

                    handle(socket, req);
                    delete buffer;
                });

        connect(socket, &QTcpSocket::disconnected,
                socket, &QObject::deleteLater);
    }
}

// 128 bits of SYSTEM randomness, hex-encoded. This is why a token can stand
// in for the password: it's unguessable, it proves nothing except "the server
// handed me this after a successful login", and it can be forgotten (server
// restart) without harming the account itself.
static QString newToken()
{
    QByteArray raw(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32*>(raw.data()), raw.size() / 4);
    return QString::fromLatin1(raw.toHex());
}

void AuthServer::handle(QTcpSocket* socket, const Request& req)
{
    // Parse the JSON body once; both routes need username + password.
    const QJsonObject in =
        QJsonDocument::fromJson(req.body).object();
    const QString username = in.value(QStringLiteral("username")).toString();
    const QString password = in.value(QStringLiteral("password")).toString();

    // Router: method + path -> handler. A real framework would give you a
    // table of routes; ours is an if-ladder, which at this size is clearer
    // than any abstraction over it.
    if (req.method == QLatin1String("POST")
        && req.path == QLatin1String("/register")) {
        const auto r = m_accounts.registerAccount(username, password);
        if (r == AccountStore::Result::Ok) {
            // Registering IS logging in — mint a session token right away so
            // a new user isn't asked to type the same password twice.
            const QString token = newToken();
            m_tokens.insert(token, username.trimmed().toLower());
            sendJson(socket, 200, {{"ok", true}, {"token", token}});
        }
        else if (r == AccountStore::Result::UsernameTaken)
            sendJson(socket, 409, {{"ok", false}, {"error", "username_taken"}});
        else
            sendJson(socket, 400, {{"ok", false}, {"error", "invalid_input"}});
        return;
    }

    if (req.method == QLatin1String("POST")
        && req.path == QLatin1String("/login")) {
        const auto r = m_accounts.login(username, password);
        if (r == AccountStore::Result::Ok) {
            const QString token = newToken();
            m_tokens.insert(token, username.trimmed().toLower());
            sendJson(socket, 200, {{"ok", true}, {"token", token}});
        }
        else
            // Deliberately ONE message for both "no such user" and "wrong
            // password" — telling an attacker which usernames exist is a
            // (small) information leak. The store distinguishes them; the
            // wire does not. A real security decision, made on purpose.
            sendJson(socket, 401, {{"ok", false}, {"error", "bad_credentials"}});
        return;
    }

    // ---- sync routes: everything below this line requires a session ----
    if (req.path == QLatin1String("/planner")) {
        // The token IS the identity here — no username in the request at
        // all. Whoever holds a valid token gets exactly that account's
        // planner and nobody else's; an invalid/forgotten token gets 401
        // regardless of what it asks for.
        const QString user = m_tokens.value(req.bearer);
        if (user.isEmpty()) {
            sendJson(socket, 401, {{"ok", false}, {"error", "auth"}});
            return;
        }

        if (req.method == QLatin1String("GET")) {
            sendJson(socket, 200,
                     {{"ok", true},
                      {"revision", m_planners.revision(user)},
                      {"data", m_planners.planner(user)}});
            return;
        }

        if (req.method == QLatin1String("PUT")) {
            const int  base    = in.value(QStringLiteral("baseRevision"))
                                     .toInt(-1);
            const bool force   = in.value(QStringLiteral("force")).toBool();
            const int  current = m_planners.revision(user);
            // Optimistic concurrency, the whole conflict story in one if:
            // the client claims "I'm based on revision N"; if the shelf has
            // moved past N, refusing (with the current revision attached)
            // beats silently overwriting whatever another device saved.
            if (!force && base != current) {
                sendJson(socket, 409,
                         {{"ok", false},
                          {"error", "conflict"},
                          {"revision", current}});
                return;
            }
            const int rev = m_planners.store(
                user, in.value(QStringLiteral("data")).toObject());
            sendJson(socket, 200, {{"ok", true}, {"revision", rev}});
            return;
        }
    }

    // ---- share & compare routes: all token-gated, all about READ access ----
    // The design in one line: the server can't summarise an opaque blob, so
    // "sharing a planner" means "permitting someone to fetch the blob" — the
    // ShareStore holds the permissions, the route below hands out the blobs,
    // and all understanding happens on the client (Compare.h + stats).

    if (req.path == QLatin1String("/share")
        || req.path == QLatin1String("/unshare")) {
        const QString user = m_tokens.value(req.bearer);
        if (user.isEmpty()) {
            sendJson(socket, 401, {{"ok", false}, {"error", "auth"}});
            return;
        }
        const QString with =
            in.value(QStringLiteral("with")).toString().trimmed().toLower();

        if (req.method == QLatin1String("POST")
            && req.path == QLatin1String("/share")) {
            // Granting to a name that doesn't exist would "work" silently
            // and then confuse everyone ("I shared, why can't you see it?"
            // — because you typed 'bobb'). Validate at the door: identity
            // questions go to AccountStore, the one store that knows.
            if (!m_accounts.hasUser(with)) {
                sendJson(socket, 404,
                         {{"ok", false}, {"error", "no_such_user"}});
                return;
            }
            if (!m_shares.grant(user, with)) {
                // grant() only refuses self-shares and empty names.
                sendJson(socket, 400,
                         {{"ok", false}, {"error", "invalid_input"}});
                return;
            }
            sendJson(socket, 200, {{"ok", true}});
            return;
        }

        if (req.method == QLatin1String("POST")
            && req.path == QLatin1String("/unshare")) {
            // No existence check here, on purpose: revoking access from a
            // deleted or misspelled name should still succeed — the caller
            // asked for an end state ("this name can't read me"), and that
            // end state holds. Idempotence beats pedantry for un-doing.
            m_shares.revoke(user, with);
            sendJson(socket, 200, {{"ok", true}});
            return;
        }
    }

    if (req.method == QLatin1String("GET")
        && req.path == QLatin1String("/shares")) {
        const QString user = m_tokens.value(req.bearer);
        if (user.isEmpty()) {
            sendJson(socket, 401, {{"ok", false}, {"error", "auth"}});
            return;
        }
        // Both directions in one reply — the dialog shows both lists, and
        // one round-trip beats two on a phone's Wi-Fi.
        sendJson(socket, 200,
                 {{"ok", true},
                  {"iShareWith",
                   QJsonArray::fromStringList(m_shares.viewersOf(user))},
                  {"sharedWithMe",
                   QJsonArray::fromStringList(
                       m_shares.ownersSharedWith(user))}});
        return;
    }

    if (req.method == QLatin1String("GET")
        && req.path.startsWith(QLatin1String("/planner/"))) {
        // A path PARAMETER: /planner/alice names alice's planner the way
        // /planner names your own. Everything after the prefix is the owner.
        const QString user = m_tokens.value(req.bearer);
        if (user.isEmpty()) {
            sendJson(socket, 401, {{"ok", false}, {"error", "auth"}});
            return;
        }
        const QString owner =
            req.path.mid(int(qstrlen("/planner/"))).trimmed().toLower();

        // 401 vs 403, the distinction worth learning once and keeping:
        // 401 = "I don't know who you are" (fix: log in again);
        // 403 = "I know EXACTLY who you are, and the answer is no"
        // (fix: ask the owner to share). Different problems, different
        // status codes, so the client can give different advice.
        if (!m_shares.canRead(user, owner)) {
            sendJson(socket, 403, {{"ok", false}, {"error", "forbidden"}});
            return;
        }
        sendJson(socket, 200,
                 {{"ok", true},
                  {"revision", m_planners.revision(owner)},
                  {"data", m_planners.planner(owner)}});
        return;
    }

    // ---- auto-update route (networked arc part 4, the arc closer) ---------
    // GET /version: the server ADVERTISES the newest release. No token — the
    // question "what's the latest TickTimer?" isn't private, and asking it
    // must work even for an app so old its login flow might have changed.
    //
    // The answer is read from version.json ON EVERY REQUEST, not cached at
    // startup. That's a deliberate trade: the file is ~200 bytes, requests
    // are rare (one per app launch), and re-reading means announcing a new
    // release is "edit the file" — no server restart, nothing to remember.
    // The operator (you) is the source of truth; the server just repeats
    // what the file says, exactly as it repeats planner blobs it never reads.
    if (req.method == QLatin1String("GET")
        && req.path == QLatin1String("/version")) {
        QFile f(QDir(m_dataDir).filePath(QStringLiteral("version.json")));
        if (!f.open(QIODevice::ReadOnly)) {
            // No file = the operator hasn't configured updates. That's not
            // an error, it's an absence — 404, and the client stays silent.
            sendJson(socket, 404,
                     {{"ok", false}, {"error", "not_configured"}});
            return;
        }
        const QJsonObject info =
            QJsonDocument::fromJson(f.readAll()).object();
        const QString latest =
            info.value(QStringLiteral("latest")).toString().trimmed();
        if (latest.isEmpty()) { // present but unusable — same silence
            sendJson(socket, 404,
                     {{"ok", false}, {"error", "not_configured"}});
            return;
        }
        sendJson(socket, 200,
                 {{"ok", true},
                  {"latest", latest},
                  {"url", info.value(QStringLiteral("url")).toString()},
                  {"notes", info.value(QStringLiteral("notes")).toString()}});
        return;
    }

    sendJson(socket, 404, {{"ok", false}, {"error", "not_found"}});
}

void AuthServer::sendJson(QTcpSocket* socket, int status,
                          const QJsonObject& body)
{
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    // Hand-assemble the HTTP response: status line, the couple of headers a
    // JSON client needs, blank line, body. Seeing this spelled out is the
    // point — a framework hides exactly these bytes.
    // A proper reason phrase after the code. An EMPTY phrase ("HTTP/1.0 409 ")
    // is technically legal but some clients — QNetworkAccessManager among them,
    // under certain reuse conditions — mishandle it, which is what made 4xx
    // replies intermittently arrive as NetworkError. Spelling out the phrase
    // costs nothing and removes the ambiguity. (The bug that ate this session.)
    const char* reason = "OK";
    switch (status) {
    case 400: reason = "Bad Request";  break;
    case 401: reason = "Unauthorized"; break;
    case 403: reason = "Forbidden";    break; // new with share/compare
    case 404: reason = "Not Found";    break;
    case 409: reason = "Conflict";     break;
    default:  reason = "OK";           break;
    }

    QByteArray response;
    response += "HTTP/1.0 " + QByteArray::number(status) + " " + reason + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n";
    // Connection: close is load-bearing, not boilerplate. We close the socket
    // after every response (one-request-per-connection), but QNetworkAccess-
    // Manager keeps connections alive and REUSES them by default — so its
    // second request would travel down a socket we'd already closed and come
    // back as a NetworkError. This header tells the client "don't reuse me",
    // so it opens a fresh connection each time. (Cost a red test to find.)
    response += "Connection: close\r\n";
    response += "Access-Control-Allow-Origin: *\r\n"; // dev convenience
    response += "\r\n";
    response += payload;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}
