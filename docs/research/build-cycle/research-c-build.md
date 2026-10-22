STATUS: DONE
COMMIT: none
CHANGED: 0 files
OPEN: 0
BLOCKERS: none
---

# C build and test wall time research

Sources were checked on 2026-10-21. `MEASURED` denotes a reported timing with its stated setup; `ASSERTED` denotes documentation, a mechanism, or an unquantified performance statement. The raw source copies are in `research/raw/`.

## Compiler caches for C: kache and ccache

- **ASSERTED** — Kache caches eligible single-source GCC/Clang C/C++ object compilations and preprocessing, keyed by source and header content, compiler identity, modeled code-generation flags, and normalized paths; links normally pass through. Source: https://kunobi.ninja/docs/kache/getting-started/c-cpp
- **ASSERTED** — Kache uses a caller's `-MD` depfile for first-compile input discovery when possible; `-MMD`, remote/fallback/read-only stores, and some out-of-tree layouts use a preprocessor-first path. A warm input memo can avoid a compiler invocation. Source: https://kunobi.ninja/docs/kache/getting-started/c-cpp
- **ASSERTED** — Kache adds `-ffile-prefix-map` rules for GCC/Clang to normalize checkout and build paths, and `KACHE_BASE_DIR` can cover another root; disabling path normalization makes absolute paths part of the key. Source: https://kunobi.ninja/docs/kache/getting-started/c-cpp
- **ASSERTED** — ccache caches object compilations; its faster direct mode validates included files from a manifest, while preprocessor mode is a fallback on misses. Its optional depend mode requires `-MD` or `-MMD` and uses compiler depfiles instead of preprocessing on misses. Source: https://github.com/ccache/ccache/blob/master/doc/manual.adoc
- **ASSERTED** — In ccache depend mode, `-MD` includes system headers in the manifest and can slightly slow hits; `-MMD` excludes them and therefore ignores their changes. Source: https://github.com/ccache/ccache/blob/master/doc/manual.adoc
- **ASSERTED** — `CCACHE_BASEDIR` rewrites absolute paths beneath a common parent to relative paths before hashing, allowing cross-worktree hits when rewritten command lines match. With debug info, ccache normally hashes the working directory unless `-fdebug-prefix-map` or `-fdebug-compilation-dir` normalizes it. The manual cautions that rewritten paths in depfiles can affect Make/Ninja dependency handling. Source: https://github.com/ccache/ccache/blob/master/doc/manual.adoc
- **ASSERTED** — `-ffile-prefix-map` covers debug and macro path mapping for Kache's portability model; `-fdebug-prefix-map` addresses debug paths for ccache, while `CCACHE_BASEDIR` addresses command-line and source paths. Since compiler options enter ccache's hash and Kache models code-generation options, different sanitizer compile flags ordinarily produce separate object-cache entries. Sources: https://kunobi.ninja/docs/kache/getting-started/c-cpp ; https://github.com/ccache/ccache/blob/master/doc/manual.adoc

## Large generated parser sources

- **ASSERTED** — Tree-sitter's generated C files can be compiled into a static or shared library. A prebuilt grammar archive can therefore avoid recompiling unchanged parser sources for each consumer link, subject to matching compiler, ABI, and compile flavour. Source: https://tree-sitter.github.io/tree-sitter/cli/generate.html
- **ASSERTED** — Unity builds batch C sources into fewer translation units to reduce compilation overhead; CMake documents faster compilation but also source-definition collisions and per-file-flag exclusions. For large parser tables, the effect is workload dependent because a larger unit may use more memory and reduce parallel scheduling. Source: https://cmake.org/cmake/help/latest/prop_tgt/UNITY_BUILD.html
- **ASSERTED** — Compiling a grammar once per identical flag/ABI flavour and archiving its object makes the object reusable by multiple final binaries; compiling it with a different `-O` level changes compiler work and generated code, so separate artifacts are needed. This is an inference from Tree-sitter's static-library output and compiler-option-sensitive cache keys. Sources: https://tree-sitter.github.io/tree-sitter/cli/generate.html ; https://github.com/ccache/ccache/blob/master/doc/manual.adoc
- **ASSERTED** — No quantified, source-backed wall-time effect was found for Tree-sitter parser unity builds, prebuilt archives, or per-file `-O` changes within this search limit. Sources consulted: https://tree-sitter.github.io/tree-sitter/cli/generate.html ; https://cmake.org/cmake/help/latest/prop_tgt/UNITY_BUILD.html

## Sharing or avoiding rebuilds across sanitizer flavours

- **ASSERTED** — Clang requires `-fsanitize=` at both compile and link time, and ASan and TSan cannot be combined in one program. Instrumented ASan, instrumented TSan, and plain objects are therefore different build products; a cached object can be shared across worktrees only within the same compatible flavour and flags. Sources: https://clang.llvm.org/docs/UsersManual.html ; https://github.com/ccache/ccache/blob/master/doc/manual.adoc
- **ASSERTED** — ASan/UBSan can be one combined flavour while TSan needs its own runner; placing outputs in flavour-specific directories avoids timestamp reuse of an object built with the wrong instrumentation. The separation follows Clang's incompatible sanitizer combinations and distinct link runtimes. Source: https://clang.llvm.org/docs/UsersManual.html
- **ASSERTED** — Kache coordinates concurrent identical cache misses across builds, so worktrees asking for the same eligible object key can share one real compilation; distinct parser files or different sanitizer keys still compile independently. Source: https://kunobi.ninja/docs/kache/how-it-works/limits

## GNU make parallelism

- **ASSERTED** — GNU make runs recipes serially by default; `-j N` permits at most N jobs, while `-l L` stops starting another job when load average reaches L and another job is running. Source: https://www.gnu.org/software/make/manual/html_node/Parallel.html
- **ASSERTED** — GNU make's jobserver passes its token endpoint in `MAKEFLAGS` as `--jobserver-auth`; recursive make and cooperating tools can draw from the same pool instead of each creating an independent `-j N` pool. Source: https://www.gnu.org/software/make/manual/html_node/Job-Slots.html
- **ASSERTED** — Supplying a core-derived job count, for example `make -j"$(nproc)"` on Linux, makes the cap follow the available processing units at invocation time; the actual best count remains constrained by memory, compiler RSS, and link load. GNU make documents the cap and load behavior, not a universal optimal count. Source: https://www.gnu.org/software/make/manual/html_node/Parallel.html

## Incremental clang-tidy and cppcheck

- **ASSERTED** — `run-clang-tidy.py` accepts source/file filters and `-j` for parallel translation-unit analysis; `clang-tidy-diff.py` reports diagnostics on changed lines but still analyzes each whole selected file, so changed-line filtering by itself does not reduce analysis cost. Source: https://clang.llvm.org/extra/clang-tidy/
- **ASSERTED** — `clang-tidy-cache` is a third-party wrapper that hashes the relevant translation unit and arguments and reuses successful clang-tidy results on a hit; its README documents local and server storage. Source: https://github.com/matus-chochlik/ctcache
- **ASSERTED** — Cppcheck's `--cppcheck-build-dir=path` stores analyzer data and reuses results for unchanged files on repeat runs; the manual also says this enables whole-program analysis with multiple threads. Source: https://github.com/danmar/cppcheck/blob/main/man/manual.md
- **ASSERTED** — No quantified timing for either analyzer's incremental mode was found in the cited tool documentation. Sources: https://clang.llvm.org/extra/clang-tidy/ ; https://github.com/danmar/cppcheck/blob/main/man/manual.md

## Linking large C binaries faster

- **MEASURED** — On an AMD Threadripper 7980X with Ubuntu 24.04, release-built linkers at 2026-08-28, default settings, and median of three warm-up-following wall-time runs, mold linked a 9.55 GiB debug TensorFlow 2.21 output in **3.15 s** versus **50.73 s** for lld (**16.1×**); this is a link step, not a whole build or test run. Source: https://github.com/rui314/mold
- **MEASURED** — In that same setup, wild linked the 4.51 GiB debug Chromium 145 output in **3.98 s** versus **16.64 s** for lld (**4.18×**, calculated from reported times); mold was **1.65 s**. Wild was not reported for every target. Source: https://github.com/rui314/mold
- **ASSERTED** — mold documents `-fuse-ld=mold` for Clang and GCC 12.1+; the alternative linker changes only the link stage, so a build dominated by hundreds of parser compiles can realize much less end-to-end improvement than linker-only ratios. Source: https://github.com/rui314/mold

## Largest reported effects

| technique | reported effect | source | applies-to |
| --- | --- | --- | --- |
| mold vs lld | 16.1× link-step speedup: 50.73 s → 3.15 s, TensorFlow 2.21 debug, 9.55 GiB output, Threadripper 7980X, Ubuntu 24.04, median of 3 warm-up-following runs | https://github.com/rui314/mold | edit loop; clean build; gate run |
| wild vs lld | 4.18× link-step speedup: 16.64 s → 3.98 s, Chromium 145 debug, 4.51 GiB output, same setup | https://github.com/rui314/mold | edit loop; clean build; gate run |

No defensible numeric effect was found within the source limit for compiler caching, parser compilation strategies, make parallelism, or incremental analyzer caching in a comparable C build-and-test workload.
