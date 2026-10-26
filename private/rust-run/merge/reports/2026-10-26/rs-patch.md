STATUS: DONE
COMMIT: 6437c7b0
CHANGED: 2 files
CLOSED: 5 of 5
OPEN: 0
BLOCKERS: none
SCORES: codex 226161/229953 flagged 1; pm 39983/47116 flagged 73
---
Decision: withdraw the root [patch] mapping of bbbc54ae (one commit, 6437c7b0 on lane/rs-patch, rebased on main deca97c9).
The proper fix needs the dependency's source (registry/git), its version requirement and semver matching,
`package` renames on both sides, and a patch record separate from [workspace.dependencies]; that needs new
CBMCargoDep fields in rust_cargo.h, outside this lane's edit band, and would not fit the 45 minutes. The
withdrawal leaves pxc_rust_dep_dir byte-identical to 81db821a (a dependency without path or workspace = true
is external) and rust_cargo.c skips [patch.<source>] like any unread table, with a comment stating the Cargo rule.
Files: internal/cbm/lsp/rust_cargo.c, src/pipeline/pass_lsp_cross.c (pxc_rust_dep_dir only).

Observation: hand-indexed workspaces /scratch/agent/tmp/rs-patch/fx/{f0,f1a,f1b,f2,f3,f4,f5} (fixtures.py builds
only path dependencies), runner /scratch/agent/tmp/rs-patch/bin/run.sh, outputs before.txt (main 0cac5290 binary)
and after.txt (this commit) in /scratch/agent/tmp/rs-patch. Call site `dep::f()` in app/src/lib.rs.

1. CLOSED. f1a [patch."https://github.com/x/y"] dep path over `dep = "=1.0.0"`; f1b [patch.crates-io] dep path over
   `dep = { git = ... }`. Before: CALLS run -> dep.src.lib.f lsp_module_path (wrong link). After: R row
   lsp_unresolved function_not_in_registry, no CALLS.
2. CLOSED. f2 dep version 2.0.0 patched over `dep = "=1.0.0"`. Before: wrong link. After: lsp_unresolved
   function_not_in_registry.
3. CLOSED. f3 `decoy = { package = "dep", path = "dep" }`, app `decoy = "1"`, `decoy::f()`. Before: run ->
   dep.src.lib.f (wrong link). After: lsp_unresolved function_not_in_registry. (`dep` stays external: withheld.)
4. CLOSED. f4 patch before `[workspace.dependencies] dep.path = "dep"`, member `dep.workspace = true`.
   Before: lsp_unresolved (withheld, regression). After: CALLS run -> dep.src.lib.f lsp_module_path, as at 81db821a.
5. CLOSED. f5 member app's `[patch.crates-io] dep = { path = "patches/dep" }`. Before: patches/dep became a
   package/crate root (crate::g inside it resolved lsp_module_path). After: no package; that call resolves only by
   the same-file rule (lsp_direct). (A vendor/ path is not indexed at all, hence patches/.)
Cost: bbbc54ae's accepted shape (f0: crates-io patch, version 1.0.0, `=1.0.0`) is withheld again
(lsp_unresolved function_not_in_registry), never linked by name.

Build: make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/rs-patch -j4: rc 0, 0 warnings.
Suites: rust_lsp pipeline edge_imports: 1476 passed, 0 failed, 0 skipped; exit 1 from LeakSanitizer only (known).
Fixtures: 51 fixtures x 2 modes, 93 known mismatches, 0 failures.
Score codex: calls ALL 226161/229953, flagged 1 (base 226161, flagged 1). edgediff vs rust-base: CALLS removed 7,
  added 0 (callers in cli/e2e_benches, exec-server/testing, core/tests/remote_env_windows). None from this lane:
  scoring main 0cac5290 (with bbbc54ae) and diffing against this commit gives removed=0 added=0 over 925167 edges,
  so the 7 come from main commits between base b9a7012c and 0cac5290. codex's [patch] tables hold git entries only.
Score pm: calls ALL 39983/47116, flagged 73 (base identical). edgediff vs base: CALLS removed 0 (USAGE 5, WRITES 3
  removed, pre-existing on main; pm has no [patch]).
Logs: /scratch/agent/tmp/rs-patch/check/{SUMMARY,suites.log,fixtures.log,score-*.log,edges-*}
