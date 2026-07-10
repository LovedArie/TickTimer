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
    include/Category.h
    include/Event.h
    include/Folder.h
    include/Ids.h
    include/JsonStore.h
    include/Segment.h
    include/SpecialDay.h
    include/Task.h
    include/Stats.h
    include/TrackerService.h
    src/AppData.cpp
    src/JsonStore.cpp
    src/Stats.cpp
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
    include/ReviewWidgets.h
    include/SpecialDaysPage.h
    include/TaskDetailDialog.h
    include/TaskRow.h
    include/UpcomingPage.h
    include/WeekAgendaView.h
    include/Theme.h
    include/Widgets.h
    src/ActivitiesPage.cpp
    src/AgendaWidget.cpp
    src/DueDateDialog.cpp
    src/EventDialog.cpp
    src/GlancePanel.cpp
    src/MainWindow.cpp
    src/PickActivityDialog.cpp
    src/PlannerPage.cpp
    src/PomodoroPage.cpp
    src/ReviewWidgets.cpp
    src/SpecialDaysPage.cpp
    src/TaskDetailDialog.cpp
    src/TaskRow.cpp
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
    src/UpdateClient.cpp
    src/UpdateBanner.cpp
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

add_test(NAME domain COMMAND test_domain)

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
target_link_libraries(test_ui PRIVATE Qt6::Widgets Qt6::Test Qt6::Network)

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
    target_link_libraries(screenshot-tool PRIVATE Qt6::Widgets Qt6::Network)
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
