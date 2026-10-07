//! Recover SCIP target identities using document bodies and explicit file-module bindings.
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::Path;
use syntax::ast::{self, AstNode, HasAttrs, HasGenericArgs, HasName};
use syntax::{Edition, SourceFile, SyntaxKind, SyntaxNode, TextRange, TextSize};

struct Definition {
    document: usize,
    range: Vec<i32>,
    body: Vec<i32>,
}

#[derive(Default)]
/// Definition occurrences grouped by SCIP symbol, retaining document and body identity.
pub struct Definitions {
    sites: HashMap<u32, Vec<Definition>>,
    internal: HashSet<u32>,
    imports: HashMap<(u32, usize, Vec<i32>), HashSet<usize>>,
}

impl Definitions {
    /// Repeated occurrences at the same document span represent one definition site.
    pub fn insert(&mut self, symbol: u32, document: usize, range: &[i32], body: &[i32]) -> bool {
        let definitions = self.sites.entry(symbol).or_default();
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

    fn bind_imports(&mut self, imports: Vec<(usize, u32, Vec<i32>, Option<u32>)>,
                    libraries: &HashSet<(usize, usize, u32)>, modules: &[(usize, usize, String, bool)]) {
        for (document, sym, range, library) in imports {
            let candidates: HashSet<_> = if let Some(library) = library {
                libraries.iter().filter(|(_, _, root)| *root == library).map(|(_, child, _)| *child).collect()
            } else {
                modules.iter().filter(|(_, child, _, _)| *child == document).map(|(parent, _, _, _)| *parent).collect()
            };
            let sites = self.sites.get(&sym);
            let direct = sites.is_some_and(|sites| sites.iter().any(|s| candidates.contains(&s.document)));
            let local = sites.is_some_and(|sites| sites.iter().any(|s| s.document == document));
            // A parent may re-export a symbol defined elsewhere. Only a declaration
            // in the proven parent can narrow a collision; a unique SCIP site stands.
            if library.is_some() && (direct || local) || library.is_none() && candidates.len() == 1 && direct {
                self.imports.entry((sym, document, range)).or_default().extend(candidates);
            }
        }
    }


    /// Return a proven site, None for an external symbol, or Err for an ambiguous collision.
    pub fn resolve(
        &self,
        symbol: u32,
        document: usize,
        body: &[i32],
        range: &[i32],
    ) -> Result<Option<(usize, i32)>, ()> {
        let Some(definitions) = self.sites.get(&symbol) else {
            return if self.internal.contains(&symbol) { Err(()) } else { Ok(None) };
        };
        // A body is document-local target evidence, even if another document has
        // the only recorded definition. It must agree with the selected site.
        let matches: Vec<_> = definitions.iter().filter(|d| {
            if !body.is_empty() {
                d.document == document && d.body == body
            } else if let Some(imports) = self.imports.get(&(symbol, document, range.to_vec())) {
                imports.contains(&d.document)
            } else {
                true
            }
        }).collect();
        if matches.len() != 1 { return Err(()); }
        let d = matches[0];
        Ok(Some((d.document, d.range[0])))
    }
}


pub struct Occ { pub range: Vec<i32>, pub body: Vec<i32>, pub sym: u32, pub roles: u32 }
pub struct Doc { pub path: String, pub occs: Vec<Occ>, pub enc: u64 }

/// Collect definitions and recover alternate compilation paths from SCIP-backed
/// file-module declarations. Explicit parent imports disambiguate test-crate twins.
pub fn collect_definitions(docs: &[Doc], syms: &[String], corpus: &Path)
    -> (Definitions, Vec<(usize, u32, i32)>)
{
    let mut def = Definitions::default();
    for (di, d) in docs.iter().enumerate() {
        for o in &d.occs {
            if o.roles & 1 == 1 { def.insert(o.sym, di, &o.range, &o.body); }
        }
    }
    let mut paths = HashMap::new();
    for (di, d) in docs.iter().enumerate() {
        if let Ok(path) = corpus.join(&d.path).canonicalize() {
            paths.entry(path).and_modify(|site| *site = None).or_insert(Some(di));
        }
    }
    let symbols: BTreeMap<_, _> = syms.iter().enumerate().map(|(i, s)| (s.as_str(), i as u32)).collect();
    let mut modules = Vec::new();
    let mut libraries = HashSet::new();
    let mut internal_roots = HashSet::new();
    let mut file_modules = vec![Vec::new(); docs.len()];
    let mut alias_ranges = vec![HashSet::new(); docs.len()];
    let mut imports = Vec::new();
    let mut recovered = Vec::new();
    for (di, d) in docs.iter().enumerate() {
        let Ok(text) = std::fs::read_to_string(corpus.join(&d.path)) else { continue };
        let ls: Vec<_> = std::iter::once(0).chain(text.bytes().enumerate()
            .filter_map(|(i, b)| (b == b'\n').then_some(i + 1))).collect();
        let Some(root) = [Edition::Edition2024, Edition::Edition2021, Edition::Edition2018, Edition::Edition2015]
            .into_iter().map(|ed| SourceFile::parse(&text, ed)).find(|p| p.errors().is_empty())
            .map(|p| p.syntax_node()) else { continue };
        let crate_root = d.occs.iter().any(|o| o.roles & 1 == 1
            && syms[o.sym as usize].ends_with(" crate/"));
        if let Some((symbol, path)) = crate_root.then(|| library_file(&corpus.join(&d.path))).flatten() {
            if let Some(&sym) = symbols.get(symbol.as_str()) {
                if paths.contains_key(&path) { internal_roots.insert(symbol.clone()); }
                if let Some(&child) = paths.get(&path).and_then(Option::as_ref) { libraries.insert((di, child, sym)); }
            }
        }
        for o in &d.occs {
            let symbol = &syms[o.sym as usize];
            if o.roles & 1 == 1 && symbol.ends_with('/') && document_range(&text, &ls, &o.range, d.enc) {
                file_modules[di].push(o.sym);
            }
            let existing_body = def.sites.get(&o.sym).is_some_and(|sites|
                sites.iter().any(|s| s.document == di && s.body == o.body));
            let missing = (!existing_body).then(|| source_function(&root, &text, &ls, d.enc, symbol, &o.body)).flatten();
            if let Some(range) = missing.filter(|range| def.insert(o.sym, di, range, &o.body)) {
                recovered.push((di, o.sym, range[0] + 1));
            }
            if o.range.len() != 3 { continue; }
            let Some(start) = to_off(&text, &ls, o.range[0], o.range[1], d.enc) else { continue };
            let Some(end) = to_off(&text, &ls, o.range[0], o.range[2], d.enc) else { continue };
            let range = TextRange::new(TextSize::from(start as u32), TextSize::from(end as u32));
            if o.roles & 1 == 1 && literal_definition(&root, range, symbol) {
                let spans: HashSet<_> = d.occs.iter().filter(|other| other.roles & 1 == 1 && other.sym == o.sym)
                    .map(|other| &other.range).collect();
                if spans.len() == 1 { alias_ranges[di].insert(o.range.clone()); }
            }
            if !symbol.ends_with('/') { continue; }
            let Some(module) = root.children().filter_map(ast::Module::cast)
                .find(|m| m.name().is_some_and(|n| n.syntax().text_range() == range)) else { continue };
            if module.name().is_none_or(|n| n.text().trim_start_matches("r#") != name_of(symbol)) { continue; }
            internal_roots.insert(symbol.clone());
            let Some((child, explicit)) = module_target(&module, o.sym, &def, docs, corpus, &paths, crate_root, di) else { continue };
            modules.push((di, child, symbol.clone(), explicit));
        }
        imports.extend(import_sites(&root, d, syms, &text, &ls).into_iter()
            .map(|(sym, range, library)| (di, sym, range, library)));
    }
    def.bind_imports(imports, &libraries, &modules);
    for symbol in internal_roots { mark_internal(&mut def, symbol_prefix(&symbol), &symbols); }
    modules.extend(libraries.iter().map(|&(parent, child, sym)| (parent, child, syms[sym as usize].clone(), true)));
    recovered.extend(recover_modules(&mut def, docs, syms, &symbols, modules, &file_modules, &alias_ranges, &paths, corpus));
    (def, recovered)
}

fn document_range(text: &str, ls: &[usize], range: &[i32], enc: u64) -> bool {
    let end = match range {
        [0, 0, end] => to_off(text, ls, 0, *end, enc),
        [0, 0, line, column] => to_off(text, ls, *line, *column, enc),
        _ => None,
    };
    end == Some(text.len())
}

fn module_target(module: &ast::Module, sym: u32, def: &Definitions, docs: &[Doc], corpus: &Path,
                 paths: &HashMap<std::path::PathBuf, Option<usize>>, crate_root: bool, document: usize)
    -> Option<(usize, bool)>
{
    let candidates: HashSet<_> = def.sites.get(&sym).into_iter().flatten()
        .filter(|s| matches!(s.range.len(), 3 | 4) && s.range[0..2] == [0, 0] && s.body == s.range)
        .filter(|s| {
            let doc = &docs[s.document];
            let Ok(text) = std::fs::read_to_string(corpus.join(&doc.path)) else { return false };
            let ls: Vec<_> = std::iter::once(0).chain(text.bytes().enumerate()
                .filter_map(|(i, b)| (b == b'\n').then_some(i + 1))).collect();
            document_range(&text, &ls, &s.range, doc.enc)
        })
        .map(|s| corpus.join(&docs[s.document].path)).collect();
    let proven = (candidates.len() == 1).then(|| candidates.iter().next().unwrap().as_path());
    let explicit = module.attrs().any(|a| a.meta().is_some_and(|m| m.simple_name().as_deref() == Some("path")));
    let source = corpus.join(&docs[document].path);
    let directory_is_parent = crate_root || source.file_name().is_some_and(|n| n == "mod.rs");
    if !explicit && proven.is_none() && !directory_is_parent { return None; }
    let path = module_file(module, &source, directory_is_parent, proven)?;
    Some((paths.get(&path).copied().flatten()?, explicit))
}

fn mark_internal(def: &mut Definitions, prefix: &str, symbols: &BTreeMap<&str, u32>) {
    for (&name, &sym) in symbols.range(prefix..).take_while(|(name, _)| name.starts_with(prefix)) {
        if name.ends_with("().") { def.internal.insert(sym); }
    }
}

fn recover_modules(def: &mut Definitions, docs: &[Doc], syms: &[String], symbols: &BTreeMap<&str, u32>,
                   mut modules: Vec<(usize, usize, String, bool)>, file_modules: &[Vec<u32>],
                   alias_ranges: &[HashSet<Vec<i32>>], paths: &HashMap<std::path::PathBuf, Option<usize>>,
                   corpus: &Path) -> Vec<(usize, u32, i32)> {
    let mut recovered = Vec::new();
    let mut cursor = 0;
    while cursor < modules.len() {
        let (_, child, symbol, overridden) = &modules[cursor];
        let overridden = *overridden;
        let child = *child;
        let new_prefix = symbol_prefix(symbol).to_string();
        mark_internal(def, &new_prefix, symbols);
        let d = &docs[child];
        let mut nested = Vec::new();
        // The full-document module definition supplies the original moniker prefix.
        for &module in &file_modules[child] {
            let old_prefix = symbol_prefix(&syms[module as usize]);
            // #[path] resets default child lookup to the containing directory.
            // The derived namespace need not have its own SCIP occurrence.
            for (parent, descendant, declared, explicit) in modules.iter().filter(|(parent, _, _, _)| *parent == child) {
                let Some(suffix) = declared.strip_prefix(old_prefix) else { continue };
                let alias = format!("{new_prefix}{suffix}");
                if !symbols.range(alias.as_str()..).next().is_some_and(|(name, _)| name.starts_with(&alias)) { continue; }
                let target = if *explicit { Some(*descendant) } else {
                    let source = corpus.join(&d.path);
                    default_module_file(&source, &name_of(declared), overridden || source.file_name().is_some_and(|n| n == "mod.rs"))
                        .and_then(|p| paths.get(&p).copied().flatten())
                };
                if let Some(target) = target { nested.push((*parent, target, alias, *explicit)); }
            }
            for o in d.occs.iter().filter(|o| o.roles & 1 == 1 && alias_ranges[child].contains(&o.range)) {
                let Some(suffix) = syms[o.sym as usize].strip_prefix(old_prefix) else { continue };
                let alias = format!("{new_prefix}{suffix}");
                let Some(&sym) = symbols.get(alias.as_str()) else { continue };
                if def.insert(sym, child, &o.range, &o.body) { recovered.push((child, sym, o.range[0] + 1)); }
            }
        }
        for binding in nested { if !modules.contains(&binding) { modules.push(binding); } }
        cursor += 1;
    }
    recovered
}

fn symbol_prefix(symbol: &str) -> &str {
    if symbol.ends_with(" crate/") { symbol.trim_end_matches("crate/") } else { symbol }
}

fn literal_definition(root: &SyntaxNode, range: TextRange, symbol: &str) -> bool {
    let Some(token) = root.token_at_offset(range.start()).right_biased() else { return false };
    let Some(name) = token.parent_ancestors().find_map(ast::Name::cast) else { return false };
    if name.syntax().text_range() != range || name.text().trim_start_matches("r#") != name_of(symbol) { return false; }
    let Some(item) = name.syntax().parent() else { return false };
    let kind = item.kind();
    let literal = match symbol.chars().last() {
        Some('.') if symbol.ends_with(").") => kind == SyntaxKind::FN,
        Some('.') => matches!(kind, SyntaxKind::CONST | SyntaxKind::STATIC | SyntaxKind::RECORD_FIELD | SyntaxKind::VARIANT),
        Some('#') => matches!(kind, SyntaxKind::STRUCT | SyntaxKind::ENUM | SyntaxKind::UNION | SyntaxKind::TRAIT | SyntaxKind::TYPE_ALIAS),
        Some('/') => kind == SyntaxKind::MODULE,
        Some('!') => matches!(kind, SyntaxKind::MACRO_RULES | SyntaxKind::MACRO_DEF),
        Some(']') => kind == SyntaxKind::TYPE_PARAM,
        _ => false,
    };
    if !literal || root.children().any(|n| n.kind() == SyntaxKind::MACRO_CALL) { return false; }
    if item.ancestors().any(|n| !stable_attributes(&n)) { return false; }
    if let Some(implementation) = item.ancestors().find_map(ast::Impl::cast) {
        if implementation.trait_().is_some() { return false; }
        let Some(ast::Type::PathType(ty)) = implementation.self_ty() else { return false };
        let Some(path) = ty.path() else { return false };
        let Some(segment) = path.segment() else { return false };
        if path.qualifier().is_some() || segment.generic_arg_list().is_some() { return false; }
        let Some(self_name) = segment.name_ref() else { return false };
        let concrete = root.children().filter(|n| matches!(n.kind(), SyntaxKind::STRUCT | SyntaxKind::ENUM | SyntaxKind::UNION))
            .any(|n| stable_attributes(&n) && n.children().find_map(ast::Name::cast).is_some_and(|name| name.text() == self_name.text()));
        if !concrete { return false; }
    }
    true
}

fn stable_attributes(node: &SyntaxNode) -> bool {
    node.children().filter_map(ast::Attr::cast).all(|attr| {
        let Some(meta) = attr.meta() else { return false };
        match meta.simple_name().as_deref() {
            Some("allow" | "deny" | "warn" | "forbid" | "doc" | "inline" | "cold" | "track_caller" | "must_use" | "repr" | "non_exhaustive" | "macro_export") => true,
            Some("derive") => matches!(node.kind(), SyntaxKind::STRUCT | SyntaxKind::ENUM | SyntaxKind::UNION),
            _ => false,
        }
    })
}

pub fn name_of(sym: &str) -> String {
    let d = sym.trim_end_matches(|c| c == '.' || c == '#' || c == '/' || c == '!' || c == ')' || c == '(' || c == ']');
    let i = d.rfind(|c| c == ']' || c == '/' || c == '#' || c == ' ' || c == '.' || c == '[' || c == '(').map(|i| i + 1).unwrap_or(0);
    d[i..].trim_matches('`').trim_start_matches("r#").to_string()
}

fn import_sites(root: &SyntaxNode, d: &Doc, syms: &[String], text: &str, ls: &[usize])
    -> Vec<(u32, Vec<i32>, Option<u32>)>
{
    let mut imported_names: HashMap<u32, Vec<(String, Option<u32>)>> = HashMap::new();
    let mut imports = Vec::new();
    for o in &d.occs {
        if o.range.len() != 3 { continue; }
        let Some(start) = to_off(text, ls, o.range[0], o.range[1], d.enc) else { continue };
        let Some(end) = to_off(text, ls, o.range[0], o.range[2], d.enc) else { continue };
        let range = TextRange::new(TextSize::from(start as u32), TextSize::from(end as u32));
        let Some((name, prefix)) = explicit_import(root, range) else { continue };
        let library = if let Some(prefix) = prefix {
            let candidates: HashSet<_> = d.occs.iter().filter(|o| o.range.len() == 3
                && syms[o.sym as usize].ends_with(" crate/")
                && to_off(text, ls, o.range[0], o.range[1], d.enc) == Some(u32::from(prefix.start()) as usize)
                && to_off(text, ls, o.range[0], o.range[2], d.enc) == Some(u32::from(prefix.end()) as usize))
                .map(|o| o.sym).collect();
            if candidates.len() != 1 { continue; }
            candidates.into_iter().next()
        } else { None };
        imported_names.entry(o.sym).or_default().push((name, library));
        imports.push((o.sym, o.range.clone(), library));
    }
    for o in &d.occs {
        let Some(names) = imported_names.get(&o.sym) else { continue };
        let Some(start) = to_off(text, ls, o.range[0], o.range[1], d.enc) else { continue };
        let Some(name) = unqualified_name(root, TextSize::from(start as u32)) else { continue };
        for (_, library) in names.iter().filter(|(spelling, _)| *spelling == name) {
            imports.push((o.sym, o.range.clone(), *library));
        }
    }
    imports
}

fn unqualified_name(root: &SyntaxNode, start: TextSize) -> Option<String> {
    let token = root.token_at_offset(start).right_biased()?;
    let path = token.parent_ancestors().find_map(ast::Path::cast)?;
    if path.qualifier().is_some() { return None; }
    Some(path.segment()?.name_ref()?.text().to_string())
}

fn library_file(source: &Path) -> Option<(String, std::path::PathBuf)> {
    let directory = source.parent()?.ancestors().find(|p| p.join("Cargo.toml").is_file())?;
    let manifest: toml::Value = std::fs::read_to_string(directory.join("Cargo.toml")).ok()?.parse().ok()?;
    let package = manifest.get("package")?;
    let name = package.get("name")?.as_str()?;
    let version = package.get("version")?.as_str()?;
    let library = manifest.get("lib");
    if library.is_none() && package.get("autolib").and_then(toml::Value::as_bool) == Some(false) { return None; }
    let path = match library.and_then(|lib| lib.get("path")) {
        Some(value) => value.as_str()?,
        None => "src/lib.rs",
    };
    let file = directory.join(path).canonicalize().ok()?;
    Some((format!("rust-analyzer cargo {name} {version} crate/"), file))
}

fn module_file(module: &ast::Module, source: &Path, directory_is_parent: bool, proven: Option<&Path>) -> Option<std::path::PathBuf> {
    if module.semicolon_token().is_none() { return None; }
    let mut explicit = Vec::new();
    for attr in module.attrs() {
        let meta = attr.meta()?;
        // Conditional path attributes cannot be interpreted without cfg evaluation.
        match meta.simple_name().as_deref() {
            Some("cfg" | "allow" | "deny" | "warn" | "forbid" | "doc" | "expect") => continue,
            Some("path") => {},
            _ => return None,
        }
        let ast::Meta::KeyValueMeta(value) = meta else { return None };
        let ast::Expr::Literal(value) = value.expr()? else { return None };
        let ast::LiteralKind::String(value) = value.kind() else { return None };
        explicit.push(source.parent()?.join(value.value().ok()?.as_ref()));
    }
    if !explicit.is_empty() {
        return (explicit.len() == 1).then(|| explicit[0].canonicalize().ok()).flatten();
    }
    if let Some(path) = proven { return path.canonicalize().ok(); }
    let name = module.name()?.text().trim_start_matches("r#").to_string();
    default_module_file(source, &name, directory_is_parent)
}

fn default_module_file(source: &Path, name: &str, directory_is_parent: bool) -> Option<std::path::PathBuf> {
    let base = if directory_is_parent {
        source.parent()?.to_path_buf()
    } else { source.with_extension("") };
    let candidates: Vec<_> = [base.join(format!("{name}.rs")), base.join(&name).join("mod.rs")]
        .into_iter().filter_map(|p| p.canonicalize().ok()).collect();
    (candidates.len() == 1).then(|| candidates[0].clone())
}

fn explicit_import(root: &SyntaxNode, range: TextRange) -> Option<(String, Option<TextRange>)> {
    let token = root.token_at_offset(range.start()).right_biased()?;
    token.parent_ancestors().filter_map(ast::UseTree::cast).find_map(|tree| {
        let path = tree.path()?;
        let name = path.segment()?.name_ref()?;
        if name.syntax().text_range() != range || tree.star_token().is_some()
            || tree.use_tree_list().is_some() { return None; }
        let mut paths = vec![path.clone()];
        for parent in tree.syntax().ancestors().skip(1).filter_map(ast::UseTree::cast) {
            if let Some(path) = parent.path() { paths.push(path); }
        }
        paths.reverse();
        let joined = paths.iter().map(|p| p.syntax().text().to_string().replace(' ', "")).collect::<Vec<_>>().join("::");
        let top_level = tree.syntax().ancestors().find(|n| n.kind() == SyntaxKind::USE)
            .and_then(|n| n.parent()).is_some_and(|n| n.kind() == SyntaxKind::SOURCE_FILE);
        if joined.split("::").count() != 2 || !top_level { return None; }
        let prefix = if joined.starts_with("super::") { None } else {
            let path = &paths[0];
            let name = path.qualifier().and_then(|p| p.as_single_name_ref()).or_else(|| path.as_single_name_ref())?;
            Some(name.syntax().text_range())
        };
        let spelling = tree.rename().and_then(|r| r.name()).map(|n| n.text().to_string())
            .unwrap_or_else(|| name.text().to_string());
        Some((spelling, prefix))
    })
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
