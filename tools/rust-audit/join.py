#!/usr/bin/env python3
"""Join independent call truth (truth.tsv) with the published graph (g_nodes.tsv, g_calls.tsv)."""
import collections, sys, bisect
import os
D=os.environ.get('AUDIT_DIR','/tmp/ts-audit')+'/'
nodes={}; by_file=collections.defaultdict(list)
for l in open(D+'g_nodes.tsv',errors='replace'):
    i,label,name,f,s,e=l.rstrip('\n').split('\t'); nodes[int(i)]=(label,name,f,int(s),int(e))
    if label in('Function','Method'): by_file[f].append((int(s),int(e),name,int(i)))
def find(f,line,name):
    """graph Function/Method node in file f whose span contains line and whose name matches; innermost."""
    best=None
    for s,e,n,i in by_file.get(f,()):
        if s<=line<=e and n==name and (best is None or e-s<best[0]): best=(e-s,i)
    return best[1] if best else None
edges=set(); strat={}
for l in open(D+'g_calls.tsv'):
    s,t,st=l.rstrip('\n').split('\t'); edges.add((int(s),int(t))); strat[(int(s),int(t))]=st
C=collections.Counter(); shapes=collections.Counter(); shapes_hit=collections.Counter()
truth_pairs=set(); truth_pairs_macro=set(); caller_unmapped=0; callee_unmapped=0
form=collections.Counter(); form_hit=collections.Counter()
def callee_form(sym):
    d=sym.split(' ',4)[-1]
    if 'impl#' in d: return 'trait_impl_method' if d.count('][')>=1 else 'inherent_method'
    if '#' in d: return 'trait_decl_method'
    return 'free_fn'
for l in open(D+'truth.tsv',errors='replace'):
    f,line,kind,cf,cl,cn,sym,df,dl,shape,wr=l.rstrip('\n').split('\t')
    if kind.endswith('unresolved'): C['site: oracle has no target ('+kind+')']+=1; continue
    if kind=='ref_not_call': C['implicit or non-call reference (operators, await, fn values)']+=1; continue
    cname=sym.rsplit('().',1)[0]; cname=cname[max(cname.rfind(']'),cname.rfind('/'),cname.rfind('#'),cname.rfind(' '))+1:]
    inmacro = kind=='ref_in_macro'
    scope='macro_arg' if inmacro else kind
    if df=='ext': C[f'{scope}: target outside corpus (std/dependency)']+=1; continue
    C[f'{scope}: target in corpus']+=1
    if cf=='-': C[f'{scope}: in corpus, caller is not a fn (const/static init)']+=1; continue
    a=find(cf,int(cl),cn); b=find(df,int(dl),cname)
    if a is None: caller_unmapped+=1
    if b is None: callee_unmapped+=1
    hit = a is not None and b is not None and (a,b) in edges
    if a is not None and b is not None: (truth_pairs_macro if inmacro else truth_pairs).add((a,b))
    C[f'{scope}: in corpus, fn caller, PUBLISHED' if hit else f'{scope}: in corpus, fn caller, missing']+=1
    fm=callee_form(sym); form[(scope,fm)]+=1; form_hit[(scope,fm)]+=hit
    if kind=='method': shapes[shape+(' +?/await' if wr!='-' else '')]+=1; shapes_hit[shape+(' +?/await' if wr!='-' else '')]+=hit
for k in sorted(C): print(f'{C[k]:>9}  {k}')
print(f'caller fn not found in graph: {caller_unmapped}; callee fn not found in graph: {callee_unmapped}')
print('\nrecall by target form (sites with in-corpus target and fn caller):')
for k in sorted(form): print(f'  {k[0]:<10}{k[1]:<20}{form_hit[k]:>8} / {form[k]:<8} {100*form_hit[k]/form[k]:.1f}%')
print('\nmethod-call receiver shapes (in-corpus target): published / total')
for k,v in shapes.most_common(): print(f'  {k:<34}{shapes_hit[k]:>7} / {v:<7} {100*shapes_hit[k]/v:.1f}%')
# precision: published Rust fn->fn edges vs truth pairs
allp=truth_pairs|truth_pairs_macro
rs=[e for e in edges if e[0] in nodes and e[1] in nodes and nodes[e[0]][0] in('Function','Method') and nodes[e[1]][0] in('Function','Method')]
ok=sum(1 for e in rs if e in allp)
print(f'\npublished Rust fn->fn CALLS pairs: {len(rs)}; confirmed by truth: {ok}; not confirmed: {len(rs)-ok}')
bad=collections.Counter(strat[e] for e in rs if e not in allp); good=collections.Counter(strat[e] for e in rs if e in allp)
for k in sorted(set(bad)|set(good), key=lambda k:-(bad[k]+good[k])): print(f'  {k or "(none)":<28} confirmed {good[k]:>7}  not confirmed {bad[k]:>6}')
print(f'truth pairs (explicit) {len(truth_pairs)}; found {sum(1 for p in truth_pairs if p in edges)}; truth pairs (macro args only) {len(truth_pairs_macro-truth_pairs)}')
import random; random.seed(3); un=[e for e in rs if e not in allp]; random.shuffle(un)
with open(D+'unconfirmed_sample.tsv','w') as o:
    for s,t in un[:60]: o.write(f'{strat[(s,t)]}\t{nodes[s][2]}:{nodes[s][3]} {nodes[s][1]}\t->\t{nodes[t][2]}:{nodes[t][3]} {nodes[t][1]}\n')
