#pragma once
// ---------------------------------------------------------------------------
// PickActivityDialog — "What are you doing at 9:00 AM?"
//
// UC1, steps 1–2: the user has chosen a time (the clicked slot); here they
// indicate the activity and how long. Duration first (pills: 30m–2h, capped
// by the free run of slots below the clicked one), then a click on an
// activity confirms — one tap fewer than a select-then-OK dance, exactly
// like the validated prototype.
//
// Note the dialog only COLLECTS the answer (chosenActivityId/Minutes); the
// caller performs the actual AppData::addEvent. Dialogs that mutate data
// behind their caller's back are a debugging nightmare — keep them as pure
// questions.
// ---------------------------------------------------------------------------

#include <QDate>
#include <QDialog>

class AppData;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

class PickActivityDialog : public QDialog
{
    Q_OBJECT

public:
    PickActivityDialog(const AppData* data, QDate date, int slotIndex,
                       QWidget* parent = nullptr);

    // A block can now BE three things (block-labels addendum), so the dialog
    // answers with a KIND plus the matching id/text — a tiny tagged result.
    // The caller switches on the kind; it never has to guess from which
    // fields happen to be empty.
    enum class Kind { None, Activity, Task, AdHoc };

    // Valid only after exec() returned QDialog::Accepted.
    Kind    chosenKind() const { return m_kind; }
    QString chosenActivityId() const { return m_activityId; }
    QString chosenTaskId() const { return m_taskId; }
    // The text field's content, trimmed. For AdHoc it IS the block; for
    // Activity it rides along as the optional label painted on the block.
    QString enteredTitle() const;
    int     chosenStartMinutes() const;
    int     chosenEndMinutes() const;

private:
    int  maxFreeSlots() const; // contiguous free slots from the clicked one
    void buildDurationPills(QWidget* host);
    void buildChoiceList();    // activities AND open tasks, rail order
    void onItemClicked(QListWidgetItem* item);
    void onTitleReturnPressed(); // Enter in the text field -> ad-hoc block

    const AppData* m_data;
    QDate m_date;
    int   m_slotIndex;
    int   m_durationSlots = 2; // default 1h, like the prototype
    Kind    m_kind = Kind::None;
    QString m_activityId;
    QString m_taskId;

    QLineEdit*   m_titleEdit = nullptr;
    QListWidget* m_list = nullptr;
    QVector<class QPushButton*> m_pills;
};
