#include "MainWindow.h"

#include "ActivitiesPage.h"
#include "ArchivePage.h"
#include "ChatPage.h"
#include "PlannerPage.h"
#include "TaskDetailPanel.h"
#include "QuickCaptureOverlay.h"
#include "SpecialDaysPage.h"
#include "UpcomingPage.h"
#include "BlockAlarmService.h"
#include "DebugPanel.h"
#include "AffordabilityService.h"
#include "CheckIn.h"
#include "CheckInService.h"
#include "ChatSession.h"
#include "MemoryStore.h"
#include "PomodoroEngine.h"
#include "PomodoroLink.h"
#include "PomodoroPage.h"
#include "Prefs.h"
#include "Theme.h"
#include "Widgets.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QMoveEvent>
#include <QPainter>
#include <QRect>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>

#include <optional>
#include <QSystemTrayIcon>
#ifdef TICKTIMER_HAS_MULTIMEDIA
#include <QSoundEffect>
#elif defined(Q_OS_WIN)
// Tier-2 sound (v19.9): Windows' own waveform API. Always present on
// Windows — no Qt module, no extra DLLs — so a Qt kit installed without
// Multimedia (the owner's, it turned out) still plays the real chimes.
#include <windows.h>
#include <mmsystem.h>
#endif
#include "NotificationToast.h"
#include <QFile>
#include <functional>
#include <QHBoxLayout>
#include <QSettings>
#include <QKeySequence>
#include <QShortcut>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "SettingsDialog.h"
#include "ShareClient.h"
#include "SharingDialog.h"
#include "AuthClient.h"
#include "SessionStore.h"
#include "LoginDialog.h"
#include "SyncClient.h"
#include "UpdateBanner.h"
#include "UpdateClient.h"
#include "Version.h"
#include "SyncDialog.h"
#include "SyncService.h"

namespace
{
// The size the window opens at when it has no memory of a previous session.
// Named because it is now used TWICE — first run, and the rescue path when a
// remembered rectangle turns out to be unreachable. A magic number used once
// is a value; used twice it is a decision, and decisions get names.
constexpr int kDefaultWidth  = 1150;
constexpr int kDefaultHeight = 780;

// Would a window at this rectangle be reachable on the screens attached
// RIGHT NOW? Two lines, because the interesting half — what "reachable"
// means — lives in Widgets.h as a pure function over rectangles so the tests
// can hand it monitor layouts nobody here owns. This is the impure adapter:
// it knows about today, and nothing else.
bool isReachable(const QRect& frame)
{
    return overlapsAnyScreen(frame, availableScreenRects());
}
} // namespace

MainWindow::MainWindow(const QString& username)
    // Per-account file when logged in; the global data.json when not. This
    // one line is what makes two accounts on one machine keep separate local
    // planners.
    : m_username(username)
    , m_store(JsonStore::filePathForUser(username))
    , m_tracker(&m_data)
{
    setWindowTitle(tr("TickTimer"));
    // The FALLBACK size. restoreWindowState() runs at the end of this
    // constructor and may replace it; setting it here first means every path
    // out of that function — no memory, rejected blob, unreachable screen —
    // lands on a sane window without needing to say so.
    resize(kDefaultWidth, kDefaultHeight);

    // ---- THE autosave line (wired FIRST, before any data exists) ----------
    // Every changed() — one keystroke in a note, one committed segment,
    // one heartbeat — flows through here to the disk. QSaveFile makes each
    // write atomic, so there is never a moment when the file is half-written.
    // Order matters: connect BEFORE loading/seeding, so even the first-run
    // seed categories are saved the instant they're created. (Loading
    // itself doesn't re-trigger a save — resetFrom deliberately stays
    // silent, exactly for this wiring.)
    connect(&m_data, &AppData::changed, this, [this]() {
        if (!m_store.save(m_data))
            statusBar()->showMessage(
                tr("Save failed: %1").arg(m_store.errorMessage()));
    });

    // ---- STARTUP SEQUENCE (order is the design) ---------------------------
    // 1. Load everything from disk (design-doc §4: load-all on startup).
    // 2. If a RunningState survived — the app died while tracking — turn
    //    it into a real Segment and tell the user what was rescued.
    // 3. First run (no file): seed the starter categories instead.
    // 0a. The old app-rename bridge (TimeFocusTracker → TickTimer).
    const bool migrated = JsonStore::migrateLegacyData();
    // 0b. The accounts adoption: if THIS user has no file yet but the global
    //     data.json exists, it becomes this account's data (your old planner
    //     transfers to your account, once). Runs before load so the very
    //     next line reads the adopted file.
    const bool adopted =
        JsonStore::adoptGlobalDataForUser(m_username);

    QString recoveryMessage;
    if (m_store.load(m_data))
        recoveryMessage = m_data.recoverInterruptedTracking();
    else
        m_data.seedDefaults();

    // ---- chrome: header, left nav, page stack ------------------------------
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QWidget(central);
    header->setStyleSheet(
        "background: white; border-bottom: 1px solid #E2E6E0;");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 14, 24, 14);

    // U+2261 "identical to" as the hamburger glyph — deliberately boring.
    // The prettier U+2630 trigram is missing from many fonts and renders
    // as an empty button (caught in testing): Unicode-as-icon is a font
    // lottery, so either ship a real QIcon or pick a glyph every font has.
    auto* navToggle = new QPushButton(QStringLiteral("\u2261"), header);
    navToggle->setFixedSize(34, 34);
    navToggle->setCursor(Qt::PointingHandCursor);
    navToggle->setToolTip(tr("Show or hide the sidebar (Ctrl+B)"));
    // A shortcut on the button itself: while the window is active,
    // Ctrl+B presses this button — no extra wiring, and the tooltip
    // advertises it (a shortcut nobody can discover doesn't exist).
    navToggle->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    headerLayout->addWidget(navToggle);
    headerLayout->addSpacing(14);

    auto* brand = new QLabel(tr("TickTimer"), header);
    brand->setObjectName("h1");
    brand->setStyleSheet("border: none;");
    auto* tag = new QLabel(tr("plan vs. actual — see where your time goes"),
                           header);
    tag->setObjectName("sub");
    tag->setStyleSheet("border: none; color:#616974;");
    headerLayout->addWidget(brand);
    headerLayout->addSpacing(10);
    headerLayout->addWidget(tag);
    // A QLabel's minimum width IS its text width — this tagline alone is
    // wider than half a phone screen and would force the whole window past
    // it. On compact screens the brand stays, the slogan yields.
    if (isCompactScreen())
        tag->hide();
    headerLayout->addStretch(1);

    // ---- global quick-add (v21.1): one shortcut, from anywhere -------------
    // The overlay is built once and summoned on demand; the header button is
    // the discoverable twin of the shortcut (a shortcut nobody finds is a
    // feature nobody has).
    m_capture = new QuickCaptureOverlay(&m_data, this);
    {
        QSettings settings;
        m_capture->setDefaultCategoryId(
            settings.value(QStringLiteral("capture/lastCategoryId")).toString());
    }
    connect(m_capture, &QuickCaptureOverlay::taskCaptured, this,
            [this](const QString& catId) {
                // "Capture memory": the next summon defaults to wherever you
                // threw the last one — most brain-dumps go to the same bucket.
                QSettings settings;
                settings.setValue(QStringLiteral("capture/lastCategoryId"),
                                  catId);
                m_capture->setDefaultCategoryId(catId);
            });
    auto* captureBtn = new QPushButton(tr("+ Capture"), header);
    captureBtn->setToolTip(tr("Quick-add a task from anywhere (Ctrl+N)"));
    captureBtn->setCursor(Qt::PointingHandCursor);
    captureBtn->setStyleSheet(
        "background:#EEF2F0; border:none; border-radius:8px; padding:6px 12px; "
        "color:#2F7E6E; font-weight:600;");
    connect(captureBtn, &QPushButton::clicked, this,
            [this]() { m_capture->popup(); });
    headerLayout->addWidget(captureBtn);
    headerLayout->addSpacing(10);
    // ApplicationShortcut: fires with focus on ANY page — that is the point.
    auto* captureShortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this);
    captureShortcut->setContext(Qt::ApplicationShortcut);
    connect(captureShortcut, &QShortcut::activated, this,
            [this]() { m_capture->popup(); });

    // WHOSE session this is, said out loud (owner request): two accounts on
    // one machine means two planners, and a header that names the account
    // kills the "am I about to plan in mom's calendar?" doubt before it
    // forms. Empty username (tests, tools, the legacy no-login path) —
    // no label at all: greeting a nameless session with "Welcome, " would
    // just advertise the plumbing.
    if (!m_username.isEmpty()) {
        auto* welcome = new QLabel(tr("Welcome, %1").arg(m_username), header);
        welcome->setObjectName("sub");
        welcome->setStyleSheet("border: none; color:#616974;");
        headerLayout->addWidget(welcome);
    }

    auto* body = new QWidget(central);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto* nav = new QWidget(body);
    nav->setFixedWidth(190);
    nav->setStyleSheet("border-right: 1px solid #E2E6E0;");
    // Visibility is NOT decided here any more (v23). It used to be one line —
    // "hidden on compact screens" — but the moment the rail became something
    // the user's choice can outlive a launch, that one line became two rules
    // that must agree (the remembered choice, and the screen's default when
    // there is no choice yet). Two rules in two places drift; both now live
    // in restoreWindowState(), and this is just the widget.
    m_nav = nav;
    auto* navLayout = new QVBoxLayout(nav);
    navLayout->setContentsMargins(12, 18, 12, 18);
    navLayout->setSpacing(4);
    m_navLayout = navLayout; // enableSync() appends the Sync button later

    m_pages = new QStackedWidget(body);
    m_planner = new PlannerPage(&m_data, &m_tracker, m_pages);
    m_pages->addWidget(m_planner);
    m_pages->addWidget(new UpcomingPage(&m_data, m_pages));
    m_pages->addWidget(new ActivitiesPage(&m_data, m_pages));
    m_pages->addWidget(new SpecialDaysPage(&m_data, m_pages));
    // The Pomodoro machine is app-lifetime state, exactly like m_tracker
    // (the page is just its biggest face). Built here so the link adapter
    // can hold both services, and so notifications outlive any page.
    m_pomodoro     = new PomodoroEngine(this);
    m_pomodoroLink = new PomodoroLink(m_pomodoro, &m_tracker, this);
    m_pages->addWidget(new PomodoroPage(m_pomodoro, m_pomodoroLink,
                                        &m_tracker, &m_data, m_pages));
    m_blockAlarm = new BlockAlarmService(&m_data, this);

    // v28.0 — the proactive heads-up, with NO model in it. The whole
    // pipeline (trigger → verdict → manners → sentence) already works;
    // 28.1 swaps only the sentence-writer. The service sends finished
    // text, unlike BlockAlarm's ids-then-resolve, because the sentence is
    // BUILT from a Report computed at sweep time — re-deriving it at toast
    // time would just run the same query twice for the same instant.
    m_afford = new AffordabilityService(&m_data, this);
    // One assistant, one voice: the nudge speaks in whatever persona the
    // chat is set to. Injected (see the service header for why).
    m_afford->setPersonaProvider(
        [] { return chat::configuredPersonaBand(); });
    connect(m_afford, &AffordabilityService::nudge, this,
            [](const QString& title, const QString& body,
               const QString& /*taskId — the §G.1 action's seam, unused
                                until the check-in gives tapping a toast
                                somewhere to GO*/) {
                ToastSpec spec;
                spec.title = title;
                spec.body  = body;
                spec.kind  = ToastSpec::Kind::Alert; // danger-red accent
                spec.msecs = 9000; // longer than a block alarm: this one
                                   // carries numbers worth actually reading
                NotificationToast::show(spec);
            });

    // v28.2p2 — the morning knock. The toast's action row meets the
    // consumer it was built for (v28.0's seam, §G.1's shape): notification
    // → tap → the Assistant opens with the check-in waiting. Index 6 by
    // IDENTITY, per the stack's own comment above.
    m_checkIn = new CheckInService(&m_data, this);
    connect(m_checkIn, &CheckInService::offer, this,
            [this](const QString& title, const QString& body) {
                ToastSpec spec;
                spec.title      = title;
                spec.body       = body;
                spec.kind       = ToastSpec::Kind::Info; // an invitation,
                                                         // not an alarm
                spec.msecs      = 12000; // mornings are slow; so is this
                spec.actionText = tr("Check in");
                spec.onAction   = [this]() {
                    showPage(6);
                    m_chat->beginCheckIn();
                };
                NotificationToast::show(spec);
            });

    setupNotifications();

    // Recurring blocks roll forward HERE (v19.10) — at startup, and again
    // at each midnight for sessions that outlive the day. The roll is a
    // domain door (AppData::rollRepeats); MainWindow only owns the
    // CALENDAR of when to knock, the same division as the alarm service.
    m_data.rollRepeats(QDate::currentDate());
    // Lapsed dismissals get tidied on the same knocks (needs-a-block §C).
    // Housekeeping, not correctness: the flag rule compares dismissedUntil
    // against `now` itself, so nothing depends on this having run — it
    // just keeps the stored data from carrying dead timestamps around.
    m_data.expireDismissals(QDateTime::currentDateTime());
    m_data.trimMoods(QDate::currentDate(),
                     checkin::Rule().retentionDays); // §G.2: forgetting is
                                                     // a domain promise
    const auto armMidnightRoll = [this](auto&& self) -> void {
        const QDateTime nextMidnight(QDate::currentDate().addDays(1),
                                     QTime(0, 0, 5)); // +5 s: land safely
                                                      // PAST the boundary
        QTimer::singleShot(
            QDateTime::currentDateTime().msecsTo(nextMidnight), this,
            [this, self]() {
                m_data.rollRepeats(QDate::currentDate());
                m_data.expireDismissals(QDateTime::currentDateTime());
    m_data.trimMoods(QDate::currentDate(),
                     checkin::Rule().retentionDays); // §G.2: forgetting is
                                                     // a domain promise
                self(self); // re-arm for the following midnight
            });
    };
    armMidnightRoll(armMidnightRoll);
    m_pages->addWidget(new ArchivePage(&m_data, m_pages)); // index 5 — the quiet room
    // index 6 — the assistant (v25). Appended LAST although its nav button
    // sits above Archive's, because the stack index is an identity other code
    // already relies on (showPage(5) is Archive in the screenshot tool and in
    // every note that mentions it). Inserting in visual order would have
    // renumbered a page behind an unrelated feature's back — the classic
    // "index means position" trap. Visual order is the rail's business;
    // identity is the stack's.
    m_chat = new ChatPage(&m_data, m_pages);
    m_pages->addWidget(m_chat);

    const char* navNames[] = {"Calendar", "Upcoming", "Activities",
                              "Special days", "Pomodoro"};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton(nav);
        b->setObjectName("nav");
        b->setText(tr(navNames[i]));
        b->setCheckable(true);
        b->setChecked(i == 0);
        b->setAutoExclusive(true); // radio behaviour among siblings
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this,
                [this, i]() { m_pages->setCurrentIndex(i); });
        m_navButtons.append(b);
        navLayout->addWidget(b);
    }

    // The assistant is a DESTINATION (checkable, autoExclusive, above the
    // stretch) — same class of thing as Calendar or Pomodoro, not furniture
    // like Settings. Built outside the loop because its page index (6) is
    // deliberately not its position in the rail; see the addWidget above.
    auto* assistantBtn = new QToolButton(nav);
    assistantBtn->setObjectName("nav");
    assistantBtn->setText(tr("✦  Assistant"));
    assistantBtn->setCheckable(true);
    assistantBtn->setAutoExclusive(true);
    assistantBtn->setCursor(Qt::PointingHandCursor);
    assistantBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(assistantBtn, &QToolButton::clicked, this,
            [this]() { m_pages->setCurrentIndex(6); });
    navLayout->addWidget(assistantBtn);
    // NOT appended to m_navButtons here — see below. The layout decides where
    // the button SITS; m_navButtons decides which button a page index lights
    // up, and those two orders are now deliberately different.

    navLayout->addStretch(1);

    // Archive sits BELOW the stretch, with Sync/Share: furniture, not a
    // destination (items 3–5). It's checkable + autoExclusive like its
    // siblings so the rail's radio behaviour keeps working when you're in it.
    auto* archiveBtn = new QToolButton(nav);
    archiveBtn->setObjectName("nav");
    archiveBtn->setText(tr("🗄  Archive"));
    archiveBtn->setCheckable(true);
    archiveBtn->setAutoExclusive(true);
    archiveBtn->setCursor(Qt::PointingHandCursor);
    archiveBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(archiveBtn, &QToolButton::clicked, this,
            [this]() { m_pages->setCurrentIndex(5); });
    // m_navButtons is indexed BY PAGE INDEX (showPage(i) checks
    // m_navButtons[i]), so Archive (page 5) must be appended before the
    // Assistant (page 6) even though the Assistant sits higher in the rail.
    // Appending in visual order instead would make showPage(5) highlight the
    // Assistant while showing the Archive — a silent off-by-one that no
    // compiler can catch, because both are QToolButton*.
    m_navButtons.append(archiveBtn);
    m_navButtons.append(assistantBtn);
    navLayout->addWidget(archiveBtn);

    // Settings is furniture too — but NOT checkable: it opens a dialog and
    // you come straight back, so it must never steal the rail's "where am
    // I" highlight from the page you're actually on. (Archive above is a
    // real destination; that's the whole difference in three property
    // calls.) Unlike Sync/Share it is NOT gated on a session — hours and
    // week start are this machine's taste, meaningful before any login.
    auto* settingsBtn = new QToolButton(nav);
    settingsBtn->setObjectName("nav");
    settingsBtn->setText(tr("⚙  Settings"));
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(settingsBtn, &QToolButton::clicked, this, [this]() {
        SettingsDialog dialog(this, MemoryStore::pathForUser(m_username));
        if (dialog.exec() != QDialog::Accepted)
            return;
        // The dialog wrote QSettings; now the pages re-read and re-tell
        // their widgets. Pull, not push: the dialog stays ignorant of who
        // listens, and adding a future pref-consumer touches only this
        // line's callee, never the dialog.
        m_planner->applyDisplayPrefs();
    });
    navLayout->addWidget(settingsBtn);

    bodyLayout->addWidget(nav);
    bodyLayout->addWidget(m_pages, 1);
    // v28.6.1 — the task-detail OVERLAY. Deliberately NOT in bodyLayout:
    // the v28.6.0 docked version squeezed the pages and read as part of
    // the Activities panel (owner feedback, first session). As an overlay
    // child of `body` it floats IN FRONT — scrim dims the content, drawer
    // slides over it, click-away closes. Found by runTaskDetail via
    // findChild from window(), so this line is still the panel's entire
    // integration.
    new TaskDetailPanel(&m_data, body);

    // The whole show/hide feature is ONE call: hiding a widget releases
    // its space and the QHBoxLayout hands it to the pages automatically.
    // Layouts reflow; they don't reserve. (Remembering this state across
    // restarts is a QSettings exercise for later.)
    connect(navToggle, &QPushButton::clicked, this, [this]() {
        const bool showing = !m_nav->isVisible();
        m_nav->setVisible(showing);
        // Written HERE, not at close. The asymmetry with the geometry (saved
        // once, in closeEvent) is deliberate and worth naming: write
        // immediately when the USER makes a decision, batch when the WINDOW
        // MANAGER does. Pressing Ctrl+B is an intent — it deserves to survive
        // a crash, a force-quit, a pulled power cable. Dragging a window edge
        // is not; saving on every resize event would mean hundreds of
        // settings writes to record one decision, and losing the last of them
        // costs the user nothing they'd notice.
        prefs::setSidebarVisible(showing);
    });

    rootLayout->addWidget(header);
    rootLayout->addWidget(body, 1);
    setCentralWidget(central);

    QString startupNote = tr("Data file: %1").arg(m_store.filePath());
    if (adopted)
        startupNote = tr("Welcome — your existing planner was moved into "
                         "your account. ") + startupNote;
    if (migrated)
        startupNote = tr("Welcome to TickTimer — your existing data "
                         "moved in with the new name.");
    if (!recoveryMessage.isEmpty())
        startupNote = recoveryMessage; // rarer and more important: it wins
    statusBar()->showMessage(startupNote, /*ms=*/8000);

    // v28.10 — the seams, reachable (see DebugPanel.h for the why). Lazy:
    // most launches never press the chord, so most launches never pay for
    // the panel. The two lambdas ARE the composition-root's contribution —
    // which text is "the briefing" and which objects own clocks is wiring
    // knowledge, and it stays here with the rest of the wiring.
    auto* debugChord = new QShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+D")), this);
    connect(debugChord, &QShortcut::activated, this, [this]() {
        if (!m_debug) {
            m_debug = new DebugPanel(
                m_afford, m_checkIn,
                [this] { return m_chat->currentBriefing(); },
                [this](const std::optional<QDateTime>& fake) {
                    // One rewire, every clock: the panel promises "the app
                    // judges this moment", so a service left on the wall
                    // clock here would make the panel a liar.
                    std::function<QDateTime()> now =
                        fake ? std::function<QDateTime()>(
                                   [t = *fake] { return t; })
                             : std::function<QDateTime()>([] {
                                   return QDateTime::currentDateTime();
                               });
                    m_afford->setNowProvider(now);
                    m_checkIn->setNowProvider(now);
                    m_chat->nowProvider = now;
                },
                [this]() -> QString {
                    // The panel plays the model (v29.0). Refresh the
                    // handle world first: a proposal must speak THIS
                    // turn's names or die in validate() — which is
                    // itself worth demonstrating, but not by default.
                    m_chat->currentBriefing();
                    const verbs::HandleMap& handles = m_chat->handles();
                    for (const Task& t : m_data.tasks()) {
                        if (t.done || t.archived || t.estimateMinutes != 0)
                            continue;
                        const int at = handles.taskIds.indexOf(t.id);
                        if (at < 0)
                            continue; // beyond the briefing's cap this turn
                        verbs::Proposal p;
                        p.targetHandle =
                            QStringLiteral("T%1").arg(at + 1);
                        p.estimateMinutes = 90; // a visibly sample-ish guess
                        if (!t.dueDate.isValid())
                            p.dueDate = QDate::currentDate().addDays(2);
                        m_chat->presentProposal(p, verbs::Role::Intake);
                        return tr("Proposed for '%1' — the card is waiting "
                                  "in the Assistant chat.")
                            .arg(t.title);
                    }
                    return tr("No unsized open task in this turn's "
                              "briefing — capture one first (Ctrl+N).");
                },
                [this]() -> QString {
                    // v29.1 — the interview, from the panel. beginIntake
                    // refreshes its own handle world and triages; the
                    // status line reports which way it went.
                    return m_chat->beginIntake()
                               ? tr("Interview started — the question is "
                                    "waiting in the Assistant chat.")
                               : tr("Nothing interview-worthy right now "
                                    "(everything is sized, skipped, or "
                                    "the queue is empty).");
                },
                this);
        }
        m_debug->show();
        m_debug->raise();
        m_debug->activateWindow();
    });

    // v29.0 — §B's cheap insurance, wired where the file path lives. A
    // rolling single copy: the state BEFORE the most recent Apply, not an
    // archive (archives are what version control is for). No UI, no
    // button — a file that exists if it is ever needed.
    m_chat->preApplyHook = [this]() {
        const QString src = m_store.filePath();
        const QString dst = src + QStringLiteral(".pre-apply");
        QFile::remove(dst);
        QFile::copy(src, dst);
    };

    // v30.0 — the memory band (§L), wired here for the same reason the hook
    // above is: this is where the account, and therefore the file path, is
    // known. Login scopes memory exactly as it scopes the planner, so the
    // two paths are chosen from the same username and can never drift.
    //
    // Read on every turn rather than cached at startup. The file is meant to
    // be opened and corrected by hand, so an edit must take effect on the
    // next question, not the next launch.
    m_chat->memoryBandProvider = [this]() {
        return memory::promptBand(
            MemoryStore(MemoryStore::pathForUser(m_username)).load());
    };

    // LAST, on purpose. Everything above adds widgets, and adding widgets
    // changes the window's size hint — restore the remembered rectangle
    // before the chrome is built and the layout gets a vote it shouldn't
    // have. Also before show(): restoring geometry to an already-visible
    // window is a visible jump.
    restoreWindowState();
    // Only NOW may the debounce save. Set here rather than inside
    // restoreWindowState(): that function has three early returns, and a
    // flag that must be set on every path out of a function belongs after
    // the call, not copy-pasted before each return.
    m_windowStateRestored = true;
}

void MainWindow::restoreWindowState()
{
    // ---- the rail ---------------------------------------------------------
    // The remembered choice, falling back to what this SCREEN wants when
    // there is no memory yet: on a phone-sized display the 190px rail would
    // eat half the width, so it starts folded there. Because QSettings is
    // per-machine, a compact device gets the compact default on its first run
    // and its OWN choice ever after — a laptop's preference can never leak
    // onto a phone.
    m_nav->setVisible(prefs::sidebarVisible(!isCompactScreen()));

    // ---- the window -------------------------------------------------------
    // Compact screens take whatever the window manager hands them (on Android
    // the window is the screen). Restoring a 1150x780 desktop rectangle onto
    // a 5-inch display isn't a preference, it's a bug — so we don't save or
    // restore geometry there at all. Note this is a NON-SYMMETRIC skip that
    // saveWindowState() must match; that's exactly why the two functions are
    // declared side by side in the header.
    if (isCompactScreen())
        return;

    const QByteArray blob = prefs::windowGeometry();
    // Two failures, one response. Empty = first run on this machine. False =
    // Qt looked at the bytes and refused them (wrong version, corrupt, hand-
    // edited). Neither is recoverable and neither is worth telling the user
    // about; both mean "no memory", and the default size set in the
    // constructor already stands.
    if (blob.isEmpty() || !restoreGeometry(blob))
        return;

    // The monitor check restoreGeometry() cannot do for us. See isReachable().
    if (!isReachable(frameGeometry())) {
        // Drop any maximized/full-screen flag the blob carried too: it was
        // maximized ON A SCREEN THAT'S GONE, and re-applying it here would
        // maximize onto whatever screen we land on — a second surprise while
        // recovering from the first.
        setWindowState(Qt::WindowNoState);
        resize(kDefaultWidth, kDefaultHeight);
        if (const QScreen* primary = QGuiApplication::primaryScreen())
            move(primary->availableGeometry().center() - rect().center());
    }
}

void MainWindow::saveWindowState()
{
    // The rail is already written on every toggle (see the ☰ handler); this
    // is the belt-and-braces pass, and it also covers any future code path
    // that hides the rail without going through the button.
    prefs::setSidebarVisible(m_nav->isVisible());

    // Mirror of the restore-side skip. saveGeometry() on a compact device
    // would store a phone-shaped rectangle that a later desktop session on
    // the same settings file would happily restore.
    if (!isCompactScreen())
        prefs::setWindowGeometry(saveGeometry());
}

// ---- v23.1: the debounced save ---------------------------------------------
// v23.0 saved geometry in closeEvent only, and the tradeoff table said the
// cost was acceptable: "a hard crash loses the window geometry." That shipped
// at midnight and was wrong by morning, because "a hard crash" quietly
// included "the developer pressing Qt Creator's Stop button" — which is how
// the person testing the feature ends the app dozens of times a day. A save
// hook that a NORMAL workflow skips isn't a tradeoff, it's a bug with a
// justification attached.
//
// The fix keeps the original objection intact. The objection was never
// "don't save outside close" — it was "a drag is a hundred resizeEvents
// describing one decision, and one decision deserves one write." So: every
// geometry-changing event restarts a 1-second single-shot timer, and only
// silence fires it. Drag for ten seconds, get one write, one second after
// you let go. The window's memory is now at most one second stale no matter
// how the process dies; closeEvent's save remains as the final word for the
// (common) case where the app outlives its last resize by hours.

void MainWindow::scheduleWindowStateSave()
{
    // The gate (see the header): during construction and restore, these
    // events describe the DEFAULT rectangle, and saving that would erase the
    // real memory on every startup.
    if (!m_windowStateRestored || !isVisible())
        return;
    // A minimized window's rectangle is nobody's decision — Windows parks it
    // wherever it likes. Skip; the un-minimize fires changeEvent and catches
    // the real geometry a second later.
    if (isMinimized())
        return;
    if (!m_saveWindowTimer) {
        m_saveWindowTimer = new QTimer(this);
        m_saveWindowTimer->setSingleShot(true);
        m_saveWindowTimer->setInterval(1000);
        connect(m_saveWindowTimer, &QTimer::timeout, this,
                [this]() { saveWindowState(); });
    }
    m_saveWindowTimer->start(); // start() on a running timer RESTARTS it —
                                // that one Qt fact is the entire debounce
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    scheduleWindowStateSave();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    scheduleWindowStateSave();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    // Maximize/restore/full-screen arrive HERE, not as a move or resize —
    // this is the event the maximized flag lives in, and the reason the trio
    // has three members instead of the two everyone remembers.
    if (event->type() == QEvent::WindowStateChange)
        scheduleWindowStateSave();
}

void MainWindow::setupNotifications()
{
    // The notifier lives HERE, not in the engine (which must stay liftable
    // and UI-free) and not in the page (which may be hidden — being hidden
    // is the whole scenario a notification exists for). It now serves TWO
    // clients — the Pomodoro's phase ends and the agenda's block starts —
    // through one tray icon and one house style of toast.
    //
    // A tray icon is required plumbing: Qt routes desktop balloons through
    // QSystemTrayIcon::showMessage, and most platforms only deliver them
    // for a VISIBLE tray icon. So TickTimer now has a permanent tray
    // presence — a small cost that buys a native notification path with
    // zero new Qt modules. The icon itself is painted, not shipped: a
    // 64-px disc in the app's focus green with a white minute-hand tick —
    // no resource file, identical on every platform.
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return; // headless/tests/odd desktops: silently no balloons

    // Chimes in three tiers (v19.9 — the owner's build fell through the
    // v19.8 version silently: their Qt kit has no Multimedia module, so
    // the ifdef quietly took the beep. The lesson is the fix: degrade in
    // STEPS, and never past a step the platform is known to have):
    //   1. QSoundEffect  — any platform, when Qt Multimedia exists;
    //   2. winmm PlaySound — Windows' own API, ALWAYS present there: the
    //      real chime with zero extra installs (WAV bytes read from the
    //      resource and kept alive — SND_ASYNC plays from OUR buffer);
    //   3. QApplication::beep() — the floor, for platforms with neither.
#ifdef TICKTIMER_HAS_MULTIMEDIA
    const auto makeChime = [this](const QString& qrcPath) {
        auto* fx = new QSoundEffect(this);
        fx->setSource(QUrl(QStringLiteral("qrc") + qrcPath));
        fx->setVolume(0.85);
        return std::function<void()>([fx]() { fx->play(); });
    };
#elif defined(Q_OS_WIN)
    const auto makeChime = [](const QString& qrcPath) {
        // The QByteArray is captured BY VALUE into the lambda and the
        // lambda lives as long as the connection: the async player reads
        // from a buffer that provably outlives every playback.
        QFile wav(qrcPath);
        // open()'s verdict is checked (Qt 6.11 marks it nodiscard — the
        // owner's compiler flagged it, and the zero-warning policy covers
        // THEIR machine, not just the CI's): a failed open yields empty
        // bytes, and empty bytes already fall back to the beep below.
        const QByteArray bytes =
            wav.open(QIODevice::ReadOnly) ? wav.readAll() : QByteArray();
        return std::function<void()>([bytes]() {
            if (bytes.isEmpty()) {
                QApplication::beep();
                return;
            }
            PlaySoundW(reinterpret_cast<LPCWSTR>(bytes.constData()), nullptr,
                       SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        });
    };
#else
    const auto makeChime = [](const QString&) {
        return std::function<void()>([]() { QApplication::beep(); });
    };
#endif
    const auto phaseSound = makeChime(QStringLiteral(":/sounds/notify_phase.wav"));
    const auto blockSound = makeChime(QStringLiteral(":/sounds/notify_block.wav"));

    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(theme::focus());
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRect(4, 4, 56, 56));
        p.setPen(QPen(Qt::white, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPoint(32, 34), QPoint(32, 16)); // minute hand at 12
        p.drawLine(QPoint(32, 34), QPoint(44, 40)); // hour hand at ~4
    }
    m_tray = new QSystemTrayIcon(QIcon(pix), this);
    m_tray->setToolTip(tr("TickTimer"));
    m_tray->show();
    // Clicking the tray brings the window back — earns its keep the moment
    // the mini timer is floating over a maximized browser.
    connect(m_tray, &QSystemTrayIcon::activated, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });

    connect(m_pomodoro, &PomodoroEngine::phaseEnded, this,
            [this, phaseSound](PomodoroEngine::Phase, PomodoroEngine::Phase next) {
                // The preference is read AT FIRE TIME (derive-don't-store):
                // unticking the box mid-phase silences the very next toast,
                // no stale cached flag to chase.
                if (!prefs::pomodoroNotify())
                    return;
                phaseSound();
                const bool toBreak = (next != PomodoroEngine::Phase::Focus);
                const QString title = toBreak ? tr("Focus done")
                                              : tr("Break over");
                const QString body =
                    toBreak ? tr("Time for a break — %1 on the clock.")
                                  .arg(m_pomodoro->timeText())
                            : tr("Round %1 — %2 of focus. You've got this.")
                                  .arg(m_pomodoro->round())
                                  .arg(m_pomodoro->timeText());
                NotificationToast::show(title, body);
            });

    // ---- client 2: "your planned block is starting" -------------------------
    connect(m_blockAlarm, &BlockAlarmService::blocksStarting, this,
            [this, blockSound](const QVector<QString>& eventIds) {
                if (!prefs::blockStartNotify()) // fire-time read, as always
                    return;

                // Compose a line per block — resolved FRESH from AppData at
                // toast time (the service sent ids, not text, for exactly
                // this reason: titles may have been edited since).
                QStringList lines;
                for (const QString& id : eventIds) {
                    const Event* e = m_data.eventById(id);
                    if (!e)
                        continue; // deleted between alarm and toast: skip
                    // Already tracking it? You clearly know — the own-hands
                    // rule's cousin: never announce what the user is
                    // actively doing.
                    if (m_tracker.isTrackingEvent(id))
                        continue;

                    // The block's name, by its identity (block-labels
                    // addendum): activity occurrence, task block, or ad-hoc
                    // title — first one that exists wins.
                    QString name = e->title;
                    if (const Activity* a = m_data.activityById(e->activityId))
                        name = a->name;
                    else if (const Task* t = m_data.taskById(e->taskId))
                        name = t->title;
                    if (name.isEmpty())
                        name = tr("(untitled block)");

                    lines << tr("%1 · %2 – %3")
                                 .arg(name,
                                      timeLabel(e->plannedStartMinutes),
                                      timeLabel(e->plannedEndMinutes));
                }
                if (lines.isEmpty())
                    return;

                blockSound();
                // Our own card, not showMessage (v19.9): the tray API only
                // SUBMITS a balloon to the OS pipeline, and Windows may
                // decline in silence (Focus Assist, per-app settings) —
                // which is precisely how the owner's 12:00 went unheard.
                // The tray icon itself stays: presence + click-to-raise.
                NotificationToast::show(
                    lines.size() == 1 ? tr("Starting now")
                                      : tr("%1 blocks starting now")
                                            .arg(lines.size()),
                    lines.join(QStringLiteral("\n")));
            });

    // ---- client 3: "that block is finished" (v19.8) -------------------------
    // Fires from the tracker's own exit door, so it is exact: the toast
    // lands within one tick of the boundary, names the block, and — when
    // the link was driving — arrives alongside the Pomodoro's pause.
    connect(&m_tracker, &TrackerService::trackedBlockEnded, this,
            [this, blockSound](const QString& eventId) {
                if (!prefs::blockStartNotify()) // one agenda voice, one pref
                    return;
                QString name = tr("Planned block");
                if (const Event* e = m_data.eventById(eventId))
                    name = m_data.eventLabel(*e);
                blockSound();
                NotificationToast::show(
                    tr("%1 finished").arg(name),
                    m_pomodoroLink->isEnabled()
                        ? tr("Its planned time is up — tracking stopped and "
                             "the Pomodoro is paused.")
                        : tr("Its planned time is up — tracking stopped."));
            });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Clean shutdown: stop() commits any live interval as a Segment and
    // clears the crash insurance — both of which emit changed() and hence
    // already saved. The explicit save after is belt-and-braces (it also
    // covers the nothing-was-running case where stop() is a no-op).
    m_tracker.stop();
    m_store.save(m_data);
    // The window's own memory, captured while the window still HAS geometry
    // to capture. Doing this after event->accept() would work today and
    // break the day someone adds a hide() above it; the ordering rule is
    // "read the widget before you let it go".
    saveWindowState();
    event->accept();
}

void MainWindow::showPage(int index)
{
    if (index < 0 || index >= m_pages->count())
        return;
    m_pages->setCurrentIndex(index);
    if (index < m_navButtons.size())
        m_navButtons[index]->setChecked(true); // autoExclusive unchecks the rest
}

void MainWindow::goOnline(const QString& serverUrl, const QString& token)
{
    if (m_reconnect)
        m_reconnect->stop();
    if (m_signInBtn) {
        // deleteLater, not delete: this can be reached from the button's own
        // clicked handler, and a slot must never delete its sender.
        m_signInBtn->deleteLater();
        m_signInBtn = nullptr;
    }
    statusBar()->showMessage(tr("Back online — syncing."), 5000);
    enableSync(serverUrl, token);

    // AND ACTUALLY SYNC. v30.4.4, and the second time this exact class of bug
    // has been found by using the thing: the message above said "syncing"
    // while the code merely switched the service on.
    //
    // setAutoSync(true) does catch up "with unsent edits" — but only edits it
    // KNOWS about, and while offline there was no SyncService at all, so
    // nothing marked the session's changes dirty. A service constructed after
    // the fact starts clean and pushes nothing, and the edits made offline sit
    // there looking saved.
    //
    // syncNow() is the same entry the Sync button uses: same truth table, same
    // never-auto-resolve rule. A conflict here still goes to a human.
    if (m_sync)
        m_sync->syncNow();
}

void MainWindow::addSignInButton()
{
    m_signInBtn = new QToolButton(this);
    m_signInBtn->setObjectName("nav"); // same name, same stylesheet, for free
    m_signInBtn->setText(tr("⇅  Sign in to sync"));
    m_signInBtn->setCursor(Qt::PointingHandCursor);
    m_signInBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_signInBtn, &QToolButton::clicked, this, [this]() {
        LoginDialog dialog(m_offlineServerUrl, this);
        if (dialog.exec() != QDialog::Accepted || dialog.offline())
            return; // cancelled, or chose to stay offline again

        // THE GUARD THAT MATTERS. This window is already holding
        // data-<m_username>.json in memory and saving to it. Enabling sync
        // for a DIFFERENT account would push this planner up as theirs and
        // pull theirs down over it — one tap, two planners destroyed. The
        // window cannot change identity mid-life, so a different account is
        // a refusal, not a switch.
        if (dialog.loggedInUser().compare(m_username, Qt::CaseInsensitive) != 0) {
            statusBar()->showMessage(
                tr("That's a different account. Restart TickTimer to use it — "
                   "this window is holding %1's planner.").arg(m_username),
                8000);
            return;
        }

        session::setLastUser(dialog.loggedInUser());
        if (!dialog.deviceToken().isEmpty())
            session::setDeviceToken(dialog.loggedInUser(), dialog.deviceToken());
        goOnline(dialog.serverUrl(), dialog.authToken());
    });
    m_navLayout->addWidget(m_signInBtn);
}

void MainWindow::beginOffline(const QString& serverUrl)
{
    m_offlineServerUrl = serverUrl;

    const QString token = session::deviceToken(m_username);

    // Say only what is true. v30.4.3: the first field run started offline with
    // no device token — which is the normal state for anyone upgrading from
    // before v30.2, or anyone who unticks "Remember this device" — and sat
    // there forever while the status bar promised it would sync by itself.
    // A promise the code cannot keep is worse than no promise.
    statusBar()->showMessage(
        token.isEmpty()
            ? tr("Working offline — your changes are saved here. Sign in to "
                 "sync them.")
            : tr("Working offline — your changes are saved here and will sync "
                 "when the server is back."));

    // ALWAYS offer a way back, whether or not a silent one exists. Without
    // this the only route online was restarting the app, which the offline
    // door never mentioned.
    addSignInButton();

    // Remember that this session EDITED something, so whatever sync service
    // is built later knows there is work to send. Nothing else is watching:
    // an offline session has no SyncService, so its changes are invisible to
    // the one constructed afterwards. Persisted, because "offline session"
    // very often ends with closing the app.
    connect(&m_data, &AppData::changed, this, [this]() {
        session::setOfflineEditsPending(m_username, true);
    });

    // A silent retry needs something to retry WITH. Without a remembered
    // device there is no credential to offer — but the app can still WATCH
    // for the server and say when it is worth signing in.
    //
    // v30.4.3, from the first field run: "the user won't know when the server
    // will come up." Quite right. A button that is always there and only
    // sometimes works asks a person to guess, and guessing wrong looks like
    // the app being broken.
    if (token.isEmpty()) {
        m_reconnectClient = new AuthClient(serverUrl, this);
        connect(m_reconnectClient, &AuthClient::reachable, this,
                [this](bool isReachable) {
            if (!isReachable || !m_signInBtn)
                return; // still gone, or we already went online
            m_reconnect->stop(); // it is back; no reason to keep asking
            m_signInBtn->setText(tr("⇅  Server is back — sign in"));
            statusBar()->showMessage(
                tr("The server is back. Sign in to sync your changes."));
        });

        m_reconnect = new QTimer(this);
        m_reconnect->setInterval(60 * 1000);
        connect(m_reconnect, &QTimer::timeout,
                m_reconnectClient, &AuthClient::probeReachable);
        m_reconnect->start();
        m_reconnectClient->probeReachable(); // ask once now, not in a minute
        return;
    }

    m_reconnectClient = new AuthClient(serverUrl, this);
    connect(m_reconnectClient, &AuthClient::resultReady, this,
            [this](AuthClient::Outcome outcome, const QString&,
                   const QString& sessionToken, const QString&) {
        if (outcome == AuthClient::Outcome::Success) {
            goOnline(m_offlineServerUrl, sessionToken);
            return;
        }
        if (outcome == AuthClient::Outcome::BadCredentials) {
            // The server answered and refused us. Retrying a revoked device
            // every minute forever is a slow failure nobody can see, so stop
            // and forget it — the next launch will ask for a password, which
            // is the honest thing to ask for.
            m_reconnect->stop();
            session::clearDeviceToken(m_username);
            statusBar()->showMessage(
                tr("This device is no longer signed in. Log in again to "
                   "sync."));
        }
        // Anything else (still unreachable, a puzzling reply) — say nothing
        // and let the timer come round again. An offline app that nags about
        // every failed poll is worse than one that waits quietly.
    });

    m_reconnect = new QTimer(this);
    // A minute: long enough that a phone in a pocket is not paying for it,
    // short enough that walking back into Wi-Fi feels like it just worked.
    m_reconnect->setInterval(60 * 1000);
    connect(m_reconnect, &QTimer::timeout, this, [this]() {
        m_reconnectClient->resumeSession(session::deviceToken(m_username));
    });
    m_reconnect->start();
}

void MainWindow::enableSync(const QString& serverUrl, const QString& token)
{
    // v30.2 — beginOffline()'s retry can reach this mid-session, and the
    // window may only have one sync stack: a second would mean two services
    // racing to push the same planner, and two Sync buttons in the rail.
    if (m_sync)
        return;

    m_syncClient = new SyncClient(serverUrl, token, this);
    m_sync       = new SyncService(&m_data, m_syncClient, m_username, this);

    // The button sits BELOW the rail's stretch — pinned to the bottom, away
    // from the page navigation it isn't part of. Same objectName as the nav
    // buttons so the stylesheet dresses it for free.
    auto* b = new QToolButton(this);
    b->setObjectName("nav");
    b->setText(tr("⇅  Sync"));
    b->setCursor(Qt::PointingHandCursor);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(b, &QToolButton::clicked, this, [this]() {
        SyncDialog dialog(m_sync, this);
        dialog.exec();
    });
    m_navLayout->addWidget(b);

    // v30.4.4 — work stranded by an offline session. Checked HERE rather than
    // only on the reconnect path, because an offline session often ends by
    // closing the app: the next launch logs in normally, never touches
    // goOnline(), and would otherwise leave those edits sitting on disk
    // forever, looking saved and never sent.
    //
    // Cleared only on a SUCCESSFUL sync. A failed one must leave the flag
    // standing, or one bad moment loses the work permanently.
    if (session::offlineEditsPending(m_username)) {
        connect(m_sync, &SyncService::finished, this,
                [this](bool ok, const QString&) {
            if (ok)
                session::setOfflineEditsPending(m_username, false);
        });
        QTimer::singleShot(0, m_sync, &SyncService::syncNow);
    }

    // Auto-sync on: edits push themselves 5s after a burst goes quiet.
    // The one thing autonomy must NOT hide is a conflict — that's a human
    // decision (never auto-WHO-WINS). With no dialog open, the nudge is
    // the Sync button itself turning ⚠; opening it shows the resolution
    // box (SyncDialog checks hasPendingConflict on construction).
    m_sync->setAutoSync(true);
    // The ⚠ is DERIVED from service state after every event, never toggled
    // by the events themselves — an event-toggled icon can strand (a failed
    // force-push once left ⚠ lit with no conflict behind it). Derive,
    // don't store: the same §3.5 rule, applied to one glyph.
    const auto reflectSyncState = [this, b]() {
        b->setText(m_sync->hasPendingConflict() ? tr("⚠  Sync")
                                                : tr("⇅  Sync"));
        b->setToolTip(m_sync->hasPendingConflict()
                          ? tr("A sync conflict needs your decision — "
                               "click to choose which version wins.")
                          : tr("Auto-sync is on; edits push themselves. "
                               "Click to sync manually or pull."));
    };
    connect(m_sync, &SyncService::conflictDetected, this,
            [reflectSyncState](int) { reflectSyncState(); });
    connect(m_sync, &SyncService::finished, this,
            [reflectSyncState](bool, const QString&) { reflectSyncState(); });
    reflectSyncState();

    // Share & compare rides the same capability gate as sync: both exist
    // only once a session token exists, so this is their shared front door.
    // The client is created ONCE here (it holds the token); the dialog is
    // created per click and thrown away — services persist, glass doesn't.
    m_shareClient = new ShareClient(serverUrl, token, this);
    auto* shareBtn = new QToolButton(this);
    shareBtn->setObjectName("nav");
    shareBtn->setText(tr("👥  Share"));
    shareBtn->setCursor(Qt::PointingHandCursor);
    shareBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(shareBtn, &QToolButton::clicked, this, [this]() {
        SharingDialog dialog(m_shareClient, &m_data, &m_tracker,
                             m_username, this);
        dialog.exec();
    });
    m_navLayout->addWidget(shareBtn);

    // ---- update check (networked arc part 4) ------------------------------
    // Fire-and-forget on launch, riding the same "we know where the server
    // is" moment as sync/share. The division of labour, one line each:
    // UpdateClient FETCHES, version::decideBanner JUDGES (pure — the non-nag
    // truth table lives in the domain tests), UpdateBanner RENDERS. Every
    // outcome except "show it" is deliberate silence — a check the user
    // never asked for has no right to report its failures.
    auto* updates = new UpdateClient(serverUrl, this);
    connect(updates, &UpdateClient::checkFinished, this,
            [this](UpdateClient::Outcome outcome, const QString& latest,
                   const QString& url, const QString& notes) {
                if (outcome != UpdateClient::Outcome::Success)
                    return; // unreachable / unconfigured — invisible
                const QString dismissed =
                    QSettings()
                        .value(QStringLiteral("update/lastDismissed"))
                        .toString();
                if (version::decideBanner(version::current(), latest,
                                          dismissed)
                    != version::Banner::Show)
                    return; // up to date, or this one was waved away
                auto* banner = new UpdateBanner(latest, url, notes,
                                                centralWidget());
                // Top of the window, above the header — visible, not modal.
                if (auto* root = qobject_cast<QVBoxLayout*>(
                        centralWidget()->layout()))
                    root->insertWidget(0, banner);
            });
    updates->checkForUpdate();
}
