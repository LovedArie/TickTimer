# ----------------------------------------------------------------------------
# Time & Focus Tracker — build recipe.
#
# CMake in one breath: this file doesn't build anything itself; it DESCRIBES
# the project (which sources form which programs, which libraries they need),
# and CMake generates the actual build system for whatever machine you're on.
# One recipe, every platform — which is why Qt Creator opens this file
# directly as "the project".
#
# LAYOUT (v2): headers in include/, implementations in src/, tests in tests/.
# The include/-plus-src/ split is a common C++ convention. The key line that
# makes it work is target_include_directories(... include) below: it adds
# include/ to the compiler's SEARCH PATH, so every file can simply write
#     #include "AppData.h"
# and the compiler finds it. Before this refactor, includes carried relative
# paths ("../domain/AppData.h") — which welds every file to the folder tree.
# An include directory decouples them: reorganize folders, edit ONE line here.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.21)

project(TickTimer VERSION 0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)          # the C++ we write to (design decision:
set(CMAKE_CXX_STANDARD_REQUIRED ON) # modern, but nothing newer than needed)

# Finds your installed Qt (any 6.x — built against 6.4 here, your 6.8 kit
# works unchanged). Widgets = the UI module; Test = the QTest framework.
find_package(Qt6 REQUIRED COMPONENTS Widgets Test Network)
# Sound is optional equipment (v19.8): with Qt Multimedia the app plays
# real chimes from resources; without it, Windows' own winmm plays them;
# failing both, it beeps. A missing module must cost sound, never builds.
find_package(Qt6 QUIET COMPONENTS Multimedia)

# The sound tier as an INTERFACE library (v19.9.1 — the owner's Windows
# build taught the lesson: MainWindow.cpp is compiled into THREE targets,
# and hanging winmm on just the app left test_ui.exe with an undefined
# `__imp_PlaySoundW`). An INTERFACE library owns no code; it carries
# usage requirements — link lines, compile definitions — to every target
# that links it. The rule it encodes: a dependency belongs to the SOURCE
# that needs it, so it must travel with that source into every target;
# per-target sprinkling is how one of them gets forgotten.
add_library(ticktimer_sound INTERFACE)
if(Qt6Multimedia_FOUND)
    target_link_libraries(ticktimer_sound INTERFACE Qt6::Multimedia)
    target_compile_definitions(ticktimer_sound INTERFACE TICKTIMER_HAS_MULTIMEDIA)
    message(STATUS "TickTimer sound: QSoundEffect (Qt Multimedia found)")
elseif(WIN32)
    # Tier 2: Windows' own waveform API — always present, no extra DLLs.
    target_link_libraries(ticktimer_sound INTERFACE winmm)
    message(STATUS "TickTimer sound: winmm PlaySound (Qt Multimedia NOT in this kit — chimes still play)")
else()
    message(STATUS "TickTimer sound: system beep only (no Qt Multimedia on a non-Windows platform)")
endif()

# Turns on AUTOMOC and friends: Qt classes with Q_OBJECT need generated
# meta-code (signals/slots machinery); this makes CMake run Qt's moc tool
# automatically. Forgetting this is the #1 "undefined reference to vtable"
# error every Qt beginner meets — now you know the cure before the disease.
qt_standard_project_setup()

# Headers are listed alongside .cpp files ON PURPOSE: AUTOMOC scans the
# target's sources for Q_OBJECT, and headers it never sees it never mocs.
#
# A note on what this flat layout COSTS us: the old folders (domain/,
# storage/, tracking/, ui/) made the architecture visible in the file tree.
# Now the layers live only in these lists and in the include graph. The
# test target below still ENFORCES the layering (domain must build without
# Qt Widgets); the folders merely used to make it visible. Tradeoff, chosen
# deliberately for a simpler tree at this project size.
set(DOMAIN_SOURCES
    include/Activity.h
    include/AppData.h
    include/AssistantVerbs.h
    src/AssistantVerbs.cpp
    include/ProposalScrub.h
    src/ProposalScrub.cpp
    include/Memory.h
    src/Memory.cpp
    include/MemoryStore.h
    src/MemoryStore.cpp
    include/Intake.h
    src/Intake.cpp
    include/Category.h
    include/Event.h
    include/Folder.h
    include/Ids.h
    include/JsonStore.h
    include/Segment.h
    include/SpecialDay.h
    include/Task.h
    include/TaskCoverage.h
    include/MissedBlocks.h
    include/Reschedule.h
    include/Affordability.h
    include/NudgePhrasing.h
    include/Mood.h
    include/CheckIn.h
    include/ReturnPolicy.h
    include/Stats.h
    include/DayBriefing.h
    include/TrackerService.h
    include/BlockAlarmService.h
    src/BlockAlarmService.cpp
    include/PomodoroEngine.h
    include/PomodoroLink.h
    src/PomodoroEngine.cpp
    src/PomodoroLink.cpp
    src/AppData.cpp
    src/JsonStore.cpp
    src/Stats.cpp
    src/DayBriefing.cpp
    src/TrackerService.cpp
)

set(UI_SOURCES
    include/ActivitiesPage.h
    include/AgendaWidget.h
    include/DueDateDialog.h
    include/EventDialog.h
    include/GlancePanel.h
    include/MainWindow.h
    include/PickActivityDialog.h
    include/PlannerPage.h
    include/PomodoroPage.h
    include/PomodoroMiniWindow.h
    src/PomodoroMiniWindow.cpp
    include/NotificationToast.h
    src/NotificationToast.cpp
    include/Affordability.h
    include/AffordabilityService.h
    src/AffordabilityService.cpp
    include/DebugPanel.h
    src/DebugPanel.cpp
    include/ProposalCard.h
    src/ProposalCard.cpp
    include/NudgePhrasing.h
    include/Mood.h
    include/CheckIn.h
    include/CheckInService.h
    src/CheckInService.cpp
    include/NudgeClient.h
    src/NudgeClient.cpp
    include/ReviewWidgets.h
    include/SpecialDaysPage.h
    include/TaskDetailDialog.h
    include/TaskDetailForm.h
    include/TaskDetailPanel.h
    include/QuickAddParser.h
    include/LlmProvider.h
    include/LlmQuickAdd.h
    include/LlmQuickAddClient.h
    include/ChatSession.h
    include/ChatClient.h
    include/ChatPage.h
    include/QuickAddPreview.h
    include/QuickCaptureOverlay.h
    include/TaskSnapshotModel.h
    include/CategoryTaskModel.h
    include/CategoryTaskDelegate.h
    include/TaskListModel.h
    include/TaskFilterProxy.h
    include/TaskCardDelegate.h
    include/UpcomingPage.h
    include/WeekAgendaView.h
    include/Theme.h
    include/Widgets.h
    src/ActivitiesPage.cpp
    src/AgendaWidget.cpp
    src/DueDateDialog.cpp
    src/EventDialog.cpp
    src/GlancePanel.cpp
    include/NeedsBlockCard.h
    src/NeedsBlockCard.cpp
    include/SlidePanel.h
    src/SlidePanel.cpp
    src/MainWindow.cpp
    src/PickActivityDialog.cpp
    src/PlannerPage.cpp
    src/PomodoroPage.cpp
    src/ReviewWidgets.cpp
    src/SpecialDaysPage.cpp
    src/TaskDetailDialog.cpp
    src/TaskDetailForm.cpp
    src/TaskDetailPanel.cpp
    src/QuickAddParser.cpp
    src/LlmProvider.cpp
    src/LlmQuickAdd.cpp
    src/IntakeExtract.cpp
    src/LlmQuickAddClient.cpp
    include/IntakeClient.h
    src/IntakeClient.cpp
    src/ChatSession.cpp
    src/ChatClient.cpp
    src/ChatPage.cpp
    src/QuickCaptureOverlay.cpp
    src/TaskSnapshotModel.cpp
    src/CategoryTaskModel.cpp
    src/CategoryTaskDelegate.cpp
    src/TaskListModel.cpp
    src/TaskFilterProxy.cpp
    src/TaskCardDelegate.cpp
    src/UpcomingPage.cpp
    src/WeekAgendaView.cpp
    src/AuthClient.cpp
    src/LoginDialog.cpp
    src/SyncClient.cpp
    src/SyncService.cpp
    src/SyncDialog.cpp
    src/ShareClient.cpp
    src/SharingDialog.cpp
    src/CompareDialog.cpp
    include/Prefs.h
    include/SettingsDialog.h
    src/SettingsDialog.cpp
    include/SettingsPages.h
    src/SettingsPages.cpp
    include/CatchUpCard.h
    src/CatchUpCard.cpp
    src/UpdateClient.cpp
    src/UpdateBanner.cpp
    src/ArchivePage.cpp
    include/AuthClient.h
    include/LoginDialog.h
    include/SyncClient.h
    include/SyncService.h
    include/SyncDialog.h
    include/ShareClient.h
    include/SharingDialog.h
    include/CompareDialog.h
    include/Compare.h
    include/UpdateClient.h
    include/UpdateBanner.h
    include/ArchivePage.h
    include/Version.h
)

qt_add_executable(ticktimer
    ${DOMAIN_SOURCES}
    ${UI_SOURCES}
    src/main.cpp
    # WIN32-only: the .rc compiles to the embedded icon + version metadata.
    # A list variable that's empty everywhere else, so this one line serves
    # all platforms — the app builds identically on Linux/macOS, just
    # without a Windows icon resource they wouldn't use anyway.
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/installer/ticktimer.rc>
)

# THE line of this refactor: include/ joins the compiler's header search
# path for this target. PRIVATE = "for building this target only" (nobody
# links against an app, so nothing needs to inherit the path).
target_include_directories(ticktimer PRIVATE include)

target_link_libraries(ticktimer PRIVATE Qt6::Widgets Qt6::Network)
target_link_libraries(ticktimer PRIVATE ticktimer_sound)
# The chime WAVs ride inside the binary as Qt resources — nothing to
# copy beside the .exe, nothing for the installer to forget.
qt_add_resources(ticktimer "sounds" PREFIX "/sounds" BASE resources FILES
    resources/notify_phase.wav
    resources/notify_block.wav
)

# On Windows this hides the console window behind the GUI; harmless elsewhere.
set_target_properties(ticktimer PROPERTIES WIN32_EXECUTABLE ON)

# ---- Android packaging ---------------------------------------------------------
# qt_add_executable above is what makes this possible at all: on an Android
# kit it builds the app as a shared library and hands it to Qt's
# androiddeployqt, which wraps it in a Gradle project and produces the APK.
# Qt Creator drives that pipeline when you press Run on an Android kit —
# these properties are just the facts stamped into the package.
if(ANDROID)
    set_target_properties(ticktimer PROPERTIES
        # Reverse-DNS id Android uses to tell apps apart; installing a build
        # with a DIFFERENT package name creates a second app (and a second,
        # empty data folder) instead of updating the first. Pick once, keep.
        QT_ANDROID_PACKAGE_NAME    "org.ticktimer.app"
        # versionCode is Android's monotonic "is this an upgrade?" integer;
        # versionName is the human-readable one shown in app info.
        QT_ANDROID_VERSION_CODE    14
        QT_ANDROID_VERSION_NAME    "14.0"
        # Android 9 (API 28) and newer — old enough for any phone you own,
        # new enough that Qt 6 doesn't have to carry compatibility shims.
        QT_ANDROID_MIN_SDK_VERSION "28"
    )
endif()

# ---- tests -------------------------------------------------------------------
# The test binary compiles the domain/storage/tracking sources DIRECTLY and
# links only QtCore/Gui/Test — no Widgets. That this even builds is the
# architecture test nobody has to write: the day a domain file #includes a
# widget header, this target breaks and the layering violation is caught.
# Tests and dev tools are DESKTOP-ONLY: an Android configure would otherwise
# try to package each of these as its own APK (on Android every executable
# is an app). The domain they test is identical on every platform, so
# desktop-green is the proof — nothing is lost by fencing them off.
if(NOT ANDROID)

enable_testing()

qt_add_executable(test_domain
    ${DOMAIN_SOURCES}
    tests/test_domain.cpp
)
target_include_directories(test_domain PRIVATE include)
target_link_libraries(test_domain PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
# installerVersionMatchesTheHeader() reads the .iss and compares its
# AppVersion to Version.h. A test binary has no reliable idea where the
# source tree is — it may be run from build-release/, from ctest, or from
# a deploy folder — so the path is baked in at configure time rather than
# guessed from argv[0] or the working directory.
target_compile_definitions(test_domain PRIVATE
    TICKTIMER_ISS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/installer/ticktimer.iss")

add_test(NAME domain COMMAND test_domain)

# The model/view suite: unit-tests TaskListModel + TaskFilterProxy directly,
# through the QAbstractItemModel interface (rowCount / data / roles / filter /
# sort) — no widgets, so it links like the domain suite (Gui for QColor only).
# The DELEGATE, being paint code, is exercised by test_ui driving the real page;
# this suite proves the data side that a delegate merely renders.
qt_add_executable(test_taskmodel
    ${DOMAIN_SOURCES}
    include/TaskSnapshotModel.h
    include/CategoryTaskModel.h
    include/TaskListModel.h
    include/TaskFilterProxy.h
    src/TaskSnapshotModel.cpp
    src/CategoryTaskModel.cpp
    src/TaskListModel.cpp
    src/TaskFilterProxy.cpp
    tests/test_taskmodel.cpp
)
target_include_directories(test_taskmodel PRIVATE include)
target_link_libraries(test_taskmodel PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME taskmodel COMMAND test_taskmodel)

# The quick-add parser suite: a PURE function, so the leanest target in the
# project — parser + tests, Core/Test only. No domain, no widgets, no Gui.
# This is what "parse is a pure function" buys: microsecond tests, zero setup.
qt_add_executable(test_nlp
    include/QuickAddParser.h
    include/LlmProvider.h
    include/LlmQuickAdd.h
    src/QuickAddParser.cpp
    # v24: the provider layer joins this target rather than starting its own.
    # It is pure (QUrl/QJson/QSettings, no QNetworkAccessManager), so it earns
    # a seat in the Core-only suite — which is the whole argument for having
    # written it as free functions over a value instead of a network class.
    src/LlmProvider.cpp
    src/LlmQuickAdd.cpp
    # v25: chat:: is pure too — a transcript, a budget and a prompt, no
    # sockets — so the conversation layer is asserted here, offline, in
    # microseconds. Note what is NOT here: brief::, which reads AppData and
    # therefore belongs to test_domain. Two pure AI layers, two suites,
    # neither able to drag the other's dependencies in.
    include/ChatSession.h
    src/ChatSession.cpp
    # v29.1: intake's extraction half is pure too (values in, JSON out —
    # the prompt takes the area NAME, not AppData, precisely so it can sit
    # here). Its domain-reading brain stays in Intake.cpp / test_domain.
    include/Intake.h
    src/IntakeExtract.cpp
    # v30.0: memory:: earns a seat by the same argument chat:: did — a string
    # in and values out, no AppData, no clock, no socket. Note what is NOT
    # here: MemoryStore, which knows QStandardPaths and QSaveFile. The split
    # between the two files is exactly what lets the prompt band be asserted
    # in this Core-only suite without dragging the disk in behind it.
    include/Memory.h
    src/Memory.cpp
    tests/test_nlp.cpp
)
target_include_directories(test_nlp PRIVATE include)
target_link_libraries(test_nlp PRIVATE Qt6::Core Qt6::Test)

add_test(NAME nlp COMMAND test_nlp)
set_tests_properties(taskmodel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The UI regression suite: real widgets, driven headlessly on the
# "offscreen" platform (same trick as the screenshot tool). It exists for
# bugs the domain suite CANNOT see — widget lifetime, signal re-entrancy,
# event-loop ordering. Its first test is the add-a-task crash: the bug's
# tombstone, guaranteed to resurrect the failure if the fix is ever undone.
qt_add_executable(test_ui
    ${DOMAIN_SOURCES}
    ${UI_SOURCES}
    tests/test_ui.cpp
)
target_include_directories(test_ui PRIVATE include)
target_link_libraries(test_ui PRIVATE Qt6::Widgets Qt6::Test Qt6::Network
                                      ticktimer_sound)
# (No sound RESOURCES here on purpose: the tests never reach the chime
# code — setupNotifications bails on the headless tray guard — and if a
# desktop run ever did, empty resource bytes fall back to the beep.)

add_test(NAME ui COMMAND test_ui)
set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---- optional developer tools --------------------------------------------------
# Off by default so a normal build stays lean. `option()` declares a switch
# settable from the command line: cmake -B build -DBUILD_TOOLS=ON
# The tool reuses the app's own sources — same window, same theme — so the
# screenshot can never drift from what users actually see.
option(BUILD_TOOLS "Build developer tools (README screenshot generator)" OFF)
if(BUILD_TOOLS)
    qt_add_executable(screenshot-tool
        ${DOMAIN_SOURCES}
        ${UI_SOURCES}
        tools/screenshot.cpp
    )
    target_include_directories(screenshot-tool PRIVATE include)
    target_link_libraries(screenshot-tool PRIVATE Qt6::Widgets Qt6::Network
                                                  ticktimer_sound)
endif()

# ---- auth: shared account/hashing code, the server, and its tests ----------
# Desktop-only (a phone doesn't host the server; the client TALKS to one).
# AccountStore + PasswordHash are compiled once into a small static library so
# the server AND the account tests link the exact same code — no risk of the
# test verifying a different build than production runs.
add_library(ticktimer_auth STATIC
    src/AccountStore.cpp
    src/PlannerStore.cpp
    src/ShareStore.cpp
)
target_include_directories(ticktimer_auth PUBLIC include)
target_link_libraries(ticktimer_auth PUBLIC Qt6::Core)

qt_add_executable(ticktimer-server
    server/AuthServer.cpp
    server/server_main.cpp
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/installer/ticktimer-server.rc>
)
target_include_directories(ticktimer-server PRIVATE include server)
# Network, not Widgets: a headless service. QCoreApplication + QTcpServer.
target_link_libraries(ticktimer-server PRIVATE ticktimer_auth Qt6::Network)

qt_add_executable(test_auth
    ${DOMAIN_SOURCES}
    tests/test_auth.cpp
)
target_include_directories(test_auth PRIVATE include)
target_link_libraries(test_auth PRIVATE ticktimer_auth Qt6::Test Qt6::Gui)
add_test(NAME auth COMMAND test_auth)
# Headless environments (CI, ctest on a server) have no display; the suite
# links Qt6::Gui, whose default platform plugin (xcb) aborts without one.
# "offscreen" renders nowhere — exactly right for tests. Same property the
# ui suite has carried since it was born; auth needed it too the first time
# plain `ctest` ran on a machine with no display attached.
set_tests_properties(auth PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# End-to-end: spawns the REAL ticktimer-server, drives the REAL AuthClient
# against it over a real socket. Proves the whole loop (client → HTTP → server
# → store → response) not just the pieces. Kept separate from the fast unit
# tests because it needs the server binary and a live port.
qt_add_executable(test_login_live
    ${DOMAIN_SOURCES}
    include/AuthClient.h
    include/SyncClient.h
    include/SyncService.h
    include/ShareClient.h
    include/UpdateClient.h
    src/AuthClient.cpp
    src/SyncClient.cpp
    src/SyncService.cpp
    src/ShareClient.cpp
    src/UpdateClient.cpp
    tests/test_login_live.cpp
)
target_include_directories(test_login_live PRIVATE include)
target_link_libraries(test_login_live PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME login_live COMMAND test_login_live)
set_tests_properties(login_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

endif() # NOT ANDROID
