# Session Notes — TickTimer (C++/Qt mentorship)

*Running record. Used to decide what to quiz and where to pick up. Updated at
the end of the v19.1 session — the DAILY-DRIVER pass: the first session built
entirely from the owner's own usage feedback (archive, priority, honest
tracking, schedule compare — upgraded mid-session to an editable two-agenda
planning screen on owner feedback — editable special days).*

> **This file stops at v19.2.4 and the tree is at v29.1.0.** Everything
> below is a snapshot of that moment — the version, the test count, and the
> doc-debt list are all historical, not current. Ten versions of work landed
> after it (the AI arc, catch-up, subtasks & sizing, the write boundary,
> intake); their record lives in `docs/CHANGES_v2*.md`, the design addenda,
> and `docs/PROJECT_LOG.md`. Kept as-is rather than rewritten: it is a log,
> and back-dating a log destroys the only thing it is for.

---

## Project & where it stands

**TickTimer** — a C++17 / Qt 6 Widgets desktop app: a plan-vs-actual time
tracker that credits every life area (work, health, relationships, rest), built
especially for people whose focus is derailed by anxiety-driven procrastination.

- **State:** **v19.2.4** (`include/Version.h`, the single source of truth
  feeding the code, both Windows exes, and the update check), data format
  **v8** (…v7 + Category.archived, all additive), **89 tests green**
  (47 domain + 12 UI + 19 auth + 10 live end-to-end); **auto-sync** on
  (5s debounce, conflicts still human-resolved), clean
  warning-free build against Qt 6
  (CMake + QTest, headless); **Android-ready** (`docs/ANDROID.md`);
  **Windows-distributable** — a one-double-click `deploy-windows.bat` bundles
  both exes with their Qt DLLs into a portable folder, plus an Inno Setup
  script (`installer/ticktimer.iss`) for a real `TickTimer-Setup.exe`, both
  carrying an embedded app icon (`docs/INSTALLING.md`); a **client +
  self-hosted server** — login gate (now with an editable **Server** field),
  whole-planner device sync, **share & compare**, and **update notices**
  (a non-nagging banner when the server advertises a newer release —
  `version.json`, `docs/GITHUB.md` for the release workflow) through a
  `ticktimer-server` program you run yourself (`docs/SERVER.md`). The
  networked arc is complete.
- **Architecture:** domain (`AppData` aggregate root, value types, `Stats`) is
  UI-free and fully testable; storage (`JsonStore`, atomic writes, crash
  recovery); tracking (`TrackerService` state machine); UI (custom-painted
  widgets that *report* via signals, pages that *decide*). **Networked arc**
  adds a third slice: a self-hosted server (`AccountStore` + `PlannerStore` +
  `AuthServer`) and a client sync stack split into wire (`SyncClient`),
  policy (`SyncService`, whose brain is the pure `sync::decide` truth table),
  and glass (`LoginDialog`, `SyncDialog`) — bolted *in front of* the planner,
  which didn't change. Part 3 added the same three layers for sharing:
  `ShareStore` (server permissions) / `ShareClient` (wire) / `Compare.h`
  (pure brain) + `SharingDialog`/`CompareDialog` (glass). Tests split the
  same way: Widgets-free domain suite,
  headless UI regression suite, auth/unit suite, and a live end-to-end suite
  that spawns the real server.
- **Docs:** UP artifact set (Vision, Use-Case Model, Supplementary Spec,
  Glossary, Risk/Iteration/Phase plans, Development Case) + `design-doc.md` +
  design addenda + reading guide + troubleshooting log.

**Doc debt (cleared at the v18 session):** design-doc gained the `share`
addendum row, the `shares.json` persistence note, and a refreshed backlog
(share/compare retired; totals-only sharing + share invitations added as
named future fences); reading guide gained the part-3 file map; SERVER.md
documents the four new routes and the 401-vs-403 distinction; question bank
gained section Q (12 questions); this header refreshed. Remaining: the
**stale duplicate source tree** (`src/storage/`,
`src/tracking/`, `src/domain/`, `src/ui/`, `include/domain/`, `include/ui/`,
`include/storage/`, `include/tracking/`) is still on disk and not in the
build — worth deleting to avoid editing the wrong copy. *(Resolved since:
those folders are gone; the tree is flat, `include/` + `src/`.)* Also still
open:
week/month **unaccounted** rows (denominator decision), **subtype labels**
for distracted/break kinds (owner interest, §3.37), back-porting
`ShareClient::classify` into the two older wire clients (rule-of-three
hygiene, addendum §D), and the last networked-arc part (**auto-update**).

---

## Features shipped (recent sessions)

- **Tasks & deadlines** (description, repeat hint, due date as invalid-QDate TBD).
- **Folders** for life areas + **drag-and-drop** into them (right-click kept).
- **Special Days** as prominent cards; **Upcoming** view (derived query).
- **Pomodoro** with **adjustable durations** persisted in `QSettings`.
- **"Due today" strip** on the day calendar (read-only bridge from tasks).
- **Upcoming restyled as cards** (presentation only).
- **Week agenda** — seven `AgendaWidget`s behind one shared axis (reuse payoff).
- **Drag-to-resize events** — edge grab, ↕ cursor, live clamped preview,
  committed through `AppData::resizeEvent` (guarded by `isFree`).
- **Distracted time** — a third `SegmentKind`, tracker state, rose UI, tolerant
  JSON reader, format v5.

---

## Concepts covered (cumulative)

- **Domain / presentation / settings** classification of every fact.
- **Aggregate root / "one door":** rules live in `AppData` where nothing bypasses.
- **Derive-don't-store:** stats/upcoming/next-occurrence recomputed, never cached.
- **Make illegal states unrepresentable:** `enum class`, invalid-QDate TBD.
- **Additive schema growth + tolerant readers:** missing key / unknown enum →
  safe default; no migration.
- **Const-correctness as architecture:** a `const` view makes the wrong call
  un-compilable → "widget reports, domain decides."
- **Clamp vs. guard:** UI makes bad states hard to *reach*; domain makes them
  impossible to *exist*. Keep both.
- **Second-consumer rule:** share on the 2nd real caller; don't extract for one.
- **Custom-paint discipline:** input writes state + `update()`; paint only reads.
- **Qt object ownership / dialog lifetimes:** parent modal dialogs to `window()`;
  `deleteLater` when rebuilding from a widget's own signal.
- **Exhaustive `switch` over `if/else` on enums** (no `default` → compiler guards).
- **Debugging loop:** reproduce → backtrace → fix the real cause → re-run repro;
  "missing" is a symptom with ≥4 causes; a disproven hypothesis is progress.
- **Signals decouple** (not for speed); function-pointer `connect` is type-checked.

---

## Mastered (answered correctly, ideally by application)

- Presentation vs domain vs settings bins.
- Derive-don't-store (Upcoming; and *why not* a distracted counter).
- Clamp-vs-guard split (predicted it before seeing the code).
- Const view forces the widget→signal→domain architecture.
- Dialog parenting / double-free root cause.
- Reproduce-don't-guess debugging; "missing" ≠ "not saved".
- Second-consumer / speculative-abstraction rule.

## Still shaky (re-taught by *use*; keep spaced-repeating)

- **Aggregate root / "one door"** — struggled at first ("I don't know" ×3), then
  applied it correctly to `resizeEvent`. Reinforce with fresh hands-on reps.
- **Hands-on "spot-the-bug" format** — weaker than MC/TF; the enum-`else` bug
  needed re-teaching. Give more write-it / fix-it questions, lower stakes.
- **Custom `paintEvent` rule** — missed the T/F first; solid after re-teach.
  Confirm it stays solid next time it comes up.

---

## What to tackle next (options)

- **Doc merge pass** (teach the doc side properly): merge addenda #3 + the new
  agenda/tracking addendum into `design-doc.md`; delete the stale duplicate
  source tree; refresh the Use-Case Model with the resize/distracted interactions.
- **Glance panel:** show a Distracted box (currently focus/break only; category
  totals already include distracted).
- **Deferred fences worth revisiting:** placing a task onto an agenda *time
  block* (not just the due strip); acting on `repeat` (regenerate on completion);
  vacation date *ranges*; window/sidebar state in `QSettings`.
- **Bigger arcs:** Qt model/view refactor; SQLite migration once data grows.

## Teaching adjustments for this learner

- One question at a time; ~75% MC/TF, ~25% hands-on. Spaced repetition every
  other response.
- Celebrate climbing out of dips (happened this session); keep stakes low on
  hands-on so "I don't know" stays comfortable.
- Always follow an answer with a good-vs-bad code pair.

---

## Session — block identity (labels, task blocks, ad-hoc blocks) → v14

**Shipped:** blocks now say what they are. (1) A custom **label** typed at
planning time or in the block dialog, painted on the block ("Gym" / *legs
day*). (2) **Task blocks** — the picker lists open tasks under each life area;
a block can BE "Lab 4", wearing the task's category colour, ✓ when done.
(3) **Ad-hoc blocks** — type anything + Enter in the picker; no activity
needed; paints neutral grey. Format **v6** (additive: `Event.taskId`,
`Event.title`), **31 tests green** (+6).

**Concepts this session:**
- **Generalize, don't multiply:** one `Event` with optional identity fields
  beat three subclasses — consumers only care about the time range.
- **Illegal states unrepresentable, API edition:** three creation doors
  (`addEvent` / `addTaskEvent` / `addAdHocEvent`) so nonsense combinations
  have no signature to call; shared rules in one private worker.
- **Refuse / cascade / DOWNGRADE:** `removeTask` demotes references to text
  (rescues the title into the event) — contrast with `removeActivity`'s
  refusal. Third option worth keeping.
- **Resolution helpers as a seam:** `eventLabel` / `eventCategoryId` — the
  three widgets that each walked Event→Activity→Category now share one rule;
  a fourth identity kind is a two-branch change.
- **Validate-on-commit:** label field saves on `editingFinished` because the
  domain can refuse; per-keystroke refusal fights the user.
- **Pointer-into-container lifetime bug** caught live in `removeTask`: copy
  before erase.

**Doc debt (unchanged + new):** design-doc §4 still frozen at v11/format 3;
now THREE addenda unmerged (task-details, agenda-and-tracking, block-labels).
Stale duplicate source tree (`src/storage/`, `src/tracking/`, `include/domain/`,
`src/domain/`, `src/ui/`, `include/ui/`, `include/storage/`, `include/tracking/`)
still worth deleting. Known limitation: ad-hoc time appears in totals but no
category bar (test-pinned; future fix = explicit "Other" bar).

**Follow-up (same session):** owner clarified the label field should also
*attach a task* to an existing block. Added `setEventTask` (link/unlink, same
last-identity guard, mirrored from `setEventTitle`), `QCompleter` on the label
field (activation links, plain text labels — next-gesture disambiguation
again), "Linked task / Unlink" row, task-as-subtitle painting with the ✓
following the task's line. Activity keeps name/colour/attribution precedence
(test-pinned with task in a different category). **33 tests green** (+2). No
format bump — `taskId` already persisted. Ordering lesson: link before
clearing the title, or an ad-hoc block refuses mid-sequence.

---

## Session — Android port & compact-screen mode

**Shipped:** the codebase is Android-ready. CMake: `if(ANDROID)` packaging
block (org.ticktimer.app, versionCode 14, minSdk 28) + `if(NOT ANDROID)`
fence for tests/tools. UI: `isCompactScreen()` geometry gate (rail starts
collapsed — reusing the existing ☰ toggle; tagline + glance yield; Pomodoro
settings stack; Activities rail 168), `makeTouchScrollable()` on all five
scroll areas (TouchGesture, so desktop mouse drags untouched). Verified at
412×915 offscreen + OCR; desktop pixel-unchanged; **33 tests green**.
APK itself builds on the owner's machine — `docs/ANDROID.md` is the
click-by-click guide (sandbox has no Android toolchain).

**Concepts:** geometry-not-platform gating; QStackedWidget min = max over
ALL pages (the 550px hostage situation); three minimum-size contracts and
their three fixes (word wrap / QBoxLayout direction / scroll-area severing);
diagnosis-as-a-command (the TICKTIMER_PROBE layout probe); qt_add_executable
as the day-one enabler of androiddeployqt.

**Known limitations (documented in ANDROID.md):** touch drag on the agenda
scrolls instead of edge-resizing (dialog nudge buttons cover it); Widgets on
Android is functional, not native-feeling — a QML front-end over the same
domain is the natural future project; no desktop↔phone data sync.

**Follow-up:** ANDROID.md gained Option B — sideloading the built APK file
(build-folder path, transfer, "install unknown apps" gate) and the
package-name + signing-key identity rule (debug vs release can't overwrite
each other; uninstall in between, data folder goes with it).

---

## Session — the add-a-task crash (delete-during-signal) → first UI test

**Bug:** adding a task in the Activities detail crashed the app; the task
survived (saved before the stack unwound). **Cause:** `rebuildDetail()` ran
inside `changed()` (direct connection) and `delete m_detail->takeWidget()`
destroyed the QLineEdit whose returnPressed was still on the stack —
use-after-free. **Fix:** `deleteLater()` at all three `takeWidget` sites
(Activities detail, Upcoming, Special days); the due strip already used it.

**New infrastructure:** `test_ui` target — the project's first UI regression
test. Drives the real page offscreen (select rail item, type, Enter) and
asserts the lifetime CONTRACT via QPointer (alive immediately after the
keystroke, dead after the event loop drains) — deterministic, because the
raw UAF passed by allocator luck on Linux even under ASAN while crashing on
Windows/MinGW. Red (segfault) before the fix, green after; 33 domain + UI
suite green.

**Concepts:** direct connections make mutation → rebuild re-entrant; a slot
must never delete the sender's widget tree (deleteLater = destruction when
the event loop regains control); UB may pass by luck — test the contract,
not the crash; symptom "crash after successful mutation" localizes to UI
listeners; fix the pattern (grep), not the instance. Logged in
TROUBLESHOOTING.md (symptom-indexed).

**Polish:** block line order swapped per owner — name / TIME / description
(time is fixed anatomy, same spot on every block; free text below). Accepted
consequence, flagged: 2-slot blocks show name + time only. Paint-only change,
OCR-verified, preview re-shot, suites green.

**Polish:** block label grew into a multiline box (`LabelEdit` :
QPlainTextEdit, ~3 lines). Ripples handled: completer re-wired via Qt's
custom-completer pattern (setWidget + keyPressEvent; popup keys ignored so
Enter selects, not newlines); save contract moved from editingFinished
(gone — Enter = new line) to commit-on-focus-out with a PopupFocusReason
guard; agenda paints the description word-wrapped into remaining block
height (clip, not elide). Lost the QLineEdit-only ⊗ clear button. New UI
test pins Enter-newlines + focus-out-commits; offscreen lesson logged: focus
events need an ACTIVE window (activateWindow + qWaitForWindowActive), which
cost one red iteration. 33 domain + 2 UI tests green.

**Polish:** task line and comments now COEXIST on a block (was either/or):
name / time / linked task (elided, ✓ when done) / comments (word-wrapped,
remaining height). Linking a task no longer clears the label — original
"task replaces label" design invalidated by real usage (label = comments;
linking destroyed them); reversal recorded in §3.29 and as a sequel note on
QB H11 (the ordering lesson survives, the code it described doesn't).
OCR-verified all four lines on one block; 33 domain + 2 UI tests green.

**Feature:** task descriptions on blocks, behind a "Task notes" checkbox
(Calendar top bar). Classified as a SETTING (QSettings planner/showTaskNotes,
default ON) — data.json untouched. Widget told via setShowTaskDescriptions;
week view fans out to 7 columns (body in .cpp — incomplete-type lesson:
headers that forward-declare can't inline-call members). Paint: indented 12px
under the task line, word-wrapped, advances the y-offset so comments follow.
Verified ON and OFF by OCR; probe now prints QSettings().fileName() — found
the "Unknown Organization" fallback, deliberately NOT fixed (setting an org
name would relocate settings + data folder). 33 domain + 2 UI green.

**Polish:** top bar de-cluttered — ‹ Today › alone at the far right; the
Task-notes toggle moved onto the agenda header row ("Your day" left, toggle
right), directly above what it changes. Principle noted: controls near their
effect need no explanatory label; distance creates that need. Tradeoff
flagged: toggle visible in Day view only, still governs the week columns.
Positions OCR-verified; 33 domain + 2 UI green.

**Polish:** column flow for block text (§3.34). Hard-broken lists were
clipping at the bottom while the right half sat empty; drawFlowedText
re-flows overflowing description/comments into two balanced half-width
columns (QTextLayout line-by-line placement; '\n' → QChar::LineSeparator;
counting pass + positioning pass, uniform line height). Single column
preserved when text fits — columns only on overflow. OCR-verified: all four
description items + comments visible on one block. 33 domain + 2 UI green.

**Fix (owner-diagnosed):** column flow's fit test was selfish — a
description that fit its area hogged one column and starved the comments
below (metric-dependent: bit on Windows fonts, scraped by on Linux until
+1 line). drawFlowedText gained a `budget` param; caller reserves the
comments' measured height (capped at half). Rule generalized: columns
appear when they earn something, and making room for the neighbor counts.
Red reproduced (comment invisible) → green (2-col + comment back),
OCR-verified. 33 domain + 2 UI green.

---

## Session — distracted made visible (§3.35)

**Owner request:** "when I'm distracted, I don't want it to be considered
break." Domain was already right (three buckets since §3.24); the display
lied twice: GlancePanel's live split was a stale two-way if/else (live
Distracted ticked the BREAK box), and no Distracted box existed in glance /
week / month. Fixed: three-way switch (matching EventDialog/AgendaWidget) +
a third StatBox in all three summaries, theme::danger() (same hue as the
block bars). Month pairs Focused with Distracted only — rest needs no audit
at month scale. Category bars unchanged (t.total(): boxes judge quality,
bars report quantity). New UI test liveDistractedTimeIsNotCountedAsBreak
asserts through the captions the user reads (1s live → DISTRACTED, BREAK
stays 0s). Fence "distracted box in glance" retired in design-doc §5; index
row updated. **33 domain + 3 UI green**; glance OCR-verified
(FOCUSED 40m · BREAK 0s · DISTRACTED 10m).

**Concepts:** partial pattern adoption as a bug class (grep for the
ABSENCE of a known-good pattern); if/else over a 3-value enum vs a
self-auditing switch; per-timescale display decisions; quality-vs-quantity
split between boxes and bars.

**Fix (owner-reported, §3.36):** multiline labels × ad-hoc blocks — the
whole paragraph became the headline (dialog + agenda). Rule adopted: first
line = title, rest = body. eventLabel now returns the ad-hoc first line;
new eventBody resolver returns the remainder (whole title for
activity/task blocks). Derive-don't-store: one field, two views, no format
bump. Dialog header fixed itself via the shared resolver; agenda comments
switched to eventBody. +2 domain tests (**35 domain + 3 UI green**);
OCR-verified: "Rona +" / time / body.

---

## Session — category bars count focus only (§3.37, reversal)

**Owner's falsifying data:** GTI350 block, 10m focus + 1h08m distracted →
bar claimed 1h22m ("it states that I did all that work — misleading").
Reversed §3.35's t.total() attribution: byCategory += focusSeconds — one
line in Stats, app-wide effect (glance bars, week pie) because attribution
derives in one place; GlancePanel live credit now focus-gated. Break/drift
accumulate as fixed sink rows (glance) and slices (week pie + legend),
derived from totals — NOT routed to the owner's "Wasting time" category
(magic names: rename-fragile, double-counting, namespace theft). The
pinning test changed WITH the rule and got stronger (+ distracted segment).
K3 got a sequel; second full reversal after §3.29 — same honest protocol.
Subtype labels for drift/breaks named as the likely next domain session
(fence annotated). **35 domain + 3 UI green**; OCR: Health 40m (was 50m),
Break 0s / Distracted 10m rows present; books balance.

**Concepts:** design decisions falsified by use; tests pin decisions, not
truths (change the test in the same commit as the rule, with the why);
derived sink rows vs magic categories; single-point derivation making a
one-line rule change app-wide.

---

## Session — tracking honesty: the live-window constraint (§3.38)

**Owner constraint:** status changes (focus/break/distracted) allowed only
while NOW is inside the block's planned window — no pre-logging a 5 PM
block at 11h. Shape: pure `Event::isLiveAt(t)` (half-open, every boundary
unit-tested with supplied time) + `TrackerService::canTrackNow` + guards as
the FIRST line of the three start doors (zero-side-effect refusal: a
refused switch must not kill the running interval — test-pinned). `stop()`
deliberately unguarded (truth gets written); running intervals not auto-cut
at window end (switching blocked, stopping allowed). Dialog braces: buttons
gated, idle label explains ("tracking opens at 5:00 PM"), 30 s gate timer
because tick() only pulses while tracking.

**The clock became a dependency:** `nowProvider` seam (std::function,
defaults to wall clock) — forced by the domain day starting at 06:00
(0–1440 event refused at the door; real-clock tests flaky before 6 AM).
Tests now replay the owner's literal example at an injected 11:30.

**Battle scars logged:** member variable in `public slots:` → AutoMoc
"Not a signal or slot declaration" + stale-binary test ghosts
(TROUBLESHOOTING entry + QB L4). **37 domain + 4 UI green.**

**Polish (§3.39, owner-spotted):** short blocks hid task descriptions —
§3.34 columns only split the area BELOW the header, ~one line tall on a
3-slot block. New placement rule: tight block → task line left half,
description flows in the right half from the task row down (drawFlowedText
gained maxColumns; side region wraps/clips, never sub-columnizes); comments
take the left half beneath. Trigger geometric (< 2 line-heights below) —
NOT text-fit, per the §3.34 font-metrics lesson. Placement decided before
the task line draws (it changes its width). Roomy blocks unchanged
(regression-OCR'd). Verified on the owner's exact ING150 scenario: task
line x≈387, description x≈551 on the SAME row, wrapping in the right half.
**37 domain + 4 UI green.** Preview: docs/side-description-preview.png.

---

## Session — glance polish + unaccounted time (§3.40)

**Shipped:** zero rows hidden (categories AND sinks — "0s is useless");
day pie in the glance panel (CategoryPie reused; fed the SAME rows as the
bars, so the bars are the legend — invariant: one rows container feeds
both); UNACCOUNTED time = elapsed planned window minus tracked, clamped —
derived (never stored), grey sink row + slice, live interval subtracted so
the number doesn't grow while actively tracking. Stats API: `now` is a
parameter (default wall clock) — nowProvider lesson generalised; boundaries
pinned at a fixed 11:30 (+1 domain test incl. the never-tracked case the
summarize early-exit used to skip). Week/month unaccounted deferred
(legend-percentage denominator decision).

**Bug caught mid-session:** two clocks in one decision — dialog verdict on
tracker's injected clock, hint wording on the wall clock; suite went red
the moment real time crossed the test block's 5 PM start. One `now` per
decision now (QB L5). **38 domain + 4 UI green.** OCR: School 0s and Break
0s gone, Unaccounted 7h 40m (matches hand-computed 460m), pie rendered
(docs/glance-preview.png).

---

## Session — login & local accounts (networked arc, part 1/4)

**Shipped:** client/server split. New `ticktimer-server` (QCoreApplication +
QTcpServer, hand-rolled HTTP/JSON) storing accounts with salted+stretched
password hashes (PasswordHash.h, pbkdf2-shaped, Argon2 swap flagged); shared
`ticktimer_auth` static lib so server + tests link identical code. Client:
`AuthClient` (async QNAM → typed Outcome) + `LoginDialog` (two-mode gate);
main() gates the window behind it; server URL from QSettings (localhost →
LAN → Pi, no recompile). Deliberate security calls: username enumeration
avoided (one bad_credentials for both cases), case-insensitive names,
never-plaintext.

**Decision recorded (user-driven):** no Google — own auth so storage is a
free choice (laptop now, Pi later). Consequences of laptop hosting written
up in docs/SERVER.md (server only up while laptop is; same-Wi-Fi only;
rotating IP printed on startup; not internet-facing; firewall prompt normal).

**The hard bug (QB M1):** valid 4xx responses arriving as NetworkError on
alternating requests — THREE stacked causes: reply->error() nonzero for HTTP
4xx (branch on HttpStatusCodeAttribute instead), empty HTTP reason phrase
(send real phrases), and QNAM connection reuse against a one-shot server
driven from a nested event loop (clearConnectionCache per request). Found and
pinned by test_login_live (spawns real server, real socket).

**Tests: 38 domain + 6 UI + 9 auth + 4 live = 57 green**, warning-free.
Qt6::Network added to the build. Planner domain untouched — identity is a new
subsystem in front, not a rewrite.

**Next in arc:** sync (part 2) — now that identity exists to hang it on.

---

## Session — device sync (networked arc, part 2/4)

**Shipped:** full-document sync with optimistic concurrency. Server:
PlannerStore (opaque `{revision, data}` blob per account — server never
reads planner internals), session tokens on login/register (in-memory,
128-bit; token IS the identity, no username on the wire), GET/PUT /planner
with 409-on-stale-base; username charset gate at registration (usernames
became filenames). Client: JsonStore split into conversion vs file I/O (one
format, disk AND wire); AppData::replaceAll (resetFrom + changed — the loud
door for live pulls); SyncClient (wire, QB-M lessons applied from line 1) /
SyncService (policy: sync::decide truth table + dirty bit in QSettings +
m_applying reentrancy guard) / SyncDialog (glass: Sync now + explicit
conflict buttons); Sync button pinned under the rail stretch; main() hands
the token over via enableSync.

**Design:** the whole brain is decide(serverRev, lastRev, dirty) — a pure
four-row truth table, tested as the table. Conflicts are never resolved
silently: hold the server version, ask the human, force-push only on
explicit "keep mine". First-run dirty defaults TRUE (existing data uploads;
fresh second device gets one safe prompt). Known limits recorded honestly
(one account per device; sync while idle; races → "press sync again").

**Battle scars:** struct in a `private slots:` section → moc "Not a signal
or slot declaration" (L4's second sighting, caught fast); awaitService must
defer fire() with singleShot(0) because resolveUseServer emits finished()
synchronously — quit-before-exec hangs forever (QB O7).

**Tests: 38 domain + 6 UI + 12 auth + 7 live = 63 green**, warning-free.
The live suite now runs a two-"device" playbook against the real server:
push → pull-replace (guard pinned: not dirty after pull) → conflict →
use-server → conflict → keep-mine → other device pulls the forced version.

**Next in arc:** share & compare planners (part 3) — read-only access to
someone else's shelf is a small step from here.

**Docs maintenance (owner request):** TROUBLESHOOTING.md caught up with the
networked-arc sessions — 8 new symptom-indexed entries (QNAM three-stacked
NetworkError; find_package missing component; vtable/AUTOMOC header-in-
sources; .moc include rename; QTest macros in value-returning helpers;
synchronous-signal await hang; two-clocks time-dependent test failure;
silent domain-door refusals) + a second-sighting sequel on the moc
slots-section entry (struct declarations break it too).

**Doc refresh (owner request, v17):** brought the top-level docs current with
the networked arc. design-doc → v5/v17 (status, shipped-features paragraph,
persistence note with the two server stores, §3 index incl. login+sync
addenda, backlog, how-we-got-here narrative). READING_GUIDE gained a Part 6
networked-arc file map + a "new since v15" landmarks section. SESSION_NOTES
header block refreshed (state, architecture, doc-debt). No code changes;
suites still 63 green.

**Launch docs (owner hit the wall running two programs):** the server exited
instantly with -1073741515 (0xC0000135 — missing Qt DLLs when run outside Qt
Creator; fixed with windeployqt). Wrote docs/RUNNING.md (two-program launch
sequence, target selector, windeployqt, the can't-reach-server checklist,
exit-code table), added the -1073741515 entry to TROUBLESHOOTING.md, and
rewrote the README Build & Run section (was single-program; now server-first
then app, with the Windows DLL note). No code changes.

**UX fix (owner hit it live):** registering with an email (phan.perry.ouy@
gmail.com) was refused by the username charset guard with only a generic
"check your details" message. Added a client-side pre-flight check in
LoginDialog::submit (register mode only) that matches the SAME
[A-Za-z0-9_-]{1,32} rule AccountStore enforces, with a specific, friendly
message naming the forbidden chars and suggesting a plain username; also
updated the username placeholder to show the rule up-front. Belt-and-braces:
client explains, server still refuses. Existing logins with legacy names
skip the client check. 44 tests green, warning-free.

---

## Session — per-account local storage + data adoption (accounts addendum)

**Owner ask:** different user → their own data, AND my existing planner must
transfer, not vanish. Shipped: local file scoped to the user
(`data-<username>.json`; empty user → legacy global path, so tests/tools are
unchanged), sync-state keys namespaced `sync/<user>/…`, threaded via
`MainWindow(username="")` from `main()`. Adoption migration
(`adoptGlobalDataForUser`): first user after the upgrade COPIES the old
global `data.json` into their account file, original retired to
`.pre-accounts.bak` — copy-then-backup, never move-then-hope. Guarded:
existing account file → no-op; second user → fresh; re-run → no-op. Closes
the sync addendum's headline known-limit (one account per device).

**Battle scar (QB P4):** first adoption test set XDG_DATA_HOME mid-run, but
QStandardPaths resolves at process start — paths diverged, test failed.
Fixed by driving the REAL defaultFilePath/filePathForUser with unique names
+ a scope-guard restoring any real global file. (test_auth now links
DOMAIN_SOURCES + Qt6::Gui and runs under offscreen, since it touches QColor.)

**Tests: 38 domain + 6 UI + 14 auth + 7 live = 65 green**, warning-free.

**Docs (owner request):** added the "no Qt platform plugin could be
initialized / could not find platform plugin windows" entry to
TROUBLESHOOTING.md as the GUI sibling of the missing-DLL entry (server is
headless so only the app hits it; deploy each exe separately). Rewrote the
README Run section and RUNNING.md step 3 to launch BOTH programs from the Qt
command prompt (two windows) with a one-time windeployqt on each exe;
Qt Creator kept as the alternative. No code changes.

---

## Session — Settings, identity labels & the visible window (v19.3)

**Owner asks (3):** peer's username visible atop the compare agenda; a
"Welcome, <username>" header; a Settings dialog for agenda hours + week
start (Sunday/Monday).

**Shipped:**
- **Welcome header** — right of the main header, only when a username
  exists (empty = tests/tools/legacy path → no label).
- **Compare identities, pinned** — the real fix was structural: the old
  "You"/peer headings lived INSIDE the QScrollArea and scrolled away.
  They moved above the scroll (mirrored layout: 1:1 stretch, spacing 14,
  right margin = scrollbar extent) and carry real names — `alice (you)` /
  `mom` — threaded MainWindow → SharingDialog → CompareDialog. UI test
  asserts ANCESTRY (no QScrollArea above the labels), not text presence.
- **Settings dialog** (⚙, nav furniture; NOT checkable — a dialog must
  not steal the rail highlight). Agenda hours became a DISPLAY WINDOW
  over the unchanged plan:: grid (domain rule vs taste; no format bump,
  prefs never sync). One honesty rule: **data always wins** —
  `AgendaWidget::windowCovering` (pure/static/shared) stretches the shown
  range over the date's events, so a narrowed window can hide empty hours
  but never a block. Multi-column screens compute the UNION through the
  same function: week view over 7 dates (axis + columns move together),
  compare over 2 datasets. Geometry refactor ≈ one subtraction because
  everything already went through one slotTop (now a member); slot
  indices in signals stay DOMAIN indices, so pickers/pages/tests were
  untouched. Week start became a defaulted PARAMETER on summarizeWeek
  (never a QSettings read in the pure layer — the `now` lesson again),
  and one shared `stats::weekStart` snap feeds the week agenda, week
  review, "Week of …" label, and month grid (headers rotate, column of
  the 1st re-derived).
- **Prefs.h** (new): typed, self-clamping getters — garbage on disk,
  never in the program. Pull-not-push apply: MainWindow →
  PlannerPage::applyDisplayPrefs(), the one QSettings choke point.

**Battle scar (QB U10):** a local named `slots` in the new axis code was
macro-erased by Qt (`expected unqualified-id before '=' token`) — the trap
`durationLabel`'s comment already documented. Renamed `slotCount`.

**Tests: 48 domain + 16 UI + 19 auth + 11 live = 94 green**, warning-free.
New: weekStart edges + "the parameter moves real totals"; pinned-header
ancestry; window shrink/stretch (incl. widget self-observing
AppData::changed for HEIGHT — no page call); Sunday re-snap on fixed
dates; Settings persist-on-OK via the real button.

**Docs:** design-addendum-settings.md (A–I) · design-doc §3 index row +
shipped v19.3 + backlog ("settings area" retired — dialog, not a page;
Archive stays a destination) · README (status, 3 bullets) · QB section U
(U1–U14) · diagrams/settings_pref_flow.{mmd,svg,png} (SVG hand-authored:
mermaid-cli needs Chrome, unavailable in the build container).
Version.h → 19.3.0.

**Release-hygiene follow-up (owner asked about deploy + Inno):** bumped
installer/ticktimer.iss AppVersion 19.2.4 → 19.3.0 — the by-hand half of
the "bump BOTH, every release" pair that Version.h documents. Everything
else in deploy-windows.bat / the .iss is untouched by this session: same
two exes, same Qt modules (Widgets/Core/Network), dist is wildcard-copied.

---

## Session — the Pomodoro grows up (v19.4)

**Owner asks (3):** phase-end notification (sound + popup); link the
Pomodoro to the tracked block (focus→focus, break→break, paused→
distracted); a mini always-on-top timer (per screenshot).

**The refactor all three forced:** the countdown state machine moved out
of PomodoroPage into **PomodoroEngine** (QObject, DOMAIN_SOURCES, zero
widget includes) — notifications must fire while the page is hidden, the
mini card must show the SAME clock, the link needs a non-widget signal
source. TrackerService lesson, second verse: app-lifetime state is a
MainWindow-owned service; windows are faces. The page kept its widgets
and ALL QSettings reads (doctrine) and now TELLS the engine/link.

**Shipped:**
- **PomodoroEngine** — three signal grains: changed (tick, for views),
  modeChanged (transitions, for the link — steering on ticks would open
  60 tracker segments/min), phaseEnded (countdown hit 0, for the
  notifier; skip() stays silent — no toast for your own hands).
  `engaged` distinguishes paused (link: distracted) from reset (link:
  hands off) as honest state, not a heuristic. tickOneSecond() public =
  the determinism seam (nowProvider's sibling).
- **PomodoroLink** — the adapter the old header's "don't tangle two
  clean machines" warning demanded. Rule: *you pick the block, the
  Pomodoro picks the kind.* Never starts on Idle, never stops, always
  behind canTrackNow. Listens to modeChanged + ONE tracker edge
  (Idle→tracking: a join snaps into the rhythm); manual kind-switches
  while already tracking stand. Opt-in, default OFF (it writes real
  segments).
- **Notifications** — MainWindow tray icon (painted 64px disc, no
  resource file) + QApplication::beep() + showMessage toast. Zero new Qt
  modules → deploy-windows.bat untouched. Pref read AT FIRE TIME.
  Tray click raises the window.
- **PomodoroMiniWindow** — Qt::Tool + Frameless + StaysOnTop +
  translucent (own-every-pixel card, press-anchor drag, position in
  QSettings). Second FACE of the engine, owns no clock. Parent = memory
  owner, flags = still top-level (the two meanings of parent, split).
- Prefs.h: pomodoroNotify (default ON), pomodoroDrivesTracker (default
  OFF), pomodoroMiniPos. Page is the only writer.

**Tests: 50 domain + 18 UI + 19 auth + 11 live = 98 green**, warning-free.
Engine cycle (auto-flow, silent skip, round pairing, 4th→LONG break
proven by length, engaged bit); link rules 1–7 as one narrative; mini =
second face BOTH directions (button→engine, tick→label); page→engine
duration hand-off. Visual check: mini card grabbed in 3 states, pixel-
verified (focus green #2F7E6E circle, brk amber on break, rounded
corners).

**Docs:** design-addendum-pomodoro.md (A–H) · design-doc index + shipped
v19.4 · README (status, big Pomodoro bullet, counters) · QB section V
(V1–V12) · diagrams/pomodoro_engine_faces.{mmd,svg,png}.
Version 19.3.0 → 19.4.0 (Version.h AND installer/ticktimer.iss — the
grep-for-the-old-version habit, applied).

---

## Session — block-start alarms (v19.5)

**Owner ask:** notify when a planned block is starting ("notification
for the adendawidget").

**Shipped:** `BlockAlarmService` (domain, MainWindow-owned — the third
services verse; the WIDGET was the wrong owner: ten instances alive,
none necessarily visible). No stored alarm list — derives the NEXT
start from AppData, arms one Qt::PreciseTimer single-shot (coarse's ~5%
slack = 2 min late on a 40-min nap), re-derives on AppData::changed;
nap capped at 1h (int-msec overflow + suspend self-heal). Quietness:
high-water mark (born at "now" → no duplicates, no startup back-spam),
2-min grace (slept laptop wakes to SILENCE, stale starts swept, never
resurrected), own-hands both halves (created-mid-flight = behind the
mark; already-tracking = muted at toast time). Signal carries IDS —
titles resolved fresh at toast time by the identity ladder (activity /
task / ad-hoc). Toast rides the SAME tray icon (setupPomodoroNotifications
→ setupNotifications, two clients). Pref `agenda/notifyBlockStart`
default ON, checkbox in ⚙ Settings (agenda-wide behaviour ≠ the
Pomodoro's page-local toggles), read at fire time.

**Lesson the tests forced (QB W7):** this service reads the clock in
its CONSTRUCTOR (the mark), so the nowProvider-patched-after pattern
from TrackerService silently fails — dependency used in the ctor must
come THROUGH the ctor. Second ctor takes std::function<QDateTime()>.

**Honest-test moment:** the "simultaneous starts" test I first sketched
is UNREACHABLE (30-min slot grid vs 2-min grace) — rewritten as the
sweep test (one poll, stale silent + fresh announced, one emission);
the vector payload documented as headroom.

**Tests: 54 domain + 18 UI + 19 auth + 11 live = 102 green**, warning-
free. New: once-and-only-once; stale-silence + no resurrection;
created-underway; the sweep. Settings-dialog test extended with the
checkbox round-trip.

**Docs:** design-addendum-block-alarms.md (A–F) · design-doc index +
shipped v19.5 · README bullet/status/counters · QB section W (W1–W8) ·
diagrams/block_alarm_flow.{mmd,svg,png}. Version 19.4.0 → 19.5.0
(Version.h AND installer/ticktimer.iss).

---

## Hotfix — mini timer vs. a minimized main window (v19.5.1)

**Owner bug:** the pin-on-top card only stayed up while the main window
was visible; minimize the app → card gone.

**Root cause (TROUBLESHOOTING.md, new entry):** the card's "memory-only"
QWidget parent is a Win32 OWNER on Windows, and owned windows hide while
their owner is minimized — StaysOnTop never gets a vote on a hidden
window. The earlier "two meanings of parent" lesson was HALF right: Qt
splits memory-parent from window-parent, but the OS may not.

**Fix:** parentless card + the two silent services paid back explicitly:
~PomodoroPage() deletes it by hand; WA_QuitOnClose(false) so an open
card never zombifies the app after the main window closes. Regression
test pins the arrangement (parentless + attribute + flags). QB V9
rewritten as the corrected lesson; QB W9 added as the spot-the-culprit.

**Tests: 54 + 19 + 19 + 11 = 103 green.** Version 19.5.0 → 19.5.1
(Version.h AND the .iss).

---

## Session — the link becomes visible (v19.6)

**Owner report:** running a linked focus phase, "been focused 7 minutes,
I don't see any update" — plus a screenshot whose plan-vs-actual bar was
almost all red (distracted).

**Diagnosis:** everything WORKED; nothing acknowledged it. On a 2-hour
block the bar grows ~0.5 px/min; the link follows its rules silently;
and if the block isn't being tracked (or the checkbox is off), the link
is inert BY DESIGN with zero feedback. Invisible correctness reads as
broken.

**Shipped:**
- **Live badge** on the tracked block: `● Focusing · 7:12`, state-
  coloured, digital seconds (a once-a-minute value would re-create the
  silence), title yields width, day view only. AgendaWidget self-
  subscribes to tracker tick/stateChanged (hidden widgets ignore
  update() for free).
- **Status line** under the link checkbox — every branch names its
  actor: driving what + recording which kind + live interval, or the
  human act it awaits, or "Link off". Listens to BOTH machines (the
  test caught the tracker-only wiring going stale on Start-after-track).
- PomodoroPage ctor +tracker +data (read-only, for the sentence).

**Bug flushed (TROUBLESHOOTING: "a seam with holes"):** four wall-clock
reads inside TrackerService bypassed nowProvider (liveSeconds,
beginInterval, heartbeat, commit end). Found because the pixel check's
fake-clock badge refused to tick; then liveDistractedTimeIsNotCounted-
AsBreak turned out to be green FOR THE WRONG REASON (qWait(1100) of
real sleep working around the hole). Sealed all four; rewrote the test
to move the fake clock — UI suite 1.3 s → 0.2 s. Rule: seam repairs are
grep-audits, not symptom patches; sleeps in tests are a smell.

**Tests: 54 + 20 + 19 + 11 = 104 green**, warning-free. New UI test
walks the status line's four sentences. QB V13–V15. Version 19.5.1 →
19.6.0 (both files).

---

## Session — adoption on the play edge (v19.7)

**Owner ask (after their screenshot showed pomodoro running, tracker
"Idle — not tracking"):** start the Pomodoro WITHOUT clicking focus on
the block first and still get the link.

**Shipped:** the rule evolved, not broke: "never picks a block" existed
to prevent GUESSING — but pressing ▶ over a planned block involves none:
you picked it when you planned it, and the no-overlap law makes "the
block under the clock" at most one (new TrackerService::liveEventNow()).
Adoption fires on exactly two edges — engine running false→true, and
setEnabled(true) mid-run — with three refusals, each a test: phase
flips never adopt (a human Stop stays stopped; ▶ again re-adopts),
paused never adopts (no DISTRACTED slander on untouched blocks), no
live block → wait. Status line makes the offer concrete: names the
live block + the one action.

**Tests:** the old rule-1 assertion went red ON SCHEDULE (the law
changed); rewritten as pomodoroLinkDrivesTheBlockUnderTheClock — eight
rules, one narrative. UI narrative test updated (concrete offer). QB V3
corrected in place (bank-honesty policy, second use) + V16 (why each
excluded trigger would fail). README, addendum §C v19.7 note, diagram
link box + edge label refreshed. **54 + 20 + 19 + 11 = 104 green.**
Version 19.6.0 → 19.7.0 (both files).

---

## Session — blocks end on time, and you can hear it (v19.8)

**Owner report at 12:00 (screenshot):** badge still "Focusing" on the
10–12 block, Pomodoro grinding at 21:09, Lunch unannounced. Plus:
"create an mp4 for the notifications."

**Shipped:**
- **Exit door:** TrackerService::enforceWindow() rides the existing
  1-second tick (a check, not a timer); canTrackNow is now BOTH doors.
  Commits the in-flight interval with its real end stamp, stops, emits
  trackedBlockEnded(id). Deleted-while-tracking ends itself by the same
  door. moc battle scar en route: my duplicate `public slots:` section
  swallowed following declarations ("Not a signal or slot declaration")
  — the header's ORIGINAL slots section had documented the exact trap;
  fixed by plain public method (PMF connects need no moc).
- **Link pauses the engine** on trackedBlockEnded (enabled && running):
  pause, never reset — cycle survives lunch; ▶ later adopts the block
  then under the clock. The ONE tracker→engine message (adapters may be
  bilingual so machines stay monolingual). Rule-8 interlock now
  load-bearing: the pause must not re-adopt-and-stamp-Distracted.
- **"Finished" toast** (third notifier client), same agenda pref, names
  the block, says whether the Pomodoro was paused.
- **Real chimes:** two synthesized WAVs (mallet-bell family: phase =
  A5→D6, block = C6-E6-G6 arpeggio) embedded as Qt resources; played by
  QSoundEffect behind TICKTIMER_HAS_MULTIMEDIA (OPTIONAL_COMPONENTS —
  missing module costs sound, never features). Likely original mystery:
  QApplication::beep() maps to Windows' often-silenced "Default Beep";
  Focus Assist named as the remaining toast suspect. Owner got .mp4
  previews (app ships WAV: QSoundEffect wants uncompressed).

**Tests: 56 + 20 + 19 + 11 = 106 green.** New: exit-door narrative
(open window inert / real end stamp / idempotent) and pause-on-end
(engaged survives; unlinked machines stay strangers). QB W10–W12.
Version 19.7.0 → 19.8.0 (both files). Diagram tracker→link edge
relabelled.

---

## Hotfix session — own the pipelines (v19.9)

**Owner report on v19.8:** still the Windows beep (not the chimes), and
no popup at all — though the beep firing proved the handler ran.

**Root causes (TROUBLESHOOTING: "system beep instead of the chime"):**
two RENTED pipelines failing quietly. (1) Their Qt kit has no Multimedia
(optional in the installer) → my ifdef degraded to beep in silence — a
degrade nobody can see is indistinguishable from a bug. (2) showMessage
only SUBMITS a balloon; Windows declined without error.

**Shipped:**
- **Three-tier chime**: QSoundEffect → winmm PlaySoundW(SND_MEMORY|
  SND_ASYNC) — always present on Windows, zero installs, plays the real
  WAV from resources (bytes captured by value: the async player must
  outlive scope, QB W14) → beep floor. Configure PRINTS the tier.
  Tier-2 branch mock-compiled here (fake Win32 surface, real Qt) since
  the container can't build Windows code.
- **NotificationToast**: app-owned popup (mini-timer recipe +
  WA_ShowWithoutActivating + WA_DeleteOnClose), top-right stack,
  fade-out, click-dismiss. Replaces all three showMessage calls; tray
  stays for presence/raise. Qt timing scar (QB W15): destroyed() fires
  while ~QObject runs and QPointers may not be null yet — the stacking
  test caught the survivor parked one slot low; registries must evict
  by sender identity.

**Tests: 56 + 21 + 19 + 11 = 107 green.** New: toast structural test
(unsuppressable + polite + fire-and-forget + stacking + close-ranks).
QB W13–W15; addendum §G superseded note; README. Version 19.8.0 →
19.9.0 (both files).

---

## Hotfix — the tier travels with the source (v19.9.1)

**Owner build failure (MinGW, Qt 6.11.1, no Multimedia):** test_ui.exe —
`undefined reference to __imp_PlaySoundW`; plus a nodiscard warning on
QFile::open (new in their Qt).

**Cause & fix (TROUBLESHOOTING entry):** MainWindow.cpp compiles into
THREE targets; winmm + the multimedia definition were hung on the app
only. Now an INTERFACE library `ticktimer_sound` carries the tier's
usage requirements to every consumer (app, test_ui, screenshot-tool) —
dependencies travel with the source. open()'s verdict checked (failed →
empty bytes → existing beep fallback). Proven both ways in CI: tier-1
build green AND a forced tier-drop build
(-DCMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia=ON) green across all
targets — "works without the module" is now a tested claim. QB W16.
**56 + 21 + 19 + 11 = 107 green.** Version 19.9.0 → 19.9.1 (both files).

---

## Session — Repeat, made real (v19.10)

**Owner ask (TickTick screenshots):** "the feature" for both tasks and
activities — read as RECURRENCE (stated assumption; reminders already
exist as block alarms).

**Archaeology:** Task.repeat existed since v7 — stored, edited, chip-
displayed, ACTED ON BY NOTHING. Decoration wearing behaviour's clothes.

**Shipped:**
- **One invariant carries it all: the rule lives on the newest link.**
  Spawn strips the old item — duplicate-spawn immunity for free
  (done→undone→done finds no rule), honest archives, chains without a
  chain table.
- **Tasks:** completion is the metronome — setTaskDone spawns next
  (fresh id, +rule advance via shared nextOccurrence; Qt's month
  clamping accepted).
- **Blocks:** the calendar is the metronome — Event.repeat (borrowed
  Task::Repeat, debt documented QB X6), AppData::rollRepeats(today) at
  startup + midnight (MainWindow owns the calendar of knocking, domain
  owns the door). No retroactive backfill; occupied dates SKIPPED via
  isFree (366 tries then retry tomorrow); spawns copy identity not
  history (task link demoted to text — removeTask pattern, 3rd use).
  Collect-then-mutate against the invalidated-iterator trap (QB X5).
- **UI:** EventDialog "Repeats" combo (applies on change — control
  panel, not form); ⟳ chip on the block anatomy line (pixel-diff
  verified: 54px glyph run vs twin). Format v8→v9, absent-field
  migration.

**Tests: 60 + 22 + 19 + 11 = 112 green.** New: task spawn + chain-guard
cycle; honest roll (no backfill, idempotent-per-day, segments empty);
skip-don't-fight; JSON round-trip + pre-v9 read. QB section X (X1–X7).
Caught up the design-doc shipped line (had drifted at v19.6 numbers) —
drift noted; README counters had drifted too (v19.9 UI bump missed).
Version 19.9.1 → 19.10.0 (both files).

## Session — Model/View, made real (v20.0.0)

**Archaeology:** every list in the app is rebuild-on-`changed()` — delete all
child widgets, build them again from a fresh query. Honest, instant at this
size, and the exact opposite of how Qt shows a list. `READING_GUIDE.md` had
flagged model/view as the one missing fundamental. Paid here, once, on the
cleanest candidate.

**Why Upcoming:** derived (no save code → can't corrupt disk), already had a
priority filter (maps straight onto a proxy), custom cards (real reason for a
delegate), self-contained (one page, one consumer). Behaviour-preserving —
existing UI tests are the safety net.

**Shipped:**
- **`TaskListModel : QAbstractListModel`** — the adapter. rowCount()/data()
  over a **by-value `QVector<Task>` snapshot** (upcomingTasks() hands back
  `const Task*` that dangle on edit — snapshot, don't point). Owns the
  `AppData::changed` → `refresh()` connection the page used to own;
  beginResetModel/endResetModel because the list is fully re-derived ("assume
  everything moved"). Custom roles namespace `taskmodel::Role`, incl. a
  `BucketRole`. Deliberately widget-free (no Theme.h → no QApplication).
- **`TaskFilterProxy : QSortFilterProxyModel`** — the lens. filterAcceptsRow()
  (priority) + lessThan() (due date, then title) — the old lens's remove_if and
  upcomingTasks()'s sort, both relocated to view logic.
- **`TaskCardDelegate : QStyledItemDelegate`** — the brush. paint() card +
  section header, sizeHint() height; section headers via the
  contiguous-bucket + draw-on-change trick. Interaction is hit-testing:
  editorEvent() → doneToggled / deleteRequested / editRequested; one
  `geometryFor()` so the clicked checkbox is the drawn one. Edit dialog parented
  to window(), never the vanishing row (double-free rule, again).
- **`UpcomingPage`** — now pure wiring: builds the pipeline once, wires delegate
  signals to AppData, points lenses at the proxy. Rebuilds NOTHING on change.
  283 → 177 lines.
- **Diagram** `diagrams/model_view_pipeline.puml` (+png/svg): before/after.

**Not changed, on purpose:** every other page still rebuilds; TaskRow stays
(Activities uses it); AppData untouched — the domain doesn't know one consumer
stopped rebuilding. A lesson and a template, not a crusade.

**Tests: 60 + 6 + 23 + 19 (+ 11 live) = 108 automated, 119 with server.** New
`test_taskmodel` (6): only-dated-undone, roles, bucket-by-today,
resnapshot-on-change, proxy filter, proxy sort. test_ui +1
(`upcomingDelegateHitTestsClickZones`); `upcomingLensesFilterByPriority`
retargeted from hunting QPushButtons to reading the view's model — a better
test (asserts the truth a delegate renders). QB section Y (Y1–Y14). Version
19.10.0 → 20.0.0 (both files) — model/view is a notable, named feature.

## Session — Model/View, deepened: granular updates (v20.1.0)

**The itch:** v20's model was honest but blunt — `refresh()` called
`beginResetModel()` on *every* change. That's rebuild-on-change wearing a
model's coat: the view drops scroll position + selection each time, can't
animate. The actual payoff of model/view is incremental updates; v20 wasn't
cashing it in yet.

**Obstacle:** the source is a derived query (`upcomingTasks()`) that recomputes
wholesale — it never says "row 3 changed." So the model must DIFF old vs new and
emit the matching signals itself. The diff is the lesson.

**Shipped — `refresh()` now decides (see `diagrams/model_refresh_decision`):**
- same ids + order → `dataChanged` only on rows whose **visible** role moved
  (description-only edit → NO signal; scroll/selection kept).
- structural change, survivors keep order → `beginRemoveRows` (bottom-up) +
  `beginInsertRows` (top-down) + `dataChanged` for edited survivors.
- a survivor reordered (due-date edit re-sorts it) → `beginResetModel` fallback.
  "Granular when you can prove it, reset when you can't." The fallback is a
  feature: a half-correct diff desyncs the view (blank/dup rows, crashes);
  knowing your diff's boundary and resetting loudly past it is the senior move.
- `rolesEqual()` compares only card-painted fields — the reason a description
  edit is silent. Snapshot still kept fresh; signal suppressed.

**Not changed:** delegate, proxy, page, AppData — all untouched. The proxy
(`dynamicSortFilter`) consumes the new granular source signals for free.

**Tests: 60 + 11 + 23 + 19 (+ 11 live) = 113 automated, 124 with server.** 5 new
model tests use `QSignalSpy` to assert the EXACT signal, not just end state:
editingTitleEmitsDataChangedOnly, editingOnlyDescriptionEmitsNothing,
completingTaskRemovesOneRow, addingTaskInsertsOneRow,
reorderingByDueDateFallsBackToReset (pins the reset boundary so nobody
"optimises" it away). QB Y15–Y19. New diagram `model_refresh_decision.puml`
(+png/svg). Version 20.0.0 → 20.1.0 (both files).

## Session — Model/View, reused for contrast: Activities tasks (v20.2.0)

**The ask:** apply the Upcoming pattern to the Activities detail pane's task
list, for contrast. Contrast is the whole value — the differences teach more
than another identical copy would.

**Archaeology:** the detail pane rebuilt EVERYTHING on changed() — header, both
add-inputs, all TaskRow widgets — which is exactly why it carried an elaborate
deleteLater crash comment: typing a task + Enter freed the input mid-signal. Only
ONE UI test builds ActivitiesPage (the crash test); small blast radius. TaskRow's
only real consumer was this pane.

**Shipped:**
- `CategoryTaskModel` — a PARAMETERISED model over `tasksIn(categoryId)`, re-
  pointed by `setCategoryId()` on rail selection; plain reset refresh (short
  list). Widget-free, unit-tested alongside TaskListModel.
- `CategoryTaskDelegate` — FLAT rows (contrast to Upcoming's cards) with 5 hit-
  zones: checkbox (toggle done), title (edit dialog), due badge (DueDateDialog),
  archive-when-done, delete. Reflects done via checked box + strikethrough.
- `ActivitiesPage` detail pane rebuilt as a PERSISTENT skeleton (QStackedWidget:
  empty / content). Task input + model/view are persistent; header + activity
  rows refilled in place via a deleteLater `clearLayout()` helper. Task list is a
  fixed-height QListView (no inner scroll) so the pane's QScrollArea scrolls as
  one — `updateTaskViewHeight()` keeps its height in step with row count.
- **No proxy** (no lens, keep insertion order) — the deliberate restraint vs
  Upcoming's proxy.
- **TaskRow DELETED** (.h/.cpp + CMake) — zero consumers after this, the clean
  inverse of the second-consumer extraction.

**The payoff:** the add-task input is persistent, so the mid-signal-delete crash
is impossible by construction. The crash test was RETARGETED from "the panel dies
later, safely" to the stronger "the input never dies."

**Tests: 60 + 14 + 23 + 19 (+ 11 live) = 116 automated, 127 with server.** 3 new
CategoryTaskModel tests + retargeted crash test. QB Y20–Y25. New diagram
`activities_modelview_contrast.puml` (+png/svg). Version 20.1.0 → 20.2.0.

## Session — Incremental updates, generalised: the shared base (v20.3.0)

**The itch:** two snapshot models existed (Upcoming, Activities), but only
Upcoming updated incrementally — Activities still reset on every change. Bringing
the granular diff to Activities without a second copy meant giving the diff one
home.

**Shipped:**
- `TaskSnapshotModel` — an abstract QAbstractListModel base owning the snapshot
  (m_rows), rowCount(), and BOTH update paths: `applySnapshot()` (the v20.1 diff:
  dataChanged / insert / remove, reset iff survivors reorder) and
  `resetSnapshot()` (plain reset for a context swap). Subclasses fill two holes:
  their snapshot source and `rolesEqual()` (which fields their delegate paints).
  Template Method; the "second-consumer rule" applied to an ALGORITHM, not a
  widget.
- `TaskListModel` and `CategoryTaskModel` both rebased on it. TaskListModel's
  behaviour is unchanged (16 model tests still green — the proof the extraction
  was behaviour-preserving).
- **Activities is now incremental:** `setCategoryId()` stays a reset (context
  swap), but `refresh()` (in-place edit) now diffs — ticking a checkbox flips ONE
  row via dataChanged instead of rebuilding the list; archiving removes one row.
- The two `rolesEqual` overrides differ on purpose: Upcoming omits `done` (its
  list is undone-only, completion makes a row leave); Activities includes `done`
  (in-place checkbox + strikethrough must repaint).
- `ActivitiesPage::updateTaskViewHeight()` now also wired to rowsInserted /
  rowsRemoved (height follows a count that changes granularly now, not just on
  reset).

**Rule to remember:** reset for a context swap, diff for an in-place edit.

**Tests: 60 + 17 + 23 + 19 (+ 11 live) = 119 automated, 130 with server.** 3 new
model tests (categoryTogglingDoneEmitsDataChangedNotReset, categoryArchiving-
RemovesOneRow, categorySwitchStillResets). QB Y26–Y29. New diagram
`snapshot_model_hierarchy.puml` (+png/svg). Version 20.2.0 → 20.3.0.

## Session — Diagrams migrated to PlantUML

**What:** all 11 diagrams re-authored in PlantUML (`.puml`), replacing the Mermaid
(`.mmd`) sources. Idiomatic type per diagram: activity diagram for the refresh()
decision flow; component graphs for the pipelines / flows (model_view_pipeline,
activities_modelview_contrast, repeat_chain, block_alarm_flow,
pomodoro_engine_faces, settings_pref_flow); a proper class diagram for
snapshot_model_hierarchy; a state diagram for task_lifecycle; sequence diagrams
for share_compare and update_check.

**Style:** a shared `diagrams/_style.puml` (included by every diagram) carries the
app palette (focus #2F7E6E, danger #C25B54, ink #616974) — restyle all at once by
editing one file. `diagrams/README.md` documents the render command
(`plantuml -tpng/-tsvg`) and toolchain (plantuml + graphviz + JRE).

**Housekeeping:** PNG/SVG regenerated from the .puml; .mmd sources removed; doc
references (addendum, question bank, session notes) updated .mmd -> .puml. No code
changed, so no version bump.

## Session — Natural-language quick-add (v21.0.0)

**The pick:** of Android / AI / smaller features, AI won — it's the user's
career direction, the highest-value capture feature for an ADHD workflow, and it
fits the codebase's pure-function soul. Phase 1 is DETERMINISTIC on purpose: get
the shape right (pure parse -> struct -> preview/commit); an LLM fallback can
slot behind the same ParsedTask later.

**Shipped:**
- `nlp::parseQuickAdd(text, today) -> ParsedTask` (include/QuickAddParser.h +
  src/QuickAddParser.cpp) — pure, Core-only, `today` as a parameter (the
  summarizeDay determinism trick). Grammar: today/tomorrow/tmrw, weekdays +
  `next`, `in N days/weeks`, `aug 8`/`8 aug`/`aug 8 2027`, ISO; `!`/urgent/
  high/low; daily..yearly + `every X`; `#tag`. Rules as decisions: first-match-
  wins per facet; bare weekday = soonest on-or-after today; no-year month dates
  bump to soonest future; impossible dates stay in the title; slash dates
  unsupported (locale ambiguity).
- Activities task input upgraded: same persistent QLineEdit now parses on
  commit (addTask + updateTask when priority/repeat present — the second hop is
  a single-row dataChanged under the v20.3 granular model) and shows a LIVE
  PREVIEW QLabel on every keystroke. Same pure function on both paths, so the
  preview cannot drift from the commit.
- `#tag` resolution is the UI's job (`resolveCategoryHint`): exact
  case-insensitive name match, else fall back to the selected area. The parser
  stays category-blind (purity by delegation).
- Placeholder now teaches the grammar by example.

**Tests: 60 + 17 model + 17 nlp + 26 UI + 19 auth (+ 11 live) = 139 automated,
150 with server.** New `test_nlp` target (leanest in the project: parser +
tests, Core/Test only; 17 cases, one rule per test, all green on first build in
5 ms). 3 new UI tests: one-line -> fully-dressed task; #health re-routes past
the rail selection; preview shows while typing / hides on clear. QB Section Z
(Z1–Z8). New diagram `quickadd_flow.puml` (+png/svg). New
`docs/design-addendum-quickadd.md`. Version 20.3.0 -> 21.0.0.

**Note:** the sandbox image viewer glitched on new screenshots this session;
preview behaviour is pinned by `quickAddPreviewFollowsTyping` instead (better
than a screenshot anyway — it runs on every build).

## Session — Quick-add grammar patch: ordinal dates (v21.0.1)

**Field report:** "lab 4 report 28th july" parsed URGENT correctly but left the
date TBD — the day-number matcher used a plain toInt(), so ordinal suffixes
(1st/2nd/3rd/28th) never read as numbers. Real-world phrasing, real gap.

**Fixed, TDD-style:** wrote the failing tests FIRST (ordinalDaySuffixesParse —
including the reported line verbatim — and bareOrdinalMeansSoonestDayOfMonth),
watched them fail on the old parser, then patched:
- `dayNumber()` helper strips ordinal suffixes loosely ("22th" still reads as
  22 — quick capture forgives typos) and reports whether one was present.
- Month-day matching now takes ordinals in both orders: "28th july",
  "july 28th", "aug 1st".
- NEW rule: a BARE ordinal ("rent 28th") = the soonest such day-of-month
  (`soonestDayOfMonth`, skips months lacking the day — a "31st" jumps short
  months). A bare NUMBER stays a title word ("lab 4" must never grow a phantom
  date) — the suffix is the user stating "this is a day"; that intent is what
  licenses the guess. Guarded by bareNumberIsNeverADate.

**Tests: 60 + 17 model + 20 nlp + 26 UI + 19 auth (+ 11 live) = 142 automated,
153 with server.** All prior tests unregressed. Addendum §C updated; QB Z9.
Version 21.0.0 -> 21.0.1.

## Session — The global capture bar (v21.1.0)

**The ask:** quick-add phase 2a — capture from anywhere. Ctrl+N (or the
header's "+ Capture" button) summons a floating overlay on ANY page: type,
Enter commits, input clears and stays open (brain-dump batching), Esc closes.

**Shipped:**
- `QuickCaptureOverlay` (frameless modal QDialog, owned by MainWindow, built
  once + `popup()`-summoned). Ctrl+N wired with Qt::ApplicationShortcut — the
  "from anywhere" guarantee IS the feature. Header button = discoverable twin.
- Category rules, in order: `#tag` (explicit) -> remembered default ("capture
  memory": last captured-into category, persisted via QSettings on the
  overlay's `taskCaptured` signal) -> first category. Stale remembered id
  falls through instead of ghost-writing (test-pinned). The overlay's preview
  ALWAYS names the landing area — with no visible context, that's the trust
  story.
- **Second-consumer rule struck twice:** '#tag' resolution promoted to the
  domain as `AppData::categoryIdByName()` (exact, case-insensitive, no fuzzy
  prefix magic — a silent wrong guess beats no guess never); the preview
  readout extracted to `quickAddPreviewHtml()` (QuickAddPreview.h, header-only
  inline à la Task.h). ActivitiesPage refactored onto both; its behaviour
  unchanged (its 3 quick-add UI tests still green).
- Three surfaces, one parse: no surface can disagree with what Enter will do.

**Tests: 61 domain + 17 model + 20 nlp + 29 UI + 19 auth (+ 11 live) = 146
automated, 157 with server.** New: categoryIdByNameIsExactAndCaseInsensitive
(domain); captureOverlayUsesDefaultCategory, captureOverlayHashTagOverrides-
AndBatches, captureOverlayStaleDefaultFallsBack (UI). QB Z10–Z12. Diagram
`quickadd_flow.puml` updated to the three-surface picture. Addendum §H.
Version 21.0.1 -> 21.1.0.

## Session — Click-away dismiss + the AI fallback (v21.2.0)

**Two asks in one:** (1) field feedback — Esc-only dismissal of the capture
overlay felt unfriendly; (2) quick-add phase 2b, the LLM fallback.

**Click-away:** overlay closes on QEvent::WindowDeactivate (click anywhere
else / alt-tab); setModal(true) dropped — modality would swallow the very
outside-click that is now the gesture. Esc still works. Test sends the event
directly (offscreen-activation-proof).

**AI fallback (Ctrl+Enter, explicit — never automatic):**
- `nlp::llm` (LlmQuickAdd.h/.cpp, PURE): systemPrompt(today) — the JSON-only
  contract with today's date+weekday so relative dates resolve server-side;
  parseApiReply — envelope + fence-stripping + defensive field mapping
  (garbage degrades to the deterministic parser's defaults; missing title
  fails loudly). All logic here, all tested with forged payloads offline.
- `LlmQuickAddClient` (wire, 4th in the client family): async POST to
  api.anthropic.com /v1/messages, x-api-key + anthropic-version headers, 15s
  transfer timeout, generation counter drops stale replies. Key read AT
  REQUEST TIME (pref-read-at-fire-time): QSettings ai/anthropicApiKey then
  ANTHROPIC_API_KEY env. No key -> fails fast, offline, message names the fix.
- Overlay: eventFilter claims Ctrl+Enter before returnPressed; reply ARMS
  m_aiParse (std::optional) + re-renders preview with ✨ provenance marker;
  ANY edit disarms (stale-answer drift forbidden); commit uses armed parse or
  live deterministic parse; hint line carries state (asking… / ready / error
  with "regular parse still works").
- SettingsDialog: "AI quick-add key" row (junk-drawer rule: a credential has
  no natural home page). Written raw — one consumer doesn't earn a prefs::
  accessor (second-consumer rule cuts both ways).
- Battle scar recorded: moc chokes on multi-line raw string literals (empty
  .moc -> vtable link error); forged JSON payloads in tests are built with
  QJsonDocument instead.

**Tests: 61 + 17 model + 25 nlp + 33 UI + 19 auth (+ 11 live) = 155
automated, 166 with server.** New: 5 pure llm tests; 4 UI (click-away/Esc,
no-key hint, AI-arms-commit via QMetaObject::invokeMethod slot seam — no
network, edit-disarms). QB Z13–Z16. Diagram quickadd_flow gains the AI path.
Addendum §I. Version 21.1.0 -> 21.2.0.

## Session — Roadmap review (docs only, no version bump)

**Why:** the owner's stated destination came into focus — an **AI assistant /
secretary** that chats, asks how the day went, plans tomorrow, and makes
decisions. That reframes the AI work from "a feature" to "the product
direction," so the plan needed to say so before the next build.

**Prompted by two owner questions worth recording:**
- *"Will it use my tokens? Which model?"* — Claude.ai and the Claude API are
  separate products with separate billing; the app only ever spends API
  credits on the key in Settings, and **no key = zero network calls**. Default
  model `claude-haiku-4-5` (~$0.0017 per Ctrl+Enter at $1/$5 per Mtok).
- *"Isn't GLM-5.2 open source and runnable locally?"* — checked: MIT weights,
  genuinely open, but 744B MoE (~40B active per token, **all** 744B resident).
  ~1.5 TB unquantized; ~239 GB at Unsloth 2-bit on a 256 GB Mac Studio or
  4× RTX 3090 + 192 GB RAM, at ~3–9 tok/s. Not a laptop model, and too slow
  for conversation. Realistic local path today: a 4B–14B model via Ollama /
  LM Studio, both of which serve an **OpenAI-compatible endpoint on
  localhost** — the same plumbing as any cloud provider.

**Docs updated:**
- `06_IterationPlan.md` — the canonical plan (rather than a competing
  ROADMAP.md; single source of truth). New §3c records the arcs since v11
  (accounts/sync/share, daily-driver, model/view, PlantUML, quick-add/AI).
  §4 re-planned: §4a the AI ladder (parser ✅ → surfaces ✅ → LLM fallback ✅ →
  **provider layer ⬜ next** → chat panel → tool use → planning policies),
  §4b what's still open, §4c retired (model/view, sync). Revision-history row
  added; subtitle now says v21.2.
- `READING_GUIDE.md` — corrected the stale "model/view is the planned
  next-level lesson" note; it shipped in v20.

**Two flags raised, deliberately recorded rather than acted on:**
1. **"Tasks meet the plan" may be a prerequisite in disguise.** A secretary
   that schedules needs somewhere to put tasks; that feature has never been
   built. Revisit before tool use.
2. **Android is drifting.** README has claimed "Android-ready" for many
   versions on build-config evidence alone — no APK ever built or deployed.

**Decision pending (next session):** provider layer, or "tasks meet the plan"
first. No code changed, so no version bump and no new question-bank entries —
a planning session produces no new claims about the code to be examined on.

## Session — "Needs a block", part 1: the domain (v21.3.0)

**The arc's origin:** the owner asked to "talk more about tasks meet the
plan" — archaeology showed it already shipped (doc drift in the iteration
plan; corrected). The REAL gap: the app never *notices* an urgent task has
no time set aside. Designed across FOUR HTML prototype rounds
(prototypes/needs-a-block*.html), each round steered by owner decisions:

- v1→v2: glance-panel only; the card GATES the panel (numbers wait until
  you've looked — a pause, not a toll booth); rule = two settings OR'd
  (priority set + due window, default urgent + ≤3d); absolute dates shown.
- v2→v3: coverage is DEADLINE-AWARE (owner: "this needs to be caught") —
  block must land in [today, max(due, today)]; past/late blocks don't
  count; why-lines explain; the list is view-independent → week view too.
- v3→v4: re-arm and dismissal are TWO INDEPENDENT CLOCKS (owner's 9pm
  planning case; dismissal default 21:00, review 06:00); "Find time" spans
  days (strip shows each day's largest free run); repeated dismissal of an
  URGENT task escalates — by SPECIFICITY, not volume (decision menu at 3,
  pinned at 6).
- Final calls: counter resets ON COMPLETION; deadline change reuses
  DueDateDialog (no new text box); gate memory is per-DEVICE (QSettings);
  bounded event scan from day one; TONE SETTING CUT.

**Shipped (part 1 = domain + storage only, deliberately no UI):**
- `TaskCoverage.h` (pure `coverage::`): Rule, Escalation, deadlineOf,
  isCovered, needsBlock, rung (derived, never stored), uncoveredReason,
  rankAt. `ReturnPolicy.h` (pure): one nextReturn, three modes, used by
  both clocks — second-consumer rule firing on day one.
- Task grows `dismissedUntil` + `dismissCount` (facts → data.json → sync
  for free via the shared conversion). Format **v9 → v10**, additive,
  tolerant read, zero migration.
- AppData doors: `dismissTask` (counts, refuses invalid until),
  `clearDismissal` (count stays — history append-only), `expireDismissals`
  (rollRepeats-mold housekeeping; NOT load-bearing — needsBlock compares
  against `now` itself). `setTaskDone(true)` resets the evidence; repeat
  successors start clean. Queries: `tasksNeedingBlock` (THE derived list,
  bounded scan, pinned/overdue/urgent/rest order) + `taskUncoveredReason`
  (full history, flagged tasks only).
- prefs::: rule, escalation, the two ReturnPolicies (06:00 / 21:00
  defaults), gate enable (disable forgets last-review → honest re-arm),
  per-device last-review.

**Milestone: first session with a real Linux Qt toolchain in the sandbox**
(apt qt6-base-dev, Qt 6.4) — everything compiled and ran HERE, not shipped
blind. **Tests: 75 domain (+14, all first-run green after one missing
include) + 19 model + 27 nlp + 33 UI + 19 auth = 173 automated** (live
suite not run in sandbox). Diagram `needs_block_rule.puml` (+png/svg —
plantuml installed too; NOTE: `_style.puml` is referenced by quickadd_flow
but MISSING from the repo — flagged in addendum §H). QB section AA (12).
Version 21.2.0 → 21.3.0.

**Next:** part 2 — the gated glance panel (card, decision menu with
DueDateDialog reuse, dismissal strip, Settings sections, gate-open
derivation from lastReview × review policy, the "only when new qualifies"
re-arm mode). Then part 3 — multi-day placement + the week-view panel.

## Session — "Needs a block", part 2: the gated glance panel (v21.4.0)

**Shipped (same session as part 1 — the domain was fresh, the UI followed):**
- `NeedsBlockCard` (new widget): three shapes — GATE (the card IS the
  panel; numbers wait behind "Show my day"), STRIP (pinned rung-2 rows
  stay visible + "N more need a block" expandable), CLEAR (a quiet ✓; the
  panel is what it always was). Rows carry the absolute+relative date, the
  why-line (taskUncoveredReason), and at rung ≥1 the "Not today…" DECISION
  MENU (give it time / deadline was wrong / not urgent / put off anyway).
  Put-off strip with "bring back" in every state — dismissal never makes a
  task vanish without a trace.
- **The gate's entire state machine is one derived line:**
  `open = reviewPolicy.nextReturn(lastReview) > now` — ReturnPolicy's
  THIRD consumer, zero new code, no stored open/closed flag. lastReview is
  per-device QSettings (§C's table); "Show my day" writes it and dismisses
  NOTHING (§E, pinned by test).
- GlancePanel: stays a CONST view — card signals forwarded verbatim;
  day-content wrapped in one hideable container; `nowProvider` seam added
  (TrackerService doctrine, fourth application). PlannerPage is the
  deciding end: DueDateDialog REUSED for "deadline was wrong" (mirrors
  ActivitiesPage::chooseDueDate), dismissal `until` computed from the
  prefs clock AT FIRE TIME. Part-2 interim for "Find time": switches to
  the day view (the slot picker already lists open tasks); part 3 upgrades.
- SettingsDialog: "Needs a block" section — priority checkboxes, due
  window combo, TWO ReturnPolicy editors from ONE makePolicyEditor helper
  (unused parameter widgets HIDDEN per mode — unpickable beats validated),
  escalation spin + urgent-only, gate toggle, and the read-only coverage
  note (§A: truth, not taste). Existing widgets gained objectNames; the
  old settings test COUNTED combos (layout assumption pretending to be a
  test) — retargeted by name.
- MainWindow: expireDismissals at startup + midnight beside rollRepeats.
- deleteLater discipline: the dismiss-click rebuild destroys the sender's
  row mid-signal — the founding test_ui crash, deliberately re-walked by
  escalatedRowDemandsADecision.

**Tests: 75 domain + 19 model + 27 nlp + 38 UI (+5, incl. the retargeted
settings test) + 19 auth = 178 automated, all green; full app compiles
warning-free on the sandbox's Qt 6.4.** Diagram extended with the gate
rectangle (re-rendered). QB AA13–AA17. Addendum part-2 fence retired.
Version 21.3.0 → 21.4.0.

**Next (part 3, the last):** multi-day "Find time" — the day strip
(largest free run per day, earliest-that-fits preselected, past-deadline
days shown-but-refused), candidate-run highlighting on the day agenda, and
the same derived list rendered on the week view (WeekAgendaView already
routes clicks through planAt). Then delete the prototypes.

## Session — "Needs a block", part 3: placement (v21.5.0) + the smeared-title bug

**Bug first (owner's screenshot: the card title smeared by a teal pill).**
Root cause found by READING, then proven by rendering: the card's rebuild
teardown handled `item->widget()` but not widgets inside NESTED layouts —
and deleting a QLayout does NOT delete its widgets. The header's title +
count badge (children of the CARD, positioned by the nested HBox) were
orphaned on every rebuild, stacking stale paints under fresh ones. Cure:
recursive `deleteLayoutTree` (deleteLater at every depth). The full loop is
in docs/screenshot/: `needsblock-bug-reproduced.png` (harness rebuilt the
smear on demand) → fix → `needsblock-fix-verified.png` (same harness,
clean). Lesson banked as QB AA20: layouts manage geometry, never lifetime.

**Part 3 shipped — placement:**
- "Find time" enters PLACING MODE: banner above the day agenda (task,
  deadline, cancel), day strip with each day's largest free run (full days
  disabled-but-visible; past-deadline days red-but-CLICKABLE — a recorded
  softening of the prototype's hard refusal: the domain permits a late
  block, coverage just won't count it, and the banner + why-line both say
  so), earliest-day-that-fits preselected and jumped to.
- `AgendaWidget::setHighlightRuns` — dashed focus-green invitation
  overlays with "6:00 – 9:00 free · click to place" labels; pure
  presentation, clicks travel the ordinary emptySlotClicked path.
  (Verified by offscreen screenshot: docs/screenshot/placing-mode-preview.png.)
- The interception lives at the top of `planAt` — the ONE planning step
  both views already route through — so week-view placement cost ZERO new
  code there. Placement books one hour clamped into the clicked run;
  drag-to-resize (existing) covers "needs more".
- The week tab gained its own NeedsBlockCard: the same derived query
  rendered twice (view-independence, §H's prediction), five signals into
  the SAME named slots (the part-2 lambdas were promoted to slots — one
  target per action keeps two surfaces identical). Shared lastReview means
  one "Show my day" opens both gates: one look is one look.
- freeRunsFor: derived per call from eventsOn (sorted), never cached.

**Polish fences recorded:** week columns accept placement clicks but don't
paint the invitations (day view only); placing-strip shows at most
today+13 days.

**Prototypes DELETED** (needs-a-block*.html) per their own addendum rule —
recoverable from the v21.3 output archive.

**Tests: 75 domain + 19 model + 27 nlp + 41 UI (+3) + 19 auth = 181
automated, all green.** Diagram now covers parts 1–3 (placement box,
re-rendered). QB AA18–AA20 (arc total: 20). Version 21.4.0 → 21.5.0.

**The arc is closed.** Next per the iteration plan §4a: the provider layer
(swap AI backends), then the chat panel, then tool use — where a secretary
proposing "block the lab report Thursday?" will READ tasksNeedingBlock and
WRITE through the same doors this arc built.

**Post-ship doc audit (same session, owner asked "did you update the
docs?"):** the arc's own docs were current (addendum, QB, diagram, session
notes) — but auditing instead of asserting found six stale spots, four of
them pre-existing drift: design-doc §3's addendum index was missing BOTH
quickadd and needs-a-block rows; §4 still claimed format version **7**
(with a history list that already contradicted it — now 10, with v9/v10
entries); the iteration plan still called parts 2–3 "fenced"; the diagrams
index row said "part 1"; the README's feature list never mentioned the arc,
its "Project status" still counted "six design addenda" (there are
nineteen — now phrased count-free, pointing at §3's index), and the roadmap
carried an unchecked "Plan a task onto the agenda" that shipped eras ago.
All fixed. Lesson, again: docs drift where nobody re-reads them —
the CLAIM ("version 7") and the LIST (ending at v8) disagreed in the same
sentence for three arcs. Audit on ship, not on memory.

## Session — Deadline times, the focus panel, and the field-debugging saga (v22.0.0)

The longest session yet, and the first with a **verified build**: the mentor
environment gained Qt 6.4 mid-session, so from that point every drop compiled
clean and ran the whole suite (now **181 tests green**) before shipping.
Internally the work rolled through sub-versions v22–v22.9.1; it ships as one
release, **v22.0.0**.

### Features
- **Deadline times** — `Task.dueTime` (invalid = all-day) through the whole
  stack: storage (additive, v21 files load untouched), three guarded domain
  doors ("a time needs a date"), `dueMoment()` /
  `isOverdue(QDateTime)` overload, quick-add grammar (`5pm`, `17h30`, `noon`,
  `midnight`→23:59, `at 5` — a bare number is still never a time), the LLM
  schema, and every display surface.
- **The glance panel, redesigned twice on owner review** — final form is
  design A ("focus"): the gate presents ONE hero task (accent rail, big
  title, full-width actions), `1 of N ▾` to survey the rest; the strip state
  is two compact chips opening a **slide-over drawer** (`SlidePanel`, new
  reusable widget) — expansion is a navigation, not a mutation. Hero density
  is a *parameter* of `makeTaskRow`, so restyling the drawer later cost the
  word `true`.
- **Upcoming page** — width cap removed, full type-size step; the v22 count
  chips were built and then deleted on owner review ("too small" is fixed
  with size, not controls), machinery removed with them.
- **Pomodoro link default ON** — a `QSettings` default flip; respects anyone
  who chose otherwise.
- **Right-click the planner's period label → today** (`CustomContextMenu` as
  the second-verb idiom; `applyDate()` extracted so both gestures share one
  update path).

### The debugging saga (four war stories, all documented in addenda + bank)
1. **The unresizable window → the squashed card**: a QVBoxLayout's minimum is
   the sum of its rows; the v22 fix (fixed-height scroll area) trusted
   `QScrollArea::sizeHint()` — a cached guess. Final shape: stretch factors
   CLAIM space, hints only suggest it; `GlancePanel` hands the stretch to
   whichever section is on stage.
2. **"Show my day does nothing"** — the season finale. Press-flip diagnostics
   proved delivery; the pristine label proved the click fired and the
   re-derivation overruled it. Root cause: `main.cpp` set no ORGANIZATION
   name, so `QSettings` resolved to an anonymous path the owner's machine
   refused to persist — the same vanishing writes behind the un-stickable
   pomodoro checkbox. Fix: name the org **and** a session witness
   (`m_sessionReview`) so no storage failure can re-close a gate the user
   just opened. Plus, en route: fingerprint-gated rebuilds (destroying a
   button mid-click eats the click) and the deferred `reviewed()` signal.
3. **"My data is completely erased"** — it never was: naming the org moved
   `AppDataLocation` one level deeper, past a main.cpp comment that WARNED
   about exactly this. `migrateDataFiles` (copy, never move, never
   overwrite) now bridges the move and re-aims the ancient TimeFocusTracker
   chain.
4. **The first compile found five stale tests** — two broken by legitimate
   changes (the default flip; hardcoded hit-test pixels → now derived from
   `sizeHint()`), three event-loop sync. Tests that never run are
   documentation.

### Decisions worth remembering
- Two absent-able fields beat one `QDateTime` that must lie.
- A bound lives where its constraint lives (space → scroll; attention → the
  focus design; the v22 fold confused the two and died for it).
- Don't route a user's action through a subsystem that can fail when the
  action's effect doesn't need it.
- A comment that says "caught in testing" is a tripwire; the patch that steps
  past it owes a migration or an apology.
- Rename when the meaning changes, not the implementation (`needsBlockStrip`
  survived becoming a chip; every test passed unmodified).

### State
**v22.0.0**, data format v9 (…v8 + `Task.dueTime`, additive), **181 tests
green** on a real Qt 6.4 build (81 domain, 32 nlp, 19 taskmodel, 48 UI, auth),
flat-zip delivery (the nested-folder trap is dead), question bank at **V42**,
new docs: `design-addendum-deadline-time`, `design-addendum-v22-ui-fixes`
(with both revision post-mortems), `design-addendum-glance-focus` (§A–§F),
diagrams `deadline_time_flow` + `needsblock_height_bound` (three acts) +
`glance_focus_states`. Temporary click diagnostics still in
`NeedsBlockCard.cpp` — remove next session now the bug is closed.

## Session — The window remembers, and a test that only failed at night (v23.0.0)

A deliberately gentle session after v22's marathon: one small feature, done
properly, plus the housekeeping v22 signed an IOU for. Verified build
throughout — Qt 6.4.2 installed fresh at the top of the session, **207 tests
green** at the bottom.

### Housekeeping (the v22 IOU, paid)
The temporary click diagnostics are gone from `NeedsBlockCard.cpp`: the
`clickLog` helper, its five call sites, the press/release instrumentation
connects, and the four includes only it needed. The `showMyDay` handler is
back to real logic only.

### Feature — window & sidebar memory
- `Prefs.h` gains a `window/*` section: `windowGeometry()` (opaque
  `QByteArray` from `saveGeometry()`) and `sidebarVisible(fallback)` — the
  file's first getter to take its own default as a parameter, because the
  default is a property of the *screen*, not of the preference.
- `Widgets.h` gains `overlapsAnyScreen()` (pure, takes the screen rects) and
  `availableScreenRects()` (the impure adapter). Sibling of `isCompactScreen()`
  — both answer questions about space and know nothing about widgets.
- `MainWindow` gains `restoreWindowState()` / `saveWindowState()`, declared
  adjacent so the compact-screen guard they *both* need can't drift apart.
- 7 new tests: 4 cheap ones on the pure policy (unplugged monitor, negative
  coordinates, one-pixel overlap, zero screens), 3 expensive ones on the wiring.

### Decisions worth remembering
- **Store the blob.** When a framework hands you an opaque serialization of
  its own state, don't extract "just the useful bits" — `saveGeometry()`
  carries screen identity, maximize state, DPI and a version tag, and four
  integers silently lose all four.
- **A repair function you cannot write correctly is worse than none**, because
  it looks like a guarantee. `windowGeometry()` does no validation; only
  `restoreGeometry()` can judge the bytes, so the repair-on-read rule is
  delegated to it rather than faked here.
- **`restoreGeometry()` returning true is not permission to trust the result.**
  Qt validates the format, never your monitor layout. The unplugged-second-
  monitor bug is the most-reported bug in desktop software and it is always
  exactly this.
- **Write on intent, batch on motion.** Ctrl+B saves immediately (a rare,
  deliberate choice that should survive a force-quit); geometry saves once at
  close (a hundred resize events describing one decision). Stated cost: a hard
  crash loses the geometry and keeps the sidebar. Correct way round.
- **A correct line can become a bug when a second rule arrives beside it.**
  `nav->setVisible(!isCompactScreen())` was fine as one rule; the moment the
  user's choice could outlive a launch there were two rules that had to agree,
  so both moved into one function. Nothing about the line changed — its
  neighbourhood did.

### The bycatch (two of them)
1. **A test that failed for one hour a day.**
   `chipsOpenTheSlidePanelAndActionsFlowThrough` dismissed a task until
   `QTime(23, 0)` and asserted the put-off chip existed. The build ran at
   **23:55**; the dismissal was already past, the list was empty, the chip was
   never built. It had been broken since it was written and had simply never
   been run in that hour. The audit that followed is the better lesson: every
   *other* clock-touching test was immune because it drives a fake clock
   through the `nowProvider` seam. **A seam only protects the tests that
   actually use it.**
2. **The roadmap lied.** README listed `[ ] Drag-and-drop into folders` as
   outstanding; `CategoryTree` has implemented it for eleven versions, and the
   *design doc* has listed it as retired since v11. The doc and the README
   disagreed and nobody re-read both. Same lesson as v22's version-number
   drift, arriving from a different direction: audit on ship, not on memory.

### State
**v23.0.0**, data format v9 (unchanged — this arc adds no facts, only taste),
**207 tests green** (81 domain, 32 nlp, 19 taskmodel, 19 auth, 56 UI) on Qt
6.4.2. New docs: `design-addendum-window-memory` (§A–§I), diagram
`window_memory_restore`, question bank at **V57**. README roadmap corrected
(two lines), design-doc §3 index and retired-list updated. No diagnostics
left in the tree.

## Session — v23.1.0: the tradeoff that lasted one night

First field report on v23.0 arrived within hours: **"Ctrl+B works; window
position and maximized state don't."** That split is the exact signature the
design predicts when `closeEvent` never runs — which reclassified §E's
"accepted cost" (a hard crash loses geometry) into a bug, because "a hard
crash" quietly included **Qt Creator's Stop button**, i.e. how a developer
ends the app dozens of times a day. The give-away in hindsight: the manual
test instructions opened with a ⚠️ about which button to close the app with.
When testing a feature needs a warning about how to exit the program, the
warning is the design apologising.

### The fix — debounce, not write-per-event
`moveEvent` + `resizeEvent` + `changeEvent` (`WindowStateChange` — maximize
lives THERE, not in resize; that's why the trio has three members) each
restart a 1s single-shot QTimer; only silence fires `saveWindowState()`.
`QTimer::start()` on a running timer restarts it — that one fact is the whole
debounce. The v23.0 objection ("one decision, one write") survives intact;
the memory is now ≤1s stale no matter how the process dies. `closeEvent`
demoted to a courtesy pass: still right for shutdown WORK (committing the
live interval), no longer the only copy of anything.

### The gate
`m_windowStateRestored` blocks scheduling until restore has finished —
without it, construction's own move/resize events (describing the DEFAULT
rectangle) would schedule a save that erases the real memory 1s after every
launch: the feature deleting itself. Flag set after the `restoreWindowState()`
call, not inside it (three early returns; a flag needed on every exit path
belongs after the call). Plus `!isVisible()` and `!isMinimized()` guards — a
minimized window's rectangle is nobody's decision.

### Tests: 2 new, both educational in failure
- `geometryIsWrittenWithoutAClose` — resize, wait past the debounce, **never
  call close()**: the blob must already be on disk. Passed first try.
- `startupDoesNotOverwriteTheStoredGeometry` — failed TWICE before passing,
  both failures in the test, not the code: (1) baseline captured before
  `close()`, which bakes in "debounce bytes == close bytes" (offscreen says
  no); read the baseline after the window is gone — the claim is only about
  construction. (2) pixel-exact `QSize(870,610)` vs restored 870x628: the
  offscreen platform's frame margins differ before/after first show and
  `restoreGeometry` does frame math. Assert the property (storage untouched,
  restore acted), not the platform's arithmetic.

### The embarrassing one
`Version.h` still said **22.0.0** — the v23.0 session updated the notes,
README, and question bank and missed the single source of truth itself, the
file that exists *because* versions drifted. Also caught: README claimed 166
tests (actual: 207 then, 208 now). A single source of truth only works if
it's on the ship checklist. Stamped 23.1.0.

### State
**v23.1.0**, data format v9, **208 tests green** (81 domain, 32 nlp,
19 taskmodel, 19 auth, 58 UI), Qt 6.4.2. Addendum §E rewritten as
tradeoff-plus-revision (the failure kept in the record on purpose), diagram
updated (debounce path added; closeEvent relabelled "courtesy pass"),
question bank at **V62**. Open question for next session: user to confirm
whether the header tagline is visible — if hidden, their display scaling
trips `isCompactScreen()` (<600 logical px) and the geometry skip is by
design; the threshold becomes the next fix.

### Post-ship doc audit (v23.1, closing)
Owner confirmed the tagline is visible — compact-mode hypothesis closed; the
debounce was the whole fix. Asked "did you update the documentation?"; the
answer from memory was yes, the answer from an audit was *mostly*: README
still said "nineteen" addenda (23 on disk), addendum §G/§H still described
v23.0's test counts, the design-doc index row still advertised the
write-on-close tradeoff that lost, and TROUBLESHOOTING.md had no entry for
the session's own textbook symptom. All four fixed; the symptom
("sidebar persists, geometry doesn't → closeEvent never ran") is now in the
troubleshooting log where the next person will actually look. Fourth
demonstration this arc of the same rule: **answer doc questions with grep,
not with memory.**


## Session — v24.0.0: the vendor becomes a dropdown

The provider layer, flagged "next" two sessions running, finally built.
`LlmQuickAddClient` had five vendor facts welded into one function (host,
path, header, model, reply shape); a **provider** is those facts named and
gathered into a value — base URL + dialect + model + key — and **two dialects
cover nearly the whole market**, because the industry converged on OpenAI's
request shape. Five built-ins: Anthropic, OpenAI, Groq, **Ollama (local, no
key, no bill)**, Custom. The wire client now asks `ai::` for its URL, body and
headers; what's left in the untestable file is POST, timeout, status codes,
staleness — too thin to hide a bug in, which was the point.

### The design argument, settled and recorded
Value + free functions over a closed enum, not a strategy hierarchy: the
dialect varies in DATA, not stateful behaviour, so `-Wswitch` is the registry
and purity keeps the whole layer in the Core-only suite (12 new tests in
`test_nlp`, all offline). The revisit criterion is written into the addendum's
§C — when the chat loop brings streaming/multi-turn state, THAT is a real
object. Deciding the reversal condition now is what makes the simple choice
safe.

### Two traps, one per layer
1. **`QUrl::resolved()` deletes Groq.** Its API mounts under `/openai`, and
   RFC 3986 resolution replaces the base's whole path — the prefix vanishes,
   every request 404s. Correct per spec (a browser's question), wrong for our
   intent. `endpoint()` concatenates; a test pins it.
2. **QSettings silently eats `ai/model/<id>`.** First cut used singular key
   names; the migration test failed on its FIRST run — the migrated model
   read back empty. QSettings forbids a name being both a value and a group,
   and v21 stored `ai/model` as a value. Fix: plural names (`ai/keys/`,
   `ai/models/`) AND remove-legacy-before-write. Second project instance of
   QSettings failing silently (v22.7's anonymous path was the first); the
   pattern is now in TROUBLESHOOTING where the next person will look. The
   suite caught it before a user could — day-one proof of writing the
   migration test WITH the migration.

### Keys are per-provider, and the dialog earned a buffer
One global model would send claude-haiku to OpenAI on the first switch. So:
`ai/keys/<id>` + `ai/models/<id>`, a one-time idempotent migration from v21's
single-vendor entries (copy-into-empty-only, then remove — ONE copy of a
credential), and a stash-then-load edit buffer in Settings so switching the
combo attributes each typed key to the provider being LEFT. OK writes every
provider touched; Cancel still writes nothing, proven through three switches
by the new UI test. `needsKey=false` for Ollama corrected the old global
"no key -> refuse" guard, and a keyless request sends NO auth header rather
than a malformed empty Bearer.

### Bycatch
- **`Version.h` now enforces itself**: constexpr parser + `static_assert`
  stops the build if the STRING disagrees with the MACROS — the exact drift it
  shipped with in v23.0. Checking, not generating, because the .rc files eat
  the string as a single token.
- **`diagrams/_style.puml` was missing** — documented in the diagrams README,
  `!include`d by every .puml, absent from the tree; `plantuml *.puml` failed
  wholesale. Restored (palette reconstructed from Theme.h), new diagram
  rendered against it, and an existing diagram re-rendered as proof. Also
  noted: `window_memory_restore` is still Mermaid despite the PlantUML
  migration claim. Same drift family as v23's roadmap line: grep, not memory.

### State
**v24.0.0**, data format v9 (unchanged — providers are preferences, not
facts), **223 tests green** (81 domain, 44 nlp, 19 taskmodel, 19 auth, 60 UI)
on Qt 6.4.2 (Linux this session; toolchain reinstalled from bare). New docs:
`design-addendum-provider` (§A–§J), diagram `llm_provider_dialects`,
TROUBLESHOOTING entry (the QSettings value-vs-group loss), question bank at
**V72**. README status/counts/roadmap updated; design-doc §3 index row added;
iteration plan §4a provider row flipped to ✅ with the chat panel now marked
next. `Version.h` stamped 24.0.0 — by hand this time, by the compiler
forevermore.

## Session — v25.0.0: the assistant gets eyes

The chat panel, flagged "next" since the provider session. Scoped
deliberately: chat + **sees the day**, read-only — a chat that cannot touch
your data is safe to get wrong; the moment it can call `addTask()` you are
debugging conversation state AND mutation policy at once. Tool use is the
next session's line to cross.

### Two pure layers, two suites — the split IS the design
`brief::dayBriefing(data, today, now)` turns the aggregate root into one
text block (domain-pure → test_domain); `chat::` owns the transcript, the
character-budget `window()`, and the system prompt (Core-pure → test_nlp).
*brief:: knows tasks but no vendors; chat:: knows vendors but no tasks.*
They meet only in ChatPage, one string handed to another, and the BUILD
enforces the separation — ChatSession.cpp compiles inside the Core-only
test_nlp target.

### §C's promised revisit, honoured: multi-turn is still data
The per-message shape is identical across dialects ({role, content}); only
the system prompt's home differs — the same one difference the one-shot body
already expressed. So `ai::chatRequestBody` is one switch and `requestBody`
now DELEGATES with a one-turn list; `oneShotBodyIsAOneTurnChat` compares the
outputs byte-for-byte so a re-inlined second switch cannot drift silently.
All 44 pre-existing nlp tests passed through the delegated path unchanged —
the refactor proven before a single new test existed. Promotion trigger
sharpened for the record: streaming / tool-call transcripts, where a dialect
holds state ACROSS calls.

### The near-miss: rail order vs stack identity
The Assistant's button sits ABOVE Archive's; its page is index 6, after
Archive's 5 (showPage(5) is an identity the screenshot tool and old notes
already rely on). Appending nav buttons in VISUAL order would have made
showPage(5) show the Archive while highlighting the Assistant — both are
QToolButton*, so no compiler objects. Caught during the build, resolved as a
sentence written at both sites: the layout decides where a button SITS;
m_navButtons is indexed by page IDENTITY. `assistantPageHighlightsItsOwnButton`
walks both pages so the sentence stays true.

### The log is a superset of the conversation
`localOnly` turns (errors, notices) are shown to the human and NEVER sent —
without the flag, the app eventually tells the model it said "couldn't reach
the AI service", which is false and the sort of thing a model builds on.
Failures land in the log as warning bubbles, not dialogs: they belong to the
conversation they happened in. `cancel()` bumps the generation BEFORE
abort(), so the abort-triggered finished() dies as stale instead of
surfacing a spurious error — Stop means silence.

### The briefing is the privacy page, in executable form
Four anti-hallucination rules encoded with a test each: empty sections say
so; counts stated, cuts visible ("+1 more"); no ids; no notes/descriptions
(plant "SECRET" in a description, assert it never appears). Blocks labelled
[past]/[NOW]/[upcoming] against a nowProvider seam; the task partition
REUSES upcomingTasks() so app and assistant cannot disagree about what
"upcoming" means. Rebuilt at FIRE TIME every turn — add a task between two
questions and the second knows it. "What can it see?" shows the exact text,
verbatim, plus which provider receives it.

### Idioms declined, on the record
No model/view for the log (append-only, tens of rows, variable height —
virtualisation irrelevant, and a delegate's word-wrap sizeHint is the cost
with no payer); no rebuild-on-changed() (the log grows at one end; a rebuild
throws away scroll position to re-derive a one-row change). The v20 arc
taught what the pattern is for; declining it here is the same lesson
finishing.

### Process notes (the embarrassing section, maintained honestly)
Two self-inflicted edit wounds, both caught by the compiler/tests within
minutes: a str_replace that ate the customProvider test's header, and one
that ate test_domain's QTEST_GUILESS_MAIN ("undefined reference to main" —
the linker as proofreader). Both restored; the habit reinforced is the one
the suite exists for: build after every edit, trust nothing that hasn't run.
One signature guess also failed honestly (updateTask has a Repeat parameter)
— fixed from the header, not from memory.

### State
**v25.0.0**, data format **v9 (unchanged** — the transcript is deliberately
not persisted), **238 tests green** (86 domain, 50 nlp, 19 taskmodel, 19
auth, 64 UI) on Qt 6.4.2 (Linux, offscreen). New docs:
`design-addendum-chat` (§A–§J), diagram `chat_turn_flow` (PlantUML, rendered
against `_style.puml`), question bank at **V82**. README
status/counts/suite-breakdown updated; design-doc §3 index row added;
iteration plan §4a chat row flipped to ✅ with tool use now marked next.
`Version.h` stamped 25.0.0 — and the v24 static_assert let it through, which
is the guard doing its job quietly. Open thread for next session: tool use —
model-emitted calls into the domain API, behind approval policies; the
read-only line drawn this session is the line that step deliberately
crosses.

## Session — v25.1.0: check the key, and prove the pipeline

Two halves, both about the same question: "did my setup actually work?"

### Half 1 — the live demo (container-only, not shipped)
A ~60-line Python stub speaking the OpenAI dialect on localhost, and a
temporary driver that seeded a believable day (past block with 50m tracked,
a block spanning `now`, an upcoming gym block, an overdue Lab 4) and drove
the REAL ChatPage against it. The stub answered by regex-reading the
briefing it received — so its replies ("You planned 3 blocks… you've focused
50m… 'Lab 4 report' is overdue") could only be right if the pipeline truly
carried the data. Both turns landed; the briefing measured 1962 chars, right
on the ~2K estimate the budget maths assumed. Demo tooling then REMOVED and
CMakeLists restored — the tree ships clean; stub_llm.py survives as a
standalone dev aid (also the recipe for testing against LM Studio at home).

Container comedy, recorded for the lesson: `pkill -f stub_llm` matched the
wrapper shell's OWN command line (it contained the pattern) and killed the
command mid-run — twice, the second time via the rm argument. The fix is the
character-class trick (`pkill -f 'stub_ll[m]'`) and the deeper habit:
processes that must outlive one command shouldn't; run stub + client in ONE
command lifetime. Also: the container reaps background daemons between tool
calls — same conclusion.

### Half 2 — the Test button (v25.1.0, shipped)
Settings → AI grows a Test row: one tiny request ("Reply with only the word
OK." / "ping"), verdict inline — ✓ in focus green or the provider-aware
error in danger red. Three decisions carry the feature:
(1) it tests WHAT IS ON SCREEN — `aiProviderFromFields()` mirrors
`ai::configured()` over the dialog's widgets, because Cancel-writes-nothing
forbids saving just to test; (2) key composition is field-first/env-second,
with the env half extracted as `ai::envKey()` and passed to the probe as a
FULL override via the new `ChatClient::setKeyOverride` seam; (3) the probe
IS ChatClient — no new wire class, so the ✓ vouches for the exact wire the
Assistant uses. Verdict cleared on provider switch (a stale ✓ vouches for a
setup nobody tried); probe parented to the dialog (an in-flight test dies
with a closed dialog).

### State
**v25.1.0**, data format v9 unchanged, **240 tests green** (86 domain, 50
nlp, 19 taskmodel, 19 auth, 66 UI). New: README AI-section line, provider
addendum **§K**, bank **V83–V85**. Deferred consciously: a "test succeeded"
live-path UI test would need a stub server inside test_ui — the offline
fail-fast paths are pinned instead, and the live path shares every line of
wire with the Assistant, which the container demo exercised end-to-end.

### Addendum — docs/AI.md (same session)
Owner asked whether a file details AI use and configuration. Honest answer:
no — README had six lines and the two addenda are developer why-docs. Wrote
**docs/AI.md**, the USER guide, in the CAPS-file convention (RUNNING,
INSTALLING, SERVER…): the two features, a provider table with costs, 2-minute
setup, Ollama and custom-endpoint walkthroughs, a "what gets sent, and when"
section (with the "What can it see?" button as its own verification), the
model-override rationale, and every wire error message decoded into a fix —
that table is the wire clients' `tr()` strings, documented as the contract
they already are. README's AI section now links it. Docs-only change:
version stays 25.1.0.

## Session — planning only, no code: the assistant roadmap

A design conversation, deliberately closed with documentation instead of a
commit. Version unchanged at **25.1.0**; test count unchanged at **240**.
Output: `design-addendum-assistant.md` (§A–§O), two diagrams, a rewritten §4a,
and one piece of doc drift fixed.

### The spine, which drew itself three times
*Code decides **when**. Code computes **what is true**. The model only
**phrases**.* It arrived independently while decomposing three separate
scenarios — the breakfast nudge (trigger = a recurring block, not learned
behaviour), "can I afford to put this off?" (verdict = deadline arithmetic
against free slots), and encouragement (evidence from the briefing, not
personality). When the same rule falls out of three unrelated problems, it
stops being a preference and becomes the architecture.

Corollary worth keeping: **the model is an enhancement layer, never a
dependency.** Every AI feature planned sits on something that already works
without it — the plain toast, the deterministic parser, the Upcoming page.
On a 4 GB laptop GPU that is not a nicety; it is what makes the thing usable.

### How much of the "AI" turned out to be arithmetic
The recurring surprise of the session. "Learn my morning routine" is a
recurring block (v19). "Have I put work in?" is `stats::summarize` over
segments kept since v1 — **no new storage at all**, including the
focus-vs-distracted signal. "Put it off" is `dismissTask(until)` + `moveEvent`
+ `isFree`, all guarded doors that already exist. **The only genuinely
non-derivable fact in the entire design is mood**, which is why it is the only
new thing being stored.

### Decisions that will be easy to get wrong later, so they are written down
- **No undo button — because the verb set makes one unnecessary.** Every verb
  the assistant can call must have an inverse it can also call; then "ask it
  for something else" *is* undo. Destructive verbs are withheld outright: the
  briefing excludes descriptions and notes, so a deleted task cannot be
  restored — the assistant never saw what it destroyed. Archive, never delete.
  The domain made this decision in v7, years before the assistant needed it.
- **Persona sits UNDER the safety floors**, never over them. If a persona can
  soften "never invent" or the read-only promise, the product ships a
  prompt-injection hole with the user holding the injector. Two floors above
  every persona: non-shaming (the Supplementary Spec), and *know your lane* — a
  planner that starts playing counsellor is out of its depth.
- **The check-in runs local, always.** Not a fallback — the default. Mood is
  the most sensitive data this app will ever hold. Per-role routing is a
  **privacy boundary**, not a convenience, and that argument arrived on day one
  rather than someday.
- **Never overwrite the user's estimate; show both.** "You said 4h · history
  suggests 6h." Silently inflating hides the pattern from the person it belongs
  to. Showing the delta teaches calibration until the app is unnecessary —
  which is the good outcome.
- **Fall back only on *unreachable*, never on 401/404.** Those are
  configuration bugs; masking them costs an evening wondering why replies got
  dumber.

### §3.30, transposed
"Platform is the wrong proxy for the actual variable" (the Android addendum's
reason for rejecting `#ifdef Q_OS_ANDROID`) settled the whole
modularity question: don't detect hardware, VRAM, or OS — *try the seat*.
A machine without Ollama refuses on localhost in milliseconds, so the
fall-through is free on every platform including Android, where the local seat
simply doesn't exist. The app answered this question once already, in a
different domain, and wrote the principle down. Reading your own docs is
cheaper than re-deriving them.

### V72, on the other path
`qwen3:8b` reports `thinking`, which breaks the Assistant two ways: `<think>`
leaking into `content`, or `content` empty with the text in `reasoning` (the
app then reports "AI reply had no text content" and discards a good answer).
This is **exactly the bug V72 records** on the Anthropic path from v21.2 —
`content[0]` versus walking to the first text block. The same class of bug was
waiting on the OpenAI path all along; a local model is what exposed it. Fix is
pure-layer and offline-testable, and it ships FIRST in v26 because nothing else
is verifiable on the owner's machine until it lands.

### Roadmap, re-sequenced (§N)
v26 AI foundation → v27 **subtasks & sizing (no AI at all)** → v28 proactive
assistant (**still read-only**) → v29 tool use (intake first) → v30 memory.
Two deliberate choices in that order: subtasks are justified on their own
merits and sequenced *before* estimation so estimation isn't written twice; and
v28 stays read-only because living with the assistant's judgment for an
iteration is the cheapest way to learn whether it deserves a write.

### Doc drift found and fixed
§4a still argued "why the provider layer is next" — it shipped in v24, and the
chat panel in v25. The paragraph had outlived its subject. Same failure family
as the v23 roadmap line and the missing `_style.puml`: **grep, not memory.**

### Hardware, for the record
Owner's machine: ~40 GB RAM, RTX 3050 Ti **Laptop (4 GB VRAM)** + integrated
Radeon. The card, not the RAM, is the constraint: `qwen3:8b` Q4 is 5.2 GB of
weights against 4 GB, so it partially offloads. Recommended local setup is a
Modelfile pinning `num_ctx 8192` (the app needs ~3,200 tokens/turn; 40 K
reserves twelve times that in the scarcest resource) with thinking disabled.
Local is therefore the **cheap, always-available seat**, not the primary brain —
which is what makes per-role routing earn its place immediately.

### State
No code touched. **v25.1.0**, format v9, 240 tests green. New:
`docs/design-addendum-assistant.md`, `diagrams/assistant_spine.*`,
`diagrams/assistant_roadmap.*`. Updated: §4a rewritten, design-doc §3 index row,
these notes. **Question bank deliberately NOT extended** — it documents what
exists, and mixing "shipped" with "planned" in a study aid is how you end up
confidently explaining a feature that was never built. V83 remains the tail;
the bank grows when the code does.

---

## Session — planning only, no code: three amendments from an outside comparison

**No code changed. v25.1.0, format v9, 240 tests green — untouched.**
Three sections added to `docs/design-addendum-assistant.md`; nothing else in
the repo moved.

### What prompted it
A comparison against **OpenClaw** (Peter Steinberger's open-source personal
agent; runs locally, lives in chat apps, executes shell commands and browser
automation). It configures two axes we had collapsed into one: **multiple
models** (providers → per-model allowlist → aliases) and **multiple agents**
(each with its own workspace, model registry, session store, and tool
permissions).

The exercise was worth an hour for one reason: it was a chance to be *wrong*
about §E before writing it, using someone else's shipped design as the check.

### Three points of independent convergence — the useful evidence
- Their "if a model breaks tools, set `reasoning: false`" is **§D item 3**.
- Their "set `contextWindow` accurately per model or get truncation surprises"
  is the **Modelfile `num_ctx 8192`** note.
- Their "add Ollama for anything you don't want sent to a third party" is
  **§E.4's check-in rule** — a local seat that exists as a *privacy boundary*,
  not as a performance tier.

Three designs reached independently from the same constraints is a much
stronger signal than one design defended in isolation.

### Where the comparison went the other way — persona
OpenClaw's persona lives in editable workspace files (`AGENTS.md`, `SOUL.md`,
`USER.md`) with **no architectural line between voice and permission**. That is
correct *for them*: the operator is the owner and already granted shell access,
so a persona file cannot escalate anything.

It would be wrong here, and §C.2's four-band assembly (safety → floors →
persona → context) is why. **The difference is not quality, it is threat
model**: TickTimer has to survive being used by someone who did not write it.
Worth recording, because "the popular project does it the other way" is exactly
the argument that will show up later, and the answer is already known.

### The three amendments
- **§B.4 — verb lists are per-role, not global.** Their per-agent allow/deny
  lists, transposed. Withholding `removeTask` globally prevents *damage*;
  scoping per role prevents a *category*. Nudge and check-in get empty verb
  lists and are therefore structurally incapable of writing, which also means
  an injection landing in a nudge has nothing to reach for. Free now (the
  structure does not exist yet, so it can be born as `role → verbs`), expensive
  after v29.
- **§E.5 — seats are named.** Their model aliases, adapted. Three surfaces need
  a display string — Settings rows, "Test all" ✓/✗, the bubble attribution tag
  — and if it is not stored once, all three invent it from provider + address
  and drift. Ours names *seats*, not models, because a seat is provider +
  address + model + key and no single field identifies it. **Hard rule: the
  name is cosmetic and never a key** — a display string that quietly became an
  identifier is how rename turns destructive.
- **§O.7 — does memory partition by role?** Their multi-agent isolation is
  partly about memory "bleed." One residue file serving both the planner and
  the check-in re-mixes the mood data §E.4 pinned to the local seat: the fact
  stays home, the *residue* rides along. Recorded undecided, because it is the
  kind of question that gets answered by default if nobody writes it down.

### What was deliberately NOT taken
- **Multi-agent isolation.** Their agents need separate workspaces because the
  agent owns its data. **Here `AppData` owns the data and guards it** — 86
  domain tests deep — and the assistant has no workspace to isolate. Importing
  the machinery would be solving a problem the domain model already prevented.
- **Sub-agents / orchestration.** Cost control for an always-on agent. This one
  answers one message when a page is open.
- **A JSON config file.** Correct for a power tool, wrong for a Settings
  dialog. The friction difference is the product difference, not an oversight.

### One thing to carry into v29
Their guidance: models under ~14B often fail multi-step tool use, and older
Llama variants break on the tool schema outright. **`qwen3:8b` is under that
line.** So the local seat is a fine *conversation* seat and a doubtful *tool*
seat — which per-role routing (§E) already expresses, and which is a second,
unplanned argument for the feature.

### Question bank
**Still not extended.** Same rule as last session: the bank documents what
exists. V85 remains the tail (corrected v25.2: this line first said V83 —
copied from the previous session's notes instead of grepping the bank,
which is the exact "grep, not memory" failure both entries cite; v25.1 had
already added V83–V85). Three new design sections that no test has ever
run are precisely the thing that must not enter a study aid.

### State
No code touched. **v25.1.0**, format v9, 240 tests green. Updated:
`docs/design-addendum-assistant.md` (§B.4, §E.5, §O.7, and the §E.4 attribution
bullet now pointing at §E.5), these notes. Next session: **§D**, shipping as
v25.2 — the reasoning-model fix, pure-layer, offline-testable.

---

## Session — v25.2.0: the reply contains more than the answer

**§D of the roadmap, shipped** — the reasoning-model fix, deliberately alone
in its iteration: pure `ai::` layer, no UI, no storage, no format bump, so
one rollback undoes exactly one thing. First code in three sessions.

### What shipped
`qwen3:8b` (capabilities `completion, tools, thinking`) broke the Assistant
two ways: `<think>…</think>` leaking from `content` into the bubble, and
replies with everything routed to a `reasoning` side field being *discarded*
as "no text content." Both are **V72's bug class on the other path** — "the
reply contains more than the answer" — and the recognition is the story:
Anthropic separates deliberation structurally (v21.2 fixed it by *walking
blocks*), the OpenAI dialect ships it inside the string, so the same class
needed a *scrub + fallback* instead. Full reasoning:
`design-addendum-provider.md §L`; flow: `diagrams/extract_text_flow.*`.

### The three decisions (short form — §L has the long one)
- **Scanner, not regex.** The lazy-dotall regex fails open on a missing
  closer — and the *unclosed* `<think>` is the truncation case we most need
  to catch. Two `indexOf`s per span, unclosed drops to end-of-text, exposed
  as `ai::strippedOfThinking()` so tests hit edge cases without envelopes.
  Runs on both dialects: harmless where the tag never occurs, and a Custom
  endpoint may claim either dialect while proxying a model that tags.
- **Fallback only on empty-after-scrub; content beats reasoning.** Recover
  a discarded answer, never concatenate deliberation onto a present one —
  that would reintroduce the leak as a side effect of fixing the silence.
  Both spellings (`reasoning`, `reasoning_content`); the "no text content"
  error survives and now means what it always claimed to.
- **`think: false` opt-in per catalog entry, never per dialect.** OpenAI
  proper 400s on unknown fields — a blanket flag breaks every cloud seat to
  maybe-help the local one. `Provider.sendThinkFlag`, Ollama only; Custom
  endpoints get no surprise fields. Best-effort by design: the Modelfile is
  the reliable off-switch, the scrub is the correctness.

### Two self-inflicted diagram bugs, for the record
PlantUML 1.2020.02 read a literal `<think>` in a label as markup (escape:
`~<`), and a stray `;` mid-label terminated it two lines early, leaving the
tail dangling. The error message named the right line both times and got
blamed anyway — the tool was innocent twice. Probe-minimally-then-bisect
found it; `plantuml` is now a render step away in-container.

### Drift found and fixed (mine)
The previous session's entry said "V83 remains the tail" — the tail was V85
(v25.1 added V83–V85). Copied from the *prior* entry instead of grepping the
bank: the exact failure both entries lecture about. Corrected in place, with
the correction visible rather than silent — a log that quietly rewrites
itself isn't a log.

### What the container can and cannot prove
246 green in-container proves the *offline* story — forged bytes through the
pure layer. The acceptance test this iteration actually exists for is
`qwen3:8b` answering cleanly on the owner's machine, ideally with the §D
Modelfile (`num_ctx 8192`, thinking off). If a tag other than `<think>`
shows up in the wild, the scrub is one `QLatin1String` and one test away
from learning it — deliberately narrow until reality votes.

### State
**v25.2.0**, format v9 (unchanged), **246 tests green** (86 domain · 56 nlp
· 19 taskmodel · 19 auth · 66 UI; live e2e on top). Changed: `LlmProvider.h/
.cpp`, `test_nlp.cpp` (+6), `Version.h`, README, provider addendum (+§L),
assistant addendum (§D struck to a stub — first section promoted out of the
roadmap), question bank (**V86–V88 — the bank grows again, because the code
did**), diagrams (+`extract_text_flow`, indexed). Next per §N: v26's
remainder — §C persona, §E routing — with §B.4/§E.5 already amended in.

---

## Session — v25.3.0: persona, or rule 4 split in half

**§C of the roadmap, shipped.** The assistant gets a voice setting — Calm
(default), Brief, Coach, Custom + one line of free text — with the safety
rules locked above every style. Full reasoning:
`design-addendum-chat.md §K`; diagram: `persona_prompt_bands.*`.

### The insight the whole feature hangs on
Old rule 4 fused two kinds of instruction: *never shaming* (safety — no
user may trade it away) and *calm tone* (exactly what personas exist to
vary). Shipping personas meant splitting it: the non-shaming half PROMOTED
to a locked floor band, the style half DEMOTED into the Calm preset. Rule 3
(brevity) followed it down — verbosity is a persona property, and Brief
exists to disagree with Calm about it. A second floor added: know your
lane (a planner playing counsellor is out of its depth).

### "How, never what" is a string equality, not a comment
`personaChangesTheStyleBandOnly` asserts everything above the STYLE marker
is byte-identical between Calm and Coach; the catalog walk feeds Custom
"Talk like a pirate" and asserts the contract, both floors, and the band
ORDER survive. The one-arg `systemPrompt` overload survives and *is* Calm —
pinned — so shipping personas changed nobody's assistant until they opt in.

### Choices worth remembering
- **The free text is a QLineEdit, maxLength 240.** §C.4's short-beats-
  elaborate warning enforced by the *widget*: one line invites a note, a
  text area invites a character sheet.
- **QSettings, not data.json.** Persona is taste; facts sync, taste stays.
  Another device gets its persona, not yours.
- **Quick-add got nothing.** A JSON parser has no tone; the persona reaches
  exactly one call site (ChatPage's send, read fresh at fire time).
- **Empty band emits no STYLE header** — a header with no body reads like a
  lost instruction.
- The UI test presses the REAL OK button (house pattern from the earlier
  settings tests) after `dialog.save()` turned out to be private — driving
  internals would also have skipped whatever accept() guards.

### State
**v25.3.0**, format v9 (unchanged — QSettings only), **252 tests green**
(86 domain · 61 nlp · 19 taskmodel · 19 auth · 67 UI; live e2e on top).
Changed: `ChatSession.h/.cpp` (bands, catalog, configured band),
`ChatPage.cpp` (one call site), `SettingsDialog.h/.cpp` (two rows, save),
`test_nlp` (+5), `test_ui` (+1), `Version.h`, README, AI.md (§5b), chat
addendum (+§K), assistant addendum (§C struck — v26's remainder is §E
alone), bank (**V89–V91**), diagrams (+`persona_prompt_bands`, indexed).
Next per §N: **§E per-role provider routing as v26.0** — the migration, the
circuit breaker, named seats (§E.5), and the check-in privacy boundary.

---

## Session — v26.0.0: routing, or silence falls through and speech does not

**§E of the roadmap, shipped — v26 complete** (§D v25.2 · §C v25.3 · §E
v26.0). The Assistant gets a fallback seat that answers when the primary is
UNREACHABLE — and only then. Full reasoning:
`design-addendum-provider.md §M`; diagram: `chat_route_walk.*`.

### The one rule everything hangs on
Two failure classes. UNREACHABLE (nothing answered: refused, no route,
timeout — status 0) falls through to the next seat, announced in the
transcript as it happens. REFUSED (a server SAID something: 401/403/404/
429/5xx, unparseable 200) fails loudly on the seat that said it — a wrong
key masked by a quieter seat costs an evening of "why did the answers get
dumber". Fallback answers are attributed ("answered by Ollama (local)"):
two authors of different quality means saying which one spoke.

### Decisions worth remembering
- **Migration by derivation, not by write** — recorded deviation from §E's
  own copy-once note. Missing route key ⇒ `[configured()]` at read time.
  Can't run twice, downgrade-safe, and `ai/provider` stays meaningful as
  the primary seat. Repair-on-read was already the house idiom; a migration
  expressible as a derivation should be one.
- **`ai::Feature`, not `ai::Role`** — the name Role was taken by the
  message speaker (User/Assistant); the compiler caught the collision on
  the first build. The docs' word is still "role"; the comment says so.
- **Breaker = value + injected clock**, one per process (seat health is a
  machine fact, not a conversation fact), in-memory on purpose. planRoute's
  all-cooling ⇒ EMPTY ⇒ instant named fast-fail is the offline-mode fix.
- **The static that almost ate the hook**: first draft cached
  TICKTIMER_AI_DOWN in a function-local static — a one-shot hook in any
  process that asked before the test set it. Read per call; comment names
  the trap. Same discipline: the e2e test cleans the process-wide breaker
  before AND after, because shared state outliving a test is how green
  suites go flaky.
- **PlantUML 2020 crashed on switch-inside-repeat** (engine exception, not
  my syntax this time — checked first, lesson learned twice over). Redrawn
  as the concrete two-seat walk the UI actually edits.

### Scope cuts, all in §M
Quick-add keeps one seat (its fallback is the deterministic parser — the
walk shipped on the role with nothing else to give); "Test all", per-bubble
badge, seat-rename box = polish revs; shared primary until Check-in's
"local, always" forces per-role primaries properly.

### State
**v26.0.0**, format v9 (unchanged — QSettings only), **260 tests green**
(86 domain · 67 nlp · 19 taskmodel · 19 auth · 69 UI; live e2e on top).
Changed: `LlmProvider.h/.cpp` (Feature, routes, resolved(), seatName,
Breaker, planRoute, hook), `ChatClient.h/.cpp` (the walk), `ChatPage.cpp`
(notices), `SettingsDialog.h/.cpp` (fallback row, route save), `test_nlp`
(+6), `test_ui` (+2), `Version.h`, README, AI.md (§5c), provider addendum
(+§M), assistant addendum (§E struck — **the v26 block of the roadmap is
done**), bank (**V92–V94**), diagrams (+`chat_route_walk`, indexed).
Next per §N: **v27 — subtasks & sizing.** No AI in it at all: new domain
fields, a format bump, migration, pure modelling — a different kind of
iteration entirely.

---

## Sessions — v26.1 through v26.8 *(RECONSTRUCTED in the v26.8 audit)*

**These twelve drops shipped with no session notes at all.** The gap runs
from v26.1 straight to v27.0 — the log jumps a whole arc. This entry is
written after the fact, from the addenda and the code, and is marked as
reconstruction so nobody later mistakes it for a contemporaneous account.

**What shipped, in one line each:**

| | |
|---|---|
| **v26.1** | Settings became a **shell + pages**: a `SettingsPage` contract, a nav rail, and a dialog that knows no concrete page. `design-addendum-settings-nav.md`, diagram `settings_pages` |
| **v26.2** | Catch-up, in two slices — the pure domain (`missed::`, `reschedule::`, `Event.outcome`/`movedToId`, **format v11**), then the surfaces (`CatchUpCard` asks, `GlancePanel` forwards, `PlannerPage` decides, the two doors mutate) |
| **v26.3** | The 320-pixel lesson: first live run produced a **46-block backlog**, four buttons clipped mid-glyph, no bulk exit |
| **v26.4** | The eraser: one accidental "Skip all 46" tap, so undo and receipts |
| **v26.5** | The way back, on the map — a *derived* recovery surface, because the receipt was RAM and the accident was history |
| **v26.6** | Hidden must be distinguishable from gone: a snoozed card and destroyed data looked pixel-identical |
| **v26.7** | **B′ — one chip, three intensities**, settled by HTML prototypes in an hour. Plus seven codas (.1–.7) of layout physics and called-in debts |
| **v26.8** | Defaults are design: the horizon default dropped from 7 days to **3** |

**Why the notes went missing, which is the part worth keeping.** Session
notes get written at natural stopping points, and between v26.1 and v26.8
there weren't any. The feature was planned as two afternoons and took
twelve versions — each drop was a same-day response to a field report, and
"I'll write it up once this settles" never fired because it never settled.
The knowledge wasn't lost (`design-addendum-catch-up.md` §K–§L is one of
the most thorough documents in the repo, and §L's nine-item taxonomy of the
derail is genuinely reusable). But the *narrative index* was, and that is
what this log is for: the addenda say what the design is, the notes say
what the week was like.

**The rule, then:** a drop is not shipped until its note exists. If a
session is too rushed for a note, it is too rushed to ship — and if twelve
of them are too rushed in a row, that is the signal, not the exception.

---

## Session — the v26.8 documentation & diagram audit *(this session, docs + 2 version bumps)*

A full pass over the tree, on request: "go through the code and update the
documentation and the diagrams." Run as a review — every claim checked
against what the code actually contains, nothing taken on trust.

### Two real bugs, not cosmetics

**1. `Version.h` was four releases stale** — `26.0.0` while the tree
contains catch-up (v26.2), the chip states (v26.7) and `lookBackDays = 3`
(v26.8, visible in `MissedBlocks.h`). Fixed to **26.8.0**; the file's own
`static_assert` guard re-verified by compiling it standalone.

The instructive part: **that guard could not have caught this.** It proves
the three macros agree with the string — *internal* consistency, which it
does perfectly. Nothing in a compiler knows what version the *work* is at.
That is an external fact, and external facts need a step in a human
process. Hence the checklist below.

**2. `installer/ticktimer.iss` was FIVE releases stale** — `21.2.0`. Not
cosmetic: Inno uses `AppVersion` for upgrade detection, so the
install-over-the-top path was being exercised against a lie.

And the cause is sitting in one folder, which makes it the cleanest lesson
of the session. `Version.h` has three consumers:

| consumer | mechanism | result |
|---|---|---|
| C++ code | `#include`s the header | never drifted |
| `ticktimer.rc` | `#include`s the header | never drifted |
| `ticktimer.iss` | **a human retypes it** | drifted 5× |

The two mechanical consumers were right for five straight releases; the one
carrying a comment that shouted `MUST match` was wrong. **Mechanism beats
intention, and a comment is not a mechanism.** The mechanical fix (read the
version back off the built exe, which `ticktimer.rc` already stamps from
`Version.h`) is written into the script but left **commented out** — it
cannot be exercised without Windows and Inno, and shipping an untested edit
to the last artifact between the project and a user's machine is how a
documentation cleanup becomes an outage.

### The diagrams were in worse shape than the docs claimed

- **Four had never been rendered** — `catch_up_ladder`, `catch_up_surfaces`,
  `catch_up_chip_states`, `settings_pages`. The entire v26.1/v26.2 arc
  shipped with sources nobody could look at. All four now render.
- **Six were unindexed**, including `chat_turn_flow` — the whole v25
  chat-turn picture, invisible since the day it was written.
- **"Mermaid retired" was never true.** The iteration plan has claimed it
  since v20; there were **15** live `.mmd` files.

Converted the six that were *indexed as canonical and had never rendered
once* — that is a real defect, where "uses the older of two working tools"
is not. Originals archived under `diagrams/legacy-mermaid/`, not deleted
(same reasoning as `migrateLegacyData`: a conversion is a rewrite, and the
original is the only way to check it later). `window_memory_restore` became
two diagrams — the original drew restore and save as one flowchart with two
disconnected entry points, which PlantUML activity syntax cannot express
and which was confusing regardless. The remaining **9** `.mmd` are recorded
as named debt in `diagrams/README.md` rather than left quietly wrong.

**New: `app_architecture.puml`** — the gap that mattered. Twenty-nine
close-ups of individual arcs and no establishing shot; someone opening the
repo cold had to infer the system's shape from the file list. It draws the
seven pages, the model/view layer, the pure/wire split, and the single
`changed()` signal (in green, because that is what the picture is *about*).

### The README contradicted itself four ways

`v27.0` in the status line and `v26.8` mid-sentence. `292 tests` in one
place, `267` in another. `60 domain tests` (actually 102). `format v6`
(actually v11). All corrected against counts taken from source.

The hardcoded total was replaced with per-suite figures and *"run `ctest`
for the number of the day."* A total that must be hand-updated in three
places is a drift generator — which is exactly how it came to hold three
different values.

### The v27.0 discrepancy — recorded, not resolved

`SESSION_NOTES` and the iteration plan describe v27.0 as shipped with 267
tests green. **The code is not in this tree:** `Task.h` has no `parentId`,
no `estimateMinutes`, no `chunkable`; there is no
`design-addendum-subtasks.md`; and `format v11` is the *catch-up* v11
(`Event.outcome` / `movedToId`), not the subtasks one.

**Nothing was deleted.** Deleting documentation for a feature that may be
sitting on the owner's disk is the expensive mistake; recording the
discrepancy is the cheap one. The README status line and the iteration plan
now both carry the warning, and **this must be resolved before v27.1 or
v28** — both build on fields that may not exist.

### NEW: the shipping checklist

Added because this session found three drift bugs that a two-minute
checklist would have prevented, and one of them reached users.

**Before calling a drop shipped:**

1. **Bump `include/Version.h`** (macros *and* string — the `static_assert`
   checks they agree, nothing checks they are current).
2. **Bump `installer/ticktimer.iss`** `AppVersion` to match. The `.rc`
   files need nothing; they `#include` the header.
3. **Write the session note.** No note, not shipped.
4. **Run the two diagram checks** in `diagrams/README.md` — unrendered and
   unindexed. Ten seconds of shell.
5. **Grep the status tables** (`README.md`, `docs/06_IterationPlan.md`) for
   the previous version string. If it appears, it is stale.

**And the owner-side half — applying a drop:**

6. After unzipping, run **`grep VERSION_STRING include/Version.h`** — the
   number must match the CHANGES file's title. Five seconds; it is the
   check whose absence let v27 half-land and go unnoticed for weeks. Every
   CHANGES file now prints this command. (Mechanism beats intention,
   round three: the `.rc` files never drift because they derive; the
   installer drifted because a human retyped; a drop applied by hand with
   no check is the same missing net.)

### The lesson worth keeping

Every finding this session was the same shape: **a claim that no mechanism
checked.** The version string had a `static_assert` for the half that could
be automated and drifted on the half that couldn't. The diagram index had
no check, so it lost six entries. "Mermaid retired" had no check, so it
survived six versions as a straightforward falsehood.

Documentation does not rot because people are careless. It rots because
prose has no compiler — so the job is to keep finding the places where a
five-line shell loop can be the compiler, and to write down honestly what
is left over for a human.

### State

Code: `include/Version.h` (26.0.0 → 26.8.0), `installer/ticktimer.iss`
(21.2.0 → 26.8.0 + the commented mechanical fix). Docs: `README.md`,
`docs/06_IterationPlan.md`, `diagrams/README.md` (rewritten),
`diagrams/legacy-mermaid/README.md` (new), these notes,
`docs/CHANGES_v26.8-audit.md` (new), `docs/QUESTION_BANK.md`. Diagrams:
`app_architecture` (new), six Mermaid → PlantUML conversions, one split,
eleven newly rendered `.png`/`.svg` pairs. No behaviour changed.

---

## Session — v27.0.0: subtasks & sizing, or one field and five policies

> **RECONCILED after v28.0 — this drop never landed.** The session ran and
> the zip was built, but the owner confirmed the apply step never finished:
> none of the code below is in the tree. Kept unedited as the design
> record. Re-land FRESH when wanted; do not apply the old zip (it predates
> v28.0 and would roll it back).

**Roadmap §I + §J.1, shipped — format v11.** The first domain iteration
since the AI arc began, and the first format bump since dismissals. Full
reasoning: `design-addendum-subtasks.md`; diagram: `subtask_policies.*`.

### §I's warning held exactly
"Adding parentId is ten minutes; the consequences are the iteration." The
field is one QString. The iteration was: five queries, five decisions —
workload surfaces parents-only (the noise rule), `tasksDueOn` showing
dated pieces (a calendar that hides real deadlines lies), `taskCountIn`
counting everything (it guards removeCategory; a guard must see the whole
truth even where a display list curates). Plus the depth rule at the door,
the no-auto-complete roll-up (the tick is the reward — silent 5/5 steals
it and is wrong when a piece was missing), and the archive cascade both
ways (inverses, v7's own rule).

### Open questions from §I, answered
Children MAY carry their own deadlines (dueOn shows them); they inherit
the parent's category at birth; they may NOT have children.

### Two catches worth the log
- **Version-number drift in the ROADMAP:** §I said "format v10" — the file
  has been at 10 since dismissals. Shipped as v11. Grep-not-memory, and
  this time the stale copy was the planning doc itself.
- **Mid-enum insertion caught at review:** the two new model roles first
  landed between existing enumerators, silently renumbering everything
  after them. Harmless today (all uses are symbolic) — moved to the end
  anyway, with a comment making the append-only habit legible.

### The dialog stayed a pure question
Pieces arrive as snapshot values; changes leave as declarations (a DIFF of
toggles, a list of new titles) applied by the page through AppData only on
Accept. Cancel discards everything by construction. The uncommitted
add-line still counts on Save — losing typed text to a missed Enter would
be the dialog teaching a flinch.

### Deferred, recorded (v27.1)
§J.2 multiplier + §H affordability() (pure queries, no format change, own
session — §I's sequencing argument says they now land on a domain with the
right shape); the Activities card's n/m chip; fixture files for old-format
loads.

### State
**v27.0.0**, **format v11**, **267 tests green** (91 domain · 67 nlp · 20
taskmodel · 19 auth · 70 UI; live e2e on top). Changed: `Task.h`,
`JsonStore.cpp` (v11), `AppData.h/.cpp` (addSubtask, childrenOf, progress,
sizing, policies, cascade), `TaskListModel.h/.cpp` + `TaskCardDelegate.cpp`
(chips), `TaskDetailDialog.h/.cpp` (SIZE + PIECES), both pages' edit call
sites, tests (+5 domain, +1 model, +1 UI), `Version.h`, README, roadmap
(§I struck, §J split — J.1 shipped, J.2 planned), NEW
`design-addendum-subtasks.md`, bank (**V95–V97**), diagrams
(+`subtask_policies`). Next: **v27.1 — the multiplier and affordability()**,
then v28 (proactive, read-only) has everything it needs.

---

## Session — documentation audit after v27.0: four drifts, one real bug

> **RECONCILED after v28.0:** this audit ran against the session tree, which
> HAD the v27 code — the owner's tree never did (the drop was never fully
> applied). The doc fixes below DID land; the code they describe didn't.

**No code changed. v27.0.0, format v11, 267 tests green — untouched.**
A deliberate audit pass ("make sure everything is well documented"),
run like a review: every surface checked against what actually shipped.

### Found and fixed
1. **The §J strike deleted PLANNED content — the real bug.** Striking the
   shipped §J.1 out of the roadmap took §J.2 (the multiplier) and §J.3
   (decomposition) with it, while the stub claimed they "remain planned".
   A roadmap must shrink by what SHIPPED, not by what sits next to it.
   Restored under a visible restoration note (condensed, same arguments) —
   the correction is legible, same policy as the V83/V85 fix.
2. **Both status tables were stale.** The §N table and the iteration plan
   still showed v26 as "next" and v27 as ⬜ — five drops behind reality,
   in the same repo whose notes twice lecture about doc drift. Both now
   name what shipped, where the reasoning moved, and what v27.1 still
   owes. The iteration plan's "format v10" error (the same drift caught
   at implementation time) is corrected in place with a note.
3. **Subtasks had NO user-facing documentation.** AI.md is AI-only, and
   the README's feature sections stopped at v24 — persona, routing, and
   the whole hierarchy feature existed only in status-line clauses and
   design addenda. Added two README sections: "The Assistant (v25–v26)"
   (pointer into AI.md) and "Break it into pieces (v27)".
4. **READING_GUIDE stopped at v15.** Five arcs (model/view, quick-add,
   provider, chat/AI, subtasks) had shipped unindexed. Added §7 — one
   landmark per idea, the guide's own style — marked as audit-added.

### Checked, deliberately unchanged
- **FOR-TESTERS.md** — it is a server/sync runbook, not a feature
  checklist; nothing in v25–v27 changes how a tester runs two machines.
- **design-doc.md** — per the guide's own §198–200 note, addenda merge
  into it as deliberate future work; the subtasks addendum is the
  canonical home until that merge, matching every arc before it.
- **AI.md** — §5b/§5c already landed with their features; no TOC to sync.
- **Question bank** — documents code, and no code changed.

### The lesson worth keeping
Striking shipped sections is scalpel work: cut on the shipped/planned
boundary, not on the heading boundary. The §J stub even SAID the right
thing while the content underneath was gone — which is the worst kind of
drift, because the summary passes a skim. The audit habit (grep the
tables after every ship) would have caught 2 immediately; 1 needed
actually reading the section.

### State
Docs-only. Updated: `design-addendum-assistant.md` (§J.2/§J.3 restored,
§N table current), `06_IterationPlan.md` (v26 ✅, v27.0 ✅ / v27.1 ⬜,
format note), `README.md` (two feature sections), `READING_GUIDE.md`
(§7), these notes. Next session: **v27.1 — the multiplier and
affordability()** — with its planning prose now back where it belongs.


---

## Session — v28.0.0: affordability, or the pipeline before the model

**Roadmap §H shipped domain-first (§H.6) — and it pulled §F's nudge
pipeline in with it, because the owner closed §O.1: volunteer-mode.** "I
want it to be my secretary… give me a good heads up." Volunteering turns a
query into a nudge (trigger, surface, manners), so the slice was re-cut:
**the whole pipeline ships with no model in it**, sentences written by
C++, and 28.1 swaps exactly one box. §A's corollary as structure — the
fallback path isn't a mode, it's what shipped first. Skipped v27 per §N's
always-allowed order swap; the planned-blocks proxy (§H.3) carries
affordability until estimates exist.

**The verdict:** four values, not a score (the inputs have ~half-hour
precision; a score would pretend otherwise). `outstanding = plannedAll −
tracked` against `capacity = plannedAhead + free daytime`; Tight on
cramped / last-call / slipping — and *slipping needs both halves*: behind
your own plan two weeks out is a Tuesday problem, not a toast. **Unknown
is a verdict**: no blocks ever planned → "I can't tell how much is left,"
and it never toasts — honesty is served, not interrupted with.

**The manners (§F.3, load-bearing for an ADHD-facing tool):**
change-of-verdict (a secretary doesn't mention it four times), quiet hours
wrap-aware, cap of 3, §H.5 dismissal respect. The subtle rule: a Tight
suppressed by quiet/cap stores NOTHING — undelivered news is still owed.

**Bookkeeping in QSettings, not data.json — a real decision:** "what did I
last say today" is manners state, not a fact about a life. Losing it costs
one repeated heads-up; *syncing it would be wrong* (a laptop's nudge must
not mute the phone in the owner's hand). No format bump → this slice is
clear of the audit's v11 collision, which still gates 28.2.

**Toast seams for "distant future: animate it":** `ToastSpec` (toast as a
value), kind-driven accent (Alert = danger red), ONE `moveTo()` every
position goes through, optional action row. Joints, not motion — no
animation written; the movement seam is what makes it an afternoon later.

**Checked:** balance-verified every touched file (no Qt in the session
container — the suite runs on the owner's machine; flagged in CHANGES).
10 new pure domain tests: four verdicts, straddle split, focus pay-down,
each manners rule alone incl. the 23:40/07:30/08:00 wrap pins and the
18:01 dismissal lapse. Per V170, every fixture pins its own dates.

Changed: NEW `Affordability.h`, `AffordabilityService.h/.cpp`,
`design-addendum-affordability.md`, `affordability_flow.*`;
`NotificationToast.h/.cpp` (seams), `TaskListModel.h/.cpp` (role + its own
verdict diff — event-driven verdict moves are invisible to the base's
Task diff), `TaskCardDelegate.cpp` (pill), `MainWindow.h/.cpp` (wiring),
CMake (app + DOMAIN_SOURCES), `test_domain.cpp` (+10), Version 28.0.0 +
installer, README, iteration plan, roadmap (§H struck, §O.1 closed), bank
(**V189–V196**), diagrams index. Checklist run: versions ✓ note ✓ diagram
checks ✓ status-table grep ✓. Next: **28.1 — the model phrases the
Report** (persona rides in; briefing gains the verdict), then 28.2
check-in + mood behind the v11 resolution.


---

## Session — v27 reconciled: started, never finished (docs only, no bump)

The owner closed the audit's open question: *"v27 started but did not
finish. That is my mistake."* Half right — the session ran, the drop was
built, the apply step never finished. But the **process** had no net: a
zip applied by hand, with nothing checking it landed, is the installer
lesson wearing a different coat. The blame belongs to the missing
mechanism, and the fix is the five-second apply check now in the
checklist (item 6), printed in every CHANGES file from here on.

**What the resolution settles:**
- **The v11 collision is gone, by reality.** v11 = catch-up
  (`Event.outcome` / `movedToId`), full stop. Subtasks and mood each take
  the next free number at their own landing time — no renumbering needed.
- **28.2 is unblocked** (its gate was this exact question).
- **Subtasks re-land FRESH, never from the old zip** — that drop predates
  v28.0 and the audit; unzipping it now would roll back both. The design
  record survives (roadmap §I–§J, the annotated v27.0.0 note, bank
  V95–V97), so a re-land is a rebuild against the current tree, not
  archaeology.

**Changed (docs only, per the docs-session precedent — no version bump):**
the v27.0.0 and post-v27 audit notes annotated (kept, not deleted — they
are the design record); iteration plan row → "never landed / re-land
fresh"; README status line and the "Break it into pieces" section
annotated, dead addendum link removed; checklist +item 6; bank **V197**.
Next: **28.1 — the model phrases the Report** (nothing gates it), with
the subtasks re-land slotted wherever the owner wants the feature.

---

## Session — v28.1.0: the model phrases; the pipeline doesn't notice

**The swap the slice was cut for.** Sweep, verdict, manners: byte-identical.
New: `nudge::` (pure — prompt bands with the locked rules ABOVE the persona,
the Report flattened to labelled facts, a mechanical accept gate),
`NudgeClient` (quick-add's one-shot recipe, third use — with one
personality change: it CANNOT fail loudly; every failure collapses into
`fallback()` because nudges fire when nobody is watching), and
`deliver()` — the one exit both voices share, so bookkeeping cannot
diverge. Bookkeeping moved to delivery (a cap written before the toast
exists counts speech that may never happen); recorded honesty cost: a
21:59:58 request can deliver at 22:00:04.

**Feature::Nudge deliberately ABSENT from the routing enum** — documented
at the enum: its fallback seat is `afford::sentence()`, the quick-add
argument; a route table for a feature that cannot fail is configuration
surface with no failure to configure away.

**The link-graph save of the session:** the service reading
`chat::configuredPersonaBand()` directly would have dragged ChatSession +
Qt Network into DOMAIN_SOURCES — undoing the test-target split the CMake
file celebrates in a comment. Persona is INJECTED
(`setPersonaProvider`); MainWindow wires the real one; the default empty
band is itself a valid prompt. The include graph is architecture.

**The ask-side shipped too:** the briefing's new DEADLINE PRESSURE section
— per-task verdict WITH numbers, Comfortable stated not omitted, Unknown
honest — makes §H.1's founding question ("can I go out tonight?")
answerable in chat from the same derivation the toast uses.

**Checked:** balance-verified all nine touched files (no Qt in container —
suite runs on the owner's machine, flagged in CHANGES). +4 domain tests:
rules-above-style order pinned, numbers-in-message, the accept gate
(cleans shape, rejects essays, exact-cap passes), DEADLINE PRESSURE
end-to-end. Wire client untested-by-suite, mirrors the tested quick-add
pattern — noted honestly.

Changed: NEW `NudgePhrasing.h`, `NudgeClient.h/.cpp`;
`AffordabilityService.h/.cpp` (async speak path, deliver(), persona
injection), `DayBriefing.cpp` (+section), `LlmProvider.h` (absence
comment), `MainWindow.cpp` (injection), CMake (wire kept OUT of
DOMAIN_SOURCES), `test_domain.cpp` (+4), diagram updated (the sentence
box is now the fork), addendum §K, README, plan, Version 28.1.0 +
installer, bank (**V198–V201**). Checklist: 1–6 ✓. Next: **28.2 —
check-in + mood** (unblocked; format v12) or the subtasks re-land,
owner's pick.


---

## Session — v28.2.0 part 1: mood, or the first fact the app cannot derive

**Roadmap §G, domain slice only — the catch-up cut, chosen on the v27
lesson: a slice small enough to land whole beats a feature big enough to
half-land.** Shipped: `Mood` (coarse rough/okay/good + owner-only note),
**format v12** (the version comment closes the collision thread: 11 =
catch-up by reality, 12 = mood, subtasks take 13+), one-per-day upsert,
`trimMoods` on both midnight-knock sites (forgetting emits changed() —
forgetting is also a write), `checkin::` (heavy = ≥5h planned OR ≥2 due
within 2 days; morning [06:00,11:00); once means once, lastOffered as a
parameter so the rule tests without QSettings), and the briefing's MOOD
line — coarse only, silent when empty.

**The decision pair worth remembering:** mood → data.json (a fact; the
phone's assistant should know what the laptop's does) while the nudge
ledger stayed in QSettings (courtesy; must NOT sync). Same question,
opposite answers — that's what makes it a real test, not a rule of thumb.

**The privacy pin:** `briefingSpeaksCoarseMoodAndNeverTheNote` — the note
is the owner's words about their own state; if that test ever fails, the
fix is in the briefing. Load path: moods land through a silent door
BEFORE resetFrom/replaceAll so the existing silence/single-changed()
semantics cover them; pre-v12 files run the loop zero times, which is the
correct migration — mood cannot be back-derived.

**Checked:** balance-verified all eight touched files (no Qt in container,
suite runs owner-side — flagged in CHANGES); +5 domain tests incl. the
v12 round-trip with an accented note, trim idempotence, both heaviness
arms plus the quiet-Tuesday negative, and the [start,end) window edges.

Changed: NEW `Mood.h`, `CheckIn.h`, `design-addendum-checkin.md`,
`checkin_gate.*`; `AppData.h/.cpp`, `JsonStore.cpp` (v12),
`DayBriefing.cpp`, `MainWindow.cpp` (both knocks), CMake, tests (+5),
Version 28.2.0 + installer, README, plan, bank (**V202–V205**), diagram
index. Checklist 1–6 ✓. Next: **28.2 part 2 — the surfaces**: the toast
action meets its planned first consumer, the chat opens with the check-in
waiting, and §E.4's local-always rule forces the first per-role primary.


---

## Session — v28.2.1 part 2: the surfaces, and the leak part 1 shipped

**The v28 arc is complete.** Shipped: `CheckInService` (10-min timer,
first look 30 s post-launch — launching INTO a toast is an ambush;
`lastOffered` marked AT EMIT because a dismissed toast was still an ask),
the toast action seam's first consumer (Info kind, 12 s, tap →
showPage(6) → `beginCheckIn`), and the one-tap answer: three buttons,
recordMood through the domain door, C++ acknowledgements per level
(§G.3 — evidence, not pep; "the plan is a tool, not a judge").

**The finding of the session — part 1 shipped a §E.4 violation.** The
MOOD briefing line rode to whatever seat chat used, including cloud,
while §E.4 says the fact never leaves the machine. A privacy rule stated
in a planning doc did not survive one implementation session that never
re-read it. Fix = two mechanisms, not a citation:
`Options.includeMood` default **false** (new call sites are private by
accident, never leaky by accident) and `ai::isLocal` (loopback only —
a LAN box is still a wire), with ChatPage opting in only when EVERY
route seat is local, primary and fallback both. The privacy test now
pins the default. §E.4 in the check-in itself is satisfied by
SUBTRACTION: no model is in the loop at all; per-role primaries arrive
with the model's entry.

**Version 28.2.1, not a shared 28.2.0:** the catch-up precedent predates
the apply-check ritual, whose whole mechanism is "the number must match
the CHANGES title" — two drops sharing a number would make a
part-1-only tree indistinguishable from a complete one, the exact
blindness that half-landed v27.

**Checked:** balance-verified (the one flagged imbalance was `[start,
end)` notation inside comments — verified code-clean with a stricter
strip); privacy test strengthened (+default-silent pin), +1 isLocal
boundary test. Changed: NEW `CheckInService.h/.cpp`,
`checkin_surfaces.*`; `ChatPage.h/.cpp`, `DayBriefing.h/.cpp` (the
gate), `LlmProvider.h` (isLocal), `MainWindow.h/.cpp`, CMake, tests,
addendum (parts merged, §G–§K), README, plan, Version 28.2.1 +
installer, bank (**V206–V208**). Checklist 1–6 ✓. Next per §N: **v29 —
tool use** (the confirm loop, intake first) — or the subtasks re-land,
owner's pick, and v29's intake would actually LIKE having subtasks
first.


---

## Session — consolidation and setup (docs only, no bump)

Session close-out at the owner's request: "set the whole thing up."
Shipped a **single complete tree at 28.2.1** instead of the six
incremental drops this session produced, plus `docs/SETUP.md` (one page:
prerequisites → build → ctest → two-process launch → AI config → the
mood-locality rule → what to expect in week one) and a `.gitignore`.

**The reasoning is the session's own lesson, applied one last time.** Six
zips applied by hand in order is the exact process that lost v27 — built,
downloaded, never fully applied, then described as shipped in three
documents for weeks. Consolidation deletes the sequencing step rather
than asking anyone to be careful about it: mechanism over intention, the
thread running from the installer's five-version drift through the
apply-check ritual to here.

`SETUP.md` also names the two things a fresh reader cannot infer: that
the server must start before the app, and that mood reaches the assistant
ONLY when every chat seat is loopback-local — the §E.4 rule that part 1
shipped wrong and part 2 made mechanical. Recommending `git init` is the
same move again: version control makes the whole apply-check problem
disappear.

Final sweep: version consistent across Version.h / .iss / both .rc files
(the last two derive it), 26 diagrams all rendered and indexed, pure/wire
CMake split verified intact (DOMAIN_SOURCES holds only the four pure
headers — no *Service.cpp, no NudgeClient), build artifacts excluded from
the package. Checklist 1–6 ✓.

State: **v28 arc complete.** Next per §N: v29 tool use (the confirm loop,
intake first), or the subtasks re-land — which v29's intake would prefer
to have first.

---

## v28.3.0 — subtasks & sizing: the re-land (format v13)

**The drop:** §I subtasks + §J.1 sizing, fresh against the v28.2.1 tree.
`Task.parentId` (one level, door-enforced, category inherited at birth and
frozen by the absence of any mover), `estimateMinutes` (0 = unset) +
`chunkable` through one `setTaskSize` door, the five query policies
(parents-only workload / dated pieces on the day / count-everything guard /
archive cascade both directions / no roll-up), delete cascade with
title-demotion for every removed id, repeat-spawn carrying `parentId` +
size, orphan adoption in `resetFrom`, `AppData::Batch` implemented (all 47
emit sites routed through `notifyChanged()`), the detail dialog's checklist
+ SIZE row behind the grown ctor (compile-error-as-bug-report, v22 style)
with the `seedTaskDetailPieces`/`applyTaskDetailAnswers` free-function
pair, the "☑ 2/5" chip on Upcoming via a pieces sidecar diffed like the
verdict cache. 13 domain + 2 model tests. Full record:
`design-addendum-subtasks.md`.

**The discovery, and it matters:** the uploaded v28.2.1 tree already
contained a **half-applied earlier attempt at this very drop** — `Task.h`
complete, `AppData.h` partially written (it referenced a `PieceCount` type
that did not exist, so the tree as uploaded **did not compile**; the green
`build-release/` objects predate the partial write), everything else
absent. The v27 failure mode, repeating: a session died mid-write and the
partial state was zipped up as if finished. Resolution: the finished parts
matched §I/§J exactly and were kept; everything else was written fresh to
fit. **Process rule reaffirmed, now with two data points: land whole or
don't — and run the apply-check ritual after *every* drop, because a
partial application looks finished until it doesn't compile.**

**Numbering:** roadmap rows keep their names (v29 = tool use, v30 =
memory); this landed as a minor bump. Format v13 (11 = catch-up, 12 =
moods, 13 = subtasks). Docs corrected where the premature "SHIPPED v27.0
(format v11)" claims survived the audit (`design-addendum-assistant.md`
§I/§J + roadmap table).

**Deferred to v28.4.0:** the §J.2 multiplier + rewiring `afford::` off the
planned-blocks proxy — pure queries, no format change, on the shape this
drop built.

**Follow-up, same session:** the owner asked whether `deploy-windows.bat`
→ Inno was "still the ritual". Answer: yes for releasing — and the bat now
absorbs the other two steps. Step 0: an apply check (Version.h vs the
.iss's `AppVersion`; the pair ships in every drop, so disagreement is the
signature of a half-apply — hard stop, version printed loudly for an
eyeball against the drop's filename). Post-build: `ctest
--output-on-failure`, hard stop on red — a red suite must not become an
installer. One double-click now runs check → build → test → package;
Inno stays the only manual step after it.

State: **v28.3 shipped.** Next per §N: **v28.4** (small, pure), then
**v29 tool use** — whose intake verb (§K) now has the `addSubtask` door it
wanted, and whose confirm-card multi-verb applies inherit `AppData::Batch`
free.


---

## v28.3.1 — hotfix: the ODR redefinition (owner's first build of 28.3.0)

First build of the re-land failed everywhere with one error:
`redefinition of 'struct PieceCount'` — `Task.h:309` vs `AppData.h:59`.
The half-applied session had defined `PieceCount` (plus an unused
`SubtaskEdit` for a bulk-`setSubtasks` design it never built) in the TAIL
of `Task.h`; the re-land's survey grepped the partial state for
`parentId|estimateMinutes|chunkable|subtask` — none of which match
"PieceCount" — and never read the file past ~line 240. It then declared
`PieceCount` a second time in `AppData.h`, and every translation unit
that includes both (all of them) refused. C++'s One Definition Rule:
one struct, one definition.

Fix: `AppData.h`'s copy survives (beside `pieceProgress`, enriched with
the deleted copy's `any()`/`complete()` helpers); `Task.h` keeps the
naming note, loses both dead types; the Batch example comment's
`setSubtasks` reference now names a real door. Duplicate-symbol sweep
over every symbol the drop introduced: all single-definition.

Also corrected, honestly: the 28.3.0 notes claimed the pre-drop tree
"could not compile" (undeclared `PieceCount` in `AppData.h`). Wrong —
Task.h defined it all along, in the unread tail. The pre-drop tree was
internally consistent; **the re-land introduced the breakage it thought
it had found.**

Lesson, joining "grep, not memory" in the process file: **read the whole
file when inheriting half-finished work.** Grep answers the question you
asked; only reading answers the ones you didn't. (Also worked as
designed: SETUP §3's "send the first error" note — one error, one
minutes-fix, exactly as promised.)


---

## v28.3.2 — hotfix 2: first light (the suites meet Qt for the first time)

The 28.3.1 build compiled; the bat's new test gate then failed 3 of 6
suites — and this was the suites' FIRST execution ever (written without
Qt; the owner's old ritual never ran ctest). First-light score: 208/226
green. The 18 failures reduced to three causes:

- **The version-pin tripwire, one drop late:** the mood test pins the
  written format version; the v13 bump missed it. Now 13, commented as a
  tripwire: JsonStore's literal and this pin move together, every bump.
- **One cause behind all 15 UI failures:** QSettings persistence. Every
  failure was "wrote/planted a value, read back empty" — settings
  dialogs, the geometry trio, the gate-rearm and address-row plants, the
  chat-route plants. `test_ui` never set `organizationName` (only the
  application name); default `QSettings()` scopes by BOTH, and empty-org
  persists nothing on the owner's Qt 6.11/Windows. The real app and the
  passing `test_nlp` both set both. Fix: `initTestCase` sets both once.
  Diagnosed blind from one run's output by the shared signature — the
  gate's `--output-on-failure` earning its flag.
- **A timing bet lost:** `login_live`'s unreachable-server test gave the
  OS 3 s to report a dead port; the owner's box took 3.6 s. Ceiling now
  15 s — QTRY ceilings are free when green.

Also corrected: README's hand-carried test counts had drifted ~20 low;
now counted mechanically (~320 across six suites). Lesson pair for the
process file: **a version bump must grep the tests for pins on the old
number**, and **hand-carried counts drift — count, don't carry.**


---

## v28.3.3 — hotfix 3: two never-green assertions (suite goes fully green)

28.3.2's run: 13 of the 15 UI failures cleared by the organization-name
fix, confirming the one-cause diagnosis. The two survivors were both
assertions executing for the FIRST time, and both were test bugs:
the address-row visibility test asserted against a widget on a
non-current settings page (vacuous pass on the negative, impossible pass
on the positive — fixed by navigating to the Assistant page by nav
title); and the geometry test planted an 870-wide window on the
offscreen platform's 800x600 screen, so `restoreGeometry()` clamped it
and the exactness claim was testing Qt, not our wiring (fixed: 780x560,
fits any screen). The app code needed no changes for either. Lessons:
**a passing negative assertion can be vacuous — ask what would make it
fail**, and **offscreen has a real screen (800x600); geometry tests must
fit inside it.** Also shipped alongside: `tools/run-tests.bat`, after a
bare command prompt taught us test exes need Qt's bin on PATH
("Qt6Gui.dll not found").


---

## v28.3.4 — tooling patch: the stale-binary trap

A results file arrived showing the 28.3.3 failures UNCHANGED — including
`Expected (870)`, a literal the fixed source no longer contains, and
unshifted line numbers. Diagnosis: the suite ran yesterday's binaries;
`run-tests.bat` executed ctest without building, so "unzip fix → run
tests" re-tested old code. Fixed: the runner now does an incremental
`cmake --build` before ctest (compiler comes from the CMake cache by
absolute path, so no toolchain discovery needed). Reading a failure
message against the CURRENT source — does the quoted literal even exist
anymore? — is what caught it; that check is worth keeping.


---

## v28.3.5 — the last red test (pin invariants, not environment numbers)

28.3.4's rebuilt run: address-row test green (the page-navigation fix),
one survivor — geometry, now failing 780 vs 798. The convergence was the
clue: 870 -> 798 and 780 -> 798 means 798 is the environment's number,
not ours (870 clamped down by the offscreen screen; 780 clamped up by
the layout's computed minimum — no setMinimumSize exists, the rail +
pages + margins derive ~798). Save and restore were faithful the whole
time. The test now measures the first window's actual size and asserts
the restore reproduces it — the round-trip is ours to pin; the width is
not. QB V225. This should be the green run.


---

## v28.3.6 — geometry test, final form (assert the name, nothing else)

The measured `lived` came back 1166 — bigger than both plants — which
killed every clamp theory at once and pointed at the true mechanism:
under the no-fonts fallback metrics the window's layout demands ~1166
minimum, wider than offscreen's entire 800x600 screen. Width on this
platform is decided by three parties in sequence (layout minimum, screen
fit, and WHEN each gets to act relative to the first layout pass) — and
never by the save/restore wiring, which has been faithful in every run.
Three width assertions in three drafts all lost to environment owners;
the fix is not a fourth guess but a retreat to the test's own name:
restore acted (≠ untouched default) and construction did not overwrite
the blob. Meta-lesson for the QB (V225 now reads as a trilogy): when an
assertion keeps losing to the environment, stop refining the number —
ask what the test's NAME claims, and assert exactly that.


---

## v28.3.6 — GREEN. The arc closes.

All six suites pass on the owner's machine: the first fully verified
state in the project's history — apply-checked, clean-built, ~320 tests
green on real hardware, and the installer that follows is the first one
ever cut from a proven tree. The v28.3 arc, end to end: the feature
(subtasks + sizing, format v13), two process tools (the apply check and
the test gate in deploy-windows.bat, plus run-tests.bat), and a
first-light debugging campaign that went 18 red → 3 causes → 2 → 1 → 0,
every diagnosis made from run output alone, with zero changes needed to
the app's feature code after 28.3.1. All eight failure families are now
logged in TROUBLESHOOTING.md in the house format; the question bank
carries the arc as V209–V226. Next: v28.4 (the §J.2 multiplier + afford
off real estimates — small, pure, no format change), then v29 tool use.


---

## v28.4.0 — sizing intelligence (§J.2), and the session closes

The small, pure drop §N promised: `afford::personalMultiplier()` (median
of actual÷estimate over finished-tracked-estimated tasks; 1.0 under 3
samples; clamped [0.5, 3.0]; derived never stored) and the §H rewire —
outstanding = estimate × rate − tracked when an estimate exists, proxy
as fallback, Unknown shrunk to "no estimate AND no blocks". An unsized
parent borrows its pieces' estimate sum (the §J.3 opening move, minus
the AI). Report/sentence/nudge/briefing all carry the basis; sweeps
compute the rate once. Five new domain tests (141); format stays v13.

**Session verdict:** v28.3 subtasks re-landed and verified green on real
hardware for the first time in project history; the deploy pipeline
gained the apply check, the test gate, and run-tests.bat; the
troubleshooting log gained eight earned entries; QB runs to V229. The
owner's manual pass found the core solid with a pieces-polish list
forming (headline gap: no UI door to date a piece). **Next session
candidates:** the pieces polish list, then v29 tool use — whose intake
verb (§K) now has both the `addSubtask` door and real estimate handling
waiting for it.


---

## v28.4.1 — the fixture the domain refused

The rewire's first light: 142/143, all five new behaviors green except
the median test, which read 1.0 — "not enough samples". The fixture had
planted 05:00 blocks and `isFree` guards the planner's day window
(06:00–24:00), so every `addTaskEvent` returned "" and the unchecked ids
made `appendSegment` silently no-op. Two lessons, both old friends in
new clothes: invented fixtures must obey the domain's OWN gates (the
passing tests all used 09:00 blocks for a reason the new test didn't
ask about), and every door's return gets a QVERIFY — the pays-down test
had the precedent and the new test skipped it. Fixed test-only; the
multiplier had behaved correctly the whole time (no valid samples IS
1.0).


---

## v28.4.1 — VERIFIED GREEN. Session closed.

The owner confirms: all six suites green on 28.4.1, feature working.
Final docs audit same day: SETUP's hand-test path now points at
`run-tests.bat` (bare ctest dies on Windows — TROUBLESHOOTING has the
entry); the iteration plan gained the Sizing intelligence row; §H's
summary and the promoted affordability addendum carry the v28.4
estimate-first update; the subtasks addendum's piece-visibility bullet
is sharpened into the concrete chicken-and-egg the QA pass found (no UI
door to date a piece — the polish list's headline); the QA checklist
matches 28.4.1 and gained the J2 block; TROUBLESHOOTING gained the
refused-fixture entry; QB runs to V231. State at close: **v28.4.1,
format v13, ~325 tests green on real hardware, docs consistent.** Next
session: pieces polish (headline: dating a piece), then v29 tool use.

---

## v28.5.0 — the piece's own panel (the polish headline, closed)

The slice scoped exactly as planned, and the scoping survived contact:
**the domain needed zero changes.** A piece was already a full Task and
the detail dialog already edited every relevant field — the entire
chicken-and-egg ("no UI door to date a piece") came down to UI wiring.
The door is the row's TITLE (checkbox still just ticks — two targets,
two verbs, TickTick's split); a piece's panel opens with a breadcrumb
and no checklist; rows gained the "Aug 8 · 45 min" display chip.

The design's spine: **navigation is part of the answer.** The dialog
records navigationTarget() and performs nothing; the new runTaskDetail
free function (seed → exec → apply, LOOPING while the answer names a
next task) acts on it. Click-through ACCEPTS — the hop saves the
sitting; only Cancel discards. All four call sites shrank to one line
and dropped their defensive Task snapshots: re-reading by id each hop
beats a snapshot on both axes (can't dangle, can't go stale).

**Known debt, on purpose:** the panel is still modal, so the hop is
close-and-reopen — recorded in §L.4 and handed to the side-panel slice,
which will now touch one function instead of four sites.

**Drift found in passing:** README's pieces section still wore the
pre-re-land "designed, never landed" banner — directly contradicting
the status line three paragraphs up. The v27-reconciliation audit added
the section; nobody removed the warning when 28.3 actually landed.
Same lesson as ever, new coat: status prose is checked by grep after a
ship, banners apparently aren't. Fixed; "banner sweep" belongs in the
post-ship grep habit now.

Four new UI tests (V-named in the QB): the recorded-not-performed
contract, the hop-saves-the-sitting rule, the newborn-has-no-door count,
and the breadcrumb's target. 341 tests green across six suites — the first time the count is MEASURED (per-suite Totals summed), not carried forward as an estimate. Format stays v13.
Next: pieces reorder + quick-date conveniences (small), the promotion /
double-count decision (slice 2), or the side panel (slice 3).

**Close-out:** clean build in the session sandbox (Linux/Qt 6.4), all six
suites green — 143 domain, 21 taskmodel, 67 nlp, 80 ui, 19 auth,
11 login_live = **341**. Diagram rendered (`piece_detail_sequence.*`).
Per the v28.3 lesson: sandbox green is necessary, not sufficient — the
owner's `run-tests.bat` pass on real hardware is the ship gate, and the
deploy script's test gate enforces it.

---

## v28.6.0 — the docked panel (explicit save, the owner's call)

The §L.4 debt paid one session after it was recorded — and the seam
argument held exactly: the container swap touched runTaskDetail and
ZERO call sites, second redesign in a row behind the same three-argument
signature.

The refactor: TaskDetailForm extracted (fields + answers + dirty
tracking), TaskDetailDialog hollowed to a modal wrapper and kept as the
fallback, TaskDetailPanel docked as [nav][pages][panel]. The extraction's
product is a question relocated: "what does navigation MEAN" moved from
the form (which can't know) to the containers (which disagree —
record-and-accept vs. guarded swap). The whole pre-existing suite
passing unchanged through the wrapper is the refactor's proof.

**The owner rejected instant save** — deliberately, against the TickTick
reference: saving should be an act with feedback. The panel's grammar:
lit Save button = dirty (a COMPARISON, not a touched-flag — retype the
original and it goes quiet), "Saved ✓" flash after, Save/Discard/Stay
on every exit over dirty work, Save as the prompt default so Enter can
never destroy. v28.5's "the hop saves the sitting" grew a question mark;
the modal fallback keeps the unconditional rule (its Save/Cancel already
IS the choice — asking twice would be noise).

Two lifetime rules worth keeping from the swap-in-place: deleteLater
the outgoing form (the swap may run inside its own signal emission) AND
queue navigateRequested→openTask (let the click finish before the
ground moves). Either alone leaves a path open. V239 has the pair.

Three new panel tests through the injected prompt seam (the house
pattern — with the field-report caveat honored: the REAL QMessageBox
walks in the QA block, including Enter-defaults-to-Save). **344 tests
green across six suites, measured.** Format stays v13.

**Close-out:** clean build in the session sandbox (Linux/Qt 6.4), all
six suites green — 143+21+67+83+19+11 = 344. Diagram rendered. Sandbox
green is necessary, not sufficient: the owner's run-tests.bat pass on
real hardware is the ship gate — and this slice more than most needs
the MANUAL pass, because the slide, the prompt feel, and the panel
width are exactly what offscreen tests cannot judge.

Next: pieces reorder / quick-date polish (small), promotion & the
double-count guard (slice 2, the design decision), or v29 tool use.

---

## v28.6.1 — docked → overlay (the fastest feedback loop yet)

The owner ran 28.6.0 and rejected the SHAPE within hours: the docked
panel read as part of the Activities page and shrank the main screen.
Both complaints are one fact — a layout member competes for space — and
the fix is categorical, not cosmetic: out of the layout entirely.
Overlay child + scrim (rgba dim = the "lower contrast" asked for, AND
the click-away target) + position-based slide + 440 px (was "tight" at
360) with a 220 px keep-clear clamp.

The guard generalized cleanly: scrim click = ✕ = Esc, all through
Save/Discard/Stay — click-away close never means click-away discard
(clickingOutsideRunsTheSameGuard pins it, and the scrim swallows the
press either way: a click that closed the panel must not ALSO land on
what was underneath).

Two Qt traps filed in the QB: WA_StyledBackground (V243 — invisible
until widgets overlap, detonates exactly on the docked→overlay
migration) and un-laid-out children not following resizes (the host
event filter). The catch-up arc's daily-driving thesis collects another
data point: no test judges "feels like part of the wrong panel."

**Close-out:** clean build, six suites green, **345 measured**
(143+21+67+84+19+11). Format v13. Owner's manual pass is the ship gate
as always — and the dim level + prompt frequency questions in the QA
block are the real verdict on this one.

---

## v28.6.2 — the patchwork panel (a documented trap, re-hit)

The owner's screenshot: white header and Save row, palette-grey middle
— seam tracking the scroll area exactly. Theme.h has documented the
mechanism since v3: setWidget() flips the child's autoFillBackground
ON, and the form started painting palette grey inside the white panel.
One line (OFF after every setWidget) fixes it.

The honest part: this is the SECOND documented landmine this project
has re-hit — main.cpp's folder-rename warning was the first, and this
time the warning even lived in the file that defines the app's look.
The sharpened lesson, filed in V244 and TROUBLESHOOTING: a comment is a
tripwire only for patches that read it; a TEST is a tripwire that stops
the patch regardless. theFormNeverFillsItsOwnBackground now pins the
flag after open and after a save's rebuild.

Also on record: the owner LIKES the unsaved prompt — "prevents human
error from accidental changes" — so the guard's frequency question is
answered and the gentler-guard idea leaves the polish list.

**Close-out:** clean build, six suites, **346 measured**
(143+21+67+85+19+11). Format v13. This drop is one line of app code and
its safety net; the owner's rebuild is the visual verdict — the QA
line to check: the panel one uniform white, seam gone.

---

## v28.7.0 — pieces in the list (the owner A/B-ed us against TickTick)

The owner tested TickTick's subtask flow side by side with our checklist
door and reported theirs more intuitive: create from the list, structure
IN the list, subtask edits like a task. The scale argument sealed it —
a checklist in the parent's panel stops working past a handful of
pieces. Everything shipped as scoped in one session: right-click →
"Add a piece" (create-first-name-second, title selected), indented rows
that are REAL rows (tick/chip/✕/open-with-breadcrumb all free, because
a piece IS a task), §D amended for display while every counting query
stays parents-only.

Design notes worth their ink: interleaving sits AFTER the parent sort
so families never split (V245); the indent lives in geometryFor so
paint and hit-tests shift together (V246); create-first avoids a whole
shadow "pending piece" lifecycle (V247).

An honest catch by the test suite: the new page test HUNG ctest —
startPieceUnder in a panel-less window falls back to the modal dialog,
whose exec() blocks forever offscreen. The test now builds the shipped
arrangement (page + panel under one window) and the hang is documented
in-test as what it is: the modal fallback is a real path with a real
blocking cost.

**Close-out:** clean build, six suites, **348 measured**
(143+22+67+86+19+11). Format v13. Owner's manual pass gates the ship;
the QA block's last feel question (should walk-away auto-discard an
untouched "New piece"?) is the one open thread this feature keeps.

---

## v28.8.0 — the size ladder ("720 min" retired)

Owner request with a design question inside it: dropdown, 30-minute
steps, hours past the hour — "but how many entries?" The shipped
answer: the shape, not a number — 15/30/45m (15 kept against the ask,
because "Fits short gaps" is built on it), half-hours to 8h, whole
hours to 16h, and the CAP is doctrine: past two workdays, the app's
answer is pieces, not a bigger number.

The correctness core: off-ladder values (spinbox era, parsed captures)
insert at their sorted rung — opening the panel is never an edit,
pinned by isDirty()==false right after seeding an odd value (V249).
And the formatter audit found drift already underway: durationLabel
(slots) and spanLabel (seconds) were two shapes of one idea about to
gain a third — minutesLabel is now the core, durationLabel delegates,
the piece chip converts ("12h", not "720 min"), spanLabel keeps its
briefing style deliberately.

**Close-out:** clean build, six suites, **350 measured**
(143+22+67+88+19+11). Format v13. The QA feel question: is 16h the
right ceiling for how the owner actually estimates?

*Post-ship correction, same session: the README's headline test count
had silently drifted (a replace in the 28.6.x chain missed and left
345; deeper down, an untouched "~320" survived from months ago). Both
now read 350. The count joins the version string in the post-ship grep
sweep — prose numbers rot exactly like banners do.*

---

## Docs audit — the v28.5→v28.8 day, reconciled

One sitting shipped six drops; this pass audited every doc against the
tree (grep, not memory). The day in one paragraph, for the record:

**v28.5.0** opened the door — a piece's checklist title opens its own
full panel (breadcrumb back, "Aug 8 · 45m" chips), navigation recorded
as part of the answer, `runTaskDetail` consolidating four call sites.
**v28.6.0** built the docked panel with EXPLICIT SAVE (the owner's call
against TickTick's instant commit): lit-button dirty, "Saved ✓" flash,
Save/Discard/Stay. **v28.6.1**, hours later on owner feedback, took the
panel OUT of the layout — overlay + scrim + click-away, 440 px — and
**v28.6.2** fixed the patchwork paint (the Theme.h setWidget trap's
second detonation, now pinned by test). **v28.7.0** put pieces IN the
list, TickTick-style — right-click "Add a piece" (create-first, title
pre-selected), indented family rows, §D amended for display with
counting untouched. **v28.8.0** retired "720 min": the size ladder
(15m–16h, cap-as-doctrine), off-ladder values inserting at their sorted
rung, `minutesLabel` unifying the duration dialect. Owner verdicts
logged along the way: the unsaved prompt is LIKED; the overlay shape is
right. 341→350 tests, all measured; format v13 untouched all day.

**Audit findings (all fixed):**
- `design-doc.md`'s addenda index had NO row for the subtasks addendum
  (missing since v28.3!) nor for the new detail-panel addendum — both
  added, §-ranges and today's sections included.
- The assistant addendum's index row still said "PLANNED, not built" —
  frozen from before v26. Now states the truth: read-only arc shipped
  through v28.4, tool use the still-planned v29.
- `SETUP.md` sat frozen at v28.2.1 — including a verify step that would
  FAIL a newcomer ("must print 28.2.1"). Six releases of drift.
- README's status marker said v28.6 against a 28.8.0 Version.h, and
  the line was missing the 28.7/28.8 clauses entirely; the four
  panel-era clauses consolidated into one (the line had grown to 3.7k
  characters by accretion).

**The sweep list, updated.** Post-ship greps now include: version
strings (Version.h + .iss + QA), test counts (README ×2), banners,
the README status MARKER, SETUP's expected version, and — new rule
from today — every new addendum lands with its design-doc index row in
the same drop, because an unindexed design record is findable only by
people who already know it exists.

---

## v28.9.0 — promotion (arithmetic before hands)

The slice-2 decision, made and shipped: **the trigger is the date**.
The insight that settled the design came from reading the sweep before
proposing — affordability has NO piece filter, so a dated piece was
already its own line everywhere (verdict, nudge, needs-a-block) except
in its parent's arithmetic. Promotion doesn't ADD a concept; it makes
counting agree with a fact the code already acts on. One function
changed (the §J.2 estimate block in Affordability.h), nothing stored,
no migration.

Both paths, one trigger: sized parent subtracts dated pieces (floor 0
→ proxy basis, honestly flagged — pieces promoted past the guess mean
the decomposition outgrew it); unsized parent borrows only undated
pieces (v28.4 amended — and its test passed UNCHANGED, the additive-
amendment proof, same shape as the wrapper proof in 28.6).
Report.minutesPromoted is the ledger. The owner's exact FINALS case is
the headline test: 480 + 240 = 720, entered once, believed once.

Deliberately out: Upcoming stays one-row-per-parent (§M stands);
promoted-piece cards on the digest are a future display question, not
a counting one. §D + §M + §O now state the complete pieces policy in
three lines (V252).

**Close-out:** clean build, six suites, **353 measured**
(146+22+67+88+19+11). Format v13. The QA feel question: does the
parent visibly "shrinking" as pieces gain dates read as sensible?
With counting closed, v29 tool use now lands on numbers that don't
lie — which was the argument for this ordering.

---

## v28.9.1 — the ladder learns to scroll

One line, owner request: the 26-rung dropdown opened as a full-height
behemoth covering most of the panel. setMaxVisibleItems(6) caps the
popup at six rows with a scrollbar — six ≈ the sub-hour rungs plus a
couple of hours, the zone most picks live in, everything else one
flick away. Pinned in estimateDropdownSpeaksHours. One caveat filed in
the code comment: fully native popup styles can ignore the hint — the
QA line verifies on real Windows. 353 tests green; format v13.

*Docs re-audit on request: three drifts from the day's last two drops —
README's DEEP test count (the second spot, missed again: it's now
explicitly "README ×2" in the sweep), the design-doc subtasks row still
reading A–N with no §O/promotion, and v28.9.1 missing its CHANGES file,
log entry, and plan note. All fixed. The pattern to name: the smaller
the drop, the more its docs get skipped — patch releases now run the
same full checklist as features.*

---

## v28.10 — the seams, reachable (Slice 0 of the road to v29)

**Decision at open:** owner green-lit the v29 milestone plan with the
default sequencing — Slice 0 (the field-report cleanup) before the write
boundary, because v29's confirm loop will need hand-forcing even more
than v28's toasts did.

**Shipped, in one session:** the debug panel (`DebugPanel`, Ctrl+Shift+D;
`design-addendum-debug.md` A–H; `diagrams/debug_seams.*`;
`docs/TESTING.md` — the force-recipe file the field report ordered);
`CheckInService::forceOffer` + `clearTodaysAsk` and
`AffordabilityService::forgetManners` (the services own their keys — the
panel presses); the briefing's three field fixes (DAY STATUS, PLAN FOR
TOMORROW, day-totals disambiguation → chat addendum **§C.1** and its
fifth rule: *computed facts are stated, never implied*); assistant
markdown (`Qt::MarkdownText`, assistant-and-not-localOnly only, markdown
over RichText as the narrower surface); `TICKTIMER_AI_DOWN=*` and
`forcedDown` reaching NudgeClient (silent fallback) + quick-add (named
cause) — the mechanism behind "never heard the v28.0 voice".

**Teaching threads this session:** seams-must-be-reachable as the other
half of testability; glass-vs-brains for debug surfaces; rehearsal
semantics (skip the gate, not the script, spend nothing); why a wildcard
beats an enumerated copy of a catalog; §A applied to briefing content
(the fifth rule); per-wire failure manners as a feature, not an
inconsistency.

**Close-out:** clean build on Linux/Qt 6.4 (a first for the tree — the
sandbox baseline built and ran everything before any edit), six suites,
**359 measured** (149+22+68+90+19+11; was 353). Format v13. Version.h →
28.10.0 (checklist item one, done first this time). Docs swept: CHANGES
file, README ×2 **+ the format line** (drift caught: "v11" over a v13
tree), design-doc addendum row, iteration-plan row, reading guide,
diagrams index, bank **V253–V264** (280 total), this file, the log.
**Next session: v29 Slice 1 — the write boundary, model-less** (role →
verbs allow-list, per-turn handles, the confirm-loop card, the
needs-details queue, the data.json copy-aside; Dialect promotion
deliberately deferred to Slice 2 with the reason recorded, per §B.3).

**Postscript (same day):** the drop failed the owner's apply check —
Version.h 28.10.0 vs ticktimer.iss 28.9.1. Mentor's miss, and an
instructive one: earlier in the session I read the .iss's *history
comment* (the GetStringFileInfo mechanism) as its present, concluded
"mechanism, not retyping", and skipped the bump; the live line below
says "bump BOTH, every release", and the mechanism is commented out on
purpose (untestable artifact — no Windows/Inno on the audit machine).
Fixed the one token, replicated the check's exact findstr/token-3 logic
green, swept the tree for other live 28.9.1 strings (only code comments
citing history — kept), confirmed server/version.json is publish-time
per GITHUB.md step 6 (untouched, deliberately), re-cut the zip. Lesson
already in the log's postscript; bank got **V265**. The checklist line
for future drops: the version bump is TWO tokens in TWO files, and the
check exists because everyone — including the mentor — eventually
forgets the second one.

**Postscript 2 (the apply of the fix failed too):** the corrected zip
was re-cut under the SAME filename as the first cut, the owner's
machine ended up holding two identical-looking drops, and the stale one
got applied — the check fired a second time on the same mismatch. New
delivery rule, effective immediately: **a re-cut drop gets a `-rN`
suffix** (`TickTimer-v28.10.0-src-r2.zip`), because "eyeball the zip's
filename against the version it prints" — the check's own advice —
only works if two different drops cannot share a name. Same lesson as
the version files themselves: two things that must be distinguished
need a mechanical distinguisher, not care.

---

## v29.0 — the write boundary, Slice 1 (the machine, no model)

**Shipped, two working turns:** `AssistantVerbs.h/.cpp` (per-role closed
verbs — Intake alone writes, additively; HandleMap with dedup + strict
fail-safe resolution; validate with role-first check order; apply with
tap-time re-validation through existing doors only); briefing task lines
carry [T1] handles (`handlesOut` grown LAST and defaulted after catching
the param-order mistake pre-compile); the NEEDS DETAILS section (derived,
estimate-keyed, silent-when-empty); `ProposalCard` (glass, summary from
fields); `ChatPage::presentProposal` + preApplyHook + localOnly receipts;
rolling `data.json.pre-apply` wired at the composition root; the debug
panel's **Inject sample proposal** (the panel plays the model);
`diagrams/write_boundary.*`; addendum A–I; TESTING.md recipe (including
the stale-card scene as a hands-on step); assistant addendum §B/§K status
notes and §B.3's recorded-why.

**Two first-red lessons kept honest:** the urgency-partition fixture had
to size "Far away" (the queue is about missing facts, not dates — an
unsized far task RIGHTLY queues), and the chunkable assertion now pins
preservation, not a guessed default.

**Teaching threads:** machine-before-model as the write-side fallback
doctrine; allow-list-as-diff-review; trust vs routing axes; fail-safe vs
fuzzy resolution; additive semantics needing an absence state; the
description-is-the-request rule; record vs live-UI lifetimes;
record-the-deferral as the promotion criterion's other half.

**Close-out:** clean build, six suites, **366 measured**
(154+22+68+92+19+11; was 359). Format v13. Version 29.0.0 in BOTH files,
apply-check logic replicated green before the cut; zip named
`TickTimer-v29.0.0-src.zip` (the -rN rule stands if a re-cut is ever
needed). Bank **V266–V277** (293 total). Drift caught: README's
"twenty-four" addenda over a thirty-four-file folder.
**Next session: v29 Slice 2 — the model joins intake** (one open
question, extraction into a Proposal, guess-and-confirm from history,
triage + ask-once via the waiting dismissedUntil; §B.3's Dialect
criterion gets its real test there).

**Postscript — v29.0.1 (live debugging, second machine):** the browser
screenshot was the tell — `{"error":"not_found"}` at `/` proved network
health and server identity in one glance, moving the search from
firewalls to the request path in a single step. Root cause: trailing
slash in the Server field → `//register` (client concatenation) + exact
route match (kept: strict beats fuzzy at a boundary) + the InvalidInput
catch-all's misleading prose. Fix: `normalizeServerUrl` at every entry
(SyncClient included — same base, same landmine, found before it fired)
+ `UnknownServerReply` naming the unforeseen. Owner unblocked mid-session
with the one-character workaround before the patch was cut. 369 tests
(login_live +3, incl. the exact failing call). TROUBLESHOOTING entry
keyed on the exact message. Versions ×2 → 29.0.1. Bank V278.

**Postscript 3 — v29.0.2 (same evening, share direction):** asymmetric
share failure (he→her ok, she→him "no account"). Chain: raw
serverUrl saved by LoginDialog + ShareClient (the consumer v29.0.1
missed) + a classifier that collapsed route-404 into the typo message —
its own comment defended the collapse. Fixed at the birth
(LoginDialog::serverUrl normalizes; consumers keep defensive copies,
demoted), split the 404s (UnexpectedReply names our bug). Mentor's
postmortem, on the record: the v29.0.1 consumer-side shape ARMED this
bug by letting login pass while saving poison — fix-at-birth was the
lesson and same-day evidence proved it. Test-craft note: loginForToken
registers (its comment says so); first draft double-registered and got
an empty token — read the helper before calling the helper. Also
de-collided v29.0.1's test usernames from names later tests mint. 371
tests (93 ui + 15 login_live). Versions ×2 → 29.0.2. Bank V279.

---

## v29.1 — the interview (Slice 2: the model joins intake)

**Shipped, three working turns:** `intake::` domain brain (historyGuess:
median of tracked actuals, 2-sample floor, basis attached; §K.6 triage
with ask-once via the long-waiting dismissedUntil; the C++ question;
the crisp parser whose refusals are the design); `intake::llm`
extraction (values-only prompt after the purity correction — contract:
estimate_minutes/due_date, null-when-unsure, guess-agreement, do-not-
restate, realistic-total; parseReply with envelope + fences + string-
clothed numbers); `IntakeClient` (quick-add's twin, forcedDown from
birth); ChatPage's interview mode (three tiers, chained continuation,
guess-through-the-card, Skip via the owner's door, discard-is-not-skip
via askedThisSession); §K.1's check-in entry + the panel's forcing
button; `diagrams/intake_flow.*`; addendum A–H; TESTING recipe.

**The build pushed back twice and both verdicts are kept (addendum
§H):** linker → one header, two TUs by dependency group; test_nlp's
charter → prompts over values. **And one honest wobble on the record:**
a duplicate extraction layer written from plan notes without re-reading
the tree — reconciled same turn; rule minted: the header is the
abstract.

**Teaching threads:** model-as-proposer; extraction vs tool-calling
(and why two non-firings make §B.3 healthier, not dead); crisp-first as
cost + sovereignty; consent-riding entries (§K.1); the three lifetimes;
suite structure as the enforcement layer.

**Close-out:** six suites, **379 measured** (158+22+70+95+19+15; was
371). Format v13. Versions ×2 → 29.1.0, checked. Bank **V280–V290**
(306 total). **Next session: owner's field run of the interview** —
findings become the punch list, per house tradition; then candidate
slices: the memory file (§L, K.4's other half) or rescheduling (the
second verb, where overwrite semantics and possibly the Dialect
criterion get real).

---

## v29.2 — the reschedule verb (Slice 3: the first verb that changes the calendar)

**Shipped, in the addendum's own build order — the inverse first.** §H had
found that a move's inverse did not exist: catch-up's "Undo" and "Bring back"
invert a *decision* (`resolveBlock(id, Unset)`), which on a moved block clears
`outcome`/`movedToId` and leaves the replacement sitting on the calendar — the
work appearing twice, "a state nobody proposed". So `AppData::undoReschedule`
was built before any verb could reach it, removing the replacement and clearing
the original in one guarded step, one `changed()` for two mutations. Then:
block handles as a second strict namespace (`B1`, never a UUID); `Verb::MoveBlock`
with `verbsFor(Role::Chat) → { MoveBlock }`; the briefing's `can move to:` lines
(§C); `scrub::`; the chat-turn wiring; and the read-only claim retired.

**The two fences are the whole safety story.** Only blocks the domain already
judges missed may move — *a block you might still do is a plan you are living
inside, and an assistant that may move it is a calendar editor*. And the
placement must be one `reschedule::propose()` offers **right now**. The model
selects; it never invents a time. Proposals carry the concrete placement rather
than an index into the offered list, because an index is a handle with no
fail-safe property — a list recomputed at the tap would silently rename option
2, while a placement fails closed. `apply()` re-runs the whole search at the
tap, and `rescheduleBlock` declines rather than forces, so even the remaining
race ends in a refusal.

**Three build verdicts worth keeping.** `verbs::World` (clock, missed rule,
search policy) is **passed, not defaulted** — a default-constructed World would
give MoveBlock a silently wrong verdict, so every existing caller now says
explicitly that it consults none; the per-block deadline is derived inside
validation, because policy comes from the caller and facts come from the
domain. `scrub::` got its **own file** rather than a home in `AssistantVerbs.h`:
that header's value is that a diff of it IS the complete security review, and a
reply parser grants no capability — it can only produce a `Proposal` that
`validate()` then judges exactly as it judges a C++-composed one. And intake's
extraction could not be reused, by design: intake is a *mode* where prose would
be a malfunction, chat is a *conversation* where the proposal rides inside the
reply.

**Two test tripwires fired and were RE-PINNED, not relaxed.**
`chatPromptStatesTheReadOnlyContract` and
`everyPersonaKeepsTheContractAndTheFloors` both asserted "cannot change
anything". They now pin the *shape* of the permission — one proposable change,
everything else refused, nothing without the tap, no persona able to widen any
of it. A test that stopped naming the boundary would stop guarding it.

**§B.3's third recorded non-firing.** Slice 1: no cross-call state because C++
composes the proposals. Slice 2: because extraction is one exchange by
construction. Slice 3: **because the conversation is `ChatSession`'s
transcript's job, not the dialect's.** §F was decided (`Role::Chat`) and the
criterion still did not fire — recorded honestly rather than triumphantly, and
still armed. What *would* fire it is native `tool_use` blocks, declined because
a local Ollama seat has no tool support and this verb must not become a
premium-seat feature.

**The version incident — the expensive lesson of the slice.** The verb shipped
in seven commits and the version never moved, so the app called itself 29.1.0
through an entire manual QA pass. That is not bookkeeping: the QA run opened a
month-old *installed* binary (`%LOCALAPPDATA%\Programs\TickTimer` — what a Start
Menu shortcut opens; neither `deploy-windows.bat` nor Qt Creator touches it),
saw a briefing with no block handles and no "can move to" line, and it read
exactly like a broken feature. Both builds reported the same ProductVersion, so
*Help → About* could not tell them apart, and diagnosing it meant diffing UTF-16
strings out of the two exes. Fixed twice over: the bump itself, then
`installerVersionMatchesTheHeader()` in the **domain** suite — not at CMake
configure time, because the failure mode is an ordinary incremental build where
nobody reconfigures. `deploy-windows.bat`'s comparison catches a *half* bump but
is silent about *no* bump, since two files agreeing on a stale number is exactly
what it asserts. Cost of writing that test, now in TROUBLESHOOTING: **moc's
simplified preprocessor mis-lexes raw string literals**, aborts before writing
the `.moc`, and the compiler then blames the missing include four thousand lines
away — and the aborted run still refreshes AUTOMOC's timestamp, so every later
build skips the work and the error appears unfixable.

**The field run: all 22 steps passed on a live Groq route**, including the two
that matter — the stale card and the invented time. Three fixes it earned, all
in the checklist rather than the code: the installed-copy trap above (the check
offered is step 7's handles, not About); "seeing the block on both days is the
design, not a duplicate" stated up front, because two rows read cold look like a
bug and the first person through reported it as one; and the header counts now
name their convention and say to re-measure.

**Close-out:** six suites, **402 measured** (181 + 22 + 70 + 95 + 19 + 15; was
379 — 23 new, 22 for the verb plus the version-seam pin). Format v13, unchanged
— this slice added no persisted fields. Versions ×2 → 29.2.0, and now pinned by
a test. Question bank unchanged at V290.

**Record close-out (following session).** The addendum shipped still marked
DRAFT, still saying "nothing built yet", and carrying **two `## G.` sections** —
the revised one and the provisional one it was written to replace. De-DRAFTed to
the house form (the title carries the marker; shipped addenda have no status
paragraph), the survivor absorbed the one fact only the provisional copy held
(§I re-armed the criterion and named multi-step rescheduling as its case), §H's
tense corrected with the built door recorded, and the file finally **indexed in
`design-doc.md` §3** — it had been missing. The checklist's derived count was
also wrong (18 claimed, 23 measured); it had drifted from birth, where it read
"379 green, 24 of them new", which was self-contradictory.

**Next session: candidate slices** — the memory file (§L, K.4's other half), or
§H.2's `movedFromId` back-link, the additive format-v14 bump that would give
split pieces an expressible inverse and let `Kind::Split` inside the fence. The
latter is a domain change and gets its own addendum, as this one records.

---

## v29.3 — the split's inverse (the domain iteration §H.2 owed)

**The repo was arguing with itself, and settling it was the slice.** v29.2's
§H.2 and `AppData.h`'s own SCOPE paragraph both named a `movedFromId`
back-link as the fix for a split having no inverse. `Event.h` had already
**rejected** that exact field when the forward link was born — *"two pointers
can disagree… derive the reverse, store the forward."* Both could not stand.

**The answer was to name what was actually broken.** The reverse question
("was this block rescheduled from somewhere?") was never the problem — a scan
answers it and cannot drift. The problem was that `movedToId` held **one** id
where a split produces **many**, so pieces 2..n were linked at neither end.
That is a **cardinality** defect, fixed by widening the field that is too
narrow, not by adding a second field pointing the other way. `movedToIds`
keeps exactly one record owning the move, so there is still nothing to
disagree with; the back-link would have made n+1 records that must agree, and
for a plain move the pair would be pure redundancy — the shape Event.h
refused. **Rejected a second time, with better evidence than the first.**

**Format v14, additive both ways.** Writes `movedToIds` and keeps `movedToId`
as a compatibility mirror of the first element; reads the list when present,
else wraps the single key. The mirror is safe in `JsonStore` and would not be
in memory — one door writes both in the same instant and the loader always
prefers the list, so no reader ever chooses between two live opinions. Two
costs recorded rather than discovered later: a v13 build that re-saves a v14
file collapses a split back to its first piece, and splits already on disk
cannot be retro-linked because nothing ever named their siblings.

**`undoReschedule` became all-or-nothing on the way back**, matching the entry
contract: every replacement judged before any is touched, one piece holding
tracked segments refuses the whole move, a hand-deleted piece is a repair, all
under one `Batch`. The verb's §I fence was **not** lifted — this removes the
reason §I cited, and opening it is a separate act.

**Close-out:** six suites, **408 measured** (181+22+70+95+19+15; was 402).
Versions ×2 → 29.3.0. The format-version tripwire fired on the first run and
was bumped 13 → 14 in the same drop — the behaviour its own comment asks for,
after v28.3.0 missed it once.

---

## v30.0 — the memory file (§L, read-first)

**§N's v30, unblocked once v29 completed.** The owner writes short, lasting
things about themselves into `memory-<username>.md`; the assistant is told
that text every turn.

**The half deliberately not built.** §L.4 says memory writes ride the confirm
loop, and they still will — but not yet. **`AssistantVerbs.h` is untouched:**
no `Verb`, no `Role` change, no signature change, so a diff of the
security-sensitive header is empty. The reason is sharper than "machine before
model": **memory would be the first thing a model writes that a model later
reads as prompt.** Every earlier verb writes domain data that reaches the
model only after `brief::` turns it into a computed fact; a memory entry goes
back into the system prompt verbatim, forever. Living with the read half for
an iteration answers the two questions that should govern the write verb —
whether memory earns its per-turn cost, and what people actually put in it.

**Sidecar decided (§L.5).** `MemoryStore::pathForUser()` mirrors
`JsonStore::filePathForUser()` exactly, because if the two ever disagreed
about what a username maps to, logging in would pair one person's planner with
another person's memory. Accepted cost: memory does not follow you to another
device. **And the claim that must not be overstated —** not syncing is *not*
never leaving the machine: memory rides in the prompt to the AI provider on
every turn. `docs/AI.md` §6 now states both halves, including "if that matters
for something you were about to write down, use Ollama — or don't write it
down."

**Two of §L's rules became physical.** Entries are replaced, not appended — an
entry is a line, the editor edits lines, and there is no add button. And
trimming is a **prompt** concern, never a data concern: the file keeps
everything, the band drops whole entries and never truncates, because half a
sentence about a person is a fact with its qualifier removed.

**Never destroy the owner's text.** Unrecognised content is preserved verbatim
under a sink heading, never sent, and the sink is also what makes
`parse(render(f)) == f` stable — without it a preserved `- ` line would be
re-adopted as an entry of the last section and the file would mutate on every
save. The settings page re-reads at save time so a hand-edit made *while the
dialog was open* survives an OK.

**Three things the work turned up.** Contract rule 2 read "never from memory",
meaning the model's own recollection — with a section literally called memory
in the same prompt it reads as "ignore the memory section", so it was reworded
and the old phrasing is pinned gone. The **"What can it see?" viewer would
have started lying**: its caption said "nothing else is sent", so it now shows
the memory band under its own heading — rule recorded, *whatever is sent is
what is shown*, and any future prompt addition inherits it. And one test
earned its keep during the writing: the band-order test first matched the bare
phrase and measured the **contract's** mention of the section rather than the
band.

**Close-out:** six suites, **422 measured** (194+22+75+97+19+15; was 408).
`data.json` stays at v14 — this slice added no persisted planner fields.
Versions ×2 → 30.0.0.

**Next: the field run.** v29.3 and v30.0 have both had zero real-data
exercise, and per house tradition the findings are the punch list. After that,
the candidates are §L.4's write verb (v30.1) or opening §I's fence for Split,
which v29.3 unblocked.

---

## v30.1 — the undo verb (closing a promise the code never kept)

**Found by asking what was next.** §N's table was exhausted at v30, so "what's
on the roadmap" turned into an audit of what the shipped record actually
claims — and §B.1's *"no undo button, because every verb the assistant can
call has an inverse **it can also call**"* turned out to be false, and to have
been false since v29.2.

`AppData::undoReschedule` was built in v29.2 *specifically* as `MoveBlock`'s
inverse, generalized in v29.3 to cover splits, and **had no caller anywhere in
the app**. There is no Undo verb; catch-up's "Undo" and "Bring back" reverse a
*decision*, never a move; `missed::` excludes `Moved` from both lists on
purpose. So a move the assistant made could not be reversed by the assistant,
the drawer, or the owner. §M withholds an undo button only *"as long as §B.1's
verb discipline holds"* — the condition had quietly stopped holding.

**The obvious design didn't work, and wasn't forced.** A handle-targeted
`UndoMove` is not expressible: block handles are registered only in the
briefing's UNRESOLVED BLOCKS section, and `DayBriefing.cpp` says why — *"the
namespace IS the set of legal targets, rather than a superset the validator has
to whittle down."* A moved block is no longer missed, so it has no handle.
Giving it one means listing moved blocks in the briefing, which turns the
namespace into a superset for `MoveBlock` and leaves `validate()` as the only
thing keeping moved blocks out of it. That property was bought deliberately in
v29.2 and is not worth spending to make a sentence sayable.

**So the verb carries nothing.** `{"undo_move": {}}` — no handle, no date, no
times — and C++ decides which move. The target rides in `verbs::World`, which
the CALLER builds fresh per call, and never in `Proposal`, which `scrub::`
builds from the model's own reply: an id field there would sit one careless
edit away from being model-aimable. Enforced twice rather than asserted once —
`validateUndo()` never reads the Proposal, and `scrub::` returns before reading
any field on the undo shape. Both pinned: a proposal naming a *different* block
is applied, and the World's block is the one restored.

**Scope is the last move this conversation applied**, recorded at the tap (a
proposal never applied is not a move that happened), cleared on use and on a
new conversation, never persisted. It lives in `ChatPage` because it is a fact
about a *conversation* — the domain has no opinion about who moved a block or
how recently. Not covered, and said out loud so it reads as a decision: a move
the OWNER made in the drawer. §B.1 promised undoing what the assistant did.

**§B.1 amended, not patched — and the honest half of this slice.** The promise
was false for `SetTaskDetails` too: it is additive-only (§K.5), so it cannot
clear an estimate it set. Making §B.1 literally true would need a clearing
verb, which re-opens the rule that exists precisely so the assistant can never
overwrite or destroy a value — a far larger security decision than the gap
being closed. So that half is **withdrawn rather than built**, the original
sentence stays visible with a dated amendment under it, and the guarantee is
restated as what the code actually keeps: verbs that REARRANGE get an inverse;
verbs that only FILL AN EMPTY FIELD do not need one.

**The tripwire did its job again.** `verbsAreScopedPerRole` failed on the first
build, because it names Chat's list explicitly rather than asserting "not
empty". Re-pinned to `{ MoveBlock, UndoMove }` in order — a third verb
appearing on that line should cost somebody an addendum.

**Close-out:** six suites, **435 measured** (205+22+76+98+19+15; was 422).
Format v14 unchanged — no persisted fields. Versions ×2 → 30.1.0.

**Still owed: the field run.** v29.3, v30.0 and now v30.1 have had zero
real-data exercise between them. `docs/QA_CHECKLIST_v30.0.md` covers the first
two; the undo needs a step of its own (move a block, tap Apply, say "undo
that", and confirm asking twice refuses politely rather than reversing
something else).

---

## v30.2 — offline start and remembered devices (cross-platform, Phase 1)

**The session that started as "put it on our phones" and found a hard gate.**
The owner wants TickTimer on his phone, his girlfriend's, and a few friends'
— sideloaded or web, never a store. Scoping that produced a five-phase plan
(offline start → the server hosted responsibly on a VPS → Android
distribution → the WASM build → push) and four findings, of which one had to
be fixed before any of the rest mattered.

**`main.cpp` made login a hard gate.** No reachable server meant no app — not
even to read your own local `data.json`. Invisible on a desktop next to the
server; fatal on the device most often away from it.

**Two problems, deliberately separated.** Offline start needs only a NAME
(enough to open `data-<user>.json` — no credential, no server change).
Staying signed in needs a CREDENTIAL. Doing only the first would have shipped
an app that opens offline and still demands a password on every online launch,
which on a phone is most of the misery unfixed. The owner chose both.

**The offline door gives away nothing, and that is the argument.**
`data-<user>.json` was always plain JSON in the account's own folder: login
proved identity TO THE SERVER and was never a lock on the file. So opening on
a remembered name hands over nothing that was not already lying there — it
stops the app pretending otherwise. Fenced anyway: offered only when the
server could not be REACHED (a refusal is not an invitation to work offline),
and only for an account with local data. The honest gap, stated rather than
hidden: a device that has never synced has nothing local to open, so the first
login must happen online.

**Device tokens are a second, different credential — not a persisted session
token.** `AuthServer`'s "tokens are session state, not records… persisting
tokens would be persisting open doors" is still right about the session tokens
it was written about, and those are untouched. What stopped being true is the
sentence it leaned on — *"logging in again mints fresh ones, which the app
already does on every launch"* — which described a desktop on its own LAN, not
a phone. So: the ordinary access/refresh split. The persisted door opens
exactly one thing, a fresh session for one account, and closes on request
without touching any other device.

Stored **SHA-256-hashed, not PBKDF2**, and the difference is the lesson: you
stretch what a HUMAN chose, because humans choose guessable things. Nobody
guesses 128 bits of CSPRNG output, and 200,000 iterations per resume would buy
no security while handing anyone a cheap way to make the server work.

**Coming back online is silent.** An offline session retries its remembered
device once a minute and calls `enableSync` mid-session on the first
acceptance — the phone that spent the morning on mobile data catches up when
it gets home. A refusal stops the timer and says so once; anything else stays
quiet, because an app that nags about every failed poll is worse than one that
waits. `enableSync` gained a guard: one window, one sync stack, or two
services would race to push the same planner.

**Remember-me defaults ON.** The phone this exists for has one owner; a shared
desktop is the case where it should be unticked, and is exactly the case where
somebody is standing there to untick it.

**Also corrected:** `docs/ANDROID.md` had claimed "there is **no sync**
between them" for fourteen versions after sync shipped. The correction says so
in the paragraph, rather than quietly editing the past.

**Close-out:** six suites, **445 measured** (205+22+76+98+25+19; was 435).
Format v14 unchanged. Versions ×2 → 30.2.0.

**Recorded for the phases ahead.** Push on iOS IS possible — Safari has done
Web Push for Home-Screen web apps since 16.4. The obstacle is ours: the server
keeps the planner an opaque blob (`design-addendum-sync` §D) and cannot
compute what to say. The agreed shape is a narrow uploaded schedule of
`{when, title}` the server fires blindly, which keeps the fence and happens to
fix Android's background reminders too — `BlockAlarmService` is an in-process
timer and equally dead there when the app is not running.

**Still owed, and named so it is a decision rather than a drift:** the server
is still the one `docs/SERVER.md` says not to expose (Phase 2 — bind
localhost, Caddy for TLS, rate-limit `/login`, close registration); there is
no revoke UI though `DeviceStore` can already list and forget; and device
tokens have no expiry, because guessing a duration before anyone has lived
with one would be inventing a number to look thorough. Three slices — v29.3,
v30.0, v30.1 — and now a fourth still have zero real-data exercise.

---

## v30.2.1 — the hardening half of Phase 2 (earning past our own warning)

**`docs/SERVER.md` forbade the plan.** *"A development server with no
hardening. Do not expose it to the public internet."* The VPS is the plan, so
that sentence had to stop being true rather than be quietly ignored. Three
changes, in order of how much they matter.

**The bind address now defaults to `127.0.0.1`.** It was `QHostAddress::Any` —
right for a laptop serving a phone on the same Wi-Fi, exactly wrong the day the
same binary runs on a box with a public address. One forgotten flag was the
whole distance between those two sentences. Now the risky choice is the one
somebody types (`--bind any`), and a reverse proxy is the only thing that
reaches the parser.

The cost is real and paid on purpose: the owner's current LAN setup breaks
until he passes the flag. That failure is **loud** — the phone cannot connect,
and the startup banner literally prints "NOT reachable — pass `--bind any`" —
and one flag from fixed. The old default's failure is silent and not.

**Registration can be invite-gated** (`--invite CODE`), checked BEFORE the
account store is touched so a wrong code cannot reveal whether a username was
free. Still open by DEFAULT, because a closed default makes the first account
impossible to create; instead the banner warns on the **combination** — every
interface *and* open registration — since each alone is perfectly fine and only
together are they an open door on whatever network the box is on.

**A login brake:** five failures from one address in five minutes earns 429,
including on a correct password. Only failures count, and a success forgives
them, so fumbling your own typing never locks you out.

**Deviation from the approved plan, with its reason.** The plan said rate-limit
at the proxy. **Stock Caddy has no rate-limit directive** — it needs a plugin
and a custom build via xcaddy. Making the safe deployment depend on compiling
your own web server is how the safe deployment does not happen, so it is twenty
lines in the server instead, working behind any proxy or none. `X-Forwarded-For`
is honoured only from a **loopback** peer: trusting it from an arbitrary peer
would let anyone mint a fresh identity per attempt and defeat the brake
entirely.

**CORS preflight** answered before anything looks at the path. Needed by the
WebAssembly build even served same-origin, because a browser preflights on the
HEADERS — `Content-Type: application/json`, `Authorization` — not only on the
address. Without it a web client fails before its real request is sent, and the
failure looks like the server being down. `sendJson` also learned that 204
means no content, decided at the payload rather than at the write so the
Content-Length and the bytes cannot disagree.

**A gap created and closed in the same slice.** Gating the server without
giving the client a key would have been half a feature, so `registerUser` takes
an invite code and the dialog grows a field for it in register mode. Two new
outcomes: `InviteRequired`, and `TooManyAttempts` — deliberately NOT mapped to
"wrong username or password", because the brake may well have caught a correct
one and that advice would have someone retyping something that was right all
along. The first draft *did* map it to BadCredentials, directly contradicting
the comment sitting above it; caught on re-reading.

**Close-out:** six suites, **448 measured** (205+22+76+98+25+22; was 445).
Versions ×2 → 30.2.1. Deployment templates ship as files —
`deploy/Caddyfile.example`, `deploy/ticktimer.service.example` — rather than as
prose to retype, and `SERVER.md` §4 now describes the hardened deployment
instead of forbidding it.

**One cost recorded rather than hidden:** `login_live` went 9s → 41s (the whole
run, 46s). Measured rather than guessed — the three new tests cost ~6.6s run
individually, so the remainder is Windows slowing repeated socket reuse inside
one process. A harness artifact, not a product one: the app makes one login per
launch, not twenty. The other five suites still finish in about five seconds.
Each new test also runs against its OWN server process, because the throttle
counter is per client address and every test here arrives from 127.0.0.1.

**Next, and it is not code:** the VPS itself — a box, a domain, Caddy in front,
`systemctl enable --now ticktimer`. After that, Phase 3 (Android distribution,
and pinning `QT_ANDROID_VERSION_CODE` against `Version.h` — a third seam still
reading 14 while the app says 30), then the WASM build, then push. And the
field run that four slices are still owed.

---

## v30.3 — Android distribution (cross-platform, Phase 3)

**No addendum for this one, deliberately.** Nothing here touches the domain:
it is a build recipe, a `.gitignore` rule, a Caddy block and three documents.
The house rule asks for an addendum when a feature enters the domain, and
writing one anyway to look thorough is the habit that makes addenda stop
meaning anything.

**The third version seam is gone rather than pinned.** `CMakeLists.txt` had
`QT_ANDROID_VERSION_CODE 14` and `VERSION_NAME "14.0"` hand-typed, drifted
sixteen major versions behind an app calling itself 30.2.1, and covered by no
test at all.

The obvious fix was a test, matching `installerVersionMatchesTheHeader()`. The
better fix was to notice **why** that test exists: Inno Setup genuinely cannot
read a C header, so its seam has to be hand-synced and therefore has to be
pinned. **CMake can read the header.** So the Android values are now derived
— `file(READ)` plus a regex over `include/Version.h` — and there is nothing
left to keep in sync, which is strictly better than something that can drift
under a watchful test.

`versionCode` is `major*10000 + minor*100 + patch`, which stays monotonic for
any minor/patch under 100 and still reads back as the real version: 30.2.1 →
300201, and this slice's own bump moved it to 300300 with nothing typed.

**Two details worth keeping.** The parse lives OUTSIDE `if(ANDROID)`, so a
regex that stopped matching fails on the very next desktop configure instead
of waiting for whoever next builds for a phone — and it fails with
`FATAL_ERROR`, because a silent miss would stamp an APK 0.0.0, which Android
installs happily over a real one and then refuses to upgrade.

**Keystores can no longer be committed.** `.gitignore` refuses `*.keystore`,
`*.jks` and `android-release-key*`. The signing key is the one secret with no
cheap rotation: Android identifies "the same app" by package name + key, so
losing it means every phone must uninstall — taking its local planner with it
— and committing it means anyone with the repo can sign something that
upgrades the app in place.

**Distribution is a URL, and Caddy serves it — not us.** `deploy/Caddyfile`
gains a `handle_path /download/*` block over `/var/www/ticktimer`. The
temptation was to add a file route to `ticktimer-server`; the reason not to is
that serving files means parsing paths, and a hand-rolled parser that
mishandles `../` hands out the whole disk. Caddy solved static files years
ago. The right move was to not write that code — which is the same doctrine
Phase 2 set when it put the proxy in front of the parser.

Handing the app to someone is now: open the URL on the phone, tap the file,
allow "install unknown apps" once. Plus the invite code, if the server was
started with one.

**`ANDROID.md` gained the release path it never had** — create the keystore
once (`keytool`, 10000 days), point Qt Creator at it, and understand that a
higher `versionCode` signed with the SAME key is what upgrades a friend's
phone in place instead of demanding an uninstall. Also the update-notice hook:
`version.json` with `latest` and a `url` at the APK, which shows a banner and
never downloads or installs anything by itself.

**Close-out:** six suites, **448 measured**, unchanged — this slice adds no
test slots, because what it changed is either derived (and therefore verified
by every configure) or documentation. Versions ×2 → 30.3.0, and the Android
stamps followed on their own.

**Next: Phase 4, the WASM build** — and before that, still, the VPS itself and
the field run that five slices are now owed.
