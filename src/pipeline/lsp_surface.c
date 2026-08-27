/*
 * lsp_surface.c — per-file LSP-surface codec (closure-repair incremental).
 *
 * See lsp_surface.h for the contract. Two invariants carry the feature:
 *
 *  1. ROUND-TRIP FIDELITY: defs_from_json(build_json(defs)) must hand the
 *     per-language registrars the same values collect_all_defs would have
 *     built from a real parse — a lossy field here silently degrades
 *     cross-file resolution only on the incremental path, the exact class
 *     of divergence this feature exists to eliminate.
 *
 *  2. CANONICAL BYTES: every field is written, in fixed order, with an
 *     explicit JSON null for absent strings (NULL and "" are different
 *     values in the CBMLSPDef contract — receiver_type NULL means "not a
 *     method", and several registrars branch on that). Byte equality of the
 *     serialization therefore IS surface equality, and the sha over the
 *     bytes is the early-cutoff key: a body edit reserializes identically.
 */
#include "pipeline/lsp_surface.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "cbm.h" /* cbm_label_is_relation — reg-only surface membership */
#include "foundation/log.h"
#include "foundation/sha256.h"
#include "yyjson/yyjson.h"

enum {
    SURFACE_CODEC_VERSION = 5,
    SURFACE_MAX_JSON_BYTES = 8 * 1024 * 1024,
    SURFACE_MAX_ENTRIES = 131072,
};

/* Labels the incremental name registry serves that pxc_map_label does NOT
 * carry into the CBMLSPDef set. Their (name, qn, label) triple must still
 * participate in the surface hash, or renaming one would slip past the
 * early cutoff while stale references to it survive in dependent files —
 * for Table/View that means a renamed table keeping stale FROM/JOIN lineage
 * edges from dependent SQL files. KEEP IN SYNC with pxc_map_label
 * (pass_lsp_cross.c) and incr_label_is_registry_symbol
 * (pipeline_incremental.c); the codec unit test cross-checks the three. */
static bool surface_reg_only_label(const char *label) {
    return label && (strcmp(label, "Field") == 0 || cbm_label_is_relation(label));
}

static void add_str_or_null(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                            const char *val) {
    if (val) {
        yyjson_mut_obj_add_str(doc, obj, key, val);
    } else {
        yyjson_mut_obj_add_null(doc, obj, key);
    }
}

static void add_str_array_or_null(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                  const char **items, int count_or_neg1_terminated) {
    if (!items) {
        yyjson_mut_obj_add_null(doc, obj, key);
        return;
    }
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (count_or_neg1_terminated >= 0) {
        for (int i = 0; i < count_or_neg1_terminated; i++) {
            yyjson_mut_arr_add_str(doc, arr, items[i] ? items[i] : "?");
        }
    } else {
        for (int i = 0; items[i]; i++) {
            yyjson_mut_arr_add_str(doc, arr, items[i]);
        }
    }
    yyjson_mut_obj_add_val(doc, obj, key, arr);
}

/* Serialize one file's surface: its slice of all_defs plus the registry-only
 * symbols from its raw extraction defs. Returns a malloc'd JSON string. */
static char *surface_file_to_json(const CBMFileResult *result, const CBMLSPDef *defs, int def_count,
                                  bool is_rust) {
    if (def_count < 0 || def_count > SURFACE_MAX_ENTRIES ||
        (result &&
         (result->defs.count < 0 || result->defs.count > SURFACE_MAX_ENTRIES ||
          result->imports.count < 0 || result->imports.count > SURFACE_MAX_ENTRIES ||
          result->mod_decls.count < 0 || result->mod_decls.count > SURFACE_MAX_ENTRIES))) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "v", SURFACE_CODEC_VERSION);

    yyjson_mut_val *lsp = yyjson_mut_arr(doc);
    for (int i = 0; i < def_count; i++) {
        const CBMLSPDef *d = &defs[i];
        yyjson_mut_val *o = yyjson_mut_obj(doc);
        add_str_or_null(doc, o, "qn", d->qualified_name);
        add_str_or_null(doc, o, "sn", d->short_name);
        add_str_or_null(doc, o, "lb", d->label);
        add_str_or_null(doc, o, "rt", d->receiver_type);
        add_str_or_null(doc, o, "dm", d->def_module_qn);
        add_str_or_null(doc, o, "rcr", d->rust_crate_root_qn);
        add_str_or_null(doc, o, "rcs", d->rust_crate_source_module_qn);
        add_str_or_null(doc, o, "ret", d->return_types);
        add_str_or_null(doc, o, "emb", d->embedded_types);
        add_str_or_null(doc, o, "fd", d->field_defs);
        add_str_or_null(doc, o, "mn", d->method_names_str);
        add_str_array_or_null(doc, o, "spt", d->signature_param_types, d->signature_param_count);
        yyjson_mut_obj_add_bool(doc, o, "ii", d->is_interface);
        yyjson_mut_obj_add_int(doc, o, "lg", (int)d->lang);
        add_str_or_null(doc, o, "ns", d->namespace_name);
        add_str_or_null(doc, o, "tq", d->trait_qn);
        yyjson_mut_obj_add_bool(doc, o, "ir", d->is_rust_impl_relation);
        yyjson_mut_obj_add_bool(doc, o, "ab", d->is_abstract);
        add_str_array_or_null(doc, o, "dec", d->decorators, -1);
        yyjson_mut_arr_add_val(lsp, o);
    }
    yyjson_mut_obj_add_val(doc, root, "lsp", lsp);

    yyjson_mut_val *reg = yyjson_mut_arr(doc);
    if (result) {
        for (int i = 0; i < result->defs.count; i++) {
            const CBMDefinition *d = &result->defs.items[i];
            if (!surface_reg_only_label(d->label) || !d->name || !d->qualified_name) {
                continue;
            }
            yyjson_mut_val *o = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_str(doc, o, "n", d->name);
            yyjson_mut_obj_add_str(doc, o, "q", d->qualified_name);
            yyjson_mut_obj_add_str(doc, o, "k", d->label);
            yyjson_mut_arr_add_val(reg, o);
        }
    }
    yyjson_mut_obj_add_val(doc, root, "reg", reg);

    if (!is_rust) {
        yyjson_mut_obj_add_null(doc, root, "rust");
    } else {
        yyjson_mut_val *rust = yyjson_mut_obj(doc);
        add_str_or_null(doc, rust, "m", result ? result->module_qn : NULL);
        yyjson_mut_obj_add_int(doc, rust, "is", result ? result->rust_imports_status : 0);
        yyjson_mut_obj_add_int(doc, rust, "ms", result ? result->rust_mod_decls_status : 0);
        yyjson_mut_val *imports = yyjson_mut_arr(doc);
        for (int i = 0; result && i < result->imports.count; i++) {
            const CBMImport *imp = &result->imports.items[i];
            yyjson_mut_val *o = yyjson_mut_obj(doc);
            add_str_or_null(doc, o, "n", imp->local_name);
            add_str_or_null(doc, o, "p", imp->module_path);
            yyjson_mut_obj_add_uint(doc, o, "ds", imp->declaration_start_byte);
            yyjson_mut_obj_add_uint(doc, o, "de", imp->declaration_end_byte);
            yyjson_mut_obj_add_uint(doc, o, "ss", imp->site_start_byte);
            yyjson_mut_obj_add_uint(doc, o, "se", imp->site_end_byte);
            yyjson_mut_obj_add_uint(doc, o, "xs", imp->scope_start_byte);
            yyjson_mut_obj_add_uint(doc, o, "xe", imp->scope_end_byte);
            add_str_or_null(doc, o, "om", imp->owner_module_path);
            yyjson_mut_obj_add_bool(doc, o, "md", imp->rust_module_scope);
            yyjson_mut_obj_add_uint(doc, o, "pr", imp->rust_provenance);
            yyjson_mut_obj_add_uint(doc, o, "vi", imp->rust_visibility);
            yyjson_mut_arr_add_val(imports, o);
        }
        yyjson_mut_obj_add_val(doc, rust, "i", imports);
        yyjson_mut_val *mods = yyjson_mut_arr(doc);
        for (int i = 0; result && i < result->mod_decls.count; i++) {
            const CBMModDecl *decl = &result->mod_decls.items[i];
            yyjson_mut_val *o = yyjson_mut_obj(doc);
            add_str_or_null(doc, o, "n", decl->child_name);
            add_str_or_null(doc, o, "pp", decl->parent_path);
            add_str_or_null(doc, o, "p", decl->path_override);
            yyjson_mut_obj_add_uint(doc, o, "vi", decl->rust_visibility);
            yyjson_mut_obj_add_bool(doc, o, "in", decl->is_inline);
            yyjson_mut_obj_add_bool(doc, o, "t", decl->is_cfg_test_gated);
            yyjson_mut_arr_add_val(mods, o);
        }
        yyjson_mut_obj_add_val(doc, rust, "d", mods);
        yyjson_mut_obj_add_val(doc, root, "rust", rust);
    }

    size_t json_len = 0;
    char *json = yyjson_mut_write(doc, 0, &json_len);
    yyjson_mut_doc_free(doc);
    if (json_len > SURFACE_MAX_JSON_BYTES) {
        free(json);
        return NULL;
    }
    return json;
}

int cbm_lsp_surface_build_rows(const char *project, CBMFileResult **cache,
                               const cbm_file_info_t *files, int file_count,
                               const CBMLSPDef *all_defs, const int *def_starts,
                               cbm_lsp_surface_row_t **out_rows, int *out_count) {
    *out_rows = NULL;
    *out_count = 0;
    if (file_count <= 0) {
        return 0;
    }
    cbm_lsp_surface_row_t *rows = calloc((size_t)file_count, sizeof(*rows));
    if (!rows) {
        return -1;
    }
    int n = 0;
    for (int i = 0; i < file_count; i++) {
        if (!cache[i]) {
            /* Never parsed this run (read/extract skip): no surface claim.
             * The routing layer treats a missing row as "must full-rebuild
             * before this file can be reasoned about", which is the correct
             * fail-closed default for an unreadable file. */
            continue;
        }
        int start = def_starts ? def_starts[i] : 0;
        int end = def_starts ? def_starts[i + 1] : 0;
        char *json = surface_file_to_json(cache[i], all_defs ? all_defs + start : NULL, end - start,
                                          files[i].language == CBM_LANG_RUST);
        if (!json) {
            cbm_store_free_lsp_surfaces(rows, n);
            return -1;
        }
        char sha[CBM_SHA256_HEX_LEN + 1];
        cbm_sha256_hex(json, strlen(json), sha);
        cbm_lsp_surface_row_t *r = &rows[n];
        r->project = strdup(project);
        r->rel_path = strdup(files[i].rel_path);
        r->surface_sha = strdup(sha);
        r->defs_json = json;
        r->ref_bloom = NULL;
        r->ref_bloom_len = 0;
        r->config_ctx = strdup("");
        if (!r->project || !r->rel_path || !r->surface_sha || !r->config_ctx) {
            n++;
            cbm_store_free_lsp_surfaces(rows, n);
            return -1;
        }
        n++;
    }
    *out_rows = rows;
    *out_count = n;
    return 0;
}

static const char *arena_str_or_null(CBMArena *arena, yyjson_val *v) {
    if (!v || yyjson_is_null(v)) {
        return NULL;
    }
    const char *s = yyjson_get_str(v);
    return s ? cbm_arena_strdup(arena, s) : NULL;
}

static const char **arena_str_array(CBMArena *arena, yyjson_val *arr, bool null_terminated,
                                    int *out_count) {
    *out_count = 0;
    if (!arr || yyjson_is_null(arr) || !yyjson_is_arr(arr)) {
        return NULL;
    }
    int count = (int)yyjson_arr_size(arr);
    const char **items =
        cbm_arena_alloc(arena, (size_t)(count + (null_terminated ? 1 : 0)) * sizeof(char *));
    if (!items) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        const char *s = yyjson_get_str(yyjson_arr_get(arr, (size_t)i));
        items[i] = s ? cbm_arena_strdup(arena, s) : "?";
    }
    if (null_terminated) {
        items[count] = NULL;
    }
    *out_count = count;
    return items;
}

int cbm_lsp_surface_defs_from_json(CBMArena *arena, const char *defs_json, CBMLSPDef **out_defs) {
    *out_defs = NULL;
    if (!arena || !defs_json) {
        return -1;
    }
    size_t json_len = strnlen(defs_json, SURFACE_MAX_JSON_BYTES + 1U);
    if (json_len > SURFACE_MAX_JSON_BYTES)
        return -1;
    yyjson_doc *doc = yyjson_read(defs_json, json_len, 0);
    if (!doc) {
        return -1;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *ver = root ? yyjson_obj_get(root, "v") : NULL;
    yyjson_val *lsp = root ? yyjson_obj_get(root, "lsp") : NULL;
    if (!ver || yyjson_get_int(ver) != SURFACE_CODEC_VERSION || !lsp || !yyjson_is_arr(lsp)) {
        yyjson_doc_free(doc);
        return -1;
    }
    size_t lsp_size = yyjson_arr_size(lsp);
    if (lsp_size > SURFACE_MAX_ENTRIES || lsp_size > INT_MAX ||
        lsp_size > SIZE_MAX / sizeof(CBMLSPDef)) {
        yyjson_doc_free(doc);
        return -1;
    }
    int count = (int)lsp_size;
    if (count == 0) {
        yyjson_doc_free(doc);
        return 0;
    }
    CBMLSPDef *defs = cbm_arena_alloc(arena, (size_t)count * sizeof(CBMLSPDef));
    if (!defs) {
        yyjson_doc_free(doc);
        return -1;
    }
    memset(defs, 0, (size_t)count * sizeof(CBMLSPDef));
    for (int i = 0; i < count; i++) {
        yyjson_val *o = yyjson_arr_get(lsp, (size_t)i);
        CBMLSPDef *d = &defs[i];
        d->qualified_name = arena_str_or_null(arena, yyjson_obj_get(o, "qn"));
        d->short_name = arena_str_or_null(arena, yyjson_obj_get(o, "sn"));
        d->label = arena_str_or_null(arena, yyjson_obj_get(o, "lb"));
        d->receiver_type = arena_str_or_null(arena, yyjson_obj_get(o, "rt"));
        d->def_module_qn = arena_str_or_null(arena, yyjson_obj_get(o, "dm"));
        d->rust_crate_root_qn = arena_str_or_null(arena, yyjson_obj_get(o, "rcr"));
        d->rust_crate_source_module_qn = arena_str_or_null(arena, yyjson_obj_get(o, "rcs"));
        d->return_types = arena_str_or_null(arena, yyjson_obj_get(o, "ret"));
        d->embedded_types = arena_str_or_null(arena, yyjson_obj_get(o, "emb"));
        d->field_defs = arena_str_or_null(arena, yyjson_obj_get(o, "fd"));
        d->method_names_str = arena_str_or_null(arena, yyjson_obj_get(o, "mn"));
        d->signature_param_types =
            arena_str_array(arena, yyjson_obj_get(o, "spt"), false, &d->signature_param_count);
        d->is_interface = yyjson_get_bool(yyjson_obj_get(o, "ii"));
        d->lang = (CBMLanguage)yyjson_get_int(yyjson_obj_get(o, "lg"));
        d->namespace_name = arena_str_or_null(arena, yyjson_obj_get(o, "ns"));
        d->trait_qn = arena_str_or_null(arena, yyjson_obj_get(o, "tq"));
        d->is_rust_impl_relation = yyjson_get_bool(yyjson_obj_get(o, "ir"));
        d->is_abstract = yyjson_get_bool(yyjson_obj_get(o, "ab"));
        int dec_count = 0;
        d->decorators = arena_str_array(arena, yyjson_obj_get(o, "dec"), true, &dec_count);
        if (!d->qualified_name || !d->short_name || !d->label) {
            /* A def the writer could not have produced: qn/sn/label are
             * unconditionally present in build_lsp_def output. Corrupt row. */
            yyjson_doc_free(doc);
            return -1;
        }
    }
    yyjson_doc_free(doc);
    *out_defs = defs;
    return count;
}

int cbm_lsp_surface_rust_carrier_from_json(CBMArena *arena, const char *defs_json,
                                           CBMFileResult *out) {
    if (!arena || !defs_json || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    size_t json_len = strnlen(defs_json, SURFACE_MAX_JSON_BYTES + 1U);
    if (json_len > SURFACE_MAX_JSON_BYTES)
        return -1;
    yyjson_doc *doc = yyjson_read(defs_json, json_len, 0);
    if (!doc)
        return -1;
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *ver = root ? yyjson_obj_get(root, "v") : NULL;
    yyjson_val *rust = root ? yyjson_obj_get(root, "rust") : NULL;
    if (!ver || yyjson_get_int(ver) != SURFACE_CODEC_VERSION || !rust) {
        yyjson_doc_free(doc);
        return -1;
    }
    if (yyjson_is_null(rust)) {
        yyjson_doc_free(doc);
        return 0;
    }
    if (!yyjson_is_obj(rust)) {
        yyjson_doc_free(doc);
        return -1;
    }
    yyjson_val *imports = yyjson_obj_get(rust, "i");
    yyjson_val *mods = yyjson_obj_get(rust, "d");
    yyjson_val *import_status_value = yyjson_obj_get(rust, "is");
    yyjson_val *mod_status_value = yyjson_obj_get(rust, "ms");
    int import_status = (int)yyjson_get_int(import_status_value);
    int mod_status = (int)yyjson_get_int(mod_status_value);
    const char *module = yyjson_get_str(yyjson_obj_get(rust, "m"));
    if (!module || !imports || !yyjson_is_arr(imports) || !mods || !yyjson_is_arr(mods) ||
        !yyjson_is_int(import_status_value) || !yyjson_is_int(mod_status_value) ||
        import_status < CBM_RUST_CARRIER_COMPLETE || import_status > CBM_RUST_CARRIER_PARTIAL ||
        mod_status < CBM_RUST_CARRIER_COMPLETE || mod_status > CBM_RUST_CARRIER_PARTIAL) {
        yyjson_doc_free(doc);
        return -1;
    }
    out->module_qn = cbm_arena_strdup(arena, module);
    out->rust_imports_status = (CBMRustCarrierStatus)import_status;
    out->rust_mod_decls_status = (CBMRustCarrierStatus)mod_status;
    size_t import_size = yyjson_arr_size(imports);
    if (import_size > SURFACE_MAX_ENTRIES || import_size > INT_MAX ||
        import_size > SIZE_MAX / sizeof(CBMImport)) {
        yyjson_doc_free(doc);
        return -1;
    }
    int import_count = (int)import_size;
    if (import_count > 0) {
        out->imports.items = cbm_arena_alloc(arena, (size_t)import_count * sizeof(CBMImport));
        if (!out->imports.items) {
            yyjson_doc_free(doc);
            return -1;
        }
        memset(out->imports.items, 0, (size_t)import_count * sizeof(CBMImport));
        out->imports.cap = import_count;
        for (int i = 0; i < import_count; i++) {
            yyjson_val *o = yyjson_arr_get(imports, (size_t)i);
            yyjson_val *ds = yyjson_obj_get(o, "ds");
            yyjson_val *de = yyjson_obj_get(o, "de");
            yyjson_val *ss = yyjson_obj_get(o, "ss");
            yyjson_val *se = yyjson_obj_get(o, "se");
            yyjson_val *xs = yyjson_obj_get(o, "xs");
            yyjson_val *xe = yyjson_obj_get(o, "xe");
            yyjson_val *owner_module = yyjson_obj_get(o, "om");
            yyjson_val *module_scope = yyjson_obj_get(o, "md");
            yyjson_val *pr = yyjson_obj_get(o, "pr");
            yyjson_val *vi = yyjson_obj_get(o, "vi");
            uint64_t ds_u = yyjson_get_uint(ds);
            uint64_t de_u = yyjson_get_uint(de);
            uint64_t ss_u = yyjson_get_uint(ss);
            uint64_t se_u = yyjson_get_uint(se);
            uint64_t xs_u = yyjson_get_uint(xs);
            uint64_t xe_u = yyjson_get_uint(xe);
            uint64_t pr_u = yyjson_get_uint(pr);
            uint64_t vi_u = yyjson_get_uint(vi);
            CBMImport *imp = &out->imports.items[i];
            imp->local_name = arena_str_or_null(arena, yyjson_obj_get(o, "n"));
            imp->module_path = arena_str_or_null(arena, yyjson_obj_get(o, "p"));
            imp->owner_module_path = arena_str_or_null(arena, owner_module);
            if (!imp->local_name || !imp->module_path || !yyjson_is_uint(ds) ||
                !yyjson_is_uint(de) || !yyjson_is_uint(ss) || !yyjson_is_uint(se) ||
                !imp->owner_module_path || !yyjson_is_uint(xs) || !yyjson_is_uint(xe) ||
                !yyjson_is_bool(module_scope) || !yyjson_is_uint(pr) || !yyjson_is_uint(vi) ||
                ds_u > UINT32_MAX || de_u > UINT32_MAX || ss_u > UINT32_MAX || se_u > UINT32_MAX ||
                xs_u > UINT32_MAX || xe_u > UINT32_MAX || ds_u > de_u || xs_u >= xe_u ||
                ss_u >= se_u || ss_u < ds_u || se_u > de_u || ds_u < xs_u || de_u > xe_u ||
                (pr_u != CBM_RUST_IMPORT_PROVENANCE_NAMED_EXACT &&
                 pr_u != CBM_RUST_IMPORT_PROVENANCE_GLOB_EXACT) ||
                vi_u > CBM_RUST_IMPORT_VIS_PUBLIC) {
                yyjson_doc_free(doc);
                return -1;
            }
            imp->declaration_start_byte = (uint32_t)ds_u;
            imp->declaration_end_byte = (uint32_t)de_u;
            imp->site_start_byte = (uint32_t)ss_u;
            imp->site_end_byte = (uint32_t)se_u;
            imp->scope_start_byte = (uint32_t)xs_u;
            imp->scope_end_byte = (uint32_t)xe_u;
            imp->rust_module_scope = yyjson_get_bool(module_scope);
            imp->rust_provenance = (uint8_t)pr_u;
            imp->rust_visibility = (uint8_t)vi_u;
        }
        out->imports.count = import_count;
    }
    size_t mod_size = yyjson_arr_size(mods);
    if (mod_size > SURFACE_MAX_ENTRIES || mod_size > INT_MAX ||
        mod_size > SIZE_MAX / sizeof(CBMModDecl)) {
        yyjson_doc_free(doc);
        return -1;
    }
    int mod_count = (int)mod_size;
    if (mod_count > 0) {
        out->mod_decls.items = cbm_arena_alloc(arena, (size_t)mod_count * sizeof(CBMModDecl));
        if (!out->mod_decls.items) {
            yyjson_doc_free(doc);
            return -1;
        }
        memset(out->mod_decls.items, 0, (size_t)mod_count * sizeof(CBMModDecl));
        out->mod_decls.cap = mod_count;
        for (int i = 0; i < mod_count; i++) {
            yyjson_val *o = yyjson_arr_get(mods, (size_t)i);
            yyjson_val *path = yyjson_obj_get(o, "p");
            yyjson_val *parent = yyjson_obj_get(o, "pp");
            yyjson_val *visibility = yyjson_obj_get(o, "vi");
            yyjson_val *is_inline = yyjson_obj_get(o, "in");
            yyjson_val *test_gated = yyjson_obj_get(o, "t");
            CBMModDecl *decl = &out->mod_decls.items[i];
            decl->child_name = arena_str_or_null(arena, yyjson_obj_get(o, "n"));
            decl->parent_path = arena_str_or_null(arena, parent);
            decl->path_override = arena_str_or_null(arena, path);
            uint64_t visibility_u = yyjson_get_uint(visibility);
            decl->rust_visibility = (uint8_t)visibility_u;
            decl->is_inline = yyjson_get_bool(is_inline);
            decl->is_cfg_test_gated = yyjson_get_bool(test_gated);
            if (!decl->child_name || !decl->parent_path || !yyjson_is_str(parent) ||
                (!yyjson_is_null(path) && !yyjson_is_str(path)) || !yyjson_is_bool(is_inline) ||
                !yyjson_is_bool(test_gated) || !yyjson_is_uint(visibility) ||
                visibility_u > CBM_RUST_IMPORT_VIS_PUBLIC) {
                yyjson_doc_free(doc);
                return -1;
            }
        }
        out->mod_decls.count = mod_count;
    }
    bool ok = out->module_qn && cbm_arena_status(arena) == CBM_ARENA_STATUS_AVAILABLE;
    yyjson_doc_free(doc);
    return ok ? 1 : -1;
}
