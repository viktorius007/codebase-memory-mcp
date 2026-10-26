STATUS: DONE (round 2)
COMMIT: 3460b3d8 (lane/w2-trait, rebased onto main c4a6b353)
CHANGED: internal/cbm/lsp/rust_lsp.c, rust_modtree.c/.h, rust_cargo.c/.h, src/pipeline/pass_lsp_cross.c (pxc_rust_* only); fixtures tests/fixtures/graph/rust/{fnitemtrait,traitpathdecl,cfgfeature}
CLOSED: the review findings on 8dfffc6d (P3 feature model a-g), ed8067d7 (open-self fall-through), f61877c7 (tuple result typing); OPEN item 1, T1 chained `.in_set(..).after(..)` on ScheduleConfigs<T>
OPEN: (2) `run_if` with IntoScheduleConfigs and ObserverSystemExt both in scope (176 bevy sites; not started, cut-off); (3) bevy `Type::default`/`Type::from` with one corpus impl (not started; on round 1 the stop was type_path_not_proven, a receiver-identity step); (4) P3 cfg-false `mod` unmounting, see the note in the body; the P3 feature model has no harness fixture for true arms, because fixtures run plain rustc with no --cfg feature
BLOCKERS: none
SCORES: bevy 69506/84295 f35; pm 42589/47116 f73; codex 226198/229953 f1; ruff 103872/112300 f1
---
## Baseline and totals
Main c4a6b353, scored here with its own binary: bevy 67,813 f35; pm 42,364 f73; ruff 103,872 f1; codex 226,198 f1.

| corpus | final tip | main | change |
|---|---|---|---|
| bevy | 69,506 (82.5 percent) | 67,813 | +1,693 |
| pm | 42,589 (90.4 percent) | 42,364 | +225 |
| ruff | 103,872 | 103,872 | identical edge set |
| codex | 226,198 | 226,198 | identical edge set |

Flagged equals main on all four.

## Branch, oldest first
Round 1, kept unchanged:
- f61877c7 T1: fn-item and tuple receiver to the sole in-scope trait declaration
- 1c7dc7b6 P1: `Trait::m(x)` with a trait-object or blanket-parameter self
- 8dfffc6d P3: default features decide cfg(feature)
- ed8067d7 P1: a blanket impl defining m withholds

Round 2:
- **2848a393 P3 review.** The Cargo feature model is rebuilt as a fixpoint:
  - (e) `default` is enabled only when the package declares it;
  - `dep:x` activates x;
  - (b) `x/f` activates x and its implicit feature only when Cargo defines one (an optional dependency with no `dep:x` suppressing it), and enables f in x;
  - (c) weak `x?/f` takes effect only when x is active;
  - a bare name enables a declared or implicit feature, and no other name is invented;
  - (d) an optional dependency's `features = [..]` apply only while it is active;
  - (f) build dependencies are not merged (resolver 2);
  - (a) single-quoted lists are read;
  - (g) a set that does not settle in 64 passes leaves every feature predicate unknown.

  bevy is unchanged at 64,454 f35 against 8dfffc6d.
- **de4269f1 P1 review.**
  - Change: an open-self `Trait::m(x)` whose declaration is refused stops with bound_declaration_not_proven and never falls through to the sole-impl fallback.
  - Fixture: traitpathdecl gains `via_child` (`impl<T: Marker> Parent for T`, `&dyn Child`). It is withheld. The harness credits the blanket impl the run executes, so this case does not discriminate.
- **ee7b3f6b T1 review.**
  - Change: result typing admits a tuple receiver.
  - Fixture: fnitemtrait gains `tupled_chain` (`(sys, other).configure().finish()`), now right. Before this commit: right=3 withheld=4. After: right=4 withheld=3.
- **84d10f3a T1 chain (OPEN item 1).**
  - Failing step, from CBM_RUST_RESOLVE_DUMP at bevy_ui_widgets/src/text_input.rs, occurrence 29587-29713:
    - the first `.in_set` resolves (lsp_bound_dispatch to IntoScheduleConfigs.in_set);
    - the second `.in_set`, on `ScheduleConfigs<T>` with T carried out of the declaration's return type, stops with trait_receiver_partial, because the bound `T: Schedulable<..>` of `impl<T> IntoScheduleConfigs<T, ()> for ScheduleConfigs<T>` is unproved for an open T.
  - Truth names that impl: `impl#[ScheduleConfigs<T>][IntoScheduleConfigs<T, ()>]in_set()`.
  - Rule: the receiver `H<..>` has an argument the file cannot name, and all of these hold:
    - H has no inherent m and no Deref target;
    - m is not a std-trait name;
    - exactly one in-scope corpus trait declares m;
    - every m registered on H belongs to that trait, in one impl.

    The call then links that impl, and the impl types the result (`Self` is the receiver), so the chain continues. A tuple-expression receiver is excluded, because `(x,)` evaluates as `x`. That exclusion fixed a flag at bevy_sprite/src/lib.rs:86.
  - Assumption: with one candidate impl, a compiling call meets its bounds.
  - Credit: bevy 64,454 to 64,811 (pre-rebase), f35.
  - Fixture: fnitemtrait `stepped` (`sys.step().step()`) is right. Before this commit: right=5 withheld=4. After: right=6 withheld=3.
- **3460b3d8 T1 chain follow-up.**
  - Problem: after the rebase, ruff builders.rs:1211 `f.snapshot()` on `&mut Formatter<Context>` linked the Formatter impl, while truth names `impl#[&mut W][Buffer]snapshot()`. That raised ruff flagged from 1 to 2.
  - Change: the rule now also withholds when the trait has a reference impl (core.reference) or a blanket or tuple impl defining m.
  - Result: ruff f1; bevy 69,510 to 69,506.

## Removed CALLS against main c4a6b353 (edgediff of main's and the tip's score.db)
- **ruff 0, codex 0.**
- **pm 2.** Callers of pm-sqlite-helpers `sql_trace::install` and `sql_trace::report_json`. `pub mod sql_trace` is `#[cfg(feature = "hotpath")]`, and hotpath is not enabled by default anywhere. Truth has 0 sql_trace rows.
- **bevy 342.** All 342 removed edges have callers inside declarations a default-feature build does not compile, which the P3 rule drops:
  - bevy_pbr meshlet: 187
  - bevy_anti_alias dlss: 40
  - bevy_camera_controller pan_camera: 22
  - bevy_dev_tools ci_testing: 21
  - bevy_ecs `bevy_debug_stepping`: 13
  - testbed helpers, reflect derive docs, render diagnostic, ui experimental, hotpatch, asset io, clipboard, and similar: the rest

  296 sit in files with no truth row. 46 sit in cfg-disabled modules or items of files truth covers, for example the stepping tests and `assert_executor_supports_stepping`. bevy flagged stays at 35, so none of them was a credited truth pair. The tip still gains +1,693 on bevy.
- **Correction to round 1.** The round-1 header gave the removed count against main 27586789 as 0 CALLS. That was wrong: it was bevy 281 and pm 2, as the round-1 body and the P3 commit message (295 against its parent) said. The class is the same as above.

## Note on OPEN item 4 (P3 `mod` unmounting)
A cfg-false `mod x;` is skipped only as a declaration. A file that another declaration reaches is still mounted through that declaration, whether the other is cfg-true, unknown, or a `#[path]` alias. So a file is unmounted only when no live declaration references it, which is the behaviour asked for. I did not add a separate fixture for it.

## Checks on the final tip 3460b3d8
- **Build.** `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w2-trait -j4` builds warning-free under -Werror. Pipeline objects were touched first, because header dependencies do not rebuild them (see round 1).
- **Fixtures.** `make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/w2-trait`: 62 fixtures x 2 modes, 94 known mismatches, 0 failures.
- **Suites.** `scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract" BUILD_DIR=build/t-w2-trait` gave `1961 passed, 0 failed, 0 skipped`: rust_lsp 836, call_reference_contract 15, lang_contract 43, extraction 487, pipeline 541, edge_structural 39. pipeline and edge_structural exit rc=1 on LeakSanitizer reports only, with 0 failed assertions (the known condition). The pipeline count is 541 here against 542 in round 1; I have not checked whether main c4a6b353 accounts for the difference. Log: /scratch/agent/tmp/w2t/suites3.log.

## Regression fix (lane/fix-pairact, worktree /scratch/agent/wt/fix-pairact)
- **Commit.** ea5d87c7 on main 605f06a2. Main had not moved, so the rebase was a no-op.
- **Cause.** On main 605f06a2 the gate failed tests/test_pipeline.c:4003, pipeline_rust_trait_return_known_generic_constructor, row `repeated` -> pair_act (expected 0, got 1). The open-argument sole-impl rule (84d10f3a) linked `impl<T> PairAct for Pair<T, T>` without checking the impl's repeated parameter.
- **Fix.** The new rust_open_arg_impl_self_fits requires every argument of the impl's self type to be one of these:
  - an impl parameter standing once;
  - a repeated parameter, where the receiver's arguments at those positions are known and equal;
  - a concrete argument, or a parameter nested in another type, where the receiver's argument is known and equal to it.

  Otherwise the call is withheld.
- **Test.**
  - The `repeated` row (MakePair<u8, u8>) now expects 1, because Rust applies the impl for equal known arguments.
  - A new sibling, `repeated_mixed<T: MakePair<u8, u16>>`, expects 0.
  - No unresolved_sites check was added for the sibling.
  - I did not run the new row against main's binary, so it is not shown to fail without the fix.
- **Build.** BUILD_DIR=/scratch/agent/build/fix-pairact builds warning-free.
- **Suites.** `scripts/test.sh --suites "pipeline rust_lsp" BUILD_DIR=build/t-fix` gave 1377 passed, 0 failed, 0 skipped (rust_lsp 836, pipeline 541). pipeline exits rc=1 on LeakSanitizer only.
- **Score.** bevy 70,246/84295, flagged 35 (equal to the floor of 70,246, flagged 35).
