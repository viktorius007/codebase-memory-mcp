# Wave 2 plan (decided 2026-10-07 during wave 1)

Starts after wave 1 is merged, scored on the regenerated truth, and verified.

1. **Std inventory (Sol high).** Generator under `tools/` reading rustdoc JSON of the pinned
   toolchain (std, core, alloc) → the table arm S created (`internal/cbm/lsp/rust_std_methods.h`
   or the file it reads): one row per reachable method (inherent, via Deref target, via prelude
   trait), version and source per row, plus a per-type completeness stamp. Resolver verdicts:
   listed → std method; unlisted on a type stamped complete → the project's method; else withhold
   and record. Owner measurement (wave 1, arm S): std-reason stops pm 4, jmap 0, jmapc 0, codex
   922, ruff 223, bevy 151. Acceptance: those stops become links or stay recorded; no flagged
   pair added on any corpus; the gap query shows what remains.
2. **Count-then-fix of the item-4 mechanisms and the item-5 open items** from the merged
   main's stop records (one enumerating arm, then fix arms on closed slices).
3. **Negative-memo survivor** from arm H's pending manifest (the property it named).
4. **Q's Python ambiguity guard**: 375 ruff Python CALLS withheld, ≥3 correct; needs a Python
   ruler or a narrower guard.
5. One verification pair (Sol high + Opus 5.5 high) over the whole wave.
