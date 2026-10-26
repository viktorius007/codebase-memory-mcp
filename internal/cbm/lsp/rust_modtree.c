/* rust_modtree.c — Rust crate module tree and path name resolution.
 * See rust_modtree.h for the contract. */
#include "rust_modtree.h"
#include "rust_lsp.h"
#include "lsp_neg_memo.h"
#include "../helpers.h"
#include "../../../src/foundation/mem_core.h"
#include <stdio.h>
#include <string.h>

extern const TSLanguage *tree_sitter_rust(void);

enum {
    RMT_NS_TYPE = 1,
    RMT_NS_VALUE = 2,
    RMT_NS_MACRO = 4,
    RMT_NS_ALL = 7,
    RMT_MAX_DEFS = 4,
    RMT_MAX_SEGS = 32,
    RMT_MAX_DEPTH = 48,
    RMT_VISIT_MAX = 64,
    RMT_VISIT_SLOTS = 1024, /* power of two; a walk stops at half */
    RMT_KEY_MAX = 512,
};

typedef enum {
    RMT_MODULE = 1,
    RMT_FN,
    RMT_TYPE,
    RMT_TRAIT,
    RMT_ENUM,
    RMT_ALIAS, /* `type X = ..;`: names another type, owns no impls */
    RMT_VALUE,
    RMT_MACRO,
    RMT_EXTERNAL, /* an item of a crate outside the project, or of the language; its
                   * qn is the dotted path it was reached by, NULL when unknown */
    RMT_OPAQUE,   /* declared but not modelled (an unloadable `mod x;`) */
} RmtKind;

typedef struct {
    uint8_t kind;
    uint8_t ns;
    int32_t module;
    /* RMT_FN: 1-based line of the declaring `fn`, 0 when two declarations
     * of one QN merged (cfg twins in one module) */
    uint32_t line;
    uint32_t start;
    bool conditional;
    const char *qn;
    const char *value_type; /* RMT_VALUE: a `const`/`static` item's declared type as written */
} RmtDef;

typedef struct {
    int count;
    bool open; /* some source of the name could not be decided */
    RmtDef defs[RMT_MAX_DEFS];
    const char *assoc; /* set when the path ends in an associated item of defs[0] */
} RmtDefs;

/* Who may name an item or import: every module (RMT_VIS_PUB), or the
 * modules at or below `vis` (a module id). Facts carry it unresolved: a
 * count of levels above the declaring module, or RMT_VIS_PUB/RMT_VIS_CRATE. */
enum { RMT_VIS_PUB = -1, RMT_VIS_CRATE = -2 };

typedef struct {
    int next;
    int module;
    const char *name;
    int import; /* -1 for a declared item */
    int vis;
    RmtDef def;
} RmtEntry;

typedef enum { RMT_IMPORT_PENDING = 0, RMT_IMPORT_ACTIVE, RMT_IMPORT_DONE } RmtImportState;

typedef struct {
    int module;
    int next_glob;
    int vis;
    const char **segs;
    int seg_count;
    bool glob;
    bool conditional;
    bool extern_crate;
    uint8_t state;
    RmtDefs result;
} RmtImport;

typedef struct {
    int crate;
    int parent;
    int file;
    int next_in_file;
    int first_glob;
    const char *qn;
    uint32_t start;
    uint32_t end;
    bool macro_items; /* item-position macro calls may declare unseen names */
    uint8_t prelude_restrictions;
} RmtModule;

typedef struct {
    const char *root_rel;
    const char *extern_name;
    CBMRustEdition edition;
    int root_module;
    const char **features; /* enabled Cargo features; NULL: not known */
    int feature_count;
} RmtCrate;

typedef struct {
    const char *rel;
    const char *qn;
    const char *dir;
    int first_module;
    uint8_t prelude_restrictions;
} RmtFile;

/* `type name = value;` written in `impl Trait for Self { .. }` of a module,
 * whose self type is a path. `self_qn` is the project type the path names,
 * filled when the tree is finished; `next` chains the entries of one
 * (self QN, name) key. `unreadable` marks an impl with type or const
 * parameters, or an associated type declared with type parameters: what it
 * writes depends on bindings this record does not carry. */
typedef struct {
    int module;
    const char *self_path; /* the self type's path, without arguments */
    const char *self_text; /* the self type as written, arguments included */
    const char *name;
    const char *value;
    const char *self_qn;
    bool unreadable;
    int next;
} RmtImplAssoc;

/* Read-only lookup over a finished tree for trait-in-scope queries. `bound`
 * groups the entries such a query can accept (not named `_`, binding a type
 * with a QN) by the trait identity rmt_trait_identity_equal compares; `anon`
 * groups the `_` imports by module. Both keep ascending entry ids. Not ready
 * while the tree is unfinished, after an import was left undecided, or when
 * memory ran out: queries then scan every entry. */
typedef struct {
    bool ready;
    int entry_count; /* the tree's counts when the index was built */
    int module_count;
    CBMIdxMemo plain;  /* trait QN -> group */
    CBMIdxMemo facade; /* std/core/alloc path after the crate -> group */
    int *bound_start;  /* group g owns bound_ids[bound_start[g] .. bound_start[g + 1]) */
    int *bound_ids;
    int *anon_start; /* module m owns anon_ids[anon_start[m] .. anon_start[m + 1]) */
    int *anon_ids;
} RmtTraitIndex;

struct CBMRustModTree {
    CBMArena arena;
    RmtFile *files;
    int file_count, file_cap;
    RmtCrate *crates;
    int crate_count, crate_cap;
    RmtModule *modules;
    int module_count, module_cap;
    RmtEntry *entries;
    int entry_count, entry_cap;
    RmtImport *imports;
    int import_count, import_cap;
    CBMIdxMemo file_by_rel;
    CBMIdxMemo file_by_qn;
    CBMIdxMemo names;     /* "<module>\x1f<name>" -> first entry */
    CBMIdxMemo externs;   /* "<crate>\x1f<name>" -> target crate + 1, 0 = outside project */
    CBMIdxMemo use_heads; /* "<module>\x1f<head>" -> 1 when a `use` declared there starts so */
    RmtTraitIndex trait_index;
    RmtImplAssoc *impl_assocs; /* associated types written in trait impls */
    int impl_assoc_count, impl_assoc_cap;
    CBMIdxMemo impl_assoc_by_self; /* "<self QN>\x1f<name>" -> first impl_assocs entry */
};

/* Modules one name lookup has entered through globs. A facade crate's
 * prelude reaches hundreds of modules (`bevy::prelude::*` globs every
 * sub-crate's prelude, and those glob their own modules), so the set is
 * hashed; `full` records that the walk stopped before covering them all. */
typedef struct {
    int slots[RMT_VISIT_SLOTS]; /* module id + 1; 0 is an empty slot */
    int count;
    bool full;
} RmtVisit;

/* Test builds scribble every grown slot: a field a record setter forgets then
 * reads as garbage in every test run, not as whatever zero or stale byte the
 * allocator happened to hand back (RmtFile.prelude_restrictions read leftover
 * heap bytes, and only under load did they disable the prelude). */
#ifdef CBM_ENABLE_TEST_SEAMS
#define RMT_SCRIBBLE(next, old_cap, grown, size)                                         \
    memset((char *)(next) + (size_t)(old_cap) * (size), 0xFF,                            \
           (size_t)((grown) - (old_cap)) * (size))
#else
#define RMT_SCRIBBLE(next, old_cap, grown, size) ((void)0)
#endif

#define RMT_GROW(tree, field, count, cap)                                     \
    do {                                                                      \
        if ((tree)->count >= (tree)->cap) {                                   \
            int grown = (tree)->cap ? (tree)->cap * 2 : 64;                   \
            void *next = cbm_realloc(CBM_MEM_CLASS_OTHER, (tree)->field,      \
                                     (size_t)grown * sizeof(*(tree)->field)); \
            if (!next)                                                        \
                return -1;                                                    \
            RMT_SCRIBBLE(next, (tree)->cap, grown, sizeof(*(tree)->field));   \
            (tree)->field = next;                                             \
            (tree)->cap = grown;                                              \
        }                                                                     \
    } while (0)

/* ── Construction ─────────────────────────────────────────────────── */

CBMRustModTree *cbm_rust_modtree_new(void) {
    CBMRustModTree *tree = cbm_calloc(CBM_MEM_CLASS_OTHER, sizeof(*tree));
    if (tree) {
        cbm_arena_init(&tree->arena);
    }
    return tree;
}

static void rmt_trait_index_free(RmtTraitIndex *index) {
    cbm_free(CBM_MEM_CLASS_OTHER, index->bound_start);
    cbm_free(CBM_MEM_CLASS_OTHER, index->bound_ids);
    cbm_free(CBM_MEM_CLASS_OTHER, index->anon_start);
    cbm_free(CBM_MEM_CLASS_OTHER, index->anon_ids);
    *index = (RmtTraitIndex){0}; /* memo slots live in the tree arena */
}

void cbm_rust_modtree_free(CBMRustModTree *tree) {
    if (!tree) {
        return;
    }
    rmt_trait_index_free(&tree->trait_index);
    cbm_free(CBM_MEM_CLASS_OTHER, tree->files);
    cbm_free(CBM_MEM_CLASS_OTHER, tree->crates);
    cbm_free(CBM_MEM_CLASS_OTHER, tree->modules);
    cbm_free(CBM_MEM_CLASS_OTHER, tree->entries);
    cbm_free(CBM_MEM_CLASS_OTHER, tree->imports);
    cbm_free(CBM_MEM_CLASS_OTHER, tree->impl_assocs);
    cbm_arena_destroy(&tree->arena);
    cbm_free(CBM_MEM_CLASS_OTHER, tree);
}

static uint8_t rmt_scope_prelude_restrictions(const CBMRustModTree *tree, int module) {
    uint8_t restrictions = 0;
    for (int parent = module; parent >= 0; parent = tree->modules[parent].parent)
        restrictions |= tree->modules[parent].prelude_restrictions;
    return restrictions;
}

static bool rmt_implicit_extern_allowed(const CBMRustModTree *tree, int module, const char *name) {
    uint8_t restrictions = rmt_scope_prelude_restrictions(tree, module);
    if (restrictions & CBM_RUST_PRELUDE_DISABLED)
        return false;
    if (!(restrictions & CBM_RUST_PRELUDE_CORE_ONLY) || strcmp(name, "std") != 0)
        return true;
    /* no_std omits the automatic std entry; an explicit root extern-crate
     * declaration still adds that name to the extern prelude. */
    char key[RMT_KEY_MAX];
    int root = tree->crates[tree->modules[module].crate].root_module;
    int written = snprintf(key, sizeof(key), "%d\x1f%s", root, name);
    for (int e = written > 0 && written < (int)sizeof(key)
                     ? cbm_idxmemo_get(&tree->names, key) : -1;
         e >= 0; e = tree->entries[e].next) {
        int import = tree->entries[e].import;
        if (import >= 0 && tree->imports[import].extern_crate)
            return true;
    }
    return false;
}

static const char *rmt_dirname(CBMArena *arena, const char *rel) {
    const char *slash = strrchr(rel, '/');
    return slash ? cbm_arena_strndup(arena, rel, (size_t)(slash - rel)) : "";
}

int cbm_rust_modtree_add_file(CBMRustModTree *tree, const char *rel_path, const char *module_qn) {
    if (!tree || !rel_path || !module_qn) {
        return -1;
    }
    RMT_GROW(tree, files, file_count, file_cap);
    int id = tree->file_count++;
    RmtFile *file = &tree->files[id];
    /* The whole record: a field left out keeps the grown array's bytes
     * (prelude_restrictions did, and leftover bits disabled the prelude). */
    const char *rel = cbm_arena_strdup(&tree->arena, rel_path);
    *file = (RmtFile){
        .rel = rel,
        .qn = cbm_arena_strdup(&tree->arena, module_qn),
        .dir = rmt_dirname(&tree->arena, rel),
        .first_module = -1,
    };
    cbm_idxmemo_put_if_absent(&tree->file_by_rel, &tree->arena, file->rel, id);
    cbm_idxmemo_put_if_absent(&tree->file_by_qn, &tree->arena, file->qn, id);
    return id;
}

int cbm_rust_modtree_add_crate(CBMRustModTree *tree, const char *root_rel_path,
                               const char *extern_name, CBMRustEdition edition) {
    if (!tree || !root_rel_path) {
        return -1;
    }
    RMT_GROW(tree, crates, crate_count, crate_cap);
    int id = tree->crate_count++;
    tree->crates[id] = (RmtCrate){
        .root_rel = cbm_arena_strdup(&tree->arena, root_rel_path),
        .extern_name = extern_name ? cbm_arena_strdup(&tree->arena, extern_name) : NULL,
        .edition = edition,
        .root_module = -1,
    };
    return id;
}

void cbm_rust_modtree_set_crate_features(CBMRustModTree *tree, int crate_id,
                                         const char *const *features, int count) {
    if (!tree || crate_id < 0 || crate_id >= tree->crate_count || count < 0) {
        return;
    }
    const char **copy = cbm_arena_alloc(&tree->arena, (size_t)(count + 1) * sizeof(*copy));
    if (!copy) {
        return;
    }
    for (int i = 0; i < count; i++) {
        copy[i] = cbm_arena_strdup(&tree->arena, features[i]);
    }
    tree->crates[crate_id].features = copy;
    tree->crates[crate_id].feature_count = count;
}

void cbm_rust_modtree_add_extern(CBMRustModTree *tree, int crate_id, const char *name,
                                 int target_crate) {
    if (!tree || crate_id < 0 || crate_id >= tree->crate_count || !name || !name[0]) {
        return;
    }
    const char *key = cbm_arena_sprintf(&tree->arena, "%d\x1f%s", crate_id, name);
    cbm_idxmemo_put_if_absent(&tree->externs, &tree->arena, key, target_crate + 1);
}

static int rmt_new_module(CBMRustModTree *tree, int crate, int parent, int file, uint32_t start,
                          uint32_t end) {
    RMT_GROW(tree, modules, module_count, module_cap);
    int id = tree->module_count++;
    tree->modules[id] = (RmtModule){.crate = crate,
                                    .parent = parent,
                                    .file = file,
                                    .next_in_file = tree->files[file].first_module,
                                    .first_glob = -1,
                                    .qn = tree->files[file].qn,
                                    .start = start,
                                    .end = end};
    tree->files[file].first_module = id;
    return id;
}

/* Chain a new entry behind the first one for `name`, keeping the memo's
 * first-put slot as the chain head. */
static int rmt_add_entry(CBMRustModTree *tree, int module, const char *name, RmtDef def, int import,
                         int vis) {
    RMT_GROW(tree, entries, entry_count, entry_cap);
    int id = tree->entry_count++;
    tree->entries[id] = (RmtEntry){.next = -1, .module = module,
                                   .name = cbm_arena_strdup(&tree->arena, name),
                                   .import = import, .vis = vis, .def = def};
    const char *key = cbm_arena_sprintf(&tree->arena, "%d\x1f%s", module, name);
    int32_t head = cbm_idxmemo_get(&tree->names, key);
    if (head < 0) {
        cbm_idxmemo_put_if_absent(&tree->names, &tree->arena, key, id);
    } else {
        tree->entries[id].next = tree->entries[head].next;
        tree->entries[head].next = id;
    }
    return id;
}

/* The module whose subtree may name an item declared in `module` with
 * the fact visibility `levels` (see RMT_VIS_PUB). */
static int rmt_vis_scope(const CBMRustModTree *tree, int module, int levels) {
    if (levels == RMT_VIS_PUB) {
        return RMT_VIS_PUB;
    }
    if (levels == RMT_VIS_CRATE) {
        return tree->crates[tree->modules[module].crate].root_module;
    }
    for (int i = 0; i < levels && tree->modules[module].parent >= 0; i++) {
        module = tree->modules[module].parent;
    }
    return module;
}

static void rmt_add_item(CBMRustModTree *tree, int module, const char *name, RmtKind kind,
                         uint8_t ns, int target_module, int vis, uint32_t line, uint32_t start) {
    const char *qn = NULL;
    if (kind != RMT_MODULE && kind != RMT_OPAQUE) {
        qn = cbm_arena_sprintf(&tree->arena, "%s.%s", tree->modules[module].qn, name);
    }
    RmtDef def = {.kind = (uint8_t)kind,
                  .ns = ns,
                  .module = target_module,
                  .line = line,
                  .start = start,
                  .qn = qn};
    (void)rmt_add_entry(tree, module, name, def, -1, rmt_vis_scope(tree, module, vis));
}

/* Split `a::b::c` into segments; a leading `::` yields an empty first one. */
static int rmt_split_path(CBMArena *arena, const char *path, const char **segs, int max) {
    int count = 0;
    const char *p = path;
    while (*p == ' ') {
        p++;
    }
    if (strncmp(p, "::", 2) == 0) {
        segs[count++] = "";
        p += 2;
    }
    while (*p && count < max) {
        const char *sep = strstr(p, "::");
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        while (len > 0 && p[len - 1] == ' ') {
            len--;
        }
        if (len == 0) {
            return -1;
        }
        segs[count++] = cbm_arena_strndup(arena, p, len);
        if (!sep) {
            return count;
        }
        p = sep + 2;
        while (*p == ' ') {
            p++;
        }
    }
    return *p ? -1 : count;
}

static int rmt_add_import(CBMRustModTree *tree, int module, const char *local, const char *path,
                          bool glob, int vis, bool conditional) {
    const char *segs[RMT_MAX_SEGS];
    int count = rmt_split_path(&tree->arena, path, segs, RMT_MAX_SEGS);
    if (count <= 0) {
        return -1;
    }
    RMT_GROW(tree, imports, import_count, import_cap);
    int id = tree->import_count++;
    const char **owned = cbm_arena_alloc(&tree->arena, (size_t)count * sizeof(*owned));
    if (!owned) {
        tree->import_count--;
        return -1;
    }
    memcpy(owned, segs, (size_t)count * sizeof(*owned));
    int scope = rmt_vis_scope(tree, module, vis);
    if (owned[0][0] != '\0' && !conditional) {
        const char *key = cbm_arena_sprintf(&tree->arena, "%d\x1f%s", module, owned[0]);
        cbm_idxmemo_put_if_absent(&tree->use_heads, &tree->arena, key, 1);
    }
    tree->imports[id] = (RmtImport){.module = module,
                                    .next_glob = -1,
                                    .vis = scope,
                                    .segs = owned,
                                    .seg_count = count,
                                    .glob = glob,
                                    .conditional = conditional};
    if (glob) {
        tree->imports[id].next_glob = tree->modules[module].first_glob;
        tree->modules[module].first_glob = id;
    } else {
        RmtDef none = {0};
        (void)rmt_add_entry(tree, module, local, none, id, scope);
    }
    return id;
}

/* ── Source walk ──────────────────────────────────────────────────── */

char *cbm_rust_path_attribute(CBMArena *arena, const char *source, TSNode mod_item) {
    for (TSNode item = ts_node_prev_named_sibling(mod_item); !ts_node_is_null(item);
         item = ts_node_prev_named_sibling(item)) {
        if (strcmp(ts_node_type(item), "attribute_item") != 0) {
            break;
        }
        TSNode attribute = cbm_find_child_by_kind(item, "attribute");
        if (ts_node_is_null(attribute) || ts_node_named_child_count(attribute) < 2) {
            continue;
        }
        TSNode name = ts_node_named_child(attribute, 0);
        size_t name_length = ts_node_end_byte(name) - ts_node_start_byte(name);
        if (name_length != 4 || memcmp(source + ts_node_start_byte(name), "path", 4) != 0) {
            continue;
        }
        TSNode literal = ts_node_named_child(attribute, 1);
        const char *literal_kind = ts_node_type(literal);
        if (strcmp(literal_kind, "string_literal") != 0 &&
            strcmp(literal_kind, "raw_string_literal") != 0) {
            return NULL;
        }
        TSNode content = cbm_find_child_by_kind(literal, "string_content");
        if (ts_node_is_null(content)) {
            return cbm_arena_strdup(arena, "");
        }
        uint32_t start = ts_node_start_byte(content);
        size_t length = ts_node_end_byte(content) - start;
        if (strcmp(literal_kind, "string_literal") == 0 && memchr(source + start, '\\', length)) {
            return NULL;
        }
        return cbm_arena_strndup(arena, source + start, length);
    }
    return NULL;
}

/* Join `dir` and a relative path, folding `.` and `..`. NULL when the path
 * leaves the project root or is absolute. */
static const char *rmt_join_path(CBMArena *arena, const char *dir, const char *rel) {
    if (!rel || !rel[0] || rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':')) {
        return NULL;
    }
    const char *joined = dir[0] ? cbm_arena_sprintf(arena, "%s/%s", dir, rel) : rel;
    size_t len = strlen(joined);
    char *out = cbm_arena_alloc(arena, len + 1);
    if (!out) {
        return NULL;
    }
    size_t used = 0;
    for (const char *p = joined; *p;) {
        const char *end = p;
        while (*end && *end != '/' && *end != '\\') {
            end++;
        }
        size_t seg = (size_t)(end - p);
        if (seg == 2 && p[0] == '.' && p[1] == '.') {
            if (used == 0) {
                return NULL;
            }
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
            memcpy(out + used, p, seg);
            used += seg;
        }
        p = *end ? end + 1 : end;
    }
    out[used] = '\0';
    return out;
}

static int rmt_file_id(const CBMRustModTree *tree, const char *rel) {
    return rel ? cbm_idxmemo_get(&tree->file_by_rel, rel) : -1;
}

typedef struct {
    const char *node_kind;
    RmtKind kind;
    uint8_t ns;
} RmtItemKind;

static const RmtItemKind RMT_ITEM_KINDS[] = {
    {"function_item", RMT_FN, RMT_NS_VALUE}, {"struct_item", RMT_TYPE, RMT_NS_TYPE | RMT_NS_VALUE},
    {"union_item", RMT_TYPE, RMT_NS_TYPE},   {"enum_item", RMT_ENUM, RMT_NS_TYPE},
    {"trait_item", RMT_TRAIT, RMT_NS_TYPE},  {"type_item", RMT_ALIAS, RMT_NS_TYPE},
    {"const_item", RMT_VALUE, RMT_NS_VALUE}, {"static_item", RMT_VALUE, RMT_NS_VALUE},
    {"proc_macro", RMT_MACRO, RMT_NS_MACRO},
};

static uint8_t rmt_prelude_attribute(CBMArena *arena, const char *source, TSNode item) {
    TSNode attr = cbm_find_child_by_kind(item, "attribute");
    if (ts_node_is_null(attr) || ts_node_named_child_count(attr) == 0)
        return 0;
    const char *name = cbm_node_text(arena, ts_node_named_child(attr, 0), source);
    if (!name)
        return 0;
    bool conditional = strcmp(name, "cfg_attr") == 0;
    const char *text = conditional ? cbm_node_text(arena, attr, source) : name;
    if (!text || strstr(text, "no_implicit_prelude"))
        return CBM_RUST_PRELUDE_DISABLED;
    if (strstr(text, "no_core"))
        return CBM_RUST_PRELUDE_DISABLED | CBM_RUST_PRELUDE_CORE_ONLY;
    return strstr(text, "no_std") ? CBM_RUST_PRELUDE_CORE_ONLY : 0;
}

uint8_t cbm_rust_prelude_restrictions(CBMArena *arena, const char *source, TSNode scope) {
    uint8_t restrictions = 0;
    TSNode module = strcmp(ts_node_type(scope), "mod_item") == 0 ? scope : ts_node_parent(scope);
    if (!ts_node_is_null(module) && strcmp(ts_node_type(module), "mod_item") == 0) {
        for (TSNode attr = ts_node_prev_named_sibling(module);
             !ts_node_is_null(attr) && strcmp(ts_node_type(attr), "attribute_item") == 0;
             attr = ts_node_prev_named_sibling(attr))
            restrictions |= rmt_prelude_attribute(arena, source, attr);
        if (ts_node_eq(scope, module))
            scope = ts_node_child_by_field_name(module, "body", 4);
    }
    for (uint32_t i = 0; !ts_node_is_null(scope) && i < ts_node_named_child_count(scope); i++) {
        TSNode item = ts_node_named_child(scope, i);
        if (strcmp(ts_node_type(item), "inner_attribute_item") == 0)
            restrictions |= rmt_prelude_attribute(arena, source, item);
    }
    return restrictions;
}

/* One fact of a file, in source order. Inline module bodies are bracketed
 * by RMF_MOD_OPEN and RMF_MOD_CLOSE. */
typedef enum {
    RMF_ITEM = 1,  /* `name` is a named item of kind RMT_ITEM_KINDS[item] */
    RMF_MOD_DECL,  /* `mod name;`, `text` its `#[path]` value or NULL */
    RMF_MOD_OPEN,  /* `mod name { .. }`, body bytes [start, end) */
    RMF_MOD_CLOSE, /* end of the innermost open inline module */
    RMF_IMPORT,    /* `use` / `extern crate` binding `name` (NULL when glob) to `text` */
    RMF_MACRO,     /* an item-position macro call */
    RMF_MACRO_MOD, /* `mod name;` written in an unexpanded item macro's braces */
    RMF_PRELUDE,   /* item: standard-prelude restriction bits */
    RMF_MACRO_EXPORT, /* `#[macro_export] macro_rules! name`: an item of the crate root */
    RMF_IMPL_ASSOC, /* `type name = text;` in a trait impl for the type path `self_path` */
} RmfOp;

typedef struct {
    uint8_t op;
    uint8_t item;
    bool glob;
    bool conditional;
    int8_t vis; /* levels above the declaring module, RMT_VIS_PUB or RMT_VIS_CRATE */
    uint32_t start;
    uint32_t end;
    uint32_t line; /* RMF_ITEM: 1-based line of the item */
    const char *name;
    const char *text;
    /* conditional records: the item's `#[cfg(..)]` predicates joined as
     * `all(..)`, NULL when another attribute (cfg_attr) also gates it */
    const char *cfg;
    const char *self_path; /* RMF_IMPL_ASSOC: the impl's self type path, without arguments */
    const char *self_text; /* RMF_IMPL_ASSOC: the impl's self type as written */
    bool unreadable;       /* RMF_IMPL_ASSOC: the value depends on unrecorded parameters */
} RmfRecord;

struct CBMRustModFacts {
    CBMArena arena;
    RmfRecord *records;
    int count, cap;
};

typedef struct {
    CBMRustModFacts *facts;
    const char *source;
    bool failed;
    bool conditional;
    const char *cfg; /* predicate of the plain `#[cfg]` attributes when they alone gate */
} RmfScan;

static int rmf_grow(CBMRustModFacts *facts) {
    RMT_GROW(facts, records, count, cap);
    return 0;
}

static void rmf_push(RmfScan *scan, RmfRecord record) {
    if (scan->failed || rmf_grow(scan->facts) != 0) {
        scan->failed = true;
        return;
    }
    record.conditional = scan->conditional;
    record.cfg = scan->conditional ? scan->cfg : NULL;
    scan->facts->records[scan->facts->count++] = record;
}

static void rmf_scan_items(RmfScan *scan, TSNode container);

static TSNode rmt_visibility_node(TSNode item) {
    return cbm_find_child_by_kind(item, "visibility_modifier");
}

/* An item's `pub(..)` as a fact visibility. `pub(in path)` other than
 * `self`/`super`/`crate` counts as crate-wide: wider than declared, so a
 * name it hides can only add a rival candidate, never be chosen alone. */
static int8_t rmf_visibility(const char *source, TSNode item) {
    TSNode node = rmt_visibility_node(item);
    if (ts_node_is_null(node)) {
        return 0;
    }
    char text[RMT_KEY_MAX];
    size_t used = 0;
    for (uint32_t i = ts_node_start_byte(node); i < ts_node_end_byte(node); i++) {
        if (source[i] != ' ' && source[i] != '\t' && source[i] != '\n' && source[i] != '\r') {
            if (used + 1 >= sizeof(text)) {
                return RMT_VIS_CRATE;
            }
            text[used++] = source[i];
        }
    }
    text[used] = '\0';
    if (strcmp(text, "pub") == 0) {
        return RMT_VIS_PUB;
    }
    const char *inner = strncmp(text, "pub(", 4) == 0 ? text + 4 : NULL;
    if (!inner) {
        return RMT_VIS_CRATE; /* the 2015 `crate` modifier */
    }
    if (strncmp(inner, "in", 2) == 0 && strcmp(inner, "in)") != 0) {
        inner += 2;
    }
    if (strcmp(inner, "self)") == 0) {
        return 0;
    }
    int8_t levels = 0;
    while (strncmp(inner, "super", 5) == 0 && levels < INT8_MAX) {
        levels++;
        inner += 5;
        if (strncmp(inner, "::", 2) == 0) {
            inner += 2;
        }
    }
    return levels > 0 && strcmp(inner, ")") == 0 ? levels : RMT_VIS_CRATE;
}

static void rmf_scan_mod(RmfScan *scan, TSNode item) {
    CBMArena *arena = &scan->facts->arena;
    TSNode name_node = ts_node_child_by_field_name(item, "name", 4);
    if (ts_node_is_null(name_node)) {
        return;
    }
    const char *name = cbm_node_text(arena, name_node, scan->source);
    TSNode body = ts_node_child_by_field_name(item, "body", 4);
    if (ts_node_is_null(body)) {
        rmf_push(scan, (RmfRecord){.op = RMF_MOD_DECL,
                                   .item = cbm_rust_prelude_restrictions(arena, scan->source, item),
                                   .vis = rmf_visibility(scan->source, item),
                                   .name = name,
                                   .text = cbm_rust_path_attribute(arena, scan->source, item)});
        return;
    }
    rmf_push(scan, (RmfRecord){.op = RMF_MOD_OPEN,
                               .vis = rmf_visibility(scan->source, item),
                               .name = name,
                               .text = cbm_rust_cfg_scope_qn(arena, item, scan->source, name),
                               .start = ts_node_start_byte(body),
                               .end = ts_node_end_byte(body)});
    rmf_scan_items(scan, body);
    rmf_push(scan, (RmfRecord){.op = RMF_MOD_CLOSE});
}

static void rmf_scan_use(RmfScan *scan, TSNode item) {
    CBMArena *arena = &scan->facts->arena;
    CBMImportArray imports = {0};
    cbm_extract_rust_use_tree(arena, ts_node_child_by_field_name(item, "argument", 8), scan->source,
                              &imports);
    int8_t vis = rmf_visibility(scan->source, item);
    for (int i = 0; i < imports.count; i++) {
        const CBMImport *imp = &imports.items[i];
        if (!imp->local_name || !imp->module_path) {
            continue;
        }
        if (strcmp(imp->local_name, "*") == 0) {
            size_t len = strlen(imp->module_path);
            if (len > 3) {
                rmf_push(scan,
                         (RmfRecord){.op = RMF_IMPORT,
                                     .glob = true,
                                     .vis = vis,
                                     .text = cbm_arena_strndup(arena, imp->module_path, len - 3)});
            }
            continue;
        }
        rmf_push(scan, (RmfRecord){.op = RMF_IMPORT,
                                   .vis = vis,
                                   .name = imp->local_name,
                                   .text = imp->module_path});
    }
}

/* `extern crate name [as alias];` binds the alias to that crate's root. */
static void rmf_scan_extern_crate(RmfScan *scan, TSNode item) {
    CBMArena *arena = &scan->facts->arena;
    const char *name = NULL;
    const char *alias = NULL;
    uint32_t count = ts_node_named_child_count(item);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(item, i);
        const char *kind = ts_node_type(child);
        if (strcmp(kind, "identifier") != 0 && strcmp(kind, "self") != 0) {
            continue;
        }
        const char *text = cbm_node_text(arena, child, scan->source);
        if (!name) {
            name = text;
        } else {
            alias = text;
        }
    }
    if (!name) {
        return;
    }
    const char *path = strcmp(name, "self") == 0 ? "crate" : cbm_arena_sprintf(arena, "::%s", name);
    rmf_push(scan, (RmfRecord){.op = RMF_IMPORT,
                               .vis = rmf_visibility(scan->source, item),
                               .name = alias ? alias : name,
                               .text = path,
                               .item = 1});
}

/* `name` without a leading raw-identifier `r#`; NULL stays NULL. */
static const char *rmt_unraw(const char *name) {
    return name && strncmp(name, "r#", 2) == 0 ? name + 2 : name;
}

/* True when `attribute_item` is `#[cfg ..]` or `#[cfg_attr ..]`, read by
 * its name token so spacing and comments before the arguments do not hide
 * it. */
static bool rmf_attribute_is_cfg(CBMArena *arena, const char *source, TSNode attribute_item) {
    TSNode attr = cbm_find_child_by_kind(attribute_item, "attribute");
    if (ts_node_is_null(attr) || ts_node_named_child_count(attr) == 0)
        return false;
    const char *name = rmt_unraw(cbm_node_text(arena, ts_node_named_child(attr, 0), source));
    return name && (strcmp(name, "cfg") == 0 || strcmp(name, "cfg_attr") == 0);
}

/* The resolver already expands local macro_rules!. Reuse that matcher so
 * a trait re-export can reach an actual generated declaration, rather than
 * trusting an item-shaped argument which the macro might discard.
 * An out-of-line `mod name;` enters the tree the same way: only when the
 * selected arm of a textually visible local macro_rules! emits it at item
 * position with no cfg attribute on it or on the invocation. An unresolved
 * macro (a path, an alias, another file's definition), an arm that drops or
 * cfg-gates the tokens, or a nested invocation keeps the module's
 * may-declare-unseen-names fact. */
static bool rmf_scan_macro_items(RmfScan *scan, TSNode invocation) {
    TSNode root = invocation;
    for (TSNode parent = ts_node_parent(root); !ts_node_is_null(parent);
         parent = ts_node_parent(root))
        root = parent;
    char *expanded = cbm_rust_expand_local_item_macro(&scan->facts->arena, scan->source,
                                                      root, invocation);
    if (!expanded)
        return false;
    TSParser *parser = ts_parser_new();
    if (!parser)
        return true;
    ts_parser_set_language(parser, tree_sitter_rust());
    TSTree *tree = ts_parser_parse_string(parser, NULL, expanded, (uint32_t)strlen(expanded));
    TSNode items = tree ? ts_tree_root_node(tree) : (TSNode){0};
    if (!ts_node_is_null(items) && !ts_node_has_error(items)) {
        bool conditional = scan->conditional;
        const char *cfg = scan->cfg;
        for (uint32_t i = 0; i < ts_node_named_child_count(items); i++) {
            TSNode item = ts_node_named_child(items, i);
            if (strcmp(ts_node_type(item), "attribute_item") == 0) {
                if (rmf_attribute_is_cfg(&scan->facts->arena, expanded, item)) {
                    scan->conditional = true;
                    scan->cfg = NULL;
                }
            } else if (strcmp(ts_node_type(item), "trait_item") == 0) {
                TSNode name = ts_node_child_by_field_name(item, "name", 4);
                for (size_t k = 0; k < sizeof(RMT_ITEM_KINDS) / sizeof(RMT_ITEM_KINDS[0]); k++) {
                    if (strcmp(RMT_ITEM_KINDS[k].node_kind, "trait_item") != 0)
                        continue;
                    rmf_push(scan, (RmfRecord){.op = RMF_ITEM, .item = (uint8_t)k,
                        .vis = rmf_visibility(expanded, item),
                        .line = ts_node_start_point(invocation).row + 1,
                        .start = ts_node_start_byte(invocation),
                        .name = cbm_node_text(&scan->facts->arena, name, expanded)});
                }
                scan->conditional = conditional;
                scan->cfg = cfg;
            } else if (strcmp(ts_node_type(item), "mod_item") == 0) {
                TSNode name = ts_node_child_by_field_name(item, "name", 4);
                if (!scan->conditional && !ts_node_is_null(name) &&
                    ts_node_is_null(ts_node_child_by_field_name(item, "body", 4))) {
                    CBMArena *arena = &scan->facts->arena;
                    rmf_push(scan, (RmfRecord){
                        .op = RMF_MOD_DECL,
                        .item = cbm_rust_prelude_restrictions(arena, expanded, item),
                        .vis = rmf_visibility(expanded, item),
                        .name = cbm_node_text(arena, name, expanded),
                        .text = cbm_rust_path_attribute(arena, expanded, item)});
                }
                scan->conditional = conditional;
                scan->cfg = cfg;
            } else if (strcmp(ts_node_type(item), "line_comment") != 0 &&
                       strcmp(ts_node_type(item), "block_comment") != 0) {
                scan->conditional = conditional;
                scan->cfg = cfg;
            }
        }
        scan->conditional = conditional;
        scan->cfg = cfg;
    }
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return true;
}

/* Push the out-of-line `mod name;` items of `body` (item text) as candidates.
 * False when `body` does not parse as items. */
static bool rmf_scan_mod_candidate_items(RmfScan *scan, char *body) {
    /* A cheap byte filter only; the parse below finds `mod` as a token,
     * whatever spacing or comments follow it. */
    if (!body || !strstr(body, "mod"))
        return true;
    TSParser *parser = ts_parser_new();
    if (!parser)
        return true;
    TSTree *tree = NULL;
    if (ts_parser_set_language(parser, tree_sitter_rust()))
        tree = ts_parser_parse_string(parser, NULL, body, (uint32_t)strlen(body));
    ts_parser_delete(parser);
    if (!tree)
        return true;
    TSNode items = ts_tree_root_node(tree);
    bool parsed = !ts_node_has_error(items);
    CBMArena *arena = &scan->facts->arena;
    for (uint32_t i = 0; parsed && i < ts_node_named_child_count(items); i++) {
        TSNode item = ts_node_named_child(items, i);
        TSNode name = ts_node_child_by_field_name(item, "name", 4);
        if (strcmp(ts_node_type(item), "mod_item") != 0 || ts_node_is_null(name) ||
            !ts_node_is_null(ts_node_child_by_field_name(item, "body", 4)))
            continue;
        rmf_push(scan, (RmfRecord){.op = RMF_MACRO_MOD,
                                   .item = cbm_rust_prelude_restrictions(arena, body, item),
                                   .vis = rmf_visibility(body, item),
                                   .name = cbm_node_text(arena, name, body),
                                   .text = cbm_rust_path_attribute(arena, body, item)});
    }
    ts_tree_delete(tree);
    return parsed;
}

/* The text inside the braces of a `{ .. }` token tree, or NULL. */
static char *rmf_brace_body(RmfScan *scan, TSNode tokens) {
    uint32_t start = ts_node_start_byte(tokens);
    uint32_t end = ts_node_end_byte(tokens);
    if (end < start + 2 || scan->source[start] != '{' || scan->source[end - 1] != '}')
        return NULL;
    return cbm_arena_strndup(&scan->facts->arena, scan->source + start + 1, end - start - 2);
}

/* The out-of-line `mod name;` items written in the braces of an item macro
 * the local expander cannot read (a path, an alias, another file's
 * definition), as candidates the tree mounts only when nothing else can bind
 * the name (rmt_mount_macro_mods). A body that is no item list is read one
 * level down, for the branches of a cfg-alias macro
 * (`multi_threaded! { if { mod a; } else { mod b; } }`): which branch the
 * build selects is not known here, and a candidate never displaces a module
 * that something else binds. */
static void rmf_scan_macro_mod_candidates(RmfScan *scan, TSNode invocation) {
    TSNode tokens = cbm_find_child_by_kind(invocation, "token_tree");
    if (ts_node_is_null(tokens))
        return;
    if (rmf_scan_mod_candidate_items(scan, rmf_brace_body(scan, tokens)))
        return;
    for (uint32_t i = 0; i < ts_node_named_child_count(tokens); i++) {
        TSNode branch = ts_node_named_child(tokens, i);
        if (strcmp(ts_node_type(branch), "token_tree") == 0)
            rmf_scan_mod_candidate_items(scan, rmf_brace_body(scan, branch));
    }
}

/* Proc macros export a macro name, not the implementation function as a
 * value. In particular the derive name comes from the attribute, not fn. */
static bool rmf_scan_proc_macro(RmfScan *scan, TSNode item) {
    if (strcmp(ts_node_type(item), "function_item") != 0)
        return false;
    for (TSNode attr = ts_node_prev_named_sibling(item); !ts_node_is_null(attr);
         attr = ts_node_prev_named_sibling(attr)) {
        const char *kind = ts_node_type(attr);
        if (strcmp(kind, "line_comment") == 0 || strcmp(kind, "block_comment") == 0)
            continue;
        if (strcmp(kind, "attribute_item") != 0)
            break;
        TSNode body = cbm_find_child_by_kind(attr, "attribute");
        if (ts_node_is_null(body) || ts_node_named_child_count(body) == 0)
            continue;
        const char *name = cbm_node_text(&scan->facts->arena, ts_node_named_child(body, 0),
                                        scan->source);
        bool derive = name && strcmp(name, "proc_macro_derive") == 0;
        if (!derive && (!name || (strcmp(name, "proc_macro") != 0 &&
                                 strcmp(name, "proc_macro_attribute") != 0)))
            continue;
        TSNode exported = ts_node_child_by_field_name(item, "name", 4);
        if (derive) {
            TSNode tokens = cbm_find_child_by_kind(body, "token_tree");
            exported = ts_node_is_null(tokens) ? (TSNode){0} : ts_node_named_child(tokens, 0);
            if (ts_node_is_null(exported) || strcmp(ts_node_type(exported), "identifier") != 0)
                return true;
        }
        rmf_push(scan, (RmfRecord){.op = RMF_ITEM,
            .item = (uint8_t)(sizeof(RMT_ITEM_KINDS) / sizeof(RMT_ITEM_KINDS[0]) - 1),
            .vis = RMT_VIS_PUB, .line = ts_node_start_point(item).row + 1,
            .start = ts_node_start_byte(item),
            .name = cbm_node_text(&scan->facts->arena, exported, scan->source)});
        return true;
    }
    return false;
}

/* Whether an outer attribute of `item` is named `name`. */
static bool rmf_has_attribute(RmfScan *scan, TSNode item, const char *name) {
    for (TSNode attr = ts_node_prev_named_sibling(item); !ts_node_is_null(attr);
         attr = ts_node_prev_named_sibling(attr)) {
        const char *kind = ts_node_type(attr);
        if (strcmp(kind, "line_comment") == 0 || strcmp(kind, "block_comment") == 0)
            continue;
        if (strcmp(kind, "attribute_item") != 0)
            return false;
        TSNode body = cbm_find_child_by_kind(attr, "attribute");
        const char *written = ts_node_is_null(body) || ts_node_named_child_count(body) == 0
                                  ? NULL
                                  : cbm_node_text(&scan->facts->arena,
                                                  ts_node_named_child(body, 0), scan->source);
        if (written && strcmp(written, name) == 0)
            return true;
    }
    return false;
}

/* True when the impl declares a type or const parameter: an associated type
 * it writes may then depend on what the parameter is bound to. */
static bool rmf_impl_has_type_params(TSNode impl) {
    TSNode params = ts_node_child_by_field_name(impl, "type_parameters", 15);
    uint32_t count = ts_node_is_null(params) ? 0 : ts_node_named_child_count(params);
    for (uint32_t i = 0; i < count; i++) {
        const char *kind = ts_node_type(ts_node_named_child(params, i));
        if (strcmp(kind, "lifetime") != 0 && strcmp(kind, "lifetime_parameter") != 0 &&
            strcmp(kind, "line_comment") != 0 && strcmp(kind, "block_comment") != 0)
            return true;
    }
    return false;
}

/* The path an impl's self type names, without generic arguments; NULL for a
 * reference, slice, array, tuple or any other self type that is no path. */
static TSNode rmf_impl_self_path(TSNode impl) {
    TSNode self = ts_node_child_by_field_name(impl, "type", 4);
    if (!ts_node_is_null(self) && strcmp(ts_node_type(self), "generic_type") == 0)
        self = ts_node_child_by_field_name(self, "type", 4);
    const char *kind = ts_node_is_null(self) ? "" : ts_node_type(self);
    TSNode none = {0};
    return strcmp(kind, "type_identifier") == 0 || strcmp(kind, "scoped_type_identifier") == 0
               ? self : none;
}

/* True when a `type` item of an impl declares a type or const parameter of
 * its own (a generic associated type). */
static bool rmf_assoc_has_type_params(TSNode member) {
    return rmf_impl_has_type_params(member);
}

/* Each `type name = value;` of a trait impl whose self type is a path. An
 * impl with type or const parameters, or a generic associated type, is
 * recorded unreadable: it still competes with every other impl that writes
 * the same name on that self type. */
static void rmf_scan_impl(RmfScan *scan, TSNode impl) {
    TSNode body = ts_node_child_by_field_name(impl, "body", 4);
    TSNode self = rmf_impl_self_path(impl);
    if (ts_node_is_null(body) || ts_node_is_null(self) ||
        ts_node_is_null(ts_node_child_by_field_name(impl, "trait", 5)))
        return;
    bool generic_impl = rmf_impl_has_type_params(impl);
    const char *self_path = cbm_node_text(&scan->facts->arena, self, scan->source);
    const char *self_text = cbm_node_text(&scan->facts->arena,
                                          ts_node_child_by_field_name(impl, "type", 4),
                                          scan->source);
    uint32_t count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < count && !scan->failed; i++) {
        TSNode member = ts_node_named_child(body, i);
        if (strcmp(ts_node_type(member), "type_item") != 0)
            continue;
        TSNode name = ts_node_child_by_field_name(member, "name", 4);
        TSNode value = ts_node_child_by_field_name(member, "type", 4);
        if (ts_node_is_null(name) || ts_node_is_null(value))
            continue;
        rmf_push(scan, (RmfRecord){.op = RMF_IMPL_ASSOC,
                                   .self_path = self_path,
                                   .self_text = self_text,
                                   .unreadable = generic_impl || rmf_assoc_has_type_params(member),
                                   .name = cbm_node_text(&scan->facts->arena, name, scan->source),
                                   .text = cbm_node_text(&scan->facts->arena, value,
                                                         scan->source)});
    }
}

static void rmf_scan_item(RmfScan *scan, TSNode item) {
    const char *kind = ts_node_type(item);
    if (rmf_scan_proc_macro(scan, item))
        return;
    if (strcmp(kind, "macro_definition") == 0) {
        /* A `#[macro_export]` macro_rules! is path-addressable at its crate
         * root (the reference, "Path-based scope"); one without it is not
         * path-addressable where it stands. */
        TSNode name = ts_node_child_by_field_name(item, "name", 4);
        if (!ts_node_is_null(name) && rmf_has_attribute(scan, item, "macro_export"))
            rmf_push(scan, (RmfRecord){.op = RMF_MACRO_EXPORT,
                                       .line = ts_node_start_point(item).row + 1,
                                       .start = ts_node_start_byte(item),
                                       .name = cbm_node_text(&scan->facts->arena, name,
                                                             scan->source)});
    } else if (strcmp(kind, "impl_item") == 0) {
        rmf_scan_impl(scan, item);
    } else if (strcmp(kind, "mod_item") == 0) {
        rmf_scan_mod(scan, item);
    } else if (strcmp(kind, "use_declaration") == 0) {
        rmf_scan_use(scan, item);
    } else if (strcmp(kind, "extern_crate_declaration") == 0) {
        rmf_scan_extern_crate(scan, item);
    } else if (strcmp(kind, "macro_invocation") == 0) {
        rmf_push(scan, (RmfRecord){.op = RMF_MACRO});
        if (!rmf_scan_macro_items(scan, item))
            rmf_scan_macro_mod_candidates(scan, item);
    } else {
        for (size_t i = 0; i < sizeof(RMT_ITEM_KINDS) / sizeof(RMT_ITEM_KINDS[0]); i++) {
            if (strcmp(kind, RMT_ITEM_KINDS[i].node_kind) != 0) {
                continue;
            }
            TSNode name = ts_node_child_by_field_name(item, "name", 4);
            if (!ts_node_is_null(name)) {
                /* A `const`/`static` keeps its declared type for value typing. */
                TSNode type = RMT_ITEM_KINDS[i].kind == RMT_VALUE
                                  ? ts_node_child_by_field_name(item, "type", 4)
                                  : (TSNode){0};
                rmf_push(scan, (RmfRecord){
                                   .op = RMF_ITEM,
                                   .item = (uint8_t)i,
                                   .vis = rmf_visibility(scan->source, item),
                                   .line = ts_node_start_point(item).row + 1,
                                   .start = ts_node_start_byte(item),
                                   .name = cbm_node_text(&scan->facts->arena, name, scan->source),
                                   .text = ts_node_is_null(type)
                                               ? NULL
                                               : cbm_node_text(&scan->facts->arena, type,
                                                               scan->source)});
            }
            return;
        }
    }
}

/* The predicate of `#[cfg(PRED)]`: PRED's text, NULL when unreadable. */
static const char *rmf_cfg_predicate(RmfScan *scan, TSNode attr) {
    TSNode args = ts_node_child_by_field_name(attr, "arguments", 9);
    const char *text = ts_node_is_null(args) ? NULL
                                             : cbm_node_text(&scan->facts->arena, args, scan->source);
    size_t len = text ? strlen(text) : 0;
    if (len < 2 || text[0] != '(' || text[len - 1] != ')')
        return NULL;
    return cbm_arena_strndup(&scan->facts->arena, text + 1, len - 2);
}

static void rmf_scan_items(RmfScan *scan, TSNode container) {
    uint8_t restrictions =
        cbm_rust_prelude_restrictions(&scan->facts->arena, scan->source, container);
    if (restrictions)
        rmf_push(scan, (RmfRecord){.op = RMF_PRELUDE, .item = restrictions});
    bool saved = scan->conditional;
    const char *saved_cfg = scan->cfg;
    scan->conditional = false;
    scan->cfg = NULL;
    bool cfg_decidable = true;
    uint32_t count = ts_node_named_child_count(container);
    for (uint32_t i = 0; i < count && !scan->failed; i++) {
        TSNode item = ts_node_named_child(container, i);
        if (strcmp(ts_node_type(item), "attribute_item") == 0) {
            TSNode attr = cbm_find_child_by_kind(item, "attribute");
            if (!ts_node_is_null(attr) && ts_node_named_child_count(attr) > 0) {
                const char *name =
                    cbm_node_text(&scan->facts->arena, ts_node_named_child(attr, 0), scan->source);
                bool cfg = name && strcmp(name, "cfg") == 0;
                if (cfg || (name && strcmp(name, "cfg_attr") == 0)) {
                    scan->conditional = true;
                    const char *predicate = cfg ? rmf_cfg_predicate(scan, attr) : NULL;
                    cfg_decidable &= predicate != NULL;
                    scan->cfg = !cfg_decidable ? NULL
                                : scan->cfg ? cbm_arena_sprintf(&scan->facts->arena, "all(%s, %s)",
                                                                scan->cfg, predicate)
                                            : predicate;
                }
            }
            continue;
        }
        rmf_scan_item(scan, item);
        scan->conditional = false;
        scan->cfg = NULL;
        cfg_decidable = true;
    }
    scan->conditional = saved;
    scan->cfg = saved_cfg;
}

void cbm_rust_modfacts_free(CBMRustModFacts *facts) {
    if (!facts) {
        return;
    }
    cbm_free(CBM_MEM_CLASS_OTHER, facts->records);
    cbm_arena_destroy(&facts->arena);
    cbm_free(CBM_MEM_CLASS_OTHER, facts);
}

CBMRustModFacts *cbm_rust_modfacts_scan(const char *source, int len) {
    if (!source || len < 0) {
        return NULL;
    }
    TSParser *parser = ts_parser_new();
    TSTree *ts_tree = NULL;
    if (parser && ts_parser_set_language(parser, tree_sitter_rust())) {
        ts_tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)len);
    }
    if (parser) {
        ts_parser_delete(parser);
    }
    if (!ts_tree) {
        return NULL;
    }
    CBMRustModFacts *facts = cbm_calloc(CBM_MEM_CLASS_OTHER, sizeof(*facts));
    if (facts) {
        cbm_arena_init(&facts->arena);
        RmfScan scan = {.facts = facts, .source = source};
        rmf_scan_items(&scan, ts_tree_root_node(ts_tree));
        if (scan.failed) {
            cbm_rust_modfacts_free(facts);
            facts = NULL;
        }
    }
    ts_tree_delete(ts_tree);
    return facts;
}

/* ── Tree construction from facts ─────────────────────────────────── */

typedef struct {
    CBMRustModTree *tree;
    const CBMRustModFacts *const *facts;
    int crate;
    int file;
    struct RmtMacroMods *pending; /* RMF_MACRO_MOD candidates of the crate */
} RmtBuild;

typedef struct {
    RmtBuild build; /* the declaring file's build */
    const RmfRecord *decl;
    int module;
    const char *child_dir;
    bool inline_scope;
    int depth;
} RmtMacroMod;

typedef struct RmtMacroMods {
    RmtMacroMod *items;
    int count, cap;
} RmtMacroMods;

static int rmt_macro_mods_grow(RmtMacroMods *list) {
    RMT_GROW(list, items, count, cap);
    return 0;
}

static void rmt_build_file(const RmtBuild *outer, int module, int file, const char *child_dir,
                           int depth);

/* `mod name;` → the declared file: `#[path]` relative to the declaring
 * file's directory (or the inline module's directory), else `name.rs` or
 * `name/mod.rs` in the declaring module's child directory. */
static int rmt_out_of_line_file(const RmtBuild *build, const RmfRecord *decl, const char *child_dir,
                                bool inline_scope, const char **out_children_dir) {
    CBMRustModTree *tree = build->tree;
    if (decl->text) {
        const char *base = inline_scope ? child_dir : tree->files[build->file].dir;
        const char *target = rmt_join_path(&tree->arena, base, decl->text);
        int file = rmt_file_id(tree, target);
        if (file >= 0) {
            *out_children_dir = tree->files[file].dir;
        }
        return file;
    }
    const char *flat_dir = child_dir[0] ? cbm_arena_sprintf(&tree->arena, "%s/", child_dir) : "";
    int flat = rmt_file_id(tree, cbm_arena_sprintf(&tree->arena, "%s%s.rs", flat_dir, decl->name));
    int nested =
        rmt_file_id(tree, cbm_arena_sprintf(&tree->arena, "%s%s/mod.rs", flat_dir, decl->name));
    if ((flat >= 0) == (nested >= 0)) {
        return -1;
    }
    *out_children_dir = cbm_arena_sprintf(&tree->arena, "%s%s", flat_dir, decl->name);
    return flat >= 0 ? flat : nested;
}

static void rmt_build_mod_decl(const RmtBuild *build, const RmfRecord *decl, int module,
                               const char *child_dir, bool inline_scope, int depth) {
    CBMRustModTree *tree = build->tree;
    const char *children_dir = NULL;
    int file = rmt_out_of_line_file(build, decl, child_dir, inline_scope, &children_dir);
    if (file < 0 || depth >= RMT_MAX_DEPTH) {
        rmt_add_item(tree, module, decl->name, RMT_OPAQUE, RMT_NS_TYPE, -1, decl->vis, 0, 0);
        return;
    }
    int child = rmt_new_module(tree, build->crate, module, file, 0, UINT32_MAX);
    if (child < 0) {
        return;
    }
    tree->modules[child].prelude_restrictions = decl->item;
    rmt_add_item(tree, module, decl->name, RMT_MODULE, RMT_NS_TYPE, child, decl->vis, 0, 0);
    rmt_build_file(build, child, file, children_dir, depth + 1);
}

/* Index just past the RMF_MOD_CLOSE matching the inline module opened
 * before `at`. */
static int rmf_skip_body(const CBMRustModFacts *facts, int at) {
    for (int open = 1; at < facts->count && open > 0; at++) {
        open += facts->records[at].op == RMF_MOD_OPEN    ? 1
                : facts->records[at].op == RMF_MOD_CLOSE ? -1
                                                         : 0;
    }
    return at;
}

/* Apply the facts from `at` to `module` up to the close of its body (or the
 * end of the file); returns the index after the last fact applied. */
typedef enum { RMT_CFG_FALSE, RMT_CFG_TRUE, RMT_CFG_UNKNOWN } RmtCfg;

enum { RMT_CFG_MAX_DEPTH = 16, RMT_CFG_MAX_WORD = 64 };

typedef struct {
    const char *at;
    const RmtCrate *crate;
    bool failed;
} RmtCfgParse;

static void rmt_cfg_space(RmtCfgParse *p) {
    while (*p->at == ' ' || *p->at == '\t' || *p->at == '\n' || *p->at == '\r')
        p->at++;
}

static bool rmt_cfg_feature_enabled(const RmtCrate *crate, const char *name, size_t len) {
    for (int i = 0; i < crate->feature_count; i++)
        if (strlen(crate->features[i]) == len && strncmp(crate->features[i], name, len) == 0)
            return true;
    return false;
}

/* One cfg predicate under the crate's enabled features. `feature = ".."`
 * is decided, `all`, `any` and `not` combine three-valued; every other
 * option (`test`, `unix`, `target_os = ..`) is unknown. */
static RmtCfg rmt_cfg_eval(RmtCfgParse *p, int depth) {
    rmt_cfg_space(p);
    const char *word = p->at;
    while ((*p->at >= 'a' && *p->at <= 'z') || (*p->at >= 'A' && *p->at <= 'Z') ||
           (*p->at >= '0' && *p->at <= '9') || *p->at == '_')
        p->at++;
    size_t word_len = (size_t)(p->at - word);
    rmt_cfg_space(p);
    if (word_len == 0 || word_len > RMT_CFG_MAX_WORD || depth > RMT_CFG_MAX_DEPTH) {
        p->failed = true;
        return RMT_CFG_UNKNOWN;
    }
    if (*p->at == '=') {
        p->at++;
        rmt_cfg_space(p);
        if (*p->at != '"') {
            p->failed = true;
            return RMT_CFG_UNKNOWN;
        }
        const char *value = ++p->at;
        while (*p->at && *p->at != '"' && *p->at != '\\')
            p->at++;
        if (*p->at != '"') {
            p->failed = true;
            return RMT_CFG_UNKNOWN;
        }
        size_t value_len = (size_t)(p->at - value);
        p->at++;
        if (word_len == 7 && strncmp(word, "feature", 7) == 0)
            return rmt_cfg_feature_enabled(p->crate, value, value_len) ? RMT_CFG_TRUE
                                                                      : RMT_CFG_FALSE;
        return RMT_CFG_UNKNOWN;
    }
    if (*p->at != '(')
        return RMT_CFG_UNKNOWN;
    bool all = word_len == 3 && strncmp(word, "all", 3) == 0;
    bool any = word_len == 3 && strncmp(word, "any", 3) == 0;
    bool not = word_len == 3 && strncmp(word, "not", 3) == 0;
    p->at++;
    int count = 0;
    bool saw_false = false, saw_true = false, saw_unknown = false;
    RmtCfg last = RMT_CFG_UNKNOWN;
    for (;;) {
        rmt_cfg_space(p);
        if (*p->at == ')')
            break;
        last = rmt_cfg_eval(p, depth + 1);
        if (p->failed)
            return RMT_CFG_UNKNOWN;
        count++;
        saw_false |= last == RMT_CFG_FALSE;
        saw_true |= last == RMT_CFG_TRUE;
        saw_unknown |= last == RMT_CFG_UNKNOWN;
        rmt_cfg_space(p);
        if (*p->at == ',')
            p->at++;
        else if (*p->at != ')') {
            p->failed = true;
            return RMT_CFG_UNKNOWN;
        }
    }
    p->at++;
    if (all)
        return saw_false ? RMT_CFG_FALSE : saw_unknown ? RMT_CFG_UNKNOWN : RMT_CFG_TRUE;
    if (any)
        return saw_true ? RMT_CFG_TRUE : saw_unknown ? RMT_CFG_UNKNOWN : RMT_CFG_FALSE;
    if (not && count == 1)
        return last == RMT_CFG_TRUE ? RMT_CFG_FALSE
               : last == RMT_CFG_FALSE ? RMT_CFG_TRUE
                                       : RMT_CFG_UNKNOWN;
    return RMT_CFG_UNKNOWN;
}

/* What a conditional record's `#[cfg]` predicate says in the crate being
 * built: UNKNOWN unless the crate's features are known and the predicate
 * reads completely. */
static RmtCfg rmt_record_cfg(const CBMRustModTree *tree, int crate, const RmfRecord *record) {
    if (!record->conditional || !record->cfg || crate < 0 || crate >= tree->crate_count ||
        !tree->crates[crate].features)
        return RMT_CFG_UNKNOWN;
    RmtCfgParse parse = {.at = record->cfg, .crate = &tree->crates[crate]};
    RmtCfg value = rmt_cfg_eval(&parse, 0);
    rmt_cfg_space(&parse);
    return parse.failed || *parse.at ? RMT_CFG_UNKNOWN : value;
}

static int rmt_add_impl_assoc(CBMRustModTree *tree, int module, const RmfRecord *record) {
    RMT_GROW(tree, impl_assocs, impl_assoc_count, impl_assoc_cap);
    tree->impl_assocs[tree->impl_assoc_count++] = (RmtImplAssoc){
        .module = module,
        .self_path = cbm_arena_strdup(&tree->arena, record->self_path),
        .self_text = cbm_arena_strdup(&tree->arena, record->self_text),
        .unreadable = record->unreadable,
        .next = -1,
        .name = cbm_arena_strdup(&tree->arena, record->name),
        .value = cbm_arena_strdup(&tree->arena, record->text)};
    return 0;
}

static int rmt_build_items(const RmtBuild *build, const CBMRustModFacts *facts, int at, int module,
                           const char *child_dir, bool inline_scope, int depth) {
    CBMRustModTree *tree = build->tree;
    while (at < facts->count) {
        const RmfRecord *record = &facts->records[at++];
        switch (record->op) {
        case RMF_MOD_CLOSE:
            return at;
        case RMF_MOD_OPEN: {
            if (rmt_record_cfg(tree, build->crate, record) == RMT_CFG_FALSE) {
                at = rmf_skip_body(facts, at);
                break;
            }
            const char *dir =
                child_dir[0] ? cbm_arena_sprintf(&tree->arena, "%s/%s", child_dir, record->name)
                             : cbm_arena_strdup(&tree->arena, record->name);
            int child =
                rmt_new_module(tree, build->crate, module, build->file, record->start, record->end);
            if (child < 0) {
                at = rmf_skip_body(facts, at);
                break;
            }
            tree->modules[child].qn =
                cbm_arena_sprintf(&tree->arena, "%s.%s", tree->modules[module].qn,
                                  record->text ? record->text : record->name);
            cbm_idxmemo_put_if_absent(&tree->file_by_qn, &tree->arena, tree->modules[child].qn,
                                      tree->modules[child].file);
            rmt_add_item(tree, module, record->name, RMT_MODULE, RMT_NS_TYPE, child, record->vis, 0,
                         0);
            at = rmt_build_items(build, facts, at, child, dir, true, depth + 1);
            break;
        }
        case RMF_MOD_DECL:
            if (rmt_record_cfg(tree, build->crate, record) != RMT_CFG_FALSE)
                rmt_build_mod_decl(build, record, module, child_dir, inline_scope, depth);
            break;
        case RMF_IMPORT: {
            RmtCfg cfg = rmt_record_cfg(tree, build->crate, record);
            if (cfg == RMT_CFG_FALSE)
                break;
            int id = rmt_add_import(tree, module, record->name, record->text, record->glob,
                                    record->vis, record->conditional && cfg != RMT_CFG_TRUE);
            if (id >= 0)
                tree->imports[id].extern_crate = record->item != 0;
            break;
        }
        case RMF_PRELUDE:
            tree->modules[module].prelude_restrictions |= record->item;
            break;
        case RMF_MACRO_EXPORT: {
            int root = tree->crates[build->crate].root_module;
            rmt_add_item(tree, root, record->name, RMT_MACRO, RMT_NS_MACRO, -1, RMT_VIS_PUB,
                         record->line, record->start);
            tree->entries[tree->entry_count - 1].def.conditional = record->conditional;
            break;
        }
        case RMF_IMPL_ASSOC:
            if (rmt_record_cfg(tree, build->crate, record) != RMT_CFG_FALSE)
                rmt_add_impl_assoc(tree, module, record);
            break;
        case RMF_MACRO:
            tree->modules[module].macro_items = true;
            break;
        case RMF_MACRO_MOD:
            if (build->pending && rmt_macro_mods_grow(build->pending) == 0)
                build->pending->items[build->pending->count++] = (RmtMacroMod){
                    .build = *build, .decl = record, .module = module,
                    .child_dir = child_dir, .inline_scope = inline_scope, .depth = depth};
            break;
        case RMF_ITEM: {
            RmtCfg cfg = rmt_record_cfg(tree, build->crate, record);
            if (cfg == RMT_CFG_FALSE)
                break;
            rmt_add_item(tree, module, record->name, RMT_ITEM_KINDS[record->item].kind,
                         RMT_ITEM_KINDS[record->item].ns, -1, record->vis, record->line,
                         record->start);
            tree->entries[tree->entry_count - 1].def.conditional =
                record->conditional && cfg != RMT_CFG_TRUE;
            if (record->text)
                tree->entries[tree->entry_count - 1].def.value_type =
                    cbm_arena_strdup(&tree->arena, record->text);
            break;
        }
        default:
            break;
        }
    }
    return at;
}

static void rmt_build_file(const RmtBuild *outer, int module, int file, const char *child_dir,
                           int depth) {
    const CBMRustModFacts *facts = outer->facts[file];
    if (!facts) {
        outer->tree->modules[module].macro_items = true;
        return;
    }
    RmtBuild build = *outer;
    build.file = file;
    (void)rmt_build_items(&build, facts, 0, module, child_dir, false, depth);
}

/* ── Per-file tree facts digest ───────────────────────────────────── */

typedef struct {
    uint64_t hash;
    const char *source;
} RmtDigest;

enum { RMT_TOKEN_END = 0xff };
static const uint64_t RMT_FNV_PRIME = 0x100000001b3ULL;
static const uint64_t RMT_FNV_OFFSET = 0xcbf29ce484222325ULL;

static void rmt_digest_bytes(RmtDigest *d, const char *bytes, size_t length) {
    for (size_t i = 0; i < length; i++) {
        d->hash ^= (uint8_t)bytes[i];
        d->hash *= RMT_FNV_PRIME;
    }
    d->hash ^= RMT_TOKEN_END; /* token boundary: "ab"+"c" differs from "a"+"bc" */
    d->hash *= RMT_FNV_PRIME;
}

static void rmt_digest_str(RmtDigest *d, const char *text) {
    rmt_digest_bytes(d, text, strlen(text));
}

static void rmt_digest_node(RmtDigest *d, TSNode node) {
    if (ts_node_is_null(node)) {
        rmt_digest_str(d, "");
        return;
    }
    uint32_t start = ts_node_start_byte(node);
    rmt_digest_bytes(d, d->source + start, ts_node_end_byte(node) - start);
}

/* Every fact rmt_walk_item reads from an item, without byte offsets: an edit
 * that leaves the digest unchanged leaves every other file's resolution
 * through the tree unchanged. */
static void rmt_digest_items(RmtDigest *d, CBMArena *arena, TSNode container, int depth) {
    uint32_t count = ts_node_named_child_count(container);
    for (uint32_t i = 0; i < count; i++) {
        TSNode item = ts_node_named_child(container, i);
        const char *kind = ts_node_type(item);
        if (strcmp(kind, "attribute_item") == 0 || strcmp(kind, "inner_attribute_item") == 0) {
            TSNode attr = cbm_find_child_by_kind(item, "attribute");
            if (!ts_node_is_null(attr) && ts_node_named_child_count(attr) > 0) {
                const char *name = cbm_node_text(arena, ts_node_named_child(attr, 0), d->source);
                if (name && (strcmp(name, "cfg") == 0 || strcmp(name, "cfg_attr") == 0 ||
                             strcmp(name, "no_implicit_prelude") == 0 ||
                             strcmp(name, "no_core") == 0 || strcmp(name, "no_std") == 0 ||
                             strcmp(name, "proc_macro") == 0 ||
                             strcmp(name, "proc_macro_attribute") == 0 ||
                             strcmp(name, "proc_macro_derive") == 0 ||
                             strcmp(name, "macro_export") == 0))
                    rmt_digest_node(d, item);
            }
        } else if (strcmp(kind, "mod_item") == 0) {
            rmt_digest_str(d, "mod");
            rmt_digest_node(d, rmt_visibility_node(item));
            rmt_digest_node(d, ts_node_child_by_field_name(item, "name", 4));
            const char *attr = cbm_rust_path_attribute(arena, d->source, item);
            rmt_digest_str(d, attr ? "path" : "nopath");
            rmt_digest_str(d, attr ? attr : "");
            TSNode body = ts_node_child_by_field_name(item, "body", 4);
            if (!ts_node_is_null(body) && depth < RMT_MAX_DEPTH) {
                rmt_digest_str(d, "{");
                rmt_digest_items(d, arena, body, depth + 1);
                rmt_digest_str(d, "}");
            }
        } else if (strcmp(kind, "use_declaration") == 0) {
            rmt_digest_str(d, "use");
            rmt_digest_node(d, rmt_visibility_node(item));
            rmt_digest_node(d, ts_node_child_by_field_name(item, "argument", 8));
        } else if (strcmp(kind, "impl_item") == 0) {
            /* rmf_scan_impl reads the header and the associated types. */
            rmt_digest_str(d, "impl");
            rmt_digest_node(d, ts_node_child_by_field_name(item, "type_parameters", 15));
            rmt_digest_node(d, ts_node_child_by_field_name(item, "trait", 5));
            rmt_digest_node(d, ts_node_child_by_field_name(item, "type", 4));
            TSNode body = ts_node_child_by_field_name(item, "body", 4);
            uint32_t members = ts_node_is_null(body) ? 0 : ts_node_named_child_count(body);
            for (uint32_t m = 0; m < members; m++) {
                TSNode member = ts_node_named_child(body, m);
                if (strcmp(ts_node_type(member), "type_item") == 0)
                    rmt_digest_node(d, member);
            }
        } else if (strcmp(kind, "extern_crate_declaration") == 0) {
            rmt_digest_str(d, "extern");
            rmt_digest_node(d, item);
        } else if (strcmp(kind, "macro_definition") == 0) {
            rmt_digest_str(d, "macro_rules");
            rmt_digest_node(d, ts_node_child_by_field_name(item, "name", 4));
        } else if (strcmp(kind, "macro_invocation") == 0) {
            /* The expansion read by rmf_scan_macro_items depends on the
             * whole invocation; the definitions enter through the macro
             * facts digest. */
            rmt_digest_str(d, "macro");
            rmt_digest_node(d, item);
        } else {
            for (size_t k = 0; k < sizeof(RMT_ITEM_KINDS) / sizeof(RMT_ITEM_KINDS[0]); k++) {
                if (strcmp(kind, RMT_ITEM_KINDS[k].node_kind) == 0) {
                    rmt_digest_str(d, kind);
                    rmt_digest_node(d, rmt_visibility_node(item));
                    rmt_digest_node(d, ts_node_child_by_field_name(item, "name", 4));
                    if (RMT_ITEM_KINDS[k].kind == RMT_VALUE)
                        rmt_digest_node(d, ts_node_child_by_field_name(item, "type", 4));
                    break;
                }
            }
        }
    }
}

uint64_t cbm_rust_modtree_file_digest(const char *source, TSNode root) {
    RmtDigest d = {.hash = RMT_FNV_OFFSET, .source = source};
    if (!source || ts_node_is_null(root)) {
        return d.hash;
    }
    CBMArena arena;
    cbm_arena_init(&arena);
    rmt_digest_items(&d, &arena, root, 0);
    cbm_arena_destroy(&arena);
    return d.hash;
}

/* ── Name resolution ──────────────────────────────────────────────── */

static bool rmt_same_def(const RmtDef *a, const RmtDef *b) {
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == RMT_MODULE) {
        return a->module == b->module;
    }
    if (a->kind == RMT_EXTERNAL) {
        return true; /* one name outside the project, whatever path spelled it */
    }
    if (!a->qn || !b->qn) {
        return a->qn == b->qn;
    }
    return strcmp(a->qn, b->qn) == 0;
}

static void rmt_defs_add(RmtDefs *defs, const RmtDef *def) {
    for (int i = 0; i < defs->count; i++) {
        if (rmt_same_def(&defs->defs[i], def)) {
            defs->defs[i].ns |= def->ns;
            defs->defs[i].conditional |= def->conditional;
            if (defs->defs[i].line != def->line || defs->defs[i].start != def->start) {
                defs->defs[i].line = 0; /* two declarations: neither line names the item */
            }
            const char *qn = defs->defs[i].qn;
            if (def->kind == RMT_EXTERNAL && (!qn || !def->qn || strcmp(qn, def->qn) != 0)) {
                defs->defs[i].qn = NULL; /* two spellings: the path is unknown */
            }
            return;
        }
    }
    if (defs->count >= RMT_MAX_DEFS) {
        defs->open = true;
        return;
    }
    defs->defs[defs->count++] = *def;
}

static void rmt_defs_merge(RmtDefs *into, const RmtDefs *from, uint8_t ns) {
    into->open |= from->open;
    for (int i = 0; i < from->count; i++) {
        if (from->defs[i].ns & ns) {
            rmt_defs_add(into, &from->defs[i]);
        }
    }
}

static void rmt_resolve_import(CBMRustModTree *tree, int import);
static void rmt_resolve_path(CBMRustModTree *tree, CBMArena *arena, int module,
                             const char *const *segs, int count, uint8_t final_ns, bool is_use,
                             RmtDefs *out, bool building);

/* Enter `module`: false when this walk has entered it already, or when the
 * walk is full (then `visit->full` is set). */
static bool rmt_visit(RmtVisit *visit, int module) {
    unsigned slot = ((unsigned)module * 2654435761u) & (RMT_VISIT_SLOTS - 1);
    while (visit->slots[slot] != 0) {
        if (visit->slots[slot] == module + 1) {
            return false;
        }
        slot = (slot + 1) & (RMT_VISIT_SLOTS - 1);
    }
    if (visit->count >= RMT_VISIT_SLOTS / 2) {
        visit->full = true;
        return false;
    }
    visit->slots[slot] = module + 1;
    visit->count++;
    return true;
}

static void rmt_import_defs(CBMRustModTree *tree, int import, bool building, uint8_t ns,
                            RmtDefs *out) {
    if (building) {
        rmt_resolve_import(tree, import);
    }
    const RmtImport *imp = &tree->imports[import];
    if (imp->state == RMT_IMPORT_ACTIVE) {
        return; /* an import never names itself: `use tracing;` heads at the extern prelude */
    }
    if (imp->state != RMT_IMPORT_DONE) {
        out->open = true;
        return;
    }
    RmtDefs result = imp->result;
    if (imp->conditional)
        for (int i = 0; i < result.count; i++)
            result.defs[i].conditional = true;
    rmt_defs_merge(out, &result, ns);
}

/* True when `module` may name what is declared with visibility scope `vis`. */
static bool rmt_sees(const CBMRustModTree *tree, int vis, int module) {
    if (vis == RMT_VIS_PUB) {
        return true;
    }
    for (int m = module; m >= 0; m = tree->modules[m].parent) {
        if (m == vis) {
            return true;
        }
    }
    return false;
}

/* The modules a glob-imported name must be visible to: the importer of
 * every glob on the way to it (a name re-exported by `pub use m::*` is no
 * wider than the item itself nor than the glob). */
typedef struct {
    int items[RMT_VISIT_MAX];
    int count;
} RmtSeers;

static bool rmt_seen(const CBMRustModTree *tree, int vis, const RmtSeers *seers) {
    for (int i = 0; seers && i < seers->count; i++) {
        if (!rmt_sees(tree, vis, seers->items[i])) {
            return false;
        }
    }
    return true;
}

/* Names that come through `use path::*`: every name of the source module
 * the importers may see, its own glob imports included, recursively.
 * Code that compiles never uses a name two globs supply as different items,
 * so a glob whose contents are unknown (an external module, an enum, an
 * undecided path) cannot change a name a modelled glob supplies: it only
 * leaves a name no modelled glob supplies undecided. */
static bool rmt_use_proves_extern(const CBMRustModTree *tree, int module, const char *head);
static bool rmt_extern_prelude(CBMRustModTree *tree, int crate, const char *name,
                               RmtDef *out, bool building);

static void rmt_lookup(CBMRustModTree *tree, int module, const char *name, uint8_t ns, RmtDefs *out,
                       RmtVisit *visit, bool building, const RmtSeers *seers);

static void rmt_lookup_globs(CBMRustModTree *tree, int module, const char *name, uint8_t ns,
                             RmtDefs *out, RmtVisit *visit, bool building, const RmtSeers *seers) {
    RmtSeers chain = {.count = 0};
    if (seers) {
        chain = *seers;
    }
    if (chain.count >= RMT_VISIT_MAX) {
        out->open = true;
        return;
    }
    chain.items[chain.count++] = module;
    bool unknown_source = false;
    RmtDefs supplied = {0};
    for (int g = tree->modules[module].first_glob; g >= 0; g = tree->imports[g].next_glob) {
        if (!rmt_seen(tree, tree->imports[g].vis, seers)) {
            continue;
        }
        if (building) {
            rmt_resolve_import(tree, g);
        }
        const RmtImport *imp = &tree->imports[g];
        if (imp->state == RMT_IMPORT_ACTIVE) {
            continue; /* a glob never supplies a name its own path depends on */
        }
        if (imp->state != RMT_IMPORT_DONE || imp->result.open || imp->result.count != 1 ||
            imp->result.defs[0].kind != RMT_MODULE) {
            unknown_source = true;
            continue;
        }
        RmtDefs found = {0};
        rmt_lookup(tree, imp->result.defs[0].module, name, ns, &found, visit, building, &chain);
        if (found.count == 0) {
            unknown_source |= found.open;
            continue;
        }
        if (imp->conditional)
            for (int i = 0; i < found.count; i++)
                found.defs[i].conditional = true;
        rmt_defs_merge(&supplied, &found, ns);
    }
    rmt_defs_merge(out, &supplied, ns);
    /* A walk that ran out skipped globs that may supply the name too, so a
     * candidate another branch found does not settle it. */
    if ((supplied.count == 0 && unknown_source) || visit->full) {
        out->open = true;
    }
}

/* `name` in `module`'s own scope; with `seers`, only what each of them may
 * see. A declaration or import they may not see still shadows the
 * module's globs, as it does for the module itself. */
static void rmt_lookup(CBMRustModTree *tree, int module, const char *name, uint8_t ns, RmtDefs *out,
                       RmtVisit *visit, bool building, const RmtSeers *seers) {
    if (tree->crates[tree->modules[module].crate].edition == CBM_RUST_EDITION_UNKNOWN) {
        out->open = true;
        return;
    }
    char key[RMT_KEY_MAX];
    int written = snprintf(key, sizeof(key), "%d\x1f%s", module, name);
    if (written < 0 || written >= (int)sizeof(key)) {
        out->open = true;
        return;
    }
    bool declared_type = false;
    for (int e = cbm_idxmemo_get(&tree->names, key); e >= 0; e = tree->entries[e].next) {
        const RmtEntry *entry = &tree->entries[e];
        declared_type |=
            entry->import < 0 && !entry->def.conditional && (entry->def.ns & RMT_NS_TYPE) != 0;
    }
    bool hidden = false;
    for (int e = cbm_idxmemo_get(&tree->names, key); e >= 0; e = tree->entries[e].next) {
        const RmtEntry *entry = &tree->entries[e];
        bool seen = rmt_seen(tree, entry->vis, seers);
        RmtDefs *into = out;
        RmtDefs unseen = {0};
        if (!seen) {
            into = &unseen;
        }
        if (entry->import >= 0) {
            /* In valid Rust an unconditional named import sharing a spelling
             * with an unconditional local type must occupy another namespace
             * (a common case is a derive macro). The import's unknown external
             * contents therefore cannot obscure that type. */
            bool separate_type = declared_type && !tree->imports[entry->import].conditional;
            uint8_t import_ns = separate_type ? ns & ~RMT_NS_TYPE : ns;
            if (!import_ns)
                continue;
            RmtDefs imported = {0};
            rmt_import_defs(tree, entry->import, building, import_ns, &imported);
            if (separate_type && imported.open) {
                /* An unknown import can still bind the value namespace, but
                 * cannot reopen the locally declared type namespace when this
                 * name is re-exported through another module. */
                RmtDef unknown = {.kind = RMT_EXTERNAL, .ns = RMT_NS_VALUE, .module = -1};
                rmt_defs_add(into, &unknown);
            } else {
                into->open |= imported.open;
            }
            for (int i = 0; i < imported.count; i++) {
                RmtDef def = imported.defs[i];
                def.ns &= import_ns;
                rmt_defs_add(into, &def);
            }
        } else if (entry->def.ns & ns) {
            rmt_defs_add(into, &entry->def);
        }
        hidden |= unseen.count > 0 || unseen.open;
    }
    if (out->count > 0 || out->open || hidden) {
        return;
    }
    if (!rmt_visit(visit, module)) {
        out->open |= visit->full;
        return;
    }
    rmt_lookup_globs(tree, module, name, ns, out, visit, building, seers);
    RmtDef external = {0};
    if (out->count == 0 && (ns & RMT_NS_TYPE) &&
        (tree->crates[tree->modules[module].crate].edition != CBM_RUST_EDITION_2015 &&
         tree->crates[tree->modules[module].crate].edition != CBM_RUST_EDITION_UNKNOWN) &&
        rmt_implicit_extern_allowed(tree, module, name) &&
        rmt_use_proves_extern(tree, module, name) &&
        rmt_extern_prelude(tree, tree->modules[module].crate, name, &external, building)) {
        /* The same-module use rules out this name in its unknown globs and
         * item macros. Absence remains absence when another module glob-imports
         * this scope; the extern prelude itself is never glob-exported. */
        out->open = false;
        return;
    }
    if (out->count == 0 && tree->modules[module].macro_items) {
        out->open = true;
    }
}

static void rmt_resolve_import(CBMRustModTree *tree, int import) {
    RmtImport *imp = &tree->imports[import];
    if (imp->state != RMT_IMPORT_PENDING) {
        return;
    }
    imp->state = RMT_IMPORT_ACTIVE;
    RmtDefs result = {0};
    if (imp->extern_crate && imp->seg_count == 2 && imp->segs[0][0] == '\0') {
        RmtDef external;
        if (rmt_extern_prelude(tree, tree->modules[imp->module].crate, imp->segs[1], &external, true))
            rmt_defs_add(&result, &external);
        else
            result.open = true;
    } else {
        rmt_resolve_path(tree, &tree->arena, imp->module, imp->segs, imp->seg_count,
                         imp->glob ? RMT_NS_TYPE : RMT_NS_ALL, true, &result, true);
    }
    imp = &tree->imports[import];
    if (result.assoc) {
        result = (RmtDefs){.open = true};
    }
    imp->result = result;
    imp->state = RMT_IMPORT_DONE;
}

static const char *const RMT_LANGUAGE_CRATES[] = {"std",        "core", "alloc",
                                                  "proc_macro", "test", NULL};

/* Names every module sees through the standard prelude or as primitive
 * types; a path headed by one names no project item unless shadowed. */
static const char *const RMT_PRELUDE[] = {
    "Some",     "None",         "Ok",        "Err",     "Option",     "Result", "Vec",
    "String",   "Box",          "ToString",  "ToOwned", "Clone",      "Copy",   "Default",
    "Drop",     "Eq",           "PartialEq", "Ord",     "PartialOrd", "Fn",     "FnMut",
    "FnOnce",   "From",         "Into",      "TryFrom", "TryInto",    "AsRef",  "AsMut",
    "Iterator", "IntoIterator", "Extend",    "Send",    "Sync",       "Sized",  "Unpin",
    "drop",     "i8",           "i16",       "i32",     "i64",        "i128",   "isize",
    "u8",       "u16",          "u32",       "u64",     "u128",       "usize",  "f32",
    "f64",      "bool",         "char",      "str",     NULL};

static bool rmt_in_list(const char *const *list, const char *name) {
    for (const char *const *p = list; *p; p++) {
        if (strcmp(*p, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool rmt_extern_prelude(CBMRustModTree *tree, int crate, const char *name,
                               RmtDef *out, bool building) {
    /* A root extern-crate declaration adds its alias to the extern prelude
     * of child modules too. Keep the declaration's resolved crate identity. */
    char key[RMT_KEY_MAX];
    int root = tree->crates[crate].root_module;
    int declared = snprintf(key, sizeof(key), "%d\x1f%s", root, name);
    for (int e = declared > 0 && declared < (int)sizeof(key)
                     ? cbm_idxmemo_get(&tree->names, key) : -1;
         e >= 0; e = tree->entries[e].next) {
        int import = tree->entries[e].import;
        if (import < 0 || !tree->imports[import].extern_crate ||
            tree->imports[import].state == RMT_IMPORT_ACTIVE)
            continue;
        RmtDefs alias = {0};
        rmt_import_defs(tree, import, building, RMT_NS_TYPE, &alias);
        if (!alias.open && alias.count == 1 &&
            (alias.defs[0].kind == RMT_MODULE || alias.defs[0].kind == RMT_EXTERNAL)) {
            *out = alias.defs[0];
            return true;
        }
    }
    int written = snprintf(key, sizeof(key), "%d\x1f%s", crate, name);
    int32_t target =
        written > 0 && written < (int)sizeof(key) ? cbm_idxmemo_get(&tree->externs, key) : -1;
    if (target > 0 && tree->crates[target - 1].root_module >= 0) {
        *out = (RmtDef){
            .kind = RMT_MODULE, .ns = RMT_NS_TYPE, .module = tree->crates[target - 1].root_module};
        return true;
    }
    if (target == 0 || rmt_in_list(RMT_LANGUAGE_CRATES, name)) {
        *out = (RmtDef){.kind = RMT_EXTERNAL, .ns = RMT_NS_ALL, .module = -1, .qn = name};
        return true;
    }
    return false;
}

/* Outside `use` paths an unmodelled glob or macro item may shadow an extern
 * crate silently: E0659 covers only import and macro resolution. A 2018+
 * `use` declared in the same module and headed by `head` is that check:
 * it compiles only if no glob or macro-expanded item of the module binds
 * `head`. With no declared item or import of that name either, a non-use
 * path headed by `head` in the module names the extern crate too. */
static bool rmt_use_proves_extern(const CBMRustModTree *tree, int module, const char *head) {
    char key[RMT_KEY_MAX];
    int written = snprintf(key, sizeof(key), "%d\x1f%s", module, head);
    return written > 0 && written < (int)sizeof(key) &&
           cbm_idxmemo_get(&tree->use_heads, key) >= 0 && cbm_idxmemo_get(&tree->names, key) < 0;
}

static void rmt_resolve_head(CBMRustModTree *tree, int module, const char *head, uint8_t ns,
                             bool is_use, bool macro_path, RmtDefs *out, bool building) {
    int crate = tree->modules[module].crate;
    RmtVisit visit = {.count = 0};
    int scope = is_use && (tree->crates[crate].edition == CBM_RUST_EDITION_2015)
                    ? tree->crates[crate].root_module
                    : module;
    rmt_lookup(tree, scope, head, ns, out, &visit, building, NULL);
    RmtDef def = {.kind = RMT_EXTERNAL, .ns = RMT_NS_ALL, .module = -1};
    /* A 2018+ `use` path whose head names both an extern crate and a glob
     * or macro-expanded name in scope is rejected as ambiguous (E0659), so
     * a head left open only by an unmodelled glob or macro still names the
     * extern crate; another path does when such a `use` proves it. A
     * project crate and a language crate (`std`, `core`, ...) are settled
     * this way, so a `use` of either names the same item in every module.
     * A dependency crate is not: its items publish nothing, and impl
     * receivers keep its type names file-local. The head of a macro path is
     * resolved during expansion like a `use` path, under the same E0659
     * rule (glob import vs outer scope during import/macro resolution). */
    if (out->open && out->count == 0 && !(tree->crates[crate].edition == CBM_RUST_EDITION_2015) &&
        (ns & RMT_NS_TYPE) && rmt_implicit_extern_allowed(tree, module, head) &&
        (is_use || macro_path || rmt_use_proves_extern(tree, module, head)) &&
        rmt_extern_prelude(tree, crate, head, &def, building) &&
        (def.kind == RMT_MODULE || rmt_in_list(RMT_LANGUAGE_CRATES, head))) {
        *out = (RmtDefs){0};
        rmt_defs_add(out, &def);
        return;
    }
    if (out->count > 0 || out->open) {
        return;
    }
    if (((ns & RMT_NS_TYPE) && rmt_implicit_extern_allowed(tree, module, head) &&
         rmt_extern_prelude(tree, crate, head, &def, building)) ||
        (!(rmt_scope_prelude_restrictions(tree, module) & CBM_RUST_PRELUDE_DISABLED) &&
         rmt_in_list(RMT_PRELUDE, head))) {
        rmt_defs_add(out, &def);
    }
}

/* Resolve the keyword or name heading a path; returns the index of the
 * first segment still to walk, or -1 when the head is unresolvable. */
static int rmt_start(CBMRustModTree *tree, int module, const char *const *segs, int count,
                     uint8_t final_ns, bool is_use, RmtDefs *cur, bool building) {
    const char *head = segs[0];
    int crate = tree->modules[module].crate;
    if (head[0] == '\0') {
        RmtDef def;
        uint8_t restrictions = rmt_scope_prelude_restrictions(tree, module);
        if (count >= 2 && (restrictions & CBM_RUST_PRELUDE_CORE_ONLY) &&
            (strcmp(segs[1], "std") == 0 ||
             ((restrictions & CBM_RUST_PRELUDE_DISABLED) && strcmp(segs[1], "core") == 0))) {
            RmtVisit visit = {0};
            rmt_lookup(tree, tree->crates[crate].root_module, segs[1], RMT_NS_TYPE, cur, &visit,
                       building, NULL);
            return !cur->open && cur->count == 1 ? 2 : -1;
        }
        if (count < 2 || !rmt_extern_prelude(tree, crate, segs[1], &def, building)) {
            return -1;
        }
        rmt_defs_add(cur, &def);
        return 2;
    }
    if (strcmp(head, "crate") == 0 || strcmp(head, "self") == 0 || strcmp(head, "super") == 0) {
        int target = strcmp(head, "crate") == 0 ? tree->crates[crate].root_module : module;
        int i = strcmp(head, "super") == 0 ? 0 : 1;
        for (; i < count && strcmp(segs[i], "super") == 0 && target >= 0; i++) {
            target = tree->modules[target].parent;
        }
        if (target < 0) {
            return -1;
        }
        RmtDef def = {.kind = RMT_MODULE, .ns = RMT_NS_TYPE, .module = target};
        rmt_defs_add(cur, &def);
        return i;
    }
    if (strcmp(head, "Self") == 0) {
        cur->open = true;
        return -1;
    }
    rmt_resolve_head(tree, module, head, count == 1 ? final_ns : RMT_NS_TYPE, is_use,
                     final_ns == RMT_NS_MACRO, cur, building);
    return 1;
}

/* `prefix.segs[from..count)` in `arena`: the dotted path of an item reached
 * through an external one. */
static const char *rmt_external_path(CBMArena *arena, const char *prefix, const char *const *segs,
                                     int from, int count) {
    if (!prefix) {
        return NULL;
    }
    const char *path = prefix;
    for (int i = from; i < count; i++) {
        path = cbm_arena_sprintf(arena, "%s.%s", path, segs[i]);
    }
    return path;
}

static void rmt_resolve_path(CBMRustModTree *tree, CBMArena *arena, int module,
                             const char *const *segs, int count, uint8_t final_ns, bool is_use,
                             RmtDefs *out, bool building) {
    if (tree->crates[tree->modules[module].crate].edition == CBM_RUST_EDITION_UNKNOWN) {
        out->open = true;
        return;
    }
    RmtDefs cur = {0};
    int i = rmt_start(tree, module, segs, count, final_ns, is_use, &cur, building);
    if (i < 0) {
        out->open = true;
        return;
    }
    for (; i < count; i++) {
        if (cur.open || cur.count != 1) {
            out->open = true;
            return;
        }
        RmtDef def = cur.defs[0];
        bool last = i == count - 1;
        if (def.kind == RMT_EXTERNAL) {
            cur.defs[0].qn = rmt_external_path(arena, def.qn, segs, i, count);
            break;
        }
        if (def.kind == RMT_TYPE || def.kind == RMT_TRAIT || def.kind == RMT_ENUM ||
            def.kind == RMT_ALIAS) {
            if (!last) {
                out->open = true;
                return;
            }
            rmt_defs_add(out, &def);
            out->assoc = segs[i];
            return;
        }
        if (def.kind != RMT_MODULE) {
            out->open = true;
            return;
        }
        RmtVisit visit = {.count = 0};
        cur = (RmtDefs){0};
        rmt_lookup(tree, def.module, segs[i], last ? final_ns : RMT_NS_TYPE, &cur, &visit, building,
                   NULL);
        if (cur.count == 0 && !cur.open) {
            out->open = true;
            return;
        }
    }
    rmt_defs_merge(out, &cur, RMT_NS_ALL);
}

int cbm_rust_modtree_file_count(const CBMRustModTree *tree) {
    return tree ? tree->file_count : 0;
}

static void rmt_trait_index_build(CBMRustModTree *tree);
static void rmt_impl_assoc_index_build(CBMRustModTree *tree);

static bool rmt_extern_prelude(CBMRustModTree *tree, int crate, const char *name,
                               RmtDef *out, bool building);

/* Bounded assumption (macro-declared module): `mod name;` written inside an
 * item macro the expander cannot read is mounted when its file exists and
 * nothing else can bind `name` in that module: no dependency, extern crate
 * or language crate of that name for the crate, no glob import in the
 * module, no other item, import or module of that name there, and no second
 * macro-declared candidate for it. A consumer path through `name` compiles
 * only if the name is bound; with no other candidate, the macro-emitted
 * declaration is that binding. Otherwise the may-declare fact stays. */
/* A dependency key of the crate's package binds `name` whether or not the
 * dependency's crate root is registered. */
static bool rmt_package_binds(const CBMRustModTree *tree, int crate, const char *name) {
    char key[RMT_KEY_MAX];
    int written = snprintf(key, sizeof(key), "%d\x1f%s", crate, name);
    return written < 0 || written >= (int)sizeof(key) ||
           cbm_idxmemo_get(&tree->externs, key) >= 0;
}

static void rmt_mount_macro_mods(CBMRustModTree *tree, RmtMacroMods *pending) {
    for (int i = 0; i < pending->count; i++) {
        const RmtMacroMod cand = pending->items[i];
        /* Raw identifiers compare by their bare spelling on both sides. */
        const char *name = rmt_unraw(cand.decl->name);
        int rivals = 0;
        for (int j = 0; j < pending->count; j++)
            rivals += pending->items[j].module == cand.module &&
                      strcmp(rmt_unraw(pending->items[j].decl->name), name) == 0;
        char key[RMT_KEY_MAX];
        char raw_key[RMT_KEY_MAX];
        int written = snprintf(key, sizeof(key), "%d\x1f%s", cand.module, name);
        int raw_written = snprintf(raw_key, sizeof(raw_key), "%d\x1fr#%s", cand.module, name);
        RmtDef external;
        const char *children_dir = NULL;
        if (rivals != 1 || written < 0 || written >= (int)sizeof(key) || raw_written < 0 ||
            raw_written >= (int)sizeof(raw_key) || tree->modules[cand.module].first_glob >= 0 ||
            cbm_idxmemo_get(&tree->names, key) >= 0 ||
            cbm_idxmemo_get(&tree->names, raw_key) >= 0 || rmt_in_list(RMT_PRELUDE, name) ||
            rmt_package_binds(tree, tree->modules[cand.module].crate, name) ||
            rmt_extern_prelude(tree, tree->modules[cand.module].crate, name, &external, true) ||
            rmt_out_of_line_file(&cand.build, cand.decl, cand.child_dir, cand.inline_scope,
                                 &children_dir) < 0)
            continue;
        rmt_build_mod_decl(&cand.build, cand.decl, cand.module, cand.child_dir, cand.inline_scope,
                           cand.depth);
    }
}

void cbm_rust_modtree_finish(CBMRustModTree *tree, const CBMRustModFacts *const *facts) {
    /* Preserve restrictions even for files whose containing item macro leaves
     * their module mount unknown. No such file may assume a restricted prelude. */
    for (int f = 0; f < tree->file_count; f++)
        for (int i = 0; facts[f] && i < facts[f]->count; i++) {
            const RmfRecord *record = &facts[f]->records[i];
            if (record->op == RMF_PRELUDE || record->op == RMF_MOD_DECL)
                tree->files[f].prelude_restrictions |= record->item;
        }
    if (!tree || !facts) {
        return;
    }
    RmtMacroMods pending = {0};
    for (int c = 0; c < tree->crate_count; c++) {
        int file = rmt_file_id(tree, tree->crates[c].root_rel);
        if (file < 0) {
            continue;
        }
        int root = rmt_new_module(tree, c, -1, file, 0, UINT32_MAX);
        if (root < 0) {
            cbm_free(CBM_MEM_CLASS_OTHER, pending.items);
            return;
        }
        tree->crates[c].root_module = root;
        RmtBuild build = {.tree = tree, .facts = facts, .crate = c, .file = file,
                          .pending = &pending};
        rmt_build_file(&build, root, file, tree->files[file].dir, 0);
    }
    /* After every crate root exists, so a dependency registered after its
     * dependent still reads as a binding. */
    rmt_mount_macro_mods(tree, &pending);
    cbm_free(CBM_MEM_CLASS_OTHER, pending.items);
    int previous_open = tree->import_count + 1;
    for (int pass = 0; pass <= tree->import_count; pass++) {
        for (int i = 0; i < tree->import_count; i++)
            rmt_resolve_import(tree, i);
        int open = 0;
        for (int i = 0; i < tree->import_count; i++)
            open += tree->imports[i].result.open;
        if (open == 0 || open >= previous_open)
            break;
        previous_open = open;
        /* A recursive import may have seen a pending re-export. Retry only
         * those open routes after the other imports have been settled. */
        for (int i = 0; i < tree->import_count; i++)
            if (tree->imports[i].result.open)
                tree->imports[i].state = RMT_IMPORT_PENDING;
    }
    /* Built here, before any reader sees the tree, so readers share it unlocked. */
    rmt_trait_index_build(tree);
    rmt_impl_assoc_index_build(tree);
}

/* ── Queries ──────────────────────────────────────────────────────── */

bool cbm_rust_modtree_has_file(const CBMRustModTree *tree, const char *module_qn) {
    if (!tree || !module_qn) {
        return false;
    }
    int file = cbm_idxmemo_get(&tree->file_by_qn, module_qn);
    return file >= 0 && tree->files[file].first_module >= 0;
}

bool cbm_rust_modtree_extern_outside(const CBMRustModTree *tree, const char *module_qn,
                                     const char *name) {
    int file = tree && module_qn && name ? cbm_idxmemo_get(&tree->file_by_qn, module_qn) : -1;
    if (file < 0 || tree->files[file].first_module < 0)
        return false;
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        char key[RMT_KEY_MAX];
        int written = snprintf(key, sizeof(key), "%d\x1f%s", tree->modules[m].crate, name);
        if (written <= 0 || written >= (int)sizeof(key) ||
            cbm_idxmemo_get(&tree->externs, key) != 0)
            return false;
    }
    return true;
}

const char *cbm_rust_modtree_extern_root(const CBMRustModTree *tree, const char *module_qn,
                                         const char *name) {
    int file = tree && module_qn && name ? cbm_idxmemo_get(&tree->file_by_qn, module_qn) : -1;
    if (file < 0) {
        return NULL;
    }
    const char *root = NULL;
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        char key[RMT_KEY_MAX];
        int written = snprintf(key, sizeof(key), "%d\x1f%s", tree->modules[m].crate, name);
        int32_t target =
            written > 0 && written < (int)sizeof(key) ? cbm_idxmemo_get(&tree->externs, key) : -1;
        int root_module = target > 0 ? tree->crates[target - 1].root_module : -1;
        const char *qn = root_module >= 0 ? tree->files[tree->modules[root_module].file].qn : NULL;
        if (!qn || (root && strcmp(root, qn) != 0)) {
            return NULL;
        }
        root = qn;
    }
    return root;
}

const char *cbm_rust_modtree_crate_name(const CBMRustModTree *tree, const char *root_qn) {
    if (!tree || !root_qn)
        return NULL;
    const char *name = NULL;
    for (int c = 0; c < tree->crate_count; c++) {
        int root = tree->crates[c].root_module;
        const char *extern_name = tree->crates[c].extern_name;
        if (root < 0 || strcmp(tree->files[tree->modules[root].file].qn, root_qn) != 0)
            continue;
        if (!extern_name || (name && strcmp(name, extern_name) != 0))
            return NULL;
        name = extern_name;
    }
    return name;
}

static bool rmt_is_ancestor(const CBMRustModTree *tree, int ancestor, int module) {
    for (int m = tree->modules[module].parent; m >= 0; m = tree->modules[m].parent) {
        if (m == ancestor) {
            return true;
        }
    }
    return false;
}

bool cbm_rust_modtree_file_outside(const CBMRustModTree *tree, const char *caller_qn,
                                   const char *owner_qn) {
    if (!tree || !caller_qn || !owner_qn) {
        return false;
    }
    int caller = cbm_idxmemo_get(&tree->file_by_qn, caller_qn);
    int owner = cbm_idxmemo_get(&tree->file_by_qn, owner_qn);
    if (caller < 0 || owner < 0 || tree->files[caller].first_module < 0 ||
        tree->files[owner].first_module < 0) {
        return false;
    }
    for (int m = tree->files[caller].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        for (int up = m; up >= 0; up = tree->modules[up].parent) {
            if (tree->modules[up].file == owner) {
                return false;
            }
        }
    }
    return true;
}

static bool rmt_contains(const RmtModule *module, uint32_t offset) {
    return module->start <= offset && offset < module->end;
}

/* True when no other module of the file at `offset` nests inside `module`. */
static bool rmt_innermost(const CBMRustModTree *tree, int file, int module, uint32_t offset) {
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        if (m != module && rmt_contains(&tree->modules[m], offset) &&
            rmt_is_ancestor(tree, module, m)) {
            return false;
        }
    }
    return true;
}

bool cbm_rust_modtree_prelude_allows(const CBMRustModTree *tree, const char *module_qn,
                                     uint32_t offset, const char *name) {
    if (!tree || !module_qn || !name)
        return false;
    int file = cbm_idxmemo_get(&tree->file_by_qn, module_qn);
    if (file >= 0 && tree->files[file].first_module < 0) {
        if (strcmp(name, "TryFrom") == 0 || strcmp(name, "TryInto") == 0 ||
            strcmp(name, "FromIterator") == 0 || strcmp(name, "Future") == 0)
            return false;
        /* These legacy names are in every edition. Without a module mount,
         * any recorded restriction may belong to an ancestor: refuse it. */
        uint8_t restrictions = 0;
        for (int f = 0; f < tree->file_count; f++)
            restrictions |= tree->files[f].prelude_restrictions;
        return !(restrictions & CBM_RUST_PRELUDE_DISABLED) &&
               !((restrictions & CBM_RUST_PRELUDE_CORE_ONLY) && strcmp(name, "ToString") == 0);
    }
    bool any = false;
    for (int m = file >= 0 ? tree->files[file].first_module : -1; m >= 0;
         m = tree->modules[m].next_in_file) {
        if (!rmt_contains(&tree->modules[m], offset) || !rmt_innermost(tree, file, m, offset))
            continue;
        CBMRustEdition edition = tree->crates[tree->modules[m].crate].edition;
        if (edition == CBM_RUST_EDITION_UNKNOWN ||
            ((strcmp(name, "TryFrom") == 0 || strcmp(name, "TryInto") == 0 ||
              strcmp(name, "FromIterator") == 0) &&
             edition != CBM_RUST_EDITION_2021 && edition != CBM_RUST_EDITION_2024) ||
            (strcmp(name, "Future") == 0 && edition != CBM_RUST_EDITION_2024))
            return false;
        uint8_t restrictions = rmt_scope_prelude_restrictions(tree, m);
        if ((restrictions & CBM_RUST_PRELUDE_DISABLED) ||
            ((restrictions & CBM_RUST_PRELUDE_CORE_ONLY) && strcmp(name, "ToString") == 0))
            return false;
        any = true;
    }
    return any;
}

static CBMRustPathResult rmt_classify(const RmtDefs *defs) {
    CBMRustPathResult unknown = {.kind = CBM_RUST_PATH_UNKNOWN};
    if (defs->open || defs->count == 0) {
        return unknown;
    }
    if (defs->assoc) {
        const RmtDef *owner = &defs->defs[0];
        return (CBMRustPathResult){.kind = CBM_RUST_PATH_ASSOC,
                                   .qn = owner->qn,
                                   .assoc = defs->assoc,
                                   .is_trait = owner->kind == RMT_TRAIT};
    }
    if (defs->count != 1) {
        return (CBMRustPathResult){.kind = CBM_RUST_PATH_AMBIGUOUS};
    }
    const RmtDef *def = &defs->defs[0];
    if (def->kind == RMT_OPAQUE) {
        return unknown;
    }
    if (def->kind == RMT_FN) {
        return (CBMRustPathResult){
            .kind = CBM_RUST_PATH_FUNCTION, .qn = def->qn, .line = def->line};
    }
    return (CBMRustPathResult){.kind = CBM_RUST_PATH_NOT_FUNCTION,
                               .qn = def->qn,
                               .value_type = def->kind == RMT_VALUE ? def->value_type : NULL};
}

static bool rmt_same_result(const CBMRustPathResult *a, const CBMRustPathResult *b) {
    if (a->kind != b->kind || a->is_trait != b->is_trait) {
        return false;
    }
    bool same_qn = (!a->qn && !b->qn) || (a->qn && b->qn && strcmp(a->qn, b->qn) == 0);
    bool same_assoc =
        (!a->assoc && !b->assoc) || (a->assoc && b->assoc && strcmp(a->assoc, b->assoc) == 0);
    return same_qn && same_assoc && a->line == b->line;
}

/* Replace a head bound by an enclosing block's `use` with that use's path. */
static int rmt_apply_block_uses(CBMArena *arena, const char **segs, int count,
                                const char *const *names, const char *const *paths, int extra,
                                bool *is_use) {
    for (int i = extra - 1; i >= 0; i--) {
        if (!names[i] || !paths[i] || strcmp(names[i], segs[0]) != 0) {
            continue;
        }
        *is_use = true;
        const char *head[RMT_MAX_SEGS];
        int head_count = rmt_split_path(arena, paths[i], head, RMT_MAX_SEGS);
        if (head_count <= 0 || head_count + count - 1 > RMT_MAX_SEGS) {
            return -1;
        }
        memmove(segs + head_count, segs + 1, (size_t)(count - 1) * sizeof(*segs));
        memcpy(segs, head, (size_t)head_count * sizeof(*segs));
        return head_count + count - 1;
    }
    return count;
}

static CBMRustPathResult rmt_classify_type(const RmtDefs *defs) {
    CBMRustPathResult unknown = {.kind = CBM_RUST_PATH_UNKNOWN};
    if (defs->open || defs->assoc || defs->count == 0) {
        return unknown;
    }
    if (defs->count > 1)
        return (CBMRustPathResult){.kind = CBM_RUST_PATH_AMBIGUOUS};
    const RmtDef *def = &defs->defs[0];
    if (def->kind == RMT_TYPE || def->kind == RMT_ENUM || def->kind == RMT_TRAIT ||
        def->kind == RMT_ALIAS) {
        if (def->line == 0)
            return (CBMRustPathResult){.kind = CBM_RUST_PATH_AMBIGUOUS};
        return (CBMRustPathResult){.kind = CBM_RUST_PATH_TYPE,
                                   .qn = def->qn,
                                   .is_trait = def->kind == RMT_TRAIT,
                                   .conditional = def->conditional};
    }
    if (def->kind == RMT_EXTERNAL) {
        return (CBMRustPathResult){
            .kind = CBM_RUST_PATH_NOT_FUNCTION, .qn = def->qn, .conditional = def->conditional};
    }
    return unknown;
}

const char *cbm_rust_modtree_item_module(const CBMRustModTree *tree, CBMArena *arena,
                                         const char *item_qn) {
    if (!tree || !arena || !item_qn) {
        return NULL;
    }
    char *prefix = cbm_arena_strdup(arena, item_qn);
    for (char *dot = strrchr(prefix, '.'); dot; dot = strrchr(prefix, '.')) {
        *dot = '\0';
        if (cbm_idxmemo_get(&tree->file_by_qn, prefix) >= 0) {
            return prefix;
        }
    }
    return NULL;
}

/* Merge only exports of block globs whose paths name a proved module.
 * Unknown sources keep the answer open even when another glob has a match. */
static bool rmt_block_globs(CBMRustModTree *tree, CBMArena *arena, int module, const char *name,
                            uint8_t ns, const char *const *names, const char *const *paths,
                            int extra, RmtDefs *out) {
    bool has_glob = false;
    for (int i = 0; i < extra; i++) {
        if (!names[i] || !paths[i] || strcmp(names[i], "*") != 0)
            continue;
        has_glob = true;
        const char *segs[RMT_MAX_SEGS];
        int count = rmt_split_path(arena, paths[i], segs, RMT_MAX_SEGS);
        if (count < 2 || strcmp(segs[count - 1], "*") != 0) {
            out->open = true;
            continue;
        }
        RmtDefs source = {0};
        rmt_resolve_path(tree, arena, module, segs, count - 1, RMT_NS_TYPE, true, &source, false);
        if (source.open || source.count != 1 || source.defs[0].kind != RMT_MODULE) {
            out->open = true;
            continue;
        }
        RmtDefs found = {0};
        RmtVisit visit = {.count = 0};
        RmtSeers seers = {.count = 1, .items = {module}};
        rmt_lookup(tree, source.defs[0].module, name, ns, &found, &visit, false, &seers);
        rmt_defs_merge(out, &found, ns);
    }
    return has_glob && (out->open || out->count > 0);
}

/* Resolve in the source module selected by offset, or the exact physical
 * module QN for a definition header; multiple crate mounts must agree. */
static CBMRustPathResult rmt_resolve_query(const CBMRustModTree *tree, CBMArena *arena,
                                           const char *module_qn, uint32_t offset,
                                           bool every_module, const char *path, uint8_t ns,
                                           const char *const *extra_names,
                                           const char *const *extra_paths, int extra_count) {
    CBMRustPathResult unknown = {.kind = CBM_RUST_PATH_UNKNOWN};
    if (!tree || !arena || !module_qn || !path || path[0] == '<') {
        return unknown;
    }
    int file = cbm_idxmemo_get(&tree->file_by_qn, module_qn);
    const char *segs[RMT_MAX_SEGS];
    int count = file >= 0 ? rmt_split_path(arena, path, segs, RMT_MAX_SEGS) : -1;
    bool block_globs = count == 1;
    for (int i = 0; block_globs && i < extra_count; i++) {
        if (extra_names[i] && strcmp(extra_names[i], segs[0]) == 0)
            block_globs = false;
    }
    bool is_use = false;
    if (count > 0) {
        count = rmt_apply_block_uses(arena, segs, count, extra_names, extra_paths, extra_count,
                                     &is_use);
    }
    if (count <= 0) {
        return unknown;
    }
    CBMRustPathResult agreed = unknown;
    bool any = false;
    /* Queries never resolve an import (all are DONE after finish), so the
     * shared tree is only read here. */
    CBMRustModTree *readonly = (CBMRustModTree *)tree;
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        if (every_module && strcmp(tree->modules[m].qn, module_qn) != 0) {
            continue;
        }
        if (!every_module &&
            (!rmt_contains(&tree->modules[m], offset) || !rmt_innermost(tree, file, m, offset))) {
            continue;
        }
        if (tree->crates[tree->modules[m].crate].edition == CBM_RUST_EDITION_UNKNOWN)
            return (CBMRustPathResult){.kind = CBM_RUST_PATH_EDITION_UNKNOWN};
        RmtDefs defs = {0};
        if (!block_globs || !rmt_block_globs(readonly, arena, m, segs[0], ns, extra_names,
                                             extra_paths, extra_count, &defs)) {
            rmt_resolve_path(readonly, arena, m, segs, count, ns, is_use, &defs, false);
        }
        CBMRustPathResult result =
            ns == RMT_NS_TYPE ? rmt_classify_type(&defs) : rmt_classify(&defs);
        if (result.kind == CBM_RUST_PATH_UNKNOWN)
            return unknown;
        if (any && !rmt_same_result(&agreed, &result))
            return (CBMRustPathResult){.kind = CBM_RUST_PATH_AMBIGUOUS};
        agreed = result;
        any = true;
    }
    return agreed;
}

const char *cbm_rust_modtree_resolve_nominal_type(const CBMRustModTree *tree, CBMArena *arena,
                                                  const char *module_qn, const char *path) {
    if (!tree || !arena || !module_qn || !path) {
        return NULL;
    }
    int file = cbm_idxmemo_get(&tree->file_by_qn, module_qn);
    const char *segs[RMT_MAX_SEGS];
    int count = file >= 0 ? rmt_split_path(arena, path, segs, RMT_MAX_SEGS) : -1;
    if (count <= 0) {
        return NULL;
    }
    const char *agreed = NULL;
    CBMRustModTree *readonly = (CBMRustModTree *)tree;
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        RmtDefs defs = {0};
        rmt_resolve_path(readonly, arena, m, segs, count, RMT_NS_TYPE, false, &defs, false);
        if (!defs.open && defs.count == 0) {
            continue; /* the name is not in scope here, so no impl here can name it */
        }
        const RmtDef *def = &defs.defs[0];
        if (defs.open || defs.count != 1 || defs.assoc || !def->qn ||
            (def->kind != RMT_TYPE && def->kind != RMT_ENUM) ||
            (agreed && strcmp(agreed, def->qn) != 0)) {
            return NULL;
        }
        agreed = def->qn;
    }
    return agreed;
}

CBMRustPathResult cbm_rust_modtree_resolve(const CBMRustModTree *tree, CBMArena *arena,
                                           const char *module_qn, uint32_t offset, const char *path,
                                           const char *const *extra_names,
                                           const char *const *extra_paths, int extra_count) {
    return rmt_resolve_query(tree, arena, module_qn, offset, false, path, RMT_NS_VALUE, extra_names,
                             extra_paths, extra_count);
}

CBMRustPathResult cbm_rust_modtree_resolve_type(const CBMRustModTree *tree, CBMArena *arena,
                                                const char *module_qn, uint32_t offset,
                                                const char *path, const char *const *extra_names,
                                                const char *const *extra_paths, int extra_count) {
    return rmt_resolve_query(tree, arena, module_qn, offset, false, path, RMT_NS_TYPE, extra_names,
                             extra_paths, extra_count);
}

CBMRustPathResult cbm_rust_modtree_resolve_macro(const CBMRustModTree *tree, CBMArena *arena,
                                                 const char *module_qn, uint32_t offset,
                                                 const char *path, const char *const *extra_names,
                                                 const char *const *extra_paths, int extra_count) {
    return rmt_resolve_query(tree, arena, module_qn, offset, false, path, RMT_NS_MACRO, extra_names,
                             extra_paths, extra_count);
}

CBMRustPathResult cbm_rust_modtree_resolve_type_in_file(const CBMRustModTree *tree, CBMArena *arena,
                                                        const char *module_qn, const char *path) {
    return rmt_resolve_query(tree, arena, module_qn, 0, true, path, RMT_NS_TYPE, NULL, NULL, 0);
}

/* The module path after the crate of a std/core/alloc QN (".fmt.Debug"),
 * NULL for any other QN. */
static const char *rmt_facade_tail(const char *qn) {
    bool facade = strncmp(qn, "std.", 4) == 0 || strncmp(qn, "core.", 5) == 0 ||
                  strncmp(qn, "alloc.", 6) == 0;
    return facade ? strchr(qn, '.') : NULL;
}

/* The standard facade re-exports a trait under the same module path. */
static bool rmt_trait_identity_equal(const char *a, const char *b) {
    if (!a || !b)
        return false;
    if (strcmp(a, b) == 0)
        return true;
    const char *at = rmt_facade_tail(a);
    const char *bt = rmt_facade_tail(b);
    return at && bt && strcmp(at, bt) == 0;
}

/* What a trait-in-scope query compares an entry's binding by: the type
 * namespace item it binds, read from the finished imports. */
static CBMRustPathResult rmt_entry_type(CBMRustModTree *tree, const RmtEntry *entry) {
    RmtDefs bound = {0};
    if (entry->import >= 0)
        rmt_import_defs(tree, entry->import, false, RMT_NS_TYPE, &bound);
    else
        rmt_defs_add(&bound, &entry->def);
    return rmt_classify_type(&bound);
}

/* The bound group of the trait identity of `qn`, -1 when no entry binds it.
 * Two QNs share a group exactly when rmt_trait_identity_equal holds. */
static int rmt_trait_group(const RmtTraitIndex *index, const char *qn) {
    const char *tail = rmt_facade_tail(qn);
    return tail ? cbm_idxmemo_get(&index->facade, tail) : cbm_idxmemo_get(&index->plain, qn);
}

/* Count one member of `group` into `start` (two slots longer than the
 * group count), to be turned into offsets by rmt_index_offsets. */
static void rmt_index_count(int *start, int group) {
    start[group + 2]++;
}

/* After counting: start[g + 1] is where group g's ids begin, so that
 * filling with ids[start[g + 1]++] leaves start[g] .. start[g + 1] per group. */
static void rmt_index_offsets(int *start, int groups) {
    for (int g = 2; g <= groups; g++)
        start[g] += start[g - 1];
}

static bool rmt_trait_index_fill(CBMRustModTree *tree, RmtTraitIndex *index, int *group_of) {
    int groups = 0;
    for (int i = 0; i < tree->entry_count; i++) {
        const RmtEntry *entry = &tree->entries[i];
        group_of[i] = -1;
        if (strcmp(entry->name, "_") == 0) {
            if (entry->import >= 0)
                rmt_index_count(index->anon_start, entry->module);
            continue;
        }
        const char *qn = rmt_entry_type(tree, entry).qn;
        if (!qn)
            continue;
        int group = rmt_trait_group(index, qn);
        if (group < 0) {
            const char *tail = rmt_facade_tail(qn);
            const char *key = cbm_arena_strdup(&tree->arena, tail ? tail : qn);
            group = groups++;
            cbm_idxmemo_put_if_absent(tail ? &index->facade : &index->plain, &tree->arena, key,
                                      group);
            if (rmt_trait_group(index, qn) != group)
                return false;
        }
        group_of[i] = group;
        rmt_index_count(index->bound_start, group);
    }
    rmt_index_offsets(index->bound_start, groups);
    rmt_index_offsets(index->anon_start, tree->module_count);
    for (int i = 0; i < tree->entry_count; i++) {
        const RmtEntry *entry = &tree->entries[i];
        if (group_of[i] >= 0)
            index->bound_ids[index->bound_start[group_of[i] + 1]++] = i;
        else if (entry->import >= 0 && strcmp(entry->name, "_") == 0)
            index->anon_ids[index->anon_start[entry->module + 1]++] = i;
    }
    return true;
}

/* Index the entries a trait-in-scope query can accept. Only a finished tree
 * whose imports are all decided is indexed: then each entry's binding is a
 * fixed fact, the same one every query reads. */
static void rmt_trait_index_build(CBMRustModTree *tree) {
    RmtTraitIndex *index = &tree->trait_index;
    rmt_trait_index_free(index);
    for (int i = 0; i < tree->import_count; i++)
        if (tree->imports[i].state != RMT_IMPORT_DONE)
            return;
    size_t entries = (size_t)tree->entry_count;
    size_t modules = (size_t)tree->module_count;
    int *group_of = cbm_alloc(CBM_MEM_CLASS_OTHER, (entries + 1) * sizeof(int));
    index->bound_start = cbm_calloc(CBM_MEM_CLASS_OTHER, (entries + 2) * sizeof(int));
    index->bound_ids = cbm_alloc(CBM_MEM_CLASS_OTHER, (entries + 1) * sizeof(int));
    index->anon_start = cbm_calloc(CBM_MEM_CLASS_OTHER, (modules + 2) * sizeof(int));
    index->anon_ids = cbm_alloc(CBM_MEM_CLASS_OTHER, (entries + 1) * sizeof(int));
    bool ready = group_of && index->bound_start && index->bound_ids && index->anon_start &&
                 index->anon_ids && rmt_trait_index_fill(tree, index, group_of);
    cbm_free(CBM_MEM_CLASS_OTHER, group_of);
    if (!ready) {
        rmt_trait_index_free(index);
        return;
    }
    index->entry_count = tree->entry_count;
    index->module_count = tree->module_count;
    index->ready = true;
}

/* The tree's trait index, NULL when queries must scan every entry. */
static const RmtTraitIndex *rmt_trait_index(const CBMRustModTree *tree) {
    const RmtTraitIndex *index = &tree->trait_index;
    bool current =
        index->entry_count == tree->entry_count && index->module_count == tree->module_count;
    return index->ready && current ? index : NULL;
}

/* Anonymous imports introduce no name, but their traits are in scope and
 * propagate through globs. The visibility chain is the same as named globs. */
static bool rmt_anonymous_trait(CBMRustModTree *tree, int module, const char *qn,
                                RmtVisit *visit, const RmtSeers *seers) {
    if (!rmt_visit(visit, module))
        return false;
    /* The index lists exactly the entries the module and `_` tests below keep. */
    const RmtTraitIndex *index = rmt_trait_index(tree);
    int first = index ? index->anon_start[module] : 0;
    int last = index ? index->anon_start[module + 1] : tree->entry_count;
    for (int k = first; k < last; k++) {
        int i = index ? index->anon_ids[k] : k;
        const RmtEntry *entry = &tree->entries[i];
        if (entry->module != module || strcmp(entry->name, "_") != 0 ||
            entry->import < 0 || !rmt_seen(tree, entry->vis, seers))
            continue;
        CBMRustPathResult result = rmt_classify_type(&tree->imports[entry->import].result);
        if (rmt_trait_identity_equal(result.qn, qn))
            return true;
    }
    RmtSeers chain = seers ? *seers : (RmtSeers){0};
    if (chain.count >= RMT_VISIT_MAX)
        return false;
    chain.items[chain.count++] = module;
    for (int g = tree->modules[module].first_glob; g >= 0; g = tree->imports[g].next_glob) {
        const RmtImport *imp = &tree->imports[g];
        if (!rmt_seen(tree, imp->vis, seers) || imp->result.open || imp->result.count != 1 ||
            imp->result.defs[0].kind != RMT_MODULE)
            continue;
        RmtVisit branch = *visit;
        if (rmt_anonymous_trait(tree, imp->result.defs[0].module, qn, &branch, &chain))
            return true;
    }
    return false;
}

/* Search actual bindings, not the trait's spelling. An alias supplied by an
 * ancestor glob or a prelude can have any name. Each candidate name must
 * resolve from the call's module, which checks shadowing and glob ambiguity. */
bool cbm_rust_modtree_trait_in_scope(const CBMRustModTree *tree, CBMArena *arena,
                                      const char *module_qn, uint32_t offset, const char *qn,
                                      const char *const *extra_names,
                                      const char *const *extra_paths, int extra_count,
                                      const char *const *extra_globs, int glob_count) {
    int file = tree && module_qn ? cbm_idxmemo_get(&tree->file_by_qn, module_qn) : -1;
    if (file < 0 || !qn)
        return false;
    CBMRustModTree *readonly = (CBMRustModTree *)tree;
    /* The index lists exactly the entries the identity and `_` tests below
     * keep, in the same order; without it every entry is tried. */
    const RmtTraitIndex *index = rmt_trait_index(tree);
    int group = index ? rmt_trait_group(index, qn) : -1;
    const int *ids = index && group >= 0 ? index->bound_ids + index->bound_start[group] : NULL;
    int candidates = !index ? tree->entry_count
                     : ids  ? index->bound_start[group + 1] - index->bound_start[group]
                            : 0;
    bool any = false;
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        if (!rmt_contains(&tree->modules[m], offset) || !rmt_innermost(tree, file, m, offset))
            continue;
        if (tree->crates[tree->modules[m].crate].edition == CBM_RUST_EDITION_UNKNOWN)
            return false;
        RmtVisit visit = {0};
        bool visible = rmt_anonymous_trait(readonly, m, qn, &visit, NULL);
        /* Anonymous block imports accumulate; a later `as _` does not
         * replace an earlier trait, because neither binds an identifier. */
        for (int i = 0; !visible && i < extra_count; i++) {
            if (!extra_names[i] || !extra_paths[i] || strcmp(extra_names[i], "_") != 0)
                continue;
            const char *segs[RMT_MAX_SEGS];
            int count = rmt_split_path(arena, extra_paths[i], segs, RMT_MAX_SEGS);
            bool is_use = true;
            if (count > 0)
                count = rmt_apply_block_uses(arena, segs, count, extra_names, extra_paths,
                                             extra_count, &is_use);
            RmtDefs found = {0};
            if (count > 0)
                rmt_resolve_path(readonly, arena, m, segs, count, RMT_NS_TYPE, is_use, &found, false);
            visible = rmt_trait_identity_equal(rmt_classify_type(&found).qn, qn);
        }
        for (int k = 0; !visible && k < candidates; k++) {
            const RmtEntry *entry = &tree->entries[ids ? ids[k] : k];
            CBMRustPathResult candidate = rmt_entry_type(readonly, entry);
            if (!rmt_trait_identity_equal(candidate.qn, qn) || strcmp(entry->name, "_") == 0)
                continue;
            const char *segs[RMT_MAX_SEGS] = {entry->name};
            bool is_use = false;
            int count = rmt_apply_block_uses(arena, segs, 1, extra_names, extra_paths, extra_count,
                                             &is_use);
            RmtDefs found = {0};
            if (count > 0)
                rmt_resolve_path(readonly, arena, m, segs, count, RMT_NS_TYPE, is_use, &found, false);
            for (int g = 0; g < glob_count && count == 1; g++) {
                const char *glob_segs[RMT_MAX_SEGS];
                int n = rmt_split_path(arena, extra_globs[g], glob_segs, RMT_MAX_SEGS);
                RmtDefs source = {0};
                if (n > 0)
                    rmt_resolve_path(readonly, arena, m, glob_segs, n, RMT_NS_TYPE, true, &source, false);
                if (source.open || source.count != 1 || source.defs[0].kind != RMT_MODULE)
                    continue;
                RmtSeers seers = {.items = {m}, .count = 1};
                RmtVisit branch = {0};
                RmtDefs supplied = {0};
                rmt_lookup(readonly, source.defs[0].module, entry->name, RMT_NS_TYPE,
                            &supplied, &branch, false, &seers);
                if (supplied.count && !supplied.open) {
                    found.open = false;
                    rmt_defs_merge(&found, &supplied, RMT_NS_TYPE);
                }
            }
            CBMRustPathResult result = rmt_classify_type(&found);
            visible = rmt_trait_identity_equal(result.qn, qn);
        }
        if (!visible)
            return false;
        any = true;
    }
    return any;
}

/* The resolved result of the module-level `use` in `module` that binds
 * `local` to the path `segs`, NULL when the module declares none. */
static const RmtDefs *rmt_declared_use(const CBMRustModTree *tree, int module, const char *local,
                                       const char *const *segs, int count) {
    char key[RMT_KEY_MAX];
    int written = snprintf(key, sizeof(key), "%d\x1f%s", module, local);
    if (written < 0 || written >= (int)sizeof(key)) {
        return NULL;
    }
    for (int e = cbm_idxmemo_get(&tree->names, key); e >= 0; e = tree->entries[e].next) {
        int id = tree->entries[e].import;
        if (id < 0 || tree->imports[id].seg_count != count ||
            tree->imports[id].state != RMT_IMPORT_DONE) {
            continue;
        }
        bool same = true;
        for (int i = 0; same && i < count; i++) {
            same = strcmp(tree->imports[id].segs[i], segs[i]) == 0;
        }
        if (same) {
            return &tree->imports[id].result;
        }
    }
    return NULL;
}

/* Fold one module's answer into `agreed`; false when it is undecided or
 * names other items than the modules before it. */
static bool rmt_use_agree(RmtDefs *agreed, bool *any, const RmtDefs *defs) {
    if (defs->open || defs->count == 0 || defs->assoc) {
        return false;
    }
    if (!*any) {
        *agreed = *defs;
        *any = true;
        return true;
    }
    if (agreed->count != defs->count) {
        return false;
    }
    for (int i = 0; i < defs->count; i++) {
        bool found = false;
        for (int j = 0; !found && j < agreed->count; j++) {
            found = rmt_same_def(&agreed->defs[j], &defs->defs[i]);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

/* Write the project items or modules of an agreed `use` answer;
 * -1 when an item has no materializable QN. */
static int rmt_use_emit(const CBMRustModTree *tree, const RmtDefs *agreed, const char **out_qns,
                        int max) {
    for (int i = 0; i < agreed->count; i++) {
        if (agreed->defs[i].kind != RMT_EXTERNAL && agreed->defs[i].line == 0 &&
            agreed->defs[i].kind != RMT_MODULE && agreed->defs[i].kind != RMT_OPAQUE)
            return 0;
        for (int j = 0; j < i; j++)
            if ((agreed->defs[i].ns & agreed->defs[j].ns) != 0)
                return 0;
    }
    int found = 0;
    for (int i = 0; i < agreed->count; i++) {
        const RmtDef *def = &agreed->defs[i];
        if (def->kind == RMT_EXTERNAL) {
            continue;
        }
        const char *qn = def->kind == RMT_MODULE ? tree->modules[def->module].qn : def->qn;
        if (def->kind == RMT_OPAQUE || !qn) {
            return -1;
        }
        if (found < max) {
            out_qns[found++] = qn;
        }
    }
    return found;
}

int cbm_rust_modtree_use_items_at(const CBMRustModTree *tree, CBMArena *arena, const char *rel_path,
                                  uint32_t offset, const char *local_name, const char *path,
                                  const char **out_qns, int max) {
    if (!tree || !arena || !rel_path || !local_name || !path || !out_qns) {
        return -1;
    }
    int file = rmt_file_id(tree, rel_path);
    const char *segs[RMT_MAX_SEGS];
    int count = file >= 0 ? rmt_split_path(arena, path, segs, RMT_MAX_SEGS) : -1;
    if (count <= 0) {
        return -1;
    }
    RmtDefs agreed = {0};
    bool any = false;
    CBMRustModTree *readonly = (CBMRustModTree *)tree; /* imports are DONE: only read */
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        if (!rmt_contains(&tree->modules[m], offset) || !rmt_innermost(tree, file, m, offset)) {
            continue;
        }
        const RmtDefs *declared = rmt_declared_use(tree, m, local_name, segs, count);
        RmtDefs defs = {0};
        if (!declared) {
            rmt_resolve_path(readonly, arena, m, segs, count, RMT_NS_ALL, true, &defs, false);
        }
        if (!rmt_use_agree(&agreed, &any, declared ? declared : &defs)) {
            return -1;
        }
    }
    return any ? rmt_use_emit(tree, &agreed, out_qns, max) : -1;
}

int cbm_rust_modtree_use_items(const CBMRustModTree *tree, CBMArena *arena, const char *rel_path,
                               const char *local_name, const char *path, const char **out_qns,
                               int max) {
    if (!tree || !arena || !rel_path || !local_name || !path || !out_qns) {
        return -1;
    }
    int file = rmt_file_id(tree, rel_path);
    const char *segs[RMT_MAX_SEGS];
    int count = file >= 0 ? rmt_split_path(arena, path, segs, RMT_MAX_SEGS) : -1;
    if (count <= 0) {
        return -1;
    }
    RmtDefs agreed = {0};
    bool any = false;
    for (int m = tree->files[file].first_module; m >= 0; m = tree->modules[m].next_in_file) {
        const RmtDefs *declared = rmt_declared_use(tree, m, local_name, segs, count);
        if (declared && !rmt_use_agree(&agreed, &any, declared)) {
            return -1;
        }
    }
    CBMRustModTree *readonly = (CBMRustModTree *)tree; /* imports are DONE: only read */
    for (int m = any ? -1 : tree->files[file].first_module; m >= 0;
         m = tree->modules[m].next_in_file) {
        RmtDefs defs = {0};
        rmt_resolve_path(readonly, arena, m, segs, count, RMT_NS_ALL, true, &defs, false);
        if (!rmt_use_agree(&agreed, &any, &defs)) {
            return -1;
        }
    }
    return any ? rmt_use_emit(tree, &agreed, out_qns, max) : -1;
}

/* ── Associated types of trait impls ──────────────────────────────── */

/* Resolve every impl's self path in its module and chain the entries of
 * each (self QN, name) key. */
static void rmt_impl_assoc_index_build(CBMRustModTree *tree) {
    for (int i = 0; i < tree->impl_assoc_count; i++) {
        RmtImplAssoc *entry = &tree->impl_assocs[i];
        CBMRustPathResult self = cbm_rust_modtree_resolve_type_in_file(
            tree, &tree->arena, tree->modules[entry->module].qn, entry->self_path);
        if (self.kind != CBM_RUST_PATH_TYPE || self.is_trait || !self.qn)
            continue;
        entry->self_qn = self.qn;
        const char *key = cbm_arena_sprintf(&tree->arena, "%s\x1f%s", self.qn, entry->name);
        int32_t first = cbm_idxmemo_get(&tree->impl_assoc_by_self, key);
        if (first < 0) {
            cbm_idxmemo_put_if_absent(&tree->impl_assoc_by_self, &tree->arena, key, i);
            continue;
        }
        entry->next = tree->impl_assocs[first].next;
        tree->impl_assocs[first].next = i;
    }
}

int cbm_rust_modtree_impl_assocs(const CBMRustModTree *tree, CBMArena *arena,
                                 const char *self_qn, const char *name, CBMRustImplAssoc *out,
                                 int max) {
    if (!tree || !self_qn || !name)
        return 0;
    const char *key = cbm_arena_sprintf(arena, "%s\x1f%s", self_qn, name);
    int count = 0;
    for (int at = key ? cbm_idxmemo_get(&tree->impl_assoc_by_self, key) : -1; at >= 0;
         at = tree->impl_assocs[at].next) {
        if (count == max)
            return -1;
        const RmtImplAssoc *entry = &tree->impl_assocs[at];
        out[count++] = (CBMRustImplAssoc){.self_text = entry->self_text,
                                          .value = entry->value,
                                          .module_qn = tree->modules[entry->module].qn,
                                          .unreadable = entry->unreadable};
    }
    return count;
}
