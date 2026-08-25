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

#include "pipeline/lsp_surface.h"
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
#include "foundation/log.h"
#include "foundation/compat_fs.h"
#include "foundation/platform.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/* ── Constants ─────────────────────────────────────────────────── */

enum {
    PXC_MAX_FILE_BYTES_FACTOR = 100, /* same cap pass_calls.c uses for source size */
    PXC_ITOA_BUF = 16,
};

#if defined(CBM_INCREMENTAL_TEST_API) && CBM_INCREMENTAL_TEST_API
static atomic_bool g_pxc_test_fail_collect_alloc = false;
static atomic_bool g_pxc_test_fail_target_route_alloc = false;
static atomic_bool g_pxc_test_poison_non_rust_registry = false;
static atomic_int g_pxc_test_destination_copy_position = 0;

void cbm_pxc_test_fail_collect_alloc_once(void) {
    atomic_store(&g_pxc_test_fail_collect_alloc, true);
}

void cbm_pxc_test_fail_target_route_alloc_once(void) {
    atomic_store(&g_pxc_test_fail_target_route_alloc, true);
}

void cbm_pxc_test_poison_non_rust_registry_once(void) {
    atomic_store(&g_pxc_test_poison_non_rust_registry, true);
}

void cbm_pxc_test_poison_non_rust_registry(CBMArena *arena) {
    if (atomic_exchange(&g_pxc_test_poison_non_rust_registry, false)) {
        cbm_arena_test_fail_after(arena, 0);
        (void)cbm_arena_alloc(arena, 1);
    }
}

void cbm_pxc_test_fail_destination_copy_at(int copy_position) {
    atomic_store(&g_pxc_test_destination_copy_position, copy_position);
}

static bool pxc_test_fail_collect_alloc(void) {
    return atomic_exchange(&g_pxc_test_fail_collect_alloc, false);
}

static bool pxc_test_fail_target_route_alloc(void) {
    return atomic_exchange(&g_pxc_test_fail_target_route_alloc, false);
}

static bool pxc_test_fail_destination_copy(void) {
    int position = atomic_load(&g_pxc_test_destination_copy_position);
    while (position > 0) {
        if (atomic_compare_exchange_weak(&g_pxc_test_destination_copy_position, &position,
                                         position - 1)) {
            return position == 1;
        }
    }
    return false;
}
#else
static bool pxc_test_fail_collect_alloc(void) {
    return false;
}

static bool pxc_test_fail_target_route_alloc(void) {
    return false;
}

void cbm_pxc_test_poison_non_rust_registry(CBMArena *arena) {
    (void)arena;
}

static bool pxc_test_fail_destination_copy(void) {
    return false;
}
#endif

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
    char *buf = (char *)malloc((size_t)size + CBM_TS_LOOKAHEAD_PAD);
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

/* Map a CBMDefinition.label to a CBMLSPDef.label. Per-language LSP registrars
 * only care about type-like containers (Class/Struct/Interface/Trait/Enum/Type)
 * plus Protocol/Function/Method — variables, modules, decorators, etc. are
 * skipped. Struct passes through so Rust/Go struct type-registration via the
 * cross-file LSP path is not dropped. */
static const char *pxc_map_label(const char *label) {
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
        return label;
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

static bool pxc_is_jvm_lang(CBMLanguage lang);

static const char *pxc_last_component(const char *qn) {
    if (!qn) {
        return NULL;
    }
    const char *dot = strrchr(qn, '.');
    return dot ? dot + 1 : qn;
}

static const char *pxc_jvm_type_qn(CBMArena *arena, const char *namespace_name,
                                   const char *type_qn_or_name) {
    if (!arena || !namespace_name || !namespace_name[0] || !type_qn_or_name) {
        return type_qn_or_name;
    }
    const char *short_name = pxc_last_component(type_qn_or_name);
    if (!short_name || !short_name[0]) {
        return type_qn_or_name;
    }
    return cbm_arena_sprintf(arena, "%s.%s", namespace_name, short_name);
}

static const char *pxc_jvm_def_qn(CBMArena *arena, const CBMDefinition *src,
                                  const char *namespace_name, const char *label) {
    if (!arena || !src || !namespace_name || !namespace_name[0]) {
        return src ? src->qualified_name : NULL;
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

typedef struct {
    const char *root_qn;
    const char *source_module_qn;
    const CBMCargoTarget *target;
    bool allocation_failed;
} PxcRustTargetRoute;

static PxcRustTargetRoute pxc_rust_target_route(CBMArena *arena, const char *project_name,
                                                const char *rel_path,
                                                const CBMCargoManifest *manifest) {
    PxcRustTargetRoute route = {0};
    if (!arena || !project_name || !rel_path || !manifest || !manifest->targets ||
        !manifest->targets_complete) {
        return route;
    }
    const CBMCargoTarget *selected = NULL;
    int exact_matches = 0;
    int contained_matches = 0;
    for (int i = 0; i < manifest->target_count; i++) {
        const CBMCargoTarget *target = &manifest->targets[i];
        const char *source = target->source_path;
        if (!source)
            continue;
        bool exact = strcmp(rel_path, source) == 0;
        bool production =
            target->kind == CBM_CARGO_TARGET_LIB || target->kind == CBM_CARGO_TARGET_BIN;
        const char *owner_root = production ? target->package_dir : target->blocker_root;
        size_t owner_len = owner_root ? strlen(owner_root) : 0;
        bool contained =
            owner_root && (owner_len == 0 || (strncmp(rel_path, owner_root, owner_len) == 0 &&
                                              rel_path[owner_len] == '/'));
        if (contained && production) {
            for (int j = 0; j < manifest->target_count; j++) {
                const char *nested = manifest->targets[j].package_dir;
                size_t nested_len = nested ? strlen(nested) : 0;
                if (nested_len > owner_len && strncmp(rel_path, nested, nested_len) == 0 &&
                    rel_path[nested_len] == '/') {
                    contained = false;
                    break;
                }
            }
        }
        if (exact) {
            selected = target;
            exact_matches++;
        } else if (contained) {
            if (exact_matches == 0)
                selected = target;
            contained_matches++;
        }
    }
    if ((exact_matches > 0 ? exact_matches : contained_matches) != 1 || !selected ||
        (selected->kind != CBM_CARGO_TARGET_LIB && selected->kind != CBM_CARGO_TARGET_BIN)) {
        return route;
    }
    const char *selected_source = selected->source_path;
    const char *slash = strrchr(selected_source, '/');
    char *root_rel =
        slash ? cbm_arena_strndup(arena, selected_source, (size_t)(slash - selected_source))
              : cbm_arena_strdup(arena, "");
    if (!root_rel) {
        route.allocation_failed = true;
        return route;
    }
    char *root =
        /* cppcheck-suppress knownConditionTrueFalse -- true in allocation-fault tests. */
        pxc_test_fail_target_route_alloc() ? NULL : cbm_pipeline_fqn_module(project_name, root_rel);
    char *source_module = cbm_pipeline_fqn_module(project_name, selected_source);
    if (!root || !source_module) {
        free(root);
        free(source_module);
        route.allocation_failed = true;
        return route;
    }
    const char *owned_root = cbm_arena_strdup(arena, root);
    const char *owned_source = cbm_arena_strdup(arena, source_module);
    free(root);
    free(source_module);
    if (!owned_root || !owned_source) {
        route.allocation_failed = true;
        return route;
    }
    route.root_qn = owned_root;
    route.source_module_qn = owned_source;
    route.target = selected;
    return route;
}

/* Convert one CBMDefinition into a CBMLSPDef. Returns 0 on success, -1
 * to skip (unsupported label or missing required field). dst gets borrowed
 * pointers into src and into `arena` for synthesised composites. */
static int pxc_build_lsp_def(CBMArena *arena, const CBMDefinition *src, const char *module_qn,
                             const char *namespace_name, CBMLanguage lang,
                             PxcRustTargetRoute rust_route, CBMLSPDef *dst) {
    const char *label = pxc_map_label(src->label);
    if (!label || !src->qualified_name || !src->name)
        return -1;
    memset(dst, 0, sizeof(*dst));
    if (pxc_is_jvm_lang(lang) && namespace_name && namespace_name[0]) {
        dst->qualified_name = pxc_jvm_def_qn(arena, src, namespace_name, label);
        dst->receiver_type = pxc_jvm_type_qn(arena, namespace_name, src->parent_class);
    } else {
        dst->qualified_name = src->qualified_name;
        dst->receiver_type = src->parent_class;
    }
    dst->short_name = src->name;
    dst->label = label;
    dst->def_module_qn = module_qn;
    dst->rust_crate_root_qn = rust_route.root_qn;
    dst->rust_crate_source_module_qn = rust_route.source_module_qn;
    dst->namespace_name = namespace_name;
    dst->is_interface = (strcmp(label, "Interface") == 0 || strcmp(label, "Protocol") == 0);
    /* Single return-type string. The per-language registrars split on '|'
     * for multi-return languages (Go); single-return languages just see one
     * piece, which is what's already stored. */
    dst->return_types = src->return_type;
    dst->embedded_types = pxc_join_pipe(arena, src->base_classes);
    dst->signature_param_types = src->signature_param_types;
    dst->signature_param_count = src->signature_param_count;
    dst->lang = lang;
    dst->decorators = src->decorators;
    if (lang == CBM_LANG_RUST) {
        /* Exact impl-block provenance is captured while the Rust impl node is
         * still on hand.  Do not reconstruct it later from leaf names. */
        dst->trait_qn = src->impl_trait;
        dst->is_abstract = src->is_abstract;
    }
    return 0;
}

/* Carry one Rust type-level impl independently of any method definition.
 * `impl Trait for Type {}` is semantically meaningful even when the block is
 * empty (the trait may provide defaults), so attaching the relation only to
 * concrete method records is lossy. */
static int pxc_build_rust_impl_relation(CBMArena *arena, const CBMImplTrait *impl,
                                        const char *module_qn, PxcRustTargetRoute rust_route,
                                        CBMLSPDef *dst) {
    if (!arena || !impl || !impl->trait_name || !impl->struct_name || !impl->struct_qn) {
        return -1;
    }
    const char *receiver_qn = impl->struct_qn;
    memset(dst, 0, sizeof(*dst));
    dst->qualified_name = receiver_qn;
    dst->short_name = pxc_qn_leaf(receiver_qn);
    dst->label = "RustImpl";
    dst->receiver_type = receiver_qn;
    dst->def_module_qn = module_qn;
    dst->rust_crate_root_qn = rust_route.root_qn;
    dst->rust_crate_source_module_qn = rust_route.source_module_qn;
    dst->trait_qn = impl->trait_name; /* raw; canonicalized by Rust registry */
    dst->lang = CBM_LANG_RUST;
    dst->is_rust_impl_relation = true;
    return 0;
}

/* Collect a project-wide CBMLSPDef[] from all cached results. Returns a
 * malloc'd array (caller frees) of length *out_count. String fields are
 * borrowed from cache[i]->arena and from def_modules[i] (also borrowed). */
CBMLSPDef *cbm_pxc_collect_all_defs(CBMFileResult **cache, const cbm_file_info_t *files,
                                    int file_count, const char *project_name, char **def_modules,
                                    int *out_count, CBMPxcCollectStatus *out_status,
                                    int *out_def_starts, const CBMCargoManifest *rust_manifest) {
    *out_status = CBM_PXC_COLLECT_EMPTY;
    int total = 0;
    for (int i = 0; i < file_count; i++) {
        if (cache[i]) {
            total += cache[i]->defs.count;
            if (files[i].language == CBM_LANG_RUST) {
                total += cache[i]->impl_traits.count;
            }
        }
    }
    if (total == 0) {
        *out_count = 0;
        if (out_def_starts) {
            memset(out_def_starts, 0, (size_t)(file_count + 1) * sizeof(int));
        }
        return NULL;
    }
    /* cppcheck-suppress knownConditionTrueFalse -- true in allocation-fault tests. */
    CBMLSPDef *defs = pxc_test_fail_collect_alloc()
                          ? NULL
                          : (CBMLSPDef *)calloc((size_t)total, sizeof(CBMLSPDef));
    if (!defs) {
        *out_count = 0;
        *out_status = CBM_PXC_COLLECT_ALLOCATION_FAILED;
        if (out_def_starts) {
            memset(out_def_starts, 0, (size_t)(file_count + 1) * sizeof(int));
        }
        return NULL;
    }
    int idx = 0;
    bool materialization_failed = false;
    for (int fi = 0; fi < file_count; fi++) {
        if (out_def_starts) {
            out_def_starts[fi] = idx;
        }
        if (!cache[fi])
            continue;
        if (!def_modules[fi]) {
            def_modules[fi] = cbm_pipeline_fqn_module_dir(project_name, files[fi].rel_path,
                                                          pxc_module_is_dir(files[fi].language));
            if (!def_modules[fi]) {
                materialization_failed = true;
                break;
            }
        }
        const char *namespace_name = cache[fi]->namespace_name;
        PxcRustTargetRoute rust_route = {0};
        if (files[fi].language == CBM_LANG_RUST) {
            rust_route = pxc_rust_target_route(&cache[fi]->arena, project_name, files[fi].rel_path,
                                               rust_manifest);
            if (rust_route.allocation_failed) {
                materialization_failed = true;
                break;
            }
        }
        if ((!namespace_name || !namespace_name[0]) && files[fi].rel_path) {
            namespace_name =
                pxc_infer_jvm_namespace(&cache[fi]->arena, files[fi].rel_path, files[fi].language);
            if (namespace_name && namespace_name[0]) {
                cache[fi]->namespace_name = namespace_name;
            }
        }
        for (int di = 0; di < cache[fi]->defs.count; di++) {
            if (pxc_build_lsp_def(&cache[fi]->arena, &cache[fi]->defs.items[di], def_modules[fi],
                                  namespace_name, files[fi].language, rust_route,
                                  &defs[idx]) == 0) {
                idx++;
            }
        }
        if (files[fi].language == CBM_LANG_RUST) {
            for (int ii = 0; ii < cache[fi]->impl_traits.count; ii++) {
                if (pxc_build_rust_impl_relation(&cache[fi]->arena,
                                                 &cache[fi]->impl_traits.items[ii], def_modules[fi],
                                                 rust_route, &defs[idx]) == 0) {
                    idx++;
                }
            }
        }
        if (cbm_arena_status(&cache[fi]->arena) != CBM_ARENA_STATUS_AVAILABLE) {
            materialization_failed = true;
            break;
        }
    }
    if (materialization_failed) {
        free(defs);
        *out_count = 0;
        *out_status = CBM_PXC_COLLECT_ALLOCATION_FAILED;
        if (out_def_starts) {
            memset(out_def_starts, 0, (size_t)(file_count + 1) * sizeof(int));
        }
        return NULL;
    }
    if (out_def_starts) {
        out_def_starts[file_count] = idx;
    }
    *out_count = idx;
    *out_status = CBM_PXC_COLLECT_AVAILABLE;
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
        char *module = (char *)malloc(module_len + 1);
        if (!module)
            return NULL;
        memcpy(module, module_path, module_len);
        module[module_len] = '\0';
        const cbm_gbuf_node_t *module_node =
            cbm_pipeline_lsp_target_node(gbuf, project_name, module, false);
        free(module);
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

static int pxc_rust_file_index(const cbm_file_info_t *files, int file_count, const char *rel) {
    for (int i = 0; files && rel && i < file_count; i++)
        if (files[i].rel_path && strcmp(files[i].rel_path, rel) == 0)
            return i;
    return -1;
}

static bool pxc_rust_module_dir(const char *rel, char *out, size_t cap) {
    const char *slash = strrchr(rel, '/');
    const char *base = slash ? slash + 1 : rel;
    size_t dir_len = slash ? (size_t)(slash - rel) : 0;
    if (strcmp(base, "lib.rs") == 0 || strcmp(base, "main.rs") == 0 ||
        strcmp(base, "mod.rs") == 0) {
        if (dir_len >= cap)
            return false;
        memcpy(out, rel, dir_len);
        out[dir_len] = '\0';
        return true;
    }
    size_t stem = strlen(base);
    if (stem < 3 || strcmp(base + stem - 3, ".rs") != 0)
        return false;
    stem -= 3;
    if (dir_len + (dir_len ? 1 : 0) + stem >= cap)
        return false;
    if (dir_len)
        snprintf(out, cap, "%.*s/%.*s", (int)dir_len, rel, (int)stem, base);
    else
        snprintf(out, cap, "%.*s", (int)stem, base);
    return true;
}

static int pxc_rust_target_source_count(const CBMCargoManifest *manifest, const char *rel_path) {
    int matches = 0;
    for (int i = 0; manifest && rel_path && i < manifest->target_count; i++) {
        if (manifest->targets[i].source_path &&
            strcmp(manifest->targets[i].source_path, rel_path) == 0)
            matches++;
    }
    return matches;
}

static int pxc_rust_child_file(const cbm_file_info_t *files, CBMFileResult *const *cache,
                               int file_count, int parent, const char *parent_path,
                               const char *name, const CBMCargoManifest *manifest,
                               bool require_public, char *out_inline_path, size_t out_inline_cap) {
    if (parent < 0 || !cache[parent] ||
        cache[parent]->rust_mod_decls_status != CBM_RUST_CARRIER_COMPLETE)
        return -1;
    const CBMModDecl *decl = NULL;
    int matches = 0;
    for (int i = 0; i < cache[parent]->mod_decls.count; i++) {
        const CBMModDecl *candidate = &cache[parent]->mod_decls.items[i];
        if (candidate->child_name && candidate->parent_path &&
            strcmp(candidate->parent_path, parent_path ?: "") == 0 &&
            strcmp(candidate->child_name, name) == 0) {
            decl = candidate;
            matches++;
        }
    }
    if (matches != 1)
        return -1;
    if (require_public && decl->rust_visibility != CBM_RUST_IMPORT_VIS_PUBLIC)
        return -1;
    if (decl->is_inline) {
        int n = parent_path && parent_path[0]
                    ? snprintf(out_inline_path, out_inline_cap, "%s::%s", parent_path, name)
                    : snprintf(out_inline_path, out_inline_cap, "%s", name);
        return n > 0 && (size_t)n < out_inline_cap ? parent : -1;
    }
    if (out_inline_path && out_inline_cap)
        out_inline_path[0] = '\0';
    if (decl->path_override && decl->path_override[0]) {
        char candidate[CBM_PATH_MAX];
        const char *slash = strrchr(files[parent].rel_path, '/');
        char parent_dir[CBM_PATH_MAX] = {0};
        if (parent_path && parent_path[0]) {
            snprintf(parent_dir, sizeof(parent_dir), "%s", parent_path);
            for (char *p = parent_dir; *p; p++)
                if (*p == ':' && p[1] == ':') {
                    *p = '/';
                    memmove(p + 1, p + 2, strlen(p + 2) + 1);
                }
        }
        int n = slash ? snprintf(candidate, sizeof(candidate), "%.*s/%s%s%s",
                                 (int)(slash - files[parent].rel_path), files[parent].rel_path,
                                 parent_dir, parent_dir[0] ? "/" : "", decl->path_override)
                      : snprintf(candidate, sizeof(candidate), "%s%s%s", parent_dir,
                                 parent_dir[0] ? "/" : "", decl->path_override);
        return n > 0 && (size_t)n < sizeof(candidate)
                   ? pxc_rust_file_index(files, file_count, candidate)
                   : -1;
    }
    char dir[CBM_PATH_MAX];
    int target_sources = pxc_rust_target_source_count(manifest, files[parent].rel_path);
    if (target_sources > 1)
        return -1;
    if (target_sources == 1) {
        const char *slash = strrchr(files[parent].rel_path, '/');
        size_t dir_len = slash ? (size_t)(slash - files[parent].rel_path) : 0;
        if (dir_len >= sizeof(dir))
            return -1;
        memcpy(dir, files[parent].rel_path, dir_len);
        dir[dir_len] = '\0';
    } else if (!pxc_rust_module_dir(files[parent].rel_path, dir, sizeof(dir))) {
        return -1;
    }
    char module_dir[CBM_PATH_MAX];
    snprintf(module_dir, sizeof(module_dir), "%s", parent_path ?: "");
    for (char *p = module_dir; *p; p++)
        if (*p == ':' && p[1] == ':') {
            *p = '/';
            memmove(p + 1, p + 2, strlen(p + 2) + 1);
        }
    char flat[CBM_PATH_MAX], nested[CBM_PATH_MAX];
    if (dir[0]) {
        snprintf(flat, sizeof(flat), "%s/%s%s%s.rs", dir, module_dir, module_dir[0] ? "/" : "",
                 name);
        snprintf(nested, sizeof(nested), "%s/%s%s%s/mod.rs", dir, module_dir,
                 module_dir[0] ? "/" : "", name);
    } else {
        snprintf(flat, sizeof(flat), "%s%s%s.rs", module_dir, module_dir[0] ? "/" : "", name);
        snprintf(nested, sizeof(nested), "%s%s%s/mod.rs", module_dir, module_dir[0] ? "/" : "",
                 name);
    }
    int a = pxc_rust_file_index(files, file_count, flat);
    int b = pxc_rust_file_index(files, file_count, nested);
    return (a >= 0) == (b >= 0) ? -1 : (a >= 0 ? a : b);
}

/* A `super` path is authoritative only when exactly one complete module
 * declaration owns the current file. Inferring parents from directories alone
 * would accept files Cargo never attached to the module tree. */
static int pxc_rust_parent_file(const cbm_file_info_t *files, CBMFileResult *const *cache,
                                int file_count, int child, const CBMCargoManifest *manifest,
                                char *out_parent_path, size_t out_parent_cap) {
    if (!files || !cache || child < 0 || child >= file_count || !files[child].rel_path)
        return -1;
    int parent = -1;
    int matches = 0;
    for (int i = 0; i < file_count; i++) {
        if (!cache[i] || cache[i]->rust_mod_decls_status != CBM_RUST_CARRIER_COMPLETE)
            continue;
        for (int d = 0; d < cache[i]->mod_decls.count; d++) {
            const CBMModDecl *decl = &cache[i]->mod_decls.items[d];
            const char *name = decl->child_name;
            char inline_path[CBM_PATH_MAX];
            if (!decl->is_inline && name && decl->parent_path &&
                pxc_rust_child_file(files, cache, file_count, i, decl->parent_path, name, manifest,
                                    false, inline_path, sizeof(inline_path)) == child) {
                parent = i;
                if (out_parent_path && out_parent_cap)
                    snprintf(out_parent_path, out_parent_cap, "%s", decl->parent_path);
                matches++;
            }
        }
    }
    return matches == 1 ? parent : (matches == 0 ? -1 : -2);
}

static int pxc_rust_crate_root(const cbm_file_info_t *files, CBMFileResult *const *cache,
                               int file_count, int caller, const CBMCargoManifest *manifest) {
    int current = caller;
    for (int depth = 0; current >= 0 && depth <= file_count; depth++) {
        int parent = pxc_rust_parent_file(files, cache, file_count, current, manifest, NULL, 0);
        if (parent == -1)
            return current;
        if (parent < 0 || parent == current)
            return -1;
        current = parent;
    }
    return -1;
}

static const char *pxc_rust_caller_package_dir(const CBMCargoManifest *manifest,
                                               const char *caller_rel) {
    const char *best = NULL;
    size_t best_len = 0;
    for (int i = 0; manifest && caller_rel && i < manifest->target_count; i++) {
        const char *dir = manifest->targets[i].package_dir;
        if (!dir)
            continue;
        size_t len = strlen(dir);
        bool owns = len == 0 || (strncmp(caller_rel, dir, len) == 0 && caller_rel[len] == '/');
        if (!owns || (best && len < best_len))
            continue;
        if (best && len == best_len && strcmp(best, dir) != 0)
            return NULL;
        best = dir;
        best_len = len;
    }
    return best;
}

static bool pxc_rust_package_lib_visible(int caller_root, int lib_root) {
    return caller_root >= 0 && lib_root >= 0 && caller_root != lib_root;
}

static bool pxc_rust_package_lib_visible_for_caller(const cbm_file_info_t *files,
                                                    CBMFileResult *const *cache, int file_count,
                                                    int caller, const CBMCargoManifest *manifest,
                                                    int lib_root) {
    int caller_root = pxc_rust_crate_root(files, cache, file_count, caller, manifest);
    return pxc_rust_package_lib_visible(caller_root, lib_root);
}

#if defined(CBM_INCREMENTAL_TEST_API) && CBM_INCREMENTAL_TEST_API
bool cbm_pxc_test_package_lib_visible(int caller_root, int lib_root) {
    return pxc_rust_package_lib_visible(caller_root, lib_root);
}

bool cbm_pxc_test_package_lib_visible_for_caller(const cbm_file_info_t *files,
                                                 CBMFileResult *const *cache, int file_count,
                                                 int caller, const CBMCargoManifest *manifest,
                                                 int lib_root) {
    return pxc_rust_package_lib_visible_for_caller(files, cache, file_count, caller, manifest,
                                                   lib_root);
}
#endif

static int pxc_rust_external_root(const CBMCargoManifest *manifest, const cbm_file_info_t *files,
                                  CBMFileResult *const *cache, int file_count, int caller,
                                  const char *caller_package_dir, const char *crate_name) {
    int self_found = -1;
    int self_matches = 0;
    for (int i = 0; manifest && caller_package_dir && crate_name && i < manifest->target_count;
         i++) {
        const CBMCargoTarget *target = &manifest->targets[i];
        if (target->kind != CBM_CARGO_TARGET_LIB || !target->name || !target->package_dir ||
            !target->source_path || strcmp(target->package_dir, caller_package_dir) != 0 ||
            !cbm_cargo_crate_name_eq(target->name, crate_name))
            continue;
        self_found = pxc_rust_file_index(files, file_count, target->source_path);
        if (pxc_rust_package_lib_visible_for_caller(files, cache, file_count, caller, manifest,
                                                    self_found))
            self_matches++;
    }
    if (self_matches == 1)
        return self_found;
    if (self_matches > 1)
        return -1;
    int declared = 0;
    const char *target_package_dir = NULL;
    for (int i = 0;
         manifest && caller_package_dir && crate_name && i < manifest->dependency_route_count;
         i++) {
        const CBMCargoDependencyRoute *route = &manifest->dependency_routes[i];
        if (route->package_dir && route->name &&
            strcmp(route->package_dir, caller_package_dir) == 0 &&
            cbm_cargo_crate_name_eq(route->name, crate_name)) {
            declared++;
            target_package_dir = route->target_package_dir;
        }
    }
    if (declared != 1 || !target_package_dir)
        return -1;
    int found = -1;
    int matches = 0;
    for (int i = 0; manifest && i < manifest->target_count; i++) {
        const CBMCargoTarget *target = &manifest->targets[i];
        if (target->kind != CBM_CARGO_TARGET_LIB || !target->source_path || !target->package_dir ||
            strcmp(target->package_dir, target_package_dir) != 0)
            continue;
        found = pxc_rust_file_index(files, file_count, target->source_path);
        if (found >= 0)
            matches++;
    }
    return matches == 1 ? found : -1;
}

static const char *pxc_rust_authority_resolve(
    CBMArena *arena, const char *project_name, const char *caller_rel,
    const char *caller_module_path, const char *path, const cbm_file_info_t *files,
    CBMFileResult *const *cache, int file_count, const CBMCargoManifest *manifest, int depth,
    bool inherited_public, bool inherited_public_ancestors) {
    if (!arena || !path || !path[0] || depth > 32)
        return NULL;
    char *copy = cbm_arena_strdup(arena, path);
    if (!copy)
        return NULL;
    const char *segments[128];
    int segment_count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(copy, ":", &save); tok && segment_count < 128;
         tok = strtok_r(NULL, ":", &save)) {
        if (tok[0])
            segments[segment_count++] = tok;
    }
    if (segment_count == 0 || segment_count >= 128)
        return NULL;
    int caller = pxc_rust_file_index(files, file_count, caller_rel);
    int current = caller;
    char inline_path[CBM_PATH_MAX];
    snprintf(inline_path, sizeof(inline_path), "%s", caller_module_path ?: "");
    int pos = 0;
    bool require_public = inherited_public;
    bool require_public_ancestors = inherited_public_ancestors;
    if (strcmp(segments[0], "crate") == 0) {
        current = pxc_rust_crate_root(files, cache, file_count, caller, NULL);
        if (current < 0 && manifest) {
            PxcRustTargetRoute route =
                pxc_rust_target_route(arena, project_name, caller_rel, manifest);
            current = route.target
                          ? pxc_rust_file_index(files, file_count, route.target->source_path)
                          : -1;
        }
        inline_path[0] = '\0';
        pos = 1;
    } else if (strcmp(segments[0], "self") == 0) {
        pos = 1;
    } else if (strcmp(segments[0], "super") == 0) {
        while (pos < segment_count && strcmp(segments[pos], "super") == 0) {
            char *separator = strrchr(inline_path, ':');
            if (separator) {
                if (separator > inline_path && separator[-1] == ':')
                    separator--;
                *separator = '\0';
            } else if (inline_path[0]) {
                inline_path[0] = '\0';
            } else {
                current = pxc_rust_parent_file(files, cache, file_count, current, manifest,
                                               inline_path, sizeof(inline_path));
            }
            if (current < 0)
                return NULL;
            pos++;
        }
    } else {
        const char *caller_package_dir = pxc_rust_caller_package_dir(manifest, caller_rel);
        int external = caller_package_dir
                           ? pxc_rust_external_root(manifest, files, cache, file_count, caller,
                                                    caller_package_dir, segments[0])
                           : -1;
        if (external >= 0) {
            current = external;
            inline_path[0] = '\0';
            pos = 1;
            require_public = true;
            require_public_ancestors = true;
        }
    }
    if (current < 0 || pos >= segment_count)
        return NULL;
    while (pos + 1 < segment_count) {
        int owner = current;
        const char *segment = segments[pos++];
        char next_inline[CBM_PATH_MAX];
        current =
            pxc_rust_child_file(files, cache, file_count, owner, inline_path, segment, manifest,
                                require_public_ancestors, next_inline, sizeof(next_inline));
        if (current < 0) {
            const CBMImport *module_reexport = NULL;
            int reexports = 0;
            CBMFileResult *owner_result = owner >= 0 ? cache[owner] : NULL;
            for (int i = 0;
                 owner_result && owner_result->rust_imports_status == CBM_RUST_CARRIER_COMPLETE &&
                 i < owner_result->imports.count;
                 i++) {
                const CBMImport *imp = &owner_result->imports.items[i];
                if (!imp->rust_module_scope ||
                    imp->rust_provenance != CBM_RUST_IMPORT_PROVENANCE_NAMED_EXACT ||
                    !imp->local_name || !imp->module_path || !imp->owner_module_path ||
                    strcmp(imp->owner_module_path, inline_path) != 0 ||
                    strcmp(imp->local_name, segment) != 0 ||
                    (require_public && imp->rust_visibility != CBM_RUST_IMPORT_VIS_PUBLIC))
                    continue;
                module_reexport = imp;
                reexports++;
            }
            if (reexports == 1) {
                char expanded[CBM_PATH_MAX];
                int used = snprintf(expanded, sizeof(expanded), "%s", module_reexport->module_path);
                for (int tail = pos;
                     used >= 0 && (size_t)used < sizeof(expanded) && tail < segment_count; tail++)
                    used += snprintf(expanded + used, sizeof(expanded) - (size_t)used, "::%s",
                                     segments[tail]);
                return used > 0 && (size_t)used < sizeof(expanded)
                           ? pxc_rust_authority_resolve(arena, project_name, files[owner].rel_path,
                                                        module_reexport->owner_module_path,
                                                        expanded, files, cache, file_count,
                                                        manifest, depth + 1, require_public, false)
                           : NULL;
            }
            return NULL;
        }
        snprintf(inline_path, sizeof(inline_path), "%s", next_inline);
    }
    CBMFileResult *target = cache[current];
    const char *leaf = segments[pos];
    if (!target || !target->module_qn)
        return NULL;
    char leaf_inline[CBM_PATH_MAX];
    int leaf_module =
        pxc_rust_child_file(files, cache, file_count, current, inline_path, leaf, manifest,
                            require_public_ancestors, leaf_inline, sizeof(leaf_inline));
    if (leaf_module >= 0 && leaf_module != current && cache[leaf_module] &&
        cache[leaf_module]->module_qn)
        return cache[leaf_module]->module_qn;
    char inline_dotted[CBM_PATH_MAX];
    snprintf(inline_dotted, sizeof(inline_dotted), "%s", inline_path);
    for (char *p = inline_dotted; *p; p++)
        if (*p == ':' && p[1] == ':') {
            *p = '.';
            memmove(p + 1, p + 2, strlen(p + 2) + 1);
        }
    const char *exact =
        inline_dotted[0]
            ? cbm_arena_sprintf(arena, "%s.%s.%s", target->module_qn, inline_dotted, leaf)
            : cbm_arena_sprintf(arena, "%s.%s", target->module_qn, leaf);
    const char *found = NULL;
    bool found_exported = false;
    int definitions = 0;
    for (int i = 0; exact && i < target->defs.count; i++) {
        const char *qn = target->defs.items[i].qualified_name;
        if (qn && strcmp(qn, exact) == 0) {
            found = qn;
            found_exported = target->defs.items[i].is_exported;
            definitions++;
        }
    }
    if (definitions == 1)
        return !require_public || found_exported ? found : NULL;
    if (definitions > 1 || target->rust_imports_status != CBM_RUST_CARRIER_COMPLETE)
        return NULL;
    const CBMImport *reexport = NULL;
    int exports = 0;
    for (int i = 0; i < target->imports.count; i++) {
        const CBMImport *imp = &target->imports.items[i];
        if (!imp->rust_module_scope ||
            imp->rust_provenance != CBM_RUST_IMPORT_PROVENANCE_NAMED_EXACT || !imp->local_name ||
            !imp->owner_module_path || strcmp(imp->owner_module_path, inline_path) != 0 ||
            strcmp(imp->local_name, leaf) != 0 ||
            (require_public && imp->rust_visibility != CBM_RUST_IMPORT_VIS_PUBLIC))
            continue;
        reexport = imp;
        exports++;
    }
    return exports == 1
               ? pxc_rust_authority_resolve(arena, project_name, files[current].rel_path,
                                            reexport->owner_module_path, reexport->module_path,
                                            files, cache, file_count, manifest, depth + 1,
                                            require_public, false)
               : NULL;
}

/* Rust never consumes graph IMPORTS as semantic proof. Each exact extracted
 * local produces either a resolved QN or an authoritative empty blocker. */
CBMPxcImportMapStatus cbm_pxc_build_import_map_with_rust_authority(
    const cbm_gbuf_t *gbuf, const char *project_name, const char *rel_path, CBMLanguage lang,
    const CBMFileResult *result, const cbm_file_info_t *files, CBMFileResult *const *cache,
    int file_count, const CBMCargoManifest *rust_manifest, const char ***out_keys,
    const char ***out_vals, CBMRustImportScope **out_scopes, int *out_count) {
    *out_keys = NULL;
    *out_vals = NULL;
    if (out_scopes)
        *out_scopes = NULL;
    *out_count = 0;

    if (lang == CBM_LANG_RUST) {
        if (!result || result->rust_imports_status != CBM_RUST_CARRIER_COMPLETE)
            return CBM_PXC_IMPORT_MAP_AUTHORITY_UNAVAILABLE;
        int capacity = result->imports.count;
        if (capacity == 0)
            return CBM_PXC_IMPORT_MAP_COMPLETE;
        const char **keys = calloc((size_t)capacity, sizeof(*keys));
        const char **vals = calloc((size_t)capacity, sizeof(*vals));
        CBMRustImportScope *scopes = calloc((size_t)capacity, sizeof(*scopes));
        if (!keys || !vals || !scopes) {
            free(keys);
            free(vals);
            free(scopes);
            return CBM_PXC_IMPORT_MAP_ALLOCATION_FAILED;
        }
        CBMArena scratch;
        cbm_arena_init(&scratch);
        int count = 0;
        for (int i = 0; i < result->imports.count; i++) {
            const CBMImport *imp = &result->imports.items[i];
            if (imp->rust_provenance != CBM_RUST_IMPORT_PROVENANCE_NAMED_EXACT ||
                !imp->local_name || !imp->module_path || strcmp(imp->local_name, "_") == 0)
                continue;
            bool already_emitted = false;
            int declarations = 0;
            for (int j = 0; j < result->imports.count; j++) {
                const CBMImport *candidate = &result->imports.items[j];
                if (candidate->rust_provenance == CBM_RUST_IMPORT_PROVENANCE_NAMED_EXACT &&
                    candidate->local_name && strcmp(candidate->local_name, imp->local_name) == 0 &&
                    candidate->scope_start_byte == imp->scope_start_byte &&
                    candidate->scope_end_byte == imp->scope_end_byte) {
                    declarations++;
                    if (j < i)
                        already_emitted = true;
                }
            }
            if (already_emitted)
                continue;
            const char *resolved =
                declarations == 1
                    ? pxc_rust_authority_resolve(&scratch, project_name, rel_path,
                                                 imp->owner_module_path, imp->module_path, files,
                                                 cache, file_count, rust_manifest, 0, false, false)
                    : NULL;
            keys[count] = strdup(imp->local_name);
            vals[count] = strdup(resolved ? resolved : "");
            if (!keys[count] || !vals[count]) {
                cbm_arena_destroy(&scratch);
                cbm_pxc_free_import_map(keys, vals, count + 1);
                free(scopes);
                return CBM_PXC_IMPORT_MAP_ALLOCATION_FAILED;
            }
            scopes[count] = (CBMRustImportScope){.start_byte = imp->scope_start_byte,
                                                 .end_byte = imp->scope_end_byte,
                                                 .owner_module_path = imp->owner_module_path};
            count++;
        }
        bool scratch_ok = cbm_arena_status(&scratch) == CBM_ARENA_STATUS_AVAILABLE;
        cbm_arena_destroy(&scratch);
        if (!scratch_ok) {
            cbm_pxc_free_import_map(keys, vals, count);
            free(scopes);
            return CBM_PXC_IMPORT_MAP_ALLOCATION_FAILED;
        }
        *out_keys = keys;
        *out_vals = vals;
        if (out_scopes)
            *out_scopes = scopes;
        else
            free(scopes);
        *out_count = count;
        return CBM_PXC_IMPORT_MAP_COMPLETE;
    }

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
        return CBM_PXC_IMPORT_MAP_COMPLETE;

    size_t capacity = (size_t)edge_count + (size_t)metadata_count;
    const char **keys = (const char **)calloc(capacity, sizeof(const char *));
    const char **vals = (const char **)calloc(capacity, sizeof(const char *));
    if (!keys || !vals) {
        free(keys);
        free(vals);
        return CBM_PXC_IMPORT_MAP_ALLOCATION_FAILED;
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
    return CBM_PXC_IMPORT_MAP_COMPLETE;
}

void cbm_pxc_record_rust_authority_health(CBMFileResult *result, const CBMCargoManifest *manifest,
                                          CBMPxcImportMapStatus import_status) {
    if (!result)
        return;
    if (manifest && !manifest->targets_complete)
        cbm_rust_health_record(&result->rust_health,
                               CBM_RUST_HEALTH_MANIFEST_TARGET_AUTHORITY_UNAVAILABLE, 0, 0);
    if (import_status == CBM_PXC_IMPORT_MAP_ALLOCATION_FAILED)
        cbm_rust_health_record(&result->rust_health, CBM_RUST_HEALTH_ALLOCATION_UNAVAILABLE, 0, 0);
    else if (import_status == CBM_PXC_IMPORT_MAP_AUTHORITY_UNAVAILABLE)
        cbm_rust_health_record(&result->rust_health, CBM_RUST_HEALTH_IMPORT_CARRIER_PARTIAL, 0, 0);
}

int cbm_pxc_build_import_map(const cbm_gbuf_t *gbuf, const char *project_name, const char *rel_path,
                             CBMLanguage lang, const CBMFileResult *result, const char ***out_keys,
                             const char ***out_vals, int *out_count) {
    CBMRustImportScope *scopes = NULL;
    int status = (int)cbm_pxc_build_import_map_with_rust_authority(
        gbuf, project_name, rel_path, lang, result, NULL, NULL, 0, NULL, out_keys, out_vals,
        &scopes, out_count);
    free(scopes);
    return status;
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

bool cbm_pxc_collection_requires_abort(CBMFileResult *const *cache, const cbm_file_info_t *files,
                                       int file_count, CBMPxcCollectStatus status) {
    if (status != CBM_PXC_COLLECT_ALLOCATION_FAILED) {
        return false;
    }
    for (int i = 0; i < file_count; i++) {
        if (cache[i] && files[i].language != CBM_LANG_RUST &&
            cbm_pxc_has_cross_lsp(files[i].language)) {
            return true;
        }
    }
    return false;
}

/* Append cross-file results from `src_out` (allocated in a scratch arena
 * about to be destroyed) into `dst_calls` (lives in cache_entry->arena),
 * copying every string field into dst_arena. Skips entries whose
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
typedef struct {
    bool complete;
    uint32_t resolved_emitted;
    uint32_t unresolved_emitted;
} PxcAppendStatus;

static bool pxc_copy_destination_string(CBMArena *arena, const char *source, const char **out) {
    *out = NULL;
    if (!source) {
        return true;
    }
    /* cppcheck-suppress knownConditionTrueFalse -- true in destination-copy fault tests. */
    if (pxc_test_fail_destination_copy()) {
        return false;
    }
    *out = cbm_arena_strdup(arena, source);
    return *out != NULL;
}

static bool pxc_resolved_reserve_one(CBMArena *arena, CBMResolvedCallArray *calls) {
    if (calls->count < calls->cap) {
        return true;
    }
    int next = calls->cap == 0 ? CBM_SZ_32 : calls->cap * 2;
    CBMResolvedCall *items =
        (CBMResolvedCall *)cbm_arena_alloc(arena, (size_t)next * sizeof(*items));
    if (!items) {
        return false;
    }
    if (calls->items && calls->count > 0) {
        memcpy(items, calls->items, (size_t)calls->count * sizeof(*items));
    }
    calls->items = items;
    calls->cap = next;
    return true;
}

static bool pxc_calls_reserve_one(CBMArena *arena, CBMCallArray *calls) {
    if (calls->count < calls->cap) {
        return true;
    }
    int next = calls->cap == 0 ? CBM_SZ_32 : calls->cap * 2;
    CBMCall *items = (CBMCall *)cbm_arena_alloc(arena, (size_t)next * sizeof(*items));
    if (!items) {
        return false;
    }
    if (calls->items && calls->count > 0) {
        memcpy(items, calls->items, (size_t)calls->count * sizeof(*items));
    }
    calls->items = items;
    calls->cap = next;
    return true;
}

static void pxc_append_count(PxcAppendStatus *status, const CBMResolvedCall *call) {
    if (call->reason && call->confidence <= 0.0f) {
        status->unresolved_emitted++;
    } else {
        status->resolved_emitted++;
    }
}

static PxcAppendStatus pxc_append_results(CBMArena *dst_arena, CBMResolvedCallArray *dst_calls,
                                          const CBMResolvedCallArray *src_out) {
    PxcAppendStatus status = {.complete = false};
    if (!dst_calls || !src_out)
        return status;

    CBMArena keys;
    cbm_arena_init(&keys);
    CBMHashTable *seen = cbm_ht_create((uint32_t)(dst_calls->count + src_out->count + 1));
    if (!seen) {
        cbm_arena_destroy(&keys);
        return status;
    }

    for (int i = 0; i < dst_calls->count; i++) {
        const CBMResolvedCall *rc = &dst_calls->items[i];
        if (rc->caller_qn && rc->callee_qn) {
            char *k = cbm_arena_sprintf(&keys, "%u\x1f%s\x1f%s\x1f%u:%u\x1f%u", (unsigned)rc->kind,
                                        rc->caller_qn, rc->callee_qn, rc->site_start_byte,
                                        rc->site_end_byte, (unsigned)rc->source_origin);
            if (!k) {
                goto done;
            }
            void *prior = cbm_ht_get(seen, k);
            if (!prior || rc->confidence > dst_calls->items[(int)(uintptr_t)prior - 1].confidence) {
                const char *stored = cbm_ht_get_key(seen, k);
                void *expected = (void *)(uintptr_t)(i + 1);
                cbm_ht_set(seen, stored ? stored : k, expected);
                if (cbm_ht_get(seen, stored ? stored : k) != expected) {
                    goto done;
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
        if (!k) {
            goto done;
        }
        void *prior = k ? cbm_ht_get(seen, k) : NULL;
        if (prior) {
            CBMResolvedCall *dst = &dst_calls->items[(int)(uintptr_t)prior - 1];
            if (src->confidence > dst->confidence) {
                CBMResolvedCall materialized = *src;
                if (!pxc_copy_destination_string(dst_arena, src->caller_qn,
                                                 &materialized.caller_qn) ||
                    !pxc_copy_destination_string(dst_arena, src->callee_qn,
                                                 &materialized.callee_qn) ||
                    !pxc_copy_destination_string(dst_arena, src->strategy,
                                                 &materialized.strategy) ||
                    !pxc_copy_destination_string(dst_arena, src->reason, &materialized.reason)) {
                    goto done;
                }
                *dst = materialized;
            }
            continue;
        }
        if (!pxc_resolved_reserve_one(dst_arena, dst_calls)) {
            goto done;
        }
        CBMResolvedCall dst = *src;
        if (!pxc_copy_destination_string(dst_arena, src->caller_qn, &dst.caller_qn) ||
            !pxc_copy_destination_string(dst_arena, src->callee_qn, &dst.callee_qn) ||
            !pxc_copy_destination_string(dst_arena, src->strategy, &dst.strategy) ||
            !pxc_copy_destination_string(dst_arena, src->reason, &dst.reason)) {
            goto done;
        }
        dst_calls->items[dst_calls->count++] = dst;
        pxc_append_count(&status, src);
        if (k) {
            void *expected = (void *)(uintptr_t)dst_calls->count;
            cbm_ht_set(seen, k, expected);
            if (cbm_ht_get(seen, k) != expected) {
                goto done;
            }
        }
    }
    status.complete = true;
done:
    cbm_ht_free(seen);
    cbm_arena_destroy(&keys);
    return status;
}

/* Merge exact synthetic call carriers produced by a cross-LSP resolver. The
 * source may live in the per-file result arena or in cbm_pxc_run_one's scratch
 * arena, so every pointer-bearing field is copied explicitly. Invalid/legacy
 * zero-span carriers are rejected: a requires-LSP carrier is safe only when it
 * can join the semantic record for the same source occurrence. */
static bool pxc_append_synthetic_calls(CBMArena *dst_arena, CBMCallArray *dst_calls,
                                       const CBMCallArray *src_calls) {
    if (!dst_arena || !dst_calls || !src_calls || src_calls->count <= 0)
        return dst_arena && dst_calls && src_calls;

    CBMArena keys;
    cbm_arena_init(&keys);
    CBMHashTable *seen = cbm_ht_create((uint32_t)(dst_calls->count + src_calls->count + 1));
    if (!seen) {
        cbm_arena_destroy(&keys);
        return false;
    }
    bool complete = false;

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
        if (!key) {
            goto done;
        }
        if (!cbm_ht_get(seen, key)) {
            void *expected = (void *)(uintptr_t)(i + 1);
            cbm_ht_set(seen, key, expected);
            if (cbm_ht_get(seen, key) != expected) {
                goto done;
            }
        }
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
        if (!key) {
            goto done;
        }
        if (key && cbm_ht_get(seen, key))
            continue;

        if (!pxc_calls_reserve_one(dst_arena, dst_calls)) {
            goto done;
        }
        CBMCall dst = *src;
        if (!pxc_copy_destination_string(dst_arena, src->callee_name, &dst.callee_name) ||
            !pxc_copy_destination_string(dst_arena, src->enclosing_func_qn,
                                         &dst.enclosing_func_qn) ||
            !pxc_copy_destination_string(dst_arena, src->first_string_arg, &dst.first_string_arg) ||
            !pxc_copy_destination_string(dst_arena, src->second_arg_name, &dst.second_arg_name)) {
            goto done;
        }
        for (int ai = 0; ai < CBM_MAX_CALL_ARGS; ai++) {
            if (!pxc_copy_destination_string(dst_arena, src->args[ai].expr, &dst.args[ai].expr) ||
                !pxc_copy_destination_string(dst_arena, src->args[ai].value, &dst.args[ai].value) ||
                !pxc_copy_destination_string(dst_arena, src->args[ai].keyword,
                                             &dst.args[ai].keyword)) {
                goto done;
            }
        }
        dst_calls->items[dst_calls->count++] = dst;
        if (key) {
            void *expected = (void *)(uintptr_t)dst_calls->count;
            cbm_ht_set(seen, key, expected);
            if (cbm_ht_get(seen, key) != expected) {
                goto done;
            }
        }
    }
    complete = true;
done:
    cbm_ht_free(seen);
    cbm_arena_destroy(&keys);
    return complete;
}

static uint32_t pxc_saturating_add(uint32_t left, uint32_t right) {
    return UINT32_MAX - left < right ? UINT32_MAX : left + right;
}

static CBMPxcDispatchStatus pxc_destination_status(CBMLanguage lang, CBMFileResult *result,
                                                   CBMPxcDispatchStatus status) {
    if (cbm_file_result_status(result) == CBM_FILE_STATUS_COMPLETE &&
        status == CBM_PXC_DISPATCH_COMPLETE) {
        return status;
    }
    if (lang == CBM_LANG_RUST) {
        if (result->rust_health.issues[CBM_RUST_HEALTH_ALLOCATION_UNAVAILABLE].count == 0) {
            cbm_rust_health_record(&result->rust_health, CBM_RUST_HEALTH_ALLOCATION_UNAVAILABLE, 0,
                                   0);
        }
        result->rust_health.completed_routes &= ~CBM_RUST_HEALTH_ROUTE_CROSS_FILE;
    }
    return CBM_PXC_DISPATCH_ALLOCATION_FAILED;
}

#if defined(CBM_INCREMENTAL_TEST_API) && CBM_INCREMENTAL_TEST_API
bool cbm_pxc_test_non_rust_destination_failure_is_typed(void) {
    CBMFileResult result = {0};
    cbm_arena_init(&result.arena);
    cbm_arena_test_fail_after(&result.arena, 0);
    (void)cbm_arena_alloc(&result.arena, 1);
    bool typed = pxc_destination_status(CBM_LANG_PYTHON, &result, CBM_PXC_DISPATCH_COMPLETE) ==
                 CBM_PXC_DISPATCH_ALLOCATION_FAILED;
    cbm_arena_destroy(&result.arena);
    return typed;
}
#endif

static void pxc_finalize_rust_publication(CBMFileResult *result, uint32_t resolved_before,
                                          uint32_t unresolved_before,
                                          PxcAppendStatus resolved_status, bool synthetic_complete,
                                          bool scratch_complete) {
    result->rust_health.resolved_emitted =
        pxc_saturating_add(resolved_before, resolved_status.resolved_emitted);
    result->rust_health.unresolved_emitted =
        pxc_saturating_add(unresolved_before, resolved_status.unresolved_emitted);
    if (!resolved_status.complete || !synthetic_complete || !scratch_complete) {
        cbm_rust_health_record(&result->rust_health, CBM_RUST_HEALTH_ALLOCATION_UNAVAILABLE, 0, 0);
        result->rust_health.completed_routes &= ~CBM_RUST_HEALTH_ROUTE_CROSS_FILE;
    }
}

#if defined(CBM_INCREMENTAL_TEST_API) && CBM_INCREMENTAL_TEST_API
bool cbm_pxc_test_append_results(CBMFileResult *destination, const CBMResolvedCallArray *source) {
    uint32_t resolved_before = destination->rust_health.resolved_emitted;
    uint32_t unresolved_before = destination->rust_health.unresolved_emitted;
    PxcAppendStatus status =
        pxc_append_results(&destination->arena, &destination->resolved_calls, source);
    pxc_finalize_rust_publication(destination, resolved_before, unresolved_before, status, true,
                                  true);
    return status.complete;
}

bool cbm_pxc_test_append_synthetic_calls(CBMFileResult *destination, const CBMCallArray *source) {
    bool complete = pxc_append_synthetic_calls(&destination->arena, &destination->calls, source);
    if (!complete) {
        cbm_rust_health_record(&destination->rust_health, CBM_RUST_HEALTH_ALLOCATION_UNAVAILABLE, 0,
                               0);
    }
    return complete;
}
#endif

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
        out[i].crate_root_qn = defs[i].rust_crate_root_qn;
        out[i].crate_source_module_qn = defs[i].rust_crate_source_module_qn;
        out[i].return_types = defs[i].return_types;
        out[i].embedded_types = defs[i].embedded_types;
        out[i].field_defs = defs[i].field_defs;
        out[i].method_names_str = defs[i].method_names_str;
        out[i].signature_param_types = defs[i].signature_param_types;
        out[i].signature_param_count = defs[i].signature_param_count;
        out[i].trait_qn = defs[i].trait_qn;
        out[i].is_interface = defs[i].is_interface;
        out[i].is_rust_impl_relation = defs[i].is_rust_impl_relation;
        out[i].is_abstract = defs[i].is_abstract;
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
static CBMPxcDispatchStatus pxc_run_one_with_manifest(
    CBMLanguage lang, CBMFileResult *r, const char *source, int source_len, const char *module_qn,
    CBMLSPDef *defs, int def_count, const char **imp_names, const char **imp_qns,
    const CBMRustImportScope *rust_import_scopes, int imp_count,
    const CBMCargoManifest *rust_manifest) {
    TSTree *tree = r->cached_tree; /* may be NULL — LSP re-parses then */

    CBMArena scratch;
    cbm_arena_init(&scratch);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMCallArray synthetic_calls;
    memset(&synthetic_calls, 0, sizeof(synthetic_calls));
    uint32_t rust_resolved_before = r->rust_health.resolved_emitted;
    uint32_t rust_unresolved_before = r->rust_health.unresolved_emitted;

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
        cbm_run_rust_lsp_cross_scoped_with_manifest_and_usages(
            &scratch, source, source_len, module_qn, rdefs, def_count, imp_names, imp_qns,
            rust_import_scopes, imp_count, tree, rust_manifest, &out, &synthetic_calls,
            &r->rust_health, &r->usages, &r->arena);
        break;
    }
    default:
        break;
    }

    PxcAppendStatus resolved_status = pxc_append_results(&r->arena, &r->resolved_calls, &out);
    bool synthetic_complete = pxc_append_synthetic_calls(&r->arena, &r->calls, &synthetic_calls);
    bool scratch_complete = cbm_arena_status(&scratch) == CBM_ARENA_STATUS_AVAILABLE;
    if (lang == CBM_LANG_RUST) {
        pxc_finalize_rust_publication(r, rust_resolved_before, rust_unresolved_before,
                                      resolved_status, synthetic_complete, scratch_complete);
    }
    cbm_arena_destroy(&scratch);
    return resolved_status.complete && synthetic_complete && scratch_complete
               ? CBM_PXC_DISPATCH_COMPLETE
               : CBM_PXC_DISPATCH_ALLOCATION_FAILED;
}

void cbm_pxc_run_one(CBMLanguage lang, CBMFileResult *r, const char *source, int source_len,
                     const char *module_qn, CBMLSPDef *defs, int def_count, const char **imp_names,
                     const char **imp_qns, int imp_count) {
    pxc_run_one_with_manifest(lang, r, source, source_len, module_qn, defs, def_count, imp_names,
                              imp_qns, NULL, imp_count, NULL);
}

/* Variant of cbm_pxc_run_one for TS/JS/JSX/TSX with explicit dialect
 * flags. Same scratch-arena lifecycle as cbm_pxc_run_one. */
static CBMPxcDispatchStatus pxc_run_one_ts_status(CBMFileResult *r, const char *source,
                                                  int source_len, const char *module_qn,
                                                  CBMLSPDef *defs, int def_count,
                                                  const char **imp_names, const char **imp_qns,
                                                  int imp_count, bool js_mode, bool jsx_mode,
                                                  bool dts_mode) {
    CBMArena scratch;
    cbm_arena_init(&scratch);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));

    cbm_run_ts_lsp_cross(&scratch, source, source_len, module_qn, js_mode, jsx_mode, dts_mode, defs,
                         def_count, imp_names, imp_qns, imp_count, r->cached_tree, &out);

    PxcAppendStatus status = pxc_append_results(&r->arena, &r->resolved_calls, &out);
    bool scratch_complete = cbm_arena_status(&scratch) == CBM_ARENA_STATUS_AVAILABLE;
    cbm_arena_destroy(&scratch);
    return status.complete && scratch_complete ? CBM_PXC_DISPATCH_COMPLETE
                                               : CBM_PXC_DISPATCH_ALLOCATION_FAILED;
}

void cbm_pxc_run_one_ts(CBMFileResult *r, const char *source, int source_len, const char *module_qn,
                        CBMLSPDef *defs, int def_count, const char **imp_names,
                        const char **imp_qns, int imp_count, bool js_mode, bool jsx_mode,
                        bool dts_mode) {
    (void)pxc_run_one_ts_status(r, source, source_len, module_qn, defs, def_count, imp_names,
                                imp_qns, imp_count, js_mode, jsx_mode, dts_mode);
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
CBMPxcDispatchStatus cbm_pxc_dispatch_file(
    CBMLanguage lang, CBMFileResult *result, const char *source, int source_len, const char *rel,
    const char *def_module, const CBMCrossLspRegistries *cross_registries,
    const CBMModuleDefIndex *module_def_index, CBMLSPDef *all_defs, int all_def_count,
    const char **imp_keys, const char **imp_vals, const CBMRustImportScope *rust_import_scopes,
    int imp_count, const CBMCargoManifest *rust_manifest,
    CBMTypeRegistry *(*rust_shared_get)(void *), void *rust_shared_ctx) {
    if (!result) {
        return CBM_PXC_DISPATCH_COMPLETE;
    }
    bool used_prebuilt = false;
    CBMPxcDispatchStatus publication_status = CBM_PXC_DISPATCH_COMPLETE;
    CBMTypeRegistry *prebuilt =
        cross_registries ? cbm_pxc_registry_for_lang(cross_registries, lang) : NULL;
    if (prebuilt) {
        switch (lang) {
        case CBM_LANG_GO:
            /* Tier 3 (metadata-driven): pure lookup over the Tier-1
             * lsp_unresolved entries — no parse, no AST walk. */
            cbm_go_fast_resolve_qualified_calls(result, prebuilt, imp_keys, imp_vals, imp_count);
            used_prebuilt = true;
            break;
        case CBM_LANG_PYTHON: {
            CBMArena scratch;
            cbm_arena_init(&scratch);
            CBMResolvedCallArray out = {0};
            CBMCallArray synthetic_calls = {0};
            cbm_run_py_lsp_cross_with_registry(&scratch, source, source_len, def_module, prebuilt,
                                               imp_keys, imp_vals, imp_count, result->cached_tree,
                                               &out, &synthetic_calls);
            PxcAppendStatus resolved_status =
                pxc_append_results(&result->arena, &result->resolved_calls, &out);
            bool synthetic_complete =
                pxc_append_synthetic_calls(&result->arena, &result->calls, &synthetic_calls);
            if (!resolved_status.complete || !synthetic_complete ||
                cbm_arena_status(&scratch) != CBM_ARENA_STATUS_AVAILABLE) {
                publication_status = CBM_PXC_DISPATCH_ALLOCATION_FAILED;
            }
            cbm_arena_destroy(&scratch);
            used_prebuilt = true;
            break;
        }
        case CBM_LANG_C:
        case CBM_LANG_CPP:
        case CBM_LANG_CUDA:
            cbm_run_c_lsp_cross_with_registry(
                &result->arena, source, source_len, def_module, (lang != CBM_LANG_C), prebuilt,
                imp_keys, imp_vals, imp_count, result->cached_tree, &result->resolved_calls);
            used_prebuilt = true;
            if (cbm_arena_status(&result->arena) != CBM_ARENA_STATUS_AVAILABLE)
                publication_status = CBM_PXC_DISPATCH_ALLOCATION_FAILED;
            break;
        case CBM_LANG_CSHARP:
            cbm_run_cs_lsp_cross_with_registry(&result->arena, source, source_len, def_module,
                                               prebuilt, imp_vals, imp_count, result->cached_tree,
                                               &result->resolved_calls);
            used_prebuilt = true;
            if (cbm_arena_status(&result->arena) != CBM_ARENA_STATUS_AVAILABLE)
                publication_status = CBM_PXC_DISPATCH_ALLOCATION_FAILED;
            break;
        case CBM_LANG_JAVASCRIPT:
        case CBM_LANG_TYPESCRIPT:
        case CBM_LANG_TSX: {
            /* TS: per-file OVERLAY chained to the shared base. Filter to
             * own+imports so the overlay builder can pick out own-module
             * defs without scanning the whole project. */
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
            cbm_run_ts_lsp_cross_with_registry(&result->arena, source, source_len, def_module, js,
                                               jsx, dts, prebuilt, ts_defs, ts_def_count, imp_keys,
                                               imp_vals, imp_count, result->cached_tree,
                                               &result->resolved_calls);
            free(ts_filtered);
            used_prebuilt = true;
            if (cbm_arena_status(&result->arena) != CBM_ARENA_STATUS_AVAILABLE)
                publication_status = CBM_PXC_DISPATCH_ALLOCATION_FAILED;
            break;
        }
        /* PHP falls through to the per-file build path below until its
         * overlay variant lands. */
        default:
            break;
        }
    }

    if (used_prebuilt) {
        return pxc_destination_status(lang, result, publication_status);
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
    }
    if (lang == CBM_LANG_RUST) {
        CBMTypeRegistry *shared = rust_shared_get ? rust_shared_get(rust_shared_ctx) : NULL;
        if (shared) {
            CBMArena scratch;
            cbm_arena_init(&scratch);
            CBMResolvedCallArray out = {0};
            CBMCallArray synthetic_calls = {0};
            uint32_t rust_resolved_before = result->rust_health.resolved_emitted;
            uint32_t rust_unresolved_before = result->rust_health.unresolved_emitted;
            cbm_run_rust_lsp_cross_scoped_with_registry_and_usages(
                &scratch, source, source_len, def_module, shared, imp_keys, imp_vals,
                rust_import_scopes, imp_count, result->cached_tree, rust_manifest, &out,
                &synthetic_calls, &result->rust_health, &result->usages, &result->arena);
            PxcAppendStatus resolved_status =
                pxc_append_results(&result->arena, &result->resolved_calls, &out);
            bool synthetic_complete =
                pxc_append_synthetic_calls(&result->arena, &result->calls, &synthetic_calls);
            pxc_finalize_rust_publication(result, rust_resolved_before, rust_unresolved_before,
                                          resolved_status, synthetic_complete,
                                          cbm_arena_status(&scratch) == CBM_ARENA_STATUS_AVAILABLE);
            cbm_arena_destroy(&scratch);
            publication_status =
                resolved_status.complete && synthetic_complete &&
                        cbm_arena_status(&result->arena) == CBM_ARENA_STATUS_AVAILABLE
                    ? CBM_PXC_DISPATCH_COMPLETE
                    : CBM_PXC_DISPATCH_ALLOCATION_FAILED;
        } else {
            publication_status = pxc_run_one_with_manifest(
                lang, result, source, source_len, def_module, file_defs, file_def_count, imp_keys,
                imp_vals, rust_import_scopes, imp_count, rust_manifest);
        }
    } else if (lang == CBM_LANG_JAVASCRIPT || lang == CBM_LANG_TYPESCRIPT || lang == CBM_LANG_TSX) {
        bool js;
        bool jsx;
        bool dts;
        cbm_pxc_ts_modes(lang, rel, &js, &jsx, &dts);
        publication_status =
            pxc_run_one_ts_status(result, source, source_len, def_module, file_defs, file_def_count,
                                  imp_keys, imp_vals, imp_count, js, jsx, dts);
    } else {
        publication_status = pxc_run_one_with_manifest(
            lang, result, source, source_len, def_module, file_defs, file_def_count, imp_keys,
            imp_vals, rust_import_scopes, imp_count, rust_manifest);
    }
    free(filtered);
    return pxc_destination_status(lang, result, publication_status);
}

static bool pxc_cargo_append_existing_target(const char *repo_path, const char *member_dir,
                                             const char *package_relative, CBMCargoTargetKind kind,
                                             const char *name, CBMArena *arena,
                                             CBMCargoManifest *manifest) {
    if (!repo_path || !package_relative || !arena || !manifest)
        return false;
    char candidate[CBM_SZ_4K];
    int n = snprintf(candidate, sizeof(candidate), "%s/%s%s%s", repo_path,
                     member_dir && member_dir[0] ? member_dir : "",
                     member_dir && member_dir[0] ? "/" : "", package_relative);
    if (n <= 0 || (size_t)n >= sizeof(candidate))
        return false;
    char canonical_repo[CBM_SZ_4K];
    char canonical_target[CBM_SZ_4K];
    if (!cbm_canonical_path(repo_path, canonical_repo, sizeof(canonical_repo)) ||
        !cbm_canonical_path(candidate, canonical_target, sizeof(canonical_target))) {
        return false;
    }
    cbm_normalize_path_sep(canonical_repo);
    cbm_normalize_path_sep(canonical_target);
    size_t repo_len = strlen(canonical_repo);
    if (strncmp(canonical_target, canonical_repo, repo_len) != 0 ||
        canonical_target[repo_len] != '/') {
        return false;
    }
    const char *rel = canonical_target + repo_len + 1;
    char blocker[CBM_SZ_4K];
    const char *blocker_root = NULL;
    if (kind != CBM_CARGO_TARGET_LIB && kind != CBM_CARGO_TARGET_BIN) {
        blocker_root = "";
        const char *slash = strrchr(rel, '/');
        size_t blocker_len = slash ? (size_t)(slash - rel) : 0;
        if (blocker_len > 0 && blocker_len < sizeof(blocker)) {
            memcpy(blocker, rel, blocker_len);
            blocker[blocker_len] = '\0';
            blocker_root = blocker;
        }
    }
    return cbm_cargo_add_routed_target(arena, manifest, kind, name, member_dir ? member_dir : "",
                                       rel, blocker_root);
}

static bool pxc_cargo_has_explicit_bin(const CBMCargoManifest *manifest, const char *name) {
    for (int i = 0; manifest && name && i < manifest->target_count; i++) {
        if (manifest->targets[i].kind == CBM_CARGO_TARGET_BIN && manifest->targets[i].name &&
            strcmp(manifest->targets[i].name, name) == 0)
            return true;
    }
    return false;
}

static void pxc_cargo_normalize_relative(char *path) {
    if (!path)
        return;
    cbm_normalize_path_sep(path);
    char normalized[CBM_SZ_4K];
    size_t marks[CBM_SZ_4K / 2];
    size_t out = 0;
    int depth = 0;
    const char *p = path;
    while (*p) {
        while (*p == '/')
            p++;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0 || (len == 1 && start[0] == '.'))
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (depth > 0)
                out = marks[--depth];
            continue;
        }
        if (out && out + 1 < sizeof(normalized))
            normalized[out++] = '/';
        if (out + len >= sizeof(normalized) || depth >= (int)(sizeof(marks) / sizeof(marks[0]))) {
            return;
        }
        marks[depth++] = out ? out - 1 : 0;
        memcpy(normalized + out, start, len);
        out += len;
    }
    normalized[out] = '\0';
    memcpy(path, normalized, out + 1);
}

static bool pxc_cargo_has_explicit_bin_path(const CBMCargoManifest *manifest, const char *path) {
    char normalized_path[CBM_SZ_4K];
    snprintf(normalized_path, sizeof(normalized_path), "%s", path ? path : "");
    pxc_cargo_normalize_relative(normalized_path);
    for (int i = 0; manifest && path && i < manifest->target_count; i++) {
        if (manifest->targets[i].kind == CBM_CARGO_TARGET_BIN && manifest->targets[i].source_path) {
            char explicit_path[CBM_SZ_4K];
            snprintf(explicit_path, sizeof(explicit_path), "%s", manifest->targets[i].source_path);
            pxc_cargo_normalize_relative(explicit_path);
            if (strcmp(explicit_path, normalized_path) == 0)
                return true;
        }
    }
    return false;
}

static bool pxc_cargo_relative_is_file(const char *repo_path, const char *member_dir,
                                       const char *rel) {
    char absolute[CBM_SZ_4K];
    int n = snprintf(absolute, sizeof(absolute), "%s/%s%s%s", repo_path,
                     member_dir && member_dir[0] ? member_dir : "",
                     member_dir && member_dir[0] ? "/" : "", rel);
    cbm_path_info_t info;
    return n > 0 && (size_t)n < sizeof(absolute) && cbm_path_info_utf8(absolute, &info) == 0 &&
           info.is_regular;
}

static bool pxc_cargo_relative_path_eq(const char *left, const char *right) {
    char a[CBM_SZ_4K], b[CBM_SZ_4K];
    snprintf(a, sizeof(a), "%s", left ? left : "");
    snprintf(b, sizeof(b), "%s", right ? right : "");
    pxc_cargo_normalize_relative(a);
    pxc_cargo_normalize_relative(b);
    return strcmp(a, b) == 0;
}

static void pxc_cargo_collect_auto_bins(const char *repo_path, const char *member_dir,
                                        const CBMCargoManifest *package, CBMArena *arena,
                                        CBMCargoManifest *root) {
    char bin_dir[CBM_SZ_4K];
    int n = snprintf(bin_dir, sizeof(bin_dir), "%s/%s%ssrc/bin", repo_path,
                     member_dir && member_dir[0] ? member_dir : "",
                     member_dir && member_dir[0] ? "/" : "");
    if (n <= 0 || (size_t)n >= sizeof(bin_dir))
        return;
    cbm_dir_t *dir = cbm_opendir(bin_dir);
    if (!dir)
        return;
    cbm_dirent_t *ent;
    while ((ent = cbm_readdir(dir)) != NULL) {
        if (ent->name[0] == '.')
            continue;
        char rel[CBM_SZ_4K];
        char name[CBM_DIRENT_NAME_MAX];
        if (ent->is_dir) {
            snprintf(name, sizeof(name), "%s", ent->name);
            snprintf(rel, sizeof(rel), "src/bin/%s/main.rs", ent->name);
        } else {
            size_t len = strlen(ent->name);
            if (len <= 3 || strcmp(ent->name + len - 3, ".rs") != 0)
                continue;
            snprintf(name, sizeof(name), "%.*s", (int)(len - 3), ent->name);
            snprintf(rel, sizeof(rel), "src/bin/%s", ent->name);
        }
        if (!pxc_cargo_has_explicit_bin(package, name) &&
            !pxc_cargo_has_explicit_bin_path(package, rel)) {
            pxc_cargo_append_existing_target(repo_path, member_dir, rel, CBM_CARGO_TARGET_BIN, name,
                                             arena, root);
        }
    }
    cbm_closedir(dir);
}

static void pxc_cargo_collect_blocker_dir(const char *repo_path, const char *member_dir,
                                          const char *dirname, CBMCargoTargetKind kind,
                                          CBMArena *arena, CBMCargoManifest *root) {
    char dir_path[CBM_SZ_4K];
    int n = snprintf(dir_path, sizeof(dir_path), "%s/%s%s%s", repo_path,
                     member_dir && member_dir[0] ? member_dir : "",
                     member_dir && member_dir[0] ? "/" : "", dirname);
    if (n <= 0 || (size_t)n >= sizeof(dir_path))
        return;
    cbm_dir_t *dir = cbm_opendir(dir_path);
    if (!dir)
        return;
    cbm_dirent_t *ent;
    while ((ent = cbm_readdir(dir)) != NULL) {
        if (ent->name[0] == '.')
            continue;
        char rel[CBM_SZ_4K];
        char name[CBM_DIRENT_NAME_MAX];
        if (ent->is_dir) {
            snprintf(name, sizeof(name), "%s", ent->name);
            snprintf(rel, sizeof(rel), "%s/%s/main.rs", dirname, ent->name);
        } else {
            size_t len = strlen(ent->name);
            if (len <= 3 || strcmp(ent->name + len - 3, ".rs") != 0)
                continue;
            snprintf(name, sizeof(name), "%.*s", (int)(len - 3), ent->name);
            snprintf(rel, sizeof(rel), "%s/%s", dirname, ent->name);
        }
        pxc_cargo_append_existing_target(repo_path, member_dir, rel, kind, name, arena, root);
    }
    cbm_closedir(dir);
}

static const CBMCargoDep *pxc_cargo_workspace_dependency(const CBMCargoManifest *manifest,
                                                         const char *name) {
    const CBMCargoDep *found = NULL;
    int matches = 0;
    for (int i = 0; manifest && name && i < manifest->dep_count; i++) {
        const CBMCargoDep *dep = &manifest->deps[i];
        if (dep->workspace_source && dep->name && strcmp(dep->name, name) == 0) {
            found = dep;
            matches++;
        }
    }
    return matches == 1 ? found : NULL;
}

/* Resolve a local path dependency to the exact admitted repository-relative
 * package directory. Dependencies outside the indexed repository remain valid
 * Cargo declarations but carry no local target authority. */
typedef enum {
    PXC_CARGO_DEPENDENCY_PATH_INVALID = -1,
    PXC_CARGO_DEPENDENCY_PATH_OUTSIDE_REPOSITORY = 0,
    PXC_CARGO_DEPENDENCY_PATH_INSIDE_REPOSITORY = 1,
} PxcCargoDependencyPathStatus;

static PxcCargoDependencyPathStatus pxc_cargo_dependency_package_dir(const char *repo_path,
                                                                     const char *base_dir,
                                                                     const char *dependency_path,
                                                                     char *out, size_t out_cap) {
    if (!repo_path || !dependency_path || !out || out_cap == 0)
        return PXC_CARGO_DEPENDENCY_PATH_INVALID;
    char candidate[CBM_SZ_4K];
    int n = snprintf(candidate, sizeof(candidate), "%s/%s%s%s", repo_path,
                     base_dir && base_dir[0] ? base_dir : "", base_dir && base_dir[0] ? "/" : "",
                     dependency_path);
    if (n <= 0 || (size_t)n >= sizeof(candidate))
        return PXC_CARGO_DEPENDENCY_PATH_INVALID;
    char canonical_repo[CBM_SZ_4K], canonical_dependency[CBM_SZ_4K];
    if (!cbm_canonical_path(repo_path, canonical_repo, sizeof(canonical_repo)) ||
        !cbm_canonical_path(candidate, canonical_dependency, sizeof(canonical_dependency)))
        return PXC_CARGO_DEPENDENCY_PATH_INVALID;
    cbm_normalize_path_sep(canonical_repo);
    cbm_normalize_path_sep(canonical_dependency);
    size_t repo_len = strlen(canonical_repo);
    const char *relative = NULL;
    if (strcmp(canonical_dependency, canonical_repo) == 0) {
        relative = "";
    } else if (strncmp(canonical_dependency, canonical_repo, repo_len) == 0 &&
               canonical_dependency[repo_len] == '/') {
        relative = canonical_dependency + repo_len + 1;
    } else {
        return PXC_CARGO_DEPENDENCY_PATH_OUTSIDE_REPOSITORY;
    }
    n = snprintf(out, out_cap, "%s", relative);
    return n >= 0 && (size_t)n < out_cap ? PXC_CARGO_DEPENDENCY_PATH_INSIDE_REPOSITORY
                                         : PXC_CARGO_DEPENDENCY_PATH_INVALID;
}

static bool pxc_cargo_collect_package_targets(const char *repo_path, const char *member_dir,
                                              CBMArena *arena, CBMCargoManifest *root_manifest) {
    char manifest_path[CBM_SZ_4K];
    int n = snprintf(manifest_path, sizeof(manifest_path), "%s/%s%sCargo.toml", repo_path,
                     member_dir && member_dir[0] ? member_dir : "",
                     member_dir && member_dir[0] ? "/" : "");
    if (n <= 0 || (size_t)n >= sizeof(manifest_path))
        return false;
    int source_len = 0;
    char *source = pxc_read_file(manifest_path, &source_len);
    if (!source || source_len <= 0) {
        free(source);
        return false;
    }
    CBMCargoManifest package_manifest;
    cbm_cargo_parse(arena, source, source_len, &package_manifest);
    free(source);
    if (!package_manifest.package_name || !package_manifest.targets_complete)
        return false;
    for (int i = 0; i < package_manifest.dep_count; i++) {
        const CBMCargoDep *declared = &package_manifest.deps[i];
        const CBMCargoDep *identity = declared;
        const char *identity_base = member_dir ? member_dir : "";
        if (declared->inherits_workspace) {
            identity = pxc_cargo_workspace_dependency(root_manifest, declared->name);
            identity_base = "";
            if (!identity)
                return false;
        }
        char target_package_dir[CBM_SZ_4K];
        const char *local_dir = NULL;
        if (identity->path) {
            PxcCargoDependencyPathStatus path_status =
                pxc_cargo_dependency_package_dir(repo_path, identity_base, identity->path,
                                                 target_package_dir, sizeof(target_package_dir));
            if (path_status == PXC_CARGO_DEPENDENCY_PATH_INVALID)
                return false;
            if (path_status == PXC_CARGO_DEPENDENCY_PATH_INSIDE_REPOSITORY)
                local_dir = target_package_dir;
        }
        if (!cbm_cargo_add_dependency_route(
                arena, root_manifest, member_dir ? member_dir : "", declared->name,
                identity->package ? identity->package : identity->name, local_dir))
            return false;
    }

    bool has_explicit_lib = package_manifest.has_lib_table;
    bool has_explicit_lib_target = false;
    bool has_explicit_main = false;
    for (int i = 0; i < package_manifest.target_count; i++) {
        if (package_manifest.targets[i].kind == CBM_CARGO_TARGET_LIB) {
            has_explicit_lib = true;
            has_explicit_lib_target = true;
        } else if (package_manifest.targets[i].kind == CBM_CARGO_TARGET_BIN &&
                   package_manifest.targets[i].source_path &&
                   pxc_cargo_relative_path_eq(package_manifest.targets[i].source_path,
                                              "src/main.rs")) {
            has_explicit_main = true;
        }
        const CBMCargoTarget *target = &package_manifest.targets[i];
        const char *path = target->source_path;
        char inferred[CBM_SZ_4K];
        if (!path && target->kind == CBM_CARGO_TARGET_LIB) {
            snprintf(inferred, sizeof(inferred), "src/lib.rs");
            path = inferred;
        } else if (!path && target->kind == CBM_CARGO_TARGET_BIN && target->name) {
            char main_path[CBM_SZ_4K], flat[CBM_SZ_4K], nested[CBM_SZ_4K];
            snprintf(main_path, sizeof(main_path), "src/main.rs");
            snprintf(flat, sizeof(flat), "src/bin/%s.rs", target->name);
            snprintf(nested, sizeof(nested), "src/bin/%s/main.rs", target->name);
            bool main_exists = strcmp(target->name, package_manifest.package_name) == 0 &&
                               pxc_cargo_relative_is_file(repo_path, member_dir, main_path);
            bool flat_exists = pxc_cargo_relative_is_file(repo_path, member_dir, flat);
            bool nested_exists = pxc_cargo_relative_is_file(repo_path, member_dir, nested);
            int inferred_count =
                (main_exists ? 1 : 0) + (flat_exists ? 1 : 0) + (nested_exists ? 1 : 0);
            if (inferred_count == 1) {
                snprintf(inferred, sizeof(inferred), "%s",
                         main_exists ? main_path : (flat_exists ? flat : nested));
                path = inferred;
            } else {
                root_manifest->targets_complete = false;
            }
        }
        if (path) {
            pxc_cargo_append_existing_target(repo_path, member_dir, path, target->kind,
                                             target->name, arena, root_manifest);
        }
    }
    if (package_manifest.autolib && !has_explicit_lib) {
        pxc_cargo_append_existing_target(repo_path, member_dir, "src/lib.rs", CBM_CARGO_TARGET_LIB,
                                         package_manifest.package_name, arena, root_manifest);
    }
    if (package_manifest.has_lib_table && !has_explicit_lib_target) {
        pxc_cargo_append_existing_target(repo_path, member_dir, "src/lib.rs", CBM_CARGO_TARGET_LIB,
                                         package_manifest.package_name, arena, root_manifest);
    }
    if (package_manifest.autobins && !has_explicit_main &&
        !pxc_cargo_has_explicit_bin(&package_manifest, package_manifest.package_name) &&
        !pxc_cargo_has_explicit_bin_path(&package_manifest, "src/main.rs")) {
        pxc_cargo_append_existing_target(repo_path, member_dir, "src/main.rs", CBM_CARGO_TARGET_BIN,
                                         package_manifest.package_name, arena, root_manifest);
    }
    if (package_manifest.autobins) {
        pxc_cargo_collect_auto_bins(repo_path, member_dir, &package_manifest, arena, root_manifest);
    }
    if (package_manifest.build_path) {
        pxc_cargo_append_existing_target(repo_path, member_dir, package_manifest.build_path,
                                         CBM_CARGO_TARGET_BUILD, NULL, arena, root_manifest);
    } else if (package_manifest.auto_build) {
        pxc_cargo_append_existing_target(repo_path, member_dir, "build.rs", CBM_CARGO_TARGET_BUILD,
                                         NULL, arena, root_manifest);
    }
    pxc_cargo_collect_blocker_dir(repo_path, member_dir, "examples", CBM_CARGO_TARGET_EXAMPLE,
                                  arena, root_manifest);
    pxc_cargo_collect_blocker_dir(repo_path, member_dir, "tests", CBM_CARGO_TARGET_TEST, arena,
                                  root_manifest);
    pxc_cargo_collect_blocker_dir(repo_path, member_dir, "benches", CBM_CARGO_TARGET_BENCH, arena,
                                  root_manifest);
    return root_manifest->targets_complete;
}

static bool pxc_cargo_glob_match(const char *pattern, const char *text) {
    if (!pattern || !text)
        return false;
    if (*pattern == '\0')
        return *text == '\0';
    if (pattern[0] == '*' && pattern[1] == '*') {
        pattern += 2;
        if (*pattern == '/') {
            pattern++;
            if (pxc_cargo_glob_match(pattern, text))
                return true;
            while (*text) {
                if (*text++ == '/' && pxc_cargo_glob_match(pattern, text))
                    return true;
            }
            return false;
        }
        do {
            if (pxc_cargo_glob_match(pattern, text))
                return true;
        } while (*text++);
        return false;
    }
    if (*pattern == '*') {
        do {
            if (pxc_cargo_glob_match(pattern + 1, text))
                return true;
        } while (*text && *text++ != '/');
        return false;
    }
    if (*pattern == '?') {
        return *text && *text != '/' && pxc_cargo_glob_match(pattern + 1, text + 1);
    }
    if (*pattern == '[') {
        const char *p = pattern + 1;
        bool negate = *p == '!' || *p == '^';
        if (negate)
            p++;
        bool matched = false;
        char c = *text;
        while (*p && *p != ']') {
            if (p[1] == '-' && p[2] && p[2] != ']') {
                if (c >= p[0] && c <= p[2])
                    matched = true;
                p += 3;
            } else {
                if (c == *p)
                    matched = true;
                p++;
            }
        }
        if (*p != ']' || !c || c == '/' || matched == negate)
            return false;
        return pxc_cargo_glob_match(p + 1, text + 1);
    }
    return *pattern == *text && pxc_cargo_glob_match(pattern + 1, text + 1);
}

static void pxc_cargo_normalize_glob(char *s) {
    if (!s)
        return;
    cbm_normalize_path_sep(s);
    while (s[0] == '.' && s[1] == '/')
        memmove(s, s + 2, strlen(s + 2) + 1);
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '/')
        s[--n] = '\0';
}

static bool pxc_cargo_pattern_has_meta(const char *s) {
    return s && strpbrk(s, "*?[") != NULL;
}

static bool pxc_cargo_excludes_dir(const char *pattern, const char *dir) {
    char prefix[CBM_SZ_4K];
    snprintf(prefix, sizeof(prefix), "%s", dir);
    do {
        if (pxc_cargo_glob_match(pattern, prefix))
            return true;
        char *slash = strrchr(prefix, '/');
        if (!slash)
            break;
        *slash = '\0';
    } while (true);
    return false;
}

static bool pxc_cargo_member_declared(const CBMCargoManifest *manifest, const char *dir) {
    char normalized_dir[CBM_SZ_4K];
    snprintf(normalized_dir, sizeof(normalized_dir), "%s", dir ? dir : "");
    pxc_cargo_normalize_glob(normalized_dir);
    bool included = false;
    bool exact_member = false;
    for (int i = 0; manifest && i < manifest->member_count; i++) {
        char pattern[CBM_SZ_4K];
        snprintf(pattern, sizeof(pattern), "%s", manifest->members[i].member_path);
        pxc_cargo_normalize_glob(pattern);
        if (pxc_cargo_glob_match(pattern, normalized_dir)) {
            included = true;
            if (!pxc_cargo_pattern_has_meta(pattern) && strcmp(pattern, normalized_dir) == 0) {
                exact_member = true;
            }
        }
    }
    if (!included || exact_member)
        return included;
    for (int i = 0; i < manifest->exclude_count; i++) {
        char pattern[CBM_SZ_4K];
        snprintf(pattern, sizeof(pattern), "%s", manifest->excludes[i].member_path);
        pxc_cargo_normalize_glob(pattern);
        if (pxc_cargo_excludes_dir(pattern, normalized_dir))
            return false;
    }
    return true;
}

static bool pxc_cargo_package_already_admitted(const CBMCargoManifest *manifest,
                                               const char *package_dir, int route_index) {
    if (!manifest || !package_dir)
        return false;
    if (pxc_cargo_member_declared(manifest, package_dir))
        return true;
    for (int i = 0; i < manifest->target_count; i++) {
        const char *collected_dir = manifest->targets[i].package_dir;
        if (collected_dir && strcmp(collected_dir, package_dir) == 0)
            return true;
    }
    for (int i = 0; i < route_index; i++) {
        const char *caller_dir = manifest->dependency_routes[i].package_dir;
        if (caller_dir && strcmp(caller_dir, package_dir) == 0)
            return true;
    }
    return false;
}

/* Repositories may contain one Cargo workspace below a non-Rust umbrella
 * root (Codex keeps it in `codex-rs/`).  Derive that root from package
 * manifests, then require it to be unique: merging unrelated workspaces would
 * erase the package boundary that makes dependency routes authoritative. */
static bool pxc_cargo_find_nested_workspace(const char *repo_path, char *out, size_t out_cap) {
    cbm_pkg_members_t packages;
    cbm_pkg_members_init(&packages);
    cbm_pkgmap_collect_members(repo_path, &packages);
    bool found = false;
    bool ambiguous = false;
    for (int i = 0; i < packages.count && !ambiguous; i++) {
        char cursor[CBM_SZ_4K];
        snprintf(cursor, sizeof(cursor), "%s", packages.items[i].dir ?: "");
        while (cursor[0]) {
            char manifest_path[CBM_SZ_4K];
            int n = snprintf(manifest_path, sizeof(manifest_path), "%s/%s/Cargo.toml", repo_path,
                             cursor);
            int source_len = 0;
            char *source = n > 0 && (size_t)n < sizeof(manifest_path)
                               ? pxc_read_file(manifest_path, &source_len)
                               : NULL;
            if (source && source_len > 0) {
                CBMArena arena;
                CBMCargoManifest candidate;
                cbm_arena_init(&arena);
                cbm_cargo_parse(&arena, source, source_len, &candidate);
                if (candidate.is_workspace_root) {
                    if (!found) {
                        int copied = snprintf(out, out_cap, "%s", cursor);
                        found = copied >= 0 && (size_t)copied < out_cap;
                    } else if (strcmp(out, cursor) != 0) {
                        ambiguous = true;
                    }
                }
                cbm_arena_destroy(&arena);
            }
            free(source);
            char *slash = strrchr(cursor, '/');
            if (slash)
                *slash = '\0';
            else
                cursor[0] = '\0';
        }
    }
    bool complete = packages.complete;
    cbm_pkg_members_free(&packages);
    return complete && found && !ambiguous;
}

static const char *pxc_cargo_rebase(CBMArena *arena, const char *prefix, const char *path) {
    if (!path)
        return NULL;
    return path[0] ? cbm_arena_sprintf(arena, "%s/%s", prefix, path)
                   : cbm_arena_strdup(arena, prefix);
}

static bool pxc_cargo_rebase_manifest(CBMArena *arena, CBMCargoManifest *manifest,
                                      const char *prefix) {
    if (!arena || !manifest || !prefix || !prefix[0])
        return true;
    for (int i = 0; i < manifest->target_count; i++) {
        CBMCargoTarget *target = &manifest->targets[i];
        target->package_dir = pxc_cargo_rebase(arena, prefix, target->package_dir ?: "");
        target->source_path = pxc_cargo_rebase(arena, prefix, target->source_path);
        if (target->blocker_root)
            target->blocker_root = pxc_cargo_rebase(arena, prefix, target->blocker_root);
        if (!target->package_dir || !target->source_path ||
            (target->blocker_root && !target->blocker_root[0]))
            return false;
    }
    for (int i = 0; i < manifest->dependency_route_count; i++) {
        CBMCargoDependencyRoute *route = &manifest->dependency_routes[i];
        route->package_dir = pxc_cargo_rebase(arena, prefix, route->package_dir ?: "");
        if (route->target_package_dir)
            route->target_package_dir = pxc_cargo_rebase(arena, prefix, route->target_package_dir);
        if (!route->package_dir)
            return false;
    }
    return cbm_arena_status(arena) == CBM_ARENA_STATUS_AVAILABLE;
}

bool cbm_pxc_build_rust_manifest(const char *repo_path, CBMArena *marena, CBMCargoManifest *out_m) {
    if (!out_m) {
        return false;
    }
    memset(out_m, 0, sizeof(*out_m));
    if (!repo_path || !marena) {
        cbm_rust_health_record(&out_m->health, CBM_RUST_HEALTH_MANIFEST_READ_FAILED, 0, 0);
        return false;
    }
    char workspace_prefix[CBM_SZ_4K] = {0};
    char manifest_repo[CBM_SZ_4K];
    snprintf(manifest_repo, sizeof(manifest_repo), "%s", repo_path);
    char path[CBM_SZ_4K];
    int n = snprintf(path, sizeof(path), "%s/Cargo.toml", manifest_repo);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        cbm_rust_health_record(&out_m->health, CBM_RUST_HEALTH_MANIFEST_READ_FAILED, 0, 0);
        return false;
    }
    cbm_path_info_t info;
    if (cbm_path_info_utf8(path, &info) != 0) {
        if (!pxc_cargo_find_nested_workspace(repo_path, workspace_prefix, sizeof(workspace_prefix)))
            return false;
        n = snprintf(manifest_repo, sizeof(manifest_repo), "%s/%s", repo_path, workspace_prefix);
        if (n <= 0 || (size_t)n >= sizeof(manifest_repo))
            return false;
        n = snprintf(path, sizeof(path), "%s/Cargo.toml", manifest_repo);
        if (n <= 0 || (size_t)n >= sizeof(path) || cbm_path_info_utf8(path, &info) != 0)
            return false;
    }
    if (!info.is_regular || info.size <= 0) {
        cbm_rust_health_record(&out_m->health, CBM_RUST_HEALTH_MANIFEST_READ_FAILED, 0, 0);
        return false;
    }
    int toml_len = 0;
    char *toml = pxc_read_file(path, &toml_len);
    if (!toml || toml_len <= 0) {
        free(toml);
        cbm_rust_health_record(&out_m->health, CBM_RUST_HEALTH_MANIFEST_READ_FAILED, 0, 0);
        return false;
    }
    cbm_cargo_parse(marena, toml, toml_len, out_m);
    free(toml); /* cargo parser copies into marena */
    /* Parser targets are package-relative staging data. Rebuild the routed
     * inventory from every admitted package, including the root, exactly
     * once; retaining these entries would duplicate root explicit targets. */
    out_m->targets = NULL;
    out_m->target_count = 0;
    out_m->target_cap = 0;
    /* Only Cargo's root package and explicitly declared workspace members are
     * target authorities. A recursive manifest walk would incorrectly admit
     * excluded, vendored, or otherwise unrelated nested packages. */
    if (out_m->package_name) {
        if (!pxc_cargo_collect_package_targets(manifest_repo, "", marena, out_m)) {
            out_m->targets_complete = false;
        }
    }
    cbm_pkg_members_t packages;
    cbm_pkg_members_init(&packages);
    cbm_pkgmap_collect_members(manifest_repo, &packages);
    if (!packages.complete)
        out_m->targets_complete = false;
    for (int i = 0; i < packages.count; i++) {
        const char *member = packages.items[i].dir;
        if (member && member[0] && pxc_cargo_member_declared(out_m, member)) {
            if (!pxc_cargo_collect_package_targets(manifest_repo, member, marena, out_m)) {
                out_m->targets_complete = false;
            }
        }
    }
    cbm_pkg_members_free(&packages);

    /* Exact local path dependencies are Cargo-authorized packages even when
     * they are not workspace members. Walk the dependency-route closure: each
     * admitted package may append more exact local routes while it is read.
     * Registry routes carry no target_package_dir and can never enter this
     * inventory. Existing package targets provide the cycle/dedup guard. */
    for (int route_index = 0; route_index < out_m->dependency_route_count; route_index++) {
        const char *dependency_dir = out_m->dependency_routes[route_index].target_package_dir;
        if (!dependency_dir || !dependency_dir[0] ||
            pxc_cargo_package_already_admitted(out_m, dependency_dir, route_index))
            continue;
        if (!pxc_cargo_collect_package_targets(manifest_repo, dependency_dir, marena, out_m))
            out_m->targets_complete = false;
    }
    if (!pxc_cargo_rebase_manifest(marena, out_m, workspace_prefix))
        out_m->targets_complete = false;
    return true;
}

int cbm_pipeline_pass_lsp_cross(cbm_pipeline_ctx_t *ctx, const cbm_file_info_t *files,
                                int file_count, CBMFileResult **cache) {
    if (!ctx || !files || file_count <= 0 || !cache)
        return 0;

    cbm_log_info("pass.start", "pass", "lsp_cross", "files", itoa_buf(file_count));

    /* Allocate the only early-return resource before creating the manifest
     * snapshot. Once the snapshot exists, this function has one cleanup tail. */
    char **def_modules = (char **)calloc((size_t)file_count, sizeof(char *));
    if (!def_modules) {
        cbm_log_error("pass.err", "pass", "lsp_cross", "phase", "alloc");
        return 0;
    }

    /* Build the Rust workspace manifest once (only when the project has at
     * least one Rust file, to avoid an unconditional Cargo.toml read).
     * The manifest's strings live in `cargo_arena`; the resolver borrows
     * one immutable pointer for the duration of this pass. */
    bool have_rust = false;
    for (int i = 0; i < file_count; i++) {
        if (cache[i] && files[i].language == CBM_LANG_RUST) {
            have_rust = true;
            break;
        }
    }
    CBMArena cargo_arena;
    CBMCargoManifest cargo_manifest;
    const CBMCargoManifest *rust_manifest = NULL;
    if (have_rust) {
        cbm_arena_init(&cargo_arena);
        bool have_manifest =
            cbm_pxc_build_rust_manifest(ctx->repo_path, &cargo_arena, &cargo_manifest);
        rust_manifest = have_manifest ? &cargo_manifest : NULL;
    }

    /* Per-file module QN cache so we don't recompute it once per def + once
     * per call. cbm_pipeline_fqn_module mallocs; freed at end. */
    int def_count = 0;
    CBMPxcCollectStatus collect_status = CBM_PXC_COLLECT_EMPTY;
    int *def_starts = (int *)calloc((size_t)file_count + 1, sizeof(int));
    CBMLSPDef *all_defs =
        cbm_pxc_collect_all_defs(cache, files, file_count, ctx->project_name, def_modules,
                                 &def_count, &collect_status, def_starts, rust_manifest);
    if (collect_status == CBM_PXC_COLLECT_ALLOCATION_FAILED) {
        for (int i = 0; i < file_count; i++) {
            if (cache[i] && files[i].language == CBM_LANG_RUST) {
                cache[i]->rust_health.required_routes |= CBM_RUST_HEALTH_ROUTE_CROSS_FILE;
                cache[i]->rust_health.completed_routes &= ~CBM_RUST_HEALTH_ROUTE_CROSS_FILE;
                cbm_rust_health_record(&cache[i]->rust_health,
                                       CBM_RUST_HEALTH_ALLOCATION_UNAVAILABLE, 0, 0);
            }
        }
    }
    /* Same seam as the parallel driver: serialize per-file surfaces while the
     * result cache is alive. Failure only degrades to a full rebuild on the
     * next incremental run. */
    if (ctx->pipeline && all_defs && def_starts) {
        cbm_lsp_surface_row_t *surface_rows = NULL;
        int surface_count = 0;
        if (cbm_lsp_surface_build_rows(ctx->project_name, cache, files, file_count, all_defs,
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

    bool dispatch_failed = false;
    cbm_pxc_test_poison_non_rust_registry(&ctx->seq_cross_arena);

    if (ctx->seq_cross_arena_live &&
        cbm_arena_status(&ctx->seq_cross_arena) != CBM_ARENA_STATUS_AVAILABLE) {
        memset(&cross_registries, 0, sizeof(cross_registries));
        dispatch_failed = true;
    }

    int processed = 0;
    int skipped_no_lsp = 0;
    int skipped_no_source = 0;
    int per_lang_calls = 0;
    dispatch_failed = dispatch_failed ||
                      cbm_pxc_collection_requires_abort(cache, files, file_count, collect_status);

    for (int i = 0; i < file_count; i++) {
        if (!cache[i])
            continue;
        CBMLanguage lang = files[i].language;
        if (collect_status == CBM_PXC_COLLECT_ALLOCATION_FAILED) {
            continue;
        }
        if (!cbm_pxc_has_cross_lsp(lang)) {
            skipped_no_lsp++;
            continue;
        }

        int source_len = 0;
        char *source = pxc_read_file(files[i].path, &source_len);
        if (!source || source_len <= 0) {
            free(source);
            if (lang == CBM_LANG_RUST) {
                cache[i]->rust_health.required_routes |= CBM_RUST_HEALTH_ROUTE_CROSS_FILE;
                cbm_rust_health_record(&cache[i]->rust_health, CBM_RUST_HEALTH_SOURCE_UNAVAILABLE,
                                       0, 0);
            }
            skipped_no_source++;
            continue;
        }

        if (!def_modules[i]) {
            def_modules[i] = cbm_pipeline_fqn_module_dir(ctx->project_name, files[i].rel_path,
                                                         pxc_module_is_dir(files[i].language));
        }

        const char **imp_keys = NULL;
        const char **imp_vals = NULL;
        CBMRustImportScope *rust_import_scopes = NULL;
        int imp_count = 0;
        CBMPxcImportMapStatus import_status = cbm_pxc_build_import_map_with_rust_authority(
            ctx->gbuf, ctx->project_name, files[i].rel_path, lang, cache[i], files, cache,
            file_count, rust_manifest, &imp_keys, &imp_vals, &rust_import_scopes, &imp_count);
        if (lang == CBM_LANG_RUST)
            cbm_pxc_record_rust_authority_health(cache[i], rust_manifest, import_status);
        if (lang == CBM_LANG_RUST && import_status != CBM_PXC_IMPORT_MAP_COMPLETE) {
            cache[i]->rust_health.completed_routes &= ~CBM_RUST_HEALTH_ROUTE_CROSS_FILE;
            free(source);
            free(rust_import_scopes);
            continue;
        }

        /* Journal around the resolve: a hang here must be attributed to THIS
         * file, not to a stale extraction marker (the innocent-quarantine
         * failure mode). */
        cbm_index_mark_start(files[i].rel_path);
        CBMPxcDispatchStatus dispatch_status = cbm_pxc_dispatch_file(
            lang, cache[i], source, source_len, files[i].rel_path, def_modules[i],
            &cross_registries, module_def_index, all_defs, def_count, imp_keys, imp_vals,
            rust_import_scopes, imp_count, rust_manifest, NULL, NULL);
        if (dispatch_status == CBM_PXC_DISPATCH_ALLOCATION_FAILED && lang != CBM_LANG_RUST) {
            dispatch_failed = true;
        }
        cbm_index_mark_done(files[i].rel_path);
        per_lang_calls++;
        processed++;

        cbm_pxc_free_import_map(imp_keys, imp_vals, imp_count);
        free(rust_import_scopes);
        free(source);
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

    /* All dispatches have returned; no borrowed manifest pointer escapes. */
    if (have_rust) {
        cbm_arena_destroy(&cargo_arena);
    }

    cbm_log_info("pass.done", "pass", "lsp_cross", "files_processed", itoa_buf(processed),
                 "files_skipped_no_lsp", itoa_buf(skipped_no_lsp), "files_skipped_no_source",
                 itoa_buf(skipped_no_source), "defs_total", itoa_buf(def_count), "lsp_calls",
                 itoa_buf(per_lang_calls));
    return dispatch_failed ? CBM_NOT_FOUND : 0;
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

    bool *selected = (bool *)calloc((size_t)idx->def_count, sizeof(*selected));
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
        free(selected);
        *out_success = true;
        return NULL;
    }

    CBMLSPDef *out = (CBMLSPDef *)malloc((size_t)total * sizeof(CBMLSPDef));
    if (!out) {
        free(selected);
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
    free(selected);
    return out;
}
