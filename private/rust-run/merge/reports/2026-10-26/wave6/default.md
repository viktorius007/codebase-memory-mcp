STATUS: DONE (the target cannot come from this class: of the 645 bevy default_impl_not_proven dump rows, only 55 have a truth pair; bevy 75,627 at the tip after rebase onto 38074ddf)
COMMIT: 85248403 (lane/w6-default on main 38074ddf: f798114a plus review fix 85248403)
CHANGED: internal/cbm/lsp/rust_lsp.c; fixtures tests/fixtures/graph/rust/{defaulttuplector,defaultnested,defaultgenericfield}
CLOSED: G042 / default_impl_not_proven with a truth pair: 55 -> 32 (+23 credited on bevy; 0 sites lost)
OPEN:
- About 590 of the 645 dump rows have no truth pair. Their Default is derived or external, so no graph node can be credited for them; this is not a resolver gap.
- 109 more uncredited `[Default]default()` truth pairs stop earlier, at function_not_in_registry on `Type::default()`:
  - RenderAssetUsages::default() (64 sites, 58 in crates/): the type is declared by `bitflags!` (an external macro), so neither the module tree nor the registry sees the type. A bounded rule would read `bitflags! { vis struct Name: Ty {..} }` as a struct item in rust_modtree.
  - `<HashMap<_, _>>::default()` (23), TypeIdHashMap/Schedule::default (9): path lane.
- 21 of the remaining 32 default_impl_not_proven truth sites are `(camera, Default::default())` tuples whose map is reached through `ResMut<ExtractedUiNodes>` (Deref through the change_detection_impl! macro); 1 is `entry.insert((e, Default::default()))` on a std VacantEntry, whose argument type no table declares.
- The rest of the 32: `..Default::default()` on literals of types reached only through glob paths (4), `let mut x = Default::default();` inferred later (2), field values whose struct is reached through an unresolved path (6).
BLOCKERS: none
SCORES: bevy 75627/84295 f9 (tip f798114a on 38074ddf); pm 45169/47116 f87; codex 226364/229953 f1; ruff 104672/112300 f1. pm, codex and ruff were measured on the tip as first committed (on 05649859), against main 05649859 at 45,169 f87, 226,363 f1 and 104,672 f1.
---
## Dump-first
- Main 05649859 (own build of main): bevy 75,306 f36. 645 R rows default_impl_not_proven; 55 of them are uncredited truth pairs.
- Positions of the 55 (scorer miss list, call text):
  - tuple element in an `or_insert_with` closure or `insert` argument: 23
  - tuple-struct or tuple-variant constructor argument: 12
  - struct field value: 10
  - `..Default::default()`: 4
  - method argument: 2
  - inferred `let`: 2
  - return: 1
  - match arm: 1
- The existing positions (field value, resolved argument, annotated let, return, struct update) already work when the expected type resolves. The probe crate showed field values and arguments of plain HashMap types resolving in the cross pass.

## Commit f798114a (rule per position; bevy measured before the two rebases)
rust_expected_type_qn is split into a typed core, rust_expected_type, with these new positions:
- Tuple-struct and tuple-variant constructor arguments read field `<i>` / `V.<i>`: +13. Example: crates/bevy_ecs/src/entity/hash_map.rs:77 `Self(Default::default())` -> collections/hash_map/impl#[`HashMap<K, V, S>`][Default]default().
- Tuple element of a declared tuple, a borrow of a declared reference, and the value of an `or_insert_with` / `unwrap_or_else` / `get_or_insert_with` closure (the call's result without its borrow): +8 (bevy_ui_render `or_insert_with(|| (camera, Default::default()))` where the map is reached directly).
- A literal field declared through the struct's own parameter, read with the arguments of the literal's expected type: +2 (clone_entities.rs:455/471 OptOut/OptIn).
- Withholding is unchanged: a generic parameter, an unknown type, or a type with no corpus Default impl still records default_impl_not_proven.
- Bevy 75,306 -> 75,319 -> 75,327 -> 75,329 on 05649859; 0 sites lost.

## Removed pairs (edgediff against main 05649859, own build)
- CALLS removed: 0 on all four corpora. CALLS added: bevy 18, codex 1, pm 0, ruff 0.
- Non-CALLS removals: bevy 18, codex 3 (similarity edges).

## Checks on the final tip f798114a (main 38074ddf)
- Build: no warnings.
- Fixtures: 99 x 2, 0 failures.
- rust_lsp: 840 passed, 0 failed.
- Bevy: 75,627/84,295 f9. The w6-recv lane measured main at 75,604, so this lane adds +23 on top.

## Review fix (85248403)
- The closure rule now reads the expected value only from std receivers: an `or_insert_with` entry (through the std entry table), `Option::unwrap_or_else` / `Option::get_or_insert_with`, and `Result::unwrap_or_else`. A corpus method of the same name withholds.
- defaultnested gains `Maker::unwrap_or_else(&self, make: fn() -> Bag) -> Other`. The pre-fix binary reports a PHANTOM main -> Other::default there; the fixed one has mismatch 0.
- After the fix: build has no warnings; the 3 default fixtures pass; rust_lsp 840 passed, 0 failed; bevy 75,627 f9, unchanged.
