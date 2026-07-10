#include "CompareDialog.h"

#include "Compare.h"
#include "JsonStore.h"
#include "Stats.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
// "+25m" / "−12m" / "0s" — deltas need their sign to mean anything; the
// shared stats::formatSeconds shows magnitudes, so the sign is added here,
// in the one place deltas are displayed.
QString formatDelta(qint64 seconds)
{
    if (seconds == 0)
        return QStringLiteral("—");
    const QString magnitude = stats::formatSeconds(qAbs(seconds));
    return (seconds > 0 ? QStringLiteral("+") : QStringLiteral("−"))
           + magnitude;
}
} // namespace

CompareDialog::CompareDialog(const AppData* mine, const QString& peerName,
                             const QJsonObject& peerBlob, QWidget* parent)
    : QDialog(parent)
    , m_mine(mine)
    , m_peerName(peerName)
    , m_day(QDate::currentDate())
{
    // Pour the wire blob into the private snapshot. announceChange=false:
    // nothing subscribes to m_peer, and we're constructing, not mutating —
    // there is no "before" for a change signal to be relative to.
    JsonStore::applyJsonObject(m_peer, peerBlob, /*announceChange=*/false);

    setWindowTitle(tr("Compare with %1").arg(m_peerName));
    setModal(true);
    setMinimumWidth(420);

    auto* title = new QLabel(tr("You vs %1").arg(m_peerName), this);
    title->setObjectName("dialogTitle");

    // Date navigation: yesterday / label / tomorrow. No calendar popup —
    // comparing is a "how are we doing lately" glance, and two arrows cover
    // that; a full picker would be UI for a need nobody has voiced yet.
    auto* prev = new QPushButton(QStringLiteral("◀"), this);
    auto* next = new QPushButton(QStringLiteral("▶"), this);
    prev->setFixedWidth(36);
    next->setFixedWidth(36);
    m_dayLabel = new QLabel(this);
    m_dayLabel->setAlignment(Qt::AlignCenter);

    auto* navRow = new QHBoxLayout;
    navRow->addWidget(prev);
    navRow->addWidget(m_dayLabel, 1);
    navRow->addWidget(next);

    // The table: QGridLayout of labels. Not QTableWidget — four fixed rows
    // of read-only numbers don't need a scrolling, selectable, editable
    // model/view tower; a grid of labels is the honest amount of machinery.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    const QStringList rowNames = {tr("Focus"), tr("Break"),
                                  tr("Distracted"), tr("Total")};
    const QStringList colNames = {tr("You"), m_peerName, tr("Δ")};
    for (int c = 0; c < colNames.size(); ++c) {
        auto* head = new QLabel(colNames[c], this);
        head->setObjectName("sub");
        grid->addWidget(head, 0, c + 1, Qt::AlignRight);
    }
    for (int r = 0; r < rowNames.size(); ++r) {
        grid->addWidget(new QLabel(rowNames[r], this), r + 1, 0);
        for (int c = 0; c < 3; ++c) {
            m_cells[r][c] = new QLabel(this);
            m_cells[r][c]->setAlignment(Qt::AlignRight);
            grid->addWidget(m_cells[r][c], r + 1, c + 1);
        }
    }

    m_headline = new QLabel(this);
    m_headline->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addLayout(navRow);
    layout->addLayout(grid);
    layout->addWidget(m_headline);

    connect(prev, &QPushButton::clicked, this,
            [this]() { showDay(m_day.addDays(-1)); });
    connect(next, &QPushButton::clicked, this,
            [this]() { showDay(m_day.addDays(1)); });

    refresh();
}

void CompareDialog::showDay(const QDate& day)
{
    m_day = day;
    refresh();
}

void CompareDialog::refresh()
{
    m_dayLabel->setText(m_day == QDate::currentDate()
                            ? tr("Today")
                            : m_day.toString(QStringLiteral("ddd d MMM")));

    // The punchline of the whole feature: the SAME pure summarizer runs on
    // both datasets. Nothing here knows or cares that one AppData is live
    // and the other arrived over a socket ten seconds ago.
    const stats::Totals mine =
        stats::summarizeDay(*m_mine, m_day).totals;
    const stats::Totals theirs =
        stats::summarizeDay(m_peer, m_day).totals;
    const compare::Delta d = compare::delta(mine, theirs);

    const qint64 mineVals[4]   = {mine.focusSeconds, mine.breakSeconds,
                                  mine.distractedSeconds, mine.total()};
    const qint64 theirsVals[4] = {theirs.focusSeconds, theirs.breakSeconds,
                                  theirs.distractedSeconds, theirs.total()};
    const qint64 deltaVals[4]  = {d.focusSeconds, d.breakSeconds,
                                  d.distractedSeconds, d.totalSeconds};
    for (int r = 0; r < 4; ++r) {
        m_cells[r][0]->setText(stats::formatSeconds(mineVals[r]));
        m_cells[r][1]->setText(stats::formatSeconds(theirsVals[r]));
        m_cells[r][2]->setText(formatDelta(deltaVals[r]));
    }

    // The verdict judges FOCUS only, with compare::focusVerdict's tolerance —
    // and the wording stays gentle on purpose. This feature is "we're in it
    // together", not a leaderboard.
    switch (compare::focusVerdict(d)) {
    case compare::Verdict::Ahead:
        m_headline->setText(tr("You've focused %1 more than %2 — nice.")
                                .arg(stats::formatSeconds(d.focusSeconds),
                                     m_peerName));
        break;
    case compare::Verdict::Behind:
        m_headline->setText(tr("%1 has focused %2 more than you today — "
                               "good moment to start a block?")
                                .arg(m_peerName,
                                     stats::formatSeconds(-d.focusSeconds)));
        break;
    case compare::Verdict::Even:
        m_headline->setText(tr("You and %1 are about even.").arg(m_peerName));
        break;
    }
}
