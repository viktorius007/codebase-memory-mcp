STATUS: DONE
COMMIT: 67af4776
CHANGED: 19 files
CLOSED: round-2 findings A (closes 4), B and C; round-1 findings 1, 2, 3, 5, 6 (closed in round 2)
OPEN: 5
BLOCKERS: none
SCORES: pm 42237/47116 flagged 73 (base 39983/47116 flagged 73); bevy 61392/84295 flagged 35 (base 61368/84295 flagged 41); codex 226167/229953 flagged 1 (base 226161/229953 flagged 1); ruff 103794/112300 flagged 1 (base 104067/112300 flagged 1)
---
# lane/rs-macro, round 2

Tip bf0f65f3 is on ddc8c9b0. Main has since moved to 028c7fb1 (the lane/rs-recv merge); I have not rebased past the cut-off, so a rebase is needed before merge.

## Round-2 commits (on 5d512b29)
1. **5ee9c426**, finding A (the structural form; closes 4).
   - Hygiene is now checked on the parsed expansion instead of by the textual scan.
   - A captured fragment is a node lying entirely in a mapped call-site window.
   - Transcriber binders are the identifiers of transcriber-written patterns, with these scopes:
     - `let`: the following statements of its block;
     - match arm: its guard and value;
     - closure parameters: the body;
     - `for`: the body;
     - `if let` / `while let` / let chains: the whole construct;
     - items in a block, and pattern-taking macros (matches!, assert_matches!, select!): binders of unknown names.
   - Fixture maclit gains struct_let, struct_arm and scrutinee. The previous tip had WRONG struct_let/struct_arm -> B.go; both are now withheld, and scrutinee resolves.
2. **9b88aec8**, finding C: a numeric `literal` is exactly one Rust number token. `1..2` is a range, and a fractional part needs a following digit.
   - Fixture: maclit span!/spanned. Previous tip: PHANTOM spanned -> four. Now 0 mismatch.
3. **57a9ff67**, finding B: a dependency key names the head only when the invoking file binds no such name. Any of these withholds:
   - a source `use` with that local name;
   - an item declared with that name anywhere in the file;
   - for a path written outside a `use`: any glob, or any item-position macro.
   - In `use` paths, E0659 rules out an ambiguous head (2018+ editions). The local module is not resolved; the call is withheld.
   - Fixture: macpath shadow.rs. Previous tip: PHANTOM shadow::run -> nine. Now withheld.
4. **bf0f65f3**, refinement of A, the walker variant of the rule.
   - The expansion is withheld only when an identifier inside a captured fragment equals a transcriber binder's name in scope. Then no captured expression is ever resolved with a transcriber binder.
   - Items and pattern macros still count as "any name".
   - Why: the first form withheld every named capture under any binder, which took codex to 226013 (below its base of 226161). This refinement restores codex to 226167.

## Scores and edge diff (tip, one corpus per run)
| corpus | calls ALL | wrong-or-unconfirmed | CALLS removed vs frozen base | CALLS added |
|---|---|---|---|---|
| pm | 42237/47116 | 73 | 0 | 1604 |
| bevy | 61392/84295 | 35 | 0 | 2 |
| codex | 226167/229953 | 1 | 7 | 5 |
| ruff | 103794/112300 | 1 | 329 | 115 |

- **codex**: the 7 removed pairs are main's, as before (none is this lane's).
- **ruff: credited is 273 below base. The gate will WARN.**
  - Of the 329 removed pairs, 1 is main's Python-fixture pair. The other 328 are expansions now withheld by the hygiene rule (A) where a captured fragment names an identifier the transcriber also binds, or where a block declares an item.
  - On the previous tip ruff was 104329.
  - I have not verified the pairs one by one. They are withheld (precision kept, flagged unchanged at 1), not wrong.
- **pm**: 42237, down from 43713 on the previous tip. That is the same hygiene withholding plus finding B's lexical-binding check; I did not have time to break the 1476 down. Flagged is unchanged at 73.

## Suites, fixtures, build
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/rs-macro` rc=0, 0 warnings.
- Fixtures: `fixtures: 52 fixtures x 2 modes, 93 known mismatches, 0 failures` (macpath 7/7; maclit 6 right, 3 withheld, 0 mismatch).
- Suites:
  - rust_lsp on the tip: `836 passed, 0 failed`.
  - The six-suite run started on the tip at 06:29 and was still running at the cut-off. Its result will be in /scratch/agent/tmp/rsm/suites3.log; it has not been read.
  - The previous full run (5977b7cf) had 0 assertion failures apart from the test fixed in 5d512b29, plus the known pre-existing LeakSanitizer exits in pipeline and edge_structural.

## Open (5)
1. **D**: `let a = 0; $e` with `m!(a::b())` stays withheld, because path segments are compared to binder names. Accepted as open.
2. **ruff** credited is below base (hygiene withholding, see above). pm fell 1476 against the previous tip and is not yet broken down.
3. **Rebase** onto main 028c7fb1 is still to do.
4. **M1** (super-glob) is on branch lane/rs-macro-m1-wip (837752bc). It predates findings 2 and 5 and needs rebasing.
5. **X0**, items defined inside macro bodies: not addressed.

## Rebase onto main 028c7fb1
- `git rebase main` applied all 16 lane commits with no conflict. The new tip was 7af9564e (formerly bf0f65f3).
- Build: rc=0, 0 warnings.
- Fixtures: `fixtures: 54 fixtures x 2 modes, 93 known mismatches, 0 failures`.
- Suites rust_lsp + pipeline: `1378 passed, 0 failed, 0 skipped (2 selected suites)`. pipeline exits rc=1 on the known pre-existing LeakSanitizer condition with 0 failed assertions.
- The six-suite run on bf0f65f3, before the rebase, finished: `1962 passed, 0 failed, 0 skipped`, with the same LeakSanitizer exits.

## Round 3 (one commit): 040a9e27, on 7af9564e
1. **Numeric literal (was a wrong link).** A float takes a trailing `.` when the next character is not `.`, `_` or a letter; `1.` takes no suffix. An exponent may hold underscores after its sign (`1e+_2`).
   - Fixture: maclit trailing_dot. Previous tip: PHANTOM trailing_dot -> four. Now 0 mismatch.
2. **Reverse hygiene (was a wrong link).** Any captured fragment that holds a name and stands in a binding position (let, closure parameter, match-arm, `for` or `if let` pattern) withholds the expansion. Fixture: maclit rebound, now withheld.
3. **Unbounded recursion.** Every hygiene traversal is capped at 64 levels (RUST_HYGIENE_MAX_DEPTH); a deeper expansion is withheld.
4. **`if let` scope (was a withheld link).** The `let` binders now reach the condition and the consequence or body, not `else`. Fixture: maclit else_branch now resolves to A.go.

Checks on 040a9e27:
- Build: rc=0, 0 warnings.
- Fixtures: `fixtures: 54 fixtures x 2 modes, 93 known mismatches, 0 failures`. maclit: 7 right, 4 withheld, 0 mismatch in both modes.
- Suites rust_lsp + pipeline: `1378 passed, 0 failed, 0 skipped`. pipeline exits rc=1 on LeakSanitizer only, the known pre-existing condition.
- pm: `calls ALL 42237 / 47116`, flagged 73 (base 39983, flagged 73). Edge diff against the frozen base: `CALLS removed=0 added=1604`.

bevy, codex and ruff were not re-scored after the rebase or after round 3. The ruff figure in the header (103794, below base 104067) is from bf0f65f3.

## Round 4 (one commit): 67af4776, on 040a9e27
1. **Trailing-dot test.** It now treats any byte 0x80 or above after the dot as an identifier start, so the dot in `1.µ()` stays outside the number.
2. **Captured struct patterns.** rust_node_names now counts shorthand_field_identifier as a name. A captured `W { v }` pattern or `let W { $i } = ..` now falls under the captured-binder withhold rule.

Checks on 67af4776:
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/rs-macro` rc=0, 0 warnings.
- Fixtures: `make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/rs-macro`: `fixtures: 54 fixtures x 2 modes, 93 known mismatches, 0 failures`.
- No fixture was added for either shape, so neither fix was seen failing before the change.
- Suites and scores were not re-run in this round: the gate runs in /scratch/agent/gate3 and build/t-rs-macro, which I did not touch.
