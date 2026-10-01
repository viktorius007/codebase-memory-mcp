#!/usr/bin/env python3
"""Independent check of definitions, method ownership, imports, usages, impl relations in the published graph."""
import collections, re, random
import os
D=os.environ.get('AUDIT_DIR','/tmp/ts-audit')+'/'
nodes={}; by_file=collections.defaultdict(list); filenode={}
for l in open(D+'g_nodes.tsv',errors='replace'):
    i,label,name,f,s,e=l.rstrip('\n').split('\t'); i=int(i); nodes[i]=(label,name,f,int(s),int(e))
    if label=='File': filenode[f]=i
    else: by_file[f].append((int(s),int(e),name,i,label))
def find(f,line,name,labels=None):
    best=None
    for s,e,n,i,lab in by_file.get(f,()):
        if s<=line<=e and n==name and (labels is None or lab in labels) and (best is None or e-s<best[0]): best=(e-s,i)
    return best[1] if best else None
defs=[]; refs=[]; impls=[]; scip_files=set()
for l in open(D+'refs.tsv',errors='replace'):
    p=l.rstrip('\n').split('\t')
    if p[0]=='D': defs.append(p[1:]); scip_files.add(p[1])
    elif p[0]=='R': refs.append(p[1:])
    else: impls.append(p[1:])
# 1. definitions
LAB={'fn':{'Function','Method'},'type':{'Struct','Enum','Interface','Type','Class'},'term':{'Variable','Field'},'module':{'Module'},'macro':{'Macro','Function'}}
tot=collections.Counter(); hit=collections.Counter(); matched=set(); defnode={}
for f,line,kind,name,desc in defs:
    sub=kind
    if kind=='term': sub='term:field_or_variant' if '#' in desc else 'term:const_static'
    if kind=='fn': sub='fn:method' if ('impl#' in desc or '#' in desc) else 'fn:free'
    tot[sub]+=1
    i=find(f,int(line),name,LAB.get(kind))
    if i is not None: hit[sub]+=1; matched.add(i); defnode[(f,int(line),name)]=i
print('DEFINITIONS (truth definition -> graph node of matching kind at that place)')
for k in sorted(tot): print(f'  {k:<24}{hit[k]:>7} / {tot[k]:<7} {100*hit[k]/tot[k]:.1f}%')
extra=collections.Counter()
for i,(label,name,f,s,e) in nodes.items():
    if f in scip_files and label in('Function','Method','Struct','Enum','Interface','Type','Variable','Field') and i not in matched: extra[label]+=1
print('  graph nodes in oracle files with no truth definition at their place:',dict(extra))
dup=collections.Counter((f,name,kind) for f,line,kind,name,desc in defs if kind=='fn')
# 2. method ownership
owner={}
for l in open(D+'g_DEFINES_METHOD.tsv'):
    s,t=map(int,l.split('\t')); owner[t]=s
C=collections.Counter()
for f,line,kind,name,desc in defs:
    if kind!='fn': continue
    m=re.search(r'impl#\[`?([^\]`<]+)',desc); tr=re.search(r'([A-Za-z_0-9]+)#[^#]*\(\)\.$',desc)
    if not m and not ('#' in desc): continue
    i=find(f,int(line),name,{'Function','Method'})
    if i is None: C['method node missing']+=1; continue
    want=(m.group(1) if m else tr.group(1) if tr else '?').split('::')[-1].strip('&').strip()
    if i not in owner: C['method has no owner link']+=1
    elif nodes.get(owner[i],('','?'))[1]==want: C['owner link correct']+=1
    else: C['owner link names another type']+=1
print('\nMETHOD OWNERSHIP (truth methods)'); 
for k,v in C.most_common(): print(f'  {v:>7} {100*v/sum(C.values()):5.1f}%  {k}')
# 3. imports
imp=collections.defaultdict(set)
for l in open(D+'g_IMPORTS.tsv'):
    s,t=map(int,l.split('\t')); imp[s].add(t)
truth_imp=collections.defaultdict(set); T=collections.Counter()
for f,line,ctx,el,en,df,dl,kind,name in refs:
    if ctx!='use' or kind in('module',): continue
    j=find(df,int(dl),name)
    if j is None: T['use target has no graph node']+=1; continue
    truth_imp[f].add(j)
ok=miss=0
for f,ts in truth_imp.items():
    g=imp.get(filenode.get(f,-1),set()); ok+=len(ts&g); miss+=len(ts-g)
print(f'\nIMPORTS (use of an in-corpus item -> File->item link): published {ok} / {ok+miss} = {100*ok/(ok+miss):.1f}%; {dict(T)}')
good=bad=unver=0; badrows=[]
for f,fi in filenode.items():
    if not f.endswith('.rs'): continue
    for t in imp.get(fi,()):
        if t not in nodes or not nodes[t][2].endswith('.rs'): bad+=1; badrows.append((f,nodes.get(t))); continue
        if f not in scip_files: unver+=1
        elif t in truth_imp.get(f,()): good+=1
        else: bad+=1; badrows.append((f,nodes[t]))
print(f'  published Rust File->item IMPORTS: confirmed {good}, not confirmed {bad}, unverifiable(platform) {unver}')
random.seed(1)
for f,n in random.sample(badrows,10): print('    not confirmed:',f,'->',n)
# 4. usages
fnrefs=collections.defaultdict(set)
for f,line,ctx,el,en,df,dl,kind,name in refs:
    if el=='-': continue
    a=find(f,int(el),en,{'Function','Method'}); b=find(df,int(dl),name)
    if a is not None and b is not None: fnrefs[a].add(b)
U=collections.Counter(); tr_pairs=sum(len(v) for v in fnrefs.values()); found=0; gus=collections.defaultdict(set)
for l in open(D+'g_USAGE.tsv'):
    s,t=map(int,l.split('\t'))
    if s not in nodes or t not in nodes or not nodes[s][2].endswith('.rs'): continue
    gus[s].add(t)
    lab=nodes[t][0]
    if nodes[s][2] not in scip_files: U[(lab,'unverifiable')]+=1
    elif t in fnrefs.get(s,()): U[(lab,'confirmed')]+=1
    else: U[(lab,'not confirmed')]+=1
print('\nUSAGE links from Rust functions, by target kind: confirmed / not confirmed / unverifiable')
for lab in sorted({k[0] for k in U}): print(f'  {lab:<10}{U[(lab,"confirmed")]:>8}{U[(lab,"not confirmed")]:>8}{U[(lab,"unverifiable")]:>8}')
calls=collections.defaultdict(set)
for l in open(D+'g_calls.tsv'):
    s,t,_=l.split('\t',2); calls[int(s)].add(int(t))
bykind=collections.Counter(); bykind_hit=collections.Counter()
for a,ts in fnrefs.items():
    for b in ts:
        k=nodes[b][0]; bykind[k]+=1; bykind_hit[k]+= (b in gus.get(a,())) or (b in calls.get(a,()))
print('  truth: function references in-corpus item -> any USAGE/CALLS link published')
for k,v in bykind.most_common(): print(f'    {k:<10}{bykind_hit[k]:>8} / {v:<8} {100*bykind_hit[k]/v:.1f}%')
# 5. impl relations
ti=[i for i in impls if i[3]!='-']
print(f'\nIMPL BLOCKS in oracle files: {len(impls)}; trait impls: {len(ti)}; graph Rust trait-implementation links: 0 (no IMPLEMENTS edges, INHERITS has none for Rust)')
