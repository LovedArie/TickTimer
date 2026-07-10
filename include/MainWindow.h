#pragma once
// ---------------------------------------------------------------------------
// MainWindow — the composition root: the ONE place where the app's parts
// are created and wired together.
//
// Ownership picture (worth drawing once and remembering forever):
//
//   MainWindow
//    ├── AppData        (the data, and its rules)
//    ├── JsonStore      (how the data reaches the disk)
//    ├── TrackerService (the live timer)
//    └── pages: PlannerPage / ActivitiesPage / PomodoroPage
//                └── every page gets POINTERS to the above, never copies
//
// Everything lives exactly as long as the window. Pages borrow, never own.
// When a novice asks "who deletes what?", THIS diagram is the answer —
// and most of it is Qt's parent-child tree doing the deleting.
//
// The save wiring is one line with big consequences:
//     connect(data.changed -> store.save)
// After that, no code anywhere ever calls "save" explicitly. Every legal
// mutation, wherever it came from, reaches the disk. Forgetting to save
// is now structurally impossible — the design-doc §4 strategy realized.
// ---------------------------------------------------------------------------

#include "AppData.h"
#include "JsonStore.h"
#include "TrackerService.h"

#include <QMainWindow>

class QStackedWidget;
class QToolButton;
class QVBoxLayout;
class ShareClient;
class SyncClient;
class SyncService;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Called by main() once login has produced a session token. Sync is a
    // capability the window GAINS, not a constructor concern — the window
    // builds identically with or without it, which also keeps every existing
    // test that constructs a MainWindow working untouched.
    void enableSync(const QString& serverUrl, const QString& token);

    // The logged-in username scopes local storage to this account
    // (data-<username>.json) and the sync-state keys. Defaults to empty so
    // the every existing caller — and every test that builds a bare
    // MainWindow — keeps using the legacy global file unchanged.
    explicit MainWindow(const QString& username = QString());

    // Show a page by index (0 Calendar, 1 Upcoming, 2 Activities,
    // 3 Special days, 4 Pomodoro),
    // keeping the nav highlight in sync. Exists mainly for the
    // screenshot tool — the UI itself navigates via the nav buttons.
    void showPage(int index);

protected:
    // The window's close is our shutdown hook: commit any live interval,
    // final save, then let the close proceed.
    void closeEvent(QCloseEvent* event) override;

private:
    // Declaration ORDER matters here, and it's a genuine C++ lesson:
    // members are constructed top-to-bottom and destroyed bottom-to-top.
    // TrackerService holds a pointer to AppData, so AppData is declared
    // FIRST (born first, dies last) — the tracker can never outlive the
    // data it points into.
    AppData        m_data;
    JsonStore      m_store;
    TrackerService m_tracker;

    QStackedWidget* m_pages = nullptr;
    QString       m_username; // scopes local storage + sync state
    QVector<QToolButton*> m_navButtons;
    QVBoxLayout*  m_navLayout  = nullptr; // kept so enableSync can add to it
    SyncClient*   m_syncClient  = nullptr;
    SyncService*  m_sync        = nullptr;
    ShareClient*  m_shareClient = nullptr; // share & compare (needs the token)
};
