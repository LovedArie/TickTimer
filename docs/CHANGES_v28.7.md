# v28.7.0 — Pieces in the list (the TickTick door)

*Owner verdict after A/B-ing TickTick's subtask flow against ours: theirs
is more intuitive — create from the list, see structure in the list, and
the parent's checklist stops being the only door (it doesn't scale past
a handful of pieces). UI + one display-policy amendment; no format
change (v13).*

## What changed

- **Right-click a task → "Add a piece"** (category list): creates the
  piece immediately (title "New piece" — create-first-name-second,
  TickTick's own order) and opens the panel on it with the **title
  focused and fully selected**, so the first keystroke replaces the
  placeholder. Right-click on a piece offers nothing — one level only,
  with the real wall in the domain (`addSubtask` refuses piece parents).
- **Pieces render as indented rows** under their parent in the category
  list: real rows — checkbox ticks, due chip shows, ✕ archives, click
  opens the panel with the breadcrumb. Interleaving happens in
  `CategoryTaskModel` *after* the parent sort, so a family never splits
  even when dates would order another parent between them.
- **§D display policy amended, counting policies untouched:** pieces
  appear in the category list as indented *structure*; upcoming,
  affordability, and week digests still count parents only. The parent
  panel's checklist stays — quick ticks there, structure in the list.
- New plumbing: `cattask::IsPieceRole`; indent applied in the delegate's
  single `geometryFor` (paint and click hit-tests shift together);
  `runTaskDetailNaming` + `focusTitleForNaming` + `selectTitleForNaming`
  (select-all is deliberately NOT the default open behavior — on an
  existing task it would put the title one keypress from gone).

## Tests

`categoryModelInterleavesPiecesUnderTheirParent` (family stays together;
archived pieces out) and `startPieceCreatesUnderParentOnly` (create +
panel-on-newborn + one-level no-op). The latter caught a real cost in
the modal fallback: its exec() blocks — panel-less windows get a
blocking session, offscreen tests hang on it. Noted in the test.
**348 tests green across six suites** (measured).
