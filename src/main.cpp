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
#include "MainWindow.h"
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

    MainWindow window(login.loggedInUser());
    // Sync needs two things login just produced: where the server is, and
    // the session token proving who we are. Handed over once, here — the
    // window never sees a password. Note we ask the DIALOG for the URL, not
    // the settings value read earlier: the person may have edited the
    // address on the login screen, and the dialog owns that decision now.
    window.enableSync(login.serverUrl(), login.authToken());
    window.show();

    return app.exec(); // the Qt event loop — everything after is reactions
}
