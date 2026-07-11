#include "PlannerPage.h"

#include "AgendaWidget.h"
#include "EventDialog.h"
#include "GlancePanel.h"
#include "PickActivityDialog.h"
#include "ReviewWidgets.h"
#include "TaskDetailDialog.h"
#include "WeekAgendaView.h"
#include "AppData.h"
#include "Event.h"
#include "Task.h"
#include "TrackerService.h"
#include "Widgets.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
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
    m_viewSwitcher->setToolTip(tr("Click to switch view: Day \u2192 Week \u2192 Month"));
    connect(m_viewSwitcher, &QPushButton::clicked, this,
            [this]() { setMode((m_mode + 1) % 3); });

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
    auto* agendaTitle = new QLabel(tr("Your day"), agendaPanel);
    agendaTitle->setObjectName("h2");
    auto* agendaSub = new QLabel(
        tr("6 AM to midnight · 30-minute slots · click a free slot to say "
           "what you're doing"),
        agendaPanel);
    agendaSub->setObjectName("sub");
    // Word wrap changes a QLabel's CONTRACT with the layout: without it the
    // label's minimum width is its full text width (~500px here — wider than
    // a phone), with it the label can fold and stops dictating the window's
    // minimum. On desktop nothing visibly changes; it still fits one line.
    agendaSub->setWordWrap(true);

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
    agendaLayout->addWidget(agendaSub);
    agendaLayout->addWidget(m_duePanel);
    agendaLayout->addWidget(agendaScroll, 1);

    m_glance = new GlancePanel(m_data, m_tracker, dayView);
    m_glance->setFixedWidth(320);
    // A fixed 320px sidebar on a ~400px phone leaves the agenda a sliver —
    // on compact screens the glance panel yields entirely. Nothing is lost
    // for good: every number it shows is DERIVED (never stored), and the
    // Week/Month reviews recompute the same truths on demand.
    if (isCompactScreen())
        m_glance->hide();

    dayLayout->addWidget(agendaPanel, 1);
    dayLayout->addWidget(m_glance);

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

    updateViewSwitcher();
    rebuildDueStrip();
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
        const QDate monday = m_date.addDays(1 - m_date.dayOfWeek());
        text = tr("Week of %1").arg(monday.toString("MMM d"));
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

        const Task    snapshot = *task; // copy: the vector may move on edit
        const QString taskId   = task->id;
        connect(titleBtn, &QPushButton::clicked, this,
                [this, taskId, snapshot]() {
                    TaskDetailDialog dialog(snapshot.title, snapshot.description,
                                            snapshot.dueDate, snapshot.repeat,
                                            snapshot.priority, this);
                    if (dialog.exec() == QDialog::Accepted)
                        m_data->updateTask(taskId, dialog.chosenTitle(),
                                           dialog.chosenDescription(),
                                           dialog.chosenDueDate(),
                                           dialog.chosenRepeat(),
                                           dialog.chosenPriority());
                });

        row->addWidget(dot);
        row->addWidget(titleBtn, 1);
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
