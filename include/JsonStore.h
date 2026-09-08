#pragma once
// ---------------------------------------------------------------------------
// JsonStore — saves and loads the whole AppData as one JSON file
// (design-doc §4: load on startup, hold in memory, write back on change).
//
// SEPARATION OF CONCERNS, the architectural point of this file:
// the domain structs know NOTHING about JSON. All to/from-JSON knowledge is
// concentrated here, in the storage layer. The day we outgrow JSON and move
// to SQLite (a planned upgrade, design-doc §4), we write an SqliteStore and
// delete this file — Category, Event, AppData, and every screen stay
// untouched. Persistence is a detail; the domain is the point.
//
// WHY JSON for v1 (design-doc §4): zero setup, human-readable — you can
// open your own data file in a text editor and *see* your day, which is
// worth gold while debugging. Qt reads and writes it natively
// (QJsonDocument), so no third-party dependency.
// ---------------------------------------------------------------------------

#include <QJsonObject>
#include <QDir>
#include <QString>

class AppData;

class JsonStore
{
public:
    explicit JsonStore(QString filePath);

    // The OS-blessed place for app data (QStandardPaths). On Windows:
    // C:/Users/<you>/AppData/Roaming/TimeFocusTracker/data.json
    // On Linux:   ~/.local/share/TimeFocusTracker/data.json
    // Never hard-code such paths — every OS has its own convention.
    static QString defaultFilePath();

    // Per-account planner path (§accounts): data-<username>.json beside the
    // old global data.json. Login scopes storage so switching users switches
    // FILES — accounts no longer share one local planner. Falls back to the
    // global path when username is empty (e.g. tests, or a build with login
    // disabled), so nothing that constructs a store without a user breaks.
    static QString filePathForUser(const QString& username);
    // v31.2 — where the SYNC BASE lives: the document as both sides last
    // agreed it was. A sibling of the data file rather than a QSettings
    // value, because it is a whole planner (219 KB for the owner) and the
    // registry is no place for that. Its absence is safe by construction —
    // merge::plan with no base keeps everything (see Merge.h).
    static QString basePathForUser(const QString& username);

    // One-time bridge for the TimeFocusTracker -> TickTimer rename.
    // applicationName decides the data folder, so the rename alone would
    // strand existing data in the old folder — the app would "forget"
    // everything while the file sat intact next door. Returns true if a
    // legacy file was found and copied into the new home. General law:
    // renaming anything that touches persisted state needs a data bridge.
    static bool migrateLegacyData();
    // The copy rule alone, aimable at any two folders (exists for tests and
    // for reuse by every era's migration): copy data*.json from → to, never
    // overwrite, never move. Returns true if anything was carried over.
    static bool migrateDataFiles(const QDir& from, const QDir& to);

    // One-time ADOPTION: when a user's per-account file doesn't exist yet but
    // the old GLOBAL data.json does, copy the global file into the account's
    // file — so existing data transfers to whoever logs in first after the
    // accounts upgrade, instead of greeting them with an empty planner. Copy
    // (not move), and the global file is renamed to a .pre-accounts.bak
    // backup so nothing is ever destroyed. Returns true if data was adopted.
    static bool adoptGlobalDataForUser(const QString& username);

    const QString& filePath() const { return m_filePath; }

    // THE FORMAT FLOOR (v31, design-addendum-format-floor.md).
    //
    // The version this binary WRITES, and — the point of naming it — the
    // highest it is willing to READ. One constant for both, so the two can
    // never drift apart the way a literal in the writer and a literal in a
    // guard eventually would.
    //
    // "Additive growth" (CLAUDE.md) means a NEW binary reads an OLD file
    // safely: an unknown key reads as a default. Run that tolerance the other
    // way and it turns lethal — an OLD binary reads a NEW file just as
    // quietly, drops every key it has never heard of, and writes the wreckage
    // back. That is not hypothetical: v30.8.1 did exactly this to a format-16
    // planner and destroyed 5 schedules and 73 Event.scheduleId links without
    // one error message. A tolerant reader must not also be a confident
    // writer.
    static constexpr int kFormatVersion = 16;

    // What a load actually found. This was a bool, and the bool was a trap:
    // `false` meant BOTH "no file yet" and "could not read it", and the one
    // caller answered both with seedDefaults() — so any new failure reported
    // through it would seed starter categories and let the next autosave
    // write them over a planner it merely failed to understand. A total loss
    // where the bug being fixed managed only a partial one. Four states, so
    // the compiler makes every call site say which one it means.
    enum class LoadResult {
        Loaded,     // a file existed and its contents are now in `data`
        Empty,      // no file yet — first run; the caller should seed
        Unreadable, // a file exists but would not parse; see errorMessage()
        TooNew,     // written by a NEWER TickTimer. Nothing was applied, and
                    // this store is now isReadOnly() — see save().
    };

    LoadResult load(AppData& data);

    // True once a TooNew load has latched. Belt and braces: the refusal in
    // load() is the guard, and this is what holds if a caller ignores it.
    bool isReadOnly() const { return m_readOnly; }

    // Refuses (returning false, errorMessage() set) while isReadOnly(). The
    // file a binary could not understand is the one file it must not replace.
    bool save(const AppData& data);

    // The `version` a document declares, or 0 when it declares none (which
    // every pre-v2 file does, and which is not an error).
    static int formatVersionOf(const QJsonObject& root);

    // ---- sync hooks (design-addendum-sync) --------------------------------
    // The SAME conversion that feeds the disk feeds the wire. load/save are
    // now thin file wrappers around these two, so a planner pushed to the
    // server is byte-for-byte the planner that would have been saved — one
    // format, two destinations, zero drift between them.
    static QJsonObject toJsonObject(const AppData& data);
    // announceChange: false at startup (no listeners yet — resetFrom),
    // true when applying a sync pull (replaceAll: every screen rebuilds).
    //
    // Returns FALSE and touches nothing when the document declares a format
    // above kFormatVersion. The check lives here rather than in load()
    // because this function is the single conversion feeding both the disk
    // and the wire (see above), so one guard closes all three doors: the
    // file, a pulled sync document, and a peer's planner in Compare. A
    // too-new document arriving down the wire is the same hazard with better
    // aim. Every caller must check.
    [[nodiscard]] static bool applyJsonObject(AppData& data,
                                              const QJsonObject& root,
                                              bool announceChange);

    QString errorMessage() const { return m_error; }

private:
    QString m_filePath;
    QString m_error;
    bool    m_readOnly = false; // latched by a TooNew load; never cleared,
                                // because the file does not get younger
};
