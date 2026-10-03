/* rust_lsp.c — Type-aware call resolution for Rust source files.
 *
 * Mirrors the structure of `go_lsp.c` and reverse-engineers the relevant
 * pieces of `rust-analyzer` (`hir-def/resolver.rs`,
 * `hir-ty/method_resolution.rs`, `hir-ty/infer.rs`) into a per-file walk
 * driven by tree-sitter-rust.
 *
 * The compilation unit is split into clearly-labelled sections:
 *
 *   1.  Init + helpers            (~150 lines)
 *   2.  Builtin / prelude tables  (~100 lines)
 *   3.  Path & use resolution     (~250 lines)
 *   4.  Type-AST → CBMType        (~250 lines)
 *   5.  Generic substitution      (~150 lines)
 *   6.  Expression evaluator      (~700 lines)
 *   7.  Method dispatch           (~400 lines)
 *   8.  Macro handling            (~200 lines)
 *   9.  Statement / pattern bind  (~400 lines)
 *   10. Function & file walk      (~250 lines)
 *   11. Per-file entry            (~250 lines)
 *   12. Cross-file + batch        (~250 lines)
 *
 * Total ~3300 lines, matching the depth of go_lsp.c (2750) and py_lsp.c
 * (3188). The code is structured so each section has a single coherent
 * responsibility — there are no surprise back-edges between sections.
 */

#include "rust_lsp.h"
#include "rust_cargo.h"
#include "../helpers.h"
#include "../../../src/foundation/mem_core.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ════════════════════════════════════════════════════════════════════
 * 1. Initialisation + arena helpers
 * ════════════════════════════════════════════════════════════════════ */

/* Forward declarations for early callers in the file. */
static void rust_resolve_calls_in_node(RustLSPContext *ctx, TSNode node);
static void rust_process_nested_item(RustLSPContext *ctx, TSNode item);
static void rust_collect_uses(RustLSPContext *ctx, TSNode root);
static void rust_process_module(RustLSPContext *ctx, TSNode mod_node, int depth);
static const char *rust_module_fn_target(const RustLSPContext *ctx, const char *name);
static const char *rust_resolve_path_expr(RustLSPContext *ctx, const char *path);
static const char *rust_module_export(RustLSPContext *ctx, const char *module_qn, const char *name);
static const char *rust_qn_crate_prefix(RustLSPContext *ctx, const char *qn);
typedef struct RustQnMatch RustQnMatch;
static void rust_qn_variants(RustLSPContext *ctx, const char *qn, bool want_type, RustQnMatch *m);
static void rust_emit_resolved_call(RustLSPContext *ctx, const char *callee_qn,
                                    const char *callee_impl_key, const char *strategy,
                                    float confidence);
static void rust_inject_syn_call(RustLSPContext *ctx, const char *callee_qn);
static bool rust_map_source_range(const RustLSPContext *ctx, uint32_t start, uint32_t end,
                                  uint32_t *mapped_start, uint32_t *mapped_end);
static void rust_emit_unresolved_call(RustLSPContext *ctx, const char *expr_text,
                                      const char *reason);
static const CBMType *rust_lookup_field(RustLSPContext *ctx, const char *type_qn,
                                        const char *field_name, int depth);
static const CBMRegisteredFunc *rust_lookup_method_in_trait(RustLSPContext *ctx,
                                                            const char *trait_qn,
                                                            const char *method_name,
                                                            bool allow_abstract);
static char *rust_node_text(RustLSPContext *ctx, TSNode node);
static const char *convert_path_to_qn(CBMArena *arena, const char *path);
static bool rust_type_derefs_to_first_arg(const char *type_qn);
static const CBMType *rust_deref_step(RustLSPContext *ctx, const CBMType *t);
static bool rust_wrapper_head_derefs(RustLSPContext *ctx, const char *head);
static const char *rust_wrapper_head_qn(RustLSPContext *ctx, const char *head);
static bool rust_wrapper_has_corpus_trait_method(RustLSPContext *ctx, const CBMType *base,
                                                 const char *method);
static bool rust_trait_out_of_scope(RustLSPContext *ctx, const char *trait_qn);
static bool rust_bound_is_marker(const char *bound, size_t len);
static const CBMRegisteredFunc *rust_registry_lookup_trait_impl_method(const CBMTypeRegistry *reg,
                                                                       const char *receiver_qn,
                                                                       const char *trait_qn,
                                                                       const char *method_name,
                                                                       bool *out_ambiguous);
static const char *rust_lookup_type_param_bound(RustLSPContext *ctx, const char *name);
static void rust_collect_bounds_from_text(RustLSPContext *ctx, const char *text);
static void rust_collect_uses(RustLSPContext *ctx, TSNode root);
static void rust_record_type_param_bound(RustLSPContext *ctx, const char *param_name,
                                         const char *trait_qn);
static const CBMType *rust_trait_assoc_fn_type(RustLSPContext *ctx, const char *head,
                                              const char *fn_name);

/* The target one path call resolves to. The call step publishes it and the
 * expression evaluator types the call's value from it, so the two cannot
 * disagree. `qn` NULL with `open_qn` set records an unresolved call; both
 * NULL leaves the call deliberately open with no record. */
typedef struct {
    const char *qn;
    const char *impl_key;
    const char *strategy;
    float confidence;
    const char *reason;
    const CBMRegisteredFunc *func; /* registered target; NULL when synthesised */
    const char *receiver_qn;       /* type that `Self` in the target's signature names */
    const char *open_qn;
    const char *open_reason;
    bool names_function; /* false: the path names no in-project function */
} RustCallTarget;
static bool rust_module_tree_target(RustLSPContext *ctx, TSNode callee, const char *path,
                                    RustCallTarget *out);

void rust_lsp_init(RustLSPContext *ctx, CBMArena *arena, const char *source, int source_len,
                   const CBMTypeRegistry *registry, const char *module_qn,
                   CBMResolvedCallArray *out) {
    memset(ctx, 0, sizeof(RustLSPContext));
    ctx->arena = arena;
    ctx->source = source;
    ctx->source_len = source_len;
    ctx->root_source = source;
    ctx->root_source_len = source_len;
    ctx->registry = registry;
    ctx->module_qn = module_qn;
    ctx->relative_module_qn = module_qn;
    ctx->resolved_calls = out;
    ctx->current_scope = cbm_scope_push(arena, NULL);

    const char *dbg = getenv("CBM_LSP_DEBUG");
    ctx->debug = (dbg && dbg[0]);
}

/* Doubling-array push of a `(local, full-path)` use entry. */
void rust_lsp_add_use(RustLSPContext *ctx, const char *local_name, const char *module_path) {
    if (!ctx || !local_name || !module_path) {
        return;
    }
    if (ctx->use_count % 32 == 0) {
        int new_cap = ctx->use_count + 32;
        const char **nl =
            (const char **)cbm_arena_alloc(ctx->arena, (new_cap + 1) * sizeof(char *));
        const char **np =
            (const char **)cbm_arena_alloc(ctx->arena, (new_cap + 1) * sizeof(char *));
        const char **nr =
            (const char **)cbm_arena_alloc(ctx->arena, (new_cap + 1) * sizeof(char *));
        if (!nl || !np || !nr) {
            return;
        }
        if (ctx->use_local_names && ctx->use_count > 0) {
            memcpy(nl, ctx->use_local_names, ctx->use_count * sizeof(char *));
            memcpy(np, ctx->use_module_paths, ctx->use_count * sizeof(char *));
            memcpy(nr, ctx->use_raw_paths, ctx->use_count * sizeof(char *));
        }
        ctx->use_local_names = nl;
        ctx->use_module_paths = np;
        ctx->use_raw_paths = nr;
    }
    ctx->use_local_names[ctx->use_count] = cbm_arena_strdup(ctx->arena, local_name);
    ctx->use_module_paths[ctx->use_count] = cbm_arena_strdup(ctx->arena, module_path);
    ctx->use_raw_paths[ctx->use_count] = ctx->use_module_paths[ctx->use_count];
    ctx->use_count++;
}

void rust_lsp_add_glob(RustLSPContext *ctx, const char *module_qn) {
    if (!ctx || !module_qn) {
        return;
    }
    if (ctx->glob_count % 16 == 0) {
        int new_cap = ctx->glob_count + 16;
        const char **ng =
            (const char **)cbm_arena_alloc(ctx->arena, (new_cap + 1) * sizeof(char *));
        if (!ng) {
            return;
        }
        if (ctx->glob_module_qns && ctx->glob_count > 0) {
            memcpy(ng, ctx->glob_module_qns, ctx->glob_count * sizeof(char *));
        }
        ctx->glob_module_qns = ng;
    }
    ctx->glob_module_qns[ctx->glob_count++] = cbm_arena_strdup(ctx->arena, module_qn);
}

static char *rust_node_text(RustLSPContext *ctx, TSNode node) {
    return cbm_node_text(ctx->arena, node, ctx->source);
}

/* ════════════════════════════════════════════════════════════════════
 * 2. Builtin / prelude tables
 * ════════════════════════════════════════════════════════════════════ */

/* Rust primitive types that the grammar reports as `primitive_type`. */
static const char *RUST_PRIMITIVES[] = {"i8",   "i16",  "i32", "i64",  "i128",  "isize", "u8",
                                        "u16",  "u32",  "u64", "u128", "usize", "f32",   "f64",
                                        "bool", "char", "str", "()",   "!",     NULL};

static bool is_rust_primitive(const char *name) {
    if (!name) {
        return false;
    }
    for (const char **p = RUST_PRIMITIVES; *p; p++) {
        if (strcmp(*p, name) == 0) {
            return true;
        }
    }
    return false;
}

/* Names of macros that behave like println-family: side effects only,
 * return type `()`. */
static bool is_void_macro(const char *name) {
    if (!name) {
        return false;
    }
    static const char *m[] = {"println",
                              "print",
                              "eprintln",
                              "eprint",
                              "panic",
                              "unimplemented",
                              "todo",
                              "unreachable",
                              "assert",
                              "assert_eq",
                              "assert_ne",
                              "debug_assert",
                              "debug_assert_eq",
                              "debug_assert_ne",
                              "writeln",
                              "write",
                              NULL};
    for (const char **p = m; *p; p++) {
        if (strcmp(*p, name) == 0) {
            return true;
        }
    }
    return false;
}

/* Names of macros that produce a `String` value. */
static bool is_string_macro(const char *name) {
    if (!name) {
        return false;
    }
    return strcmp(name, "format") == 0 || strcmp(name, "concat") == 0 ||
           strcmp(name, "stringify") == 0 || strcmp(name, "env") == 0 ||
           strcmp(name, "include_str") == 0;
}

/* Autoderef bound, the rust-analyzer cap. */
enum { RUST_DEREF_MAX_HOPS = 8 };

static bool rust_method_in(const char *const *names, const char *method_name) {
    if (!method_name) {
        return false;
    }
    for (const char *const *p = names; *p; p++) {
        if (strcmp(*p, method_name) == 0) {
            return true;
        }
    }
    return false;
}

/* Trait methods that the std Deref wrappers (`Box`, `Rc`, `Arc`, guards)
 * implement on themselves. Method lookup tries the wrapper before its
 * target, so such a call never reaches the target through Deref.
 * `Borrow`/`BorrowMut` are not in the prelude: without an explicit import,
 * `rc.borrow()` derefs to `RefCell::borrow`, so they are not listed. */
static const char *const RUST_WRAPPER_TRAIT_METHODS[] = {
    /* Clone / Copy / Default */
    "clone", "default",
    /* PartialEq / Eq / PartialOrd / Ord */
    "eq", "ne", "cmp", "partial_cmp", "lt", "le", "gt", "ge",
    /* Hash */
    "hash",
    /* Display / Debug */
    "fmt", "to_string",
    /* From / Into / TryFrom / TryInto */
    "from", "into", "try_from", "try_into",
    /* AsRef / AsMut */
    "as_ref", "as_mut",
    /* Deref */
    "deref", "deref_mut",
    /* Drop */
    "drop", NULL};

static bool rust_is_wrapper_trait_method(const char *method_name) {
    return rust_method_in(RUST_WRAPPER_TRAIT_METHODS, method_name);
}

/* Prelude trait names whose method short-names we treat as universally
 * available (for emit-on-best-effort when we cannot pin down the trait
 * impl). Borrowed from `core::prelude::v1`. */
static bool is_prelude_trait_method(const char *method_name) {
    static const char *const iterator_future[] = {
        /* Iterator (most-used subset) */
        "next", "iter", "iter_mut", "into_iter", "map", "filter", "fold", "for_each", "collect",
        "count", "sum", "max", "min", "any", "all", "find", "position", "enumerate", "zip", "chain",
        "take", "skip", "rev", "cloned", "copied", "by_ref", "step_by", "flat_map", "flatten",
        "filter_map", "peekable",
        /* Future */
        "poll",
        /* Borrow / BorrowMut */
        "borrow", "borrow_mut", NULL};
    return rust_is_wrapper_trait_method(method_name) ||
           rust_method_in(iterator_future, method_name);
}

/* ════════════════════════════════════════════════════════════════════
 * 3. Path & use resolution
 * ════════════════════════════════════════════════════════════════════ */

/* Return the last `::`-separated segment of a Rust path (`std::io::Read` →
 * `Read`). Pointer aliases into `path` — caller does not own. */
static const char *path_last_segment(const char *path) {
    if (!path || !path[0]) {
        return path;
    }
    const char *p = path;
    const char *last = path;
    while (*p) {
        if (p[0] == ':' && p[1] == ':') {
            last = p + 2;
            p += 2;
            continue;
        }
        p++;
    }
    return last;
}

/* Convert a Rust path with `::` separators into our internal QN form using
 * `.` separators. Always allocates a fresh string. */
static const char *convert_path_to_qn(CBMArena *arena, const char *path) {
    if (!path || !path[0]) {
        return path;
    }
    size_t len = strlen(path);
    char *out = (char *)cbm_arena_alloc(arena, len + 1);
    if (!out) {
        return path;
    }
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == ':' && i + 1 < len && path[i + 1] == ':') {
            out[j++] = '.';
            i++;
        } else {
            out[j++] = path[i];
        }
    }
    out[j] = '\0';
    return out;
}

/* In-place strip turbofish segments (`::<...>`) from a Rust path. The
 * grammar exposes paths like `Vec::<i32>::new` or `parse::<u32>(s)` —
 * the LSP cares about the underlying name, not the explicit type
 * arguments, so we collapse `head::<args>::tail` to `head::tail`.
 *
 * Modifies `path` in place. Safe on NULL. */
static void rust_strip_turbofish(char *path) {
    if (!path)
        return;
    char *read = path;
    char *write = path;
    while (*read) {
        if (read[0] == ':' && read[1] == ':' && read[2] == '<') {
            /* Skip ::< … > balanced. */
            int depth = 1;
            const char *p = read + 3;
            while (*p && depth > 0) {
                if (*p == '<')
                    depth++;
                else if (*p == '>')
                    depth--;
                p++;
            }
            read = (char *)p;
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
}

/* Look up a `use` alias and return its fully-qualified module path,
 * or NULL if absent. The returned pointer aliases into the use map. */
static const char *rust_resolve_use(RustLSPContext *ctx, const char *local_name) {
    if (!ctx || !local_name) {
        return NULL;
    }
    for (int i = ctx->use_count - 1; i >= 0; i--) {
        if (strcmp(ctx->use_local_names[i], local_name) == 0) {
            return ctx->use_module_paths[i];
        }
    }
    return NULL;
}

/* The Rust prelude is auto-imported into every module. We map each name
 * to its canonical QN so bare references (`String`, `Vec::new`, …)
 * resolve without an explicit `use`. The list mirrors `core::prelude::v1`
 * + `alloc::prelude` + `std::prelude::v1`. The mapping is consulted before
 * the project-local fallback so prelude names always win. */
typedef struct {
    const char *name;
    const char *qn;
} RustPreludeEntry;

static const RustPreludeEntry RUST_PRELUDE[] = {{"String", "alloc.string.String"},
                                                {"ToString", "alloc.string.ToString"},
                                                {"Vec", "alloc.vec.Vec"},
                                                {"VecDeque", "alloc.collections.VecDeque"},
                                                {"HashMap", "std.collections.HashMap"},
                                                {"BTreeMap", "alloc.collections.BTreeMap"},
                                                {"HashSet", "std.collections.HashSet"},
                                                {"BTreeSet", "alloc.collections.BTreeSet"},
                                                {"Box", "alloc.boxed.Box"},
                                                {"Rc", "alloc.rc.Rc"},
                                                {"Arc", "alloc.sync.Arc"},
                                                {"Option", "core.option.Option"},
                                                {"Some", "core.option.Option.Some"},
                                                {"None", "core.option.Option.None"},
                                                {"Result", "core.result.Result"},
                                                {"Ok", "core.result.Result.Ok"},
                                                {"Err", "core.result.Result.Err"},
                                                {"Iterator", "core.iter.Iterator"},
                                                {"IntoIterator", "core.iter.IntoIterator"},
                                                {"Future", "core.future.Future"},
                                                {"Clone", "core.clone.Clone"},
                                                {"Copy", "core.marker.Copy"},
                                                {"Send", "core.marker.Send"},
                                                {"Sync", "core.marker.Sync"},
                                                {"Default", "core.default.Default"},
                                                {"PartialEq", "core.cmp.PartialEq"},
                                                {"Eq", "core.cmp.Eq"},
                                                {"PartialOrd", "core.cmp.PartialOrd"},
                                                {"Ord", "core.cmp.Ord"},
                                                {"Hash", "core.hash.Hash"},
                                                {"Display", "core.fmt.Display"},
                                                {"Debug", "core.fmt.Debug"},
                                                {"From", "core.convert.From"},
                                                {"Into", "core.convert.Into"},
                                                {"TryFrom", "core.convert.TryFrom"},
                                                {"TryInto", "core.convert.TryInto"},
                                                {"AsRef", "core.convert.AsRef"},
                                                {"AsMut", "core.convert.AsMut"},
                                                {"Borrow", "core.borrow.Borrow"},
                                                {"BorrowMut", "core.borrow.BorrowMut"},
                                                {"Deref", "core.ops.Deref"},
                                                {"DerefMut", "core.ops.DerefMut"},
                                                {"Drop", "core.ops.Drop"},
                                                {"RefCell", "core.cell.RefCell"},
                                                {"Cell", "core.cell.Cell"},
                                                {"Mutex", "std.sync.Mutex"},
                                                {"RwLock", "std.sync.RwLock"},
                                                {NULL, NULL}};

static const char *rust_lookup_prelude(const char *name) {
    if (!name)
        return NULL;
    for (const RustPreludeEntry *e = RUST_PRELUDE; e->name; e++) {
        if (strcmp(e->name, name) == 0)
            return e->qn;
    }
    return NULL;
}

/* Canonical trait-name index used while registries are still under
 * construction.  Every suffix of an interface QN is indexed, but a suffix is
 * usable only if all registrations with that spelling name the same QN.  This
 * makes raw cross-file paths (`Pinger`, `contract::Pinger`) O(1) to resolve
 * without the old leaf-first guess or an impls x types scan. */
typedef struct {
    CBMIdxMemo unique;
    CBMIdxMemo ambiguous;
} RustTraitNameIndex;

static void rust_trait_name_index_add(RustTraitNameIndex *index, CBMArena *arena,
                                      const CBMTypeRegistry *reg, const char *key, int type_index) {
    if (!index || !arena || !reg || !key || !key[0] || type_index < 0 ||
        type_index >= reg->type_count) {
        return;
    }
    int32_t previous = cbm_idxmemo_get(&index->unique, key);
    if (previous < 0) {
        cbm_idxmemo_put_if_absent(&index->unique, arena, key, type_index);
        return;
    }
    const char *previous_qn = reg->types[previous].qualified_name;
    const char *current_qn = reg->types[type_index].qualified_name;
    if (previous_qn && current_qn && strcmp(previous_qn, current_qn) != 0) {
        cbm_idxmemo_put_if_absent(&index->ambiguous, arena, key, 1);
    }
}

static void rust_trait_name_index_build(RustTraitNameIndex *index, CBMArena *arena,
                                        const CBMTypeRegistry *reg) {
    memset(index, 0, sizeof(*index));
    if (!arena || !reg) {
        return;
    }
    for (int i = 0; i < reg->type_count; i++) {
        const CBMRegisteredType *type = &reg->types[i];
        if (!type->is_interface || !type->qualified_name) {
            continue;
        }
        const char *suffix = type->qualified_name;
        for (;;) {
            rust_trait_name_index_add(index, arena, reg, suffix, i);
            const char *dot = strchr(suffix, '.');
            if (!dot || !dot[1]) {
                break;
            }
            suffix = dot + 1;
        }
    }
}

static const char *rust_trait_name_index_lookup(const RustTraitNameIndex *index,
                                                const CBMTypeRegistry *reg, const char *key) {
    if (!index || !reg || !key || cbm_idxmemo_get(&index->ambiguous, key) >= 0) {
        return NULL;
    }
    int32_t type_index = cbm_idxmemo_get(&index->unique, key);
    if (type_index < 0 || type_index >= reg->type_count) {
        return NULL;
    }
    return reg->types[type_index].qualified_name;
}

static const char *rust_canonical_impl_trait(CBMArena *arena, const CBMTypeRegistry *reg,
                                             const RustTraitNameIndex *index, const char *raw_trait,
                                             const char *def_module_qn) {
    if (!arena || !reg || !index || !raw_trait || !raw_trait[0]) {
        return NULL;
    }
    while (isspace((unsigned char)*raw_trait)) {
        raw_trait++;
    }
    if (strncmp(raw_trait, "dyn ", 4) == 0) {
        raw_trait += 4;
    } else if (strncmp(raw_trait, "impl ", 5) == 0) {
        raw_trait += 5;
    }
    const char *normalized = convert_path_to_qn(arena, raw_trait);
    if (!normalized || !normalized[0]) {
        return NULL;
    }
    const char *generic_args = strchr(normalized, '<');
    const char *lookup_name =
        generic_args
            ? cbm_arena_strndup(arena, normalized, (size_t)(generic_args - normalized))
            : normalized;

    /* A bare/relative trait declared in the impl's own module is exact and
     * therefore wins even if another module declares the same leaf. */
    const char *relative = lookup_name;
    if (strncmp(relative, "self.", 5) == 0) {
        relative += 5;
    }
    if (def_module_qn && relative[0] && strncmp(relative, "crate.", 6) != 0 &&
        strncmp(relative, "super.", 6) != 0) {
        const char *local = cbm_arena_sprintf(arena, "%s.%s", def_module_qn, relative);
        const char *local_qn = rust_trait_name_index_lookup(index, reg, local);
        if (local_qn) {
            return local_qn;
        }
    }

    const char *exact = rust_trait_name_index_lookup(index, reg, lookup_name);
    if (exact) {
        return exact;
    }

    /* Prelude traits have canonical external QNs.  This remains fail-closed:
     * the canonical QN must actually be registered in this registry. */
    if (!strchr(lookup_name, '.')) {
        const char *prelude = rust_lookup_prelude(lookup_name);
        const char *prelude_qn = rust_trait_name_index_lookup(index, reg, prelude);
        if (prelude_qn) {
            return prelude_qn;
        }
    }

    /* `crate::x::Trait` has no literal `crate` segment in extracted QNs.
     * Resolve its semantic tail only when globally unique. */
    const char *suffix = lookup_name;
    if (strncmp(suffix, "crate.", 6) == 0) {
        suffix += 6;
    } else {
        while (strncmp(suffix, "super.", 6) == 0) {
            suffix += 6;
        }
    }
    return rust_trait_name_index_lookup(index, reg, suffix);
}

static void rust_registered_type_add_embedded(CBMArena *arena, CBMRegisteredType *type,
                                              const char *embedded_qn) {
    if (!arena || !type || !embedded_qn || !embedded_qn[0]) {
        return;
    }
    int count = 0;
    if (type->embedded_types) {
        while (type->embedded_types[count]) {
            if (strcmp(type->embedded_types[count], embedded_qn) == 0) {
                return;
            }
            count++;
        }
    }
    const char **items =
        (const char **)cbm_arena_alloc(arena, (size_t)(count + 2) * sizeof(*items));
    if (!items) {
        return;
    }
    for (int i = 0; i < count; i++) {
        items[i] = type->embedded_types[i];
    }
    items[count] = embedded_qn;
    items[count + 1] = NULL;
    type->embedded_types = items;
}

/* Strip a leading `&` / `&mut` reference prefix from a textual type so we
 * can compare the inner head segment against builtins. */
static const char *skip_ref_prefix(const char *text) {
    if (!text) {
        return text;
    }
    while (*text == '&' || isspace((unsigned char)*text)) {
        text++;
    }
    if (strncmp(text, "mut ", 4) == 0) {
        text += 4;
        while (isspace((unsigned char)*text)) {
            text++;
        }
    }
    /* Also strip a single explicit lifetime: `'a ` */
    if (*text == '\'') {
        text++;
        while (*text && (isalnum((unsigned char)*text) || *text == '_')) {
            text++;
        }
        while (isspace((unsigned char)*text)) {
            text++;
        }
    }
    return text;
}

/* Resolve an otherwise-relative multi-segment type only when its head is an
 * explicit top-level `mod head;` declaration and exactly one graph-qualified
 * candidate exists. Rust filename-stem QNs do not encode whether a source is
 * a crate root, so both the child and sibling layouts are tested and any
 * ambiguity stays unresolved. No short-name/suffix guess is admitted. */
static const char *rust_registered_relative_path(RustLSPContext *ctx, const char *normalized) {
    if (!ctx || !ctx->arena || !ctx->registry || !ctx->module_qn || !normalized || !normalized[0]) {
        return NULL;
    }
    const char *head_end = strchr(normalized, '.');
    if (!head_end || head_end == normalized) {
        return NULL;
    }
    char *head = cbm_arena_strndup(ctx->arena, normalized, (size_t)(head_end - normalized));
    if (!head || cbm_idxmemo_get(&ctx->declared_modules, head) < 0) {
        return NULL;
    }

    const char *current = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, normalized);
    const char *parent_end = strrchr(ctx->module_qn, '.');
    const char *parent =
        parent_end ? cbm_arena_sprintf(ctx->arena, "%.*s.%s", (int)(parent_end - ctx->module_qn),
                                       ctx->module_qn, normalized)
                   : NULL;
    bool current_exists = current && cbm_registry_lookup_type(ctx->registry, current);
    bool parent_exists = parent && cbm_registry_lookup_type(ctx->registry, parent);
    if (current_exists == parent_exists) {
        return NULL;
    }
    return current_exists ? current : parent;
}

/* ── File-derived module identity ──────────────────────────────────
 * Graph QNs are file paths: `<project>.<dirs>.<src>.<mods>.<stem>`. Rust
 * paths name modules, so `crate::`, `super::`, workspace crates and inline
 * or `mod.rs` modules must be mapped onto that shape before any registry
 * lookup. Every mapping below is checked against the registry and refuses
 * ambiguity; no short-name guess is admitted. */

static bool rust_segment_is_source_root(const char *seg, size_t len) {
    static const char *const roots[] = {"src", "tests", "benches", "examples"};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        if (strlen(roots[i]) == len && strncmp(seg, roots[i], len) == 0) {
            return true;
        }
    }
    return false;
}

static const char *rust_target_root_qn(RustLSPContext *ctx, const char *package_path,
                                       const CBMCargoTarget *target) {
    if (!ctx || !ctx->module_qn || !target || !target->path)
        return NULL;
    const char *project_end = strchr(ctx->module_qn, '.');
    size_t project_len =
        project_end ? (size_t)(project_end - ctx->module_qn) : strlen(ctx->module_qn);
    while (package_path && strncmp(package_path, "./", 2) == 0)
        package_path += 2;
    const char *path = target->path;
    while (strncmp(path, "./", 2) == 0)
        path += 2;
    char *joined = package_path && package_path[0]
                       ? cbm_arena_sprintf(ctx->arena, "%s/%s", package_path, path)
                       : cbm_arena_strdup(ctx->arena, path);
    size_t len = strlen(joined);
    if (len > 3 && strcmp(joined + len - 3, ".rs") == 0)
        joined[len - 3] = '\0';
    for (char *p = joined; *p; p++) {
        if (*p == '/' || *p == '\\')
            *p = '.';
    }
    return cbm_arena_sprintf(ctx->arena, "%.*s.%s", (int)project_len, ctx->module_qn, joined);
}

static const char *rust_target_dir_qn(RustLSPContext *ctx, const char *package_path,
                                      const CBMCargoTarget *target) {
    const char *root = rust_target_root_qn(ctx, package_path, target);
    const char *dot = root ? strrchr(root, '.') : NULL;
    return dot ? cbm_arena_strndup(ctx->arena, root, (size_t)(dot - root)) : NULL;
}

static const CBMCargoMember *rust_package_member(RustLSPContext *ctx, const char *qn) {
    if (!ctx || !ctx->cargo_manifest || !ctx->module_qn || !qn)
        return NULL;
    const CBMCargoManifest *manifest = (const CBMCargoManifest *)ctx->cargo_manifest;
    const CBMCargoMember *owner = NULL;
    size_t owner_len = 0;
    for (int i = 0; i < manifest->member_count; i++) {
        const CBMCargoMember *member = &manifest->members[i];
        if (!member->member_path)
            continue;
        CBMCargoTarget marker = {.path = "root.rs"};
        const char *root = rust_target_root_qn(ctx, member->member_path, &marker);
        const char *dot = root ? strrchr(root, '.') : NULL;
        size_t len = dot ? (size_t)(dot - root) : 0;
        if (len > owner_len && strncmp(qn, root, len) == 0 &&
            (qn[len] == '\0' || qn[len] == '.')) {
            owner = member;
            owner_len = len;
        }
    }
    return owner;
}

static const CBMCargoTarget *rust_package_targets(RustLSPContext *ctx, const char *qn,
                                                   const char **package_path, int *target_count) {
    if (!ctx || !ctx->cargo_manifest || !ctx->module_qn || !qn)
        return NULL;
    const CBMCargoManifest *manifest = (const CBMCargoManifest *)ctx->cargo_manifest;
    const CBMCargoMember *owner = rust_package_member(ctx, qn);
    if (owner) {
        *package_path = owner->member_path;
        *target_count = owner->target_count;
        return owner->targets;
    }
    *package_path = "";
    *target_count = manifest->target_count;
    return manifest->targets;
}

static const char *rust_crate_prefix_for_qn(RustLSPContext *ctx, const char *qn) {
    const char *package_path = NULL;
    int target_count = 0;
    const CBMCargoTarget *targets = rust_package_targets(ctx, qn, &package_path, &target_count);
    const char *best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < target_count; i++) {
        const char *root = rust_target_root_qn(ctx, package_path, &targets[i]);
        const char *dir = rust_target_dir_qn(ctx, package_path, &targets[i]);
        size_t dir_len = dir ? strlen(dir) : 0;
        if (root && strcmp(qn, root) == 0)
            return dir;
        if (dir_len > best_len && strncmp(qn, dir, dir_len) == 0 &&
            (qn[dir_len] == '\0' || qn[dir_len] == '.')) {
            best = dir;
            best_len = dir_len;
        }
    }
    if (best)
        return best;
    const char *first = strchr(qn, '.');
    for (const char *p = first ? first + 1 : NULL; p && *p;) {
        const char *end = strchr(p, '.');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (rust_segment_is_source_root(p, len))
            return cbm_arena_strndup(ctx->arena, qn, (size_t)(p + len - qn));
        if (!end)
            break;
        p = end + 1;
    }
    return NULL;
}

static const char *rust_crate_root_prefix(RustLSPContext *ctx) {
    if (!ctx || !ctx->module_qn)
        return NULL;
    if (!ctx->crate_root_prefix) {
        ctx->crate_root_prefix = rust_crate_prefix_for_qn(ctx, ctx->module_qn);
        if (!ctx->crate_root_prefix) {
            const char *first = strchr(ctx->module_qn, '.');
            const char *second = first ? strchr(first + 1, '.') : NULL;
            ctx->crate_root_prefix =
                second ? cbm_arena_strndup(ctx->arena, ctx->module_qn,
                                           (size_t)(second - ctx->module_qn))
                       : ctx->module_qn;
        }
    }
    return ctx->crate_root_prefix;
}

static bool rust_stem_owns_directory(const char *stem) {
    return strcmp(stem, "mod") == 0 || strcmp(stem, "lib") == 0 || strcmp(stem, "main") == 0;
}

static bool rust_qn_owns_directory(RustLSPContext *ctx, const char *qn) {
    const char *dot = qn ? strrchr(qn, '.') : NULL;
    if (dot && rust_stem_owns_directory(dot + 1))
        return true;
    const char *package_path = NULL;
    int target_count = 0;
    const CBMCargoTarget *targets = rust_package_targets(ctx, qn, &package_path, &target_count);
    for (int i = 0; i < target_count; i++) {
        const char *root = rust_target_root_qn(ctx, package_path, &targets[i]);
        if (root && strcmp(root, qn) == 0)
            return true;
    }
    return false;
}

/* QN of the directory whose files are this file's child modules: `mod.rs`,
 * `lib.rs` and `main.rs` own their directory, `foo.rs` owns `foo/`. */
static const char *rust_module_dir_qn(RustLSPContext *ctx) {
    if (!ctx || !ctx->module_qn) {
        return NULL;
    }
    if (ctx->module_dir_qn) {
        return ctx->module_dir_qn;
    }
    const char *mq = ctx->module_qn;
    const char *dot = strrchr(mq, '.');
    ctx->module_dir_qn = dot && rust_qn_owns_directory(ctx, mq)
                             ? cbm_arena_strndup(ctx->arena, mq, (size_t)(dot - mq))
                             : mq;
    return ctx->module_dir_qn;
}

/* QN of the parent module for `super::`. */
static const char *rust_super_module_qn(RustLSPContext *ctx) {
    if (!ctx || !ctx->relative_module_qn) {
        return NULL;
    }
    const char *module = ctx->relative_module_qn;
    const char *module_dot = strrchr(module, '.');
    bool physical_identity = ctx->module_qn && strcmp(module, ctx->module_qn) == 0;
    const char *dir = physical_identity && module_dot && rust_qn_owns_directory(ctx, module)
                          ? cbm_arena_strndup(ctx->arena, module, (size_t)(module_dot - module))
                          : module;
    const char *dot = strrchr(dir, '.');
    return dot ? cbm_arena_strndup(ctx->arena, dir, (size_t)(dot - dir)) : NULL;
}

/* File-derived directory QN containing a workspace member's library root. */
static const char *rust_member_root_prefix(RustLSPContext *ctx, const CBMCargoMember *member) {
    if (!ctx || !ctx->module_qn || !member || !member->member_path) {
        return NULL;
    }
    for (int i = 0; i < member->target_count; i++) {
        if (member->targets[i].kind == CBM_CARGO_TARGET_LIB)
            return rust_target_dir_qn(ctx, member->member_path, &member->targets[i]);
    }
    CBMCargoTarget fallback = {.kind = CBM_CARGO_TARGET_LIB, .path = "src/lib.rs"};
    return rust_target_dir_qn(ctx, member->member_path, &fallback);
}

/* External crate paths enter through the package's library target.  Keep the
 * root file in the identity here: selecting only its directory would let a
 * sibling binary target answer a dependency path. */
static const char *rust_member_library_root_qn(RustLSPContext *ctx,
                                                const CBMCargoMember *member) {
    if (!ctx || !member || !member->member_path)
        return NULL;
    for (int i = 0; i < member->target_count; i++) {
        if (member->targets[i].kind == CBM_CARGO_TARGET_LIB)
            return rust_target_root_qn(ctx, member->member_path, &member->targets[i]);
    }
    return rust_member_root_prefix(ctx, member);
}

/* Rewrite a `crate_name.rest` head onto the member's file-derived prefix. */
static const char *rust_canonical_workspace_qn(RustLSPContext *ctx, const char *qn) {
    if (!ctx || !qn || !ctx->cargo_manifest || !ctx->module_qn) {
        return qn;
    }
    const char *dot = strchr(qn, '.');
    if (!dot) {
        return qn;
    }
    const char *project_end = strchr(ctx->module_qn, '.');
    size_t project_len =
        project_end ? (size_t)(project_end - ctx->module_qn) : strlen(ctx->module_qn);
    if ((size_t)(dot - qn) == project_len && strncmp(qn, ctx->module_qn, project_len) == 0) {
        return qn;
    }
    char *head = cbm_arena_strndup(ctx->arena, qn, (size_t)(dot - qn));
    const CBMCargoMember *member =
        cbm_cargo_find_member((const CBMCargoManifest *)ctx->cargo_manifest, head);
    const char *prefix = NULL;
    if (member) {
        prefix = strchr(dot + 1, '.') ? rust_member_root_prefix(ctx, member)
                                      : rust_member_library_root_qn(ctx, member);
    }
    return prefix ? cbm_arena_sprintf(ctx->arena, "%s%s", prefix, dot) : qn;
}

struct RustQnMatch {
    const CBMRegisteredFunc *func;
    const CBMRegisteredType *type;
    int matches;
};

static void rust_qn_probe(RustLSPContext *ctx, const char *candidate, bool want_type,
                          RustQnMatch *m) {
    if (!candidate) {
        return;
    }
    if (want_type) {
        const CBMRegisteredType *t = cbm_registry_lookup_type(ctx->registry, candidate);
        if (t && t != m->type) {
            m->type = t;
            m->matches++;
        }
        return;
    }
    const char *leaf_dot = strrchr(candidate, '.');
    const char *leaf = leaf_dot ? leaf_dot + 1 : candidate;
    const CBMRegisteredFunc *f = NULL;
    size_t len = strlen(candidate);
    CBMFreeFuncIter it;
    cbm_registry_free_funcs_by_short_name_chain(ctx->registry, leaf, &it);
    int matches = 0;
    for (int i; matches < 2 && (i = cbm_free_func_iter_next(&it)) >= 0;) {
        const CBMRegisteredFunc *cand = &it.reg->funcs[i];
        bool exact = cand->qualified_name && strcmp(cand->qualified_name, candidate) == 0;
        bool cfg_twin = cand->qualified_name &&
                        strncmp(cand->qualified_name, candidate, len) == 0 &&
                        cand->qualified_name[len] == '#';
        if (exact || cfg_twin) {
            f = cand;
            matches++;
        }
    }
    if (matches != 1) {
        f = NULL;
    }
    if (f && f != m->func) {
        m->func = f;
        m->matches++;
    }
}

static int rust_probe_target_array(RustLSPContext *ctx, const char *package_path,
                                   const CBMCargoTarget *targets, int target_count,
                                   const char *parent, const char *leaf, bool want_type,
                                   RustQnMatch *m) {
    int roots = 0;
    for (int i = 0; i < target_count; i++) {
        const char *dir = rust_target_dir_qn(ctx, package_path, &targets[i]);
        if (!dir || strcmp(dir, parent) != 0)
            continue;
        roots++;
        const char *root = rust_target_root_qn(ctx, package_path, &targets[i]);
        rust_qn_probe(ctx, cbm_arena_sprintf(ctx->arena, "%s.%s", root, leaf), want_type, m);
    }
    return roots;
}

static int rust_probe_target_roots(RustLSPContext *ctx, const char *parent, const char *leaf,
                                   bool want_type, RustQnMatch *m) {
    if (!ctx->cargo_manifest)
        return 0;
    const CBMCargoManifest *manifest = (const CBMCargoManifest *)ctx->cargo_manifest;
    int roots = rust_probe_target_array(ctx, "", manifest->targets, manifest->target_count,
                                        parent, leaf, want_type, m);
    for (int i = 0; i < manifest->member_count; i++) {
        const CBMCargoMember *member = &manifest->members[i];
        roots += rust_probe_target_array(ctx, member->member_path, member->targets,
                                         member->target_count, parent, leaf, want_type, m);
    }
    return roots;
}

static void rust_probe_library_root_children(RustLSPContext *ctx, const char *qn, bool want_type,
                                             RustQnMatch *m) {
    if (!ctx->cargo_manifest)
        return;
    const CBMCargoManifest *manifest = (const CBMCargoManifest *)ctx->cargo_manifest;
    for (int member_index = 0; member_index < manifest->member_count; member_index++) {
        const CBMCargoMember *member = &manifest->members[member_index];
        for (int target_index = 0; target_index < member->target_count; target_index++) {
            const CBMCargoTarget *target = &member->targets[target_index];
            if (target->kind != CBM_CARGO_TARGET_LIB)
                continue;
            const char *root = rust_target_root_qn(ctx, member->member_path, target);
            const char *dir = rust_target_dir_qn(ctx, member->member_path, target);
            size_t root_len = root ? strlen(root) : 0;
            if (!root || !dir || strncmp(qn, root, root_len) != 0 || qn[root_len] != '.')
                continue;
            const char *tail = qn + root_len + 1;
            if (!strchr(tail, '.'))
                continue;
            rust_qn_probe(ctx, cbm_arena_sprintf(ctx->arena, "%s.%s", dir, tail), want_type, m);
        }
    }
}

static int rust_append_target_files(RustLSPContext *ctx, const char *package_path,
                                    const CBMCargoTarget *targets, int target_count,
                                    const char *module_qn, const char **files, int file_count) {
    for (int i = 0; i < target_count; i++) {
        const char *dir = rust_target_dir_qn(ctx, package_path, &targets[i]);
        if (dir && strcmp(dir, module_qn) == 0)
            files[file_count++] = rust_target_root_qn(ctx, package_path, &targets[i]);
    }
    return file_count;
}

static int rust_collect_target_files(RustLSPContext *ctx, const char *module_qn,
                                     const char **files, int file_count) {
    if (!ctx->cargo_manifest)
        return file_count;
    const CBMCargoManifest *manifest = (const CBMCargoManifest *)ctx->cargo_manifest;
    file_count = rust_append_target_files(ctx, "", manifest->targets, manifest->target_count,
                                          module_qn, files, file_count);
    for (int i = 0; i < manifest->member_count; i++) {
        const CBMCargoMember *member = &manifest->members[i];
        file_count = rust_append_target_files(ctx, member->member_path, member->targets,
                                              member->target_count, module_qn, files, file_count);
    }
    return file_count;
}

static int rust_target_capacity(const RustLSPContext *ctx) {
    if (!ctx->cargo_manifest)
        return 0;
    const CBMCargoManifest *manifest = (const CBMCargoManifest *)ctx->cargo_manifest;
    int capacity = manifest->target_count;
    for (int i = 0; i < manifest->member_count; i++)
        capacity += manifest->members[i].target_count;
    return capacity;
}

/* Probe every file layout a module path can denote: the exact QN, a
 * `mod.rs` directory module, the `lib.rs`/`main.rs` crate root, a child
 * module declared from a crate-root file, and an inline `mod` flattened by
 * extraction. Exactly one distinct registered item may answer. */
static void rust_qn_variants(RustLSPContext *ctx, const char *qn, bool want_type, RustQnMatch *m) {
    memset(m, 0, sizeof(*m));
    if (!ctx || !ctx->registry || !qn || !qn[0]) {
        return;
    }
    qn = rust_canonical_workspace_qn(ctx, qn);
    rust_qn_probe(ctx, qn, want_type, m);
    rust_probe_library_root_children(ctx, qn, want_type, m);
    const char *leaf_dot = strrchr(qn, '.');
    if (!leaf_dot || leaf_dot == qn) {
        return;
    }
    const char *leaf = leaf_dot + 1;
    char *parent = cbm_arena_strndup(ctx->arena, qn, (size_t)(leaf_dot - qn));
    rust_qn_probe(ctx, cbm_arena_sprintf(ctx->arena, "%s.mod.%s", parent, leaf), want_type, m);
    int target_root_count = rust_probe_target_roots(ctx, parent, leaf, want_type, m);
    const char *prefix = rust_crate_root_prefix(ctx);
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    bool under_prefix = prefix && strncmp(parent, prefix, prefix_len) == 0 &&
                        (parent[prefix_len] == '\0' || parent[prefix_len] == '.');
    if (target_root_count == 0 && under_prefix && parent[prefix_len] == '\0') {
        rust_qn_probe(ctx, cbm_arena_sprintf(ctx->arena, "%s.lib.%s", prefix, leaf), want_type, m);
        rust_qn_probe(ctx, cbm_arena_sprintf(ctx->arena, "%s.main.%s", prefix, leaf), want_type, m);
    }
    if (under_prefix && parent[prefix_len] != '\0') {
        /* `<prefix>.lib.child.f` (a child module reached from a crate-root
         * file) lives in `<prefix>.child.f`; the same holds for `main`. */
        const char *tail = parent + prefix_len + 1;
        const char *seg_end = strchr(tail, '.');
        if (seg_end) {
            size_t seg_len = (size_t)(seg_end - tail);
            if ((seg_len == 3 && strncmp(tail, "lib", 3) == 0) ||
                (seg_len == 4 && strncmp(tail, "main", 4) == 0)) {
                rust_qn_probe(ctx,
                              cbm_arena_sprintf(ctx->arena, "%s.%s.%s", prefix, seg_end + 1, leaf),
                              want_type, m);
            }
        }
        /* `mod` is a keyword, so an interior `.mod.` segment always names a
         * `mod.rs` file standing for its directory: `a.mod.b.f` is `a.b.f`. */
        const char *mod_seg = strstr(parent, ".mod.");
        if (mod_seg) {
            rust_qn_probe(ctx,
                          cbm_arena_sprintf(ctx->arena, "%.*s%s.%s", (int)(mod_seg - parent),
                                            parent, mod_seg + 4, leaf),
                          want_type, m);
        }
        /* Inline `mod name { ... }` items are extracted under the file itself. */
        const char *parent_dot = strrchr(parent, '.');
        bool physical_identity = ctx->relative_module_qn && ctx->module_qn &&
                                 strcmp(ctx->relative_module_qn, ctx->module_qn) == 0;
        if (physical_identity && parent_dot && (size_t)(parent_dot - parent) > prefix_len) {
            rust_qn_probe(
                ctx,
                cbm_arena_sprintf(ctx->arena, "%.*s.%s", (int)(parent_dot - parent), parent, leaf),
                want_type, m);
        }
    }
    /* Nothing declared under that module: follow the module file's own
     * `use` (a `pub use` re-export, or a parent's import seen via `super::*`). */
    if (m->matches == 0 && ctx->export_depth < 3) {
        const char *exported = rust_module_export(ctx, parent, leaf);
        if (exported) {
            ctx->export_depth++;
            rust_qn_variants(ctx, exported, want_type, m);
            ctx->export_depth--;
        }
    }
}

/* Resolve `<module>.<name>` through the module file's own `use` declaration
 * (`pub use inner::Name;` re-export, or a parent's import seen through
 * `super::*`). The path text is resolved as that file would resolve it. */
/* Resolve a raw `use` path as the file with module QN `module_qn` would,
 * using only module-relative keywords, workspace crate heads and absolute
 * paths: that file's own imports, globs and `mod` declarations are not
 * available here. */
static const char *rust_resolve_path_in_module(RustLSPContext *ctx, const char *module_qn,
                                               const char *path, const char *relative_dir) {
    if (strncmp(path, "crate::", 7) == 0) {
        const char *prefix = rust_qn_crate_prefix(ctx, module_qn);
        return prefix ? cbm_arena_sprintf(ctx->arena, "%s.%s", prefix,
                                          convert_path_to_qn(ctx->arena, path + 7))
                      : NULL;
    }
    if (strncmp(path, "self::", 6) == 0) {
        return cbm_arena_sprintf(ctx->arena, "%s.%s", module_qn,
                                 convert_path_to_qn(ctx->arena, path + 6));
    }
    if (strncmp(path, "super::", 7) == 0) {
        const char *dot = strrchr(module_qn, '.');
        const char *dir = dot && rust_qn_owns_directory(ctx, module_qn)
                              ? cbm_arena_strndup(ctx->arena, module_qn, (size_t)(dot - module_qn))
                              : module_qn;
        const char *rest = path + 7;
        const char *parent = dir;
        for (;;) {
            const char *pdot = strrchr(parent, '.');
            if (!pdot) {
                return NULL;
            }
            parent = cbm_arena_strndup(ctx->arena, parent, (size_t)(pdot - parent));
            if (strncmp(rest, "super::", 7) != 0) {
                break;
            }
            rest += 7;
        }
        return cbm_arena_sprintf(ctx->arena, "%s.%s", parent, convert_path_to_qn(ctx->arena, rest));
    }
    const char *sep = strstr(path, "::");
    if (sep && ctx->cargo_manifest) {
        char *head = cbm_arena_strndup(ctx->arena, path, (size_t)(sep - path));
        const CBMCargoMember *member =
            cbm_cargo_find_member((const CBMCargoManifest *)ctx->cargo_manifest, head);
        const char *member_prefix = NULL;
        if (member) {
            member_prefix = strstr(sep + 2, "::") ? rust_member_root_prefix(ctx, member)
                                                   : rust_member_library_root_qn(ctx, member);
        }
        if (member_prefix) {
            return cbm_arena_sprintf(ctx->arena, "%s.%s", member_prefix,
                                     convert_path_to_qn(ctx->arena, sep + 2));
        }
    }
    if (sep && relative_dir) {
        /* `pub use child::Item;` names a module declared by that file; the
         * registry check on the result keeps an external head harmless. */
        return cbm_arena_sprintf(ctx->arena, "%s.%s", relative_dir,
                                 convert_path_to_qn(ctx->arena, path));
    }
    return convert_path_to_qn(ctx->arena, path);
}

/* Raw path `file` binds `name` to: its explicit `use` of that name, or with
 * `via_glob` its `use prefix::*` read as `prefix::name`. */
static const char *rust_module_export_path(RustLSPContext *ctx, const char *file,
                                           const char *name, bool via_glob) {
    if (!via_glob) {
        return ctx->imports->lookup(ctx->imports->ctx, file, name);
    }
    const char *glob = ctx->imports->lookup(ctx->imports->ctx, file, "*");
    size_t len = glob ? strlen(glob) : 0;
    if (len < 4 || strcmp(glob + len - 3, "::*") != 0) {
        return NULL;
    }
    return cbm_arena_sprintf(ctx->arena, "%.*s%s", (int)(len - 1), glob, name);
}

/* Resolve `<module>.<name>` through the module file's own `use` declaration
 * (`pub use inner::Name;` re-export, or a parent's import seen through
 * `super::*`). Results, including misses, are memoized per file. */
static const char *rust_module_export(RustLSPContext *ctx, const char *module_qn,
                                      const char *name) {
    if (!ctx || !ctx->imports || !ctx->imports->lookup || !module_qn || !name) {
        return NULL;
    }
    const char *memo_key = cbm_arena_sprintf(ctx->arena, "%s\x1f%s", module_qn, name);
    int32_t memo = cbm_idxmemo_get(&ctx->export_memo, memo_key);
    if (memo >= 0) {
        return memo == 0 ? NULL : ctx->export_values[memo - 1];
    }
    const char *result = NULL;
    const char *prefix = rust_crate_root_prefix(ctx);
    int file_capacity = rust_target_capacity(ctx) + 4;
    const char **files =
        (const char **)cbm_arena_alloc(ctx->arena, (size_t)file_capacity * sizeof(*files));
    if (!files)
        return NULL;
    int file_count = 0;
    files[file_count++] = module_qn;
    files[file_count++] = cbm_arena_sprintf(ctx->arena, "%s.mod", module_qn);
    int before_targets = file_count;
    file_count = rust_collect_target_files(ctx, module_qn, files, file_count);
    if (file_count == before_targets && prefix && strcmp(module_qn, prefix) == 0) {
        files[file_count++] = cbm_arena_sprintf(ctx->arena, "%s.lib", module_qn);
        files[file_count++] = cbm_arena_sprintf(ctx->arena, "%s.main", module_qn);
    }
    const char *own = cbm_arena_sprintf(ctx->arena, "%s.%s", module_qn, name);
    int passes = ctx->follow_glob_exports ? 2 : 1;
    for (int i = 0; i < file_count * passes && !result; i++) {
        const char *file = files[i % file_count];
        const char *path = rust_module_export_path(ctx, file, name, i >= file_count);
        if (!path || !path[0]) {
            continue;
        }
        const char *file_dot = strrchr(file, '.');
        const char *file_dir = file_dot && rust_qn_owns_directory(ctx, file)
                                   ? cbm_arena_strndup(ctx->arena, file, (size_t)(file_dot - file))
                                   : file;
        const char *absolute = rust_resolve_path_in_module(ctx, file, path, NULL);
        const char *relative = rust_resolve_path_in_module(ctx, file, path, file_dir);
        /* Prefer the reading that names a registered item; the caller
         * re-validates whichever candidate is returned. */
        RustQnMatch probe;
        if (relative && strcmp(relative, absolute ? absolute : "") != 0) {
            ctx->export_depth++;
            rust_qn_variants(ctx, relative, false, &probe);
            if (probe.matches == 0) {
                rust_qn_variants(ctx, relative, true, &probe);
            }
            ctx->export_depth--;
            if (probe.matches == 1) {
                result = relative;
            }
        }
        if (!result && absolute && strcmp(absolute, own) != 0) {
            result = absolute;
        }
    }
    int32_t slot = 0;
    if (result) {
        if (ctx->export_value_count % 64 == 0) {
            const char **grown = (const char **)cbm_arena_alloc(
                ctx->arena, (size_t)(ctx->export_value_count + 64) * sizeof(char *));
            if (!grown) {
                return result;
            }
            if (ctx->export_value_count > 0) {
                memcpy(grown, ctx->export_values, (size_t)ctx->export_value_count * sizeof(char *));
            }
            ctx->export_values = grown;
        }
        ctx->export_values[ctx->export_value_count++] = result;
        slot = ctx->export_value_count;
    }
    cbm_idxmemo_put_if_absent(&ctx->export_memo, ctx->arena, memo_key, slot);
    return result;
}

static const CBMRegisteredFunc *rust_lookup_func_variants(RustLSPContext *ctx, const char *qn) {
    RustQnMatch m;
    rust_qn_variants(ctx, qn, false, &m);
    return m.matches == 1 ? m.func : NULL;
}

static const CBMRegisteredType *rust_lookup_type_variants(RustLSPContext *ctx, const char *qn) {
    RustQnMatch m;
    rust_qn_variants(ctx, qn, true, &m);
    return m.matches == 1 ? m.type : NULL;
}

/* A type reachable through exactly one of this file's glob imports. */
static const CBMRegisteredType *rust_resolve_glob_type(RustLSPContext *ctx, const char *name) {
    if (!ctx || !name || ctx->glob_count == 0) {
        return NULL;
    }
    const CBMRegisteredType *hit = NULL;
    int sources = 0;
    for (int g = 0; g < ctx->glob_count; g++) {
        const CBMRegisteredType *t = rust_lookup_type_variants(
            ctx, cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->glob_module_qns[g], name));
        if (t && t != hit) {
            hit = t;
            sources++;
        }
    }
    return sources == 1 ? hit : NULL;
}

/* A qualified head can itself be introduced through `use module::*`, for
 * example a child file's `use super::*` seeing the parent's imported module.
 * Require exactly one exported identity before it may outrank a same-named
 * workspace crate. */
static const char *rust_resolve_glob_module_item(RustLSPContext *ctx, const char *name,
                                                 int *out_sources) {
    const char *hit = NULL;
    int sources = 0;
    for (int g = 0; g < ctx->glob_count; g++) {
        const char *candidate = rust_module_export(ctx, ctx->glob_module_qns[g], name);
        if (candidate && (!hit || strcmp(candidate, hit) != 0)) {
            hit = candidate;
            sources++;
        }
    }
    if (out_sources)
        *out_sources = sources;
    return sources == 1 ? hit : NULL;
}

/* Crate prefix of an arbitrary file-derived QN (see rust_crate_root_prefix). */
static const char *rust_qn_crate_prefix(RustLSPContext *ctx, const char *qn) {
    return rust_crate_prefix_for_qn(ctx, qn);
}

/* Registered QN for a type name that reached the resolver as
 * `<defining module>.<Name>`: signature and field types are recorded in
 * the defining file's module without that file's imports. Recovery order:
 * exact, layout variants, this file's own import of the same leaf, this
 * file's module, a unique glob source, then a unique type of that leaf
 * inside the defining crate. NULL when nothing answers exactly once. */
static const char *rust_registered_type_qn(RustLSPContext *ctx, const char *qn) {
    if (!ctx || !ctx->registry || !qn || !qn[0]) {
        return NULL;
    }
    if (cbm_registry_lookup_type(ctx->registry, qn)) {
        return qn;
    }
    const CBMRegisteredType *t = rust_lookup_type_variants(ctx, qn);
    if (t) {
        return t->qualified_name;
    }
    const char *dot = strrchr(qn, '.');
    const char *leaf = dot ? dot + 1 : qn;
    if (!leaf[0] || strchr(leaf, '<')) {
        return NULL;
    }
    const char *imported = rust_resolve_use(ctx, leaf);
    if (imported) {
        t = rust_lookup_type_variants(ctx, convert_path_to_qn(ctx->arena, imported));
        if (t) {
            return t->qualified_name;
        }
    }
    if (ctx->module_qn) {
        t = rust_lookup_type_variants(ctx,
                                      cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, leaf));
        if (t) {
            return t->qualified_name;
        }
    }
    t = rust_resolve_glob_type(ctx, leaf);
    if (t) {
        return t->qualified_name;
    }
    const char *prefix = dot ? rust_qn_crate_prefix(ctx, qn) : NULL;
    if (!prefix) {
        return NULL;
    }
    size_t prefix_len = strlen(prefix);
    const CBMRegisteredType *unique = NULL;
    int count = 0;
    CBMTypeShortIter it;
    cbm_registry_types_by_short_name_chain(ctx->registry, leaf, &it);
    for (int i; (i = cbm_type_short_iter_next(&it)) >= 0;) {
        const CBMRegisteredType *cand = &it.reg->types[i];
        if (!cand->qualified_name || strcmp(cand->short_name ? cand->short_name : "", leaf) != 0) {
            continue;
        }
        if (strncmp(cand->qualified_name, prefix, prefix_len) == 0 &&
            cand->qualified_name[prefix_len] == '.' && cand != unique) {
            unique = cand;
            count++;
        }
    }
    return count == 1 ? unique->qualified_name : NULL;
}

/* Resolve a Rust *path expression* (e.g. `Foo::bar` or `crate::x::y`)
 * into a canonical QN. The resolver cascades through these rules,
 * matching what `rust-analyzer`'s name resolver does at the path level:
 *
 *   1.  `Self::X` → `<self_type_qn>.X`
 *   2.  `crate::a::b` → `<root_module_qn>.a.b`
 *   3.  `super::a` → strip last segment of `module_qn` and prepend
 *   4.  Single-segment + matches a `use` local-name → `<full path>.X`
 *   5.  Multi-segment whose first segment is a `use` local → splice
 *   6.  Falls through unchanged (caller decides what to do).
 *
 * The returned string is arena-owned; in case (6) we return the input
 * with `::` already converted to `.`. */
static const char *rust_resolve_path_expr(RustLSPContext *ctx, const char *path) {
    if (!ctx || !path || !path[0]) {
        return path;
    }

    /* Self:: handling — we treat the receiver type's QN as the head. */
    if (strncmp(path, "Self::", 6) == 0 && ctx->self_type_qn) {
        return cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->self_type_qn,
                                 convert_path_to_qn(ctx->arena, path + 6));
    }
    if (strcmp(path, "Self") == 0 && ctx->self_type_qn) {
        return ctx->self_type_qn;
    }
    if (strncmp(path, "self::", 6) == 0 && ctx->relative_module_qn) {
        return cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->relative_module_qn,
                                 convert_path_to_qn(ctx->arena, path + 6));
    }

    /* crate:: → <root>. We approximate the crate root as the first dotted
     * segment of `module_qn` after the project prefix. The pipeline
     * forms `module_qn` as `<project>.<crate>.<rel-path-segments>`, so
     * the first two segments are project + crate root. */
    if (strncmp(path, "crate::", 7) == 0 && ctx->module_qn) {
        const char *prefix = rust_crate_root_prefix(ctx);
        return cbm_arena_sprintf(ctx->arena, "%s.%s", prefix,
                                 convert_path_to_qn(ctx->arena, path + 7));
    }

    /* super:: → the parent module's directory; `mod.rs`, `lib.rs` and
     * `main.rs` already stand for their directory. Repeated `super::` climbs. */
    if (strncmp(path, "super::", 7) == 0 && ctx->relative_module_qn) {
        const char *parent = rust_super_module_qn(ctx);
        const char *rest = path + 7;
        while (parent && strncmp(rest, "super::", 7) == 0) {
            const char *dot = strrchr(parent, '.');
            parent = dot ? cbm_arena_strndup(ctx->arena, parent, (size_t)(dot - parent)) : NULL;
            rest += 7;
        }
        if (parent) {
            return cbm_arena_sprintf(ctx->arena, "%s.%s", parent,
                                     convert_path_to_qn(ctx->arena, rest));
        }
    }

    /* Find first "::" — split into head + tail. */
    const char *sep = strstr(path, "::");
    if (!sep) {
        const char *full = rust_resolve_use(ctx, path);
        if (full) {
            return convert_path_to_qn(ctx->arena, full);
        }
        /* Prelude name (e.g. `String`, `Vec`)? */
        const char *prelude = rust_lookup_prelude(path);
        if (prelude) {
            return prelude;
        }
        /* Bare identifier — this module's own item, else a unique glob source. */
        if (ctx->module_qn) {
            const char *own = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, path);
            if (ctx->glob_count > 0 && ctx->registry &&
                !cbm_registry_lookup_type(ctx->registry, own) &&
                !cbm_registry_lookup_func(ctx->registry, own)) {
                const CBMRegisteredType *glob_type = rust_resolve_glob_type(ctx, path);
                if (glob_type) {
                    return glob_type->qualified_name;
                }
            }
            return own;
        }
        return path;
    }

    char *head = cbm_arena_strndup(ctx->arena, path, (size_t)(sep - path));
    const char *tail = sep + 2;

    const char *full = rust_resolve_use(ctx, head);
    if (full) {
        /* The use-map's full path already includes `head` as the last
         * segment; concat its parent with the rest. */
        const char *full_dotted = convert_path_to_qn(ctx->arena, full);
        const char *tail_dotted = convert_path_to_qn(ctx->arena, tail);
        return cbm_arena_sprintf(ctx->arena, "%s.%s", full_dotted, tail_dotted);
    }

    /* Prelude head: `String::from` → `alloc.string.String.from`. */
    const char *prelude = rust_lookup_prelude(head);
    if (prelude) {
        return cbm_arena_sprintf(ctx->arena, "%s.%s", prelude,
                                 convert_path_to_qn(ctx->arena, tail));
    }

    /* A source declaration is stronger than a coincidentally same-named
     * workspace package.  Prefer whichever physical layout is registered;
     * two distinct registered layouts remain unresolved. */
    if (cbm_idxmemo_get(&ctx->declared_modules, head) >= 0) {
        const char *dir = rust_module_dir_qn(ctx);
        if (dir) {
            const char *tail_qn = convert_path_to_qn(ctx->arena, tail);
            const char *owned = cbm_arena_sprintf(ctx->arena, "%s.%s.%s", dir, head, tail_qn);
            const char *other =
                cbm_arena_sprintf(ctx->arena, "%s.%s.%s", ctx->module_qn, head, tail_qn);
            bool owned_exists = ctx->registry &&
                                (cbm_registry_lookup_type(ctx->registry, owned) ||
                                 cbm_registry_lookup_func(ctx->registry, owned));
            bool other_exists = ctx->registry &&
                                (cbm_registry_lookup_type(ctx->registry, other) ||
                                 cbm_registry_lookup_func(ctx->registry, other));
            if (owned_exists && other_exists && strcmp(owned, other) != 0)
                return NULL;
            if (other_exists)
                return other;
            return owned;
        }
    }

    if (ctx->glob_count > 0) {
        int sources = 0;
        const char *glob_head = rust_resolve_glob_module_item(ctx, head, &sources);
        if (sources > 1)
            return NULL;
        if (glob_head) {
            return cbm_arena_sprintf(ctx->arena, "%s.%s", glob_head,
                                     convert_path_to_qn(ctx->arena, tail));
        }
    }

    /* Cargo-manifest aware routing — when a Cargo.toml has been parsed
     * and the path head matches either a declared dependency or a
     * workspace member, return the canonical form `<head>.<tail>` so
     * the resolver doesn't pollute the module-prefix space. */
    if (ctx->cargo_manifest) {
        const CBMCargoManifest *m = (const CBMCargoManifest *)ctx->cargo_manifest;
        const CBMCargoMember *mem = cbm_cargo_find_member(m, head);
        if (mem) {
            /* Workspace member: route to its file-derived `<project>.<path>.src`
             * prefix so the registry's exact QNs can answer. */
            const char *member_prefix = strstr(tail, "::") ? rust_member_root_prefix(ctx, mem)
                                                            : rust_member_library_root_qn(ctx, mem);
            return cbm_arena_sprintf(ctx->arena, "%s.%s", member_prefix ? member_prefix : head,
                                     convert_path_to_qn(ctx->arena, tail));
        }
        if (cbm_cargo_is_known_dep(m, head)) {
            /* Declared dependency: same canonical form. The actual
             * methods may have been pre-seeded by rust_crates_seed.c;
             * otherwise the call is correctly attributed to an
             * external crate rather than fabricated locally. */
            return cbm_arena_sprintf(ctx->arena, "%s.%s", head,
                                     convert_path_to_qn(ctx->arena, tail));
        }
    }

    /* `Type::item` where the type arrives through exactly one glob import. */
    if (ctx->glob_count > 0 && ctx->module_qn && ctx->registry) {
        const char *own = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, head);
        if (!cbm_registry_lookup_type(ctx->registry, own)) {
            const CBMRegisteredType *glob_type = rust_resolve_glob_type(ctx, head);
            if (glob_type) {
                return cbm_arena_sprintf(ctx->arena, "%s.%s", glob_type->qualified_name,
                                         convert_path_to_qn(ctx->arena, tail));
            }
        }
    }

    /* Treat unknown-head paths as absolute: `std::io::Read` → `std.io.Read`.
     * Type parsing performs the narrower exact project-relative probe; doing
     * it here would reclassify `Type::method` as a direct free-function call
     * before the UFCS/constructor dispatch logic sees it. */
    return convert_path_to_qn(ctx->arena, path);
}

/* Resolve the module named by a `use path::*` glob. */
static const char *rust_resolve_module_path(RustLSPContext *ctx, const char *path) {
    if (!ctx || !path) {
        return NULL;
    }
    if (strcmp(path, "crate") == 0) {
        return rust_crate_root_prefix(ctx);
    }
    if (strcmp(path, "self") == 0) {
        return ctx->module_qn;
    }
    if (strcmp(path, "super") == 0) {
        return rust_super_module_qn(ctx);
    }
    return rust_resolve_path_expr(ctx, path);
}

/* ════════════════════════════════════════════════════════════════════
 * 4. Type-AST → CBMType
 * ════════════════════════════════════════════════════════════════════ */

/* Reconstruct the textual Rust path under a `scoped_type_identifier` /
 * `scoped_identifier` node. We deliberately walk the named children
 * rather than using the literal source text so we do not preserve
 * whitespace or trailing turbofish noise. */
static char *gather_scoped_path(RustLSPContext *ctx, TSNode node) {
    /* Fall back to the raw source text — the grammar already produces a
     * tight `path::to::name` literal under the node. */
    return rust_node_text(ctx, node);
}

static bool rust_original_offset(const RustLSPContext *ctx, TSNode node, uint32_t *out);

/* The type a path in a type position names, resolved through the crate's
 * declared module tree and `use` graph exactly as a call path is: the
 * declaring definition's QN, or NULL when the tree does not settle it on a
 * project type (generic parameter, item outside the project, undecidable). */
static const char *rust_type_qn_by_module_tree(RustLSPContext *ctx, TSNode node, const char *path) {
    const CBMRustModTree *tree = ctx->imports ? ctx->imports->modtree : NULL;
    uint32_t offset = 0;
    if (!tree || !ctx->module_qn || !path || ctx->glob_count > ctx->file_glob_count ||
        !rust_original_offset(ctx, node, &offset)) {
        return NULL;
    }
    int block_uses = ctx->use_count > ctx->file_use_count ? ctx->use_count - ctx->file_use_count : 0;
    CBMRustPathResult r =
        cbm_rust_modtree_resolve_type(tree, ctx->arena, ctx->module_qn, offset, path,
                                      block_uses ? ctx->use_local_names + ctx->file_use_count : NULL,
                                      block_uses ? ctx->use_raw_paths + ctx->file_use_count : NULL,
                                      block_uses);
    return r.kind == CBM_RUST_PATH_TYPE ? r.qn : NULL;
}

/* Resolve a textual Rust path (with `::`) into a registered type's QN, or
 * NULL if no match. */
static const char *resolve_path_to_type_qn(RustLSPContext *ctx, const char *path) {
    if (!ctx || !path || !path[0]) {
        return NULL;
    }
    if (is_rust_primitive(path)) {
        return NULL;
    }
    const char *qn = rust_resolve_path_expr(ctx, path);
    if (!qn) {
        return NULL;
    }
    if (cbm_registry_lookup_type(ctx->registry, qn)) {
        return qn;
    }
    const CBMRegisteredType *variant = rust_lookup_type_variants(ctx, qn);
    if (variant) {
        return variant->qualified_name;
    }
    const char *normalized = convert_path_to_qn(ctx->arena, path);
    if (!normalized || strcmp(qn, normalized) != 0) {
        return qn;
    }
    const char *relative = rust_registered_relative_path(ctx, normalized);
    if (relative) {
        return relative;
    }
    return qn; /* may not be registered yet but caller can still wrap as NAMED */
}

const CBMType *rust_parse_type_node(RustLSPContext *ctx, TSNode node) {
    if (ts_node_is_null(node)) {
        return cbm_type_unknown();
    }
    const char *kind = ts_node_type(node);

    /* primitive_type: i32, bool, char, str, … */
    if (strcmp(kind, "primitive_type") == 0) {
        char *name = rust_node_text(ctx, node);
        if (!name) {
            return cbm_type_unknown();
        }
        return cbm_type_builtin(ctx->arena, name);
    }

    /* type_identifier: simple named type */
    if (strcmp(kind, "type_identifier") == 0) {
        char *name = rust_node_text(ctx, node);
        if (!name) {
            return cbm_type_unknown();
        }
        if (is_rust_primitive(name)) {
            return cbm_type_builtin(ctx->arena, name);
        }
        if (strcmp(name, "Self") == 0 && ctx->self_type_qn) {
            return cbm_type_named(ctx->arena, ctx->self_type_qn);
        }
        const char *qn = rust_type_qn_by_module_tree(ctx, node, name);
        return cbm_type_named(ctx->arena, qn ? qn : rust_resolve_path_expr(ctx, name));
    }

    /* scoped_type_identifier: A::B::C */
    if (strcmp(kind, "scoped_type_identifier") == 0) {
        char *path = gather_scoped_path(ctx, node);
        if (!path) {
            return cbm_type_unknown();
        }
        const char *qn = rust_type_qn_by_module_tree(ctx, node, path);
        return cbm_type_named(ctx->arena, qn ? qn : resolve_path_to_type_qn(ctx, path));
    }

    /* reference_type: &T or &mut T */
    if (strcmp(kind, "reference_type") == 0) {
        TSNode inner = ts_node_child_by_field_name(node, "type", 4);
        if (ts_node_is_null(inner)) {
            uint32_t nc = ts_node_named_child_count(node);
            if (nc > 0) {
                inner = ts_node_named_child(node, nc - 1);
            }
        }
        const CBMType *elem = rust_parse_type_node(ctx, inner);
        return cbm_type_reference(ctx->arena, elem);
    }

    /* pointer_type: *const T / *mut T */
    if (strcmp(kind, "pointer_type") == 0) {
        TSNode inner = ts_node_child_by_field_name(node, "type", 4);
        if (ts_node_is_null(inner)) {
            uint32_t nc = ts_node_named_child_count(node);
            if (nc > 0) {
                inner = ts_node_named_child(node, nc - 1);
            }
        }
        return cbm_type_pointer(ctx->arena, rust_parse_type_node(ctx, inner));
    }

    /* array_type: [T; N] — treated as slice T */
    if (strcmp(kind, "array_type") == 0) {
        TSNode elem = ts_node_child_by_field_name(node, "element", 7);
        if (ts_node_is_null(elem)) {
            elem = ts_node_child_by_field_name(node, "type", 4);
        }
        if (ts_node_is_null(elem) && ts_node_named_child_count(node) > 0) {
            elem = ts_node_named_child(node, 0);
        }
        return cbm_type_slice(ctx->arena, rust_parse_type_node(ctx, elem));
    }

    /* slice_type: [T] */
    if (strcmp(kind, "slice_type") == 0) {
        TSNode elem = ts_node_child_by_field_name(node, "element", 7);
        if (ts_node_is_null(elem) && ts_node_named_child_count(node) > 0) {
            elem = ts_node_named_child(node, 0);
        }
        return cbm_type_slice(ctx->arena, rust_parse_type_node(ctx, elem));
    }

    /* tuple_type: (T1, T2, …) */
    if (strcmp(kind, "tuple_type") == 0) {
        const CBMType *elems[16];
        int count = 0;
        uint32_t nc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < nc && count < 16; i++) {
            elems[count++] = rust_parse_type_node(ctx, ts_node_named_child(node, i));
        }
        if (count == 0) {
            return cbm_type_builtin(ctx->arena, "()");
        }
        if (count == 1) {
            return elems[0];
        }
        return cbm_type_tuple(ctx->arena, elems, count);
    }

    /* unit_type: () */
    if (strcmp(kind, "unit_type") == 0) {
        return cbm_type_builtin(ctx->arena, "()");
    }

    /* never_type: ! */
    if (strcmp(kind, "never_type") == 0) {
        return cbm_type_builtin(ctx->arena, "!");
    }

    /* generic_type: Foo<T1, T2, …> */
    if (strcmp(kind, "generic_type") == 0) {
        TSNode tn = ts_node_child_by_field_name(node, "type", 4);
        if (ts_node_is_null(tn) && ts_node_named_child_count(node) > 0) {
            tn = ts_node_named_child(node, 0);
        }
        char *head = rust_node_text(ctx, tn);
        if (!head) {
            return cbm_type_unknown();
        }
        const char *head_qn = rust_type_qn_by_module_tree(ctx, tn, head);
        if (!head_qn) {
            head_qn = resolve_path_to_type_qn(ctx, head);
        }

        /* Gather type_arguments. */
        TSNode args = ts_node_child_by_field_name(node, "type_arguments", 14);
        const CBMType *targs[16];
        int targ_count = 0;
        if (!ts_node_is_null(args)) {
            uint32_t anc = ts_node_named_child_count(args);
            for (uint32_t i = 0; i < anc && targ_count < 15; i++) {
                TSNode tc = ts_node_named_child(args, i);
                const char *tk = ts_node_type(tc);
                /* Skip lifetime arguments — we ignore lifetimes entirely. */
                if (strcmp(tk, "lifetime") == 0) {
                    continue;
                }
                targs[targ_count++] = rust_parse_type_node(ctx, tc);
            }
        }
        if (targ_count > 0) {
            return cbm_type_template(ctx->arena, head_qn, targs, targ_count);
        }
        return cbm_type_named(ctx->arena, head_qn);
    }

    /* function_type: fn(T1, T2) -> R */
    if (strcmp(kind, "function_type") == 0) {
        return cbm_type_func(ctx->arena, NULL, NULL, NULL);
    }

    /* dynamic_type: dyn Trait — record as named on the trait QN */
    if (strcmp(kind, "dynamic_type") == 0) {
        TSNode inner = ts_node_child_by_field_name(node, "trait", 5);
        if (ts_node_is_null(inner) && ts_node_named_child_count(node) > 0) {
            inner = ts_node_named_child(node, 0);
        }
        return rust_parse_type_node(ctx, inner);
    }

    /* abstract_type: impl Trait — best-effort same as dyn Trait */
    if (strcmp(kind, "abstract_type") == 0) {
        TSNode inner = ts_node_child_by_field_name(node, "trait", 5);
        if (ts_node_is_null(inner) && ts_node_named_child_count(node) > 0) {
            inner = ts_node_named_child(node, 0);
        }
        return rust_parse_type_node(ctx, inner);
    }

    /* bounded_type: `A + Send + 'a` — the principal trait, the first bound
     * that is not a lifetime, `?Sized` or an auto trait. */
    if (strcmp(kind, "bounded_type") == 0) {
        uint32_t nc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode b = ts_node_named_child(node, i);
            char *btext = rust_node_text(ctx, b);
            if (!btext) {
                continue;
            }
            const char *bt = strncmp(btext, "dyn ", 4) == 0 ? btext + 4 : btext;
            if (!rust_bound_is_marker(bt, strlen(bt))) {
                return rust_parse_type_node(ctx, b);
            }
        }
        return cbm_type_unknown();
    }

    /* parenthesized_type or wrapped types */
    if (strcmp(kind, "parenthesized_type") == 0 && ts_node_named_child_count(node) > 0) {
        return rust_parse_type_node(ctx, ts_node_named_child(node, 0));
    }

    /* qualified_type: <T as Trait>::Item */
    if (strcmp(kind, "qualified_type") == 0) {
        return cbm_type_unknown();
    }

    return cbm_type_unknown();
}

/* Parse a textual Rust type (`Vec<String>`, `&mut Foo`, `Result<T, E>`)
 * into a CBMType. Used when we receive types as strings (return types of
 * extracted `CBMDefinition`s, cross-file `CBMRustLSPDef::return_types`,
 * stdlib seed entries).
 *
 * The parser is intentionally simple: it recognises the small surface
 * area that tree-sitter would produce in `rust_parse_type_node` but
 * without a parser. This is the same trade-off `cbm_rust_parse_return_type_text`
 * makes for Go. */
/* The type a path spelled in `module_qn`'s file names, through the crate
 * module tree: a project type's QN, or an outside item's path as the `use`
 * graph spells it (a bare prelude name takes its canonical path). NULL when
 * there is no tree or it does not settle the name. */
static const char *rust_text_type_qn_by_module_tree(const CBMRustModTree *tree, CBMArena *arena,
                                                    const char *module_qn, const char *path) {
    if (!tree || !module_qn) {
        return NULL;
    }
    CBMRustPathResult r = cbm_rust_modtree_resolve_type_in_file(tree, arena, module_qn, path);
    if (r.kind == CBM_RUST_PATH_TYPE) {
        return r.qn;
    }
    if (r.kind != CBM_RUST_PATH_NOT_FUNCTION) {
        return NULL;
    }
    return r.qn ? r.qn : rust_lookup_prelude(path);
}

/* Type text as written in source may span lines (`(\n    A,\n    B,\n)`).
 * Fold every whitespace run to one space and drop it at both ends, so an
 * element or argument is the bare type it spells. Clean text is returned
 * as is. */
static const char *rust_type_text_fold_space(CBMArena *arena, const char *text) {
    bool clean = !isspace((unsigned char)text[0]);
    size_t len = 0;
    for (const char *p = text; *p; p++, len++) {
        bool space = isspace((unsigned char)*p);
        if (space && (*p != ' ' || !p[1] || isspace((unsigned char)p[1]))) {
            clean = false;
        }
    }
    if (clean) {
        return text;
    }
    char *out = cbm_arena_alloc(arena, len + 1);
    if (!out) {
        return text;
    }
    size_t used = 0;
    for (const char *p = text; *p; p++) {
        if (!isspace((unsigned char)*p)) {
            out[used++] = *p;
        } else if (used > 0 && out[used - 1] != ' ') {
            out[used++] = ' ';
        }
    }
    if (used > 0 && out[used - 1] == ' ') {
        used--;
    }
    out[used] = '\0';
    return out;
}

/* A bound in a `+` list that adds no methods a call could reach: a
 * lifetime, a `?Sized` relaxation, or an auto trait. */
static bool rust_bound_is_marker(const char *bound, size_t len) {
    if (len == 0 || bound[0] == '\'' || bound[0] == '?') {
        return true;
    }
    static const char *const auto_traits[] = {"Send", "Sync", "Unpin", "UnwindSafe",
                                              "RefUnwindSafe", NULL};
    size_t start = len;
    while (start > 0 && bound[start - 1] != ':' && bound[start - 1] != ' ') {
        start--;
    }
    for (const char *const *p = auto_traits; *p; p++) {
        if (strlen(*p) == len - start && strncmp(bound + start, *p, len - start) == 0) {
            return true;
        }
    }
    return false;
}

/* The principal trait of a bound list spelled as text, `A + Send + 'a` ->
 * `A`, trimmed; `text` itself when it has no top-level `+`; "" when every
 * bound is a marker. A `->` inside an `Fn(..) -> R` bound is not a
 * closing bracket. */
static const char *rust_principal_bound_text(CBMArena *arena, const char *text) {
    const char *principal = NULL;
    size_t principal_len = 0;
    bool split = false;
    int depth = 0;
    const char *start = text;
    for (const char *q = text;; q++) {
        if (*q == '-' && q[1] == '>') {
            q++;
            continue;
        }
        if (*q == '<' || *q == '(' || *q == '[') {
            depth++;
        } else if (*q == '>' || *q == ')' || *q == ']') {
            depth--;
        }
        if (!(*q == '\0' || (*q == '+' && depth == 0))) {
            continue;
        }
        split = split || *q == '+';
        const char *b = start;
        const char *e = q;
        while (b < e && isspace((unsigned char)*b)) {
            b++;
        }
        while (e > b && isspace((unsigned char)e[-1])) {
            e--;
        }
        if (!principal && !rust_bound_is_marker(b, (size_t)(e - b))) {
            principal = b;
            principal_len = (size_t)(e - b);
        }
        if (*q == '\0') {
            break;
        }
        start = q + 1;
    }
    if (!split) {
        return text;
    }
    return principal ? cbm_arena_strndup(arena, principal, principal_len) : "";
}

/* The type an associated-type binding argument names: `X` in
 * `Output = X` (as in `Future<Output = X>`); `arg` itself otherwise. */
static const char *rust_assoc_binding_value(const char *arg) {
    const char *p = arg;
    if (!(isalpha((unsigned char)*p) || *p == '_')) {
        return arg;
    }
    while (isalnum((unsigned char)*p) || *p == '_') {
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p != '=' || p[1] == '=') {
        return arg;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    return p;
}

/* True when `t` names an item of the standard library crates. */
static bool rust_type_is_std_trait(const CBMType *t) {
    const char *qn = NULL;
    if (t && t->kind == CBM_TYPE_NAMED) {
        qn = t->data.named.qualified_name;
    } else if (t && t->kind == CBM_TYPE_TEMPLATE) {
        qn = t->data.template_type.template_name;
    }
    return qn && (strncmp(qn, "core.", 5) == 0 || strncmp(qn, "alloc.", 6) == 0 ||
                  strncmp(qn, "std.", 4) == 0);
}

/* Generic head QN when the module tree does not settle `head`: a path keeps
 * its segments as a dotted QN, a prelude type its canonical path. */
static const char *rust_unsettled_generic_head_qn(CBMArena *arena, const char *head) {
    if (strstr(head, "::")) {
        return convert_path_to_qn(arena, head);
    }
    static const char *const prelude_types[] = {"Box",      "Option",       "Result", "String",
                                                "Vec",      "Iterator",     "Future", "IntoIterator",
                                                NULL};
    for (const char *const *p = prelude_types; *p; p++) {
        if (strcmp(head, *p) == 0) {
            return rust_lookup_prelude(head);
        }
    }
    return head;
}

static const CBMType *parse_type_text_with_params(CBMArena *arena, const char *text,
                                                  const char *module_qn, const char **type_params,
                                                  const CBMRustModTree *tree) {
    if (!text || !text[0]) {
        return cbm_type_unknown();
    }
    text = rust_type_text_fold_space(arena, text);
    if (!text[0]) {
        return cbm_type_unknown();
    }

    /* HRTB: `for<'a, 'b> Fn(&'a T) -> R` — strip the higher-rank
     * binder. We don't reason about explicit lifetimes anywhere, so
     * dropping it leaves the rest of the type untouched. */
    if (strncmp(text, "for<", 4) == 0) {
        const char *p = text + 4;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '<')
                depth++;
            else if (*p == '>')
                depth--;
            p++;
        }
        while (*p == ' ')
            p++;
        text = p;
        if (!*text)
            return cbm_type_unknown();
    }

    /* Bare leading lifetime (e.g. `'a`) — accept and skip, treating
     * the rest of the text as the actual type. Rare outside of HRTBs
     * but cheap to handle. */
    if (text[0] == '\'' && (isalpha((unsigned char)text[1]) || text[1] == '_')) {
        const char *p = text + 1;
        while (*p && (isalnum((unsigned char)*p) || *p == '_'))
            p++;
        while (*p == ' ')
            p++;
        text = p;
        if (!*text)
            return cbm_type_unknown();
    }

    /* Opaque `impl Trait`: a method called on it resolves to the impl of the
     * concrete type it hides, which the text does not name, so a project
     * trait leaves it unknown. A standard-library trait (`Future`,
     * `Iterator`) keeps the value typed: its methods are outside the project
     * and its written associated types (`Output = X`, `Item = X`) are what
     * `.await` and iteration yield. */
    if (strncmp(text, "impl ", 5) == 0) {
        const CBMType *bound_t =
            parse_type_text_with_params(arena, text + 5, module_qn, type_params, tree);
        return rust_type_is_std_trait(bound_t) ? bound_t : cbm_type_unknown();
    }
    const char *bound = rust_principal_bound_text(arena, text);
    if (!*bound) {
        return cbm_type_unknown();
    }
    if (bound != text) {
        return parse_type_text_with_params(arena, bound, module_qn, type_params, tree);
    }
    if (strncmp(text, "dyn ", 4) == 0) {
        return parse_type_text_with_params(arena, text + 4, module_qn, type_params, tree);
    }

    /* Reference: &T or &'a T or &mut T or &'a mut T */
    if (text[0] == '&') {
        const char *p = text + 1;
        if (*p == '\'') {
            p++;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
                p++;
            }
        }
        while (*p == ' ') {
            p++;
        }
        if (strncmp(p, "mut ", 4) == 0) {
            p += 4;
        }
        const CBMType *elem = parse_type_text_with_params(arena, p, module_qn, type_params, tree);
        return cbm_type_reference(arena, elem);
    }

    /* Pointer: *const T / *mut T */
    if (text[0] == '*') {
        const char *p = text + 1;
        if (strncmp(p, "const ", 6) == 0) {
            p += 6;
        } else if (strncmp(p, "mut ", 4) == 0) {
            p += 4;
        }
        return cbm_type_pointer(arena,
                                parse_type_text_with_params(arena, p, module_qn, type_params, tree));
    }

    /* Slice: [T] */
    if (text[0] == '[' && text[strlen(text) - 1] == ']') {
        const char *p = text + 1;
        const char *end = text + strlen(text) - 1;
        size_t inner_len = (size_t)(end - p);
        char *inner = cbm_arena_strndup(arena, p, inner_len);
        /* Array form `[T; N]` — strip the count. */
        char *semi = strchr(inner, ';');
        if (semi) {
            *semi = '\0';
            /* Trim trailing whitespace. */
            char *q = semi - 1;
            while (q > inner && isspace((unsigned char)*q)) {
                *q-- = '\0';
            }
        }
        return cbm_type_slice(arena,
                              parse_type_text_with_params(arena, inner, module_qn, type_params, tree));
    }

    /* Unit / never */
    if (strcmp(text, "()") == 0) {
        return cbm_type_builtin(arena, "()");
    }
    if (strcmp(text, "!") == 0) {
        return cbm_type_builtin(arena, "!");
    }

    /* Tuple: (T1, T2, …) — only when not a single parenthesised type. */
    if (text[0] == '(' && text[strlen(text) - 1] == ')') {
        const char *p = text + 1;
        size_t inner_len = strlen(text) - 2;
        char *inner = cbm_arena_strndup(arena, p, inner_len);
        /* Detect comma at top level. */
        int depth = 0;
        bool has_comma = false;
        for (char *q = inner; *q; q++) {
            if (*q == '<' || *q == '(' || *q == '[')
                depth++;
            else if (*q == '>' || *q == ')' || *q == ']')
                depth--;
            else if (*q == ',' && depth == 0) {
                has_comma = true;
                break;
            }
        }
        if (!has_comma) {
            return parse_type_text_with_params(arena, inner, module_qn, type_params, tree);
        }
        /* Split by top-level commas. */
        const CBMType *elems[16];
        int count = 0;
        char *start = inner;
        depth = 0;
        for (char *q = inner;; q++) {
            if (*q == '<' || *q == '(' || *q == '[')
                depth++;
            else if (*q == '>' || *q == ')' || *q == ']')
                depth--;
            if ((*q == ',' && depth == 0) || *q == '\0') {
                char save = *q;
                *q = '\0';
                /* Trim. */
                while (*start == ' ')
                    start++;
                if (count < 15 && *start) {
                    elems[count++] =
                        parse_type_text_with_params(arena, start, module_qn, type_params, tree);
                }
                if (save == '\0')
                    break;
                start = q + 1;
            }
        }
        if (count == 0)
            return cbm_type_builtin(arena, "()");
        if (count == 1)
            return elems[0];
        return cbm_type_tuple(arena, elems, count);
    }

    /* Generic head: head<args> */
    const char *lt = strchr(text, '<');
    if (lt) {
        const char *gt = text + strlen(text) - 1;
        if (*gt == '>') {
            size_t head_len = (size_t)(lt - text);
            char *head = cbm_arena_strndup(arena, text, head_len);
            /* Recursive split of args by top-level commas. */
            const char *args = lt + 1;
            size_t args_len = (size_t)(gt - args);
            char *abuf = cbm_arena_strndup(arena, args, args_len);
            const CBMType *targs[16];
            int targ_count = 0;
            int depth = 0;
            char *start = abuf;
            for (char *q = abuf;; q++) {
                if (*q == '<' || *q == '(' || *q == '[')
                    depth++;
                else if (*q == '>' || *q == ')' || *q == ']')
                    depth--;
                if ((*q == ',' && depth == 0) || *q == '\0') {
                    char save = *q;
                    *q = '\0';
                    while (*start == ' ')
                        start++;
                    start = (char *)rust_assoc_binding_value(start);
                    /* Skip lifetime args. */
                    if (*start != '\'' && *start && targ_count < 15) {
                        targs[targ_count++] =
                            parse_type_text_with_params(arena, start, module_qn, type_params, tree);
                    }
                    if (save == '\0')
                        break;
                    start = q + 1;
                }
            }
            const char *head_qn = rust_text_type_qn_by_module_tree(tree, arena, module_qn, head);
            if (!head_qn) {
                head_qn = rust_unsettled_generic_head_qn(arena, head);
            }
            if (is_rust_primitive(head)) {
                /* Primitives don't take generics in practice except for str ref — pass through. */
                return cbm_type_builtin(arena, head);
            }
            /* Map a few well-known std type sugars. */
            return cbm_type_template(arena, head_qn, targs, targ_count);
        }
    }

    /* Bare identifier or path. */
    if (is_rust_primitive(text)) {
        return cbm_type_builtin(arena, text);
    }
    if (type_params) {
        for (int i = 0; type_params[i]; i++) {
            if (strcmp(text, type_params[i]) == 0) {
                return cbm_type_type_param(arena, text);
            }
        }
    }
    /* Self -> module-qualified placeholder caller will substitute. */
    if (strcmp(text, "Self") == 0) {
        return cbm_type_named(arena, "Self");
    }
    /* Has `::` → absolute path; treat dotted paths as already-qualified
     * QNs (cross-file callers pass module-qualified text directly). */
    if (!strchr(text, '.')) {
        const char *declared = rust_text_type_qn_by_module_tree(tree, arena, module_qn, text);
        if (declared) {
            return cbm_type_named(arena, declared);
        }
    }
    if (strstr(text, "::")) {
        return cbm_type_named(arena, convert_path_to_qn(arena, text));
    }
    if (strchr(text, '.')) {
        return cbm_type_named(arena, text);
    }
    return cbm_type_named(arena, cbm_arena_sprintf(arena, "%s.%s", module_qn, text));
}

/* A byte that can sit inside an identifier, keyword or lifetime. */
static bool rust_type_text_word_byte(unsigned char c) {
    return isalnum(c) || c == '_' || c == '\'' || c >= 0x80;
}

/* Skip one comment starting at `p` (`//` to end of line, or a nestable
 * block comment); returns `p` itself when no comment starts there. */
static const char *rust_type_text_skip_comment(const char *p) {
    if (p[0] != '/') {
        return p;
    }
    if (p[1] == '/') {
        while (*p && *p != '\n') {
            p++;
        }
        return p;
    }
    if (p[1] != '*') {
        return p;
    }
    int depth = 0;
    while (*p) {
        if (p[0] == '/' && p[1] == '*') {
            depth++;
            p += 2;
        } else if (p[0] == '*' && p[1] == '/') {
            p += 2;
            if (--depth == 0) {
                return p;
            }
        } else {
            p++;
        }
    }
    return p;
}

/* Type text as source spelled it may span lines (rustfmt breaks a long tuple
 * or generic argument list) and carry comments. The parser below splits on
 * punctuation and trims only spaces, so reduce the text to one spelling:
 * comments dropped, and a run of whitespace kept as one space only between
 * two word bytes (`&'a mut T`, `dyn Trait`), removed everywhere else. */
static const char *rust_type_text_canonical(CBMArena *arena, const char *text) {
    if (!text || !strpbrk(text, " \t\r\n\f\v/")) {
        return text;
    }
    size_t len = strlen(text);
    char *out = (char *)cbm_arena_alloc(arena, len + 1);
    if (!out) {
        return text;
    }
    size_t n = 0;
    bool gap = false;
    for (const char *p = text; *p;) {
        const char *after = rust_type_text_skip_comment(p);
        if (after != p) {
            gap = true;
            p = after;
            continue;
        }
        if (isspace((unsigned char)*p)) {
            gap = true;
            p++;
            continue;
        }
        if (gap && n > 0 && rust_type_text_word_byte((unsigned char)out[n - 1]) &&
            rust_type_text_word_byte((unsigned char)*p)) {
            out[n++] = ' ';
        }
        gap = false;
        out[n++] = *p++;
    }
    out[n] = '\0';
    return out;
}

/* Public-ish helper used by the cross-file path. */
static const CBMType *rust_parse_return_type_text(CBMArena *arena, const char *text,
                                                  const char *module_qn) {
    return parse_type_text_with_params(arena, rust_type_text_canonical(arena, text), module_qn,
                                       NULL, NULL);
}

/* A type spelled in the file of `module_qn`, its names resolved through the
 * crate module tree when one is given. */
static const CBMType *rust_parse_def_type_text(CBMArena *arena, const char *text,
                                               const char *module_qn, const CBMRustModTree *tree) {
    return parse_type_text_with_params(arena, rust_type_text_canonical(arena, text), module_qn,
                                       NULL, tree);
}

/* Make `rt` the alias `type Name<params..> = target;` spelled in the file of
 * `module_qn`: the target's type with each parameter as a TYPE_PARAM, read
 * through the module tree when one is given. */
static void rust_register_type_alias(CBMArena *arena, CBMRegisteredType *rt, const char *target,
                                     const char *const *params, int param_count,
                                     const char *module_qn, const CBMRustModTree *tree) {
    if (!target || !target[0]) {
        return;
    }
    const char **names = NULL;
    if (param_count > 0) {
        names = (const char **)cbm_arena_alloc(arena, (size_t)(param_count + 1) * sizeof(char *));
        if (!names) {
            return;
        }
        for (int i = 0; i < param_count; i++) {
            names[i] = params[i];
        }
        names[param_count] = NULL;
    }
    const CBMType *t = parse_type_text_with_params(
        arena, rust_type_text_canonical(arena, target), module_qn, names, tree);
    if (t && !cbm_type_is_unknown(t)) {
        rt->alias_type = t;
        rt->type_param_names = names;
    }
}

typedef struct {
    const char *module_qn;
    const CBMRustModTree *tree;
} RustSignatureParamParserContext;

static const CBMType *rust_signature_param_type_adapter(CBMArena *arena, const char *text,
                                                        void *parser_ctx) {
    const RustSignatureParamParserContext *ctx =
        (const RustSignatureParamParserContext *)parser_ctx;
    return rust_parse_def_type_text(arena, text, ctx ? ctx->module_qn : NULL,
                                    ctx ? ctx->tree : NULL);
}

/* ════════════════════════════════════════════════════════════════════
 * 5. Generic substitution
 * ════════════════════════════════════════════════════════════════════ */

/* Recursively substitute every `TYPE_PARAM` reference in `t` whose name
 * matches `params[i]` with `args[i]`. Preserves structure for composite
 * types. */
static const CBMType *rust_substitute_type(CBMArena *arena, const CBMType *t, const char **params,
                                           const CBMType **args) {
    if (!t || !params || !args) {
        return t;
    }
    switch (t->kind) {
    case CBM_TYPE_TYPE_PARAM:
        for (int i = 0; params[i]; i++) {
            if (strcmp(t->data.type_param.name, params[i]) == 0) {
                return args[i];
            }
        }
        return t;
    case CBM_TYPE_REFERENCE:
        return cbm_type_reference(
            arena, rust_substitute_type(arena, t->data.reference.elem, params, args));
    case CBM_TYPE_POINTER:
        return cbm_type_pointer(arena,
                                rust_substitute_type(arena, t->data.pointer.elem, params, args));
    case CBM_TYPE_SLICE:
        return cbm_type_slice(arena, rust_substitute_type(arena, t->data.slice.elem, params, args));
    case CBM_TYPE_TEMPLATE: {
        const CBMType *new_args[16];
        int n = 0;
        for (; n < t->data.template_type.arg_count && n < 16; n++) {
            new_args[n] =
                rust_substitute_type(arena, t->data.template_type.template_args[n], params, args);
        }
        return cbm_type_template(arena, t->data.template_type.template_name, new_args, n);
    }
    case CBM_TYPE_TUPLE: {
        const CBMType *new_elems[16];
        int n = 0;
        for (; n < t->data.tuple.count && n < 16; n++) {
            new_elems[n] = rust_substitute_type(arena, t->data.tuple.elems[n], params, args);
        }
        return cbm_type_tuple(arena, new_elems, n);
    }
    default:
        return t;
    }
}

/* Naive Hindley-Milner-style type unification. Walks `param_type`
 * structurally against `arg_type`; whenever a `TYPE_PARAM` is bound for
 * the first time, store the corresponding `arg_type`. Subsequent
 * conflicting bindings are ignored (best-effort). */
static void rust_unify_type(const CBMType *param_type, const CBMType *arg_type,
                            const char **type_param_names, const CBMType **inferred,
                            int param_count) {
    if (!param_type || !arg_type || cbm_type_is_unknown(arg_type)) {
        return;
    }
    if (param_type->kind == CBM_TYPE_TYPE_PARAM) {
        for (int i = 0; i < param_count; i++) {
            if (strcmp(param_type->data.type_param.name, type_param_names[i]) == 0) {
                if (!inferred[i]) {
                    inferred[i] = arg_type;
                }
                return;
            }
        }
        return;
    }
    if (param_type->kind == CBM_TYPE_REFERENCE && arg_type->kind == CBM_TYPE_REFERENCE) {
        rust_unify_type(param_type->data.reference.elem, arg_type->data.reference.elem,
                        type_param_names, inferred, param_count);
        return;
    }
    if (param_type->kind == CBM_TYPE_REFERENCE) {
        rust_unify_type(param_type->data.reference.elem, arg_type, type_param_names, inferred,
                        param_count);
        return;
    }
    if (param_type->kind == CBM_TYPE_SLICE && arg_type->kind == CBM_TYPE_SLICE) {
        rust_unify_type(param_type->data.slice.elem, arg_type->data.slice.elem, type_param_names,
                        inferred, param_count);
        return;
    }
    if (param_type->kind == CBM_TYPE_TEMPLATE && arg_type->kind == CBM_TYPE_TEMPLATE) {
        if (param_type->data.template_type.arg_count == arg_type->data.template_type.arg_count) {
            int ac = param_type->data.template_type.arg_count;
            for (int i = 0; i < ac; i++) {
                rust_unify_type(param_type->data.template_type.template_args[i],
                                arg_type->data.template_type.template_args[i], type_param_names,
                                inferred, param_count);
            }
        }
        return;
    }
    /* TUPLE unification — needed for tuple-return generics like
     * `fn pair<A, B>(a: A, b: B) -> (A, B)`. */
    if (param_type->kind == CBM_TYPE_TUPLE && arg_type->kind == CBM_TYPE_TUPLE) {
        int pc = param_type->data.tuple.count;
        int ac = arg_type->data.tuple.count;
        int min_ = pc < ac ? pc : ac;
        for (int i = 0; i < min_; i++) {
            rust_unify_type(param_type->data.tuple.elems[i], arg_type->data.tuple.elems[i],
                            type_param_names, inferred, param_count);
        }
        return;
    }
    /* POINTER unification. */
    if (param_type->kind == CBM_TYPE_POINTER && arg_type->kind == CBM_TYPE_POINTER) {
        rust_unify_type(param_type->data.pointer.elem, arg_type->data.pointer.elem,
                        type_param_names, inferred, param_count);
        return;
    }
    /* Bidirectional fallback: if `arg_type` (rather than `param_type`)
     * carries the type-param marker, swap and retry. This lets
     * `unify(known_concrete, fresh_var)` solve the var. */
    if (arg_type->kind == CBM_TYPE_TYPE_PARAM) {
        rust_unify_type(arg_type, param_type, type_param_names, inferred, param_count);
        return;
    }
}

/* Apply a solved type-param environment to a type, recursively
 * substituting bound param names with their concrete types. Returns
 * the substituted type (arena-allocated when new structure is built).
 *
 * This is the post-solve step of HM-lite: after `rust_unify_type` has
 * filled the `inferred` array, this helper walks a target type and
 * rewrites every `TYPE_PARAM` reference. */
static const CBMType *rust_apply_subst(CBMArena *arena, const CBMType *t, const char **names,
                                       const CBMType **inferred, int count) {
    if (!t)
        return t;
    switch (t->kind) {
    case CBM_TYPE_TYPE_PARAM:
        for (int i = 0; i < count; i++) {
            if (inferred[i] && names[i] && strcmp(t->data.type_param.name, names[i]) == 0) {
                return inferred[i];
            }
        }
        return t;
    case CBM_TYPE_REFERENCE:
        return cbm_type_reference(
            arena, rust_apply_subst(arena, t->data.reference.elem, names, inferred, count));
    case CBM_TYPE_POINTER:
        return cbm_type_pointer(
            arena, rust_apply_subst(arena, t->data.pointer.elem, names, inferred, count));
    case CBM_TYPE_SLICE:
        return cbm_type_slice(arena,
                              rust_apply_subst(arena, t->data.slice.elem, names, inferred, count));
    case CBM_TYPE_TEMPLATE: {
        int n = t->data.template_type.arg_count;
        if (n <= 0)
            return t;
        const CBMType *new_args[16];
        if (n > 16)
            n = 16;
        for (int i = 0; i < n; i++) {
            new_args[i] = rust_apply_subst(arena, t->data.template_type.template_args[i], names,
                                           inferred, count);
        }
        return cbm_type_template(arena, t->data.template_type.template_name, new_args, n);
    }
    case CBM_TYPE_TUPLE: {
        int n = t->data.tuple.count;
        if (n <= 0)
            return t;
        const CBMType *new_elems[16];
        if (n > 16)
            n = 16;
        for (int i = 0; i < n; i++) {
            new_elems[i] = rust_apply_subst(arena, t->data.tuple.elems[i], names, inferred, count);
        }
        return cbm_type_tuple(arena, new_elems, n);
    }
    default:
        return t;
    }
}

/* ════════════════════════════════════════════════════════════════════
 * 6. Expression evaluator
 * ════════════════════════════════════════════════════════════════════ */

/* Evaluate the type of a literal child like `integer_literal`, `float_literal`,
 * `string_literal`, `char_literal`, `boolean_literal`. */
static const CBMType *rust_eval_literal_type(RustLSPContext *ctx, const char *kind, TSNode node) {
    if (strcmp(kind, "integer_literal") == 0) {
        char *text = rust_node_text(ctx, node);
        /* Look at suffix. */
        if (text) {
            /* Strip leading `-` if any. */
            const char *p = text;
            if (*p == '-')
                p++;
            /* Find suffix start (first non-digit/non-_/non-x/non-X/non-hex). */
            while (*p && (isdigit((unsigned char)*p) || *p == '_' || *p == '.' || *p == 'x' ||
                          *p == 'X' || *p == 'b' || *p == 'B' || *p == 'o' || *p == 'O' ||
                          (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
                p++;
            }
            if (*p) {
                return cbm_type_builtin(ctx->arena, p);
            }
        }
        return cbm_type_builtin(ctx->arena, "i32");
    }
    if (strcmp(kind, "float_literal") == 0) {
        char *text = rust_node_text(ctx, node);
        if (text) {
            const char *p = text;
            while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' ||
                          *p == '+' || *p == '-' || *p == '_')) {
                p++;
            }
            if (*p) {
                return cbm_type_builtin(ctx->arena, p);
            }
        }
        return cbm_type_builtin(ctx->arena, "f64");
    }
    if (strcmp(kind, "string_literal") == 0 || strcmp(kind, "raw_string_literal") == 0) {
        /* &'static str — represented as &str */
        return cbm_type_reference(ctx->arena, cbm_type_builtin(ctx->arena, "str"));
    }
    if (strcmp(kind, "char_literal") == 0) {
        return cbm_type_builtin(ctx->arena, "char");
    }
    if (strcmp(kind, "boolean_literal") == 0 || strcmp(kind, "true") == 0 ||
        strcmp(kind, "false") == 0) {
        return cbm_type_builtin(ctx->arena, "bool");
    }
    return cbm_type_unknown();
}

/* Look up the registered method or field type for a field/method-style
 * access. Order: inherent method → field → trait method (with single-impl
 * preference). */
static const CBMType *rust_eval_member_access(RustLSPContext *ctx, const CBMType *recv,
                                              const char *member, bool method_call,
                                              const CBMRegisteredFunc **out_method);

/* `t` with every `Self` replaced by `self_qn`, through references, tuples
 * and generic arguments (`Result<Self>`, `Option<&Self>`). */
static const CBMType *rust_subst_self(RustLSPContext *ctx, const CBMType *t, const char *self_qn) {
    if (!t || !self_qn) {
        return t;
    }
    switch (t->kind) {
    case CBM_TYPE_NAMED:
        return strcmp(t->data.named.qualified_name, "Self") == 0 ? cbm_type_named(ctx->arena, self_qn)
                                                                 : t;
    case CBM_TYPE_REFERENCE:
        return cbm_type_reference(ctx->arena,
                                  rust_subst_self(ctx, t->data.reference.elem, self_qn));
    case CBM_TYPE_TEMPLATE: {
        int n = t->data.template_type.arg_count;
        if (n <= 0) {
            return t;
        }
        const CBMType **args = cbm_arena_alloc(ctx->arena, sizeof(*args) * (size_t)n);
        for (int i = 0; i < n; i++) {
            args[i] = rust_subst_self(ctx, t->data.template_type.template_args[i], self_qn);
        }
        return cbm_type_template(ctx->arena, t->data.template_type.template_name, args, n);
    }
    case CBM_TYPE_TUPLE: {
        int n = t->data.tuple.count;
        if (n <= 0) {
            return t;
        }
        const CBMType **elems = cbm_arena_alloc(ctx->arena, sizeof(*elems) * (size_t)n);
        for (int i = 0; i < n; i++) {
            elems[i] = rust_subst_self(ctx, t->data.tuple.elems[i], self_qn);
        }
        return cbm_type_tuple(ctx->arena, elems, n);
    }
    default:
        return t;
    }
}

/* Type a path call `f(..)` / `Type::f(..)` from the same target the call
 * step publishes for it. True when the module tree settles the path as a
 * function: `*out` is then the target's declared return type, unknown when
 * the target is not registered, and `*out_fn` the registered target or NULL.
 * False leaves the call to other evidence. */
static bool rust_eval_path_call_by_target(RustLSPContext *ctx, TSNode func_node,
                                          const CBMType **out,
                                          const CBMRegisteredFunc **out_fn) {
    TSNode callee = func_node;
    if (strcmp(ts_node_type(callee), "generic_function") == 0) {
        callee = ts_node_child_by_field_name(callee, "function", 8);
        if (ts_node_is_null(callee)) {
            return false;
        }
    }
    const char *ck = ts_node_type(callee);
    bool bare = strcmp(ck, "identifier") == 0;
    if (!bare && strcmp(ck, "scoped_identifier") != 0) {
        return false;
    }
    char *path = rust_node_text(ctx, callee);
    if (!path) {
        return false;
    }
    rust_strip_turbofish(path);
    if (bare && (cbm_scope_lookup_callable(ctx->current_scope, path) ||
                 cbm_scope_contains(ctx->current_scope, path))) {
        return false;
    }
    RustCallTarget target = {0};
    if (!rust_module_tree_target(ctx, callee, path, &target) || !target.names_function) {
        return false;
    }
    const CBMRegisteredFunc *f = target.func;
    *out = cbm_type_unknown();
    *out_fn = f;
    if (f && f->signature && f->signature->kind == CBM_TYPE_FUNC &&
        f->signature->data.func.return_types && f->signature->data.func.return_types[0]) {
        const char *self_qn = target.receiver_qn ? target.receiver_qn : f->receiver_type;
        *out = rust_subst_self(ctx, f->signature->data.func.return_types[0], self_qn);
    }
    return true;
}

enum { RUST_ALIAS_EXPAND_MAX_DEPTH = 8 };

/* `t` with every registered type alias replaced by the type it stands for,
 * the alias's written type arguments substituted for its parameters
 * (`type R<T> = Result<T, E>`: `R<Foo>` -> `Result<Foo, E>`). A parameter
 * the use leaves unwritten stays a type parameter. */
static const CBMType *rust_expand_type_aliases(RustLSPContext *ctx, const CBMType *t, int depth) {
    if (!t || depth > RUST_ALIAS_EXPAND_MAX_DEPTH) {
        return t;
    }
    switch (t->kind) {
    case CBM_TYPE_REFERENCE: {
        const CBMType *elem = rust_expand_type_aliases(ctx, t->data.reference.elem, depth);
        return elem == t->data.reference.elem ? t : cbm_type_reference(ctx->arena, elem);
    }
    case CBM_TYPE_NAMED:
    case CBM_TYPE_TEMPLATE: {
        bool named = t->kind == CBM_TYPE_NAMED;
        const char *qn = named ? t->data.named.qualified_name : t->data.template_type.template_name;
        const CBMRegisteredType *rt = qn ? cbm_registry_lookup_type(ctx->registry, qn) : NULL;
        int argc = named ? 0 : t->data.template_type.arg_count;
        if (rt && rt->alias_type) {
            const CBMType *target = rt->alias_type;
            if (rt->type_param_names && argc > 0) {
                int params = 0;
                while (rt->type_param_names[params]) {
                    params++;
                }
                int n = argc < params ? argc : params;
                const char **names = cbm_arena_alloc(ctx->arena, sizeof(char *) * (size_t)(n + 1));
                const CBMType **args =
                    cbm_arena_alloc(ctx->arena, sizeof(*args) * (size_t)(n + 1));
                for (int i = 0; i < n; i++) {
                    names[i] = rt->type_param_names[i];
                    args[i] = t->data.template_type.template_args[i];
                }
                names[n] = NULL;
                args[n] = NULL;
                target = rust_substitute_type(ctx->arena, target, names, args);
            }
            return rust_expand_type_aliases(ctx, target, depth + 1);
        }
        if (named || argc <= 0) {
            return t;
        }
        const CBMType **args = cbm_arena_alloc(ctx->arena, sizeof(*args) * (size_t)argc);
        bool changed = false;
        for (int i = 0; i < argc; i++) {
            const CBMType *arg = t->data.template_type.template_args[i];
            args[i] = rust_expand_type_aliases(ctx, arg, depth + 1);
            changed = changed || args[i] != arg;
        }
        return changed ? cbm_type_template(ctx->arena, qn, args, argc) : t;
    }
    default:
        return t;
    }
}

/* Std types whose methods' returns are declared in RUST_STD_RETURNS. */
typedef enum {
    RUST_STD_NONE,
    RUST_STD_OPTION,
    RUST_STD_RESULT,
    RUST_STD_ARC,
    RUST_STD_RC,
    RUST_STD_WEAK_SYNC,
    RUST_STD_WEAK_RC,
    RUST_STD_MUTEX,
    RUST_STD_RWLOCK,
    RUST_STD_REFCELL,
    RUST_STD_CELL,
    RUST_STD_PATHBUF,
    RUST_STD_PATH,
    RUST_STD_STRING,
    RUST_STD_VEC,
} RustStdFamily;

typedef struct {
    RustStdFamily family;
    const char *qn;
} RustStdFamilyQN;

/* Every path a std type is known under (re-exports name the same type).
 * `std.io.Result<T>` and `anyhow.Result<T>` are `Result<T, E>` with E fixed. */
static const RustStdFamilyQN RUST_STD_FAMILY_QNS[] = {
    {RUST_STD_OPTION, "core.option.Option"},  {RUST_STD_OPTION, "std.option.Option"},
    {RUST_STD_RESULT, "core.result.Result"},  {RUST_STD_RESULT, "std.result.Result"},
    {RUST_STD_RESULT, "std.io.Result"},       {RUST_STD_RESULT, "anyhow.Result"},
    {RUST_STD_RESULT, "std.sync.LockResult"}, {RUST_STD_RESULT, "std.sync.TryLockResult"},
    {RUST_STD_ARC, "alloc.sync.Arc"},         {RUST_STD_ARC, "std.sync.Arc"},
    {RUST_STD_RC, "alloc.rc.Rc"},             {RUST_STD_RC, "std.rc.Rc"},
    {RUST_STD_WEAK_SYNC, "alloc.sync.Weak"},  {RUST_STD_WEAK_SYNC, "std.sync.Weak"},
    {RUST_STD_WEAK_RC, "alloc.rc.Weak"},      {RUST_STD_WEAK_RC, "std.rc.Weak"},
    {RUST_STD_MUTEX, "std.sync.Mutex"},       {RUST_STD_RWLOCK, "std.sync.RwLock"},
    {RUST_STD_REFCELL, "core.cell.RefCell"},  {RUST_STD_REFCELL, "std.cell.RefCell"},
    {RUST_STD_CELL, "core.cell.Cell"},        {RUST_STD_CELL, "std.cell.Cell"},
    {RUST_STD_PATHBUF, "std.path.PathBuf"},   {RUST_STD_PATH, "std.path.Path"},
    {RUST_STD_STRING, "alloc.string.String"}, {RUST_STD_STRING, "std.string.String"},
    {RUST_STD_VEC, "alloc.vec.Vec"},          {RUST_STD_VEC, "std.vec.Vec"},
    {RUST_STD_NONE, NULL}};

/* A std method's declared return, written over the receiver: `Self` is the
 * receiver, `T` / `E` its first / second generic argument, `_` a type the
 * call does not fix, `~X` X with one reference removed, `&X` a reference,
 * `[X]` a slice, `()` unit; a lowercase name is a builtin, a dotted name a
 * std type. `assoc_only` marks an associated function taking the receiver
 * as its first argument (`Arc::downgrade(&a)`), never a method call. */
typedef struct {
    RustStdFamily family;
    const char *method;
    const char *ret;
    bool assoc_only;
} RustStdReturn;

#define RS_OPT(x) "core.option.Option<" x ">"
#define RS_RES(x, e) "core.result.Result<" x "," e ">"
#define RS_PATH "std.path.Path"
#define RS_PATHBUF "std.path.PathBuf"
#define RS_OSSTR "std.ffi.OsStr"

static const RustStdReturn RUST_STD_RETURNS[] = {
    /* Option<T> */
    {RUST_STD_OPTION, "unwrap", "T", false},
    {RUST_STD_OPTION, "expect", "T", false},
    {RUST_STD_OPTION, "unwrap_or", "T", false},
    {RUST_STD_OPTION, "unwrap_or_else", "T", false},
    {RUST_STD_OPTION, "unwrap_or_default", "T", false},
    {RUST_STD_OPTION, "unwrap_unchecked", "T", false},
    {RUST_STD_OPTION, "as_ref", RS_OPT("&T"), false},
    {RUST_STD_OPTION, "as_mut", RS_OPT("&T"), false},
    {RUST_STD_OPTION, "take", "Self", false},
    {RUST_STD_OPTION, "take_if", "Self", false},
    {RUST_STD_OPTION, "replace", "Self", false},
    {RUST_STD_OPTION, "clone", "Self", false},
    {RUST_STD_OPTION, "or", "Self", false},
    {RUST_STD_OPTION, "or_else", "Self", false},
    {RUST_STD_OPTION, "xor", "Self", false},
    {RUST_STD_OPTION, "filter", "Self", false},
    {RUST_STD_OPTION, "inspect", "Self", false},
    {RUST_STD_OPTION, "cloned", RS_OPT("~T"), false},
    {RUST_STD_OPTION, "copied", RS_OPT("~T"), false},
    {RUST_STD_OPTION, "ok_or", RS_RES("T", "_"), false},
    {RUST_STD_OPTION, "ok_or_else", RS_RES("T", "_"), false},
    {RUST_STD_OPTION, "is_some", "bool", false},
    {RUST_STD_OPTION, "is_none", "bool", false},
    {RUST_STD_OPTION, "is_some_and", "bool", false},
    {RUST_STD_OPTION, "is_none_or", "bool", false},
    {RUST_STD_OPTION, "insert", "&T", false},
    {RUST_STD_OPTION, "get_or_insert", "&T", false},
    {RUST_STD_OPTION, "get_or_insert_with", "&T", false},
    {RUST_STD_OPTION, "get_or_insert_default", "&T", false},
    /* Result<T, E> */
    {RUST_STD_RESULT, "unwrap", "T", false},
    {RUST_STD_RESULT, "expect", "T", false},
    {RUST_STD_RESULT, "unwrap_or", "T", false},
    {RUST_STD_RESULT, "unwrap_or_else", "T", false},
    {RUST_STD_RESULT, "unwrap_or_default", "T", false},
    {RUST_STD_RESULT, "unwrap_err", "E", false},
    {RUST_STD_RESULT, "expect_err", "E", false},
    {RUST_STD_RESULT, "ok", RS_OPT("T"), false},
    {RUST_STD_RESULT, "err", RS_OPT("E"), false},
    {RUST_STD_RESULT, "map_err", RS_RES("T", "_"), false},
    {RUST_STD_RESULT, "or", RS_RES("T", "_"), false},
    {RUST_STD_RESULT, "or_else", RS_RES("T", "_"), false},
    {RUST_STD_RESULT, "map", RS_RES("_", "E"), false},
    {RUST_STD_RESULT, "and", RS_RES("_", "E"), false},
    {RUST_STD_RESULT, "and_then", RS_RES("_", "E"), false},
    {RUST_STD_RESULT, "as_ref", RS_RES("&T", "&E"), false},
    {RUST_STD_RESULT, "as_mut", RS_RES("&T", "&E"), false},
    {RUST_STD_RESULT, "cloned", RS_RES("~T", "E"), false},
    {RUST_STD_RESULT, "copied", RS_RES("~T", "E"), false},
    {RUST_STD_RESULT, "clone", "Self", false},
    {RUST_STD_RESULT, "inspect", "Self", false},
    {RUST_STD_RESULT, "inspect_err", "Self", false},
    {RUST_STD_RESULT, "is_ok", "bool", false},
    {RUST_STD_RESULT, "is_err", "bool", false},
    {RUST_STD_RESULT, "is_ok_and", "bool", false},
    {RUST_STD_RESULT, "is_err_and", "bool", false},
    /* Arc<T> */
    {RUST_STD_ARC, "clone", "Self", false},
    {RUST_STD_ARC, "as_ref", "&T", false},
    {RUST_STD_ARC, "downgrade", "alloc.sync.Weak<T>", true},
    {RUST_STD_ARC, "strong_count", "usize", true},
    {RUST_STD_ARC, "weak_count", "usize", true},
    {RUST_STD_ARC, "ptr_eq", "bool", true},
    {RUST_STD_ARC, "try_unwrap", RS_RES("T", "Self"), true},
    {RUST_STD_ARC, "into_inner", RS_OPT("T"), true},
    {RUST_STD_ARC, "unwrap_or_clone", "T", true},
    {RUST_STD_ARC, "get_mut", RS_OPT("&T"), true},
    {RUST_STD_ARC, "make_mut", "&T", true},
    /* Rc<T> */
    {RUST_STD_RC, "clone", "Self", false},
    {RUST_STD_RC, "as_ref", "&T", false},
    {RUST_STD_RC, "downgrade", "alloc.rc.Weak<T>", true},
    {RUST_STD_RC, "strong_count", "usize", true},
    {RUST_STD_RC, "weak_count", "usize", true},
    {RUST_STD_RC, "ptr_eq", "bool", true},
    {RUST_STD_RC, "try_unwrap", RS_RES("T", "Self"), true},
    {RUST_STD_RC, "into_inner", RS_OPT("T"), true},
    {RUST_STD_RC, "unwrap_or_clone", "T", true},
    {RUST_STD_RC, "get_mut", RS_OPT("&T"), true},
    {RUST_STD_RC, "make_mut", "&T", true},
    /* Weak<T> */
    {RUST_STD_WEAK_SYNC, "clone", "Self", false},
    {RUST_STD_WEAK_SYNC, "upgrade", RS_OPT("alloc.sync.Arc<T>"), false},
    {RUST_STD_WEAK_SYNC, "strong_count", "usize", false},
    {RUST_STD_WEAK_SYNC, "weak_count", "usize", false},
    {RUST_STD_WEAK_SYNC, "ptr_eq", "bool", false},
    {RUST_STD_WEAK_RC, "clone", "Self", false},
    {RUST_STD_WEAK_RC, "upgrade", RS_OPT("alloc.rc.Rc<T>"), false},
    {RUST_STD_WEAK_RC, "strong_count", "usize", false},
    {RUST_STD_WEAK_RC, "weak_count", "usize", false},
    {RUST_STD_WEAK_RC, "ptr_eq", "bool", false},
    /* Mutex<T>: LockResult / TryLockResult are Result<Guard, _>. */
    {RUST_STD_MUTEX, "lock", RS_RES("std.sync.MutexGuard<T>", "_"), false},
    {RUST_STD_MUTEX, "try_lock", RS_RES("std.sync.MutexGuard<T>", "_"), false},
    {RUST_STD_MUTEX, "into_inner", RS_RES("T", "_"), false},
    {RUST_STD_MUTEX, "get_mut", RS_RES("&T", "_"), false},
    {RUST_STD_MUTEX, "is_poisoned", "bool", false},
    {RUST_STD_MUTEX, "clear_poison", "()", false},
    /* RwLock<T> */
    {RUST_STD_RWLOCK, "read", RS_RES("std.sync.RwLockReadGuard<T>", "_"), false},
    {RUST_STD_RWLOCK, "try_read", RS_RES("std.sync.RwLockReadGuard<T>", "_"), false},
    {RUST_STD_RWLOCK, "write", RS_RES("std.sync.RwLockWriteGuard<T>", "_"), false},
    {RUST_STD_RWLOCK, "try_write", RS_RES("std.sync.RwLockWriteGuard<T>", "_"), false},
    {RUST_STD_RWLOCK, "into_inner", RS_RES("T", "_"), false},
    {RUST_STD_RWLOCK, "get_mut", RS_RES("&T", "_"), false},
    {RUST_STD_RWLOCK, "is_poisoned", "bool", false},
    {RUST_STD_RWLOCK, "clear_poison", "()", false},
    /* RefCell<T> / Cell<T> */
    {RUST_STD_REFCELL, "borrow", "core.cell.Ref<T>", false},
    {RUST_STD_REFCELL, "borrow_mut", "core.cell.RefMut<T>", false},
    {RUST_STD_REFCELL, "try_borrow", RS_RES("core.cell.Ref<T>", "_"), false},
    {RUST_STD_REFCELL, "try_borrow_mut", RS_RES("core.cell.RefMut<T>", "_"), false},
    {RUST_STD_REFCELL, "into_inner", "T", false},
    {RUST_STD_REFCELL, "get_mut", "&T", false},
    {RUST_STD_REFCELL, "take", "T", false},
    {RUST_STD_REFCELL, "replace", "T", false},
    {RUST_STD_REFCELL, "replace_with", "T", false},
    {RUST_STD_REFCELL, "clone", "Self", false},
    {RUST_STD_CELL, "get", "T", false},
    {RUST_STD_CELL, "take", "T", false},
    {RUST_STD_CELL, "replace", "T", false},
    {RUST_STD_CELL, "into_inner", "T", false},
    {RUST_STD_CELL, "get_mut", "&T", false},
    {RUST_STD_CELL, "set", "()", false},
    /* PathBuf (its other methods are Path's, through Deref) */
    {RUST_STD_PATHBUF, "as_path", "&" RS_PATH, false},
    {RUST_STD_PATHBUF, "clone", "Self", false},
    {RUST_STD_PATHBUF, "to_owned", "Self", false},
    {RUST_STD_PATHBUF, "push", "()", false},
    {RUST_STD_PATHBUF, "pop", "bool", false},
    {RUST_STD_PATHBUF, "set_extension", "bool", false},
    {RUST_STD_PATHBUF, "set_file_name", "()", false},
    {RUST_STD_PATHBUF, "capacity", "usize", false},
    {RUST_STD_PATHBUF, "into_os_string", "std.ffi.OsString", false},
    /* Path */
    {RUST_STD_PATH, "join", RS_PATHBUF, false},
    {RUST_STD_PATH, "to_path_buf", RS_PATHBUF, false},
    {RUST_STD_PATH, "to_owned", RS_PATHBUF, false},
    {RUST_STD_PATH, "with_extension", RS_PATHBUF, false},
    {RUST_STD_PATH, "with_file_name", RS_PATHBUF, false},
    {RUST_STD_PATH, "parent", RS_OPT("&" RS_PATH), false},
    {RUST_STD_PATH, "file_name", RS_OPT("&" RS_OSSTR), false},
    {RUST_STD_PATH, "file_stem", RS_OPT("&" RS_OSSTR), false},
    {RUST_STD_PATH, "extension", RS_OPT("&" RS_OSSTR), false},
    {RUST_STD_PATH, "as_os_str", "&" RS_OSSTR, false},
    {RUST_STD_PATH, "to_str", RS_OPT("&str"), false},
    {RUST_STD_PATH, "strip_prefix", RS_RES("&" RS_PATH, "_"), false},
    {RUST_STD_PATH, "canonicalize", RS_RES(RS_PATHBUF, "_"), false},
    {RUST_STD_PATH, "read_link", RS_RES(RS_PATHBUF, "_"), false},
    {RUST_STD_PATH, "display", "std.path.Display", false},
    {RUST_STD_PATH, "exists", "bool", false},
    {RUST_STD_PATH, "is_file", "bool", false},
    {RUST_STD_PATH, "is_dir", "bool", false},
    {RUST_STD_PATH, "is_symlink", "bool", false},
    {RUST_STD_PATH, "is_absolute", "bool", false},
    {RUST_STD_PATH, "is_relative", "bool", false},
    {RUST_STD_PATH, "has_root", "bool", false},
    {RUST_STD_PATH, "starts_with", "bool", false},
    {RUST_STD_PATH, "ends_with", "bool", false},
    /* String */
    {RUST_STD_STRING, "clone", "Self", false},
    {RUST_STD_STRING, "to_owned", "Self", false},
    {RUST_STD_STRING, "to_string", "Self", false},
    {RUST_STD_STRING, "to_lowercase", "Self", false},
    {RUST_STD_STRING, "to_uppercase", "Self", false},
    {RUST_STD_STRING, "repeat", "Self", false},
    {RUST_STD_STRING, "replace", "Self", false},
    {RUST_STD_STRING, "split_off", "Self", false},
    {RUST_STD_STRING, "as_str", "&str", false},
    {RUST_STD_STRING, "as_mut_str", "&str", false},
    {RUST_STD_STRING, "trim", "&str", false},
    {RUST_STD_STRING, "trim_start", "&str", false},
    {RUST_STD_STRING, "trim_end", "&str", false},
    {RUST_STD_STRING, "as_bytes", "&[u8]", false},
    {RUST_STD_STRING, "into_bytes", "alloc.vec.Vec<u8>", false},
    {RUST_STD_STRING, "len", "usize", false},
    {RUST_STD_STRING, "capacity", "usize", false},
    {RUST_STD_STRING, "is_empty", "bool", false},
    {RUST_STD_STRING, "starts_with", "bool", false},
    {RUST_STD_STRING, "ends_with", "bool", false},
    {RUST_STD_STRING, "contains", "bool", false},
    {RUST_STD_STRING, "pop", RS_OPT("char"), false},
    {RUST_STD_STRING, "remove", "char", false},
    {RUST_STD_STRING, "push", "()", false},
    {RUST_STD_STRING, "push_str", "()", false},
    {RUST_STD_STRING, "insert", "()", false},
    {RUST_STD_STRING, "insert_str", "()", false},
    {RUST_STD_STRING, "clear", "()", false},
    {RUST_STD_STRING, "truncate", "()", false},
    /* Vec<T> (with the slice methods it reaches through Deref); the
     * iterators keep the element-typed Iterator the chains below read. */
    {RUST_STD_VEC, "clone", "Self", false},
    {RUST_STD_VEC, "to_vec", "Self", false},
    {RUST_STD_VEC, "split_off", "Self", false},
    {RUST_STD_VEC, "len", "usize", false},
    {RUST_STD_VEC, "capacity", "usize", false},
    {RUST_STD_VEC, "is_empty", "bool", false},
    {RUST_STD_VEC, "contains", "bool", false},
    {RUST_STD_VEC, "pop", RS_OPT("T"), false},
    {RUST_STD_VEC, "remove", "T", false},
    {RUST_STD_VEC, "swap_remove", "T", false},
    {RUST_STD_VEC, "first", RS_OPT("&T"), false},
    {RUST_STD_VEC, "last", RS_OPT("&T"), false},
    {RUST_STD_VEC, "first_mut", RS_OPT("&T"), false},
    {RUST_STD_VEC, "last_mut", RS_OPT("&T"), false},
    {RUST_STD_VEC, "as_slice", "&[T]", false},
    {RUST_STD_VEC, "as_mut_slice", "&[T]", false},
    {RUST_STD_VEC, "iter", "core.iter.Iterator<T>", false},
    {RUST_STD_VEC, "iter_mut", "core.iter.Iterator<T>", false},
    {RUST_STD_VEC, "into_iter", "core.iter.Iterator<T>", false},
    {RUST_STD_VEC, "drain", "core.iter.Iterator<T>", false},
    {RUST_STD_VEC, "push", "()", false},
    {RUST_STD_VEC, "insert", "()", false},
    {RUST_STD_VEC, "clear", "()", false},
    {RUST_STD_VEC, "truncate", "()", false},
    {RUST_STD_VEC, "extend", "()", false},
    {RUST_STD_VEC, "extend_from_slice", "()", false},
    {RUST_STD_VEC, "retain", "()", false},
    {RUST_STD_VEC, "dedup", "()", false},
    {RUST_STD_VEC, "reverse", "()", false},
    {RUST_STD_VEC, "sort", "()", false},
    {RUST_STD_VEC, "sort_by", "()", false},
    {RUST_STD_VEC, "sort_by_key", "()", false},
    {RUST_STD_VEC, "sort_unstable", "()", false},
    {RUST_STD_NONE, NULL, NULL, false}};

#undef RS_OPT
#undef RS_RES
#undef RS_PATH
#undef RS_PATHBUF
#undef RS_OSSTR

static RustStdFamily rust_std_family_of_qn(const char *qn) {
    if (!qn) {
        return RUST_STD_NONE;
    }
    for (const RustStdFamilyQN *e = RUST_STD_FAMILY_QNS; e->qn; e++) {
        if (strcmp(e->qn, qn) == 0) {
            return e->family;
        }
    }
    return RUST_STD_NONE;
}

/* A head whose last segment names a Result: `io::Result`, `fmt::Result`, a
 * dependency's `Result<T>` alias (T first, its error unknown), and
 * `LockResult<G>` / `TryLockResult<G>`, which are `Result<G, _>`. */
static bool rust_std_result_leaf(const char *qn) {
    static const char *const leaves[] = {"Result", "LockResult", "TryLockResult", NULL};
    const char *dot = strrchr(qn, '.');
    return rust_method_in(leaves, dot ? dot + 1 : qn);
}

/* The std type a written or recorded type head names, resolved like any
 * type name: the head itself, the registry, then for a bare name the
 * file's `use` and the prelude. A project type sharing the name is not std;
 * any other head whose last segment is a Result reads as core's Result. */
static RustStdFamily rust_std_family_of(RustLSPContext *ctx, const char *head) {
    RustStdFamily fam = rust_std_family_of_qn(head);
    if (fam != RUST_STD_NONE || !head) {
        return fam;
    }
    const char *qn = convert_path_to_qn(ctx->arena, head);
    fam = rust_std_family_of_qn(qn);
    if (fam != RUST_STD_NONE) {
        return fam;
    }
    const char *registered = rust_registered_type_qn(ctx, qn);
    if (registered) {
        return rust_std_family_of_qn(registered);
    }
    if (rust_std_result_leaf(qn)) {
        return RUST_STD_RESULT;
    }
    if (strchr(qn, '.')) {
        return RUST_STD_NONE;
    }
    const char *imported = rust_resolve_use(ctx, qn);
    if (imported) {
        return rust_std_family_of_qn(convert_path_to_qn(ctx->arena, imported));
    }
    return rust_std_family_of_qn(rust_lookup_prelude(qn));
}

static const RustStdReturn *rust_std_return_entry(RustStdFamily fam, const char *method,
                                                  bool method_call) {
    for (const RustStdReturn *e = RUST_STD_RETURNS; e->method; e++) {
        if (e->family == fam && strcmp(e->method, method) == 0 &&
            !(method_call && e->assoc_only)) {
            return e;
        }
    }
    return NULL;
}

enum { RUST_STD_SPEC_MAX_ARGS = 4, RUST_STD_SPEC_NAME_MAX = 64 };

/* One type of a RUST_STD_RETURNS spec at `*sp`, written over `self`. */
static const CBMType *rust_std_spec_parse(RustLSPContext *ctx, const char **sp,
                                          const CBMType *self) {
    const char *s = *sp;
    const CBMType *const *args =
        self->kind == CBM_TYPE_TEMPLATE ? self->data.template_type.template_args : NULL;
    int argc = self->kind == CBM_TYPE_TEMPLATE ? self->data.template_type.arg_count : 0;
    const CBMType *out = cbm_type_unknown();
    if (*s == '&' || *s == '~') {
        char op = *s;
        *sp = s + 1;
        const CBMType *inner = rust_std_spec_parse(ctx, sp, self);
        if (cbm_type_is_unknown(inner)) {
            return inner;
        }
        if (op == '&') {
            return cbm_type_reference(ctx->arena, inner);
        }
        return inner->kind == CBM_TYPE_REFERENCE ? inner->data.reference.elem
                                                 : cbm_type_unknown();
    }
    if (*s == '[') {
        *sp = s + 1;
        const CBMType *elem = rust_std_spec_parse(ctx, sp, self);
        *sp += **sp == ']' ? 1 : 0;
        return cbm_type_is_unknown(elem) ? elem : cbm_type_slice(ctx->arena, elem);
    }
    if (s[0] == '(' && s[1] == ')') {
        *sp = s + 2;
        return cbm_type_builtin(ctx->arena, "()");
    }
    char name[RUST_STD_SPEC_NAME_MAX];
    size_t n = 0;
    while ((isalnum((unsigned char)*s) || *s == '_' || *s == '.') && n + 1 < sizeof(name)) {
        name[n++] = *s++;
    }
    name[n] = '\0';
    const CBMType *targs[RUST_STD_SPEC_MAX_ARGS];
    int targc = 0;
    if (*s == '<') {
        s++;
        while (*s && *s != '>') {
            *sp = s;
            const CBMType *a = rust_std_spec_parse(ctx, sp, self);
            s = *sp;
            if (targc < RUST_STD_SPEC_MAX_ARGS) {
                targs[targc++] = a;
            }
            s += *s == ',' ? 1 : 0;
        }
        s += *s == '>' ? 1 : 0;
    }
    *sp = s;
    if (strcmp(name, "Self") == 0) {
        out = self;
    } else if (strcmp(name, "T") == 0) {
        out = argc > 0 && args[0] ? args[0] : cbm_type_unknown();
    } else if (strcmp(name, "E") == 0) {
        out = argc > 1 && args[1] ? args[1] : cbm_type_unknown();
    } else if (strcmp(name, "_") == 0 || n == 0) {
        out = cbm_type_unknown();
    } else if (!strchr(name, '.')) {
        out = cbm_type_builtin(ctx->arena, cbm_arena_strdup(ctx->arena, name));
    } else {
        const char *qn = cbm_arena_strdup(ctx->arena, name);
        out = targc > 0 ? cbm_type_template(ctx->arena, qn, targs, targc)
                        : cbm_type_named(ctx->arena, qn);
    }
    return out;
}

/* `entry`'s return for the receiver `self`; NULL when the spec reads unknown. */
static const CBMType *rust_std_return_type(RustLSPContext *ctx, const RustStdReturn *entry,
                                           const CBMType *self) {
    const char *sp = entry->ret;
    const CBMType *t = rust_std_spec_parse(ctx, &sp, self);
    return cbm_type_is_unknown(t) ? NULL : t;
}

static const char *rust_std_type_head(const CBMType *t) {
    if (t && t->kind == CBM_TYPE_TEMPLATE) {
        return t->data.template_type.template_name;
    }
    if (t && t->kind == CBM_TYPE_NAMED) {
        return t->data.named.qualified_name;
    }
    return NULL;
}

/* Lock and borrow guards implement only Deref, Drop, Debug and Display on
 * themselves: `guard.clone()` or `guard.as_ref()` reaches the target. */
static const char *const RUST_GUARD_TRAIT_METHODS[] = {"deref", "deref_mut", "drop",
                                                       "fmt",   "to_string", NULL};

static bool rust_std_is_guard(RustLSPContext *ctx, const char *head) {
    static const char *const guards[] = {"core.cell.Ref",          "std.cell.Ref",
                                         "core.cell.RefMut",       "std.cell.RefMut",
                                         "std.sync.MutexGuard",    "std.sync.RwLockReadGuard",
                                         "std.sync.RwLockWriteGuard", NULL};
    const char *qn = head ? rust_wrapper_head_qn(ctx, head) : NULL;
    return qn && rust_method_in(guards, qn);
}

static const CBMType *rust_strip_references(const CBMType *t) {
    while (t && t->kind == CBM_TYPE_REFERENCE) {
        t = t->data.reference.elem;
    }
    return t;
}

/* Declared return of `recv.method(..)` when method lookup settles on a std
 * method in RUST_STD_RETURNS: the receiver, then its Deref targets through
 * std wrappers, as autoderef probes them (PathBuf reaches Path's methods).
 * NULL leaves the call to the registry. A wrapper's own trait methods stop
 * the walk: rustc finds them on the wrapper, not its target, as it does a
 * `Borrow`/`BorrowMut` the file imports. */
static const CBMType *rust_std_method_return(RustLSPContext *ctx, const CBMType *recv,
                                             const char *method) {
    const CBMType *cur = rust_strip_references(recv);
    for (int hop = 0; cur && hop < RUST_DEREF_MAX_HOPS; hop++) {
        RustStdFamily fam = rust_std_family_of(ctx, rust_std_type_head(cur));
        const char *head = rust_std_type_head(cur);
        bool wrapper = head && rust_wrapper_head_derefs(ctx, head);
        if (fam == RUST_STD_NONE && !wrapper) {
            return NULL;
        }
        if (hop > 0 && fam == RUST_STD_REFCELL &&
            (rust_resolve_use(ctx, "Borrow") || rust_resolve_use(ctx, "BorrowMut"))) {
            return NULL;
        }
        const RustStdReturn *e =
            fam != RUST_STD_NONE ? rust_std_return_entry(fam, method, true) : NULL;
        if (!e && fam == RUST_STD_PATHBUF) {
            e = rust_std_return_entry(RUST_STD_PATH, method, true);
        }
        if (e) {
            return rust_std_return_type(ctx, e, cur);
        }
        bool own_trait_method = rust_std_is_guard(ctx, head)
                                    ? rust_method_in(RUST_GUARD_TRAIT_METHODS, method)
                                    : rust_is_wrapper_trait_method(method);
        if (!wrapper || own_trait_method ||
            rust_wrapper_has_corpus_trait_method(ctx, cur, method)) {
            return NULL;
        }
        cur = rust_strip_references(rust_deref_step(ctx, cur));
    }
    return NULL;
}

/* `Type::method(recv, ..)` for a std type in RUST_STD_RETURNS: the declared
 * return over the first argument's type, when that argument is the type
 * the path names (`Arc::clone(&a)`, `Arc::downgrade(&a)`). */
static const CBMType *rust_std_path_call_return(RustLSPContext *ctx, TSNode func_node,
                                                TSNode args_node) {
    if (strcmp(ts_node_type(func_node), "scoped_identifier") != 0 || ts_node_is_null(args_node) ||
        ts_node_named_child_count(args_node) == 0) {
        return NULL;
    }
    char *path = rust_node_text(ctx, func_node);
    if (!path) {
        return NULL;
    }
    rust_strip_turbofish(path);
    char *sep = strstr(path, "::");
    char *last = NULL;
    while (sep) {
        last = sep;
        sep = strstr(sep + 2, "::");
    }
    if (!last) {
        return NULL;
    }
    *last = '\0';
    RustStdFamily fam = rust_std_family_of(ctx, path);
    const RustStdReturn *e =
        fam != RUST_STD_NONE ? rust_std_return_entry(fam, last + 2, false) : NULL;
    if (!e) {
        return NULL;
    }
    const CBMType *recv =
        rust_strip_references(rust_eval_expr_type(ctx, ts_node_named_child(args_node, 0)));
    if (rust_std_family_of(ctx, rust_std_type_head(recv)) != fam) {
        return NULL;
    }
    return rust_std_return_type(ctx, e, recv);
}

static const CBMType *rust_eval_call_type_of(RustLSPContext *ctx, TSNode node,
                                             const CBMRegisteredFunc **callee);

/* Type of a call expression. `*callee`, when the call names a registered
 * function or method, is that function: `.await` reads its async flag. A
 * declared type alias in the result reads as the type it stands for. */
static const CBMType *rust_eval_call_type(RustLSPContext *ctx, TSNode node,
                                          const CBMRegisteredFunc **callee) {
    return rust_expand_type_aliases(ctx, rust_eval_call_type_of(ctx, node, callee), 0);
}

static const CBMType *rust_eval_call_type_of(RustLSPContext *ctx, TSNode node,
                                             const CBMRegisteredFunc **callee) {
    *callee = NULL;
    TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
    TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
    if (ts_node_is_null(func_node)) {
        return cbm_type_unknown();
    }
    const char *fk = ts_node_type(func_node);

    const CBMType *produced = rust_std_path_call_return(ctx, func_node, args_node);
    if (produced) {
        return produced;
    }
    if (rust_eval_path_call_by_target(ctx, func_node, &produced, callee)) {
        return produced;
    }

    /* Constructor of a tuple struct, unit-like struct, or stdlib
     * factory invoked via path. Try the registry; on miss, also try
     * UFCS-style method lookup to catch `String::new()`,
     * `Vec::new()`, etc. */
    if (strcmp(fk, "identifier") == 0 || strcmp(fk, "scoped_identifier") == 0) {
        char *path = rust_node_text(ctx, func_node);
        if (path) {
            /* Strip ALL turbofish (`::<...>`) so the lookup below
             * ignores explicit type arguments — handles forms like
             * `Vec::<i32>::new` and `parse::<u32>`. */
            rust_strip_turbofish(path);
            const char *qn = rust_resolve_path_expr(ctx, path);
            if (qn) {
                const CBMRegisteredType *rt = cbm_registry_lookup_type(ctx->registry, qn);
                if (rt) {
                    return cbm_type_named(ctx->arena, qn);
                }
                const CBMRegisteredFunc *f = cbm_registry_lookup_func(ctx->registry, qn);
                if (f && f->signature && f->signature->kind == CBM_TYPE_FUNC) {
                    const CBMType *const *rt_arr = f->signature->data.func.return_types;
                    if (rt_arr && rt_arr[0]) {
                        *callee = f;
                        const CBMType *ret = rt_arr[0];
                        /* Constructor-style on a stdlib receiver with
                         * `unknown` return — substitute the receiver
                         * type so chains keep typing. The heuristic
                         * matches `new`, `default`, anything starting
                         * with `from_` / `with_`, plus a small list of
                         * common factory verbs.
                         *
                         * For smart-pointer factories (`Box::new`,
                         * `Rc::new`, `Arc::new`, `RefCell::new`,
                         * `Pin::new`, `Mutex::new`, `RwLock::new`)
                         * we also try to capture the first call
                         * argument's type as a TEMPLATE arg so the
                         * Deref chain has something to peel. */
                        if (f->receiver_type && cbm_type_is_unknown(ret) && f->short_name &&
                            (strcmp(f->short_name, "new") == 0 ||
                             strcmp(f->short_name, "default") == 0 ||
                             strcmp(f->short_name, "open") == 0 ||
                             strcmp(f->short_name, "create") == 0 ||
                             strcmp(f->short_name, "create_new") == 0 ||
                             strcmp(f->short_name, "bind") == 0 ||
                             strcmp(f->short_name, "connect") == 0 ||
                             strcmp(f->short_name, "spawn") == 0 ||
                             strcmp(f->short_name, "now") == 0 ||
                             strncmp(f->short_name, "from_", 5) == 0 ||
                             strncmp(f->short_name, "with_", 5) == 0 ||
                             strcmp(f->short_name, "from") == 0)) {
                            /* Detect smart-pointer wrappers and
                             * capture the first arg's type so
                             * `let b = Box::new(x); b.method()`
                             * dispatches via Deref. */
                            bool is_wrapper = strcmp(f->short_name, "new") == 0 &&
                                              rust_type_derefs_to_first_arg(f->receiver_type);
                            if (is_wrapper && !ts_node_is_null(args_node)) {
                                uint32_t anc = ts_node_named_child_count(args_node);
                                if (anc > 0) {
                                    const CBMType *arg_t = rust_eval_expr_type(
                                        ctx, ts_node_named_child(args_node, 0));
                                    if (arg_t && !cbm_type_is_unknown(arg_t)) {
                                        return cbm_type_template(ctx->arena, f->receiver_type,
                                                                 &arg_t, 1);
                                    }
                                }
                            }
                            return cbm_type_named(ctx->arena, f->receiver_type);
                        }
                        /* Self -> receiver_type substitution. */
                        return rust_subst_self(ctx, ret, f->receiver_type);
                    }
                }
                /* UFCS path lookup: split off short name and try
                 * `cbm_registry_lookup_method`. */
                const char *dot = strrchr(qn, '.');
                if (dot) {
                    char *head = cbm_arena_strndup(ctx->arena, qn, (size_t)(dot - qn));
                    const char *short_name = dot + 1;
                    const CBMRegisteredFunc *m =
                        cbm_registry_lookup_method_aliased(ctx->registry, head, short_name);
                    if (!m && ctx->module_qn) {
                        const char *full_head =
                            cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, head);
                        m = cbm_registry_lookup_method_aliased(ctx->registry, full_head,
                                                               short_name);
                        if (m)
                            head = (char *)full_head;
                    }
                    if (!m) {
                        const CBMType *provided = rust_trait_assoc_fn_type(ctx, head, short_name);
                        if (provided) {
                            return provided;
                        }
                    }
                    if (m && m->signature && m->signature->kind == CBM_TYPE_FUNC &&
                        m->signature->data.func.return_types &&
                        m->signature->data.func.return_types[0]) {
                        const CBMType *ret = m->signature->data.func.return_types[0];
                        *callee = m;
                        /* Substitute Self / unknown returns with the
                         * receiver type so chained calls keep typing. */
                        if (!cbm_type_is_unknown(ret)) {
                            return rust_subst_self(ctx, ret, head);
                        }
                        if (cbm_type_is_unknown(ret) &&
                            (strcmp(short_name, "new") == 0 ||
                             strcmp(short_name, "default") == 0 ||
                             strcmp(short_name, "with_capacity") == 0 ||
                             strcmp(short_name, "from") == 0)) {
                            /* Smart-pointer wrap: capture first arg
                             * type into a TEMPLATE so Deref can peel. */
                            if (strcmp(short_name, "new") == 0 &&
                                rust_type_derefs_to_first_arg(head) &&
                                !ts_node_is_null(args_node)) {
                                uint32_t anc = ts_node_named_child_count(args_node);
                                if (anc > 0) {
                                    const CBMType *arg_t = rust_eval_expr_type(
                                        ctx, ts_node_named_child(args_node, 0));
                                    if (arg_t && !cbm_type_is_unknown(arg_t)) {
                                        return cbm_type_template(ctx->arena, head, &arg_t, 1);
                                    }
                                }
                            }
                            return cbm_type_named(ctx->arena, head);
                        }
                        return ret;
                    }
                }
            }
        }
    }

    /* Method call expressed as field_expression callee. */
    if (strcmp(fk, "field_expression") == 0) {
        TSNode value = ts_node_child_by_field_name(func_node, "value", 5);
        TSNode field = ts_node_child_by_field_name(func_node, "field", 5);
        if (!ts_node_is_null(value) && !ts_node_is_null(field)) {
            const CBMType *recv = rust_eval_expr_type(ctx, value);
            char *mname = rust_node_text(ctx, field);
            if (mname && recv) {
                const CBMType *base = recv;
                while (base && base->kind == CBM_TYPE_REFERENCE) {
                    base = base->data.reference.elem;
                }
                const CBMType *declared = rust_std_method_return(ctx, recv, mname);
                if (declared) {
                    return declared;
                }
                /* Template Vec<T> method handling */
                if (base && base->kind == CBM_TYPE_TEMPLATE) {
                    const char *tname = base->data.template_type.template_name;
                    /* Iterator-producing methods on Vec/&[T]/Option/Result/HashMap. */
                    if (strstr(tname, "Vec") || strstr(tname, "VecDeque") ||
                        strstr(tname, "HashSet") || strstr(tname, "BTreeSet")) {
                        if (strcmp(mname, "iter") == 0 || strcmp(mname, "iter_mut") == 0 ||
                            strcmp(mname, "into_iter") == 0 || strcmp(mname, "drain") == 0) {
                            /* Iterator<Item=T> — represent loosely as the elem type for our
                             * downstream chain calls. Even when the elem type is not
                             * known, return Iterator (with no args) so further chain calls
                             * can still attribute via Iterator's registered methods. */
                            if (base->data.template_type.arg_count > 0) {
                                return cbm_type_template(
                                    ctx->arena, "core.iter.Iterator",
                                    &base->data.template_type.template_args[0], 1);
                            }
                            return cbm_type_template(ctx->arena, "core.iter.Iterator", NULL, 0);
                        }
                        /* Methods returning the element type directly. */
                        if (strcmp(mname, "remove") == 0 || strcmp(mname, "swap_remove") == 0) {
                            if (base->data.template_type.arg_count > 0) {
                                return base->data.template_type.template_args[0];
                            }
                        }
                        if (strcmp(mname, "len") == 0 || strcmp(mname, "capacity") == 0) {
                            return cbm_type_builtin(ctx->arena, "usize");
                        }
                        if (strcmp(mname, "is_empty") == 0 || strcmp(mname, "contains") == 0) {
                            return cbm_type_builtin(ctx->arena, "bool");
                        }
                        if (strcmp(mname, "first") == 0 || strcmp(mname, "last") == 0 ||
                            strcmp(mname, "get") == 0 || strcmp(mname, "pop") == 0) {
                            if (base->data.template_type.arg_count > 0) {
                                const CBMType *opt_args[1] = {
                                    base->data.template_type.template_args[0]};
                                return cbm_type_template(ctx->arena, "core.option.Option",
                                                         opt_args, 1);
                            }
                        }
                        if (strcmp(mname, "as_slice") == 0) {
                            if (base->data.template_type.arg_count > 0) {
                                return cbm_type_reference(
                                    ctx->arena,
                                    cbm_type_slice(ctx->arena,
                                                   base->data.template_type.template_args[0]));
                            }
                        }
                    }
                    if (strstr(tname, "Iterator")) {
                        /* map/filter/take/skip/rev/chain → Iterator (with the relevant elem) */
                        if (strcmp(mname, "collect") == 0) {
                            /* Often Vec<Item>; without turbofish info we model as Vec<elem>. */
                            if (base->data.template_type.arg_count > 0) {
                                return cbm_type_template(ctx->arena, "alloc.vec.Vec",
                                                         base->data.template_type.template_args,
                                                         base->data.template_type.arg_count);
                            }
                        }
                        if (strcmp(mname, "count") == 0 || strcmp(mname, "len") == 0) {
                            return cbm_type_builtin(ctx->arena, "usize");
                        }
                        if (strcmp(mname, "next") == 0 || strcmp(mname, "last") == 0 ||
                            strcmp(mname, "nth") == 0 || strcmp(mname, "find") == 0 ||
                            strcmp(mname, "max") == 0 || strcmp(mname, "min") == 0 ||
                            strcmp(mname, "max_by") == 0 || strcmp(mname, "min_by") == 0) {
                            if (base->data.template_type.arg_count > 0) {
                                const CBMType *a0 = base->data.template_type.template_args[0];
                                return cbm_type_template(ctx->arena, "core.option.Option", &a0,
                                                         1);
                            }
                        }
                        if (strcmp(mname, "filter") == 0 || strcmp(mname, "take") == 0 ||
                            strcmp(mname, "skip") == 0 || strcmp(mname, "rev") == 0 ||
                            strcmp(mname, "cloned") == 0 || strcmp(mname, "copied") == 0 ||
                            strcmp(mname, "step_by") == 0 || strcmp(mname, "fuse") == 0 ||
                            strcmp(mname, "peekable") == 0 || strcmp(mname, "by_ref") == 0 ||
                            strcmp(mname, "take_while") == 0 ||
                            strcmp(mname, "skip_while") == 0 || strcmp(mname, "inspect") == 0) {
                            return base; /* preserves Iterator<Item> */
                        }
                        if ((strcmp(mname, "sum") == 0 || strcmp(mname, "product") == 0 ||
                             strcmp(mname, "fold") == 0 || strcmp(mname, "reduce") == 0) &&
                            base->data.template_type.arg_count > 0) {
                            return base->data.template_type.template_args[0];
                        }
                        if (strcmp(mname, "any") == 0 || strcmp(mname, "all") == 0) {
                            return cbm_type_builtin(ctx->arena, "bool");
                        }
                        if (strcmp(mname, "position") == 0) {
                            const CBMType *usize = cbm_type_builtin(ctx->arena, "usize");
                            return cbm_type_template(ctx->arena, "core.option.Option", &usize,
                                                     1);
                        }
                        if (strcmp(mname, "enumerate") == 0 &&
                            base->data.template_type.arg_count > 0) {
                            /* Iterator<(usize, T)>. */
                            const CBMType *pair[2] = {
                                cbm_type_builtin(ctx->arena, "usize"),
                                base->data.template_type.template_args[0]};
                            const CBMType *tup = cbm_type_tuple(ctx->arena, pair, 2);
                            return cbm_type_template(ctx->arena, "core.iter.Iterator", &tup, 1);
                        }
                    }
                    /* HashMap<K, V> / BTreeMap<K, V> generics. */
                    if (strstr(tname, "HashMap") || strstr(tname, "BTreeMap")) {
                        if (strcmp(mname, "len") == 0) {
                            return cbm_type_builtin(ctx->arena, "usize");
                        }
                        if (strcmp(mname, "is_empty") == 0 ||
                            strcmp(mname, "contains_key") == 0) {
                            return cbm_type_builtin(ctx->arena, "bool");
                        }
                        if (strcmp(mname, "get") == 0 || strcmp(mname, "get_mut") == 0 ||
                            strcmp(mname, "remove") == 0) {
                            if (base->data.template_type.arg_count > 1) {
                                const CBMType *v = base->data.template_type.template_args[1];
                                return cbm_type_template(ctx->arena, "core.option.Option", &v,
                                                         1);
                            }
                        }
                        if (strcmp(mname, "iter") == 0 || strcmp(mname, "iter_mut") == 0) {
                            /* Iterator<(K, V)>. */
                            if (base->data.template_type.arg_count > 1) {
                                const CBMType *pair[2] = {
                                    base->data.template_type.template_args[0],
                                    base->data.template_type.template_args[1]};
                                const CBMType *tup = cbm_type_tuple(ctx->arena, pair, 2);
                                return cbm_type_template(ctx->arena, "core.iter.Iterator", &tup,
                                                         1);
                            }
                        }
                        if (strcmp(mname, "keys") == 0 &&
                            base->data.template_type.arg_count > 0) {
                            return cbm_type_template(ctx->arena, "core.iter.Iterator",
                                                     &base->data.template_type.template_args[0],
                                                     1);
                        }
                        if (strcmp(mname, "values") == 0 &&
                            base->data.template_type.arg_count > 1) {
                            return cbm_type_template(ctx->arena, "core.iter.Iterator",
                                                     &base->data.template_type.template_args[1],
                                                     1);
                        }
                    }
                }
                /* Fall through: registered method on the named type.
                 * Unwrap the resulting FUNC signature to its first
                 * return type so chains like
                 * `String::new().to_uppercase().len()` keep typing
                 * across each link. */
                const CBMType *t = rust_eval_member_access(ctx, recv, mname, true, callee);
                if (t && t->kind == CBM_TYPE_FUNC && t->data.func.return_types &&
                    t->data.func.return_types[0]) {
                    const CBMType *ret = t->data.func.return_types[0];
                    /* Self -> receiver. */
                    if (ret->kind == CBM_TYPE_NAMED &&
                        strcmp(ret->data.named.qualified_name, "Self") == 0) {
                        const CBMType *rb = recv;
                        while (rb && rb->kind == CBM_TYPE_REFERENCE)
                            rb = rb->data.reference.elem;
                        if (rb && rb->kind == CBM_TYPE_NAMED) {
                            return cbm_type_named(ctx->arena, rb->data.named.qualified_name);
                        }
                    }
                    return ret;
                }
                return t;
            }
        }
    }

    /* Fallback: function expression's return type. */
    const CBMType *func_type = rust_eval_expr_type(ctx, func_node);
    if (func_type && func_type->kind == CBM_TYPE_FUNC && func_type->data.func.return_types &&
        func_type->data.func.return_types[0]) {
        return func_type->data.func.return_types[0];
    }
    if (func_type && func_type->kind == CBM_TYPE_NAMED) {
        return func_type;
    }
    return cbm_type_unknown();
}

const CBMType *rust_eval_expr_type(RustLSPContext *ctx, TSNode node) {
    if (ts_node_is_null(node)) {
        return cbm_type_unknown();
    }
    const char *kind = ts_node_type(node);

    /* Identifier: scope or registered symbol. */
    if (strcmp(kind, "identifier") == 0) {
        char *name = rust_node_text(ctx, node);
        if (!name) {
            return cbm_type_unknown();
        }
        const CBMType *t = cbm_scope_lookup(ctx->current_scope, name);
        if (!cbm_type_is_unknown(t)) {
            return t;
        }
        /* Module-level function. */
        const CBMRegisteredFunc *f =
            cbm_registry_lookup_symbol(ctx->registry, ctx->module_qn, name);
        if (f && f->signature) {
            return f->signature;
        }
        /* Use-aliased symbol: resolve path then look up. */
        const char *full = rust_resolve_use(ctx, name);
        if (full) {
            const char *qn = convert_path_to_qn(ctx->arena, full);
            const CBMRegisteredFunc *uf = rust_lookup_func_variants(ctx, qn);
            if (uf && uf->signature) {
                return uf->signature;
            }
            const CBMRegisteredType *ut = rust_lookup_type_variants(ctx, qn);
            if (ut) {
                return cbm_type_named(ctx->arena, ut->qualified_name);
            }
        }
        /* Same-module type? */
        const char *type_qn = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, name);
        if (cbm_registry_lookup_type(ctx->registry, type_qn)) {
            return cbm_type_named(ctx->arena, type_qn);
        }
        return cbm_type_unknown();
    }

    /* self_parameter token: bound in scope as `self`. */
    if (strcmp(kind, "self") == 0 || strcmp(kind, "self_parameter") == 0) {
        return cbm_scope_lookup(ctx->current_scope, "self");
    }

    /* scoped_identifier: A::B::C — could be a function or a type. */
    if (strcmp(kind, "scoped_identifier") == 0) {
        char *path = rust_node_text(ctx, node);
        if (!path) {
            return cbm_type_unknown();
        }
        const char *qn = rust_resolve_path_expr(ctx, path);
        if (!qn) {
            return cbm_type_unknown();
        }
        const CBMRegisteredFunc *f = rust_lookup_func_variants(ctx, qn);
        if (f && f->signature) {
            return f->signature;
        }
        const CBMRegisteredType *t = rust_lookup_type_variants(ctx, qn);
        if (t) {
            return cbm_type_named(ctx->arena, t->qualified_name);
        }
        return cbm_type_unknown();
    }

    /* generic_function: foo::<T> */
    if (strcmp(kind, "generic_function") == 0) {
        TSNode fn = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(fn)) {
            return rust_eval_expr_type(ctx, fn);
        }
    }

    /* field_expression: obj.field or obj.0 (tuple) */
    if (strcmp(kind, "field_expression") == 0) {
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        TSNode field = ts_node_child_by_field_name(node, "field", 5);
        if (ts_node_is_null(value) || ts_node_is_null(field)) {
            return cbm_type_unknown();
        }
        const CBMType *recv = rust_eval_expr_type(ctx, value);
        const char *fk = ts_node_type(field);
        if (strcmp(fk, "integer_literal") == 0) {
            /* Tuple field access. */
            if (recv) {
                const CBMType *base = recv;
                while (base && base->kind == CBM_TYPE_REFERENCE) {
                    base = base->data.reference.elem;
                }
                if (base && base->kind == CBM_TYPE_TUPLE) {
                    char *idx_text = rust_node_text(ctx, field);
                    int idx = atoi(idx_text);
                    if (idx >= 0 && idx < base->data.tuple.count) {
                        return base->data.tuple.elems[idx];
                    }
                }
            }
            return cbm_type_unknown();
        }
        char *fname = rust_node_text(ctx, field);
        if (!fname) {
            return cbm_type_unknown();
        }
        return rust_eval_member_access(ctx, recv, fname, false, NULL);
    }

    /* call_expression: any callable invocation. */
    if (strcmp(kind, "call_expression") == 0) {
        const CBMRegisteredFunc *callee = NULL;
        return rust_eval_call_type(ctx, node, &callee);
    }

    /* macro_invocation: vec!, format!, … */
    if (strcmp(kind, "macro_invocation") == 0) {
        TSNode mname = ts_node_child_by_field_name(node, "macro", 5);
        if (ts_node_is_null(mname) && ts_node_named_child_count(node) > 0) {
            mname = ts_node_named_child(node, 0);
        }
        char *name = rust_node_text(ctx, mname);
        if (!name) {
            return cbm_type_unknown();
        }
        if (strcmp(name, "vec") == 0) {
            /* vec![T] — peek first argument's type. */
            TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
            if (!ts_node_is_null(args)) {
                /* args is a token_tree; skim its named children for the first
                 * expression token. */
                uint32_t nc = ts_node_named_child_count(args);
                for (uint32_t i = 0; i < nc; i++) {
                    TSNode c = ts_node_named_child(args, i);
                    const CBMType *elem = rust_eval_expr_type(ctx, c);
                    if (elem && !cbm_type_is_unknown(elem)) {
                        return cbm_type_template(ctx->arena, "alloc.vec.Vec", &elem, 1);
                    }
                }
            }
            return cbm_type_template(ctx->arena, "alloc.vec.Vec", NULL, 0);
        }
        if (is_string_macro(name)) {
            return cbm_type_named(ctx->arena, "alloc.string.String");
        }
        if (is_void_macro(name)) {
            return cbm_type_builtin(ctx->arena, "()");
        }
        return cbm_type_unknown();
    }

    /* reference_expression: &x or &mut x */
    if (strcmp(kind, "reference_expression") == 0) {
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (ts_node_is_null(value) && ts_node_named_child_count(node) > 0) {
            value = ts_node_named_child(node, 0);
        }
        return cbm_type_reference(ctx->arena, rust_eval_expr_type(ctx, value));
    }

    /* unary_expression — *x dereferences, !x is bool, -x same as operand */
    if (strcmp(kind, "unary_expression") == 0) {
        TSNode operand = ts_node_named_child_count(node) > 0
                             ? ts_node_named_child(node, ts_node_named_child_count(node) - 1)
                             : (TSNode){0};
        char *op = NULL;
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode c = ts_node_child(node, i);
            if (!ts_node_is_named(c)) {
                op = rust_node_text(ctx, c);
                if (op)
                    break;
            }
        }
        const CBMType *inner =
            ts_node_is_null(operand) ? cbm_type_unknown() : rust_eval_expr_type(ctx, operand);
        if (op && strcmp(op, "*") == 0) {
            if (inner && inner->kind == CBM_TYPE_REFERENCE) {
                return inner->data.reference.elem;
            }
            if (inner && inner->kind == CBM_TYPE_POINTER) {
                return inner->data.pointer.elem;
            }
            return inner;
        }
        if (op && strcmp(op, "!") == 0) {
            return cbm_type_builtin(ctx->arena, "bool");
        }
        return inner;
    }

    /* binary_expression — comparisons → bool, logical → bool, arith → left */
    if (strcmp(kind, "binary_expression") == 0) {
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode c = ts_node_child(node, i);
            if (ts_node_is_named(c))
                continue;
            char *op = rust_node_text(ctx, c);
            if (!op)
                continue;
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
                strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
                return cbm_type_builtin(ctx->arena, "bool");
            }
            break;
        }
        if (!ts_node_is_null(left)) {
            return rust_eval_expr_type(ctx, left);
        }
        return cbm_type_unknown();
    }

    /* index_expression */
    if (strcmp(kind, "index_expression") == 0) {
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (ts_node_is_null(value) && ts_node_named_child_count(node) > 0) {
            value = ts_node_named_child(node, 0);
        }
        const CBMType *op_type = rust_eval_expr_type(ctx, value);
        const CBMType *base = op_type;
        while (base && base->kind == CBM_TYPE_REFERENCE)
            base = base->data.reference.elem;
        if (base && base->kind == CBM_TYPE_SLICE) {
            return base->data.slice.elem;
        }
        if (base && base->kind == CBM_TYPE_TEMPLATE) {
            const char *nm = base->data.template_type.template_name;
            if ((strstr(nm, "Vec") || strstr(nm, "VecDeque")) &&
                base->data.template_type.arg_count > 0) {
                return base->data.template_type.template_args[0];
            }
            if ((strstr(nm, "HashMap") || strstr(nm, "BTreeMap")) &&
                base->data.template_type.arg_count > 1) {
                return base->data.template_type.template_args[1];
            }
        }
        return cbm_type_unknown();
    }

    /* parenthesized_expression */
    if (strcmp(kind, "parenthesized_expression") == 0 && ts_node_named_child_count(node) > 0) {
        return rust_eval_expr_type(ctx, ts_node_named_child(node, 0));
    }

    /* try_expression: e? — peel one Result<T, _> / Option<T> layer. */
    if (strcmp(kind, "try_expression") == 0 && ts_node_named_child_count(node) > 0) {
        const CBMType *inner = rust_eval_expr_type(ctx, ts_node_named_child(node, 0));
        if (inner && inner->kind == CBM_TYPE_TEMPLATE) {
            const char *nm = inner->data.template_type.template_name;
            if ((strstr(nm, "Result") || strstr(nm, "Option")) &&
                inner->data.template_type.arg_count > 0) {
                return inner->data.template_type.template_args[0];
            }
        }
        return inner;
    }

    /* `e.await`. A call to an `async fn` is typed with the function's
     * declared output, which is what awaiting it yields. Any other awaited
     * value is a future type: its output is approximated by the first
     * generic argument, except Option<T>, which no future type spells. */
    if (strcmp(kind, "await_expression") == 0 && ts_node_named_child_count(node) > 0) {
        TSNode awaited = ts_node_named_child(node, 0);
        const CBMRegisteredFunc *callee = NULL;
        const CBMType *inner = strcmp(ts_node_type(awaited), "call_expression") == 0
                                   ? rust_eval_call_type(ctx, awaited, &callee)
                                   : rust_eval_expr_type(ctx, awaited);
        if (callee && (callee->flags & CBM_FUNC_FLAG_ASYNC) != 0) {
            return inner;
        }
        if (inner && inner->kind == CBM_TYPE_TEMPLATE) {
            const char *name = inner->data.template_type.template_name;
            if (name && strstr(name, "Option"))
                return inner;
            if (inner->data.template_type.arg_count > 0)
                return inner->data.template_type.template_args[0];
        }
        return inner;
    }

    /* type_cast_expression: x as T */
    if (strcmp(kind, "type_cast_expression") == 0) {
        TSNode tn = ts_node_child_by_field_name(node, "type", 4);
        if (!ts_node_is_null(tn)) {
            return rust_parse_type_node(ctx, tn);
        }
    }

    /* tuple_expression */
    if (strcmp(kind, "tuple_expression") == 0) {
        const CBMType *elems[16];
        int count = 0;
        uint32_t nc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < nc && count < 16; i++) {
            elems[count++] = rust_eval_expr_type(ctx, ts_node_named_child(node, i));
        }
        if (count == 0) {
            return cbm_type_builtin(ctx->arena, "()");
        }
        if (count == 1) {
            return elems[0];
        }
        return cbm_type_tuple(ctx->arena, elems, count);
    }

    /* array_expression: [a, b, c] */
    if (strcmp(kind, "array_expression") == 0) {
        if (ts_node_named_child_count(node) > 0) {
            const CBMType *elem = rust_eval_expr_type(ctx, ts_node_named_child(node, 0));
            return cbm_type_slice(ctx->arena, elem);
        }
        return cbm_type_unknown();
    }

    /* range_expression: a..b, a..=b, a.., ..b, .. — model as
     * Iterator<elem> so chains like `(0..n).map(...).count()` keep
     * typing. The element type is the type of the start/end expr. */
    if (strcmp(kind, "range_expression") == 0) {
        const CBMType *elem_type = NULL;
        uint32_t nc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode c = ts_node_named_child(node, i);
            const CBMType *t = rust_eval_expr_type(ctx, c);
            if (t && !cbm_type_is_unknown(t)) {
                elem_type = t;
                break;
            }
        }
        if (elem_type) {
            return cbm_type_template(ctx->arena, "core.iter.Iterator", &elem_type, 1);
        }
        return cbm_type_template(ctx->arena, "core.iter.Iterator", NULL, 0);
    }

    /* unit_expression */
    if (strcmp(kind, "unit_expression") == 0) {
        return cbm_type_builtin(ctx->arena, "()");
    }

    /* struct_expression: Foo { … } */
    if (strcmp(kind, "struct_expression") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (ts_node_is_null(name_node) && ts_node_named_child_count(node) > 0) {
            name_node = ts_node_named_child(node, 0);
        }
        if (!ts_node_is_null(name_node)) {
            char *path = rust_node_text(ctx, name_node);
            if (path) {
                return cbm_type_named(ctx->arena, resolve_path_to_type_qn(ctx, path));
            }
        }
        return cbm_type_unknown();
    }

    /* if_expression / match_expression / block / loop_expression — value of
     * the trailing expression in the consequence. */
    if (strcmp(kind, "if_expression") == 0) {
        TSNode cons = ts_node_child_by_field_name(node, "consequence", 11);
        if (!ts_node_is_null(cons)) {
            return rust_eval_expr_type(ctx, cons);
        }
    }
    if (strcmp(kind, "match_expression") == 0) {
        /* Take the type of the first arm's value if available. */
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body)) {
            uint32_t nc = ts_node_named_child_count(body);
            for (uint32_t i = 0; i < nc; i++) {
                TSNode arm = ts_node_named_child(body, i);
                if (strcmp(ts_node_type(arm), "match_arm") == 0) {
                    TSNode v = ts_node_child_by_field_name(arm, "value", 5);
                    if (!ts_node_is_null(v)) {
                        return rust_eval_expr_type(ctx, v);
                    }
                }
            }
        }
    }
    if (strcmp(kind, "block") == 0) {
        /* Find the last expression child (the trailing expression of the block). */
        uint32_t nc = ts_node_named_child_count(node);
        TSNode last = {0};
        bool found = false;
        for (uint32_t i = nc; i > 0; i--) {
            TSNode c = ts_node_named_child(node, i - 1);
            const char *ck = ts_node_type(c);
            if (strcmp(ck, "expression_statement") != 0 && strcmp(ck, "let_declaration") != 0 &&
                strcmp(ck, "empty_statement") != 0 && strcmp(ck, "line_comment") != 0 &&
                strcmp(ck, "block_comment") != 0) {
                last = c;
                found = true;
                break;
            }
        }
        if (found) {
            return rust_eval_expr_type(ctx, last);
        }
        return cbm_type_builtin(ctx->arena, "()");
    }
    if (strcmp(kind, "loop_expression") == 0) {
        return cbm_type_builtin(ctx->arena, "()");
    }

    /* closure_expression: |a, b| body — best effort. Return type comes from
     * the body's last expression if present. */
    if (strcmp(kind, "closure_expression") == 0) {
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (ts_node_is_null(body)) {
            return cbm_type_func(ctx->arena, NULL, NULL, NULL);
        }
        return cbm_type_func(ctx->arena, NULL, NULL, NULL);
    }

    /* Literals fall through. */
    if (strcmp(kind, "integer_literal") == 0 || strcmp(kind, "float_literal") == 0 ||
        strcmp(kind, "string_literal") == 0 || strcmp(kind, "raw_string_literal") == 0 ||
        strcmp(kind, "char_literal") == 0 || strcmp(kind, "boolean_literal") == 0 ||
        strcmp(kind, "true") == 0 || strcmp(kind, "false") == 0) {
        return rust_eval_literal_type(ctx, kind, node);
    }

    /* break / continue / return — expressions with no useful type for callers. */
    if (strcmp(kind, "return_expression") == 0 || strcmp(kind, "break_expression") == 0 ||
        strcmp(kind, "continue_expression") == 0 || strcmp(kind, "yield_expression") == 0) {
        return cbm_type_builtin(ctx->arena, "!");
    }

    return cbm_type_unknown();
}

/* Bidirectional wrapper. Evaluate `node` with an `expected` hint. The
 * hint is used post-synthesis: if synthesis returned an under-specified
 * type (unknown, or a TEMPLATE without args, or a NAMED that the hint
 * refines), we substitute the hint when it matches structurally. */
const CBMType *rust_eval_expr_typed(RustLSPContext *ctx, TSNode node, const CBMType *expected) {
    const CBMType *synth = rust_eval_expr_type(ctx, node);
    if (!expected)
        return synth;
    if (!synth || cbm_type_is_unknown(synth)) {
        return expected;
    }
    /* Template with same head and missing args -> use expected. */
    if (synth->kind == CBM_TYPE_TEMPLATE && expected->kind == CBM_TYPE_TEMPLATE &&
        synth->data.template_type.template_name && expected->data.template_type.template_name &&
        strcmp(synth->data.template_type.template_name,
               expected->data.template_type.template_name) == 0 &&
        synth->data.template_type.arg_count == 0 && expected->data.template_type.arg_count > 0) {
        return expected;
    }
    /* NAMED matches expected NAMED — no refinement needed. */
    if (synth->kind == CBM_TYPE_NAMED && expected->kind == CBM_TYPE_TEMPLATE &&
        synth->data.named.qualified_name && expected->data.template_type.template_name &&
        strcmp(synth->data.named.qualified_name, expected->data.template_type.template_name) == 0) {
        return expected; /* refine NAMED → TEMPLATE with args */
    }
    return synth;
}

/* `sig` with every `Self` in its first return type replaced by `self_qn`. */
static const CBMType *rust_sig_subst_self(RustLSPContext *ctx, const CBMType *sig,
                                          const char *self_qn) {
    if (!sig || sig->kind != CBM_TYPE_FUNC || !sig->data.func.return_types ||
        !sig->data.func.return_types[0]) {
        return sig;
    }
    const CBMType *ret = sig->data.func.return_types[0];
    const CBMType *sub = rust_subst_self(ctx, ret, self_qn);
    if (sub == ret) {
        return sig;
    }
    int n = 0;
    while (sig->data.func.return_types[n]) {
        n++;
    }
    const CBMType **rets = cbm_arena_alloc(ctx->arena, sizeof(*rets) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) {
        rets[i] = sig->data.func.return_types[i];
    }
    rets[0] = sub;
    rets[n] = NULL;
    return cbm_type_func(ctx->arena, sig->data.func.param_names, sig->data.func.param_types,
                         rets);
}

/* Member access type: returns the CBMType for `recv.field_or_method`.
 * Methods return their FUNC signature with `Self` replaced by the type that
 * declares them; fields return the field type. A member the receiver lacks
 * is looked up on its Deref target, hop by hop, as rustc's autoderef does; a
 * method call keeps a wrapper-level prelude trait method on the wrapper.
 * `*out_method`, when given, is the registered method found, else NULL. */
static const CBMType *rust_eval_member_access_at(RustLSPContext *ctx, const CBMType *recv,
                                                 const char *member, bool method_call, int hop,
                                                 const CBMRegisteredFunc **out_method);

static const CBMType *rust_eval_member_access(RustLSPContext *ctx, const CBMType *recv,
                                              const char *member, bool method_call,
                                              const CBMRegisteredFunc **out_method) {
    if (out_method) {
        *out_method = NULL;
    }
    return rust_eval_member_access_at(ctx, recv, member, method_call, 0, out_method);
}

static const CBMType *rust_eval_member_access_deref(RustLSPContext *ctx, const CBMType *base,
                                                    const char *member, bool method_call,
                                                    int hop,
                                                    const CBMRegisteredFunc **out_method) {
    if (hop >= RUST_DEREF_MAX_HOPS || (method_call && rust_is_wrapper_trait_method(member))) {
        return cbm_type_unknown();
    }
    const CBMType *next = rust_deref_step(ctx, base);
    if (!next) {
        return cbm_type_unknown();
    }
    return rust_eval_member_access_at(ctx, next, member, method_call, hop + 1, out_method);
}

static const CBMType *rust_eval_member_access_at(RustLSPContext *ctx, const CBMType *recv,
                                                 const char *member, bool method_call, int hop,
                                                 const CBMRegisteredFunc **out_method) {
    if (!recv || !member)
        return cbm_type_unknown();
    /* Auto-deref through references. */
    const CBMType *base = recv;
    while (base && (base->kind == CBM_TYPE_REFERENCE || base->kind == CBM_TYPE_POINTER)) {
        base = (base->kind == CBM_TYPE_REFERENCE) ? base->data.reference.elem
                                                  : base->data.pointer.elem;
    }
    if (!base)
        return cbm_type_unknown();

    const char *type_qn = NULL;
    const CBMType **template_args = NULL;
    int template_count = 0;
    const char **template_params = NULL;

    if (base->kind == CBM_TYPE_NAMED) {
        type_qn = base->data.named.qualified_name;
    } else if (base->kind == CBM_TYPE_TEMPLATE) {
        type_qn = base->data.template_type.template_name;
        template_args = (const CBMType **)base->data.template_type.template_args;
        template_count = base->data.template_type.arg_count;
    } else if (base->kind == CBM_TYPE_BUILTIN) {
        /* Map a few primitives into stdlib QNs so registered methods on
         * `str`/`String`/integers are findable. */
        const char *nm = base->data.builtin.name;
        if (strcmp(nm, "str") == 0)
            type_qn = "core.str";
        else if (strcmp(nm, "String") == 0)
            type_qn = "alloc.string.String";
        else
            type_qn = nm;
    } else if (base->kind == CBM_TYPE_SLICE) {
        type_qn = "core.slice";
    } else {
        return cbm_type_unknown();
    }
    {
        const char *registered = rust_registered_type_qn(ctx, type_qn);
        if (registered) {
            type_qn = registered;
        }
    }
    if (!type_qn)
        return cbm_type_unknown();

    /* Check inherent method first. */
    const CBMRegisteredFunc *method = rust_lookup_method(ctx, type_qn, member);
    if (method && method->signature) {
        if (out_method) {
            *out_method = method;
        }
        const CBMType *sig = method->signature;
        if (template_args && method->type_param_names) {
            template_params = method->type_param_names;
            sig = rust_substitute_type(ctx->arena, sig, template_params, template_args);
        }
        return rust_sig_subst_self(ctx, sig, type_qn);
    }

    /* Field on the type's RegisteredType. */
    const CBMType *ft = rust_lookup_field(ctx, type_qn, member, 0);
    if (ft) {
        if (template_args && template_params) {
            return rust_substitute_type(ctx->arena, ft, template_params, template_args);
        }
        return ft;
    }
    return rust_eval_member_access_deref(ctx, base, member, method_call, hop, out_method);
}

/* ════════════════════════════════════════════════════════════════════
 * 7. Method dispatch + field lookup
 * ════════════════════════════════════════════════════════════════════ */

static const CBMType *rust_lookup_field(RustLSPContext *ctx, const char *type_qn,
                                        const char *field_name, int depth) {
    if (!type_qn || !field_name || depth > CBM_LSP_MAX_LOOKUP_DEPTH)
        return NULL;
    const CBMRegisteredType *rt = cbm_registry_lookup_type(ctx->registry, type_qn);
    if (!rt)
        return NULL;
    if (rt->alias_of)
        return rust_lookup_field(ctx, rt->alias_of, field_name, depth + 1);
    if (rt->field_names) {
        for (int i = 0; rt->field_names[i]; i++) {
            if (strcmp(rt->field_names[i], field_name) == 0 && rt->field_types &&
                rt->field_types[i]) {
                return rt->field_types[i];
            }
        }
    }
    if (rt->embedded_types) {
        for (int i = 0; rt->embedded_types[i]; i++) {
            const CBMType *f = rust_lookup_field(ctx, rt->embedded_types[i], field_name, depth + 1);
            if (f)
                return f;
        }
    }
    return NULL;
}

/* Hardcoded Deref<Target=U> map for stdlib smart pointers + guards. The
 * format mirrors the de-facto rule: `<smart-pointer-QN-prefix>` →
 * `<inner-type position>` where inner is taken from the receiver's
 * template args. We don't store U literally; instead we let the call
 * site pass `template_args[idx]` as the new receiver type for retry. */
static bool rust_type_derefs_to_first_arg(const char *type_qn) {
    if (!type_qn)
        return false;
    static const char *derefable[] = {"alloc.boxed.Box",
                                      "std.boxed.Box",
                                      "alloc.rc.Rc",
                                      "std.rc.Rc",
                                      "alloc.sync.Arc",
                                      "std.sync.Arc",
                                      "core.cell.RefCell",
                                      "std.cell.RefCell",
                                      "core.cell.Cell",
                                      "std.cell.Cell",
                                      "core.cell.Ref",
                                      "std.cell.Ref",
                                      "core.cell.RefMut",
                                      "std.cell.RefMut",
                                      "std.sync.MutexGuard",
                                      "std.sync.RwLockReadGuard",
                                      "std.sync.RwLockWriteGuard",
                                      "core.pin.Pin",
                                      NULL};
    for (const char **p = derefable; *p; p++) {
        if (strcmp(*p, type_qn) == 0)
            return true;
    }
    return false;
}

/* One QN per std Deref wrapper (`std.sync.Arc` and `alloc.sync.Arc` are the
 * same type): the prelude's when it names one, else `qn`. NULL when `qn` is
 * not a wrapper. */
static const char *rust_std_wrapper_canonical(const char *qn) {
    if (!rust_type_derefs_to_first_arg(qn)) {
        return NULL;
    }
    const char *dot = strrchr(qn, '.');
    const char *prelude = rust_lookup_prelude(dot ? dot + 1 : qn);
    return prelude && rust_type_derefs_to_first_arg(prelude) ? prelude : qn;
}

/* A template head as written (`Arc`, `std::sync::Arc`, or a name some
 * signature recorded under its defining module) names a std Deref wrapper
 * once it is resolved like any other type name: through the file's `use`
 * map and the registry, then the prelude for a bare name nothing else
 * claims. A user type that shares the name resolves to itself. */
static const char *rust_wrapper_head_qn(RustLSPContext *ctx, const char *head) {
    if (rust_type_derefs_to_first_arg(head)) {
        return rust_std_wrapper_canonical(head);
    }
    const char *qn = convert_path_to_qn(ctx->arena, head);
    if (rust_type_derefs_to_first_arg(qn)) {
        return rust_std_wrapper_canonical(qn);
    }
    const char *registered = rust_registered_type_qn(ctx, qn);
    if (registered) {
        return rust_std_wrapper_canonical(registered);
    }
    return !strchr(qn, '.') && !rust_resolve_use(ctx, qn)
               ? rust_std_wrapper_canonical(rust_lookup_prelude(qn))
               : NULL;
}

static bool rust_wrapper_head_derefs(RustLSPContext *ctx, const char *head) {
    return rust_wrapper_head_qn(ctx, head) != NULL;
}

/* How a corpus trait impl's self type on a std wrapper relates to a receiver
 * typed as that wrapper (see rust_wrapper_corpus_trait_method). */
typedef enum {
    RUST_IMPL_SELF_UNDECIDED, /* a type parameter, alias or unknown type takes part */
    RUST_IMPL_SELF_SAME,      /* both spell one concrete type */
    RUST_IMPL_SELF_DIFFERS,   /* concrete types that cannot unify */
} RustImplSelfMatch;

/* A leaf of a type that names one concrete type: a builtin, or a registered
 * type that is no alias and no std wrapper left without its arguments. */
static bool rust_type_leaf_concrete(RustLSPContext *ctx, const CBMType *t) {
    if (t->kind == CBM_TYPE_BUILTIN) {
        return t->data.builtin.name != NULL;
    }
    if (t->kind != CBM_TYPE_NAMED || !t->data.named.qualified_name ||
        rust_type_derefs_to_first_arg(t->data.named.qualified_name)) {
        return false;
    }
    const CBMRegisteredType *rt = cbm_registry_lookup_type(ctx->registry,
                                                           t->data.named.qualified_name);
    return rt && !rt->alias_of;
}

/* Whether the impl self type `impl_self` (as written, in the impl's module) is
 * the receiver type `recv` (as typed at the call). Names compare by QN, so a
 * trait object compares by its trait; anything the comparison cannot ground
 * (an impl type parameter is an unregistered name) is undecided. */
static RustImplSelfMatch rust_impl_self_match(RustLSPContext *ctx, const CBMType *impl_self,
                                              const CBMType *recv) {
    if (!impl_self || !recv) {
        return RUST_IMPL_SELF_UNDECIDED;
    }
    if (impl_self->kind == CBM_TYPE_TEMPLATE && recv->kind == CBM_TYPE_TEMPLATE) {
        const char *a = impl_self->data.template_type.template_name;
        const char *b = recv->data.template_type.template_name;
        const char *ca = a ? rust_std_wrapper_canonical(a) : NULL;
        const char *cb = b ? rust_std_wrapper_canonical(b) : NULL;
        if (!ca || !cb) {
            return RUST_IMPL_SELF_UNDECIDED;
        }
        if (strcmp(ca, cb) != 0) {
            return RUST_IMPL_SELF_DIFFERS;
        }
        if (impl_self->data.template_type.arg_count != recv->data.template_type.arg_count) {
            return RUST_IMPL_SELF_UNDECIDED;
        }
        RustImplSelfMatch all = RUST_IMPL_SELF_SAME;
        for (int i = 0; i < recv->data.template_type.arg_count; i++) {
            RustImplSelfMatch m =
                rust_impl_self_match(ctx, impl_self->data.template_type.template_args[i],
                                     recv->data.template_type.template_args[i]);
            if (m == RUST_IMPL_SELF_DIFFERS) {
                return m;
            }
            if (m == RUST_IMPL_SELF_UNDECIDED) {
                all = m;
            }
        }
        return all;
    }
    if (impl_self->kind == CBM_TYPE_REFERENCE && recv->kind == CBM_TYPE_REFERENCE) {
        return rust_impl_self_match(ctx, impl_self->data.reference.elem, recv->data.reference.elem);
    }
    if (!rust_type_leaf_concrete(ctx, impl_self) || !rust_type_leaf_concrete(ctx, recv)) {
        return RUST_IMPL_SELF_UNDECIDED;
    }
    if (impl_self->kind != recv->kind) {
        return RUST_IMPL_SELF_DIFFERS;
    }
    const char *a = impl_self->kind == CBM_TYPE_NAMED ? impl_self->data.named.qualified_name
                                                      : impl_self->data.builtin.name;
    const char *b = recv->kind == CBM_TYPE_NAMED ? recv->data.named.qualified_name
                                                 : recv->data.builtin.name;
    return strcmp(a, b) == 0 ? RUST_IMPL_SELF_SAME : RUST_IMPL_SELF_DIFFERS;
}

/* The method a corpus trait impl on the std wrapper `base` (`impl Tr for
 * Arc<X>`) provides as `method`. rustc finds such a method on the wrapper
 * before it derefs to the target, when the impl applies to this wrapper and
 * its trait is in scope. Returns the one impl proven to apply; sets *blocked
 * when an impl may apply but that is not proven (impl type parameters, a
 * trait object whose marker bounds the type text drops, several impls), so
 * the caller links neither it nor the deref target. An impl whose self type
 * differs from `base`, or whose trait is out of scope, leaves the call to the
 * deref walk. */
static const CBMRegisteredFunc *rust_wrapper_corpus_trait_method(RustLSPContext *ctx,
                                                                 const CBMType *base,
                                                                 const char *method,
                                                                 bool *blocked) {
    *blocked = false;
    if (!base || base->kind != CBM_TYPE_TEMPLATE || !base->data.template_type.template_name) {
        return NULL;
    }
    const char *wrapper = rust_wrapper_head_qn(ctx, base->data.template_type.template_name);
    if (!wrapper) {
        return NULL;
    }
    const CBMRegisteredFunc *applies = NULL;
    for (const CBMTypeRegistry *reg = ctx->registry; reg; reg = reg->fallback) {
        CBMMethodIter it;
        cbm_registry_methods(reg, wrapper, method, &it);
        for (int index; (index = cbm_method_iter_next(&it)) >= 0;) {
            const CBMRegisteredFunc *candidate = &reg->funcs[index];
            if ((candidate->flags & CBM_FUNC_FLAG_RUST_TRAIT_IMPL) == 0 ||
                !candidate->impl_trait_qn ||
                rust_trait_out_of_scope(ctx, candidate->impl_trait_qn)) {
                continue;
            }
            RustImplSelfMatch m = rust_impl_self_match(ctx, candidate->rust_wrapper_impl_self, base);
            if (m == RUST_IMPL_SELF_DIFFERS) {
                continue;
            }
            bool exact = m == RUST_IMPL_SELF_SAME && candidate->impl_key &&
                         !strstr(candidate->impl_key, "dyn ");
            if (!exact || (applies && applies != candidate)) {
                *blocked = true;
                return NULL;
            }
            applies = candidate;
        }
    }
    return applies;
}

/* Whether rustc may find `method` on the std wrapper `base` itself through a
 * corpus trait impl, before any deref: one applies, or one may apply and that
 * is not settled (see rust_wrapper_corpus_trait_method). */
static bool rust_wrapper_has_corpus_trait_method(RustLSPContext *ctx, const CBMType *base,
                                                 const char *method) {
    bool blocked = false;
    return rust_wrapper_corpus_trait_method(ctx, base, method, &blocked) != NULL || blocked;
}

/* Returns the inner type after one Deref step. For NAMED types we check
 * for a registered `embedded_types` entry whose tail is "Target=<U>" or,
 * for stdlib smart pointers, peel the first template arg. Returns NULL
 * if no Deref relationship is known. */
static const CBMType *rust_deref_step(RustLSPContext *ctx, const CBMType *t) {
    if (!t)
        return NULL;
    /* Smart pointers: Box<T>, Rc<T>, Arc<T>, etc. The grammar gives us a
     * TEMPLATE; the inner type is the first template arg. */
    if (t->kind == CBM_TYPE_TEMPLATE && t->data.template_type.template_name) {
        if (rust_wrapper_head_derefs(ctx, t->data.template_type.template_name)) {
            if (t->data.template_type.arg_count > 0) {
                return t->data.template_type.template_args[0];
            }
            return NULL;
        }
    }
    /* NAMED smart-pointer (rare — only when the user wrote `Rc` without
     * type arg) — no inner to peel. */
    if (t->kind == CBM_TYPE_NAMED && t->data.named.qualified_name) {
        if (rust_type_derefs_to_first_arg(t->data.named.qualified_name)) {
            return NULL;
        }
        /* Project type with a registered Deref impl. We approximate this
         * by checking `embedded_types` for an entry of the form
         * `<TraitQN>:DerefTarget:<U>` — set up by `impl Deref for X`
         * post-processing. (Currently not produced by extract_defs; the
         * call still works for stdlib types.) */
        const CBMRegisteredType *rt =
            cbm_registry_lookup_type(ctx->registry, t->data.named.qualified_name);
        if (rt && rt->embedded_types) {
            for (int i = 0; rt->embedded_types[i]; i++) {
                const char *e = rt->embedded_types[i];
                static const char prefix[] = "DerefTarget:";
                if (strncmp(e, prefix, sizeof(prefix) - 1) == 0) {
                    return cbm_type_named(ctx->arena, e + sizeof(prefix) - 1);
                }
            }
        }
    }
    return NULL;
}

/* A private inherent method the caller cannot name: rustc skips it during
 * method probing and goes on to trait methods. Proven only by the module
 * tree; without one every method stays a candidate. */
static bool rust_method_hidden_from_caller(const RustLSPContext *ctx,
                                           const CBMRegisteredFunc *method) {
    if (!ctx || !method->rust_private_module || !ctx->module_qn ||
        strcmp(method->rust_private_module, ctx->module_qn) == 0) {
        return false;
    }
    const CBMRustModTree *tree = ctx->imports ? ctx->imports->modtree : NULL;
    return cbm_rust_modtree_file_outside(tree, ctx->module_qn, method->rust_private_module);
}

static const CBMRegisteredFunc *rust_registry_lookup_inherent_method(const RustLSPContext *ctx,
                                                                     const CBMTypeRegistry *reg,
                                                                     const char *receiver_qn,
                                                                     const char *method_name) {
    if (!reg || !receiver_qn || !method_name) {
        return NULL;
    }
    int best_index = -1;
    CBMMethodIter it;
    cbm_registry_methods(reg, receiver_qn, method_name, &it);
    for (int index; (index = cbm_method_iter_next(&it)) >= 0;) {
        const CBMRegisteredFunc *candidate = &reg->funcs[index];
        if ((candidate->flags & CBM_FUNC_FLAG_RUST_TRAIT_IMPL) == 0 &&
            !rust_method_hidden_from_caller(ctx, candidate) &&
            (best_index < 0 || index < best_index)) {
            best_index = index;
        }
    }
    if (best_index >= 0) {
        return &reg->funcs[best_index];
    }
    return reg->fallback ? rust_registry_lookup_inherent_method(ctx, reg->fallback, receiver_qn,
                                                                method_name)
                         : NULL;
}

static const CBMRegisteredFunc *rust_registry_lookup_inherent_method_aliased(
    const RustLSPContext *ctx, const CBMTypeRegistry *reg, const char *receiver_qn,
    const char *method_name) {
    const CBMRegisteredFunc *method =
        rust_registry_lookup_inherent_method(ctx, reg, receiver_qn, method_name);
    if (method) {
        return method;
    }
    const CBMRegisteredType *type = cbm_registry_lookup_type(reg, receiver_qn);
    for (int depth = 0; depth < 16 && type && type->alias_of; depth++) {
        method = rust_registry_lookup_inherent_method(ctx, reg, type->alias_of, method_name);
        if (method) {
            return method;
        }
        type = cbm_registry_lookup_type(reg, type->alias_of);
    }
    return NULL;
}

static const CBMRegisteredFunc *rust_registry_lookup_unique_method(
    const CBMTypeRegistry *reg, const char *receiver_qn, const char *method_name) {
    const CBMRegisteredFunc *only = NULL;
    CBMMethodIter it;
    cbm_registry_methods_chain(reg, receiver_qn, method_name, &it);
    for (int index; (index = cbm_method_iter_next(&it)) >= 0;) {
        if (only) {
            return NULL;
        }
        only = &it.reg->funcs[index];
    }
    return only;
}

const char *cbm_rust_impl_key(CBMArena *arena, const char *receiver_qn,
                              const char *receiver_spelling, const char *trait_spelling) {
    if (!arena || !receiver_qn || !receiver_spelling || !trait_spelling) {
        return NULL;
    }
    return cbm_arena_sprintf(arena, "%zu:%s%zu:%s%zu:%s", strlen(receiver_qn), receiver_qn,
                             strlen(receiver_spelling), receiver_spelling,
                             strlen(trait_spelling), trait_spelling);
}

const CBMRegisteredFunc *cbm_rust_registry_lookup_impl_method(
    const CBMTypeRegistry *reg, const char *receiver_qn, const char *impl_key,
    const char *method_name) {
    if (!reg || !receiver_qn || !impl_key || !method_name) {
        return NULL;
    }
    CBMMethodIter it;
    cbm_registry_methods(reg, receiver_qn, method_name, &it);
    for (int index; (index = cbm_method_iter_next(&it)) >= 0;) {
        const CBMRegisteredFunc *candidate = &reg->funcs[index];
        if ((candidate->flags & CBM_FUNC_FLAG_RUST_TRAIT_IMPL) != 0 && candidate->impl_key &&
            strcmp(candidate->impl_key, impl_key) == 0) {
            return candidate;
        }
    }
    return reg->fallback
               ? cbm_rust_registry_lookup_impl_method(reg->fallback, receiver_qn, impl_key,
                                                       method_name)
               : NULL;
}

/* Whether `name` is spelled as an identifier anywhere in this file's own
 * buffer (not a macro expansion being walked). Memoized per file: the answer
 * depends on that buffer alone. */
static bool rust_source_spells_identifier(RustLSPContext *ctx, const char *name) {
    int32_t memo = cbm_idxmemo_get(&ctx->spelled_names, name);
    if (memo >= 0) {
        return memo != 0;
    }
    size_t n = strlen(name);
    const char *src = ctx->root_source;
    const char *end = src + ctx->root_source_len;
    bool found = false;
    for (const char *p = src; n > 0 && !found && p + n <= end; p++) {
        p = memmem(p, (size_t)(end - p), name, n);
        if (!p) {
            break;
        }
        bool starts = p == src || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
        bool ends = p + n == end || !(isalnum((unsigned char)p[n]) || p[n] == '_');
        found = starts && ends;
    }
    cbm_idxmemo_put_if_absent(&ctx->spelled_names, ctx->arena, cbm_arena_strdup(ctx->arena, name),
                              found ? 1 : 0);
    return found;
}

/* A trait whose methods a call on a concrete type in this file provably cannot
 * reach: rustc considers a trait's methods only when the trait is in scope.
 * Proven when the trait is declared in another file, is no prelude name, its
 * name is spelled
 * nowhere in this file (so no `use`, `as` rename or `as _` import names it),
 * and every glob import in scope is of this file's own module (whose items
 * and imports are this file's). Without the proof the trait stays a
 * candidate. */
static bool rust_trait_out_of_scope(RustLSPContext *ctx, const char *trait_qn) {
    if (!ctx || !trait_qn || !ctx->module_qn || !ctx->root_source || ctx->root_source_len <= 0) {
        return false;
    }
    size_t module_len = strlen(ctx->module_qn);
    if (strncmp(trait_qn, ctx->module_qn, module_len) == 0 && trait_qn[module_len] == '.') {
        return false;
    }
    for (int i = 0; i < ctx->glob_count; i++) {
        if (!ctx->glob_module_qns[i] || strcmp(ctx->glob_module_qns[i], ctx->module_qn) != 0) {
            return false;
        }
    }
    const char *dot = strrchr(trait_qn, '.');
    const char *name = dot ? dot + 1 : trait_qn;
    /* A prelude name (`Clone`, `AsRef`) is in scope unspelled. */
    if (rust_lookup_prelude(name)) {
        return false;
    }
    return !rust_source_spells_identifier(ctx, name);
}

/* Find the trait-impl method for one concrete receiver. With trait_qn==NULL,
 * exactly one implemented trait may contribute this method; two distinct
 * traits are ambiguous and deliberately resolve to NULL. An impl on a std
 * wrapper with a recorded self type is no candidate here. With `scope`, an
 * impl of a trait out of scope there is no candidate (*out_filtered set). */
static const CBMRegisteredFunc *
rust_registry_lookup_trait_impl_method_scoped(RustLSPContext *scope, const CBMTypeRegistry *reg,
                                              const char *receiver_qn, const char *trait_qn,
                                              const char *method_name, bool *out_ambiguous,
                                              bool *out_filtered) {
    if (out_ambiguous) {
        *out_ambiguous = false;
    }
    if (!reg || !receiver_qn || !method_name) {
        return NULL;
    }
    int best_index = -1;
    const char *matched_impl = NULL;
    CBMMethodIter it;
    cbm_registry_methods(reg, receiver_qn, method_name, &it);
    for (int index; (index = cbm_method_iter_next(&it)) >= 0;) {
        const CBMRegisteredFunc *candidate = &reg->funcs[index];
        /* An impl on a std wrapper applies by the wrapper's arguments, which a
         * lookup by QN does not see: rust_wrapper_corpus_trait_method decides it. */
        if ((candidate->flags & CBM_FUNC_FLAG_RUST_TRAIT_IMPL) == 0 || !candidate->impl_trait_qn ||
            candidate->rust_wrapper_impl_self) {
            continue;
        }
        if (trait_qn && strcmp(candidate->impl_trait_qn, trait_qn) != 0) {
            continue;
        }
        if (scope && rust_trait_out_of_scope(scope, candidate->impl_trait_qn)) {
            if (out_filtered) {
                *out_filtered = true;
            }
            continue;
        }
        const char *candidate_impl = candidate->impl_key ? candidate->impl_key
                                                         : candidate->impl_trait_qn;
        if (!trait_qn && matched_impl && strcmp(matched_impl, candidate_impl) != 0) {
            if (out_ambiguous) {
                *out_ambiguous = true;
            }
            return NULL;
        }
        matched_impl = candidate_impl;
        if (best_index < 0 || index < best_index) {
            best_index = index;
        }
    }
    if (best_index >= 0) {
        return &reg->funcs[best_index];
    }
    return reg->fallback ? rust_registry_lookup_trait_impl_method_scoped(
                               scope, reg->fallback, receiver_qn, trait_qn, method_name,
                               out_ambiguous, out_filtered)
                         : NULL;
}

static const CBMRegisteredFunc *rust_registry_lookup_trait_impl_method(const CBMTypeRegistry *reg,
                                                                       const char *receiver_qn,
                                                                       const char *trait_qn,
                                                                       const char *method_name,
                                                                       bool *out_ambiguous) {
    return rust_registry_lookup_trait_impl_method_scoped(NULL, reg, receiver_qn, trait_qn,
                                                         method_name, out_ambiguous, NULL);
}

static const CBMRegisteredFunc *rust_lookup_method_depth(RustLSPContext *ctx, const char *type_qn,
                                                         const char *member_name, int depth) {
    if (!type_qn || !member_name)
        return NULL;
    if (depth > CBM_LSP_MAX_LOOKUP_DEPTH)
        return NULL;

    /* Direct inherent method lookup. */
    const CBMRegisteredFunc *f =
        rust_registry_lookup_inherent_method_aliased(ctx, ctx->registry, type_qn, member_name);
    if (f)
        return f;

    bool trait_ambiguous = false;
    f = rust_registry_lookup_trait_impl_method(ctx->registry, type_qn, NULL, member_name,
                                               &trait_ambiguous);
    if (f) {
        return f;
    }
    if (trait_ambiguous) {
        return NULL;
    }

    /* Follow alias / embedded chain. */
    const CBMRegisteredType *rt = cbm_registry_lookup_type(ctx->registry, type_qn);
    if (rt) {
        if (rt->alias_of) {
            f = rust_lookup_method_depth(ctx, rt->alias_of, member_name, depth + 1);
            if (f)
                return f;
        }
        if (rt->embedded_types) {
            for (int i = 0; rt->embedded_types[i]; i++) {
                /* Skip the synthetic DerefTarget marker — handled by the
                 * caller in the receiver-walk loop. */
                if (strncmp(rt->embedded_types[i], "DerefTarget:", 12) == 0)
                    continue;
                /* Skip bound markers used for type-param trait bound
                 * recording — those live under "Bound:<TraitQN>". */
                if (strncmp(rt->embedded_types[i], "Bound:", 6) == 0) {
                    /* Dispatch through the bound trait. */
                    f = rust_lookup_method_depth(ctx, rt->embedded_types[i] + 6, member_name,
                                                 depth + 1);
                    if (f)
                        return f;
                    continue;
                }
                f = rust_lookup_method_depth(ctx, rt->embedded_types[i], member_name, depth + 1);
                if (f)
                    return f;
            }
        }
    }
    return NULL;
}

const CBMRegisteredFunc *rust_lookup_method(RustLSPContext *ctx, const char *type_qn,
                                            const char *member_name) {
    return rust_lookup_method_depth(ctx, type_qn, member_name, 0);
}

/* Marks a trait's `embedded_types` entry as a supertrait QN. Kept apart
 * from impl entries, which name traits a type implements. */
#define RUST_SUPERTRAIT_PREFIX "Super:"
enum { RUST_SUPERTRAIT_PREFIX_LEN = sizeof(RUST_SUPERTRAIT_PREFIX) - 1 };

static const CBMRegisteredFunc *rust_lookup_method_in_trait_own(RustLSPContext *ctx,
                                                                const char *trait_qn,
                                                                const char *method_name,
                                                                bool allow_abstract);

/* `method_name` declared by `trait_qn` itself, else by the first of its
 * supertraits (declaration order, depth first) that declares it. */
static const CBMRegisteredFunc *rust_lookup_method_in_trait_depth(RustLSPContext *ctx,
                                                                  const char *trait_qn,
                                                                  const char *method_name,
                                                                  bool allow_abstract, int depth) {
    const CBMRegisteredFunc *f =
        rust_lookup_method_in_trait_own(ctx, trait_qn, method_name, allow_abstract);
    if (f || !ctx || !trait_qn || depth > CBM_LSP_MAX_LOOKUP_DEPTH) {
        return f;
    }
    for (const CBMTypeRegistry *r = ctx->registry; r; r = r->fallback) {
        const CBMRegisteredType *t = cbm_registry_lookup_type(r, trait_qn);
        for (int i = 0; t && t->is_interface && t->embedded_types && t->embedded_types[i]; i++) {
            const char *e = t->embedded_types[i];
            if (strncmp(e, RUST_SUPERTRAIT_PREFIX, RUST_SUPERTRAIT_PREFIX_LEN) != 0) {
                continue;
            }
            f = rust_lookup_method_in_trait_depth(ctx, e + RUST_SUPERTRAIT_PREFIX_LEN,
                                                  method_name, allow_abstract, depth + 1);
            if (f) {
                return f;
            }
        }
    }
    return NULL;
}

static const CBMRegisteredFunc *rust_lookup_method_in_trait(RustLSPContext *ctx,
                                                            const char *trait_qn,
                                                            const char *method_name,
                                                            bool allow_abstract) {
    return rust_lookup_method_in_trait_depth(ctx, trait_qn, method_name, allow_abstract, 0);
}

static const CBMRegisteredFunc *rust_lookup_method_in_trait_own(RustLSPContext *ctx,
                                                                const char *trait_qn,
                                                                const char *method_name,
                                                                bool allow_abstract) {
    if (!ctx || !trait_qn || !method_name)
        return NULL;
    const CBMTypeRegistry *reg = ctx->registry;
    int best_default = -1;
    int best_any = -1;
    CBMMethodIter it;
    cbm_registry_methods(reg, trait_qn, method_name, &it);
    for (int index; (index = cbm_method_iter_next(&it)) >= 0;) {
        const CBMRegisteredFunc *candidate = &reg->funcs[index];
        if (candidate->flags & CBM_FUNC_FLAG_RUST_TRAIT_IMPL) {
            continue;
        }
        if (best_any < 0 || index < best_any) {
            best_any = index;
        }
        if ((candidate->flags & CBM_FUNC_FLAG_RUST_ABSTRACT) == 0 &&
            (best_default < 0 || index < best_default)) {
            best_default = index;
        }
    }
    int selected = best_default >= 0 ? best_default : (allow_abstract ? best_any : -1);
    if (selected >= 0) {
        return &reg->funcs[selected];
    }
    if (reg->fallback) {
        RustLSPContext fallback_ctx = *ctx;
        fallback_ctx.registry = reg->fallback;
        return rust_lookup_method_in_trait_own(&fallback_ctx, trait_qn, method_name,
                                               allow_abstract);
    }
    return NULL;
}

/* Resolve an ordinary method call with Rust precedence: inherent first,
 * concrete trait override second, then an unambiguous trait default. */
static const CBMRegisteredFunc *rust_resolve_trait_method(RustLSPContext *ctx,
                                                          const char *receiver_type_qn,
                                                          const char *method_name,
                                                          int *out_impl_count,
                                                          bool *out_trait_dispatch) {
    if (out_impl_count)
        *out_impl_count = 0;
    if (out_trait_dispatch)
        *out_trait_dispatch = false;
    if (!ctx || !receiver_type_qn || !method_name)
        return NULL;
    const CBMTypeRegistry *reg = ctx->registry;

    const CBMRegisteredType *receiver_type = cbm_registry_lookup_type(reg, receiver_type_qn);
    if (receiver_type && receiver_type->is_interface) {
        const CBMRegisteredFunc *trait_method =
            rust_lookup_method_in_trait(ctx, receiver_type_qn, method_name, true);
        if (trait_method && out_impl_count) {
            *out_impl_count = 2; /* dynamic/bound trait dispatch */
        }
        if (trait_method && out_trait_dispatch) {
            *out_trait_dispatch = true;
        }
        return trait_method;
    }

    /* Ordinary concrete dispatch always prefers an inherent method.  Trait
     * implementations may share the exact same receiver/name/QN, so the
     * generic registry first-match lookup is not precise enough here. */
    const CBMRegisteredFunc *inh =
        rust_registry_lookup_inherent_method_aliased(ctx, reg, receiver_type_qn, method_name);
    if (inh) {
        bool trait_ambiguous = false;
        const CBMRegisteredFunc *trait_method = rust_registry_lookup_trait_impl_method(
            reg, receiver_type_qn, NULL, method_name, &trait_ambiguous);
        const CBMCargoMember *caller_member = rust_package_member(ctx, ctx->module_qn);
        const CBMCargoMember *receiver_member = rust_package_member(ctx, receiver_type_qn);
        if (trait_method && caller_member && receiver_member && caller_member != receiver_member) {
            if (out_impl_count)
                *out_impl_count = 2;
            return NULL;
        }
        if (out_impl_count)
            *out_impl_count = 1;
        return inh;
    }

    /* Negative memo (sealed registry only): this whole cascade — embedded-
     * impl walk + trait-default tail — reads nothing but the registry and the
     * two query strings, so under read_only a miss is a pure fact. Macro-
     * expanded kernel rust asks the same failing (receiver, method) question
     * thousands of times per file; without the memo each repeat re-paid the
     * full walk (~63 s per trait-heavy file). Only the full miss (0 impls,
     * no trait default) is memoized, preserving out_impl_count fidelity for
     * the ambiguous (>=2 impls) case. */
    bool nm_active = reg && reg->read_only;
    uint64_t nm_key = 0;
    if (nm_active) {
        nm_key = cbm_negmemo_key(1, receiver_type_qn, method_name);
        if (cbm_negmemo_contains(&ctx->neg_memo, nm_key)) {
            return NULL;
        }
    }

    bool trait_ambiguous = false;
    bool scope_filtered = false; /* a candidate was dropped for this file's scope */
    const CBMRegisteredFunc *impl_method = rust_registry_lookup_trait_impl_method_scoped(
        ctx, reg, receiver_type_qn, NULL, method_name, &trait_ambiguous, &scope_filtered);
    if (trait_ambiguous) {
        if (out_impl_count) {
            *out_impl_count = 2;
        }
        return NULL;
    }
    if (impl_method) {
        if (out_impl_count) {
            *out_impl_count = 1;
        }
        if (out_trait_dispatch) {
            *out_trait_dispatch = true;
        }
        return impl_method;
    }

    /* No concrete override. Resolve a default through the receiver's exact
     * type-level impl relations. Two traits contributing the same default
     * method are ambiguous without a source-level qualification/import proof. */
    const CBMRegisteredFunc *default_method = NULL;
    const char *default_trait = NULL;
    if (receiver_type && receiver_type->embedded_types) {
        for (int i = 0; receiver_type->embedded_types[i]; i++) {
            const char *trait_qn = receiver_type->embedded_types[i];
            if (strncmp(trait_qn, "DerefTarget:", 12) == 0 || strncmp(trait_qn, "Bound:", 6) == 0) {
                continue;
            }
            const CBMRegisteredType *trait_type = cbm_registry_lookup_type(reg, trait_qn);
            if (!trait_type || !trait_type->is_interface) {
                continue;
            }
            const CBMRegisteredFunc *candidate =
                rust_lookup_method_in_trait_own(ctx, trait_qn, method_name, false);
            if (!candidate) {
                continue;
            }
            if (rust_trait_out_of_scope(ctx, trait_qn)) {
                scope_filtered = true;
                continue;
            }
            if (default_trait && strcmp(default_trait, trait_qn) != 0) {
                if (out_impl_count) {
                    *out_impl_count = 2;
                }
                return NULL;
            }
            default_trait = trait_qn;
            default_method = candidate;
        }
    }
    if (default_method) {
        if (out_impl_count) {
            *out_impl_count = 1;
        }
        if (out_trait_dispatch) {
            *out_trait_dispatch = true;
        }
        return default_method;
    }
    /* A miss that depends on this scope's imports is not a registry fact. */
    if (nm_active && !scope_filtered) {
        cbm_negmemo_insert(&ctx->neg_memo, ctx->arena, nm_key);
    }
    return NULL;
}

// Find the sole concrete implementer of trait `trait_qn` that declares
// `method_name`, returning that impl's method (NULL if none or 2+), setting
// *out_n to the count (capped at 2). Used for `Trait::method` UFCS so it
// resolves to the concrete impl rather than the trait's own abstract method.
// Type-level impl links are canonical full QNs. Concrete lookup additionally
// requires the function's exact impl-trait provenance, so an inherent method
// with the same receiver/name can never satisfy this trait-specific path.
static const CBMRegisteredFunc *rust_find_sole_trait_impl(RustLSPContext *ctx, const char *trait_qn,
                                                          const char *method_name, int *out_n) {
    if (out_n)
        *out_n = 0;
    if (!ctx || !trait_qn || !method_name)
        return NULL;
    const CBMTypeRegistry *reg = ctx->registry;
    /* Negative memo (sealed registry only) — registry-pure cascade; only the
     * zero-implementer miss is memoized (out_n fidelity for the 2+ case). */
    bool nm_active = reg && reg->read_only;
    uint64_t nm_key = 0;
    if (nm_active) {
        nm_key = cbm_negmemo_key(2, trait_qn, method_name);
        if (cbm_negmemo_contains(&ctx->neg_memo, nm_key)) {
            return NULL;
        }
    }
    const CBMRegisteredFunc *first = NULL;
    const char *first_qn = NULL;
    int n = 0;
    const char *tdot = strrchr(trait_qn, '.');
    const char *tbare = tdot ? tdot + 1 : trait_qn;
    CBMTypeEmbedIter eit;
    cbm_registry_types_by_embedded_bare(reg, tbare, &eit);
    for (int ti; n < 2 && (ti = cbm_type_embed_iter_next(&eit)) >= 0;) {
        const CBMRegisteredType *t = &reg->types[ti];
        if (!t->embedded_types || !t->qualified_name)
            continue;
        bool impls = false;
        for (int j = 0; t->embedded_types[j]; j++) {
            const char *e = t->embedded_types[j];
            if (strcmp(e, trait_qn) == 0) {
                impls = true;
                break;
            }
        }
        if (!impls)
            continue;
        const CBMRegisteredFunc *mf = rust_registry_lookup_trait_impl_method(
            reg, t->qualified_name, trait_qn, method_name, NULL);
        if (!first_qn) {
            first = mf;
            first_qn = t->qualified_name;
            n = 1;
        } else if (strcmp(first_qn, t->qualified_name) != 0) {
            n = 2;
        }
    }
    if (out_n)
        *out_n = n;
    if (nm_active && n == 0) {
        cbm_negmemo_insert(&ctx->neg_memo, ctx->arena, nm_key);
    }
    return n == 1 ? first : NULL;
}

/* True when `t` names `Self` anywhere: such a return is only known once
 * `Self` is substituted, which is done for a bare `Self` return alone. */
static bool rust_type_mentions_self(const CBMType *t, int depth) {
    if (!t || depth > CBM_LSP_MAX_LOOKUP_DEPTH) {
        return false;
    }
    switch (t->kind) {
    case CBM_TYPE_NAMED:
        return strcmp(t->data.named.qualified_name, "Self") == 0 ||
               strncmp(t->data.named.qualified_name, "Self.", 5) == 0;
    case CBM_TYPE_REFERENCE:
        return rust_type_mentions_self(t->data.reference.elem, depth + 1);
    case CBM_TYPE_TEMPLATE:
        for (int i = 0; i < t->data.template_type.arg_count; i++) {
            if (rust_type_mentions_self(t->data.template_type.template_args[i], depth + 1)) {
                return true;
            }
        }
        return strncmp(t->data.template_type.template_name, "Self", 4) == 0;
    case CBM_TYPE_BUILTIN:
        return false;
    default:
        return true; /* shapes not inspected here are not trusted */
    }
}

/* Type of `Type::f(..)` when `f` is not declared on `Type` itself but comes
 * from a trait `Type` implements: the impl's override or the trait's sole
 * default (rust_resolve_trait_method). A `Self` return is `Type`; any other
 * return is taken as written in the trait only when it does not name `Self`.
 * NULL when no single trait provides `f`. */
static const CBMType *rust_trait_assoc_fn_type(RustLSPContext *ctx, const char *head,
                                              const char *fn_name) {
    if (!ctx || !head || !fn_name) {
        return NULL;
    }
    const char *candidates[2] = {
        head, ctx->module_qn ? cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, head) : NULL};
    for (int c = 0; c < 2; c++) {
        const char *type_qn = candidates[c];
        const CBMRegisteredType *rt =
            type_qn ? cbm_registry_lookup_type(ctx->registry, type_qn) : NULL;
        if (!rt || rt->is_interface) {
            continue;
        }
        int impls = 0;
        const CBMRegisteredFunc *f = rust_resolve_trait_method(ctx, type_qn, fn_name, &impls, NULL);
        if (!f || impls != 1 || !f->signature || f->signature->kind != CBM_TYPE_FUNC ||
            !f->signature->data.func.return_types || !f->signature->data.func.return_types[0]) {
            return NULL;
        }
        const CBMType *ret = f->signature->data.func.return_types[0];
        if (ret->kind == CBM_TYPE_NAMED && strcmp(ret->data.named.qualified_name, "Self") == 0) {
            return cbm_type_named(ctx->arena, type_qn);
        }
        if (cbm_type_is_unknown(ret) || rust_type_mentions_self(ret, 0)) {
            return NULL;
        }
        return ret;
    }
    return NULL;
}

/* ════════════════════════════════════════════════════════════════════
 * 8. Macro handling
 * ════════════════════════════════════════════════════════════════════ */

/* Walk a `macro_invocation`'s `token_tree` looking for nested call/method
 * call expressions. We do this so calls inside `vec![foo()]`,
 * `assert_eq!(a.bar(), 0)`, and `dbg!(get_value())` still get attributed
 * to the enclosing function. */
static void rust_walk_macro_tokens(RustLSPContext *ctx, TSNode node) {
    if (ts_node_is_null(node))
        return;
    const char *kind = ts_node_type(node);
    if (strcmp(kind, "call_expression") == 0 || strcmp(kind, "macro_invocation") == 0 ||
        strcmp(kind, "field_expression") == 0) {
        rust_resolve_calls_in_node(ctx, node);
        return;
    }
    uint32_t nc = ts_node_child_count(node);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(node, i);
        if (!ts_node_is_null(c))
            rust_walk_macro_tokens(ctx, c);
    }
}

/* Map a Rust infix/index operator token to the std::ops trait method that
 * the compiler desugars it to. `a + b` calls `Add::add`, `a[i]` calls
 * `Index::index`, etc. Returns NULL for operators with no overloadable
 * trait method (comparison/logical operators route through PartialEq /
 * PartialOrd whose methods we don't model here — sound to skip). */
static const char *rust_binop_trait_method(const char *op_text) {
    if (!op_text)
        return NULL;
    if (strcmp(op_text, "+") == 0)
        return "add";
    if (strcmp(op_text, "-") == 0)
        return "sub";
    if (strcmp(op_text, "*") == 0)
        return "mul";
    if (strcmp(op_text, "/") == 0)
        return "div";
    if (strcmp(op_text, "%") == 0)
        return "rem";
    if (strcmp(op_text, "&") == 0)
        return "bitand";
    if (strcmp(op_text, "|") == 0)
        return "bitor";
    if (strcmp(op_text, "^") == 0)
        return "bitxor";
    if (strcmp(op_text, "<<") == 0)
        return "shl";
    if (strcmp(op_text, ">>") == 0)
        return "shr";
    return NULL;
}

/* If `recv` is a user-defined NAMED type that defines operator method
 * `method` (via inherent impl or `impl <trait> for T`), emit a CALLS edge to
 * it. Models Rust operator-overload desugaring (`a + b` → T::add, `a[i]` →
 * T::index). Sound-only: we emit nothing when the operand type is unknown,
 * primitive, or the type has no such method registered — so we never guess on
 * built-in arithmetic. */
static void rust_emit_operator_call(RustLSPContext *ctx, const CBMType *recv, const char *method,
                                    TSNode site) {
    if (!recv || !method)
        return;
    const CBMType *base = recv;
    while (base && (base->kind == CBM_TYPE_REFERENCE || base->kind == CBM_TYPE_POINTER)) {
        base = (base->kind == CBM_TYPE_REFERENCE) ? base->data.reference.elem
                                                  : base->data.pointer.elem;
    }
    /* Only user-defined named types — built-in arithmetic must not emit. */
    if (!base || base->kind != CBM_TYPE_NAMED)
        return;
    const char *type_qn = base->data.named.qualified_name;
    if (!type_qn || is_rust_primitive(type_qn))
        return;
    int impl_count = 0;
    const CBMRegisteredFunc *m = rust_resolve_trait_method(ctx, type_qn, method, &impl_count, NULL);
    if (m && m->qualified_name) {
        uint32_t saved_start = ctx->emit_site_start_byte;
        uint32_t saved_end = ctx->emit_site_end_byte;
        uint32_t mapped_start = 0;
        uint32_t mapped_end = 0;
        bool mapped = !ts_node_is_null(site) &&
                      rust_map_source_range(ctx, ts_node_start_byte(site), ts_node_end_byte(site),
                                            &mapped_start, &mapped_end);
        ctx->emit_site_start_byte = mapped ? mapped_start : 0;
        ctx->emit_site_end_byte = mapped ? mapped_end : 0;
        rust_emit_resolved_call(ctx, m->qualified_name, m->impl_key, "lsp_operator_trait",
                                CBM_RUST_CONF_OPERATOR);
        /* `a + b` is a binary_expression, never a syntactic call node, so the
         * extractor produced no CBMCall to pair with the resolved_call above.
         * Inject one so the pipeline emits the CALLS edge. Inside a built-in
         * expression macro rust_emit_resolved_call already injects through
         * inject_syn_calls; outside it, inject explicitly. Generated
         * macro_rules! source has no faithful mapped site and stays semantic
         * only. */
        if (mapped && ctx->inject_syn_calls == 0)
            rust_inject_syn_call(ctx, m->qualified_name);
        ctx->emit_site_start_byte = saved_start;
        ctx->emit_site_end_byte = saved_end;
    }
}

/* ── User-defined macro_rules! support ────────────────────────────
 *
 * Strategy: collect every `macro_rules!` definition in the file
 * during the pre-walk, store each rule's transcriber text, and on
 * `macro_invocation` re-parse the transcriber as a synthetic Rust
 * function body so any calls inside the body are attributed to the
 * enclosing function of the invocation site.
 *
 * Recursion guard caps expansion depth at 8 (matches the rustc
 * default for macro recursion safety). */

typedef struct RustMacroRule {
    const char *macro_name;
    const char *pattern_text; /* left-hand side (without outer brackets) */
    int pattern_len;
    const char *transcriber_text;
    int transcriber_len;
    uint32_t definition_start_byte;
    uint32_t definition_end_byte;
    uint32_t scope_start_byte;
    uint32_t scope_end_byte;
    /* Nonzero for a definition inherited from another file: every rule of
     * that definition shares the id, and its byte fields describe the
     * definition's own text, not this file. */
    uint32_t external_id;
    bool miss; /* external_id entry recording that no definition is inherited */
} RustMacroRule;

/* Strip a single outer pair of matching brackets from a token-tree
 * text representation. Returns the inner span via out_text/out_len.
 * If no brackets, returns the original. */
static void rust_macro_strip_outer(const char *tt, int len, const char **out_text, int *out_len) {
    if (len >= 2 && ((tt[0] == '{' && tt[len - 1] == '}') || (tt[0] == '(' && tt[len - 1] == ')') ||
                     (tt[0] == '[' && tt[len - 1] == ']'))) {
        *out_text = tt + 1;
        *out_len = len - 2;
    } else {
        *out_text = tt;
        *out_len = len;
    }
}

/* A zeroed rule appended to ctx->macro_rules_arr, or NULL. */
static RustMacroRule *rust_macro_rules_push(RustLSPContext *ctx) {
    if (ctx->macro_rules_count % 16 == 0) {
        int new_cap = ctx->macro_rules_count + 16;
        struct RustMacroRule **narr = (struct RustMacroRule **)cbm_arena_alloc(
            ctx->arena, new_cap * sizeof(struct RustMacroRule *));
        if (!narr)
            return NULL;
        if (ctx->macro_rules_arr && ctx->macro_rules_count > 0) {
            memcpy(narr, ctx->macro_rules_arr,
                   ctx->macro_rules_count * sizeof(struct RustMacroRule *));
        }
        ctx->macro_rules_arr = narr;
    }
    RustMacroRule *r = (RustMacroRule *)cbm_arena_alloc(ctx->arena, sizeof(*r));
    if (!r)
        return NULL;
    memset(r, 0, sizeof(*r));
    ctx->macro_rules_arr[ctx->macro_rules_count++] = r;
    return r;
}

static void rust_record_macro_rule(RustLSPContext *ctx, const char *macro_name, TSNode pattern,
                                   TSNode transcriber, TSNode definition, TSNode scope) {
    if (!ctx || !macro_name || ts_node_is_null(transcriber))
        return;
    RustMacroRule *r = rust_macro_rules_push(ctx);
    if (!r)
        return;
    r->macro_name = cbm_arena_strdup(ctx->arena, macro_name);
    r->definition_start_byte = ts_node_start_byte(definition);
    r->definition_end_byte = ts_node_end_byte(definition);
    r->scope_start_byte = ts_node_start_byte(scope);
    r->scope_end_byte = ts_node_end_byte(scope);

    /* Cache pattern text for matching at invocation time. */
    if (!ts_node_is_null(pattern)) {
        char *pt = cbm_node_text(ctx->arena, pattern, ctx->source);
        if (pt) {
            const char *inner;
            int inner_len;
            rust_macro_strip_outer(pt, (int)strlen(pt), &inner, &inner_len);
            r->pattern_text = cbm_arena_strndup(ctx->arena, inner, (size_t)inner_len);
            r->pattern_len = inner_len;
        }
    }

    char *tt = cbm_node_text(ctx->arena, transcriber, ctx->source);
    if (tt) {
        int len = (int)strlen(tt);
        const char *inner;
        int inner_len;
        rust_macro_strip_outer(tt, len, &inner, &inner_len);
        r->transcriber_text = cbm_arena_strndup(ctx->arena, inner, (size_t)inner_len);
        r->transcriber_len = inner_len;
    }
}

static void rust_collect_macro_rules(RustLSPContext *ctx, TSNode root) {
    if (ts_node_is_null(root))
        return;
    uint32_t nc = ts_node_child_count(root);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(root, i);
        if (ts_node_is_null(c))
            continue;
        const char *k = ts_node_type(c);
        if (strcmp(k, "macro_definition") == 0) {
            TSNode name_node = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(name_node))
                continue;
            char *macro_name = rust_node_text(ctx, name_node);
            if (!macro_name)
                continue;
            uint32_t mnc = ts_node_child_count(c);
            for (uint32_t j = 0; j < mnc; j++) {
                TSNode rule = ts_node_child(c, j);
                if (ts_node_is_null(rule) || !ts_node_is_named(rule))
                    continue;
                if (strcmp(ts_node_type(rule), "macro_rule") != 0)
                    continue;
                TSNode left = ts_node_child_by_field_name(rule, "left", 4);
                TSNode right = ts_node_child_by_field_name(rule, "right", 5);
                if (!ts_node_is_null(right)) {
                    rust_record_macro_rule(ctx, macro_name, left, right, c, root);
                }
            }
        } else if (strcmp(k, "mod_item") == 0) {
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body))
                rust_collect_macro_rules(ctx, body);
        }
    }
}

/* macro_rules! uses textual scope: a definition is visible only after its
 * declaration, inside the containing module. A nested module may shadow an
 * outer definition; within one scope the latest preceding definition wins.
 * Every rule belonging to the selected definition shares the same source
 * coordinates, so expansion can still try its patterns in declaration order. */
static RustMacroRule *rust_visible_macro_definition(RustLSPContext *ctx, const char *macro_name,
                                                    TSNode invocation) {
    if (!ctx || !macro_name || !ctx->macro_rules_arr)
        return NULL;

    uint32_t position = 0;
    bool have_position = false;
    if (ctx->macro_origin_valid) {
        position = ctx->macro_origin_byte;
        have_position = true;
    } else if (ctx->source == ctx->root_source && !ts_node_is_null(invocation)) {
        position = ts_node_start_byte(invocation);
        have_position = true;
    }
    if (!have_position)
        return NULL;

    RustMacroRule *best = NULL;
    uint32_t best_scope_width = UINT32_MAX;
    for (int i = 0; i < ctx->macro_rules_count; i++) {
        RustMacroRule *rule = ctx->macro_rules_arr[i];
        if (!rule || rule->external_id || !rule->macro_name ||
            strcmp(rule->macro_name, macro_name) != 0 || position < rule->definition_end_byte || position < rule->scope_start_byte ||
            position > rule->scope_end_byte) {
            continue;
        }
        uint32_t scope_width = rule->scope_end_byte - rule->scope_start_byte;
        if (!best || scope_width < best_scope_width ||
            (scope_width == best_scope_width &&
             rule->definition_start_byte > best->definition_start_byte)) {
            best = rule;
            best_scope_width = scope_width;
        }
    }
    return best;
}

/* ── macro_rules! textual scope across files ──────────────────────
 *
 * A file-level `macro_rules!` stays in textual scope after its definition,
 * including inside every module declared later in the same module: the
 * child file loaded by `mod child;` sees each definition that precedes that
 * declaration, and so do its own descendants. The pipeline scans every file
 * for these facts (CBMRustMacroTable); a file with no visible definition of
 * the name walks up its module ancestry to the nearest module file that
 * defines it before the `mod` item on the path. */

extern const TSLanguage *tree_sitter_rust(void);

typedef struct {
    const char *name;
    const char *text; /* the whole `macro_rules! name { .. }` item */
    uint32_t end_byte;
} RustMacroFactDef;

typedef struct {
    const char *name;
    uint32_t start_byte;
    bool has_path;  /* `#[path = ..]` names the loaded file */
    bool macro_use; /* `#[macro_use]` lifts the child's macros into this module */
} RustMacroFactMod;

struct CBMRustMacroFacts {
    CBMArena arena;
    RustMacroFactDef *defs;
    int def_count;
    RustMacroFactMod *mods;
    int mod_count;
};

typedef struct {
    char *module_qn;
    CBMRustMacroFacts *facts;
} RustMacroTableEntry;

struct CBMRustMacroTable {
    RustMacroTableEntry *entries;
    int count;
    int cap;
};

static bool rust_bytes_contain(const char *s, int len, const char *needle) {
    size_t nlen = strlen(needle);
    if (!s || len <= 0 || (size_t)len < nlen)
        return false;
    for (int i = 0; i + (int)nlen <= len; i++) {
        if (s[i] == needle[0] && memcmp(s + i, needle, nlen) == 0)
            return true;
    }
    return false;
}

/* Whether the attributes directly above `item` include `#[<name> ..]`. */
static bool rust_item_has_attribute(const char *source, TSNode item, const char *name) {
    size_t name_len = strlen(name);
    for (TSNode prev = ts_node_prev_named_sibling(item); !ts_node_is_null(prev);
         prev = ts_node_prev_named_sibling(prev)) {
        const char *kind = ts_node_type(prev);
        if (strcmp(kind, "line_comment") == 0 || strcmp(kind, "block_comment") == 0)
            continue;
        if (strcmp(kind, "attribute_item") != 0)
            break;
        TSNode attribute = cbm_find_child_by_kind(prev, "attribute");
        if (ts_node_is_null(attribute) || ts_node_named_child_count(attribute) < 1)
            continue;
        TSNode head = ts_node_named_child(attribute, 0);
        size_t len = ts_node_end_byte(head) - ts_node_start_byte(head);
        if (len == name_len && memcmp(source + ts_node_start_byte(head), name, len) == 0)
            return true;
    }
    return false;
}

/* Grow `*items` (element size `size`) to hold one more than `count`. */
static bool rust_macro_facts_reserve(CBMArena *arena, void **items, int count, int *cap,
                                     size_t size) {
    if (count < *cap)
        return true;
    int grown_cap = *cap ? *cap * 2 : 8;
    void *grown = cbm_arena_alloc(arena, (size_t)grown_cap * size);
    if (!grown)
        return false;
    if (count > 0)
        memcpy(grown, *items, (size_t)count * size);
    *items = grown;
    *cap = grown_cap;
    return true;
}

/* Collect the file-level facts of `root` into `facts` (strings in its arena).
 * False on allocation failure. */
static bool rust_macro_facts_collect(CBMRustMacroFacts *facts, const char *source, TSNode root) {
    uint32_t count = ts_node_named_child_count(root);
    int def_cap = 0;
    int mod_cap = 0;
    for (uint32_t i = 0; i < count; i++) {
        TSNode item = ts_node_named_child(root, i);
        const char *kind = ts_node_type(item);
        bool is_def = strcmp(kind, "macro_definition") == 0;
        bool is_mod = strcmp(kind, "mod_item") == 0 &&
                      ts_node_is_null(ts_node_child_by_field_name(item, "body", 4));
        TSNode name_node = ts_node_child_by_field_name(item, "name", 4);
        if ((!is_def && !is_mod) || ts_node_is_null(name_node))
            continue;
        char *name = cbm_node_text(&facts->arena, name_node, source);
        if (!name)
            return false;
        if (is_def) {
            char *text = cbm_node_text(&facts->arena, item, source);
            if (!text || !rust_macro_facts_reserve(&facts->arena, (void **)&facts->defs,
                                                   facts->def_count, &def_cap,
                                                   sizeof(RustMacroFactDef)))
                return false;
            facts->defs[facts->def_count++] = (RustMacroFactDef){
                .name = name, .text = text, .end_byte = ts_node_end_byte(item)};
        } else {
            if (!rust_macro_facts_reserve(&facts->arena, (void **)&facts->mods, facts->mod_count,
                                          &mod_cap, sizeof(RustMacroFactMod)))
                return false;
            facts->mods[facts->mod_count++] = (RustMacroFactMod){
                .name = name,
                .start_byte = ts_node_start_byte(item),
                .has_path = rust_item_has_attribute(source, item, "path"),
                .macro_use = rust_item_has_attribute(source, item, "macro_use")};
        }
    }
    return true;
}

void cbm_rust_macro_facts_free(CBMRustMacroFacts *facts) {
    if (!facts)
        return;
    cbm_arena_destroy(&facts->arena);
    cbm_free(CBM_MEM_CLASS_OTHER, facts);
}

static CBMRustMacroFacts *rust_macro_facts_from_root(const char *source, TSNode root) {
    CBMRustMacroFacts *facts = (CBMRustMacroFacts *)cbm_calloc(CBM_MEM_CLASS_OTHER, sizeof(*facts));
    if (!facts)
        return NULL;
    cbm_arena_init(&facts->arena);
    if (!rust_macro_facts_collect(facts, source, root) || facts->def_count == 0) {
        cbm_rust_macro_facts_free(facts);
        return NULL;
    }
    return facts;
}

CBMRustMacroFacts *cbm_rust_macro_facts_scan(const char *source, int len) {
    if (!rust_bytes_contain(source, len, "macro_rules"))
        return NULL;
    TSParser *parser = ts_parser_new();
    if (!parser)
        return NULL;
    CBMRustMacroFacts *facts = NULL;
    if (ts_parser_set_language(parser, tree_sitter_rust())) {
        TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)len);
        if (tree) {
            facts = rust_macro_facts_from_root(source, ts_tree_root_node(tree));
            ts_tree_delete(tree);
        }
    }
    ts_parser_delete(parser);
    return facts;
}

static uint64_t rust_macro_digest_mix(uint64_t hash, const char *s) {
    for (; *s; s++) {
        hash ^= (unsigned char)*s;
        hash *= 0x100000001b3ULL;
    }
    hash ^= 0xffU; /* field separator */
    hash *= 0x100000001b3ULL;
    return hash;
}

uint64_t cbm_rust_macro_facts_digest(const char *source, int len, TSNode root) {
    if (!rust_bytes_contain(source, len, "macro_rules") || ts_node_is_null(root))
        return 0;
    CBMRustMacroFacts *facts = rust_macro_facts_from_root(source, root);
    if (!facts)
        return 0;
    /* Definitions and declarations merged in source order, offsets omitted. */
    uint64_t hash = 0xcbf29ce484222325ULL;
    int d = 0;
    int m = 0;
    while (d < facts->def_count || m < facts->mod_count) {
        bool take_def = m >= facts->mod_count || (d < facts->def_count &&
                                                  facts->defs[d].end_byte <= facts->mods[m].start_byte);
        if (take_def) {
            hash = rust_macro_digest_mix(hash, "D");
            hash = rust_macro_digest_mix(hash, facts->defs[d++].text);
        } else {
            const RustMacroFactMod *mod = &facts->mods[m++];
            hash = rust_macro_digest_mix(hash, mod->macro_use ? "U" : mod->has_path ? "P" : "M");
            hash = rust_macro_digest_mix(hash, mod->name);
        }
    }
    cbm_rust_macro_facts_free(facts);
    return hash ? hash : 1;
}

CBMRustMacroTable *cbm_rust_macro_table_new(void) {
    return (CBMRustMacroTable *)cbm_calloc(CBM_MEM_CLASS_OTHER, sizeof(CBMRustMacroTable));
}

void cbm_rust_macro_table_add(CBMRustMacroTable *table, const char *module_qn,
                              CBMRustMacroFacts *facts) {
    if (!facts)
        return;
    char *qn = module_qn ? cbm_mem_strdup(CBM_MEM_CLASS_OTHER, module_qn) : NULL;
    if (table && qn && table->count == table->cap) {
        int cap = table->cap ? table->cap * 2 : 16;
        RustMacroTableEntry *grown =
            (RustMacroTableEntry *)cbm_realloc(CBM_MEM_CLASS_OTHER, table->entries,
                                                (size_t)cap * sizeof(*grown));
        if (grown) {
            table->entries = grown;
            table->cap = cap;
        }
    }
    if (!table || !qn || table->count == table->cap) {
        cbm_free(CBM_MEM_CLASS_OTHER, qn);
        cbm_rust_macro_facts_free(facts);
        return;
    }
    table->entries[table->count++] = (RustMacroTableEntry){.module_qn = qn, .facts = facts};
}

void cbm_rust_macro_table_free(CBMRustMacroTable *table) {
    if (!table)
        return;
    for (int i = 0; i < table->count; i++) {
        cbm_free(CBM_MEM_CLASS_OTHER, table->entries[i].module_qn);
        cbm_rust_macro_facts_free(table->entries[i].facts);
    }
    cbm_free(CBM_MEM_CLASS_OTHER, table->entries);
    cbm_free(CBM_MEM_CLASS_OTHER, table);
}

static bool rust_macro_table_defines(const CBMRustMacroTable *table, const char *name) {
    for (int i = 0; i < table->count; i++) {
        const CBMRustMacroFacts *facts = table->entries[i].facts;
        for (int d = 0; d < facts->def_count; d++) {
            if (strcmp(facts->defs[d].name, name) == 0)
                return true;
        }
    }
    return false;
}

static const CBMRustMacroFacts *rust_macro_table_file(const CBMRustMacroTable *table,
                                                      const char *module_qn) {
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->entries[i].module_qn, module_qn) == 0)
            return table->entries[i].facts;
    }
    return NULL;
}

typedef enum {
    RUST_XMACRO_NOT_DECLARED, /* this file does not load the child */
    RUST_XMACRO_PASS,         /* it does, and defines no such macro before it */
    RUST_XMACRO_FOUND,
    RUST_XMACRO_UNDECIDABLE, /* a `#[macro_use]` child or a twin declaration intervenes */
} RustXMacroStep;

/* What the file `facts` binds to `name` at its `mod child;` item. */
static RustXMacroStep rust_macro_parent_step(const CBMRustMacroFacts *facts, const char *child,
                                             bool child_has_path, const char *name,
                                             const char **out_text) {
    const RustMacroFactMod *decl = NULL;
    for (int m = 0; m < facts->mod_count; m++) {
        const RustMacroFactMod *mod = &facts->mods[m];
        if (strcmp(mod->name, child) != 0 || mod->has_path != child_has_path)
            continue;
        if (decl)
            return RUST_XMACRO_UNDECIDABLE;
        decl = mod;
    }
    if (!decl)
        return RUST_XMACRO_NOT_DECLARED;
    for (int m = 0; m < facts->mod_count; m++) {
        if (facts->mods[m].macro_use && facts->mods[m].start_byte < decl->start_byte)
            return RUST_XMACRO_UNDECIDABLE;
    }
    const RustMacroFactDef *best = NULL;
    for (int d = 0; d < facts->def_count; d++) {
        const RustMacroFactDef *def = &facts->defs[d];
        if (def->end_byte <= decl->start_byte && strcmp(def->name, name) == 0)
            best = def;
    }
    if (!best)
        return RUST_XMACRO_PASS;
    *out_text = best->text;
    return RUST_XMACRO_FOUND;
}

/* The `macro_rules!` item text that the file's module ancestors bind to
 * `name` by textual scope, or NULL when none or undecidable. Each step names
 * the parent module's file from the child's module identity; the module tree
 * must confirm that file's module is an ancestor of this one. */
static const char *rust_inherited_macro_text(RustLSPContext *ctx, const char *name) {
    const CBMRustMacroTable *table = ctx->imports ? ctx->imports->macros : NULL;
    const CBMRustModTree *tree = ctx->imports ? ctx->imports->modtree : NULL;
    if (!table || !tree || !ctx->module_qn || !ctx->relative_module_qn ||
        !rust_macro_table_defines(table, name) || !cbm_rust_modtree_has_file(tree, ctx->module_qn))
        return NULL;
    char *cur = cbm_arena_strdup(ctx->arena, ctx->relative_module_qn);
    bool child_has_path = strcmp(ctx->relative_module_qn, ctx->module_qn) != 0;
    static const char *const parent_files[] = {"", ".mod", ".lib", ".main"};
    enum { RUST_XMACRO_FIRST_ROOT_FILE = 2, RUST_XMACRO_MAX_DEPTH = 32 };
    for (int depth = 0; cur && depth < RUST_XMACRO_MAX_DEPTH; depth++) {
        char *dot = strrchr(cur, '.');
        if (dot && depth == 0 && strcmp(dot + 1, "mod") == 0) {
            *dot = '\0'; /* `dir/mod.rs` is the module `dir` */
            dot = strrchr(cur, '.');
        }
        if (!dot || strcmp(dot + 1, "lib") == 0 || strcmp(dot + 1, "main") == 0)
            return NULL; /* a crate root has no parent module */
        const char *child = dot + 1;
        *dot = '\0';
        const char *found = NULL;
        bool passed = false;
        bool crate_root = false;
        for (size_t k = 0; k < sizeof(parent_files) / sizeof(parent_files[0]); k++) {
            const char *parent_qn = cbm_arena_sprintf(ctx->arena, "%s%s", cur, parent_files[k]);
            const CBMRustMacroFacts *facts =
                parent_qn ? rust_macro_table_file(table, parent_qn) : NULL;
            if (!facts || !cbm_rust_modtree_has_file(tree, parent_qn) ||
                cbm_rust_modtree_file_outside(tree, ctx->module_qn, parent_qn))
                continue;
            const char *text = NULL;
            RustXMacroStep step = rust_macro_parent_step(facts, child, child_has_path, name, &text);
            if (step == RUST_XMACRO_UNDECIDABLE)
                return NULL;
            if (step == RUST_XMACRO_NOT_DECLARED)
                continue;
            if (step == RUST_XMACRO_FOUND) {
                if (found && strcmp(found, text) != 0)
                    return NULL;
                found = text;
            } else {
                passed = true;
            }
            crate_root = crate_root || k >= RUST_XMACRO_FIRST_ROOT_FILE;
        }
        if (found && passed)
            return NULL; /* two parents disagree */
        if (found || crate_root)
            return found;
        child_has_path = false;
    }
    return NULL;
}

/* Record the rules of the macro_rules! item `text` as one external
 * definition of `name`, or a miss when there is none, and return its first
 * rule (NULL for a miss). */
static RustMacroRule *rust_record_external_macro(RustLSPContext *ctx, const char *name,
                                                 const char *text) {
    int first = ctx->macro_rules_count;
    TSParser *parser = text ? ts_parser_new() : NULL;
    TSTree *tree = NULL;
    if (parser && ts_parser_set_language(parser, tree_sitter_rust()))
        tree = ts_parser_parse_string(parser, NULL, text, (uint32_t)strlen(text));
    TSNode def = tree ? ts_node_named_child(ts_tree_root_node(tree), 0) : (TSNode){0};
    if (!ts_node_is_null(def) && strcmp(ts_node_type(def), "macro_definition") == 0) {
        const char *saved_source = ctx->source;
        ctx->source = text;
        uint32_t count = ts_node_named_child_count(def);
        for (uint32_t i = 0; i < count; i++) {
            TSNode rule = ts_node_named_child(def, i);
            if (strcmp(ts_node_type(rule), "macro_rule") == 0) {
                rust_record_macro_rule(ctx, name, ts_node_child_by_field_name(rule, "left", 4),
                                       ts_node_child_by_field_name(rule, "right", 5), def, def);
            }
        }
        ctx->source = saved_source;
    }
    if (tree)
        ts_tree_delete(tree);
    if (parser)
        ts_parser_delete(parser);
    bool miss = ctx->macro_rules_count == first;
    if (miss) {
        RustMacroRule *r = rust_macro_rules_push(ctx);
        if (!r)
            return NULL;
        r->macro_name = cbm_arena_strdup(ctx->arena, name);
        r->miss = true;
    }
    for (int i = first; i < ctx->macro_rules_count; i++) {
        ctx->macro_rules_arr[i]->external_id = (uint32_t)first + 1;
    }
    return miss ? NULL : ctx->macro_rules_arr[first];
}

/* The definition an invocation of `macro_name` expands: the file's own
 * textually visible definition, else the one inherited across files. */
static RustMacroRule *rust_macro_definition_for(RustLSPContext *ctx, const char *macro_name,
                                                TSNode invocation) {
    RustMacroRule *local = rust_visible_macro_definition(ctx, macro_name, invocation);
    if (local || !ctx || !macro_name || strchr(macro_name, ':') || !ctx->imports ||
        !ctx->imports->macros)
        return local;
    for (int i = 0; i < ctx->macro_rules_count; i++) {
        RustMacroRule *rule = ctx->macro_rules_arr[i];
        if (rule->external_id && rule->macro_name && strcmp(rule->macro_name, macro_name) == 0)
            return rule->miss ? NULL : rule;
    }
    return rust_record_external_macro(ctx, macro_name,
                                      rust_inherited_macro_text(ctx, macro_name));
}

static bool rust_has_visible_macro_definition(RustLSPContext *ctx, const char *macro_name,
                                              TSNode invocation) {
    return rust_macro_definition_for(ctx, macro_name, invocation) != NULL;
}

/* ── Metavar matching + substitution ─────────────────────────────
 *
 * Real `macro_rules!` semantics in a clean-room subset. We implement
 * the common shapes:
 *   $name:expr / $name:ty / $name:ident / $name:tt / $name:path /
 *   $name:pat / $name:literal / $name:block / $name:stmt
 *
 * Repetition `$( ... ) sep? kind` (kind `*`, `+` or `?`) binds each
 * fragment once per iteration, nested up to RUST_MACRO_MAX_DEPTH groups
 * deep; the transcriber's `$( ... )` group pastes them per iteration with
 * its separator. A matcher or transcriber the engine cannot model expands
 * to nothing rather than to a guess.
 *
 * Substitution writes the bound values back into the transcriber and
 * re-parses the result so any calls inside are visible to the
 * resolver. */

#define RUST_MACRO_MAX_BINDINGS 256
#define RUST_MACRO_MAX_REPS 128
#define RUST_MACRO_MAX_DEPTH 4
#define RUST_MACRO_NAME_MAX 32

/* Pattern match results below zero: no match, or a shape the engine does
 * not model (no further rule may be tried: it could have been the match). */
enum { MACRO_NO_MATCH = -1, MACRO_UNMODELLED = -2 };

/* One metavar value. A metavar declared inside `depth` repetition groups
 * has one binding per iteration, keyed by the iteration index of each
 * enclosing group. */
typedef struct {
    char name[RUST_MACRO_NAME_MAX]; /* metavar name (without `$`) */
    const char *value;              /* substring of the invocation args */
    int value_len;
    int depth;
    int idx[RUST_MACRO_MAX_DEPTH];
} MacroBinding;

/* Iteration count of one matched repetition group, recorded for every
 * metavar declared inside it: the group sits `depth` groups deep at the
 * enclosing iteration `idx`. */
typedef struct {
    char name[RUST_MACRO_NAME_MAX];
    int depth;
    int idx[RUST_MACRO_MAX_DEPTH];
    int count;
} MacroRepCount;

typedef struct {
    MacroBinding kv[RUST_MACRO_MAX_BINDINGS];
    int count;
    MacroRepCount reps[RUST_MACRO_MAX_REPS];
    int rep_count;
} MacroEnv;

/* Skip leading ASCII whitespace and Rust line comments. */
static int macro_skip_ws(const char *s, int len, int from) {
    while (from < len) {
        char c = s[from];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            from++;
            continue;
        }
        if (c == '/' && from + 1 < len && s[from + 1] == '/') {
            from += 2;
            while (from < len && s[from] != '\n')
                from++;
            continue;
        }
        break;
    }
    return from;
}

/* Consume an identifier (ASCII rust ident). Returns end position or
 * `from` if no identifier. */
static int macro_consume_ident(const char *s, int len, int from) {
    int p = from;
    if (p >= len)
        return from;
    char c = s[p];
    if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        return from;
    p++;
    while (p < len) {
        c = s[p];
        if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            p++;
        } else
            break;
    }
    return p;
}

/* Consume a balanced bracket group starting at `from` (which must
 * point at `(`, `[`, or `{`). Returns end position (just after the
 * close bracket) or `from` if not a bracket. */
static int macro_consume_balanced(const char *s, int len, int from) {
    if (from >= len)
        return from;
    char open = s[from];
    char close = 0;
    if (open == '(')
        close = ')';
    else if (open == '[')
        close = ']';
    else if (open == '{')
        close = '}';
    else
        return from;
    int depth = 1;
    int p = from + 1;
    while (p < len && depth > 0) {
        char c = s[p];
        if (c == '"') {
            /* Skip string literal. */
            p++;
            while (p < len && s[p] != '"') {
                if (s[p] == '\\' && p + 1 < len)
                    p += 2;
                else
                    p++;
            }
            if (p < len)
                p++;
            continue;
        }
        if (c == '\'') {
            /* Could be lifetime or char literal — accept char. */
            int q = p + 1;
            if (q < len && s[q] == '\\') {
                q++;
                if (q < len)
                    q++;
            } else if (q < len)
                q++;
            if (q < len && s[q] == '\'') {
                p = q + 1;
                continue;
            }
            /* otherwise treat as lifetime — skip until non-ident. */
            p = macro_consume_ident(s, len, p + 1);
            continue;
        }
        if (c == '(' || c == '[' || c == '{')
            depth++;
        else if (c == ')' || c == ']' || c == '}')
            depth--;
        p++;
    }
    return p;
}

/* Consume a fragment from the input matching the given fragment kind.
 * Returns end position or `from` if the consumption failed. */
static int macro_consume_fragment(const char *s, int len, int from, const char *frag) {
    from = macro_skip_ws(s, len, from);
    if (from >= len)
        return from;
    if (!frag)
        frag = "tt";

    if (strcmp(frag, "ident") == 0) {
        return macro_consume_ident(s, len, from);
    }
    if (strcmp(frag, "literal") == 0) {
        /* Numeric, string, char, bool literal. */
        char c = s[from];
        if (c == '"')
            return macro_consume_balanced(s, len, from);
        if (c == '\'') {
            int q = from + 1;
            if (q < len && s[q] == '\\') {
                q++;
                if (q < len)
                    q++;
            } else if (q < len)
                q++;
            if (q < len && s[q] == '\'')
                return q + 1;
            return from;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            int p = from;
            if (c == '-')
                p++;
            while (p < len && ((s[p] >= '0' && s[p] <= '9') || s[p] == '_' || s[p] == '.' ||
                               s[p] == 'x' || s[p] == 'X' || s[p] == 'b' || s[p] == 'o' ||
                               (s[p] >= 'a' && s[p] <= 'f') || (s[p] >= 'A' && s[p] <= 'F')))
                p++;
            /* Allow type suffix. */
            int after = macro_consume_ident(s, len, p);
            return after;
        }
        return macro_consume_ident(s, len, from);
    }
    if (strcmp(frag, "tt") == 0) {
        char c = s[from];
        if (c == '(' || c == '[' || c == '{') {
            return macro_consume_balanced(s, len, from);
        }
        /* Single token: identifier, literal, or single-char punct. */
        if (c == '"' || c == '\'') {
            return macro_consume_fragment(s, len, from, "literal");
        }
        if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-') {
            int p = macro_consume_ident(s, len, from);
            if (p > from)
                return p;
            return macro_consume_fragment(s, len, from, "literal");
        }
        /* A closing delimiter ends the enclosing token tree; it is no tt. */
        if (c == ')' || c == ']' || c == '}')
            return from;
        return from + 1;
    }
    /* expr / ty / path / pat / stmt / block — balance brackets, stop
     * at top-level `,` or end of input. */
    if (strcmp(frag, "block") == 0) {
        if (s[from] == '{')
            return macro_consume_balanced(s, len, from);
        return from;
    }
    /* For expr/ty/path/pat/stmt: greedy balanced span up to a token none
     * of them holds outside brackets (`,` `;` `=>`). */
    bool item = strcmp(frag, "item") == 0;
    int p = from;
    while (p < len) {
        char c = s[p];
        if (c == '"' || c == '\'') {
            int q = macro_consume_fragment(s, len, p, "literal");
            if (q == p) {
                p++;
                continue;
            }
            p = q;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            int q = macro_consume_balanced(s, len, p);
            if (q == p)
                break;
            p = q;
            continue;
        }
        if (c == ')' || c == ']' || c == '}')
            break;
        if (c == ',')
            break;
        if (!item && (c == ';' || (c == '=' && p + 1 < len && s[p + 1] == '>')))
            break;
        p++;
    }
    return p;
}

/* Iteration path of the repetition being matched or transcribed: `depth`
 * enclosing groups, `idx[i]` the iteration of the i-th. */
typedef struct {
    int depth;
    int idx[RUST_MACRO_MAX_DEPTH];
} MacroRepPath;

static bool macro_name_copy(char *dst, const char *name, int name_len) {
    if (name_len <= 0 || name_len >= RUST_MACRO_NAME_MAX)
        return false;
    memcpy(dst, name, (size_t)name_len);
    dst[name_len] = '\0';
    return true;
}

/* True when `prefix` (depth entries) is the leading part of `idx`. */
static bool macro_idx_prefix(const int *idx, const MacroRepPath *path, int depth) {
    for (int i = 0; i < depth; i++) {
        if (idx[i] != path->idx[i])
            return false;
    }
    return true;
}

/* Bind a metavar at the current iteration path. Returns false if the
 * table is full or the name doesn't fit. */
static bool macro_env_bind(MacroEnv *env, const char *name, int name_len, const char *val,
                           int val_len, const MacroRepPath *path) {
    if (env->count >= RUST_MACRO_MAX_BINDINGS)
        return false;
    MacroBinding *b = &env->kv[env->count];
    if (!macro_name_copy(b->name, name, name_len))
        return false;
    b->value = val;
    b->value_len = val_len;
    b->depth = path->depth;
    memcpy(b->idx, path->idx, sizeof(b->idx));
    env->count++;
    return true;
}

/* The binding of `name` visible at `path`: declared at most path->depth
 * groups deep, from the enclosing iterations of `path`. */
static const MacroBinding *macro_env_lookup(const MacroEnv *env, const char *name,
                                            const MacroRepPath *path) {
    for (int i = 0; i < env->count; i++) {
        const MacroBinding *b = &env->kv[i];
        if (b->depth <= path->depth && strcmp(b->name, name) == 0 &&
            macro_idx_prefix(b->idx, path, b->depth))
            return b;
    }
    return NULL;
}

/* True when `name` was declared inside a repetition group at least
 * `depth` groups deep (it still repeats at that depth). */
static bool macro_env_repeats_at(const MacroEnv *env, const char *name, int depth) {
    for (int i = 0; i < env->rep_count; i++) {
        if (env->reps[i].depth >= depth && strcmp(env->reps[i].name, name) == 0)
            return true;
    }
    return false;
}

/* Parse the operator after a repetition group's `)` at `from`: an optional
 * separator token and the kind `*`, `+` or `?`. Returns the position after
 * the kind, or -1 for an operator the engine does not model. */
static int macro_parse_rep_op(const char *s, int len, int from, int *sep_start, int *sep_len,
                              char *kind) {
    int p = macro_skip_ws(s, len, from);
    *sep_start = p;
    *sep_len = 0;
    if (p < len && (s[p] == '*' || s[p] == '+' || s[p] == '?')) {
        *kind = s[p];
        return p + 1;
    }
    if (p + 1 < len && s[p] == '=' && s[p + 1] == '>') {
        *sep_len = 2;
    } else if (p < len && (s[p] == ',' || s[p] == ';' || s[p] == '|' || s[p] == '=' ||
                           s[p] == '.' || s[p] == '&' || s[p] == ':')) {
        *sep_len = 1;
    } else {
        return -1;
    }
    int q = macro_skip_ws(s, len, p + *sep_len);
    if (q < len && (s[q] == '*' || s[q] == '+')) {
        *kind = s[q];
        return q + 1;
    }
    return -1;
}

/* Record the iteration count of a matched group for every metavar declared
 * in its pattern `body` (nested groups included). */
static bool macro_env_record_reps(MacroEnv *env, const char *body, int body_len,
                                  const MacroRepPath *path, int count) {
    for (int p = 0; p < body_len; p++) {
        if (body[p] != '$')
            continue;
        int ns = macro_skip_ws(body, body_len, p + 1);
        int ne = macro_consume_ident(body, body_len, ns);
        if (ne == ns || ne >= body_len || body[ne] != ':')
            continue;
        if (env->rep_count >= RUST_MACRO_MAX_REPS)
            return false;
        MacroRepCount *rc = &env->reps[env->rep_count];
        if (!macro_name_copy(rc->name, body + ns, ne - ns))
            return false;
        rc->depth = path->depth;
        memcpy(rc->idx, path->idx, sizeof(rc->idx));
        rc->count = count;
        env->rep_count++;
        p = ne;
    }
    return true;
}

static int macro_match_seq(const char *pat, int pat_len, const char *in, int in_len, int ip,
                           MacroEnv *env, const MacroRepPath *path);

/* Match the repetition group whose `(` is at pat[open] against the input at
 * `ip`: as many iterations as match, joined by the separator. Returns the
 * input position after the last iteration (or a MACRO_ result below 0) and
 * sets *pat_after past the operator. */
static int macro_match_rep(const char *pat, int pat_len, int open, const char *in, int in_len,
                           int ip, MacroEnv *env, const MacroRepPath *path, int *pat_after) {
    int close_after = macro_consume_balanced(pat, pat_len, open);
    if (close_after <= open || pat[close_after - 1] != ')')
        return MACRO_UNMODELLED;
    int sep_start = 0;
    int sep_len = 0;
    char kind = 0;
    int after = macro_parse_rep_op(pat, pat_len, close_after, &sep_start, &sep_len, &kind);
    if (after < 0 || path->depth >= RUST_MACRO_MAX_DEPTH)
        return MACRO_UNMODELLED;
    const char *body = pat + open + 1;
    int body_len = close_after - 1 - (open + 1);
    MacroRepPath inner = *path;
    inner.depth = path->depth + 1;
    int n = 0;
    int cur = ip;
    for (;;) {
        int start = cur;
        if (n > 0 && sep_len > 0) {
            int s = macro_skip_ws(in, in_len, cur);
            if (s + sep_len > in_len || memcmp(in + s, pat + sep_start, (size_t)sep_len) != 0)
                break;
            start = s + sep_len;
        }
        inner.idx[path->depth] = n;
        int saved_count = env->count;
        int saved_reps = env->rep_count;
        int end = macro_match_seq(body, body_len, in, in_len, start, env, &inner);
        if (end == MACRO_UNMODELLED)
            return MACRO_UNMODELLED;
        if (end < 0 || macro_skip_ws(in, in_len, end) == macro_skip_ws(in, in_len, start)) {
            env->count = saved_count;
            env->rep_count = saved_reps;
            break;
        }
        cur = end;
        n++;
        if (kind == '?')
            break;
    }
    if (kind == '+' && n == 0)
        return MACRO_NO_MATCH;
    if (!macro_env_record_reps(env, body, body_len, path, n))
        return MACRO_UNMODELLED;
    *pat_after = after;
    return cur;
}

/* Match a pattern sequence against the input from `ip`. Fragments like
 * `$x:expr` bind into `env` at the iteration `path`. Returns the input
 * position after the matched prefix, or a MACRO_ result below 0. */
static int macro_match_seq(const char *pat, int pat_len, const char *in, int in_len, int ip,
                           MacroEnv *env, const MacroRepPath *path) {
    int pp = 0;
    while (pp < pat_len) {
        pp = macro_skip_ws(pat, pat_len, pp);
        ip = macro_skip_ws(in, in_len, ip);
        if (pp >= pat_len)
            break;
        if (pat[pp] != '$') {
            /* Literal: must match input verbatim. */
            if (ip >= in_len || pat[pp] != in[ip])
                return MACRO_NO_MATCH;
            pp++;
            ip++;
            continue;
        }
        pp = macro_skip_ws(pat, pat_len, pp + 1);
        if (pp < pat_len && pat[pp] == '(') {
            int pat_after = pp;
            ip = macro_match_rep(pat, pat_len, pp, in, in_len, ip, env, path, &pat_after);
            if (ip < 0)
                return ip;
            pp = pat_after;
            continue;
        }
        int name_start = pp;
        pp = macro_consume_ident(pat, pat_len, pp);
        int name_end = pp;
        if (name_end == name_start)
            return MACRO_UNMODELLED;
        char frag[16] = "tt";
        if (pp < pat_len && pat[pp] == ':') {
            int frag_start = ++pp;
            pp = macro_consume_ident(pat, pat_len, pp);
            int fl = pp - frag_start;
            if (fl <= 0 || fl >= (int)sizeof(frag))
                return MACRO_UNMODELLED;
            memcpy(frag, pat + frag_start, (size_t)fl);
            frag[fl] = '\0';
        }
        int val_start = macro_skip_ws(in, in_len, ip);
        int val_end = macro_consume_fragment(in, in_len, ip, frag);
        if (val_end <= val_start)
            return MACRO_NO_MATCH;
        if (!macro_env_bind(env, pat + name_start, name_end - name_start, in + val_start,
                            val_end - val_start, path))
            return MACRO_UNMODELLED;
        ip = val_end;
    }
    return ip;
}

/* Match a rule's whole pattern against the whole input. */
static int macro_pattern_match(const char *pat, int pat_len, const char *in, int in_len,
                               MacroEnv *env) {
    MacroRepPath top = {0};
    int end = macro_match_seq(pat, pat_len, in, in_len, 0, env, &top);
    if (end < 0)
        return end;
    return macro_skip_ws(in, in_len, end) == in_len ? end : MACRO_NO_MATCH;
}

/* Site windows of a synthetic buffer under construction (arena-grown). */
typedef struct {
    CBMArena *arena;
    CBMRustSiteWindow *items;
    int count;
    int cap;
} RustSiteWindows;

static void rust_site_windows_push(RustSiteWindows *w, uint32_t synthetic_start,
                                   uint32_t synthetic_end, uint32_t original_start) {
    /* A window that continues the previous one in both buffers extends it,
     * so a site pasted from consecutive fragments maps as one range. */
    if (w->count > 0) {
        CBMRustSiteWindow *last = &w->items[w->count - 1];
        if (last->synthetic_end == synthetic_start &&
            last->original_start + (last->synthetic_end - last->synthetic_start) ==
                original_start) {
            last->synthetic_end = synthetic_end;
            return;
        }
    }
    if (w->count == w->cap) {
        int cap = w->cap ? w->cap * 2 : 8;
        CBMRustSiteWindow *grown =
            (CBMRustSiteWindow *)cbm_arena_alloc(w->arena, (size_t)cap * sizeof(*grown));
        if (!grown)
            return;
        if (w->count > 0)
            memcpy(grown, w->items, (size_t)w->count * sizeof(*grown));
        w->items = grown;
        w->cap = cap;
    }
    w->items[w->count++] = (CBMRustSiteWindow){.synthetic_start = synthetic_start,
                                               .synthetic_end = synthetic_end,
                                               .original_start = original_start};
}

/* Append windows that carry the original-file bytes behind the current
 * source's range [start, end), copied to synthetic offset `at`. */
static void rust_site_windows_carry(const RustLSPContext *ctx, RustSiteWindows *w, uint32_t start,
                                    uint32_t end, uint32_t at) {
    if (end <= start)
        return;
    if (!ctx->site_map_active) {
        if (ctx->source == ctx->root_source)
            rust_site_windows_push(w, at, at + (end - start), start);
        return;
    }
    for (int i = 0; i < ctx->site_window_count; i++) {
        const CBMRustSiteWindow *cw = &ctx->site_windows[i];
        uint32_t s = start > cw->synthetic_start ? start : cw->synthetic_start;
        uint32_t e = end < cw->synthetic_end ? end : cw->synthetic_end;
        if (s < e)
            rust_site_windows_push(w, at + (s - start), at + (e - start),
                                   cw->original_start + (s - cw->synthetic_start));
    }
}

/* Where a macro invocation's argument text came from: `inv` (inv_len bytes)
 * copies the current source from byte inv_src_start. Substituting a fragment
 * of it records a window for the bytes that reach the original file. */
typedef struct {
    const RustLSPContext *ctx;
    const char *inv;
    int inv_len;
    uint32_t inv_src_start;
    RustSiteWindows *windows;
} RustMacroSubstSites;

/* Transcriber output under construction (arena-grown). */
typedef struct {
    CBMArena *arena;
    char *buf;
    int len;
    int cap;
    /* The last pasted fragment: where it ends in the invocation text and the
     * output length right after it. */
    const char *last_value_end;
    int last_value_out;
} MacroOut;

static bool macro_out_append(MacroOut *o, const char *s, int n) {
    if (o->len + n + 1 > o->cap) {
        int cap = o->cap ? o->cap : 256;
        while (o->len + n + 1 > cap)
            cap *= 2;
        char *grown = (char *)cbm_arena_alloc(o->arena, (size_t)cap);
        if (!grown)
            return false;
        if (o->len > 0)
            memcpy(grown, o->buf, (size_t)o->len);
        o->buf = grown;
        o->cap = cap;
    }
    memcpy(o->buf + o->len, s, (size_t)n);
    o->len += n;
    o->buf[o->len] = '\0';
    return true;
}

/* Copy the invocation bytes [from, to) into the output, with a site window
 * for the bytes that reach the original file. */
static bool macro_paste_span(MacroOut *o, const char *from, const char *to,
                             const RustMacroSubstSites *sites, uint32_t base) {
    if (sites && from >= sites->inv && to <= sites->inv + sites->inv_len) {
        uint32_t src = sites->inv_src_start + (uint32_t)(from - sites->inv);
        rust_site_windows_carry(sites->ctx, sites->windows, src, src + (uint32_t)(to - from),
                                base + (uint32_t)o->len);
    }
    return macro_out_append(o, from, (int)(to - from));
}

/* Paste a bound fragment. A fragment pasted right after the previous one
 * (no transcriber text between them, as in `$($t)*`) keeps the whitespace
 * that separated the two in the invocation: tokens stay apart, and adjacent
 * tokens stay one site. */
static bool macro_paste_value(MacroOut *o, const MacroBinding *b,
                              const RustMacroSubstSites *sites, uint32_t base) {
    if (sites && o->last_value_end && o->len == o->last_value_out &&
        o->last_value_end >= sites->inv && b->value >= o->last_value_end &&
        b->value <= sites->inv + sites->inv_len) {
        const char *gap = o->last_value_end;
        while (gap < b->value && (*gap == ' ' || *gap == '\t' || *gap == '\n' || *gap == '\r'))
            gap++;
        if (gap == b->value && !macro_paste_span(o, o->last_value_end, b->value, sites, base))
            return false;
    }
    if (!macro_paste_span(o, b->value, b->value + b->value_len, sites, base))
        return false;
    o->last_value_end = b->value + b->value_len;
    o->last_value_out = o->len;
    return true;
}

/* Iteration count of a transcriber repetition group `body` at `path`: the
 * count its matcher group recorded for the metavars in `body` that still
 * repeat at this depth. -1 when none does or they disagree. */
static int macro_rep_count(const MacroEnv *env, const char *body, int body_len,
                           const MacroRepPath *path) {
    int count = -1;
    for (int p = 0; p < body_len; p++) {
        if (body[p] != '$')
            continue;
        int ne = macro_consume_ident(body, body_len, p + 1);
        char name[RUST_MACRO_NAME_MAX];
        if (!macro_name_copy(name, body + p + 1, ne - (p + 1)))
            continue;
        for (int i = 0; i < env->rep_count; i++) {
            const MacroRepCount *rc = &env->reps[i];
            if (rc->depth != path->depth || strcmp(rc->name, name) != 0 ||
                !macro_idx_prefix(rc->idx, path, path->depth))
                continue;
            if (count >= 0 && count != rc->count)
                return -1;
            count = rc->count;
        }
        p = ne - 1;
    }
    return count;
}

/* Substitute env bindings into the transcriber text at iteration `path`.
 * `sites` (optional) receives a window for each substituted fragment,
 * offset by `base`. Returns false for a transcriber the engine cannot
 * model (a metavar pasted at the wrong repetition depth, a group with no
 * repeating metavar). */
static bool macro_subst_seq(MacroOut *o, const char *xs, int xs_len, const MacroEnv *env,
                            const RustMacroSubstSites *sites, uint32_t base,
                            const MacroRepPath *path) {
    int xp = 0;
    while (xp < xs_len) {
        char c = xs[xp];
        if (c == '$' && xp + 1 < xs_len && xs[xp + 1] == '(') {
            int close_after = macro_consume_balanced(xs, xs_len, xp + 1);
            if (close_after <= xp + 1 || xs[close_after - 1] != ')')
                return false;
            int sep_start = 0;
            int sep_len = 0;
            char kind = 0;
            int after = macro_parse_rep_op(xs, xs_len, close_after, &sep_start, &sep_len, &kind);
            if (after < 0 || path->depth >= RUST_MACRO_MAX_DEPTH)
                return false;
            const char *body = xs + xp + 2;
            int body_len = close_after - 1 - (xp + 2);
            int n = macro_rep_count(env, body, body_len, path);
            if (n < 0)
                return false;
            MacroRepPath inner = *path;
            inner.depth = path->depth + 1;
            for (int i = 0; i < n; i++) {
                if (i > 0 && sep_len > 0 && !macro_out_append(o, xs + sep_start, sep_len))
                    return false;
                inner.idx[path->depth] = i;
                if (!macro_subst_seq(o, body, body_len, env, sites, base, &inner))
                    return false;
            }
            xp = after;
            continue;
        }
        if (c == '$') {
            int name_end = macro_consume_ident(xs, xs_len, xp + 1);
            char name[RUST_MACRO_NAME_MAX];
            if (macro_name_copy(name, xs + xp + 1, name_end - (xp + 1))) {
                if (macro_env_repeats_at(env, name, path->depth))
                    return false;
                const MacroBinding *b = macro_env_lookup(env, name, path);
                if (b && b->value) {
                    if (!macro_paste_value(o, b, sites, base))
                        return false;
                    xp = name_end;
                    continue;
                }
            }
        }
        if (!macro_out_append(o, &c, 1))
            return false;
        xp++;
    }
    return true;
}

/* Substitute env bindings into the transcriber text. Allocates a fresh
 * string in the arena; NULL when the transcriber cannot be modelled. */
static char *macro_substitute(CBMArena *arena, const char *xs, int xs_len, const MacroEnv *env,
                              const RustMacroSubstSites *sites, uint32_t base) {
    MacroOut o = {.arena = arena};
    MacroRepPath top = {0};
    int windows_before = sites ? sites->windows->count : 0;
    if (!macro_out_append(&o, "", 0) || !macro_subst_seq(&o, xs, xs_len, env, sites, base, &top)) {
        if (sites)
            sites->windows->count = windows_before;
        return NULL;
    }
    return o.buf;
}

/* Re-parse a user macro's transcriber body as a synthetic Rust
 * function and walk it for calls. */
extern const TSLanguage *tree_sitter_rust(void);
static void rust_expand_user_macro(RustLSPContext *ctx, const char *mname, TSNode invocation) {
    if (!ctx || !mname)
        return;
    if (ctx->macro_expand_depth >= 8)
        return;

    RustMacroRule *visible = rust_macro_definition_for(ctx, mname, invocation);
    if (!visible)
        return;

    /* Try each rule from the lexically selected definition until one matches;
     * first matching rule wins. */
    RustMacroRule *hit = NULL;

    /* Extract the invocation argument text. */
    const char *inv_args = NULL;
    int inv_args_len = 0;
    uint32_t inv_src_start = 0;
    if (!ts_node_is_null(invocation)) {
        TSNode args_tt = ts_node_child_by_field_name(invocation, "arguments", 9);
        if (ts_node_is_null(args_tt)) {
            uint32_t child_count = ts_node_child_count(invocation);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(invocation, i);
                if (!ts_node_is_null(child) && strcmp(ts_node_type(child), "token_tree") == 0) {
                    args_tt = child;
                    break;
                }
            }
        }
        if (!ts_node_is_null(args_tt)) {
            char *at = cbm_node_text(ctx->arena, args_tt, ctx->source);
            if (at) {
                rust_macro_strip_outer(at, (int)strlen(at), &inv_args, &inv_args_len);
                inv_src_start = ts_node_start_byte(args_tt) + (uint32_t)(inv_args - at);
            }
        }
    }

    if (!inv_args)
        return;
    MacroEnv *env = (MacroEnv *)cbm_calloc(CBM_MEM_CLASS_OTHER, sizeof(*env));
    if (!env)
        return;
    for (int i = 0; i < ctx->macro_rules_count; i++) {
        RustMacroRule *r = ctx->macro_rules_arr[i];
        if (strcmp(r->macro_name, mname) != 0 || r->external_id != visible->external_id ||
            r->definition_start_byte != visible->definition_start_byte ||
            r->scope_start_byte != visible->scope_start_byte ||
            r->scope_end_byte != visible->scope_end_byte || !r->pattern_text) {
            continue;
        }
        env->count = 0;
        env->rep_count = 0;
        int matched = macro_pattern_match(r->pattern_text, r->pattern_len, inv_args, inv_args_len,
                                          env);
        if (matched == MACRO_UNMODELLED)
            break;
        if (matched >= 0) {
            hit = r;
            break;
        }
    }
    if (!hit || !hit->transcriber_text || hit->transcriber_len <= 0) {
        cbm_free(CBM_MEM_CLASS_OTHER, env);
        return;
    }

    /* Substitute the bound metavars into the transcriber body. A fragment
     * copied from the invocation keeps its original bytes, so calls inside it
     * map back to their source site; transcriber text has no such site. */
    static const char expand_prefix[] = "fn __cbm_macro_expand() { ";
    RustSiteWindows windows = {.arena = ctx->arena};
    RustMacroSubstSites sites = {.ctx = ctx,
                                 .inv = inv_args,
                                 .inv_len = inv_args_len,
                                 .inv_src_start = inv_src_start,
                                 .windows = &windows};
    char *substituted = macro_substitute(ctx->arena, hit->transcriber_text, hit->transcriber_len,
                                         env, &sites, (uint32_t)strlen(expand_prefix));
    cbm_free(CBM_MEM_CLASS_OTHER, env);
    if (!substituted)
        return;

    /* Expansion memo (see RustLSPContext.macro_memo): reset per top-level
     * invocation, dedup identical (macro, substituted body) within the
     * recursion chain — kills the exponential breadth of self-recursive
     * macro_rules without losing any distinctly-attributed source site. */
    if (ctx->macro_expand_depth == 0 && ctx->macro_memo.slots) {
        memset(ctx->macro_memo.slots, 0, sizeof(uint64_t) * (size_t)ctx->macro_memo.cap);
        ctx->macro_memo.count = 0;
    }
    uint64_t mm_key = cbm_negmemo_key(3, mname, substituted);
    if (cbm_negmemo_contains(&ctx->macro_memo, mm_key)) {
        return;
    }
    cbm_negmemo_insert(&ctx->macro_memo, ctx->arena, mm_key);

    /* Wrap and parse. */
    char *wrapped = cbm_arena_sprintf(ctx->arena, "%s%s; }\n", expand_prefix, substituted);
    if (!wrapped)
        return;

    TSParser *parser = ts_parser_new();
    if (!parser)
        return;
    ts_parser_set_language(parser, tree_sitter_rust());
    TSTree *tree = ts_parser_parse_string(parser, NULL, wrapped, (uint32_t)strlen(wrapped));
    if (tree) {
        ctx->macro_expand_depth++;
        TSNode root = ts_tree_root_node(tree);
        uint32_t rnc = ts_node_child_count(root);
        for (uint32_t i = 0; i < rnc; i++) {
            TSNode top = ts_node_child(root, i);
            if (ts_node_is_null(top))
                continue;
            if (strcmp(ts_node_type(top), "function_item") != 0)
                continue;
            TSNode body = ts_node_child_by_field_name(top, "body", 4);
            if (ts_node_is_null(body))
                continue;
            /* Swap source so node_text inside reads from `wrapped`. Only the
             * substituted fragments' windows map back to the original file;
             * a call there has no syntactic call node, so one is injected. */
            const char *saved_source = ctx->source;
            int saved_len = ctx->source_len;
            bool saved_map_active = ctx->site_map_active;
            const CBMRustSiteWindow *saved_windows = ctx->site_windows;
            int saved_window_count = ctx->site_window_count;
            uint32_t saved_emit_start = ctx->emit_site_start_byte;
            uint32_t saved_emit_end = ctx->emit_site_end_byte;
            ctx->source = wrapped;
            ctx->source_len = (int)strlen(wrapped);
            ctx->site_map_active = true;
            ctx->site_windows = windows.items;
            ctx->site_window_count = windows.count;
            ctx->emit_site_start_byte = 0;
            ctx->emit_site_end_byte = 0;
            ctx->inject_syn_calls++;
            rust_resolve_calls_in_node(ctx, body);
            ctx->inject_syn_calls--;
            ctx->source = saved_source;
            ctx->source_len = saved_len;
            ctx->site_map_active = saved_map_active;
            ctx->site_windows = saved_windows;
            ctx->site_window_count = saved_window_count;
            ctx->emit_site_start_byte = saved_emit_start;
            ctx->emit_site_end_byte = saved_emit_end;
        }
        ctx->macro_expand_depth--;
        ts_tree_delete(tree);
    }
    ts_parser_delete(parser);
}

/* Re-parse the arguments of a built-in macro whose arguments are ordinary Rust
 * expressions laid out in a macro-specific grammar (format!, vec!, json!,
 * matches!, tokio::select!, tracing::info!, …) and walk them for calls. The
 * tree-sitter-rust grammar tokenises macro arguments rather than parsing them
 * as expression AST, so a method call like `format!("{}", d.label())` never
 * appears as a call_expression node and rust_walk_macro_tokens cannot recover
 * it. Because these macros DO evaluate the expressions, re-parsing them and
 * resolving calls in them recovers exactly the calls the program makes.
 *
 * Each form turns the argument text into one or more regions. A region is a
 * byte range of the argument text whose non-expression syntax (a JSON key's
 * `:`, a tracing `%` sigil, a select! future) is masked with bytes of the same
 * length, wrapped in a prefix and suffix that make it a Rust function body.
 * Equal lengths keep the single-offset site map exact. The current scope
 * (params/locals) is preserved so typed receivers still resolve. */
typedef enum {
    RUST_MACRO_ARGS_EXPR_LIST,   /* format!/assert!: `a, b` as a tuple */
    RUST_MACRO_ARGS_ARRAY_BODY,  /* vec!: `a, b` or `x; n` */
    RUST_MACRO_ARGS_JSON,        /* json!: a JSON value with expression leaves */
    RUST_MACRO_ARGS_MATCHES,     /* matches!/assert_matches!: `expr, pat [if guard]` */
    RUST_MACRO_ARGS_SELECT,      /* tokio::select!: `pat = fut [, if c] => handler, …` */
    RUST_MACRO_ARGS_FUTURE_LIST, /* join!/try_join!: `[biased;] a, b` */
    RUST_MACRO_ARGS_PIN,         /* pin!: `a, b` or `let x = init;` */
    RUST_MACRO_ARGS_TRACING,     /* tracing/log: `target: t, k = %v, ?v, "fmt", args` */
    RUST_MACRO_ARGS_SNAPSHOT,    /* insta assert_*snapshot!: `[name,] value [, @"inline"]` */
    RUST_MACRO_ARGS_STATEMENTS,  /* insta::allow_duplicates!: a statement list */
    RUST_MACRO_ARGS_SETTINGS,    /* insta::with_settings!: `{settings}, body` */
} RustMacroArgForm;

static const char RUST_MACRO_ARGS_FN[] = "fn __cbm_macro_args() { ";
static const char RUST_MACRO_ARGS_TUPLE[] = "fn __cbm_macro_args() { let _ = (";
static const char RUST_MACRO_ARGS_ARRAY[] = "fn __cbm_macro_args() { let _ = [";

static bool rust_arg_ident_byte(char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           ((unsigned char)c >= 0x80);
}

/* End of the lexical unit at `i`: a whole string, raw string, char literal or
 * comment, so brackets and separators inside it are never seen; otherwise the
 * next byte. */
static int rust_arg_lex_unit(const char *s, int len, int i) {
    char c = s[i];
    if (c == '/' && i + 1 < len && s[i + 1] == '/') {
        while (i < len && s[i] != '\n')
            i++;
        return i;
    }
    if (c == '/' && i + 1 < len && s[i + 1] == '*') {
        i += 2;
        while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/'))
            i++;
        return i + 2 < len ? i + 2 : len;
    }
    if (c == '"') {
        i++;
        while (i < len && s[i] != '"')
            i += (s[i] == '\\' && i + 1 < len) ? 2 : 1;
        return i < len ? i + 1 : len;
    }
    if (c == 'r' && i + 1 < len && (s[i + 1] == '"' || s[i + 1] == '#')) {
        int pre = i - 1;
        if (pre >= 0 && (s[pre] == 'b' || s[pre] == 'c'))
            pre--;
        if (pre < 0 || !rust_arg_ident_byte(s[pre])) {
            int p = i + 1;
            int hashes = 0;
            while (p < len && s[p] == '#') {
                hashes++;
                p++;
            }
            if (p < len && s[p] == '"') {
                p++;
                while (p < len) {
                    if (s[p] == '"') {
                        int h = 0;
                        while (h < hashes && p + 1 + h < len && s[p + 1 + h] == '#')
                            h++;
                        if (h == hashes)
                            return p + 1 + hashes;
                    }
                    p++;
                }
                return len;
            }
        }
        return i + 1;
    }
    if (c == '\'') {
        int q = i + 1;
        if (q < len && s[q] == '\\') {
            q += 2;
            while (q < len && s[q] != '\'')
                q++;
            return q < len ? q + 1 : len;
        }
        if (q + 1 < len && s[q + 1] == '\'')
            return q + 2;
        if (q < len && (unsigned char)s[q] >= 0x80) {
            int p = q;
            while (p < len && p < q + 5 && s[p] != '\'')
                p++;
            if (p < len && s[p] == '\'')
                return p + 1;
        }
        return i + 1; /* lifetime or label */
    }
    return i + 1;
}

/* First depth-0 index in [from, to) holding `want` (',', ';', ':' or '='),
 * or `to`. A ':' must not be part of `::`; a '=' must be a lone `=` (not
 * `==`, `=>`, `<=`, `!=`, `+=`, …). */
static int rust_arg_find_top(const char *s, int from, int to, char want) {
    int depth = 0;
    int i = from;
    while (i < to) {
        char c = s[i];
        int next = rust_arg_lex_unit(s, to, i);
        if (next != i + 1) {
            i = next;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            depth++;
        } else if (c == ')' || c == ']' || c == '}') {
            depth--;
        } else if (depth == 0 && c == want) {
            char prev = i > from ? s[i - 1] : ' ';
            char after = i + 1 < to ? s[i + 1] : ' ';
            if (want == ':' && (prev == ':' || after == ':')) {
                i += (after == ':') ? 2 : 1;
                continue;
            }
            if (want == '=' && (after == '=' || after == '>' || strchr("=!<>+-*/%^&|", prev))) {
                i++;
                continue;
            }
            return i;
        }
        i++;
    }
    return to;
}

/* First depth-0 `=>` in [from, to), or `to`. */
static int rust_arg_find_arrow(const char *s, int from, int to) {
    int depth = 0;
    int i = from;
    while (i < to) {
        char c = s[i];
        int next = rust_arg_lex_unit(s, to, i);
        if (next != i + 1) {
            i = next;
            continue;
        }
        if (c == '(' || c == '[' || c == '{')
            depth++;
        else if (c == ')' || c == ']' || c == '}')
            depth--;
        else if (depth == 0 && c == '=' && i + 1 < to && s[i + 1] == '>' &&
                 (i == from || s[i - 1] != '='))
            return i;
        i++;
    }
    return to;
}

/* Index just past the bracket group opening at `open`, or `to`. */
static int rust_arg_group_end(const char *s, int open, int to) {
    int depth = 0;
    int i = open;
    while (i < to) {
        char c = s[i];
        int next = rust_arg_lex_unit(s, to, i);
        if (next != i + 1) {
            i = next;
            continue;
        }
        if (c == '(' || c == '[' || c == '{')
            depth++;
        else if (c == ')' || c == ']' || c == '}') {
            if (--depth == 0)
                return i + 1;
        }
        i++;
    }
    return to;
}

static int rust_arg_skip_ws(const char *s, int from, int to) {
    while (from < to) {
        int next = rust_arg_lex_unit(s, to, from);
        bool comment = next > from + 1 && s[from] == '/';
        if (!comment && s[from] != ' ' && s[from] != '\t' && s[from] != '\n' && s[from] != '\r')
            break;
        from = comment ? next : from + 1;
    }
    return from;
}

/* True when the keyword `kw` stands alone at `at`. */
static bool rust_arg_keyword_at(const char *s, int at, int to, const char *kw) {
    int n = (int)strlen(kw);
    return at + n <= to && strncmp(s + at, kw, (size_t)n) == 0 &&
           (at + n == to || !rust_arg_ident_byte(s[at + n]));
}

static void rust_arg_blank(char *s, int from, int to) {
    for (int i = from; i < to; i++) {
        if (s[i] != '\n')
            s[i] = ' ';
    }
}

/* A JSON value of json!: `{ key: value, … }` becomes the array `[ key, value,
 * … ]`, `[ … ]` keeps its elements, and any other value is a Rust expression
 * that runs as written. */
static void rust_json_mask_value(char *s, int from, int to) {
    int p = rust_arg_skip_ws(s, from, to);
    if (p >= to || (s[p] != '{' && s[p] != '['))
        return;
    bool object = s[p] == '{';
    int end = rust_arg_group_end(s, p, to);
    if (end > to || s[end - 1] != (object ? '}' : ']'))
        return;
    if (object) {
        s[p] = '[';
        s[end - 1] = ']';
    }
    int entry = p + 1;
    while (entry < end - 1) {
        int comma = rust_arg_find_top(s, entry, end - 1, ',');
        int value = entry;
        if (object) {
            int colon = rust_arg_find_top(s, entry, comma, ':');
            if (colon < comma) {
                s[colon] = ',';
                value = colon + 1;
            }
        }
        rust_json_mask_value(s, value, comma);
        entry = comma + 1;
    }
}

/* tracing / log arguments: `target: t`, `parent: p` and `name: n` lose their
 * label, `field.path = v` and `"field" = v` lose their name, a `%`/`?` sigil
 * is dropped, and a log key-value `;` separator becomes `,`. */
static void rust_tracing_mask(char *s, int len) {
    int seg = 0;
    while (seg < len) {
        int comma = rust_arg_find_top(s, seg, len, ',');
        int semi = rust_arg_find_top(s, seg, comma, ';');
        int end = semi < comma ? semi : comma;
        if (semi < comma)
            s[semi] = ',';
        int p = rust_arg_skip_ws(s, seg, end);
        int q = p;
        if (q < end && s[q] == '"') {
            q = rust_arg_lex_unit(s, end, q);
        } else {
            while (q < end && (rust_arg_ident_byte(s[q]) || s[q] == '.'))
                q++;
        }
        if (q > p) {
            int after = rust_arg_skip_ws(s, q, end);
            bool label = after < end && s[after] == ':' && (after + 1 >= end || s[after + 1] != ':') &&
                         s[p] != '"';
            bool field = after < end && rust_arg_find_top(s, after, end, '=') == after;
            if (label || field) {
                rust_arg_blank(s, p, after + 1);
                p = rust_arg_skip_ws(s, after + 1, end);
            }
        }
        if (p < end && (s[p] == '%' || s[p] == '?'))
            s[p] = ' ';
        seg = end + 1;
    }
}

/* Re-parse one region of the argument text. `text` is a masked copy of the
 * argument text whose byte 0 sits at `src_start` in the current source. */
static void rust_reparse_macro_region(RustLSPContext *ctx, uint32_t src_start, const char *text,
                                      int start, int len, const char *prefix, const char *suffix) {
    if (len <= 0)
        return;
    char *region = cbm_arena_strndup(ctx->arena, text + start, (size_t)len);
    if (!region)
        return;
    char *wrapped = cbm_arena_sprintf(ctx->arena, "%s%s%s }\n", prefix, region, suffix);
    if (!wrapped)
        return;

    /* `region` has the length of its source range, and only masked bytes
     * differ. Carry the windows of that range that reach the original file
     * (all of it in a file's own source; the substituted fragments inside a
     * macro_rules! expansion) into the synthetic wrapper. */
    uint32_t region_start = src_start + (uint32_t)start;
    RustSiteWindows windows = {.arena = ctx->arena};
    rust_site_windows_carry(ctx, &windows, region_start, region_start + (uint32_t)len,
                            (uint32_t)strlen(prefix));
    bool mapped = windows.count > 0;

    /* Same expansion memo as rust_expand_user_macro (site tag 4), but ONLY for
     * text generated by a macro_rules! transcriber (unmapped, depth > 0): there
     * the identical argument text is re-parsed on every re-expansion of a
     * self-recursive macro and has no distinct source site. A mapped region is
     * one source occurrence — a built-in macro nested in another one's
     * arguments, `vec![sse(vec![ev("a")])]` — and identical text at another
     * occurrence is another site, so it is always walked. */
    if (!mapped && ctx->macro_expand_depth > 0) {
        uint64_t am_key = cbm_negmemo_key(4, wrapped, NULL);
        if (cbm_negmemo_contains(&ctx->macro_memo, am_key))
            return;
        cbm_negmemo_insert(&ctx->macro_memo, ctx->arena, am_key);
    }

    TSParser *parser = ts_parser_new();
    if (!parser)
        return;
    ts_parser_set_language(parser, tree_sitter_rust());
    TSTree *tree = ts_parser_parse_string(parser, NULL, wrapped, (uint32_t)strlen(wrapped));
    if (!tree) {
        ts_parser_delete(parser);
        return;
    }
    TSNode root = ts_tree_root_node(tree);
    /* Bail out if the synthetic source failed to parse cleanly — a
     * format-string-only arg, named args, or other non-expression token
     * soup must not produce bogus edges. */
    TSNode top = ts_node_named_child_count(root) == 1 ? ts_node_named_child(root, 0) : (TSNode){0};
    TSNode body = (!ts_node_is_null(top) && strcmp(ts_node_type(top), "function_item") == 0)
                      ? ts_node_child_by_field_name(top, "body", 4)
                      : (TSNode){0};
    if (!ts_node_has_error(root) && !ts_node_is_null(body)) {
        ctx->macro_expand_depth++;
        const char *saved_source = ctx->source;
        int saved_len = ctx->source_len;
        bool saved_map_active = ctx->site_map_active;
        const CBMRustSiteWindow *saved_windows = ctx->site_windows;
        int saved_window_count = ctx->site_window_count;
        ctx->source = wrapped;
        ctx->source_len = (int)strlen(wrapped);
        ctx->site_map_active = true;
        ctx->site_windows = windows.items;
        ctx->site_window_count = windows.count;
        /* The syntactic extractor never produced call nodes for these
         * macro-hidden expressions, so resolved_calls emitted here have
         * no CBMCall to pair with. Inject matching synthetic calls. */
        ctx->inject_syn_calls++;
        rust_resolve_calls_in_node(ctx, body);
        ctx->inject_syn_calls--;
        ctx->source = saved_source;
        ctx->source_len = saved_len;
        ctx->site_map_active = saved_map_active;
        ctx->site_windows = saved_windows;
        ctx->site_window_count = saved_window_count;
        ctx->macro_expand_depth--;
    }
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

/* tokio::select!: each branch `pat = fut [, if cond] => handler` runs `fut`
 * and `cond` in the enclosing scope, then `handler` with `pat` bound to the
 * future's output, whose type is not modelled. The branch re-parses as a
 * match arm on an untyped scrutinee, so `pat`'s names shadow outer locals
 * without a type. */
static void rust_reparse_select_branches(RustLSPContext *ctx, uint32_t src_start, char *s,
                                         int len) {
    static const char arm_prefix[] = "fn __cbm_macro_args() { match __cbm_select_output { ";
    int p = rust_arg_skip_ws(s, 0, len);
    if (rust_arg_keyword_at(s, p, len, "biased")) {
        int semi = rust_arg_skip_ws(s, p + 6, len);
        if (semi < len && s[semi] == ';')
            p = semi + 1;
    }
    while ((p = rust_arg_skip_ws(s, p, len)) < len) {
        int arrow = rust_arg_find_arrow(s, p, len);
        if (arrow >= len)
            return;
        int handler = rust_arg_skip_ws(s, arrow + 2, len);
        int handler_end = (handler < len && s[handler] == '{')
                              ? rust_arg_group_end(s, handler, len)
                              : rust_arg_find_top(s, handler, len, ',');
        if (rust_arg_keyword_at(s, p, len, "else") && rust_arg_skip_ws(s, p + 4, len) == arrow) {
            rust_reparse_macro_region(ctx, src_start, s, handler, handler_end - handler,
                                      RUST_MACRO_ARGS_TUPLE, ");");
        } else {
            int eq = rust_arg_find_top(s, p, arrow, '=');
            if (eq >= arrow)
                return;
            int fut_end = arrow;
            int comma = rust_arg_find_top(s, eq + 1, arrow, ',');
            if (comma < arrow) {
                int cond = rust_arg_skip_ws(s, comma + 1, arrow);
                if (!rust_arg_keyword_at(s, cond, arrow, "if"))
                    return;
                fut_end = comma;
                rust_reparse_macro_region(ctx, src_start, s, cond + 2, arrow - cond - 2,
                                          RUST_MACRO_ARGS_TUPLE, ");");
            }
            rust_reparse_macro_region(ctx, src_start, s, eq + 1, fut_end - eq - 1, RUST_MACRO_ARGS_TUPLE,
                                      ");");
            char *arm = cbm_arena_strndup(ctx->arena, s, (size_t)len);
            if (!arm)
                return;
            rust_arg_blank(arm, eq, arrow);
            rust_reparse_macro_region(ctx, src_start, arm, p, handler_end - p, arm_prefix, " };");
        }
        p = rust_arg_skip_ws(s, handler_end, len);
        if (p < len && s[p] == ',')
            p++;
    }
}

static void rust_resolve_macro_arg_exprs(RustLSPContext *ctx, TSNode invocation,
                                         RustMacroArgForm form) {
    if (!ctx || ts_node_is_null(invocation))
        return;
    if (ctx->macro_expand_depth >= 8)
        return;

    /* The grammar exposes the argument list as a `token_tree` child rather
     * than via an `arguments` field, so locate it by node type. */
    TSNode args_tt = {0};
    uint32_t inc = ts_node_child_count(invocation);
    for (uint32_t i = 0; i < inc; i++) {
        TSNode c = ts_node_child(invocation, i);
        if (!ts_node_is_null(c) && strcmp(ts_node_type(c), "token_tree") == 0) {
            args_tt = c;
            break;
        }
    }
    if (ts_node_is_null(args_tt))
        return;
    char *at = cbm_node_text(ctx->arena, args_tt, ctx->source);
    if (!at)
        return;
    const char *inner;
    int len;
    rust_macro_strip_outer(at, (int)strlen(at), &inner, &len);
    if (len <= 0)
        return;
    char *s = cbm_arena_strndup(ctx->arena, inner, (size_t)len);
    if (!s)
        return;
    uint32_t src_start = ts_node_start_byte(args_tt) + (uint32_t)(inner - at);

    switch (form) {
    case RUST_MACRO_ARGS_EXPR_LIST:
        /* A tuple keeps a trailing format-spec arg like `width = w` valid. */
        rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_TUPLE, ");");
        break;
    case RUST_MACRO_ARGS_ARRAY_BODY:
        rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_ARRAY, "];");
        break;
    case RUST_MACRO_ARGS_JSON:
        rust_json_mask_value(s, 0, len);
        rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_ARRAY, "];");
        break;
    case RUST_MACRO_ARGS_MATCHES: {
        /* `expr, pat [if guard] [, message…]` is the match `match expr { pat
         * [if guard] => () }`: the guard sees the pattern's bindings.
         * assert_matches!'s `expr, pat [if guard] => arm` already is one. */
        int comma = rust_arg_find_top(s, 0, len, ',');
        if (comma >= len)
            break;
        int pat_end = rust_arg_find_top(s, comma + 1, len, ',');
        bool has_arm = rust_arg_find_arrow(s, comma + 1, pat_end) < pat_end;
        if (has_arm)
            pat_end = len;
        s[comma] = '{';
        static const char match_prefix[] = "fn __cbm_macro_args() { match ";
        rust_reparse_macro_region(ctx, src_start, s, 0, pat_end, match_prefix,
                                  has_arm ? " };" : " => () };");
        break;
    }
    case RUST_MACRO_ARGS_SELECT:
        rust_reparse_select_branches(ctx, src_start, s, len);
        break;
    case RUST_MACRO_ARGS_FUTURE_LIST: {
        int p = rust_arg_skip_ws(s, 0, len);
        if (rust_arg_keyword_at(s, p, len, "biased")) {
            int semi = rust_arg_skip_ws(s, p + 6, len);
            if (semi < len && s[semi] == ';')
                rust_arg_blank(s, p, semi + 1);
        }
        rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_TUPLE, ");");
        break;
    }
    case RUST_MACRO_ARGS_PIN: {
        /* `pin!(let x = init;)` declares; `pin!(a, b)` names expressions. */
        int p = rust_arg_skip_ws(s, 0, len);
        if (rust_arg_keyword_at(s, p, len, "let"))
            rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_FN, "");
        else
            rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_TUPLE, ");");
        break;
    }
    case RUST_MACRO_ARGS_TRACING:
        rust_tracing_mask(s, len);
        rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_TUPLE, ");");
        break;
    case RUST_MACRO_ARGS_SNAPSHOT:
        /* The inline snapshot `@"…"` / `@r#"…"#` is a literal once its `@`
         * is blanked; the name and the value are expressions. */
        for (int seg = 0; seg < len;) {
            int comma = rust_arg_find_top(s, seg, len, ',');
            int p = rust_arg_skip_ws(s, seg, comma);
            if (p < comma && s[p] == '@')
                s[p] = ' ';
            seg = comma + 1;
        }
        rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_TUPLE, ");");
        break;
    case RUST_MACRO_ARGS_STATEMENTS:
        rust_reparse_macro_region(ctx, src_start, s, 0, len, RUST_MACRO_ARGS_FN, "");
        break;
    case RUST_MACRO_ARGS_SETTINGS: {
        /* The settings map `{key => value, …}` is not an expression; the body
         * after it is a block or closure expression. */
        int comma = rust_arg_find_top(s, 0, len, ',');
        if (comma < len)
            rust_reparse_macro_region(ctx, src_start, s, comma + 1, len - comma - 1,
                                      RUST_MACRO_ARGS_TUPLE, ");");
        break;
    }
    }
}

/* Argument form of a built-in macro named by `mname` (`json`,
 * `serde_json::json`, `tokio::select`, …), or false when its arguments are not
 * plain expressions in a known grammar. The path qualifier must be absent or
 * name the crate that defines the macro. */
static bool rust_builtin_macro_arg_form(const char *mname, RustMacroArgForm *form) {
    if (strncmp(mname, "::", 2) == 0)
        mname += 2;
    const char *leaf = mname;
    for (const char *p = mname; (p = strstr(p, "::")) != NULL; p += 2)
        leaf = p + 2;
    size_t qual_len = leaf > mname ? (size_t)(leaf - mname - 2) : 0;
    static const struct {
        const char *leaf;
        const char *quals; /* space-separated crate paths; "" alone = unqualified only */
        RustMacroArgForm form;
    } table[] = {
        {"println", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"eprintln", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"print", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"eprint", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"format", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"write", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"writeln", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"panic", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"assert", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"assert_eq", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"assert_ne", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"debug_assert", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"debug_assert_eq", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"debug_assert_ne", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"dbg", "", RUST_MACRO_ARGS_EXPR_LIST},
        {"vec", "", RUST_MACRO_ARGS_ARRAY_BODY},
        {"json", "serde_json", RUST_MACRO_ARGS_JSON},
        {"matches", "std core", RUST_MACRO_ARGS_MATCHES},
        {"assert_matches", "assert_matches std::assert_matches core::assert_matches",
         RUST_MACRO_ARGS_MATCHES},
        {"debug_assert_matches", "assert_matches std::assert_matches core::assert_matches",
         RUST_MACRO_ARGS_MATCHES},
        {"select", "tokio futures", RUST_MACRO_ARGS_SELECT},
        {"join", "tokio futures", RUST_MACRO_ARGS_FUTURE_LIST},
        {"try_join", "tokio futures", RUST_MACRO_ARGS_FUTURE_LIST},
        {"pin", "tokio std::pin core::pin", RUST_MACRO_ARGS_PIN},
        {"trace", "tracing log", RUST_MACRO_ARGS_TRACING},
        {"debug", "tracing log", RUST_MACRO_ARGS_TRACING},
        {"info", "tracing log", RUST_MACRO_ARGS_TRACING},
        {"warn", "tracing log", RUST_MACRO_ARGS_TRACING},
        {"error", "tracing log", RUST_MACRO_ARGS_TRACING},
        {"event", "tracing", RUST_MACRO_ARGS_TRACING},
        {"log", "log", RUST_MACRO_ARGS_TRACING},
        {"span", "tracing", RUST_MACRO_ARGS_TRACING},
        {"trace_span", "tracing", RUST_MACRO_ARGS_TRACING},
        {"debug_span", "tracing", RUST_MACRO_ARGS_TRACING},
        {"info_span", "tracing", RUST_MACRO_ARGS_TRACING},
        {"warn_span", "tracing", RUST_MACRO_ARGS_TRACING},
        {"error_span", "tracing", RUST_MACRO_ARGS_TRACING},
        {"ensure", "anyhow eyre", RUST_MACRO_ARGS_EXPR_LIST},
        {"bail", "anyhow eyre", RUST_MACRO_ARGS_EXPR_LIST},
        {"anyhow", "anyhow", RUST_MACRO_ARGS_EXPR_LIST},
        {"eyre", "eyre", RUST_MACRO_ARGS_EXPR_LIST},
        {"format_err", "anyhow eyre", RUST_MACRO_ARGS_EXPR_LIST},
        {"assert_snapshot", "insta", RUST_MACRO_ARGS_SNAPSHOT},
        {"assert_debug_snapshot", "insta", RUST_MACRO_ARGS_SNAPSHOT},
        {"assert_display_snapshot", "insta", RUST_MACRO_ARGS_SNAPSHOT},
        {"assert_compact_debug_snapshot", "insta", RUST_MACRO_ARGS_SNAPSHOT},
        {"allow_duplicates", "insta", RUST_MACRO_ARGS_STATEMENTS},
        {"with_settings", "insta", RUST_MACRO_ARGS_SETTINGS},
        {"queue", "crossterm", RUST_MACRO_ARGS_EXPR_LIST},
        {"execute", "crossterm", RUST_MACRO_ARGS_EXPR_LIST},
        {"line", "ratatui ratatui_macros", RUST_MACRO_ARGS_ARRAY_BODY},
        {"text", "ratatui ratatui_macros", RUST_MACRO_ARGS_ARRAY_BODY},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(leaf, table[i].leaf) != 0)
            continue;
        if (qual_len == 0) {
            *form = table[i].form;
            return true;
        }
        for (const char *q = table[i].quals; *q;) {
            size_t n = strcspn(q, " ");
            if (n == qual_len && strncmp(q, mname, n) == 0) {
                *form = table[i].form;
                return true;
            }
            q += n;
            while (*q == ' ')
                q++;
        }
        return false;
    }
    return false;
}

/* ════════════════════════════════════════════════════════════════════
 * 9. Statement / pattern binding
 * ════════════════════════════════════════════════════════════════════ */

/* The positional sub-patterns of a tuple, tuple-struct or slice pattern, in
 * source order: every child except the delimiters, attributes and a
 * tuple-struct pattern's path. `_` is an anonymous token and still holds a
 * position. Returns the count written to `out` (at most `cap`). */
static int rust_pattern_elements(TSNode pattern, TSNode *out, int cap) {
    int n = 0;
    uint32_t nc = ts_node_child_count(pattern);
    for (uint32_t i = 0; i < nc && n < cap; i++) {
        TSNode c = ts_node_child(pattern, i);
        const char *ck = ts_node_type(c);
        const char *field = ts_node_field_name_for_child(pattern, i);
        if ((field && strcmp(field, "type") == 0) || strcmp(ck, "attribute_item") == 0) {
            continue;
        }
        if (!ts_node_is_named(c) && strcmp(ck, "_") != 0) {
            continue;
        }
        out[n++] = c;
    }
    return n;
}

/* The position a sub-pattern takes in a value of `total` positions when the
 * pattern lists `count` elements with a `..` at `rest` (-1: none). Elements
 * after the `..` count from the end. -1 when the position is not known. */
static int rust_pattern_position(int index, int count, int rest, int total) {
    if (rest < 0 || index < rest) {
        return index;
    }
    if (total < 0) {
        return -1;
    }
    return total - (count - index);
}

/* The registered QN of the type a pattern destructures, after peeling the
 * references the match ergonomics bind through. NULL when not a named type. */
static const char *rust_pattern_owner_qn(RustLSPContext *ctx, const CBMType *base) {
    const char *qn = NULL;
    if (base && base->kind == CBM_TYPE_NAMED) {
        qn = base->data.named.qualified_name;
    } else if (base && base->kind == CBM_TYPE_TEMPLATE) {
        qn = base->data.template_type.template_name;
    }
    if (!qn) {
        return NULL;
    }
    const char *registered = rust_registered_type_qn(ctx, qn);
    return registered ? registered : qn;
}

/* The last path segment of a pattern's type (`E::V`, `V`, `Self::V`,
 * `E::<T>::V`), or NULL. */
static const char *rust_pattern_path_leaf(RustLSPContext *ctx, TSNode pattern) {
    TSNode tn = ts_node_child_by_field_name(pattern, "type", 4);
    if (ts_node_is_null(tn)) {
        return NULL;
    }
    if (strcmp(ts_node_type(tn), "generic_type") == 0) {
        TSNode inner = ts_node_child_by_field_name(tn, "type", 4);
        if (!ts_node_is_null(inner)) {
            tn = inner;
        }
    }
    if (strcmp(ts_node_type(tn), "scoped_identifier") == 0 ||
        strcmp(ts_node_type(tn), "scoped_type_identifier") == 0) {
        TSNode name = ts_node_child_by_field_name(tn, "name", 4);
        if (!ts_node_is_null(name)) {
            tn = name;
        }
    }
    return rust_node_text(ctx, tn);
}

/* `Option` or `Result` when `qn` names the std one, else NULL. */
static const char *rust_std_sum_type(const char *qn) {
    if (!qn) {
        return NULL;
    }
    if (strcmp(qn, "core.option.Option") == 0 || strcmp(qn, "std.option.Option") == 0 ||
        strcmp(qn, "Option") == 0) {
        return "Option";
    }
    if (strcmp(qn, "core.result.Result") == 0 || strcmp(qn, "std.result.Result") == 0 ||
        strcmp(qn, "Result") == 0) {
        return "Result";
    }
    return NULL;
}

/* The type of payload position `pos` of variant (or tuple struct) `leaf`
 * of the matched value `base`: `Some`/`Ok`/`Err` of std's Option and Result
 * from the template arguments; a corpus enum variant from its registered
 * payload field `<Variant>.<pos>`; a tuple struct, or a tuple alias matched
 * by a tuple pattern (`leaf` NULL), from its field `<pos>`. Unknown when the
 * position's type is not known. */
static const CBMType *rust_tuple_variant_field(RustLSPContext *ctx, const CBMType *base,
                                               const char *leaf, int pos) {
    if (!base || pos < 0) {
        return cbm_type_unknown();
    }
    if (base->kind == CBM_TYPE_TEMPLATE && leaf) {
        const char *sum = rust_std_sum_type(base->data.template_type.template_name);
        int argc = base->data.template_type.arg_count;
        if (sum && pos == 0) {
            int arg = -1;
            if (strcmp(sum, "Option") == 0 && strcmp(leaf, "Some") == 0) {
                arg = 0;
            } else if (strcmp(sum, "Result") == 0 && strcmp(leaf, "Ok") == 0) {
                arg = 0;
            } else if (strcmp(sum, "Result") == 0 && strcmp(leaf, "Err") == 0) {
                arg = 1;
            }
            return arg >= 0 && arg < argc ? base->data.template_type.template_args[arg]
                                          : cbm_type_unknown();
        }
        if (sum) {
            return cbm_type_unknown();
        }
    }
    const char *owner = rust_pattern_owner_qn(ctx, base);
    if (!owner) {
        return cbm_type_unknown();
    }
    const char *dot = strrchr(owner, '.');
    const char *owner_leaf = dot ? dot + 1 : owner;
    const char *field = !leaf || strcmp(leaf, owner_leaf) == 0 || strcmp(leaf, "Self") == 0
                            ? cbm_arena_sprintf(ctx->arena, "%d", pos)
                            : cbm_arena_sprintf(ctx->arena, "%s.%d", leaf, pos);
    const CBMType *ft = rust_lookup_field(ctx, owner, field, 0);
    return ft ? ft : cbm_type_unknown();
}

/* The type of named field `fname` of the struct or struct-like variant
 * `leaf` of the matched value `base`. NULL when not known. */
static const CBMType *rust_struct_pattern_field(RustLSPContext *ctx, const char *owner,
                                                const char *leaf, const char *fname) {
    if (!owner || !fname) {
        return NULL;
    }
    if (leaf) {
        const CBMType *vt =
            rust_lookup_field(ctx, owner, cbm_arena_sprintf(ctx->arena, "%s.%s", leaf, fname), 0);
        if (vt) {
            return vt;
        }
    }
    return rust_lookup_field(ctx, owner, fname, 0);
}

/* A binding made through a reference with the default binding mode is a
 * reference to the matched part. */
static const CBMType *rust_pattern_bind_through(RustLSPContext *ctx, const CBMType *part,
                                                bool by_ref) {
    if (!by_ref || !part || cbm_type_is_unknown(part)) {
        return part;
    }
    return cbm_type_reference(ctx->arena, part);
}

/* The number of positions of a tuple variant or tuple struct payload, or -1. */
static int rust_tuple_variant_arity(RustLSPContext *ctx, const CBMType *base, const char *leaf) {
    int n = 0;
    while (n < 64 && !cbm_type_is_unknown(rust_tuple_variant_field(ctx, base, leaf, n))) {
        n++;
    }
    return n > 0 ? n : -1;
}

/* Recursively bind every identifier inside a pattern node to the type its
 * position has in the matched value of type `type`, and to unknown where
 * that position's type is not known. Handles tuple_pattern, slice_pattern,
 * struct_pattern, tuple_struct_pattern, ref_pattern, mut_pattern,
 * reference_pattern, captured_pattern, or_pattern, identifier. */
static void rust_bind_pattern(RustLSPContext *ctx, TSNode pattern, const CBMType *type) {
    if (ts_node_is_null(pattern))
        return;
    const char *kind = ts_node_type(pattern);

    if (strcmp(kind, "identifier") == 0) {
        char *name = rust_node_text(ctx, pattern);
        if (name && strcmp(name, "_") != 0) {
            cbm_scope_bind(ctx->current_scope, name, type);
        }
        return;
    }
    if (strcmp(kind, "captured_pattern") == 0) {
        /* name @ subpattern */
        TSNode name_node = ts_node_child_by_field_name(pattern, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char *name = rust_node_text(ctx, name_node);
            if (name && strcmp(name, "_") != 0) {
                cbm_scope_bind(ctx->current_scope, name, type);
            }
        }
        TSNode sub = ts_node_child_by_field_name(pattern, "pattern", 7);
        if (!ts_node_is_null(sub))
            rust_bind_pattern(ctx, sub, type);
        return;
    }
    if (strcmp(kind, "ref_pattern") == 0 || strcmp(kind, "mut_pattern") == 0 ||
        strcmp(kind, "reference_pattern") == 0) {
        /* The sub-pattern is the last named child: `mut x` and `&mut x`
         * carry a `mutable_specifier` before it. `&p` matches through one
         * reference; `ref p` binds a reference to the part. */
        const CBMType *sub_t = type;
        if (strcmp(kind, "reference_pattern") == 0) {
            sub_t = type && type->kind == CBM_TYPE_REFERENCE ? type->data.reference.elem
                                                             : cbm_type_unknown();
        } else if (strcmp(kind, "ref_pattern") == 0) {
            sub_t = rust_pattern_bind_through(ctx, type, true);
        }
        uint32_t nc = ts_node_named_child_count(pattern);
        if (nc > 0) {
            rust_bind_pattern(ctx, ts_node_named_child(pattern, nc - 1), sub_t);
        }
        return;
    }
    if (strcmp(kind, "tuple_pattern") == 0 || strcmp(kind, "tuple_struct_pattern") == 0 ||
        strcmp(kind, "slice_pattern") == 0) {
        /* `(a, b)` takes tuple positions, `V(a, b)` the variant's payload
         * positions, `[a, b]` slice elements. A `..` skips positions;
         * the elements after it count from the end. */
        const CBMType *base = type;
        bool by_ref = false;
        while (base && base->kind == CBM_TYPE_REFERENCE) {
            base = base->data.reference.elem;
            by_ref = true;
        }
        TSNode elems[64];
        int count = rust_pattern_elements(pattern, elems, 64);
        int rest = -1;
        for (int i = 0; i < count; i++) {
            if (strcmp(ts_node_type(elems[i]), "remaining_field_pattern") == 0) {
                rest = i;
                break;
            }
        }
        const char *leaf = strcmp(kind, "tuple_struct_pattern") == 0
                               ? rust_pattern_path_leaf(ctx, pattern)
                               : NULL;
        bool slice = strcmp(kind, "slice_pattern") == 0;
        int total = -1;
        if (rest >= 0 && base && base->kind == CBM_TYPE_TUPLE && !leaf) {
            total = base->data.tuple.count;
        } else if (rest >= 0 && !slice) {
            total = rust_tuple_variant_arity(ctx, base, leaf);
        }
        for (int i = 0; i < count; i++) {
            if (i == rest) {
                continue;
            }
            const CBMType *part = cbm_type_unknown();
            if (slice) {
                if (base && base->kind == CBM_TYPE_SLICE) {
                    part = base->data.slice.elem;
                }
                /* `rest @ ..` binds the remaining sub-slice. */
                if (strcmp(ts_node_type(elems[i]), "captured_pattern") == 0) {
                    TSNode sub = ts_node_child_by_field_name(elems[i], "pattern", 7);
                    if (!ts_node_is_null(sub) &&
                        strcmp(ts_node_type(sub), "remaining_field_pattern") == 0) {
                        part = base && base->kind == CBM_TYPE_SLICE ? base : cbm_type_unknown();
                    }
                }
            } else {
                int pos = rust_pattern_position(i, count, rest, total);
                if (!leaf && base && base->kind == CBM_TYPE_TUPLE) {
                    if (pos >= 0 && pos < base->data.tuple.count) {
                        part = base->data.tuple.elems[pos];
                    }
                } else {
                    part = rust_tuple_variant_field(ctx, base, leaf, pos);
                }
            }
            rust_bind_pattern(ctx, elems[i], rust_pattern_bind_through(ctx, part, by_ref));
        }
        return;
    }
    if (strcmp(kind, "match_pattern") == 0) {
        if (ts_node_named_child_count(pattern) > 0)
            rust_bind_pattern(ctx, ts_node_named_child(pattern, 0), type);
        return;
    }
    if (strcmp(kind, "struct_pattern") == 0) {
        /* For each field_pattern, bind the local name to the field's type. */
        TSNode body = ts_node_child_by_field_name(pattern, "body", 4);
        TSNode iter = ts_node_is_null(body) ? pattern : body;
        const CBMType *base = type;
        bool by_ref = false;
        while (base && base->kind == CBM_TYPE_REFERENCE) {
            base = base->data.reference.elem;
            by_ref = true;
        }
        const char *type_qn = rust_pattern_owner_qn(ctx, base);
        const char *leaf = rust_pattern_path_leaf(ctx, pattern);

        uint32_t nc = ts_node_named_child_count(iter);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode fp = ts_node_named_child(iter, i);
            const char *fk = ts_node_type(fp);
            if (strcmp(fk, "field_pattern") == 0) {
                TSNode name_node = ts_node_child_by_field_name(fp, "name", 4);
                TSNode pat_node = ts_node_child_by_field_name(fp, "pattern", 7);
                char *fname = rust_node_text(ctx, name_node);
                if (!fname)
                    continue;
                const CBMType *ft = rust_struct_pattern_field(ctx, type_qn, leaf, fname);
                ft = rust_pattern_bind_through(ctx, ft ? ft : cbm_type_unknown(), by_ref);
                if (!ts_node_is_null(pat_node)) {
                    rust_bind_pattern(ctx, pat_node, ft);
                } else {
                    cbm_scope_bind(ctx->current_scope, fname, ft);
                }
            } else if (strcmp(fk, "shorthand_field_identifier") == 0 ||
                       strcmp(fk, "identifier") == 0) {
                char *fname = rust_node_text(ctx, fp);
                if (fname && strcmp(fname, "_") != 0) {
                    const CBMType *ft = rust_struct_pattern_field(ctx, type_qn, leaf, fname);
                    cbm_scope_bind(
                        ctx->current_scope, fname,
                        rust_pattern_bind_through(ctx, ft ? ft : cbm_type_unknown(), by_ref));
                }
            }
        }
        return;
    }
    if (strcmp(kind, "or_pattern") == 0) {
        /* For an OR pattern we attempt to bind names from the first branch. */
        if (ts_node_named_child_count(pattern) > 0) {
            rust_bind_pattern(ctx, ts_node_named_child(pattern, 0), type);
        }
        return;
    }
    /* Other patterns we ignore for binding purposes. */
}

static const char *rust_resolve_callable_value(RustLSPContext *ctx, TSNode value,
                                               const char **source_name);

void rust_process_statement(RustLSPContext *ctx, TSNode node) {
    if (ts_node_is_null(node))
        return;
    const char *kind = ts_node_type(node);

    /* let_declaration: let pat: T = expr;
     *
     * Bidirectional inference: if the user wrote `let v: Vec<String> = …;`
     * we pass `Vec<String>` as the expected hint when synthesising the
     * RHS. That lets ambiguous calls like `Vec::new()` keep their full
     * template arguments through the chain. */
    if (strcmp(kind, "let_declaration") == 0) {
        TSNode pat = ts_node_child_by_field_name(node, "pattern", 7);
        TSNode tn = ts_node_child_by_field_name(node, "type", 4);
        TSNode val = ts_node_child_by_field_name(node, "value", 5);

        const CBMType *annotated = NULL;
        if (!ts_node_is_null(tn)) {
            annotated = rust_parse_type_node(ctx, tn);
        }
        const CBMType *let_type = annotated;
        if ((!let_type || cbm_type_is_unknown(let_type)) && !ts_node_is_null(val)) {
            /* Synthesis: evaluate the RHS with the (possibly NULL)
             * annotated type as a hint. */
            let_type = rust_eval_expr_typed(ctx, val, annotated);
        }
        if (!let_type)
            let_type = cbm_type_unknown();
        if (!ts_node_is_null(pat))
            rust_bind_pattern(ctx, pat, let_type);
        if (!ts_node_is_null(pat) && strcmp(ts_node_type(pat), "identifier") == 0 &&
            !ts_node_is_null(val)) {
            char *binding_name = rust_node_text(ctx, pat);
            const char *value_kind = ts_node_type(val);
            const char *callable_qn = NULL;
            if (strcmp(value_kind, "identifier") == 0) {
                char *value_name = rust_node_text(ctx, val);
                if (value_name && cbm_scope_contains(ctx->current_scope, value_name)) {
                    callable_qn = cbm_scope_lookup_callable(ctx->current_scope, value_name);
                } else if (value_name) {
                    const char *resolved = rust_resolve_path_expr(ctx, value_name);
                    const CBMRegisteredFunc *function =
                        resolved ? cbm_registry_lookup_func(ctx->registry, resolved) : NULL;
                    if (!function && resolved && ctx->module_qn) {
                        function = cbm_registry_lookup_func(
                            ctx->registry,
                            cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, resolved));
                    }
                    callable_qn = function ? function->qualified_name : NULL;
                }
            } else if (strcmp(value_kind, "scoped_identifier") == 0) {
                char *value_path = rust_node_text(ctx, val);
                const char *resolved = value_path ? rust_resolve_path_expr(ctx, value_path) : NULL;
                const CBMRegisteredFunc *function =
                    resolved ? cbm_registry_lookup_func(ctx->registry, resolved) : NULL;
                callable_qn = function ? function->qualified_name : NULL;
            }
            if (binding_name && callable_qn) {
                cbm_scope_bind_callable(ctx->current_scope, binding_name, let_type, callable_qn);
            }
        }
        return;
    }

    if (strcmp(kind, "assignment_expression") == 0 ||
        strcmp(kind, "compound_assignment_expr") == 0) {
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        TSNode right = ts_node_child_by_field_name(node, "right", 5);
        if (!ts_node_is_null(left) && strcmp(ts_node_type(left), "identifier") == 0) {
            char *name = rust_node_text(ctx, left);
            if (name && cbm_scope_contains(ctx->current_scope, name)) {
                const char *target = NULL;
                if (strcmp(kind, "assignment_expression") == 0 &&
                    ctx->callable_control_flow_depth == 0 && !ts_node_is_null(right)) {
                    target = rust_resolve_callable_value(ctx, right, NULL);
                }
                cbm_scope_update_callable(ctx->current_scope, name, target);
            }
        }
        return;
    }

    /* const_item / static_item: const NAME: T = …; */
    if (strcmp(kind, "const_item") == 0 || strcmp(kind, "static_item") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode tn = ts_node_child_by_field_name(node, "type", 4);
        const CBMType *type =
            ts_node_is_null(tn) ? cbm_type_unknown() : rust_parse_type_node(ctx, tn);
        if (!ts_node_is_null(name_node)) {
            char *name = rust_node_text(ctx, name_node);
            if (name)
                cbm_scope_bind(ctx->current_scope, name, type);
        }
        return;
    }
}

/* ════════════════════════════════════════════════════════════════════
 * 10. Function & file walk
 * ════════════════════════════════════════════════════════════════════ */

/* Inject a synthetic CBMCall into result->calls so the downstream pipeline
 * (cbm_pipeline_find_lsp_resolution) can pair it with the resolved_call and
 * emit a CALLS edge. `callee_qn`'s last dot segment is used as the textual
 * callee_name, matching how the resolver's short-name comparison works. Only
 * used for calls the syntactic extractor cannot see (operator desugaring,
 * macro-hidden method calls). */
static bool rust_map_source_range(const RustLSPContext *ctx, uint32_t start, uint32_t end,
                                  uint32_t *mapped_start, uint32_t *mapped_end) {
    if (!ctx || !mapped_start || !mapped_end || end <= start)
        return false;
    if (ctx->site_map_active) {
        for (int i = 0; i < ctx->site_window_count; i++) {
            const CBMRustSiteWindow *w = &ctx->site_windows[i];
            if (start >= w->synthetic_start && end <= w->synthetic_end) {
                *mapped_start = w->original_start + (start - w->synthetic_start);
                *mapped_end = w->original_start + (end - w->synthetic_start);
                return true;
            }
        }
        return false;
    }
    /* Only the immutable root buffer has identity coordinates. */
    if (ctx->source != ctx->root_source)
        return false;
    *mapped_start = start;
    *mapped_end = end;
    return true;
}

static const char *rust_callee_short_name(RustLSPContext *ctx, const char *callee_qn) {
    const char *dot = strrchr(callee_qn, '.');
    const char *short_name = dot ? dot + 1 : callee_qn;
    const char *impl_suffix = strchr(short_name, '[');
    return impl_suffix ? cbm_arena_strndup(ctx->arena, short_name,
                                          (size_t)(impl_suffix - short_name))
                       : short_name;
}

static void rust_inject_syn_call(RustLSPContext *ctx, const char *callee_qn) {
    if (!ctx || !ctx->syn_calls || !callee_qn || !ctx->enclosing_func_qn)
        return;
    const char *short_name = rust_callee_short_name(ctx, callee_qn);
    if (!short_name || !short_name[0])
        return;
    CBMCall call = {0};
    call.callee_name = cbm_arena_strdup(ctx->arena, short_name);
    call.enclosing_func_qn = ctx->enclosing_func_qn;
    if (ctx->emit_site_end_byte > ctx->emit_site_start_byte) {
        call.site_start_byte = ctx->emit_site_start_byte;
        call.site_end_byte = ctx->emit_site_end_byte;
        call.callee_start_byte = ctx->emit_callee_start_byte;
        call.callee_end_byte = ctx->emit_callee_end_byte;
        call.requires_lsp_resolution = true;
    }
    cbm_calls_push(ctx->syn_calls, ctx->arena, call);
}

/* The generic call extractor intentionally filters Rust built-in macro names
 * that live in its keyword table (`println`, `panic`, ...). Once the LSP has
 * proven a canonical macro target, restore exactly one source-occurrence
 * carrier and require that semantic result: a same-named local function must
 * never win through the legacy fallback. Non-keyword macros already have a
 * parser carrier, so leave them alone to avoid duplicate cross-pass carriers. */
static void rust_ensure_known_macro_carrier(RustLSPContext *ctx, const char *macro_name,
                                            const char *callee_qn) {
    if (!ctx || !ctx->syn_calls || !macro_name || !callee_qn ||
        !cbm_is_keyword(macro_name, CBM_LANG_RUST) ||
        ctx->emit_site_end_byte <= ctx->emit_site_start_byte) {
        return;
    }

    const char *short_name = rust_callee_short_name(ctx, callee_qn);
    for (int i = 0; i < ctx->syn_calls->count; i++) {
        CBMCall *call = &ctx->syn_calls->items[i];
        if (!call->callee_name || !call->enclosing_func_qn ||
            strcmp(call->enclosing_func_qn, ctx->enclosing_func_qn) != 0 ||
            (strcmp(call->callee_name, macro_name) != 0 &&
             strcmp(call->callee_name, short_name) != 0) ||
            call->site_start_byte != ctx->emit_site_start_byte ||
            call->site_end_byte != ctx->emit_site_end_byte) {
            continue;
        }
        call->requires_lsp_resolution = true;
        return;
    }
    rust_inject_syn_call(ctx, callee_qn);
}

static void rust_emit_resolved_call_reason(RustLSPContext *ctx, const char *callee_qn,
                                           const char *callee_impl_key, const char *strategy,
                                           float confidence, const char *reason) {
    if (!ctx || !ctx->resolved_calls || !callee_qn || !ctx->enclosing_func_qn)
        return;
    CBMResolvedCall rc = {
        .caller_qn = ctx->enclosing_func_qn,
        .callee_qn = callee_qn,
        .caller_impl_key = ctx->enclosing_impl_key,
        .callee_impl_key = callee_impl_key,
        .strategy = strategy,
        .confidence = confidence,
        .reason = reason,
        .kind = CBM_RESOLVED_INVOCATION,
        .site_start_byte = ctx->emit_site_start_byte,
        .site_end_byte = ctx->emit_site_end_byte,
    };
    cbm_resolvedcall_push(ctx->resolved_calls, ctx->arena, rc);
    if (ctx->inject_syn_calls > 0 && ctx->emit_site_end_byte > ctx->emit_site_start_byte) {
        rust_inject_syn_call(ctx, callee_qn);
    }
}

static void rust_emit_resolved_call(RustLSPContext *ctx, const char *callee_qn,
                                    const char *callee_impl_key, const char *strategy,
                                    float confidence) {
    rust_emit_resolved_call_reason(ctx, callee_qn, callee_impl_key, strategy, confidence, NULL);
}

static bool rust_qn_has_suffix(const char *qualified_name, const char *suffix) {
    if (!qualified_name || !suffix) {
        return false;
    }
    size_t qn_len = strlen(qualified_name);
    size_t suffix_len = strlen(suffix);
    return qn_len >= suffix_len && strcmp(qualified_name + qn_len - suffix_len, suffix) == 0 &&
           (qn_len == suffix_len || qualified_name[qn_len - suffix_len - 1U] == '.');
}

/* Resolve only an exact identifier/scoped-path value. No branch, closure, or
 * other composite expression enters this helper. A suffix fallback is admitted
 * only for a qualified import path and only when it identifies one free func. */
static const char *rust_resolve_callable_value(RustLSPContext *ctx, TSNode value,
                                               const char **source_name) {
    const char *kind = ts_node_type(value);
    if (strcmp(kind, "identifier") != 0 && strcmp(kind, "scoped_identifier") != 0) {
        return NULL;
    }
    char *path = rust_node_text(ctx, value);
    if (!path || !path[0]) {
        return NULL;
    }
    if (source_name) {
        *source_name = path;
    }
    if (strcmp(kind, "identifier") == 0 && cbm_scope_contains(ctx->current_scope, path)) {
        return cbm_scope_lookup_callable(ctx->current_scope, path);
    }
    const char *resolved = rust_resolve_path_expr(ctx, path);
    if (!resolved) {
        return NULL;
    }
    const CBMRegisteredFunc *function = cbm_registry_lookup_func(ctx->registry, resolved);
    if (!function && ctx->module_qn) {
        function = cbm_registry_lookup_func(
            ctx->registry, cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, resolved));
    }
    if (function && !function->receiver_type) {
        return function->qualified_name;
    }
    if (!strchr(resolved, '.')) {
        return NULL;
    }
    const char *tail = strrchr(resolved, '.');
    tail = tail ? tail + 1 : resolved;
    const CBMRegisteredFunc *unique = NULL;
    int matches = 0;
    CBMFreeFuncIter iter;
    cbm_registry_free_funcs_by_short_name(ctx->registry, tail, &iter);
    for (int index; matches < 2 && (index = cbm_free_func_iter_next(&iter)) >= 0;) {
        const CBMRegisteredFunc *candidate = &ctx->registry->funcs[index];
        if (!candidate->receiver_type && candidate->qualified_name &&
            rust_qn_has_suffix(candidate->qualified_name, resolved)) {
            unique = candidate;
            matches++;
        }
    }
    return matches == 1 && unique ? unique->qualified_name : NULL;
}

static void rust_resolve_callable_argument_references(RustLSPContext *ctx, TSNode arguments) {
    if (ts_node_is_null(arguments)) {
        return;
    }
    uint32_t count = ts_node_named_child_count(arguments);
    for (uint32_t i = 0; i < count; i++) {
        TSNode value = ts_node_named_child(arguments, i);
        const char *source_name = NULL;
        const char *target = rust_resolve_callable_value(ctx, value, &source_name);
        if (!target) {
            continue;
        }
        uint32_t start = 0;
        uint32_t end = 0;
        if (!rust_map_source_range(ctx, ts_node_start_byte(value), ts_node_end_byte(value), &start,
                                   &end)) {
            continue;
        }
        CBMResolvedCall reference = {
            .caller_qn = ctx->enclosing_func_qn,
            .callee_qn = target,
            .caller_impl_key = ctx->enclosing_impl_key,
            .strategy = "lsp_callable_value_reference",
            .confidence = CBM_RUST_CONF_DIRECT,
            .reason = source_name,
            .kind = CBM_RESOLVED_CALL_REFERENCE,
            .site_start_byte = start,
            .site_end_byte = end,
        };
        cbm_resolvedcall_push(ctx->resolved_calls, ctx->arena, reference);
    }
}


static void rust_emit_unresolved_call(RustLSPContext *ctx, const char *expr_text,
                                      const char *reason) {
    if (!ctx || !ctx->resolved_calls || !ctx->enclosing_func_qn)
        return;
    CBMResolvedCall rc = {
        .caller_qn = ctx->enclosing_func_qn,
        .callee_qn = expr_text ? expr_text : "?",
        .caller_impl_key = ctx->enclosing_impl_key,
        .strategy = "lsp_unresolved",
        .confidence = 0.0f,
        .reason = reason,
        .kind = CBM_RESOLVED_INVOCATION,
        .site_start_byte = ctx->emit_site_start_byte,
        .site_end_byte = ctx->emit_site_end_byte,
    };
    cbm_resolvedcall_push(ctx->resolved_calls, ctx->arena, rc);
}

/* Entry hook implementation: classify a call_expression and emit the best
 * edge we can. The wrapper below establishes the exact original occurrence
 * for every return path through this function. */
static void rust_emit_call_target(RustLSPContext *ctx, const RustCallTarget *t) {
    if (t->qn) {
        rust_emit_resolved_call_reason(ctx, t->qn, t->impl_key, t->strategy, t->confidence,
                                       t->reason);
    } else if (t->open_qn) {
        rust_emit_unresolved_call(ctx, t->open_qn, t->open_reason);
    }
}

static void rust_call_target_set(RustCallTarget *t, const CBMRegisteredFunc *func, const char *qn,
                                 const char *strategy, float confidence) {
    t->func = func;
    t->qn = qn;
    t->impl_key = func ? func->impl_key : NULL;
    t->strategy = strategy;
    t->confidence = confidence;
}

/* The first argument of the call whose callee is `callee`, or a null node. */
static TSNode rust_call_first_argument(TSNode callee) {
    TSNode none = {0};
    TSNode call = ts_node_is_null(callee) ? none : ts_node_parent(callee);
    if (!ts_node_is_null(call) && strcmp(ts_node_type(call), "generic_function") == 0) {
        call = ts_node_parent(call);
    }
    if (ts_node_is_null(call) || strcmp(ts_node_type(call), "call_expression") != 0) {
        return none;
    }
    TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
    uint32_t n = ts_node_is_null(args) ? 0 : ts_node_named_child_count(args);
    for (uint32_t i = 0; i < n; i++) {
        TSNode arg = ts_node_named_child(args, i);
        const char *kind = ts_node_type(arg);
        if (strcmp(kind, "line_comment") != 0 && strcmp(kind, "block_comment") != 0) {
            return arg;
        }
    }
    return none;
}

/* The one method `trait_qn` contributes to `type_qn` under `method_name`:
 * the impl's own definition, else the trait's provided default when the type
 * implements the trait without overriding it. NULL when the type does not
 * implement the trait, or implements it more than once (generic arguments). */
static const CBMRegisteredFunc *rust_trait_method_for_type(RustLSPContext *ctx,
                                                           const char *type_qn,
                                                           const char *trait_qn,
                                                           const char *method_name) {
    const CBMTypeRegistry *reg = ctx->registry;
    const CBMRegisteredFunc *found = NULL;
    const char *found_impl = NULL;
    CBMMethodIter it;
    cbm_registry_methods(reg, type_qn, method_name, &it);
    for (int index; (index = cbm_method_iter_next(&it)) >= 0;) {
        const CBMRegisteredFunc *f = &reg->funcs[index];
        if ((f->flags & CBM_FUNC_FLAG_RUST_TRAIT_IMPL) == 0 || !f->impl_trait_qn ||
            strcmp(f->impl_trait_qn, trait_qn) != 0) {
            continue;
        }
        const char *impl = f->impl_key ? f->impl_key : f->impl_trait_qn;
        if (found_impl && strcmp(found_impl, impl) != 0) {
            return NULL;
        }
        found_impl = impl;
        found = found ? found : f;
    }
    if (found) {
        return found;
    }
    const CBMRegisteredType *t = cbm_registry_lookup_type(reg, type_qn);
    for (int i = 0; t && t->embedded_types && t->embedded_types[i]; i++) {
        if (strcmp(t->embedded_types[i], trait_qn) == 0) {
            return rust_lookup_method_in_trait_own(ctx, trait_qn, method_name, false);
        }
    }
    return NULL;
}

/* `Trait::method(recv, ..)` dispatches on the first argument's type: a
 * concrete type selects its impl of `trait_qn`; `dyn Trait`, `impl Trait`
 * or a type parameter bounded by the trait selects the trait's own
 * declaration. An argument of unknown type selects nothing. The first
 * argument is the receiver only when the selected method takes `self` (an
 * impl method's receiver matches the trait's, so this holds for a trait
 * declared outside the registry too): for an associated function
 * (`fn make(cfg: C) -> Self`) it is an ordinary argument and selects nothing. */
static const CBMRegisteredFunc *rust_trait_ufcs_by_receiver(RustLSPContext *ctx,
                                                            const char *trait_qn,
                                                            const char *method_name,
                                                            TSNode receiver) {
    if (ts_node_is_null(receiver)) {
        return NULL;
    }
    const CBMType *base = rust_eval_expr_type(ctx, receiver);
    while (base && (base->kind == CBM_TYPE_REFERENCE || base->kind == CBM_TYPE_POINTER)) {
        base = base->kind == CBM_TYPE_REFERENCE ? base->data.reference.elem
                                                : base->data.pointer.elem;
    }
    const char *type_qn = NULL;
    if (base && base->kind == CBM_TYPE_NAMED) {
        type_qn = base->data.named.qualified_name;
    } else if (base && base->kind == CBM_TYPE_TEMPLATE) {
        type_qn = base->data.template_type.template_name;
    }
    if (!type_qn) {
        return NULL;
    }
    const char *dot = strrchr(type_qn, '.');
    const char *bound = rust_lookup_type_param_bound(ctx, dot ? dot + 1 : type_qn);
    const char *registered = bound ? NULL : rust_registered_type_qn(ctx, type_qn);
    const CBMRegisteredType *t =
        registered ? cbm_registry_lookup_type(ctx->registry, registered) : NULL;
    const CBMRegisteredFunc *found = NULL;
    if (bound || (t && t->is_interface)) {
        found = strcmp(bound ? bound : registered, trait_qn) == 0
                    ? rust_lookup_method_in_trait_own(ctx, trait_qn, method_name, true)
                    : NULL;
    } else if (registered) {
        found = rust_trait_method_for_type(ctx, registered, trait_qn, method_name);
    }
    return found && (found->flags & CBM_FUNC_FLAG_RUST_SELF) ? found : NULL;
}

/* UFCS form `T::method` / `Trait::method` for a path already resolved to
 * `<type or trait QN>.<method>`; `first_arg` is the call's first argument
 * (null when unknown). True when the call was settled (a target found or
 * deliberately left open); false when no type or trait answers. */
static bool rust_ufcs_target(RustLSPContext *ctx, const char *qn, TSNode first_arg,
                             RustCallTarget *out) {
    const char *dot = strrchr(qn, '.');
    if (!dot) {
        return false;
    }
    char *head = cbm_arena_strndup(ctx->arena, qn, (size_t)(dot - qn));
    const char *short_name = dot + 1;
    /* If `head` is a trait, `Trait::method` UFCS resolves to the sole
     * concrete impl (lsp_trait_ufcs), NEVER the trait's own abstract
     * method that the inherent lookup below would find. Resolve the trait
     * QN (head or module-qualified) via its is_interface flag — set at
     * type-registration time, so it is reliable even on an early pass
     * before impl links are wired. When the impl isn't known yet, emit
     * nothing: a partial-pass lsp_ufcs to the abstract method would
     * otherwise outrank (higher conf) the real trait_ufcs from the
     * complete pass and win the join. */
    const char *trait_qn = NULL;
    const CBMRegisteredType *head_t = cbm_registry_lookup_type(ctx->registry, head);
    if (!head_t) {
        head_t = rust_lookup_type_variants(ctx, head);
        if (head_t) {
            head = cbm_arena_strdup(ctx->arena, head_t->qualified_name);
        }
    }
    if (head_t && head_t->is_interface) {
        trait_qn = head;
    } else if (ctx->module_qn) {
        const char *fh = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, head);
        const CBMRegisteredType *ft = cbm_registry_lookup_type(ctx->registry, fh);
        if (ft && ft->is_interface)
            trait_qn = fh;
    }
    if (trait_qn) {
        /* A typed first argument is the receiver `recv.method()` would
         * dispatch on, so the target carries the same proof and strategy. */
        const CBMRegisteredFunc *typed =
            rust_trait_ufcs_by_receiver(ctx, trait_qn, short_name, first_arg);
        if (typed) {
            rust_call_target_set(out, typed, typed->qualified_name, "lsp_trait_dispatch",
                                 CBM_RUST_CONF_UFCS);
            out->receiver_qn = typed->receiver_type;
            return true;
        }
        int tn = 0;
        const CBMRegisteredFunc *ti_m = rust_find_sole_trait_impl(ctx, trait_qn, short_name, &tn);
        if (tn == 1) {
            rust_call_target_set(
                out, ti_m,
                ti_m ? ti_m->qualified_name
                     : cbm_arena_sprintf(ctx->arena, "%s.%s", trait_qn, short_name),
                "lsp_trait_ufcs", CBM_RUST_CONF_TRAIT_SOLE);
            out->receiver_qn = ti_m ? ti_m->receiver_type : NULL;
        }
        return true;
    }
    const CBMRegisteredFunc *m = cbm_registry_lookup_method_aliased(ctx->registry, head, short_name);
    if (!m && ctx->module_qn) {
        /* Fall back to module-qualified head: `Logger.new` →
         * `<module>.Logger.new`. */
        const char *full_head = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, head);
        m = cbm_registry_lookup_method_aliased(ctx->registry, full_head, short_name);
    }
    if (m) {
        const CBMRegisteredFunc *unique =
            rust_registry_lookup_unique_method(ctx->registry, m->receiver_type, m->short_name);
        if (unique) {
            rust_call_target_set(out, unique, unique->qualified_name,
                                 strcmp(short_name, "new") == 0 ? "lsp_constructor" : "lsp_ufcs",
                                 CBM_RUST_CONF_UFCS);
            out->receiver_qn = unique->receiver_type;
        }
        return true;
    }
    /* Trait method through single-impl dispatch. */
    int impls = 0;
    const CBMRegisteredFunc *tm = rust_resolve_trait_method(ctx, head, short_name, &impls, NULL);
    const char *receiver = head;
    if (!tm && ctx->module_qn) {
        receiver = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, head);
        tm = rust_resolve_trait_method(ctx, receiver, short_name, &impls, NULL);
    }
    if (tm && impls == 1) {
        /* `Type::method()` names the receiver explicitly, so its sole
         * trait implementation is exact; only a trait-headed path
         * (`Trait::method()`) depends on inference and stays a guess. */
        rust_call_target_set(out, tm, tm->qualified_name,
                             head_t && !head_t->is_interface ? "lsp_ufcs" : "lsp_trait_ufcs",
                             CBM_RUST_CONF_TRAIT_SOLE);
        out->receiver_qn = receiver;
        return true;
    }
    return false;
}

static bool rust_resolve_ufcs_path(RustLSPContext *ctx, TSNode callee, const char *qn) {
    RustCallTarget t = {0};
    if (!rust_ufcs_target(ctx, qn, rust_call_first_argument(callee), &t)) {
        return false;
    }
    rust_emit_call_target(ctx, &t);
    return true;
}


/* Offset of `node` in the original file: identity in the root buffer,
 * mapped for reparsed macro arguments, the invocation site inside a
 * macro_rules! expansion. */
static bool rust_original_offset(const RustLSPContext *ctx, TSNode node, uint32_t *out) {
    uint32_t end = 0;
    if (rust_map_source_range(ctx, ts_node_start_byte(node), ts_node_end_byte(node), out, &end)) {
        return true;
    }
    if (ctx->macro_origin_valid) {
        *out = ctx->macro_origin_byte;
        return true;
    }
    return false;
}

static const char *rust_impl_self_path(CBMArena *arena, const char *spelling);
static bool rust_impl_has_type_param(RustLSPContext *ctx, TSNode impl_node, const char *name);

/* The module-level impl block whose body holds `node`, or a null node when
 * `node` sits in a trait, in a block-local impl or outside any impl. An item
 * nested in a method body cannot name the outer impl's `Self` (E0401), so
 * the innermost impl is the one `Self` refers to. */
static TSNode rust_enclosing_module_impl(TSNode node) {
    TSNode none = {0};
    for (TSNode cur = ts_node_parent(node); !ts_node_is_null(cur); cur = ts_node_parent(cur)) {
        const char *kind = ts_node_type(cur);
        if (strcmp(kind, "trait_item") == 0) {
            return none;
        }
        if (strcmp(kind, "impl_item") != 0) {
            continue;
        }
        TSNode parent = ts_node_parent(cur);
        const char *pk = ts_node_is_null(parent) ? "" : ts_node_type(parent);
        if (strcmp(pk, "source_file") == 0) {
            return cur;
        }
        TSNode holder = strcmp(pk, "declaration_list") == 0 ? ts_node_parent(parent) : none;
        return !ts_node_is_null(holder) && strcmp(ts_node_type(holder), "mod_item") == 0 ? cur
                                                                                          : none;
    }
    return none;
}

/* `Self::name` inside an impl body: `Self` is the impl's self type, so the
 * path is that type's associated item. The self type is resolved through the
 * module tree where the impl spells it, generic arguments dropped; true when
 * the tree names a project type, with `out` set as for any `Type::name`. */
static bool rust_self_path_target(RustLSPContext *ctx, TSNode callee, const char *path,
                                  RustCallTarget *out) {
    const CBMRustModTree *tree = ctx->imports ? ctx->imports->modtree : NULL;
    const char *name = path + 6;
    if (!tree || !ctx->module_qn || !name[0] || strstr(name, "::")) {
        return false;
    }
    TSNode impl = rust_enclosing_module_impl(callee);
    TSNode type_node =
        ts_node_is_null(impl) ? impl : ts_node_child_by_field_name(impl, "type", 4);
    uint32_t offset = 0;
    if (ts_node_is_null(type_node) || !rust_original_offset(ctx, type_node, &offset)) {
        return false;
    }
    char *spelling = rust_node_text(ctx, type_node);
    const char *self_path = spelling ? rust_impl_self_path(ctx->arena, spelling) : NULL;
    if (!self_path || rust_impl_has_type_param(ctx, impl, self_path)) {
        return false;
    }
    CBMRustPathResult r = cbm_rust_modtree_resolve_type(tree, ctx->arena, ctx->module_qn, offset,
                                                        self_path, NULL, NULL, 0);
    if (r.kind != CBM_RUST_PATH_TYPE) {
        return false;
    }
    /* Only a found function settles the call: a variant, an ambiguous or an
     * unregistered item keeps the receiver-based path resolution. */
    RustCallTarget found = {0};
    const char *qn = cbm_arena_sprintf(ctx->arena, "%s.%s", r.qn, name);
    if (!rust_ufcs_target(ctx, qn, rust_call_first_argument(callee), &found) || !found.func) {
        return false;
    }
    *out = found;
    out->names_function = true;
    return true;
}

/* The free function the module tree named by `qn` declared at `line`.
 * Every item of a file shares the file's module QN, so a name declared in
 * two modules of one file holds the plain node and `<qn>#L<line>` twins:
 * the twin at the declaring line is the item, and when no twin carries that
 * line the declaration kept the plain QN. Without a line (two declarations
 * in one module) or a namesake twin, the unique node of the name decides. */
static const CBMRegisteredFunc *rust_module_function_node(RustLSPContext *ctx, const char *qn,
                                                          uint32_t line) {
    RustQnMatch m = {0};
    rust_qn_probe(ctx, qn, false, &m);
    if (m.func || line == 0) {
        return m.func;
    }
    const CBMRegisteredFunc *f =
        cbm_registry_lookup_func(ctx->registry, cbm_rust_line_qualified_qn(ctx->arena, qn, line));
    return f ? f : cbm_registry_lookup_func(ctx->registry, qn);
}

/* Resolve a call path through the crate's declared module tree and `use`
 * graph into `out`. True when the tree settled the call; false leaves it to
 * the file-layout fallbacks (file outside every crate, undecidable name).
 * `out->names_function` is false when the tree proved the path names no
 * in-project function (a variant, a type, an external item). */
static bool rust_module_tree_target(RustLSPContext *ctx, TSNode callee, const char *path,
                                    RustCallTarget *out) {
    if (strncmp(path, "Self::", 6) == 0) {
        return rust_self_path_target(ctx, callee, path, out);
    }
    const CBMRustModTree *tree = ctx->imports ? ctx->imports->modtree : NULL;
    uint32_t offset = 0;
    if (!tree || !ctx->module_qn || ctx->glob_count > ctx->file_glob_count ||
        !rust_original_offset(ctx, callee, &offset)) {
        return false;
    }
    int block_uses = ctx->use_count > ctx->file_use_count ? ctx->use_count - ctx->file_use_count : 0;
    CBMRustPathResult r =
        cbm_rust_modtree_resolve(tree, ctx->arena, ctx->module_qn, offset, path,
                                 block_uses ? ctx->use_local_names + ctx->file_use_count : NULL,
                                 block_uses ? ctx->use_raw_paths + ctx->file_use_count : NULL,
                                 block_uses);
    switch (r.kind) {
    case CBM_RUST_PATH_FUNCTION: {
        out->names_function = true;
        const CBMRegisteredFunc *func = rust_module_function_node(ctx, r.qn, r.line);
        if (func && !func->receiver_type) {
            rust_call_target_set(out, func, func->qualified_name, "lsp_module_path",
                                 CBM_RUST_CONF_DIRECT);
            out->impl_key = NULL;
            out->reason = path;
        } else {
            out->open_qn = r.qn;
            out->open_reason = "module_function_not_registered";
        }
        return true;
    }
    case CBM_RUST_PATH_ASSOC: {
        out->names_function = true;
        const char *qn = cbm_arena_sprintf(ctx->arena, "%s.%s", r.qn, r.assoc);
        if (!rust_ufcs_target(ctx, qn, rust_call_first_argument(callee), out)) {
            out->open_qn = qn;
            out->open_reason = "associated_item_not_registered";
        }
        return true;
    }
    case CBM_RUST_PATH_NOT_FUNCTION:
        return true;
    case CBM_RUST_PATH_AMBIGUOUS:
        /* The name's target depends on the build configuration: no link. */
        out->open_qn = path;
        out->open_reason = "cfg_dependent_name";
        return true;
    default:
        return false;
    }
}

static bool rust_resolve_call_by_module_tree(RustLSPContext *ctx, TSNode callee, const char *path) {
    RustCallTarget t = {0};
    if (!rust_module_tree_target(ctx, callee, path, &t)) {
        return false;
    }
    rust_emit_call_target(ctx, &t);
    return true;
}

static void rust_resolve_call_expression_inner(RustLSPContext *ctx, TSNode node) {
    TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
    TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
    if (ts_node_is_null(func_node))
        return;
    rust_resolve_callable_argument_references(ctx, args_node);
    const char *fk = ts_node_type(func_node);

    /* Method call via field_expression. The grammar can also expose
     * `s.cast::<T>(x)` as a `generic_function` callee whose inner
     * `function` is a field_expression — peel that wrapper here. */
    if (strcmp(fk, "generic_function") == 0) {
        TSNode inner = ts_node_child_by_field_name(func_node, "function", 8);
        if (!ts_node_is_null(inner)) {
            func_node = inner;
            fk = ts_node_type(func_node);
        }
    }
    if (strcmp(fk, "field_expression") == 0) {
        TSNode value = ts_node_child_by_field_name(func_node, "value", 5);
        TSNode field = ts_node_child_by_field_name(func_node, "field", 5);
        if (ts_node_is_null(value) || ts_node_is_null(field))
            return;
        char *mname = rust_node_text(ctx, field);
        if (!mname)
            return;
        /* Strip turbofish from the method name in case the grammar
         * embedded it as part of the field token. */
        rust_strip_turbofish(mname);

        const CBMType *recv = rust_eval_expr_type(ctx, value);
        const CBMType *base = recv;
        while (base && (base->kind == CBM_TYPE_REFERENCE || base->kind == CBM_TYPE_POINTER)) {
            base = (base->kind == CBM_TYPE_REFERENCE) ? base->data.reference.elem
                                                      : base->data.pointer.elem;
        }

        const char *type_qn = NULL;
        if (base && base->kind == CBM_TYPE_NAMED)
            type_qn = base->data.named.qualified_name;
        else if (base && base->kind == CBM_TYPE_TEMPLATE)
            type_qn = base->data.template_type.template_name;
        else if (base && base->kind == CBM_TYPE_BUILTIN) {
            const char *nm = base->data.builtin.name;
            if (strcmp(nm, "str") == 0)
                type_qn = "core.str";
            else if (strcmp(nm, "String") == 0)
                type_qn = "alloc.string.String";
            else if (is_rust_primitive(nm)) {
                /* Primitive integer / float / char / bool / unit / never
                 * methods are registered under their primitive name. */
                type_qn = nm;
            }
        } else if (base && base->kind == CBM_TYPE_SLICE) {
            /* `&[T]` / `[T]` method dispatch. */
            type_qn = "core.slice";
        }

        bool wrapper_blocked = false;
        const CBMRegisteredFunc *wrapper_method =
            rust_wrapper_corpus_trait_method(ctx, base, mname, &wrapper_blocked);
        if (wrapper_method) {
            rust_emit_resolved_call(ctx, wrapper_method->qualified_name, wrapper_method->impl_key,
                                    "lsp_trait_dispatch", CBM_RUST_CONF_TRAIT_SOLE);
            return;
        }
        if (wrapper_blocked) {
            return;
        }
        if (type_qn) {
            const char *registered = rust_registered_type_qn(ctx, type_qn);
            if (registered) {
                type_qn = registered;
            }
            int impl_count = 0;
            bool trait_dispatch = false;
            const CBMRegisteredFunc *m =
                rust_resolve_trait_method(ctx, type_qn, mname, &impl_count, &trait_dispatch);
            if (m) {
                const char *strategy = "lsp_method_dispatch";
                float conf = CBM_RUST_CONF_METHOD;
                const CBMRegisteredType *method_owner =
                    m->receiver_type ? cbm_registry_lookup_type(ctx->registry, m->receiver_type)
                                     : NULL;
                if ((m->flags & CBM_FUNC_FLAG_RUST_TRAIT_IMPL) != 0 ||
                    (method_owner && method_owner->is_interface) || trait_dispatch) {
                    strategy = "lsp_trait_dispatch";
                    conf = (impl_count == 1) ? CBM_RUST_CONF_TRAIT_SOLE : CBM_RUST_CONF_TRAIT_AMB;
                }
                rust_emit_resolved_call(ctx, m->qualified_name, m->impl_key, strategy, conf);
                (void)args_node;
                return;
            }

            /* Walk the Deref chain — `Box<T>::method` may live on `T`,
             * `Rc<RefCell<T>>::method` peels two levels, etc. We bound
             * the chain at 8 hops to mirror the rust-analyzer cap. */
            const CBMType *cur = base;
            for (int hop = 0; hop < RUST_DEREF_MAX_HOPS && !rust_is_wrapper_trait_method(mname);
                 hop++) {
                const CBMType *next = rust_deref_step(ctx, cur);
                if (!next)
                    break;
                /* Unwrap reference layers introduced by deref. */
                while (next &&
                       (next->kind == CBM_TYPE_REFERENCE || next->kind == CBM_TYPE_POINTER)) {
                    next = (next->kind == CBM_TYPE_REFERENCE) ? next->data.reference.elem
                                                              : next->data.pointer.elem;
                }
                if (!next)
                    break;
                const CBMRegisteredFunc *hop_wrapper_method =
                    rust_wrapper_corpus_trait_method(ctx, next, mname, &wrapper_blocked);
                if (hop_wrapper_method) {
                    rust_emit_resolved_call(ctx, hop_wrapper_method->qualified_name,
                                            hop_wrapper_method->impl_key, "lsp_deref_dispatch",
                                            CBM_RUST_CONF_PROMOTED);
                    return;
                }
                if (wrapper_blocked)
                    return;
                const char *next_qn = NULL;
                if (next->kind == CBM_TYPE_NAMED)
                    next_qn = next->data.named.qualified_name;
                else if (next->kind == CBM_TYPE_TEMPLATE)
                    next_qn = next->data.template_type.template_name;
                else if (next->kind == CBM_TYPE_BUILTIN) {
                    const char *nm = next->data.builtin.name;
                    if (strcmp(nm, "str") == 0)
                        next_qn = "core.str";
                    else if (strcmp(nm, "String") == 0)
                        next_qn = "alloc.string.String";
                }
                if (!next_qn)
                    break;
                const char *next_registered = rust_registered_type_qn(ctx, next_qn);
                if (next_registered)
                    next_qn = next_registered;
                int hop_impls = 0;
                const CBMRegisteredFunc *hm =
                    rust_resolve_trait_method(ctx, next_qn, mname, &hop_impls, NULL);
                if (hm) {
                    rust_emit_resolved_call(ctx, hm->qualified_name, hm->impl_key,
                                            "lsp_deref_dispatch",
                                            CBM_RUST_CONF_PROMOTED);
                    return;
                }
                cur = next;
            }

            /* Chalk-lite: receiver is typed as a NAMED with a name
             * that matches a current type-param bound. Resolve through
             * the bound trait. */
            {
                /* Just the local tail name. */
                const char *short_qn = type_qn;
                const char *dot = strrchr(type_qn, '.');
                if (dot)
                    short_qn = dot + 1;
                const char *bound = rust_lookup_type_param_bound(ctx, short_qn);
                if (bound) {
                    int bimpls = 0;
                    const CBMRegisteredFunc *bm =
                        rust_resolve_trait_method(ctx, bound, mname, &bimpls, NULL);
                    if (bm) {
                        rust_emit_resolved_call(ctx, bm->qualified_name, bm->impl_key,
                                                "lsp_bound_dispatch",
                                                CBM_RUST_CONF_TRAIT_AMB);
                        return;
                    }
                }
            }

            /* Prelude trait method best-effort. */
            if (is_prelude_trait_method(mname)) {
                rust_emit_resolved_call(ctx,
                                        cbm_arena_sprintf(ctx->arena, "%s.%s", type_qn, mname),
                                        NULL, "lsp_prelude_trait", CBM_RUST_CONF_TRAIT_AMB);
                return;
            }
            rust_emit_unresolved_call(ctx, cbm_arena_sprintf(ctx->arena, "%s.%s", type_qn, mname),
                                      "method_not_found");
            return;
        }

        /* Receiver type is unknown — record best-effort with the textual
         * receiver path so downstream can still see what we tried. */
        char *recv_text = rust_node_text(ctx, value);
        rust_emit_unresolved_call(
            ctx, cbm_arena_sprintf(ctx->arena, "%s.%s", recv_text ? recv_text : "?", mname),
            "unknown_receiver_type");
        return;
    }

    /* Direct identifier or scoped path call. */
    if (strcmp(fk, "identifier") == 0 || strcmp(fk, "scoped_identifier") == 0 ||
        strcmp(fk, "generic_function") == 0) {
        TSNode actual_func = func_node;
        if (strcmp(fk, "generic_function") == 0) {
            TSNode inner = ts_node_child_by_field_name(actual_func, "function", 8);
            if (!ts_node_is_null(inner))
                actual_func = inner;
        }
        char *path = rust_node_text(ctx, actual_func);
        if (!path)
            return;
        /* Strip ALL turbofish (`Vec::<i32>::new` → `Vec::new`). */
        rust_strip_turbofish(path);

        if (strcmp(ts_node_type(actual_func), "identifier") == 0) {
            const char *alias_target = cbm_scope_lookup_callable(ctx->current_scope, path);
            if (alias_target) {
                rust_emit_resolved_call_reason(ctx, alias_target, NULL, "lsp_callable_alias",
                                               CBM_RUST_CONF_DIRECT, path);
                return;
            }
            if (cbm_scope_contains(ctx->current_scope, path)) {
                return;
            }
            /* A glob declared inside an enclosing block shadows the module's
             * items; its source is not modelled, so the name stays open. */
            if (ctx->glob_count > ctx->file_glob_count) {
                rust_emit_unresolved_call(ctx, path, "unresolved_glob_import");
                return;
            }
            /* The function the walked module itself declares under this
             * name; its file shares the name, so only the declaring module
             * tells the nodes apart. */
            const char *module_fn = rust_module_fn_target(ctx, path);
            if (module_fn) {
                rust_emit_resolved_call_reason(ctx, module_fn, NULL, "lsp_module_path",
                                               CBM_RUST_CONF_DIRECT, path);
                return;
            }
        }

        if (rust_resolve_call_by_module_tree(ctx, actual_func, path))
            return;
        const char *qn = rust_resolve_path_expr(ctx, path);
        if (!qn)
            return;

        /* Try registered free function first. Also try module-prefixed
         * fallback so `Logger::new` (which resolves to "Logger.new")
         * still finds the project's `<module>.Logger.new`. */
        const CBMRegisteredFunc *direct = rust_lookup_func_variants(ctx, qn);
        if (direct && !direct->receiver_type) {
            qn = direct->qualified_name;
            const char *strategy = rust_resolve_use(ctx, path) ? "lsp_import_alias" : "lsp_direct";
            rust_emit_resolved_call_reason(ctx, qn, NULL, strategy, CBM_RUST_CONF_DIRECT, path);
            return;
        }
        if (ctx->module_qn && strstr(qn, ".") == NULL) {
            const char *full = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, qn);
            direct = cbm_registry_lookup_func(ctx->registry, full);
            if (direct && !direct->receiver_type) {
                rust_emit_resolved_call(ctx, full, NULL, "lsp_direct", CBM_RUST_CONF_DIRECT);
                return;
            }
        }

        if (rust_resolve_ufcs_path(ctx, actual_func, qn))
            return;

        const char *workspace_path = path;
        if (strcmp(ts_node_type(actual_func), "identifier") == 0) {
            const char *import_target = rust_resolve_use(ctx, path);
            if (import_target)
                workspace_path = import_target;
        }
        const char *tail = strrchr(workspace_path, ':');
        if (tail && tail > workspace_path && tail[-1] == ':') {
            tail += 1;
        } else {
            tail = workspace_path;
        }

        /* Cross-crate workspace-member resolution (#56): when the call
         * path's head is a declared Cargo workspace member (e.g.
         * `crate_a::helper` from inside crate_b) we cannot rely on the
         * caller-crate-scoped fallback below — that filters by the
         * CALLER's module prefix and would resolve to a same-named local
         * function instead. Route to the function defined inside the
         * MEMBER crate by matching the registered QN's `.<member>.`
         * path segment plus the call tail. Requires a parsed manifest
         * (threaded through pass_lsp_cross.c); NULL manifest skips this. */
        if (ctx->cargo_manifest && tail && *tail) {
            const char *head_sep = strstr(workspace_path, "::");
            if (head_sep && head_sep > workspace_path) {
                char *head = cbm_arena_strndup(ctx->arena, workspace_path,
                                               (size_t)(head_sep - workspace_path));
                const CBMCargoManifest *m = (const CBMCargoManifest *)ctx->cargo_manifest;
                const CBMCargoMember *workspace_member =
                    head ? cbm_cargo_find_member(m, head) : NULL;
                bool has_library_target = false;
                for (int target_index = 0;
                     workspace_member && target_index < workspace_member->target_count;
                     target_index++) {
                    if (workspace_member->targets[target_index].kind == CBM_CARGO_TARGET_LIB) {
                        has_library_target = true;
                        break;
                    }
                }
                if (workspace_member && has_library_target) {
                    rust_emit_unresolved_call(ctx, workspace_path,
                                              "workspace_library_path_not_registered");
                    return;
                }
                if (workspace_member) {
                    /* `.crate_a.` — the member directory appears as a dotted
                     * QN segment for every def inside that crate. */
                    char *needle = cbm_arena_sprintf(ctx->arena, ".%s.", head);
                    const CBMRegisteredFunc *mem_unique = NULL;
                    int mem_matches = 0;
                    /* Iterate only free funcs whose short_name == tail via the index;
                     * the receiver/short_name/needle re-checks below are unchanged. */
                    CBMFreeFuncIter ffit;
                    cbm_registry_free_funcs_by_short_name(ctx->registry, tail, &ffit);
                    for (int i; mem_matches < 2 && (i = cbm_free_func_iter_next(&ffit)) >= 0;) {
                        const CBMRegisteredFunc *f = &ctx->registry->funcs[i];
                        if (!f->short_name || !f->qualified_name)
                            continue;
                        if (f->receiver_type)
                            continue; /* free functions only */
                        if (strcmp(f->short_name, tail) != 0)
                            continue;
                        if (!strstr(f->qualified_name, needle))
                            continue; /* not defined in the member crate */
                        mem_matches++;
                        if (mem_matches == 1)
                            mem_unique = f;
                    }
                    if (mem_matches == 1 && mem_unique) {
                        rust_emit_resolved_call(ctx, mem_unique->qualified_name,
                                                mem_unique->impl_key, "lsp_cross_crate",
                                                CBM_RUST_CONF_DIRECT);
                        return;
                    }
                }
            }
        }

        /* Glob imports: an item of the current module has already been
         * tried above and shadows every glob. Exactly one glob source may
         * define the bare name; two sources, or none, resolve nothing. */
        if (strcmp(ts_node_type(actual_func), "identifier") == 0 && ctx->glob_count > 0 &&
            !rust_resolve_use(ctx, path)) {
            const CBMRegisteredFunc *hit = NULL;
            int sources = 0;
            for (int g = 0; g < ctx->glob_count; g++) {
                const char *candidate =
                    cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->glob_module_qns[g], path);
                const CBMRegisteredFunc *f = rust_lookup_func_variants(ctx, candidate);
                if (f && !f->receiver_type && f != hit) {
                    hit = f;
                    sources++;
                }
            }
            if (sources == 1) {
                rust_emit_resolved_call_reason(ctx, hit->qualified_name, NULL, "lsp_glob_import",
                                               CBM_RUST_CONF_DIRECT, path);
                return;
            }
            rust_emit_unresolved_call(ctx, path, "unresolved_glob_import");
            return;
        }

        /* Last-ditch: emit with the resolved path. */
        rust_emit_unresolved_call(ctx, qn, "function_not_in_registry");
        return;
    }
}

static void rust_resolve_call_expression(RustLSPContext *ctx, TSNode node) {
    uint32_t saved_start = ctx->emit_site_start_byte;
    uint32_t saved_end = ctx->emit_site_end_byte;
    uint32_t saved_callee_start = ctx->emit_callee_start_byte;
    uint32_t saved_callee_end = ctx->emit_callee_end_byte;
    uint32_t mapped_start = 0;
    uint32_t mapped_end = 0;
    if (rust_map_source_range(ctx, ts_node_start_byte(node), ts_node_end_byte(node), &mapped_start,
                              &mapped_end)) {
        ctx->emit_site_start_byte = mapped_start;
        ctx->emit_site_end_byte = mapped_end;
    } else {
        ctx->emit_site_start_byte = 0;
        ctx->emit_site_end_byte = 0;
    }
    ctx->emit_callee_start_byte = 0;
    ctx->emit_callee_end_byte = 0;
    TSNode callee = ts_node_child_by_field_name(node, "function", 8);
    if (!ts_node_is_null(callee) && strcmp(ts_node_type(callee), "generic_function") == 0) {
        TSNode inner = ts_node_child_by_field_name(callee, "function", 8);
        if (!ts_node_is_null(inner)) {
            callee = inner;
        }
    }
    if (!ts_node_is_null(callee) && strcmp(ts_node_type(callee), "field_expression") == 0) {
        callee = ts_node_child_by_field_name(callee, "field", 5);
    } else if (!ts_node_is_null(callee) && strcmp(ts_node_type(callee), "scoped_identifier") == 0) {
        callee = ts_node_child_by_field_name(callee, "name", 4);
    }
    if (!ts_node_is_null(callee) &&
        rust_map_source_range(ctx, ts_node_start_byte(callee), ts_node_end_byte(callee),
                              &mapped_start, &mapped_end)) {
        ctx->emit_callee_start_byte = mapped_start;
        ctx->emit_callee_end_byte = mapped_end;
    }
    rust_resolve_call_expression_inner(ctx, node);
    ctx->emit_site_start_byte = saved_start;
    ctx->emit_site_end_byte = saved_end;
    ctx->emit_callee_start_byte = saved_callee_start;
    ctx->emit_callee_end_byte = saved_callee_end;
}

/* Walk every node in a function body, recording calls and refining scope
 * for control-flow constructs that bind variables. */
#define CBM_RUST_EVAL_STEP_CAP 200000 /* per-file budget */
static void rust_resolve_calls_in_node(RustLSPContext *ctx, TSNode node) {
    if (ts_node_is_null(node))
        return;
    /* Pathological-input guard: bail out once we've spent too many
     * eval steps on this file. Prevents hangs on adversarial input. */
    if (ctx->eval_step_count > CBM_RUST_EVAL_STEP_CAP)
        return;
    ctx->eval_step_count++;
    const char *kind = ts_node_type(node);
    if (strcmp(kind, "function_item") == 0 || strcmp(kind, "impl_item") == 0 ||
        strcmp(kind, "trait_item") == 0 || strcmp(kind, "mod_item") == 0) {
        rust_process_nested_item(ctx, node);
        return; /* A nested item's body is not executed by its enclosing function. */
    }

    const CBMType *saved_match_type = ctx->pending_match_type;
    if (strcmp(kind, "match_expression") == 0) {
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        ctx->pending_match_type =
            ts_node_is_null(value) ? cbm_type_unknown() : rust_eval_expr_type(ctx, value);
    }

    /* A `let` initializer runs before its pattern is in scope: resolve the
     * value first so `let f = f(x);` still reaches the function `f`. */
    TSNode let_value = {0};
    bool let_value_walked = false;
    if (strcmp(kind, "let_declaration") == 0) {
        let_value = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(let_value)) {
            rust_resolve_calls_in_node(ctx, let_value);
            let_value_walked = true;
        }
    }

    /* Bind variables introduced by this statement. */
    rust_process_statement(ctx, node);

    /* Resolve a call expression. */
    if (strcmp(kind, "call_expression") == 0) {
        rust_resolve_call_expression(ctx, node);
        /* Closure-parameter inference: when the call is a known
         * iterator-style method that takes a closure of `Item`, stash
         * the receiver's element type so the closure_expression child
         * binds its first param to it. We compute this here (after
         * the call edge is emitted) so the recursion picks it up. */
        TSNode fn = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(fn) && strcmp(ts_node_type(fn), "field_expression") == 0) {
            TSNode val = ts_node_child_by_field_name(fn, "value", 5);
            TSNode fld = ts_node_child_by_field_name(fn, "field", 5);
            if (!ts_node_is_null(val) && !ts_node_is_null(fld)) {
                char *mname = rust_node_text(ctx, fld);
                static const char *item_methods[] = {
                    "map",    "filter",     "for_each",   "find",        "position", "any",
                    "all",    "take_while", "skip_while", "filter_map",  "inspect",  "max_by",
                    "min_by", "max_by_key", "min_by_key", "sort_by_key", NULL};
                bool is_item_method = false;
                if (mname) {
                    for (const char **mm = item_methods; *mm; mm++) {
                        if (strcmp(mname, *mm) == 0) {
                            is_item_method = true;
                            break;
                        }
                    }
                }
                if (is_item_method) {
                    const CBMType *recv = rust_eval_expr_type(ctx, val);
                    const CBMType *base = recv;
                    while (base &&
                           (base->kind == CBM_TYPE_REFERENCE || base->kind == CBM_TYPE_POINTER)) {
                        base = (base->kind == CBM_TYPE_REFERENCE) ? base->data.reference.elem
                                                                  : base->data.pointer.elem;
                    }
                    if (base && base->kind == CBM_TYPE_TEMPLATE &&
                        base->data.template_type.arg_count > 0) {
                        const char *tn = base->data.template_type.template_name;
                        if (tn && (strstr(tn, "Iterator") || strstr(tn, "Vec") ||
                                   strstr(tn, "VecDeque") || strstr(tn, "Option") ||
                                   strstr(tn, "Result") || strstr(tn, "HashSet") ||
                                   strstr(tn, "BTreeSet") || strstr(tn, "Slice"))) {
                            ctx->pending_closure_param_type =
                                base->data.template_type.template_args[0];
                        }
                    } else if (base && base->kind == CBM_TYPE_SLICE) {
                        ctx->pending_closure_param_type = base->data.slice.elem;
                    }
                }
            }
        }
        /* Continue recursion so calls inside arguments are also seen. */
    }

    /* Operator-overload desugaring: `a + b` calls <T as Add>::add when the
     * left operand is a user-defined type T with that operator method;
     * `a[i]` calls T::index. The tree-sitter-rust grammar models these as
     * binary_expression / index_expression rather than call_expression, so
     * lang_specs.c's call-type whitelist never sees them — we recover the
     * call edge here. Sound-only via rust_emit_operator_call (no edge unless
     * the operand type actually defines the method). */
    if (strcmp(kind, "binary_expression") == 0) {
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        if (!ts_node_is_null(left)) {
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode c = ts_node_child(node, i);
                if (ts_node_is_named(c))
                    continue;
                char *op = rust_node_text(ctx, c);
                const char *method = rust_binop_trait_method(op);
                if (method) {
                    rust_emit_operator_call(ctx, rust_eval_expr_type(ctx, left), method, node);
                }
                break; /* operator is the sole anonymous child */
            }
        }
    } else if (strcmp(kind, "index_expression") == 0) {
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (ts_node_is_null(value) && ts_node_named_child_count(node) > 0) {
            value = ts_node_named_child(node, 0);
        }
        if (!ts_node_is_null(value)) {
            rust_emit_operator_call(ctx, rust_eval_expr_type(ctx, value), "index", node);
        }
    }

    /* Macro invocation: walk inner tokens for nested calls and try the
     * macro-as-function mapping. */
    if (strcmp(kind, "macro_invocation") == 0) {
        uint32_t saved_start = ctx->emit_site_start_byte;
        uint32_t saved_end = ctx->emit_site_end_byte;
        uint32_t mapped_start = 0;
        uint32_t mapped_end = 0;
        bool mapped = rust_map_source_range(ctx, ts_node_start_byte(node), ts_node_end_byte(node),
                                            &mapped_start, &mapped_end);
        ctx->emit_site_start_byte = mapped ? mapped_start : 0;
        ctx->emit_site_end_byte = mapped ? mapped_end : 0;

        uint32_t saved_macro_origin = ctx->macro_origin_byte;
        bool saved_macro_origin_valid = ctx->macro_origin_valid;
        /* A mapped macro-argument wrapper still identifies an exact original
         * occurrence; generated macro_rules! transcribers deliberately do not
         * map and therefore inherit the invocation that established them. */
        if (mapped) {
            ctx->macro_origin_byte = mapped_start;
            ctx->macro_origin_valid = true;
        }

        TSNode mname_node = ts_node_child_by_field_name(node, "macro", 5);
        if (!ts_node_is_null(mname_node)) {
            char *mname = rust_node_text(ctx, mname_node);
            if (mname) {
                /* A source-visible macro_rules! definition wins over prelude
                 * macros with the same name. Do this before applying any
                 * built-in argument grammar or canonical std path. */
                if (rust_has_visible_macro_definition(ctx, mname, node)) {
                    rust_expand_user_macro(ctx, mname, node);
                } else {
                    /* For known std macros emit a synthetic call under their
                     * canonical paths so trace tools can see the dependency. */
                    const char *path = NULL;
                    /* Macros whose arguments are ordinary Rust expressions — their
                     * call sites are lost to tree-sitter's macro tokenisation, so
                     * re-parse the args to recover calls like `format!("{}",
                     * d.label())`. */
                    RustMacroArgForm arg_form;
                    if (rust_builtin_macro_arg_form(mname, &arg_form))
                        rust_resolve_macro_arg_exprs(ctx, node, arg_form);
                    if (strcmp(mname, "println") == 0 || strcmp(mname, "eprintln") == 0 ||
                        strcmp(mname, "print") == 0 || strcmp(mname, "eprint") == 0 ||
                        strcmp(mname, "format") == 0 || strcmp(mname, "write") == 0 ||
                        strcmp(mname, "writeln") == 0) {
                        path = cbm_arena_sprintf(ctx->arena, "std.macros.%s", mname);
                    } else if (strcmp(mname, "vec") == 0) {
                        path = "alloc.vec.vec";
                    } else if (strcmp(mname, "panic") == 0) {
                        path = "core.panicking.panic";
                    } else if (strcmp(mname, "include") == 0) {
                        /* `include!` pulls another file in at compile time —
                         * we never see the included source. Emit a
                         * documentation edge so trace tools can flag it. */
                        path = "core.macros.include";
                    } else if (strcmp(mname, "include_str") == 0 ||
                               strcmp(mname, "include_bytes") == 0) {
                        /* Equivalent for data inclusion. */
                        path = cbm_arena_sprintf(ctx->arena, "core.macros.%s", mname);
                    } else if (strcmp(mname, "env") == 0 || strcmp(mname, "option_env") == 0) {
                        /* Compile-time env var read. Specifically: an
                         * `include!(concat!(env!("OUT_DIR"), …))` pattern
                         * indicates code generated by a build.rs that we
                         * cannot see. We surface the env! call so trace
                         * tools know to look for OUT_DIR. */
                        path = cbm_arena_sprintf(ctx->arena, "core.macros.%s", mname);
                    }
                    if (path) {
                        rust_emit_resolved_call(ctx, path, NULL, "lsp_macro",
                                                CBM_RUST_CONF_MACRO_KNOWN);
                        rust_ensure_known_macro_carrier(ctx, mname, path);
                    }
                }
            }
        }
        TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
        if (!ts_node_is_null(args))
            rust_walk_macro_tokens(ctx, args);
        ctx->macro_origin_byte = saved_macro_origin;
        ctx->macro_origin_valid = saved_macro_origin_valid;
        ctx->emit_site_start_byte = saved_start;
        ctx->emit_site_end_byte = saved_end;
        /* Don't recurse normally below — we already drilled into args. */
        return;
    }

    /* Push a fresh scope for blocks and constructs introducing new bindings. */
    bool push_scope =
        (strcmp(kind, "block") == 0 || strcmp(kind, "if_expression") == 0 ||
         strcmp(kind, "if_let_expression") == 0 || strcmp(kind, "while_expression") == 0 ||
         strcmp(kind, "while_let_expression") == 0 || strcmp(kind, "for_expression") == 0 ||
         strcmp(kind, "match_arm") == 0 || strcmp(kind, "closure_expression") == 0);
    bool uncertain_callable_flow =
        strcmp(kind, "if_expression") == 0 || strcmp(kind, "if_let_expression") == 0 ||
        strcmp(kind, "while_expression") == 0 || strcmp(kind, "while_let_expression") == 0 ||
        strcmp(kind, "for_expression") == 0 || strcmp(kind, "loop_expression") == 0 ||
        strcmp(kind, "match_arm") == 0 || strcmp(kind, "closure_expression") == 0;

    CBMScope *saved = ctx->current_scope;
    int saved_use_count = ctx->use_count;
    int saved_glob_count = ctx->glob_count;
    if (push_scope) {
        ctx->current_scope = cbm_scope_push(ctx->arena, ctx->current_scope);
    }
    if (strcmp(kind, "block") == 0) {
        rust_collect_uses(ctx, node);
        /* Item bindings cover the whole block, including calls before the
         * declaration. Nested function identity is not module identity. */
        for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
            TSNode item = ts_node_named_child(node, i);
            const char *item_kind = ts_node_type(item);
            if (strcmp(item_kind, "foreign_mod_item") == 0) {
                TSNode body = ts_node_child_by_field_name(item, "body", 4);
                for (uint32_t j = 0; j < ts_node_named_child_count(body); j++) {
                    TSNode name =
                        ts_node_child_by_field_name(ts_node_named_child(body, j), "name", 4);
                    if (!ts_node_is_null(name)) {
                        cbm_scope_bind(ctx->current_scope, rust_node_text(ctx, name),
                                       cbm_type_unknown());
                    }
                }
            }
            if (strcmp(item_kind, "function_item") == 0 || strcmp(item_kind, "const_item") == 0 ||
                strcmp(item_kind, "static_item") == 0 || strcmp(item_kind, "struct_item") == 0) {
                TSNode name = ts_node_child_by_field_name(item, "name", 4);
                if (!ts_node_is_null(name)) {
                    cbm_scope_bind(ctx->current_scope, rust_node_text(ctx, name),
                                   cbm_type_unknown());
                }
            }
        }
    }
    if (uncertain_callable_flow) {
        ctx->callable_control_flow_depth++;
    }

    /* if_let / while_let bind a pattern from the value's matched form.
     * Modern tree-sitter-rust parses `if let X = y { ... }` as
     * `if_expression` containing a `let_condition` child rather than as
     * the legacy `if_let_expression`. Same for `while let`. We handle
     * both shapes here. */
    if (strcmp(kind, "if_let_expression") == 0 || strcmp(kind, "while_let_expression") == 0) {
        TSNode pat = ts_node_child_by_field_name(node, "pattern", 7);
        TSNode val = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(pat) && !ts_node_is_null(val)) {
            const CBMType *vt = rust_eval_expr_type(ctx, val);
            rust_bind_pattern(ctx, pat, vt);
        }
    }
    if (strcmp(kind, "if_expression") == 0 || strcmp(kind, "while_expression") == 0) {
        /* Look for a let_condition (or let_chain) anywhere in the
         * condition slot. */
        TSNode cond = ts_node_child_by_field_name(node, "condition", 9);
        if (!ts_node_is_null(cond)) {
            uint32_t nc2 = ts_node_named_child_count(cond);
            /* Walk one level down for let_condition / let_chain. */
            const char *ck = ts_node_type(cond);
            TSNode targets[8];
            int tcount = 0;
            if (strcmp(ck, "let_condition") == 0) {
                targets[tcount++] = cond;
            } else if (strcmp(ck, "let_chain") == 0) {
                for (uint32_t i = 0; i < nc2 && tcount < 8; i++) {
                    TSNode c2 = ts_node_named_child(cond, i);
                    if (strcmp(ts_node_type(c2), "let_condition") == 0) {
                        targets[tcount++] = c2;
                    }
                }
            }
            for (int t = 0; t < tcount; t++) {
                TSNode lc = targets[t];
                TSNode pat = ts_node_child_by_field_name(lc, "pattern", 7);
                TSNode val = ts_node_child_by_field_name(lc, "value", 5);
                if (!ts_node_is_null(pat) && !ts_node_is_null(val)) {
                    const CBMType *vt = rust_eval_expr_type(ctx, val);
                    rust_bind_pattern(ctx, pat, vt);
                }
            }
        }
    }

    /* for_expression: bind the loop variable from the iter's element type. */
    if (strcmp(kind, "for_expression") == 0) {
        TSNode pat = ts_node_child_by_field_name(node, "pattern", 7);
        TSNode val = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(pat) && !ts_node_is_null(val)) {
            const CBMType *vt = rust_eval_expr_type(ctx, val);
            const CBMType *base = vt;
            while (base && base->kind == CBM_TYPE_REFERENCE)
                base = base->data.reference.elem;
            const CBMType *elem = cbm_type_unknown();
            if (base && base->kind == CBM_TYPE_SLICE) {
                elem = base->data.slice.elem;
            } else if (base && base->kind == CBM_TYPE_TEMPLATE) {
                const char *nm = base->data.template_type.template_name;
                if ((strstr(nm, "Vec") || strstr(nm, "VecDeque") || strstr(nm, "Iterator") ||
                     strstr(nm, "Range")) &&
                    base->data.template_type.arg_count > 0) {
                    elem = base->data.template_type.template_args[0];
                }
                if ((strstr(nm, "HashMap") || strstr(nm, "BTreeMap")) &&
                    base->data.template_type.arg_count > 1) {
                    /* Iter over (K, V) tuples. */
                    const CBMType *pair[2] = {base->data.template_type.template_args[0],
                                              base->data.template_type.template_args[1]};
                    elem = cbm_type_tuple(ctx->arena, pair, 2);
                }
            }
            rust_bind_pattern(ctx, pat, elem);
        }
    }

    /* match_expression: per-arm scope handled when we descend. */
    if (strcmp(kind, "match_arm") == 0) {
        TSNode pattern = ts_node_child_by_field_name(node, "pattern", 7);
        if (!ts_node_is_null(pattern)) {
            rust_bind_pattern(ctx, pattern,
                              ctx->pending_match_type ? ctx->pending_match_type
                                                      : cbm_type_unknown());
        }
    }

    /* closure_expression: bind closure parameters.
     *
     * Priority order for each param:
     *   1. Explicit type annotation (`|n: &i32|`) — use it directly.
     *   2. `ctx->pending_closure_param_type` for the FIRST param when the
     *      surrounding call resolver inferred one (`.map(|x| ...)` on
     *      Iterator<T>).
     *   3. Otherwise unknown.
     *
     * After binding, clear the pending type so it doesn't leak into a
     * sibling closure. */
    if (strcmp(kind, "closure_expression") == 0) {
        const CBMType *hint = ctx->pending_closure_param_type;
        ctx->pending_closure_param_type = NULL;
        TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
        if (!ts_node_is_null(params)) {
            uint32_t pc = ts_node_named_child_count(params);
            for (uint32_t i = 0; i < pc; i++) {
                TSNode p = ts_node_named_child(params, i);
                /* For `parameter`-shaped nodes, peel off the type
                 * annotation if present; otherwise treat the whole node
                 * as the pattern. */
                TSNode pat = p;
                const CBMType *bound = (i == 0 && hint) ? hint : cbm_type_unknown();
                if (strcmp(ts_node_type(p), "parameter") == 0) {
                    TSNode tn = ts_node_child_by_field_name(p, "type", 4);
                    TSNode pn = ts_node_child_by_field_name(p, "pattern", 7);
                    if (!ts_node_is_null(pn))
                        pat = pn;
                    if (!ts_node_is_null(tn)) {
                        bound = rust_parse_type_node(ctx, tn);
                    }
                }
                rust_bind_pattern(ctx, pat, bound);
            }
        }
    }

    /* Recurse. */
    uint32_t nc = ts_node_child_count(node);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(node, i);
        if (let_value_walked && ts_node_eq(c, let_value)) {
            continue;
        }
        if (!ts_node_is_null(c)) {
            rust_resolve_calls_in_node(ctx, c);
        }
    }

    if (push_scope) {
        ctx->current_scope = saved;
    }
    ctx->use_count = saved_use_count;
    ctx->glob_count = saved_glob_count;
    if (uncertain_callable_flow) {
        ctx->callable_control_flow_depth--;
    }
    ctx->pending_match_type = saved_match_type;
}

/* Graph identity of the function `func_node` named `name`, owned by `parent_qn`
 * (impl or trait owner) or by the file's module. Extraction publishes a
 * trait-impl method as `<Type>.<name>[<Trait>]`, a cfg twin free function as
 * `<name>#cfg(..)]` (extract_rust_impl, cbm_rust_cfg_scope_qn), and every item
 * of a file whose QN another item of the file also takes, other than the one
 * the graph keeps, as `<qn>#L<line>` (cbm_rust_separate_shared_qns). */
static const char *rust_function_node_qn(RustLSPContext *ctx, TSNode func_node, const char *name,
                                         const char *parent_qn) {
    const char *prefix = parent_qn ? parent_qn : ctx->module_qn;
    const char *qn =
        parent_qn && ctx->self_trait_spelling
            ? cbm_arena_sprintf(ctx->arena, "%s.%s[%s]", prefix, name, ctx->self_trait_spelling)
            : cbm_arena_sprintf(ctx->arena, "%s.%s", prefix, name);
    if (!parent_qn) {
        qn = cbm_rust_cfg_scope_qn(ctx->arena, func_node, ctx->source, qn);
    }
    if (ctx->registry) {
        const char *line_qn = cbm_rust_line_qualified_qn(ctx->arena, qn,
                                                         ts_node_start_point(func_node).row + 1);
        if (cbm_registry_lookup_func(ctx->registry, line_qn)) {
            return line_qn;
        }
    }
    return qn;
}

/* Process a single function: bind parameters / `self`, then walk body. */
static void rust_process_function(RustLSPContext *ctx, TSNode func_node, const char *parent_qn) {
    TSNode name_node = ts_node_child_by_field_name(func_node, "name", 4);
    if (ts_node_is_null(name_node))
        return;
    char *name = rust_node_text(ctx, name_node);
    if (!name || !name[0])
        return;

    const char *saved_func_qn = ctx->enclosing_func_qn;
    ctx->enclosing_func_qn = rust_function_node_qn(ctx, func_node, name, parent_qn);

    CBMScope *saved = ctx->current_scope;
    ctx->current_scope = cbm_scope_push(ctx->arena, ctx->current_scope);

    /* Chalk-lite: capture the active function's type-parameter bounds
     * and where-clause bounds into ctx so dispatch through generic
     * receivers can route via the bound trait. */
    int saved_bound_count = ctx->type_param_bound_count;
    TSNode tp_list = ts_node_child_by_field_name(func_node, "type_parameters", 15);
    if (!ts_node_is_null(tp_list)) {
        char *tp_text = rust_node_text(ctx, tp_list);
        if (tp_text)
            rust_collect_bounds_from_text(ctx, tp_text);
    }
    TSNode where_clause = ts_node_child_by_field_name(func_node, "where_clause", 12);
    if (!ts_node_is_null(where_clause)) {
        char *wt = rust_node_text(ctx, where_clause);
        if (wt)
            rust_collect_bounds_from_text(ctx, wt);
    }

    /* Bind self for impl/trait methods. */
    TSNode params = ts_node_child_by_field_name(func_node, "parameters", 10);
    if (!ts_node_is_null(params)) {
        uint32_t pc = ts_node_named_child_count(params);
        for (uint32_t i = 0; i < pc; i++) {
            TSNode p = ts_node_named_child(params, i);
            const char *pk = ts_node_type(p);
            if (strcmp(pk, "self_parameter") == 0) {
                if (ctx->self_type_qn) {
                    /* Determine if &self / &mut self / self by reference. */
                    char *text = rust_node_text(ctx, p);
                    const CBMType *self_t = cbm_type_named(ctx->arena, ctx->self_type_qn);
                    if (text && strchr(text, '&')) {
                        self_t = cbm_type_reference(ctx->arena, self_t);
                    }
                    cbm_scope_bind(ctx->current_scope, "self", self_t);
                }
                continue;
            }
            if (strcmp(pk, "parameter") == 0) {
                TSNode pat = ts_node_child_by_field_name(p, "pattern", 7);
                TSNode tn = ts_node_child_by_field_name(p, "type", 4);
                const CBMType *pt =
                    ts_node_is_null(tn) ? cbm_type_unknown() : rust_parse_type_node(ctx, tn);
                if (!ts_node_is_null(pat))
                    rust_bind_pattern(ctx, pat, pt);
            }
        }
    }

    /* Walk function body. */
    TSNode body = ts_node_child_by_field_name(func_node, "body", 4);
    if (!ts_node_is_null(body)) {
        rust_resolve_calls_in_node(ctx, body);
    }

    ctx->current_scope = saved;
    ctx->enclosing_func_qn = saved_func_qn;
    /* Restore bound-env count so the caller's bounds are unaffected. */
    ctx->type_param_bound_count = saved_bound_count;
}

/* Walk an `impl_item`, processing each `function_item` inside its body
 * with the appropriate `self_type_qn` (and `self_trait_qn` for trait impls). */
/* Chalk-lite: record a `T: Trait` bound in the per-function bound
 * environment. The arrays grow by 8 to keep allocs cheap. */
static void rust_record_type_param_bound(RustLSPContext *ctx, const char *param_name,
                                         const char *trait_qn) {
    if (!ctx || !param_name || !trait_qn)
        return;
    /* Grow by 8s. */
    if (ctx->type_param_bound_count % 8 == 0) {
        int new_cap = ctx->type_param_bound_count + 8;
        void *narr = cbm_arena_alloc(ctx->arena, new_cap * sizeof(*ctx->type_param_bounds));
        if (!narr)
            return;
        if (ctx->type_param_bounds && ctx->type_param_bound_count > 0) {
            memcpy(narr, ctx->type_param_bounds,
                   ctx->type_param_bound_count * sizeof(*ctx->type_param_bounds));
        }
        ctx->type_param_bounds = narr;
    }
    ctx->type_param_bounds[ctx->type_param_bound_count].param_name =
        cbm_arena_strdup(ctx->arena, param_name);
    ctx->type_param_bounds[ctx->type_param_bound_count].trait_qn =
        cbm_arena_strdup(ctx->arena, trait_qn);
    ctx->type_param_bound_count++;
}

/* Look up the first trait bound for a given type-param name. Returns
 * NULL if `name` has no bound recorded. */
static const char *rust_lookup_type_param_bound(RustLSPContext *ctx, const char *name) {
    if (!ctx || !name)
        return NULL;
    for (int i = 0; i < ctx->type_param_bound_count; i++) {
        if (strcmp(ctx->type_param_bounds[i].param_name, name) == 0) {
            return ctx->type_param_bounds[i].trait_qn;
        }
    }
    return NULL;
}

/* Parse the impl/function's <T: Bound + Bound, U: Bound> + where clause
 * text into the per-context bound environment. We don't reason about
 * lifetimes; we record only trait bounds and associated-type bindings.
 *
 * Format we accept (simplified TOML-like grammar):
 *   `<T: Clone + Debug, U: Iterator<Item = V>>`
 *   `where T: Clone, U: Iterator<Item = V>`
 *
 * Multiple bounds are split on `+` (top-level), entries on `,`. */
static void rust_collect_bounds_from_text(RustLSPContext *ctx, const char *text) {
    if (!ctx || !text)
        return;
    /* Walk text, find segments separated by `,` at top depth. For each
     * segment, split on `:` to get (param, bound-list); split bounds on `+`
     * at top depth. Resolve each bound through the path resolver to its QN. */
    int len = (int)strlen(text);
    int from = 0;
    while (from < len) {
        /* Skip whitespace + leading 'where'/punct. */
        while (from < len && (text[from] == ' ' || text[from] == '\n' || text[from] == '<' ||
                              text[from] == ',' || text[from] == '>' || text[from] == 'w')) {
            if (text[from] == 'w' && from + 5 < len && strncmp(text + from, "where", 5) == 0) {
                from += 5;
            } else {
                from++;
            }
        }
        if (from >= len)
            break;

        /* Param name. */
        int name_start = from;
        if (text[from] == '\'') {
            /* Lifetime — skip. */
            from++;
            while (from < len && (isalnum((unsigned char)text[from]) || text[from] == '_'))
                from++;
            continue;
        }
        while (from < len && (isalnum((unsigned char)text[from]) || text[from] == '_'))
            from++;
        int name_end = from;
        if (name_end == name_start) {
            from++;
            continue;
        }
        char *pname =
            cbm_arena_strndup(ctx->arena, text + name_start, (size_t)(name_end - name_start));
        /* Look for `:`. */
        while (from < len && (text[from] == ' ' || text[from] == '\t'))
            from++;
        if (from >= len || text[from] != ':') {
            /* No bound; skip to next entry. */
            while (from < len && text[from] != ',' && text[from] != '>' && text[from] != '\n')
                from++;
            continue;
        }
        from++; /* consume `:` */

        /* Bound list: split on `+` at depth 0, terminated by `,` / `>` /
         * end of where clause. */
        int depth = 0;
        int bound_start = from;
        while (from < len) {
            char c = text[from];
            if (c == '<' || c == '(' || c == '[')
                depth++;
            else if (c == '>' || c == ')' || c == ']') {
                if (depth == 0)
                    break;
                depth--;
            } else if (depth == 0 && (c == '+' || c == ',' || c == '\n')) {
                /* End of one bound. */
                int b_end = from;
                /* Trim trailing whitespace. */
                while (b_end > bound_start && (text[b_end - 1] == ' ' || text[b_end - 1] == '\t')) {
                    b_end--;
                }
                /* Trim leading whitespace. */
                int b_start = bound_start;
                while (b_start < b_end && (text[b_start] == ' ' || text[b_start] == '\t')) {
                    b_start++;
                }
                if (b_end > b_start) {
                    char *btext =
                        cbm_arena_strndup(ctx->arena, text + b_start, (size_t)(b_end - b_start));
                    /* Strip any `<…>` associated-type suffix for the
                     * trait QN lookup — we keep the trait name only. */
                    char *langle = strchr(btext, '<');
                    if (langle)
                        *langle = '\0';
                    const char *qn = rust_resolve_path_expr(ctx, btext);
                    if (qn) {
                        rust_record_type_param_bound(ctx, pname, qn);
                    }
                }
                if (c == '+') {
                    from++;
                    bound_start = from;
                    continue;
                }
                /* End of entry. */
                break;
            }
            from++;
        }
        /* Advance past entry terminator. */
        if (from < len && (text[from] == ',' || text[from] == '\n'))
            from++;
    }
}

/* ── Impl self type declared in another file ───────────────────────
 * Extraction keys an impl block's self type and methods by the impl file's
 * module (`<impl file module>.App`, see extract_rust_impl). When that QN is
 * not a declared type and the crate module tree proves the self type path
 * names a struct, enum or union declared elsewhere, the block belongs to
 * that declared type. One rule serves the registry (methods registered on
 * the declared type), the per-file walk (`Self` inside the block) and the
 * impl-header link (the method owner), so a method is looked up on the same
 * type its owner link names. The answer depends only on the registry, the
 * module tree, the file and the self type spelling, so every site computes
 * the same one. */

typedef const CBMRegisteredType *(*RustImplTypeLookup)(const void *ctx, const char *qn);

/* The self type spelling as a path (`App`, `super::App`), generic arguments
 * dropped; NULL for references, tuples, slices, trait objects and the like. */
static const char *rust_impl_self_path(CBMArena *arena, const char *spelling) {
    const char *path = cbm_rust_impl_owner_name(arena, spelling);
    if (!path || !path[0]) {
        return NULL;
    }
    for (const char *p = path; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != ':') {
            return NULL;
        }
    }
    return path;
}

static const char *rust_impl_declared_self(const CBMRustModTree *tree, CBMArena *arena,
                                           const char *module_qn, const char *receiver_qn,
                                           const char *spelling, RustImplTypeLookup lookup,
                                           const void *lookup_ctx) {
    if (!tree || !module_qn || !receiver_qn || !spelling || lookup(lookup_ctx, receiver_qn)) {
        return NULL;
    }
    const char *path = rust_impl_self_path(arena, spelling);
    const char *declared =
        path ? cbm_rust_modtree_resolve_nominal_type(tree, arena, module_qn, path) : NULL;
    if (!declared || strcmp(declared, receiver_qn) == 0) {
        return NULL;
    }
    const CBMRegisteredType *type = lookup(lookup_ctx, declared);
    return type && !type->is_interface ? type->qualified_name : NULL;
}

/* The std Deref wrapper (`alloc.boxed.Box`, `alloc.sync.Arc`, ...) an impl
 * self type spelled `Box<T>` / `Arc<dyn X>` names, or NULL. A method of such
 * an impl is a method of the wrapper: rustc finds it on the wrapper before it
 * derefs to the target. A bare name the module tree cannot settle (no `use`
 * or item binds it; only a glob of unknown contents might) is read as the
 * prelude reads it, the rule a call site applies to the same spelling
 * (rust_wrapper_head_qn). */
static const char *rust_impl_std_wrapper_self(const CBMRustModTree *tree, CBMArena *arena,
                                              const char *module_qn, const char *spelling) {
    const char *path = spelling ? rust_impl_self_path(arena, spelling) : NULL;
    const char *qn = path ? rust_text_type_qn_by_module_tree(tree, arena, module_qn, path) : NULL;
    if (!qn && path && tree && module_qn && !strchr(path, ':') &&
        cbm_rust_modtree_resolve_type_in_file(tree, arena, module_qn, path).kind ==
            CBM_RUST_PATH_UNKNOWN) {
        qn = rust_lookup_prelude(path);
    }
    return qn ? rust_std_wrapper_canonical(qn) : NULL;
}

/* The self type of a trait impl on the std wrapper `wrapper_qn`, spelled
 * `spelling` in `module_qn`, with the wrapper named by its canonical QN. */
static const CBMType *rust_wrapper_impl_self_type(CBMArena *arena, const char *spelling,
                                                  const char *wrapper_qn, const char *module_qn,
                                                  const CBMRustModTree *tree) {
    const CBMType *t = rust_parse_def_type_text(arena, spelling, module_qn, tree);
    if (!t || t->kind != CBM_TYPE_TEMPLATE) {
        return NULL;
    }
    return cbm_type_template(arena, wrapper_qn, t->data.template_type.template_args,
                             t->data.template_type.arg_count);
}

static const CBMRegisteredType *rust_impl_registry_type(const void *reg, const char *qn) {
    return cbm_registry_lookup_type((const CBMTypeRegistry *)reg, qn);
}

/* Declared self type of an impl block whose self type is spelled
 * `type_text` in this file, or NULL to keep the extracted receiver. */
static const char *rust_impl_declared_self_in_file(RustLSPContext *ctx, const char *type_text) {
    const CBMRustModTree *tree = ctx->imports ? ctx->imports->modtree : NULL;
    if (!tree || !ctx->registry || !ctx->module_qn || !type_text) {
        return NULL;
    }
    const char *receiver_qn = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn,
                                                cbm_rust_impl_owner_name(ctx->arena, type_text));
    return rust_impl_declared_self(tree, ctx->arena, ctx->module_qn, receiver_qn, type_text,
                                   rust_impl_registry_type, ctx->registry);
}

/* Helper: does `name` appear in the impl's `<T, U, ...>` type
 * parameter list? Used to detect blanket impls (impl<T: Trait> X for T). */
static bool rust_impl_has_type_param(RustLSPContext *ctx, TSNode impl_node, const char *name) {
    if (!name)
        return false;
    TSNode tp = ts_node_child_by_field_name(impl_node, "type_parameters", 15);
    if (ts_node_is_null(tp))
        return false;
    uint32_t nc = ts_node_named_child_count(tp);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_named_child(tp, i);
        const char *ck = ts_node_type(c);
        if (strcmp(ck, "type_identifier") == 0 || strcmp(ck, "constrained_type_parameter") == 0) {
            char *nm = rust_node_text(ctx, c);
            if (!nm)
                continue;
            /* For constrained_type_parameter, the name is the
             * first identifier-y child. */
            if (strcmp(ck, "constrained_type_parameter") == 0) {
                TSNode lhs = ts_node_child_by_field_name(c, "left", 4);
                if (!ts_node_is_null(lhs))
                    nm = rust_node_text(ctx, lhs);
            }
            if (nm && strcmp(nm, name) == 0)
                return true;
        }
    }
    return false;
}

static void rust_process_impl(RustLSPContext *ctx, TSNode impl_node) {
    TSNode type_node = ts_node_child_by_field_name(impl_node, "type", 4);
    if (ts_node_is_null(type_node))
        return;
    char *type_text = rust_node_text(ctx, type_node);
    if (!type_text)
        return;

    /* Detect blanket impl: `impl<T: Trait> ForeignTrait for T { ... }`
     * where type_text is a name that appears in the impl's type
     * parameters. In that case the receiver isn't a concrete type — it's
     * any T satisfying the bound. We register the methods on the trait
     * QN itself so dispatch through T: Trait finds them. */
    TSNode trait_node = ts_node_child_by_field_name(impl_node, "trait", 5);
    bool is_blanket =
        !ts_node_is_null(trait_node) && rust_impl_has_type_param(ctx, impl_node, type_text);
    const char *effective_recv = NULL;

    /* `self` is typed by the impl's type name without its generic and
     * lifetime arguments (`W<'a>` binds `self` to `W`): the registry holds a
     * type and its methods under that bare QN, the one the owner below names. */
    if (is_blanket) {
        char *tt = rust_node_text(ctx, trait_node);
        if (tt)
            effective_recv = rust_resolve_path_expr(ctx, cbm_rust_impl_owner_name(ctx->arena, tt));
    } else {
        effective_recv = rust_impl_declared_self_in_file(ctx, type_text);
        if (!effective_recv) {
            effective_recv =
                rust_resolve_path_expr(ctx, cbm_rust_impl_owner_name(ctx->arena, type_text));
        }
    }
    if (!effective_recv)
        return;

    const char *saved_self = ctx->self_type_qn;
    const char *saved_trait = ctx->self_trait_qn;
    const char *saved_trait_spelling = ctx->self_trait_spelling;
    const char *saved_impl_key = ctx->enclosing_impl_key;
    ctx->self_type_qn = effective_recv;
    ctx->self_trait_qn = NULL;
    ctx->self_trait_spelling = NULL;
    ctx->enclosing_impl_key = NULL;

    if (!ts_node_is_null(trait_node)) {
        char *tt = rust_node_text(ctx, trait_node);
        if (tt && !is_blanket) {
            ctx->self_trait_qn = rust_resolve_path_expr(ctx, tt);
            ctx->enclosing_impl_key = cbm_rust_impl_key(ctx->arena, effective_recv, type_text, tt);
        }
        if (tt && tt[0]) {
            ctx->self_trait_spelling = tt;
        }
    }

    /* The Method node is keyed by this file's module and the implementing
     * type's base name (extract_rust_impl), not by the resolved receiver:
     * `impl<T> Shape for G<T>` in ops.rs owns `ops.G.area[Shape]` even when
     * `G` is declared in another module. */
    const char *owner_qn = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn,
                                             cbm_rust_impl_owner_name(ctx->arena, type_text));
    TSNode body = ts_node_child_by_field_name(impl_node, "body", 4);
    if (!ts_node_is_null(body)) {
        uint32_t nc = ts_node_child_count(body);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode c = ts_node_child(body, i);
            if (ts_node_is_null(c) || !ts_node_is_named(c))
                continue;
            const char *ck = ts_node_type(c);
            if (strcmp(ck, "function_item") == 0) {
                rust_process_function(ctx, c, owner_qn);
            }
        }
    }

    ctx->self_type_qn = saved_self;
    ctx->self_trait_qn = saved_trait;
    ctx->self_trait_spelling = saved_trait_spelling;
    ctx->enclosing_impl_key = saved_impl_key;
}

/* Walk the default method bodies of a `trait_item`. Extraction publishes them
 * as `<module>.<Trait>.<name>`; inside them `self` / `Self` is the trait
 * itself, so a method called on `self` is that trait's declaration. */
static void rust_process_trait(RustLSPContext *ctx, TSNode trait_node) {
    char *name = rust_node_text(ctx, ts_node_child_by_field_name(trait_node, "name", 4));
    TSNode body = ts_node_child_by_field_name(trait_node, "body", 4);
    if (!name || !name[0] || ts_node_is_null(body))
        return;
    const char *owner_qn = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, name);
    const CBMRegisteredType *trait_type =
        ctx->registry ? cbm_registry_lookup_type(ctx->registry, owner_qn) : NULL;

    const char *saved_self = ctx->self_type_qn;
    const char *saved_trait = ctx->self_trait_qn;
    const char *saved_trait_spelling = ctx->self_trait_spelling;
    const char *saved_impl_key = ctx->enclosing_impl_key;
    ctx->self_type_qn = trait_type && trait_type->is_interface ? trait_type->qualified_name : NULL;
    ctx->self_trait_qn = NULL;
    ctx->self_trait_spelling = NULL;
    ctx->enclosing_impl_key = NULL;
    for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
        TSNode item = ts_node_named_child(body, i);
        if (strcmp(ts_node_type(item), "function_item") == 0) {
            rust_process_function(ctx, item, owner_qn);
        }
    }
    ctx->self_type_qn = saved_self;
    ctx->self_trait_qn = saved_trait;
    ctx->self_trait_spelling = saved_trait_spelling;
    ctx->enclosing_impl_key = saved_impl_key;
}

/* An item declared inside a function body: its own body is not executed by
 * the enclosing function, so it is walked under its own graph identity. It
 * sees the module's items and the block's `use`s, but none of the enclosing
 * function's locals, `Self`, generics or control flow. Items spelled inside a
 * macro expansion have no graph node and are left unwalked. */
static void rust_process_nested_item(RustLSPContext *ctx, TSNode item) {
    if (ctx->source != ctx->root_source || ctx->macro_expand_depth > 0 || ctx->site_map_active) {
        return;
    }
    CBMScope *saved_scope = ctx->current_scope;
    const char *saved_self = ctx->self_type_qn;
    const char *saved_trait = ctx->self_trait_qn;
    const char *saved_trait_spelling = ctx->self_trait_spelling;
    const char *saved_impl_key = ctx->enclosing_impl_key;
    const char *saved_func_qn = ctx->enclosing_func_qn;
    const CBMType *saved_closure_param = ctx->pending_closure_param_type;
    const CBMType *saved_match = ctx->pending_match_type;
    int saved_flow_depth = ctx->callable_control_flow_depth;

    CBMScope *file_scope = ctx->current_scope;
    while (file_scope && file_scope->parent) {
        file_scope = file_scope->parent;
    }
    ctx->current_scope = file_scope;
    ctx->self_type_qn = NULL;
    ctx->self_trait_qn = NULL;
    ctx->self_trait_spelling = NULL;
    ctx->enclosing_impl_key = NULL;
    ctx->pending_closure_param_type = NULL;
    ctx->pending_match_type = NULL;
    ctx->callable_control_flow_depth = 0;

    const char *kind = ts_node_type(item);
    if (strcmp(kind, "function_item") == 0) {
        rust_process_function(ctx, item, NULL);
    } else if (strcmp(kind, "impl_item") == 0) {
        rust_process_impl(ctx, item);
    } else if (strcmp(kind, "trait_item") == 0) {
        rust_process_trait(ctx, item);
    } else if (strcmp(kind, "mod_item") == 0) {
        rust_process_module(ctx, item, 0);
    }

    ctx->current_scope = saved_scope;
    ctx->self_type_qn = saved_self;
    ctx->self_trait_qn = saved_trait;
    ctx->self_trait_spelling = saved_trait_spelling;
    ctx->enclosing_impl_key = saved_impl_key;
    ctx->enclosing_func_qn = saved_func_qn;
    ctx->pending_closure_param_type = saved_closure_param;
    ctx->pending_match_type = saved_match;
    ctx->callable_control_flow_depth = saved_flow_depth;
}

/* Bound on inline-module nesting the walk follows. */
enum { RUST_INLINE_MOD_MAX_DEPTH = 64 };

/* Number of free functions of this file whose graph identity is `name`'s
 * module-level QN or a line/cfg-qualified twin of it, capped at 2. */
static int rust_file_fn_namesakes(RustLSPContext *ctx, const char *name) {
    const char *qn = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, name);
    size_t len = strlen(qn);
    CBMFreeFuncIter it;
    cbm_registry_free_funcs_by_short_name_chain(ctx->registry, name, &it);
    int n = 0;
    for (int i; n < 2 && (i = cbm_free_func_iter_next(&it)) >= 0;) {
        const char *cand = it.reg->funcs[i].qualified_name;
        if (cand && strncmp(cand, qn, len) == 0 && (cand[len] == '\0' || cand[len] == '#')) {
            n++;
        }
    }
    return n;
}

/* True when `id`, an identifier spelled like a function the module walked
 * from `body` declares, cannot be a binding that hides that function from a
 * bare call: the callee of a call, a segment of a qualified path outside a
 * `use`, the name of a method, the module's own declaration, or a macro
 * token directly followed by a parenthesised argument list. */
static bool rust_module_fn_mention_is_inert(TSNode id, TSNode body) {
    TSNode parent = ts_node_parent(id);
    const char *pk = ts_node_type(parent);
    if (strcmp(pk, "function_item") == 0 || strcmp(pk, "function_signature_item") == 0) {
        TSNode owner = ts_node_parent(parent);
        return !ts_node_is_null(owner) && strcmp(ts_node_type(owner), "block") != 0;
    }
    if (strcmp(pk, "generic_function") == 0) {
        id = parent;
        parent = ts_node_parent(parent);
        pk = ts_node_type(parent);
    }
    if (strcmp(pk, "call_expression") == 0) {
        return ts_node_eq(ts_node_child_by_field_name(parent, "function", 8), id);
    }
    if (strcmp(pk, "token_tree") == 0) {
        TSNode next = ts_node_next_sibling(id);
        return !ts_node_is_null(next) && strcmp(ts_node_type(next), "token_tree") == 0 &&
               ts_node_child_count(next) > 0 &&
               strcmp(ts_node_type(ts_node_child(next, 0)), "(") == 0;
    }
    if (strcmp(pk, "scoped_identifier") != 0) {
        return false;
    }
    for (TSNode a = parent; !ts_node_is_null(a) && !ts_node_eq(a, body); a = ts_node_parent(a)) {
        if (strcmp(ts_node_type(a), "use_declaration") == 0) {
            return false;
        }
    }
    return true;
}

/* Clear every name in `names` that some identifier of `body` (nested modules
 * excluded: they are their own scope) could bind in place of the module's
 * function: a local, a parameter, a pattern, a block item, a `use`. */
static void rust_drop_shadowed_module_fns(RustLSPContext *ctx, TSNode body, const char **names,
                                          int count) {
    TSTreeCursor cur = ts_tree_cursor_new(body);
    for (;;) {
        TSNode n = ts_tree_cursor_current_node(&cur);
        const char *k = ts_node_type(n);
        bool descend = !(strcmp(k, "mod_item") == 0 && !ts_node_eq(n, body));
        if (strcmp(k, "identifier") == 0 || strcmp(k, "shorthand_field_identifier") == 0) {
            descend = false;
            char *text = rust_node_text(ctx, n);
            for (int i = 0; text && i < count; i++) {
                if (names[i] && strcmp(names[i], text) == 0 &&
                    !rust_module_fn_mention_is_inert(n, body)) {
                    names[i] = NULL;
                }
            }
        }
        if (descend && ts_tree_cursor_goto_first_child(&cur)) {
            continue;
        }
        while (!ts_tree_cursor_goto_next_sibling(&cur)) {
            if (!ts_tree_cursor_goto_parent(&cur)) {
                ts_tree_cursor_delete(&cur);
                return;
            }
        }
    }
}

/* Bind the functions module `body` (the file root or an inline module's
 * declaration list) declares whose name another function of the file
 * shares. Extraction gives all of them the file's module QN, so the name
 * alone cannot pick one; Rust resolves a bare name in the module's bodies to
 * the module's own item unless a local, a block item or a block `use`
 * shadows it, and items of enclosing modules are not in scope at all. A
 * name declared twice in the module (cfg twins) or that any identifier of
 * the module could rebind stays unbound and keeps its prior resolution. */
static void rust_bind_module_fns(RustLSPContext *ctx, TSNode body) {
    ctx->module_fn_names = NULL;
    ctx->module_fn_qns = NULL;
    ctx->module_fn_count = 0;
    if (!ctx->registry || !ctx->module_qn) {
        return;
    }
    uint32_t nc = ts_node_named_child_count(body);
    const char **names = NULL;
    const char **qns = NULL;
    int count = 0;
    for (uint32_t i = 0; i < nc; i++) {
        TSNode item = ts_node_named_child(body, i);
        if (strcmp(ts_node_type(item), "function_item") != 0) {
            continue;
        }
        char *name = rust_node_text(ctx, ts_node_child_by_field_name(item, "name", 4));
        if (!name || !name[0] || rust_file_fn_namesakes(ctx, name) < 2) {
            continue;
        }
        bool twin = false;
        for (uint32_t j = 0; j < nc && !twin; j++) {
            TSNode other = ts_node_named_child(body, j);
            if (j != i && strcmp(ts_node_type(other), "function_item") == 0) {
                char *other_name =
                    rust_node_text(ctx, ts_node_child_by_field_name(other, "name", 4));
                twin = other_name && strcmp(other_name, name) == 0;
            }
        }
        const char *qn = rust_function_node_qn(ctx, item, name, NULL);
        if (twin || !cbm_registry_lookup_func(ctx->registry, qn)) {
            continue;
        }
        if (!names) {
            names = (const char **)cbm_arena_alloc(ctx->arena, nc * sizeof(char *));
            qns = (const char **)cbm_arena_alloc(ctx->arena, nc * sizeof(char *));
            if (!names || !qns) {
                return;
            }
        }
        names[count] = name;
        qns[count] = qn;
        count++;
    }
    if (count == 0) {
        return;
    }
    rust_drop_shadowed_module_fns(ctx, body, names, count);
    ctx->module_fn_names = names;
    ctx->module_fn_qns = qns;
    ctx->module_fn_count = count;
}

/* The graph node of the function the module being walked declares as
 * `name`, when rust_bind_module_fns bound it; NULL otherwise. */
static const char *rust_module_fn_target(const RustLSPContext *ctx, const char *name) {
    for (int i = 0; i < ctx->module_fn_count; i++) {
        if (ctx->module_fn_names[i] && strcmp(ctx->module_fn_names[i], name) == 0) {
            return ctx->module_fn_qns[i];
        }
    }
    return NULL;
}

/* Walk the items of inline module `mod_node` at nesting `depth`. Its own
 * `use`s and globs bind its whole body and shadow what the enclosing module
 * sees; the crate's module tree already knows them as that module's imports,
 * so they extend the module-level use set, not a block's. `self::` /
 * `super::` in those `use`s are relative to the inline module. Its items are
 * extracted under the file's module QN, so bodies keep that identity. */
static void rust_process_module(RustLSPContext *ctx, TSNode mod_node, int depth) {
    TSNode body = ts_node_child_by_field_name(mod_node, "body", 4);
    char *name = rust_node_text(ctx, ts_node_child_by_field_name(mod_node, "name", 4));
    if (ts_node_is_null(body) || !name || !name[0] || depth > RUST_INLINE_MOD_MAX_DEPTH) {
        return;
    }
    int saved_uses = ctx->use_count;
    int saved_globs = ctx->glob_count;
    int saved_file_uses = ctx->file_use_count;
    int saved_file_globs = ctx->file_glob_count;
    const char *saved_relative = ctx->relative_module_qn;
    ctx->relative_module_qn =
        saved_relative ? cbm_arena_sprintf(ctx->arena, "%s.%s", saved_relative, name) : NULL;
    rust_collect_uses(ctx, body);
    ctx->relative_module_qn = saved_relative;
    ctx->file_use_count = ctx->use_count;
    ctx->file_glob_count = ctx->glob_count;
    const char **saved_fn_names = ctx->module_fn_names;
    const char **saved_fn_qns = ctx->module_fn_qns;
    int saved_fn_count = ctx->module_fn_count;
    rust_bind_module_fns(ctx, body);

    uint32_t count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < count; i++) {
        TSNode item = ts_node_named_child(body, i);
        const char *kind = ts_node_type(item);
        if (strcmp(kind, "function_item") == 0) {
            rust_process_function(ctx, item, NULL);
        } else if (strcmp(kind, "impl_item") == 0) {
            rust_process_impl(ctx, item);
        } else if (strcmp(kind, "trait_item") == 0) {
            rust_process_trait(ctx, item);
        } else if (strcmp(kind, "mod_item") == 0) {
            rust_process_module(ctx, item, depth + 1);
        }
    }

    ctx->use_count = saved_uses;
    ctx->glob_count = saved_globs;
    ctx->file_use_count = saved_file_uses;
    ctx->file_glob_count = saved_file_globs;
    ctx->module_fn_names = saved_fn_names;
    ctx->module_fn_qns = saved_fn_qns;
    ctx->module_fn_count = saved_fn_count;
}

void rust_lsp_process_file(RustLSPContext *ctx, TSNode root) {
    if (ts_node_is_null(root))
        return;

    /* Pass 0: collect macro_rules! definitions before walking bodies so
     * macro_invocation handlers can expand user-defined macros. */
    rust_collect_macro_rules(ctx, root);

    /* Record bare `mod foo;` declarations (file links). The pipeline
     * uses these to know which sibling files to include in cross-file
     * resolution. We just store them as Imports with a `mod:` prefix
     * so the pipeline can distinguish them from `use` imports.
     *
     * `mod foo { ... }` (inline module) is NOT recorded — the body is
     * already in this file. Only bare `mod foo;` declarations are. */
    {
        uint32_t rnc = ts_node_child_count(root);
        for (uint32_t i = 0; i < rnc; i++) {
            TSNode c = ts_node_child(root, i);
            if (ts_node_is_null(c))
                continue;
            if (strcmp(ts_node_type(c), "mod_item") != 0)
                continue;
            /* Inline mod has a `body` field; bare decl does not. */
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body))
                continue; /* inline */
            TSNode mname = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(mname))
                continue;
            char *name = rust_node_text(ctx, mname);
            if (!name)
                continue;
            cbm_idxmemo_put_if_absent(&ctx->declared_modules, ctx->arena, name, 1);
            /* Surface as a synthetic CALLS edge from "<module>" to
             * the sibling module so the cross-file pass picks it up
             * via short-name fallback. We attribute it to the file's
             * synthetic module-scope caller. */
            const char *save_caller = ctx->enclosing_func_qn;
            ctx->enclosing_func_qn = ctx->module_qn;
            rust_emit_resolved_call(ctx,
                                    cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, name),
                                    NULL, "lsp_mod_decl", 0.70f);
            ctx->enclosing_func_qn = save_caller;
        }
    }

    /* Pass 1: bind module-level const/static so functions can see them. */
    uint32_t nc = ts_node_child_count(root);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(root, i);
        if (ts_node_is_null(c))
            continue;
        const char *ck = ts_node_type(c);
        if (strcmp(ck, "const_item") == 0 || strcmp(ck, "static_item") == 0) {
            rust_process_statement(ctx, c);
        }
    }

    /* Pass 2: walk every top-level item. */
    rust_bind_module_fns(ctx, root);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = ts_node_child(root, i);
        if (ts_node_is_null(c))
            continue;
        const char *ck = ts_node_type(c);
        if (strcmp(ck, "function_item") == 0) {
            rust_process_function(ctx, c, NULL);
        } else if (strcmp(ck, "impl_item") == 0) {
            rust_process_impl(ctx, c);
        } else if (strcmp(ck, "trait_item") == 0) {
            rust_process_trait(ctx, c);
        } else if (strcmp(ck, "mod_item") == 0) {
            rust_process_module(ctx, c, 0);
        }
    }
}

/* ════════════════════════════════════════════════════════════════════
 * 11. Per-file entry: build registry + run
 * ════════════════════════════════════════════════════════════════════ */

/* Collect `use_declaration`s in the file and materialise our use map.
 * Tree-sitter-rust models the pattern as:
 *
 *   use_declaration → identifier | scoped_identifier | scoped_use_list |
 *                     use_list | use_as_clause | use_wildcard.
 *
 * We expand each of these into one or more (alias, full-path) entries. */
static void rust_collect_uses(RustLSPContext *ctx, TSNode root) {
    /* Recursive walker. */
    typedef struct stack_t {
        TSNode node;
        struct stack_t *prev;
    } stack_t;
    stack_t *top = (stack_t *)cbm_arena_alloc(ctx->arena, sizeof(stack_t));
    top->node = root;
    top->prev = NULL;
    while (top) {
        TSNode n = top->node;
        top = top->prev;
        if (ts_node_is_null(n))
            continue;
        const char *k = ts_node_type(n);
        if (strcmp(k, "use_declaration") == 0) {
            CBMImportArray imports = {0};
            cbm_extract_rust_use_tree(ctx->arena, ts_node_child_by_field_name(n, "argument", 8),
                                      ctx->source, &imports);
            for (int i = 0; i < imports.count; i++) {
                const CBMImport *imp = &imports.items[i];
                if (strcmp(imp->local_name, "*") == 0) {
                    size_t length = strlen(imp->module_path);
                    if (length > 2) {
                        char *module = cbm_arena_strndup(ctx->arena, imp->module_path, length - 3);
                        const char *module_qn = rust_resolve_module_path(ctx, module);
                        rust_lsp_add_glob(ctx, module_qn ? module_qn
                                                         : convert_path_to_qn(ctx->arena, module));
                    }
                } else if (strcmp(imp->local_name, "_") != 0) {
                    const char *path = imp->module_path;
                    bool needs_relative_module =
                        strncmp(path, "self::", 6) == 0 || strncmp(path, "super::", 7) == 0;
                    if (needs_relative_module && !ctx->relative_module_qn) {
                        continue;
                    }
                    if (strncmp(path, "self::", 6) == 0 || strncmp(path, "super::", 7) == 0 ||
                        strncmp(path, "crate::", 7) == 0) {
                        path = rust_resolve_path_expr(ctx, path);
                    }
                    int before = ctx->use_count;
                    rust_lsp_add_use(ctx, imp->local_name, path);
                    if (ctx->use_count > before) {
                        ctx->use_raw_paths[before] = imp->module_path;
                    }
                }
            }
        }
        /* Imports belong to this lexical scope, not its nested items. */
        if (ts_node_eq(n, root)) {
            uint32_t nc = ts_node_child_count(n);
            for (uint32_t i = 0; i < nc; i++) {
                TSNode c = ts_node_child(n, i);
                if (ts_node_is_null(c))
                    continue;
                stack_t *nx = (stack_t *)cbm_arena_alloc(ctx->arena, sizeof(stack_t));
                nx->node = c;
                nx->prev = top;
                top = nx;
            }
        }
    }
}

/* Derive-macro synthesis.
 *
 * Without expanding proc-macros we still know the impl footprint of the
 * well-known derives, so `x.clone()` / `MyT::default()` on a derived type
 * resolve and type their result. Unknown derives are left alone. Each
 * matching derive registers its methods on the type (static ones such as
 * `default` / `parse` reached through path lookup) and appends the trait QN
 * to the type's `embedded_types` so trait dispatch walks it. */
typedef enum {
    RUST_DERIVE_RET_UNKNOWN,
    RUST_DERIVE_RET_SELF,
    RUST_DERIVE_RET_BOOL,
    RUST_DERIVE_RET_UNIT,
} RustDeriveReturn;

typedef struct {
    const char *short_name;
    RustDeriveReturn ret;
} RustDeriveMethod;

#define RUST_DERIVE_MAX_METHODS 4

typedef struct {
    const char *derive_name;
    const char *trait_qn;
    RustDeriveMethod methods[RUST_DERIVE_MAX_METHODS]; /* ends at the first NULL short_name */
} RustDeriveImpl;

static const RustDeriveImpl RUST_DERIVES[] = {
    {"Clone", "core.clone.Clone", {{"clone", RUST_DERIVE_RET_SELF}}},
    {"Copy", "core.marker.Copy", {{NULL}}},
    {"Debug", "core.fmt.Debug", {{"fmt", RUST_DERIVE_RET_UNKNOWN}}},
    {"Display", "core.fmt.Display", {{"fmt", RUST_DERIVE_RET_UNKNOWN}}},
    {"Default", "core.default.Default", {{"default", RUST_DERIVE_RET_SELF}}},
    {"PartialEq", "core.cmp.PartialEq", {{"eq", RUST_DERIVE_RET_BOOL}, {"ne", RUST_DERIVE_RET_BOOL}}},
    {"Eq", "core.cmp.Eq", {{NULL}}},
    {"PartialOrd",
     "core.cmp.PartialOrd",
     {{"partial_cmp", RUST_DERIVE_RET_UNKNOWN},
      {"lt", RUST_DERIVE_RET_BOOL},
      {"le", RUST_DERIVE_RET_BOOL}}},
    {"Ord", "core.cmp.Ord", {{"cmp", RUST_DERIVE_RET_UNKNOWN}}},
    {"Hash", "core.hash.Hash", {{"hash", RUST_DERIVE_RET_UNIT}}},
    {"Send", "core.marker.Send", {{NULL}}},
    {"Sync", "core.marker.Sync", {{NULL}}},
    {"Serialize", "serde.Serialize", {{"serialize", RUST_DERIVE_RET_UNKNOWN}}},
    {"Deserialize", "serde.Deserialize", {{"deserialize", RUST_DERIVE_RET_UNKNOWN}}},
    {"Parser",
     "clap.Parser",
     {{"parse", RUST_DERIVE_RET_SELF},
      {"try_parse", RUST_DERIVE_RET_UNKNOWN},
      {"parse_from", RUST_DERIVE_RET_SELF},
      {"try_parse_from", RUST_DERIVE_RET_UNKNOWN}}},
    {"Args", "clap.Args", {{NULL}}},
    {"Subcommand", "clap.Subcommand", {{NULL}}},
    {"ValueEnum", "clap.ValueEnum", {{NULL}}},
    {"Error", "core.error.Error", {{NULL}}},
};

static const RustDeriveImpl *rust_derive_lookup(const char *name, size_t len) {
    for (size_t i = 0; i < sizeof(RUST_DERIVES) / sizeof(RUST_DERIVES[0]); i++) {
        if (strlen(RUST_DERIVES[i].derive_name) == len &&
            strncmp(RUST_DERIVES[i].derive_name, name, len) == 0) {
            return &RUST_DERIVES[i];
        }
    }
    return NULL;
}

static void rust_register_derive_impl(CBMArena *arena, CBMTypeRegistry *reg, int32_t type_index,
                                      const RustDeriveImpl *impl) {
    const char *type_qn = reg->types[type_index].qualified_name;
    rust_registered_type_add_embedded(arena, &reg->types[type_index], impl->trait_qn);
    for (int mi = 0; mi < RUST_DERIVE_MAX_METHODS && impl->methods[mi].short_name; mi++) {
        const RustDeriveMethod *dm = &impl->methods[mi];
        CBMRegisteredFunc rf;
        memset(&rf, 0, sizeof(rf));
        rf.short_name = dm->short_name;
        rf.qualified_name = cbm_arena_sprintf(arena, "%s.%s", type_qn, dm->short_name);
        rf.receiver_type = type_qn;
        rf.min_params = -1;
        rf.flags |= CBM_FUNC_FLAG_RUST_TRAIT_IMPL;
        rf.impl_trait_qn = impl->trait_qn;
        const CBMType *ret_t = cbm_type_unknown();
        switch (dm->ret) {
        case RUST_DERIVE_RET_SELF:
            ret_t = cbm_type_named(arena, type_qn);
            break;
        case RUST_DERIVE_RET_BOOL:
            ret_t = cbm_type_builtin(arena, "bool");
            break;
        case RUST_DERIVE_RET_UNIT:
            ret_t = cbm_type_builtin(arena, "()");
            break;
        case RUST_DERIVE_RET_UNKNOWN:
            break;
        }
        const CBMType **ra = (const CBMType **)cbm_arena_alloc(arena, 2 * sizeof(const CBMType *));
        ra[0] = ret_t;
        ra[1] = NULL;
        rf.signature = cbm_type_func(arena, NULL, NULL, ra);
        cbm_registry_add_func(reg, rf);
    }
}

/* Register the curated derives named by `#[derive(..)]` decorators on the
 * registered type at `type_index`. A derive path (`serde::Serialize`) is
 * matched on its last segment. */
static void rust_register_derives(CBMArena *arena, CBMTypeRegistry *reg, int32_t type_index,
                                  const char *const *decorators) {
    if (!decorators || type_index < 0 || type_index >= reg->type_count ||
        !reg->types[type_index].qualified_name) {
        return;
    }
    for (int di = 0; decorators[di]; di++) {
        const char *p = strstr(decorators[di], "derive");
        const char *lparen = p ? strchr(p, '(') : NULL;
        const char *rparen = lparen ? strchr(lparen, ')') : NULL;
        if (!rparen) {
            continue;
        }
        const char *q = lparen + 1;
        while (q < rparen) {
            while (q < rparen && (*q == ' ' || *q == ',' || *q == '\n' || *q == '\t')) {
                q++;
            }
            const char *tok_start = q;
            while (q < rparen && *q != ',' && *q != ' ' && *q != '\n' && *q != '\t') {
                q++;
            }
            if (q == tok_start) {
                break;
            }
            const char *short_start = tok_start;
            for (const char *r = tok_start; r < q - 1; r++) {
                if (r[0] == ':' && r[1] == ':') {
                    short_start = r + 2;
                }
            }
            const RustDeriveImpl *impl = rust_derive_lookup(short_start, (size_t)(q - short_start));
            if (impl) {
                rust_register_derive_impl(arena, reg, type_index, impl);
            }
        }
    }
}

/* Build the registry from the per-file `result->defs`, `result->impl_traits`,
 * and a Rust prelude seed. */
void cbm_rust_build_local_registry(CBMArena *arena, CBMTypeRegistry *reg, CBMFileResult *result,
                                   const char *module_qn, TSNode root, const char *source) {

    cbm_registry_init(reg, arena);
    cbm_rust_stdlib_register(reg, arena);

    /* Phase A1: register all type containers first. Trait provenance on a
     * later method can then be canonicalized without declaration-order or
     * leaf-first guesses. */
    CBMIdxMemo type_idx = {0};
    for (int i = 0; i < reg->type_count; i++) {
        if (reg->types[i].qualified_name) {
            cbm_idxmemo_put_if_absent(&type_idx, arena, reg->types[i].qualified_name, i);
        }
    }
    for (int i = 0; i < result->defs.count; i++) {
        CBMDefinition *d = &result->defs.items[i];
        if (!d->qualified_name || !d->name || !cbm_label_is_type_like(d->label)) {
            continue;
        }
        CBMRegisteredType rt;
        memset(&rt, 0, sizeof(rt));
        rt.qualified_name = d->qualified_name;
        rt.short_name = d->name;
        rt.is_interface = (strcmp(d->label, "Interface") == 0 || strcmp(d->label, "Trait") == 0);
        int alias_param_count = 0;
        while (d->type_alias_params && d->type_alias_params[alias_param_count]) {
            alias_param_count++;
        }
        rust_register_type_alias(arena, &rt, d->type_alias, d->type_alias_params,
                                 alias_param_count, module_qn, NULL);
        cbm_registry_add_type(reg, rt);
        cbm_idxmemo_put_if_absent(&type_idx, arena, rt.qualified_name, reg->type_count - 1);
    }
    RustTraitNameIndex trait_names;
    rust_trait_name_index_build(&trait_names, arena, reg);

    /* Phase A2: register every Function/Method definition. */
    for (int i = 0; i < result->defs.count; i++) {
        CBMDefinition *d = &result->defs.items[i];
        if (!d->qualified_name || !d->name)
            continue;

        if (d->label && (strcmp(d->label, "Function") == 0 || strcmp(d->label, "Method") == 0)) {
            CBMRegisteredFunc rf;
            memset(&rf, 0, sizeof(rf));
            rf.qualified_name = d->qualified_name;
            rf.short_name = d->name;
            rf.min_params = -1;
            if (d->is_abstract) {
                rf.flags |= CBM_FUNC_FLAG_RUST_ABSTRACT;
            }
            if (d->is_async) {
                rf.flags |= CBM_FUNC_FLAG_ASYNC;
            }
            if (d->has_rust_self) {
                rf.flags |= CBM_FUNC_FLAG_RUST_SELF;
            }
            if (d->impl_trait) {
                rf.flags |= CBM_FUNC_FLAG_RUST_TRAIT_IMPL;
                rf.impl_trait_qn =
                    rust_canonical_impl_trait(arena, reg, &trait_names, d->impl_trait, module_qn);
            } else if (d->is_rust_private && module_qn) {
                rf.rust_private_module = module_qn;
            }

            /* Build FUNC sig from return_types / param_types. */
            const CBMType **ret_types = NULL;
            if (d->return_types) {
                int count = 0;
                while (d->return_types[count])
                    count++;
                if (count > 0) {
                    ret_types = (const CBMType **)cbm_arena_alloc(
                        arena, (count + 1) * sizeof(const CBMType *));
                    for (int j = 0; j < count; j++) {
                        ret_types[j] =
                            rust_parse_return_type_text(arena, d->return_types[j], module_qn);
                    }
                    ret_types[count] = NULL;
                }
            } else if (d->return_type && d->return_type[0]) {
                ret_types = (const CBMType **)cbm_arena_alloc(arena, 2 * sizeof(const CBMType *));
                ret_types[0] = rust_parse_return_type_text(arena, d->return_type, module_qn);
                ret_types[1] = NULL;
            }
            const CBMType **param_types = NULL;
            const char **param_names = d->param_names;
            if (d->signature_param_types || d->signature_param_count > 0) {
                RustSignatureParamParserContext parser_ctx = {.module_qn = module_qn};
                param_types = cbm_type_materialize_signature_params(
                    arena, d->signature_param_types, d->signature_param_count,
                    rust_signature_param_type_adapter, &parser_ctx);
                param_names = NULL;
            } else if (d->param_types) {
                int count = 0;
                while (d->param_types[count])
                    count++;
                if (count > 0) {
                    param_types = (const CBMType **)cbm_arena_alloc(
                        arena, (count + 1) * sizeof(const CBMType *));
                    for (int j = 0; j < count; j++) {
                        param_types[j] =
                            rust_parse_return_type_text(arena, d->param_types[j], module_qn);
                    }
                    param_types[count] = NULL;
                }
            }
            rf.signature = cbm_type_func(arena, param_names, param_types, ret_types);

            if (strcmp(d->label, "Method") == 0 && d->parent_class) {
                rf.receiver_type = d->parent_class;
                if (cbm_idxmemo_get(&type_idx, rf.receiver_type) < 0) {
                    CBMRegisteredType auto_t;
                    memset(&auto_t, 0, sizeof(auto_t));
                    auto_t.qualified_name = rf.receiver_type;
                    const char *dot = strrchr(rf.receiver_type, '.');
                    auto_t.short_name = dot ? cbm_arena_strdup(arena, dot + 1) : rf.receiver_type;
                    cbm_registry_add_type(reg, auto_t);
                    cbm_idxmemo_put_if_absent(&type_idx, arena, auto_t.qualified_name,
                                              reg->type_count - 1);
                }
            }
            if (rf.receiver_type && rf.impl_trait_qn && d->receiver) {
                rf.impl_key =
                    cbm_rust_impl_key(arena, rf.receiver_type, d->receiver, d->impl_trait);
            }

            cbm_registry_add_func(reg, rf);
        }
    }

    /* Phase B: walk the AST to extract struct fields + record `impl Trait
     * for Type` linkage as embedded types. */
    if (!ts_node_is_null(root)) {
        uint32_t nc = ts_node_child_count(root);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode top = ts_node_child(root, i);
            if (ts_node_is_null(top))
                continue;
            const char *tk = ts_node_type(top);

            if (strcmp(tk, "struct_item") == 0) {
                TSNode name_node = ts_node_child_by_field_name(top, "name", 4);
                TSNode body = ts_node_child_by_field_name(top, "body", 4);
                if (ts_node_is_null(name_node) || ts_node_is_null(body))
                    continue;
                char *tn = cbm_node_text(arena, name_node, source);
                if (!tn || !tn[0])
                    continue;
                const char *type_qn = cbm_arena_sprintf(arena, "%s.%s", module_qn, tn);

                /* Iterate field_declaration_list / ordered_field_declaration_list. */
                if (strcmp(ts_node_type(body), "field_declaration_list") == 0) {
                    uint32_t fc = ts_node_named_child_count(body);
                    const char *fld_names[64];
                    const CBMType *fld_types[64];
                    int fld_count = 0;
                    for (uint32_t j = 0; j < fc && fld_count < 63; j++) {
                        TSNode fd = ts_node_named_child(body, j);
                        if (strcmp(ts_node_type(fd), "field_declaration") != 0)
                            continue;
                        TSNode fn = ts_node_child_by_field_name(fd, "name", 4);
                        TSNode ft = ts_node_child_by_field_name(fd, "type", 4);
                        char *fname = cbm_node_text(arena, fn, source);
                        if (!fname)
                            continue;
                        /* Build a temporary context for parsing types. */
                        RustLSPContext tmp;
                        memset(&tmp, 0, sizeof(tmp));
                        tmp.arena = arena;
                        tmp.source = source;
                        tmp.source_len = (int)strlen(source);
                        tmp.registry = reg;
                        tmp.module_qn = module_qn;
                        const CBMType *ft_t = rust_parse_type_node(&tmp, ft);
                        fld_names[fld_count] = fname;
                        fld_types[fld_count] = ft_t;
                        fld_count++;
                    }
                    if (fld_count > 0) {
                        for (int ti = 0; ti < reg->type_count; ti++) {
                            if (reg->types[ti].qualified_name &&
                                strcmp(reg->types[ti].qualified_name, type_qn) == 0) {
                                const char **names = (const char **)cbm_arena_alloc(
                                    arena, (fld_count + 1) * sizeof(const char *));
                                const CBMType **types = (const CBMType **)cbm_arena_alloc(
                                    arena, (fld_count + 1) * sizeof(const CBMType *));
                                for (int fi = 0; fi < fld_count; fi++) {
                                    names[fi] = fld_names[fi];
                                    types[fi] = fld_types[fi];
                                }
                                names[fld_count] = NULL;
                                types[fld_count] = NULL;
                                reg->types[ti].field_names = names;
                                reg->types[ti].field_types = types;
                                break;
                            }
                        }
                    }
                }
            }

            if (strcmp(tk, "trait_item") == 0) {
                TSNode name_node = ts_node_child_by_field_name(top, "name", 4);
                TSNode body = ts_node_child_by_field_name(top, "body", 4);
                if (ts_node_is_null(name_node))
                    continue;
                char *tn = cbm_node_text(arena, name_node, source);
                if (!tn || !tn[0])
                    continue;
                const char *trait_qn = cbm_arena_sprintf(arena, "%s.%s", module_qn, tn);

                /* Mark as interface and collect method names. */
                for (int ti = 0; ti < reg->type_count; ti++) {
                    if (!reg->types[ti].qualified_name)
                        continue;
                    if (strcmp(reg->types[ti].qualified_name, trait_qn) == 0) {
                        reg->types[ti].is_interface = true;
                        if (!ts_node_is_null(body)) {
                            const char *methods[64];
                            int mc = 0;
                            uint32_t bc = ts_node_named_child_count(body);
                            for (uint32_t j = 0; j < bc && mc < 63; j++) {
                                TSNode item = ts_node_named_child(body, j);
                                const char *ik = ts_node_type(item);
                                if (strcmp(ik, "function_item") != 0 &&
                                    strcmp(ik, "function_signature_item") != 0)
                                    continue;
                                TSNode mn = ts_node_child_by_field_name(item, "name", 4);
                                if (ts_node_is_null(mn))
                                    continue;
                                char *mname = cbm_node_text(arena, mn, source);
                                if (mname)
                                    methods[mc++] = mname;
                            }
                            if (mc > 0) {
                                const char **arr = (const char **)cbm_arena_alloc(
                                    arena, (mc + 1) * sizeof(const char *));
                                for (int mi = 0; mi < mc; mi++)
                                    arr[mi] = methods[mi];
                                arr[mc] = NULL;
                                reg->types[ti].method_names = arr;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    /* Phase B1: walk top-level free `function_item`s to harvest their
     * return types into the registry — `extract_defs` does not fill
     * `return_type` for Rust free functions either, so a let-binding
     * like `let v = pair();` would otherwise know nothing about pair's
     * return tuple. */
    if (!ts_node_is_null(root)) {
        RustLSPContext tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.arena = arena;
        tmp.source = source;
        tmp.source_len = (int)strlen(source);
        tmp.registry = reg;
        tmp.module_qn = module_qn;

        uint32_t rnc = ts_node_child_count(root);
        for (uint32_t i = 0; i < rnc; i++) {
            TSNode top = ts_node_child(root, i);
            if (ts_node_is_null(top) || strcmp(ts_node_type(top), "function_item") != 0)
                continue;
            TSNode mn = ts_node_child_by_field_name(top, "name", 4);
            TSNode rtn = ts_node_child_by_field_name(top, "return_type", 11);
            if (ts_node_is_null(mn) || ts_node_is_null(rtn))
                continue;
            char *fname = cbm_node_text(arena, mn, source);
            if (!fname)
                continue;
            const CBMType *ret = rust_parse_type_node(&tmp, rtn);
            const char *fn_qn = cbm_arena_sprintf(arena, "%s.%s", module_qn, fname);
            for (int k = 0; k < reg->func_count; k++) {
                CBMRegisteredFunc *rf = &reg->funcs[k];
                if (!rf->qualified_name)
                    continue;
                if (rf->receiver_type)
                    continue; /* free fns only */
                if (strcmp(rf->qualified_name, fn_qn) != 0)
                    continue;
                const CBMType **ret_arr =
                    (const CBMType **)cbm_arena_alloc(arena, 2 * sizeof(const CBMType *));
                ret_arr[0] = ret;
                ret_arr[1] = NULL;
                rf->signature = cbm_type_func_replace_returns(arena, rf->signature, ret_arr);
                break;
            }
        }
    }

    /* Phase A2: derive-macro synthesis (rust_register_derives). */
    for (int i = 0; i < result->defs.count; i++) {
        CBMDefinition *d = &result->defs.items[i];
        if (!d->qualified_name || !d->name || !d->decorators || !cbm_label_is_type_like(d->label)) {
            continue;
        }
        int32_t tix = cbm_idxmemo_get(&type_idx, d->qualified_name);
        if (tix >= 0) {
            rust_register_derives(arena, reg, tix, (const char *const *)d->decorators);
        }
    }

    /* Phase B2: walk impl bodies to harvest each method's return type
     * from the AST. The unified `extract_defs` extractor does not fill
     * `return_type` for Rust impl methods, so without this pass our
     * registered functions have no return-type signature and chained
     * method calls (`File::open().read()`) break. */
    if (!ts_node_is_null(root)) {
        uint32_t rnc = ts_node_child_count(root);
        for (uint32_t i = 0; i < rnc; i++) {
            TSNode top = ts_node_child(root, i);
            if (ts_node_is_null(top) || strcmp(ts_node_type(top), "impl_item") != 0)
                continue;
            TSNode type_node = ts_node_child_by_field_name(top, "type", 4);
            TSNode body = ts_node_child_by_field_name(top, "body", 4);
            if (ts_node_is_null(type_node) || ts_node_is_null(body))
                continue;
            char *type_name = cbm_node_text(arena, type_node, source);
            if (!type_name || !type_name[0])
                continue;
            const char *type_qn = cbm_arena_sprintf(arena, "%s.%s", module_qn, type_name);

            RustLSPContext tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.arena = arena;
            tmp.source = source;
            tmp.source_len = (int)strlen(source);
            tmp.registry = reg;
            tmp.module_qn = module_qn;
            tmp.self_type_qn = type_qn;

            uint32_t bnc = ts_node_child_count(body);
            for (uint32_t j = 0; j < bnc; j++) {
                TSNode item = ts_node_child(body, j);
                if (ts_node_is_null(item) || !ts_node_is_named(item))
                    continue;
                if (strcmp(ts_node_type(item), "function_item") != 0)
                    continue;
                TSNode mn = ts_node_child_by_field_name(item, "name", 4);
                TSNode rtn = ts_node_child_by_field_name(item, "return_type", 11);
                if (ts_node_is_null(mn) || ts_node_is_null(rtn))
                    continue;
                char *mname = cbm_node_text(arena, mn, source);
                if (!mname)
                    continue;
                const CBMType *ret = rust_parse_type_node(&tmp, rtn);
                /* Substitute Self -> receiver type so chains work. */
                if (ret && ret->kind == CBM_TYPE_NAMED &&
                    strcmp(ret->data.named.qualified_name, "Self") == 0) {
                    ret = cbm_type_named(arena, type_qn);
                }
                /* Patch the registered function's signature. */
                for (int k = 0; k < reg->func_count; k++) {
                    CBMRegisteredFunc *rf = &reg->funcs[k];
                    if (!rf->receiver_type || !rf->short_name)
                        continue;
                    if (strcmp(rf->receiver_type, type_qn) != 0)
                        continue;
                    if (strcmp(rf->short_name, mname) != 0)
                        continue;
                    const CBMType **ret_arr =
                        (const CBMType **)cbm_arena_alloc(arena, 2 * sizeof(const CBMType *));
                    ret_arr[0] = ret;
                    ret_arr[1] = NULL;
                    rf->signature = cbm_type_func_replace_returns(arena, rf->signature, ret_arr);
                    break;
                }
            }
        }
    }

    /* Phase C: encode `impl Trait for Type` as `embedded_types` on the
     * receiver type so trait dispatch can find them. */
    for (int i = 0; i < result->impl_traits.count; i++) {
        CBMImplTrait *it = &result->impl_traits.items[i];
        if (!it->struct_name || !it->trait_name)
            continue;
        const char *recv_qn = it->struct_qn
                                  ? it->struct_qn
                                  : cbm_arena_sprintf(arena, "%s.%s", module_qn, it->struct_name);
        const char *trait_qn =
            rust_canonical_impl_trait(arena, reg, &trait_names, it->trait_name, module_qn);
        if (!trait_qn) {
            continue;
        }

        CBMRegisteredType *rt = NULL;
        int32_t type_index = cbm_idxmemo_get(&type_idx, recv_qn);
        if (type_index >= 0) {
            rt = &reg->types[type_index];
        }
        if (!rt) {
            CBMRegisteredType auto_t;
            memset(&auto_t, 0, sizeof(auto_t));
            auto_t.qualified_name = recv_qn;
            const char *dot = strrchr(recv_qn, '.');
            auto_t.short_name = dot ? dot + 1 : recv_qn;
            cbm_registry_add_type(reg, auto_t);
            rt = &reg->types[reg->type_count - 1];
            cbm_idxmemo_put_if_absent(&type_idx, arena, auto_t.qualified_name, reg->type_count - 1);
        }
        rust_registered_type_add_embedded(arena, rt, trait_qn);
    }
}

void cbm_run_rust_lsp_with_manifest(CBMArena *arena, CBMFileResult *result, const char *source,
                                    int source_len, TSNode root,
                                    const struct CBMCargoManifest *manifest) {
    if (!arena || !result || !source)
        return;
    const char *module_qn = result->module_qn ? result->module_qn : "rust";

    CBMTypeRegistry reg;
    cbm_rust_build_local_registry(arena, &reg, result, module_qn, root, source);
    /* Finalize after all per-file adds so lookups during the walk
     * use the hash buckets. */
    cbm_registry_finalize(&reg);

    RustLSPContext ctx;
    rust_lsp_init(&ctx, arena, source, source_len, &reg, module_qn, &result->resolved_calls);
    ctx.cargo_manifest = manifest;
    /* Let the resolver inject synthetic syntactic calls for operator/macro
     * desugaring so those recovered calls reach the CALLS-edge pipeline. */
    ctx.syn_calls = &result->calls;

    rust_collect_uses(&ctx, root);
    ctx.file_glob_count = ctx.glob_count;
    ctx.file_use_count = ctx.use_count;
    rust_lsp_process_file(&ctx, root);
}

void cbm_run_rust_lsp(CBMArena *arena, CBMFileResult *result, const char *source, int source_len,
                      TSNode root) {
    cbm_run_rust_lsp_with_manifest(arena, result, source, source_len, root, NULL);
}

/* ════════════════════════════════════════════════════════════════════
 * 12. Cross-file + batch
 * ════════════════════════════════════════════════════════════════════ */

extern const TSLanguage *tree_sitter_rust(void);

/* Populate + finalize a Rust cross-file type registry from `defs`. Shared by the
 * per-file resolver (cbm_run_rust_lsp_cross_with_manifest) and the build-once shared
 * registry (cbm_rust_build_cross_registry) so both produce a byte-identical registry.
 * `module_qn` is ONLY the fallback used to qualify a def's return type when that def
 * carries no def_module_qn; pass NULL for the shared build (all_defs always carry
 * def_module_qn — verified: 0 NULL across the C + Rust kernel corpora). */
typedef struct {
    const CBMTypeRegistry *reg;
    const CBMIdxMemo *type_idx;
} RustDeclaredTypes;

static const CBMRegisteredType *rust_declared_type(const void *ctx, const char *qn) {
    const RustDeclaredTypes *declared = (const RustDeclaredTypes *)ctx;
    int32_t index = cbm_idxmemo_get(declared->type_idx, qn);
    return index >= 0 ? &declared->reg->types[index] : NULL;
}

/* Receiver each Method / impl-relation def is registered on: its declared
 * self type when the impl sits in another file (rust_impl_declared_self),
 * else the extracted receiver. Runs while `type_idx` holds declared types
 * only, which is what the final registry answers for these QNs at walk time:
 * a receiver left unmoved is auto-registered below, one moved never is. */
static const char **rust_registered_receivers(CBMArena *arena, const CBMTypeRegistry *reg,
                                              const CBMIdxMemo *type_idx,
                                              const CBMRustModTree *tree, CBMRustLSPDef *defs,
                                              int def_count, const char *module_qn) {
    const char **receivers =
        (const char **)cbm_arena_alloc(arena, (size_t)(def_count > 0 ? def_count : 1) *
                                                  sizeof(const char *));
    if (!receivers) {
        return NULL;
    }
    RustDeclaredTypes declared = {.reg = reg, .type_idx = type_idx};
    /* receiver QN -> index of the def that decided it, one memo per def kind:
     * a method moves to a std wrapper's QN, its impl relation does not, so a
     * relation's decision never stands for a method's. */
    CBMIdxMemo seen_methods = {0};
    CBMIdxMemo seen_relations = {0};
    for (int i = 0; i < def_count; i++) {
        CBMRustLSPDef *d = &defs[i];
        receivers[i] = d->receiver_type;
        bool owns_methods = d->is_rust_impl_relation || (d->label && strcmp(d->label, "Method") == 0);
        if (!tree || !owns_methods || !d->receiver_type || !d->receiver_spelling) {
            continue;
        }
        CBMIdxMemo *seen = d->is_rust_impl_relation ? &seen_relations : &seen_methods;
        int32_t decided = cbm_idxmemo_get(seen, d->receiver_type);
        if (decided >= 0) {
            receivers[i] = receivers[decided];
            continue;
        }
        const char *def_mod = d->def_module_qn ? d->def_module_qn : module_qn;
        const char *self = rust_impl_declared_self(tree, arena, def_mod, d->receiver_type,
                                                   d->receiver_spelling, rust_declared_type,
                                                   &declared);
        if (!self && !d->is_rust_impl_relation) {
            self = rust_impl_std_wrapper_self(tree, arena, def_mod, d->receiver_spelling);
        }
        if (self) {
            receivers[i] = self;
        }
        cbm_idxmemo_put_if_absent(seen, arena, d->receiver_type, i);
    }
    return receivers;
}

/* Record on `trait` each supertrait in `spellings` ("|"-separated bounds as
 * written in `module_qn`) that names a trait declared in the corpus: by the
 * module tree, else by the canonical trait-name index. Marker bounds and
 * names that settle on no registered trait are dropped. */
static void rust_register_supertraits(CBMArena *arena, const CBMTypeRegistry *reg,
                                      const CBMIdxMemo *type_idx, const RustTraitNameIndex *names,
                                      const CBMRustModTree *tree, CBMRegisteredType *trait,
                                      const char *spellings, const char *module_qn) {
    char *buf = cbm_arena_strdup(arena, spellings);
    for (char *save = NULL, *s = buf ? strtok_r(buf, "|", &save) : NULL; s;
         s = strtok_r(NULL, "|", &save)) {
        if (rust_bound_is_marker(s, strlen(s))) {
            continue;
        }
        const char *lt = strchr(s, '<');
        const char *path = lt ? cbm_arena_strndup(arena, s, (size_t)(lt - s)) : s;
        const char *qn = rust_text_type_qn_by_module_tree(tree, arena, module_qn, path);
        int32_t index = qn ? cbm_idxmemo_get(type_idx, qn) : -1;
        if (index < 0 || !reg->types[index].is_interface) {
            qn = rust_canonical_impl_trait(arena, reg, names, s, module_qn);
        }
        if (qn && strcmp(qn, trait->qualified_name) != 0) {
            rust_registered_type_add_embedded(
                arena, trait, cbm_arena_sprintf(arena, "%s%s", RUST_SUPERTRAIT_PREFIX, qn));
        }
    }
}

static void rust_populate_cross_registry(CBMTypeRegistry *reg, CBMArena *arena,
                                         CBMRustLSPDef *defs, int def_count,
                                         const char *module_qn, const CBMRustModTree *tree) {
    cbm_registry_init(reg, arena);
    cbm_rust_stdlib_register(reg, arena);

    /* qn → (type index + 1), FIRST occurrence wins (mirrors the linear scans
     * this map replaces). Both in-loop registry probes below — the receiver
     * auto-registration check and the trait-linkage lookup — used to scan the
     * UNFINALIZED registry linearly (no buckets exist before finalize): the
     * checklist's lookup-in-registration-loop pattern. Invisible on small
     * per-file builds; on the shared all_defs build (~1.4M entries) those
     * scans were a constant ~63 s of the kernel run — and the sibling
     * null-filter files waited on the build once-guard for exactly that long,
     * which is why no resolution-side fix ever moved their wall time. Index,
     * not pointer, because reg->types reallocs as it grows. */
    CBMIdxMemo type_idx = {0};
    for (int ti = 0; ti < reg->type_count; ti++) {
        const char *qn = reg->types[ti].qualified_name;
        if (qn) {
            cbm_idxmemo_put_if_absent(&type_idx, arena, qn, ti);
        }
    }

    /* Register all type declarations before resolving raw impl-trait names.
     * This makes provenance independent of source/file ordering. */
    for (int i = 0; i < def_count; i++) {
        CBMRustLSPDef *d = &defs[i];
        if (!d->qualified_name || !d->short_name || !d->label)
            continue;
        if (!cbm_label_is_type_like(d->label)) {
            continue;
        }
        CBMRegisteredType rt;
        memset(&rt, 0, sizeof(rt));
        rt.qualified_name = cbm_arena_strdup(arena, d->qualified_name);
        rt.short_name = cbm_arena_strdup(arena, d->short_name);
        rt.is_interface =
            d->is_interface || strcmp(d->label, "Trait") == 0 || strcmp(d->label, "Interface") == 0;
        rust_register_type_alias(arena, &rt, d->return_types, d->signature_param_types,
                                 d->signature_param_count,
                                 d->def_module_qn ? d->def_module_qn : module_qn, tree);
        cbm_registry_add_type(reg, rt);
        cbm_idxmemo_put_if_absent(&type_idx, arena, rt.qualified_name, reg->type_count - 1);
    }
    for (int i = 0; i < def_count; i++) {
        CBMRustLSPDef *d = &defs[i];
        if (d->decorators && d->qualified_name && d->label && cbm_label_is_type_like(d->label)) {
            rust_register_derives(arena, reg, cbm_idxmemo_get(&type_idx, d->qualified_name),
                                  (const char *const *)d->decorators);
        }
    }

    const char **receivers =
        rust_registered_receivers(arena, reg, &type_idx, tree, defs, def_count, module_qn);

    RustTraitNameIndex trait_names;
    rust_trait_name_index_build(&trait_names, arena, reg);

    for (int i = 0; i < def_count; i++) {
        CBMRustLSPDef *d = &defs[i];
        int32_t type_index = d->embedded_types && d->qualified_name
                                 ? cbm_idxmemo_get(&type_idx, d->qualified_name)
                                 : -1;
        if (type_index >= 0 && reg->types[type_index].is_interface) {
            rust_register_supertraits(arena, reg, &type_idx, &trait_names, tree,
                                      &reg->types[type_index], d->embedded_types,
                                      d->def_module_qn ? d->def_module_qn : module_qn);
        }
    }

    /* Materialize independent impl relations first, including empty impls. */
    for (int i = 0; i < def_count; i++) {
        CBMRustLSPDef *d = &defs[i];
        const char *receiver = receivers ? receivers[i] : d->receiver_type;
        if (!d->is_rust_impl_relation || !receiver || !d->trait_qn) {
            continue;
        }
        const char *def_mod = d->def_module_qn ? d->def_module_qn : module_qn;
        const char *trait_qn =
            rust_canonical_impl_trait(arena, reg, &trait_names, d->trait_qn, def_mod);
        if (!trait_qn) {
            continue;
        }
        int32_t type_index = cbm_idxmemo_get(&type_idx, receiver);
        if (type_index < 0) {
            CBMRegisteredType auto_t;
            memset(&auto_t, 0, sizeof(auto_t));
            auto_t.qualified_name = cbm_arena_strdup(arena, receiver);
            const char *dot = strrchr(receiver, '.');
            auto_t.short_name = dot ? cbm_arena_strdup(arena, dot + 1) : auto_t.qualified_name;
            cbm_registry_add_type(reg, auto_t);
            type_index = reg->type_count - 1;
            cbm_idxmemo_put_if_absent(&type_idx, arena, auto_t.qualified_name, type_index);
        }
        rust_registered_type_add_embedded(arena, &reg->types[type_index], trait_qn);
    }

    for (int i = 0; i < def_count; i++) {
        CBMRustLSPDef *d = &defs[i];
        if (!d->qualified_name || !d->short_name || !d->label || d->is_rust_impl_relation)
            continue;
        const char *def_mod = d->def_module_qn ? d->def_module_qn : module_qn;

        /* Struct fields (`name:type|name:type`) let `self.field.method()` and
         * `value.field.method()` type their receiver across files. Field types
         * are spelled in the defining module; rust_registered_type_qn recovers
         * their registered identity at use time. */
        if (d->field_defs && d->field_defs[0] && cbm_label_is_type_like(d->label)) {
            int32_t tix = cbm_idxmemo_get(&type_idx, d->qualified_name);
            if (tix >= 0 && !reg->types[tix].field_names) {
                int count = 1;
                for (const char *p = d->field_defs; *p; p++) {
                    if (*p == '|')
                        count++;
                }
                const char **names =
                    (const char **)cbm_arena_alloc(arena, (size_t)(count + 1) * sizeof(char *));
                const CBMType **types = (const CBMType **)cbm_arena_alloc(
                    arena, (size_t)(count + 1) * sizeof(const CBMType *));
                int n = 0;
                char *buf = cbm_arena_strdup(arena, d->field_defs);
                for (char *entry = buf; entry && *entry;) {
                    char *bar = strchr(entry, '|');
                    if (bar)
                        *bar = '\0';
                    char *colon = strchr(entry, ':');
                    if (colon && colon > entry && colon[1]) {
                        *colon = '\0';
                        names[n] = entry;
                        types[n] = rust_parse_def_type_text(arena, colon + 1, def_mod, tree);
                        n++;
                    }
                    entry = bar ? bar + 1 : NULL;
                }
                names[n] = NULL;
                types[n] = NULL;
                if (n > 0) {
                    reg->types[tix].field_names = names;
                    reg->types[tix].field_types = types;
                }
            }
        }

        if (strcmp(d->label, "Function") == 0 || strcmp(d->label, "Method") == 0) {
            CBMRegisteredFunc rf;
            memset(&rf, 0, sizeof(rf));
            rf.qualified_name = cbm_arena_strdup(arena, d->qualified_name);
            rf.short_name = cbm_arena_strdup(arena, d->short_name);
            rf.min_params = -1;
            if (d->is_abstract) {
                rf.flags |= CBM_FUNC_FLAG_RUST_ABSTRACT;
            }
            if (d->is_async) {
                rf.flags |= CBM_FUNC_FLAG_ASYNC;
            }
            if (d->has_rust_self) {
                rf.flags |= CBM_FUNC_FLAG_RUST_SELF;
            }
            if (d->trait_qn && d->trait_qn[0]) {
                rf.flags |= CBM_FUNC_FLAG_RUST_TRAIT_IMPL;
                rf.impl_trait_qn =
                    rust_canonical_impl_trait(arena, reg, &trait_names, d->trait_qn, def_mod);
            } else if (d->is_rust_private && def_mod) {
                rf.rust_private_module = cbm_arena_strdup(arena, def_mod);
            }

            /* Build sig from return_types text. */
            const CBMType **ret_types = NULL;
            if (d->return_types && d->return_types[0]) {
                int count = 1;
                for (const char *p = d->return_types; *p; p++) {
                    if (*p == '|')
                        count++;
                }
                ret_types =
                    (const CBMType **)cbm_arena_alloc(arena, (count + 1) * sizeof(const CBMType *));
                int idx = 0;
                char *buf = cbm_arena_strdup(arena, d->return_types);
                char *start = buf;
                for (char *p = buf;; p++) {
                    if (*p == '|' || *p == '\0') {
                        char save = *p;
                        *p = '\0';
                        if (start[0]) {
                            ret_types[idx++] =
                                rust_parse_def_type_text(arena, start, def_mod, tree);
                        }
                        if (save == '\0')
                            break;
                        start = p + 1;
                    }
                }
                ret_types[idx] = NULL;
            }
            RustSignatureParamParserContext parser_ctx = {.module_qn = def_mod, .tree = tree};
            const CBMType **param_types = cbm_type_materialize_signature_params(
                arena, d->signature_param_types, d->signature_param_count,
                rust_signature_param_type_adapter, &parser_ctx);
            rf.signature = cbm_type_func(arena, NULL, param_types, ret_types);

            const char *receiver = receivers ? receivers[i] : d->receiver_type;
            if (strcmp(d->label, "Method") == 0 && receiver && receiver[0]) {
                rf.receiver_type = cbm_arena_strdup(arena, receiver);
                if (cbm_idxmemo_get(&type_idx, rf.receiver_type) < 0) {
                    CBMRegisteredType auto_t;
                    memset(&auto_t, 0, sizeof(auto_t));
                    auto_t.qualified_name = rf.receiver_type;
                    const char *dot = strrchr(receiver, '.');
                    auto_t.short_name = dot ? cbm_arena_strdup(arena, dot + 1) : rf.receiver_type;
                    cbm_registry_add_type(reg, auto_t);
                    cbm_idxmemo_put_if_absent(&type_idx, arena, auto_t.qualified_name,
                                              reg->type_count - 1);
                }
            }
            if (rf.receiver_type && rf.impl_trait_qn && d->receiver_spelling) {
                rf.impl_key = cbm_rust_impl_key(arena, rf.receiver_type, d->receiver_spelling,
                                                d->trait_qn);
                if (rust_type_derefs_to_first_arg(rf.receiver_type)) {
                    rf.rust_wrapper_impl_self = rust_wrapper_impl_self_type(
                        arena, d->receiver_spelling, rf.receiver_type, def_mod, tree);
                }
            }

            cbm_registry_add_func(reg, rf);

            /* Method provenance also carries the type-level relation for
             * backwards-compatible direct CBMRustLSPDef callers. Production
             * extraction additionally emits an independent relation record.
             * An impl on a std wrapper applies only to the wrapper arguments
             * its self type spells, so it marks no relation on the wrapper. */
            if (rf.receiver_type && rf.impl_trait_qn && !rf.rust_wrapper_impl_self) {
                CBMRegisteredType *rt = NULL;
                int32_t tix = cbm_idxmemo_get(&type_idx, rf.receiver_type);
                if (tix >= 0) {
                    rt = &reg->types[tix];
                }
                if (rt) {
                    rust_registered_type_add_embedded(arena, rt, rf.impl_trait_qn);
                }
            }
        }
    }

    /* Finalise the cross-file registry now that all defs are added.
     * (type_idx is arena-owned — freed with the registry's arena.) */
    cbm_registry_finalize(reg);
}

/* ════════════════════════════════════════════════════════════════════
 * 12. Impl-header links: `impl [Trait for] Type` → registered identities
 * ════════════════════════════════════════════════════════════════════
 *
 * Each impl block's self type and trait are resolved with the same path
 * rules as every other Rust path (source `use` bindings, globs, `crate::`,
 * `super::`, workspace crates), at the block's own lexical scope: inline
 * `mod` bodies and function bodies add their own `use` declarations, and a
 * bare name declared as an item in an enclosing block is local to that
 * block, so it never resolves to a module-level namesake. Only an exact
 * registered type (trait for the trait position, non-trait for the self
 * position) is accepted; anything else leaves the field NULL. */

#define RUST_IMPL_LINK_MAX_DEPTH 512

typedef struct {
    /* Type-namespace items the file declares outside function bodies (inline
     * module items included: they are extracted under the file's module QN). */
    CBMIdxMemo file_items;
} RustFileItems;

static const char *const RUST_TYPE_ITEM_KINDS[] = {"struct_item", "enum_item", "union_item",
                                                   "type_item",   "trait_item", NULL};

static bool rust_is_type_item(const char *kind) {
    for (int k = 0; RUST_TYPE_ITEM_KINDS[k]; k++) {
        if (strcmp(kind, RUST_TYPE_ITEM_KINDS[k]) == 0) {
            return true;
        }
    }
    return false;
}

/* Record the type items of a module scope (file root or inline module body). */
static void rust_file_items_collect(RustLSPContext *ctx, TSNode scope, RustFileItems *items,
                                    int depth) {
    if (depth > RUST_IMPL_LINK_MAX_DEPTH) {
        return;
    }
    uint32_t count = ts_node_named_child_count(scope);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(scope, i);
        const char *kind = ts_node_type(child);
        if (rust_is_type_item(kind)) {
            char *name = rust_node_text(ctx, ts_node_child_by_field_name(child, "name", 4));
            if (name && name[0]) {
                cbm_idxmemo_put_if_absent(&items->file_items, ctx->arena, name, 1);
            }
        } else if (strcmp(kind, "mod_item") == 0) {
            TSNode body = ts_node_child_by_field_name(child, "body", 4);
            if (!ts_node_is_null(body)) {
                rust_file_items_collect(ctx, body, items, depth + 1);
            }
        }
    }
}

/* A scope that declares items: a block, an inline module body, the file. */
static bool rust_is_item_scope(TSNode node) {
    const char *kind = ts_node_type(node);
    if (strcmp(kind, "block") == 0 || strcmp(kind, "source_file") == 0) {
        return true;
    }
    TSNode parent = ts_node_parent(node);
    return strcmp(kind, "declaration_list") == 0 && !ts_node_is_null(parent) &&
           strcmp(ts_node_type(parent), "mod_item") == 0;
}

static bool rust_item_named(RustLSPContext *ctx, TSNode item, const char *name, size_t len) {
    char *text = rust_node_text(ctx, ts_node_child_by_field_name(item, "name", 4));
    return text && strlen(text) == len && strncmp(text, name, len) == 0;
}

/* Number of type-namespace items (type items and modules) `scope` declares
 * as `name`; *out is the last of them. */
static int rust_scope_items_named(RustLSPContext *ctx, TSNode scope, const char *name, size_t len,
                                  TSNode *out) {
    int found = 0;
    uint32_t count = ts_node_named_child_count(scope);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(scope, i);
        const char *kind = ts_node_type(child);
        if ((rust_is_type_item(kind) || strcmp(kind, "mod_item") == 0) &&
            rust_item_named(ctx, child, name, len)) {
            *out = child;
            found++;
        }
    }
    return found;
}

/* True when one of `scope`'s own `use` declarations binds `name`. */
static bool rust_scope_uses_name(RustLSPContext *ctx, TSNode scope, const char *name, size_t len) {
    uint32_t count = ts_node_named_child_count(scope);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(scope, i);
        if (strcmp(ts_node_type(child), "use_declaration") != 0) {
            continue;
        }
        CBMImportArray imports = {0};
        cbm_extract_rust_use_tree(ctx->arena, ts_node_child_by_field_name(child, "argument", 8),
                                  ctx->source, &imports);
        for (int k = 0; k < imports.count; k++) {
            const char *local = imports.items[k].local_name;
            if (local && strlen(local) == len && strncmp(local, name, len) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* The type item `item` denotes once the inline-module segments `rest`
 * (NULL: none) are followed, or a null node. */
static TSNode rust_inline_path_item(RustLSPContext *ctx, TSNode item, const char *rest) {
    TSNode none = {0};
    while (rest) {
        TSNode body = ts_node_child_by_field_name(item, "body", 4);
        if (strcmp(ts_node_type(item), "mod_item") != 0 || ts_node_is_null(body)) {
            return none;
        }
        const char *sep = strstr(rest, "::");
        size_t len = sep ? (size_t)(sep - rest) : strlen(rest);
        if (rust_scope_items_named(ctx, body, rest, len, &item) != 1) {
            return none;
        }
        rest = sep ? sep + 2 : NULL;
    }
    return rust_is_type_item(ts_node_type(item)) ? item : none;
}

/* The item of this file a header path denotes: the innermost enclosing scope
 * that declares or imports the path's head decides, and inline module
 * segments are followed. A null node when no scope declares the head, when a
 * scope's `use` binds it first, when it names an out-of-line `mod`, or when
 * a module scope declares it more than once (cfg twins keep the module-level
 * resolution). *shadowed is set when a block declares the head but the path
 * does not end at exactly one type item: that name never reaches outer
 * scopes. */
static TSNode rust_impl_local_item(RustLSPContext *ctx, TSNode impl_node, const char *path,
                                   bool *shadowed) {
    TSNode none = {0};
    *shadowed = false;
    const char *sep = strstr(path, "::");
    size_t head_len = sep ? (size_t)(sep - path) : strlen(path);
    for (TSNode scope = ts_node_parent(impl_node); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        if (!rust_is_item_scope(scope)) {
            continue;
        }
        TSNode item = none;
        int found = rust_scope_items_named(ctx, scope, path, head_len, &item);
        if (found == 0) {
            if (rust_scope_uses_name(ctx, scope, path, head_len)) {
                return none;
            }
            continue;
        }
        bool block = strcmp(ts_node_type(scope), "block") == 0;
        bool out_of_line = strcmp(ts_node_type(item), "mod_item") == 0 &&
                           ts_node_is_null(ts_node_child_by_field_name(item, "body", 4));
        if (out_of_line) {
            return none;
        }
        TSNode local = found == 1 ? rust_inline_path_item(ctx, item, sep ? sep + 2 : NULL) : none;
        *shadowed = block && ts_node_is_null(local);
        return local;
    }
    return none;
}

/* Registered identity of in-file type item `item`. Extraction gives every
 * item of a file the file's module QN; a same-QN item other than the one the
 * graph keeps carries a `#L<line>` suffix. *def_line pins the graph node. */
static const char *rust_impl_local_qn(RustLSPContext *ctx, TSNode item, bool want_trait,
                                      uint32_t *def_line) {
    char *name = rust_node_text(ctx, ts_node_child_by_field_name(item, "name", 4));
    if (!name || !name[0] || !ctx->module_qn || !ctx->registry) {
        return NULL;
    }
    uint32_t line = ts_node_start_point(item).row + 1;
    const char *qn = cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, name);
    const CBMRegisteredType *type =
        cbm_registry_lookup_type(ctx->registry, cbm_arena_sprintf(ctx->arena, "%s#L%u", qn, line));
    if (!type) {
        type = cbm_registry_lookup_type(ctx->registry, qn);
    }
    if (!type || !type->qualified_name || type->is_interface != want_trait) {
        return NULL;
    }
    *def_line = line;
    return type->qualified_name;
}

/* QN a bare name denotes at module scope, following Rust's order: an explicit
 * `use` or an item of this file, then exactly one glob import, then the
 * prelude. Registry presence alone cannot stand for "declared here": the
 * registry also carries receiver QNs synthesized from impl blocks. */
static const char *rust_impl_bare_name_qn(RustLSPContext *ctx, const RustFileItems *items,
                                          const char *name) {
    const char *used = rust_resolve_use(ctx, name);
    if (used) {
        return convert_path_to_qn(ctx->arena, used);
    }
    if (ctx->module_qn && cbm_idxmemo_get(&items->file_items, name) >= 0) {
        return cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->module_qn, name);
    }
    const CBMRegisteredType *glob = rust_resolve_glob_type(ctx, name);
    if (glob) {
        return glob->qualified_name;
    }
    return rust_lookup_prelude(name);
}

/* Registered QN of one impl-header type position, or NULL. *def_line is set
 * when the position names an item of this file. */
static const char *rust_impl_header_qn(RustLSPContext *ctx, TSNode impl_node, TSNode type_node,
                                       bool want_trait, const RustFileItems *items,
                                       uint32_t *def_line) {
    if (ts_node_is_null(type_node)) {
        return NULL;
    }
    if (strcmp(ts_node_type(type_node), "generic_type") == 0) {
        type_node = ts_node_child_by_field_name(type_node, "type", 4);
        if (ts_node_is_null(type_node)) {
            return NULL;
        }
    }
    const char *kind = ts_node_type(type_node);
    bool bare = strcmp(kind, "type_identifier") == 0;
    if (!bare && strcmp(kind, "scoped_type_identifier") != 0) {
        return NULL;
    }
    char *path = rust_node_text(ctx, type_node);
    if (!path || !path[0] || strcmp(path, "Self") == 0 || strncmp(path, "Self::", 6) == 0) {
        return NULL;
    }
    if (bare && rust_impl_has_type_param(ctx, impl_node, path)) {
        return NULL;
    }
    bool shadowed = false;
    TSNode local = rust_impl_local_item(ctx, impl_node, path, &shadowed);
    if (!ts_node_is_null(local)) {
        return rust_impl_local_qn(ctx, local, want_trait, def_line);
    }
    if (shadowed) {
        return NULL;
    }
    const char *qn = bare ? rust_impl_bare_name_qn(ctx, items, path)
                          : rust_resolve_path_expr(ctx, path);
    if (!qn || !ctx->registry) {
        return NULL;
    }
    const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, qn);
    if (!type) {
        type = rust_lookup_type_variants(ctx, qn);
    }
    if (!type || !type->qualified_name || type->is_interface != want_trait) {
        return NULL;
    }
    return type->qualified_name;
}

static void rust_record_impl_link(RustLSPContext *ctx, TSNode impl_node,
                                  const RustFileItems *items, CBMRustImplLinkArray *out) {
    CBMRustImplLink link = {0};
    TSNode type_node = ts_node_child_by_field_name(impl_node, "type", 4);
    TSNode trait_node = ts_node_child_by_field_name(impl_node, "trait", 5);
    /* The type rust_process_impl walks the block as, and the registry files
     * its methods under, is the owner (see rust_impl_declared_self). */
    char *type_text = ts_node_is_null(type_node) ? NULL : rust_node_text(ctx, type_node);
    bool blanket = !ts_node_is_null(trait_node) && rust_impl_has_type_param(ctx, impl_node, type_text);
    link.type_qn = blanket ? NULL : rust_impl_declared_self_in_file(ctx, type_text);
    if (!link.type_qn) {
        link.type_qn = rust_impl_header_qn(ctx, impl_node, type_node, false, items, &link.type_line);
    }
    if (link.type_qn && !ts_node_is_null(trait_node)) {
        link.trait_qn =
            rust_impl_header_qn(ctx, impl_node, trait_node, true, items, &link.trait_line);
    }
    link.start_line = ts_node_start_point(impl_node).row + 1;
    link.end_line = ts_node_end_point(impl_node).row + 1;
    /* Unresolved blocks are recorded too: their methods must not fall to an
     * enclosing block's owner. */
    cbm_rustimpllink_push(out, ctx->arena, link);
}

static void rust_collect_impl_links_in(RustLSPContext *ctx, TSNode node,
                                       const RustFileItems *items, int depth,
                                       CBMRustImplLinkArray *out);

/* Walk a scope that may carry its own `use` declarations (inline module body
 * or block), restoring the enclosing bindings afterwards. */
static void rust_collect_impl_links_scoped(RustLSPContext *ctx, TSNode scope,
                                           const char *relative_module_qn,
                                           const RustFileItems *items, int depth,
                                           CBMRustImplLinkArray *out) {
    int saved_uses = ctx->use_count;
    int saved_globs = ctx->glob_count;
    const char *saved_relative = ctx->relative_module_qn;
    ctx->relative_module_qn = relative_module_qn;
    rust_collect_uses(ctx, scope);
    rust_collect_impl_links_in(ctx, scope, items, depth + 1, out);
    ctx->relative_module_qn = saved_relative;
    ctx->use_count = saved_uses;
    ctx->glob_count = saved_globs;
}

static void rust_collect_impl_links_in(RustLSPContext *ctx, TSNode node,
                                       const RustFileItems *items, int depth,
                                       CBMRustImplLinkArray *out) {
    if (depth > RUST_IMPL_LINK_MAX_DEPTH) {
        return;
    }
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *kind = ts_node_type(child);
        if (strcmp(kind, "impl_item") == 0) {
            rust_record_impl_link(ctx, child, items, out);
            rust_collect_impl_links_in(ctx, child, items, depth + 1, out);
        } else if (strcmp(kind, "mod_item") == 0) {
            TSNode body = ts_node_child_by_field_name(child, "body", 4);
            char *name = rust_node_text(ctx, ts_node_child_by_field_name(child, "name", 4));
            if (ts_node_is_null(body) || !name) {
                continue;
            }
            /* Inline module items are extracted under the file's module QN;
             * only `self::` / `super::` see the extra module level. */
            const char *relative =
                ctx->relative_module_qn
                    ? cbm_arena_sprintf(ctx->arena, "%s.%s", ctx->relative_module_qn, name)
                    : NULL;
            rust_collect_impl_links_scoped(ctx, body, relative, items, depth, out);
        } else if (strcmp(kind, "block") == 0) {
            rust_collect_impl_links_scoped(ctx, child, ctx->relative_module_qn, items, depth,
                                           out);
        } else {
            rust_collect_impl_links_in(ctx, child, items, depth + 1, out);
        }
    }
}

static void rust_collect_impl_links(CBMArena *arena, const char *source, int source_len,
                                    const char *module_qn, const char *relative_module_qn,
                                    const CBMTypeRegistry *reg, TSNode root,
                                    const struct CBMCargoManifest *manifest,
                                    const CBMRustImportSource *imports,
                                    CBMRustImplLinkArray *out) {
    RustLSPContext ctx;
    rust_lsp_init(&ctx, arena, source, source_len, reg, module_qn, NULL);
    ctx.relative_module_qn = relative_module_qn;
    ctx.cargo_manifest = manifest;
    ctx.imports = imports;
    ctx.follow_glob_exports = true;
    /* `mod name;` declarations make `name::Item` a child-module path. */
    uint32_t root_count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < root_count; i++) {
        TSNode item = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(item), "mod_item") != 0 ||
            !ts_node_is_null(ts_node_child_by_field_name(item, "body", 4))) {
            continue;
        }
        char *name = rust_node_text(&ctx, ts_node_child_by_field_name(item, "name", 4));
        if (name) {
            cbm_idxmemo_put_if_absent(&ctx.declared_modules, arena, name, 1);
        }
    }
    /* Source `use` bindings only: graph IMPORTS targets are name matches. */
    rust_collect_uses(&ctx, root);
    ctx.file_glob_count = ctx.glob_count;
    RustFileItems items = {0};
    rust_file_items_collect(&ctx, root, &items, 0);
    rust_collect_impl_links_in(&ctx, root, &items, 0, out);
}

/* Resolve one Rust file against an ALREADY-built (per-file or shared) registry. */
static void rust_resolve_against_registry(
    CBMArena *arena, const char *source, int source_len, const char *module_qn,
    const char *relative_module_qn, const CBMTypeRegistry *reg, const char **import_names,
    const char **import_qns, int import_count, TSNode root, const struct CBMCargoManifest *manifest,
    const CBMRustImportSource *imports, CBMResolvedCallArray *out, CBMCallArray *synthetic_calls,
    CBMRustImplLinkArray *impl_links) {
    if (impl_links) {
        rust_collect_impl_links(arena, source, source_len, module_qn, relative_module_qn, reg, root,
                                manifest, imports, impl_links);
    }
    RustLSPContext ctx;
    rust_lsp_init(&ctx, arena, source, source_len, reg, module_qn, out);
    ctx.relative_module_qn = relative_module_qn;
    ctx.cargo_manifest = manifest;
    ctx.imports = imports;
    ctx.syn_calls = synthetic_calls;
    if (relative_module_qn) {
        for (int i = 0; i < import_count; i++) {
            if (import_names[i] && import_qns[i]) {
                rust_lsp_add_use(&ctx, import_names[i], import_qns[i]);
            }
        }
    }
    /* Source bindings take precedence over heuristic graph import targets. */
    rust_collect_uses(&ctx, root);
    ctx.file_glob_count = ctx.glob_count;
    ctx.file_use_count = ctx.use_count;
    rust_lsp_process_file(&ctx, root);
}

/* Tier-2: build the Rust cross registry ONCE from all project defs, sealed
 * read-only, and shared across every Rust file's resolve (mirrors C/py/cs/ts).
 * Converts the pipeline's CBMLSPDef into CBMRustLSPDef inline (same field copy as
 * pass_lsp_cross.c's pxc_lspdefs_to_rust, including proven trait provenance).
 * module_qn=NULL is byte-identical because all_defs always carry def_module_qn. */
CBMTypeRegistry *cbm_rust_build_cross_registry(CBMArena *arena, CBMLSPDef *defs, int def_count) {
    return cbm_rust_build_cross_registry_with_modtree(arena, defs, def_count, NULL);
}

CBMTypeRegistry *cbm_rust_build_cross_registry_with_modtree(CBMArena *arena, CBMLSPDef *defs,
                                                            int def_count,
                                                            const CBMRustModTree *modtree) {
    if (!arena)
        return NULL;
    CBMTypeRegistry *reg = (CBMTypeRegistry *)cbm_arena_alloc(arena, sizeof(*reg));
    if (!reg)
        return NULL;
    CBMRustLSPDef *rdefs = NULL;
    if (def_count > 0) {
        rdefs = (CBMRustLSPDef *)cbm_arena_alloc(arena, (size_t)def_count * sizeof(CBMRustLSPDef));
        if (!rdefs)
            return NULL;
        for (int i = 0; i < def_count; i++) {
            rdefs[i].qualified_name = defs[i].qualified_name;
            rdefs[i].short_name = defs[i].short_name;
            rdefs[i].label = defs[i].label;
            rdefs[i].receiver_type = defs[i].receiver_type;
            rdefs[i].def_module_qn = defs[i].def_module_qn;
            rdefs[i].return_types = defs[i].return_types;
            rdefs[i].embedded_types = defs[i].embedded_types;
            rdefs[i].field_defs = defs[i].field_defs;
            rdefs[i].method_names_str = defs[i].method_names_str;
            rdefs[i].signature_param_types = defs[i].signature_param_types;
            rdefs[i].signature_param_count = defs[i].signature_param_count;
            rdefs[i].trait_qn = defs[i].trait_qn;
            rdefs[i].receiver_spelling = defs[i].receiver_spelling;
            rdefs[i].decorators = defs[i].decorators;
            rdefs[i].is_interface = defs[i].is_interface;
            rdefs[i].is_rust_impl_relation = defs[i].is_rust_impl_relation;
            rdefs[i].is_abstract = defs[i].is_abstract;
            rdefs[i].is_async = defs[i].is_async;
            rdefs[i].is_rust_private = defs[i].is_rust_private;
            rdefs[i].has_rust_self = defs[i].has_rust_self;
        }
    }
    rust_populate_cross_registry(reg, arena, rdefs, def_count, /*module_qn=*/NULL, modtree);
    reg->read_only = true; /* seal: shared Tier-2 registry is read-only during resolve */
    return reg;
}

/* Cross-file Rust resolve using a pre-built shared registry (Tier-2). Skips the
 * per-file registry build; just parse + resolve. Mirrors cbm_run_c_lsp_cross_with_registry. */
void cbm_run_rust_lsp_cross_with_registry(
    CBMArena *arena, const char *source, int source_len, const char *module_qn,
    const char *relative_module_qn, const CBMTypeRegistry *reg, const char **import_names,
    const char **import_qns, int import_count, TSTree *cached_tree,
    const struct CBMCargoManifest *manifest, const CBMRustImportSource *imports,
    CBMResolvedCallArray *out, CBMCallArray *synthetic_calls,
    CBMRustImplLinkArray *impl_links) {
    if (!source || source_len <= 0 || !out || !reg)
        return;
    TSParser *parser = NULL;
    TSTree *tree = cached_tree;
    bool owns_tree = false;
    if (!tree) {
        parser = ts_parser_new();
        if (!parser)
            return;
        ts_parser_set_language(parser, tree_sitter_rust());
        tree = ts_parser_parse_string(parser, NULL, source, source_len);
        owns_tree = true;
        if (!tree) {
            ts_parser_delete(parser);
            return;
        }
    }
    TSNode root = ts_tree_root_node(tree);
    rust_resolve_against_registry(arena, source, source_len, module_qn, relative_module_qn, reg,
                                  import_names, import_qns, import_count, root, manifest, imports,
                                  out, synthetic_calls, impl_links);
    if (owns_tree) {
        ts_tree_delete(tree);
        if (parser)
            ts_parser_delete(parser);
    }
}

void cbm_run_rust_lsp_cross_with_manifest(
    CBMArena *arena, const char *source, int source_len, const char *module_qn,
    const char *relative_module_qn, CBMRustLSPDef *defs, int def_count, const char **import_names,
    const char **import_qns, int import_count, TSTree *cached_tree,
    const struct CBMCargoManifest *manifest, const CBMRustImportSource *imports,
    CBMResolvedCallArray *out, CBMCallArray *synthetic_calls,
    CBMRustImplLinkArray *impl_links) {
    if (!source || source_len <= 0 || !out)
        return;

    TSParser *parser = NULL;
    TSTree *tree = cached_tree;
    bool owns_tree = false;
    if (!tree) {
        parser = ts_parser_new();
        if (!parser)
            return;
        ts_parser_set_language(parser, tree_sitter_rust());
        tree = ts_parser_parse_string(parser, NULL, source, source_len);
        owns_tree = true;
        if (!tree) {
            ts_parser_delete(parser);
            return;
        }
    }
    TSNode root = ts_tree_root_node(tree);

    /* Build registry from cross-file defs + stdlib (per-file). */
    CBMTypeRegistry reg;
    rust_populate_cross_registry(&reg, arena, defs, def_count, module_qn,
                                 imports ? imports->modtree : NULL);

    rust_resolve_against_registry(arena, source, source_len, module_qn, relative_module_qn, &reg,
                                  import_names, import_qns, import_count, root, manifest, imports,
                                  out, synthetic_calls, impl_links);

    if (owns_tree) {
        ts_tree_delete(tree);
        if (parser)
            ts_parser_delete(parser);
    }
}

/* Manifest-free entry point. Preserves the pre-existing signature used by
 * the unit tests (test_rust_lsp.c) and the batch wrapper — delegates to
 * the manifest-aware variant with a NULL manifest. */
void cbm_run_rust_lsp_cross(CBMArena *arena, const char *source, int source_len,
                            const char *module_qn, CBMRustLSPDef *defs, int def_count,
                            const char **import_names, const char **import_qns, int import_count,
                            TSTree *cached_tree, CBMResolvedCallArray *out) {
    cbm_run_rust_lsp_cross_with_manifest(arena, source, source_len, module_qn, module_qn, defs,
                                         def_count, import_names, import_qns, import_count,
                                         cached_tree, NULL, NULL, out, NULL, NULL);
}

void cbm_batch_rust_lsp_cross(CBMArena *arena, CBMBatchRustLSPFile *files, int file_count,
                              CBMResolvedCallArray *out) {
    if (!files || file_count <= 0 || !out)
        return;

    for (int f = 0; f < file_count; f++) {
        CBMBatchRustLSPFile *file = &files[f];
        memset(&out[f], 0, sizeof(CBMResolvedCallArray));
        if (!file->source || file->source_len <= 0)
            continue;

        CBMArena file_arena;
        cbm_arena_init(&file_arena);

        CBMResolvedCallArray file_out;
        memset(&file_out, 0, sizeof(file_out));

        cbm_run_rust_lsp_cross(&file_arena, file->source, file->source_len, file->module_qn,
                               file->defs, file->def_count, file->import_names, file->import_qns,
                               file->import_count, file->cached_tree, &file_out);

        if (file_out.count > 0) {
            out[f].count = file_out.count;
            out[f].items =
                (CBMResolvedCall *)cbm_arena_alloc(arena, file_out.count * sizeof(CBMResolvedCall));
            for (int j = 0; j < file_out.count; j++) {
                CBMResolvedCall *src = &file_out.items[j];
                CBMResolvedCall *dst = &out[f].items[j];
                memset(dst, 0, sizeof(*dst));
                dst->caller_qn = src->caller_qn ? cbm_arena_strdup(arena, src->caller_qn) : NULL;
                dst->callee_qn = src->callee_qn ? cbm_arena_strdup(arena, src->callee_qn) : NULL;
                dst->caller_impl_key = src->caller_impl_key
                                           ? cbm_arena_strdup(arena, src->caller_impl_key)
                                           : NULL;
                dst->callee_impl_key = src->callee_impl_key
                                           ? cbm_arena_strdup(arena, src->callee_impl_key)
                                           : NULL;
                dst->strategy = src->strategy ? cbm_arena_strdup(arena, src->strategy) : NULL;
                dst->confidence = src->confidence;
                dst->reason = src->reason ? cbm_arena_strdup(arena, src->reason) : NULL;
                dst->kind = src->kind;
                dst->site_start_byte = src->site_start_byte;
                dst->site_end_byte = src->site_end_byte;
            }
        }

        cbm_arena_destroy(&file_arena);
    }
}
