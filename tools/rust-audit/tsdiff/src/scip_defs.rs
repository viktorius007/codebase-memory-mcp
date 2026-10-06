//! Resolve colliding SCIP symbols only with rust-analyzer's document-local definition body.
use std::collections::HashMap;
use syntax::ast::{self, AstNode, HasAttrs, HasName};
use syntax::{SyntaxNode, TextRange, TextSize};

struct Definition {
    document: usize,
    range: Vec<i32>,
    body: Vec<i32>,
}

#[derive(Default)]
/// Definition occurrences grouped by SCIP symbol, retaining document and body identity.
pub struct Definitions(HashMap<u32, Vec<Definition>>);

impl Definitions {
    /// Repeated occurrences at the same document span represent one definition site.
    pub fn insert(&mut self, symbol: u32, document: usize, range: &[i32], body: &[i32]) -> bool {
        let definitions = self.0.entry(symbol).or_default();
        if !definitions
            .iter()
            .any(|d| d.document == document && d.range == range)
        {
            definitions.push(Definition {
                document,
                range: range.to_vec(),
                body: body.to_vec(),
            });
            return true;
        }
        false
    }

    /// Return a proven site, None for an external symbol, or Err for an ambiguous collision.
    pub fn resolve(
        &self,
        symbol: u32,
        document: usize,
        body: &[i32],
    ) -> Result<Option<(usize, i32)>, ()> {
        let Some(definitions) = self.0.get(&symbol) else {
            return Ok(None);
        };
        if definitions.len() == 1 {
            let d = &definitions[0];
            return Ok(Some((d.document, d.range[0])));
        }
        // The SCIP writer emits definition_body only when the resolved target is in
        // this document (rust-analyzer/src/cli/scip.rs). A local namesake alone is
        // insufficient: an imported item can shadow it. Never pick the first twin.
        let mut matches = definitions
            .iter()
            .filter(|d| !body.is_empty() && d.document == document && d.body == body);
        let Some(d) = matches.next() else {
            return Err(());
        };
        if matches.next().is_some() {
            return Err(());
        }
        Ok(Some((d.document, d.range[0])))
    }
}

/// A same-document SCIP body proves a source function even when a procedural
/// attribute erased its definition occurrence. Names alone never identify a site.
pub fn source_function(root: &SyntaxNode, text: &str, ls: &[usize], enc: u64,
                       symbol: &str, coordinates: &[i32]) -> Option<[i32; 3]> {
    let (sl, sc, el, ec) = match coordinates {
        [sl, sc, ec] => (*sl, *sc, *sl, *ec),
        [sl, sc, el, ec] => (*sl, *sc, *el, *ec),
        _ => return None,
    };
    let start = to_off(text, ls, sl, sc, enc)?;
    let end = to_off(text, ls, el, ec, enc)?;
    if end < start { return None; }
    let body = TextRange::new(TextSize::from(start as u32), TextSize::from(end as u32));
    let name = symbol.strip_suffix("().")?.rsplit(['/', '#', ']', ' ']).next()?;
    let mut matches = root.descendants().filter_map(ast::Fn::cast).filter_map(|f| {
        let source_name = f.name()?;
        if source_name.text().trim_start_matches("r#") != name.trim_matches('`') {
            return None;
        }
        let range = source_name.syntax().text_range();
        let identifies = body.contains_range(range)
            || f.attrs().any(|a| a.syntax().text_range().contains_range(body));
        (identifies && f.syntax().text_range().contains_range(body)).then_some(range)
    });
    let range = matches.next()?;
    if matches.next().is_some() { return None; }
    let offset = u32::from(range.start()) as usize;
    let line = ls.partition_point(|&s| s <= offset) - 1;
    let column = |at: usize| -> i32 {
        let prefix = &text[ls[line]..at];
        if enc == 1 { prefix.len() as i32 }
        else if enc == 0 || enc == 2 { prefix.encode_utf16().count() as i32 }
        else { prefix.chars().count() as i32 }
    };
    Some([line as i32, column(offset), column(u32::from(range.end()) as usize)])
}

pub fn to_off(t: &str, ls: &[usize], line: i32, ch: i32, enc: u64) -> Option<usize> {
    let s = *ls.get(line as usize)?;
    if enc == 1 || enc == 0 && false { return Some(s + ch as usize); }
    let rest = &t[s..]; let mut units = 0i32;
    for (i, c) in rest.char_indices() {
        if units >= ch { return Some(s + i); }
        units += if enc == 2 || enc == 0 { c.len_utf16() as i32 } else { 1 };
    }
    Some(s + rest.len())
}
