#include "ArchivePage.h"

#include "AppData.h"
#include "Theme.h"
#include "Widgets.h"

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
    layout->setContentsMargins(26, 22, 26, 22);

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

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
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
        row->setSpacing(9);
        auto* name = new QLabel(main, panel);
        name->setStyleSheet("color:#616974;"); // archived = greyed, on purpose
        auto* meta = new QLabel(detail, panel);
        meta->setStyleSheet("color:#9AA1A9; font-size:11px;");
        row->addWidget(name, 1);
        row->addWidget(meta);
        return row;
    };

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
        del->setFixedWidth(24);
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
