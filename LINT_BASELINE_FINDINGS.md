# Baseline lint findings — branch `fix/baseline-lint`

Base commit: `ba72e3ff`. All measurements taken with the project's own toolchain
(`/opt/homebrew/opt/llvm/bin/{clang-tidy,clang-format}`, cppcheck 2.21.0) via
`make -f Makefile.cbm` targets, not by hand-invoking the linters.

## Measured baseline (four legs, not three)

`scripts/lint.sh` runs four targets. Three were red at `ba72e3ff`:

| Leg | Status at base | Finding |
|---|---|---|
| `lint-format` | RED | 5 files in `src/pipeline/` |
| `lint-cppcheck` | RED | 1 hit: `arrayIndexOutOfBounds` in `src/store/store.c:5623` |
| `lint-no-suppress` | RED | **not reported by anyone** — `internal/cbm/extract_defs.c:6936` |
| `lint-tidy` | RED | 5,263 errors across 96 files (see "Left deliberately") |

Note on scope: `LINT_SRCS` in `Makefile.cbm` does **not** include `$(UI_SRCS)`,
so `src/ui/*.c` is not gated by `scripts/lint.sh`. A raw glob over `src/**/*.c`
reports `src/ui/httpd.c` as format-dirty, but the gate never looks at it.
`src/ui/*.h` *is* gated, via the separate `LINT_HDRS` wildcard.

## What changed

### 1. `style(pipeline): apply clang-format to the five drifted files` (b8f97327)

Category: **pure formatting**. Files: `pass_githistory.c`, `pass_parallel.c`,
`pass_pkgmap.c`, `pipeline.c`, `registry.c`.

Behaviour-neutrality proof — not an assertion, a measurement. Each file was
compiled before and after with the project's `CFLAGS_COMMON` at `-O1`, and the
resulting object files hashed:

```
OBJECT-IDENTICAL pass_githistory.c
OBJECT-IDENTICAL pass_parallel.c
OBJECT-IDENTICAL pass_pkgmap.c
OBJECT-IDENTICAL pipeline.c
OBJECT-IDENTICAL registry.c
```

The comparison was itself verified to be discriminating: mutating a single
string literal in `registry.c` (`"iter_mut"` → `"iter_MUT"`) produced
`INSTRUMENT OK: mutant detected (5806fd2ae0d74c93 vs 36e8caf30f45aa9d)`. A
`git diff -w` check was *not* treated as sufficient, because clang-format
reflows lines across boundaries and `-w` would hide a genuine token move.

### 2. `fix(lint): whitelist rust_subtree_has_test_ident` (7dd05568)

Category: **lint metadata**, zero runtime reach.

`scripts/check-nolint-whitelist.sh` enforces a two-step contract documented in
`src/foundation/recursion_whitelist.h`: a recursive function needs *both* the
`NOLINT(misc-no-recursion)` marker *and* a whitelist entry. Commit `3ddde64c`
(2026-07-05, "flag defs inside Rust `#[cfg(test)]` mod") added the marker and
skipped the entry, so this leg has been red on every run since.

The function is genuinely bounded — `max_depth` is an explicit parameter
decremented on each descent, so termination does not depend on tree shape,
which is the substantive criterion the whitelist encodes.

`recursion_whitelist.h` is not `#include`d by any translation unit (verified by
`rg`); it is consumed only by `Makefile.cbm` and the check script. Editing it
cannot alter runtime behaviour.

### 3. `fix(lint): suppress unreachable cppcheck bound in store.c` (2e680a1e)

Category: **false positive → narrow justified suppression**. Also folded in the
`src/ui/httpd.c` format fix (object-identical; ungated, done for tidiness).

**Adjudication.** In `arch_packages_from_qn` (`src/store/store.c`):

- `pnames`/`pcounts` are declared `[CBM_SZ_64]`, and `CBM_SZ_64 == 64`.
- `np` starts at `0`. The **only** increment is inside
  `else if (np < CBM_SZ_64) { …; np++; }`, so `np` is bounded above by 64.
- `MAX_PREVIEW_NAMES` is `#define`d to `ST_MAX_PKGS`, which is `64`.
- `np` is never aliased (`&np` appears nowhere in the function) and never
  assigned elsewhere except `np = MAX_PREVIEW_NAMES` *inside* the guard.

Therefore `if (np > MAX_PREVIEW_NAMES)` is `if (np > 64)` where `np <= 64` —
the body is unreachable, and `free(pnames[64])` cannot execute. cppcheck
evaluates the loop body in isolation from the enclosing guard and reports the
index it would reach if the guard admitted entry.

**Why the dead block stays.** `git show 18fa9979:src/store/store.c` shows
`#define MAX_PREVIEW_NAMES 15` — the cap was raised to 64 later, to keep the
`packages` and `layers` aspects enumerating the same package universe (the
in-file comment at line ~5538 records that small single-file packages appeared
in `layers` but silently vanished from `packages`). The truncation block is the
retained safety net for the cap being lowered again. Deleting it would be a
behaviour change on a future edit, so it was left in place.

This is **not a real bug**: no out-of-bounds access is reachable, so there is
nothing to red-prove. Fixing it "for real" would mean deleting live-looking
defensive code on a false alarm.

**Suppression scope.** One `// cppcheck-suppress arrayIndexOutOfBounds` on the
single offending line, with the reasoning in a block comment above the guard.
It is not a file-level or check-level rule, and nothing was added to
`.cppcheck`. The repo already uses this inline idiom (`src/daemon/host.c`,
`src/foundation/platform.c`, `src/pipeline/pass_route_nodes.c`), and
`lint-no-suppress` bans only `NOLINT` forms, not `cppcheck-suppress`.

The store.c edit is comment-only: `OBJECT-IDENTICAL store.c (comment-only)`.

## Left deliberately: `lint-tidy` (5,263 errors, 96 files)

This is a pre-existing backlog, not drift I could mechanically clear, and it is
**out of scope for a cleanup branch**. Breakdown:

| Check | Count |
|---|---|
| `readability-magic-numbers` | 4,034 |
| `misc-include-cleaner` | 405 |
| `readability-function-cognitive-complexity` | 275 |
| `readability-braces-around-statements` | 179 |
| `bugprone-implicit-widening-of-multiplication-result` | 58 |
| `readability-math-missing-parentheses` | 56 |
| everything else (incl. 49 `clang-analyzer-*`) | ~256 |

**It is code drift, not toolchain drift.** I tested this rather than assuming
it, because the obvious hypothesis was the LLVM 20 → 22 gap (CI pins
`clang-format-20`; local Homebrew is 22.1.8, installed 2026-07-03).

Same tool (clang-tidy 22), same `.clang-tidy` (byte-identical to the version at
`3908624f`, the commit whose message claims "zero lint errors"), two versions of
`src/mcp/mcp.c`:

```
HISTORICAL mcp.c (from the zero-lint-errors commit):   0 magic-number hits
CURRENT    mcp.c:                                    261 magic-number hits
```

The zero was verified not to be a silent failure — appending
`int cbm_probe_magic(void) { return 123456; }` to the historical file made
clang-tidy report it immediately, so the file was genuinely being analysed.

The file grew 3,430 → 11,301 lines. The largest contributors
(`src/cli/config_json_like.c` 562, `src/daemon/ipc.c` 432) were added
2026-07-12 and 2026-07-16 — after the last green tidy run. `lint-tidy` is
excluded from CI (`_lint.yml` runs `--ci`, i.e. cppcheck + clang-format only)
and is described in `Makefile.cbm` as "enforced locally via pre-commit", so
months of new code landed without this leg ever gating it.

**Why I stopped rather than pushing through.** Clearing it is not cosmetic
tidying:

- 4,034 magic-number hits with `IgnoredIntegerValues: ""` means literal `0` and
  `1` are flagged. Naming 1,830 occurrences of `1` is a large, low-value,
  high-churn diff across 96 files — and it would collide with the concurrent
  agent's files.
- 275 cognitive-complexity and 13 function-size hits require **splitting
  functions** — a behaviour-risking refactor, and the exact opposite of "a
  reviewer can see at a glance that nothing behavioural changed".
- 49 `clang-analyzer-*` hits (including `unix.Malloc` leaks and
  `core.NullDereference`) may be **real defects**. Each needs individual
  adjudication and, where real, a red-first proof. That is a defect-hunting
  wave, not a lint cleanup.

There is also an owner-level decision embedded here that is not mine to make:
whether to keep `lint-tidy` in `scripts/lint.sh` at all given that CI never runs
it and it has been red for months. The options — (a) burn down the backlog,
(b) relax `.clang-tidy` (e.g. restore clang-tidy's default
`IgnoredIntegerValues: 1;2;3;4`, which alone removes ~2,300 of the 4,034 hits),
(c) split `lint-tidy` into a separate non-gating target matching CI — differ in
cost by orders of magnitude and change what the gate *means*.

## Verification performed

- `make -f Makefile.cbm lint-format` → **exit 0**
- `make -f Makefile.cbm lint-cppcheck` → **exit 0**
- `make -f Makefile.cbm lint-no-suppress` → **exit 0**
- `bash scripts/lint.sh --ci` → **exit 0** (this is what CI gates on)
- `bash scripts/lint.sh` → **exit 2** (clang-tidy only; see above)
- `scripts/test.sh --suites "store_arch store_nodes store_edges store_search
  store_bulk store_pragmas store_checkpoint registry pipeline parallel httpd
  extraction"` → **860 passed, 0 failed, 1 skipped** (skip is
  `SKIP_PLATFORM`, Windows-only #798)

Files touched: `src/pipeline/{pass_githistory,pass_parallel,pass_pkgmap,pipeline,registry}.c`,
`src/foundation/recursion_whitelist.h`, `src/store/store.c`, `src/ui/httpd.c`.
`src/cypher/cypher.c` and `tests/test_cypher.c` were not touched; neither had
any finding on the three legs I cleared.
