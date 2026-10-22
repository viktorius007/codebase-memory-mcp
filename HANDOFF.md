## STATE — 22 Oct: main = origin/main (22526ac6). Build, test and gate conventions, measurements and host facts for every repository on this host: `~/src/build-cycle-standard/README.md`, section 0 first.
Landed 21–22 Oct on this fork: incremental all-core builds with depfiles and no build-dir deletion (`reports/lanes/cbm-build.md` in that standard; no-change build 108 s to 0.4 s, header edit 45 s to 16 s); GCC 15 LSP fix; two time-based tests now assert the mechanism (`reports/lanes/cbm-time-assertions.md`; the lock registry gained a settable clock, TSan clean). Known and not re-reported: clang-format findings in four untouched src files, LeakSanitizer exits in eleven suites, the umask-0002 `cli` failure. Lint tools on this host: clang-format/clang-tidy 21 (apt), cppcheck 2.20.0 in `~/.local/bin`.

## STATE — 17 Oct: main = origin/main (bench consolidation, a477a034 and its HANDOFF follow-up). Nothing running; no lane, branch, worktree or scratch left.
Product code is unchanged since 93a2275e; the bench base binary is b9a7012c. Verified 17 Oct: a fresh agent given only this file ran rebuild.sh from a clean
GitHub clone, got REBUILD: PASS, and every corpus, truth/refs file and base edge set was identical to ~/Developer/cbm-bench.
History before this point: private/rust-run/merge/run-ledger.md (authoritative) and private/rust-run/merge/handover/HANDOFF-pre-closeout-16oct.md (previous HANDOFF).
HANDOFF.md and private/ are tracked and pushed (17 Oct); the GitHub repo is public, so neither may hold secrets or personal data.

Landed 16 Oct: b3a1f2bc C++ init-capture + walk-depth-cap stop rows; 3f70536a vendored checksum re-bless; ebcbd291 deterministic indexes (callsline-v2);
b9a7012c Python forward fix (fix/py-fwd-r3, 13 commits; five reviews, last PASS reports/iter3-pyfwd-review5.md); 93a2275e C/C++ digests re-frozen.
Verified on b9a7012c tree: full scripts/test.sh 10321 passed / 0 failed / 11 skipped (193 suites); gate build/8 suites 2224/0/lint PASS; React Native IDENTICAL;
bitcoin +4 Python CALLS + SEMANTICALLY_RELATED churn (re-frozen); Rust vs ebcbd291: codex/pm/bevy 0/0, ruff +3 graded USAGE.
Installed ~/.local/bin/codebase-memory-mcp = release build of b9a7012c (scripts/build.sh), 16 Oct 16:59. The running daemon and MCP server still run the old copy until restarted.

Bench (corpora, truth, frozen bases): ~/Developer/cbm-bench, regenerated only by tools/rust-bench/ (see BENCH below). Rust base ~/Developer/cbm-bench/rust-base/<codex|ruff|pm|bevy> (b9a7012c);
C/C++ base graphs ~/Developer/cbm-bench/cpp/base/, digests scripts/graph-identity/DIGESTS.
Landing procedure: fresh clone in short /tmp path, merge, tools/rust-bench/gate.sh <clone> <scratch>, edgediff vs baseline, independent Opus review, ff main on PASS, housekeeping same step (private/CLAUDE.md).
scripts/test.sh needs BUILD_DIR to be a direct child of build/ (default is fine). Extract agent final reports with /tmp/iter2-lastmsg.sh if present (else read transcript last assistant message).

BENCH — consolidated 17 Oct (owner decision: one source of truth; bulky data is never kept, only regenerated).
- tools/rust-bench/: corpora.tsv (4 Rust corpora, full pinned SHAs), fetch-corpora.sh (gh tarballs, no .git; C/C++ via scripts/graph-identity/fetch-corpora.sh),
  gen-truth.sh (rust-analyzer pinned to 1.99.0 b940084d, refuses another version; build output kept out of the corpus), baseline.sh (freezes <bench>/rust-base), gate.sh, edgediff.py.
  On a new machine, from the repo root: `tools/rust-bench/rebuild.sh b9a7012c [bench-dir]` (about 25 min; bench-dir defaults to ~/Developer/cbm-bench, which every script here uses when not given one). It checks prerequisites (gh logged in with access to the
  private pm repo; installs rustup toolchain 1.99.0 + rust-analyzer), fetches, generates truth, builds the base binary in a temporary worktree,
  freezes rust-base and cpp/base, and fails if the C/C++ digests differ from scripts/graph-identity/DIGESTS or the Rust corpus/truth/refs/base-edge
  hashes (fingerprint.py) differ from tools/rust-bench/FINGERPRINTS. Each corpus is loaded with its own toolchain (corpora.tsv column 5: codex 1.95.0, ruff 1.97.1, pm 1.98.1, bevy 1.99.0), installed by gen-truth.sh.
  Linux: 17 Oct, Ubuntu 26.04 x86_64 (16 vCPU, 31 GB, no swap): REBUILD: PASS, all 16 FINGERPRINTS and DIGESTS identical to macOS, 1778 s. Needs zlib1g-dev (root).
  Fixed on the way: GCC 14+ rejects b9a7012c's rust_lsp.c pointer mismatch (built with -Wno-error=incompatible-pointer-types); the truth was host-dependent
  because rust-analyzer resolves #[cfg(target_os)] for the host, so gen-truth.sh pins cargo.target aarch64-apple-darwin on every host (Mac output unchanged).
  Truth runs one corpus at a time (CBM_TRUTH_JOBS): codex peaks at 19 GB, and codex + bevy together thrashed a 31 GB box for 2 h without finishing.
  A FAIL there on fingerprints means the platform changes the data. Last line REBUILD: PASS|FAIL <step>.
  Then the gate: tools/rust-bench/gate.sh <clone> <scratch>.
- ~/Developer/cbm-bench was rebuilt 17 Oct entirely by these scripts; the old copy is in the macOS Trash. Verified, do not re-investigate:
  fresh corpora identical to the old ones; truth deterministic (all four regenerated twice, byte-identical); old truth was NOT reproducible with 1.99.0
  (pm identical; codex 2,978 sorted-line diffs, ruff 32, bevy 952; an older, unrecorded rust-analyzer); new base edge sets equal the old b9a7012c base
  for codex/pm/bevy; ruff 34/34 differ only in CONTAINS_FILE/FOLDER/HAS_BRANCH (old ruff copy was a git checkout, branch "main" vs "working-tree");
  C/C++ refrozen digests equal DIGESTS. Scores on the new base: codex 226,161/229,953 (denominator was 229,961), ruff 104,067/112,300, pm 39,983/47,116, bevy 61,368/84,295.

OPEN product defects (recorded, unowned — new work):
- Python: unbound-earlier reading links own node though a same-named submodule or module __getattr__ binds at runtime (pass_lsp_cross.c:2552, review5 M1, main same); walrus in a branch's class bases/def defaults not seen (extract_imports.c:844);
  annotation-only `f: int` counted as value binder (extract_imports.c:726/762, withholds a correct link); aliased imports link replaced def via lsp_import_alias; submodule import overwriting package attribute unmodelled;
  same-file call to own def ignores branch rebind; star import of branch-only binder does not override; function-local del of non-decorated name still links; `global` write in __init__ function not followed;
  fastapi_depends still builds IMPORTS map (pass_calls.c:1874); FastAPI lsp_cross_prepare ~535 ms vs ~35 ms on main.
- C/C++: operator[]/->/bool/copy-ctor/delete past depth cap silent; local-class methods past cap keep default reason; lambda return-type inference wrong links (c_lsp.c:2812-2827);
  registry unique_name fallback links a call the LSP declined; `#line` after same-line comment not blanked (preprocessor.cpp:455/619); out-of-range lines from .h/.cpp pairs sharing a QN.
- Rust: LSP/modtree link a registry dep to a same-named workspace member (rust_lsp.c/rust_modtree.c); virtual-workspace file CALLS without IMPORTS; no test for trait-in-scope index fail-closed path.
- Cross-cutting: SEMANTICALLY_RELATED shifts corpus-wide on any CALLS change (graph-identity check.sh should class SR separately); sequential vs parallel driver give two print() calls in one function different lines;
  Windows subprocess tests uncompiled; backoff test SIGALRM; memory-core baseline pass_lsp_cross.c 25->24.
Remote: origin holds only main (16 Oct: 76 stale branches deleted — 67 merged, 8 identical on upstream, 1 superseded Dependabot bump; the backup bundle was trashed 17 Oct by owner decision). The earlier "644" count included 574 upstream/* tracking refs.
