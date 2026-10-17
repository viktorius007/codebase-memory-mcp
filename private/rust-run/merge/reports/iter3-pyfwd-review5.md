VERDICT: PASS
SHA: 7a8a448f
BLOCKING: 0
MAJOR: 1
NOTES: 5

---

The branch is safe to land. Landing it adds no wrong link that main does not already publish, and every link main published that the branch drops is recorded with a `name_binding_not_proven` row. Both drivers gave identical results on every fixture.

The one major finding is a hole in the new rule. In some cases it publishes again a wrong link that main publishes and that 82b8d902 had withheld with a row.

What I ran:
- **Builds:** four binaries — main ebcbd291, 82b8d902, the tip 7a8a448f, and the merge of main and the tip (tree 89bd8724, built without a commit).
- **Merge tree:** byte-identical to the /tmp/py5 gate clone (b9a7012c).
- **Merge vs tip:** identical on all fixtures; the only differences were row order on the parallel driver.
- **Drivers:** every fixture was indexed both as a sequential run (1–43 files) and as a parallel run (61–139 files), and the logs confirm the mode.
- **Oracles:** CPython 3.15's `symtable` for locality, and real imports run with the `X`/`Y` environment variables unset and set for what a name is bound to at runtime.

## Fix before next dispatch

**M1. When nothing earlier in the module binds a name, the rule treats it as unbound and links the module's own node. But Python then falls back to a submodule of the same name or to a module `__getattr__`.**
- **Code:** pass_lsp_cross.c:2552-2553. With `kept` set and the earlier reading ending at `PXC_PY_UNBOUND`, the verdict becomes BOUND to the module's own node. This runs before the submodule fallback in `pxc_python_member_binding` (:2636) can apply. The submodule's own Module node does not exist in the graph, because the `__init__` def has the same QN and replaces it.
- **Consequence:** CALLS and CALL_REFERENCE edges point at a function that is not what the import binds when the branch does not run. 82b8d902 recorded a row for each of these.
- **Probe:** unaliased importers (`from M import N; N(); print(N)`), with identical results on both drivers.

| case | merge | main | 82b8 | CPython, X unset / X set |
|---|---|---|---|---|
| pa2/`__init__`: `if X: def sub`, plus a file pa2/sub.py | `uu_pa2.u CALLS` and `CALL_REFERENCE Function graph.pa2.sub` | same | rows at uu_pa2.py:4 and :5 | `module pa2.sub` / `function pa2.sub` |
| pa3: `from pa3 import sub`, then `if X: def sub` | `CALLS Function graph.pa3.sub` | same | rows | `module pa3.sub` / `function pa3.sub` |
| a5: `if X: def f`, plus a module `def __getattr__` | `uu_a5.u CALLS` and `CALL_REFERENCE graph.a5.f` | same | rows | `builtins.len` / `function a5.f` |

- **Not blocking:** main publishes the same links.
- **Suggested fix:** when the reading ends unbound, withhold if the owner is a package that has a submodule file of that name, or if the facts contain an own definition of `__getattr__`.

## Notes

- **N1. A walrus inside a branch's class bases or def defaults is not seen, so a link to the def survives.**
  - **Code:** `py_ns_scan_rebinds` does not descend into class or function headers (extract_imports.c:844).
  - **Cases:** w1 is `if X: def f; class K((f := object))` and w2 is `if X: def f; def g(a=(f := len))`.
  - **Result:** the merge and main link `uu_w1.u` and `uu_w2.u` to `graph.w1.f` and `graph.w2.f`. 82b8 recorded rows.
  - **CPython:** with X set, f is `builtins.object` (w1) and `builtins.len` (w2); with X unset the import fails. The link is wrong in every run.
  - **Pre-existing:** the same gap exists at module level. All three builds link the w3, w4 and w5 forms. The pattern is contrived.
- **N2. Some correct links main published are now withheld, each with a row.**
  - a24 is `def f` followed by `if X: f: int`. The facts count an annotation alone as a value binder (extract_imports.c:726/:762), but CPython binds `function a24.f` in every run.
  - a39 is `while X: f = f`.
  - m1 is a module-level `type handler = int` before `@wrap def handler`. 82b8 published the USAGE; main and the merge record a row. symtable reports handler global in the reader.
  - gl.py: `global handler; del handler` in one function withholds the USAGE edges of readers defined after it, as `global h; h = 3` already did at 82b8.
- **N3. The Python resolver is slower than main, but not because of these three commits.**
  - FastAPI (1141 files): lsp_cross_prepare takes 38 ms on main and 539 ms on the merge, and the whole pipeline goes from 1032 to 1545 ms.
  - ruff: 1437 vs 1459 ms.
  - The fix lane measured 526 ms at 7fcdc632, and the fourth review (N5) already recorded it.
- **N4. Aliased imports (`from a27 import f as a27_f`) still give main's wrong CALLS edges** through `lsp_import_alias` and `lsp_callable_alias`, even where the binding is withheld.
  - Example: `u_a27 CALLS a27.f` with strategy `lsp_import_alias`, while every run binds `a8b.f`.
  - It is identical on main and 82b8; this is the fourth review's N2.
  - With unaliased imports the same cases give rows only.
- **N5. A submodule import overwrites a package attribute, and the facts do not model that.**
  - Case: pd3 is `def sub`, then `if X: from .sub import g`.
  - With X set, CPython binds `module pd3.sub`.
  - All three builds link `Function pd3.sub`, so this is not new.

## The fix lane's three open items: all are on main

All three were probed on main and on the merge, with the same output on both drivers.
1. **Same-file call when a branch rebinds the own def:** `o1.caller` and `o1b.caller` CALLS `o1.f` and `o1b.f` on main, 82b8 and the merge.
2. **Star import of a module that binds the name only in a branch:** `o2.caller` CALLS `o2.g` on all three.
3. **Function-local `del` of a name that is not decorated:** `o3.r1` CALLS `lib.f`, and `o3.r2` CALLS and USAGE `o3.h`, on all three.

## Checked and found correct

- **B1 (`del` and `type` in a function), 26 shapes:** the decorated-name USAGE agrees with symtable in every case, on both drivers.
  - Local, so a row: plain, parenthesised, list and multi-target `del`; `del` inside an `if`; a function-level `type` statement; a lambda or nested function reading a name that a later `del` or `type` makes local.
  - Global, so USAGE: `del h.attr`, `del d[h]`, `del (xs[0], h.attr)`, `type T[h] = ...`, `type X = h`, `type T[U] = h`, `del` or `type` in a class body (also at module level), a `del` in a nested function, and `h` as part of a `for` iterable `h, xs` (the new expression_list case).
  - Other reads of h in the same function that stay global and keep the USAGE: a return or assignment tuple, a call argument, `with open(h) as (a, b)`, and a comprehension.
- **The new rule against CPython** (24 module shapes plus packages):
  - Correctly linked: if/else defs, defs in a `for` or `with` body, async defs, `X = []; X += [1]`, `f = 1; f += 1; if X: f = 2`, a star import that does not export the name followed by a branch def, `def; del; if X: def`, and a module importing its own name.
  - Correctly withheld with rows (main linked them wrongly): a star import that exports f followed by a branch def, a def in one arm and a value in the other, a try-import with a def fallback, a def then a class, `globals()`, `from lib import f; f += 1`, a nested `if` that reassigns f, a decorated def in a branch, a branch import, and the same-file `from lib import f; if X: def f`.
  - New correct link: pd1 `from .impl import f; from pd1 import f` now links `pd1.impl.f`.
- **Imported `__all__` shapes:**
  - The merge equals main for `xa + [...]`, `[...] + [...]`, `tuple()`, an annotated assignment, `.append` and a tuple literal.
  - `del __all__` is now a row; main linked a name that does not exist at runtime.
  - A branch-only `__all__` assignment is a row on all builds.
- **Silent drops:** none. Every edge main has and the merge lacks, in all fixtures and in ruff, either carries a row or is replaced by the correct edge.
- **Corpora:**
  - ruff, main vs merge: only +3 USAGE (`write_to_debug_expr` → x, y, z) and −3 rows; nothing removed.
  - bitcoin, gate graph vs the frozen base: +4 CALLS (address.py → messages.hash256 ×2 and sha256 ×2); otherwise only SEMANTICALLY_RELATED churn (−4/+3, plus 11 changes in properties only).
  - FastAPI, main vs merge: only SEMANTICALLY_RELATED edges are removed.
- **Upgrade and incremental paths:**
  - An index written by main and re-indexed by the merge build after an edit equals a fresh index (it forces a full rebuild because the extractor build changed).
  - Editing branch binders on the merge build equals a fresh index on both drivers.
- **Suites:** the six suites at the tip pass, 1336/0. The gate on the identical merge tree shows build, 8 suites (2224/0) and lint PASS. Its only failure is cpp-bitcoin DIFFERENT, which is the expected change above.

## Could not do

- I did not run `lint-ci` myself; I relied on the gate, whose tree is identical to my merge.
- I did not re-run the codex, pm or bevy scores.

My scratch in /tmp/rv6 is trashed. My worktree is back on its own branch, so fix/py-fwd-r3 is free to check out elsewhere. Nothing was written under `private/` or the corpora.
