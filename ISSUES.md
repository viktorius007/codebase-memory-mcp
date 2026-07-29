# ISSUES — open defects and gaps

Live backlog of known-but-unfixed work. Append here; remove an item when it is fixed and
verified. Each entry states what was measured, on what project, so the next reader can
re-run it rather than trust the write-up.

## Open

- **`callers_total` still counts `Module` nodes — the non-callable-node fix covered `File`
  only.** (re-opened 2026-07-29 by an independent review; **this entry corrects a false
  claim made when the original item was closed**)

  Commit `b87e995c` added `trace_row_is_uncallable` (`src/mcp/mcp.c:6124`), which tests
  `strcmp(label, "File") == 0` and nothing else, and its commit message asserted the split
  keeps non-callable nodes out of the totals *"regardless of which extractor path produced
  the edge"*. **That sentence is false.** A `Module` node cannot execute either, and it
  does source CALLS edges — **85** in `Users-viktor-Projects-agent` (verified on the
  rebuilt binary):

      cli query_graph --project Users-viktor-Projects-agent \
        --query 'MATCH (a:Module)-[r:CALLS]->(b) RETURN count(r) AS n'   # -> 85

  Reported live by the reviewer (re-verify before acting): tracing
  `error_body_truncated_message` inbound returns `callers_total: 2`, one row being the
  file's Module node from a module-scope `LazyLock` initializer, rendered as `error_body 1`
  — indistinguishable from a function, and a real `Function` named `error_body` exists
  elsewhere in the same graph. Worse in this repo: tracing `render` inbound reported
  `callers_total: 5` where all five rows were Module nodes from `.tsx` files.

  `Folder` is genuinely clean (0 CALLS edges as source, both projects), so the fix is to
  widen the predicate to `Module` — the original filed item's own suggested fix named
  `File`, `Folder` **and** `Module`, and only one was implemented.

- **Memory leak: `trace_path` never frees the rows split out into `unattributed_files`.**
  (found 2026-07-29 by an independent review of `b87e995c`; **a defect introduced by that
  commit**)

  `trace_split_unattributed` (`src/mcp/mcp.c:6141`) parks the spilled rows in the tail of
  `tr->visited` and then lowers `tr->visited_count` to the kept count.
  `cbm_store_traverse_free` (`src/store/store.c:4181`) frees per row, bounded by
  `visited_count` — so the parked tail is never visited. Each `cbm_node_hop_t` owns 6
  `heap_strdup`'d strings, so every CALLS `trace_path` leaks `6 x spilled_rows`
  allocations, accumulating for the life of the MCP daemon. Reviewer measured a single
  request spilling 57 rows (= 342 leaked strings).

  **Why the suite cannot catch it:** `ASAN_OPTIONS=detect_leaks=1 ./build/c/test-runner`
  prints `detect_leaks is not supported on this platform` on macOS. Leak detection runs
  only in the Linux soak (`.github/workflows/_soak.yml:245`), which is unlikely to drive a
  spilling `trace_path`. Verify the leak by reading the free loop's bound, not by running
  the suite locally.

  **Preferred fix** (reviewer's, and it is the right shape): give the spill a distinct
  type that *cannot* be passed to `cbm_store_traverse_free`, and free the tail explicitly.
  A plain "also free the tail" patch works but leaves the footgun: the spill aliases
  `tr`'s `edges`/root pointers via `*spill = *tr`, so any future `cbm_store_traverse_free`
  on it becomes a double-free of the whole edges array. Today that is safe **only** because
  nothing frees the spill. The comment at `src/mcp/mcp.c:6123` warns of this; the type does
  not enforce it.

- **`unattributed_files` ignores the page budget and repeats in full on every page.**
  (found 2026-07-29 by an independent review of `b87e995c`)

  `src/mcp/mcp.c:6658-6675` (tree) and `:6726-6745` (JSON) emit `unattr_out`/`unattr_in`
  **whole**, not the windowed view used for the main legs (`view_out`/`view_in`, sliced to
  `out_len`/`in_len`). Reviewer measured `--limit 5` returning 5 callers plus all 57
  unattributed rows, and an 11-page walk at `--limit 1` repeating the same `__file__` row
  on every page. `limit` is documented as a context-bomb guard (`src/mcp/mcp.c:29`); this
  section defeats it. No rows are lost and `truncated` stays consistent, so it is a
  budget/noise defect, not a wrong answer.

- **Latent: duplicate JSON keys if both legs spill.** (found 2026-07-29, same review)

  `src/mcp/mcp.c:6727-6731` (outbound) and `:6740-6744` (inbound) add
  `unattributed_total` / `unattributed_note` / `unattributed_files` to the **same** root
  object. `yyjson_mut_obj_add_*` does not dedupe, so a `direction: both` request where both
  legs spill emits each key twice and a last-wins parser silently drops the outbound set.
  `via_port_*` never hits this (inbound-only); `callers`/`callees` never hit it (per-leg
  key names). Introduced by choosing a leg-independent key name.

  **Currently unreachable**, which is why it is filed rather than fixed:
  `MATCH (a)-[r:CALLS]->(b) WHERE b.label = "File"` returns **0** in both indexed projects,
  so `unattr_out` is always empty in practice. It bites the moment any extractor path emits
  a File-targeted CALLS edge. Fix is per-leg key names.

- **Pre-existing, newly aggravated: the trace cursor hash omits `edge_types`.**
  (found 2026-07-29, same review)

  `trace_params_hash` (`src/mcp/mcp.c:5957`) hashes project/func/direction/mode/depth/
  include_tests/limit but **not** `edge_types`, so a cursor minted on a CALLS-only walk is
  accepted on a `CALLS`+`OVERRIDE` walk whose row set differs (reviewer measured 12 vs 11
  rows), and the watermark then indexes a different array — rows can be skipped or
  repeated. Predates this work and is untouched by it, but `b87e995c` widens the gap: the
  two sets previously differed only by *added* rows, and the CALLS leg now also has rows
  *removed*. A concrete skipped row was **not** confirmed. Fix: include `edge_types` in the
  hash.

- **`unattributed_files` uses the tree table even under `risk_labels`.** (found 2026-07-29,
  same review) `src/mcp/mcp.c:6661` always calls `bfs_to_tree_table` for the spill section,
  while the main leg switches to `bfs_to_toon_table` when `risk_labels`/`data_flow` is set —
  and the JSON branch (`:6731`) *does* pass `risk_labels` through. So a `risk_labels=true`
  text response mixes two table shapes in one answer, and text and JSON disagree about the
  spill's columns. Cosmetic; fix by mirroring the main leg's `flat_trace` branch.

- **The CLI costs ~2 s warm / ~5 s cold per invocation, and the cost is not process
  startup.** (found 2026-07-29, re-measured 2026-07-29 on a rebuilt binary)

  Measured with `/usr/bin/time -p`, binary built from this tree, on a 16-core M3 Max **with
  the machine otherwise idle** — these numbers are load-sensitive, so re-measure on a quiet
  machine before comparing against them (an ASan test run in parallel pushed the warm figure
  to 12-36 s, which says nothing about the tool):

  | condition | `cli list_projects` wall time |
  |---|---|
  | cold (no daemon; one is spawned and retired per call) | 4.71 / 4.71 / 5.78 s |
  | warm (`daemon start` running, daemon reused) | 1.93 / 1.95 / 1.96 s |
  | `--version` (process loads, does no daemon work) | **0.00-0.01 s** |

  The `--version` row is the load-bearing one: the ~300 MB binary loads and exits in about
  10 ms, so the cost is **not** image size, dynamic linking, or process creation. It is
  work done on the daemon path — connect/activation handshake and per-request setup. A
  warm daemon removes roughly 3 s of the cold cost but leaves ~1.95 s, so the `hint:`
  text's claim that starting a daemon "removes this startup cost from every CLI command"
  overstates what it actually buys.

  Not yet diagnosed to a specific call, and **deliberately not fixed here**: it lives in
  the daemon IPC/activation path (`src/daemon/ipc.c`, `src/daemon/runtime.c`,
  `src/cli/cli.c`), which is a different subsystem from the extraction/reporting work in
  this session and carries different risk. Filed with the measurement so the next reader
  starts from data rather than re-deriving it.

  Next step for whoever picks this up: attribute the ~1.95 s warm cost before changing
  anything — the `--version` result already rules out the most-assumed cause.

- **Node QNs embed `#[cfg(...)]` predicate text, so a qualified name is not usable as an
  identifier.** (found 2026-07-29)

  A cfg-gated Rust function's node QN is e.g.
  `…agent-store.src.scan.append_open_rejects_a_fifo_at_the_session_path#cfg(unix)]`
  — note the unbalanced trailing `]`. Visible directly in `trace_path` output rows.

  This is deliberate (`#495`: cfg-gated twins would otherwise collide on upsert and one
  branch would be lost) and the suffix is now applied consistently on both the definition
  and call sides, so attribution is correct. What remains is a **presentation** problem:
  the QN is what a consumer copies back into `--function-name` / `--qualified-name`, and
  the suffix is neither documented in the tool descriptions nor obviously strippable.

  Not fixed here because the fix is a design choice with no single right answer — carry
  the predicate in a separate field and keep QNs clean, or document the suffix as part of
  the QN grammar. Both are behaviour changes to a stable surface.

## Confirmed working — do not "fix"

- **`status: ambiguous` on an ambiguous symbol is correct behaviour.** Tracing
  `execute_plan` on `Users-viktor-Projects-agent` (three same-named candidates) returned
  `status: ambiguous` naming all three with file paths, rather than guessing one. The
  consumer reported this cost it nothing and required no correction. This is the *safe*
  failure mode, and the direct counterpart to a silent zero — resist any change that turns
  it into a best-guess single answer.

- **Both CLI invocation forms work.** Positional JSON
  (`cli trace_path '{"project_name":…}'`) and flags
  (`cli trace_path --project <slug> --function-name <qn> --direction inbound`) were both
  verified to return identical output on 2026-07-29. Recorded because two consumers
  independently reported the JSON form as broken; re-testing showed it works, so the
  reports were wrong and no fix is needed. Kept here so the claim is not re-filed.
  (The JSON form does print a deprecation warning to stderr; it still works.)

- **`--edge-types` DOES reach the traversal it names.** An earlier revision of this file
  asserted that passing `--edge-types CALLS --edge-types OVERRIDE` "appears not to be
  reaching the traversal it names". **That is false**, re-measured 2026-07-29 on
  `Users-viktor-Projects-agent` with a binary built from this tree:

  - `…ProcessExecutor.execute --direction inbound --edge-types CALLS --edge-types OVERRIDE`
    returns `inbound_total: 22` — the same 22 as CALLS alone, *and* the response header
    changes to `edges: CALLS,OVERRIDE` with the leg renamed `inbound`, proving the flag was
    parsed and honoured.
  - The same walk `--direction outbound --edge-types OVERRIDE` returns exactly 1 row:
    `agent-core.src.ports.command.CommandExecutor.execute`.

  The real reason OVERRIDE cannot rescue an impl trace is **edge direction**:
  `MATCH (a)-[r:OVERRIDE]->(b)` shows the edge runs **impl → trait**
  (`ProcessExecutor.execute` → `CommandExecutor.execute`). An *inbound* walk from the impl
  therefore never crosses it — not because the flag was ignored, but because there is no
  inbound OVERRIDE edge at that node. Anyone debugging the flag itself is looking in the
  wrong place; the answer is the graph's edge orientation.

  The dynamic-dispatch blind spot this was filed under is **fixed and shipped**:
  `trace_path` now reports port-mediated callers separately as `via_port_total` /
  `via_port` / `via_port_callers`. Do not re-file it.
