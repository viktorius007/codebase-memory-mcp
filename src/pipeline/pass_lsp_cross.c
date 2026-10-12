/*
 * pass_lsp_cross.c — Cross-file LSP type-aware call resolution pass.
 *
 * See pass_lsp_cross.h for the high-level contract. This file is the
 * pipeline glue that converts the existing per-file extraction state
 * (CBMDefinition / CBMImport / IMPORTS-edge gbuf state) into the input
 * shape each language LSP's cbm_run_X_lsp_cross expects, then merges
 * the resulting CBMResolvedCall entries back into per-file results.
 *
 * The pass is a no-op for any file whose CBMFileResult is missing or
 * whose language has no cross-file LSP entry registered (e.g. Rust /
 * Java today). Per-LSP emit functions dedup against entries already in
 * resolved_calls, so this pass is also idempotent — safe to invoke
 * multiple times if the pipeline gains a re-run path later.
 */
#include "pipeline/pass_lsp_cross.h"

#include "helpers.h"
#include "pipeline/lsp_surface.h"
#include "foundation/mem_core.h"
#include "result_spill.h"
#include "pipeline/pipeline_internal.h"
#include "pipeline/lsp_resolve.h"
#include "callable_sig.h"
#include "lsp/go_lsp.h"
#include "lsp/c_lsp.h"
#include "lsp/py_lsp.h"
#include "lsp/ts_lsp.h"
#include "lsp/php_lsp.h"
#include "lsp/java_lsp.h"
#include "lsp/kotlin_lsp.h"
#include "lsp/rust_lsp.h"
#include "lsp/rust_cargo.h"
#include "graph_buffer/graph_buffer.h"
#include "foundation/constants.h"
#include "foundation/hash_table.h"
#include "foundation/mem_core.h"
#include "foundation/log.h"
#include "foundation/platform.h"
#include "foundation/compat_fs.h"
#include "foundation/compat.h" /* CBM_TLS */
#include "pipeline/worker_pool.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

/* ── Constants ─────────────────────────────────────────────────── */

enum {
    PXC_MAX_FILE_BYTES_FACTOR = 100, /* same cap pass_calls.c uses for source size */
    PXC_ITOA_BUF = 16,
};

extern const TSLanguage *tree_sitter_rust(void);

/* Format an int into a thread-local rotating buffer for log key=value emission.
 * Mirrors the itoa_log helper in pass_calls.c — kept local so passes don't
 * grow a foundation-wide formatting API just for log output. */
static const char *itoa_buf(int val) {
    static _Thread_local char bufs[PXC_ITOA_BUF][PXC_ITOA_BUF];
    static _Thread_local int slot = 0;
    char *out = bufs[slot];
    slot = (slot + 1) & (PXC_ITOA_BUF - 1);
    snprintf(out, PXC_ITOA_BUF, "%d", val);
    return out;
}

/* ── Local helpers ─────────────────────────────────────────────── */

/* True for languages whose module QN is derived from the CONTAINING DIRECTORY
 * (Java package, Go package) rather than the filename stem. MUST match the
 * extraction-side cbm_lang_module_is_dir() in internal/cbm/helpers.c so the
 * cross-file LSP caller_qn agrees with the def-node QN (the lsp_resolve join
 * keys on exact equality). */
static bool pxc_module_is_dir(CBMLanguage lang) {
    return lang == CBM_LANG_JAVA || lang == CBM_LANG_GO;
}

/* Slurp a file into a malloc'd, NUL-terminated buffer. Mirrors the
 * read_file helper in pass_calls.c / pass_parallel.c (kept local so the
 * pipeline doesn't grow a public read-file API just for this pass). */
static char *pxc_read_file(const char *path, int *out_len) {
    FILE *f = cbm_fopen(path, "rb");
    if (!f)
        return NULL;
    (void)fseek(f, 0, SEEK_END);
    long size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (long)PXC_MAX_FILE_BYTES_FACTOR * (long)CBM_SZ_1K * (long)CBM_SZ_1K) {
        (void)fclose(f);
        return NULL;
    }
    /* +pad: tree-sitter lexer lookahead reads past EOF; keep it in-bounds */
    enum { CBM_TS_LOOKAHEAD_PAD = 16 };
    char *buf = (char *)cbm_alloc(CBM_MEM_CLASS_EXTRACT, (size_t)size + CBM_TS_LOOKAHEAD_PAD);
    if (!buf) {
        (void)fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)size, f);
    (void)fclose(f);
    if (nread > (size_t)size)
        nread = (size_t)size;
    memset(buf + nread, 0, CBM_TS_LOOKAHEAD_PAD);
    *out_len = (int)nread;
    return buf;
}

/* Canonical path of a Rust file in scope → the first scope index carrying it.
 * The record owns its key so one free per value releases the table. */
typedef struct {
    int index;
    char key[];
} PXCRustCanonicalEntry;

static void pxc_rust_canonical_entry_free(const char *key, void *value, void *userdata) {
    (void)key;
    (void)userdata;
    cbm_free(CBM_MEM_CLASS_OTHER, value);
}

static void pxc_rust_canonical_index_free(CBMHashTable *index) {
    if (index) {
        cbm_ht_foreach(index, pxc_rust_canonical_entry_free, NULL);
        cbm_ht_free(index);
    }
}

/* NULL on allocation failure. One canonicalization per file instead of one per
 * (declaration, file) pair; first index wins, as a linear scan would. */
static CBMHashTable *pxc_rust_canonical_index(const cbm_file_info_t *files, int file_count) {
    CBMHashTable *index = cbm_ht_create((uint32_t)file_count * 2U + CBM_SZ_64);
    if (!index) {
        return NULL;
    }
    for (int i = 0; i < file_count; i++) {
        char canonical[CBM_SZ_4K];
        if (files[i].language != CBM_LANG_RUST || !files[i].path ||
            !cbm_canonical_path(files[i].path, canonical, sizeof(canonical)) ||
            cbm_ht_get(index, canonical)) {
            continue;
        }
        size_t length = strlen(canonical) + 1;
        PXCRustCanonicalEntry *entry = (PXCRustCanonicalEntry *)cbm_alloc(
            CBM_MEM_CLASS_OTHER, sizeof(PXCRustCanonicalEntry) + length);
        if (!entry) {
            pxc_rust_canonical_index_free(index);
            return NULL;
        }
        entry->index = i;
        memcpy(entry->key, canonical, length);
        cbm_ht_set(index, entry->key, entry);
    }
    return index;
}

static int pxc_rust_path_target_index(const CBMHashTable *index, const char *parent_path,
                                      const char *path) {
    char joined[CBM_SZ_4K];
    const char *slash = strrchr(parent_path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(parent_path, '\\');
    if (!slash || (backslash && backslash > slash)) {
        slash = backslash;
    }
#endif
    bool absolute = path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
    int written = absolute ? snprintf(joined, sizeof(joined), "%s", path)
                           : snprintf(joined, sizeof(joined), "%.*s/%s",
                                      slash ? (int)(slash - parent_path) : 0, parent_path, path);
    char target[CBM_SZ_4K];
    if (written < 0 || written >= (int)sizeof(joined) ||
        !cbm_canonical_path(joined, target, sizeof(target))) {
        return -1;
    }
    const PXCRustCanonicalEntry *entry = (const PXCRustCanonicalEntry *)cbm_ht_get(index, target);
    return entry ? entry->index : -1;
}

static char *pxc_rust_declared_module(const char *parent_module, const char *name) {
    if (!parent_module || !parent_module[0] || !name || !name[0]) {
        return NULL;
    }
    size_t parent_length = strlen(parent_module);
    const char *last = strrchr(parent_module, '.');
    if (last && (strcmp(last + 1, "mod") == 0 || strcmp(last + 1, "lib") == 0 ||
                 strcmp(last + 1, "main") == 0)) {
        parent_length = (size_t)(last - parent_module);
    }
    size_t length = parent_length + 1 + strlen(name) + 1;
    char *module = (char *)cbm_alloc(CBM_MEM_CLASS_OTHER, length);
    if (module) {
        snprintf(module, length, "%.*s.%s", (int)parent_length, parent_module, name);
    }
    return module;
}

typedef struct {
    int parent_index;
    int target_index;
    char *name;
} PXCRustPathModule;

typedef struct {
    PXCRustPathModule *items;
    int count;
    int capacity;
} PXCRustPathModules;

static bool pxc_rust_path_modules_push(PXCRustPathModules *modules, int parent_index,
                                       int target_index, char *name) {
    if (modules->count >= modules->capacity) {
        int capacity = modules->capacity ? modules->capacity * 2 : 16;
        PXCRustPathModule *items = (PXCRustPathModule *)cbm_realloc(
            CBM_MEM_CLASS_OTHER, modules->items, (size_t)capacity * sizeof(*items));
        if (!items) {
            return false;
        }
        modules->items = items;
        modules->capacity = capacity;
    }
    modules->items[modules->count++] = (PXCRustPathModule){
        .parent_index = parent_index, .target_index = target_index, .name = name};
    return true;
}

/* Record one Rust file's `#[path]` module facts (strings in `arena`). Reads
 * and parses the file when no cached result carries its source or tree.
 * Returns false on allocation failure; an unreadable file has empty facts. */
static bool pxc_rust_file_path_facts(CBMArena *arena, const CBMFileResult *cached,
                                     const char *abs_path, CBMRustPathFacts *out) {
    memset(out, 0, sizeof(*out));
    int source_len = 0;
    char *owned_source = NULL;
    const char *source = cached ? cached->source : NULL;
    if (source) {
        source_len = cached->source_len;
    } else if (abs_path) {
        owned_source = pxc_read_file(abs_path, &source_len);
        source = owned_source;
    }
    TSTree *owned_tree = NULL;
    TSTree *tree = cached ? cached->cached_tree : NULL;
    TSParser *parser = NULL;
    if (source && source_len > 0 && !tree) {
        parser = ts_parser_new();
        if (parser && ts_parser_set_language(parser, tree_sitter_rust())) {
            owned_tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
            tree = owned_tree;
        }
    }
    bool complete = true;
    CBMRustPathMod *mods = NULL;
    int capacity = 0;
    if (source && tree) {
        TSNode root = ts_tree_root_node(tree);
        uint32_t count = ts_node_named_child_count(root);
        for (uint32_t child = 0; complete && child < count; child++) {
            TSNode item = ts_node_named_child(root, child);
            if (strcmp(ts_node_type(item), "mod_item") != 0) {
                continue;
            }
            if (!ts_node_is_null(ts_node_child_by_field_name(item, "body", 4))) {
                out->inline_scope = true;
                continue;
            }
            char *path = cbm_rust_path_attribute(arena, source, item);
            TSNode name_node = ts_node_child_by_field_name(item, "name", 4);
            char *name = path && !ts_node_is_null(name_node)
                             ? cbm_node_text(arena, name_node, source)
                             : NULL;
            if (!name) {
                continue;
            }
            if (out->count >= capacity) {
                capacity = capacity ? capacity * 2 : 4;
                CBMRustPathMod *grown = (CBMRustPathMod *)cbm_arena_alloc(
                    arena, (size_t)capacity * sizeof(CBMRustPathMod));
                if (!grown) {
                    complete = false;
                    break;
                }
                if (out->count > 0) {
                    memcpy(grown, mods, (size_t)out->count * sizeof(CBMRustPathMod));
                }
                mods = grown;
            }
            mods[out->count++] = (CBMRustPathMod){.name = name, .path = path};
        }
    }
    out->mods = mods;
    out->known = complete;
    /* Another file's expansion reads this file's macro_rules! items, so they
     * belong to the facts whose change re-resolves the whole crate. */
    out->tree_digest =
        source && tree
            ? cbm_rust_modtree_file_digest(source, ts_tree_root_node(tree)) ^
                  cbm_rust_macro_facts_digest(source, source_len, ts_tree_root_node(tree))
            : 0;
    if (owned_tree) {
        ts_tree_delete(owned_tree);
    }
    if (parser) {
        ts_parser_delete(parser);
    }
    cbm_free(CBM_MEM_CLASS_EXTRACT, owned_source);
    return complete;
}

static void pxc_merge_rust_path_module(char **declared, bool *resolved, int target,
                                       char *candidate) {
    if (!resolved[target]) {
        declared[target] = candidate;
        resolved[target] = true;
    } else if (declared[target][0] && strcmp(declared[target], candidate) != 0) {
        cbm_free(CBM_MEM_CLASS_OTHER, declared[target]);
        candidate[0] = '\0';
        declared[target] = candidate;
    } else {
        cbm_free(CBM_MEM_CLASS_OTHER, candidate);
    }
}

static bool pxc_resolve_rust_path_modules(const PXCRustPathModules *modules,
                                          char *const *def_modules, int file_count,
                                          const bool *mounted, const bool *has_inline_scope,
                                          char ***out_declared, bool **out_resolved) {
    char **declared = (char **)cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)file_count * sizeof(char *));
    char **next = (char **)cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)file_count * sizeof(char *));
    bool *resolved = (bool *)cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)file_count * sizeof(bool));
    bool *next_resolved =
        (bool *)cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)file_count * sizeof(bool));
    if (!declared || !next || !resolved || !next_resolved) {
        cbm_free(CBM_MEM_CLASS_OTHER, declared);
        cbm_free(CBM_MEM_CLASS_OTHER, next);
        cbm_free(CBM_MEM_CLASS_OTHER, resolved);
        cbm_free(CBM_MEM_CLASS_OTHER, next_resolved);
        return false;
    }
    for (int depth = 0; depth <= file_count; depth++) {
        for (int i = 0; i < modules->count; i++) {
            const PXCRustPathModule *module = &modules->items[i];
            const char *parent = def_modules[module->parent_index];
            if (mounted[module->parent_index]) {
                if (!resolved[module->parent_index] || has_inline_scope[module->parent_index]) {
                    continue;
                }
                parent = declared[module->parent_index];
            }
            char *candidate = pxc_rust_declared_module(parent, module->name);
            if (candidate) {
                pxc_merge_rust_path_module(next, next_resolved, module->target_index, candidate);
            }
        }
        bool unchanged = true;
        for (int i = 0; i < file_count; i++) {
            if (resolved[i] != next_resolved[i] ||
                (resolved[i] && strcmp(declared[i], next[i]) != 0)) {
                unchanged = false;
            }
        }
        if (unchanged) {
            for (int i = 0; i < file_count; i++) {
                cbm_free(CBM_MEM_CLASS_OTHER, next[i]);
            }
            break;
        }
        for (int i = 0; i < file_count; i++) {
            cbm_free(CBM_MEM_CLASS_OTHER, declared[i]);
            declared[i] = NULL;
            resolved[i] = false;
        }
        char **swap_declared = declared;
        declared = next;
        next = swap_declared;
        bool *swap_resolved = resolved;
        resolved = next_resolved;
        next_resolved = swap_resolved;
    }
    cbm_free(CBM_MEM_CLASS_OTHER, next);
    cbm_free(CBM_MEM_CLASS_OTHER, next_resolved);
    *out_declared = declared;
    *out_resolved = resolved;
    return true;
}

/* Scratch for one identity computation over the scope (the project). Index j
 * is a scope index; subset_of[j] is the resolved-subset index or -1. */
typedef struct {
    const cbm_file_info_t *scope;
    int count;
    int *subset_of;
    CBMRustPathFacts *facts;
    char **modules;     /* per-scope module QN; borrowed for subset files */
    bool *owned_module; /* modules[j] is heap-owned here */
    bool *mounted;
    bool *has_inline_scope;
} PXCRustPathScope;

static void pxc_rust_path_scope_free(PXCRustPathScope *s) {
    for (int j = 0; s->modules && s->owned_module && j < s->count; j++) {
        if (s->owned_module[j]) {
            free(s->modules[j]);
        }
    }
    cbm_free(CBM_MEM_CLASS_OTHER, s->subset_of);
    cbm_free(CBM_MEM_CLASS_OTHER, s->facts);
    cbm_free(CBM_MEM_CLASS_OTHER, s->modules);
    cbm_free(CBM_MEM_CLASS_OTHER, s->owned_module);
    cbm_free(CBM_MEM_CLASS_OTHER, s->mounted);
    cbm_free(CBM_MEM_CLASS_OTHER, s->has_inline_scope);
}

/* Map scope ↔ subset, and gather every scope file's facts: fresh parses for
 * the subset (recorded on their results for the surface), persisted facts for
 * the rest. False when any Rust file's facts are unavailable — the caller then
 * fails every identity closed rather than guess a parent. */
static bool pxc_rust_path_scope_load(const cbm_pipeline_ctx_t *ctx, CBMArena *arena,
                                     CBMFileResult **cache, const cbm_file_info_t *files,
                                     int file_count, const char *project_name,
                                     char *const *def_modules, PXCRustPathScope *s) {
    bool project_scope = ctx && ctx->project_files;
    s->scope = project_scope ? ctx->project_files : files;
    s->count = project_scope ? ctx->project_file_count : file_count;
    size_t n = (size_t)(s->count > 0 ? s->count : 1);
    s->subset_of = (int *)cbm_alloc(CBM_MEM_CLASS_OTHER, n * sizeof(int));
    s->facts = (CBMRustPathFacts *)cbm_calloc(CBM_MEM_CLASS_OTHER, n * sizeof(CBMRustPathFacts));
    s->modules = (char **)cbm_calloc(CBM_MEM_CLASS_OTHER, n * sizeof(char *));
    s->owned_module = (bool *)cbm_calloc(CBM_MEM_CLASS_OTHER, n * sizeof(bool));
    s->mounted = (bool *)cbm_calloc(CBM_MEM_CLASS_OTHER, n * sizeof(bool));
    s->has_inline_scope = (bool *)cbm_calloc(CBM_MEM_CLASS_OTHER, n * sizeof(bool));
    if (!s->subset_of || !s->facts || !s->modules || !s->owned_module || !s->mounted ||
        !s->has_inline_scope) {
        return false;
    }
    for (int j = 0; j < s->count; j++) {
        s->subset_of[j] = project_scope ? -1 : j;
    }
    if (project_scope) {
        CBMHashTable *by_path = cbm_ht_create((uint32_t)s->count * 2U + CBM_SZ_64);
        if (!by_path) {
            return false;
        }
        for (int j = 0; j < s->count; j++) {
            cbm_ht_set(by_path, s->scope[j].rel_path, &s->subset_of[j]);
        }
        bool mapped = true;
        for (int i = 0; i < file_count && mapped; i++) {
            int *slot = (int *)cbm_ht_get(by_path, files[i].rel_path);
            if (slot) {
                *slot = i;
            } else {
                mapped = false; /* a resolved file outside discovery */
            }
        }
        cbm_ht_free(by_path);
        if (!mapped) {
            return false;
        }
    }
    for (int j = 0; j < s->count; j++) {
        if (s->scope[j].language != CBM_LANG_RUST) {
            continue;
        }
        int i = s->subset_of[j];
        if (i >= 0) {
            CBMArena *owner = cache[i] ? &cache[i]->arena : arena;
            if (!pxc_rust_file_path_facts(owner, cache[i], files[i].path, &s->facts[j])) {
                return false;
            }
            if (cache[i]) {
                cache[i]->rust_path_facts = s->facts[j];
            }
            s->modules[j] = def_modules[i];
        } else {
            const CBMRustPathFacts *stored =
                ctx->rust_path_facts ? (const CBMRustPathFacts *)cbm_ht_get(ctx->rust_path_facts,
                                                                            s->scope[j].rel_path)
                                     : NULL;
            if (!stored || !stored->known) {
                return false;
            }
            s->facts[j] = *stored;
            s->modules[j] = cbm_pipeline_fqn_module_dir(project_name, s->scope[j].rel_path,
                                                        pxc_module_is_dir(s->scope[j].language));
            if (!s->modules[j]) {
                return false;
            }
            s->owned_module[j] = true;
        }
        s->has_inline_scope[j] = s->facts[j].inline_scope;
    }
    return true;
}

/* Declared module identity of every resolved Rust file, computed over the
 * whole project (ctx->project_files) when the resolved files are a subset of
 * it, so a re-parsed `#[path]` target whose declaring parent is not
 * re-parsed still receives the identity a full build gives it. */
static void pxc_apply_rust_path_modules(const cbm_pipeline_ctx_t *ctx, CBMArena *arena,
                                        CBMFileResult **cache, const cbm_file_info_t *files,
                                        int file_count, const char *project_name,
                                        char *const *def_modules) {
    for (int i = 0; i < file_count; i++) {
        if (files[i].language == CBM_LANG_RUST && cache[i]) {
            cache[i]->rust_relative_module_qn = "";
        }
    }
    PXCRustPathScope s = {0};
    PXCRustPathModules modules = {0};
    CBMHashTable *canonical = NULL;
    bool ok = pxc_rust_path_scope_load(ctx, arena, cache, files, file_count, project_name,
                                       def_modules, &s);
    bool any_declaration = false;
    for (int j = 0; ok && j < s.count; j++) {
        any_declaration = any_declaration || s.facts[j].count > 0;
    }
    if (ok && any_declaration) {
        canonical = pxc_rust_canonical_index(s.scope, s.count);
        ok = canonical != NULL;
    }
    for (int j = 0; ok && any_declaration && j < s.count; j++) {
        for (int m = 0; ok && m < s.facts[j].count; m++) {
            const CBMRustPathMod *mod = &s.facts[j].mods[m];
            int target = pxc_rust_path_target_index(canonical, s.scope[j].path, mod->path);
            if (target >= 0) {
                ok = pxc_rust_path_modules_push(&modules, j, target, (char *)mod->name);
            }
        }
    }
    pxc_rust_canonical_index_free(canonical);
    if (!ok) {
        cbm_free(CBM_MEM_CLASS_OTHER, modules.items);
        pxc_rust_path_scope_free(&s);
        return;
    }
    for (int i = 0; i < file_count; i++) {
        if (files[i].language == CBM_LANG_RUST && cache[i]) {
            cache[i]->rust_relative_module_qn = NULL;
        }
    }
    for (int i = 0; i < modules.count; i++) {
        s.mounted[modules.items[i].target_index] = true;
    }
    char **declared = NULL;
    bool *resolved = NULL;
    bool resolution_ok = pxc_resolve_rust_path_modules(&modules, s.modules, s.count, s.mounted,
                                                       s.has_inline_scope, &declared, &resolved);
    for (int j = 0; j < s.count; j++) {
        int i = s.subset_of[j];
        if (i >= 0 && s.mounted[j] && cache[i]) {
            const char *module =
                resolution_ok && resolved[j] && declared[j][0] && !s.has_inline_scope[j]
                    ? cbm_arena_strdup(&cache[i]->arena, declared[j])
                    : "";
            cache[i]->rust_relative_module_qn = module ? module : "";
        }
        if (resolution_ok) {
            cbm_free(CBM_MEM_CLASS_OTHER, declared[j]);
        }
    }
    cbm_free(CBM_MEM_CLASS_OTHER, modules.items);
    cbm_free(CBM_MEM_CLASS_OTHER, declared);
    cbm_free(CBM_MEM_CLASS_OTHER, resolved);
    pxc_rust_path_scope_free(&s);
}

/* Map a CBMDefinition.label to a CBMLSPDef.label. Per-language LSP registrars
 * only care about type-like containers (Class/Struct/Interface/Trait/Enum/Type)
 * plus Protocol/Function/Method — variables, modules, decorators, etc. are
 * skipped. Struct passes through so Rust/Go struct type-registration via the
 * cross-file LSP path is not dropped. */
/* The label a cross def keeps is a static spelling, never the result's own
 * pointer: in spill mode the CBMFileResult is freed (parked on disk) the
 * moment the collector is done with it, and the defs outlive it. A kept
 * spelling outside the table falls back to an arena copy. */
static const char *pxc_map_label(CBMArena *arena, const char *label) {
    if (!label)
        return NULL;
    if (cbm_label_is_type_like(label) || strcmp(label, "Protocol") == 0 ||
        strcmp(label, "Function") == 0 || strcmp(label, "Method") == 0 ||
        /* Properties reach the cross defs so the Kotlin registrar can attach
         * them as fields of their receiver type (kotlin_lookup_property_type
         * was blind across files, leaving property references to the
         * name-only fallback). Every registrar filters by explicit label, so
         * the other languages ignore Variable defs untouched. */
        strcmp(label, "Variable") == 0 || strcmp(label, "AssociatedConst") == 0) {
        static const char *const canon[] = {"Class", "Struct",   "Interface", "Enum",   "Type",
                                            "Trait", "Protocol", "Function",  "Method", "Variable"};
        for (size_t i = 0; i < sizeof(canon) / sizeof(canon[0]); i++) {
            if (strcmp(label, canon[i]) == 0) {
                return canon[i];
            }
        }
        return arena ? cbm_arena_strdup(arena, label) : NULL;
    }
    return NULL;
}

/* Build the embedded_types "|"-separated string from base_classes[].
 * Returns NULL when there are no bases. Allocated in the supplied arena. */
static const char *pxc_join_pipe(CBMArena *arena, const char *const *items) {
    if (!items || !items[0])
        return NULL;
    int count = 0;
    size_t total = 0;
    for (int i = 0; items[i]; i++) {
        count++;
        total += strlen(items[i]);
    }
    if (count == 0)
        return NULL;
    /* count - 1 separators + NUL. */
    size_t bufsz = total + (size_t)(count - 1) + 1;
    char *buf = (char *)cbm_arena_alloc(arena, bufsz);
    if (!buf)
        return NULL;
    char *p = buf;
    for (int i = 0; i < count; i++) {
        size_t n = strlen(items[i]);
        memcpy(p, items[i], n);
        p += n;
        if (i + 1 < count)
            *p++ = '|';
    }
    *p = '\0';
    return buf;
}

/* ── Cross-file base-class QN resolution ──────────────────────────
 *
 * CBMDefinition.base_classes carries the SOURCE SPELLING of each base
 * ("Base", "django.db.Model", "React.Component"): extraction strips
 * keywords and generic arguments, but it cannot know WHERE the name is
 * declared. The Python and TS cross-file registrars, however, consume
 * CBMLSPDef.embedded_types as fully-qualified names — py_lookup_attribute
 * and ts_lookup_member feed each entry straight into
 * cbm_registry_lookup_type. An unqualified spelling therefore matched
 * nothing declared in ANOTHER file: `class Child(Base)` in child.py never
 * saw Base in base.py, so a call to an inherited method through a typed
 * receiver had no member to find and fell through to the weak textual
 * cascade (where the #592/#606 guard correctly kills it).
 *
 * Resolve each base name ONCE per definition here, from exactly the
 * inputs pass_semantic uses to draw its INHERITS edge: the project
 * registry, the declaring module, and the file's import map. Two
 * properties follow. The LSP's inheritance view is the same relation the
 * graph records, so the two cannot diverge. And the binding is
 * import- or same-module-backed, not a short-name guess, so the CALLS
 * edge it enables is a supported fact that the weak-member guard keeps.
 *
 * Cost: O(defs) hash lookups + ONE import map per file. No per-call-site
 * hierarchy walk, no registry scan, no per-file registry rebuild.
 */
static bool pxc_lang_resolves_base_qns(CBMLanguage lang) {
    switch (lang) {
    case CBM_LANG_PYTHON:
    case CBM_LANG_JAVASCRIPT:
    case CBM_LANG_TYPESCRIPT:
    case CBM_LANG_TSX:
        return true;
    default:
        /* Go / JVM / C# / C++ / Rust registrars qualify their own embedded
         * types already (struct embedding, parent_class chains, impl-trait
         * provenance). Re-resolving here would fight those paths, so they
         * keep the raw join. */
        return false;
    }
}

/* True for the registry strategies that are pure short-name guesses.
 * EXPLICIT drop-list, mirroring cbm_tsjs_suppress_weak_method_match: a
 * base class bound by "some project type happens to share this name" is
 * exactly the fabricated relation #606 removed, and inheritance
 * multiplies it — every inherited member of the wrong base would become
 * a callable target. Import-, module- and suffix-aware strategies are
 * kept; anything unresolved simply retains its source spelling and the
 * behaviour that predates this resolution. */
static bool pxc_base_strategy_is_weak(const char *strategy) {
    if (!strategy || !strategy[0]) {
        return true;
    }
    return strcmp(strategy, "suffix_match") == 0 || strcmp(strategy, "unique_name") == 0 ||
           strcmp(strategy, "field_type_hint") == 0 || strcmp(strategy, "fuzzy") == 0;
}

/* One file's base-resolution scope: the registry + import map pass_semantic
 * uses, plus the facts the external-base gate reads. NULL (the surface-probe
 * path) keeps every base at its raw spelling. */
typedef struct {
    const cbm_registry_t *reg;
    const char **imp_keys;
    const char **imp_vals;
    int imp_count;
    const CBMImportArray *imports;
    const cbm_gbuf_t *gbuf;
    const char *project_name;
    const char *rel;
    CBMLanguage lang;
} pxc_base_scope_t;

/* Resolve one base-class spelling to a project QN. Mirrors
 * pass_semantic.c::resolve_as_class — same registry, same type-like veto —
 * then additionally rejects weak short-name strategies (see above).
 * Returns NULL when the base is not a confidently-known project type;
 * stdlib and third-party bases land here and keep their raw spelling. */
static const char *pxc_resolve_base_qn(const pxc_base_scope_t *bs, const char *raw,
                                       const char *module_qn) {
    if (!bs || !bs->reg || !raw || !raw[0]) {
        return NULL;
    }
    cbm_resolution_t res =
        cbm_registry_resolve(bs->reg, raw, module_qn, bs->imp_keys, bs->imp_vals, bs->imp_count);
    if (!res.qualified_name || !res.qualified_name[0]) {
        return NULL;
    }
    if (pxc_base_strategy_is_weak(res.strategy)) {
        return NULL;
    }
    if (!cbm_label_is_type_like(cbm_registry_label_of(bs->reg, res.qualified_name))) {
        return NULL;
    }
    /* `unittest.TestCase` under an external `import unittest` survives the
     * weak-strategy veto through the same-module suffix fallback; the shared
     * external-base gate keeps it at its source spelling. */
    if (cbm_python_external_base_contradicts(bs->lang, bs->imports, raw, res.qualified_name,
                                             bs->gbuf, bs->project_name, bs->rel)) {
        return NULL;
    }
    return res.qualified_name;
}

/* pxc_join_pipe over base_classes, substituting each resolved QN for its
 * source spelling. Unresolved entries pass through verbatim so a base the
 * registry does not know keeps working exactly as before. */
static const char *pxc_join_base_qns(CBMArena *arena, const char *const *bases,
                                     const pxc_base_scope_t *bs, const char *module_qn) {
    if (!bases || !bases[0]) {
        return NULL;
    }
    int count = 0;
    while (bases[count]) {
        count++;
    }
    const char **resolved =
        (const char **)cbm_arena_alloc(arena, (size_t)(count + 1) * sizeof(const char *));
    if (!resolved) {
        return pxc_join_pipe(arena, bases);
    }
    for (int i = 0; i < count; i++) {
        const char *qn = pxc_resolve_base_qn(bs, bases[i], module_qn);
        resolved[i] = qn ? qn : bases[i];
    }
    resolved[count] = NULL;
    return pxc_join_pipe(arena, resolved);
}

static bool pxc_is_jvm_lang(CBMLanguage lang);

/* Leaf of the BASE QN: a callable identity suffix (#2061) never contains
 * '.', so this equals the historical strrchr split for every QN. */
static const char *pxc_last_component(const char *qn) {
    if (!qn) {
        return NULL;
    }
    const char *leaf = qn;
    size_t base_len = cbm_qn_callable_base_len(qn);
    for (size_t i = 0; i < base_len; i++) {
        if (qn[i] == '.') {
            leaf = qn + i + 1;
        }
    }
    return leaf;
}

/* Every return is arena-owned: the fallback spelling is a copy, never the
 * result's pointer (the result may be parked on disk before the def is read). */
static const char *pxc_jvm_type_qn(CBMArena *arena, const char *namespace_name,
                                   const char *type_qn_or_name) {
    if (!arena || !type_qn_or_name) {
        return NULL;
    }
    const char *short_name =
        (namespace_name && namespace_name[0]) ? pxc_last_component(type_qn_or_name) : NULL;
    if (!short_name || !short_name[0]) {
        return cbm_arena_strdup(arena, type_qn_or_name);
    }
    return cbm_arena_sprintf(arena, "%s.%s", namespace_name, short_name);
}

static const char *pxc_jvm_def_qn(CBMArena *arena, const CBMDefinition *src,
                                  const char *namespace_name, const char *label) {
    if (!arena || !src) {
        return NULL;
    }
    if (!namespace_name || !namespace_name[0]) {
        return cbm_arena_strdup(arena, src->qualified_name); /* a copy, see above */
    }
    if (strcmp(label, "Method") == 0 || strcmp(label, "Function") == 0 ||
        strcmp(label, "Constructor") == 0) {
        if (src->parent_class && src->parent_class[0]) {
            return cbm_arena_sprintf(arena, "%s.%s.%s", namespace_name,
                                     pxc_last_component(src->parent_class), src->name);
        }
        return cbm_arena_sprintf(arena, "%s.%s", namespace_name, src->name);
    }
    return cbm_arena_sprintf(arena, "%s.%s", namespace_name, src->name);
}

static const char *pxc_infer_jvm_namespace(CBMArena *arena, const char *rel_path,
                                           CBMLanguage lang) {
    if (!arena || !rel_path || !pxc_is_jvm_lang(lang)) {
        return NULL;
    }
    const char *root = NULL;
    const char *lang_root = lang == CBM_LANG_KOTLIN ? "kotlin/" : "java/";
    if (strncmp(rel_path, "src/main/", 9) == 0 &&
        strncmp(rel_path + 9, lang_root, strlen(lang_root)) == 0) {
        root = rel_path + 9 + strlen(lang_root);
    } else if (strncmp(rel_path, "src/test/", 9) == 0 &&
               strncmp(rel_path + 9, lang_root, strlen(lang_root)) == 0) {
        root = rel_path + 9 + strlen(lang_root);
    } else {
        const char *needle = lang == CBM_LANG_KOTLIN ? "/kotlin/" : "/java/";
        root = strstr(rel_path, needle);
        if (root) {
            root += strlen(needle);
        } else if (strncmp(rel_path, "src/", 4) == 0) {
            root = rel_path + 4;
        } else {
            root = strstr(rel_path, "/src/");
            if (root) {
                root += strlen("/src/");
            }
        }
    }
    if (!root || !root[0]) {
        return NULL;
    }
    if (strncmp(root, "main/", 5) == 0 || strncmp(root, "test/", 5) == 0) {
        root += 5;
    }
    if (strncmp(root, "java/", 5) == 0) {
        root += 5;
    } else if (strncmp(root, "kotlin/", 7) == 0) {
        root += 7;
    }
    const char *slash = strrchr(root, '/');
    if (!slash || slash <= root) {
        return NULL;
    }
    size_t len = (size_t)(slash - root);
    char *ns = (char *)cbm_arena_alloc(arena, len + 1);
    if (!ns) {
        return NULL;
    }
    memcpy(ns, root, len);
    ns[len] = '\0';
    for (size_t i = 0; i < len; i++) {
        if (ns[i] == '/') {
            ns[i] = '.';
        }
    }
    return ns;
}

static const char *pxc_qn_leaf(const char *name) {
    if (!name) {
        return NULL;
    }
    const char *leaf = name;
    /* Base only: a callable identity suffix (#2061) may carry ':' labels. */
    const char *end = name + cbm_qn_callable_base_len(name);
    for (const char *p = name; p < end; p++) {
        if (*p == '.' || *p == ':' || *p == '/' || *p == '\\') {
            leaf = p + 1;
        }
    }
    return leaf;
}

/* Convert one CBMDefinition into a CBMLSPDef. Returns 0 on success, -1
 * to skip (unsupported label or missing required field). dst gets borrowed
 * pointers into src and into `arena` for synthesised composites. */
static int pxc_build_lsp_def(CBMArena *arena, const CBMDefinition *src, const char *module_qn,
                             const char *namespace_name, CBMLanguage lang, CBMLSPDef *dst,
                             const pxc_base_scope_t *bs) {
    const char *label = pxc_map_label(arena, src->label);
    if (!label || !src->qualified_name || !src->name)
        return -1;
    memset(dst, 0, sizeof(*dst));
    if (pxc_is_jvm_lang(lang) && namespace_name && namespace_name[0]) {
        dst->qualified_name = pxc_jvm_def_qn(arena, src, namespace_name, label);
        dst->receiver_type = pxc_jvm_type_qn(arena, namespace_name, src->parent_class);
    } else {
        dst->qualified_name = cbm_arena_strdup(arena, src->qualified_name);
        dst->receiver_type = src->parent_class ? cbm_arena_strdup(arena, src->parent_class) : NULL;
    }
    /* Copies, not borrows: a result may be parked on disk (spill mode) the
     * moment the collector is done with it; the def list outlives it. */
    dst->short_name = cbm_arena_strdup(arena, src->name);
    dst->label = label;
    dst->def_module_qn = module_qn;
    dst->namespace_name = namespace_name ? cbm_arena_strdup(arena, namespace_name) : NULL;
    /* C# types carry their OWN declared namespace: the file-level one is only
     * the file's first namespace, wrong for every later namespace (#2120). */
    if (lang == CBM_LANG_CSHARP && cbm_label_is_type_like(src->label)) {
        dst->namespace_name =
            src->decl_namespace ? cbm_arena_strdup(arena, src->decl_namespace) : NULL;
    }
    dst->is_interface = (strcmp(label, "Interface") == 0 || strcmp(label, "Protocol") == 0);
    /* Single return-type string. The per-language registrars split on '|'
     * for multi-return languages (Go); single-return languages just see one
     * piece, which is what's already stored. */
    dst->return_types = src->return_type ? cbm_arena_strdup(arena, src->return_type) : NULL;
    /* Languages whose cross registrars read embedded_types as QNs get their
     * bases resolved against the project registry; everyone else keeps the
     * raw source spelling their own registrar already knows how to handle. */
    dst->embedded_types = (bs && bs->reg && pxc_lang_resolves_base_qns(lang))
                              ? pxc_join_base_qns(arena, src->base_classes, bs, module_qn)
                              : pxc_join_pipe(arena, src->base_classes);
    /* A Rust trait's supertrait bounds, as written: the Rust registrar
     * resolves them in the trait's module. */
    if (lang == CBM_LANG_RUST && src->supertraits) {
        dst->embedded_types = cbm_arena_strdup(arena, src->supertraits);
    }
    dst->signature_param_types = NULL;
    dst->signature_param_count = 0;
    if (src->signature_param_types && src->signature_param_count > 0) {
        const char **types = (const char **)cbm_arena_alloc(
            arena, (size_t)src->signature_param_count * sizeof(const char *));
        if (types) {
            for (int i = 0; i < src->signature_param_count; i++) {
                types[i] = src->signature_param_types[i]
                               ? cbm_arena_strdup(arena, src->signature_param_types[i])
                               : NULL;
            }
            dst->signature_param_types = types;
            dst->signature_param_count = src->signature_param_count;
        }
    }
    dst->lang = lang;
    dst->decorators = NULL;
    if (src->decorators) {
        int n = 0;
        while (src->decorators[n]) {
            n++;
        }
        const char **decos =
            (const char **)cbm_arena_alloc(arena, (size_t)(n + 1) * sizeof(const char *));
        if (decos) {
            for (int i = 0; i < n; i++) {
                decos[i] = cbm_arena_strdup(arena, src->decorators[i]);
            }
            decos[n] = NULL;
            dst->decorators = decos;
        }
    }
    if (lang == CBM_LANG_RUST) {
        /* Exact impl-block provenance is captured while the Rust impl node is
         * still on hand.  Do not reconstruct it later from leaf names. */
        dst->trait_qn = src->impl_trait ? cbm_arena_strdup(arena, src->impl_trait) : NULL;
        dst->receiver_spelling = src->receiver ? cbm_arena_strdup(arena, src->receiver) : NULL;
        dst->is_abstract = src->is_abstract;
        dst->is_async = src->is_async;
        dst->is_rust_private = src->is_rust_private;
        dst->has_rust_self = src->has_rust_self;
        dst->has_rust_value_self = src->has_rust_value_self;
        dst->has_rust_shared_self = src->has_rust_shared_self;
        dst->rust_impl_self_needs_proof = src->rust_impl_self_needs_proof;
        const char *const *lists[] = {src->rust_type_params, src->rust_unbounded_impl_params};
        const char ***copies[] = {&dst->type_params, &dst->rust_unbounded_impl_params};
        for (int list = 0; list < 2; list++) {
            int n = 0;
            while (lists[list] && lists[list][n]) {
                n++;
            }
            const char **names =
                lists[list] ? cbm_arena_alloc(arena, (size_t)(n + 1) * sizeof(const char *)) : NULL;
            for (int i = 0; names && i < n; i++) {
                names[i] = cbm_arena_strdup(arena, lists[list][i]);
            }
            if (names) {
                names[n] = NULL;
            }
            *copies[list] = names;
        }
        /* A type alias carries its target as the type it stands for and its
         * type parameters as that type's positional parameters. */
        if (src->type_alias) {
            dst->return_types = cbm_arena_strdup(arena, src->type_alias);
            int n = 0;
            while (src->type_alias_params && src->type_alias_params[n]) {
                n++;
            }
            const char **params =
                n > 0 ? (const char **)cbm_arena_alloc(arena, (size_t)n * sizeof(const char *))
                      : NULL;
            for (int i = 0; params && i < n; i++) {
                params[i] = cbm_arena_strdup(arena, src->type_alias_params[i]);
            }
            dst->signature_param_types = params;
            dst->signature_param_count = params ? n : 0;
        }
    }
    return 0;
}

/* Go, Rust and C: fold per-field "Field" definitions into their owning type's
 * field_defs. extract_defs.c emits one flat CBMDefinition per struct field
 * (label "Field", parent_class = owning struct QN, name = field name,
 * return_type = raw type text). Those rows are dropped by pxc_build_lsp_def
 * (pxc_map_label excludes "Field"), so without this fold every struct
 * registers with zero fields: Go field-chain calls (h.svc.Handle) can never
 * resolve, and a C member access on a struct from another file (every header)
 * has no owner to bind. Fields are always declared in the same file as their
 * struct, so scanning the file's own defs covers every case. Runs inside
 * cbm_pxc_collect_all_defs — one site covers both the prebuilt-registry path
 * and the per-file fallback, since both consume all_defs.
 *
 * type_label is the label the language's aggregate carries: "Struct" for Go
 * and Rust, "Class" for a C struct or union. */
static void pxc_fold_struct_fields(CBMArena *arena, const CBMFileResult *result, CBMLSPDef *defs,
                                   int start, int end, const char *type_label) {
    if (!arena || !result || !defs || start >= end) {
        return;
    }
    for (int si = start; si < end; si++) {
        CBMLSPDef *dst = &defs[si];
        if (!dst->label || strcmp(dst->label, type_label) != 0 || !dst->qualified_name) {
            continue;
        }
        int count = 0;
        size_t total = 0; /* "name:type" bytes; separators and NUL added below */
        for (int di = 0; di < result->defs.count; di++) {
            const CBMDefinition *fd = &result->defs.items[di];
            if (!fd->label || !fd->parent_class || !fd->name || !fd->name[0] || !fd->return_type ||
                !fd->return_type[0] || strcmp(fd->label, "Field") != 0 ||
                strcmp(fd->parent_class, dst->qualified_name) != 0) {
                continue;
            }
            total += strlen(fd->name) + 1 + strlen(fd->return_type);
            count++;
        }
        if (count == 0) {
            continue;
        }
        /* count - 1 separators + NUL. */
        size_t bufsz = total + (size_t)(count - 1) + 1;
        char *buf = (char *)cbm_arena_alloc(arena, bufsz);
        if (!buf) {
            continue;
        }
        char *p = buf;
        int written = 0;
        for (int di = 0; di < result->defs.count; di++) {
            const CBMDefinition *fd = &result->defs.items[di];
            if (!fd->label || !fd->parent_class || !fd->name || !fd->name[0] || !fd->return_type ||
                !fd->return_type[0] || strcmp(fd->label, "Field") != 0 ||
                strcmp(fd->parent_class, dst->qualified_name) != 0) {
                continue;
            }
            size_t n = strlen(fd->name);
            memcpy(p, fd->name, n);
            p += n;
            *p++ = ':';
            n = strlen(fd->return_type);
            memcpy(p, fd->return_type, n);
            p += n;
            if (written + 1 < count) {
                *p++ = '|';
            }
            written++;
        }
        *p = '\0';
        dst->field_defs = buf;
    }
}

/* Append "name:type" to a "|"-joined field list held in `arena`. */
static const char *pxc_field_defs_append(CBMArena *arena, const char *list, const char *name,
                                         const char *type) {
    if (!type || !type[0] || strchr(type, '|')) {
        return list;
    }
    return list && list[0] ? cbm_arena_sprintf(arena, "%s|%s:%s", list, name, type)
                           : cbm_arena_sprintf(arena, "%s:%s", name, type);
}

/* Rust: a tuple struct's positions and an enum's variant payloads are fields
 * a pattern destructures. A tuple struct `W(A, B)` or a tuple alias
 * `type W = (A, B);` (its def carries the elements as param_types) gains
 * fields `0:A|1:B`; an enum gains, per variant
 * `V`, `V.0:A` for a tuple payload and `V.f:T` for a named field `f` (the
 * Field defs scoped by the variant's QN), and `V.:E` for the variant itself,
 * payload or not, so a value path `E::V` is provably a variant. A field name
 * holding a `.` never matches a member access, so the enum's own member
 * lookup is unchanged. */
static void pxc_fold_rust_payload_fields(CBMArena *arena, const CBMFileResult *result,
                                         CBMLSPDef *defs, int start, int end) {
    if (!arena || !result || !defs || start >= end) {
        return;
    }
    for (int si = start; si < end; si++) {
        CBMLSPDef *dst = &defs[si];
        if (!dst->label || !dst->qualified_name ||
            (strcmp(dst->label, "Struct") != 0 && strcmp(dst->label, "Type") != 0 &&
             strcmp(dst->label, "Enum") != 0)) {
            continue;
        }
        bool is_enum = strcmp(dst->label, "Enum") == 0;
        size_t qn_len = strlen(dst->qualified_name);
        const char *list = dst->field_defs;
        for (int di = 0; di < result->defs.count; di++) {
            const CBMDefinition *d = &result->defs.items[di];
            if (!d->label || !d->qualified_name) {
                continue;
            }
            if (!is_enum) {
                if (strcmp(d->label, dst->label) == 0 && d->param_types &&
                    strcmp(d->qualified_name, dst->qualified_name) == 0) {
                    for (int p = 0; d->param_types[p]; p++) {
                        list = pxc_field_defs_append(arena, list, cbm_arena_sprintf(arena, "%d", p),
                                                     d->param_types[p]);
                    }
                }
                continue;
            }
            if (strcmp(d->label, "EnumVariant") != 0 || !d->name ||
                strncmp(d->qualified_name, dst->qualified_name, qn_len) != 0 ||
                d->qualified_name[qn_len] != '.' ||
                (strncmp(d->qualified_name + qn_len + 1, d->name, strlen(d->name)) != 0 ||
                 (d->qualified_name[qn_len + 1 + strlen(d->name)] != '\0' &&
                  d->qualified_name[qn_len + 1 + strlen(d->name)] != '#'))) {
                continue;
            }
            list = pxc_field_defs_append(arena, list, cbm_arena_sprintf(arena, "%s.", d->name),
                                         dst->short_name);
            for (int p = 0; d->param_types && d->param_types[p]; p++) {
                list = pxc_field_defs_append(
                    arena, list, cbm_arena_sprintf(arena, "%s.%d", d->name, p), d->param_types[p]);
            }
            for (int fi = 0; fi < result->defs.count; fi++) {
                const CBMDefinition *fd = &result->defs.items[fi];
                if (fd->label && strcmp(fd->label, "Field") == 0 && fd->parent_class && fd->name &&
                    strcmp(fd->parent_class, d->qualified_name) == 0) {
                    list = pxc_field_defs_append(
                        arena, list, cbm_arena_sprintf(arena, "%s.%s", d->name, fd->name),
                        fd->return_type);
                }
            }
        }
        dst->field_defs = list;
    }
}

/* Python annotation text -> the one type name it declares, or NULL. Strips a
 * string-literal forward reference and unwraps `Optional[X]`, `X | None` and
 * `None | X`; anything else with brackets, commas or a real union (generics,
 * Callable, Union[A, B]) names no single receiver type and is dropped. */
static const char *pxc_py_annotation_type_name(CBMArena *arena, const char *text) {
    if (!text) {
        return NULL;
    }
    while (*text == ' ') {
        text++;
    }
    size_t n = strlen(text);
    while (n > 0 && text[n - 1] == ' ') {
        n--;
    }
    if (n >= 2 && (text[0] == '"' || text[0] == '\'') && text[n - 1] == text[0]) {
        text++;
        n -= 2;
    }
    static const char *const optional_prefixes[] = {"Optional[", "typing.Optional["};
    for (size_t i = 0; i < sizeof(optional_prefixes) / sizeof(optional_prefixes[0]); i++) {
        size_t plen = strlen(optional_prefixes[i]);
        if (n > plen + 1 && strncmp(text, optional_prefixes[i], plen) == 0 && text[n - 1] == ']') {
            text += plen;
            n -= plen + 1;
            break;
        }
    }
    static const char none_tail[] = " | None";
    static const char none_head[] = "None | ";
    const size_t none_len = sizeof(none_tail) - 1;
    if (n > none_len && strncmp(text + n - none_len, none_tail, none_len) == 0) {
        n -= none_len;
    } else if (n > none_len && strncmp(text, none_head, none_len) == 0) {
        text += none_len;
        n -= none_len;
    }
    if (n == 0) {
        return NULL;
    }
    for (size_t i = 0; i < n; i++) {
        char c = text[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '.';
        if (!ok) {
            return NULL;
        }
    }
    return cbm_arena_strndup(arena, text, n);
}

/* Python: fold the file's annotated instance fields (result->field_types,
 * #1277) into their class defs' field_defs as "name:QN|name:QN", the format
 * py_register_lsp_defs reads. Each annotation resolves through the same
 * registry + import map, and the same weak-strategy and type-like vetoes, as
 * the file's base classes (pxc_resolve_base_qn): a field whose type is not a
 * confidently-known project type is left out rather than guessed. Linear in
 * the file: one hash of the file's class defs, one pass over its fields. */
typedef struct {
    int owner; /* index into defs[], or -1 */
    const char *type_qn;
} pxc_py_field_t;

static void pxc_fold_py_field_types(CBMArena *arena, const CBMFileResult *result, CBMLSPDef *defs,
                                    int start, int end, const pxc_base_scope_t *bs,
                                    const char *module_qn) {
    int nf = result ? result->field_types.count : 0;
    if (!arena || nf == 0 || !defs || start >= end || !bs || !bs->reg) {
        return;
    }
    CBMHashTable *by_qn = cbm_ht_create((uint32_t)(end - start));
    pxc_py_field_t *fields = (pxc_py_field_t *)cbm_arena_alloc(arena, (size_t)nf * sizeof(*fields));
    /* Per class def: exact buffer size, then the write cursor into it. */
    size_t *lens = (size_t *)cbm_arena_alloc(arena, (size_t)(end - start) * sizeof(size_t));
    size_t *used = (size_t *)cbm_arena_alloc(arena, (size_t)(end - start) * sizeof(size_t));
    if (!by_qn || !fields || !lens || !used) {
        cbm_ht_free(by_qn); /* NULL-safe */
        return;
    }
    memset(lens, 0, (size_t)(end - start) * sizeof(size_t));
    memset(used, 0, (size_t)(end - start) * sizeof(size_t));
    for (int si = start; si < end; si++) {
        if (defs[si].label && strcmp(defs[si].label, "Class") == 0 && defs[si].qualified_name) {
            cbm_ht_set(by_qn, defs[si].qualified_name, &defs[si]);
        }
    }
    for (int f = 0; f < nf; f++) {
        const CBMFieldType *ft = &result->field_types.items[f];
        fields[f].owner = -1;
        fields[f].type_qn = NULL;
        CBMLSPDef *owner = ft->class_qn ? (CBMLSPDef *)cbm_ht_get(by_qn, ft->class_qn) : NULL;
        const char *name = owner ? pxc_py_annotation_type_name(arena, ft->type_text) : NULL;
        const char *qn = name ? pxc_resolve_base_qn(bs, name, module_qn) : NULL;
        if (!qn || !ft->field_name || !ft->field_name[0]) {
            continue;
        }
        fields[f].owner = (int)(owner - defs);
        fields[f].type_qn = qn;
        /* "name:QN" plus one byte for the '|' or the NUL after it. */
        lens[fields[f].owner - start] += strlen(ft->field_name) + 1 + strlen(qn) + 1;
    }
    cbm_ht_free(by_qn);
    for (int si = start; si < end; si++) {
        char *buf = lens[si - start] ? (char *)cbm_arena_alloc(arena, lens[si - start]) : NULL;
        if (buf) {
            buf[0] = '\0';
        }
        defs[si].field_defs = buf;
    }
    for (int f = 0; f < nf; f++) {
        char *buf = fields[f].owner >= 0 ? (char *)defs[fields[f].owner].field_defs : NULL;
        if (!buf) {
            continue;
        }
        int slot = fields[f].owner - start;
        int wrote =
            snprintf(buf + used[slot], lens[slot] - used[slot], "%s%s:%s", used[slot] ? "|" : "",
                     result->field_types.items[f].field_name, fields[f].type_qn);
        if (wrote > 0) {
            used[slot] += (size_t)wrote;
        }
    }
}

/* Carry one Rust type-level impl independently of any method definition.
 * `impl Trait for Type {}` is semantically meaningful even when the block is
 * empty (the trait may provide defaults), so attaching the relation only to
 * concrete method records is lossy. */
static int pxc_build_rust_impl_relation(CBMArena *arena, const CBMImplTrait *impl,
                                        const char *project_name, const char *rel_path,
                                        const char *module_qn, CBMLSPDef *dst) {
    if (!arena || !impl || !impl->trait_name || !impl->struct_name || !impl->struct_qn) {
        return -1;
    }
    const char *receiver_qn = cbm_arena_strdup(arena, impl->struct_qn);
    memset(dst, 0, sizeof(*dst));
    dst->qualified_name = receiver_qn;
    dst->short_name = pxc_qn_leaf(receiver_qn);
    dst->label = "RustImpl";
    dst->receiver_type = receiver_qn;
    dst->receiver_spelling = cbm_arena_strdup(arena, impl->struct_name);
    dst->def_module_qn = module_qn;
    dst->trait_qn = cbm_arena_strdup(arena, impl->trait_name); /* raw; canonicalized later */
    dst->impl_target = impl->target_type ? cbm_arena_strdup(arena, impl->target_type) : NULL;
    dst->impl_item = impl->item_type ? cbm_arena_strdup(arena, impl->item_type) : NULL;
    dst->lang = CBM_LANG_RUST;
    dst->is_rust_impl_relation = true;
    return 0;
}

/* Collect a project-wide CBMLSPDef[] from all cached results. Returns a
 * malloc'd array (caller frees) of length *out_count. String fields are
 * borrowed from cache[i]->arena and from def_modules[i] (also borrowed).
 * Required configured-owner spill load failure: NULL, *out_count = -1, and
 * cleared out_def_starts. No partial aggregate is usable in that case. */
CBMLSPDef *cbm_pxc_collect_all_defs(const cbm_pipeline_ctx_t *ctx, CBMArena *arena,
                                    CBMFileResult **cache, const cbm_file_info_t *files,
                                    int file_count, const char *project_name, char **def_modules,
                                    int *out_count, int *out_def_starts) {
    const cbm_result_spill_t *spill = ctx ? ctx->spill : NULL;
    bool owner_walk_required = ctx && atomic_load(&ctx->test_definition_owners_seen);
    int total = 0;
    for (int i = 0; i < file_count; i++) {
        int defs = 0;
        int impls = 0;
        if (cache[i]) {
            owner_walk_required = owner_walk_required || cache[i]->has_test_definition_owners;
            defs = cache[i]->defs.count;
            impls = cache[i]->impl_traits.count;
        } else if (spill && cbm_result_spill_has(spill, i)) {
            cbm_result_spill_peek_counts(spill, i, &defs, &impls);
        }
        total += defs;
        if (files[i].language == CBM_LANG_RUST) {
            total += impls;
        }
    }
    if (total == 0) {
        *out_count = 0;
        if (out_def_starts) {
            memset(out_def_starts, 0, (size_t)(file_count + 1) * sizeof(int));
        }
        return NULL;
    }
    CBMLSPDef *defs = (CBMLSPDef *)calloc((size_t)total, sizeof(CBMLSPDef));
    if (!defs) {
        *out_count = 0;
        if (out_def_starts) {
            memset(out_def_starts, 0, (size_t)(file_count + 1) * sizeof(int));
        }
        return NULL;
    }
    for (int fi = 0; fi < file_count; fi++) {
        def_modules[fi] = cbm_pipeline_fqn_module_dir(project_name, files[fi].rel_path,
                                                      pxc_module_is_dir(files[fi].language));
    }
    int idx = 0;
    for (int fi = 0; fi < file_count; fi++) {
        if (out_def_starts) {
            out_def_starts[fi] = idx;
        }
        const int file_start = idx;
        CBMFileResult *fr = cache[fi];
        bool fr_loaded = false;
        if (!fr && spill && cbm_result_spill_has(spill, fi)) {
            /* The test bit applies only here: registry and subsequent direct
             * loads remain real. Returning immediately forces exactly the
             * first actual collector spilled-load boundary, without timing. */
#if defined(CBM_ENABLE_TEST_SEAMS) && CBM_ENABLE_TEST_SEAMS
            bool force_null = owner_walk_required && ctx &&
                              (ctx->test_fault_mask & CBM_PIPELINE_TEST_FAULT_COLLECT_SPILL_NULL);
            fr = force_null ? NULL : cbm_result_spill_load(spill, fi);
#else
            fr = cbm_result_spill_load(spill, fi);
#endif
            fr_loaded = fr != NULL;
            if (!fr && owner_walk_required) {
                free(defs);
                *out_count = -1;
                if (out_def_starts) {
                    memset(out_def_starts, 0, (size_t)(file_count + 1) * sizeof(int));
                }
                return NULL;
            }
        }
        if (!fr)
            continue;
        const char *namespace_name = fr->namespace_name;
        if ((!namespace_name || !namespace_name[0]) && files[fi].rel_path) {
            namespace_name = pxc_infer_jvm_namespace(arena, files[fi].rel_path, files[fi].language);
        }
        /* One import map per FILE (not per def, and not per base name): the
         * cross-file base-class resolution below needs the same local-name →
         * import-QN view pass_semantic uses. Built only for the languages
         * that consume resolved base QNs, and only when a caller supplied the
         * pipeline context (the surface-probe path passes NULL and keeps the
         * raw spelling). */
        pxc_base_scope_t bs = {.imports = &fr->imports,
                               .project_name = project_name,
                               .rel = files[fi].rel_path,
                               .lang = files[fi].language};
        if (ctx && ctx->registry && pxc_lang_resolves_base_qns(files[fi].language)) {
            bs.reg = ctx->registry;
            bs.gbuf = ctx->gbuf;
            cbm_pxc_build_import_map(ctx, project_name, files[fi].rel_path, files[fi].language, fr,
                                     &bs.imp_keys, &bs.imp_vals, &bs.imp_count);
        }
        for (int di = 0; di < fr->defs.count; di++) {
            if (pxc_build_lsp_def(arena, &fr->defs.items[di], def_modules[fi], namespace_name,
                                  files[fi].language, &defs[idx], bs.reg ? &bs : NULL) == 0) {
                idx++;
            }
        }
        if (files[fi].language == CBM_LANG_PYTHON) {
            pxc_fold_py_field_types(arena, fr, defs, file_start, idx, &bs, def_modules[fi]);
        }
        cbm_pxc_free_import_map(bs.imp_keys, bs.imp_vals, bs.imp_count); /* NULL-safe */
        /* Rust struct fields are declared beside their struct exactly like
         * Go's, and `self.field.method()` needs them for the same reason. */
        if (files[fi].language == CBM_LANG_GO || files[fi].language == CBM_LANG_RUST) {
            pxc_fold_struct_fields(arena, fr, defs, file_start, idx, "Struct");
        } else if (files[fi].language == CBM_LANG_C || files[fi].language == CBM_LANG_CPP) {
            /* Both: a `.h` file is classified C++, and that is where C declares
             * the structs its `.c` files access. */
            pxc_fold_struct_fields(arena, fr, defs, file_start, idx, "Class");
        }
        if (files[fi].language == CBM_LANG_RUST) {
            pxc_fold_rust_payload_fields(arena, fr, defs, file_start, idx);
            for (int ii = 0; ii < fr->impl_traits.count; ii++) {
                if (pxc_build_rust_impl_relation(arena, &fr->impl_traits.items[ii], project_name,
                                                 files[fi].rel_path, def_modules[fi],
                                                 &defs[idx]) == 0) {
                    idx++;
                }
            }
        }
        if (fr_loaded) {
            cbm_free_result(fr);
        }
    }
    if (out_def_starts) {
        out_def_starts[file_count] = idx;
    }
    pxc_apply_rust_path_modules(ctx, arena, cache, files, file_count, project_name, def_modules);
    *out_count = idx;
    return defs;
}

/* Return the one source import path bound to `local_name`, or NULL when the
 * extraction metadata is absent or two distinct imports bind the same local.
 * The latter is deliberately fail-closed: choosing either path would turn an
 * ambiguous Python value into a fabricated CALL_REFERENCE. */
static const char *pxc_unique_import_path(const CBMFileResult *result, const char *local_name,
                                          bool *out_ambiguous) {
    if (out_ambiguous)
        *out_ambiguous = false;
    if (!result || !local_name)
        return NULL;
    const char *path = NULL;
    for (int i = 0; i < result->imports.count; i++) {
        const CBMImport *imp = &result->imports.items[i];
        if (!imp->local_name || !imp->module_path || strcmp(imp->local_name, local_name) != 0) {
            continue;
        }
        if (path && strcmp(path, imp->module_path) != 0) {
            if (out_ambiguous)
                *out_ambiguous = true;
            return NULL;
        }
        path = imp->module_path;
    }
    return path;
}

static const char *pxc_import_leaf(const char *path) {
    if (!path)
        return NULL;
    const char *leaf = path;
    for (const char *p = path; *p; p++) {
        if (*p == '.' || *p == '/' || *p == '\\' || *p == ':')
            leaf = p + 1;
    }
    return leaf;
}

static char *pxc_kotlin_import_from_metadata(const CBMFileResult *result, const char *local_name) {
    bool ambiguous = false;
    const char *path = pxc_unique_import_path(result, local_name, &ambiguous);
    const char *leaf = pxc_import_leaf(path);
    if (ambiguous || !path || !path[0] || path[0] == '.' || !leaf ||
        strcmp(leaf, local_name) != 0) {
        return NULL;
    }
    /* Keep the source package/member spelling. Kotlin cross registries are
     * package-qualified (target.actual), not path/project-qualified. The
     * resolver still requires a registered function/type before emitting an
     * exact semantic relationship. */
    return cbm_mem_strdup(CBM_MEM_CLASS_OTHER, path);
}

/* IMPORTS edges correctly target dependency modules, but Python from-imports
 * also carry a source-level member: `from target import handler` is extracted
 * as module_path=`target.handler` while its edge targets Module `P.target`.
 * Reattach that member only when the raw metadata proves one unique,
 * non-aliased path. The shared Python registry must still materialize the
 * resulting exact QN before it earns CALL_REFERENCE. */
static char *pxc_import_value_qn(CBMLanguage lang, const CBMFileResult *result,
                                 const char *local_name, const cbm_gbuf_node_t *target) {
    if (!target || !target->qualified_name)
        return NULL;
    if (lang == CBM_LANG_KOTLIN)
        return pxc_kotlin_import_from_metadata(result, local_name);
    if (lang != CBM_LANG_PYTHON)
        return cbm_mem_strdup(CBM_MEM_CLASS_OTHER, target->qualified_name);

    bool ambiguous = false;
    const char *path = pxc_unique_import_path(result, local_name, &ambiguous);
    if (ambiguous)
        return NULL;
    const char *source_leaf = pxc_import_leaf(path);
    const char *target_leaf = pxc_import_leaf(target->qualified_name);
    if (!path || !source_leaf || !target_leaf || strcmp(source_leaf, local_name) != 0 ||
        strcmp(source_leaf, target_leaf) == 0 || !target->label ||
        (strcmp(target->label, "Module") != 0 && strcmp(target->label, "Folder") != 0)) {
        return cbm_mem_strdup(CBM_MEM_CLASS_OTHER, target->qualified_name);
    }

    size_t need = strlen(target->qualified_name) + 1 + strlen(source_leaf) + 1;
    char *qualified = (char *)cbm_alloc(CBM_MEM_CLASS_OTHER, need);
    if (!qualified)
        return NULL;
    snprintf(qualified, need, "%s.%s", target->qualified_name, source_leaf);
    return qualified;
}

static bool pxc_import_map_has_local(const char *const *keys, int count, const char *local_name) {
    if (!keys || !local_name)
        return false;
    for (int i = 0; i < count; i++) {
        if (keys[i] && strcmp(keys[i], local_name) == 0)
            return true;
    }
    return false;
}

/* Recover a Python import directly from extraction metadata when graph import
 * materialization produced no edge. Prefer an already materialized exact node
 * (including the shared project-prefix rule); otherwise accept only the
 * ordinary non-aliased dotted spelling and let the sealed Python registry
 * decide whether that QN denotes a class/function. A wrong candidate cannot
 * earn a semantic edge because the resolver requires registry materialization. */
static char *pxc_python_import_from_metadata(const cbm_gbuf_t *gbuf, const char *project_name,
                                             const char *local_name, const char *module_path) {
    if (!gbuf || !local_name || !module_path)
        return NULL;
    const char *source_leaf = pxc_import_leaf(module_path);
    if (!source_leaf || strcmp(source_leaf, local_name) != 0 || module_path[0] == '.')
        return NULL;

    const cbm_gbuf_node_t *exact =
        cbm_pipeline_lsp_target_node(gbuf, project_name, module_path, false);
    if (exact && exact->qualified_name)
        return cbm_mem_strdup(CBM_MEM_CLASS_OTHER, exact->qualified_name);

    const char *last_dot = strrchr(module_path, '.');
    if (last_dot && last_dot > module_path) {
        size_t module_len = (size_t)(last_dot - module_path);
        char *module = (char *)cbm_alloc(CBM_MEM_CLASS_OTHER, module_len + 1);
        if (!module)
            return NULL;
        memcpy(module, module_path, module_len);
        module[module_len] = '\0';
        const cbm_gbuf_node_t *module_node =
            cbm_pipeline_lsp_target_node(gbuf, project_name, module, false);
        cbm_free(CBM_MEM_CLASS_OTHER, module);
        if (module_node && module_node->qualified_name && module_node->label &&
            (strcmp(module_node->label, "Module") == 0 ||
             strcmp(module_node->label, "Folder") == 0)) {
            size_t need = strlen(module_node->qualified_name) + 1 + strlen(source_leaf) + 1;
            char *qualified = (char *)cbm_alloc(CBM_MEM_CLASS_OTHER, need);
            if (!qualified)
                return NULL;
            snprintf(qualified, need, "%s.%s", module_node->qualified_name, source_leaf);
            return qualified;
        }
    }

    if (!project_name || !project_name[0])
        return cbm_mem_strdup(CBM_MEM_CLASS_OTHER, module_path);
    size_t need = strlen(project_name) + 1 + strlen(module_path) + 1;
    char *qualified = (char *)cbm_alloc(CBM_MEM_CLASS_OTHER, need);
    if (!qualified)
        return NULL;
    snprintf(qualified, need, "%s.%s", project_name, module_path);
    return qualified;
}

/* Build the private per-file import map from graph and extraction bindings.
 * Both pipeline drivers call this implementation; publishing producers apply
 * the language guard even when a rejected import remains in this map. */
int cbm_pxc_build_import_map(const cbm_pipeline_ctx_t *ctx, const char *project_name,
                             const char *rel_path, CBMLanguage lang, const CBMFileResult *result,
                             const char ***out_keys, const char ***out_vals, int *out_count) {
    const cbm_gbuf_t *gbuf = ctx->gbuf;
    *out_keys = NULL;
    *out_vals = NULL;
    *out_count = 0;

    const cbm_gbuf_edge_t **edges = NULL;
    int edge_count = 0;
    char *file_qn = cbm_pipeline_fqn_compute(project_name, rel_path, "__file__");
    const cbm_gbuf_node_t *file_node = file_qn ? cbm_gbuf_find_by_qn(gbuf, file_qn) : NULL;
    free(file_qn);
    if (file_node && cbm_gbuf_find_edges_by_source_type(gbuf, file_node->id, "IMPORTS", &edges,
                                                        &edge_count) != 0) {
        edges = NULL;
        edge_count = 0;
    }

    int metadata_count =
        (lang == CBM_LANG_PYTHON || lang == CBM_LANG_KOTLIN) && result ? result->imports.count : 0;
    if (edge_count == 0 && (!result || result->imports.count == 0))
        return 0;

    size_t capacity = (size_t)edge_count + (result ? (size_t)result->imports.count : 0);
    const char **keys =
        (const char **)cbm_calloc(CBM_MEM_CLASS_OTHER, capacity * sizeof(const char *));
    const char **vals =
        (const char **)cbm_calloc(CBM_MEM_CLASS_OTHER, capacity * sizeof(const char *));
    if (!keys || !vals) {
        cbm_free(CBM_MEM_CLASS_OTHER, keys);
        cbm_free(CBM_MEM_CLASS_OTHER, vals);
        return 0;
    }
    int count = 0;
    for (int i = 0; i < edge_count; i++) {
        const cbm_gbuf_edge_t *e = edges[i];
        const cbm_gbuf_node_t *target = cbm_gbuf_find_by_id(gbuf, e->target_id);
        if (!target || !e->properties_json)
            continue;
        const char *start = strstr(e->properties_json, "\"local_name\":\"");
        if (!start)
            continue;
        start += strlen("\"local_name\":\"");
        const char *end = strchr(start, '"');
        if (!end || end <= start)
            continue;
        size_t n = (size_t)(end - start);
        char *local = (char *)cbm_alloc(CBM_MEM_CLASS_OTHER, n + 1);
        if (!local)
            continue;
        memcpy(local, start, n);
        local[n] = '\0';
        char *value = pxc_import_value_qn(lang, result, local, target);
        if (!value) {
            cbm_free(CBM_MEM_CLASS_OTHER, local);
            continue;
        }
        keys[count] = local;
        vals[count] = value;
        count++;
    }

    /* Keep rejected import bindings private. Erasing a binding would let a
     * weaker name guess replace the rejected target in later producers. */
    if (result) {
        file_qn = cbm_pipeline_fqn_compute(project_name, rel_path, "__file__");
        for (int i = 0; i < result->imports.count; i++) {
            const CBMImport *imp = &result->imports.items[i];
            if (!imp->local_name || !imp->local_name[0] || !imp->module_path ||
                pxc_import_map_has_local(keys, count, imp->local_name))
                continue;
            const cbm_gbuf_node_t *target =
                cbm_pipeline_resolve_import_node(ctx, rel_path, file_qn, imp, NULL);
            if (!target ||
                !cbm_suppress_cross_language_code_match(lang, ctx->repo_path, target->file_path))
                continue;
            char *local = cbm_mem_strdup(CBM_MEM_CLASS_OTHER, imp->local_name);
            char *value = pxc_import_value_qn(lang, result, imp->local_name, target);
            if (!local || !value) {
                cbm_free(CBM_MEM_CLASS_OTHER, local);
                cbm_free(CBM_MEM_CLASS_OTHER, value);
                continue;
            }
            keys[count] = local;
            vals[count++] = value;
        }
        free(file_qn);
    }

    /* IMPORTS edges are best-effort graph relationships. The cross-LSP still
     * has the authoritative extraction metadata in both sequential and
     * parallel caches, so a missing edge must not erase an otherwise exact
     * Python import. Add only locals not already represented by an edge. */
    for (int i = 0; i < metadata_count; i++) {
        const CBMImport *imp = &result->imports.items[i];
        if (!imp->local_name || !imp->local_name[0] || !imp->module_path ||
            pxc_import_map_has_local(keys, count, imp->local_name)) {
            continue;
        }
        bool ambiguous = false;
        const char *path = pxc_unique_import_path(result, imp->local_name, &ambiguous);
        if (ambiguous || !path)
            continue;
        char *value =
            lang == CBM_LANG_KOTLIN
                ? pxc_kotlin_import_from_metadata(result, imp->local_name)
                : pxc_python_import_from_metadata(gbuf, project_name, imp->local_name, path);
        if (!value)
            continue;
        char *local = cbm_mem_strdup(CBM_MEM_CLASS_OTHER, imp->local_name);
        if (!local) {
            cbm_free(CBM_MEM_CLASS_OTHER, value);
            continue;
        }
        keys[count] = local;
        vals[count] = value;
        count++;
    }
    *out_keys = keys;
    *out_vals = vals;
    *out_count = count;
    return 0;
}

void cbm_pxc_free_import_map(const char **keys, const char **vals, int count) {
    if (keys) {
        for (int i = 0; i < count; i++)
            cbm_free(CBM_MEM_CLASS_OTHER, (void *)keys[i]);
        cbm_free(CBM_MEM_CLASS_OTHER, (void *)keys);
    }
    if (vals) {
        for (int i = 0; i < count; i++)
            cbm_free(CBM_MEM_CLASS_OTHER, (void *)vals[i]);
        cbm_free(CBM_MEM_CLASS_OTHER, (void *)vals);
    }
}

/* Detect TS dialect flags from a relative path. */
void cbm_pxc_ts_modes(CBMLanguage lang, const char *rel_path, bool *out_js, bool *out_jsx,
                      bool *out_dts) {
    *out_js = (lang == CBM_LANG_JAVASCRIPT);
    *out_jsx = (lang == CBM_LANG_TSX);
    *out_dts = false;
    if (!rel_path)
        return;
    size_t rl = strlen(rel_path);
    if (lang == CBM_LANG_JAVASCRIPT && rl >= 4 && strcmp(rel_path + rl - 4, ".jsx") == 0) {
        *out_jsx = true;
    }
    if (lang == CBM_LANG_TYPESCRIPT && rl >= 5 && strcmp(rel_path + rl - 5, ".d.ts") == 0) {
        *out_dts = true;
    }
}

/* Returns true when this language has a cross-file LSP wired up. */
bool cbm_pxc_has_cross_lsp(CBMLanguage lang) {
    switch (lang) {
    case CBM_LANG_GO:
    case CBM_LANG_C:
    case CBM_LANG_CPP:
    case CBM_LANG_CUDA:
    case CBM_LANG_PYTHON:
    case CBM_LANG_JAVASCRIPT:
    case CBM_LANG_TYPESCRIPT:
    case CBM_LANG_TSX:
    case CBM_LANG_PHP:
    case CBM_LANG_CSHARP: /* tier-2 prebuilt registry path (pass_parallel.c) */
    case CBM_LANG_JAVA:   /* fallback cbm_pxc_run_one path */
    case CBM_LANG_KOTLIN: /* fallback cbm_pxc_run_one path */
    case CBM_LANG_RUST:   /* fallback cbm_pxc_run_one path (manifest-aware) */
        return true;
    default:
        return false;
    }
}

/* Per-thread arenas for the per-file scratch below (the walk's overlay and the
 * append dedup keys), kept rewound between files on a resolve worker that has
 * promised to drop them at its end (cbm_pxc_thread_scratch_begin/_end).
 * Opened fresh per file they were 18 k 64 KB-default arenas on the Go corpus,
 * 1.6 GB allocated and 60 % never written (waste sanitizer, 2026-09-17). An
 * arena one big file grew past PXC_KEEP_BYTES is dropped, not held; a nested
 * take finds the slot empty and opens its own. Everywhere else (the sequential
 * pass) take/give are exactly init/destroy. */
enum { PXC_SCRATCH_DISPATCH = 0, PXC_SCRATCH_KEYS = 1, PXC_SCRATCH_COUNT = 2 };
enum { PXC_KEEP_BYTES = 4 * 1024 * 1024 };
static CBM_TLS bool tl_pxc_keep;
/* The parked arenas live on the HEAP behind one thread-local pointer. Inline in
 * thread-local storage they were ~8 KB of static TLS charged to every thread in
 * the image, and static TLS comes out of each thread's own stack allocation, so
 * past a certain size a small-stack thread cannot be created at all — which is
 * precisely how this branch broke the 64 KB parent-death watchdog and, with it,
 * indexing on x86-64 Linux (PR #2233). The holder is allocated once per
 * keeping thread, so parking still costs no allocation per file. */
typedef struct {
    CBMArena arena[PXC_SCRATCH_COUNT];
    bool live[PXC_SCRATCH_COUNT];
} pxc_scratch_t;
static CBM_TLS pxc_scratch_t *tl_pxc;

static void pxc_scratch_take(int slot, CBMArena *into) {
    if (tl_pxc && tl_pxc->live[slot]) {
        *into = tl_pxc->arena[slot];
        tl_pxc->live[slot] = false;
        cbm_arena_rewind(into);
        return;
    }
    cbm_arena_init(into);
}

static void pxc_scratch_give(int slot, CBMArena *from) {
    if (tl_pxc_keep && from->nblocks > 0 && cbm_arena_capacity(from) <= (size_t)PXC_KEEP_BYTES) {
        if (!tl_pxc) {
            tl_pxc = cbm_calloc(CBM_MEM_CLASS_OTHER, sizeof(pxc_scratch_t));
        }
        if (tl_pxc && !tl_pxc->live[slot]) {
            tl_pxc->arena[slot] = *from;
            tl_pxc->live[slot] = true;
            memset(from, 0, sizeof(*from));
            return;
        }
    }
    cbm_arena_destroy(from);
}

void cbm_pxc_thread_scratch_begin(void) {
    tl_pxc_keep = true;
}

void cbm_pxc_thread_scratch_end(void) {
    if (tl_pxc) {
        for (int slot = 0; slot < PXC_SCRATCH_COUNT; slot++) {
            if (tl_pxc->live[slot]) {
                cbm_arena_destroy(&tl_pxc->arena[slot]);
                tl_pxc->live[slot] = false;
            }
        }
        cbm_free(CBM_MEM_CLASS_OTHER, tl_pxc);
        tl_pxc = NULL;
    }
    tl_pxc_keep = false;
}

/* Append cross-file results from `src_out` (allocated in a scratch arena
 * about to be destroyed) into `dst_calls` (lives in cache_entry->arena),
 * copying every string field into dst_arena. A manifest-qualified Rust
 * cross-crate result supersedes any earlier result for the exact same source
 * occurrence: the Cargo member path is stronger evidence than a same-named
 * local fallback. Skips entries whose
 * (kind, caller_qn, callee_qn, source span) is already present — avoids
 * inflating the array with cross-file duplicates without conflating distinct
 * same-named occurrences. For an exact duplicate, retain the higher-confidence
 * record.
 *
 * Dedup uses a hash table keyed on
 * "kind\x1fcaller\x1fcallee\x1fstart:end\x1forigin", giving O(1)
 * membership.
 * The previous linear strcmp scan made each append O(n), so a file that
 * resolved very many cross-calls turned the whole append into O(n^2) and could
 * peg a core for minutes (observed: an index hung in pxc_append_results/strcmp).
 * The key strings live in a scratch arena that is destroyed after the table. */
/* Replace a file's Rust impl links with this run's (strings copied into the
 * result arena; the resolver's scratch arena dies with the call). */
static void pxc_store_rust_impl_links(CBMFileResult *r, const CBMRustImplLinkArray *links) {
    r->rust_impl_links.count = 0;
    for (int i = 0; i < links->count; i++) {
        CBMRustImplLink link = links->items[i];
        link.type_qn = link.type_qn ? cbm_arena_strdup(&r->arena, link.type_qn) : NULL;
        link.trait_qn = link.trait_qn ? cbm_arena_strdup(&r->arena, link.trait_qn) : NULL;
        cbm_rustimpllink_push(&r->rust_impl_links, &r->arena, link);
    }
}

static void pxc_append_results(CBMArena *dst_arena, CBMResolvedCallArray *dst_calls,
                               const CBMResolvedCallArray *src_out) {
    if (!dst_calls || !src_out)
        return;

    CBMArena keys;
    pxc_scratch_take(PXC_SCRATCH_KEYS, &keys);
    CBMHashTable *seen = cbm_ht_create((uint32_t)(dst_calls->count + src_out->count + 1));

    /* Per-file Rust resolution may already have confidently matched the tail
     * of `member::call()` to a same-named local function. Once the cross pass
     * proves that `member` is a declared Cargo workspace member, replace only
     * records for that exact parser occurrence before constructing the dedup
     * table. This preserves distinct same-named calls at other spans while
     * preventing the stale local result from out-ranking manifest evidence. */
    for (int j = 0; j < src_out->count; j++) {
        const CBMResolvedCall *src = &src_out->items[j];
        bool supersedes = src->strategy && (strcmp(src->strategy, "lsp_cross_crate") == 0 ||
                                            strcmp(src->strategy, "lsp_module_path") == 0);
        if (!supersedes || !src->caller_qn || !src->callee_qn ||
            src->site_end_byte <= src->site_start_byte) {
            continue;
        }
        for (int i = 0; i < dst_calls->count; i++) {
            CBMResolvedCall *dst = &dst_calls->items[i];
            if (!dst->caller_qn || dst->kind != src->kind ||
                dst->site_start_byte != src->site_start_byte ||
                dst->site_end_byte != src->site_end_byte ||
                dst->source_origin != src->source_origin ||
                strcmp(dst->caller_qn, src->caller_qn) != 0) {
                continue;
            }
            dst->caller_qn = cbm_arena_strdup(dst_arena, src->caller_qn);
            dst->callee_qn = cbm_arena_strdup(dst_arena, src->callee_qn);
            dst->caller_impl_key =
                src->caller_impl_key ? cbm_arena_strdup(dst_arena, src->caller_impl_key) : NULL;
            dst->callee_impl_key =
                src->callee_impl_key ? cbm_arena_strdup(dst_arena, src->callee_impl_key) : NULL;
            dst->strategy = cbm_arena_strdup(dst_arena, src->strategy);
            dst->confidence = src->confidence;
            dst->reason = src->reason ? cbm_arena_strdup(dst_arena, src->reason) : NULL;
            dst->kind = src->kind;
            dst->site_start_byte = src->site_start_byte;
            dst->site_end_byte = src->site_end_byte;
            dst->source_origin = src->source_origin;
        }
    }

    for (int i = 0; i < dst_calls->count; i++) {
        const CBMResolvedCall *rc = &dst_calls->items[i];
        if (rc->caller_qn && rc->callee_qn) {
            char *k = cbm_arena_sprintf(&keys, "%u\x1f%s\x1f%s\x1f%u:%u\x1f%u", (unsigned)rc->kind,
                                        rc->caller_qn, rc->callee_qn, rc->site_start_byte,
                                        rc->site_end_byte, (unsigned)rc->source_origin);
            if (k) {
                void *prior = cbm_ht_get(seen, k);
                if (!prior ||
                    rc->confidence > dst_calls->items[(int)(uintptr_t)prior - 1].confidence) {
                    const char *stored = cbm_ht_get_key(seen, k);
                    cbm_ht_set(seen, stored ? stored : k, (void *)(uintptr_t)(i + 1));
                }
            }
        }
    }

    for (int j = 0; j < src_out->count; j++) {
        const CBMResolvedCall *src = &src_out->items[j];
        if (!src->caller_qn || !src->callee_qn)
            continue;
        char *k = cbm_arena_sprintf(&keys, "%u\x1f%s\x1f%s\x1f%u:%u\x1f%u", (unsigned)src->kind,
                                    src->caller_qn, src->callee_qn, src->site_start_byte,
                                    src->site_end_byte, (unsigned)src->source_origin);
        void *prior = k ? cbm_ht_get(seen, k) : NULL;
        if (prior) {
            CBMResolvedCall *dst = &dst_calls->items[(int)(uintptr_t)prior - 1];
            if (src->confidence > dst->confidence) {
                dst->caller_qn = cbm_arena_strdup(dst_arena, src->caller_qn);
                dst->callee_qn = cbm_arena_strdup(dst_arena, src->callee_qn);
                dst->caller_impl_key =
                    src->caller_impl_key ? cbm_arena_strdup(dst_arena, src->caller_impl_key) : NULL;
                dst->callee_impl_key =
                    src->callee_impl_key ? cbm_arena_strdup(dst_arena, src->callee_impl_key) : NULL;
                dst->strategy = src->strategy ? cbm_arena_strdup(dst_arena, src->strategy) : NULL;
                dst->confidence = src->confidence;
                dst->reason = src->reason ? cbm_arena_strdup(dst_arena, src->reason) : NULL;
                dst->kind = src->kind;
                dst->site_start_byte = src->site_start_byte;
                dst->site_end_byte = src->site_end_byte;
                dst->source_origin = src->source_origin;
            }
            continue;
        }
        CBMResolvedCall dst = {0};
        dst.caller_qn = cbm_arena_strdup(dst_arena, src->caller_qn);
        dst.callee_qn = cbm_arena_strdup(dst_arena, src->callee_qn);
        dst.caller_impl_key =
            src->caller_impl_key ? cbm_arena_strdup(dst_arena, src->caller_impl_key) : NULL;
        dst.callee_impl_key =
            src->callee_impl_key ? cbm_arena_strdup(dst_arena, src->callee_impl_key) : NULL;
        dst.strategy = src->strategy ? cbm_arena_strdup(dst_arena, src->strategy) : NULL;
        dst.confidence = src->confidence;
        dst.reason = src->reason ? cbm_arena_strdup(dst_arena, src->reason) : NULL;
        dst.kind = src->kind;
        dst.site_start_byte = src->site_start_byte;
        dst.site_end_byte = src->site_end_byte;
        dst.source_origin = src->source_origin;
        cbm_resolvedcall_push(dst_calls, dst_arena, dst);
        if (k) {
            cbm_ht_set(seen, k, (void *)(uintptr_t)dst_calls->count);
        }
    }

    cbm_ht_free(seen);
    pxc_scratch_give(PXC_SCRATCH_KEYS, &keys);
}

/* Merge exact synthetic call carriers produced by a cross-LSP resolver. The
 * source may live in the per-file result arena or in cbm_pxc_run_one's scratch
 * arena, so every pointer-bearing field is copied explicitly. Invalid/legacy
 * zero-span carriers are rejected: a requires-LSP carrier is safe only when it
 * can join the semantic record for the same source occurrence. */
static void pxc_append_synthetic_calls(CBMArena *dst_arena, CBMCallArray *dst_calls,
                                       const CBMCallArray *src_calls) {
    if (!dst_arena || !dst_calls || !src_calls || src_calls->count <= 0)
        return;

    CBMArena keys;
    pxc_scratch_take(PXC_SCRATCH_KEYS, &keys);
    CBMHashTable *seen = cbm_ht_create((uint32_t)(dst_calls->count + src_calls->count + 1));

    for (int i = 0; i < dst_calls->count; i++) {
        const CBMCall *call = &dst_calls->items[i];
        if (!call->callee_name || !call->enclosing_func_qn ||
            call->site_end_byte <= call->site_start_byte) {
            continue;
        }
        char *key = cbm_arena_sprintf(
            &keys, "%s\x1f%s\x1f%u:%u\x1f%u\x1f%d:%d", call->enclosing_func_qn, call->callee_name,
            call->site_start_byte, call->site_end_byte, (unsigned)call->source_origin,
            call->requires_lsp_resolution ? 1 : 0, call->is_method ? 1 : 0);
        if (key && !cbm_ht_get(seen, key))
            cbm_ht_set(seen, key, (void *)(uintptr_t)(i + 1));
    }

    for (int i = 0; i < src_calls->count; i++) {
        const CBMCall *src = &src_calls->items[i];
        if (!src->requires_lsp_resolution || !src->callee_name || !src->enclosing_func_qn ||
            src->site_end_byte <= src->site_start_byte) {
            continue;
        }
        char *key = cbm_arena_sprintf(
            &keys, "%s\x1f%s\x1f%u:%u\x1f%u\x1f%d:%d", src->enclosing_func_qn, src->callee_name,
            src->site_start_byte, src->site_end_byte, (unsigned)src->source_origin,
            src->requires_lsp_resolution ? 1 : 0, src->is_method ? 1 : 0);
        if (key && cbm_ht_get(seen, key))
            continue;

        CBMCall dst = *src;
        dst.callee_name = cbm_arena_strdup(dst_arena, src->callee_name);
        dst.enclosing_func_qn = cbm_arena_strdup(dst_arena, src->enclosing_func_qn);
        dst.first_string_arg =
            src->first_string_arg ? cbm_arena_strdup(dst_arena, src->first_string_arg) : NULL;
        dst.second_arg_name =
            src->second_arg_name ? cbm_arena_strdup(dst_arena, src->second_arg_name) : NULL;
        dst.args = NULL;
        if (src->args && src->arg_count > 0) {
            dst.args = cbm_arena_calloc(dst_arena, (size_t)src->arg_count * sizeof(CBMCallArg));
            if (!dst.args) {
                dst.arg_count = 0;
            }
        }
        for (int ai = 0; dst.args && ai < src->arg_count; ai++) {
            dst.args[ai].index = src->args[ai].index;
            dst.args[ai].expr =
                src->args[ai].expr ? cbm_arena_strdup(dst_arena, src->args[ai].expr) : NULL;
            dst.args[ai].value =
                src->args[ai].value ? cbm_arena_strdup(dst_arena, src->args[ai].value) : NULL;
            dst.args[ai].keyword =
                src->args[ai].keyword ? cbm_arena_strdup(dst_arena, src->args[ai].keyword) : NULL;
        }
        cbm_calls_push(dst_calls, dst_arena, dst);
        if (key)
            cbm_ht_set(seen, key, (void *)(uintptr_t)dst_calls->count);
    }

    cbm_ht_free(seen);
    pxc_scratch_give(PXC_SCRATCH_KEYS, &keys);
}

/* ── Rust workspace manifest (Cargo.toml) for cross-CRATE resolution ──
 *
 * cbm_pxc_run_one's signature is shared with the parallel pass
 * (pass_parallel.c) and cannot grow a manifest parameter without touching
 * that file. We therefore pass the parsed workspace manifest to the Rust
 * cross-file resolver through a file-static borrowed pointer that the
 * sequential driver (cbm_pipeline_pass_lsp_cross, below) sets up once per
 * pass run from the project's root Cargo.toml. The manifest's strings are
 * owned by `g_pxc_rust_manifest_arena`; the pointer is borrowed (NULL when
 * the project has no Cargo.toml — single-crate / non-workspace projects,
 * where in-file resolution needs no workspace metadata). */
static _Thread_local const CBMCargoManifest *g_pxc_rust_manifest = NULL;

/* ── CBM_RUST_RESOLVE_DUMP: development oracle lane ───────────────────
 * When the variable names a file, rows use this one TSV layout:
 * kind, source_file, occurrence_start, occurrence_end, source_qn, source_callee,
 * target_qn, strategy, confidence, verdict_or_kind, callee_impl_key, reason,
 * callee_identifier_start, callee_identifier_end.
 *
 * R rows describe resolver output; their final identifier columns are zero.
 * C rows are written only after a Rust CALLS insertion succeeds. Their source
 * and target QNs come from the graph nodes used by that insertion, verdict is
 * "admitted", reason is "-", and the final columns are the parser-owned exact
 * identifier span. Both pipelines write through one handle; each row is a
 * single fputs so the stdio lock keeps concurrent worker rows intact. */
static FILE *g_pxc_rust_dump = NULL;

void cbm_pxc_rust_dump_open(void) {
    char dump_path[CBM_SZ_1K];
    if (g_pxc_rust_dump ||
        !cbm_safe_getenv("CBM_RUST_RESOLVE_DUMP", dump_path, sizeof(dump_path), NULL)) {
        return;
    }
    g_pxc_rust_dump = fopen(dump_path, "a");
}

void cbm_pxc_rust_dump_close(void) {
    if (g_pxc_rust_dump) {
        fclose(g_pxc_rust_dump);
        g_pxc_rust_dump = NULL;
    }
}

static void pxc_rust_dump_field(char *buf, size_t cap, size_t *len, const char *value) {
    const char *text = value ? value : "-";
    for (const char *p = text; *p && *len + 4 < cap; p++) {
        buf[(*len)++] = (*p == '\t' || *p == '\n') ? ' ' : *p;
    }
}

static void pxc_rust_dump_row(FILE *dump, const char *rel, const char *kind, uint32_t start,
                              uint32_t end, const char *a, const char *b, const char *c,
                              const char *d, float confidence, const char *e, const char *f,
                              const char *g, uint32_t callee_start, uint32_t callee_end) {
    char line[CBM_SZ_2K];
    size_t len = (size_t)snprintf(line, sizeof(line), "%s\t%s\t%u\t%u\t", kind, rel, start, end);
    if (len >= sizeof(line)) {
        return;
    }
    const char *cols[] = {a, b, c, d};
    for (size_t i = 0; i < sizeof(cols) / sizeof(cols[0]); i++) {
        pxc_rust_dump_field(line, sizeof(line), &len, cols[i]);
        line[len++] = '\t';
    }
    len += (size_t)snprintf(line + len, sizeof(line) - len, "%.2f\t", (double)confidence);
    if (len + 4 >= sizeof(line)) {
        return;
    }
    const char *tail[] = {e, f, g};
    for (size_t i = 0; i < sizeof(tail) / sizeof(tail[0]); i++) {
        if (i > 0) {
            line[len++] = '\t';
        }
        pxc_rust_dump_field(line, sizeof(line), &len, tail[i]);
    }
    int range_len = snprintf(line + len, sizeof(line) - len, "\t%u\t%u", callee_start, callee_end);
    if (range_len < 0 || (size_t)range_len >= sizeof(line) - len) {
        return;
    }
    len += (size_t)range_len;
    if (len + 2 >= sizeof(line)) {
        return;
    }
    line[len++] = '\n';
    line[len] = '\0';
    fputs(line, dump);
}

static void pxc_rust_dump_rows(FILE *dump, const char *rel, const CBMFileResult *result) {
    for (int i = 0; i < result->resolved_calls.count; i++) {
        const CBMResolvedCall *r = &result->resolved_calls.items[i];
        pxc_rust_dump_row(dump, rel, "R", r->site_start_byte, r->site_end_byte, r->caller_qn,
                          r->callee_qn, r->strategy, r->reason, r->confidence,
                          r->kind == CBM_RESOLVED_INVOCATION        ? "invocation"
                          : r->kind == CBM_RESOLVED_FIELD_REFERENCE ? "field"
                                                                    : "reference",
                          r->callee_impl_key, NULL, 0, 0);
    }
}

void cbm_pxc_rust_dump_file(const char *rel, const CBMFileResult *result) {
    if (g_pxc_rust_dump && rel && result) {
        pxc_rust_dump_rows(g_pxc_rust_dump, rel, result);
    }
}

void cbm_pxc_rust_dump_call(const char *rel, const CBMCall *call, const char *source_qn,
                            const char *target_qn, const CBMResolvedCall *lsp) {
    if (!g_pxc_rust_dump || !rel || !call || !source_qn || !target_qn || !lsp) {
        return;
    }
    pxc_rust_dump_row(g_pxc_rust_dump, rel, "C", call->site_start_byte, call->site_end_byte,
                      source_qn, call->callee_name, target_qn, lsp->strategy, lsp->confidence,
                      "admitted", lsp->callee_impl_key, NULL, call->callee_start_byte,
                      call->callee_end_byte);
}

void cbm_pxc_set_rust_manifest(const CBMCargoManifest *m) {
    g_pxc_rust_manifest = m;
}

static char *pxc_rust_imports_key(const char *module_qn, const char *local_name) {
    size_t ml = strlen(module_qn);
    size_t nl = strlen(local_name);
    char *key = (char *)cbm_alloc(CBM_MEM_CLASS_HASH_TABLE, ml + nl + 2);
    if (!key) {
        return NULL;
    }
    memcpy(key, module_qn, ml);
    key[ml] = '\x1f';
    memcpy(key + ml + 1, local_name, nl + 1);
    return key;
}

void cbm_pipeline_rust_imports_add(cbm_pipeline_ctx_t *ctx, const char *rel,
                                   const CBMImport *import) {
    if (!ctx || !rel || !import || !import->local_name || !import->module_path ||
        strcmp(import->local_name, "_") == 0) {
        return;
    }
    if (!ctx->rust_imports) {
        ctx->rust_imports = cbm_ht_create(CBM_SZ_4K);
        if (!ctx->rust_imports) {
            return;
        }
    }
    char *module_qn = cbm_pipeline_fqn_module_dir(ctx->project_name, rel, false);
    if (!module_qn) {
        return;
    }
    char *key = pxc_rust_imports_key(module_qn, import->local_name);
    free(module_qn);
    if (!key) {
        return;
    }
    char *path = cbm_mem_strdup(CBM_MEM_CLASS_HASH_TABLE, import->module_path);
    if (!path) {
        cbm_free(CBM_MEM_CLASS_HASH_TABLE, key);
        return;
    }
    /* The table borrows key strings: keep the first key alive for the
     * table's lifetime and reuse it when the same local name repeats. */
    const char *stored_key = cbm_ht_get_key(ctx->rust_imports, key);
    if (stored_key) {
        cbm_free(CBM_MEM_CLASS_HASH_TABLE, key);
        key = (char *)stored_key;
    }
    char *previous = (char *)cbm_ht_set(ctx->rust_imports, key, path);
    cbm_free(CBM_MEM_CLASS_HASH_TABLE, previous);
}

typedef struct {
    void **items;
    uint32_t count;
} pxc_owned_ptrs_t;

static void pxc_rust_imports_collect_entry(const char *key, void *value, void *userdata) {
    pxc_owned_ptrs_t *owned = (pxc_owned_ptrs_t *)userdata;
    owned->items[owned->count++] = (void *)key;
    owned->items[owned->count++] = value;
}

void cbm_pipeline_rust_imports_free(cbm_pipeline_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    cbm_rust_modtree_free(ctx->rust_use_modtree);
    cbm_rust_macro_table_free(ctx->rust_use_macros);
    ctx->rust_use_modtree = NULL;
    ctx->rust_use_macros = NULL;
    if (!ctx->rust_imports) {
        return;
    }
    /* The table borrows keys, so collect every key and value first and free
     * them only after the table (and its iteration) is gone. */
    uint32_t entries = cbm_ht_count(ctx->rust_imports);
    size_t owned_bytes = ((size_t)entries * 2 + 1) * sizeof(void *);
    pxc_owned_ptrs_t owned = {.items = cbm_calloc(CBM_MEM_CLASS_HASH_TABLE, owned_bytes),
                              .count = 0};
    if (owned.items) {
        cbm_ht_foreach(ctx->rust_imports, pxc_rust_imports_collect_entry, &owned);
    }
    cbm_ht_free(ctx->rust_imports);
    ctx->rust_imports = NULL;
    for (uint32_t i = 0; owned.items && i < owned.count; i++) {
        cbm_free(CBM_MEM_CLASS_HASH_TABLE, owned.items[i]);
    }
    cbm_free(CBM_MEM_CLASS_HASH_TABLE, owned.items);
}

static const char *pxc_rust_import_lookup(void *table, const char *module_qn,
                                          const char *local_name) {
    if (!table || !module_qn || !local_name) {
        return NULL;
    }
    char *key = pxc_rust_imports_key(module_qn, local_name);
    if (!key) {
        return NULL;
    }
    const char *path = (const char *)cbm_ht_get((const CBMHashTable *)table, key);
    cbm_free(CBM_MEM_CLASS_HASH_TABLE, key);
    return path;
}

static _Thread_local CBMRustImportSource g_pxc_rust_imports = {0};

void cbm_pxc_set_rust_imports(const CBMHashTable *imports, const CBMRustModTree *modtree,
                              const CBMRustMacroTable *macros) {
    g_pxc_rust_imports.lookup = imports ? pxc_rust_import_lookup : NULL;
    g_pxc_rust_imports.ctx = (void *)imports;
    g_pxc_rust_imports.modtree = modtree;
    g_pxc_rust_imports.macros = macros;
}

const CBMRustImportSource *cbm_pxc_get_rust_import_source(void) {
    return g_pxc_rust_imports.lookup || g_pxc_rust_imports.modtree ? &g_pxc_rust_imports : NULL;
}

const struct CBMCargoManifest *cbm_pxc_get_rust_manifest(void) {
    return g_pxc_rust_manifest;
}

/* Convert a CBMLSPDef array (the pipeline's lingua franca, go_lsp.h:73)
 * into a CBMRustLSPDef array (rust_lsp.h) inside `arena`. The two structs
 * have similar fields but different layouts; CBMLSPDef adds
 * language/namespace metadata, so a memcpy is unsafe — copy field-by-field. */
static CBMRustLSPDef *pxc_lspdefs_to_rust(CBMArena *arena, const CBMLSPDef *defs, int def_count) {
    if (!defs || def_count <= 0)
        return NULL;
    CBMRustLSPDef *out =
        (CBMRustLSPDef *)cbm_arena_alloc(arena, (size_t)def_count * sizeof(CBMRustLSPDef));
    if (!out)
        return NULL;
    for (int i = 0; i < def_count; i++) {
        out[i].qualified_name = defs[i].qualified_name;
        out[i].short_name = defs[i].short_name;
        out[i].label = defs[i].label;
        out[i].receiver_type = defs[i].receiver_type;
        out[i].def_module_qn = defs[i].def_module_qn;
        out[i].return_types = defs[i].return_types;
        out[i].embedded_types = defs[i].embedded_types;
        out[i].field_defs = defs[i].field_defs;
        out[i].method_names_str = defs[i].method_names_str;
        out[i].signature_param_types = defs[i].signature_param_types;
        out[i].signature_param_count = defs[i].signature_param_count;
        out[i].trait_qn = defs[i].trait_qn;
        out[i].receiver_spelling = defs[i].receiver_spelling;
        out[i].impl_target = defs[i].impl_target;
        out[i].impl_item = defs[i].impl_item;
        out[i].decorators = defs[i].decorators;
        out[i].is_interface = defs[i].is_interface;
        out[i].is_rust_impl_relation = defs[i].is_rust_impl_relation;
        out[i].is_abstract = defs[i].is_abstract;
        out[i].is_async = defs[i].is_async;
        out[i].is_rust_private = defs[i].is_rust_private;
        out[i].has_rust_self = defs[i].has_rust_self;
        out[i].has_rust_value_self = defs[i].has_rust_value_self;
        out[i].has_rust_shared_self = defs[i].has_rust_shared_self;
        out[i].rust_impl_self_needs_proof = defs[i].rust_impl_self_needs_proof;
        out[i].rust_unbounded_impl_params = defs[i].rust_unbounded_impl_params;
        out[i].type_params = defs[i].type_params;
    }
    return out;
}

/* Run cross-file LSP for a single file inside a scratch arena that gets
 * freed when the call returns. The LSP would otherwise allocate a fresh
 * type registry + stdlib + all project defs into the supplied arena, and
 * that adds up to O(N×project_size) memory if we used cache[i]->arena
 * directly across N files (test_incremental.c saw 3.5 GB peak on a
 * 1100-file repo before this fix). Output gets copied into the file's own
 * arena and merged into result->resolved_calls. */
void cbm_pxc_run_one(CBMLanguage lang, CBMFileResult *r, const char *source, int source_len,
                     const char *module_qn, CBMLSPDef *defs, int def_count, const char **imp_names,
                     const char **imp_qns, int imp_count) {
    TSTree *tree = r->cached_tree; /* may be NULL — LSP re-parses then */

    CBMArena scratch;
    pxc_scratch_take(PXC_SCRATCH_DISPATCH, &scratch);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMCallArray synthetic_calls;
    memset(&synthetic_calls, 0, sizeof(synthetic_calls));

    switch (lang) {
    case CBM_LANG_GO:
        cbm_run_go_lsp_cross(&scratch, source, source_len, module_qn, defs, def_count, imp_names,
                             imp_qns, imp_count, tree, &out);
        break;
    case CBM_LANG_C:
    case CBM_LANG_CPP:
    case CBM_LANG_CUDA: {
        bool cpp_mode = (lang != CBM_LANG_C);
        /* C/C++ cross LSP takes include_paths/include_ns_qns instead of
         * imports — the existing pipeline doesn't carry C-style include
         * resolution as a separate map, so pass NULL/0 and let the LSP
         * fall back to its own #include scan. */
        if (r->has_test_definition_owners) {
            if (!cbm_run_c_lsp_cross_with_test_owners(&scratch, source, source_len, module_qn,
                                                      cpp_mode, defs, def_count, NULL, NULL, 0,
                                                      tree, &out, CBM_SOURCE_ORIGIN_RAW, r)) {
                cbm_pipeline_test_owner_error(r);
                pxc_scratch_give(PXC_SCRATCH_DISPATCH, &scratch);
                return; /* out may be partial: never append it */
            }
        } else {
            cbm_run_c_lsp_cross(&scratch, source, source_len, module_qn, cpp_mode, defs, def_count,
                                NULL, NULL, 0, tree, &out);
        }
        break;
    }
    case CBM_LANG_PYTHON:
        cbm_run_py_lsp_cross(&scratch, source, source_len, module_qn, defs, def_count, imp_names,
                             imp_qns, imp_count, tree, &out, &synthetic_calls);
        break;
    case CBM_LANG_PHP:
        cbm_run_php_lsp_cross(&scratch, source, source_len, module_qn, defs, def_count, imp_names,
                              imp_qns, imp_count, tree, &out);
        break;
    case CBM_LANG_JAVA:
        cbm_run_java_lsp_cross(&scratch, source, source_len, module_qn, defs, def_count, imp_names,
                               imp_qns, imp_count, tree, &out);
        break;
    case CBM_LANG_KOTLIN:
        cbm_run_kotlin_lsp_cross(&scratch, source, source_len, module_qn, defs, def_count,
                                 imp_names, imp_qns, imp_count, tree, &out);
        break;
    case CBM_LANG_RUST: {
        /* The Rust resolver wants CBMRustLSPDef (rust_lsp.h), not the
         * pipeline's CBMLSPDef — the structs share their first 9 fields
         * but diverge after, so convert into the scratch arena. The
         * workspace manifest (set once by the sequential driver) lets
         * `crate_a::foo` route across the crate boundary (#56). */
        CBMRustLSPDef *rdefs = pxc_lspdefs_to_rust(&scratch, defs, def_count);
        const char *source_module_qn = r->module_qn ? r->module_qn : module_qn;
        const char *declared_module_qn = r->rust_relative_module_qn;
        const char *relative_module_qn =
            declared_module_qn ? (declared_module_qn[0] ? declared_module_qn : NULL) : module_qn;
        CBMRustImplLinkArray impl_links = {0};
        cbm_run_rust_lsp_cross_with_manifest(
            &scratch, source, source_len, source_module_qn, relative_module_qn, rdefs, def_count,
            imp_names, imp_qns, imp_count, tree, g_pxc_rust_manifest,
            cbm_pxc_get_rust_import_source(), &out, &synthetic_calls, &impl_links);
        pxc_store_rust_impl_links(r, &impl_links);
        break;
    }
    default:
        break;
    }

    pxc_append_results(&r->arena, &r->resolved_calls, &out);
    pxc_append_synthetic_calls(&r->arena, &r->calls, &synthetic_calls);
    pxc_scratch_give(PXC_SCRATCH_DISPATCH, &scratch);
}

/* Variant of cbm_pxc_run_one for TS/JS/JSX/TSX with explicit dialect
 * flags. Same scratch-arena lifecycle as cbm_pxc_run_one. */
void cbm_pxc_run_one_ts(CBMFileResult *r, const char *source, int source_len, const char *module_qn,
                        CBMLSPDef *defs, int def_count, const char **imp_names,
                        const char **imp_qns, int imp_count, bool js_mode, bool jsx_mode,
                        bool dts_mode) {
    CBMArena scratch;
    pxc_scratch_take(PXC_SCRATCH_DISPATCH, &scratch);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));

    cbm_run_ts_lsp_cross(&scratch, source, source_len, module_qn, js_mode, jsx_mode, dts_mode, defs,
                         def_count, imp_names, imp_qns, imp_count, r->cached_tree, &out);

    pxc_append_results(&r->arena, &r->resolved_calls, &out);
    pxc_scratch_give(PXC_SCRATCH_DISPATCH, &scratch);
}

/* Parse the project's root Cargo.toml (if present) into `out_m`, using
 * `marena` for the manifest's owned strings. Returns true when a manifest
 * was parsed (a workspace root or any [package]/[dependencies]); false when
 * there is no readable Cargo.toml, leaving *out_m untouched. The resulting
 * manifest feeds cross-CRATE Rust resolution (#56): its [workspace].members
 * map lets `crate_a::foo` route to the member crate's def. */
/* Per-file cross-LSP dispatch, shared by the PARALLEL resolve worker and the
 * SEQUENTIAL driver. One code path = one semantics: filter the global defs
 * down to the file's own+imported modules via the module-def index, resolve
 * through the shared prebuilt registry when the language has one (per-file
 * OVERLAY pattern — no registry build, no finalize), and only fall back to
 * the per-file registry build (with the FILTERED defs) for languages without
 * a shared-registry variant. Before this helper existed the sequential
 * driver fed the FULL def list into full per-file registry builds —
 * O(files x defs), which ground an 81k-file TS corpus for hours.
 *
 * `rust_shared_get` supplies the lazily-built shared Rust all-defs registry
 * (the parallel resolver owns its once-guard); NULL means "no shared rust
 * registry available" and rust NULL-filter files take the per-file build. */
/* Per-file registry-build cost counters (#1669). Surfaced by pass_parallel at
 * end of resolve. */
_Atomic uint64_t g_pxc_defs_registered = 0;
_Atomic uint64_t g_pxc_build_files = 0;
_Atomic uint64_t g_pxc_filter_files = 0;
_Atomic uint64_t g_pxc_filter_failed = 0;

/* Overlay registrations count as per-file registry work too: the complexity
 * gate (test_complexity.c) sums ALL defs registered per file, whichever path
 * built them. Without this the shared-registry languages would report zero and
 * the linearity gate would pass vacuously. */
void cbm_pxc_count_perfile_defs(uint64_t defs) {
    atomic_fetch_add_explicit(&g_pxc_defs_registered, defs, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_pxc_build_files, 1, memory_order_relaxed);
}

void cbm_pxc_filter_stats(uint64_t *defs_registered, uint64_t *build_files, uint64_t *filter_files,
                          uint64_t *filter_failed) {
    if (defs_registered)
        *defs_registered = atomic_load_explicit(&g_pxc_defs_registered, memory_order_relaxed);
    if (build_files)
        *build_files = atomic_load_explicit(&g_pxc_build_files, memory_order_relaxed);
    if (filter_files)
        *filter_files = atomic_load_explicit(&g_pxc_filter_files, memory_order_relaxed);
    if (filter_failed)
        *filter_failed = atomic_load_explicit(&g_pxc_filter_failed, memory_order_relaxed);
}

/* A file the extractor marked lsp_skipped (its parse alone used more than its
 * share of the per-file budget) takes no cross-file resolve either: the same
 * tree the per-file walk could not afford. One check for every language and
 * both drivers. */
void cbm_pxc_dispatch_file(CBMLanguage lang, CBMFileResult *result, const char *source,
                           int source_len, const char *rel, const char *def_module,
                           const CBMCrossLspRegistries *cross_registries,
                           const CBMModuleDefIndex *module_def_index, CBMLSPDef *all_defs,
                           int all_def_count, const char **imp_keys, const char **imp_vals,
                           int imp_count, CBMTypeRegistry *(*rust_shared_get)(void *),
                           void *rust_shared_ctx) {
    if (result && result->lsp_skipped) {
        if (result->has_test_definition_owners) {
            cbm_pipeline_test_owner_error(result);
        }
        return;
    }
    if (!result) {
        return;
    }
    bool used_prebuilt = false;
    CBMTypeRegistry *prebuilt =
        cross_registries ? cbm_pxc_registry_for_lang(cross_registries, lang) : NULL;
    if (prebuilt) {
        /* One lifecycle for every language (2026-09-13): the walk runs in a
         * scratch arena against a per-file OVERLAY registry chained to the
         * immutable shared base; whatever it registers or refines lands in
         * the overlay and dies with the file; only the resolved calls it
         * produces are copied into the result. Before this, Go/C/C#/Java/TS
         * were handed the RESULT arena (and C/C#/TS the shared base to write
         * into): the kernel proof measured that as 4.4 GB of resolve-time
         * growth in retained results plus arena pointers stored into the
         * shared registry -- a use-after-free class the moment the arena was
         * not the result arena (ASan, lsp_resolution_probe). */
        CBMArena scratch;
        pxc_scratch_take(PXC_SCRATCH_DISPATCH, &scratch);
        CBMTypeRegistry overlay;
        cbm_registry_init(&overlay, &scratch);
        overlay.fallback = prebuilt;
        CBMResolvedCallArray out = {0};
        CBMCallArray synthetic_calls = {0};
        switch (lang) {
        case CBM_LANG_GO:
            /* Tier 3 (metadata-driven): pure lookup over the Tier-1
             * lsp_unresolved entries -- no parse, no AST walk -- then the
             * AST walk against the overlay (chained to the sealed base). */
            cbm_go_fast_resolve_qualified_calls(result, prebuilt, imp_keys, imp_vals, imp_count);
            cbm_run_go_lsp_cross_with_registry(&scratch, source, source_len, def_module, &overlay,
                                               imp_keys, imp_vals, imp_count, result->cached_tree,
                                               &out);
            used_prebuilt = true;
            break;
        case CBM_LANG_PYTHON:
            cbm_run_py_lsp_cross_with_registry(&scratch, source, source_len, def_module, &overlay,
                                               imp_keys, imp_vals, imp_count, result->cached_tree,
                                               &out, &synthetic_calls);
            used_prebuilt = true;
            break;
        case CBM_LANG_C:
        case CBM_LANG_CPP:
        case CBM_LANG_CUDA:
            if (result->has_test_definition_owners) {
                if (!cbm_run_c_lsp_cross_with_registry_with_test_owners(
                        &scratch, source, source_len, def_module, (lang != CBM_LANG_C), &overlay,
                        imp_keys, imp_vals, imp_count, result->cached_tree, &out,
                        CBM_SOURCE_ORIGIN_RAW, result)) {
                    cbm_pipeline_test_owner_error(result);
                    pxc_scratch_give(PXC_SCRATCH_DISPATCH, &scratch);
                    return; /* no append and no fallback on an invalid proof */
                }
            } else {
                cbm_run_c_lsp_cross_with_registry(&scratch, source, source_len, def_module,
                                                  (lang != CBM_LANG_C), &overlay, imp_keys,
                                                  imp_vals, imp_count, result->cached_tree, &out);
            }
            used_prebuilt = true;
            break;
        case CBM_LANG_CSHARP:
            cbm_run_cs_lsp_cross_with_registry(&scratch, source, source_len, def_module, &overlay,
                                               imp_vals, imp_count, result->cached_tree, &out);
            used_prebuilt = true;
            break;
        case CBM_LANG_JAVA:
            /* Java builds its own-module overlay on top of the one it is
             * handed; both live in scratch now. */
            cbm_run_java_lsp_cross_with_registry(&scratch, result, source, source_len, def_module,
                                                 &overlay, imp_keys, imp_vals, imp_count,
                                                 result->cached_tree, &out);
            used_prebuilt = true;
            break;
        case CBM_LANG_JAVASCRIPT:
        case CBM_LANG_TYPESCRIPT:
        case CBM_LANG_TSX: {
            bool js;
            bool jsx;
            bool dts;
            cbm_pxc_ts_modes(lang, rel, &js, &jsx, &dts);
            CBMLSPDef *ts_defs = all_defs;
            int ts_def_count = all_def_count;
            CBMLSPDef *ts_filtered = NULL;
            if (module_def_index) {
                int fc = 0;
                bool filter_succeeded = false;
                ts_filtered = cbm_pxc_filter_defs_for_file(
                    module_def_index, all_defs, lang, result->namespace_name, def_module, imp_vals,
                    imp_count, &fc, &filter_succeeded);
                if (filter_succeeded) {
                    ts_defs = ts_filtered;
                    ts_def_count = fc;
                }
            }
            cbm_run_ts_lsp_cross_with_registry(&scratch, source, source_len, def_module, js, jsx,
                                               dts, &overlay, ts_defs, ts_def_count, imp_keys,
                                               imp_vals, imp_count, result->cached_tree, &out);
            free(ts_filtered);
            used_prebuilt = true;
            break;
        }
        /* PHP falls through to the per-file build path below until its
         * overlay variant lands. */
        default:
            break;
        }
        if (used_prebuilt) {
            pxc_append_results(&result->arena, &result->resolved_calls, &out);
            pxc_append_synthetic_calls(&result->arena, &result->calls, &synthetic_calls);
        }
        pxc_scratch_give(PXC_SCRATCH_DISPATCH, &scratch);
    }

    if (used_prebuilt) {
        return;
    }
    /* Fallback: gopls per-file filter + per-file registry build. RUST is
     * exempt from the module filter: its resolution is Cargo-manifest-aware
     * and a `crate_a::foo` reference routes to defs in ANOTHER workspace
     * crate — a module that is in neither own_module nor the import map, so
     * the filter starves cross-crate resolution (#56 repro red). Rust
     * therefore always resolves against the FULL def universe: the lazily
     * built shared registry when available, else a full per-file build. */
    CBMLSPDef *filtered = NULL;
    CBMLSPDef *file_defs = all_defs;
    int file_def_count = all_def_count;
    if (module_def_index && lang != CBM_LANG_RUST) {
        int filtered_count = 0;
        bool filter_succeeded = false;
        filtered = cbm_pxc_filter_defs_for_file(module_def_index, all_defs, lang,
                                                result->namespace_name, def_module, imp_vals,
                                                imp_count, &filtered_count, &filter_succeeded);
        if (filter_succeeded) {
            file_defs = filtered;
            file_def_count = filtered_count;
        }
        atomic_fetch_add_explicit(&g_pxc_filter_files, 1, memory_order_relaxed);
        if (!filter_succeeded) {
            atomic_fetch_add_explicit(&g_pxc_filter_failed, 1, memory_order_relaxed);
        }
    }
    /* Per-file registry build cost is driven by THIS number. If it tracks the
     * corpus instead of the file's own module + imports, cross-file LSP is
     * O(files x corpus_defs) — see #1669. */
    atomic_fetch_add_explicit(&g_pxc_defs_registered, (uint64_t)file_def_count,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(&g_pxc_build_files, 1, memory_order_relaxed);
    if (lang == CBM_LANG_RUST) {
        CBMTypeRegistry *shared = rust_shared_get ? rust_shared_get(rust_shared_ctx) : NULL;
        if (shared) {
            CBMArena scratch;
            pxc_scratch_take(PXC_SCRATCH_DISPATCH, &scratch);
            CBMResolvedCallArray out = {0};
            CBMCallArray synthetic_calls = {0};
            CBMRustImplLinkArray impl_links = {0};
            const char *source_module_qn = result->module_qn ? result->module_qn : def_module;
            const char *declared_module_qn = result->rust_relative_module_qn;
            const char *relative_module_qn =
                declared_module_qn ? (declared_module_qn[0] ? declared_module_qn : NULL)
                                   : def_module;
            cbm_run_rust_lsp_cross_with_registry(
                &scratch, source, source_len, source_module_qn, relative_module_qn, shared,
                imp_keys, imp_vals, imp_count, result->cached_tree, cbm_pxc_get_rust_manifest(),
                cbm_pxc_get_rust_import_source(), &out, &synthetic_calls, &impl_links);
            pxc_append_results(&result->arena, &result->resolved_calls, &out);
            pxc_store_rust_impl_links(result, &impl_links);
            pxc_append_synthetic_calls(&result->arena, &result->calls, &synthetic_calls);
            pxc_scratch_give(PXC_SCRATCH_DISPATCH, &scratch);
        } else {
            cbm_pxc_run_one(lang, result, source, source_len, def_module, file_defs, file_def_count,
                            imp_keys, imp_vals, imp_count);
        }
    } else if (lang == CBM_LANG_JAVASCRIPT || lang == CBM_LANG_TYPESCRIPT || lang == CBM_LANG_TSX) {
        bool js;
        bool jsx;
        bool dts;
        cbm_pxc_ts_modes(lang, rel, &js, &jsx, &dts);
        cbm_pxc_run_one_ts(result, source, source_len, def_module, file_defs, file_def_count,
                           imp_keys, imp_vals, imp_count, js, jsx, dts);
    } else {
        cbm_pxc_run_one(lang, result, source, source_len, def_module, file_defs, file_def_count,
                        imp_keys, imp_vals, imp_count);
    }
    free(filtered);
}

static bool pxc_cargo_path_prefix(const char *path, const char *prefix) {
    while (strncmp(prefix, "./", 2) == 0)
        prefix += 2;
    size_t len = strlen(prefix);
    return len == 0 || (strncmp(path, prefix, len) == 0 && path[len] == '/');
}

static const char *pxc_cargo_package_rel(const CBMCargoManifest *workspace,
                                         const char *package_path, const char *rel_path) {
    const char *owner = NULL;
    size_t owner_len = 0;
    for (int i = 0; i < workspace->member_count; i++) {
        const char *candidate = workspace->members[i].member_path;
        if (!candidate)
            continue;
        while (strncmp(candidate, "./", 2) == 0)
            candidate += 2;
        size_t len = strlen(candidate);
        if (len > owner_len && pxc_cargo_path_prefix(rel_path, candidate)) {
            owner = candidate;
            owner_len = len;
        }
    }
    while (package_path && strncmp(package_path, "./", 2) == 0)
        package_path += 2;
    if (owner) {
        if (!package_path || strlen(package_path) != owner_len ||
            strncmp(package_path, owner, owner_len) != 0)
            return NULL;
        return rel_path + owner_len + 1;
    }
    return package_path && package_path[0] ? NULL : rel_path;
}

static bool pxc_cargo_named_default(const char *rel, const char *prefix, CBMCargoTargetKind kind,
                                    CBMCargoTargetKind *out_kind, char *name, size_t name_size) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(rel, prefix, prefix_len) != 0)
        return false;
    const char *tail = rel + prefix_len;
    const char *slash = strchr(tail, '/');
    size_t name_len = 0;
    if (!slash) {
        size_t len = strlen(tail);
        if (len <= 3 || strcmp(tail + len - 3, ".rs") != 0)
            return false;
        name_len = len - 3;
    } else if (strcmp(slash, "/main.rs") == 0) {
        name_len = (size_t)(slash - tail);
    }
    if (name_len == 0 || name_len >= name_size)
        return false;
    memcpy(name, tail, name_len);
    name[name_len] = '\0';
    *out_kind = kind;
    return true;
}

static bool pxc_cargo_default_target(const char *rel, const CBMCargoManifest *manifest,
                                     CBMCargoTargetKind *kind, char *name, size_t name_size) {
    if (strcmp(rel, "src/lib.rs") == 0) {
        *kind = CBM_CARGO_TARGET_LIB;
        const char *lib_name = manifest->lib_name ? manifest->lib_name : manifest->package_name;
        snprintf(name, name_size, "%s", lib_name ? lib_name : "");
        return true;
    }
    if (strcmp(rel, "src/main.rs") == 0) {
        *kind = CBM_CARGO_TARGET_BIN;
        snprintf(name, name_size, "%s", manifest->package_name ? manifest->package_name : "");
        return true;
    }
    return pxc_cargo_named_default(rel, "src/bin/", CBM_CARGO_TARGET_BIN, kind, name, name_size) ||
           pxc_cargo_named_default(rel, "tests/", CBM_CARGO_TARGET_TEST, kind, name, name_size) ||
           pxc_cargo_named_default(rel, "examples/", CBM_CARGO_TARGET_EXAMPLE, kind, name,
                                   name_size) ||
           pxc_cargo_named_default(rel, "benches/", CBM_CARGO_TARGET_BENCH, kind, name, name_size);
}

static void pxc_cargo_discover_targets(CBMArena *arena, const CBMCargoManifest *workspace,
                                       CBMCargoManifest *manifest, const char *package_path,
                                       const cbm_file_info_t *files, int file_count,
                                       const char *inherited_edition) {
    for (int i = 0; i < file_count; i++) {
        if (files[i].language != CBM_LANG_RUST || !files[i].rel_path)
            continue;
        const char *rel = pxc_cargo_package_rel(workspace, package_path, files[i].rel_path);
        CBMCargoTargetKind kind;
        char name[CBM_SZ_1K];
        if (!rel || !pxc_cargo_default_target(rel, manifest, &kind, name, sizeof(name)))
            continue;
        cbm_cargo_record_discovered_target(arena, manifest, kind, name, rel, inherited_edition);
    }
}

bool cbm_pxc_build_rust_manifest(const cbm_pipeline_ctx_t *ctx, CBMArena *marena,
                                 CBMCargoManifest *out_m, const cbm_file_info_t *files,
                                 int file_count) {
    if (!ctx || !ctx->repo_path || !marena || !out_m)
        return false;
    /* Target roots are discovered from the project's files: a partial
     * re-resolve must still see the src/lib.rs of a crate it does not
     * re-parse, or every call into that crate loses its root. */
    if (ctx->project_files) {
        files = ctx->project_files;
        file_count = ctx->project_file_count;
    }
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/Cargo.toml", ctx->repo_path);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return false;
    int toml_len = 0;
    char *toml = pxc_read_file(path, &toml_len);
    if (!toml || toml_len <= 0) {
        cbm_free(CBM_MEM_CLASS_EXTRACT, toml);
        return false;
    }
    memset(out_m, 0, sizeof(*out_m));
    cbm_cargo_parse(marena, toml, toml_len, out_m);
    cbm_free(CBM_MEM_CLASS_EXTRACT, toml); /* cargo parser copies into marena */
    pxc_cargo_discover_targets(marena, out_m, out_m, "", files, file_count,
                               out_m->workspace_edition);
    /* Rust code names a member by its package (or lib) name, which need not
     * equal its directory: read each member manifest so `codex_core::x` can
     * route to `core/`. A missing or unreadable member manifest leaves the
     * directory name as the only key. */
    for (int i = 0; i < out_m->member_count; i++) {
        CBMCargoMember *member = &out_m->members[i];
        if (!member->member_path) {
            continue;
        }
        n = snprintf(path, sizeof(path), "%s/%s/Cargo.toml", ctx->repo_path, member->member_path);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        int member_len = 0;
        char *member_toml = pxc_read_file(path, &member_len);
        if (!member_toml || member_len <= 0) {
            cbm_free(CBM_MEM_CLASS_EXTRACT, member_toml);
            continue;
        }
        CBMCargoManifest member_manifest;
        cbm_cargo_parse(marena, member_toml, member_len, &member_manifest);
        cbm_free(CBM_MEM_CLASS_EXTRACT, member_toml);
        pxc_cargo_discover_targets(marena, out_m, &member_manifest, member->member_path, files,
                                   file_count, out_m->workspace_edition);
        cbm_cargo_set_member_names(member, member_manifest.package_name, member_manifest.lib_name,
                                   member_manifest.targets, member_manifest.target_count);
    }
    return true;
}

/* ── Rust crate graph → module tree ──────────────────────────────────
 * Every package Cargo can build from this repository — the root package,
 * workspace members and the path dependencies they reach — contributes its
 * targets as crate roots; each crate's extern prelude holds its declared
 * dependencies (a path dependency names the in-project library crate) and,
 * for non-library targets, the package's own library. */

typedef struct {
    const char *dir; /* package directory relative to the repository root, "" for the root */
    CBMCargoManifest *manifest;
    int lib_crate;
    int first_crate; /* targets are crates [first_crate, first_crate + crate_count) */
    int crate_count;
} PXCRustPackage;

typedef struct {
    CBMArena *arena;
    const cbm_pipeline_ctx_t *ctx;
    const cbm_file_info_t *files;
    int file_count;
    PXCRustPackage *packages;
    int package_count;
    int package_cap;
    const CBMCargoManifest *root;
} PXCRustCrates;

/* Join a package-relative path onto a repository-relative directory,
 * folding `.` and `..` segments. */
static const char *pxc_rel_join(CBMArena *arena, const char *dir, const char *rel) {
    while (strncmp(rel, "./", 2) == 0) {
        rel += 2;
    }
    char *out = dir[0] ? cbm_arena_sprintf(arena, "%s/%s", dir, rel) : cbm_arena_strdup(arena, rel);
    size_t used = 0;
    for (char *p = out; *p;) {
        char *end = strchr(p, '/');
        size_t seg = end ? (size_t)(end - p) : strlen(p);
        if (seg == 2 && p[0] == '.' && p[1] == '.') {
            while (used > 0 && out[used - 1] != '/') {
                used--;
            }
            if (used > 0) {
                used--;
            }
        } else if (seg > 0 && !(seg == 1 && p[0] == '.')) {
            if (used > 0) {
                out[used++] = '/';
            }
            memmove(out + used, p, seg);
            used += seg;
        }
        p = end ? end + 1 : p + seg;
    }
    out[used] = '\0';
    return out;
}

static int pxc_rust_package_index(const PXCRustCrates *crates, const char *dir) {
    for (int i = 0; i < crates->package_count; i++) {
        if (strcmp(crates->packages[i].dir, dir) == 0) {
            return i;
        }
    }
    return -1;
}

/* Parse `<dir>/Cargo.toml` and append it as a package; -1 when absent or
 * not a package manifest. */
static int pxc_rust_add_package(PXCRustCrates *crates, const char *dir) {
    int existing = pxc_rust_package_index(crates, dir);
    if (existing >= 0) {
        return existing;
    }
    char path[CBM_SZ_4K];
    int n = snprintf(path, sizeof(path), "%s%s%s/Cargo.toml", crates->ctx->repo_path,
                     dir[0] ? "/" : "", dir);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        return -1;
    }
    int len = 0;
    char *toml = pxc_read_file(path, &len);
    if (!toml) {
        return -1;
    }
    CBMCargoManifest *manifest = cbm_arena_calloc(crates->arena, sizeof(*manifest));
    if (manifest) {
        cbm_cargo_parse(crates->arena, toml, len, manifest);
    }
    cbm_free(CBM_MEM_CLASS_EXTRACT, toml);
    if (!manifest || !manifest->package_name) {
        return -1;
    }
    if (crates->package_count >= crates->package_cap) {
        int cap = crates->package_cap ? crates->package_cap * 2 : 64;
        PXCRustPackage *grown =
            cbm_realloc(CBM_MEM_CLASS_OTHER, crates->packages, (size_t)cap * sizeof(*grown));
        if (!grown) {
            return -1;
        }
        crates->packages = grown;
        crates->package_cap = cap;
    }
    crates->packages[crates->package_count] =
        (PXCRustPackage){.dir = cbm_arena_strdup(crates->arena, dir),
                         .manifest = manifest,
                         .lib_crate = -1,
                         .first_crate = -1};
    return crates->package_count++;
}

/* The directory a dependency of `package` points at: its own `path`, or the
 * workspace dependency of the same key. NULL for registry dependencies. */
static const char *pxc_rust_dep_dir(const PXCRustCrates *crates, const PXCRustPackage *package,
                                    const CBMCargoDep *dep) {
    if (dep->path) {
        return pxc_rel_join(crates->arena, package->dir, dep->path);
    }
    for (int i = 0; crates->root && dep->name && i < crates->root->dep_count; i++) {
        const CBMCargoDep *ws = &crates->root->deps[i];
        if (ws->path && ws->name && strcmp(ws->name, dep->name) == 0) {
            return pxc_rel_join(crates->arena, "", ws->path);
        }
    }
    return NULL;
}

/* A `members` glob ending in a star: every directory directly under its
 * prefix that holds a package manifest and Rust sources. */
static void pxc_rust_add_member_glob(PXCRustCrates *crates, const char *member) {
    const char *prefix = pxc_rel_join(crates->arena, "", member);
    size_t plen = strlen(prefix) - 1; /* keeps the '/' before '*' */
    for (int f = 0; f < crates->file_count; f++) {
        const char *rel = crates->files[f].rel_path;
        if (!rel || strncmp(rel, prefix, plen) != 0) {
            continue;
        }
        const char *slash = strchr(rel + plen, '/');
        if (slash) {
            (void)pxc_rust_add_package(
                crates, cbm_arena_strndup(crates->arena, rel, (size_t)(slash - rel)));
        }
    }
}

/* The root package, workspace members and, transitively, path dependencies. */
static void pxc_rust_collect_packages(PXCRustCrates *crates) {
    (void)pxc_rust_add_package(crates, "");
    for (int i = 0; crates->root && i < crates->root->member_count; i++) {
        const char *member = crates->root->members[i].member_path;
        size_t len = member ? strlen(member) : 0;
        if (len >= 2 && strcmp(member + len - 2, "/*") == 0) {
            pxc_rust_add_member_glob(crates, member);
        } else if (member) {
            (void)pxc_rust_add_package(crates, pxc_rel_join(crates->arena, "", member));
        }
    }
    for (int p = 0; p < crates->package_count; p++) {
        for (int d = 0; d < crates->packages[p].manifest->dep_count; d++) {
            const char *dir = pxc_rust_dep_dir(crates, &crates->packages[p],
                                               &crates->packages[p].manifest->deps[d]);
            if (dir) {
                (void)pxc_rust_add_package(crates, dir);
            }
        }
    }
}

/* Cargo exposes package `a-b` to Rust code as `a_b`. */
static char *pxc_rust_crate_name(CBMArena *arena, const char *name) {
    char *out = cbm_arena_strdup(arena, name);
    for (char *p = out; p && *p; p++) {
        if (*p == '-') {
            *p = '_';
        }
    }
    return out;
}

static void pxc_rust_add_package_crates(PXCRustCrates *crates, PXCRustPackage *package,
                                        const CBMCargoManifest *owners, const char *inherited,
                                        CBMRustModTree *tree) {
    CBMCargoManifest *m = package->manifest;
    pxc_cargo_discover_targets(crates->arena, owners, m, package->dir, crates->files,
                               crates->file_count, inherited);
    const char *edition = cbm_cargo_effective_edition(m, inherited);
    CBMRustEdition tree_edition = !edition                       ? CBM_RUST_EDITION_UNKNOWN
                                  : strcmp(edition, "2015") == 0 ? CBM_RUST_EDITION_2015
                                                                 : CBM_RUST_EDITION_MODERN;
    for (int t = 0; t < m->target_count; t++) {
        const CBMCargoTarget *target = &m->targets[t];
        if (!target->path) {
            continue;
        }
        bool lib = target->kind == CBM_CARGO_TARGET_LIB;
        const char *name =
            lib ? pxc_rust_crate_name(crates->arena, m->lib_name ? m->lib_name : m->package_name)
                : NULL;
        int id = cbm_rust_modtree_add_crate(
            tree, pxc_rel_join(crates->arena, package->dir, target->path), name, tree_edition);
        if (id < 0) {
            continue;
        }
        if (lib) {
            package->lib_crate = id;
        }
        if (package->first_crate < 0) {
            package->first_crate = id;
        }
        package->crate_count = id - package->first_crate + 1;
    }
}

static void pxc_rust_add_package_externs(PXCRustCrates *crates, const PXCRustPackage *package,
                                         CBMRustModTree *tree) {
    const CBMCargoManifest *m = package->manifest;
    const char *own =
        pxc_rust_crate_name(crates->arena, m->lib_name ? m->lib_name : m->package_name);
    int end = package->first_crate + package->crate_count;
    for (int c = package->first_crate; c >= 0 && c < end; c++) {
        if (c != package->lib_crate && package->lib_crate >= 0) {
            cbm_rust_modtree_add_extern(tree, c, own, package->lib_crate);
        }
        for (int d = 0; d < m->dep_count; d++) {
            const CBMCargoDep *dep = &m->deps[d];
            const char *dir = pxc_rust_dep_dir(crates, package, dep);
            int target = dir ? pxc_rust_package_index(crates, dir) : -1;
            cbm_rust_modtree_add_extern(tree, c, pxc_rust_crate_name(crates->arena, dep->name),
                                        target >= 0 ? crates->packages[target].lib_crate : -1);
        }
    }
}

/* Register every target of every package as a crate root, then fill each
 * crate's extern prelude. Target discovery attributes a file to the
 * innermost package directory holding it. */
static void pxc_rust_add_crates(PXCRustCrates *crates, CBMRustModTree *tree) {
    CBMCargoManifest *owners = cbm_arena_calloc(crates->arena, sizeof(*owners));
    if (!owners) {
        return;
    }
    for (int p = 0; p < crates->package_count && p < CBM_CARGO_MAX_MEMBERS; p++) {
        owners->members[owners->member_count++].member_path = crates->packages[p].dir;
    }
    const char *inherited = crates->root ? crates->root->workspace_edition : NULL;
    for (int p = 0; p < crates->package_count; p++) {
        pxc_rust_add_package_crates(crates, &crates->packages[p], owners, inherited, tree);
    }
    for (int p = 0; p < crates->package_count; p++) {
        pxc_rust_add_package_externs(crates, &crates->packages[p], tree);
    }
}

typedef struct {
    const cbm_file_info_t *files;
    const int *file_index;      /* modtree file id -> files[] index */
    CBMRustModFacts **facts;    /* modtree file id -> scanned facts */
    CBMRustMacroFacts **macros; /* modtree file id -> file-level macro facts */
} PXCRustScanCtx;

/* cbm_parallel_for body: read and scan one registered file. */
static void pxc_rust_scan_file(int file_id, void *arg) {
    PXCRustScanCtx *scan = arg;
    int len = 0;
    char *source = pxc_read_file(scan->files[scan->file_index[file_id]].path, &len);
    if (source) {
        scan->facts[file_id] = cbm_rust_modfacts_scan(source, len);
        scan->macros[file_id] = cbm_rust_macro_facts_scan(source, len);
        cbm_free(CBM_MEM_CLASS_EXTRACT, source);
    }
}

/* Read and parse every registered file across worker threads (parsing is
 * the tree's whole cost), then build the tree from the facts in one thread,
 * so the result does not depend on scheduling. Files that define file-level
 * macros enter `macros`, keyed by their graph module QN. */
static void pxc_rust_finish_modtree(CBMRustModTree *tree, CBMRustMacroTable *macros,
                                    const char *project_name, const cbm_file_info_t *files,
                                    const int *file_index) {
    int count = cbm_rust_modtree_file_count(tree);
    size_t slots = (size_t)(count > 0 ? count : 1);
    CBMRustModFacts **facts = cbm_calloc(CBM_MEM_CLASS_OTHER, slots * sizeof(*facts));
    CBMRustMacroFacts **macro_facts = cbm_calloc(CBM_MEM_CLASS_OTHER, slots * sizeof(*macro_facts));
    if (!facts || !macro_facts) {
        cbm_free(CBM_MEM_CLASS_OTHER, facts);
        cbm_free(CBM_MEM_CLASS_OTHER, macro_facts);
        return;
    }
    PXCRustScanCtx scan = {
        .files = files, .file_index = file_index, .facts = facts, .macros = macro_facts};
    cbm_parallel_for(count, pxc_rust_scan_file, &scan, (cbm_parallel_for_opts_t){0});
    cbm_rust_modtree_finish(tree, (const CBMRustModFacts *const *)facts);
    for (int i = 0; i < count; i++) {
        cbm_rust_modfacts_free(facts[i]);
        char *module =
            macros && macro_facts[i]
                ? cbm_pipeline_fqn_module_dir(project_name, files[file_index[i]].rel_path,
                                              pxc_module_is_dir(files[file_index[i]].language))
                : NULL;
        if (module) {
            cbm_rust_macro_table_add(macros, module, macro_facts[i]); /* takes the facts */
            cbm_free(CBM_MEM_CLASS_OTHER, module);
        } else {
            cbm_rust_macro_facts_free(macro_facts[i]);
        }
    }
    cbm_free(CBM_MEM_CLASS_OTHER, facts);
    cbm_free(CBM_MEM_CLASS_OTHER, macro_facts);
}

CBMRustModTree *cbm_pxc_build_rust_modtree(const cbm_pipeline_ctx_t *ctx,
                                           const CBMCargoManifest *root,
                                           const cbm_file_info_t *files, int file_count,
                                           CBMRustMacroTable **out_macros) {
    if (out_macros) {
        *out_macros = NULL;
    }
    /* The tree is a fact of the whole project: a partial re-resolve builds
     * it from every discovered file, read from disk, as a full build does. */
    if (ctx && ctx->project_files) {
        files = ctx->project_files;
        file_count = ctx->project_file_count;
    }
    if (!ctx || !ctx->repo_path || !files || file_count <= 0) {
        return NULL;
    }
    CBMRustModTree *tree = cbm_rust_modtree_new();
    int *file_index = cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)file_count * sizeof(int));
    if (!tree || !file_index) {
        cbm_rust_modtree_free(tree);
        cbm_free(CBM_MEM_CLASS_OTHER, file_index);
        return NULL;
    }
    for (int i = 0; i < file_count; i++) {
        if (files[i].language != CBM_LANG_RUST || !files[i].rel_path) {
            continue;
        }
        char *module = cbm_pipeline_fqn_module_dir(ctx->project_name, files[i].rel_path,
                                                   pxc_module_is_dir(files[i].language));
        int id = module ? cbm_rust_modtree_add_file(tree, files[i].rel_path, module) : -1;
        free(module);
        if (id >= 0) {
            file_index[id] = i;
        }
    }
    CBMArena arena;
    cbm_arena_init(&arena);
    PXCRustCrates crates = {
        .arena = &arena, .ctx = ctx, .files = files, .file_count = file_count, .root = root};
    pxc_rust_collect_packages(&crates);
    pxc_rust_add_crates(&crates, tree);
    CBMRustMacroTable *macros = out_macros ? cbm_rust_macro_table_new() : NULL;
    pxc_rust_finish_modtree(tree, macros, ctx->project_name, files, file_index);
    if (out_macros) {
        *out_macros = macros;
    }
    cbm_free(CBM_MEM_CLASS_OTHER, crates.packages);
    cbm_arena_destroy(&arena);
    cbm_free(CBM_MEM_CLASS_OTHER, file_index);
    return tree;
}

void cbm_pipeline_rust_modtree_prepare(cbm_pipeline_ctx_t *ctx, const cbm_file_info_t *files,
                                       int file_count) {
    if (!ctx || !files || ctx->rust_use_modtree) {
        return;
    }
    bool have_rust = false;
    for (int i = 0; i < file_count && !have_rust; i++) {
        have_rust = files[i].language == CBM_LANG_RUST;
    }
    if (!have_rust) {
        return;
    }
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMCargoManifest manifest;
    bool have_manifest = cbm_pxc_build_rust_manifest(ctx, &arena, &manifest, files, file_count);
    cbm_rust_macro_table_free(ctx->rust_use_macros);
    ctx->rust_use_macros = NULL;
    ctx->rust_use_modtree = cbm_pxc_build_rust_modtree(ctx, have_manifest ? &manifest : NULL, files,
                                                       file_count, &ctx->rust_use_macros);
    cbm_arena_destroy(&arena);
}

CBMRustModTree *cbm_pipeline_rust_modtree_take(cbm_pipeline_ctx_t *ctx,
                                               const CBMCargoManifest *root,
                                               const cbm_file_info_t *files, int file_count,
                                               CBMRustMacroTable **out_macros) {
    if (!ctx || !ctx->rust_use_modtree) {
        return cbm_pxc_build_rust_modtree(ctx, root, files, file_count, out_macros);
    }
    CBMRustModTree *tree = ctx->rust_use_modtree;
    ctx->rust_use_modtree = NULL;
    if (out_macros) {
        *out_macros = ctx->rust_use_macros;
    } else {
        cbm_rust_macro_table_free(ctx->rust_use_macros);
    }
    ctx->rust_use_macros = NULL;
    return tree;
}

int cbm_pipeline_pass_lsp_cross(cbm_pipeline_ctx_t *ctx, const cbm_file_info_t *files,
                                int file_count, CBMFileResult **cache) {
    if (!ctx) {
        return 0;
    }
    if (!files || file_count <= 0 || !cache) {
        if (atomic_load(&ctx->test_definition_owners_seen)) {
            atomic_store(&ctx->test_declarations_failed, 1);
            return CBM_PIPELINE_ABORT_PRESERVE_DB;
        }
        return 0;
    }

    cbm_log_info("pass.start", "pass", "lsp_cross", "files", itoa_buf(file_count));

    /* Build the Rust workspace manifest once (only when the project has at
     * least one Rust file, to avoid an unconditional Cargo.toml read).
     * The manifest's strings live in `cargo_arena`; the resolver borrows
     * the pointer through the file-static set below. */
    bool have_rust = false;
    for (int i = 0; i < file_count; i++) {
        if (!cbm_pipeline_test_result_ok(ctx, cache[i])) {
            return CBM_PIPELINE_ABORT_PRESERVE_DB;
        }
        if (cache[i] && files[i].language == CBM_LANG_RUST) {
            have_rust = true;
        }
    }
    bool owner_walk_required = atomic_load(&ctx->test_definition_owners_seen) != 0;
    char cross_disabled[CBM_SZ_16];
    if (owner_walk_required && cbm_safe_getenv("CBM_DISABLE_LSP_CROSS", cross_disabled,
                                               sizeof(cross_disabled), NULL) != NULL) {
        atomic_store(&ctx->test_declarations_failed, 1);
        return CBM_PIPELINE_ABORT_PRESERVE_DB;
    }
    CBMArena cargo_arena;
    CBMCargoManifest cargo_manifest;
    bool have_manifest = false;
    if (have_rust) {
        cbm_arena_init(&cargo_arena);
        have_manifest =
            cbm_pxc_build_rust_manifest(ctx, &cargo_arena, &cargo_manifest, files, file_count);
        cbm_pxc_set_rust_manifest(have_manifest ? &cargo_manifest : NULL);
        cbm_pxc_set_rust_imports(ctx->rust_imports, NULL, NULL);
    }

    /* Per-file module QN cache so we don't recompute it once per def + once
     * per call. cbm_pipeline_fqn_module mallocs; freed at end. */
    char **def_modules = cbm_pipeline_test_force_def_modules_null(ctx)
                             ? NULL
                             : (char **)calloc((size_t)file_count, sizeof(char *));
    if (!def_modules) {
        cbm_log_error("pass.err", "pass", "lsp_cross", "phase", "alloc");
        if (have_rust) {
            cbm_pxc_set_rust_manifest(NULL);
            cbm_arena_destroy(&cargo_arena);
        }
        if (owner_walk_required) {
            atomic_store(&ctx->test_declarations_failed, 1);
            return CBM_PIPELINE_ABORT_PRESERVE_DB;
        }
        return 0;
    }

    int def_count = 0;
    int *def_starts =
        (int *)cbm_calloc(CBM_MEM_CLASS_EXTRACT, ((size_t)file_count + 1) * sizeof(int));
    /* The defs own their strings in the caller-owned seq_cross_arena, which
     * the registries and later passes already borrow from (see below). */
    if (!ctx->seq_cross_arena_live) {
        cbm_arena_init(&ctx->seq_cross_arena);
        ctx->seq_cross_arena_live = true;
    }
    CBMLSPDef *all_defs =
        cbm_pxc_collect_all_defs(ctx, &ctx->seq_cross_arena, cache, files, file_count,
                                 ctx->project_name, def_modules, &def_count, def_starts);
    if (def_count < 0 || (owner_walk_required && (!all_defs || def_count <= 0))) {
        atomic_store(&ctx->test_declarations_failed, 1);
        cbm_free(CBM_MEM_CLASS_EXTRACT, def_starts);
        free(all_defs);
        for (int i = 0; i < file_count; i++) {
            free(def_modules[i]);
        }
        free(def_modules);
        if (have_rust) {
            cbm_pxc_set_rust_manifest(NULL);
            cbm_arena_destroy(&cargo_arena);
        }
        return CBM_PIPELINE_ABORT_PRESERVE_DB;
    }
    CBMRustModTree *rust_modtree = NULL;
    CBMRustMacroTable *rust_macros = NULL;
    if (have_rust) {
        rust_modtree = cbm_pipeline_rust_modtree_take(ctx, have_manifest ? &cargo_manifest : NULL,
                                                      files, file_count, &rust_macros);
        cbm_pxc_set_rust_imports(ctx->rust_imports, rust_modtree, rust_macros);
    }
    /* Same seam as the parallel driver: serialize per-file surfaces while the
     * result cache is alive. Failure only degrades to a full rebuild on the
     * next incremental run. */
    if (ctx->pipeline && all_defs && def_starts) {
        cbm_lsp_surface_row_t *surface_rows = NULL;
        int surface_count = 0;
        if (cbm_lsp_surface_build_rows(ctx, ctx->project_name, cache, files, file_count, all_defs,
                                       def_starts, &surface_rows, &surface_count) == 0) {
            cbm_pipeline_set_lsp_surfaces(ctx->pipeline, surface_rows, surface_count);
        }
    }
    cbm_free(CBM_MEM_CLASS_EXTRACT, def_starts);

    /* Shared prepare (mirrors run_parallel_pipeline): inverted module-def
     * index + per-language shared registries, built ONCE for the whole pass.
     * The per-file loop below then dispatches through the SAME helper the
     * parallel resolve worker uses — previously this driver handed the FULL
     * def list to full per-file registry builds (O(files x defs); the
     * ms-typescript sequential crawl). The registries live in the
     * CALLER-OWNED ctx->seq_cross_arena: resolved_calls may borrow registry
     * strings that the later calls pass still reads, so the arena must
     * outlive this pass (run_sequential_pipeline destroys it after all
     * passes; freeing here was a pass_calls use-after-free). */
    CBMModuleDefIndex *module_def_index =
        all_defs ? cbm_pxc_build_module_def_index(all_defs, def_count) : NULL;
    CBMCrossLspRegistries cross_registries = {0};
    if (all_defs) {
        CBMArena *xa = &ctx->seq_cross_arena;
        if (!ctx->seq_cross_arena_live) {
            cbm_arena_init(xa);
            ctx->seq_cross_arena_live = true;
        }
        cross_registries.go = cbm_go_build_cross_registry(xa, all_defs, def_count);
        cross_registries.python = cbm_py_build_cross_registry(xa, all_defs, def_count);
        cross_registries.c = cbm_c_build_cross_registry(xa, all_defs, def_count);
        cross_registries.cs = cbm_cs_build_cross_registry(xa, all_defs, def_count);
        cross_registries.ts = cbm_ts_build_cross_registry(xa, all_defs, def_count);
    }

    int processed = 0;
    int skipped_no_lsp = 0;
    int skipped_no_source = 0;
    int per_lang_calls = 0;

    for (int i = 0; i < file_count; i++) {
        if (!cache[i])
            continue;
        if (!cbm_pipeline_test_result_ok(ctx, cache[i])) {
            break;
        }
        CBMLanguage lang = files[i].language;
        if (!cbm_pxc_has_cross_lsp(lang)) {
            if (cache[i]->has_test_definition_owners) {
                cbm_pipeline_test_owner_error(cache[i]);
                (void)cbm_pipeline_test_result_ok(ctx, cache[i]);
                break;
            }
            skipped_no_lsp++;
            continue;
        }

        int source_len = 0;
        char *source = pxc_read_file(files[i].path, &source_len);
        if (!source || source_len <= 0) {
            cbm_free(CBM_MEM_CLASS_EXTRACT, source);
            if (cache[i]->has_test_definition_owners) {
                cbm_pipeline_test_owner_error(cache[i]);
                (void)cbm_pipeline_test_result_ok(ctx, cache[i]);
                break;
            }
            skipped_no_source++;
            continue;
        }

        if (!def_modules[i]) {
            def_modules[i] = cbm_pipeline_fqn_module_dir(ctx->project_name, files[i].rel_path,
                                                         pxc_module_is_dir(files[i].language));
        }

        if (!def_modules[i] && cache[i]->has_test_definition_owners) {
            cbm_free(CBM_MEM_CLASS_EXTRACT, source);
            cbm_pipeline_test_owner_error(cache[i]);
            (void)cbm_pipeline_test_result_ok(ctx, cache[i]);
            break;
        }

        const char **imp_keys = NULL;
        const char **imp_vals = NULL;
        int imp_count = 0;
        cbm_pxc_build_import_map(ctx, ctx->project_name, files[i].rel_path, lang, cache[i],
                                 &imp_keys, &imp_vals, &imp_count);

        /* Journal around the resolve: a hang here must be attributed to THIS
         * file, not to a stale extraction marker (the innocent-quarantine
         * failure mode). */
        cbm_index_mark_start(files[i].rel_path);
        cbm_pxc_dispatch_file(lang, cache[i], source, source_len, files[i].rel_path, def_modules[i],
                              &cross_registries, module_def_index, all_defs, def_count, imp_keys,
                              imp_vals, imp_count, NULL, NULL);
        cbm_index_mark_done(files[i].rel_path);
        per_lang_calls++;
        processed++;

        cbm_pxc_free_import_map(imp_keys, imp_vals, imp_count);
        cbm_free(CBM_MEM_CLASS_EXTRACT, source);
        if (!cbm_pipeline_test_result_ok(ctx, cache[i])) {
            break;
        }
    }

    cbm_pxc_free_module_def_index(module_def_index);
    free(all_defs);
    /* The module-QN strings are borrowed by the shared cross registries in
     * ctx->seq_cross_arena, which deliberately outlive this pass so that
     * pass_calls can read borrowed registry strings. Freeing the strings here
     * while keeping the registries alive was a use-after-free (pass_calls
     * strcmp on a freed module QN, caught by ASan on the kernel corpus tier).
     * Ownership transfers to the ctx; released beside the arena. */
    ctx->seq_cross_def_modules = def_modules;
    ctx->seq_cross_def_module_count = file_count;

    /* Drop the borrowed manifest pointer before its arena dies, so a later
     * pass (or a stale thread-local) can never read freed manifest memory. */
    if (have_rust) {
        cbm_pxc_set_rust_manifest(NULL);
        /* The tree outlives the pass: the registry reads it while the calls
         * and usages passes bind bare names (released by the pipeline). */
        cbm_pxc_set_rust_imports(NULL, rust_modtree, NULL);
        cbm_rust_modtree_free(ctx->seq_rust_modtree);
        ctx->seq_rust_modtree = rust_modtree;
        cbm_rust_macro_table_free(rust_macros);
        cbm_arena_destroy(&cargo_arena);
    }
    (void)have_manifest;

    cbm_log_info("pass.done", "pass", "lsp_cross", "files_processed", itoa_buf(processed),
                 "files_skipped_no_lsp", itoa_buf(skipped_no_lsp), "files_skipped_no_source",
                 itoa_buf(skipped_no_source), "defs_total", itoa_buf(def_count), "lsp_calls",
                 itoa_buf(per_lang_calls));
    return atomic_load(&ctx->test_declarations_failed) ? CBM_PIPELINE_ABORT_PRESERVE_DB : 0;
}

/* ── Per-module def index (gopls "package summary" pattern) ──── */

typedef struct {
    int count;
    int cap;
    int *indices; /* malloc'd; indices into the caller's all_defs[] */
} pxc_module_entry_t;

struct CBMModuleDefIndex {
    CBMHashTable *ht;           /* module_qn → pxc_module_entry_t* */
    CBMHashTable *namespace_ht; /* declared package/namespace → pxc_module_entry_t* */
    int def_count;              /* total entries in the all_defs[] array */
};

/* cbm_ht_foreach callback: free each pxc_module_entry_t. */
static void pxc_module_entry_free_cb(const char *key, void *value, void *userdata) {
    (void)key;
    (void)userdata;
    pxc_module_entry_t *e = (pxc_module_entry_t *)value;
    if (!e)
        return;
    free(e->indices);
    free(e);
}
static pxc_module_entry_t *pxc_module_entry_get_or_create(CBMHashTable *ht, const char *key) {
    if (!ht || !key || !key[0]) {
        return NULL;
    }
    pxc_module_entry_t *e = (pxc_module_entry_t *)cbm_ht_get(ht, key);
    if (e) {
        return e;
    }
    e = (pxc_module_entry_t *)calloc(1, sizeof(*e));
    if (!e) {
        return NULL;
    }
    e->cap = 8;
    e->indices = (int *)calloc((size_t)e->cap, sizeof(*e->indices));
    if (!e->indices) {
        free(e);
        return NULL;
    }
    cbm_ht_set(ht, key, e);
    return e;
}

static void pxc_module_entry_add_index(pxc_module_entry_t *e, int index) {
    if (!e) {
        return;
    }
    if (e->count >= e->cap) {
        int new_cap = e->cap * 2;
        int *new_indices = (int *)realloc(e->indices, (size_t)new_cap * sizeof(*new_indices));
        if (!new_indices) {
            return;
        }
        e->indices = new_indices;
        e->cap = new_cap;
    }
    e->indices[e->count++] = index;
}

static bool pxc_is_jvm_lang(CBMLanguage lang);
static bool pxc_def_lang_matches(CBMLanguage caller_lang, CBMLanguage def_lang);

static int pxc_mark_entry_defs(bool *selected, const pxc_module_entry_t *e,
                               const CBMLSPDef *all_defs, CBMLanguage caller_lang) {
    if (!selected || !e) {
        return 0;
    }
    int added = 0;
    for (int j = 0; j < e->count; j++) {
        int idx = e->indices[j];
        const CBMLSPDef *def = &all_defs[idx];
        if (!pxc_def_lang_matches(caller_lang, def->lang) || selected[idx]) {
            continue;
        }
        selected[idx] = true;
        added++;
    }
    return added;
}

static bool pxc_is_jvm_lang(CBMLanguage lang) {
    return lang == CBM_LANG_JAVA || lang == CBM_LANG_KOTLIN;
}

/* Java and Kotlin files without a package declaration still share one JVM
 * default package. The hash table intentionally rejects empty keys, so use an
 * internal-only sentinel to make that package selectable across files. */
static const char *pxc_namespace_index_key(const char *namespace_name) {
    return namespace_name && namespace_name[0] ? namespace_name
                                               : "\x1f"
                                                 "cbm-jvm-default-package";
}

static bool pxc_def_lang_matches(CBMLanguage caller_lang, CBMLanguage def_lang) {
    if (pxc_is_jvm_lang(caller_lang)) {
        return pxc_is_jvm_lang(def_lang);
    }
    return true;
}

static void pxc_mark_module_defs(const CBMModuleDefIndex *idx, bool *selected,
                                 const CBMLSPDef *all_defs, CBMLanguage caller_lang,
                                 const char *module_qn, int *total) {
    if (!idx || !idx->ht || !module_qn || !module_qn[0]) {
        return;
    }
    pxc_module_entry_t *e = (pxc_module_entry_t *)cbm_ht_get(idx->ht, module_qn);
    int added = pxc_mark_entry_defs(selected, e, all_defs, caller_lang);
    if (total) {
        *total += added;
    }
}

/* Import maps bind source locals to semantic targets. A from-import therefore
 * carries a symbol QN (`project.module.Class` / `project.module.function`),
 * while the def index is keyed by the declaring file module
 * (`project.module`). Select the nearest materialized module prefix; the
 * language registry still has to prove the full target QN before an edge is
 * emitted, so this broadens the candidate set without weakening precision. */
static void pxc_mark_import_defs(const CBMModuleDefIndex *idx, bool *selected,
                                 const CBMLSPDef *all_defs, CBMLanguage caller_lang,
                                 const char *import_qn, int *total) {
    if (!idx || !idx->ht || !import_qn || !import_qn[0]) {
        return;
    }
    char *candidate = strdup(import_qn);
    if (!candidate) {
        return;
    }
    bool matched = false;
    for (;;) {
        pxc_module_entry_t *entry = (pxc_module_entry_t *)cbm_ht_get(idx->ht, candidate);
        if (entry) {
            int added = pxc_mark_entry_defs(selected, entry, all_defs, caller_lang);
            if (total) {
                *total += added;
            }
            matched = true;
            break;
        }
        char *dot = strrchr(candidate, '.');
        if (!dot) {
            break;
        }
        *dot = '\0';
    }
    /* Kotlin/Java source imports are package-qualified while file modules are
     * path-qualified. If no module prefix matched, walk the declared-package
     * index from the full import toward its package. This only selects a small
     * candidate registry; the language resolver must still prove the symbol. */
    if (!matched && pxc_is_jvm_lang(caller_lang) && idx->namespace_ht) {
        strcpy(candidate, import_qn);
        for (;;) {
            pxc_module_entry_t *entry = (pxc_module_entry_t *)cbm_ht_get(
                idx->namespace_ht, pxc_namespace_index_key(candidate));
            if (entry) {
                int added = pxc_mark_entry_defs(selected, entry, all_defs, caller_lang);
                if (total) {
                    *total += added;
                }
                break;
            }
            char *dot = strrchr(candidate, '.');
            if (!dot) {
                break;
            }
            *dot = '\0';
        }
    }
    free(candidate);
}

CBMModuleDefIndex *cbm_pxc_build_module_def_index(CBMLSPDef *all_defs, int def_count) {
    if (!all_defs || def_count <= 0) {
        return NULL;
    }

    CBMHashTable *ht = cbm_ht_create(64);
    CBMHashTable *namespace_ht = cbm_ht_create(64);
    if (!ht || !namespace_ht) {
        cbm_ht_free(ht);
        cbm_ht_free(namespace_ht);
        return NULL;
    }

    /* Single pass: index each def by file module and by declared package.
     * JVM mixed roots (`src/main/java` + `src/main/kotlin`) share the
     * declared package, not the path-derived module prefix. */
    for (int i = 0; i < def_count; i++) {
        pxc_module_entry_add_index(pxc_module_entry_get_or_create(ht, all_defs[i].def_module_qn),
                                   i);
        pxc_module_entry_add_index(
            pxc_module_entry_get_or_create(namespace_ht,
                                           pxc_namespace_index_key(all_defs[i].namespace_name)),
            i);
    }

    CBMModuleDefIndex *idx = (CBMModuleDefIndex *)calloc(1, sizeof(*idx));
    if (!idx) {
        cbm_ht_foreach(ht, pxc_module_entry_free_cb, NULL);
        cbm_ht_free(ht);
        cbm_ht_foreach(namespace_ht, pxc_module_entry_free_cb, NULL);
        cbm_ht_free(namespace_ht);
        return NULL;
    }
    idx->ht = ht;
    idx->namespace_ht = namespace_ht;
    idx->def_count = def_count;
    return idx;
}

void cbm_pxc_free_module_def_index(CBMModuleDefIndex *idx) {
    if (!idx) {
        return;
    }
    if (idx->ht) {
        cbm_ht_foreach(idx->ht, pxc_module_entry_free_cb, NULL);
        cbm_ht_free(idx->ht);
    }
    if (idx->namespace_ht) {
        cbm_ht_foreach(idx->namespace_ht, pxc_module_entry_free_cb, NULL);
        cbm_ht_free(idx->namespace_ht);
    }
    free(idx);
}

CBMLSPDef *cbm_pxc_filter_defs_for_file(const CBMModuleDefIndex *idx, CBMLSPDef *all_defs,
                                        CBMLanguage caller_lang, const char *caller_namespace,
                                        const char *own_module, const char *const *imp_qns,
                                        int imp_count, int *out_count, bool *out_success) {
    if (out_count) {
        *out_count = 0;
    }
    if (out_success) {
        *out_success = false;
    }
    if (!idx || !idx->ht || !all_defs || !out_count || !out_success || idx->def_count <= 0) {
        return NULL;
    }

    bool *selected =
        (bool *)cbm_calloc(CBM_MEM_CLASS_EXTRACT, (size_t)idx->def_count * sizeof(*selected));
    if (!selected) {
        return NULL;
    }

    int total = 0;
    pxc_mark_module_defs(idx, selected, all_defs, caller_lang, own_module, &total);
    for (int i = 0; i < imp_count; i++) {
        pxc_mark_import_defs(idx, selected, all_defs, caller_lang, imp_qns[i], &total);
    }
    if (pxc_is_jvm_lang(caller_lang) && idx->namespace_ht) {
        const char *namespace_key = pxc_namespace_index_key(caller_namespace);
        pxc_module_entry_t *e = (pxc_module_entry_t *)cbm_ht_get(idx->namespace_ht, namespace_key);
        total += pxc_mark_entry_defs(selected, e, all_defs, caller_lang);
    }

    if (total == 0) {
        cbm_free(CBM_MEM_CLASS_EXTRACT, selected);
        *out_success = true;
        return NULL;
    }

    CBMLSPDef *out = (CBMLSPDef *)malloc((size_t)total * sizeof(CBMLSPDef));
    if (!out) {
        cbm_free(CBM_MEM_CLASS_EXTRACT, selected);
        return NULL;
    }

    int n = 0;
    for (int i = 0; i < idx->def_count; i++) {
        if (selected[i]) {
            out[n++] = all_defs[i];
        }
    }
    *out_count = n;
    *out_success = true;
    cbm_free(CBM_MEM_CLASS_EXTRACT, selected);
    return out;
}
