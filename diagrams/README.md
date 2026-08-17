# Diagrams

Source-of-truth for **new** diagrams is **PlantUML** (`.puml`). The rendered
`.png` / `.svg` beside each source are generated from it — regenerate after
editing.

## Render

```sh
# one diagram, both formats
plantuml -tpng app_architecture.puml
plantuml -tsvg app_architecture.puml

# all of them
plantuml -tpng *.puml && plantuml -tsvg *.puml
```

Requires `plantuml` + `graphviz` + a JRE (`apt install plantuml graphviz
default-jre-headless`). In an editor, the VS Code "PlantUML" extension previews
`.puml` live.

## Shared style

Every diagram starts with `!include _style.puml`, which sets the app palette
(focus `#2F7E6E`, danger `#C25B54`, ink `#616974`) and shapes. Edit `_style.puml`
to restyle all diagrams at once. `_style.puml` is an include, not a diagram —
don't render it directly. *(Restored in v24 — it was documented and included
by every diagram but missing from the tree, so `plantuml *.puml` failed; the
palette here is reconstructed from Theme.h and this README's own description.)*

## Toolchain status — read this before believing anything else

The iteration plan has said **"Mermaid retired"** since v20. That was **never
true**, and the v26.8 audit is where it got corrected instead of repeated.
The real state:

| | count | |
|---|---|---|
| PlantUML `.puml` | **34** | canonical; all render, all have current `.png` + `.svg` |
| Mermaid `.mmd` still live | **15** | render fine, PNGs current — **tracked debt**, see below |
| Mermaid superseded | 6 | converted in v26.8, archived under `legacy-mermaid/` |

*Counted at v29.1.0 with `ls *.puml | wc -l`, `ls *.mmd | wc -l`,
`ls legacy-mermaid/*.mmd | wc -l`. The row above read 25 / 9 / 6 from v26.8
until then — this table's own rule, applied to this table, six sessions
late. Re-run the three commands rather than trusting the printed numbers.*

**Why the nine survivors are not being converted in a hurry.** They render,
their outputs are current, and nothing downstream is broken by them. The six
that *were* converted shared a different property: they were indexed as
canonical documentation and had **never been rendered even once** — six
diagrams in the repo that nobody had ever looked at. That is a real defect;
"uses the older of two working tools" is not. Converting the rest is a
half-hour of honest work for a future session, listed here so it stays
visible rather than becoming folklore again.

**The rule this section exists to enforce:** a status claim in documentation
is a promise that someone will eventually `ls` the folder. Write the number,
not the vibe.

## The diagrams

| File | Type | What it shows |
|------|------|---------------|
| **`app_architecture`** | **component** | **The establishing shot (v26.8, new): all 7 pages, the model/view layer, the pure/wire split, and the single `changed()` signal that drives every repaint and every save. Start here.** |
| `model_view_pipeline` | component | Upcoming: rebuild-on-change vs model/view (v20) |
| `model_refresh_decision` | activity | `refresh()`: granular update vs reset (v20.1) — *converted to PlantUML in v26.8* |
| `activities_modelview_contrast` | component | Upcoming vs Activities pipelines, and why they refresh differently (v20.2) — *converted in v26.8* |
| `snapshot_model_hierarchy` | class | the shared `TaskSnapshotModel` base (v20.3) |
| `task_lifecycle` | state | Task / Activity life stages |
| `repeat_chain` | component | recurrence: the rule on the newest link |
| `block_alarm_flow` | component | block-start alarms, derive-don't-store |
| `pomodoro_engine_faces` | component | one Pomodoro clock, many faces |
| `settings_pref_flow` | component | how a display preference travels |
| `settings_pages` | class | Settings as shell + pages: the `SettingsPage` contract, why the dialog knows no concrete page, and why pages are built eagerly (v26.1) |
| `deadline_time_flow` | component | a deadline **time** crossing five layers, and why the date/time pairing rule lives at the domain door only (v22) — *converted in v26.8* |
| `glance_focus_states` | state | the glance panel's four shapes, all derived, no stored mode (v22.4) — *converted in v26.8* |
| `needsblock_height_bound` | component | the card-height bug in three acts: hints suggest, stretch factors claim (v21 to v22.1) — *converted in v26.8* |
| `window_memory_restore` | activity | restoring the window: four ways in, one default rectangle out (v23) — *converted in v26.8* |
| `window_memory_save` | activity | the save side: why the debounce is load-bearing and `closeEvent` is only a courtesy (v23.1) — *split out in v26.8* |
| `needs_block_rule` | component | "needs a block", complete: coverage/flag rule, the derived list, the two clocks, the gate, placement (v21.5, parts 1-3) |
| `catch_up_ladder` | activity | catch-up: the derived verdict, then the reschedule ladder tried cheapest-first, ending in an honest empty hand (v26.2) |
| `catch_up_surfaces` | component | catch-up part 2: card asks, panel forwards, page decides, doors mutate — and the one `changed()`-driven repaint path (v26.2) |
| `catch_up_chip_states` | state | the chip's three intensities and why no state locks the drawer — attention and access as separate axes (v26.7) |
| `share_compare_sequence` | sequence | one "Compare" click, end to end |
| `update_check_sequence` | sequence | one launch-time update check |
| `quickadd_flow` | component | natural-language quick-add: three surfaces, one parse, AI fallback (v21.2) |
| `llm_provider_dialects` | component | the AI provider layer: one value, two dialects, the pure/wire split (v24) |
| `chat_turn_flow` | activity | **one chat turn end to end (v25): context rebuilt at fire time, the transcript window, read-only by contract** — *existed since v25 but was never indexed until v26.8* |
| `extract_text_flow` | activity | `ai::extractText` on a reasoning model: the `<think>` scrub, the reasoning-field fallback, and where each named error fires (v25.2) |
| `persona_prompt_bands` | component | the assistant prompt as four bands — two locked, one yours, one generated — and why persona lives in QSettings, not data.json (v25.3) |
| `chat_route_walk` | activity | one chat turn walking the seat route: the breaker's fast-fail, the unreachable-only fall-through, and the fallback attribution notice (v26) |
| `checkin_surfaces` | component | v28.2 part 2: the knock → the tap → one-button mood, and the two walls (includeMood default-false, `ai::isLocal`) that keep mood on the machine |
| `checkin_gate` | activity | v28.2 part 1: the check-in's stingy gate — morning window, once-only, heavy-day test — and why mood (format v12) is the first underivable fact |
| `affordability_flow` | activity | v28.0's whole proactive pipeline with no model in it: timer → `afford::` verdict → manners gate → C++ sentence → alert toast. The sentence box is the ONLY thing 28.1 replaces |
| `debug_seams` | component | v28.10: the Ctrl+Shift+D panel and every seam it presses — the clock rewire through the composition root, the service doors, the briefing viewer, and the `TICKTIMER_AI_DOWN=*` path to every wire's fallback voice |
| `write_boundary` | component | v29.0: proposal → validation → confirmation → effect with no model anywhere; the amber tap in the middle, the stale-card re-validation, and the Slice 2 swap point (a new proposer, nothing else changes) |
| `intake_flow` | component | v29.1: the interview — three answer tiers (crisp C++, the model, the honest hint), the guess crossing the card, Skip through the owner's own door, and the C++ path complete with every AI seat down |
| `example_class_diagram` | class | teaching reference: the house notation for class diagrams — *not a picture of shipped code* |

## Planning diagrams (PLANNED features, nothing built)

| File | What it shows |
|---|---|
| `assistant_spine.*` | The rule the whole AI arc follows: **code decides *when*, code computes *what is true*, the model only *phrases*** — plus the trust boundary (proposal, guarded doors, your tap) and the no-model fallback path. See `docs/design-addendum-assistant.md` sections A-B. |
| `assistant_roadmap.*` | Iteration dependencies: v26 foundation, v27 subtasks & sizing (*no AI*), v28 proactive (*read-only*), v29 tool use, v30 memory, and why each arrow points where it goes. See section N. |

## Audit habit

After any session that adds or renames a diagram, run:

```sh
# every source has a rendered output
for f in *.puml; do b=${f%.puml}; [ "$b" = _style ] && continue; \
  [ -f "$b.png" ] || echo "UNRENDERED: $b"; done

# every source appears in this file
# (prefix match, not exact: the planning diagrams are indexed as `name.*`)
for f in *.puml *.mmd; do b=${f%.*}; [ "$b" = _style ] && continue; \
  grep -q "\`$b" README.md || echo "UNINDEXED: $b"; done
```

Both checks would have caught the v26.8 findings the day they appeared:
**four unrendered diagrams** (`catch_up_ladder`, `catch_up_surfaces`,
`catch_up_chip_states`, `settings_pages` — the entire v26.1/v26.2 arc shipped
with sources nobody could see) and **six unindexed** ones. Ten seconds of
shell beats six months of drift.
