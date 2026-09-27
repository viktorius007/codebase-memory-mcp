#!/usr/bin/env python3
"""Gate graph Rust CALLS edges against exact SCIP occurrence truth."""

import argparse
from collections import Counter
import importlib.util
import json
import os
import re
import subprocess
import sys

from scip_rust_sites import (
    AMBIGUOUS,
    EXTERNAL,
    LOCAL,
    score_calls,
    write_score_details,
)


EXPECTED_KEYS = (
    "calls_matched",
    "calls_graph_only_referenced",
    "calls_oracle_only",
    "imports",
)
PAGE_ROWS = 500


class HarnessError(Exception):
    """A defect of the run's inputs or tools, distinct from a FAIL verdict."""


def load_decoder():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scip-call-edges.py")
    spec = importlib.util.spec_from_file_location("scip_call_edges", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_expectation(path):
    values = {}
    with open(path, encoding="utf-8") as handle:
        for line_number, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            key, separator, value = line.partition("=")
            where = f"{path}:{line_number}"
            if not separator:
                raise HarnessError(
                    f"expectation {where}: expected key=value, got {line!r}"
                )
            if key not in EXPECTED_KEYS:
                raise HarnessError(
                    f"expectation {where}: unknown key {key!r}, "
                    f"expected one of {EXPECTED_KEYS}"
                )
            if key in values:
                raise HarnessError(f"expectation {where}: duplicate key {key!r}")
            if not re.fullmatch(r"[0-9]+", value):
                raise HarnessError(
                    f"expectation {where}: key {key!r} given {value!r}, "
                    "expected a non-negative integer"
                )
            values[key] = int(value)
    missing = [key for key in EXPECTED_KEYS if key not in values]
    if missing:
        raise HarnessError(f"expectation {path}: missing keys {missing}")
    return values


def graph_edges(cbm, project, edge_type):
    query = (
        f"MATCH (a)-[:{edge_type}]->(b) "
        "RETURN a.file_path AS af, a.start_line AS asl, a.end_line AS ael, "
        "a.qualified_name AS aqn, b.file_path AS bf, b.start_line AS bsl, "
        "b.end_line AS bel, b.qualified_name AS bqn LIMIT 1000000"
    )
    rows, offset = [], 0
    while True:
        command = [
            cbm, "cli", "query_graph", "--project", project, "--query", query,
            "--format", "json", "--max-rows", str(PAGE_ROWS),
            "--offset", str(offset), "--max-output-tokens", "60000",
        ]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            raise HarnessError(
                f"query_graph {edge_type} failed (exit {result.returncode}): "
                f"{result.stderr.strip() or result.stdout.strip()}"
            )
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise HarnessError(
                f"query_graph {edge_type} returned unparseable JSON: {error}: "
                f"{result.stdout[:300]}"
            ) from error
        page = payload.get("rows", [])
        rows.extend(tuple(row) for row in page)
        if not payload.get("has_more") or not page:
            return rows
        offset += len(page)


def strict_call_pairs(decoder, scip_path, corpus, cargo):
    _, definitions, references, _ = decoder.parse_with_definition_sites(scip_path)
    result = subprocess.run(
        [cargo, "metadata", "--no-deps", "--format-version", "1"],
        cwd=corpus, capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise HarnessError(
            f"cargo metadata failed in {corpus} (exit {result.returncode}): "
            f"{result.stderr.strip()[:300]}"
        )
    metadata = json.loads(result.stdout)
    workspace = {package["name"] for package in metadata["packages"]}
    defining_file = {}
    for path, entries in definitions.items():
        for _, symbol in entries:
            defining_file.setdefault(symbol, path)
    strict = set()
    for path, occurrences in references.items():
        intervals = definitions[path]
        for source_range, target in occurrences:
            caller = decoder.innermost(
                intervals, (source_range[0], source_range[1])
            )
            caller_descriptor = decoder.descriptor_of(caller or "")
            target_descriptor = decoder.descriptor_of(target)
            if (
                caller
                and caller != target
                and caller_descriptor.endswith("().")
                and "#" not in caller_descriptor
                and target_descriptor.endswith("().")
                and "#" not in target_descriptor
                and decoder.crate_of(caller) in workspace
                and decoder.crate_of(target) in workspace
                and defining_file.get(caller) == path
                and defining_file.get(target) == path
            ):
                strict.add((caller, target))
    return strict


def write_detail(detail_dir, name, rows):
    with open(os.path.join(detail_dir, name), "w", encoding="utf-8") as handle:
        for row in sorted(rows):
            handle.write("\t".join(str(value) for value in row) + "\n")


def edge_detail(edge):
    return (
        edge.relative_path, edge.callee_start_byte, edge.callee_end_byte,
        edge.strategy, edge.call_form, edge.caller_symbol or "-",
        edge.oracle_caller_symbol or "-", edge.target_symbol or "-",
        edge.oracle_symbol or "-",
        edge.target_qualified_name, edge.reason,
    )


def recall_detail(occurrence):
    return (
        occurrence.relative_path, occurrence.callee_start_byte,
        occurrence.callee_end_byte, occurrence.strategy,
        occurrence.call_form, occurrence.oracle_symbol,
    )


def print_breakdowns(report):
    precision = Counter(
        (
            edge.strategy, edge.call_form, edge.outcome,
            edge.reason,
        )
        for edge in report.edges
    )
    for (strategy, call_form, outcome, reason), count in sorted(precision.items()):
        print(
            f"precision strategy={strategy} call_form={call_form} "
            f"outcome={outcome} reason={reason} count={count}"
        )
    recall = Counter(
        (item.strategy, item.call_form, item.outcome) for item in report.recall
    )
    for (strategy, call_form, outcome), count in sorted(recall.items()):
        print(
            f"recall strategy={strategy} call_form={call_form} "
            f"outcome={outcome} count={count}"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cbm", required=True, help="production binary")
    parser.add_argument("--project", required=True, help="indexed project name")
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--scip", required=True)
    parser.add_argument("--db", required=True, help="graph SQLite database")
    parser.add_argument("--dump", required=True, help="published Rust call dump")
    parser.add_argument("--cargo", required=True)
    parser.add_argument("--expected", required=True)
    parser.add_argument("--detail-dir", required=True)
    args = parser.parse_args()

    expected = load_expectation(args.expected)
    report = score_calls(args.corpus, args.scip, args.db, args.dump)
    strict = strict_call_pairs(load_decoder(), args.scip, args.corpus, args.cargo)
    correct = [edge for edge in report.edges if edge.outcome == "correct"]
    wrong = [edge for edge in report.edges if edge.outcome == "wrong"]
    unscorable = [edge for edge in report.edges if edge.outcome == "unscorable"]
    wrong_occurrences = sum(
        edge.outcome == "wrong" for edge in report.occurrences
    )
    matched = [
        edge for edge in correct
        if (edge.caller_symbol, edge.oracle_symbol) in strict
    ]
    graph_only = [edge for edge in correct if edge not in matched]
    missed = [item for item in report.recall if item.outcome == "missed"]
    relation_outcomes = {
        (item.oracle_caller_symbol, item.oracle_symbol): item.outcome
        for item in report.recall
        if item.oracle_caller_symbol is not None
    }
    relation_found = sum(
        outcome == "found" for outcome in relation_outcomes.values()
    )
    occurrence_found = len(report.recall) - len(missed)
    relation_recall = (
        relation_found / len(relation_outcomes) * 100 if relation_outcomes else 0.0
    )
    occurrence_recall = (
        occurrence_found / len(report.recall) * 100 if report.recall else 0.0
    )
    imports = sorted(set(graph_edges(args.cbm, args.project, "IMPORTS")))
    inventory = Counter(site.resolved_symbol for site in report.inventory)

    write_score_details(args.detail_dir, report)
    write_detail(args.detail_dir, "calls-matched.tsv", map(edge_detail, matched))
    write_detail(
        args.detail_dir, "calls-graph-only-referenced.tsv",
        map(edge_detail, graph_only),
    )
    write_detail(args.detail_dir, "calls-fabricated.tsv", map(edge_detail, wrong))
    write_detail(args.detail_dir, "calls-unscorable.tsv", map(edge_detail, unscorable))
    write_detail(args.detail_dir, "calls-oracle-only.tsv", map(recall_detail, missed))
    write_detail(args.detail_dir, "imports.tsv", imports)

    print(
        f"calls_graph={len(report.edges)} correct={len(correct)} wrong={len(wrong)} "
        f"unscorable={len(unscorable)} matched={len(matched)} "
        f"graph_only_referenced={len(graph_only)} fabricated={len(wrong)} "
        f"wrong_occurrences={wrong_occurrences} "
        f"non_rust_target_edges={report.non_rust_target_edges} "
        f"integrity_errors={len(report.integrity)} "
        f"oracle_relations={len(relation_outcomes)} "
        f"relations_found={relation_found} relation_recall={relation_recall:.2f}% "
        f"oracle_internal={len(report.recall)} oracle_only={len(missed)} "
        f"occurrences_found={occurrence_found} "
        f"occurrence_recall={occurrence_recall:.2f}% "
        f"oracle_local={inventory[LOCAL]} oracle_external={inventory[EXTERNAL]} "
        f"oracle_ambiguous={inventory[AMBIGUOUS]} imports={len(imports)}"
    )
    print_breakdowns(report)

    violations = []
    if wrong:
        violations.append(
            f"violation: fabricated expected=0 got={len(wrong)} "
            "detail=calls-fabricated.tsv"
        )
    if report.integrity:
        categories = Counter(error.category for error in report.integrity)
        violations.append(
            f"violation: call_integrity expected=0 got={len(report.integrity)} "
            f"categories={dict(sorted(categories.items()))} "
            "detail=integrity-errors.tsv"
        )
    for label, count, key, detail in (
        ("matched", len(matched), "calls_matched", "calls-matched.tsv"),
        (
            "graph_only_referenced", len(graph_only),
            "calls_graph_only_referenced", "calls-graph-only-referenced.tsv",
        ),
        ("oracle_only", len(missed), "calls_oracle_only", "calls-oracle-only.tsv"),
        ("imports", len(imports), "imports", "imports.tsv"),
    ):
        if count != expected[key]:
            violations.append(
                f"violation: {label} expected={expected[key]} got={count} detail={detail}"
            )
    for violation in violations:
        print(violation)
    return 1 if violations else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (HarnessError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(2)
