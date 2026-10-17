#!/usr/bin/env python3
"""Exact edge-set difference between two graph databases.

Usage: edgediff.py BASE.db NEW.db OUTDIR
Key per edge: (source qualified_name, target qualified_name, edge type), with multiplicity.
Writes OUTDIR/removed.tsv and OUTDIR/added.tsv (type, source, target, count) and prints
per-type counts: `TYPE removed=N added=M`.
"""
import collections, os, sqlite3, sys

def edges(path):
    with sqlite3.connect(f'file:{path}?mode=ro', uri=True) as db:
        db.text_factory = lambda b: b.decode('utf-8', errors='surrogateescape')
        return collections.Counter(db.execute(
            'select s.qualified_name, t.qualified_name, e.type from edges e '
            'join nodes s on s.id = e.source_id join nodes t on t.id = e.target_id'))

base, new, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
b, n = edges(base), edges(new)
removed, added = b - n, n - b
for name, c in (('removed', removed), ('added', added)):
    with open(os.path.join(out, f'{name}.tsv'), 'w', errors='surrogateescape') as f:
        for (s, t, k), cnt in sorted(c.items(), key=lambda x: (x[0][2], x[0][0], x[0][1])):
            f.write(f'{k}\t{s}\t{t}\t{cnt}\n')
types = sorted({k for (_, _, k) in removed} | {k for (_, _, k) in added})
print(f'edges base={sum(b.values())} new={sum(n.values())} removed={sum(removed.values())} added={sum(added.values())}')
for k in types:
    r = sum(v for (_, _, kk), v in removed.items() if kk == k)
    a = sum(v for (_, _, kk), v in added.items() if kk == k)
    print(f'{k} removed={r} added={a}')
