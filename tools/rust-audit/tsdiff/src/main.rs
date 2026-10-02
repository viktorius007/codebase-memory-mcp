//! Differential check: tree-sitter-rust facts vs rust-analyzer parser facts, per file.
use std::collections::{BTreeMap, HashSet};
use std::ffi::c_char;
use std::io::Write;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use syntax::ast::{self, AstNode, HasArgList, HasName};
use syntax::{Edition, SourceFile, SyntaxKind::*, SyntaxNode, TextRange};

extern "C" {
    fn ts_facts(src: *const c_char, len: u32, out_len: *mut u32, n_err: *mut u32, n_missing: *mut u32) -> *mut c_char;
    fn ts_facts_free(p: *mut c_char);
}

fn r(x: Option<TextRange>) -> String {
    match x {
        Some(t) => format!("{} {}", u32::from(t.start()), u32::from(t.end())),
        None => "-1 -1".into(),
    }
}
/// Node range without leading attributes (tree-sitter keeps attributes as sibling nodes).
fn trimmed(n: &SyntaxNode) -> TextRange {
    let mut start = n.text_range().start();
    for c in n.children_with_tokens() {
        let k = c.kind();
        if k == ATTR || k == WHITESPACE || k == COMMENT { continue; }
        start = c.text_range().start();
        break;
    }
    TextRange::new(start, n.text_range().end())
}
fn nr<N: AstNode>(x: Option<N>) -> String { r(x.map(|n| trimmed(n.syntax()))) }
fn sr(n: &SyntaxNode) -> String { r(Some(trimmed(n))) }

fn has_let_chain(e: &ast::BinExpr) -> bool {
    if !matches!(e.op_token().map(|t| t.kind()), Some(AMP2)) { return false; }
    if matches!(e.rhs(), Some(ast::Expr::LetExpr(_))) { return true; }
    match e.lhs() {
        Some(ast::Expr::LetExpr(_)) => true,
        Some(ast::Expr::BinExpr(l)) => has_let_chain(&l),
        _ => false,
    }
}

/// Convention differences that are not defects: rust-analyzer parses the meta items
/// inside `cfg_attr(..)`, tree-sitter keeps them as a token tree; a negative const
/// generic argument is a prefix expression in rust-analyzer and a literal in tree-sitter.
fn is_convention(n: &SyntaxNode) -> bool {
    let in_generic_args = |p: SyntaxNode| p.kind() == CONST_ARG && p.parent().is_some_and(|g| g.kind() == GENERIC_ARG_LIST);
    if n.kind() == PREFIX_EXPR && n.parent().is_some_and(in_generic_args) { return true; }
    n.ancestors().any(|a| a.kind() == CFG_ATTR_META)
}

fn ra_facts(root: &SyntaxNode, out: &mut Vec<String>) {
    for n in root.descendants() {
        let k = n.kind();
        if is_convention(&n) { continue; }
        let def = |kind: &str, name: Option<ast::Name>, out: &mut Vec<String>| out.push(format!("DEF {kind} {}", nr(name)));
        match k {
            FN => def("FN", ast::Fn::cast(n).unwrap().name(), out),
            STRUCT => def("STRUCT", ast::Struct::cast(n).unwrap().name(), out),
            ENUM => def("ENUM", ast::Enum::cast(n).unwrap().name(), out),
            UNION => def("UNION", ast::Union::cast(n).unwrap().name(), out),
            TRAIT => def("TRAIT", ast::Trait::cast(n).unwrap().name(), out),
            TYPE_ALIAS => def("TYPE", ast::TypeAlias::cast(n).unwrap().name(), out),
            STATIC => def("STATIC", ast::Static::cast(n).unwrap().name(), out),
            MODULE => def("MOD", ast::Module::cast(n).unwrap().name(), out),
            MACRO_RULES => def("MACRO_RULES", ast::MacroRules::cast(n).unwrap().name(), out),
            VARIANT => def("VARIANT", ast::Variant::cast(n).unwrap().name(), out),
            CONST => {
                let c = ast::Const::cast(n).unwrap();
                match c.name() {
                    Some(nm) => out.push(format!("DEF CONST {}", nr(Some(nm)))),
                    None => out.push(format!("DEF CONST {}", r(c.underscore_token().map(|t| t.text_range())))),
                }
            }
            RECORD_FIELD => {
                let f = ast::RecordField::cast(n).unwrap();
                out.push(format!("DEF FIELD_DEF {}", nr(f.name())));
                out.push(format!("FTY {}", nr(f.ty())));
            }
            TUPLE_FIELD => out.push(format!("FTY {}", nr(ast::TupleField::cast(n).unwrap().ty()))),
            IMPL => {
                let i = ast::Impl::cast(n).unwrap();
                out.push(format!("IMPL {} {}", nr(i.self_ty()), nr(i.trait_())));
            }
            CALL_EXPR => {
                let c = ast::CallExpr::cast(n).unwrap();
                out.push(format!("CALL {} {}", nr(c.expr()), nr(c.arg_list())));
            }
            METHOD_CALL_EXPR => {
                let c = ast::MethodCallExpr::cast(n).unwrap();
                out.push(format!("MCALL {} {} {}", nr(c.receiver()), nr(c.name_ref()), nr(c.arg_list())));
            }
            MACRO_CALL => {
                let c = ast::MacroCall::cast(n).unwrap();
                out.push(format!("MACRO {} {}", nr(c.path()), nr(c.token_tree())));
            }
            FIELD_EXPR => {
                let f = ast::FieldExpr::cast(n).unwrap();
                let name = f.name_ref().map(|x| x.syntax().text_range()).or_else(|| f.index_token().map(|t| t.text_range()));
                out.push(format!("FIELD {} {}", nr(f.expr()), r(name)));
            }
            RECORD_EXPR => out.push(format!("SLIT {}", nr(ast::RecordExpr::cast(n).unwrap().path()))),
            RECORD_EXPR_FIELD => {
                let f = ast::RecordExprField::cast(n).unwrap();
                match f.name_ref() {
                    Some(nm) => out.push(format!("SFIELD {} {}", nr(Some(nm)), nr(f.expr()))),
                    None => out.push(format!("SFIELD {} -1 -1", nr(f.expr()))),
                }
            }
            BIN_EXPR => {
                let b = ast::BinExpr::cast(n.clone()).unwrap();
                if has_let_chain(&b) { continue; }
                let le = b.lhs().map(|l| u32::from(l.syntax().text_range().end()) as i64).unwrap_or(-1);
                let op = b.op_token().map(|t| t.text().to_string()).unwrap_or_default();
                out.push(format!("BIN {} {} {}", sr(&n), le, op));
            }
            RANGE_EXPR => out.push(format!("RANGE {}", sr(&n))),
            LET_STMT => {
                let l = ast::LetStmt::cast(n).unwrap();
                out.push(format!("LET {} {} {}", nr(l.pat()), nr(l.ty()), nr(l.initializer())));
            }
            PARAM => {
                let p = ast::Param::cast(n).unwrap();
                if p.dotdotdot_token().is_some() { continue; }
                out.push(format!("PARAM {} {}", nr(p.pat()), nr(p.ty())));
            }
            RET_TYPE => out.push(format!("RET {}", nr(ast::RetType::cast(n).unwrap().ty()))),
            CLOSURE_EXPR => {
                let c = ast::ClosureExpr::cast(n.clone()).unwrap();
                out.push(format!("CLOSURE {} {}", sr(&n), nr(c.body())));
            }
            MATCH_ARM => {
                let a = ast::MatchArm::cast(n).unwrap();
                out.push(format!("ARM {} {} {}", nr(a.pat()), nr(a.expr()), nr(a.guard().and_then(|g| g.condition()))));
            }
            CAST_EXPR => {
                let c = ast::CastExpr::cast(n).unwrap();
                out.push(format!("CAST {} {}", nr(c.expr()), nr(c.ty())));
            }
            USE_TREE => {
                let u = ast::UseTree::cast(n).unwrap();
                let p = nr(u.path());
                if u.star_token().is_some() { out.push(format!("USE {p} S -1 -1")); }
                else if u.use_tree_list().is_some() { out.push(format!("USE {p} L -1 -1")); }
                else if let Some(rn) = u.rename() {
                    let a = rn.name().map(|x| x.syntax().text_range()).or_else(|| rn.underscore_token().map(|t| t.text_range()));
                    out.push(format!("USE {p} A {}", r(a)));
                } else { out.push(format!("USE {p} P -1 -1")); }
            }
            GENERIC_ARG_LIST => {
                let g = ast::GenericArgList::cast(n.clone()).unwrap();
                let st = g.l_angle_token().map(|t| t.text_range().start()).unwrap_or(n.text_range().start());
                out.push(format!("GARGS {}", r(Some(TextRange::new(st, n.text_range().end())))));
            }
            GENERIC_PARAM_LIST => out.push(format!("TPARAMS {}", sr(&n))),
            IF_EXPR => out.push(format!("IF {} {}", sr(&n), nr(ast::IfExpr::cast(n.clone()).unwrap().condition()))),
            MATCH_EXPR => out.push(format!("MATCH {} {}", sr(&n), nr(ast::MatchExpr::cast(n.clone()).unwrap().expr()))),
            FOR_EXPR => {
                let f = ast::ForExpr::cast(n.clone()).unwrap();
                out.push(format!("FOR {} {} {}", sr(&n), nr(f.pat()), nr(f.iterable())));
            }
            WHILE_EXPR => out.push(format!("WHILE {} {}", sr(&n), nr(ast::WhileExpr::cast(n.clone()).unwrap().condition()))),
            REF_EXPR => out.push(format!("REF {}", sr(&n))),
            TRY_EXPR => out.push(format!("TRY {}", sr(&n))),
            AWAIT_EXPR => out.push(format!("AWAIT {}", sr(&n))),
            INDEX_EXPR => out.push(format!("INDEX {}", sr(&n))),
            PREFIX_EXPR => out.push(format!("UNARY {}", sr(&n))),
            RETURN_EXPR => out.push(format!("RETURN {}", sr(&n))),
            RECORD_PAT => out.push(format!("SPAT {}", nr(ast::RecordPat::cast(n).unwrap().path()))),
            TUPLE_STRUCT_PAT => out.push(format!("TSPAT {}", nr(ast::TupleStructPat::cast(n).unwrap().path()))),
            _ => {}
        }
    }
}

struct FileResult {
    path: String,
    edition: String, // "2021" etc or "RAERR"
    ts_err: u32,
    ts_missing: u32,
    lines: usize,
    per_kind: BTreeMap<String, [u64; 4]>, // ra, ts, only_ra, only_ts
    diffs: Vec<String>,
    ts_errs: Vec<String>,
}

fn kind_of(f: &str) -> String {
    let mut it = f.split(' ');
    let a = it.next().unwrap_or("");
    if a == "DEF" { format!("DEF_{}", it.next().unwrap_or("")) } else { a.to_string() }
}

fn snippet(text: &str, fact: &str) -> String {
    let off = fact.split(' ').filter_map(|x| x.parse::<i64>().ok()).find(|&x| x >= 0).unwrap_or(0) as usize;
    let off = off.min(text.len());
    let line = text.as_bytes()[..off].iter().filter(|&&b| b == b'\n').count() + 1;
    let ls = text[..off].rfind('\n').map(|i| i + 1).unwrap_or(0);
    let le = text[off..].find('\n').map(|i| off + i).unwrap_or(text.len());
    let mut s: String = text[ls..le].trim().chars().take(140).collect();
    s = s.replace('\t', " ");
    format!("{line}\t{s}")
}

fn process(path: &str, max_diffs: usize) -> Option<FileResult> {
    let bytes = std::fs::read(path).ok()?;
    let text = String::from_utf8(bytes).ok()?;
    let mut res = FileResult { path: path.into(), edition: "RAERR".into(), ts_err: 0, ts_missing: 0,
        lines: text.lines().count(), per_kind: BTreeMap::new(), diffs: vec![], ts_errs: vec![] };
    // tree-sitter
    let (mut ol, mut ne, mut nm) = (0u32, 0u32, 0u32);
    let p = unsafe { ts_facts(text.as_ptr() as *const c_char, text.len() as u32, &mut ol, &mut ne, &mut nm) };
    let ts_text = unsafe { String::from_utf8_lossy(std::slice::from_raw_parts(p as *const u8, ol as usize)).into_owned() };
    unsafe { ts_facts_free(p) };
    res.ts_err = ne; res.ts_missing = nm;
    let mut ts_set: HashSet<&str> = HashSet::new();
    for l in ts_text.lines() {
        if let Some(e) = l.strip_prefix('!') { if res.ts_errs.len() < 20 { res.ts_errs.push(format!("{e}\t{}", snippet(&text, e))); } }
        else { ts_set.insert(l); }
    }
    // rust-analyzer: first edition with zero errors
    let mut ra: Option<Vec<String>> = None;
    for (name, ed) in [("2021", Edition::Edition2021), ("2024", Edition::Edition2024), ("2018", Edition::Edition2018), ("2015", Edition::Edition2015)] {
        let parse = SourceFile::parse(&text, ed);
        if parse.errors().is_empty() {
            let mut v = Vec::new();
            ra_facts(&parse.syntax_node(), &mut v);
            res.edition = name.into();
            ra = Some(v);
            break;
        }
    }
    let Some(ra) = ra else { return Some(res) };
    let ra_set: HashSet<&str> = ra.iter().map(|s| s.as_str()).collect();
    for f in &ra_set {
        let e = res.per_kind.entry(kind_of(f)).or_insert([0; 4]);
        e[0] += 1;
        if !ts_set.contains(f) { e[2] += 1; if res.diffs.len() < max_diffs { res.diffs.push(format!("R\t{f}\t{}", snippet(&text, f))); } }
    }
    for f in &ts_set {
        let e = res.per_kind.entry(kind_of(f)).or_insert([0; 4]);
        e[1] += 1;
        if !ra_set.contains(f) { e[3] += 1; if res.diffs.len() < max_diffs { res.diffs.push(format!("T\t{f}\t{}", snippet(&text, f))); } }
    }
    Some(res)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 { eprintln!("usage: tsdiff <listfile> <outprefix> [max_diffs_per_file]"); std::process::exit(2); }
    let max_diffs: usize = args.get(3).and_then(|s| s.parse().ok()).unwrap_or(40);
    let files: Arc<Vec<String>> = Arc::new(std::fs::read_to_string(&args[1]).unwrap().lines().map(String::from).collect());
    let next = Arc::new(AtomicUsize::new(0));
    let results: Arc<Mutex<Vec<FileResult>>> = Arc::new(Mutex::new(Vec::new()));
    let nthreads = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4).min(8);
    let mut hs = vec![];
    for _ in 0..nthreads {
        let (files, next, results) = (files.clone(), next.clone(), results.clone());
        hs.push(std::thread::Builder::new().stack_size(1 << 30).spawn(move || loop {
            let i = next.fetch_add(1, Ordering::Relaxed);
            if i >= files.len() { break; }
            if let Some(res) = process(&files[i], max_diffs) { results.lock().unwrap().push(res); }
        }).unwrap());
    }
    for h in hs { h.join().unwrap(); }
    let mut results = Arc::try_unwrap(results).ok().unwrap().into_inner().unwrap();
    results.sort_by(|a, b| a.path.cmp(&b.path));
    let mut fsum = std::io::BufWriter::new(std::fs::File::create(format!("{}.files.tsv", args[2])).unwrap());
    let mut fdiff = std::io::BufWriter::new(std::fs::File::create(format!("{}.diffs.tsv", args[2])).unwrap());
    let mut ferr = std::io::BufWriter::new(std::fs::File::create(format!("{}.tserrors.tsv", args[2])).unwrap());
    let mut tot: BTreeMap<String, [u64; 4]> = BTreeMap::new();
    let (mut n_ok, mut n_raerr, mut n_ts_bad, mut n_diff_files, mut lines_ok) = (0u64, 0u64, 0u64, 0u64, 0u64);
    for res in &results {
        let (mut only_r, mut only_t) = (0, 0);
        let valid = res.edition != "RAERR";
        if valid {
            n_ok += 1; lines_ok += res.lines as u64;
            if res.ts_err + res.ts_missing > 0 { n_ts_bad += 1; }
            for (k, v) in &res.per_kind {
                let e = tot.entry(k.clone()).or_insert([0; 4]);
                for i in 0..4 { e[i] += v[i]; }
                only_r += v[2]; only_t += v[3];
            }
            if only_r + only_t > 0 { n_diff_files += 1; }
            for d in &res.diffs { writeln!(fdiff, "{}\t{}", res.path, d).unwrap(); }
            for e in &res.ts_errs { writeln!(ferr, "{}\t{}", res.path, e).unwrap(); }
        } else { n_raerr += 1; }
        writeln!(fsum, "{}\t{}\t{}\t{}\t{}\t{}\t{}", res.path, res.edition, res.lines, res.ts_err, res.ts_missing, only_r, only_t).unwrap();
    }
    println!("files_read={} reference_valid={} reference_rejected={} valid_lines={}", results.len(), n_ok, n_raerr, lines_ok);
    println!("valid_files_with_ts_error_or_missing={} valid_files_with_fact_diff={}", n_ts_bad, n_diff_files);
    println!("{:<18}{:>12}{:>12}{:>10}{:>10}", "kind", "ra", "ts", "only_ra", "only_ts");
    let mut s = [0u64; 4];
    for (k, v) in &tot { println!("{:<18}{:>12}{:>12}{:>10}{:>10}", k, v[0], v[1], v[2], v[3]); for i in 0..4 { s[i] += v[i]; } }
    println!("{:<18}{:>12}{:>12}{:>10}{:>10}", "TOTAL", s[0], s[1], s[2], s[3]);
}
