/*
 * pipeline_delta.c — delta-repair staging primitives (closure route only).
 *
 * The dedicated incremental subsystem: instead of loading the whole previous
 * graph into RAM and dumping a complete new generation, the closure route
 * CLONES the live database (copy-on-write where the filesystem offers it),
 * PATCHES exactly the repaired node/edge set into the clone inside one
 * transaction, and publishes through the same sealed-staging finalize leg as
 * the dump path. The general indexing pipeline is untouched; these
 * primitives exist only for the closure orchestration in
 * pipeline_incremental.c.
 *
 * Id discipline carries the design: node ids are AUTOINCREMENT and never
 * reused, the small in-RAM gbuf is pre-seeded with PROXY nodes carrying
 * their real database ids, and fresh nodes are numbered above the previous
 * generation's MAX(id) — so "id > max_db_id" is the complete, marker-free
 * definition of what the patch inserts, and every edge endpoint id is
 * database-valid by construction.
 *
 * FTS policy: the nodes_fts table is contentless, so purged rows cannot be
 * deleted individually on existing databases; their rowids can never alias
 * a live node again (AUTOINCREMENT), so dead entries simply drop out of the
 * rowid join at query time. The patch inserts rows for exactly the new
 * nodes through cbm_store_fts_rebuild() — the SAME writer the wholesale
 * rebuild uses, so the two paths cannot index different column sets. This
 * path is the one most users actually hit: a hand-written INSERT here that
 * named only the identifier columns would leave nodes_fts.body NULL for
 * every node arriving by delta merge, invisibly, while a full reindex
 * looked perfect (#518/#519).
 */
#include "foundation/constants.h"
#include "pipeline/pipeline_internal.h"

#include <stdint.h> /* intptr_t — SQLITE_TRANSIENT wrapper */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "foundation/compat_fs.h"
#include "foundation/hash_table.h"
#include "foundation/log.h"
#include "sqlite3.h"
#include "store/store.h"

enum { DELTA_IN_CHUNK = 200 };

/* Positional bind indices for the snapshot re-link statement. */
enum {
    DELTA_BIND_1 = 1,
    DELTA_BIND_2 = 2,
    DELTA_BIND_3 = 3,
    DELTA_BIND_4 = 4,
    DELTA_BIND_5 = 5,
};

/* Module-local SQLITE_TRANSIENT wrapper to dodge performance-no-int-to-ptr.
 * Same pattern as store.c's BIND_TRANSIENT and mcp.c's MCP_SQLITE_TRANSIENT. */
static sqlite3_destructor_type delta_sqlite_transient(void) {
    static const volatile intptr_t raw = -1;
    sqlite3_destructor_type dtor = NULL;
    memcpy(&dtor, (const void *)&raw, sizeof(dtor));
    return dtor;
}
#define DELTA_SQLITE_TRANSIENT (delta_sqlite_transient())

/* Assemble "?,?,...,?" for a chunked IN list. buf must hold 2*count. */
static void delta_placeholders(char *buf, int count) {
    int pos = 0;
    for (int i = 0; i < count; i++) {
        buf[pos++] = '?';
        if (i + 1 < count) {
            buf[pos++] = ',';
        }
    }
    buf[pos] = '\0';
}

int cbm_delta_stage_clone(const char *final_db_path, char **out_stage_path) {
    *out_stage_path = NULL;
    char *stage = cbm_pipeline_create_staging_path(final_db_path);
    if (!stage) {
        return CBM_NOT_FOUND;
    }
    if (cbm_clone_or_copy_file(final_db_path, stage) != 0) {
        cbm_pipeline_discard_stage(stage);
        free(stage);
        return CBM_NOT_FOUND;
    }
    *out_stage_path = stage;
    return 0;
}

/* The store's default page cache (~2 MB) is sized for queries. The purge
 * deletes every edge incident to the closure through the edge table's
 * secondary indexes, and the patch re-inserts them: random row-level writes
 * across B-trees as large as the file. With a small cache each write evicts
 * and re-reads index pages (measured 11.4 s for an 841-file purge on a
 * 460 MB graph, against 3.8 s once the pages stay resident). A cache the
 * size of the staged file bounds that working set; it is still smaller than
 * the in-memory graph the full rebuild — this route's only alternative —
 * holds, and SQLite allocates cache pages only as they are touched. */
int cbm_delta_stage_size_cache(cbm_store_t *staging, const char *stage_path) {
    enum { KIB = 1024, DEFAULT_CACHE_KIB = 2000 };
    sqlite3 *db = cbm_store_get_db(staging);
    if (!db) {
        return CBM_NOT_FOUND;
    }
    struct stat st;
    long long cache_kib = DEFAULT_CACHE_KIB;
    if (stat(stage_path, &st) == 0 && (long long)st.st_size / KIB > cache_kib) {
        cache_kib = (long long)st.st_size / KIB;
    }
    char sql[CBM_SZ_64];
    snprintf(sql, sizeof(sql), "PRAGMA cache_size = -%lld;", cache_kib);
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : CBM_NOT_FOUND;
}

/* Append the current row (source QN, target QN, type, properties) to the
 * snapshot. Returns false on allocation failure. */
static bool delta_snapshot_push(cbm_delta_saved_edge_t **items, int *count, int *cap,
                                sqlite3_stmt *stmt) {
    if (*count >= *cap) {
        int ncap = *cap ? *cap * 2 : 64;
        cbm_delta_saved_edge_t *grown = realloc(*items, (size_t)ncap * sizeof(**items));
        if (!grown) {
            return false;
        }
        *items = grown;
        *cap = ncap;
    }
    const char *sq = (const char *)sqlite3_column_text(stmt, 0);
    const char *tq = (const char *)sqlite3_column_text(stmt, 1);
    const char *type = (const char *)sqlite3_column_text(stmt, 2);
    const char *props = (const char *)sqlite3_column_text(stmt, 3);
    cbm_delta_saved_edge_t *e = &(*items)[*count];
    e->source_qn = strdup(sq ? sq : "");
    e->target_qn = strdup(tq ? tq : "");
    e->type = strdup(type ? type : "");
    e->props = strdup(props ? props : "{}");
    if (!e->source_qn || !e->target_qn || !e->type || !e->props) {
        free(e->source_qn);
        free(e->target_qn);
        free(e->type);
        free(e->props);
        return false;
    }
    (*count)++;
    return true;
}

typedef enum {
    DELTA_SNAPSHOT_INBOUND,  /* target re-parsed, source not */
    DELTA_SNAPSHOT_OUTBOUND, /* source re-parsed, producer may not be */
} delta_snapshot_side_t;

/* Snapshot one chunk of re-parsed paths from one side. Rows carry the source
 * file in column 4 and the target file in column 5; `reparsed` holds every
 * re-parsed path, not only this chunk's. Returns 0 or CBM_NOT_FOUND. */
static int delta_snapshot_chunk(sqlite3 *db, const char *project, const char *const *paths,
                                int chunk, delta_snapshot_side_t side, const CBMHashTable *reparsed,
                                cbm_delta_saved_edge_t **items, int *count, int *cap) {
    char ph[2 * DELTA_IN_CHUNK + 1];
    delta_placeholders(ph, chunk);
    char sql[CBM_SZ_4K];
    int n;
    if (side == DELTA_SNAPSHOT_INBOUND) {
        n = snprintf(sql, sizeof(sql),
                     /* CROSS JOIN pins nodes-first: the planner otherwise walks
                      * EVERY project edge through the url_path index prefix
                      * (measured 14.6s vs 4ms at kernel scale). */
                     "SELECT src.qualified_name, tgt.qualified_name, e.type, e.properties,"
                     " src.file_path, tgt.file_path"
                     " FROM nodes tgt"
                     " CROSS JOIN edges e ON e.target_id = tgt.id"
                     " CROSS JOIN nodes src ON e.source_id = src.id"
                     " WHERE tgt.project = ?1 AND tgt.file_path IN (%s)"
                     " AND src.file_path NOT IN (%s)"
                     " AND src.file_path <> '' AND src.file_path IS NOT NULL",
                     ph, ph);
    } else {
        /* Only impl links can outlive their source's re-parse; the type
         * filter keeps the walk on the (source_id, type) index. */
        n = snprintf(sql, sizeof(sql),
                     "SELECT src.qualified_name, tgt.qualified_name, e.type, e.properties,"
                     " src.file_path, tgt.file_path"
                     " FROM nodes src"
                     " CROSS JOIN edges e ON e.source_id = src.id"
                     " CROSS JOIN nodes tgt ON e.target_id = tgt.id"
                     " WHERE src.project = ?1 AND src.file_path IN (%s)"
                     " AND e.type IN ('IMPLEMENTS', 'DEFINES_METHOD')",
                     ph);
    }
    if (n < 0 || (size_t)n >= sizeof(sql)) {
        return CBM_NOT_FOUND;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, CBM_NOT_FOUND, &stmt, NULL) != SQLITE_OK) {
        return CBM_NOT_FOUND;
    }
    sqlite3_bind_text(stmt, 1, project, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    for (int i = 0; i < chunk; i++) {
        sqlite3_bind_text(stmt, 2 + i, paths[i], CBM_NOT_FOUND, SQLITE_TRANSIENT);
        if (side == DELTA_SNAPSHOT_INBOUND) {
            sqlite3_bind_text(stmt, 2 + chunk + i, paths[i], CBM_NOT_FOUND, SQLITE_TRANSIENT);
        }
    }
    int step_rc;
    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *type = (const char *)sqlite3_column_text(stmt, 2);
        const char *source_file_path = (const char *)sqlite3_column_text(stmt, 4);
        const char *target_file_path = (const char *)sqlite3_column_text(stmt, 5);
        bool source_reparsed = side == DELTA_SNAPSHOT_OUTBOUND;
        bool target_reparsed = side == DELTA_SNAPSHOT_INBOUND ||
                               (target_file_path && cbm_ht_get(reparsed, target_file_path));
        if (!cbm_pipeline_persisted_edge_outlives_reparse(source_file_path, type, source_reparsed,
                                                          target_reparsed)) {
            continue;
        }
        if (!delta_snapshot_push(items, count, cap, stmt)) {
            sqlite3_finalize(stmt);
            return CBM_NOT_FOUND;
        }
    }
    sqlite3_finalize(stmt);
    return step_rc == SQLITE_DONE ? 0 : CBM_NOT_FOUND;
}

int cbm_delta_snapshot_carried_edges(cbm_store_t *store, const char *project,
                                     const char *const *paths, int path_count,
                                     cbm_delta_saved_edge_t **out, int *out_count) {
    *out = NULL;
    *out_count = 0;
    if (path_count <= 0) {
        return 0;
    }
    sqlite3 *db = cbm_store_get_db(store);
    if (!db) {
        return CBM_NOT_FOUND;
    }
    CBMHashTable *reparsed = cbm_ht_create((uint32_t)path_count * 2U + CBM_SZ_64);
    if (!reparsed) {
        return CBM_NOT_FOUND;
    }
    for (int i = 0; i < path_count; i++) {
        cbm_ht_set(reparsed, paths[i], (void *)paths[i]);
    }
    cbm_delta_saved_edge_t *items = NULL;
    int count = 0;
    int cap = 0;
    int rc = 0;
    static const delta_snapshot_side_t sides[] = {DELTA_SNAPSHOT_INBOUND, DELTA_SNAPSHOT_OUTBOUND};
    for (size_t s = 0; rc == 0 && s < sizeof(sides) / sizeof(sides[0]); s++) {
        for (int off = 0; rc == 0 && off < path_count; off += DELTA_IN_CHUNK) {
            int chunk = path_count - off;
            if (chunk > DELTA_IN_CHUNK) {
                chunk = DELTA_IN_CHUNK;
            }
            rc = delta_snapshot_chunk(db, project, paths + off, chunk, sides[s], reparsed, &items,
                                      &count, &cap);
        }
    }
    cbm_ht_free(reparsed);
    if (rc != 0) {
        cbm_delta_free_snapshot(items, count);
        return CBM_NOT_FOUND;
    }
    *out = items;
    *out_count = count;
    return 0;
}

void cbm_delta_free_snapshot(cbm_delta_saved_edge_t *items, int count) {
    if (!items) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(items[i].source_qn);
        free(items[i].target_qn);
        free(items[i].type);
        free(items[i].props);
    }
    free(items);
}

int cbm_delta_purge(cbm_store_t *store, const char *project, const char *const *paths,
                    int path_count) {
    if (path_count <= 0) {
        return 0;
    }
    sqlite3 *db = cbm_store_get_db(store);
    if (!db) {
        return CBM_NOT_FOUND;
    }
    for (int off = 0; off < path_count; off += DELTA_IN_CHUNK) {
        int chunk = path_count - off;
        if (chunk > DELTA_IN_CHUNK) {
            chunk = DELTA_IN_CHUNK;
        }
        char ph[2 * DELTA_IN_CHUNK + 1];
        delta_placeholders(ph, chunk);
        char sql[CBM_SZ_1K];
        int n = snprintf(sql, sizeof(sql),
                         "DELETE FROM nodes WHERE project = ?1 AND file_path IN (%s)", ph);
        if (n < 0 || (size_t)n >= sizeof(sql)) {
            return CBM_NOT_FOUND;
        }
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, sql, CBM_NOT_FOUND, &stmt, NULL) != SQLITE_OK) {
            return CBM_NOT_FOUND;
        }
        sqlite3_bind_text(stmt, 1, project, CBM_NOT_FOUND, SQLITE_TRANSIENT);
        for (int i = 0; i < chunk; i++) {
            sqlite3_bind_text(stmt, 2 + i, paths[off + i], CBM_NOT_FOUND, SQLITE_TRANSIENT);
        }
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            return CBM_NOT_FOUND;
        }
    }
    return 0;
}

/* Every project node becomes a proxy. The first cut filtered by label and
 * the fail-closed patch immediately found the counterexamples (synthetic
 * Decorator nodes on django; Macro is six of the kernel's 8.5M nodes) — any
 * node the resolvers or restricted post-passes can target as an edge
 * endpoint must carry its real id in RAM, and curating that set by label is
 * guessing. The proxy load stays edge-free and property-free, which is
 * where the old full load actually spent its time. */

int64_t cbm_delta_preseed(cbm_store_t *store, const char *project, cbm_gbuf_t *gbuf) {
    sqlite3 *db = cbm_store_get_db(store);
    if (!db) {
        return -1;
    }
    int64_t max_id = 0;
    {
        sqlite3_stmt *stmt = NULL;
        /* Global MAX, not project-scoped: node ids are one keyspace for the
         * whole database, and the watermark must clear every row in it. */
        if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id), 0) FROM nodes", CBM_NOT_FOUND, &stmt,
                               NULL) != SQLITE_OK) {
            return -1;
        }
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            max_id = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    /* Proxy only what resolution can reach by NAME or QN: the registry
     * symbols (cbm_label_is_type_like + Function/Method/Variable/Field, the
     * exact set incr_label_is_registry_symbol admits) plus the structural
     * nodes edges attach to. Everything else -- overwhelmingly Macro, six of
     * the kernel's 8.5M nodes -- is never a lookup target, and a proxy for it
     * would be pure load cost.
     *
     * Safety net for the labels this list does not anticipate: a resolver
     * that upserts an unproxied QN now produces a fresh node that the patch
     * maps back onto its existing row by qualified name, rather than the
     * UNIQUE violation the pre-remap patch would have raised. A resolver that
     * only LOOKS UP still needs its target resident, which is why the list
     * mirrors the registry's own membership rule instead of guessing. */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT id, label, name, qualified_name, file_path FROM nodes"
                           " WHERE project = ?1 AND label NOT IN"
                           " ('Macro','Comment','Section','Branch','Commit','Tag')"
                           " ORDER BY id",
                           CBM_NOT_FOUND, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, project, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    int step_rc;
    int64_t seeded = 0;
    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(stmt, 0);
        const char *label = (const char *)sqlite3_column_text(stmt, 1);
        const char *name = (const char *)sqlite3_column_text(stmt, 2);
        const char *qn = (const char *)sqlite3_column_text(stmt, 3);
        const char *fp = (const char *)sqlite3_column_text(stmt, 4);
        /* Pin the gbuf id to the database id: proxies ARE their rows. */
        cbm_gbuf_set_next_id(gbuf, id);
        int64_t got = cbm_gbuf_upsert_node(gbuf, label, name, qn, fp ? fp : "", 0, 0, "{}");
        if (got != id) {
            /* A QN collision inside the preseed set would silently split
             * identity between RAM and disk; the run cannot be trusted. */
            sqlite3_finalize(stmt);
            cbm_log_error("delta.preseed_id_mismatch", "qn", qn ? qn : "");
            return -1;
        }
        seeded++;
    }
    sqlite3_finalize(stmt);
    if (step_rc != SQLITE_DONE) {
        return -1;
    }
    cbm_gbuf_set_next_id(gbuf, max_id + 1);
    char seeded_buf[32];
    snprintf(seeded_buf, sizeof(seeded_buf), "%lld", (long long)seeded);
    cbm_log_info("delta.preseed", "proxies", seeded_buf);
    return max_id;
}

/* Patch visitor state: inserts everything above the id watermark. */
typedef struct {
    sqlite3 *db;
    const char *project;
    int64_t max_db_id;
    sqlite3_stmt *node_stmt;
    sqlite3_stmt *edge_stmt;
    sqlite3_stmt *qn_lookup;
    /* gbuf id -> real database id for any fresh node whose qualified
     * name turns out to already exist on disk: a resolver referenced
     * a symbol the narrowed proxy set did not pre-load, so it upserted
     * a stand-in. Mapping it back keeps the existing row authoritative
     * and every edge endpoint database-valid. Appended in ascending
     * gbuf-id order (nodes are visited that way), so lookups binary
     * search -- CBMHashTable stores key POINTERS without copying,
     * which a stack-formatted integer key cannot satisfy. */
    struct {
        int64_t from;
        int64_t to;
    } *remap;
    int remap_count;
    int remap_cap;
    bool failed;
    int64_t nodes;
    int64_t edges;
    int64_t remapped;
} delta_patch_ctx_t;

static void delta_remap_put(delta_patch_ctx_t *ctx, int64_t from, int64_t to) {
    if (ctx->remap_count >= ctx->remap_cap) {
        int ncap = ctx->remap_cap ? ctx->remap_cap * 2 : 256;
        void *grown = realloc(ctx->remap, (size_t)ncap * sizeof(*ctx->remap));
        if (!grown) {
            ctx->failed = true;
            return;
        }
        ctx->remap = grown;
        ctx->remap_cap = ncap;
    }
    ctx->remap[ctx->remap_count].from = from;
    ctx->remap[ctx->remap_count].to = to;
    ctx->remap_count++;
}

static int64_t delta_remap_get(const delta_patch_ctx_t *ctx, int64_t id) {
    if (id <= ctx->max_db_id || ctx->remap_count == 0) {
        return id;
    }
    int lo = 0;
    int hi = ctx->remap_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (ctx->remap[mid].from == id) {
            return ctx->remap[mid].to;
        }
        if (ctx->remap[mid].from < id) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return id;
}

static void delta_patch_node(const cbm_gbuf_node_t *node, void *userdata) {
    delta_patch_ctx_t *ctx = (delta_patch_ctx_t *)userdata;
    if (ctx->failed || node->id <= ctx->max_db_id) {
        return;
    }
    if (node->qualified_name) {
        sqlite3_reset(ctx->qn_lookup);
        sqlite3_bind_text(ctx->qn_lookup, 1, ctx->project, CBM_NOT_FOUND, SQLITE_TRANSIENT);
        sqlite3_bind_text(ctx->qn_lookup, 2, node->qualified_name, CBM_NOT_FOUND, SQLITE_TRANSIENT);
        if (sqlite3_step(ctx->qn_lookup) == SQLITE_ROW) {
            /* Already on disk: an unchanged file's symbol. A node from a
             * CHANGED file cannot appear here -- the purge removed it --
             * so repaired files still receive fresh rows. */
            delta_remap_put(ctx, node->id, sqlite3_column_int64(ctx->qn_lookup, 0));
            ctx->remapped++;
            return;
        }
    }
    sqlite3_reset(ctx->node_stmt);
    sqlite3_bind_int64(ctx->node_stmt, 1, node->id);
    sqlite3_bind_text(ctx->node_stmt, 2, ctx->project, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    sqlite3_bind_text(ctx->node_stmt, 3, node->label, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    sqlite3_bind_text(ctx->node_stmt, 4, node->name, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    sqlite3_bind_text(ctx->node_stmt, 5, node->qualified_name, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    sqlite3_bind_text(ctx->node_stmt, 6, node->file_path ? node->file_path : "", CBM_NOT_FOUND,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(ctx->node_stmt, 7, node->start_line);
    sqlite3_bind_int(ctx->node_stmt, 8, node->end_line);
    sqlite3_bind_text(ctx->node_stmt, 9, node->properties_json ? node->properties_json : "{}",
                      CBM_NOT_FOUND, SQLITE_TRANSIENT);
    if (sqlite3_step(ctx->node_stmt) != SQLITE_DONE) {
        ctx->failed = true;
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "%lld", (long long)node->id);
        cbm_log_error("delta.patch_node_failed", "qn",
                      node->qualified_name ? node->qualified_name : "", "err",
                      sqlite3_errmsg(ctx->db));
        cbm_log_error("delta.patch_node_id", "id", id_buf);
        sqlite3_stmt *who = NULL;
        if (sqlite3_prepare_v2(ctx->db, "SELECT label, qualified_name FROM nodes WHERE id = ?1",
                               CBM_NOT_FOUND, &who, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(who, 1, node->id);
            if (sqlite3_step(who) == SQLITE_ROW) {
                cbm_log_error("delta.patch_id_owner", "label",
                              (const char *)sqlite3_column_text(who, 0), "qn",
                              (const char *)sqlite3_column_text(who, 1));
            }
            sqlite3_finalize(who);
        }
        return;
    }
    ctx->nodes++;
}

static void delta_patch_edge(const cbm_gbuf_edge_t *edge, void *userdata) {
    delta_patch_ctx_t *ctx = (delta_patch_ctx_t *)userdata;
    if (ctx->failed) {
        return;
    }
    int64_t src = delta_remap_get(ctx, edge->source_id);
    int64_t tgt = delta_remap_get(ctx, edge->target_id);
    if (src <= ctx->max_db_id && tgt <= ctx->max_db_id && src == edge->source_id &&
        tgt == edge->target_id) {
        return; /* both endpoints pre-existed unremapped: already recorded */
    }
    sqlite3_reset(ctx->edge_stmt);
    sqlite3_bind_text(ctx->edge_stmt, 1, ctx->project, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    sqlite3_bind_int64(ctx->edge_stmt, 2, src);
    sqlite3_bind_int64(ctx->edge_stmt, 3, tgt);
    sqlite3_bind_text(ctx->edge_stmt, 4, edge->type, CBM_NOT_FOUND, SQLITE_TRANSIENT);
    sqlite3_bind_text(ctx->edge_stmt, 5, edge->properties_json ? edge->properties_json : "{}",
                      CBM_NOT_FOUND, SQLITE_TRANSIENT);
    if (sqlite3_step(ctx->edge_stmt) != SQLITE_DONE) {
        ctx->failed = true;
        cbm_log_error("delta.patch_edge_failed", "type", edge->type ? edge->type : "");
        return;
    }
    ctx->edges++;
}

/* Re-link the snapshotted edges by qualified name. An endpoint whose QN no
 * longer exists simply matches no row — full-reindex semantics for deleted
 * symbols, dedup by the UNIQUE edge constraint — and an endpoint pair the
 * edge's producer would no longer link (cbm_pipeline_carried_edge_fits) is
 * dropped. Returns false on failure.
 *
 * Extracted from cbm_delta_patch to keep that function inside the
 * cognitive-complexity budget. */
static bool delta_relink_snapshot(sqlite3 *db, const char *project,
                                  const cbm_delta_saved_edge_t *snapshot, int snapshot_count) {
    sqlite3_stmt *endpoints = NULL;
    sqlite3_stmt *relink = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT s.id, s.file_path, s.label, t.id, t.label"
                           " FROM nodes s, nodes t"
                           " WHERE s.project = ?1 AND s.qualified_name = ?2"
                           " AND t.project = ?1 AND t.qualified_name = ?3",
                           CBM_NOT_FOUND, &endpoints, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db,
                           "INSERT OR IGNORE INTO edges (project, source_id, target_id,"
                           " type, properties) VALUES (?1, ?2, ?3, ?4, ?5)",
                           CBM_NOT_FOUND, &relink, NULL) != SQLITE_OK) {
        sqlite3_finalize(endpoints);
        return false;
    }
    bool ok = true;
    for (int i = 0; i < snapshot_count && ok; i++) {
        sqlite3_reset(endpoints);
        sqlite3_bind_text(endpoints, DELTA_BIND_1, project, CBM_NOT_FOUND, DELTA_SQLITE_TRANSIENT);
        sqlite3_bind_text(endpoints, DELTA_BIND_2, snapshot[i].source_qn, CBM_NOT_FOUND,
                          DELTA_SQLITE_TRANSIENT);
        sqlite3_bind_text(endpoints, DELTA_BIND_3, snapshot[i].target_qn, CBM_NOT_FOUND,
                          DELTA_SQLITE_TRANSIENT);
        int step_rc = sqlite3_step(endpoints);
        if (step_rc == SQLITE_DONE) {
            continue;
        }
        if (step_rc != SQLITE_ROW) {
            ok = false;
            break;
        }
        const char *source_file = (const char *)sqlite3_column_text(endpoints, 1);
        const char *source_label = (const char *)sqlite3_column_text(endpoints, 2);
        const char *target_label = (const char *)sqlite3_column_text(endpoints, 4);
        if (!cbm_pipeline_carried_edge_fits(source_file, snapshot[i].type, source_label,
                                            target_label)) {
            continue;
        }
        sqlite3_reset(relink);
        sqlite3_bind_text(relink, DELTA_BIND_1, project, CBM_NOT_FOUND, DELTA_SQLITE_TRANSIENT);
        sqlite3_bind_int64(relink, DELTA_BIND_2, sqlite3_column_int64(endpoints, 0));
        sqlite3_bind_int64(relink, DELTA_BIND_3, sqlite3_column_int64(endpoints, 3));
        sqlite3_bind_text(relink, DELTA_BIND_4, snapshot[i].type, CBM_NOT_FOUND,
                          DELTA_SQLITE_TRANSIENT);
        sqlite3_bind_text(relink, DELTA_BIND_5, snapshot[i].props, CBM_NOT_FOUND,
                          DELTA_SQLITE_TRANSIENT);
        ok = sqlite3_step(relink) == SQLITE_DONE;
    }
    sqlite3_finalize(endpoints);
    sqlite3_finalize(relink);
    return ok;
}

int cbm_delta_patch(cbm_store_t *store, const char *project, cbm_gbuf_t *gbuf, int64_t max_db_id,
                    const cbm_delta_saved_edge_t *snapshot, int snapshot_count) {
    sqlite3 *db = cbm_store_get_db(store);
    if (!db) {
        return CBM_NOT_FOUND;
    }
    if (cbm_store_begin(store) != CBM_STORE_OK) {
        return CBM_NOT_FOUND;
    }
    delta_patch_ctx_t ctx = {.db = db, .project = project, .max_db_id = max_db_id};
    if (sqlite3_prepare_v2(db,
                           "SELECT id FROM nodes WHERE project = ?1"
                           " AND qualified_name = ?2",
                           CBM_NOT_FOUND, &ctx.qn_lookup, NULL) != SQLITE_OK) {
        cbm_store_rollback(store);
        return CBM_NOT_FOUND;
    }
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO nodes (id, project, label, name, qualified_name,"
                           " file_path, start_line, end_line, properties)"
                           " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9)",
                           CBM_NOT_FOUND, &ctx.node_stmt, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db,
                           "INSERT OR IGNORE INTO edges (project, source_id, target_id, type,"
                           " properties) VALUES (?1,?2,?3,?4,?5)",
                           CBM_NOT_FOUND, &ctx.edge_stmt, NULL) != SQLITE_OK) {
        sqlite3_finalize(ctx.node_stmt);
        sqlite3_finalize(ctx.edge_stmt);
        sqlite3_finalize(ctx.qn_lookup);
        free(ctx.remap);
        cbm_store_rollback(store);
        return CBM_NOT_FOUND;
    }
    cbm_gbuf_foreach_node(gbuf, delta_patch_node, &ctx);
    if (!ctx.failed) {
        cbm_gbuf_foreach_edge(gbuf, delta_patch_edge, &ctx);
    }
    sqlite3_finalize(ctx.node_stmt);
    sqlite3_finalize(ctx.edge_stmt);
    sqlite3_finalize(ctx.qn_lookup);

    if (!ctx.failed && snapshot_count > 0 &&
        !delta_relink_snapshot(db, project, snapshot, snapshot_count)) {
        ctx.failed = true;
    }

    /* Row-level FTS for exactly the new nodes, through the shared writer —
     * same columns, same tokenizer, same prose backfill as the wholesale
     * rebuild. NOT_FOUND means the index could not be written at all (FTS5
     * compiled out); search then runs without it, matching the dump path. */
    if (!ctx.failed) {
        int fts_rc = cbm_store_fts_rebuild(store, project, max_db_id);
        if (fts_rc == CBM_STORE_NOT_FOUND) {
            cbm_log_warn("delta.fts_insert_unavailable", "project", project);
        } else if (fts_rc != CBM_STORE_OK) {
            ctx.failed = true;
        }
    }

    if (ctx.failed) {
        free(ctx.remap);
        cbm_store_rollback(store);
        return CBM_NOT_FOUND;
    }
    free(ctx.remap);
    if (cbm_store_commit(store) != CBM_STORE_OK) {
        return CBM_NOT_FOUND;
    }
    char nodes_buf[32];
    char edges_buf[32];
    snprintf(nodes_buf, sizeof(nodes_buf), "%lld", (long long)ctx.nodes);
    snprintf(edges_buf, sizeof(edges_buf), "%lld", (long long)ctx.edges);
    char remap_buf[32];
    snprintf(remap_buf, sizeof(remap_buf), "%lld", (long long)ctx.remapped);
    cbm_log_info("delta.patch", "nodes", nodes_buf, "edges", edges_buf, "remapped", remap_buf);
    return 0;
}
