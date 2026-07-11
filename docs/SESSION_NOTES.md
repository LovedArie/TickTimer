# Session Notes — TickTimer (C++/Qt mentorship)

*Running record. Used to decide what to quiz and where to pick up. Updated at
the end of the v19.1 session — the DAILY-DRIVER pass: the first session built
entirely from the owner's own usage feedback (archive, priority, honest
tracking, schedule compare — upgraded mid-session to an editable two-agenda
planning screen on owner feedback — editable special days).*

---

## Project & where it stands

**TickTimer** — a C++17 / Qt 6 Widgets desktop app: a plan-vs-actual time
tracker that credits every life area (work, health, relationships, rest), built
especially for people whose focus is derailed by anxiety-driven procrastination.

- **State:** **v19.1.0** (`include/Version.h`, the single source of truth
  feeding the code, both Windows exes, and the update check), data format
  **v7** (v6 + archived/priority/day-colour, all additive), **85 tests
  green** (46 domain + 11 UI + 19 auth + 9 live end-to-end), clean
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
build — worth deleting to avoid editing the wrong copy. Also still open:
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
