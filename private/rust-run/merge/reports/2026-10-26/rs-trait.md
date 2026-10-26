STATUS: DONE
COMMIT: 7f7f9db1
CHANGED: 5 files
CLOSED: none on the corpora. B4/B5 trait half: the rule is correct under the round-2 ruling, but it credits 0 corpus sites.
OPEN: 4 (T1 not committed; P1; B5 receiver typing; B4 bounds-only promotion where m is declared by more than one corpus trait, e.g. pm XStore and XStoreAdapter)
BLOCKERS: none. Decision for the coordinator: lane/rs-trait has no open wrong link, but it also gains no credit, so merging is optional (see "Round 2").
SCORES: pm 39983/47116 flagged 73 (base 39983/47116 flagged 73); bevy 61368/84295 flagged 35 (base 61368/84295 flagged 35 per base score.log); codex 226161/229953 flagged 1 (base 226161/229953 flagged 1); ruff 104067/112300 flagged 1 (base 104067/112300 flagged 1)
---
## Round 2 (final): commit 7f7f9db1
`fix(rust): the bounds-only trait impl rule requires that no competitor can exist, not that none is found`

### Branch state
lane/rs-trait = main ddc8c9b0 + cfeb2b0f + b0338b11 + 7f7f9db1. All three commits stay on the branch. 7f7f9db1 replaces b0338b11's competitor enumeration, so cfeb2b0f and b0338b11 must not merge without 7f7f9db1.

### The rule, as ruled
The new `rust_bounds_only_impl_has_competitor` guards the bounds-only promotion. It requires all of these:
1. Exactly one trait in the registry declares m: every trait declaration, supertraits included, in scope or not. It must be this impl's trait. A trait-impl method m of any other trait also withholds.
2. That trait has exactly one recorded impl in the whole corpus. The count covers every type's impl headers for the trait: every instantiation, S<..>, &S<..>, and blanket impls recorded on their parameter.
3. The head has no Deref relation (no recorded target, no Deref step, no Deref impl entry) and no inherent m.
4. The trait is a corpus trait. Std traits stay excluded.

The bounded assumption that remains: impls the registry does not record (proc-macro or derive output) and dependency traits are assumed absent.

### Fixture
tests/fixtures/graph/rust/facadeimpl/src/competitors.rs holds all five review shapes:
- another trait's &self default;
- a Deref target with a failing bound;
- an `impl ByRef for &S2<Owned>` by-value default;
- a blanket `impl<Z: Gate> Blanket for Z {}` default;
- a second instantiation `impl Keyed<u16> for S4<NoMarker> {}`.

Results:
- b0338b11's binary: 6 mismatches (WRONG/PHANTOM/SILENT).
- 7f7f9db1: `sites=7 right=2 withheld=5 mismatch=0` in both modes. The 2 right sites are the pm-shaped facade calls in main.rs.

### Credit that survives: zero on all four corpora
| corpus | calls ALL | flagged | base |
|---|---|---|---|
| pm | 39983/47116 | 73 | 39983 / 73 |
| bevy | 61368/84295 | 35 | 61368 / 35 |
| ruff | 104067/112300 | 1 | 104067 / 1 |
| codex | 226161/229953 | 1 | 226161 / 1 |

- **Why pm drops to base.** Every pm target method name is declared by two corpus traits. For example `create_reason` is declared by ReasonStore and also by the adapter trait (pm-fakes decision_store/reason.rs:14, canonical_store/adapter_forwarding.rs:107, canonical_store.rs:130 and others). Condition 1 therefore withholds every site. The 45 oracle-gap flags are gone too: pm flagged is back to 73.
- **Edge diff at 7f7f9db1** (/scratch/agent/score/rs-trait/c5): removed CALLS are pm 0, bevy 0, ruff 1, codex 7. Every one is also removed by main ddc8c9b0 alone, so 0 come from this lane. Added CALLS on pm and bevy: 0.
- **Suites at 7f7f9db1.** `1962 passed, 0 failed, 0 skipped`: rust_lsp 836/0, call_reference_contract 15/0, extraction 487/0, lang_contract 43/0, pipeline 542/0, edge_structural 39/0. pipeline and edge_structural exit rc=1 on LeakSanitizer only (2 leak reports, 0 failed assertions; the known pre-existing condition).
- **Fixtures at 7f7f9db1.** `fixtures: 52 fixtures x 2 modes, 93 known mismatches, 0 failures`.
- **Build.** rc=0, 0 warnings.

### Recommendation
The three commits fix no corpus site. Their lasting value is the fixture and a proof-safe hook. The pm credit needs a different proof: show that the competing traits (the XStoreAdapter traits) are not implemented for the receiver's head. That needs per-impl bound reading across files, which the registry does not hold today. Dropping all three commits instead loses nothing on the scores.

---
# History (rounds before the final one; superseded numbers kept for the record)
## Branch state (fix round)
lane/rs-trait = main ddc8c9b0 + cfeb2b0f (rule, unchanged) + b0338b11 (fix for both review findings, committed separately). Both wrong links are closed at b0338b11, so the two commits must merge together. Merging cfeb2b0f alone would bring back the wrong links.

## Commit b0338b11 (review fix)
`fix(rust): the bounds-only trait impl rule withholds when a Deref target or another trait's m can supply the call`
- **Finding 1.** Another trait the head implements declares m with a default body (`impl U for S<Owned> {}`), and Rust takes it at the earlier &self probe. Finding 2: an `impl<A> Deref for S<A>` whose target offers m, while the bound fails.
- **Rule.** The new `rust_bounds_only_impl_has_competitor`, called inside `rust_impl_self_differs_in_params_only`, guards both call sites (the probe choice, and the shape-refusal clearance at dispatch). The promotion withholds when either of these holds:
  - the head has a recorded Deref target or a Deref step;
  - any other trait among the head's recorded impls (embedded_types, impl headers parsed) declares m, with or without a body.
  The site then keeps its trait_receiver_shape stop.
- **Assumption.** Otherwise unchanged. Competitors the registry cannot see stay assumed absent: std or dependency traits, already excluded for std, and corpus blanket impls that are not recorded on the head.
- **Observation (harness).** tests/fixtures/graph/rust/facadeimpl/src/competitors.rs, called from main.rs, holds both shapes.
  - On the cfeb2b0f binary the graph linked `S.hit[Bounded]` and `W.reach[Viaderef]`, while the run reaches `Shared::hit` and `Inner::reach`. The harness did not flag them because the struct-literal variant was untyped; the constructor variant reproduces both.
  - At b0338b11 the harness reports `sites=4 right=2 withheld=2 mismatch=0` in both modes. The two right sites are the original facade calls.
- **Scores at b0338b11** (outputs in /scratch/agent/score/rs-trait/c4):
  - pm 40252 (+269 vs base), flagged 118. The 45 new flags are the accepted oracle gap, with the same list as below.
  - bevy 61403 (+35), flagged 35.
  - ruff 104074 (+7), flagged 1. It was 104131 at cfeb2b0f. The guard now withholds 57 sites that cfeb2b0f credited, mostly `Option<T>` impls of corpus traits such as Combine and OrDefault.
  - codex 226163 (+2), flagged 1.
- **Removed CALLS at b0338b11.** pm 0, bevy 0, ruff 1, codex 7. Every one of them is also removed by main ddc8c9b0 alone, so 0 come from this lane.
- **Suites at b0338b11.** `1962 passed, 0 failed, 0 skipped`: rust_lsp 836/0, call_reference_contract 15/0, extraction 487/0, lang_contract 43/0, pipeline 542/0, edge_structural 39/0. pipeline and edge_structural exit rc=1 on LeakSanitizer only, with 0 failed assertions (the known pre-existing condition).
- **Fixtures at b0338b11.** `fixtures: 52 fixtures x 2 modes, 93 known mismatches, 0 failures`.
- **Build.** rc=0, 0 warnings.

The sections below describe cfeb2b0f as first reported. The 45-pair list and the zero-removed-CALLS table still hold at b0338b11, because the pm numbers are identical.
# rs-trait lane report

Branch lane/rs-trait, worktree /scratch/agent/wt/rs-trait, rebased on main ddc8c9b0 (the rebase was clean). Tip cfeb2b0f, one commit.

## Commit cfeb2b0f
`fix(rust): a corpus trait's sole in-scope impl on a known generic receiver is dispatched when only its parameter bounds are unproved`

Files: internal/cbm/lsp/rust_lsp.c, plus a new fixture at tests/fixtures/graph/rust/facadeimpl/{Cargo.toml,PROVENANCE,src/main.rs}.

- **Class.** This is the trait half of B4/B5: pm `trait_receiver_shape`, for example the `CanonicalProjectStore<A>` methods of ReasonStore, GoalStore, ReqStore, IssueStore, TaskStore and StepStore, implemented as `impl<A: XAdapter> XStore for CanonicalProjectStore<A>` in port/canonical_store/{decision,execution}_facade.rs.
- **Root cause.** `rust_method_probe_rank` compared the written impl self `CanonicalProjectStore<A>` (A is a bounded impl parameter) with the receiver `CanonicalProjectStore<OwnedSqliteStore>` and got UNDECIDED. The same-file bound proof (`rust_method_probe_bounds_proven`) cannot read the bounds of an impl written in another file, so the site was withheld as `trait_receiver_shape`.
- **Proof rule.** This is the new `rust_impl_self_differs_in_params_only`, used in `rust_method_probe_choice` and at the method-dispatch site. All of these must hold:
  1. The trait is a corpus trait (in the project and an interface).
  2. The scoped trait-impl lookup returned exactly one impl of m for the receiver's type head (impl_count 1), with require_scope set.
  3. No inherent method and no reference-impl candidate competes, either proved or uncertain.
  4. The written impl self and the receiver are the same template head with the same number of arguments.
  5. They differ only at positions where the impl names one of its own type parameters. Each parameter appears at one position only, and every receiver argument is a proved type.
  6. Under these conditions the impl applies exactly when those parameters' bounds hold.
- **Bounded assumption.** The program compiles, so the bounds hold, and no std or dependency trait gives the corpus type a competing method named m. Std traits are excluded outright. An earlier variant also accepted std-trait impls such as `impl<D,F> Iterator for QueryIter`: it gained about 400 more credited sites on bevy, but added one unconfirmed pair (bevy_pbr/src/atmosphere/environment.rs:138 -> query/iter.rs:1051 `next`). So that variant was dropped.
- **Observation (fixture).** tests/fixtures/graph/rust/facadeimpl has `impl<A: ReasonStoreAdapter> ReasonStore for Canon<A>` in a child module, called on `Canon<Owned>` from main. On base main it was `withheld=2`; it is now `right=2 withheld=0 mismatch=0` in both sequential and parallel mode.
- **Observation (corpus).** pm xtask/tests/static_safety_policy.rs:537 `.create_reason(..)` now matches the truth row `port/canonical_store/decision_facade/impl#[CanonicalProjectStore<A>][ReasonStore]create_reason().` (decision_facade.rs:126). pm adds 268 CALLS edges.

## Scores (final tip, run one corpus at a time as gate.sh does; outputs in /scratch/agent/score/rs-trait/c3)
- pm: `calls ALL 40252 / 47116 85.4%` (base 39983, +269). Flagged 118 (base 73, +45). Edges: base=161337 new=161702 removed=61 added=426.
- bevy: `calls ALL 61405 / 84295 72.8%` (base 61368, +37). Flagged 35, equal to the base score.log value of 35 (the brief's 41 does not match base score.log). Edges: removed=28 added=57.
- ruff: `calls ALL 104131 / 112300 92.7%` (base 104067, +64). Flagged 1 (base 1). Edges: removed=94 added=25.
- codex: `calls ALL 226163 / 229953 98.4%` (base 226161, +2). Flagged 1 (base 1). Edges: removed=33 added=6.

### The 45 new pm flags (why the status is BLOCKED)
- All 45 pairs are WRONG TARGET lsp_trait_dispatch in crates/pm-projection/src/graph/query/tests.rs, across 19 test fns, for example :1146 `ready_steps_empty_while_task_dependencies_are_incomplete` -> execution_facade.rs:55 `add_task_dep`.
- Every one of the 19 callers builds its receiver as `CanonicalProjectStore::new(InMemory{Decision,Execution}Store::new())`.
- truth.tsv has `path_unresolved` for those `InMemory*::new()` paths (for example lines 1147 and 1148). rust-analyzer therefore left A unknown and credits the trait declaration instead: truth row :1154 is `port/execution_store/TaskStore#add_task_dep()` (execution_store.rs:75).
- The graph links the impl, which is the method the program executes.
- This is the same pm-fakes oracle gap as the 72 base `lsp_constructor` flags. The only way to clear these flags is to withhold correct links to match the oracle, which needs an owner ruling.

### Removed CALLS pairs (edgediff removed.tsv)
- pm: 0 CALLS removed. The 61 removed edges are 53 SEMANTICALLY_RELATED, 5 USAGE and 3 WRITES.
- bevy: 0 CALLS removed. All 28 removed edges are SEMANTICALLY_RELATED.
- ruff: 1 CALLS removed, a Python fixture edge (`pylint/global_statement.global_function_assign -> CONSTANT`).
- codex: 7 CALLS removed: cargo_bin from e2e_benches and wine_exec_server, plus exec-server testing main and remote_env_windows callees.
- None of these come from this commit. I scored main ddc8c9b0 (binary /scratch/agent/score/rs-trait/bin-main, outputs in cmain/): main alone removes the same 1 ruff and 7 codex CALLS pairs, and the tip removes no CALLS pair that main keeps.
- Main alone gives ruff 104067 and codex 226161, so ruff +64 and codex +2 are this commit's gain.

## Coordinator ruling and its conditions
The coordinator accepted the 45 pm flags as the same pm-fakes oracle gap as the 72 base lsp_constructor flags. The ruling rests on the compile argument stated in the rule and is a reversible decision. The two conditions follow.

### Condition 1: the 45 flagged pm pairs and the truth rows each would need
Each pair is listed with the call sites behind it. For every site, truth today names the trait declaration (third column). To credit the graph link, the truth would need a row at that site naming the impl method the graph links to (second column), as truth already does wherever rust-analyzer resolved the receiver's type argument (for example xtask/tests/static_safety_policy.rs:537).

| graph pair (caller -> callee) | call site | truth today names (decl file:line) |
|---|---|---|
| crates/pm-projection/src/graph/query/tests.rs:307 command_read_session_preserves_nonempty_dependency_reads -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:55 add_task_dep | crates/pm-projection/src/graph/query/tests.rs:313 | `port/execution_store/TaskStore#add_task_dep().` (crates/pm-port-store/src/port/execution_store.rs:75) |
| crates/pm-projection/src/graph/query/tests.rs:385 same_query_instance_refreshes_after_execution_mutation -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:55 add_task_dep | crates/pm-projection/src/graph/query/tests.rs:391 | `port/execution_store/TaskStore#add_task_dep().` (crates/pm-port-store/src/port/execution_store.rs:75) |
| crates/pm-projection/src/graph/query/tests.rs:385 same_query_instance_refreshes_after_execution_mutation -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:401 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:385 same_query_instance_refreshes_after_execution_mutation -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:404 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:428 public_graph_query_wrappers_delegate_to_fresh_projection -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:84 add_task_address | crates/pm-projection/src/graph/query/tests.rs:436 | `port/execution_store/TaskStore#add_task_address().` (crates/pm-port-store/src/port/execution_store.rs:95) |
| crates/pm-projection/src/graph/query/tests.rs:535 ready_steps_frozen_for_task_under_deprecated_adr -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:309 update_adr_status | crates/pm-projection/src/graph/query/tests.rs:542 | `port/decision_store/adr/AdrStore#update_adr_status().` (crates/pm-port-store/src/port/decision_store/adr.rs:54) |
| " | crates/pm-projection/src/graph/query/tests.rs:545 | `port/decision_store/adr/AdrStore#update_adr_status().` (crates/pm-port-store/src/port/decision_store/adr.rs:54) |
| " | crates/pm-projection/src/graph/query/tests.rs:548 | `port/decision_store/adr/AdrStore#update_adr_status().` (crates/pm-port-store/src/port/decision_store/adr.rs:54) |
| crates/pm-projection/src/graph/query/tests.rs:1028 relationship_frame_keeps_complete_issue_without_fix_commit_unresolved -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:84 add_task_address | crates/pm-projection/src/graph/query/tests.rs:1073 | `port/execution_store/TaskStore#add_task_address().` (crates/pm-port-store/src/port/execution_store.rs:95) |
| crates/pm-projection/src/graph/query/tests.rs:1028 relationship_frame_keeps_complete_issue_without_fix_commit_unresolved -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:1076 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:1028 relationship_frame_keeps_complete_issue_without_fix_commit_unresolved -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:1079 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:1146 ready_steps_empty_while_task_dependencies_are_incomplete -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:55 add_task_dep | crates/pm-projection/src/graph/query/tests.rs:1154 | `port/execution_store/TaskStore#add_task_dep().` (crates/pm-port-store/src/port/execution_store.rs:75) |
| crates/pm-projection/src/graph/query/tests.rs:1146 ready_steps_empty_while_task_dependencies_are_incomplete -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:1163 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:1146 ready_steps_empty_while_task_dependencies_are_incomplete -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:1166 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:1178 closed_completed_external_ref_collection_ignores_absent_open_and_non_completed_events -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:392 create_external_ref | crates/pm-projection/src/graph/query/tests.rs:1181 | `port/decision_store/issue/ExternalRefStore#create_external_ref().` (crates/pm-port-store/src/port/decision_store/issue.rs:105) |
| " | crates/pm-projection/src/graph/query/tests.rs:1187 | `port/decision_store/issue/ExternalRefStore#create_external_ref().` (crates/pm-port-store/src/port/decision_store/issue.rs:105) |
| " | crates/pm-projection/src/graph/query/tests.rs:1193 | `port/decision_store/issue/ExternalRefStore#create_external_ref().` (crates/pm-port-store/src/port/decision_store/issue.rs:105) |
| " | crates/pm-projection/src/graph/query/tests.rs:1199 | `port/decision_store/issue/ExternalRefStore#create_external_ref().` (crates/pm-port-store/src/port/decision_store/issue.rs:105) |
| crates/pm-projection/src/graph/query/tests.rs:1178 closed_completed_external_ref_collection_ignores_absent_open_and_non_completed_events -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:417 record_external_ref_event | crates/pm-projection/src/graph/query/tests.rs:1206 | `port/decision_store/issue/ExternalRefStore#record_external_ref_event().` (crates/pm-port-store/src/port/decision_store/issue.rs:134) |
| " | crates/pm-projection/src/graph/query/tests.rs:1216 | `port/decision_store/issue/ExternalRefStore#record_external_ref_event().` (crates/pm-port-store/src/port/decision_store/issue.rs:134) |
| " | crates/pm-projection/src/graph/query/tests.rs:1226 | `port/decision_store/issue/ExternalRefStore#record_external_ref_event().` (crates/pm-port-store/src/port/decision_store/issue.rs:134) |
| crates/pm-projection/src/graph/query/tests.rs:1262 public_issue_closure_cause_uses_external_ref_event_log -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue | crates/pm-projection/src/graph/query/tests.rs:1266 | `port/decision_store/issue/IssueStore#create_issue().` (crates/pm-port-store/src/port/decision_store/issue.rs:27) |
| crates/pm-projection/src/graph/query/tests.rs:1262 public_issue_closure_cause_uses_external_ref_event_log -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:364 update_issue_status | crates/pm-projection/src/graph/query/tests.rs:1279 | `port/decision_store/issue/IssueStore#update_issue_status().` (crates/pm-port-store/src/port/decision_store/issue.rs:59) |
| " | crates/pm-projection/src/graph/query/tests.rs:1282 | `port/decision_store/issue/IssueStore#update_issue_status().` (crates/pm-port-store/src/port/decision_store/issue.rs:59) |
| crates/pm-projection/src/graph/query/tests.rs:1262 public_issue_closure_cause_uses_external_ref_event_log -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:392 create_external_ref | crates/pm-projection/src/graph/query/tests.rs:1285 | `port/decision_store/issue/ExternalRefStore#create_external_ref().` (crates/pm-port-store/src/port/decision_store/issue.rs:105) |
| crates/pm-projection/src/graph/query/tests.rs:1262 public_issue_closure_cause_uses_external_ref_event_log -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:432 add_caused_by | crates/pm-projection/src/graph/query/tests.rs:1291 | `port/decision_store/issue/CausedByStore#add_caused_by().` (crates/pm-port-store/src/port/decision_store/issue.rs:167) |
| crates/pm-projection/src/graph/query/tests.rs:1262 public_issue_closure_cause_uses_external_ref_event_log -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:417 record_external_ref_event | crates/pm-projection/src/graph/query/tests.rs:1304 | `port/decision_store/issue/ExternalRefStore#record_external_ref_event().` (crates/pm-port-store/src/port/decision_store/issue.rs:134) |
| crates/pm-projection/src/graph/query/tests.rs:1650 ready_steps_happy_path_still_returns_blocking_candidates -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:164 add_step_dep | crates/pm-projection/src/graph/query/tests.rs:1662 | `port/execution_store/StepStore#add_step_dep().` (crates/pm-port-store/src/port/execution_store.rs:183) |
| crates/pm-projection/src/graph/query/tests.rs:1861 task_complete_true_only_when_all_steps_executed -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:1876 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| " | crates/pm-projection/src/graph/query/tests.rs:1889 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:1861 task_complete_true_only_when_all_steps_executed -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:1879 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| " | crates/pm-projection/src/graph/query/tests.rs:1892 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:1902 task_complete_dependency_in_progress_does_not_infect_owner_task -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:55 add_task_dep | crates/pm-projection/src/graph/query/tests.rs:1911 | `port/execution_store/TaskStore#add_task_dep().` (crates/pm-port-store/src/port/execution_store.rs:75) |
| crates/pm-projection/src/graph/query/tests.rs:1902 task_complete_dependency_in_progress_does_not_infect_owner_task -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:1928 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| " | crates/pm-projection/src/graph/query/tests.rs:1947 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:1902 task_complete_dependency_in_progress_does_not_infect_owner_task -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:1944 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| " | crates/pm-projection/src/graph/query/tests.rs:1950 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:1964 task_status_tracks_execution_progress -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:1989 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| " | crates/pm-projection/src/graph/query/tests.rs:2003 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:1964 task_status_tracks_execution_progress -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:1992 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| " | crates/pm-projection/src/graph/query/tests.rs:2006 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:2025 task_status_blocked_when_depends_on_unsatisfied -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:55 add_task_dep | crates/pm-projection/src/graph/query/tests.rs:2033 | `port/execution_store/TaskStore#add_task_dep().` (crates/pm-port-store/src/port/execution_store.rs:75) |
| crates/pm-projection/src/graph/query/tests.rs:2025 task_status_blocked_when_depends_on_unsatisfied -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:2069 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:2025 task_status_blocked_when_depends_on_unsatisfied -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:2072 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:2164 issue_status_mirrors_stored_status_before_accepted -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue | crates/pm-projection/src/graph/query/tests.rs:2173 | `port/decision_store/issue/IssueStore#create_issue().` (crates/pm-port-store/src/port/decision_store/issue.rs:27) |
| " | crates/pm-graph-query-contract/src/contract_tests/graph_query_contract/issue_status.rs:31 | `port/decision_store/issue/IssueStore#create_issue().` (crates/pm-port-store/src/port/decision_store/issue.rs:27) |
| crates/pm-projection/src/graph/query/tests.rs:2164 issue_status_mirrors_stored_status_before_accepted -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:364 update_issue_status | crates/pm-projection/src/graph/query/tests.rs:2195 | `port/decision_store/issue/IssueStore#update_issue_status().` (crates/pm-port-store/src/port/decision_store/issue.rs:59) |
| " | crates/pm-graph-query-contract/src/contract_tests/graph_query_contract/issue_status.rs:52 | `port/decision_store/issue/IssueStore#update_issue_status().` (crates/pm-port-store/src/port/decision_store/issue.rs:59) |
| crates/pm-projection/src/graph/query/tests.rs:2221 issue_status_stays_accepted_when_complete_task_has_no_fix_commit -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:84 add_task_address | crates/pm-projection/src/graph/query/tests.rs:2229 | `port/execution_store/TaskStore#add_task_address().` (crates/pm-port-store/src/port/execution_store.rs:95) |
| crates/pm-projection/src/graph/query/tests.rs:2221 issue_status_stays_accepted_when_complete_task_has_no_fix_commit -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:2240 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:2221 issue_status_stays_accepted_when_complete_task_has_no_fix_commit -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:2243 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:2256 issue_resolves_when_landing_step_carries_the_legacy_unrecorded_sentinel -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:84 add_task_address | crates/pm-projection/src/graph/query/tests.rs:2266 | `port/execution_store/TaskStore#add_task_address().` (crates/pm-port-store/src/port/execution_store.rs:95) |
| crates/pm-projection/src/graph/query/tests.rs:2256 issue_resolves_when_landing_step_carries_the_legacy_unrecorded_sentinel -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:2268 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:2256 issue_resolves_when_landing_step_carries_the_legacy_unrecorded_sentinel -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:2271 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:2287 issue_stays_accepted_when_landing_step_carries_an_arbitrary_non_sha_string -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:84 add_task_address | crates/pm-projection/src/graph/query/tests.rs:2297 | `port/execution_store/TaskStore#add_task_address().` (crates/pm-port-store/src/port/execution_store.rs:95) |
| crates/pm-projection/src/graph/query/tests.rs:2287 issue_stays_accepted_when_landing_step_carries_an_arbitrary_non_sha_string -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:128 allocate_step | crates/pm-projection/src/graph/query/tests.rs:2299 | `port/execution_store/StepStore#allocate_step().` (crates/pm-port-store/src/port/execution_store.rs:147) |
| crates/pm-projection/src/graph/query/tests.rs:2287 issue_stays_accepted_when_landing_step_carries_an_arbitrary_non_sha_string -> crates/pm-port-store/src/port/canonical_store/execution_facade.rs:138 mark_executed | crates/pm-projection/src/graph/query/tests.rs:2302 | `port/execution_store/StepStore#mark_executed().` (crates/pm-port-store/src/port/execution_store.rs:157) |
| crates/pm-projection/src/graph/query/tests.rs:4070 public_projection_loads_caused_by_edges_for_graph_walks -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue | crates/pm-projection/src/graph/query/tests.rs:4078 | `port/decision_store/issue/IssueStore#create_issue().` (crates/pm-port-store/src/port/decision_store/issue.rs:27) |
| crates/pm-projection/src/graph/query/tests.rs:4070 public_projection_loads_caused_by_edges_for_graph_walks -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:392 create_external_ref | crates/pm-projection/src/graph/query/tests.rs:4090 | `port/decision_store/issue/ExternalRefStore#create_external_ref().` (crates/pm-port-store/src/port/decision_store/issue.rs:105) |
| crates/pm-projection/src/graph/query/tests.rs:4070 public_projection_loads_caused_by_edges_for_graph_walks -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:432 add_caused_by | crates/pm-projection/src/graph/query/tests.rs:4096 | `port/decision_store/issue/CausedByStore#add_caused_by().` (crates/pm-port-store/src/port/decision_store/issue.rs:167) |
| crates/pm-projection/src/graph/query/tests.rs:4607 public_projection_loads_task_addresses_edges_for_graph_walks -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue | crates/pm-projection/src/graph/query/tests.rs:4613 | `port/decision_store/issue/IssueStore#create_issue().` (crates/pm-port-store/src/port/decision_store/issue.rs:27) |

Pairs: 45; call sites: 61. Every site's receiver is `CanonicalProjectStore::new(InMemory{Decision,Execution}Store::new())`, whose inner path truth records as `path_unresolved`. Full TSV: /scratch/agent/score/rs-trait/pm45.tsv.

### Condition 2: per-corpus edge diff (final tip cfeb2b0f vs frozen base; main ddc8c9b0 alone for attribution)
| corpus | removed CALLS (tip) | removed CALLS (main alone) | removed CALLS caused by the tip, not by main | other removed types (tip) |
|---|---|---|---|---|
| pm | 0 | n/a | 0 | 53 SEMANTICALLY_RELATED, 5 USAGE, 3 WRITES |
| bevy | 0 | n/a | 0 | 28 SEMANTICALLY_RELATED |
| ruff | 1 | 1 (same pair) | 0 | 2 DECORATES, 5 SEMANTICALLY_RELATED, 43 USAGE, 43 WRITES |
| codex | 7 | 7 (same pairs) | 0 | 9 SEMANTICALLY_RELATED, 2 TESTS, 15 USAGE |

This commit removes zero CALLS pairs on all four corpora. The ruff pair and the 7 codex pairs are removed by main ddc8c9b0 itself (Python binder and lane/rs dependency resolution). Diff files: /scratch/agent/score/rs-trait/c3/score-<corpus>-edges/{removed,added}.tsv and cmain/ for main.

## Build, suites, fixtures (final tip cfeb2b0f)
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/rs-trait -j6` passed with rc=0 and 0 warnings.
- Suites (`scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract" BUILD_DIR=build/t-rs-trait`): `1962 passed, 0 failed, 0 skipped`.
  - rust_lsp 836/0, call_reference_contract 15/0, extraction 487/0, lang_contract 43/0, pipeline 542/0, edge_structural 39/0.
  - pipeline and edge_structural exit rc=1 only on LeakSanitizer with 0 failed assertions. That is the known pre-existing condition.
- Fixtures (`make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/rs-trait`): `fixtures: 52 fixtures x 2 modes, 93 known mismatches, 0 failures`.

## Open classes, not started in this lane
- **T1** (bevy 1,449, IntoScheduleConfigs `.chain/.after/.in_set/.run_if` on fn items and tuples).
- **P1** path calls (`Trait::m`, `T::m`, `<&Self>::m`, single-impl `Type::from` and `Type::default`).
- **B5** typing of const, static and unit-struct receivers. The trait lookup it needs is this commit's rule.
- **Std-trait impls on generic corpus receivers** (for example `Iterator for QueryIter<D,F>`). Worth about +400 credited on bevy, but this needs a proof that no dependency trait competes, so it was left out.

## T1 (IntoScheduleConfigs on fn items and tuples): not committed within the 30-minute cut-off
No T1 code is committed. The tip is still cfeb2b0f, unchanged as instructed. What I found, for the next attempt:
- **Truth targets** (bevy, from /scratch/agent/tmp/rs-inv/bevy.cls.tsv).
  - Most T1 sites credit the trait declaration's default body: `IntoScheduleConfigs#in_set()` 419, `#run_if()` 176, `#chain()` 169, `#after()` 168, `#before()` 38, `#chain_weak()` 28, and so on.
  - A second group credits `impl#[ScheduleConfigs<T>][IntoScheduleConfigs<T, ()>]m()` (after 132, before 91, in_set 85, ...). There the receiver is already a `ScheduleConfigs<T>`, the return of an earlier chained call. That is B2 return typing, not a fn-item or tuple receiver, so it must not take the trait-declaration target.
- **Impls in crates/bevy_ecs/src/schedule/config.rs.**
  - Blanket impls: `impl<F, Marker> IntoScheduleConfigs<ScheduleSystem, Marker> for F` (:699) and `impl<S: SystemSet> IntoScheduleConfigs<InternedSystemSet, ()> for S` (:715).
  - Tuple impls come from `macro_rules! impl_node_type_collection` (:724), expanded by `all_tuples!` (:751).
  - None of these overrides in_set, run_if, chain, after or before. Only `ScheduleConfigs<T>` (:607) and `BoxedSystem` (:709) override, and neither can be a fn item or a tuple. So for a fn-item or tuple receiver, the default body in the trait declaration is the only possible target.
- **Proposed rule.** At the `unknown_receiver_type` emission for method calls (rust_lsp.c, end of the method branch of rust_resolve_call_expression_inner), link to the trait declaration when all of these hold:
  1. The receiver node is a tuple_expression, or a path that is no local binding and names a corpus fn item.
  2. Exactly one corpus trait declaring m with a default body is in scope (rust_trait_out_of_scope).
  3. No impl of that trait whose written self is a bare impl parameter or a tuple overrides m.
  4. m is not a method of a std trait that fn items or tuples implement (Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default, Fn*, Into/From, ToOwned, Borrow, Any).
- **Why it was not done in time.** The registry has no index of trait declarations by method name. A per-site scan of all functions over bevy's unknown-receiver sites is too slow, so the next attempt needs a name index built once per registry. Then come a fixture (blanket `impl<F> T for F` plus a macro-generated tuple impl, with a negative case where a second in-scope trait declares m), the scores and the suites.
