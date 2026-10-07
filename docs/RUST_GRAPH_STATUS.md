# Rust graph: verified state and direction

Dated 2026-10-07, at `main` aa48ba96, amended the same day after an adversarial review
(`private/rust-run/review/REVIEW.md`; eight reports under `review/out/`). This file is the only authority on Rust
graph accuracy, its measurement and the work that follows. Every figure below
comes from the two frozen score runs of that commit (`/tmp/cbm-score-aa48ba96`,
`/tmp/cbm-score2-aa48ba96`; summaries `private/rust-run/summary-aa48ba96.txt`
and `summary-ruff-aa48ba96.txt`), produced by the commands in "Reproduce". The
2026-10-01 version of this file, its figures and its recommendation are
superseded; it stays in Git history. The run's per-merge record is
`private/rust-run/SCORE.md` (one line per merge) and `run-ledger.md`
(git-ignored, on this disk only).

## Direction

The owner's rules stand: no rust-analyzer, rustc or cargo at index time; a
wrong link is worse than a missing one; every link the source can prove is
published. The 2026-10-01 recommendation to take Rust links from a
rust-analyzer SCIP index is withdrawn. The run of 1 to 7 October (244 commits
between a44f6b73 and aa48ba96) reached, with the C resolver alone:

| Corpus | Call sites linked | Wrong-or-flagged pairs | On 2026-10-01 |
|---|---|---|---|
| codex-rs (measurement corpus) | 225,790 / 229,952 (98.2%) | 1 (not in the truth) | 119,996 (52.2%), 568 |
| ruff (held-out since 6 Oct) | 103,252 / 112,301 (91.9%) | 1 (conditional) | 77,202 (68.7%), 296 on 6 Oct |

Corpora the fleet never saw, scored by both review providers on 7 October with identical
figures (bench roots `~/.cache/cbm-rust-bench-<name>`, truth by the same generator):

| Corpus | Calls linked | Wrong links read in source | IMPLEMENTS | IMPORTS |
|---|---|---|---|---|
| project-management (owner's) | 39,680 / 47,116 (84.2%) | 1 | 37.7% | 95.3% |
| jmap (owner's) | 4,084 / 5,974 (68.4%) | 10 | 37.6% | 43.1% |
| jmap-client (owner's) | 6,029 / 6,163 (97.8%) | 0 | 100% | 99.5% |
| bevy | 60,036 / 84,297 (71.2%) | 72 | 44.8% | 95.2% |

jmap's figure is a manifest defect (`jmap-core.workspace = true` is not parsed, `rust_cargo.c:35`),
not resolution. jmap-client's 390 flagged pairs are all correct links mislabelled by the truth
generator. Most bevy mechanisms share one cause: the resolver has no model of standard-library
inherent methods or of trait scope, so a project item wins any name tie.

The 2026-10-01 version estimated that a C resolver without generic
substitution reaches at most 78.4% of explicit call sites. The measured figure
is 98.2%.

The review of 7 October (two providers, identical briefs, four corpora the fleet never saw)
withdrew three of the guarantees the fleet stated. What holds and what does not:

- Every published figure in this document reproduces from independent code, and 400 randomly
  drawn credited call sites across six corpora were all right on reading.
- "Zero wrong links" is withdrawn. A `name @ pattern` binding never binds (the forked grammar's
  `captured_pattern` has no fields; `rust_lsp.c:10935` reads fields), so the outer type is
  dispatched: one wrong ruff link the harness counts as correct. On corpora the fleet never saw
  (table below) 83 wrong links were read in source, from twelve mechanisms listed in REVIEW.md.
- "No Rust link is a name match" is withdrawn for USAGE: `mod x; pub use x::*` publishes a
  Module-to-function edge (658 on ruff); macro tokens that are discarded or stringified
  publish USAGE; a field token inside a macro binds an imported free function.
- "No link crosses languages" is withdrawn for IMPORTS: a Rust `use` or glob links into `.toml`
  and `.proto` nodes (codex 20, ruff 2, bevy) through the generic import fallback.
- `unresolved.py` is not a proof: it counts a site as covered when the caller links any
  same-named target (8 codex, 2 ruff sites are neither published nor recorded).
- The seeded mutation lane (`tests/mutation/`) has not applied since 20 September; no CI runs
  it. Two providers showed guards whose removal no suite catches, five with a demonstrated
  wrong link once removed.

What the fleet stated, kept where it held:

- No Rust link is a name match for CALLS and IMPORTS from a `use`. A `use` resolves through the module tree
  (29b54531, 941873cb); the package-map name guess for Rust imports is gone
  (71723861); a bare name binds a Rust type, method or constant only through a
  `use`, a glob or module placement (f23bad8e, 390deeb5, 75e91b22).
- No link crosses languages on a name match, for any producer (IMPORTS,
  CALLS, USAGE, DECORATES, INHERITS, RAISES, TESTS, WRITES): one language-family
  guard in the candidate registry (1633e0f8).
- A call the resolver cannot prove is withheld, not guessed: trait dispatch
  the receiver does not select (0e10fa93, 7b9e746d), `From`/`Into` with
  several impls (8b5c22ae, 7e47aad9), channel handles and `Deref`/`dyn`
  targets (8512665d), reference receivers with two candidate impls (903dcdc3).
- Every Rust call or reference site the resolver gave up on is recorded with
  its stop reason and is queryable (aa48ba96; "Unresolved sites" below).
  `tools/rust-audit/unresolved.py` proves that every in-corpus truth site is
  either published or recorded, on both corpora.

## Measurement setup

- Corpus A, measurement: codex-rs at `bc98fc69f8`, 4,698 `.rs` files,
  1,879,640 lines, `~/.cache/cbm-rust-bench/codex/codex-rs` (read-only; never
  edit or commit there). Oracle: `rust-analyzer scip .` with toolchain 1.95.0
  on `aarch64-apple-darwin`, default features (`private/rust-s3/codex.scip`,
  356 MB; copy at `/tmp/cbm-truth/frozen/codex.scip`).
- Corpus B, held-out since 6 Oct: ruff at `b8c5e73a`, 1,953 `.rs` files,
  763,814 lines, `~/.cache/cbm-rust-bench2/ruff`; oracle `ruff.scip` beside it,
  produced by the stable rust-analyzer in 3 minutes. Every merge since 6 Oct
  01:35 was scored on both corpora; a fix had to hold on ruff, which no arm
  had seen.
- Truth tables (`truth.tsv`, `refs.tsv` in each bench root) come from
  `tools/rust-audit/tsdiff` (`truth`, `refs`, `scipstat` binaries, which need
  `../rust-analyzer` for its parser crate): rust-analyzer's parser finds each
  call site, the SCIP occurrence at the callee name gives its target. The
  generator was corrected twice during the run: same-named definitions of
  sibling test and bin targets are resolved by the SCIP enclosing range
  (8f8e6299), and a `#[path]`-compiled file is re-rooted at the module path
  the oracle defines it under (e8c84d6c, f4aa834a). Earlier tables are kept as
  `*.pre-tg` and `*.pre-cp`. The ruff truth has been checked only through the
  pairs the scorer flagged (all read by hand on 6 Oct: 22 graph-wrong, 3
  scorer-wrong, 17 conditional; the 22 are gone).
- Score command: `scripts/rust-score.sh <binary> <out-dir>` indexes the corpus
  into a private cache (codex 48 s, ruff 16 s), exports the graph tables, runs
  the `tools/rust-audit` scripts and prints the summary. Credit is per
  caller-callee pair: a call site counts when a CALLS edge joins its caller to
  its callee (positions are not stored). Sites that share a pair with another
  credited site are an upper bound on unverified credit: 75,911 of 225,790 on
  codex, 41,927 of 103,252 on ruff.
- Unverifiable pairs (caller file not compiled on the oracle platform, caller
  with no oracle data, oracle left the site unresolved) are counted and
  reported, never credited: 5,581 on codex, 102 on ruff.

## Calls

Call sites whose target is defined in the corpus and whose caller is a
function (`tools/rust-audit/join.py`):

| Site kind | codex-rs | ruff |
|---|---|---|
| Path call (`f()`, `m::f()`, `T::f()`) | 100,915 / 101,522 (99.4%) | 30,396 / 31,119 (97.7%) |
| Method call (`x.f()`) | 85,895 / 88,389 (97.2%) | 63,005 / 68,994 (91.3%) |
| Call inside macro arguments | 38,980 / 40,041 (97.4%) | 9,851 / 12,188 (80.8%) |
| All | 225,790 / 229,952 (98.2%) | 103,252 / 112,301 (91.9%) |

Published Rust function-to-function CALLS pairs
(`tools/rust-audit/precision.py`):

| Verdict | codex-rs | ruff |
|---|---|---|
| Correct, explicit call | 155,377 (84.6%) | 68,525 (91.8%) |
| Correct, call inside macro arguments | 22,687 (12.4%) | 6,029 (8.1%) |
| Unverifiable, caller file not compiled on the oracle platform | 3,614 | 9 |
| Unverifiable, oracle left the site unresolved (dev-dependency cycle, inactive cfg) | 1,299 | 69 |
| Unverifiable, oracle has no target for the name in macro arguments | 393 | 19 |
| Unverifiable, caller has no oracle data | 275 | 5 |
| Correct target, reference is not a call (fn value) | 1 | 0 |
| Flagged | 1 | 1 |

The two flagged pairs, both read against source:

- codex `core/src/windows_sandbox.rs:381 emit_windows_sandbox_setup_failure_metrics`
  to `windows-sandbox-rs/src/setup_error.rs:155 extract_failure`: a
  `cfg(windows)` path the oracle, built on macOS, never compiled. The link is
  right; the truth has no row for it.
- ruff `crates/ruff_formatter/src/buffer.rs:661 Recording::new` to
  `buffer.rs:39 Buffer::elements`: the graph links the trait declaration, the
  oracle records the blanket `&mut W` forwarder. Conditional; the scorer, not
  the graph, labels it wrong.

Self-recursive calls are published when the receiver type proves the target
(b31b4569); two corpus recursions stay withheld (`records_tests.rs:140`,
`loader/local.rs:370`). A call to one of two `cfg` twins publishes nothing.

## Definitions, owners, implementations

Definition nodes against the oracle's definitions (`families.py`):

| Kind | codex-rs | ruff |
|---|---|---|
| Free functions | 34,134 / 34,134 (100%) | 10,073 / 10,125 (99.5%) |
| Methods | 19,486 / 19,486 (100%) | 16,659 / 16,684 (99.9%) |
| `macro_rules!` | 51 / 51 | 35 / 35 |
| Modules | 5,028 / 5,037 (99.8%) | 2,272 / 2,273 (100%) |
| Constants and statics | 5,070 / 5,075 (99.9%) | 632 / 1,066 (59.3%) |
| Fields and enum variants | 27,961 / 27,961 (100%) | 8,494 / 8,494 (100%) |
| Types (struct, enum, trait, alias) | 17,232 / 17,636 (97.7%) | 11,420 / 12,524 (91.2%) |
| Method owner link (`DEFINES_METHOD`) correct | 19,292 (99.0%) | 16,161 (96.9%) |
| `impl Trait for Type` published as IMPLEMENTS (both in corpus) | 984 / 1,017 (96.8%) | 1,706 / 2,376 (71.8%) |

Trait identity is one `core.*` name for `core::`, `std::` and `alloc::`
paths, imported names and module prefixes (8fca6136).

## Imports and usages

IMPORTS, a `use` of an in-corpus item published as a File-to-item link:

| | codex-rs | ruff |
|---|---|---|
| Published of oracle uses | 48,784 / 49,307 (98.9%) | 19,706 / 20,057 (98.2%) |
| Use target has no graph node | 178 | 1,189 |
| Published, not confirmed by the oracle | 370 | 2,649 |
| Published, unverifiable (platform) | 1,056 | 22 |

One IMPORTS edge differs between two indexes of ruff from run to run
(`generate_cli_help.rs` to `ruff::lib`); its cause is open.

USAGE from Rust sources, by target kind. "Hit / mapped" is recall over the
oracle's function-level references; module-level references (13,855 on
codex, 9,565 on ruff) are confirmed separately by the non-`use` references in
the enclosing Module or File span.

| Target kind | codex confirmed / unconfirmed / unverifiable | codex hit / mapped | ruff confirmed / unconfirmed / unverifiable | ruff hit / mapped |
|---|---|---|---|---|
| Struct | 81,117 / 201 / 1,327 | 71,884 / 72,500 (99.2%) | 40,242 / 1,078 / 0 | 34,537 / 34,784 (99.3%) |
| Enum | 51,833 / 352 / 750 | 48,799 / 48,901 (99.8%) | 20,302 / 45 / 0 | 18,253 / 18,371 (99.4%) |
| Interface (trait) | 2,395 / 4 / 2 | 1,400 / 1,427 (98.1%) | 5,209 / 1 / 0 | 3,677 / 3,679 (99.9%) |
| Type alias | 2,728 / 5 / 14 | 2,421 / 2,667 (90.8%) | 1,525 / 2 / 0 | 1,406 / 1,593 (88.3%) |
| Function | 43,126 / 422 / 1,590 | 72,108 / 72,413 (99.6%) | 7,316 / 1,620 / 0 | 13,491 / 13,759 (98.1%) |
| Method | 12,880 / 232 / 148 | 107,645 / 110,862 (97.1%) | 5,840 / 73 / 0 | 61,905 / 68,081 (90.9%) |
| Field | 169,725 / 856 / 2,463 | 169,725 / 215,967 (78.6%) | 29,938 / 118 / 19 | 29,938 / 40,953 (73.1%) |
| Variable | 13,022 / 241 / 579 | 12,780 / 13,541 (94.4%) | 793 / 6 / 0 | 756 / 1,037 (72.9%) |
| AssociatedConst | 108 / 0 / 0 | 101 / 131 (77.1%) | 1,099 / 12 / 0 | 1,087 / 1,164 (93.4%) |
| EnumVariant | 0 | 0 / 66,424 | 0 | 0 / 37,429 |
| Macro | 0 | 0 / 2,171 | 0 | 0 / 651 |
| Module | 0 | 0 / 243 | 0 | 0 / 33 |

EnumVariant, Macro and Module are not modelled as USAGE targets. Field USAGE
comes from the resolver's field-access records (3cd84a53); a field reached by
bare name is dropped. Rust attributes bind only `macro_rules!` and proc-macro
nodes (c7b13d0c).

## Unresolved sites

A reader asking "who calls X" or "what does F call" can tell a true "nobody"
from "unknown". Every Rust call site and type, member or value reference the
resolver did not link is stored in `unresolved_occurrences` (file, line, byte
span, enclosing item, written name, stop reason) with the `unresolved_sites`
view over it. The stop reason is the resolver's own stop code from a closed
set listed in `src/pipeline/pipeline_internal.h`, never a display message.
`trace_path` answers carry an `unresolved_sites` block (counts of calls and
references in scope, the scope, a paginated listing);
`check_index_coverage` and `index_status` report the totals. Records are
metadata, never nodes or edges; an incremental file replacement removes them.
The published edge set is byte-identical with and without the records on
both corpora.

| | codex-rs | ruff |
|---|---|---|
| Records | 1,540,313 | 368,108 |
| of which calls | 532,030 | 99,565 |
| of which type, member or reference | 1,008,283 | 268,543 |

At most 4,162 recorded codex call sites and 9,049 ruff ones are in-corpus
truth sites (the truth total minus the sites linked); the rest target the
standard library or dependencies, for which the graph has no nodes. Recorded
call sites by stop reason:

| Stop reason | codex-rs | ruff |
|---|---|---|
| `target_not_materialized` | 221,586 | 29,838 |
| `unknown_receiver_type` | 85,906 | 17,115 |
| `strategy_closed` | 55,682 | 8,578 |
| `keyword_syntax_filtered` | 52,651 | 11,275 |
| `function_not_in_registry` | 43,847 | 7,977 |
| `method_not_found` | 41,379 | 11,263 |
| `no_resolution` | 23,742 | 11,359 |
| `associated_item_not_registered` | 4,745 | 1,343 |
| `receiver_identity_not_proven` | 972 | 98 |
| `unresolved_glob_import` | 691 | 59 |
| `bound_declaration_not_proven` | 674 | 449 |
| all other reasons together | 155 | 211 |

Query the records of one project:

```
sqlite3 "file:<db>?mode=ro" "select kind, reason, count(*) from unresolved_sites where project='<p>' group by 1,2 order by 3 desc"
sqlite3 "file:<db>?mode=ro" "select file_path, line, caller, name, reason from unresolved_sites where project='<p>' and name='recv'"
```

Cost of the records, measured back to back on the same machine (aa48ba96):
score CPU within 4% of the previous commit; `score.db` 9 to 10% larger on
codex and 4 to 5% on ruff; peak memory +7.5%.

## Parser

Vendored grammar: a self-maintained fork of `tree-sitter-rust` v0.24.2
(`77a3747`), source and corpus tests in `tools/tree-sitter-rust/`, ABI 15
(`internal/cbm/vendored/grammars/MANIFEST.md`, entry "rust"). Against
rust-analyzer's parser on 83,552 files of stable code (`tools/rust-audit/tsdiff`),
disagreeing files fell from 7,137 to 6, all nightly-only syntax, excluding the
two convention differences and the edition-2015-only files. The 50 inputs in
`tools/rust-audit/cases/` are pinned error-free by
`rust_audit_cases_parse_without_error_nodes` in `tests/test_parse_coverage.c`.
No Rust file of either corpus is recorded as partially parsed or unusable in
`index_coverage` of the frozen score databases.

Orchard's grammar was assessed on 5 Oct (`private/rust-run/orchard/ASSESSMENT.md`):
six lexical fixes were ported (bfccf545: Unicode in block comments, `1._m()`,
comment between `macro_rules` and `!`, spaced and Unicode metavariables,
custom integer suffixes, empty type arguments, raw-string ABI, higher-ranked
bounds after `+`, labelled loop conditions); the whole grammar was rejected
(5 of 40 audit cases error, 7 mis-shaped, 5 of 5 consumer probes regress). Of
Orchard's 183 test inputs, 24 still error in the fork; the stable ones
(`Vehicle<>`, `extern r#"C"# fn`, `'a: while break 'a {}`, `pub extern "C" {`
and the rest listed in the assessment) occur in neither corpus.

Fixed limits of any tree-sitter grammar: no edition switch; macro arguments
are flat tokens. The runtime re-parses a macro argument range as Rust with
correct byte offsets; standard macro forms (`vec!`, `format_args!`, `json!`,
`matches!`, `select!`, `assert*!`, `insta`, `anyhow`, logging) and same-file
and cross-file `macro_rules!` expansions are walked per site (76261787,
a72b33a5, 7e27aec4, 3af26c81, 17daede7, 072bdb7b, 03777ea7).

## Oracle cost

Needed only to regenerate truth, never at index time:

| Workspace | Lines | Wall time | Peak memory | SCIP size |
|---|---|---|---|---|
| codex-rs | 1,879,640 | 470 s | 22.2 GB | 356 MB |
| ruff | 763,814 | about 180 s | not measured | not measured |

The tool's own full index of codex-rs takes 48 s and of ruff 16 s.

## Work, in order

Each item lands only when its score holds on all six corpora, the exact edge-set diff against
the previous `main` is read, and the suites and lint pass. Owner decisions taken 7 October:
a std receiver with a project trait method of the same name is withheld with its own stop
reason (not guessed); the stop records are the disclosure of the tool's limits and the ranked
gap list drives a dated, sourced table of std methods; reading std's source at index time is
deferred until the withheld counts on the owner's projects are measured.

1. Harness: regenerate `tests/mutation/patches/*` against current source and run the lane in
   CI; delete the `published_other_target` early return in `tools/rust-audit/unresolved.py:189`
   and record parenthesised indirect calls; make the truth generator define `#[path]`-compiled
   and test-crate targets (jmap-client 390 false flags, bevy `lib_main`).
2. Located-cause resolver fixes, one arm each: `name @ pattern` binding; the dotted manifest
   form; inherent lookup ignoring impl type arguments and `self`-less associated functions
   (`rust_lsp.c:7553`); the IMPORTS fallback admitting non-Rust targets (`pass_pkgmap.c:1621`);
   `mod x; pub use x::*` USAGE (`pass_usages.c:472`); discarded and stringified macro tokens
   (`extract_usages.c:2973`); Deref dispatch skipping `rust_trait_receiver_refusal`
   (`rust_lsp.c:13219`); reference layers stripped in trait UFCS (`rust_lsp.c:11636`);
   explicit `<Type as Trait<..>>::m` withheld; raw-pointer receivers (`rust_lsp.c:13003`).
3. The withhold rule for std receivers, its stop reason, the dated std-method table skeleton,
   and the gap query over `unresolved_sites` grouped by receiver type and method.
4. Mechanisms reported by one provider, each to be counted from the records after item 3:
   blanket impls linked to themselves; `fold`/`sum`/`product`/`reduce` typed as the item;
   std rules chosen by `strstr` on the type name (`rust_lsp.c:5838`); struct fields typed by
   the struct's own generic parameter; `macro_rules!` hygiene; `try_from` with a
   macro-generated impl; block-local items taking the module path's name; arguments of a
   project's own `macro_rules!` never walked (pm macro recall 25.5%); the parallel pipeline
   dropping IMPLEMENTS for empty impls; one of two cfg-twin bindings published.
5. The earlier queue, still open: the 62 calls 903dcdc3 withheld; WORKLIST ranks 14 to 19 and 22
   (`private/rust-run/audit/ra/measured/WORKLIST.md`); `From` argument proof; EnumVariant,
   Macro and Module USAGE; ruff impl links 71.8% and const/static 59.3%; `registry.c:1101`;
   `fqn.c:65`; the nondeterministic ruff IMPORTS edge; invalid UTF-8 in truncated edge
   properties (`pass_parallel.c:1606`).

## Not measured

- The ruff truth beyond the flagged pairs; the truth generator's behaviour on
  a third corpus.
- Oracle and graph on Windows or Linux `cfg` paths: the oracle ran on
  `aarch64-apple-darwin` only.
- Reading std's source at index time (deferred behind the withheld-site count).
- Graph accuracy for languages other than Rust. The cross-language guard
  (1633e0f8) removed 843 IMPORTS, 372 CALLS, 318 USAGE, 317 DECORATES and 80
  INHERITS edges on ruff and about 120 on codex from Python, TypeScript and
  shell files into Rust; their replacements in those languages were not
  scored.
- The installed binary `~/.local/bin/codebase-memory-mcp` and its daemon:
  never touched by the run.

## Reproduce

```
make -f Makefile.cbm cbm BUILD_DIR=/tmp/cbm-trial -j8
scripts/rust-score.sh /tmp/cbm-trial/codebase-memory-mcp /tmp/cbm-score-<sha8>
CBM_RUST_BENCH=~/.cache/cbm-rust-bench2 CBM_RUST_CORPUS=~/.cache/cbm-rust-bench2/ruff \
  scripts/rust-score.sh /tmp/cbm-trial/codebase-memory-mcp /tmp/cbm-score2-<sha8>
scripts/rust-score.sh --rescore <out-dir>          # audit scripts only, no indexing
python3 tools/rust-audit/unresolved.py <out-dir>   # every truth site published or recorded
```

Each out-dir holds `join.txt`, `precision.txt`, `misses.txt`, `families.txt`,
`wrong_pairs.tsv`, `unconfirmed_sample.tsv`, `unresolved-neither.tsv`, the
graph tables `g_*.tsv` and the database `cache/score.db`. Keep one frozen
score directory per corpus for the current `main`; trash the previous one
when a merge lands. Before a merge, diff the exact edge sets of the trial and
the frozen `score.db` (key: source qualified name, target qualified name,
edge type; grouped by source and target file extension): it caught two
regressions the summaries and `tools/rust-audit/compare_graphs.py` missed.

Regenerate truth (needs `../rust-analyzer` beside this repository):

```
cd tools/rust-audit/tsdiff && CARGO_TARGET_DIR=/tmp/ts-audit-target cargo build --release
/tmp/ts-audit-target/release/truth <index.scip> <corpus> truth.tsv
/tmp/ts-audit-target/release/refs  <index.scip> <corpus> refs.tsv
/tmp/ts-audit-target/release/scipstat <index.scip>
```

Parser comparison: `tsdiff <file list> <name> <jobs>` over the corpora in the
manifest entry; `RUST_GRAMMAR_DIR=<dir with parser.c, scanner.c, tree_sitter/>`
builds against another grammar.

## Repository state

- `main` at aa48ba96 is the published line, not pushed; `main` is the only
  branch. The 75 retired branches of the run are patches under
  `private/rust-run/retired/`.
- Gates before a code commit: `make -f Makefile.cbm lint-ci`;
  `scripts/test.sh BUILD_DIR=<dir>` with private `CBM_RUNTIME_DIR` and
  `CBM_CACHE_DIR`; both pass at aa48ba96.
- `rm` is blocked on this machine; use `trash`. Never touch `~/.cbm`, the
  installed binary or its daemon; never edit the corpora under
  `~/.cache/cbm-rust-bench*`; never index the live codex checkout.

## Trait scope and std dispatch disclosure (2026-10-07)

Concrete dot-call trait candidates require a positive lexical identity proof.
The existing module tree proves declarations in the innermost module, named
`use` imports (including renames and re-exports), and unambiguous module globs;
active block `use` paths, anonymous imports, and globs are supplied separately.
Renamed bindings and anonymous imports propagate through proved ancestor globs
and crate prelude re-exports. Root `extern crate ... as ...` bindings, including
`extern crate self as ...`, enter the extern prelude of child modules. Imports
left open by a recursive re-export are retried after their dependencies settle.
Declared procedural macro exports occupy the macro namespace, so a derive import
does not hide a same-named trait imported by name or a glob. Dotted Cargo dependency fields such as
`facade.workspace = true` name the dependency `facade`, allowing its re-exports
to be followed. Block imports use Rust’s import-path resolution rules. An unconditional trait
declaration in the current source module also proves scope when the file has no
Cargo mapping; attributed declarations do not use this fallback. Supertrait methods are
reachable through a proved imported subtrait. A prelude proof names the canonical
std trait, never a project trait with the same leaf name. Prelude availability
also checks module attributes inherited through out-of-line modules: `no_core`
and `no_implicit_prelude` disable it, and `no_std` withholds alloc-only traits.
Conditional restrictions are conservative. `FromIterator`/`TryFrom`/`TryInto` require a proved
2021/2024 edition, and `Future` requires 2024; a per-file pass with no crate
edition does not assume those additions. For a registered file whose item macro
leaves its module mount unknown, only edition-independent prelude names are
eligible, and any recorded prelude restriction is conservatively a possible
ancestor restriction. The enclosing trait
impl supplies its canonical trait inside its body, as Rust does. Explicit root
`extern crate std` declarations remain available to child modules under `no_std`;
only the automatic std entry is omitted. Comments, strings,
qualified paths in unrelated expressions, and imports in siblings are no proof.
Unknown scope stops with `trait_scope_identity`. Local item macros reuse the
existing rule matcher and transcriber substitution to prove generated traits;
item-shaped arguments consumed by a macro establish nothing. Unsupported
expansions remain opaque. Qualified
trait paths, generic bounds and operator desugaring retain their separate rules.

These rules follow the Rust Reference's
[method lookup](https://doc.rust-lang.org/reference/expressions/method-call-expr.html)
[use declarations](https://doc.rust-lang.org/reference/items/use-declarations.html),
[prelude availability](https://doc.rust-lang.org/reference/names/preludes.html),
and [namespaces](https://doc.rust-lang.org/reference/names/namespaces.html),
consulted on 2026-10-07. No compiler or std source is read while indexing.

When a canonical std receiver has a method name supplied by a project trait,
the dot call is withheld with `unrecognised_std_method`. A trait candidate found
through Deref retains the original std wrapper for this decision. Project
inherent candidates on the pointee remain eligible. This reason denotes a
std/project-trait dispatch collision, including methods whose return type is
already modelled: a return-type rule does not prove the absence of a preceding
inherent method. `unresolved_sites.name` stores `receiver-head.method` for this
reason (for example `alloc.vec.Vec.reserve`); its existing columns are unchanged.
Deref candidates disclose the original std wrapper, or a std head reached later
when the original receiver is a project type. Project inherent methods
are exempt from both the trait-scope and std-collision rules. Method selection
checks inherent precedence at each receiver shape, including mutable references,
and trait-object methods inherited from supertraits. Unproved reference, mutable,
and wrapper dispatch each have their own stop reason. Calls withheld inside
expression macros receive a source-site carrier so their stop records reach
`unresolved_sites`; a trait stop cannot replace an unrelated neighbouring call.
A generic-bound target whose reachable supertraits are unavailable is withheld
with `bound_declaration_not_proven`, including in expression macros. This loses
some previously credited calls: an unknown supertrait can define a competing
method, so ignoring it would not prove dispatch. The current std table supplies
inference rules rather than complete trait method sets; no negative knowledge
about those unindexed supertraits is claimed. Call-shaped tokens in an opaque macro carry
`macro_call_not_proven` records; these are candidates, not proof that the macro
evaluates them. The resolver publishes no link from this fallback. Inherent
methods competing through closed instantiated self types compare their written arguments
with the receiver before choosing an overload or inferring its return type;
an undecidable comparison records `inherent_self_type_not_proven`. This keeps
newly reachable workspace dependencies from selecting getters for builder types.

The resolver includes `internal/cbm/lsp/rust_std_methods.h` at compile time.
Each row has its receiver, method, inference knowledge, signature source URL and
2026-10-07 capture date. Its initial entries are copied from the previous return,
closure-parameter, producer and legacy collection/iterator rules. The legacy
substring matching and approximate returns are preserved here, not certified as
correct std signatures. This task adds no std method knowledge and no new CALLS
publication through the table. Future updates should start from the ranked gaps,
check the cited signature, add a real indexing regression, then update the dated
row. The table is part of the binary; deployment needs no adjacent data file.

Rank the twenty largest std dispatch gaps in any graph with:

```sh
sqlite3 -header -tabs 'file:/path/to/graph.db?mode=ro' \
  < tools/rust-audit/std-method-gaps.sql
```

The query groups by receiver head and method, counts call occurrences rather than
caller–callee pairs, and uses deterministic tie ordering. An empty result is an
empty disclosure for this reason, not proof of complete std coverage.
