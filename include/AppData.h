#pragma once
// ---------------------------------------------------------------------------
// AppData — the single owner of ALL the application's data, and the only
// door through which that data may be changed.
//
// Design pattern: this is an *aggregate root* guarding its invariants.
// The Supplementary Specification's integrity rules —
//   - an Activity used by any Event cannot be deleted,
//   - a Category can only be deleted once it holds no Activities
//     and no Tasks (extended by the Tasks addendum),
//   - two Events on the same day cannot overlap —
// are enforced HERE, in the mutation methods, not in the UI. If the rules
// lived in the UI, every new screen (and the future Android UI!) would have
// to re-implement them, and one day one screen would forget. Put the law
// where the data lives, and no caller can break it.
//
// That is also WHY the containers are private with read-only accessors,
// while Category/Activity/Event themselves are open structs: THIS class has
// invariants to defend; they don't.
//
// WHY AppData is a QObject: for one thing only — the `changed()` signal.
// Qt's signals & slots are the framework's built-in Observer pattern.
// Every mutation ends with `emit changed()`, and whoever cares (the UI
// repaints, the JsonStore saves) subscribes with `connect(...)`. AppData
// never knows the UI exists — the dependency points strictly downward.
// ---------------------------------------------------------------------------

#include "Activity.h"
#include "Category.h"
#include "Event.h"
#include "Folder.h"
#include "SpecialDay.h"
#include "Task.h"

#include <QObject>
#include <QVector>

#include <optional>

// Crash insurance for the live timer (Supplementary Spec, Reliability):
// while a Focus/Break timer runs, WHAT is running is persisted as this
// little block. `lastSeen` is refreshed by a heartbeat every ~30 s. If the
// app dies, the next launch finds this block and converts [start..lastSeen]
// into a real Segment — at most ~30 s of the in-progress interval is lost.
struct RunningState
{
    QString     eventId;
    SegmentKind kind = SegmentKind::Focus;
    QDateTime   start;
    QDateTime   lastSeen;
};

class AppData : public QObject
{
    Q_OBJECT // enables signals/slots; processed by Qt's moc tool at build time

public:
    explicit AppData(QObject* parent = nullptr);

    // ---- read access -----------------------------------------------------
    // const& = "look, don't touch": callers can iterate but cannot mutate,
    // so no code path can bypass the rules below.
    const QVector<Category>& categories() const { return m_categories; }
    const QVector<Activity>& activities() const { return m_activities; }
    const QVector<Event>&    events()     const { return m_events; }
    const QVector<Task>&     tasks()      const { return m_tasks; }
    const QVector<Folder>&     folders()     const { return m_folders; }
    const QVector<SpecialDay>& specialDays() const { return m_specialDays; }

    // Lookups by id. They return POINTERS because "not found" must be
    // representable (nullptr). LIFETIME RULE, and it matters: the pointer is
    // valid only until the next mutation — QVector may move its elements in
    // memory when it changes. Look up, use, let go; never store one.
    const Category* categoryById(const QString& id) const;
    const Activity* activityById(const QString& id) const;
    const Event*    eventById(const QString& id) const;
    const Task*     taskById(const QString& id) const;
    const Folder*   folderById(const QString& id) const;

    QVector<const Event*> eventsOn(QDate date) const;   // sorted by start time
    int  activityCountIn(const QString& categoryId) const;
    int  taskCountIn(const QString& categoryId) const;
    // Sorted the way a todo list reads: open tasks first (dated ones by
    // urgency, "TBD" ones after), finished tasks at the bottom.
    QVector<const Task*> tasksIn(const QString& categoryId) const;
    int  categoryCountInFolder(const QString& folderId) const;

    // The Upcoming page's entire data supply: every undone, DATED task,
    // most urgent first. A query, not a table — nothing here is stored
    // (addendum §3.13; derive-don't-store §3.5).
    QVector<const Task*> upcomingTasks() const;
    // The Archive page's two lists — everything the rest of the app hides.
    QVector<const Task*>     archivedTasks() const;
    QVector<const Activity*> archivedActivities() const;
    // Every undone task whose due date IS this exact day — what the Calendar
    // shows as "due today". A query, same discipline as upcomingTasks():
    // derived on demand, never stored.
    QVector<const Task*> tasksDueOn(QDate date) const;
    // Special days sorted by their next occurrence as seen from `today`.
    QVector<const SpecialDay*> specialDaysSorted(QDate today) const;
    int  eventCountUsing(const QString& activityId) const;
    bool isFree(QDate date, int startMin, int endMin,
                const QString& ignoreEventId = QString()) const;

    // ---- event identity, resolved (block-labels addendum) ------------------
    // Every screen that paints or names a block asks THESE two questions
    // instead of re-deriving Event -> Activity -> Category itself. One
    // resolution rule, one place — before this, three widgets each walked
    // the chain and a fourth identity kind would have meant three edits.
    //
    // eventLabel / eventBody: ONE stored field (Event.title), TWO derived
    // views. For an ad-hoc block the title doubles as identity AND notes —
    // the rule is the one people naturally write by: FIRST LINE is the
    // headline, the rest is the body. Splitting at read time (derive, don't
    // store) means no new field, no format bump, and old files load
    // unchanged.
    // eventLabel: the block's primary text. Activity name, else the linked
    // Task's title, else the ad-hoc title, else "(missing)" (a dangling
    // reference made visible, never hidden).
    QString eventLabel(const Event& e) const;
    // eventBody: the block's free-text notes — the part of `title` that is
    // NOT already the headline. Activity/task blocks: the whole title
    // (their identity comes from elsewhere). Ad-hoc blocks: everything
    // after the first line.
    QString eventBody(const Event& e) const;
    // eventCategoryId: which life area the block's time belongs to.
    // Activity -> its category; Task -> its category (tasks carry one
    // directly, §3.10); ad-hoc -> "" (no life area — its time counts in
    // day totals but not in the per-category bars; a deliberate,
    // documented limitation, not an accident).
    QString eventCategoryId(const Event& e) const;

    // ---- mutations (each ends with `emit changed()`) ----------------------
    // They return the new id, or bool for success — and REFUSE illegal
    // operations instead of trusting the caller. The UI's job is merely to
    // make illegal operations hard to reach (e.g. hide the delete button);
    // the domain's job is to make them impossible.
    QString addCategory(const QString& name, const QColor& color);
    bool    renameCategory(const QString& id, const QString& name);
    bool    recolorCategory(const QString& id, const QColor& color);
    bool    removeCategory(const QString& id);          // fails if it has activities

    QString addActivity(const QString& name, const QString& categoryId);
    bool    removeActivity(const QString& id);          // fails if any Event uses it
    // Archive = hide, never forget. The complement of removeActivity's
    // refusal: an in-use activity CAN'T be deleted, so this is its only
    // retirement path — out of the pickers, still owning its history.
    bool    setActivityArchived(const QString& id, bool archived);

    // Three creation doors, one per block identity (block-labels addendum).
    // Three NAMED functions instead of one addEvent(activityId, taskId,
    // title): a caller physically cannot pass a nonsense combination
    // ("both a task and an activity"), because no signature accepts one —
    // illegal calls are unrepresentable at the call site, not caught later.
    // All three run through the same isFree gate; all fail -> "".
    QString addEvent(QDate date, int startMin, int endMin,
                     const QString& activityId,          // fails on overlap -> ""
                     const QString& title = QString());  // optional block label
    QString addTaskEvent(QDate date, int startMin, int endMin,
                         const QString& taskId);         // a work block on a Task
    QString addAdHocEvent(QDate date, int startMin, int endMin,
                          const QString& title);         // spontaneous; title required
    bool    moveEvent(const QString& id, int newStartMin); // keeps duration
    // Change an event's planned SPAN (start and/or end). Routes through the
    // same isFree guard as creation — in-bounds, and non-overlapping with
    // OTHER events (this one excluded by id) — plus a one-slot minimum. It
    // REFUSES an illegal span (returns false) rather than clamping: the
    // widget clamps for a friendly live preview, the domain is the single
    // door that makes an illegal span impossible to store, whoever asks.
    bool    resizeEvent(const QString& id, int newStartMin, int newEndMin);
    bool    setEventNote(const QString& id, const QString& note);
    // Change the label painted on the block. Trimmed. REFUSES emptying the
    // title of an ad-hoc event — that would strip the block's last identity
    // and leave a nameless plan the invariant forbids.
    bool    setEventTitle(const QString& id, const QString& title);
    // Link (or unlink, taskId = "") a Task to an EXISTING block — "Study
    // GTI350, working on Lab 4". A block may hold an activity AND a task;
    // the activity stays its identity (name, colour), the task says what
    // you're doing inside it. Same guards as everywhere: a link must point
    // at a real Task, and an unlink may not strip the last identity.
    bool    setEventTask(const QString& id, const QString& taskId);
    bool    removeEvent(const QString& id);
    bool    appendSegment(const QString& eventId, const Segment& segment);
    // Honest tracking works both ways: appendSegment adds a fact the timer
    // missed (you studied, forgot to press focus); removeSegment retracts a
    // fact that was never true (pressed the wrong button, left it running).
    // By index within the event — segments have no ids, and the editor
    // shows them as a positional list.
    bool    removeSegment(const QString& eventId, int index);

    QString addTask(const QString& title, const QString& categoryId,
                    QDate dueDate = QDate());   // invalid date = "TBD"
    bool    setTaskDone(const QString& id, bool done);
    bool    setTaskDueDate(const QString& id, QDate dueDate); // invalid clears
    bool    removeTask(const QString& id);
    // Coarse edit for the detail panel: retitle, re-note, re-date and set
    // recurrence in ONE call. Why coarse when we already have fine setters?
    // Granularity should match the USER'S action. The checkbox and the date
    // badge are single-field flicks, so they keep single-field setters. The
    // panel is one deliberate "edit this task" action, so it is one mutation
    // and one changed() — four setters would fire four rebuilds for what the
    // user experienced as a single edit. Refuses an empty title (a task must
    // keep a real name); an empty description is fine.
    bool    updateTask(const QString& id, const QString& title,
                       const QString& description, QDate dueDate,
                       Task::Repeat repeat,
                       Task::Priority priority = Task::Priority::Medium);
    bool    setTaskArchived(const QString& id, bool archived);
    bool    setTaskPriority(const QString& id, Task::Priority priority);

    QString addFolder(const QString& name);
    bool    renameFolder(const QString& id, const QString& name);
    bool    removeFolder(const QString& id);       // fails while it holds categories
    bool    setCategoryFolder(const QString& categoryId,
                              const QString& folderId); // "" = move to top level

    QString addSpecialDay(const QString& title, QDate date, bool repeatsYearly);
    bool    removeSpecialDay(const QString& id);
    // One coarse edit, like updateTask: the edit dialog is one deliberate
    // action, so it is one mutation and one changed(). Invalid color =
    // "back to automatic" (urgency colouring), a real answer as always.
    bool    updateSpecialDay(const QString& id, const QString& title,
                             QDate date, bool repeatsYearly,
                             const QColor& color);

    // ---- live-timer crash insurance ---------------------------------------
    const std::optional<RunningState>& running() const { return m_running; }
    void setRunning(const RunningState& state);
    void touchRunning(const QDateTime& lastSeen);       // heartbeat
    void clearRunning();

    // Called once after load(). If a RunningState survived a crash, turn it
    // into a real Segment and report what happened (message for the UI).
    QString recoverInterruptedTracking();

    // ---- used only by JsonStore when loading -------------------------------
    // Loading replaces everything wholesale; it is not a user edit, so it
    // deliberately does NOT emit changed() (the caller refreshes explicitly —
    // otherwise loading would immediately trigger a pointless save).
    void resetFrom(QVector<Category> categories,
                   QVector<Activity> activities,
                   QVector<Event> events,
                   QVector<Task> tasks,
                   QVector<Folder> folders,
                   QVector<SpecialDay> specialDays,
                   std::optional<RunningState> running);

    // replaceAll: resetFrom + changed(). resetFrom is deliberately SILENT
    // because it runs at startup, before any widget exists to listen. This
    // door exists for the one moment the rule differs: a sync PULL replaces
    // everything while the app is alive, so every screen must rebuild and
    // the autosave must write the new state — both of which changed()
    // already drives. Same data motion, different audience.
    void replaceAll(QVector<Category> categories,
                    QVector<Activity> activities,
                    QVector<Event> events,
                    QVector<Task> tasks,
                    QVector<Folder> folders,
                    QVector<SpecialDay> specialDays,
                    std::optional<RunningState> running);

    // First-run seed: the five life areas and starter activities the
    // prototype validated, so the app never greets you with an empty void.
    void seedDefaults();

signals:
    void changed(); // "something is different — repaint / save yourselves"

private:
    Event* mutableEventById(const QString& id); // private: edits stay in-house
    // The shared, guarded tail of the three addEvent doors (see .cpp).
    QString appendGuardedEvent(QDate date, int startMin, int endMin,
                               const QString& activityId,
                               const QString& taskId, const QString& title);

    QVector<Category> m_categories;
    QVector<Activity> m_activities;
    QVector<Event>    m_events;
    QVector<Task>       m_tasks;
    QVector<Folder>     m_folders;
    QVector<SpecialDay> m_specialDays;

    // std::optional (C++17): a value that may legitimately be absent.
    // Clearer and safer than a magic "empty id means nothing is running".
    std::optional<RunningState> m_running;
};
