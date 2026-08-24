# Design addendum — notifications that survive the app closing (v30.6)

*Session: "I tested the Android version yesterday. There's no push
notification." Companion to `design-doc.md` §3, the block-alarms addendum
(whose service this rewrites) and the android addendum (whose §3.30 this
deliberately departs from). Discharges the Android half of the gap
`docs/WEB.md` has been naming since v30.4.*

---

## A. What was actually wrong, in three layers

The report was one sentence and the cause was three, stacked. Worth
separating, because only the first is a bug and the third is the feature.

**The tray gate.** `MainWindow::setupNotifications()` opened with
`if (!QSystemTrayIcon::isSystemTrayAvailable()) return;`. Android has no
system tray, so the function returned at its first line and took the
Pomodoro chime, the block alarm and the block-finished toast with it.
Nothing logged anything, because a guard doing exactly what it says is not
an error.

That guard was correct in v19.8, when a notification *was* a tray balloon:
Qt routes them through `QSystemTrayIcon::showMessage` and most platforms
only deliver them for a visible tray icon, so no tray genuinely meant no
notifications. v19.9 then replaced the balloon with `NotificationToast` — a
window we paint, which no pipeline can decline — and the guard was never
revisited. **A feature check must guard the feature it names.** This one
said "is there a tray?" and meant "can we speak at all?", and those stopped
being the same question eleven versions ago.

**The toast is a desktop window.** `NotificationToast` is parentless
`Qt::Tool | FramelessWindowHint | WindowStaysOnTopHint`, parked top-right of
the primary screen. An ordinary Android app cannot draw a floating window at
all — that needs the `SYSTEM_ALERT_WINDOW` overlay permission, which is a
thing users grant by hand and sideloaded apps get asked about with
suspicion. So even with the gate removed, the toast can only ever be an
in-app card.

**The schedule lived in our process.** This is the real one.
`BlockAlarmService` armed a `QTimer`, and its own comment named the property
that dies: the hour-capped nap exists so "an hourly self-check heals clock
jumps and suspend/resume without any platform-specific wake signals". It
heals nothing when the process is frozen, which is what Android does to a
backgrounded app. **An alarm that only fires while you are looking at the
app is not an alarm.**

## B. The word "push", and why this is not that

The report said *push notification*, and separating the two decided the
whole shape of the work.

A **local notification** is scheduled ahead of time by the app and held by
the phone's own `AlarmManager` + `NotificationManager`. It fires with the
app closed, needs no network, no Google account and no Firebase. A **push**
notification is a server telling Google's servers to wake a phone, and it is
only needed when the server knows something the phone does not.

Every alarm in this app is derivable from data already on the device. So
push was rejected — not as too hard, but as the wrong mechanism: it would
drag in a Firebase project, Play Services on every device and a sender key
in `ticktimer-server`, permanent infrastructure, to deliver facts the phone
was already holding. It stays available for the one case that would earn it
(§G).

Reading the owner's own phone with `adb` settled that this is ordinary work
rather than a Qt limitation. Daylio and TickTick both request
`POST_NOTIFICATIONS`, both declare `RECEIVE_BOOT_COMPLETED`, and both were
caught mid-notification on channels with `importance=4` — the flag that
makes a notification a heads-up banner with sound instead of a quiet line in
the shade. TickTimer requested none of them and had registered no channel at
all. **Diagnosis by comparison with a working neighbour beat reasoning from
first principles, and cost four commands.**

## C. The inversion: a schedule is a value

The service used to own a timer and push event **ids** at fire time, with
`MainWindow` re-resolving them into text at the last possible moment. The
v19.7 addendum argues for that explicitly, and it was right: titles may have
been edited since, so resolving late is resolving correctly.

That argument inverts the moment the app might not exist at fire time.
`Alarms.h` now derives a forward window of **finished** alarms — key,
instant, title, body — and something else holds them. The one rule
everything follows from:

> **C++ must not need to run at fire time.**

The rejected alternative here is the tempting one: let the Android receiver
start the app, re-read `data.json` and decide what to say, preserving the
ids-not-text rule exactly. Rejected for a 2–4 second cold start per alarm,
the battery blame that comes with it, and a failure mode invisible until it
bites — the app would be resurrecting itself at 06:00 to compose a sentence.

Pre-rendering is safe for a reason worth writing down: **every edit path
goes through `AppData::changed()`, which republishes the entire window**, so
the copy Android holds is never staler than the last change anyone made. The
ids-not-text rule was never about ids; it was about freshness, and
republishing buys the same freshness by a different route.

## D. Two mute rules that had to become pure to survive

`MainWindow` used to drop a block alarm when you were already tracking that
block — the own-hands rule, the cousin of the Pomodoro's `skip()`. And the
block-*finished* toast fired from `TrackerService`'s exit door, so it was by
construction only ever about something being tracked.

Both are now one parameter of `alarms::upcoming`: `trackedEventId`. A block
being tracked contributes its **end** alarm instead of its **start** alarm,
and everything else contributes its start. That single parameter does both
jobs, and both became testable without a service.

They had to move because a rule enforced at fire time cannot survive the app
being closed — there is no `MainWindow` to enforce it. The mute now works
by *republishing a schedule that omits the alarm*, and starting to track
emits `changed()`, so the republish happens at exactly the moment it must.
**The rule works precisely in the case where it matters.**

Blocks with a settled `BlockOutcome` (Done, Moved, Dropped) are skipped too:
a decision already made is not something to be alarmed about.

## E. One seam, one selection point

`Notifier` is an interface with three questions: say this now
(`announce`), hold this schedule (`publish`), and — the one that carries
the real difference — **will publishing also deliver?**
(`deliversSchedule`).

Desktop answers no: the app's own `Qt::PreciseTimer` is the only thing that
will ever ring, so `publish()` is an explicit empty body and `MainWindow`
announces when the service says an alarm came due. Android answers yes: the
OS holds the schedule and will post it with this process dead, so the
in-process voices must stay **quiet** about anything scheduled — otherwise
an alarm landing while the app happens to be open arrives twice. Hoisting
that into one question kept the answer in one place instead of five.

The rejected alternative was `#ifdef Q_OS_ANDROID` at each of the five
notification call sites. There are three targets here — desktop, Android and
the WebAssembly build for iPhones — so a two-branch fork becomes a
three-branch fork, five times over. Worse: **an `#ifdef` is not a seam.** No
test can reach a branch it did not compile, so the Android path would have
been verified exactly as often as someone remembered to build an APK.
`notify::make()` is now the only place in the codebase that asks what
platform it is for notification purposes.

**The departure from §3.30, argued rather than smuggled.** The android
addendum established that compact mode is decided by geometry, not platform,
because "platform is the wrong proxy for the actual variable". That is right
for *layout*, where the real variable is how much room a container was
handed. It is wrong here. The real variable for a notification is what the
OS will let this process do while it is not running, and no width answers
that. A phone-width window on a desktop still gets the desktop toast —
which is exactly the outcome a geometry rule would get wrong.

## F. The Android side, and what each piece defends

`android/` is a new tree, and taking ownership of `AndroidManifest.xml` is
itself the decision: Qt generated one for a year, and a generated manifest
is one you can never add to. Three choices in it are load-bearing.

**`USE_EXACT_ALARM`, not `SCHEDULE_EXACT_ALARM`.** Since Android 14 the
latter is denied by default: the app must detect that, send the user into a
Settings screen and handle refusal — an entire permission flow.
`USE_EXACT_ALARM` is granted at install with no prompt, and is restricted to
apps whose core function is an alarm clock or calendar, which TickTimer's
block alarms literally are. That restriction is enforced by Play review,
which this app never faces: it is sideloaded to a handful of known people
and will never be in a store. TickTick, on the owner's own phone, made the
same call; Daylio took the other road and needed a battery-optimisation
exemption on top of it.

**A boot receiver, which nobody asks for until it is too late.** Android
discards every scheduled alarm on reboot, silently. Without `BootReceiver`
the alarms would vanish at the first restart and never return — a failure
delayed by days and indistinguishable from "the feature never worked". It
re-arms from the copy `TickNotifier` persisted, so it needs neither Qt nor
`data.json` nor to launch the app. It listens for `MY_PACKAGE_REPLACED` as
well, because sideloading a new APK clears alarms the same way a reboot
does, and for this project that happens often.

**Keeping the `%%INSERT_...%%` tokens.** The manifest is a copy of Qt's
template, and androiddeployqt substitutes the version code, version name and
native library name into those placeholders. Hard-code one and you have
created a fourth version seam of the kind `installer/ticktimer.iss` already
teaches about — right exactly once. So
`test_domain::androidManifestKeepsTheVersionTokens` asserts the holes are
still there, alongside the three permissions whose loss is equally silent.
**Mechanism beats intention**, which is the same sentence that file has
carried since v26.8.

The permission request lives in Java rather than C++, and that is a
dependency decision, not a convenience. Qt's public `QPermission` classes
cover camera, microphone, bluetooth, contacts, calendar and location — and
*not* notifications, so the C++ route would be
`QtAndroidPrivate::requestPermission` out of a private header, one Qt
upgrade from breaking. We are already writing Java; four lines of it there
cannot rot. The answer is never awaited: the app re-asks `canNotify()` when
it next matters, which needs no callback and no override of Qt's Activity.

## G. Limits, named

**The affordability nudge is not scheduled, deliberately.** Its value is the
live numbers in the sentence — "needs 10h, you have 6h before Friday" — and
those are computed from current data at sweep time. Handing it to
`AlarmManager` would mean pre-rendering arithmetic that is stale by the time
it buzzes. It remains in-app only. The condition that would justify building
it properly is clear: either a periodic background job running the real
sweep, or moving the computation server-side and using push — and the latter
is the one case in this app where push would genuinely earn its
infrastructure, because a shared plan changed by someone else is a fact the
phone does not have.

**"…and the Pomodoro is paused" was lost.** The old block-finished toast
could say whether the link had stopped the timer too, because it composed
its sentence at fire time with `PomodoroLink` in reach. A pre-rendered alarm
cannot know. The block-finished alarm now says only that the planned time is
up. One sentence traded for the notification existing at all when the app is
closed.

**The small icon was written up here as a cosmetic debt, and it was a
crash.** The first build passed `getApplicationInfo().icon` to
`setSmallIcon`, reasoning that the launcher icon is always present. It is
not: Qt strips the icon attribute from the manifest when no app icon is
configured, so the call returned **0**, and `setSmallIcon(0)` throws
`IllegalArgumentException` — which killed the receiver process every time an
alarm arrived. The alarm fired correctly, Android cold-started a process for
it correctly, and the user saw nothing.

It is fixed with a real white-silhouette vector
(`android/res/drawable/ic_stat_ticktimer.xml`), looked up by *name* at
runtime rather than through the generated `R` class, with a framework
drawable as a fallback so a missing asset can never crash again.

The honest record: this section originally called it cosmetic on the
strength of reading the API, before anything had run. **A claim about a
platform is worth nothing until the platform has executed it** — the same
lesson `docs/TROUBLESHOOTING.md` already carries for OpenSSL and for the
compact layout, arrived at a third time by the same route.

**The chime is the system notification sound on Android.** The app's own
WAVs live in Qt's resource system, which an Android notification channel
cannot reach.

**Samsung's One UI app-sleep still wins if it wants to.**
`setExactAndAllowWhileIdle` handles Doze; it does not handle the separate
OEM layer that puts unused apps to sleep. That is a manual Settings
exclusion, documented rather than coded, because asking for a
battery-optimisation exemption on first launch is a worse first impression
than a line in a doc.

**WebAssembly is unchanged and still fenced.** A closed tab runs no timers
either, and the Web Push work that would fix it is its own project.
`docs/WEB.md` now says Android is done and WASM is not, instead of pairing
them.

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| pure brain | `Alarms.h` **(new)** | the schedule as a value: `upcoming`, `forPhase`, `forCheckIn`, `dueBetween`, `nextAfter` |
| policy | `AlarmService.{h,cpp}` (was `BlockAlarmService`) | owns clock, mark, timer; derives and **publishes**; two injected providers |
| policy | `CheckInService` | `lastOffered()` — the read side of the ledger it already owned |
| seam | `Notifier.h` **(new)** | `announce` / `publish` / `deliversSchedule` / `canSpeak`; `notify::make` is the one `#ifdef` |
| glass | `DesktopNotifier.{h,cpp}` **(new)** | v19.9's toast and three-tier chime, moved out of `MainWindow` |
| glass | `AndroidNotifier.{h,cpp}` **(new)** | JNI to `TickNotifier`; `deliversSchedule() == true` |
| glass | `NotificationToast.h` | `ToastSpec::Sound` — the chime becomes part of the value |
| glass | `MainWindow.{h,cpp}` | tray gate removed; three notification clients become two; owns the `Notifier` by `unique_ptr` |
| glass | `DebugPanel.{h,cpp}` | the "Block alarms" group: poll, republish, show the schedule |
| Android | `android/AndroidManifest.xml`, `TickNotifier.java`, `AlarmReceiver.java`, `BootReceiver.java` **(new)** | permissions, channel at importance 4, `AlarmManager`, reboot survival |
| build | `CMakeLists.txt` | `QT_ANDROID_PACKAGE_SOURCE_DIR`, `QT_ANDROID_TARGET_SDK_VERSION` pinned to 35, `TICKTIMER_MANIFEST_PATH` |
| tests | `test_domain.cpp`, `test_ui.cpp` | ten pure `alarms::` slots, six service slots, the manifest guard, the panel group |
