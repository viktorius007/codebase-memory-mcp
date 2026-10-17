#!/usr/bin/env python3
"""Print one sha256 line per bench artefact the gate depends on, so two machines can be compared.

Usage: fingerprint.py BENCH-DIR NAME...
Per Rust corpus: the corpus tree (sorted relative paths and file contents), truth.tsv, refs.tsv,
and the base graph's edges keyed by (type, source qualified name, target qualified name), which
excludes row ids and absolute paths. Output order and content are platform-independent.
"""
import hashlib
import os
import sqlite3
import sys


def file_digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def tree_digest(root):
    h = hashlib.sha256()
    entries = []
    for directory, _, files in os.walk(root):
        for name in files:
            path = os.path.join(directory, name)
            entries.append((os.path.relpath(path, root).encode('utf-8', 'surrogateescape'), path))
    for rel, path in sorted(entries):
        h.update(rel + b'\0' + file_digest(path).encode() + b'\n')
    return h.hexdigest()


def edges_digest(db_path):
    with sqlite3.connect(f'file:{db_path}?mode=ro', uri=True) as db:
        db.text_factory = bytes
        rows = sorted(db.execute(
            'select e.type, s.qualified_name, t.qualified_name from edges e '
            'join nodes s on s.id = e.source_id join nodes t on t.id = e.target_id'))
    h = hashlib.sha256()
    for row in rows:
        h.update(b'\t'.join(row) + b'\n')
    return h.hexdigest()


bench, names = sys.argv[1], sys.argv[2:]
for name in names:
    root = os.path.join(bench, name)
    print(f'{name} corpus {tree_digest(os.path.join(root, name))}')
    print(f'{name} truth {file_digest(os.path.join(root, "truth.tsv"))}')
    print(f'{name} refs {file_digest(os.path.join(root, "refs.tsv"))}')
    print(f'{name} base-edges {edges_digest(os.path.join(bench, "rust-base", name, "cache", "score.db"))}')
