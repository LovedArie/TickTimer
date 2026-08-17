# v28.9.0 — Promotion (the double-count closed)

*The slice-2 design decision, shipped. Domain arithmetic only — one
function changed, nothing stored, format stays v13.*

## The rule (subtasks addendum §O)

**The trigger is the date.** Affordability's first guard already gives a
dated piece its own verdict, nudge, and needs-a-block line — so its
minutes now LEAVE the parent:

- **Sized parent:** own estimate MINUS the dated pieces' estimates,
  floored at 0. A fully-decomposed parent (pieces promoted past its
  estimate) cedes everything and honestly degrades to the
  planned-blocks proxy — never negative, never subsidizing other work.
- **Unsized parent:** borrows ONLY the undated pieces (the v28.4 rule,
  amended — a dated piece's minutes are its own on the borrow path too).
- **Undated pieces are unchanged**: no verdict of their own, still
  weigh on the parent. The v28.4 borrow test passes untouched — the
  amendment is additive.

Before: a 12h parent with three dated 4h pieces read as 24h of believed
work nobody entered — wrong verdicts, doubled nudges. After: believed
total = entered total, once (`promotedPieceStopsWeighingOnItsParent`
pins the owner's exact FINALS case: 480 + 240 = 720).

`Report` gains `minutesPromoted` — the ledger of what left, for
sentences and tests.

## Why now

v28.7 made dating a piece one right-click away, turning the latent
double-count into a live one on the main path — and v29's tool use
inherits whatever the numbers say. Arithmetic first, hands second.

## Tests

`promotedPieceStopsWeighingOnItsParent`, `borrowSkipsPromotedPieces`,
`fullyPromotedParentFallsBackToTheProxy`; the v28.4
`pieceEstimatesSizeAnUnsizedParent` green unchanged.
**353 tests across six suites** (measured).
Diagram: `diagrams/piece_promotion.*`.
