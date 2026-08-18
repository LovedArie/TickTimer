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
#include "Mood.h"
#include "Task.h"
#include "TaskCoverage.h" // coverage:: — the needs-a-block pure brain

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

// "☑ 2/5" as a value: how many of a parent's pieces are done, out of how
// many exist. A struct and not a QPair, because `p.done`/`p.total` at the
// call site reads as the domain; `p.first`/`p.second` reads as a puzzle.
// (v28.3, roadmap §I — the query that fills it is pieceProgress below.)
struct PieceCount
{
    int done  = 0;
    int total = 0;

    bool any() const { return total > 0; } // "does a checklist exist at all?"
    // Every piece ticked. NOT the same as the parent being done — the
    // roll-up decision (§I): finishing every piece never completes the
    // parent. A reader asking `complete()` is asking about the CHECKLIST,
    // and only about the checklist.
    bool complete() const { return total > 0 && done == total; }
};

class AppData : public QObject
{
    Q_OBJECT // enables signals/slots; processed by Qt's moc tool at build time

public:
    explicit AppData(QObject* parent = nullptr);

    // ---- one user action, one repaint (v28.3) -----------------------------
    //
    // Every mutation below ends by announcing changed(), and every listener
    // does real work on it: models rebuild, the JsonStore writes the file to
    // disk. That is correct for a single flick — tick a checkbox, one save —
    // but the task detail panel now performs THREE mutations for what the
    // user experienced as one OK click (the fields, the size, the pieces).
    // Three saves and three rebuilds for one action is waste that grows: the
    // tool-use iteration (§B) will apply several verbs from one confirm card
    // and wants exactly one repaint at the end of them.
    //
    // So: a scoped guard. While one is alive, changed() is withheld and a
    // dirty flag is set instead; the last one out emits, once, if anything
    // actually happened.
    //
    //     {
    //         AppData::Batch batch(*data);   // silence begins
    //         data->updateTask(...);
    //         data->setTaskSize(...);
    //         data->addSubtask(...);
    //     }                                  // ONE changed(), here
    //
    // WHY RAII AND NOT begin()/end(): because an early `return` between them
    // — or a thrown exception, or a `continue` someone adds next year — would
    // leave the app permanently silent, and a UI that has stopped repainting
    // with no error is a genuinely horrible bug to find. A destructor cannot
    // be forgotten. This is the same instinct as std::lock_guard, and the
    // same instinct as the door-enforced invariants above: make the correct
    // thing the automatic thing.
    //
    // Non-copyable for the obvious reason — a copy would decrement the depth
    // twice and emit early. Nesting is counted, so a batched call site that
    // itself calls a batched helper behaves.
    class Batch
    {
    public:
        explicit Batch(AppData& data);
        ~Batch();
        Batch(const Batch&)            = delete;
        Batch& operator=(const Batch&) = delete;

    private:
        AppData& m_data;
    };

    // ---- read access -----------------------------------------------------
    // const& = "look, don't touch": callers can iterate but cannot mutate,
    // so no code path can bypass the rules below.
    const QVector<Category>& categories() const { return m_categories; }
    const QVector<Activity>& activities() const { return m_activities; }
    const QVector<Event>&    events()     const { return m_events; }
    const QVector<Task>&     tasks()      const { return m_tasks; }
    const QVector<Folder>&     folders()     const { return m_folders; }
    const QVector<SpecialDay>& specialDays() const { return m_specialDays; }
    const QVector<Mood>&       moods()       const { return m_moods; }

    // Lookups by id. They return POINTERS because "not found" must be
    // representable (nullptr). LIFETIME RULE, and it matters: the pointer is
    // valid only until the next mutation — QVector may move its elements in
    // memory when it changes. Look up, use, let go; never store one.
    const Category* categoryById(const QString& id) const;
    // Category by NAME, case-insensitive; empty string when nothing matches.
    // Exists for quick-add's '#school' hints (v21.1) — resolution used to live
    // in ActivitiesPage, but the moment the global capture bar became a second
    // consumer, "what does this name mean?" revealed itself as a DOMAIN
    // question, not a page's. Returns the ID (a value), not a pointer: names
    // come from user text, and handing back a dangling-prone pointer for a
    // fuzzy lookup would invite exactly the lifetime bug the rule above warns
    // about.
    QString categoryIdByName(const QString& name) const;
    const Activity* activityById(const QString& id) const;
    const Event*    eventById(const QString& id) const;
    const Task*     taskById(const QString& id) const;
    const Folder*   folderById(const QString& id) const;

    QVector<const Event*> eventsOn(QDate date) const;   // sorted by start time
    int  activityCountIn(const QString& categoryId) const;
    // POLICY (v28.3): counts EVERYTHING, pieces included — because this
    // number GUARDS. removeCategory refuses while it is non-zero, and a
    // count that overlooked pieces would let a category be deleted out
    // from under tasks that still live in it. The chip on the category
    // card shows the same number: two answers to "how many things live
    // here?" that disagree would be worse than a chip that counts pieces.
    int  taskCountIn(const QString& categoryId) const;
    // Sorted the way a todo list reads: open tasks first (dated ones by
    // urgency, "TBD" ones after), finished tasks at the bottom.
    //
    // POLICY (v28.3): PARENTS ONLY, same reasoning as upcomingTasks() —
    // this is a per-category workload list, and pieces would double-count
    // their parent. Note the deliberate asymmetry with taskCountIn above:
    // the LIST hides pieces, the GUARD counts them. Different questions.
    QVector<const Task*> tasksIn(const QString& categoryId) const;
    int  categoryCountInFolder(const QString& folderId) const;

    // The Upcoming page's entire data supply: every undone, DATED task,
    // most urgent first. A query, not a table — nothing here is stored
    // (addendum §3.13; derive-don't-store §3.5).
    //
    // POLICY (v28.3): PARENTS ONLY. This is the app's workload view, and a
    // parent already stands for all of its pieces — listing "Lab 4" and
    // then "read the spec" and "write section 1" underneath it would show
    // one obligation three times and make a heavy week look heavier than
    // it is. Pieces live inside the parent's detail panel. The ONE query
    // that disagrees is tasksDueOn below, and it disagrees on purpose —
    // see its comment.
    QVector<const Task*> upcomingTasks() const;
    // The Archive page's two lists — everything the rest of the app hides.
    QVector<const Task*>     archivedTasks() const;
    QVector<const Activity*> archivedActivities() const;
    QVector<const Category*> archivedCategories() const;
    // The one cascade rule, named once and reused by every filter:
    // hidden = own flag OR the owning category's flag.
    bool taskHidden(const Task& t) const;
    // Every undone task whose due date IS this exact day — what the Calendar
    // shows as "due today". A query, same discipline as upcomingTasks():
    // derived on demand, never stored.
    //
    // POLICY (v28.3): this one INCLUDES dated pieces. A piece with a
    // deadline of its own is a real obligation on that day — "get Marc's
    // section by Thursday" is the thing you must actually do on Thursday,
    // and hiding it because its parent is due next week would be lying to
    // the calendar. Contrast upcomingTasks() directly above, which shows
    // parents only: the two queries disagree on purpose, because one asks
    // "what is my workload?" and the other asks "what is due this day?".
    QVector<const Task*> tasksDueOn(QDate date) const;

    // ---- pieces (v28.3, roadmap §I) ---------------------------------------
    // A parent's pieces, in the order they were added — and NOT sorted,
    // which makes this the only task query in the file that isn't.
    // Deliberate: a checklist's order is meaning ("read the spec" then
    // "write section 1"), and sorting it by title or date would scramble
    // the sequence the user was thinking in when they typed it.
    // Returns empty for a non-existent id, and for a piece (pieces have no
    // pieces — see the one-level rule on addSubtask).
    QVector<const Task*> subtasksOf(const QString& parentId) const;
    // "☑ 2/5" for the card, without the caller counting anything itself.
    // Archived pieces are excluded from BOTH numbers: an archived piece is
    // out of sight, so counting it in the denominator would make a finished
    // task read as permanently incomplete.
    PieceCount pieceProgress(const QString& parentId) const;
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
    // Retire a whole life area (v8). Hides the category AND its world —
    // views must treat "my category is archived" as "I'm hidden too" —
    // but touches no child flags, so restore is exact.
    bool    setCategoryArchived(const QString& id, bool archived);
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
    bool    setEventRepeat(const QString& id, Task::Repeat repeat);

    // ---- catch-up: what happened to a block that didn't (v26.2) -----------
    // The DECISION half of the missed-block feature. Whether a block was
    // missed is derived (missed::judge); what you chose to do about it is a
    // fact, so it comes through a door here, syncs, and lands in data.json.
    //
    // resolveBlock refuses BlockOutcome::Moved on purpose. "Moved" is not a
    // state you can simply assert — it is only true if a replacement block
    // actually exists, and the only thing that can produce one is
    // rescheduleBlock below. Letting a caller set Moved by hand would allow
    // a movedToId pointing at nothing, which is exactly the class of lie the
    // aggregate root exists to prevent.
    bool    resolveBlock(const QString& id, BlockOutcome outcome);

    // The bulk form (v26.3): one decision over many blocks, ONE changed().
    // Exists because the catch-up card met reality — a 46-block backlog —
    // and looping the single door from the UI would emit 46 changed()s,
    // each a synchronous full-surface repaint. Same refusal as the single
    // door: Moved cannot be asserted, only earned. Returns how many blocks
    // actually changed (ids that don't exist are skipped, not fatal — a
    // stale id in a bulk list shouldn't sink the other 45 decisions).
    int     resolveBlocks(const QStringList& ids, BlockOutcome outcome);

    // Create the replacement block and link the old one to it, atomically
    // from any observer's point of view. Copies the source's IDENTITY
    // (activity / task / title / note) but never its segments — the time you
    // already spent belongs to the day you spent it, and carrying it forward
    // would double-count it in every report.
    //
    // Returns the new block's id, or empty if the target slot isn't free —
    // the same decline-don't-force contract as the three addEvent doors.
    QString rescheduleBlock(const QString& id, QDate newDate,
                            int startMin, int endMin);

    // The SPLIT form (reschedule::Kind::Split): one missed block becomes
    // several smaller ones. All-or-nothing — every span is validated (free
    // on its day, and not colliding with its sibling spans) BEFORE anything
    // is appended, because a half-applied split leaves the calendar in a
    // state nobody proposed. movedToId points at the FIRST piece: the
    // forward pointer is one link by design (§H of the addendum), and the
    // remaining pieces are found the same way every reverse question is —
    // by scanning, which cannot drift.
    struct BlockSpan
    {
        QDate date;
        int   startMin = 0;
        int   endMin   = 0;
    };
    QString rescheduleBlockSplit(const QString& id,
                                 const QVector<BlockSpan>& spans);

    // The INVERSE of rescheduleBlock (v29.2) — removes the replacement and
    // returns the original to unresolved, as one change.
    //
    // Why this is a door and not two calls at the call site: the nearest
    // existing sequence (removeEvent + resolveBlock(id, Unset)) is two
    // mutations, and a listener running between them sees the work twice —
    // an unresolved original AND a live replacement. Same reasoning that
    // made rescheduleBlock one door; a Batch makes both halves one changed().
    //
    // It also exists so the assistant's MoveBlock verb has an inverse, which
    // is what lets the write boundary keep its promise of no undo button
    // (assistant addendum §B.1): every verb is undoable by another verb.
    //
    // REFUSES, rather than forcing, in three cases:
    //   - the block was never moved (outcome != Moved) — nothing to undo;
    //   - the replacement has TRACKED SEGMENTS. rescheduleBlock deliberately
    //     never copies segments ("the time you already spent belongs to the
    //     day you spent it"), so the replacement's segments are time you
    //     really sat through. Deleting them to tidy a link would destroy a
    //     fact to fix a pointer. An undo is only safe while the replacement
    //     is untouched, and after that the honest answer is no.
    // A DANGLING movedToId (replacement already deleted by hand) is not a
    // refusal but a repair: the original's Moved state is a lie once its
    // target is gone, so it is cleared and true returned.
    //
    // SCOPE, stated because the gap is real and undetectable from here: this
    // is the inverse of rescheduleBlock, NOT of rescheduleBlockSplit. A split
    // sets movedToId to its FIRST piece only, and the siblings carry no back-
    // link, so undoing one would delete that piece and orphan the rest. This
    // door cannot tell the two apart — duration does not separate them, since
    // a legitimate Kind::Shorten replacement is also shorter than its
    // original. The MoveBlock verb is fenced to single-replacement kinds
    // (addendum §I) precisely so its promise holds; a future caller that can
    // reach split moves needs the movedFromId back-link first (§H.2), not a
    // guess here.
    bool undoReschedule(const QString& id);

    // v19.10: advance every repeating block whose date has passed — the
    // rule re-arms at the first rule-date >= today whose slots are FREE
    // (occupied dates are skipped, not fought; a year of collisions and
    // the rule stays on the old block to retry tomorrow). No retroactive
    // occurrences for days you weren't there: an empty plan for last
    // Tuesday is noise, not history. Returns how many blocks were
    // spawned; called at startup and at each midnight by MainWindow.
    int rollRepeats(QDate today);
    bool    removeEvent(const QString& id);
    bool    appendSegment(const QString& eventId, const Segment& segment);
    // Honest tracking works both ways: appendSegment adds a fact the timer
    // missed (you studied, forgot to press focus); removeSegment retracts a
    // fact that was never true (pressed the wrong button, left it running).
    // By index within the event — segments have no ids, and the editor
    // shows them as a positional list.
    bool    removeSegment(const QString& eventId, int index);

    QString addTask(const QString& title, const QString& categoryId,
                    QDate dueDate = QDate(),    // invalid date = "TBD"
                    QTime dueTime = QTime());   // invalid time = "all day"
    // v19.10: completing a task that REPEATS spawns its next occurrence
    // (fresh id, due date advanced, rule carried forward; the completed
    // one keeps its victory and LOSES the rule — the chain invariant that
    // doubles as the no-double-spawn guard). Un-completing does not
    // un-spawn: history is append-only here like everywhere else.
    bool    setTaskDone(const QString& id, bool done);
    // The deadline door. Both halves move together, because they ARE one
    // fact: a time with no date is meaningless, so clearing the date clears
    // the time here rather than leaving an orphan behind for some later
    // reader to trip over. The parameter is deliberately NOT defaulted —
    // a caller that forgets it gets a compile error, not a silently wiped
    // time. (Loud beats convenient when the failure mode is data loss.)
    bool    setTaskDueDate(const QString& id, QDate dueDate,
                           QTime dueTime); // invalid date clears both
    bool    removeTask(const QString& id);
    // Coarse edit for the detail panel: retitle, re-note, re-date and set
    // recurrence in ONE call. Why coarse when we already have fine setters?
    // Granularity should match the USER'S action. The checkbox and the date
    // badge are single-field flicks, so they keep single-field setters. The
    // panel is one deliberate "edit this task" action, so it is one mutation
    // and one changed() — four setters would fire four rebuilds for what the
    // user experienced as a single edit. Refuses an empty title (a task must
    // keep a real name); an empty description is fine.
    // v22: dueTime sits immediately after dueDate — beside the fact it
    // completes, not bolted onto the end after the defaulted arguments. The
    // cost is that every call site must be touched; that cost is the POINT,
    // since a trailing defaulted QTime would let a forgotten call site
    // silently erase the user's deadline time. A compile error is the
    // cheapest bug report there is.
    bool    updateTask(const QString& id, const QString& title,
                       const QString& description, QDate dueDate,
                       QTime dueTime, Task::Repeat repeat,
                       Task::Priority priority = Task::Priority::Medium);
    // v28.3: archiving a PARENT cascades to its pieces, in BOTH directions
    // of the toggle — archive takes the checklist with it, restore brings
    // it back. An archived parent whose pieces stayed visible would leave
    // orphan checklist lines pointing at a task the app is hiding; a
    // restored parent with a missing checklist would look like data loss.
    // Archiving a single PIECE alone is allowed and cascades nowhere.
    bool    setTaskArchived(const QString& id, bool archived);
    bool    setTaskPriority(const QString& id, Task::Priority priority);

    // ---- pieces & sizing (v28.3, roadmap §I / §J.1) ------------------------
    // Birth door for a piece of an existing task: "read the spec" under
    // "Lab 4". THE ONE-LEVEL RULE lives here: the parent must exist and
    // must not itself be a piece — a Task cannot type-check that (a Task
    // is a Task), so the door enforces it, the same move as the
    // time-without-a-date rule in addTask. The piece INHERITS the parent's
    // category at birth (§I: a piece of a school task is school work) and
    // may carry its own deadline — with the same orphan-clock rule as
    // every other deadline in the app. Returns the new id, or empty on
    // refusal (missing parent / parent is a piece / blank title).
    QString addSubtask(const QString& parentId, const QString& title,
                       QDate dueDate = QDate(), QTime dueTime = QTime());
    // The sizing pair (§J.1) through one door: how long you think it will
    // take, and whether it fits short gaps. One door and not two, because
    // the detail panel asks them as one question ("size this task") and
    // two setters would emit two changed()s for one user action — the
    // updateTask coarse-vs-fine reasoning, applied again. Negative
    // estimates are clamped to 0 ("unset"); there is no such thing as a
    // task that takes minus twenty minutes.
    bool    setTaskSize(const QString& id, int estimateMinutes,
                        bool chunkable);

    // ---- needs-a-block (addendum §C/§D) -----------------------------------
    // "Not today": hide the task from the review until `until`. The domain
    // takes the TIMESTAMP, not the policy — the ReturnPolicy that computes
    // it is per-device taste (prefs::), and the domain stays settings-free
    // the same way it stays Widgets-free. Each call is one deliberate
    // put-off, so each call increments dismissCount (the escalation
    // ladder's evidence). Refuses an invalid `until`: that would mean
    // "dismissed forever", which is not a state this feature has.
    bool dismissTask(const QString& id, const QDateTime& until);
    // "Bring back": clear a live dismissal. The count is NOT decremented —
    // history is append-only here like everywhere else; un-dismissing
    // doesn't un-happen the dismissals.
    bool clearDismissal(const QString& id);
    // Housekeeping in the rollRepeats mold: clear every dismissal whose
    // time has come, in one pass with one changed(). Returns how many.
    // A NICETY, not a correctness requirement — the flag rule compares
    // dismissedUntil against `now` itself, so a lapsed timestamp never
    // hides a task even if this hasn't run (addendum §C).
    int  expireDismissals(const QDateTime& now);

    // ---- mood (v28.2, format v12) -----------------------------------------
    // The one fact the app cannot derive (§G.2). Upsert by DATE: a
    // check-in answers "how is today", so re-answering replaces — there is
    // never more than one mood per day, by construction, not by cleanup.
    void recordMood(QDate day, Mood::Level level,
                    const QString& note = QString());
    const Mood* moodOn(QDate day) const;
    // Housekeeping in the expireDismissals mold, run from the same
    // midnight knock: drop moods older than `retentionDays`. Trimming is a
    // DOMAIN rule — 14 days is a promise about what the app remembers, not
    // a UI convenience — so it lives behind this door, not in a widget.
    int  trimMoods(QDate today, int retentionDays);
    // Load-time door for JsonStore only: silent on purpose, called BEFORE
    // resetFrom/replaceAll so their (silent / single-changed()) semantics
    // cover moods too. Not a general setter — the app mutates through
    // recordMood.
    void setMoodsFromLoad(QVector<Mood> moods);

    // THE derived list every surface renders (glance panel now, week view
    // in part 3) — one query so no two screens can disagree about what
    // needs a block. Sorted: pinned, overdue, urgent, rest; ties by
    // soonest due date, dateless last. The event scan is BOUNDED to
    // today-or-later (addendum §F): coverage only ever concerns the
    // future, and a year of history shouldn't slow down a glance.
    QVector<const Task*> tasksNeedingBlock(const coverage::Rule& rule,
                                           const coverage::Escalation& esc,
                                           const QDateTime& now) const;
    // Why a flagged task's EXISTING blocks didn't count — the app must
    // explain itself when it flags something the user believes is handled.
    // May scan the full history (it wants "time was set aside Monday; it
    // didn't happen"), which is fine: it runs only per flagged task, on
    // demand, never in the hot refresh path.
    coverage::Reason taskUncoveredReason(const QString& id,
                                         QDate today) const;

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
    // `notify` exists for rescheduleBlock, which performs TWO mutations
    // (create the replacement, mark the original Moved) that must look like
    // one to anyone listening. changed() is a direct connection, so a
    // listener would otherwise run synchronously between the halves and see
    // a replacement block whose original still reads as unresolved.
    QString appendGuardedEvent(QDate date, int startMin, int endMin,
                               const QString& activityId,
                               const QString& taskId, const QString& title,
                               bool notify = true);

    // The ONE place changed() is actually emitted from (v28.3). Every
    // mutation calls this instead of emitting directly; while a Batch is
    // alive it withholds and remembers, and the last Batch out emits once.
    // Routing every emit through one gate is what makes the Batch class
    // above enforceable — a direct `emit changed()` anywhere in the .cpp
    // would be a hole in the fence, so there are none.
    void notifyChanged();
    int  m_batchDepth = 0; // nested Batches are counted, not forbidden
    bool m_batchDirty = false; // did anything happen while batched?

    QVector<Category> m_categories;
    QVector<Activity> m_activities;
    QVector<Event>    m_events;
    QVector<Task>       m_tasks;
    QVector<Folder>     m_folders;
    QVector<SpecialDay> m_specialDays;
    QVector<Mood>       m_moods; // one per date, kept sorted by recordMood

    // std::optional (C++17): a value that may legitimately be absent.
    // Clearer and safer than a magic "empty id means nothing is running".
    std::optional<RunningState> m_running;
};
