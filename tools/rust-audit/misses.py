#!/usr/bin/env python3
"""Break down missing path-call sites (name resolution only) by locality and syntactic form of the callee path."""
import collections, random, re
import os
D=os.environ.get('AUDIT_DIR','/tmp/ts-audit')+'/'
C=os.environ.get('AUDIT_CORPUS','/tmp/cbm-rust/corpus')+'/'
nodes={}; by_file=collections.defaultdict(list)
for l in open(D+'g_nodes.tsv',errors='replace'):
    i,label,name,f,s,e=l.rstrip('\n').split('\t')
    if label in('Function','Method'): by_file[f].append((int(s),int(e),name,int(i)))
def find(f,line,name):
    best=None
    for s,e,n,i in by_file.get(f,()):
        if s<=line<=e and n==name and (best is None or e-s<best[0]): best=(e-s,i)
    return best[1] if best else None
edges=set()
for l in open(D+'g_calls.tsv'):
    s,t,_=l.split('\t',2); edges.add((int(s),int(t)))
src={}
def line(f,n):
    if f not in src: src[f]=open(C+f,errors='replace').read().split('\n')
    return src[f][n-1] if n-1<len(src[f]) else ''
def crate(f): 
    p=f.split('/'); return '/'.join(p[:p.index('src')]) if 'src' in p else '/'.join(p[:p.index('tests')]) if 'tests' in p else p[0]
B=collections.Counter(); T=collections.Counter(); S=collections.defaultdict(list)
for l in open(D+'truth.tsv',errors='replace'):
    f,ln,kind,cf,cl,cn,sym,df,dl,shape,wr=l.rstrip('\n').split('\t')
    if kind!='path' or df=='ext' or cf=='-': continue
    cname=sym.rsplit('().',1)[0]; cname=cname[max(cname.rfind(']'),cname.rfind('/'),cname.rfind('#'),cname.rfind(' '))+1:]
    a=find(cf,int(cl),cn); b=find(df,int(dl),cname)
    hit=a is not None and b is not None and (a,b) in edges
    text=line(f,int(ln)); m=re.search(r'([\w:<>, ]*?)\b'+re.escape(cname)+r'\s*(::<[^(]*>)?\s*\(',text)
    pre=(m.group(1) if m else '').strip()
    q=pre.rstrip(':').split('::')[-1] if pre.endswith('::') else ''
    formk='bare name' if not pre.endswith('::') else 'Self::' if q=='Self' else 'crate/super/self path' if re.match(r'(crate|super|self)\b',pre.strip()) else 'Type::fn' if q[:1].isupper() else 'module::fn'
    loc='same file' if f==df else 'same crate' if crate(f)==crate(df) else 'other crate'
    istest='test target' if '/tests/' in '/'+f or f.endswith('_tests.rs') or '/tests.rs' in f else 'src'
    k=(formk,loc); T[k]+=1
    if not hit:
        B[k]+=1; why='caller node absent' if a is None else 'callee node absent' if b is None else 'edge absent'
        S[k].append((f,ln,text.strip()[:110],df,why)); B[('why',why)]+=1
print(f'{"form":<24}{"locality":<12}{"missing":>8}{"total":>8}{"recall":>8}')
for k,v in sorted(T.items(), key=lambda kv:-B[kv[0]]):
    print(f'{k[0]:<24}{k[1]:<12}{B[k]:>8}{v:>8}{100*(v-B[k])/v:>7.1f}%')
print({k[1]:v for k,v in B.items() if k[0]=='why'})
random.seed(2)
for k in sorted(T, key=lambda k:-B[k])[:5]:
    print('\n',k)
    for x in random.sample(S[k],min(6,len(S[k]))): print('   ',x[0]+':'+x[1],'|',x[2],'| ->',x[3],'|',x[4])
