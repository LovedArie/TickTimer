#pragma once

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
    void setServerUrl(const QString& serverUrl) { m_serverUrl = serverUrl; }
    QString serverUrl() const { return m_serverUrl; }

    enum class Outcome {
        Success,
        UsernameTaken,     // register only
        BadCredentials,    // login only
        InvalidInput,
        NetworkError       // server unreachable — the "is it even running?" case
    };
    Q_ENUM(Outcome)

    void registerUser(const QString& username, const QString& password);
    void login(const QString& username, const QString& password);

signals:
    // One signal for both calls: the dialog shows a spinner, then reacts to
    // whatever comes back. `username` echoes back so a caller who fired
    // several requests knows which one resolved.
    // `token` is the session pass the server minted on success (empty on
    // failure): the password proved identity ONCE, at this moment; every
    // later call (sync!) carries this stand-in instead, so the password
    // never crosses the wire again. In memory only — the app logs in fresh
    // each launch anyway.
    void resultReady(AuthClient::Outcome outcome, const QString& username,
                     const QString& token);

private:
    void post(const QString& path, const QString& username,
              const QString& password);

    QNetworkAccessManager m_net;
    QString               m_serverUrl;
};
