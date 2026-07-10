#include "GlancePanel.h"

#include "ReviewWidgets.h" // CategoryPie — one pie widget, every screen

#include "Theme.h"
#include "Widgets.h"
#include "AppData.h"
#include "Stats.h"
#include "TrackerService.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

GlancePanel::GlancePanel(const AppData* data, const TrackerService* tracker,
                         QWidget* parent)
    : QFrame(parent)
    , m_data(data)
    , m_tracker(tracker)
    , m_date(QDate::currentDate())
{
    setObjectName("panel");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("At a glance"), this);
    title->setObjectName("h2");
    auto* sub = new QLabel(
        tr("Calculated live from what you tracked — nothing is stored twice."),
        this);
    sub->setObjectName("sub");
    sub->setWordWrap(true);

    m_focusBox = new StatBox(tr("Focused"), theme::focus(), this);
    m_breakBox = new StatBox(tr("Break"), theme::brk(), this);
    // Distracted gets its own box (owner request — break is CHOSEN rest,
    // distraction is off-task drift; folding them together hides the one
    // number this app exists to expose). Same danger hue the block bars
    // already use — one colour per meaning, everywhere.
    m_distractedBox = new StatBox(tr("Distracted"), theme::danger(), this);
    auto* statRow = new QHBoxLayout;
    statRow->addWidget(m_focusBox);
    statRow->addWidget(m_breakBox);
    statRow->addWidget(m_distractedBox);

    m_bars = new CategoryBars(this);

    // The day's pie (owner request), reusing the week review's CategoryPie
    // — one chart widget, every screen. No legend of its own: the BARS
    // above are the legend (same rows, same colours, same order), which is
    // why the pie must be fed exactly the rows the bars show.
    m_pie = new CategoryPie(this);

    m_encourage = new QLabel(this);
    m_encourage->setObjectName("encourage");
    m_encourage->setWordWrap(true);

    layout->addWidget(title);
    layout->addWidget(sub);
    layout->addLayout(statRow);
    layout->addWidget(m_bars);
    {
        auto* pieRow = new QHBoxLayout;   // centred, not stretched
        pieRow->addStretch(1);
        pieRow->addWidget(m_pie);
        pieRow->addStretch(1);
        layout->addLayout(pieRow);
    }
    layout->addWidget(m_encourage);
    layout->addStretch(1);

    refresh();
}

void GlancePanel::setDate(QDate date)
{
    m_date = date;
    refresh();
}

void GlancePanel::refresh()
{
    // Step 1: derive today's numbers from raw Segments — nothing cached.
    stats::PeriodSummary s = stats::summarizeDay(*m_data, m_date);

    // Step 2: add the live, still-uncommitted interval so the numbers grow
    // before your eyes while you focus (motivation is a feature).
    if (m_tracker->state() != TrackerService::State::Idle) {
        const Event* e = m_data->eventById(m_tracker->trackedEventId());
        if (e && e->date == m_date) {
            const qint64 live = m_tracker->liveSeconds();
            // A three-way SWITCH, not if/else — the old two-way split was
            // the bug the owner caught: a live Distracted timer ticked up
            // the BREAK box, because "not focusing" was silently treated as
            // "on break". The domain never made that mistake (Stats keeps
            // three buckets); only this display did. Every other live split
            // in the codebase (EventDialog, AgendaWidget) already switches.
            switch (m_tracker->state()) {
            case TrackerService::State::Focusing:
                s.totals.focusSeconds += live;
                break;
            case TrackerService::State::OnBreak:
                s.totals.breakSeconds += live;
                break;
            case TrackerService::State::Distracted:
                s.totals.distractedSeconds += live;
                break;
            case TrackerService::State::Idle:
                break;
            }
            // Live seconds credit the category bar only while FOCUSING —
            // the same focus-only rule Stats now applies to stored
            // segments (§3.37). Live break/distracted still tick their
            // boxes and sink rows via the switch above.
            if (m_tracker->state() == TrackerService::State::Focusing) {
                const QString catId = m_data->eventCategoryId(*e);
                if (!catId.isEmpty())
                    s.byCategory[catId] += live;
            }
            // The live interval is elapsed-but-not-yet-stored, which is
            // exactly unaccounted's definition — subtract it, or the
            // number would GROW while you are actively tracking.
            s.unaccountedSeconds =
                qMax<qint64>(0, s.unaccountedSeconds - live);
        }
    }

    m_focusBox->setValue(stats::formatSeconds(s.totals.focusSeconds));
    m_breakBox->setValue(stats::formatSeconds(s.totals.breakSeconds));
    m_distractedBox->setValue(
        stats::formatSeconds(s.totals.distractedSeconds));

    // Zero rows are HIDDEN (owner request: "0s is useless information") —
    // categories and sinks alike. The panel shows where time went, and a
    // row where none went says nothing.
    QVector<CategoryBars::Row> rows;
    for (const Category& c : m_data->categories())
        if (const qint64 secs = s.byCategory.value(c.id, 0); secs > 0)
            rows.append({c.name, c.color, secs});
    // The sink rows close the books (§3.37): categories show productive
    // time only, so break, drift, and now UNACCOUNTED time (§3.40 — the
    // planned window that elapsed with nothing tracked) accumulate here.
    // Grey for unaccounted, deliberately: the same "no story" semantic as
    // ad-hoc blocks' neutral paint. Derived, never stored.
    if (s.totals.breakSeconds > 0)
        rows.append({tr("Break"), theme::brk(), s.totals.breakSeconds});
    if (s.totals.distractedSeconds > 0)
        rows.append({tr("Distracted"), theme::danger(),
                     s.totals.distractedSeconds});
    if (s.unaccountedSeconds > 0)
        rows.append({tr("Unaccounted"), QColor("#8A929C"),
                     s.unaccountedSeconds});

    // The pie eats EXACTLY the rows the bars show — that identity is what
    // lets the bars double as the pie's legend. Hidden on an empty day.
    QVector<CategoryPie::Slice> slices;
    for (const CategoryBars::Row& r : rows)
        slices.append({r.color, r.seconds});
    m_pie->setSlices(slices);
    m_pie->setVisible(!slices.isEmpty());

    m_bars->setRows(std::move(rows));

    // The calm, non-shaming line (Supplementary Spec, Usability). Note what
    // it never says: "unproductive", "wasted", "only". Words are part of
    // the requirements here, not decoration.
    const qint64 total = s.totals.total();
    QString msg;
    if (total == 0) {
        msg = tr("A blank day is a fresh start. Plan a block and press "
                 "focus when you begin.");
    } else if (s.totals.focusSeconds * 100 >= total * 60) {
        msg = tr("Nice — most of your tracked time today was focused. "
                 "Keep the momentum.");
    } else {
        msg = tr("You've tracked %1 so far. Small steps count — pick one "
                 "thing and start the focus timer.")
                  .arg(stats::formatSeconds(total));
    }
    m_encourage->setText(msg);
}

// ---- CategoryBars -----------------------------------------------------------

namespace
{
constexpr int kRowHeight  = 34;
constexpr int kBarHeight  = 8;
} // namespace

CategoryBars::CategoryBars(QWidget* parent)
    : QWidget(parent)
{
}

void CategoryBars::setRows(QVector<Row> rows)
{
    m_rows = std::move(rows);
    updateGeometry(); // row count changed => our sizeHint changed
    update();
}

QSize CategoryBars::sizeHint() const
{
    // int(...) is deliberate: Qt 6 containers report size as 64-bit
    // qsizetype, and brace-init {} refuses to narrow it silently into
    // QSize's int (a C++11 safety net). The cast says "I checked: a
    // handful of category rows always fits" — explicit, not accidental.
    return {260, int(m_rows.size()) * kRowHeight};
}

void CategoryBars::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.setFont(scaledFont(font(), -1));

    // Bars are scaled to the LARGEST category, not to the day total — so
    // the biggest bar is always full width and the others read relative to
    // it, which is what the eye compares anyway (same as the prototype).
    qint64 maxSeconds = 1;
    for (const Row& r : m_rows)
        maxSeconds = qMax(maxSeconds, r.seconds);

    int y = 0;
    for (const Row& r : m_rows) {
        p.setBrush(r.color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRect(0, y + 3, 9, 9));

        p.setPen(theme::ink());
        p.drawText(QRect(16, y, width() - 80, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, r.name);
        p.setPen(theme::inkSoft());
        p.drawText(QRect(width() - 76, y, 76, 16),
                   Qt::AlignRight | Qt::AlignVCenter,
                   stats::formatSeconds(r.seconds));

        const QRect track(0, y + 19, width(), kBarHeight);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::track());
        p.drawRoundedRect(track, 4, 4);
        if (r.seconds > 0) {
            const int w = int(qint64(track.width()) * r.seconds / maxSeconds);
            p.setBrush(r.color);
            p.drawRoundedRect(QRect(track.left(), track.top(),
                                    qMax(w, kBarHeight), kBarHeight), 4, 4);
        }
        y += kRowHeight;
    }
}
