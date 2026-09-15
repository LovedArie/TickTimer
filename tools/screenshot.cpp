// ---------------------------------------------------------------------------
// screenshot.cpp — a developer TOOL, not part of the app: it opens the real
// MainWindow (with whatever data.json you have), waits half a second for
// layout and first paint, renders the window into an image, saves it, and
// quits. Used to produce docs/screenshots/*.png for the README.
//
// Why a tool instead of pressing PrintScreen: a repeatable command gives
// the same framing every time, works on machines with no screen at all
// (CI, this project's test sandbox), and turns "update the screenshot"
// from a chore into one command. Automating the boring parts of
// documentation is how the boring parts stay done.
//
// Build it with:  cmake -B build -DBUILD_TOOLS=ON  && cmake --build build
// Run it with:    ./build/screenshot-tool [output.png]
//
// TICKTIMER_DEMO=1 shoots a MADE-UP planner instead of yours. The README is
// public, so a screenshot of real data publishes a real schedule. Demo mode
// switches the organisation and application names first, which moves BOTH
// the data folder (QStandardPaths) and the preferences (QSettings) somewhere
// the real app never looks - so it cannot read, or overwrite, a real planner.
// TICKTIMER_PLANNER_MODE=1 shows the week instead of the day.
//
// The README's set, from the repo root (Git Bash, Qt's bin on PATH):
//   S=build-release/screenshot-tool.exe; export TICKTIMER_DEMO=1
//   $S docs/screenshots/day.png 0 1280 1000
//   TICKTIMER_PLANNER_MODE=1 $S docs/screenshots/week.png 0 1280 1000
//   $S docs/screenshots/life-areas.png 2 1280 860
//   $S docs/screenshots/upcoming.png 1 1280 860
//   # phone size: offscreen, or Windows clamps the window to the screen
//   export QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR=C:/Windows/Fonts
//   export TICKTIMER_COMPACT=1 QT_SCALE_FACTOR=2
//   $S docs/screenshots/phone-day.png 0 390 844
//   $S docs/screenshots/phone-areas.png 2 390 844
// Pages: 0 Calendar, 1 Upcoming, 2 Life areas, 3 Special days, 4 Pomodoro,
// 5 Archive, 6 Assistant.
// ---------------------------------------------------------------------------

#include "AppData.h"
#include "JsonStore.h"
#include "MainWindow.h"
#include "PlannerPage.h"
#include "Segment.h"
#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QFont>
#include <QSettings>
#include <QStatusBar>
#include <QStackedWidget>
#include <QTimer>

namespace {

// A believable week for a student with a part-time job, built through the
// same AppData doors the app uses, so every rule the real app enforces
// applies here too. Anchored on TODAY: the calendar always opens on today,
// and the now-line has to land among the blocks. Only blocks already over
// get tracked time, so "planned vs actual" looks honest at any hour.
void seedDemo(AppData& d)
{
    const QDate today = QDate::currentDate();
    const QDateTime now = QDateTime::currentDateTime();

    const QString school = d.addCategory(QStringLiteral("School"), QColor("#4C6FE0"));
    const QString work   = d.addCategory(QStringLiteral("Work"), QColor("#E08A3C"));
    const QString health = d.addCategory(QStringLiteral("Health"), QColor("#3FAE6A"));
    const QString life   = d.addCategory(QStringLiteral("Personal"), QColor("#9B59B6"));

    const QString lecture = d.addActivity(QStringLiteral("Algorithms lecture"), school);
    const QString study   = d.addActivity(QStringLiteral("Study session"), school);
    const QString shift   = d.addActivity(QStringLiteral("Cafe shift"), work);
    const QString gym     = d.addActivity(QStringLiteral("Gym"), health);
    const QString run     = d.addActivity(QStringLiteral("Morning run"), health);
    const QString reading = d.addActivity(QStringLiteral("Reading"), life);
    const QString errands = d.addActivity(QStringLiteral("Groceries"), life);

    const QString lab = d.addTask(QStringLiteral("Lab 4 - graph search (5%)"), school,
                                  today.addDays(2), QTime(23, 59));
    d.setTaskPriority(lab, Task::Priority::Urgent);
    d.setTaskSize(lab, 180, true);
    d.addSubtask(lab, QStringLiteral("Implement BFS and DFS"));
    d.addSubtask(lab, QStringLiteral("Write the complexity analysis"));
    const QString essay = d.addTask(QStringLiteral("Essay outline - ethics in AI"), school,
                                    today.addDays(5));
    d.setTaskSize(essay, 90, true);
    d.addTask(QStringLiteral("Swap Saturday shift with Sam"), work, today.addDays(1));
    d.addTask(QStringLiteral("Renew bus pass"), life, today.addDays(1));
    d.setTaskDone(d.addTask(QStringLiteral("Pay phone bill"), life, today), true);
    d.addTask(QStringLiteral("Book dentist appointment"), health);

    const auto track = [&](const QString& id, QDate day, int fromMin, int toMin) {
        if (id.isEmpty())
            return;
        const QDateTime a(day, QTime(fromMin / 60, fromMin % 60));
        const QDateTime b(day, QTime(toMin / 60, toMin % 60));
        if (b > now)
            return; // no tracked time in the future
        Segment seg;
        seg.start = a;
        seg.end   = b;
        d.appendSegment(id, seg);
    };

    // Monday to Sunday of this week.
    const QDate monday = today.addDays(1 - today.dayOfWeek());
    for (int i = 0; i < 7; ++i) {
        const QDate day = monday.addDays(i);
        const int dow = day.dayOfWeek();
        if (dow <= 5) {
            track(d.addEvent(day, 7 * 60, 8 * 60, run), day, 7 * 60 + 5, 7 * 60 + 52);
            track(d.addEvent(day, 8 * 60 + 30, 10 * 60, lecture), day, 8 * 60 + 32, 9 * 60 + 58);
            track(d.addTaskEvent(day, 10 * 60 + 30, 12 * 60, lab), day, 10 * 60 + 35, 11 * 60 + 40);
            track(d.addEvent(day, 13 * 60, 14 * 60 + 30, study), day, 13 * 60 + 10, 14 * 60 + 20);
        }
        if (dow == 2 || dow == 4 || dow == 6)
            track(d.addEvent(day, 16 * 60, 20 * 60, shift), day, 16 * 60, 20 * 60);
        if (dow == 1 || dow == 3 || dow == 5)
            track(d.addEvent(day, 17 * 60, 18 * 60 + 30, gym), day, 17 * 60 + 5, 18 * 60 + 15);
        if (dow == 7)
            track(d.addEvent(day, 10 * 60, 11 * 60, errands), day, 10 * 60, 10 * 60 + 50);
        track(d.addEvent(day, 21 * 60, 22 * 60, reading), day, 21 * 60, 21 * 60 + 40);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TickTimer"));

    // Demo mode (see the header): new names FIRST, before anything asks
    // QStandardPaths or QSettings where things live.
    const bool demo = qEnvironmentVariableIsSet("TICKTIMER_DEMO");
    if (demo) {
        QApplication::setOrganizationName(QStringLiteral("TickTimerDemo"));
        QApplication::setApplicationName(QStringLiteral("TickTimerDemo"));
        QSettings().clear(); // the same fresh preferences every run
        const QString path = JsonStore::defaultFilePath();
        QFile::remove(path);
        AppData data;
        seedDemo(data);
        if (!JsonStore(path).save(data)) {
            qWarning("could not write the demo planner to %s", qPrintable(path));
            return 1;
        }
    }
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
    // The status bar names the data file, whose path contains the Windows
    // account name - not something a public README should carry.
    if (demo)
        window.statusBar()->hide();

    // Optional 2nd argument: which page to shoot (0 Calendar, 1 Activities,
    // 2 Pomodoro) — so the README can show more than the front page.
    if (argc > 2)
        window.showPage(QString::fromLocal8Bit(argv[2]).toInt());
    if (qEnvironmentVariableIntValue("TICKTIMER_PLANNER_MODE") > 0)
        if (auto* planner = window.findChild<PlannerPage*>())
            QMetaObject::invokeMethod(planner, "setMode",
                Q_ARG(int, qEnvironmentVariableIntValue("TICKTIMER_PLANNER_MODE")));

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
