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
