#include "JsonStore.h"

#include "AppData.h"

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
    return c;
}

static QJsonObject toJson(const Activity& a)
{
    return QJsonObject{
        {"id",         a.id},
        {"name",       a.name},
        {"categoryId", a.categoryId},
        {"archived",   a.archived}, // v7
    };
}

static Activity activityFromJson(const QJsonObject& o)
{
    Activity a;
    a.id         = o["id"].toString();
    a.name       = o["name"].toString();
    a.categoryId = o["categoryId"].toString();
    a.archived   = o["archived"].toBool(); // missing key (pre-v7) -> false
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
    return QJsonObject{{"id", folder.id}, {"name", folder.name}};
}

static Folder folderFromJson(const QJsonObject& o)
{
    Folder folder;
    folder.id   = o["id"].toString();
    folder.name = o["name"].toString();
    return folder;
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
        {"repeat",       repeatToString(e.repeat)}, // v9: recurring blocks
        // v11: the catch-up verdict. Unset serialises to "", which is also
        // what a pre-v11 file's MISSING key reads back as — so the format
        // grows without a migration branch, the fourth time running.
        {"outcome",      blockOutcomeToString(e.outcome)},
        {"movedToId",    e.movedToId},
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
    e.repeat              = repeatFromString(o["repeat"].toString());
    // v11: blockOutcomeFromString("") == Unset, and anything unrecognised
    // also degrades to Unset — garbage on disk can never invent a decision
    // the user didn't make. A movedToId whose target no longer exists is
    // tolerated here and ignored by readers, exactly like a dangling
    // activityId: the loader repairs shape, not referential integrity.
    e.outcome             = blockOutcomeFromString(o["outcome"].toString());
    e.movedToId           = o["movedToId"].toString();
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

bool JsonStore::load(AppData& data)
{
    m_error.clear();

    QFile file(m_filePath);
    if (!file.exists())
        return false; // first run — not an error, just nothing to load yet

    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Could not open %1: %2")
                      .arg(m_filePath, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        m_error = QStringLiteral("Data file is not valid JSON: %1")
                      .arg(parseError.errorString());
        return false;
    }

    return applyJsonObject(data, doc.object(), /*announceChange=*/false);
}

bool JsonStore::applyJsonObject(AppData& data, const QJsonObject& root,
                                bool announceChange)
{
    QVector<Category> categories;
    for (const QJsonValue& v : root["categories"].toArray())
        categories.append(categoryFromJson(v.toObject()));

    QVector<Activity> activities;
    for (const QJsonValue& v : root["activities"].toArray())
        activities.append(activityFromJson(v.toObject()));

    QVector<Event> events;
    for (const QJsonValue& v : root["events"].toArray())
        events.append(eventFromJson(v.toObject()));

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

    QJsonObject root{
        // The version number planted on day one, earning its keep: the
        // format grew a "tasks" array, so 1 becomes 2. The change is
        // additive (old files still load — see the loader), but bumping
        // costs nothing and lets any future reader that must care tell
        // the files apart.
        {"version",     13}, // v13: + Task.parentId / estimateMinutes /
                             //      chunkable (subtasks §I + sizing §J.1 —
                             //      the v27 re-land, finally). v12: + moods
                             //      (check-in §G.2); v11: + Event.outcome /
                             //      movedToId (catch-up — where the audit's
                             //      numbering collision ended); v10:
                             //      + Task.dismissedUntil / dismissCount;
                             //      v9: + Event.repeat.
        {"categories",  categories},
        {"activities",  activities},
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
