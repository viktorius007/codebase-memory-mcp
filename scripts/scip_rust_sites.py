#!/usr/bin/env python3
"""Inventory Rust call sites independently from source and rust-analyzer SCIP.

``call_sites(corpus_root, scip_path)`` returns one ``CallSite`` per resolved
source call. Byte offsets are absolute, zero-based, and end-exclusive, matching
the resolver dump's callee-identifier columns. Internal symbols retain their
complete SCIP identity; call-through-value, external, and ambiguous resolutions
use those literal marker strings. Call forms are ``plain_path``, ``method``,
``qualified_path``, ``macro_argument``, and ``constructor``.
"""

import argparse
import bisect
import importlib.util
import json
from collections import Counter, defaultdict
from pathlib import Path
import re
import sqlite3
import subprocess
import tempfile
from typing import NamedTuple


CALL_THROUGH_VALUE = "call_through_value"
EXTERNAL = "external"
AMBIGUOUS = "ambiguous"
NON_INTERNAL_TRUTH = frozenset((CALL_THROUGH_VALUE, EXTERNAL, AMBIGUOUS))


class CallSite(NamedTuple):
    relative_path: str
    callee_start_byte: int
    callee_end_byte: int
    resolved_symbol: str
    call_form: str


class PublishedCall(NamedTuple):
    relative_path: str
    site_start_byte: int
    site_end_byte: int
    callee_start_byte: int
    callee_end_byte: int
    caller_qualified_name: str
    target_qualified_name: str
    strategy: str


class GraphCall(NamedTuple):
    source_id: int
    target_id: int
    source_path: str
    target_path: str
    caller_qualified_name: str
    target_qualified_name: str


class ScoredEdge(NamedTuple):
    relative_path: str
    callee_start_byte: int
    callee_end_byte: int
    caller_qualified_name: str
    caller_symbol: str | None
    target_qualified_name: str
    target_symbol: str | None
    strategy: str
    call_form: str
    oracle_caller_symbol: str | None
    oracle_symbol: str | None
    outcome: str
    reason: str


class CallerOutcome(NamedTuple):
    graph_symbol: str | None
    oracle_symbol: str | None
    outcome: str
    reason: str


class RecallOccurrence(NamedTuple):
    relative_path: str
    callee_start_byte: int
    callee_end_byte: int
    oracle_caller_symbol: str | None
    oracle_symbol: str
    call_form: str
    strategy: str
    outcome: str


class IntegrityError(NamedTuple):
    category: str
    relative_path: str
    callee_start_byte: int
    callee_end_byte: int
    caller_qualified_name: str
    target_qualified_name: str


class ScoreReport(NamedTuple):
    edges: tuple[ScoredEdge, ...]
    occurrences: tuple[ScoredEdge, ...]
    recall: tuple[RecallOccurrence, ...]
    inventory: tuple[CallSite, ...]
    integrity: tuple[IntegrityError, ...]
    completeness: tuple[tuple[str, int], ...]
    non_rust_target_edges: int


_DECODER_PATH = Path(__file__).with_name("scip-call-edges.py")
_DECODER_SPEC = importlib.util.spec_from_file_location("scip_call_edges", _DECODER_PATH)
_DECODER = importlib.util.module_from_spec(_DECODER_SPEC)
_DECODER_SPEC.loader.exec_module(_DECODER)


def _fill_spaces(masked, start, end):
    masked[start:end] = b" " * (end - start)


def _raw_string_end(source, start):
    cursor = start
    if source[cursor : cursor + 1] in (b"b", b"c"):
        cursor += 1
    if source[cursor : cursor + 1] != b"r":
        return None
    cursor += 1
    hashes = 0
    while source[cursor : cursor + 1] == b"#":
        hashes += 1
        cursor += 1
    if source[cursor : cursor + 1] != b'"':
        return None
    terminator = b'"' + (b"#" * hashes)
    close = source.find(terminator, cursor + 1)
    return len(source) if close < 0 else close + len(terminator)


def _quoted_end(source, start, quote):
    cursor = start + 1
    while cursor < len(source):
        if source[cursor] == ord("\\"):
            cursor += 2
        elif source[cursor] == quote:
            return cursor + 1
        else:
            cursor += 1
    return len(source)


def _char_end(source, start):
    cursor = start + 1
    if cursor >= len(source):
        return None
    if source[cursor] == ord("\\"):
        cursor += 2
        if source[start + 1 : start + 3] in (b"\\x", b"\\u"):
            while cursor < len(source) and source[cursor] != ord("'"):
                cursor += 1
    else:
        cursor += 1
        while cursor < len(source) and source[cursor] & 0xC0 == 0x80:
            cursor += 1
    return cursor + 1 if source[cursor : cursor + 1] == b"'" else None


def _block_comment_end(source, start):
    cursor = start + 2
    depth = 1
    while cursor < len(source) and depth:
        if source[cursor : cursor + 2] == b"/*":
            depth += 1
            cursor += 2
        elif source[cursor : cursor + 2] == b"*/":
            depth -= 1
            cursor += 2
        else:
            cursor += 1
    return cursor


def _masked_source(source):
    masked = bytearray(source)
    cursor = 0
    while cursor < len(source):
        raw_end = _raw_string_end(source, cursor)
        if raw_end is not None:
            _fill_spaces(masked, cursor, raw_end)
            cursor = raw_end
        elif source[cursor : cursor + 2] == b"//":
            end = source.find(b"\n", cursor + 2)
            end = len(source) if end < 0 else end
            _fill_spaces(masked, cursor, end)
            cursor = end
        elif source[cursor : cursor + 2] == b"/*":
            end = _block_comment_end(source, cursor)
            _fill_spaces(masked, cursor, end)
            cursor = end
        elif source[cursor : cursor + 1] == b'"':
            end = _quoted_end(source, cursor, ord('"'))
            _fill_spaces(masked, cursor, end)
            cursor = end
        elif source[cursor : cursor + 1] == b"'":
            end = _char_end(source, cursor)
            if end is None:
                cursor += 1
            else:
                _fill_spaces(masked, cursor, end)
                cursor = end
        else:
            cursor += 1
    return bytes(masked)


def _previous_code_byte(code, cursor):
    cursor -= 1
    while cursor >= 0 and code[cursor : cursor + 1].isspace():
        cursor -= 1
    return cursor


def _skip_space(code, cursor):
    while code[cursor : cursor + 1].isspace():
        cursor += 1
    return cursor


def _skip_turbofish(code, cursor):
    if code[cursor : cursor + 2] != b"::":
        return cursor
    opening = _skip_space(code, cursor + 2)
    if code[opening : opening + 1] != b"<":
        return cursor
    cursor = opening + 1
    while cursor < len(code):
        if code[cursor] == ord(">"):
            call_open = _skip_space(code, cursor + 1)
            if code[call_open : call_open + 1] == b"(":
                return cursor + 1
        cursor += 1
    return opening


def _matching_open_parenthesis(code, close):
    depth = 1
    cursor = close - 1
    while cursor >= 0:
        if code[cursor] == ord(")"):
            depth += 1
        elif code[cursor] == ord("("):
            depth -= 1
            if depth == 0:
                return cursor
        cursor -= 1
    return None


def _grouping_parenthesis(code, opening):
    previous = _previous_code_byte(code, opening)
    if previous < 0:
        return True
    byte = code[previous]
    if byte >= 0x80 or chr(byte).isalnum() or byte == ord("_"):
        end = previous + 1
        start = previous
        while start > 0 and (
            code[start - 1] >= 0x80
            or chr(code[start - 1]).isalnum()
            or code[start - 1] == ord("_")
        ):
            start -= 1
        return code[start:end] in {
            b"break", b"if", b"match", b"return", b"while", b"yield"
        }
    return byte not in (ord("!"), ord(")"), ord("]"), ord(">"), ord("."))


def _callee_prefix(prefix):
    prefix = prefix.strip()
    while prefix[:1] in (b"&", b"*"):
        prefix = prefix[1:].lstrip()
    allowed = b"_:.<> \t\r\n"
    return all(
        byte >= 0x80 or chr(byte).isalnum() or byte in allowed for byte in prefix
    )


def _invoked_leaf(code, start, end):
    cursor = _skip_space(code, end)
    cursor = _skip_space(code, _skip_turbofish(code, cursor))
    parenthesized = False
    while code[cursor : cursor + 1] == b")":
        opening = _matching_open_parenthesis(code, cursor)
        if opening is None or not _grouping_parenthesis(code, opening):
            return False
        if not parenthesized and not _callee_prefix(code[opening + 1 : start]):
            return False
        parenthesized = True
        cursor = _skip_space(code, cursor + 1)
    return code[cursor : cursor + 1] == b"("


def _is_macro_open(code, cursor):
    bang = _previous_code_byte(code, cursor)
    if bang < 0 or code[bang] != ord("!"):
        return False
    predecessor = _previous_code_byte(code, bang)
    if predecessor < 0:
        return False
    byte = code[predecessor]
    return byte >= 0x80 or chr(byte).isalnum() or byte == ord("_")


def _is_attribute_open(code, cursor):
    marker = _previous_code_byte(code, cursor)
    if marker >= 0 and code[marker] == ord("#"):
        return True
    if marker < 0 or code[marker] != ord("!"):
        return False
    marker = _previous_code_byte(code, marker)
    return marker >= 0 and code[marker] == ord("#")


def _delimited_ranges(code, selected_open):
    pairs = {ord("("): ord(")"), ord("["): ord("]"), ord("{"): ord("}")}
    closers = set(pairs.values())
    stack = []
    ranges = []
    for cursor, byte in enumerate(code):
        if byte in pairs:
            stack.append((byte, cursor, selected_open(code, cursor)))
        elif byte in closers and stack and pairs[stack[-1][0]] == byte:
            _, start, selected = stack.pop()
            if selected:
                ranges.append((start + 1, cursor))
    return sorted(ranges)


def _merged_ranges(ranges):
    merged = []
    for start, end in ranges:
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def _macro_argument_ranges(code):
    return _merged_ranges(_delimited_ranges(code, _is_macro_open))


def _attribute_ranges(code):
    return _merged_ranges(_delimited_ranges(code, _is_attribute_open))


def _rust_call_callees(sources):
    callees = defaultdict(set)
    prefix = b"fn __cbm(){ __cbm("
    suffix = b"); }"
    with tempfile.TemporaryDirectory(prefix="cbm-rust-call-sites-") as temporary:
        inputs = {}
        temporary_path = Path(temporary)
        input_number = 0
        for relative_path, source in sources.items():
            raw_path = temporary_path / f"{input_number}.rs"
            raw_path.write_bytes(source)
            inputs[str(raw_path)] = (relative_path, 0, len(source), 0)
            input_number += 1
            code = _masked_source(source)
            for start, end in _delimited_ranges(code, _is_macro_open):
                macro_path = temporary_path / f"{input_number}.rs"
                macro_path.write_bytes(prefix + source[start:end] + suffix)
                inputs[str(macro_path)] = (relative_path, start, end, len(prefix))
                input_number += 1
        result = subprocess.run(
            (
                "ast-grep", "run", "--lang", "rust", "--pattern",
                "$F($$$ARGS)", "--json=stream", str(temporary_path),
            ),
            stdout=subprocess.PIPE,
            check=False,
        )
        if result.returncode not in (0, 1):
            raise subprocess.CalledProcessError(result.returncode, result.args)
        for line in result.stdout.splitlines():
            record = json.loads(line)
            callee = record["metaVariables"]["single"]["F"]
            relative_path, source_start, source_end, wrapper_length = inputs[
                record["file"]
            ]
            start = source_start + callee["range"]["byteOffset"]["start"]
            end = source_start + callee["range"]["byteOffset"]["end"]
            start -= wrapper_length
            end -= wrapper_length
            if source_start <= start and end <= source_end:
                text = callee["text"]
                callees[relative_path].add(
                    (
                        start,
                        end,
                        text.startswith("(") and text.endswith(")"),
                    )
                )
    return {relative_path: sorted(items) for relative_path, items in callees.items()}


def _inside_ranges(ranges, starts, offset):
    index = bisect.bisect_right(starts, offset) - 1
    return index >= 0 and offset < ranges[index][1]


def _call_form(code, start, parenthesized, macro_ranges, macro_starts):
    if _inside_ranges(macro_ranges, macro_starts, start):
        return "macro_argument"
    if parenthesized:
        return "plain_path"
    previous = _previous_code_byte(code, start)
    if previous >= 0 and code[previous] == ord("."):
        return "method"
    if previous >= 1 and code[previous - 1 : previous + 1] == b"::":
        return "qualified_path"
    return "plain_path"


def _line_starts(source):
    starts = [0]
    starts.extend(index + 1 for index, byte in enumerate(source) if byte == 10)
    return starts


def _byte_range(source, starts, source_range):
    start_line, start_column, end_line, end_column = source_range
    if start_line >= len(starts) or end_line >= len(starts):
        return None
    start = starts[start_line] + start_column
    end = starts[end_line] + end_column
    if start >= end or end > len(source):
        return None
    return start, end


def _constructor_symbol(symbol):
    return _DECODER.descriptor_of(symbol).endswith("#")


def _truth(symbols, project_packages):
    if len(symbols) != 1:
        return AMBIGUOUS
    symbol = next(iter(symbols))
    if symbol.startswith("local "):
        return CALL_THROUGH_VALUE
    descriptor = _DECODER.descriptor_of(symbol)
    if not descriptor.endswith("().") and not _constructor_symbol(symbol):
        return CALL_THROUGH_VALUE
    if _DECODER.package_of(symbol) not in project_packages:
        return EXTERNAL
    return symbol


def _callable_symbol(symbol):
    descriptor = _DECODER.descriptor_of(symbol)
    return (
        symbol.startswith("local ")
        or descriptor.endswith("().")
        or _constructor_symbol(symbol)
    )


def _skip_space(code, cursor):
    while cursor < len(code) and code[cursor : cursor + 1].isspace():
        cursor += 1
    return cursor


def _callee_position(code, end):
    cursor = _skip_space(code, end)
    if code[cursor : cursor + 2] == b"::":
        cursor = _skip_space(code, cursor + 2)
        if code[cursor : cursor + 1] == b"<":
            depth = 1
            delimiters = [0, 0, 0]
            cursor += 1
            while cursor < len(code) and depth:
                byte = code[cursor]
                if byte == ord("("):
                    delimiters[0] += 1
                elif byte == ord(")"):
                    delimiters[0] -= 1
                elif byte == ord("["):
                    delimiters[1] += 1
                elif byte == ord("]"):
                    delimiters[1] -= 1
                elif byte == ord("{"):
                    delimiters[2] += 1
                elif byte == ord("}"):
                    delimiters[2] -= 1
                elif byte == ord("<") and not any(delimiters):
                    depth += 1
                elif (
                    byte == ord(">")
                    and not any(delimiters)
                    and code[cursor - 1 : cursor] != b"-"
                ):
                    depth -= 1
                cursor += 1
            if depth:
                return False
            cursor = _skip_space(code, cursor)
    return code[cursor : cursor + 1] == b"("


def _completeness_counts(
    references, definition_sites, sources, records, project_packages
):
    inventory_keys = {
        (site.relative_path, site.callee_start_byte, site.callee_end_byte)
        for site in records
    }
    counts = Counter()
    for relative_path, occurrences in references.items():
        grouped = defaultdict(set)
        for source_range, symbol in occurrences:
            if (
                _callable_symbol(symbol)
                and _DECODER.package_of(symbol) in project_packages
            ):
                grouped[source_range].add(symbol)
        definition_ranges = {
            source_range
            for source_range, _, _ in definition_sites.get(relative_path, ())
        }
        source = sources.get(relative_path)
        if source is None:
            counts["excluded_source_missing"] += len(grouped)
            continue
        code = _masked_source(source)
        starts = _line_starts(source)
        macro_ranges = _macro_argument_ranges(code)
        macro_starts = [start for start, _ in macro_ranges]
        attribute_ranges = _attribute_ranges(code)
        attribute_starts = [start for start, _ in attribute_ranges]
        for source_range in grouped:
            if source_range in definition_ranges:
                counts["excluded_definition_site"] += 1
                continue
            byte_range = _byte_range(source, starts, source_range)
            if byte_range is None:
                counts["excluded_invalid_range"] += 1
                continue
            start, end = byte_range
            if not _callee_position(code, end):
                counts["excluded_not_callee_position"] += 1
                continue
            if _inside_ranges(attribute_ranges, attribute_starts, start):
                counts["excluded_attribute_syntax"] += 1
                continue
            if (relative_path, start, end) in inventory_keys:
                counts["inventory_found"] += 1
            elif _inside_ranges(macro_ranges, macro_starts, start):
                counts["excluded_macro_argument_parse_failure"] += 1
            else:
                counts["excluded_rust_parse_failure"] += 1
    categories = (
        "inventory_found",
        "excluded_macro_argument_parse_failure",
        "excluded_rust_parse_failure",
        "excluded_attribute_syntax",
        "excluded_definition_site",
        "excluded_not_callee_position",
        "excluded_invalid_range",
        "excluded_source_missing",
    )
    return tuple((category, counts[category]) for category in categories)


def _call_inventory(corpus_root, scip_path):
    corpus_root = Path(corpus_root)
    _, definitions, references, definition_sites = (
        _DECODER.parse_with_definition_sites(scip_path)
    )
    project_packages = {
        package
        for entries in definitions.values()
        for _, symbol in entries
        if (package := _DECODER.package_of(symbol)) is not None
    }
    sources = {
        relative_path: source_path.read_bytes()
        for relative_path in references
        if (source_path := corpus_root / relative_path).is_file()
    }
    parsed_callees = _rust_call_callees(sources)
    records = []
    for relative_path, occurrences in references.items():
        source = sources.get(relative_path)
        if source is None:
            continue
        code = _masked_source(source)
        macro_ranges = _macro_argument_ranges(code)
        macro_starts = [start for start, _ in macro_ranges]
        attribute_ranges = _attribute_ranges(code)
        attribute_starts = [start for start, _ in attribute_ranges]
        grouped = defaultdict(set)
        for source_range, symbol in occurrences:
            grouped[source_range].add(symbol)
        starts = _line_starts(source)
        for source_range, symbols in grouped.items():
            byte_range = _byte_range(source, starts, source_range)
            if byte_range is None:
                continue
            start, end = byte_range
            if _inside_ranges(attribute_ranges, attribute_starts, start):
                continue
            if not _invoked_leaf(code, start, end):
                continue
            matches = [
                callee
                for callee in parsed_callees.get(relative_path, ())
                if callee[0] <= start and end <= callee[1]
            ]
            if not matches:
                continue
            _, _, parenthesized = min(
                matches, key=lambda callee: callee[1] - callee[0]
            )
            constructor = (
                len(symbols) == 1 and _constructor_symbol(next(iter(symbols)))
            )
            records.append(
                CallSite(
                    relative_path,
                    start,
                    end,
                    _truth(symbols, project_packages),
                    (
                        "constructor"
                        if constructor
                        else _call_form(
                            code, start, parenthesized, macro_ranges, macro_starts
                        )
                    ),
                )
            )
    records = sorted(records)
    completeness = _completeness_counts(
        references, definition_sites, sources, records, project_packages
    )
    return records, completeness


def call_sites(corpus_root, scip_path):
    """Return source call occurrences resolved by the supplied SCIP oracle."""
    records, _ = _call_inventory(corpus_root, scip_path)
    return records


def _oracle_occurrences(corpus_root, scip_path, sites):
    _, definitions, references, definition_sites = (
        _DECODER.parse_with_definition_sites(scip_path)
    )
    wanted = defaultdict(set)
    for site in sites:
        wanted[site.relative_path].add(
            (site.relative_path, site.callee_start_byte, site.callee_end_byte)
        )
    callers = {}
    targets = defaultdict(set)
    for relative_path, occurrences in references.items():
        path_sites = wanted.get(relative_path)
        if not path_sites:
            continue
        source = (Path(corpus_root) / relative_path).read_bytes()
        starts = _line_starts(source)
        for source_range, target in occurrences:
            byte_range = _byte_range(source, starts, source_range)
            if byte_range is None:
                continue
            key = (relative_path, byte_range[0], byte_range[1])
            if key in path_sites:
                callers[key] = _DECODER.innermost(
                    definitions[relative_path], (source_range[0], source_range[1])
                )
                targets[key].add(target)
    exact_targets = {
        key: next(iter(symbols))
        for key, symbols in targets.items()
        if len(symbols) == 1
    }
    definition_locations = defaultdict(list)
    for relative_path, entries in definition_sites.items():
        for definition_range, _, symbol in entries:
            definition_locations[symbol].append((relative_path, definition_range))
    return callers, exact_targets, definition_locations


def published_calls(dump_path):
    """Return only published CALLS rows from a resolver dump."""
    records = []
    with open(dump_path, encoding="utf-8", errors="replace") as handle:
        for line_number, raw in enumerate(handle, 1):
            if not raw.startswith("C\t"):
                continue
            fields = raw.rstrip("\n").split("\t")
            if len(fields) < 14:
                raise ValueError(
                    f"{dump_path}:{line_number}: published C row has {len(fields)} fields; "
                    "expected at least 14"
                )
            if fields[9] != "admitted":
                continue
            try:
                site_start, site_end = int(fields[2]), int(fields[3])
                callee_start, callee_end = int(fields[12]), int(fields[13])
            except ValueError as error:
                raise ValueError(
                    f"{dump_path}:{line_number}: call ranges must be decimal byte offsets"
                ) from error
            records.append(
                PublishedCall(
                    fields[1], site_start, site_end, callee_start, callee_end,
                    fields[4], fields[6], fields[7],
                )
            )
    return tuple(records)


def _load_identity_module():
    path = Path(__file__).with_name("scip_rust_identity.py")
    spec = importlib.util.spec_from_file_location("scip_rust_identity", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _node_identities(graph_database, scip_file):
    identity = _load_identity_module()
    outcomes = identity.map_rust_graph_nodes(graph_database, scip_file)
    connection = sqlite3.connect(graph_database)
    try:
        rows = connection.execute(
            "select id, qualified_name, label, name, file_path, start_line, end_line "
            "from nodes"
        )
        by_name = defaultdict(list)
        for node_id, qualified_name, label, name, path, start, end in rows:
            by_name[qualified_name].append(
                (outcomes.get(node_id), label, name, path, start, end)
            )
    finally:
        connection.close()
    return by_name


def _single_identity(by_name, qualified_name, endpoint):
    nodes = by_name.get(qualified_name, ())
    if not nodes:
        return None, f"{endpoint}_node_missing", None
    if len(nodes) != 1:
        return None, f"{endpoint}_qualified_name_is_not_unique", None
    node = nodes[0]
    outcome = node[0]
    if outcome is None:
        return None, f"{endpoint}_node_not_semantic", node
    if outcome.status == "matched":
        return outcome.symbol, "-", node
    return None, f"{endpoint}_{outcome.status}:{outcome.reason}", node


def _rust_call_edges(graph_database):
    connection = sqlite3.connect(graph_database)
    try:
        exists = connection.execute(
            "select 1 from sqlite_master where type='table' and name='edges'"
        ).fetchone()
        if exists is None:
            return None
        rows = connection.execute(
            "select e.source_id, e.target_id, source.file_path, target.file_path, "
            "source.qualified_name, target.qualified_name "
            "from edges e "
            "join nodes source on source.id = e.source_id "
            "join nodes target on target.id = e.target_id "
            "where e.type = 'CALLS' and source.file_path glob '*.rs' "
            "order by e.source_id, e.target_id"
        )
        return tuple(GraphCall(*row) for row in rows)
    finally:
        connection.close()


def _rust_function_items(sources):
    functions = defaultdict(list)
    entries = list(sources.items())
    for offset in range(0, len(entries), 64):
        batch = entries[offset : offset + 64]
        combined = bytearray()
        starts = []
        for relative_path, source in batch:
            if combined:
                combined.extend(b"\n")
            starts.append((len(combined), relative_path, len(source)))
            combined.extend(source)
        result = subprocess.run(
            (
                "ast-grep", "run", "--stdin", "--lang", "rust",
                "--kind", "function_item", "--json=stream",
            ),
            input=bytes(combined), stdout=subprocess.PIPE, check=False,
        )
        if result.returncode not in (0, 1):
            raise subprocess.CalledProcessError(result.returncode, result.args)
        batch_starts = [start for start, _, _ in starts]
        for line in result.stdout.splitlines():
            record = json.loads(line)
            span = record["range"]
            start = span["byteOffset"]["start"]
            end = span["byteOffset"]["end"]
            index = bisect.bisect_right(batch_starts, start) - 1
            base, relative_path, source_length = starts[index]
            match = re.search(r"\bfn\s+(?:r#)?([^\W\d]\w*)", record["text"])
            if match is not None and end <= base + source_length:
                local_start = start - base
                local_end = end - base
                source = sources[relative_path]
                functions[relative_path].append(
                    (
                        local_start, local_end, match.group(1),
                        source.count(b"\n", 0, local_start) + 1,
                        source.count(b"\n", 0, local_end) + 1,
                    )
                )
    return functions


def _source_callers(corpus_root, calls):
    root = Path(corpus_root)
    sources = {
        path: source_path.read_bytes()
        for path in {call.relative_path for call in calls}
        if (source_path := root / path).is_file()
    }
    functions = _rust_function_items(sources)
    callers = {}
    for call in calls:
        candidates = [
            item for item in functions.get(call.relative_path, ())
            if item[0] <= call.callee_start_byte < item[1]
        ]
        if candidates:
            callers[
                (call.relative_path, call.callee_start_byte, call.callee_end_byte)
            ] = min(candidates, key=lambda item: item[1] - item[0])
    return callers


def _source_caller_matches(call, source_caller, node):
    if source_caller is None or node is None:
        return None
    _, label, name, path, start_line, end_line = node
    _, _, source_name, source_start_line, source_end_line = source_caller
    same_path = path == call.relative_path or path.endswith("/" + call.relative_path)
    return (
        label in ("Function", "Method")
        and same_path
        and name == source_name
        and start_line <= source_start_line
        and source_end_line <= end_line
    )


def _score_caller(call, oracle_caller_symbol, source_caller, identities):
    caller_symbol, caller_reason, caller_node = _single_identity(
        identities, call.caller_qualified_name, "caller"
    )
    if caller_reason == "caller_not_in_oracle_configuration:cfg_inactive":
        return CallerOutcome(
            caller_symbol, None, "unscorable", "caller_cfg_inactive"
        )
    if _callable_symbol(oracle_caller_symbol or ""):
        if caller_symbol is None:
            return CallerOutcome(
                caller_symbol, oracle_caller_symbol, "unscorable", caller_reason
            )
        if caller_symbol != oracle_caller_symbol:
            return CallerOutcome(
                caller_symbol, oracle_caller_symbol, "wrong",
                "caller_identity_mismatch",
            )
        return CallerOutcome(caller_symbol, oracle_caller_symbol, "correct", "-")
    source_match = _source_caller_matches(call, source_caller, caller_node)
    if source_match is True:
        category = caller_reason.rpartition(":")[2]
        if caller_symbol is not None:
            category = "non_callable_oracle"
        return CallerOutcome(
            caller_symbol, caller_symbol, "correct", f"source_caller:{category}"
        )
    if source_match is False:
        return CallerOutcome(
            caller_symbol, None, "wrong", "caller_source_mismatch"
        )
    reason = caller_reason if caller_symbol is None else "source_caller_missing"
    return CallerOutcome(caller_symbol, None, "unscorable", reason)


def _target_is_contradicted(
    site, expected_target, target_reason, target_node, definition_locations,
):
    if expected_target is None or target_node is None:
        return False
    if target_reason == "target_node_not_semantic":
        return True
    locations = definition_locations.get(expected_target, ())
    if not locations:
        return site.resolved_symbol == EXTERNAL
    _, _, _, node_path, start_line, end_line = target_node
    return not any(
        (
            node_path == relative_path
            or node_path.endswith("/" + relative_path)
        )
        and start_line - 1 <= definition_range[0] <= end_line - 1
        for relative_path, definition_range in locations
    )


def _score_edge(
    call, site, oracle_target_symbol, caller, identities, definition_locations,
):
    target_symbol, target_reason, target_node = _single_identity(
        identities, call.target_qualified_name, "target"
    )
    if site is None:
        return ScoredEdge(
            call.relative_path, call.callee_start_byte, call.callee_end_byte,
            call.caller_qualified_name, caller.graph_symbol,
            call.target_qualified_name,
            target_symbol, call.strategy, "no_oracle_site", None, None,
            "unscorable", "no_oracle_truth",
        )
    expected_target = (
        oracle_target_symbol if site.resolved_symbol == EXTERNAL
        else site.resolved_symbol
    )
    if caller.reason == "caller_cfg_inactive":
        return ScoredEdge(
            call.relative_path, call.callee_start_byte, call.callee_end_byte,
            call.caller_qualified_name, caller.graph_symbol,
            call.target_qualified_name,
            target_symbol, call.strategy, site.call_form, caller.oracle_symbol,
            expected_target, "unscorable", "caller_cfg_inactive",
        )
    unknown = []
    contradictions = []
    caller_basis = "-"
    if caller.outcome == "wrong":
        contradictions.append(caller.reason)
    elif caller.outcome == "unscorable":
        unknown.append(caller.reason)
    else:
        caller_basis = caller.reason
    if site.resolved_symbol in (CALL_THROUGH_VALUE, AMBIGUOUS):
        unknown.append(f"oracle_{site.resolved_symbol}")
    else:
        if target_symbol is None:
            if _target_is_contradicted(
                site, expected_target, target_reason, target_node,
                definition_locations,
            ):
                contradictions.append("target_identity_mismatch")
            else:
                unknown.append(target_reason)
        elif expected_target is None:
            unknown.append("oracle_target_missing")
        elif target_symbol != expected_target:
            contradictions.append("target_identity_mismatch")
    if contradictions:
        outcome = "wrong"
        reason = "+".join(contradictions)
    elif unknown:
        outcome = "unscorable"
        reason = "+".join(unknown)
    else:
        outcome = "correct"
        reason = caller_basis
    return ScoredEdge(
        call.relative_path, call.callee_start_byte, call.callee_end_byte,
        call.caller_qualified_name, caller.graph_symbol,
        call.target_qualified_name, target_symbol, call.strategy, site.call_form,
        caller.oracle_symbol,
        expected_target, outcome, reason,
    )


def score_calls(corpus_root, scip_file, graph_database, dump_path):
    """Score published edges and independent internal-call recall by occurrence."""
    inventory_records, completeness = _call_inventory(corpus_root, scip_file)
    inventory = tuple(inventory_records)
    sites = {
        (site.relative_path, site.callee_start_byte, site.callee_end_byte): site
        for site in inventory
    }
    oracle_callers, oracle_targets, definition_locations = _oracle_occurrences(
        corpus_root, scip_file, inventory
    )
    identities = _node_identities(graph_database, scip_file)
    calls = published_calls(dump_path)
    graph_calls = _rust_call_edges(graph_database)
    edges_table_missing = graph_calls is None
    if edges_table_missing:
        integrity = (IntegrityError("missing_edges_table", "-", -1, -1, "-", "-"),)
        graph_calls = ()
    else:
        integrity = ()
    calls_by_edge = defaultdict(list)
    for call in calls:
        calls_by_edge[(call.caller_qualified_name, call.target_qualified_name)].append(call)
    graph_pairs = {
        (edge.caller_qualified_name, edge.target_qualified_name)
        for edge in graph_calls
    }
    graph_relations = set()
    graph_names_by_symbol = defaultdict(set)
    for edge in graph_calls:
        caller_symbol, _, _ = _single_identity(
            identities, edge.caller_qualified_name, "caller"
        )
        target_symbol, _, _ = _single_identity(
            identities, edge.target_qualified_name, "target"
        )
        if caller_symbol is not None and target_symbol is not None:
            graph_relations.add((caller_symbol, target_symbol))
    for qualified_name in identities:
        symbol, _, _ = _single_identity(identities, qualified_name, "node")
        if symbol is not None:
            graph_names_by_symbol[symbol].add(qualified_name)
    edges = []
    scored_occurrences = []
    integrity = list(integrity)
    source_candidates = [
        site
        for key, site in sites.items()
        if not _callable_symbol(oracle_callers.get(key) or "")
    ]
    source_callers = _source_callers(corpus_root, source_candidates)
    graph_source_nodes = defaultdict(list)
    for qualified_name, nodes in identities.items():
        if len(nodes) != 1:
            continue
        node = nodes[0]
        _, label, name, _, _, _ = node
        if label in ("Function", "Method"):
            graph_source_nodes[name].append((qualified_name, node))
    source_nodes_by_site = {}
    for key, source_caller in source_callers.items():
        site = sites[key]
        source_nodes_by_site[key] = tuple(
            (qualified_name, node)
            for qualified_name, node in graph_source_nodes[source_caller[2]]
            if _source_caller_matches(site, source_caller, node)
        )
    caller_outcomes = {}
    callers_by_site = defaultdict(list)
    for call in calls:
        key = (call.relative_path, call.callee_start_byte, call.callee_end_byte)
        caller = _score_caller(
            call, oracle_callers.get(key), source_callers.get(key), identities
        )
        caller_outcomes[call] = caller
        if key in sites:
            callers_by_site[key].append(caller)
    for edge in graph_calls:
        key = (edge.caller_qualified_name, edge.target_qualified_name)
        provenance = calls_by_edge.get(key, ())
        if not provenance:
            caller_symbol, _, _ = _single_identity(
                identities, edge.caller_qualified_name, "caller"
            )
            target_symbol, _, _ = _single_identity(
                identities, edge.target_qualified_name, "target"
            )
            edges.append(
                ScoredEdge(
                    edge.source_path, -1, -1, edge.caller_qualified_name,
                    caller_symbol, edge.target_qualified_name, target_symbol,
                    "no_dump_provenance", "no_provenance", None, None,
                    "unscorable", "edge_without_dump",
                )
            )
            integrity.append(
                IntegrityError(
                    "edge_without_dump", edge.source_path, -1, -1,
                    edge.caller_qualified_name, edge.target_qualified_name,
                )
            )
            continue
        scores = [
            _score_edge(
                call,
                sites.get(
                    (
                        call.relative_path, call.callee_start_byte,
                        call.callee_end_byte,
                    )
                ),
                oracle_targets.get(
                    (
                        call.relative_path, call.callee_start_byte,
                        call.callee_end_byte,
                    )
                ),
                caller_outcomes[call],
                identities,
                definition_locations,
            )
            for call in provenance
        ]
        scored_occurrences.extend(scores)
        rank = {"correct": 0, "unscorable": 1, "wrong": 2}
        edges.append(min(scores, key=lambda score: rank[score.outcome]))
    for call in calls:
        key = (call.caller_qualified_name, call.target_qualified_name)
        if not edges_table_missing and key not in graph_pairs:
            integrity.append(
                IntegrityError(
                    "dump_without_edge", call.relative_path, call.callee_start_byte,
                    call.callee_end_byte, call.caller_qualified_name,
                    call.target_qualified_name,
                )
            )
    edges_by_site = defaultdict(list)
    for edge in scored_occurrences:
        edges_by_site[
            (edge.relative_path, edge.callee_start_byte, edge.callee_end_byte)
        ].append(edge)
    recall = []
    for site in inventory:
        if site.resolved_symbol in NON_INTERNAL_TRUTH:
            continue
        key = (site.relative_path, site.callee_start_byte, site.callee_end_byte)
        site_edges = edges_by_site.get(key, ())
        site_callers = callers_by_site.get(key, ())
        raw_caller = oracle_callers.get(key)
        found = False
        if _callable_symbol(raw_caller or ""):
            caller = None
            if site_callers:
                rank = {"correct": 0, "unscorable": 1, "wrong": 2}
                caller = min(site_callers, key=lambda item: rank[item.outcome])
            if caller is not None and caller.outcome == "unscorable":
                continue
            oracle_caller_symbol = raw_caller
            found = any(edge.outcome == "correct" for edge in site_edges)
            if not found and oracle_caller_symbol is not None:
                found = (
                    oracle_caller_symbol, site.resolved_symbol
                ) in graph_relations
        else:
            source_nodes = source_nodes_by_site.get(key, ())
            outcomes = [node[0] for _, node in source_nodes]
            if outcomes and all(
                outcome is not None
                and outcome.status == "not_in_oracle_configuration"
                for outcome in outcomes
            ):
                continue
            caller_symbols = {
                outcome.symbol
                for outcome in outcomes
                if outcome is not None and outcome.status == "matched"
            }
            oracle_caller_symbol = (
                next(iter(caller_symbols)) if len(caller_symbols) == 1 else None
            )
            target_names = graph_names_by_symbol.get(site.resolved_symbol, ())
            found = any(
                (caller_name, target_name) in graph_pairs
                for caller_name, _ in source_nodes
                for target_name in target_names
            )
        strategies = sorted({edge.strategy for edge in site_edges})
        strategy = "+".join(strategies) if strategies else "no_published_edge"
        recall.append(
            RecallOccurrence(
                site.relative_path, site.callee_start_byte, site.callee_end_byte,
                oracle_caller_symbol, site.resolved_symbol, site.call_form, strategy,
                "found" if found else "missed",
            )
        )
    non_rust = sum(not edge.target_path.endswith(".rs") for edge in graph_calls)
    return ScoreReport(
        tuple(edges), tuple(scored_occurrences), tuple(recall), inventory,
        tuple(integrity), completeness, non_rust,
    )


def format_completeness(completeness):
    """Format the exhaustive oracle-reference accounting for scorer output."""
    total = sum(count for _, count in completeness)
    categories = " ".join(
        f"{category}={count}" for category, count in completeness
    )
    return f"call_site_completeness total={total} {categories}"


def write_score_details(output_directory, report):
    """Write every wrong published edge and missed internal occurrence as TSV."""
    output = Path(output_directory)
    output.mkdir(parents=True, exist_ok=True)
    wrong_header = (
        "relative_path", "callee_start_byte", "callee_end_byte", "strategy",
        "call_form", "caller_qualified_name", "caller_symbol",
        "oracle_caller_symbol", "target_qualified_name", "target_symbol",
        "oracle_symbol", "reason",
    )
    missed_header = (
        "relative_path", "callee_start_byte", "callee_end_byte", "strategy",
        "call_form", "oracle_symbol",
    )
    wrong_rows = [
        (
            edge.relative_path, edge.callee_start_byte, edge.callee_end_byte,
            edge.strategy, edge.call_form, edge.caller_qualified_name,
            edge.caller_symbol or "-", edge.oracle_caller_symbol or "-",
            edge.target_qualified_name,
            edge.target_symbol or "-", edge.oracle_symbol or "-", edge.reason,
        )
        for edge in report.edges if edge.outcome == "wrong"
    ]
    edge_rows = [
        (
            edge.relative_path, edge.callee_start_byte, edge.callee_end_byte,
            edge.strategy, edge.call_form, edge.caller_qualified_name,
            edge.caller_symbol or "-", edge.oracle_caller_symbol or "-",
            edge.target_qualified_name,
            edge.target_symbol or "-", edge.oracle_symbol or "-", edge.outcome,
            edge.reason,
        )
        for edge in report.edges
    ]
    wrong_occurrence_rows = [
        (
            edge.relative_path, edge.callee_start_byte, edge.callee_end_byte,
            edge.strategy, edge.call_form, edge.caller_qualified_name,
            edge.caller_symbol or "-", edge.oracle_caller_symbol or "-",
            edge.target_qualified_name,
            edge.target_symbol or "-", edge.oracle_symbol or "-", edge.reason,
        )
        for edge in report.occurrences if edge.outcome == "wrong"
    ]
    missed_rows = [
        (
            occurrence.relative_path, occurrence.callee_start_byte,
            occurrence.callee_end_byte, occurrence.strategy, occurrence.call_form,
            occurrence.oracle_symbol,
        )
        for occurrence in report.recall if occurrence.outcome == "missed"
    ]
    for name, header, rows in (
        (
            "edge-outcomes.tsv",
            wrong_header[:-1] + ("outcome", "reason"),
            edge_rows,
        ),
        ("wrong-edges.tsv", wrong_header, wrong_rows),
        ("wrong-occurrences.tsv", wrong_header, wrong_occurrence_rows),
        ("missed-occurrences.tsv", missed_header, missed_rows),
        (
            "integrity-errors.tsv",
            (
                "category", "relative_path", "callee_start_byte", "callee_end_byte",
                "caller_qualified_name", "target_qualified_name",
            ),
            report.integrity,
        ),
    ):
        with (output / name).open("w", encoding="utf-8") as handle:
            handle.write("\t".join(header) + "\n")
            for row in sorted(rows):
                handle.write("\t".join(str(value) for value in row) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus")
    parser.add_argument("scip")
    args = parser.parse_args()
    inventory, completeness = _call_inventory(args.corpus, args.scip)
    counts = Counter(site.call_form for site in inventory)
    print(f"total: {sum(counts.values())}")
    for call_form in (
        "plain_path", "method", "qualified_path", "macro_argument", "constructor"
    ):
        print(f"{call_form}: {counts[call_form]}")
    print(format_completeness(completeness))


if __name__ == "__main__":
    main()
