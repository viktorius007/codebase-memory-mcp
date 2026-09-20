# Language benchmark status and grading

No language-quality score table has been revalidated against the current source
tree. Historical grades, corpus counts and per-run transcripts live in Git;
they are not current capability guarantees. Parser availability does not imply
complete semantic relationships.

For Rust, consult the [supported subset and remaining work](RUST_CODEGRAPH_TRUTH_PLAN.md)
and [regression verification](RUST_REPAIR_EVIDENCE.md). A passing repository test
gate is not a full-language benchmark, full-corpus parity result, or warm-update
latency measurement.

## Grading

For a compact source-verified evaluation, grade each applicable question as
PASS (1.0), PARTIAL (0.5), or FAIL (0.0). Exclude genuinely inapplicable questions
from the denominator; missing supported behavior is a failure or partial result,
not N/A. Record exact targets and missed or false relationships, not just whether
a query returned rows.

Cover definition discovery, targeted retrieval, text search, relationships and
architecture where applicable. Include both positive targets and same-name
decoys; report incomplete coverage alongside zero-result answers.

## Reproduction and publication

Use [Measuring quality, latency, and agent savings](MEASURING_SAVINGS.md) for
frozen inputs, independent answer checks, isolated conditions and measurement.
The [evaluation plan](EVALUATION_PLAN.md) supplies a larger draft question cohort;
its corpus assignments and symbol examples require validation before execution.

Publish the tested CBM commit, corpus commits, index version and mode, platform,
question set, raw evidence, exclusions and failures with any new scores. Keep
answer quality, indexing/query latency, and model-token savings separate. Replace
this status with results only when those artifacts support the current claims.
