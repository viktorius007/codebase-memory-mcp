//! Independent definition/reference truth extractor: rust-analyzer syntax for call sites + SCIP occurrences for targets.
//! usage: truth <index.scip> <corpus-root> <out.tsv>
use std::collections::HashMap;
#[path = "../scip_defs.rs"]
mod scip_defs;
use scip_defs::{Doc, Occ, name_of, to_off};
use std::io::Write;
use syntax::ast::{self, AstNode, HasName};
use syntax::{Edition, SourceFile, SyntaxKind::*, TextSize};

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


fn kind_of(sym: &str) -> &'static str {
    if sym.ends_with(").") { "fn" } else if sym.ends_with('#') { "type" } else if sym.ends_with('/') { "module" } else if sym.ends_with('!') { "macro" }
    else if sym.ends_with(')') { "param" } else if sym.ends_with(']') { "typeparam" } else if sym.ends_with('.') { "term" } else { "other" }
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
                            (2, 2) => o.sym = intern(&mut smap, &mut syms, std::str::from_utf8(s).unwrap_or("")),
                            (3, 0) => o.roles = v as u32,
                            (7, 2) => { let mut z = Rd { b: s, p: 0 }; while z.p < s.len() { o.body.push(z.varint() as i32); } }
                            (7, 0) => o.body.push(v as i32),
                            _ => {}
                        }
                    }
                    if o.sym != u32::MAX && !syms[o.sym as usize].starts_with("local ") { d.occs.push(o); }
                }
                _ => {}
            }
        }
        docs.push(d);
    }
    let (def, recovered) = scip_defs::collect_definitions(&docs, &syms, std::path::Path::new(&a[2]));
    let mut out = std::io::BufWriter::new(std::fs::File::create(&a[3]).unwrap());
    let mut ambiguous = 0usize;
    for (document, d) in docs.iter().enumerate() {
        let Ok(text) = std::fs::read_to_string(format!("{}/{}", a[2], d.path)) else { continue };
        let ls = line_starts(&text);
        let mut root = None;
        for ed in [Edition::Edition2024, Edition::Edition2021, Edition::Edition2018, Edition::Edition2015] {
            let p = SourceFile::parse(&text, ed);
            if p.errors().is_empty() { root = Some(p.syntax_node()); break; }
        }
        let Some(root) = root else { continue };
        let line_of = |off: usize| ls.partition_point(|&s| s <= off);
        // impl blocks: I <file> <line> <self type text> <trait text or ->
        for n in root.descendants().filter(|n| n.kind() == IMPL) {
            let i = ast::Impl::cast(n.clone()).unwrap();
            let clean = |t: String| t.split_whitespace().collect::<Vec<_>>().join(" ");
            writeln!(out, "I\t{}\t{}\t{}\t{}", d.path, line_of(u32::from(n.text_range().start()) as usize),
                i.self_ty().map(|t| clean(t.syntax().text().to_string())).unwrap_or("-".into()), i.trait_().map(|t| clean(t.syntax().text().to_string())).unwrap_or("-".into())).unwrap();
        }
        for &(_, symbol, line) in recovered.iter().filter(|&&(di, _, _)| di == document) {
            let sym = &syms[symbol as usize];
            writeln!(out, "D\t{}\t{}\t{}\t{}\t{}", d.path, line, kind_of(sym), name_of(sym), sym.splitn(5, ' ').nth(4).unwrap_or("")).unwrap();
        }
        for o in &d.occs {
            let sym = &syms[o.sym as usize];
            let Some(s) = to_off(&text, &ls, o.range[0], o.range[1], d.enc) else { continue };
            if o.roles & 1 == 1 {
                writeln!(out, "D\t{}\t{}\t{}\t{}\t{}", d.path, o.range[0] + 1, kind_of(sym), name_of(sym), sym.splitn(5, ' ').nth(4).unwrap_or("")).unwrap();
                continue;
            }
            let (di, dl) = match def.resolve(o.sym, document, &o.body, &o.range) {
                Ok(Some(site)) => site,
                Ok(None) => continue,
                Err(()) => { ambiguous += 1; continue; }
            };
            let tok = root.token_at_offset(TextSize::from(s as u32)).right_biased();
            let (mut in_use, mut encl) = (false, None);
            if let Some(t) = &tok { for anc in t.parent_ancestors() { match anc.kind() { USE => in_use = true, FN if encl.is_none() => encl = ast::Fn::cast(anc).and_then(|f| f.name()), _ => {} } } }
            if encl.as_ref().is_some_and(|name| u32::from(name.syntax().text_range().start()) as usize == s) { continue; }
            let (el, en) = match encl { Some(nm) => (line_of(u32::from(nm.syntax().text_range().start()) as usize).to_string(), nm.text().to_string()), None => ("-".into(), "-".into()) };
            writeln!(out, "R\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", d.path, o.range[0] + 1, if in_use { "use" } else { "ref" }, el, en, docs[di].path, dl + 1, kind_of(sym), name_of(sym)).unwrap();
        }
    }
    eprintln!("ambiguous_definition_references={ambiguous}");
}
