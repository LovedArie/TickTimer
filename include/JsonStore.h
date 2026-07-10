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

    // One-time bridge for the TimeFocusTracker -> TickTimer rename.
    // applicationName decides the data folder, so the rename alone would
    // strand existing data in the old folder — the app would "forget"
    // everything while the file sat intact next door. Returns true if a
    // legacy file was found and copied into the new home. General law:
    // renaming anything that touches persisted state needs a data bridge.
    static bool migrateLegacyData();

    // One-time ADOPTION: when a user's per-account file doesn't exist yet but
    // the old GLOBAL data.json does, copy the global file into the account's
    // file — so existing data transfers to whoever logs in first after the
    // accounts upgrade, instead of greeting them with an empty planner. Copy
    // (not move), and the global file is renamed to a .pre-accounts.bak
    // backup so nothing is ever destroyed. Returns true if data was adopted.
    static bool adoptGlobalDataForUser(const QString& username);

    const QString& filePath() const { return m_filePath; }

    // true  = data was loaded from an existing file
    // false = no file yet (first run — caller should seed defaults) or the
    //         file was unreadable (errorMessage() says which).
    bool load(AppData& data);

    bool save(const AppData& data);

    // ---- sync hooks (design-addendum-sync) --------------------------------
    // The SAME conversion that feeds the disk feeds the wire. load/save are
    // now thin file wrappers around these two, so a planner pushed to the
    // server is byte-for-byte the planner that would have been saved — one
    // format, two destinations, zero drift between them.
    static QJsonObject toJsonObject(const AppData& data);
    // announceChange: false at startup (no listeners yet — resetFrom),
    // true when applying a sync pull (replaceAll: every screen rebuilds).
    static bool applyJsonObject(AppData& data, const QJsonObject& root,
                                bool announceChange);

    QString errorMessage() const { return m_error; }

private:
    QString m_filePath;
    QString m_error;
};
