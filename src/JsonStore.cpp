#include "JsonStore.h"

#include "AppData.h"

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
    };
}

static Category categoryFromJson(const QJsonObject& o)
{
    Category c;
    c.id    = o["id"].toString();
    c.name  = o["name"].toString();
    c.color = QColor(o["color"].toString());
    c.folderId = o["folderId"].toString(); // absent in v1/v2 files -> ""
    return c;
}

static QJsonObject toJson(const Activity& a)
{
    return QJsonObject{
        {"id",         a.id},
        {"name",       a.name},
        {"categoryId", a.categoryId},
    };
}

static Activity activityFromJson(const QJsonObject& o)
{
    Activity a;
    a.id         = o["id"].toString();
    a.name       = o["name"].toString();
    a.categoryId = o["categoryId"].toString();
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

static QJsonObject toJson(const SpecialDay& day)
{
    return QJsonObject{
        {"id",            day.id},
        {"title",         day.title},
        {"date",          day.date.toString(Qt::ISODate)},
        {"repeatsYearly", day.repeatsYearly},
    };
}

static SpecialDay specialDayFromJson(const QJsonObject& o)
{
    SpecialDay day;
    day.id            = o["id"].toString();
    day.title         = o["title"].toString();
    day.date          = QDate::fromString(o["date"].toString(), Qt::ISODate);
    day.repeatsYearly = o["repeatsYearly"].toBool();
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
        // v4 additions. Both are optional on read (see below), so a v3
        // file with neither key loads without complaint.
        {"description", task.description},
        {"repeat",      repeatToString(task.repeat)},
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
    // A missing key returns a null QJsonValue; .toString() gives "" and
    // repeatFromString("") is Repeat::None — so pre-v4 tasks read exactly
    // as "no notes, no recurrence" with zero special-casing. THIS is what
    // "additive growth" buys: no migration branch, just sane defaults.
    task.description = o["description"].toString();
    task.repeat      = repeatFromString(o["repeat"].toString());
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

bool JsonStore::migrateLegacyData()
{
    const QString newPath = defaultFilePath(); // also ensures the new dir
    if (QFile::exists(newPath))
        return false; // the new home is already lived-in — never overwrite

    // Both names live under the same parent (…/Roaming on Windows,
    // ~/.local/share on Linux), so the legacy path is "one folder over":
    // derive it from the new location instead of hard-coding an OS path.
    QDir parent(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    parent.cdUp();
    const QString legacyPath =
        parent.filePath(QStringLiteral("TimeFocusTracker/data.json"));
    if (!QFile::exists(legacyPath))
        return false; // genuinely a first run — nothing to migrate

    // COPY, not move: the old file stays behind as a free backup. If the
    // migration ever went wrong, nothing was destroyed.
    return QFile::copy(legacyPath, newPath);
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

    QJsonObject root{
        // The version number planted on day one, earning its keep: the
        // format grew a "tasks" array, so 1 becomes 2. The change is
        // additive (old files still load — see the loader), but bumping
        // costs nothing and lets any future reader that must care tell
        // the files apart.
        {"version",     6}, // v6: + Event.taskId / Event.title (additive, tolerant read)
        {"categories",  categories},
        {"activities",  activities},
        {"events",      events},
        {"tasks",       tasks},
        {"folders",     folders},
        {"specialDays", specialDays},
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
