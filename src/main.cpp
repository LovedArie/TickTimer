// ---------------------------------------------------------------------------
// main.cpp — the smallest file in the project, on purpose.
//
// A healthy main() does exactly three things: create the QApplication,
// configure app-wide facts, show the main window. All real behaviour
// lives in classes that can be tested and reasoned about; main() is just
// the ignition.
// ---------------------------------------------------------------------------

#include "LoginDialog.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // This name is not decoration: QStandardPaths uses it to build the
    // data folder (…/AppData/Roaming/TimeFocusTracker on Windows). Set it
    // BEFORE anything asks for a path. We deliberately set only the
    // APPLICATION name — AppDataLocation is "<organization>/<application>",
    // so setting both to the same string would double the folder
    // (TickTimer/TickTimer). Caught in testing. Renaming this
    // string moves the data folder — JsonStore::migrateLegacyData is
    // the bridge that keeps existing data reachable after the rename.
    QApplication::setApplicationName(QStringLiteral("TickTimer"));

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
