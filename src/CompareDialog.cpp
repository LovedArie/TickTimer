#include "CompareDialog.h"

#include "AgendaWidget.h"
#include "Compare.h"
#include "EventDialog.h"
#include "JsonStore.h"
#include "PickActivityDialog.h"
#include "Prefs.h"
#include "Stats.h"
#include "Widgets.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace
{
// "+25m" / "−12m" / "—": deltas need their sign; the shared formatSeconds
// clamps negatives (it formats magnitudes), so the sign is added here.
QString formatDelta(qint64 seconds)
{
    if (seconds == 0)
        return QStringLiteral("—");
    const QString magnitude = stats::formatSeconds(qAbs(seconds));
    return (seconds > 0 ? QStringLiteral("+") : QStringLiteral("−"))
           + magnitude;
}
} // namespace

CompareDialog::CompareDialog(AppData* mine, TrackerService* tracker,
                             const QString& myName, const QString& peerName,
                             const QJsonObject& peerBlob, QWidget* parent)
    : QDialog(parent)
    , m_mine(mine)
    , m_tracker(tracker)
    , m_myName(myName.isEmpty() ? tr("You") : myName)
    , m_peerName(peerName)
    , m_day(QDate::currentDate())
{
    // The wire blob becomes a full snapshot AppData — after which EVERY
    // query (eventsOn, eventLabel, summarizeDay, the agenda's painting)
    // answers for the peer exactly as for you. Announce nothing: nothing
    // subscribes to a snapshot.
    JsonStore::applyJsonObject(m_peer, peerBlob, /*announceChange=*/false);

    setWindowTitle(tr("Plan with %1").arg(m_peerName));
    setModal(true);
    resize(1020, 720);        // a planning surface, not a popup
    setSizeGripEnabled(true); // and the user decides how big

    auto* title = new QLabel(tr("You & %1 — side by side").arg(m_peerName),
                             this);
    title->setObjectName("dialogTitle");

    // Day navigation, unchanged from v1.
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

    // ---- the two agendas, ONE scroll ---------------------------------------
    // One QScrollArea holding both columns: they scroll together, and since
    // both are the same widget with the same slotHeight(), 09:00 on your side
    // is 09:00 on theirs to the pixel. Two scrollbars would break exactly
    // the alignment this screen exists for.
    auto* agendaHost = new QWidget;
    auto* agendaRow  = new QHBoxLayout(agendaHost);
    agendaRow->setContentsMargins(0, 0, 0, 0);
    agendaRow->setSpacing(14);

    // YOUR agenda: the live data, the real tracker — a first-class planning
    // surface, not a preview of one.
    m_myAgenda = new AgendaWidget(m_mine, m_tracker, agendaHost);
    // THEIR agenda: the snapshot, painted by the identical widget, then made
    // deaf to the mouse. It looks the same because it IS the same painter;
    // it can't be touched because events never reach it. (Merely skipping
    // the signal connections wouldn't be enough — drag-resize feedback is
    // handled inside the widget, and a block that WIGGLES when dragged but
    // never saves would be a lie. Transparent-for-mouse kills the lie at
    // the door.)
    m_peerAgenda = new AgendaWidget(&m_peer, m_tracker, agendaHost);
    m_peerAgenda->setAttribute(Qt::WA_TransparentForMouseEvents);

    agendaRow->addWidget(m_myAgenda, 1);
    agendaRow->addWidget(m_peerAgenda, 1);

    auto* scroll = new QScrollArea(this);
    makeTouchScrollable(scroll);
    scroll->setWidgetResizable(true);
    scroll->setWidget(agendaHost);

    // ---- identity headers, PINNED above the scroll (owner request) ----------
    // v2 put "You"/peer inside the scrolled host — correct at 6 AM, gone by
    // 9 PM, exactly when two look-alike columns need naming most. Headers
    // that answer "whose side is this?" belong OUTSIDE the thing that
    // scrolls, like a table's header row. They mirror the columns' layout
    // (same 1:1 stretch, same spacing) so each name sits over its agenda;
    // the right margin reserves the scrollbar's width so the second column
    // and its header agree on where the middle is.
    const auto headerCell = [&](const QString& name, const QString& note) {
        auto* cell   = new QWidget(this);
        auto* stack  = new QVBoxLayout(cell);
        stack->setContentsMargins(0, 0, 0, 0);
        stack->setSpacing(1);
        auto* head = new QLabel(name, cell);
        head->setStyleSheet(
            "font-weight:700; color:#4A505A; font-size:13px;");
        // The staleness, written where the eyes are: the peer side is
        // whatever they last PUSHED, not their live screen. One quiet
        // line kills the "why don't I see their newest block?" mystery
        // before it starts.
        auto* sub = new QLabel(note, cell);
        sub->setStyleSheet("color:#8A9098; font-size:11px;");
        stack->addWidget(head);
        stack->addWidget(sub);
        return cell;
    };
    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(14); // mirror agendaRow, column for column
    headerRow->setContentsMargins(
        0, 0, scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent), 0);
    headerRow->addWidget(
        headerCell(tr("%1 (you)").arg(m_myName), tr("live — edit freely")), 1);
    headerRow->addWidget(
        headerCell(m_peerName, tr("as of their last sync")), 1);

    // ---- the numbers, in a side column --------------------------------------
    auto* statsCol = new QVBoxLayout;
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    const QStringList rowNames = {tr("Focus"), tr("Break"),
                                  tr("Distracted"), tr("Total")};
    const QStringList colNames = {m_myName, m_peerName, tr("Δ")};
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
    statsCol->addLayout(grid);
    statsCol->addSpacing(8);
    statsCol->addWidget(m_headline);
    statsCol->addStretch(1);
    auto* statsHost = new QWidget(this);
    statsHost->setLayout(statsCol);
    statsHost->setFixedWidth(250);

    auto* mainRow = new QHBoxLayout;
    mainRow->setSpacing(16);
    // agendaSide (headers + scroll) is built just below, then inserted
    // here — declaration order in C++ vs layout order on screen don't have
    // to match; the insert index says where things GO.
    mainRow->addWidget(statsHost);

    // The pinned headers live in the LEFT column of mainRow, wrapped with
    // the scroll so they span exactly the agendas' width (not the stats').
    auto* agendaSide = new QVBoxLayout;
    agendaSide->setSpacing(6);
    agendaSide->addLayout(headerRow);
    agendaSide->addWidget(scroll, 1);
    mainRow->insertLayout(0, agendaSide, 1); // agendas left, stats right

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addLayout(navRow);
    layout->addLayout(mainRow, 1);

    // ---- wiring: PlannerPage's recipe, verbatim -----------------------------
    // Same signals, same routing, same domain doors. This screen adds zero
    // new ways to mutate the planner — it adds a new place to reach the
    // existing ones.
    connect(prev, &QPushButton::clicked, this,
            [this]() { showDay(m_day.addDays(-1)); });
    connect(next, &QPushButton::clicked, this,
            [this]() { showDay(m_day.addDays(1)); });

    connect(m_myAgenda, &AgendaWidget::emptySlotClicked, this,
            [this](int slotIndex) { planAt(m_day, slotIndex); });
    connect(m_myAgenda, &AgendaWidget::eventClicked, this,
            [this](const QString& eventId) {
                EventDialog dialog(m_mine, m_tracker, eventId, this);
                dialog.exec();
                // Nothing afterwards: the dialog's changes went through
                // AppData/TrackerService and come back via changed().
            });
    connect(m_myAgenda, &AgendaWidget::eventResized, this,
            [this](const QString& id, int startMin, int endMin) {
                m_mine->resizeEvent(id, startMin, endMin); // the ONE guard
            });

    // Your edits repaint your side and re-derive the numbers. The peer
    // column never changes — it's a snapshot, and honesty about that is a
    // documented limit, not a bug (fresh data = reopen Compare).
    connect(m_mine, &AppData::changed, this, &CompareDialog::refresh);

    refresh();
}

void CompareDialog::showDay(const QDate& day)
{
    m_day = day;
    refresh();
}

void CompareDialog::planAt(QDate date, int slotIndex)
{
    // UC1's four lines, third home (PlannerPage day view, week view, now
    // here): ask, and if confirmed, tell the domain through the same three
    // doors — which re-check isFree and can refuse. Nothing new to test.
    PickActivityDialog dialog(m_mine, date, slotIndex, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int start = dialog.chosenStartMinutes();
    const int end   = dialog.chosenEndMinutes();
    switch (dialog.chosenKind()) {
    case PickActivityDialog::Kind::Activity:
        m_mine->addEvent(date, start, end, dialog.chosenActivityId(),
                         dialog.enteredTitle());
        break;
    case PickActivityDialog::Kind::Task:
        m_mine->addTaskEvent(date, start, end, dialog.chosenTaskId());
        break;
    case PickActivityDialog::Kind::AdHoc:
        m_mine->addAdHocEvent(date, start, end, dialog.enteredTitle());
        break;
    case PickActivityDialog::Kind::None:
        break;
    }
}

void CompareDialog::refresh()
{
    m_dayLabel->setText(m_day == QDate::currentDate()
                            ? tr("Today")
                            : m_day.toString(QStringLiteral("ddd d MMM")));

    m_myAgenda->setDate(m_day);
    m_peerAgenda->setDate(m_day);

    // The hours preference applies here too — through the same union rule
    // as the week view, but across DATASETS instead of days: your 6 AM row
    // and their 6 AM row must be one horizontal line, so both sides show
    // the wider of the two needs. (Recomputed every refresh: your edits or
    // a date step can change what must be covered.)
    const auto pref   = prefs::agendaWindow();
    const auto mineW  = AgendaWidget::windowCovering(m_mine, m_day,
                                                     pref.first, pref.second);
    const auto theirW = AgendaWidget::windowCovering(&m_peer, m_day,
                                                     pref.first, pref.second);
    const int start = qMin(mineW.first, theirW.first);
    const int end   = qMax(mineW.second, theirW.second);
    m_myAgenda->setVisibleWindow(start, end);
    m_peerAgenda->setVisibleWindow(start, end);

    // The same pure summarizer on both datasets — the numbers are
    // comparable because the code path is identical (share addendum §E).
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
