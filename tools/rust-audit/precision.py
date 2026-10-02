#!/usr/bin/env python3
"""Classify published Rust CALLS pairs against the independent truth."""
import collections, random, re
import os
D=os.environ.get('AUDIT_DIR','/tmp/ts-audit')+'/'
CORPUS=os.environ.get('AUDIT_CORPUS',os.path.expanduser('~/.cache/cbm-rust-bench/codex/codex-rs'))+'/'
UNRESOLVED='unverifiable: oracle left every call site unresolved (dev-dependency cycle, inactive cfg)'
UNEXPANDED='unverifiable: no oracle record in the macro holding the call (unexpanded, or arguments unresolved)'
UNIMPORTED='unverifiable: oracle left every import of the callee crate unresolved in the caller file (dev-dependency cycle)'
nodes={}
for l in open(D+'g_nodes.tsv',errors='replace'):
    i,label,name,f,s,e=l.rstrip('\n').split('\t'); nodes[int(i)]=(label,name,f,int(s),int(e))
scip_files=set()
file_crates=collections.defaultdict(set)  # corpus file -> crate names of the oracle symbols defined in it
ref_lines=set()  # (file, line) carrying an oracle reference to a corpus definition
for l in open(D+'refs.tsv',errors='replace'):
    p=l.split('\t',3)
    if p[0] in('R','D'): ref_lines.add((p[1],int(p[2])))
bycaller=collections.defaultdict(lambda: collections.defaultdict(set))  # (caller file, line, name) -> callee name -> {(df,dl,kind)}
at_line=collections.defaultdict(list)  # (file, line) -> [(kind, callee name or '-')]: every oracle record on that line
for l in open(D+'truth.tsv',errors='replace'):
    f,line,kind,cf,cl,cn,sym,df,dl,shape,wr=l.rstrip('\n').split('\t'); scip_files.add(f)
    cname='-'
    if sym!='-':
        cname=sym.rsplit('().',1)[0]; cname=cname[max(cname.rfind(']'),cname.rfind('/'),cname.rfind('#'),cname.rfind(' '))+1:]
        if df not in('-','ext') and sym.startswith('rust-analyzer cargo '): file_crates[df].add(sym.split(' ',3)[2].replace('-','_'))
    at_line[(f,int(line))].append((kind,cname))
    if kind.endswith('unresolved') or cf=='-': continue
    bycaller[(cf,int(cl),cn)][cname].add((df,dl,kind))
src={}
def source(f):
    if f not in src:
        try: src[f]=open(CORPUS+f,errors='replace').read().split('\n')
        except OSError: src[f]=[]
    return src[f]
def macro_lines(lines,lo,hi):
    """Lines in [lo,hi] that lie inside the token tree of a `name!(...)`/`name![...]`/`name!{...}` invocation."""
    inside=set(); depth=0; text='\n'.join(lines[lo-1:hi]); ln=lo; i=0; opener=re.compile(r'\b[A-Za-z_]\w*!\s*[(\[{]')
    while i<len(text):
        m=opener.match(text,i) if depth==0 else None
        if m:
            depth=1; i=m.end(); continue
        c=text[i]
        if depth:
            inside.add(ln)
            if c in '([{': depth+=1
            elif c in ')]}': depth-=1
        if c=='\n': ln+=1
        i+=1
    return inside
use_decls={}
def crate_unresolved(f,df):
    """True when the oracle resolved none of the `use` declarations in file f that are
    rooted at the crate defining df. The oracle records a reference (refs.tsv R row) on
    every `use` line whose path reaches a corpus definition, so when every such line is
    bare the crate is absent from f's compilation, e.g. a dev-dependency that depends back
    on the crate under test, which rust-analyzer drops from the crate graph. The crate
    name comes from the oracle symbols defined in df, never from this script."""
    crates=file_crates.get(df,())
    if len(crates)!=1: return False
    crate=next(iter(crates))
    if f not in use_decls:
        decls=[]; text='\n'.join(source(f))
        for m in re.finditer(r'^[ \t]*(?:pub(?:\([^)]*\))?\s+)?use\s+(?:::)?([A-Za-z_]\w*)\s*::([^;]*);',text,re.M):
            first=text.count('\n',0,m.start())+1
            decls.append((m.group(1),range(first,first+m.group(0).count('\n')+1)))
        use_decls[f]=decls
    spans=[r for root,r in use_decls[f] if root==crate]
    return bool(spans) and not any((f,ln) in ref_lines for r in spans for ln in r)
def no_oracle_data(a,b):
    """Why the oracle cannot judge the published call a -> b, or None when it can.

    Every textual call of b's name inside a's span is located in the corpus source. The
    pair is unverifiable only when the oracle recorded no resolution for any of them: no
    record on the site line names b, and every other resolved call record on that line
    names a different call written on the same line (so none can be b's call resolved
    under another name). UNRESOLVED: each site line carries an unresolved record of the
    site's form (path or method). UNEXPANDED: some site carries no record at all, which
    is legitimate only inside a macro token tree that produced no record on any of its
    lines, and only when the name is not bound locally (a closure call has no record).
    UNIMPORTED: some site has neither, but the crate defining b is absent from the
    caller file's compilation (crate_unresolved), so the oracle cannot resolve any call
    into it and no record on that line can be b's call."""
    lines=source(a[2]); n=b[1]
    if not lines: return None
    call=re.compile(r'(\.\s*)?(?<![\w$])'+re.escape(n)+r'\s*(::\s*<[^()]*>\s*)?\(')
    local=re.compile(r'(\blet\s+(mut\s+)?|[|,(]\s*(mut\s+)?)'+re.escape(n)+r'\b\s*[:=|,)]')
    sites=[]
    for ln in range(a[3],min(a[4],len(lines))+1):
        t=re.sub(r'(^|\s)//.*','',lines[ln-1])
        if re.search(r'\bfn\s+'+re.escape(n)+r'\b',t): continue
        if local.search(t): return None
        for m in call.finditer(t):
            sites.append((ln,'method' if m.group(1) else 'path'))
    if not sites: return None
    macro=None; why=UNRESOLVED
    for ln,form in sites:
        recs=at_line.get((a[2],ln),[])
        t=lines[ln-1]
        if any(nm==n for _,nm in recs): return None
        for kind,nm in recs:
            if kind in('path','method','ref_in_macro') and not re.search(r'(?<![\w$])'+re.escape(nm)+r'\b',t): return None
        if any(kind==form+'_unresolved' for kind,_ in recs): continue
        if not recs:
            if macro is None: macro=macro_lines(lines,a[3],a[4])
            lo=hi=ln
            while lo-1 in macro: lo-=1
            while hi+1 in macro: hi+=1
            if ln in macro and not any((a[2],x) in at_line for x in range(lo,hi+1)):
                if why==UNRESOLVED: why=UNEXPANDED
                continue
        if crate_unresolved(a[2],b[2]):
            why=UNIMPORTED; continue
        return None
    return why
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
            if not tg: k=no_oracle_data(a,b) or 'NOT IN TRUTH: caller never references a function of that name'
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
