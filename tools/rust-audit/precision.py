#!/usr/bin/env python3
"""Classify published Rust CALLS pairs against the independent truth."""
import collections, random
import os
D=os.environ.get('AUDIT_DIR','/tmp/ts-audit')+'/'
nodes={}
for l in open(D+'g_nodes.tsv',errors='replace'):
    i,label,name,f,s,e=l.rstrip('\n').split('\t'); nodes[int(i)]=(label,name,f,int(s),int(e))
scip_files=set(); callers=collections.defaultdict(lambda: collections.defaultdict(set))  # (file) -> list of (line,name) -> callee name -> {(df,dl,kind)}
bycaller=collections.defaultdict(lambda: collections.defaultdict(set))
for l in open(D+'truth.tsv',errors='replace'):
    f,line,kind,cf,cl,cn,sym,df,dl,shape,wr=l.rstrip('\n').split('\t'); scip_files.add(f)
    if kind.endswith('unresolved') or cf=='-': continue
    cname=sym.rsplit('().',1)[0]; cname=cname[max(cname.rfind(']'),cname.rfind('/'),cname.rfind('#'),cname.rfind(' '))+1:]
    bycaller[(cf,int(cl),cn)][cname].add((df,dl,kind))
idx=collections.defaultdict(list)
for (cf,cl,cn) in bycaller: idx[cf].append((cl,cn))
C=collections.Counter(); S=collections.defaultdict(list); strat=collections.Counter()
for l in open(D+'g_calls.tsv'):
    s,t,st=l.rstrip('\n').split('\t'); s=int(s); t=int(t)
    if s not in nodes or t not in nodes: continue
    a,b=nodes[s],nodes[t]
    if a[0] not in('Function','Method') or b[0] not in('Function','Method'): continue
    if a[2] not in scip_files: k='unverifiable: caller file not compiled on the oracle platform'
    else:
        key=next(((a[2],cl,cn) for cl,cn in idx[a[2]] if cn==a[1] and a[3]<=cl<=a[4]),None)
        if key is None: k='unverifiable: caller has no oracle data (inactive cfg or no calls)'
        else:
            tg=bycaller[key].get(b[1])
            if not tg: k='NOT IN TRUTH: caller never references a function of that name'
            else:
                exact=[x for x in tg if x[0]==b[2] and x[1]!='-' and b[3]<=int(x[1])<=b[4]]
                if exact:
                    kinds={x[2] for x in exact}
                    k='correct: explicit call' if kinds&{'path','method'} else 'correct: call inside macro arguments' if 'ref_in_macro' in kinds else 'correct target, but reference is not a call (fn value / implicit)'
                else: k='WRONG TARGET: caller calls a different function of that name'
    C[k]+=1; S[k].append((st,a,b)); 
    if k.startswith(('WRONG','NOT IN')): strat[st]+=1
tot=sum(C.values())
for k,v in C.most_common(): print(f'{v:>7} {100*v/tot:5.1f}%  {k}')
print('wrong/not-in-truth by strategy:',dict(strat))
random.seed(5)
for k in C:
    if k.startswith(('WRONG','NOT IN')):
        print('\n'+k)
        for st,a,b in random.sample(S[k],min(12,len(S[k]))): print(f'  {st:<20} {a[2]}:{a[3]} {a[1]} -> {b[2]}:{b[3]} {b[1]}')
