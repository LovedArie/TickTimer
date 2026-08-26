#include "SyncDialog.h"
#include "Widgets.h" // isCompactScreen

#include "SyncService.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SyncDialog::SyncDialog(SyncService* sync, QWidget* parent)
    : QDialog(parent)
    , m_sync(sync)
{
    setWindowTitle(tr("Sync"));
    setModal(true);
    setMinimumWidth(isCompactScreen() ? 0 : 380); // v30.7: 380 > a 360px phone

    auto* title = new QLabel(tr("Sync with your server"), this);
    title->setObjectName("dialogTitle");

    m_info = new QLabel(this);
    m_info->setObjectName("sub");

    m_status = new QLabel(this);
    m_status->setWordWrap(true);

    m_syncBtn = new QPushButton(tr("Sync now"), this);
    m_syncBtn->setObjectName("primary");
    m_syncBtn->setDefault(true);

    // The conflict box exists from the start but stays HIDDEN until the
    // service says a human choice is needed — simpler than building widgets
    // on demand, and the layout already knows its size.
    m_conflictBox = new QWidget(this);
    auto* conflictLayout = new QVBoxLayout(m_conflictBox);
    conflictLayout->setContentsMargins(0, 8, 0, 0);
    auto* conflictText = new QLabel(
        tr("Both this device and the server have changes. Which version "
           "should win? (The other one is overwritten.)"),
        m_conflictBox);
    conflictText->setWordWrap(true);
    auto* useServer = new QPushButton(tr("Use server version"), m_conflictBox);
    auto* keepMine  = new QPushButton(tr("Keep mine (overwrite server)"),
                                      m_conflictBox);
    conflictLayout->addWidget(conflictText);
    conflictLayout->addWidget(useServer);
    conflictLayout->addWidget(keepMine);
    m_conflictBox->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(m_info);
    layout->addWidget(m_syncBtn);
    layout->addWidget(m_conflictBox);
    layout->addWidget(m_status);

    connect(m_syncBtn, &QPushButton::clicked, this, [this]() {
        m_syncBtn->setEnabled(false); // braces; the service's m_busy is the belt
        m_conflictBox->hide();
        m_sync->syncNow();
    });
    connect(useServer, &QPushButton::clicked, this, [this]() {
        m_conflictBox->hide();
        m_sync->resolveUseServer();
    });
    connect(keepMine, &QPushButton::clicked, this, [this]() {
        m_conflictBox->hide();
        m_sync->resolveKeepMine();
    });

    connect(m_sync, &SyncService::statusChanged,
            m_status, &QLabel::setText);
    connect(m_sync, &SyncService::finished, this,
            [this](bool, const QString& message) {
                m_status->setText(message);
                m_syncBtn->setEnabled(true);
                refreshInfo();
            });
    connect(m_sync, &SyncService::conflictDetected, this, [this](int) {
        m_status->clear();
        m_conflictBox->show();
        m_syncBtn->setEnabled(true);
    });

    refreshInfo();

    // Auto-sync can hold a conflict from BEFORE this dialog existed —
    // signals only reach the living, so ask the STATE on open. (Bug
    // confession: this block first shipped INSIDE the finished-lambda
    // above — an anchored text edit matched the first refreshInfo() in
    // the file, not the ctor's. It compiled, tests passed, and the owner
    // met a ⚠ button with a dialog that offered no choice. Position bugs
    // survive compilers; only reading the diff catches them.)
    if (m_sync->hasPendingConflict()) {
        m_status->setText(tr("A background sync found a conflict — "
                             "choose which version wins."));
        m_conflictBox->show();
    }
}

void SyncDialog::refreshInfo()
{
    // Human words, human time (owner feedback: "revision 0" read as
    // "synced zero times"). The revision still exists for machines and
    // tests; people get a clock.
    const QDateTime t = m_sync->lastSyncTime();
    QString when;
    if (!t.isValid())
        when = tr("never on this device");
    else if (t.date() == QDate::currentDate())
        when = tr("today at %1").arg(t.time().toString(tr("h:mm AP")));
    else if (t.date() == QDate::currentDate().addDays(-1))
        when = tr("yesterday at %1").arg(t.time().toString(tr("h:mm AP")));
    else
        when = t.toString(tr("d MMM, h:mm AP"));

    m_info->setText(m_sync->dirty()
                        ? tr("Changes waiting to sync · last synced: %1")
                              .arg(when)
                        : tr("Everything synced · last synced: %1").arg(when));
}
