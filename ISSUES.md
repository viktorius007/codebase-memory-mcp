# ISSUES — open defects and gaps

Live backlog of known-but-unfixed work. Append here; remove an item when it is fixed and
verified. Each entry states what was measured, on what project, so the next reader can
re-run it rather than trust the write-up.

## Open

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
</content>
