STATUS: DONE
COMMIT: bbbc54ae
CHANGED: 6 files
CLOSED: 1 items
OPEN: 3 items
BLOCKERS: bench rebuild ended "REBUILD: FAIL fingerprints differ", so no corpus score; ASan leak reports (field_cache in modfacts scan) make the pipeline and edge_imports suites exit rc=1 with 0 test failures
---
Branch lane/rs in /scratch/agent/wt/rs: a76d3f4a..bbbc54ae, 2 signed-off commits.
Files: internal/cbm/lsp/rust_lsp.c, rust_modtree.c, rust_modtree.h, rust_cargo.c,
src/pipeline/pass_lsp_cross.c (pxc_rust_* only, plus pxc_rel_escapes, which only they call).
No fixture was added: tools/fixtures/fixtures.py builds only path dependencies that are workspace
members, so no shape in item 1 (registry, git, out-of-repository, or [patch]) can be built with rustc.

## CLOSED 1: registry dependency linked to a same-named workspace member (HANDOFF Rust)
81db821a covers these shapes:
- registry or git dep (without `workspace = true`) vs a root path entry of the same name;
- root `[package]` with `[workspace.dependencies] dep = {path}` plus `[dependencies] dep = "1.0"`:
  workspace-table entries are no longer externs of the root package;
- `path = "../../dep"` that leaves the repository (pxc_rel_escapes);
- rust_lsp.c fallback member routes: refused when the tree's extern verdict is "outside"
  (new cbm_rust_modtree_extern_outside), or when the tree places the file in no crate
  (`include!`d files);
- rust_cargo.c: a dotted `[workspace.dependencies] dep.path` no longer merges into the root's
  own `dep` record (round-1 review finding 1, a regression).
bbbc54ae: a root `[patch.<source>] dep = {path}` resolves a member's registry `dep` to that path.
This was the round-2 review regression. Non-root patches stay ignored.
Observation: codex-rs core/src/context_manager/history.rs:1177 and tui/src/clipboard_paste.rs:179
(`image::...`, registry `image` beside member utils/image) get the extern verdict "outside" and
are not routed into utils/image. Not confirmed by an index run, because the bench failed.
Shapes not covered (narrowed in the commit bodies):
- a head whose extern maps into the project but to a crate other than the same-named member,
  while the tree returns UNKNOWN;
- the `[patch.crates-io.dep]` sub-table form;
- a dotted `dep.path` in a patch after a same-named [workspace.dependencies] entry (it merges);
- the patch version match and `package =` renames are not checked;
- pass_pkgmap.c:510 reads the patch entries as CBM_CRATE_WORKSPACE_DEP records (only matters for
  an invalid manifest that has `workspace = true` with no real table entry).

## OPEN 2: virtual-workspace root file has CALLS but no IMPORTS
IMPORTS come only from the module tree (pass_pkgmap.c:4152). A file in no crate (for example a
.rs file under a virtual root) gets none. Since 81db821a, rust_workspace_member refuses member
routes from such a file. So its member-routed CALLS are now withheld (function_not_in_registry),
which removes part of the mismatch. Other fallback CALLS routes for files in no crate were not
audited, and no fixture diagnosis was run (fixture indexing was disallowed before 03:00, and the
time went to the review rounds).

## OPEN 3: no test for the trait-in-scope index fail-closed path
rust_modtree.c:2227: when the index is NULL, every entry is scanned. Testing this needs
allocation-failure injection in a C test. Tests are deferred in this run.

## OPEN 4: ten bevy_reflect dyn-trait calls removed without a stop record
The site list (/tmp/M5/truly-absent2-bevy.tsv) is gone. The bench rebuild ended
"REBUILD: FAIL fingerprints differ from tools/rust-bench/FINGERPRINTS", so the bevy score and
edgediff could not run. Next step: once the bench passes, run edgediff against rust-base/bevy with
#B/#L keys normalised, list the dyn-trait CALLS that have no unresolved_sites row, and find the
silent return in rust_lsp.c's dyn-receiver dispatch.

## Runs (summary lines)
- Build (each round, final at bbbc54ae): `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/rs -j4`: EXIT 0, 0 warnings.
- Suites at 81db821a: `scripts/test.sh --suites "rust_lsp pipeline edge_imports" BUILD_DIR=build/t-rs`: "1476 passed, 0 failed, 0 skipped (3 selected suites, 16 jobs)", exit 1.
- Suites at bbbc54ae: same command and the same line, "1476 passed, 0 failed, 0 skipped", exit 1.
  Per suite: rust_lsp rc=0 pass=836; edge_imports rc=1 pass=98 fail=0; pipeline rc=1 pass=542 fail=0.
  The rc=1 comes from AddressSanitizer leak reports (pipeline: 57 direct leaks; edge_imports: 4).
  They are allocated in internal/cbm/cbm.c:922 field_cache from rmf_scan_item/rmf_scan_mod
  (cbm_rust_modfacts_scan via pxc_rust_scan_file). No changed function appears in any leak stack.
  Not checked against main for lack of time.
- Fixtures at bbbc54ae: `make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/rs`: "fixtures: 51 fixtures x 2 modes, 93 known mismatches, 0 failures, 44.9 s".
- Corpus score: none. The bench rebuild ended FAIL.

## Gate edge diff (codex, main 0cac5290 vs rust-base/codex)
Verdict: all 24 removed CALLS/USAGE/TESTS edges are withdrawn links that Cargo cannot prove. None is
a regression in the Cargo/rust-analyzer sense. All 24 come from four files that no Cargo target
compiles:
- cli/e2e_benches/codex_help.rs (1 CALLS -> utils/cargo-bin cargo_bin via
  `codex_utils_cargo_bin::cargo_bin`). cli/Cargo.toml:137-139: "These Rust sources are intentionally
  Bazel-only macrobenchmarks rather than Cargo targets", `ignored-paths = ["e2e_benches/*.rs"]`. The
  file is not under src/, tests/, benches/ or examples/, and no [[bench]] declares it.
- core/tests/remote_env_windows/remote_env_windows_test.rs:
  2 CALLS (TurnEnvironmentSelections.new, PathUri.parse), 2 TESTS (the same pair; TESTS derive from
  those CALLS) and 15 USAGE (CollaborationMode, Settings, ThreadSettingsOverrides,
  TurnEnvironmentSelection fields and PathUri.parse; heads `codex_protocol::...` and the path-uri
  crate). Cargo auto-discovers only tests/*.rs and tests/<dir>/main.rs. This directory holds only
  remote_env_windows_test.rs, BUILD.bazel and README.md, so Cargo does not discover the test.
  core/Cargo.toml:177 has `ignored-paths = ["tests/remote_env_windows/*.rs"]`, and no `mod` in
  core/tests/*.rs or suite/ names it.
- exec-server/testing/exec_server.rs (3 CALLS: ExecServerRuntimePaths.new, run_main,
  HttpClientFactory.new, through `use codex_exec_server::...` and `use codex_http_client::...`) and
  exec-server/testing/wine_exec_server.rs (1 CALLS -> cargo_bin). testing/ is built only by Bazel
  (exec-server/BUILD.bazel loads exec_server_compat_test); no Cargo target path names it.
Truth: ~/Developer/cbm-bench/codex/truth.tsv has 0 rows for each of the four files, because
rust-analyzer analyzes no file outside a crate. So these edges were never in the truth, and the score
is unchanged (226161/229953, flagged 1).
Code path now withholding them: 81db821a, internal/cbm/lsp/rust_lsp.c rust_workspace_member(). Since
the module tree holds the project, `!cbm_rust_modtree_has_file(tree, module_qn)` returns NULL for
every head named from a file the tree places in no crate. This is the round-1 review finding-3 rule:
the caller's crate is not established, so the member route is unproven. It blocks the four member
routes (rust_canonical_workspace_qn, the use-path rewrite, rust_resolve_path_expr, the
lsp_cross_crate route). The calls now end at the existing stop, function_not_in_registry.
Note: Bazel does compile these files against the same member crates, so the removed targets are what
a Bazel build binds. If the product wants Bazel-only sources linked, the needed rule is different:
establish the crate from BUILD.bazel deps. Under the Cargo/rust-analyzer oracle of this run, the
withhold is the intended behaviour. SEMANTICALLY_RELATED 8 removed / 6 added is the corpus-wide
churn that follows any CALLS change (HANDOFF cross-cutting item); it was not adjudicated per edge.
