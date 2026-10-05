//! Resolve colliding SCIP symbols only with rust-analyzer's document-local definition body.
use std::collections::HashMap;

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
    pub fn insert(&mut self, symbol: u32, document: usize, range: &[i32], body: &[i32]) {
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
        }
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
