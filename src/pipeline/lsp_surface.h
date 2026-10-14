/*
 * lsp_surface.h — per-file LSP-surface codec (closure-repair incremental).
 *
 * A file's SURFACE is everything another file's resolution can consume from
 * it: the CBMLSPDefs pass_lsp_cross registers into the cross registries,
 * plus the registry-only symbols (labels the name registry serves that
 * pxc_map_label drops — Field, today), plus a Rust file's `use` table
 * (local name -> module path), which rust_module_export consults when a
 * path crosses this file's re-exports. Serialized to canonical JSON, hashed,
 * and persisted per file (store: lsp_surface rows) at publication, so an
 * incremental run can (a) detect that an edit left a file's surface
 * unchanged — a body edit — and skip recomputing its dependents, and
 * (b) rehydrate the cross registries for files it does not re-parse.
 *
 * The JSON is a versioned object {"v":15,"lang":N,"lsp":[...],"reg":[...],"imp":[...],"py":...}.
 * Python rows carry content-only namespace facts, including ordered binding
 * events and unknown effects; other languages carry an explicit null.
 * Field order and array order are fixed by the writer, so byte equality of the
 * serialization is surface equality; the sha256 of these bytes is the
 * stored surface_sha. Rows written by a different codec version fail the
 * "v" check on load and route the run to a full rebuild.
 */
#ifndef CBM_PIPELINE_LSP_SURFACE_H
#define CBM_PIPELINE_LSP_SURFACE_H

#include "pipeline/pass_lsp_cross.h"
#include "store/store.h"
#include "foundation/arena.h"

/* Build one store row per file that produced surface content. `def_starts`
 * is the (file_count + 1)-entry prefix array from cbm_pxc_collect_all_defs:
 * all_defs[def_starts[i] .. def_starts[i+1]) are file i's defs. Rows carry
 * heap strings; release with cbm_store_free_lsp_surfaces. Files with an
 * empty surface still get a row (empty arrays hash too — "no defs" must be
 * distinguishable from "no data"). Returns 0, or -1 on allocation failure
 * or invalid/incomplete Python namespace metadata. */
int cbm_lsp_surface_build_rows(const cbm_pipeline_ctx_t *ctx, const char *project,
                               CBMFileResult **cache, const cbm_file_info_t *files, int file_count,
                               const CBMLSPDef *all_defs, const int *def_starts,
                               cbm_lsp_surface_row_t **out_rows, int *out_count);

/* Decode one row's defs_json back into the CBMLSPDef array pass_lsp_cross
 * registration consumes. Strings are arena-allocated. Returns the def count,
 * 0 for an empty surface, or -1 when the JSON is missing, malformed, or a
 * different codec version — callers route that to a full rebuild. */
int cbm_lsp_surface_defs_from_json(CBMArena *arena, const char *defs_json, CBMLSPDef **out_defs);

/* Decode one row's Rust `use` table back into the CBMImport array
 * cbm_pipeline_rust_imports_add consumes. Strings are arena-allocated.
 * Returns the import count, 0 for a file without one (every non-Rust file),
 * or -1 on the same conditions as cbm_lsp_surface_defs_from_json. */
int cbm_lsp_surface_imports_from_json(CBMArena *arena, const char *defs_json,
                                      CBMImport **out_imports);

/* Decode one Rust row's `#[path]` module facts (strings arena-allocated).
 * Returns 0 with out->known set, or -1 when the row carries no recorded facts
 * (non-Rust row, facts never recorded, malformed, other codec version). */
int cbm_lsp_surface_rust_path_facts_from_json(CBMArena *arena, const char *defs_json,
                                              CBMRustPathFacts *out);
/* A restored Python namespace retains its raw module identity and source path.
 * Facts and origin strings live in the caller arena; no JSON storage is borrowed. */
typedef struct {
    const char *module_qn;
    const char *rel_path;
    CBMPyNamespaceFacts facts;
} cbm_lsp_python_namespace_t;

/* Returns 1 for a complete Python payload, 0 for a valid non-Python row, or
 * -1 for invalid/incomplete metadata or allocation failure. Output is empty
 * unless a complete Python payload was copied successfully. */
int cbm_lsp_surface_python_namespace_from_json(CBMArena *arena, const char *json,
                                               cbm_lsp_python_namespace_t *out);

/* Conservative namespace admission for incremental planning. Both rows must
 * have a current, valid fact envelope before any equality shortcut is used.
 * Returns -1 for invalid/incomplete metadata, 0 when both rows are non-Python
 * or their complete Python surfaces are identical, and 1 for a Python surface
 * change or a language transition involving Python. A NULL new_json denotes
 * deletion: valid Python metadata returns 1, valid non-Python metadata 0.
 * NULL old_json is invalid. The full Python surface comparison deliberately
 * includes definition properties until a narrower dependency closure is proven.
 */
int cbm_lsp_surface_python_change(const char *old_json, const char *new_json);

#endif /* CBM_PIPELINE_LSP_SURFACE_H */
