STATUS: DONE (two mechanisms committed and scored; full fixture run 0 failures)
COMMIT: 3add2055 (lane/w4-wrap, rebased on main b3da18df)
CHANGED: src/pipeline/pass_lsp_cross.c (+352), internal/cbm/lsp/rust_lsp.c (+41), tests/fixtures/graph/rust/wfbound (new)
CLOSED (bevy sites still uncredited, main -> after commit 1, key file:line): G006 454->80, G028 80->4, G031 72->4, G040 57->29, G049 45->31, G019 165->164. Commit 2 (derived Deref) adds +319 credited, mostly in G005/G043/G046/G055 (Res/ResMut/Extract on a derive(Deref) newtype). Per-group recount after commit 2 not run.
OPEN:
- G005 remainder: Single<D> (deref to D's item), Gizmos -> GizmoBuffer then generic-trait method, TaskPool, Key; generic derive(Deref) newtypes (field types are not read against the type's params, so kept out); multi-field derive(Deref) with #[deref].
- G053/G054 (World.entity, 80): the reference impl `impl<T: ContainsEntity> ContainsEntity for &mut T` is an uncertain rank-0 candidate on `&mut World` because its bound sits in another file; needs a negative proof that World implements no ContainsEntity (closed-world over corpus traits, macro/derive emitted impls must be excluded first). Not written.
- G023 (TypeIdHashMap alias, 69): the alias lives under `cfg::alloc! { mod map; pub use map::*; }` in bevy_utils: path/macro-lane re-export, not a receiver-typing gap.
- G012, G020, G024, G025, G041, G044, G047, G057: not reached.
- derive(Deref) has no graph fixture: tools/fixtures/fixtures.py builds lib crates only and cannot build the proc-macro crate a derive needs. Probe used instead (Res<Cache>.get / Cache.get with #[derive(Deref)] struct Cache(HashMap)).
BLOCKERS: none now. Earlier: full `make -f Makefile.cbm fixtures` did not finish: the first attempt's make rebuilt prod_lsp_all.o at 5 percent CPU for 27 minutes (load average 29 on 16 cores, four lanes), the second (BIN= the final binary) hit a 25-minute timeout with no output. The new fixture passes alone (`FIXTURES_ONLY=wfbound`: right=2 withheld=1 mismatch=0 both modes). Edge diffs of pm/ruff/codex against main's gate5 score dbs timed out under the same load.
SCORES: bevy 73860/84295 f36 (tip 3add2055); pm 45167/47116 f87; codex 226246/229953 f1; ruff 104504/112300 f1
---
## Commit f6c53796: trait impl parameters bounded as the self type's declaration bounds them need no proof
- Groups G006, G028, G031, G049, G040 (Handle<T>.clone 366 of G006). Dump: `trait_receiver_shape:14:...handle.Handle:14:...handle.Handle`. `impl<T: Asset> Clone for Handle<T>` has a bounded T, so T was never substituted and the two Handle shapes differed.
- Rule: a cross-pass pre-step (pxc_rust_wf_*) reads the source of each Rust file that has a bounded trait impl. For `impl<..> Trait for Head<..>` with Head a struct/enum/union declared once in the same file, a parameter written as the whole argument at position p needs no proof when every bound it carries (inline or where, compared as written, whitespace dropped) is a bound the declaration carries at p. A bound that names a parameter counts only when both items name their parameters alike, and a `?Sized` declaration needs `?Sized` on the impl. Any other where predicate, a default or an unreadable parameter makes the impl ineligible. Such parameters join the method's rust_unbounded_impl_params.
- Corpus: examples/large_scenes/caldera_hotel/src/main.rs:148 `WorldAssetRoot(hotel_01.clone())` -> handle/impl#[`Handle<T>`][Clone]clone().
- bevy 72,341 -> 73,099, flagged 36 -> 36. CALLS removed 0, added 380 (edgediff vs main's inventory score db). Only SEMANTICALLY_RELATED churn was removed (42).
- pm 45,167 f87, ruff 104,504 f1, codex 226,246 f1 (codex +4).
- Fixture tests/fixtures/graph/rust/wfbound: Handle<T>.clone through a let from a generic call, a turbofish call, a parameter and self.field all link. `impl<T: Asset + Default> Clone for Slot<T>` against `struct Slot<T: Asset>` stays withheld, and `&Slot<Image>.clone()` (reference clone) gets no phantom.

## Commit 861afa4b: a one-field type with #[derive(Deref)] derefs to that field
- Groups G005 and its cascades G043/G046/G055. Dump: `...Res.get method_not_found` for `view_key_cache.get(..)`, `view_key_cache: Res<ViewKeyCache>`, `#[derive(Resource, Deref, DerefMut)] struct ViewKeyCache(HashMap<..>)`. The Res hop reached ViewKeyCache, which had no Deref target.
- Rule (admitted derive(Deref) assumption): in the cross registry, once a type's fields are registered, a non-generic type that derives `Deref` and has exactly one field takes that field's type as its Deref target. If it already has a different target, the target becomes unknown.
- Corpus: examples/shader_advanced/specialized_mesh_pipeline.rs:324 `view_key_cache.get(&view.retained_view_entity)` -> collections/hash_map/impl#[`HashMap<K, V, S>`]get().
- bevy 73,099 -> 73,418, flagged 36 -> 36. CALLS removed 0 vs main (added 678 cumulative).
- pm 45,167 f87, ruff 104,504 f1, codex 226,246 f1 (same as commit 1).

## Suites (run before the coordinator's gate-policy change; no suite run is active now, and the batch gate on main runs them) (final code; the rebase onto a3548bcd only adds a reports commit)
`scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract" BUILD_DIR=build/t-w4-wrap` on a fresh dir: 1962 passed, 0 failed, 0 skipped. EXIT=1 comes only from the LeakSanitizer exits in pipeline and edge_structural: the known field_cache leaks in rust_modtree.c rmf_scan_*, none in the new code.

## Fixtures
- wfbound alone: ok in both modes (sites=3, right=2, withheld=1, mismatch=0).
- Full run: not completed (see BLOCKERS). A third run started at 16:34 in the background (`make -f Makefile.cbm fixtures BIN=/scratch/agent/tmp/w4wrap/bin-c2`, log /scratch/agent/tmp/w4wrap/fixtures.log, output /scratch/agent/tmp/w4wrap/fixtures.out). The result is appended here when it ends.

## Build
`make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w4-wrap -j4`: rc 0, no warnings (-Werror).
- Final run, 16:38 (coordinator's duty; free -g showed 13 GB available): `FIXTURES_WORK=/scratch/agent/tmp/w4wrap/fxwork make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/w4-wrap` -> `fixtures: 75 fixtures x 2 modes, 96 known mismatches, 0 failures, 123.0 s`, EXIT=0, on tip 861afa4b. FIXTURES_WORK moves the work dir off /tmp: the earlier failure was `[Errno 122] Disk quota exceeded` on the shared /tmp tmpfs (16 GB, 80 percent used by other lanes' runs).

## Review round (4 findings), rebased on main b3da18df: tip 3add2055
- 3add2055 fixes all four findings. (1) The declaration must be a sibling item of the impl in the same module scope (file root or an inline mod's body, never a block), so bounds spelled alike name the same trait. Anything else withholds. (2) A method takes the innermost trait impl's list; an innermost impl that gains nothing gives none. (3) A bounded lifetime parameter or any lifetime where predicate makes the item opaque, and a type parameter bound by a lifetime other than 'static gains nothing. (4) Deref counts only inside a `derive(..)` attribute's comma-split argument list, as the last path segment.
- Probes: `#[doc = "derive(Deref)"]` newtype withheld; `#[derive(Clone, bevy::prelude::Deref)]` newtype links HashMap::get.
- Build rc 0, no warnings. wfbound: sites=4 right=2 withheld=2 mismatch=0 in both modes. rust_lsp: 836 passed, 0 failed. Full fixtures earlier on 861afa4b: 75 x 2, 0 failures.
- bevy on 3add2055: 73,860/84,295 f36. Main b3da18df was not scored here; its merge messages give about 73,063 (72,341 + 139 + 583). pm/ruff/codex were not rescored after the review (not requested); on 861afa4b they were pm 45,167 f87, ruff 104,504 f1, codex 226,246 f1.
