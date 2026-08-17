#pragma once
// ---------------------------------------------------------------------------
// TaskDetailForm — the task-detail FIELDS (title, notes, deadline, repeat,
// priority, size, pieces checklist, breadcrumb), extracted from
// TaskDetailDialog in v28.6 so two containers can share one form:
//
//   TaskDetailDialog  — the modal wrapper (kept as the FALLBACK for windows
//                       without a panel, and for every existing test)
//   TaskDetailPanel   — the docked side panel (v28.6, the TickTick-style
//                       experience: the app stays alive beside it)
//
// The PURE-QUESTION contract lives HERE now, one level down from where it
// was born: the form gathers answers and mutates nothing. What changed with
// the extraction is WHO decides what a click means. In v28.5 the dialog
// answered "what does clicking a piece title do?" itself (record + accept).
// A docked panel answers it differently (guarded swap-in-place). So the
// form no longer decides at all — it EMITS navigateRequested(id) and lets
// its container choose. Same fields, same answers, two policies on top.
//
// Dirty tracking (v28.6): the explicit-save panel needs to know "has the
// user changed anything since the seed?" — so the form can snapshot its own
// answers (markClean) and compare (isDirty). The dialog ignores both; its
// Save/Cancel pair never needed the question.
// ---------------------------------------------------------------------------

#include "Task.h" // Task::Repeat / Task::Priority — value types

#include <QDate>
#include <QString>
#include <QTime>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QDateEdit;
class QTimeEdit;
class QCheckBox;
class QComboBox;
class QVBoxLayout;

class TaskDetailForm : public QWidget
{
    Q_OBJECT

public:
    // One checklist line, as an ANSWER — unchanged in meaning since v28.3,
    // moved here with the form (TaskDetailDialog::Piece remains a working
    // alias, so no caller or test learned a new name).
    struct Piece
    {
        QString id;    // empty == created in this form, not yet in AppData
        QString title;
        bool    done     = false;
        bool    archived = false; // the ✕: "get this line out of my sight"
        QDate   dueDate;              // hint only — invalid = undated
        int     estimateMinutes = 0;  // hint only — 0 = unsized
    };

    TaskDetailForm(const QString& title, const QString& description,
                   QDate dueDate, QTime dueTime, Task::Repeat repeat,
                   Task::Priority priority, int estimateMinutes,
                   bool chunkable, QWidget* parent = nullptr);

    // Builds and seeds the PIECES checklist section (optional — a form
    // that never gets pieces shows no checklist; a piece's own form).
    void seedPieces(const QVector<Piece>& pieces);

    // Shown only when this form is displaying a PIECE: "‹ parent title"
    // above the fields. Clicking emits navigateRequested(parentId) — what
    // that MEANS (accept-and-hop, or guarded swap) is the container's call.
    void setBreadcrumb(const QString& parentId, const QString& parentTitle);
    bool hasBreadcrumb() const { return m_hasBreadcrumb; }

    // The answers. Valid any time; containers read them at their own
    // commit moment (dialog: Accepted; panel: the Save button / a guarded
    // navigation).
    QString        chosenTitle() const;
    QString        chosenDescription() const;
    QDate          chosenDueDate() const;   // invalid == "no due date"
    QTime          chosenDueTime() const;   // invalid == "all day"
    Task::Repeat   chosenRepeat() const;
    Task::Priority chosenPriority() const;
    int            chosenEstimateMinutes() const; // 0 == "no estimate"
    bool           chosenChunkable() const;
    QVector<Piece> chosenPieces() const;

    // ---- dirty tracking (v28.6, for the explicit-save panel) ------------
    // markClean(): "the answers as they stand are the saved truth" —
    // containers call it after seeding (and after a successful apply).
    // isDirty(): do the answers differ from that truth? Implemented as a
    // straight comparison of the ANSWER set, not a "was any signal ever
    // fired" flag — typing a title and retyping the original is not an
    // edit, and a flag would say it was.
    void markClean();
    bool isDirty() const;

    // v28.7 — the naming handoff: focus the title and select ALL of it,
    // so a just-created "New piece" is replaced by the first keystroke.
    // Select-all is exactly why this is NOT the default on open — on an
    // existing task it would put the whole title one keypress from gone.
    void selectTitleForNaming();

signals:
    // The user asked to GO somewhere (a piece's title, or the breadcrumb).
    // The form reports the wish and performs nothing — the container owns
    // the policy. Connect with Qt::QueuedConnection if handling it may
    // destroy this form (the panel rebuilds; deleting the sender inside
    // its own signal emission is the classic self-delete crash).
    void navigateRequested(const QString& taskId);

    // Any answer changed (field edits, ticks, ✕, typing in the add-row).
    // The panel's Save button listens; the dialog doesn't care.
    void edited();

private:
    QLineEdit*      m_title    = nullptr;
    QPlainTextEdit* m_notes    = nullptr;
    QDateEdit*      m_date     = nullptr;
    QCheckBox*      m_noDate   = nullptr;
    QTimeEdit*      m_time     = nullptr;
    QCheckBox*      m_allDay   = nullptr;
    QComboBox*      m_priority = nullptr;
    QComboBox*      m_repeat   = nullptr;
    QComboBox*      m_estimate  = nullptr; // v28.8 — dropdown, not spinbox
    QCheckBox*      m_chunkable = nullptr;

    struct PieceRow
    {
        Piece      piece;
        QCheckBox* box = nullptr;
        QWidget*   rowWidget = nullptr;
    };
    QVector<PieceRow> m_pieceRows;
    QVBoxLayout*      m_piecesLayout = nullptr;
    QLineEdit*        m_newPiece     = nullptr;
    bool              m_hasBreadcrumb = false;

    // The clean baseline: the full answer set at the last markClean().
    // Stored as answers (not widget states) so isDirty() is "compare two
    // values", not "replay two widget trees".
    struct Answers
    {
        QString        title, description;
        QDate          date;
        QTime          time;
        Task::Repeat   repeat   = Task::Repeat::None;
        Task::Priority priority = Task::Priority::Medium;
        int            estimate = 0;
        bool           chunkable = false;
        QVector<Piece> pieces;
    };
    Answers currentAnswers() const;
    Answers m_clean;

    void appendPieceRow(const Piece& piece);
    void commitNewPiece();
    void syncDeadlineEnabled();
};
