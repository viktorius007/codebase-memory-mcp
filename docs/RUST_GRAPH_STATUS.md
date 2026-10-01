# Rust graph: verified state and direction

Dated 2026-10-01. This file is the only authority on Rust graph accuracy, its
measurement and the work that follows. Every figure below was produced by the
commands in "Reproduce" on that date; nothing is carried over from earlier
reports. Earlier plans, reviews and ledgers are retired: tracked ones live in
Git history, untracked ones in `private/archive/rust-s3-2026-10-01.tar.zst`.
Do not act on them.

## Direction

Set by the tech lead on 2026-10-01 under the owner's delegation. It replaces
the rule of 2026-09-27 that Rust resolution must not use rust-analyzer.

1. Rust semantic links (calls, imports, usages, trait implementations, method
   ownership) come from a rust-analyzer SCIP index of the repository, mapped
   onto the tree-sitter graph nodes.
2. tree-sitter plus the C resolver stays as the structure layer (files,
   definitions) and as the fallback for files rust-analyzer does not cover.
   Fallback links carry a marker that tells the reader they are partial.
3. The C "proof-only" rebuild (`s3/proof` and its lanes I, F, L, S, plus the
   parked macro, G17 and Stage 2 lanes) stops. Its branches stay unmerged.
4. The Rust grammar is forked and fixed, gated by `tools/rust-audit`.

Why, in measured terms:

- The published call graph finds 52.2% of in-repository call sites; method
  calls 33.9%. Its links are right (at most 0.6% suspect), so the failure is
  missing links, not wrong ones.
- Every missing method call needs the receiver's type. Reaching the 89%
  ceiling the retired plan claimed requires generic substitution, `Deref`
  chains and return-type propagation through dependency sources: a type
  checker. rust-analyzer's own layers for this are 139,000 lines
  (`hir-def` 38,873, `hir-ty` 70,798, `hir-expand` 9,680, `hir` 19,482);
  the C resolver is 7,581 (`internal/cbm/lsp/rust_lsp.c`).
- rust-analyzer already produces the complete answer. `tools/rust-audit`
  turns its SCIP index into 533,785 resolved call sites in 10.6 s.
- The parser is not the obstacle: grammar defects lose no definition, call,
  import or impl on the measurement corpus.

Cost the owner accepts with this direction: the accurate mode needs a Rust
toolchain with rust-analyzer, a repository whose `cargo metadata` succeeds,
and the time and memory in "rust-analyzer cost" below.

## Measurement setup

- Corpus: codex-rs at `bc98fc69f8`, 4,698 `.rs` files, 1,879,640 lines
  (`/tmp/cbm-rust/corpus`; recreate with
  `git -C /Users/viktor/Projects/github/codex worktree add <dir> bc98fc69f8`).
- Oracle: `rust-analyzer scip .` with toolchain 1.95.0 on
  `aarch64-apple-darwin`, default features (`private/rust-s3/codex.scip`,
  356 MB). 4,486 documents, 3,415,976 occurrences, 108,923 global
  definitions. A fresh run on 2026-10-01 reproduced the same counts.
- Published graph: index of the corpus by main's lineage at `a44f6b73`
  (`/tmp/cbm-rust/s3/work/cargo-roots/measure-final2.kF9hgj/cache/corpus.db`,
  99,549 CALLS edges). Re-index with
  `scripts/rust-corpus-measure.sh <binary> <corpus> <scip> <out>` if that
  file is gone.
- Truth is derived independently of the repository's own scorers
  (`scripts/scip-*.py`): rust-analyzer's parser finds each call site, the SCIP
  occurrence at the callee name gives its target.

Oracle limits, all measured:

| Limit | Size |
|---|---|
| Corpus files with no oracle data (other-platform or orphan) | 212 of 4,698 |
| Syntactic call sites with no oracle target | 5,456 of 539,241 (1.0%) |
| Global symbols defined in more than one file (ambiguous target) | 128 of 108,546 |
| Trait-implementation relationships in the SCIP file | 0 (take them from `impl` syntax) |

## Published graph against the oracle

Call sites whose target is defined in the corpus and whose caller is a
function (`tools/rust-audit/join.py`):

| Site kind | Published | Total | Share |
|---|---|---|---|
| Path call (`f()`, `m::f()`, `T::f()`) | 76,209 | 101,519 | 75.1% |
| Method call (`x.f()`) | 29,939 | 88,386 | 33.9% |
| Call inside macro arguments | 13,848 | 40,041 | 34.6% |
| All | 119,996 | 229,946 | 52.2% |

Another 458,262 call sites target the standard library or dependencies; the
graph has no nodes for those targets.

Published Rust function-to-function CALLS pairs, 98,915
(`tools/rust-audit/precision.py`):

| Verdict | Pairs | Share |
|---|---|---|
| Correct, explicit call | 87,667 | 88.6% |
| Correct, call inside macro arguments | 7,573 | 7.7% |
| Unverifiable, caller file not built on the oracle platform | 2,959 | 3.0% |
| Unverifiable, caller has no oracle data | 147 | 0.1% |
| Oracle resolves the name to another function | 301 | 0.3% |
| Caller references no function of that name | 267 | 0.3% |

The last two rows are an upper bound on wrong links: hand checks of two
"another function" rows found the graph right and the oracle symbol ambiguous
(`cli/tests/login.rs` `codex_command`, `http-client/src/bin/custom_ca_probe.rs`
`run_probe`).

Missing path calls need name resolution only (`tools/rust-audit/misses.py`):

| Callee written as | Defined in | Published |
|---|---|---|
| bare name | same file | 97.2% of 38,650 |
| bare name | same crate, other file | 46.8% of 23,526 |
| `Type::f` | same crate, other file | 66.6% of 9,712 |
| `Type::f` | other crate | 74.0% of 11,783 |
| `module::f` | same crate, other file | 50.6% of 3,918 |
| `module::f` | other crate | 62.9% of 3,340 |

Method calls by what the receiver is (in-corpus targets):

| Receiver | Published | Total |
|---|---|---|
| `self` | 8,327 | 11,888 |
| local initialised by a path call | 6,091 | 11,211 |
| local initialised by a method call | 1,035 | 10,970 |
| binding from a pattern (`match`, `if let`, `for`) | 1,041 | 9,130 |
| field of a local | 1,290 | 9,017 |
| result of a method chain | 1,243 | 7,802 |
| field of `self` | 2,229 | 6,687 |
| parameter with a written type | 4,324 | 6,049 |
| local initialised by a path call followed by `?` or `.await` | 2,351 | 5,639 |
| result of a call | 960 | 5,127 |
| closure parameter | 214 | 1,988 |

Other link families (`tools/rust-audit/families.py`):

| Family | Result |
|---|---|
| Function definitions | 52,925 of 53,160 have a node (99.6%) |
| Struct, enum, trait definitions | 8,785 of 8,979 (97.8%) |
| Type aliases and associated types | 283 of 476 (59.5%) |
| Enum variants | 0 of 8,181: not modelled as nodes |
| Fields | 23,949 of 27,961 (85.7%) |
| Constants and statics | 4,389 of 5,075 (86.5%) |
| `macro_rules!` definitions | 0 of 51 have a node |
| Modules | not checked: module nodes are named by file path, the oracle by module name |
| Method owner link (`DEFINES_METHOD`) | 84.4% correct, 14.8% absent, 4 wrong |
| IMPORTS, recall | 32,902 of 48,856 uses of in-corpus items (67.3%) |
| IMPORTS, precision | 32,902 confirmed, 21,244 not confirmed, 1,510 unverifiable |
| USAGE to a field | 79,590 confirmed, 96,988 not confirmed |
| USAGE to a method | 9,170 confirmed, 38,877 not confirmed |
| USAGE to a struct | 36,942 confirmed, 20,967 not confirmed |
| Trait implementations | 2,504 `impl Trait for Type` blocks; 0 links published |

Unconfirmed USAGE and IMPORTS links are name matches. Among USAGE links to
fields, methods and structs, 70,963 name a target the oracle resolves
elsewhere and 85,807 name something the function never references in the
corpus (example: `path.display()` on a standard
library path linked to `AbsolutePathBuf::display`,
`app-server/src/request_processors/projects.rs:321`). Unconfirmed IMPORTS
include links from Rust files to nodes of other languages.

## Parser

Vendored grammar: `tree-sitter-rust` v0.24.2 (`77a3747`), byte-identical to
upstream apart from a trailing newline. Upstream HEAD is that release
(2026-03-27). Runtime: tree-sitter with ABI 15.

`tools/rust-audit/tsdiff` compares 42 fact kinds by byte range between the
vendored parser and rust-analyzer's parser. On 83,552 valid files of stable
code (35,380,317 lines: Cargo registry cache, codex corpus, cargo, bevy, ruff,
serde, nextest):

| Measure | Vendored grammar | Experimental patch |
|---|---|---|
| Files with error nodes | 6,689 | 199 |
| Files with a wrong tree and no error node | 613 | 152 |
| Files with any disagreement | 7,302 | 351 |
| Codex corpus files with any disagreement | 27 | 1 |

On the codex corpus the defects lose no definition, call, import or impl; they
add 15 spurious call nodes.

Defects confirmed on rustc 1.98.1, one file each in `tools/rust-audit/cases/`:

| Case | Input | Vendored result |
|---|---|---|
| 01 | `&v[..=i]` | assignment, no error |
| 02, 03, 04 | `0..*n`, `a..&b`, `..-a` | binary operator, no error |
| 05, 23 | `break (a, b)`, `break 'l &x` | spurious call or binary operator, no error |
| 06 | `s..(s + n)` | spurious call, no error |
| 07, 35 | `m! {} *p = 1;`, `m! {} (a)` | joins two statements, no error |
| 08 | `str![..]`, `str!(..)` | error; macro call lost |
| 37 | `try!(r)` (edition 2015) | error; macro call lost |
| 09 | `raw` as a type or binding name | error; following functions lost |
| 10 | `type X;` in an `extern` block | error; type lost |
| 11, 28 | closure parameter trailing comma or attribute | error |
| 12, 13, 27 | attributes in struct patterns, tuple elements, repeated on a parameter | error |
| 14, 18 | turbofish inside a type | error; following function lost |
| 15, 16, 31 | `where (): T`, `where fn(u8): T`, `where mac!(u8): T`, `where dyn T: U` | error |
| 17, 19 | function or struct named like a primitive type | error |
| 21 | `~` or lone `$` in `macro_rules!` | error |
| 22 | negative literal as const generic argument | error |
| 29 | `safe fn`, `safe static` | error |

The experiment (`tools/rust-audit/grammar-experiment.patch`, also branch
`audit/feasibility` at `c8867a6` in `../tree-sitter-rust`) fixes all but cases
19 and 21 (lone `$`). It is not mergeable: it makes `safe` a keyword, which
breaks identifiers named `safe` (`tui/src/insert_history.rs:547` in the
corpus); 68 files have new errors on lines containing `raw`; and upstream's
`tree-sitter test` reports changed trees for macro statements.

Fixed limits of any tree-sitter grammar: no edition switch (37 of 83,552 files
parse only as edition 2015); macro arguments are flat tokens. The runtime
re-parses a macro argument range as Rust with correct byte offsets
(`ts_parser_set_included_ranges`; verified on
`assert_eq!(foo(a), b.bar::<T>(c)?, "m {}", x.y)`).

Nightly-only syntax: 495 of 11,489 compiler and standard-library files have
error nodes. Out of scope.

## rust-analyzer cost

| Workspace | Lines | Wall time | Peak memory | SCIP size | Build artefacts |
|---|---|---|---|---|---|
| codex-rs | 1,879,640 | 470 s | 22.2 GB | 356 MB | 978 MB |
| nextest | 120,254 | 49 s | 3.3 GB | 18 MB | not measured |

The tool's own full index of codex-rs takes about 1 minute. `rust-analyzer
scip` is a batch command with no incremental mode.

## The retired ceiling claim

The retired plan stated that 89.0% of call relations are provable without a
type checker. A random sample of 24 sites it labelled provable was read
against source: all 24 are derivable in principle, and the derivation needs
return-type propagation, `Result`/`Option` through `?`, `async` returns, and
in at least one case generic substitution plus `Deref` through a dependency
type (`accepted_stdin_write_ids.lock().await` then `.contains(..)`,
`exec-server/src/local_process.rs:673`). The figure is an upper bound that
assumes that machinery exists. It was not reproduced.

## Work, in order

Each step lands only when its gate passes on the corpus.

1. **SCIP ingestion.** Add a pass that reads a SCIP index for the repository
   (given by path, or produced by running `rust-analyzer scip`), maps each
   occurrence to graph nodes by file and line, and publishes CALLS, IMPORTS,
   USAGE, trait-implementation and method-owner links for covered files.
   `tools/rust-audit/tsdiff/src/bin/truth.rs` and `refs.rs` are the reference
   for the mapping (protobuf reader included, no dependency).
   Gate: `join.py` reports at least 99% for every site kind;
   `precision.py` reports no pair outside the "correct" and "unverifiable"
   rows other than pairs whose symbol is among the 128 ambiguous ones.
2. **Stop publishing name-matched Rust links.** Rust USAGE and IMPORTS from
   the generic name matcher are removed for files covered by SCIP and, for
   uncovered files, published only when the C resolver proved the binding.
   Gate: `families.py` reports zero "not confirmed" USAGE and IMPORTS.
3. **Coverage reporting.** A file without SCIP data is recorded in
   `index_coverage` with its reason (no index, other platform, stale index);
   answers that depend on it say so. The SCIP generation and the commit it was
   built from are stored with the project.
4. **Grammar fork.** Vendor a fixed `tree-sitter-rust` with `grammar.js` in
   `tools/` (as `tools/tree-sitter-arkts` does), generated at ABI 15.
   Gate: `tsdiff` over the seven corpora in "Reproduce" reports zero files
   with error nodes and zero with fact differences, excluding the two
   convention differences listed there; upstream's `tree-sitter test` changes
   are each reviewed.
5. **Fallback quality, optional.** Cross-file name resolution for bare and
   module-qualified paths (25,310 missing path-call sites). No type
   propagation work.

## Not measured

- `s3/proof` (`e67af2a4`) and its candidate graph.
- The installed binary `~/.local/bin/codebase-memory-mcp` reports `dev`; its
  commit is unknown.
- rust-analyzer behaviour when `cargo metadata` fails, on workspaces with
  several target platforms, and with non-default features.
- Refresh strategy after edits.
- Languages other than Rust.

## Reproduce

Needs `../rust-analyzer` (source checkout, for its parser crate) and
`../tree-sitter-rust` beside this repository.

```
cd tools/rust-audit/tsdiff
CARGO_TARGET_DIR=/tmp/ts-audit-target cargo build --release
mkdir -p /tmp/ts-audit && cd /tmp/ts-audit
find /tmp/cbm-rust/corpus -name '*.rs' -type f > codex.list
/tmp/ts-audit-target/release/tsdiff codex.list codex 400        # parser comparison
/tmp/ts-audit-target/release/truth <index.scip> <corpus> truth.tsv
/tmp/ts-audit-target/release/refs  <index.scip> <corpus> refs.tsv
/tmp/ts-audit-target/release/scipstat <index.scip>
DB=<corpus.db>
sqlite3 -separator $'\t' "file:$DB?mode=ro" "select id,label,name,file_path,start_line,end_line from nodes where file_path like '%.rs'" > g_nodes.tsv
sqlite3 -separator $'\t' "file:$DB?mode=ro" "select source_id,target_id,coalesce(json_extract(properties,'\$.strategy'),'') from edges where type='CALLS'" > g_calls.tsv
for t in IMPORTS DEFINES_METHOD USAGE; do sqlite3 -separator $'\t' "file:$DB?mode=ro" "select source_id,target_id from edges where type='$t'" > g_$t.tsv; done
cd <repo>/tools/rust-audit
python3 join.py; python3 precision.py; python3 misses.py; python3 families.py
python3 classify.py registry codex cargo bevy ruff serde nextest
```

`AUDIT_DIR` (default `/tmp/ts-audit`) and `AUDIT_CORPUS` (default
`/tmp/cbm-rust/corpus`) redirect the scripts. Build against another grammar
with `RUST_GRAMMAR_DIR=<dir with parser.c, scanner.c, tree_sitter/>`.

Parser corpora: `~/.cargo/registry/src` (73,310 files), the codex corpus,
`../cargo`, `../bevy`, `../ruff`, `../serde`, `../nextest`; nightly:
`../rust/{compiler,library,src}`.

`tsdiff` convention differences that are not defects: rust-analyzer parses
macro calls inside attribute values (`#[doc = concat!(..)]`), tree-sitter
keeps them as tokens; a negative const generic argument is a literal node in
tree-sitter and a prefix expression in rust-analyzer.

## Repository state

- `main` at `a44f6b73` is the published line.
- Unmerged and retired: `s3/proof`, `s3/i*`, `s3/f*`, `s3/l1`, `s3/s1`,
  `s3/ceiling`, `s3/stage0`, `s3/mc-main`, `s3/g17*`, `s3/docs-plan`,
  `w1/*`, `w2/join`. Their worktrees are under `/tmp/cbm-s3-*`,
  `/tmp/cbm-w*`, `/tmp/cbm-fable-*`. Delete a branch only after confirming it
  holds nothing step 5 wants.
- `rm` is blocked on this machine; use `trash`. Never touch `~/.cbm`, the
  installed binary or its daemon; never index the live codex checkout.
- Gates before a code commit: `make -f Makefile.cbm lint-ci`;
  `scripts/test.sh BUILD_DIR=<dir>` with private `CBM_RUNTIME_DIR` and
  `CBM_CACHE_DIR`.
