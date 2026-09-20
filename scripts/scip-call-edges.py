#!/usr/bin/env python3
"""Derive call-edge counts from a rust-analyzer SCIP index, without the scip CLI.

Used as the independent oracle for the 2026-09-20 findings in ISSUES.md.

    rust-analyzer scip <repo> --output index.scip
    scripts/scip-call-edges.py index.scip <repo>

Method: occurrences with symbol_roles & 1 are definitions and contribute their
enclosing_range (Occurrence field 7, falling back to the occurrence range) to a
per-file interval list. Every non-definition occurrence is attributed to the
innermost definition interval containing its start position, producing
(caller, target, path) triples. A target is call-like when its descriptor ends
in "()." and free-function when that descriptor also contains no "#". Crate
identity comes from the "rust-analyzer cargo <crate> <version>" symbol prefix,
intersected with the workspace names reported by `cargo metadata --no-deps`.

Known limitation: a "()." target also matches a function referenced without
being invoked -- passed as a value, or named in a `use`. Every count below is
therefore an upper bound on true call edges.
"""

import json
import re
import subprocess
import sys
from collections import defaultdict

DEFINITION_ROLE = 0x1


def varint(buf, i):
    result = shift = 0
    while True:
        byte = buf[i]
        i += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, i
        shift += 7


def fields(buf, start, end):
    i = start
    while i < end:
        key, i = varint(buf, i)
        number, wire = key >> 3, key & 7
        if wire == 0:
            value, i = varint(buf, i)
            yield number, ("varint", value)
        elif wire == 2:
            length, i = varint(buf, i)
            yield number, ("bytes", buf[i : i + length])
            i += length
        elif wire == 5:
            yield number, ("fixed", buf[i : i + 4])
            i += 4
        elif wire == 1:
            yield number, ("fixed", buf[i : i + 8])
            i += 8
        else:
            raise ValueError(f"unsupported wire type {wire}")


def packed_ints(buf):
    out, i = [], 0
    while i < len(buf):
        value, i = varint(buf, i)
        out.append(value)
    return out


def norm_range(values):
    if len(values) == 3:
        return (values[0], values[1], values[0], values[2])
    return (values[0], values[1], values[2], values[3])


def parse(path):
    data = open(path, "rb").read()
    definitions = defaultdict(list)
    references = defaultdict(list)
    documents = 0
    for number, (kind, payload) in fields(data, 0, len(data)):
        if number != 2 or kind != "bytes":
            continue
        documents += 1
        doc_path, occurrences = None, []
        for dnum, (dkind, dpayload) in fields(payload, 0, len(payload)):
            if dnum == 1 and dkind == "bytes":
                doc_path = dpayload.decode()
            elif dnum == 2 and dkind == "bytes":
                occurrences.append(dpayload)
        if doc_path is None:
            continue
        for occ in occurrences:
            rng = symbol = enclosing = None
            roles = 0
            for onum, (okind, opayload) in fields(occ, 0, len(occ)):
                if onum == 1 and okind == "bytes":
                    rng = packed_ints(opayload)
                elif onum == 2 and okind == "bytes":
                    symbol = opayload.decode("utf8", "replace")
                elif onum == 3 and okind == "varint":
                    roles = opayload
                elif onum == 7 and okind == "bytes":
                    enclosing = packed_ints(opayload)
            if symbol is None or rng is None:
                continue
            if roles & DEFINITION_ROLE:
                definitions[doc_path].append((norm_range(enclosing or rng), symbol))
            else:
                references[doc_path].append((norm_range(rng), symbol))
    return documents, definitions, references


def innermost(intervals, position):
    best = best_size = None
    for (start_line, start_col, end_line, end_col), symbol in intervals:
        if (start_line, start_col) <= position <= (end_line, end_col):
            size = (end_line - start_line, end_col - start_col)
            if best_size is None or size < best_size:
                best, best_size = symbol, size
    return best


def crate_of(symbol):
    match = re.match(r"rust-analyzer cargo ([^\s]+) ", symbol)
    return match.group(1) if match else None


def descriptor_of(symbol):
    parts = symbol.split(" ", 4)
    return parts[4] if len(parts) > 4 else ""


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    scip_path, repo = sys.argv[1], sys.argv[2]
    documents, definitions, references = parse(scip_path)

    edges = set()
    for path, refs in references.items():
        intervals = definitions[path]
        for (rng, symbol) in refs:
            caller = innermost(intervals, (rng[0], rng[1]))
            if caller and caller != symbol:
                edges.add((caller, symbol, path))

    metadata = json.loads(
        subprocess.run(
            ["cargo", "metadata", "--no-deps", "--format-version", "1"],
            cwd=repo,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
    )
    workspace = {package["name"] for package in metadata["packages"]}

    def is_call(symbol):
        return descriptor_of(symbol).endswith("().")

    def is_free_function(symbol):
        descriptor = descriptor_of(symbol)
        return descriptor.endswith("().") and "#" not in descriptor

    defining_file = {}
    for path, entries in definitions.items():
        for _, symbol in entries:
            defining_file.setdefault(symbol, path)

    call_like = [e for e in edges if is_call(e[1])]
    internal = [e for e in call_like if crate_of(e[0]) in workspace and crate_of(e[1]) in workspace]
    cross_crate = [e for e in internal if crate_of(e[0]) != crate_of(e[1])]
    same_file_free = [
        e
        for e in edges
        if is_free_function(e[0])
        and is_free_function(e[1])
        and crate_of(e[0]) in workspace
        and crate_of(e[1]) in workspace
        and defining_file.get(e[0]) == e[2]
        and defining_file.get(e[1]) == e[2]
    ]

    print(f"documents:                      {documents}")
    print(f"workspace packages:             {len(workspace)}")
    print(f"attributed reference edges:     {len(edges)}")
    print(f"call-like edges:                {len(call_like)}")
    print(f"workspace-internal call edges:  {len(internal)}")
    print(f"  cross-crate:                  {len(cross_crate)}")
    print(f"  same-crate:                   {len(internal) - len(cross_crate)}")
    print(f"same-file free-function calls:  {len(same_file_free)}")


if __name__ == "__main__":
    main()
