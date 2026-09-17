#!/usr/bin/env python3
"""Diff codebase-memory graph edges against a rust-analyzer LSIF gold standard.

Reproduces the 2026-09-17 comparison (see ISSUES.md entries of that date).
Ground truth: pm-lsif.json.gz in this directory — built with
`rust-analyzer lsif .` (rust-analyzer 1.95.0, 59807616 2026-04-14) at the
project-management workspace root, commit dd7ec1b2e, clean tracked tree.
The CBM side queries the live index; to reproduce the measured numbers the
index must be built from the same commit.

Usage: python3 compare.py [path-to-lsif.json[.gz]]
Requires: codebase-memory-mcp on PATH with the
Users-viktor-Projects-project-management project indexed.
"""
import collections
import gzip
import json
import re
import subprocess
import sys

PROJECT = 'Users-viktor-Projects-project-management'
ROOT = 'file:///Users/viktor/Projects/project-management/'
CRATES = ['pm-usecase', 'pm-validate', 'pm-graph', 'pm-domain',
          'pm-cli-grammar', 'pm-infra']
SEMANTIC = {'CALLS', 'USAGE', 'IMPORTS', 'OVERRIDE', 'IMPLEMENTS', 'WRITES',
            'CALL_REFERENCE', 'HTTP_CALLS', 'TESTS'}


def keyof(path):
    m = re.match(r'crates/([^/]+)/src/(?:sqlite/|usecase/)?([^/.]+)', path)
    if not m:
        return None
    crate, mod = m.group(1), m.group(2)
    if mod in ('lib', 'main', 'sqlite', 'usecase'):
        return (crate, '<root>')
    return (crate, mod)


def cbm_edges(crate):
    """All semantic-type module-pair edges CBM holds inside one crate."""
    q = (f"MATCH (a)-[r]->(b) WHERE a.file_path CONTAINS 'crates/{crate}/src/' "
         f"AND b.file_path CONTAINS 'crates/{crate}/src/' "
         f"RETURN DISTINCT a.file_path, b.file_path, type(r) LIMIT 20000")
    out = subprocess.run(
        ['codebase-memory-mcp', 'cli', 'query_graph', '--project', PROJECT,
         '--query', q, '--max-rows', '20000'],
        capture_output=True, text=True, check=True).stdout
    refs, rows_started = {}, False
    pairs = collections.defaultdict(set)
    for line in out.splitlines():
        m = re.match(r'  (\d+) (\S+)$', line)
        if m and not rows_started:
            refs[m.group(1)] = m.group(2)
            continue
        if line.startswith('rows:'):
            rows_started = True
            continue
        if rows_started and line.startswith('  '):
            parts = line.strip().split()
            if len(parts) != 3:
                continue

            def expand(t):
                m2 = re.match(r'@(\d+)\+(.*)$', t)
                return refs[m2.group(1)] + m2.group(2) if m2 else t

            a, b, et = expand(parts[0]), expand(parts[1]), parts[2]
            if et not in SEMANTIC:
                continue
            ka, kb = keyof(a), keyof(b)
            if (ka and kb and ka != kb
                    and ka[1] != '<root>' and kb[1] != '<root>'):
                pairs[(ka[1], kb[1])].add(et)
    return pairs


def lsif_edges(path):
    """Module-pair reference edges (ref module -> def module) per crate."""
    docs = {}
    ref_items = collections.defaultdict(lambda: ([], []))
    opener = gzip.open if path.endswith('.gz') else open
    with opener(path, 'rt') as fh:
        for line in fh:
            if '"document"' not in line and '"uri"' not in line:
                continue
            o = json.loads(line)
            if o.get('type') == 'vertex' and o.get('label') == 'document':
                docs[o['id']] = o['uri']
            elif o.get('type') == 'edge' and o.get('label') == 'item':
                p = o.get('property')
                d = o.get('shard', o.get('document'))
                if p == 'definitions':
                    ref_items[o['outV']][0].append(d)
                elif p == 'references':
                    ref_items[o['outV']][1].append(d)

    def lkey(did):
        u = docs.get(did, '')
        return keyof(u[len(ROOT):]) if u.startswith(ROOT) else None

    per_crate = {c: collections.Counter() for c in CRATES}
    for defs, refs_ in ref_items.values():
        dset = {lkey(d) for d in defs} - {None}
        rset = {lkey(d) for d in refs_} - {None}
        for dk in dset:
            for rk in rset:
                if (rk != dk and rk[0] == dk[0] and rk[0] in CRATES
                        and rk[1] != '<root>' and dk[1] != '<root>'):
                    per_crate[rk[0]][(rk[1], dk[1])] += 1
    return per_crate


def main():
    lsif_path = sys.argv[1] if len(sys.argv) > 1 else 'pm-lsif.json.gz'
    lsif = lsif_edges(lsif_path)
    totals = collections.Counter()
    for c in CRATES:
        cbm = cbm_edges(c)
        cset, lset = set(cbm), set(lsif[c])
        both, conly, lonly = cset & lset, cset - lset, lset - cset
        totals.update(lsif=len(lset), cbm=len(cset), agree=len(both))
        print(f"=== {c}: LSIF={len(lset)} CBM={len(cset)} agree={len(both)} "
              f"CBM-only={len(conly)} LSIF-only={len(lonly)}")
        for a, b in sorted(conly):
            print(f"   CBM-only : {a} -> {b}  [{','.join(sorted(cbm[(a, b)]))}]")
        for a, b in sorted(lonly):
            print(f"   LSIF-only: {a} -> {b}  ({lsif[c][(a, b)]} refs)")
    print(f"\nTOTAL: LSIF={totals['lsif']} CBM={totals['cbm']} "
          f"agree={totals['agree']}  "
          f"recall={totals['agree'] / totals['lsif']:.0%} "
          f"precision={totals['agree'] / totals['cbm']:.0%}")


if __name__ == '__main__':
    main()
