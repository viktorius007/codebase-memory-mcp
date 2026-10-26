STATUS: DONE
COMMIT: 53f79f36
CHANGED: 8 files
CLOSED: 3 items
OPEN: 4 items
BLOCKERS: suite pipeline exits rc=1 with 0 assertion failures (LeakSanitizer in rust_modtree.c field cache + UBSan in objectscript_udl/perl doclink), all outside this lane's diff
---
Branch lane/cpp, worktree /scratch/agent/wt/cpp, based on main a76d3f4a. Signed commits. First round: 47241181, 3c4d7bc9, 2dd31de6. Review round 1: 315a10db (C), d5c9d634 (A, B). Review round 2: 3d165b5d (B and the two new wrong links), 37561578 (C). Review round 3: e7ec052b (items 1 and 2), 30066389 (item 3). Review round 4: 53f79f36 (PROVENANCE line number). Ten commits in total.
Production build `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/cpp -j4`: rc 0, 0 warnings after each commit.

Files: internal/cbm/lsp/c_lsp.c, internal/cbm/preprocessor.cpp, and new fixture programs
tests/fixtures/graph/cpp/{lambdaret,linecomment,depthcap}/{main.*,PROVENANCE}.

## CLOSED

1. 47241181 + d5c9d634 + 3d165b5d + e7ec052b  lambda return-type inference wrong links (c_lsp.c:2812-2827)
   Shape: for a lambda with no trailing return type, the operand of the first `return` was typed in
   the enclosing scope. If a parameter, a local or an init-capture had the same name as an outer
   variable, the lambda took the outer variable's type:
   `Outer w; auto f = [](Inner w){ return w; }; f(Inner{}).run();` linked to Outer::run.
   Fix: c_lambda_body_return_type types the operand in its own frame, which binds the init-captures
   (typed in the enclosing scope), the parameters and the body declarations that come before the
   return. If the operand uses a name the frame binds to an unknown type (for example an `auto`
   parameter of a generic lambda), the type is unknown and no link is made. Capture typing is split
   out of the capture walk (c_lambda_capture_types). An init-capture of unknown type is now bound
   as unknown, so in the lambda walk it hides the outer name it spells.
   Observation: tests/fixtures/graph/cpp/lambdaret/main.cpp. Built with g++ -std=c++17, it prints
   Inner::run for the by_param, by_local, by_capture, generic, defaulted and discarded calls, then
   Outer::run three times.
   Fix round, d5c9d634:
   - Finding A (wrong link): a defaulted parameter, `[](Inner w = Inner{}) { return w; }`, was left
     unbound because c_process_statement binds only parameter_declaration. It now gets its type in
     c_lambda_bind_param. Any parameter or body declarator still unbound afterwards is bound as
     unknown, so it hides the outer name.
   - Finding B (withheld link): the syntactic guard c_expr_names_unknown_local is removed. While the
     return operand is typed, the identifier evaluator stops at an unknown-typed binding of the
     lambda's own frame (thread-local c_lambda_infer_frame) instead of falling back to the
     registry. `return (static_cast<void>(w), Inner{});` with `auto w` is typed Inner again.
   Fix round 2, 3d165b5d (finding B, plus two new wrong links from d5c9d634):
   `[](auto w){ return true ? Inner{} : w; }` (with Inner::operator Outer) and
   `[](auto w){ return (w, Inner{}); }` (with operator,(Outer, Inner)) were typed Inner, but the
   compiler gives Outer. Rule now: the return type counts as proven only when no unknown-typed name
   the lambda binds can influence it. c_unknown_local_decides assumes every subexpression can,
   with two exceptions: the operand of sizeof/alignof, and the left operand of a comma when that
   operand is `(void)e` or `static_cast<void>(e)`. No function parameter has type void, so no
   operator, overload is viable there. The identifier-level frame stop from d5c9d634 can no longer
   be reached and is removed. Results: `(static_cast<void>(w), Inner{})` is inferred Inner;
   `return w;`, `return w.member;`, the conditional and the plain comma are withheld. Withheld
   means the lambda's result type is unknown, so the member call on it gets no link and keeps the
   pipeline's default stop; c_lsp.c writes no stop row for it.
   Fixture: lambdaret/main.cpp now also has the conversion, the comma overload, and the
   `conditional` and `comma` lambdas. g++ prints Outer::run for both of those lambdas and
   Inner::run for `discarded`.
   Fix round 3, e7ec052b: `[](auto w){ return sizeof(w) > 4 ? Inner{} : Outer{}; }` linked
   Inner::run, but C++ gives Outer. (a) The root defect, which applies outside lambdas too: a
   conditional_expression now has a proven type only when both branches evaluate to the same
   proven type (cbm_type_equal). Otherwise, and for a conditional with no consequence (GNU
   `a ?: b`), its type is unknown. (b) The sizeof/alignof exception is dropped from
   c_unknown_local_decides; only the void-cast comma exception is left. Round-3 item 2 (withheld
   link) is also fixed: c_text_is_compact skips comments, so `static_cast</* c */ void>(w)` counts
   as a void cast. Fixture: `sized` (g++ prints Outer::run; the resolver withholds) and `commented`
   (g++ prints Inner::run; the resolver infers Inner).

2. 3c4d7bc9 + 315a10db + 37561578 + 30066389  `#line` after same-line comment not blanked (preprocessor.cpp:455/619)
   Shape: `/* generated */ #line 900 "gen.y"` and `# /* marker */ line 950 "gen.y"`. Translation
   phase 3 turns each comment into a space before directives are read, so the compiler (and
   simplecpp) honour both lines. is_source_line_directive accepted only spaces and tabs around the
   tokens, so these lines were not blanked.
   Fix: skip_blanks_and_comments counts a block comment closed on the same line as a blank.
   Observation: tests/fixtures/graph/cpp/linecomment/main.c, rewritten for review findings
   (315a10db, 37561578, and finally 30066389). answer is defined normally at main.c:11 (corrected in 53f79f36). main comes
   after `# /* marker */ line 950 "gen.y"` and calls answer only through the object-like macro ASK
   (`return ASK != 42 + unused_marker;`, main.c:19), so that call exists only in the preprocessed
   pass. A #define sends the file through cbm_preprocess_with_map. The program prints
   "main at 951" and exits 0, so the compiler honours both directives. Expected: the call
   main -> answer at main.c:19. Without 3c4d7bc9 the call maps to gen.y:952, the main-file check
   rejects it, and the graph has no main -> answer edge. The earlier DEFINE_GETTER definition
   could not be adopted: cbm.c:4242-4251 requires definition syntax in the mapped original span.

3. 2dd31de6  operator[]/->/bool/copy-ctor/delete past depth cap silent
   Shape: in a subtree past C_LSP_MAX_WALK_DEPTH (512) these sites got no unresolved_sites row.
   The pipeline joins a stop row to its candidate by caller, span and leaf name:
   - `v[i]`: the row was named "operator", but the candidate is "operator[]";
   - `p->m`, `delete p`, `if (x)` / `while (x)` / `do..while` with a lone identifier condition, and
     `T b = a;`: the stop walk did not treat these as links, although extract_cpp_operator_call
     and extract_cpp_implicit_calls make candidates of them ("operator->", "~", "operator bool",
     "T").
   Fix: c_walk_cap_implicit_name emits each row under the candidate's own name and span, with the
   extractor's conditions mirrored. `T &r = a;` and `T *p = q;` build no object and get no row.
   Observation: tests/fixtures/graph/cpp/depthcap/main.cpp puts these statements 600 blocks deep.
   Under g++ -std=c++17 the program prints Box's copy constructor, operator bool, operator[],
   operator-> and destructor. Each of those sites now carries stop walk_depth_cap_512.

Not yet observed in the graph: the fixture harness (tools/fixtures/fixtures.py) runs only the
rust and python lanes. run_all loops `for lang in ("rust", "python")`, and only rust_truth and
python_truth exist. The three C/C++ programs above are therefore compiled and run, but not yet
indexed and compared. Proposed change (needs tools/fixtures/fixtures.py, not made): add a "cpp"
lane. Its truth step builds each program with `g++ -std=c++17 -g -finstrument-functions` (or under
gdb with breakpoint call-site tracing) to record caller/callee/line facts, and a "c"/"cpp" entry
goes into the `for lang in` tuple and into stage().

## OPEN

4. local-class methods past cap keep default reason (c_lsp.c c_record_walk_cap_stops)
   Found: past the cap, the cursor walk does not descend into a function_definition, because the
   method's caller QN is unknown there. Inside a function body, the normal (uncapped) walk also
   has no dispatch to c_process_class / c_process_function for a local class:
   c_resolve_calls_in_node_inner reaches c_process_function only in test-owner mode, at about
   line 5069. So the uncapped walk already gives a local-class method's calls the enclosing
   function as caller, and those rows cannot join the extractor's candidates either. A correct cap
   row needs the extractor's QN for a local-class method, and I found no local-class handling in
   extract_defs. Proposed direction (c_lsp.c only, not made for lack of time to check the
   extractor's QN): when c_record_walk_cap_stops meets a class_specifier or struct_specifier, call
   c_process_class on it. Its methods then run c_process_function with enclosing_class_qn set, and
   their bodies hit the cap with the method as caller. This is only right if extract_defs names a
   local class's method `<module>.<Class>.<method>`, which needs checking first.
5. registry unique_name fallback links a call the LSP declined
   Found: the fallback is in src/pipeline/registry.c (strategy "unique_name", about line 2024).
   The guards are cbm_weak_member_unique_name_exempt (registry.c:714) and pass_calls.c:1441-1445.
   This is outside the permitted files. Not investigated further.
6. out-of-range lines from .h/.cpp pairs sharing a QN
   Found: the definition/line merge is in the pipeline, not in c_lsp.c or preprocessor.cpp. Not
   investigated.
7. C/C++ lane in the fixture harness (see the proposed change under CLOSED). Without it no C/C++
   fixture is compared automatically.

## Test and fixture runs
These ran from /scratch/agent/wt/cpp between 03:07:47 and 03:25:59 UTC. The code was tip 30066389;
53f79f36 after it changes only a PROVENANCE text file, so the code is the same as the final tip.
A first run, started at 03:00 on 3d165b5d, was stopped once the round-3 code changes landed.

`scripts/test.sh --suites "c_lsp c_lsp_chain_binary c_lsp_chain_receiver stack_overflow_a stack_overflow_b stack_overflow_c extraction pipeline" BUILD_DIR=build/t-cpp` exited rc=1:
  c_lsp_chain_binary rc=0 pass=1 fail=0 skip=0
  c_lsp rc=0 pass=785 fail=0 skip=0
  c_lsp_chain_receiver rc=0 pass=1 fail=0 skip=0
  extraction rc=0 pass=487 fail=0 skip=0
  pipeline rc=1 pass=542 fail=0 skip=0
  stack_overflow_a rc=0 pass=7 fail=0 skip=0
  stack_overflow_c rc=0 pass=7 fail=0 skip=0
  stack_overflow_b rc=0 pass=6 fail=0 skip=0
  1836 passed, 0 failed, 0 skipped  (8 selected suites, 16 jobs)
The pipeline suite fails on sanitizer reports, not on any assertion. Its failure sites:
  - LeakSanitizer leaks: calloc in field_cache (internal/cbm/cbm.c:922), via
    cbm_ts_child_by_field_name, rmf_scan_item / rmf_scan_mod (internal/cbm/lsp/rust_modtree.c:852/665)
    and pxc_rust_scan_file (src/pipeline/pass_lsp_cross.c:4583). A second stack goes through
    cbm_pxc_dispatch_file_with_php_bindings (pass_lsp_cross.c:4137).
  - UBSan "null pointer passed as argument": objectscript_udl/scanner.c:122,
    internal/cbm/doclink_perl.c:1541/1548, src/pipeline/doc_links_perl.c:247.
None of these is in c_lsp.c, preprocessor.cpp or any C/C++ path this lane changed; the full log is
build/t-cpp/test-logs/failed/pipeline.log in the worktree. I have not checked whether main a76d3f4a
shows the same reports, because the run rules allow suites only on this lane. That run would decide
whether this is a pre-existing failure or one this branch causes.

`make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/cpp` exited rc=0:
  fixtures: 51 fixtures x 2 modes, 93 known mismatches, 0 failures, 40.8 s
(rust and python fixtures only. The harness has no C/C++ lane, so tests/fixtures/graph/cpp/* are
not indexed; see OPEN 7.)

Not pushed: lane/cpp is local, for the coordinator to merge.
