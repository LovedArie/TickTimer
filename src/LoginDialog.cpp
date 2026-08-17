#include "LoginDialog.h"

#include <QLabel>
#include <QLineEdit>
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
    layout->setSpacing(10);
    layout->addWidget(m_title);
    layout->addWidget(m_username);
    layout->addWidget(m_password);
    layout->addWidget(m_submit);
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
    m_status->setText(tr("Contacting server…"));
    if (m_registerMode)
        m_client->registerUser(user, pass);
    else
        m_client->login(user, pass);
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

void LoginDialog::onResult(AuthClient::Outcome outcome,
                           const QString& username, const QString& token)
{
    setBusy(false);
    switch (outcome) {
    case AuthClient::Outcome::Success:
        m_user  = username;
        m_token = token;
        accept(); // the gate opens — main() shows the app
        return;
    case AuthClient::Outcome::UsernameTaken:
        m_status->setText(tr("That username is taken. Try another."));
        return;
    case AuthClient::Outcome::BadCredentials:
        m_status->setText(tr("Wrong username or password."));
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
        // The most likely dev-time failure gets the most actionable message:
        // it points straight at the usual cause.
        m_status->setText(tr("Can't reach the server. Is it running, and is "
                             "the address correct?"));
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
}
