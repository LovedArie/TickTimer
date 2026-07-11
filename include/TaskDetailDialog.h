#pragma once
// ---------------------------------------------------------------------------
// TaskDetailDialog — the "mini panel" that opens when you click a task:
// its title, a free-text description (notes), a due date, and a repeat
// setting, all in one place.
//
// SAME CONTRACT as DueDateDialog and PickActivityDialog, and worth stating
// out loud because it is the pattern behind every dialog in this app: a
// dialog is a pure QUESTION. It gathers an answer and mutates NOTHING. The
// caller reads the getters and decides what to do — here, it calls
// AppData::updateTask exactly once. Keeping the dialog domain-free is what
// lets AppData stay the single door to every change (its whole reason to
// exist) and lets this class be reused or tested without a live data store.
//
// Why a QPlainTextEdit for the notes and not a QLineEdit: notes are
// multi-line by nature ("bring the form / room B-204 / email prof first").
// QLineEdit is one line; QPlainTextEdit is the multi-line plain-text field.
// (QTextEdit would work too but carries rich-text machinery we do not want —
// pick the smallest widget that fits the job.)
//
// The due-date control is a QDateEdit paired with a "No due date" checkbox,
// because — as everywhere in this domain — "no date" is a real answer, not
// an absence. Ticking the box greys the calendar and makes chosenDueDate()
// return an invalid QDate: the "DATE TBD" state, §3.11.
// ---------------------------------------------------------------------------

#include "Task.h" // for Task::Repeat — a value type, safe to include here

#include <QDate>
#include <QDialog>
#include <QString>

class QLineEdit;
class QPlainTextEdit;
class QDateEdit;
class QCheckBox;
class QComboBox;

class TaskDetailDialog : public QDialog
{
    Q_OBJECT

public:
    // Seeded from the task's CURRENT values so opening the panel shows the
    // truth; the dialog never reaches back into the task after that.
    TaskDetailDialog(const QString& title, const QString& description,
                     QDate dueDate, Task::Repeat repeat,
                     Task::Priority priority = Task::Priority::Medium,
                     QWidget* parent = nullptr);

    // Valid only after exec() returned Accepted.
    QString      chosenTitle() const;
    QString      chosenDescription() const;
    QDate        chosenDueDate() const;   // invalid == "no due date" (TBD)
    Task::Repeat chosenRepeat() const;
    Task::Priority chosenPriority() const;

private:
    QLineEdit*      m_title    = nullptr;
    QPlainTextEdit* m_notes    = nullptr;
    QDateEdit*      m_date     = nullptr;
    QCheckBox*      m_noDate   = nullptr;
    QComboBox*      m_priority = nullptr;
    QComboBox*      m_repeat   = nullptr;
};
