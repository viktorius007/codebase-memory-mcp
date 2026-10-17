STATUS: DONE
COMMIT: 9fca4abc6d87ca9da724b72869a16b9da953a7b8
CHANGED: 7 files, diff against 8109dd1d
OPEN: 41 unresolved items
BLOCKERS: none
GATE: PASS
SCORE-codex: calls 226152/229961 flagged 1  (base 225798/229961 flagged 1)
SCORE-ruff: calls 103878/112300 flagged 1  (base 103252/112300 flagged 1)
SCORE-pm: calls 39983/47116 flagged 73  (base 39680/47116 flagged 74)
SCORE-bevy: calls 61267/84295 flagged 41  (base 60018/84295 flagged 76)
DISK: 3.5G /tmp/F1b
WRONG-REMOVED: 9 of 9
REMAINING-EXTERNAL-BOUND: 41 (whole ruff graph)
---

## What changed and why

- [pass_pkgmap.c](/tmp/cbm-m/f1b/src/pipeline/pass_pkgmap.c:1695) and [pipeline_internal.h](/tmp/cbm-m/f1b/src/pipeline/pipeline_internal.h:932): added the shared import-binding proof. A candidate is refused when the local identifier is import-bound but the imported module cannot be matched to a corpus node/file. Synthetic builtins, relative imports, and absolute local Python modules remain eligible.
- [pass_usages.c](/tmp/cbm-m/f1b/src/pipeline/pass_usages.c:659) and [pass_parallel.c](/tmp/cbm-m/f1b/src/pipeline/pass_parallel.c:3819): apply the proof before sequential or parallel TS/TSX USAGE/CALL_REFERENCE publication and record `name_binding_not_proven`.
- [pass_semantic.c](/tmp/cbm-m/f1b/src/pipeline/pass_semantic.c:675) and the parallel semantic twin: apply the same proof to Python/TS base resolution, covering INHERITS or IMPLEMENTS according to the resolved target label.
- [pass_lsp_cross.c](/tmp/cbm-m/f1b/src/pipeline/pass_lsp_cross.c:724): prevent the same external binding from entering the cross-LSP embedded-base surface.
- [test_pipeline.c](/tmp/cbm-m/f1b/tests/test_pipeline.c:4224): added both-mode real-index fixtures for external TSX `Foo`, external Python `Bar`, and a relative TSX control.

No new stop reason was needed; the existing closed-set reason `name_binding_not_proven` describes the refusal.

## Acceptance evidence

1. **Regression tests, red then green**

   - Start SHA `8109dd1d`, isolated no-production-change worktree:
     `CBM_NO_CCACHE=1 ... scripts/test.sh --suites pipeline_unresolved BUILD_DIR=/tmp/F1b/build-red-start`
     → `30 passed, 2 failed`; both external-import tests failed in sequential and parallel modes. Log: [red-start2.log](/tmp/F1b/red-start2.log).
   - Discriminator: the unrelated Python `Bar` is nested under a local class named `pydantic`. That makes the old QN-segment heuristic accept it without making the external package materialize. The new module-file proof rejects it. TSX uses one compatible `Foo` plus a cross-language `Foo`, exercising the single-language recovery. The relative-import control passed at the start SHA.
   - Final focused run:
     `CBM_NO_CCACHE=1 ... scripts/test.sh --suites pipeline_unresolved BUILD_DIR=/tmp/F1b/build-fix`
     → `32 passed`. Log: [target-fix.log](/tmp/F1b/target-fix.log).
   - Test oracles require both zero wrong edges and at least one `name_binding_not_proven` row, preventing an absent-edge-only/vacuous pass.

2. **Full gate and scores**

   - Command:
     `/Users/viktor/Developer/github/codebase-memory-mcp/private/rust-run/merge/gate.sh /tmp/cbm-m/f1b /tmp/F1b`
   - Final result: `GATE: PASS`. [SUMMARY.txt](/tmp/F1b/gate/SUMMARY.txt)
   - Build: PASS, 40s. [build.log](/tmp/F1b/gate/build.log)
   - Required suites: PASS, `1868 passed`, 305s. [suites.log](/tmp/F1b/gate/suites.log)
   - Lint: PASS, 783s. [lint.log](/tmp/F1b/gate/lint.log)
   - All four call scores exactly match the task’s start figures; flagged counts are unchanged.

3. **Ruff precision**

   - Final SQL query for USAGE edges from `playground/ruff/src/Editor/%` to `playground/ty/src/Editor/Diagnostics.tsx::{Diagnostic,DiagnosticLocation}` returned `0`.
   - `unresolved_sites` contains exactly nine `Diagnostic` rows with `name_binding_not_proven`, one for every named wrong site.
   - It additionally contains two `DiagnosticLocation` stops from the same external `ruff_wasm` binding. These were pre-existing single-candidate-path false links covered by the outcome.
   - Exact no-cache start-to-final edge diff:
     `edgediff.py /tmp/F1b/start-score-ruff/cache/score.db /tmp/F1b/gate/score-ruff/cache/score.db /tmp/F1b/start-final-diff-ruff`
     → `USAGE removed=11 added=0`. The removals are the nine required `Diagnostic` pairs plus the two `DiagnosticLocation` pairs; no other start edge changed. Thus the coordinator-classified 62 valid builtin/local/relative recoveries remain.
   - Lists: [removed.tsv](/tmp/F1b/start-final-diff-ruff/removed.tsv), [added.tsv](/tmp/F1b/start-final-diff-ruff/added.tsv), [summary log](/tmp/F1b/start-final-edgediff-final.log).
   - The final stop rows include:
     `Diagnostics.tsx:14,65`; `Editor.tsx:40`; `SourceEditor.tsx:43,127,181,210,225,271`, plus `DiagnosticLocation` at `Diagnostics.tsx:152` and `SourceEditor.tsx:251`.

4. **Scope and commit**

   - `git diff --name-only 8109dd1d..9fca4abc` reports only six TS/Python/shared pipeline files and `tests/test_pipeline.c`.
   - No `internal/cbm/lsp/rust_*` file changed.
   - Branch is `integ/w1`, clean, with commit subject `Withhold names bound to external imports`.

## Edge-set differences against frozen base

Exact per-type files and full lists:

- Codex: base 927930, final 929198; removed 8097, added 9365. CALLS −261/+523; CALL_REFERENCE −2/+52; CONFIGURES −36/+36; DECORATES −101/+101; DEFINES −6312/+6312; DEFINES_METHOD −118/+172; HANDLES −10/+10; IMPLEMENTS −46/+47; IMPORTS −0/+605; SEMANTICALLY_RELATED −18/+14; SIMILAR_TO −6/+6; TESTS −42/+159; USAGE −1092/+1267; WRITES −53/+61. [types](/tmp/F1b/gate/edges-codex.txt), [removed](/tmp/F1b/gate/edges-codex/removed.tsv), [added](/tmp/F1b/gate/edges-codex/added.tsv).
- Ruff: base 412726, final 408444; removed 15075, added 10793. CALLS −1775/+1843; CALL_REFERENCE −2/+2; CONFIGURES −709/+709; DECORATES −197/+197; DEFINES −4000/+4000; DEFINES_METHOD −130/+134; IMPLEMENTS −17/+17; IMPORTS −260/+1440; INHERITS −13/+30; MENTIONS −4/+4; SEMANTICALLY_RELATED −27/+12; SIMILAR_TO −11/+11; TESTS −39/+100; THROWS −25/+0; USAGE −6951/+2279; WRITES −915/+15. [types](/tmp/F1b/gate/edges-ruff.txt), [removed](/tmp/F1b/gate/edges-ruff/removed.tsv), [added](/tmp/F1b/gate/edges-ruff/added.tsv).
- PM: base 161478, final 161623; removed 3232, added 3377. CALLS −228/+374; CALL_REFERENCE −1/+4; CONFIGURES −27/+27; DECORATES −4/+4; DEFINES −2489/+2489; DEFINES_METHOD −4/+11; IMPORTS −38/+187; SEMANTICALLY_RELATED −55/+12; TESTS −8/+87; USAGE −343/+173; WRITES −35/+9. [types](/tmp/F1b/gate/edges-pm.txt), [removed](/tmp/F1b/gate/edges-pm/removed.tsv), [added](/tmp/F1b/gate/edges-pm/added.tsv).
- Bevy: base 306218, final 307642; removed 14727, added 16151. CALLS −655/+1396; CALL_REFERENCE −0/+22; CONFIGURES −280/+280; DECORATES −1388/+1388; DEFINES −6808/+6808; DEFINES_METHOD −186/+238; IMPLEMENTS −46/+46; IMPORTS −1409/+1301; MENTIONS −2/+2; SEMANTICALLY_RELATED −31/+38; SIMILAR_TO −48/+48; TESTS −78/+655; USAGE −3785/+3919; WRITES −11/+10. [types](/tmp/F1b/gate/edges-bevy.txt), [removed](/tmp/F1b/gate/edges-bevy/removed.tsv), [added](/tmp/F1b/gate/edges-bevy/added.tsv).

The task-attributable final-vs-start class is only the 11 ruff USAGE removals described above. There were no final-vs-start additions.

## Ruled out and assumptions

- Ruled out treating any `IMPORTS` edge as proof that a module exists: name-fallback IMPORTS edges can themselves be wrong.
- Ruled out applying a file-wide Python import gate to every Python USAGE. Raw usage edges do not retain enough occurrence scope to distinguish module imports from same-spelled identifiers in nested scopes. Those paths are reported as OPEN rather than silently removed.
- Synthetic targets such as `<python-builtins>` are excluded because they are not repository definitions that merely share a name.
- Assumption for the whole-graph audit: module-scope imports are paired with USAGE `callee` roots; scoped imports are excluded because persisted USAGE edges contain no occurrence span. Relative TS/Python paths, Python module suffixes, workspace `package.json` names, and synthetic targets are accounted for by [count_external_bound.py](/tmp/F1b/count_external_bound.py).

## OPEN items

The whole-final-ruff audit reports 41 remaining pairs, all Python USAGE paths outside the TSX-USAGE/Python-INHERITS single-candidate recovery changed here. The exhaustive file:line/source/target list is [ruff-external-bound-final.txt](/tmp/F1b/ruff-external-bound-final.txt).

Representative examples:

- `crates/ruff_linter/resources/test/fixtures/airflow/AIR003_dag_decorator.py:1`: external `airflow.decorators.dag` is linked to the unrelated fixture `AIR201.dag`.
- `crates/ruff_linter/resources/test/fixtures/flake8_django/DJ001.py:2`: external `django.db.models` is linked to a formatter fixture’s `models`.
- `scripts/generate_mkdocs.py:11`: external `itertools.chain` is linked to `RUF005.chain`.

How seen: the audit parsed module-scope imports, proved their specifiers had no matching corpus module/package file, excluded synthetic targets, and joined the imported local name to persisted USAGE/INHERITS pairs.

## Not checked

- jmap and jmapc were unavailable, as stated in the brief.
- Mutation testing was not requested or run.
- The installed codebase-memory daemon/index was not used as evidence because the task forbids touching it and states that its older binary is not evidence for this tree.
- No independent sub-agent verification was possible because the task explicitly required single-threaded work with no sub-agents.