# Learning queue

Topics raised by real changes in this repo that deserve a proper sit-down,
rather than the three lines they got in passing. Each entry names **where in
this codebase it actually bit**, so the explanation has something concrete to
point at.

---

## Move semantics and rvalue references

**Raised by:** v29.3, `Event::movedToId` (a `QString`) becoming
`Event::movedToIds` (a `QStringList`).

`Event` holds two container members by value now — `QVector<Segment> segments`
and `QStringList movedToIds`. Every time `m_events` reallocates, every `Event`
in it is relocated, and what that costs depends on whether the container is
*copied* or *moved*.

Questions to work through: what a move actually does to the source object; why
a moved-from object must still be destructible; how Qt's implicit sharing
(copy-on-write) interacts with all this, and why it means `QStringList` copies
are cheap in a way `std::vector<std::string>` copies are not; and when
`std::move` earns its keystrokes versus when the compiler was already going to
elide the copy.

Related landmine already in the codebase: `AppData.cpp` takes **copies** of
`src->taskId` and friends before calling `appendGuardedEvent`, because the
append can reallocate and invalidate `src`. That is a lifetime problem, not a
move problem — but the two are easy to confuse and worth separating deliberately.

---

## RAII and destructor ordering

**Raised by:** `AppData::Batch`, which v29.3 leans on harder — undoing a
three-piece split is four mutations that a listener must see as one
`changed()`.

`Batch batch(*this);` is a variable whose whole purpose is its lifetime. Its
constructor suppresses emission, its destructor restores it and fires once, and
nothing in `undoReschedule` ever calls "end the batch" — the closing brace does
it. That is RAII: the scope *is* the transaction.

Questions to work through: the exact moment a destructor runs and in what order
when several objects share a scope; why this pattern survives an early `return`
(and why that is the entire point); what happens if a destructor needs to do
something that can fail; and how the same shape underlies `QMutexLocker`,
`std::lock_guard`, and Qt's own signal blockers.

Compare against the alternative the codebase did *not* choose: a manual
begin/end pair, which is one early return away from leaving the whole app with
signals permanently suppressed.

---

## Smart pointers vs Qt parent-child ownership — and which one `Notifier` gets

**Raised by:** v30.6, `MainWindow::m_notifier` — the first thing this codebase
owns with `std::unique_ptr` rather than by handing it a `QObject` parent.

Almost every object in this app is owned the Qt way: `new PomodoroEngine(this)`
hands the engine to `MainWindow`, and Qt deletes it when the parent dies. That
is real ownership, not a convention — `QObject`'s destructor walks its children
and deletes them. It works because those objects are `QObject`s, which they are
because they emit signals.

`Notifier` emits nothing. It is an abstract base class with three virtual
functions and no meta-object at all, so there is no parent to hand it to and no
reason to make it a `QObject` just to be owned. `std::unique_ptr<Notifier>`
says the honest thing: exactly one object owns this, ownership cannot be
copied, and it is destroyed at the closing brace of `~MainWindow`. The
`virtual ~Notifier() = default;` in the header is what makes deleting a
`DesktopNotifier` through a `Notifier*` defined behaviour rather than
undefined — without it the derived destructor never runs.

Questions to work through: what `unique_ptr` actually stores and why it costs
nothing over a raw pointer; why it cannot be copied but can be moved, and what
`std::move` does to the source; what `std::make_unique` buys over `new`; what
exactly goes wrong when a base class destructor is not virtual and why the
compiler will not warn you; and where the boundary really sits — when *should*
a new class be a `QObject` child instead.

Compare against the alternative: making `Notifier` a `QObject` purely to park
it under `MainWindow`. It would work, and it would put a class in the
meta-object system that has nothing to say to it — paying moc, a vtable it
does not need, and a reader's time wondering what signal they missed.

---

## JNI, `QJniObject`, and the C++/Java boundary

**Raised by:** v30.6, `src/AndroidNotifier.cpp` calling into
`android/src/org/ticktimer/app/TickNotifier.java` — the first non-C++ code in
this project.

Qt ships no notification API, so the only way to reach Android's
`NotificationManager` and `AlarmManager` is to call Java from C++. `QJniObject`
is Qt's wrapper over JNI, the C interface that lets native code find a Java
class, look up a method and invoke it. The strings in that file are not
decoration: `"(Landroid/content/Context;Ljava/lang/String;)V"` is a JNI type
signature, spelling out the parameter types and return type of the method being
called, and it is checked at *runtime* on the phone rather than at compile time
here. A typo in one is a crash on the device with nothing useful on this
machine — which is why that file names its class in a single constant.

The other half is lifetime, and it is genuinely different from anything else in
this codebase. A `jobject` is a handle owned by the Java virtual machine's
garbage collector, not by C++. `QJniObject` holds a global reference so the
collector cannot move the object out from under it, and releases that reference
in its destructor — RAII again, applied across a language boundary.

Questions to work through: what the JVM's local vs global references are and
why a long-lived handle needs the second kind; how `QJniObject::fromString`
and `.object()` relate; what "the context" actually is in Android and why
`QNativeInterface::QAndroidApplication::context()` sometimes returns an
Activity and sometimes not; and why the schedule crosses this boundary as a
JSON *string* rather than as a structured object.

---

## Smaller notes, already answered in passing

- **Why the compatibility mirror is safe in `JsonStore` but not in `Event`.**
  Not a C++ question — a design one. Two fields holding the same fact are only
  dangerous if something can observe them *disagreeing*. In storage, one door
  writes both in a single instant and the loader always prefers the list, so
  there is no window. In memory, an `Event` is handed to arbitrary readers for
  arbitrary durations, and any of them could hold a stale opinion. The rule:
  duplicate a fact only where you control every read and every write.

---

## Translation units, linkage, and why moving a function into a header needs `inline`

Came up in v31.2, moving the conflict-box sentences out of `SyncDialog.cpp`
and into `Merge.h` so tests could reach them.

A **translation unit** is one .cpp file after the preprocessor has pasted in
every `#include`. The compiler sees TUs one at a time and knows nothing about
the others; the linker then joins the object files and must find exactly one
definition of every symbol that is used. That "exactly one" is the **One
Definition Rule**.

An **anonymous namespace** (`namespace { ... }`) gives everything inside it
*internal linkage*: the symbol is private to its TU, so two .cpp files can
each define `headingFor` with no collision. That is what the helpers had
while they lived in `SyncDialog.cpp` — and it is exactly why no test could
call them. Internal linkage is not a visibility convention; the name is
genuinely absent from every other TU.

Move such a function into a header and the problem inverts. A header is
pasted into every .cpp that includes it, so `Merge.h` included by
`SyncDialog.cpp`, `SyncService.cpp`, `test_domain.cpp` and the rest would
produce one definition of `renderClashes` per TU — several definitions of one
symbol, and the linker refuses. `inline` is the permission slip: it tells the
linker "you will see this definition many times, they are all identical,
collapse them into one." In modern C++ that is what `inline` is *for*; the
old "paste the body at the call site for speed" meaning is only a hint the
compiler is free to ignore.

So the whole `merge::` namespace is `inline` free functions in a header, and
that is not an accident of style: a header-only pure function can be called
from the domain suite, which links no widgets at all. The rule of thumb this
codebase follows — extract the judgement, put it in a header, pin it with
microsecond tests — depends on this mechanism to work.

Worth working through next: why `static` at namespace scope means the same
thing as an anonymous namespace (and why the anonymous namespace is
preferred), and what `inline` does *not* promise — in particular that each TU
must see a byte-identical definition, which is why editing a header requires
rebuilding every file that includes it.

---

## Event filters, and why an *ignored* event keeps travelling

Came up in 31.2.0 (B8): a mouse wheel over a hovered dropdown was editing
data, and the fix had to cover 37 controls in nine files plus every control
written after it — so it could not be a line in 37 constructors.

An **event filter** is an object that gets shown another object's events
*before* that object sees them. `target->installEventFilter(spy)` makes `spy`
see `target`'s events; `qApp->installEventFilter(spy)` makes it see
*everything*, because every event in a widget program is delivered through
`QApplication::notify`. `WheelGuard` and `installCompactDialogFitter` both take
that second form, and the reason is the same: the alternative is remembering,
in every future constructor, to opt in.

`bool eventFilter(QObject* watched, QEvent* event)` answers one question:
**has this event been dealt with?** Return `true` and the target never sees it.
Return `false` and delivery continues as normal.

The subtle half is `accept()` / `ignore()`, which is a *different* flag from
that return value, and the wheel guard needs both:

- `event->ignore()` marks the event as "not handled here".
- Returning `true` stops it reaching the control.

Together they mean "this control does not take this wheel event" — and because
the event is ignored rather than consumed, `QApplication::notify` offers it to
the parent, then that parent's parent, until something accepts it. The
enclosing scroll area does, so the page scrolls. Had the guard returned `true`
while leaving the event *accepted*, the wheel would have vanished instead:
the combo would be safe and the page would be frozen under your hand.

One more piece of the same fix is worth keeping: `QComboBox` has focus policy
`Qt::WheelFocus`, and Qt gives focus to such a widget *before* the event
reaches any filter — so a `hasFocus()` test alone would always answer "yes" and
guard nothing. The guard therefore downgrades that policy to `StrongFocus` on
`QEvent::Polish`, the moment a widget is built and about to be shown. A check
is only as good as what it can still see.

Worth working through next: the whole delivery path
(`QCoreApplication::sendEvent` → `notify` → filters → `event()` → handler),
where `QGestureManager` hooks into it (the trap that makes a touch drag
impossible inside a scrolling page), and why a *spontaneous* event — one from
the window system, as `QTest::wheelEvent` produces — propagates while a
hand-made one sent straight to a widget does not.

---

## Nested event loops: `menu.exec()`, `dialog.exec()`, and decisions that wait

Also 31.2.0, and the reason two features are written as "ask, then act after"
rather than as connected lambdas.

`QApplication::exec()` is *the* event loop: take an event, deliver it, repeat.
`QMenu::exec()` and `QDialog::exec()` start **another one, inside the first**.
Your call does not return until the menu closes or the dialog is dismissed —
while the app stays alive, because that inner loop keeps delivering events.

So code written after `exec()` runs *later*, when the thing has closed, and
that is exactly what the block menu wants: it asks which action was chosen,
`exec()` returns the answer, and only then does the page start moving mode. The
same shape in `onEventClicked`: the dialog is shown, and a `MoveOrSwap` result
is acted on after it is gone. Starting a mode that claims the next tap from
*inside* a loop that is still eating taps would be a mode nobody could use.

The trap the project already paid for lives one step further in:
`deleteLater()` queues a deletion that only the loop level which posted it
processes, so a widget discarded before a nested `exec()` survives the whole
dialog — still parented, still painting, in the corner
(`ActivityDetailDialog::rebuildScheduleRows`). `hide()` before `deleteLater()`
is the cure.

Worth working through next: `QEventLoop` used directly (the pattern behind
`QSignalSpy::wait`), what "re-entrancy" costs a class whose method can be
re-entered through a nested loop, and why Qt's own docs recommend `open()`
with a signal over `exec()` for new code.

---

## A finger is not a mouse: touch events, synthesised mouse events, and the one you are allowed to take

Came up in 31.2.0 (§M.8a), when the owner asked for a two-finger tap to open a
block's menu on the phone.

A touchscreen does not produce mouse events. It produces **touch events**:
`TouchBegin` when the first finger lands, `TouchUpdate` as fingers move or more
land, `TouchEnd` when the last one lifts. Each carries a list of *points*, one
per finger. A plain widget never sees these unless it asks, with
`setAttribute(Qt::WA_AcceptTouchEvents)`.

Almost everything in this app is written against **mouse** events, though, and
it still works on the phone because Qt **synthesises** them: a one-finger touch
that no widget accepts is turned into a press, moves and a release. That is why
`AgendaWidget` can tell a phone from a desktop by asking
`event->source() != Qt::MouseEventNotSynthesized` — a synthesised press
remembers it came from a finger.

Here is the rule the two-finger tap had to be built around: **accepting a
`TouchBegin` switches synthesis off for that whole sequence.** Accept every
touch, and the one-finger tap, the hold and the drag all stop receiving the
mouse events they are made of. So `AgendaWidget::event()` refuses a
`TouchBegin` with one point (`ignore()`, and Qt goes on to make its mouse
press) and accepts only a sequence that *begins* with two points already down.
The price is honest and written into the addendum: two fingers that land far
enough apart in time arrive as a one-point `TouchBegin` and are missed. The
double-tap is the door that always works.

**That design did not survive the device** (see the next section).

### What the phone's own logs changed

On the Galaxy S21 all three gestures failed, and temporary `qInfo` lines read
back with `adb logcat` said why, instead of leaving a guess:

- a quick second tap's `TouchBegin` arrived, but Qt **never synthesised its
  mouse press** — so the double-tap code, built on presses, never saw it;
- the second finger of a two-finger tap was **never in the `TouchBegin`**; it
  joins in a later `TouchUpdate`, which a widget that refused the begin never
  receives;
- the hold likely cancelled itself: lifting releases the page's scroller, the
  scroller gives up the **mouse grab**, the widget receives `UngrabMouse`, and
  the code read that as "this became a scroll" and ended the lift it had just
  started.

So the calendar now **accepts every touch on a phone** and runs its gestures
from the touch events. The logic sits in four member functions
(`touchPressed`/`touchMoved`/`touchReleased`/`touchCancelled`) that both routes
call: real touch events on the phone, mouse events in the older tests. That is
the rule "one implementation, several callers" — the tests exercise the same
code the device runs, not a copy of it.

Two things are lost by leaving synthesis, and each is replaced on purpose. The
`UngrabMouse` signal no longer means anything (there is no mouse grab), so a
scroll is recognised by distance travelled, by `TouchCancel`, and by asking the
scroller its `state()`. And a touch point's `pressPosition()` turned out not to
be trustworthy for a finger that joins mid-sequence (the test helper even
reports a *stationary* finger at (0,0)), so the widget records where each finger
first landed itself, in a `QHash<int, QPoint>` keyed by the point's id — the
general lesson being: when an input's history matters, keep that history
yourself rather than trusting every sender to fill it in.

There is a second layer above all of this. `QScroller`, the page's flick
scrolling, recognises its gesture inside `QApplication::notify`, *before* any
widget's handler runs — which is why a drag that starts from a bare press never
reaches the calendar, and why a drag that starts from a one-second hold does
(the scroller has ruled out a pan by then). While the finger carries the block,
the widget releases the scroller with `QScroller::ungrabGesture`, and gives it
back on every exit and in the destructor. Forgetting to give it back does not
crash anything; it leaves a page that can never be scrolled again, silently.

And one small tool from the same change: **`QPointer<T>`**. The lifted block's
picture is a `QLabel` whose parent is the *window*, so the window may destroy it
while `AgendaWidget` still holds the address. A raw `QLabel*` would then point
at freed memory. A `QPointer` is told when its `QObject` is destroyed and reads
as null afterwards — it cannot dangle, which is exactly the guarantee a plain
pointer into someone else's ownership lacks.

Worth working through next: `Qt::AA_SynthesizeMouseForUnhandledTouchEvents`
and its mirror `AA_SynthesizeTouchForUnhandledMouseEvents`; how Qt's gesture
framework (`QGestureRecognizer`) sits between touch events and widgets; and why
`QPointer` works only for `QObject`s — what it would take to get the same
guarantee for a plain C++ object (`std::weak_ptr`, and what that asks of the
owner).
