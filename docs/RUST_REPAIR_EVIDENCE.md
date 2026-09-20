# Rust repair verification

This file records the current repair's contract and durable regression oracles.
Historical runs, discarded candidates and compiler transcripts are retired to Git.
The remaining objective is in [RUST_CODEGRAPH_TRUTH_PLAN.md](RUST_CODEGRAPH_TRUTH_PLAN.md).

## Current gate

The source/test tree committed at `61fde702` passed the canonical scripts/test.sh
gate on macOS arm64:
**8,074 passed, 0 failed, 10 skipped**, across 141 suites under ASan/UBSan.
All subsequent production-runtime and security-string checks passed; the command
exited 0.

Repository lint-ci, diff-scoped clang-tidy, the security audit and whitespace checks
passed on the final source. No lint suppression was added. Independent source
reviews found no blocking issue in either the cleanup or its Rust integration.

No mutation sweep, installed-binary validation, full-corpus incremental parity check
or comparable warm-update performance measurement has been completed.

## Regression contracts

| Test | Observable contract |
|---|---|
| rust_use_trees_preserve_exact_paths_and_bindings | Exact local-name/module-path pairs for nested aliases, grouped self, globs and trivia. |
| rustlsp_nested_use_aliases_resolve_exact_targets | Exact String/Vec constructor identities and caller in local and cross resolvers. |
| extract_rust_test_attributes_match_paths_not_documentation | Each definition appears once with its expected attribute classification; documentation-only helpers remain non-tests. |
| testdetect_explicit_test_marker_does_not_require_name_prefix | Two distinct attributed sources link to the production target; helpers and test-to-test calls do not qualify. |
| pipeline_rust_test_attribute_survives_large_docs_in_both_modes | Compact marker survives 5,000-byte documentation; live CALLS and TESTS point to parse in both modes. |
| rustlsp_collection_imports_preserve_library_owner | Exact constructor/method identities retain std HashMap/HashSet and alloc BTreeMap owners. |
| rust_exact_calls_survive_without_name_fallback_in_both_modes | Real extraction and both materializers preserve exact local free-function edges while refusing guessed, method, external and shadowed targets; a C control stays positive. |
| rustlsp_missing_path_does_not_bind_same_named_local_function | An unreachable qualified path remains unresolved while the genuine local invocation resolves exactly. |

The binding table includes library/receiver decoys, aliases, block imports, nested
functions, constants, tuple structs, foreign declarations and glob scopes. Expectations
are test-owned exact counts and qualified targets, not values read from the resolver.

The two persisted-edge pipeline regressions exercise legacy and closure restoration:
current-epoch CALLS/TESTS survive and IMPLEMENTS/OVERRIDE do not. They prove restoration
policy, not whole-corpus full/incremental parity.

Existing convergence, matrix, reference-precision and node probes retain their exact
fixture-derived positive local edges. Callable references remain distinct from calls;
self loops and unsupported method bindings remain excluded.

## Executed oracle discrimination

Temporary fixture variations were executed with production unchanged and then restored.

| Property | Fixture variation | Required assertion failed |
|---|---|---|
| Exact published binding | Invoke other() instead of target(), retaining both declarations | Total remains one; the caller-to-target edge is absent. |
| Unreachable path | Invoke process(10) instead of missing::process(10) | Actual local target differs from the unresolved-path expectation. |
| Live test relationships | Remove parse() but retain declarations and test attribute | CALLS count becomes zero instead of one. |
| Persisted-edge policy | Seed OVERRIDE instead of CALLS | Both restoration routes retain their expected route and fail the expected CALLS count. |
| Import identity | Change Map to OtherMap; separately invoke Sequence::new() instead of Text::new() | Exact local-name and String-target assertions fail. |
| Attribute identity | Add #[test] to a documentation-only helper; remove a source marker; replace the large-doc test attribute with #[allow(dead_code)] | Classification, TESTS count and stored-marker assertions fail respectively. |
| Library owner | Bind Map to BTreeMap instead of HashMap | Exact std HashMap target differs from alloc BTreeMap. |

Independent rustc 1.94.0, edition-2021 checks corroborated block-import shadowing,
glob precedence, tuple-struct value shadowing, external aliases, foreign declaration
shadowing, exact test-attribute classification and collection ownership. These checks
do not establish Cargo/cfg/macro completeness.

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

Diff lint consumes staged files. For focused iteration, use scripts/test.sh --suites
with rust_lsp,parallel,pipeline,repro_lexical_binding_precision; it does not replace
the complete gate.
