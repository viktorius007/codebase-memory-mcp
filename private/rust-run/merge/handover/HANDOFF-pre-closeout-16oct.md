## STATE — 16 Oct 01:10: main = origin/main = ac66c020 (771e0398 + review-fix merge). Iteration-2 product lanes LANDED (user cleared "c_lsp chain test and the test flakes"); Python revert 55a4bd8f still HELD.
Landed: fb3a1ecc venue-parity stub speaks --list-suites (plain test.sh had aborted at Step 0j since b4407ba4); 2dac0d56/1e3409f2/57b422a7/771e0398 merges of lanes clsp-receiver (c_lsp.c memo window, 34 s -> 6 s),
frontend (grace window seam, test waits on poll count), subproc (test clock + shutdown-phase accessor; cancel/grace/settle tests assert transitions), clsp-binary (ancestor-path classifiers, 71 s -> 15 s),
clsp-operator (operator branch opens the window into the left operand, 15 s -> 0.4 s). Graphs identical before/after on React Native 0.88 C++, Bitcoin Core v27.0, repo C sources, mixed corpus (reviewer: 4 corpora x 3 binaries + raw dump).
Gate PASS on 57b422a7 (/tmp/iter2-gate), lint-ci PASS on 771e0398, full plain test.sh 413 s real, suites 10283/3/11, post-wave 99 s; new wave floor = cli 154 s, pipeline 72, test_impact_git 65.
Review (reports/iter2-review.md) FAIL on process: 0 blocking; F-1 memo-reset untested, F-2 grace length untested, F-3 duplicated labeled-argument kind list, F-4 no reuse query in commit bodies -> CLOSED by review-fix (a3c52775..2016dea9, merged ac66c020): mutants M4/M5/M6 each caught by a new test (verified independently), gate PASS, pushed. F-4 (reuse queries in the four lane commit bodies) not fixable retroactively.
OPEN product defects (recorded, unowned): CALLS edge `line` differs run-to-run on same binary (648/357,880 edges on repo C corpus, values past EOF; receiver lane); C++ init-capture [x = x.b()] evaluated after rebinding -> wrong link (review N-2); walk depth cap 512 leaves operator links >509 unresolved (N-3); a += a += ... and (a+b).f() chains still per-link; Rust handle_usages/reference_name still ts_node_parent per reference; Windows subprocess tests rewritten uncompiled; backoff test SIGALRM storm not certain (needs sleep seam in compat.c).
Scratch trashed. C++ corpora for graph-identity checks kept at /tmp/cbm-cpp-corpora/{react-native-0.88,bitcoin-v27} with graphcmp.py + index.sh (gate still has no C/C++ corpus: open item). Agent reports land only in their final message (Write refuses private/ from worktrees): extract with /tmp/iter2-lastmsg.sh <transcript> <out>.
Binary installed ~/.local/bin/codebase-memory-mcp = a3444225 build; product changed since (771e0398) — not reinstalled.

## LIVE LANES — 15 Oct 14:20 (current; older CHAIN entries below are history)
main = ba3ad3b2 (ff from integ/w1 at 14:15; gate PASS 1922). FXT lane (Sonnet high, /tmp/cbm-m/fxt, fix/fixture-target, out/FXT.report.md) added 14:35: `make fixtures` compiler-truth target. Branches left: main, integ/w1, fix/e61, fix/h5h (unmerged
Python alternatives, superseded by fix/s5h — user decides). Clones: /tmp/cbm-m/{rfx-s,pfx-s,m8,rev2}; scratch /tmp/{RFX,PFX,M8};
keep /tmp/{RW1,RW2,RW3,FIN2,FIN6}. Running: RFX (Sonnet high, Agent tool, fix/rw3-rust, brief merge/tasks/RFX.filled.md,
report merge/out/RFX.report.md), PFX (Sonnet high, fix/rw3-py, PFX.filled.md, out/PFX.report.md), M8 (Sol 5.6 medium, codex
exec, /tmp/cbm-m/m8 on 25b15cb1+2 commits, final full suite; report out/M8.report.md). Agents are barred from gate.sh/lint;
the coordinator runs ONE gate on a fresh clone = main + fix/rw3-rust + fix/rw3-py + m8, then ff main. Then D2 docs
(tasks/D2.md placeholders). R-W3 findings and scope per lane: merge/run-ledger.md 14:20 entry.

## LIVE LANES (14 Oct, orchestrator run — read `private/rust-run/merge/run-ledger.md` first)

Sol 6.1 medium agents via `codex exec`, launched 11:02 from `private/rust-run/merge/launch.sh`
(briefs `merge/COMMON.md` + `merge/tasks/*.md`, reports `merge/out/<name>.report.md`, exit stamps
`merge/out/<name>.exit`). Clones: `/tmp/cbm-m/integ` (branch `integ/w1`, M-chain), `/tmp/cbm-m/up`
(branch `integ/up`, `git merge upstream/main` in progress, U1–U3 resolve statically, U4 builds and
commits), `/tmp/cbm-m/std` (branch `w2/std-inventory` from w1/S, STD task). Scratch `/tmp/<name>`.
Gate = `merge/gate.sh <clone> <scratch>` (build, 5 suites, lint-ci, 4-corpus score vs
`score-7c95c002`, exact edge diff via `merge/edgediff.py`). ruff base re-frozen on arm T truth
(103252/112300 fl 1). `review/gen-truth.sh` paths fixed; jmap/jmapc lines commented (sha lost).
rustdoc JSON installed on `nightly-2026-08-10` (rustc 1.99.0-nightly). Base binary `/tmp/cbm-base/`.
BENCH RELOCATED 11:55: corpora now live at `~/Developer/cbm-bench/{codex,ruff,pm,bevy}` (outside any
git tree); `private/rust-run/bench` is a symlink to it. Reason, proved: a corpus inside this repo's
working tree inherits the repo's git context — `.gitignore` `bin/` dropped every `src/bin/*.rs`
(codex −103 calls), the branch node became `main`, FILE_CHANGES_WITH edges came from the cbm repo's
history. The base binary rescored from the new location reproduces `score-7c95c002` with an empty
edge diff on all four corpora, so the frozen base stands. gate.sh passes realpaths and now runs the
suites alone (a timing guard at tests/test_extraction.c:6529 failed under parallel scoring).
CHAIN 13:55 (15 Oct): R-W3 FAIL 7 blocking (Rust B1-B3 new wrong links from 416d4559; Python B4-B6 from one-binding-per-file map). Fix lanes launching: Rust on rfx-s (Sonnet high) + rfx-e (Sol 6.1 fast); Python on pfx-s (Sonnet high). integ/w1 stays ba3ad3b2 until fixed; NOT ready for main.
CHAIN 13:30 (15 Oct): integ/w1 = ba3ad3b2 (+fix/s5h; gate PASS 1922; scores as 25b15cb1; graphs /tmp/FIN6). R-W3 Opus re-review launching on /tmp/cbm-m/rev2. M8 mutation lane 7/11 judged (1 survivor trait-before-value-scope, known).
CHAIN 13:25 (15 Oct): Python repair chosen = Sonnet-high ba953975 (fix/s5h; 3735 restored, 0 wrong links, identity-decorator proof) → merged onto 25b15cb1 = ba3ad3b2 in /tmp/cbm-m/fin6, gate running /tmp/FIN6. H5H merge (fin5) superseded. All 7 repair arms scored (exp/README.md). M8 running. DATA LOSS 12:13–12:37: experiment /tmp scratch + truth table deleted externally; edge diffs in merge/exp/data.
CHAIN 12:30 (15 Oct): integ/w1 = 25b15cb1 (+B1G; gate PASS 1922; codex 226144 fl1, ruff 104046 fl1, pm 39983 fl73, bevy 61327 fl35; graphs /tmp/FIN4). M8 launching on /tmp/cbm-m/m8. Python repair candidates E61=H5H (identical graphs), Opus×2 + Sonnet high pending.
CHAIN 11:35 (15 Oct): B1G 416d4559 DONE (gate PASS, scores up, fixtures verified by coordinator) → merged onto 1af757f5 = 25b15cb1 in /tmp/cbm-m/fin4, gate running /tmp/FIN4; ff integ/w1 on PASS, then M8. Python chain: M1G b43c2c17 not landed; repair candidates = E61 4547bb47 (6.1 fast, PARTIAL-PASS) + 5 arms still running (model experiment, see merge/exp/README.md). Screener round (6 Haiku-low) running.
CHAIN 09:40 (15 Oct): M1G b43c2c17 gate PASS but FAIL on my read — 3,868 ruff + 603 codex/pm same-module Python USAGE/WRITES silently dropped (top-level assignment → blocked import-map entry → sentinel unstripped in resolve_lang). M1G2 running on /tmp/cbm-m/m1g, scratch /tmp/M1G2; B1G running. Nothing fetched; integ/w1 still 1af757f5.
CHAIN 06:50 (15 Oct): repo `integ/w1` = 1af757f5 (+M7: 22/23 full-suite failures closed; gate PASS 1920; codex 225948 fl1, ruff 103588 fl1, pm 39983 fl73, bevy 61254 fl38). Remaining full-suite failure = invalid-Rust fixture lrp_rust_s6_trait_method → M8 (brief drafted) after B1G. B1G/M1G running on /tmp/cbm-m/b1g, m1g. Tip graphs /tmp/FIN3/gate/score-*.
CHAIN 06:30 (15 Oct): R-W2 Opus re-review on 32d11a53 FAIL 5 blocking (2 new from B1F: R1 Copy-via-macro wrong link, R3 unrelated same-named impl vetoes call = most of B1F's score loss; 2 new from M1F: P1 star import ignores __all__/underscore, P2 statement order; 1 pre-existing on main: P3 single-candidate unbound Python name published, F1 rule). Fix lanes B1G (/tmp/cbm-m/b1g, high) and M1G (/tmp/cbm-m/m1g) running from 32d11a53; M7 running. Keep /tmp/RW2 and /tmp/RW1.
CHAIN 05:45 (15 Oct): repo `integ/w1` = 32d11a53 (+B1F all-receiver probe ranking, +M6 tooling/mutation re-base, +D1 docs; gate PASS 1920; codex 225937 fl1, ruff 103587 fl1, pm 39983 fl73, bevy 61224 fl38 — B1F withheld ~800 truth-right sites with recorded stops, wave-3 recovery item). R-W2 Opus re-review running (/tmp/RW2). M7 (23 full-suite failures, all branch-caused; main is 9856/0) running on /tmp/cbm-m/m7. Tip graphs /tmp/FIN2/gate/score-*.
CHAIN 01:15 (15 Oct): repo `integ/w1` = c8bae5fa (M1F: Python relative/star-import INHERITS recovered; gate PASS 1916; scores unchanged from f10b3151). B1F (probe-order for all receiver shapes) and M6 (full suite + mutation-rust, 6 commits on 582bc055) still running. Tip graphs /tmp/M1F/gate/score-*/cache/score.db.
CHAIN 23:30: repo `integ/w1` = f10b3151 (gate PASS 1916; codex 226152 fl1, ruff 103883 fl1, pm 39983 fl73, bevy 61267 fl41). Opus review R-W1 running on /tmp/cbm-m/rev; M6 still running on /tmp/cbm-m/m6.
CHAIN 23:15: candidate final tip f10b3151 = 582bc055 + fix/m5b (probe-order ranking, M5c) + fix/f1b (external-import binding withhold); repo staging `integ/w1-fin`, gate running (/tmp/FIN). M6 full suite on 582bc055 found 7 failures outside the gate suites (lsp_resolution_probe 3, edge_types_probe 4), agent attributing. OPEN for after landing: 41 pre-existing Python USAGE wrong links on ruff (external imports → same-named fixture defs; private/rust-run/merge/out/F1b-ruff-external-bound-final.txt).
CHAIN 22:30: repo `integ/w1` = 582bc055 (U6: upstream 24d02512 merged; gate PASS 1916 tests; scores unchanged codex 226152 fl1 / ruff 103878 fl1 / pm 39983 fl73 / bevy 61267 fl41; Rust edge diff 0). M6 running (/tmp/cbm-m/m6: full test.sh + lint + mutation-rust). F1b gate PASS at 7c8239fa, report pending → merge next. Then R-W1 Opus review, then land on main.
CHAIN 21:40: repo `integ/w1` = 8109dd1d (gate PASS 1868 tests; codex 226152 fl1, ruff 103878 fl1, pm 39983 fl73, bevy 61267 fl41). Start graphs for U6/F1b: /tmp/U6pre/gate/score-*/cache/score.db.
CHAIN 21:20: F1 d5ca97b0 merged with 6baa0451 → 8109dd1d (repo branch `integ/w1-f1m`, gating in /tmp/U6pre). F1 recovered 71 ruff edges; 9 are wrong (ruff playground `Diagnostic` imported from external `ruff_wasm`, linked to ty's interface) → F1b running (/tmp/cbm-m/f1b). U6 upstream merge running (/tmp/cbm-m/u6a). M5b superseded by W2b's seam fix. Briefs drafted: M6 (full suite + mutation-rust), R-W1 (Opus review).
CHAIN 20:50: repo `integ/w1` = 6baa0451 (main + ten arms + w2/std-inventory + W2b verdict). Gate PASS 1868 tests; codex 226152 fl1, ruff 103878 fl1, pm 39983 fl73, bevy 61267 fl41 (base 225798/103252/39680/60018). W2b also fixed the H/S mutable-receiver seam; M5b's own fix is superseded (regressed 3 S tests). F1 gating on /tmp/cbm-m/f1 (d5ca97b0). Then U6 upstream merge, M6, Opus review, land on main.
CHAIN 18:50: M5 (arm S) landed PARTIAL as 3cb44ed1, staged in repo as `integ/w1-s` (repo `integ/w1` still e84f786e). One failing test (arm H guard tests/test_pipeline.c:14549, H/S receiver-precedence seam at rust_lsp.c:14444) → M5b running on /tmp/cbm-m/m5. F1 running in parallel on /tmp/cbm-m/f1 (from integ/w1-s). M5's 2222 'unrecorded removed CALLS' = 2162 B2 block-scope re-keys (#B prefixes) + 60 real (1 codex, 48 ruff non-Rust→F1, 1 pm shell→TOML→F1, 10 bevy bevy_reflect no-record→OPEN). Scores at 3cb44ed1: codex 223074 fl1, ruff 103453 fl1, pm 39926 fl73, bevy 60800 fl41. Keep /tmp/M5 (start graphs for both briefs) until M5b and F1 land.
CHAIN 15:00: integ/w1 (repo) = e84f786e (Q,D,H,T,P,B1,Y2,Y1,B2); /tmp/cbm-m/m5 = b0aa8aec (+ fix/arena merged) with M5 (arm S) running. Scores at e84f786e: codex 225983 fl 1, ruff 103572 fl 1, pm 39709 fl 73, bevy 60251 fl 51, all tests pass except arena test (fixed by fix/arena). STD lane DONE 15:32: branch `w2/std-inventory` = f898f5b9 (from w1/S 7921a99a): tools/std-inventory/ (generate.py, probe.py compile oracle `make probe`, measure.py), header internal/cbm/lsp/rust_std_inventory.h 32,123 rows, stamps 111 complete / 11 complete_except_deref / 4 incomplete, 0 callable-and-unlisted over 985k compiled probe calls (Opus review R-STD found 5 BLOCKING omissions in STD2; STD3 closed them). OPEN: dyn Any/dyn Error incomplete, provenance labels name impl trait not declaring trait, 223-name universal over-list (safe). NEXT W2b (after M5 lands): resolver verdict listed→std / unlisted-on-complete→project / else withhold+record, based on integ/w1 + w2/std-inventory merged. Earlier: integ/w1 = a3bcd63a (Q,D,H,T,P,B1,Y2,Y1 landed; scores codex 225980 fl 1, ruff 103451 fl 1, pm 39688 fl 73, bevy 60146 fl 54); only failing test = arena budget test_extraction.c:204 (M2b fix 482376ff on /tmp/cbm-m/m2, to merge in). M4 (B2) running on /tmp/cbm-m/m4. Then M5 (S), merge M2b fix, gate, F1, merge integ/up, M6 full test.sh + mutation-rust, Opus review, land on main.
LANDED 12:57: integ/w1 = b173c0e2 (main + Q,D,H,T + D fixture fix a8973fa5 + M1b registry repair), G1 gate PASS; branch also fetched into this repo as `integ/w1`. Next in chain: M2 (P,B1) → M3 (Y2,Y1) → M4 (B2) → M5 (S) → M6 full test.sh + mutation-rust →
merge integ/up into integ/w1 → Opus-high review → land on main. UPSTREAM MERGED 13:32: branch `integ/up` = 7feef206 (upstream/main 24d02512 into main 7c95c002, plus
a fix for three wrong Rust IMPORTS→config.md edges that upstream's stem containment admitted; gate
PASS, Rust edge diff 0, scores = base). To be merged into integ/w1 after M5 (expect conflicts where
arms and upstream both touched pass_calls/pass_parallel/pipeline_internal.h/cbm.h/test_pipeline.c).
QUEUED F1 (after M5, before landing on main): A1 audit (merge/out/A1.report.md) found Q's non-Rust
multi-candidate refusal (registry.c:1986-1990 on integ/w1) loses ~232 right ruff links (Python
INHERITS 19/32, TSX USAGE 18/36, Python CALLS 78/333) while removing ~4,624 wrong ones, and that
USAGE/WRITES/INHERITS/THROWS withholds have NO stop record (8.5% recorded) — a silent-stop violation.
Fill @START@/@STARTSCORES@/
@CONFLICTS@ in `tasks/M2..M5.md` from the previous step's SUMMARY before each dispatch.

## State (2026-10-14 — analysis session; nothing implemented, nothing merged)

`main` = 7c95c002 = `origin/main` (pushed 12 Oct; installed binary `~/.local/bin/codebase-memory-mcp`
built from it 12 Oct 22:18). Sessions since the 7 Oct handoff: 8 Oct 17637fef "restore semantic graph
precision" cleared the 14 failures the old handoff listed; 12 Oct merged upstream 4d9592b5
(merge body documents every meeting point; gates: 9,856 C tests passed, 0 failed, lint-ci pass).
Neither session re-scored the Rust graph. This session did (below). The ten wave-1 arm branches
are STILL unmerged and the mutation lane is still inert.

### Verified on 14 Oct (fresh build of 7c95c002, binary trashed; rebuild with
`make -f Makefile.cbm cbm BUILD_DIR=/tmp/cbm-trial -j8`)

| Corpus | Calls linked today | base (7 Oct, arm T truth) | flagged today / base |
|---|---|---|---|
| codex | 225,798 / 229,961 (98.2%) | 225,799 | 1 / 1 |
| ruff (held-out) | 103,252 / 112,301 (91.9%) | 103,252 | 1 / 1 |
| pm | 39,680 / 47,116 (84.2%) | 39,680 | 74 / 74 |
| bevy | 60,018 / 84,295 (71.2%) | 60,037 | 76 / 76 |

Five Rust suites (`scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract
extraction" BUILD_DIR=<dir>`): 1,800 passed, exit 0. Full `scripts/test.sh` not rerun this session.
jmap and jmapc NOT scored: pinned jmap sha 78b20f1 is not in `~/Developer/jmap`; jmap-client is
`~/Developer/jmap/crates/jmap-client` (no separate repo). Find the jmap commit the truth was built
from (arm T `truth/jmap/truth.log`) or regenerate truth at a new sha.

### Measurement infrastructure — RELOCATED this session (the only current base)

The repo moved `~/Projects/github` → `~/Developer/github` between 7 and 12 Oct; `~/.cache/cbm-rust-bench*`
and every `/tmp/cbm-score-*` were lost. Rebuilt and stored under `private/rust-run/` (gitignored):
- `private/rust-run/bench/<name>/` for codex, ruff, pm, bevy: corpus checkout at the pinned sha
  (codex `codex/codex-rs` @bc98fc69f8, 4,698 files, from `git archive`; ruff `ruff` @b8c5e73a,
  salvaged, with `ruff.scip`; pm `pm` @dffb94f165; bevy `bevy` @7107093) plus `truth.tsv`,
  `refs.tsv` (codex/pm/bevy = arm T regenerated truth; ruff = the original 6 Oct truth, 223,306 rows;
  arm T's ruff truth with 13 more rows is in `wave1/truth/ruff`).
- `private/rust-run/score-7c95c002/<name>/` = today's frozen scores (score.db, g_*.tsv, join/precision/
  misses/families.txt); truth symlinks repointed at `bench/`. This replaces `/tmp/cbm-score-*-aa48ba96`
  as the comparison base for every future merge.
- Score command: `CBM_RUST_BENCH=$R/bench/<name> CBM_RUST_CORPUS=$R/bench/<name>/<corpus-dir>
  scripts/rust-score.sh <binary> <out-dir>` (codex 95 s, ruff 28 s, pm 14 s, bevy 32 s on this machine).
  Out-dir under /tmp is wiped per run; anything you must keep goes under `private/rust-run/`.
- Oracles: `private/rust-s3/codex.scip`, `bench/ruff/ruff.scip`. pm/jmap/bevy `.scip` lost; regenerate
  with `private/rust-run/review/gen-truth.sh` AFTER fixing its hardcoded `/Users/viktor/Projects/...`
  paths (REPO, RA, and the four `gen` source paths; rust-analyzer checkout is now
  `~/Developer/github/rust-analyzer`; toolchain rust-analyzer is 1.99.0, the truth was 1.95.0).
- Source-of-truth method, for the record: oracle = `rust-analyzer scip`; call sites enumerated by
  rust-analyzer's parser (`tools/rust-audit/tsdiff`), never by the grammar under test. Grammar fork
  = tree-sitter-rust v0.24.2 (77a3747, still upstream's latest tag) + orchard lexical fixes, checked
  against rust-analyzer's parser on 83,552 files. Six corpora: codex tuning, ruff held-out, pm/jmap/
  jmapc/bevy never seen by the fleet. The 7 Oct review (`review/REVIEW.md`) found the figures do
  not transfer (bevy 71%, 72 wrong links; jmap 68%, 10 wrong); root cause: no model of std
  inherent methods / trait scope. `docs/RUST_GRAPH_STATUS.md` is the authority and is current to 7 Oct.

### Wave 1 — ten `w1/<arm>` branches, all based on 61b613db, none merged

Dry-run `git merge-tree --write-tree main w1/<arm>` on 14 Oct: D, T, Y2, B2, S clean; H conflicts
in `.github/workflows/pr.yml`; P in `internal/cbm/cbm.h`; B1 in `src/pipeline/pipeline.c`; Y1 in
`tests/test_pipeline.c`; Q in `src/pipeline/pass_calls.c`, `pass_parallel.c`, `tests/test_pipeline.c`.
Arm reports: `private/rust-run/wave1/out/w1-<arm>.report.md` (+ `.r.report.md` resumes), ledger
`wave1/run-ledger.md`, briefs `wave1/{COMMON.md,tasks/}`. Expected score deltas per arm are in the
report headers (SCORE-<corpus> lines). Arm statuses unchanged from 7 Oct:
Q merge as is | D, H, T done | P, B1 recovered | Y2 DONE | Y1 PARTIAL (89 std pairs) |
B2 PARTIAL 6/9 (f6a4200f) | S PARTIAL 6/8 (7921a99a; codex −2,884 by design: std withhold; 9 OPEN).

Mutation lane: 0 of 8 `tests/mutation/patches/*` apply at main; no CI job runs it (arm H fixes both).

### Upstream

`upstream/main` = 24d02512 (13 Oct), 171 commits past the merge-base 4d9592b5, all "Break-glass merge
PR #…" from 10–13 Oct; none Rust-named, but dry-run merge onto main conflicts in 29 files incl.
`src/pipeline/{pass_calls,pass_parallel,registry,pass_lsp_cross,pass_pkgmap,pass_definitions,
pass_semantic,pass_usages,pipeline,pipeline_incremental}.c`, `pipeline_internal.h`, `store.h`,
`internal/cbm/{cbm.h,extract_imports.c,extract_unified.c,lsp/cs_lsp.c,result_compact.c}`,
`tests/{test_mcp,test_pipeline}.c`, README/THIRD_PARTY/MANIFEST/checksums. Fork is 624 commits ahead.
12 Oct merge method that worked: parallel agents per conflict file group, then full gate.

### Next session, in order (owner agreed the order on 14 Oct report; nothing started)

1. Merge arms onto a branch from main: Q, D, H, T, P, B1, Y2, Y1, B2, S. Gate per merge: five suites,
   `make -f Makefile.cbm lint-ci`, four-corpus score vs `score-7c95c002`, exact edge-set diff
   (key: source QN, target QN, edge type) read before landing. Then full `scripts/test.sh` with the
   installed CBM stopped. Then `make -f Makefile.cbm mutation-rust`.
2. Install arm T's ruff truth (`wave1/truth/ruff`) into `bench/ruff` and re-freeze.
3. Verification pair over the merged diff (Sol high + Opus 5.5 high, `review/` templates).
4. Merge upstream (171+ commits, 29 conflict files) — after the arms, so each conflict is paid once.
5. Wave 2: `private/rust-run/wave2/PLAN.md` (std inventory first), fed the OPEN items from B2 and S.
6. Update `docs/RUST_GRAPH_STATUS.md` figures and "Repository state" (it still says main not pushed).

### `private/` paths still needed (rules in `private/CLAUDE.md`)
- `bench/`, `score-7c95c002/` (new, above). `wave1/` (ledger, briefs, reports, `truth/`, EXPERIMENT,
  `sol-profile-proposed.md`, `close-gates/`), `wave2/PLAN.md`, `review/`, `gate-17637fef/` (8 Oct gate
  logs; python ScipScorerTests failures there were a Python <3.10 `str | None` environment issue,
  resolved 12 Oct), `run-ledger.md`, `SCORE.md`, `summary-*.txt`, `audit/`, `orchard/`, `rust-s3/`,
  `benchmarks/` (codegraph comparison), `retired/` (75 patches), `stashes/` (3 patches), `fleet/`.
- `~/Developer/cache-salvage/` now holds only `pm-drafts` (not ours).

### Standing rules (unchanged)
No skills installed by cbm (`install --skip-config`); owner is a layperson; wrong link worse than
missing; silent stop worse than recorded; `rm` is hook-banned, use `trash`; never edit corpora under
`bench/`; never touch `~/.cbm` or the running daemon; never accept a sandboxed gate result; Sol briefs:
outcome-bound stop rules, numeric recall floor, fenced files, no sub-agents; clean up `/tmp` as you go.
