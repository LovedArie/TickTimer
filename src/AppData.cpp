#include "AppData.h"

#include "Ids.h"

#include <algorithm> // std::sort, std::remove_if — the STL, not hand-rolled loops
#include <QHash>     // the bounded-scan bucket in tasksNeedingBlock

AppData::AppData(QObject* parent)
    : QObject(parent)
{
}

// ---- read access -----------------------------------------------------------

// One template helper instead of three copy-pasted loops. `auto&` works for
// both const and non-const vectors; returning a pointer lets us say "not
// found" as nullptr.
template <typename Vec>
static auto* findById(Vec& vec, const QString& id)
{
    for (auto& item : vec)
        if (item.id == id)
            return &item;
    using Item = std::remove_reference_t<decltype(vec[0])>;
    return static_cast<Item*>(nullptr);
}

const Category* AppData::categoryById(const QString& id) const { return findById(m_categories, id); }
const Activity* AppData::activityById(const QString& id) const { return findById(m_activities, id); }

QString AppData::categoryIdByName(const QString& name) const
{
    // Exact name, case-insensitive — '#SCHOOL', '#school', '#School' all mean
    // the same area. No prefix/fuzzy matching on purpose: a quick-add that
    // guessed 'Sch' -> 'School' would one day guess wrong silently. Exact-or-
    // nothing keeps the fallback path (the caller's default) the honest one.
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};
    for (const Category& c : m_categories)
        if (c.name.compare(trimmed, Qt::CaseInsensitive) == 0)
            return c.id;
    return {};
}
const Event*    AppData::eventById(const QString& id)    const { return findById(m_events, id); }
const Task*     AppData::taskById(const QString& id)      const { return findById(m_tasks, id); }
const Folder*   AppData::folderById(const QString& id)    const { return findById(m_folders, id); }
Event*          AppData::mutableEventById(const QString& id)   { return findById(m_events, id); }

// ---- the changed() gate (v28.3) -------------------------------------------
// Every mutation in this file ends in notifyChanged(), never in a direct
// emit. Outside a Batch this IS `emit changed()`; inside one it only sets a
// flag, and the outermost Batch's destructor emits once if the flag is up.
// The subtle part is the flag RESET: it happens when the emit happens, so a
// batch during which nothing actually changed (all idempotent early-returns)
// emits nothing at all — the same "no changed() storm" promise the
// individual setters already make, kept at the group level.
void AppData::notifyChanged()
{
    if (m_batchDepth > 0) {
        m_batchDirty = true;
        return;
    }
    emit changed();
}

AppData::Batch::Batch(AppData& data) : m_data(data)
{
    ++m_data.m_batchDepth;
}

AppData::Batch::~Batch()
{
    // The LAST one out emits — nested batches just decrement. Emitting from
    // a destructor is safe here for the same reason the direct connection
    // is: listeners run synchronously, see fully-applied state (every
    // mutation completed before the batch closed), and the depth is already
    // back to zero, so a listener that mutates in response notifies normally.
    if (--m_data.m_batchDepth == 0 && m_data.m_batchDirty) {
        m_data.m_batchDirty = false;
        emit m_data.changed();
    }
}

QVector<const Event*> AppData::eventsOn(QDate date) const
{
    QVector<const Event*> result;
    for (const Event& e : m_events)
        if (e.date == date)
            result.append(&e);

    // Sorted by start time so the UI can just draw them in order.
    std::sort(result.begin(), result.end(),
              [](const Event* a, const Event* b) {
                  return a->plannedStartMinutes < b->plannedStartMinutes;
              });
    return result;
}

int AppData::activityCountIn(const QString& categoryId) const
{
    int n = 0;
    for (const Activity& a : m_activities)
        if (a.categoryId == categoryId)
            ++n;
    return n;
}

int AppData::taskCountIn(const QString& categoryId) const
{
    int n = 0;
    for (const Task& task : m_tasks)
        if (task.categoryId == categoryId)
            ++n;
    return n;
}

QVector<const Task*> AppData::tasksIn(const QString& categoryId) const
{
    QVector<const Task*> result;
    for (const Task& task : m_tasks)
        if (task.categoryId == categoryId && !task.isPiece()) // parents only
            result.append(&task);

    std::sort(result.begin(), result.end(),
              [](const Task* a, const Task* b) {
                  if (a->done != b->done)
                      return !a->done;              // open before finished
                  const bool aDated = a->dueDate.isValid();
                  const bool bDated = b->dueDate.isValid();
                  if (aDated != bDated)
                      return aDated;                // dated before "TBD"
                  if (aDated && a->dueDate != b->dueDate)
                      return a->dueDate < b->dueDate; // most urgent first
                  return a->title.localeAwareCompare(b->title) < 0;
              });
    return result;
}

int AppData::categoryCountInFolder(const QString& folderId) const
{
    int n = 0;
    for (const Category& c : m_categories)
        if (c.folderId == folderId)
            ++n;
    return n;
}

QVector<const Task*> AppData::upcomingTasks() const
{
    QVector<const Task*> result;
    for (const Task& task : m_tasks)
        if (!task.done && !taskHidden(task) && task.dueDate.isValid()
            && !task.isPiece()) // parents only — the header's v28.3 policy
            result.append(&task);

    std::sort(result.begin(), result.end(),
              [](const Task* a, const Task* b) {
                  if (a->dueDate != b->dueDate)
                      return a->dueDate < b->dueDate; // most urgent first
                  // Same day: the one with a CLOCK on it comes first, earliest
                  // first. An all-day task sorts after every timed one on its
                  // day — "due sometime today" really is the loosest deadline
                  // of that day, so the order tells the truth.
                  if (a->dueTime != b->dueTime) {
                      if (!a->dueTime.isValid()) return false;
                      if (!b->dueTime.isValid()) return true;
                      return a->dueTime < b->dueTime;
                  }
                  return a->title.localeAwareCompare(b->title) < 0;
              });
    return result;
}

QVector<const Task*> AppData::tasksDueOn(QDate date) const
{
    // Exactly this day, still open. Overdue tasks are NOT included: their
    // day has passed, so they belong to Upcoming's overdue list, not to
    // this calendar day. Sorted by title for a stable order (all share the
    // one date — but v22 gave that date a clock, so there IS something else
    // to sort by now: timed tasks first, in clock order, all-day ones after.
    // The strip then reads top-to-bottom as the day actually unfolds.
    QVector<const Task*> result;
    for (const Task& task : m_tasks)
        if (!task.done && !taskHidden(task) && task.dueDate == date)
            result.append(&task);

    std::sort(result.begin(), result.end(), [](const Task* a, const Task* b) {
        if (a->dueTime != b->dueTime) {
            if (!a->dueTime.isValid()) return false; // all-day sinks
            if (!b->dueTime.isValid()) return true;
            return a->dueTime < b->dueTime;
        }
        return a->title.localeAwareCompare(b->title) < 0;
    });
    return result;
}

QVector<const SpecialDay*> AppData::specialDaysSorted(QDate today) const
{
    QVector<const SpecialDay*> result;
    for (const SpecialDay& day : m_specialDays)
        result.append(&day);

    std::sort(result.begin(), result.end(),
              [today](const SpecialDay* a, const SpecialDay* b) {
                  const QDate na = a->nextOccurrence(today);
                  const QDate nb = b->nextOccurrence(today);
                  if (na != nb)
                      return na < nb;
                  return a->title.localeAwareCompare(b->title) < 0;
              });
    return result;
}

int AppData::eventCountUsing(const QString& activityId) const
{
    int n = 0;
    for (const Event& e : m_events)
        if (e.activityId == activityId)
            ++n;
    return n;
}

bool AppData::isFree(QDate date, int startMin, int endMin,
                     const QString& ignoreEventId) const
{
    if (startMin < plan::kDayStartMinutes || endMin > plan::kDayEndMinutes
        || startMin >= endMin)
        return false;

    for (const Event& e : m_events) {
        if (e.date != date || e.id == ignoreEventId)
            continue;
        if (e.overlaps(startMin, endMin))
            return false;
    }
    return true;
}

// ---- mutations --------------------------------------------------------------

QString AppData::addCategory(const QString& name, const QColor& color)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};

    Category c;
    c.id    = ids::newId();
    c.name  = trimmed;
    c.color = color.isValid() ? color : QColor("#4C6FE0");
    m_categories.append(c);

    notifyChanged();
    return c.id;
}

bool AppData::renameCategory(const QString& id, const QString& name)
{
    Category* c = findById(m_categories, id);
    if (!c || name.trimmed().isEmpty())
        return false;
    c->name = name.trimmed();
    notifyChanged();
    return true;
}

bool AppData::recolorCategory(const QString& id, const QColor& color)
{
    Category* c = findById(m_categories, id);
    if (!c || !color.isValid())
        return false;
    c->color = color;
    notifyChanged();
    return true;
}

bool AppData::removeCategory(const QString& id)
{
    // THE INTEGRITY RULE (Supplementary Spec, extended by the Tasks
    // addendum): a category may only be deleted once it contains no
    // activities AND no tasks. Enforced here — the UI merely hides the
    // button, but even a buggy caller cannot break the data.
    if (activityCountIn(id) > 0 || taskCountIn(id) > 0)
        return false;

    const int before = m_categories.size();
    m_categories.erase(
        std::remove_if(m_categories.begin(), m_categories.end(),
                       [&](const Category& c) { return c.id == id; }),
        m_categories.end());

    if (m_categories.size() == before)
        return false;
    notifyChanged();
    return true;
}

QString AppData::addActivity(const QString& name, const QString& categoryId)
{
    // Referential integrity on CREATE too: an activity must point at a
    // category that actually exists, or the reference is born broken.
    if (name.trimmed().isEmpty() || !categoryById(categoryId))
        return {};

    Activity a;
    a.id         = ids::newId();
    a.name       = name.trimmed();
    a.categoryId = categoryId;
    m_activities.append(a);

    notifyChanged();
    return a.id;
}

bool AppData::removeActivity(const QString& id)
{
    // Twin integrity rule: an activity used by any planned block stays.
    if (eventCountUsing(id) > 0)
        return false;

    const int before = m_activities.size();
    m_activities.erase(
        std::remove_if(m_activities.begin(), m_activities.end(),
                       [&](const Activity& a) { return a.id == id; }),
        m_activities.end());

    if (m_activities.size() == before)
        return false;
    notifyChanged();
    return true;
}

// The shared tail of all three creation doors: the time-range rules live
// HERE, once. The doors above it differ only in which identity they verify —
// so a fourth identity kind someday is one new door, zero touched rules.
QString AppData::appendGuardedEvent(QDate date, int startMin, int endMin,
                                    const QString& activityId,
                                    const QString& taskId,
                                    const QString& title,
                                    bool notify)
{
    // UC1 extension 3a: "the chosen time is already occupied → System
    // declines and indicates the conflict." The decline happens here.
    if (!date.isValid() || !isFree(date, startMin, endMin))
        return {};

    Event e;
    e.id                  = ids::newId();
    e.date                = date;
    e.plannedStartMinutes = startMin;
    e.plannedEndMinutes   = endMin;
    e.activityId          = activityId;
    e.taskId              = taskId;
    e.title               = title;
    m_events.append(e);

    if (notify)
        notifyChanged();
    return e.id;
}

// ---- catch-up (v26.2) -----------------------------------------------------

bool AppData::resolveBlock(const QString& id, BlockOutcome outcome)
{
    // Moved is earned, not asserted — see the header. Without this guard a
    // caller could leave outcome == Moved with an empty movedToId, and every
    // reader downstream would have to defend against a chain link that goes
    // nowhere.
    if (outcome == BlockOutcome::Moved)
        return false;

    Event* e = mutableEventById(id);
    if (!e)
        return false;

    e->outcome = outcome;
    // Clearing the link is part of the same decision: re-deciding a block
    // that HAD been moved (say, back to Dropped) must not leave a dangling
    // pointer to the replacement.
    e->movedToId.clear();

    notifyChanged();
    return true;
}

int AppData::resolveBlocks(const QStringList& ids, BlockOutcome outcome)
{
    if (outcome == BlockOutcome::Moved)
        return 0; // the same invariant as the single door, refused once

    int changedCount = 0;
    for (const QString& id : ids) {
        Event* e = mutableEventById(id);
        if (!e || e->outcome == outcome)
            continue; // unknown or already there: skip, don't sink the batch
        e->outcome = outcome;
        e->movedToId.clear();
        ++changedCount;
    }

    // ONE emission for the whole batch — the entire reason this door exists.
    // Zero mutations -> zero emissions: changed() means changed.
    if (changedCount > 0)
        notifyChanged();
    return changedCount;
}

QString AppData::rescheduleBlock(const QString& id, QDate newDate,
                                 int startMin, int endMin)
{
    const Event* src = eventById(id);
    if (!src)
        return {};

    // Copy the identity into LOCALS before touching m_events.
    //
    // This is not defensive style, it is a correctness requirement:
    // appendGuardedEvent calls QVector::append, which may REALLOCATE the
    // vector's buffer and invalidate every Event* into it — including `src`.
    // Reading src->taskId after the append is a use-after-free that will
    // usually appear to work, which is the worst kind. Take copies, append,
    // then look the pointers up again.
    const QString activityId = src->activityId;
    const QString taskId     = src->taskId;
    const QString title      = src->title;
    const QString note       = src->note;

    // Quiet: the two halves of this operation must look atomic to listeners.
    const QString newId = appendGuardedEvent(newDate, startMin, endMin,
                                             activityId, taskId, title,
                                             /*notify=*/false);
    if (newId.isEmpty())
        return {}; // slot wasn't free — decline, don't force

    if (Event* fresh = mutableEventById(newId))
        fresh->note = note;

    // Re-lookup, for the reallocation reason above.
    if (Event* old = mutableEventById(id)) {
        old->outcome   = BlockOutcome::Moved;
        old->movedToId = newId;
    }

    notifyChanged(); // exactly one, for both halves
    return newId;
}

QString AppData::rescheduleBlockSplit(const QString& id,
                                      const QVector<BlockSpan>& spans)
{
    const Event* src = eventById(id);
    if (!src || spans.isEmpty())
        return {};

    // ---- validate EVERYTHING before touching anything ----------------------
    // Two checks per span: free against the calendar, and free against its
    // SIBLINGS. isFree can't see the siblings (they don't exist yet), so the
    // pairwise check is done here — miss it and two proposed pieces on the
    // same afternoon would pass isFree individually and collide on append.
    for (int i = 0; i < spans.size(); ++i) {
        const BlockSpan& a = spans.at(i);
        if (!a.date.isValid() || !isFree(a.date, a.startMin, a.endMin))
            return {};
        for (int j = i + 1; j < spans.size(); ++j) {
            const BlockSpan& b = spans.at(j);
            if (a.date == b.date
                && !(a.endMin <= b.startMin || b.endMin <= a.startMin))
                return {};
        }
    }

    // Same reallocation discipline as the single-span door: copies first.
    const QString activityId = src->activityId;
    const QString taskId     = src->taskId;
    const QString title      = src->title;
    const QString note       = src->note;

    QString firstId;
    for (const BlockSpan& span : spans) {
        const QString pieceId =
            appendGuardedEvent(span.date, span.startMin, span.endMin,
                               activityId, taskId, title, /*notify=*/false);
        // Validated above, so this can only fail if the guard and the
        // validation disagree — which would be a bug worth hearing about,
        // not silently absorbing. Q_ASSERT in debug; in release the honest
        // move is still to stop (the earlier pieces stand — they were legal
        // and appending them was correct) and report failure by returning
        // empty without marking the source Moved.
        Q_ASSERT(!pieceId.isEmpty());
        if (pieceId.isEmpty())
            return {};
        if (Event* fresh = mutableEventById(pieceId))
            fresh->note = note;
        if (firstId.isEmpty())
            firstId = pieceId;
    }

    if (Event* old = mutableEventById(id)) {
        old->outcome   = BlockOutcome::Moved;
        old->movedToId = firstId;
    }

    notifyChanged(); // one, for the whole split
    return firstId;
}

QString AppData::addEvent(QDate date, int startMin, int endMin,
                          const QString& activityId, const QString& title)
{
    if (!activityById(activityId))  // identity check: must be a real Activity
        return {};
    return appendGuardedEvent(date, startMin, endMin,
                              activityId, QString(), title.trimmed());
}

QString AppData::addTaskEvent(QDate date, int startMin, int endMin,
                              const QString& taskId)
{
    if (!taskById(taskId))          // identity check: must be a real Task
        return {};
    return appendGuardedEvent(date, startMin, endMin,
                              QString(), taskId, QString());
}

QString AppData::addAdHocEvent(QDate date, int startMin, int endMin,
                               const QString& title)
{
    const QString t = title.trimmed();
    if (t.isEmpty())                // identity check: the title IS the identity
        return {};
    return appendGuardedEvent(date, startMin, endMin,
                              QString(), QString(), t);
}

bool AppData::moveEvent(const QString& id, int newStartMin)
{
    Event* e = mutableEventById(id);
    if (!e)
        return false;

    const int duration = e->plannedEndMinutes - e->plannedStartMinutes;
    if (!isFree(e->date, newStartMin, newStartMin + duration, /*ignore=*/id))
        return false;

    // UC1 extension *a: moving reschedules the PLAN only — the tracked
    // Segments travel with the Event untouched, because what really
    // happened is a fact and facts don't move.
    e->plannedStartMinutes = newStartMin;
    e->plannedEndMinutes   = newStartMin + duration;

    notifyChanged();
    return true;
}

bool AppData::resizeEvent(const QString& id, int newStartMin, int newEndMin)
{
    Event* e = mutableEventById(id);
    if (!e)
        return false;

    // A block must stay at least one slot tall — you can't shrink an event
    // into nothing. (isFree already rejects start >= end, but this is the
    // stricter, meaningful floor for a resize.)
    if (newEndMin - newStartMin < plan::kSlotMinutes)
        return false;

    // The SAME guard creation and moving use: in-bounds, ordered, and not
    // overlapping any OTHER event (this one excluded by id). One rule, one
    // door — the resize can't invent a way around it.
    if (!isFree(e->date, newStartMin, newEndMin, /*ignore=*/id))
        return false;

    // The plan changes; the tracked Segments stay put — what happened is a
    // fact, and facts don't stretch (same principle as moveEvent).
    e->plannedStartMinutes = newStartMin;
    e->plannedEndMinutes   = newEndMin;

    notifyChanged();
    return true;
}

bool AppData::setEventNote(const QString& id, const QString& note)
{
    Event* e = mutableEventById(id);
    if (!e)
        return false;
    e->note = note;
    notifyChanged();
    return true;
}

bool AppData::setEventTitle(const QString& id, const QString& title)
{
    Event* e = mutableEventById(id);
    if (!e)
        return false;

    const QString t = title.trimmed();
    // The invariant, defended at the mutation door: an ad-hoc event's title
    // is its ONLY identity, so clearing it is refused — not clamped, not
    // silently ignored, refused (same contract as resizeEvent).
    if (t.isEmpty() && e->activityId.isEmpty() && e->taskId.isEmpty())
        return false;

    e->title = t;
    notifyChanged();
    return true;
}

bool AppData::setEventTask(const QString& id, const QString& taskId)
{
    Event* e = mutableEventById(id);
    if (!e)
        return false;

    // A link must point at a real Task — never store a reference we cannot
    // resolve (the same rule addTaskEvent enforces at creation).
    if (!taskId.isEmpty() && !taskById(taskId))
        return false;

    // Unlinking is refused when the task IS the block's last identity
    // (no activity, no title) — the mirror image of setEventTitle's guard.
    // Two mutations, one invariant, each defending the field it clears.
    if (taskId.isEmpty() && e->activityId.isEmpty() && e->title.isEmpty())
        return false;

    e->taskId = taskId;
    notifyChanged();
    return true;
}

QString AppData::eventLabel(const Event& e) const
{
    if (const Activity* a = activityById(e.activityId))
        return a->name;
    if (const Task* t = taskById(e.taskId))
        return t->title;
    if (!e.title.isEmpty()) {
        // Ad-hoc block: the title is the identity — but only its FIRST
        // LINE. A multiline title would otherwise become a paragraph-sized
        // headline in every screen that asks "what is this block called?"
        // (the dialog header, the agenda's bold line 1, the week columns).
        const int nl = e.title.indexOf(QLatin1Char('\n'));
        return nl < 0 ? e.title : e.title.left(nl).trimmed();
    }
    return QStringLiteral("(missing)");
}

QString AppData::eventBody(const Event& e) const
{
    // Blocks whose identity comes from an Activity or Task keep the WHOLE
    // title as body — it never doubled as a headline there.
    if (activityById(e.activityId) || taskById(e.taskId))
        return e.title;
    // Ad-hoc: the headline took the first line; the body is the rest.
    const int nl = e.title.indexOf(QLatin1Char('\n'));
    return nl < 0 ? QString() : e.title.mid(nl + 1).trimmed();
}

QString AppData::eventCategoryId(const Event& e) const
{
    if (const Activity* a = activityById(e.activityId))
        return a->categoryId;
    if (const Task* t = taskById(e.taskId))
        return t->categoryId;
    return {};
}

bool AppData::removeEvent(const QString& id)
{
    // Composition pays off: erasing the Event destroys its QVector<Segment>
    // with it. No manual cleanup, no leak possible. That's RAII.
    const int before = m_events.size();
    m_events.erase(
        std::remove_if(m_events.begin(), m_events.end(),
                       [&](const Event& e) { return e.id == id; }),
        m_events.end());

    if (m_events.size() == before)
        return false;

    // If the deleted event was the one being tracked, the crash-insurance
    // block would point at a ghost — clear it.
    if (m_running && m_running->eventId == id)
        m_running.reset();

    notifyChanged();
    return true;
}

bool AppData::appendSegment(const QString& eventId, const Segment& segment)
{
    Event* e = mutableEventById(eventId);
    if (!e || segment.seconds() <= 0)  // a zero-length segment is noise, drop it
        return false;
    e->segments.append(segment);
    notifyChanged();
    return true;
}

bool AppData::removeSegment(const QString& eventId, int index)
{
    Event* e = mutableEventById(eventId);
    if (!e || index < 0 || index >= e->segments.size())
        return false; // out-of-range is refused, never clamped — a retraction
                      // must name exactly the fact it retracts
    e->segments.removeAt(index);
    notifyChanged();
    return true;
}

QString AppData::addTask(const QString& title, const QString& categoryId,
                         QDate dueDate, QTime dueTime)
{
    // Same birth rules as Activity: a real title, a real category —
    // references must not be born broken.
    if (title.trimmed().isEmpty() || !categoryById(categoryId))
        return {};

    Task task;
    task.id         = ids::newId();
    task.title      = title.trimmed();
    task.categoryId = categoryId;
    task.dueDate    = dueDate; // invalid is fine: that IS "date TBD"
    // A clock with no calendar is not a deadline. Refusing the orphan HERE,
    // at the one door tasks are born through, means no later reader ever has
    // to ask "what does a time without a date mean?" — the state simply
    // cannot exist. (Make illegal states unrepresentable; when the type
    // system can't, the door does it.)
    task.dueTime    = dueDate.isValid() ? dueTime : QTime();
    m_tasks.append(task);

    notifyChanged();
    return task.id;
}

bool AppData::setCategoryArchived(const QString& id, bool archived)
{
    Category* c = findById(m_categories, id);
    if (!c)
        return false;
    if (c->archived == archived)
        return true;
    c->archived = archived;
    notifyChanged();
    return true;
}

QVector<const Category*> AppData::archivedCategories() const
{
    QVector<const Category*> result;
    for (const Category& c : m_categories)
        if (c.archived)
            result.append(&c);
    return result;
}

bool AppData::taskHidden(const Task& t) const
{
    if (t.archived)
        return true;
    const Category* c = categoryById(t.categoryId);
    return c && c->archived; // an archived life area hides its whole world
}

bool AppData::setActivityArchived(const QString& id, bool archived)
{
    Activity* a = findById(m_activities, id);
    if (!a)
        return false;
    if (a->archived == archived)
        return true; // idempotent, no changed() storm
    a->archived = archived;
    notifyChanged();
    return true;
}

bool AppData::setTaskArchived(const QString& id, bool archived)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    if (task->archived == archived)
        return true;
    task->archived = archived;

    // v28.3: the cascade, BOTH directions of the toggle (header rationale).
    // Deliberately unconditional: restoring a parent restores EVERY piece,
    // even one the user had archived individually beforehand — "bring
    // Lab 4 back" means the whole checklist, and re-hiding one line is a
    // one-click correction, while a silently missing line looks like data
    // loss. One pass, one changed().
    for (Task& t : m_tasks)
        if (t.parentId == id)
            t.archived = archived;

    notifyChanged();
    return true;
}

bool AppData::setTaskPriority(const QString& id, Task::Priority priority)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    if (task->priority == priority)
        return true;
    task->priority = priority;
    notifyChanged();
    return true;
}

// ---- pieces & sizing (v28.3, roadmap §I / §J.1) ---------------------------

QString AppData::addSubtask(const QString& parentId, const QString& title,
                            QDate dueDate, QTime dueTime)
{
    const Task* parent = taskById(parentId);
    // THE ONE-LEVEL RULE, enforced where tasks are born: no parent, no
    // piece; and a piece may not have pieces. The second check is the whole
    // reason this door exists separately from addTask — nothing about the
    // Task struct forbids nesting, so the door must.
    if (!parent || parent->isPiece())
        return {};
    if (title.trimmed().isEmpty())
        return {};

    Task piece;
    piece.id         = ids::newId();
    piece.title      = title.trimmed();
    piece.parentId   = parentId;
    // Inherits the parent's life area at birth (§I): a piece of a school
    // task IS school work. And since no door can move a task's category
    // afterwards (set at birth and at repeat-spawn, nowhere else), parent
    // and pieces cannot drift apart — the invariant holds by the ABSENCE
    // of a door, which is the cheapest enforcement there is.
    piece.categoryId = parent->categoryId;
    piece.dueDate    = dueDate; // a piece may carry its own deadline (§I)
    // Same orphan-clock rule as addTask: a time without a date is not a
    // state the domain accepts, so the third birth door enforces it too.
    piece.dueTime    = dueDate.isValid() ? dueTime : QTime();
    m_tasks.append(piece); // `parent` may dangle past this line — read above

    notifyChanged();
    return piece.id;
}

bool AppData::setTaskSize(const QString& id, int estimateMinutes,
                          bool chunkable)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    const int clamped = std::max(0, estimateMinutes); // negatives read "unset"
    if (task->estimateMinutes == clamped && task->chunkable == chunkable)
        return true; // idempotent, no changed() storm
    task->estimateMinutes = clamped;
    task->chunkable       = chunkable;
    notifyChanged();
    return true;
}

QVector<const Task*> AppData::subtasksOf(const QString& parentId) const
{
    // Insertion order, deliberately unsorted — see the header. Archived
    // pieces are skipped so this list and pieceProgress() below describe
    // the same set: the dialog's checklist and its "2/5" must never
    // disagree about what exists.
    QVector<const Task*> result;
    for (const Task& task : m_tasks)
        if (task.parentId == parentId && !task.archived)
            result.append(&task);
    return result;
}

PieceCount AppData::pieceProgress(const QString& parentId) const
{
    PieceCount count;
    for (const Task& task : m_tasks) {
        if (task.parentId != parentId || task.archived)
            continue;
        ++count.total;
        if (task.done)
            ++count.done;
    }
    return count;
}

QVector<const Task*> AppData::archivedTasks() const
{
    QVector<const Task*> result;
    for (const Task& task : m_tasks)
        if (task.archived)
            result.append(&task);
    std::sort(result.begin(), result.end(),
              [](const Task* a, const Task* b) {
                  return a->title.localeAwareCompare(b->title) < 0;
              });
    return result;
}

QVector<const Activity*> AppData::archivedActivities() const
{
    QVector<const Activity*> result;
    for (const Activity& a : m_activities)
        if (a.archived)
            result.append(&a);
    std::sort(result.begin(), result.end(),
              [](const Activity* a, const Activity* b) {
                  return a->name.localeAwareCompare(b->name) < 0;
              });
    return result;
}

bool AppData::setTaskDone(const QString& id, bool done)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    if (task->done == done)
        return true; // already true — no change, so no changed() storm

    task->done = done;

    // Completion resets the needs-a-block evidence (addendum §D): the
    // dismissal count exists to say "you keep putting this off", and a
    // finished task isn't being put off. Any live dismissal is moot for
    // the same reason. Un-completing does NOT restore either — history is
    // append-only, and the un-check is a correction, not a resurrection.
    if (done) {
        task->dismissCount   = 0;
        task->dismissedUntil = QDateTime();
    }

    // The repeat hint becomes REAL here (v19.10 — it was stored and shown
    // since v7, acted on never): completing a repeating, dated task
    // spawns the next occurrence. Chain invariant: the rule moves to the
    // new task and leaves the old one — so cycling done/undone/done can
    // never double-spawn (the second completion finds no rule), and the
    // Archive doesn't fill with chips claiming finished tasks still
    // repeat.
    if (done && task->repeat != Task::Repeat::None
        && task->dueDate.isValid()) {
        Task next;
        next.id          = ids::newId();
        next.title       = task->title;
        next.categoryId  = task->categoryId;
        next.description = task->description;
        next.priority    = task->priority;
        next.dueDate     = nextOccurrence(task->dueDate, task->repeat);
        // The clock rides along with the calendar: a bill due "the 1st at
        // 09:00" repeats at 09:00, not "sometime that day". The time is part
        // of the habit, so the chain carries it forward like every other
        // field above.
        next.dueTime     = task->dueTime;
        next.repeat      = task->repeat;
        // v28.3: the chain carries the NEW facts too. A repeating piece
        // stays a piece (its next occurrence is still "get Marc's section",
        // still under the same parent — spawning it to the top level would
        // quietly promote a checklist line into a full task every cycle).
        // And the size rides along like the clock does: the same job next
        // week takes the same time, until the user says otherwise.
        next.parentId        = task->parentId;
        next.estimateMinutes = task->estimateMinutes;
        next.chunkable       = task->chunkable;
        task->repeat     = Task::Repeat::None;
        m_tasks.append(next); // task* may dangle past this line — done above
    }

    notifyChanged();
    return true;
}

bool AppData::setTaskDueDate(const QString& id, QDate dueDate, QTime dueTime)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    task->dueDate = dueDate; // invalid QDate clears back to "TBD"
    // Clearing the date clears the time with it — see the header: the two
    // halves are one fact, and a stranded time would be a lie waiting to be
    // believed the next time a date is set.
    task->dueTime = dueDate.isValid() ? dueTime : QTime();
    notifyChanged();
    return true;
}

// ---- needs-a-block doors (addendum §C/§D) ---------------------------------

bool AppData::dismissTask(const QString& id, const QDateTime& until)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    if (!until.isValid())
        return false; // "dismissed forever" is not a state — refuse, don't
                      // invent a duration the caller didn't ask for
    task->dismissedUntil = until;
    task->dismissCount  += 1; // one deliberate put-off, one count
    notifyChanged();
    return true;
}

bool AppData::clearDismissal(const QString& id)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    if (!task->dismissedUntil.isValid())
        return true; // nothing to clear — no change, no changed() storm
    task->dismissedUntil = QDateTime();
    notifyChanged();   // the count stays: history is append-only
    return true;
}

void AppData::recordMood(QDate day, Mood::Level level, const QString& note)
{
    if (!day.isValid())
        return;
    for (Mood& m : m_moods) {
        if (m.date == day) { // upsert: today already answered — replace
            m.level = level;
            m.note  = note;
            notifyChanged();
            return;
        }
    }
    Mood m;
    m.date  = day;
    m.level = level;
    m.note  = note;
    m_moods.append(m);
    notifyChanged();
}

const Mood* AppData::moodOn(QDate day) const
{
    for (const Mood& m : m_moods)
        if (m.date == day)
            return &m;
    return nullptr;
}

int AppData::trimMoods(QDate today, int retentionDays)
{
    const QDate cutoff = today.addDays(-retentionDays);
    int removed = 0;
    for (int i = m_moods.size(); i-- > 0;) {
        if (m_moods.at(i).date < cutoff) {
            m_moods.removeAt(i);
            ++removed;
        }
    }
    if (removed > 0)
        notifyChanged(); // a save must follow: forgetting is also a write
    return removed;
}

void AppData::setMoodsFromLoad(QVector<Mood> moods)
{
    m_moods = std::move(moods); // silent by contract — see the header
}

int AppData::expireDismissals(const QDateTime& now)
{
    int cleared = 0;
    for (Task& t : m_tasks) {
        if (t.dismissedUntil.isValid() && t.dismissedUntil <= now) {
            t.dismissedUntil = QDateTime();
            ++cleared;
        }
    }
    if (cleared > 0)
        notifyChanged(); // one signal for the whole pass, like rollRepeats
    return cleared;
}

QVector<const Task*> AppData::tasksNeedingBlock(
    const coverage::Rule& rule, const coverage::Escalation& esc,
    const QDateTime& now) const
{
    const QDate today = now.date();

    // The bounded scan (addendum §F): ONE pass over events, keeping only
    // task-linked blocks dated today or later, bucketed by task. Coverage
    // only ever concerns [today, deadline], so the past — which is most of
    // a long-lived file — is never walked on the hot path.
    QHash<QString, QVector<QDate>> futureBlocks;
    for (const Event& e : m_events) {
        if (e.taskId.isEmpty() || !e.date.isValid() || e.date < today)
            continue;
        futureBlocks[e.taskId].append(e.date);
    }

    QVector<const Task*> out;
    for (const Task& t : m_tasks) {
        const bool covered =
            coverage::isCovered(t, futureBlocks.value(t.id), today);
        if (coverage::needsBlock(t, covered, rule, now))
            out.append(&t);
    }

    // Pinned, overdue, urgent, rest — the one ordering every surface
    // shows (coverage::rankAt); ties by soonest due date, dateless last.
    std::sort(out.begin(), out.end(),
              [&esc, today](const Task* a, const Task* b) {
                  const int ra = coverage::rankAt(*a, esc, today);
                  const int rb = coverage::rankAt(*b, esc, today);
                  if (ra != rb)
                      return ra < rb;
                  const QDate da = a->dueDate, db = b->dueDate;
                  if (da.isValid() != db.isValid())
                      return da.isValid(); // dated before dateless
                  return da < db;
              });
    return out;
}

coverage::Reason AppData::taskUncoveredReason(const QString& id,
                                              QDate today) const
{
    const Task* task = taskById(id);
    if (!task)
        return coverage::Reason::None;

    // The full history this time — this query WANTS to see the lapsed
    // Monday block. Fine off the hot path: it runs only for the handful of
    // flagged tasks, on demand (addendum §F).
    QVector<QDate> dates;
    for (const Event& e : m_events)
        if (e.taskId == id && e.date.isValid())
            dates.append(e.date);

    return coverage::uncoveredReason(*task, dates, today);
}

bool AppData::setEventRepeat(const QString& id, Task::Repeat repeat)
{
    Event* e = mutableEventById(id);
    if (!e)
        return false;
    if (e->repeat == repeat)
        return true;
    e->repeat = repeat;
    notifyChanged();
    return true;
}

int AppData::rollRepeats(QDate today)
{
    // Collect first, mutate after: appendGuardedEvent grows m_events, and
    // growing a vector mid-iteration is the classic invalidated-iterator
    // trap. Ids are stable; pointers are not.
    QVector<QString> dueIds;
    for (const Event& e : m_events)
        if (e.repeat != Task::Repeat::None && e.date < today)
            dueIds.append(e.id);

    int spawned = 0;
    for (const QString& id : dueIds) {
        Event* old = mutableEventById(id);
        if (!old)
            continue;

        // March the rule forward to the first candidate >= today, then
        // keep marching past occupied dates (up to a year — beyond that,
        // leave the rule on the old block and let tomorrow retry rather
        // than silently killing the chain).
        QDate target = nextOccurrence(old->date, old->repeat);
        while (target.isValid() && target < today)
            target = nextOccurrence(target, old->repeat);
        int guard = 366;
        while (target.isValid() && guard-- > 0
               && !isFree(target, old->plannedStartMinutes,
                          old->plannedEndMinutes))
            target = nextOccurrence(target, old->repeat);
        if (!target.isValid() || guard <= 0)
            continue;

        // The spawn copies IDENTITY, not history: fresh id, no segments.
        // A linked TASK is demoted to text (the removeTask downgrade
        // pattern): next week's block should say what this one was about,
        // not claim a deliverable that may be done by then — and if that
        // task itself repeats, ITS next occurrence is a different id
        // anyway.
        Event next;
        next.id                  = ids::newId();
        next.date                = target;
        next.plannedStartMinutes = old->plannedStartMinutes;
        next.plannedEndMinutes   = old->plannedEndMinutes;
        next.activityId          = old->activityId;
        next.title               = old->title;
        if (next.activityId.isEmpty() && next.title.isEmpty())
            if (const Task* t = taskById(old->taskId))
                next.title = t->title;
        next.repeat = old->repeat;
        old->repeat = Task::Repeat::None; // the chain invariant, again
        m_events.append(next);            // `old` may dangle past this line
        ++spawned;
    }

    if (spawned > 0)
        notifyChanged();
    return spawned;
}

bool AppData::removeTask(const QString& id)
{
    // Contrast with removeActivity, which REFUSES while events reference it.
    // Deleting a task shouldn't be blocked by old calendar blocks — but a
    // dangling taskId would paint "(missing)" forever. So the reference is
    // DEMOTED to text: the task's title is copied into each referencing
    // event (unless the user already gave it a label) and the link cleared.
    // The plan keeps its meaning; the model keeps its integrity. This is the
    // third option between "refuse" and "cascade delete": downgrade.
    //
    // ORDER MATTERS: taskById() returns a pointer INTO m_tasks, and erase()
    // invalidates it. Copy the title to a value FIRST, erase second. Reading
    // through a pointer after the container changed is exactly the dangling-
    // pointer trap the lookup comment in AppData.h warns about.
    // v28.3: deleting a parent takes its pieces with it — a "read the spec"
    // whose "Lab 4" no longer exists is noise, not a task. Cascade DELETE is
    // normally the option this file argues against, but the alternatives are
    // worse here: refusing makes a parent undeletable until each piece is
    // hand-deleted (busywork), and promoting orphans to top level silently
    // converts checklist lines into inexplicable full tasks. Note the
    // asymmetry with archive on purpose: archive is reversible, so its
    // cascade is generous; delete is not, so its cascade is at least honest
    // about the blast radius. Every removed id (parent AND pieces) gets the
    // same title-demotion courtesy for any block that referenced it.
    QHash<QString, QString> rescuedTitles; // removed id -> its title
    if (const Task* doomed = taskById(id)) {
        rescuedTitles.insert(id, doomed->title);
        for (const Task& t : m_tasks)
            if (t.parentId == id)
                rescuedTitles.insert(t.id, t.title);
    }

    const int before = m_tasks.size();
    m_tasks.erase(
        std::remove_if(m_tasks.begin(), m_tasks.end(),
                       [&](const Task& task) {
                           return task.id == id || task.parentId == id;
                       }),
        m_tasks.end());

    if (m_tasks.size() == before)
        return false;

    for (Event& e : m_events) {
        if (!rescuedTitles.contains(e.taskId))
            continue;
        const QString rescued = rescuedTitles.value(e.taskId);
        e.taskId.clear();
        if (e.title.isEmpty())     // don't clobber a label the user typed
            e.title = rescued;     // the block now names itself
    }

    notifyChanged();
    return true;
}

bool AppData::updateTask(const QString& id, const QString& title,
                         const QString& description, QDate dueDate,
                         QTime dueTime, Task::Repeat repeat,
                         Task::Priority priority)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;

    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty())
        return false; // a task must keep a real title — same rule as birth

    task->title       = trimmed;
    task->description = description; // trimming notes would eat the user's
                                    // intentional blank lines — leave as-is
    task->dueDate     = dueDate;     // invalid QDate == "TBD", first-class
    task->dueTime     = dueDate.isValid() ? dueTime : QTime(); // same pairing
                                     // rule as the birth door and the date
                                     // setter — one invariant, three
                                     // enforcers, no way in around it
    task->repeat      = repeat;
    task->priority    = priority;    // v7: the urgency rank rides the same edit
    notifyChanged();  // ONE mutation, ONE repaint — see the header's rationale
    return true;
}

QString AppData::addFolder(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};

    Folder folder;
    folder.id   = ids::newId();
    folder.name = trimmed;
    m_folders.append(folder);

    notifyChanged();
    return folder.id;
}

bool AppData::renameFolder(const QString& id, const QString& name)
{
    Folder* folder = findById(m_folders, id);
    if (!folder || name.trimmed().isEmpty())
        return false;
    folder->name = name.trimmed();
    notifyChanged();
    return true;
}

bool AppData::removeFolder(const QString& id)
{
    // Same integrity family as removeCategory: a container may only be
    // deleted once it is empty (addendum, integrity extension).
    if (categoryCountInFolder(id) > 0)
        return false;

    const int before = m_folders.size();
    m_folders.erase(
        std::remove_if(m_folders.begin(), m_folders.end(),
                       [&](const Folder& f) { return f.id == id; }),
        m_folders.end());

    if (m_folders.size() == before)
        return false;
    notifyChanged();
    return true;
}

bool AppData::setCategoryFolder(const QString& categoryId,
                                const QString& folderId)
{
    Category* c = findById(m_categories, categoryId);
    // "" is a legal destination (top level); anything else must exist —
    // references are never born broken.
    if (!c || (!folderId.isEmpty() && !folderById(folderId)))
        return false;
    if (c->folderId == folderId)
        return true; // no change, no signal storm

    c->folderId = folderId;
    notifyChanged();
    return true;
}

QString AppData::addSpecialDay(const QString& title, QDate date,
                               bool repeatsYearly)
{
    if (title.trimmed().isEmpty() || !date.isValid())
        return {};

    SpecialDay day;
    day.id            = ids::newId();
    day.title         = title.trimmed();
    day.date          = date;
    day.repeatsYearly = repeatsYearly;
    m_specialDays.append(day);

    notifyChanged();
    return day.id;
}

bool AppData::updateSpecialDay(const QString& id, const QString& title,
                               QDate date, bool repeatsYearly,
                               const QColor& color)
{
    SpecialDay* day = findById(m_specialDays, id);
    if (!day)
        return false;
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty() || !date.isValid())
        return false; // a day keeps a real name and a real date — birth rules
    day->title         = trimmed;
    day->date          = date;
    day->repeatsYearly = repeatsYearly;
    day->color         = color; // invalid = back to automatic urgency colours
    notifyChanged();
    return true;
}

bool AppData::removeSpecialDay(const QString& id)
{
    const int before = m_specialDays.size();
    m_specialDays.erase(
        std::remove_if(m_specialDays.begin(), m_specialDays.end(),
                       [&](const SpecialDay& d) { return d.id == id; }),
        m_specialDays.end());

    if (m_specialDays.size() == before)
        return false;
    notifyChanged();
    return true;
}

// ---- live-timer crash insurance ----------------------------------------------

void AppData::setRunning(const RunningState& state)
{
    m_running = state;
    notifyChanged(); // changed() -> save: the insurance MUST reach the disk
}

void AppData::touchRunning(const QDateTime& lastSeen)
{
    if (!m_running)
        return;
    m_running->lastSeen = lastSeen;
    notifyChanged();
}

void AppData::clearRunning()
{
    if (!m_running)
        return;
    m_running.reset();
    notifyChanged();
}

QString AppData::recoverInterruptedTracking()
{
    // Called once, right after load. If a RunningState is on disk, the app
    // previously died (crash, kill, power cut) while a timer ran: the clean
    // shutdown path always commits the segment and clears this block.
    if (!m_running)
        return {};

    const RunningState state = *m_running;
    m_running.reset();

    Segment s;
    s.kind  = state.kind;
    s.start = state.start;
    // We honestly do not know when the app died — the heartbeat's lastSeen
    // is our best evidence, so at most one heartbeat interval is lost.
    // (Supplementary Spec: "at most the current in-progress interval is
    // affected.")
    s.end   = state.lastSeen.isValid() ? state.lastSeen : state.start;

    const Event*    e = eventById(state.eventId);
    const Activity* a = e ? activityById(e->activityId) : nullptr;
    if (!e || s.seconds() <= 0)
        return {}; // nothing worth keeping — silently tidy up

    appendSegment(state.eventId, s);

    const QString name = a ? a->name : QStringLiteral("a block");
    return QStringLiteral("Recovered %1 min of tracked time on \"%2\" "
                          "from an interrupted session.")
        .arg(qMax<qint64>(1, s.seconds() / 60))
        .arg(name);
}

// ---- load & first run ----------------------------------------------------------

void AppData::resetFrom(QVector<Category> categories,
                        QVector<Activity> activities,
                        QVector<Event> events,
                        QVector<Task> tasks,
                        QVector<Folder> folders,
                        QVector<SpecialDay> specialDays,
                        std::optional<RunningState> running)
{
    // Parameters taken BY VALUE + std::move: the caller builds the vectors,
    // we steal their contents instead of copying them. Move semantics
    // (C++11) — the reason modern C++ can pass big things around cheaply.
    m_categories = std::move(categories);
    m_activities = std::move(activities);
    m_events     = std::move(events);
    m_tasks       = std::move(tasks);
    m_folders     = std::move(folders);
    m_specialDays = std::move(specialDays);
    m_running     = std::move(running);

    // v28.3, orphan adoption: a piece whose parentId resolves to nothing —
    // or to another PIECE (a hand-edited file nesting deeper than the
    // domain allows) — is promoted to a top-level task, right here at the
    // one door all loaded data enters through (file, sync, share alike).
    // Why promote instead of keep-or-drop: a dangling parentId would make
    // the task invisible on every list surface (those show parents only)
    // yet still counted by the guards — data loss by invisibility. Two
    // passes on purpose: judge everyone against the loaded state FIRST,
    // then fix — adopting while judging would let one adoption change the
    // verdict on the next piece.
    QHash<QString, const Task*> byId;
    byId.reserve(m_tasks.size());
    for (const Task& t : m_tasks)
        byId.insert(t.id, &t);
    QVector<QString> orphans;
    for (const Task& t : m_tasks) {
        if (t.parentId.isEmpty())
            continue;
        const Task* parent = byId.value(t.parentId, nullptr);
        if (!parent || parent->isPiece())
            orphans.append(t.id);
    }
    for (Task& t : m_tasks)
        if (orphans.contains(t.id))
            t.parentId.clear();
    // Deliberately no emit — see the header comment.
}

void AppData::replaceAll(QVector<Category> categories,
                         QVector<Activity> activities,
                         QVector<Event> events,
                         QVector<Task> tasks,
                         QVector<Folder> folders,
                         QVector<SpecialDay> specialDays,
                         std::optional<RunningState> running)
{
    resetFrom(std::move(categories), std::move(activities), std::move(events),
              std::move(tasks), std::move(folders), std::move(specialDays),
              std::move(running));
    notifyChanged(); // live replacement: rebuild every screen, trigger autosave
}

void AppData::seedDefaults()
{
    // The starter palette the prototype validated: five life areas that
    // together say "productivity has many dimensions" (design-doc §1) —
    // including the honest one nobody likes to log.
    const QString work   = addCategory("Work / Study",  QColor("#4C6FE0"));
    const QString social = addCategory("Social",        QColor("#E0688A"));
    const QString health = addCategory("Health",        QColor("#4CA96A"));
    const QString rest   = addCategory("Rest",          QColor("#8B6FD4"));
    const QString waste  = addCategory("Wasting time",  QColor("#C25B54"));

    addActivity("Study Math",     work);
    addActivity("Coding project", work);
    addActivity("Gym",            health);
    addActivity("Walk",           health);
    addActivity("Friends",        social);
    addActivity("Rest / nap",     rest);
    addActivity("Doomscroll",     waste);
}
