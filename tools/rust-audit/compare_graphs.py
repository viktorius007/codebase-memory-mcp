#!/usr/bin/env python3
"""Compare every graph node and edge by content, preserving multiplicity.

Usage: compare_graphs.py BASE.db TARGET.db
Row IDs are replaced by node identities; JSON properties are canonicalized.
Malformed JSON and invalid UTF-8 are preserved rather than excluded.
"""
import collections
import json
import sqlite3
import sys


def properties(text):
    try:
        canonical = json.dumps(json.loads(text or '{}'), sort_keys=True, separators=(',', ':'))
        return ('json', canonical)
    except json.JSONDecodeError:
        return ('raw', text)


def graph(path):
    with sqlite3.connect(f'file:{path}?mode=ro', uri=True) as db:
        db.text_factory = lambda data: data.decode('utf-8', errors='surrogateescape')
        identities = {
            row[0]: tuple(row[1:7]) + (properties(row[7]),)
            for row in db.execute('select id,label,name,qualified_name,file_path,'
                                  'start_line,end_line,properties from nodes')
        }
        nodes = collections.Counter(identities.values())
        edges = collections.Counter(
            (identities[source], identities[target], kind, properties(props))
            for source, target, kind, props in db.execute(
                'select source_id,target_id,type,properties from edges')
        )
    return nodes, edges


def main():
    base, target = graph(sys.argv[1]), graph(sys.argv[2])
    different = False
    for kind, left, right in zip(('nodes', 'edges'), base, target):
        missing, extra = left - right, right - left
        print(f'{kind}: base={sum(left.values())} target={sum(right.values())} '
              f'missing={sum(missing.values())} extra={sum(extra.values())}')
        for name, changes in (('missing', missing), ('extra', extra)):
            for identity, count in list(changes.items())[:5]:
                print(f'  {name} x{count}: {json.dumps(identity)}')
        different |= bool(missing or extra)
    return int(different)


if __name__ == '__main__':
    sys.exit(main())
