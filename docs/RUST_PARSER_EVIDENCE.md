# Rust raw-parser evidence

Observed 2026-09-20. Interpretation and next actions belong to
[the current plan](RUST_CODEGRAPH_TRUTH_PLAN.md).

## Reproduction identity

- CBM source: `bbe753d30cd55babec4a8cce38692dadb97c1d40`.
- Corpus: `/Users/viktor/Projects/project-management`, clean at
  `e8a08ab4c97cf835a2735d7011794f7dd1c586de` on evidence readback.
- Homebrew installed `tree-sitter-cli 0.27.0`; `tree-sitter --version` returned
  `tree-sitter 0.27.0`. The separately installed `tree-sitter` formula is the library.
- Grammar: upstream `tree-sitter/tree-sitter-rust`, tag `v0.24.2`, commit
  `77a3747266f4d621d0757825e6b11edcbf991ca5`; matches the revision in
  `internal/cbm/vendored/grammars/MANIFEST.md:203`.
- `diff -u` on both parser.c and scanner.c showed only a missing final newline in
  CBM's vendored copies. This compares grammar sources; it does not claim the CLI's
  parsing runtime/build options equal CBM's embedded runtime/build options.
- Upstream checkout and parser cache: `/private/tmp/cbm-rust-parser.zpl7FD`.
- Raw output: `/private/tmp/cbm-rust-parser.zpl7FD/raw-trees.xml`, SHA-256
  `dd58ba1e39bf14e5a61d6c81f183f7abe762f98198634ed002780a0b77ea0d40`.
  The CLI appends a statistics line after the XML document; strip that trailer before
  feeding the whole file to a strict XML parser. Temporary artifacts may expire.

Executed from the grammar checkout:

```sh
XDG_CACHE_HOME=/private/tmp/cbm-rust-parser.zpl7FD/cache tree-sitter parse \
  --grammar-path /private/tmp/cbm-rust-parser.zpl7FD --xml --stat \
  /Users/viktor/Projects/project-management/crates/pm-port/src/writer_identity.rs \
  /Users/viktor/Projects/project-management/crates/pm-cli/src/repository_continuity.rs \
  /Users/viktor/Projects/project-management/crates/pm-cli-grammar/src/context.rs \
  > /private/tmp/cbm-rust-parser.zpl7FD/raw-trees.xml
```

Exit 0. Summary: `Total parses: 3; successful parses: 3; failed parses: 0;
success percentage: 100.00%; average speed: 20774 bytes/ms`.
The CLI warned about unconfigured parser directories; the explicit grammar path worked.
The speed is parser throughput for this invocation, not end-to-end index latency.

Source hashes recorded during the subsequent evidence readback:

| Corpus-relative file | SHA-256 |
|---|---|
| `crates/pm-port/src/writer_identity.rs` | `c4b91abd3cdbc178a5433c8d0b125c50477c165f56e17bd2995ded2079706bea` |
| `crates/pm-cli/src/repository_continuity.rs` | `158816731e6e6a414cca851aa8d3ea1f73d35673862339b3a2bf09b0fa79301b` |
| `crates/pm-cli-grammar/src/context.rs` | `1c0379114f8427526699067fbe40da681fe6a88552270825257d3dfe7b5cf916` |

## Observed syntax

Raw XML rows are zero-based. The output includes syntax node kinds, field names,
source text, and row/column ranges. Byte ranges were not separately captured here.

| Construct | Observation | Raw output anchor |
|---|---|---|
| Grouped import alias | `scoped_use_list` → `use_list` → `use_as_clause`, with distinct `path` and `alias` identifiers | XML line 3872; repository_continuity.rs source line 14 |
| `From<WriterIdentity> for String` | `impl_item` has `generic_type field="trait"`, `type_arguments` containing WriterIdentity, and separate `type_identifier field="type"` String | XML line 1684; writer_identity.rs source line 153 |
| `encoded.push(...)` | `call_expression` contains `field_expression field="function"`; receiver is `identifier field="value"` encoded, method is `field_identifier field="field"` push | XML line 2062; writer_identity.rs source line 183 |
| `char::from(...)` | Nested call has `scoped_identifier field="function"`, with `path` char and `name` from | XML line 2072; writer_identity.rs source line 183 |
| `Option<Rc<RefCell<dyn RepositoryContinuity>>>` | Nested generic_type/type_arguments preserve all wrappers, then dynamic_type with `type_identifier field="trait"` RepositoryContinuity | XML line 12939; context.rs source line 176 |

Selected verbatim node lines (noncontiguous; not a standalone XML document):

```xml
<use_as_clause srow="13" scol="4" erow="13" ecol="52">
  <identifier field="path" srow="13" scol="4" erow="13" ecol="24">RepositoryContinuity</identifier>
  <identifier field="alias" srow="13" scol="28" erow="13" ecol="52">RepositoryContinuityPort</identifier>
<generic_type field="trait" srow="152" scol="5" erow="152" ecol="25">
  <type_identifier field="type" srow="152" scol="5" erow="152" ecol="9">From</type_identifier>
  <type_arguments field="type_arguments" srow="152" scol="9" erow="152" ecol="25">
    <type_identifier srow="152" scol="10" erow="152" ecol="24">WriterIdentity</type_identifier>
<type_identifier field="type" srow="152" scol="30" erow="152" ecol="36">String</type_identifier>
<field_expression field="function" srow="182" scol="8" erow="182" ecol="20">
  <identifier field="value" srow="182" scol="8" erow="182" ecol="15">encoded</identifier>
  <field_identifier field="field" srow="182" scol="16" erow="182" ecol="20">push</field_identifier>
<dynamic_type srow="175" scol="45" erow="175" ecol="69">
  <type_identifier field="trait" srow="175" scol="49" erow="175" ecol="69">RepositoryContinuity</type_identifier>
```

## Supported conclusion and limits

These five constructs retain the relevant syntactic distinctions before CBM extraction.
The observed alias and trait-argument losses therefore have information available at
the parser boundary. This run does not establish symbol binding, inferred variable
types, macro expansion, complete Rust syntax coverage, or equivalence of all CBM/CLI
runtime behaviour. The proposed attribute/test-name raw-tree case was not separately
inspected in this raw-parser run. The later F7 extraction/storage/TESTS regressions and
compiler attribute oracle are retained in [RUST_REPAIR_EVIDENCE.md](RUST_REPAIR_EVIDENCE.md).
