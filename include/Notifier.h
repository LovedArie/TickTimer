#pragma once
// ---------------------------------------------------------------------------
// Notifier — how this app SPEAKS, as an interface (v30.6).
//
// THE PROBLEM THIS SOLVES. Notification delivery is the one thing in
// TickTimer that genuinely differs per platform, and until v30.6 the app
// had exactly one implementation of it — a frameless always-on-top QWidget
// parked top-right of the primary screen — wired directly into five call
// sites in MainWindow. On a desktop that is correct and unsuppressible,
// which was the entire point of v19.9. On Android it is meaningless: an
// ordinary app cannot draw a floating window, and the process is frozen
// when the alarm is supposed to ring.
//
// THE ALTERNATIVE REJECTED, explicitly, because it is the obvious one:
// `#ifdef Q_OS_ANDROID` at each call site. There are THREE targets here —
// desktop, Android, and the WebAssembly build for iPhones — so a two-branch
// fork becomes a three-branch fork, five times over. Worse, an #ifdef is
// not a seam: no test can reach the branch it did not compile, so the
// Android path would be verified exactly as often as someone remembered to
// build an APK. An interface with one selection point can at least be
// pointed at a fake.
//
// A DELIBERATE DEPARTURE FROM HOUSE DOCTRINE, and it should be argued
// rather than smuggled. design-addendum-android.md §3.30 says compact mode
// is decided by GEOMETRY, not platform — "platform is the wrong proxy for
// the actual variable" — and that rule is right for LAYOUT, where the real
// variable is how much room a container was handed. It is wrong here. The
// real variable for a notification is what the operating system will let
// this process do while it is not running, and that is not something a
// width can answer. A phone-width window on a desktop still gets the
// desktop toast, which is exactly the outcome the geometry rule would get
// wrong.
//
// OWNERSHIP. Notifier is NOT a QObject: it has no signals, no slots, no
// parent, and no reason to sit in the meta-object system. So it is owned
// the plain C++ way, by std::unique_ptr in MainWindow, and destroyed when
// MainWindow is — deterministically, at the closing brace of the
// destructor, in reverse order of construction. Qt's parent-child
// ownership is the right tool when a QObject's lifetime must be tied to a
// widget tree it lives in; a unique_ptr is the right tool when one object
// exclusively owns another and nothing else may. This is the latter, and
// the virtual destructor below is what makes deleting through the base
// pointer defined behaviour rather than a coin flip.
// ---------------------------------------------------------------------------

#include "Alarms.h"
#include "NotificationToast.h" // ToastSpec — the value a notifier speaks

#include <QVector>

#include <memory>

class QObject;

class Notifier
{
public:
    virtual ~Notifier() = default;

    // Say this NOW. Five callers: the two agenda voices, the Pomodoro, the
    // affordability nudge and the morning check-in.
    virtual void announce(const ToastSpec& spec) = 0;

    // Hold this forward schedule. Called on every republish, and it
    // REPLACES rather than appends — see AlarmService::derive.
    virtual void publish(const QVector<alarms::Alarm>& schedule) = 0;

    // Will publish() also DELIVER these at their instant, or only store
    // them? This is the one real capability difference between the
    // platforms, and hoisting it into a question keeps the answer in one
    // place instead of five.
    //
    //   false (desktop) — the app's own QTimer is the only thing that will
    //     ever ring, so MainWindow must announce() when AlarmService says
    //     an alarm came due.
    //   true (Android)  — the OS holds the schedule and will post the
    //     notification itself, with this process dead if need be. The
    //     in-process announce() must then be SUPPRESSED, or an alarm that
    //     fires while the app happens to be open arrives twice.
    virtual bool deliversSchedule() const = 0;

    // Are we allowed to speak at all? Android gates notifications behind a
    // runtime permission (POST_NOTIFICATIONS, mandatory since API 33);
    // every desktop answers yes. Asked, never assumed: a notifier that
    // cannot speak should be visible as a fact, not as silence.
    virtual bool canSpeak() const = 0;

    // Ask the user for permission, if this platform has such a notion and
    // has not been answered yet. A no-op everywhere it is meaningless.
    virtual void requestPermission() {}
};

namespace notify
{

// THE SINGLE SELECTION POINT. This function's body is the only place in
// TickTimer that asks which platform it is running on for notification
// purposes — the whole reason the interface above exists. `parent` is
// handed to whatever QObjects an implementation needs internally (the
// desktop chime owns QSoundEffects); the Notifier itself is not one.
std::unique_ptr<Notifier> make(QObject* parent);

} // namespace notify
