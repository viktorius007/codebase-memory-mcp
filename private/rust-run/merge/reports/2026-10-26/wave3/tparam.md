STATUS: DONE. Review round (four wrong-link shapes) fixed in c60745f2. Rebased on main b7b52bac. Earlier cut-off overrun is noted below.
COMMIT: c60745f2 (lane/w3-tparam, on main b7b52bac; commits e9f79404 and c60745f2)
CHANGED: internal/cbm/lsp/rust_lsp.c (projection substitution in call-return typing), internal/cbm/lsp/rust_modtree.c/.h (impl associated-type facts, digest, query), tests/fixtures/graph/rust/tproj (new), tests/fixtures/graph/rust/tprojneg (new), tools/fixtures/known.tsv (2 withheld rows)
CLOSED: method_not_found receiver heads F.Mut 237 -> 18, F.Ref 129 -> 48, F.DeferredMut 24 -> 0 (bevy B1/B2 truth sites). This covers the "inner-ok ret=Mut" (213) and "ret=Ref" (47) chain rows, whose inner callee returns F::Mut / F::Ref. The review shapes are closed in c60745f2: generic impl beside another trait's item, erased self-type arguments, Self in the value, generic associated type.
OPEN: D.Item 43: Query's D is bound to a reference (&mut T), and the impl `impl<T> QueryData for &mut T` is generic. This is the w3-query lane's family. Self.Item 11: DebugCheckedUnwrap for Option<T>/Result<T,U> is a generic impl on a std self. Impls with type parameters are not recorded at all. The remaining F.Ref 48 / F.Mut 18 sites have an argument whose own type is unknown (`event.event_target()`, loop variables). Projections need a bound P (argument or turbofish). The annotated-let source was not wired: a typed binding already fixes the receiver. Tuple-struct constructor calls (`Entity(1)`) are not typed as arguments (seen in the fixture draft; outside this lane). Not checked: a type with two trait impls naming the same associated type differently is refused as ambiguous; P's trait bound is not consulted, because registered fns carry no fn-parameter bounds. No unit test for the modtree digest or query was added: tests/ is outside the edit set (tester lane).
BLOCKERS: suites: pipeline reports `FAIL tests/test_pipeline.c:4007` (pipeline_rust_trait_return_known_generic_constructor, row `repeated -> pair_act` expected 1, got 0). This is not caused by this lane. I rebuilt that test's project and indexed it with main 6ab8c225's own binary (built in a side worktree): it gives the same `trait_receiver_partial` stop at that site. So the failure is on main as merged with fix-pairact.
SCORES: bevy 71127/84295 f35 (tip c60745f2 on b7b52bac). pm, codex and ruff were not rescored after the review fix. On the pre-fix tip 31053789 (on 6ab8c225) they were pm 44864/47116 f73; codex 226198/229953 f1; ruff 104459/112300 f1.
---
# w3-tparam: type parameters and projections in call returns

## Re-cut dump on main 9cff5b79
Binary: the w3-query build of pristine 9cff5b79, saved as /scratch/agent/tmp/w3tp/main-bin.
bevy score with CBM_RUST_RESOLVE_DUMP: 69,506 / 84,295, flagged 35. This matches the brief.
Files: dump /scratch/agent/tmp/w3tp/main-dump.tsv, score dir /scratch/agent/tmp/w3tp/main-bevy.
Grouped table (/scratch/agent/tmp/w2std2/group.py over /scratch/agent/tmp/rs-inv/bevy.cls.tsv):
/scratch/agent/tmp/w3tp/main-grouped.txt. Its top rows:
```
308 | B2 | unknown_receiver_type | call | inner-unresolved:unknown_receiver_type
213 | B2 | method_not_found | call | inner-ok ret=Mut        (inner callee returns F::Mut<'_>)
152 | B2 | unknown_receiver_type | let | call | unres:unknown_receiver_type
110 | B2 | unknown_receiver_type | call | inner-unresolved:method_not_found
 87 | B2 | unknown_receiver_type | call | inner-unresolved:function_not_in_registry
 47 | B2 | method_not_found | call | inner-ok ret=Ref        (F::Ref<'_>)
 47 | B2 | method_not_found | let | call | ok:method_dispatch ret=Mut
 42 | B1 | method_not_found | let-Ok | call | ok:method_dispatch ret=Result   (Result<F::Mut<'_>,_>)
```
First failing step by receiver head: method_not_found rows at B1/B2 sites, last R row per site, script /scratch/agent/tmp/w3tp/scripts/heads.py.
Columns: main | after the commit (pre-rebase build, same base) | example.
```
F.Mut          237 -> 18   examples/no_std/library/src/lib.rs:132 .insert (world.get_entity_mut(e) -> Result<F::Mut<'_>,_>)
F.Ref          129 -> 48   examples/ecs/immutable_components.rs:78 .get (world.entity(e) -> F::Ref<'_>)
D.Item          43 -> 43   tests/window/desktop_request_redraw.rs:99 .rotate_x (Query<&mut T>::single_mut -> D::Item)
F.DeferredMut   24 -> 0    crates/bevy_ui_widgets/src/slider.rs:614 .get_mut
Self.Item       11 -> 11   crates/bevy_ecs/src/bundle/info.rs:366 .initialize (Option<T>::debug_checked_unwrap)
```
Substitution source, confirmed on the fixture and the corpus. F is a fn type parameter declared `entities: F`. The existing argument inference already bound F to the argument's type (e.g. Entity). The return type `F::Mut<'_>` was held as the unsubstituted head `F.Mut`, because inference and substitution only knew bare parameters. The turbofish source (`spawn_one::<Entity>()`) failed at the same step. No bevy site in these families needed a source other than an argument or a turbofish.
Site stops after the commit (bevy, B1/B2): method_not_found 755 -> 427, lsp_method_dispatch 185 -> 535.

## Commit 31053789: projection rule
Rule: suppose a call fixes a type parameter P, by turbofish or by an argument whose declared type is P, &P or &mut P. Then a return type spelled `P::Name` (or `Self::Name` with the receiver type) is read as V, taken from `type Name = V;` in the project's `impl Trait for X`, where X is the project type bound to P.
How the module tree records it:
- One fact per (self QN, name): RMF_IMPL_ASSOC, recorded only for impls with no type or const parameters and a path self type.
- V is parsed in the impl's own module.
- Unknown cases: two different values for one key (ambiguous), a reference, std or parameter binding, and no impl.
- The file digest covers the impl header and its `type` items, so editing an associated type re-resolves dependants.
Observation: bevy examples/ecs/immutable_components.rs:78 `world.entity(entity).get::<Name>()` now links
impl#[`EntityRef<'w>`]get() at crates/bevy_ecs/src/world/entity_access/entity_ref.rs:122, which is the truth row.
Gain (pre-rebase, on 9cff5b79): bevy 69,506 -> 70,353 (+847), flagged 35 -> 35.

## Removed pairs
Lane in isolation (bevy, 9cff5b79 vs 9cff5b79 + commit; tools/rust-bench/edgediff.py):
- CALLS removed 0, added 497.
- Other removals: SEMANTICALLY_RELATED 37 removed / 34 added (similarity churn). TESTS +374, USAGE +176, WRITES +2 added.
Final tip against main 9cff5b79's score dbs (/tmp/w2t-main-<corpus>):
- These diffs also contain the w2-macro and fix-pairact merges, so they do not isolate this lane.
- CALLS removed: bevy 281, pm 2, codex 0, ruff 0. Lists in /scratch/agent/tmp/w3tp/edges-<corpus>/removed.tsv.
- The lane's own bevy diff above removes none.

## Scores (final tip 31053789, scripts/rust-score.sh as gate.sh runs it, /scratch/agent/tmp/w3tp/f-<corpus>)
Each line: final tip, then main 9cff5b79 (from the brief), then main 605f06a2 (coordinator's figures).
- bevy: 71,093 / 84,295 f35. 9cff5b79: 69,506 f35. 605f06a2: 70,246. Lane gain +847.
- pm: 44,864 / 47,116 f73. 9cff5b79: 42,589. 605f06a2: 44,864. Lane gain 0.
- codex: 226,198 / 229,953 f1. 9cff5b79: 226,198 f1. Lane gain 0.
- ruff: 104,459 / 112,300 f1. 9cff5b79: 103,872 f1. w2-macro reported +534.
Flagged did not rise on any corpus.

## Verification
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w3-tparam -j4`, -Werror, EXIT 0 on the final tip.
- Fixtures: 65 fixtures x 2 modes, 94 known mismatches, 0 failures (log /scratch/agent/tmp/w3tp/fixtures.log).
  tproj: five sites were FALSE-STOP method_not_found before: argument, let binding, Result/if-let, turbofish, F::Ref. All five are now right.
- Suites: `scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract" BUILD_DIR=build/t-w3-tparam`. Log /scratch/agent/tmp/w3tp/suites.log.
  - Result: EXIT=1, 1961 passed, 0 failed (runner count).
  - edge_structural rc=1 is LeakSanitizer only.
  - pipeline rc=1: LeakSanitizer, plus the main-side `test_pipeline.c:4007` assertion described under BLOCKERS.

## Review round: c60745f2 (rebased with e9f79404 onto main b7b52bac)
Four shapes confirmed by the coordinator, all fixed in one commit:
1. A generic impl of the bounding trait beside a concrete impl of another trait with the same associated name.
   - Cause: the generic impl was skipped, so the concrete one answered.
   - Fix: generic impls are recorded unreadable, and any unreadable record of the name on that self type withholds.
   - Selecting by bounding trait is not implemented, because registered fns carry no fn-parameter bounds (OPEN).
2. Self-type arguments were erased.
   - Fix: the record keeps the self type as written. A self type written with arguments must equal the bound type exactly (cbm_type_equal). One written without arguments takes only a bound type without arguments. Two fitting impls with different values withhold.
3. `Self` in the value was read as the caller's receiver.
   - Fix: `Self` is now the impl's self type. A value spelling `Self::` withholds.
4. Generic associated types.
   - Fix: a `type X<T>` with a type parameter is recorded unreadable, and a projection written with type arguments withholds.
Fixture tprojneg, seven sites:
- On 31053789: shape_generic_impl -> Wrong::hit and shape_gat -> T::hit were WRONG.
- Now: both are withheld, listed in known.tsv as M3. shape_self_args and shape_self_in_value are right (Right::hit).
- tproj stays right on all five sites.
Verification on c60745f2:
- Build: -Werror, EXIT 0.
- Fixtures: 68 x 2 modes, 96 known mismatches, 0 failures.
- rust_lsp suite: 836 passed, 0 failed, EXIT 0.
- bevy score: 71,127 / 84,295, flagged 35. The floor from the pre-fix tip was 71,093.
- Edge diff against the pre-fix tip's bevy db (/scratch/agent/tmp/w3tp/f-bevy, on 6ab8c225): CALLS removed 0, added 33. The added pairs include w3-locals' effects. SEMANTICALLY_RELATED: 3 removed, 13 added.
- So the review fixes withheld no bevy CALLS pair.
Not rerun on c60745f2: the full six-suite run (last run on 31053789: 1961 passed, 0 failed, plus the main-side test_pipeline.c:4007 assertion) and the pm, codex and ruff scores.
