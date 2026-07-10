#pragma once

#include "AuthClient.h"

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;

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

private slots:
    void submit();
    void toggleMode();
    void onResult(AuthClient::Outcome outcome, const QString& username,
                  const QString& token);

private:
    void setBusy(bool busy);

    AuthClient*  m_client;
    bool         m_registerMode = false; // false = login, true = create account
    QString      m_user;
    QString      m_token;

    QLineEdit*   m_username = nullptr;
    QLineEdit*   m_password = nullptr;
    QLineEdit*   m_server   = nullptr; // editable server address (persisted)
    QLabel*      m_title    = nullptr;
    QLabel*      m_status   = nullptr;
    QPushButton* m_submit   = nullptr;
    QPushButton* m_toggle   = nullptr;
};
