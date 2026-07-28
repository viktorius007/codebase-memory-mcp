# Cypher silent-wrong-answer fixes — analysis

Branch `fix/cypher-silent`, worktree `/Users/viktor/Projects/github/cbm-wt-cypher2`.
Base `ba72e3ff` (cypher suite: 174 passed). After: **187 passed**.

Commits: `dc326d8a` (both fixes + 11 tests), `5414b788` (one more non-regression test).

---

## Defect A — ORDER BY on an unresolvable column silently ignored

### Root cause (verified, not assumed)

The prior agent's lead was correct as far as it went, and incomplete.

`rb_apply_order_by` (was cypher.c:3374) calls `rb_find_order_column`, which compares
`ret->order_by` against `rb->columns[]` and against each item's `AS` alias. On a miss it
returns `CBM_NOT_FOUND` (-1) and `rb_apply_order_by` returns **without sorting** — rc==0,
no error, rows in scan order. Indistinguishable from a correct sort.

Two things the lead did not mention, both found by tracing the graph
(`trace_path`/`query_graph` over `codebase-memory-mcp`) rather than by reading around
line 3340:

1. **The WITH path has the same hole, by a different mechanism.** `with_sort_skip_limit`
   → `sort_bindings` looks each key up with `binding_get_virtual`, which returns `""` for
   an unknown var. Every row compares equal, the bubble sort does nothing, and the rows
   come back unsorted — again silently. This is a *second* instance of the same defect
   class and would have survived a fix confined to `rb_apply_order_by`.
2. **`ORDER BY` with no key at all** parsed to an empty string key (`parse_order_by_var`
   returned void on a missing IDENT, leaving `buf` as `""`), which then matched no column
   and took the exact same silent-drop path. A malformed query was accepted.

### The boundary, and why it is drawn there

The genuine design question: openCypher permits `RETURN f.name ORDER BY f.complexity` —
sorting on an expression absent from the RETURN list. Rejecting that would break
legitimate queries, which is worse than the bug.

**The engine cannot evaluate it.** The relevant fact is *when* sorting happens:

- `execute_return_clause` fully materializes the projection into `result_builder_t`
  (rows of already-projected `const char *`), and only then calls `rb_apply_order_by`.
- By that point the `binding_t` array — the only thing that could resolve `f.complexity`
  against the underlying node — has not been freed yet, but is not passed to, or reachable
  from, the sort. The result builder holds strings and column names, nothing else.
- Therefore the complete set of evaluable sort keys is exactly: **the result table's own
  column names, plus each RETURN item's `AS` alias.** For the WITH path, symmetrically:
  the clause's own projected aliases (`resolve_item_alias` over `wc->items`), since the
  projected vbindings carry exactly one virtual var per WITH item.

So the line is: **anything in the projected result is sortable and keeps working;
anything else is refused with an error naming the key and telling the caller to add it
to RETURN.** This is not "reject unreturned columns" as a policy choice — it is the
honest edge of what this executor can compute. Making `ORDER BY f.complexity` work
without returning it would require sorting bindings before projection (a real
architectural change: a pre-projection sort stage), which is out of scope here and
would be a spec-gated change, not a bug fix.

Note the boundary is about the **result table**, not the literal RETURN list — which is
why `RETURN * ORDER BY f.file_path` still sorts (star projection materializes
`.name/.qualified_name/.label/.file_path` as real columns). That case is pinned by
`cypher_order_by_star_projection_still_sorts`.

### Fix

- New thread-local `g_cypher_unresolved_order_key` (mirrors the existing
  `g_cypher_group_key_truncated` pattern exactly — same reset point in
  `cypher_deadline_arm`, same "record in the executor, fail at the entry point" shape).
- `rb_apply_order_by` records the key instead of silently returning.
- `with_sort_skip_limit` gains `with_order_key_resolves` (static check against the
  clause's projected aliases) and records rather than sorting nothing. It also skips
  SKIP/LIMIT in that case, since the query fails as a whole.
- `cbm_cypher_execute` turns a recorded key into `unsupported: ORDER BY '<key>' — sorting
  works on the returned columns, and '<key>' is not one of them; add it to RETURN …`,
  matching README's documented `unsupported …` contract.
- `parse_order_by_expr`/`parse_order_by_var`/`parse_order_by_agg`/`parse_order_by_clause`
  now return status instead of void, so a missing sort key is a parse error rather than
  an empty key that gets silently dropped downstream.

---

## Defect B — `count` unusable as a property name

### Root cause

`lex_try_ident` → `keyword_lookup` maps every word against the keyword table
unconditionally, with no positional context. So in `a.count`, `count` lexes as
`TOK_COUNT`, not `TOK_IDENT`.

`parse_var_dot_prop` then did:

```c
if (match(p, TOK_DOT)) {
    const cbm_token_t *prop = expect(p, TOK_IDENT);
    if (prop) { item->property = heap_strdup(prop->text); }   /* miss → silently skipped */
}
```

`expect` fails **and does not advance**. Two consequences compound: the property is
dropped (leaving a property-less item that would project a blank column), *and* the
`count` token is left unconsumed. Parsing then unwinds to the end-of-query check, which
reports `unexpected trailing tokens at pos 24` — an error that names the wrong problem
entirely and sends the reader hunting for a syntax error that isn't there.

### Fix, and why "make it parse" rather than "better error"

The brief left the call to me. Making it parse is correct, because **after a `.` nothing
but a property name can appear** — there is no grammatical ambiguity to resolve. A
keyword is a legal property key in openCypher; the engine was simply losing positional
context. Rejecting with a nicer message would refuse a query that is valid openCypher
and that the storage layer can answer.

`expect_property_name` accepts any *word-shaped* token, with `is_word_token` deriving
keyword-ness by scanning the `keywords[]` table itself rather than enumerating token
types — a second enumeration would drift silently the moment a keyword is added.

Applied at **all five** dot-property sites, found by grepping `TOK_DOT` (the defect was
not specific to RETURN):
`parse_var_dot_prop` (RETURN/WITH items), `parse_cond_lhs` (WHERE), `parse_value_literal`
(RHS of a comparison), `parse_func_arg` (function arguments), `parse_order_by_var`
(ORDER BY keys).

Two guardrails so the accept-set does not over-open:

- A quoted string is still **not** a property name (openCypher escapes names with
  backticks, not quotes) — pinned by `cypher_quoted_string_not_a_property_name`, green
  in both states.
- A *missing* name after `.` is now a hard error at every site, where three of the five
  previously fell through to a property-less item that projects blank.

One knock-on: `parse_order_by_expr` treated any aggregate token as a call. With keywords
now legal as names, `ORDER BY count` (a keyword-shaped alias) would misparse, so the
aggregate branch is now taken only when `(` actually follows. The documented recipe
`RETURN type(r), count(r) ORDER BY count(r) DESC` is pinned by
`cypher_order_by_aggregate_call_still_sorts`.

---

## Red-first proof

Executed on unmodified `src/cypher/cypher.c` with the new tests in place
(`179 passed, 7 failed`; the 5 non-regression tests green in that same run):

```
cypher_order_by_unknown_column_errors        FAIL tests/test_cypher.c:3644: ASSERT(rc != 0)
cypher_order_by_unreturned_column_errors     FAIL tests/test_cypher.c:3662: ASSERT(rc != 0)
cypher_with_order_by_unknown_alias_errors    FAIL tests/test_cypher.c:3682: ASSERT(rc != 0)
cypher_order_by_missing_key_rejected         FAIL tests/test_cypher.c:3697: rc == 0, expected -1 == -1
cypher_order_by_returned_second_column_still_sorts   PASS
cypher_order_by_returned_json_metric_still_sorts     PASS
cypher_order_by_aggregate_call_still_sorts           PASS
cypher_order_by_star_projection_still_sorts          PASS
cypher_order_by_alias_still_sorts                    PASS
cypher_node_property_named_count_parses      FAIL tests/test_cypher.c:3803: rc == -1, expected 0 == 0
cypher_edge_property_named_count_parses      FAIL tests/test_cypher.c:3819: rc == -1, expected 0 == 0
cypher_where_property_named_keyword_parses   FAIL tests/test_cypher.c:3837: rc == -1, expected 0 == 0
cypher_quoted_string_not_a_property_name             PASS
```

The red was produced by restoring `git show ba72e3ff:src/cypher/cypher.c` over the
source **with the new tests still present**, then restoring the fix — so the failures are
attributable to the production change alone, and the 5 non-regression tests are proven
green in *both* states (the red is earned by the named property, not by collateral
breakage).

Fixture design note: `setup_order_by_store` deliberately gives name order, file_path
order and complexity order that all **disagree** (Alpha/z.go/1, Beta/m.go/3,
Gamma/a.go/2). A sort test that passed by receiving insertion order or name order would
be vacuous; here it cannot.

---

## Found but not fixed

1. **`sort_bindings` and `rb_apply_order_by` are both bubble sorts** — O(n²) on the
   result set, under the 100k-row ceiling. Not a correctness issue and out of scope, but
   a large ORDER BY is quadratic.
2. **Multi-key ORDER BY still sorts by the first key only.** The parser consumes the
   remaining keys (deliberately, per the existing comment, so a trailing LIMIT is not
   dropped) but the executor ignores them. This is a silent partial-sort: `ORDER BY a, b`
   silently behaves as `ORDER BY a`. Same defect class as what I fixed, but a distinct
   defect with a real design choice attached (implement multi-key sorting, or reject the
   second key) — flagged rather than fixed, since fixing it is a behavioural change
   needing a decision, not a bug fix inside my scope.
3. **`rb_is_numeric_column` samples only the first non-empty value** to decide numeric vs
   string comparison. A column that is numeric in row 0 and non-numeric later sorts
   inconsistently. Narrow, pre-existing, not touched.
4. The skill doc `~/.claude/skills/codebase-memory/SKILL.md` documents trap 9 —
   "`count` is unparseable as a property name". That trap is now obsolete on this branch
   and should be removed from the doc once this merges. Documentation the orchestrator
   owns, not a code change.
