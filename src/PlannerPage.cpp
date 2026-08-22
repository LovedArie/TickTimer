#include "PlannerPage.h"

#include "Stats.h"

#include "AgendaWidget.h"
#include "DueDateDialog.h" // "the deadline was wrong" — reuse, not a new box
#include "EventDialog.h"
#include "GlancePanel.h"
#include "NeedsBlockCard.h"
#include "PickActivityDialog.h"
#include "Prefs.h"         // the dismissal clock, read at fire time
#include "ReviewWidgets.h"
#include "TaskDetailDialog.h"
#include "WeekAgendaView.h"
#include "AppData.h"
#include "Event.h"
#include "Task.h"
#include "TrackerService.h"
#include "ResponsiveWatcher.h"
#include "Widgets.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
#include "Prefs.h"

#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

PlannerPage::PlannerPage(AppData* data, TrackerService* tracker,
                         QWidget* parent)
    : QWidget(parent)
    , m_data(data)
    , m_tracker(tracker)
    , m_date(QDate::currentDate())
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 16, 26, 20);
    layout->setSpacing(12);

    // ---- top bar: ‹ Today ›            [Day | Week | Month] --------------
    auto* topBar = new QHBoxLayout;

    auto* prev = new QPushButton(QStringLiteral("\u2039"), this);
    auto* next = new QPushButton(QStringLiteral("\u203A"), this);
    prev->setFixedWidth(34);
    next->setFixedWidth(34);
    connect(prev, &QPushButton::clicked, this, [this]() { shiftPeriod(-1); });
    connect(next, &QPushButton::clicked, this, [this]() { shiftPeriod(+1); });

    // The period display is a BUTTON dressed as a label: the moment it
    // became clickable, QLabel stopped being the right class. A button
    // brings the clicked signal, keyboard focus, and Enter-to-activate
    // for free; the costume (no border, label-like text) is stylesheet
    // work — see #periodBtn in Theme.h. Clicking cycles Day -> Week ->
    // Month; the pointing-hand cursor and hover colour are the
    // discoverability cues that make the shortcut findable at all.
    m_viewSwitcher = new QPushButton(this);
    m_viewSwitcher->setObjectName("viewSwitcher");
    m_viewSwitcher->setMinimumWidth(150);
    m_viewSwitcher->setCursor(Qt::PointingHandCursor);
    m_viewSwitcher->setToolTip(
        tr("Left-click: switch view (Day \u2192 Week \u2192 Month)\n"
           "Right-click: jump back to today"));
    connect(m_viewSwitcher, &QPushButton::clicked, this,
            [this]() { setMode((m_mode + 1) % 3); });
    // A QPushButton has no rightClicked signal — Qt routes secondary clicks
    // through the CONTEXT MENU channel instead. Opting into
    // CustomContextMenu turns that channel into a plain signal we can wire to
    // anything, which is the idiomatic way to give a button a second verb
    // without subclassing it just to override mousePressEvent. (Bonus: the
    // same signal fires for the keyboard's Menu key and for a long-press on
    // touch, so the shortcut is reachable without a mouse.)
    m_viewSwitcher->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_viewSwitcher, &QWidget::customContextMenuRequested,
            this, &PlannerPage::goToToday);

    // "Task notes" — a DISPLAY PREFERENCE, so it follows the Pomodoro
    // durations' rule: it lives in QSettings, never in data.json. The
    // classification question ("domain fact, presentation, or setting?")
    // is asked once per new fact; this one changes how blocks LOOK, not
    // what is true — a setting. Default ON: the owner asked to see them,
    // so they show immediately; unticking hides and is remembered.
    auto* taskNotes = new QCheckBox(tr("Task notes"), this);
    {
        QSettings settings;
        taskNotes->setChecked(
            settings.value(QStringLiteral("planner/showTaskNotes"), true)
                .toBool());
    }
    const auto applyTaskNotes = [this](bool on) {
        m_agenda->setShowTaskDescriptions(on);
        m_weekAgenda->setShowTaskDescriptions(on);
    };
    connect(taskNotes, &QCheckBox::toggled, this,
            [applyTaskNotes](bool on) {
                QSettings settings;
                settings.setValue(QStringLiteral("planner/showTaskNotes"), on);
                applyTaskNotes(on);
            });

    topBar->addStretch(1);
    topBar->addWidget(prev);
    topBar->addWidget(m_viewSwitcher);
    topBar->addWidget(next);

    // The Day/Week/Month segmented control used to live here. Removed by
    // the owner's call once the view switcher covered its job — the
    // switcher's own text ("Today" / "Week of…" / "July 2026") already
    // says which view you're in. Known costs, accepted for a cleaner bar
    // in a single-user tool: no direct jump to a specific view (you cycle
    // through), and less discoverability for a first-time user. If direct
    // access is ever missed, a QMenu on the view switcher is the escape
    // hatch.

    layout->addLayout(topBar);

    // ---- the three views, stacked; only one visible at a time ------------
    m_stack = new QStackedWidget(this);

    // Day view: the agenda panel on the left, the glance panel on the right.
    auto* dayView = new QWidget(this);
    auto* dayLayout = new QHBoxLayout(dayView);
    dayLayout->setContentsMargins(0, 0, 0, 0);
    dayLayout->setSpacing(18);

    auto* agendaPanel = new QFrame(dayView);
    agendaPanel->setObjectName("panel");
    auto* agendaLayout = new QVBoxLayout(agendaPanel);
    agendaLayout->setContentsMargins(16, 14, 16, 14);
    agendaLayout->setSpacing(6);
    m_agendaLayout = agendaLayout;
    auto* agendaTitle = new QLabel(tr("Your day"), agendaPanel);
    agendaTitle->setObjectName("h2");
    m_agendaSub = new QLabel(
        tr("6 AM to midnight · 30-minute slots · click a free slot to say "
           "what you're doing"),
        agendaPanel);
    m_agendaSub->setObjectName("sub");
    // Word wrap changes a QLabel's CONTRACT with the layout: without it the
    // label's minimum width is its full text width (~500px here — wider than
    // a phone), with it the label can fold and stops dictating the window's
    // minimum. On desktop nothing visibly changes; it still fits one line.
    m_agendaSub->setWordWrap(true);

    // The "Due today" strip: tasks whose due date is the viewed day, shown
    // read-only above the timeline. Built empty and hidden; rebuildDueStrip()
    // fills it and reveals it only when the day actually has due tasks — an
    // empty strip is visual noise, so it earns its space or disappears.
    m_duePanel = new QFrame(agendaPanel);
    m_duePanel->setObjectName("duePanel");
    m_duePanel->setStyleSheet(
        "#duePanel { background:#F4F6F2; border:1px solid #E6E9E4; "
        "border-radius:10px; }");
    m_dueLayout = new QVBoxLayout(m_duePanel);
    m_dueLayout->setContentsMargins(12, 10, 12, 10);
    m_dueLayout->setSpacing(6);
    m_duePanel->hide();

    m_agenda = new AgendaWidget(m_data, m_tracker, agendaPanel);
    auto* agendaScroll = new QScrollArea(agendaPanel);
    agendaScroll->setWidgetResizable(true);
    agendaScroll->setWidget(m_agenda);
    makeTouchScrollable(agendaScroll); // finger-flick on Android/touchscreens
    m_agendaScroll = agendaScroll;

    // "Your day" on the left, the Task-notes toggle hugging the right —
    // the toggle sits directly ON the thing it changes (owner request),
    // instead of floating in the page-wide top bar next to date navigation
    // it has nothing to do with. Controls near their effect need no label
    // explaining what they affect; distance is what creates that need.
    auto* agendaHead = new QHBoxLayout;
    agendaHead->addWidget(agendaTitle);
    agendaHead->addStretch(1);
    agendaHead->addWidget(taskNotes);
    agendaLayout->addLayout(agendaHead);
    agendaLayout->addWidget(m_agendaSub);

    // ---- the placing banner (needs-a-block part 3) ------------------------
    // Lives above the due strip, hidden until "Find time" starts a
    // placement. Everything inside is rebuilt by refreshPlacing() — the
    // banner has no state of its own beyond m_placingTaskId.
    m_placingBanner = new QFrame(agendaPanel);
    m_placingBanner->setObjectName("placingBanner");
    m_placingBanner->setStyleSheet(QStringLiteral(
        "#placingBanner { background: rgba(47,126,110,0.08); "
        "border: 1px solid rgba(47,126,110,0.35); border-radius: 10px; }"));
    {
        auto* v = new QVBoxLayout(m_placingBanner);
        v->setContentsMargins(10, 8, 10, 8);
        v->setSpacing(6);
        auto* top = new QHBoxLayout;
        m_placingLabel = new QLabel(m_placingBanner);
        m_placingLabel->setWordWrap(true);
        top->addWidget(m_placingLabel, 1);
        auto* cancel = new QPushButton(tr("Cancel"), m_placingBanner);
        cancel->setObjectName("placeCancel");
        connect(cancel, &QPushButton::clicked,
                this, &PlannerPage::cancelPlacing);
        top->addWidget(cancel);
        v->addLayout(top);
        m_placingStrip = new QHBoxLayout;
        m_placingStrip->setSpacing(5);
        v->addLayout(m_placingStrip);
    }
    m_placingBanner->hide();
    agendaLayout->addWidget(m_placingBanner);
    agendaLayout->addWidget(m_duePanel);
    agendaLayout->addWidget(agendaScroll, 1);

    m_glance = new GlancePanel(m_data, m_tracker, dayView);
    m_glance->setFixedWidth(320);
    // Whether it SHOWS is decided in applyLayoutMode(), not here: a fixed
    // 320px sidebar is affordable or it is not, and that depends on the width
    // this page was handed, which changes while the app runs.

    dayLayout->addWidget(agendaPanel, 1);
    dayLayout->addWidget(m_glance);

    // ---- needs-a-block: connect the glance card to the deciding slots -----
    // (Named slots, not lambdas, since v21.5: the week tab's card connects
    // to the SAME five — one target per action is what keeps two surfaces
    // behaviourally identical. The slot bodies live at the end of the file.)
    connect(m_glance, &GlancePanel::planTaskRequested,
            this, &PlannerPage::onNbPlan);
    connect(m_glance, &GlancePanel::editDeadlineRequested,
            this, &PlannerPage::onNbEditDeadline);
    connect(m_glance, &GlancePanel::notUrgentRequested,
            this, &PlannerPage::onNbNotUrgent);
    connect(m_glance, &GlancePanel::dismissRequested,
            this, &PlannerPage::onNbDismiss);
    connect(m_glance, &GlancePanel::bringBackRequested,
            this, &PlannerPage::onNbBringBack);
    connect(m_glance, &GlancePanel::catchUpAcceptRequested,
            this, &PlannerPage::onCuAccept);
    connect(m_glance, &GlancePanel::catchUpResolveRequested,
            this, &PlannerPage::onCuResolve);
    connect(m_glance, &GlancePanel::catchUpShowDayRequested,
            this, &PlannerPage::onCuShowDay);
    connect(m_glance, &GlancePanel::catchUpResolveAllRequested,
            this, &PlannerPage::onCuResolveAll);

    m_week  = new WeekReviewPage(m_data, this);
    m_month = new MonthReviewPage(m_data, this);

    // Week tab: the seven-day agenda on TOP, the review stats BELOW it, in one
    // vertical scroll. The agenda is tall (6 AM–midnight), and you wanted the
    // stats kept — so the tab scrolls to show both rather than choosing one.
    m_weekAgenda = new WeekAgendaView(m_data, m_tracker, this);
    auto* weekInner = new QWidget;
    auto* weekLayout = new QVBoxLayout(weekInner);
    weekLayout->setContentsMargins(4, 4, 4, 4);
    weekLayout->setSpacing(18);
    // The week tab gets its OWN NeedsBlockCard (needs-a-block part 3):
    // the derived list is view-independent (the viewed date is not an
    // input to the flag), so this is the SAME query rendered where the
    // week has room — not a copy that could drift. Both cards connect to
    // the same five slots; the shared lastReview means one "Show my day"
    // opens both gates, which is the correct reading of "one look".
    m_weekCard = new NeedsBlockCard(m_data, this);
    m_weekCard->setMaximumWidth(380);
    connect(m_weekCard, &NeedsBlockCard::planTaskRequested,
            this, &PlannerPage::onNbPlan);
    connect(m_weekCard, &NeedsBlockCard::editDeadlineRequested,
            this, &PlannerPage::onNbEditDeadline);
    connect(m_weekCard, &NeedsBlockCard::notUrgentRequested,
            this, &PlannerPage::onNbNotUrgent);
    connect(m_weekCard, &NeedsBlockCard::dismissRequested,
            this, &PlannerPage::onNbDismiss);
    connect(m_weekCard, &NeedsBlockCard::bringBackRequested,
            this, &PlannerPage::onNbBringBack);
    connect(m_weekCard, &NeedsBlockCard::reviewed,
            this, &PlannerPage::refresh);
    weekLayout->addWidget(m_weekCard);
    weekLayout->addWidget(m_weekAgenda);
    weekLayout->addWidget(m_week);
    weekLayout->addStretch(0);
    auto* weekScroll = new QScrollArea(this);
    weekScroll->setWidgetResizable(true);
    weekScroll->setWidget(weekInner);
    makeTouchScrollable(weekScroll);

    // The week columns report clicks; the page plans, on that column's date —
    // reusing the exact path the single-day agenda uses (planAt / onEventClicked).
    connect(m_weekAgenda, &WeekAgendaView::emptySlotClicked,
            this, &PlannerPage::planAt);
    connect(m_weekAgenda, &WeekAgendaView::eventClicked,
            this, &PlannerPage::onEventClicked);
    connect(m_weekAgenda, &WeekAgendaView::eventResized,
            this, &PlannerPage::onEventResized);

    // Both views exist now — hand them the loaded preference. The widgets
    // are TOLD; only this page ever touches QSettings for it.
    applyTaskNotes(taskNotes->isChecked());

    // Month gets the SAME scroll wrapper the week tab already has — a
    // QScrollArea's minimum ignores its content, so the chart page stops
    // dictating the window's minimum width (the layout probe caught it at
    // 498px). On a phone the charts scroll; on desktop nothing changes.
    auto* monthScroll = new QScrollArea(this);
    monthScroll->setWidgetResizable(true);
    monthScroll->setWidget(m_month);
    makeTouchScrollable(monthScroll);

    m_stack->addWidget(dayView);     // index 0 == mode 0 (day)
    m_stack->addWidget(weekScroll);  // index 1 (week: agenda + stats)
    m_stack->addWidget(monthScroll); // index 2 (month)
    layout->addWidget(m_stack, 1);

    // ---- wiring ------------------------------------------------------------
    connect(m_agenda, &AgendaWidget::emptySlotClicked,
            this, &PlannerPage::onEmptySlotClicked);
    connect(m_agenda, &AgendaWidget::eventClicked,
            this, &PlannerPage::onEventClicked);
    connect(m_agenda, &AgendaWidget::eventResized,
            this, &PlannerPage::onEventResized);

    connect(m_data, &AppData::changed, this, &PlannerPage::refresh);

    // The 1-second tick repaints the live numbers. The agenda gets a plain
    // update() (repaint me); the glance re-derives its stats. Cheap enough
    // at once a second — measure before optimizing (and it only ticks
    // while a timer runs at all).
    connect(m_tracker, &TrackerService::tick, this, [this]() {
        m_agenda->update();
        m_glance->refresh();
    });
    connect(m_tracker, &TrackerService::stateChanged, this,
            &PlannerPage::refresh);

    applyDisplayPrefs(); // initial read — ends with updateViewSwitcher()
    rebuildDueStrip();
}

// ---- responding to the container's size class --------------------------------

bool PlannerPage::event(QEvent* e)
{
    if (e->type() == ResponsiveModeEvent::type())
        applyLayoutMode(static_cast<ResponsiveModeEvent*>(e)->mode());

    return QWidget::event(e);
}

void PlannerPage::applyLayoutMode(responsive::Mode mode)
{
    // Expanded ONLY — a sharpening, not just a port. The glance is a fixed
    // 320px, so at Medium (600-840) it would leave the agenda, the thing this
    // page is FOR, under 520px. The old screen-based test hid it on phones
    // and showed it everywhere else, including at window widths where it was
    // already crowding the agenda out.
    //
    // Nothing is lost when it goes: every number on it is DERIVED, never
    // stored, and the Week and Month reviews recompute the same truths.
    m_glance->setVisible(mode == responsive::Mode::Expanded);

    // ---- give the agenda its room back --------------------------------------
    // The day grid is the reason this page exists, and on a phone it was
    // getting the leftovers: a 64px hour-label gutter and 32px of panel margin
    // out of 360, with a scrollbar taking a slice of what remained.
    const bool compact = mode == responsive::Mode::Compact;

    // The gutter only has to fit "12 PM". 64px is generous on a desktop and
    // 18% of a phone's width.
    m_agenda->setGutter(compact ? AgendaWidget::kCompactGutter
                                : AgendaWidget::kDefaultGutter);

    // No scrollbar on a touchscreen. It is not an affordance there — the
    // gesture is the affordance, and makeTouchScrollable() already provides
    // it — so the bar is pure width spent on decoration, drawn over the
    // content it is stealing room from.
    m_agendaScroll->setVerticalScrollBarPolicy(
        compact ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    m_agendaScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // The subtitle is an instruction, so it has to tell the truth about the
    // gesture THIS device uses. On a touchscreen a tap is how you scroll, so
    // planning moved to a long press (AgendaWidget's header explains why) —
    // and a caption still saying "click" would be teaching the one gesture
    // that no longer works.
    if (m_agendaSub) {
        m_agendaSub->setText(
            compact ? tr("6 AM to midnight · 30-minute slots · press and hold "
                         "a free slot to say what you're doing")
                    : tr("6 AM to midnight · 30-minute slots · click a free "
                         "slot to say what you're doing"));
    }

    if (m_agendaLayout)
        m_agendaLayout->setContentsMargins(compact ? 8 : 16, compact ? 10 : 14,
                                           compact ? 8 : 16, compact ? 10 : 14);
}

void PlannerPage::applyDisplayPrefs()
{
    const auto window = prefs::agendaWindow();
    m_agenda->setVisibleWindow(window.first, window.second);
    m_weekAgenda->setVisibleWindow(window.first, window.second);

    m_firstDay = prefs::firstDayOfWeek();
    m_weekAgenda->setFirstDayOfWeek(m_firstDay);
    m_week->setFirstDayOfWeek(m_firstDay);
    m_month->setFirstDayOfWeek(m_firstDay);
    updateViewSwitcher(); // the "Week of …" label snaps with the same pref
}

void PlannerPage::shiftPeriod(int direction)
{
    // ‹ and › move by whatever the current view shows — a day, a week, or
    // a month. One date field serves all three views; each interprets it.
    switch (m_mode) {
    case 0: m_date = m_date.addDays(direction);      break;
    case 1: m_date = m_date.addDays(7 * direction);  break;
    case 2: m_date = m_date.addMonths(direction);    break;
    }
    applyDate();
}

void PlannerPage::goToToday()
{
    // Deliberately does NOT change the view mode: you asked to come home, not
    // to be moved to a different room. Right-clicking in Month view lands on
    // this month; in Day view, on today. The gesture means one thing
    // everywhere, which is what makes it safe to reach for without looking.
    if (m_date == QDate::currentDate())
        return; // already home; don't churn every sub-view for nothing
    m_date = QDate::currentDate();
    applyDate();
}

void PlannerPage::applyDate()
{
    // The one "the date moved" routine. Every navigation gesture ends here,
    // so none of them can drift apart or forget a sub-view — adding
    // goToToday() cost exactly zero new update logic, which is the whole
    // return on extracting this.
    m_agenda->setDate(m_date);
    m_glance->setDate(m_date);
    m_weekAgenda->setDate(m_date);
    m_week->setDate(m_date);
    m_month->setDate(m_date);
    updateViewSwitcher();
    rebuildDueStrip(); // the viewed day moved — its due tasks changed
}

void PlannerPage::setMode(int mode)
{
    m_mode = mode;
    m_stack->setCurrentIndex(mode);
    updateViewSwitcher();
}

void PlannerPage::updateViewSwitcher()
{
    const QDate today = QDate::currentDate();
    QString text;
    switch (m_mode) {
    case 0:
        if (m_date == today)                 text = tr("Today");
        else if (m_date == today.addDays(-1)) text = tr("Yesterday");
        else if (m_date == today.addDays(1))  text = tr("Tomorrow");
        else                                  text = m_date.toString("ddd, MMM d");
        break;
    case 1: {
        // The SAME snap the week view and week review use — three screens,
        // one formula, or "Week of Jul 6" could sit above a grid that
        // starts Jul 5.
        const QDate start = stats::weekStart(m_date, m_firstDay);
        text = tr("Week of %1").arg(start.toString("MMM d"));
        break;
    }
    case 2:
        text = m_date.toString("MMMM yyyy");
        break;
    }
    m_viewSwitcher->setText(text);
}

void PlannerPage::onEmptySlotClicked(int slotIndex)
{
    // The day view always plans on the currently-viewed day.
    planAt(m_date, slotIndex);
}

void PlannerPage::planAt(QDate date, int slotIndex)
{
    // Placement interception (needs-a-block part 3). Because THIS is the
    // single planning step both the day agenda and every week column route
    // through, one interception makes "Find time" work from both views —
    // the reuse payoff of having refused two planning paths back in the
    // week-agenda session.
    if (!m_placingTaskId.isEmpty()) {
        if (!m_data->taskById(m_placingTaskId)) { // deleted mid-placement
            cancelPlacing();
            return;
        }
        const int slotStart =
            plan::kDayStartMinutes + slotIndex * plan::kSlotMinutes;
        // Place one hour, clamped into the free run under the click — the
        // prototype's rule, kept because drag-to-resize already exists:
        // "an hour to start; stretch the block if it needs more" is
        // honest and one click. A click outside any run does nothing (the
        // highlights say where clicks count).
        for (const auto& run : freeRunsFor(date, plan::kSlotMinutes)) {
            if (slotStart < run.first || slotStart >= run.second)
                continue;
            const int end = qMin(slotStart + 60, run.second);
            m_data->addTaskEvent(date, slotStart, end, m_placingTaskId);
            // Done either way it landed: within the deadline the task just
            // left the list; past it, the card's why-line explains — the
            // system says so, it doesn't nag twice (§A explainability).
            cancelPlacing();
            return;
        }
        return; // stay placing; wrong spot, no side effects
    }

    // UC1, the whole main flow in four lines: ask (dialog), and if the user
    // confirmed, tell the domain. The dialog collected the answer; AppData
    // applies the rules; changed() repaints the world. Parented to `this`
    // (the page) — a stable widget, never deleted mid-dialog.
    PickActivityDialog dialog(m_data, date, slotIndex, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int start = dialog.chosenStartMinutes();
    const int end   = dialog.chosenEndMinutes();

    // Three answers, three domain doors — the switch makes the mapping
    // exhaustive and visible. Note the page still DECIDES nothing about
    // legality: each door re-checks isFree and can refuse.
    switch (dialog.chosenKind()) {
    case PickActivityDialog::Kind::Activity:
        m_data->addEvent(date, start, end, dialog.chosenActivityId(),
                         dialog.enteredTitle()); // typed text = block label
        break;
    case PickActivityDialog::Kind::Task:
        m_data->addTaskEvent(date, start, end, dialog.chosenTaskId());
        break;
    case PickActivityDialog::Kind::AdHoc:
        m_data->addAdHocEvent(date, start, end, dialog.enteredTitle());
        break;
    case PickActivityDialog::Kind::None:
        break; // dialog accepted with no choice can't happen, but be exhaustive
    }
}

void PlannerPage::onEventClicked(const QString& eventId)
{
    EventDialog dialog(m_data, m_tracker, eventId, this);
    dialog.exec();
    // Nothing to do afterwards: every change the dialog made already went
    // through AppData/TrackerService and reached us via changed().
}

void PlannerPage::onEventResized(const QString& id, int startMin, int endMin)
{
    // The whole feature funnels through the ONE guard: resizeEvent enforces
    // in-bounds / no-overlap / one-slot-minimum and refuses otherwise. If it
    // refuses (returns false) nothing changes and the block simply repaints at
    // its old span on the next changed() — no special-case needed here.
    m_data->resizeEvent(id, startMin, endMin);
}

void PlannerPage::refresh()
{
    m_agenda->update();
    m_glance->refresh();
    if (m_weekCard) // the SAME derived list, second surface (part 3)
        m_weekCard->refresh(QDateTime::currentDateTime());
    refreshPlacing(); // banner + day strip + highlights, all derived
    rebuildDueStrip(); // a task's date/title/done may have changed
    // Week/month pages listen to AppData::changed themselves.
}

void PlannerPage::rebuildDueStrip()
{
    // Clear the old content. deleteLater (not delete) is deliberate: this
    // method can run as a side effect of clicking a row INSIDE the strip
    // (edit a task -> updateTask -> changed() -> refresh() -> here), and a
    // widget must outlive the signal it is currently emitting. Reparenting
    // to nullptr removes it from view immediately; the event loop frees it.
    while (QLayoutItem* item = m_dueLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }

    const auto due = m_data->tasksDueOn(m_date);
    if (due.isEmpty()) {
        m_duePanel->hide(); // no due tasks -> the strip disappears entirely
        return;
    }
    m_duePanel->show();

    const QDate today = QDate::currentDate();
    const QString when = (m_date == today)
                             ? tr("DUE TODAY")
                             : tr("DUE %1").arg(m_date.toString("MMM d").toUpper());
    auto* head = new QLabel(when, m_duePanel);
    head->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    m_dueLayout->addWidget(head);

    for (const Task* task : due) {
        auto* rowW = new QWidget(m_duePanel);
        auto* row = new QHBoxLayout(rowW);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);

        auto* dot = new QLabel(rowW);
        const Category* c = m_data->categoryById(task->categoryId);
        dot->setPixmap(colorDot(c ? c->color : QColor("#616974"), 9));

        // Clickable, opening the same detail panel as everywhere else —
        // one editor for a task, reached from wherever the task appears.
        auto* titleBtn = new QPushButton(task->title, rowW);
        titleBtn->setFlat(true);
        titleBtn->setCursor(Qt::PointingHandCursor);
        titleBtn->setStyleSheet(
            "QPushButton { border:none; background:transparent; "
            "text-align:left; padding:1px 0; color:#2B2F36; } "
            "QPushButton:hover { color:#2F7E6E; }");
        if (!task->description.trimmed().isEmpty())
            titleBtn->setToolTip(task->description);

        const QString taskId = task->id;
        connect(titleBtn, &QPushButton::clicked, this, [this, taskId]() {
            // v28.5: one call, fresh read by id — no snapshot to go
            // stale. Note this strip includes DATED PIECES (tasksDueOn's
            // policy), so this click is also how a piece opens straight
            // from the planner — breadcrumb and all.
            runTaskDetail(*m_data, taskId, this);
        });

        row->addWidget(dot);
        row->addWidget(titleBtn, 1);
        // v22: the deadline clock, right where the day is being planned —
        // this strip's whole job is "what does today owe you", and "by 17:00"
        // is half that answer. Sorted into the strip by time upstream
        // (AppData::tasksDueOn), so the chips read top-to-bottom in order.
        if (task->dueTime.isValid()) {
            auto* when = new QLabel(dueTimeLabel(task->dueTime), rowW);
            when->setStyleSheet(
                "background:#FFFFFF; border-radius:8px; padding:2px 7px; "
                "color:#2F7E6E; font-size:11px; font-weight:600;");
            row->addWidget(when);
        }
        if (task->repeat != Task::Repeat::None) {
            auto* chip = new QLabel(
                QStringLiteral("\u27F3 %1").arg(repeatLabel(task->repeat)), rowW);
            chip->setStyleSheet(
                "background:#FFFFFF; border-radius:8px; padding:2px 7px; "
                "color:#616974; font-size:11px;");
            row->addWidget(chip);
        }
        m_dueLayout->addWidget(rowW);
    }
}

// ---- needs-a-block: the deciding slots (parts 2–3) -------------------------
// Two cards ask (glance panel, week tab); these five decide — once. Each is
// one obvious step through an existing domain door; the cards never learn
// what any of them cost.

void PlannerPage::onNbPlan(const QString& taskId)
{
    // "Find time" (part 3): enter placing mode, jump to the EARLIEST day
    // that both fits an hour and beats the deadline. If nothing before the
    // deadline fits, land on today anyway — the strip shows the refused
    // days red rather than hiding them (shown-and-refused beats silently
    // absent; the prototype argued this and the owner agreed).
    const Task* task = m_data->taskById(taskId);
    if (!task)
        return;
    m_placingTaskId = taskId;
    setMode(0); // the day strip lives on the day view

    const QDate today    = QDate::currentDate();
    const QDate deadline = coverage::deadlineOf(*task, today);
    const int span = deadline.isValid()
                         ? int(qMin<qint64>(today.daysTo(deadline), 13))
                         : 6;
    QDate best = today;
    for (int d = 0; d <= span; ++d) {
        const QDate day = today.addDays(d);
        if (!freeRunsFor(day, 60).isEmpty()) {
            best = day;
            break;
        }
    }
    m_date = best;
    updateViewSwitcher();
    m_agenda->setDate(m_date);
    refresh();
}

void PlannerPage::onNbEditDeadline(const QString& taskId)
{
    // "The deadline was wrong" reuses DueDateDialog — the owner's call,
    // and the cheaper one: the calendar picker, validation, and TBD
    // handling come free, and setTaskDueDate stays the one door (mirrors
    // ActivitiesPage::chooseDueDate line for line).
    const Task* task = m_data->taskById(taskId);
    if (!task)
        return;
    DueDateDialog dialog(task->dueDate, task->dueTime, window());
    if (dialog.exec() == QDialog::Accepted)
        m_data->setTaskDueDate(taskId, dialog.chosenDate(), dialog.chosenTime());
}

void PlannerPage::onNbNotUrgent(const QString& taskId)
{
    m_data->setTaskPriority(taskId, Task::Priority::Medium);
}

// ---- catch-up (v26.2 §K) ---------------------------------------------------

void PlannerPage::onCuAccept(const QString& eventId,
                             const reschedule::Option& option)
{
    // One signal, two doors: a single placement takes the simple door, a
    // split takes the all-or-nothing one. The CARD doesn't know the doors
    // exist — it hands over the option verbatim, and this page translates.
    if (option.pieces.isEmpty())
        return;

    if (option.pieces.size() == 1) {
        const reschedule::Piece& p = option.pieces.first();
        m_data->rescheduleBlock(eventId, p.date, p.startMinutes,
                                p.endMinutes);
    } else {
        QVector<AppData::BlockSpan> spans;
        spans.reserve(option.pieces.size());
        for (const reschedule::Piece& p : option.pieces)
            spans.append({p.date, p.startMinutes, p.endMinutes});
        m_data->rescheduleBlockSplit(eventId, spans);
    }
    // No refresh() call here, on purpose: the door emits changed(), and the
    // usual pipeline repaints every surface — including the card, whose
    // fingerprint now differs. Calling refresh here too would be the app
    // updating twice for one fact, the exact duplication changed() exists
    // to prevent. (Both doors can also DECLINE — slot taken between propose
    // and click. Then nothing changed, nothing repaints, and the proposal
    // stays on screen still pressable; the next changed() from any source
    // re-proposes against current reality. Quiet, but never wrong.)
}

void PlannerPage::onCuResolve(const QString& eventId, BlockOutcome outcome)
{
    m_data->resolveBlock(eventId, outcome);
}

void PlannerPage::onCuResolveAll(const QStringList& eventIds,
                                 BlockOutcome outcome)
{
    // The bulk door, NOT a loop over the single one: 46 blocks through
    // resolveBlock would be 46 changed() emissions, each a synchronous
    // repaint of every surface. One decision, one door, one emission.
    m_data->resolveBlocks(eventIds, outcome);
}

void PlannerPage::onCuShowDay(QDate date)
{
    // The dead-end row's escape hatch: put the contested day on screen so
    // the user can clear the way by hand. Reuses the page's one date-moved
    // routine — a navigation added anywhere else would eventually forget
    // the due strip (the exact bug applyDate() was extracted to prevent).
    setMode(0);
    m_date = date;
    applyDate();
}

void PlannerPage::onNbDismiss(const QString& taskId)
{
    // `until` from THIS machine's dismissal clock, read at fire time —
    // change the policy in Settings and the very next dismissal honours
    // it. The domain stores only the resulting fact.
    m_data->dismissTask(taskId,
                        prefs::dismissReturnPolicy().nextReturn(
                            QDateTime::currentDateTime()));
}

void PlannerPage::onNbBringBack(const QString& taskId)
{
    m_data->clearDismissal(taskId);
}

void PlannerPage::cancelPlacing()
{
    m_placingTaskId.clear();
    refreshPlacing();
}

QVector<QPair<int, int>> PlannerPage::freeRunsFor(QDate date,
                                                  int minMinutes) const
{
    // Contiguous free stretches of the planning grid, computed from the
    // day's events on demand — the isFree question asked in bulk. Derived
    // every call, never cached; a handful of events per day makes that
    // free, and stale geometry is exactly what caching would invite.
    QVector<QPair<int, int>> runs;
    int cursor = plan::kDayStartMinutes;
    for (const Event* e : m_data->eventsOn(date)) { // sorted by start
        if (e->plannedStartMinutes - cursor >= minMinutes)
            runs.append({cursor, e->plannedStartMinutes});
        cursor = qMax(cursor, e->plannedEndMinutes);
    }
    if (plan::kDayEndMinutes - cursor >= minMinutes)
        runs.append({cursor, plan::kDayEndMinutes});
    return runs;
}

void PlannerPage::refreshPlacing()
{
    const Task* task = m_placingTaskId.isEmpty()
                           ? nullptr
                           : m_data->taskById(m_placingTaskId);
    if (!task) { // not placing, or the task vanished mid-placement
        m_placingTaskId.clear();
        m_placingBanner->hide();
        m_agenda->setHighlightRuns({});
        return;
    }

    const QDate today    = QDate::currentDate();
    const QDate deadline = coverage::deadlineOf(*task, today);
    const bool  late     = deadline.isValid() && m_date > deadline;

    m_placingBanner->show();
    m_placingLabel->setText(
        tr("Placing <b>%1</b>%2.%3 Click a highlighted run — one hour to "
           "start; drag the block's edge if it needs more.")
            .arg(task->title.toHtmlEscaped(),
                 deadline.isValid()
                     ? tr(" — deadline %1")
                           .arg(deadline.toString(QStringLiteral("ddd d MMM")))
                     : QString(),
                 late ? tr(" <b>This day is past the deadline — a block "
                           "here won't count as covered.</b>")
                      : QString()));

    // Rebuild the day strip. takeAt + deleteLater — the freshly-learned
    // deleteLayoutTree lesson applies here too, but this layout holds only
    // direct widgets, so the flat loop is sufficient AND correct.
    while (QLayoutItem* item = m_placingStrip->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    const int span = deadline.isValid()
                         ? int(qMin<qint64>(today.daysTo(deadline) + 2, 13))
                         : 6;
    for (int d = 0; d <= span; ++d) {
        const QDate day  = today.addDays(d);
        const auto  runs = freeRunsFor(day, 60);
        int biggest = 0;
        for (const auto& r : runs)
            biggest = qMax(biggest, r.second - r.first);

        auto* b = new QPushButton(m_placingBanner);
        b->setObjectName("placeDay");
        b->setProperty("dayOffset", d);
        b->setCheckable(true);
        b->setChecked(day == m_date);
        b->setEnabled(biggest >= 60);
        const bool dayLate = deadline.isValid() && day > deadline;
        b->setText(tr("%1\n%2")
                       .arg(d == 0 ? tr("Today")
                                   : day.toString(QStringLiteral("ddd d")),
                            biggest >= 60
                                ? tr("%1h%2 free")
                                      .arg(biggest / 60)
                                      .arg(biggest % 60
                                               ? QStringLiteral("30")
                                               : QString())
                                : tr("full")));
        if (dayLate)
            b->setStyleSheet(QStringLiteral(
                "color:#C25B54; border-color: rgba(194,91,84,0.5);"));
        connect(b, &QPushButton::clicked, this, [this, day]() {
            m_date = day; // stay placing; just look at another day
            updateViewSwitcher();
            m_agenda->setDate(m_date);
            refresh();
        });
        m_placingStrip->addWidget(b);
    }
    m_placingStrip->addStretch(1);

    // The invitations — only meaningful on the day view; the week's
    // columns still ACCEPT placement clicks (planAt intercepts), they just
    // don't glow. Recorded as a polish fence, not an accident.
    m_agenda->setHighlightRuns(m_mode == 0 ? freeRunsFor(m_date, 60)
                                           : QVector<QPair<int, int>>{});
}
