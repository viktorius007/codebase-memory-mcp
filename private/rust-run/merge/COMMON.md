# Shared brief — read first

## Why this work exists

`codebase-memory-mcp` builds a code graph (definitions, CALLS, IMPORTS, USAGE, IMPLEMENTS links)
with tree-sitter and a C resolver, with no compiler or language server at index time. Its only
consumers are AI agents, who cannot tell a wrong link from a right one: a wrong link is worse
than a missing one, and a silent stop is worse than a recorded one. On 7 October 2026 ten agents
each fixed one slice of resolver defects on branches `w1/<arm>` from main 61b613db; none is
merged. main has since moved to 7c95c002 (an 8 October precision repair and a 12 October
upstream merge). Your task, after the `---` below, is one step of landing and extending that
work. Other agents work other steps in other clones at the same time and do not know you exist.
The coordinator reads your report header and decides what lands; your report is what lets it do
so without redoing your work.

## Fixed rules

- Commit on the branch named in your task, in your clone; never push; never check out, commit
  to, or merge into `main`. A task that says "no git writes" overrides this: then you commit nothing.
- No rust-analyzer, rustc or cargo may be needed when the tool indexes a repository. Use them
  freely as measurement or generation tools run by hand.
- A link the resolver cannot prove is withheld and recorded with a stop reason from the closed
  set in `src/pipeline/pipeline_internal.h`.
- Fenced, never edited by you: `Makefile.cbm`, `scripts/test.sh`, `scripts/rust-score.sh`,
  everything under `tools/rust-audit/`, `tests/mutation/`, `.github/workflows/`, the lint
  configuration the lint target reads, and everything under `@MERGE@` and `tools/rust-bench/` (the coordinator's gate).
  A gate is passed by changing the code it judges, never the gate. A fenced file that an arm
  branch itself changes (arm H: `.github/workflows/pr.yml`, `tests/mutation/`; arm T:
  `tools/rust-audit/`) lands with the arm as it is — the fence forbids edits of your own.
- Work single-threaded; do not spawn sub-agents.
- Never touch `~/.cbm`, `~/.local/bin/codebase-memory-mcp`, any running daemon, anything under
  `~/Developer/cbm-bench/` (read-only inputs), or any `/tmp`
  directory other than `@SCRATCH@`. Other agents share this machine.
- A command the harness denies is reported under BLOCKERS with the exact error, never worked
  around.
- The `codebase-memory-mcp` MCP tools your session may offer run the installed binary, an older
  build; nothing they return is evidence about the tree you work on.
- `@SCRATCH@` stays under 8 GB (`du -sh`, reported). Send build, test, index and score output to
  files under `@SCRATCH@` and read `tail`/`grep` of them, not the whole file.

## Environment (measured by the coordinator on 14 October)

- Your clone is `@CLONE@`, on the branch your task names, clean at start. Build:
  `make -f Makefile.cbm cbm BUILD_DIR=@SCRATCH@/build -j"$(nproc)"` (about 1 min clean, 10 s with a warm ccache; binary
  `@SCRATCH@/build/codebase-memory-mcp`). A vendored grammar `parser.c` change needs a clean BUILD_DIR.
- Suites: `CBM_RUNTIME_DIR=@SCRATCH@/rt CBM_CACHE_DIR=@SCRATCH@/cache
  scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction"
  BUILD_DIR=@SCRATCH@/build-tests` — 1,800 passed on main 7c95c002. `scripts/test.sh` deletes the
  binary in its BUILD_DIR, so it gets its own. Lint: `make -f Makefile.cbm lint-ci`.
- The gate that decides acceptance for build, suites, lint and scores:
  `@MERGE@/../../../tools/rust-bench/gate.sh @CLONE@ @SCRATCH@` → `@SCRATCH@/gate/SUMMARY.txt` (read this), step logs
  beside it, per-corpus score dirs `@SCRATCH@/gate/score-<corpus>/` and the exact edge-set
  difference against the frozen base in `@SCRATCH@/gate/edges-<corpus>/{removed,added}.tsv`
  (columns: type, source QN, target QN, count). About 12 minutes. Last line `GATE: PASS|FAIL`.
  It empties `@SCRATCH@/gate` at every run: copy anything you need to keep.
- Corpora (read-only) and the frozen base score of main (`rust-base/BINARY_SHA` names the commit), all under `~/Developer/cbm-bench` (`tools/rust-bench/fetch-corpora.sh`, `gen-truth.sh` and
  `baseline.sh` rebuild it; the corpus list and pinned commits are `tools/rust-bench/corpora.tsv`):

  | name | bench root (truth.tsv, refs.tsv) | corpus dir | frozen base score |
  |---|---|---|---|
  | codex | `codex` | `codex/codex/codex-rs` | `rust-base/codex` |
  | ruff (held-out) | `ruff` | `ruff/ruff` | `rust-base/ruff` |
  | pm | `pm` | `pm/pm` | `rust-base/pm` |
  | bevy | `bevy` | `bevy/bevy` | `rust-base/bevy` |

  Base figures are the `calls ALL` and `wrong-or-unconfirmed pairs` lines of `rust-base/<name>/score.log`. Each base dir holds `score.log` (summary),
  `cache/score.db` (the graph: tables `nodes`, `edges`; view `unresolved_sites`) and `g_*.tsv`.
  jmap and jmapc, which the wave-1 reports also score, are unavailable (pinned checkouts lost):
  four corpora are the measure.
- Score one corpus by hand: `CBM_RUST_BENCH=<bench root> CBM_RUST_CORPUS=<corpus dir>
  scripts/rust-score.sh <binary> <out-dir under @SCRATCH@>` (codex 95 s, the others ≤ 35 s;
  summary on stdout, graph at `<out-dir>/cache/score.db`).
  `python3 @MERGE@/../../../tools/rust-bench/edgediff.py <base.db> <new.db> <out-dir>` writes the exact edge-set
  difference between two graphs and prints counts by type.
- Index any directory without the daemon: `CBM_CACHE_DIR=<dir> CBM_RUNTIME_DIR=<dir>
  scripts/index-local.sh <binary> <repo-dir> <name>` → `<dir>/<name>.db`.
- The fork's design record is `docs/RUST_GRAPH_STATUS.md` in your clone.

## The report (your final message)

Open with this header, one field per line, then `---`. An unfillable field reads `none`.

```
STATUS: DONE|PARTIAL|BLOCKED
COMMIT: <sha on your branch, or none>
CHANGED: <n files, diff against your start commit>
OPEN: <n unresolved items>
BLOCKERS: <one line, or none>
GATE: PASS|FAIL|not run
SCORE-codex: calls <credited>/<truth> flagged <n>  (base 225798/229961 flagged 1)
SCORE-ruff: calls <credited>/<truth> flagged <n>  (base 103252/112300 flagged 1)
SCORE-pm: calls <credited>/<truth> flagged <n>  (base 39680/47116 flagged 74)
SCORE-bevy: calls <credited>/<truth> flagged <n>  (base 60018/84295 flagged 76)
DISK: <du -sh @SCRATCH@>
<task-specific count lines your task names>
---
```

Then, in this order: what changed and why (a line or two per file or merge); evidence per
acceptance criterion (the command, the output line, the log path) — a criterion with no
executed evidence is reported as not met; edge-set difference per corpus (counts by type, the
classes and their explanation, the list paths); what you ruled out or assumed (assumptions
marked as such); OPEN items (file:line and how you saw each); what you did not check. Never fill
a gap with a plausible statement. When the report is written, terminate.
