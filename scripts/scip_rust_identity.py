#!/usr/bin/env python3
"""Map Rust graph nodes to exact rust-analyzer SCIP definition identities."""

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import importlib.util
import json
import os
from pathlib import Path
import re
import sqlite3
import sys


SCIP_KINDS_BY_GRAPH_LABEL = {
    "Enum": frozenset((11,)),
    "Field": frozenset((12, 15)),
    "Function": frozenset((17,)),
    "Interface": frozenset((53,)),
    "Method": frozenset((26, 70, 80)),
    "Module": frozenset((29,)),
    "Struct": frozenset((49,)),
    "Type": frozenset((55,)),
    "Variable": frozenset((8, 37, 44, 61, 82)),
}

OUTCOME_STATUSES = (
    "matched",
    "ambiguous",
    "not_in_oracle_configuration",
    "oracle_gap",
)

# This is the closed vocabulary for every outcome without one exact identity.
IDENTITY_REASON_DESCRIPTIONS = {
    "cfg_inactive": "the node has an explicit cfg predicate that is demonstrably false",
    "derive_owner_unmapped": "the derive node's owner has no exact SCIP identity",
    "derive_symbol_missing": "SCIP has no derived-method symbol for the mapped owner",
    "included_source": "the Rust source is consumed through a literal include! macro",
    "invalid_derive_metadata": "derive metadata is present but incomplete or invalid",
    "multiple_compatible_definitions": "more than one SCIP definition matches the node",
    "multiple_compatible_derive_symbols": "more than one derived SCIP symbol matches",
    "name_mismatch": "a compatible definition kind exists in the span under another name",
    "oracle_definition_missing": "the SCIP document has no compatible definition in the span",
    "oracle_document_missing": "SCIP has no definition-bearing document for the source path",
    "proc_macro_output": "an attribute procedural macro owns the emitted definition",
    "unsupported_node_kind": "the graph node kind has no semantic SCIP definition kind",
}
IDENTITY_REASONS = frozenset(IDENTITY_REASON_DESCRIPTIONS)

_INCLUDE_LITERAL = re.compile(r'include!\s*\(\s*(?:r#*)?"([^"]+)"#*\s*\)')
_PROC_MACRO_ATTRIBUTES = ("::test", "#[test_case", "#[rstest", "#[async_trait", "#[instrument")


@dataclass(frozen=True)
class IdentityOutcome:
    """The exact SCIP identity for one graph node, or why none is assigned."""

    status: str
    symbol: str | None = None
    reason: str | None = None
    candidates: tuple[str, ...] = ()
    derive: str | None = None
    owner: str | None = None


def _load_decoder():
    path = Path(__file__).with_name("scip-call-edges.py")
    spec = importlib.util.spec_from_file_location("scip_call_edges", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _project_roots(connection):
    table = connection.execute(
        "select 1 from sqlite_master where type='table' and name='projects'"
    ).fetchone()
    if table is None:
        return {}
    return dict(connection.execute("select name, root_path from projects"))


def _relative_path(file_path, project, roots):
    path = os.path.normpath(file_path)
    root = roots.get(project)
    if root and os.path.isabs(path):
        path = os.path.relpath(path, root)
    return path.replace(os.sep, "/")


def _parse_properties(properties):
    try:
        values = json.loads(properties or "{}")
    except (TypeError, json.JSONDecodeError):
        return {}
    return values if isinstance(values, dict) else {}


def _generated_outcome(node, outcomes_by_qn, oracle_symbols):
    _, _, _, method, _, _, _, _, properties = node
    values = _parse_properties(properties)
    derive = values.get("rust_derive")
    owner = values.get("rust_derive_owner")
    if not isinstance(derive, str) or not derive or not isinstance(owner, str) or not owner:
        return IdentityOutcome(
            status="oracle_gap",
            reason="invalid_derive_metadata",
            derive=derive if isinstance(derive, str) else None,
            owner=owner if isinstance(owner, str) else None,
        )
    owner_outcomes = outcomes_by_qn.get(owner, ())
    if len(owner_outcomes) != 1 or owner_outcomes[0].status != "matched":
        return IdentityOutcome(
            status="oracle_gap",
            reason="derive_owner_unmapped",
            derive=derive,
            owner=owner,
        )
    owner_symbol = owner_outcomes[0].symbol
    parts = owner_symbol.split(" ", 4)
    if len(parts) != 5 or not parts[4].endswith("#"):
        return IdentityOutcome(
            status="oracle_gap",
            reason="derive_owner_unmapped",
            derive=derive,
            owner=owner,
        )
    prefix = " ".join(parts[:4]) + " "
    owner_descriptor = parts[4][:-1]
    module, separator, owner_name = owner_descriptor.rpartition("/")
    descriptor_prefix = f"{module}/impl#[" if separator else "impl#["
    descriptor_suffix = f"][{derive}]{method}()."
    candidates = set()
    for symbol in oracle_symbols:
        if not symbol.startswith(prefix):
            continue
        descriptor = symbol[len(prefix) :]
        if not descriptor.startswith(descriptor_prefix) or not descriptor.endswith(
            descriptor_suffix
        ):
            continue
        receiver = descriptor[len(descriptor_prefix) : -len(descriptor_suffix)]
        receiver_name = receiver.strip("`").split("<", 1)[0]
        if receiver_name == owner_name.strip("`"):
            candidates.add(symbol)
    if len(candidates) == 1:
        return IdentityOutcome(
            status="matched",
            symbol=next(iter(candidates)),
            derive=derive,
            owner=owner,
        )
    ordered = tuple(sorted(candidates))
    return IdentityOutcome(
        status="ambiguous" if ordered else "oracle_gap",
        reason=(
            "multiple_compatible_derive_symbols" if ordered else "derive_symbol_missing"
        ),
        candidates=ordered,
        derive=derive,
        owner=owner,
    )


def _identity_for_node(node, definitions, roots, included_sources, derive_owner):
    node_id, project, label, name, _, file_path, start_line, end_line, properties = node
    del node_id
    path = _relative_path(file_path, project, roots)
    allowed_kinds = SCIP_KINDS_BY_GRAPH_LABEL.get(label, ())
    entries = definitions.get(path, ())
    values = _parse_properties(properties)
    decorators = values.get("decorators", ())
    if not isinstance(decorators, list):
        decorators = ()
    declaration_lines = (
        range(start_line - 1, end_line) if derive_owner else (start_line - 1,)
    )
    if label == "Module":
        candidates = {
            symbol
            for rng, symbol, _, symbol_kind in entries
            if rng[0] == start_line - 1
            and rng[2] == end_line - 1
            and symbol_kind in allowed_kinds
        }
    else:
        candidates = {
            symbol
            for rng, symbol, scip_name, symbol_kind in entries
            if rng[0] in declaration_lines
            and scip_name == name
            and symbol_kind in allowed_kinds
        }
    if len(candidates) == 1:
        return IdentityOutcome(status="matched", symbol=next(iter(candidates)))
    if len(candidates) > 1:
        ordered = tuple(sorted(candidates))
        return IdentityOutcome(
            status="ambiguous",
            reason="multiple_compatible_definitions",
            candidates=ordered,
        )
    if not allowed_kinds:
        return IdentityOutcome(status="oracle_gap", reason="unsupported_node_kind")
    if (project, path) in included_sources:
        return IdentityOutcome(status="oracle_gap", reason="included_source")
    host_target_os = {"darwin": "macos", "linux": "linux", "win32": "windows"}.get(
        sys.platform
    )
    inactive_cfg = False
    for attribute in decorators:
        if not isinstance(attribute, str):
            continue
        predicate = "".join(attribute.split())
        target_os = re.fullmatch(r'#\[cfg\(target_os="([^"]+)"\)\]', predicate)
        inactive_cfg = (
            predicate == "#[cfg(any())]"
            or (predicate == "#[cfg(windows)]" and os.name != "nt")
            or (predicate == "#[cfg(unix)]" and os.name == "nt")
            or (
                target_os is not None
                and host_target_os is not None
                and target_os.group(1) != host_target_os
            )
        )
        if inactive_cfg:
            break
    if inactive_cfg:
        return IdentityOutcome(
            status="not_in_oracle_configuration",
            reason="cfg_inactive",
        )
    if any(
        marker in attribute
        for attribute in decorators
        if isinstance(attribute, str)
        for marker in _PROC_MACRO_ATTRIBUTES
    ):
        return IdentityOutcome(status="oracle_gap", reason="proc_macro_output")
    kind_matches = set()
    if label != "Module":
        kind_matches = {
            scip_name
            for rng, _, scip_name, symbol_kind in entries
            if rng[0] in declaration_lines
            and symbol_kind in allowed_kinds
        }
    if kind_matches:
        return IdentityOutcome(status="oracle_gap", reason="name_mismatch")
    return IdentityOutcome(
        status="oracle_gap",
        reason=("oracle_definition_missing" if entries else "oracle_document_missing"),
    )


def map_rust_graph_nodes(graph_database, scip_file):
    """Return ``node id -> IdentityOutcome`` for every graph node backed by a Rust file.

    Ordinary definitions require the graph node's name and kind to agree with
    the SCIP occurrence. File-level modules instead use their exact full-file
    range because their graph name is a path. Multiple compatible symbols are
    reported, never selected heuristically. File nodes are non-semantic and
    absent from the returned mapping. Synthesized derive nodes declare
    ``rust_derive`` and ``rust_derive_owner`` in their JSON properties; their
    identity is the exact matching derived-method symbol used by a SCIP
    occurrence.
    """
    decoder = _load_decoder()
    definitions = decoder.parse_definition_identities(scip_file)
    connection = sqlite3.connect(graph_database)
    try:
        roots = _project_roots(connection)
        nodes = list(connection.execute(
            "select id, project, label, name, qualified_name, file_path, "
            "start_line, end_line, properties from nodes "
            "where file_path like '%.rs' and label <> 'File'"
        ))
    finally:
        connection.close()

    included_sources = set()
    for project, root in roots.items():
        if not root:
            continue
        root_path = Path(root).resolve()
        for source_path in root_path.rglob("*.rs"):
            source = source_path.read_text(encoding="utf-8", errors="replace")
            for included in _INCLUDE_LITERAL.findall(source):
                included_path = (source_path.parent / included).resolve()
                try:
                    relative = included_path.relative_to(root_path).as_posix()
                except ValueError:
                    continue
                included_sources.add((project, relative))

    regular_nodes = []
    generated_nodes = []
    for node in nodes:
        values = _parse_properties(node[8])
        if "rust_derive" in values or "rust_derive_owner" in values:
            generated_nodes.append(node)
        else:
            regular_nodes.append(node)

    derive_owners = set()
    for node in generated_nodes:
        owner = _parse_properties(node[8]).get("rust_derive_owner")
        if isinstance(owner, str):
            derive_owners.add(owner)

    outcomes = {}
    outcomes_by_qn = defaultdict(list)
    for node in regular_nodes:
        outcome = _identity_for_node(
            node, definitions, roots, included_sources, node[4] in derive_owners
        )
        outcomes[node[0]] = outcome
        outcomes_by_qn[node[4]].append(outcome)

    if generated_nodes:
        _, generated_definitions, references, _ = decoder.parse_with_definition_sites(
            scip_file
        )
        oracle_symbols = {
            symbol
            for entries in generated_definitions.values()
            for _, symbol in entries
        }
        oracle_symbols.update(
            symbol for entries in references.values() for _, symbol in entries
        )
        for node in generated_nodes:
            outcomes[node[0]] = _generated_outcome(
                node, outcomes_by_qn, oracle_symbols
            )
    return outcomes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("graph_database")
    parser.add_argument("scip_file")
    args = parser.parse_args()
    outcomes = map_rust_graph_nodes(args.graph_database, args.scip_file)
    counts = Counter(outcome.status for outcome in outcomes.values())
    reasons = Counter(
        outcome.reason for outcome in outcomes.values() if outcome.reason is not None
    )
    with sqlite3.connect(args.graph_database) as connection:
        excluded = connection.execute(
            "select count(*) from nodes "
            "where file_path like '%.rs' and label = 'File'"
        ).fetchone()[0]
    print(
        "outcomes "
        + " ".join(f"{status}={counts[status]}" for status in OUTCOME_STATUSES)
        + f" total={len(outcomes)} excluded_non_semantic_files={excluded}"
    )
    print(
        "reasons "
        + " ".join(
            f"{reason}={reasons[reason]}" for reason in IDENTITY_REASON_DESCRIPTIONS
        )
    )


if __name__ == "__main__":
    main()
