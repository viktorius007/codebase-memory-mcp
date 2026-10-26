STATUS: DONE
COMMIT: ee5d3880
CHANGED: internal/cbm/lsp/rust_lsp.c, internal/cbm/lsp/rust_modtree.c, internal/cbm/lsp/rust_modtree.h, tests/fixtures/graph/rust/{macglob (new), macasm (new), maclit}
CLOSED: M2 remainder for glob-reached corpus macros (pm `use super::*`, dependency paths beside globs; bevy `children!` via `bevy::prelude::*`); pm `invoke!`; ty/path fragment before `[`; review finding (1) on 1da9f1c9 (lifetime / arrow); M1 measured: no macro step fails, no rule written
OPEN: 7 (below; review finding (2) is OPEN 7)
BLOCKERS: none
SCORES: bevy 70246/84295 f35; pm 44864/47116 f73; codex 226198/229953 f1; ruff 104406/112300 f1
---
# Round 1 (on main 27586789; superseded hashes, see Round 2 for the rebased tip and final scores)

Against current main: bevy +731 (63,114 -> 63,845), pm +2,272 (42,237 -> 44,509), ruff +534 (103,794 -> 104,328), codex +0. Flagged unchanged on all four (35 / 73 / 1 / 1). CALLS removed against main: 0 on all four corpora.

Baseline for edge diffs: main's scores are the gate3 run of 040a9e27 (/scratch/agent/gate3/gate/score-*), which give exactly the main figures in the brief. No separate scoring of main's binary was needed.

## Commits (in order)

### 1. ac2a2ff5 feat: a macro_rules! reached through glob imports resolves through the crate module tree
- Family: M2 remainder (glob import chains).
- Rule: when no textual-scope definition and no source `use` names the macro, the invocation path is resolved in the macro namespace of the crate module tree (rust_modtree). The tree already follows `use` items, visibility and glob chains across crates.
  - `#[macro_export] macro_rules!` is now entered as a crate-root item, through the new fact RMF_MACRO_EXPORT.
  - New query: cbm_rust_modtree_resolve_macro. A glob whose source the tree cannot read leaves the name open; an open or ambiguous name withholds.
  - `$crate` reads as `crate` inside the defining crate, or as the invoking crate's dependency key for it.
  - Otherwise `$crate` becomes an internal head `__cbm_crate_<hex root QN>`, which only a nested macro path reads back. Bevy examples need this: they reach bevy_ecs's `children!` -> `$crate::related!` -> `$crate::recursive_spawn!` without depending on bevy_ecs.
- Assumption: when one glob supplies the macro, an unreadable glob supplies nothing different (two different items would be E0659 at the use).
- Observation: pm crates/pm-dependency-graph/src/dag/tests.rs:23 `check!(g.is_acyclic(), ..)` (child `use super::*`) now matches truth dag/impl#[DependencyGraph]is_acyclic().
- Credited: pm 42,237 -> 44,219 (flagged 73, CALLS removed 0). Bevy is unchanged on this commit alone, because `related!` did not match until commit 2.

### 2. 1da9f1c9 fix: a macro_rules! `ty` or `path` fragment ends before a following `[` or `{`
- Rule: a `ty`/`path` fragment keeps `,` `=` `;` inside `<..>`, and stops at a top-level `[` or `{` once a complete type has been consumed. A complete type ends in an identifier other than mut/const/dyn/impl, or in `>`, `)` or `]`. The follow set of ty/path admits both brackets.
- Before this commit the fragment swallowed `Children [..]`, so bevy's `related!` never matched.
- Observation: bevy examples/testbed/ui.rs:252 `width: vmin(20.)` inside `children![..]` now matches truth geometry/vmin().
- Credited: bevy 63,114 -> 63,845 (flagged 35, CALLS removed 0).

### 3. 56902da2 fix: a macro path's head names the extern crate beside a glob the tree cannot read
- Rule: the first segment of a multi-segment macro path gets the treatment the tree already gives a `use` path's head. When only an unreadable glob or item macro keeps the name open, and the name is a dependency key, it names that dependency.
- Assumption: rustc resolves macro paths during expansion and rejects glob-vs-outer-scope ambiguity there (E0659). So in a compiling program, the unreadable glob does not supply the head.
- Observation: pm crates/pm-cli/tests/step_session_identity.rs:123 `allocated_to(&dir, &step)` inside `pm_check::check_eq!` (beside `use predicates::prelude::*`) now matches truth allocated_to().
- Credited: pm 44,219 -> 44,381 (flagged 73, CALLS removed 0).

### 4. 68461900 fix: a method call a transcriber assembles from captured receiver and name is the call site's
- Rule: in an expansion, a method call whose node spans transcriber tokens is sited at the call site (receiver through method name) when its receiver and its method name each map to a captured fragment, in source order.
- Assumption: the transcriber's `.` and parentheses do not change which method is called. The receiver and the name resolve in call-site scope, and the existing hygiene check guards against transcriber binders.
- Observation: pm .../replay_contract/intent_sequence.rs:55 `invoke!(seen, "ReasonStore", decisions.submit_reason(reason.seq_id))` now matches truth ReasonStore#submit_reason().
- Credited: pm 44,381 -> 44,509 (flagged 73, CALLS removed 0).

Ruff's +534 and codex's +0 were measured on the tip only, not per commit. Ruff's added CALLS are in ruff_python_formatter callers reaching ruff_formatter builders (token, space, hard_line_break) through macro arguments of glob-imported `ruff_formatter` macros.

## Removed pairs against main
- CALLS: 0 removed on bevy, pm, ruff and codex.
- IMPORTS (not scored):
  - codex: +293 added, 0 removed. `use` of an exported macro (`skip_if_no_network` etc.) now also links to the macro's node.
  - bevy: 62 removed, 61 added. Same cause: a `use` of an exported macro now links to the macro's node, e.g. benches.src.lib -> benches.src.lib.bench and bevy_shader lib -> lib.load_shader_library.
  - bevy, one net loss: `use bevy_utils::once` names both a module and a macro. Its edge moved from bevy_utils.src.once (11 edges) to bevy_utils.src.lib (10 edges). See OPEN 5.
- SEMANTICALLY_RELATED (ruff 9/21) and CALL_REFERENCE (pm +9) drift with the new call edges.

## Checks on the tip 68461900
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w2-macro -j4` rc=0, 0 warnings. Also rebuilt after touching both edited files: 0 warnings.
- Fixtures: `make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/w2-macro`: `fixtures: 56 fixtures x 2 modes, 93 known mismatches, 0 failures`.
  - macglob (new): 5/5 right. On main's binary: 0 right, 5 withheld.
  - macasm (new): 2/2 right. On the pre-commit-4 binary: 1 right, 1 withheld.
  - maclit: 8 right, 4 withheld. The new type_then_bracket site was withheld before commit 2.
  - macpath: 7/7.
- Suites: `scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract" BUILD_DIR=build/t-w2-macro`: `1962 passed, 0 failed, 0 skipped (6 selected suites)`. pipeline (542 pass) and edge_structural (39 pass) exit rc=1 with 0 failed assertions: the known LeakSanitizer exits (two "LeakSanitizer: detected memory leaks" reports; the UBSan runtime-error lines are in objectscript_udl/doclink_perl, not this lane's code). The log is /scratch/agent/tmp/w2m/suites2.log. A first run had died after its link step without a result. No rust_lsp unit test was added: the new behaviour is covered by the fixtures (macglob, macasm, maclit) only.
- Scores (scripts/rust-score.sh as gate.sh runs it, one corpus at a time; edge diff by tools/rust-bench/edgediff.py against main's score.db):
  - bevy `calls ALL 63845 / 84295`, flagged 35; edges removed=90 (IMPORTS 62 + others), CALLS removed=0 added=307
  - pm `calls ALL 44509 / 47116`, flagged 73; CALLS removed=0 added=1549
  - ruff `calls ALL 104328 / 112300`, flagged 1; CALLS removed=0 added=357
  - codex `calls ALL 226167 / 229953`, flagged 1; CALLS removed=0, IMPORTS added=293

Inventory classes on the tip (rs-inv scripts re-pointed at the tip's score dirs; heuristic classes):
- pm: M2 2,564 -> 422, M1 209 -> 97.
- bevy: M2 805 -> 165 (children 771 -> 155), M1 2,078 -> 1,966.

## OPEN
1. M1 bevy (1,966 sites). The std expression macro argument forms (assert*, format family, vec!, matches!, log/tracing) were already in place on main (rust_builtin_macro_arg_form). The remaining sites stop on receiver typing inside already-parsed arguments: unknown_receiver_type, method_not_found, trait_receiver_shape. That is w2-recv/w2-std territory. The few `macro_call_not_proven` sites sampled sit inside `bsn!` or plain tuples, which the inventory's 60-line macro scan misattributes. No rule was added here.
2. bevy `children!` remainder (155 sites). Not broken down. Candidates: lists longer than recursive_spawn's direct arms (its recursive arm), and invocations inside block-level globs. A glob in a block makes rust_modtree_macro_text withhold, as the existing modtree path call does.
3. Ruff's 328 hygiene withholdings: not reworked. Ruff is +534 above main from the glob-chain rule, so it no longer sits below the wave-1 base (104,067).
4. X0 (pm 253, items defined inside corpus macro bodies): not addressed.
5. IMPORTS for a `use` naming both a module and an exported macro (bevy_utils `once`): the edge target moved to the crate root. This is IMPORTS only, not CALLS.
6. A `$crate::f()` call written in a transcriber, expanded in a crate with no dependency key for the defining crate, stays unresolved. The internal crate mark heads only nested macro paths.

# Round 2 (on main 9cff5b79; 5 commits, rebased clean twice: c4a6b353, then 9cff5b79)

Tip ee5d3880 sits on main 9cff5b79. The rebased commits are 73813e8e, d3477cf2, 97e28a54, 48c7f0f4 and ee5d3880. The first four are the unchanged round-1 commits.

## 5. ee5d3880 fix: a reference lifetime or a function arrow does not complete a `ty` before `[`
- Fixes review finding (1), a CONFIRMED wrong link from 1da9f1c9.
- Rule: before a top-level `[` or `{`, a `ty`/`path` fragment is complete only when it ends in one of:
  - a path segment that is not a lifetime;
  - a closed generic list whose `>` is not the `>` of `->`;
  - a closed group.
- Fixture: maclit gains type_arm_taken with `choose_ref!(&'static [u8], dropped_ref())` and `choose_fn!(fn() -> [u8; 1], dropped_fn())`.
  - On 68461900: `MISMATCH CALLS type_arm_taken -> dropped_ref: PHANTOM` and `-> dropped_fn: PHANTOM`.
  - Now: 0 mismatch.

## M1: measured, no rule written
The failing step was measured on the resolve dump (CBM_RUST_RESOLVE_DUMP, tip before the rebase). Every M1 inventory site was joined to the dump's R rows on its line and callee name.
- bevy, 1,966 M1 sites:
  - 1,659 have an R row from the parsed macro argument, meaning the std-macro step worked. Their stops are lsp_unresolved:
    - unknown_receiver_type 1,216;
    - method_not_found 187;
    - function_not_in_registry 63;
    - index_output_not_proven 60;
    - trait_scope_identity 21;
    - operator_output_not_proven 12;
    - bound_declaration_not_proven 8.
  - 240 have no row on that line: `*x` deref operators and continuation lines of multi-line calls.
  - 67 resolve to another strategy (operator/prelude-trait/callable-value).
  - 141 sit inside unexpanded corpus `children!` lists (macro_call_not_proven).
  - Samples:
    - examples/reflection/auto_register_static/src/lib.rs:41 `registry.contains(..)` in info!: `registry = reg.read()` on `Res<AppTypeRegistry>`;
    - examples/window/window_settings.rs:129 `time.elapsed()` in format!: `Res<Time>`;
    - crates/bevy_platform/tests/aligned_vec.rs:179 `rebuilt.alignment()` in assert!: `rebuilt = unsafe { .. }`.
  - All of these are receiver typing, the same stop the same shapes get outside macros.
- pm, 97 M1 sites: unknown_receiver_type 40, field/value rows 23, no row 22, macro_call_not_proven 9, method_not_found 3.
  - The 9 macro_call_not_proven are `check!(matches!(from.transition(..), ..))`. Their receivers are bound by a `for` tuple pattern or by a previous call's result (B1/B2).
  - The same nesting with a free-function call resolves: checked on a minimal check!(matches!(..)) project with an external glob beside it. So the nesting is not the failing step.
- The brief's grammar decisions are already implemented on main by rust_builtin_macro_arg_form: prelude meaning unless a `use` binds the name, the matches! pattern skipped, and format family positional and `name = expr` arguments.
- The bevy `children!` remainder no longer fails at the macro either. Debug on examples/ui/widgets/standard_widgets.rs showed every `children!` -> `related!` -> `recursive_spawn!` matched and passed hygiene. The calls left there stop at function_not_in_registry for `observe` (bevy_ecs entity_command::observe through `bevy::ui_widgets`), which also misses outside macros (5 P2 sites).
- On the rebased tip the inventory gives bevy M1 1,892 and M2 101 (main after w2-recv/w2-trait lifted receivers).

## Final tip ee5d3880 against main 9cff5b79 (main's binary built and scored here, same scripts)
- Corrected main figure: pm on main 9cff5b79 scores 42,589.

| corpus | main | tip | gain | flagged main/tip | CALLS removed |
|---|---|---|---|---|---|
| bevy | 69,506 | 70,246 | +740 | 35 / 35 | 0 |
| pm | 42,589 | 44,864 | +2,275 | 73 / 73 | 0 |
| ruff | 103,872 | 104,406 | +534 | 1 / 1 | 0 |
| codex | 226,198 | 226,198 | 0 | 1 / 1 | 0 |

- Non-CALLS edge changes:
  - IMPORTS: bevy -60/+59; pm +363; codex +293. A `use` of an exported macro now also links to the macro node.
  - USAGE: bevy +353, ruff +98.
  - SEMANTICALLY_RELATED drifts.
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w2-macro` rc=0, 0 warnings.
  - The first build after the second rebase was incremental, and its binary aborted with "stack smashing detected" in cbm_parallel_resolve. That happened on every corpus and on parallel-mode fixtures.
  - main's own binary did not abort on the same input.
  - After `rm -rf` of the build dir, a clean rebuild (-j8 for time) runs clean. I conclude stale objects across the rebase; this is not a code defect. The scores and fixtures above come from the clean build.
- Fixtures: `fixtures: 64 fixtures x 2 modes, 94 known mismatches, 0 failures` (macglob 5/5, macasm 2/2, maclit 8 right + 4 withheld, macpath 7/7).
- Suites summary line: `1961 passed, 0 failed, 0 skipped (6 selected suites, 16 jobs)` (log /scratch/agent/tmp/w2m/suites4.log).
  - pipeline (541 pass) and edge_structural (39 pass) exit rc=1 with 0 failed assertions: the known LeakSanitizer exits.
  - The suites ran on the rebased tip ee5d3880 with the incremental build/t-w2-macro.

## OPEN, round 2 additions
7. Review finding (2): `macro_rules! m {..} pub(crate) use m;` in a sibling module, reached by `use crate::helpers::*`, stays withheld. Only `#[macro_export]` macros are tree items. Making a non-exported macro an item of its module would also let `use super::*` children and `self::m!` paths see it, which rustc does not allow, unless such an item is marked importable only by a `use` in its own module. That needs a separate flag and a text lookup for non-exported definitions, so I did not take it in this round.
- OPEN 1 (M1) is resolved as above: no macro-specific failing step remains. What is left is receiver typing and the `observe` path call.
