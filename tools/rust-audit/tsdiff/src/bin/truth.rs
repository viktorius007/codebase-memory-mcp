//! Independent call-truth extractor: rust-analyzer syntax for call sites + SCIP occurrences for targets.
//! usage: truth <index.scip> <corpus-root> <out.tsv>
use std::collections::HashMap;
#[path = "../scip_defs.rs"]
mod scip_defs;
use scip_defs::{Definitions, to_off};
use std::io::Write;
use syntax::ast::{self, AstNode, HasName};
use syntax::{Edition, SourceFile, SyntaxKind::*, SyntaxNode, TextRange, TextSize};

struct Rd<'a> { b: &'a [u8], p: usize }
impl<'a> Rd<'a> {
    fn varint(&mut self) -> u64 { let mut v = 0u64; let mut s = 0; loop { let x = self.b[self.p]; self.p += 1; v |= ((x & 0x7f) as u64) << s; if x < 0x80 { return v; } s += 7; } }
    fn next(&mut self) -> Option<(u32, u8, u64, &'a [u8])> {
        if self.p >= self.b.len() { return None; }
        let k = self.varint(); let (f, w) = ((k >> 3) as u32, (k & 7) as u8);
        match w {
            0 => { let v = self.varint(); Some((f, w, v, &[])) }
            1 => { self.p += 8; Some((f, w, 0, &[])) }
            5 => { self.p += 4; Some((f, w, 0, &[])) }
            2 => { let n = self.varint() as usize; let s = &self.b[self.p..self.p + n]; self.p += n; Some((f, w, 0, s)) }
            _ => panic!("wire type {w}"),
        }
    }
}
struct Occ { range: Vec<i32>, body: Vec<i32>, sym: u32, roles: u32 }
struct Doc { path: String, occs: Vec<Occ>, enc: u64 }

fn intern(m: &mut HashMap<String, u32>, v: &mut Vec<String>, s: &str) -> u32 {
    if let Some(&i) = m.get(s) { return i; }
    let i = v.len() as u32; v.push(s.to_string()); m.insert(s.to_string(), i); i
}

fn line_starts(t: &str) -> Vec<usize> { let mut v = vec![0]; for (i, b) in t.bytes().enumerate() { if b == b'\n' { v.push(i + 1); } } v }
fn fn_like(sym: &str) -> bool { sym.ends_with(").") }

fn strip(mut e: ast::Expr, w: &mut String) -> ast::Expr {
    loop {
        e = match e {
            ast::Expr::ParenExpr(p) => match p.expr() { Some(x) => x, None => return ast::Expr::ParenExpr(p) },
            ast::Expr::RefExpr(p) => match p.expr() { Some(x) => x, None => return ast::Expr::RefExpr(p) },
            ast::Expr::TryExpr(p) => match p.expr() { Some(x) => { w.push('?'); x } None => return ast::Expr::TryExpr(p) },
            ast::Expr::AwaitExpr(p) => match p.expr() { Some(x) => { w.push('a'); x } None => return ast::Expr::AwaitExpr(p) },
            ast::Expr::PrefixExpr(p) => match p.expr() { Some(x) => x, None => return ast::Expr::PrefixExpr(p) },
            other => return other,
        }
    }
}
fn binding(fnode: &SyntaxNode, name: &str, before: TextSize) -> &'static str {
    let Some(f) = ast::Fn::cast(fnode.clone()) else { return "no_fn" };
    let mut best: Option<(&'static str, TextSize)> = None;
    for n in fnode.descendants() {
        if n.text_range().start() >= before { continue; }
        let kind = match n.kind() {
            PARAM => {
                let p = ast::Param::cast(n.clone()).unwrap();
                match p.pat() { Some(ast::Pat::IdentPat(ip)) if ip.name().map(|x| x.text() == name).unwrap_or(false) => if p.ty().is_some() { "param_typed" } else { "closure_param" }, _ => continue }
            }
            LET_STMT => {
                let l = ast::LetStmt::cast(n.clone()).unwrap();
                match l.pat() { Some(ast::Pat::IdentPat(ip)) if ip.name().map(|x| x.text() == name).unwrap_or(false) => {
                    if l.ty().is_some() { "let_typed" } else {
                        let mut w = String::new();
                        match l.initializer().map(|e| strip(e, &mut w)) {
                            Some(ast::Expr::RecordExpr(_)) => "let=struct_lit",
                            Some(ast::Expr::CallExpr(_)) => if w.is_empty() { "let=path_call" } else { "let=path_call?/await" },
                            Some(ast::Expr::MethodCallExpr(_)) => "let=method_call",
                            Some(ast::Expr::MacroExpr(_)) => "let=macro",
                            Some(ast::Expr::Literal(_)) => "let=literal",
                            _ => "let=other",
                        }
                    } } _ => continue }
            }
            _ => continue,
        };
        if best.map(|b| n.text_range().start() >= b.1).unwrap_or(true) { best = Some((kind, n.text_range().start())); }
    }
    let _ = f;
    best.map(|b| b.0).unwrap_or("pattern_binding")
}
fn recv_shape(recv: ast::Expr, fnode: Option<&SyntaxNode>, at: TextSize) -> (String, String) {
    let mut w = String::new();
    let e = strip(recv, &mut w);
    let shape = match &e {
        ast::Expr::PathExpr(p) => {
            let t = p.syntax().text().to_string();
            if t == "self" { "self".to_string() }
            else if !t.contains("::") && t.chars().next().map(|c| c.is_lowercase() || c == '_').unwrap_or(false) {
                format!("local:{}", fnode.map(|f| binding(f, &t, at)).unwrap_or("no_fn"))
            } else { "path_or_const".to_string() }
        }
        ast::Expr::FieldExpr(f) => {
            let mut root = f.expr();
            while let Some(ast::Expr::FieldExpr(g)) = root.clone() { root = g.expr(); }
            match root { Some(ast::Expr::PathExpr(p)) if p.syntax().text() == "self" => "self.field".to_string(), Some(ast::Expr::PathExpr(_)) => "local.field".to_string(), _ => "expr.field".to_string() }
        }
        ast::Expr::CallExpr(_) => "call_result".to_string(),
        ast::Expr::MethodCallExpr(_) => "method_chain".to_string(),
        ast::Expr::MacroExpr(_) => "macro".to_string(),
        ast::Expr::Literal(_) => "literal".to_string(),
        ast::Expr::IndexExpr(_) => "index".to_string(),
        _ => "other".to_string(),
    };
    (shape, if w.is_empty() { "-".into() } else { w })
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let data = std::fs::read(&a[1]).unwrap();
    let (mut smap, mut syms) = (HashMap::new(), Vec::new());
    let mut docs: Vec<Doc> = Vec::new();
    let mut rd = Rd { b: &data, p: 0 };
    while let Some((f, w, _, s)) = rd.next() {
        if f != 2 || w != 2 { continue; }
        let mut d = Doc { path: String::new(), occs: vec![], enc: 0 };
        let mut r = Rd { b: s, p: 0 };
        while let Some((f, w, v, s)) = r.next() {
            match (f, w) {
                (1, 2) => d.path = String::from_utf8_lossy(s).into_owned(),
                (6, 0) => d.enc = v,
                (2, 2) => {
                    let mut o = Occ { range: vec![], body: vec![], sym: u32::MAX, roles: 0 };
                    let mut q = Rd { b: s, p: 0 };
                    while let Some((f, w, v, s)) = q.next() {
                        match (f, w) {
                            (1, 2) => { let mut z = Rd { b: s, p: 0 }; while z.p < s.len() { o.range.push(z.varint() as i32); } }
                            (1, 0) => o.range.push(v as i32),
                            (2, 2) => o.sym = intern(&mut smap, &mut syms, std::str::from_utf8(s).unwrap_or("")),
                            (3, 0) => o.roles = v as u32,
                            (7, 2) => { let mut z = Rd { b: s, p: 0 }; while z.p < s.len() { o.body.push(z.varint() as i32); } }
                            (7, 0) => o.body.push(v as i32),
                            _ => {}
                        }
                    }
                    if o.sym != u32::MAX { d.occs.push(o); }
                }
                _ => {}
            }
        }
        docs.push(d);
    }
    eprintln!("docs={} symbols={} enc_of_first={}", docs.len(), syms.len(), docs.first().map(|d| d.enc).unwrap_or(9));
    // definition sites
    let mut def = Definitions::default();
    for (di, d) in docs.iter().enumerate() { for o in &d.occs { if o.roles & 1 == 1 { def.insert(o.sym, di, &o.range, &o.body); } } }
    let recorded: std::collections::HashSet<u32> = docs.iter().flat_map(|d| &d.occs)
        .filter(|o| o.roles & 1 == 1).map(|o| o.sym).collect();
    // SCIP's enclosing_range is the target body, not the caller's lexical scope.
    for (di, d) in docs.iter().enumerate() {
        let Ok(text) = std::fs::read_to_string(format!("{}/{}", a[2], d.path)) else { continue };
        let ls = line_starts(&text);
        let parsed = SourceFile::parse(&text, Edition::Edition2024);
        if !parsed.errors().is_empty() { continue; }
        let root = parsed.syntax_node();
        for o in &d.occs {
            if recorded.contains(&o.sym) { continue; }
            let Some(range) = scip_defs::source_function(&root, &text, &ls, d.enc,
                                                        &syms[o.sym as usize], &o.body) else { continue };
            def.insert(o.sym, di, &range, &o.body);
        }
    }
    let mut out = std::io::BufWriter::new(std::fs::File::create(&a[3]).unwrap());
    let (mut nofile, mut raerr) = (0, 0);
    let mut ambiguous = 0usize;
    for (document, d) in docs.iter().enumerate() {
        let Ok(text) = std::fs::read_to_string(format!("{}/{}", a[2], d.path)) else { nofile += 1; continue };
        let ls = line_starts(&text);
        let mut by_range: HashMap<(usize, usize), (u32, u32, usize)> = HashMap::new();
        let mut flat: Vec<(usize, usize, u32, u32, usize)> = vec![];
        for (oi, o) in d.occs.iter().enumerate() {
            let (sl, sc, el, ec) = if o.range.len() == 3 { (o.range[0], o.range[1], o.range[0], o.range[2]) } else { (o.range[0], o.range[1], o.range[2], o.range[3]) };
            let (Some(s), Some(e)) = (to_off(&text, &ls, sl, sc, d.enc), to_off(&text, &ls, el, ec, d.enc)) else { continue };
            by_range.insert((s, e), (o.sym, o.roles, oi)); flat.push((s, e, o.sym, o.roles, oi));
        }
        let mut root = None;
        for ed in [Edition::Edition2024, Edition::Edition2021, Edition::Edition2018, Edition::Edition2015] {
            let p = SourceFile::parse(&text, ed);
            if p.errors().is_empty() { root = Some(p.syntax_node()); break; }
        }
        let Some(root) = root else { raerr += 1; continue };
        let line_of = |off: usize| ls.partition_point(|&s| s <= off);
        let look = |r: TextRange| by_range.get(&(u32::from(r.start()) as usize, u32::from(r.end()) as usize)).copied();
        let mut used: std::collections::HashSet<(usize, usize)> = Default::default();
        let mut emit = |kind: &str, node: &SyntaxNode, name_r: TextRange, shape: &str, wr: &str, out: &mut dyn Write| {
            let Some((sym, roles, oi)) = look(name_r) else { writeln!(out, "{}\t{}\t{}_unresolved\t-\t-\t-\t-\t-\t-\t{}\t{}", d.path, line_of(u32::from(name_r.start()) as usize), kind, shape, wr).unwrap(); return };
            used.insert((u32::from(name_r.start()) as usize, u32::from(name_r.end()) as usize));
            if roles & 1 == 1 || !fn_like(&syms[sym as usize]) { return; }
            let fnode = node.ancestors().find(|n| n.kind() == FN);
            let (cf, cl, cn) = match fnode.as_ref().and_then(|f| ast::Fn::cast(f.clone())).and_then(|f| f.name()) {
                Some(nm) => (d.path.clone(), line_of(u32::from(nm.syntax().text_range().start()) as usize).to_string(), nm.text().to_string()),
                None => ("-".into(), "-".into(), "-".into()),
            };
            let (df, dl) = match def.resolve(sym, document, &d.occs[oi].body) {
                Ok(Some((di, l))) => (docs[di].path.clone(), (l + 1).to_string()),
                Ok(None) => ("ext".into(), "-".into()),
                Err(()) => { ambiguous += 1; ("-".into(), "-".into()) }
            };
            writeln!(out, "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", d.path, line_of(u32::from(name_r.start()) as usize), if df == "-" { format!("{kind}_unresolved") } else { kind.into() }, cf, cl, cn, syms[sym as usize], df, dl, shape, wr).unwrap();
        };
        let mut macro_ranges: Vec<TextRange> = vec![];
        for n in root.descendants() {
            match n.kind() {
                CALL_EXPR => {
                    let c = ast::CallExpr::cast(n.clone()).unwrap();
                    if let Some(ast::Expr::PathExpr(p)) = c.expr() {
                        if let Some(nr) = p.path().and_then(|p| p.segment()).and_then(|s| s.name_ref()) { emit("path", &n, nr.syntax().text_range(), "-", "-", &mut out); }
                    }
                }
                METHOD_CALL_EXPR => {
                    let c = ast::MethodCallExpr::cast(n.clone()).unwrap();
                    if let (Some(nr), Some(rc)) = (c.name_ref(), c.receiver()) {
                        let fnode = n.ancestors().find(|x| x.kind() == FN);
                        let (shape, wr) = recv_shape(rc, fnode.as_ref(), n.text_range().start());
                        emit("method", &n, nr.syntax().text_range(), &shape, &wr, &mut out);
                    }
                }
                MACRO_CALL => if let Some(tt) = ast::MacroCall::cast(n.clone()).unwrap().token_tree() { macro_ranges.push(tt.syntax().text_range()); },
                _ => {}
            }
        }
        // function-like references that are not syntactic call sites
        for (s, e, sym, roles, oi) in flat {
            if roles & 1 == 1 || !fn_like(&syms[sym as usize]) || used.contains(&(s, e)) { continue; }
            let in_macro = macro_ranges.iter().any(|r| u32::from(r.start()) as usize <= s && e <= u32::from(r.end()) as usize);
            let tok = root.token_at_offset(TextSize::from(s as u32)).right_biased();
            let fnode = tok.as_ref().and_then(|t| t.parent_ancestors().find(|n| n.kind() == FN));
            // Procedural expansion can mark the source declaration as a reference.
            if fnode.as_ref().and_then(|f| ast::Fn::cast(f.clone())).and_then(|f| f.name())
                .is_some_and(|name| name.syntax().text_range() == TextRange::new(TextSize::from(s as u32), TextSize::from(e as u32))) { continue; }
            let (cf, cl, cn) = match fnode.and_then(ast::Fn::cast).and_then(|f| f.name()) {
                Some(nm) => (d.path.clone(), line_of(u32::from(nm.syntax().text_range().start()) as usize).to_string(), nm.text().to_string()),
                None => ("-".into(), "-".into(), "-".into()),
            };
            let (df, dl) = match def.resolve(sym, document, &d.occs[oi].body) {
                Ok(Some((di, l))) => (docs[di].path.clone(), (l + 1).to_string()),
                Ok(None) => ("ext".into(), "-".into()),
                Err(()) => { ambiguous += 1; ("-".into(), "-".into()) }
            };
            writeln!(out, "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t-\t-", d.path, line_of(s), if df == "-" { "ref_unresolved" } else if in_macro { "ref_in_macro" } else { "ref_not_call" }, cf, cl, cn, syms[sym as usize], df, dl).unwrap();
        }
    }
    eprintln!("ambiguous_definition_references={ambiguous}");
    eprintln!("missing_files={nofile} reference_parse_errors={raerr}");
}
