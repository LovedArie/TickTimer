#include "UpcomingPage.h"

#include "AppData.h"
#include "TaskDetailDialog.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

UpcomingPage::UpcomingPage(AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 22);

    m_scroll = new QScrollArea(this);
    makeTouchScrollable(m_scroll); // finger-flick on touch screens
    m_scroll->setWidgetResizable(true);
    layout->addWidget(m_scroll);

    connect(m_data, &AppData::changed, this, &UpcomingPage::rebuild);
    rebuild();
}

void UpcomingPage::rebuild()
{
    // deleteLater, NOT delete — the fix for a real crash (see test_ui.cpp).
    // This rebuild runs INSIDE changed(), a direct connection — meaning the
    // widget being discarded may be the very one whose signal handler is
    // still on the call stack (the add-task input's returnPressed, a
    // TaskRow checkbox, a delete button...). A plain `delete` frees it
    // mid-signal and control unwinds into freed memory. deleteLater defers
    // destruction until the event loop is back in charge and nobody is
    // executing inside the widget — Qt's documented cure, and the same one
    // the due strip already used. One pattern, now everywhere.
    if (QWidget* old = m_scroll->takeWidget())
        old->deleteLater();
    m_scroll->setWidget(buildContent());
}

QWidget* UpcomingPage::buildContent()
{
    auto* panel = new QFrame;
    panel->setObjectName("panel");
    panel->setMaximumWidth(720);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("Upcoming"), panel);
    title->setObjectName("h2");
    auto* sub = new QLabel(
        tr("Every dated, unfinished task across all your life areas — "
           "derived live, stored nowhere."),
        panel);
    sub->setObjectName("sub");
    sub->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(sub);

    // ---- the four views (item 7): All / Urgent / Medium / Low --------------
    // Tabs, not four side-by-side lists: same cards, one lens at a time —
    // a laptop screen breathes, and the buckets below (overdue / this week
    // / later) keep working inside whichever lens is on. checkable +
    // autoExclusive QToolButtons = the nav rail's radio idiom, reused.
    auto* tabs = new QHBoxLayout;
    tabs->setSpacing(6);
    const struct { QString label; int filter; } lenses[] = {
        {tr("All"), -1},
        {tr("Urgent"), int(Task::Priority::Urgent)},
        {tr("Medium"), int(Task::Priority::Medium)},
        {tr("Low"),    int(Task::Priority::Low)},
    };
    for (const auto& lens : lenses) {
        auto* b = new QToolButton(panel);
        b->setObjectName("nav");
        b->setText(lens.label);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setChecked(m_filter == lens.filter);
        b->setCursor(Qt::PointingHandCursor);
        const int f = lens.filter;
        connect(b, &QToolButton::clicked, this, [this, f]() {
            m_filter = f;
            rebuild(); // the data didn't change — only the lens did
        });
        tabs->addWidget(b);
    }
    tabs->addStretch(1);
    layout->addSpacing(6);
    layout->addLayout(tabs);

    const QDate today = QDate::currentDate();
    auto tasks = m_data->upcomingTasks();
    if (m_filter >= 0)
        tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                        [this](const Task* t) {
                            return int(t->priority) != m_filter;
                        }),
                    tasks.end());

    if (tasks.isEmpty()) {
        auto* empty = new QLabel(
            m_filter >= 0
                ? tr("Nothing at this urgency. Set a task's priority from "
                     "its detail panel (click its title).")
                : tr("Nothing on the horizon. Give tasks a due date in the "
                     "Activities tab and they'll line up here."),
            panel);
        empty->setObjectName("encourage");
        empty->setWordWrap(true);
        layout->addSpacing(6);
        layout->addWidget(empty);
        layout->addStretch(1);
        return wrapLeft(panel);
    }

    // One pass, three buckets. The thresholds are a VIEW decision (what
    // deserves the "this week" alarm level), so they live here — the
    // domain only knows how to order tasks, not how to dramatise them.
    const auto sectionCaption = [&](const QString& text, const QColor& color) {
        auto* caption = new QLabel(text.toUpper(), panel);
        caption->setStyleSheet(
            QStringLiteral("color:%1; font-size:10px; font-weight:700; "
                           "letter-spacing:1px;").arg(color.name()));
        layout->addSpacing(8);
        layout->addWidget(caption);
    };

    int bucket = -1; // 0 overdue, 1 this week, 2 later — captions on entry
    for (const Task* task : tasks) {
        int b;
        if (task->dueDate < today)
            b = 0;
        else if (task->dueDate <= today.addDays(6))
            b = 1;
        else
            b = 2;

        if (b != bucket) { // tasks arrive date-sorted, so buckets are runs
            bucket = b;
            if (b == 0) sectionCaption(tr("Overdue"), theme::danger());
            if (b == 1) sectionCaption(tr("This week"), theme::focus());
            if (b == 2) sectionCaption(tr("Later"), theme::inkSoft());
        }
        layout->addWidget(buildTaskCard(task, panel));
    }

    layout->addStretch(1);
    return wrapLeft(panel);
}

QWidget* UpcomingPage::buildTaskCard(const Task* task, QWidget* parent)
{
    // The task's category is its identity here (Upcoming mixes every life
    // area), so the CATEGORY colour drives the accent and the dot — while the
    // countdown's colour carries URGENCY. Two colours, two meanings, no clash.
    const Category* category = m_data->categoryById(task->categoryId);
    const QColor accent = category ? category->color : theme::inkSoft();

    const QDate  today = QDate::currentDate();
    const qint64 in    = today.daysTo(task->dueDate);

    auto* card = new QFrame(parent);
    card->setObjectName("taskCard");
    card->setStyleSheet(QStringLiteral(
        "#taskCard { background:#FFFFFF; border:1px solid #E6E9E4; "
        "border-left:4px solid %1; border-radius:12px; }").arg(accent.name()));
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(14, 12, 12, 12);
    row->setSpacing(11);

    const QString taskId   = task->id;
    const Task    snapshot = *task; // by value — the vector may move on edit

    // Completable, so the card keeps a checkbox a birthday never needs.
    // Ticking it marks the task done, which drops it from upcomingTasks()
    // on the very next rebuild — the list re-derives, nothing to hand-remove.
    auto* check = new QCheckBox(card);
    connect(check, &QCheckBox::toggled, this, [this, taskId](bool on) {
        m_data->setTaskDone(taskId, on);
    });

    auto* dot = new QLabel(card);
    dot->setPixmap(colorDot(accent, 13));

    // Title (click to edit) over a category-name subtitle.
    auto* textCol = new QVBoxLayout;
    textCol->setSpacing(2);
    auto* title = new QPushButton(task->title, card);
    title->setFlat(true);
    title->setCursor(Qt::PointingHandCursor);
    title->setStyleSheet(
        "QPushButton { border:none; background:transparent; text-align:left; "
        "padding:0; font-size:15px; font-weight:700; color:#2B2F36; } "
        "QPushButton:hover { color:#2F7E6E; }");
    connect(title, &QPushButton::clicked, this, [this, taskId, snapshot]() {
        // Parent the dialog to the WINDOW, never to this card: saving rebuilds
        // Upcoming and deletes the card. A dialog parented to the card would be
        // destroyed as its child mid-scope, then again on the stack unwind —
        // the double-free we fixed once already. The window outlives it.
        TaskDetailDialog dialog(snapshot.title, snapshot.description,
                                snapshot.dueDate, snapshot.repeat,
                                snapshot.priority, window());
        if (dialog.exec() == QDialog::Accepted)
            m_data->updateTask(taskId, dialog.chosenTitle(),
                               dialog.chosenDescription(),
                               dialog.chosenDueDate(), dialog.chosenRepeat(),
                               dialog.chosenPriority());
    });

    QString subText = category ? category->name : tr("(no area)");
    if (task->repeat != Task::Repeat::None)
        subText += QStringLiteral("   \u27F3 %1").arg(repeatLabel(task->repeat));
    auto* sub = new QLabel(subText, card);
    sub->setStyleSheet("font-size:12px; color:#7A828C;");
    textCol->addWidget(title);
    textCol->addWidget(sub);

    // The deadline as a headline, coloured to match the section it sits under:
    // rose overdue, focus green this week, calm grey later.
    QString when;
    QColor  whenColor;
    if (in < 0) {
        const qint64 late = -in;
        when = (late == 1) ? tr("1 day overdue")
                           : tr("%1 days overdue").arg(late);
        whenColor = theme::danger();
    } else if (in == 0) {
        when = tr("due today");    whenColor = theme::focus();
    } else if (in == 1) {
        when = tr("due tomorrow"); whenColor = theme::focus();
    } else if (in <= 6) {
        when = tr("in %1 days").arg(in); whenColor = theme::focus();
    } else {
        when = tr("in %1 days").arg(in); whenColor = theme::inkSoft();
    }
    auto* countdown = new QLabel(when, card);
    countdown->setStyleSheet(QStringLiteral(
        "font-size:13px; font-weight:800; color:%1;").arg(whenColor.name()));

    // The urgency chip. Medium stays silent — it's the default, and a chip
    // on every card would make all of them invisible. Urgent shouts in the
    // danger hue; Low whispers in grey.
    QLabel* prio = nullptr;
    if (task->priority != Task::Priority::Medium) {
        prio = new QLabel(priorityLabel(task->priority).toUpper(), card);
        const QString c = task->priority == Task::Priority::Urgent
                              ? theme::danger().name()
                              : QStringLiteral("#8A93A0");
        prio->setStyleSheet(QStringLiteral(
            "font-size:10px; font-weight:800; letter-spacing:1px; "
            "color:%1; border:1px solid %1; border-radius:8px; "
            "padding:2px 7px;").arg(c));
    }

    auto* x = new QPushButton(QStringLiteral("\u00D7"), card);
    x->setObjectName("danger");
    x->setFixedSize(26, 26);
    x->setCursor(Qt::PointingHandCursor);
    connect(x, &QPushButton::clicked, this,
            [this, taskId]() { m_data->removeTask(taskId); });

    row->addWidget(check);
    row->addWidget(dot);
    row->addLayout(textCol, 1);
    if (prio)
        row->addWidget(prio);
    row->addWidget(countdown);
    row->addWidget(x);
    return card;
}
