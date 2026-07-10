// ---------------------------------------------------------------------------
// screenshot.cpp — a developer TOOL, not part of the app: it opens the real
// MainWindow (with whatever data.json you have), waits half a second for
// layout and first paint, renders the window into an image, saves it, and
// quits. Used to produce docs/screenshot.png for the README.
//
// Why a tool instead of pressing PrintScreen: a repeatable command gives
// the same framing every time, works on machines with no screen at all
// (CI, this project's test sandbox), and turns "update the screenshot"
// from a chore into one command. Automating the boring parts of
// documentation is how the boring parts stay done.
//
// Build it with:  cmake -B build -DBUILD_TOOLS=ON  && cmake --build build
// Run it with:    ./build/screenshot-tool [output.png]
// ---------------------------------------------------------------------------

#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TickTimer"));
    theme::applyTheme(app); // same one-call theme setup as main.cpp

    MainWindow window;
    // Optional 4th/5th arguments: window size — lets the tool render the
    // COMPACT layout ("what will this look like on a phone?") without
    // owning a phone. Pair with TICKTIMER_COMPACT=1 in the environment.
    const int w = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 1180;
    const int h = argc > 4 ? QString::fromLocal8Bit(argv[4]).toInt() : 800;
    window.resize(w > 0 ? w : 1180, h > 0 ? h : 800);
    window.show();

    // Optional 2nd argument: which page to shoot (0 Calendar, 1 Activities,
    // 2 Pomodoro) — so the README can show more than the front page.
    if (argc > 2)
        window.showPage(QString::fromLocal8Bit(argv[2]).toInt());

    // Layout probe (TICKTIMER_PROBE=1): print the window's minimum size and
    // every stacked page's contribution. A QStackedWidget's minimum is the
    // MAX over all its pages — so "the window won't shrink" is always some
    // page's fault, and this names the culprit instead of leaving you to
    // bisect widgets by hand. Diagnosis should be a command, not a hunt.
    if (qEnvironmentVariableIsSet("TICKTIMER_PROBE")) {
        // Where do preferences live on THIS machine? Ask QSettings instead
        // of guessing the platform path — the answer depends on org/app
        // names and OS conventions, and guessing wrong fails silently.
        qInfo() << "settings file:" << QSettings().fileName();
        qInfo() << "window minimumSizeHint:" << window.minimumSizeHint();
        const auto stacks = window.findChildren<QStackedWidget*>();
        for (QStackedWidget* stack : stacks)
            for (int i = 0; i < stack->count(); ++i)
                qInfo() << "  stack" << stacks.indexOf(stack) << "page" << i
                        << stack->widget(i)->metaObject()->className()
                        << "min:" << stack->widget(i)->minimumSizeHint();
    }

    const QString outPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("screenshot.png");

    // Grab AFTER the event loop has run briefly: layouts settle and the
    // first paint happens inside the loop, not inside show(). Grabbing
    // immediately would capture a half-laid-out window.
    QTimer::singleShot(500, &app, [&window, &app, outPath]() {
        window.grab().save(outPath);
        app.quit();
    });

    return app.exec();
}
