# Design addendum — the settings nav (v26.1)

Supersedes the *layout* half of `design-addendum-settings.md`. Everything
that document says about **which** preferences exist, what they default to,
and why they live in `QSettings` rather than `data.json` still stands
unchanged. This addendum is only about the shape of the dialog around them.

---

## A. The problem

`SettingsDialog` had grown to a 579-line constructor building a single flat
`QFormLayout`. Sections were faked with bold `QLabel`s carrying
`objectName("h2")`. Its own header comment had been warning about this
since v7:

> A settings dialog that hoards every knob in the app becomes the junk
> drawer nobody can find anything in.

By v26 it *was* the junk drawer. Three symptoms, in increasing order of
seriousness:

1. **Scrolling.** Agenda hours, an AI credential, and an escalation ladder
   sat in one column with no way to jump between them.
2. **A misfiled caveat.** The note *"Blocks outside these hours still
   show…"* was pinned to the bottom of the dialog, below the AI and
   needs-a-block sections. It describes the agenda hour combos and nothing
   else, but where it sat it read as a statement about all of Settings.
3. **One `save()` that every feature edits.** Every new preference meant
   another block appended to a single function. That is the same
   merge-conflict magnet `AppData` deliberately avoids by having one named
   door per operation instead of one `addEvent(everything)`.

Symptom 3 is the one that actually forced the change. The catch-up feature
(missed blocks) adds roughly six preferences. Under the old shape they'd
have been six more rows in the flat form and twelve more lines in the
shared `save()`.

---

## B. The shape

A `QListWidget` nav on the left drives a `QStackedWidget` on the right.

```
SettingsDialog                     the shell: nav, stack, OK/Cancel
├── QListWidget#settingsNav        one row per page
└── QScrollArea → QStackedWidget   one page visible at a time
    ├── AgendaSettingsPage
    ├── NeedsBlockSettingsPage
    └── AssistantSettingsPage
```

**Why not `QTabWidget`.** Tabs stop scaling past roughly six sections — the
labels compress, then elide, then sprout scroll arrows. We are at three
today and will be at four the moment catch-up lands. A vertical list has
room for a dozen and reads the way every settings screen built in the last
decade reads.

**Why the nav is a fixed 168px and not a `QSplitter`.** The nav has one job
and no reason to be resizable. Every degree of freedom in a layout is
another state that has to look correct.

**Why one `QScrollArea` around the stack, not one per page.** A
`QStackedWidget`'s size hint is the maximum of its children's, so the dialog
sizes to the tallest page and never resizes when you switch. A window that
jumps as you click nav rows feels broken. The scroll area is the escape
valve for a small screen, not the primary layout mechanism.

---

## C. Who owns the save

The contract is two virtuals on `SettingsPage`:

```cpp
virtual QString title() const = 0;   // the nav label
virtual void    save() = 0;          // widgets -> QSettings
```

`SettingsDialog::save()` is now a four-line loop over its pages. Adding a
page adds **zero** lines to it. That is the whole return on the refactor.

**There is no `load()`.** Pages read `prefs::` in their constructors,
because a page is built exactly once — `MainWindow` constructs a fresh
`SettingsDialog` on every ⚙ click. A `load()` would be ceremony for a
lifecycle this dialog does not have. Add it the day a page needs re-reading,
not before.

**The one-write promise survives.** Widgets edit local state; OK calls
`save()` on each page in one pass; Cancel writes nothing. The promise is now
distributed but not weakened — no page writes outside its `save()`, and the
Assistant page's *Test* button explicitly builds its probe from the fields
on screen rather than saving first, exactly as before.

---

## D. Considered and rejected

**Remembering the last page you visited.** Genuinely nice UX. It would mean
writing to `QSettings` when the dialog closes — including on Cancel — which
quietly puts an asterisk on the one promise this dialog makes out loud. A
nice-to-have does not get to weaken a stated invariant. Rejected.

**Lazy page construction.** Build each page the first time it is shown. The
obvious optimisation, and wrong twice:

- `test_ui.cpp` reaches in with `dialog.findChild<QComboBox*>("aiProviderCombo")`
  and similar. `findChild` is recursive, so it happily walks into a stacked
  page that isn't visible — but a page that has not been *constructed* has
  no children to find. Eight tests would begin failing for reasons unrelated
  to settings.
- The saving is imaginary. A few dozen widgets, built once, on a click the
  user made deliberately.

Optimising an imperceptible cost by breaking the test suite is a bad trade
twice over. Pages are constructed eagerly, and the comment in
`SettingsDialog.cpp` says why so nobody "fixes" it later.

**One file per page.** Six new files for three small classes that are never
used apart from this dialog and that share the `PolicyEditor` helper.
`ReviewWidgets.h` already sets the precedent for several collaborating
widgets in one file. One class per file is a good default, not a law — the
law is *things that change together live together*.

---

## E. What did not change

Deliberately, and this is the measure of the refactor:

- **Every `QSettings` key, default, and clamp.** `prefs::` still owns all of
  them. Nothing to migrate; a settings file written by v26.0 reads
  identically under v26.1.
- **Every `objectName`.** `startHourCombo`, `endHourCombo`,
  `weekStartCombo`, `blockAlarmCheck`, `needsBlockGateCheck`,
  `aiProviderCombo`, `aiKeyEdit`, `aiModelEdit`, `aiBaseUrlEdit`,
  `aiDialectCombo`, `aiTestButton`, `aiTestResult`, `aiPersonaCombo`,
  `aiPersonaTextEdit`, `aiChatFallbackCombo`. `test_ui.cpp` compiles and
  passes untouched.
- **The look.** The section headings still carry `objectName("h2")` and the
  footnotes `objectName("sub")`, so `Theme.h` styles them exactly as before.
  The nav's hover and selected washes are copied from `#railTree` on
  purpose: two vertical pickers in one app that highlight differently is the
  kind of small inconsistency that makes a UI feel assembled rather than
  designed.

A refactor whose tests had to be rewritten has not been proven to be a
refactor. These were not touched.

---

## F. One genuine improvement smuggled in

The agenda-hours footnote moved from the bottom of the dialog to the Agenda
page, beside the three combos it describes. This is a behaviour change in
the strict sense — different text is on screen at different times — but it
is the note arriving at its correct address. A caveat filed under the wrong
heading is a caveat nobody applies correctly.

Two other small cleanups, both mechanical:

- `settingsui::rowOf()` replaces four hand-rolled copies of *"new QWidget +
  new QHBoxLayout + zero margins + addWidget ×N + addStretch"*. Extracted on
  the rule of three (it was at four).
- The needs-a-block page grew two real headings — *When things come back*
  and *Escalation* — where the flat form had run everything together under
  one.

---

## G. What this sets up

The catch-up feature adds `CatchUpSettingsPage`: one new class, one
`addPage()` line, zero edits to `SettingsDialog::save()`. That is the test
of whether this addendum earned its keep, and it gets answered next.
