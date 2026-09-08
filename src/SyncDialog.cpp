#include "SyncDialog.h"
#include "Widgets.h" // isCompactScreen

#include "SyncService.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

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
    // v31.2 — THE WORDS CHANGED BECAUSE THE BEHAVIOUR DID.
    //
    // This used to say "Which version should win? (The other one is
    // overwritten.)" — true then, and the reason a device could lose a whole
    // day's work for having added something different. Everything both sides
    // did independently is merged before this box is ever shown, so the only
    // thing left to decide is the handful of rows edited in both places.
    // Saying "the other one is overwritten" now would be a lie that makes
    // people afraid of the safe choice.
    // ---- half one: the rows that ARE a question ------------------------
    m_contestedBox = new QWidget(m_conflictBox);
    auto* contestedLayout = new QVBoxLayout(m_contestedBox);
    contestedLayout->setContentsMargins(0, 0, 0, 0);

    auto* conflictText = new QLabel(
        tr("Everything both devices changed separately has been merged. "
           "A few items were edited in both places — pick which version "
           "of those wins:"),
        m_contestedBox);
    conflictText->setWordWrap(true);

    // The items themselves, and WHICH SIDE each one came from. A choice
    // between two unnamed versions is a coin toss; naming the rows made it a
    // decision, and naming the two sides is what makes it an informed one.
    m_clashList = new QLabel(m_contestedBox);
    m_clashList->setWordWrap(true);
    m_clashList->setStyleSheet(QStringLiteral("color:#616974;"));

    auto* useServer = new QPushButton(tr("Use the server's"), m_contestedBox);
    auto* keepMine  = new QPushButton(tr("Keep mine"), m_contestedBox);
    contestedLayout->addWidget(conflictText);
    contestedLayout->addWidget(m_clashList);
    contestedLayout->addWidget(useServer);
    contestedLayout->addWidget(keepMine);

    // ---- half two: the rows that are merely REPORTED --------------------
    // Below the buttons, because they answer "what else happened?" and not
    // "what should I press?".
    m_settledBox = new QWidget(m_conflictBox);
    auto* settledLayout = new QVBoxLayout(m_settledBox);
    settledLayout->setContentsMargins(0, 8, 0, 0);

    auto* settledText = new QLabel(
        tr("Kept automatically — a delete never overrides an edit. "
           "Neither button changes these:"),
        m_settledBox);
    settledText->setWordWrap(true);

    m_settledList = new QLabel(m_settledBox);
    m_settledList->setWordWrap(true);
    m_settledList->setStyleSheet(QStringLiteral("color:#616974;"));
    settledLayout->addWidget(settledText);
    settledLayout->addWidget(m_settledList);

    conflictLayout->addWidget(m_contestedBox);
    conflictLayout->addWidget(m_settledBox);
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
        showClashes(); // name the rows BEFORE showing the box
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
        m_status->setText(tr("A background sync merged what it could — "
                             "a few items need your call."));
        showClashes();
        m_conflictBox->show();
    }
}

void SyncDialog::showClashes()
{
    QVector<merge::Clash> all = m_sync->pendingClashes();

    // SORTED, because merge::plan walks its ids out of a QSet and a QSet has
    // no order to promise. Without this the same conflict lists itself in a
    // different order every time it is shown, which reads as a different
    // conflict — and no test could pin the text either. Collection first, so
    // the grouping renderClashes does is contiguous.
    std::sort(all.begin(), all.end(),
              [](const merge::Clash& a, const merge::Clash& b) {
                  if (a.collection != b.collection)
                      return a.collection < b.collection;
                  if (a.label != b.label)
                      return a.label < b.label;
                  return a.id < b.id;
              });

    // The split the whole box is built around: what the buttons decide, and
    // what has already been decided for you.
    QVector<merge::Clash> contested;
    QVector<merge::Clash> settled;
    for (const merge::Clash& c : all)
        (merge::decidedByPreference(c.kind) ? contested : settled) << c;

    m_clashList->setText(merge::renderClashes(contested));
    m_contestedBox->setVisible(!contested.isEmpty());

    m_settledList->setText(merge::renderClashes(settled));
    m_settledBox->setVisible(!settled.isEmpty());
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
