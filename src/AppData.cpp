#include "AppData.h"

#include "Ids.h"

#include <algorithm> // std::sort, std::remove_if — the STL, not hand-rolled loops

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
const Event*    AppData::eventById(const QString& id)    const { return findById(m_events, id); }
const Task*     AppData::taskById(const QString& id)      const { return findById(m_tasks, id); }
const Folder*   AppData::folderById(const QString& id)    const { return findById(m_folders, id); }
Event*          AppData::mutableEventById(const QString& id)   { return findById(m_events, id); }

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
        if (task.categoryId == categoryId)
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
        if (!task.done && !task.archived && task.dueDate.isValid())
            result.append(&task);

    std::sort(result.begin(), result.end(),
              [](const Task* a, const Task* b) {
                  if (a->dueDate != b->dueDate)
                      return a->dueDate < b->dueDate; // most urgent first
                  return a->title.localeAwareCompare(b->title) < 0;
              });
    return result;
}

QVector<const Task*> AppData::tasksDueOn(QDate date) const
{
    // Exactly this day, still open. Overdue tasks are NOT included: their
    // day has passed, so they belong to Upcoming's overdue list, not to
    // this calendar day. Sorted by title for a stable order (all share the
    // one date, so there is nothing else to sort by).
    QVector<const Task*> result;
    for (const Task& task : m_tasks)
        if (!task.done && !task.archived && task.dueDate == date)
            result.append(&task);

    std::sort(result.begin(), result.end(), [](const Task* a, const Task* b) {
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

    emit changed();
    return c.id;
}

bool AppData::renameCategory(const QString& id, const QString& name)
{
    Category* c = findById(m_categories, id);
    if (!c || name.trimmed().isEmpty())
        return false;
    c->name = name.trimmed();
    emit changed();
    return true;
}

bool AppData::recolorCategory(const QString& id, const QColor& color)
{
    Category* c = findById(m_categories, id);
    if (!c || !color.isValid())
        return false;
    c->color = color;
    emit changed();
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
    emit changed();
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

    emit changed();
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
    emit changed();
    return true;
}

// The shared tail of all three creation doors: the time-range rules live
// HERE, once. The doors above it differ only in which identity they verify —
// so a fourth identity kind someday is one new door, zero touched rules.
QString AppData::appendGuardedEvent(QDate date, int startMin, int endMin,
                                    const QString& activityId,
                                    const QString& taskId,
                                    const QString& title)
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

    emit changed();
    return e.id;
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

    emit changed();
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

    emit changed();
    return true;
}

bool AppData::setEventNote(const QString& id, const QString& note)
{
    Event* e = mutableEventById(id);
    if (!e)
        return false;
    e->note = note;
    emit changed();
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
    emit changed();
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
    emit changed();
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

    emit changed();
    return true;
}

bool AppData::appendSegment(const QString& eventId, const Segment& segment)
{
    Event* e = mutableEventById(eventId);
    if (!e || segment.seconds() <= 0)  // a zero-length segment is noise, drop it
        return false;
    e->segments.append(segment);
    emit changed();
    return true;
}

bool AppData::removeSegment(const QString& eventId, int index)
{
    Event* e = mutableEventById(eventId);
    if (!e || index < 0 || index >= e->segments.size())
        return false; // out-of-range is refused, never clamped — a retraction
                      // must name exactly the fact it retracts
    e->segments.removeAt(index);
    emit changed();
    return true;
}

QString AppData::addTask(const QString& title, const QString& categoryId,
                         QDate dueDate)
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
    m_tasks.append(task);

    emit changed();
    return task.id;
}

bool AppData::setActivityArchived(const QString& id, bool archived)
{
    Activity* a = findById(m_activities, id);
    if (!a)
        return false;
    if (a->archived == archived)
        return true; // idempotent, no changed() storm
    a->archived = archived;
    emit changed();
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
    emit changed();
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
    emit changed();
    return true;
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
    emit changed();
    return true;
}

bool AppData::setTaskDueDate(const QString& id, QDate dueDate)
{
    Task* task = findById(m_tasks, id);
    if (!task)
        return false;
    task->dueDate = dueDate; // invalid QDate clears back to "TBD"
    emit changed();
    return true;
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
    QString rescuedTitle;
    if (const Task* doomed = taskById(id))
        rescuedTitle = doomed->title;

    const int before = m_tasks.size();
    m_tasks.erase(
        std::remove_if(m_tasks.begin(), m_tasks.end(),
                       [&](const Task& task) { return task.id == id; }),
        m_tasks.end());

    if (m_tasks.size() == before)
        return false;

    for (Event& e : m_events) {
        if (e.taskId != id)
            continue;
        e.taskId.clear();
        if (e.title.isEmpty())      // don't clobber a label the user typed
            e.title = rescuedTitle; // the block now names itself
    }

    emit changed();
    return true;
}

bool AppData::updateTask(const QString& id, const QString& title,
                         const QString& description, QDate dueDate,
                         Task::Repeat repeat, Task::Priority priority)
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
    task->repeat      = repeat;
    task->priority    = priority;    // v7: the urgency rank rides the same edit
    emit changed();  // ONE mutation, ONE repaint — see the header's rationale
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

    emit changed();
    return folder.id;
}

bool AppData::renameFolder(const QString& id, const QString& name)
{
    Folder* folder = findById(m_folders, id);
    if (!folder || name.trimmed().isEmpty())
        return false;
    folder->name = name.trimmed();
    emit changed();
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
    emit changed();
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
    emit changed();
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

    emit changed();
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
    emit changed();
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
    emit changed();
    return true;
}

// ---- live-timer crash insurance ----------------------------------------------

void AppData::setRunning(const RunningState& state)
{
    m_running = state;
    emit changed(); // changed() -> save: the insurance MUST reach the disk
}

void AppData::touchRunning(const QDateTime& lastSeen)
{
    if (!m_running)
        return;
    m_running->lastSeen = lastSeen;
    emit changed();
}

void AppData::clearRunning()
{
    if (!m_running)
        return;
    m_running.reset();
    emit changed();
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
    emit changed(); // live replacement: rebuild every screen, trigger autosave
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
