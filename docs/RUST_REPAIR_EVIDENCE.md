# Rust repair verification

This file records the current repair's contract, its measurement against
rust-analyzer, and the durable regression oracles. Historical runs, discarded
candidates and compiler transcripts are retired to Git. The remaining objective
is in [RUST_CODEGRAPH_TRUTH_PLAN.md](RUST_CODEGRAPH_TRUTH_PLAN.md).

## Measurement lane

`scripts/scip-site-join.py` scores every parsed Rust call site of an index
against a rust-analyzer SCIP index of the same corpus. Indexing with
`CBM_RUST_RESOLVE_DUMP=<file>` writes one `R` row per resolver record and one
`C` row per call site (joined resolution, strategy, admission verdict); the
script locates the callee identifier inside each call span, takes the SCIP
occurrence at that position as truth, and compares the resolver's target with
the truth definition by file, leaf, owner type and trait. Precision is reported
per strategy on internal (workspace-function) truths; misses are broken down by
the resolver's own unresolved reason.

```sh
rsync -a --exclude target --exclude .git <workspace>/ /tmp/corpus/
rust-analyzer scip <workspace> --output /tmp/index.scip
CBM_CACHE_DIR=/tmp/cache CBM_RUNTIME_DIR=$(mktemp -d /tmp/rt.XXXXXX) \
  CBM_RUST_RESOLVE_DUMP=/tmp/dump.tsv \
  build/c/codebase-memory-mcp cli index_repository --repo-path /tmp/corpus --mode full --name c
scripts/scip-site-join.py --corpus /tmp/corpus --dump /tmp/dump.tsv \
  --db /tmp/cache/c.db --scip /tmp/index.scip --out /tmp/join
```

Definitions are checked separately: 99.56% of rust-analyzer's function and
method definitions in codex-rs have a graph node at the same file and lines; the
remainder are macro-generated items and `extern` declarations.

## codex-rs corpus, epoch 12 to 13 (rust-analyzer 1.98.1, 2026-09-25)

| Measure | Epoch 12 | Epoch 13 |
|---|---|---|
| CALLS edges | 22,020 | 73,707 |
| Call sites admitted | 27,142 | 170k |
| Site-level recall, internal function calls | 23.8% | 43.6% |
| Calls attributed to the File node | 631 | 22 |
| Lowest admitted-strategy precision | 94.0% (`lsp_direct`) | 98.0% (`lsp_trait_dispatch`) |

Per-strategy site precision at epoch 13 on internal truths: `lsp_direct`
99.90%, `lsp_method_dispatch` 99.88%, `lsp_constructor` 100%, `lsp_ufcs`
99.80%, `lsp_import_alias` 99.38%, `lsp_glob_import` 99.74%,
`lsp_deref_dispatch` 99.34%, `lsp_trait_dispatch` 98.05%, `lsp_cross_crate`
100%. Closed: `lsp_trait_ufcs` 66.67% (`Default::default()` inference),
`lsp_callable_alias` 0% (closure values), `lsp_prelude_trait` and `lsp_macro`
(no graph target).

Root causes repaired, each found as the largest miss class of the lane:

1. The admission gate accepted only file-local free-function callers and four
   strategies; 96% of sites were closed, including 26k measured-correct
   resolutions.
2. `crate::` mapped to the first two QN segments, omitting the source root, so
   every `use crate::…` path missed the registry; `super::` from `mod.rs` and
   workspace crate names (`codex_core::x`, dir `core`) had the same shape defect,
   and the manifest parser dropped members past 64.
3. Any glob import closed every bare call in the file, including same-module
   items that shadow globs by language rule.
4. Calls inside `impl Trait for Type` methods and `#[cfg]` twins carried an
   enclosing name without the published `[Trait]` / `#cfg(..)]` suffix, so they
   attached to the File node and no resolver row could join.
5. Struct fields never reached the shared registry, and field or return types
   spelled in the defining module could not be recovered, so
   `self.field.method()` never typed its receiver.
6. `pub use` re-exports and a parent's imports seen through `super::*` were not
   followed; Rust IMPORTS edges (generic name matcher) were unusable for this,
   so the pipeline now keeps a table of raw Rust import paths per file.
7. `let x = x(..)` bound the pattern before resolving the initializer.

## Current gate

`make scip-rust` on the committed fixture: `calls_graph=7 matched=5
graph_only_referenced=2 fabricated=0 oracle_strict=5 oracle_only=0 imports=3`;
the second attested graph-only edge is the trait-impl method body
(`Loud::greet` to `shout`) that the epoch-12 gate refused. The semantic-epoch
contract is 13. Focused suites (`rust_lsp pipeline parallel
repro_lexical_binding_precision`) and the full `scripts/test.sh` leg are
recorded in the commit that lands this file.

## Regression contracts

| Test | Observable contract |
|---|---|
| pipeline_rust_module_paths_resolve_exact_calls_in_both_modes | One crate laid out as Cargo does: field-typed and let-bound receivers, a trait-impl caller, `Arc<dyn Trait>`, a `crate::` re-export, a glob, a `#[cfg]` twin caller and `super::` from `mod.rs` all publish the exact edge in both pipelines; `Default::default()` publishes none. |
| pipeline_rust_trait_impl_call_admission_is_exact_in_both_modes | Keyed trait-impl dispatch admitted; two same-named impls stay closed. |
| rust_use_trees_preserve_exact_paths_and_bindings | Exact local-name/module-path pairs for nested aliases, grouped self, globs and trivia. |
| rustlsp_nested_use_aliases_resolve_exact_targets | Exact String/Vec constructor identities and caller in local and cross resolvers. |
| extract_rust_test_attributes_match_paths_not_documentation | Each definition appears once with its expected attribute classification. |
| rustlsp_collection_imports_preserve_library_owner | Std HashMap/HashSet and alloc BTreeMap owners are kept. |
| rustlsp_missing_path_does_not_bind_same_named_local_function | An unreachable qualified path stays unresolved while the local invocation resolves. |
| scip harness (`tests/fixtures/scip_harness`) | fabricated=0 unconditionally; matched, graph-only-referenced, oracle-only and imports pinned. |

## Reproduction

Run the repository's canonical gate with isolated runtime/cache state:

```sh
set -euo pipefail
source scripts/test-runtime.sh
cbm_test_runtime_init
trap 'cbm_test_runtime_cleanup "$PWD/build/c/codebase-memory-mcp"' EXIT
CCACHE_DISABLE=1 CBM_NO_CCACHE=1 ASAN_OPTIONS=detect_leaks=0 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 scripts/test.sh
make -j3 -f Makefile.cbm lint-ci lint-tidy-diff
scripts/security-audit.sh
```

For focused iteration, use scripts/test.sh --suites with
rust_lsp,pipeline,parallel,repro_lexical_binding_precision; it does not replace
the complete gate.
