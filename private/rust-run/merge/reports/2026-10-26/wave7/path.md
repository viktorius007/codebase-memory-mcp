STATUS: DONE. Both items landed, plus generic From selection. Bevy passes 90 percent: 75,908 against a 75,866 target
COMMIT: 553a2af1 (lane/w7-path rebased on main ed41f23a; the review fixes are the last commit)
CHANGED: internal/cbm/lsp/rust_modtree.c, rust_modtree.h (CBMRustPathResult.value_type), rust_lsp.c; fixtures tests/fixtures/graph/rust/{constrecv,bitflagsdecl,genericfrom}
CLOSED: RenderAssetUsages default sites 66->0 (bitflags); const/static receivers and arguments: bevy +117 and pm +422 on the final tip (B5); generic `From<T>` sole impl: bevy +51
OPEN:
- bitflags!-generated methods (`all`, `bitor`, `eq`, `is_empty`, `contains`) are deliberately unlinked: truth.tsv gives them no node ("-"). Only the hand-written `impl Default for RenderAssetUsages` (66 rows, render_asset.rs) has one.
- `Type::from(x)` misses that remain: arguments with a generic return type (`asset_server.load(..)`) and std-typed arguments.
BLOCKERS: none. Build hazard, reported for the gate: 2e5cacba adds a field to CBMRustPathResult. An incremental build in my BUILD_DIR left pass_usages.o and the other objects including rust_modtree.h at the old layout (their mtimes predated the header edit and make did not rebuild them), and the index worker segfaulted in cbm_pipeline_rust_item_usage_target. The final tip was built in a fresh BUILD_DIR. Anyone picking it up incrementally must delete those objects
SCORES: bevy 75908/84295 f9; pm 45659/47116 f87; ruff 104773/112300 f1; codex 226498/229953 f1 (tip 748d5fd6, one corpus at a time). Main 6406679d, built fresh and scored the same way: bevy 75654 f9; pm 45237 f87; ruff 104704 f1; codex 226453 f1. The flagged pair sets are identical on all four (diff of wrong_pairs.tsv is empty), and no flagged pair is added or removed
---
## 2e5cacba: a `const` or `static` named through the module tree has its declared type (item 2, B5)
- Mechanism: the tree scan records each const/static type text (RmfRecord.text -> RmtDef.value_type, copied into the tree arena). A NOT_FUNCTION result carries it as CBMRustPathResult.value_type, and the file digest hashes the type node. rust_eval_value_path_type, when the path names no type or variant, resolves the path through the tree and parses the type in the declaring module.
- Gain (measured before the rebase, bevy only): bevy 75,604 -> 75,721, flagged 9 unchanged, no removed pairs. Example: examples/3d/transmission.rs:125 `LinearRgba::from(ANTIQUE_WHITE)` now links srgba/impl#[LinearRgba][`From<Srgba>`]from().
- Fixture: constrecv. Main: FALSE-STOP on the From call and withheld on `ORANGE_RED.lighter`; now both are right.
## b9d4d849: a `bitflags!` invocation declares its structs in the module tree (item 1)
- Truth reading: only the hand-written Default impl has nodes (66 rows); the generated methods have none and stay unmodelled.
- Rule: each `struct Name` token pair inside a `bitflags!` invocation (macro path ending in `bitflags`) is a struct item of the module, with `pub` read as public and `pub(..)` as crate-wide. The module keeps its may-declare-unseen-names fact.
- Gain: bevy 75,721 -> 75,807 (+86, all 66 RenderAssetUsages default sites plus chains), flagged unchanged. Example: crates/bevy_winit/src/cursor/custom_cursor.rs:193.
- Fixture: bitflagsdecl, a three-crate workspace with a bitflags! stand-in in another crate, reached through `pub use render_asset::*`. Main: FALSE-STOP; now right.
## 748d5fd6: `Type::from(x)` links the type's only From impl when that impl is generic
- Rule: if no impl matches the argument exactly, the type has one impl of the trait, and its parameter is an impl type parameter, link that impl. The argument must be typed and must not be the type itself; std's reflexive `impl<T> From<T> for T` takes that case.
- Gain: bevy 75,807 -> 75,858 (+51), flagged unchanged. Example: examples/stress_tests/many_text.rs:146 `TextColor::from(css::YELLOW)` now links text/impl#[TextColor][`From<T>`]from().
- Fixture: genericfrom. Main: FALSE-STOP; now right.
## Final tip checks (748d5fd6 on 6406679d)
- Build: fresh BUILD_DIR, no warnings.
- Fixtures (full, FIXTURES_WORK under /scratch): 103 x 2 modes, 98 known, 0 failures.
- rust_lsp on a fresh dir: EXIT=0, 840 passed, 0 failed.
- Scores: see SCORES.
## 553a2af1: review fixes (three items)
- A const typed by an applied generic alias withholds (constrecv: `WRAPPED.get()` is withheld).
- The generic From fallback expands aliases on the argument and the receiver before the reflexive comparison, and withholds when the argument still names an alias it cannot expand.
- The generic From fallback requires the impl's trait to be core::convert::From.
- Tests: rustlsp_generic_from_alias_of_self_is_reflexive, rustlsp_generic_sole_impl_fallback_requires_from_trait.
## Final tip 553a2af1 on ed41f23a
- Build: fresh BUILD_DIR, no warnings.
- Fixtures: 105 x 2 modes, 98 known, 0 failures.
- rust_lsp on a fresh dir: EXIT=0, 842 passed, 0 failed.
- Bevy: see the line appended below. pm, ruff and codex were not rescored after the review fixes and rebase.
- Bevy on 553a2af1: 76263/84295 f9.
