/*
 * pass_usages.c — Resolve usages, throws, and read/write edges.
 *
 * For each file, re-extracts and resolves:
 *   - USAGE edges: ordinary identifier references to registered symbols
 *   - CALL_REFERENCE edges: explicit callable-reference syntax (not invocations)
 *   - THROWS/RAISES edges: exception types
 *   - READS/WRITES edges: variable read/write access patterns
 *
 * All three use the same registry lookup strategy. Combined into one pass
 * to avoid triple re-extraction.
 *
 * Depends on: pass_definitions having populated the registry and graph buffer
 */
#include "foundation/constants.h"
#include "foundation/str_util.h" // cbm_json_escape
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_internal.h"
#include "pipeline/lsp_resolve.h"
#include "pipeline/pass_lsp_cross.h"
#include "graph_buffer/graph_buffer.h"
#include "foundation/log.h"
#include "foundation/compat.h"
#include "foundation/compat_fs.h"
#include "foundation/limits.h"
#include "cbm.h"
#include "lsp/rust_modtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CBM_CALL_REFERENCE_LOOKUP_TEST_API) && CBM_CALL_REFERENCE_LOOKUP_TEST_API
#include <stdatomic.h>

static _Atomic uint64_t g_lsp_reference_lookup_rows_examined = 0;

void cbm_pipeline_lsp_reference_lookup_test_note_row(void) {
    atomic_fetch_add_explicit(&g_lsp_reference_lookup_rows_examined, 1, memory_order_relaxed);
}

void cbm_pipeline_lsp_reference_lookup_test_reset(void) {
    atomic_store_explicit(&g_lsp_reference_lookup_rows_examined, 0, memory_order_relaxed);
}

uint64_t cbm_pipeline_lsp_reference_lookup_test_rows_examined(void) {
    return atomic_load_explicit(&g_lsp_reference_lookup_rows_examined, memory_order_relaxed);
}
#endif

/* True for languages whose module QN derives from the CONTAINING DIRECTORY
 * (Java/Go package). MUST match cbm_lang_module_is_dir() (internal/cbm/helpers.c)
 * so same-module resolution keys against the directory-based def-node QNs. */
static bool pu_module_is_dir(CBMLanguage lang) {
    return lang == CBM_LANG_JAVA || lang == CBM_LANG_GO;
}

/* Read file into heap buffer. Caller must free(). */
static char *read_file(const char *path, int *out_len) {
    FILE *f = cbm_fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    (void)fseek(f, 0, SEEK_END);
    long size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > cbm_max_file_bytes()) { /* generous, env-configurable cap (B4) */
        (void)fclose(f);
        return NULL;
    }
    /* +pad: tree-sitter lexer lookahead reads past EOF; keep it in-bounds */
    enum { CBM_TS_LOOKAHEAD_PAD = 16 };
    char *buf = malloc((size_t)size + CBM_TS_LOOKAHEAD_PAD);
    if (!buf) {
        (void)fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, SKIP_ONE, size, f);
    (void)fclose(f);
    if (nread > (size_t)size) {
        nread = (size_t)size;
    }
    memset(buf + nread, 0, CBM_TS_LOOKAHEAD_PAD);
    *out_len = (int)nread;
    return buf;
}

static const char *itoa_log(int val) {
    enum { RING_BUF_COUNT = 4, RING_BUF_MASK = 3 };
    static CBM_TLS char bufs[RING_BUF_COUNT][CBM_SZ_32];
    static CBM_TLS int idx = 0;
    int i = idx;
    idx = (idx + SKIP_ONE) & RING_BUF_MASK;
    snprintf(bufs[i], sizeof(bufs[i]), "%d", val);
    return bufs[i];
}

/* Check if an exception name is a "checked" exception (Java-style).
 * Checked: Exception, IOException, etc. (extends Exception, not RuntimeException).
 * Simple heuristic: if name contains "Error" or "Panic", it's a runtime exception. */
static bool is_checked_exception(const char *name) {
    if (!name) {
        return false;
    }
    if (strstr(name, "Error") || strstr(name, "Panic") || strstr(name, "error") ||
        strstr(name, "panic")) {
        return false;
    }
    return true; /* Default: treat as checked */
}

/* Build the same semantic import map used by cross-LSP and parallel resolution.
 * The old sequential-only shortcut treated raw paths such as `./types` as QNs,
 * so duplicate symbol names fell through to an ambiguous unique-name lookup. */
static int build_import_map(cbm_pipeline_ctx_t *ctx, const char *rel_path,
                            const CBMFileResult *result, const char ***out_keys,
                            const char ***out_vals, int *out_count, CBMLanguage lang) {
    return cbm_pxc_build_import_map(ctx, ctx->project_name, rel_path, lang, result, out_keys,
                                    out_vals, out_count);
}

static void free_import_map(const char **keys, const char **vals, int count) {
    cbm_pxc_free_import_map(keys, vals, count);
}

/* Find the graph buffer node for an enclosing function QN, falling back to file node. */
static const cbm_gbuf_node_t *find_enclosing_node(cbm_pipeline_ctx_t *ctx, const char *func_qn,
                                                  const char *rel_path) {
    const cbm_gbuf_node_t *node = NULL;
    if (func_qn && func_qn[0]) {
        node = cbm_gbuf_find_by_qn(ctx->gbuf, func_qn);
        /* A class-level reference in a directory-module language carries the
         * DIRECTORY module QN, which hits the shared Folder/Project node —
         * attribute to this file's File node instead (#787). */
        if (cbm_pipeline_node_is_dir_container(node)) {
            node = NULL;
        }
    }
    if (!node) {
        char *file_qn = cbm_pipeline_fqn_compute(ctx->project_name, rel_path, "__file__");
        node = cbm_gbuf_find_by_qn(ctx->gbuf, file_qn);
        free(file_qn);
    }
    return node;
}

/* The CALLS resolver's admitted invocation record for the call whose callee is
 * this occurrence: the innermost record in the same function that encloses the
 * reference token and names the same member leaf. Two records at that site
 * that disagree on the target prove nothing. */
static const CBMResolvedCall *rust_member_invocation_record(const CBMResolvedCallArray *rows,
                                                            const CBMUsage *usage,
                                                            const cbm_gbuf_t *gbuf,
                                                            const char *project_name) {
    if (!rows || !usage->enclosing_func_qn || usage->site_end_byte <= usage->site_start_byte) {
        return NULL;
    }
    const char *ref_leaf = cbm_lsp_bare_segment(usage->ref_name);
    const CBMResolvedCall *best = NULL;
    bool ambiguous = false;
    for (int i = 0; i < rows->count; i++) {
        const CBMResolvedCall *rc = &rows->items[i];
        if (rc->kind != CBM_RESOLVED_INVOCATION || !rc->caller_qn || !rc->callee_qn ||
            !rc->strategy || rc->confidence < CBM_LSP_CONFIDENCE_FLOOR ||
            !cbm_pipeline_rust_strategy_admitted(rc->strategy) ||
            rc->source_origin != usage->source_origin ||
            rc->site_start_byte > usage->site_start_byte ||
            rc->site_end_byte < usage->site_end_byte ||
            strcmp(rc->caller_qn, usage->enclosing_func_qn) != 0 ||
            !cbm_pipeline_lsp_method_leaf_eq(cbm_lsp_bare_segment(rc->callee_qn), ref_leaf)) {
            continue;
        }
        if (best &&
            rc->site_end_byte - rc->site_start_byte > best->site_end_byte - best->site_start_byte) {
            continue;
        }
        if (best && rc->site_start_byte == best->site_start_byte &&
            rc->site_end_byte == best->site_end_byte) {
            if (!cbm_pipeline_invocation_targets_equal(best->callee_qn, rc->callee_qn, gbuf,
                                                       project_name, false)) {
                ambiguous = true;
            }
            continue;
        }
        best = rc;
        ambiguous = false;
    }
    return ambiguous ? NULL : best;
}

const cbm_gbuf_node_t *cbm_pipeline_rust_member_usage_target(const cbm_gbuf_t *gbuf,
                                                             const char *project_name,
                                                             const cbm_registry_t *registry,
                                                             const CBMResolvedCallArray *rows,
                                                             const CBMUsage *usage) {
    if (!usage || !usage->ref_name) {
        return NULL;
    }
    const CBMResolvedCall *record = rust_member_invocation_record(rows, usage, gbuf, project_name);
    if (record) {
        return cbm_pipeline_lsp_target_node(gbuf, project_name, record->callee_qn, false);
    }
    const char *owned =
        cbm_registry_rust_owner_member(registry, usage->ref_name, usage->enclosing_func_qn, NULL);
    return owned ? cbm_gbuf_find_by_qn(gbuf, owned) : NULL;
}

/* The Field node named by `field_qn`. A field sharing its QN with a same-named
 * method of its type carries the extractor's `#L<line>` suffix; exactly one
 * such Field is required. */
static const cbm_gbuf_node_t *rust_field_node(const cbm_gbuf_t *gbuf, const char *project_name,
                                              const char *field_qn) {
    const cbm_gbuf_node_t *node = cbm_pipeline_lsp_target_node(gbuf, project_name, field_qn, false);
    if (node && node->label && strcmp(node->label, "Field") == 0) {
        return node;
    }
    if (!node || !node->qualified_name) {
        return NULL;
    }
    const cbm_gbuf_node_t **named = NULL;
    int count = 0;
    if (cbm_gbuf_find_by_name(gbuf, cbm_lsp_bare_segment(field_qn), &named, &count) != 0) {
        return NULL;
    }
    size_t qn_len = strlen(node->qualified_name);
    const cbm_gbuf_node_t *field = NULL;
    for (int i = 0; i < count; i++) {
        const cbm_gbuf_node_t *n = named[i];
        if (!n->label || strcmp(n->label, "Field") != 0 || !n->qualified_name ||
            strncmp(n->qualified_name, node->qualified_name, qn_len) != 0 ||
            strncmp(n->qualified_name + qn_len, "#L", 2) != 0) {
            continue;
        }
        if (field) {
            return NULL;
        }
        field = n;
    }
    return field;
}

const cbm_gbuf_node_t *cbm_pipeline_rust_field_usage_target(const cbm_gbuf_t *gbuf,
                                                            const char *project_name,
                                                            const CBMResolvedCallArray *rows,
                                                            const CBMUsage *usage, bool *owned) {
    *owned = false;
    if (!rows || !usage || !usage->ref_name || usage->site_end_byte <= usage->site_start_byte) {
        return NULL;
    }
    const CBMResolvedCall *best = NULL;
    for (int i = 0; i < rows->count; i++) {
        const CBMResolvedCall *rc = &rows->items[i];
        if (rc->kind != CBM_RESOLVED_FIELD_REFERENCE || !rc->callee_qn ||
            rc->confidence < CBM_LSP_CONFIDENCE_FLOOR ||
            rc->source_origin != usage->source_origin ||
            rc->site_start_byte != usage->site_start_byte ||
            rc->site_end_byte != usage->site_end_byte ||
            strcmp(cbm_lsp_bare_segment(rc->callee_qn), usage->ref_name) != 0) {
            continue;
        }
        *owned = true;
        if (best && strcmp(best->callee_qn, rc->callee_qn) != 0) {
            return NULL;
        }
        best = rc;
    }
    return best ? rust_field_node(gbuf, project_name, best->callee_qn) : NULL;
}

const cbm_gbuf_node_t *cbm_pipeline_rust_invocation_usage_target(const cbm_gbuf_t *gbuf,
                                                                 const char *project,
                                                                 const CBMFileResult *result,
                                                                 const CBMUsage *usage) {
    for (int i = 0; i < result->calls.count; i++) {
        const CBMCall *call = &result->calls.items[i];
        if (call->callee_start_byte < usage->site_start_byte ||
            call->callee_end_byte > usage->site_end_byte ||
            (call->callee_start_byte != usage->site_start_byte &&
             (!call->callee_name || strcmp(call->callee_name, usage->ref_name) != 0)) ||
            call->source_origin != usage->source_origin)
            continue;
        CBMUsage occurrence = *usage;
        occurrence.site_start_byte = call->site_start_byte;
        occurrence.site_end_byte = call->site_end_byte;
        const CBMResolvedCall *proof =
            rust_member_invocation_record(&result->resolved_calls, &occurrence, gbuf, project);
        if (proof && proof->site_start_byte == call->site_start_byte &&
            proof->site_end_byte == call->site_end_byte)
            return cbm_pipeline_lsp_target_node(gbuf, project, proof->callee_qn, false);
    }
    return NULL;
}

const cbm_gbuf_node_t *cbm_pipeline_rust_item_usage_target(const cbm_gbuf_t *gbuf,
                                                           const cbm_registry_t *registry,
                                                           const char *module_qn,
                                                           const CBMUsage *usage) {
    const CBMRustImportSource *imports = cbm_pxc_get_rust_import_source();
    if (!imports || !imports->modtree || usage->source_origin != CBM_SOURCE_ORIGIN_RAW ||
        (usage->kind != CBM_USAGE_TYPE && usage->semantic_reference_local_shadow) ||
        (usage->scope_path && !usage->scope_path[0]) || usage->is_member_access) {
        return NULL;
    }
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMRustPathResult result =
        usage->kind == CBM_USAGE_TYPE
            ? cbm_rust_modtree_resolve_type(
                  imports->modtree, &arena, module_qn, usage->site_start_byte,
                  usage->scope_path ? usage->scope_path : usage->ref_name, NULL, NULL, 0)
            : cbm_rust_modtree_resolve(imports->modtree, &arena, module_qn, usage->site_start_byte,
                                       usage->scope_path ? usage->scope_path : usage->ref_name,
                                       NULL, NULL, 0);
    const cbm_gbuf_node_t *node =
        (result.kind == CBM_RUST_PATH_NOT_FUNCTION || result.kind == CBM_RUST_PATH_TYPE ||
         result.kind == CBM_RUST_PATH_FUNCTION) &&
                result.qn && !(result.kind == CBM_RUST_PATH_FUNCTION && result.line == 0)
            ? cbm_gbuf_find_by_qn(gbuf, result.qn)
            : NULL;
    if (!node && result.kind == CBM_RUST_PATH_ASSOC && !result.is_trait && result.qn &&
        result.assoc) {
        const char *member = cbm_registry_rust_owner_member(
            registry, cbm_arena_sprintf(&arena, "Self::%s", result.assoc), usage->enclosing_func_qn,
            result.qn);
        node = member ? cbm_gbuf_find_by_qn(gbuf, member) : NULL;
    }
    cbm_arena_destroy(&arena);
    /* The value namespace also contains functions and struct constructors;
     * type parameters and lexical value shadows were rejected above. */
    if (usage->kind != CBM_USAGE_TYPE &&
        (!node || !node->label ||
         (strcmp(node->label, "Variable") != 0 && strcmp(node->label, "Function") != 0 &&
          strcmp(node->label, "Struct") != 0 && strcmp(node->label, "Method") != 0 &&
          strcmp(node->label, "AssociatedConst") != 0))) {
        return NULL;
    }
    return node && node->label &&
                   (strcmp(node->label, "AssociatedConst") == 0 ||
                    strcmp(node->label, "Method") == 0 || strcmp(node->label, "Function") == 0 ||
                    strcmp(node->label, "Variable") == 0 || strcmp(node->label, "Struct") == 0 ||
                    strcmp(node->label, "Enum") == 0 || strcmp(node->label, "Interface") == 0 ||
                    strcmp(node->label, "Type") == 0)
               ? node
               : NULL;
}

const cbm_gbuf_node_t *cbm_pipeline_rust_write_target(const cbm_gbuf_t *gbuf, const char *project,
                                                      const char *module,
                                                      const CBMFileResult *result,
                                                      const CBMReadWrite *rw) {
    CBMUsage usage = {.ref_name = rw->var_name,
                      .enclosing_func_qn = rw->enclosing_func_qn,
                      .site_start_byte = rw->site_start_byte,
                      .site_end_byte = rw->site_end_byte,
                      .is_member_access = rw->is_member_access};
    bool owned = false;
    if (rw->is_member_access) {
        return cbm_pipeline_rust_field_usage_target(gbuf, project, &result->resolved_calls, &usage,
                                                    &owned);
    }
    /* The usage carries the same lexical blockers and block import proof. */
    for (int i = 0; i < result->usages.count; i++) {
        const CBMUsage *site = &result->usages.items[i];
        if (site->site_start_byte == rw->site_start_byte &&
            site->site_end_byte == rw->site_end_byte) {
            const cbm_gbuf_node_t *node =
                cbm_pipeline_rust_item_usage_target(gbuf, NULL, module, site);
            return node && node->label && strcmp(node->label, "Variable") == 0 ? node : NULL;
        }
    }
    return NULL;
}

bool cbm_pipeline_rust_bare_field_target(CBMLanguage lang, const cbm_gbuf_node_t *target) {
    return lang == CBM_LANG_RUST && target && target->label && strcmp(target->label, "Field") == 0;
}

/* Resolve USAGE edges for one file's extracted usages. */
static int resolve_usage_edges(cbm_pipeline_ctx_t *ctx, const CBMFileResult *result,
                               const char *rel, const char *module_qn, const char **imp_keys,
                               const char **imp_vals, int imp_count, CBMLanguage lang) {
    int resolved = 0;
    cbm_pipeline_lsp_reference_index_t reference_index = {0};
    bool reference_index_ready =
        cbm_pipeline_lsp_reference_index_build(&result->resolved_calls, &reference_index);
    for (int u = 0; u < result->usages.count; u++) {
        CBMUsage *usage = &result->usages.items[u];
        if (!usage->ref_name) {
            continue;
        }
        const cbm_gbuf_node_t *src = find_enclosing_node(ctx, usage->enclosing_func_qn, rel);
        if (!src) {
            continue;
        }

        const cbm_gbuf_node_t *tgt = NULL;
        const char *publishing_rule = "registry";
        bool precise_call_reference = false;
        const CBMResolvedCall *semantic_reference = NULL;
        if (cbm_pipeline_usage_semantic_reference_candidate(usage)) {
            bool allow_tail = cbm_pipeline_lsp_allow_tail_match(lang);
            semantic_reference = cbm_pipeline_find_lsp_reference_indexed_in_graph(
                &result->resolved_calls, reference_index_ready ? &reference_index : NULL, usage,
                allow_tail, ctx->gbuf, ctx->project_name);
            if (semantic_reference &&
                !cbm_pipeline_usage_allows_semantic_reference(usage, semantic_reference)) {
                semantic_reference = NULL;
            }
            if (semantic_reference) {
                tgt = cbm_pipeline_lsp_target_node(ctx->gbuf, ctx->project_name,
                                                   semantic_reference->callee_qn, allow_tail);
                precise_call_reference = cbm_pipeline_node_is_callable_target(tgt);
                if (tgt)
                    publishing_rule = "semantic_reference";
            }
        }

        /* A syntactically explicit reference remains useful when exact semantic
         * resolution is unavailable, but the textual registry fallback proves
         * only value use—not an occurrence-exact callable target. Emit USAGE in
         * that case; CALL_REFERENCE is reserved for the exact LSP join. */
        if (!tgt) {
            /* An occurrence-exact semantic record owns this reference even when
             * its target is not materialized in the graph (for example, a
             * Kotlin local function). Falling back by raw name here would bind
             * an unrelated same-named declaration. */
            if (semantic_reference) {
                continue;
            }
            /* A proven field access owns its occurrence (twin: pass_parallel.c). */
            bool field_owned = false;
            if (lang == CBM_LANG_RUST) {
                tgt = cbm_pipeline_rust_field_usage_target(
                    ctx->gbuf, ctx->project_name, &result->resolved_calls, usage, &field_owned);
            }
            if (tgt)
                publishing_rule = "field_owner";
            if (field_owned && !tgt) {
                continue;
            }
        }
        if (!tgt && lang == CBM_LANG_RUST && usage->is_member_access) {
            tgt = cbm_pipeline_rust_member_usage_target(ctx->gbuf, ctx->project_name, ctx->registry,
                                                        &result->resolved_calls, usage);
            if (tgt)
                publishing_rule = "invocation";
        }
        if (!tgt && lang == CBM_LANG_RUST) {
            tgt = cbm_pipeline_rust_invocation_usage_target(ctx->gbuf, ctx->project_name, result,
                                                            usage);
            if (tgt)
                publishing_rule = "invocation";
        }
        if (!tgt && lang == CBM_LANG_RUST) {
            tgt = cbm_pipeline_rust_item_usage_target(ctx->gbuf, ctx->registry, module_qn, usage);
        }
        if (tgt && strcmp(publishing_rule, "registry") == 0 && lang == CBM_LANG_RUST)
            publishing_rule = "module_path";
        /* An unresolved Rust type path must not be shortened to a same-named
         * declaration from another module or an unrelated imported alias. */
        if (!tgt && lang == CBM_LANG_RUST && usage->kind == CBM_USAGE_TYPE) {
            continue;
        }
        if (!tgt && lang == CBM_LANG_RUST) {
            continue;
        }
        if (!tgt) {
            /* SQL usages are FROM/JOIN lineage refs and may bind Table/View
             * targets (cbm_registry_resolve_lineage); every other language
             * resolves through the default variant, whose central relation
             * veto keeps same-named code identifiers out of the lineage layer. */
            cbm_resolution_t res =
                (lang == CBM_LANG_SQL)
                    ? cbm_registry_resolve_lineage(ctx->registry, usage->ref_name, module_qn,
                                                   imp_keys, imp_vals, imp_count)
                    : cbm_registry_resolve(ctx->registry, usage->ref_name, module_qn, imp_keys,
                                           imp_vals, imp_count);
            if (!res.qualified_name || res.qualified_name[0] == '\0') {
                continue;
            }
            if (!cbm_pipeline_reference_candidate_fallback_allowed(lang, usage, res.strategy,
                                                                   imp_keys, imp_count)) {
                continue;
            }
            tgt = cbm_gbuf_find_by_qn(ctx->gbuf, res.qualified_name);
            /* #1928: the registry fallback is a bare-name guess — never let it
             * bind a reference across a language boundary (the LSP-backed
             * semantic branch above is not affected). */
            if (tgt &&
                (cbm_suppress_cross_language_ref(lang, tgt->file_path) ||
                 cbm_suppress_cross_language_code_match(lang, ctx->repo_path, tgt->file_path))) {
                continue;
            }
            /* #1942/#1962: a bare Go reference can never denote a struct
             * field; the member half of a selector may. */
            if (tgt && cbm_go_suppress_bare_field_ref(lang == CBM_LANG_GO, usage->is_member_access,
                                                      tgt->label)) {
                continue;
            }
            if (usage->semantic_reference_blocked && (usage->semantic_reference_local_shadow ||
                                                      cbm_pipeline_node_is_callable_target(tgt))) {
                continue;
            }
            /* A bare name never proves a Rust field or type member. */
            if (cbm_pipeline_rust_bare_field_target(lang, tgt)) {
                continue;
            }
            if (tgt && lang == CBM_LANG_RUST && cbm_registry_label_is_type_member(tgt->label)) {
                tgt = cbm_pipeline_rust_member_usage_target(
                    ctx->gbuf, ctx->project_name, ctx->registry, &result->resolved_calls, usage);
            }
        }
        if (!tgt || src->id == tgt->id ||
            (cbm_language_for_filename(src->file_path) == CBM_LANG_PROTOBUF &&
             cbm_suppress_cross_language_ref(CBM_LANG_PROTOBUF, tgt->file_path)) ||
            cbm_suppress_cross_language_code_match(src->file_path &&
                                                           strcmp(src->file_path, rel) == 0
                                                       ? lang
                                                       : cbm_language_for_filename(src->file_path),
                                                   ctx->repo_path, tgt->file_path)) {
            continue;
        }

        /* ref_name is sliced source text and can contain quotes/newlines —
         * escape it or the edge properties JSON is malformed. */
        char esc_ref[CBM_SZ_256];
        cbm_json_escape(esc_ref, sizeof(esc_ref), usage->ref_name);
        char uprops[CBM_SZ_512];
        if (lang == CBM_LANG_RUST)
            snprintf(uprops, sizeof(uprops), "{\"callee\":\"%s\",\"rule\":\"%s\"}", esc_ref,
                     publishing_rule);
        else
            snprintf(uprops, sizeof(uprops), "{\"callee\":\"%s\"}", esc_ref);
        const char *edge_type = precise_call_reference ? "CALL_REFERENCE" : "USAGE";
        cbm_gbuf_insert_edge(ctx->gbuf, src->id, tgt->id, edge_type, uprops);
        resolved++;
    }
    cbm_pipeline_lsp_reference_index_free(&reference_index);
    return resolved;
}

/* Resolve THROWS/RAISES edges for one file's extracted throws. */
static int resolve_throw_edges(cbm_pipeline_ctx_t *ctx, const CBMFileResult *result,
                               const char *rel, const char *module_qn, const char **imp_keys,
                               const char **imp_vals, int imp_count) {
    int resolved = 0;
    for (int t = 0; t < result->throws.count; t++) {
        CBMThrow *thr = &result->throws.items[t];
        if (!thr->exception_name || !thr->enclosing_func_qn) {
            continue;
        }

        const cbm_gbuf_node_t *src = find_enclosing_node(ctx, thr->enclosing_func_qn, rel);
        if (!src) {
            continue;
        }

        const char *edge_type = is_checked_exception(thr->exception_name) ? "THROWS" : "RAISES";
        cbm_resolution_t res = cbm_registry_resolve(ctx->registry, thr->exception_name, module_qn,
                                                    imp_keys, imp_vals, imp_count);

        const cbm_gbuf_node_t *tgt = NULL;
        if (res.qualified_name && res.qualified_name[0]) {
            tgt = cbm_gbuf_find_by_qn(ctx->gbuf, res.qualified_name);
        }
        if (!tgt || src->id == tgt->id ||
            cbm_suppress_cross_language_code_match(cbm_language_for_filename(src->file_path),
                                                   ctx->repo_path, tgt->file_path)) {
            continue;
        }

        cbm_gbuf_insert_edge(ctx->gbuf, src->id, tgt->id, edge_type, "{}");
        resolved++;
    }
    return resolved;
}

/* Resolve READS/WRITES edges for one file's extracted read/write accesses. */
static int resolve_rw_edges(cbm_pipeline_ctx_t *ctx, const CBMFileResult *result, const char *rel,
                            const char *module_qn, const char **imp_keys, const char **imp_vals,
                            int imp_count, CBMLanguage lang) {
    int resolved = 0;
    for (int r = 0; r < result->rw.count; r++) {
        CBMReadWrite *rw = &result->rw.items[r];
        if (!rw->var_name) {
            continue;
        }

        const cbm_gbuf_node_t *src = find_enclosing_node(ctx, rw->enclosing_func_qn, rel);
        if (!src) {
            continue;
        }

        cbm_resolution_t res = cbm_registry_resolve(ctx->registry, rw->var_name, module_qn,
                                                    imp_keys, imp_vals, imp_count);
        if (lang != CBM_LANG_RUST && (!res.qualified_name || res.qualified_name[0] == '\0')) {
            continue;
        }

        const cbm_gbuf_node_t *tgt =
            lang == CBM_LANG_RUST ? cbm_pipeline_rust_write_target(ctx->gbuf, ctx->project_name,
                                                                   module_qn, result, rw)
                                  : cbm_gbuf_find_by_qn(ctx->gbuf, res.qualified_name);
        if (!tgt || src->id == tgt->id) {
            continue;
        }
        /* #1928: every resolution here is a bare-name registry guess — never
         * let it bind a read/write across a language boundary. */
        if (cbm_suppress_cross_language_ref(lang, tgt->file_path) ||
            cbm_suppress_cross_language_code_match(src->file_path &&
                                                           strcmp(src->file_path, rel) == 0
                                                       ? lang
                                                       : cbm_language_for_filename(src->file_path),
                                                   ctx->repo_path, tgt->file_path) ||
            cbm_suppress_cross_language_code_match(lang, ctx->repo_path, tgt->file_path)) {
            continue;
        }
        /* #1942/#1962: a bare Go reference can never denote a struct field;
         * a selector-LHS write (`t.err = x`) may bind it. */
        if (cbm_go_suppress_bare_field_ref(lang == CBM_LANG_GO, rw->is_member_access, tgt->label)) {
            continue;
        }

        const char *edge_type = rw->is_write ? "WRITES" : "READS";
        cbm_gbuf_insert_edge(ctx->gbuf, src->id, tgt->id, edge_type, "{}");
        resolved++;
    }
    return resolved;
}

int cbm_pipeline_pass_usages(cbm_pipeline_ctx_t *ctx, const cbm_file_info_t *files,
                             int file_count) {
    cbm_log_info("pass.start", "pass", "usages", "files", itoa_log(file_count));

    int usage_resolved = 0;
    int throw_resolved = 0;
    int rw_resolved = 0;
    int errors = 0;

    for (int i = 0; i < file_count; i++) {
        if (cbm_pipeline_check_cancel(ctx)) {
            return CBM_NOT_FOUND;
        }

        const char *path = files[i].path;
        const char *rel = files[i].rel_path;

        CBMFileResult *result = NULL;
        bool result_owned = false;
        if (ctx->result_cache) {
            result = ctx->result_cache[i];
        }
        if (!result) {
            int source_len = 0;
            char *source = read_file(path, &source_len);
            if (!source) {
                errors++;
                continue;
            }
            result = cbm_extract_file(source, source_len, files[i].language, ctx->project_name, rel,
                                      CBM_EXTRACT_BUDGET, NULL, NULL);
            free(source);
            if (!result) {
                errors++;
                continue;
            }
            result_owned = true;
        }

        if (result->usages.count == 0 && result->throws.count == 0 && result->rw.count == 0) {
            if (result_owned) {
                cbm_free_result(result);
            }
            continue;
        }

        const char **imp_keys = NULL;
        const char **imp_vals = NULL;
        int imp_count = 0;
        build_import_map(ctx, rel, result, &imp_keys, &imp_vals, &imp_count, files[i].language);

        char *module_qn = cbm_pipeline_fqn_module_dir(ctx->project_name, rel,
                                                      pu_module_is_dir(files[i].language));

        usage_resolved += resolve_usage_edges(ctx, result, rel, module_qn, imp_keys, imp_vals,
                                              imp_count, files[i].language);
        throw_resolved +=
            resolve_throw_edges(ctx, result, rel, module_qn, imp_keys, imp_vals, imp_count);
        rw_resolved += resolve_rw_edges(ctx, result, rel, module_qn, imp_keys, imp_vals, imp_count,
                                        files[i].language);

        free(module_qn);
        free_import_map(imp_keys, imp_vals, imp_count);
        if (result_owned) {
            cbm_free_result(result);
        }
    }

    cbm_log_info("pass.done", "pass", "usages", "usage", itoa_log(usage_resolved), "throws",
                 itoa_log(throw_resolved), "rw", itoa_log(rw_resolved), "errors", itoa_log(errors));
    return 0;
}
