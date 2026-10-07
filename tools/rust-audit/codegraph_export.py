#!/usr/bin/env python3
"""Export a codegraph (colbymchenry/codegraph) index into the TSV tables rust-score.sh --rescore reads.

    codegraph_export.py <project>/.codegraph/codegraph.db <out-dir>

Node ids are renumbered to integers; function/method nodes become Function/Method; `calls`
edges become CALLS with codegraph's resolvedBy as the strategy column. The other tables are
written empty so families.py runs; they carry no codegraph data."""
import json, sqlite3, sys
db, out = sys.argv[1], sys.argv[2].rstrip('/') + '/'
c = sqlite3.connect(f'file:{db}?mode=ro', uri=True)
label = {'function': 'Function', 'method': 'Method', 'struct': 'Struct', 'enum': 'Enum', 'trait': 'Trait',
         'file': 'File', 'import': 'Import', 'variable': 'Variable', 'enum_member': 'EnumMember',
         'type_alias': 'TypeAlias'}
ids = {}
with open(out + 'g_nodes.tsv', 'w') as o:
    for sid, kind, name, f, s, e in c.execute('select id,kind,name,file_path,start_line,end_line from nodes'):
        if not f or not f.endswith('.rs'): continue
        ids[sid] = len(ids) + 1
        name = (name or '').replace('\t', ' ').replace('\n', ' ')
        o.write(f'{ids[sid]}\t{label.get(kind, kind)}\t{name}\t{f}\t{s or 0}\t{e or 0}\n')
n_calls = 0; dropped = 0
with open(out + 'g_calls.tsv', 'w') as o:
    for s, t, m in c.execute("select source,target,metadata from edges where kind='calls'"):
        if s not in ids or t not in ids: dropped += 1; continue
        strat = (json.loads(m).get('resolvedBy') if m else '') or 'codegraph'
        o.write(f'{ids[s]}\t{ids[t]}\t{strat}\n'); n_calls += 1
for t in ('IMPORTS', 'DEFINES_METHOD', 'IMPLEMENTS', 'USAGE', 'CALL_REFERENCE'):
    open(out + f'g_{t}.tsv', 'w').close()
with open(out + 'edge_types.tsv', 'w') as o:
    for k, n in c.execute('select kind,count(*) from edges group by 1 order by 1'): o.write(f'{k}\t{n}\n')
print(f'nodes {len(ids)}  CALLS {n_calls}  calls dropped (endpoint not a .rs node) {dropped}')
