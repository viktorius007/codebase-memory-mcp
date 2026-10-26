STATUS: DONE. Three commits rebased on main 7b274c86. Build warning-free. rust_lsp + pipeline suites and `make fixtures` pass on the tip. The four corpora were scored on 9cbc1713; the coordinator ruled that the review fix needs no rescore.
COMMIT: a2b842da (lane/w3-query on main 7b274c86: 6e42db17 iteration family, ba973317 indexing family, a2b842da review fix: `(T,)` one-element tuple types, item depth bound 64)
CHANGED: internal/cbm/lsp/rust_lsp.c: the Query item rule (rust_bevy_ecs_type, rust_bevy_query_item, rust_bevy_query_iter_item, rust_bevy_query_read_only, rust_bevy_query_call_return), one hook each in rust_written_iter_item and rust_eval_call_type, and the trailing-comma tuple fix in rust_parse_type_node and parse_type_text_with_params. New fixtures: tests/fixtures/graph/rust/bevyqiter and bevyqget.
CLOSED: bevy Query-bound B1/B2 sites go from 422 to 53 in the re-cut below. The `D.Item` projection head (44 method_not_found in the w2-std table) no longer reaches bindings. Review items: singleton-tuple Query data `(&A,)` is kept as a tuple (it was flattened to `&A`), and the nesting bound is 8 -> 64.
OPEN: (1) The 53 residual sites. Most are `#[derive(Deref, DerefMut)]` component wrappers behind Mut (examples/time/timers.rs:50 timer.tick). The rest: `single_inner` 2, `par_iter_mut` closures 2, ParamSet `.p0()` 1, `Single<D>` / `Option<Single<D>>` Deref to D::Item (7 corpus-wide), and tuple-struct patterns (`Ok(PendingAnimation((a, b)))`). (2) Derived `#[derive(QueryData)]` structs have no item, by decision. (3) The w2-std USAGE item extract_cameras -> RenderExtractedVisibleEntities.classes has not been re-checked.
BLOCKERS: none. The one new bevy flag (mesh_view_bindings.rs:756 -> impl From<Msaa>) was accepted by the coordinator as a rust-analyzer truth gap; the evidence is below.
SCORES: bevy 71142/84295 f36; pm 44864/47116 f73; codex 226198/229953 f1; ruff 104406/112300 f1 (tip 9cbc1713 on main 605f06a2, whose figures are bevy 70,246, pm 44,864, ruff 104,406; bevy +896, the others unchanged, flagged unchanged except the accepted bevy pair)
---
# w3-query: bevy Query items

## Re-cut of the dump on main 9cff5b79 (measure first)
Binary built from 9cff5b79 (/scratch/agent/score/w3-query/bin-main). Bevy scored with CBM_RUST_RESOLVE_DUMP (score 69,506 / 84,295, f35, the brief's figure). Dump: /scratch/agent/tmp/w3q/dump-main.tsv.
- The w2-std grouping script, re-run unchanged: /scratch/agent/tmp/w3q/grouped-main.txt. The rows match the brief: for/&query 179, param ? 230, closure_param 120, pattern_binding 111, let-Some 107, let-Ok 70, iter_mut 54.
- Query-specific re-cut (script /scratch/agent/tmp/w3q/qcut.py <dump> /scratch/agent/tmp/rs-inv/bevy.cls.tsv). The script takes the uncredited B1/B2 sites whose identifier receiver is bound by a `for` pattern, a let-Ok / let-Some pattern or a closure parameter, where the bound expression's root is a fn parameter declared `Query<..>` / `Single<..>`. It then classifies the data parameter's text. 422 sites:
```
count | binding form  | source           | data (top level)
113 | for-tuple     | &mut query       | tuple
 43 | for-tuple     | &query           | tuple
 41 | for-tuple     | query.iter_mut() | tuple
 38 | let-Ok-tuple  | .get_mut         | tuple
 37 | for           | &mut query       | &mut T
 27 | let-Ok        | .get_mut         | &mut T
 22 | let-Ok        | .get             | &T
 21 | let-Ok-tuple  | .get             | tuple
 21 | for-tuple     | query.iter()     | tuple
 15 | for           | query.iter_mut() | &mut T
  8 | for           | &query           | &T
  7 | let-Ok        | .single_mut      | &mut T
  7 | for           | query.iter()     | &T
  5 | let-Some-tuple| .iter().next()   | tuple
  3 | let-Ok        | .single          | &T
  3 | closure-tuple | .iter()          | tuple
  2 | closure       | .get             | &T
  1 each: let-Ok(-tuple) .single_inner x2, .single, par_iter_mut closures x2, for over .p0(), for over by-value query, let-Some over Option<Single>, closure .iter()
Family totals: iteration 295, indexing 121, other 6.
Data shapes (element level): &mut T 88, &T 49, tuples of {&T, &mut T, Entity, Option<..>, Has<..>, Ref<..>} the rest; "other" 3.
```
The first failing step on all of these is the binding's type. The w2-std dump showed the Query method itself resolving, with the declared item `D::Item` / `ROQueryItem` reaching the binding unsubstituted, as `D.Item` or unknown. Single-typed receivers are rare (7 sites), so `Single` was left out.

## Commits
1. 267ed70c: iteration family (target 295 sites). Rule: QueryData::Item, as a closed reading of crates/bevy_ecs/src/query/fetch.rs: `&T`->`&T`, `&mut T`->`Mut<T>` (`&T` read-only), `Entity`->`Entity`, `Has<T>`->bool, `Ref<T>`->`Ref<T>`, `Option<D>`->`Option<Item D>`, tuple->tuple of items, any other shape->unknown. `iter`/`iter_mut` return QueryIter with `D::ReadOnly`/`D`. QueryIter as an Iterator, and `&Query` / `&mut Query` / `Query` as an IntoIterator, yield the item (hook in rust_written_iter_item, so for-loops, patterns and adaptor closures all read it). Types are matched by their path under crates/bevy_ecs/src. Gain: bevy 69,506 -> 70,081 (+575), flagged 35 -> 36 (the pair below). Example: crates/bevy_solari/src/scene/binder.rs:177 transform.to_matrix -> `impl#[GlobalTransform]to_matrix()`.
2. 9cbc1713: indexing family (target 121 sites). `get`/`single` -> `Result<Item<D::ReadOnly>, E>`, `get_mut`/`single_mut` -> `Result<Item<D>, E>` (the declared Result keeps E). Gain: bevy 70,081 -> 70,402 (+321), flagged unchanged at 36. Example: tests/3d/test_skinned_mesh_bounds.rs:107 player.play -> `impl#[AnimationPlayer]play()` through Mut's Deref.
After both commits the re-cut leaves 53 of the 422 sites; the residue is listed under OPEN.

## Removed pairs and the flagged pair (pre-rebase build against 9cff5b79, tools/rust-bench/edgediff.py)
CALLS removed 0, added 734. Other removed edges: SEMANTICALLY_RELATED 48 (similarity churn). Added edges: USAGE +1121, WRITES +331, TESTS +1.
New flagged pair: WRONG TARGET lsp_ufcs crates/bevy_pbr/src/render/mesh_view_bindings.rs:626 prepare_mesh_view_bind_groups -> :188 `From<Msaa>::from`, from line 756 `MeshPipelineViewLayoutKey::from(*msaa)`. Here `msaa` is the 5th element (`&Msaa`) of the `for (..) in &views` tuple, so `*msaa: Msaa` and rustc calls `impl From<Msaa>` (line 188). The truth row for line 756 is core `From::from` (ext). rust-analyzer did not type this loop's pattern bindings: every method call on them is `method_unresolved local:pattern_binding` in truth (lines 755, 765, 766, 775, 779, 782-785). It picked line 757's impl only from the `Option<..>` argument shape. We read this as a truth gap, not a wrong link, and did not withhold the edge to keep the count flat. Coordinator ruling: accepted as a truth gap; the edge is kept.

## Review fix (a2b842da)
- rust_parse_type_node and parse_type_text_with_params collapsed `(T,)` to T. A trailing comma now keeps a one-element tuple; `(T)` is still T.
- RUST_BEVY_ITEM_MAX_DEPTH is 64 (it was 8).
- bevyqiter gains `singleton` (`Query<(&Transform,)>`, `for (transform,) in &query`): 13/13 right. The binary before the fix leaves that site withheld (12 right, 1 withheld).

## Fixtures
- bevyqiter (13 sites): main 0 right, 12 withheld (the singleton site did not exist yet); tip 13 right.
- bevyqget (4 sites): main 0 right, 3 withheld, 1 FALSE-STOP (`D.Item` method_not_found); tip 4 right.
- `make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/w3-query` on a2b842da: 70 fixtures x 2 modes, 96 known mismatches, 0 failures.

## Suites
- Six suites on 9cbc1713 (rebased on 605f06a2), one foreground command: 1961 passed, 0 failed, EXIT=1. Besides LeakSanitizer, pipeline printed one failure site: test_pipeline.c `repeated -> pair_act`.
- That row flapped on main itself. main 6ab8c225 flipped its expectation, and 06fcb231 ("expects a withhold again") flipped it back. Production binaries built from main's rust_lsp.c and from ours give the same unresolved row on that input (trait_receiver_partial). On 3d6c0437 the pipeline suite passed twice in a row with no failure site.
- Final tip a2b842da, rust_lsp + pipeline (`scripts/test.sh --suites "rust_lsp pipeline" BUILD_DIR=build/t-w3-query`): 1378 passed, 0 failed, 0 skipped, no failure site. EXIT=1 comes from LeakSanitizer only (the known leak exit). Log: /scratch/agent/tmp/w3q/suites-final.log.

## Score dirs
/scratch/agent/score/w3-query/{s0-bevy (main 9cff5b79), sA-bevy (commit 1), s1-bevy (both commits, pre-rebase), s1-bevy-edges, tip-<corpus> (9cbc1713 on 605f06a2)}.
