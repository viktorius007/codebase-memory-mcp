#!/usr/bin/env python3
"""Score graph Rust CALLS edges and call-occurrence recall against SCIP."""

import argparse
from collections import Counter, defaultdict
import sys

from scip_rust_sites import (
    format_completeness,
    NON_INTERNAL_TRUTH,
    recall_relation_key,
    score_calls,
    write_score_details,
)


def print_precision(report):
    counts = Counter(edge.outcome for edge in report.edges)
    wrong_occurrences = sum(
        edge.outcome == "wrong" for edge in report.occurrences
    )
    judged = counts["correct"] + counts["wrong"]
    precision = counts["correct"] / judged * 100 if judged else 0.0
    print(
        f"published_edges={len(report.edges)} correct={counts['correct']} "
        f"wrong={counts['wrong']} unscorable={counts['unscorable']} "
        f"precision={precision:.2f}% wrong_occurrences={wrong_occurrences} "
        f"integrity_errors={len(report.integrity)} "
        f"non_rust_target_edges={report.non_rust_target_edges}"
    )
    print("=== precision by resolution strategy and call form ===")
    breakdown = defaultdict(Counter)
    for edge in report.edges:
        breakdown[(edge.strategy, edge.call_form)][edge.outcome] += 1
        if edge.outcome == "unscorable":
            breakdown[(edge.strategy, edge.call_form)][edge.reason] += 1
    for (strategy, call_form), values in sorted(breakdown.items()):
        judged = values["correct"] + values["wrong"]
        value = values["correct"] / judged * 100 if judged else 0.0
        print(
            f"strategy={strategy} call_form={call_form} "
            f"correct={values['correct']} wrong={values['wrong']} "
            f"unscorable={values['unscorable']} precision={value:.2f}%"
        )
        for reason, count in sorted(values.items()):
            if reason not in ("correct", "wrong", "unscorable"):
                print(f"  unscorable_reason={reason} count={count}")
    categories = Counter(
        edge.reason for edge in report.edges if edge.outcome == "unscorable"
    )
    print("=== unscorable categories ===")
    for category, count in sorted(categories.items()):
        print(f"unscorable_category={category} count={count}")
    print("=== published edge label categories ===")
    label_categories = Counter(
        edge.reason for edge in report.edges if edge.reason != "-"
    )
    for category, count in sorted(label_categories.items()):
        print(f"label_category={category} count={count}")


def print_integrity(report):
    print("=== integrity errors ===")
    for category, count in sorted(
        Counter(error.category for error in report.integrity).items()
    ):
        print(f"integrity_error={category} count={count}")


def print_recall(report):
    counts = Counter(occurrence.outcome for occurrence in report.recall)
    total = len(report.recall)
    value = counts["found"] / total * 100 if total else 0.0
    relations = {
        recall_relation_key(occurrence): occurrence.outcome
        for occurrence in report.recall
        if occurrence.oracle_caller_symbol is not None
    }
    relation_counts = Counter(relations.values())
    relation_total = len(relations)
    relation_value = (
        relation_counts["found"] / relation_total * 100 if relation_total else 0.0
    )
    print("=== recall by resolution strategy and call form ===")
    breakdown = Counter(
        (occurrence.strategy, occurrence.call_form, occurrence.outcome)
        for occurrence in report.recall
    )
    for key, count in sorted(breakdown.items()):
        strategy, call_form, outcome = key
        print(
            f"strategy={strategy} call_form={call_form} "
            f"outcome={outcome} count={count}"
        )
    caller_categories = Counter(
        (occurrence.caller_category, occurrence.outcome)
        for occurrence in report.recall
    )
    for (category, outcome), count in sorted(caller_categories.items()):
        print(
            f"caller_category={category} outcome={outcome} count={count}"
        )
    print(
        f"truth relations {relation_total} found correct "
        f"{relation_counts['found']} recall={relation_value:.2f}%"
    )
    print(
        f"truth sites {total} found correct {counts['found']} recall={value:.2f}%"
    )


def print_non_internal_inventory(report):
    counts = Counter(
        (site.resolved_symbol, site.call_form)
        for site in report.inventory
        if site.resolved_symbol in NON_INTERNAL_TRUTH
    )
    print("=== oracle-resolved non-internal and ambiguous occurrences ===")
    for (truth_class, call_form), count in sorted(counts.items()):
        print(f"truth={truth_class} call_form={call_form} count={count}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--dump", required=True)
    parser.add_argument("--db", required=True)
    parser.add_argument("--scip", required=True)
    parser.add_argument("--out", help="directory for wrong/missed TSV detail")
    args = parser.parse_args()

    report = score_calls(args.corpus, args.scip, args.db, args.dump)
    if args.out:
        write_score_details(args.out, report)
    print_precision(report)
    print_integrity(report)
    print_recall(report)
    print_non_internal_inventory(report)
    print(format_completeness(report.completeness))
    return 1 if report.integrity else 0


if __name__ == "__main__":
    sys.exit(main())
