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

#include <QMetaObject>

#include "Responsive.h"

#include <QMainWindow>

class BlockAlarmService;
class AffordabilityService;
class CheckInService;
class ActivitiesPage;
class ChatPage;
class DebugPanel;
class PlannerPage;
class PomodoroEngine;
class PomodoroLink;
class QStackedWidget;
class ResponsiveWatcher;
class QSystemTrayIcon;
class QTimer;
class QToolButton;
class QuickCaptureOverlay;
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

    // v30.2 — start with NO session, on this machine's local planner.
    //
    // The window is fully usable: everything except sync is local anyway.
    // What this adds is a quiet retry — a remembered device is offered to the
    // server on a timer, and the first time it is accepted, enableSync() runs
    // and the Sync button appears mid-session. Nobody is asked to do anything;
    // the phone that spent the morning on mobile data simply catches up when
    // it gets home.
    //
    // Safe to have never been called: a window that was never offline has no
    // timer and no retry, and enableSync() guards against running twice.
    void beginOffline(const QString& serverUrl);

private:
    // v30.4.3 — the two halves of coming back online, shared by the silent
    // retry and the button, so they cannot drift into doing it differently.
    void goOnline(const QString& serverUrl, const QString& token);
    void addSignInButton();

public:

    // The logged-in username scopes local storage to this account
    // (data-<username>.json) and the sync-state keys. Defaults to empty so
    // the every existing caller — and every test that builds a bare
    // MainWindow — keeps using the legacy global file unchanged.
    explicit MainWindow(const QString& username = QString());

    // Exists for ONE reason, and it is not resource cleanup — Qt's parent
    // tree handles that. During ~QWidget the children are destroyed one by
    // one, and the page stack emits currentChanged as it goes. That reaches
    // reflectCurrentPage(), which then touches header widgets that have
    // already been deleted: a segfault at the very end of every session, in a
    // handler that is perfectly correct while the window is alive.
    ~MainWindow() override;

    // Show a page by index (0 Calendar, 1 Upcoming, 2 Activities,
    // 3 Special days, 4 Pomodoro, 5 Archive, 6 Assistant),
    // keeping the nav highlight in sync. Exists mainly for the
    // screenshot tool — the UI itself navigates via the nav buttons.
    //
    // v25 note: 6 is the Assistant even though its rail button sits ABOVE
    // Archive's. Page index is identity (old callers keep working); rail
    // position is layout. m_navButtons is built in page-index order so this
    // function's highlight stays correct.
    void showPage(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The window's close is our shutdown hook: commit any live interval,
    // final save, then let the close proceed.
    void closeEvent(QCloseEvent* event) override;

    // v23.1 — the debounce trio. Every way a window's rectangle can change
    // arrives through one of these three events; each just pokes the same
    // debounced save. See scheduleWindowStateSave() for why this exists
    // (short version: closeEvent turned out to be a hook you can't rely on).
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setupNotifications(); // ONE tray icon; two clients: the Pomodoro's
                               // phaseEnded and the agenda's blocksStarting

    // The window's own memory (v23). A matched pair, deliberately adjacent:
    // any key one of them touches, the other must too, and putting them side
    // by side is the cheapest guard against the classic "we started saving a
    // new field and forgot to read it back" drift.
    //
    // Called at the END of the constructor and from closeEvent respectively —
    // restore AFTER the layout exists (so nothing we set gets overwritten by
    // the chrome being built), save BEFORE the window goes away.
    // ---- one truth for "which page am I on" -------------------------------
    // The highlight is DERIVED from the stack, never pushed by whoever
    // happened to fire. Two navigations (the desktop rail and the phone's
    // bottom bar) would otherwise mean four push sites that must agree; this
    // is the same "Derive, don't store" rule the Sync glyph already follows.
    void reflectCurrentPage(int index);

    // The four actions that live in the rail on a desktop and in the profile
    // menu on a phone. Extracted from their click lambdas so both surfaces
    // call ONE function each rather than duplicating a dialog invocation.
    void buildProfileMenu(class QMenu* menu);
    void openSettings();
    void openSyncDialog();
    void openSharingDialog();
    void promptSignIn();

    void applyChromeMode(responsive::Mode mode);

    // Hand back any width the window is holding beyond its screen. See the
    // clamp-up-never-down note in the .cpp; called from anything that can
    // LOWER the window's minimum.
    void refitToScreen();
    bool m_refitQueued = false; // at most one queued refit in flight
    void restoreWindowState();
    void saveWindowState();

    // v23.1: geometry now also saves itself ~1s after the window stops
    // moving/resizing, not only at close. Field lesson from v23.0, hours old:
    // write-on-close protects nothing when the process is killed instead of
    // closed — and a developer's Stop button kills. The debounce keeps the
    // original objection intact (a drag is a hundred events describing one
    // decision; we still write once per decision, just one second after it
    // instead of at quit).
    void scheduleWindowStateSave();
    QTimer* m_saveWindowTimer   = nullptr;
    // Gate: move/resize events also fire while the CONSTRUCTOR builds the
    // chrome and while restoreWindowState() itself moves the window. A save
    // scheduled then would overwrite the stored blob with the half-built
    // default — the memory erasing itself on startup. False until restore
    // has finished; nothing schedules before that.
    bool m_windowStateRestored = false;
    // Declaration ORDER matters here, and it's a genuine C++ lesson:
    // members are constructed top-to-bottom and destroyed bottom-to-top.
    // TrackerService holds a pointer to AppData, so AppData is declared
    // FIRST (born first, dies last) — the tracker can never outlive the
    // data it points into.
    AppData        m_data;
    JsonStore      m_store;
    TrackerService m_tracker;

    QStackedWidget* m_pages = nullptr;

    // Watches the PAGE STACK's width, not the window's — toggling the rail
    // changes the former by 190px without touching the latter. Parented to
    // the stack, so it needs no cleanup here.
    ResponsiveWatcher* m_responsive = nullptr;
    // ---- which SHELL this window is ---------------------------------------
    // Decided ONCE, at construction, from the device — not from the container
    // width. A desktop user who narrows a window is rearranging a desktop app;
    // they must not lose Archive, Ctrl+B or the rail because of it. See
    // docs/design-addendum-mobile-shell.md.
    bool m_phoneShell = false;

    ActivitiesPage* m_activities = nullptr; // typed: its life-area sheet
                                            // tells the FAB to step aside
    class MobileNavBar* m_mobileNav = nullptr; // phone only
    class QPushButton*  m_profileBtn = nullptr; // phone only: the way to
                                                // Settings / Sync / Share
    class QPushButton*  m_captureFab = nullptr; // phone only: the big +

    // The phone header's top-right slot. ONE button, re-labelled and re-aimed
    // per page — Glance on the calendar, the area switcher on Activities,
    // nothing elsewhere. A per-page control belongs at the top right of the
    // SCREEN, which is where a thumb reaches for it; putting it inside the
    // page's own panel buried it in the content it acts on.
    class QPushButton*  m_headerAction = nullptr;
    // Only OUR wiring, so re-aiming the button can undo exactly what it did.
    // QObject::disconnect() with no arguments severs every connection the
    // object has — including ones Qt made internally — which is a sledgehammer
    // that shows up later as a crash somewhere else entirely.
    QMetaObject::Connection m_headerActionConn;
    void placeCaptureFab();
    void updateCaptureFabVisibility();
    // Two independent reasons the + can be absent, tracked separately so
    // neither can clobber the other's decision.
    // Set the moment destruction begins. ~QWidget destroys children one by
    // one and the page stack emits currentChanged as it goes, so a handler
    // that is perfectly correct while the window lives will happily touch
    // widgets that no longer exist.
    bool m_tearingDown   = false;
    bool m_fabPageAllows = true;   // this page has no bottom-right action
    bool m_fabCovered    = false;  // a sheet is over the page

    // The user's INTENT for the rail, which is not the same as whether the
    // rail is on screen: the phone shell never shows it. Storing visibility
    // instead would persist `false` from a phone session and fold the rail on
    // the next desktop launch from the same settings file.
    bool m_railWanted = true;

    class QLabel* m_tagline = nullptr; // hidden when the content area is tight
    class QLabel* m_welcome = nullptr; // ditto — chrome yields before content
    class QHBoxLayout* m_headerLayout = nullptr; // margins tighten when narrow
    PlannerPage*  m_planner = nullptr; // typed: Settings re-applies prefs here
    PomodoroEngine* m_pomodoro     = nullptr; // app-lifetime, like m_tracker
    PomodoroLink*   m_pomodoroLink = nullptr; // engine→tracker adapter
    QSystemTrayIcon* m_tray        = nullptr; // shared by every notifier
    BlockAlarmService* m_blockAlarm = nullptr; // "your 9:00 is starting"
    AffordabilityService* m_afford = nullptr;  // "Lab 4 is looking tight" (v28)
    CheckInService* m_checkIn = nullptr;       // the morning knock (v28.2p2)
    ChatPage*       m_chat    = nullptr;       // typed: the check-in opens it
    DebugPanel*     m_debug   = nullptr;       // lazy; Ctrl+Shift+D (v28.10)
    QString       m_username; // scopes local storage + sync state
    QVector<QToolButton*> m_navButtons;
    QWidget*      m_nav        = nullptr; // the rail itself: its visibility is
                                          // remembered across launches (v23)
    QVBoxLayout*  m_navLayout  = nullptr; // kept so enableSync can add to it
    SyncClient*   m_syncClient  = nullptr;
    // v30.2 — the offline retry. Non-null only between beginOffline() and
    // the first successful resume, after which it stops and is never armed
    // again: once sync exists, keeping a login timer alive would be a second
    // thing racing to create it.
    class QToolButton* m_signInBtn       = nullptr; // offline: the way back
    class QTimer*      m_reconnect       = nullptr;
    class AuthClient*  m_reconnectClient = nullptr;
    QString            m_offlineServerUrl;
    SyncService*  m_sync        = nullptr;
    ShareClient*  m_shareClient = nullptr; // share & compare (needs the token)
    QuickCaptureOverlay* m_capture = nullptr; // Ctrl+N global quick-add (v21.1)
};
