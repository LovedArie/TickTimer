#include "MainWindow.h"

#include "ActivitiesPage.h"
#include "ArchivePage.h"
#include "PlannerPage.h"
#include "SpecialDaysPage.h"
#include "UpcomingPage.h"
#include "PomodoroPage.h"
#include "Theme.h"
#include "Widgets.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QSettings>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "ShareClient.h"
#include "SharingDialog.h"
#include "SyncClient.h"
#include "UpdateBanner.h"
#include "UpdateClient.h"
#include "Version.h"
#include "SyncDialog.h"
#include "SyncService.h"

MainWindow::MainWindow(const QString& username)
    // Per-account file when logged in; the global data.json when not. This
    // one line is what makes two accounts on one machine keep separate local
    // planners.
    : m_username(username)
    , m_store(JsonStore::filePathForUser(username))
    , m_tracker(&m_data)
{
    setWindowTitle(tr("TickTimer"));
    resize(1150, 780);

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

    auto* body = new QWidget(central);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto* nav = new QWidget(body);
    nav->setFixedWidth(190);
    nav->setStyleSheet("border-right: 1px solid #E2E6E0;");
    // On a phone-sized screen the 190px rail would eat half the width, so it
    // STARTS hidden there — the ☰ button (below) already knows how to toggle
    // it, so compact mode costs one line, not a new navigation system.
    nav->setVisible(!isCompactScreen());
    auto* navLayout = new QVBoxLayout(nav);
    navLayout->setContentsMargins(12, 18, 12, 18);
    navLayout->setSpacing(4);
    m_navLayout = navLayout; // enableSync() appends the Sync button later

    m_pages = new QStackedWidget(body);
    m_pages->addWidget(new PlannerPage(&m_data, &m_tracker, m_pages));
    m_pages->addWidget(new UpcomingPage(&m_data, m_pages));
    m_pages->addWidget(new ActivitiesPage(&m_data, m_pages));
    m_pages->addWidget(new SpecialDaysPage(&m_data, m_pages));
    m_pages->addWidget(new PomodoroPage(m_pages));
    m_pages->addWidget(new ArchivePage(&m_data, m_pages)); // index 5 — the quiet room

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
    m_navButtons.append(archiveBtn);
    navLayout->addWidget(archiveBtn);

    bodyLayout->addWidget(nav);
    bodyLayout->addWidget(m_pages, 1);

    // The whole show/hide feature is ONE call: hiding a widget releases
    // its space and the QHBoxLayout hands it to the pages automatically.
    // Layouts reflow; they don't reserve. (Remembering this state across
    // restarts is a QSettings exercise for later.)
    connect(navToggle, &QPushButton::clicked, this,
            [nav]() { nav->setVisible(!nav->isVisible()); });

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
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Clean shutdown: stop() commits any live interval as a Segment and
    // clears the crash insurance — both of which emit changed() and hence
    // already saved. The explicit save after is belt-and-braces (it also
    // covers the nothing-was-running case where stop() is a no-op).
    m_tracker.stop();
    m_store.save(m_data);
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

void MainWindow::enableSync(const QString& serverUrl, const QString& token)
{
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
        SharingDialog dialog(m_shareClient, &m_data, &m_tracker, this);
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
