# Rust graph: verified state and direction

Dated 2026-10-14 at `integ/w1` `f10b3151`, proposed for landing on `main`
`7c95c002`. The 2026-10-07 figures retained below are dated history; they are
not descriptions of the current tree. The complete earlier document remains
in Git at `aa48ba96:docs/RUST_GRAPH_STATUS.md`. Current measurements use the
four available corpora under `~/Developer/cbm-bench/`. The pinned jmap and
jmap-client checkouts are unavailable and are not replaced by different
revisions.

This file is the authority on Rust graph accuracy, its measurement, and the
work that follows. Figures headed 2026-10-14 come from the coordinator gate on
`f10b3151`. Figures headed 2026-10-07 come from the earlier run at `aa48ba96`
or the wave-one base `61b613db` and are retained only as history. A "flagged"
pair is a CALLS pair the scorer classifies as wrong or cannot confirm against
the rust-analyzer truth. It is not a count of pairs read and adjudicated in
source unless the text says so.

## Direction

The owner's rules stand: no rust-analyzer, rustc, cargo, or standard-library
source is needed at index time; a wrong link is worse than a missing one; a
link the resolver cannot prove is withheld and recorded with a stop reason
from the closed set in `src/pipeline/pipeline_internal.h`.

Current measurement and the two dated checkpoints:

| Corpus | 2026-10-14 `f10b3151`: calls credited / truth, flagged | 2026-10-07: calls credited / truth, flagged | On 2026-10-01 |
|---|---|---|---|
| codex-rs | 226,152 / 229,961, 1 | 225,790 / 229,952, 1 | 119,996 (52.2%), 568 |
| ruff (held-out) | 103,883 / 112,300, 1 | 103,252 / 112,301, 1 | 77,202 (68.7%), 296 (measured 6 Oct) |

The other two currently available corpora, with the 7 October checkpoint kept
as history:

| Corpus | 2026-10-14 `f10b3151`: calls credited / truth, flagged | 2026-10-07: calls credited / truth, flagged |
|---|---|---|
| project-management | 39,983 / 47,116, 73 | 39,680 / 47,116, 74 |
| bevy | 61,267 / 84,295, 41 | 60,036 / 84,297, 78 |

The historical review also read selected pairs in source, but that is a
different measurement and is not substituted for the scorer's flagged count.
Jmap and jmap-client are unavailable for the 14 October gate.

The 14 October integration moved against the frozen `main` `7c95c002` base as
follows: codex +354 credited calls with flagged unchanged at 1; ruff +631 with
flagged unchanged at 1; project-management +303 with flagged 74 to 73; bevy
+1,249 with flagged 76 to 41. The gate passed 1,916 tests and lint.

## Measurement setup

The current bench roots are outside every Git tree and are read-only:

| Name | Bench root | Corpus |
|---|---|---|
| codex | `~/Developer/cbm-bench/codex` | `~/Developer/cbm-bench/codex/codex/codex-rs` |
| ruff | `~/Developer/cbm-bench/ruff` | `~/Developer/cbm-bench/ruff/ruff` |
| project-management | `~/Developer/cbm-bench/pm` | `~/Developer/cbm-bench/pm/pm` |
| bevy | `~/Developer/cbm-bench/bevy` | `~/Developer/cbm-bench/bevy/bevy` |

Each bench root contains `truth.tsv` and `refs.tsv`. The truth was generated
from rust-analyzer SCIP data. `tools/rust-bench/fetch-corpora.sh` fetches the
corpora at the commits pinned in `tools/rust-bench/corpora.tsv`,
`gen-truth.sh` regenerates the truth with the pinned rust-analyzer 1.99.0,
`rebuild.sh <base-sha>` runs every step on a new machine, and
`baseline.sh` freezes the base score the gate diffs against. On 17 October
the truth was regenerated with these scripts: the earlier truth came from an
unrecorded older rust-analyzer and could not be reproduced. The codex
denominator moved from 229,961 to 229,953; ruff, pm, and bevy totals are
unchanged. Figures dated before 17 October use the earlier truth; rust-analyzer is an offline oracle, not an index
dependency. A corpus placed under this repository inherits its ignore and Git
history rules: on 14 October that placement lost 103 codex call credits. The
bench roots above avoid that artefact.

The gate compares exact edge sets with frozen `7c95c002` databases, builds the
binary, runs lint, and runs the `rust_lsp`, `pipeline`, `edge_structural`,
`lang_contract`, and `extraction` suites. Its score directories contain
`cache/score.db`; its edge directories contain `removed.tsv` and `added.tsv`.
Credit is per caller-callee pair: a call site is credited when a CALLS edge
joins its caller to the oracle callee. Positions are not stored on edges.

### Measurement history (2026-10-07)

The earlier run used codex-rs as the measurement corpus and ruff as a held-out
corpus. The C resolver reached 225,790 / 229,952 and 103,252 / 112,301 credited
calls respectively. Its review withdrew the earlier claims of zero wrong
links, no USAGE name matching, no cross-language IMPORTS, and complete
unresolved-site accounting. The ten wave-one arms and the 14 October repairs
addressed the named resolver and disclosure defects; remaining work is listed
under "Work, in order".

## Calls

Current aggregate CALLS measurements are the two tables under "Direction".
The frozen base is `main` `7c95c002`: codex 225,798 / 229,961 flagged 1; ruff
103,252 / 112,300 flagged 1; project-management 39,680 / 47,116 flagged 74;
bevy 60,018 / 84,295 flagged 76.

### Call-shape history (2026-10-07)

The 7 October report separated path calls, method calls, and calls inside macro
arguments. That breakdown was not recomputed on `f10b3151`; only the aggregate
current score is claimed here.

## Definitions, owners, implementations

### Historical checkpoint (2026-10-07)

The `aa48ba96` oracle comparison found complete free-function and method
definition coverage on codex and near-complete coverage on ruff, while ruff
constant/static coverage and trait IMPLEMENTS remained materially lower. The
14 October gate does not emit a replacement family table, so no current family
percentages are claimed here.

## Imports and usages

Wave-one arm Q made Rust module IMPORTS depend on proved module identity and
removed generic cross-language admission. Arm P added dotted Cargo dependency
bindings, re-export handling, and macro-discard records. F1 records guarded
non-Rust refusals, and F1b prevents an external import binding from falling
back to an unrelated in-corpus same-name definition. The remaining known
external-binding USAGE class is listed as open work.

### Historical checkpoint (2026-10-07)

The earlier IMPORTS and target-kind USAGE tables were not recomputed on
`f10b3151` and must not be presented as current.

## Unresolved sites

Recorded resolver refusals use the closed reason set in
`src/pipeline/pipeline_internal.h` and are stored in `unresolved_occurrences`;
the `unresolved_sites` view exposes file, line, byte span, enclosing item,
written name, and reason. Records are metadata, not nodes or edges. F1 raised
guard-stop coverage from 298 / 4,800 to 4,759 / 4,759 on its measured start,
and later integration retained those records. The ledger still identifies ten
bevy_reflect trait calls removed without a matching stop record; that is open.

Query one generated graph read-only:

```sh
sqlite3 -header -tabs 'file:/absolute/path/to/score.db?mode=ro' \
  "select kind, reason, count(*) from unresolved_sites group by 1,2 order by 3 desc"
```

The database path is an output parameter created by the gate, not a checked-in
repository path.

### Historical checkpoint (2026-10-07)

The earlier run published stop-reason and record-count tables. Those totals
were not recomputed on `f10b3151`; the current disclosure is the open silent
withhold class above.

## Parser

The vendored Rust grammar remains a self-maintained tree-sitter fork under
`tools/tree-sitter-rust/`, with its manifest entry in
`internal/cbm/vendored/grammars/MANIFEST.md`. Rust truth extraction and parser
audit tools live under `tools/rust-audit/`. Arm T corrected the truth/reference
extractors for shared definitions, enclosing ranges, and path-compiled files;
the current truth totals in the score tables include those changes.

## Oracle cost

The compiler and rust-analyzer are needed only to regenerate truth, never to
index. The 2026-10-07 run measured the oracle separately from indexing. No
current replacement cost measurement was made on 14 October.

## Work, in order

Landed items are struck through. Each measured effect is the arm report's own
standalone result or the final integration gate; effects are not added across
non-independent branches.

1. ~~Mutation and truth harness repairs~~ — arms H and T, integrated by M1.
   H regenerated the Rust mutation seeds and wired the conditional CI lane; T
   corrected truth totals to codex 229,961, ruff 112,300, and bevy 84,295 and
   removed the historical jmap-client false-flag class. The last full-suite
   and mutation-lane run is M6 on `582bc055`. It had not
   emitted a report when this section was written; the latest ledger records
   seven full-suite failures under attribution and no final mutation result,
   so neither lane is claimed complete.
2. ~~Located resolver defects~~ — arms B1, P, Q, and D, integrated by M1 and
   M2. B1 recovered 4 codex and 64 ruff credits on its integration step; P
   retained scores while adding proved Cargo/re-export/macro accounting; Q
   added proved module IMPORTS and conservative ambiguity refusal; D added one
   codex credit and reduced the project-management and bevy flagged counts by
   one and five on the first clean integration gate. The named fixes cover
   `name @ pattern`, generic inherent selection, self-less associated
   functions, dotted dependency fields, cross-language import fallback,
   re-export USAGE, discarded/stringified macro tokens, Deref and reference
   receiver proof, explicit trait paths, and raw pointers.
3. ~~Return, bound, reference, block-identity, and empty-impl repairs~~ — arms
   Y2, Y1, and B2, integrated by M3 and M4. Their cumulative integration moved
   codex to 225,983, ruff to 103,572, project-management to 39,709, and bevy
   to 60,251, with flagged counts 1, 1, 73, and 51.
4. ~~Conservative standard-library collision rule~~ — arm S plus the generated
   inventory at `tools/std-inventory/` and
   `internal/cbm/lsp/rust_std_inventory.h`, integrated by W2b. The inventory
   has 32,123 rows and stamps 111 receiver families complete, 11
   complete-except-Deref, and 4 incomplete. W2b recovered 3,078 codex and 467
   bevy credits from arm S's conservative starting point; the final gate has
   no flagged increase.
5. ~~Guard recording, external-import binding, and reference-receiver ranking~~
   — F1, F1b, and M5c. F1 recorded all 4,759 measured guarded refusals; F1b
   removed 9 of 9 reviewed wrong ruff external-import links while keeping the
   score; M5c added five truth-backed ruff calls and removed none.
6. ~~Second upstream merge~~ — U6 merged `integ/up` `7feef206` (upstream
   `24d02512`) as `582bc055`; Rust-endpoint edge differences were zero and the
   gate passed 1,916 tests before F1b/M5c's final merge.
7. **Open: external-import Python USAGE.** F1b found 41 pre-existing wrong
   ruff USAGE pairs where names bound to external packages fall through to
   same-named fixture definitions. The occurrence scope needed for a safe
   file-wide repair is not retained on those raw edges. Source: the OPEN
   section of
   `private/rust-run/merge/reports/F1b.report.md`.
8. **Open: silent Rust withholds.** Ten bevy_reflect dyn-trait calls are absent
   without a matching stop record after block-key normalization. Source: the
   2026-10-14 18:50 M5 ledger entry in
   `private/rust-run/merge/run-ledger.md`.
9. **Open: inventory completeness.** `tools/std-inventory/generate.py` still
   lacks complete `dyn Any` and `dyn Error` inherent-method data;
   `tools/std-inventory/receivers.json` lacks concrete `Option.Inner` and
   `Option.Item` receivers; declaring-trait provenance and the generic-self
   over-list remain approximate. These are the six OPEN items in
   `private/rust-run/merge/reports/STD3.report.md`.
10. **Open: identity and macro proof.** Block-local node identity landed, but
    some impl-receiver and field-type consumers still use module-scoped
    spellings, and unknown macro arguments can still be admitted by the
    builtin macro policy. Source: the OPEN section of the B2 resume report at
    `private/rust-run/merge/reports/w1-B2.r.report.md`.
11. **Open: the dated queue.** `From` argument proof; EnumVariant, Macro, and
    Module USAGE; ruff impl and constant/static recall; broader registry/FQN
    identity; and invalid UTF-8 in truncated edge properties were not closed
    by a final report and were not remeasured on 14 October.

## Not measured

- The ruff truth beyond scorer-flagged pairs.
- Oracle and graph behavior on Windows or Linux cfg paths; the oracle ran on
  `aarch64-apple-darwin`.
- Current jmap and jmap-client scores; their pinned revisions are unavailable.
- Current per-call-shape, definition-family, IMPORTS-family, and USAGE-family
  tables; the values above are explicitly dated where historical.
- Graph accuracy for languages other than Rust, except the exact non-Rust
  classes adjudicated by F1 and F1b.
- The installed `~/.local/bin/codebase-memory-mcp`, `~/.cbm`, and the running
  daemon; the run never touches them.

## Reproduce

The canonical full reproduction on this machine is:

```sh
tools/rust-bench/gate.sh /absolute/path/to/codebase-memory-mcp /absolute/path/to/scratch
```

Both arguments are parameters. The scratch directory must be private and may
be discarded after reading `gate/SUMMARY.txt`. The gate builds with
`Makefile.cbm`, runs lint and the five suites, scores all four bench roots, and
writes exact per-corpus edge lists beside the scores. The command was run on
`f10b3151`; its summary ended `GATE: PASS`, with 1,916 tests and the four score
rows under "Direction".

To score only one available corpus with an already-built binary:

```sh
CBM_RUST_BENCH="$HOME/Developer/cbm-bench/ruff" \
CBM_RUST_CORPUS="$HOME/Developer/cbm-bench/ruff/ruff" \
scripts/rust-score.sh /absolute/path/to/codebase-memory-mcp /absolute/path/to/output
```

Replace both `ruff` components together with `codex` and
`codex/codex-rs`, `pm` and `pm`, or `bevy` and `bevy`. The absolute binary and
output paths are parameters. jmap and jmap-client cannot be reproduced because
their pinned checkouts are unavailable; no current command is claimed for
them.

The generated standard-method disclosure can be ranked from any gate graph:

```sh
sqlite3 -header -tabs 'file:/absolute/path/to/score.db?mode=ro' \
  < tools/rust-audit/std-method-gaps.sql
```

An empty result means no records with that reason in that graph, not complete
standard-library coverage.

## Repository state (2026-10-15)

- `main` is `59a67044`, pushed to `origin`, and is the installed binary. It holds
  the wave-one integration, the 14 October std-dispatch work, the B1F/B1G method
  selection, the RW3 Rust repair (B2, B3, M1 fixed; B1 one of three sites right,
  two recorded stops), the Python own-name repair (ba953975), M8's mutation
  seeds and tests, and `make -f Makefile.cbm fixtures`.
- Scores on `main` against the 7c95c002 base (calls credited, flagged):
  codex 226161 / 1 (base 225798 / 1), ruff 104067 / 1 (103252 / 1),
  pm 39983 / 73 (39680 / 74), bevy 61367 / 35 (60018 / 76). No CALLS edge
  present on the previous tip was removed on any corpus.
- Open Rust findings are the 93 entries of `tools/fixtures/known.tsv`, each keyed
  to its review finding: R2 (an impl on an intermediate `Deref` step, the only
  class that still publishes a wrong edge), R4 (two traits with different self
  shapes recorded `method_not_found`), the two remaining B1 bound sites, N2 stop
  names, and R-W1 B4–B7. `make fixtures` fails when any entry stops mismatching
  or a new mismatch appears. The mutation lane's one survivor is
  `mutable-value-precedence`.
- `fix/rw3-py` (fce3f3a1) is the unlanded Python statement-order repair: it
  matches Python on 37 of 43 fixture modules in both modes but hangs the ruff
  index in the package re-export lookup (`pxc_python_reexported_node` re-extracts
  package files per lookup). `fix/e61` and `fix/h5h` are superseded alternatives
  to ba953975, kept because the repository hook refuses to delete unmerged
  branches. All wave-one, staging and landed repair branches are deleted; their
  commits are on `main`.
- The gate command is the first command under "Reproduce". Corpora are under
  `~/Developer/cbm-bench/`, outside all Git trees, and must never be edited.
- `rm` is blocked on this machine; use `trash`. Never touch `~/.cbm`, the
  installed binary, or its daemon. Never edit or re-index the corpus trees.

## Trait scope and std dispatch disclosure (2026-10-14)

Concrete dot-call trait candidates require a positive lexical identity proof.
The module tree can prove declarations in the innermost module, named imports,
renames and re-exports, unambiguous globs, supported block imports, prelude
entries allowed by crate attributes and edition, supertraits, and the trait of
the enclosing impl. Comments, strings, unrelated qualified paths, and sibling
imports are no proof. Unsupported macro expansion remains opaque.

When a canonical standard-library receiver has a method name also supplied by
a project trait, the resolver consults the generated inventory in
`internal/cbm/lsp/rust_std_inventory.h`. A listed name, incomplete stamp, or
missing stamp withholds with `unrecognised_std_method`. A name absent under a
complete stamp proves only that the standard library supplies no competing
method; project trait scope, applicable impl, and receiver shape must still be
proved. Complete-except-Deref repeats the collision check on a proved pointee;
an unknown pointee is withheld. The inventory provides negative knowledge
only: it never supplies a target or a positive dispatch proof.

Method selection (B1F, 15 October, superseding M5c's reference-only ranking)
places inherent and trait candidates in one Rust probe order: by-value, shared,
then mutable at each autoderef step, with inherent methods winning only at an
equal rank. The probe order does not consult `Copy`: move legality follows
selection, and a compiling call proves that legality. A same-named impl that is
out of scope or has a provably different receiver shape has no vote; only an
unproved shape that could precede the selected candidate withholds the call. A
missing method-self or impl-self shape withholds the site instead of granting
inherent precedence; this applies to owned, reference, Box, Rc and Arc receivers.
Trait-object methods inherited from supertraits remain eligible.
Unproved reference, mutable, and wrapper dispatch each have their own stop
reason; ambiguity is withheld with a specific closed reason. Raw pointers never autoderef implicitly. Explicit
trait paths, reference layers, generic bounds, inherent self types, and Deref
pointees each retain their separate proof and stop rules.

A candidate leaves the probe order only on a fact the resolver has established the
way rustc does (RW3): an impl's self type resolved through its aliases; a trait in
scope when it is declared in an enclosing block or imported; a derived or otherwise
keyless impl whose declaring type no autoderef step reaches. A bound `T: Tr<A>` on
a blanket impl is proved from the impl headers of every file (self type and trait
arguments both compared); `Copy` is the exact trait, never a name prefix. A bound
or shape that cannot be settled leaves the candidate unknown and the call is
withheld with `trait_receiver_partial`; a bounded blanket impl written in a file the
resolver does not read has unreadable bounds and is withheld the same way.

The inventory is generated by `tools/std-inventory/generate.py`; its developer
oracle in `tools/std-inventory/probe.py` may use rustc, but indexing never does.
Its current 32,123 rows and 111 / 11 / 4 stamp split are source- and
compiler-probed development artifacts compiled into the binary. The six known
inventory limitations remain open above.

## The edit loop

Run `make -f Makefile.cbm fixtures BUILD_DIR=<dir>` after every resolver change: it runs each program under `tests/fixtures/graph` with its own compiler or interpreter, indexes it with the built binary in both pipeline modes, compares the graph's edges and `unresolved_sites` rows with what the run did, and exits non-zero naming every site that differs and is not in `tools/fixtures/known.tsv` (about two minutes). A fix removes its entries from that list in the same commit. Run the coordinator's gate (build, suites, lint, corpus scores) once, before a merge.
