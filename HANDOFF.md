# HANDOFF — `callers_total` / ISSUES.md accuracy work (2026-07-29)

Written for a successor with **no prior context**, who should **trust nothing here** and
verify everything. Every factual claim below names the command that produced it. Claims I
did not verify are tagged **UNVERIFIED BELIEF** with what would settle them.

Repo: `/Users/viktor/Projects/github/codebase-memory-mcp`, branch `main`.
Baseline (last commit not mine): `357bfbdc`. My HEAD: `38ff24da` (this document).
**8 commits, all local: 7 of work + this handoff. NOTHING PUSHED.**
`git rev-parse origin/main` → `8d2b9564`.

---

## 0. The 60-second version

`trace_path`'s `callers_total` counted `__file__` (File) nodes as callers. Root cause was
**not** what the old ISSUES.md said. The definition walk and the call walk each compute a
qualified name (QN) for the enclosing function, and `calls_find_source()`
(`src/pipeline/pass_calls.c:437`) joins them by **exact string equality**; on a miss it
falls back to the file's `__file__` node *by design*. Any construct where the two walks
disagreed about the QN produced a File-sourced CALLS edge. I found and fixed three such
disagreements, plus one I introduced myself, plus a reporting-side guard.

Measured effect, agent workspace (~9k nodes): File-sourced CALLS **240 → 15**, total CALLS
**9259 → 9257** (attribution moved; call information was not discarded).

---

## 1. WHAT WAS DONE — the 7 commits

Verify the set with:
```bash
cd /Users/viktor/Projects/github/codebase-memory-mcp
git log --oneline 357bfbdc..HEAD
git diff --stat 357bfbdc..HEAD
```

### `ab8fa46c` fix(pipeline): cfg-gated Rust calls attribute to their function
- **Root cause.** `extract_defs.c` folds a `#[cfg(...)]` predicate into a free function's
  definition QN (`foo` → `foo#cfg(unix)]`) so cfg-gated twins don't collide on upsert
  (pre-existing behaviour, issue #495). The call walk applied no such suffix, so the join
  missed for every call inside a cfg-gated fn.
- **Change.** Suffix builder now lives once, `cbm_rust_cfg_qualified_name`
  (`internal/cbm/helpers.c:930`, declared `internal/cbm/helpers.h:57`), called from both
  the def side (`internal/cbm/extract_defs.c:3450`) and the call side
  (`internal/cbm/extract_unified.c`, in `compute_func_qn`).
- **Test.** `tool_trace_cfg_gated_caller_not_attributed_to_file_node`,
  `tests/test_mcp.c:3931`.
- **Red proof observed** (against unfixed code):
  ```
  [cfgattr] FAIL cfg-gated caller absent from callers:
    callers_total: 1
    callers: 1  (rows: name hop; ...)
    cfgattr-proj.lib.rs:
      __file__ 1
  FAIL tests/test_mcp.c:3965: strstr(r, "cfgattr_gated_caller") is NULL
  ```
- **Live evidence.** After reindexing with a rebuilt binary, the originally filed repro
  (`ensure_single_link_regular_file`, inbound, include_tests) lost its `__file__` row and
  gained two real cfg-gated test callers. File-sourced CALLS 240 → 56.

### `ad8ee36e` fix(pipeline): strip generic args in call-scope impl QNs
- **Root cause.** The def walk strips generic arguments from an impl's type
  (`Holder<T>` → `Holder`) so the Method node is `...Holder.run`; the call walk used the
  `type` field's raw text, producing `Holder<T>.run`.
- **Trigger is angle brackets in the impl's `type` text**, not genericity: `impl<T> Trait
  for Plain` has generic params but no brackets in `type` and was never affected.
  Lifetimes bite too (`Sink<'a>`).
- **Change.** One helper `cbm_strip_generic_args` (`internal/cbm/helpers.c:916`, declared
  `helpers.h:68`), called from `extract_defs.c:4543` (type), `extract_defs.c:4552` (trait),
  and `extract_unified.c:715` (call-scope).
- **Test.** `tool_trace_generic_impl_caller_not_attributed_to_file_node`,
  `tests/test_mcp.c:4021`.
- **Red proof observed:**
  ```
  [genattr] FAIL generic-impl caller absent from callers:
    callers_total: 1 / genattr-proj.lib.rs: __file__ 1
  FAIL tests/test_mcp.c:4053: strstr(r, "genattr_generic_caller") is NULL
  ```
- **Live evidence.** File-sourced CALLS 56 → 42; `agent-cli/src/stream_tee.rs` (the file
  this was diagnosed on) dropped to zero File-sourced calls.

### `3eb76443` fix(pipeline): nested Rust `fn` attributes to the enclosing function
- **Root cause.** The def walk emits a node for a `function_item` and does **not** descend
  into its body, so a nested `fn` helper gets **no node**. `push_boundary_scopes` pushed a
  `SCOPE_FUNC` for it anyway, so the enclosing QN named a function nothing in the graph
  carries.
- **Change.** Extended an existing OCaml-only `skip_nested` guard to Rust —
  `internal/cbm/extract_unified.c:1393`. Only the outermost function pushes a scope.
- **Test.** `tool_trace_nested_fn_caller_not_attributed_to_file_node`,
  `tests/test_mcp.c:4107`.
- **Red proof observed:**
  ```
  [nestattr] FAIL outer fn absent from callers:
    callers_total: 1 / nestattr-proj.lib.rs: __file__ 1
  FAIL tests/test_mcp.c:4140: strstr(r, "nestattr_outer") is NULL
  ```
- **Live evidence.** File-sourced CALLS 42 → 26. Total CALLS 9259 → 9258 (the single
  delta is a call that became a self-edge), i.e. attribution moved rather than call
  information being discarded. See §5 for what I did *not* verify here.

### `7fa8e9df` fix(pipeline): do NOT cfg-suffix impl-method QNs — **a regression I caused**
- **What happened.** `ab8fa46c` suffixed uniformly on the call side. But the def walk is
  **asymmetric**: `extract_rust_impl` emits impl METHODS as plain `<type_qn>.<name>` with
  **no** cfg predicate, while `extract_func_def` suffixes free functions. Suffixing
  uniformly invented a QN no node carries and broke **9 previously-correct call sites**.
- **How it was caught.** By re-measuring live data, **not** by the suite — the suite was
  green through the regression. I compared the post-fix File-sourced rows against the
  pre-fix set and found files (`tool_registry.rs`, `baseline.rs`, `projection.rs`,
  `capture.rs`) that had **zero** File-sourced rows before my change and non-zero after.
- **Test.** `tool_trace_cfg_gated_method_not_attributed_to_file_node`,
  `tests/test_mcp.c:4195`. It guards the asymmetry from the other side: the `ab8fa46c`
  test reddens if a free function stops being suffixed; this one reddens if a method
  starts being.
- **Red proof observed** — produced by *reintroducing the exact defect* (restoring the
  uniform suffix), which is the strongest form of red proof available here:
  ```
  [cfgmethod] FAIL cfg-gated method absent from callers:
    callers_total: 1 / cfgmethod-proj.lib.rs: __file__ 1
  FAIL tests/test_mcp.c:4228: strstr(r, "cfgmethod_gated_method") is NULL
  ```
- **Live evidence.** File-sourced CALLS 26 → 15, completing 240 → 15.

### `b87e995c` fix(mcp): non-callable nodes out of callers/callees and their totals
- **Why a reporting fix at all.** The extraction fixes remove *causes*, not the
  *category*: a call written at file/module scope (a `const` initialiser) has no enclosing
  function and is correctly sourced at the file. All 15 survivors are of that kind. The
  invariant is about what the tool **claims**, so reporting is the right place for it.
- **Change.** `trace_split_unattributed` (`src/mcp/mcp.c:6141`) and
  `trace_row_is_uncallable` (`src/mcp/mcp.c:6124`); called at `src/mcp/mcp.c:6483-6484`.
  File rows move to `unattributed_total` + `unattributed_files`, **partitioned, not
  dropped** — the edge is real evidence a call exists in that file.
- **Scoped to a pure-CALLS walk.** For any other `edge_types` a File node is a legitimate
  result, so the split does not apply.
- **Test.** `tool_trace_file_node_excluded_from_callers_total`, `tests/test_mcp.c:4264`.
  It drives the **store directly**, not the indexer, so it pins the reporting contract
  independently of which extraction paths currently mint such edges.
- **Red proof observed** — by disabling the split on otherwise-final code:
  ```
  [filetotal] FAIL callers_total counts the File node:
    callers_total: 2
    callers: 2
      ft_real_caller 1
      __file__ 1
  ```
- **Live evidence.** `normalize_private_directory`: `callers_total` 7 → 6, with the file
  listed under `unattributed_files` instead of among the callers.

### `fdfadee7` docs(issues): rewrite ISSUES.md
Removed the two fixed items from Open (the file's own convention), corrected the false
`--edge-types` claim into "Confirmed working", re-filed CLI cost with fresh numbers,
filed the new cfg-QN item. Details in §3.

### `31771603` docs(mcp): document `unattributed_files`; correct two notes
The tool description documented `via_port_*` but not the new `unattributed_*` keys — an
agent would have met an undocumented key. Also corrected a comment that claimed "no
allocation" (false; it uses a scratch buffer) and added measurement conditions to the
ISSUES.md timing table.

---

## 2. WHAT WAS NOT DONE

### Test-suite status — read this carefully
- **`mcp` suite: 190 passed, 0 failed, 2 skipped.** Baseline was 185/0/2; I added 5 tests
  and lost none. Reproduce: `./build/c/test-runner mcp`. **This is the acceptance gate and
  it is green.**
- **Full suite: I never obtained a fully green run, and I am not claiming one.**
  - One earlier full run **was** green at **6870 passed / 0 failed / 4 skipped**.
  - A later run: **6865 / 5 / 4**. A final "clean run" was **still executing when I was
    retired** — its result is unknown. Log:
    `/private/tmp/claude-502/.../scratchpad/cleanrun.log` (scratchpad is session-scoped and
    may be gone; just re-run).
  - **The 5 failures, and what I established about each:**
    | test | assert | mechanism |
    |---|---|---|
    | `subprocess_quiet_timeout_kills_ignoring_tree` | `test_subprocess.c:510 ASSERT(ready)` | reads a byte from a pipe written by a **forked child** |
    | `cli_activation_quiesce_does_not_wait_on_bootstrap_startup` | `test_cli.c:903 ASSERT(child_ready)` | `read(ready_pipe[0],&ready,1)==1 && ready=='R'` (`test_cli.c:853`), same fork+pipe shape |
    | `lsp_rust_nested_generic_type_no_crash` | `test_stack_overflow.c:891` | `so_extract_crashes` returns `WIFSIGNALED(status)`; child arms `alarm(30)` (`test_stack_overflow.c:440`) |
    | `lsp_java_nested_generic_type_no_crash` | `test_stack_overflow.c:901` | same |
    | `lsp_csharp_nested_generic_type_no_crash` | `test_stack_overflow.c:911` | same |
  - **First two: caused by me, mechanism proven.** I ran `pkill -f codebase-memory-mcp`
    while the suite was running. That pattern matches the **repo path**, so it killed the
    test-runner and its forked children. Verify:
    `echo /Users/viktor/Projects/github/codebase-memory-mcp/build/c/test-runner | grep -c codebase-memory-mcp` → `1`.
    One background run died with exit **144** (SIGTERM). They pass in isolation:
    `./build/c/test-runner subprocess` → `29 passed` on a retry.
  - **Last three: NOT a pkill artifact — reproducible in isolation** under load:
    `./build/c/test-runner stack_overflow_a` → `7 passed, 3 failed`, twice.
    **UNVERIFIED BELIEF (this is the single most important open item):** they are
    watchdog timeouts caused by machine load, not a regression from my commits.
    Evidence *for*: (a) the crash check is literally `WIFSIGNALED`, and the child arms a
    30s `alarm`, so a slow parse is indistinguishable from a crash; (b) the test file's
    own header (`test_stack_overflow.c:795-812`) says a guarded 6000-deep generic "indexes
    in a few seconds — comfortably inside the child watchdog", i.e. a ~10x margin that
    heavy load erases; (c) load averages were **154–211** throughout, from other agents in
    a sibling repo; (d) the failures span **Rust, Java and C#**, but my only
    language-gated change is Rust-only (`CBM_LANG_RUST`), so a common cause in my diff is
    implausible; (e) `git diff --name-only 357bfbdc..HEAD` touches **no** LSP or
    type-parser file, and `cbm_strip_generic_args` is called only from `extract_defs.c` and
    `extract_unified.c` — never from any `*_parse_type_node`.
    **What settles it:** I built a baseline worktree at `357bfbdc` for exactly this and
    the build did not finish before I was retired. Finish it:
    ```bash
    cd <scratchpad>/base-wt            # or: git worktree add /tmp/base-wt 357bfbdc
    make -f Makefile.cbm build/c/test-runner -j16
    ./build/c/test-runner stack_overflow_a
    ```
    **If the baseline also fails these 3, they are pre-existing/environmental and nothing
    in my series caused them. If the baseline passes while HEAD fails, I was wrong and one
    of my commits is implicated — start at `ad8ee36e` (`cbm_strip_generic_args`).**
    Best done on an idle machine; check `uptime` first.
    A `git worktree remove <path>` is needed to clean up; `git worktree list` will show it.

### Deliberately not fixed (with reasons, both filed in ISSUES.md)
- **CLI startup cost.** Lives in the daemon IPC/activation subsystem
  (`src/daemon/ipc.c`, `src/daemon/runtime.c`, `src/cli/cli.c`) — a different subsystem
  from this work with different risk. Filed with measurements so the next reader starts
  from data.
- **cfg predicate text inside node QNs.** Correct and deliberate for attribution (#495),
  but it is a presentation problem with two defensible designs (separate field vs.
  documented QN grammar). Filed rather than guessed at.

### Not done
- No mutation testing (user-gated in this project's conventions; never run).
- No lint/`cargo deny` gate run — this is a C project; I used `make` + `test-runner` only.
- The **adversarial reviewer agent I spawned never reported back**. Its findings, if any,
  are lost. Re-running an independent review of `git diff 357bfbdc..HEAD` is worthwhile,
  focused on §5.

---

## 3. ALL FINDINGS AND WHERE THEY LIVE

| # | Finding | Lives in |
|---|---|---|
| 1 | CLI cost: cold 4.71/4.71/5.78s, warm 1.93/1.95/1.96s, **`--version` 0.00–0.01s**. The `--version` datum rules out image size / dynamic linking / process creation as the cause — it is daemon-path work. Numbers are load-sensitive (a parallel ASan run pushed warm to 12–36s). | `ISSUES.md` Open |
| 2 | Node QNs embed cfg predicate text: `...append_open_rejects_a_fifo_at_the_session_path#cfg(unix)]` — note the unbalanced trailing `]`. A QN is what a consumer copies back into `--function-name`. | `ISSUES.md` Open |
| 3 | **`--edge-types` DOES work.** The old entry's claim that it "does not reach the traversal it names" is **false**. Passing `CALLS`+`OVERRIDE` flips the header to `edges: CALLS,OVERRIDE` and renames the leg `inbound` — proof it was parsed and honoured. The real reason it can't rescue an impl trace is **edge orientation**: `MATCH (a)-[r:OVERRIDE]->(b)` shows **impl → trait**, so an inbound walk from the impl never crosses one. | `ISSUES.md` Confirmed-working |
| 4 | The old "File node is a **containment artifact**" framing is **wrong**. The File node's only inbound edge is `CONTAINS_FILE`, which a CALLS walk never traverses; it is reached by walking a real **CALLS** edge backwards. Verified: `MATCH (a)-[r]->(b:File) WHERE b.file_path CONTAINS "agent-store/src/scan.rs" RETURN type(r), count(r)` → `CONTAINS_FILE 1` only. | **only here** (the wrong text was deleted, not annotated) |
| 5 | **Dogfood defect:** a Cypher error (e.g. `ORDER BY` on a non-projected column) prints to **stderr** with **empty stdout** and exit 1. Any `2>/dev/null` turns an error into a silent empty result indistinguishable from a true negative. The `codebase-memory` skill's own examples use `2>/dev/null`. It bit me during this task. | **only here** |
| 6 | 225 of 240 File-sourced CALLS were **mis-attributed real calls**, not junk. Only 1 was genuinely outside any function body. This is why "stop minting them" would have destroyed resolved call data. | commit messages + **only here** for the method |
| 7 | The `codebase-memory` skill at `~/.claude/skills/codebase-memory/SKILL.md` is now **stale**: its trap 12 documents the `callers_total` defect I fixed, and it does not mention `unattributed_files`. **Outside my write scope — untouched.** | **only here** |
| 8 | P2's "diagnostic chatter contaminates the result stream" is **NOT a defect**. `level=` lines are stderr-only by construction (`src/foundation/log.c:205`), and the hint is `fprintf(stderr, ...)` at `src/main.c:1325`, gated on `daemon_spawned` — it does not appear on a warm-daemon call. stdout carries clean JSON. **Dropped rather than carried forward.** | `ISSUES.md` (by omission) + commit `fdfadee7` message |

---

## 4. THE TRAPS

1. **`pkill -f codebase-memory-mcp` kills the test suite.** The pattern matches the repo
   path, so it kills the test-runner and every process it forked. It looks safe because
   the brief authorises it "for CBM processes ONLY". **Instead:** target the daemon
   precisely (`pgrep -f 'cbm-daemon'`, or match `--cbm-daemon-internal`) and **never run
   it while a test-runner is alive** (`pgrep -f 'build/c/test-runner'` first).
2. **The cfg-suffix asymmetry.** Applying the cfg suffix uniformly on the call side looks
   *more* consistent and silently re-breaks 9 sites, because `extract_rust_impl` emits
   impl methods **without** a suffix while `extract_func_def` suffixes free functions.
   **Parity with the def walk is the rule, not uniformity.** I made this exact mistake.
   `tool_trace_cfg_gated_method_not_attributed_to_file_node` now pins it — if you "clean
   this up", that test reddens; believe the test.
3. **Treating File-sourced CALLS as junk.** 225 of 240 were real calls. Deleting or
   refusing to mint them destroys resolved call information. Eliminated dead end.
4. **Trusting the old ISSUES.md framing.** It said "containment artifact" (false, see
   finding #4) and that `--edge-types` was broken (false, finding #3). Both would send you
   to the wrong code. Both are corrected in the current file.
5. **The graph index for this repo is one build behind my changes.** I used the tool to
   navigate its own source; results reflect the pre-fix index. Corroborating evidence only
   — never sole evidence about my new functions. Re-index if you need current structure.
6. **`2>/dev/null` on any `cli` call.** See finding #5 — you will read a silent empty
   result as a true negative. Use `2>&1 | grep -v '^level='`.
7. **The same-build barrier.** A newly built binary refuses to start while any process
   from a different build is alive ("a conflicting CBM process is active"). Every `cli`
   call spawns a temporary daemon, so your own calls create these. `CBM_CACHE_DIR` does
   **not** isolate it. I hit this: a 12:52 daemon binary blocked a 12:41 test-runner.
8. **Load contaminates every wall-clock measurement.** Other agents work in a sibling
   repo; I saw load 154–211. The three `stack_overflow_a` failures and all CLI timings are
   suspect under load. Check `uptime` before trusting any number.
9. **`tail -N` on a backgrounded test run buffers everything until exit** — the log looks
   empty for an hour and failure names are lost. Redirect to a file instead.

---

## 5. MY LEAST-CONFIDENT WORK — look hardest here

### `trace_split_unattributed` (`src/mcp/mcp.c:6141`) — highest risk in the diff
In-place stable partition. `spill` is a **non-owning VIEW** into `tr`'s array; the moved
rows are parked in the tail of the same allocation.
- **Verified:** only `tr_out`/`tr_in` are freed, at `src/mcp/mcp.c:6777` and `6780`,
  **after** all serialization; `unattr_out`/`unattr_in` are never freed (`rg -n
  'unattr_out|unattr_in' src/mcp/mcp.c` — all reads). The malloc-failure path returns
  early and preserves the pre-existing (over-counting) behaviour rather than crashing.
  The whole suite builds and runs under **ASan/UBSan** and the path is exercised by
  `tool_trace_file_node_excluded_from_callers_total`.
- **NOT verified:** I did not write a test that pages a trace whose spill is non-empty,
  so **cursor/pagination interaction with a non-empty spill is untested**. My reasoning is
  that the split runs at `6483-6484`, *before* the page windows and cursor watermark are
  computed, so windowing sees the already-partitioned set — but that is reasoning, not a
  test. **UNVERIFIED BELIEF.** Settle it with a fixture that has >`limit` callers plus at
  least one File-sourced row, then walk it with `--cursor` and check no row is skipped or
  repeated. A silent skip/repeat here would be exactly the class of defect this work
  exists to eliminate.
- Also not verified: `*spill = *tr` copies the whole struct, including any `edges` array
  pointer. Nothing frees `spill`, so I believe this is inert — but if a future change ever
  frees a spill, it would double-free. The comment at `6123` says so explicitly.

### The nested-fn change (`3eb76443`, `extract_unified.c:1393`)
- **The concern:** `closure_expression` **is** in `rust_func_types`
  (`internal/cbm/lang_specs.c:296-297`), so extending `skip_nested` to Rust could change
  closure attribution.
- **Verified empirically:** I indexed a fixture with a `let f = || clos_callee();` and a
  `.map(|_| clos_callee())` and both calls attributed to their enclosing **function**
  (`clos_outer`, `clos_iter_caller`), not to a File node. Reasoning that matches: a
  `closure_expression` has no `name` field, so `compute_func_qn` returns NULL and no scope
  was ever pushed for it — the change should be a no-op for closures.
- **NOT verified / gaps:** that fixture check is **not pinned by a committed test** — it
  was throwaway, and the project has been deleted. Nested `impl` blocks inside function
  bodies, and nested `mod` blocks, were **not** tested at all. Whether attributing a
  nested fn's calls to the outer fn is the *desired* semantic (vs. suppressing them) is a
  design judgement I made alone — see §6.

---

## 6. OPEN QUESTIONS — decide, don't re-derive

1. **Are the 3 `stack_overflow_a` failures pre-existing?** *(highest priority)* Options:
   (a) environmental/load — my belief, evidence in §2; (b) a real regression from
   `ad8ee36e`. **Decide with the baseline worktree procedure in §2**, on an idle machine.
   Everything else in this handoff is unaffected either way, but this gates "is the full
   suite green".
2. **Is "attribute a nested fn's calls to the enclosing outer fn" right?** I chose it
   because the outer fn is the only callable node that textually contains the call site,
   and it matches the def walk's node inventory. Alternative: emit nodes for nested `fn`s
   so they can be attributed precisely — bigger change, more nodes, affects the def walk.
   No evidence either way beyond consistency with the existing OCaml precedent.
3. **Should the cfg predicate stay inside QNs?** (ISSUES.md item 2.) Options: (a) move the
   predicate to a separate field and keep QNs clean — breaks anything relying on QN
   uniqueness for cfg twins; (b) document the suffix as part of the QN grammar in the tool
   description — cheap, but leaves an unbalanced `]` in a copy-pasteable identifier.
4. **Is `unattributed_files` the right surface?** It mirrors `via_port_*` deliberately.
   Alternative: a single `partial: true` flag. I chose the section because it preserves the
   evidence (which file holds the unresolved call), and a flag does not.
5. **Should the `codebase-memory` skill be updated?** (Finding #7.) It is stale in two
   places and lives outside this repo's write scope. Owner decision.

---

## 7. HOW TO REBUILD AND RE-VERIFY (exact commands)

```bash
cd /Users/viktor/Projects/github/codebase-memory-mcp

# ASan/UBSan test runner (~10 min cold)
make -f Makefile.cbm build/c/test-runner -j16
./build/c/test-runner mcp          # EXPECT: 190 passed, 0 failed, 2 skipped
./build/c/test-runner              # full suite; see §2 before judging failures

# production binary (~5 min)
make -f Makefile.cbm cbm -j16      # -> build/c/codebase-memory-mcp

# live check of the P0 fix (reindex a Rust workspace with the NEW binary first)
B=./build/c/codebase-memory-mcp
$B cli index_repository --repo-path <rust-repo> --name verify 2>&1 | grep -v '^level='
$B cli query_graph --project verify \
   --query 'MATCH (a:File)-[r:CALLS]->(b) RETURN count(r) AS n' 2>&1 | grep -v '^level='
# on the agent workspace this went 240 (pre-fix) -> 15 (post-fix)
```
Notes: a new test must be added to **both** the `TEST(...)` body and the `RUN_TEST(...)`
list (`tests/test_mcp.c:10580-10584`) or it silently never runs. Never `rm` — use `trash`.

---

## 8. STATE AT HANDOFF

- Working tree **clean**; `git status --porcelain` empty.
- 8 commits `357bfbdc..38ff24da` (7 of work + this handoff), **all local, nothing pushed**,
  origin still `8d2b9564`. Verify: `git rev-list --count 357bfbdc..HEAD` → `8`.
- **The baseline-worktree build never finished** before I was retired, so open question #1
  (§6) is genuinely unresolved — I could not settle it, and I am not guessing at it.
- Graph projects left indexed: `Users-viktor-Projects-agent` and
  `Users-viktor-Projects-github-codebase-memory-mcp` — all scratch projects I created were
  deleted (`cli delete_project`).
- A **git worktree at `357bfbdc`** may still exist in the session scratchpad
  (`base-wt`) with a partial build. `git worktree list` → `git worktree remove <path>`.
- Background jobs that may still be running when you arrive: a full `test-runner` run and
  the baseline worktree build. Check `pgrep -f 'build/c/test-runner'` before starting
  anything that rebuilds or kills processes.
