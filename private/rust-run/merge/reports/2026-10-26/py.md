STATUS: DONE
COMMIT: 3f5014f2 (lane/py, merged); 0db332d8 (lane/py-fix)
CHANGED: 3 files
CLOSED: 8 items
OPEN: 5 items
BLOCKERS: none (suites edge_imports and pipeline exit rc=1 on LeakSanitizer reports only; 0 test failures; no leak frame in this lane's code)
---
Lane: Python, worktree /scratch/agent/wt/py, branch lane/py (base a76d3f4a). 18 signed-off commits:
8 item commits, then 10 fix-round commits answering review findings A-N.
Files: internal/cbm/extract_imports.c, src/pipeline/pass_lsp_cross.c, src/pipeline/pass_calls.c
(3 files, +475/-41). No hunk touches the pxc_rust_* functions.
Production build `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/py -j4`: rc 0 under -Werror,
no warnings. All three files are clang-format clean.

Observations: each commit body names the item, the code shape, and the edge or stop record the fixture
should now give. These were derived from the code, not executed: before 03:00 UTC only compiling was
allowed, and no fixture program was indexed for the new shapes. The suite and fixture results are below.

## CLOSED

1. 5cf5e1d1: walrus in a branch's class bases/def defaults not seen (extract_imports.c:844).
   Shape: `if c: class C(B := f()): ...` / `def f(a=(d := 1)) -> (r := T)` / `@(deco := w) def g(x=(e := 2))`.
   py_ns_scan_rebinds skipped the whole definition or class, and the unproven top-level header only got an
   UNKNOWN fact. New py_ns_header_rebinds scans the parameters, return_type and superclasses fields.
   py_ns_decorated_header_rebinds also scans the decorators of an unproven top-level def.
   Expected: SHADOW UNSUPPORTED facts for B, d, r, deco, e.
2. e6026c5f: annotation-only `f: int` counted as a value binder (extract_imports.c:726/762).
   Shape: `def f(); if TYPE_CHECKING: f: Callable; f()`. py_ns_binding_identifier now returns false for an
   assignment that has no `right` field. Expected: no SHADOW for f, so f() links the def again.
3. fb099a1b: fastapi_depends still builds the IMPORTS map (pass_calls.c:1874).
   The pass now uses cbm_pxc_build_import_map / cbm_pxc_free_import_map, the same namespace-replay map the
   CALLS passes read. Depends(ref) links exactly when a bare ref() would.
4. 96e71791: same-file call to own def ignores branch rebind.
   Shape: `def f(); if c: f = other; def g(): f()`. New pxc_python_bound_before means a SHADOW UNSUPPORTED or
   a non-fitting OWN_OR_KEPT now blocks a name an earlier statement bound, even when the map has no entry
   for it. A fitting redefinition (`if c: def f()`, value over value) keeps the link.
   Expected: g's f() is withheld with an import_map_blocked row instead of CALLS g->f.
5. 1bbf05f4: star import of a branch-only binder does not override.
   Shape: m.py `if c: def h()`; app.py `def h(); from m import *; h()`. When the replay collects exports for
   a star import, a name first bound in an unmodelled statement gets a blocked entry. apply_star then
   blocks app's own h. Expected: h() is withheld instead of linking to app.h.
6. 47c0ba72: `global` write in a function not followed (the item says "__init__ function"; I read it as
   any function, methods such as __init__ included).
   Shape: `def handler(); def init(): global handler; handler = other`. New py_ns_global_writes appends,
   after the module's facts, an UNKNOWN EFFECT plus one SHADOW per name a function declares global and binds:
   - OWN_OR_KEPT/value when every binding is a plain assignment, so the lazy-singleton
     `global _inst; _inst = X()` keeps its USAGE link;
   - UNSUPPORTED otherwise.
   PARTIAL: this fixes the registry/import-map path and the cross-file final binding (`from m import
   handler` is withheld). The same-file lsp_direct edge from py_lsp.c remains (see OPEN).
   Not modelled: an `import` inside the function that binds the global name, and `global` in a class body.
7. 691e42b2: unbound-earlier reading links own node though a same-named submodule or module __getattr__
   binds at runtime (pass_lsp_cross.c:2552, review5 M1).
   In pxc_python_final_binding, when only an OWN_OR_KEPT binder binds the name and nothing earlier does,
   the verdict is now OPAQUE if the module has `__getattr__` (or an any-name binder), or if it is a package
   __init__.py whose directory provides the submodule (pxc_python_find_in). This covers cross-file reads.
8. 74ec337e: submodule import overwriting package attribute unmodelled.
   A BOUND, non-submodule binding of a name in a package __init__ whose directory has a submodule of that
   name is now OPAQUE. Exception: the package's own run imports that submodule no later than the name's
   last binder (`from .x import x`, `from . import x`, or absolute `...pkg.x...`), so the common re-export
   still links.
   Residue: inside __init__.py itself, a same-file bare read after `def util` followed by `from .util import y`
   is still not modelled in the importer's own map or in py_lsp.

## Review fix round (findings A-J on a76d3f4a..74ec337e)

A wrong-link finding has its fix committed separately, after the commit it fixes. No commit was removed.
- A (5cf5e1d1, lambda defaults skipped): fixed in 95cbdc21. A lambda gets py_ns_header_rebinds; only its
  body is skipped.
- F (47c0ba72, nested def headers skipped in global-write scan): fixed in 69a1eb36. py_ns_body_binds walks
  the parameters, return_type and superclasses of nested defs, classes and lambdas.
- G, H, I (74ec337e): fixed in da9547c6.
  - G: `from . import member` no longer counts as a load.
  - H: absolute imports no longer count; the binding stays OPAQUE (withheld).
  - I: the exemption needs an unconditional binder (own def, import, VALUE/DECORATED/UNPROVEN_CLASS
    shadow) at or after the load.
- J (74ec337e, relative level > 1 withheld a stable re-export): fixed in da9547c6. The relative target
  path is resolved from the package directory.
- E (691e42b2, deleted __getattr__ treated as live): fixed in 2ae41b61. The hook counts only when the last
  fact naming it is not a DELETE; an any-name binder after it still counts.
- C, D (1bbf05f4): fixed in a76f107e.
  - C: a name the star does not export keeps an importer entry the importer already holds.
  - D: pxc_python_star_keeps keeps a held entry equal to the star module's own node when every binder of
    the name in that module is an own def or a fitting OWN_OR_KEPT, and the module has no star or
    any-name binder.
- B (fb099a1b): partial, in c2998a7a. Wrong-link side: Depends(ref) is withheld when more than one
  top-level fact (star and any-name included) names the ref's root. This prevents `from a import x;
  def r(d=Depends(x)); from b import x` linking b.x, a case I found next to B. Withheld side: see OPEN 5.

## Review fix round 2 (findings K, L)
- K (2ae41b61, wrong link: a star import after `del __getattr__` can restore the hook): fixed in
  6c6eb7f7. An IMPORT_STAR fact counts as possibly binding the hook; only a later DELETE clears it.
- L (a76f107e, withheld link: the __all__ loop wrote back the blocked value): fixed in c5007861. The
  same pxc_python_star_keeps preservation now applies in the __all__ loop.

## Review fix round 3 (finding M)
- M (6c6eb7f7, withheld link: every star import marked the hook live): fixed in e3e3db94.
  pxc_python_star_may_bind_hook marks the hook live only when one of these holds:
  - the star's source is unlocated, or its facts are unreadable;
  - its export set may hold __getattr__ (pxc_python_name_is_exported; a non-literal __all__ counts as
    live).

## Review fix round 4 (finding N, final round)
- N (e3e3db94, wrong link: the export set was read from ALL_* facts only): fixed in 3f5014f2.
  pxc_python_exports_known treats the export set as known only in one of two cases:
  - the source has no star import, no any-name binder, and no other binder of __all__ (import or branch);
  - or a literal `__all__ = [...]` comes after every such fact, followed only by literal appends.
  Otherwise the star marks the hook live.

## OPEN

1. Aliased imports link a replaced def through lsp_import_alias. The edge comes from
   internal/cbm/lsp/py_lsp.c:2869-2888, outside this lane's files. The alias join trusts the import
   binding's NAMED type and does not check whether a later statement replaced the binding. Proposed change
   in py_lsp.c: before `imported_alias = true`, require that the binding is still the import's own
   binding, i.e. that py_invalidate_possible_bindings has not invalidated it (the same check lsp_direct
   relies on). Alternatively, the CALLS pass could veto an lsp_import_alias row whose name the replay map
   holds as blocked ("").
2. Function-local `del` of a non-decorated name still links. Function-scope local binding lives in
   internal/cbm/extract_calls.c:4532 (python_callee_is_bound_parameter / cbm_walk_python_param_is_bound) and
   in py_lsp.c's function scope. Neither treats a `del NAME` in a function body as making NAME local.
   Proposed change: add delete_statement targets to the function's local-binder set in both places, so the
   call becomes callee_is_locally_bound and is withheld.
3. Same-file part of the `global` write item (see CLOSED 6). py_lsp.c binds the module function in pass 1
   (py_bind_module_function) and ignores `global` writes in function bodies, so g's handler() still gets
   lsp_direct. Proposed change in py_lsp.c pass 1: pre-scan function bodies for `global N` plus a binding of
   N, the same rule as extract_imports.c py_ns_global_writes, and call py_invalidate_binding_target for N
   after the module run. Alternatively, have pass_calls drop an lsp_direct row whose name the replay map
   blocks; that is a pass_calls.c-only change but I did not make it, because it changes LSP precedence for
   every Python call and needs measurement.
4. FastAPI lsp_cross_prepare takes about 535 ms against about 35 ms on main. Not investigated: it needs a
   profiled index run, and no index run was allowed before 03:00 UTC. The CBM_PROF lsp_cross_prepare
   per-builder split (pass_lsp_cross.c ~2644) is where to look first. A likely candidate is the Python
   facts re-parse in pxc_python_module_facts when the facts cache misses.

5. Review finding B, withheld-link half: `from deps import get_db; def route(db=Depends(get_db));
   del get_db` is still withheld, where the old IMPORTS map linked it, and no unresolved record is written
   for a withheld Depends reference. Proving it needs the binding in force at the def's position, and
   CBMPyNamespaceFact (internal/cbm/cbm.h) carries no statement position. Proposed change, outside this
   lane: add the statement start byte to CBMPyNamespaceFact, and replay the facts up to the def's
   start_byte for Depends. To record the stop, call cbm_pipeline_record_unresolved_call with reason
   name_binding_not_proven for the Depends call site.

## Tests
All runs started after 03:00 UTC on the final tip 3f5014f2. The sanitized build compiled
pass_lsp_cross.c after the last edit.

`scripts/test.sh --suites "extraction,edge_imports,pipeline,call_reference_contract" BUILD_DIR=build/t-py`
(log: /scratch/agent/py-tests.log):
  call_reference_contract rc=0 pass=15 fail=0 skip=0 secs=2
  edge_imports rc=1 pass=98 fail=0 skip=0 secs=18
  extraction rc=0 pass=487 fail=0 skip=0 secs=23
  pipeline rc=1 pass=542 fail=0 skip=0 secs=91
  1142 passed, 0 failed, 0 skipped  (4 selected suites, 16 jobs; selection fd7ebc61e69856bab31bff2edfd5127c23da7635ddf792c3716ee95a280c9525)

Both rc=1 suites have no assertion failure. Their rc comes from LeakSanitizer:
- edge_imports: "SUMMARY: AddressSanitizer: 712752 byte(s) leaked in 30 allocation(s)."
- pipeline: "SUMMARY: AddressSanitizer: 19326672 byte(s) leaked in 988 allocation(s)."
- pipeline also has UBSan null-argument reports in objectscript_udl/scanner.c:122, doclink_perl.c:1541/1548
  and doc_links_perl.c:247.
- Every leak frame is in one of these places:
  - worker-thread caches: helpers.c ks_get/cbm_kind_in_set, cbm.c field_cache;
  - extract_unified.c / extract_usages.c / extract_defs.c;
  - the Rust paths: rust_modtree.c, rust_lsp.c rust_prove_macro_usages, pxc_rust_scan_file.
- No frame is in py_ns_*, pxc_python_*, or the fastapi / Depends code.
- I did not run the same suites on base a76d3f4a, so whether these leaks also occur on base is not
  measured.
Failing-suite logs: /scratch/agent/wt/py/build/t-py/test-logs/failed/.

`make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/py` (log: /scratch/agent/py-fixtures.log):
  fixtures: 51 fixtures x 2 modes, 93 known mismatches, 0 failures, 50.5 s
The fixture set is all Rust (51 rust/* fixtures, no Python fixture), so it does not exercise this lane's
Python shapes. Every Python observation in the commit bodies is still derived from the code, not executed.
Next step: write Python fixtures for the shapes in each commit body.

Production build `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/py -j4` on 3f5014f2: rc 0,
no warnings (-Werror).

## Gate edge diff (lane/py merged at main 0cac5290, against the rust-base graphs)
Read from the Python sources in ~/Developer/cbm-bench/{ruff/ruff,pm/pm} and the base and gate score.db
edge rows. Each item below says whether the runtime binds the name to the target. No code was edited.

Convention used for USAGE/WRITES to a module-level Variable node. The base graph uses one Variable node per
module global name (QN module.name). Every read or write of that global, in module code or through
`global`, links to that node whatever value it holds at the time. So when the runtime binds a module
global to that node's name, the link is correct even if the global was rebound later.

### ruff: CALLS, 1 removed, a withdrawn wrong link
- pylint/global_statement.py:55 `CONSTANT()` in global_function_assign -> global_statement.CONSTANT
  (the Variable at line 4, `CONSTANT = 1`; base strategy same_module).
  - At runtime, line 52 (`global CONSTANT; def CONSTANT(): pass`) binds the global to the nested function,
    so the call reaches that nested function. The nested function has no graph node.
  - The edge to the int Variable was wrong. 47c0ba72 withdrew it: the global write becomes an UNSUPPORTED
    SHADOW, and 96e71791 blocks the name.

### ruff: DECORATES, 2 removed (withdrawn wrong links) and 2 added (correct)
- removed: corpus/classliteral_decorators_cycle.py:24 `@name_3 class name_1` -> name_3 (the Function at
  line 15). The def at line 15 is itself decorated (`@name_1`), so name_3 never holds that function object:
  - if the decoration ran, name_3 holds name_1(f); here name_1 takes 0 arguments, so that call raises
    TypeError;
  - otherwise name_3 holds what `from .. import name_3` (line 4) bound.
  So the edge was wrong. It was withheld through the try/else and finally binders: 96e71791 makes a branch
  rebind of a name bound earlier block it.
- removed: corpus/cyclic_symbol_in_comprehension.py:54 `@name_3 async def name_5` -> name_3 (the Function
  at line 23 of the module). Line 52 `(name_3 := 0)` runs unconditionally at module level, so at line 54 the
  decorator is the int 0, never the def. The edge was wrong; 96e71791 withheld it (walrus SHADOW after
  earlier binders).
- added: DECORATES name_1 -> <decorator:name_3> and name_5 -> <decorator:name_3>, plus the matching 2 USAGE
  edges to the same placeholders. These are the base's own convention for a decorator with an unproven
  target: the base has 202 `<decorator:...>` nodes and 13588 DECORATES edges to them. They name the spelling
  and claim no definition, so they are correct.

### ruff: USAGE, 43 removed
- 5 withdrawn wrong links, cross-file name guesses. Each file binds the name itself with `with ... as` and
  imports nothing that binds it:
  - ruff/statement/with_39.py -> flake8_bandit/S603.cmd
  - ruff/statement/with.py -> black/cases/remove_parens.example1 and .example3
  - black/cases/remove_with_brackets.py -> remove_parens.example1
  - black/cases/parenthesized_context_managers.py -> remove_parens.example1
  The second `with ... as` of the name now finds it bound earlier and blocks it (96e71791). Before, the
  absent entry fell through to the registry's unique-name guess.
- 5 withdrawn wrong links to Function or Class targets whose definition the read cannot reach:
  - classliteral_decorators_cycle name_1 -> name_3, and cyclic_symbol_in_comprehension (module and name_5)
    -> name_3: the same reasoning as the DECORATES items above.
  - corpus/cyclic_lambdas.py -> name_3 (Class at line 12): it is decorated with `@(*name_2,)`, so name_3
    holds the decorator's result, not the class.
  - ruff/statement/match.py -> foo (Function at line 54): the reads at lines 2, 10 and 19 come before the
    def (unbound at runtime). The later reads can follow the case-guard walrus `foo := 1`.
- 33 regressions: module-level Variable targets whose name is rebound by a compound statement, a walrus,
  `with ... as`, a tuple target, `del`, or a function's `global` write. Every one of these reads is a read
  of that module global, so under the convention above the runtime binds the name to the node:
  - flake8_bugbear B020 values, B909 myset/some_list/some_other_list
  - C417_1 a, SIM112 env, PERF203 i, PERF401 G_INDEX, E741 a and l
  - global_statement CONSTANT (fix_constant, global_del, global_operator_assign)
  - load_before_global_declaration x, UP028_0 SOME_GLOBAL
  - FURB101_2 f, FURB103_2 f (module, `_`, `_set`), FURB131 y, RUF019 k
  - black collections x, pattern_matching_generic match (module level), pep_572 x
  - pep_572_remove_parens test and y (await_the_walrus reads the global y), named_expr y, assign a
  - corpus 04_assign_named_expr x, 10_if_with_named_expr x, cyclic_type_alias name_4 (module and name_1)
  Cause: 96e71791. In pxc_python_replay_namespace, the SHADOW-UNSUPPORTED branch and
  pxc_python_replay_own_or_kept now call pxc_python_bound_before and set the map entry to "" (blocked) for
  a name an earlier statement bound, even when that earlier binder was the module's own Variable. Before,
  the name had no entry and resolved to the same-module Variable. The usage pass then meets
  import_map_blocked.
  47c0ba72 contributes the trailing global-write SHADOWs (CONSTANT, G_INDEX, SOME_GLOBAL, x) that trigger
  the same path.

### ruff: WRITES, 43 removed
- 1 withdrawn wrong link, cross-file: pyupgrade/UP036_4.py `cmd = [...]` (in `if` blocks) ->
  flake8_bandit/S603.cmd.
- 4 withdrawn wrong links, writes to a local attributed to the module global:
  - E741.f2 -> E741.l: f2's `l = 0` is local, and f3's `nonlocal l` refers to it;
  - RET504.nonlocal_assignment -> RET504.X: `nonlocal X` refers to the enclosing function's X at line 204;
  - pattern_matching_generic.get_grammars -> match: lines 42-44 are locals, with no `global`;
  - RET504 file node (`py.__file__`) -> X: no module-level statement writes X; only `class X` at line 126
    binds it.
- 38 regressions: module-level and `global` writes to module Variables, the same names and the same cause
  as the 33 USAGE regressions, plus non_augmented_assignment a (`a = a+-1`, line 54),
  assignment_split_value_first b, F811_31 foo and bar (`foo = None` at module level, then a conditional
  `from some_module import foo`; the write at line 5 is to that global), E741 f1 (`global l; l = 0`), and
  RET504.global_assignment -> X (`global X; X = 1` writes the global named X, whose node is the Class).
  Cause: the 96e71791 bound_before block, with 47c0ba72 for the `global` writers.

### pm: 8 removed (USAGE 5, WRITES 3), all withdrawn wrong links
Every one is a cross-file name guess with no import between the files:
- crate-decomposition-2026-10-06/analyze.py, analyze2.py and issue-20312-verification-2026-10-06/history.py
  -> 2026-09-23-clone-screener-eval/analyse.s;
- analyze.py -> subfamilies.m;
- fn_sizes.py -> subfamilies.crate.
In each source file the name is the file's own loop or assignment target, e.g. analyze.py:76
`m, t = module_of(...)` and :79 `s = o['symbol']`, fn_sizes.py:39 `crate = ...` in a loop, history.py:17
`for uuid,(path,s) in ...`. So the runtime never binds it to the other file's variable. They are now
withheld because a later rebind of the same name in the file blocks it (96e71791) instead of falling
through to the unique-name guess.

### Proposed fix for the regressions (for the new worktree; I have made no edit)
In src/pipeline/pass_lsp_cross.c pxc_python_replay_namespace, at both pxc_python_bound_before call sites
(the SHADOW-UNSUPPORTED branch and pxc_python_replay_own_or_kept), block a name that has no entry only when
the module's own node for it (pxc_python_own_node) is not a Variable, or when no own node exists.
- For a Variable node, the absent entry keeps resolving to the module global, as before 96e71791. That
  restores the 33 USAGE and 38 WRITES regressions.
- The fixes these commits made still hold for a branch rebinding an own Function or Class (96e71791, the
  DECORATES cases): those names stay blocked.
- Exception: the CALLS global_function_assign -> CONSTANT case. Its own node is the Variable, so the
  exemption alone would bring that wrong CALLS edge back. Keeping it withdrawn needs one of two things:
  - the shadow carries its binder kind: extract_imports.c py_ns_rebinds already computes PyNsBinder, and
    can keep def_kind on an UNSUPPORTED SHADOW; the exemption then applies only when no shadow of the name
    is a function or class binder;
  - or a CALLS row whose target is a Variable is not published from this map entry.
- The withdrawn cross-file guesses stay withdrawn. I checked the gate dbs: none of with_39.cmd,
  UP036_4.cmd, with.example1, analyze.s, analyze.m, fn_sizes.crate or history.s has an own node in its file,
  so these names keep their block.

## Regression fix (lane/py-fix, commit 0db332d8 on main 5212666c)
Commit 0db332d8 "fix(python): a module Variable rebound by a value stays linked to its global".
It changes only src/pipeline/pass_lsp_cross.c (Python functions, +30/-5). The commit sits on 5212666c,
which was the tip of main when I committed; `git rebase main` had nothing to do then. main has since moved
to 028c7fb1 and I have not rebased. As instructed, I made no further change.

Rule: an unmodelled rebinding leaves a name resolving to its own module Variable unless the file calls
that name.
- Mechanism: pxc_python_variable_kept, read in the SHADOW-UNSUPPORTED branch of
  pxc_python_replay_namespace. PXCPythonResolver.own carries the file result.
- Why not the def_kind option: I tried it first, keeping a function or class def_kind on UNSUPPORTED
  SHADOWs in extract_imports.c. cbm_py_namespace_facts_valid (internal/cbm/cbm.c:131) rejects a def_kind
  on a non-OWN_OR_KEPT SHADOW, so the whole file's facts became INCOMPLETE and the replay switched off.
  On a 3-file fixture indexed with index-local.sh, the CALLS global_function_assign -> CONSTANT and the
  DECORATES classliteral name_1 -> name_3 edges came back. I reverted that; extract_imports.c is unchanged
  from main.
- Why not "no CALLS to a Variable": that rule lives in pass_calls.c / pass_parallel.c, outside the files
  this fix may touch.
- The rule chosen keys the exemption on a call through the name, the one use that can publish a
  definition the Variable does not stand for.
- Fixture check on the final code: B020 USAGE/WRITES -> values are restored; there is no CALLS
  global_function_assign -> CONSTANT; DECORATES name_1 -> <decorator:name_3> is as on main.

Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/py-fix -j4` gave rc 0, no warnings
(-Werror).

Ruff score, run as gate.sh runs it (CBM_RUST_BENCH=~/Developer/cbm-bench/ruff,
CBM_RUST_CORPUS=~/Developer/cbm-bench/ruff/ruff, scripts/rust-score.sh on the 0db332d8 binary): EXIT=0
in 50s. tools/rust-bench/edgediff.py against ~/Developer/cbm-bench/rust-base/ruff/cache/score.db
(lists in /scratch/agent/pyfix/edges-ruff/{removed,added}.tsv):
  edges base=410752 new=410729 removed=27 added=4
  CALLS removed=1 added=0
  DECORATES removed=2 added=2
  USAGE removed=14 added=2
  WRITES removed=10 added=0
The gate on main removed 89 and added 4; this branch restores 62 of those 89 rows:
- 61 of the 71 regressions;
- E741.f2 WRITES -> E741.l. This is a base edge I classified as a wrong link: f2's `l = 0` is a local, so
  it is an extractor scope misattribution, not a map decision. It returns with the base behaviour for
  that Variable.
The 71 are therefore not all back. 10 stay withheld by the rule, listed below.

All 27 remaining removed rows:
- 17 withdrawn wrong links, unchanged from "Gate edge diff":
  - CALLS global_function_assign -> CONSTANT;
  - DECORATES classliteral name_1 -> name_3 and cyclic_symbol name_5 -> name_3;
  - USAGE classliteral name_1 -> name_3, cyclic_symbol (module, name_5) -> name_3,
    cyclic_lambdas -> name_3, match.py -> foo;
  - USAGE cross-file: with_39 -> S603.cmd; with.py -> remove_parens.example1 and .example3;
    remove_with_brackets and parenthesized_context_managers -> remove_parens.example1;
  - WRITES UP036_4 -> S603.cmd; RET504.nonlocal_assignment -> X; RET504 file node -> X;
    pattern_matching_generic.get_grammars -> match.
- 10 regressions still withheld:
  - 7 rows of pylint/global_statement.py CONSTANT: USAGE fix_constant, global_del, global_operator_assign;
    WRITES module, fix_constant, global_operator_assign, multiple_assignment. The file calls CONSTANT() at
    line 55, the very call whose CALLS edge must stay withdrawn; the map cannot separate those reads from
    that call.
  - 2 rows of black/cases/pattern_matching_generic.py match: USAGE module and WRITES module. The file
    calls `match()` (line 3, `with match() as match`); after the rebind the call reaches whatever `as`
    bound.
  - 1 row, WRITES RET504.global_assignment -> X. `global X; X = 1` writes the global named X, but its own
    node is `class X` (line 126). The exemption covers Variables only, so that Function and Class own
    nodes keep 96e71791's block.
Added (4): the DECORATES and USAGE edges to <decorator:name_3>, as on main. They are correct (see "Gate
edge diff").

Suites: `scripts/test.sh --suites "extraction,edge_imports,pipeline,call_reference_contract"
BUILD_DIR=build/t-py-fix` on the 0db332d8 tree (log: /scratch/agent/pyfix/tests.log):
  call_reference_contract rc=0 pass=15 fail=0 skip=0 secs=4
  edge_imports rc=1 pass=98 fail=0 skip=0 secs=30
  extraction rc=0 pass=487 fail=0 skip=0 secs=33
  pipeline rc=1 pass=542 fail=0 skip=0 secs=123
  1142 passed, 0 failed, 0 skipped  (4 selected suites, 16 jobs; selection fd7ebc61e69856bab31bff2edfd5127c23da7635ddf792c3716ee95a280c9525)
The two rc=1 exits are LeakSanitizer only:
- edge_imports: "SUMMARY: AddressSanitizer: 614448 byte(s) leaked in 26 allocation(s)."
- pipeline: "SUMMARY: AddressSanitizer: 16254048 byte(s) leaked in 850 allocation(s)."
There are 0 test failures, which is the known state.
