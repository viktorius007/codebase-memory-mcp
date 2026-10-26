#!/usr/bin/env python3
"""graphcmp.py BEFORE.db AFTER.db -- exact comparison of the complete node and edge sets.

Node key: every column except the row id (project, label, name, qualified_name,
file_path, start_line, end_line, properties). Edge key: source and target node keys
(by qualified_name) plus type and properties. Multisets; prints counts, a digest of
each set, and the first differences.

SEMANTICALLY_RELATED edges are derived from the CALLS neighbourhood, so any CALLS change
moves them corpus-wide. They are compared as their own class (line "semantic:") and left out
of the structural "edges:" comparison; they never decide the verdict.
"""
import collections, hashlib, sqlite3, sys


SEM = "SEMANTICALLY_RELATED"


def load(path):
    db = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
    db.text_factory = lambda b: b.decode('utf-8', errors='surrogateescape')
    nodes = collections.Counter(db.execute(
        'select project, label, name, qualified_name, file_path, start_line, end_line, properties '
        'from nodes'))
    edges = collections.Counter(db.execute(
        'select e.project, s.qualified_name, t.qualified_name, e.type, e.properties from edges e '
        'join nodes s on s.id = e.source_id join nodes t on t.id = e.target_id'))
    sem = collections.Counter({k: n for k, n in edges.items() if k[3] == SEM})
    edges = collections.Counter({k: n for k, n in edges.items() if k[3] != SEM})
    return nodes, edges, sem


def digest(counter):
    h = hashlib.sha256()
    for row, n in sorted(counter.items(), key=lambda x: repr(x)):
        h.update(repr((row, n)).encode('utf-8', errors='surrogateescape'))
    return h.hexdigest()[:16]


bn, be, bs = load(sys.argv[1])
an, ae, as_ = load(sys.argv[2])
same = True
for name, b, a in (('nodes', bn, an), ('edges', be, ae), ('semantic', bs, as_)):
    removed, added = b - a, a - b
    print(f'{name}: before={sum(b.values())} after={sum(a.values())} '
          f'removed={sum(removed.values())} added={sum(added.values())} '
          f'digest before={digest(b)} after={digest(a)}')
    for tag, diff in (('-', removed), ('+', added)):
        for row in list(diff)[:5]:
            print(f'  {tag} {row}')
    if name != 'semantic':
        same = same and not removed and not added
by_type = collections.Counter({SEM: sum(as_.values())})
for (_, _, _, t, _), n in ae.items():
    by_type[t] += n
print('after edges by type: ' + ' '.join(f'{t}={n}' for t, n in sorted(by_type.items())))
print('IDENTICAL' if same else 'DIFFERENT')
sys.exit(0 if same else 1)
