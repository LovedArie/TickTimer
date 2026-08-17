#pragma once
// ---------------------------------------------------------------------------
// DueDateDialog — pick a task's due date (and, since v22, an optional time
// of day), or explicitly choose "no date".
//
// Same contract as PickActivityDialog: a dialog is a pure QUESTION. It
// collects an answer (chosenDate / chosenTime) and mutates nothing — the
// caller applies it. Note that "no due date" is a real answer here, not a
// cancel: the owner's tasks live as "DATE TBD" until a date exists, so
// clearing a date must be as easy as setting one. Three exits, three
// meanings:
//   OK           -> the calendar's selected date (+ the time, if enabled)
//   No due date  -> an invalid QDate (the TBD state, §3.11)
//   Cancel       -> keep whatever the task already had
//
// THE TIME ROW (v22). "All day" is a CHECKBOX, not a magic time value, for
// the same reason "no due date" is a button rather than a sentinel date:
// absence is a first-class answer in this domain, and the control that says
// so out loud is the one users read correctly. Ticking it disables the time
// editor — a greyed control says "this answer is off" far better than a
// number you must remember to ignore (the exact idiom TaskDetailDialog
// already uses for the date itself).
// ---------------------------------------------------------------------------

#include <QDate>
#include <QDialog>
#include <QTime>

class QCalendarWidget;
class QCheckBox;
class QTimeEdit;

class DueDateDialog : public QDialog
{
    Q_OBJECT

public:
    // Seeded from the task's current deadline. An invalid `initialTime` means
    // the task is all-day, which is also the default for a task that has no
    // date at all.
    explicit DueDateDialog(QDate initial, QTime initialTime = QTime(),
                           QWidget* parent = nullptr);

    // Valid only after exec() returned Accepted. An invalid QDate is a
    // deliberate answer: "this task has no due date".
    QDate chosenDate() const;
    // Invalid == "all day". Always invalid when the date was cleared — a
    // time with no date is not a state this app allows (see AppData).
    QTime chosenTime() const;

private:
    QCalendarWidget* m_calendar = nullptr;
    QTimeEdit*       m_time     = nullptr;
    QCheckBox*       m_allDay   = nullptr;
    bool m_cleared = false;
};
