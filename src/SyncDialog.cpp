#include "SyncDialog.h"

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
    setMinimumWidth(380);

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
}

void SyncDialog::refreshInfo()
{
    m_info->setText(m_sync->dirty()
                        ? tr("Local changes not yet on the server · last "
                             "synced revision %1").arg(m_sync->lastRevision())
                        : tr("Everything synced · revision %1")
                              .arg(m_sync->lastRevision()));
}
