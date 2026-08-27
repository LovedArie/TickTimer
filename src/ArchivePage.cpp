#include "ArchivePage.h"

#include "AppData.h"
#include "Theme.h"
#include "Widgets.h"
#include "Touch.h" // v30.7 — the 48dp minimum

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

ArchivePage::ArchivePage(AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
{
    auto* layout = new QVBoxLayout(this);
    // Desktop breathing room is phone overflow: 26 a side is 52 of 360dp.
    const bool compact = isCompactScreen();
    layout->setContentsMargins(compact ? 8 : 26, compact ? 10 : 22,
                               compact ? 8 : 26, compact ? 10 : 22);

    m_scroll = new QScrollArea(this);
    makeTouchScrollable(m_scroll);
    m_scroll->setWidgetResizable(true);
    layout->addWidget(m_scroll);

    connect(m_data, &AppData::changed, this, &ArchivePage::rebuild);
    rebuild();
}

void ArchivePage::rebuild()
{
    // deleteLater, not delete — the restore button that triggered this
    // rebuild lives in the widget being discarded (the J-section rule).
    if (QWidget* old = m_scroll->takeWidget())
        old->deleteLater();
    m_scroll->setWidget(buildContent());
}

QWidget* ArchivePage::buildContent()
{
    auto* panel = new QFrame;
    panel->setObjectName("panel");
    panel->setMaximumWidth(720);

    const bool compact = isCompactScreen();

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(compact ? 10 : 18, compact ? 12 : 16,
                               compact ? 10 : 18, compact ? 12 : 16);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("Archive"), panel);
    title->setObjectName("h2");
    auto* sub = new QLabel(
        tr("Finished tasks and retired activities rest here — out of every "
           "list, still part of your history. Restore anything, any time."),
        panel);
    sub->setObjectName("sub");
    sub->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(sub);

    const auto caption = [&](const QString& text) {
        auto* c = new QLabel(text, panel);
        c->setStyleSheet("color:#616974; font-size:10px; font-weight:700; "
                         "letter-spacing:1px;");
        layout->addSpacing(10);
        layout->addWidget(c);
    };
    const auto quietRow = [&](const QString& main, const QString& detail) {
        auto* row = new QHBoxLayout;
        row->setSpacing(compact ? 6 : 9);
        auto* name = new QLabel(main, panel);
        name->setStyleSheet("color:#616974;"); // archived = greyed, on purpose
        auto* meta = new QLabel(detail, panel);
        meta->setStyleSheet("color:#9AA1A9; font-size:11px;");
        // An unwrapped QLabel reports its whole text as its MINIMUM width,
        // so a long activity name plus its life area plus two buttons made
        // this page wider than a phone and clipped every row ("2 activitie",
        // "GTI35"). Wrapping drops the minimum to the longest single word.
        name->setWordWrap(true);
        meta->setWordWrap(true);
        row->addWidget(name, 1);
        row->addWidget(meta);
        return row;
    };

    // ---- archived life areas (categories) -----------------------------------
    caption(tr("LIFE AREAS"));
    const auto areas = m_data->archivedCategories();
    if (areas.isEmpty()) {
        auto* none = new QLabel(tr("No archived life areas."), panel);
        none->setObjectName("sub");
        layout->addWidget(none);
    }
    for (const Category* c : areas) {
        auto* row = quietRow(c->name,
                             tr("%1 activities · %2 tasks")
                                 .arg(m_data->activityCountIn(c->id))
                                 .arg(m_data->taskCountIn(c->id)));
        const QString categoryId = c->id;
        auto* restore = new QPushButton(tr("Restore"), panel);
        restore->setCursor(Qt::PointingHandCursor);
        connect(restore, &QPushButton::clicked, this, [this, categoryId]() {
            // One flag flip and the whole world returns — nothing was
            // touched on the children, so nothing needs un-touching.
            m_data->setCategoryArchived(categoryId, false);
        });
        row->addWidget(restore);
        layout->addLayout(row);
    }

    // ---- archived tasks ----------------------------------------------------
    caption(tr("TASKS"));
    const auto tasks = m_data->archivedTasks();
    if (tasks.isEmpty()) {
        auto* none = new QLabel(tr("No archived tasks."), panel);
        none->setObjectName("sub");
        layout->addWidget(none);
    }
    for (const Task* task : tasks) {
        const Category* c = m_data->categoryById(task->categoryId);
        auto* row = quietRow(task->title, c ? c->name : QString());

        const QString taskId = task->id;
        auto* restore = new QPushButton(tr("Restore"), panel);
        restore->setCursor(Qt::PointingHandCursor);
        connect(restore, &QPushButton::clicked, this, [this, taskId]() {
            m_data->setTaskArchived(taskId, false);
        });

        // Delete forever IS offered for tasks — nothing references a Task,
        // so removal is safe. It's the archive's one irreversible button,
        // and it lives here, behind a deliberate visit, not on a daily list.
        auto* del = new QPushButton(QStringLiteral("\u00D7"), panel);
        del->setObjectName("danger");
        del->setFixedSize(touch::sizeFor(24, compact),
                          touch::sizeFor(24, compact));
        del->setCursor(Qt::PointingHandCursor);
        del->setToolTip(tr("Delete forever"));
        connect(del, &QPushButton::clicked, this, [this, taskId]() {
            m_data->removeTask(taskId);
        });

        row->addWidget(restore);
        row->addWidget(del);
        layout->addLayout(row);
    }

    // ---- archived activities -----------------------------------------------
    caption(tr("ACTIVITIES"));
    const auto activities = m_data->archivedActivities();
    if (activities.isEmpty()) {
        auto* none = new QLabel(tr("No archived activities."), panel);
        none->setObjectName("sub");
        layout->addWidget(none);
    }
    for (const Activity* a : activities) {
        const Category* c = m_data->categoryById(a->categoryId);
        auto* row = quietRow(a->name, c ? c->name : QString());

        const QString activityId = a->id;
        auto* restore = new QPushButton(tr("Restore"), panel);
        restore->setCursor(Qt::PointingHandCursor);
        connect(restore, &QPushButton::clicked, this, [this, activityId]() {
            m_data->setActivityArchived(activityId, false);
        });
        row->addWidget(restore);
        layout->addLayout(row);
        // No delete button — see the header. The domain would refuse an
        // in-use delete anyway; the page simply doesn't advertise dead ends.
    }

    layout->addStretch(1);
    return wrapLeft(panel);
}
