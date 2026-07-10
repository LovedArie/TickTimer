#pragma once
// ---------------------------------------------------------------------------
// DueDateDialog — pick a task's due date, or explicitly choose "no date".
//
// Same contract as PickActivityDialog: a dialog is a pure QUESTION. It
// collects an answer (chosenDate) and mutates nothing — the caller applies
// it. Note that "no due date" is a real answer here, not a cancel: the
// owner's tasks live as "DATE TBD" until a date exists, so clearing a date
// must be as easy as setting one. Three exits, three meanings:
//   OK           -> the calendar's selected date
//   No due date  -> an invalid QDate (the TBD state, §3.11)
//   Cancel       -> keep whatever the task already had
// ---------------------------------------------------------------------------

#include <QDate>
#include <QDialog>

class QCalendarWidget;

class DueDateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DueDateDialog(QDate initial, QWidget* parent = nullptr);

    // Valid only after exec() returned Accepted. An invalid QDate is a
    // deliberate answer: "this task has no due date".
    QDate chosenDate() const;

private:
    QCalendarWidget* m_calendar = nullptr;
    bool m_cleared = false;
};
