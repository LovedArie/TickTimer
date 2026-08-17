#pragma once
// ---------------------------------------------------------------------------
// prefs — typed accessors for DISPLAY preferences, stored in QSettings.
//
// The dividing line this file exists to defend (same as the Pomodoro
// durations, §3.31): data.json holds FACTS about your time; QSettings holds
// TASTE about how this machine shows them. Your agenda hours and week start
// are taste — change them and no fact changed, nothing needs to sync, and
// two devices may legitimately disagree.
//
// Why functions instead of raw QSettings().value(...) calls at every use
// site: the DEFAULTS and the CLAMPING live here, once. A widget asking for
// the agenda window can never receive garbage (end before start, hours
// outside the domain grid, minutes off the slot lines) no matter what a
// hand-edited settings file says — the getter repairs on the way out.
// Garbage tolerated on disk, never in the program. (AppData does the same
// for data.json; this is the miniature version for preferences.)
//
// Header-only on purpose: four tiny functions, no state, one include.
// ---------------------------------------------------------------------------

#include "Event.h"        // plan:: — the domain grid these preferences live inside
#include "ReturnPolicy.h" // the two needs-a-block clocks (§C)
#include "MissedBlocks.h" // missed::Rule — the catch-up threshold (v26.2)
#include "TaskCoverage.h" // coverage::Rule / Escalation (§B/§D)

#include <QByteArray>
#include <QDateTime>
#include <QPoint>
#include <QSettings>
#include <Qt>

namespace prefs
{

// ---- the agenda's visible window (minutes after midnight) -----------------
// A DISPLAY window over the domain grid, never a replacement for it: the
// legal planning range stays plan::kDayStartMinutes..kDayEndMinutes; this
// only chooses how much of it the agenda paints by default. (AgendaWidget
// additionally stretches past this window whenever a block would otherwise
// be hidden — "data always wins", see design-addendum-settings §B.)

inline QPair<int, int> agendaWindow()
{
    QSettings settings;
    int start = settings.value(QStringLiteral("agenda/startMinutes"),
                               plan::kDayStartMinutes).toInt();
    int end   = settings.value(QStringLiteral("agenda/endMinutes"),
                               plan::kDayEndMinutes).toInt();

    // Repair, in dependency order: onto the slot grid, into the domain
    // window, and only THEN enforce "at least two slots visible" — a
    // nonsense pair like (900, 300) degrades to something sane, never UB.
    const auto snap = [](int m) {
        return (m / plan::kSlotMinutes) * plan::kSlotMinutes;
    };
    start = qBound(plan::kDayStartMinutes, snap(start),
                   plan::kDayEndMinutes - 2 * plan::kSlotMinutes);
    end   = qBound(start + 2 * plan::kSlotMinutes, snap(end),
                   plan::kDayEndMinutes);
    return {start, end};
}

inline void setAgendaWindow(int startMinutes, int endMinutes)
{
    QSettings settings;
    settings.setValue(QStringLiteral("agenda/startMinutes"), startMinutes);
    settings.setValue(QStringLiteral("agenda/endMinutes"), endMinutes);
}

// ---- first day of the week -------------------------------------------------
// Stored as the Qt::DayOfWeek integer (Monday==1 .. Sunday==7). Anything
// unrecognized reads back as Monday — the app's historical behaviour, so a
// corrupt key degrades to "what it always did", not to a surprise.

inline Qt::DayOfWeek firstDayOfWeek()
{
    const int raw = QSettings()
                        .value(QStringLiteral("week/firstDay"),
                               int(Qt::Monday))
                        .toInt();
    return (raw >= int(Qt::Monday) && raw <= int(Qt::Sunday))
               ? Qt::DayOfWeek(raw)
               : Qt::Monday;
}

inline void setFirstDayOfWeek(Qt::DayOfWeek day)
{
    QSettings().setValue(QStringLiteral("week/firstDay"), int(day));
}

// ---- pomodoro --------------------------------------------------------------
// The durations stay on the Pomodoro page (they were there first, and a
// preference lives closest to where you think about it). These three are
// the page's NEW knobs. Notify defaults ON (a silent Pomodoro is a broken
// promise — the countdown's whole job is to interrupt you); the tracker
// link defaults OFF (it writes real distracted/break segments into your
// day, so it must be an informed opt-in, never a surprise).

inline bool pomodoroNotify()
{
    return QSettings()
        .value(QStringLiteral("pomodoro/notifyPhaseEnd"), true)
        .toBool();
}

inline void setPomodoroNotify(bool on)
{
    QSettings().setValue(QStringLiteral("pomodoro/notifyPhaseEnd"), on);
}

// v22 (owner request): DEFAULT ON. The link was born opt-in because gluing
// two clean machines together needed proving; it has since proved itself, and
// the off-by-default state made the app's headline feature — "press start and
// your planned block records itself" — something you had to discover in a
// checkbox. Note what changing a DEFAULT does and does not do: QSettings only
// falls back to it when the key is ABSENT, so this flips the switch for new
// installs and for anyone who never touched it, and silently respects the
// choice of anyone who did. That asymmetry is exactly why a default is the
// right place to make this change, and why it needs no migration code.
inline bool pomodoroDrivesTracker()
{
    return QSettings()
        .value(QStringLiteral("pomodoro/drivesTracker"), true)
        .toBool();
}

inline void setPomodoroDrivesTracker(bool on)
{
    QSettings().setValue(QStringLiteral("pomodoro/drivesTracker"), on);
}

// Where the mini timer was last dropped. Null point = never moved yet.
inline QPoint pomodoroMiniPos()
{
    return QSettings()
        .value(QStringLiteral("pomodoro/miniPos"), QPoint())
        .toPoint();
}

inline void setPomodoroMiniPos(QPoint pos)
{
    QSettings().setValue(QStringLiteral("pomodoro/miniPos"), pos);
}

// ---- the window itself (v23) ------------------------------------------------
// Per-device by definition, and the cleanest example in this file of the
// data.json / QSettings line: where your window sits and whether the rail is
// showing are facts about THIS SCREEN, not about your plan. Two machines
// SHOULD disagree — a laptop and a desktop have nothing to say to each other
// about window size — which is exactly what QSettings gives us for free.

// Geometry is stored as the OPAQUE blob QWidget::saveGeometry() produces,
// never as four integers. That blob carries what x/y/w/h cannot: which screen
// the window was on, whether it was maximized or full-screen, the DPI it was
// laid out at, and a version tag Qt uses to reject data it doesn't recognise.
// Store four numbers instead and a maximized window comes back un-maximized —
// silently, and only for the people who maximize, which is the worst kind of
// bug to hear about. No getter validation here on purpose: this value is not
// ours to repair, and Qt's own restoreGeometry() is the only thing that can
// judge it. It returns false on garbage, and MainWindow treats false as
// "no memory" — the repair-on-read rule, delegated to the one code that can
// actually do the reading.
inline QByteArray windowGeometry()
{
    return QSettings().value(QStringLiteral("window/geometry")).toByteArray();
}

inline void setWindowGeometry(const QByteArray& blob)
{
    QSettings().setValue(QStringLiteral("window/geometry"), blob);
}

// The sidebar's default is the CALLER's business, so the getter takes it: a
// phone-sized screen starts with the rail folded, a desktop starts with it
// open, and neither of those is a fact this file knows. QSettings falls back
// to a default only when the key is ABSENT, so this reads as: "whatever you
// chose on this machine — and on a machine you've never chosen on, whatever
// suits its screen." Same asymmetry that made the pomodoro default flip safe
// in v22.
inline bool sidebarVisible(bool fallback)
{
    return QSettings()
        .value(QStringLiteral("window/sidebarVisible"), fallback)
        .toBool();
}

inline void setSidebarVisible(bool on)
{
    QSettings().setValue(QStringLiteral("window/sidebarVisible"), on);
}

// ---- agenda alarms ----------------------------------------------------------
// Default ON, same reasoning as the Pomodoro toast: a planner that stays
// silent at the exact moment the plan begins isn't finishing its job.
// (The Pomodoro keys sit above under their own banner; this one is the
// AGENDA speaking, so it gets its own.)

inline bool blockStartNotify()
{
    return QSettings()
        .value(QStringLiteral("agenda/notifyBlockStart"), true)
        .toBool();
}

inline void setBlockStartNotify(bool on)
{
    QSettings().setValue(QStringLiteral("agenda/notifyBlockStart"), on);
}

// (The week-snap FORMULA itself lives in Stats.h as stats::weekStart —
// it's pure date math the domain layer needs, and the domain must never
// include this file: QSettings is UI-machine state, and a pure summarizer
// that reads it stops being a function of its arguments.)

// ---- needs-a-block ----------------------------------------------------------
// (needs-a-block addendum §B–§E.) Everything in this section is TASTE — what
// deserves a nudge, when the review comes back, this machine's rhythm. The
// FACTS the feature produces (dismissedUntil, dismissCount) live on Task and
// sync; these knobs do not, and two devices may legitimately disagree.
//
// The include direction is the same story as always: this file includes the
// pure headers (TaskCoverage.h, ReturnPolicy.h); they never include this.
// The domain judges; prefs:: only remembers which knobs you chose.

inline coverage::Rule needsBlockRule()
{
    QSettings s;
    coverage::Rule r;
    r.flagUrgent = s.value(QStringLiteral("needsBlock/flagUrgent"), true).toBool();
    r.flagMedium = s.value(QStringLiteral("needsBlock/flagMedium"), false).toBool();
    r.flagLow    = s.value(QStringLiteral("needsBlock/flagLow"),    false).toBool();
    // Repair on read: the window is 0 (off) or a sane handful of days.
    r.dueWithinDays =
        qBound(0, s.value(QStringLiteral("needsBlock/dueWithinDays"), 3).toInt(), 60);
    return r;
}

inline void setNeedsBlockRule(const coverage::Rule& r)
{
    QSettings s;
    s.setValue(QStringLiteral("needsBlock/flagUrgent"),    r.flagUrgent);
    s.setValue(QStringLiteral("needsBlock/flagMedium"),    r.flagMedium);
    s.setValue(QStringLiteral("needsBlock/flagLow"),       r.flagLow);
    s.setValue(QStringLiteral("needsBlock/dueWithinDays"), r.dueWithinDays);
}

inline coverage::Escalation needsBlockEscalation()
{
    QSettings s;
    coverage::Escalation e;
    e.decisionAfter =
        qBound(1, s.value(QStringLiteral("needsBlock/escalateAfter"), 3).toInt(), 50);
    e.urgentOnly =
        s.value(QStringLiteral("needsBlock/escalateUrgentOnly"), true).toBool();
    // pinAfterExtra stays at its struct default (3) — one knob fewer until
    // real use asks for it; the second-consumer rule cuts both ways.
    return e;
}

inline void setNeedsBlockEscalation(const coverage::Escalation& e)
{
    QSettings s;
    s.setValue(QStringLiteral("needsBlock/escalateAfter"),      e.decisionAfter);
    s.setValue(QStringLiteral("needsBlock/escalateUrgentOnly"), e.urgentOnly);
}

// The two clocks (addendum §C) — same value type, different keys, different
// defaults, deliberately never wired together. Review: daily 06:00.
// Dismissal: 21:00, the owner's "while I plan tomorrow" moment.

inline ReturnPolicy readReturnPolicy(const QString& prefix,
                                     ReturnPolicy fallback)
{
    QSettings s;
    ReturnPolicy p;
    p.mode = returnModeFromString(
        s.value(prefix + QStringLiteral("/mode"),
                returnModeToString(fallback.mode)).toString());
    p.time = QTime::fromString(
        s.value(prefix + QStringLiteral("/time"),
                fallback.time.toString(QStringLiteral("HH:mm"))).toString(),
        QStringLiteral("HH:mm"));
    if (!p.time.isValid())
        p.time = fallback.time; // repair, as always
    p.hours = qBound(1, s.value(prefix + QStringLiteral("/hours"),
                                fallback.hours).toInt(), 48);
    return p;
}

inline void writeReturnPolicy(const QString& prefix, const ReturnPolicy& p)
{
    QSettings s;
    s.setValue(prefix + QStringLiteral("/mode"),  returnModeToString(p.mode));
    s.setValue(prefix + QStringLiteral("/time"),  p.time.toString(QStringLiteral("HH:mm")));
    s.setValue(prefix + QStringLiteral("/hours"), p.hours);
}

inline ReturnPolicy reviewReturnPolicy()
{
    ReturnPolicy def;
    def.mode = ReturnPolicy::Mode::AtTime;
    def.time = QTime(6, 0);
    return readReturnPolicy(QStringLiteral("needsBlock/review"), def);
}
inline void setReviewReturnPolicy(const ReturnPolicy& p)
{
    writeReturnPolicy(QStringLiteral("needsBlock/review"), p);
}

inline ReturnPolicy dismissReturnPolicy()
{
    ReturnPolicy def;
    def.mode = ReturnPolicy::Mode::AtTime;
    def.time = QTime(21, 0);
    return readReturnPolicy(QStringLiteral("needsBlock/dismiss"), def);
}
inline void setDismissReturnPolicy(const ReturnPolicy& p)
{
    writeReturnPolicy(QStringLiteral("needsBlock/dismiss"), p);
}

// The gate (addendum §E). gateEnabled is the on/off; lastReview is the
// DEVICE-LOCAL "when did I last look" the gate derives its open/closed
// state from (part 2). Per-device on purpose: if this synced, opening the
// phone at noon would skip the review because the laptop looked at 07:00 —
// the pause belongs to each machine you plan from.

inline bool needsBlockGateEnabled()
{
    return QSettings()
        .value(QStringLiteral("needsBlock/gateEnabled"), true).toBool();
}

inline void setNeedsBlockGateEnabled(bool on)
{
    QSettings s;
    s.setValue(QStringLiteral("needsBlock/gateEnabled"), on);
    // Disabling forgets the last look, so re-enabling re-arms honestly
    // instead of resuming a stale "already reviewed" state (addendum §E).
    if (!on)
        s.remove(QStringLiteral("needsBlock/lastReview"));
}

inline QDateTime needsBlockLastReview()
{
    return QSettings()
        .value(QStringLiteral("needsBlock/lastReview")).toDateTime();
}

inline void setNeedsBlockLastReview(const QDateTime& when)
{
    QSettings().setValue(QStringLiteral("needsBlock/lastReview"), when);
}

// ---- catch-up: missed blocks (v26.2) --------------------------------------
// TASTE, all of it: what share of a block counts as "it happened", how far
// back to look, and when to be asked. The FACTS the feature produces
// (Event::outcome, movedToId) live in data.json and sync — the same split as
// needs-a-block above.
//
// Clamped on the way out like every other getter here. minPercent is bounded
// to 0..100 because a hand-edited 500 would mark every block in history as
// missed; lookBackDays has a floor of 1 because zero would silently disable
// the feature while the checkbox still said it was on.

inline missed::Rule missedRule()
{
    QSettings settings;
    missed::Rule r;
    r.minPercent = qBound(
        0, settings.value(QStringLiteral("catchup/minPercent"),
                          r.minPercent).toInt(), 100);
    r.lookBackDays = qBound(
        1, settings.value(QStringLiteral("catchup/lookBackDays"),
                          r.lookBackDays).toInt(), 60);
    return r;
}

inline void setMissedRule(const missed::Rule& r)
{
    QSettings settings;
    settings.setValue(QStringLiteral("catchup/minPercent"), r.minPercent);
    settings.setValue(QStringLiteral("catchup/lookBackDays"), r.lookBackDays);
}

// The two moments the card may appear. Both default ON: the morning one
// recovers the day, the evening one replans what's left. They answer
// different questions with the same data, which is why they are two flags
// and not one enum — "both" is the useful default, not an edge case.
inline bool catchUpOnOpen()
{
    return QSettings()
        .value(QStringLiteral("catchup/onOpen"), true).toBool();
}

inline void setCatchUpOnOpen(bool on)
{
    QSettings().setValue(QStringLiteral("catchup/onOpen"), on);
}

inline bool catchUpAtEndOfDay()
{
    return QSettings()
        .value(QStringLiteral("catchup/atEndOfDay"), true).toBool();
}

inline void setCatchUpAtEndOfDay(bool on)
{
    QSettings().setValue(QStringLiteral("catchup/atEndOfDay"), on);
}

// How far forward the proposer may look when the block serves no deadline.
inline int catchUpHorizonDays()
{
    return qBound(1, QSettings()
                         .value(QStringLiteral("catchup/horizonDays"), 14)
                         .toInt(), 90);
}

inline void setCatchUpHorizonDays(int days)
{
    QSettings().setValue(QStringLiteral("catchup/horizonDays"), days);
}

} // namespace prefs
