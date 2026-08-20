#pragma once

#include "AuthClient.h"

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;

// ---------------------------------------------------------------------------
// LoginDialog — the app's front door. Two modes in one dialog (log in / create
// account) because they share the same two fields; a toggle link flips between
// them, which is friendlier than two separate windows.
//
// It owns an AuthClient and is purely REACTIVE: type, submit, disable the
// buttons while the request is in flight, then respond to resultReady. The
// dialog never blocks — that's the whole reason the client is async.
//
// On success it accept()s; main() only shows the main window if the dialog was
// accepted. That makes login a genuine gate without tangling auth logic into
// MainWindow — the dialog is a checkpoint, not a dependency the rest of the
// app has to know about.
// ---------------------------------------------------------------------------

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(const QString& serverUrl, QWidget* parent = nullptr);

    // The name that logged in — main() passes it on so the app can greet the
    // user / scope future synced data to them.
    QString loggedInUser() const { return m_user; }
    // The session token the server minted — main() hands it to the sync
    // machinery. Memory only; never persisted.
    QString authToken() const { return m_token; }
    // The server address that was ACTUALLY used (the person may have edited
    // it on this very screen). main() must ask us rather than re-using the
    // value it read from settings before the dialog ran — the dialog is now
    // the owner of that decision.
    QString serverUrl() const;

    // v30.2 — the gate opened WITHOUT the server. The app should run on this
    // machine's local planner and leave sync switched off until the server
    // answers again. authToken() is empty in this case, which is the reason
    // main() must ask this rather than infer it from an empty token.
    bool offline() const { return m_offline; }

    // The durable credential the server minted, if the person asked to be
    // remembered. Empty otherwise, and empty on every resume — a resume
    // spends one of these, it never mints another.
    QString deviceToken() const { return m_deviceToken; }

private slots:
    void submit();
    void toggleMode();
    void onResult(AuthClient::Outcome outcome, const QString& username,
                  const QString& token, const QString& deviceToken);

private:
    void setBusy(bool busy);

    // Try the remembered device before showing anyone a form. Called once,
    // from the constructor: on success the dialog accepts before it is ever
    // seen, which is the whole point of remembering a device.
    void tryResume();

    // Offer to open <username>'s local planner with no server. Shown only
    // when the server could not be REACHED — never when it answered and said
    // no, because a refused credential is not an invitation to work offline.
    void offerOffline(const QString& username);

    AuthClient*  m_client;
    bool         m_registerMode = false; // false = login, true = create account
    bool         m_offline      = false; // accepted without a server
    QString      m_user;
    QString      m_token;
    QString      m_deviceToken;
    QString      m_resumingUser;         // whose token tryResume() is spending

    QLineEdit*   m_username = nullptr;
    QLineEdit*   m_password = nullptr;
    QLineEdit*   m_server   = nullptr; // editable server address (persisted)
    QLabel*      m_title    = nullptr;
    QLabel*      m_status   = nullptr;
    QPushButton* m_submit   = nullptr;
    QPushButton* m_toggle   = nullptr;
    QCheckBox*   m_remember = nullptr; // ask the server for a device token
    QPushButton* m_offlineBtn = nullptr; // appears only when the server is
                                         // unreachable and local data exists
};
