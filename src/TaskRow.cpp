#include "TaskRow.h"

#include "AppData.h"
#include "DueDateDialog.h"
#include "TaskDetailDialog.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

TaskRow::TaskRow(AppData* data, const Task& task, bool showCategoryDot,
                 QWidget* parent)
    : QWidget(parent)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(9);

    const QString taskId = task.id; // captured by every lambda below
    // A by-VALUE snapshot of the fields the detail panel seeds from. The
    // `task` reference points into AppData's vector, which may move its
    // elements on the next mutation — copying the plain values now means
    // the click handler can never read a dangling reference.
    const Task snapshot = task;

    auto* check = new QCheckBox(this);
    check->setChecked(task.done); // state FIRST, connect AFTER — restoring
                                  // "done" must never fire the slot
    connect(check, &QCheckBox::toggled, this, [data, taskId](bool on) {
        data->setTaskDone(taskId, on);
    });

    if (showCategoryDot) {
        auto* dot = new QLabel(this);
        const Category* c = data->categoryById(task.categoryId);
        dot->setPixmap(colorDot(c ? c->color : QColor("#616974"), 9));
        row->addWidget(check);
        row->addWidget(dot);
    } else {
        row->addWidget(check);
    }

    // The title is now a CLICKABLE control, not a passive label: clicking it
    // opens the detail panel. The moment a label needs a click, QLabel is the
    // wrong class — a flat QPushButton brings the clicked signal, keyboard
    // focus and Enter-to-open for free; the label look is stylesheet costume.
    auto* title = new QPushButton(task.title, this);
    title->setFlat(true);
    title->setCursor(Qt::PointingHandCursor);
    title->setToolTip(tr("Click to edit details"));
    QFont f = title->font();
    f.setStrikeOut(task.done); // the todo-app idiom for "finished"
    title->setFont(f);
    title->setStyleSheet(QStringLiteral(
        "QPushButton { border:none; background:transparent; text-align:left; "
        "padding:2px 0; color:%1; } "
        "QPushButton:hover { color:#2F7E6E; }")
            .arg(task.done ? "#AEB4AC" : "#2B2F36"));
    connect(title, &QPushButton::clicked, this,
            [this, data, taskId, snapshot]() {
                // Parent the dialog to the top-level WINDOW, not to `this`
                // (the row). Saving fires changed(), which rebuilds the pages
                // and DELETES this row. If the row were the dialog's parent,
                // Qt would destroy this stack dialog as a child mid-scope, and
                // the stack unwind would destroy it AGAIN -> double free. The
                // window outlives the dialog, so ownership stays sane.
                TaskDetailDialog dialog(snapshot.title, snapshot.description,
                                        snapshot.dueDate, snapshot.repeat,
                                        snapshot.priority, window());
                if (dialog.exec() == QDialog::Accepted)
                    data->updateTask(taskId, dialog.chosenTitle(),
                                     dialog.chosenDescription(),
                                     dialog.chosenDueDate(),
                                     dialog.chosenRepeat(),
                                     dialog.chosenPriority());
            });

    // The urgency chip (v7). Medium is silent — chips must stay rare to
    // stay readable. Urgent in the danger hue, Low in quiet grey.
    QLabel* prioChip = nullptr;
    if (task.priority != Task::Priority::Medium) {
        const QString c = task.priority == Task::Priority::Urgent
                              ? QStringLiteral("#C25B54")
                              : QStringLiteral("#8A93A0");
        prioChip = new QLabel(priorityLabel(task.priority).toUpper(), this);
        prioChip->setStyleSheet(QStringLiteral(
            "font-size:10px; font-weight:800; letter-spacing:1px; "
            "color:%1; border:1px solid %1; border-radius:8px; "
            "padding:2px 7px;").arg(c));
    }

    // A recurrence chip, only when there is one to show. repeatLabel returns
    // "" for None, but we gate on the enum directly for clarity.
    QLabel* repeatChip = nullptr;
    if (task.repeat != Task::Repeat::None) {
        repeatChip = new QLabel(
            QStringLiteral("\u27F3 %1").arg(repeatLabel(task.repeat)), this);
        repeatChip->setStyleSheet(
            "background:#EEF0ED; border-radius:8px; padding:3px 8px; "
            "color:#616974; font-size:11px;");
    }

    // A quiet "has notes" cue: three-line glyph, full text on hover. It says
    // "there's more here" without spending a whole row on it.
    QLabel* noteCue = nullptr;
    if (!task.description.trimmed().isEmpty()) {
        noteCue = new QLabel(QStringLiteral("\u2261"), this);
        noteCue->setToolTip(task.description);
        noteCue->setStyleSheet("color:#8A9098; font-size:15px;");
    }

    // The due-date badge: its label IS the state — "Aug 8", rose when
    // overdue, or the honest "date TBD". Kept as a quick one-tap shortcut
    // even though the detail panel can set the date too: single-field flicks
    // deserve single-field controls.
    auto* dateBtn = new QPushButton(
        task.dueDate.isValid() ? task.dueDate.toString("MMM d")
                               : tr("date TBD"),
        this);
    dateBtn->setCursor(Qt::PointingHandCursor);
    dateBtn->setStyleSheet(
        task.isOverdue(QDate::currentDate())
            ? "background:#F7ECEA; border:none; border-radius:8px; "
              "padding:5px 10px; color:#C25B54; font-weight:600;"
            : "background:#EEF0ED; border:none; border-radius:8px; "
              "padding:5px 10px; color:#616974;");
    const QDate current = task.dueDate;
    connect(dateBtn, &QPushButton::clicked, this, [this, data, taskId, current]() {
        // Same ownership rule as the title dialog above: parent to the
        // window, never to this row (which the save's rebuild deletes).
        DueDateDialog dialog(current, window());
        if (dialog.exec() == QDialog::Accepted)
            data->setTaskDueDate(taskId, dialog.chosenDate());
    });

    // Archive appears the moment a task is DONE (item 4): "get this victory
    // off my list" — one click, reversible from the Archive page. Deleting
    // stays available too, but archive is the gentle default for finished
    // work: out of sight, never out of history.
    QPushButton* archive = nullptr;
    if (task.done) {
        archive = new QPushButton(tr("Archive"), this);
        archive->setCursor(Qt::PointingHandCursor);
        archive->setStyleSheet(
            "background:#EEF0ED; border:none; border-radius:8px; "
            "padding:5px 10px; color:#616974; font-weight:600;");
        connect(archive, &QPushButton::clicked, this,
                [data, taskId]() { data->setTaskArchived(taskId, true); });
    }

    auto* x = new QPushButton(QStringLiteral("\u00D7"), this);
    x->setObjectName("danger");
    x->setFixedWidth(24);
    x->setCursor(Qt::PointingHandCursor);
    connect(x, &QPushButton::clicked, this,
            [data, taskId]() { data->removeTask(taskId); });

    row->addWidget(title, 1);
    if (prioChip)
        row->addWidget(prioChip);
    if (repeatChip)
        row->addWidget(repeatChip);
    if (noteCue)
        row->addWidget(noteCue);
    row->addWidget(dateBtn);
    if (archive)
        row->addWidget(archive);
    row->addWidget(x);
}
