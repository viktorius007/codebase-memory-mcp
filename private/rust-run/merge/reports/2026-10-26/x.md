STATUS: DONE
COMMIT: 3f7b8818
CHANGED: 3 files
CLOSED: 3 items
OPEN: 3 items
BLOCKERS: none
---
CLOSED

1. SEMANTICALLY_RELATED churn (commit 95c2cedd; scripts/graph-identity/graphcmp.py, check.sh)
   Mechanism: load() put every edge type in one multiset, so derived SR edges (they follow the CALLS neighbourhood) were compared and counted with resolved edges and failed the verdict.
   Fix: SR gets its own multiset and a "semantic:" line (counts, digest, first diffs). The structural "edges:" line and the verdict exclude it. check.sh prints the semantic counts beside the edge counts.
   Observation (synthetic 3-node dbs, python3 -I graphcmp.py a.db b.db): CALLS identical and SR 1 -> 2 prints "edges: ... removed=0 added=0", "semantic: before=1 after=2 removed=1 added=2", IDENTICAL, exit 0. A real CALLS difference (a.db vs c.db) still prints "+ ... CALLS" and DIFFERENT.
   The full check.sh was not run (it needs the corpora and a full index, which is a test-class run before 03:00 UTC).

2. Sequential vs parallel line (commit 1720f319; src/pipeline/pass_parallel.c)
   Mechanism: two calls to the same callee from the same caller make the same edge (src:tgt:CALLS). cbm_gbuf_insert_edge keeps the props text that is larger by strcmp when confidence ties. pass_calls.c (sequential) writes "line" before "args"; cbm_pipeline_close_call_props (parallel) wrote "args" before "line". The two drivers therefore compared different fields first and kept different call sites.
   Fix: cbm_pipeline_close_call_props now writes the line before the args.
   Observation: a.py = main(){print("b"); print("a")}, indexed alone (sequential) and with 60 filler files (parallel, over the 50-file threshold).
   Before: parallel kept line 2, sequential kept line 3. After: both keep "line":3.
   Production build warning-free: make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/x -j4 (0 warnings or errors).

3. Byte-identical CALLS args (commit 3f7b8818 (amended after review); src/pipeline/pass_parallel.c; replaces the withdrawn 1d98a904, which made the sequential side adopt the parallel text)
   Mechanism: append_args_json (parallel) and calls_append_args (sequential) were two implementations. Parallel ran sanitize_expr (double quote to single quote, newline to space, cut at 120), wrote "k", and bounded fields at 127 bytes. Sequential JSON-escapes the raw expression, has no "k", and drops an argument whose object exceeds 512 bytes.
   Fix per ruling: append_args_json now writes the sequential text, which calls_args_overlong_probe asserts (unchanged test). pass_calls.c untouched; sanitize_expr and format_call_arg removed. Still valid JSON.
   Observation: a.py with print("a", end="y", sep='"q"\nz'), alone (sequential) vs with 60 filler files (parallel): CALLS rows SAME, both "args":[{"i":0,"e":"\"a\"","v":"a"},{"i":1,"e":"\"y\"","v":"y"},...]. Production build warning-free.
   Review finding fixed: the parallel props buffer was 2048 bytes against the sequential 512, so two 250-byte positional arguments kept both (parallel) vs one (sequential). cbm_pipeline_close_call_props now budgets CALLS edges in 512 bytes with the sequential 32-byte line room and argument-fit test. Observation: sink('a'*250, 'b'*250), sequential vs parallel: CALLS rows SAME, one argument kept in both (before the fix parallel kept two).
   Consequence: parallel CALLS props lose "k" and keep the original quote style, so the frozen C/C++ graphs (binary b9a7012c) will differ in props text on a new freeze.

OPEN

1. DIGESTS re-record not done (rebuild had not passed at 02:39 UTC). `tail -1 /scratch/agent/tmp/rebuild.log` was empty at 02:37 UTC and ~/Developer/cbm-bench/cpp/base/*/graph.db does not exist, so no row can be computed yet. Proposed change, once the log reads "REBUILD: PASS": for each corpus run `python3 -I scripts/graph-identity/graphcmp.py G G` on ~/Developer/cbm-bench/cpp/base/<corpus>/graph.db and take the "nodes:", "edges:" and "semantic:" digest and count fields. New DIGESTS layout, columns: binary corpus nodes nodes-digest edges edges-digest sr sr-digest, with the binary column b9a7012c and a header line "edge columns exclude SEMANTICALLY_RELATED; sr columns are that class alone". check.sh freeze would print and record the same layout; I have not edited check.sh freeze, because the layout and rows go in together. Until then the stored edges-digests (hashed over all edge types) are stale against the new graphcmp.py.
2. A CALLS change is still reported as a structural edge difference and fails the check. That is deliberate: it is the real change. Only SR no longer floods or fails it. 
3. Not run: scripts/graph-identity/check.sh against the real corpora, and any test suite. Both fall under "no test run before 03:00 UTC". Suggested suites after 03:00 for the pass_parallel.c change: the pipeline, parallel and calls suites.


Note for OPEN 1: because commit 3f7b8818 (amended after review) changes the parallel props text, the frozen bases from the old binary and a re-freeze from the new binary will differ in the edges digest; the binary column should name the binary that produced the graphs.

Test runs (from 03:00 UTC, worktree /scratch/agent/wt/x, BUILD_DIR=build/t-x):
- scripts/test.sh --suites "pipeline,pipeline_expression,pipeline_unresolved,call_reference_contract,pipeline_frozen": "632 passed, 0 failed, 0 skipped  (5 selected suites, 16 jobs; ...)". Per suite: pipeline 542 pass, pipeline_expression 8, pipeline_unresolved 51, call_reference_contract 15, pipeline_frozen 16. This includes calls_args_overlong_probe, unchanged and passing.
- scripts/test.sh --suites "parallel,graph_buffer": "155 passed, 0 failed, 0 skipped  (2 selected suites, 16 jobs; ...)".
- The runner exits rc=1 on the pipeline, pipeline_expression, pipeline_unresolved and graph_buffer-run logs because LeakSanitizer reports leaks at process exit (1.0 MB to 19.6 MB). Every leak stack tops out in ks_build (internal/cbm/helpers.c:657) via rust_lsp.c / pass_lsp_cross.c. None has a frame in append_args_json, cbm_pipeline_close_call_props or calls_append_args (grep of the three logs: 0 hits). I judge these unrelated to my commits (the HANDOFF mentions a memory-core baseline). I did not run the same suites on main to confirm they are pre-existing.
