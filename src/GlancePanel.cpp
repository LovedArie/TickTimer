#include "GlancePanel.h"


#include "CatchUpCard.h"
#include "NeedsBlockCard.h"
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
    m_title = title;
    m_sub = new QLabel(
        tr("Calculated live from what you tracked — nothing is stored twice."),
        this);
    m_sub->setObjectName("sub");
    m_sub->setWordWrap(true);

    // The review card sits ABOVE the numbers and, when the gate is closed,
    // INSTEAD of them (needs-a-block §E). Its action signals are forwarded
    // verbatim: this panel is a const view and decides nothing.
    m_needsBlock = new NeedsBlockCard(m_data, this);
    connect(m_needsBlock, &NeedsBlockCard::planTaskRequested,
            this, &GlancePanel::planTaskRequested);
    connect(m_needsBlock, &NeedsBlockCard::editDeadlineRequested,
            this, &GlancePanel::editDeadlineRequested);
    connect(m_needsBlock, &NeedsBlockCard::notUrgentRequested,
            this, &GlancePanel::notUrgentRequested);
    connect(m_needsBlock, &NeedsBlockCard::dismissRequested,
            this, &GlancePanel::dismissRequested);
    connect(m_needsBlock, &NeedsBlockCard::bringBackRequested,
            this, &GlancePanel::bringBackRequested);
    // "Show my day" only changed per-device QSettings — no changed() will
    // fire, so the card asks for the re-derive itself.
    connect(m_needsBlock, &NeedsBlockCard::reviewed,
            this, &GlancePanel::refresh);

    // Everything the gate can hold back lives in ONE container, so the
    // gate is a single setVisible — not a dozen widgets each remembering
    // to hide. (Named for the test suite: it finds the gate by finding
    // this widget.)
    m_dayContent = new QWidget(this);
    m_dayContent->setObjectName("glanceContent");
    auto* content = new QVBoxLayout(m_dayContent);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(10);

    m_focusBox = new StatBox(tr("Focused"), theme::focus(), m_dayContent);
    m_breakBox = new StatBox(tr("Break"), theme::brk(), m_dayContent);
    // Distracted gets its own box (owner request — break is CHOSEN rest,
    // distraction is off-task drift; folding them together hides the one
    // number this app exists to expose). Same danger hue the block bars
    // already use — one colour per meaning, everywhere.
    m_distractedBox = new StatBox(tr("Distracted"), theme::danger(),
                                  m_dayContent);
    auto* statRow = new QHBoxLayout;
    statRow->addWidget(m_focusBox);
    statRow->addWidget(m_breakBox);
    statRow->addWidget(m_distractedBox);

    m_bars = new CategoryBars(m_dayContent);

    // The pie RETIRED here in v26.7 (glance de-crowding, catch-up §K.5):
    // it was the same split the bars already show, rendered a third time —
    // the bars were literally its legend, which is a chart confessing it
    // adds no information. CategoryPie itself lives on in the week and
    // month reviews, where it isn't sitting next to its own data.
    m_encourage = new QLabel(m_dayContent);
    m_encourage->setObjectName("encourage");
    m_encourage->setWordWrap(true);

    content->addLayout(statRow);
    content->addWidget(m_bars);
        content->addWidget(m_encourage);
    // Top-pack the day content INSIDE its own container: the container is
    // about to take the panel-level stretch (below), and without this its
    // stats would drift apart to fill the room instead of staying compact.
    content->addStretch(1);

    // The catch-up strip (v26.2 §K). Built here, placed BELOW the review
    // card: the gate may hold the day's numbers hostage, the strip may not,
    // and stacking them this way keeps the blocking question first.
    m_catchUp = new CatchUpCard(m_data, this);
    connect(m_catchUp, &CatchUpCard::acceptProposalRequested,
            this, &GlancePanel::catchUpAcceptRequested);
    connect(m_catchUp, &CatchUpCard::resolveRequested,
            this, &GlancePanel::catchUpResolveRequested);
    connect(m_catchUp, &CatchUpCard::showDayRequested,
            this, &GlancePanel::catchUpShowDayRequested);
    connect(m_catchUp, &CatchUpCard::resolveAllRequested,
            this, &GlancePanel::catchUpResolveAllRequested);

    // The REVIEW ROW (v26.7.1): both review features share one horizontal
    // line, which is what makes their pills read as one grammar instead of
    // two stacked dialects (the owner's screenshot). The needs-block card
    // takes the row's width — its gate and pinned rows need room — and the
    // catch-up chip sits beside it, top-aligned.
    //
    // ADJACENT, not book-ended (v26.7.2): the first cut gave the
    // needs-block card the row's stretch, which shoved the chip to the far
    // edge — two objects pinned to opposite walls FRAME the void between
    // them, and the eye pays for every comparison with a saccade across
    // dead space (the owner felt it as fatigue). Both cards now size to
    // their hints and pack left; ONE trailing stretch owns the slack.
    //
    // KNOWN SQUEEZE, accepted: when the strip mode shows pinned hero rows
    // (rung-2 escalations — rare), the card's hint widens and the HBox
    // squeezes it against the chip inside the 320px panel. Common case is
    // exactly the prototype; the rare case is cramped but functional. If
    // escalations turn out frequent, the fix is stacking when pinned rows
    // exist — a decision waiting, not a surprise.
    // ORDER AND STRETCH, third and final cut (v26.7.4). The needs-block
    // card wraps its content in a QScrollArea, and a scroll area's
    // sizeHint is a CACHED GUESS (the v22 scar, biting again): sized to
    // that hint in a row, the card came out narrow and CLIPPED its own
    // pill mid-word. The rule that ends the whack-a-mole: in a row, the
    // widget with the unreliable hint takes the stretch — it absorbs the
    // hint's error along with the slack — and honest-hint widgets (the
    // chip is a plain button) pack first. The card's internal left-pack
    // then puts its pill flush after the chip: adjacent pills, slack at
    // the far right, nothing clipped, and gate mode still owns the full
    // row because the chip hides while the gate is closed.
    m_reviewRow = new QWidget(this);
    {
        auto* rowLay = new QHBoxLayout(m_reviewRow);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(8);
        rowLay->addWidget(m_catchUp, 0, Qt::AlignTop);
        rowLay->addWidget(m_needsBlock, 1);
    }
    // The drawers cover the PANEL, not the pill-height row the cards now
    // live in (v26.7.5). addWidget above re-parented both cards into the
    // row — and the drawers' old host rule was "my parent", so without
    // these two lines every slide-over opens inside a 40px strip: the
    // needs-block one invisibly, the catch-up one mangled. Written
    // contract now: the panel says who gets covered.
    m_needsBlock->setDrawerHost(this);
    m_catchUp->setDrawerHost(this);

    layout->addWidget(title);
    layout->addWidget(m_sub);
    layout->addWidget(m_reviewRow);
    layout->addWidget(m_dayContent, 1);
    // NO trailing addStretch (v22.1). The slack now lives inside whichever
    // section is on stage — refresh() moves the stretch factor to the review
    // card when the gate closes, so the card fills the panel instead of
    // huddling squashed at the top over a void (the owner's screenshot).

    refresh();
}

bool GlancePanel::needsAttention() const
{
    return (m_needsBlock && m_needsBlock->hasAnything())
           || (m_catchUp && m_catchUp->hasAnything());
}

void GlancePanel::setHeadingVisible(bool on)
{
    if (m_title)
        m_title->setVisible(on);
}

void GlancePanel::setCardDrawerHost(QWidget* host)
{
    m_needsBlock->setDrawerHost(host);
    m_catchUp->setDrawerHost(host);
}

void GlancePanel::setDate(QDate date)
{
    m_date = date;
    refresh();
}

void GlancePanel::refresh()
{
    // Step 0: the review card decides the panel's shape (needs-a-block
    // §E). One `now`, from the seam, for every derivation this pass — the
    // gate and the flag must never disagree about what time it is.
    const QDateTime now = nowProvider();
    m_needsBlock->refresh(now);
    // The gate is an INPUT to the chip, decided before the chip refreshes —
    // the card owns its visibility; the panel only reports the veto.
    m_catchUp->setSuppressed(m_needsBlock->gateClosed());
    m_catchUp->refresh(now);
    m_dayContent->setVisible(!m_needsBlock->gateClosed());
    // v22.1: whoever is on stage gets the room. Gate closed -> the card IS
    // the panel (§E said so all along), so it takes the vertical stretch and
    // its rows fill the height; gate open -> the day content takes it back
    // and the card sizes to its content. The v22 bug was laying the card out
    // at its sizeHint — and a scroll area's sizeHint is a cached GUESS about
    // its content, not a claim on the space around it. Stretch factors claim
    // space; hints only suggest it.
    // The vertical stretch now targets the review ROW (the card's new
    // home): same v22.1 rule, one level up — whoever is on stage gets the
    // room. While the gate holds the panel, the catch-up chip yields
    // entirely: one blocking review at a time is the §K ceiling, and the
    // chip returns the moment "Show my day" opens the gate.
    const bool closed = m_needsBlock->gateClosed();
    if (auto* lay = qobject_cast<QVBoxLayout*>(layout())) {
        lay->setStretchFactor(m_reviewRow, closed ? 1 : 0);
        lay->setStretchFactor(m_dayContent, closed ? 0 : 1);
    }
    m_sub->setText(m_needsBlock->gateClosed()
                       ? tr("First, a look at what has no time set aside.")
                       : tr("Calculated live from what you tracked — "
                            "nothing is stored twice."));

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
