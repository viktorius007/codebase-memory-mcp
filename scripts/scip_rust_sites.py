#!/usr/bin/env python3
"""Inventory Rust call sites independently from source and rust-analyzer SCIP.

``call_sites(corpus_root, scip_path)`` returns one ``CallSite`` per resolved
source call. Byte offsets are absolute, zero-based, and end-exclusive, matching
the resolver dump's callee-identifier columns. Internal symbols retain their
complete SCIP identity; local, external, and ambiguous resolutions use those
literal marker strings. Call forms are ``plain_path``, ``method``,
``qualified_path``, and ``macro_argument``.
"""

import argparse
import bisect
import importlib.util
from collections import Counter, defaultdict
from pathlib import Path
import sqlite3
from typing import NamedTuple


LOCAL = "local"
EXTERNAL = "external"
AMBIGUOUS = "ambiguous"


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
    oracle_symbol: str | None
    outcome: str
    reason: str


class RecallOccurrence(NamedTuple):
    relative_path: str
    callee_start_byte: int
    callee_end_byte: int
    oracle_symbol: str
    call_form: str
    strategy: str
    outcome: str


class ScoreReport(NamedTuple):
    edges: tuple[ScoredEdge, ...]
    recall: tuple[RecallOccurrence, ...]
    inventory: tuple[CallSite, ...]


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
    merged = []
    for start, end in sorted(ranges):
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def _macro_argument_ranges(code):
    return _delimited_ranges(code, _is_macro_open)


def _attribute_ranges(code):
    return _delimited_ranges(code, _is_attribute_open)


def _next_code_byte(code, cursor):
    while cursor < len(code) and code[cursor : cursor + 1].isspace():
        cursor += 1
    return cursor


def _skip_turbofish(code, cursor):
    if code[cursor : cursor + 2] != b"::":
        return cursor
    opening = _next_code_byte(code, cursor + 2)
    if code[opening : opening + 1] != b"<":
        return cursor
    depth = 0
    cursor = opening
    while cursor < len(code):
        if code[cursor] == ord("<"):
            depth += 1
        elif code[cursor] == ord(">"):
            depth -= 1
            if depth == 0:
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
        return code[start:end] in {b"break", b"if", b"match", b"return", b"while", b"yield"}
    return byte not in (ord("!"), ord(")"), ord("]"), ord(">"), ord("."))


def _callee_prefix(prefix):
    prefix = prefix.strip()
    while prefix[:1] in (b"&", b"*"):
        prefix = prefix[1:].lstrip()
    allowed = b"_:.<> \t\r\n"
    return all(byte >= 0x80 or chr(byte).isalnum() or byte in allowed for byte in prefix)


def _call_syntax(code, identifier_start, identifier_end):
    cursor = _next_code_byte(code, identifier_end)
    cursor = _next_code_byte(code, _skip_turbofish(code, cursor))
    parenthesized = False
    while code[cursor : cursor + 1] == b")":
        opening = _matching_open_parenthesis(code, cursor)
        if opening is None or not _grouping_parenthesis(code, opening):
            return False, False
        if not parenthesized and not _callee_prefix(code[opening + 1 : identifier_start]):
            return False, False
        parenthesized = True
        cursor = _next_code_byte(code, cursor + 1)
    return code[cursor : cursor + 1] == b"(", parenthesized


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


def _truth(symbols, workspace_crates):
    if len(symbols) != 1:
        return AMBIGUOUS
    symbol = next(iter(symbols))
    if symbol.startswith("local "):
        return LOCAL
    if _DECODER.crate_of(symbol) not in workspace_crates:
        return EXTERNAL
    return symbol


def _callable_symbol(symbol):
    return symbol.startswith("local ") or _DECODER.descriptor_of(symbol).endswith("().")


def call_sites(corpus_root, scip_path):
    """Return source call occurrences resolved by the supplied SCIP oracle."""
    corpus_root = Path(corpus_root)
    _, definitions, references, _ = _DECODER.parse_with_definition_sites(scip_path)
    workspace_crates = {
        crate
        for entries in definitions.values()
        for _, symbol in entries
        if (crate := _DECODER.crate_of(symbol)) is not None
    }
    records = []
    for relative_path, occurrences in references.items():
        source_path = corpus_root / relative_path
        if not source_path.is_file():
            continue
        source = source_path.read_bytes()
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
            symbols = {symbol for symbol in symbols if _callable_symbol(symbol)}
            if not symbols:
                continue
            byte_range = _byte_range(source, starts, source_range)
            if byte_range is None:
                continue
            start, end = byte_range
            if _inside_ranges(attribute_ranges, attribute_starts, start):
                continue
            is_call, parenthesized = _call_syntax(code, start, end)
            if not is_call:
                continue
            records.append(
                CallSite(
                    relative_path,
                    start,
                    end,
                    _truth(symbols, workspace_crates),
                    _call_form(code, start, parenthesized, macro_ranges, macro_starts),
                )
            )
    return sorted(records)


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
        rows = connection.execute("select id, qualified_name from nodes")
        by_name = defaultdict(list)
        for node_id, qualified_name in rows:
            if node_id in outcomes:
                by_name[qualified_name].append(outcomes[node_id])
    finally:
        connection.close()
    return by_name


def _single_identity(by_name, qualified_name):
    outcomes = by_name.get(qualified_name, ())
    if not outcomes:
        return None, "target_node_missing"
    if len(outcomes) != 1:
        return None, "target_qualified_name_is_not_unique"
    outcome = outcomes[0]
    if outcome.status == "matched":
        return outcome.symbol, "-"
    return None, f"target_{outcome.status}:{outcome.reason}"


def _score_edge(call, site, identities):
    caller_symbol, _ = _single_identity(identities, call.caller_qualified_name)
    target_symbol, target_reason = _single_identity(
        identities, call.target_qualified_name
    )
    if site is None:
        return ScoredEdge(
            call.relative_path, call.callee_start_byte, call.callee_end_byte,
            call.caller_qualified_name, caller_symbol, call.target_qualified_name,
            target_symbol, call.strategy, "no_oracle_site", None, "unscorable",
            "no_oracle_truth",
        )
    if site.resolved_symbol in (LOCAL, EXTERNAL, AMBIGUOUS):
        return ScoredEdge(
            call.relative_path, call.callee_start_byte, call.callee_end_byte,
            call.caller_qualified_name, caller_symbol, call.target_qualified_name,
            target_symbol, call.strategy, site.call_form, site.resolved_symbol,
            "unscorable", f"oracle_{site.resolved_symbol}",
        )
    if target_symbol is None:
        return ScoredEdge(
            call.relative_path, call.callee_start_byte, call.callee_end_byte,
            call.caller_qualified_name, caller_symbol, call.target_qualified_name,
            None, call.strategy, site.call_form, site.resolved_symbol,
            "unscorable", target_reason,
        )
    outcome = "correct" if target_symbol == site.resolved_symbol else "wrong"
    reason = "-" if outcome == "correct" else "definition_identity_mismatch"
    return ScoredEdge(
        call.relative_path, call.callee_start_byte, call.callee_end_byte,
        call.caller_qualified_name, caller_symbol, call.target_qualified_name,
        target_symbol, call.strategy, site.call_form, site.resolved_symbol,
        outcome, reason,
    )


def score_calls(corpus_root, scip_file, graph_database, dump_path):
    """Score published edges and independent internal-call recall by occurrence."""
    inventory = tuple(call_sites(corpus_root, scip_file))
    sites = {
        (site.relative_path, site.callee_start_byte, site.callee_end_byte): site
        for site in inventory
    }
    identities = _node_identities(graph_database, scip_file)
    edges = tuple(
        _score_edge(
            call,
            sites.get(
                (call.relative_path, call.callee_start_byte, call.callee_end_byte)
            ),
            identities,
        )
        for call in published_calls(dump_path)
    )
    edges_by_site = defaultdict(list)
    for edge in edges:
        edges_by_site[
            (edge.relative_path, edge.callee_start_byte, edge.callee_end_byte)
        ].append(edge)
    recall = []
    for site in inventory:
        if site.resolved_symbol in (LOCAL, EXTERNAL, AMBIGUOUS):
            continue
        key = (site.relative_path, site.callee_start_byte, site.callee_end_byte)
        site_edges = edges_by_site.get(key, ())
        found = any(edge.outcome == "correct" for edge in site_edges)
        strategies = sorted({edge.strategy for edge in site_edges})
        strategy = "+".join(strategies) if strategies else "no_published_edge"
        recall.append(
            RecallOccurrence(
                site.relative_path, site.callee_start_byte, site.callee_end_byte,
                site.resolved_symbol, site.call_form, strategy,
                "found" if found else "missed",
            )
        )
    return ScoreReport(edges, tuple(recall), inventory)


def write_score_details(output_directory, report):
    """Write every wrong published edge and missed internal occurrence as TSV."""
    output = Path(output_directory)
    output.mkdir(parents=True, exist_ok=True)
    wrong_header = (
        "relative_path", "callee_start_byte", "callee_end_byte", "strategy",
        "call_form", "caller_qualified_name", "caller_symbol",
        "target_qualified_name", "target_symbol", "oracle_symbol", "reason",
    )
    missed_header = (
        "relative_path", "callee_start_byte", "callee_end_byte", "strategy",
        "call_form", "oracle_symbol",
    )
    wrong_rows = [
        (
            edge.relative_path, edge.callee_start_byte, edge.callee_end_byte,
            edge.strategy, edge.call_form, edge.caller_qualified_name,
            edge.caller_symbol or "-", edge.target_qualified_name,
            edge.target_symbol or "-", edge.oracle_symbol or "-", edge.reason,
        )
        for edge in report.edges if edge.outcome == "wrong"
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
        ("wrong-edges.tsv", wrong_header, wrong_rows),
        ("missed-occurrences.tsv", missed_header, missed_rows),
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
    counts = Counter(site.call_form for site in call_sites(args.corpus, args.scip))
    print(f"total: {sum(counts.values())}")
    for call_form in ("plain_path", "method", "qualified_path", "macro_argument"):
        print(f"{call_form}: {counts[call_form]}")


if __name__ == "__main__":
    main()
