STATUS: BLOCKED
COMMIT: 0695bd0e
CHANGED: 14 files
OPEN: 7
BLOCKERS: scripts/lint.sh and the pre-commit hook are red on base 8917df3f itself (clang-format violations in 4 untouched src files; ~19.6k clang-tidy findings), so they cannot go green without reformatting product sources, which is outside this lane
---

# cbm-build lane: codebase-memory-mcp edit-build-test cycle

Branch `lane/cbm-build`, worktree `/scratch/agent/wt/codebase-memory-mcp-cbm-build`, two commits on base 8917df3f:

- `249c4167` build: incremental, cache-friendly, all-core builds
- `0695bd0e` build: clang-analyzer lint lanes follow the core count

The production binary built by `scripts/build.sh` is byte-identical to base: sha256 `6b3c8afe1a1c8afa4aebf7196a1404fbb8b403bf100b2460543ec1387366844b` for both `/scratch/agent/tmp/cbm-base/build/c/codebase-memory-mcp` (a `git archive 8917df3f` export) and the lane's `build/c/codebase-memory-mcp`. No C source file changed (`git diff --stat 8917df3f HEAD -- src internal tests/*.c` is empty).

## Changes

| File | Before | After | Why |
|---|---|---|---|
| `Makefile.cbm` (parallelism) | No default `-j`; direct `make` ran one job | `MAKEFLAGS += -j$(CBM_JOBS)` (nproc) unless `-j…` or `--jobserver…` is already in MAKEFLAGS | A direct `make -f Makefile.cbm <target>` no longer compiles serially; `-j1` or `CBM_JOBS=` still override (GNU make ≥ 4.3) |
| `Makefile.cbm` (headers) | `$(PROJECT_HDRS)` (every project header) on every per-file object; `LSP_UNITY_DEPS` globbed all `internal/cbm/*.h` | `DEPFLAGS = -MD -MP` on every object compile; `-include` of existing `.d`; an object whose `.d` is missing gets `FORCE` once | A header edit rebuilds only its includers. `-MD` rather than `-MMD`: kache uses a caller's `-MD` file as the read set, `-MMD` forces an extra preprocessor run per miss (kache C/C++ docs, "Cached invocations") |
| `Makefile.cbm` (config stamps) | One `.build-config` (TEST_SEAMS, CFLAGS_EXTRA, SANITIZE, MEMWASTE*, TEST_COVERAGE) on test, TSan and prod objects; vendored test objects had none | `.build-config` (prod), `.build-config-test`, `.build-config-tsan`, each keyed on compiler identity (`__VERSION__`, `__clang__`, target-arch macros from `$(CC) -dM -E`) plus that flavour's variables; vendored test/TSan objects depend on their stamp | test.sh's `cbm TEST_SEAMS=1` no longer invalidates the test runner (measured: 424 prod recompiles, then 0 for `build/c/test-runner`). Replaces the protection `build.sh`'s delete gave against a compiler or SANITIZE change. Identity, not name: `cc` and `gcc` are one compiler, and keying on the name rebuilt all 424 objects when switching between `scripts/build.sh` (CC=gcc) and a direct make (CC=cc) |
| `Makefile.cbm` (`test-foundation`) | One-shot link of foundation sources: hundreds of undefined references (`tests/test_main.c` registers every suite) | Runs `runner_isolation` plus the 15 suites of test_main.c's "Foundation" block through `build/c/test-runner` | Links; shares the runner's objects. No callers existed outside the Makefile |
| `Makefile.cbm` (`cbm-with-ui`) | One compiler process over ~450 sources | Same per-file prod objects with `embedded_assets.o` for `embedded_stub.o`; `src/ui/embedded_assets.c: embed ;`; a `.with-ui` marker makes the next plain `cbm` relink | `scripts/build.sh --with-ui` 82 s; the following plain build relinks to the base-identical binary |
| `Makefile.cbm` (lint) | `lint-tidy`: one clang-tidy process over all sources; `lint-mem`/`lint-mem-ci`: `xargs -P 10`/`-P 4` | One clang-tidy per source, `xargs -P $(CBM_JOBS)`, in all three | Core count; same files, flags and checks; header findings print once per includer |
| `Makefile.cbm` | `clean-c` only | `clean` alias | Conventional name for the explicit clean |
| `scripts/build.sh` | `cbm_remove_build_dir` before every build | Incremental; help text says how to get a clean build | Keeps objects and cppcheck's cache; CI checkouts have no `build/` |
| `scripts/test.sh` | Default leg ran `scripts/clean.sh` (build dir, `graph-ui/node_modules`, `dist`, fixtures) | `scripts/clean.sh --fixtures-only` | Same reason; leftover fixture dirs are still removed |
| `scripts/clean.sh` | No options | `--fixtures-only` | Used by test.sh |
| `scripts/lint.sh` | `make -j2` / `make -j3` | `make -j"$NPROC"` | Core count |
| `scripts/hooks/pre-commit` | `make -j3 lint-ci lint-tidy-diff`; `make -j$(sysctl…‖nproc) test` | `NPROC` once; `make -j"$NPROC" lint-ci lint-tidy-diff`; `make -j"$NPROC" test-par` | Core count; `test-par` runs the same suites (harness checks against `--list-suites`) one process per suite instead of one process for all |
| `tools/rust-bench/gate.sh` | `-j8` | `-j"$(getconf _NPROCESSORS_ONLN)"` | Core count |
| `test-infrastructure/Dockerfile.mingw`, `docker-compose.yml` | `-j4` | `-j$(nproc)` / `-j$$(nproc)` | Core count |
| `scripts/extract_nomic_vectors.py`, `private/rust-run/merge/COMMON.md`, `~/.claude/references/build-cycle/README.md` | `-j12`, `-j8`, "clean sanitizer build" | `-j"$(sysctl -n hw.ncpu)"`, `-j"$(nproc)"`, "incremental sanitizer build" | Documented commands |
| `tests/repro/repro_make_tracks_headers.sh` | Grepped `make -pn` for headers on link-target lines | Builds one object per flavour in a scratch BUILD_DIR, asserts `make -q` says up to date, out of date after `-W` on an included header, up to date after `-W` on an unrelated header | Checks the depfile behaviour; red with `MAKEFLAGS='DEPFLAGS='` (verified) |
| `tests/test_makefile_ts_runtime_dependencies.sh` | `make -q -W` with the phony stamp check, so prod targets always reported "out of date" | Holds the stamps old (`-o`), touches an empty `.d`, and first asserts each target is up to date | The probe can now fail for all three targets |

Not changed: `flake.nix` (`$NIX_BUILD_CORES` already follows the builder), the VM scripts (`JOBS='$(nproc)'`), historical records under `private/rust-run/merge/reports/` and `handover/`.

## Timing (16 cores, gcc 15.2, kache 1.2.2; my runs)

Other lanes shared the ~/.claude/references/build-cycle/host. One base run at 12:00 UTC was discarded at load average 3048 (an I/O spike from another session deleting cargo targets); all figures below were taken at load below 17.

| Measure | Before (8917df3f export) | After (0695bd0e) |
|---|---|---|
| Clean `scripts/build.sh`, first run in a new tree | 190 s | 139 s |
| Clean `scripts/build.sh`, warm | 108 s (137 s earlier) | 78 s (75 s rerun) |
| `scripts/build.sh`, nothing changed | full clean rebuild (~108 s) | 0.36 s, 0 compiles |
| One-file edit (`src/foundation/str_util.c`), `make -f Makefile.cbm cbm` | 2.2 s, 1 compile (`-j16` given) | 1.5 s, 1 compile (no `-j`) |
| One-file edit, `scripts/build.sh` | full clean rebuild (~108 s) | 2.8 s |
| One-header edit (`src/foundation/str_util.h`), `make cbm` | 45.2 s, 254 compiles | 16.3 s, 30 compiles |
| `make -f Makefile.cbm test`, build part, empty build dir, no `-j` | ~27 min (12:15–12:42), serial, 617 compiles | 6 min 32 s first; 5 min 1 s second |
| `make test`, suite run (one process) | > 23 min, ended with the session (6,751 PASS lines) | > 20 min, stopped at the 1200 s cap (7,990 PASS lines) |
| `make test-par` (193 suites) | not measured | 5 min 27 s |
| TSan runner build / `scripts/test.sh --tsan` | not measured | 1 min 25 s / 7 min 52 s |
| `make lint-tidy` | 72 s | 15 s |
| `make lint-cppcheck` | deleted with build/c by build.sh and test.sh | ~5 min cold, 1.4 s warm |

## What remains uncached, and why

kache reasons are from `kache stats --full` after each build; `lsp_all.c` from `KACHE_LOG=kache=debug`.

1. **Every ASan/UBSan compile** (our test objects and vendored test objects alike): "unsupported flag(s): -fsanitize=address,undefined". The ASan runner is never cached; this is the second test-runner build's 5 min. TSan and prod compiles are cached. Host-side fix: `KACHE_CC_EXTRA_ALLOWLIST_FLAGS='-fsanitize=address,undefined'` (or `[cc] extra_allowlist_flags` in `~/.config/kache/config.toml`). Verified: with it the second compile of an ASan object is a local hit. A repo `.kache.toml` is the wrong place: kache uses the first config file it finds, so it would replace the user's `local_store` setting. With the allow-list a warm test-runner build still took 5 min 41 s, because of item 2.
2. **`internal/cbm/lsp_all.c`, all flavours** (~2 min prod, ~5 min ASan; the critical path of every clean build), for two separate false positives:
   - `internal/cbm/lsp/rust_lsp.c:16226` holds the literal `"core.macros.include"`. kache's raw-text scan (`cc_raw_assembler_hidden_input`, kache `src/compiler/cc.rs:6278`) reads a `.include` followed by a quote as an assembler include. Splitting it as `"core.macros." "include"` yields the same string and clears this check (verified, then reverted).
   - `vendored/yyjson/yyjson.h:231-236` defines `yyjson_has_include(x)` as `__has_include(x)`. kache refuses an alias of the probe operator.
   Both must go for the unit to cache. The yyjson half needs a vendored-source patch plus a `scripts/vendored-checksums.txt` update, so I left both for a follow-up.
3. **The 43 TUs that include `yyjson.h`, plus `yyjson.c`** (mcp.c 27 s, cli.c, layout3d.c, …): the same yyjson alias.
4. **`internal/cbm/preprocessor.cpp`**: simplecpp implements `__has_include` itself, and kache reads its source as a macro-named probe.
5. **`build/c/unixcoder_blob.o`**: `.incbin` of `vendored/nomic/code_vectors.bin`. Uncacheable by design.
6. **Links**: passthrough (`cache_cc_links` is off on this ~/.claude/references/build-cycle/host).
7. **`COVERAGE=1` / `TEST_COVERAGE=1` lanes**: kache passes coverage builds through. `diag`, `test-lsan` and the `test-runner-cov` lanes call compilers by absolute Homebrew/LLVM path, which bypasses the shims (macOS lanes).

Tree-sitter O0 pragmas: 64 of 163 vendored `parser.c` files carry `#pragma GCC optimize ("O0")` / `#pragma clang optimize off`. No flag or build step removes or overrides them. Measured with `/usr/bin/gcc -O2` (no cache): agda `parser.c` 1.42 s with the pragma, 3.70 s without it.

## Judgement calls

- `-MD` over the brief's `-MMD`: the depfiles also list system headers, so a compiler upgrade rebuilds; kache keys directly from the file.
- A missing depfile forces one rebuild of that object, so a `build/c` from before this change cannot keep stale header dependencies.
- The pre-commit hook runs `test-par`, not `test`. Same suites and sanitizer build; the single-process run exceeds 20 min here.
- `make test` stays single-process, because `CBM_TEST_SEQUENTIAL=1` in test.sh relies on that. It now builds on all cores.
- The kache allow-list is a ~/.claude/references/build-cycle/host setting and is not committed.
- Lint tools are not on this ~/.claude/references/build-cycle/host. I installed user-local copies under `/scratch/agent/tools/lint/bin` (no sudo): clang-format 20.1.8 (Ubuntu deb, the version `_lint.yml` pins), clang-tidy 21.1.8 (deb), and cppcheck 2.20.0 built from the upstream tag, as `_lint.yml` does. Ubuntu's cppcheck 2.19 fails with `internalAstError` on `(TSNode){0}`. Prepend that directory to PATH to reproduce the lint runs.

## Open items

1. `lint-format` fails on base 8917df3f: 19 violations in `src/cli/cli.c`, `src/daemon/frontend.c`, `src/daemon/runtime.c`, `src/daemon/version_cohort.c`. Both clang-format 20.1.8 and 21.1.8 report them on the base export. This blocks `scripts/lint.sh`, `lint.sh --ci` and the pre-commit hook.
2. Full `lint-tidy`: 19,634 findings at base (21,687 in the per-file run). `scripts/lint.sh` without `--ci` cannot pass on this tree.
3. kache ASan allow-list (uncached item 1): a ~/.claude/references/build-cycle/host config decision.
4. `lsp_all.c`, yyjson includers and `preprocessor.cpp` uncacheable (items 2–4): needs a yyjson patch plus the rust_lsp literal split, or a kache fix.
5. Pre-existing suite failures on this ~/.claude/references/build-cycle/host, identical with the base runner (`run-tests-parallel.sh` over the same 12 suites: same suites, same counts). `cli` has 10 failures: `install_dir_group_or_world_writable`, because umask 002 leaves the worktree and `build/c` at mode 0775. Eleven suites (`parallel`, `pipeline`, `pipeline_expression`, `artifact`, `edge_imports`, `edge_structural`, `lsp_resolution_probe`, `doc_mentions_rs`, `pipeline_semantic_manifest_repro`, `pipeline_unresolved`, `repro_reference_precision`) pass every test but exit 1 on LeakSanitizer leaks from `internal/cbm/cbm.c:922` (`field_cache`).
6. The single-process `make test` suite run exceeds the 1200 s cap both before and after; the run part is unchanged by this lane.
7. `scripts/test.sh --contracts-only` stops at Step 0o: `zip` is not installed. Steps 0a–0n pass. The later steps that read build scripts pass when run directly (line endings, select-lanes, lane wiring, setup scripts, runtime isolation, exec bit).

## Commands run, with results

Green:
- `timeout 1200 scripts/build.sh` (clean, warm, no-op, one-file) — rc 0
- `timeout 1200 scripts/build.sh --with-ui`, then `scripts/build.sh` — rc 0, base-identical hash after the plain build
- `timeout 1200 make -f Makefile.cbm build/c/test-runner` (no `-j`) — rc 0
- `timeout 1200 scripts/test.sh --suites arena,hash_table,str_util,store_nodes,cypher,daemon_ipc` — 490 passed, 0 failed, 1 skipped
- `timeout 1200 make -f Makefile.cbm test-foundation` — 387 passed, 3 skipped
- `timeout 1200 make -f Makefile.cbm build/c/test-runner-tsan`, then `timeout 1200 scripts/test.sh --tsan` — 1606 passed, 8 skipped
- `bash tests/test_makefile_ts_runtime_dependencies.sh` — PASS
- `bash tests/repro/repro_make_tracks_headers.sh` — pass; red with `MAKEFLAGS='DEPFLAGS='`
- `make -f Makefile.cbm lint-cppcheck` (cppcheck 2.20.0) — rc 0
- `scripts/security-audit.sh` — passed
- `make cbm TEST_SEAMS=1` then `make build/c/test-runner` — 0 test recompiles

Red, for pre-existing causes only (open items 1, 2, 5, 6):
- `scripts/lint.sh --ci` — every step passes except `lint-format`
- `scripts/lint.sh` — `lint-tidy` and `lint-format`
- `git -c core.hooksPath=scripts/hooks hook run pre-commit` — stops at `lint-format`
- `make test-par` under umask 022 — 10316 passed, 10 failed, 10 skipped
- `make test` — 1200 s cap
