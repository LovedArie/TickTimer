#include "NeedsBlockCard.h"

#include "AppData.h"
#include "Prefs.h"
#include "SlidePanel.h"
#include "Theme.h"
#include "Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
// Only the STRIP mode's pinned list keeps a row cap (a soft label, not a
// button). The gate list is uncapped since v22.1: the card owns the panel's
// height there, so every row is visible by default and the scroll bar is the
// bound — see the header for the v22 -> v22.1 story.
constexpr int kMaxRows = 4;

// One place for the card's small type — chips and hints read a step down.
QLabel* softLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("sub");
    l->setWordWrap(true);
    return l;
}

// "Fri 24 Jul · in 3d" — the absolute date answers WHEN, the countdown
// answers HOW WORRIED (owner: "I love the addition of the date displayed").
QString dueText(const Task& t, QDate today)
{
    if (!t.dueDate.isValid())
        return QObject::tr("no due date");
    QString abs = t.dueDate.toString(QStringLiteral("ddd d MMM"));
    // v22: the clock rides with the date, not with the countdown. "Fri 24 Jul
    // 17:00 · in 3d" reads as one fact (when) followed by one feeling (how
    // worried) — splitting the deadline across both halves would blur that.
    if (t.dueTime.isValid())
        abs += QLatin1Char(' ') + dueTimeLabel(t.dueTime);
    const qint64 days = today.daysTo(t.dueDate);
    QString rel;
    if (days < 0)       rel = QObject::tr("%n day(s) overdue", nullptr, int(-days));
    else if (days == 0) rel = QObject::tr("today");
    else if (days == 1) rel = QObject::tr("tomorrow");
    else                rel = QObject::tr("in %n day(s)", nullptr, int(days));
    return abs + QStringLiteral(" · ") + rel;
}

// The owed "why" (§A) — the domain states the fact, these are the words.
QString reasonText(coverage::Reason r)
{
    switch (r) {
    case coverage::Reason::BlockAfterDeadline:
        return QObject::tr("⚠ Blocked after the deadline — that time won't "
                           "make it");
    case coverage::Reason::BlockInPast:
        return QObject::tr("⚠ Time was set aside — it came and went");
    case coverage::Reason::NoBlock:
    case coverage::Reason::None:
        break;
    }
    return {};
}
} // namespace

NeedsBlockCard::NeedsBlockCard(const AppData* data, QWidget* parent)
    : QFrame(parent)
    , m_data(data)
{
    setObjectName("needsBlockCard");

    // The card is now a scroll area wrapping the rows, not the rows directly.
    // Everything that builds rows still talks to m_layout and is unaware of
    // the change — the bound is structural, so no rebuild code had to learn
    // about it. (Widgets created with `this` as parent are reparented into
    // m_body automatically the moment m_layout->addWidget() takes them.)
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_body = new QWidget(this);
    m_layout = new QVBoxLayout(m_body);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(8);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_body);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // AdjustToContents matters in STRIP mode, where the card sizes to its
    // content: the default AdjustIgnored policy answers sizeHint() with a
    // hardcoded 256x192, so a two-line strip would reserve a fist of empty
    // space. In GATE mode the hint is irrelevant — GlancePanel hands the
    // card the layout stretch and the height comes from the panel, not from
    // any hint (v22.1; the v22 fixed ceiling is gone, and good riddance).
    m_scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    // Let the panel's background show through — the card is a region of the
    // glance panel, not a box sitting on top of it.
    m_scroll->viewport()->setAutoFillBackground(false);
    m_body->setAutoFillBackground(false);
    // NO makeTouchScrollable here (v22.2), on purpose. Grabbing the
    // viewport's touch gesture makes QScroller intercept presses on every
    // child — and this card is buttons-first, scroll-second. Worse, on the
    // week tab this card lives INSIDE weekScroll, whose viewport is already
    // grabbed: nested grabbed scrollers fight over the same press. Wheel
    // and the scroll bar still work; on touch, the outer scroll area still
    // flicks. A review card whose buttons always click beats one that also
    // flicks.

    outer->addWidget(m_scroll);
}

bool NeedsBlockCard::lookedRecently(const QDateTime& now) const
{
    const ReturnPolicy policy = prefs::reviewReturnPolicy();
    const QDateTime stored = prefs::needsBlockLastReview();
    if (stored.isValid() && policy.nextReturn(stored) > now)
        return true;
    // The session witness (v22.7): valid whenever THIS card saw the review
    // happen, regardless of what the settings store managed to keep. Expires
    // by the same policy, so the gate still re-arms on schedule.
    return m_sessionReview.isValid()
           && policy.nextReturn(m_sessionReview) > now;
}

void NeedsBlockCard::refresh(const QDateTime& now)
{
    // The derivations still run on every call — fingerprint() re-derives the
    // list, the gate, and the put-off set from scratch, so nothing can be
    // stale. What changed (v22.2) is the LAST step: widgets are only torn
    // down and rebuilt when the derived picture actually differs. See the
    // header for the click-eating bug this closes: the glance panel calls
    // this once per second while a timer runs, and rebuilding an identical
    // card each tick destroyed the very button the user was clicking.
    const QString print = fingerprint(now);
    if (print == m_lastPrint)
        return; // pixel-identical — keep the widgets, keep the click
    m_lastPrint = print;
    rebuild(now);
}

QString NeedsBlockCard::fingerprint(const QDateTime& now) const
{
    // One entry per fact the card PAINTS. The discipline that keeps this
    // honest: whenever rebuild()/makeTaskRow() grows a new visible field,
    // it must be added here, or edits to that field stop repainting. (Same
    // pact TaskListModel::rolesEqual keeps with its delegate.)
    const auto rule = prefs::needsBlockRule();
    const auto esc  = prefs::needsBlockEscalation();

    QStringList f;
    // Gate inputs — `looked` is included as the DERIVED bool, because it can
    // flip purely by the clock crossing nextReturn (no pref write involved).
    // Same predicate the rebuild uses (v22.7) — two copies of it is how the
    // print and the gate would drift apart.
    f << (prefs::needsBlockGateEnabled() ? QStringLiteral("g1")
                                         : QStringLiteral("g0"))
      << (lookedRecently(now) ? QStringLiteral("l1") : QStringLiteral("l0"))
      // Relative due texts ("in 3d") change when the DATE rolls, not the
      // second — so the date is in the print and the time of day is not.
      << now.date().toString(Qt::ISODate)
      // The card's own expansion state: part of what is rendered, so part
      // of the print (toggling routes through refresh and rebuilds).
      << m_decisionFor
      << (m_showAll ? QStringLiteral("a1") : QStringLiteral("a0"))
      << QStringLiteral("dm%1").arg(m_drawerMode) // drawer = rendered state
      // The escalation knobs feed rung(), which picks row styling.
      << QString::number(esc.decisionAfter)
      << QString::number(esc.pinAfterExtra)
      << (esc.urgentOnly ? QStringLiteral("u1") : QStringLiteral("u0"));

    for (const Task* t : m_data->tasksNeedingBlock(rule, esc, now)) {
        f << t->id << t->title << QString::number(int(t->priority))
          << t->dueDate.toString(Qt::ISODate)
          << t->dueTime.toString(Qt::ISODate)
          << QString::number(t->dismissCount)
          << QString::number(
                 int(m_data->taskUncoveredReason(t->id, now.date())));
        if (const Category* c = m_data->categoryById(t->categoryId))
            f << c->name << c->color.name();
    }

    // The put-off strip: membership AND the return time it prints. A lapsed
    // dismissal drops out of this loop by itself, so the per-second ticks
    // now bring a task back the very second its timer expires — a small
    // free upgrade from having the print re-derived on every call.
    for (const Task& t : m_data->tasks())
        if (t.dismissedUntil.isValid() && t.dismissedUntil > now
            && !t.done && !t.archived)
            f << QStringLiteral("p") + t.id
              << t.title
              << t.dismissedUntil.toString(Qt::ISODate);

    return f.join(QLatin1Char('\x1f')); // unit separator: never in titles
}

namespace
{
// Tear down one layout item COMPLETELY. The subtlety this function exists
// for (and the bug it fixes — docs/screenshot/needsblock-bug-reproduced.png):
// deleting a QLayout does NOT delete the widgets inside it. They are
// children of the CARD, not of the layout — so the old teardown, which
// only handled `item->widget()`, silently orphaned every widget living in
// a nested layout (the title + count badge in the header row). Orphans
// keep painting at their stale positions, stacking under each fresh
// rebuild: the owner's screenshot was the previous badge smeared across
// the new title. The cure is to recurse into child layouts and
// deleteLater every widget found at any depth.
void deleteLayoutTree(QLayoutItem* item)
{
    if (!item)
        return;
    if (QWidget* w = item->widget()) {
        w->hide();
        w->deleteLater(); // mid-signal safety, same as ever
    } else if (QLayout* nested = item->layout()) {
        while (QLayoutItem* child = nested->takeAt(0))
            deleteLayoutTree(child);
    }
    delete item; // the item/layout shells are inert — safe to free now
}
} // namespace

void NeedsBlockCard::rebuild(const QDateTime& now)
{
    // deleteLater, NOT delete, for the widgets (see deleteLayoutTree):
    // this rebuild routinely runs while a click handler inside an old row
    // is still on the stack (dismiss -> domain -> changed() -> refresh()
    // -> here). Freeing the sender mid-signal is the founding crash of
    // test_ui.cpp; the cure hasn't changed.
    while (QLayoutItem* item = m_layout->takeAt(0))
        deleteLayoutTree(item);

    const auto rule = prefs::needsBlockRule();
    const auto esc  = prefs::needsBlockEscalation();
    const auto list = m_data->tasksNeedingBlock(rule, esc, now);

    // Any live dismissals? (Shown as the put-off strip in every state.)
    QVector<const Task*> putOff;
    for (const Task& t : m_data->tasks())
        if (t.dismissedUntil.isValid() && t.dismissedUntil > now
            && !t.done && !t.archived)
            putOff.append(&t);

    // The gate derivation — one predicate, shared with fingerprint().
    const bool looked = lookedRecently(now);
    m_gateClosed = prefs::needsBlockGateEnabled()
                   && !list.isEmpty() && !looked;
    m_hasAnything = !list.isEmpty() || !putOff.isEmpty();

    // "Show all" is meaningless once there is nothing to show all OF — and
    // leaving it set would make the counter reappear pre-expanded next time
    // the list grows, which nobody asked for. Same self-cleaning discipline
    // as m_decisionFor below.
    if (list.size() <= 1)
        m_showAll = false;

    // Drop stale expansion state (the task got scheduled or finished).
    if (!m_decisionFor.isEmpty()) {
        const bool still = std::any_of(
            list.begin(), list.end(),
            [this](const Task* t) { return t->id == m_decisionFor; });
        if (!still)
            m_decisionFor.clear();
    }

    // ---- CLEAR: nothing qualifies -----------------------------------------
    if (list.isEmpty()) {
        if (putOff.isEmpty() && !m_hasAnything) {
            hide(); // the panel is exactly what it always was
            return;
        }
        show();
        auto* ok = softLabel(tr("✓ Everything's on the calendar"), this);
        ok->setStyleSheet(QStringLiteral("color:%1;").arg(
            theme::focus().name()));
        m_layout->addWidget(ok);
        // fallthrough to the put-off strip below
    } else if (m_gateClosed) {
        // ---- GATE: one task, big (design A) --------------------------------
        show();
        auto* head = new QHBoxLayout;
        auto* title = new QLabel(tr("Needs a block"), this);
        title->setObjectName("h2");
        head->addWidget(title);
        head->addStretch(1);

        // The counter doubles as the survey escape hatch. With one task it is
        // an inert badge; with more it is a button, because that is the only
        // point at which "show me the rest" is a question worth asking. A
        // control that appears exactly when it becomes meaningful teaches
        // itself — no tooltip required.
        if (list.size() > 1) {
            auto* all = new QPushButton(
                m_showAll ? tr("Showing all %1 ▴").arg(list.size())
                          : tr("1 of %1 ▾").arg(list.size()),
                this);
            all->setObjectName("needsBlockShowAll");
            all->setFlat(true);
            all->setCursor(Qt::PointingHandCursor);
            all->setStyleSheet(QStringLiteral(
                "border:none;background:transparent;color:%1;font-weight:600;"
                "padding:1px 4px;").arg(theme::focus().name()));
            connect(all, &QPushButton::clicked, this, [this, now]() {
                m_showAll = !m_showAll;
                refresh(now); // m_showAll is in the print — rebuilds
            });
            head->addWidget(all);
        } else {
            auto* count = new QLabel(QString::number(list.size()), this);
            count->setStyleSheet(QStringLiteral(
                "background:%1;color:white;border-radius:9px;padding:1px 8px;"
                "font-weight:600;").arg(theme::focus().name()));
            head->addWidget(count);
        }
        m_layout->addLayout(head);
        m_layout->addWidget(softLabel(
            list.size() > 1 && !m_showAll
                ? tr("First, the one most worth your next hour.")
                : tr("Open work with no time set aside yet."),
            this));

        // Focus: the top-ranked task alone, in the hero presentation. Expand
        // and it becomes the ordinary list — same builder, same actions, just
        // the compact density (see makeTaskRow's `focus` parameter).
        if (m_showAll) {
            for (const Task* t : list)
                m_layout->addWidget(
                    makeTaskRow(*t, coverage::rung(*t, esc), now));
        } else {
            const Task* top = list.first();
            m_layout->addWidget(
                makeTaskRow(*top, coverage::rung(*top, esc), now, true));
            if (list.size() > 1)
                m_layout->addWidget(softLabel(
                    tr("%n more after this one", nullptr,
                       int(list.size()) - 1),
                    this));
        }

        auto* open = new QPushButton(tr("Show my day →"), this);
        open->setObjectName("showMyDay");
        connect(open, &QPushButton::clicked, this, [this]() {
            // The card's ONE write: its own per-device memory (§C's table).
            // Everything else about this widget is a signal.
            prefs::setNeedsBlockLastReview(QDateTime::currentDateTime());
            // The session witness (v22.7): whatever the settings store does
            // with that write, THIS object saw the review happen. The
            // re-derive consults both, so no storage failure can re-close
            // the gate the user just opened. (This was the field bug: the
            // write vanished, the rebuild re-read nothing, and the gate
            // snapped shut on release.)
            m_sessionReview = QDateTime::currentDateTime();

            // Answer IMMEDIATELY in memory, don't wait for the re-derive.
            // gateClosed() is what GlancePanel asks to decide the panel's
            // shape, and the honest answer the instant this runs is "open".
            m_gateClosed = false;

            // Bust the fingerprint by hand. The re-derived print SHOULD
            // differ now (the `looked` flag flipped), but "should" is doing
            // real work in that sentence — a policy edge case or a settings
            // backend that doesn't round-trip a QDateTime would leave the
            // print identical, the rebuild skipped, and the button looking
            // dead. The one place we KNOW the state changed is right here,
            // so say so explicitly instead of inferring it downstream.
            m_lastPrint.clear();

            // …and emit on the NEXT event-loop turn, not inside this
            // handler. This is the v22.2 fix finished properly: the rebuild
            // that `reviewed()` triggers calls deleteLayoutTree, which does
            // w->hide() before deleteLater() — and hide() RELEASES THE
            // MOUSE GRAB. Tearing down this button while its own
            // mouseReleaseEvent is still on the stack is how a click gets
            // swallowed. singleShot(0) lets the event finish unwinding
            // first; the widget tree is only rebuilt once Qt is done with
            // the widget that started it.
            QTimer::singleShot(0, this, [this]() { emit reviewed(); });
        });
        m_layout->addWidget(open);
        // The closing reassurance must not contradict the strip below it:
        // "Nothing is dismissed" over a "4 put off" counter reads as a bug
        // (the owner's screenshot). The sentence means the GATE dismisses
        // nothing — say that, and acknowledge the put-offs when they exist.
        m_layout->addWidget(softLabel(
            putOff.isEmpty()
                ? tr("Nothing is dismissed — reviewing hides nothing.")
                : tr("Reviewing hides nothing — only \"Not today\" "
                     "puts things off."),
            this));
    } else {
        // ---- STRIP: reviewed; pinned rows stay, the rest fold up ----------
        show();
        QVector<const Task*> pinned, rest;
        for (const Task* t : list)
            (coverage::rung(*t, esc) == 2 ? pinned : rest).append(t);

        if (!pinned.isEmpty()) {
            auto* ph = softLabel(tr("STILL NEEDS A DECISION"), this);
            ph->setStyleSheet(QStringLiteral(
                "color:%1;font-weight:700;letter-spacing:0.05em;")
                .arg(theme::danger().name()));
            m_layout->addWidget(ph);
            // Pinned rows are unbounded in principle too (nothing stops six
            // tasks reaching rung 2), so the same cap applies. The scroll
            // ceiling would catch it anyway; capping keeps it CALM as well
            // as bounded.
            const int shownPinned = qMin(int(pinned.size()), kMaxRows);
            // Hero density here too (v22.9.1): a pinned task is the app's
            // LOUDEST ask, and it was dressed quieter than the gate's card.
            // One visual language for one meaning — "this needs a decision"
            // now looks identical at the gate, pinned in the strip, and in
            // the drawer.
            for (int i = 0; i < shownPinned; ++i)
                m_layout->addWidget(makeTaskRow(*pinned[i], 2, now,
                                                /*focus=*/true));
            if (pinned.size() > kMaxRows)
                m_layout->addWidget(softLabel(
                    tr("+%n more pinned — scroll or clear a few", nullptr,
                       int(pinned.size()) - kMaxRows),
                    this));
        }

    }

    // ---- the two chips (v22.9, owner: "two buttons that open a side
    // screen"). The inline accordions are gone: each list now lives in a
    // slide-over drawer, and these compact chips — sharing ONE row — are its
    // handles. An accordion answers "show me more" by shoving everything
    // below it; a drawer answers by LAYERING, so the panel behind never
    // jumps. The old objectNames are kept deliberately: to every existing
    // test (and every user's muscle memory) these are still "the strip" and
    // "the put-off strip" — the handle's meaning survived its mechanism.
    const int restCount = m_gateClosed ? 0 : [&] {
        int n = 0;
        for (const Task* t : list)
            if (coverage::rung(*t, esc) != 2)
                ++n;
        return n;
    }();
    if (restCount > 0 || !putOff.isEmpty()) {
        // PILLS since v26.7.1, and left-packed (no per-widget stretch): the
        // glance panel now lays this card in ONE review row beside the
        // catch-up chip, and two features sharing a row must share a
        // grammar. Solid pill = actionable; dashed gray = waiting — the
        // same coat the snoozed catch-up chip wears, because "put off
        // until 14:00" and "snoozed until 22:30" are the same state in two
        // features and should read as one.
        auto* chips = new QHBoxLayout;
        chips->setSpacing(8);
        if (restCount > 0) {
            auto* strip = new QPushButton(
                tr("%n need(s) a block", nullptr, restCount), this);
            strip->setObjectName("needsBlockStrip");
            strip->setCursor(Qt::PointingHandCursor);
            strip->setFixedHeight(30); // radius 14 needs >=28px — see the
                                       // catch-up chip's v26.7.3 note
            // 14px, not 999px: Qt drops any radius over half the height
            // (v26.7.2 — see the catch-up chip's coats for the full note).
            strip->setStyleSheet(QStringLiteral(
                "QPushButton { background:#FFFFFF; border:1px solid #E6E3DD;"
                " border-radius:14px; padding:5px 12px; font-weight:600;"
                " color:#1E2228; }"
                "QPushButton:hover { background:rgba(47,126,110,0.08); }"));
            connect(strip, &QPushButton::clicked, this, [this, now]() {
                m_drawerMode = 1;
                refresh(now); // rebuild's tail fills and opens the drawer
            });
            chips->addWidget(strip);
        }
        if (!putOff.isEmpty()) {
            auto* d = new QPushButton(
                tr("%n put off · %1", nullptr, int(putOff.size()))
                    .arg(putOff.first()->dismissedUntil.toString(
                        QStringLiteral("HH:mm"))),
                this);
            d->setObjectName("putOffStrip");
            d->setCursor(Qt::PointingHandCursor);
            d->setFixedHeight(30);
            // Faded solid, not dashed — waiting is a WEIGHT, not a line
            // style; dashes buzz (v26.7.2, the eye-fatigue report).
            d->setStyleSheet(QStringLiteral(
                "QPushButton { background:transparent;"
                " border:1px solid #E0E2E6; border-radius:14px;"
                " padding:5px 12px; font-weight:500; color:#767E89; }"
                "QPushButton:hover { color:#4F5560; border-color:#C9CDD3; }"));
            connect(d, &QPushButton::clicked, this, [this, now]() {
                m_drawerMode = 2;
                refresh(now);
            });
            chips->addWidget(d);
        }
        chips->addStretch(1);
        m_layout->addLayout(chips);
    }

    // Reconcile the drawer with reality: a mode whose list emptied (last
    // task planned from inside the drawer, last put-off brought back) closes
    // it rather than leaving an empty sheet to stare at.
    if ((m_drawerMode == 1 && restCount == 0)
        || (m_drawerMode == 2 && putOff.isEmpty()))
        m_drawerMode = 0;
    fillDrawer(now);

    // HEIGHT HONESTY (v26.7.6) — Coda 2's lesson on the vertical axis. The
    // card wraps a QScrollArea, whose sizeHint is a cached guess; nothing
    // bounded it vertically, so the card reserved phantom height and the
    // review row inherited a gap. The BODY is a plain widget with an honest
    // hint (pinned hero rows included), so the cap comes from it. Gate mode
    // lifts the cap entirely: there the card is SUPPOSED to fill the panel,
    // and v22.1's stretch mechanism needs room to hand out.
    if (m_gateClosed)
        setMaximumHeight(QWIDGETSIZE_MAX);
    else
        setMaximumHeight(qMax(30, m_body->sizeHint().height()));

    // TOP-PACK (v22.3). The card takes the panel's vertical stretch when the
    // gate is closed, so m_body is routinely TALLER than its content — and a
    // QVBoxLayout hands that slack to every item that will accept it. Labels
    // accept (vertical policy Preferred); buttons don't (Fixed). The result
    // was the owner's screenshot: paragraph-sized gaps between every line
    // while the buttons stayed put. One trailing stretch takes all the slack
    // instead, so the rows sit together at the top and the empty space pools
    // harmlessly at the bottom. Filling a space and being STRETCHED across it
    // are different things; this is the line that says which one we meant.
    m_layout->addStretch(1);

    // Nudge the size chain explicitly. QScrollArea CACHES its content's
    // sizeHint and only re-reads it on a LayoutRequest — which a
    // deleteLater-heavy rebuild can defer past the next paint. That timing
    // hole is the squashed card the owner screenshotted; asking directly
    // costs nothing and closes it.
    m_layout->activate();
    m_scroll->updateGeometry();
    updateGeometry();
}

SlidePanel* NeedsBlockCard::drawer()
{
    if (!m_drawer) {
        // The injected host first (v26.7.5) — the glance panel, so the
        // sheet covers the COLUMN. The parent fallback is for bare
        // embeddings (tests); it is no longer trusted in the app, because
        // the parent is now a pill-height row and a drawer over 40px is a
        // drawer nobody sees.
        QWidget* host = m_drawerHost ? m_drawerHost
                                     : (parentWidget() ? parentWidget()
                                                       : this);
        m_drawer = new SlidePanel(host);
        connect(m_drawer, &SlidePanel::closed, this, [this]() {
            m_drawerMode = 0;
            // WITHOUT this line the pill can never reopen the drawer
            // (v26.7.7): the strip click routes through the fingerprint
            // gate, and this handler mutates rendering state (the mode)
            // AFTER the print recorded it — so "click again" recomputes
            // the same print, refresh early-returns, and fillDrawer never
            // runs. The V158 rule, violated in this card and obeyed by
            // its sibling: state that affects cached rendering must
            // invalidate the cache, never slip around it. (Latent since
            // v22.9, masked by the once-per-second refreshes while a
            // timer runs — the print drifts with the minute, so reopening
            // "worked" if you waited. A close-then-reopen inside one
            // minute caught it bare.)
            m_lastPrint.clear();
        });
    }
    return m_drawer;
}

void NeedsBlockCard::fillDrawer(const QDateTime& now)
{
    if (m_drawerMode == 0) {
        if (m_drawer && m_drawer->isOpen())
            m_drawer->closePanel();
        return;
    }

    SlidePanel* panel = drawer();
    panel->clearContent();
    QVBoxLayout* v = panel->contentLayout();
    const int at = v->count() - 1; // insert above the trailing stretch

    if (m_drawerMode == 1) {
        panel->setTitle(tr("Needs a block"));
        const auto rule = prefs::needsBlockRule();
        const auto esc  = prefs::needsBlockEscalation();
        // Same rows, same actions, same signals as everywhere else — the
        // drawer is a different ROOM, not a different vocabulary. Planning
        // or dismissing from here mutates data, data emits changed(), the
        // card rebuilds, and rebuild's tail re-fills this very drawer: the
        // list updates in place with zero drawer-specific plumbing.
        // HERO density (v22.9.1, owner: "the needs-a-block is still the
        // older style"). The drawer inherited compact rows because that was
        // makeTaskRow's default — but the hero card IS the app's face for
        // "a task asking for a decision" now, and the drawer is where those
        // decisions get made. One boolean, because v22.4 made density a
        // PARAMETER instead of a fork: restyling a whole surface costs the
        // word `true`. That cheapness is the return on that decision.
        for (const Task* t : m_data->tasksNeedingBlock(rule, esc, now))
            if (coverage::rung(*t, esc) != 2)
                v->insertWidget(v->count() - 1,
                                makeTaskRow(*t, coverage::rung(*t, esc), now,
                                            /*focus=*/true));
    } else {
        panel->setTitle(tr("Put off for now"));
        for (const Task& t : m_data->tasks()) {
            if (!(t.dismissedUntil.isValid() && t.dismissedUntil > now
                  && !t.done && !t.archived))
                continue;
            auto* row = new QWidget(this);
            auto* h   = new QHBoxLayout(row);
            h->setContentsMargins(4, 2, 0, 2);
            auto* text = new QVBoxLayout;
            text->setSpacing(1);
            auto* title = new QLabel(t.title, row);
            title->setStyleSheet(QStringLiteral("font-weight:600;"));
            title->setWordWrap(true);
            text->addWidget(title);
            text->addWidget(softLabel(
                tr("returns %1").arg(
                    t.dismissedUntil.toString(QStringLiteral("HH:mm"))),
                row));
            h->addLayout(text, 1);
            auto* back = new QPushButton(tr("bring back"), row);
            back->setObjectName("bringBack");
            back->setFlat(true);
            back->setCursor(Qt::PointingHandCursor);
            const QString id = t.id;
            connect(back, &QPushButton::clicked, this,
                    [this, id]() { emit bringBackRequested(id); });
            h->addWidget(back);
            v->insertWidget(v->count() - 1, row);
        }
    }
    Q_UNUSED(at);
    panel->open();
}

QWidget* NeedsBlockCard::makeTaskRow(const Task& task, int rung,
                                     const QDateTime& now, bool focus)
{
    const Category* cat = m_data->categoryById(task.categoryId);

    // The accent rail's colour answers "how worried should I be?" before a
    // word is read: danger for anything escalated or already late, otherwise
    // the task's own category colour. One hue per meaning, as everywhere —
    // the rail is not decoration, it is the same fact the meta line spells
    // out, arriving a beat earlier.
    const bool alarming = rung > 0 || task.isOverdue(now);
    const QColor accent = alarming ? theme::danger()
                                   : (cat ? cat->color : theme::inkSoft());

    auto* row = new QFrame(this);
    row->setObjectName(focus ? "needsBlockFocus"
                             : (rung > 0 ? "needsBlockRowEscalated"
                                         : "needsBlockRow"));
    if (focus)
        row->setStyleSheet(QStringLiteral(
            "#needsBlockFocus { border-left: 3px solid %1; "
            "background: %2; border-radius: 6px; }")
                               .arg(accent.name(),
                                    theme::pastel(accent).name()));
    else if (rung > 0)
        row->setStyleSheet(QStringLiteral(
            "#needsBlockRowEscalated { border-left: 3px solid %1; "
            "border-radius: 4px; }").arg(theme::danger().name()));

    auto* v = new QVBoxLayout(row);
    v->setContentsMargins(focus ? 12 : (rung > 0 ? 8 : 0),
                          focus ? 10 : 4,
                          focus ? 12 : 0,
                          focus ? 10 : 4);
    v->setSpacing(focus ? 5 : 3);

    // Line 1: the title. In focus density the accent rail already carries
    // the category, so the dot would be saying it twice — dropped, and the
    // title gets the room instead.
    auto* title = new QLabel(task.title, row);
    title->setWordWrap(true);
    if (focus) {
        QFont f = title->font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        f.setWeight(QFont::DemiBold);
        title->setFont(f);
        v->addWidget(title);
    } else {
        auto* line1 = new QHBoxLayout;
        auto* dot = new QLabel(row);
        dot->setFixedSize(9, 9);
        dot->setStyleSheet(
            QStringLiteral("background:%1;border-radius:4px;")
                .arg((cat ? cat->color : theme::inkSoft()).name()));
        title->setStyleSheet(QStringLiteral("font-weight:600;"));
        line1->addWidget(dot);
        line1->addWidget(title, 1);
        v->addLayout(line1);
    }

    // Line 2: urgency + the date, absolute and relative.
    QString meta;
    if (task.priority == Task::Priority::Urgent)
        meta = tr("URGENT · ");
    meta += dueText(task, now.date());
    if (cat)
        meta += QStringLiteral(" · ") + cat->name;
    v->addWidget(softLabel(meta, row));

    // The why-line — only when there IS a why (§A explainability).
    const QString why =
        reasonText(m_data->taskUncoveredReason(task.id, now.date()));
    if (!why.isEmpty()) {
        auto* w = softLabel(why, row);
        w->setStyleSheet(QStringLiteral("color:%1;").arg(
            theme::danger().name()));
        v->addWidget(w);
    }
    if (rung > 0)
        v->addWidget(softLabel(
            tr("Put off %n time(s)%1", nullptr, task.dismissCount)
                .arg(rung == 2 ? tr(" — this one stays visible")
                               : QString()),
            row));

    // Actions. The escalated "Not today…" opens the decision menu instead
    // of dismissing in one click — the frictionless dodge is what rung 1
    // exists to remove (§D: specificity, not volume).
    const QString id = task.id;
    if (m_decisionFor == id) {
        v->addWidget(makeDecisionMenu(task));
    } else {
        auto* acts = new QHBoxLayout;
        if (focus)
            acts->setSpacing(8);
        auto* plan = new QPushButton(tr("Find time"), row);
        plan->setObjectName("findTime");
        // In focus density this is THE action the card is asking for, so it
        // takes the default-button styling (and the Enter key with it).
        if (focus)
            plan->setDefault(true);
        connect(plan, &QPushButton::clicked, this,
                [this, id]() { emit planTaskRequested(id); });
        // Focus density splits the width evenly between the two answers;
        // compact density lets them size themselves and pads the remainder.
        // The stretch is the ONLY difference — same buttons, same handlers.
        acts->addWidget(plan, focus ? 1 : 0);

        auto* later = new QPushButton(
            rung > 0 ? tr("Not today…") : tr("Not today"), row);
        later->setObjectName("notToday");
        connect(later, &QPushButton::clicked, this, [this, id, rung, now]() {
            if (rung > 0) {
                m_decisionFor = id;
                refresh(now); // m_decisionFor is in the print — rebuilds
            } else {
                emit dismissRequested(id);
            }
        });
        acts->addWidget(later, focus ? 1 : 0);
        if (!focus)
            acts->addStretch(1);
        v->addLayout(acts);
    }
    return row;
}

QWidget* NeedsBlockCard::makeDecisionMenu(const Task& task)
{
    // §D's four options. The middle two CHANGE THE DATA — five dodges mean
    // either the deadline is wrong or the priority is, and either
    // correction is honest information. Putting it off remains available;
    // it just says the count out loud.
    auto* menu = new QFrame(this);
    menu->setObjectName("decisionMenu");
    menu->setStyleSheet(QStringLiteral(
        "#decisionMenu { border:1px solid %1; border-radius:6px; }")
                            .arg(theme::danger().name()));
    auto* v = new QVBoxLayout(menu);
    v->setContentsMargins(8, 6, 8, 6);
    v->setSpacing(4);

    auto* q = softLabel(
        tr("Put off %n time(s). Something has to give — which is it?",
           nullptr, task.dismissCount),
        menu);
    q->setStyleSheet(QStringLiteral("color:%1;font-weight:600;")
                         .arg(theme::danger().name()));
    v->addWidget(q);

    const QString id = task.id;
    const auto add = [this, menu, v](const QString& text,
                                     const QString& objectName,
                                     std::function<void()> fire) {
        auto* b = new QPushButton(text, menu);
        b->setObjectName(objectName);
        connect(b, &QPushButton::clicked, this, std::move(fire));
        v->addWidget(b);
    };
    add(tr("Give it time — find a block"), QStringLiteral("decidePlan"),
        [this, id]() { m_decisionFor.clear(); emit planTaskRequested(id); });
    add(tr("The deadline was wrong — change it"),
        QStringLiteral("decideDeadline"),
        [this, id]() { m_decisionFor.clear(); emit editDeadlineRequested(id); });
    add(tr("It isn't urgent after all — drop to Medium"),
        QStringLiteral("decideNotUrgent"),
        [this, id]() { m_decisionFor.clear(); emit notUrgentRequested(id); });
    add(tr("Put it off again anyway"), QStringLiteral("decideDismiss"),
        [this, id]() { m_decisionFor.clear(); emit dismissRequested(id); });
    return menu;
}
