STATUS: DONE
COMMIT: 430eac49 (lane/w7-deref, on main 6406679d)
CHANGED: internal/cbm/lsp/rust_lsp.c (three commits), tests/fixtures/graph/rust/sysinput (new), tests/fixtures/graph/rust/refdefault (new)
CLOSED (bevy uncredited sites, main 38074ddf -> lane, by file:line against the w4 group file): G020 162 -> 22; G012 World head 57 -> 0 (all run_system_once; site-level recount after the third commit not run; its +100 credited matches the head).
OPEN:
- G020 residue (22): `scene.spawn()` SpawnListSystem impl on F (8), pipe on non-block receivers (5), `get`/`id` on EntityRef (5; not fn items, the inventory's binding scanner put them here), `add.into_function()` (3), write_buffer (1).
- G012 other heads: Result 39 (payload projection), A 32 (trait impls with generic arguments), Box 25; and G025 Ref 17 / PartialReflect 11. Time went to the World head (57) and G020 instead. G025's Ref sites are mostly `world.entity(x)` returning `F::Ref<'_>` with x untyped (binding lane, see w5 report).
- The G020 rule is bevy-bounded: it names the two traits IntoScheduleConfigs and ObserverSystemExt and the observer inputs On/Trigger. Any other pair of declaring traits still withholds.
- Housekeeping: a mistyped BUILD_DIR first pointed at /scratch/agent/build/w7-main, another lane's binary file. make failed before writing and the binary is untouched (mtime 20:13), but /scratch/agent/build/w7-main.log was overwritten with my 178-byte error log.
BLOCKERS: none
SCORES: bevy 76009/84295 f9; pm 45237/47116 f87; codex 226453/229953 f1; ruff 104704/112300 f1
---
Main 6406679d, scored here with a build of that commit: bevy 75,654 f9, pm 45,237 f87, codex 226,453 f1, ruff 104,704 f1. On the lane tip bevy rises by 355 to 76,009 (90.2 percent, target 75,866 met) and flagged is unchanged on all four. CALLS removed against main: 0 on all four; bevy adds 206 CALLS; pm, ruff and codex have identical edge sets.

## 1a4c0844 (563f9491 before the rebase): a fn item's first parameter decides between IntoScheduleConfigs and ObserverSystemExt
- Truth at the 86 G020 run_if sites: 80 credit IntoScheduleConfigs#run_if and 6 credit ObserverSystemExt#run_if. The receiver fn's first parameter splits them exactly: On<..> at all 6 observer sites; another parameter (77) or none (3) at the schedule sites.
- Rule: for a fn-item receiver whose method is declared by exactly those two in-scope corpus traits, a first parameter headed On or Trigger selects ObserverSystemExt, and any other parameter or none selects IntoScheduleConfigs. The fn signature must be registered; any other trait set withholds.
- Corpus: examples/asset/alter_mesh.rs:18 -> IntoScheduleConfigs#run_if; examples/state/computed_states.rs:195 -> ObserverSystemExt#run_if.
- bevy 75,604 -> 75,721 f9 (on 38074ddf), CALLS removed 0. Fixture sysinput: main withholds both sites, the lane gets right=2.

## 4e7ee667 (f8ea4032): a function item declared in a block is a fn-item receiver too
- Dump: `is_true_inc.xor` and `pipe_out.pipe` unknown_receiver_type. The receivers are fns declared inside test bodies, which the scope binds as callables, so they were taken for values.
- Rule: the name's callable target must be the block's own fn item (rust_is_block_fn_item); cfg-dependent identities and aliases stay values.
- Corpus: crates/bevy_ecs/src/schedule/condition.rs:1876 `is_true_inc.xor(is_false_inc)` -> SystemCondition#xor.
- bevy 75,721 -> 75,859 f9, CALLS removed 0. G020 162 -> 22. Fixture sysinput: block-local run_if cases added, right=4.

## 351ceeb3 (47ed21c3): a trait default reached through autoref from an impl for &Type or &mut Type
- Dump: `...World.run_system_once method_not_found`. `impl RunSystemOnce for &mut World` defines only run_system_once_with, and that relation lives on core.reference, not on World.
- Rule: with no inherent method, impl method or own-relation default, a corpus trait declaring the method with a default and implemented for `&Type`/`&mut Type` without defining it supplies the default. The impl is seen through a method it defines, and that method's written reference self ranks the autoref in the usual probe, the shared impl preferred. Two such traits withhold.
- Corpus: crates/bevy_ecs/src/system/builder.rs:993 `world.run_system_once(builder_system)` -> RunSystemOnce#run_system_once (dump lsp_trait_dispatch).
- bevy 75,859 -> 75,959 f9, CALLS removed 0, index time unchanged (45 s). Fixture refdefault: main has 2 FALSE-STOP method_not_found, the lane gets right=2 (`&mut` only, and `&` plus `&mut`).

## Checks on the final tip 351ceeb3
- Build: rc 0, no warnings.
- rust_lsp: 840 passed, 0 failed.
- Fixtures (`FIXTURES_WORK=/scratch/agent/tmp/w7deref/fxwork make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/w7-deref`): 102 x 2 modes, 98 known mismatches, 0 failures.
- Scores were run one at a time, all scratch under /scratch/agent.

## Review round: tip 430eac49 (on main 6406679d)
- 430eac49 fixes both confirmed items on 351ceeb3. (1) The probe now ranks the requested default itself: its own receiver form at the impl's reference self type, never a witness method. (2) When the probe picks another trait's reference override, the route returns that pick instead of NULL. The note on 4e7ee667 (a block-local `let f = f;` alias passes the fn-item check, with the same identity and target) is stated in the commit body.
- Fixture refdefault adds the reviewer's Touch/Look case: the previous tip links Touch::touch (WRONG); 430eac49 gets right=3. sysinput right=4.
- rust_lsp: 840 passed. bevy 76,009/84,295 f9, no CALLS removed against main.
