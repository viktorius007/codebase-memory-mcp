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

#include <ctype.h>
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
    const cbm_pipeline_ctx_t *ctx;
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
    cbm_resolution_t res = cbm_registry_resolve_lang(bs->reg, raw, module_qn, bs->imp_keys,
                                                     bs->imp_vals, bs->imp_count, bs->lang);
    if (!res.qualified_name || !res.qualified_name[0]) {
        return NULL;
    }
    if (pxc_base_strategy_is_weak(res.strategy)) {
        return NULL;
    }
    if (!cbm_label_is_type_like(cbm_registry_label_of(bs->reg, res.qualified_name))) {
        return NULL;
    }
    bool import_proved = res.strategy && (strcmp(res.strategy, "import_map") == 0 ||
                                          strcmp(res.strategy, "import_map_suffix") == 0);
    const cbm_gbuf_node_t *candidate = cbm_gbuf_find_by_qn(bs->gbuf, res.qualified_name);
    if (!import_proved && cbm_external_import_binding_leaves_corpus(bs->ctx, bs->lang, bs->imports,
                                                                    raw, bs->rel, candidate)) {
        return NULL;
    }
    /* `unittest.TestCase` under an external `import unittest` survives the
     * weak-strategy veto through the same-module suffix fallback; the shared
     * external-base gate keeps it at its source spelling. */
    if (!import_proved &&
        cbm_python_external_base_contradicts(bs->lang, bs->imports, raw, res.qualified_name,
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
        const char *const *lists[] = {src->rust_type_params, src->rust_unbounded_impl_params,
                                      src->rust_impl_type_params, src->rust_type_param_defaults};
        const char ***copies[] = {&dst->type_params, &dst->rust_unbounded_impl_params,
                                  &dst->rust_impl_type_params, &dst->rust_type_param_defaults};
        for (int list = 0; list < 4; list++) {
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
    dst->impl_output = impl->output_type ? cbm_arena_strdup(arena, impl->output_type) : NULL;
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
        pxc_base_scope_t bs = {.ctx = ctx,
                               .imports = &fr->imports,
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

typedef struct {
    const char **keys;
    const char **vals;
    int count;
    int capacity;
} PXCPythonImportMap;

/* FQN helpers retain their historical libc allocation contract. Keep that
 * boundary in one place while resolver-owned buffers use mem_core. */
static void pxc_free_fqn(char *qn) {
    free(qn);
}

static void pxc_python_map_free(PXCPythonImportMap *map) {
    if (!map)
        return;
    for (int i = 0; i < map->count; i++) {
        cbm_free(CBM_MEM_CLASS_OTHER, (void *)map->keys[i]);
        cbm_free(CBM_MEM_CLASS_OTHER, (void *)map->vals[i]);
    }
    cbm_free(CBM_MEM_CLASS_OTHER, map->keys);
    cbm_free(CBM_MEM_CLASS_OTHER, map->vals);
    memset(map, 0, sizeof(*map));
}

static int pxc_python_map_find(const PXCPythonImportMap *map, const char *key) {
    for (int i = 0; map && key && i < map->count; i++) {
        if (strcmp(map->keys[i], key) == 0)
            return i;
    }
    return -1;
}

static bool pxc_python_map_reserve(PXCPythonImportMap *map) {
    if (map->count < map->capacity)
        return true;
    int capacity = map->capacity ? map->capacity * 2 : CBM_SZ_16;
    const char **keys = cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)capacity * sizeof(*keys));
    const char **vals = cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)capacity * sizeof(*vals));
    if (!keys || !vals) {
        cbm_free(CBM_MEM_CLASS_OTHER, keys);
        cbm_free(CBM_MEM_CLASS_OTHER, vals);
        return false;
    }
    if (map->count > 0) {
        memcpy(keys, map->keys, (size_t)map->count * sizeof(*keys));
        memcpy(vals, map->vals, (size_t)map->count * sizeof(*vals));
    }
    cbm_free(CBM_MEM_CLASS_OTHER, map->keys);
    cbm_free(CBM_MEM_CLASS_OTHER, map->vals);
    map->keys = keys;
    map->vals = vals;
    map->capacity = capacity;
    return true;
}

/* An empty value is a proved Python binding with no proved project identity.
 * The registry treats it as a blocker instead of falling through to a weaker
 * same-module or unique-name guess. */
static bool pxc_python_map_set(PXCPythonImportMap *map, const char *key, const char *value) {
    if (!map || !key || !key[0])
        return false;
    char *copy = cbm_mem_strdup(CBM_MEM_CLASS_OTHER, value ? value : "");
    if (!copy)
        return false;
    int found = pxc_python_map_find(map, key);
    if (found >= 0) {
        cbm_free(CBM_MEM_CLASS_OTHER, (void *)map->vals[found]);
        map->vals[found] = copy;
        return true;
    }
    if (!pxc_python_map_reserve(map)) {
        cbm_free(CBM_MEM_CLASS_OTHER, copy);
        return false;
    }
    char *key_copy = cbm_mem_strdup(CBM_MEM_CLASS_OTHER, key);
    if (!key_copy) {
        cbm_free(CBM_MEM_CLASS_OTHER, copy);
        return false;
    }
    map->keys[map->count] = key_copy;
    map->vals[map->count] = copy;
    map->count++;
    return true;
}

static void pxc_python_map_block_all(PXCPythonImportMap *map) {
    for (int i = 0; map && i < map->count; i++)
        (void)pxc_python_map_set(map, map->keys[i], NULL);
}

static bool pxc_python_name_in_list(const CBMPyNamespaceFact *fact, const char *name) {
    for (int i = 0; fact && name && i < fact->name_count; i++) {
        if (fact->names[i] && strcmp(fact->names[i], name) == 0)
            return true;
    }
    return false;
}

/* Evaluate the final __all__ value for one name. A dynamic value is not an
 * export oracle; deletion restores Python's public-name rule. */
static bool pxc_python_name_is_exported(const CBMPyNamespaceFacts *facts, const char *name,
                                        bool *out_has_all, bool *out_known) {
    bool has_all = false;
    bool known = true;
    bool member = false;
    for (int i = 0; facts && i < facts->count; i++) {
        const CBMPyNamespaceFact *fact = &facts->items[i];
        if (fact->kind == CBM_PY_NS_ALL_SET) {
            has_all = true;
            known = true;
            member = pxc_python_name_in_list(fact, name);
        } else if (fact->kind == CBM_PY_NS_ALL_APPEND) {
            if (!has_all || !known) {
                has_all = true;
                known = false;
            } else {
                member = member || pxc_python_name_in_list(fact, name);
            }
        } else if (fact->kind == CBM_PY_NS_ALL_UNKNOWN) {
            has_all = true;
            known = false;
            member = false;
        } else if (fact->kind == CBM_PY_NS_ALL_DELETE) {
            has_all = false;
            known = true;
            member = false;
        }
    }
    if (out_has_all)
        *out_has_all = has_all;
    if (out_known)
        *out_known = known;
    return has_all ? known && member : name && name[0] != '_';
}

static bool pxc_python_fact_path(const CBMPyNamespaceFact *fact, bool include_member, char *path,
                                 size_t capacity) {
    if (!fact || !path || capacity == 0 || (size_t)fact->relative_level >= capacity)
        return false;
    size_t used = fact->relative_level;
    memset(path, '.', used);
    const char *module = fact->module_name ? fact->module_name : "";
    size_t module_len = strlen(module);
    if (module_len >= capacity - used)
        return false;
    memcpy(path + used, module, module_len);
    used += module_len;
    if (include_member && fact->member_name && fact->member_name[0]) {
        size_t member_len = strlen(fact->member_name);
        bool separator = module_len > 0;
        if (member_len + (separator ? 1U : 0U) >= capacity - used)
            return false;
        if (separator)
            path[used++] = '.';
        memcpy(path + used, fact->member_name, member_len);
        used += member_len;
    }
    path[used] = '\0';
    return used > 0;
}

static const cbm_gbuf_node_t *pxc_python_fact_target(const cbm_pipeline_ctx_t *ctx,
                                                     const char *rel_path, const char *file_qn,
                                                     const CBMPyNamespaceFact *fact,
                                                     bool include_member) {
    char path[CBM_PATH_MAX];
    if (!pxc_python_fact_path(fact, include_member, path, sizeof(path)))
        return NULL;
    CBMImport imp = {.local_name = fact->local_name, .module_path = path};
    const cbm_gbuf_node_t *target =
        cbm_pipeline_resolve_import_node(ctx, rel_path, file_qn, &imp, NULL);
    if (target && cbm_suppress_cross_language_code_match(CBM_LANG_PYTHON, ctx->repo_path,
                                                         target->file_path)) {
        return NULL;
    }
    return target;
}

/* What one directory on the module search path provides for a module path,
 * in importlib's FileFinder order: a directory holding `__init__.py` is a
 * regular package and wins over a same-named `.py` module, which wins over a
 * directory without one (a namespace package portion, which also loses to a
 * regular hit in any other search directory). */
typedef enum {
    PXC_PY_FINDS_NOTHING = 0,
    PXC_PY_FINDS_NAMESPACE,
    PXC_PY_FINDS_REGULAR
} PXCPythonFinds;

/* The node of `label` that carries `qn` (consumed) and stands for exactly the
 * repo-relative `path`: a QN alone can collide (`a/b.py` and the folder `a/b`
 * share one). */
static const cbm_gbuf_node_t *pxc_python_node_at(const cbm_pipeline_ctx_t *ctx, char *qn,
                                                 const char *label, const char *path) {
    const cbm_gbuf_node_t *node = qn ? cbm_gbuf_find_by_qn(ctx->gbuf, qn) : NULL;
    pxc_free_fqn(qn);
    return node && node->label && strcmp(node->label, label) == 0 && node->file_path &&
                   strcmp(node->file_path, path) == 0
               ? node
               : NULL;
}

static bool pxc_python_is_package_dir(const cbm_pipeline_ctx_t *ctx, const char *dir) {
    char path[CBM_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s%s__init__.py", dir, dir[0] ? "/" : "");
    if (n <= 0 || (size_t)n >= sizeof(path))
        return false;
    char *qn = cbm_pipeline_fqn_compute(ctx->project_name, path, "__file__");
    bool found = qn && cbm_gbuf_find_by_qn(ctx->gbuf, qn);
    pxc_free_fqn(qn);
    return found;
}

/* What the search directory `dir` provides for the slash-form module path
 * `module`, and its node. A regular package is its Folder, or its `__init__`
 * Module when no Folder carries the path. A hit without a node stays a hit
 * (*out NULL), so it still counts against another directory's hit. */
static PXCPythonFinds pxc_python_find_in(const cbm_pipeline_ctx_t *ctx, const char *dir,
                                         const char *module, const cbm_gbuf_node_t **out) {
    char path[CBM_PATH_MAX];
    char file[CBM_PATH_MAX];
    *out = NULL;
    int n = snprintf(path, sizeof(path), "%s%s%s", dir, dir[0] && module[0] ? "/" : "", module);
    int m = snprintf(file, sizeof(file), "%s/__init__.py", path);
    if (n <= 0 || (size_t)n >= sizeof(path) || m <= 0 || (size_t)m >= sizeof(file))
        return PXC_PY_FINDS_NOTHING;
    const char *project = ctx->project_name;
    if (pxc_python_is_package_dir(ctx, path)) {
        *out = pxc_python_node_at(ctx, cbm_pipeline_fqn_folder(project, path), "Folder", path);
        if (!*out)
            *out = pxc_python_node_at(ctx, cbm_pipeline_fqn_module(project, file), "Module", file);
        return PXC_PY_FINDS_REGULAR;
    }
    m = snprintf(file, sizeof(file), "%s.py", path);
    if (m > 0 && (size_t)m < sizeof(file)) {
        *out = pxc_python_node_at(ctx, cbm_pipeline_fqn_module(project, file), "Module", file);
        if (*out)
            return PXC_PY_FINDS_REGULAR;
    }
    *out = pxc_python_node_at(ctx, cbm_pipeline_fqn_folder(project, path), "Folder", path);
    return *out ? PXC_PY_FINDS_NAMESPACE : PXC_PY_FINDS_NOTHING;
}

static bool pxc_python_pop_dir(char *dir) {
    if (!dir[0])
        return false;
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';
    else
        dir[0] = '\0';
    return true;
}

/* The project module or package `<level dots><dotted>` names when imported
 * from `rel_path`. A relative import names one path under the importing
 * file's package. An absolute one is searched the way the interpreter's
 * search path finds it: in the nearest ancestor of the importing file that is
 * no package (the directory a script there runs from) and in every directory
 * above it up to the repository root; a package's own directory is never a
 * search root, so `from tf.util import x` inside tf/ does not find tf/tf.py.
 * The module is located only when exactly one of those directories provides
 * it; two providers, or a hit without a node, leave it unlocated (NULL), and so
 * does a module outside the project. */
static const cbm_gbuf_node_t *pxc_python_import_module(const cbm_pipeline_ctx_t *ctx,
                                                       const char *rel_path,
                                                       uint32_t relative_level,
                                                       const char *dotted) {
    char module[CBM_PATH_MAX];
    char dir[CBM_PATH_MAX];
    size_t len = strlen(dotted);
    int n = snprintf(dir, sizeof(dir), "%s", rel_path ? rel_path : "");
    if (len >= sizeof(module) || n <= 0 || (size_t)n >= sizeof(dir))
        return NULL;
    for (size_t i = 0; i <= len; i++)
        module[i] = dotted[i] == '.' ? '/' : dotted[i];
    (void)pxc_python_pop_dir(dir);
    const cbm_gbuf_node_t *node = NULL;
    if (relative_level > 0) {
        for (uint32_t up = 1; up < relative_level; up++) {
            if (!pxc_python_pop_dir(dir))
                return NULL;
        }
        if (!dir[0] && !module[0])
            return NULL;
        return pxc_python_find_in(ctx, dir, module, &node) == PXC_PY_FINDS_NOTHING ? NULL : node;
    }
    if (!module[0])
        return NULL;
    while (dir[0] && pxc_python_is_package_dir(ctx, dir))
        (void)pxc_python_pop_dir(dir);
    int regular = 0;
    int portions = 0;
    const cbm_gbuf_node_t *regular_node = NULL;
    const cbm_gbuf_node_t *portion_node = NULL;
    do {
        PXCPythonFinds finds = pxc_python_find_in(ctx, dir, module, &node);
        if (finds == PXC_PY_FINDS_REGULAR) {
            regular++;
            regular_node = node;
        } else if (finds == PXC_PY_FINDS_NAMESPACE) {
            portions++;
            portion_node = node;
        }
    } while (pxc_python_pop_dir(dir));
    if (regular > 0)
        return regular == 1 ? regular_node : NULL;
    return portions == 1 ? portion_node : NULL;
}

/* Standard-library decorators that return the decorated object, or an
 * equivalent of the very definition it decorates, so the name stays bound to
 * that definition. */
static const struct {
    const char *module;
    const char *member;
} PXC_IDENTITY_DECORATORS[] = {
    {"dataclasses", "dataclass"},
    {"typing", "final"},
    {"typing_extensions", "final"},
    {"typing", "runtime_checkable"},
    {"typing_extensions", "runtime_checkable"},
    {"functools", "total_ordering"},
    {"enum", "unique"},
};

/* True when the facts before `upto` bind `head` (a dotted decorator head) to
 * the standard-library decorator module.member and no star import follows that
 * binding. */
static bool pxc_python_decorator_is_stdlib(const CBMPyNamespaceFacts *facts, int upto,
                                           const char *head, const char *module,
                                           const char *member) {
    const char *dot = strchr(head, '.');
    if (dot && strchr(dot + 1, '.'))
        return false;
    char first[CBM_SZ_128];
    size_t first_len = dot ? (size_t)(dot - head) : strlen(head);
    if (first_len == 0 || first_len >= sizeof(first))
        return false;
    memcpy(first, head, first_len);
    first[first_len] = '\0';
    if (dot && strcmp(dot + 1, member) != 0)
        return false;
    for (int i = upto - 1; i >= 0; i--) {
        const CBMPyNamespaceFact *fact = &facts->items[i];
        /* A star import may rebind the name to anything. An unmodelled statement
         * is not scanned: the replay keeps every other proven binding across
         * it as well. */
        if (fact->kind == CBM_PY_NS_IMPORT_STAR)
            return false;
        if (!fact->local_name || strcmp(fact->local_name, first) != 0)
            continue;
        if (dot)
            return fact->kind == CBM_PY_NS_IMPORT_MODULE && fact->module_name &&
                   strcmp(fact->module_name, module) == 0;
        return fact->kind == CBM_PY_NS_IMPORT_NAME && fact->relative_level == 0 &&
               fact->module_name && strcmp(fact->module_name, module) == 0 && fact->member_name &&
               strcmp(fact->member_name, member) == 0;
    }
    return false;
}

static bool pxc_python_decorator_preserves(const CBMPyNamespaceFacts *facts, int upto,
                                           const char *text) {
    while (*text == '@' || *text == ' ')
        text++;
    char head[CBM_SZ_128];
    size_t len = 0;
    while ((isalnum((unsigned char)text[len]) || text[len] == '_' || text[len] == '.') &&
           len + 1 < sizeof(head)) {
        head[len] = text[len];
        len++;
    }
    head[len] = '\0';
    if (len == 0)
        return false;
    for (size_t k = 0; k < sizeof(PXC_IDENTITY_DECORATORS) / sizeof(PXC_IDENTITY_DECORATORS[0]);
         k++) {
        if (pxc_python_decorator_is_stdlib(facts, upto, head, PXC_IDENTITY_DECORATORS[k].module,
                                           PXC_IDENTITY_DECORATORS[k].member))
            return true;
    }
    return false;
}

/* A decorated definition keeps its name bound to its own definition when every
 * decorator is one of the identity-preserving standard-library decorators
 * above, resolved through the imports that precede it. Any other decorator may
 * return anything, so the name stays blocked. The decorators are read from the
 * definition's node, as the extractor wrote them. */
static bool pxc_python_decorators_keep_definition(const cbm_gbuf_node_t *node,
                                                  const CBMPyNamespaceFacts *facts, int upto) {
    const char *p =
        node && node->properties_json ? strstr(node->properties_json, "\"decorators\":[") : NULL;
    if (!p)
        return false;
    p += strlen("\"decorators\":[");
    int seen = 0;
    while (*p && *p != ']') {
        if (*p == ',' || *p == ' ') {
            p++;
            continue;
        }
        if (*p != '"')
            return false;
        p++;
        char text[CBM_SZ_256];
        size_t n = 0;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1])
                p++;
            if (n + 1 < sizeof(text))
                text[n++] = *p;
            p++;
        }
        if (*p != '"')
            return false;
        p++;
        text[n] = '\0';
        if (!pxc_python_decorator_preserves(facts, upto, text))
            return false;
        seen++;
    }
    return seen > 0 && *p == ']';
}

/* The state one import-map build shares across the modules it reads: the
 * namespace facts of each project module it consulted, and the verdict for each
 * (module, name) it asked about. Both live for that build only, so a package
 * many names are imported from is read once, and a re-export chain that
 * branches through star imports visits each (module, name) once. A verdict
 * still being computed is PXC_PY_PENDING: meeting it again is an import cycle. */
typedef enum { PXC_PY_UNBOUND = 0, PXC_PY_OPAQUE, PXC_PY_BOUND, PXC_PY_PENDING } PXCPythonVerdict;

typedef struct {
    bool usable;
    CBMPyNamespaceFacts facts;
} PXCPythonModuleFacts;

typedef struct {
    PXCPythonVerdict verdict;
    const cbm_gbuf_node_t *node;
} PXCPythonMemberVerdict;

typedef struct {
    const cbm_pipeline_ctx_t *ctx;
    const char *project_name;
    CBMArena arena;
    CBMHashTable *facts;   /* owner Module QN -> PXCPythonModuleFacts */
    CBMHashTable *members; /* "<module QN>\n<name>" -> PXCPythonMemberVerdict */
} PXCPythonResolver;

static bool pxc_python_resolver_ready(PXCPythonResolver *pr) {
    if (pr->facts)
        return pr->members != NULL;
    cbm_arena_init(&pr->arena);
    pr->facts = cbm_ht_create_in(CBM_MEM_CLASS_OTHER, CBM_SZ_16);
    pr->members = cbm_ht_create_in(CBM_MEM_CLASS_OTHER, CBM_SZ_16);
    return pr->facts && pr->members;
}

static void pxc_python_resolver_free(PXCPythonResolver *pr) {
    if (!pr->facts && !pr->members)
        return;
    cbm_ht_free(pr->facts);
    cbm_ht_free(pr->members);
    cbm_arena_destroy(&pr->arena);
    pr->facts = NULL;
    pr->members = NULL;
}

/* The Module node a package's own names are read from: a Module is its own
 * owner, a package Folder is owned by its `__init__` module. */
static const cbm_gbuf_node_t *pxc_python_owner_module(const cbm_gbuf_t *gbuf,
                                                      const cbm_gbuf_node_t *module) {
    if (!module || !module->label || !module->qualified_name)
        return NULL;
    if (strcmp(module->label, "Module") == 0)
        return module;
    if (strcmp(module->label, "Folder") != 0)
        return NULL;
    char qn[CBM_PATH_MAX];
    int n = snprintf(qn, sizeof(qn), "%s.__init__", module->qualified_name);
    const cbm_gbuf_node_t *init =
        n > 0 && (size_t)n < sizeof(qn) ? cbm_gbuf_find_by_qn(gbuf, qn) : NULL;
    return init && init->label && strcmp(init->label, "Module") == 0 ? init : NULL;
}

/* Whether `module`, the node the import resolver gave for `<level
 * dots><dotted>` written in `rel_path`, is the module the interpreter's search
 * finds there (pxc_python_import_module). The resolver can answer with a near
 * miss: an absolute `from tf.util import x` inside the package tf/ comes back
 * as the sibling module tf/tf.py, and reading that module's own bindings would
 * publish whatever it binds under the name. Only a located module is read. */
static bool pxc_python_module_located(const cbm_pipeline_ctx_t *ctx, const char *rel_path,
                                      uint32_t relative_level, const char *dotted,
                                      const cbm_gbuf_node_t *module) {
    const cbm_gbuf_node_t *located =
        module && dotted ? pxc_python_import_module(ctx, rel_path, relative_level, dotted) : NULL;
    if (!located)
        return false;
    if (located == module)
        return true;
    const cbm_gbuf_node_t *owner = pxc_python_owner_module(ctx->gbuf, module);
    return owner && owner == pxc_python_owner_module(ctx->gbuf, located);
}

/* The namespace facts of every module source an import-map build has read,
 * shared by all builds of the process. A pass builds one file's map at a time,
 * up to four times per file (lsp_cross, calls, usages, semantic), and every
 * imported module is read to find the statement that last binds the name; a
 * module many files import from would otherwise be parsed once per importing
 * file and pass. An entry is keyed by the source's absolute path and answers
 * only when the bytes read now equal the bytes it was extracted from, so an
 * edited file is extracted again; the facts are content-only (cbm.h), so equal
 * bytes give equal facts. A build copies the facts into its own arena. The
 * cache is emptied when an entry would take it past PXC_PY_FACTS_CACHE_BYTES. */
enum { PXC_PY_FACTS_CACHE_BYTES = CBM_SZ_64 * CBM_SZ_1K * CBM_SZ_1K };

typedef struct {
    CBMArena arena;
    const char *path;
    const char *source;
    int source_len;
    bool usable;
    CBMPyNamespaceFacts facts;
} PXCPythonCachedFacts;

static atomic_flag g_pxc_py_facts_lock = ATOMIC_FLAG_INIT;
static CBMHashTable *g_pxc_py_facts; /* absolute path -> PXCPythonCachedFacts */
static size_t g_pxc_py_facts_bytes;

static void pxc_py_facts_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_pxc_py_facts_lock, memory_order_acquire)) {}
}

static void pxc_py_facts_unlock(void) {
    atomic_flag_clear_explicit(&g_pxc_py_facts_lock, memory_order_release);
}

static void pxc_py_cached_facts_free(PXCPythonCachedFacts *entry) {
    if (!entry)
        return;
    cbm_arena_destroy(&entry->arena);
    cbm_free(CBM_MEM_CLASS_OTHER, entry);
}

static void pxc_py_cached_facts_free_each(const char *key, void *value, void *userdata) {
    (void)key;
    (void)userdata;
    pxc_py_cached_facts_free(value);
}

/* Copies the cached facts of `path` into `arena` when the cache holds them
 * for exactly `source`. True on such a hit, with *usable set. */
static bool pxc_py_facts_cache_get(const char *path, const char *source, int source_len,
                                   CBMArena *arena, CBMPyNamespaceFacts *out, bool *usable) {
    bool hit = false;
    pxc_py_facts_lock();
    const PXCPythonCachedFacts *entry = g_pxc_py_facts ? cbm_ht_get(g_pxc_py_facts, path) : NULL;
    if (entry && entry->source_len == source_len &&
        memcmp(entry->source, source, (size_t)source_len) == 0) {
        hit = true;
        *usable = entry->usable && cbm_py_namespace_facts_copy(arena, &entry->facts, out);
    }
    pxc_py_facts_unlock();
    return hit;
}

static void pxc_py_facts_cache_put(const char *path, const char *source, int source_len,
                                   bool usable, const CBMPyNamespaceFacts *facts) {
    PXCPythonCachedFacts *entry = cbm_calloc(CBM_MEM_CLASS_OTHER, sizeof(*entry));
    if (!entry)
        return;
    cbm_arena_init_sized(&entry->arena, CBM_SZ_4K);
    char *copy = cbm_arena_alloc(&entry->arena, (size_t)source_len);
    if (copy)
        memcpy(copy, source, (size_t)source_len);
    entry->path = cbm_arena_strdup(&entry->arena, path);
    entry->source = copy;
    entry->source_len = source_len;
    entry->usable = usable && cbm_py_namespace_facts_copy(&entry->arena, facts, &entry->facts);
    if (!entry->path || !entry->source || (usable && !entry->usable)) {
        pxc_py_cached_facts_free(entry);
        return;
    }
    size_t bytes = cbm_arena_capacity(&entry->arena);
    pxc_py_facts_lock();
    if (!g_pxc_py_facts)
        g_pxc_py_facts = cbm_ht_create_in(CBM_MEM_CLASS_OTHER, CBM_SZ_64);
    PXCPythonCachedFacts *old = g_pxc_py_facts ? cbm_ht_delete(g_pxc_py_facts, path) : NULL;
    if (old)
        g_pxc_py_facts_bytes -= cbm_arena_capacity(&old->arena);
    if (g_pxc_py_facts && g_pxc_py_facts_bytes + bytes > (size_t)PXC_PY_FACTS_CACHE_BYTES) {
        cbm_ht_foreach(g_pxc_py_facts, pxc_py_cached_facts_free_each, NULL);
        cbm_ht_clear(g_pxc_py_facts);
        g_pxc_py_facts_bytes = 0;
    }
    if (g_pxc_py_facts && bytes <= (size_t)PXC_PY_FACTS_CACHE_BYTES) {
        (void)cbm_ht_set(g_pxc_py_facts, entry->path, entry);
        g_pxc_py_facts_bytes += bytes;
        entry = NULL;
    }
    pxc_py_facts_unlock();
    pxc_py_cached_facts_free(old);
    pxc_py_cached_facts_free(entry);
}

/* The ordered namespace facts of one project module, read from its source once
 * per build and extracted once per source content (pxc_py_facts_cache_get).
 * False when the source cannot be read or its facts are incomplete: nothing
 * about that module's names is then proven. */
static bool pxc_python_module_facts(PXCPythonResolver *pr, const cbm_gbuf_node_t *owner,
                                    CBMPyNamespaceFacts *out) {
    memset(out, 0, sizeof(*out));
    if (!owner || !owner->file_path || !owner->qualified_name || !pr->ctx->repo_path ||
        !pxc_python_resolver_ready(pr))
        return false;
    PXCPythonModuleFacts *known = cbm_ht_get(pr->facts, owner->qualified_name);
    if (known) {
        *out = known->facts;
        return known->usable;
    }
    PXCPythonModuleFacts *entry = cbm_arena_calloc(&pr->arena, sizeof(*entry));
    char *key = cbm_arena_strdup(&pr->arena, owner->qualified_name);
    if (!entry || !key)
        return false;
    char path[CBM_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", pr->ctx->repo_path, owner->file_path);
    int source_len = 0;
    char *source = n > 0 && (size_t)n < sizeof(path) ? pxc_read_file(path, &source_len) : NULL;
    if (!source || !pxc_py_facts_cache_get(path, source, source_len, &pr->arena, &entry->facts,
                                           &entry->usable)) {
        CBMFileResult *result =
            source ? cbm_extract_file(source, source_len, CBM_LANG_PYTHON, pr->project_name,
                                      owner->file_path, CBM_EXTRACT_BUDGET, NULL, NULL)
                   : NULL;
        entry->usable =
            result && result->py_namespace.status == CBM_PY_NS_COMPLETE &&
            cbm_py_namespace_facts_valid(&result->py_namespace) &&
            cbm_py_namespace_facts_copy(&pr->arena, &result->py_namespace, &entry->facts);
        if (source && result)
            pxc_py_facts_cache_put(path, source, source_len, entry->usable, &entry->facts);
        cbm_free_result(result);
    }
    cbm_free(CBM_MEM_CLASS_EXTRACT, source);
    (void)cbm_ht_set(pr->facts, key, entry);
    *out = entry->facts;
    return entry->usable;
}

static bool pxc_python_replay_namespace(PXCPythonResolver *pr, const char *rel_path,
                                        const char *module_qn, const CBMPyNamespaceFacts *facts,
                                        PXCPythonImportMap *map, bool collect_exports, int depth);

static bool pxc_python_load_namespace(PXCPythonResolver *pr, const cbm_gbuf_node_t *module,
                                      PXCPythonImportMap *map, CBMPyNamespaceFacts *out_facts,
                                      int depth) {
    const cbm_gbuf_node_t *owner = pxc_python_owner_module(pr->ctx->gbuf, module);
    if (depth > CBM_SZ_16 || !pxc_python_module_facts(pr, owner, out_facts))
        return false;
    return pxc_python_replay_namespace(pr, owner->file_path, owner->qualified_name, out_facts, map,
                                       true, depth + 1);
}

static bool pxc_python_apply_star(PXCPythonResolver *pr, const char *rel_path, const char *file_qn,
                                  const CBMPyNamespaceFact *fact, PXCPythonImportMap *map,
                                  int depth) {
    const cbm_gbuf_node_t *module = pxc_python_fact_target(pr->ctx, rel_path, file_qn, fact, false);
    PXCPythonImportMap exported = {0};
    CBMPyNamespaceFacts source = {0};
    /* A package is read through its `__init__` module, which the resolver's
     * answer alone does not prove is the package imported here. */
    bool package = module && module->label && strcmp(module->label, "Folder") == 0;
    if ((package && !pxc_python_module_located(pr->ctx, rel_path, fact->relative_level,
                                               fact->module_name, module)) ||
        !pxc_python_load_namespace(pr, module, &exported, &source, depth)) {
        pxc_python_map_free(&exported);
        return false;
    }
    bool has_all = false;
    bool known = true;
    (void)pxc_python_name_is_exported(&source, "", &has_all, &known);
    if (!known) {
        pxc_python_map_free(&exported);
        return false;
    }
    for (int i = 0; i < exported.count; i++) {
        const char *value = pxc_python_name_is_exported(&source, exported.keys[i], NULL, NULL)
                                ? exported.vals[i]
                                : NULL;
        if (!pxc_python_map_set(map, exported.keys[i], value)) {
            pxc_python_map_free(&exported);
            return false;
        }
    }
    if (has_all) {
        for (int i = 0; i < source.count; i++) {
            const CBMPyNamespaceFact *all = &source.items[i];
            if (all->kind != CBM_PY_NS_ALL_SET && all->kind != CBM_PY_NS_ALL_APPEND)
                continue;
            for (int j = 0; j < all->name_count; j++) {
                const char *name = all->names[j];
                if (!name || !pxc_python_name_is_exported(&source, name, NULL, NULL))
                    continue;
                int found = pxc_python_map_find(&exported, name);
                const char *value = found >= 0 ? exported.vals[found] : NULL;
                if (!pxc_python_map_set(map, name, value)) {
                    pxc_python_map_free(&exported);
                    return false;
                }
            }
        }
    }
    pxc_python_map_free(&exported);
    return true;
}

/* The module-level node a name defined in this file lands on. A package
 * `__init__` file defines its names on the package QN, so the symbol scope, not
 * the module QN, prefixes the name. */
static const cbm_gbuf_node_t *pxc_python_own_node(const cbm_pipeline_ctx_t *ctx,
                                                  const char *module_qn, const char *name) {
    char qn[CBM_PATH_MAX];
    int n = snprintf(qn, sizeof(qn), "%.*s.%s", (int)cbm_fqn_symbol_scope_len(module_qn), module_qn,
                     name);
    return n > 0 && (size_t)n < sizeof(qn) ? cbm_gbuf_find_by_qn(ctx->gbuf, qn) : NULL;
}

static bool pxc_python_decorated_node_kept(const cbm_pipeline_ctx_t *ctx, const char *module_qn,
                                           const CBMPyNamespaceFacts *facts, int index) {
    const cbm_gbuf_node_t *node =
        pxc_python_own_node(ctx, module_qn, facts->items[index].local_name);
    return pxc_python_decorators_keep_definition(node, facts, index);
}

/* A class statement, an assignment, or a decorated definition its own node
 * keeps: the statement binds the name to the module's own node for it. */
static bool pxc_python_shadow_binds_own(const cbm_pipeline_ctx_t *ctx, const char *module_qn,
                                        const CBMPyNamespaceFacts *facts, int index) {
    const CBMPyNamespaceFact *fact = &facts->items[index];
    return fact->kind == CBM_PY_NS_SHADOW && fact->local_name &&
           (fact->reason == CBM_PY_NS_REASON_UNPROVEN_CLASS ||
            fact->reason == CBM_PY_NS_REASON_VALUE ||
            (fact->reason == CBM_PY_NS_REASON_DECORATED &&
             pxc_python_decorated_node_kept(ctx, module_qn, facts, index)));
}

static PXCPythonVerdict pxc_python_member_binding(PXCPythonResolver *pr,
                                                  const cbm_gbuf_node_t *module, const char *member,
                                                  int depth, const cbm_gbuf_node_t **out);

/* The Module or package Folder `from <module> import <member>`, written in
 * `rel_path`, names (a relative or absolute module path), or NULL. */
static const cbm_gbuf_node_t *pxc_python_import_name_module(PXCPythonResolver *pr,
                                                            const char *rel_path,
                                                            const char *file_qn,
                                                            const CBMPyNamespaceFact *fact) {
    const cbm_pipeline_ctx_t *ctx = pr->ctx;
    const cbm_gbuf_node_t *module = pxc_python_fact_target(ctx, rel_path, file_qn, fact, false);
    if ((!module || !module->label || strcmp(module->label, "Module") != 0) &&
        fact->relative_level == 0) {
        module =
            cbm_pipeline_lsp_target_node(ctx->gbuf, pr->project_name, fact->module_name, false);
    }
    if (!module || !module->label || !fact->member_name ||
        (strcmp(module->label, "Module") != 0 && strcmp(module->label, "Folder") != 0))
        return NULL;
    return module;
}

/* The node an import statement binds its local name to: the imported module,
 * or the member the imported module binds under that name (its own definition,
 * or whatever its own imports bind it to: a package `__init__` that re-exports
 * `from .base import Base`). NULL when the import names nothing in the project
 * or the binding cannot be followed.
 *
 * The imported module's own bindings are read only when the interpreter's
 * search locates the module the resolver answered (pxc_python_module_located).
 * For an unlocated module the file whose map is built keeps the resolver's
 * exact `<module>.<member>` node, as before; an import met while following a
 * re-export (`followed`) answers nothing for it, nor for a module or package
 * import whose target the search does not locate. */
static const cbm_gbuf_node_t *pxc_python_import_fact_node(PXCPythonResolver *pr,
                                                          const char *rel_path, const char *file_qn,
                                                          const CBMPyNamespaceFact *fact, int depth,
                                                          bool followed) {
    const cbm_pipeline_ctx_t *ctx = pr->ctx;
    const cbm_gbuf_node_t *target = NULL;
    if (fact->kind == CBM_PY_NS_IMPORT_NAME && fact->module_name && fact->module_name[0]) {
        const cbm_gbuf_node_t *module = pxc_python_import_name_module(pr, rel_path, file_qn, fact);
        if (!module)
            return NULL;
        if (pxc_python_module_located(ctx, rel_path, fact->relative_level, fact->module_name,
                                      module)) {
            if (pxc_python_member_binding(pr, module, fact->member_name, depth, &target) !=
                PXC_PY_BOUND)
                target = NULL;
        } else if (!followed) {
            char qn[CBM_PATH_MAX];
            int n = snprintf(qn, sizeof(qn), "%s.%s", module->qualified_name, fact->member_name);
            target = n > 0 && (size_t)n < sizeof(qn) ? cbm_gbuf_find_by_qn(ctx->gbuf, qn) : NULL;
        }
        return target;
    }
    char root_name[CBM_PATH_MAX];
    const char *dotted =
        fact->kind == CBM_PY_NS_IMPORT_NAME ? fact->member_name : fact->module_name;
    if (fact->kind == CBM_PY_NS_IMPORT_MODULE && (fact->flags & CBM_PY_NS_IMPORT_BINDS_ROOT) &&
        fact->module_name) {
        CBMPyNamespaceFact root_fact = *fact;
        const char *dot = strchr(fact->module_name, '.');
        size_t len = dot ? (size_t)(dot - fact->module_name) : strlen(fact->module_name);
        if (len >= sizeof(root_name))
            return NULL;
        memcpy(root_name, fact->module_name, len);
        root_name[len] = '\0';
        root_fact.module_name = root_name;
        dotted = root_name;
        target = pxc_python_fact_target(ctx, rel_path, file_qn, &root_fact, false);
    } else {
        target = pxc_python_fact_target(ctx, rel_path, file_qn, fact,
                                        fact->kind == CBM_PY_NS_IMPORT_NAME);
    }
    if (followed && target &&
        !pxc_python_module_located(ctx, rel_path, fact->relative_level, dotted, target))
        return NULL;
    return target;
}

/* Whether `from <star module> import *` in `owner` binds `member`, and to what. */
static PXCPythonVerdict pxc_python_star_binding(PXCPythonResolver *pr, const cbm_gbuf_node_t *owner,
                                                const char *file_qn, const CBMPyNamespaceFact *fact,
                                                const char *member, int depth,
                                                const cbm_gbuf_node_t **out) {
    const cbm_gbuf_node_t *star =
        pxc_python_fact_target(pr->ctx, owner->file_path, file_qn, fact, false);
    CBMPyNamespaceFacts star_facts;
    if (!pxc_python_module_located(pr->ctx, owner->file_path, fact->relative_level,
                                   fact->module_name, star) ||
        !pxc_python_module_facts(pr, pxc_python_owner_module(pr->ctx->gbuf, star), &star_facts))
        return PXC_PY_OPAQUE;
    bool known = true;
    if (!pxc_python_name_is_exported(&star_facts, member, NULL, &known))
        return known ? PXC_PY_UNBOUND : PXC_PY_OPAQUE;
    return pxc_python_member_binding(pr, star, member, depth, out);
}

/* Whether `fact`, a statement of `owner` that binds `member` under that name,
 * is `from <owner> import <member>`: a package importing its own name binds
 * what its run bound to it before that statement, or else the submodule
 * (pxc_python_member_binding), as if the statement were not there. */
static bool pxc_python_imports_own_name(PXCPythonResolver *pr, const cbm_gbuf_node_t *owner,
                                        const char *file_qn, const CBMPyNamespaceFact *fact) {
    if (fact->kind != CBM_PY_NS_IMPORT_NAME || !fact->module_name || !fact->module_name[0] ||
        !fact->member_name || strcmp(fact->member_name, fact->local_name) != 0)
        return false;
    const cbm_gbuf_node_t *module =
        pxc_python_import_name_module(pr, owner->file_path, file_qn, fact);
    return module && pxc_python_owner_module(pr->ctx->gbuf, module) == owner &&
           pxc_python_module_located(pr->ctx, owner->file_path, fact->relative_level,
                                     fact->module_name, module);
}

/* How a statement of `owner`'s run binds `member`, read backwards by
 * pxc_python_final_binding. */
typedef enum {
    PXC_PY_STEP_SKIP = 0,    /* binds another name, or rebinds what is already bound */
    PXC_PY_STEP_OWN,         /* binds the module's own node for the name */
    PXC_PY_STEP_OWN_OR_KEPT, /* binds that node, or leaves the earlier binding */
    PXC_PY_STEP_UNBOUND,     /* `del member` */
    PXC_PY_STEP_OTHER,       /* an import, a star, a SHADOW: read by the caller */
} PXCPythonStep;

/* The step `fact` makes for `__all__`, which only its own facts bind: a list
 * or another value binds the module's node for it, `+=` binds it or keeps the
 * earlier one, `del __all__` unbinds it. */
static PXCPythonStep pxc_python_all_step(const CBMPyNamespaceFact *fact) {
    switch (fact->kind) {
    case CBM_PY_NS_ALL_SET:
        return PXC_PY_STEP_OWN;
    case CBM_PY_NS_ALL_UNKNOWN:
        return fact->reason == CBM_PY_NS_REASON_VALUE ? PXC_PY_STEP_OWN : PXC_PY_STEP_OTHER;
    case CBM_PY_NS_ALL_APPEND:
        return PXC_PY_STEP_OWN_OR_KEPT;
    case CBM_PY_NS_ALL_DELETE:
        return PXC_PY_STEP_UNBOUND;
    default:
        return PXC_PY_STEP_SKIP;
    }
}

static PXCPythonStep pxc_python_step(PXCPythonResolver *pr, const cbm_gbuf_node_t *owner,
                                     const char *file_qn, const CBMPyNamespaceFact *fact,
                                     const char *member) {
    if (fact->kind == CBM_PY_NS_IMPORT_STAR ||
        (fact->kind == CBM_PY_NS_SHADOW && fact->local_name &&
         strcmp(fact->local_name, CBM_PY_NS_ANY_NAME) == 0))
        return PXC_PY_STEP_OTHER;
    if (strcmp(member, "__all__") == 0 && !fact->local_name)
        return pxc_python_all_step(fact);
    if (!fact->local_name || strcmp(fact->local_name, member) != 0)
        return PXC_PY_STEP_SKIP;
    if (fact->kind == CBM_PY_NS_DELETE)
        return PXC_PY_STEP_UNBOUND;
    if (fact->kind == CBM_PY_NS_SHADOW && fact->reason == CBM_PY_NS_REASON_OWN_OR_KEPT)
        return PXC_PY_STEP_OWN_OR_KEPT;
    if (pxc_python_imports_own_name(pr, owner, file_qn, fact))
        return PXC_PY_STEP_SKIP;
    if (fact->kind == CBM_PY_NS_OWN_DEF)
        return PXC_PY_STEP_OWN;
    return PXC_PY_STEP_OTHER;
}

/* Whether `node`, the module's own node for a name, is what `fact` binds when
 * it binds that node: a def's Function, a class's Class, an assignment's
 * Variable. A def and an assignment of one name share one QN, and the graph
 * keeps one node for it, so a Function node does not stand for the value an
 * assignment in an `if` branch binds. A binder whose node the facts do not
 * fix (a decorated definition its node keeps, an import) fits any node. */
static bool pxc_python_binder_fits(const cbm_gbuf_node_t *node, const CBMPyNamespaceFact *fact) {
    const char *label = NULL;
    bool own_or_kept =
        fact->kind == CBM_PY_NS_SHADOW && fact->reason == CBM_PY_NS_REASON_OWN_OR_KEPT;
    if (fact->kind == CBM_PY_NS_OWN_DEF || own_or_kept)
        label = fact->def_kind == CBM_PY_NS_DEF_FUNCTION ? "Function"
                : fact->def_kind == CBM_PY_NS_DEF_CLASS  ? "Class"
                                                         : "Variable";
    else if (fact->kind == CBM_PY_NS_SHADOW && fact->reason == CBM_PY_NS_REASON_UNPROVEN_CLASS)
        label = "Class";
    else if ((fact->kind == CBM_PY_NS_SHADOW && fact->reason == CBM_PY_NS_REASON_VALUE) ||
             fact->kind == CBM_PY_NS_ALL_SET || fact->kind == CBM_PY_NS_ALL_APPEND ||
             fact->kind == CBM_PY_NS_ALL_UNKNOWN)
        label = "Variable";
    return node && (!label || (node->label && strcmp(node->label, label) == 0));
}

/* The binding `owner`'s top-level run leaves on `member`: the last statement
 * that binds it decides. A star import binds it only when the starred module
 * exports it; `del` leaves it unbound. A statement the facts do not model
 * names each module name it may bind (a SHADOW of reason UNSUPPORTED: an `if`
 * or `try` branch importing it, a loop target, a tuple target), or
 * CBM_PY_NS_ANY_NAME when it may bind any (globals(), exec(), a star import in
 * a branch): the binding after it is not proven, so the name is opaque.
 *
 * A statement that binds the name to the module's own node for it or leaves
 * the earlier binding in place (a SHADOW of reason OWN_OR_KEPT: an undecorated
 * def or class or an assignment in an `if` or `try` branch, `x += v`) binds
 * that node when the node is what the statement binds (pxc_python_binder_fits)
 * and every statement before it leaves the name unbound or binds that same
 * node: the node is then the only binding the run can leave. Any other
 * earlier binder may survive it, so the name is opaque. */
static PXCPythonVerdict pxc_python_final_binding(PXCPythonResolver *pr,
                                                 const cbm_gbuf_node_t *owner,
                                                 const CBMPyNamespaceFacts *facts,
                                                 const char *member, int depth,
                                                 const cbm_gbuf_node_t **out) {
    char *file_qn = cbm_pipeline_fqn_compute(pr->project_name, owner->file_path, "__file__");
    const cbm_gbuf_node_t *own = pxc_python_own_node(pr->ctx, owner->qualified_name, member);
    bool kept = false; /* an OWN_OR_KEPT binder follows the statements left to read */
    bool fits = true;  /* every binder read so far binds `own`, if it binds */
    PXCPythonVerdict verdict = PXC_PY_UNBOUND;
    for (int i = facts->count - 1; i >= 0; i--) {
        const CBMPyNamespaceFact *fact = &facts->items[i];
        PXCPythonStep step = pxc_python_step(pr, owner, file_qn, fact, member);
        if (step == PXC_PY_STEP_SKIP)
            continue;
        if (step == PXC_PY_STEP_OWN_OR_KEPT) {
            kept = true;
            fits = fits && pxc_python_binder_fits(own, fact);
            continue;
        }
        if (step == PXC_PY_STEP_UNBOUND)
            break;
        if (fact->kind == CBM_PY_NS_IMPORT_STAR) {
            verdict = pxc_python_star_binding(pr, owner, file_qn, fact, member, depth, out);
            if (verdict != PXC_PY_UNBOUND)
                break;
            continue;
        }
        *out = NULL;
        if (step == PXC_PY_STEP_OWN ||
            pxc_python_shadow_binds_own(pr->ctx, owner->qualified_name, facts, i)) {
            *out = own;
            fits = fits && pxc_python_binder_fits(own, fact);
        } else if (fact->kind == CBM_PY_NS_IMPORT_NAME || fact->kind == CBM_PY_NS_IMPORT_MODULE) {
            *out = pxc_python_import_fact_node(pr, owner->file_path, file_qn, fact, depth, true);
        }
        verdict = *out ? PXC_PY_BOUND : PXC_PY_OPAQUE;
        break;
    }
    pxc_free_fqn(file_qn);
    if (kept) {
        bool only_own = verdict == PXC_PY_UNBOUND || (verdict == PXC_PY_BOUND && *out == own);
        verdict = only_own && fits && own ? PXC_PY_BOUND : PXC_PY_OPAQUE;
        *out = own;
    }
    if (verdict != PXC_PY_BOUND)
        *out = NULL;
    return verdict;
}

/* Whether `node`, the node at `<package>.<member>`, is a submodule: what
 * `from <package> import <member>` binds when the package's own run leaves the
 * name unbound. */
static bool pxc_python_is_submodule(const cbm_gbuf_node_t *node) {
    return node && node->label &&
           (strcmp(node->label, "Module") == 0 || strcmp(node->label, "Folder") == 0);
}

/* Whether the statement of `facts` that last binds `member` is a decorated
 * definition of it (`@d def member`), with no star import or any-name write
 * after it. */
static bool pxc_python_last_binder_is_decorated_def(const CBMPyNamespaceFacts *facts,
                                                    const char *member) {
    for (int i = facts->count - 1; i >= 0; i--) {
        const CBMPyNamespaceFact *fact = &facts->items[i];
        if (fact->kind == CBM_PY_NS_IMPORT_STAR)
            return false;
        if (!fact->local_name)
            continue;
        if (fact->kind == CBM_PY_NS_SHADOW && strcmp(fact->local_name, CBM_PY_NS_ANY_NAME) == 0)
            return false;
        if (strcmp(fact->local_name, member) == 0)
            return fact->kind == CBM_PY_NS_SHADOW && fact->reason == CBM_PY_NS_REASON_DECORATED;
    }
    return false;
}

/* What `module` binds `member` to once its top-level run has finished: a node
 * of the project (PXC_PY_BOUND), something this resolver cannot name
 * (PXC_PY_OPAQUE), or nothing (PXC_PY_UNBOUND). The module's own namespace
 * facts decide, through the last statement that binds the name, also when the
 * module has its own definition of it (`def enc` and then `try: from
 * ._speedups import enc`). The node at `<module>.<member>` answers, as before,
 * when that statement is the decorated definition the node is (an importer
 * calls `@asynccontextmanager def f` through its definition, unlike the
 * defining file's own map), for a submodule the run leaves unbound, and for a
 * module whose facts cannot be read (no `__init__` module, an unreadable or
 * incomplete source). A chain longer than CBM_SZ_16 hops, or one that returns
 * to a (module, name) still being resolved, is opaque. */
static PXCPythonVerdict pxc_python_member_binding(PXCPythonResolver *pr,
                                                  const cbm_gbuf_node_t *module, const char *member,
                                                  int depth, const cbm_gbuf_node_t **out) {
    *out = NULL;
    if (!module || !module->qualified_name || !member)
        return PXC_PY_OPAQUE;
    char qn[CBM_PATH_MAX];
    int n = snprintf(qn, sizeof(qn), "%s.%s", module->qualified_name, member);
    if (n <= 0 || (size_t)n >= sizeof(qn))
        return PXC_PY_OPAQUE;
    const cbm_gbuf_node_t *exact = cbm_gbuf_find_by_qn(pr->ctx->gbuf, qn);
    const cbm_gbuf_node_t *owner = pxc_python_owner_module(pr->ctx->gbuf, module);
    CBMPyNamespaceFacts facts;
    if (!owner || !pxc_python_module_facts(pr, owner, &facts) ||
        (exact && !pxc_python_is_submodule(exact) &&
         pxc_python_last_binder_is_decorated_def(&facts, member))) {
        *out = exact;
        return exact ? PXC_PY_BOUND : PXC_PY_OPAQUE;
    }
    if (depth > CBM_SZ_16)
        return PXC_PY_OPAQUE;
    char *key = cbm_arena_sprintf(&pr->arena, "%s\n%s", owner->qualified_name, member);
    if (!key)
        return PXC_PY_OPAQUE;
    PXCPythonMemberVerdict *seen = cbm_ht_get(pr->members, key);
    if (seen) {
        *out = seen->node;
        return seen->verdict == PXC_PY_PENDING ? PXC_PY_OPAQUE : seen->verdict;
    }
    seen = cbm_arena_calloc(&pr->arena, sizeof(*seen));
    if (!seen)
        return PXC_PY_OPAQUE;
    seen->verdict = PXC_PY_PENDING;
    (void)cbm_ht_set(pr->members, key, seen);
    PXCPythonVerdict verdict = pxc_python_final_binding(pr, owner, &facts, member, depth + 1, out);
    if (verdict == PXC_PY_UNBOUND && pxc_python_is_submodule(exact)) {
        verdict = PXC_PY_BOUND;
        *out = exact;
    }
    if (verdict != PXC_PY_BOUND)
        *out = NULL;
    seen->verdict = verdict;
    seen->node = *out;
    return verdict;
}

/* A statement that binds its name to the module's own node or leaves the
 * earlier binding (`fact`, a SHADOW of reason OWN_OR_KEPT): an entry that
 * already holds that node stays when the node is what the statement binds
 * (pxc_python_binder_fits); any other entry is no longer proven. A name the
 * statement binds first keeps no entry, as an absent entry resolves to the
 * same-module name. */
static bool pxc_python_replay_own_or_kept(const cbm_pipeline_ctx_t *ctx, const char *module_qn,
                                          const CBMPyNamespaceFact *fact, PXCPythonImportMap *map) {
    int existing = pxc_python_map_find(map, fact->local_name);
    if (existing < 0)
        return true;
    const cbm_gbuf_node_t *node = pxc_python_own_node(ctx, module_qn, fact->local_name);
    const char *held = map->vals[existing];
    if (pxc_python_binder_fits(node, fact) && held && strcmp(held, node->qualified_name) == 0)
        return true;
    return pxc_python_map_set(map, fact->local_name, NULL);
}

static bool pxc_python_replay_namespace(PXCPythonResolver *pr, const char *rel_path,
                                        const char *module_qn, const CBMPyNamespaceFacts *facts,
                                        PXCPythonImportMap *map, bool collect_exports, int depth) {
    if (!pr || !pr->ctx || !facts || !map || depth > CBM_SZ_16)
        return false;
    const cbm_pipeline_ctx_t *ctx = pr->ctx;
    char *file_qn = cbm_pipeline_fqn_compute(pr->project_name, rel_path, "__file__");
    for (int i = 0; i < facts->count; i++) {
        const CBMPyNamespaceFact *fact = &facts->items[i];
        if (fact->kind == CBM_PY_NS_OWN_DEF && fact->local_name) {
            int existing = pxc_python_map_find(map, fact->local_name);
            const cbm_gbuf_node_t *node = pxc_python_own_node(ctx, module_qn, fact->local_name);
            if ((collect_exports || existing >= 0) &&
                !pxc_python_map_set(map, fact->local_name, node ? node->qualified_name : NULL)) {
                pxc_free_fqn(file_qn);
                return false;
            }
        } else if ((fact->kind == CBM_PY_NS_IMPORT_NAME || fact->kind == CBM_PY_NS_IMPORT_MODULE) &&
                   fact->local_name) {
            const cbm_gbuf_node_t *target =
                pxc_python_import_fact_node(pr, rel_path, file_qn, fact, depth, false);
            if ((target || fact->kind == CBM_PY_NS_IMPORT_NAME) &&
                !pxc_python_map_set(map, fact->local_name,
                                    target ? target->qualified_name : NULL)) {
                pxc_free_fqn(file_qn);
                return false;
            }
        } else if (fact->kind == CBM_PY_NS_IMPORT_STAR) {
            if (!pxc_python_apply_star(pr, rel_path, file_qn, fact, map, depth))
                pxc_python_map_block_all(map);
        } else if (pxc_python_shadow_binds_own(ctx, module_qn, facts, i)) {
            /* A class statement and an assignment bind the name to the module's
             * own definition: the class object, or the assigned value, which the
             * extractor records as a module-level node under this QN. Python
             * binds that node at this statement, so the entry holds it and only
             * replaces or creates a binding that already exists (an absent entry
             * resolves to the same node as a same-module name). When no node
             * carries the QN, the interpreter binds a value this resolver cannot
             * see; only a recorded block keeps a weaker name guess away. */
            int existing = pxc_python_map_find(map, fact->local_name);
            const cbm_gbuf_node_t *node = pxc_python_own_node(ctx, module_qn, fact->local_name);
            bool unseen_value = !node && fact->reason == CBM_PY_NS_REASON_VALUE;
            if ((collect_exports || existing >= 0 || unseen_value) &&
                !pxc_python_map_set(map, fact->local_name, node ? node->qualified_name : NULL)) {
                pxc_free_fqn(file_qn);
                return false;
            }
        } else if (fact->kind == CBM_PY_NS_SHADOW && fact->reason == CBM_PY_NS_REASON_OWN_OR_KEPT &&
                   fact->local_name) {
            if (!pxc_python_replay_own_or_kept(ctx, module_qn, fact, map)) {
                pxc_free_fqn(file_qn);
                return false;
            }
        } else if (fact->kind == CBM_PY_NS_SHADOW && fact->reason == CBM_PY_NS_REASON_UNSUPPORTED &&
                   fact->local_name) {
            /* A statement the facts do not model may rebind the name (an `if`
             * or `try` branch re-importing it, a loop or tuple target): the
             * binding an earlier statement proved no longer decides it. A name
             * that statement binds first keeps no entry, as before, and the
             * any-name SHADOW is left to the re-export reader
             * (pxc_python_final_binding). */
            if (strcmp(fact->local_name, CBM_PY_NS_ANY_NAME) != 0 &&
                pxc_python_map_find(map, fact->local_name) >= 0 &&
                !pxc_python_map_set(map, fact->local_name, NULL)) {
                pxc_free_fqn(file_qn);
                return false;
            }
        } else if ((fact->kind == CBM_PY_NS_SHADOW || fact->kind == CBM_PY_NS_DELETE) &&
                   fact->local_name) {
            if (!pxc_python_map_set(map, fact->local_name, NULL)) {
                pxc_free_fqn(file_qn);
                return false;
            }
        }
    }
    pxc_free_fqn(file_qn);
    return true;
}

/* The legacy CBMImport projection spells `from . import m2` as `..m2` and
 * cannot represent that m2 is a module binding. Ordered namespace facts own
 * this proof, so remove the contradictory compatibility row before the
 * parallel semantic gate reads it. */
static void pxc_python_repair_module_attribute_imports(const char *rel_path,
                                                       const CBMFileResult *result) {
    if (!rel_path || !result)
        return;
    CBMFileResult *mutable_result = (CBMFileResult *)(uintptr_t)result;
    for (int i = 0; i < result->py_namespace.count; i++) {
        const CBMPyNamespaceFact *fact = &result->py_namespace.items[i];
        if (fact->kind != CBM_PY_NS_IMPORT_NAME || !fact->local_name ||
            (fact->module_name && fact->module_name[0]) || fact->relative_level == 0)
            continue;
        for (int j = 0; j < mutable_result->imports.count;) {
            CBMImport *imp = &mutable_result->imports.items[j];
            if (!imp->local_name || strcmp(imp->local_name, fact->local_name) != 0) {
                j++;
                continue;
            }
            memmove(imp, imp + 1, (size_t)(mutable_result->imports.count - j - 1) * sizeof(*imp));
            mutable_result->imports.count--;
        }
    }
}

static int pxc_python_build_import_map(const cbm_pipeline_ctx_t *ctx, const char *project_name,
                                       const char *rel_path, const CBMFileResult *result,
                                       const char ***out_keys, const char ***out_vals,
                                       int *out_count) {
    PXCPythonImportMap map = {0};
    if (!result || result->py_namespace.status != CBM_PY_NS_COMPLETE ||
        !cbm_py_namespace_facts_valid(&result->py_namespace)) {
        return 0;
    }
    pxc_python_repair_module_attribute_imports(rel_path, result);
    char *module_qn = cbm_pipeline_fqn_module_dir(project_name, rel_path, false);
    PXCPythonResolver resolver = {.ctx = ctx, .project_name = project_name};
    bool ok = module_qn && pxc_python_replay_namespace(&resolver, rel_path, module_qn,
                                                       &result->py_namespace, &map, false, 0);
    pxc_python_resolver_free(&resolver);
    pxc_free_fqn(module_qn);
    if (!ok) {
        pxc_python_map_free(&map);
        return 0;
    }
    *out_keys = map.keys;
    *out_vals = map.vals;
    *out_count = map.count;
    return 0;
}

/* A bare name passed directly as a call argument (`accept(handler)`): the
 * extractor marks that callable-reference candidate and stamps the argument
 * itself as its site, so the site is exactly the name. An attribute argument
 * (`isinstance(self.tool, T)`) is stamped over the whole attribute, and a name
 * anywhere else is no candidate. Python attribute names carry no member flag,
 * so this shape is the one where the name is known to be a free name read in
 * the enclosing function. */
static bool pxc_python_bare_argument(const CBMUsage *usage) {
    return usage->may_be_call_reference && usage->site_end_byte > usage->site_start_byte &&
           (size_t)(usage->site_end_byte - usage->site_start_byte) == strlen(usage->ref_name);
}

/* A function body runs after the module's top-level run has bound its names,
 * so the file's final binding is the one it reads. A statement of the module's
 * own run (a class body included) reads the binding in force where it stands,
 * which the final binding does not give: `class C(x=f)` above `@d def f` reads
 * an unbound name. */
static bool pxc_python_in_function_body(const CBMFileResult *result, const CBMUsage *usage) {
    return usage->enclosing_func_qn && result->module_qn &&
           strcmp(usage->enclosing_func_qn, result->module_qn) != 0;
}

/* `@d def f` binds f to d(f). The import map withholds that name
 * (pxc_python_decorated_node_kept), so no CALLS, CALL_REFERENCE, WRITES,
 * INHERITS or RAISES edge can take the definition for the value. A bare name
 * passed as a call argument is still a reference to that definition: the
 * Python LSP fails a decorated value closed to ordinary USAGE instead of
 * CALL_REFERENCE (py_func_is_exact_callable_value), and the registry fallback
 * that publishes here only ever emits USAGE. This rewrites a blocked resolution
 * of such an argument to the file's own module-level Function when the last
 * statement that binds the name is that decorated definition and no star
 * import follows it. Any other occurrence shape, a class, and a name bound any
 * other way keep the blocked verdict and its unresolved row: the resolvers
 * behind them have no scope for attribute, class-body or local names. So does
 * a name some other binding may have replaced where it is read: a local
 * shadow, a loop, comprehension, `as` or walrus target of an enclosing scope
 * (`[accept(h) for h in xs]`), and any binding the extractor's lexical pass
 * saw in force there (`global h; h = 3`, or a module-level `h = 5` inside an
 * `if`), which the usage passes would otherwise drop without a row. */
bool cbm_pxc_python_decorated_usage(const cbm_pipeline_ctx_t *ctx, const char *project_name,
                                    const char *rel_path, CBMLanguage lang,
                                    const CBMFileResult *result, const CBMUsage *usage,
                                    cbm_resolution_t *res) {
    if (lang != CBM_LANG_PYTHON || !ctx || !result || !usage || !usage->ref_name || !res ||
        !res->strategy || strcmp(res->strategy, "import_map_blocked") != 0 ||
        usage->kind != CBM_USAGE_VALUE || usage->is_member_access ||
        usage->semantic_reference_local_shadow || usage->semantic_reference_blocked ||
        usage->python_target_shadow || !pxc_python_bare_argument(usage) ||
        !pxc_python_in_function_body(result, usage) ||
        result->py_namespace.status != CBM_PY_NS_COMPLETE ||
        !cbm_py_namespace_facts_valid(&result->py_namespace) ||
        !pxc_python_last_binder_is_decorated_def(&result->py_namespace, usage->ref_name))
        return false;
    char *module_qn = cbm_pipeline_fqn_module_dir(project_name, rel_path, false);
    const cbm_gbuf_node_t *node =
        module_qn ? pxc_python_own_node(ctx, module_qn, usage->ref_name) : NULL;
    pxc_free_fqn(module_qn);
    if (!node || !node->label || strcmp(node->label, "Function") != 0)
        return false;
    *res = (cbm_resolution_t){node->qualified_name, "python_decorated_definition", 0.95, 1};
    return true;
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
    if (lang == CBM_LANG_PYTHON)
        return pxc_python_build_import_map(ctx, project_name, rel_path, result, out_keys, out_vals,
                                           out_count);

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

    int metadata_count = lang == CBM_LANG_KOTLIN && result ? result->imports.count : 0;
    /* A completed PHP binding snapshot can establish a genuinely empty map.
     * The ordinary map remains a compatibility projection; only the separate
     * typed snapshot may prove that an import was left unresolved. */
    cbm_php_vendor_names_t php_bindings = {0};
    if (lang == CBM_LANG_PHP)
        cbm_pipeline_php_vendor_names_build(gbuf, project_name, rel_path, result, &php_bindings);
    bool authoritative_empty = php_bindings.targets != NULL;
    cbm_pipeline_php_vendor_names_free(&php_bindings);
    if (edge_count == 0 && (!result || result->imports.count == 0) && !authoritative_empty)
        return 0;

    size_t capacity = (size_t)edge_count + (result ? (size_t)result->imports.count : 0);
    if (capacity == 0)
        capacity = 1;
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
        char *value = pxc_kotlin_import_from_metadata(result, imp->local_name);
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

/* Replace the records of `src`'s exact parser occurrence with `src`. The
 * occurrence keeps ONE record: every earlier record there is superseded by
 * the same evidence, and rewriting each of them would leave identical copies
 * that the dedup table in pxc_append_results only indexes, never collapses. */
static void pxc_supersede_occurrence(CBMArena *dst_arena, CBMResolvedCallArray *dst_calls,
                                     const CBMResolvedCall *src) {
    bool replaced = false;
    int kept = 0;
    for (int i = 0; i < dst_calls->count; i++) {
        const CBMResolvedCall *cur = &dst_calls->items[i];
        bool same_site = cur->caller_qn && cur->kind == src->kind &&
                         cur->site_start_byte == src->site_start_byte &&
                         cur->site_end_byte == src->site_end_byte &&
                         cur->source_origin == src->source_origin &&
                         strcmp(cur->caller_qn, src->caller_qn) == 0;
        if (same_site && replaced) {
            continue; /* dropped: the occurrence already holds the evidence */
        }
        if (kept != i) {
            dst_calls->items[kept] = *cur;
        }
        CBMResolvedCall *dst = &dst_calls->items[kept++];
        if (!same_site) {
            continue;
        }
        replaced = true;
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
    dst_calls->count = kept;
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
     * preventing the stale local result from out-ranking manifest evidence.
     * PHP external-receiver evidence likewise supersedes a speculative local
     * row at that exact occurrence after complete typed import classification. */
    for (int j = 0; j < src_out->count; j++) {
        const CBMResolvedCall *src = &src_out->items[j];
        /* These project-registry refusals must also replace an earlier
         * file-local guess for the same invocation, rather than compete by
         * confidence with a target the complete scope no longer proves. */
        bool trait_stop = src->kind == CBM_RESOLVED_INVOCATION && src->reason && src->strategy &&
                          strcmp(src->strategy, "lsp_unresolved") == 0 &&
                          (strcmp(src->reason, "unrecognised_std_method") == 0 ||
                           strcmp(src->reason, "inherent_self_type_not_proven") == 0 ||
                           strcmp(src->reason, "trait_scope_identity") == 0);
        bool supersedes =
            trait_stop || (src->strategy && (strcmp(src->strategy, "lsp_cross_crate") == 0 ||
                                             strcmp(src->strategy, "lsp_module_path") == 0 ||
                                             strcmp(src->strategy, "php_external_receiver") == 0));
        if (!supersedes || !src->caller_qn || !src->callee_qn ||
            src->site_end_byte <= src->site_start_byte) {
            continue;
        }
        if (!trait_stop) {
            pxc_supersede_occurrence(dst_arena, dst_calls, src);
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
            /* A macro argument carrier can cover several different calls.
             * A trait stop closes only its method, never an inherent neighbour. */
            const char *old_leaf = cbm_lsp_bare_segment(dst->callee_qn);
            const char *new_leaf = cbm_lsp_bare_segment(src->callee_qn);
            size_t old_len = cbm_pipeline_lsp_method_leaf_len(old_leaf);
            size_t new_len = cbm_pipeline_lsp_method_leaf_len(new_leaf);
            if (!old_leaf || old_len != new_len || strncmp(old_leaf, new_leaf, old_len) != 0)
                continue;
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

static bool pxc_python_has_star_import(const CBMFileResult *result) {
    for (int i = 0; result && i < result->py_namespace.count; i++) {
        if (result->py_namespace.items[i].kind == CBM_PY_NS_IMPORT_STAR)
            return true;
    }
    return false;
}

static bool pxc_def_qn_exists(const CBMLSPDef *defs, int count, const char *qn) {
    for (int i = 0; defs && qn && i < count; i++) {
        if (defs[i].qualified_name && strcmp(defs[i].qualified_name, qn) == 0)
            return true;
    }
    return false;
}

/* Python's AST resolver deliberately invalidates every name at a star import
 * because it has no export oracle. The shared import-map builder does have
 * that oracle, so restore only exact bare invocations whose final binding is
 * a registered project definition. */
static void pxc_python_append_star_calls(CBMArena *arena, CBMFileResult *result,
                                         const CBMLSPDef *defs, int def_count,
                                         const char **imp_keys, const char **imp_vals,
                                         int imp_count, CBMResolvedCallArray *out) {
    if (!pxc_python_has_star_import(result))
        return;
    for (int i = 0; i < result->calls.count; i++) {
        CBMCall *call = &result->calls.items[i];
        if (!call->callee_name || !call->enclosing_func_qn || call->is_method ||
            strchr(call->callee_name, '.') || strstr(call->callee_name, "::") ||
            call->site_end_byte <= call->site_start_byte)
            continue;
        const char *target = NULL;
        bool blocked = false;
        for (int j = 0; j < imp_count; j++) {
            if (!imp_keys[j] || !imp_vals[j] || strcmp(imp_keys[j], call->callee_name) != 0)
                continue;
            if (!imp_vals[j][0]) {
                /* The star binds the name to nothing this resolver can name
                 * (an `__all__` exclusion, a re-export it cannot follow). Both
                 * CALLS drivers read this map, so the registry meets the empty
                 * value, withholds the call and records its row. */
                call->callee_is_locally_bound = false;
                blocked = true;
                break;
            }
            target = imp_vals[j];
            break;
        }
        if (blocked || call->callee_is_locally_bound)
            continue;
        if (!target || !pxc_def_qn_exists(defs, def_count, target))
            continue;
        bool already_resolved = false;
        for (int j = 0; j < out->count; j++) {
            const CBMResolvedCall *row = &out->items[j];
            already_resolved = row->kind == CBM_RESOLVED_INVOCATION &&
                               row->site_start_byte == call->site_start_byte &&
                               row->site_end_byte == call->site_end_byte && row->caller_qn &&
                               strcmp(row->caller_qn, call->enclosing_func_qn) == 0;
            if (already_resolved)
                break;
        }
        if (!already_resolved) {
            CBMResolvedCall row = {.caller_qn = call->enclosing_func_qn,
                                   .callee_qn = target,
                                   .strategy = "python_import_binding",
                                   .confidence = 0.95f,
                                   .kind = CBM_RESOLVED_INVOCATION,
                                   .site_start_byte = call->site_start_byte,
                                   .site_end_byte = call->site_end_byte,
                                   .source_origin = call->source_origin};
            cbm_resolvedcall_push(out, arena, row);
        }
    }
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
        out[i].impl_output = defs[i].impl_output;
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
        out[i].rust_impl_type_params = defs[i].rust_impl_type_params;
        out[i].rust_type_param_defaults = defs[i].rust_type_param_defaults;
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
    cbm_pxc_run_one_with_php_bindings(lang, r, source, source_len, module_qn, defs, def_count,
                                      imp_names, imp_qns, imp_count, NULL);
}

void cbm_pxc_run_one_with_php_bindings(CBMLanguage lang, CBMFileResult *r, const char *source,
                                       int source_len, const char *module_qn, CBMLSPDef *defs,
                                       int def_count, const char **imp_names, const char **imp_qns,
                                       int imp_count, const CBMPHPImportBindings *bindings) {
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
            CBMLSPStatus status =
                cbm_run_c_lsp_cross_status(&scratch, source, source_len, module_qn, cpp_mode, defs,
                                           def_count, NULL, NULL, 0, tree, &out);
            if (status != CBM_LSP_COMPLETE)
                cbm_c_lsp_record_failure(r, status);
        }
        break;
    }
    case CBM_LANG_PYTHON: {
        CBMLSPStatus status = cbm_run_py_lsp_cross_status(&scratch, source, source_len, module_qn,
                                                          defs, def_count, imp_names, imp_qns,
                                                          imp_count, tree, &out, &synthetic_calls);
        if (status != CBM_LSP_COMPLETE)
            cbm_py_lsp_record_failure(r, status);
    } break;
    case CBM_LANG_PHP:
        cbm_run_php_lsp_cross_with_bindings(&scratch, source, source_len, module_qn, defs,
                                            def_count, imp_names, imp_qns, imp_count, tree, &out,
                                            bindings);
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
            cbm_pxc_get_rust_import_source(), &out, &synthetic_calls, &r->usages, &impl_links);
        pxc_store_rust_impl_links(r, &impl_links);
        break;
    }
    default:
        break;
    }

    if (lang == CBM_LANG_PYTHON)
        pxc_python_append_star_calls(&scratch, r, defs, def_count, imp_names, imp_qns, imp_count,
                                     &out);
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
    cbm_pxc_dispatch_file_with_php_bindings(lang, result, source, source_len, rel, def_module,
                                            cross_registries, module_def_index, all_defs,
                                            all_def_count, imp_keys, imp_vals, imp_count,
                                            rust_shared_get, rust_shared_ctx, NULL);
}

void cbm_pxc_dispatch_file_with_php_bindings(
    CBMLanguage lang, CBMFileResult *result, const char *source, int source_len, const char *rel,
    const char *def_module, const CBMCrossLspRegistries *cross_registries,
    const CBMModuleDefIndex *module_def_index, CBMLSPDef *all_defs, int all_def_count,
    const char **imp_keys, const char **imp_vals, int imp_count,
    CBMTypeRegistry *(*rust_shared_get)(void *), void *rust_shared_ctx,
    const CBMPHPImportBindings *bindings) {
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
        case CBM_LANG_PYTHON: {
            CBMLSPStatus status = cbm_run_py_lsp_cross_with_registry_status(
                &scratch, source, source_len, def_module, &overlay, imp_keys, imp_vals, imp_count,
                result->cached_tree, &out, &synthetic_calls);
            if (status != CBM_LSP_COMPLETE)
                cbm_py_lsp_record_failure(result, status);
        }
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
                CBMLSPStatus status = cbm_run_c_lsp_cross_with_registry_status(
                    &scratch, source, source_len, def_module, (lang != CBM_LANG_C), &overlay,
                    imp_keys, imp_vals, imp_count, result->cached_tree, &out);
                if (status != CBM_LSP_COMPLETE)
                    cbm_c_lsp_record_failure(result, status);
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
            if (lang == CBM_LANG_PYTHON)
                pxc_python_append_star_calls(&scratch, result, all_defs, all_def_count, imp_keys,
                                             imp_vals, imp_count, &out);
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
                cbm_pxc_get_rust_import_source(), &out, &synthetic_calls, &result->usages,
                &impl_links);
            pxc_append_results(&result->arena, &result->resolved_calls, &out);
            pxc_store_rust_impl_links(result, &impl_links);
            pxc_append_synthetic_calls(&result->arena, &result->calls, &synthetic_calls);
            pxc_scratch_give(PXC_SCRATCH_DISPATCH, &scratch);
        } else {
            cbm_pxc_run_one_with_php_bindings(lang, result, source, source_len, def_module,
                                              file_defs, file_def_count, imp_keys, imp_vals,
                                              imp_count, bindings);
        }
    } else if (lang == CBM_LANG_JAVASCRIPT || lang == CBM_LANG_TYPESCRIPT || lang == CBM_LANG_TSX) {
        bool js;
        bool jsx;
        bool dts;
        cbm_pxc_ts_modes(lang, rel, &js, &jsx, &dts);
        cbm_pxc_run_one_ts(result, source, source_len, def_module, file_defs, file_def_count,
                           imp_keys, imp_vals, imp_count, js, jsx, dts);
    } else {
        cbm_pxc_run_one_with_php_bindings(lang, result, source, source_len, def_module, file_defs,
                                          file_def_count, imp_keys, imp_vals, imp_count, bindings);
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
                                  : strcmp(edition, "2021") == 0 ? CBM_RUST_EDITION_2021
                                  : strcmp(edition, "2024") == 0 ? CBM_RUST_EDITION_2024
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
        bool had_error = cache[i]->has_error;
        cbm_php_vendor_names_t php_bindings;
        cbm_pipeline_php_vendor_names_build(ctx->gbuf, ctx->project_name, files[i].rel_path,
                                            cache[i], &php_bindings);
        cbm_pxc_dispatch_file_with_php_bindings(
            lang, cache[i], source, source_len, files[i].rel_path, def_modules[i],
            &cross_registries, module_def_index, all_defs, def_count, imp_keys, imp_vals, imp_count,
            NULL, NULL, &php_bindings);
        cbm_pipeline_php_vendor_names_free(&php_bindings);
        cbm_index_mark_done(files[i].rel_path);
        if (!had_error && cache[i]->has_error && ctx->pipeline)
            cbm_pipeline_add_file_error(ctx->pipeline, files[i].rel_path, cache[i]->error_msg,
                                        "lsp_skipped");
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
#ifdef CBM_ENABLE_TEST_SEAMS
static _Thread_local int pxc_import_probe_allocations_left = -1;
void cbm_pxc_test_import_probe_fail_after(int successful_allocations) {
    pxc_import_probe_allocations_left = successful_allocations;
}
#endif

static bool pxc_mark_import_defs(const CBMModuleDefIndex *idx, bool *selected,
                                 const CBMLSPDef *all_defs, CBMLanguage caller_lang,
                                 const char *import_qn, int *total) {
    if (!idx || !idx->ht || !import_qn || !import_qn[0]) {
        return true;
    }
    static const char init_seg[] = ".__init__";
    size_t import_len = strlen(import_qn);
    if (import_len > SIZE_MAX - sizeof(init_seg))
        return false;
#ifdef CBM_ENABLE_TEST_SEAMS
    if (pxc_import_probe_allocations_left == 0)
        return false;
    if (pxc_import_probe_allocations_left > 0)
        pxc_import_probe_allocations_left--;
#endif
    char *candidate = (char *)cbm_alloc(CBM_MEM_CLASS_OTHER, import_len + sizeof(init_seg));
    if (!candidate)
        return false;
    memcpy(candidate, import_qn, import_len + 1);
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
        if (caller_lang == CBM_LANG_PYTHON) {
            size_t cand_len = strlen(candidate);
            memcpy(candidate + cand_len, init_seg, sizeof(init_seg));
            entry = (pxc_module_entry_t *)cbm_ht_get(idx->ht, candidate);
            candidate[cand_len] = '\0';
            /* A suffixed module may be a non-Python directory named __init__.
             * Only this new probe filters strictly; normal-entry precedence
             * above remains unchanged. Eligibility is independent of dedup. */
            bool eligible = false;
            for (int j = 0; entry && j < entry->count; j++) {
                int di = entry->indices[j];
                if (all_defs[di].lang != CBM_LANG_PYTHON)
                    continue;
                eligible = true;
                if (!selected[di]) {
                    selected[di] = true;
                    if (total)
                        (*total)++;
                }
            }
            if (eligible) {
                matched = true;
                break;
            }
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
    cbm_free(CBM_MEM_CLASS_OTHER, candidate);
    return true;
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
        if (!pxc_mark_import_defs(idx, selected, all_defs, caller_lang, imp_qns[i], &total)) {
            goto failed; /* out_success remains false: dispatcher uses all_defs */
        }
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
        goto failed;
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
failed:
    cbm_free(CBM_MEM_CLASS_EXTRACT, selected);
    return NULL;
}
