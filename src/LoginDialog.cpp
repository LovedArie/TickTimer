#include "LoginDialog.h"

#include "SessionStore.h"

#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QSettings>
#include <QPushButton>
#include <QVBoxLayout>

LoginDialog::LoginDialog(const QString& serverUrl, QWidget* parent)
    : QDialog(parent)
    , m_client(new AuthClient(serverUrl, this))
{
    setWindowTitle(tr("TickTimer"));
    setModal(true);

    m_title = new QLabel(this);
    m_title->setObjectName("dialogTitle");

    m_username = new QLineEdit(this);
    m_username->setPlaceholderText(tr("Username"));

    m_password = new QLineEdit(this);
    m_password->setPlaceholderText(tr("Password"));
    // Password mode dots-out the field — the one QLineEdit setting that is a
    // security feature, not a cosmetic one (shoulder-surfing).
    m_password->setEchoMode(QLineEdit::Password);

    m_status = new QLabel(this);
    m_status->setObjectName("sub");
    m_status->setWordWrap(true);

    m_submit = new QPushButton(this);
    m_submit->setObjectName("primary");
    m_submit->setDefault(true); // Enter submits

    m_toggle = new QPushButton(this);
    m_toggle->setFlat(true);
    m_toggle->setObjectName("quiet");
    m_toggle->setCursor(Qt::PointingHandCursor);

    // The server address, editable right where it matters. Until now it
    // lived only in QSettings with no UI — fine on the dev machine, a wall
    // for anyone else: a fresh install (your girlfriend's PC) points at
    // localhost, where no server lives, and she'd have no way to fix it
    // short of the registry. First-run configuration belongs on the first
    // screen the person sees.
    auto* serverLabel = new QLabel(tr("Server"), this);
    serverLabel->setObjectName("sub");
    m_server = new QLineEdit(this);
    m_server->setText(serverUrl.trimmed());
    m_server->setPlaceholderText(QStringLiteral("http://192.168.1.20:8080"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    // v30.2 — "remember this device". Default ON: the phone this exists for
    // has one owner, and asking her for a password at every launch is how an
    // app stops being opened. A shared desktop is the case where it should be
    // unticked, which is exactly the case where someone is present to untick
    // it. (See SessionStore.h for what is actually stored, and what is not.)
    // v30.2.1 — the invite code, for a server started with --invite. Shown
    // in register mode only (toggleMode hides it), because a server that
    // asks for one asks only when an account is being CREATED.
    m_invite = new QLineEdit(this);
    m_invite->setObjectName("loginInvite");
    m_invite->setPlaceholderText(tr("Invite code (only if you were given one)"));
    m_invite->hide();

    m_remember = new QCheckBox(tr("Remember this device"), this);
    m_remember->setObjectName("loginRemember");
    m_remember->setChecked(true);

    // The offline door. Built hidden and shown only when the server proves
    // unreachable AND this machine has data for the account in question —
    // an offer to open a planner that does not exist is worse than no offer.
    m_offlineBtn = new QPushButton(this);
    m_offlineBtn->setObjectName("loginOffline");
    m_offlineBtn->hide();

    layout->setSpacing(10);
    layout->addWidget(m_title);
    layout->addWidget(m_username);
    layout->addWidget(m_password);
    layout->addWidget(m_invite);
    layout->addWidget(m_remember);
    layout->addWidget(m_submit);
    layout->addWidget(m_offlineBtn);
    layout->addWidget(m_toggle);
    layout->addWidget(serverLabel);
    layout->addWidget(m_server);
    layout->addWidget(m_status);

    connect(m_submit,   &QPushButton::clicked, this, &LoginDialog::submit);
    connect(m_toggle,   &QPushButton::clicked, this, &LoginDialog::toggleMode);
    connect(m_password, &QLineEdit::returnPressed, this, &LoginDialog::submit);
    connect(m_client,   &AuthClient::resultReady, this, &LoginDialog::onResult);

    // Land on LOGIN first (owner request — and a bug confession: the old
    // three-line dance here flipped the mode TWICE and ended on register,
    // while its own comment claimed login. The UI test couldn't find a
    // "Log in" button and we worked around the symptom instead of reading
    // these three lines. Set the opposite, flip once: ends on login.)
    m_registerMode = true;
    toggleMode(); // -> false == login mode, all labels written

    // LAST: a remembered device may open the gate before anyone sees it.
    tryResume();
}
void LoginDialog::toggleMode()
{
    m_registerMode = !m_registerMode;
    m_title->setText(m_registerMode ? tr("Create your account")
                                    : tr("Welcome back"));
    m_submit->setText(m_registerMode ? tr("Create account") : tr("Log in"));
    m_toggle->setText(m_registerMode
                          ? tr("Already have an account? Log in")
                          : tr("New here? Create an account"));
    if (m_invite)
        m_invite->setVisible(m_registerMode);
    m_status->clear();
}

void LoginDialog::submit()
{
    const QString user = m_username->text().trimmed();
    const QString pass = m_password->text();
    if (user.isEmpty() || pass.isEmpty()) {
        m_status->setText(tr("Enter a username and password."));
        return;
    }

    // Apply whatever is in the server field to THIS attempt, and persist it
    // the moment it's used — persist-on-use, not persist-on-type, so a
    // half-edited address never gets saved. Next launch starts from the last
    // address that was actually tried.
    const QString url = serverUrl();
    if (!url.isEmpty()) {
        m_client->setServerUrl(url);
        QSettings().setValue(QStringLiteral("sync/serverUrl"), url);
    }

    setBusy(true);
    m_offlineBtn->hide(); // a fresh attempt supersedes the last failure's offer
    m_status->setText(tr("Contacting server…"));

    // v30.2 — the opt-in travels with the attempt. A device label goes too,
    // so the row in a future revoke list says "Arie's laptop" rather than a
    // hex string nobody can identify.
    const bool remember = m_remember->isChecked();
    if (m_registerMode)
        m_client->registerUser(user, pass, remember, session::deviceLabel(),
                               m_invite->text());
    else
        m_client->login(user, pass, remember, session::deviceLabel());
}

QString LoginDialog::serverUrl() const
{
    QString url = m_server->text().trimmed();
    // Forgive the most common omission: "192.168.1.20:8080" without the
    // scheme. QUrl treats a scheme-less string unpredictably; adding http://
    // here beats a "can't reach the server" that was really a parse failure.
    // v29.0.2 — normalize where the value is BORN. v29.0.1 normalized
    // inside AuthClient, which made login tolerate a trailing slash while
    // quietly SAVING the raw slash into settings — arming the same bug in
    // every downstream consumer (ShareClient found it within hours). This
    // function is the source every save and every service inherits from;
    // clean here means clean everywhere, forever, including consumers not
    // written yet.
    if (!url.isEmpty() && !url.contains(QStringLiteral("://")))
        url.prepend(QStringLiteral("http://"));
    return AuthClient::normalizeServerUrl(url);
}

void LoginDialog::tryResume()
{
    const QString user  = session::lastUser();
    const QString token = session::deviceToken(user);
    if (user.isEmpty() || token.isEmpty())
        return; // nobody remembered here — the form stands as it always did

    m_resumingUser = user;
    setBusy(true);
    m_status->setText(tr("Signing you in as %1…").arg(user));
    m_client->resumeSession(token);
}

void LoginDialog::offerOffline(const QString& username)
{
    const QString name = username.trimmed().toLower();
    // Only offer what exists. Opening a planner this machine has never seen
    // would greet someone with an empty week and call it their data.
    if (name.isEmpty() || !session::localAccounts().contains(name)) {
        m_offlineBtn->hide();
        return;
    }

    m_offlineBtn->setText(tr("Work offline as %1").arg(name));
    m_offlineBtn->show();
    // Rebuilt each time rather than connected once: the button's meaning
    // changes with the name, and a stale capture would open the wrong
    // account's planner — the one bug this feature must not have.
    disconnect(m_offlineBtn, &QPushButton::clicked, nullptr, nullptr);
    connect(m_offlineBtn, &QPushButton::clicked, this, [this, name]() {
        m_user    = name;
        m_token.clear();       // there is no session; saying so is the point
        m_offline = true;
        accept();
    });
}

void LoginDialog::onResult(AuthClient::Outcome outcome,
                           const QString& username, const QString& token,
                           const QString& deviceToken)
{
    setBusy(false);
    const bool wasResume = !m_resumingUser.isEmpty();
    const QString resumingUser = m_resumingUser;
    m_resumingUser.clear(); // one attempt; anything after this is a real login

    switch (outcome) {
    case AuthClient::Outcome::Success:
        m_user        = username;
        m_token       = token;
        m_deviceToken = deviceToken; // empty on a resume, by design
        accept(); // the gate opens — main() shows the app
        return;
    case AuthClient::Outcome::UsernameTaken:
        m_status->setText(tr("That username is taken. Try another."));
        return;
    case AuthClient::Outcome::BadCredentials:
        if (wasResume) {
            // The server ANSWERED and refused: this device was revoked, or
            // the account is gone. Forget the dead credential immediately —
            // retrying it every launch would be a permanent slow failure —
            // and fall back to the form. Deliberately NOT an offline offer:
            // a refused credential is not an invitation to work offline.
            session::clearDeviceToken(resumingUser);
            m_username->setText(resumingUser);
            m_status->setText(tr("This device isn't remembered any more. "
                                 "Please log in again."));
            m_password->setFocus();
            return;
        }
        m_status->setText(tr("Wrong username or password."));
        return;
    case AuthClient::Outcome::InviteRequired:
        m_status->setText(tr("This server needs an invite code to create an "
                             "account. Ask whoever runs it for one."));
        m_invite->setFocus();
        return;
    case AuthClient::Outcome::TooManyAttempts:
        // Deliberately NOT "wrong password": the brake may well have caught
        // a correct one, and telling someone their details are wrong would
        // have them retyping something that was right all along.
        m_status->setText(tr("Too many attempts. Wait a few minutes and try "
                             "again."));
        return;
    case AuthClient::Outcome::InvalidInput:
        m_status->setText(tr("Please check your details and try again."));
        return;
    case AuthClient::Outcome::UnknownServerReply:
        m_status->setText(tr("The server answered, but not in a way this "
                             "app understands. Check the server address — "
                             "just http://host:port, nothing after it — "
                             "and that app and server versions match."));
        return;
    case AuthClient::Outcome::NetworkError:
        // The one outcome that means "we never got an answer", and therefore
        // the only one where working offline is honest. Whose planner we
        // offer depends on which attempt failed: a resume already knows the
        // name, a manual login knows only what was typed.
        offerOffline(wasResume ? resumingUser : m_username->text());
        if (m_offlineBtn->isVisible()) {
            m_status->setText(tr("Can't reach the server. You can work "
                                 "offline on this device — your changes will "
                                 "sync when it's back."));
        } else {
            m_status->setText(tr("Can't reach the server. Is it running, and "
                                 "is the address correct?"));
        }
        return;
    }
}
void LoginDialog::setBusy(bool busy)
{
    // Disable inputs while a request is in flight so the user can't fire a
    // second submit on top of the first. The async client would tolerate it,
    // but double-submits confuse humans more than machines.
    m_submit->setEnabled(!busy);
    m_toggle->setEnabled(!busy);
    m_username->setEnabled(!busy);
    m_password->setEnabled(!busy);
    m_server->setEnabled(!busy);
    if (m_invite)
        m_invite->setEnabled(!busy);
    if (m_remember)
        m_remember->setEnabled(!busy);
    if (m_offlineBtn)
        m_offlineBtn->setEnabled(!busy);
}
