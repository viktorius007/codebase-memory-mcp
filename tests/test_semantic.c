/*
 * test_semantic.c — Unit tests for semantic.c (pure functions).
 *
 * Covers: tokenize, cosine, normalize, vec_add_scaled, random_index,
 * proximity, diffuse, corpus lifecycle, get_config.
 */
#include "test_framework.h"
#include "../src/foundation/compat.h"
#include "../src/foundation/compat_thread.h"
#include "test_helpers.h"
#include <pipeline/definition_properties.h>
#include <pipeline/pipeline.h>
#include <pipeline/pipeline_internal.h>
#include <semantic/rotsq.h>
#include <semantic/semantic.h>
#include <simhash/minhash.h>
#include <store/store.h>
#include <sqlite3.h>

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(CBM_ENABLE_TEST_SEAMS) && CBM_ENABLE_TEST_SEAMS
extern bool cbm_semantic_test_body_field_contains_token(const char *json, const char *expected);
extern bool cbm_semantic_test_body_field_injects_token(const char *json, const char *expected);
extern void cbm_parallel_test_property_serialization_failures_reset(void);
extern long cbm_parallel_test_property_serialization_failures(void);
#endif

/* ── Tokenize ────────────────────────────────────────────────────── */

TEST(sem_tokenize_camel) {
    char *tokens[32];
    int n = cbm_sem_tokenize("parseUserInput", tokens, 32);
    ASSERT_GTE(n, 3);
    ASSERT_STR_EQ(tokens[0], "parse");
    ASSERT_STR_EQ(tokens[1], "user");
    ASSERT_STR_EQ(tokens[2], "input");
    for (int i = 0; i < n; i++)
        free(tokens[i]);
    PASS();
}

TEST(sem_tokenize_snake) {
    char *tokens[32];
    int n = cbm_sem_tokenize("handle_http_request", tokens, 32);
    ASSERT_GTE(n, 3);
    ASSERT_STR_EQ(tokens[0], "handle");
    ASSERT_STR_EQ(tokens[1], "http");
    ASSERT_STR_EQ(tokens[2], "request");
    for (int i = 0; i < n; i++)
        free(tokens[i]);
    PASS();
}

TEST(sem_tokenize_dot) {
    char *tokens[32];
    int n = cbm_sem_tokenize("net.http.client", tokens, 32);
    ASSERT_GTE(n, 3);
    ASSERT_STR_EQ(tokens[0], "net");
    ASSERT_STR_EQ(tokens[1], "http");
    ASSERT_STR_EQ(tokens[2], "client");
    for (int i = 0; i < n; i++)
        free(tokens[i]);
    PASS();
}

TEST(sem_tokenize_null) {
    int n = cbm_sem_tokenize(NULL, NULL, 0);
    ASSERT_EQ(n, 0);
    PASS();
}

TEST(sem_tokenize_max_out) {
    char *tokens[3];
    int n = cbm_sem_tokenize("a_b_c_d_e_f_g", tokens, 3);
    ASSERT_EQ(n, 3);
    for (int i = 0; i < n; i++)
        free(tokens[i]);
    PASS();
}

TEST(sem_tokenize_abbrev_expansion) {
    char *tokens[32];
    int n = cbm_sem_tokenize("getCtxErrMsg", tokens, 32);
    /* get, ctx, context, err, error, msg, message */
    ASSERT_GTE(n, 4);
    bool has_ctx = false, has_context = false, has_err = false, has_error = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(tokens[i], "ctx") == 0)
            has_ctx = true;
        if (strcmp(tokens[i], "context") == 0)
            has_context = true;
        if (strcmp(tokens[i], "err") == 0)
            has_err = true;
        if (strcmp(tokens[i], "error") == 0)
            has_error = true;
    }
    ASSERT_TRUE(has_ctx && has_context && has_err && has_error);
    for (int i = 0; i < n; i++)
        free(tokens[i]);
    PASS();
}

static int write_project_identity_fixture(const char *path, const char *profile_record_name,
                                          const char *profile_table_name) {
    FILE *file = cbm_fopen(path, "wb");
    if (!file) {
        return -1;
    }
    int written = fprintf(file,
                          "def sanitize(value: str) -> str:\n"
                          "    return value.strip().lower()\n\n"
                          "def lookup(table: dict, key: str) -> str:\n"
                          "    return table.get(key, \"\")\n\n"
                          "def audit_log(message: str) -> None:\n"
                          "    print(message)\n\n"
                          "def normalize_user_record(record: dict, table: dict) -> dict:\n"
                          "    result = {}\n"
                          "    name = sanitize(record.get(\"name\", \"\"))\n"
                          "    email = sanitize(record.get(\"email\", \"\"))\n"
                          "    role = lookup(table, name)\n"
                          "    if name and email:\n"
                          "        result[\"name\"] = name\n"
                          "        result[\"email\"] = email\n"
                          "        result[\"role\"] = role\n"
                          "        audit_log(\"normalized user record\")\n"
                          "    return result\n\n"
                          "def normalize_account_record(record: dict, table: dict) -> dict:\n"
                          "    result = {}\n"
                          "    name = sanitize(record.get(\"name\", \"\"))\n"
                          "    email = sanitize(record.get(\"email\", \"\"))\n"
                          "    role = lookup(table, name)\n"
                          "    while name and email:\n"
                          "        result[\"name\"] = name\n"
                          "        result[\"email\"] = email\n"
                          "        result[\"role\"] = role\n"
                          "        audit_log(\"normalized account record\")\n"
                          "        break\n"
                          "    return result\n\n"
                          "def normalize_member_record(record: dict, table: dict) -> dict:\n"
                          "    result = {}\n"
                          "    name = sanitize(record.get(\"name\", \"\"))\n"
                          "    email = sanitize(record.get(\"email\", \"\"))\n"
                          "    role = lookup(table, name)\n"
                          "    for _ in range(1):\n"
                          "        if not (name and email):\n"
                          "            continue\n"
                          "        result[\"name\"] = name\n"
                          "        result[\"email\"] = email\n"
                          "        result[\"role\"] = role\n"
                          "        audit_log(\"normalized member record\")\n"
                          "    return result\n\n"
                          "def normalize_profile_record(%s: dict, %s: dict) -> dict:\n"
                          "    result = {}\n"
                          "    name = sanitize(%s.get(\"name\", \"\"))\n"
                          "    email = sanitize(%s.get(\"email\", \"\"))\n"
                          "    role = lookup(%s, name)\n"
                          "    try:\n"
                          "        assert name and email\n"
                          "        result[\"name\"] = name\n"
                          "        result[\"email\"] = email\n"
                          "        result[\"role\"] = role\n"
                          "        audit_log(\"normalized profile record\")\n"
                          "    except AssertionError:\n"
                          "        audit_log(\"skipped profile record\")\n"
                          "    return result\n",
                          profile_record_name, profile_table_name, profile_record_name,
                          profile_record_name, profile_table_name);
    return fclose(file) == 0 && written > 0 ? 0 : -1;
}

static char *semantic_edge_fingerprint(const char *db_path, const char *project, int *edge_count) {
    cbm_store_t *store = cbm_store_open_path(db_path);
    if (!store) {
        return NULL;
    }
    sqlite3 *db = cbm_store_get_db(store);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT substr(s.qualified_name, length(?1) + 2), "
        "substr(t.qualified_name, length(?1) + 2), e.properties "
        "FROM edges e JOIN nodes s ON s.id=e.source_id JOIN nodes t ON t.id=e.target_id "
        "WHERE e.project=?1 AND e.type='SEMANTICALLY_RELATED' ORDER BY 1,2,3";
    if (!db || sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        cbm_store_close(store);
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
    size_t capacity = 4096;
    size_t length = 0;
    char *result = malloc(capacity);
    if (!result) {
        sqlite3_finalize(stmt);
        cbm_store_close(store);
        return NULL;
    }
    result[0] = '\0';
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *source = (const char *)sqlite3_column_text(stmt, 0);
        const char *target = (const char *)sqlite3_column_text(stmt, 1);
        const char *properties = (const char *)sqlite3_column_text(stmt, 2);
        source = source ? source : "";
        target = target ? target : "";
        properties = properties ? properties : "";
        size_t needed = strlen(source) + strlen(target) + strlen(properties) + 4;
        while (length + needed >= capacity) {
            capacity *= 2;
            char *grown = realloc(result, capacity);
            if (!grown) {
                free(result);
                sqlite3_finalize(stmt);
                cbm_store_close(store);
                return NULL;
            }
            result = grown;
        }
        length += (size_t)snprintf(result + length, capacity - length, "%s|%s|%s\n", source, target,
                                   properties);
        count++;
    }
    sqlite3_finalize(stmt);
    cbm_store_close(store);
    *edge_count = count;
    return result;
}

static char *index_semantic_fixture(const char *repo_path, const char *db_path, const char *project,
                                    int *edge_count) {
    cbm_pipeline_t *pipeline = cbm_pipeline_new(repo_path, db_path, CBM_MODE_FULL);
    if (!pipeline || !cbm_pipeline_set_project_name(pipeline, project)) {
        cbm_pipeline_free(pipeline);
        return NULL;
    }
    int run_rc = cbm_pipeline_run(pipeline);
    cbm_pipeline_free(pipeline);
    return run_rc == 0 ? semantic_edge_fingerprint(db_path, project, edge_count) : NULL;
}

/* Project names are storage identities, not code. This exact pipeline fixture
 * proves that renaming one project cannot change semantic edge identities,
 * scores, or properties, while a real source-symbol change still can. */
TEST(sem_project_identity_is_not_semantic_input) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_sem_project_identity_XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(tmp));
    char repo[512], source[512], db_a[512], db_b[512], db_changed[512];
    snprintf(repo, sizeof(repo), "%s/repo", tmp);
    snprintf(source, sizeof(source), "%s/records.py", repo);
    snprintf(db_a, sizeof(db_a), "%s/alpha.db", tmp);
    snprintf(db_b, sizeof(db_b), "%s/many.db", tmp);
    snprintf(db_changed, sizeof(db_changed), "%s/changed.db", tmp);
    ASSERT_EQ(cbm_mkdir(repo), 0);
    ASSERT_EQ(write_project_identity_fixture(source, "record", "table"), 0);

    const char *old_enabled = getenv("CBM_SEMANTIC_ENABLED");
    char *saved_enabled = old_enabled ? strdup(old_enabled) : NULL;
    const char *old_threshold = getenv("CBM_SEMANTIC_THRESHOLD");
    char *saved_threshold = old_threshold ? strdup(old_threshold) : NULL;
    cbm_setenv("CBM_SEMANTIC_ENABLED", "1", 1);
    cbm_setenv("CBM_SEMANTIC_THRESHOLD", "0.8395", 1);

    int alpha_count = 0, renamed_count = 0, changed_count = 0;
    char *alpha = index_semantic_fixture(repo, db_a, "alpha", &alpha_count);
    char *renamed =
        index_semantic_fixture(repo, db_b, "many-prefix-tokens-change-context", &renamed_count);
    int content_write_rc =
        write_project_identity_fixture(source, "archived_snapshot", "vault_catalog");
    char *changed = content_write_rc == 0
                        ? index_semantic_fixture(repo, db_changed, "alpha", &changed_count)
                        : NULL;

    if (saved_enabled) {
        cbm_setenv("CBM_SEMANTIC_ENABLED", saved_enabled, 1);
    } else {
        cbm_unsetenv("CBM_SEMANTIC_ENABLED");
    }
    if (saved_threshold) {
        cbm_setenv("CBM_SEMANTIC_THRESHOLD", saved_threshold, 1);
    } else {
        cbm_unsetenv("CBM_SEMANTIC_THRESHOLD");
    }
    free(saved_enabled);
    free(saved_threshold);

    bool project_invariant =
        alpha && renamed && alpha_count > 0 && renamed_count > 0 && strcmp(alpha, renamed) == 0;
    bool content_sensitive = alpha && changed && changed_count > 0 && strcmp(alpha, changed) != 0;
    free(alpha);
    free(renamed);
    free(changed);
    th_rmtree(tmp);

    ASSERT_TRUE(project_invariant);
    ASSERT_TRUE(content_sensitive);
    PASS();
}

static void fill_body_tokens(char *tokens, size_t length) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz_";
    for (size_t i = 0; i < length; i++) {
        tokens[i] = alphabet[i % (sizeof(alphabet) - 1)];
    }
    tokens[length] = '\0';
}

/* Cargo's ManRenderer::push_man landed exactly on the old 2 KiB property
 * boundary: a longer storage project prefix pushed its 1,020-byte body-token
 * bag out of the node. The compact push_top_header carrier is the control arm
 * where the old mechanism cannot fire. */
TEST(sem_definition_properties_preserve_cargo_boundary) {
    uint32_t fingerprint[CBM_MINHASH_K] = {0};
    char push_man_tokens[1021];
    char push_top_header_tokens[105];
    fill_body_tokens(push_man_tokens, sizeof(push_man_tokens) - 1);
    fill_body_tokens(push_top_header_tokens, sizeof(push_top_header_tokens) - 1);

    CBMDefinition push_man = {
        .name = "push_man",
        .label = "Method",
        .signature = "(&mut self)",
        .return_type = "Result<(), Error>",
        .parent_class = "a.crates.mdman.src.format.man.ManRenderer",
        .complexity = 79,
        .cognitive = 414,
        .loop_count = 1,
        .loop_depth = 1,
        .param_count = 1,
        .max_access_depth = 2,
        .alloc_in_loop = 5,
        .fingerprint = fingerprint,
        .fingerprint_k = CBM_MINHASH_K,
        .is_exported = true,
        .structural_profile = "11,0,1,8,10,0,28,160,3,3,0,11,33,7,0,0,0,0,11,8,4,120,390,0,390",
        .body_tokens = push_man_tokens,
    };
    cbm_def_properties_t short_props = {0}, long_props = {0};
    ASSERT_EQ(cbm_def_properties_build(&push_man, &short_props), CBM_DEF_PROPERTIES_OK);
    push_man.parent_class =
        "many-prefix-tokens-change-context.crates.mdman.src.format.man.ManRenderer";
    ASSERT_EQ(cbm_def_properties_build(&push_man, &long_props), CBM_DEF_PROPERTIES_OK);
    ASSERT_EQ(short_props.length, 2025);
    ASSERT_EQ(long_props.length, 2057);
    ASSERT_NOT_NULL(strstr(short_props.json, "\"bt\":\""));
    ASSERT_NOT_NULL(strstr(long_props.json, "\"bt\":\""));
    ASSERT_NOT_NULL(strstr(short_props.json, push_man_tokens));
    ASSERT_NOT_NULL(strstr(long_props.json, push_man_tokens));
    cbm_def_properties_destroy(&short_props);
    cbm_def_properties_destroy(&long_props);

    CBMDefinition control = push_man;
    control.name = "push_top_header";
    control.parent_class = "a.crates.mdman.src.format.man.ManRenderer";
    control.complexity = 0;
    control.cognitive = 0;
    control.loop_count = 0;
    control.loop_depth = 0;
    control.alloc_in_loop = 0;
    control.structural_profile = "0,0,0,0,3,0,7,40,0,0,0,0,2,0,0,0,0,0,0,2,2,7,16,0,16";
    control.body_tokens = push_top_header_tokens;
    ASSERT_EQ(cbm_def_properties_build(&control, &short_props), CBM_DEF_PROPERTIES_OK);
    control.parent_class =
        "many-prefix-tokens-change-context.crates.mdman.src.format.man.ManRenderer";
    ASSERT_EQ(cbm_def_properties_build(&control, &long_props), CBM_DEF_PROPERTIES_OK);
    ASSERT_EQ(short_props.length, 1095);
    ASSERT_EQ(long_props.length, 1127);
    ASSERT_NOT_NULL(strstr(short_props.json, push_top_header_tokens));
    ASSERT_NOT_NULL(strstr(long_props.json, push_top_header_tokens));
    cbm_def_properties_destroy(&short_props);
    cbm_def_properties_destroy(&long_props);
    PASS();
}

/* The persisted body-token carrier is 2 KiB. The semantic consumer must not
 * silently reduce that contract to 512 bytes: a source token near the tail is
 * still content and must reach the exact production tokenizer. */
TEST(sem_body_tokens_consume_complete_bounded_carrier) {
    char json[CBM_SZ_2K];
    size_t used = (size_t)snprintf(json, sizeof(json), "{\"bt\":\"");
    int written = snprintf(json + used, sizeof(json) - used, "quoted\\\"identifier ");
    ASSERT_GT(written, 0);
    ASSERT_LT((size_t)written, sizeof(json) - used);
    used += (size_t)written;
    for (int i = 0; i < 80; i++) {
        written = snprintf(json + used, sizeof(json) - used, "padding%02d ", i);
        ASSERT_GT(written, 0);
        ASSERT_LT((size_t)written, sizeof(json) - used);
        used += (size_t)written;
    }
    ASSERT_GT(used, CBM_SZ_512);
    ASSERT_LT(snprintf(json + used, sizeof(json) - used, "semantic_tail_marker raise\"}"),
              (int)(sizeof(json) - used));
    ASSERT_TRUE(cbm_semantic_test_body_field_contains_token(json, "semantic"));
    ASSERT_TRUE(cbm_semantic_test_body_field_contains_token(json, "tail"));
    ASSERT_TRUE(cbm_semantic_test_body_field_contains_token(json, "marker"));
    ASSERT_TRUE(cbm_semantic_test_body_field_injects_token(json, "throw"));
    PASS();
}

TEST(sem_definition_properties_fail_closed) {
    CBMDefinition def = {.name = "bounded", .label = "Function"};
    cbm_def_properties_t props = {0};
    cbm_def_properties_test_fail_allocation_once();
    ASSERT_EQ(cbm_def_properties_build(&def, &props), CBM_DEF_PROPERTIES_ALLOCATION_UNAVAILABLE);
    ASSERT_NULL(props.json);

    char *oversized = malloc(CBM_DEF_PROPERTIES_MAX_BYTES + 1U);
    ASSERT_NOT_NULL(oversized);
    memset(oversized, 'x', CBM_DEF_PROPERTIES_MAX_BYTES);
    oversized[CBM_DEF_PROPERTIES_MAX_BYTES] = '\0';
    def.signature = oversized;
    ASSERT_EQ(cbm_def_properties_build(&def, &props), CBM_DEF_PROPERTIES_OVERSIZE);
    ASSERT_NULL(props.json);
    free(oversized);

    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_sem_props_fail_closed_XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(tmp));
    char repo[512], source[512], db[512];
    snprintf(repo, sizeof(repo), "%s/repo", tmp);
    snprintf(source, sizeof(source), "%s/simple.py", repo);
    snprintf(db, sizeof(db), "%s/simple.db", tmp);
    ASSERT_EQ(cbm_mkdir(repo), 0);
    FILE *file = cbm_fopen(source, "wb");
    ASSERT_NOT_NULL(file);
    ASSERT_GT(fprintf(file, "def bounded(value):\n    return value\n"), 0);
    ASSERT_EQ(fclose(file), 0);
    cbm_pipeline_t *pipeline = cbm_pipeline_new(repo, db, CBM_MODE_FULL);
    ASSERT_NOT_NULL(pipeline);
    cbm_def_properties_test_fail_allocation_once();
    ASSERT_EQ(cbm_pipeline_run(pipeline), CBM_PIPELINE_ABORT_PRESERVE_DB);
    cbm_pipeline_free(pipeline);

    /* Force the production parallel extraction route. One worker consumes the
     * allocation fault; the shared latch must stop the generation and the
     * pipeline must fail closed instead of publishing a partial index. */
    for (int i = 0; i < 51; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/parallel_%02d.py", repo, i);
        file = cbm_fopen(path, "wb");
        ASSERT_NOT_NULL(file);
        ASSERT_GT(fprintf(file, "def parallel_%02d(value):\n    return value + %d\n", i, i), 0);
        ASSERT_EQ(fclose(file), 0);
    }
    char parallel_db[512];
    snprintf(parallel_db, sizeof(parallel_db), "%s/parallel.db", tmp);
    pipeline = cbm_pipeline_new(repo, parallel_db, CBM_MODE_FULL);
    ASSERT_NOT_NULL(pipeline);

    const char *old_workers = getenv("CBM_WORKERS");
    const char *old_single_thread = getenv("CBM_INDEX_SINGLE_THREAD");
    char *saved_workers = old_workers ? strdup(old_workers) : NULL;
    char *saved_single_thread = old_single_thread ? strdup(old_single_thread) : NULL;
    ASSERT_TRUE(!old_workers || saved_workers);
    ASSERT_TRUE(!old_single_thread || saved_single_thread);
    int set_workers_rc = cbm_setenv("CBM_WORKERS", "4", 1);
    int unset_single_thread_rc = set_workers_rc == 0 ? cbm_unsetenv("CBM_INDEX_SINGLE_THREAD") : -1;
    int parallel_rc = -1;
    long parallel_failures = 0;
    if (set_workers_rc == 0 && unset_single_thread_rc == 0) {
        cbm_parallel_test_property_serialization_failures_reset();
        cbm_def_properties_test_fail_allocation_once();
        parallel_rc = cbm_pipeline_run(pipeline);
        parallel_failures = cbm_parallel_test_property_serialization_failures();
    }
    cbm_pipeline_free(pipeline);
    int restore_workers_rc =
        saved_workers ? cbm_setenv("CBM_WORKERS", saved_workers, 1) : cbm_unsetenv("CBM_WORKERS");
    int restore_single_thread_rc =
        saved_single_thread ? cbm_setenv("CBM_INDEX_SINGLE_THREAD", saved_single_thread, 1)
                            : cbm_unsetenv("CBM_INDEX_SINGLE_THREAD");
    free(saved_workers);
    free(saved_single_thread);
    th_rmtree(tmp);

    ASSERT_EQ(set_workers_rc, 0);
    ASSERT_EQ(unset_single_thread_rc, 0);
    ASSERT_EQ(restore_workers_rc, 0);
    ASSERT_EQ(restore_single_thread_rc, 0);
    ASSERT_GT(parallel_failures, 0);
    ASSERT_EQ(parallel_rc, CBM_PIPELINE_ABORT_PRESERVE_DB);
    PASS();
}

/* ── Cosine similarity ───────────────────────────────────────────── */

static void fill_vec(cbm_sem_vec_t *v, float val) {
    for (int i = 0; i < CBM_SEM_DIM; i++)
        v->v[i] = val;
}

TEST(sem_cosine_identical) {
    cbm_sem_vec_t a, b;
    fill_vec(&a, 0.5f);
    fill_vec(&b, 0.5f);
    float sim = cbm_sem_cosine(&a, &b);
    ASSERT_FLOAT_EQ(sim, 1.0f, 0.001f);
    PASS();
}

TEST(sem_cosine_orthogonal) {
    cbm_sem_vec_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.v[0] = 1.0f;
    b.v[1] = 1.0f;
    float sim = cbm_sem_cosine(&a, &b);
    ASSERT_FLOAT_EQ(sim, 0.0f, 0.001f);
    PASS();
}

TEST(sem_cosine_zero_vector) {
    cbm_sem_vec_t a, b;
    memset(&a, 0, sizeof(a));
    fill_vec(&b, 1.0f);
    float sim = cbm_sem_cosine(&a, &b);
    ASSERT_FLOAT_EQ(sim, 0.0f, 0.001f);
    PASS();
}

TEST(sem_cosine_negative) {
    cbm_sem_vec_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.v[0] = 1.0f;
    b.v[0] = -1.0f;
    float sim = cbm_sem_cosine(&a, &b);
    ASSERT_FLOAT_EQ(sim, -1.0f, 0.001f);
    PASS();
}

TEST(sem_cosine_null) {
    ASSERT_FLOAT_EQ(cbm_sem_cosine(NULL, NULL), 0.0f, 0.001f);
    PASS();
}

/* ── Normalize ───────────────────────────────────────────────────── */

TEST(sem_normalize_unit) {
    cbm_sem_vec_t v;
    memset(&v, 0, sizeof(v));
    v.v[0] = 1.0f;
    cbm_sem_normalize(&v);
    ASSERT_FLOAT_EQ(cbm_sem_cosine(&v, &v), 1.0f, 0.001f);
    PASS();
}

TEST(sem_normalize_scales) {
    cbm_sem_vec_t v;
    fill_vec(&v, 2.0f);
    cbm_sem_normalize(&v);
    float mag_sq = 0.0f;
    for (int i = 0; i < CBM_SEM_DIM; i++)
        mag_sq += v.v[i] * v.v[i];
    float mag = sqrtf(mag_sq);
    ASSERT_FLOAT_EQ(mag, 1.0f, 0.01f);
    PASS();
}

TEST(sem_normalize_zero) {
    cbm_sem_vec_t v;
    memset(&v, 0, sizeof(v));
    cbm_sem_normalize(&v);
    /* Should remain zero (no division by zero) */
    PASS();
}

TEST(sem_normalize_null) {
    cbm_sem_normalize(NULL); /* should not crash */
    PASS();
}

/* ── Vec add scaled ──────────────────────────────────────────────── */

TEST(sem_vec_add_scaled_basic) {
    cbm_sem_vec_t dst;
    memset(&dst, 0, sizeof(dst));
    cbm_sem_vec_t src;
    fill_vec(&src, 1.0f);
    cbm_sem_vec_add_scaled(&dst, &src, 0.5f);
    ASSERT_FLOAT_EQ(dst.v[0], 0.5f, 0.001f);
    ASSERT_FLOAT_EQ(dst.v[CBM_SEM_DIM - 1], 0.5f, 0.001f);
    PASS();
}

TEST(sem_vec_add_scaled_null) {
    cbm_sem_vec_t v;
    fill_vec(&v, 1.0f);
    cbm_sem_vec_add_scaled(NULL, &v, 1.0f); /* should not crash */
    cbm_sem_vec_add_scaled(&v, NULL, 1.0f); /* should not crash */
    PASS();
}

/* ── Random index ────────────────────────────────────────────────── */

TEST(sem_random_index_deterministic) {
    cbm_sem_vec_t a, b;
    cbm_sem_random_index("hello", &a);
    cbm_sem_random_index("hello", &b);
    ASSERT_FLOAT_EQ(cbm_sem_cosine(&a, &b), 1.0f, 0.001f);
    PASS();
}

TEST(sem_random_index_different_tokens) {
    cbm_sem_vec_t a, b;
    cbm_sem_random_index("function", &a);
    cbm_sem_random_index("variable", &b);
    /* Different tokens should produce different vectors */
    float sim = cbm_sem_cosine(&a, &b);
    ASSERT_TRUE(sim < 1.0f - 1e-6f);
    PASS();
}

TEST(sem_random_index_null) {
    cbm_sem_vec_t v;
    memset(&v, 0, sizeof(v));
    cbm_sem_random_index(NULL, &v);
    /* Should produce zero vector for NULL token */
    for (int i = 0; i < CBM_SEM_DIM; i++) {
        ASSERT_FLOAT_EQ(v.v[i], 0.0f, 0.001f);
    }
    PASS();
}

/* ── Proximity ───────────────────────────────────────────────────── */

TEST(sem_proximity_same_file) {
    float p = cbm_sem_proximity("src/main.c", "src/main.c");
    ASSERT_FLOAT_EQ(p, 1.1f, 0.01f); /* CBM_SEM_UNIT_POS + CBM_SEM_PROX_MAX_BOOST */
    PASS();
}

TEST(sem_proximity_same_dir) {
    /* Files sharing 1 of 2 directory components: ratio = 0.5 → 1.0 + 0.5*0.10 = 1.05 */
    float p = cbm_sem_proximity("src/core/a.c", "src/io/b.c");
    ASSERT_TRUE(p > 1.0f && p < 1.10f);
    PASS();
}

TEST(sem_proximity_different_paths) {
    float p = cbm_sem_proximity("src/foo/a.c", "tests/bar/b.c");
    ASSERT_FLOAT_EQ(p, 1.0f, 0.01f);
    PASS();
}

TEST(sem_proximity_null) {
    ASSERT_FLOAT_EQ(cbm_sem_proximity(NULL, "foo.c"), 1.0f, 0.01f);
    ASSERT_FLOAT_EQ(cbm_sem_proximity("foo.c", NULL), 1.0f, 0.01f);
    PASS();
}

/* ── Diffuse ─────────────────────────────────────────────────────── */

TEST(sem_diffuse_zero_neighbors) {
    cbm_sem_vec_t v;
    fill_vec(&v, 0.5f);
    cbm_sem_diffuse(&v, NULL, 0, 0.3f);
    /* With zero neighbors, vector should be unchanged */
    ASSERT_FLOAT_EQ(v.v[0], 0.5f, 0.001f);
    PASS();
}

TEST(sem_diffuse_single_neighbor) {
    cbm_sem_vec_t v;
    memset(&v, 0, sizeof(v));
    v.v[0] = 0.5f;
    v.v[1] = 0.5f;
    cbm_sem_normalize(&v); /* unit-length input */
    cbm_sem_vec_t nb;
    memset(&nb, 0, sizeof(nb));
    nb.v[0] = 1.0f;
    cbm_sem_normalize(&nb);
    cbm_sem_diffuse(&v, &nb, 1, 0.3f);
    /* After diffuse+normalize, result should still be unit-length */
    float mag_sq = 0.0f;
    for (int i = 0; i < CBM_SEM_DIM; i++)
        mag_sq += v.v[i] * v.v[i];
    ASSERT_FLOAT_EQ(sqrtf(mag_sq), 1.0f, 0.01f);
    /* Component 0 should be pulled toward neighbor's strong dim-0 */
    ASSERT_TRUE(v.v[0] > 0.0f);
    PASS();
}

/* ── Corpus lifecycle ────────────────────────────────────────────── */

TEST(sem_corpus_new_free) {
    cbm_sem_corpus_t *c = cbm_sem_corpus_new();
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(cbm_sem_corpus_doc_count(c), 0);
    ASSERT_EQ(cbm_sem_corpus_token_count(c), 0);
    cbm_sem_corpus_free(c);
    PASS();
}

TEST(sem_corpus_add_one_doc) {
    cbm_sem_corpus_t *c = cbm_sem_corpus_new();
    ASSERT_NOT_NULL(c);
    const char *tokens[] = {"parse", "user", "input"};
    cbm_sem_corpus_add_doc(c, tokens, 3);
    ASSERT_EQ(cbm_sem_corpus_doc_count(c), 1);
    ASSERT_TRUE(cbm_sem_corpus_token_count(c) > 0);
    cbm_sem_corpus_free(c);
    PASS();
}

TEST(sem_corpus_idf) {
    cbm_sem_corpus_t *c = cbm_sem_corpus_new();
    ASSERT_NOT_NULL(c);
    const char *doc1[] = {"a", "b", "c"};
    const char *doc2[] = {"a", "d", "e"};
    cbm_sem_corpus_add_doc(c, doc1, 3);
    cbm_sem_corpus_add_doc(c, doc2, 3);
    /* IDF for "a" (appears in 2 docs): log(2/2) = log(1) = 0 */
    float idf_a = cbm_sem_corpus_idf(c, "a");
    ASSERT_TRUE(idf_a < 0.01f);
    /* IDF for "b" (appears in 1 doc): log(2/1) > 0 */
    float idf_b = cbm_sem_corpus_idf(c, "b");
    ASSERT_TRUE(idf_b > 0.0f);
    cbm_sem_corpus_free(c);
    PASS();
}

TEST(sem_corpus_add_null_doc) {
    cbm_sem_corpus_t *c = cbm_sem_corpus_new();
    ASSERT_NOT_NULL(c);
    cbm_sem_corpus_add_doc(c, NULL, 0);
    cbm_sem_corpus_add_doc(c, NULL, -1);
    ASSERT_EQ(cbm_sem_corpus_doc_count(c), 0);
    cbm_sem_corpus_free(c);
    PASS();
}

TEST(sem_corpus_free_null) {
    cbm_sem_corpus_free(NULL); /* should not crash */
    PASS();
}

/* ── Config ──────────────────────────────────────────────────────── */

TEST(sem_get_config_defaults) {
    cbm_sem_config_t cfg = cbm_sem_get_config();
    ASSERT_TRUE(cfg.w_tfidf > 0.0f);
    ASSERT_TRUE(cfg.w_ri > 0.0f);
    ASSERT_TRUE(cfg.threshold > 0.0f);
    ASSERT_TRUE(cfg.max_edges > 0);
    PASS();
}

/* ── RaBitQ estimator quality (from-paper 4-bit quantization) ────── */

typedef struct {
    atomic_int *ready;
    atomic_int *start;
    float value;
    cbm_rsq_code_t code;
} rotsq_thread_ctx_t;

static void *rotsq_concurrent_first_encode(void *opaque) {
    rotsq_thread_ctx_t *ctx = opaque;
    float vec[CBM_RSQ_IN_DIM];
    for (int i = 0; i < CBM_RSQ_IN_DIM; i++) {
        vec[i] = ctx->value + (float)i / (float)CBM_RSQ_IN_DIM;
    }
    atomic_fetch_add_explicit(ctx->ready, 1, memory_order_release);
    while (atomic_load_explicit(ctx->start, memory_order_acquire) == 0) {
        cbm_usleep(1000);
    }
    cbm_rsq_encode(vec, &ctx->code);
    return NULL;
}

/* The daemon can initialize semantic encoders from multiple request threads.
 * Run this first so ThreadSanitizer observes the one-time initialization. */
TEST(sem_rotsq_concurrent_first_encode) {
    atomic_int ready;
    atomic_int start;
    atomic_init(&ready, 0);
    atomic_init(&start, 0);
    rotsq_thread_ctx_t ctx[2] = {
        {.ready = &ready, .start = &start, .value = 0.25F},
        {.ready = &ready, .start = &start, .value = -0.5F},
    };
    cbm_thread_t threads[2];
    bool started0 = cbm_thread_create(&threads[0], 0, rotsq_concurrent_first_encode, &ctx[0]) == 0;
    bool started1 = cbm_thread_create(&threads[1], 0, rotsq_concurrent_first_encode, &ctx[1]) == 0;
    for (int spins = 0; started0 && started1 && spins < 5000 &&
                        atomic_load_explicit(&ready, memory_order_acquire) < 2;
         spins++) {
        cbm_usleep(1000);
    }
    bool both_ready = atomic_load_explicit(&ready, memory_order_acquire) == 2;
    atomic_store_explicit(&start, 1, memory_order_release);
    if (started0) {
        (void)cbm_thread_join(&threads[0]);
    }
    if (started1) {
        (void)cbm_thread_join(&threads[1]);
    }

    ASSERT_TRUE(started0);
    ASSERT_TRUE(started1);
    ASSERT_TRUE(both_ready);
    ASSERT_TRUE(ctx[0].code.scale > 0.0F);
    ASSERT_TRUE(ctx[1].code.scale > 0.0F);
    PASS();
}

/* Deterministic pseudo-random unit vectors; validates that the quantized
 * inner-product estimator tracks the exact float IP within tight bounds.
 * These bounds gate the semantic pass's use of the codes: cosine scores are
 * thresholded at ~0.75, so the estimator error must be well under the
 * decision margin for typical pairs. */
TEST(sem_rotsq_ip_error_bounds) {
    enum { N = 64 };
    static float vecs[N][CBM_RSQ_IN_DIM];
    static cbm_rsq_code_t codes[N];
    uint32_t state = 0xC0FFEEu;
    for (int i = 0; i < N; i++) {
        double norm = 0.0;
        for (int d = 0; d < CBM_RSQ_IN_DIM; d++) {
            /* xorshift32 → roughly uniform in [-1, 1] */
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            vecs[i][d] = ((float)(state & 0xFFFFFF) / (float)0x7FFFFF) - 1.0F;
            norm += (double)vecs[i][d] * (double)vecs[i][d];
        }
        float inv = norm > 0.0 ? (float)(1.0 / sqrt(norm)) : 0.0F;
        for (int d = 0; d < CBM_RSQ_IN_DIM; d++) {
            vecs[i][d] *= inv;
        }
        cbm_rsq_encode(vecs[i], &codes[i]);
    }
    double max_err = 0.0;
    double sum_err = 0.0;
    int pairs = 0;
    for (int i = 0; i < N; i++) {
        for (int j = i; j < N; j++) {
            double exact = 0.0;
            for (int d = 0; d < CBM_RSQ_IN_DIM; d++) {
                exact += (double)vecs[i][d] * (double)vecs[j][d];
            }
            double est = (double)cbm_rsq_ip(&codes[i], &codes[j]);
            double err = fabs(est - exact);
            sum_err += err;
            if (err > max_err) {
                max_err = err;
            }
            pairs++;
        }
    }
    double mean_err = sum_err / pairs;
    /* Self-IP of a unit vector must estimate ~1. */
    double self_est = (double)cbm_rsq_ip(&codes[0], &codes[0]);
    ASSERT_TRUE(fabs(self_est - 1.0) < 0.05);
    /* 4-bit RaBitQ-style SQ after rotation: expect mean error well under 1%
     * of the unit scale and max under ~4% — comfortably inside the semantic
     * threshold's decision margin. */
    ASSERT_TRUE(mean_err < 0.01);
    ASSERT_TRUE(max_err < 0.04);
    PASS();
}

/* ── Suite ───────────────────────────────────────────────────────── */

SUITE(semantic) {
    RUN_TEST(sem_rotsq_concurrent_first_encode);
    RUN_TEST(sem_rotsq_ip_error_bounds);
    RUN_TEST(sem_tokenize_camel);
    RUN_TEST(sem_tokenize_snake);
    RUN_TEST(sem_tokenize_dot);
    RUN_TEST(sem_tokenize_null);
    RUN_TEST(sem_tokenize_max_out);
    RUN_TEST(sem_tokenize_abbrev_expansion);
    RUN_TEST(sem_project_identity_is_not_semantic_input);
    RUN_TEST(sem_definition_properties_preserve_cargo_boundary);
    RUN_TEST(sem_body_tokens_consume_complete_bounded_carrier);
    RUN_TEST(sem_definition_properties_fail_closed);
    RUN_TEST(sem_cosine_identical);
    RUN_TEST(sem_cosine_orthogonal);
    RUN_TEST(sem_cosine_zero_vector);
    RUN_TEST(sem_cosine_negative);
    RUN_TEST(sem_cosine_null);
    RUN_TEST(sem_normalize_unit);
    RUN_TEST(sem_normalize_scales);
    RUN_TEST(sem_normalize_zero);
    RUN_TEST(sem_normalize_null);
    RUN_TEST(sem_vec_add_scaled_basic);
    RUN_TEST(sem_vec_add_scaled_null);
    RUN_TEST(sem_random_index_deterministic);
    RUN_TEST(sem_random_index_different_tokens);
    RUN_TEST(sem_random_index_null);
    RUN_TEST(sem_proximity_same_file);
    RUN_TEST(sem_proximity_same_dir);
    RUN_TEST(sem_proximity_different_paths);
    RUN_TEST(sem_proximity_null);
    RUN_TEST(sem_diffuse_zero_neighbors);
    RUN_TEST(sem_diffuse_single_neighbor);
    RUN_TEST(sem_corpus_new_free);
    RUN_TEST(sem_corpus_add_one_doc);
    RUN_TEST(sem_corpus_idf);
    RUN_TEST(sem_corpus_add_null_doc);
    RUN_TEST(sem_corpus_free_null);
    RUN_TEST(sem_get_config_defaults);
}
