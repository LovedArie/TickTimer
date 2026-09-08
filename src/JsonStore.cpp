#include "JsonStore.h"

#include "AppData.h"
#include "Ids.h" // ids::newId — the v9-repeat upgrade path mints a Schedule

#include <algorithm> // std::max — the v13 estimate clamp

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

// ---- serialization helpers (file-local) -------------------------------------
// `static` at file scope = visible only inside this .cpp. These helpers are
// private machinery of the storage layer; nothing else should ever call them.
// Each pair (toJson / fromJson) is small and boring on purpose — boring code
// is what you want at the persistence boundary, where bugs eat user data.

// Timestamps are stored as ISO-8601 strings ("2026-07-03T14:05:00") —
// human-readable in the file and unambiguous to parse back.
static QString   dtToJson(const QDateTime& dt) { return dt.toString(Qt::ISODate); }
static QDateTime dtFromJson(const QString& s)  { return QDateTime::fromString(s, Qt::ISODate); }

static QJsonObject toJson(const Category& c)
{
    return QJsonObject{
        {"id",       c.id},
        {"name",     c.name},
        {"color",    c.color.name()}, // "#4C6FE0" — readable, portable
        {"folderId", c.folderId},     // "" = top level (v3 addition)
        {"archived", c.archived},     // v8: retired life areas
        // v15: which ordering this area's task list gives. A string, not the
        // enum's int, for the same reason repeat and priority are strings —
        // a hand-read file should say "manual", not 1, and an unknown value
        // has to be able to fall back to the default rather than to garbage.
        {"sortMode", sortModeToString(c.sortMode)},
    };
}

static Category categoryFromJson(const QJsonObject& o)
{
    Category c;
    c.id    = o["id"].toString();
    c.name  = o["name"].toString();
    c.color = QColor(o["color"].toString());
    c.folderId = o["folderId"].toString(); // absent in v1/v2 files -> ""
    c.archived = o["archived"].toBool();   // absent pre-v8 -> false
    // Absent pre-v15 -> "" -> Smart, which is the order every area has
    // always been shown in. Nothing moves on upgrade.
    c.sortMode = sortModeFromString(o["sortMode"].toString());
    return c;
}

static QJsonObject toJson(const Activity& a)
{
    return QJsonObject{
        {"id",         a.id},
        {"name",       a.name},
        {"categoryId",  a.categoryId},
        {"archived",    a.archived},    // v7
        {"description", a.description}, // v15
        {"sortKey",     a.sortKey},     // v15
    };
}

static Activity activityFromJson(const QJsonObject& o)
{
    Activity a;
    a.id          = o["id"].toString();
    a.name        = o["name"].toString();
    a.categoryId  = o["categoryId"].toString();
    a.archived    = o["archived"].toBool(); // missing key (pre-v7) -> false
    // Missing key (pre-v15) -> "" -> the empty description every activity
    // already had. Additive growth, no migration branch.
    a.description = o["description"].toString();
    // Absent pre-v15 -> 0 for every activity, which the stable sort in
    // AppData::activitiesIn turns back into today's insertion order.
    a.sortKey     = o["sortKey"].toInt();
    return a;
}

// One place the SegmentKind <-> JSON string mapping lives, so the writer and
// the (tolerant) reader can't drift, and both the segment path and the
// crash-recovery "running" block share it.
static const char* kindToStr(SegmentKind k)
{
    switch (k) {
    case SegmentKind::Break:      return "break";
    case SegmentKind::Distracted: return "distracted";
    case SegmentKind::Focus:      return "focus";
    }
    return "focus"; // unreachable, but the compiler wants a value
}

static SegmentKind kindFromStr(const QString& s)
{
    if (s == QLatin1String("break"))      return SegmentKind::Break;
    if (s == QLatin1String("distracted")) return SegmentKind::Distracted;
    return SegmentKind::Focus; // unknown/missing -> safe default (old files ok)
}

static QJsonObject toJson(const Segment& s)
{
    return QJsonObject{
        {"kind",  kindToStr(s.kind)},
        {"start", dtToJson(s.start)},
        {"end",   dtToJson(s.end)},
    };
}

static Segment segmentFromJson(const QJsonObject& o)
{
    Segment s;
    s.kind  = kindFromStr(o["kind"].toString());
    s.start = dtFromJson(o["start"].toString());
    s.end   = dtFromJson(o["end"].toString());
    return s;
}

static QJsonObject toJson(const Folder& folder)
{
    return QJsonObject{{"id", folder.id},
                       {"name", folder.name},
                       {"archived", folder.archived}}; // v15
}

static Folder folderFromJson(const QJsonObject& o)
{
    Folder folder;
    folder.id       = o["id"].toString();
    folder.name     = o["name"].toString();
    folder.archived = o["archived"].toBool(); // pre-v15 -> false
    return folder;
}

// ---- Schedule (v15) --------------------------------------------------------
// skipDates is a QStringList of ISO dates and goes out as a JSON array of
// strings -- the first array-valued field in this file that is not a nested
// object. A comma-joined string would have been fewer lines and would have
// made "what if a date ever contains a comma" a question somebody has to
// answer; an array cannot be ambiguous.
static QJsonObject toJson(const Schedule& sched)
{
    QJsonArray skips;
    for (const QString& d : sched.skipDates)
        skips.append(d);

    QJsonArray weekdaysArray;
    for (int d : sched.weekdays)
        weekdaysArray.append(d);

    return QJsonObject{
        {"id",              sched.id},
        {"activityId",      sched.activityId},
        {"title",           sched.title},
        {"startDate",       sched.startDate.toString(Qt::ISODate)},
        // Invalid QDate -> "" -> parses back invalid = "open-ended". The
        // same free ride the TBD due date has taken since v3.
        {"endDate",         sched.endDate.toString(Qt::ISODate)},
        {"startMinutes",    sched.startMinutes},
        {"endMinutes",      sched.endMinutes},
        {"repeat",          repeatToString(sched.repeat)},
        // v16: which weekdays a weekly rule lands on, 1=Mon..7=Sun. An
        // ABSENT key (every v15 file) reads as an empty list, which
        // Schedule.h defines as "the weekday of startDate" — exactly what
        // those rules already meant. Additive, no migration branch.
        {"weekdays",        weekdaysArray},
        {"reminderMinutes", sched.reminderMinutes},
        {"skipDates",       skips},
    };
}

static Schedule scheduleFromJson(const QJsonObject& o)
{
    Schedule sched;
    sched.id              = o["id"].toString();
    sched.activityId      = o["activityId"].toString();
    sched.title           = o["title"].toString();
    sched.startDate       = QDate::fromString(o["startDate"].toString(),
                                              Qt::ISODate);
    sched.endDate         = QDate::fromString(o["endDate"].toString(),
                                              Qt::ISODate);
    sched.startMinutes    = o["startMinutes"].toInt();
    sched.endMinutes      = o["endMinutes"].toInt();
    sched.repeat          = repeatFromString(o["repeat"].toString());
    sched.reminderMinutes = o["reminderMinutes"].toInt();
    for (const QJsonValue& v : o["skipDates"].toArray())
        sched.skipDates.append(v.toString());
    // v16; absent -> empty -> "the weekday of startDate" (Schedule.h).
    // Values outside 1..7 are dropped rather than trusted: this is a file,
    // and a file can say anything.
    for (const QJsonValue& v : o["weekdays"].toArray()) {
        const int d = v.toInt();
        if (d >= 1 && d <= 7 && !sched.weekdays.contains(d))
            sched.weekdays.append(d);
    }
    std::sort(sched.weekdays.begin(), sched.weekdays.end());
    return sched;
}

static QJsonObject toJson(const Mood& mood)
{
    return QJsonObject{
        {"date",  mood.date.toString(Qt::ISODate)},
        {"level", moodLevelToString(mood.level)},
        {"note",  mood.note}, // stored in full; NEVER copied into briefings
    };
}

static Mood moodFromJson(const QJsonObject& o)
{
    Mood m;
    m.date  = QDate::fromString(o["date"].toString(), Qt::ISODate);
    m.level = moodLevelFromString(o["level"].toString());
    m.note  = o["note"].toString();
    return m;
}

static QJsonObject toJson(const SpecialDay& day)
{
    return QJsonObject{
        {"id",            day.id},
        {"title",         day.title},
        {"date",          day.date.toString(Qt::ISODate)},
        {"repeatsYearly", day.repeatsYearly},
        // v7: an invalid colour serialises to "" and parses back invalid —
        // "no colour chosen" round-trips for free, like the TBD date.
        {"color",         day.color.isValid() ? day.color.name() : QString()},
    };
}

static SpecialDay specialDayFromJson(const QJsonObject& o)
{
    SpecialDay day;
    day.id            = o["id"].toString();
    day.title         = o["title"].toString();
    day.date          = QDate::fromString(o["date"].toString(), Qt::ISODate);
    day.repeatsYearly = o["repeatsYearly"].toBool();
    day.color         = QColor(o["color"].toString()); // "" -> invalid
    return day;
}

static QJsonObject toJson(const Task& task)
{
    return QJsonObject{
        {"id",          task.id},
        {"title",       task.title},
        {"categoryId",  task.categoryId},
        {"done",        task.done},
        // An invalid QDate serialises to "" and parses back invalid —
        // the "DATE TBD" state round-trips for free (§3.11).
        {"dueDate",     task.dueDate.toString(Qt::ISODate)},
        // v22: the deadline's clock half. Same free ride as every optional
        // field before it — an invalid QTime serialises to "" and parses
        // back invalid, so "all day" round-trips with no flag beside it,
        // and a v21 file (no key at all) loads as all-day. Because sync and
        // sharing reuse THIS converter, the time syncs with zero extra work.
        {"dueTime",     task.dueTime.toString(Qt::ISODate)},
        // v4 additions. Both are optional on read (see below), so a v3
        // file with neither key loads without complaint.
        {"description", task.description},
        {"repeat",      repeatToString(task.repeat)},
        // v7 additions: the archive stage and the urgency rank.
        {"archived",    task.archived},
        {"priority",    priorityToString(task.priority)},
        // v10 additions (needs-a-block §C/§G). Invalid QDateTime -> "" ->
        // parses back invalid: "not dismissed" round-trips for free, the
        // same trick the TBD due date has used since v3. Because this SAME
        // conversion feeds the sync wire, both facts sync with no further
        // work — the free ride taskId got in v6.
        {"dismissedUntil", task.dismissedUntil.toString(Qt::ISODate)},
        {"dismissCount",   task.dismissCount},
        // v13 additions (subtasks §I, sizing §J.1). All three ride the
        // additive-growth train: empty string / 0 / false are exactly the
        // struct's defaults, so a v12 file with none of these keys loads
        // as "top-level, unsized, needs a real run" — which is what every
        // pre-v13 task WAS. Sixth time this trick has paid for itself.
        {"parentId",        task.parentId},
        {"estimateMinutes", task.estimateMinutes},
        {"chunkable",       task.chunkable},
        {"sortKey",         task.sortKey}, // v15: the hand-dragged position
        // v15: when the chain stops. Invalid -> "" -> parses back invalid =
        // "forever", which is what every task before this meant.
        {"repeatUntil",     task.repeatUntil.toString(Qt::ISODate)},
    };
}

static Task taskFromJson(const QJsonObject& o)
{
    Task task;
    task.id         = o["id"].toString();
    task.title      = o["title"].toString();
    task.categoryId = o["categoryId"].toString();
    task.done       = o["done"].toBool();
    task.dueDate    = QDate::fromString(o["dueDate"].toString(), Qt::ISODate);
    // v22: missing key -> "" -> invalid QTime -> "all day", which is exactly
    // what every pre-v22 task meant. Additive growth, fifth time now.
    task.dueTime    = QTime::fromString(o["dueTime"].toString(), Qt::ISODate);
    // A missing key returns a null QJsonValue; .toString() gives "" and
    // repeatFromString("") is Repeat::None — so pre-v4 tasks read exactly
    // as "no notes, no recurrence" with zero special-casing. THIS is what
    // "additive growth" buys: no migration branch, just sane defaults.
    task.description = o["description"].toString();
    task.repeat      = repeatFromString(o["repeat"].toString());
    task.archived    = o["archived"].toBool();                    // v7
    task.priority    = priorityFromString(o["priority"].toString()); // v7
    // v10: missing keys read as "never dismissed" — an invalid QDateTime
    // and a zero count are exactly the defaults the struct already carries,
    // so a v9 file loads with zero special-casing. Additive growth, again.
    task.dismissedUntil = QDateTime::fromString(
        o["dismissedUntil"].toString(), Qt::ISODate);
    task.dismissCount   = o["dismissCount"].toInt();              // v10
    // v13: missing keys read as the defaults ("" / 0 / false) — see the
    // writer's comment. A negative estimate in a hand-edited file is
    // clamped to "unset" here for the same reason setTaskSize clamps it:
    // no reader should ever meet minus twenty minutes. Note what is NOT
    // checked here: whether parentId resolves. The loader converts one
    // record at a time and cannot see the others; referential repair is
    // AppData::resetFrom's job (orphan adoption), where the whole picture
    // exists.
    task.parentId        = o["parentId"].toString();
    task.estimateMinutes = std::max(0, o["estimateMinutes"].toInt());
    task.chunkable       = o["chunkable"].toBool();
    task.sortKey         = o["sortKey"].toInt(); // v15: absent -> 0
    task.repeatUntil     = QDate::fromString(o["repeatUntil"].toString(),
                                             Qt::ISODate); // v15; absent -> forever
    return task;
}

static QJsonObject toJson(const Event& e)
{
    QJsonArray segments;
    for (const Segment& s : e.segments)
        segments.append(toJson(s));

    return QJsonObject{
        {"id",           e.id},
        {"date",         e.date.toString(Qt::ISODate)},
        {"startMinutes", e.plannedStartMinutes},
        {"endMinutes",   e.plannedEndMinutes},
        {"activityId",   e.activityId},
        {"taskId",       e.taskId},   // v6: block identity may be a Task…
        // v15: which rule produced this block. It REPLACES the v9 "repeat"
        // key, and the old key is deliberately still READ below rather than
        // repurposed -- never repurpose a key (CLAUDE.md); a v9 file's
        // "repeat" still means what it always meant, and the loader turns it
        // into the thing that means it now.
        {"scheduleId",   e.scheduleId},
        // v11: the catch-up verdict. Unset serialises to "", which is also
        // what a pre-v11 file's MISSING key reads back as — so the format
        // grows without a migration branch, the fourth time running.
        {"outcome",      blockOutcomeToString(e.outcome)},
        // v14: EVERY replacement, in creation order — a plain move has one,
        // a split has several. `movedToId` stays beside it as a COMPATIBILITY
        // MIRROR of the first element, so a v13 build reads exactly what it
        // always read.
        //
        // A mirror is safe HERE and would not be safe in memory. One door
        // writes both in the same instant and the loader below always prefers
        // the list, so no reader ever has to choose between two live
        // opinions. Event.h keeps a single field for the same reason in
        // reverse: nothing in the domain should be able to hold two.
        {"movedToIds",   QJsonArray::fromStringList(e.movedToIds)},
        {"movedToId",    e.movedToIds.value(0)},
        {"title",        e.title},    // v6: …or just this label (ad-hoc)
        {"note",         e.note},
        {"segments",     segments},
    };
}

static Event eventFromJson(const QJsonObject& o)
{
    Event e;
    e.id                  = o["id"].toString();
    e.date                = QDate::fromString(o["date"].toString(), Qt::ISODate);
    e.plannedStartMinutes = o["startMinutes"].toInt();
    e.plannedEndMinutes   = o["endMinutes"].toInt();
    e.activityId          = o["activityId"].toString();
    // Pre-v6 events have neither key; a missing key reads as "" — an
    // activity-only event, exactly what it always was. Additive growth,
    // tolerant read, no migration branch: the same recipe as tasks (v4)
    // and distracted time (v5), third time now, and it still costs nothing.
    e.taskId              = o["taskId"].toString();
    // repeatFromString("") == None — pre-v9 files read exactly as they
    // behaved: nothing repeated. The same absent-field migration trick
    // the task's repeat used at v4.
    e.scheduleId          = o["scheduleId"].toString(); // v15; pre-v15 -> ""
    // v11: blockOutcomeFromString("") == Unset, and anything unrecognised
    // also degrades to Unset — garbage on disk can never invent a decision
    // the user didn't make. A movedToId whose target no longer exists is
    // tolerated here and ignored by readers, exactly like a dangling
    // activityId: the loader repairs shape, not referential integrity.
    e.outcome             = blockOutcomeFromString(o["outcome"].toString());
    // v14: prefer the list. A pre-v14 file carries only the single key, and
    // wrapping it reproduces exactly the old behaviour — one replacement, or
    // none at all when the key is absent and reads back as "". Additive
    // growth, tolerant read, no migration branch: the sixth time now.
    if (o.contains("movedToIds")) {
        for (const QJsonValue& v : o["movedToIds"].toArray()) {
            const QString replacementId = v.toString();
            if (!replacementId.isEmpty())
                e.movedToIds.append(replacementId);
        }
    } else {
        const QString replacementId = o["movedToId"].toString();
        if (!replacementId.isEmpty())
            e.movedToIds.append(replacementId);
    }
    e.title               = o["title"].toString();
    e.note                = o["note"].toString();
    for (const QJsonValue& v : o["segments"].toArray())
        e.segments.append(segmentFromJson(v.toObject()));
    return e;
}

// ---- JsonStore ----------------------------------------------------------------

JsonStore::JsonStore(QString filePath)
    : m_filePath(std::move(filePath))
{
}

QString JsonStore::defaultFilePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir); // ensure the folder exists before first save
    return dir + QStringLiteral("/data.json");
}

QString JsonStore::filePathForUser(const QString& username)
{
    // Empty username → the legacy global file. Keeps every store constructed
    // without a user (tests, tools) working exactly as before.
    if (username.trimmed().isEmpty())
        return defaultFilePath();

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    // Same canonical lowercasing as the server's PlannerStore: "Alice" and
    // "alice" are one account, so they must map to one file. The username's
    // charset is already restricted at registration (letters/digits/_/-), so
    // it's filename-safe here without further scrubbing.
    return dir + QStringLiteral("/data-")
         + username.trimmed().toLower() + QStringLiteral(".json");
}

QString JsonStore::basePathForUser(const QString& username)
{
    QString path = filePathForUser(username);
    // Same folder, same canonical name, different prefix — so the base can
    // never land somewhere the data file did not, and a user who copies
    // their profile carries both or neither.
    path.insert(path.lastIndexOf(QLatin1Char('/')) + 1,
                QStringLiteral("base-"));
    return path; // .../base-data-<user>.json
}

bool JsonStore::adoptGlobalDataForUser(const QString& username)
{
    if (username.trimmed().isEmpty())
        return false;

    const QString userPath = filePathForUser(username);
    if (QFile::exists(userPath))
        return false; // this account already has its own file — hands off

    const QString globalPath = defaultFilePath();
    if (!QFile::exists(globalPath))
        return false; // nothing to adopt — a genuinely fresh install

    // Copy the old global planner into this account's file. COPY, not move:
    // if anything about this upgrade went wrong we want the original intact.
    if (!QFile::copy(globalPath, userPath))
        return false;

    // Then retire the global file to a backup name, so the NEXT user to log
    // in doesn't also adopt it (adoption is a one-time event, for the first
    // user after the upgrade — realistically the person whose data it was).
    // Rename, not delete: still nothing is ever destroyed.
    const QString backup = globalPath + QStringLiteral(".pre-accounts.bak");
    QFile::remove(backup);            // clear any stale backup first
    QFile::rename(globalPath, backup);
    return true;
}

bool JsonStore::migrateDataFiles(const QDir& from, const QDir& to)
{
    // The one migration rule, factored out so a test can point it at two
    // temp folders: copy every planner file (global data.json AND every
    // per-account data-<user>.json) that exists in `from` and not in `to`.
    // COPY, never move — the old folder stays behind as a free backup, so
    // even a migration that went wrong destroys nothing. Never overwrite —
    // if the new home is lived-in, its files win.
    bool migrated = false;
    const QStringList files =
        from.entryList({QStringLiteral("data*.json")}, QDir::Files);
    for (const QString& name : files) {
        const QString dst = to.filePath(name);
        if (!QFile::exists(dst) && QFile::copy(from.filePath(name), dst))
            migrated = true;
    }
    return migrated;
}

bool JsonStore::migrateLegacyData()
{
    const QString newDirPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(newDirPath);
    const QDir newDir(newDirPath);
    bool migrated = false;

    // v22.8 — THE ORG-NAME MOVE. v22.7 gave the app an organization name so
    // QSettings would finally have a real address; what that change ALSO did,
    // unflagged, was move QStandardPaths::AppDataLocation one level deeper
    // (…/TickTimer → …/TickTimer/TickTimer), so the app opened an empty
    // folder and the owner met a blank planner — "my data is completely
    // erased." It never was: the file sat untouched at the old address.
    // The old home is literally the new home's PARENT, and only when the
    // last two path components match (org == app name) do we scan it — the
    // guard that keeps this from ever grazing an unrelated folder.
    {
        QDir oldHome(newDirPath);
        const QString leaf = oldHome.dirName();
        if (oldHome.cdUp() && oldHome.dirName() == leaf)
            migrated |= migrateDataFiles(oldHome, newDir);
    }

    // The pre-rename era ("TimeFocusTracker") — after the org-name move it
    // sits TWO levels up, not one; this chain had to be re-aimed too, or the
    // oldest upgrade path would have quietly died with the same change.
    if (!QFile::exists(newDir.filePath(QStringLiteral("data.json")))) {
        QDir roots(newDirPath);
        roots.cdUp();
        roots.cdUp();
        const QDir ancient(roots.filePath(QStringLiteral("TimeFocusTracker")));
        if (ancient.exists())
            migrated |= migrateDataFiles(ancient, newDir);
    }
    return migrated;
}

int JsonStore::formatVersionOf(const QJsonObject& root)
{
    // Absent is 0, not an error: every file written before v2 predates the
    // key, and those must keep loading. Only a version ABOVE ours is fatal.
    return root.value(QStringLiteral("version")).toInt(0);
}

JsonStore::LoadResult JsonStore::load(AppData& data)
{
    m_error.clear();

    QFile file(m_filePath);
    if (!file.exists())
        return LoadResult::Empty; // first run — not an error, nothing to load

    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Could not open %1: %2")
                      .arg(m_filePath, file.errorString());
        return LoadResult::Unreadable;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        m_error = QStringLiteral("Data file is not valid JSON: %1")
                      .arg(parseError.errorString());
        return LoadResult::Unreadable;
    }

    // THE FLOOR. Checked here as well as inside applyJsonObject so the answer
    // can name the numbers — the caller has to tell a human which version
    // wrote this and which one is running, and "it failed" does not.
    const int found = formatVersionOf(doc.object());
    if (found > kFormatVersion) {
        m_readOnly = true; // latched before any chance to save
        m_error = QStringLiteral(
                      "%1 was written by a newer version of TickTimer "
                      "(data format %2). This copy understands format %3. "
                      "Opening it would delete everything this version does "
                      "not recognise, so it has not been opened. Update "
                      "TickTimer.")
                      .arg(m_filePath)
                      .arg(found)
                      .arg(kFormatVersion);
        return LoadResult::TooNew;
    }

    if (!applyJsonObject(data, doc.object(), /*announceChange=*/false)) {
        // applyJsonObject only refuses on the floor, which is already
        // handled above — but if that ever stops being true, refusing to
        // save is the safe answer, not seeding over the file.
        m_readOnly = true;
        if (m_error.isEmpty())
            m_error = QStringLiteral("Could not apply the contents of %1")
                          .arg(m_filePath);
        return LoadResult::Unreadable;
    }
    return LoadResult::Loaded;
}

bool JsonStore::applyJsonObject(AppData& data, const QJsonObject& root,
                                bool announceChange)
{
    // THE FLOOR (design-addendum-format-floor.md §F.2). Nothing below this
    // line may run against a document we cannot fully read: every loop here
    // is deliberately tolerant of keys it does not know, which is exactly
    // what makes a newer document load quietly and lossily. Refuse before
    // `data` is touched, so a rejected document leaves the caller's AppData
    // as it found it.
    if (formatVersionOf(root) > kFormatVersion)
        return false;

    QVector<Category> categories;
    for (const QJsonValue& v : root["categories"].toArray())
        categories.append(categoryFromJson(v.toObject()));

    QVector<Activity> activities;
    for (const QJsonValue& v : root["activities"].toArray())
        activities.append(activityFromJson(v.toObject()));

    QVector<Event> events;
    // v15: the v9 "repeat" key is still read, into a side map rather than
    // onto the Event -- the field it used to fill no longer exists. It is
    // converted into a Schedule further down. Reading an old key for its
    // original meaning is what a tolerant loader does; repurposing it would
    // be the thing CLAUDE.md forbids.
    QHash<QString, Task::Repeat> legacyEventRepeats;
    for (const QJsonValue& v : root["events"].toArray()) {
        const QJsonObject o = v.toObject();
        events.append(eventFromJson(o));
        if (o.contains("repeat"))
            legacyEventRepeats.insert(o["id"].toString(),
                                      repeatFromString(o["repeat"].toString()));
    }

    // Version-1 files have no "tasks" key; a missing key reads as an
    // empty array — old files load into the grown format unchanged.
    // This is what "additive change" means in practice.
    QVector<Task> tasks;
    for (const QJsonValue& v : root["tasks"].toArray())
        tasks.append(taskFromJson(v.toObject()));

    QVector<Folder> folders;
    for (const QJsonValue& v : root["folders"].toArray())
        folders.append(folderFromJson(v.toObject()));

    QVector<SpecialDay> specialDays;
    for (const QJsonValue& v : root["specialDays"].toArray())
        specialDays.append(specialDayFromJson(v.toObject()));

    std::optional<RunningState> running;
    if (root.contains("running")) {
        const QJsonObject r = root["running"].toObject();
        RunningState state;
        state.eventId  = r["eventId"].toString();
        state.kind     = kindFromStr(r["kind"].toString());
        state.start    = dtFromJson(r["start"].toString());
        state.lastSeen = dtFromJson(r["lastSeen"].toString());
        running = state;
    }

    // v28.2 — moods land through their own silent door, BEFORE the big
    // reset, so replaceAll's single changed() (or resetFrom's silence)
    // covers them too. A file older than v12 simply has no array: the
    // loop runs zero times and the owner starts with no history, which is
    // the correct migration — mood cannot be back-derived.
    QVector<Mood> moods;
    for (const QJsonValue& v : root["moods"].toArray())
        moods.append(moodFromJson(v.toObject()));
    data.setMoodsFromLoad(std::move(moods));

    // v31 -- schedules land through the same silent door as moods, and for
    // the same reason: one more container would otherwise mean an eighth
    // parameter on resetFrom, replaceAll, and every call site of both.
    QVector<Schedule> schedules;
    for (const QJsonValue& v : root["schedules"].toArray())
        schedules.append(scheduleFromJson(v.toObject()));

    // ---- the one upgrade path in this file, and why it is not a migration --
    //
    // A file written before v15 may carry Event.repeat: the v9 rule that
    // lived on the newest block of a chain. That mechanism is gone, so the
    // fact is carried across to the mechanism that replaced it rather than
    // dropped -- a user who set "repeats weekly" on a block should not find
    // it silently forgotten by an upgrade.
    //
    // Note what this is NOT: the old key is still read exactly as it always
    // meant, and no key was repurposed. The conversion is a READING of an
    // old fact into the current model, which is what a tolerant loader is
    // for; a v14 file loaded by a v14 build still behaves as it always did.
    for (Event& e : events) {
        const Task::Repeat legacy =
            legacyEventRepeats.value(e.id, Task::Repeat::None);
        if (legacy == Task::Repeat::None || !e.date.isValid())
            continue;
        Schedule sched;
        sched.id           = ids::newId();
        sched.activityId   = e.activityId;
        sched.title        = e.title;
        sched.startDate    = e.date;
        sched.startMinutes = e.plannedStartMinutes;
        sched.endMinutes   = e.plannedEndMinutes;
        sched.repeat       = legacy;
        // A block-carried repeat had no identity of its own when the block
        // was task-linked; give the rule the task's title so the migrated
        // schedule can still say what it is (schedules do not link tasks --
        // see the v9 comment rollRepeats used to carry: next week's block
        // should not claim a deliverable that may be done by then).
        if (sched.activityId.isEmpty() && sched.title.trimmed().isEmpty())
            continue; // nothing to name it with; drop the rule, keep the block
        schedules.append(sched);
        e.scheduleId = sched.id;
    }
    data.setSchedulesFromLoad(std::move(schedules));

    // Startup goes the silent way (nobody is listening yet); a live sync
    // pull goes the loud way so every screen rebuilds — see AppData.h.
    if (announceChange)
        data.replaceAll(std::move(categories), std::move(activities),
                        std::move(events), std::move(tasks),
                        std::move(folders), std::move(specialDays),
                        std::move(running));
    else
        data.resetFrom(std::move(categories), std::move(activities),
                       std::move(events), std::move(tasks),
                       std::move(folders), std::move(specialDays),
                       std::move(running));
    return true;
}

QJsonObject JsonStore::toJsonObject(const AppData& data)
{
    QJsonArray categories;
    for (const Category& c : data.categories())
        categories.append(toJson(c));

    QJsonArray activities;
    for (const Activity& a : data.activities())
        activities.append(toJson(a));

    QJsonArray events;
    for (const Event& e : data.events())
        events.append(toJson(e));

    QJsonArray tasks;
    for (const Task& task : data.tasks())
        tasks.append(toJson(task));

    QJsonArray folders;
    for (const Folder& folder : data.folders())
        folders.append(toJson(folder));

    QJsonArray specialDays;
    for (const SpecialDay& day : data.specialDays())
        specialDays.append(toJson(day));

    QJsonArray moodsJson;
    for (const Mood& mood : data.moods())
        moodsJson.append(toJson(mood));

    QJsonArray schedules;
    for (const Schedule& sched : data.schedules())
        schedules.append(toJson(sched));

    QJsonObject root{
        // The version number planted on day one, earning its keep: the
        // format grew a "tasks" array, so 1 becomes 2. The change is
        // additive (old files still load — see the loader), but bumping
        // costs nothing and lets any future reader that must care tell
        // the files apart.
        // kFormatVersion, never a literal: the number written and the
        // number refused on read are one constant (JsonStore.h).
        {"version",     kFormatVersion},
                             // v16: + Schedule.weekdays (a weekly rule names
                             //      its own days; startDate no longer decides
                             //      the weekday — see Schedule.h).
                             //      v15: + the "schedules" array and
                             //      Event.scheduleId (recurrence you can
                             //      look FORWARD along, replacing the v9
                             //      Event.repeat chain); + Activity
                             //      .description/.sortKey, Folder.archived,
                             //      Category.sortMode, Task.sortKey
                             //      (editable activities, a semester that
                             //      can retire whole, and hand ordering).
                             //      v14: + Event.movedToIds (the split's inverse —
                             //      movedToId stays as its compat mirror);
                             //      v13: + Task.parentId / estimateMinutes /
                             //      chunkable (subtasks §I + sizing §J.1 —
                             //      the v27 re-land, finally). v12: + moods
                             //      (check-in §G.2); v11: + Event.outcome /
                             //      movedToId (catch-up — where the audit's
                             //      numbering collision ended); v10:
                             //      + Task.dismissedUntil / dismissCount;
                             //      v9: + Event.repeat.
        {"categories",  categories},
        {"activities",  activities},
        {"schedules",   schedules},   // v15
        {"events",      events},
        {"tasks",       tasks},
        {"folders",     folders},
        {"specialDays", specialDays},
        {"moods",       moodsJson},
    };

    if (data.running()) {
        const RunningState& r = *data.running();
        root["running"] = QJsonObject{
            {"eventId",  r.eventId},
            {"kind",     kindToStr(r.kind)},
            {"start",    dtToJson(r.start)},
            {"lastSeen", dtToJson(r.lastSeen)},
        };
    }

    return root;
}

bool JsonStore::save(const AppData& data)
{
    // The one file this binary must never replace is the one it could not
    // understand. m_error is NOT cleared first: the sentence explaining which
    // version wrote the file is the useful one, and it was set by load().
    if (m_readOnly)
        return false;

    m_error.clear();

    const QJsonObject root = toJsonObject(data);

    // THE RELIABILITY RULE (Supplementary Spec): "a crash during a save must
    // not corrupt the existing data — write safely (write-then-replace)."
    // QSaveFile IS that rule, shipped with Qt: it writes to a hidden
    // temporary file and only on commit() atomically renames it over the
    // real one. Kill the process mid-save and yesterday's file is intact.
    // A plain QFile would truncate the file first and could leave you with
    // half a file — with your whole history in it.
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = QStringLiteral("Could not write %1: %2")
                      .arg(m_filePath, file.errorString());
        return false;
    }

    // Indented = human-readable on disk; a few wasted bytes buy easy
    // debugging, the very reason we chose JSON (design-doc §4).
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

    if (!file.commit()) {
        m_error = QStringLiteral("Could not commit save to %1: %2")
                      .arg(m_filePath, file.errorString());
        return false;
    }
    return true;
}
