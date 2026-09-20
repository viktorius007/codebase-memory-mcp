#!/usr/bin/env python3
"""Join production-graph Rust edges against a rust-analyzer SCIP oracle.

Consumes an already-built graph project and an already-generated SCIP index of
the same corpus, classifies every Rust CALLS edge, and gates the result on an
expectation file. The SCIP derivation (protobuf decode, definition intervals,
innermost attribution, descriptor predicates) is imported unchanged from
scripts/scip-call-edges.py, the committed oracle.

Edge classes, keyed on (caller file, caller short name, callee short name):

- fabricated (must-not-exist, unconditional): graph CALLS edge whose key has no
  attributed reference anywhere in the SCIP index. Any count above zero rejects
  the run regardless of the expectation file, because such an edge can only
  come from name guessing or emission bypasses, never from source.
- matched (must-match): graph CALLS edge present in the oracle's strict
  same-file free-function call set -- the subset the graph documents as
  supported. The expectation pins the exact count.
- graph_only_referenced (may-exceed): graph CALLS edge outside the strict set
  but attested by an oracle reference (method calls, cross-file calls,
  function-value references). Allowed because SCIP's "()." descriptor also
  matches non-call references; bounded by the expectation.
- oracle_only: strict-set oracle edges absent from the graph. The expectation
  pins the exact count so coverage loss fails loudly.

IMPORTS edges are counted (not oracle-joined) and pinned by the expectation.

Prints one counts line to stdout, then one "violation: ..." line per rejected
constraint. Edge-level detail lands in --detail-dir as sorted TSV files.
Exit 0 accept, 1 reject, 2 harness error.
"""

import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys

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


def short_name(decoder, symbol):
    descriptor = decoder.descriptor_of(symbol)
    if descriptor.endswith("()."):
        descriptor = descriptor[:-3]
    identifiers = re.findall(r"[A-Za-z0-9_]+", descriptor)
    return identifiers[-1] if identifiers else None


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
                raise HarnessError(f"expectation {where}: expected key=value, got {line!r}")
            if key not in EXPECTED_KEYS:
                raise HarnessError(
                    f"expectation {where}: unknown key {key!r}, expected one of {EXPECTED_KEYS}"
                )
            if key in values:
                raise HarnessError(f"expectation {where}: duplicate key {key!r}")
            if not re.fullmatch(r"[0-9]+", value):
                raise HarnessError(
                    f"expectation {where}: key {key!r} given {value!r}, expected a non-negative integer"
                )
            values[key] = int(value)
    missing = [key for key in EXPECTED_KEYS if key not in values]
    if missing:
        raise HarnessError(f"expectation {path}: missing keys {missing}")
    return values


def graph_edges(cbm, project, edge_type):
    query = (
        f"MATCH (a)-[:{edge_type}]->(b) "
        "RETURN a.file_path AS af, a.name AS an, b.name AS bn LIMIT 1000000"
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


def oracle_sets(decoder, scip_path, corpus, cargo):
    _, definitions, references = decoder.parse(scip_path)
    edges = set()
    for path, refs in references.items():
        intervals = definitions[path]
        for rng, symbol in refs:
            caller = decoder.innermost(intervals, (rng[0], rng[1]))
            if caller and caller != symbol:
                edges.add((caller, symbol, path))

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

    def is_free_function(symbol):
        descriptor = decoder.descriptor_of(symbol)
        return descriptor.endswith("().") and "#" not in descriptor

    referenced, strict = set(), set()
    for caller, target, path in edges:
        caller_name = short_name(decoder, caller)
        target_name = short_name(decoder, target)
        if caller_name is None or target_name is None:
            continue
        referenced.add((path, caller_name, target_name))
        if (
            is_free_function(caller)
            and is_free_function(target)
            and decoder.crate_of(caller) in workspace
            and decoder.crate_of(target) in workspace
            and defining_file.get(caller) == path
            and defining_file.get(target) == path
        ):
            strict.add((path, caller_name, target_name))
    return referenced, strict


def normalize_calls(rows, corpus):
    prefix = os.path.abspath(corpus) + os.sep
    edges = set()
    for file_path, caller, callee in rows:
        if file_path.startswith(prefix):
            file_path = file_path[len(prefix):]
        if file_path.endswith(".rs"):
            edges.add((file_path, caller, callee))
    return edges


def write_detail(detail_dir, name, edges):
    with open(os.path.join(detail_dir, name), "w", encoding="utf-8") as handle:
        for edge in sorted(edges):
            handle.write("\t".join(edge) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cbm", required=True, help="production binary")
    parser.add_argument("--project", required=True, help="indexed project name")
    parser.add_argument("--corpus", required=True, help="corpus root the graph and SCIP index describe")
    parser.add_argument("--scip", required=True, help="rust-analyzer SCIP index file")
    parser.add_argument("--cargo", required=True, help="cargo binary for workspace identity")
    parser.add_argument("--expected", required=True, help="expectation file")
    parser.add_argument("--detail-dir", required=True, help="directory for edge-level TSV detail")
    args = parser.parse_args()

    expected = load_expectation(args.expected)
    decoder = load_decoder()
    referenced, strict = oracle_sets(decoder, args.scip, args.corpus, args.cargo)
    calls = normalize_calls(graph_edges(args.cbm, args.project, "CALLS"), args.corpus)
    imports = sorted(set(graph_edges(args.cbm, args.project, "IMPORTS")))

    matched = calls & strict
    graph_only = calls - strict
    graph_only_referenced = graph_only & referenced
    fabricated = graph_only - referenced
    oracle_only = strict - calls

    write_detail(args.detail_dir, "calls-matched.tsv", matched)
    write_detail(args.detail_dir, "calls-graph-only-referenced.tsv", graph_only_referenced)
    write_detail(args.detail_dir, "calls-fabricated.tsv", fabricated)
    write_detail(args.detail_dir, "calls-oracle-only.tsv", oracle_only)
    write_detail(args.detail_dir, "imports.tsv", imports)

    print(
        f"calls_graph={len(calls)} matched={len(matched)} "
        f"graph_only_referenced={len(graph_only_referenced)} "
        f"fabricated={len(fabricated)} oracle_strict={len(strict)} "
        f"oracle_only={len(oracle_only)} imports={len(imports)}"
    )
    violations = []
    if fabricated:
        violations.append(
            f"violation: fabricated expected=0 got={len(fabricated)} detail=calls-fabricated.tsv"
        )
    for label, count, key, detail in (
        ("matched", len(matched), "calls_matched", "calls-matched.tsv"),
        ("graph_only_referenced", len(graph_only_referenced),
         "calls_graph_only_referenced", "calls-graph-only-referenced.tsv"),
        ("oracle_only", len(oracle_only), "calls_oracle_only", "calls-oracle-only.tsv"),
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
    except HarnessError as error:
        print(error, file=sys.stderr)
        sys.exit(2)
