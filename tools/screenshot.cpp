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
#include <QFont>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TickTimer"));
    theme::applyTheme(app); // same one-call theme setup as main.cpp

    // TICKTIMER_FONTPT=19 reproduces a PHONE's text metrics on a desktop.
    // Not a cosmetic knob: Qt on Android takes its default font from the
    // system, and this project's test phone reports 19pt against a desktop's
    // ~9pt. Widths are text-driven, so a layout that fits at desktop metrics
    // can still overflow a phone by a factor of two — measured here as a
    // window minimum of 376 on the desktop and 618 on the device, for the
    // same code. Without this, every phone width question costs a full
    // build-sign-install-screencap round trip.
    if (qEnvironmentVariableIntValue("TICKTIMER_FONTPT") > 0) {
        QFont f = QApplication::font();
        f.setPointSize(qEnvironmentVariableIntValue("TICKTIMER_FONTPT"));
        QApplication::setFont(f);
    }

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
    const auto probe = [&window]() {
        if (!qEnvironmentVariableIsSet("TICKTIMER_PROBE"))
            return;
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

        // ...and then the individual widgets over budget, with the path that
        // reaches each one. Naming the page is only half a diagnosis: a page
        // is over budget because some LEAF inside it promised a width it
        // cannot keep, and that leaf is usually a label or a checkbox whose
        // text is simply too long to wrap. TICKTIMER_BUDGET overrides the
        // default phone budget.
        const int budget = qEnvironmentVariableIntValue("TICKTIMER_BUDGET") > 0
                               ? qEnvironmentVariableIntValue("TICKTIMER_BUDGET")
                               : 360;
        qInfo() << "widgets wider than" << budget << "px:";
        for (QWidget* w : window.findChildren<QWidget*>()) {
            if (w->isHidden())
                continue; // a hidden widget contributes nothing to a layout
            const int mw = w->minimumSizeHint().width();
            if (mw <= budget)
                continue;
            QString path;
            for (const QObject* o = w; o && o != &window; o = o->parent())
                path = QLatin1Char('/') + QString::fromLatin1(
                           o->metaObject()->className()) + path;
            qInfo().noquote()
                << QStringLiteral("  %1 min=%2 %3")
                       .arg(path).arg(mw)
                       .arg(w->property("text").toString().left(60));
        }
    };

    const QString outPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("screenshot.png");

    // Grab AFTER the event loop has run briefly: layouts settle and the
    // first paint happens inside the loop, not inside show(). Grabbing
    // immediately would capture a half-laid-out window.
    QTimer::singleShot(500, &app, [&window, &app, outPath, probe]() {
        // The probe runs HERE, not right after show(), and that is a
        // correctness fix rather than tidiness: since v30.5 the layout mode
        // is dispatched through the event loop (ResponsiveWatcher), so a
        // measurement taken before the loop has turned reports the layout
        // the window was BORN with, not the one it settled into. Same reason
        // the grab waits.
        probe();
        window.grab().save(outPath);
        app.quit();
    });

    return app.exec();
}
