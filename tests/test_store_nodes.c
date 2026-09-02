/*
 * test_store_nodes.c — Tests for store schema, project CRUD, and node CRUD.
 *
 * Ported from internal/store/store_test.go (TestOpenMemory, TestNodeCRUD,
 * TestNodeDedup, TestProjectCRUD, TestUpsertNodeBatch, etc.)
 */
#include "test_framework.h"
#include "test_helpers.h"
#include <store/store.h>
#include <foundation/constants.h>
#include <foundation/compat.h>
#include <foundation/sha256.h>
#include <cbm.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int calls;
    int cancel_on_call;
} file_outline_cancel_probe_t;

static bool file_outline_cancel_probe(void *context) {
    file_outline_cancel_probe_t *probe = context;
    probe->calls++;
    return probe->calls >= probe->cancel_on_call;
}

/* ── Label allowlist / SQL drift guard ──────────────────────────── */

/* CONTRACT PIN. `cbm_label_is_type_like()` is documented in cbm.h as the single
 * source of truth for type-like labels, "instead of scattering
 * `|| strcmp(label,\"Struct\")==0` across the tree". A SQL string literal cannot
 * call it, so four queries in store.c and the BM25 ranking in mcp.c hardcoded
 * their own label lists — and silently stopped matching once Struct (Rust, Go,
 * Swift, D) began being emitted. get_architecture and vector search dropped
 * every struct in the project; search_code under-ranked them.
 *
 * This pins the SQL mirrors to the C predicate in BOTH directions, so the next
 * type-like label fails here instead of quietly shrinking query results. */
TEST(sql_label_allowlists_match_cbm_label_is_type_like) {
    /* Every label the C predicate accepts must appear in the SQL fragment. */
    static const char *const type_like[] = {"Class", "Struct", "Interface",
                                            "Enum",  "Type",   "Trait"};
    for (size_t i = 0; i < sizeof(type_like) / sizeof(type_like[0]); i++) {
        ASSERT_TRUE(cbm_label_is_type_like(type_like[i]));
        char quoted[64];
        snprintf(quoted, sizeof(quoted), "'%s'", type_like[i]);
        ASSERT_NOT_NULL(strstr(CBM_SQL_TYPE_LIKE_LABELS, quoted));
        ASSERT_NOT_NULL(strstr(CBM_SQL_CALLABLE_OR_TYPE_LABELS, quoted));
    }
    /* And nothing the predicate rejects may be smuggled into the type-like
     * fragment — otherwise the SQL would widen past the C contract. */
    static const char *const not_type_like[] = {"Function", "Method", "Module",
                                                "File",     "Folder", "Variable"};
    for (size_t i = 0; i < sizeof(not_type_like) / sizeof(not_type_like[0]); i++) {
        ASSERT_FALSE(cbm_label_is_type_like(not_type_like[i]));
        char quoted[64];
        snprintf(quoted, sizeof(quoted), "'%s'", not_type_like[i]);
        ASSERT_NULL(strstr(CBM_SQL_TYPE_LIKE_LABELS, quoted));
    }
    /* The callable fragment carries exactly Function and Method on top. */
    ASSERT_NOT_NULL(strstr(CBM_SQL_CALLABLE_OR_TYPE_LABELS, "'Function'"));
    ASSERT_NOT_NULL(strstr(CBM_SQL_CALLABLE_OR_TYPE_LABELS, "'Method'"));
    PASS();
}

/* Same drift guard for the relation labels (Table/View — SQL data lineage).
 * Relations are registry symbols but deliberately NOT type-like: the default
 * cbm_registry_resolve vetoes them, so a code identifier sharing a table's
 * name never binds into the lineage layer. */
TEST(sql_relation_labels_match_cbm_label_is_relation) {
    static const char *const relations[] = {"Table", "View", "Model"};
    for (size_t i = 0; i < sizeof(relations) / sizeof(relations[0]); i++) {
        ASSERT_TRUE(cbm_label_is_relation(relations[i]));
        ASSERT_TRUE(cbm_label_is_registry_symbol(relations[i]));
        ASSERT_FALSE(cbm_label_is_type_like(relations[i]));
        char quoted[64];
        snprintf(quoted, sizeof(quoted), "'%s'", relations[i]);
        ASSERT_NOT_NULL(strstr(CBM_SQL_RELATION_LABELS, quoted));
        /* Relations must NOT ride in the callable/type fragments — the arch
         * queries opt in explicitly by appending CBM_SQL_RELATION_LABELS. */
        ASSERT_NULL(strstr(CBM_SQL_CALLABLE_OR_TYPE_LABELS, quoted));
    }
    /* cbm_label_is_registry_symbol covers exactly the seeded families. */
    ASSERT_TRUE(cbm_label_is_registry_symbol("Function"));
    ASSERT_TRUE(cbm_label_is_registry_symbol("Method"));
    ASSERT_TRUE(cbm_label_is_registry_symbol("Class"));
    ASSERT_TRUE(cbm_label_is_registry_symbol("Variable"));
    ASSERT_TRUE(cbm_label_is_registry_symbol("Field"));
    ASSERT_FALSE(cbm_label_is_registry_symbol("Module"));
    ASSERT_FALSE(cbm_label_is_registry_symbol("File"));
    ASSERT_FALSE(cbm_label_is_relation("Class"));
    ASSERT_FALSE(cbm_label_is_relation(NULL));
    PASS();
}

/* ── Schema / Open / Close ──────────────────────────────────────── */

TEST(store_open_memory) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_close(s);
    PASS();
}

TEST(store_close_null) {
    cbm_store_close(NULL); /* should not crash */
    PASS();
}

TEST(store_open_memory_twice) {
    cbm_store_t *s1 = cbm_store_open_memory();
    cbm_store_t *s2 = cbm_store_open_memory();
    ASSERT_NOT_NULL(s1);
    ASSERT_NOT_NULL(s2);
    /* independent databases */
    cbm_store_close(s1);
    cbm_store_close(s2);
    PASS();
}

/* ── Project CRUD ───────────────────────────────────────────────── */

TEST(store_project_crud) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);

    /* Create */
    int rc = cbm_store_upsert_project(s, "myproject", "/home/user/myproject");
    ASSERT_EQ(rc, CBM_STORE_OK);

    /* Get */
    cbm_project_t p = {0};
    rc = cbm_store_get_project(s, "myproject", &p);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(p.name, "myproject");
    ASSERT_STR_EQ(p.root_path, "/home/user/myproject");
    ASSERT_NOT_NULL(p.indexed_at);
    cbm_project_free_fields(&p);

    /* List */
    cbm_project_t *projects = NULL;
    int count = 0;
    rc = cbm_store_list_projects(s, &projects, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(projects[0].name, "myproject");
    cbm_store_free_projects(projects, count);

    /* Get non-existent */
    cbm_project_t p2 = {0};
    rc = cbm_store_get_project(s, "nonexistent", &p2);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

TEST(store_project_update) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/old/path");

    /* Update root path */
    cbm_store_upsert_project(s, "test", "/new/path");

    cbm_project_t p = {0};
    cbm_store_get_project(s, "test", &p);
    ASSERT_STR_EQ(p.root_path, "/new/path");
    cbm_project_free_fields(&p);

    /* Should still be 1 project */
    cbm_project_t *projects = NULL;
    int count = 0;
    cbm_store_list_projects(s, &projects, &count);
    ASSERT_EQ(count, 1);
    cbm_store_free_projects(projects, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_project_delete) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    int rc = cbm_store_delete_project(s, "test");
    ASSERT_EQ(rc, CBM_STORE_OK);

    cbm_project_t p = {0};
    rc = cbm_store_get_project(s, "test", &p);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

/* ── Node CRUD ──────────────────────────────────────────────────── */

TEST(store_node_crud) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Insert node */
    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "Foo",
                    .qualified_name = "test.main.Foo",
                    .file_path = "main.go",
                    .start_line = 10,
                    .end_line = 20,
                    .properties_json = "{\"signature\":\"func Foo(x int) error\"}"};
    int64_t id = cbm_store_upsert_node(s, &n);
    ASSERT_GT(id, 0);

    /* Find by QN */
    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_qn(s, "test", "test.main.Foo", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.name, "Foo");
    ASSERT_STR_EQ(found.label, "Function");
    ASSERT_STR_EQ(found.file_path, "main.go");
    ASSERT_EQ(found.start_line, 10);
    ASSERT_EQ(found.end_line, 20);
    ASSERT_NOT_NULL(found.properties_json);
    cbm_node_free_fields(&found);

    /* Find by ID */
    cbm_node_t found2 = {0};
    rc = cbm_store_find_node_by_id(s, id, &found2);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found2.qualified_name, "test.main.Foo");
    cbm_node_free_fields(&found2);

    /* Find by name */
    cbm_node_t *nodes = NULL;
    int count = 0;
    rc = cbm_store_find_nodes_by_name(s, "test", "Foo", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(nodes[0].name, "Foo");
    cbm_store_free_nodes(nodes, count);

    /* Count */
    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 1);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_dedup) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Insert same QN twice — should update, not duplicate */
    cbm_node_t n1 = {
        .project = "test", .label = "Function", .name = "Foo", .qualified_name = "test.main.Foo"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "Foo",
                     .qualified_name = "test.main.Foo",
                     .properties_json = "{\"updated\":true}"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 1);

    /* Verify it was updated */
    cbm_node_t found = {0};
    cbm_store_find_node_by_qn(s, "test", "test.main.Foo", &found);
    ASSERT_NOT_NULL(found.properties_json);
    /* Should contain "updated" */
    ASSERT(strstr(found.properties_json, "updated") != NULL);
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_find_by_label) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {
        .project = "test", .label = "Function", .name = "A", .qualified_name = "test.A"};
    cbm_node_t n2 = {.project = "test", .label = "Class", .name = "B", .qualified_name = "test.B"};
    cbm_node_t n3 = {
        .project = "test", .label = "Function", .name = "C", .qualified_name = "test.C"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    cbm_store_upsert_node(s, &n3);

    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_label(s, "test", "Function", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    cbm_store_free_nodes(nodes, count);

    rc = cbm_store_find_nodes_by_label(s, "test", "Class", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(nodes[0].name, "B");
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_find_by_file) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "A",
                     .qualified_name = "test.A",
                     .file_path = "main.go"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "B",
                     .qualified_name = "test.B",
                     .file_path = "util.go"};
    cbm_node_t n3 = {.project = "test",
                     .label = "Function",
                     .name = "C",
                     .qualified_name = "test.C",
                     .file_path = "main.go"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    cbm_store_upsert_node(s, &n3);

    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_file(s, "test", "main.go", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_file_outline_is_filtered_stable_bounded_and_cancellable_issue469) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "outline", "/tmp/outline"), CBM_STORE_OK);

    cbm_node_t nodes[] = {
        {.project = "outline",
         .label = "Method",
         .name = "omega",
         .qualified_name = "outline.main.omega",
         .file_path = "main.c",
         .start_line = 30,
         .end_line = 33},
        {.project = "outline",
         .label = "Module",
         .name = "main",
         .qualified_name = "outline.main",
         .file_path = "main.c",
         .start_line = 1,
         .end_line = 80},
        {.project = "outline",
         .label = "Function",
         .name = "zeta",
         .qualified_name = "outline.main.zeta",
         .file_path = "main.c",
         .start_line = 10,
         .end_line = 20},
        {.project = "outline",
         .label = "Function",
         .name = "alpha",
         .qualified_name = "outline.main.alpha",
         .file_path = "main.c",
         .start_line = 10,
         .end_line = 15},
        {.project = "outline",
         .label = "Class",
         .name = "VisibleWithoutFilter",
         .qualified_name = "outline.main.VisibleWithoutFilter",
         .file_path = "main.c",
         .start_line = 5,
         .end_line = 40},
        {.project = "outline",
         .label = "Function",
         .name = "other",
         .qualified_name = "outline.other.other",
         .file_path = "other.c",
         .start_line = 1,
         .end_line = 2},
    };
    for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
        ASSERT_GT(cbm_store_upsert_node(s, &nodes[i]), 0);
    }

    static const char *const labels[] = {"Function", "Method"};
    cbm_file_outline_row_t *rows = NULL;
    int count = 0;
    int total = 0;
    ASSERT_EQ(cbm_store_get_file_outline(s, "outline", "main.c", labels, 2, 2, 0, NULL, NULL, &rows,
                                         &count, &total),
              CBM_STORE_OK);
    ASSERT_EQ(total, 3);
    ASSERT_EQ(count, 2);
    ASSERT_STR_EQ(rows[0].name, "alpha");
    ASSERT_STR_EQ(rows[1].name, "zeta");
    cbm_store_free_file_outline(rows, count);

    rows = NULL;
    count = 0;
    total = 0;
    ASSERT_EQ(cbm_store_get_file_outline(s, "outline", "main.c", labels, 2, 2, 2, NULL, NULL, &rows,
                                         &count, &total),
              CBM_STORE_OK);
    ASSERT_EQ(total, 3);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(rows[0].name, "omega");
    cbm_store_free_file_outline(rows, count);

    rows = NULL;
    count = 0;
    total = 0;
    ASSERT_EQ(cbm_store_get_file_outline(s, "outline", "main.c", NULL, 0, 10, 0, NULL, NULL, &rows,
                                         &count, &total),
              CBM_STORE_OK);
    ASSERT_EQ(total, 4);
    ASSERT_EQ(count, 4);
    ASSERT_STR_EQ(rows[0].name, "VisibleWithoutFilter");
    ASSERT_STR_EQ(rows[1].name, "alpha");
    ASSERT_STR_EQ(rows[2].name, "zeta");
    ASSERT_STR_EQ(rows[3].name, "omega");
    cbm_store_free_file_outline(rows, count);

    file_outline_cancel_probe_t probe = {.cancel_on_call = 3};
    rows = (cbm_file_outline_row_t *)(uintptr_t)1;
    count = 99;
    total = 99;
    ASSERT_EQ(cbm_store_get_file_outline(s, "outline", "main.c", labels, 2, 2, 0,
                                         file_outline_cancel_probe, &probe, &rows, &count, &total),
              CBM_STORE_CANCELLED);
    ASSERT_NULL(rows);
    ASSERT_EQ(count, 0);
    ASSERT_EQ(total, 0);
    ASSERT_TRUE(probe.calls >= 3);

    const char *empty_label[] = {""};
    ASSERT_EQ(cbm_store_get_file_outline(s, "outline", "main.c", empty_label, 1, 1, 0, NULL, NULL,
                                         &rows, &count, &total),
              CBM_STORE_ERR);
    ASSERT_EQ(cbm_store_get_file_outline(s, "outline", "main.c", NULL, 0,
                                         CBM_STORE_FILE_OUTLINE_MAX_LIMIT + 1, 0, NULL, NULL, &rows,
                                         &count, &total),
              CBM_STORE_ERR);

    cbm_store_close(s);
    PASS();
}

TEST(store_file_outline_fails_closed_on_text_budget_issue469) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "outline-budget", "/tmp/outline-budget"), CBM_STORE_OK);

    size_t qn_size = CBM_STORE_FILE_OUTLINE_MAX_TEXT_BYTES + 64U;
    char *large_qn = malloc(qn_size + 1U);
    ASSERT_NOT_NULL(large_qn);
    memset(large_qn, 'q', qn_size);
    large_qn[qn_size] = '\0';
    cbm_node_t node = {.project = "outline-budget",
                       .label = "Function",
                       .name = "oversized",
                       .qualified_name = large_qn,
                       .file_path = "large.c",
                       .start_line = 1,
                       .end_line = 2};
    ASSERT_GT(cbm_store_upsert_node(s, &node), 0);
    free(large_qn);

    cbm_file_outline_row_t *rows = (cbm_file_outline_row_t *)(uintptr_t)1;
    int count = 99;
    int total = 99;
    ASSERT_EQ(cbm_store_get_file_outline(s, "outline-budget", "large.c", NULL, 0, 1, 0, NULL, NULL,
                                         &rows, &count, &total),
              CBM_STORE_SCAN_LIMIT);
    ASSERT_NULL(rows);
    ASSERT_EQ(count, 0);
    ASSERT_EQ(total, 0);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_find_not_found) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_qn(s, "test", "nonexistent", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    rc = cbm_store_find_node_by_id(s, 99999, &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_count_empty) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 0);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_delete_by_file) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "A",
                     .qualified_name = "test.A",
                     .file_path = "main.go"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "B",
                     .qualified_name = "test.B",
                     .file_path = "util.go"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    cbm_store_delete_nodes_by_file(s, "test", "main.go");
    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 1);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_delete_by_label) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {
        .project = "test", .label = "Function", .name = "A", .qualified_name = "test.A"};
    cbm_node_t n2 = {.project = "test", .label = "Class", .name = "B", .qualified_name = "test.B"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    cbm_store_delete_nodes_by_label(s, "test", "Function");
    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 1);

    cbm_store_close(s);
    PASS();
}

/* ── Batch operations ───────────────────────────────────────────── */

TEST(store_node_batch_upsert) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Create 150 nodes */
    cbm_node_t nodes[150];
    int64_t ids[150];
    char names[150][32];
    char qns[150][64];

    for (int i = 0; i < 150; i++) {
        snprintf(names[i], sizeof(names[i]), "func_%d", i);
        snprintf(qns[i], sizeof(qns[i]), "test.pkg.func_%d", i);
        nodes[i] = (cbm_node_t){
            .project = "test",
            .label = "Function",
            .name = names[i],
            .qualified_name = qns[i],
            .file_path = "pkg.go",
            .start_line = i * 10,
            .end_line = i * 10 + 9,
        };
    }

    int rc = cbm_store_upsert_node_batch(s, nodes, 150, ids);
    ASSERT_EQ(rc, CBM_STORE_OK);

    /* Verify all IDs are non-zero */
    for (int i = 0; i < 150; i++) {
        ASSERT_GT(ids[i], 0);
    }

    /* Verify count */
    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 150);

    /* Re-upsert should not duplicate */
    int64_t ids2[150];
    rc = cbm_store_upsert_node_batch(s, nodes, 150, ids2);
    ASSERT_EQ(rc, CBM_STORE_OK);
    cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 150);

    /* IDs should be the same */
    for (int i = 0; i < 150; i++) {
        ASSERT_EQ(ids[i], ids2[i]);
    }

    cbm_store_close(s);
    PASS();
}

TEST(store_node_batch_empty) {
    cbm_store_t *s = cbm_store_open_memory();
    int rc = cbm_store_upsert_node_batch(s, NULL, 0, NULL);
    ASSERT_EQ(rc, CBM_STORE_OK);
    cbm_store_close(s);
    PASS();
}

/* ── Cascade delete ─────────────────────────────────────────────── */

TEST(store_cascade_delete) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Create nodes and an edge */
    cbm_node_t n1 = {
        .project = "test", .label = "Function", .name = "A", .qualified_name = "test.A"};
    cbm_node_t n2 = {
        .project = "test", .label = "Function", .name = "B", .qualified_name = "test.B"};
    int64_t id1 = cbm_store_upsert_node(s, &n1);
    int64_t id2 = cbm_store_upsert_node(s, &n2);

    cbm_edge_t e = {.project = "test", .source_id = id1, .target_id = id2, .type = "CALLS"};
    cbm_store_insert_edge(s, &e);

    /* Delete project — should cascade */
    cbm_store_delete_project(s, "test");

    int ncnt = cbm_store_count_nodes(s, "test");
    int ecnt = cbm_store_count_edges(s, "test");
    ASSERT_EQ(ncnt, 0);
    ASSERT_EQ(ecnt, 0);

    cbm_store_close(s);
    PASS();
}

/* ── File hashes ────────────────────────────────────────────────── */

TEST(store_file_hash_crud) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Upsert */
    int rc = cbm_store_upsert_file_hash(s, "test", "main.go", "abc123", 1000000, 512);
    ASSERT_EQ(rc, CBM_STORE_OK);

    /* Get */
    cbm_file_hash_t *hashes = NULL;
    int count = 0;
    rc = cbm_store_get_file_hashes(s, "test", &hashes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(hashes[0].rel_path, "main.go");
    ASSERT_STR_EQ(hashes[0].sha256, "abc123");
    ASSERT_EQ(hashes[0].mtime_ns, 1000000);
    ASSERT_EQ(hashes[0].size, 512);
    cbm_store_free_file_hashes(hashes, count);

    /* Update */
    rc = cbm_store_upsert_file_hash(s, "test", "main.go", "def456", 2000000, 1024);
    ASSERT_EQ(rc, CBM_STORE_OK);
    rc = cbm_store_get_file_hashes(s, "test", &hashes, &count);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(hashes[0].sha256, "def456");
    ASSERT_EQ(hashes[0].mtime_ns, 2000000);
    cbm_store_free_file_hashes(hashes, count);

    /* Delete single */
    rc = cbm_store_delete_file_hash(s, "test", "main.go");
    ASSERT_EQ(rc, CBM_STORE_OK);
    rc = cbm_store_get_file_hashes(s, "test", &hashes, &count);
    ASSERT_EQ(count, 0);
    cbm_store_free_file_hashes(hashes, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_lsp_surface_round_trip) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Empty project: OK + zero rows — the "no surface data, route to full
     * rebuild" signal, and the upgrade path for pre-table databases. */
    cbm_lsp_surface_row_t *rows = NULL;
    int count = CBM_NOT_FOUND;
    ASSERT_EQ(cbm_store_get_lsp_surfaces(s, "test", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_lsp_surfaces(rows, count);

    /* Batch with the field shapes the codec will produce: a real defs array,
     * an empty surface, a binary bloom (with embedded NUL), and no bloom. */
    const unsigned char bloom[] = {0x01, 0x00, 0xfe, 0x7f, 0x00, 0xab};
    cbm_lsp_surface_row_t in[] = {
        {.project = "test",
         .rel_path = "pkg/a.go",
         .surface_sha = "sha-a",
         .defs_json = "[{\"qn\":\"pkg.A\",\"sn\":\"A\",\"label\":\"Function\"}]",
         .ref_bloom = bloom,
         .ref_bloom_len = (int)sizeof(bloom),
         .config_ctx = "cfg-1"},
        {.project = "test",
         .rel_path = "pkg/b.go",
         .surface_sha = "sha-b",
         .defs_json = "[]",
         .ref_bloom = NULL,
         .ref_bloom_len = 0,
         .config_ctx = ""},
    };
    ASSERT_EQ(cbm_store_upsert_lsp_surface_batch(s, in, 2), CBM_STORE_OK);

    ASSERT_EQ(cbm_store_get_lsp_surfaces(s, "test", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    /* ORDER BY rel_path: a.go before b.go. */
    ASSERT_STR_EQ(rows[0].rel_path, "pkg/a.go");
    ASSERT_STR_EQ(rows[0].surface_sha, "sha-a");
    ASSERT_STR_EQ(rows[0].defs_json, "[{\"qn\":\"pkg.A\",\"sn\":\"A\",\"label\":\"Function\"}]");
    ASSERT_STR_EQ(rows[0].config_ctx, "cfg-1");
    ASSERT_EQ(rows[0].ref_bloom_len, (int)sizeof(bloom));
    ASSERT_TRUE(rows[0].ref_bloom != NULL && memcmp(rows[0].ref_bloom, bloom, sizeof(bloom)) == 0);
    ASSERT_STR_EQ(rows[1].rel_path, "pkg/b.go");
    ASSERT_STR_EQ(rows[1].defs_json, "[]");
    ASSERT_EQ(rows[1].ref_bloom_len, 0);
    ASSERT_TRUE(rows[1].ref_bloom == NULL);
    cbm_store_free_lsp_surfaces(rows, count);

    /* Upsert-on-conflict replaces the whole row, including dropping a bloom. */
    cbm_lsp_surface_row_t update = {.project = "test",
                                    .rel_path = "pkg/a.go",
                                    .surface_sha = "sha-a2",
                                    .defs_json = "[]",
                                    .ref_bloom = NULL,
                                    .ref_bloom_len = 0,
                                    .config_ctx = ""};
    ASSERT_EQ(cbm_store_upsert_lsp_surface_batch(s, &update, 1), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_get_lsp_surfaces(s, "test", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    ASSERT_STR_EQ(rows[0].surface_sha, "sha-a2");
    ASSERT_EQ(rows[0].ref_bloom_len, 0);
    cbm_store_free_lsp_surfaces(rows, count);

    /* Project-scoped delete removes everything. */
    ASSERT_EQ(cbm_store_delete_lsp_surfaces(s, "test"), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_get_lsp_surfaces(s, "test", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_lsp_surfaces(rows, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_dependent_files_lookup) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* a.py and c.py each call into b.py; b.py also references itself, and
     * d.py sits apart. Dependents of {b.py} must be exactly {a.py, c.py}:
     * the self-reference is not a dependent, d.py is unrelated. */
    const char *files[] = {"a.py", "b.py", "c.py", "d.py"};
    int64_t ids[4] = {0};
    for (int i = 0; i < 4; i++) {
        char qn[64];
        snprintf(qn, sizeof(qn), "test.%s.fn", files[i]);
        cbm_node_t n = {.project = "test",
                        .label = "Function",
                        .name = "fn",
                        .qualified_name = qn,
                        .file_path = files[i],
                        .start_line = 1,
                        .end_line = 2,
                        .properties_json = "{}"};
        ids[i] = cbm_store_upsert_node(s, &n);
        ASSERT_TRUE(ids[i] > 0);
    }
    const int64_t edge_pairs[][2] = {{ids[0], ids[1]}, {ids[2], ids[1]}, {ids[1], ids[1]}};
    for (int i = 0; i < 3; i++) {
        cbm_edge_t e = {.project = "test",
                        .source_id = edge_pairs[i][0],
                        .target_id = edge_pairs[i][1],
                        .type = "CALLS",
                        .properties_json = "{}"};
        ASSERT_TRUE(cbm_store_insert_edge(s, &e) > 0);
    }

    /* Structural container noise: a Folder node (placeholder file_path)
     * with a CONTAINS_FILE edge into b.py must never surface as a
     * dependent — it is not a source file and cannot be re-resolved. */
    cbm_node_t folder = {.project = "test",
                         .label = "Folder",
                         .name = "pkg",
                         .qualified_name = "test.pkg",
                         .file_path = "{}",
                         .start_line = 0,
                         .end_line = 0,
                         .properties_json = "{}"};
    int64_t folder_id = cbm_store_upsert_node(s, &folder);
    ASSERT_TRUE(folder_id > 0);
    cbm_edge_t contains = {.project = "test",
                           .source_id = folder_id,
                           .target_id = ids[1],
                           .type = "CONTAINS_FILE",
                           .properties_json = "{}"};
    ASSERT_TRUE(cbm_store_insert_edge(s, &contains) > 0);

    const char *targets[] = {"b.py"};
    char **deps = NULL;
    int dep_count = 0;
    ASSERT_EQ(cbm_store_get_dependent_files(s, "test", targets, 1, &deps, &dep_count),
              CBM_STORE_OK);
    ASSERT_EQ(dep_count, 2);
    bool saw_a = false;
    bool saw_c = false;
    for (int i = 0; i < dep_count; i++) {
        saw_a = saw_a || strcmp(deps[i], "a.py") == 0;
        saw_c = saw_c || strcmp(deps[i], "c.py") == 0;
    }
    ASSERT_TRUE(saw_a && saw_c);
    cbm_store_free_dependent_files(deps, dep_count);

    /* No inbound edges: an isolated target has no dependents. */
    const char *targets_d[] = {"d.py"};
    ASSERT_EQ(cbm_store_get_dependent_files(s, "test", targets_d, 1, &deps, &dep_count),
              CBM_STORE_OK);
    ASSERT_EQ(dep_count, 0);
    cbm_store_free_dependent_files(deps, dep_count);

    /* A target list already containing the dependent excludes it: asking for
     * dependents of {a.py, b.py} returns only c.py. */
    const char *targets_ab[] = {"a.py", "b.py"};
    ASSERT_EQ(cbm_store_get_dependent_files(s, "test", targets_ab, 2, &deps, &dep_count),
              CBM_STORE_OK);
    ASSERT_EQ(dep_count, 1);
    ASSERT_STR_EQ(deps[0], "c.py");
    cbm_store_free_dependent_files(deps, dep_count);

    cbm_store_close(s);
    PASS();
}

TEST(store_file_hash_upsert_rejects_null_required_fields) {
    /* Pins the API contract that `cbm_store_upsert_file_hash` returns
     * CBM_STORE_ERR (not silent OK) when a NOT NULL column would receive
     * SQL NULL. This is the failure mode that
     * `pipeline_incremental.c:persist_hashes` checks for and logs as
     * `incremental.persist_hash_failed`. If this contract ever changes
     * (e.g. the schema relaxes NOT NULL on rel_path or sha256), the
     * downstream warning becomes silent and the orphaned-node bug class
     * can re-emerge. Track that change here, not just in the consumer. */
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Sanity: a fully-valid upsert returns OK. */
    int rc = cbm_store_upsert_file_hash(s, "test", "main.go", "abc123", 1000000, 512);
    ASSERT_EQ(rc, CBM_STORE_OK);

    /* NULL sha256 violates NOT NULL on file_hashes.sha256 → must return ERR. */
    rc = cbm_store_upsert_file_hash(s, "test", "other.go", NULL, 2000000, 1024);
    ASSERT_EQ(rc, CBM_STORE_ERR);

    /* NULL rel_path violates NOT NULL on file_hashes.rel_path → must return ERR. */
    rc = cbm_store_upsert_file_hash(s, "test", NULL, "deadbeef", 3000000, 2048);
    ASSERT_EQ(rc, CBM_STORE_ERR);

    /* NULL project violates NOT NULL on file_hashes.project → must return ERR. */
    rc = cbm_store_upsert_file_hash(s, NULL, "third.go", "cafebabe", 4000000, 4096);
    ASSERT_EQ(rc, CBM_STORE_ERR);

    /* The valid row from earlier must still be present — partial-failure
     * policy: a single bad upsert does not corrupt or remove other rows. */
    cbm_file_hash_t *hashes = NULL;
    int count = 0;
    cbm_store_get_file_hashes(s, "test", &hashes, &count);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(hashes[0].rel_path, "main.go");
    cbm_store_free_file_hashes(hashes, count);

    cbm_store_close(s);
    PASS();
}

/* ── Properties JSON round-trip ─────────────────────────────────── */

TEST(store_node_properties_json) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "Bar",
                    .qualified_name = "test.Bar",
                    .properties_json = "{\"visibility\":\"public\",\"is_entry_point\":true}"};
    cbm_store_upsert_node(s, &n);

    cbm_node_t found = {0};
    cbm_store_find_node_by_qn(s, "test", "test.Bar", &found);
    ASSERT_NOT_NULL(found.properties_json);
    ASSERT(strstr(found.properties_json, "visibility") != NULL);
    ASSERT(strstr(found.properties_json, "public") != NULL);
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_null_properties) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* NULL properties should default to "{}" */
    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "Baz",
                    .qualified_name = "test.Baz",
                    .properties_json = NULL};
    cbm_store_upsert_node(s, &n);

    cbm_node_t found = {0};
    cbm_store_find_node_by_qn(s, "test", "test.Baz", &found);
    ASSERT_NOT_NULL(found.properties_json);
    ASSERT_STR_EQ(found.properties_json, "{}");
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

/* ── File overlap ───────────────────────────────────────────────── */

TEST(store_find_by_file_overlap) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t na = {.project = "test",
                     .label = "Function",
                     .name = "funcA",
                     .qualified_name = "test.main.funcA",
                     .file_path = "main.go",
                     .start_line = 1,
                     .end_line = 10};
    cbm_node_t nb = {.project = "test",
                     .label = "Function",
                     .name = "funcB",
                     .qualified_name = "test.main.funcB",
                     .file_path = "main.go",
                     .start_line = 12,
                     .end_line = 25};
    cbm_node_t nc = {.project = "test",
                     .label = "Function",
                     .name = "funcC",
                     .qualified_name = "test.main.funcC",
                     .file_path = "other.go",
                     .start_line = 1,
                     .end_line = 50};
    /* Module node should be excluded from overlap results */
    cbm_node_t nm = {.project = "test",
                     .label = "Module",
                     .name = "main",
                     .qualified_name = "test.main",
                     .file_path = "main.go",
                     .start_line = 1,
                     .end_line = 100};
    cbm_store_upsert_node(s, &na);
    cbm_store_upsert_node(s, &nb);
    cbm_store_upsert_node(s, &nc);
    cbm_store_upsert_node(s, &nm);

    /* Overlap with funcA (lines 5-8) */
    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_file_overlap(s, "test", "main.go", 5, 8, &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(nodes[0].name, "funcA");
    cbm_store_free_nodes(nodes, count);

    /* Overlap spanning funcA and funcB (lines 8-15) */
    rc = cbm_store_find_nodes_by_file_overlap(s, "test", "main.go", 8, 15, &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    cbm_store_free_nodes(nodes, count);

    /* No overlap (lines 26-30) */
    rc = cbm_store_find_nodes_by_file_overlap(s, "test", "main.go", 26, 30, &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_nodes(nodes, count);

    /* Different file */
    rc = cbm_store_find_nodes_by_file_overlap(s, "test", "other.go", 1, 50, &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(nodes[0].name, "funcC");
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

/* ── QN suffix ─────────────────────────────────────────────────── */

TEST(store_find_by_qn_suffix_single) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "HandleRequest",
                    .qualified_name = "test.cmd.server.main.HandleRequest"};
    cbm_store_upsert_node(s, &n);

    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_qn_suffix(s, "test", "main.HandleRequest", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(nodes[0].name, "HandleRequest");
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_find_by_qn_suffix_no_match) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n = {
        .project = "test", .label = "Function", .name = "Foo", .qualified_name = "test.main.Foo"};
    cbm_store_upsert_node(s, &n);

    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_qn_suffix(s, "test", "main.Bar", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_find_by_qn_suffix_multiple) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "Run",
                     .qualified_name = "test.cmd.server.Run"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "Run",
                     .qualified_name = "test.cmd.worker.Run"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_qn_suffix(s, "test", "Run", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

TEST(store_find_by_qn_suffix_dot_boundary) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "HandleRequest",
                     .qualified_name = "test.main.HandleRequest"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "MyHandleRequestHelper",
                     .qualified_name = "test.main.MyHandleRequestHelper"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    /* Should only match the one with ".HandleRequest" suffix, not partial word */
    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_qn_suffix(s, "test", "HandleRequest", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(nodes[0].name, "HandleRequest");
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

/* ── Node degree ───────────────────────────────────────────────── */

TEST(store_node_degree) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t na = {
        .project = "test", .label = "Function", .name = "A", .qualified_name = "test.A"};
    cbm_node_t nb = {
        .project = "test", .label = "Function", .name = "B", .qualified_name = "test.B"};
    cbm_node_t nc = {
        .project = "test", .label = "Function", .name = "C", .qualified_name = "test.C"};
    int64_t idA = cbm_store_upsert_node(s, &na);
    int64_t idB = cbm_store_upsert_node(s, &nb);
    int64_t idC = cbm_store_upsert_node(s, &nc);

    /* A->B (CALLS), A->C (CALLS), B->C (CALLS), A->C (USAGE — not counted) */
    cbm_edge_t e1 = {.project = "test", .source_id = idA, .target_id = idB, .type = "CALLS"};
    cbm_edge_t e2 = {.project = "test", .source_id = idA, .target_id = idC, .type = "CALLS"};
    cbm_edge_t e3 = {.project = "test", .source_id = idB, .target_id = idC, .type = "CALLS"};
    cbm_edge_t e4 = {.project = "test", .source_id = idA, .target_id = idC, .type = "USAGE"};
    cbm_store_insert_edge(s, &e1);
    cbm_store_insert_edge(s, &e2);
    cbm_store_insert_edge(s, &e3);
    cbm_store_insert_edge(s, &e4);

    int inA, outA, inB, outB, inC, outC;
    cbm_store_node_degree(s, idA, &inA, &outA);
    ASSERT_EQ(inA, 0);
    ASSERT_EQ(outA, 2);

    cbm_store_node_degree(s, idB, &inB, &outB);
    ASSERT_EQ(inB, 1);
    ASSERT_EQ(outB, 1);

    cbm_store_node_degree(s, idC, &inC, &outC);
    ASSERT_EQ(inC, 2);
    ASSERT_EQ(outC, 0);

    cbm_store_close(s);
    PASS();
}

/* ── File hash batch ───────────────────────────────────────────── */

TEST(store_file_hash_batch) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_file_hash_t hashes[3] = {
        {.project = "test", .rel_path = "a.go", .sha256 = "h1", .mtime_ns = 1000, .size = 100},
        {.project = "test", .rel_path = "b.go", .sha256 = "h2", .mtime_ns = 2000, .size = 200},
        {.project = "test", .rel_path = "c.go", .sha256 = "h3", .mtime_ns = 3000, .size = 300},
    };
    int rc = cbm_store_upsert_file_hash_batch(s, hashes, 3);
    ASSERT_EQ(rc, CBM_STORE_OK);

    cbm_file_hash_t *stored = NULL;
    int count = 0;
    rc = cbm_store_get_file_hashes(s, "test", &stored, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 3);
    cbm_store_free_file_hashes(stored, count);

    /* Update hashes (should not duplicate) */
    hashes[0].sha256 = "updated";
    hashes[0].mtime_ns = 9000;
    rc = cbm_store_upsert_file_hash_batch(s, hashes, 3);
    ASSERT_EQ(rc, CBM_STORE_OK);

    rc = cbm_store_get_file_hashes(s, "test", &stored, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 3);
    /* Verify updated value */
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(stored[i].rel_path, "a.go") == 0) {
            ASSERT_STR_EQ(stored[i].sha256, "updated");
            ASSERT_EQ(stored[i].mtime_ns, 9000);
            found = 1;
        }
    }
    ASSERT_TRUE(found);
    cbm_store_free_file_hashes(stored, count);

    cbm_store_close(s);
    PASS();
}

/* Guard for the persist-tail switch (pipeline.c dump_and_persist_hashes) from a
 * per-file cbm_store_upsert_file_hash loop to a single cbm_store_upsert_file_hash_batch:
 * both paths must yield IDENTICAL file_hashes rows (same tuples, same upsert/replace
 * semantics — only the transaction boundary differs). Uses sha256="" exactly as the
 * persist path does. */
TEST(store_file_hash_batch_equals_loop) {
    cbm_file_hash_t rows[4] = {
        {.project = "p", .rel_path = "a.c", .sha256 = "", .mtime_ns = 111, .size = 10},
        {.project = "p", .rel_path = "b/c.c", .sha256 = "", .mtime_ns = 222, .size = 20},
        {.project = "p", .rel_path = "d.rs", .sha256 = "", .mtime_ns = 333, .size = 30},
        {.project = "p", .rel_path = "e.py", .sha256 = "", .mtime_ns = 444, .size = 40},
    };

    /* Store A: the original per-file loop. */
    cbm_store_t *a = cbm_store_open_memory();
    cbm_store_upsert_project(a, "p", "/tmp/p");
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(cbm_store_upsert_file_hash(a, rows[i].project, rows[i].rel_path, rows[i].sha256,
                                             rows[i].mtime_ns, rows[i].size),
                  CBM_STORE_OK);
    }

    /* Store B: the batched path. */
    cbm_store_t *b = cbm_store_open_memory();
    cbm_store_upsert_project(b, "p", "/tmp/p");
    ASSERT_EQ(cbm_store_upsert_file_hash_batch(b, rows, 4), CBM_STORE_OK);

    cbm_file_hash_t *ha = NULL, *hb = NULL;
    int ca = 0, cb = 0;
    ASSERT_EQ(cbm_store_get_file_hashes(a, "p", &ha, &ca), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_get_file_hashes(b, "p", &hb, &cb), CBM_STORE_OK);
    ASSERT_EQ(ca, 4);
    ASSERT_EQ(cb, 4);

    /* Compare as sets (get_file_hashes has no ORDER BY). */
    for (int i = 0; i < ca; i++) {
        int found = 0;
        for (int j = 0; j < cb; j++) {
            if (strcmp(ha[i].rel_path, hb[j].rel_path) == 0) {
                ASSERT_STR_EQ(ha[i].sha256, hb[j].sha256);
                ASSERT_EQ(ha[i].mtime_ns, hb[j].mtime_ns);
                ASSERT_EQ(ha[i].size, hb[j].size);
                found = 1;
                break;
            }
        }
        ASSERT_TRUE(found);
    }

    cbm_store_free_file_hashes(ha, ca);
    cbm_store_free_file_hashes(hb, cb);
    cbm_store_close(a);
    cbm_store_close(b);
    PASS();
}

/* ── Find edges by URL path ────────────────────────────────────── */

TEST(store_find_edges_by_url_path) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t ns = {
        .project = "test", .label = "Function", .name = "caller", .qualified_name = "test.caller"};
    cbm_node_t nt = {.project = "test",
                     .label = "Function",
                     .name = "handler",
                     .qualified_name = "test.handler"};
    int64_t srcID = cbm_store_upsert_node(s, &ns);
    int64_t tgtID = cbm_store_upsert_node(s, &nt);

    cbm_edge_t e = {.project = "test",
                    .source_id = srcID,
                    .target_id = tgtID,
                    .type = "HTTP_CALLS",
                    .properties_json = "{\"url_path\":\"/api/orders/create\",\"confidence\":0.8}"};
    cbm_store_insert_edge(s, &e);

    /* Search for edges containing "orders" */
    cbm_edge_t *edges = NULL;
    int count = 0;
    int rc = cbm_store_find_edges_by_url_path(s, "test", "orders", &edges, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT(strstr(edges[0].properties_json, "/api/orders/create") != NULL);
    cbm_store_free_edges(edges, count);

    /* Search for non-matching */
    rc = cbm_store_find_edges_by_url_path(s, "test", "users", &edges, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_edges(edges, count);

    cbm_store_close(s);
    PASS();
}

/* ── Restore from ──────────────────────────────────────────────── */

TEST(store_restore_from) {
    /* Create in-memory source store with data */
    cbm_store_t *src = cbm_store_open_memory();
    cbm_store_upsert_project(src, "test", "/tmp/test");
    for (int i = 0; i < 10; i++) {
        char name[32], qn[64];
        snprintf(name, sizeof(name), "Func%d", i);
        snprintf(qn, sizeof(qn), "test.main.Func%d", i);
        cbm_node_t n = {.project = "test",
                        .label = "Function",
                        .name = name,
                        .qualified_name = qn,
                        .file_path = "main.go",
                        .start_line = i * 10,
                        .end_line = i * 10 + 5};
        cbm_store_upsert_node(src, &n);
    }

    /* Create destination store */
    cbm_store_t *dst = cbm_store_open_memory();

    /* Restore: copy from src → dst */
    int rc = cbm_store_restore_from(dst, src);
    ASSERT_EQ(rc, CBM_STORE_OK);

    /* Verify data survived */
    cbm_node_t found = {0};
    rc = cbm_store_find_node_by_qn(dst, "test", "test.main.Func5", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.name, "Func5");
    cbm_node_free_fields(&found);

    int cnt = cbm_store_count_nodes(dst, "test");
    ASSERT_EQ(cnt, 10);

    cbm_store_close(src);
    cbm_store_close(dst);
    PASS();
}

/* ── Pragma settings ───────────────────────────────────────────── */

TEST(store_pragma_settings) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    /* Just verify we can open and the store works — pragma settings
     * are verified by the fact that the store functions correctly. */
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t n = {
        .project = "test", .label = "Function", .name = "X", .qualified_name = "test.X"};
    int64_t id = cbm_store_upsert_node(s, &n);
    ASSERT_TRUE(id > 0);
    cbm_store_close(s);
    PASS();
}

TEST(store_find_node_ids_by_qns) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Insert two nodes */
    cbm_node_t na = {
        .project = "test", .label = "Function", .name = "A", .qualified_name = "test.A"};
    cbm_node_t nb = {
        .project = "test", .label = "Function", .name = "B", .qualified_name = "test.B"};
    int64_t id1 = cbm_store_upsert_node(s, &na);
    int64_t id2 = cbm_store_upsert_node(s, &nb);
    ASSERT_TRUE(id1 > 0);
    ASSERT_TRUE(id2 > 0);

    /* Batch lookup: 2 found + 1 missing */
    const char *qns[] = {"test.A", "test.B", "test.missing"};
    int64_t ids[3];
    int found = cbm_store_find_node_ids_by_qns(s, "test", qns, 3, ids);
    ASSERT_EQ(found, 2);
    ASSERT_EQ(ids[0], id1);
    ASSERT_EQ(ids[1], id2);
    ASSERT_EQ(ids[2], 0); /* missing → 0 */

    /* Empty batch */
    int found2 = cbm_store_find_node_ids_by_qns(s, "test", NULL, 0, ids);
    ASSERT_EQ(found2, 0);

    cbm_store_close(s);
    PASS();
}

/* ── Integrity check tests ──────────────────────────────────────── */

TEST(store_integrity_clean) {
    /* A fresh store with correct data should pass integrity check */
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "test-proj", "/tmp/test");
    ASSERT_TRUE(cbm_store_check_integrity(s));
    cbm_store_close(s);
    PASS();
}

TEST(store_integrity_empty) {
    /* An empty store (no project rows) should pass — 0 rows is fine */
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(cbm_store_check_integrity(s));
    cbm_store_close(s);
    PASS();
}

TEST(store_integrity_corrupt_bad_path) {
    /* Simulate corruption: root_path is a numeric string (not a real path).
     * This matches the real corruption where node IDs ended up in root_path. */
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    sqlite3 *db = cbm_store_get_db(s);
    sqlite3_exec(db,
                 "INSERT INTO projects (name, indexed_at, root_path) "
                 "VALUES ('some-project', '2024-01-01', '826');",
                 NULL, NULL, NULL);
    ASSERT_FALSE(cbm_store_check_integrity(s));
    cbm_store_close(s);
    PASS();
}

TEST(store_integrity_windows_lowercase_drive_issue367) {
    /* Windows drive letters may be lower- or upper-case; a lowercase drive
     * path must NOT be treated as corrupt. Previously the check only accepted
     * 'A'..'Z', so "c:/repo" was flagged and the DB auto-deleted (#227/#367). */
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "lc-drive", "c:/Users/dev/repo");
    ASSERT_TRUE(cbm_store_check_integrity(s));
    cbm_store_close(s);
    PASS();
}

TEST(store_integrity_corrupt_too_many_rows) {
    /* Simulate corruption: >5 rows in projects table */
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    sqlite3 *db = cbm_store_get_db(s);
    for (int i = 0; i < 10; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO projects (name, indexed_at, root_path) "
                 "VALUES ('proj-%d', '2024-01-01', '/tmp/%d');",
                 i, i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    ASSERT_FALSE(cbm_store_check_integrity(s));
    cbm_store_close(s);
    PASS();
}

/* ── Quarantine verdict (#1206, #1037) ──────────────────────────────
 *
 * The bool check above cannot answer the only question the quarantine path
 * actually asks: "is this database damaged, or did I just lose a lock race?"
 * Answering "damaged" to the second question is what made concurrent instances
 * rename each other's HEALTHY databases to .corrupt (#1206). These bind the
 * three-way verdict so that behaviour cannot come back. */

TEST(store_integrity_verdict_healthy_is_ok) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "healthy-proj", "/tmp/healthy");
    ASSERT_EQ(cbm_store_check_integrity_verdict(s), CBM_INTEGRITY_OK);
    cbm_store_close(s);
    PASS();
}

TEST(store_integrity_verdict_real_corruption_is_corrupt) {
    /* Structural damage the shallow check can see: node IDs landing in
     * root_path. This one MUST be quarantinable — a verdict that never says
     * CORRUPT would protect broken databases instead of users. */
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    sqlite3 *db = cbm_store_get_db(s);
    sqlite3_exec(db,
                 "INSERT INTO projects (name, indexed_at, root_path) "
                 "VALUES ('broken', '2024-01-01', '826');",
                 NULL, NULL, NULL);
    ASSERT_EQ(cbm_store_check_integrity_verdict(s), CBM_INTEGRITY_CORRUPT);
    cbm_store_close(s);
    PASS();
}

TEST(store_integrity_verdict_unopenable_is_transient_not_corrupt) {
    /* A handle we could not open tells us NOTHING about the file's contents.
     * Reporting CORRUPT here is how a database nobody could read got renamed
     * and rebuilt from scratch (#1206) — the destructive answer to a question
     * that was never asked. */
    ASSERT_EQ(cbm_store_check_integrity_verdict(NULL), CBM_INTEGRITY_TRANSIENT);
    PASS();
}

TEST(store_integrity_null_check) {
    /* NULL store should return false (not crash) */
    ASSERT_FALSE(cbm_store_check_integrity(NULL));
    PASS();
}

/* ── Edge case: NULL / empty field handling ────────────────────── */

TEST(store_node_null_project) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);

    /* Upsert with NULL project — should fail gracefully */
    cbm_node_t n = {
        .project = NULL, .label = "Function", .name = "Foo", .qualified_name = "null.Foo"};
    int64_t id = cbm_store_upsert_node(s, &n);
    /* Either returns error or silently succeeds; must not crash */
    (void)id;

    cbm_store_close(s);
    PASS();
}

TEST(store_node_null_qn) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Upsert with NULL qualified_name */
    cbm_node_t n = {.project = "test", .label = "Function", .name = "Bar", .qualified_name = NULL};
    int64_t id = cbm_store_upsert_node(s, &n);
    /* Must not crash regardless of return value */
    (void)id;

    cbm_store_close(s);
    PASS();
}

TEST(store_node_empty_strings) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Upsert with all fields as empty strings */
    cbm_node_t n = {.project = "test",
                    .label = "",
                    .name = "",
                    .qualified_name = "",
                    .file_path = "",
                    .start_line = 0,
                    .end_line = 0,
                    .properties_json = ""};
    int64_t id = cbm_store_upsert_node(s, &n);
    /* Should succeed — empty strings are valid */
    ASSERT_GT(id, 0);

    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_qn(s, "test", "", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.name, "");
    ASSERT_STR_EQ(found.label, "");
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: not-found lookups ──────────────────────────────── */

TEST(store_find_by_id_not_found) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_id(s, 999999, &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    /* Negative ID should also be not found */
    rc = cbm_store_find_node_by_id(s, -1, &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

TEST(store_find_by_qn_not_found) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Insert a node so the store is non-empty */
    cbm_node_t n = {
        .project = "test", .label = "Function", .name = "Exists", .qualified_name = "test.Exists"};
    cbm_store_upsert_node(s, &n);

    /* Search for a non-existent QN */
    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_qn(s, "test", "test.DoesNotExist", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    /* Wrong project */
    rc = cbm_store_find_node_by_qn(s, "other-project", "test.Exists", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: cross-project lookups ──────────────────────────── */

TEST(store_find_by_qn_any_cross_project) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "proj-a", "/tmp/a");
    cbm_store_upsert_project(s, "proj-b", "/tmp/b");

    cbm_node_t na = {.project = "proj-a",
                     .label = "Function",
                     .name = "SharedFunc",
                     .qualified_name = "proj-a.main.SharedFunc"};
    cbm_node_t nb = {.project = "proj-b",
                     .label = "Class",
                     .name = "Widget",
                     .qualified_name = "proj-b.pkg.Widget"};
    cbm_store_upsert_node(s, &na);
    cbm_store_upsert_node(s, &nb);

    /* find_node_by_qn_any finds without project filter */
    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_qn_any(s, "proj-a.main.SharedFunc", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.name, "SharedFunc");
    ASSERT_STR_EQ(found.project, "proj-a");
    cbm_node_free_fields(&found);

    rc = cbm_store_find_node_by_qn_any(s, "proj-b.pkg.Widget", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.name, "Widget");
    ASSERT_STR_EQ(found.project, "proj-b");
    cbm_node_free_fields(&found);

    /* Non-existent QN */
    rc = cbm_store_find_node_by_qn_any(s, "nonexistent.Nope", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

TEST(store_find_by_name_any_cross_project) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "proj-a", "/tmp/a");
    cbm_store_upsert_project(s, "proj-b", "/tmp/b");

    /* Same name in two projects */
    cbm_node_t na = {.project = "proj-a",
                     .label = "Function",
                     .name = "Init",
                     .qualified_name = "proj-a.main.Init"};
    cbm_node_t nb = {.project = "proj-b",
                     .label = "Function",
                     .name = "Init",
                     .qualified_name = "proj-b.main.Init"};
    cbm_node_t nc = {.project = "proj-b",
                     .label = "Function",
                     .name = "Other",
                     .qualified_name = "proj-b.main.Other"};
    cbm_store_upsert_node(s, &na);
    cbm_store_upsert_node(s, &nb);
    cbm_store_upsert_node(s, &nc);

    /* find_nodes_by_name_any should find both "Init" across projects */
    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_name_any(s, "Init", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    cbm_store_free_nodes(nodes, count);

    /* Name that doesn't exist */
    rc = cbm_store_find_nodes_by_name_any(s, "Nonexistent", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: empty result sets ──────────────────────────────── */

TEST(store_find_by_file_no_match) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "Foo",
                    .qualified_name = "test.Foo",
                    .file_path = "main.go"};
    cbm_store_upsert_node(s, &n);

    /* Search for a file that has no nodes */
    cbm_node_t *nodes = NULL;
    int count = 0;
    int rc = cbm_store_find_nodes_by_file(s, "test", "nonexistent.go", &nodes, &count);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_nodes(nodes, count);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: batch upsert boundary ─────────────────────────── */

TEST(store_node_batch_upsert_zero) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Batch upsert with count=0 should succeed, do nothing */
    int rc = cbm_store_upsert_node_batch(s, NULL, 0, NULL);
    ASSERT_EQ(rc, CBM_STORE_OK);

    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 0);

    cbm_store_close(s);
    PASS();
}

TEST(store_node_batch_upsert_100) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t nodes[100];
    int64_t ids[100];
    char names[100][32];
    char qns[100][64];

    for (int i = 0; i < 100; i++) {
        snprintf(names[i], sizeof(names[i]), "stress_%d", i);
        snprintf(qns[i], sizeof(qns[i]), "test.stress.stress_%d", i);
        nodes[i] = (cbm_node_t){.project = "test",
                                .label = "Function",
                                .name = names[i],
                                .qualified_name = qns[i],
                                .file_path = "stress.go",
                                .start_line = i,
                                .end_line = i + 1};
    }

    int rc = cbm_store_upsert_node_batch(s, nodes, 100, ids);
    ASSERT_EQ(rc, CBM_STORE_OK);

    /* All IDs should be positive */
    for (int i = 0; i < 100; i++)
        ASSERT_GT(ids[i], 0);

    /* IDs should all be unique */
    for (int i = 0; i < 100; i++)
        for (int j = i + 1; j < 100; j++)
            ASSERT_NEQ(ids[i], ids[j]);

    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 100);

    /* Verify a few random lookups */
    cbm_node_t found = {0};
    rc = cbm_store_find_node_by_qn(s, "test", "test.stress.stress_0", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.name, "stress_0");
    cbm_node_free_fields(&found);

    rc = cbm_store_find_node_by_qn(s, "test", "test.stress.stress_99", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.name, "stress_99");
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: delete then verify remaining ──────────────────── */

TEST(store_delete_by_label_verify_remaining) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {
        .project = "test", .label = "Function", .name = "FuncA", .qualified_name = "test.FuncA"};
    cbm_node_t n2 = {
        .project = "test", .label = "Class", .name = "ClassB", .qualified_name = "test.ClassB"};
    cbm_node_t n3 = {
        .project = "test", .label = "Function", .name = "FuncC", .qualified_name = "test.FuncC"};
    cbm_node_t n4 = {
        .project = "test", .label = "Method", .name = "MethodD", .qualified_name = "test.MethodD"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    cbm_store_upsert_node(s, &n3);
    cbm_store_upsert_node(s, &n4);

    /* Delete all Functions */
    int rc = cbm_store_delete_nodes_by_label(s, "test", "Function");
    ASSERT_EQ(rc, CBM_STORE_OK);

    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 2);

    /* Class and Method should remain */
    cbm_node_t found = {0};
    rc = cbm_store_find_node_by_qn(s, "test", "test.ClassB", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.label, "Class");
    cbm_node_free_fields(&found);

    rc = cbm_store_find_node_by_qn(s, "test", "test.MethodD", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.label, "Method");
    cbm_node_free_fields(&found);

    /* Deleted ones should be gone */
    rc = cbm_store_find_node_by_qn(s, "test", "test.FuncA", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);
    rc = cbm_store_find_node_by_qn(s, "test", "test.FuncC", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

TEST(store_delete_by_file_verify_remaining) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "A",
                     .qualified_name = "test.A",
                     .file_path = "delete_me.go"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "B",
                     .qualified_name = "test.B",
                     .file_path = "keep_me.go"};
    cbm_node_t n3 = {.project = "test",
                     .label = "Function",
                     .name = "C",
                     .qualified_name = "test.C",
                     .file_path = "delete_me.go"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    cbm_store_upsert_node(s, &n3);

    int rc = cbm_store_delete_nodes_by_file(s, "test", "delete_me.go");
    ASSERT_EQ(rc, CBM_STORE_OK);

    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 1);

    /* Only keep_me.go node should remain */
    cbm_node_t found = {0};
    rc = cbm_store_find_node_by_qn(s, "test", "test.B", &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.file_path, "keep_me.go");
    cbm_node_free_fields(&found);

    /* Deleted nodes should be gone */
    rc = cbm_store_find_node_by_qn(s, "test", "test.A", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);
    rc = cbm_store_find_node_by_qn(s, "test", "test.C", &found);
    ASSERT_EQ(rc, CBM_STORE_NOT_FOUND);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: upsert dedup with field changes ───────────────── */

TEST(store_node_upsert_updates_fields) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Insert initial node */
    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "MyFunc",
                     .qualified_name = "test.MyFunc",
                     .file_path = "old.go",
                     .start_line = 1,
                     .end_line = 10,
                     .properties_json = "{\"version\":1}"};
    int64_t id1 = cbm_store_upsert_node(s, &n1);
    ASSERT_GT(id1, 0);

    /* Upsert same QN with changed fields */
    cbm_node_t n2 = {.project = "test",
                     .label = "Method",
                     .name = "MyFunc",
                     .qualified_name = "test.MyFunc",
                     .file_path = "new.go",
                     .start_line = 50,
                     .end_line = 60,
                     .properties_json = "{\"version\":2}"};
    int64_t id2 = cbm_store_upsert_node(s, &n2);
    ASSERT_EQ(id1, id2); /* Same ID — updated, not duplicated */

    /* Count should still be 1 */
    int cnt = cbm_store_count_nodes(s, "test");
    ASSERT_EQ(cnt, 1);

    /* Verify fields were updated */
    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_id(s, id1, &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.label, "Method");
    ASSERT_STR_EQ(found.file_path, "new.go");
    ASSERT_EQ(found.start_line, 50);
    ASSERT_EQ(found.end_line, 60);
    ASSERT(strstr(found.properties_json, "version") != NULL);
    ASSERT(strstr(found.properties_json, "2") != NULL);
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: long qualified name ───────────────────────────── */

TEST(store_node_long_qn) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Build a 1200-char qualified name */
    char long_qn[1201];
    memset(long_qn, 'a', 1200);
    long_qn[0] = 't'; /* make it look like a dotted path */
    for (int i = 50; i < 1200; i += 50)
        long_qn[i] = '.';
    long_qn[1200] = '\0';

    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "LongName",
                    .qualified_name = long_qn,
                    .file_path = "big.go"};
    int64_t id = cbm_store_upsert_node(s, &n);
    ASSERT_GT(id, 0);

    /* Should be retrievable by QN */
    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_qn(s, "test", long_qn, &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_STR_EQ(found.qualified_name, long_qn);
    ASSERT_STR_EQ(found.name, "LongName");
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: properties JSON with special characters ────────── */

TEST(store_node_properties_special_chars) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* JSON with quotes, backslashes, unicode, newlines */
    const char *props = "{\"desc\":\"line1\\nline2\","
                        "\"path\":\"C:\\\\Users\\\\test\","
                        "\"emoji\":\"\\u2603\","
                        "\"nested\":{\"key\":\"val with \\\"quotes\\\"\"}}";

    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "SpecialFunc",
                    .qualified_name = "test.SpecialFunc",
                    .properties_json = props};
    int64_t id = cbm_store_upsert_node(s, &n);
    ASSERT_GT(id, 0);

    cbm_node_t found = {0};
    int rc = cbm_store_find_node_by_id(s, id, &found);
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_NOT_NULL(found.properties_json);
    /* Round-trip should preserve the JSON exactly */
    ASSERT_STR_EQ(found.properties_json, props);
    cbm_node_free_fields(&found);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: delete from non-existent project/file ─────────── */

TEST(store_delete_nodes_nonexistent) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Insert one node */
    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "Survivor",
                    .qualified_name = "test.Survivor",
                    .file_path = "main.go"};
    cbm_store_upsert_node(s, &n);

    /* Delete by non-existent file — should succeed but delete nothing */
    int rc = cbm_store_delete_nodes_by_file(s, "test", "ghost.go");
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(cbm_store_count_nodes(s, "test"), 1);

    /* Delete by non-existent label */
    rc = cbm_store_delete_nodes_by_label(s, "test", "Interface");
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(cbm_store_count_nodes(s, "test"), 1);

    /* Delete by non-existent project */
    rc = cbm_store_delete_nodes_by_project(s, "no-such-project");
    ASSERT_EQ(rc, CBM_STORE_OK);
    ASSERT_EQ(cbm_store_count_nodes(s, "test"), 1);

    cbm_store_close(s);
    PASS();
}

/* ── Edge case: count nodes for unknown project ───────────────── */

TEST(store_count_nodes_unknown_project) {
    cbm_store_t *s = cbm_store_open_memory();
    /* No project created — count should be 0 */
    int cnt = cbm_store_count_nodes(s, "ghost-project");
    ASSERT_EQ(cnt, 0);
    cbm_store_close(s);
    PASS();
}

/* ── Index coverage (#963) ──────────────────────────────────────── */

/* Round-trip + deleted-file prune + shadow miss-graph materialization +
 * empty-replace wipe. The prune keys off file_hashes (the live-file set), so
 * a row for a file with no hash row must not survive the replace. */
TEST(store_coverage_roundtrip_prune_shadow) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_store_upsert_file_hash(s, "test", "src/a.py", "", 1, 10);

    cbm_coverage_row_t rows[] = {
        {.rel_path = "src/a.py", .kind = "parse_partial", .detail = "4-7"},
        {.rel_path = "src/a.py",
         .kind = "analysis_partial:rust",
         .detail = "{\"version\":1,\"status\":\"partial\"}"},
        {.rel_path = "gone.py", .kind = "oversized", .detail = "too big"},
        /* By-design rows (#963): neither has a file_hashes row, yet both must
         * SURVIVE the deleted-file prune (deliberately-unindexed paths never
         * have hashes) — and must stay OUT of the shadow miss graph. */
        {.rel_path = "secret.py", .kind = "not_indexed_file", .detail = "gitignore"},
        {.rel_path = "generated", .kind = "not_indexed_dir", .detail = "excluded subtree"},
    };
    ASSERT_EQ(cbm_store_coverage_replace(s, "test", rows, 5), CBM_STORE_OK);

    cbm_coverage_row_t *got = NULL;
    int n = 0;
    ASSERT_EQ(cbm_store_coverage_get(s, "test", &got, &n), CBM_STORE_OK);
    ASSERT_EQ(n, 4); /* gone.py pruned; syntactic + semantic rows coexist */
    int saw_partial = 0;
    int saw_analysis = 0;
    int saw_by_design = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(got[i].kind, "parse_partial") == 0) {
            saw_partial++;
        }
        if (strncmp(got[i].kind, "not_indexed", 11) == 0) {
            saw_by_design++;
        }
        if (strcmp(got[i].kind, "analysis_partial:rust") == 0) {
            saw_analysis++;
        }
    }
    ASSERT_EQ(saw_partial, 1);
    ASSERT_EQ(saw_analysis, 1);
    ASSERT_EQ(saw_by_design, 2);
    cbm_store_free_coverage(got, n);

    /* Shadow miss-graph materialized under "test::missed": FAILURES only —
     * Project → Folder(src) → File(a.py){kind,detail}; the by-design rows do
     * not appear. */
    cbm_node_t *nodes = NULL;
    int nc = 0;
    ASSERT_EQ(cbm_store_find_nodes_by_label(s, "test::missed", "File", &nodes, &nc), CBM_STORE_OK);
    ASSERT_EQ(nc, 1);
    ASSERT_STR_EQ(nodes[0].file_path, "src/a.py");
    ASSERT_NOT_NULL(strstr(nodes[0].properties_json, "\"kind\":\"parse_partial\""));
    ASSERT_NOT_NULL(strstr(nodes[0].properties_json, "\"detail\":\"4-7\""));
    cbm_store_free_nodes(nodes, nc);
    nodes = NULL;
    nc = 0;
    ASSERT_EQ(cbm_store_find_nodes_by_label(s, "test::missed", "Folder", &nodes, &nc),
              CBM_STORE_OK);
    ASSERT_EQ(nc, 1);
    ASSERT_STR_EQ(nodes[0].name, "src");
    cbm_store_free_nodes(nodes, nc);

    /* Empty replace clears the table AND wipes the shadow graph. */
    ASSERT_EQ(cbm_store_coverage_replace(s, "test", NULL, 0), CBM_STORE_OK);
    got = NULL;
    n = 0;
    ASSERT_EQ(cbm_store_coverage_get(s, "test", &got, &n), CBM_STORE_OK);
    ASSERT_EQ(n, 0);
    free(got);
    nodes = NULL;
    nc = 0;
    ASSERT_EQ(cbm_store_find_nodes_by_label(s, "test::missed", "File", &nodes, &nc), CBM_STORE_OK);
    ASSERT_EQ(nc, 0);
    cbm_store_free_nodes(nodes, nc);

    cbm_store_close(s);
    PASS();
}

TEST(store_coverage_targeted_path_and_scope_lookup) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "coverage-targeted", "/tmp/coverage-targeted"),
              CBM_STORE_OK);

    /* Failure rows need live-file hash records so coverage_replace does not
     * correctly prune them as deleted. By-design exclusions have no hashes. */
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-targeted", "src/partial.c", "", 10, 20),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-targeted", "src/skipped.c", "", 11, 21),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-targeted", "src2/other.c", "", 12, 22),
              CBM_STORE_OK);

    cbm_coverage_row_t rows[] = {
        {.rel_path = "src/partial.c", .kind = "parse_partial", .detail = "7-9"},
        {.rel_path = "src/skipped.c", .kind = "oversized", .detail = "too large"},
        {.rel_path = "src2/other.c", .kind = "extract", .detail = "parser failed"},
        {.rel_path = "generated", .kind = "not_indexed_dir", .detail = "excluded subtree"},
        {.rel_path = "secret.c", .kind = "not_indexed_file", .detail = "gitignore"},
    };
    ASSERT_EQ(cbm_store_coverage_replace(s, "coverage-targeted", rows, 5), CBM_STORE_OK);

    cbm_coverage_row_t *got = NULL;
    int count = 0;
    ASSERT_EQ(cbm_store_coverage_get_path(s, "coverage-targeted", "src/partial.c", &got, &count),
              CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(got[0].rel_path, "src/partial.c");
    ASSERT_STR_EQ(got[0].kind, "parse_partial");
    ASSERT_STR_EQ(got[0].detail, "7-9");
    cbm_store_free_coverage(got, count);

    /* A file below an excluded directory inherits that directory row. */
    got = NULL;
    count = 0;
    ASSERT_EQ(cbm_store_coverage_get_path(s, "coverage-targeted", "generated/nested/file.c", &got,
                                          &count),
              CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(got[0].rel_path, "generated");
    ASSERT_STR_EQ(got[0].kind, "not_indexed_dir");
    cbm_store_free_coverage(got, count);

    /* Prefixes must stop at path-segment boundaries. */
    got = NULL;
    count = 0;
    ASSERT_EQ(
        cbm_store_coverage_get_path(s, "coverage-targeted", "generated2/file.c", &got, &count),
        CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_coverage(got, count);

    got = NULL;
    count = 0;
    ASSERT_EQ(cbm_store_coverage_get_scope(s, "coverage-targeted", "src", &got, &count),
              CBM_STORE_OK);
    ASSERT_EQ(count, 2);
    ASSERT_STR_EQ(got[0].rel_path, "src/partial.c");
    ASSERT_STR_EQ(got[1].rel_path, "src/skipped.c");
    cbm_store_free_coverage(got, count);

    /* A scope nested below an excluded directory still reports its ancestor. */
    got = NULL;
    count = 0;
    ASSERT_EQ(
        cbm_store_coverage_get_scope(s, "coverage-targeted", "generated/nested", &got, &count),
        CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(got[0].rel_path, "generated");
    cbm_store_free_coverage(got, count);

    cbm_file_hash_t hash = {0};
    ASSERT_EQ(cbm_store_get_file_hash(s, "coverage-targeted", "src/partial.c", &hash),
              CBM_STORE_OK);
    ASSERT_STR_EQ(hash.project, "coverage-targeted");
    ASSERT_STR_EQ(hash.rel_path, "src/partial.c");
    ASSERT_EQ(hash.mtime_ns, 10);
    ASSERT_EQ(hash.size, 20);
    cbm_store_clear_file_hash(&hash);
    ASSERT_NULL(hash.project);
    ASSERT_NULL(hash.rel_path);
    ASSERT_NULL(hash.sha256);

    ASSERT_EQ(cbm_store_get_file_hash(s, "coverage-targeted", "missing.c", &hash),
              CBM_STORE_NOT_FOUND);
    cbm_store_clear_file_hash(&hash);

    cbm_store_close(s);
    PASS();
}

TEST(store_coverage_meta_zero_row_truncation_and_delete) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "coverage-meta", "/tmp/coverage-meta"), CBM_STORE_OK);

    cbm_coverage_meta_t write_meta = {
        .generation = "generation-42",
        .index_mode = "fast",
        .recording_status = "truncated",
        .ignored_files_stored = 2000,
        .ignored_files_total = 2501,
        .coverage_version = 1,
        .hash_records_complete = false,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 37,
    };
    /* Metadata is meaningful even when the authoritative miss set is empty. */
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "coverage-meta", NULL, 0, &write_meta),
              CBM_STORE_OK);

    cbm_coverage_row_t *rows = NULL;
    int count = 0;
    ASSERT_EQ(cbm_store_coverage_get(s, "coverage-meta", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_coverage(rows, count);

    cbm_coverage_meta_t got = {0};
    ASSERT_EQ(cbm_store_coverage_meta_get(s, "coverage-meta", &got), CBM_STORE_OK);
    ASSERT_STR_EQ(got.project, "coverage-meta");
    ASSERT_STR_EQ(got.generation, "generation-42");
    ASSERT_NOT_NULL(got.recorded_at);
    ASSERT_STR_EQ(got.index_mode, "fast");
    ASSERT_STR_EQ(got.recording_status, "truncated");
    ASSERT_EQ(got.ignored_files_stored, 2000);
    ASSERT_EQ(got.ignored_files_total, 2501);
    ASSERT_EQ(got.coverage_version, 1);
    ASSERT_FALSE(got.hash_records_complete);
    ASSERT_STR_EQ(got.rust_analysis_recording_status, "complete");
    ASSERT_EQ(got.rust_files_total, 37);
    cbm_store_coverage_meta_clear(&got);
    ASSERT_NULL(got.project);
    ASSERT_NULL(got.generation);
    ASSERT_NULL(got.recorded_at);
    ASSERT_NULL(got.index_mode);
    ASSERT_NULL(got.recording_status);
    ASSERT_NULL(got.rust_analysis_recording_status);

    /* Replacing through the compatibility wrapper clears possibly-stale meta. */
    ASSERT_EQ(cbm_store_coverage_replace(s, "coverage-meta", NULL, 0), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_coverage_meta_get(s, "coverage-meta", &got), CBM_STORE_NOT_FOUND);
    cbm_store_coverage_meta_clear(&got);

    /* Recreate metadata and a missed-graph node, then project deletion must
     * remove the table rows, metadata, and the derived ::missed project. */
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-meta", "bad.c", "", 1, 1), CBM_STORE_OK);
    cbm_coverage_row_t failure = {.rel_path = "bad.c", .kind = "parse_partial", .detail = "1-2"};
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "coverage-meta", &failure, 1, &write_meta),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_delete_project(s, "coverage-meta"), CBM_STORE_OK);

    rows = NULL;
    count = 0;
    ASSERT_EQ(cbm_store_coverage_get(s, "coverage-meta", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 0);
    cbm_store_free_coverage(rows, count);
    ASSERT_EQ(cbm_store_coverage_meta_get(s, "coverage-meta", &got), CBM_STORE_NOT_FOUND);
    cbm_store_coverage_meta_clear(&got);
    cbm_project_t shadow = {0};
    ASSERT_EQ(cbm_store_get_project(s, "coverage-meta::missed", &shadow), CBM_STORE_NOT_FOUND);
    cbm_project_free_fields(&shadow);

    cbm_store_close(s);
    PASS();
}

TEST(store_coverage_replace_rejects_invalid_row_arguments) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "coverage-invalid", "/tmp/coverage-invalid"),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-invalid", "kept.c", "", 1, 1), CBM_STORE_OK);
    cbm_coverage_row_t kept = {.rel_path = "kept.c", .kind = "parse_partial", .detail = "3-4"};
    cbm_coverage_meta_t original_meta = {
        .generation = "before-invalid-call",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = 1,
        .hash_records_complete = true,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "coverage-invalid", &kept, 1, &original_meta),
              CBM_STORE_OK);

    cbm_coverage_meta_t replacement_meta = original_meta;
    replacement_meta.generation = "must-not-commit";
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "coverage-invalid", &kept, -1, &replacement_meta),
              CBM_STORE_ERR);
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "coverage-invalid", NULL, 1, &replacement_meta),
              CBM_STORE_ERR);

    cbm_coverage_row_t *rows = NULL;
    int count = 0;
    ASSERT_EQ(cbm_store_coverage_get(s, "coverage-invalid", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(rows[0].rel_path, "kept.c");
    ASSERT_STR_EQ(rows[0].detail, "3-4");
    cbm_store_free_coverage(rows, count);
    cbm_coverage_meta_t got = {0};
    ASSERT_EQ(cbm_store_coverage_meta_get(s, "coverage-invalid", &got), CBM_STORE_OK);
    ASSERT_STR_EQ(got.generation, "before-invalid-call");
    cbm_store_coverage_meta_clear(&got);

    cbm_store_close(s);
    PASS();
}

TEST(store_coverage_replace_rolls_back_when_shadow_rebuild_fails) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "coverage-shadow", "/tmp/coverage-shadow"), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-shadow", "old.c", "", 1, 1), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-shadow", "new.c", "", 2, 2), CBM_STORE_OK);

    cbm_coverage_row_t old_row = {
        .rel_path = "old.c", .kind = "parse_partial", .detail = "old-detail"};
    cbm_coverage_meta_t old_meta = {
        .generation = "old-generation",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = 1,
        .hash_records_complete = true,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "coverage-shadow", &old_row, 1, &old_meta),
              CBM_STORE_OK);
    int old_shadow_nodes = cbm_store_count_nodes(s, "coverage-shadow::missed");
    ASSERT_GT(old_shadow_nodes, 0);

    /* Force only the derived-view rebuild to fail. The authoritative rows,
     * metadata, and prior shadow graph must remain one atomic generation. */
    ASSERT_EQ(cbm_store_exec(s, "CREATE TRIGGER fail_missed_insert BEFORE INSERT ON nodes "
                                "WHEN NEW.project = 'coverage-shadow::missed' "
                                "BEGIN SELECT RAISE(ABORT, 'forced missed shadow failure'); END;"),
              CBM_STORE_OK);

    cbm_coverage_row_t new_row = {
        .rel_path = "new.c", .kind = "parse_partial", .detail = "new-detail"};
    cbm_coverage_meta_t new_meta = old_meta;
    new_meta.generation = "new-generation";
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "coverage-shadow", &new_row, 1, &new_meta),
              CBM_STORE_ERR);

    cbm_coverage_row_t *rows = NULL;
    int count = 0;
    ASSERT_EQ(cbm_store_coverage_get(s, "coverage-shadow", &rows, &count), CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(rows[0].rel_path, "old.c");
    ASSERT_STR_EQ(rows[0].detail, "old-detail");
    cbm_store_free_coverage(rows, count);

    cbm_coverage_meta_t got = {0};
    ASSERT_EQ(cbm_store_coverage_meta_get(s, "coverage-shadow", &got), CBM_STORE_OK);
    ASSERT_STR_EQ(got.generation, "old-generation");
    cbm_store_coverage_meta_clear(&got);
    ASSERT_EQ(cbm_store_count_nodes(s, "coverage-shadow::missed"), old_shadow_nodes);

    cbm_store_close(s);
    PASS();
}

TEST(store_analysis_rows_do_not_rebuild_or_materialize_missed_graph) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "coverage-analysis", "/tmp/coverage-analysis"),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "coverage-analysis", "same.rs", "", 1, 1),
              CBM_STORE_OK);
    cbm_coverage_row_t initial[] = {
        {.rel_path = "same.rs", .kind = "parse_partial", .detail = "2-3"},
        {.rel_path = "same.rs", .kind = "analysis_partial:rust", .detail = "health-a"},
    };
    ASSERT_EQ(cbm_store_coverage_replace(s, "coverage-analysis", initial, 2), CBM_STORE_OK);

    cbm_node_t *nodes = NULL;
    int count = 0;
    ASSERT_EQ(cbm_store_find_nodes_by_label(s, "coverage-analysis::missed", "File", &nodes, &count),
              CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(strstr(nodes[0].properties_json, "\"kind\":\"parse_partial\""));
    ASSERT_NULL(strstr(nodes[0].properties_json, "analysis_partial"));
    cbm_store_free_nodes(nodes, count);

    /* If the semantic row contaminated the missed fingerprint this changed
     * detail would attempt a rebuild and the trigger would abort it. */
    ASSERT_EQ(cbm_store_exec(s, "CREATE TRIGGER reject_analysis_shadow BEFORE INSERT ON nodes "
                                "WHEN NEW.project = 'coverage-analysis::missed' "
                                "BEGIN SELECT RAISE(ABORT, 'unexpected rebuild'); END;"),
              CBM_STORE_OK);
    cbm_coverage_row_t changed[] = {
        {.rel_path = "same.rs", .kind = "parse_partial", .detail = "2-3"},
        {.rel_path = "same.rs", .kind = "analysis_failed:rust", .detail = "health-b"},
    };
    ASSERT_EQ(cbm_store_coverage_replace(s, "coverage-analysis", changed, 2), CBM_STORE_OK);
    nodes = NULL;
    count = 0;
    ASSERT_EQ(cbm_store_find_nodes_by_label(s, "coverage-analysis::missed", "File", &nodes, &count),
              CBM_STORE_OK);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(strstr(nodes[0].properties_json, "\"detail\":\"2-3\""));
    cbm_store_free_nodes(nodes, count);
    cbm_store_close(s);
    PASS();
}

static void store_test_sha256_final_hex(cbm_sha256_ctx *sha, char out[65]) {
    uint8_t digest[CBM_SHA256_DIGEST_LEN];
    cbm_sha256_final(sha, digest);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < CBM_SHA256_DIGEST_LEN; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    out[CBM_SHA256_HEX_LEN] = '\0';
}

static void store_test_sha256_coverage_row(cbm_sha256_ctx *sha, const char *path, const char *kind,
                                           const char *detail) {
    const char *fields[] = {path, kind, detail};
    const unsigned char tags[] = {'P', 'K', 'D'};
    for (int field = 0; field < 3; field++) {
        uint64_t length = strlen(fields[field]);
        unsigned char framed[9] = {tags[field]};
        uint64_t n = length;
        for (int i = 0; i < 8; i++) {
            framed[8 - i] = (unsigned char)(n & 0xffU);
            n >>= 8;
        }
        cbm_sha256_update(sha, framed, sizeof(framed));
        cbm_sha256_update(sha, fields[field], (size_t)length);
    }
}

TEST(store_syntactic_coverage_project_pages_filter_order_digest_and_bound_thousands) {
    enum {
        FILES = 1200,
        SYNTACTIC_ROWS = FILES + 1,
        SEMANTIC_ROWS = FILES,
        ALL_ROWS = SYNTACTIC_ROWS + SEMANTIC_ROWS,
        PATH_BYTES = 48,
        LONG_DETAIL_BYTES = 8192,
    };
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "syntactic-bulk", "/tmp/syntactic-bulk"), CBM_STORE_OK);
    cbm_coverage_row_t *rows = calloc(ALL_ROWS, sizeof(*rows));
    char (*paths)[PATH_BYTES] = calloc(ALL_ROWS, sizeof(*paths));
    char *long_detail = malloc(LONG_DETAIL_BYTES);
    ASSERT_NOT_NULL(rows);
    ASSERT_NOT_NULL(paths);
    ASSERT_NOT_NULL(long_detail);
    memset(long_detail, 'x', LONG_DETAIL_BYTES - 1);
    memset(long_detail, 'a', 30);
    long_detail[30] = (char)0xe2;
    long_detail[31] = (char)0x82;
    long_detail[32] = (char)0xac;
    long_detail[LONG_DETAIL_BYTES - 1] = '\0';

    int n = 0;
    for (int i = 0; i < FILES; i++) {
        char live_path[PATH_BYTES];
        snprintf(live_path, sizeof(live_path), "src/file%04d.rs", i);
        ASSERT_EQ(cbm_store_upsert_file_hash(s, "syntactic-bulk", live_path, "", i + 1, 1),
                  CBM_STORE_OK);
        snprintf(paths[n], PATH_BYTES, "%s", live_path);
        rows[n] = (cbm_coverage_row_t){
            .rel_path = paths[n],
            .kind = i % 2 == 0 ? "parse_partial" : "read",
            .detail = i == 0 ? long_detail : (i % 2 == 0 ? "pp" : "skip"),
        };
        n++;
        snprintf(paths[n], PATH_BYTES, "%s", live_path);
        rows[n] = (cbm_coverage_row_t){
            .rel_path = paths[n], .kind = "analysis_partial:rust", .detail = "semantic"};
        n++;
    }
    snprintf(paths[n], PATH_BYTES, "src/file0000.rs");
    rows[n] =
        (cbm_coverage_row_t){.rel_path = paths[n], .kind = "read", .detail = "duplicate-kind"};
    n++;
    ASSERT_EQ(n, ALL_ROWS);
    cbm_coverage_meta_t meta = {
        .generation = "generation-syntactic-bulk",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = FILES,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "syntactic-bulk", rows, ALL_ROWS, &meta),
              CBM_STORE_OK);

    cbm_sha256_ctx expected_sha;
    cbm_sha256_init(&expected_sha);
    for (int i = 0; i < FILES; i++) {
        char path[PATH_BYTES];
        snprintf(path, sizeof(path), "src/file%04d.rs", i);
        const char *kind = i % 2 == 0 ? "parse_partial" : "read";
        const char *detail = i == 0 ? long_detail : (i % 2 == 0 ? "pp" : "skip");
        store_test_sha256_coverage_row(&expected_sha, path, kind, detail);
        if (i == 0) {
            store_test_sha256_coverage_row(&expected_sha, path, "read", "duplicate-kind");
        }
    }
    char expected_digest[65];
    store_test_sha256_final_hex(&expected_sha, expected_digest);
    char expected_detail_digest[65];
    cbm_sha256_hex(long_detail, strlen(long_detail), expected_detail_digest);

    cbm_syntactic_coverage_request_t request = {
        .project = "syntactic-bulk",
        .mode = CBM_SYNTACTIC_COVERAGE_PROJECT,
        .selector = NULL,
        .offset = 0,
        .limit = 37,
        .detail_preview_bytes = 31,
    };
    int observed = 0;
    do {
        cbm_syntactic_coverage_page_t page = {0};
        ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
                  CBM_SYNTACTIC_COVERAGE_OK);
        ASSERT_TRUE(page.has_meta);
        ASSERT_STR_EQ(page.meta.generation, "generation-syntactic-bulk");
        ASSERT_EQ(page.totals.rows_total, SYNTACTIC_ROWS);
        ASSERT_EQ(page.totals.parse_partial_rows, FILES / 2);
        ASSERT_EQ(page.totals.skipped_rows, FILES / 2 + 1);
        ASSERT_EQ(page.totals.not_indexed_dir_rows, 0);
        ASSERT_EQ(page.totals.not_indexed_file_rows, 0);
        ASSERT_STR_EQ(page.rows_sha256, expected_digest);
        ASSERT_TRUE(page.returned > 0);
        ASSERT_TRUE(page.returned <= request.limit);
        for (int i = 0; i < page.returned; i++, observed++) {
            int file_index = observed == 0 ? 0 : observed - 1;
            if (observed > 1) {
                file_index = observed - 1;
            }
            char expected_path[PATH_BYTES];
            snprintf(expected_path, sizeof(expected_path), "src/file%04d.rs", file_index);
            ASSERT_STR_EQ(page.rows[i].rel_path, expected_path);
            ASSERT_EQ(page.rows[i].match, CBM_SYNTACTIC_COVERAGE_MATCH_PROJECT);
            if (observed == 0) {
                ASSERT_STR_EQ(page.rows[i].kind, "parse_partial");
                ASSERT_EQ(strlen(page.rows[i].detail), 30);
                ASSERT_EQ(page.rows[i].detail_complete_bytes, LONG_DETAIL_BYTES - 1);
                ASSERT_TRUE(page.rows[i].detail_truncated);
                ASSERT_STR_EQ(page.rows[i].detail_sha256, expected_detail_digest);
            } else if (observed == 1) {
                ASSERT_STR_EQ(page.rows[i].kind, "read");
                ASSERT_STR_EQ(page.rows[i].detail, "duplicate-kind");
            } else {
                const char *expected_kind = file_index % 2 == 0 ? "parse_partial" : "read";
                ASSERT_STR_EQ(page.rows[i].kind, expected_kind);
            }
        }
        ASSERT_EQ(page.next_offset, request.offset + page.returned);
        bool has_more = page.has_more;
        request.offset = page.next_offset;
        cbm_store_syntactic_coverage_page_clear(&page);
        if (!has_more) {
            break;
        }
    } while (true);
    ASSERT_EQ(observed, SYNTACTIC_ROWS);
    ASSERT_EQ(request.offset, SYNTACTIC_ROWS);

    free(long_detail);
    free(paths);
    free(rows);
    cbm_store_close(s);
    PASS();
}

TEST(store_syntactic_coverage_exact_and_scope_use_segment_safe_ancestors) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "syntactic-targeted", "/tmp/syntactic-targeted"),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "syntactic-targeted", "generated/nested", "", 1, 1),
              CBM_STORE_OK);
    ASSERT_EQ(
        cbm_store_upsert_file_hash(s, "syntactic-targeted", "generated/nested/a.rs", "", 2, 1),
        CBM_STORE_OK);
    ASSERT_EQ(
        cbm_store_upsert_file_hash(s, "syntactic-targeted", "generated2/nested/a.rs", "", 3, 1),
        CBM_STORE_OK);
    const cbm_coverage_row_t rows[] = {
        {.rel_path = "gen", .kind = "not_indexed_dir", .detail = "collision"},
        {.rel_path = "generated", .kind = "not_indexed_dir", .detail = "ancestor"},
        {.rel_path = "generated/nested", .kind = "parse_partial", .detail = "exact"},
        {.rel_path = "generated/nested/a.rs", .kind = "read", .detail = "child-a"},
        {.rel_path = "generated/nested/a.rs", .kind = "oversized", .detail = "child-b"},
        {.rel_path = "generated/nested/deeper/b.rs", .kind = "not_indexed_file", .detail = "b"},
        {.rel_path = "generated2/nested/a.rs", .kind = "read", .detail = "sibling"},
        {.rel_path = "generated/nested/a.rs", .kind = "analysis_failed:rust", .detail = "sem"},
    };
    cbm_coverage_meta_t meta = {
        .generation = "generation-targeted",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 1,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "syntactic-targeted", rows, 8, &meta), CBM_STORE_OK);

    cbm_syntactic_coverage_request_t exact = {
        .project = "syntactic-targeted",
        .mode = CBM_SYNTACTIC_COVERAGE_EXACT_PATH,
        .selector = "generated/nested/a.rs",
        .offset = 0,
        .limit = 8,
        .detail_preview_bytes = 64,
    };
    cbm_syntactic_coverage_page_t page = {0};
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &exact, &page), CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_EQ(page.totals.rows_total, 3);
    ASSERT_STR_EQ(page.rows[0].rel_path, "generated");
    ASSERT_EQ(page.rows[0].match, CBM_SYNTACTIC_COVERAGE_MATCH_ANCESTOR);
    ASSERT_STR_EQ(page.rows[1].kind, "oversized");
    ASSERT_STR_EQ(page.rows[2].kind, "read");
    ASSERT_EQ(page.rows[1].match, CBM_SYNTACTIC_COVERAGE_MATCH_EXACT);
    ASSERT_EQ(page.rows[2].match, CBM_SYNTACTIC_COVERAGE_MATCH_EXACT);
    cbm_store_syntactic_coverage_page_clear(&page);

    cbm_syntactic_coverage_request_t scope = {
        .project = "syntactic-targeted",
        .mode = CBM_SYNTACTIC_COVERAGE_SCOPE,
        .selector = "generated/nested",
        .offset = 0,
        .limit = 8,
        .detail_preview_bytes = 64,
    };
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &scope, &page), CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_EQ(page.totals.rows_total, 5);
    ASSERT_STR_EQ(page.rows[0].rel_path, "generated");
    ASSERT_EQ(page.rows[0].match, CBM_SYNTACTIC_COVERAGE_MATCH_ANCESTOR);
    ASSERT_STR_EQ(page.rows[1].rel_path, "generated/nested");
    ASSERT_EQ(page.rows[1].match, CBM_SYNTACTIC_COVERAGE_MATCH_EXACT);
    ASSERT_STR_EQ(page.rows[4].rel_path, "generated/nested/deeper/b.rs");
    ASSERT_EQ(page.rows[4].match, CBM_SYNTACTIC_COVERAGE_MATCH_DESCENDANT);
    cbm_store_syntactic_coverage_page_clear(&page);
    cbm_store_close(s);
    PASS();
}

TEST(store_syntactic_coverage_empty_meta_invalid_and_allocation_statuses) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "syntactic-status", "/tmp/syntactic-status"),
              CBM_STORE_OK);
    cbm_coverage_meta_t meta = {
        .generation = "generation-status",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 0,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "syntactic-status", NULL, 0, &meta), CBM_STORE_OK);
    cbm_syntactic_coverage_request_t request = {
        .project = "syntactic-status",
        .mode = CBM_SYNTACTIC_COVERAGE_PROJECT,
        .selector = NULL,
        .offset = 0,
        .limit = 1,
        .detail_preview_bytes = 32,
    };
    cbm_syntactic_coverage_page_t page = {0};
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page), CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_TRUE(page.has_meta);
    ASSERT_STR_EQ(page.meta.generation, "generation-status");
    ASSERT_EQ(page.totals.rows_total, 0);
    ASSERT_EQ(page.returned, 0);
    ASSERT_FALSE(page.has_more);
    ASSERT_EQ(strlen(page.rows_sha256), 64);
    cbm_store_syntactic_coverage_page_clear(&page);

    request.selector = "ambiguous";
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_INVALID_ARGUMENT);
    request.selector = NULL;
    request.offset = -1;
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_INVALID_ARGUMENT);
    request.offset = 0;
    request.limit = CBM_SYNTACTIC_COVERAGE_PAGE_MAX_ROWS + 1;
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_INVALID_ARGUMENT);
    request.limit = 1;
    request.detail_preview_bytes = CBM_SYNTACTIC_COVERAGE_DETAIL_MAX_BYTES + 1U;
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_INVALID_ARGUMENT);
    request.detail_preview_bytes = 32;
    request.mode = CBM_SYNTACTIC_COVERAGE_EXACT_PATH;
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_INVALID_ARGUMENT);
    request.mode = CBM_SYNTACTIC_COVERAGE_SCOPE;
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_INVALID_ARGUMENT);

    cbm_coverage_row_t row = {
        .rel_path = "excluded.rs", .kind = "not_indexed_file", .detail = "excluded"};
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "syntactic-status", &row, 1, &meta), CBM_STORE_OK);
    request.mode = CBM_SYNTACTIC_COVERAGE_PROJECT;
    request.selector = NULL;
    cbm_store_syntactic_coverage_test_fail_alloc_after(s, 0);
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_ALLOCATION_FAILED);
    ASSERT_NULL(page.rows);
    ASSERT_FALSE(page.has_meta);
    cbm_store_syntactic_coverage_test_fail_alloc_after(s, 6);
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_ALLOCATION_FAILED);
    ASSERT_NULL(page.rows);
    cbm_store_syntactic_coverage_test_fail_alloc_after(s, 9);
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_ALLOCATION_FAILED);
    ASSERT_NULL(page.rows);
    cbm_store_syntactic_coverage_test_fail_alloc_after(s, -1);
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page), CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_EQ(page.returned, 1);
    cbm_store_syntactic_coverage_page_clear(&page);
    ASSERT_EQ(cbm_store_exec(s, "DROP TABLE index_coverage;"), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page),
              CBM_SYNTACTIC_COVERAGE_STORE_ERROR);
    cbm_store_close(s);
    PASS();
}

TEST(store_syntactic_coverage_digest_frames_embedded_nul_fields) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "syntactic-nul", "/tmp/syntactic-nul"), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_exec(s, "INSERT INTO index_coverage(project,rel_path,kind,detail) VALUES"
                                "('syntactic-nul',CAST(X'610062' AS TEXT),'c','d');"),
              CBM_STORE_OK);

    const unsigned char path[] = {'a', 0, 'b'};
    cbm_sha256_ctx expected_sha;
    cbm_sha256_init(&expected_sha);
    const unsigned char *fields[] = {path, (const unsigned char *)"c", (const unsigned char *)"d"};
    const size_t lengths[] = {sizeof(path), 1, 1};
    const unsigned char tags[] = {'P', 'K', 'D'};
    for (int field = 0; field < 3; field++) {
        unsigned char framed[9] = {tags[field]};
        uint64_t n = lengths[field];
        for (int i = 0; i < 8; i++) {
            framed[8 - i] = (unsigned char)(n & 0xffU);
            n >>= 8;
        }
        cbm_sha256_update(&expected_sha, framed, sizeof(framed));
        cbm_sha256_update(&expected_sha, fields[field], lengths[field]);
    }
    char expected_digest[65];
    store_test_sha256_final_hex(&expected_sha, expected_digest);

    cbm_syntactic_coverage_request_t request = {
        .project = "syntactic-nul",
        .mode = CBM_SYNTACTIC_COVERAGE_PROJECT,
        .selector = NULL,
        .offset = 0,
        .limit = 1,
        .detail_preview_bytes = 8,
    };
    cbm_syntactic_coverage_page_t page = {0};
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(s, &request, &page), CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_EQ(page.totals.rows_total, 1);
    ASSERT_STR_EQ(page.rows_sha256, expected_digest);
    cbm_store_syntactic_coverage_page_clear(&page);
    cbm_store_close(s);
    PASS();
}

typedef struct {
    cbm_store_t *writer;
    int rc;
} syntactic_snapshot_hook_ctx_t;

static void syntactic_snapshot_replace_after_totals(void *userdata) {
    syntactic_snapshot_hook_ctx_t *ctx = userdata;
    const cbm_coverage_row_t replacement[] = {
        {.rel_path = "new-a.rs", .kind = "not_indexed_file", .detail = "new-a"},
        {.rel_path = "new-b.rs", .kind = "not_indexed_file", .detail = "new-b"},
        {.rel_path = "new-b.rs", .kind = "analysis_failed:rust", .detail = "semantic"},
    };
    cbm_coverage_meta_t replacement_meta = {
        .generation = "generation-syntactic-new",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 1,
    };
    ctx->rc = cbm_store_coverage_replace_ex(ctx->writer, "syntactic-snapshot", replacement, 3,
                                            &replacement_meta);
}

TEST(store_syntactic_coverage_pages_share_outer_transaction_snapshot) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_syntactic_snapshot_XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(tmp));
    char db[512];
    snprintf(db, sizeof(db), "%s/graph.db", tmp);
    cbm_store_t *writer = cbm_store_open_path(db);
    ASSERT_NOT_NULL(writer);
    ASSERT_EQ(cbm_store_upsert_project(writer, "syntactic-snapshot", tmp), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(writer, "syntactic-snapshot", "new-b.rs", "", 1, 1),
              CBM_STORE_OK);
    const cbm_coverage_row_t initial[] = {
        {.rel_path = "old-a.rs", .kind = "not_indexed_file", .detail = "old-a"},
        {.rel_path = "old-b.rs", .kind = "not_indexed_file", .detail = "old-b"},
    };
    cbm_coverage_meta_t initial_meta = {
        .generation = "generation-syntactic-old",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 0,
    };
    ASSERT_EQ(
        cbm_store_coverage_replace_ex(writer, "syntactic-snapshot", initial, 2, &initial_meta),
        CBM_STORE_OK);
    cbm_store_t *reader = cbm_store_open_path_query(db);
    ASSERT_NOT_NULL(reader);
    ASSERT_EQ(cbm_store_begin(reader), CBM_STORE_OK);
    syntactic_snapshot_hook_ctx_t hook = {.writer = writer, .rc = CBM_STORE_ERR};
    cbm_store_syntactic_coverage_test_set_after_totals_hook(
        reader, syntactic_snapshot_replace_after_totals, &hook);
    cbm_syntactic_coverage_request_t request = {
        .project = "syntactic-snapshot",
        .mode = CBM_SYNTACTIC_COVERAGE_PROJECT,
        .selector = NULL,
        .offset = 0,
        .limit = 1,
        .detail_preview_bytes = 32,
    };
    cbm_syntactic_coverage_page_t first = {0};
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(reader, &request, &first),
              CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_EQ(hook.rc, CBM_STORE_OK);
    ASSERT_STR_EQ(first.meta.generation, "generation-syntactic-old");
    ASSERT_EQ(first.totals.rows_total, 2);
    ASSERT_STR_EQ(first.rows[0].rel_path, "old-a.rs");
    ASSERT_TRUE(first.has_more);
    char old_digest[65];
    snprintf(old_digest, sizeof(old_digest), "%s", first.rows_sha256);
    request.offset = first.next_offset;
    cbm_store_syntactic_coverage_page_clear(&first);

    cbm_store_syntactic_coverage_test_set_after_totals_hook(reader, NULL, NULL);
    cbm_syntactic_coverage_page_t second = {0};
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(reader, &request, &second),
              CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_STR_EQ(second.meta.generation, "generation-syntactic-old");
    ASSERT_STR_EQ(second.rows_sha256, old_digest);
    ASSERT_STR_EQ(second.rows[0].rel_path, "old-b.rs");
    ASSERT_FALSE(second.has_more);
    cbm_store_syntactic_coverage_page_clear(&second);
    ASSERT_EQ(cbm_store_commit(reader), CBM_STORE_OK);

    request.offset = 0;
    request.limit = 8;
    cbm_syntactic_coverage_page_t current = {0};
    ASSERT_EQ(cbm_store_syntactic_coverage_get_page(reader, &request, &current),
              CBM_SYNTACTIC_COVERAGE_OK);
    ASSERT_STR_EQ(current.meta.generation, "generation-syntactic-new");
    ASSERT_EQ(current.totals.rows_total, 2);
    ASSERT_STR_EQ(current.rows[0].rel_path, "new-a.rs");
    ASSERT_STR_EQ(current.rows[1].rel_path, "new-b.rs");
    cbm_store_syntactic_coverage_page_clear(&current);
    cbm_store_close(reader);
    cbm_store_close(writer);
    th_rmtree(tmp);
    PASS();
}

TEST(store_analysis_coverage_page_empty_current_meta_and_contract) {
    ASSERT_EQ(CBM_SEMANTIC_INDEX_VERSION, 4);
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "analysis-empty", "/tmp/analysis-empty"), CBM_STORE_OK);
    cbm_coverage_meta_t meta = {
        .generation = "generation-empty",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 0,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "analysis-empty", NULL, 0, &meta), CBM_STORE_OK);

    cbm_analysis_coverage_page_t page = {0};
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-empty", 0, 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_OK);
    ASSERT_TRUE(page.has_meta);
    ASSERT_EQ(page.meta.coverage_version, CBM_SEMANTIC_INDEX_VERSION);
    ASSERT_STR_EQ(page.meta.generation, "generation-empty");
    ASSERT_EQ(page.totals.rows_total, 0);
    ASSERT_EQ(page.totals.degraded_files_total, 0);
    ASSERT_EQ(page.returned, 0);
    ASSERT_FALSE(page.has_more);
    ASSERT_EQ(page.next_offset, 0);
    cbm_store_analysis_coverage_page_clear(&page);

    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-empty", -1, 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_INVALID_ARGUMENT);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(
                  s, "analysis-empty", 0, CBM_ANALYSIS_COVERAGE_PAGE_MAX_ROWS + 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_INVALID_ARGUMENT);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(
                  s, "analysis-empty", 0, 1, CBM_ANALYSIS_COVERAGE_DETAIL_MAX_BYTES + 1U, &page),
              CBM_ANALYSIS_COVERAGE_INVALID_ARGUMENT);
    cbm_store_close(s);
    PASS();
}

TEST(store_analysis_coverage_page_filters_orders_and_bounds_thousands) {
    enum {
        SYNTACTIC_ROWS = 3000,
        FAILED_FILES = 100,
        PARTIAL_ONLY_FILES = 100,
        UNSUPPORTED_FILES = 1,
        SEMANTIC_ROWS = FAILED_FILES * 2 + PARTIAL_ONLY_FILES + UNSUPPORTED_FILES,
        ALL_ROWS = SYNTACTIC_ROWS + SEMANTIC_ROWS,
        PATH_BYTES = 48,
        DETAIL_BYTES = 8192,
    };
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "analysis-bulk", "/tmp/analysis-bulk"), CBM_STORE_OK);
    cbm_coverage_row_t *rows = calloc(ALL_ROWS, sizeof(*rows));
    char (*paths)[PATH_BYTES] = calloc(ALL_ROWS, sizeof(*paths));
    char *detail = malloc(DETAIL_BYTES);
    ASSERT_NOT_NULL(rows);
    ASSERT_NOT_NULL(paths);
    ASSERT_NOT_NULL(detail);
    memset(detail, 'x', DETAIL_BYTES - 1);
    memset(detail, 'a', 30);
    detail[30] = (char)0xe2;
    detail[31] = (char)0x82;
    detail[32] = (char)0xac;
    detail[DETAIL_BYTES - 1] = '\0';

    int n = 0;
    for (int i = 0; i < SYNTACTIC_ROWS; i++) {
        snprintf(paths[n], PATH_BYTES, "syntactic/%04d.txt", i);
        rows[n] = (cbm_coverage_row_t){
            .rel_path = paths[n], .kind = "not_indexed_file", .detail = detail};
        n++;
    }
    for (int i = 0; i < FAILED_FILES + PARTIAL_ONLY_FILES + UNSUPPORTED_FILES; i++) {
        char live_path[PATH_BYTES];
        snprintf(live_path, sizeof(live_path), "rust/r%04d.rs", i);
        ASSERT_EQ(cbm_store_upsert_file_hash(s, "analysis-bulk", live_path, "", i + 1, 1),
                  CBM_STORE_OK);
        if (i < FAILED_FILES) {
            snprintf(paths[n], PATH_BYTES, "%s", live_path);
            rows[n] = (cbm_coverage_row_t){
                .rel_path = paths[n], .kind = "analysis_failed:rust", .detail = detail};
            n++;
        }
        snprintf(paths[n], PATH_BYTES, "%s", live_path);
        rows[n] = (cbm_coverage_row_t){.rel_path = paths[n],
                                       .kind = i < FAILED_FILES + PARTIAL_ONLY_FILES
                                                   ? "analysis_partial:rust"
                                                   : "analysis_future:rust",
                                       .detail = detail};
        n++;
    }
    ASSERT_EQ(n, ALL_ROWS);
    cbm_coverage_meta_t meta = {
        .generation = "generation-bulk",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = FAILED_FILES + PARTIAL_ONLY_FILES + UNSUPPORTED_FILES,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "analysis-bulk", rows, ALL_ROWS, &meta),
              CBM_STORE_OK);

    int64_t offset = 0;
    int observed = 0;
    do {
        cbm_analysis_coverage_page_t page = {0};
        ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-bulk", offset, 64, 31, &page),
                  CBM_ANALYSIS_COVERAGE_OK);
        ASSERT_TRUE(page.has_meta);
        ASSERT_EQ(page.totals.rows_total, SEMANTIC_ROWS);
        ASSERT_EQ(page.totals.partial_rows, FAILED_FILES + PARTIAL_ONLY_FILES);
        ASSERT_EQ(page.totals.failed_rows, FAILED_FILES);
        ASSERT_EQ(page.totals.unsupported_rows, UNSUPPORTED_FILES);
        ASSERT_EQ(page.totals.degraded_files_total,
                  FAILED_FILES + PARTIAL_ONLY_FILES + UNSUPPORTED_FILES);
        ASSERT_EQ(page.totals.failed_files, FAILED_FILES);
        ASSERT_EQ(page.totals.partial_files, PARTIAL_ONLY_FILES);
        ASSERT_EQ(page.totals.unsupported_files, UNSUPPORTED_FILES);
        for (int i = 0; i < page.returned; i++, observed++) {
            const cbm_analysis_coverage_row_t *row = &page.rows[i];
            char expected_path[PATH_BYTES];
            const char *expected_kind = NULL;
            if (observed < FAILED_FILES * 2) {
                snprintf(expected_path, sizeof(expected_path), "rust/r%04d.rs", observed / 2);
                expected_kind =
                    observed % 2 == 0 ? "analysis_failed:rust" : "analysis_partial:rust";
            } else if (observed < FAILED_FILES * 2 + PARTIAL_ONLY_FILES) {
                snprintf(expected_path, sizeof(expected_path), "rust/r%04d.rs",
                         observed - FAILED_FILES);
                expected_kind = "analysis_partial:rust";
            } else {
                snprintf(expected_path, sizeof(expected_path), "rust/r%04d.rs",
                         FAILED_FILES + PARTIAL_ONLY_FILES);
                expected_kind = "analysis_future:rust";
            }
            ASSERT_STR_EQ(row->rel_path, expected_path);
            ASSERT_STR_EQ(row->kind, expected_kind);
            ASSERT_EQ(strlen(row->detail), 30);
            ASSERT_EQ(row->detail_complete_bytes, DETAIL_BYTES - 1);
            ASSERT_TRUE(row->detail_truncated);
            ASSERT_EQ(strlen(row->detail_sha256), 64);
        }
        ASSERT_EQ(page.next_offset, offset + page.returned);
        bool has_more = page.has_more;
        offset = page.next_offset;
        cbm_store_analysis_coverage_page_clear(&page);
        if (!has_more) {
            break;
        }
    } while (true);
    ASSERT_EQ(observed, SEMANTIC_ROWS);
    ASSERT_EQ(offset, SEMANTIC_ROWS);

    free(detail);
    free(paths);
    free(rows);
    cbm_store_close(s);
    PASS();
}

TEST(store_analysis_coverage_page_reports_allocation_and_store_errors) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "analysis-errors", "/tmp/analysis-errors"), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "analysis-errors", "one.rs", "", 1, 1), CBM_STORE_OK);
    cbm_coverage_row_t row = {
        .rel_path = "one.rs", .kind = "analysis_partial:rust", .detail = "health"};
    cbm_coverage_meta_t meta = {
        .generation = "generation-errors",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 1,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "analysis-errors", &row, 1, &meta), CBM_STORE_OK);

    cbm_analysis_coverage_page_t page = {0};
    cbm_store_analysis_coverage_test_fail_alloc_after(s, 0);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-errors", 0, 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_ALLOCATION_FAILED);
    ASSERT_NULL(page.rows);
    ASSERT_FALSE(page.has_meta);
    cbm_store_analysis_coverage_test_fail_alloc_after(s, 6);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-errors", 0, 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_ALLOCATION_FAILED);
    ASSERT_NULL(page.rows);
    cbm_store_analysis_coverage_test_fail_alloc_after(s, 9);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-errors", 0, 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_ALLOCATION_FAILED);
    ASSERT_NULL(page.rows);
    cbm_store_analysis_coverage_test_fail_alloc_after(s, -1);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-errors", 0, 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_OK);
    ASSERT_EQ(page.returned, 1);
    cbm_store_analysis_coverage_page_clear(&page);

    ASSERT_EQ(cbm_store_exec(s, "DROP TABLE index_coverage;"), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-errors", 0, 1, 32, &page),
              CBM_ANALYSIS_COVERAGE_STORE_ERROR);
    cbm_store_close(s);
    PASS();
}

TEST(store_analysis_coverage_file_totals_use_exclusive_severity_precedence) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cbm_store_upsert_project(s, "analysis-precedence", "/tmp/analysis-precedence"),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "analysis-precedence", "failed.rs", "", 1, 1),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "analysis-precedence", "partial.rs", "", 2, 1),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(s, "analysis-precedence", "future.rs", "", 3, 1),
              CBM_STORE_OK);
    const cbm_coverage_row_t rows[] = {
        {.rel_path = "failed.rs", .kind = "analysis_failed:rust", .detail = "failed"},
        {.rel_path = "failed.rs", .kind = "analysis_future:rust", .detail = "future"},
        {.rel_path = "partial.rs", .kind = "analysis_partial:rust", .detail = "partial"},
        {.rel_path = "partial.rs", .kind = "analysis_future:rust", .detail = "future"},
        {.rel_path = "future.rs", .kind = "analysis_future:rust", .detail = "future"},
    };
    cbm_coverage_meta_t meta = {
        .generation = "generation-precedence",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 3,
    };
    ASSERT_EQ(cbm_store_coverage_replace_ex(s, "analysis-precedence", rows, 5, &meta),
              CBM_STORE_OK);

    cbm_analysis_coverage_page_t page = {0};
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(s, "analysis-precedence", 0, 8, 32, &page),
              CBM_ANALYSIS_COVERAGE_OK);
    ASSERT_EQ(page.totals.rows_total, 5);
    ASSERT_EQ(page.totals.failed_files, 1);
    ASSERT_EQ(page.totals.partial_files, 1);
    ASSERT_EQ(page.totals.unsupported_files, 1);
    ASSERT_EQ(page.totals.failed_files + page.totals.partial_files + page.totals.unsupported_files,
              page.totals.degraded_files_total);
    cbm_store_analysis_coverage_page_clear(&page);
    cbm_store_close(s);
    PASS();
}

typedef struct {
    cbm_store_t *writer;
    int rc;
} analysis_snapshot_hook_ctx_t;

static void analysis_snapshot_replace_after_totals(void *userdata) {
    analysis_snapshot_hook_ctx_t *ctx = userdata;
    cbm_coverage_row_t replacement = {
        .rel_path = "new.rs", .kind = "analysis_failed:rust", .detail = "new-health"};
    cbm_coverage_meta_t replacement_meta = {
        .generation = "generation-new",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 1,
    };
    ctx->rc = cbm_store_coverage_replace_ex(ctx->writer, "analysis-snapshot", &replacement, 1,
                                            &replacement_meta);
}

TEST(store_analysis_coverage_page_is_transaction_consistent) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_analysis_snapshot_XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(tmp));
    char db[512];
    snprintf(db, sizeof(db), "%s/graph.db", tmp);
    cbm_store_t *writer = cbm_store_open_path(db);
    ASSERT_NOT_NULL(writer);
    ASSERT_EQ(cbm_store_upsert_project(writer, "analysis-snapshot", tmp), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(writer, "analysis-snapshot", "old.rs", "", 1, 1),
              CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(writer, "analysis-snapshot", "new.rs", "", 1, 1),
              CBM_STORE_OK);
    cbm_coverage_row_t initial = {
        .rel_path = "old.rs", .kind = "analysis_partial:rust", .detail = "old-health"};
    cbm_coverage_meta_t initial_meta = {
        .generation = "generation-old",
        .index_mode = "full",
        .recording_status = "complete",
        .coverage_version = CBM_SEMANTIC_INDEX_VERSION,
        .hash_records_complete = true,
        .rust_analysis_recording_status = "complete",
        .rust_files_total = 1,
    };
    ASSERT_EQ(
        cbm_store_coverage_replace_ex(writer, "analysis-snapshot", &initial, 1, &initial_meta),
        CBM_STORE_OK);
    cbm_store_t *reader = cbm_store_open_path_query(db);
    ASSERT_NOT_NULL(reader);
    analysis_snapshot_hook_ctx_t hook_ctx = {.writer = writer, .rc = CBM_STORE_ERR};
    cbm_store_analysis_coverage_test_set_after_totals_hook(
        reader, analysis_snapshot_replace_after_totals, &hook_ctx);

    cbm_analysis_coverage_page_t page = {0};
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(reader, "analysis-snapshot", 0, 8, 64, &page),
              CBM_ANALYSIS_COVERAGE_OK);
    ASSERT_EQ(hook_ctx.rc, CBM_STORE_OK);
    ASSERT_STR_EQ(page.meta.generation, "generation-old");
    ASSERT_EQ(page.totals.rows_total, 1);
    ASSERT_EQ(page.returned, 1);
    ASSERT_STR_EQ(page.rows[0].rel_path, "old.rs");
    cbm_store_analysis_coverage_page_clear(&page);

    cbm_store_analysis_coverage_test_set_after_totals_hook(reader, NULL, NULL);
    ASSERT_EQ(cbm_store_analysis_coverage_get_page(reader, "analysis-snapshot", 0, 8, 64, &page),
              CBM_ANALYSIS_COVERAGE_OK);
    ASSERT_STR_EQ(page.meta.generation, "generation-new");
    ASSERT_EQ(page.totals.rows_total, 1);
    ASSERT_EQ(page.returned, 1);
    ASSERT_STR_EQ(page.rows[0].rel_path, "new.rs");
    cbm_store_analysis_coverage_page_clear(&page);

    cbm_store_close(reader);
    cbm_store_close(writer);
    th_rmtree(tmp);
    PASS();
}

SUITE(store_nodes) {
    RUN_TEST(store_coverage_roundtrip_prune_shadow);
    RUN_TEST(store_coverage_targeted_path_and_scope_lookup);
    RUN_TEST(store_coverage_meta_zero_row_truncation_and_delete);
    RUN_TEST(store_coverage_replace_rejects_invalid_row_arguments);
    RUN_TEST(store_coverage_replace_rolls_back_when_shadow_rebuild_fails);
    RUN_TEST(store_analysis_rows_do_not_rebuild_or_materialize_missed_graph);
    RUN_TEST(store_syntactic_coverage_project_pages_filter_order_digest_and_bound_thousands);
    RUN_TEST(store_syntactic_coverage_exact_and_scope_use_segment_safe_ancestors);
    RUN_TEST(store_syntactic_coverage_empty_meta_invalid_and_allocation_statuses);
    RUN_TEST(store_syntactic_coverage_digest_frames_embedded_nul_fields);
    RUN_TEST(store_syntactic_coverage_pages_share_outer_transaction_snapshot);
    RUN_TEST(store_analysis_coverage_page_empty_current_meta_and_contract);
    RUN_TEST(store_analysis_coverage_page_filters_orders_and_bounds_thousands);
    RUN_TEST(store_analysis_coverage_page_reports_allocation_and_store_errors);
    RUN_TEST(store_analysis_coverage_file_totals_use_exclusive_severity_precedence);
    RUN_TEST(store_analysis_coverage_page_is_transaction_consistent);
    RUN_TEST(sql_label_allowlists_match_cbm_label_is_type_like);
    RUN_TEST(sql_relation_labels_match_cbm_label_is_relation);
    RUN_TEST(store_open_memory);
    RUN_TEST(store_close_null);
    RUN_TEST(store_open_memory_twice);
    RUN_TEST(store_integrity_clean);
    RUN_TEST(store_integrity_empty);
    RUN_TEST(store_integrity_corrupt_bad_path);
    RUN_TEST(store_integrity_windows_lowercase_drive_issue367);
    RUN_TEST(store_integrity_corrupt_too_many_rows);
    RUN_TEST(store_integrity_verdict_healthy_is_ok);
    RUN_TEST(store_integrity_verdict_real_corruption_is_corrupt);
    RUN_TEST(store_integrity_verdict_unopenable_is_transient_not_corrupt);
    RUN_TEST(store_integrity_null_check);
    RUN_TEST(store_project_crud);
    RUN_TEST(store_project_update);
    RUN_TEST(store_project_delete);
    RUN_TEST(store_node_crud);
    RUN_TEST(store_node_dedup);
    RUN_TEST(store_node_find_by_label);
    RUN_TEST(store_node_find_by_file);
    RUN_TEST(store_file_outline_is_filtered_stable_bounded_and_cancellable_issue469);
    RUN_TEST(store_file_outline_fails_closed_on_text_budget_issue469);
    RUN_TEST(store_node_find_not_found);
    RUN_TEST(store_node_count_empty);
    RUN_TEST(store_node_delete_by_file);
    RUN_TEST(store_node_delete_by_label);
    RUN_TEST(store_node_batch_upsert);
    RUN_TEST(store_node_batch_empty);
    RUN_TEST(store_cascade_delete);
    RUN_TEST(store_file_hash_crud);
    RUN_TEST(store_lsp_surface_round_trip);
    RUN_TEST(store_dependent_files_lookup);
    RUN_TEST(store_file_hash_upsert_rejects_null_required_fields);
    RUN_TEST(store_node_properties_json);
    RUN_TEST(store_node_null_properties);
    RUN_TEST(store_find_by_file_overlap);
    RUN_TEST(store_find_by_qn_suffix_single);
    RUN_TEST(store_find_by_qn_suffix_no_match);
    RUN_TEST(store_find_by_qn_suffix_multiple);
    RUN_TEST(store_find_by_qn_suffix_dot_boundary);
    RUN_TEST(store_node_degree);
    RUN_TEST(store_file_hash_batch);
    RUN_TEST(store_file_hash_batch_equals_loop);
    RUN_TEST(store_find_edges_by_url_path);
    RUN_TEST(store_restore_from);
    RUN_TEST(store_pragma_settings);
    RUN_TEST(store_find_node_ids_by_qns);
    RUN_TEST(store_node_null_project);
    RUN_TEST(store_node_null_qn);
    RUN_TEST(store_node_empty_strings);
    RUN_TEST(store_find_by_id_not_found);
    RUN_TEST(store_find_by_qn_not_found);
    RUN_TEST(store_find_by_qn_any_cross_project);
    RUN_TEST(store_find_by_name_any_cross_project);
    RUN_TEST(store_find_by_file_no_match);
    RUN_TEST(store_node_batch_upsert_zero);
    RUN_TEST(store_node_batch_upsert_100);
    RUN_TEST(store_delete_by_label_verify_remaining);
    RUN_TEST(store_delete_by_file_verify_remaining);
    RUN_TEST(store_node_upsert_updates_fields);
    RUN_TEST(store_node_long_qn);
    RUN_TEST(store_node_properties_special_chars);
    RUN_TEST(store_delete_nodes_nonexistent);
    RUN_TEST(store_count_nodes_unknown_project);
}
