# CLI array-flag and `detail:"ids"` argument-handling analysis

Scope: three reported behaviours in the `cli` argument path (`src/cli/cli.c`,
`cbm_cli_build_args_json`) and the `search_graph` response path
(`src/mcp/mcp.c`). Question for each: DEFECT, GAP, or BY DESIGN.

## Item A1 — `--semantic-query '["retry","backoff"]'` silently accepted

**Verdict: DEFECT.** It is the `#997` failure mode verbatim, in array clothing.

Evidence:

- `src/cli/cli.c`, the `#997` comment in `cbm_cli_build_args_json`, states the
  project's ruling on this exact class: an argument the tool cannot use as
  given is rejected loudly, because silently typing it through means "the
  caller gets silently-wrong output". The named precedent is `trace_path
  --max-depth 1`, which traced at depth 3 and looked like a real answer.
- `tests/test_cli.c`, `cli_build_args_json_repeated_array_issue680`, defines
  the intended contract for array flags: `--semantic-query send
  --semantic-query publish` → `"semantic_query":["send","publish"]`. Repetition
  IS the array form. No test anywhere accepts a JSON literal.
- `src/mcp/mcp.c` (search_graph schema, the `semantic_query` property) is
  emphatic: `MUST be an ARRAY of keyword strings (e.g. ["send","pubsub",
  "publish"]) — NOT a single string. Each keyword is scored independently via
  per-keyword min-cosine.` The old CLI behaviour produced exactly the banned
  shape — a one-element array whose single element is the literal text
  `["retry","backoff"]`, brackets and quotes included — which is then embedded
  and cosine-matched as one nonsense token. Hence 0.02 vs 0.95+.
- The MCP path already rejects the analogous mistake at the server boundary:
  `run_semantic_query_core` returns a type error for a non-array
  `semantic_query`, and the handler answers with a teaching error
  ("must be an array of keyword strings … not a single string"). The CLI was
  the one door that let the malformed shape through, because it wrapped the
  blob into a syntactically valid array before the server ever saw it — the
  server's guard cannot fire on it.

So the server-side intent is explicit, the CLI contract is tested, and the CLI
was the only path that defeated both. That is a defect, not a design choice.

## Item A2 — `--fields '["complexity"]'` emits a literal `["complexity"]` column

**Verdict: DEFECT — same root cause, same fix.** `fields` is
`{"type":"array","items":{"type":"string"}}` in the search_graph schema and is
consumed by `sg_parse_fields` as a list of node-property names. The literal
string `["complexity"]` is not a property on any node, so
`sg_toon_extra_cells` finds no value and emits an empty cell for every row —
a column that is named wrong and filled with nothing, presented as a result.
One code path (`cli_add_typed`'s `array` branch) produced both A1 and A2, so
one guard fixes both.

## Why `--aspects` already rejected it

Not a different CLI contract — a second, tool-specific validation layer.
`get_architecture` validates each element against `VALID_ASPECTS`
(`aspect_is_valid`, `src/mcp/mcp.c`) and errors with the full 14-value list.
The CLI wrapped `["clusters"]` into a one-element array identically; the
downstream enum check happened to catch it. `semantic_query` and `fields` have
no closed value set to check against, so nothing caught them. The divergence
was in downstream validation coverage, never in the argument parser's intent.

## Fix (A1 + A2)

`src/cli/cli.c`: `cli_looks_like_json_array()` + a rejection branch in
`cbm_cli_build_args_json`, applied only to schema-`array`-typed flags. A value
that is `[`…`]` after trimming whitespace is refused with a message naming the
flag, echoing the given value (truncated at 120 chars), and stating the
repeated-flag form plus the `--args-file`/stdin escape hatch.

Deliberately scoped to array-typed flags: string-typed flags such as
`--name-pattern` take regexes that legitimately begin with a character class
(`[A-Z].*Handler`), and those must keep working. Array-typed properties in
this schema set are keyword lists, property names, enum values, paths, scopes
and project names — none of which is ever bracket-wrapped. Guarded by
`cli_build_args_json_string_flag_keeps_bracket_value`.

## Item B — `--detail ids` ignores `--fields`

**Verdict: BY DESIGN in what it drops; GAP in doing it silently.** Fixed the
silence only; the drop behaviour is unchanged.

Evidence that dropping is intended:

- `src/mcp/mcp.c`, the `detail` property description: `ids: bare qualified-name
  enumeration (one column) — cheapest form for wide sweeps where per-row
  metadata is noise.` A second column would contradict the documentation every
  agent reads.
- `emit_search_results_toon`'s ids branch hard-codes `id_cols[] = {"qn"}` and
  returns before the extra-field columns are appended — the one-column shape
  is structural, not incidental.
- Commit `c5bffb7f` introduced the tier as "detail:'ids' tier: bare-qn
  enumeration for wide sweeps (-44%)". The whole point is byte reduction;
  honouring `fields` would defeat it.

Evidence that the silence is a gap, from the repo's own ruling on the
identical shape: the same commit message says "requesting core columns via
fields hints instead of emitting empty cells", implemented one line away from
this code as the `core_fields_requested` hint. The project has already decided
that `fields` entries which cannot be honoured are reported, not swallowed.
`detail:"ids"` + `fields` is the same situation — the caller asked for
complexity, received a qn list, and cannot distinguish "column refused" from
"column empty" — and it was the one case that stayed quiet.

## Fix (B)

`src/mcp/mcp.c`: when `detail_ids && nfields > 0`, emit a `hint` naming the
dropped fields and the cause (`detail="ids"` is a one-column enumeration) plus
the remedy (drop `detail="ids"`). Placed as the leading branch of the existing
hint chain so exactly one hint is ever emitted. Output shape, columns and byte
budget for every other call are untouched; the hint fires only on the
contradictory combination.

No schema description text was changed — the schema already documented the
intended behaviour correctly in both items; the code disagreed with it.

## Red proofs

Both observed failing against unmodified source, then passing after the fix.

- `cli_build_args_json_array_flag_rejects_json_literal` (tests/test_cli.c) —
  pre-fix: `FAIL tests/test_cli.c:11782: json is not NULL` (the JSON-array
  literal was accepted and produced args JSON instead of an error).
- `tool_search_graph_detail_ids_hints_dropped_fields` (tests/test_mcp.c) —
  pre-fix: `FAIL tests/test_mcp.c:1849: strstr(inner, "hint") is NULL` (the
  one-column contract already held; the drop was silent).

The Item B test pins three distinct config values — ids+fields (hint),
ids without fields (no hint), fields without ids (real column, no hint) — so
an implementation that emits the hint unconditionally, or never, fails.
