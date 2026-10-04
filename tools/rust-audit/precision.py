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
file_mod={}  # corpus file -> oracle descriptor prefix of its file module ('' for a crate root)
fn_defs=collections.defaultdict(lambda: collections.defaultdict(set))  # file -> fn descriptor -> {definition line}
for l in open(D+'refs.tsv',errors='replace'):
    p=l.rstrip('\n').split('\t')
    if p[0] in('R','D'): ref_lines.add((p[1],int(p[2])))
    if p[0]=='D' and p[3]=='fn': fn_defs[p[1]][p[5]].add(p[2])
    elif p[0]=='D' and p[3]=='module' and p[2]=='1': file_mod[p[1]]='' if p[5]=='crate/' else p[5]
manifests={}
def manifest_dir(f):
    """Directory of the nearest Cargo.toml above corpus file f: the package that compiles it."""
    d=os.path.dirname(f)
    if d not in manifests:
        manifests[d]=d if os.path.exists(CORPUS+d+'/Cargo.toml') else manifest_dir(d) if d else ''
    return manifests[d]
def second_module_paths():
    """[(descriptor prefix, declaring file, source file)] for every `#[path = ".."] mod name;`
    that compiles a source file under a module path other than the one the oracle defines
    its symbols under.

    The oracle records each source file's definitions once (refs.tsv D rows, one module
    path per file). A file that is compiled a second time through `#[path]` has a second
    set of symbols, <declaring file's module path><name>/..., with no definition record;
    truth.tsv gives their definition file as `ext`. Only a declaration outside an inline
    module is taken: its path is relative to the declaring file's directory."""
    decl=re.compile(r'^#\[path\s*=\s*"([^"]+)"\]\s*(?:#\[[^\]]*\]\s*)*(?:pub(?:\([^)]*\))?\s+)?mod\s+(\w+)\s*;',re.M)
    out=[]
    for f in sorted(file_mod):
        text='\n'.join(source(f))
        if '#[path' not in text: continue
        for m in decl.finditer(text):
            target=os.path.normpath(os.path.join(os.path.dirname(f),m.group(1)))
            prefix=file_mod[f]+m.group(2)+'/'
            if target in file_mod and file_mod[target]!=prefix: out.append((prefix,f,target))
    return out
src={}
def source(f):
    if f not in src:
        try: src[f]=open(CORPUS+f,errors='replace').read().split('\n')
        except OSError: src[f]=[]
    return src[f]
aliases=second_module_paths()
alias_prefixes=tuple({a[0] for a in aliases})
pending=[]  # oracle call records whose symbol lies under a second module path
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
    if df=='ext' and alias_prefixes and sym.startswith('rust-analyzer cargo '):
        _,_,crate,_,desc=sym.split(' ',4)
        if desc.startswith(alias_prefixes): pending.append((cf,int(cl),cn,cname,kind,crate.replace('-','_'),desc))
dir_crates=collections.defaultdict(set)  # package directory -> crate names of the oracle symbols defined in it
for f,crates in file_crates.items(): dir_crates[manifest_dir(f)]|=crates
def source_definition(crate,desc):
    """(file, line) of the one source definition behind an oracle symbol that lies under a
    second module path, or None. The symbol must belong to the package of the declaring
    file, and the rest of its descriptor, re-rooted at the module path the oracle defines
    the source file under, must name exactly one fn definition in that file."""
    found=set()
    for prefix,decl_file,target in aliases:
        if not desc.startswith(prefix) or crate not in dir_crates[manifest_dir(decl_file)]: continue
        found|={(target,ln) for ln in fn_defs[target].get(file_mod[target]+desc[len(prefix):],())}
    return next(iter(found)) if len(found)==1 else None
rerouted=[]
for cf,cl,cn,cname,kind,crate,desc in pending:
    d=source_definition(crate,desc)
    if d is None: continue
    bycaller[(cf,cl,cn)][cname].add((d[0],d[1],kind)); rerouted.append((cf,cl,cn,desc,d))
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
print(f'oracle call records judged by source definition (file compiled under a second module path): {len(rerouted)} of {len(pending)}; second module paths: {len(aliases)}')
for cf,cl,cn,desc,d in rerouted: print(f'  {cf}:{cl} {cn} -> {desc} = {d[0]}:{d[1]}')
with open(D+'wrong_pairs.tsv','w') as o:  # every pair behind the WRONG TARGET and NOT IN TRUTH counts
    for k in sorted(S):
        if k.startswith(('WRONG','NOT IN')):
            for st,a,b in S[k]: o.write(f'{k.split(":")[0]}\t{st}\t{a[2]}:{a[3]} {a[1]}\t{b[2]}:{b[3]} {b[1]}\n')
random.seed(5)
for k in C:
    if k.startswith(('WRONG','NOT IN')):
        print('\n'+k)
        for st,a,b in random.sample(S[k],min(12,len(S[k]))): print(f'  {st:<20} {a[2]}:{a[3]} {a[1]} -> {b[2]}:{b[3]} {b[1]}')
