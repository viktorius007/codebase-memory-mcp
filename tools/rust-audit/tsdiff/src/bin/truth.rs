//! Independent call-truth extractor: rust-analyzer syntax for call sites + SCIP occurrences for targets.
//! usage: truth <index.scip> <corpus-root> <out.tsv>
use std::collections::HashMap;
#[path = "../scip_defs.rs"]
mod scip_defs;
use scip_defs::{Doc, Occ, to_off};
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
    let (def, _recovered) = scip_defs::collect_definitions(&docs, &syms, std::path::Path::new(&a[2]));
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
            let fnode = node.ancestors().find(|n| n.kind() == FN);
            let (cf, cl, cn) = match fnode.as_ref().and_then(|f| ast::Fn::cast(f.clone())).and_then(|f| f.name()) {
                Some(nm) => (d.path.clone(), line_of(u32::from(nm.syntax().text_range().start()) as usize).to_string(), nm.text().to_string()),
                None => ("-".into(), "-".into(), "-".into()),
            };
            let occurrence = look(name_r);
            if kind == "indirect" && occurrence.is_none_or(|(sym, _, _)| !fn_like(&syms[sym as usize])) {
                writeln!(out, "{}\t{}\tindirect_unresolved\t{}\t{}\t{}\t-\t-\t-\t{}\t{}",
                    d.path, line_of(u32::from(name_r.start()) as usize), cf, cl, cn, shape, wr).unwrap();
                return;
            }
            let Some((sym, roles, oi)) = occurrence else { writeln!(out, "{}\t{}\t{}_unresolved\t-\t-\t-\t-\t-\t-\t{}\t{}", d.path, line_of(u32::from(name_r.start()) as usize), kind, shape, wr).unwrap(); return };
            used.insert((u32::from(name_r.start()) as usize, u32::from(name_r.end()) as usize));
            if roles & 1 == 1 || !fn_like(&syms[sym as usize]) { return; }
            let (df, dl) = match def.resolve(sym, document, &d.occs[oi].body, &d.occs[oi].range) {
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
                    if let Some(expr) = c.expr() {
                        if let ast::Expr::PathExpr(p) = expr {
                            if let Some(nr) = p.path().and_then(|p| p.segment()).and_then(|s| s.name_ref()) { emit("path", &n, nr.syntax().text_range(), "-", "-", &mut out); }
                        } else {
                            let shape = expr.syntax().text().to_string().split_whitespace().collect::<Vec<_>>().join(" ");
                            let e = strip(expr, &mut String::new());
                            let range = match e {
                                ast::Expr::PathExpr(p) => p.path().and_then(|p| p.segment()).and_then(|s| s.name_ref())
                                    .map(|nr| nr.syntax().text_range()).unwrap_or(n.text_range()),
                                other => other.syntax().text_range(),
                            };
                            emit("indirect", &n, range, &shape, "-", &mut out);
                        }
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
            let (df, dl) = match def.resolve(sym, document, &d.occs[oi].body, &d.occs[oi].range) {
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

#[cfg(test)]
mod tests {
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static NEXT: AtomicUsize = AtomicUsize::new(0);

    fn varint(mut value: u64) -> Vec<u8> {
        let mut result = Vec::new();
        while value > 127 { result.push((value as u8 & 127) | 128); value >>= 7; }
        result.push(value as u8);
        result
    }

    fn field(number: u64, value: &[u8]) -> Vec<u8> {
        [varint(number * 8 + 2), varint(value.len() as u64), value.to_vec()].concat()
    }

    fn occurrence(symbol: &str, range: &[u64], body: &[u64], definition: bool) -> Vec<u8> {
        let mut result = field(1, &range.iter().flat_map(|&v| varint(v)).collect::<Vec<_>>());
        result.extend(field(2, symbol.as_bytes()));
        result.extend([24, u8::from(definition)]);
        result.extend(field(7, &body.iter().flat_map(|&v| varint(v)).collect::<Vec<_>>()));
        field(2, &result)
    }

    fn extract(files: &[(&str, &str, Vec<Vec<u8>>)]) -> (Vec<Vec<String>>, Vec<Vec<String>>) {
        let root = PathBuf::from(std::env::var("TMPDIR").unwrap_or_else(|_| "/tmp/w1-T".into()))
            .join(format!("truth-test-{}-{}", std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed)));
        std::fs::create_dir_all(&root).unwrap();
        let mut index = Vec::new();
        for (path, source, occurrences) in files {
            let file = root.join(path);
            std::fs::create_dir_all(file.parent().unwrap()).unwrap();
            std::fs::write(file, source).unwrap();
            let mut document = field(1, path.as_bytes());
            document.extend(occurrences.concat());
            document.extend([48, 1]);
            index.extend(field(2, &document));
        }
        let scip = root.join("index.scip");
        std::fs::write(&scip, index).unwrap();
        let bins = PathBuf::from(std::env::var("TSDIFF_TEST_BIN_DIR").expect("build truth/refs and set TSDIFF_TEST_BIN_DIR"));
        let outputs: Vec<Vec<Vec<String>>> = ["truth", "refs"].iter().map(|program| {
            let output = root.join(format!("{program}.tsv"));
            let result = std::process::Command::new(bins.join(program))
                .args([scip.as_os_str(), root.as_os_str(), output.as_os_str()]).output().unwrap();
            assert!(result.status.success(), "{}", String::from_utf8_lossy(&result.stderr));
            std::fs::read_to_string(output).unwrap().lines()
                .map(|line| line.split('\t').map(String::from).collect()).collect()
        }).collect();
        std::fs::remove_dir_all(root).unwrap();
        (outputs[0].clone(), outputs[1].clone())
    }

    #[test]
    fn path_compiled_module_definitions_are_recovered_from_the_module_declaration() {
        let module = "rust-analyzer cargo fixture 0 alternate/";
        let old = "rust-analyzer cargo fixture 0 original/target().";
        let target = "rust-analyzer cargo fixture 0 alternate/target().";
        let (truth, refs) = extract(&[
            ("lib.rs", "#[path = \"shared.rs\"]\nmod alternate;\nfn caller() { alternate::target(); }\n", vec![
                occurrence(module, &[1, 4, 13], &[], false),
                occurrence(target, &[2, 25, 31], &[], false)]),
            ("shared.rs", "pub fn target() {}\n", vec![
                occurrence("rust-analyzer cargo fixture 0 original/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence(old, &[0, 7, 13], &[0, 0, 18], true)]),
        ]);
        assert_eq!(truth[0][7..9], ["shared.rs", "1"]);
        assert!(refs.iter().any(|row| row == &vec!["D", "shared.rs", "1", "fn", "target", "alternate/target()."]));
    }

    #[test]
    fn nested_path_compiled_module_without_a_root_occurrence_is_recovered() {
        let alias = "rust-analyzer cargo fixture 0 alternate/inner/target().";
        let (truth, refs) = extract(&[
            ("lib.rs", "#[path = \"outer/mod.rs\"] mod alternate;\nfn caller() { alternate::inner::target(); }\n", vec![
                occurrence("rust-analyzer cargo fixture 0 crate/", &[0, 0, 2, 0], &[0, 0, 2, 0], true),
                occurrence("rust-analyzer cargo fixture 0 alternate/", &[0, 29, 38], &[], false),
                occurrence(alias, &[1, 32, 38], &[], false)]),
            ("outer/mod.rs", "pub mod inner;\n", vec![
                occurrence("rust-analyzer cargo fixture 0 original/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence("rust-analyzer cargo fixture 0 original/inner/", &[0, 8, 13], &[], false)]),
            ("outer/inner.rs", "pub fn target() {}\n", vec![
                occurrence("rust-analyzer cargo fixture 0 original/inner/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence("rust-analyzer cargo fixture 0 original/inner/target().", &[0, 7, 13], &[0, 0, 18], true)]),
        ]);
        assert_eq!(truth[0][7..9], ["outer/inner.rs", "1"]);
        assert!(refs.iter().any(|r| r[0] == "D" && r[1] == "outer/inner.rs" && r[5] == "alternate/inner/target()."));
    }

    #[test]
    fn path_override_changes_the_default_child_directory() {
        let alias = "rust-analyzer cargo fixture 0 alternate/inner/target().";
        let (truth, _) = extract(&[
            ("lib.rs", "#[path = \"outer.rs\"] mod alternate;\n#[path = \"inner.rs\"] mod standalone;\nfn caller() { alternate::inner::target(); }\n", vec![
                occurrence("rust-analyzer cargo fixture 0 crate/", &[0, 0, 3, 0], &[0, 0, 3, 0], true),
                occurrence("rust-analyzer cargo fixture 0 alternate/", &[0, 25, 34], &[], false),
                occurrence("rust-analyzer cargo fixture 0 standalone/", &[1, 25, 35], &[], false),
                occurrence(alias, &[2, 32, 38], &[], false)]),
            ("other.rs", "mod outer;\n", vec![
                occurrence("rust-analyzer cargo fixture 0 crate/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence("rust-analyzer cargo fixture 0 outer/", &[0, 4, 9], &[], false)]),
            ("outer.rs", "pub mod inner;\n", vec![
                occurrence("rust-analyzer cargo fixture 0 outer/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence("rust-analyzer cargo fixture 0 outer/inner/", &[0, 8, 13], &[], false)]),
            ("outer/inner.rs", "pub fn target() {}\n", vec![
                occurrence("rust-analyzer cargo fixture 0 outer/inner/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence("rust-analyzer cargo fixture 0 outer/inner/target().", &[0, 7, 13], &[0, 0, 18], true)]),
            ("inner.rs", "pub fn target() {}", vec![
                occurrence("rust-analyzer cargo fixture 0 standalone/", &[0, 0, 18], &[0, 0, 18], true),
                occurrence("rust-analyzer cargo fixture 0 standalone/target().", &[0, 7, 13], &[0, 0, 18], true)]),
        ]);
        assert_eq!(truth[0][7..9], ["inner.rs", "1"]);
    }

    #[test]
    fn path_compiled_literal_type_and_its_inherent_method_keep_their_sites() {
        let target = "rust-analyzer cargo fixture 0 alternate/impl#[Thing]target().";
        let (truth, refs) = extract(&[
            ("lib.rs", "#[path = \"shared.rs\"]\nmod alternate;\nfn caller() { alternate::Thing::target(); }\n", vec![
                occurrence("rust-analyzer cargo fixture 0 alternate/", &[1, 4, 13], &[], false),
                occurrence("rust-analyzer cargo fixture 0 alternate/Thing#", &[2, 25, 30], &[], false),
                occurrence(target, &[2, 32, 38], &[], false)]),
            ("shared.rs", "pub struct Thing;\nimpl Thing { pub fn target() {} }\n", vec![
                occurrence("rust-analyzer cargo fixture 0 original/", &[0, 0, 2, 0], &[0, 0, 2, 0], true),
                occurrence("rust-analyzer cargo fixture 0 original/Thing#", &[0, 11, 16], &[0, 0, 17], true),
                occurrence("rust-analyzer cargo fixture 0 original/impl#[Thing]target().", &[1, 20, 26], &[1, 13, 31], true)]),
        ]);
        assert_eq!(truth[0][7..9], ["shared.rs", "2"]);
        assert!(refs.iter().any(|row| row == &vec!["D", "shared.rs", "1", "type", "Thing", "alternate/Thing#"]));
    }

    #[test]
    fn alternate_module_with_cfg_twins_stays_ambiguous() {
        let target = "rust-analyzer cargo fixture 0 alternate/target().";
        let (truth, _) = extract(&[
            ("lib.rs", "#[path = \"shared.rs\"]\nmod alternate;\nfn caller() { alternate::target(); }\n", vec![
                occurrence("rust-analyzer cargo fixture 0 alternate/", &[1, 4, 13], &[], false),
                occurrence(target, &[2, 25, 31], &[], false)]),
            ("shared.rs", "#[cfg(a)]\nfn target() {}\n#[cfg(b)]\nfn target() {}\n", vec![
                occurrence("rust-analyzer cargo fixture 0 original/", &[0, 0, 4, 0], &[0, 0, 4, 0], true),
                occurrence("rust-analyzer cargo fixture 0 original/target().", &[1, 3, 9], &[0, 0, 1, 14], true)]),
        ]);
        assert_eq!(truth[0][2], "path_unresolved");
        assert_eq!(truth[0][7..9], ["-", "-"]);
    }

    #[test]
    fn explicit_parent_import_selects_the_definition_in_its_test_crate() {
        let target = "rust-analyzer cargo fixture 0 target().";
        let (truth, refs) = extract(&[
            ("a.rs", "fn target() {}\n", vec![occurrence(target, &[0, 3, 9], &[0, 0, 14], true)]),
            ("tests/main.rs", "mod child;\nfn target() {}\n", vec![
                occurrence("rust-analyzer cargo fixture 0 crate/", &[0, 0, 2, 0], &[0, 0, 2, 0], true),
                occurrence("rust-analyzer cargo fixture 0 child/", &[0, 4, 9], &[], false),
                occurrence(target, &[1, 3, 9], &[1, 0, 14], true)]),
            ("tests/child.rs", "use super::target;\nfn caller() { target(); }\nfn other() { dependency::target(); }\n", vec![
                occurrence(target, &[0, 11, 17], &[], false),
                occurrence(target, &[1, 14, 20], &[], false),
                occurrence(target, &[2, 25, 31], &[], false)]),
        ]);
        assert_eq!(truth[0][7..9], ["tests/main.rs", "2"]);
        assert_eq!(truth[1][7..9], ["-", "-"]);
        assert!(refs.iter().any(|row| row[0] == "R" && row[1] == "tests/child.rs" && row[6..8] == ["tests/main.rs", "2"]));
        let target = "rust-analyzer cargo fixture 0 sibling/target().";
        let (truth, _) = extract(&[
            ("tests/main.rs", "mod sibling;\nmod child;\npub use sibling::target;\n", vec![
                occurrence("rust-analyzer cargo fixture 0 crate/", &[0, 0, 3, 0], &[0, 0, 3, 0], true),
                occurrence("rust-analyzer cargo fixture 0 sibling/", &[0, 4, 11], &[], false),
                occurrence("rust-analyzer cargo fixture 0 child/", &[1, 4, 9], &[], false)]),
            ("tests/other_main.rs", "#[path = \"child.rs\"] mod child;\npub use sibling::target;\n", vec![
                occurrence("rust-analyzer cargo other 0 crate/", &[0, 0, 2, 0], &[0, 0, 2, 0], true),
                occurrence("rust-analyzer cargo other 0 child/", &[0, 25, 30], &[], false)]),
            ("tests/sibling.rs", "pub fn target() {}\n", vec![
                occurrence("rust-analyzer cargo fixture 0 sibling/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence(target, &[0, 7, 13], &[0, 0, 18], true)]),
            ("tests/child.rs", "use super::target;\nfn caller() { target(); }\n", vec![
                occurrence(target, &[0, 11, 17], &[], false),
                occurrence(target, &[1, 14, 20], &[], false)]),
        ]);
        assert_eq!(truth[0][7..9], ["tests/sibling.rs", "1"]);
    }

    #[test]
    fn library_target_compiled_under_another_crate_gets_its_own_definition() {
        let target = "rust-analyzer cargo fixture 0.1.0 target().";
        let (truth, refs) = extract(&[
            ("Cargo.toml", "[package]\nname = \"fixture\"\nversion = \"0.1.0\"\n[lib]\npath = \"source.rs\"\n", vec![]),
            ("source.rs", "pub fn target() {}\n", vec![
                occurrence("rust-analyzer cargo host 1.0.0 crate/", &[0, 0, 1, 0], &[0, 0, 1, 0], true),
                occurrence("rust-analyzer cargo host 1.0.0 target().", &[0, 7, 13], &[0, 0, 18], true)]),
            ("src/main.rs", "use fixture::target as imported;\nfn target() { imported(); }\n", vec![
                occurrence("rust-analyzer cargo fixture 0.1.0 crate/", &[0, 0, 2, 0], &[0, 0, 2, 0], true),
                occurrence("rust-analyzer cargo fixture 0.1.0 crate/", &[0, 4, 11], &[], false),
                occurrence(target, &[0, 13, 19], &[], false),
                occurrence(target, &[1, 3, 9], &[1, 0, 27], true),
                occurrence(target, &[1, 14, 22], &[], false)]),
        ]);
        assert_eq!(truth[0][7..9], ["source.rs", "1"]);
        assert!(refs.iter().any(|row| row == &vec!["D", "source.rs", "1", "fn", "target", "target()."]));
    }

    #[test]
    fn an_import_without_a_local_body_cannot_select_the_local_namesake() {
        let target = "rust-analyzer cargo fixture 0 target().";
        let (truth, _) = extract(&[("main.rs", "use dependency::target as imported;\nfn target() { imported(); }\n", vec![
            occurrence("rust-analyzer cargo fixture 0 crate/", &[0, 4, 14], &[], false),
            occurrence(target, &[0, 16, 22], &[], false),
            occurrence(target, &[1, 3, 9], &[1, 0, 27], true),
            occurrence(target, &[1, 14, 22], &[], false),
        ])]);
        assert_eq!(truth[0][2], "path_unresolved");
        assert_eq!(truth[0][7..9], ["-", "-"]);
    }

    #[test]
    fn parenthesised_parameter_calls_are_indirect_sites() {
        let (truth, _) = extract(&[("lib.rs", "fn caller(f: fn()) { (f)(); }\n", vec![
            occurrence("local 0", &[0, 10, 11], &[0, 10, 11], true),
            occurrence("local 0", &[0, 22, 23], &[0, 10, 11], false),
        ])]);
        assert_eq!(truth.len(), 1);
        assert_eq!(truth[0], vec!["lib.rs", "1", "indirect_unresolved", "lib.rs", "1", "caller", "-", "-", "-", "(f)", "-"]);
    }
}
