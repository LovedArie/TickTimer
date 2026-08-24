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
