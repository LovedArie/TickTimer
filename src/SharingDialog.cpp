#include "SharingDialog.h"

#include "CompareDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

SharingDialog::SharingDialog(ShareClient* client, AppData* myData,
                             TrackerService* tracker, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
    , m_myData(myData)
    , m_tracker(tracker)
{
    setWindowTitle(tr("Share & compare"));
    setModal(true);
    setMinimumWidth(420);

    auto* title = new QLabel(tr("Share & compare planners"), this);
    title->setObjectName("dialogTitle");

    // The privacy fact, stated where the decision is made, not buried in a
    // doc: a grant exposes the WHOLE planner (titles included), because the
    // server can't extract "just the totals" from a blob it never reads.
    auto* privacy = new QLabel(
        tr("Sharing lets someone see your whole planner (read-only). "
           "You can stop sharing at any time."),
        this);
    privacy->setObjectName("sub");
    privacy->setWordWrap(true);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("username to share with"));
    m_shareBtn = new QPushButton(tr("Share"), this);
    m_shareBtn->setObjectName("primary");
    auto* shareRow = new QHBoxLayout;
    shareRow->addWidget(m_nameEdit, 1);
    shareRow->addWidget(m_shareBtn);

    auto* iShareTitle = new QLabel(tr("I share with"), this);
    m_iShareList = new QVBoxLayout;
    auto* withMeTitle = new QLabel(tr("Shared with me"), this);
    m_withMeList = new QVBoxLayout;

    m_status = new QLabel(this);
    m_status->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(privacy);
    layout->addLayout(shareRow);
    layout->addWidget(iShareTitle);
    layout->addLayout(m_iShareList);
    layout->addWidget(withMeTitle);
    layout->addLayout(m_withMeList);
    layout->addWidget(m_status);
    layout->addStretch(1);

    // Pressing Enter in the field is the same as clicking Share — the
    // returnPressed/clicked pair every login form teaches.
    connect(m_shareBtn, &QPushButton::clicked, this, [this]() {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty())
            return;
        m_shareBtn->setEnabled(false);
        m_status->setText(tr("Sharing with %1…").arg(name));
        m_client->share(name);
    });
    connect(m_nameEdit, &QLineEdit::returnPressed,
            m_shareBtn, &QPushButton::click);

    // After any grant/revoke, re-fetch rather than patching the lists
    // locally: the server's answer IS the truth, and re-asking can never
    // drift from it. (Cheap at this scale; correctness first.)
    connect(m_client, &ShareClient::shareUpdated, this,
            [this](ShareClient::Outcome outcome) {
                m_shareBtn->setEnabled(true);
                switch (outcome) {
                case ShareClient::Outcome::Success:
                    m_status->setText(tr("Done."));
                    m_nameEdit->clear();
                    refresh();
                    break;
                case ShareClient::Outcome::NotFound:
                    m_status->setText(tr("No account with that name — "
                                         "check the spelling."));
                    break;
                case ShareClient::Outcome::AuthError:
                    m_status->setText(tr("Session expired — please restart "
                                         "the app and log in again."));
                    break;
                default:
                    m_status->setText(tr("Could not reach the server."));
                    break;
                }
            });

    connect(m_client, &ShareClient::sharesReady, this,
            [this](ShareClient::Outcome outcome,
                   const QStringList& iShareWith,
                   const QStringList& sharedWithMe) {
                if (outcome != ShareClient::Outcome::Success) {
                    m_status->setText(tr("Could not load sharing info."));
                    return;
                }
                rebuildLists(iShareWith, sharedWithMe);
            });

    connect(m_client, &ShareClient::peerPlannerReady, this,
            [this](ShareClient::Outcome outcome, const QString& user,
                   const QJsonObject& data) {
                if (outcome == ShareClient::Outcome::Forbidden) {
                    // Race made visible: they revoked between our list load
                    // and the click. Refresh so the UI tells the new truth.
                    m_status->setText(
                        tr("%1 is no longer sharing with you.").arg(user));
                    refresh();
                    return;
                }
                if (outcome != ShareClient::Outcome::Success) {
                    m_status->setText(tr("Could not fetch %1's planner.")
                                          .arg(user));
                    return;
                }
                m_status->clear();
                openCompare(user, data);
            });

    refresh();
}

void SharingDialog::refresh()
{
    m_status->setText(tr("Loading…"));
    m_client->fetchShares();
}

void SharingDialog::clearLayout(QVBoxLayout* layout)
{
    // Standard Qt idiom for emptying a layout: take items until none remain,
    // deleting each item's widget. deleteLater, not delete — the click that
    // triggered a rebuild may still be unwinding through one of these very
    // widgets (the add-a-task crash, section J, never again).
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void SharingDialog::rebuildLists(const QStringList& iShareWith,
                                 const QStringList& sharedWithMe)
{
    clearLayout(m_iShareList);
    clearLayout(m_withMeList);

    if (iShareWith.isEmpty()) {
        auto* none = new QLabel(tr("nobody yet"), this);
        none->setObjectName("sub");
        m_iShareList->addWidget(none);
    }
    for (const QString& name : iShareWith) {
        auto* row    = new QWidget(this);
        auto* h      = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* label  = new QLabel(name, row);
        auto* remove = new QPushButton(tr("Stop sharing"), row);
        h->addWidget(label, 1);
        h->addWidget(remove);
        connect(remove, &QPushButton::clicked, this, [this, name]() {
            m_status->setText(tr("Removing %1…").arg(name));
            m_client->unshare(name);
        });
        m_iShareList->addWidget(row);
    }

    if (sharedWithMe.isEmpty()) {
        auto* none = new QLabel(
            tr("nobody yet — ask them to share with your username"), this);
        none->setObjectName("sub");
        m_withMeList->addWidget(none);
    }
    for (const QString& name : sharedWithMe) {
        auto* row     = new QWidget(this);
        auto* h       = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* label   = new QLabel(name, row);
        auto* compare = new QPushButton(tr("Compare"), row);
        h->addWidget(label, 1);
        h->addWidget(compare);
        connect(compare, &QPushButton::clicked, this, [this, name]() {
            m_status->setText(tr("Fetching %1's planner…").arg(name));
            m_client->fetchPeerPlanner(name);
        });
        m_withMeList->addWidget(row);
    }

    m_status->clear();
}

void SharingDialog::openCompare(const QString& user,
                                const QJsonObject& peerBlob)
{
    // Stack dialog parented to US — we outlive its exec() by definition
    // (the A1 rule: a stack dialog's parent must be guaranteed to survive it).
    CompareDialog dialog(m_myData, m_tracker, user, peerBlob, this);
    dialog.exec();
}
