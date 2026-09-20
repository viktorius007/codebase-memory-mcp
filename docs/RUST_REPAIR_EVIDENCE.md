# Rust repair evidence

Observed 2026-09-20 on macOS arm64. This is retained evidence, not a request to
repeat the investigation. The commit stack, current gates and next actions are in
[RUST_CODEGRAPH_TRUTH_PLAN.md](RUST_CODEGRAPH_TRUTH_PLAN.md). Parser syntax evidence
is separately retained in [RUST_PARSER_EVIDENCE.md](RUST_PARSER_EVIDENCE.md).

## Regression oracles and executed discriminators

All named fixtures are committed in `tests/`; expectations are test-owned exact
values, not values read back from the implementation. The fixture variants below
were temporary. Production code was unchanged during each discriminator, and the
original test sources were restored before the corresponding green run.

| Test | Observable oracle | Executed discriminator and result |
|---|---|---|
| `rust_use_trees_preserve_exact_paths_and_bindings` | Ten exact local-name/module-path pairs, including nested aliases, grouped self, globs and trivia | Change `Map` to `OtherMap`: exact local-name assertion fails. |
| `rustlsp_nested_use_aliases_resolve_exact_targets` | Exact String/Vec constructor identities and caller in local and cross resolvers | Change the `Text::new()` call to `Sequence::new()`: Vec target fails the String expectation. |
| `extract_rust_test_attributes_match_paths_not_documentation` | Exact per-function classification, with each expected definition present exactly once | Change the documentation-only helper to `#[test]`: true classification fails the false expectation. |
| `testdetect_explicit_test_marker_does_not_require_name_prefix` | Exactly two distinct attributed test sources link to the production target; helpers and test-to-test calls do not qualify | Remove one compact source marker: one TESTS edge fails the expected two. |
| `pipeline_rust_test_attribute_survives_large_docs_in_both_modes` | The stored marker survives when 5,000-byte documentation causes the optional decorators array to be omitted; sequential and parallel modes both tested | Replace `#[test]` with `#[allow(dead_code)]`: stored-marker assertion fails. |
| `rustlsp_collection_imports_preserve_library_owner` | Six exact constructor/method targets, exact caller/count and floating-point confidence floor, in local and cross resolvers | Before production fix: `alloc.collections.HashMap.len` fails expected `std.collections.HashMap.len`. After fix, change imported `Map` to BTreeMap: `alloc.collections.BTreeMap.len` fails the HashMap expectation. |

Pre-packaging final source: **1,252 passed** across
`extraction,extraction_imports,rust_lsp,pipeline,parallel`, ASan/UBSan. The attribute
fixture run had **627 passed, 3 expected failures**; collection pre-fix run had
**1,251 passed, 1 expected failure**; collection fixture run had **523 passed,
1 expected failure**. Exact failure messages were inspected, not inferred from exit
codes. These are targeted-suite results; the packaging gates are recorded in the plan.
No mutation sweep was run, and these results do not claim complete Rust coverage.

Canonical targeted invocation:

```sh
set -euo pipefail
source scripts/test-runtime.sh
cbm_test_runtime_init
trap 'cbm_test_runtime_cleanup "$PWD/build/c/codebase-memory-mcp"' EXIT
CCACHE_DISABLE=1 CBM_NO_CCACHE=1 ASAN_OPTIONS=detect_leaks=0 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  scripts/test.sh --suites extraction,extraction_imports,rust_lsp,pipeline,parallel
```

## Independent compiler oracles

Tool: `rustc 1.94.0`, edition 2021. These exact inputs were compiled in a disposable
directory; the commands below are self-contained replay commands. Rust warnings about unused imports/results/functions were nonfatal.
Do not write compiler outputs into a live corpus checkout.

### Import paths and aliases

Input `import-oracle.rs`, SHA-256 `47ca14c8a3e9cfba66ac3736599266621f3b28c7eda190977fa86246403a2489`.

```rust
mod pm_cli_grammar { pub mod context { pub trait RepositoryContinuity {} } }
pub(crate) use pm_cli_grammar::context::RepositoryContinuity as RepositoryContinuityPort;
use std::{collections::{HashMap as Map, BTreeMap}, io::{self as io_mod, Read}, sync::*};
mod config { pub struct Config; }
mod nested {
    use {crate::config::Config, super::helper};
    pub fn check() { let _ = Config; helper(); }
}
use std :: /* path trivia */ fmt :: {self, Debug as _};
fn helper() {}
struct Repository;
impl RepositoryContinuityPort for Repository {}
fn requires_port<T: pm_cli_grammar::context::RepositoryContinuity>() {}
fn main() {
    requires_port::<Repository>();
    let _: Map<u8, u8> = Map::new();
    let _: BTreeMap<u8, u8> = BTreeMap::new();
    let _: io_mod::Result<()> = Ok(());
    let _: Arc<u8> = Arc::new(1);
    let _: fmt::Result = Ok(());
    let mut input = &b"x"[..];
    input.read_exact(&mut [0]).unwrap();
    nested::check();
}
```

```sh
rustc --edition=2021 --crate-name import_oracle import-oracle.rs -o import-oracle && ./import-oracle
```

Compiled and ran with exit 0; the alias implements the declared trait, nested paths type-check, and the read succeeds.

### Attribute trivia and documentation

Input `test-oracle.rs`, SHA-256 `cdf6d549f499c0b62851a475c3ccc31a771969ce89f791353e76402b4757f2e6`.

```rust
#[ /* outer /* nested */ trivia */ test ]
fn rejects_empty() {}
#[ test]
fn unicode_whitespace() {}
fn test_helper() {}
#[doc = "tokio::test"]
fn helper() {}
```

```sh
rustc --edition=2021 --crate-name test_oracle --test test-oracle.rs -o test-oracle && ./test-oracle --list
```

`rustc --test` followed by `--list` listed exactly `rejects_empty` and `unicode_whitespace`: 2 tests, 0 benchmarks. Neither helper was listed.

### Invalid alloc owner

Input `hashmap-owner.rs`, SHA-256 `1bd7323cac81b73036db4500e30e203cbc3701c8b55125e76d18295757cbea11`.

```rust
extern crate alloc;
use alloc::collections::HashMap;
fn main() { let _: HashMap<u8, u8> = HashMap::new(); }
```

```sh
rustc --edition=2021 --crate-name hashmap_owner hashmap-owner.rs -o hashmap-owner
```

Compilation failed with E0432: no HashMap in alloc::collections. The compiler suggested std::collections::HashMap.

### Valid collection owners

Input `collection-owner-oracle.rs`, SHA-256 `935d66b78a75c42c741ea68b7b3cb53be24b953d611272f7a630d7b516e33129`.

```rust
extern crate alloc;
use std::collections::{HashMap as Map, HashSet as Set, BTreeMap as Tree};
fn run(m: &Map<i32, i32>, s: &Set<i32>, t: &Tree<i32, i32>) {
    m.len(); s.len(); t.len();
    let _ = Map::<i32, i32>::new();
    let _ = Set::<i32>::new();
    let _ = Tree::<i32, i32>::new();
}
fn main() {
    let map = std::collections::HashMap::<i32, i32>::new();
    let set = std::collections::HashSet::<i32>::new();
    let tree: alloc::collections::BTreeMap<i32, i32> = std::collections::BTreeMap::new();
    run(&map, &set, &tree);
    println!("{}", std::any::type_name_of_val(&map));
    println!("{}", std::any::type_name_of_val(&set));
    println!("{}", std::any::type_name_of_val(&tree));
}
```

```sh
rustc --edition=2021 --crate-name collection_owner collection-owner-oracle.rs -o collection-owner-oracle && ./collection-owner-oracle
```

Compiled and ran with exit 0. The std-to-alloc BTreeMap assignment type-checked. Exact output:

```text
std::collections::hash::map::HashMap<i32, i32>
std::collections::hash::set::HashSet<i32>
alloc::collections::btree::map::BTreeMap<i32, i32>
```

## Review findings that changed the repair

- `is_test` is inherited by helpers in test files. The first bypass was rejected;
  publication now requires the compact Rust attribute marker.
- The optional decorators array can be omitted by the bounded serializer. The marker
  is generated from the definition before optional fields and shared by both modes.
- Rust accepts non-ASCII Pattern_White_Space. The classifier handles its five non-ASCII
  code points; U+2028 is exercised in both the regression and compiler oracle.
- `ASSERT_GTE` casts operands to integers in this test framework. The new confidence
  assertion uses `ASSERT_TRUE(confidence >= floor)` to retain its fractional meaning.
- Changing HashMap/HashSet owner strings without removing their old std aliases would
  create self-aliases. The correction removes those entries and retains BTreeMap's alias.

No review or compiler result here validates cfg/macro identity, import lexical scopes,
all standard-library API entries, or the suppressed graph relationships.

## Artifact retention

Large raw logs remain under `/private/tmp/cbm-rust-repair-evidence/` and may expire.
They are supplementary: source inputs, exact outcomes, compiler identity, permanent
regression names and the commit/gate ledger are retained in repository documentation.
The initial mixed-source import build is **not** before/after evidence. Do not reuse
its failure as a clean baseline. Source hashes taken during temporary fixture variants
are likewise not the final tested source.
