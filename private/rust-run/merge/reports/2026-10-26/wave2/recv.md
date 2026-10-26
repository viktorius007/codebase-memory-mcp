STATUS: DONE
COMMIT: 5fa5f27c (lane/w2-recv, 6 commits on main 27586789; main has not moved, so no rebase was needed)
CHANGED: internal/cbm/lsp/rust_lsp.c, internal/cbm/lsp/rust_lsp.h, src/pipeline/pass_lsp_cross.c (pxc_rust_* in cbm_pxc_collect_all_defs), src/pipeline/pipeline_internal.h (one closed reason code), tools/fixtures/known.tsv (one entry), tests/fixtures/graph/rust/{qiter,macderef,macderef2,selfparam,derefalias}
CLOSED: D2 (bevy 463 -> 22); D1 Res/ResMut through macro-emitted Deref (3,273 -> 358); B3 generic-parameter fields (bevy 1,390 -> 1,183, pm 227 -> 99); Deref targets written as aliases; the 6 review items on 9bff1152/a8460dd1
OPEN: B4 (bevy 761, pm 509); B5 const/static (bevy 243, pm 207); D1 rest (358: Extract 111, ResMut 104, Res 54, Single 48, Local 41); D3 (bevy 374, pm 98); 1 pm pair lost to e625affd (see Removed pairs); macro binding through `use` globs (review item 5, narrowed rule); incremental re-resolve has no macro table
BLOCKERS: none
SCORES: bevy 67813/84295 f35; pm 42364/47116 f73; codex 226198/229953 f1; ruff 103872/112300 f1
---
# w2-recv: receiver typing, wave 2

The baseline is current main 27586789, scored by /scratch/agent/gate3 (lane/rs-macro 040a9e27, the same code as main): bevy 63,114, pm 42,237, ruff 103,794, codex 226,167; flagged 35 / 73 / 1 / 1. The edge diffs below come from tools/rust-bench/edgediff.py run against those score.db files.

## Net on the final tip 5fa5f27c, against main
- Credited: bevy +4,699, pm +127, ruff +78, codex +31.
- Flagged did not change on any corpus.
- CALLS pairs removed: bevy 0, pm 1 (explained below), ruff 0, codex 0. CALLS pairs added: bevy 3,406, pm 128, ruff 69, codex 27.

## Commits
1. **9bff1152, class D2 and the prelude-name sites.**
   - The file-local pass published a guessed `<type>.<method>`. That made the site ambiguous against the project pass's inherent hit, so the call was dropped as no_resolution.
   - The guess is now an unresolved stop.
   - bevy 63,114 -> 63,722. Example: crates/bevy_winit/src/system.rs:259 `closing.iter()` -> query.rs:678, truth `system/query/impl#[Query<'w, 's, D, F>]iter().`
   - Fixture: qiter.
2. **a8460dd1, class D1.**
   - A Deref impl emitted by a file-level invocation of a corpus macro_rules! now becomes an impl relation on `<invoking module>.<owner>`. This is bevy `change_detection_impl!(Res<'w, T>, T, Resource)`.
   - The matcher now reads a lifetime as one tt.
   - bevy 63,722 -> 67,550. Example: examples/ui/text/text_debug.rs:34 `asset_server.load(..)` -> crates/bevy_asset/src/server/mod.rs:364, truth `server/impl#[AssetServer]load().`
   - Fixture: macderef.
3. **8da96c7d, the review items.** The edge set is identical to a8460dd1.
   1. Reason code. Settled finally by commit 6.
   2. A literal matcher word matches only a whole input word, so `&mut` no longer matches the head of `&mutant`.
   3. cfg is read on the statement before it is unwrapped, and cfg-gated emitted impls are withheld.
   4. Every method of every other impl in the file's corpus-macro expansions is registered as an inherent method of the wrapper. It has no node, so the call publishes nothing, but it is never read through the Deref.
   5. An invocation is skipped when a file-level `use` may bind its name outside the corpus: a path not rooted at crate/self/super that ends in the name, or a glob under any crate except std/core/alloc.
   6. The Deref path must name core::ops::Deref: an explicit core/std path, or a bare `Deref` imported from core/std ops with no other item or import of that name in the file. The relation carries `core::ops::Deref`.
   - Fixture: macderef2, which holds a cfg-gated invocation with a different target next to a written Deref, and a macro that emits both `impl Own { fn load }` and a Deref.
   - One known entry was added to tools/fixtures/known.tsv: the emitted Own::load has no graph node (UNMAPPED). Checked directly: own() gets no edge to Inner::load.
4. **e625affd, class B3: fields typed by a generic parameter.**
   - `self.f` through the impl's bare `self`, where `f` is declared as the struct's own parameter (also behind references), takes the impl's parameter at that position as a type parameter. The impl's bounds then dispatch the call (lsp_bound_dispatch to the trait declaration).
   - A parameter inside a compound field type is left alone. An earlier version substituted it and removed 12 correct bevy pairs (MessageIterator -> MessageIteratorWithId); it was not committed.
   - bevy 67,550 -> 67,739. Example: crates/bevy_math/src/curve/adaptors.rs:270 `self.preimage.domain()` -> curve/mod.rs:327, truth `curve/Curve#domain().`
   - pm gains here: 42,237 -> 42,364.
   - Fixture: selfparam, which includes a renamed parameter after a lifetime. It is red on main.
5. **c1ae3ece, class B3/D1: Deref target written as an alias.**
   - The Deref target is expanded through rust_expand_type_aliases, e.g. `type Target = DiGraph<N, S>`.
   - bevy 67,739 -> 67,813. Example: crates/bevy_ecs/src/schedule/pass.rs:69 `self.dag.add_edge(..)` -> graph_map.rs:153, truth `schedule/graph/graph_map/impl#[Graph<DIRECTED, N, S>]add_edge().`
   - Fixture: derefalias.
6. **5fa5f27c, review item 1 settled.**
   - Reusing method_not_found turned fixture rust/wrap `Box<S>.clone()` -> `S::clone` into a FALSE-STOP. A prelude-trait name may still exist through an impl the resolver cannot prove.
   - The stop is prelude_trait_method_not_proven again, now added to the closed set in pipeline_internal.h with a one-line meaning. method_not_found stays the stop for every other name.
   - The edge set is unchanged.

## Removed pairs
- One pm pair, against main:
  - crates/pm-store-runtime/src/memoized_snapshot.rs:328 `GraphSnapshotSource::all_requirement_quality_attributes(self.inner)` inside `impl<S: GraphSnapshotSource + ..> ReqReadCapabilities for MemoizedSnapshotSource<'_, S>`. It is credited on main, truth `GraphSnapshotSource#all_requirement_quality_attributes()`.
  - After e625affd, `self.inner` (`&'a mut S`) is the bounded type parameter S, not an unregistered placeholder. The trait-path (UFCS) call then stops at function_not_in_registry.
  - The cause is P1 (trait path call with a type-parameter argument), not receiver typing. It is left OPEN: the fix belongs in the UFCS path, which is w2-trait's area. Net pm is +127.
- All other removed rows in the diffs are SEMANTICALLY_RELATED similarity rows.

## Verification on the final tip 5fa5f27c
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w2-recv -j4` exits 0 under -Werror.
- Suites: 1,962 passed, 0 failed. pipeline and edge_structural exit rc=1 on LeakSanitizer reports only, with 0 failed assertions (the known case).
- Fixtures line: `fixtures: 59 fixtures x 2 modes, 94 known mismatches, 0 failures, 44.2 s`
- Scores (scripts/rust-score.sh, one corpus at a time, tip binary):
  - bevy 67,813/84,295 f35
  - pm 42,364/47,116 f73
  - ruff 103,872/112,300 f1
  - codex 226,198/229,953 f1

## What is left (miss.py/cls.py re-run on the tip, copies in /scratch/agent/tmp/w2r/inv)

| class | bevy | pm |
|---|---|---|
| B3 | 1,183 | 99 |
| B4 | 761 | 509 |
| B5 | 243 | 207 |
| D1 | 358 | 0 |
| D2 | 22 | 0 |
| D3 | 374 | 98 |

- **B5.** A module-level const/static receiver is typed only for associated consts, variants and unit structs. Free `const`/`static` items are not in the Rust registry: Variable defs reach the cross defs but the registrar drops them. Next step: register the declared type of Variable defs and read it in rust_eval_value_path_type. Repro: /scratch/agent/tmp/w2r/b5.
- **B4 on pm.** The bulk is trait_receiver_shape 218 and trait_scope_identity 166: trait methods on known types (CanonicalProjectStore<A> facade traits). This overlaps w2-trait.
- **D1 rest.**
  - Extract and Single targets are associated-type projections.
  - Local over `#[derive(Deref)]` is a proc-macro target the resolver refuses by design.
  - The remaining Res/ResMut sites are mostly chained receivers (B2-shaped) or trait methods.
- **B3 rest.** 815 of the original unknown_receiver_type stops have bases typed by patterns, closures or previous calls (B1/B2, w2-std).
- **Bevy TypeIdHashMap.** Cross-crate alias field receivers still stop at method_not_found, about 60 dump rows.
- **Review item 5.** The wave 1 path-scope lookup does not follow `use` globs (bevy binds `change_detection_impl` through `use crate::{change_detection::{traits::*, ..}}`), so it would withhold every bevy site. The narrower rule in 8da96c7d is applied instead. Resolving globs in rust_path_scoped_macro_text would let the full rule replace it.
- **Incremental re-resolve.** It may call cbm_pxc_collect_all_defs without the macro table, so macro-emitted Derefs are absent there.

## Sites left for w2-std
In the remaining B3/B4/B5/D classes, sites whose source line goes through `.unwrap()`, `.expect(`, `Some(`, `.ok()`, `.iter()` or `.get(` were about bevy 533 and pm 254 at a8460dd1. This is a heuristic upper bound. B1 (bevy 2,378) and B2 (3,375) were not touched.
