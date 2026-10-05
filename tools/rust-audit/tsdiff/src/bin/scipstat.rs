//! Count what a SCIP index contains. usage: scipstat <index.scip>
use std::collections::BTreeMap;
struct Rd<'a> { b: &'a [u8], p: usize }
impl<'a> Rd<'a> {
    fn varint(&mut self) -> u64 { let mut v = 0u64; let mut s = 0; loop { let x = self.b[self.p]; self.p += 1; v |= ((x & 0x7f) as u64) << s; if x < 0x80 { return v; } s += 7; } }
    fn next(&mut self) -> Option<(u32, u8, u64, &'a [u8])> {
        if self.p >= self.b.len() { return None; }
        let k = self.varint(); let (f, w) = ((k >> 3) as u32, (k & 7) as u8);
        match w { 0 => { let v = self.varint(); Some((f, w, v, &[])) } 1 => { self.p += 8; Some((f, w, 0, &[])) } 5 => { self.p += 4; Some((f, w, 0, &[])) }
            2 => { let n = self.varint() as usize; let s = &self.b[self.p..self.p + n]; self.p += n; Some((f, w, 0, s)) } _ => panic!() }
    }
}
fn main() {
    let data = std::fs::read(std::env::args().nth(1).unwrap()).unwrap();
    let (mut docs, mut occ, mut defs, mut encl, mut syminfo, mut ext, mut text_bytes, mut doc_bytes) = (0u64, 0u64, 0u64, 0u64, 0u64, 0u64, 0u64, 0u64);
    let mut rel: BTreeMap<&str, u64> = BTreeMap::new(); let mut kinds: BTreeMap<u64, u64> = BTreeMap::new();
    let mut syminfo_scan = |s: &[u8], rel: &mut BTreeMap<&str, u64>, kinds: &mut BTreeMap<u64, u64>, db: &mut u64| {
        let mut q = Rd { b: s, p: 0 };
        while let Some((f, w, v, s)) = q.next() { match (f, w) {
            (5, 0) => *kinds.entry(v).or_default() += 1,
            (3, 2) => *db += s.len() as u64,
            (4, 2) => { let mut z = Rd { b: s, p: 0 }; while let Some((f, w, v, _)) = z.next() { if w == 0 && v != 0 { *rel.entry(match f { 2 => "is_reference", 3 => "is_implementation", 4 => "is_type_definition", 5 => "is_definition", _ => "other" }).or_default() += 1; } } }
            _ => {} } }
    };
    let mut defdocs: std::collections::HashMap<Vec<u8>, std::collections::HashSet<u64>> = Default::default();
    let mut rd = Rd { b: &data, p: 0 };
    while let Some((f, w, _, s)) = rd.next() {
        if f == 3 && w == 2 { ext += 1; syminfo_scan(s, &mut rel, &mut kinds, &mut doc_bytes); }
        if f != 2 || w != 2 { continue; }
        docs += 1;
        let mut r = Rd { b: s, p: 0 };
        while let Some((f, w, _, s)) = r.next() { match (f, w) {
            (5, 2) => text_bytes += s.len() as u64,
            (3, 2) => { syminfo += 1; syminfo_scan(s, &mut rel, &mut kinds, &mut doc_bytes); }
            (2, 2) => { occ += 1; let mut q = Rd { b: s, p: 0 }; let (mut sym, mut isdef): (&[u8], bool) = (&[], false); while let Some((f, w, v, s2)) = q.next() { if f == 2 && w == 2 { sym = s2; } if f == 3 && w == 0 && v & 1 == 1 { defs += 1; isdef = true; } if f == 7 { encl += 1; } } if isdef && !sym.starts_with(b"local ") { defdocs.entry(sym.to_vec()).or_default().insert(docs); } }
            _ => {} } }
    }
    println!("bytes={} documents={docs} occurrences={occ} definitions={defs} with_enclosing_range={encl} symbol_infos={syminfo} external_symbols={ext} documentation_bytes={doc_bytes} embedded_text_bytes={text_bytes}", data.len());
    let multi: Vec<_> = defdocs.iter().filter(|(_, d)| d.len() > 1).collect();
    println!("global_symbols_defined={} defined_in_more_than_one_document={}", defdocs.len(), multi.len());
    for (k, d) in multi.iter().take(4) { println!("  e.g. {} ({} documents)", String::from_utf8_lossy(k), d.len()); }
    println!("relationships={rel:?}"); println!("symbol_kinds={kinds:?}");
}
