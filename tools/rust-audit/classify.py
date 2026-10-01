#!/usr/bin/env python3
"""Classify tsdiff disagreements by root cause. usage: classify.py <corpus>... [--other N]"""
import re, sys, collections, os
AUD=os.environ.get('AUDIT_DIR','/tmp/ts-audit')
PRIM = r'(str|bool|char|[iu](8|16|32|64|128|size)|f(32|64))'
RULES = [
    ('prim_named_macro', re.compile(r'\b' + PRIM + r'!\s*[\[({]')),
    ('try_macro_2015', re.compile(r'\btry!\s*[\[({]')),
    ('extern_type', re.compile(r'^(pub(\(.*?\))? )?type [^=]+;')),
    ('safe_extern_item', re.compile(r'\bsafe (fn|static)\b')),
    ('range_to_inclusive', re.compile(r'(^|[\[(,=|{\s])\.\.=')),
    ('range_then_unary', re.compile(r'\.\.=?\s*[*&-]')),
    ('range_then_paren', re.compile(r'\.\.=?\s*\(')),
    ('break_then_expr', re.compile(r'\bbreak\s+(\'\w+\s+)?[(&*-]')),
    ('closure_trailing_comma', re.compile(r',\s*\|')),
    ('raw_as_identifier', re.compile(r'\braw\b')),
    ('neg_const_generic', re.compile(r'(<|,\s*)-\s*\d[\w.]*\s*[,>]')),
    ('turbofish_in_type', re.compile(r'::<[^()]*::<|->.*::<|:\s*[\w:]+::<')),
    ('prim_named_item', re.compile(r'\b' + PRIM + r'(::<|\s*\{)')),
    ('attr_in_pattern_or_expr', re.compile(r'^#\[')),
]
def classify(parent, snip, fact):
    if parent in ('token_tree_pattern', 'token_tree', 'ERROR', 'token_repetition_pattern', 'macro_rule', 'macro_definition', 'token_binding_pattern'):
        return 'macro_rules_tokens'
    for name, rx in RULES:
        if rx.search(snip): return name
    if parent == 'where_clause' or 'type_identifier' in fact: return 'where_clause_left_type'
    return 'OTHER'
KEY = ('DEF', 'CALL', 'MCALL', 'MACRO', 'IMPL', 'USE')
files = collections.defaultdict(set); lost = collections.Counter(); fab = collections.Counter(); nline = collections.Counter()
other = []; args = [a for a in sys.argv[1:] if not a.startswith('--')]
front = set()
valid = 0; err_files = set(); diff_files = set(); lines = 0
for c in args:
    for l in open(f'{AUD}/{c}.files.tsv', errors='replace'):
        p, ed, n, e, m, r, t = l.rstrip('\n').split('\t')
        if ed == 'RAERR': continue
        valid += 1; lines += int(n)
        if int(e) + int(m): err_files.add(p)
        if int(r) + int(t): diff_files.add(p)
    seen_front = {}
    def is_front(p):
        if p not in seen_front:
            try: h = open(p, 'rb').read(400)
            except OSError: h = b''
            seen_front[p] = h.startswith(b'---') or (h.startswith(b'#!') and b'\n---' in h[:200])
        return seen_front[p]
    for kind, fn in (('E', f'{AUD}/{c}.tserrors.tsv'), ('D', f'{AUD}/{c}.diffs.tsv')):
        for l in open(fn, errors='replace'):
            f = l.rstrip('\n').split('\t')
            if kind == 'E': p, fact, ln, snip = f[0], f[1], f[2], f[3] if len(f) > 3 else ''; side = 'E'
            else: p, side, fact, ln, snip = f[0], f[1], f[2], f[3], f[4] if len(f) > 4 else ''
            parent = fact.split(' ')[-1] if kind == 'E' else ''
            cause = 'frontmatter_nightly' if is_front(p) else classify(parent, snip, fact)
            files[cause].add(p); nline[cause] += 1
            k = fact.split(' ')[0]
            if side == 'R' and k in KEY: lost[cause] += 1
            if side == 'T' and k in KEY: fab[cause] += 1
            if cause == 'OTHER': other.append((p, side, fact, ln, snip))
print(f'valid_files={valid} lines={lines} files_with_error_nodes={len(err_files)} files_with_any_disagreement={len(err_files | diff_files)} silent_only_files={len(diff_files - err_files)}')
print(f'{"cause":<28}{"files":>8}{"records":>9}{"key_facts_missing":>19}{"key_facts_spurious":>20}')
for cause in sorted(files, key=lambda c: -len(files[c])):
    print(f'{cause:<28}{len(files[cause]):>8}{nline[cause]:>9}{lost[cause]:>19}{fab[cause]:>20}')
n = 0
for a in sys.argv:
    if a.startswith('--other='): n = int(a.split('=')[1])
import random; random.seed(1); random.shuffle(other)
for p, side, fact, ln, snip in other[:n]:
    print(f'{re.sub(r".*/(index.crates.io-[^/]*|github|corpus)/", "", p)}:{ln}\t{side}\t{fact}\t{snip[:120]}')
