# v28.6.2 — Uniform panel color (the Theme.h trap's second detonation)

*One-line fix, one honest filing. The panel's patchwork coloring —
white header/footer, palette-grey middle — was `QScrollArea::setWidget()`
flipping the form's `autoFillBackground` ON, exactly as Theme.h's v3
comment has warned since the agenda turned black on dark-mode Windows.*

## What changed

- `TaskDetailPanel::buildFor` switches `autoFillBackground` back OFF
  after every `setWidget` — the panel's white now shows through
  uniformly, including after the rebuilds a save and a navigation do.

## The filing

This is the **second documented landmine this project has re-hit** (the
first: the data-folder rename stepping past main.cpp's own warning).
The tripwire rule applies — a comment that documents a trap is owed a
read by every patch near it. The trap now has a TEST
(`theFormNeverFillsItsOwnBackground`), which is the only tripwire that
stops a patch instead of hoping to be read: it pins the flag off after
open AND after a save's rebuild. TROUBLESHOOTING gains the
symptom-keyed entry.

## Tests

**346 green across six suites** (measured).
