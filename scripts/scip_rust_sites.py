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
from typing import NamedTuple, Optional


CALL_THROUGH_VALUE = "call_through_value"
EXTERNAL = "external"
AMBIGUOUS = "ambiguous"
NON_INTERNAL_TRUTH = frozenset((CALL_THROUGH_VALUE, EXTERNAL, AMBIGUOUS))
NO_ORACLE_LOCATION = "no_oracle_location"

_EXPRESSION_MACROS = frozenset(
    (
        "assert",
        "assert_eq",
        "assert_ne",
        "dbg",
        "debug_assert",
        "debug_assert_eq",
        "debug_assert_ne",
        "eprint",
        "eprintln",
        "format",
        "format_args",
        "format_args_nl",
        "panic",
        "pin",
        "print",
        "println",
        "ready",
        "todo",
        "try",
        "unimplemented",
        "unreachable",
        "vec",
        "write",
        "writeln",
    )
)
_MACRO_DEFINITION = re.compile(
    rb"(?<![A-Za-z0-9_])macro_rules\s*!\s*"
    rb"((?:r#)?[A-Za-z_][A-Za-z0-9_]*)\s*([({[])"
)
_FRAGMENT_SPEC = (
    rb"\$\s*(?:r#)?[A-Za-z_][A-Za-z0-9_]*\s*:\s*"
    rb"(expr(?:_2021)?|pat(?:_param)?)\b"
)
_OPTIONAL_TRAILING_COMMA = rb"(?:\s*\$\s*\(\s*,\s*\)\s*\?)?"
_DIRECT_MACRO_FRAGMENT = re.compile(
    rb"\s*" + _FRAGMENT_SPEC + _OPTIONAL_TRAILING_COMMA + rb"\s*"
)
_REPEATED_MACRO_FRAGMENT = re.compile(
    rb"\s*\$\s*[({[]\s*" + _FRAGMENT_SPEC
    + rb"\s*[)}\]]\s*[,;]?\s*[*+?]"
    + _OPTIONAL_TRAILING_COMMA + rb"\s*",
    re.DOTALL,
)

_DECODER_PATH = Path(__file__).with_name("scip-call-edges.py")
_DECODER_SPEC = importlib.util.spec_from_file_location("scip_call_edges", _DECODER_PATH)
_DECODER = importlib.util.module_from_spec(_DECODER_SPEC)
_DECODER_SPEC.loader.exec_module(_DECODER)
DefinitionIdentity = _DECODER.DefinitionIdentity


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
    caller_symbol: Optional[str]
    target_qualified_name: str
    target_symbol: Optional[str]
    strategy: str
    call_form: str
    oracle_caller_symbol: Optional[str]
    oracle_symbol: Optional[str]
    outcome: str
    reason: str
    caller_identity: Optional[DefinitionIdentity] = None
    oracle_caller_identity: Optional[DefinitionIdentity] = None
    target_identity: Optional[DefinitionIdentity] = None
    oracle_target_identity: Optional[DefinitionIdentity] = None


class CallerOutcome(NamedTuple):
    graph_symbol: Optional[str]
    oracle_symbol: Optional[str]
    outcome: str
    reason: str
    graph_identity: Optional[DefinitionIdentity] = None
    oracle_identity: Optional[DefinitionIdentity] = None


class OracleDefinition(NamedTuple):
    symbol: str
    identity: Optional[DefinitionIdentity]


class RecallOccurrence(NamedTuple):
    relative_path: str
    callee_start_byte: int
    callee_end_byte: int
    oracle_caller_symbol: Optional[str]
    caller_category: str
    oracle_symbol: str
    call_form: str
    strategy: str
    outcome: str
    oracle_caller_identity: Optional[DefinitionIdentity] = None
    oracle_target_identity: Optional[DefinitionIdentity] = None
    target_category: str = "scip_definition"


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


def _previous_identifier(code, cursor):
    end = _previous_code_byte(code, cursor) + 1
    start = end
    while start > 0:
        byte = code[start - 1]
        if byte < 0x80 and not chr(byte).isalnum() and byte != ord("_"):
            break
        start -= 1
    return code[start:end]


def _is_matches_open(code, cursor):
    bang = _previous_code_byte(code, cursor)
    if bang < 0 or code[bang] != ord("!"):
        return False
    identifier = _previous_identifier(code, bang)
    return (
        identifier == b"matches" or identifier.endswith(b"_matches")
    )


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


def _matching_delimiter(code, opening):
    pairs = {ord("("): ord(")"), ord("["): ord("]"), ord("{"): ord("}")}
    if code[opening] not in pairs:
        return None
    stack = [pairs[code[opening]]]
    for cursor in range(opening + 1, len(code)):
        byte = code[cursor]
        if byte in pairs:
            stack.append(pairs[byte])
        elif stack and byte == stack[-1]:
            stack.pop()
            if not stack:
                return cursor
    return None


def _comma_segments(code, start, end):
    pairs = {ord("("): ord(")"), ord("["): ord("]"), ord("{"): ord("}")}
    stack = []
    ranges = []
    segment_start = _skip_space(code, start)
    for cursor in range(start, end):
        byte = code[cursor]
        if byte in pairs:
            stack.append(pairs[byte])
        elif stack and byte == stack[-1]:
            stack.pop()
        elif byte == ord(",") and not stack:
            segment_end = _previous_code_byte(code, cursor) + 1
            if segment_start < segment_end:
                ranges.append((segment_start, segment_end))
            segment_start = _skip_space(code, cursor + 1)
    segment_end = _previous_code_byte(code, end) + 1
    if segment_start < segment_end:
        ranges.append((segment_start, segment_end))
    return ranges


def _direct_fragment_kind(code):
    match = _DIRECT_MACRO_FRAGMENT.fullmatch(code)
    if match is None:
        return None
    return "pat" if match.group(1).startswith(b"pat") else "expr"


def _matcher_contract(matcher):
    repeated = _REPEATED_MACRO_FRAGMENT.fullmatch(matcher)
    if repeated is not None:
        kind = "pat" if repeated.group(1).startswith(b"pat") else "expr"
        return (kind,), kind, True
    segments = _comma_segments(matcher, 0, len(matcher))
    signature = tuple(
        _direct_fragment_kind(matcher[start:end]) for start, end in segments
    )
    kinds = set(signature)
    uniform = next(iter(kinds)) if len(kinds) == 1 else None
    return signature, uniform, False


def _macro_rule_contracts(sources):
    contracts = defaultdict(list)
    for source in sources.values():
        code = _masked_source(source)
        for definition in _MACRO_DEFINITION.finditer(code):
            name = definition.group(1).removeprefix(b"r#").decode()
            body_open = definition.end() - 1
            body_close = _matching_delimiter(code, body_open)
            if body_close is None:
                continue
            cursor = body_open + 1
            while cursor < body_close:
                cursor = _skip_space(code, cursor)
                if code[cursor : cursor + 1] not in (b"(", b"[", b"{"):
                    cursor += 1
                    continue
                matcher_close = _matching_delimiter(code, cursor)
                if matcher_close is None or matcher_close > body_close:
                    break
                after = _skip_space(code, matcher_close + 1)
                if code[after : after + 2] == b"=>":
                    contracts[name].append(
                        _matcher_contract(code[cursor + 1 : matcher_close])
                    )
                cursor = matcher_close + 1
    return contracts


def _argument_kinds(path, count, contracts):
    parts = path.split("::")
    name = parts[-1]
    standard = len(parts) > 1 and parts[0] in ("alloc", "core", "std")
    unshadowed = len(parts) == 1 and name not in contracts
    if name in _EXPRESSION_MACROS and (standard or unshadowed):
        return ("expr",) * count
    if count and (standard or unshadowed) and (
        name == "matches" or name.endswith("_matches")
    ):
        return ("expr",) + (None,) * (count - 1)
    project_contracts = contracts.get(name, ()) if len(parts) == 1 else ()
    applicable = [
        contract for contract in project_contracts
        if len(contract[0]) == count or contract[2]
    ]
    kinds = []
    for index in range(count):
        candidates = {
            signature[index] if len(signature) == count else uniform
            for signature, uniform, _ in applicable
        }
        kind = next(iter(candidates)) if len(candidates) == 1 else None
        kinds.append(kind)
    return tuple(kinds)


def _macro_path(code, bang):
    cursor = _previous_code_byte(code, bang) + 1
    parts = []
    while cursor > 0:
        start = cursor
        while start > 0:
            byte = code[start - 1]
            if (
                byte < 0x80
                and not chr(byte).isalnum()
                and byte not in (ord("_"), ord("#"))
            ):
                break
            start -= 1
        part = code[start:cursor].removeprefix(b"r#")
        if not part:
            break
        parts.append(part.decode())
        separator = _previous_code_byte(code, start)
        if separator < 1 or code[separator - 1 : separator + 1] != b"::":
            break
        cursor = _previous_code_byte(code, separator - 1) + 1
    return "::".join(reversed(parts))


def _macro_argument_ranges(code):
    return _merged_ranges(_delimited_ranges(code, _is_macro_open))


def _attribute_ranges(code):
    return _merged_ranges(_delimited_ranges(code, _is_attribute_open))


def _matches_pattern_ranges(code):
    pairs = {ord("("): ord(")"), ord("["): ord("]"), ord("{"): ord("}")}
    ranges = []
    for start, end in _delimited_ranges(code, _is_matches_open):
        stack = []
        separator = None
        for cursor in range(start, end):
            byte = code[cursor]
            if byte in pairs:
                stack.append(pairs[byte])
            elif stack and byte == stack[-1]:
                stack.pop()
            elif byte == ord(",") and not stack:
                separator = cursor
                break
        if separator is None:
            continue
        pattern_start = _skip_space(code, separator + 1)
        pattern_end = end
        while (
            pattern_end > pattern_start
            and code[pattern_end - 1 : pattern_end].isspace()
        ):
            pattern_end -= 1
        if code[pattern_end - 1 : pattern_end] == b",":
            pattern_end = _previous_code_byte(code, pattern_end - 1) + 1
        if pattern_start < pattern_end:
            ranges.append((pattern_start, pattern_end))
    return ranges


def _macro_argument_segments(code, contracts):
    ranges = []
    for start, end in _delimited_ranges(code, _is_macro_open):
        segments = _comma_segments(code, start, end)
        opening = start - 1
        bang = _previous_code_byte(code, opening)
        path = _macro_path(code, bang)
        kinds = _argument_kinds(path, len(segments), contracts)
        ranges.extend(
            (segment_start, segment_end, kind)
            for (segment_start, segment_end), kind in zip(segments, kinds)
        )
    return ranges


def _rust_syntax_ranges(sources, macro_contracts=None):
    if macro_contracts is None:
        macro_contracts = _macro_rule_contracts(sources)
    if len(sources) > 128:
        entries = list(sources.items())
        results = [
            _rust_syntax_ranges(
                dict(entries[offset : offset + 128]), macro_contracts
            )
            for offset in range(0, len(entries), 128)
        ]
        return tuple(
            {
                relative_path: ranges
                for result in results
                for relative_path, ranges in result[index].items()
            }
            for index in range(4)
        )
    callees = defaultdict(set)
    patterns = defaultdict(set)
    pattern_candidates = defaultdict(set)
    undetermined_patterns = defaultdict(set)
    imports = defaultdict(set)
    call_prefix = b"fn __cbm(){ __cbm("
    call_suffix = b"); }"
    pattern_prefix = b"fn __cbm(){ match __value { "
    pattern_suffix = b" => (), _ => () } }"
    with tempfile.TemporaryDirectory(prefix="cbm-rust-call-sites-") as temporary:
        inputs = {}
        temporary_path = Path(temporary)
        source_path = temporary_path / "sources"
        fragment_path = temporary_path / "fragments"
        source_path.mkdir()
        fragment_path.mkdir()
        input_number = 0

        def add_input(
            content, source_range, definite_pattern=False, allow_call=True,
            allow_pattern=True, full_pattern=False, full_source=False,
        ):
            nonlocal input_number
            directory = source_path if full_source else fragment_path
            path = directory / f"{input_number}.rs"
            path.write_bytes(content)
            inputs[str(path)] = (
                *source_range, definite_pattern, allow_call, allow_pattern,
                full_pattern,
            )
            input_number += 1

        for relative_path, source in sources.items():
            add_input(
                source, (relative_path, 0, len(source), 0), True,
                full_source=True,
            )
            code = _masked_source(source)
            token_ranges = set(_delimited_ranges(code, _is_macro_open))
            token_ranges.update(_delimited_ranges(code, _is_attribute_open))
            for start, end in sorted(token_ranges):
                add_input(
                    call_prefix + source[start:end] + call_suffix,
                    (relative_path, start, end, len(call_prefix)),
                    False,
                    True,
                    False,
                )
            for start, end in _matches_pattern_ranges(code):
                add_input(
                    pattern_prefix + source[start:end] + pattern_suffix,
                    (relative_path, start, end, len(pattern_prefix)),
                    True,
                    False,
                )
            for start, end, argument_kind in _macro_argument_segments(
                code, macro_contracts
            ):
                if argument_kind == "expr":
                    continue
                if argument_kind == "pat":
                    add_input(
                        pattern_prefix + source[start:end] + pattern_suffix,
                        (relative_path, start, end, len(pattern_prefix)),
                        True,
                        False,
                    )
                    continue
                segment = code[start:end]
                if (
                    b"=>" in segment
                    or b"=" in segment
                    or re.search(rb"\|\s*_\s*(?:,|\|)", segment)
                ):
                    continue
                add_input(
                    pattern_prefix + source[start:end] + pattern_suffix,
                    (relative_path, start, end, len(pattern_prefix)),
                    False,
                    False,
                    True,
                    True,
                )
        queries = (
            ("call", "--pattern", "$F($$$ARGS)", temporary_path),
            ("pattern", "--kind", "tuple_struct_pattern", temporary_path),
            ("import", "--kind", "use_declaration", source_path),
        )
        for query, option, value, query_path in queries:
            result = subprocess.run(
                (
                    "ast-grep", "run", "--lang", "rust", option, value,
                    "--json=stream", str(query_path),
                ),
                stdout=subprocess.PIPE,
                check=False,
            )
            if result.returncode not in (0, 1):
                raise subprocess.CalledProcessError(
                    result.returncode, result.args
                )
            for line in result.stdout.splitlines():
                record = json.loads(line)
                node = (
                    record["metaVariables"]["single"]["F"]
                    if query == "call"
                    else record
                )
                (
                    relative_path, source_start, source_end, wrapper_length,
                    definite_pattern, allow_call, allow_pattern, full_pattern,
                ) = inputs[record["file"]]
                if query == "call" and not allow_call:
                    continue
                if query == "pattern" and not allow_pattern:
                    continue
                if query == "import" and wrapper_length:
                    continue
                start = (
                    source_start
                    + node["range"]["byteOffset"]["start"]
                    - wrapper_length
                )
                end = (
                    source_start
                    + node["range"]["byteOffset"]["end"]
                    - wrapper_length
                )
                if not (source_start <= start and end <= source_end):
                    continue
                if query == "call":
                    text = node["text"]
                    callees[relative_path].add(
                        (
                            start,
                            end,
                            text.startswith("(") and text.endswith(")"),
                        )
                    )
                elif query == "pattern":
                    if full_pattern:
                        full_start = _skip_space(
                            sources[relative_path], source_start
                        )
                        full_end = source_end
                        while (
                            full_end > full_start
                            and sources[relative_path][
                                full_end - 1 : full_end
                            ].isspace()
                        ):
                            full_end -= 1
                        if (start, end) != (full_start, full_end):
                            continue
                    text = _masked_source(sources[relative_path][start:end])
                    pattern_only = re.search(
                        rb"(?<![A-Za-z0-9_])_(?![A-Za-z0-9_])|"
                        rb"(?:r#)?[A-Za-z_][A-Za-z0-9_]*\s*@",
                        text,
                    )
                    if definite_pattern:
                        patterns[relative_path].add((start, end))
                    elif pattern_only is not None:
                        pattern_candidates[relative_path].add((start, end))
                    else:
                        undetermined_patterns[relative_path].add((start, end))
                else:
                    imports[relative_path].add((start, end))
        for relative_path, items in pattern_candidates.items():
            patterns[relative_path].update(items)
    return (
        {relative_path: sorted(items) for relative_path, items in callees.items()},
        {
            relative_path: _merged_ranges(sorted(items))
            for relative_path, items in patterns.items()
        },
        {
            relative_path: _merged_ranges(sorted(items))
            for relative_path, items in undetermined_patterns.items()
        },
        {
            relative_path: _merged_ranges(sorted(items))
            for relative_path, items in imports.items()
        },
    )


def _rust_call_callees(sources):
    callees, _, _, _ = _rust_syntax_ranges(sources)
    return callees


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


def _self_constructor_symbol(symbol):
    parts = symbol.split(" ", 4)
    if len(parts) < 5:
        return None
    descriptor = parts[4]
    marker = "impl#["
    impl_start = descriptor.find(marker)
    if impl_start < 0:
        return None
    owner_start = impl_start + len(marker)
    owner_end = descriptor.find("]", owner_start)
    if owner_end < 0:
        return None
    owner = descriptor[owner_start:owner_end]
    return " ".join(parts[:4]) + " " + descriptor[:impl_start] + owner + "#"


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
    references, definition_sites, sources, records, project_packages, patterns,
    undetermined_patterns, imports,
):
    inventory_keys = {
        (site.relative_path, site.callee_start_byte, site.callee_end_byte)
        for site in records
    }
    counts = Counter()
    for relative_path, occurrences in references.items():
        grouped = defaultdict(set)
        for source_range, symbol in occurrences:
            if _DECODER.package_of(symbol) in project_packages:
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
        pattern_ranges = patterns.get(relative_path, ())
        pattern_starts = [start for start, _ in pattern_ranges]
        undetermined_ranges = undetermined_patterns.get(relative_path, ())
        undetermined_starts = [start for start, _ in undetermined_ranges]
        import_ranges = imports.get(relative_path, ())
        import_starts = [start for start, _ in import_ranges]
        for source_range in grouped:
            if source_range in definition_ranges:
                counts["excluded_definition_site"] += 1
                continue
            byte_range = _byte_range(source, starts, source_range)
            if byte_range is None:
                counts["excluded_invalid_range"] += 1
                continue
            start, end = byte_range
            symbols = grouped[source_range]
            truth_symbols = symbols
            if source[start:end] == b"Self" and len(symbols) == 1:
                self_constructor = _self_constructor_symbol(next(iter(symbols)))
                if self_constructor is not None:
                    truth_symbols = {self_constructor}
            pattern_constructor = any(
                _constructor_symbol(symbol)
                or _self_constructor_symbol(symbol) is not None
                for symbol in symbols
            )
            if (
                pattern_constructor
                and _inside_ranges(pattern_ranges, pattern_starts, start)
            ):
                counts["excluded_pattern"] += 1
                continue
            if pattern_constructor and _inside_ranges(
                undetermined_ranges, undetermined_starts, start
            ):
                counts["excluded_macro_argument_tuple_undetermined"] += 1
                continue
            if (relative_path, start, end) in inventory_keys:
                counts["inventory_found"] += 1
                continue
            if _previous_identifier(code, start) == b"fn":
                counts["excluded_function_definition"] += 1
                continue
            if _inside_ranges(import_ranges, import_starts, start):
                counts["excluded_import_or_reexport"] += 1
                continue
            descriptors = [_DECODER.descriptor_of(item) for item in truth_symbols]
            if (
                not re.fullmatch(
                    rb"(?:r#)?[A-Za-z_][A-Za-z0-9_]*", source[start:end]
                )
                and any(item.endswith("().") for item in descriptors)
            ):
                counts["excluded_operator_resolution_reference"] += 1
                continue
            if all(item.endswith("#") for item in descriptors):
                counts["excluded_type_or_variant_reference"] += 1
                continue
            if not all(item.endswith("().") for item in descriptors):
                endings = {item[-1:] for item in descriptors}
                ending = next(iter(endings)) if len(endings) == 1 else None
                category = {
                    "/": "excluded_namespace_reference",
                    ".": "excluded_value_reference",
                    "]": "excluded_type_parameter_or_impl_reference",
                    "!": "excluded_macro_reference",
                    ":": "excluded_trait_reference",
                }.get(ending, "excluded_ambiguous_project_reference")
                counts[category] += 1
                continue
            if not _callee_position(code, end):
                counts["excluded_function_reference_not_invoked"] += 1
                continue
            if _inside_ranges(attribute_ranges, attribute_starts, start):
                counts["excluded_attribute_syntax"] += 1
            elif _inside_ranges(macro_ranges, macro_starts, start):
                counts["excluded_macro_argument_call_parser_failure"] += 1
            else:
                counts["excluded_call_parser_failure"] += 1
    categories = (
        "inventory_found",
        "excluded_pattern",
        "excluded_macro_argument_tuple_undetermined",
        "excluded_function_definition",
        "excluded_import_or_reexport",
        "excluded_function_reference_not_invoked",
        "excluded_type_or_variant_reference",
        "excluded_namespace_reference",
        "excluded_value_reference",
        "excluded_type_parameter_or_impl_reference",
        "excluded_macro_reference",
        "excluded_trait_reference",
        "excluded_ambiguous_project_reference",
        "excluded_operator_resolution_reference",
        "excluded_macro_argument_call_parser_failure",
        "excluded_call_parser_failure",
        "excluded_attribute_syntax",
        "excluded_definition_site",
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
    (
        parsed_callees,
        parsed_patterns,
        undetermined_patterns,
        import_ranges,
    ) = _rust_syntax_ranges(sources)
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
        pattern_ranges = parsed_patterns.get(relative_path, ())
        pattern_starts = [start for start, _ in pattern_ranges]
        undetermined_ranges = undetermined_patterns.get(relative_path, ())
        undetermined_starts = [start for start, _ in undetermined_ranges]
        grouped = defaultdict(set)
        for source_range, symbol in occurrences:
            grouped[source_range].add(symbol)
        starts = _line_starts(source)
        for source_range, symbols in grouped.items():
            byte_range = _byte_range(source, starts, source_range)
            if byte_range is None:
                continue
            start, end = byte_range
            truth_symbols = symbols
            if source[start:end] == b"Self" and len(symbols) == 1:
                self_constructor = _self_constructor_symbol(next(iter(symbols)))
                if self_constructor is not None:
                    truth_symbols = {self_constructor}
            constructor = len(truth_symbols) == 1 and _constructor_symbol(
                next(iter(truth_symbols))
            )
            in_macro = _inside_ranges(macro_ranges, macro_starts, start)
            in_attribute = _inside_ranges(
                attribute_ranges, attribute_starts, start
            )
            if in_attribute and not any(_callable_symbol(item) for item in symbols):
                continue
            if not _invoked_leaf(code, start, end):
                continue
            if (
                constructor
                and _inside_ranges(pattern_ranges, pattern_starts, start)
            ):
                continue
            if constructor and _inside_ranges(
                undetermined_ranges, undetermined_starts, start
            ):
                continue
            matches = [
                callee
                for callee in parsed_callees.get(relative_path, ())
                if callee[0] <= start and end <= callee[1]
            ]
            if not matches:
                if (
                    not (in_macro or in_attribute)
                    or _previous_identifier(code, start) == b"fn"
                ):
                    continue
                parenthesized = False
            else:
                _, _, parenthesized = min(
                    matches, key=lambda callee: callee[1] - callee[0]
                )
            records.append(
                CallSite(
                    relative_path,
                    start,
                    end,
                    _truth(truth_symbols, project_packages),
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
        references, definition_sites, sources, records, project_packages,
        parsed_patterns, undetermined_patterns, import_ranges,
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
    definitions_by_symbol = defaultdict(list)
    definitions_by_symbol_path = defaultdict(list)
    definition_locations = defaultdict(list)
    for relative_path, entries in definition_sites.items():
        for definition_range, enclosing_range, symbol in entries:
            identity = DefinitionIdentity(symbol, relative_path, definition_range)
            definitions_by_symbol[symbol].append((identity, enclosing_range))
            definitions_by_symbol_path[(symbol, relative_path)].append(
                (identity, enclosing_range)
            )
            definition_locations[symbol].append((relative_path, definition_range))
    identities_by_symbol = {
        symbol: frozenset(identity for identity, _ in entries)
        for symbol, entries in definitions_by_symbol.items()
    }
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
                position = (source_range[0], source_range[1])
                caller_symbol = _DECODER.innermost(
                    definitions[relative_path], (source_range[0], source_range[1])
                )
                caller_candidates = [
                    (identity, enclosing_range)
                    for identity, enclosing_range in definitions_by_symbol_path.get(
                        (caller_symbol, relative_path), ()
                    )
                    if (enclosing_range[0], enclosing_range[1]) <= position
                    <= (enclosing_range[2], enclosing_range[3])
                ]
                caller_identity = None
                caller_scope = None
                if caller_candidates:
                    smallest = min(
                        (enclosing[2] - enclosing[0], enclosing[3] - enclosing[1])
                        for _, enclosing in caller_candidates
                    )
                    closest = {
                        identity
                        for identity, enclosing in caller_candidates
                        if (
                            enclosing[2] - enclosing[0],
                            enclosing[3] - enclosing[1],
                        ) == smallest
                    }
                    if len(closest) == 1:
                        caller_identity = next(iter(closest))
                        caller_scope = next(
                            enclosing
                            for identity, enclosing in caller_candidates
                            if identity == caller_identity
                        )
                if caller_symbol is not None:
                    callers[key] = OracleDefinition(
                        caller_symbol, caller_identity
                    )

                target_entries = definitions_by_symbol_path.get(
                    (target, relative_path), ()
                )
                target_identities = identities_by_symbol.get(target, ())
                target_identity = None
                if len(target_identities) == 1:
                    target_identity = next(iter(target_identities))
                else:
                    same_file = {
                        identity
                        for identity, _ in target_entries
                    }
                    if len(same_file) == 1:
                        target_identity = next(iter(same_file))
                    else:
                        caller_local = {
                            identity
                            for identity in same_file
                            if caller_scope is not None
                            and (caller_scope[0], caller_scope[1])
                            <= (
                                identity.definition_range[0],
                                identity.definition_range[1],
                            )
                            <= (caller_scope[2], caller_scope[3])
                        }
                        enclosing = {
                            identity
                            for identity, owner_range in target_entries
                            if (owner_range[0], owner_range[1]) <= position
                            <= (owner_range[2], owner_range[3])
                        }
                        if len(caller_local) == 1:
                            target_identity = next(iter(caller_local))
                        elif len(enclosing) == 1:
                            target_identity = next(iter(enclosing))
                targets[key].add(OracleDefinition(target, target_identity))
    exact_targets = {
        key: next(iter(definitions))
        for key, definitions in targets.items()
        if len(definitions) == 1
    }
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
        if outcome.definition_path is None or outcome.definition_range is None:
            return None, f"{endpoint}_definition_location_missing", node
        return DefinitionIdentity(
            outcome.symbol, outcome.definition_path, outcome.definition_range
        ), "-", node
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
    with tempfile.TemporaryDirectory(prefix="cbm-rust-callers-") as temporary:
        inputs = {}
        temporary_path = Path(temporary)
        for input_number, (relative_path, source) in enumerate(sources.items()):
            path = temporary_path / f"{input_number}.rs"
            path.write_bytes(source)
            inputs[str(path)] = (relative_path, source)
        result = subprocess.run(
            (
                "ast-grep", "run", "--lang", "rust", "--kind",
                "function_item", "--json=stream", str(temporary_path),
            ),
            stdout=subprocess.PIPE,
            check=False,
        )
        if result.returncode not in (0, 1):
            raise subprocess.CalledProcessError(result.returncode, result.args)
        for line in result.stdout.splitlines():
            record = json.loads(line)
            span = record["range"]
            start = span["byteOffset"]["start"]
            end = span["byteOffset"]["end"]
            relative_path, source = inputs[record["file"]]
            match = re.search(r"\bfn\s+(?:r#)?([^\W\d]\w*)", record["text"])
            if match is not None:
                functions[relative_path].append(
                    (
                        start, end, match.group(1),
                        source.count(b"\n", 0, start) + 1,
                        source.count(b"\n", 0, end) + 1,
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


def _score_caller(call, oracle_caller, source_caller, identities):
    caller_identity, caller_reason, caller_node = _single_identity(
        identities, call.caller_qualified_name, "caller"
    )
    caller_symbol = caller_identity.symbol if caller_identity is not None else None
    oracle_caller_symbol = oracle_caller.symbol if oracle_caller is not None else None
    oracle_caller_identity = (
        oracle_caller.identity if oracle_caller is not None else None
    )
    if caller_reason == "caller_not_in_oracle_configuration:cfg_inactive":
        return CallerOutcome(
            caller_symbol, None, "unscorable", "caller_cfg_inactive",
            caller_identity, None,
        )
    if _callable_symbol(oracle_caller_symbol or ""):
        if caller_identity is None:
            if _source_caller_matches(call, source_caller, caller_node) is False:
                return CallerOutcome(
                    caller_symbol, oracle_caller_symbol, "wrong",
                    "caller_source_mismatch",
                    caller_identity, oracle_caller_identity,
                )
            return CallerOutcome(
                caller_symbol, oracle_caller_symbol, "unscorable", caller_reason,
                caller_identity, oracle_caller_identity,
            )
        if oracle_caller_identity is None:
            return CallerOutcome(
                caller_symbol, oracle_caller_symbol, "unscorable",
                "caller_oracle_definition_location_missing", caller_identity, None,
            )
        if caller_identity != oracle_caller_identity:
            return CallerOutcome(
                caller_symbol, oracle_caller_symbol, "wrong",
                "caller_identity_mismatch",
                caller_identity, oracle_caller_identity,
            )
        return CallerOutcome(
            caller_symbol, oracle_caller_symbol, "correct", "-",
            caller_identity, oracle_caller_identity,
        )
    source_match = _source_caller_matches(call, source_caller, caller_node)
    if source_match is True:
        category = caller_reason.rpartition(":")[2]
        if caller_symbol is not None:
            category = "non_callable_oracle"
        return CallerOutcome(
            caller_symbol, caller_symbol, "correct", f"source_caller:{category}",
            caller_identity, caller_identity,
        )
    if source_match is False:
        return CallerOutcome(
            caller_symbol, None, "wrong", "caller_source_mismatch",
            caller_identity, None,
        )
    reason = caller_reason if caller_symbol is None else "source_caller_missing"
    return CallerOutcome(
        caller_symbol, None, "unscorable", reason, caller_identity, None
    )


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
    if len({relative_path for relative_path, _ in locations}) > 1:
        locations = tuple(
            location for location in locations
            if location[0] == site.relative_path
        )
        if len(locations) != 1:
            return None
    _, _, _, node_path, start_line, end_line = target_node
    return not any(
        (
            node_path == relative_path
            or node_path.endswith("/" + relative_path)
        )
        and start_line - 1 <= definition_range[0] <= end_line - 1
        for relative_path, definition_range in locations
    )


def _compare_target_identity(
    site, expected_target, expected_identity, target_identity, target_reason,
    target_node, definition_locations,
):
    if expected_identity is not None:
        if target_identity is not None:
            if target_identity == expected_identity:
                return target_identity, expected_identity, "correct", "-"
            return (
                target_identity, expected_identity, "wrong",
                "target_identity_mismatch",
            )
        if _target_is_contradicted(
            site, expected_target, target_reason, target_node, definition_locations
        ):
            return None, expected_identity, "wrong", "target_identity_mismatch"
        return None, expected_identity, "unscorable", target_reason

    locations = definition_locations.get(expected_target, ())
    if locations:
        contradicted = _target_is_contradicted(
            site, expected_target, target_reason, target_node, definition_locations
        )
        if contradicted is True:
            return target_identity, None, "wrong", "target_identity_mismatch"
        return target_identity, None, "unscorable", "oracle_target_location_ambiguous"
    if target_identity is not None:
        if target_identity.symbol != expected_target:
            return target_identity, None, "wrong", "target_identity_mismatch"
        return target_identity, target_identity, "correct", "-"

    outcome = target_node[0] if target_node is not None else None
    if (
        target_reason in (
            "target_oracle_gap:oracle_definition_missing",
            "target_oracle_gap:oracle_document_missing",
        )
        and outcome.definition_path is not None
        and outcome.definition_range is not None
    ):
        identity = DefinitionIdentity(
            expected_target, outcome.definition_path, outcome.definition_range
        )
        return identity, identity, "correct", "-"
    if _target_is_contradicted(
        site, expected_target, target_reason, target_node, definition_locations
    ):
        return None, None, "wrong", "target_identity_mismatch"
    return None, None, "unscorable", target_reason


def _score_edge(
    call, site, oracle_target, caller, identities, definition_locations,
):
    target_identity, target_reason, target_node = _single_identity(
        identities, call.target_qualified_name, "target"
    )
    target_symbol = target_identity.symbol if target_identity is not None else None
    if site is None:
        return ScoredEdge(
            call.relative_path, call.callee_start_byte, call.callee_end_byte,
            call.caller_qualified_name, caller.graph_symbol,
            call.target_qualified_name,
            target_symbol, call.strategy, "no_oracle_site", None, None,
            "unscorable", "no_oracle_truth",
            caller.graph_identity, caller.oracle_identity, target_identity, None,
        )
    oracle_target_symbol = oracle_target.symbol if oracle_target is not None else None
    expected_target = (
        oracle_target_symbol
        if site.resolved_symbol == EXTERNAL
        else site.resolved_symbol
    )
    expected_target_identity = None
    if oracle_target is not None and oracle_target.symbol == expected_target:
        expected_target_identity = oracle_target.identity
    if caller.reason == "caller_cfg_inactive":
        return ScoredEdge(
            call.relative_path, call.callee_start_byte, call.callee_end_byte,
            call.caller_qualified_name, caller.graph_symbol,
            call.target_qualified_name,
            target_symbol, call.strategy, site.call_form, caller.oracle_symbol,
            expected_target, "unscorable", "caller_cfg_inactive",
            caller.graph_identity, caller.oracle_identity, target_identity,
            expected_target_identity,
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
    elif expected_target is None:
        unknown.append("oracle_target_missing")
    else:
        target_identity, expected_target_identity, status, target_reason = (
            _compare_target_identity(
                site, expected_target, expected_target_identity, target_identity,
                target_reason, target_node, definition_locations,
            )
        )
        target_symbol = (
            target_identity.symbol if target_identity is not None else None
        )
        if status == "wrong":
            contradictions.append(target_reason)
        elif status == "unscorable":
            unknown.append(target_reason)
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
        caller.graph_identity, caller.oracle_identity, target_identity,
        expected_target_identity,
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
    graph_names_by_identity = defaultdict(set)
    for edge in graph_calls:
        caller_identity, _, _ = _single_identity(
            identities, edge.caller_qualified_name, "caller"
        )
        target_identity, _, _ = _single_identity(
            identities, edge.target_qualified_name, "target"
        )
        if caller_identity is not None and target_identity is not None:
            graph_relations.add((caller_identity, target_identity))
    for qualified_name in identities:
        identity, _, _ = _single_identity(identities, qualified_name, "node")
        if identity is not None:
            graph_names_by_identity[identity].add(qualified_name)
    edges = []
    scored_occurrences = []
    integrity = list(integrity)
    source_candidate_keys = {
        key for key in sites
        if not _callable_symbol(
            oracle_callers[key].symbol if key in oracle_callers else ""
        )
    }
    for call in calls:
        key = (call.relative_path, call.callee_start_byte, call.callee_end_byte)
        if key not in sites or not _callable_symbol(
            oracle_callers[key].symbol if key in oracle_callers else ""
        ):
            continue
        caller_identity, caller_reason, _ = _single_identity(
            identities, call.caller_qualified_name, "caller"
        )
        if caller_identity is None and caller_reason != (
            "caller_not_in_oracle_configuration:cfg_inactive"
        ):
            source_candidate_keys.add(key)
    source_candidates = [sites[key] for key in source_candidate_keys]
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
            caller_identity, _, _ = _single_identity(
                identities, edge.caller_qualified_name, "caller"
            )
            target_identity, _, _ = _single_identity(
                identities, edge.target_qualified_name, "target"
            )
            edges.append(
                ScoredEdge(
                    edge.source_path, -1, -1, edge.caller_qualified_name,
                    caller_identity.symbol if caller_identity is not None else None,
                    edge.target_qualified_name,
                    target_identity.symbol if target_identity is not None else None,
                    "no_dump_provenance", "no_provenance", None, None,
                    "unscorable", "edge_without_dump",
                    caller_identity, None, target_identity, None,
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
        rank = {"correct": 0, "wrong": 1, "unscorable": 2}
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
        if _callable_symbol(raw_caller.symbol if raw_caller is not None else ""):
            caller = None
            if site_callers:
                rank = {"correct": 0, "unscorable": 1, "wrong": 2}
                caller = min(site_callers, key=lambda item: rank[item.outcome])
            if caller is not None and caller.reason == "caller_cfg_inactive":
                continue
            oracle_caller_symbol = raw_caller.symbol
            oracle_caller_identity = raw_caller.identity
            caller_category = "scip_callable"
            if caller is not None and caller.outcome == "unscorable":
                gap = caller.reason.removeprefix("caller_oracle_gap:")
                caller_category = f"caller_gap:{gap}"
            found = any(edge.outcome == "correct" for edge in site_edges)
            oracle_target = oracle_targets.get(key)
            oracle_target_identity = (
                oracle_target.identity if oracle_target is not None else None
            )
            if (
                not found
                and oracle_caller_identity is not None
                and oracle_target_identity is not None
            ):
                found = (
                    oracle_caller_identity, oracle_target_identity
                ) in graph_relations
        else:
            oracle_caller_identity = None
            source_nodes = source_nodes_by_site.get(key, ())
            outcomes = [node[0] for _, node in source_nodes]
            if outcomes and all(
                outcome is not None
                and outcome.status == "not_in_oracle_configuration"
                for outcome in outcomes
            ):
                continue
            source_caller = source_callers.get(key)
            if source_caller is None:
                oracle_caller_symbol = "caller_gap:no_enclosing_function_item"
                caller_category = "caller_gap:no_enclosing_function_item"
            else:
                start, end, name, _, _ = source_caller
                oracle_caller_symbol = (
                    f"source:{site.relative_path}:{start}:{end}:{name}"
                )
                caller_category = "source_function_item"
            oracle_target = oracle_targets.get(key)
            oracle_target_identity = (
                oracle_target.identity if oracle_target is not None else None
            )
            target_names = graph_names_by_identity.get(
                oracle_target_identity, ()
            )
            found = any(
                (caller_name, target_name) in graph_pairs
                for caller_name, _ in source_nodes
                for target_name in target_names
            )
        strategies = sorted({edge.strategy for edge in site_edges})
        strategy = "+".join(strategies) if strategies else "no_published_edge"
        target_category = (
            "scip_definition"
            if oracle_target_identity is not None
            else (
                "oracle_location_ambiguous"
                if definition_locations.get(site.resolved_symbol)
                else NO_ORACLE_LOCATION
            )
        )
        recall.append(
            RecallOccurrence(
                site.relative_path, site.callee_start_byte, site.callee_end_byte,
                oracle_caller_symbol, caller_category, site.resolved_symbol,
                site.call_form, strategy, "found" if found else "missed",
                oracle_caller_identity, oracle_target_identity, target_category,
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


def recall_relation_key(occurrence):
    """Return the declaration-exact caller/target key for one truth occurrence."""
    caller = occurrence.oracle_caller_identity or occurrence.oracle_caller_symbol
    target = occurrence.oracle_target_identity
    if target is None:
        target = (occurrence.oracle_symbol, occurrence.target_category)
    return caller, target


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
        "call_form", "oracle_caller_symbol", "caller_category", "oracle_symbol",
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
            occurrence.oracle_caller_symbol or "-", occurrence.caller_category,
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
