# ISSUES — open defects and gaps

Live backlog of known-but-unfixed work. Append here; remove an item when it is fixed and
verified. Each entry states what was measured, on what project, so the next reader can
re-run it rather than trust the write-up.

## Open

- **`trace_path` reports an impl method reached only by dynamic dispatch as having ZERO
  callers, with nothing marking the answer partial.** (found 2026-07-29)

  The three documented modes — `calls` (CALLS), `data_flow` (CALLS+DATA_FLOWS),
  `cross_service` (HTTP/ASYNC/DATA_FLOWS/CROSS_*) — traverse no `OVERRIDE` edge, and
  `OVERRIDE` is where the vtable link lives. So for any method invoked through a
  `dyn Trait` / interface reference, the caller side of the graph is unreachable from the
  impl node.

  Measured on a Rust ports-and-adapters workspace (project slug
  `Users-viktor-Projects-agent`, ~8.9k nodes / 33.8k edges):

  | query | `callers_total` |
  |---|---|
  | `…process_executor.ProcessExecutor.execute`, `--direction inbound --depth 2` | **0** |
  | same, `--include-tests true` | 22 — every one a test |
  | the port trait it implements, `…ports.command.CommandExecutor.execute` | **1** — the real production caller |

  The impl reads as dead code. There is no `partial: true`, no warning, no hint that a
  traversal class was skipped — the response is shaped exactly like a genuine zero. A reader
  acting on it concludes "this adapter is test-only" and may weaken or delete live production
  code; that outcome was one inference away in the run that found this.

  Severity is a function of architecture: in a hexagonal/ports-and-adapters codebase EVERY
  port crossing is a `dyn` hop, so every adapter method in the repo is affected
  simultaneously — `Tool`, `DirectTool`, `ApprovalPort`, `CommandExecutor`, `CompletionPort`.

  **Sub-defect: `--edge-types` does not do what its name implies here.** Passing
  `--edge-types CALLS --edge-types OVERRIDE` on the impl query left `callers_total: 22`
  unchanged (test-inclusive), i.e. requesting the edge type that carries the vtable link had
  no effect on traversal. A separate observation from the same session reported
  `CALLS`+`OVERRIDE` returning *fewer* callers than `CALLS` alone; that one was not
  reproduced here and is recorded as unconfirmed. Either way the flag appears not to be
  reaching the traversal it names.

  **Current workaround, and it works:** resolve the *trait* method with `search_graph` (the
  impls and the trait declaration share a bare name, so the qualified name disambiguates)
  and trace that instead of the impl. This is documented consumer-side in the
  `codebase-memory` agent skill, but a workaround in a skill file only protects readers who
  have loaded that skill.

  **Suggested shape of a fix**, in preference order: (1) make `mode: calls` follow `OVERRIDE`
  from an impl node to its trait declaration and continue from the trait's callers, since
  "who can reach this code at runtime" is the question `trace_path` is asked; or (2) if that
  is deliberately out of scope, mark the response — a `dispatch: dynamic` note, or a
  `partial` flag naming the skipped edge class — so a zero is legible as "not traversed"
  rather than "not called". Option 2 alone would have prevented the near-miss above.

- **A `__file__` node is returned inside a callers list, inflating `callers_total`.**
  (found 2026-07-29)

  Reproduced on the Rust ports-and-adapters workspace `Users-viktor-Projects-agent`
  (~8.9k nodes / 33.8k edges):

      codebase-memory-mcp cli trace_path '{"project_name":"Users-viktor-Projects-agent",
        "function_name":"ensure_single_link_regular_file","direction":"inbound",
        "include_tests":true}'

  returns `callers_total: 12`, and among the listed callers is a row

      agent-store.src.scan.rs:
        __file__ 2

  A File node is not a caller. The reported total therefore includes at least one
  non-caller, with nothing in the response marking which rows are real call sites and
  which are containment artifacts.

  **Why it matters even though this instance is benign:** the consumer that hit it ignored
  the row and its verdict was unaffected — but `callers_total` is exactly the field an
  agent reads to answer "how many places call this", and to decide whether a symbol is
  safe to change. An inflated count is a silent wrong answer: it looks like a clean
  result, and nothing prompts the reader to check. It is the same failure shape as the
  OVERRIDE zero-callers defect above, in the opposite direction — that one under-reports,
  this one over-reports, and neither is marked.

  **Suggested fix**, in preference order: (1) exclude non-callable node labels (`File`,
  `Folder`, `Module`) from callers lists and from `callers_total`; or (2) if a containment
  edge is deliberately surfaced, label the row with its node kind so a reader can tell a
  call site from a file, and exclude it from the total.

- **The CLI pays a full daemon cold start per invocation, and prints daemon chatter into
  the result stream.** (found 2026-07-29)

  Measured on the same project. Each `codebase-memory-mcp cli <tool>` call costs ~1.35 s
  before any query work, and emits to the result stream:

      level=warn msg=mem.allocator.not_owned owned_classes=0/6 …
      level=info msg=mem.init budget_mb=24576 total_ram_mb=49152 source=ram_fraction
      hint: this command started a temporary CBM daemon…

  One consumer traced six symbols in sequence and spent ~8 s on startup for ~0 s of query.
  For comparison, `rg` answered the same six single-symbol questions in ~0.05 s each, which
  is what that consumer concluded made the graph net-negative for uniquely-named symbols.

  Two separable asks: (1) reuse a warm daemon across invocations, or document
  `daemon start` as the harness-setup step so a batch of calls pays the cost once; and
  (2) send `level=` diagnostics to stderr rather than into the tool result — an LLM
  consumer receives them as part of the answer, and CLAUDE.md's agent-consumed-output rule
  says a tool's default output should be decision-ready, not something the reader must
  filter.

## Confirmed working — do not "fix"

- **`status: ambiguous` on an ambiguous symbol is correct behaviour.** Tracing
  `execute_plan` on `Users-viktor-Projects-agent` (three same-named candidates) returned
  `status: ambiguous` naming all three with file paths, rather than guessing one. The
  consumer reported this cost it nothing and required no correction. This is the *safe*
  failure mode, and the direct counterpart to the OVERRIDE defect's silent zero — resist
  any change that turns it into a best-guess single answer.

- **Both CLI invocation forms work.** Positional JSON
  (`cli trace_path '{"project_name":…}'`) and flags
  (`cli trace_path --project <slug> --function-name <qn> --direction inbound`) were both
  verified to return identical output on 2026-07-29. Recorded because two consumers
  independently reported the JSON form as broken; re-testing showed it works, so the
  reports were wrong and no fix is needed. Kept here so the claim is not re-filed.
