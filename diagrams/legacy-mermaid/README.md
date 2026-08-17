# Superseded Mermaid sources

These six `.mmd` files were converted to PlantUML during the **v26.8
documentation audit** and are kept here, not deleted.

**Why keep them?** The same reason `JsonStore::migrateLegacyData` copies
instead of moving: a conversion is a *rewrite*, and a rewrite can lose a
detail nobody notices for six months. The original is the only way to check.
They cost a few kilobytes and settle any "did the diagram used to say X?"
argument in ten seconds.

**Why these six specifically?** They shared one property: every one of them
was *indexed as canonical documentation and had never been rendered even
once*. No `.png`, no `.svg` — the tree carried six diagrams that literally
nobody had ever looked at. Converting them was the cheapest way to find out
whether they were still true. (Two were: `deadline_time_flow` and
`model_refresh_decision` described the code exactly. The rest needed only
wording.)

One diagram became two: `window_memory_restore.mmd` drew the restore path
and the save path in a single flowchart with two disconnected entry points.
That is not expressible in PlantUML's activity syntax, and it was confusing
regardless — they are two independent stories that happen to share a
preference key. They are now `window_memory_restore.puml` and
`window_memory_save.puml`.

**Nine other `.mmd` files remain live** in the parent folder. They render
fine and their PNGs are current, so converting them is real work with no
user-visible payoff — tracked as debt in `../README.md` rather than done in
a hurry. See that file's "Toolchain status" section.
