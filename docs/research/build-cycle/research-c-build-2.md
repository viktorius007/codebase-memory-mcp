STATUS: DONE
COMMIT: none
CHANGED: 0 files
OPEN: 2
BLOCKERS: No measured wall-time effect for excluding vendored sources from sanitizer instrumentation; no mold/lld report found for a C test executable of a few hundred MB.
---

# Measured build and test wall-time evidence, second round

Checked 2026-10-21. Fifteen new sources were checked beyond `research-c-build.md`; one scraped page returned 404 and is excluded from findings. **MEASURED** means the cited source reports a number or an observed comparison; **ASSERTED** means it describes behavior without an applicable timing. Timings are for the stated operation, not necessarily an end-to-end build.

## Many Tree-sitter grammars: artifacts, optimization, and parallel work

- **ASSERTED** — tree-sitter-language-pack downloads per-language native parser libraries into a versioned cache and can load pre-staged `libtree_sitter_*.so`/`.dylib`/`.dll` files directly. This is a prebuilt shared-library model; the source gives no build-time comparison. Source: https://github.com/xberg-io/tree-sitter-language-pack/blob/main/docs-site/src/content/docs/guides/configuration.mdx
- **MEASURED** — Helix 22.03 moved more than 50 grammars out of git submodules; it shallow-clones 60 grammar repositories in parallel in as little as **6 s on a good connection**. The 6 s is for **fetching**, not compiling; its separate `hx --grammar build` compiles them. Source: https://helix-editor.com/news/release-22-03-highlights/
- **MEASURED** — Tree-sitter's generated `#pragma GCC optimize ("O0", "jump-tables")` for large lexers avoids costly optimization inside the lexer despite an outer `-O2` build. On **117 nvim-treesitter parsers** with GCC 16, retaining the pragma compiled them in **309 s** versus **1,572 s** without it (**5.09×**, calculated). With Clang 22, **77 finished** in **205 s** with versus **4,211 s** without (**20.5×**, calculated); other grammars did not finish. This also trades against parse throughput, reported up to about 15% faster without the pragma. Source: https://github.com/tree-sitter/tree-sitter/issues/6044
- **MEASURED** — In the same Tree-sitter report, GCC 16.2.1 at outer `-O2` compiled C grammar `parser.c` in **0.4 s with the lexer O0 pragma** versus **1.1 s without**; C# took **2.1 s versus 3.1 s**. Tests used x86-64 and compared parse throughput separately. Source: https://github.com/tree-sitter/tree-sitter/issues/6044
- **MEASURED** — A proposed generated-table representation cut ABL parser compilation on Fedora AArch64 / Clang 21.1.8 from **8.48 s to 4.75 s**, peak compiler memory **2,743 to 924 MiB**, for a parser source initially **121.16 MiB**. Lean fell **1.60 to 0.31 s**. This is an experimental generator change, not a reported shipped build policy. Source: https://github.com/tree-sitter/tree-sitter/issues/5974

## Reuse of sanitizer-instrumented vendored objects and caches

- **ASSERTED** — Halide documents cross-build ccache use with normalized debug paths and precompiled-header settings. Source: https://github.com/halide/Halide/blob/main/doc/BuildingHalideWithCMake.md
- **MEASURED** — On an M3 MacBook Pro, Halide’s second package build was **3× faster with 100% cache hits**. The reported run was not a sanitizer build, so this does not establish a sanitizer-specific speedup. Source: https://github.com/halide/Halide/blob/main/doc/BuildingHalideWithCMake.md
- **MEASURED** — WarpX split Clang TSan and UBSan CI jobs after diagnosing low ccache hits; on a rerun after an uncached first run, its three TSan jobs took **29, 46, 46 s** and three UBSan jobs **32, 45, 40 s**. The source says cache reuse was working but does not report the cold durations or hit percentages, so no speedup can be calculated. Source: https://github.com/BLAST-WarpX/warpx/pull/6940
- **ASSERTED** — WarpX's distinct sanitizer jobs and Halide's object-cache setup demonstrate flavour-specific cached build products; neither source reports prebuilt ASan/UBSan/TSan vendor archives reused by multiple test runners, or a sanitizer-specific ccache hit rate. Sources: https://github.com/BLAST-WarpX/warpx/pull/6940 ; https://github.com/halide/Halide/blob/main/doc/BuildingHalideWithCMake.md

## Whether comparable projects instrument third-party sources

- **ASSERTED** — OpenROAD's Bazel sanitizer configuration explicitly exempts `external/` source files with `--per_file_copt=.*external/.*@-fno-sanitize=all`; third-party headers inlined into OpenROAD translation units remain instrumented, and would need an ignorelist to exempt. Source: https://github.com/the-openroad-project/OpenROAD/blob/master/docs/user/Bazel.md
- **MEASURED** — In OpenROAD's UBSan sweep, **36 of 3,980 tests failed**, including **24** attributed to third-party UB reached through inlined Boost and Lemon headers even after external source files were exempted. This measures the diagnostic coverage trade-off, not compilation or test wall time. Source: https://github.com/the-openroad-project/OpenROAD/blob/master/docs/user/Bazel.md
- **ASSERTED** — Sourcegraph's scip-clang ASan Bazel configuration explicitly disables a sanitizer ignorelist because the ignored files would otherwise be undeclared compile inputs. No build-time number is reported. Source: https://github.com/sourcegraph/scip-clang/blob/main/.bazelrc

## GNU make incrementality defeats and fixes

- **ASSERTED** — GNU make documents generated per-source `.d` prerequisites from actual included headers, allowing each object to depend on its own include set rather than a manually shared header list. The documentation gives no wall-time comparison. Source: https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html
- **MEASURED** — Colibri's `.build-config` stamp used `$(file ...)`, unavailable in GNU Make 3.81, so an unchanged second `make colibri` linked again. On a real arm64 macOS GNU Make 3.81 ~/.claude/references/build-cycle/host, the merged fix wrote a **276-byte stamp** and the second invocation made **zero links** versus **one redundant link** in the control tree; GNU Make 4.3 was already no-op on the second run. The issue reports behavior, not seconds saved. Source: https://github.com/JustVugg/colibri/issues/1732

## clang-tidy and cppcheck incremental analysis

- **MEASURED** — Autoware's clang-tidy cache wrapper reduced a local cache-hit file from **1.2 s to 0.2 s** (**6×**). Its reported job comparison was **8,398 s before** versus **1,670 s after** (**5.03×**); the issue reports this as a workflow comparison, so its full change should not be attributed solely to individual cache hits without additional control. Source: https://github.com/autowarefoundation/autoware-github-actions/issues/402
- **MEASURED** — On sakura-editor-next's `sakura_core/sakura.vcxproj`, cppcheck 2.21.0 with the same x64 Release parameters, a no-build-dir run took **609 s**, a cold `--cppcheck-build-dir` run **599 s**, and a warm repeat **18 s** (**33.8×** relative to no dir). Same machine, source tree, and warm OS file cache. The cached run emitted **7,558** findings, versus **6,280** without the build dir because it re-enabled whole-program checks otherwise disabled under `-j`; the speedup is not a same-check-set comparison. Source: https://github.com/tsuyoshi-otake/sakura-editor-next/issues/112

## mold and lld for modest-size test executables

- **MEASURED** — For an **approximately 81–85 MiB stripped, statically linked Rust program** on a four-core laptop, with debug linking stripped and unsupported features excluded, mold 2.40 took **425.8 ± 6.9 ms** versus lld 15 **898.9 ± 8.1 ms** (**2.11×**, calculated). This is smaller than the requested few-hundred-MB C test binary, and is link-only. Source: https://davidlattimore.github.io/posts/2024/03/18/wild-linker-march-update.html
- **MEASURED** — For a **1.6 GB debug, static C++20 executable** built with GCC 12 on a dual Xeon Gold 5218 Ubuntu 20.04 machine, 8-thread mold 1.11 linked in **4.458 s** versus lld 16 **6.069 s** (**1.36×**, calculated), 10 runs after 3 warmups on one NUMA node. The stripped result was **158 MB**, but the timed output retained debug data and was 1.6 GB; it is not a 158 MB link benchmark. Source: https://github.com/rui314/mold/issues/1020
- **ASSERTED** — None of the 15 new sources supplied an apples-to-apples mold/lld timing for a C test executable of a few hundred MB. The closest sizes above bound what can be claimed from this round; no size interpolation is justified. Sources: https://davidlattimore.github.io/posts/2024/03/18/wild-linker-march-update.html ; https://github.com/rui314/mold/issues/1020

## Largest reported effects

| technique | reported effect | source | applies-to |
| --- | --- | --- | --- |
| cppcheck warm build dir | 609 s → 18 s, **33.8×**; check set differs because whole-program checks are enabled | https://github.com/tsuyoshi-otake/sakura-editor-next/issues/112 | gate run |
| retain large-lexer O0 pragma in generated parsers | Clang 22: 4,211 s → 205 s, **20.5×** for 77 completed grammars; GCC 16: 1,572 s → 309 s, **5.09×** for 117 | https://github.com/tree-sitter/tree-sitter/issues/6044 | clean build; gate run |
| clang-tidy cached workflow | 8,398 s → 1,670 s, **5.03×** workflow comparison; local per-file hit **6×** | https://github.com/autowarefoundation/autoware-github-actions/issues/402 | gate run |
| generated parser string-literal table proposal | Lean Clang compile 1.60 s → 0.31 s, **5.16×**; experimental | https://github.com/tree-sitter/tree-sitter/issues/5974 | clean build; gate run |
| Halide ccache second build | **3×** faster, 100% hit rate, M3 MacBook Pro; non-sanitizer setup | https://github.com/halide/Halide/blob/main/doc/BuildingHalideWithCMake.md | edit loop; gate run |
| mold vs lld, 81–85 MiB Rust output | 898.9 ms → 425.8 ms, **2.11×** link-only | https://davidlattimore.github.io/posts/2024/03/18/wild-linker-march-update.html | edit loop; clean build; gate run |
| mold vs lld, 1.6 GB debug C++ output | 6.069 s → 4.458 s, **1.36×** link-only at 8 threads | https://github.com/rui314/mold/issues/1020 | edit loop; clean build; gate run |
