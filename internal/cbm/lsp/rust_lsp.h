/* rust_lsp.h — Type-aware call resolution for Rust source files.
 *
 * Architecture mirrors go_lsp / py_lsp: build a `CBMTypeRegistry` from the
 * file's own definitions plus a small hand-rolled stdlib seed, then walk
 * each function body with scope-tracked variable bindings, evaluating the
 * type of each receiver expression and dispatching method calls through
 * inherent impls, trait impls, deref/autoref, generics, and `Self`.
 *
 * The implementation is a structural mirror of the algorithm in
 * `rust-analyzer` (`hir-def/resolver.rs` + `hir-ty/method_resolution.rs`)
 * reverse-engineered into pure C with no new runtime dependencies. The
 * goal is per-file (and cross-file) call attribution at >=90% parity with
 * what rust-analyzer would produce for the same source — without the cost
 * of a full IDE process.
 *
 * Public entry points:
 *   - `cbm_run_rust_lsp` — single-file resolution invoked from
 *     `cbm_extract_file()`.
 *   - `cbm_run_rust_lsp_cross` — cross-file resolution given a list of
 *     `CBMLSPDef`s gathered by the pipeline.
 *   - `cbm_batch_rust_lsp_cross` — batch wrapper that processes several
 *     files in one CGo call (per-file arenas + result copy).
 */
#ifndef CBM_LSP_RUST_LSP_H
#define CBM_LSP_RUST_LSP_H

#include "type_rep.h"
#include "scope.h"
#include "type_registry.h"
#include "lsp_neg_memo.h"
#include "../cbm.h"
#include "go_lsp.h" // for CBMLSPDef (pipeline def), used by the Tier-2 builder
#include "rust_modtree.h"

/* Forward declaration — defined in rust_cargo.h. We keep it forward
 * here to avoid pulling rust_cargo.h into every consumer that only
 * needs the LSP API. */
struct CBMCargoManifest;

/* Global confidence assigned to LSP-resolved call edges. The pipeline's
 * shared override resolver (`pipeline/lsp_resolve.h`) only admits entries
 * scoring >= CBM_LSP_CONFIDENCE_FLOOR (0.6). Numbers here mirror the Go
 * LSP so the call-edge mix from a polyglot project stays comparable. */
#define CBM_RUST_CONF_DIRECT 0.95f      /* path::to::func or alias hit  */
#define CBM_RUST_CONF_METHOD 0.95f      /* inherent method dispatch     */
#define CBM_RUST_CONF_TRAIT_SOLE 0.92f  /* trait method, single impl    */
#define CBM_RUST_CONF_TRAIT_AMB 0.85f   /* trait method, many impls     */
#define CBM_RUST_CONF_UFCS 0.93f        /* T::method() / Self::new()    */
#define CBM_RUST_CONF_PROMOTED 0.90f    /* through Deref / blanket impl */
#define CBM_RUST_CONF_MACRO_KNOWN 0.85f /* known std macro mapped to fn */
#define CBM_RUST_CONF_OPERATOR 0.88f    /* a+b → T::add (operator trait) */

/* Synthetic bytes [synthetic_start, synthetic_end) copy the original file's
 * bytes from original_start. */
typedef struct CBMRustSiteWindow {
    uint32_t synthetic_start;
    uint32_t synthetic_end;
    uint32_t original_start;
} CBMRustSiteWindow;

/* Rust-flavoured LSP context: one per file, lifetime tied to a single
 * `cbm_extract_file()` invocation (or the cross-file caller's arena). */
typedef struct {
    CBMArena *arena;
    const char *source;
    int source_len;
    const char *root_source; /* immutable original file buffer */
    int root_source_len;
    TSNode root_node; /* borrowed original tree for block declaration types */

    const CBMTypeRegistry *registry;
    CBMScope *current_scope;
    /* Alias assignments beneath a branch/loop/match/closure cannot establish
     * one unconditional post-region target, so they clear callable identity. */
    int callable_control_flow_depth;

    /* Negative-lookup memo for the registry-pure resolve cascades (trait
     * method / sole-trait-impl / free-func fallback). Active ONLY when the
     * registry is sealed (read_only) — see lsp_neg_memo.h. Arena-backed,
     * dies with the file. Zeroed by rust_lsp_init's memset (lazy alloc). */
    CBMNegMemo neg_memo;

    /* `use` map: parallel arrays mapping a local-name (the last segment, or
     * the `as` alias) to its full module path (`std::collections::HashMap`,
     * `crate::foo::Bar`). Glob imports go in `glob_module_qns` instead. */
    const char **use_local_names;
    const char **use_module_paths;
    const char **use_raw_paths; /* the path as written, for module-tree resolution */
    int use_count;
    int file_use_count; /* entries below this index are file-level, above are block `use`s */

    const char **glob_module_qns;
    const char **glob_raw_paths;
    int glob_count;
    /* Globs declared at file level; any glob beyond this index belongs to an
     * enclosing block and shadows the module's own items. */
    int file_glob_count;

    /* Exact names from top-level out-of-line `mod name;` declarations. A
     * relative scoped type may be project-qualified only when its head is in
     * this set; coincidentally same-named indexed modules stay external. */
    CBMIdxMemo declared_modules;

    /* Raw `use` paths of every Rust file in the project, keyed by that file's
     * module QN and the imported local name (pipeline-owned). Lets
     * `crate_root::Item` follow a `pub use` re-export and lets `super::*`
     * see the parent module's imports. NULL outside the pipeline. */
    const struct CBMRustImportSource *imports;
    int export_depth;
    /* Also follow a module file's `use path::*` when it binds no `name`
     * explicitly. The import table keeps one glob per file (the last), so a
     * miss stays a miss; set only by impl-header resolution. */
    bool follow_glob_exports;
    CBMIdxMemo export_memo; /* "<module>\x1f<name>" -> 0 (miss) or 1-based export_values slot */
    const char **export_values;
    int export_value_count;

    /* Module-qualified name for this file (e.g. "<project>.<crate>.foo"). */
    const char *module_qn;
    /* Graph prefix of the inline module whose body is being walked. */
    const char *item_module_qn;
    /* Rust module identity used only for `self::` / `super::` imports. It
     * differs from module_qn when #[path] gives a file a non-physical name;
     * NULL means that identity was ambiguous and relative imports stay open. */
    const char *relative_module_qn;
    /* Functions the module being walked (the file root or an inline module)
     * declares whose name another function of the file shares, mapped to
     * their exact graph node: a bare call there names this node. Not
     * inherited by nested modules; see rust_bind_module_fns. */
    const char **module_fn_names;
    const char **module_fn_qns;
    int module_fn_count;
    /* Type-namespace items (struct, enum, union, type alias, trait) the
     * module being walked declares: a bare name among them is that item,
     * whatever an enclosing module imports or the prelude calls by that
     * name. Not inherited by nested modules. */
    const char **module_type_names;
    int module_type_count;
    /* The registry holds this file's items only (the per-file pass), so what
     * a glob import of another module brings into scope is unseen. */
    bool registry_is_file_local;
    uint8_t prelude_restrictions; /* source module's prelude attributes */
    TSNode module_scope; /* original source module, for declarations without Cargo mapping */
    /* Value-namespace items declared in the blocks enclosing the walk point,
     * outermost first: the name and, for a function with its own graph node,
     * that node's QN (NULL otherwise). Entries from block_item_base on are
     * visible inside a nested item's body; a nested module starts a new base. */
    const char **block_item_names;
    const char **block_item_qns;
    int block_item_count;
    int block_item_cap;
    int block_item_base;
    /* Lazily derived from module_qn: the crate's `<project>.<dirs>.<src>`
     * prefix and the module directory this file owns. */
    const char *crate_root_prefix;
    const char *module_dir_qn;

    /* Enclosing function context. `enclosing_func_qn` is the QN we attach
     * to every emitted CBMResolvedCall as `caller_qn`. */
    const char *enclosing_func_qn;
    const char *enclosing_impl_key;

    /* `Self` resolution: when inside `impl T { ... }` or
     * `impl Trait for T { ... }`, `self_type_qn` is `T`'s QN. NULL outside
     * an impl. `self_trait_qn` is the trait QN (only set for trait impls)
     * — used when emitting trait method calls so we can prefer concrete
     * implementations over the trait method when only one impl exists. */
    const char *self_type_qn;
    const CBMType *self_value_type; /* actual Self shape for a reference impl */
    /* The impl's self-type arguments by position (`impl<C> Tr for Map<C>`
     * gives {"C"}); "" where the argument is not a bare name. NULL outside
     * a non-blanket impl or when the self type has no arguments. */
    const char **self_param_names;
    const char *self_trait_qn;
    const char *self_trait_spelling; /* raw `impl <Trait> for` text, NULL outside trait impls */

    /* Scrutinee type inherited by the match arms currently being walked.
     * Even when an enum payload's exact field type is unavailable, binding
     * its pattern as unknown prevents an outer same-named local from leaking
     * into the arm. */
    const CBMType *pending_match_type;

    /* QN of the registered type that declared the field the latest
     * successful rust_lookup_field found (after alias hops). A caller that
     * clears it before a typing call reads which type owns the field. */
    const char *field_owner_qn;

    /* User-defined `macro_rules!` definitions collected during the
     * pre-walk phase. Each rule stores its pattern + transcriber text;
     * `macro_invocation` resolution attempts to match the invocation
     * against each rule and then substitutes/re-walks the expansion.
     *
     * The arrays are doubling-grown out of `arena`. */
    struct RustMacroRule **macro_rules_arr;
    int macro_rules_count;

    /* Original-file occurrence that established the lexical environment for
     * the current macro expansion. Nested macro invocations are parsed from a
     * synthetic transcriber buffer, so their tree-sitter offsets cannot be
     * used to decide which source-level macro_rules! definition is visible. */
    uint32_t macro_origin_byte;
    bool macro_origin_valid;

    /* Recursion guard for macro expansion. Real macro_rules! can be
     * recursive; we cap at 8 nested expansions to keep the walker
     * bounded. */
    int macro_expand_depth;

    /* Pathological-input guard: counts the number of call-resolution
     * + type-evaluation steps spent on the current file. When the
     * counter exceeds the cap we stop attributing further calls so a
     * malicious or hand-crafted file cannot wedge the resolver.
     * Mirrors the cap added to `c_lsp.c` since the worktree branched
     * (RUST_LSP_FOLLOWUP §B.2). */
    int eval_step_count;

    /* Cargo.toml manifest, when the caller has parsed one and routed
     * it through. The resolver consults `dep_count`/`member_count` so
     * paths beginning with a workspace member or declared dependency
     * route to a sensible canonical form (`<crate>.<tail>`) instead of
     * falling through to module-prefix fallback. NULL when no manifest
     * is available — the resolver still works, just without workspace
     * awareness. Owned by the caller; the RustLSPContext only borrows. */
    const struct CBMCargoManifest *cargo_manifest;

    /* Written bounds of active impl/function parameters. A NULL trait QN
     * starts a parameter scope; "?" records a bound whose trait identity is
     * unproven. Counts are restored when leaving the item. */
    struct {
        const char *param_name; /* "T", "U", "Item" */
        const char *trait_qn;   /* Trait QN, NULL scope marker, or "?". */
    } *type_param_bounds;
    int type_param_bound_count;

    struct {
        const char *alias_name; /* "U" */
        const char *aliased_to; /* "T.Item" (representational) */
    } *type_param_aliases;
    int type_param_alias_count;

    /* Output: resolved (and unresolved-with-reason) calls accumulate here. */
    CBMResolvedCallArray *resolved_calls;

    /* Syntactic-call list (result->calls), borrowed from the per-file
     * extraction result. The downstream pipeline only turns a resolved_call
     * into a CALLS edge when a *syntactic* CBMCall with the same
     * (enclosing_func_qn, callee short-name) exists here. Some calls the Rust
     * resolver recovers — operator-trait desugaring (`a + b`) and method
     * calls hidden inside macro token-trees (`format!("{}", d.label())`) —
     * never appear in result->calls because the syntactic extractor cannot
     * see them. When this is non-NULL the resolver injects matching synthetic
     * CBMCall entries so those recovered calls become real edges. Cross-file
     * callers provide a separate output array and merge it into the owning
     * file result with explicit arena-aware copying. */
    CBMCallArray *syn_calls;
    CBMUsageArray *macro_usages; /* raw tokens admitted only by mapped expression syntax */

    /* While >0, rust_emit_resolved_call also injects a matching synthetic
     * CBMCall into `syn_calls` so the recovered call becomes an edge. Set
     * around the macro-argument re-parse where the syntactic extractor never
     * produced a call node. */
    int inject_syn_calls;

    /* Current invocation occurrence in ORIGINAL-file coordinates. Ordinary
     * source calls use their tree-sitter node directly. While the walk reads
     * synthetic source (`site_map_active`), only ranges inside one of the
     * `site_windows` map back: a reparsed built-in macro argument list is one
     * window, a macro_rules! expansion has one per substituted fragment, and
     * its transcriber text has none. */
    uint32_t emit_site_start_byte;
    uint32_t emit_site_end_byte;
    uint32_t emit_callee_start_byte;
    uint32_t emit_callee_end_byte;
    bool site_map_active;
    const struct CBMRustSiteWindow *site_windows;
    int site_window_count;

    /* CBM_LSP_DEBUG=1 in env enables verbose stderr trace. */
    bool debug;

    /* Per-ROOT-invocation macro-expansion memo. Kernel macro_rules are often
     * self-recursive (define_sizes! re-invokes itself with @internal/@impls
     * args) and the expansion fallback ("no pattern matched — still expand the
     * first rule") makes that recursion non-convergent: with an unbounded
     * BREADTH under the depth-8 guard, 2-44 source invocations exploded into
     * ~200k expansions (each a full tree-sitter parse) ≈ 63 s per file.
     * Within one expansion chain an identical (macro, substituted body) is
     * walked once — identical text implies an identical walk, and recursive
     * re-invocations have no distinct source site to attribute. Reset at each
     * top-level invocation so distinct source sites keep their attribution. */
    CBMNegMemo macro_memo;
} RustLSPContext;

/* Initialise an empty context for processing one file. */
void rust_lsp_init(RustLSPContext *ctx, CBMArena *arena, const char *source, int source_len,
                   const CBMTypeRegistry *registry, const char *module_qn,
                   CBMResolvedCallArray *out);

/* Register a `use` alias. `local_name` may be the last segment of the path
 * (`HashMap` for `use std::collections::HashMap`) or an `as` alias. The
 * full `module_path` is stored verbatim (e.g. `std::collections::HashMap`).
 * Glob imports (`use foo::*`) go through `rust_lsp_add_glob` instead. */
void rust_lsp_add_use(RustLSPContext *ctx, const char *local_name, const char *module_path);
void rust_lsp_add_glob(RustLSPContext *ctx, const char *module_qn);

/* Process every function/method in the file, walking statements and
 * evaluating expression types as we go. */
void rust_lsp_process_file(RustLSPContext *ctx, TSNode root);

/* Evaluate the static type of a Rust expression node. Returns
 * `cbm_type_unknown()` for anything we cannot type. */
const CBMType *rust_eval_expr_type(RustLSPContext *ctx, TSNode node);

/* Bidirectional variant: evaluate an expression with an `expected`
 * hint available. The hint is used to:
 *   - disambiguate `Vec::new()` / `HashMap::default()` when the LHS
 *     of a let or function arg constrains the result type;
 *   - infer turbofish-free method generics (`s.parse()` typed as
 *     `Result<i32, _>`);
 *   - thread context into nested expressions (struct field init,
 *     match arm, if-else branches).
 *
 * `expected` may be NULL (no hint). If the synthesised type matches
 * the expected, returns it; if a hint resolves an ambiguity that the
 * synthesis path cannot, returns the hint-substituted form. */
const CBMType *rust_eval_expr_typed(RustLSPContext *ctx, TSNode node, const CBMType *expected);

/* Convert a `*type*` AST node (type_identifier, scoped_type_identifier,
 * reference_type, primitive_type, …) into a CBMType. */
const CBMType *rust_parse_type_node(RustLSPContext *ctx, TSNode node);

/* Bind the bindings introduced by a let/for/match-pattern statement into
 * the current scope. */
void rust_process_statement(RustLSPContext *ctx, TSNode node);

/* Look up an inherent method or field promoted through Deref / embedded
 * trait blanket impls. Returns NULL if nothing matches. */
const CBMRegisteredFunc *rust_lookup_method(RustLSPContext *ctx, const char *type_qn,
                                            const char *member_name);

/* Populate the same per-file registry used by cbm_run_rust_lsp, including
 * AST-declared return-type refinements. Exposed as a production-used seam for
 * structural registry regression tests. */
void cbm_rust_build_local_registry(CBMArena *arena, CBMTypeRegistry *reg, CBMFileResult *result,
                                   const char *module_qn, TSNode root, const char *source);

/* Entry point — called from `cbm_extract_file()` after the unified
 * extractor has filled `result->defs`, `result->imports`, and
 * `result->impl_traits`. Builds a per-file registry, parses the `use`
 * graph, and walks every function body emitting CBMResolvedCall entries. */
void cbm_run_rust_lsp(CBMArena *arena, CBMFileResult *result, const char *source, int source_len,
                      TSNode root);

/* Same as `cbm_run_rust_lsp`, but accepts an optional Cargo manifest
 * so the resolver can route paths whose head is a workspace member /
 * declared dependency. Pass NULL to fall back to the manifest-free
 * behaviour. */
void cbm_run_rust_lsp_with_manifest(CBMArena *arena, CBMFileResult *result, const char *source,
                                    int source_len, TSNode root,
                                    const struct CBMCargoManifest *manifest);

/* Register a curated subset of the Rust core/alloc/std prelude into the
 * given registry. The seed is intentionally compact (~150 types, ~600
 * methods) and covers Option/Result/Vec/String/HashMap/BTreeMap/Iterator
 * plus the prelude trait method names (`clone`, `to_string`, `into`, …).
 * Generated from `scripts/gen-rust-stdlib.py` if present, otherwise the
 * hand-written `rust_stdlib_data.c` module is used. */
void cbm_rust_stdlib_register(CBMTypeRegistry *reg, CBMArena *arena);

/* Register seeds for commonly-used external crates (serde, tokio,
 * anyhow, clap, regex, log, futures, parking_lot, once_cell, chrono,
 * uuid, reqwest, rayon). Best-effort curated entries — RUST_LSP_FOLLOWUP
 * §A3. Calls into crates not in this seed remain `unresolved`. */
void cbm_rust_crates_register(CBMTypeRegistry *reg, CBMArena *arena);

/* --- Cross-file LSP resolution --- */

/* Cross-file definition record. The Rust LSP reuses the same shape as
 * `CBMLSPDef` in `go_lsp.h` for compatibility with the pipeline plumbing
 * — see that header for field semantics. We declare a separate typedef
 * to allow Rust-specific fields without touching the Go ABI. */
typedef struct {
    const char *qualified_name;
    const char *short_name;
    const char *label;            /* "Function", "Method", "Type", "Trait" */
    const char *receiver_type;    /* for methods: receiver type QN (NULL for free fns) */
    const char *def_module_qn;    /* module QN where this def lives */
    const char *return_types;     /* "|"-separated return type texts          */
    const char *embedded_types;   /* "|"-separated embedded type QNs          */
    const char *field_defs;       /* "|"-separated "name:type" pairs          */
    const char *method_names_str; /* "|"-separated method names for traits   */
    const char **signature_param_types; /* borrowed ordered parameter texts    */
    int signature_param_count;          /* positional entries; "?" is unknown */
    const char *trait_qn;               /* raw impl-trait spelling; uniquely canonicalized */
    const char *receiver_spelling;      /* exact impl receiver spelling; registry-only */
    const char *impl_target;            /* impl relation: raw `type Target = ..;` text */
    const char *impl_item; /* written Item; empty when impl applicability is unproved */
    const char *impl_output; /* written Output; empty when impl applicability is unproved */
    const char **decorators;            /* borrowed attribute texts (`#[derive(..)]`), NULL-ended */
    bool is_interface;           /* true for traits                          */
    bool is_rust_impl_relation;   /* independent type-level impl record       */
    bool is_abstract;             /* required trait declaration (no default)  */
    bool is_async;                /* `async fn`: return is the awaited output */
    bool is_rust_private;         /* inherent method without a visibility modifier */
    bool has_rust_self;           /* first parameter is a `self` receiver */
    bool has_rust_value_self;     /* unborrowed `self` or `mut self` */
    bool has_rust_shared_self;    /* explicit shared `&self` receiver */
    bool rust_impl_self_needs_proof; /* type/const impl parameters or a where clause */
    const char **rust_impl_type_params; /* source-declared impl parameters, including bounded ones */
    const char **rust_type_param_defaults; /* positional source defaults; empty means absent */
    const char **rust_unbounded_impl_params; /* declared impl parameters with no bounds */
    const char **type_params;     /* the fn's own type parameter names, NULL-ended */
} CBMRustLSPDef;

const char *cbm_rust_impl_key(CBMArena *arena, const char *receiver_qn,
                              const char *receiver_spelling, const char *trait_spelling);
const CBMRegisteredFunc *cbm_rust_registry_lookup_impl_method(
    const CBMTypeRegistry *reg, const char *receiver_qn, const char *impl_key,
    const char *method_name);

/* Run cross-file resolution on a single file. */
void cbm_run_rust_lsp_cross(CBMArena *arena, const char *source, int source_len,
                            const char *module_qn, CBMRustLSPDef *defs, int def_count,
                            const char **import_names, const char **import_qns, int import_count,
                            TSTree *cached_tree, CBMResolvedCallArray *out);

/* Same as `cbm_run_rust_lsp_cross`, plus an optional parsed Cargo manifest
 * (NULL = manifest-free behaviour). The manifest lets call paths whose head
 * is a workspace member / declared dependency route across the crate
 * boundary (`crate_a::foo` → the def inside crate_a). Wired from the
 * cross-file LSP pass (pass_lsp_cross.c) which builds the manifest once from
 * the project root Cargo.toml. `relative_module_qn` is the declared module
 * identity for `self::` / `super::`; NULL suppresses those imports. */
/* Pipeline-owned lookup of another file's raw `use` path: (file module QN,
 * local name) -> path text, or NULL. */
typedef struct CBMRustImportSource {
    const char *(*lookup)(void *ctx, const char *file_module_qn, const char *local_name);
    void *ctx;
    /* The project's crate module tree; NULL when none was built. Path calls
     * it settles take precedence over the file-layout fallbacks. */
    const struct CBMRustModTree *modtree;
    /* Project-wide file-level `macro_rules!` definitions; NULL when none were
     * collected. Lets a file expand a macro its ancestor module declared
     * before the `mod` item that loads it (textual scope across files). */
    const struct CBMRustMacroTable *macros;
} CBMRustImportSource;

/* Selected local macro_rules! transcriber after matching and substitution,
 * or NULL for missing/unsupported rules. Arena-owned; does not run proc macros. */
char *cbm_rust_expand_local_item_macro(CBMArena *arena, const char *source, TSNode root,
                                       TSNode invocation);

/* One file's macro facts: its file-level `macro_rules!` definitions and its
 * out-of-line `mod name;` declarations, in source order. NULL when the file
 * defines no file-level macro (the common case, decided without parsing).
 * A pure function of the file's bytes; safe to call from several threads. */
typedef struct CBMRustMacroFacts CBMRustMacroFacts;
CBMRustMacroFacts *cbm_rust_macro_facts_scan(const char *source, int len);
void cbm_rust_macro_facts_free(CBMRustMacroFacts *facts);

/* Digest of the macro facts another file can observe (definition texts and
 * their order against `mod` declarations), independent of byte offsets; 0
 * when the file defines no file-level macro. `root` is the file's parse. */
uint64_t cbm_rust_macro_facts_digest(const char *source, int len, TSNode root);

/* The project's macro facts keyed by file graph module QN. Immutable once
 * filled; lookups are read-only and safe from several threads. */
typedef struct CBMRustMacroTable CBMRustMacroTable;
CBMRustMacroTable *cbm_rust_macro_table_new(void);
/* Copies `module_qn`; takes ownership of `facts` (NULL is ignored). */
void cbm_rust_macro_table_add(CBMRustMacroTable *table, const char *module_qn,
                              CBMRustMacroFacts *facts);
void cbm_rust_macro_table_free(CBMRustMacroTable *table);

/* What a file-level invocation of a corpus macro_rules! declares on the impl
 * self type `receiver` (spelling as the expansion reads): the `target` of an
 * `impl Deref` naming core::ops::Deref, or (target NULL) the name `method` of
 * a method another emitted impl gives the type. */
typedef struct {
    const char *receiver;
    const char *target;
    const char *method;
} CBMRustMacroDeref;
typedef struct {
    CBMRustMacroDeref *items;
    int count;
    int cap;
} CBMRustMacroDerefArray;
/* NULL-terminated names of the file-level macro_rules! in `table` whose
 * transcriber writes a Deref impl, each defined exactly once in the corpus
 * (an invocation of a name defined twice could reach either). Arena-owned;
 * NULL when there are none. */
const char **cbm_rust_deref_macro_names(CBMArena *arena, const CBMRustMacroTable *table);
/* When `source` invokes one of `names`: append to `out` what its file-level,
 * non-cfg-gated invocations of corpus macro_rules! emit, each expanded through
 * the first matching arm of the one corpus definition of its name. An
 * invocation whose name a file-level `use` may bind outside the corpus is
 * skipped. Arena-owned strings. */
void cbm_rust_macro_deref_impls(CBMArena *arena, const CBMRustMacroTable *table,
                                const char **names, const char *source, int len,
                                CBMRustMacroDerefArray *out);

void cbm_run_rust_lsp_cross_with_manifest(CBMArena *arena, const char *source, int source_len,
                                          const char *module_qn, const char *relative_module_qn,
                                          CBMRustLSPDef *defs, int def_count,
                                          const char **import_names, const char **import_qns,
                                          int import_count, TSTree *cached_tree,
                                          const struct CBMCargoManifest *manifest,
                                          const CBMRustImportSource *imports,
                                          CBMResolvedCallArray *out, CBMCallArray *synthetic_calls, CBMUsageArray *macro_usages,
                                          CBMRustImplLinkArray *impl_links);

/* Both cross entry points take an optional `impl_links` output (arena-owned,
 * NULL to skip): one record per `impl` block whose header names resolved to
 * registered definitions (see CBMRustImplLink). */

/* Tier-2: build the project-wide Rust cross registry ONCE from all defs, finalize,
 * and seal read-only. Shared across every Rust file's resolve (mirrors C/py/cs/ts).
 * Def-driven → byte-identical entries to the per-file build. */
CBMTypeRegistry *cbm_rust_build_cross_registry(CBMArena *arena, CBMLSPDef *defs, int def_count);

/* Same, with the crate module tree: an impl block whose self type is declared
 * in another file registers its methods on that declared type. NULL keeps
 * every method on its extracted receiver. */
CBMTypeRegistry *cbm_rust_build_cross_registry_with_modtree(CBMArena *arena, CBMLSPDef *defs,
                                                            int def_count,
                                                            const CBMRustModTree *modtree);

/* Cross-file Rust resolve using a pre-built shared registry (Tier-2). Skips the
 * per-file registry build; just parse + resolve. `manifest` = the same Cargo manifest
 * the per-file path uses (cross-crate #56). `synthetic_calls` is an optional,
 * arena-owned output for exact carriers recovered from non-call syntax. Attribute
 * proc-macros are handled by the graph's DECORATES + USAGE semantic passes, not
 * by this resolver. */
void cbm_run_rust_lsp_cross_with_registry(CBMArena *arena, const char *source, int source_len,
                                          const char *module_qn, const char *relative_module_qn,
                                          const CBMTypeRegistry *reg, const char **import_names,
                                          const char **import_qns, int import_count,
                                          TSTree *cached_tree,
                                          const struct CBMCargoManifest *manifest,
                                          const CBMRustImportSource *imports,
                                          CBMResolvedCallArray *out, CBMCallArray *synthetic_calls, CBMUsageArray *macro_usages,
                                          CBMRustImplLinkArray *impl_links);

/* Per-file input for batch cross-file Rust LSP processing. */
typedef struct {
    const char *source;
    int source_len;
    const char *module_qn;
    TSTree *cached_tree;
    CBMRustLSPDef *defs;
    int def_count;
    const char **import_names;
    const char **import_qns;
    int import_count;
} CBMBatchRustLSPFile;

/* Process several files in one CGo call (per-file arenas, result copy). */
void cbm_batch_rust_lsp_cross(CBMArena *arena, CBMBatchRustLSPFile *files, int file_count,
                              CBMResolvedCallArray *out);

#endif /* CBM_LSP_RUST_LSP_H */
