#include "TaskDetailDialog.h"

#include "AppData.h"         // the free helpers only
#include "TaskDetailPanel.h" // runTaskDetail's preferred container (v28.6)

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

TaskDetailDialog::TaskDetailDialog(const QString& title,
                                   const QString& description, QDate dueDate,
                                   QTime dueTime, Task::Repeat repeat,
                                   Task::Priority priority,
                                   int estimateMinutes, bool chunkable,
                                   QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Task details"));
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    m_form = new TaskDetailForm(title, description, dueDate, dueTime, repeat,
                                priority, estimateMinutes, chunkable, this);
    layout->addWidget(m_form);

    // THE modal navigation policy, in one connect: record + accept. The
    // hop saves the sitting (v28.5's rule) because accept() is what makes
    // the caller apply — reject would silently discard every edit as the
    // price of navigation.
    connect(m_form, &TaskDetailForm::navigateRequested, this,
            [this](const QString& id) {
                m_navigateTo = id;
                accept();
            });

    // ---- buttons — the modality half the form doesn't carry ---------------
    layout->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* save = new QPushButton(tr("Save"), this);
    save->setObjectName("primary");
    save->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
}

void TaskDetailDialog::seedPieces(const QVector<Piece>& pieces)
{
    m_form->seedPieces(pieces);
}

void TaskDetailDialog::setBreadcrumb(const QString& parentId,
                                     const QString& parentTitle)
{
    m_form->setBreadcrumb(parentId, parentTitle);
    setWindowTitle(tr("Piece details")); // same widget, honest label
}

QString TaskDetailDialog::chosenTitle() const { return m_form->chosenTitle(); }
QString TaskDetailDialog::chosenDescription() const
{
    return m_form->chosenDescription();
}
QDate TaskDetailDialog::chosenDueDate() const
{
    return m_form->chosenDueDate();
}
QTime TaskDetailDialog::chosenDueTime() const
{
    return m_form->chosenDueTime();
}
Task::Repeat TaskDetailDialog::chosenRepeat() const
{
    return m_form->chosenRepeat();
}
Task::Priority TaskDetailDialog::chosenPriority() const
{
    return m_form->chosenPriority();
}
int TaskDetailDialog::chosenEstimateMinutes() const
{
    return m_form->chosenEstimateMinutes();
}
bool TaskDetailDialog::chosenChunkable() const
{
    return m_form->chosenChunkable();
}
QVector<TaskDetailDialog::Piece> TaskDetailDialog::chosenPieces() const
{
    return m_form->chosenPieces();
}

// ---- the free-function family ----------------------------------------------

void seedTaskDetailPieces(TaskDetailForm& form, const AppData& data,
                          const QString& taskId)
{
    const Task* task = data.taskById(taskId);
    if (!task || task->isPiece())
        return; // no section at all for a piece — one level only

    QVector<TaskDetailForm::Piece> pieces;
    for (const Task* piece : data.subtasksOf(taskId)) {
        TaskDetailForm::Piece p;
        p.id    = piece->id;
        p.title = piece->title;
        p.done  = piece->done;
        p.dueDate         = piece->dueDate;         // the row's chip
        p.estimateMinutes = piece->estimateMinutes; // (display hints)
        pieces.append(p);
    }
    form.seedPieces(pieces); // even when empty: the add-row must appear,
                             // or a task could never gain its FIRST piece
}

void seedTaskDetailPieces(TaskDetailDialog& dialog, const AppData& data,
                          const QString& taskId)
{
    seedTaskDetailPieces(dialog.form(), data, taskId);
}

void applyTaskDetailAnswers(AppData& data, const QString& taskId,
                            const TaskDetailForm& form)
{
    AppData::Batch batch(data); // N mutations, ONE changed() at the brace

    data.updateTask(taskId, form.chosenTitle(), form.chosenDescription(),
                    form.chosenDueDate(), form.chosenDueTime(),
                    form.chosenRepeat(), form.chosenPriority());
    data.setTaskSize(taskId, form.chosenEstimateMinutes(),
                     form.chosenChunkable());

    for (const TaskDetailForm::Piece& piece : form.chosenPieces()) {
        if (piece.id.isEmpty()) {
            // Born in the form. A line added and then ✕ed in the same
            // sitting was a change of mind, not a task — never create it.
            if (piece.archived)
                continue;
            const QString newId = data.addSubtask(taskId, piece.title);
            if (!newId.isEmpty() && piece.done)
                data.setTaskDone(newId, true);
        } else {
            data.setTaskDone(piece.id, piece.done);
            if (piece.archived)
                data.setTaskArchived(piece.id, true);
        }
    }
}

void applyTaskDetailAnswers(AppData& data, const QString& taskId,
                            const TaskDetailDialog& dialog)
{
    applyTaskDetailAnswers(data, taskId, dialog.form());
}

void runTaskDetailNaming(AppData& data, const QString& taskId,
                         QWidget* windowParent)
{
    // Same discovery as runTaskDetail (window() → findChild), then the
    // one extra step the naming flow needs. Falls through to the plain
    // session when no panel exists.
    if (QWidget* w = windowParent ? windowParent->window() : nullptr)
        if (auto* panel = w->findChild<TaskDetailPanel*>()) {
            panel->openTask(taskId);
            panel->focusTitleForNaming();
            return;
        }
    runTaskDetail(data, taskId, windowParent);
}

void runTaskDetail(AppData& data, QString taskId, QWidget* windowParent)
{
    // v28.6: the docked panel is the preferred container. Found by
    // findChild from the top-level window — which is exactly the handle
    // every call site already passes (the window-not-row ownership rule),
    // so adding the panel changed ZERO call sites. One panel per window by
    // construction (MainWindow builds it); other windows have none and
    // fall through to the modal loop below.
    if (QWidget* w = windowParent ? windowParent->window() : nullptr)
        if (auto* panel = w->findChild<TaskDetailPanel*>()) {
            panel->openTask(taskId);
            return; // the panel owns the session from here — no loop:
                    // navigation is swap-in-place, not close-and-reopen
        }

    // ---- the modal fallback: the v28.5 loop, verbatim ----------------------
    // Each hop re-reads the task FRESH from AppData rather than trusting a
    // snapshot across iterations.
    while (!taskId.isEmpty()) {
        const Task* task = data.taskById(taskId);
        if (!task)
            return; // navigated to a task that vanished — stop quietly
        const Task snapshot = *task; // by value: updateTask may move the vector

        TaskDetailDialog dialog(snapshot.title, snapshot.description,
                                snapshot.dueDate, snapshot.dueTime,
                                snapshot.repeat, snapshot.priority,
                                snapshot.estimateMinutes, snapshot.chunkable,
                                windowParent);

        // A piece gets the way back up. Guarded twice on purpose: isPiece()
        // says there SHOULD be a parent, taskById says there IS.
        if (snapshot.isPiece())
            if (const Task* parent = data.taskById(snapshot.parentId))
                dialog.setBreadcrumb(parent->id, parent->title);

        seedTaskDetailPieces(dialog, data, taskId); // no-op for a piece

        if (dialog.exec() != QDialog::Accepted)
            return; // Cancel means "discard AND stay" — both, always

        applyTaskDetailAnswers(data, taskId, dialog);
        taskId = dialog.navigationTarget(); // empty = a plain save = done
    }
}
