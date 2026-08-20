#include "AuthServer.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QDateTime>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTextStream>

AuthServer::AuthServer(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_accounts(QDir(dataDir).filePath(QStringLiteral("accounts.json")))
    , m_planners(QDir(dataDir).filePath(QStringLiteral("planners")))
    , m_shares(QDir(dataDir).filePath(QStringLiteral("shares.json")))
    , m_devices(QDir(dataDir).filePath(QStringLiteral("devices.json")))
    , m_dataDir(dataDir)
{
    connect(&m_server, &QTcpServer::newConnection,
            this, &AuthServer::onNewConnection);
}

bool AuthServer::start(quint16 port, const QHostAddress& bindAddress)
{
    // v30.2.1 — the caller chooses, and the default is LOCALHOST.
    //
    // QHostAddress::Any listens on every interface, which is what "reachable
    // from my phone on the same Wi-Fi" means and what this used to do
    // unconditionally. It is also what "reachable from the entire internet"
    // means the day the same binary runs on a VPS — and this is a hand-rolled
    // HTTP parser that should only ever see requests a real server already
    // validated. One forgotten flag was the whole distance between those two
    // sentences, so the default now fails safe and `--bind any` is a thing
    // somebody types on purpose.
    if (!m_server.listen(bindAddress, port)) {
        QTextStream(stderr)
            << "TickTimer server: could not listen on port " << port
            << " — is another copy already running?\n";
        return false;
    }

    // Print every address a client could use — the fix for "my laptop's IP
    // keeps changing / I don't know what it is." The user reads this line off
    // their own terminal and types it into the phone.
    const bool everyInterface = (bindAddress == QHostAddress::Any
                                 || bindAddress == QHostAddress::AnyIPv4);

    QTextStream out(stdout);
    out << "TickTimer server listening on port " << port
        << " (bound to " << bindAddress.toString() << ")\n";
    out << "  from THIS computer:      http://localhost:" << port << "\n";
    if (everyInterface) {
        const auto addresses = QNetworkInterface::allAddresses();
        for (const QHostAddress& a : addresses) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol
                && !a.isLoopback()) {
                out << "  from another device:     http://" << a.toString()
                    << ":" << port << "\n";
            }
        }
    } else {
        // Say the thing somebody is about to be confused by, before they are.
        out << "  from another device:     NOT reachable — pass --bind any\n";
    }

    // Warn on the COMBINATION, not on either half. Open registration on a
    // laptop's LAN is fine; every interface behind a proxy is fine. Together
    // they are an open signup endpoint on whatever network this box is on,
    // which is the actual risk here — bigger than the parser everyone
    // worries about.
    if (everyInterface && registrationIsOpen()) {
        out << "\n  ! Registration is OPEN and this server is listening on "
               "every interface.\n"
               "    Anyone who can reach it can create an account. Pass "
               "--invite <code>\n"
               "    if this box is not on a network you control.\n";
    }

    out << "\nPress Ctrl+C to stop.\n";
    out.flush();
    return true;
}

// ---- the credential-stuffing brake (v30.2.1) -------------------------------

namespace
{
// Five wrong answers in a row buys a pause. Generous enough that a person
// mistyping their own password never notices; miserly enough that guessing at
// scale stops being worth the wall-clock.
constexpr int   kMaxFailures    = 5;
constexpr qint64 kWindowMs      = 5 * 60 * 1000; // failures older than this
                                                 // are forgotten entirely
} // namespace

QString AuthServer::clientIdFor(const QTcpSocket* socket,
                                const QByteArray& forwardedFor)
{
    const QHostAddress peer = socket ? socket->peerAddress() : QHostAddress();

    // Honour X-Forwarded-For ONLY from a loopback peer — i.e. a proxy running
    // on this same box, which is the deployment this exists for. Trusting it
    // from an arbitrary peer would let anyone mint a fresh identity per
    // attempt by varying one header, which is not a brake at all.
    if (peer.isLoopback() && !forwardedFor.isEmpty()) {
        // "client, proxy1, proxy2" — the first entry is the original client.
        const QByteArray first = forwardedFor.split(',').first().trimmed();
        if (!first.isEmpty())
            return QString::fromLatin1(first);
    }
    return peer.toString();
}

bool AuthServer::throttled(const QString& clientId)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<qint64>& stamps = m_failures[clientId];

    // Prune first, so the window slides instead of being a permanent ban.
    for (int i = stamps.size() - 1; i >= 0; --i)
        if (now - stamps.at(i) > kWindowMs)
            stamps.remove(i);

    if (stamps.isEmpty()) {
        m_failures.remove(clientId); // don't grow a map of empty vectors
        return false;
    }
    return stamps.size() >= kMaxFailures;
}

void AuthServer::noteFailure(const QString& clientId)
{
    m_failures[clientId].append(QDateTime::currentMSecsSinceEpoch());
}

void AuthServer::clearFailures(const QString& clientId)
{
    // A correct password proves the earlier misses were fumbles, not an
    // attack. Forgiving them is what keeps a real person from being punished
    // for their own typing.
    m_failures.remove(clientId);
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
                    QByteArray forwardedFor;
                    for (const QByteArray& line : head.split('\n')) {
                        const QByteArray l = line.trimmed().toLower();
                        if (l.startsWith("content-length:"))
                            contentLength = l.mid(15).trimmed().toInt();
                        // v30.2.1 — who the PROXY says this came from. Read
                        // here, trusted only in clientIdFor(), and only from
                        // a loopback peer.
                        if (l.startsWith("x-forwarded-for:")) {
                            QByteArray v = line.trimmed();
                            forwardedFor = v.mid(v.indexOf(':') + 1).trimmed();
                        }
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
                    req.bearer   = QString::fromLatin1(bearer);
                    req.clientId = clientIdFor(socket, forwardedFor);
                    req.body     = body.left(contentLength);

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

QJsonObject AuthServer::withDeviceToken(const QJsonObject& in,
                                        const QString& username,
                                        QJsonObject out)
{
    // OPT-IN, and asked for by the CLIENT rather than decided here. A device
    // token is a durable credential; minting one for every login — including
    // a one-off login on someone else's machine — would leave credentials
    // lying around that nobody asked to create. Absent key reads as false,
    // which makes "don't remember me" the behaviour of every client written
    // before this existed.
    if (!in.value(QStringLiteral("remember")).toBool())
        return out;

    const QString raw =
        m_devices.remember(username,
                           in.value(QStringLiteral("device")).toString());
    if (!raw.isEmpty())
        out[QStringLiteral("deviceToken")] = raw;
    return out;
}

void AuthServer::handle(QTcpSocket* socket, const Request& req)
{
    // Parse the JSON body once; both routes need username + password.
    const QJsonObject in =
        QJsonDocument::fromJson(req.body).object();
    const QString username = in.value(QStringLiteral("username")).toString();
    const QString password = in.value(QStringLiteral("password")).toString();

    // v30.2.1 — the CORS preflight, answered before anything else looks at
    // the path. A browser sends OPTIONS on its own, ahead of any request that
    // carries Content-Type: application/json or an Authorization header —
    // which is every call this API has. Without this, a web client fails
    // before its real request is ever sent, and the failure looks like the
    // server is down rather than like a missing header.
    //
    // Needed for the WebAssembly build even though it will be served from the
    // same origin: the preflight is triggered by the HEADERS, not only by a
    // cross-origin address.
    if (req.method == QLatin1String("OPTIONS")) {
        sendJson(socket, 204, {});
        return;
    }

    // Router: method + path -> handler. A real framework would give you a
    // table of routes; ours is an if-ladder, which at this size is clearer
    // than any abstraction over it.
    if (req.method == QLatin1String("POST")
        && req.path == QLatin1String("/register")) {
        // The brake applies to registration too: an open signup endpoint is
        // as good a place to hammer as a login one.
        if (throttled(req.clientId)) {
            sendJson(socket, 429, {{"ok", false}, {"error", "too_many"}});
            return;
        }
        // v30.2.1 — the invite gate. Checked BEFORE the account store is
        // touched, so a wrong code cannot tell you whether a username was
        // free: registration on a public box should leak nothing at all.
        if (!registrationIsOpen()
            && in.value(QStringLiteral("invite")).toString().trimmed()
                   != m_inviteCode) {
            noteFailure(req.clientId);
            sendJson(socket, 403,
                     {{"ok", false}, {"error", "invite_required"}});
            return;
        }
        const auto r = m_accounts.registerAccount(username, password);
        if (r == AccountStore::Result::Ok) {
            // Registering IS logging in — mint a session token right away so
            // a new user isn't asked to type the same password twice.
            const QString token = newToken();
            m_tokens.insert(token, username.trimmed().toLower());
            sendJson(socket, 200, withDeviceToken(in, username,
                                                  {{"ok", true},
                                                   {"token", token}}));
        }
        else if (r == AccountStore::Result::UsernameTaken)
            sendJson(socket, 409, {{"ok", false}, {"error", "username_taken"}});
        else
            sendJson(socket, 400, {{"ok", false}, {"error", "invalid_input"}});
        return;
    }

    if (req.method == QLatin1String("POST")
        && req.path == QLatin1String("/login")) {
        if (throttled(req.clientId)) {
            // 429 rather than 401: this is not a verdict on the password, and
            // saying so keeps a locked-out person from retyping a correct one
            // forever wondering why it stopped working.
            sendJson(socket, 429, {{"ok", false}, {"error", "too_many"}});
            return;
        }
        const auto r = m_accounts.login(username, password);
        if (r == AccountStore::Result::Ok) {
            clearFailures(req.clientId);
            const QString token = newToken();
            m_tokens.insert(token, username.trimmed().toLower());
            sendJson(socket, 200, withDeviceToken(in, username,
                                                  {{"ok", true},
                                                   {"token", token}}));
        }
        else {
            // Deliberately ONE message for both "no such user" and "wrong
            // password" — telling an attacker which usernames exist is a
            // (small) information leak. The store distinguishes them; the
            // wire does not. A real security decision, made on purpose.
            //
            // v30.2.1: and one failure counted, which is what makes the
            // one-message rule worth anything — an oracle you can only
            // consult five times per five minutes is a poor oracle.
            noteFailure(req.clientId);
            sendJson(socket, 401, {{"ok", false}, {"error", "bad_credentials"}});
        }
        return;
    }

    // v30.2 — exchange a remembered device for a fresh session. The ONE
    // thing a device token can buy, which is what keeps the persisted
    // credential from being an open door (DeviceStore.h).
    //
    // No password, and deliberately no username either: the token IS the
    // identity, exactly as the bearer token is on /planner below. A client
    // that had to say who it was could get that wrong; this one cannot.
    if (req.method == QLatin1String("POST")
        && req.path == QLatin1String("/session")) {
        const QString deviceToken =
            in.value(QStringLiteral("deviceToken")).toString();
        const QString user = m_devices.userFor(deviceToken);
        if (user.isEmpty()) {
            // Same shape as a bad password: unknown, revoked and malformed
            // are one answer. A client that can tell them apart learns which
            // tokens once existed.
            sendJson(socket, 401, {{"ok", false}, {"error", "auth"}});
            return;
        }
        const QString token = newToken();
        m_tokens.insert(token, user);
        sendJson(socket, 200,
                 {{"ok", true}, {"token", token}, {"username", user}});
        return;
    }

    // Revoke one remembered device — what "log out" means once a device can
    // be remembered. IDEMPOTENT: forgetting a token that was already gone is
    // a success, because the caller wanted it not to work and it does not.
    //
    // Authenticated by the device token itself rather than a session, so a
    // client whose session already expired can still hang up properly.
    if (req.method == QLatin1String("POST")
        && req.path == QLatin1String("/session/revoke")) {
        m_devices.forget(in.value(QStringLiteral("deviceToken")).toString());
        sendJson(socket, 200, {{"ok", true}});
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
    // 204 means NO CONTENT: no body, and therefore Content-Length: 0. Decided
    // HERE rather than at the write below, so the header and the bytes cannot
    // disagree — a Content-Length that promises more than arrives is how a
    // client ends up waiting forever for a response it already has.
    const QByteArray payload =
        (status == 204) ? QByteArray()
                        : QJsonDocument(body).toJson(QJsonDocument::Compact);

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
    case 204: reason = "No Content";   break; // the CORS preflight (v30.2.1)
    case 429: reason = "Too Many Requests"; break;
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

    // ---- CORS (v30.2.1) ----------------------------------------------------
    // Allow-Origin alone was never enough. A browser preflights any request
    // carrying Content-Type: application/json or an Authorization header —
    // which is every call this API has — and refuses the real request unless
    // the preflight names the METHOD and the HEADERS it is about to use.
    // Without these three lines a web client fails before its request is
    // sent, and the failure looks like the server being down.
    //
    // `*` is safe for Allow-Origin here specifically because this API
    // authenticates with a bearer HEADER and never a cookie: a browser will
    // not send credentials to a wildcard origin, and we do not ask it to.
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    // Cache the preflight so a browser does not send one before every single
    // call — a chatty sync would otherwise double its request count.
    response += "Access-Control-Max-Age: 86400\r\n";

    response += "\r\n";
    response += payload; // empty for 204, per the Content-Length above

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}
