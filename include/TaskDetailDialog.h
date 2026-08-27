#pragma once
// ---------------------------------------------------------------------------
// TaskDetailDialog — the MODAL wrapper around TaskDetailForm.
//
// v28.6 hollowed this class out: every field it used to build now lives in
// TaskDetailForm (see that header for the story), and this wrapper adds
// exactly what modality needs — a window title, Save/Cancel buttons, and
// ONE policy decision: what does a navigation request mean here? Answer:
// record the target and accept ("save-then-go", the v28.5 rule) — because
// a modal dialog cannot swap content in place; the caller's loop
// (runTaskDetail) closes this one and opens the next.
//
// Still here at all because it is the FALLBACK: runTaskDetail prefers the
// docked TaskDetailPanel when the window has one, and falls back to this
// modal loop when it doesn't (other windows, tests, tools). Same form,
// same answers, two containers.
//
// The pure-question contract is unchanged in substance and has simply
// moved down a level with the fields: the FORM gathers answers and
// mutates nothing; this wrapper decides when the answer is final
// (Accepted) and nothing else.
// ---------------------------------------------------------------------------

#include "TaskDetailForm.h" // the fields; Piece lives there now

#include <QDate>
#include <QDialog>
#include <QString>
#include <QTime>
#include <QVector>

class AppData; // for the free helpers below — the dialog itself never uses it
class TaskDetailPanel; // the docked container runTaskDetail prefers

class TaskDetailDialog : public QDialog
{
    Q_OBJECT

public:
    // The v28.3 name every call site and test knows — now an alias for the
    // form's struct, so nobody learned a new spelling in the extraction.
    using Piece = TaskDetailForm::Piece;

    // Same signature since v28.3 (sizing inserted before `parent` so a
    // forgotten seed is a compile error, not a silent 0-over-real-estimate
    // save — updateTask's v22 reasoning).
    TaskDetailDialog(const QString& title, const QString& description,
                     QDate dueDate, QTime dueTime, Task::Repeat repeat,
                     Task::Priority priority, int estimateMinutes,
                     bool chunkable, QWidget* parent = nullptr);

    // Thin forwards to the form — kept so the public face of "the detail
    // dialog" did not change shape in the refactor.
    void seedPieces(const QVector<Piece>& pieces);
    void setBreadcrumb(const QString& parentId, const QString& parentTitle);

    // Where the user asked to GO, as part of the answer (v28.5). Empty = a
    // plain save. Set by this wrapper's navigation policy: record + accept.
    QString navigationTarget() const { return m_navigateTo; }

    // Valid only after exec() returned Accepted. All forwards.
    QString        chosenTitle() const;
    QString        chosenDescription() const;
    QDate          chosenDueDate() const;
    QTime          chosenDueTime() const;
    Task::Repeat   chosenRepeat() const;
    Task::Priority chosenPriority() const;
    int            chosenEstimateMinutes() const;
    bool           chosenChunkable() const;
    QVector<Piece> chosenPieces() const;

    // The wrapped form — the free helpers (seed/apply) address the form
    // directly, so both containers share one code path.
    TaskDetailForm&       form()       { return *m_form; }
    const TaskDetailForm& form() const { return *m_form; }

private:
    TaskDetailForm* m_form = nullptr;
    QString         m_navigateTo;
};

// ---------------------------------------------------------------------------
// The free-function family. seed READS, apply WRITES, run ORCHESTRATES —
// and since v28.6 the form-taking overloads are the real ones; the
// dialog-taking overloads forward, kept for the existing tests and any
// future modal-only caller.
// ---------------------------------------------------------------------------

void seedTaskDetailPieces(TaskDetailForm& form, const AppData& data,
                          const QString& taskId);
void seedTaskDetailPieces(TaskDetailDialog& dialog, const AppData& data,
                          const QString& taskId);

void applyTaskDetailAnswers(AppData& data, const QString& taskId,
                            const TaskDetailForm& form);
void applyTaskDetailAnswers(AppData& data, const QString& taskId,
                            const TaskDetailDialog& dialog);

// The whole detail experience, one call. v28.6: prefers the docked
// TaskDetailPanel when windowParent's window has one (the TickTick-style
// swap-in-place — openTask and done); falls back to the v28.5 modal loop
// otherwise. The four call sites still don't know or care which — that
// was the point of the seam.
// Which container this device should use for a task form, asked in ONE place
// (v30.8). Returns the window's docked drawer, or nullptr when the caller
// should fall through to the modal.
//
// It returns nullptr on a compact DEVICE regardless of what the window holds:
// the drawer is 440dp capped by "the window minus the 220dp it refuses to
// cover", which on a 360dp phone floors at 200 — and a title, notes, a due
// date and time, repeat, priority, estimate and two buttons do not go in
// 200dp. The modal does, full-screen, which is the platform's own answer for
// a complex form on a small screen.
//
// PUBLIC because it is the only part of this routing a test can observe
// without blocking: both paths end in a working form, and the modal runs its
// own event loop, so a test that let runTaskDetail choose would hang rather
// than fail. Exposing the decision is what makes it checkable.
TaskDetailPanel* dockedTaskPanelFor(QWidget* windowParent);

void runTaskDetail(AppData& data, QString taskId, QWidget* windowParent);

// v28.7 — runTaskDetail plus the naming handoff: when the panel serves
// the session, the title arrives focused and fully selected (a
// just-created "New piece" dies to the first keystroke). The modal
// fallback degrades to plain runTaskDetail — exec() blocks, so there is
// no "after open" moment to focus into; acceptable, since the fallback
// only serves panel-less windows.
void runTaskDetailNaming(AppData& data, const QString& taskId,
                         QWidget* windowParent);
