#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// ---------------------------------------------------------------------------
// AuthClient — the DESKTOP side of the auth conversation. Wraps
// QNetworkAccessManager (Qt's HTTP client) so the login dialog deals in
// "register(user, pass)" and a result signal, never in sockets and JSON.
//
// The key Qt lesson here is ASYNC. A network call can take a second or fail;
// blocking the UI thread while it does would freeze the whole app. So
// QNetworkAccessManager is fire-and-forget: you POST, and LATER a `finished`
// signal delivers the reply. This class translates that into a single
// `resultReady` signal carrying a clean verdict — the dialog connects to it
// and stays responsive throughout.
//
// The server URL is injected (not hard-coded): "http://localhost:8080" today,
// "http://192.168.1.x:8080" when a phone talks to your laptop, your Pi's
// address later. One setting, three deployments, zero code changes — the same
// seam philosophy as nowProvider and the AccountStore path.
// ---------------------------------------------------------------------------

class AuthClient : public QObject
{
    Q_OBJECT
public:
    explicit AuthClient(const QString& serverUrl, QObject* parent = nullptr);

    // The URL was always injected rather than hard-coded; this makes it
    // changeable at runtime too. Each request reads m_serverUrl fresh, so a
    // new address simply applies to the NEXT call — no connection to tear
    // down (our server is one-request-per-connection anyway).
    void setServerUrl(const QString& serverUrl)
    {
        m_serverUrl = normalizeServerUrl(serverUrl);
    }

    // v29.0.1 — the trailing-slash landmine, defused at every entry. A
    // pasted base URL routinely ends in '/', the client naively appended
    // "/register", the server's EXACT route match 404'd "//register", and
    // the catch-all blamed the owner's password (see TROUBLESHOOTING:
    // "Please check your details"). Normalizing here — and in SyncClient,
    // which shares the base — makes the whole class of paste unrepresentable
    // instead of asking every future field to be careful. Trims whitespace,
    // strips trailing slashes, never eats the scheme's own "//".
    static QString normalizeServerUrl(QString url);
    QString serverUrl() const { return m_serverUrl; }

    enum class Outcome {
        Success,
        UsernameTaken,     // register only
        BadCredentials,    // login only
        InvalidInput,
        TooManyAttempts,    // v30.2.1 — the server's brake is on. NOT a
                            // verdict on the password: saying "wrong
                            // password" here would have someone retyping a
                            // CORRECT one and concluding their account
                            // broke.
        InviteRequired,     // v30.2.1 — this server only takes new accounts
                            // with a code, and ours was missing or wrong
        NetworkError,       // server unreachable — the "is it even running?" case
        UnknownServerReply, // v29.0.1 — the server ANSWERED, with an error
                            // token this client doesn't know (not_found, a
                            // future version's vocabulary…). Distinct from
                            // InvalidInput because the honest advice is
                            // "check the address / versions", not "check
                            // your password" — collapsing the two cost a
                            // live debugging session.
    };
    Q_ENUM(Outcome)

    // `remember` (v30.2) asks the server to ALSO mint a durable device token,
    // returned alongside the session token. Opt-in and defaulted off: a login
    // on someone else's machine must not quietly leave a credential behind.
    // `deviceLabel` is what a human revoking it later will read.
    // `invite` (v30.2.1) is the code a server started with `--invite` demands.
    // Empty is correct against a server that does not ask for one, which is
    // every server on a network its owner controls.
    void registerUser(const QString& username, const QString& password,
                      bool remember = false,
                      const QString& deviceLabel = QString(),
                      const QString& invite = QString());
    void login(const QString& username, const QString& password,
               bool remember = false,
               const QString& deviceLabel = QString());

    // v30.2 — trade a remembered device for a fresh session, no password.
    //
    // Note there is no username parameter: the token IS the identity, and the
    // server tells US who it belongs to. A client that had to name the account
    // could name the wrong one; this one cannot be wrong, only refused.
    void resumeSession(const QString& deviceToken);

    // Hang up: ask the server to forget this device. Fire-and-forget by
    // design — the local credential is cleared either way, so a revoke that
    // never lands must not strand someone in a half-logged-out state. The
    // server's copy is idempotent, so a retry later costs nothing.
    void revokeDevice(const QString& deviceToken);

signals:
    // One signal for both calls: the dialog shows a spinner, then reacts to
    // whatever comes back. `username` echoes back so a caller who fired
    // several requests knows which one resolved.
    // `token` is the session pass the server minted on success (empty on
    // failure): the password proved identity ONCE, at this moment; every
    // later call (sync!) carries this stand-in instead, so the password
    // never crosses the wire again. Still in memory only, and still dies with
    // the process — v30.2 did NOT change that. What changed is that the app
    // no longer necessarily logs in fresh each launch: a remembered device
    // trades its durable token for a new one of these instead.
    // `deviceToken` (v30.2) is the durable credential, non-empty only when a
    // caller asked to be remembered AND the server obliged. Empty everywhere
    // else, including every resume — a resume SPENDS a device token, it does
    // not mint another, so nothing here silently multiplies credentials.
    //
    // On a resume, `username` is the server's answer rather than an echo:
    // the client did not know whose token it was holding.
    void resultReady(AuthClient::Outcome outcome, const QString& username,
                     const QString& token, const QString& deviceToken);

private:
    // One POST for every route. Takes the body already built rather than
    // username+password, because /session carries neither — and a second
    // hand-written request/reply path is exactly how the connection-cache and
    // deferred-emit lessons below would get re-learned the hard way.
    void post(const QString& path, const QJsonObject& body,
              const QString& usernameHint);

    QNetworkAccessManager m_net;
    QString               m_serverUrl;
};
