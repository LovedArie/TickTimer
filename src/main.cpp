// ---------------------------------------------------------------------------
// main.cpp — the smallest file in the project, on purpose.
//
// A healthy main() does exactly three things: create the QApplication,
// configure app-wide facts, show the main window. All real behaviour
// lives in classes that can be tested and reasoned about; main() is just
// the ignition.
// ---------------------------------------------------------------------------

#include "LlmProvider.h"
#include "LoginDialog.h"
#include "ResponsiveWatcher.h"
#include "SessionStore.h"
#include "MainWindow.h"
#include "ProbeOverlay.h"
#include "Theme.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // These names are not decoration: QStandardPaths builds the data folder
    // from them, and QSettings builds its storage path from them. Set them
    // BEFORE anything asks for a path.
    //
    // A short archaeology, because this block has now bitten twice:
    //   * Originally only the APPLICATION name was set, with a warning that
    //     naming the organization too would "double the folder
    //     (TickTimer/TickTimer)". True — and the price of that choice was an
    //     ANONYMOUS QSettings path, which on some Windows machines silently
    //     drops writes (the un-tickable pomodoro checkbox; the un-openable
    //     review gate — v22.7's field bug).
    //   * v22.7 added the organization name to cure the settings — and
    //     walked straight into the documented folder-doubling: the data
    //     folder moved one level deeper, the app opened an empty home, and
    //     the owner met a blank planner ("my data is completely erased" —
    //     it never was; the file sat untouched at the old address).
    //   * v22.8 keeps the organization name and pays its real cost:
    //     JsonStore::migrateLegacyData — the bridge the original comment
    //     itself prescribed for renames — now carries every data*.json from
    //     the old home into the new one (copy, never move, never overwrite).
    //
    // The lesson, at the site of the wound: a comment that says "caught in
    // testing" is a tripwire, and the patch that steps past it owes the
    // reader either a migration or an apology. This one now ships both.
    QApplication::setOrganizationName(QStringLiteral("TickTimer"));
    QApplication::setApplicationName(QStringLiteral("TickTimer"));

    // v24: v21's single-vendor AI settings ("ai/anthropicApiKey", a global
    // "ai/model") become per-provider entries. One line, once, immediately
    // after the settings PATH exists and before anything reads a preference —
    // the same placement rule JsonStore::migrateLegacyData earned the hard
    // way. Idempotent, so a second launch is a no-op.
    ai::migrateLegacySettings();

    // One call pins the ENTIRE look: Fusion style, our light palette, and
    // the stylesheet. All three together, always — v3 set only the
    // stylesheet and inherited the palette from the OS, which painted the
    // calendar black on dark-mode Windows (the story is in Theme.h).
    theme::applyTheme(app);

    // Phones: a QDialog is its own top-level window, so none of the
    // container-driven layout machinery reaches it. This gives every dialog
    // the screen on a compact device and maps Android's Back key to reject().
    // Installed before the login dialog, which is the first one shown.
    responsive::installCompactDialogFitter(&app);

    // TICKTIMER_PROBE (or `?probe` in the URL on the web build): draw the
    // screen readings on top of the app. Nothing is constructed unless the
    // switch is set, so this line costs an `if` on every other launch.
    //
    // BEFORE the login dialog, not after, and that ordering is the whole
    // point: the numbers it reports — availableGeometry, device pixel ratio,
    // the isCompactScreen() verdict — are what decide the layout, and someone
    // holding a borrowed phone must be able to read them without an account,
    // a password or a reachable server. It is also the only moment guaranteed
    // to happen: login.exec() below can return without ever building a
    // window. See ProbeOverlay.h for why a console cannot do this job.
    ProbeOverlay::showIfRequested();

    // The server address lives in QSettings (a preference, not domain data —
    // same rule as everything else): localhost by default, editable to a
    // laptop's LAN IP or a future Pi with no recompile. One line, three
    // deployments.
    const QString serverUrl =
        QSettings().value(QStringLiteral("sync/serverUrl"),
                          QStringLiteral("http://localhost:8080"))
            .toString();

    // Login is a GATE: show the dialog first, and only build the app if it's
    // accepted. QDialog::exec() blocks in its own event loop until accept()
    // or reject() — the one place blocking is correct, because there's
    // genuinely nothing else for the app to do until someone logs in.
    LoginDialog login(serverUrl);
    if (login.exec() != QDialog::Accepted)
        return 0; // user closed the dialog without logging in — clean exit

    // v30.2 — remember who got in, and (only if they asked) what proves it.
    // Written HERE rather than inside the dialog because this is the moment
    // the gate is known to have opened: a dialog that stored on success would
    // also store on a success the user then cancelled out of.
    session::setLastUser(login.loggedInUser());
    if (!login.deviceToken().isEmpty())
        session::setDeviceToken(login.loggedInUser(), login.deviceToken());

    MainWindow window(login.loggedInUser());
    // Sync needs two things login just produced: where the server is, and
    // the session token proving who we are. Handed over once, here — the
    // window never sees a password. Note we ask the DIALOG for the URL, not
    // the settings value read earlier: the person may have edited the
    // address on the login screen, and the dialog owns that decision now.
    if (login.offline()) {
        // No session, so nothing to sync WITH yet. The window runs on this
        // machine's planner and keeps trying to reach the server in the
        // background; the moment it answers, sync switches itself on without
        // anyone being asked to do anything.
        window.beginOffline(login.serverUrl());
    } else {
        window.enableSync(login.serverUrl(), login.authToken());
    }
    window.show();

    return app.exec(); // the Qt event loop — everything after is reactions
}
