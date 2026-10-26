STATUS: DONE (pm flagged +14 accepted by coordinator ruling: pm-fakes oracle gap)
COMMIT: 2a876f69 (lane/w3-locals, 2 commits on main 6ab8c225, rebased: 2c71f292 rule, 2a876f69 review fix)
CHANGED: internal/cbm/lsp/rust_lsp.c (rust_sole_head_impl_method split from rust_open_arg_sole_impl_method; rust_trait_default_competitor; method-branch trait_receiver_shape stop and refusal; Deref hop alias expansion), tests/fixtures/graph/rust/{soleimpl,localalias}
CLOSED: pm B4/D3 trait_receiver_shape on CanonicalProjectStore<A> (+263 pairs); review item on 1141b5de (default-inheriting empty impls)
OPEN: B5 const/static (design below, not built); pm trait_scope_identity 168 (AdrEntityCapabilities through `use super::*`); D1 derive(Deref) newtypes (bevy_derive) and Single/Extract projections; Gizmos trait-by-argument; RunSystemOnce/CurveExt blanket declarations; the open-argument rule keeps method-defining-only competitors (unchanged here: applying the default competitor check to it removed 199 credited bevy IntoScheduleConfigs pairs)
BLOCKERS: none
SCORES: bevy 70280/84295 f35; pm 45164/47116 f87; codex 226200/229953 f1; ruff 104413/112300 f1
---
# w3-locals

## Baseline and dump (main 9cff5b79, binary /scratch/agent/tmp/w3l/main-bin)
The scores reproduce the brief: bevy 69,506/84,295 f35, pm 42,589/47,116 f73. miss.py/cls.py re-run on these scores; classes B4/B5/D1/D3 joined with CBM_RUST_RESOLVE_DUMP, last R row per site (join.py, /scratch/agent/tmp/w3l). Each row is class, first failing step, sites.

```
B4 total 509
B5 total 207
D3 total 98
B4	unres:trait_receiver_shape	218
B4	unres:trait_scope_identity	166
B4	unres:method_not_found	98
B4	unres:unknown_receiver_type	26
B4	unres:bound_declaration_not_proven	1
B5	unres:unknown_receiver_type	207
D3	unres:trait_receiver_shape	82
D3	unres:method_not_found	11
D3	unres:trait_scope_identity	5
B4 total 761
B5 total 230
D1 total 358
D3 total 372
B4	unres:unknown_receiver_type	330
B4	unres:method_not_found	219
B4	unres:trait_receiver_shape	98
B4	unres:prelude_trait_method_not_proven	29
B4	unres:receiver_identity_not_proven	25
B4	unres:trait_scope_identity	21
B4	unres:trait_impl_key_unreadable	14
B4	unres:trait_array_length_unknown	6
B4	unres:unrecognised_std_method	6
B4	unres:trait_receiver_partial	5
B4	res:lsp_field_access:wrongtarget	3
B4	unres:deref_receiver_identity_not_proven	1
B4	unres:trait_owned_receiver_borrowed	1
B4	unres:trait_receiver_unknown	1
B4	unres:bound_declaration_not_proven	1
B4	res:lsp_method_dispatch:wrongtarget	1
B5	unres:unknown_receiver_type	203
B5	unres:method_not_found	27
D1	unres:method_not_found	276
D1	unres:prelude_trait_method_not_proven	37
D1	unres:trait_scope_identity	23
D1	unres:unknown_receiver_type	10
D1	res:lsp_deref_dispatch:wrongtarget	4
D1	unres:inherent_associated_function	3
D1	unres:trait_receiver_shape	2
D1	unres:receiver_identity_not_proven	2
D1	unres:trait_impl_key_unreadable	1
D3	unres:method_not_found	274
D3	unres:prelude_trait_method_not_proven	21
D3	unres:receiver_identity_not_proven	20
D3	unres:unknown_receiver_type	12
D3	unres:trait_impl_key_unreadable	11
D3	unres:trait_scope_identity	10
D3	unres:bound_declaration_not_proven	7
D3	res:lsp_method_dispatch:wrongtarget	7
D3	unres:trait_receiver_shape	5
D3	unres:trait_receiver_partial	3
D3	res:lsp_field_access:wrongtarget	2
```
Grouped by truth owner, bevy method_not_found/prelude stops: HashMap (bevy_platform) behind an alias or derive 185, Query through Single/Extract 67, RunSystemOnce# 69, CurveExt# 36, dyn PartialReflect/Reflect inherent 65, Transform/Timer/Parallel through Single or #[derive(Deref)] newtypes about 60, Gizmos trait-by-argument 45.
pm: trait_receiver_shape 300 are all CanonicalProjectStore<A> (bounded impl in another file); trait_scope_identity 168 is one file (adr/tests.rs, AdrEntityCapabilities blanket impl in scope via `use super::*`); B5 207 are cross-file const/static receivers.

## Commit 1141b5de
- Rule: on a trait_receiver_shape stop (the probe's own stop and the refusal), if the receiver is headed by a corpus struct/enum/union H with no inherent m and no Deref target, and exactly one in-scope corpus trait declares m with exactly one impl on H defining m (blanket, tuple and reference impls withhold), that impl is linked. The open-argument rule (w2-trait) now calls the same function.
- A Deref hop's target is expanded through aliases.
- Example: pm xtask/tests/static_safety_policy.rs:537 `store.create_reason(..)` now matches truth `impl#[CanonicalProjectStore<A>][ReasonStore]create_reason()`.
- Gain against main 605f06a2 (coordinator figures bevy 70,246, pm 44,864): pm +300, bevy +51. CALLS added: pm 263, bevy 50. CALLS removed: 0 on both (edgediff against /scratch/agent/gate4 main scores).
- Defect, not fixed: pm flagged 73 -> 87. All 14 new wrong pairs are lsp_trait_dispatch in crates/pm-projection/src/graph/query/tests.rs (lines 535, 1178, 1262, 2164, 4070, 4607) on `CanonicalProjectStore::new(InMemoryDecisionStore::new())`. There the truth is the trait declaration, e.g. :1266 `IssueStore#create_issue`. rust-analyzer does not prove the bounded impl for that receiver; the likely cause is the dev-dependency cycle pm-fakes <-> pm-port-store, which duplicates the adapter trait. The same receiver in pm-adr-usecase credits the impl, so the rule cannot tell these sites apart as written.
- Next step: withhold when the impl's bound on the argument (`A: IssueStoreAdapter`) is satisfied only by an impl in a crate that is a dev-dependency of the trait's crate's dependents, or fall back to the trait declaration when the bound is unproven. Re-score pm.
- The localalias fixture is right on main too, so the alias expansion is not shown to discriminate. The soleimpl fixture is withheld on main and right on the tip.

## B5 design (not built)
Free const/static items reach the cross defs as Variable without a type. The extractor (extract_defs.c, outside this lane) sets no return_type. Inside the lane:
- rmf_scan_item records the const/static `type` text in RmfRecord.text.
- RmtDef carries it into the tree arena, and rmt_classify returns it in a new CBMRustPathResult.value_type for NOT_FUNCTION.
- rust_eval_value_path_type parses it with rust_parse_def_type_text in the declaring module (the qn prefix) when no owner type is found.
- The file digest must hash the text.
- Expected: pm 207, bevy 203 sites.

## Removed pairs
None. CALLS removed is 0 on pm and bevy against main 605f06a2. ruff and codex were scored only (ruff 104,441 vs 103,872+534 on main; codex 226,204 vs 226,198 at 9cff5b79), with no edge diff.

## Verification on 1141b5de
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w3-locals -j4` rc=0, no warning/error lines.
- Fixtures: `fixtures: 66 fixtures x 2 modes, 94 known mismatches, 0 failures, 64.9 s`
- Suites: NOT RUN (the ASan test build did not fit before cut-off).
- Scores (scripts/rust-score.sh, one at a time): bevy 70,297/84,295 f35; pm 45,164/47,116 f87; ruff 104,441/112,300 f1; codex 226,204/229,953 f1.

## After the coordinator's ruling and review (tip 2a876f69 on main 6ab8c225)
### The 14 accepted pm flags (all lsp_trait_dispatch, all in crates/pm-projection/src/graph/query/tests.rs; graph target -> truth row)
- crates/pm-projection/src/graph/query/tests.rs:1178 (closed_completed_external_ref_collection_ignores_absent_open_and_non_completed_events) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:392 create_external_ref; truth crates/pm-projection/src/graph/query/tests.rs:1181 port/decision_store/issue/ExternalRefStore#create_external_ref().
- crates/pm-projection/src/graph/query/tests.rs:1178 (closed_completed_external_ref_collection_ignores_absent_open_and_non_completed_events) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:417 record_external_ref_event; truth crates/pm-projection/src/graph/query/tests.rs:1206 port/decision_store/issue/ExternalRefStore#record_external_ref_event().
- crates/pm-projection/src/graph/query/tests.rs:1262 (public_issue_closure_cause_uses_external_ref_event_log) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue; truth crates/pm-projection/src/graph/query/tests.rs:1266 port/decision_store/issue/IssueStore#create_issue().
- crates/pm-projection/src/graph/query/tests.rs:1262 (public_issue_closure_cause_uses_external_ref_event_log) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:364 update_issue_status; truth crates/pm-projection/src/graph/query/tests.rs:1279 port/decision_store/issue/IssueStore#update_issue_status().
- crates/pm-projection/src/graph/query/tests.rs:1262 (public_issue_closure_cause_uses_external_ref_event_log) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:392 create_external_ref; truth crates/pm-projection/src/graph/query/tests.rs:1285 port/decision_store/issue/ExternalRefStore#create_external_ref().
- crates/pm-projection/src/graph/query/tests.rs:1262 (public_issue_closure_cause_uses_external_ref_event_log) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:417 record_external_ref_event; truth crates/pm-projection/src/graph/query/tests.rs:1304 port/decision_store/issue/ExternalRefStore#record_external_ref_event().
- crates/pm-projection/src/graph/query/tests.rs:1262 (public_issue_closure_cause_uses_external_ref_event_log) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:432 add_caused_by; truth crates/pm-projection/src/graph/query/tests.rs:1291 port/decision_store/issue/CausedByStore#add_caused_by().
- crates/pm-projection/src/graph/query/tests.rs:2164 (issue_status_mirrors_stored_status_before_accepted) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue; truth crates/pm-projection/src/graph/query/tests.rs:2173 port/decision_store/issue/IssueStore#create_issue().
- crates/pm-projection/src/graph/query/tests.rs:2164 (issue_status_mirrors_stored_status_before_accepted) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:364 update_issue_status; truth crates/pm-projection/src/graph/query/tests.rs:2195 port/decision_store/issue/IssueStore#update_issue_status().
- crates/pm-projection/src/graph/query/tests.rs:4070 (public_projection_loads_caused_by_edges_for_graph_walks) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue; truth crates/pm-projection/src/graph/query/tests.rs:4078 port/decision_store/issue/IssueStore#create_issue().
- crates/pm-projection/src/graph/query/tests.rs:4070 (public_projection_loads_caused_by_edges_for_graph_walks) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:392 create_external_ref; truth crates/pm-projection/src/graph/query/tests.rs:4090 port/decision_store/issue/ExternalRefStore#create_external_ref().
- crates/pm-projection/src/graph/query/tests.rs:4070 (public_projection_loads_caused_by_edges_for_graph_walks) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:432 add_caused_by; truth crates/pm-projection/src/graph/query/tests.rs:4096 port/decision_store/issue/CausedByStore#add_caused_by().
- crates/pm-projection/src/graph/query/tests.rs:4607 (public_projection_loads_task_addresses_edges_for_graph_walks) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:327 create_issue; truth crates/pm-projection/src/graph/query/tests.rs:4613 port/decision_store/issue/IssueStore#create_issue().
- crates/pm-projection/src/graph/query/tests.rs:535 (ready_steps_frozen_for_task_under_deprecated_adr) -> crates/pm-port-store/src/port/canonical_store/decision_facade.rs:309 update_adr_status; truth crates/pm-projection/src/graph/query/tests.rs:542 port/decision_store/adr/AdrStore#update_adr_status().

### Commit 2a876f69: review fix
- Rule: when the sole in-scope declaration of m has a default body, the concrete-receiver link is withheld (trait_receiver_shape stays) if the trait has any other impl relation:
  - a second impl on the head;
  - an impl on `&H`/`&mut H` or any reference;
  - an impl on a type the registry does not declare (a blanket impl's parameter).
  Empty impls count, because they inherit the default. The impl headers on the registered types are the source.
- The open-argument rule passes defaults_compete=false, so its behaviour on main is unchanged. With the check also applied there, 199 credited bevy pairs were removed (IntoScheduleConfigs before/after/in_set through blanket impls).
- Fixture: soleimpl gains `impl<A: Bound> Pick for Wrap<A> { fn pick }` beside `impl Pick for &Wrap<Bad> {}`.
  - `inherited()` calls `Wrap { inner: Bad }.pick()`. It was WRONG on 1141b5de (-> Wrap::pick) and is withheld now.
  - `good.pick()` is withheld too, which is accepted.
- Cost against 1141b5de: bevy -17 pairs that 1141b5de had added (CubicBezier to_curve 2, ScheduleConfigs after 2, the Serialize impls of 7 reflect serializers, SceneScope resolve, ...). pm unchanged.

### Scores, main 6ab8c225 (built here, /scratch/agent/tmp/w3l/main2-bin) -> tip 2a876f69
- bevy 70,246 f35 -> 70,280 f35 (CALLS +33 / -0)
- pm 44,864 f73 -> 45,164 f87 (CALLS +263 / -0; the 14 flags above)
- ruff 104,406 f1 -> 104,413 f1 (+5 / -0)
- codex 226,198 f1 -> 226,200 f1 (+2 / -0)
- Removed CALLS pairs against main: none on any corpus.

### Suite and fixture lines on 2a876f69
- `scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract" BUILD_DIR=/scratch/agent/build/w3-locals-t; echo EXIT=$?` -> `1961 passed, 0 failed, 0 skipped (6 selected suites)`, EXIT=1.
  - The one FAIL line is tests/test_pipeline.c:4007 (`failed == 5`): pair_act repeated row, 2 LeakSanitizer reports.
  - Main 6ab8c225 shows the same. Built separately, it also withholds `x.pair().pair_act()` (trait_receiver_partial) on a replica of the test crate, so the failure is pre-existing and not from this lane. gate4 on 605f06a2 failed the same row at :4003.
- `fixtures: 66 fixtures x 2 modes, 94 known mismatches, 0 failures, 46.2 s`
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w3-locals -j4` rc=0, no warnings.
