#!/usr/bin/env python3
"""Independent check of definitions, method ownership, imports, usages, impl relations in the published graph."""
import bisect, collections, re, random
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
        if s<=line<=e and n.removeprefix('r#')==name.removeprefix('r#') and (labels is None or lab in labels) and (best is None or e-s<best[0]): best=(e-s,i)
    return best[1] if best else None
defs=[]; refs=[]; impls=[]; scip_files=set()
for l in open(D+'refs.tsv',errors='replace'):
    p=l.rstrip('\n').split('\t')
    if p[0]=='D': defs.append(p[1:]); scip_files.add(p[1])
    elif p[0]=='R': refs.append(p[1:])
    else: impls.append(p[1:])
# 1. definitions
LAB={'fn':{'Function','Method'},'type':{'Struct','Enum','Interface','Type','Class'},'term':{'Variable','Field','AssociatedConst'},'module':{'Module'},'macro':{'Macro','Function'}}
# A `type` nested in another (`E#V#`, `impl#[T][Tr]Item#`) is an enum variant or an
# associated type, which the graph labels EnumVariant / AssociatedType.
VARIANT_LAB=LAB['type']|{'EnumVariant','AssociatedType'}
# A file module's truth definition sits on line 1 and carries the module's own name
# (`crate`, the file stem, or the name of a `#[path]` module); its graph node is the
# file's Module node, which is named by the file path.
filemodule={f:i for f,rows in by_file.items() for s,e,n,i,lab in rows if lab=='Module' and n==f and s==1}
tot=collections.Counter(); hit=collections.Counter(); matched=set(); defnode={}
for f,line,kind,name,desc in defs:
    sub=kind
    if kind=='term': sub='term:field_or_variant' if '#' in desc else 'term:const_static'
    if kind=='fn': sub='fn:method' if ('impl#' in desc or '#' in desc) else 'fn:free'
    tot[sub]+=1
    labels=VARIANT_LAB if kind=='type' and '#' in desc.rstrip('#') else LAB.get(kind)
    i=find(f,int(line),name,labels)
    if i is None and kind=='module' and line=='1': i=filemodule.get(f)
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
# The oracle names the self type as spelled; `use path::Owner as Alias` spells
# the owner's definition under another name.
def owner_alias_ok(f,owner_name,want):
    try: text=open(os.path.join(os.environ.get('AUDIT_CORPUS',''),f),errors='replace').read()
    except OSError: return False
    return re.search(r'\b'+re.escape(owner_name)+r'\s+as\s+'+re.escape(want)+r'\b',text) is not None
C=collections.Counter(); alias_ok=0
for f,line,kind,name,desc in defs:
    if kind!='fn': continue
    m=re.search(r'impl#\[`?([^\]`<]+)',desc); tr=re.search(r'([A-Za-z_0-9]+)#[^#]*\(\)\.$',desc)
    if not m and not ('#' in desc): continue
    i=find(f,int(line),name,{'Function','Method'})
    if i is None: C['method node missing']+=1; continue
    want=(m.group(1) if m else tr.group(1) if tr else '?').split('::')[-1].strip('&').strip()
    if i not in owner: C['method has no owner link']+=1
    elif nodes.get(owner[i],('','?'))[1]==want: C['owner link correct']+=1
    elif owner_alias_ok(f,nodes.get(owner[i],('','?'))[1],want): C['owner link correct']+=1; alias_ok+=1
    else: C['owner link names another type']+=1
print('\nMETHOD OWNERSHIP (truth methods)'); 
for k,v in C.most_common(): print(f'  {v:>7} {100*v/sum(C.values()):5.1f}%  {k}')
print(f'  (correct includes {alias_ok} whose self type is spelled through `use Owner as Alias`)')
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
module_refs=collections.defaultdict(set)
for f,line,ctx,el,en,df,dl,kind,name in refs:
    b=find(df,int(dl),name)
    if el=='-':
        if ctx!='use' and b is not None: module_refs[(f,b)].add(int(line))
        continue
    a=find(f,int(el),en,{'Function','Method'})
    if a is not None and b is not None: fnrefs[a].add(b)
module_refs={k:sorted(v) for k,v in module_refs.items()}
U=collections.Counter(); tr_pairs=sum(len(v) for v in fnrefs.values()); found=0; gus=collections.defaultdict(set)
module_confirmed=0
for l in open(D+'g_USAGE.tsv'):
    s,t=map(int,l.split('\t'))
    if s not in nodes or t not in nodes or not nodes[s][2].endswith('.rs'): continue
    gus[s].add(t)
    lab=nodes[t][0]
    source_label,_,f,lo,hi=nodes[s]
    lines=module_refs.get((f,t),()) if source_label in ('Module','File') else ()
    pos=bisect.bisect_left(lines,lo)
    module_hit=pos<len(lines) and (source_label=='File' or lines[pos]<=hi)
    if nodes[s][2] not in scip_files: U[(lab,'unverifiable')]+=1
    elif t in fnrefs.get(s,()) or module_hit:
        U[(lab,'confirmed')]+=1
        module_confirmed+=module_hit
    else: U[(lab,'not confirmed')]+=1
print('\nUSAGE pairs from Rust sources, by target kind: confirmed / not confirmed / unverifiable')
for lab in sorted({k[0] for k in U}): print(f'  {lab:<10}{U[(lab,"confirmed")]:>8}{U[(lab,"not confirmed")]:>8}{U[(lab,"unverifiable")]:>8}')
print(f'module-level USAGE pairs confirmed by non-use references in the source Module/File span: {module_confirmed}')
calls=collections.defaultdict(set)
for l in open(D+'g_calls.tsv'):
    s,t,_=l.split('\t',2); calls[int(s)].add(int(t))
for l in open(D+'g_CALL_REFERENCE.tsv'):
    s,t=map(int,l.split('\t')); calls[s].add(t)
bykind=collections.Counter(); bykind_hit=collections.Counter()
for a,ts in fnrefs.items():
    for b in ts:
        k=nodes[b][0]; bykind[k]+=1; bykind_hit[k]+= (b in gus.get(a,())) or (b in calls.get(a,()))
print('  truth: function reference pairs with mapped targets -> any USAGE/CALLS/CALL_REFERENCE pair published')
for k,v in bykind.most_common(): print(f'    {k:<10}{bykind_hit[k]:>8} / {v:<8} {100*bykind_hit[k]/v:.1f}%')
# 5. impl relations: `impl Trait for Type` -> IMPLEMENTS edge Type -> Trait.
# Truth: the impl header's in-corpus references (refs.tsv R rows on the header
# lines) whose name is the written self-type / trait leaf give the exact
# definitions; both must map onto graph nodes for the block to count.
ti=[i for i in impls if i[3]!='-']
CORPUS=os.environ.get('AUDIT_CORPUS','')
src_cache={}
def src_lines(f):
    if f not in src_cache:
        try: src_cache[f]=open(os.path.join(CORPUS,f),errors='replace').read().split('\n')
        except OSError: src_cache[f]=[]
    return src_cache[f]
def header_range(f,line):
    ls=src_lines(f); i=line-1
    while i<len(ls) and not re.search(r'\bimpl\b',ls[i]): i+=1
    j=i
    while j<len(ls) and '{' not in ls[j] and not ls[j].rstrip().endswith(';'): j+=1
    return line,j+1
def leaf(t):
    t=re.sub(r'^(unsafe|const)\s+','',t.strip())
    if re.match(r'^[&*(\[!?]|^(dyn|impl)\b',t): return None
    t=t.split('<',1)[0]
    return t.split('::')[-1].strip() or None
refs_at=collections.defaultdict(list)
for f,line,ctx,el,en,df,dl,kind,name in refs:
    if kind=='type': refs_at[(f,int(line))].append((df,int(dl),name))
TLAB=LAB['type']; I=collections.Counter(); want_pairs=set(); all_pairs=set(); sample=[]
impl_edges=set()
if os.path.exists(D+'g_IMPLEMENTS.tsv'):
    for l in open(D+'g_IMPLEMENTS.tsv'):
        s,t=map(int,l.split('\t')); impl_edges.add((s,t))
for f,line,st,tr in ti:
    a,b=header_range(f,int(line)); sl,tl=leaf(st),leaf(tr)
    def node_for(nm):
        if nm is None: return None
        hits={(df,dl,n) for k in range(a,b+1) for df,dl,n in refs_at.get((f,k),()) if n==nm}
        if len(hits)!=1: return None
        df,dl,n=hits.pop(); return find(df,dl,n,TLAB)
    sn,tn=node_for(sl),node_for(tl)
    if sn is None or tn is None: I['type or trait not defined in corpus (or no graph node)']+=1; continue
    all_pairs.add((sn,tn)); I['in corpus']+=1
    if (sn,tn) in impl_edges: I['in corpus, PUBLISHED']+=1
    else: I['in corpus, missing']+=1; sample.append((f,line,st,tr))
ok=I['in corpus, PUBLISHED']; tot=I['in corpus']
print(f'\nIMPL BLOCKS in oracle files: {len(impls)}; trait impls: {len(ti)}')
print(f'impl links published {ok} / {tot} {100*ok/max(tot,1):.1f}%  (impl Trait for Type, both defined in corpus -> IMPLEMENTS Type->Trait)')
for k,v in I.most_common(): print(f'  {v:>7}  {k}')
good=bad=unver=0; badrows=[]
for s,t in impl_edges:
    if s not in nodes or not nodes[s][2].endswith('.rs'): continue
    if nodes[s][2] not in scip_files: unver+=1
    elif (s,t) in all_pairs: good+=1
    else: bad+=1; badrows.append((nodes[s][1],nodes[s][2],nodes.get(t,('','?'))[1]))
print(f'  published Rust IMPLEMENTS: confirmed {good}, not confirmed {bad}, unverifiable(platform) {unver}')
for r in badrows[:10]: print('    not confirmed:',r)
for r in random.sample(sample,min(10,len(sample))): print('    missing:',r)
