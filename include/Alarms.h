#pragma once
// ---------------------------------------------------------------------------
// Alarms.h — the schedule as a VALUE (v30.6).
//
// WHY THIS HEADER EXISTS AT ALL. Until v30.6 the block alarm was a QTimer
// living inside BlockAlarmService: derive the next start, arm a single-shot
// for exactly then, re-derive whenever AppData changed. That is the right
// design for a process that is always alive, and it is why the desktop
// alarm has been correct since v19.7.
//
// Android froze it. A backgrounded app is suspended (Doze, app standby), so
// the timer does not fire — and being elsewhere is the entire scenario a
// block alarm exists for. The old header's own comment names the property
// that dies: an hourly self-check "heals clock jumps and suspend/resume
// without any platform-specific wake signals". It heals nothing when the
// process is not running to do the healing.
//
// THE INVERSION. The service used to own a timer and push event IDS at fire
// time, re-resolving their text from AppData at the last possible moment.
// It now derives a whole forward SCHEDULE of finished alarms and hands it
// over — to a QTimer on desktop, to Android's AlarmManager on a phone. The
// one rule everything below follows from:
//
//     C++ MUST NOT NEED TO RUN AT FIRE TIME.
//
// On Android it cannot. So the text is rendered HERE, in advance, and the
// OS stores it. That is the exact opposite of the v19.7 ids-not-text rule,
// and the inversion is safe for a reason worth stating: every edit path
// goes through AppData::changed(), which republishes the entire window, so
// the OS's copy is never staler than the last change anyone made.
//
// WHAT IS PURE HERE AND WHY IT MATTERS. Three producer functions and one
// filter, all free functions over values — no AppData mutation, no QTimer,
// no QSettings, no clock. The caller supplies `now`, the Rule, and the two
// facts that live outside the planner (which block is being tracked; when
// the check-in last spoke). That is what lets a table of microsecond tests
// pin the grace window, the horizon, the key's stability and the two mute
// rules without a single timer — the same treatment SyncPlan::decide,
// MissedBlocks and Affordability already got.
//
// NAMING NOTE (the sync/syncplan scar, CLAUDE.md): POSIX declares
// `alarm()`, singular, in <unistd.h>. A namespace named `alarm` would be a
// "redefinition as a different kind of symbol" on the first Android
// compile, exactly as `sync` was. `alarms`, plural, collides with nothing.
// ---------------------------------------------------------------------------

#include "AppData.h"
#include "CheckIn.h"
#include "Event.h"

#include <QDate>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace alarms
{

// Which chime the alarm speaks in. Data, not a constant buried in a
// handler — the same promotion ToastSpec::Kind made for the accent bar.
enum class Chime
{
    Block, // the agenda's voice: a block starting or finishing
    Phase, // the Pomodoro's voice: focus done, break over
};

// ---------------------------------------------------------------------------
// Alarm — one thing the app will say, at one instant, already written.
//
// PRE-RENDERED ON PURPOSE. See the header note: the process that shows this
// may not exist. Nothing here may require a lookup at fire time.
// ---------------------------------------------------------------------------
struct Alarm
{
    // Stable identity, DERIVED — never a counter. In-process, duplicates
    // were impossible because a high-water mark only moved forward.
    // Out-of-process the OS owns identity (Android keys a PendingIntent by
    // request code), so republishing the same alarm must produce the same
    // key or every republish would double-book the schedule. Shape is
    // "<source>:<entity id>:<epoch seconds>".
    QString key;

    QDateTime at;
    QString   title;
    QString   body;
    Chime     chime = Chime::Block;

    bool operator==(const Alarm& other) const
    {
        return key == other.key && at == other.at && title == other.title
               && body == other.body && chime == other.chime;
    }
};

struct Rule
{
    // Late is forgiven up to here; beyond it, silence. Inherited unchanged
    // from BlockAlarmService::kGraceSeconds — ten stale toasts about a
    // morning that already happened is noise, not help.
    int graceSeconds = 120;

    // How far ahead to publish. Two days is enough that a phone left off
    // overnight still wakes with tomorrow armed, and small enough that the
    // OS is never handed hundreds of pending intents. Every republish
    // replaces the whole window, so this is a working set, not a backlog.
    int horizonHours = 48;

    // WHEN TO KNOCK for the morning check-in, which is a genuinely
    // different question from checkin::Rule's window. That window says
    // when a knock is ACCEPTABLE (06:00–11:00) and was written for an app
    // already open — it fires whenever you happen to launch inside it.
    // Nobody is launching anything at 06:00 sharp, so a scheduled knock
    // needs its own answer to "when should the phone speak first?".
    // 08:00 sits inside the window with room to spare on both sides.
    int checkInMinuteOfDay = 8 * 60;
};

namespace detail
{

// "9:00 AM" from minutes-after-midnight, handling 1440 -> 12:00 AM.
//
// Deliberately a COPY of Widgets.h::timeLabel rather than a call to it:
// Widgets.h is the UI layer and includes QWidget, and this header is
// compiled into test_domain, which links without Qt Widgets on purpose
// (CMakeLists.txt: "the architecture test nobody has to write"). Twelve
// lines of formatting is the correct price for not dragging the widget
// tree into the domain.
inline QString clockLabel(int minutesAfterMidnight)
{
    const int mm     = minutesAfterMidnight % (24 * 60);
    int       hour   = mm / 60;
    const int minute = mm % 60;
    const bool am    = hour < 12;
    hour %= 12;
    if (hour == 0)
        hour = 12;
    return QStringLiteral("%1:%2 %3")
        .arg(hour)
        .arg(minute, 2, 10, QChar('0'))
        .arg(am ? QStringLiteral("AM") : QStringLiteral("PM"));
}

// The ONE place a date plus minutes-after-midnight becomes an instant.
// Moved here from BlockAlarmService.cpp so the desktop timer and the
// Android scheduler cannot drift on what "9:00 on the 3rd" means.
inline QDateTime instantOf(QDate day, int minutesAfterMidnight)
{
    return QDateTime(day, QTime(0, 0)).addSecs(qint64(minutesAfterMidnight) * 60);
}

inline QString keyFor(const QString& source, const QString& entityId,
                      const QDateTime& at)
{
    return QStringLiteral("%1:%2:%3")
        .arg(source, entityId)
        .arg(at.toSecsSinceEpoch());
}

} // namespace detail

// ---------------------------------------------------------------------------
// upcoming — every alarm the PLANNER implies, in [from, from + horizon].
//
// Two kinds, from one pass over the events:
//
//   START ("your 9:00 is beginning") for every planned block, EXCEPT the
//   one you are already tracking — the own-hands rule, which lived in
//   MainWindow until v30.6 and is now pure and pinned. It moved because it
//   has to survive the app being closed: republishing a schedule that
//   drops the tracked block is what makes the mute work out-of-process,
//   and starting to track emits changed(), so the republish happens at
//   exactly the right moment.
//
//   END ("its planned time is up") for the tracked block ONLY. On desktop
//   this fired from TrackerService's own exit door and so was, by
//   definition, only ever about something being tracked. Publishing an end
//   alarm for every planned block would be a different and worse feature —
//   the phone announcing the end of work you never started.
//
// Blocks with a settled outcome (Done / Moved / Dropped) are skipped: a
// decision already made is not something to be alarmed about.
//
// `trackedEventId` is empty when nothing is being tracked. It is passed in
// rather than read from a TrackerService so this stays a free function
// over values — the caller owns the wiring.
// ---------------------------------------------------------------------------
inline QVector<Alarm> upcoming(const AppData& data, const QDateTime& from,
                               const QString& trackedEventId,
                               const Rule& rule = {})
{
    const QDateTime until = from.addSecs(qint64(rule.horizonHours) * 3600);

    QVector<Alarm> out;
    for (const Event& e : data.events()) {
        if (e.outcome != BlockOutcome::Unset)
            continue; // already answered for; nothing to announce

        const QString label = data.eventLabel(e);
        const QString when  = QStringLiteral("%1 – %2")
                                 .arg(detail::clockLabel(e.plannedStartMinutes),
                                      detail::clockLabel(e.plannedEndMinutes));

        const bool tracked = !trackedEventId.isEmpty() && e.id == trackedEventId;

        if (!tracked) {
            const QDateTime at = detail::instantOf(e.date, e.plannedStartMinutes);
            if (at > from && at <= until) {
                Alarm a;
                a.key   = detail::keyFor(QStringLiteral("start"), e.id, at);
                a.at    = at;
                a.title = QStringLiteral("Starting now");
                a.body  = QStringLiteral("%1 · %2").arg(label, when);
                a.chime = Chime::Block;
                out.append(a);
            }
        } else {
            const QDateTime at = detail::instantOf(e.date, e.plannedEndMinutes);
            if (at > from && at <= until) {
                Alarm a;
                a.key   = detail::keyFor(QStringLiteral("end"), e.id, at);
                a.at    = at;
                a.title = QStringLiteral("%1 finished").arg(label);
                a.body  = QStringLiteral("Its planned time is up — %1.").arg(when);
                a.chime = Chime::Block;
                out.append(a);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// forCheckIn — the morning knock, scheduled rather than launched into.
//
// Returns at most one alarm, for `day`, and only when checkin::isDayHeavy
// agrees — the same gate CheckInService applies live, minus the "am I
// inside the window right now" clause, because a scheduled alarm is by
// construction inside it. `lastOffered` is the caller's QSettings fact,
// passed in for the same reason checkin::shouldOffer takes it: the
// once-a-day promise stays testable without touching settings.
//
// Deliberately NOT folded into upcoming(): it needs two inputs the planner
// does not carry, and a producer that quietly grew extra parameters is how
// a pure function stops being one.
// ---------------------------------------------------------------------------
inline QVector<Alarm> forCheckIn(const AppData& data, QDate day,
                                 QDate lastOffered, const Rule& rule = {},
                                 const checkin::Rule& gate = {})
{
    if (!day.isValid() || lastOffered == day)
        return {};
    if (!checkin::isDayHeavy(data, day, gate))
        return {};

    const QDateTime at = detail::instantOf(day, rule.checkInMinuteOfDay);

    Alarm a;
    a.key   = detail::keyFor(QStringLiteral("checkin"),
                             day.toString(Qt::ISODate), at);
    a.at    = at;
    a.title = QStringLiteral("Morning");
    a.body  = QStringLiteral("Today looks heavy. Want to check in?");
    a.chime = Chime::Block;
    return { a };
}

// ---------------------------------------------------------------------------
// forPhase — the Pomodoro's countdown, handed to the OS.
//
// The engine's phase end is a known instant the moment a phase starts, so
// it schedules like any other alarm. It takes plain values rather than a
// PomodoroEngine* on purpose: the engine is a QObject with a running timer,
// and a pure function that accepted one could not be called from a test
// without building one.
//
// `endsAt` invalid, or a paused phase, yields nothing — the caller
// republishes on modeChanged(), so pause cancels and resume re-arms with no
// state kept here.
// ---------------------------------------------------------------------------
inline QVector<Alarm> forPhase(bool running, bool nextIsBreak, int round,
                               const QDateTime& endsAt)
{
    if (!running || !endsAt.isValid())
        return {};

    Alarm a;
    a.key   = detail::keyFor(QStringLiteral("phase"),
                             QString::number(round), endsAt);
    a.at    = endsAt;
    a.title = nextIsBreak ? QStringLiteral("Focus done")
                          : QStringLiteral("Break over");
    a.body  = nextIsBreak
                  ? QStringLiteral("Time for a break.")
                  : QStringLiteral("Round %1 — back to it. You've got this.")
                        .arg(round);
    a.chime = Chime::Phase;
    return { a };
}

// ---------------------------------------------------------------------------
// dueBetween — the DESKTOP half, unchanged in meaning.
//
// Which of a published schedule became due in (mark, now], and is still
// fresh enough to be worth saying. This is BlockAlarmService::poll's inner
// loop, lifted out and made a filter over values: the high-water mark and
// the grace window are now pinned without a timer or an AppData.
//
// Android never calls this — there, the OS is the thing that knows an
// instant arrived. That asymmetry is the whole point of the split.
// ---------------------------------------------------------------------------
inline QVector<Alarm> dueBetween(const QVector<Alarm>& schedule,
                                 const QDateTime& mark, const QDateTime& now,
                                 const Rule& rule = {})
{
    QVector<Alarm> due;
    for (const Alarm& a : schedule) {
        if (a.at <= mark || a.at > now)
            continue;
        if (a.at.secsTo(now) <= rule.graceSeconds)
            due.append(a);
    }
    return due;
}

// The next instant strictly after `mark` — what a desktop timer should be
// armed for. Invalid when the schedule holds nothing ahead, which the
// caller reads as "sleep until the data changes".
inline QDateTime nextAfter(const QVector<Alarm>& schedule, const QDateTime& mark)
{
    QDateTime next;
    for (const Alarm& a : schedule)
        if (a.at > mark && (!next.isValid() || a.at < next))
            next = a.at;
    return next;
}

} // namespace alarms

// AlarmService emits QVector<alarms::Alarm>, and QSignalSpy stores signal
// arguments as QVariant — which needs the metatype declared for a type Qt
// has never seen. QVector<T> registers itself once T is known, so this one
// line covers the vector too.
Q_DECLARE_METATYPE(alarms::Alarm)
