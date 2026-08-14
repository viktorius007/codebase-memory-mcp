/*
 * pass_complexity.c — Interprocedural complexity propagation (Tier B).
 *
 * Tier A (in the extraction walk) stamps each Function/Method node with local
 * structural metrics: complexity (cyclomatic), cognitive, loop_count, loop_depth.
 * This pass propagates loop_depth along CALLS edges to estimate a worst-case
 * *transitive* nested-loop degree: a function with a depth-1 loop that calls an
 * O(n) helper is effectively O(n^2). The estimate assumes calls may occur inside
 * loops (an upper bound) — it is a queryable bottleneck *candidate* signal, not a
 * proof (true big-O is undecidable; cf. SPEED / Loopus). Cycles in the call graph
 * are broken and flagged via a `recursive` property.
 *
 * Writes two extra node properties: transitive_loop_depth, recursive.
 */
#include "foundation/constants.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_internal.h"
#include "graph_buffer/graph_buffer.h"
#include "foundation/log.h"
#include "foundation/platform.h"
#include "foundation/compat.h"
#include "cbm.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

enum { CBM_TLD_MAX_DEPTH = 256 }; /* recursion-depth cap (cycle/stack guard) */

/* Int → string for structured logging (thread-safe ring buffer). */
static const char *itoa_cx(int val) {
    enum { RING = 2, MASK = 1 };
    static CBM_TLS char bufs[RING][CBM_SZ_32];
    static CBM_TLS int idx = 0;
    int i = idx;
    idx = (idx + 1) & MASK;
    snprintf(bufs[i], sizeof(bufs[i]), "%d", val);
    return bufs[i];
}

/* Parse an integer "key":N from a flat JSON object. Returns def if absent. */
static int json_get_int(const char *json, const char *key, int dflt) {
    if (!json) {
        return dflt;
    }
    char pat[CBM_SZ_64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return dflt;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (int)strtol(p, NULL, CBM_DECIMAL_BASE);
}

/* Parse a boolean "key":true/false from a flat JSON object. */
static bool json_get_bool(const char *json, const char *key) {
    if (!json) {
        return false;
    }
    char pat[CBM_SZ_64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return *p == 't';
}

/* Append transitive_loop_depth + recursive to a node's properties JSON object. */
static void append_complexity_props(cbm_gbuf_node_t *node, int tld, bool recursive) {
    const char *old = node->properties_json ? node->properties_json : "{}";
    size_t olen = strlen(old);
    if (olen < 2 || old[olen - 1] != '}') {
        return; /* not a JSON object — leave untouched */
    }
    bool empty = (olen == 2); /* "{}" */
    char *neu = malloc(olen + CBM_SZ_64);
    if (!neu) {
        return;
    }
    memcpy(neu, old, olen - 1); /* copy without trailing '}' */
    int w =
        snprintf(neu + (olen - 1), CBM_SZ_64, "%s\"transitive_loop_depth\":%d,\"recursive\":%s}",
                 empty ? "" : ",", tld, recursive ? "true" : "false");
    if (w < 0) {
        free(neu);
        return;
    }
    free(node->properties_json);
    node->properties_json = neu;
}

typedef struct {
    int node;
    int parent;
    const cbm_gbuf_edge_t **edges;
    int edge_count;
    int next_edge;
} scc_frame_t;

#if defined(CBM_ENABLE_TEST_SEAMS) && CBM_ENABLE_TEST_SEAMS
static int g_fail_call_analysis_allocation_after = -1;

void cbm_pipeline_complexity_test_fail_analysis_allocation_once(void) {
    g_fail_call_analysis_allocation_after = 1;
}
#endif

static bool call_analysis_allocation_should_fail(void) {
#if defined(CBM_ENABLE_TEST_SEAMS) && CBM_ENABLE_TEST_SEAMS
    if (g_fail_call_analysis_allocation_after == 0) {
        g_fail_call_analysis_allocation_after = -1;
        return true;
    }
    if (g_fail_call_analysis_allocation_after > 0) {
        g_fail_call_analysis_allocation_after--;
    }
#endif
    return false;
}

static void *call_analysis_malloc(size_t size) {
    /* cppcheck-suppress knownConditionTrueFalse -- true in allocation-fault tests. */
    return call_analysis_allocation_should_fail() ? NULL : malloc(size);
}

static void *call_analysis_calloc(size_t count, size_t size) {
    /* cppcheck-suppress knownConditionTrueFalse -- true in allocation-fault tests. */
    return call_analysis_allocation_should_fail() ? NULL : calloc(count, size);
}

static int dense_index_for_id(const int64_t *ids, int count, int64_t id) {
    int lo = 0;
    int hi = count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (ids[mid] < id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < count && ids[lo] == id ? lo : -1;
}

static int component_tld_dfs(const cbm_gbuf_t *gb, const int64_t *ids, int callable_count,
                             const int *component_of, const int *component_head,
                             const int *next_member, const int *component_local_depth,
                             int *component_tld, char *component_state, int component, int depth) {
    if (component_state[component] == 2) {
        return component_tld[component];
    }
    if (component_state[component] == 1) {
        return 0; /* impossible in the condensed DAG; defensive cycle cut */
    }
    if (depth > CBM_TLD_MAX_DEPTH) {
        return component_local_depth[component];
    }
    component_state[component] = 1;
    int best = 0;
    for (int dense = component_head[component]; dense >= 0; dense = next_member[dense]) {
        const cbm_gbuf_edge_t **edges = NULL;
        int edge_count = 0;
        cbm_gbuf_find_edges_by_source_type(gb, ids[dense], "CALLS", &edges, &edge_count);
        for (int e = 0; e < edge_count; e++) {
            int target = dense_index_for_id(ids, callable_count, edges[e]->target_id);
            if (target < 0) {
                continue;
            }
            int target_component = component_of[target];
            if (target_component == component) {
                continue;
            }
            int candidate = component_tld_dfs(gb, ids, callable_count, component_of, component_head,
                                              next_member, component_local_depth, component_tld,
                                              component_state, target_component, depth + 1);
            if (candidate > best) {
                best = candidate;
            }
        }
    }
    component_tld[component] = component_local_depth[component] + best;
    component_state[component] = 2;
    return component_tld[component];
}

/* Iterative Tarjan traversal over a dense callable-node index. Every member of
 * a multi-node SCC is recursive; a one-node SCC is recursive only with a
 * self-loop. TLD is then evaluated over the condensed SCC DAG: acyclic nodes
 * keep the original recurrence, while a cyclic component has one deterministic
 * local depth (the maximum local depth of its members). */
static bool analyze_call_graph(const cbm_gbuf_t *gb, cbm_gbuf_node_t *const *nptr,
                               const int *loop_depth, int *tld, bool *recursive, int64_t maxid,
                               int callable_count) {
    if (callable_count == 0) {
        return true;
    }

    size_t dense_sz = (size_t)callable_count;
    int64_t *ids = call_analysis_malloc(dense_sz * sizeof(int64_t));
    int *index = call_analysis_malloc(dense_sz * sizeof(int));
    int *low = call_analysis_malloc(dense_sz * sizeof(int));
    bool *on_stack = call_analysis_calloc(dense_sz, sizeof(bool));
    int *node_stack = call_analysis_malloc(dense_sz * sizeof(int));
    int *component_scratch = call_analysis_malloc(dense_sz * sizeof(int));
    int *component_of = call_analysis_malloc(dense_sz * sizeof(int));
    int *component_head = call_analysis_malloc(dense_sz * sizeof(int));
    int *next_member = call_analysis_malloc(dense_sz * sizeof(int));
    int *component_local_depth = call_analysis_calloc(dense_sz, sizeof(int));
    int *component_tld = call_analysis_calloc(dense_sz, sizeof(int));
    char *component_state = call_analysis_calloc(dense_sz, sizeof(char));
    scc_frame_t *frames = call_analysis_calloc(dense_sz, sizeof(scc_frame_t));
    bool ok = ids && index && low && on_stack && node_stack && component_scratch && component_of &&
              component_head && next_member && component_local_depth && component_tld &&
              component_state && frames;
    if (!ok) {
        goto cleanup;
    }

    int dense_count = 0;
    for (int64_t id = 1; id <= maxid; id++) {
        if (nptr[id]) {
            ids[dense_count++] = id;
        }
    }
    if (dense_count != callable_count) {
        ok = false;
        goto cleanup;
    }
    for (int i = 0; i < callable_count; i++) {
        index[i] = -1;
        component_of[i] = -1;
        component_head[i] = -1;
    }

    int next_index = 0;
    int node_stack_count = 0;
    int component_count = 0;
    for (int root = 0; root < callable_count; root++) {
        if (index[root] >= 0) {
            continue;
        }
        int frame_count = 1;
        frames[0] = (scc_frame_t){.node = root, .parent = -1};
        while (frame_count > 0) {
            scc_frame_t *frame = &frames[frame_count - 1];
            int node = frame->node;
            if (index[node] < 0) {
                index[node] = next_index;
                low[node] = next_index;
                next_index++;
                node_stack[node_stack_count++] = node;
                on_stack[node] = true;
                cbm_gbuf_find_edges_by_source_type(gb, ids[node], "CALLS", &frame->edges,
                                                   &frame->edge_count);
            }

            bool descended = false;
            while (frame->next_edge < frame->edge_count) {
                int target = dense_index_for_id(ids, callable_count,
                                                frame->edges[frame->next_edge++]->target_id);
                if (target < 0) {
                    continue;
                }
                if (target == node) {
                    recursive[ids[node]] = true;
                    continue;
                }
                if (index[target] < 0) {
                    frames[frame_count++] = (scc_frame_t){.node = target, .parent = node};
                    descended = true;
                    break;
                }
                if (on_stack[target] && index[target] < low[node]) {
                    low[node] = index[target];
                }
            }
            if (descended) {
                continue;
            }

            int parent = frame->parent;
            if (low[node] == index[node]) {
                int member = -1;
                int member_count = 0;
                do {
                    member = node_stack[--node_stack_count];
                    on_stack[member] = false;
                    component_of[member] = component_count;
                    component_scratch[member_count++] = member;
                } while (member != node);
                if (member_count > 1) {
                    for (int i = 0; i < member_count; i++) {
                        recursive[ids[component_scratch[i]]] = true;
                    }
                }
                component_count++;
            }
            frame_count--;
            if (parent >= 0 && low[node] < low[parent]) {
                low[parent] = low[node];
            }
        }
    }

    for (int dense = 0; dense < callable_count; dense++) {
        int component = component_of[dense];
        next_member[dense] = component_head[component];
        component_head[component] = dense;
        int local_depth = loop_depth[ids[dense]];
        if (local_depth > component_local_depth[component]) {
            component_local_depth[component] = local_depth;
        }
    }
    for (int component = 0; component < component_count; component++) {
        component_tld_dfs(gb, ids, callable_count, component_of, component_head, next_member,
                          component_local_depth, component_tld, component_state, component, 0);
    }
    for (int dense = 0; dense < callable_count; dense++) {
        tld[ids[dense]] = component_tld[component_of[dense]];
    }

cleanup:
    free(ids);
    free(index);
    free(low);
    free(on_stack);
    free(node_stack);
    free(component_scratch);
    free(component_of);
    free(component_head);
    free(next_member);
    free(component_local_depth);
    free(component_tld);
    free(component_state);
    free(frames);
    return ok;
}

/* Seed each Function/Method node's loop_depth and self_recursive flag, and
 * remember the node pointer for write-back. The extraction seed and the SCC
 * pass jointly produce the final recursive flag. */
static int seed_loop_depths(const cbm_gbuf_t *gb, const char *label, int *loop_depth,
                            bool *recursive, cbm_gbuf_node_t **nptr, int64_t maxid) {
    const cbm_gbuf_node_t **nodes = NULL;
    int count = 0;
    if (cbm_gbuf_find_by_label(gb, label, &nodes, &count) != 0) {
        return 0;
    }
    int seeded = 0;
    for (int i = 0; i < count; i++) {
        const cbm_gbuf_node_t *n = nodes[i];
        if (n->id >= 1 && n->id <= maxid) {
            loop_depth[n->id] = json_get_int(n->properties_json, "loop_depth", 0);
            recursive[n->id] = json_get_bool(n->properties_json, "self_recursive");
            nptr[n->id] = (cbm_gbuf_node_t *)n;
            seeded++;
        }
    }
    return seeded;
}

void cbm_pipeline_pass_complexity(cbm_pipeline_ctx_t *ctx) {
    cbm_gbuf_t *gb = ctx->gbuf;
    /* Node and edge IDs are drawn from one shared counter, so node IDs are NOT
     * contiguous 1..node_count — they interleave with edge IDs. Size the lookup
     * arrays by the id ceiling (next_id) so every node id is addressable. */
    int64_t maxid = cbm_gbuf_next_id(gb) - 1;
    if (maxid < 1) {
        return;
    }
    size_t sz = (size_t)maxid + 1;
    int *loop_depth = calloc(sz, sizeof(int));
    int *tld = calloc(sz, sizeof(int));
    bool *recursive = calloc(sz, sizeof(bool));
    cbm_gbuf_node_t **nptr = calloc(sz, sizeof(cbm_gbuf_node_t *));
    if (!loop_depth || !tld || !recursive || !nptr) {
        free(loop_depth);
        free(tld);
        free(recursive);
        free(nptr);
        return;
    }

    int callable_count = seed_loop_depths(gb, "Function", loop_depth, recursive, nptr, maxid) +
                         seed_loop_depths(gb, "Method", loop_depth, recursive, nptr, maxid);
    if (!analyze_call_graph(gb, nptr, loop_depth, tld, recursive, maxid, callable_count)) {
        cbm_log_warn("pass.complexity.failed", "reason", "allocation_unavailable");
        free(loop_depth);
        free(tld);
        free(recursive);
        free(nptr);
        return;
    }

    int updated = 0;
    for (int64_t id = 1; id <= maxid; id++) {
        if (!nptr[id]) {
            continue; /* only Function/Method nodes */
        }
        append_complexity_props(nptr[id], tld[id], recursive[id]);
        updated++;
    }

    cbm_log_info("pass.complexity", "functions", itoa_cx(updated));

    free(loop_depth);
    free(tld);
    free(recursive);
    free(nptr);
}
