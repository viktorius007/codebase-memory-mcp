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
#include "result_spill.h"
#include "pipeline/pipeline_internal.h"
#include "pipeline/lsp_resolve.h"
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
    out->tree_digest =
        source && tree ? cbm_rust_modtree_file_digest(source, ts_tree_root_node(tree)) : 0;
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
        strcmp(label, "Variable") == 0) {
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

/* Resolve one base-class spelling to a project QN. Mirrors
 * pass_semantic.c::resolve_as_class — same registry, same type-like veto —
 * then additionally rejects weak short-name strategies (see above).
 * Returns NULL when the base is not a confidently-known project type;
 * stdlib and third-party bases land here and keep their raw spelling. */
static const char *pxc_resolve_base_qn(const cbm_registry_t *reg, const char *raw,
                                       const char *module_qn, const char **imp_keys,
                                       const char **imp_vals, int imp_count) {
    if (!reg || !raw || !raw[0]) {
        return NULL;
    }
    cbm_resolution_t res = cbm_registry_resolve(reg, raw, module_qn, imp_keys, imp_vals, imp_count);
    if (!res.qualified_name || !res.qualified_name[0]) {
        return NULL;
    }
    if (pxc_base_strategy_is_weak(res.strategy)) {
        return NULL;
    }
    if (!cbm_label_is_type_like(cbm_registry_label_of(reg, res.qualified_name))) {
        return NULL;
    }
    return res.qualified_name;
}

/* pxc_join_pipe over base_classes, substituting each resolved QN for its
 * source spelling. Unresolved entries pass through verbatim so a base the
 * registry does not know keeps working exactly as before. */
static const char *pxc_join_base_qns(CBMArena *arena, const char *const *bases,
                                     const cbm_registry_t *reg, const char *module_qn,
                                     const char **imp_keys, const char **imp_vals, int imp_count) {
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
        const char *qn =
            pxc_resolve_base_qn(reg, bases[i], module_qn, imp_keys, imp_vals, imp_count);
        resolved[i] = qn ? qn : bases[i];
    }
    resolved[count] = NULL;
    return pxc_join_pipe(arena, resolved);
}

static bool pxc_is_jvm_lang(CBMLanguage lang);

static const char *pxc_last_component(const char *qn) {
    if (!qn) {
        return NULL;
    }
    const char *dot = strrchr(qn, '.');
    return dot ? dot + 1 : qn;
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
    for (const char *p = name; *p; p++) {
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
                             const cbm_registry_t *reg, const char **imp_keys,
                             const char **imp_vals, int imp_count) {
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
    dst->is_interface = (strcmp(label, "Interface") == 0 || strcmp(label, "Protocol") == 0);
    /* Single return-type string. The per-language registrars split on '|'
     * for multi-return languages (Go); single-return languages just see one
     * piece, which is what's already stored. */
    dst->return_types = src->return_type ? cbm_arena_strdup(arena, src->return_type) : NULL;
    /* Languages whose cross registrars read embedded_types as QNs get their
     * bases resolved against the project registry; everyone else keeps the
     * raw source spelling their own registrar already knows how to handle. */
    dst->embedded_types = (reg && pxc_lang_resolves_base_qns(lang))
                              ? pxc_join_base_qns(arena, src->base_classes, reg, module_qn,
                                                  imp_keys, imp_vals, imp_count)
                              : pxc_join_pipe(arena, src->base_classes);
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
    }
    return 0;
}

/* Go: fold per-field "Field" definitions into their owning struct's
 * field_defs. extract_defs.c emits one flat CBMDefinition per struct field
 * (label "Field", parent_class = owning struct QN, name = field name,
 * return_type = raw type text). Those rows are dropped by pxc_build_lsp_def
 * (pxc_map_label excludes "Field"), so without this fold every Go struct
 * registers with zero fields and field-chain calls (h.svc.Handle) can
 * never resolve. Fields are always declared in the same file as their struct,
 * so scanning the file's own defs covers every case. Runs inside
 * cbm_pxc_collect_all_defs — one site covers both the prebuilt-registry path
 * and the per-file fallback, since both consume all_defs. */
static void pxc_fold_go_struct_fields(CBMArena *arena, const CBMFileResult *result, CBMLSPDef *defs,
                                      int start, int end) {
    if (!arena || !result || !defs || start >= end) {
        return;
    }
    for (int si = start; si < end; si++) {
        CBMLSPDef *dst = &defs[si];
        if (!dst->label || strcmp(dst->label, "Struct") != 0 || !dst->qualified_name) {
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
    dst->lang = CBM_LANG_RUST;
    dst->is_rust_impl_relation = true;
    return 0;
}

/* Collect a project-wide CBMLSPDef[] from all cached results. Returns a
 * malloc'd array (caller frees) of length *out_count. String fields are
 * borrowed from cache[i]->arena and from def_modules[i] (also borrowed). */
CBMLSPDef *cbm_pxc_collect_all_defs(const cbm_pipeline_ctx_t *ctx, CBMArena *arena,
                                    CBMFileResult **cache, const cbm_file_info_t *files,
                                    int file_count, const char *project_name, char **def_modules,
                                    int *out_count, int *out_def_starts) {
    const cbm_result_spill_t *spill = ctx ? ctx->spill : NULL;
    int total = 0;
    for (int i = 0; i < file_count; i++) {
        int defs = 0;
        int impls = 0;
        if (cache[i]) {
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
            fr = cbm_result_spill_load(spill, fi);
            fr_loaded = fr != NULL;
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
        const cbm_registry_t *base_reg = NULL;
        const char **imp_keys = NULL;
        const char **imp_vals = NULL;
        int imp_count = 0;
        if (ctx && ctx->registry && pxc_lang_resolves_base_qns(files[fi].language)) {
            base_reg = ctx->registry;
            cbm_pxc_build_import_map(ctx->gbuf, project_name, files[fi].rel_path,
                                     files[fi].language, fr, &imp_keys, &imp_vals, &imp_count);
        }
        for (int di = 0; di < fr->defs.count; di++) {
            if (pxc_build_lsp_def(arena, &fr->defs.items[di], def_modules[fi], namespace_name,
                                  files[fi].language, &defs[idx], base_reg, imp_keys, imp_vals,
                                  imp_count) == 0) {
                idx++;
            }
        }
        cbm_pxc_free_import_map(imp_keys, imp_vals, imp_count); /* NULL-safe */
        /* Rust struct fields are declared beside their struct exactly like
         * Go's, and `self.field.method()` needs them for the same reason. */
        if (files[fi].language == CBM_LANG_GO || files[fi].language == CBM_LANG_RUST) {
            pxc_fold_go_struct_fields(arena, fr, defs, file_start, idx);
        }
        if (files[fi].language == CBM_LANG_RUST) {
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
    return strdup(path);
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
        return strdup(target->qualified_name);

    bool ambiguous = false;
    const char *path = pxc_unique_import_path(result, local_name, &ambiguous);
    if (ambiguous)
        return NULL;
    const char *source_leaf = pxc_import_leaf(path);
    const char *target_leaf = pxc_import_leaf(target->qualified_name);
    if (!path || !source_leaf || !target_leaf || strcmp(source_leaf, local_name) != 0 ||
        strcmp(source_leaf, target_leaf) == 0 || !target->label ||
        (strcmp(target->label, "Module") != 0 && strcmp(target->label, "Folder") != 0)) {
        return strdup(target->qualified_name);
    }

    size_t need = strlen(target->qualified_name) + 1 + strlen(source_leaf) + 1;
    char *qualified = (char *)malloc(need);
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
        return strdup(exact->qualified_name);

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
            char *qualified = (char *)malloc(need);
            if (!qualified)
                return NULL;
            snprintf(qualified, need, "%s.%s", module_node->qualified_name, source_leaf);
            return qualified;
        }
    }

    if (!project_name || !project_name[0])
        return strdup(module_path);
    size_t need = strlen(project_name) + 1 + strlen(module_path) + 1;
    char *qualified = (char *)malloc(need);
    if (!qualified)
        return NULL;
    snprintf(qualified, need, "%s.%s", project_name, module_path);
    return qualified;
}

/* Build per-file import map (local_name -> semantic import QN) from gbuf
 * IMPORTS edges. Both pipeline drivers call this implementation. Returns 0
 * with *out_count = 0 when the file has no IMPORTS edges. */
int cbm_pxc_build_import_map(const cbm_gbuf_t *gbuf, const char *project_name, const char *rel_path,
                             CBMLanguage lang, const CBMFileResult *result, const char ***out_keys,
                             const char ***out_vals, int *out_count) {
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
    if (edge_count == 0 && metadata_count == 0)
        return 0;

    size_t capacity = (size_t)edge_count + (size_t)metadata_count;
    const char **keys = (const char **)calloc(capacity, sizeof(const char *));
    const char **vals = (const char **)calloc(capacity, sizeof(const char *));
    if (!keys || !vals) {
        free(keys);
        free(vals);
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
        char *local = (char *)malloc(n + 1);
        if (!local)
            continue;
        memcpy(local, start, n);
        local[n] = '\0';
        char *value = pxc_import_value_qn(lang, result, local, target);
        if (!value) {
            free(local);
            continue;
        }
        keys[count] = local;
        vals[count] = value;
        count++;
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
        char *local = strdup(imp->local_name);
        if (!local) {
            free(value);
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
            free((void *)keys[i]);
        free((void *)keys);
    }
    if (vals) {
        for (int i = 0; i < count; i++)
            free((void *)vals[i]);
        free((void *)vals);
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
    cbm_arena_init(&keys);
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
    cbm_arena_destroy(&keys);
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
    cbm_arena_init(&keys);
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
    cbm_arena_destroy(&keys);
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
                          r->kind == CBM_RESOLVED_INVOCATION ? "invocation" : "reference",
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
    if (!ctx || !ctx->rust_imports) {
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

void cbm_pxc_set_rust_imports(const CBMHashTable *imports, const CBMRustModTree *modtree) {
    g_pxc_rust_imports.lookup = imports ? pxc_rust_import_lookup : NULL;
    g_pxc_rust_imports.ctx = (void *)imports;
    g_pxc_rust_imports.modtree = modtree;
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
        out[i].decorators = defs[i].decorators;
        out[i].is_interface = defs[i].is_interface;
        out[i].is_rust_impl_relation = defs[i].is_rust_impl_relation;
        out[i].is_abstract = defs[i].is_abstract;
        out[i].is_async = defs[i].is_async;
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
    cbm_arena_init(&scratch);
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
        cbm_run_c_lsp_cross(&scratch, source, source_len, module_qn, cpp_mode, defs, def_count,
                            NULL, NULL, 0, tree, &out);
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
    cbm_arena_destroy(&scratch);
}

/* Variant of cbm_pxc_run_one for TS/JS/JSX/TSX with explicit dialect
 * flags. Same scratch-arena lifecycle as cbm_pxc_run_one. */
void cbm_pxc_run_one_ts(CBMFileResult *r, const char *source, int source_len, const char *module_qn,
                        CBMLSPDef *defs, int def_count, const char **imp_names,
                        const char **imp_qns, int imp_count, bool js_mode, bool jsx_mode,
                        bool dts_mode) {
    CBMArena scratch;
    cbm_arena_init(&scratch);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));

    cbm_run_ts_lsp_cross(&scratch, source, source_len, module_qn, js_mode, jsx_mode, dts_mode, defs,
                         def_count, imp_names, imp_qns, imp_count, r->cached_tree, &out);

    pxc_append_results(&r->arena, &r->resolved_calls, &out);
    cbm_arena_destroy(&scratch);
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
        cbm_arena_init(&scratch);
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
            cbm_run_c_lsp_cross_with_registry(&scratch, source, source_len, def_module,
                                              (lang != CBM_LANG_C), &overlay, imp_keys, imp_vals,
                                              imp_count, result->cached_tree, &out);
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
        cbm_arena_destroy(&scratch);
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
            cbm_arena_init(&scratch);
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
            cbm_arena_destroy(&scratch);
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

static bool pxc_rust_edition_2015(const CBMCargoManifest *manifest, const char *inherited) {
    const char *edition = manifest->edition_inherited ? inherited : manifest->edition;
    return !edition || strcmp(edition, "2015") == 0;
}

static void pxc_rust_add_package_crates(PXCRustCrates *crates, PXCRustPackage *package,
                                        const CBMCargoManifest *owners, const char *inherited,
                                        CBMRustModTree *tree) {
    CBMCargoManifest *m = package->manifest;
    pxc_cargo_discover_targets(crates->arena, owners, m, package->dir, crates->files,
                               crates->file_count, inherited);
    bool e2015 = pxc_rust_edition_2015(m, inherited);
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
            tree, pxc_rel_join(crates->arena, package->dir, target->path), name, e2015);
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
    const int *file_index;   /* modtree file id -> files[] index */
    CBMRustModFacts **facts; /* modtree file id -> scanned facts */
} PXCRustScanCtx;

/* cbm_parallel_for body: read and scan one registered file. */
static void pxc_rust_scan_file(int file_id, void *arg) {
    PXCRustScanCtx *scan = arg;
    int len = 0;
    char *source = pxc_read_file(scan->files[scan->file_index[file_id]].path, &len);
    if (source) {
        scan->facts[file_id] = cbm_rust_modfacts_scan(source, len);
        cbm_free(CBM_MEM_CLASS_EXTRACT, source);
    }
}

/* Read and parse every registered file across worker threads (parsing is
 * the tree's whole cost), then build the tree from the facts in one thread,
 * so the result does not depend on scheduling. */
static void pxc_rust_finish_modtree(CBMRustModTree *tree, const cbm_file_info_t *files,
                                    const int *file_index) {
    int count = cbm_rust_modtree_file_count(tree);
    CBMRustModFacts **facts =
        cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)(count > 0 ? count : 1) * sizeof(*facts));
    if (!facts) {
        return;
    }
    PXCRustScanCtx scan = {.files = files, .file_index = file_index, .facts = facts};
    cbm_parallel_for(count, pxc_rust_scan_file, &scan, (cbm_parallel_for_opts_t){0});
    cbm_rust_modtree_finish(tree, (const CBMRustModFacts *const *)facts);
    for (int i = 0; i < count; i++) {
        cbm_rust_modfacts_free(facts[i]);
    }
    cbm_free(CBM_MEM_CLASS_OTHER, facts);
}

CBMRustModTree *cbm_pxc_build_rust_modtree(const cbm_pipeline_ctx_t *ctx,
                                           const CBMCargoManifest *root,
                                           const cbm_file_info_t *files, int file_count) {
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
    pxc_rust_finish_modtree(tree, files, file_index);
    cbm_free(CBM_MEM_CLASS_OTHER, crates.packages);
    cbm_arena_destroy(&arena);
    cbm_free(CBM_MEM_CLASS_OTHER, file_index);
    return tree;
}

int cbm_pipeline_pass_lsp_cross(cbm_pipeline_ctx_t *ctx, const cbm_file_info_t *files,
                                int file_count, CBMFileResult **cache) {
    if (!ctx || !files || file_count <= 0 || !cache)
        return 0;

    cbm_log_info("pass.start", "pass", "lsp_cross", "files", itoa_buf(file_count));

    /* Build the Rust workspace manifest once (only when the project has at
     * least one Rust file, to avoid an unconditional Cargo.toml read).
     * The manifest's strings live in `cargo_arena`; the resolver borrows
     * the pointer through the file-static set below. */
    bool have_rust = false;
    for (int i = 0; i < file_count; i++) {
        if (cache[i] && files[i].language == CBM_LANG_RUST) {
            have_rust = true;
            break;
        }
    }
    CBMArena cargo_arena;
    CBMCargoManifest cargo_manifest;
    bool have_manifest = false;
    if (have_rust) {
        cbm_arena_init(&cargo_arena);
        have_manifest =
            cbm_pxc_build_rust_manifest(ctx, &cargo_arena, &cargo_manifest, files, file_count);
        cbm_pxc_set_rust_manifest(have_manifest ? &cargo_manifest : NULL);
        cbm_pxc_set_rust_imports(ctx->rust_imports, NULL);
    }

    /* Per-file module QN cache so we don't recompute it once per def + once
     * per call. cbm_pipeline_fqn_module mallocs; freed at end. */
    char **def_modules = (char **)calloc((size_t)file_count, sizeof(char *));
    if (!def_modules) {
        cbm_log_error("pass.err", "pass", "lsp_cross", "phase", "alloc");
        return 0;
    }

    int def_count = 0;
    int *def_starts = (int *)calloc((size_t)file_count + 1, sizeof(int));
    /* The defs own their strings in the caller-owned seq_cross_arena, which
     * the registries and later passes already borrow from (see below). */
    if (!ctx->seq_cross_arena_live) {
        cbm_arena_init(&ctx->seq_cross_arena);
        ctx->seq_cross_arena_live = true;
    }
    CBMLSPDef *all_defs =
        cbm_pxc_collect_all_defs(ctx, &ctx->seq_cross_arena, cache, files, file_count,
                                 ctx->project_name, def_modules, &def_count, def_starts);
    CBMRustModTree *rust_modtree = NULL;
    if (have_rust) {
        rust_modtree = cbm_pxc_build_rust_modtree(ctx, have_manifest ? &cargo_manifest : NULL,
                                                  files, file_count);
        cbm_pxc_set_rust_imports(ctx->rust_imports, rust_modtree);
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
    free(def_starts);

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
        CBMLanguage lang = files[i].language;
        if (!cbm_pxc_has_cross_lsp(lang)) {
            skipped_no_lsp++;
            continue;
        }

        int source_len = 0;
        char *source = pxc_read_file(files[i].path, &source_len);
        if (!source || source_len <= 0) {
            cbm_free(CBM_MEM_CLASS_EXTRACT, source);
            skipped_no_source++;
            continue;
        }

        if (!def_modules[i]) {
            def_modules[i] = cbm_pipeline_fqn_module_dir(ctx->project_name, files[i].rel_path,
                                                         pxc_module_is_dir(files[i].language));
        }

        const char **imp_keys = NULL;
        const char **imp_vals = NULL;
        int imp_count = 0;
        cbm_pxc_build_import_map(ctx->gbuf, ctx->project_name, files[i].rel_path, lang, cache[i],
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
        cbm_pxc_set_rust_imports(NULL, NULL);
        cbm_rust_modtree_free(rust_modtree);
        cbm_arena_destroy(&cargo_arena);
    }
    (void)have_manifest;

    cbm_log_info("pass.done", "pass", "lsp_cross", "files_processed", itoa_buf(processed),
                 "files_skipped_no_lsp", itoa_buf(skipped_no_lsp), "files_skipped_no_source",
                 itoa_buf(skipped_no_source), "defs_total", itoa_buf(def_count), "lsp_calls",
                 itoa_buf(per_lang_calls));
    return 0;
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
        (bool *)cbm_calloc(CBM_MEM_CLASS_OTHER, (size_t)idx->def_count * sizeof(*selected));
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
        cbm_free(CBM_MEM_CLASS_OTHER, selected);
        *out_success = true;
        return NULL;
    }

    CBMLSPDef *out = (CBMLSPDef *)malloc((size_t)total * sizeof(CBMLSPDef));
    if (!out) {
        cbm_free(CBM_MEM_CLASS_OTHER, selected);
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
    cbm_free(CBM_MEM_CLASS_OTHER, selected);
    return out;
}
