#pragma once
// ---------------------------------------------------------------------------
// ActivityDetailDialog — the editor an Activity never had (v31), and the
// door to its timetable.
//
// WHY IT EXISTS. Until now an activity was write-once: create it with a name
// and live with that name forever. Not "forever" as an exaggeration — the
// domain genuinely refuses to delete an activity that appears in any past
// event (AppData::removeActivity), so the workaround "delete and re-create"
// stops working the first time you actually USE the thing. A typo made on
// day one was permanent by day two. That is a data model with no undo, which
// is a bug however small the field is.
//
// WHY RENAMING IS SAFE, and worth understanding rather than assuming: every
// event stores the activity's ID, never its name (Activity.h, "reference,
// don't copy"). So a rename is one write that the whole history sees at
// once. Had the name been copied into each event — the tempting shortcut —
// this dialog could not exist without a migration pass over every block ever
// planned, and any block it missed would disagree with the others forever.
// The discipline paid for the feature.
//
// THE SHAPE, copied deliberately from TaskDetailDialog: the dialog GATHERS
// answers and mutates nothing. It is a pure question — "what should this
// activity say, and when does it happen?" — and a free helper
// (applyActivityDetailAnswers) is the one place that writes. Two reasons
// this split is worth the extra names:
//   1. the dialog can be exec()'d in a test and its answers read without an
//      AppData in sight;
//   2. every write goes through the aggregate root's guarded doors, so the
//      dialog cannot invent a rule of its own.
//
// The SCHEDULES half follows TaskDetailForm's pieces exactly: the dialog
// holds a working copy of the list, edits happen in memory, and the apply
// step reconciles it against the domain (update by id, add the new, remove
// what vanished). Nothing is written until Save, so Cancel really cancels —
// which matters far more here than for a name, because a stray schedule
// would scatter blocks across four months of calendar.
// ---------------------------------------------------------------------------

#include "Schedule.h"

#include <QDialog>
#include <QString>
#include <QVector>

class AppData;
class QLineEdit;
class QPlainTextEdit;
class QVBoxLayout;

// ---------------------------------------------------------------------------
// ScheduleEditDialog — one rule's fields. A separate modal rather than an
// inline row, and the reason is the phone: a rule has seven controls (repeat,
// first date, two times, an end date and its "no end" switch, a reminder),
// and three rules inline would be twenty-one controls stacked on a 360dp
// screen. A summary line you tap to open is what every calendar app does,
// and it also gives Cancel a meaning at both levels.
// ---------------------------------------------------------------------------
class ScheduleEditDialog : public QDialog
{
    Q_OBJECT

public:
    // Takes a Schedule by value and hands back an edited copy. It never
    // sees AppData — the same pure-question contract as its parent.
    explicit ScheduleEditDialog(const Schedule& seed,
                                QWidget* parent = nullptr);

    // Valid only after exec() returned Accepted. Carries the seed's id and
    // skipDates through untouched: editing the time of a rule is not an
    // instruction to un-delete the week you skipped.
    Schedule chosen() const;

    // "Is what the user has typed a rule at all?" — asked on Save, answered
    // by the SAME pure function the domain uses (recur::problemWith), so the
    // dialog cannot be kinder than the aggregate root. Empty means fine.
    QString problem() const;

private:
    Schedule m_seed;
    class QComboBox*  m_repeat    = nullptr;
    // One checkbox per weekday, Mon..Sun in index order so index+1 IS the
    // Qt day number — the same "the position is the value" trick the repeat
    // combo uses, and for the same reason: no lookup table to get backwards.
    QList<class QCheckBox*> m_days;
    class QWidget*    m_daysRow   = nullptr; // hidden unless REPEATS = Weekly
    class QDateEdit*  m_startDate = nullptr;
    class QTimeEdit*  m_startTime = nullptr;
    class QTimeEdit*  m_endTime   = nullptr;
    class QCheckBox*  m_noEnd     = nullptr;
    class QDateEdit*  m_endDate   = nullptr;
    class QComboBox*  m_reminder  = nullptr;
};

// ---------------------------------------------------------------------------
// What a brand-new rule starts as, before the user has touched a field.
//
// Extracted from the dialog rather than left inline because it is the answer
// to one question a test should be able to ask: "can a rule the user has
// just added be saved at all?" The first version could not. It filled in
// dates and times and left `activityId` empty, on the reasoning that the
// apply step stamps the link on anyway — true, but the SAVE button is
// pressed before the apply step exists, and recur::problemWith quite
// correctly refuses a rule that is neither linked nor titled. Every field
// filled in, and still "Give it something to be called."
//
// So the link is seeded here. A Schedule's identity is half `activityId`
// (Schedule.h), and a draft that is invalid by construction is a draft the
// dialog cannot honestly validate.
//
// Takes `today` rather than reading the clock, per the seam TrackerService
// opened: a door that decides what "now" means takes it as a parameter.
// ---------------------------------------------------------------------------
Schedule newScheduleSeed(const QString& activityId, QDate today);

class ActivityDetailDialog : public QDialog
{
    Q_OBJECT

public:
    // The activity's id comes first because it is the SUBJECT: every rule
    // this dialog hands back belongs to it, and a new rule needs the link
    // from its first frame (see newScheduleSeed above).
    ActivityDetailDialog(const QString& activityId, const QString& name,
                         const QString& description,
                         QWidget* parent = nullptr);

    // Seeds the WHEN section. Separate from the constructor, like
    // TaskDetailDialog::seedPieces, so the dialog's simplest use (just a
    // name and notes) needs no schedule vocabulary at all.
    void seedSchedules(const QVector<Schedule>& schedules);

    // Valid only after exec() returned Accepted.
    QString          chosenName() const;
    QString          chosenDescription() const;
    // The working list. Entries with an empty id are NEW; entries missing
    // relative to the seed were removed. The apply helper reads it that way.
    QVector<Schedule> chosenSchedules() const { return m_schedules; }

private:
    void rebuildScheduleRows();
    void editScheduleAt(int index); // -1 = add a new one

    QString         m_activityId;
    QLineEdit*      m_name  = nullptr;
    QPlainTextEdit* m_notes = nullptr;
    QVBoxLayout*    m_scheduleRows = nullptr;
    QVector<Schedule> m_schedules;
};

// ---------------------------------------------------------------------------
// seed READS, apply WRITES, run ORCHESTRATES — the same three-part family
// TaskDetailDialog.h introduces, and for the same reason: the middle one is
// the only function that touches the domain, so there is exactly one place
// to look when asking "what can this screen change?".
// ---------------------------------------------------------------------------

// Writes the dialog's answers through AppData's guarded doors. Silently does
// nothing for an id that has since vanished — a modal can outlive its
// subject (sync applying a remote delete underneath it), and the caller
// re-reads by id rather than holding a pointer, per AppData's lifetime rule.
//
// `today` is passed rather than read from the clock, because the schedule
// doors it calls need it to decide what counts as the future.
void applyActivityDetailAnswers(AppData& data, const QString& activityId,
                                const ActivityDetailDialog& dialog,
                                QDate today);

// The whole experience, one call: read the activity, run the modal, apply.
// The single door every call site uses, so the seed/apply pair cannot be
// half-wired by a future caller.
void runActivityDetail(AppData& data, const QString& activityId,
                       QWidget* windowParent);
