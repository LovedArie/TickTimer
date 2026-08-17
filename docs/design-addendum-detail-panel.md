# Design addendum — The detail panel (v28.6.0; overlay v28.6.1)

*The debt §L.4 of the subtasks addendum recorded, paid: the task-detail
experience converted from a blocking modal dialog to a TickTick-style
docked side panel. UI-only — no domain change, format stays v13.*

Diagram: `diagrams/detail_panel_states.*` — the panel's guard states.
The v28.5 `piece_detail_sequence.*` remains accurate for the modal
FALLBACK path.

---

## A. The shape: one form, two containers

The v28.6 refactor split what was one class into a form and its
containers:

- **`TaskDetailForm`** — every field (title, notes, deadline, repeat,
  priority, size, pieces checklist, breadcrumb) and every answer getter.
  The pure-question contract lives here now: the form gathers answers
  and mutates nothing.
- **`TaskDetailDialog`** — the modal wrapper: window title, Save/Cancel,
  and one policy (navigation = record + accept). Kept as the FALLBACK
  for windows without a panel — and for the entire existing test suite,
  which passed unchanged through the wrapper: the refactor's proof.
- **`TaskDetailPanel`** — the drawer container: slides in at the main
  window's right edge, the app stays alive behind it, and navigation is
  a swap-in-place. (Docked INTO the layout in 28.6.0; an OVERLAY since
  28.6.1 — see §G for the correction and why.)

The extraction's real product is the seam it exposed: **what a
navigation request MEANS is the container's decision.** The form emits
`navigateRequested(id)`; the dialog answers "record and accept" (the
v28.5 hop); the panel answers "guarded swap." Same fields, same answers,
two policies — and `runTaskDetail` picks the container (panel when the
window has one, modal loop otherwise), which is why the four call sites
changed zero lines *again*.

## B. Explicit save — the owner's call, and its grammar

Instant TickTick-style auto-commit was considered and **rejected by the
owner**: saving should be a deliberate act with visible feedback. The
panel's grammar:

- **The Save button is the dirty indicator.** Lit = "you have work only
  this panel knows about"; quiet = form matches the saved truth. Dirty
  is a *comparison* (`TaskDetailForm::isDirty`, answers vs. the
  `markClean` baseline), never an accumulated was-touched flag — so
  retyping the original goes quiet again, honestly.
- **Saving flashes "Saved ✓"** for a moment. Feedback in both
  directions: before (you owe a save) and after (it landed).
- **No silent discard exists.** Every exit over dirty work — switching
  tasks, clicking a piece, the breadcrumb, ✕, Esc — runs the unsaved
  prompt: **Save / Discard / Stay**, with Save as the default button so
  Enter can never destroy work. Stay means the triggering click did
  nothing at all.

The v28.5 rule "the hop saves the sitting" thus grew a question mark:
the hop saves the sitting *if you say so*. The modal fallback keeps the
old unconditional rule — its Save/Cancel pair already is an explicit
choice, so the prompt would be asking twice.

## C. Rebuild, don't reset

The panel seeds a **fresh form per task** instead of resetting fields: a
form that never resets can never reset *incompletely*, and "reopen" and
"first open" stay one code path. Two lifetime rules make the swap safe,
both worth stealing:

1. The outgoing form is **`deleteLater`'d**, never deleted — the swap
   may be running inside that form's own `navigateRequested` emission.
2. `navigateRequested → openTask` is a **queued connection** — let the
   click finish before the ground moves under it.

Saving also rebuilds — which is a feature: newborn pieces get their ids
on apply, so their titles become doors *the same sitting*. Type a piece,
Save, click straight in.

## D. The world moves while the panel is open

`AppData::changed()` while showing a task (sync, another page, a sweep):

| the shown task is… | the panel does |
|---|---|
| gone | closes — a panel editing a ghost writes to nothing |
| changed, form CLEAN | reseeds to the new truth |
| changed, form DIRTY | keeps the user's edits; their Save writes last |

The dirty case is last-write-wins by choice — the same answer this app's
sync gives everywhere else — and an `m_applying` flag mutes the echo of
the panel's *own* save, which would otherwise rebuild the form out from
under the ✓ flash.

## E. The test seam, and its honest half

The unsaved prompt is a `QMessageBox` — un-drivable under offscreen
ctest — so the panel takes an injected choice
(`setUnsavedPromptForTests`), the house seam pattern. The v28 field
report's lesson applies: *seams only tests can reach are half a seam* —
so the QA checklist's v28.6 block walks the real prompt by hand,
including the Enter-defaults-to-Save property no offscreen test can
feel.

## F. Deferred, deliberately

- **Pieces reorder** and the quick-date row conveniences — unchanged
  from §L's list.
- **Promotion** (a dated piece as its own workload line, the parent
  shrinking to the un-promoted remainder) — the next slice with a real
  design decision in it.
- **Panel width memory / user resize** — a splitter is the natural
  upgrade if the fixed 360 px chafes in use; wait for the complaint.
- **In-panel piece editing without navigation** (TickTick's popover) —
  a convenience layer on top of doors that now exist.

## G. Docked → overlay (v28.6.1, owner feedback within hours)

The 28.6.0 panel sat IN the body layout — [nav][pages][panel] — and the
owner's first look rejected the shape: it *read as part of the
Activities page* and shrank the main screen. Both complaints are the
same fact: a layout member competes for space; the owner wanted the
panel **in front**, with the world dimmed behind it. The correction:

- **Out of the layout.** The panel is now a floating child of the body,
  positioned manually against the right edge; opening it reflows
  nothing. The slide became a *position* animation (x from just past the
  edge to flush) instead of a width one.
- **The scrim** — a host-sized translucent layer (`rgba(43,47,54,0.28)`)
  under the drawer: the "lower contrast" that makes the panel read as
  foreground. It is also the click target: **clicking anywhere outside
  closes the panel** — through the *same* Save/Discard/Stay guard as ✕
  and Esc (`clickingOutsideRunsTheSameGuard` pins it). Click-away close
  never means click-away *discard*.
- **Wider: 360 → 440 px**, clamped so a narrow window always keeps
  220 px of visible content — an overlay that covers everything is a
  modal with extra steps.
- **Two Qt traps paid for and filed:** a plain QWidget ignores
  stylesheet backgrounds without `WA_StyledBackground` — harmless while
  the panel sat on a white page, a see-through ghost as an overlay. And
  un-laid-out children don't follow resizes: the panel event-filters the
  host and repositions drawer + scrim on every `Resize` (instantly — a
  resize is not an entrance).

Process note, worth its line in the log: the docked shape shipped,
was *used within hours*, and the correction landed the same day. The
catch-up arc's argument for daily-driving (§L there) keeps collecting
evidence — no offscreen test can notice that a panel "feels like part
of the Activities page."
