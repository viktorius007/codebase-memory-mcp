/*
 * test_mcp.c — Tests for the MCP server module.
 *
 * Covers: JSON-RPC parsing, MCP protocol, tool dispatch, tool handlers.
 */
#include "../src/foundation/compat.h"
#include <sqlite3.h>
#include "../src/foundation/compat_fs.h" /* cbm_unlink / cbm_rmdir */
#include "../src/foundation/constants.h"
#include "../src/foundation/log.h"
#include "../src/foundation/platform.h" /* cbm_file_size */
#include "../src/foundation/subprocess.h"
#include "../src/mcp/compact_out.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <cli/cli.h>
#include <mcp/index_supervisor.h> /* spawn-count hook — #845 in-process guard */
#include <mcp/mcp.h>
#include <mcp/mcp_internal.h>
#include <pipeline/pipeline.h>
#include <store/store.h>
#include <watcher/watcher.h>
#include <yyjson/yyjson.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* chmod / stat for read-only query reproductions */
#ifdef _WIN32
#include <direct.h>
#define cbm_chdir _chdir
#define cbm_getcwd _getcwd
#else
#ifdef __APPLE__
#include <libproc.h>
#endif

#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#define cbm_chdir chdir
#define cbm_getcwd getcwd
extern char **environ;
#endif

extern void cbm_mcp_server_test_use_borrowed_store(cbm_mcp_server_t *srv, cbm_store_t *store,
                                                   const char *project);

static bool mcp_response_has_exact_tool(const char *response, const char *expected_name) {
    yyjson_doc *doc = response ? yyjson_read(response, strlen(response), 0) : NULL;
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *result = root ? yyjson_obj_get(root, "result") : NULL;
    yyjson_val *tools = result ? yyjson_obj_get(result, "tools") : NULL;
    bool found = false;
    if (tools && yyjson_is_arr(tools)) {
        size_t index, max;
        yyjson_val *tool;
        yyjson_arr_foreach(tools, index, max, tool) {
            yyjson_val *name = yyjson_obj_get(tool, "name");
            if (name && yyjson_is_str(name) && strcmp(yyjson_get_str(name), expected_name) == 0) {
                found = true;
                break;
            }
        }
    }
    yyjson_doc_free(doc);
    return found;
}

static size_t mcp_response_tool_count(const char *response) {
    yyjson_doc *doc = response ? yyjson_read(response, strlen(response), 0) : NULL;
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *result = root ? yyjson_obj_get(root, "result") : NULL;
    yyjson_val *tools = result ? yyjson_obj_get(result, "tools") : NULL;
    size_t count = tools && yyjson_is_arr(tools) ? yyjson_arr_size(tools) : 0U;
    yyjson_doc_free(doc);
    return count;
}

static char mcp_log_buf[4096];
static bool mcp_saw_autoindex_log;

static void mcp_capture_log(const char *line) {
    snprintf(mcp_log_buf, sizeof(mcp_log_buf), "%s", line ? line : "");
    if (line && strstr(line, "msg=autoindex.")) {
        mcp_saw_autoindex_log = true;
    }
}

static bool response_contains_json_fragment(const char *response, const char *fragment) {
    if (!response || !fragment) {
        return false;
    }
    if (strstr(response, fragment)) {
        return true;
    }

    char escaped[512];
    size_t out = 0;
    for (size_t i = 0; fragment[i] && out + 2 < sizeof(escaped); i++) {
        if (fragment[i] == '"') {
            escaped[out++] = '\\';
        }
        escaped[out++] = fragment[i];
    }
    escaped[out] = '\0';
    return strstr(response, escaped) != NULL;
}

static void restore_cache_dir(const char *saved_copy) {
    if (saved_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_copy, 1);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
}

static void cleanup_project_db(const char *cache, const char *project) {
    if (!cache || !project) {
        return;
    }

    char path[CBM_SZ_4K];
    snprintf(path, sizeof(path), "%s/%s.db", cache, project);
    cbm_unlink(path);
    snprintf(path, sizeof(path), "%s/%s.db-wal", cache, project);
    cbm_unlink(path);
    snprintf(path, sizeof(path), "%s/%s.db-shm", cache, project);
    cbm_unlink(path);
}

#define MCP_MUTATION_GUARD_MAX_EVENTS 16

typedef struct {
    int deny_begin_call;      /* one-based; zero allows every acquisition */
    int deny_try_begin_call;  /* one-based; zero allows every try acquisition */
    int cancel_on_begin_call; /* one-based; zero never requests cancellation */
    int begin_count;
    int try_begin_count;
    int end_count;
    cbm_mcp_server_t *cancel_server;
    bool cancel_attempted;
    bool cancel_accepted;
    const char *observed_db_path;
    const char *observed_backup_path;
    bool db_exists_at_begin;
    bool backup_exists_at_begin;
    bool db_exists_at_end;
    bool backup_exists_at_end;
    char begin_projects[MCP_MUTATION_GUARD_MAX_EVENTS][CBM_SZ_256];
    char try_begin_projects[MCP_MUTATION_GUARD_MAX_EVENTS][CBM_SZ_256];
    char end_projects[MCP_MUTATION_GUARD_MAX_EVENTS][CBM_SZ_256];
} mcp_mutation_guard_probe_t;

typedef struct {
    const char *deny_step;
    int call_count;
    char steps[4][64];
} mcp_quarantine_hook_probe_t;

typedef struct {
    bool reject_merge_base;
    int diff_calls;
    int merge_base_calls;
} mcp_command_hook_probe_t;

static bool mcp_quarantine_hook_probe(void *context, const char *step) {
    mcp_quarantine_hook_probe_t *probe = context;
    if (!probe || !step) {
        return false;
    }
    int event = probe->call_count++;
    if (event >= 0 && event < 4) {
        snprintf(probe->steps[event], sizeof(probe->steps[event]), "%s", step);
    }
    return !probe->deny_step || strcmp(probe->deny_step, step) != 0;
}

static bool mcp_command_hook_probe(void *context, const char *command) {
    mcp_command_hook_probe_t *probe = context;
    if (!probe || !command) {
        return false;
    }
    if (strstr(command, "merge-base")) {
        probe->merge_base_calls++;
        return !probe->reject_merge_base;
    }
    probe->diff_calls++;
    return true;
}

typedef struct {
    const char *name;
    char *value;
    bool present;
} mcp_test_env_backup_t;

static void mcp_test_restore_env(mcp_test_env_backup_t *backups, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (backups[index].present) {
            (void)cbm_setenv(backups[index].name, backups[index].value, 1);
        } else {
            (void)cbm_unsetenv(backups[index].name);
        }
        free(backups[index].value);
    }
}

static int mcp_test_git(const char *root, const char *const *arguments) {
    char empty_config[CBM_SZ_4K];
    int config_length =
        snprintf(empty_config, sizeof(empty_config), "%s/.cbm-empty-gitconfig", root);
    if (config_length <= 0 || (size_t)config_length >= sizeof(empty_config)) {
        return -1;
    }
    FILE *config = cbm_fopen(empty_config, "wb");
    if (!config) {
        return -1;
    }
    if (fclose(config) != 0) {
        return -1;
    }

    mcp_test_env_backup_t backups[] = {
        {.name = "GIT_CONFIG_GLOBAL"},     {.name = "GIT_CONFIG_SYSTEM"},
        {.name = "GIT_CONFIG_NOSYSTEM"},   {.name = "GIT_CONFIG_COUNT"},
        {.name = "GIT_CONFIG_PARAMETERS"},
    };
    bool snapshot_ok = true;
    for (size_t index = 0; index < sizeof(backups) / sizeof(backups[0]); index++) {
        const char *value = getenv(backups[index].name);
        backups[index].present = value != NULL;
        backups[index].value = value ? strdup(value) : NULL;
        snapshot_ok = snapshot_ok && (!value || backups[index].value);
    }
    if (!snapshot_ok) {
        for (size_t index = 0; index < sizeof(backups) / sizeof(backups[0]); index++) {
            free(backups[index].value);
        }
        return -1;
    }
    bool environment_ok = cbm_setenv("GIT_CONFIG_GLOBAL", empty_config, 1) == 0 &&
                          cbm_setenv("GIT_CONFIG_SYSTEM", empty_config, 1) == 0 &&
                          cbm_setenv("GIT_CONFIG_NOSYSTEM", "1", 1) == 0 &&
                          cbm_setenv("GIT_CONFIG_COUNT", "0", 1) == 0 &&
                          cbm_unsetenv("GIT_CONFIG_PARAMETERS") == 0;
    if (!environment_ok) {
        mcp_test_restore_env(backups, sizeof(backups) / sizeof(backups[0]));
        return -1;
    }

    const char *git = "git";
#ifdef _WIN32
    char git_executable[CBM_SZ_4K];
    const char *resolved = cbm_find_cli("git", cbm_get_home_dir());
    int resolved_length =
        resolved ? snprintf(git_executable, sizeof(git_executable), "%s", resolved) : -1;
    if (resolved_length <= 0 || (size_t)resolved_length >= sizeof(git_executable)) {
        mcp_test_restore_env(backups, sizeof(backups) / sizeof(backups[0]));
        return -1;
    }
    git = git_executable;
#endif
    const char *argv[24] = {git, "-C", root};
    size_t index = 3;
    while (arguments && *arguments && index + 1 < sizeof(argv) / sizeof(argv[0])) {
        argv[index++] = *arguments++;
    }
    if ((arguments && *arguments) || index >= sizeof(argv) / sizeof(argv[0])) {
        mcp_test_restore_env(backups, sizeof(backups) / sizeof(backups[0]));
        return -1;
    }
    argv[index] = NULL;

    cbm_proc_opts_t options = {
        .bin = git,
        .argv = argv,
        .quiet_timeout_ms = 10000,
    };
    cbm_proc_result_t result = {0};
    int run_result =
        cbm_subprocess_run(&options, &result) == 0 && result.outcome == CBM_PROC_CLEAN ? 0 : -1;
    mcp_test_restore_env(backups, sizeof(backups) / sizeof(backups[0]));
    return run_result;
}

static bool mcp_mutation_guard_probe_begin(void *context, const char *project) {
    mcp_mutation_guard_probe_t *probe = context;
    if (!probe) {
        return false;
    }

    int event = probe->begin_count++;
    if (event < MCP_MUTATION_GUARD_MAX_EVENTS) {
        snprintf(probe->begin_projects[event], sizeof(probe->begin_projects[event]), "%s",
                 project ? project : "");
    }
    if (probe->cancel_on_begin_call > 0 && probe->begin_count == probe->cancel_on_begin_call) {
        probe->cancel_attempted = true;
        probe->cancel_accepted = cbm_mcp_server_cancel_active(probe->cancel_server);
    }
    if (probe->observed_db_path) {
        probe->db_exists_at_begin = cbm_file_exists(probe->observed_db_path);
    }
    if (probe->observed_backup_path) {
        probe->backup_exists_at_begin = cbm_file_exists(probe->observed_backup_path);
    }
    return probe->deny_begin_call == 0 || probe->begin_count != probe->deny_begin_call;
}

static bool mcp_mutation_guard_probe_try_begin(void *context, const char *project) {
    mcp_mutation_guard_probe_t *probe = context;
    if (!probe) {
        return false;
    }

    int event = probe->try_begin_count++;
    if (event < MCP_MUTATION_GUARD_MAX_EVENTS) {
        snprintf(probe->try_begin_projects[event], sizeof(probe->try_begin_projects[event]), "%s",
                 project ? project : "");
    }
    if (probe->observed_db_path) {
        probe->db_exists_at_begin = cbm_file_exists(probe->observed_db_path);
    }
    if (probe->observed_backup_path) {
        probe->backup_exists_at_begin = cbm_file_exists(probe->observed_backup_path);
    }
    return probe->deny_try_begin_call == 0 || probe->try_begin_count != probe->deny_try_begin_call;
}

static void mcp_mutation_guard_probe_end(void *context, const char *project) {
    mcp_mutation_guard_probe_t *probe = context;
    if (!probe) {
        return;
    }

    int event = probe->end_count++;
    if (event < MCP_MUTATION_GUARD_MAX_EVENTS) {
        snprintf(probe->end_projects[event], sizeof(probe->end_projects[event]), "%s",
                 project ? project : "");
    }
    if (probe->observed_db_path) {
        probe->db_exists_at_end = cbm_file_exists(probe->observed_db_path);
    }
    if (probe->observed_backup_path) {
        probe->backup_exists_at_end = cbm_file_exists(probe->observed_backup_path);
    }
}

static bool mcp_make_corrupt_project_store(const char *cache, const char *project) {
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    cbm_store_t *store = cbm_store_open_path(db_path);
    if (!store) {
        return false;
    }

    /* Numeric root paths are the deterministic corruption trigger used by
     * cbm_store_check_integrity() and the issue #557 reproduction. */
    bool created = cbm_store_upsert_project(store, project, "826") == CBM_STORE_OK;
    cbm_store_close(store);
    return created;
}

/* Keep a writer open so the fixture has a real, committed WAL generation.
 * Query-only opens must not alter either file when quarantine is denied or
 * cannot be published safely. The caller owns the returned store. */
static cbm_store_t *mcp_open_corrupt_project_store_with_wal(const char *cache,
                                                            const char *project) {
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    cbm_store_t *store = cbm_store_open_path(db_path);
    if (!store) {
        return NULL;
    }

    bool ready =
        cbm_store_exec(store, "PRAGMA wal_autocheckpoint=0;") == CBM_STORE_OK &&
        cbm_store_upsert_project(store, project, "826") == CBM_STORE_OK &&
        cbm_store_exec(store, "CREATE TABLE IF NOT EXISTS guard_wal_sentinel(value TEXT);"
                              "INSERT INTO guard_wal_sentinel(value) VALUES('committed');") ==
            CBM_STORE_OK;
    if (!ready) {
        cbm_store_close(store);
        return NULL;
    }
    return store;
}

static bool mcp_make_valid_project_store_at(const char *path, const char *project,
                                            const char *root_path) {
    cbm_store_t *store = cbm_store_open_path(path);
    if (!store) {
        return false;
    }
    bool ready = cbm_store_upsert_project(store, project, root_path) == CBM_STORE_OK &&
                 cbm_store_prepare_for_publish(store) == CBM_STORE_OK;
    cbm_store_close(store);
    return ready;
}

static unsigned char *mcp_read_file_bytes(const char *path, long *out_len) {
    if (!out_len) {
        return NULL;
    }
    *out_len = 0;
    FILE *fp = cbm_fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    unsigned char *bytes = malloc((size_t)size + 1U);
    if (!bytes) {
        fclose(fp);
        return NULL;
    }
    size_t read_count = fread(bytes, 1, (size_t)size, fp);
    fclose(fp);
    if (read_count != (size_t)size) {
        free(bytes);
        return NULL;
    }
    bytes[size] = '\0';
    *out_len = size;
    return bytes;
}

static bool mcp_file_matches_snapshot(const char *path, const unsigned char *expected,
                                      long expected_len) {
    long actual_len = 0;
    unsigned char *actual = mcp_read_file_bytes(path, &actual_len);
    bool matches = actual && expected && actual_len == expected_len &&
                   memcmp(actual, expected, (size_t)actual_len) == 0;
    free(actual);
    return matches;
}

/* Return the number of quarantine files for a project and, when present, the
 * first path whose name is distinct from the legacy fixed `.corrupt` name. */
static bool mcp_is_corrupt_backup_main_name(const char *name, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (strcmp(name, prefix) == 0) {
        return true;
    }
    const char *suffix = name + prefix_len;
    if (strncmp(name, prefix, prefix_len) != 0 || suffix[0] != '.' || strlen(suffix + 1) != 16) {
        return false;
    }
    for (const char *cursor = suffix + 1; *cursor; cursor++) {
        if (!isxdigit((unsigned char)*cursor)) {
            return false;
        }
    }
    return true;
}

static int mcp_find_corrupt_backups(const char *cache, const char *project, char *unique_path,
                                    size_t unique_path_size) {
    if (unique_path && unique_path_size > 0) {
        unique_path[0] = '\0';
    }
    char prefix[CBM_DIRENT_NAME_MAX];
    snprintf(prefix, sizeof(prefix), "%s.db.corrupt", project);
    int count = 0;
    cbm_dir_t *dir = cbm_opendir(cache);
    if (!dir) {
        return 0;
    }
    cbm_dirent_t *entry;
    while ((entry = cbm_readdir(dir)) != NULL) {
        if (!mcp_is_corrupt_backup_main_name(entry->name, prefix)) {
            continue;
        }
        char path[CBM_SZ_1K];
        snprintf(path, sizeof(path), "%s/%s", cache, entry->name);
        if (!cbm_file_exists(path)) {
            continue;
        }
        count++;
        if (unique_path && unique_path_size > 0 && unique_path[0] == '\0' &&
            strcmp(entry->name, prefix) != 0) {
            snprintf(unique_path, unique_path_size, "%s", path);
        }
    }
    cbm_closedir(dir);
    return count;
}

static int mcp_count_corrupt_artifacts(const char *cache, const char *project) {
    char prefix[CBM_DIRENT_NAME_MAX];
    snprintf(prefix, sizeof(prefix), "%s.db.corrupt", project);
    size_t prefix_len = strlen(prefix);
    int count = 0;
    cbm_dir_t *dir = cbm_opendir(cache);
    if (!dir) {
        return 0;
    }
    cbm_dirent_t *entry;
    while ((entry = cbm_readdir(dir)) != NULL) {
        if (strncmp(entry->name, prefix, prefix_len) == 0) {
            count++;
        }
    }
    cbm_closedir(dir);
    return count;
}

static int mcp_count_directory_entries_with_prefix(const char *directory, const char *prefix) {
    cbm_dir_t *dir = cbm_opendir(directory);
    if (!dir) {
        return -1;
    }
    size_t prefix_length = strlen(prefix);
    int count = 0;
    cbm_dirent_t *entry;
    while ((entry = cbm_readdir(dir)) != NULL) {
        if (strncmp(entry->name, prefix, prefix_length) == 0) {
            count++;
        }
    }
    cbm_closedir(dir);
    return count;
}

static void mcp_cleanup_corrupt_backups(const char *cache, const char *project) {
    char prefix[CBM_DIRENT_NAME_MAX];
    snprintf(prefix, sizeof(prefix), "%s.db.corrupt", project);
    size_t prefix_len = strlen(prefix);
    cbm_dir_t *dir = cbm_opendir(cache);
    if (!dir) {
        return;
    }
    cbm_dirent_t *entry;
    while ((entry = cbm_readdir(dir)) != NULL) {
        if (strncmp(entry->name, prefix, prefix_len) == 0) {
            char path[CBM_SZ_1K];
            snprintf(path, sizeof(path), "%s/%s", cache, entry->name);
            cbm_unlink(path);
        }
    }
    cbm_closedir(dir);
}

typedef struct {
    mcp_mutation_guard_probe_t guard;
    const char *replacement_path;
    const char *live_path;
    bool replacement_attempted;
    bool replacement_succeeded;
} mcp_replacing_mutation_guard_t;

static bool mcp_replacing_mutation_guard_begin(void *context, const char *project) {
    mcp_replacing_mutation_guard_t *replacement = context;
    if (!replacement || !mcp_mutation_guard_probe_begin(&replacement->guard, project)) {
        return false;
    }
    replacement->replacement_attempted = true;
    bool sidecars_removed = cbm_remove_db_sidecars(replacement->live_path) == 0;
    replacement->replacement_succeeded =
        sidecars_removed &&
        cbm_rename_replace(replacement->replacement_path, replacement->live_path) == 0;
    return true;
}

static void mcp_replacing_mutation_guard_end(void *context, const char *project) {
    mcp_replacing_mutation_guard_t *replacement = context;
    if (replacement) {
        mcp_mutation_guard_probe_end(&replacement->guard, project);
    }
}

TEST(tree_cell_sanitizes_control_and_invalid_utf8) {
    /* One raw control or invalid-UTF8 byte in a cell poisons LINE-ORIENTED
     * consumers of the ENTIRE output (BSD grep treats all of it as
     * unmatchable binary — the macos-15-intel release-smoke B3 class), so
     * cell emission guarantees valid UTF-8: control bytes escape as \u00XX,
     * invalid sequences become U+FFFD, and both force the quoted form. */
    cbm_sb_t sb;
    cbm_sb_init(&sb);
    cbm_tree_cell_str(&sb,
                      "evil\x01name\xff"
                      "end",
                      true);
    char *out = cbm_sb_finish(&sb);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "\"evil\\u0001name\xEF\xBF\xBD"
                       "end\"");
    free(out);

    cbm_sb_init(&sb);
    cbm_tree_cell_str(&sb, "b\xC3\xA4r_ok", true);
    out = cbm_sb_finish(&sb);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "b\xC3\xA4r_ok"); /* valid UTF-8 stays raw + unquoted */
    free(out);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  JSON-RPC PARSING
 * ══════════════════════════════════════════════════════════════════ */

TEST(jsonrpc_parse_request) {
    const char *line = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                       "\"params\":{\"capabilities\":{}}}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(req.jsonrpc, "2.0");
    ASSERT_STR_EQ(req.method, "initialize");
    ASSERT_EQ(req.id, 1);
    ASSERT_TRUE(req.has_id);
    ASSERT_NOT_NULL(req.params_raw);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_notification) {
    const char *line = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(req.method, "notifications/initialized");
    ASSERT_FALSE(req.has_id);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_invalid) {
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse("not json", &req);
    ASSERT_EQ(rc, -1);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_tools_call) {
    const char *line = "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\","
                       "\"params\":{\"name\":\"search_graph\","
                       "\"arguments\":{\"label\":\"Function\",\"limit\":5}}}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(req.method, "tools/call");
    ASSERT_EQ(req.id, 42);
    ASSERT_NOT_NULL(req.params_raw);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

/* issue #253: JSON-RPC 2.0 §4 permits string ids (Claude Desktop sends them
 * for "initialize"). Previously strtol-coerced to 0; must be preserved. */
TEST(jsonrpc_parse_string_id_issue253) {
    const char *line = "{\"jsonrpc\":\"2.0\",\"id\":\"init-abc\",\"method\":\"initialize\"}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(req.has_id);
    ASSERT_NOT_NULL(req.id_str);
    ASSERT_STR_EQ(req.id_str, "init-abc");
    cbm_jsonrpc_request_free(&req);

    /* A purely non-numeric string would have become 0 under strtol. */
    const char *line2 = "{\"jsonrpc\":\"2.0\",\"id\":\"xyz\",\"method\":\"ping\"}";
    cbm_jsonrpc_request_t req2 = {0};
    ASSERT_EQ(cbm_jsonrpc_parse(line2, &req2), 0);
    ASSERT_NOT_NULL(req2.id_str);
    ASSERT_STR_EQ(req2.id_str, "xyz");
    cbm_jsonrpc_request_free(&req2);
    PASS();
}

/* issue #253: the response must echo the string id verbatim, not as a number. */
TEST(jsonrpc_format_response_string_id_issue253) {
    cbm_jsonrpc_response_t resp = {
        .id_str = "init-abc",
        .result_json = "{\"ok\":true}",
    };
    char *json = cbm_jsonrpc_format_response(&resp);
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"id\":\"init-abc\""));
    /* Must NOT have coerced to a numeric id. */
    ASSERT_NULL(strstr(json, "\"id\":0"));
    free(json);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  JSON-RPC FORMATTING
 * ══════════════════════════════════════════════════════════════════ */

TEST(jsonrpc_format_response) {
    cbm_jsonrpc_response_t resp = {
        .id = 1,
        .result_json = "{\"name\":\"codebase-memory-mcp\"}",
    };
    char *json = cbm_jsonrpc_format_response(&resp);
    ASSERT_NOT_NULL(json);
    /* Should contain jsonrpc, id, and result */
    ASSERT_NOT_NULL(strstr(json, "\"jsonrpc\":\"2.0\""));
    ASSERT_NOT_NULL(strstr(json, "\"id\":1"));
    ASSERT_NOT_NULL(strstr(json, "\"result\""));
    free(json);
    PASS();
}

TEST(jsonrpc_format_error) {
    char *json = cbm_jsonrpc_format_error(5, -32600, "Invalid Request");
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"id\":5"));
    ASSERT_NOT_NULL(strstr(json, "\"error\""));
    ASSERT_NOT_NULL(strstr(json, "-32600"));
    ASSERT_NOT_NULL(strstr(json, "Invalid Request"));
    free(json);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  MCP PROTOCOL HELPERS
 * ══════════════════════════════════════════════════════════════════ */

TEST(mcp_initialize_response) {
    cbm_cli_set_version("9.8.7-test");

    /* Default (no params): returns latest supported version */
    char *json = cbm_mcp_initialize_response(NULL);
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "codebase-memory-mcp"));
    ASSERT_NOT_NULL(strstr(json, "\"version\":\"9.8.7-test\""));
    ASSERT_NOT_NULL(strstr(json, "capabilities"));
    ASSERT_NOT_NULL(strstr(json, "tools"));
    ASSERT_NOT_NULL(strstr(json, "\"listChanged\":false"));
    ASSERT_NOT_NULL(strstr(json, "\"prompts\":{\"listChanged\":false}"));
    ASSERT_NOT_NULL(strstr(json, "\"instructions\":"));
    ASSERT_NOT_NULL(strstr(json, "search_graph"));
    ASSERT_NOT_NULL(strstr(json, "auto-refresh"));
    ASSERT_NOT_NULL(strstr(json, "2025-11-25"));
    free(json);

    /* Client requests a supported version: server echoes it */
    json = cbm_mcp_initialize_response("{\"protocolVersion\":\"2024-11-05\"}");
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "2024-11-05"));
    free(json);

    json = cbm_mcp_initialize_response("{\"protocolVersion\":\"2025-06-18\"}");
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "2025-06-18"));
    free(json);

    /* Client requests unknown version: server returns its latest */
    json = cbm_mcp_initialize_response("{\"protocolVersion\":\"9999-01-01\"}");
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "2025-11-25"));
    free(json);
    cbm_cli_set_version("dev");
    PASS();
}

TEST(mcp_tools_list) {
    char *json = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(json);
    /* Should contain all tools, including the targeted coverage gate. */
    ASSERT_NOT_NULL(strstr(json, "index_repository"));
    ASSERT_NOT_NULL(strstr(json, "search_graph"));
    ASSERT_NOT_NULL(strstr(json, "query_graph"));
    ASSERT_NOT_NULL(strstr(json, "trace_path"));
    ASSERT_NOT_NULL(strstr(json, "get_code_snippet"));
    ASSERT_NOT_NULL(strstr(json, "get_graph_schema"));
    ASSERT_NOT_NULL(strstr(json, "get_architecture"));
    ASSERT_NOT_NULL(strstr(json, "search_code"));
    ASSERT_NOT_NULL(strstr(json, "list_projects"));
    ASSERT_NOT_NULL(strstr(json, "delete_project"));
    ASSERT_NOT_NULL(strstr(json, "index_status"));
    ASSERT_NOT_NULL(strstr(json, "check_index_coverage"));
    ASSERT_NOT_NULL(strstr(json, "detect_changes"));
    ASSERT_NOT_NULL(strstr(json, "manage_adr"));
    ASSERT_NOT_NULL(strstr(json, "ingest_traces"));
    free(json);
    PASS();
}

/* #1361: --help omitted check_index_coverage because its tool list was a
 * hand-maintained copy. The list is now rendered from the registry; this pins
 * the render so a formatter bug cannot reintroduce a silent omission. */
TEST(mcp_tools_help_list_matches_registry) {
    char *help = cbm_mcp_tools_help_list();
    ASSERT_NOT_NULL(help);
    int count = cbm_mcp_tool_count();
    ASSERT_GT(count, 0);
    for (int i = 0; i < count; i++) {
        const char *name = cbm_mcp_tool_name(i);
        ASSERT_NOT_NULL(name);
        ASSERT_NOT_NULL(strstr(help, name));
    }
    /* Exactly one comma between consecutive tools: the rendered cardinality
     * equals the registry's, so truncation or duplication fails here. */
    int commas = 0;
    for (const char *p = help; *p; p++) {
        if (*p == ',') {
            commas++;
        }
    }
    ASSERT_EQ(commas, count - 1);
    /* Wrapped for an 80-column terminal. */
    const char *line = help;
    while (line && *line) {
        const char *nl = strchr(line, '\n');
        size_t line_len = nl ? (size_t)(nl - line) : strlen(line);
        ASSERT_LT((int)line_len, 80);
        line = nl ? nl + 1 : NULL;
    }
    free(help);
    PASS();
}

TEST(mcp_tools_list_latest_metadata) {
    char *json = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"title\":\"Search graph\""));
    ASSERT_NOT_NULL(strstr(json, "\"title\":\"Index repository\""));
    ASSERT_NOT_NULL(strstr(json, "\"title\":\"Check index coverage\""));
    /* No tool may declare an outputSchema. The blanket permissive schema
     * ({"type":"object","additionalProperties":true}) carried zero information
     * for clients, but its presence made spec-compliant clients read
     * structuredContent as the authoritative result — which turned every
     * text-shaped (tree/TOON) reply into a rendered "{}" (#1522). Tool output
     * here is format-parameter-polymorphic, so no static schema is truthful. */
    ASSERT_NULL(strstr(json, "\"outputSchema\""));
    /* search_graph's compact degree columns intentionally count the graph
     * relationships used for call/reference/type centrality, not every edge
     * family (for example DEFINES or CONTAINS_FILE). Keep the public contract
     * aligned with the store query. */
    ASSERT_NOT_NULL(strstr(json, "in/out = selected degree across CALLS, USAGE, CALL_REFERENCE, "
                                 "INHERITS, IMPLEMENTS, and OVERRIDE"));
    ASSERT_NULL(strstr(json, "TOTAL degree across ALL edge types"));
    free(json);
    PASS();
}

TEST(mcp_tools_have_behavior_annotations) {
    struct {
        const char *name;
        bool read_only;
        bool destructive;
        bool idempotent;
        bool open_world;
    } expected[] = {
        {"index_repository", false, false, true, false},
        /* These query tools can reach resolve_store(), whose corrupt-store
         * recovery quarantines/removes database files. Keep the annotations
         * conservative until query resolution is strictly non-mutating. */
        {"search_graph", false, true, true, false},
        {"query_graph", false, true, true, false},
        {"trace_path", false, true, true, false},
        {"get_code_snippet", false, true, true, false},
        {"get_graph_schema", false, true, true, false},
        {"get_architecture", false, true, true, false},
        {"search_code", false, true, true, false},
        {"list_projects", true, false, true, false},
        {"delete_project", false, true, true, false},
        {"index_status", false, true, true, false},
        {"check_index_coverage", false, true, true, false},
        {"detect_changes", false, true, true, false},
        {"manage_adr", false, true, false, false},
        {"ingest_traces", false, false, false, false},
    };

    char *json = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(json);
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *tools = yyjson_obj_get(yyjson_doc_get_root(doc), "tools");
    ASSERT_NOT_NULL(tools);
    ASSERT_EQ(yyjson_arr_size(tools), sizeof(expected) / sizeof(expected[0]));

    size_t matched = 0;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(tools, &iter);
    yyjson_val *tool;
    while ((tool = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val *name_val = yyjson_obj_get(tool, "name");
        yyjson_val *annotations = yyjson_obj_get(tool, "annotations");
        ASSERT_NOT_NULL(name_val);
        ASSERT_NOT_NULL(annotations);
        ASSERT_TRUE(yyjson_is_obj(annotations));

        const char *name = yyjson_get_str(name_val);
        bool found = false;
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            if (strcmp(name, expected[i].name) != 0) {
                continue;
            }
            yyjson_val *read_only = yyjson_obj_get(annotations, "readOnlyHint");
            yyjson_val *destructive = yyjson_obj_get(annotations, "destructiveHint");
            yyjson_val *idempotent = yyjson_obj_get(annotations, "idempotentHint");
            yyjson_val *open_world = yyjson_obj_get(annotations, "openWorldHint");
            ASSERT_TRUE(yyjson_is_bool(read_only));
            ASSERT_TRUE(yyjson_is_bool(destructive));
            ASSERT_TRUE(yyjson_is_bool(idempotent));
            ASSERT_TRUE(yyjson_is_bool(open_world));
            ASSERT_EQ(yyjson_get_bool(read_only), expected[i].read_only);
            ASSERT_EQ(yyjson_get_bool(destructive), expected[i].destructive);
            ASSERT_EQ(yyjson_get_bool(idempotent), expected[i].idempotent);
            ASSERT_EQ(yyjson_get_bool(open_world), expected[i].open_world);
            found = true;
            matched++;
            break;
        }
        ASSERT_TRUE(found);
    }

    ASSERT_EQ(matched, sizeof(expected) / sizeof(expected[0]));
    yyjson_doc_free(doc);
    free(json);
    PASS();
}

TEST(mcp_index_repository_declares_name_override_issue571) {
    char *json = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"index_repository\""));
    ASSERT_NOT_NULL(strstr(json, "\"name\":{\"type\":\"string\""));
    ASSERT_NOT_NULL(strstr(json, "Non-ASCII bytes are encoded"));
    free(json);
    PASS();
}

TEST(mcp_tools_array_schemas_have_items) {
    /* VS Code 1.112+ rejects array schemas without "items" (see
     * https://github.com/microsoft/vscode/issues/248810).
     * Walk every tool's inputSchema and verify that every "type":"array"
     * property also contains "items". */
    char *json = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(json);

    /* Scan for all occurrences of "type":"array" — each must be followed
     * by "items" before the next closing brace of that property. */
    const char *p = json;
    while ((p = strstr(p, "\"type\":\"array\"")) != NULL) {
        /* Find the enclosing '}' for this property object */
        const char *end = strchr(p, '}');
        ASSERT_NOT_NULL(end);
        /* "items" must appear between p and end */
        size_t span = (size_t)(end - p);
        char *segment = malloc(span + 1);
        memcpy(segment, p, span);
        segment[span] = '\0';
        ASSERT_NOT_NULL(strstr(segment, "\"items\"")); /* array missing items */
        free(segment);
        p = end;
    }

    free(json);
    PASS();
}

TEST(mcp_ingest_traces_items_disallow_additional_properties_issue731) {
    char *json = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(json);

    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *tools = yyjson_obj_get(root, "tools");
    ASSERT_NOT_NULL(tools);
    ASSERT_TRUE(yyjson_is_arr(tools));

    yyjson_val *tool;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(tools, &iter);
    yyjson_val *ingest_traces = NULL;
    while ((tool = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val *name = yyjson_obj_get(tool, "name");
        if (name && yyjson_is_str(name) && strcmp(yyjson_get_str(name), "ingest_traces") == 0) {
            ingest_traces = tool;
            break;
        }
    }
    ASSERT_NOT_NULL(ingest_traces);

    yyjson_val *input_schema = yyjson_obj_get(ingest_traces, "inputSchema");
    ASSERT_NOT_NULL(input_schema);
    yyjson_val *properties = yyjson_obj_get(input_schema, "properties");
    ASSERT_NOT_NULL(properties);
    yyjson_val *traces = yyjson_obj_get(properties, "traces");
    ASSERT_NOT_NULL(traces);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(traces, "type")), "array");
    yyjson_val *items = yyjson_obj_get(traces, "items");
    ASSERT_NOT_NULL(items);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(items, "type")), "object");
    yyjson_val *item_properties = yyjson_obj_get(items, "properties");
    ASSERT_NOT_NULL(item_properties);
    yyjson_val *caller = yyjson_obj_get(item_properties, "caller");
    ASSERT_NOT_NULL(caller);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(caller, "type")), "string");
    yyjson_val *callee = yyjson_obj_get(item_properties, "callee");
    ASSERT_NOT_NULL(callee);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(callee, "type")), "string");
    yyjson_val *count = yyjson_obj_get(item_properties, "count");
    ASSERT_NOT_NULL(count);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(count, "type")), "integer");
    yyjson_val *additional_properties = yyjson_obj_get(items, "additionalProperties");
    ASSERT_NOT_NULL(additional_properties);
    ASSERT_TRUE(yyjson_is_bool(additional_properties));
    ASSERT_FALSE(yyjson_get_bool(additional_properties));

    yyjson_doc_free(doc);
    free(json);
    PASS();
}

/* Guard for PR #560 (schema enum): the get_architecture aspects items schema
 * must carry an enum of the valid tokens — including the new "overview" —
 * mirroring VALID_ASPECTS in mcp.c. Parsed structurally like
 * mcp_ingest_traces_items_disallow_additional_properties_issue731. */
TEST(mcp_get_architecture_aspects_schema_enum_pr560) {
    char *json = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(json);

    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *tools = yyjson_obj_get(root, "tools");
    ASSERT_NOT_NULL(tools);
    ASSERT_TRUE(yyjson_is_arr(tools));

    yyjson_val *tool;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(tools, &iter);
    yyjson_val *get_arch = NULL;
    while ((tool = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val *name = yyjson_obj_get(tool, "name");
        if (name && yyjson_is_str(name) && strcmp(yyjson_get_str(name), "get_architecture") == 0) {
            get_arch = tool;
            break;
        }
    }
    ASSERT_NOT_NULL(get_arch);

    yyjson_val *input_schema = yyjson_obj_get(get_arch, "inputSchema");
    ASSERT_NOT_NULL(input_schema);
    yyjson_val *properties = yyjson_obj_get(input_schema, "properties");
    ASSERT_NOT_NULL(properties);
    yyjson_val *aspects = yyjson_obj_get(properties, "aspects");
    ASSERT_NOT_NULL(aspects);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(aspects, "type")), "array");
    yyjson_val *items = yyjson_obj_get(aspects, "items");
    ASSERT_NOT_NULL(items);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(items, "type")), "string");
    yyjson_val *enum_arr = yyjson_obj_get(items, "enum");
    ASSERT_NOT_NULL(enum_arr);
    ASSERT_TRUE(yyjson_is_arr(enum_arr));

    /* The enum must be exactly the valid-token set — no more, no less. */
    static const char *expected[] = {"all",      "overview",   "structure", "dependencies",
                                     "routes",   "languages",  "packages",  "entry_points",
                                     "hotspots", "boundaries", "layers",    "file_tree",
                                     "clusters", "cycles"};
    size_t expected_count = sizeof(expected) / sizeof(expected[0]);
    ASSERT_EQ(yyjson_arr_size(enum_arr), expected_count);
    for (size_t i = 0; i < expected_count; i++) {
        bool found = false;
        yyjson_val *ev;
        yyjson_arr_iter eiter;
        yyjson_arr_iter_init(enum_arr, &eiter);
        while ((ev = yyjson_arr_iter_next(&eiter)) != NULL) {
            if (yyjson_is_str(ev) && strcmp(yyjson_get_str(ev), expected[i]) == 0) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found);
    }

    yyjson_doc_free(doc);
    free(json);
    PASS();
}

TEST(mcp_text_result) {
    char *json = cbm_mcp_text_result("{\"total\":5}", false);
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"type\":\"text\""));
    /* The text value is JSON-escaped inside the "text" field */
    ASSERT_NOT_NULL(strstr(json, "total"));
    ASSERT_NOT_NULL(strstr(json, "\"structuredContent\":{\"total\":5}"));
    ASSERT_NOT_NULL(strstr(json, "\"isError\":false"));
    ASSERT_NULL(strstr(json, "\"isError\":true"));
    free(json);
    PASS();
}

TEST(mcp_text_result_omits_structured_content_for_plain_text) {
    /* A non-JSON payload must not produce a structuredContent key AT ALL.
     *
     * History, because this field has now been wrong in both directions:
     * pre-#1488 it duplicated the whole payload ({"text": <payload>} beside an
     * identical content[0].text — 2.05x the bytes). #1488 replaced that with an
     * EMPTY object — and spec-compliant clients (Claude Code among them) treat
     * structuredContent as THE result whenever the tool declares an
     * outputSchema, so every default-format search_graph/trace_path rendered as
     * literally "{}" (#1522). Empty is not honest; it is a second lie.
     *
     * The corrected contract: no duplication AND no empty-object placeholder.
     * A text payload travels once, in content[0].text, and the envelope simply
     * has no structuredContent. (Real JSON objects and error envelopes keep
     * theirs — that is structure, not padding.) */
    char *json = cbm_mcp_text_result("plain text", false);
    ASSERT_NOT_NULL(json);
    ASSERT_NULL(strstr(json, "\"structuredContent\""));
    /* The payload is still delivered — exactly once. */
    ASSERT_NOT_NULL(strstr(json, "\"text\":\"plain text\""));
    ASSERT_NOT_NULL(strstr(json, "\"isError\":false"));
    free(json);
    PASS();
}

TEST(mcp_cancel_matches_request_id) {
    ASSERT_TRUE(cbm_mcp_cancel_request_matches("{\"requestId\":7}", 7, NULL));
    ASSERT_FALSE(cbm_mcp_cancel_request_matches("{\"requestId\":8}", 7, NULL));
    ASSERT_TRUE(cbm_mcp_cancel_request_matches("{\"requestId\":\"call-1\"}", -1, "call-1"));
    ASSERT_FALSE(cbm_mcp_cancel_request_matches("{\"requestId\":\"call-2\"}", -1, "call-1"));
    ASSERT_FALSE(cbm_mcp_cancel_request_matches("{\"requestId\":7}", -1, "7"));
    ASSERT_FALSE(cbm_mcp_cancel_request_matches("{}", 7, NULL));
    PASS();
}

TEST(mcp_text_result_error) {
    char *json = cbm_mcp_text_result("something failed", true);
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"structuredContent\":{\"error\":\"something failed\"}"));
    ASSERT_NOT_NULL(strstr(json, "\"isError\":true"));
    ASSERT_NOT_NULL(strstr(json, "something failed"));
    free(json);
    PASS();
}

static int assert_oversized_result_fails_closed(const char *payload, bool input_is_error,
                                                size_t uncapped_bytes,
                                                const char *discarded_payload_marker) {
    ASSERT_TRUE(uncapped_bytes > CBM_MCP_RESULT_MAX_BYTES);

    char *json = cbm_mcp_text_result(payload, input_is_error);
    ASSERT_NOT_NULL(json);
    ASSERT_TRUE(strlen(json) <= CBM_MCP_RESULT_MAX_BYTES);

    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_TRUE(yyjson_is_obj(root));
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(root, "isError")));

    yyjson_val *content = yyjson_obj_get(root, "content");
    ASSERT_TRUE(yyjson_is_arr(content));
    ASSERT_EQ(yyjson_arr_size(content), 1);
    yyjson_val *item = yyjson_arr_get_first(content);
    const char *message = yyjson_get_str(yyjson_obj_get(item, "text"));
    ASSERT_NOT_NULL(message);
    ASSERT_TRUE(strncmp(message, "ERROR: result exceeds safe response envelope", 44) == 0);
    ASSERT_NOT_NULL(strstr(message, "no partial result returned"));
    const char *measurements = strstr(message, "complete_response_bytes=");
    ASSERT_NOT_NULL(measurements);
    size_t measured_complete_bytes = 0;
    unsigned measured_limit_bytes = 0;
    ASSERT_EQ(sscanf(measurements, "complete_response_bytes=%zu limit_bytes=%u",
                     &measured_complete_bytes, &measured_limit_bytes),
              2);
    ASSERT_EQ(measured_complete_bytes, uncapped_bytes);
    ASSERT_EQ(measured_limit_bytes, CBM_MCP_RESULT_MAX_BYTES);
    ASSERT_NOT_NULL(strstr(message, "Remedy: manage_adr use mode=sections"));
    ASSERT_NOT_NULL(
        strstr(message, "choose bounded output, a narrower tool, or inspect source data"));
    ASSERT_NULL(strstr(json, discarded_payload_marker));

    yyjson_val *structured = yyjson_obj_get(root, "structuredContent");
    ASSERT_TRUE(yyjson_is_obj(structured));
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(structured, "error")), message);
    yyjson_doc_free(doc);
    free(json);
    PASS();
}

static char *build_unbounded_structured_result_for_test(const char *payload) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *content = yyjson_mut_arr(doc);
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, item, "type", "text");
    yyjson_mut_obj_add_str(doc, item, "text", payload);
    yyjson_mut_arr_add_val(content, item);
    yyjson_mut_obj_add_val(doc, root, "content", content);
    yyjson_doc *payload_doc = yyjson_read(payload, strlen(payload), 0);
    if (!payload_doc) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    yyjson_mut_obj_add_val(doc, root, "structuredContent",
                           yyjson_val_mut_copy(doc, yyjson_doc_get_root(payload_doc)));
    yyjson_mut_obj_add_bool(doc, root, "isError", false);
    char *uncapped = yyjson_mut_write(doc, 0, NULL);
    yyjson_doc_free(payload_doc);
    yyjson_mut_doc_free(doc);
    return uncapped;
}

TEST(mcp_text_result_complete_envelope_is_hard_bounded) {
    const size_t repeated = 40000;
    const size_t capacity = repeated + 64;
    char *structured = malloc(capacity);
    ASSERT_NOT_NULL(structured);
    int prefix = snprintf(structured, capacity, "{\"data\":\"");
    ASSERT_TRUE(prefix > 0);
    memset(structured + prefix, 's', repeated);
    snprintf(structured + prefix + repeated, capacity - (size_t)prefix - repeated, "\"}");

    /* The former envelope carried the JSON object twice: escaped in text and
     * parsed in structuredContent. Construct that old protocol shape
     * independently, proving the complete response (not merely its input)
     * exceeded the ceiling. */
    char *uncapped = build_unbounded_structured_result_for_test(structured);
    ASSERT_NOT_NULL(uncapped);
    ASSERT_EQ(assert_oversized_result_fails_closed(structured, false, strlen(uncapped),
                                                   "ssssssssssssssss"),
              0);
    free(uncapped);
    free(structured);

    /* Escaping can make a raw payload below the limit exceed the complete
     * envelope once serialized. */
    char *escape_heavy = malloc(repeated + 1);
    ASSERT_NOT_NULL(escape_heavy);
    memset(escape_heavy, '\\', repeated);
    escape_heavy[repeated] = '\0';
    ASSERT_TRUE(strlen(escape_heavy) < CBM_MCP_RESULT_MAX_BYTES);
    size_t escaped_uncapped_bytes =
        strlen("{\"content\":[{\"type\":\"text\",\"text\":\"\"}],\"isError\":false}") +
        repeated * 2;
    ASSERT_EQ(assert_oversized_result_fails_closed(escape_heavy, false, escaped_uncapped_bytes,
                                                   "\\\\\\\\"),
              0);
    free(escape_heavy);
    PASS();
}

TEST(mcp_text_result_oversized_dynamic_error_is_hard_bounded) {
    const size_t repeated = 40000;
    char *error = malloc(repeated + 1);
    ASSERT_NOT_NULL(error);
    memset(error, 'e', repeated);
    error[repeated] = '\0';

    /* Error text is present in content and structuredContent. This payload
     * needs no JSON escaping, so the fixed envelope plus both copies gives an
     * independent exact byte count for the pre-substitution response. */
    ASSERT_TRUE(strlen(error) < CBM_MCP_RESULT_MAX_BYTES);
    size_t uncapped_bytes = strlen("{\"content\":[{\"type\":\"text\",\"text\":\"\"}],"
                                   "\"structuredContent\":{\"error\":\"\"},\"isError\":true}") +
                            repeated * 2;
    ASSERT_EQ(assert_oversized_result_fails_closed(error, true, uncapped_bytes, "eeeeeeeeeeeeeeee"),
              0);
    free(error);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  ARGUMENT EXTRACTION
 * ══════════════════════════════════════════════════════════════════ */

TEST(mcp_get_tool_name) {
    const char *params = "{\"name\":\"search_graph\",\"arguments\":{\"label\":\"Function\"}}";
    char *name = cbm_mcp_get_tool_name(params);
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ(name, "search_graph");
    free(name);
    PASS();
}

TEST(mcp_get_arguments) {
    const char *params =
        "{\"name\":\"search_graph\",\"arguments\":{\"label\":\"Function\",\"limit\":5}}";
    char *args = cbm_mcp_get_arguments(params);
    ASSERT_NOT_NULL(args);
    ASSERT_NOT_NULL(strstr(args, "\"label\":\"Function\""));
    ASSERT_NOT_NULL(strstr(args, "\"limit\":5"));
    free(args);
    PASS();
}

TEST(mcp_get_string_arg) {
    const char *args = "{\"label\":\"Function\",\"name_pattern\":\".*Order.*\"}";
    char *val = cbm_mcp_get_string_arg(args, "label");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val, "Function");
    free(val);

    val = cbm_mcp_get_string_arg(args, "name_pattern");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val, ".*Order.*");
    free(val);

    val = cbm_mcp_get_string_arg(args, "nonexistent");
    ASSERT_NULL(val);
    PASS();
}

TEST(mcp_get_int_arg) {
    const char *args = "{\"limit\":10,\"offset\":5}";
    int val = cbm_mcp_get_int_arg(args, "limit", 0);
    ASSERT_EQ(val, 10);
    val = cbm_mcp_get_int_arg(args, "offset", 0);
    ASSERT_EQ(val, 5);
    val = cbm_mcp_get_int_arg(args, "missing", 42);
    ASSERT_EQ(val, 42);
    PASS();
}

TEST(mcp_get_bool_arg) {
    const char *args = "{\"include_connected\":true,\"regex\":false}";
    bool val = cbm_mcp_get_bool_arg(args, "include_connected");
    ASSERT_TRUE(val);
    val = cbm_mcp_get_bool_arg(args, "regex");
    ASSERT_FALSE(val);
    val = cbm_mcp_get_bool_arg(args, "missing");
    ASSERT_FALSE(val);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  SERVER HANDLE — PROTOCOL FLOW
 * ══════════════════════════════════════════════════════════════════ */

TEST(server_handle_initialize) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                                   "\"params\":{\"capabilities\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"id\":1"));
    ASSERT_NOT_NULL(strstr(resp, "codebase-memory-mcp"));
    ASSERT_NOT_NULL(strstr(resp, "capabilities"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_initialized_notification) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    /* Notification has no id → no response */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    ASSERT_NULL(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_tools_list) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"id\":2"));
    ASSERT_NOT_NULL(strstr(resp, "search_graph"));
    ASSERT_NOT_NULL(strstr(resp, "query_graph"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_tools_list_defaults_to_all_tools_and_accepts_cursor) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":200,\"method\":\"tools/list\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"id\":200"));
    ASSERT_NULL(strstr(resp, "\"nextCursor\""));
    ASSERT_NOT_NULL(strstr(resp, "index_repository"));
    ASSERT_NOT_NULL(strstr(resp, "manage_adr"));
    ASSERT_NOT_NULL(strstr(resp, "ingest_traces"));
    free(resp);

    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":202,\"method\":\"tools/list\",\"params\":{}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"id\":202"));
    ASSERT_NULL(strstr(resp, "\"nextCursor\""));
    ASSERT_NOT_NULL(strstr(resp, "manage_adr"));
    ASSERT_NOT_NULL(strstr(resp, "ingest_traces"));
    free(resp);

    resp = cbm_mcp_server_handle(
        srv,
        "{\"jsonrpc\":\"2.0\",\"id\":201,\"method\":\"tools/list\",\"params\":{\"cursor\":\"8\"}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"id\":201"));
    ASSERT_NULL(strstr(resp, "\"nextCursor\""));
    ASSERT_NOT_NULL(strstr(resp, "manage_adr"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_analysis_profile_filters_and_rejects_mutators) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_mcp_server_set_tool_profile(srv, CBM_MCP_TOOL_PROFILE_ANALYSIS);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":219,\"method\":\"initialize\",\"params\":{}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "analysis tool profile"));
    ASSERT_NOT_NULL(strstr(resp, "check_index_coverage"));
    ASSERT_NULL(strstr(resp, "index_repository"));
    free(resp);

    resp = cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":220,\"method\":\"tools/list\"}");
    ASSERT_NOT_NULL(resp);
    static const char *const analysis_tools[] = {
        "search_graph",     "query_graph",          "trace_path",     "get_code_snippet",
        "get_graph_schema", "get_architecture",     "search_code",    "list_projects",
        "index_status",     "check_index_coverage", "detect_changes",
    };
    ASSERT_EQ(mcp_response_tool_count(resp), sizeof(analysis_tools) / sizeof(analysis_tools[0]));
    for (size_t i = 0U; i < sizeof(analysis_tools) / sizeof(analysis_tools[0]); i++) {
        ASSERT_TRUE(mcp_response_has_exact_tool(resp, analysis_tools[i]));
    }
    free(resp);

    resp = cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":221,\"method\":\"tools/call\","
                                      "\"params\":{\"name\":\"delete_project\","
                                      "\"arguments\":{\"project\":\"anything\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "not available in the analysis tool profile"));
    ASSERT_NOT_NULL(strstr(resp, "isError"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_scout_profile_exposes_only_the_fast_tier) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_mcp_server_set_tool_profile(srv, CBM_MCP_TOOL_PROFILE_SCOUT);
    mcp_saw_autoindex_log = false;
    cbm_log_set_sink_ex(mcp_capture_log, CBM_LOG_SINK_REPLACE);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":222,\"method\":\"initialize\",\"params\":{}}");
    cbm_log_set_sink(NULL);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "scout tool profile"));
    ASSERT_NOT_NULL(strstr(resp, "check_index_coverage"));
    ASSERT_NULL(strstr(resp, "index_repository"));
    ASSERT_FALSE(mcp_saw_autoindex_log);
    free(resp);

    resp = cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":223,\"method\":\"tools/list\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_EQ(mcp_response_tool_count(resp), 7U);
    ASSERT_TRUE(mcp_response_has_exact_tool(resp, "search_graph"));
    ASSERT_TRUE(mcp_response_has_exact_tool(resp, "trace_path"));
    ASSERT_TRUE(mcp_response_has_exact_tool(resp, "get_code_snippet"));
    ASSERT_TRUE(mcp_response_has_exact_tool(resp, "get_architecture"));
    ASSERT_TRUE(mcp_response_has_exact_tool(resp, "list_projects"));
    ASSERT_TRUE(mcp_response_has_exact_tool(resp, "index_status"));
    ASSERT_TRUE(mcp_response_has_exact_tool(resp, "check_index_coverage"));
    ASSERT_FALSE(mcp_response_has_exact_tool(resp, "query_graph"));
    ASSERT_FALSE(mcp_response_has_exact_tool(resp, "search_code"));
    ASSERT_FALSE(mcp_response_has_exact_tool(resp, "get_graph_schema"));
    ASSERT_FALSE(mcp_response_has_exact_tool(resp, "detect_changes"));
    ASSERT_FALSE(mcp_response_has_exact_tool(resp, "index_repository"));
    free(resp);

    resp = cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":224,\"method\":\"tools/call\","
                                      "\"params\":{\"name\":\"query_graph\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "not available in the scout tool profile"));
    ASSERT_NOT_NULL(strstr(resp, "isError"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(analysis_profile_arguments_fail_closed_and_disable_http) {
    cbm_mcp_tool_profile_t profile = CBM_MCP_TOOL_PROFILE_ALL;
    const char *no_profile[] = {"codebase-memory-mcp"};
    const char *analysis_equals[] = {"codebase-memory-mcp", "--tool-profile=analysis"};
    const char *analysis_pair[] = {"codebase-memory-mcp", "--tool-profile", "analysis"};
    const char *scout_equals[] = {"codebase-memory-mcp", "--tool-profile=scout"};
    const char *unknown_equals[] = {"codebase-memory-mcp", "--tool-profile=analaysis"};
    const char *unknown_pair[] = {"codebase-memory-mcp", "--tool-profile", "all"};
    const char *missing_value[] = {"codebase-memory-mcp", "--tool-profile"};

    ASSERT_EQ(cbm_mcp_parse_tool_profile_args(1, no_profile, &profile), 0);
    ASSERT_EQ(profile, CBM_MCP_TOOL_PROFILE_ALL);
    ASSERT_TRUE(cbm_mcp_tool_profile_allows_http(profile));

    ASSERT_EQ(cbm_mcp_parse_tool_profile_args(2, analysis_equals, &profile), 0);
    ASSERT_EQ(profile, CBM_MCP_TOOL_PROFILE_ANALYSIS);
    ASSERT_FALSE(cbm_mcp_tool_profile_allows_http(profile));

    ASSERT_EQ(cbm_mcp_parse_tool_profile_args(3, analysis_pair, &profile), 0);
    ASSERT_EQ(profile, CBM_MCP_TOOL_PROFILE_ANALYSIS);
    ASSERT_EQ(cbm_mcp_parse_tool_profile_args(2, scout_equals, &profile), 0);
    ASSERT_EQ(profile, CBM_MCP_TOOL_PROFILE_SCOUT);
    ASSERT_FALSE(cbm_mcp_tool_profile_allows_http(profile));
    ASSERT_EQ(cbm_mcp_parse_tool_profile_args(2, unknown_equals, &profile), -1);
    ASSERT_EQ(cbm_mcp_parse_tool_profile_args(3, unknown_pair, &profile), -1);
    ASSERT_EQ(cbm_mcp_parse_tool_profile_args(2, missing_value, &profile), -1);
    PASS();
}

TEST(hook_windows_path_containment_is_case_insensitive_and_segment_safe) {
    ASSERT_TRUE(cbm_hook_path_contains_for_testing("C:/Repo", "c:/repo/src/main.c", true));
    ASSERT_FALSE(cbm_hook_path_contains_for_testing("C:/Repo", "c:/repository/src/main.c", true));
    ASSERT_FALSE(cbm_hook_path_contains_for_testing("C:/Repo", "c:/repo/src/main.c", false));
    PASS();
}

TEST(server_handle_prompts_list_workflows) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":203,\"method\":\"prompts/list\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"id\":203"));
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"explore_codebase\""));
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"review_change_impact\""));
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"project\""));
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"question\""));
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"change\""));
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"base_branch\""));
    ASSERT_NOT_NULL(strstr(resp, "\"required\":true"));
    ASSERT_NULL(strstr(resp, "\"nextCursor\""));

    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_prompts_get_workflows) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":204,\"method\":\"prompts/get\","
             "\"params\":{\"name\":\"explore_codebase\",\"arguments\":{"
             "\"project\":\"payments\",\"question\":\"How are refunds routed?\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"role\":\"user\""));
    ASSERT_NOT_NULL(strstr(resp, "\"type\":\"text\""));
    ASSERT_NOT_NULL(strstr(resp, "payments"));
    ASSERT_NOT_NULL(strstr(resp, "How are refunds routed?"));
    ASSERT_NOT_NULL(strstr(resp, "search_graph"));
    ASSERT_NOT_NULL(strstr(resp, "trace_path"));
    ASSERT_NOT_NULL(strstr(resp, "get_code_snippet"));
    free(resp);

    resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":205,\"method\":\"prompts/get\","
                                   "\"params\":{\"name\":\"review_change_impact\",\"arguments\":{"
                                   "\"project\":\"payments\",\"change\":\"refund retry policy\","
                                   "\"base_branch\":\"develop\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "refund retry policy"));
    ASSERT_NOT_NULL(strstr(resp, "develop"));
    ASSERT_NOT_NULL(strstr(resp, "detect_changes"));
    ASSERT_NOT_NULL(strstr(resp, "trace_path"));
    ASSERT_NOT_NULL(strstr(resp, "include_tests"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_prompts_get_validates_arguments) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":206,\"method\":\"prompts/get\","
                                   "\"params\":{\"name\":\"unknown\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"code\":-32602"));
    ASSERT_NOT_NULL(strstr(resp, "Invalid prompt name"));
    free(resp);

    resp = cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":207,\"method\":\"prompts/get\","
                                      "\"params\":{\"name\":\"explore_codebase\",\"arguments\":{"
                                      "\"project\":\"payments\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"code\":-32602"));
    ASSERT_NOT_NULL(strstr(resp, "Missing required prompt arguments"));
    free(resp);

    /* Optional means it may be omitted, not that an explicitly invalid value
     * may be silently substituted. */
    resp = cbm_mcp_server_handle(srv,
                                 "{\"jsonrpc\":\"2.0\",\"id\":208,\"method\":\"prompts/get\","
                                 "\"params\":{\"name\":\"review_change_impact\",\"arguments\":{"
                                 "\"project\":\"payments\",\"change\":\"refund retry policy\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NULL(strstr(resp, "\"error\""));
    ASSERT_NOT_NULL(strstr(resp, "base_branch \\\"main\\\""));
    free(resp);

    resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":209,\"method\":\"prompts/get\","
                                   "\"params\":{\"name\":\"review_change_impact\",\"arguments\":{"
                                   "\"project\":\"payments\",\"change\":\"refund retry policy\","
                                   "\"base_branch\":\"\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"code\":-32602"));
    ASSERT_NOT_NULL(strstr(resp, "Invalid prompt arguments"));
    free(resp);

    resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":210,\"method\":\"prompts/get\","
                                   "\"params\":{\"name\":\"review_change_impact\",\"arguments\":{"
                                   "\"project\":\"payments\",\"change\":\"refund retry policy\","
                                   "\"base_branch\":17}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"code\":-32602"));
    ASSERT_NOT_NULL(strstr(resp, "Invalid prompt arguments"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_logs_request_without_params) {
    mcp_log_buf[0] = '\0';
    CBMLogLevel prev_level = cbm_log_get_level();
    cbm_log_set_level(CBM_LOG_DEBUG);
    cbm_log_set_format(CBM_LOG_FORMAT_TEXT);
    cbm_log_set_sink_ex(mcp_capture_log, CBM_LOG_SINK_REPLACE);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":210,\"method\":\"tools/list\","
                                   "\"params\":{\"token\":\"secret\"}}");
    ASSERT_NOT_NULL(resp);
    free(resp);
    cbm_mcp_server_free(srv);

    cbm_log_set_sink(NULL);
    cbm_log_set_level(prev_level);

    ASSERT_NOT_NULL(strstr(mcp_log_buf, "msg=mcp.request"));
    ASSERT_NOT_NULL(strstr(mcp_log_buf, "protocol=jsonrpc"));
    ASSERT_NOT_NULL(strstr(mcp_log_buf, "method=tools/list"));
    ASSERT_NOT_NULL(strstr(mcp_log_buf, "status=ok"));
    ASSERT_NULL(strstr(mcp_log_buf, "token"));
    ASSERT_NULL(strstr(mcp_log_buf, "secret"));
    PASS();
}

TEST(server_handle_unknown_method) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"unknown/method\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"error\""));
    ASSERT_NOT_NULL(strstr(resp, "-32601")); /* Method not found */
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  TOOL HANDLERS (via server_handle)
 * ══════════════════════════════════════════════════════════════════ */

/* Helper: create a server with an in-memory store populated with test data */
static cbm_mcp_server_t *setup_mcp_with_data(void) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL); /* NULL = in-memory */
    return srv;
}

TEST(tool_list_projects_empty) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"list_projects\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"id\":10"));
    /* Should return a result (possibly empty list) */
    ASSERT_NOT_NULL(strstr(resp, "\"result\""));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_get_graph_schema_empty) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"get_graph_schema\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"result\""));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_unknown_tool) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"nonexistent_tool\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    /* Should return result with isError */
    ASSERT_NOT_NULL(strstr(resp, "isError"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_search_graph_basic) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();

    /* search_graph with no project → should work on empty store */
    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_graph\","
                                   "\"arguments\":{\"label\":\"Function\",\"limit\":10}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"result\""));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Forward declarations for helpers defined later in this file */
static cbm_mcp_server_t *setup_snippet_server(char *tmp_dir, size_t tmp_sz);
static void cleanup_snippet_dir(const char *tmp_dir);
static char *extract_text_content(const char *mcp_result);

/* callers_total/callees_total must count what the caller can enumerate: with
 * include_tests=false (default) test-file rows are hidden from the table, so
 * the totals must apply the same filter — a raw visited_count overstated the
 * set (field-eval agent read callers_total=175 against 2 visible rows and
 * distrusted the tool). */
TEST(tool_trace_totals_respect_test_filter) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "totproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/tot");

    cbm_node_t tgt = {.project = proj,
                      .label = "Function",
                      .name = "tgt",
                      .qualified_name = "totproj.a.tgt",
                      .file_path = "a.c",
                      .start_line = 1,
                      .end_line = 5};
    int64_t tid = cbm_store_upsert_node(st, &tgt);
    ASSERT_GT(tid, 0);
    cbm_node_t prod = {.project = proj,
                       .label = "Function",
                       .name = "prod_caller",
                       .qualified_name = "totproj.a.prod_caller",
                       .file_path = "src/testing/helpers.rs",
                       .start_line = 10,
                       .end_line = 15,
                       .properties_json = "{\"is_test\":false}"};
    cbm_node_t tst = {.project = proj,
                      .label = "Function",
                      .name = "test_caller",
                      .qualified_name = "totproj.t.test_caller",
                      .file_path = "src/lib.rs",
                      .start_line = 1,
                      .end_line = 5,
                      .properties_json = "{\"is_test\":true}"};
    int64_t xid = cbm_store_upsert_node(st, &tst);
    ASSERT_GT(xid, 0);
    /* Insert the hidden inline-test node first so a raw limit=1 page lands on
     * it. Pagination must filter before budgeting, then return prod_caller on
     * the first (and only) visible page rather than an empty truncated page. */
    int64_t pid = cbm_store_upsert_node(st, &prod);
    ASSERT_GT(pid, 0);
    cbm_edge_t e1 = {.project = proj, .source_id = pid, .target_id = tid, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &e1), 0);
    cbm_edge_t e2 = {.project = proj, .source_id = xid, .target_id = tid, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &e2), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":90,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"totproj\","
             "\"function_name\":\"tgt\",\"direction\":\"inbound\",\"limit\":1}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "callers_total: 1")); /* test row filtered */
    ASSERT_NOT_NULL(strstr(inner, "prod_caller"));
    ASSERT_NULL(strstr(inner, "test_caller"));
    ASSERT_NULL(strstr(inner, "truncated: true"));
    ASSERT_NULL(strstr(inner, "next: "));
    free(inner);

    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":91,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"totproj\","
             "\"function_name\":\"tgt\",\"direction\":\"inbound\",\"include_tests\":true}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "callers_total: 2")); /* both visible now */
    free(inner);
    cbm_mcp_server_free(srv);
    PASS();
}

/* SCC condensation (get_architecture aspect "cycles"): a 3-function CALLS
 * cycle A->B->C->A must be reported as one circular dependency of size 3 with
 * all three members; a separate acyclic chain (D->E) must NOT appear. The
 * aspect is opt-in — a default get_architecture call must NOT compute it. */
TEST(tool_get_architecture_cycles_detects_scc) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "cycproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/cyc");

    const char *names[5] = {"A", "B", "C", "D", "E"};
    int64_t id[5];
    for (int i = 0; i < 5; i++) {
        char qn[32];
        snprintf(qn, sizeof(qn), "cycproj.m.%s", names[i]);
        cbm_node_t n = {.project = proj,
                        .label = "Function",
                        .name = names[i],
                        .qualified_name = qn,
                        .file_path = "m.c",
                        .start_line = i + 1,
                        .end_line = i + 2};
        id[i] = cbm_store_upsert_node(st, &n);
        ASSERT_GT(id[i], 0);
    }
    /* cycle A->B->C->A, plus acyclic D->E */
    struct {
        int f;
        int t;
    } e[] = {{0, 1}, {1, 2}, {2, 0}, {3, 4}};
    for (size_t i = 0; i < sizeof(e) / sizeof(e[0]); i++) {
        cbm_edge_t ed = {
            .project = proj, .source_id = id[e[i].f], .target_id = id[e[i].t], .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(st, &ed), 0);
    }

    /* opt-in cycles aspect */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":71,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"get_architecture\",\"arguments\":{\"project\":\"cycproj\","
             "\"aspects\":[\"cycles\"]}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "cycles: 1")); /* exactly one SCC of size>1 */
    ASSERT_NOT_NULL(strstr(inner, "cycproj.m.A"));
    ASSERT_NOT_NULL(strstr(inner, "cycproj.m.B"));
    ASSERT_NOT_NULL(strstr(inner, "cycproj.m.C"));
    ASSERT_NULL(strstr(inner, "cycproj.m.D")); /* acyclic node not in any cycle */
    free(inner);
    free(resp);

    /* default call (no aspects) must NOT run the scan. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":72,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"get_architecture\",\"arguments\":{\"project\":\"cycproj\"}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NULL(strstr(inner, "cycles:"));
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Context-bomb guard: an oversized whole-file node returns an outline, not an
 * arbitrary source prefix. The immutable symbol range tells callers which raw
 * start/end range they may request explicitly. */
TEST(tool_get_code_snippet_outlines_whole_file_node) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_snipcap_XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(tmp));
    char proj_dir[512];
    snprintf(proj_dir, sizeof(proj_dir), "%s/project", tmp);
    cbm_mkdir(proj_dir);
    char src_path[600];
    snprintf(src_path, sizeof(src_path), "%s/big.py", proj_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    enum { BIG_LINES = 2000 };
    for (int i = 0; i < BIG_LINES; i++) {
        fprintf(fp, "line_%04d = %d  # padding to blow up an unclipped read\n", i, i);
    }
    fclose(fp);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "test-project";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, proj_dir);

    cbm_node_t mod = {0};
    mod.project = proj;
    mod.label = "Module";
    mod.name = "big";
    mod.qualified_name = "test-project.big";
    mod.file_path = "big.py";
    mod.start_line = 1;
    mod.end_line = BIG_LINES;
    ASSERT_GT(cbm_store_upsert_node(st, &mod), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":70,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"get_code_snippet\",\"arguments\":{\"project\":\"test-project\","
             "\"qualified_name\":\"test-project.big\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"mode\":\"outline\""));
    ASSERT_NOT_NULL(strstr(inner, "\"symbol_start_line\":1"));
    ASSERT_NOT_NULL(strstr(inner, "\"symbol_end_line\":2000"));
    ASSERT_NOT_NULL(strstr(inner, "\"outline_total\":0"));
    ASSERT_TRUE(strlen(inner) < 60000);
    ASSERT_NULL(strstr(inner, "\"source\""));
    ASSERT_NULL(strstr(inner, "line_0000"));
    ASSERT_NULL(strstr(inner, "line_1999"));
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    th_rmtree(tmp);
    PASS();
}

/* EVERY tool, not just the one that was reported.
 *
 * The duplication was invisible per-tool: each result looked reasonable on its
 * own, and only measuring the wire showed half of it was redundant. A guard
 * pinned to query_graph would not have caught it in search_graph, and would not
 * catch it in whatever tool is added next. So this enumerates the tool table
 * itself — a new tool is covered the moment it is registered, with no test edit.
 *
 * The invariant, tightened by #1522: for a NON-error result whose payload is
 * not a JSON object, the envelope must carry NO structuredContent key — not the
 * payload a second time (#1375's duplication), and not an empty object either
 * (#1488's replacement, which spec-compliant clients rendered as the entire
 * result: "{}"). Object payloads keep their parsed structuredContent; errors
 * keep structuredContent.error — bounded, small, and the only machine-readable
 * form of a failure a client gets. */
TEST(mcp_every_tool_result_is_duplication_free) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    int tools = cbm_mcp_tool_count();
    ASSERT_TRUE(tools > 0); /* an empty table would assert nothing at all */
    int checked = 0;

    for (int i = 0; i < tools; i++) {
        const char *name = cbm_mcp_tool_name(i);
        ASSERT_NOT_NULL(name);
        /* Minimal args: most tools error out, which is fine — an error envelope
         * is still an envelope, and the property must hold for it too. */
        char *envelope = cbm_mcp_handle_tool(srv, name, "{\"project\":\"test-project\"}");
        if (!envelope) {
            continue;
        }
        yyjson_doc *doc = yyjson_read(envelope, strlen(envelope), 0);
        ASSERT_NOT_NULL(doc);
        yyjson_val *root = yyjson_doc_get_root(doc);
        yyjson_val *content = yyjson_obj_get(root, "content");
        yyjson_val *first = content ? yyjson_arr_get(content, 0) : NULL;
        yyjson_val *text_val = first ? yyjson_obj_get(first, "text") : NULL;
        const char *text = text_val ? yyjson_get_str(text_val) : NULL;
        yyjson_val *structured = yyjson_obj_get(root, "structuredContent");

        yyjson_val *is_error = yyjson_obj_get(root, "isError");
        bool errored = is_error && yyjson_is_true(is_error);

        if (errored) {
            /* Errors keep machine-readable structure: either the wrapped
             * {"error": <text>} form, or — when the error payload is itself a
             * JSON object — that object parsed. Non-empty either way; an empty
             * object is the #1522 lie in error clothing. */
            ASSERT_NOT_NULL(structured);
            ASSERT_TRUE(yyjson_is_obj(structured));
            ASSERT_TRUE(yyjson_obj_size(structured) > 0);
        } else if (text && text[0]) {
            yyjson_doc *as_json = yyjson_read(text, strlen(text), 0);
            bool payload_is_object = as_json && yyjson_is_obj(yyjson_doc_get_root(as_json));
            if (as_json) {
                yyjson_doc_free(as_json);
            }
            if (payload_is_object) {
                /* JSON-object payloads: structuredContent is the PARSED form —
                 * the spec's structured+serialized pattern, not waste. It must
                 * be present and non-empty (an empty object beside a non-empty
                 * payload is exactly the #1522 lie). */
                ASSERT_NOT_NULL(structured);
                ASSERT_TRUE(yyjson_is_obj(structured));
                ASSERT_TRUE(yyjson_obj_size(structured) > 0);
            } else {
                /* Text-shaped payloads (tree/TOON): NO structuredContent key.
                 * {} rendered as the whole result in schema-honoring clients
                 * (#1522); {"text": payload} doubled the wire cost (#1375). */
                ASSERT_NULL(structured);
                checked++;
            }
        }
        yyjson_doc_free(doc);
        free(envelope);
    }

    /* If no tool produced a non-JSON payload, this test proved nothing — fail
     * rather than report a green that was never exercised. */
    ASSERT_TRUE(checked > 0);
    cbm_mcp_server_free(srv);
    th_rmtree(tmp);
    PASS();
}

TEST(tool_search_graph_includes_node_properties) {
    /* Node properties are OPT-IN columns in the default TOON output: the
     * default row is qn/label/file/lines/degrees only, `fields` adds the
     * requested property columns, and format:"json" restores the legacy
     * verbose objects with the full property blob. The setup_snippet_server
     * inserts HandleRequest with a signature/return_type/is_exported blob. */
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* Default TOON: compact table, no property spill. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\"HandleRequest\",\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    /* TOON is not a JSON object, so the envelope has no structuredContent at
     * all: {} was rendered as the entire result by schema-honoring clients
     * (#1522), and {"text": ...} doubled the wire cost (#1375). The payload
     * travels once, in content. */
    ASSERT_NULL(strstr(resp, "\"structuredContent\""));
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "results:")); /* TOON table header */
    ASSERT_NOT_NULL(strstr(inner, "(rows: name label lines in out;"));
    ASSERT_NOT_NULL(strstr(inner, "HandleRequest"));
    ASSERT_NULL(strstr(inner, "func HandleRequest")); /* signature not spilled */
    ASSERT_NULL(strstr(inner, "is_exported"));
    free(inner);
    free(resp);

    /* fields:["signature"] adds the column + values. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":43,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\"HandleRequest\",\"fields\":[\"signature\"],\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "(rows: name label lines in out signature;"));
    /* values with spaces are QUOTED so column positions survive */
    ASSERT_NOT_NULL(strstr(inner, "\"func HandleRequest() error\""));
    ASSERT_NOT_NULL(strstr(inner, "func HandleRequest"));
    free(inner);
    free(resp);

    /* format:"json" = json-stringified tree: same grouped model, column-
     * ordered row arrays — never per-row key envelopes or property blobs.
     * fields adds columns there too. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":44,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\"HandleRequest\",\"format\":\"json\","
             "\"fields\":[\"signature\"],\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"qn_prefix\"")); /* grouped tree model */
    ASSERT_NOT_NULL(strstr(inner, "\"cols\""));
    ASSERT_NOT_NULL(strstr(inner, "\"rows\""));
    ASSERT_NOT_NULL(strstr(inner, "\"signature\""));      /* requested column */
    ASSERT_NOT_NULL(strstr(inner, "func HandleRequest")); /* its value */
    ASSERT_NULL(strstr(inner, "is_exported"));            /* blob never spills */
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* detail:"ids" DROPPING the `fields` columns is by design — the schema calls
 * ids a "bare qualified-name enumeration (one column)", so a second column
 * would contradict the contract the agent read. What is NOT by design is doing
 * it in SILENCE: the caller asked for complexity, got a qn list, and has no
 * way to tell the column was refused from the column being empty. The repo
 * already ruled on exactly this shape one line away — requesting a CORE column
 * via `fields` drops it and emits a hint rather than empty cells (c5bffb7f:
 * "requesting core columns via fields hints instead of emitting empty cells").
 * Same silent-drop, same remedy. */
TEST(tool_search_graph_detail_ids_hints_dropped_fields) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* detail:"ids" + fields: one column stays (the contract), and the drop is
     * announced (the fix). */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":80,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\".*\",\"detail\":\"ids\","
             "\"fields\":[\"signature\"],\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "(cols: qn)"));     /* one column, per the schema */
    ASSERT_NULL(strstr(inner, "func HandleRequest")); /* no signature values */
    ASSERT_NOT_NULL(strstr(inner, "hint"));           /* the drop is not silent */
    ASSERT_NOT_NULL(strstr(inner, "signature"));      /* names what was dropped */
    ASSERT_NOT_NULL(strstr(inner, "detail"));         /* names the cause */
    free(inner);
    free(resp);

    /* Second config value: detail:"ids" with NO fields must stay hint-free, or
     * the hint is unconditional noise rather than a report of a real drop. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":81,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\".*\",\"detail\":\"ids\",\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "(cols: qn)"));
    ASSERT_NULL(strstr(inner, "hint"));
    free(inner);
    free(resp);

    /* Third config value: fields WITHOUT detail:"ids" still delivers the real
     * column and stays hint-free — the guard must not fire on the happy path. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":82,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\",\"arguments\":{"
             "\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\"HandleRequest\",\"fields\":[\"signature\"],\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "func HandleRequest"));
    ASSERT_NULL(strstr(inner, "hint"));
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_output_byte_budgets) {
    /* GUARD: absolute byte ceilings on default tool outputs. Re-bloat (e.g.
     * a property blob sneaking back into row emission — the fp field alone
     * is ~450B/hit) blows these ceilings immediately. The numbers are
     * generous vs the measured compact outputs (search hit rows ≈ 90B) but
     * far below the legacy verbose sizes (≈1.5KB/hit). */
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* search_graph: 1-hit search must stay under 600B. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":46,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\"HandleRequest\",\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "HandleRequest")); /* non-vacuous: hit present */
    ASSERT_LT((int)strlen(inner), 600);
    free(inner);
    free(resp);

    /* trace_path: single-hop trace on the fixture must stay under 800B. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":47,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"project\":\"test-project\",\"function_name\":\"HandleRequest\","
             "\"direction\":\"both\",\"depth\":2}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "callees:"));
    ASSERT_LT((int)strlen(inner), 800);
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_search_graph_toon_never_leaks_internal_fields) {
    /* The similarity/semantic pipeline intermediates (fp minhash hex, sp
     * structural profile, bt body-token bag) dominated the legacy payload
     * (~45%) and carry zero agent value. GUARD: they never appear in TOON
     * output — not by default and not even when explicitly requested via
     * fields (blocklist). */
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    /* A node whose properties carry the internal fields with sentinels. */
    cbm_node_t n = {0};
    n.project = "test-project";
    n.label = "Function";
    n.name = "fpCarrier";
    n.qualified_name = "test-project.src.fpCarrier";
    n.file_path = "src/fp.go";
    n.start_line = 1;
    n.end_line = 2;
    n.properties_json = "{\"fp\":\"FPSENTINEL00\",\"sp\":\"SPSENTINEL00\","
                        "\"bt\":\"BTSENTINEL00\",\"complexity\":7}";
    ASSERT_GT(cbm_store_upsert_node(st, &n), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":45,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"name_pattern\":\"fpCarrier\","
             "\"fields\":[\"fp\",\"sp\",\"bt\",\"complexity\"],\"limit\":5}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "fpCarrier"));
    ASSERT_NULL(strstr(inner, "FPSENTINEL00"));
    ASSERT_NULL(strstr(inner, "SPSENTINEL00"));
    ASSERT_NULL(strstr(inner, "BTSENTINEL00"));
    /* Non-blocked requested field still comes through. */
    ASSERT_NOT_NULL(strstr(inner, "complexity"));
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_lean_defaults_schema_and_status) {
    /* GUARDS for the lean-default contract (TOON round 2):
     * 1. get_graph_schema must not advertise the blocked internal fields
     *    (fp/sp/bt) — the server refuses to emit them, so listing them in the
     *    schema invited agents to request fields they can never get.
     * 2. index_status omits the git context block unless verbose:true — the
     *    worktree/shadow path variants only matter when debugging where an
     *    index lives. */
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    cbm_node_t n = {0};
    n.project = "test-project";
    n.label = "Function";
    n.name = "schemaCarrier";
    n.qualified_name = "test-project.src.schemaCarrier";
    n.file_path = "src/sc.go";
    n.start_line = 1;
    n.end_line = 2;
    n.properties_json = "{\"fp\":\"x\",\"sp\":\"y\",\"bt\":\"z\",\"complexity\":3}";
    ASSERT_GT(cbm_store_upsert_node(st, &n), 0);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":48,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"get_graph_schema\","
                                   "\"arguments\":{\"project\":\"test-project\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "Function"));   /* non-vacuous: label present */
    ASSERT_NOT_NULL(strstr(inner, "complexity")); /* obtainable property listed */
    ASSERT_NULL(strstr(inner, "\"fp\""));         /* blocked fields not advertised */
    ASSERT_NULL(strstr(inner, "\"sp\""));
    ASSERT_NULL(strstr(inner, "\"bt\""));
    free(inner);
    free(resp);

    /* index_status: no git block by default... */
    resp = cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":49,\"method\":\"tools/call\","
                                      "\"params\":{\"name\":\"index_status\","
                                      "\"arguments\":{\"project\":\"test-project\"}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"status\""));
    ASSERT_NULL(strstr(inner, "\"git\""));
    free(inner);
    free(resp);

    /* ...and present with verbose:true. */
    resp = cbm_mcp_server_handle(srv,
                                 "{\"jsonrpc\":\"2.0\",\"id\":50,\"method\":\"tools/call\","
                                 "\"params\":{\"name\":\"index_status\","
                                 "\"arguments\":{\"project\":\"test-project\",\"verbose\":true}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"git\""));
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── Tool-output regression suite (gating) ──────────────────────────
 * Context-explosion detector: flags the measured smells that re-introduce
 * token bloat into default outputs, independent of any specific tool:
 *   1. blocked internal fields (fp/sp/bt) appearing anywhere;
 *   2. repeated-key JSON envelopes — the same key emitted per row instead of
 *      a header-once table (the un-TOONed enumeration smell; detect_changes
 *      shipped 4,787x3 of these = 416KB);
 *   3. embedded prose notes/hints beyond one line (~220 chars) — long prose
 *      belongs in tool descriptions or docs, not repeated per response.
 * Returns NULL when clean, else a static description of the violation. */
static const char *output_explosion_smell(const char *inner) {
    static const char *row_keys[] = {
        "\"name\":", "\"label\":", "\"file\":", "\"path\":", "\"qualified_name\":", "\"qn\":"};
    if (strstr(inner, "\"fp\":") || strstr(inner, "\"sp\":") || strstr(inner, "\"bt\":")) {
        return "blocked internal field (fp/sp/bt) leaked into output";
    }
    for (size_t k = 0; k < sizeof(row_keys) / sizeof(row_keys[0]); k++) {
        int n = 0;
        for (const char *p = strstr(inner, row_keys[k]); p && n <= 32;
             p = strstr(p + 1, row_keys[k])) {
            n++;
        }
        if (n > 32) {
            return "repeated-key envelope (>32x same JSON key) — emit a header-once table";
        }
    }
    for (const char *p = strstr(inner, "\"note\":\""); p; p = strstr(p + 1, "\"note\":\"")) {
        const char *end = strchr(p + 9, '"');
        while (end && end[-1] == '\\') {
            end = strchr(end + 1, '"');
        }
        if (end && end - (p + 9) > 220) {
            return "embedded note exceeds one line (~220 chars)";
        }
    }
    return NULL;
}

/* Run one tool call on the fixture server, apply the explosion detector and
 * an absolute byte ceiling, and require a semantic-floor marker so trimming
 * can never hollow the response out either. */
static const char *check_tool_output(cbm_mcp_server_t *srv, const char *req, int ceiling,
                                     const char *floor_marker) {
    char *resp = cbm_mcp_server_handle(srv, req);
    if (!resp) {
        return "no response";
    }
    char *inner = extract_text_content(resp);
    free(resp);
    if (!inner) {
        return "no text content";
    }
    static char why[256];
    const char *smell = output_explosion_smell(inner);
    if (smell) {
        snprintf(why, sizeof(why), "%s", smell);
        free(inner);
        return why;
    }
    if ((int)strlen(inner) >= ceiling) {
        snprintf(why, sizeof(why), "output %d B >= ceiling %d B", (int)strlen(inner), ceiling);
        free(inner);
        return why;
    }
    if (floor_marker && !strstr(inner, floor_marker)) {
        snprintf(why, sizeof(why), "semantic floor missing: %s", floor_marker);
        free(inner);
        return why;
    }
    free(inner);
    return NULL;
}

TEST(tool_output_regression_gate) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    struct {
        const char *req;
        int ceiling;
        const char *floor;
    } cases[] = {
        {"{\"jsonrpc\":\"2.0\",\"id\":70,\"method\":\"tools/call\",\"params\":{"
         "\"name\":\"search_graph\",\"arguments\":{\"project\":\"test-project\","
         "\"name_pattern\":\".*\",\"limit\":50}}}",
         6000, "results:"},
        {"{\"jsonrpc\":\"2.0\",\"id\":71,\"method\":\"tools/call\",\"params\":{"
         "\"name\":\"get_graph_schema\",\"arguments\":{\"project\":\"test-project\"}}}",
         6000, "node_labels"},
        {"{\"jsonrpc\":\"2.0\",\"id\":72,\"method\":\"tools/call\",\"params\":{"
         "\"name\":\"index_status\",\"arguments\":{\"project\":\"test-project\"}}}",
         7000, "\"status\""},
        {"{\"jsonrpc\":\"2.0\",\"id\":73,\"method\":\"tools/call\",\"params\":{"
         "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"test-project\","
         "\"function_name\":\"HandleRequest\",\"direction\":\"both\"}}}",
         1500, "callees:"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *why = check_tool_output(srv, cases[i].req, cases[i].ceiling, cases[i].floor);
        if (why) {
            char msg[320];
            snprintf(msg, sizeof(msg), "case %d: %s", (int)i, why);
            FAIL(msg);
        }
    }

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_search_graph_query_honors_file_pattern_issue552) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "issue-552";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/issue-552");

    cbm_node_t lib_status = {0};
    lib_status.project = proj;
    lib_status.label = "Function";
    lib_status.name = "status";
    lib_status.qualified_name = "issue-552.src.lib.status";
    lib_status.file_path = "src/lib/status.c";
    lib_status.start_line = 1;
    lib_status.end_line = 3;
    ASSERT_GT(cbm_store_upsert_node(st, &lib_status), 0);

    cbm_node_t component_status = {0};
    component_status.project = proj;
    component_status.label = "Function";
    component_status.name = "status";
    component_status.qualified_name = "issue-552.src.components.status";
    component_status.file_path = "src/components/status.c";
    component_status.start_line = 1;
    component_status.end_line = 3;
    ASSERT_GT(cbm_store_upsert_node(st, &component_status), 0);

    cbm_store_exec(st, "INSERT INTO nodes_fts(nodes_fts) VALUES('delete-all');");
    ASSERT_EQ(cbm_store_exec(st,
                             "INSERT INTO nodes_fts(rowid, name, qualified_name, label, "
                             "file_path) "
                             "SELECT id, cbm_camel_split(name), qualified_name, label, file_path "
                             "FROM nodes;"),
              CBM_STORE_OK);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":552,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_graph\","
                                   "\"arguments\":{\"project\":\"issue-552\",\"query\":\"status\","
                                   "\"file_pattern\":\"src/lib/*\",\"limit\":10}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "search_mode: bm25"));
    ASSERT_NOT_NULL(strstr(inner, "src/lib/status.c"));
    ASSERT_NULL(strstr(inner, "src/components/status.c"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Resource discovery methods this server doesn't populate must return EMPTY
 * lists, not -32601 Method-not-found: clients like Cline probe them on connect
 * and surface the errors as a failed connection (#958). */
TEST(mcp_resource_discovery_methods_return_empty_lists) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    struct {
        const char *method;
        const char *want;
    } cases[] = {
        {"resources/list", "\"resources\":[]"},
        {"resources/templates/list", "\"resourceTemplates\":[]"},
    };
    for (int i = 0; i < 2; i++) {
        char reqbuf[256];
        snprintf(reqbuf, sizeof(reqbuf), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\"}",
                 100 + i, cases[i].method);
        char *resp = cbm_mcp_server_handle(srv, reqbuf);
        ASSERT_NOT_NULL(resp);
        ASSERT_NULL(strstr(resp, "Method not found"));
        ASSERT_NOT_NULL(strstr(resp, cases[i].want));
        free(resp);
    }

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_query_graph_basic) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"query_graph\","
             "\"arguments\":{\"query\":\"MATCH (f:Function) RETURN f.name\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"result\""));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_query_graph_max_rows_reports_truncation) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();
    cbm_store_t *store = cbm_mcp_server_store(srv);
    cbm_mcp_server_set_project(srv, "truncation-test");
    cbm_store_upsert_project(store, "truncation-test", "/tmp/truncation-test");
    for (int i = 0; i < 4; i++) {
        char name[CBM_SZ_32];
        char qualified_name[CBM_SZ_64];
        snprintf(name, sizeof(name), "function_%d", i);
        snprintf(qualified_name, sizeof(qualified_name), "truncation_test.function_%d", i);
        cbm_node_t node = {.project = "truncation-test",
                           .label = "Function",
                           .name = name,
                           .qualified_name = qualified_name,
                           .file_path = "fixture.c"};
        ASSERT_GT(cbm_store_upsert_node(store, &node), 0);
    }

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":141,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"query_graph\",\"arguments\":{"
             "\"project\":\"truncation-test\","
             "\"query\":\"MATCH (f:Function) RETURN f.name\",\"max_rows\":3}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "total: 3"));
    ASSERT_NOT_NULL(strstr(resp, "truncated: true"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_query_graph_max_rows_exact_match_is_complete) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();
    cbm_store_t *store = cbm_mcp_server_store(srv);
    cbm_mcp_server_set_project(srv, "complete-test");
    cbm_store_upsert_project(store, "complete-test", "/tmp/complete-test");
    for (int i = 0; i < 3; i++) {
        char name[CBM_SZ_32];
        char qualified_name[CBM_SZ_64];
        snprintf(name, sizeof(name), "function_%d", i);
        snprintf(qualified_name, sizeof(qualified_name), "complete_test.function_%d", i);
        cbm_node_t node = {.project = "complete-test",
                           .label = "Function",
                           .name = name,
                           .qualified_name = qualified_name,
                           .file_path = "fixture.c"};
        ASSERT_GT(cbm_store_upsert_node(store, &node), 0);
    }

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":142,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"query_graph\",\"arguments\":{"
             "\"project\":\"complete-test\","
             "\"query\":\"MATCH (f:Function) RETURN f.name\",\"max_rows\":3}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "total: 3"));
    ASSERT_NULL(strstr(resp, "truncated: true"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_query_graph_malformed_query_preserves_actionable_error) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":140,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"query_graph\","
                                   "\"arguments\":{\"project\":\"test-project\","
                                   "\"query\":\"MATCH (f RETURN f.name\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strlen(resp) <= CBM_MCP_RESULT_MAX_BYTES);
    ASSERT_NOT_NULL(strstr(resp, "\"isError\":true"));
    ASSERT_NOT_NULL(strstr(resp, "Invalid Cypher query:"));
    ASSERT_NOT_NULL(strstr(resp, "expected ')' but found RETURN at byte 9"));
    ASSERT_NOT_NULL(strstr(resp, "Context:"));
    ASSERT_NOT_NULL(strstr(resp, "Remedy: close the node pattern before RETURN"));
    ASSERT_NOT_NULL(strstr(resp, "MATCH (n:Function) RETURN n.name LIMIT 10"));
    ASSERT_NULL(strstr(resp, "token type"));
    free(resp);

    cbm_mcp_server_free(srv);
    th_rmtree(tmp);
    PASS();
}

TEST(tool_index_status_no_project) {
    cbm_mcp_server_t *srv = setup_mcp_with_data();

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"index_status\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    /* Should return error or empty status */
    ASSERT_NOT_NULL(strstr(resp, "\"result\""));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

static yyjson_val *index_status_page_files(yyjson_val *root, const char *section) {
    yyjson_val *object = yyjson_obj_get(root, section);
    return object ? yyjson_obj_get(object, "files") : NULL;
}

static int index_status_kind_slot(const char *kind) {
    if (strcmp(kind, "parse_partial") == 0) {
        return 0;
    }
    if (strcmp(kind, "read") == 0) {
        return 1;
    }
    if (strcmp(kind, "not_indexed_dir") == 0) {
        return 2;
    }
    return 3;
}

static int index_status_mark_object_paths(yyjson_val *array, bool (*seen)[4], int seen_count,
                                          const char *section_kind) {
    int represented = 0;
    size_t index, max;
    yyjson_val *item;
    yyjson_arr_foreach(array, index, max, item) {
        yyjson_val *path = yyjson_obj_get(item, "path");
        int ordinal = -1;
        ASSERT_TRUE(path && yyjson_is_str(path));
        ASSERT_EQ(sscanf(yyjson_get_str(path), "src/coverage-%04d.fixture", &ordinal), 1);
        ASSERT_TRUE(ordinal >= 0 && ordinal < seen_count);
        const char *kind = section_kind;
        yyjson_val *phase = yyjson_obj_get(item, "phase");
        if (phase && yyjson_is_str(phase)) {
            kind = yyjson_get_str(phase);
        }
        int slot = index_status_kind_slot(kind);
        ASSERT_FALSE(seen[ordinal][slot]);
        seen[ordinal][slot] = true;
        represented++;
    }
    return represented;
}

static int index_status_mark_dir_paths(yyjson_val *array, bool (*seen)[4], int seen_count) {
    int represented = 0;
    size_t index, max;
    yyjson_val *item;
    yyjson_arr_foreach(array, index, max, item) {
        int ordinal = -1;
        ASSERT_TRUE(yyjson_is_str(item));
        ASSERT_EQ(sscanf(yyjson_get_str(item), "src/coverage-%04d.fixture", &ordinal), 1);
        ASSERT_TRUE(ordinal >= 0 && ordinal < seen_count);
        ASSERT_FALSE(seen[ordinal][2]);
        seen[ordinal][2] = true;
        represented++;
    }
    return represented;
}

static size_t index_status_unpaged_fixture_envelope_bytes(const cbm_coverage_row_t *rows,
                                                          int row_count) {
    yyjson_mut_doc *payload_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *payload = yyjson_mut_obj(payload_doc);
    yyjson_mut_doc_set_root(payload_doc, payload);
    yyjson_mut_val *items = yyjson_mut_arr(payload_doc);
    for (int i = 0; i < row_count; i++) {
        yyjson_mut_val *item = yyjson_mut_obj(payload_doc);
        yyjson_mut_obj_add_strcpy(payload_doc, item, "path", rows[i].rel_path);
        yyjson_mut_obj_add_strcpy(payload_doc, item, "kind", rows[i].kind);
        yyjson_mut_obj_add_strcpy(payload_doc, item, "detail", rows[i].detail);
        yyjson_mut_arr_add_val(items, item);
    }
    yyjson_mut_obj_add_val(payload_doc, payload, "coverage", items);
    char *payload_text = yyjson_mut_write(payload_doc, 0, NULL);
    ASSERT_NOT_NULL(payload_text);

    yyjson_doc *structured_doc = yyjson_read(payload_text, strlen(payload_text), 0);
    ASSERT_NOT_NULL(structured_doc);
    yyjson_mut_doc *envelope_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *envelope = yyjson_mut_obj(envelope_doc);
    yyjson_mut_doc_set_root(envelope_doc, envelope);
    yyjson_mut_val *content = yyjson_mut_arr(envelope_doc);
    yyjson_mut_val *content_item = yyjson_mut_obj(envelope_doc);
    yyjson_mut_obj_add_str(envelope_doc, content_item, "type", "text");
    yyjson_mut_obj_add_strcpy(envelope_doc, content_item, "text", payload_text);
    yyjson_mut_arr_add_val(content, content_item);
    yyjson_mut_obj_add_val(envelope_doc, envelope, "content", content);
    yyjson_mut_obj_add_val(envelope_doc, envelope, "structuredContent",
                           yyjson_val_mut_copy(envelope_doc, yyjson_doc_get_root(structured_doc)));
    yyjson_mut_obj_add_bool(envelope_doc, envelope, "isError", false);
    char *envelope_text = yyjson_mut_write(envelope_doc, 0, NULL);
    ASSERT_NOT_NULL(envelope_text);
    size_t envelope_bytes = strlen(envelope_text);
    free(envelope_text);
    yyjson_mut_doc_free(envelope_doc);
    yyjson_doc_free(structured_doc);
    free(payload_text);
    yyjson_mut_doc_free(payload_doc);
    return envelope_bytes;
}

/* index_status is an exact, generation-bound coverage stream. The fixture is
 * deliberately larger than both the historical 500-row presentation cap and
 * the universal response ceiling, with JSON-escape-heavy details and one
 * individually oversized detail. Every page is reconstructed from the actual
 * represented rows, so advancing by requested rather than represented rows,
 * retaining the old cap, or omitting the generation check makes this fail. */
TEST(tool_index_status_coverage_pagination_is_exact_bounded_and_generation_bound) {
    enum { ROW_COUNT = 531, UNIQUE_PATH_COUNT = 530, PAGE_BUDGET = 4096 };
    const char *project = "coverage-page-stream";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/coverage-page-stream"), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    char (*paths)[64] = calloc(ROW_COUNT, sizeof(*paths));
    char (*details)[512] = calloc(ROW_COUNT, sizeof(*details));
    cbm_coverage_row_t *rows = calloc(ROW_COUNT, sizeof(*rows));
    char *oversized = malloc(20001);
    ASSERT_NOT_NULL(paths);
    ASSERT_NOT_NULL(details);
    ASSERT_NOT_NULL(rows);
    ASSERT_NOT_NULL(oversized);
    for (int i = 0; i < 20000; i++) {
        oversized[i] = "\\\"\n\tabcdef"[i % 10];
    }
    oversized[20000] = '\0';
    const char *kinds[] = {"parse_partial", "read", "not_indexed_dir", "not_indexed_file"};
    for (int i = 0; i < ROW_COUNT; i++) {
        int path_ordinal = i == ROW_COUNT - 1 ? UNIQUE_PATH_COUNT - 1 : i;
        snprintf(paths[i], sizeof(paths[i]), "src/coverage-%04d.fixture", path_ordinal);
        snprintf(details[i], sizeof(details[i]),
                 "detail-%04d-\\\\-\"-line1\nline2\t-"
                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-"
                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-"
                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
                 i);
        rows[i].rel_path = paths[i];
        rows[i].kind = i == ROW_COUNT - 1 ? "parse_partial" : kinds[i % 4];
        rows[i].detail = i == 0 ? oversized : details[i];
        ASSERT_EQ(cbm_store_upsert_file_hash(store, project, paths[i], "fixture", i + 1, 1),
                  CBM_STORE_OK);
    }
    ASSERT_TRUE(index_status_unpaged_fixture_envelope_bytes(rows, ROW_COUNT) >
                CBM_MCP_RESULT_MAX_BYTES);
    ASSERT_EQ(cbm_store_coverage_replace(store, project, rows, ROW_COUNT), CBM_STORE_OK);
    free(oversized);
    free(rows);
    free(details);
    free(paths);

    char *minimum_page = cbm_mcp_handle_tool(
        srv, "index_status",
        "{\"project\":\"coverage-page-stream\",\"limit\":500,\"max_response_bytes\":2048}");
    ASSERT_NOT_NULL(minimum_page);
    ASSERT_TRUE(strlen(minimum_page) <= 2048);
    free(minimum_page);
    char *maximum_page = cbm_mcp_handle_tool(
        srv, "index_status",
        "{\"project\":\"coverage-page-stream\",\"limit\":500,\"max_response_bytes\":65536}");
    ASSERT_NOT_NULL(maximum_page);
    ASSERT_TRUE(strlen(maximum_page) > PAGE_BUDGET);
    ASSERT_TRUE(strlen(maximum_page) <= 65536);
    free(maximum_page);

    bool seen[UNIQUE_PATH_COUNT][4] = {{false}};
    int expected_start = 0;
    int page_count = 0;
    char *cursor = NULL;
    char *first_cursor = NULL;
    bool saw_oversized_preview = false;
    do {
        char args[1024];
        if (cursor) {
            snprintf(args, sizeof(args),
                     "{\"project\":\"%s\",\"limit\":500,\"max_response_bytes\":%d,"
                     "\"cursor\":\"%s\"}",
                     project, PAGE_BUDGET, cursor);
        } else {
            snprintf(args, sizeof(args),
                     "{\"project\":\"%s\",\"limit\":500,\"max_response_bytes\":%d}", project,
                     PAGE_BUDGET);
        }
        char *result = cbm_mcp_handle_tool(srv, "index_status", args);
        ASSERT_NOT_NULL(result);
        ASSERT_TRUE(strlen(result) <= PAGE_BUDGET);
        ASSERT_TRUE(strlen(result) <= CBM_MCP_RESULT_MAX_BYTES);
        char *inner = extract_text_content(result);
        ASSERT_NOT_NULL(inner);
        yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
        yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
        ASSERT_TRUE(root && yyjson_is_obj(root));
        yyjson_val *page = yyjson_obj_get(root, "coverage_page");
        ASSERT_NOT_NULL(page);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(page, "start_ordinal")), expected_start);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(page, "total")), ROW_COUNT);
        int represented = (int)yyjson_get_int(yyjson_obj_get(page, "returned"));
        ASSERT_TRUE(represented > 0 && represented <= 500);

        yyjson_val *parse_partial = yyjson_obj_get(root, "parse_partial");
        yyjson_val *skipped = yyjson_obj_get(root, "skipped");
        yyjson_val *not_indexed = yyjson_obj_get(root, "not_indexed");
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(parse_partial, "count")), 134);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(skipped, "count")), 133);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(not_indexed, "dirs_count")), 132);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(not_indexed, "files_count")), 132);
        int emitted = 0;
        bool page_seen[UNIQUE_PATH_COUNT][4] = {{false}};
        yyjson_val *parse_files = index_status_page_files(root, "parse_partial");
        int parse_returned = (int)yyjson_arr_size(parse_files);
        int skipped_returned = (int)yyjson_arr_size(index_status_page_files(root, "skipped"));
        int dirs_returned = (int)yyjson_arr_size(yyjson_obj_get(not_indexed, "dirs"));
        int files_returned = (int)yyjson_arr_size(yyjson_obj_get(not_indexed, "files"));
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(parse_partial, "returned")), parse_returned);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(skipped, "returned")), skipped_returned);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(not_indexed, "dirs_returned")), dirs_returned);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(not_indexed, "files_returned")), files_returned);
        ASSERT_EQ(yyjson_get_bool(yyjson_obj_get(parse_partial, "truncated")),
                  parse_returned < 134);
        ASSERT_EQ(yyjson_get_bool(yyjson_obj_get(skipped, "truncated")), skipped_returned < 133);
        ASSERT_EQ(yyjson_get_bool(yyjson_obj_get(not_indexed, "truncated")),
                  dirs_returned < 132 || files_returned < 132);
        emitted += index_status_mark_object_paths(parse_files, page_seen, UNIQUE_PATH_COUNT,
                                                  "parse_partial");
        emitted += index_status_mark_object_paths(index_status_page_files(root, "skipped"),
                                                  page_seen, UNIQUE_PATH_COUNT, "read");
        emitted += index_status_mark_dir_paths(yyjson_obj_get(not_indexed, "dirs"), page_seen,
                                               UNIQUE_PATH_COUNT);
        emitted += index_status_mark_object_paths(yyjson_obj_get(not_indexed, "files"), page_seen,
                                                  UNIQUE_PATH_COUNT, "not_indexed_file");
        ASSERT_EQ(emitted, represented);
        bool expected_page[UNIQUE_PATH_COUNT][4] = {{false}};
        for (int stream_ordinal = expected_start; stream_ordinal < expected_start + represented;
             stream_ordinal++) {
            int path_ordinal;
            int kind_slot;
            if (stream_ordinal < UNIQUE_PATH_COUNT - 1) {
                path_ordinal = stream_ordinal;
                kind_slot = stream_ordinal % 4;
            } else {
                path_ordinal = UNIQUE_PATH_COUNT - 1;
                kind_slot = stream_ordinal == UNIQUE_PATH_COUNT - 1 ? 0 : 1;
            }
            expected_page[path_ordinal][kind_slot] = true;
        }
        for (int path_ordinal = 0; path_ordinal < UNIQUE_PATH_COUNT; path_ordinal++) {
            for (int kind_slot = 0; kind_slot < 4; kind_slot++) {
                ASSERT_EQ(page_seen[path_ordinal][kind_slot],
                          expected_page[path_ordinal][kind_slot]);
                if (page_seen[path_ordinal][kind_slot]) {
                    ASSERT_FALSE(seen[path_ordinal][kind_slot]);
                    seen[path_ordinal][kind_slot] = true;
                }
            }
        }
        expected_start += represented;

        if (page_count == 0) {
            yyjson_val *first = yyjson_arr_get(parse_files, 0);
            ASSERT_TRUE(first && yyjson_get_bool(yyjson_obj_get(first, "detail_truncated")));
            ASSERT_EQ(yyjson_get_uint(yyjson_obj_get(first, "detail_complete_bytes")), 20000);
            yyjson_val *hash = yyjson_obj_get(first, "detail_hash");
            ASSERT_TRUE(hash && yyjson_is_str(hash) &&
                        strncmp(yyjson_get_str(hash), "sha256:", 7) == 0);
            saw_oversized_preview = true;
        }

        yyjson_val *next = yyjson_obj_get(root, "next_cursor");
        char *next_cursor = next && yyjson_is_str(next) ? strdup(yyjson_get_str(next)) : NULL;
        if (page_count == 0 && next_cursor) {
            first_cursor = strdup(next_cursor);
        }
        bool truncated = yyjson_get_bool(yyjson_obj_get(page, "truncated"));
        ASSERT_EQ(truncated, next_cursor != NULL);
        yyjson_doc_free(doc);
        free(inner);
        free(result);
        free(cursor);
        cursor = next_cursor;
        page_count++;
        ASSERT_TRUE(page_count < ROW_COUNT + 2);
    } while (cursor);

    ASSERT_TRUE(saw_oversized_preview);
    ASSERT_TRUE(page_count > 1);
    ASSERT_EQ(expected_start, ROW_COUNT);
    for (int i = 0; i < UNIQUE_PATH_COUNT; i++) {
        ASSERT_TRUE(seen[i][i % 4]);
    }
    ASSERT_TRUE(seen[UNIQUE_PATH_COUNT - 1][0]);
    ASSERT_NOT_NULL(first_cursor);

    char replay[1024];
    snprintf(replay, sizeof(replay),
             "{\"project\":\"%s\",\"limit\":499,\"max_response_bytes\":%d,"
             "\"cursor\":\"%s\"}",
             project, PAGE_BUDGET, first_cursor);
    char *error = cbm_mcp_handle_tool(srv, "index_status", replay);
    ASSERT_NOT_NULL(strstr(error, "cursor_params_mismatch"));
    free(error);

    size_t cursor_len = strlen(first_cursor);
    first_cursor[cursor_len - 1] = first_cursor[cursor_len - 1] == '0' ? '1' : '0';
    snprintf(replay, sizeof(replay),
             "{\"project\":\"%s\",\"limit\":500,\"max_response_bytes\":%d,"
             "\"cursor\":\"%s\"}",
             project, PAGE_BUDGET, first_cursor);
    error = cbm_mcp_handle_tool(srv, "index_status", replay);
    ASSERT_NOT_NULL(strstr(error, "tampered_cursor"));
    free(error);

    snprintf(replay, sizeof(replay),
             "{\"project\":\"%s\",\"limit\":500,\"max_response_bytes\":%d,"
             "\"cursor\":\"is1:broken\"}",
             project, PAGE_BUDGET);
    error = cbm_mcp_handle_tool(srv, "index_status", replay);
    ASSERT_NOT_NULL(strstr(error, "invalid_cursor"));
    free(error);

    /* Restore the valid cursor, then change only the coverage stream. This
     * replacement deliberately does not bump the general store generation. */
    free(first_cursor);
    char *first_page = cbm_mcp_handle_tool(srv, "index_status",
                                           "{\"project\":\"coverage-page-stream\",\"limit\":500,"
                                           "\"max_response_bytes\":4096}");
    char *first_inner = extract_text_content(first_page);
    yyjson_doc *first_doc = yyjson_read(first_inner, strlen(first_inner), 0);
    yyjson_val *valid_next = yyjson_obj_get(yyjson_doc_get_root(first_doc), "next_cursor");
    first_cursor = strdup(yyjson_get_str(valid_next));
    yyjson_doc_free(first_doc);
    free(first_inner);
    free(first_page);
    cbm_coverage_row_t *replacement = NULL;
    int replacement_count = 0;
    ASSERT_EQ(cbm_store_coverage_get(store, project, &replacement, &replacement_count),
              CBM_STORE_OK);
    ASSERT_EQ(replacement_count, ROW_COUNT);
    ASSERT_TRUE(replacement[0].detail && replacement[0].detail[0]);
    char *changed_detail = strdup(replacement[0].detail);
    ASSERT_NOT_NULL(changed_detail);
    changed_detail[0] = changed_detail[0] == 'x' ? 'y' : 'x';
    free((char *)replacement[0].detail);
    replacement[0].detail = changed_detail;
    ASSERT_EQ(cbm_store_coverage_replace(store, project, replacement, replacement_count),
              CBM_STORE_OK);
    cbm_store_free_coverage(replacement, replacement_count);
    snprintf(replay, sizeof(replay),
             "{\"project\":\"%s\",\"limit\":500,\"max_response_bytes\":%d,"
             "\"cursor\":\"%s\"}",
             project, PAGE_BUDGET, first_cursor);
    error = cbm_mcp_handle_tool(srv, "index_status", replay);
    ASSERT_NOT_NULL(strstr(error, "stale_cursor"));
    free(error);

    /* A general project-generation change independently invalidates cursors. */
    free(first_cursor);
    first_page = cbm_mcp_handle_tool(srv, "index_status",
                                     "{\"project\":\"coverage-page-stream\",\"limit\":500,"
                                     "\"max_response_bytes\":4096}");
    first_inner = extract_text_content(first_page);
    first_doc = yyjson_read(first_inner, strlen(first_inner), 0);
    valid_next = yyjson_obj_get(yyjson_doc_get_root(first_doc), "next_cursor");
    first_cursor = strdup(yyjson_get_str(valid_next));
    yyjson_doc_free(first_doc);
    free(first_inner);
    free(first_page);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/coverage-page-stream"), CBM_STORE_OK);
    snprintf(replay, sizeof(replay),
             "{\"project\":\"%s\",\"limit\":500,\"max_response_bytes\":%d,"
             "\"cursor\":\"%s\"}",
             project, PAGE_BUDGET, first_cursor);
    error = cbm_mcp_handle_tool(srv, "index_status", replay);
    ASSERT_NOT_NULL(strstr(error, "stale_cursor"));
    free(error);
    free(first_cursor);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_index_status_rejects_invalid_pagination_arguments) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_EQ(cbm_store_upsert_project(store, "status-args", "/tmp/status-args"), CBM_STORE_OK);
    const char *cases[] = {
        "{\"project\":\"status-args\",\"limit\":0}",
        "{\"project\":\"status-args\",\"limit\":501}",
        "{\"project\":\"status-args\",\"limit\":\"5\"}",
        "{\"project\":\"status-args\",\"max_response_bytes\":2047}",
        "{\"project\":\"status-args\",\"max_response_bytes\":65537}",
        "{\"project\":\"status-args\",\"max_response_bytes\":4096.0}",
        "{\"project\":\"status-args\",\"cursor\":\"\"}",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *result = cbm_mcp_handle_tool(srv, "index_status", cases[i]);
        ASSERT_NOT_NULL(result);
        ASSERT_NOT_NULL(strstr(result, "isError\":true"));
        free(result);
    }
    char *bounded_error =
        cbm_mcp_handle_tool(srv, "index_status",
                            "{\"project\":\"definitely-not-an-indexed-project-coverage-budget\","
                            "\"max_response_bytes\":2048}");
    ASSERT_NOT_NULL(bounded_error);
    ASSERT_TRUE(strlen(bounded_error) <= 2048);
    ASSERT_NOT_NULL(strstr(bounded_error, "isError\":true"));
    free(bounded_error);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_index_status_oversized_identity_marker_advances) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    const char *project = "coverage-oversized-identity";
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/coverage-oversized-identity"),
              CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    char *path = malloc(6001);
    ASSERT_NOT_NULL(path);
    memcpy(path, "src/", 4);
    memset(path + 4, 'p', 5994);
    memcpy(path + 5998, ".c", 3);
    char *kind = malloc(6001);
    ASSERT_NOT_NULL(kind);
    memset(kind, 'k', 6000);
    kind[6000] = '\0';
    cbm_coverage_row_t row = {.rel_path = path, .kind = kind, .detail = "1-2"};
    ASSERT_EQ(cbm_store_upsert_file_hash(store, project, path, "fixture", 1, 1), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_coverage_replace(store, project, &row, 1), CBM_STORE_OK);

    char *result = cbm_mcp_handle_tool(
        srv, "index_status",
        "{\"project\":\"coverage-oversized-identity\",\"max_response_bytes\":2048}");
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strlen(result) <= 2048);
    char *inner = extract_text_content(result);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *marker = root ? yyjson_obj_get(root, "oversized_item") : NULL;
    ASSERT_NOT_NULL(marker);
    ASSERT_EQ(yyjson_get_uint(yyjson_obj_get(marker, "ordinal")), 0);
    ASSERT_EQ(yyjson_get_uint(yyjson_obj_get(marker, "identity_complete_bytes")),
              strlen(path) + strlen(kind));
    ASSERT_NOT_NULL(yyjson_obj_get(marker, "identity_hash"));
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(marker, "kind_truncated")));
    ASSERT_EQ(yyjson_get_uint(yyjson_obj_get(marker, "kind_complete_bytes")), strlen(kind));
    ASSERT_EQ(strlen(yyjson_get_str(yyjson_obj_get(marker, "kind"))), 128);
    yyjson_val *page = yyjson_obj_get(root, "coverage_page");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(page, "returned")), 1);
    ASSERT_FALSE(yyjson_get_bool(yyjson_obj_get(page, "truncated")));
    ASSERT_NULL(yyjson_obj_get(root, "next_cursor"));

    yyjson_doc_free(doc);
    free(inner);
    free(result);
    free(path);
    free(kind);
    cbm_mcp_server_free(srv);
    PASS();
}

/* index_status is a bounded paged presentation, so a later path need not be on
 * its first page even though the authoritative index_coverage table contains
 * it. The targeted coverage tool must query that table rather than scan one
 * presentation page. */
TEST(tool_check_index_coverage_finds_path_beyond_status_cap) {
    enum { ROW_COUNT = 502 };
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *project = "coverage-cap-regression";
    ASSERT_EQ(cbm_store_upsert_project(st, project, "/tmp/coverage-cap-regression"), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    char (*paths)[64] = calloc(ROW_COUNT, sizeof(*paths));
    cbm_coverage_row_t *rows = calloc(ROW_COUNT, sizeof(*rows));
    ASSERT_NOT_NULL(paths);
    ASSERT_NOT_NULL(rows);
    for (int i = 0; i < ROW_COUNT; i++) {
        snprintf(paths[i], sizeof(paths[i]), "src/partial-%04d.c", i);
        rows[i].rel_path = paths[i];
        rows[i].kind = "parse_partial";
        rows[i].detail = i == ROW_COUNT - 1 ? "777-790" : "1-2";
        ASSERT_EQ(cbm_store_upsert_file_hash(st, project, paths[i], "fixture", i + 1, 10),
                  CBM_STORE_OK);
    }
    ASSERT_EQ(cbm_store_coverage_replace(st, project, rows, ROW_COUNT), CBM_STORE_OK);

    char *status =
        cbm_mcp_handle_tool(srv, "index_status", "{\"project\":\"coverage-cap-regression\"}");
    ASSERT_NOT_NULL(status);
    char *status_inner = extract_text_content(status);
    ASSERT_NOT_NULL(status_inner);
    ASSERT_NOT_NULL(strstr(status_inner, "\"truncated\":true"));
    ASSERT_NULL(strstr(status_inner, "src/partial-0501.c"));
    free(status_inner);
    free(status);

    char *coverage = cbm_mcp_handle_tool(
        srv, "check_index_coverage",
        "{\"project\":\"coverage-cap-regression\",\"paths\":[\"src/partial-0501.c\"]}");
    ASSERT_NOT_NULL(coverage);
    char *coverage_inner = extract_text_content(coverage);
    ASSERT_NOT_NULL(coverage_inner);
    ASSERT_NOT_NULL(strstr(coverage_inner, "src/partial-0501.c"));
    ASSERT_NOT_NULL(strstr(coverage_inner, "\"status\":\"partial\""));
    ASSERT_NOT_NULL(strstr(coverage_inner, "777-790"));

    free(coverage_inner);
    free(coverage);
    free(rows);
    free(paths);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_check_index_coverage_reports_paths_scopes_and_ranges) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    ASSERT_EQ(cbm_store_upsert_file_hash(st, "test-project", "main.go", "", 0, 0), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(st, "test-project", "src/skip.c", "", 0, 0), CBM_STORE_OK);
    cbm_coverage_row_t rows[] = {
        {.rel_path = "main.go", .kind = "parse_partial", .detail = "3-4,9"},
        {.rel_path = "generated", .kind = "not_indexed_dir", .detail = "excluded subtree"},
        {.rel_path = "src/skip.c", .kind = "oversized", .detail = "file exceeds cap"},
    };
    ASSERT_EQ(cbm_store_coverage_replace(st, "test-project", rows, 3), CBM_STORE_OK);

    char *coverage =
        cbm_mcp_handle_tool(srv, "check_index_coverage",
                            "{\"project\":\"test-project\","
                            "\"paths\":[\"main.go\",\"generated/pkg/a.c\",\"../escape.c\"],"
                            "\"scopes\":[\".\"]}");
    ASSERT_NOT_NULL(coverage);
    char *inner = extract_text_content(coverage);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"path\":\"main.go\""));
    ASSERT_NOT_NULL(strstr(inner, "\"status\":\"partial\""));
    ASSERT_NOT_NULL(strstr(inner, "\"start\":3"));
    ASSERT_NOT_NULL(strstr(inner, "\"end\":4"));
    ASSERT_NOT_NULL(strstr(inner, "\"start\":9"));
    ASSERT_NOT_NULL(strstr(inner, "generated/pkg/a.c"));
    ASSERT_NOT_NULL(strstr(inner, "not_indexed_dir"));
    ASSERT_NOT_NULL(strstr(inner, "outside_project"));
    ASSERT_NOT_NULL(strstr(inner, "src/skip.c"));
    ASSERT_NOT_NULL(strstr(inner, "file exceeds cap"));
    ASSERT_NOT_NULL(strstr(inner, "best_effort"));

    free(inner);
    free(coverage);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_check_index_coverage_preserves_multiple_scope_labels) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *coverage = cbm_mcp_handle_tool(srv, "check_index_coverage",
                                         "{\"project\":\"test-project\","
                                         "\"scopes\":[\"alpha/one\",\"bravo/two\",\"charl/tri\"]}");
    ASSERT_NOT_NULL(coverage);
    char *inner = extract_text_content(coverage);
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *scopes = yyjson_obj_get(yyjson_doc_get_root(doc), "scopes");
    ASSERT_NOT_NULL(scopes);
    ASSERT_TRUE(yyjson_is_arr(scopes));
    ASSERT_EQ(yyjson_arr_size(scopes), 3);

    const char *expected[] = {"alpha/one", "bravo/two", "charl/tri"};
    for (size_t i = 0; i < 3; i++) {
        yyjson_val *scope = yyjson_obj_get(yyjson_arr_get(scopes, i), "scope");
        ASSERT_NOT_NULL(scope);
        ASSERT_TRUE(yyjson_is_str(scope));
        ASSERT_STR_EQ(yyjson_get_str(scope), expected[i]);
    }

    yyjson_doc_free(doc);
    free(inner);
    free(coverage);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

typedef struct {
    cbm_store_t *store;
    int calls;
    int fail_on_call;
} syntactic_page_failure_hook_t;

static void syntactic_page_fail_allocation_on_call(void *userdata) {
    syntactic_page_failure_hook_t *hook = userdata;
    hook->calls++;
    if (hook->calls == hook->fail_on_call) {
        cbm_store_syntactic_coverage_test_fail_alloc_after(hook->store, 0);
    }
}

typedef struct {
    cbm_store_t *writer;
    const char *project;
    int calls;
    int replace_on_call;
    int rc;
    cbm_coverage_row_t *replacement;
    int replacement_count;
    cbm_coverage_meta_t replacement_meta;
} mcp_syntactic_replacement_hook_t;

static void mcp_syntactic_replace_on_page(void *userdata) {
    mcp_syntactic_replacement_hook_t *hook = userdata;
    hook->calls++;
    if (hook->calls == hook->replace_on_call) {
        hook->rc = cbm_store_coverage_replace_ex(hook->writer, hook->project, hook->replacement,
                                                 hook->replacement_count, &hook->replacement_meta);
    }
}

TEST(tool_index_status_syntactic_pages_hold_one_readonly_snapshot) {
    enum { ROW_COUNT = 1100 };
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-mcp-syntactic-snapshot-XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "mcp-syntactic-snapshot";
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    cbm_store_t *writer = cbm_store_open_path(db_path);
    ASSERT_NOT_NULL(writer);
    ASSERT_EQ(cbm_store_upsert_project(writer, project, "/tmp/mcp-syntactic-snapshot"),
              CBM_STORE_OK);
    cbm_project_t info = {0};
    ASSERT_EQ(cbm_store_get_project(writer, project, &info), CBM_STORE_OK);

    cbm_coverage_row_t *initial = calloc(ROW_COUNT, sizeof(*initial));
    cbm_coverage_row_t *replacement = calloc(ROW_COUNT, sizeof(*replacement));
    char (*initial_paths)[32] = calloc(ROW_COUNT, sizeof(*initial_paths));
    char (*replacement_paths)[32] = calloc(ROW_COUNT, sizeof(*replacement_paths));
    ASSERT_NOT_NULL(initial);
    ASSERT_NOT_NULL(replacement);
    ASSERT_NOT_NULL(initial_paths);
    ASSERT_NOT_NULL(replacement_paths);
    for (int i = 0; i < ROW_COUNT; i++) {
        snprintf(initial_paths[i], sizeof(initial_paths[i]), "old/%04d.c", i);
        snprintf(replacement_paths[i], sizeof(replacement_paths[i]), "new/%04d.c", i);
        ASSERT_EQ(
            cbm_store_upsert_file_hash(writer, project, initial_paths[i], "fixture", i + 1, 1),
            CBM_STORE_OK);
        ASSERT_EQ(cbm_store_upsert_file_hash(writer, project, replacement_paths[i], "fixture",
                                             ROW_COUNT + i + 1, 1),
                  CBM_STORE_OK);
        initial[i] = (cbm_coverage_row_t){
            .rel_path = initial_paths[i], .kind = "not_indexed_file", .detail = "old"};
        replacement[i] = (cbm_coverage_row_t){
            .rel_path = replacement_paths[i], .kind = "not_indexed_file", .detail = "new"};
    }
    cbm_coverage_meta_t initial_meta = {.generation = info.indexed_at,
                                        .index_mode = "fast",
                                        .recording_status = "complete",
                                        .coverage_version = 1,
                                        .hash_records_complete = true};
    ASSERT_EQ(cbm_store_coverage_replace_ex(writer, project, initial, ROW_COUNT, &initial_meta),
              CBM_STORE_OK);
    cbm_project_free_fields(&info);

    cbm_store_t *reader = cbm_store_open_path_query(db_path);
    ASSERT_NOT_NULL(reader);
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_mcp_server_test_use_borrowed_store(srv, reader, project);
    char *warm = cbm_mcp_handle_tool(srv, "index_status",
                                     "{\"project\":\"mcp-syntactic-snapshot\",\"limit\":500}");
    ASSERT_NOT_NULL(warm);
    char *warm_inner = extract_text_content(warm);
    yyjson_doc *warm_doc = yyjson_read(warm_inner, strlen(warm_inner), 0);
    yyjson_val *warm_root = yyjson_doc_get_root(warm_doc);
    yyjson_val *warm_page = yyjson_obj_get(warm_root, "coverage_page");
    int64_t warm_end = yyjson_get_int(yyjson_obj_get(warm_page, "start_ordinal")) +
                       yyjson_get_int(yyjson_obj_get(warm_page, "returned"));
    yyjson_val *warm_cursor = yyjson_obj_get(warm_root, "next_cursor");
    ASSERT_TRUE(warm_cursor && yyjson_is_str(warm_cursor));
    char *cursor = strdup(yyjson_get_str(warm_cursor));
    yyjson_doc_free(warm_doc);
    free(warm_inner);
    free(warm);
    mcp_syntactic_replacement_hook_t hook = {
        .writer = writer,
        .project = project,
        .replace_on_call = 2,
        .rc = CBM_STORE_ERR,
        .replacement = replacement,
        .replacement_count = ROW_COUNT,
        .replacement_meta = {.generation = "replacement-generation",
                             .index_mode = "full",
                             .recording_status = "complete",
                             .coverage_version = 1,
                             .hash_records_complete = true},
    };
    cbm_store_syntactic_coverage_test_set_after_totals_hook(reader, mcp_syntactic_replace_on_page,
                                                            &hook);
    char args[1024];
    snprintf(args, sizeof(args),
             "{\"project\":\"mcp-syntactic-snapshot\",\"limit\":500,"
             "\"max_response_bytes\":65536,\"cursor\":\"%s\"}",
             cursor);
    char *response = cbm_mcp_handle_tool(srv, "index_status", args);
    ASSERT_EQ(hook.rc, CBM_STORE_OK);
    ASSERT_NOT_NULL(response);
    ASSERT_NULL(strstr(response, "failed to read a complete coverage snapshot"));
    char *inner = extract_text_content(response);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(yyjson_obj_get(root, "coverage_page"), "total")),
              ROW_COUNT);
    ASSERT_EQ(
        yyjson_get_int(yyjson_obj_get(yyjson_obj_get(root, "coverage_page"), "start_ordinal")),
        warm_end);
    ASSERT_NOT_NULL(strstr(inner, "old/"));
    ASSERT_NULL(strstr(inner, "new/"));
    yyjson_doc_free(doc);
    free(inner);
    free(response);
    free(cursor);

    cbm_store_syntactic_coverage_test_set_after_totals_hook(reader, NULL, NULL);
    cbm_mcp_server_free(srv);
    cbm_store_close(reader);
    cbm_store_close(writer);
    free(replacement_paths);
    free(initial_paths);
    free(replacement);
    free(initial);
    cleanup_project_db(cache, project);
    cbm_rmdir(cache);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    PASS();
}

TEST(tool_syntactic_coverage_routes_page_typed_rows_and_fail_closed) {
    enum {
        SCOPE_ROWS = 600,
        SEMANTIC_ROWS = 600,
        EXACT_PHASE_ROWS = 300,
        SYNTACTIC_ROWS = SCOPE_ROWS + EXACT_PHASE_ROWS + 5,
        ROW_COUNT = SCOPE_ROWS + SEMANTIC_ROWS + EXACT_PHASE_ROWS + 5,
    };
    char repo[256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-mcp-syntactic-pages-XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(repo));
    char target_path[512];
    snprintf(target_path, sizeof(target_path), "%s/target.c", repo);
    ASSERT_EQ(th_write_file(target_path, "int target(void) { return 1; }\n"), 0);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    const char *project = "syntactic-page-routes";
    ASSERT_EQ(cbm_store_upsert_project(store, project, repo), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);
    ASSERT_EQ(cbm_store_upsert_file_hash(store, project, "target.c", "fixture", 1, 31),
              CBM_STORE_OK);
    cbm_node_t node = {.project = project,
                       .label = "Function",
                       .name = "target",
                       .qualified_name = "syntactic-page-routes.target",
                       .file_path = "target.c",
                       .start_line = 1,
                       .end_line = 1};
    ASSERT_GT(cbm_store_upsert_node(store, &node), 0);

    cbm_coverage_row_t *rows = calloc(ROW_COUNT, sizeof(*rows));
    char (*paths)[32] = calloc(ROW_COUNT, sizeof(*paths));
    char (*kinds)[32] = calloc(ROW_COUNT, sizeof(*kinds));
    char *oversized_kind = malloc(18001);
    char *delimited_kind = malloc(18003);
    char *budget_kind = malloc(701);
    ASSERT_NOT_NULL(oversized_kind);
    ASSERT_NOT_NULL(delimited_kind);
    ASSERT_NOT_NULL(budget_kind);
    memset(oversized_kind, 'q', 18000);
    oversized_kind[18000] = '\0';
    memcpy(delimited_kind, "b|", 2);
    memcpy(delimited_kind + 2, oversized_kind, 18001);
    memset(budget_kind, 'r', 700);
    budget_kind[700] = '\0';
    char target_detail[300];
    memset(target_detail, ' ', sizeof(target_detail));
    memcpy(target_detail + sizeof(target_detail) - 7, "123456", 7);
    ASSERT_NOT_NULL(rows);
    ASSERT_NOT_NULL(paths);
    ASSERT_NOT_NULL(kinds);
    int n = 0;
    for (int i = 0; i < SCOPE_ROWS; i++, n++) {
        snprintf(paths[n], sizeof(paths[n]), "s/%04d.c", i);
        ASSERT_EQ(cbm_store_upsert_file_hash(store, project, paths[n], "fixture", i + 2, 1),
                  CBM_STORE_OK);
        rows[n] = (cbm_coverage_row_t){
            .rel_path = paths[n], .kind = "parse_partial", .detail = i == 0 ? "1-999999" : "1"};
    }
    for (int i = 0; i < SEMANTIC_ROWS; i++, n++) {
        snprintf(paths[n], sizeof(paths[n]), "s/%04d.c", i);
        rows[n] = (cbm_coverage_row_t){.rel_path = paths[n],
                                       .kind = "analysis_partial:rust",
                                       .detail = "semantic-must-not-leak"};
    }
    for (int i = 0; i < EXACT_PHASE_ROWS; i++, n++) {
        snprintf(kinds[n], sizeof(kinds[n]), "phase_%04d", i);
        rows[n] = (cbm_coverage_row_t){.rel_path = "target.c", .kind = kinds[n], .detail = "phase"};
    }
    rows[n++] = (cbm_coverage_row_t){
        .rel_path = "target.c", .kind = "parse_partial", .detail = target_detail};
    rows[n++] = (cbm_coverage_row_t){
        .rel_path = "sx/a", .kind = delimited_kind, .detail = "prefix collision"};
    rows[n++] = (cbm_coverage_row_t){
        .rel_path = "sx/a|b", .kind = oversized_kind, .detail = "prefix collision"};
    rows[n++] = (cbm_coverage_row_t){
        .rel_path = "sy/a", .kind = budget_kind, .detail = "ordinary budget exhaustion"};
    rows[n++] = (cbm_coverage_row_t){.rel_path = "sy/b",
                                     .kind = budget_kind,
                                     .detail = "ordinary budget exhaustion continuation"};
    ASSERT_EQ(cbm_store_upsert_file_hash(store, project, "sx/a", "fixture", 1, 1), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(store, project, "sx/a|b", "fixture", 1, 1), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(store, project, "sy/a", "fixture", 1, 1), CBM_STORE_OK);
    ASSERT_EQ(cbm_store_upsert_file_hash(store, project, "sy/b", "fixture", 1, 1), CBM_STORE_OK);
    ASSERT_EQ(n, ROW_COUNT);
    cbm_project_t project_info = {0};
    ASSERT_EQ(cbm_store_get_project(store, project, &project_info), CBM_STORE_OK);
    cbm_coverage_meta_t meta = {.generation = project_info.indexed_at,
                                .index_mode = "fast",
                                .recorded_at = "2026-08-14T00:00:00Z",
                                .recording_status = "complete",
                                .coverage_version = 1,
                                .hash_records_complete = true};
    ASSERT_EQ(cbm_store_coverage_replace_ex(store, project, rows, ROW_COUNT, &meta), CBM_STORE_OK);
    cbm_project_free_fields(&project_info);

    int ordinal = 0;
    char *cursor = NULL;
    do {
        char args[1024];
        if (cursor) {
            snprintf(args, sizeof(args), "{\"project\":\"%s\",\"limit\":500,\"cursor\":\"%s\"}",
                     project, cursor);
        } else {
            snprintf(args, sizeof(args), "{\"project\":\"%s\",\"limit\":500}", project);
        }
        char *response = cbm_mcp_handle_tool(srv, "index_status", args);
        ASSERT_NOT_NULL(response);
        ASSERT_TRUE(strlen(response) <= CBM_MCP_RESULT_MAX_BYTES);
        char *inner = extract_text_content(response);
        yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
        yyjson_val *root = yyjson_doc_get_root(doc);
        yyjson_val *page = yyjson_obj_get(root, "coverage_page");
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(page, "total")), SYNTACTIC_ROWS);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(page, "start_ordinal")), ordinal);
        ordinal += (int)yyjson_get_int(yyjson_obj_get(page, "returned"));
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(yyjson_obj_get(root, "parse_partial"), "count")),
                  SCOPE_ROWS + 1);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(yyjson_obj_get(root, "skipped"), "count")),
                  EXACT_PHASE_ROWS + 4);
        yyjson_val *not_indexed = yyjson_obj_get(root, "not_indexed");
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(not_indexed, "files_count")), 0);
        yyjson_val *next = yyjson_obj_get(root, "next_cursor");
        char *next_cursor = next ? strdup(yyjson_get_str(next)) : NULL;
        yyjson_doc_free(doc);
        free(inner);
        free(response);
        free(cursor);
        cursor = next_cursor;
    } while (cursor);
    ASSERT_EQ(ordinal, SYNTACTIC_ROWS);

    char *response =
        cbm_mcp_handle_tool(srv, "check_index_coverage",
                            "{\"project\":\"syntactic-page-routes\",\"paths\":[\"target.c\"]}");
    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(strlen(response) <= CBM_MCP_RESULT_MAX_BYTES);
    char *inner = extract_text_content(response);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *path = yyjson_arr_get(yyjson_obj_get(root, "paths"), 0);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(path, "status")), "partial");
    int path_returned = (int)yyjson_arr_size(yyjson_obj_get(path, "coverage"));
    ASSERT_TRUE(path_returned > 0 && path_returned < EXACT_PHASE_ROWS + 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(path, "coverage_total")), EXACT_PHASE_ROWS + 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(path, "coverage_returned")), path_returned);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(path, "coverage_truncated")));
    yyjson_val *coverage_items = yyjson_obj_get(path, "coverage");
    yyjson_val *partial_item = yyjson_arr_get(coverage_items, 0);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(partial_item, "kind")), "parse_partial");
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(partial_item, "detail_truncated")));
    ASSERT_NULL(yyjson_obj_get(partial_item, "ranges"));
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    response = cbm_mcp_handle_tool(srv, "check_index_coverage",
                                   "{\"project\":\"syntactic-page-routes\",\"scopes\":[\"s\"],"
                                   "\"scope_limit\":300}");
    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(strlen(response) <= CBM_MCP_RESULT_MAX_BYTES);
    inner = extract_text_content(response);
    doc = yyjson_read(inner, strlen(inner), 0);
    root = yyjson_doc_get_root(doc);
    yyjson_val *scope = yyjson_arr_get(yyjson_obj_get(root, "scopes"), 0);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(scope, "total")), SCOPE_ROWS);
    int scope_returned = (int)yyjson_arr_size(yyjson_obj_get(scope, "entries"));
    ASSERT_TRUE(scope_returned > 0 && scope_returned < 300);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(scope, "has_more")));
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(scope, "next_offset")), scope_returned);
    ASSERT_NULL(strstr(inner, "sx/a"));
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    response = cbm_mcp_handle_tool(srv, "check_index_coverage",
                                   "{\"project\":\"syntactic-page-routes\",\"scopes\":[\"sx\"],"
                                   "\"scope_limit\":1}");
    inner = extract_text_content(response);
    doc = yyjson_read(inner, strlen(inner), 0);
    scope = yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(doc), "scopes"), 0);
    yyjson_val *oversized_entries = yyjson_obj_get(scope, "entries");
    ASSERT_EQ(yyjson_arr_size(oversized_entries), 1);
    yyjson_val *first_marker = yyjson_arr_get(oversized_entries, 0);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(first_marker, "oversized_item")));
    const char *first_hash = yyjson_get_str(yyjson_obj_get(first_marker, "identity_hash"));
    ASSERT_NOT_NULL(first_hash);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(scope, "has_more")));
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(scope, "next_offset")), 1);
    char *first_hash_copy = strdup(first_hash);
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    response = cbm_mcp_handle_tool(srv, "check_index_coverage",
                                   "{\"project\":\"syntactic-page-routes\",\"scopes\":[\"sx\"],"
                                   "\"scope_limit\":1,\"scope_offset\":1}");
    inner = extract_text_content(response);
    doc = yyjson_read(inner, strlen(inner), 0);
    scope = yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(doc), "scopes"), 0);
    yyjson_val *second_marker = yyjson_arr_get(yyjson_obj_get(scope, "entries"), 0);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(second_marker, "oversized_item")));
    ASSERT_TRUE(strcmp(yyjson_get_str(yyjson_obj_get(second_marker, "identity_hash")),
                       first_hash_copy) != 0);
    ASSERT_FALSE(yyjson_get_bool(yyjson_obj_get(scope, "has_more")));
    free(first_hash_copy);
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    yyjson_mut_doc *fanout_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *fanout_root = yyjson_mut_obj(fanout_doc);
    yyjson_mut_doc_set_root(fanout_doc, fanout_root);
    yyjson_mut_obj_add_str(fanout_doc, fanout_root, "project", project);
    yyjson_mut_val *fanout_paths = yyjson_mut_arr(fanout_doc);
    for (int i = 0; i < 128; i++) {
        yyjson_mut_arr_add_str(fanout_doc, fanout_paths, "target.c");
    }
    yyjson_mut_obj_add_val(fanout_doc, fanout_root, "paths", fanout_paths);
    yyjson_mut_val *fanout_scopes = yyjson_mut_arr(fanout_doc);
    for (int i = 0; i < 32; i++) {
        yyjson_mut_arr_add_str(fanout_doc, fanout_scopes, "sy");
    }
    yyjson_mut_obj_add_val(fanout_doc, fanout_root, "scopes", fanout_scopes);
    yyjson_mut_obj_add_int(fanout_doc, fanout_root, "scope_limit", 1);
    char *fanout_args = yyjson_mut_write(fanout_doc, 0, NULL);
    yyjson_mut_doc_free(fanout_doc);
    ASSERT_NOT_NULL(fanout_args);
    response = cbm_mcp_handle_tool(srv, "check_index_coverage", fanout_args);
    ASSERT_TRUE(strlen(response) <= CBM_MCP_RESULT_MAX_BYTES);
    inner = extract_text_content(response);
    doc = inner ? yyjson_read(inner, strlen(inner), 0) : NULL;
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    yyjson_val *fanout_path_results = yyjson_obj_get(root, "paths");
    yyjson_val *fanout_scope_results = yyjson_obj_get(root, "scopes");
    ASSERT_EQ(yyjson_arr_size(fanout_path_results), 128);
    ASSERT_EQ(yyjson_arr_size(fanout_scope_results), 32);
    ASSERT_EQ(
        yyjson_get_uint(yyjson_obj_get(yyjson_arr_get(fanout_path_results, 127), "duplicate_of")),
        0);
    yyjson_val *fanout_first_scope = yyjson_arr_get(fanout_scope_results, 0);
    yyjson_val *fanout_first_entry =
        yyjson_arr_get(yyjson_obj_get(fanout_first_scope, "entries"), 0);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(fanout_first_entry, "evidence_omitted_item")));
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(fanout_first_entry, "omission_reason")),
                  "evidence_budget_exhausted");
    ASSERT_NOT_NULL(yyjson_get_str(yyjson_obj_get(fanout_first_entry, "identity_hash")));
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(fanout_first_scope, "next_offset")), 1);
    ASSERT_EQ(
        yyjson_get_uint(yyjson_obj_get(yyjson_arr_get(fanout_scope_results, 31), "duplicate_of")),
        0);
    yyjson_doc_free(doc);
    free(inner);
    free(response);
    free(fanout_args);

    yyjson_mut_doc *too_wide_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *too_wide_root = yyjson_mut_obj(too_wide_doc);
    yyjson_mut_doc_set_root(too_wide_doc, too_wide_root);
    yyjson_mut_obj_add_str(too_wide_doc, too_wide_root, "project", project);
    yyjson_mut_val *too_wide_paths = yyjson_mut_arr(too_wide_doc);
    for (int i = 0; i < 65; i++) {
        char distinct_path[64];
        snprintf(distinct_path, sizeof(distinct_path), "distinct/%02d.c", i);
        yyjson_mut_arr_add_strcpy(too_wide_doc, too_wide_paths, distinct_path);
    }
    yyjson_mut_obj_add_val(too_wide_doc, too_wide_root, "paths", too_wide_paths);
    char *too_wide_args = yyjson_mut_write(too_wide_doc, 0, NULL);
    yyjson_mut_doc_free(too_wide_doc);
    response = cbm_mcp_handle_tool(srv, "check_index_coverage", too_wide_args);
    ASSERT_NOT_NULL(strstr(response, "cannot fit a truthful bounded response"));
    ASSERT_NULL(strstr(response, "safe response envelope exceeded"));
    free(response);
    free(too_wide_args);

    char *long_duplicate = malloc(4096);
    ASSERT_NOT_NULL(long_duplicate);
    memset(long_duplicate, 'd', 4095);
    long_duplicate[4095] = '\0';
    yyjson_mut_doc *duplicate_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *duplicate_root = yyjson_mut_obj(duplicate_doc);
    yyjson_mut_doc_set_root(duplicate_doc, duplicate_root);
    yyjson_mut_obj_add_str(duplicate_doc, duplicate_root, "project", project);
    yyjson_mut_val *duplicate_paths = yyjson_mut_arr(duplicate_doc);
    for (int i = 0; i < 3; i++) {
        yyjson_mut_arr_add_strcpy(duplicate_doc, duplicate_paths, long_duplicate);
    }
    yyjson_mut_obj_add_val(duplicate_doc, duplicate_root, "paths", duplicate_paths);
    char *duplicate_args = yyjson_mut_write(duplicate_doc, 0, NULL);
    yyjson_mut_doc_free(duplicate_doc);
    response = cbm_mcp_handle_tool(srv, "check_index_coverage", duplicate_args);
    ASSERT_NOT_NULL(strstr(response, "cannot fit a truthful bounded response"));
    ASSERT_NULL(strstr(response, "safe response envelope exceeded"));
    free(response);
    free(duplicate_args);
    free(long_duplicate);

    response = cbm_mcp_handle_tool(
        srv, "get_code_snippet",
        "{\"project\":\"syntactic-page-routes\","
        "\"qualified_name\":\"syntactic-page-routes.target\",\"max_response_bytes\":65536}");
    ASSERT_NOT_NULL(strstr(response, "PARTIALLY indexed"));
    ASSERT_NOT_NULL(strstr(response, "too large to include exactly"));
    ASSERT_NULL(strstr(response, "line range(s)"));
    free(response);

    syntactic_page_failure_hook_t hook = {.store = store, .fail_on_call = 2};
    cbm_store_syntactic_coverage_test_set_after_totals_hook(
        store, syntactic_page_fail_allocation_on_call, &hook);
    response = cbm_mcp_handle_tool(srv, "index_status",
                                   "{\"project\":\"syntactic-page-routes\",\"limit\":500,"
                                   "\"max_response_bytes\":65536}");
    ASSERT_NOT_NULL(strstr(response, "index_status failed to read a complete coverage snapshot"));
    ASSERT_NOT_NULL(strstr(response, "\"isError\":true"));
    free(response);

    cbm_store_syntactic_coverage_test_fail_alloc_after(store, -1);
    hook.calls = 0;
    hook.fail_on_call = 2;
    response =
        cbm_mcp_handle_tool(srv, "check_index_coverage",
                            "{\"project\":\"syntactic-page-routes\",\"paths\":[\"target.c\"]}");
    ASSERT_NOT_NULL(strstr(response, "\"coverage_lookup\":\"error\""));
    ASSERT_NOT_NULL(strstr(response, "\"status\":\"coverage_unavailable\""));
    ASSERT_NOT_NULL(strstr(response, "\"coverage\":[]"));
    free(response);

    cbm_store_syntactic_coverage_test_fail_alloc_after(store, -1);
    hook.calls = 0;
    hook.fail_on_call = 1;
    response = cbm_mcp_handle_tool(srv, "check_index_coverage",
                                   "{\"project\":\"syntactic-page-routes\",\"scopes\":[\"s\"],"
                                   "\"scope_limit\":300}");
    ASSERT_NOT_NULL(strstr(response, "\"coverage_lookup\":\"error\""));
    ASSERT_NOT_NULL(strstr(response, "\"status\":\"coverage_unavailable\""));
    ASSERT_NOT_NULL(strstr(response, "\"total\":600"));
    ASSERT_NOT_NULL(strstr(response, "\"entries\":[]"));
    free(response);

    cbm_store_syntactic_coverage_test_fail_alloc_after(store, -1);
    hook.calls = 0;
    hook.fail_on_call = 2;
    response = cbm_mcp_handle_tool(
        srv, "get_code_snippet",
        "{\"project\":\"syntactic-page-routes\","
        "\"qualified_name\":\"syntactic-page-routes.target\",\"max_response_bytes\":65536}");
    ASSERT_NOT_NULL(strstr(response, "Coverage lookup unavailable"));
    free(response);
    cbm_store_syntactic_coverage_test_set_after_totals_hook(store, NULL, NULL);
    cbm_store_syntactic_coverage_test_fail_alloc_after(store, -1);

    long source_len = 0;
    unsigned char *source = mcp_read_file_bytes("src/mcp/mcp.c", &source_len);
    ASSERT_NOT_NULL(source);
    ASSERT_NULL(strstr((const char *)source, "cbm_store_coverage_get(store"));
    ASSERT_NULL(strstr((const char *)source, "cbm_store_coverage_get_path("));
    ASSERT_NULL(strstr((const char *)source, "cbm_store_coverage_get_scope("));
    ASSERT_NOT_NULL(strstr((const char *)source, "CBM_SYNTACTIC_COVERAGE_PROJECT"));
    ASSERT_NOT_NULL(strstr((const char *)source, "CBM_SYNTACTIC_COVERAGE_EXACT_PATH"));
    ASSERT_NOT_NULL(strstr((const char *)source, "CBM_SYNTACTIC_COVERAGE_SCOPE"));
    free(source);

    free(budget_kind);
    free(kinds);
    free(paths);
    free(rows);
    free(delimited_kind);
    free(oversized_kind);
    cbm_mcp_server_free(srv);
    th_rmtree(repo);
    PASS();
}

static int write_coverage_meta(cbm_store_t *store, const char *generation,
                               const char *recording_status) {
    cbm_coverage_meta_t meta = {
        .generation = generation,
        .index_mode = "fast",
        .recorded_at = "2026-07-12T00:00:00Z",
        .recording_status = recording_status,
        .ignored_files_stored = 0,
        .ignored_files_total = 0,
        .coverage_version = 1,
        .hash_records_complete = true,
    };
    return cbm_store_coverage_replace_ex(store, "test-project", NULL, 0, &meta);
}

static int write_rust_health_fixture(cbm_store_t *store, const char *project, int version,
                                     const char *rust_recording, int rust_files_total,
                                     const cbm_coverage_row_t *rows, int row_count) {
    cbm_project_t info = {0};
    if (cbm_store_get_project(store, project, &info) != CBM_STORE_OK) {
        return CBM_STORE_ERR;
    }
    cbm_coverage_meta_t meta = {
        .generation = info.indexed_at,
        .index_mode = "fast",
        .recorded_at = "2026-08-13T00:00:00Z",
        .recording_status = "complete",
        .ignored_files_stored = 0,
        .ignored_files_total = 0,
        .coverage_version = version,
        .hash_records_complete = true,
        .rust_analysis_recording_status = rust_recording,
        .rust_files_total = rust_files_total,
    };
    for (int i = 0; i < row_count; i++) {
        if (rows[i].rel_path && cbm_store_upsert_file_hash(store, project, rows[i].rel_path,
                                                           "fixture", i + 1, 1) != CBM_STORE_OK) {
            cbm_project_free_fields(&info);
            return CBM_STORE_ERR;
        }
    }
    int rc = cbm_store_coverage_replace_ex(store, project, rows, row_count, &meta);
    cbm_project_free_fields(&info);
    return rc;
}

static yyjson_doc *mcp_tool_inner_doc(cbm_mcp_server_t *srv, const char *tool, const char *args,
                                      char **response_out, char **inner_out) {
    *response_out = cbm_mcp_handle_tool(srv, tool, args);
    if (!*response_out) {
        return NULL;
    }
    *inner_out = extract_text_content(*response_out);
    return *inner_out ? yyjson_read(*inner_out, strlen(*inner_out), 0) : NULL;
}

TEST(tool_rust_analysis_health_verdicts_are_metadata_gated_and_exact) {
    const char *project = "rust-health-verdicts";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/rust-health-verdicts"), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    cbm_coverage_row_t mixed[] = {
        {.rel_path = "src/a.rs",
         .kind = "analysis_failed:rust",
         .detail = "{\"status\":\"failed\"}"},
        {.rel_path = "src/a.rs",
         .kind = "analysis_partial:rust",
         .detail = "{\"status\":\"partial\"}"},
        {.rel_path = "src/b.rs",
         .kind = "analysis_partial:rust",
         .detail = "{\"status\":\"partial\"}"},
        {.rel_path = "src/b.rs", .kind = "parse_partial", .detail = "7-9"},
    };
    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION, "complete", 4,
                                        mixed, 4),
              CBM_STORE_OK);
    cbm_coverage_row_t *stored_rows = NULL;
    int stored_row_count = 0;
    ASSERT_EQ(cbm_store_coverage_get(store, project, &stored_rows, &stored_row_count),
              CBM_STORE_OK);
    ASSERT_EQ(stored_row_count, 4);
    ASSERT_STR_EQ(stored_rows[0].kind, "analysis_failed:rust");
    cbm_store_free_coverage(stored_rows, stored_row_count);

    char *response = NULL;
    char *inner = NULL;
    yyjson_doc *doc = mcp_tool_inner_doc(
        srv, "index_status", "{\"project\":\"rust-health-verdicts\",\"max_response_bytes\":65536}",
        &response, &inner);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *health = yyjson_obj_get(root, "rust_analysis");
    ASSERT_NOT_NULL(health);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "failed");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_total")), 4);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_complete")), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_partial")), 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_failed")), 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "degraded_files_total")), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "partial_rows")), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "failed_rows")), 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(yyjson_obj_get(root, "coverage_page"), "total")), 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(yyjson_obj_get(root, "skipped"), "count")), 0);
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    doc = mcp_tool_inner_doc(
        srv, "check_index_coverage",
        "{\"project\":\"rust-health-verdicts\",\"paths\":[\"src/a.rs\"],\"scopes\":[\".\"]}",
        &response, &inner);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    health = yyjson_obj_get(root, "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "failed");
    yyjson_val *path = yyjson_arr_get(yyjson_obj_get(root, "paths"), 0);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(path, "status")), "no_recorded_issue");
    ASSERT_EQ(yyjson_arr_size(yyjson_obj_get(path, "coverage")), 0);
    yyjson_val *scope = yyjson_arr_get(yyjson_obj_get(root, "scopes"), 0);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(scope, "total")), 1);
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION - 1, "complete",
                                        4, mixed, 4),
              CBM_STORE_OK);
    doc = mcp_tool_inner_doc(srv, "index_status", "{\"project\":\"rust-health-verdicts\"}",
                             &response, &inner);
    ASSERT_NOT_NULL(doc);
    health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "unknown");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "reason")), "unsupported_coverage_version");
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION, "complete", 0,
                                        NULL, 0),
              CBM_STORE_OK);
    doc = mcp_tool_inner_doc(srv, "index_status", "{\"project\":\"rust-health-verdicts\"}",
                             &response, &inner);
    ASSERT_NOT_NULL(doc);
    health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "not_applicable");
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION, "complete", 2,
                                        NULL, 0),
              CBM_STORE_OK);
    doc = mcp_tool_inner_doc(srv, "index_status", "{\"project\":\"rust-health-verdicts\"}",
                             &response, &inner);
    ASSERT_NOT_NULL(doc);
    health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "complete");
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION, "unknown", 2,
                                        NULL, 0),
              CBM_STORE_OK);
    doc = mcp_tool_inner_doc(srv, "index_status", "{\"project\":\"rust-health-verdicts\"}",
                             &response, &inner);
    ASSERT_NOT_NULL(doc);
    health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "unknown");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "reason")),
                  "rust_analysis_recording_unavailable");
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_rust_analysis_evidence_has_independent_16k_budget) {
    enum { ROW_COUNT = 768 };
    const char *project = "rust-health-evidence-budget";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/rust-health-evidence-budget"),
              CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);
    cbm_coverage_row_t *rows = calloc(ROW_COUNT, sizeof(*rows));
    char (*paths)[64] = calloc(ROW_COUNT, sizeof(*paths));
    char (*details)[2048] = calloc(ROW_COUNT, sizeof(*details));
    ASSERT_NOT_NULL(rows);
    ASSERT_NOT_NULL(paths);
    ASSERT_NOT_NULL(details);
    for (int i = 0; i < ROW_COUNT; i++) {
        snprintf(paths[i], sizeof(paths[i]), "src/health-%03d.rs", i);
        for (int j = 0; j < 2046; j++) {
            details[i][j] = "\\\"\n\tabcdef"[j % 10];
        }
        details[i][2046] = '\0';
        rows[i].rel_path = paths[i];
        rows[i].kind = "analysis_partial:rust";
        rows[i].detail = details[i];
    }
    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION, "complete",
                                        ROW_COUNT, rows, ROW_COUNT),
              CBM_STORE_OK);
    free(details);
    free(paths);
    free(rows);

    char *response = NULL;
    char *inner = NULL;
    yyjson_doc *doc = mcp_tool_inner_doc(
        srv, "check_index_coverage",
        "{\"project\":\"rust-health-evidence-budget\",\"paths\":[\"src/health-000.rs\"]}",
        &response, &inner);
    ASSERT_NOT_NULL(doc);
    ASSERT_TRUE(strlen(response) <= 65536);
    yyjson_val *health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "partial");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_partial")), ROW_COUNT);
    yyjson_val *evidence = yyjson_obj_get(health, "evidence");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(evidence, "scope")), "first_page_summary");
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(evidence, "truncated")));
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(evidence, "total")), ROW_COUNT);
    ASSERT_TRUE(yyjson_get_int(yyjson_obj_get(evidence, "returned")) < ROW_COUNT);
    yyjson_mut_doc *evidence_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(evidence_doc, yyjson_val_mut_copy(evidence_doc, evidence));
    char *evidence_json = yyjson_mut_write(evidence_doc, 0, NULL);
    ASSERT_NOT_NULL(evidence_json);
    ASSERT_TRUE(strlen(evidence_json) <= 16384);
    free(evidence_json);
    yyjson_mut_doc_free(evidence_doc);
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    /* Max accepted path/scope fan-out must still leave the full MCP envelope bounded. */
    yyjson_mut_doc *max_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *max_root = yyjson_mut_obj(max_doc);
    yyjson_mut_doc_set_root(max_doc, max_root);
    yyjson_mut_obj_add_str(max_doc, max_root, "project", project);
    yyjson_mut_val *max_paths = yyjson_mut_arr(max_doc);
    for (int i = 0; i < 128; i++) {
        yyjson_mut_arr_add_str(max_doc, max_paths, "src/health-000.rs");
    }
    yyjson_mut_obj_add_val(max_doc, max_root, "paths", max_paths);
    yyjson_mut_val *max_scopes = yyjson_mut_arr(max_doc);
    for (int i = 0; i < 32; i++) {
        yyjson_mut_arr_add_str(max_doc, max_scopes, ".");
    }
    yyjson_mut_obj_add_val(max_doc, max_root, "scopes", max_scopes);
    char *max_args = yyjson_mut_write(max_doc, 0, NULL);
    ASSERT_NOT_NULL(max_args);
    yyjson_mut_doc_free(max_doc);
    doc = mcp_tool_inner_doc(srv, "check_index_coverage", max_args, &response, &inner);
    ASSERT_NOT_NULL(doc);
    ASSERT_TRUE(strlen(response) <= 65536);
    ASSERT_NOT_NULL(yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis"));
    yyjson_doc_free(doc);
    free(inner);
    free(response);
    free(max_args);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_rust_analysis_pages_mixed_corpus_without_gaps_or_syntactic_contamination) {
    enum { SEMANTIC_ROWS = 1200, SYNTACTIC_ROWS = 2000, ROW_COUNT = 3200 };
    const char *project = "rust-health-paged-mixed";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/rust-health-paged-mixed"),
              CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    cbm_coverage_row_t *rows = calloc(ROW_COUNT, sizeof(*rows));
    char (*paths)[32] = calloc(ROW_COUNT, sizeof(*paths));
    ASSERT_NOT_NULL(rows);
    ASSERT_NOT_NULL(paths);
    for (int i = 0; i < SEMANTIC_ROWS; i++) {
        snprintf(paths[i], sizeof(paths[i]), "a%04d", i);
        rows[i].rel_path = paths[i];
        rows[i].kind = i % 7 == 0 ? "analysis_failed:rust" : "analysis_partial:rust";
        rows[i].detail = "";
    }
    for (int i = 0; i < SYNTACTIC_ROWS; i++) {
        int row = SEMANTIC_ROWS + i;
        snprintf(paths[row], sizeof(paths[row]), "z%04d.c", i);
        rows[row].rel_path = paths[row];
        rows[row].kind = "parse_partial";
        rows[row].detail = "1";
    }
    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION, "complete",
                                        SEMANTIC_ROWS, rows, ROW_COUNT),
              CBM_STORE_OK);
    free(paths);
    free(rows);

    char *response = NULL;
    char *inner = NULL;
    yyjson_doc *doc = mcp_tool_inner_doc(
        srv, "check_index_coverage",
        "{\"project\":\"rust-health-paged-mixed\",\"paths\":[\"a0000\"]}", &response, &inner);
    ASSERT_NOT_NULL(doc);
    yyjson_val *health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_NOT_NULL(health);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "failed");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "degraded_files_total")), SEMANTIC_ROWS);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "partial_rows")), SEMANTIC_ROWS - 172);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "failed_rows")), 172);
    yyjson_val *evidence = yyjson_obj_get(health, "evidence");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(evidence, "total")), SEMANTIC_ROWS);
    int returned = (int)yyjson_get_int(yyjson_obj_get(evidence, "returned"));
    ASSERT_TRUE(returned > CBM_ANALYSIS_COVERAGE_PAGE_MAX_ROWS);
    yyjson_val *items = yyjson_obj_get(evidence, "items");
    ASSERT_EQ(yyjson_arr_size(items), returned);
    for (int i = 0; i < returned; i++) {
        char expected[16];
        snprintf(expected, sizeof(expected), "a%04d", i);
        yyjson_val *item = yyjson_arr_get(items, i);
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(item, "path")), expected);
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(item, "kind")),
                      i % 7 == 0 ? "analysis_failed:rust" : "analysis_partial:rust");
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(item, "detail")), "");
    }
    ASSERT_NULL(strstr(inner, "z0000.c"));
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    doc = mcp_tool_inner_doc(srv, "index_status",
                             "{\"project\":\"rust-health-paged-mixed\",\"limit\":1,"
                             "\"max_response_bytes\":65536}",
                             &response, &inner);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    health = yyjson_obj_get(root, "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "failed");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "degraded_files_total")), SEMANTIC_ROWS);
    evidence = yyjson_obj_get(health, "evidence");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(evidence, "scope")), "first_page_summary");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(evidence, "total")), SEMANTIC_ROWS);
    returned = (int)yyjson_get_int(yyjson_obj_get(evidence, "returned"));
    ASSERT_TRUE(returned > CBM_ANALYSIS_COVERAGE_PAGE_MAX_ROWS);
    items = yyjson_obj_get(evidence, "items");
    ASSERT_EQ(yyjson_arr_size(items), returned);
    for (int i = 0; i < returned; i++) {
        char expected[16];
        snprintf(expected, sizeof(expected), "a%04d", i);
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(yyjson_arr_get(items, i), "path")), expected);
    }
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(yyjson_obj_get(root, "coverage_page"), "total")),
              SYNTACTIC_ROWS);
    yyjson_val *parse_files = yyjson_obj_get(yyjson_obj_get(root, "parse_partial"), "files");
    ASSERT_EQ(yyjson_arr_size(parse_files), 1);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(yyjson_arr_get(parse_files, 0), "path")),
                  "z0000.c");
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    /* Rust evidence is intentionally a summary, not a second paginated stream.
     * It appears once, while every later syntactic page retains exact health
     * aggregates and says explicitly where the evidence lives. Reconstruct 20
     * coverage pages and measure the context avoided by not repeating it. */
    char *cursor = NULL;
    size_t first_evidence_bytes = 0;
    size_t later_summary_bytes = 0;
    size_t measured_saved_bytes = 0;
    int coverage_pages = 0;
    do {
        char args[1024];
        if (cursor) {
            snprintf(args, sizeof(args),
                     "{\"project\":\"rust-health-paged-mixed\",\"limit\":100,"
                     "\"max_response_bytes\":65536,\"cursor\":\"%s\"}",
                     cursor);
        } else {
            snprintf(args, sizeof(args),
                     "{\"project\":\"rust-health-paged-mixed\",\"limit\":100,"
                     "\"max_response_bytes\":65536}");
        }
        doc = mcp_tool_inner_doc(srv, "index_status", args, &response, &inner);
        ASSERT_NOT_NULL(doc);
        ASSERT_TRUE(strlen(response) <= CBM_MCP_RESULT_MAX_BYTES);
        root = yyjson_doc_get_root(doc);
        health = yyjson_obj_get(root, "rust_analysis");
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "degraded_files_total")), SEMANTIC_ROWS);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "partial_rows")), SEMANTIC_ROWS - 172);
        ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "failed_rows")), 172);
        yyjson_val *page_evidence = yyjson_obj_get(health, "evidence");
        if (coverage_pages == 0) {
            ASSERT_NOT_NULL(page_evidence);
            yyjson_mut_doc *copy = yyjson_mut_doc_new(NULL);
            yyjson_mut_doc_set_root(copy, yyjson_val_mut_copy(copy, page_evidence));
            char *serialized = yyjson_mut_write(copy, 0, NULL);
            ASSERT_NOT_NULL(serialized);
            first_evidence_bytes = strlen(serialized);
            free(serialized);
            yyjson_mut_doc_free(copy);
        } else {
            ASSERT_NULL(page_evidence);
            ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "evidence_scope")),
                          "first_page_summary");
            ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "evidence_total")), SEMANTIC_ROWS);
            ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "evidence_returned")), 0);
            ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(health, "evidence_truncated")));
            if (later_summary_bytes == 0) {
                yyjson_mut_doc *copy = yyjson_mut_doc_new(NULL);
                yyjson_mut_val *summary = yyjson_mut_obj(copy);
                yyjson_mut_doc_set_root(copy, summary);
                yyjson_mut_obj_add_str(copy, summary, "evidence_scope", "first_page_summary");
                yyjson_mut_obj_add_int(copy, summary, "evidence_total", SEMANTIC_ROWS);
                yyjson_mut_obj_add_int(copy, summary, "evidence_returned", 0);
                yyjson_mut_obj_add_bool(copy, summary, "evidence_truncated", true);
                char *serialized = yyjson_mut_write(copy, 0, NULL);
                ASSERT_NOT_NULL(serialized);
                later_summary_bytes = strlen(serialized);
                free(serialized);
                yyjson_mut_doc_free(copy);
            }
            ASSERT_TRUE(first_evidence_bytes > later_summary_bytes);
            measured_saved_bytes += first_evidence_bytes - later_summary_bytes;
        }
        yyjson_val *next = yyjson_obj_get(root, "next_cursor");
        char *next_cursor = next ? strdup(yyjson_get_str(next)) : NULL;
        yyjson_doc_free(doc);
        free(inner);
        free(response);
        free(cursor);
        cursor = next_cursor;
        coverage_pages++;
        ASSERT_TRUE(coverage_pages <= 20);
    } while (cursor);
    ASSERT_EQ(coverage_pages, 20);
    ASSERT_EQ(measured_saved_bytes, (first_evidence_bytes - later_summary_bytes) * 19U);

    /* Six metadata strings, one row array, then three strings per row exhaust
     * exactly after the first full store page; the next page must fail closed
     * without retaining the first page as apparently complete evidence. */
    cbm_store_analysis_coverage_test_fail_alloc_after(store,
                                                      7 + 3 * CBM_ANALYSIS_COVERAGE_PAGE_MAX_ROWS);
    doc = mcp_tool_inner_doc(srv, "index_status", "{\"project\":\"rust-health-paged-mixed\"}",
                             &response, &inner);
    ASSERT_NOT_NULL(doc);
    health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "unknown");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "reason")),
                  "analysis_evidence_unavailable");
    ASSERT_NULL(yyjson_obj_get(health, "evidence"));
    yyjson_doc_free(doc);
    free(inner);
    free(response);
    cbm_store_analysis_coverage_test_fail_alloc_after(store, -1);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_rust_analysis_page_failures_are_unknown_and_use_shared_version_contract) {
    const char *project = "rust-health-page-failure";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/rust-health-page-failure"),
              CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);
    cbm_coverage_row_t row = {
        .rel_path = "src/a.rs", .kind = "analysis_partial:rust", .detail = "{}"};
    ASSERT_EQ(write_rust_health_fixture(store, project, CBM_SEMANTIC_INDEX_VERSION, "complete", 1,
                                        &row, 1),
              CBM_STORE_OK);

    cbm_store_analysis_coverage_test_fail_alloc_after(store, 0);
    char *response = NULL;
    char *inner = NULL;
    yyjson_doc *doc = mcp_tool_inner_doc(
        srv, "index_status", "{\"project\":\"rust-health-page-failure\"}", &response, &inner);
    ASSERT_NOT_NULL(doc);
    yyjson_val *health = yyjson_obj_get(yyjson_doc_get_root(doc), "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "unknown");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "reason")),
                  "analysis_evidence_unavailable");
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    doc = mcp_tool_inner_doc(srv, "check_index_coverage",
                             "{\"project\":\"rust-health-page-failure\",\"paths\":[\"src/a.rs\"]}",
                             &response, &inner);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    health = yyjson_obj_get(root, "rust_analysis");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "unknown");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "reason")),
                  "analysis_evidence_unavailable");
    yyjson_val *metadata = yyjson_obj_get(root, "metadata");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(metadata, "recording_status")), "complete");
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(metadata, "generation_matches")));
    yyjson_val *path = yyjson_arr_get(yyjson_obj_get(root, "paths"), 0);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(path, "status")), "no_recorded_issue");
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    cbm_store_analysis_coverage_test_fail_alloc_after(store, -1);
    ASSERT_EQ(
        cbm_store_exec(store, "ALTER TABLE index_coverage_meta RENAME TO broken_coverage_meta;"),
        CBM_STORE_OK);
    doc = mcp_tool_inner_doc(srv, "index_status", "{\"project\":\"rust-health-page-failure\"}",
                             &response, &inner);
    ASSERT_NULL(doc);
    ASSERT_NOT_NULL(strstr(response, "index_status failed to read a complete coverage snapshot"));
    free(inner);
    free(response);

    long source_len = 0;
    unsigned char *source = mcp_read_file_bytes("src/mcp/mcp.c", &source_len);
    ASSERT_NOT_NULL(source);
    ASSERT_NOT_NULL(strstr((const char *)source, "meta->coverage_version != "
                                                 "CBM_SEMANTIC_INDEX_VERSION"));
    ASSERT_NULL(strstr((const char *)source, "RUST_ANALYSIS_COVERAGE_VERSION"));
    free(source);
    cbm_mcp_server_free(srv);
    PASS();
}

static int index_repository_success_reports_large_semantic_health_corpus_check(const char *repo,
                                                                               const char *cache) {
    enum { RUST_FILES = CBM_ANALYSIS_COVERAGE_PAGE_MAX_ROWS * 4 + 1 };
    for (int i = 0; i < RUST_FILES; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/r%04d.rs", repo, i);
        ASSERT_EQ(th_write_file(path, "fn broken( {\n"), 0);
    }

    cbm_setenv("CBM_CACHE_DIR", cache, 1);
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    char args[512];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", repo);
    char *response = NULL;
    char *inner = NULL;
    yyjson_doc *doc = mcp_tool_inner_doc(srv, "index_repository", args, &response, &inner);
    ASSERT_NOT_NULL(doc);
    ASSERT_TRUE(strlen(response) <= 65536);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(root, "status")), "indexed");
    yyjson_val *health = yyjson_obj_get(root, "rust_analysis");
    ASSERT_NOT_NULL(health);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(health, "verdict")), "partial");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_total")), RUST_FILES);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_complete")), 0);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_partial")), RUST_FILES);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "files_failed")), 0);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "degraded_files_total")), RUST_FILES);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "partial_rows")), RUST_FILES);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(health, "failed_rows")), 0);
    yyjson_val *evidence = yyjson_obj_get(health, "evidence");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(evidence, "total")), RUST_FILES);
    int returned = (int)yyjson_get_int(yyjson_obj_get(evidence, "returned"));
    ASSERT_TRUE(returned > 0 && returned < RUST_FILES);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(evidence, "truncated")));
    yyjson_val *items = yyjson_obj_get(evidence, "items");
    ASSERT_EQ(yyjson_arr_size(items), returned);
    const char *first_detail = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(items, 0), "detail"));
    ASSERT_TRUE(first_detail && first_detail[0]);
    for (int i = 0; i < returned; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "r%04d.rs", i);
        yyjson_val *item = yyjson_arr_get(items, i);
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(item, "path")), expected);
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(item, "kind")), "analysis_partial:rust");
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(item, "detail")), first_detail);
    }
    yyjson_mut_doc *evidence_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(evidence_doc, yyjson_val_mut_copy(evidence_doc, evidence));
    char *evidence_json = yyjson_mut_write(evidence_doc, 0, NULL);
    ASSERT_NOT_NULL(evidence_json);
    ASSERT_TRUE(strlen(evidence_json) <= 16384);

    free(evidence_json);
    yyjson_mut_doc_free(evidence_doc);
    yyjson_doc_free(doc);
    free(inner);
    free(response);
    cbm_mcp_server_free(srv);
    return 0;
}

TEST(tool_index_repository_success_reports_large_semantic_health_corpus) {
#ifdef _WIN32
    SKIP_PLATFORM("POSIX fork harness required for failure-isolated environment and cleanup");
#else
    char repo[256];
    char cache[256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-mcp-index-rust-pages-XXXXXX");
    snprintf(cache, sizeof(cache), "/tmp/cbm-mcp-index-rust-cache-XXXXXX");
    ASSERT_NOT_NULL(cbm_mkdtemp(repo));
    if (!cbm_mkdtemp(cache)) {
        th_rmtree(repo);
        FAIL("cbm_mkdtemp cache failed");
    }

    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(60);
        _exit(index_repository_success_reports_large_semantic_health_corpus_check(repo, cache));
    }
    int status = 0;
    bool waited = pid > 0 && waitpid(pid, &status, 0) == pid;
    char *project = cbm_project_name_from_path(repo);
    cleanup_project_db(cache, project);
    free(project);
    th_rmtree(cache);
    th_rmtree(repo);
    ASSERT_TRUE(waited);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    PASS();
#endif
}

TEST(tool_check_index_coverage_rejects_stale_generation) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(write_coverage_meta(store, "stale-generation", "complete"), CBM_STORE_OK);

    char *response = cbm_mcp_handle_tool(srv, "check_index_coverage",
                                         "{\"project\":\"test-project\",\"paths\":[\"main.go\"]}");
    ASSERT_NOT_NULL(response);
    char *inner = extract_text_content(response);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"generation_matches\":false"));
    ASSERT_NOT_NULL(strstr(inner, "\"status\":\"coverage_unavailable\""));
    ASSERT_NOT_NULL(strstr(inner, "\"recommended_action\":\"read_source_and_reindex\""));

    free(inner);
    free(response);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_check_index_coverage_requires_source_when_file_metadata_changed) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    cbm_project_t project = {0};
    ASSERT_EQ(cbm_store_get_project(store, "test-project", &project), CBM_STORE_OK);
    ASSERT_EQ(write_coverage_meta(store, project.indexed_at, "complete"), CBM_STORE_OK);
    cbm_project_free_fields(&project);
    ASSERT_EQ(cbm_store_upsert_file_hash(store, "test-project", "main.go", "fixture", 0, 0),
              CBM_STORE_OK);

    char *response = cbm_mcp_handle_tool(srv, "check_index_coverage",
                                         "{\"project\":\"test-project\",\"paths\":[\"main.go\"]}");
    ASSERT_NOT_NULL(response);
    char *inner = extract_text_content(response);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"generation_matches\":true"));
    ASSERT_NOT_NULL(strstr(inner, "\"freshness\":\"metadata_changed\""));
    ASSERT_NOT_NULL(strstr(inner, "\"recommended_action\":\"read_source_and_reindex\""));

    free(inner);
    free(response);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_check_index_coverage_surfaces_lookup_errors) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    cbm_project_t project = {0};
    ASSERT_EQ(cbm_store_get_project(store, "test-project", &project), CBM_STORE_OK);
    ASSERT_EQ(write_coverage_meta(store, project.indexed_at, "complete"), CBM_STORE_OK);
    cbm_project_free_fields(&project);
    ASSERT_EQ(
        cbm_store_exec(store, "ALTER TABLE index_coverage RENAME COLUMN detail TO broken_detail;"),
        CBM_STORE_OK);

    char *response = cbm_mcp_handle_tool(
        srv, "check_index_coverage",
        "{\"project\":\"test-project\",\"paths\":[\"main.go\"],\"scopes\":[\".\"]}");
    ASSERT_NOT_NULL(response);
    char *inner = extract_text_content(response);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"coverage_lookup\":\"error\""));
    ASSERT_NOT_NULL(strstr(inner, "\"status\":\"coverage_unavailable\""));
    ASSERT_NULL(strstr(inner, "\"status\":\"no_recorded_issue\""));

    free(inner);
    free(response);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(tool_index_status_includes_git_metadata) {
    /* The git context block moved behind verbose:true (lean-default contract,
     * TOON round 2) — this test pins the verbose path's content; the default-
     * omission guard lives in tool_lean_defaults_schema_and_status. */
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"index_status\","
             "\"arguments\":{\"project\":\"test-project\",\"verbose\":true}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"root_path\""));
    ASSERT_NOT_NULL(strstr(inner, "\"git\""));
    ASSERT_NOT_NULL(strstr(inner, "\"is_git\":false"));
    ASSERT_NOT_NULL(strstr(inner, "\"root_exists\":true"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  TOOL HANDLERS WITH DATA
 * ══════════════════════════════════════════════════════════════════ */

TEST(tool_trace_call_path_not_found) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"trace_call_path\","
                                   "\"arguments\":{\"function_name\":\"NonExistent\","
                                   "\"project\":\"nonexistent\"}}}");
    ASSERT_NOT_NULL(resp);
    /* Should return error about project not found */
    ASSERT_NOT_NULL(strstr(resp, "not found"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Regression for #1425: a project name that fails validation must produce a
 * clean "not found" error and NOTHING else. project_db_path() yields "" for
 * such names; SQLite opens "" as an anonymous temp db, its integrity check
 * fails, and quarantine rendered "".corrupt.<hex> - a RELATIVE path dropped
 * into the daemon's cwd on every such query. */
TEST(tool_call_invalid_project_name_leaves_no_corrupt_litter_issue1425) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/mcp-litter-XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        FAIL("cbm_mkdtemp failed");
    char oldcwd[CBM_SZ_1K];
    if (!cbm_getcwd(oldcwd, sizeof(oldcwd)))
        FAIL("getcwd failed");
    if (cbm_chdir(tmpdir) != 0)
        FAIL("chdir failed");

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_graph\","
                                   "\"arguments\":{\"name_pattern\":\"x\","
                                   "\"project\":\"bad name\"}}}");
    bool clean_error = resp && strstr(resp, "not found") != NULL;
    free(resp);
    cbm_mcp_server_free(srv);

    int litter = 0;
    cbm_dir_t *dir = cbm_opendir(tmpdir);
    if (dir) {
        cbm_dirent_t *entry;
        while ((entry = cbm_readdir(dir)) != NULL) {
            if (strstr(entry->name, ".corrupt.")) {
                litter++;
            }
        }
        cbm_closedir(dir);
    }
    if (cbm_chdir(oldcwd) != 0)
        FAIL("chdir back failed");
    th_rmtree(tmpdir);
    if (!clean_error)
        FAIL("invalid project name must produce a clean not-found error");
    if (litter != 0)
        FAIL("invalid project name must not quarantine an anonymous temp db into cwd (#1425)");
    PASS();
}

TEST(tool_trace_missing_function_name) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"trace_call_path\","
                                   "\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "required"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* An unrecognised edge_types entry must be REJECTED, not traversed. The store
 * binds these strings into `WHERE e.type IN (...)`, so "CALLS,OVERRIDE" (the
 * natural thing to type) matched nothing and returned a clean-looking
 * `callers_total: 0` — indistinguishable from a genuine zero. */
TEST(tool_trace_rejects_unknown_edge_type) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "etproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/et");

    cbm_node_t callee = {.project = proj,
                         .label = "Function",
                         .name = "target",
                         .qualified_name = "etproj.a.target",
                         .file_path = "a.c",
                         .start_line = 10,
                         .end_line = 20};
    cbm_node_t caller = {.project = proj,
                         .label = "Function",
                         .name = "source",
                         .qualified_name = "etproj.a.source",
                         .file_path = "a.c",
                         .start_line = 30,
                         .end_line = 40};
    int64_t idt = cbm_store_upsert_node(st, &callee);
    int64_t ids = cbm_store_upsert_node(st, &caller);
    ASSERT_GT(idt, 0);
    ASSERT_GT(ids, 0);
    cbm_edge_t e = {.project = proj, .source_id = ids, .target_id = idt, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &e), 0);

    /* Comma-joined form: one string, not two types. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":65,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"target\",\"project\":\"etproj\","
             "\"direction\":\"inbound\",\"edge_types\":[\"CALLS,OVERRIDE\"]}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "invalid edge_types"));
    /* The silent zero is exactly what must NOT come back. */
    ASSERT_NULL(strstr(inner, "callers_total: 0"));
    free(inner);
    free(resp);

    /* Lowercase is equally unmatched and equally silent today. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":66,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"target\",\"project\":\"etproj\","
             "\"direction\":\"inbound\",\"edge_types\":[\"calls\"]}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "invalid edge_types"));
    free(inner);
    free(resp);

    const char *bad_args[] = {
        "{\"function_name\":\"target\",\"project\":\"etproj\",\"direction\":\"inbound\","
        "\"edge_types\":\"CALLS\"}",
        "{\"function_name\":\"target\",\"project\":\"etproj\",\"direction\":\"inbound\","
        "\"edge_types\":[\"CALLS\",7]}",
        "{\"function_name\":\"target\",\"project\":\"etproj\",\"direction\":\"inbound\","
        "\"edge_types\":[]}",
        "{\"function_name\":\"target\",\"project\":\"etproj\",\"direction\":\"inbound\","
        "\"edge_types\":[\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\","
        "\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\",\"CALLS\","
        "\"CALLS\",\"CALLS\",\"CALLS\"]}",
    };
    for (size_t i = 0; i < sizeof(bad_args) / sizeof(bad_args[0]); i++) {
        resp = cbm_mcp_handle_tool(srv, "trace_call_path", bad_args[i]);
        ASSERT_NOT_NULL(resp);
        inner = extract_text_content(resp);
        free(resp);
        ASSERT_NOT_NULL(inner);
        ASSERT_NOT_NULL(strstr(inner, "invalid edge_types"));
        ASSERT_NULL(strstr(inner, "callers_total"));
        free(inner);
    }

    resp = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"function_name\":\"target\",\"project\":\"etproj\",\"mode\":\"typo\"}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "invalid mode"));
    free(inner);

    resp = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"function_name\":\"target\",\"project\":\"etproj\",\"format\":\"yaml\"}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "invalid format"));
    free(inner);

    /* A well-formed type still traverses — the guard rejects, it does not
     * break the working path. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":67,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"target\",\"project\":\"etproj\","
             "\"direction\":\"inbound\",\"edge_types\":[\"CALLS\"]}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "callers_total: 1"));
    ASSERT_NOT_NULL(strstr(inner, "source"));
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Dynamic dispatch: an impl method reached only through a dyn/interface hop
 * used to report a bare `callers_total: 0` — shaped exactly like a genuine
 * zero, one inference away from "this adapter is dead code". OVERRIDE runs
 * impl -> trait, so no inbound edge_types set can reach the caller; the walk
 * needed is outbound-over-OVERRIDE then inbound-over-CALLS.
 *
 * Shape C (owner-approved): report the exact fact and the inferred fact in
 * SEPARATE labelled sections. The port-mediated callers are an over-set (they
 * may reach a sibling impl), so they must never be merged into `callers`,
 * which would assert something false about rows that reach the fake. */
TEST(tool_trace_reports_port_mediated_callers_separately) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "dynproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/dyn");

    /* The hexagonal shape: one port trait, two impls, one production caller
     * that only ever names the PORT. */
    cbm_node_t port = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "dynproj.ports.CommandExecutor.execute",
                       .file_path = "ports/command.rs",
                       .start_line = 80,
                       .end_line = 88};
    cbm_node_t real = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "dynproj.adapters.ProcessExecutor.execute",
                       .file_path = "adapters/process_executor.rs",
                       .start_line = 220,
                       .end_line = 298};
    cbm_node_t fake = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "dynproj.fakes.FakeCommandExecutor.execute",
                       .file_path = "fakes/command.rs",
                       .start_line = 30,
                       .end_line = 42};
    cbm_node_t caller = {.project = proj,
                         .label = "Function",
                         .name = "run_tool",
                         .qualified_name = "dynproj.core.run_tool",
                         .file_path = "core/tool.rs",
                         .start_line = 10,
                         .end_line = 40};
    int64_t idp = cbm_store_upsert_node(st, &port);
    int64_t idr = cbm_store_upsert_node(st, &real);
    int64_t idf = cbm_store_upsert_node(st, &fake);
    int64_t idc = cbm_store_upsert_node(st, &caller);
    ASSERT_GT(idp, 0);
    ASSERT_GT(idr, 0);
    ASSERT_GT(idf, 0);
    ASSERT_GT(idc, 0);

    /* OVERRIDE points impl -> trait (the direction that makes an inbound
     * trace from the impl unable to reach the caller at any depth). */
    cbm_edge_t ov_r = {.project = proj, .source_id = idr, .target_id = idp, .type = "OVERRIDE"};
    cbm_edge_t ov_f = {.project = proj, .source_id = idf, .target_id = idp, .type = "OVERRIDE"};
    cbm_edge_t call = {.project = proj, .source_id = idc, .target_id = idp, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &ov_r), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &ov_f), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &call), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":63,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"dynproj.adapters.ProcessExecutor.execute\","
             "\"project\":\"dynproj\",\"direction\":\"inbound\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* The exact fact is unchanged and stays exact: nothing CALLS the impl. */
    ASSERT_NOT_NULL(strstr(inner, "callers_total: 0"));
    /* The inferred fact must be present, attributed to the port it came from,
     * and counted — this is the whole of what a bare 0 was hiding. */
    ASSERT_NOT_NULL(strstr(inner, "via_port_total: 1"));
    ASSERT_NOT_NULL(strstr(inner, "dynproj.ports.CommandExecutor.execute"));
    ASSERT_NOT_NULL(strstr(inner, "run_tool"));
    /* Separation is the point of shape C: the inferred caller must NOT be
     * merged into the exact `callers` table. `callers` is emitted with its
     * count on the same line, so an empty table reads "callers: 0". */
    ASSERT_NOT_NULL(strstr(inner, "callers: 0"));

    free(inner);
    free(resp);

    /* `edge_types` is a real traversal boundary. An OVERRIDE-only request must
     * not append callers discovered through a separate internal CALLS walk. */
    resp = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"function_name\":\"dynproj.adapters.ProcessExecutor.execute\","
        "\"project\":\"dynproj\",\"direction\":\"inbound\",\"edge_types\":[\"OVERRIDE\"]}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NULL(strstr(inner, "via_port_total"));
    ASSERT_NULL(strstr(inner, "run_tool"));
    free(inner);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Port-mediated rows are part of trace_path's one response budget and cursor
 * stream. They must not bypass limit, repeat in full on every page, or promote
 * structural CALLS endpoints into the inferred-caller count. */
TEST(tool_trace_port_mediated_rows_are_budgeted_and_structural_rows_separated) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "dynpage";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/dynpage");

    cbm_node_t port = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "dynpage.ports.Executor.execute",
                       .file_path = "src/ports.rs",
                       .start_line = 1,
                       .end_line = 4};
    cbm_node_t impl = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "dynpage.adapters.RealExecutor.execute",
                       .file_path = "src/adapters.rs",
                       .start_line = 10,
                       .end_line = 20};
    int64_t port_id = cbm_store_upsert_node(st, &port);
    int64_t impl_id = cbm_store_upsert_node(st, &impl);
    ASSERT_GT(port_id, 0);
    ASSERT_GT(impl_id, 0);
    cbm_edge_t override = {
        .project = proj, .source_id = impl_id, .target_id = port_id, .type = "OVERRIDE"};
    ASSERT_GT(cbm_store_insert_edge(st, &override), 0);

    const char *labels[] = {"Function", "Module", "Function"};
    const char *names[] = {"dyn_caller_a", "dyn_unattributed", "dyn_caller_b"};
    for (int i = 0; i < 3; i++) {
        char qn[96];
        snprintf(qn, sizeof(qn), "dynpage.src.%s", names[i]);
        cbm_node_t caller = {.project = proj,
                             .label = labels[i],
                             .name = names[i],
                             .qualified_name = qn,
                             .file_path = "src/lib.rs",
                             .start_line = 30 + i,
                             .end_line = 31 + i};
        int64_t caller_id = cbm_store_upsert_node(st, &caller);
        ASSERT_GT(caller_id, 0);
        cbm_edge_t call = {
            .project = proj, .source_id = caller_id, .target_id = port_id, .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(st, &call), 0);
    }

    char pages[3][4096];
    char cursor[192] = "";
    int page_count = 0;
    for (; page_count < 3; page_count++) {
        char args[640];
        if (cursor[0]) {
            snprintf(args, sizeof(args),
                     "{\"project\":\"dynpage\","
                     "\"function_name\":\"dynpage.adapters.RealExecutor.execute\","
                     "\"direction\":\"inbound\",\"limit\":1,\"include_tests\":true,"
                     "\"cursor\":\"%s\"}",
                     cursor);
        } else {
            snprintf(args, sizeof(args),
                     "{\"project\":\"dynpage\","
                     "\"function_name\":\"dynpage.adapters.RealExecutor.execute\","
                     "\"direction\":\"inbound\",\"limit\":1,\"include_tests\":true}");
        }
        char *resp = cbm_mcp_handle_tool(srv, "trace_call_path", args);
        ASSERT_NOT_NULL(resp);
        char *inner = extract_text_content(resp);
        free(resp);
        ASSERT_NOT_NULL(inner);
        snprintf(pages[page_count], sizeof(pages[page_count]), "%s", inner);
        ASSERT_NOT_NULL(strstr(inner, "callers_total: 0"));
        ASSERT_NOT_NULL(strstr(inner, "via_port_total: 2"));
        ASSERT_NOT_NULL(strstr(inner, "via_port_unattributed_total: 1"));

        int page_rows = 0;
        for (int i = 0; i < 3; i++) {
            if (strstr(inner, names[i])) {
                page_rows++;
            }
        }
        ASSERT_EQ(page_rows, 1);

        const char *next = strstr(inner, "next: ");
        if (next) {
            const char *end = strchr(next + 6, '\n');
            size_t len = end ? (size_t)(end - (next + 6)) : strlen(next + 6);
            ASSERT_TRUE(len < sizeof(cursor));
            memcpy(cursor, next + 6, len);
            cursor[len] = '\0';
        } else {
            cursor[0] = '\0';
        }
        free(inner);
        if (!cursor[0]) {
            page_count++;
            break;
        }
    }
    ASSERT_EQ(page_count, 3);
    for (int i = 0; i < 3; i++) {
        int seen = 0;
        for (int page = 0; page < page_count; page++) {
            if (strstr(pages[page], names[i])) {
                seen++;
            }
        }
        ASSERT_EQ(seen, 1);
    }

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_trace_port_mediated_does_not_silently_drop_ninth_port) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "dynports";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/dynports");

    cbm_node_t impl = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "dynports.Real.execute",
                       .file_path = "src/lib.rs"};
    int64_t impl_id = cbm_store_upsert_node(st, &impl);
    ASSERT_GT(impl_id, 0);
    for (int i = 0; i < 9; i++) {
        char port_name[32];
        char port_qn[64];
        char caller_name[32];
        char caller_qn[64];
        snprintf(port_name, sizeof(port_name), "port_%d", i);
        snprintf(port_qn, sizeof(port_qn), "dynports.Port.%s", port_name);
        snprintf(caller_name, sizeof(caller_name), "caller_%d", i);
        snprintf(caller_qn, sizeof(caller_qn), "dynports.%s", caller_name);
        cbm_node_t port = {.project = proj,
                           .label = "Method",
                           .name = port_name,
                           .qualified_name = port_qn,
                           .file_path = "src/ports.rs"};
        cbm_node_t caller = {.project = proj,
                             .label = "Function",
                             .name = caller_name,
                             .qualified_name = caller_qn,
                             .file_path = "src/lib.rs"};
        int64_t port_id = cbm_store_upsert_node(st, &port);
        int64_t caller_id = cbm_store_upsert_node(st, &caller);
        ASSERT_GT(port_id, 0);
        ASSERT_GT(caller_id, 0);
        cbm_edge_t override = {
            .project = proj, .source_id = impl_id, .target_id = port_id, .type = "OVERRIDE"};
        cbm_edge_t call = {
            .project = proj, .source_id = caller_id, .target_id = port_id, .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(st, &override), 0);
        ASSERT_GT(cbm_store_insert_edge(st, &call), 0);
    }

    char *resp =
        cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"dynports\",\"function_name\":\"dynports.Real.execute\","
                            "\"direction\":\"inbound\",\"include_tests\":true}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "via_port_total: 9"));
    for (int i = 0; i < 9; i++) {
        char caller_name[32];
        snprintf(caller_name, sizeof(caller_name), "caller_%d", i);
        ASSERT_NOT_NULL(strstr(inner, caller_name));
    }
    free(inner);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_trace_port_mediated_enforces_aggregate_safety_ceiling) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "dyncap";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/dyncap");

    cbm_node_t impl = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "dyncap.Real.execute",
                       .file_path = "src/lib.rs"};
    int64_t impl_id = cbm_store_upsert_node(st, &impl);
    ASSERT_GT(impl_id, 0);
    for (int port_index = 0; port_index < 2; port_index++) {
        char port_name[32];
        char port_qn[64];
        snprintf(port_name, sizeof(port_name), "port_%d", port_index);
        snprintf(port_qn, sizeof(port_qn), "dyncap.Port.%s", port_name);
        cbm_node_t port = {.project = proj,
                           .label = "Method",
                           .name = port_name,
                           .qualified_name = port_qn,
                           .file_path = "src/ports.rs"};
        int64_t port_id = cbm_store_upsert_node(st, &port);
        ASSERT_GT(port_id, 0);
        cbm_edge_t override = {
            .project = proj, .source_id = impl_id, .target_id = port_id, .type = "OVERRIDE"};
        ASSERT_GT(cbm_store_insert_edge(st, &override), 0);
        for (int caller_index = 0; caller_index < 2501; caller_index++) {
            char caller_name[48];
            char caller_qn[96];
            snprintf(caller_name, sizeof(caller_name), "caller_%d_%d", port_index, caller_index);
            snprintf(caller_qn, sizeof(caller_qn), "dyncap.%s", caller_name);
            cbm_node_t caller = {.project = proj,
                                 .label = "Function",
                                 .name = caller_name,
                                 .qualified_name = caller_qn,
                                 .file_path = "src/lib.rs"};
            int64_t caller_id = cbm_store_upsert_node(st, &caller);
            ASSERT_GT(caller_id, 0);
            cbm_edge_t call = {
                .project = proj, .source_id = caller_id, .target_id = port_id, .type = "CALLS"};
            ASSERT_GT(cbm_store_insert_edge(st, &call), 0);
        }
    }

    char *resp =
        cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"dyncap\",\"function_name\":\"dyncap.Real.execute\","
                            "\"direction\":\"inbound\",\"include_tests\":true}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "trace_too_large"));
    ASSERT_NULL(strstr(inner, "via_port_total"));
    free(inner);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Sibling impls must not be reported as each other's port-mediated callers:
 * FakeCommandExecutor.execute is reachable from ProcessExecutor.execute via
 * the shared port, but it is an implementation, not a caller. */
TEST(tool_trace_port_mediated_excludes_sibling_impls) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "sibproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/sib");

    cbm_node_t port = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "sibproj.ports.CommandExecutor.execute",
                       .file_path = "ports/command.rs",
                       .start_line = 80,
                       .end_line = 88};
    cbm_node_t real = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "sibproj.adapters.ProcessExecutor.execute",
                       .file_path = "adapters/process_executor.rs",
                       .start_line = 220,
                       .end_line = 298};
    cbm_node_t sibling = {.project = proj,
                          .label = "Method",
                          .name = "execute",
                          .qualified_name = "sibproj.adapters.SshExecutor.execute",
                          .file_path = "adapters/ssh_executor.rs",
                          .start_line = 15,
                          .end_line = 60};
    cbm_node_t caller = {.project = proj,
                         .label = "Function",
                         .name = "run_tool",
                         .qualified_name = "sibproj.core.run_tool",
                         .file_path = "core/tool.rs",
                         .start_line = 10,
                         .end_line = 40};
    int64_t idp = cbm_store_upsert_node(st, &port);
    int64_t idr = cbm_store_upsert_node(st, &real);
    int64_t ids = cbm_store_upsert_node(st, &sibling);
    int64_t idc = cbm_store_upsert_node(st, &caller);
    ASSERT_GT(idp, 0);
    ASSERT_GT(idr, 0);
    ASSERT_GT(ids, 0);
    ASSERT_GT(idc, 0);

    cbm_edge_t ov_r = {.project = proj, .source_id = idr, .target_id = idp, .type = "OVERRIDE"};
    cbm_edge_t ov_s = {.project = proj, .source_id = ids, .target_id = idp, .type = "OVERRIDE"};
    cbm_edge_t call = {.project = proj, .source_id = idc, .target_id = idp, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &ov_r), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &ov_s), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &call), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":64,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"sibproj.adapters.ProcessExecutor.execute\","
             "\"project\":\"sibproj\",\"direction\":\"inbound\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* Exactly one port-mediated caller: run_tool. The sibling impl reached
     * through the same port is NOT a caller and must be excluded. */
    ASSERT_NOT_NULL(strstr(inner, "via_port_total: 1"));
    ASSERT_NOT_NULL(strstr(inner, "run_tool"));
    ASSERT_NULL(strstr(inner, "SshExecutor"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* A section heading is a factual claim about the rows under it. The legs
 * emitted "callers"/"callees" as hardcoded literals regardless of which edge
 * type was traversed, so an inbound OVERRIDE trace of a port trait method
 * listed its IMPLEMENTATIONS under a heading reading `callers` — a relationship
 * the traversal never established, and one an agent reads as fact.
 *
 * The heading may name the CALLS relationship only when CALLS is exactly what
 * was walked. For any other edge set the leg is named by the direction it
 * actually traversed (`inbound`/`outbound`) and the traversed types are echoed
 * in `edges`, so a cold reader can still tell what the rows are. */
TEST(tool_trace_sections_do_not_claim_untraversed_relationship) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "secproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/sec");

    cbm_node_t port = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "secproj.ports.CommandExecutor.execute",
                       .file_path = "ports/command.rs",
                       .start_line = 80,
                       .end_line = 88};
    cbm_node_t real = {.project = proj,
                       .label = "Method",
                       .name = "execute",
                       .qualified_name = "secproj.adapters.ProcessExecutor.execute",
                       .file_path = "adapters/process_executor.rs",
                       .start_line = 220,
                       .end_line = 298};
    cbm_node_t caller = {.project = proj,
                         .label = "Function",
                         .name = "run_tool",
                         .qualified_name = "secproj.core.run_tool",
                         .file_path = "core/tool.rs",
                         .start_line = 10,
                         .end_line = 40};
    int64_t idp = cbm_store_upsert_node(st, &port);
    int64_t idr = cbm_store_upsert_node(st, &real);
    int64_t idc = cbm_store_upsert_node(st, &caller);
    ASSERT_GT(idp, 0);
    ASSERT_GT(idr, 0);
    ASSERT_GT(idc, 0);

    cbm_edge_t ov = {.project = proj, .source_id = idr, .target_id = idp, .type = "OVERRIDE"};
    cbm_edge_t call = {.project = proj, .source_id = idc, .target_id = idp, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &ov), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &call), 0);

    /* Inbound over OVERRIDE from the PORT reaches the impl that overrides it.
     * That impl is not a caller of anything. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":81,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"secproj.ports.CommandExecutor.execute\","
             "\"project\":\"secproj\",\"direction\":\"inbound\",\"depth\":1,"
             "\"edge_types\":[\"OVERRIDE\"]}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* The row is really there — the assertions below are about its LABEL,
     * not about the traversal having found nothing. */
    ASSERT_NOT_NULL(strstr(inner, "ProcessExecutor"));
    /* The false claim, in either spelling, must be gone. */
    ASSERT_NULL(strstr(inner, "callers"));
    ASSERT_NULL(strstr(inner, "callees"));
    /* Replaced by what the traversal actually did. */
    ASSERT_NOT_NULL(strstr(inner, "inbound_total: 1"));
    ASSERT_NOT_NULL(strstr(inner, "inbound: 1"));
    ASSERT_NOT_NULL(strstr(inner, "edges: OVERRIDE"));
    free(inner);
    free(resp);

    /* Same query through the structured-JSON encoder: the object KEYS carry
     * the identical claim, so they must tell the identical truth. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":82,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"secproj.ports.CommandExecutor.execute\","
             "\"project\":\"secproj\",\"direction\":\"inbound\",\"depth\":1,"
             "\"edge_types\":[\"OVERRIDE\"],\"format\":\"json\"}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "ProcessExecutor"));
    ASSERT_NULL(strstr(inner, "\"callers\""));
    ASSERT_NULL(strstr(inner, "\"callers_total\""));
    ASSERT_NOT_NULL(strstr(inner, "\"inbound_total\""));
    /* Key form, not the bare word: "direction":"inbound" already contains
     * "inbound" as a VALUE, so asserting the bare quoted string would pass
     * without the leg key ever having been renamed. */
    ASSERT_NOT_NULL(strstr(inner, "\"inbound\":{"));
    ASSERT_NOT_NULL(strstr(inner, "\"edges\":"));
    free(inner);
    free(resp);

    /* Two distinct edge-type configs prove the name is derived, not hardcoded
     * to a second constant: a plain CALLS trace of the same node keeps saying
     * `callers`, because there the claim is true — and emits no `edges` echo,
     * since the heading already names the relationship. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":83,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"secproj.ports.CommandExecutor.execute\","
             "\"project\":\"secproj\",\"direction\":\"inbound\",\"depth\":1,"
             "\"edge_types\":[\"CALLS\"]}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "callers_total: 1"));
    ASSERT_NOT_NULL(strstr(inner, "run_tool"));
    ASSERT_NULL(strstr(inner, "inbound_total"));
    ASSERT_NULL(strstr(inner, "edges:"));
    free(inner);
    free(resp);

    /* A multi-type set is still named by direction, and `edges` lists every
     * type walked so the reader can see the rows are a union. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":84,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"secproj.ports.CommandExecutor.execute\","
             "\"project\":\"secproj\",\"direction\":\"inbound\",\"depth\":1,"
             "\"edge_types\":[\"CALLS\",\"OVERRIDE\"]}}}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "inbound_total: 2"));
    ASSERT_NOT_NULL(strstr(inner, "edges: CALLS,OVERRIDE"));
    ASSERT_NULL(strstr(inner, "callers"));
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Regression: two same-named definitions with equal rank must be reported
 * ambiguous, not silently traced (trace_path previously took nodes[0]). */
TEST(tool_trace_call_path_ambiguous) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "amb-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/amb");
    cbm_node_t a = {.project = proj,
                    .label = "Function",
                    .name = "amb",
                    .qualified_name = "amb-proj.a.amb",
                    .file_path = "a.c",
                    .start_line = 10,
                    .end_line = 20};
    cbm_node_t b = {.project = proj,
                    .label = "Function",
                    .name = "amb",
                    .qualified_name = "amb-proj.b.amb",
                    .file_path = "b.c",
                    .start_line = 10,
                    .end_line = 20}; /* equal span -> genuine tie */
    ASSERT_GT(cbm_store_upsert_node(st, &a), 0);
    ASSERT_GT(cbm_store_upsert_node(st, &b), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":61,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"amb\",\"project\":\"amb-proj\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "ambiguous"));
    ASSERT_NOT_NULL(strstr(inner, "suggestions"));
    ASSERT_NULL(strstr(inner, "\"callees\""));
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Multi-seed union hop semantics: bfs_union_same_name deduped visited nodes
 * keep-FIRST-seen, so a node reached at hop 2 from the first seed kept hop 2
 * even when the second seed reaches it at hop 1. hop feeds risk_labels and
 * (soon) pagination watermarks — it must be the MINIMUM across seeds, matching
 * the single-BFS MIN(hop) semantics (#797). */
TEST(tool_trace_union_records_min_hop_across_seeds) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "dualproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/dual");

    /* One real definition + one body-less stub (start==end) — the #546/#650
     * shape pick_resolved_node resolves WITHOUT ambiguity while
     * bfs_union_same_name still traverses both. Seed A (real def, lower id,
     * traversed first) reaches tgt only via mid (hop 2); the stub seed B
     * reaches tgt directly (hop 1). */
    cbm_node_t sa = {.project = proj,
                     .label = "Function",
                     .name = "dual",
                     .qualified_name = "dualproj.a.dual",
                     .file_path = "a.c",
                     .start_line = 1,
                     .end_line = 50};
    cbm_node_t sb = {.project = proj,
                     .label = "Function",
                     .name = "dual",
                     .qualified_name = "dualproj.b.dual",
                     .file_path = "b.d.ts",
                     .start_line = 1,
                     .end_line = 1};
    cbm_node_t mid = {.project = proj,
                      .label = "Function",
                      .name = "mid",
                      .qualified_name = "dualproj.c.mid",
                      .file_path = "c.c",
                      .start_line = 1,
                      .end_line = 5};
    cbm_node_t tgt = {.project = proj,
                      .label = "Function",
                      .name = "tgt",
                      .qualified_name = "dualproj.c.tgt",
                      .file_path = "c.c",
                      .start_line = 10,
                      .end_line = 15};
    int64_t ida = cbm_store_upsert_node(st, &sa);
    int64_t idb = cbm_store_upsert_node(st, &sb);
    int64_t idm = cbm_store_upsert_node(st, &mid);
    int64_t idt = cbm_store_upsert_node(st, &tgt);
    ASSERT_GT(ida, 0);
    ASSERT_GT(idb, 0);
    ASSERT_GT(idm, 0);
    ASSERT_GT(idt, 0);
    cbm_edge_t e1 = {.project = proj, .source_id = ida, .target_id = idm, .type = "CALLS"};
    cbm_edge_t e2 = {.project = proj, .source_id = idm, .target_id = idt, .type = "CALLS"};
    cbm_edge_t e3 = {.project = proj, .source_id = idb, .target_id = idt, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &e1), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &e2), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &e3), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":62,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\","
             "\"arguments\":{\"function_name\":\"dual\",\"project\":\"dualproj\","
             "\"direction\":\"outbound\",\"depth\":3}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    /* tgt is one hop from seed B — the union must record hop 1, not seed A's 2. */
    ASSERT_NOT_NULL(strstr(inner, "  tgt 1"));
    ASSERT_NULL(strstr(inner, "  tgt 2"));
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Exactly-once trace pagination: 12 callees paged at limit=5 must yield
 * 5+5+2 rows with every callee appearing on exactly one page, exact totals
 * on every page, and a final page without a cursor. Stale and mismatched
 * cursors must fail with teaching errors, never silently restart. */
TEST(tool_trace_pagination_exactly_once) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "pageproj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/page");

    cbm_node_t hub = {.project = proj,
                      .label = "Function",
                      .name = "hub",
                      .qualified_name = "pageproj.h.hub",
                      .file_path = "h.c",
                      .start_line = 1,
                      .end_line = 9};
    int64_t hid = cbm_store_upsert_node(st, &hub);
    ASSERT_GT(hid, 0);
    enum { CALLEES = 12 };
    for (int i = 0; i < CALLEES; i++) {
        char nm[16];
        char qn[48];
        snprintf(nm, sizeof(nm), "c%02d", i);
        snprintf(qn, sizeof(qn), "pageproj.m.c%02d", i);
        cbm_node_t n = {.project = proj,
                        .label = "Function",
                        .name = nm,
                        .qualified_name = qn,
                        .file_path = "m.c",
                        .start_line = 1,
                        .end_line = 3};
        int64_t nid = cbm_store_upsert_node(st, &n);
        ASSERT_GT(nid, 0);
        cbm_edge_t e = {.project = proj, .source_id = hid, .target_id = nid, .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(st, &e), 0);
    }

    char pages[3][4096];
    char tok[192] = "";
    int npages = 0;
    for (; npages < 3; npages++) {
        char req[640];
        if (tok[0]) {
            snprintf(req, sizeof(req),
                     "{\"jsonrpc\":\"2.0\",\"id\":80,\"method\":\"tools/call\",\"params\":{"
                     "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"pageproj\","
                     "\"function_name\":\"hub\",\"direction\":\"outbound\",\"limit\":5,"
                     "\"cursor\":\"%s\"}}}",
                     tok);
        } else {
            snprintf(req, sizeof(req),
                     "{\"jsonrpc\":\"2.0\",\"id\":80,\"method\":\"tools/call\",\"params\":{"
                     "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"pageproj\","
                     "\"function_name\":\"hub\",\"direction\":\"outbound\",\"limit\":5}}}");
        }
        char *resp = cbm_mcp_server_handle(srv, req);
        ASSERT_NOT_NULL(resp);
        char *inner = extract_text_content(resp);
        free(resp);
        ASSERT_NOT_NULL(inner);
        snprintf(pages[npages], sizeof(pages[npages]), "%s", inner);
        ASSERT_NOT_NULL(strstr(inner, "callees_total: 12")); /* exact total, every page */
        const char *nx = strstr(inner, "next: ");
        if (nx) {
            const char *e = strchr(nx + 6, '\n');
            size_t tl = e ? (size_t)(e - (nx + 6)) : strlen(nx + 6);
            ASSERT_TRUE(tl < sizeof(tok));
            memcpy(tok, nx + 6, tl);
            tok[tl] = '\0';
        } else {
            tok[0] = '\0';
        }
        free(inner);
        if (!tok[0]) {
            npages++;
            break;
        }
    }
    ASSERT_EQ(npages, 3); /* 5 + 5 + 2 */
    /* Exactly-once: every callee appears on exactly ONE page. */
    for (int i = 0; i < CALLEES; i++) {
        char qn[48];
        snprintf(qn, sizeof(qn), "  c%02d 1\n", i);
        int seen = 0;
        for (int p = 0; p < 3; p++) {
            if (strstr(pages[p], qn)) {
                seen++;
            }
        }
        ASSERT_EQ(seen, 1);
    }
    /* Final page carries no cursor. */
    ASSERT_NULL(strstr(pages[2], "next: "));

    /* Params mismatch: replay a page-2-era cursor with a different depth. */
    const char *nx1 = strstr(pages[0], "next: ");
    ASSERT_NOT_NULL(nx1);
    char tok1[192];
    const char *e1 = strchr(nx1 + 6, '\n');
    size_t tl1 = e1 ? (size_t)(e1 - (nx1 + 6)) : strlen(nx1 + 6);
    memcpy(tok1, nx1 + 6, tl1);
    tok1[tl1] = '\0';
    char req2[640];
    snprintf(req2, sizeof(req2),
             "{\"jsonrpc\":\"2.0\",\"id\":81,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"pageproj\","
             "\"function_name\":\"hub\",\"direction\":\"outbound\",\"limit\":5,\"depth\":2,"
             "\"cursor\":\"%s\"}}}",
             tok1);
    char *resp = cbm_mcp_server_handle(srv, req2);
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "cursor_params_mismatch"));
    free(inner);

    /* Edge types are part of traversal identity too. Replaying a CALLS-only
     * cursor against a wider edge set must fail before its watermark is
     * applied to a different row array. */
    snprintf(req2, sizeof(req2),
             "{\"jsonrpc\":\"2.0\",\"id\":83,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"pageproj\","
             "\"function_name\":\"hub\",\"direction\":\"outbound\",\"limit\":5,"
             "\"edge_types\":[\"CALLS\",\"OVERRIDE\"],\"cursor\":\"%s\"}}}",
             tok1);
    resp = cbm_mcp_server_handle(srv, req2);
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "cursor_params_mismatch"));
    free(inner);

    /* Structured JSON uses the documented canonical `next` field too. */
    resp = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"project\":\"pageproj\",\"function_name\":\"hub\",\"direction\":\"outbound\","
        "\"limit\":5,\"format\":\"json\"}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    yyjson_doc *json_page = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(json_page);
    yyjson_val *json_root = yyjson_doc_get_root(json_page);
    yyjson_val *json_next = yyjson_obj_get(json_root, "next");
    ASSERT_TRUE(json_next && yyjson_is_str(json_next));
    snprintf(tok1, sizeof(tok1), "%s", yyjson_get_str(json_next));
    ASSERT_NULL(yyjson_obj_get(json_root, "next_cursor"));
    yyjson_doc_free(json_page);
    free(inner);
    snprintf(req2, sizeof(req2),
             "{\"project\":\"pageproj\",\"function_name\":\"hub\",\"direction\":\"outbound\","
             "\"limit\":5,\"format\":\"json\",\"cursor\":\"%s\"}",
             tok1);
    resp = cbm_mcp_handle_tool(srv, "trace_call_path", req2);
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "c05"));
    free(inner);

    /* An over-cap depth is rejected instead of producing a cursor for a
     * silently different traversal. */
    resp = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"project\":\"pageproj\",\"function_name\":\"hub\",\"direction\":\"outbound\","
        "\"limit\":5,\"depth\":1000}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "depth must be at most"));
    ASSERT_NULL(strstr(inner, "next: "));
    free(inner);

    /* Omitted direction canonically means "both". Cursor minting and replay
     * must hash that same default rather than minting an unusable token from
     * the raw NULL argument. */
    resp = cbm_mcp_handle_tool(srv, "trace_call_path",
                               "{\"project\":\"pageproj\",\"function_name\":\"hub\",\"limit\":5}");
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    const char *default_next = strstr(inner, "next: ");
    ASSERT_NOT_NULL(default_next);
    const char *default_end = strchr(default_next + 6, '\n');
    size_t default_len =
        default_end ? (size_t)(default_end - (default_next + 6)) : strlen(default_next + 6);
    ASSERT_TRUE(default_len < sizeof(tok1));
    memcpy(tok1, default_next + 6, default_len);
    tok1[default_len] = '\0';
    free(inner);
    snprintf(req2, sizeof(req2),
             "{\"project\":\"pageproj\",\"function_name\":\"hub\",\"limit\":5,"
             "\"cursor\":\"%s\"}",
             tok1);
    resp = cbm_mcp_handle_tool(srv, "trace_call_path", req2);
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NULL(strstr(inner, "cursor_params_mismatch"));
    ASSERT_NOT_NULL(strstr(inner, "  c05 1\n"));
    free(inner);

    /* Stale: an index run (upsert_project bumps the generation) invalidates
     * outstanding cursors with a loud, actionable error. */
    cbm_store_upsert_project(st, proj, "/tmp/page");
    snprintf(req2, sizeof(req2),
             "{\"jsonrpc\":\"2.0\",\"id\":82,\"method\":\"tools/call\",\"params\":{"
             "\"name\":\"trace_call_path\",\"arguments\":{\"project\":\"pageproj\","
             "\"function_name\":\"hub\",\"direction\":\"outbound\",\"limit\":5,"
             "\"cursor\":\"%s\"}}}",
             tok1);
    resp = cbm_mcp_server_handle(srv, req2);
    ASSERT_NOT_NULL(resp);
    inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "stale_cursor"));
    free(inner);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Pre-generation stores cannot prove cursor freshness. A wide trace on one
 * must therefore return no relationship rows at all: either the caller raises
 * the limit/narrows the traversal, or re-indexing enables lossless cursors. */
TEST(tool_trace_legacy_wide_stream_requires_explicit_refinement) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    const char *project = "legacy-trace-refinement";
    ASSERT_EQ(cbm_store_upsert_project(st, project, "/tmp/legacy-trace-refinement"), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);
    cbm_node_t hub = {.project = project,
                      .label = "Function",
                      .name = "hub",
                      .qualified_name = "legacy-trace-refinement.hub",
                      .file_path = "hub.c",
                      .start_line = 1,
                      .end_line = 2};
    int64_t hub_id = cbm_store_upsert_node(st, &hub);
    ASSERT_GT(hub_id, 0);
    enum { CALLERS = 203 };
    for (int i = 0; i < CALLERS; i++) {
        char name[32];
        char qn[96];
        snprintf(name, sizeof(name), "caller_%03d", i);
        snprintf(qn, sizeof(qn), "legacy-trace-refinement.callers.%s", name);
        cbm_node_t caller = {.project = project,
                             .label = "Function",
                             .name = name,
                             .qualified_name = qn,
                             .file_path = "callers.c",
                             .start_line = i + 1,
                             .end_line = i + 1};
        int64_t caller_id = cbm_store_upsert_node(st, &caller);
        ASSERT_GT(caller_id, 0);
        cbm_edge_t edge = {
            .project = project, .source_id = caller_id, .target_id = hub_id, .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(st, &edge), 0);
    }
    ASSERT_EQ(cbm_store_exec(st, "DROP TABLE store_meta;"), CBM_STORE_OK);

    char *response = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"project\":\"legacy-trace-refinement\",\"function_name\":\"hub\","
        "\"direction\":\"inbound\",\"depth\":2,\"limit\":100,\"format\":\"json\"}");
    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(strlen(response) <= CBM_MCP_RESULT_MAX_BYTES);
    ASSERT_NOT_NULL(strstr(response, "trace_refinement_required"));
    ASSERT_NOT_NULL(strstr(response, "203 visible relationship rows exceed limit=100"));
    ASSERT_NOT_NULL(strstr(response, "No partial rows were returned"));
    ASSERT_NOT_NULL(strstr(response, "re-index the project"));
    ASSERT_NOT_NULL(strstr(response, "\"isError\":true"));
    ASSERT_NULL(strstr(response, "caller_000"));
    free(response);

    response = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"project\":\"legacy-trace-refinement\",\"function_name\":\"hub\","
        "\"direction\":\"inbound\",\"depth\":2,\"limit\":203,\"format\":\"json\"}");
    ASSERT_NOT_NULL(response);
    char *inner = extract_text_content(response);
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(root, "callers_total")), CALLERS);
    ASSERT_NULL(yyjson_obj_get(root, "truncated"));
    ASSERT_NULL(yyjson_obj_get(root, "next"));
    ASSERT_NOT_NULL(strstr(inner, "caller_000"));
    ASSERT_NOT_NULL(strstr(inner, "caller_202"));
    yyjson_doc_free(doc);
    free(inner);
    free(response);
    cbm_mcp_server_free(srv);
    PASS();
}

/* The 5000-row safety ceiling must fail loudly instead of returning a
 * clean-looking partial total that clients mistake for the reachable set. */
TEST(tool_trace_rejects_reachable_set_beyond_safety_ceiling) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "trace-cap-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/trace-cap");

    cbm_node_t hub = {.project = proj,
                      .label = "Function",
                      .name = "cap_hub",
                      .qualified_name = "trace-cap-proj.cap_hub",
                      .file_path = "src/lib.rs",
                      .start_line = 1,
                      .end_line = 3};
    int64_t hub_id = cbm_store_upsert_node(st, &hub);
    ASSERT_GT(hub_id, 0);
    for (int i = 0; i < 5001; i++) {
        char name[32];
        char qn[64];
        snprintf(name, sizeof(name), "cap_%04d", i);
        snprintf(qn, sizeof(qn), "trace-cap-proj.%s", name);
        cbm_node_t callee = {.project = proj,
                             .label = "Function",
                             .name = name,
                             .qualified_name = qn,
                             .file_path = "src/lib.rs",
                             .start_line = i + 10,
                             .end_line = i + 11};
        int64_t callee_id = cbm_store_upsert_node(st, &callee);
        ASSERT_GT(callee_id, 0);
        cbm_edge_t edge = {
            .project = proj, .source_id = hub_id, .target_id = callee_id, .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(st, &edge), 0);
    }

    char *resp =
        cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"trace-cap-proj\",\"function_name\":\"cap_hub\","
                            "\"direction\":\"outbound\",\"depth\":1,\"limit\":5000}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "trace_too_large"));
    ASSERT_NULL(strstr(inner, "callees_total"));
    free(inner);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_trace_data_flow_uses_shortest_path_predecessor_edge_args) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "flow-edge-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/flow-edge");

    cbm_node_t a = {.project = proj,
                    .label = "Function",
                    .name = "flow_a",
                    .qualified_name = "flow-edge-proj.flow_a",
                    .file_path = "src/lib.rs"};
    cbm_node_t b = {.project = proj,
                    .label = "Function",
                    .name = "flow_b",
                    .qualified_name = "flow-edge-proj.flow_b",
                    .file_path = "src/lib.rs"};
    cbm_node_t c = {.project = proj,
                    .label = "Function",
                    .name = "flow_c",
                    .qualified_name = "flow-edge-proj.flow_c",
                    .file_path = "src/lib.rs"};
    int64_t a_id = cbm_store_upsert_node(st, &a);
    int64_t b_id = cbm_store_upsert_node(st, &b);
    int64_t c_id = cbm_store_upsert_node(st, &c);
    ASSERT_GT(a_id, 0);
    ASSERT_GT(b_id, 0);
    ASSERT_GT(c_id, 0);

    /* Insert the later-hop edge first. The old renderer picked the first edge
     * incident to flow_b, so insertion order attached beta to both rows. */
    cbm_edge_t bc = {.project = proj,
                     .source_id = b_id,
                     .target_id = c_id,
                     .type = "CALLS",
                     .properties_json = "{\"args\":[{\"e\":\"beta\"}]}"};
    cbm_edge_t ab = {.project = proj,
                     .source_id = a_id,
                     .target_id = b_id,
                     .type = "CALLS",
                     .properties_json = "{\"args\":[{\"e\":\"left]right\\\"tail\"}]}"};
    ASSERT_GT(cbm_store_insert_edge(st, &bc), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &ab), 0);

    char *resp = cbm_mcp_handle_tool(
        srv, "trace_call_path",
        "{\"project\":\"flow-edge-proj\",\"function_name\":\"flow_a\","
        "\"direction\":\"outbound\",\"depth\":2,\"mode\":\"data_flow\",\"format\":\"json\"}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "left]right"));
    ASSERT_NOT_NULL(strstr(inner, "tail"));
    ASSERT_NOT_NULL(strstr(inner, "beta"));
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_doc_free(doc);
    free(inner);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_trace_rejects_unimplemented_parameter_filter) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *tools = cbm_mcp_handle_tool(srv, "trace_call_path",
                                      "{\"project\":\"missing\",\"function_name\":\"f\","
                                      "\"mode\":\"data_flow\",\"parameter_name\":\"input\"}");
    ASSERT_NOT_NULL(tools);
    char *inner = extract_text_content(tools);
    free(tools);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "parameter_name is unsupported"));
    ASSERT_NOT_NULL(strstr(inner, "cannot be answered truthfully"));
    free(inner);

    /* The live tool contract must not keep advertising a non-functional
     * property after the handler starts rejecting it. */
    char *schema = cbm_mcp_tools_list();
    ASSERT_NOT_NULL(schema);
    ASSERT_NULL(strstr(schema, "parameter_name"));
    free(schema);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Regression: when same-named nodes differ in rank, trace must pick the real
 * definition (callable, larger body) — NOT nodes[0]. The Module is inserted
 * first; if trace took nodes[0] the outbound trace would be empty. */
TEST(tool_trace_call_path_prefers_definition) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "pref-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/pref");
    /* nodes[0]: the WRONG match (a Module, tiny span), inserted first. */
    cbm_node_t wrong = {.project = proj,
                        .label = "Module",
                        .name = "dup",
                        .qualified_name = "pref-proj.dup",
                        .file_path = "dup.x",
                        .start_line = 1,
                        .end_line = 1};
    /* the real definition: a Function with a body. */
    cbm_node_t def = {.project = proj,
                      .label = "Function",
                      .name = "dup",
                      .qualified_name = "pref-proj.src.dup",
                      .file_path = "src/dup.c",
                      .start_line = 10,
                      .end_line = 50};
    cbm_node_t callee = {.project = proj,
                         .label = "Function",
                         .name = "callee",
                         .qualified_name = "pref-proj.src.callee",
                         .file_path = "src/dup.c",
                         .start_line = 60,
                         .end_line = 70};
    ASSERT_GT(cbm_store_upsert_node(st, &wrong), 0);
    int64_t id_def = cbm_store_upsert_node(st, &def);
    int64_t id_callee = cbm_store_upsert_node(st, &callee);
    ASSERT_GT(id_def, 0);
    ASSERT_GT(id_callee, 0);
    cbm_edge_t e = {.project = proj, .source_id = id_def, .target_id = id_callee, .type = "CALLS"};
    cbm_store_insert_edge(st, &e);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":62,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\",\"arguments\":{\"function_name\":\"dup\","
             "\"project\":\"pref-proj\",\"direction\":\"outbound\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NULL(strstr(inner, "ambiguous"));
    /* picked the Function definition -> its outbound CALLS edge to "callee" shows */
    ASSERT_NOT_NULL(strstr(inner, "callee"));
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* CONTRACT PIN for the closed strategy vocabulary published by
 * trace_path(include_evidence:true).
 *
 * The indexer records ~20 internal strategy names on CALLS edges and the set
 * grows with every language added. We publish a CLASS, not the raw name, so a
 * resolver rename cannot silently change a user-visible field. This test is
 * what keeps that promise honest: every strategy production can emit must land
 * in a known class. Adding lsp_foo_dispatch passes automatically; introducing a
 * genuinely new KIND of resolution fails HERE and forces a deliberate decision
 * about the public contract instead of leaking an internal name. */
TEST(trace_evidence_strategy_class_vocabulary_is_closed) {
    /* Every strategy string assigned anywhere in src/ + internal/ as of this
     * commit, plus the two literals pass_calls.c writes directly. */
    static const char *const lsp[] = {
        "lsp_direct",         "lsp_base_dispatch",      "lsp_embed_dispatch",
        "lsp_implicit_this",  "lsp_inherited_dispatch", "lsp_method_dispatch",
        "lsp_proc_macro",     "lsp_smart_ptr_dispatch", "lsp_strategy_cross_file",
        "lsp_trait_dispatch", "lsp_type_dispatch",      "lsp_virtual_dispatch"};
    for (size_t i = 0; i < sizeof(lsp) / sizeof(lsp[0]); i++) {
        const char *cls = cbm_mcp_edge_strategy_class(lsp[i]);
        ASSERT_NOT_NULL(cls);
        ASSERT_STR_EQ(cls, "lsp");
    }
    static const char *const lang[] = {"php_self_static", "php_static_resolved",
                                       "perl_method_static", "perl_method_typed"};
    for (size_t i = 0; i < sizeof(lang) / sizeof(lang[0]); i++) {
        const char *cls = cbm_mcp_edge_strategy_class(lang[i]);
        ASSERT_NOT_NULL(cls);
        ASSERT_STR_EQ(cls, "language_rule");
    }
    static const char *const heur[] = {"callee_suffix", "field_type_hint", "service_pattern",
                                       "fastapi_depends"};
    for (size_t i = 0; i < sizeof(heur) / sizeof(heur[0]); i++) {
        const char *cls = cbm_mcp_edge_strategy_class(heur[i]);
        ASSERT_NOT_NULL(cls);
        ASSERT_STR_EQ(cls, "heuristic");
    }
    /* A failed LSP resolution is reported as unresolved, not as "lsp" — the
     * caller's question is whether the edge is trustworthy, and "we tried LSP
     * and it did not resolve" answers no. */
    ASSERT_STR_EQ(cbm_mcp_edge_strategy_class("lsp_unresolved"), "unresolved");
    ASSERT_STR_EQ(cbm_mcp_edge_strategy_class("unknown"), "unresolved");
    /* Only a NULL/empty strategy is unclassified — an unmapped non-empty value
     * must never silently disappear from the output. */
    ASSERT_NULL(cbm_mcp_edge_strategy_class(NULL));
    ASSERT_NULL(cbm_mcp_edge_strategy_class(""));
    ASSERT_STR_EQ(cbm_mcp_edge_strategy_class("some_future_resolver"), "heuristic");
    PASS();
}

/* Distilled from #559 (@vvenegasv). The indexer already records
 * {strategy, confidence} on every CALLS edge (pass_calls.c:355) and the store
 * reads it back, but no tool ever surfaced it — an agent could see THAT A->B
 * exists, never HOW it was resolved.
 *
 * Binds two things at once: the evidence columns appear only when asked for
 * (default stays lean), and the published value is the CLASS, not the raw
 * internal strategy name. Fails without the production change in both
 * directions — no columns at all before, and "lsp_trait_dispatch" would leak
 * verbatim if the classifier were bypassed. */
TEST(tool_trace_path_evidence_is_opt_in_and_class_mapped) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "ev-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/ev");
    cbm_node_t caller = {.project = proj,
                         .label = "Function",
                         .name = "caller",
                         .qualified_name = "ev-proj.src.caller",
                         .file_path = "src/a.c",
                         .start_line = 1,
                         .end_line = 5};
    cbm_node_t callee = {.project = proj,
                         .label = "Function",
                         .name = "target",
                         .qualified_name = "ev-proj.src.target",
                         .file_path = "src/a.c",
                         .start_line = 10,
                         .end_line = 20};
    int64_t id_caller = cbm_store_upsert_node(st, &caller);
    int64_t id_callee = cbm_store_upsert_node(st, &callee);
    ASSERT_GT(id_caller, 0);
    ASSERT_GT(id_callee, 0);
    /* Exactly the shape pass_calls.c:355 writes in production. */
    cbm_edge_t e = {.project = proj,
                    .source_id = id_caller,
                    .target_id = id_callee,
                    .type = "CALLS",
                    .properties_json = "{\"callee\":\"target\",\"confidence\":0.95,"
                                       "\"strategy\":\"lsp_trait_dispatch\",\"candidates\":1,"
                                       "\"args\":[{\"e\":\"payload\"}]}"};
    ASSERT_GT(cbm_store_insert_edge(st, &e), 0);

    /* Default: lean. No evidence columns, no strategy anywhere. */
    char *plain = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":91,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_path\",\"arguments\":{\"function_name\":\"caller\","
             "\"project\":\"ev-proj\",\"direction\":\"outbound\"}}}");
    ASSERT_NOT_NULL(plain);
    char *plain_txt = extract_text_content(plain);
    ASSERT_NOT_NULL(plain_txt);
    ASSERT_NOT_NULL(strstr(plain_txt, "target")); /* positive control: the hop IS there */
    ASSERT_NULL(strstr(plain_txt, "lsp"));
    ASSERT_NULL(strstr(plain_txt, "0.95"));
    free(plain_txt);
    free(plain);

    /* Opted in: the class and the confidence appear, the raw name does not. */
    char *ev = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":92,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_path\",\"arguments\":{\"function_name\":\"caller\","
             "\"project\":\"ev-proj\",\"direction\":\"outbound\",\"include_evidence\":true}}}");
    ASSERT_NOT_NULL(ev);
    char *ev_txt = extract_text_content(ev);
    ASSERT_NOT_NULL(ev_txt);
    ASSERT_NOT_NULL(strstr(ev_txt, "target"));
    ASSERT_NOT_NULL(strstr(ev_txt, "lsp"));
    ASSERT_NOT_NULL(strstr(ev_txt, "0.95"));
    /* The internal resolver name must NOT reach the client. */
    ASSERT_NULL(strstr(ev_txt, "lsp_trait_dispatch"));
    free(ev_txt);
    free(ev);

    /* #1542: the same request with format:"json" returned cols ["name","hop"]
     * — include_evidence was implemented on the tree path only, so the callers
     * most likely to ask for structured output were the ones who silently got
     * nothing. The two formats must promise the same fields. */
    char *ev_json = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":93,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_path\",\"arguments\":{\"function_name\":\"caller\","
             "\"project\":\"ev-proj\",\"direction\":\"outbound\",\"include_evidence\":true,"
             "\"mode\":\"data_flow\",\"format\":\"json\"}}}");
    ASSERT_NOT_NULL(ev_json);
    char *ev_json_txt = extract_text_content(ev_json);
    ASSERT_NOT_NULL(ev_json_txt);
    ASSERT_NOT_NULL(strstr(ev_json_txt, "\"strategy\""));
    ASSERT_NOT_NULL(strstr(ev_json_txt, "\"confidence\""));
    ASSERT_NOT_NULL(strstr(ev_json_txt, "lsp"));
    ASSERT_NOT_NULL(strstr(ev_json_txt, "0.95"));
    ASSERT_NOT_NULL(strstr(ev_json_txt, "\"args\""));
    /* Column declarations are a positional contract: evidence precedes args,
     * so the target row must place the class/confidence before the edge args. */
    ASSERT_NOT_NULL(strstr(ev_json_txt, "[\"target\",1,\"lsp\",0.95,[{\"e\":\"payload\"}]]"));
    ASSERT_NULL(strstr(ev_json_txt, "lsp_trait_dispatch"));
    free(ev_json_txt);
    free(ev_json);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Reproduce-first (#887): the client-supplied `depth` on trace_call_path must be
 * clamped to the MCP ceiling (cbm_mcp_max_depth(), default 15). On origin/main
 * an MCP_MAX_DEPTH=15 constant was defined but never applied — `depth` flowed
 * straight into bfs_union_same_name, so an unbounded value drives the shared
 * cbm_store_bfs to arbitrary depth. Over an 18-node call chain, depth=1000
 * reaches n16/n17 (RED); with the clamp the walk stops at hop 15, so n15 is
 * reached but n16 is not (GREEN). Quoted tokens ("n15"/"n16") match only the
 * node-name field, never the qualified_name (preceded by '.'), so the boundary
 * check is exact. */
TEST(tool_trace_call_path_depth_above_ceiling_is_rejected) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "depth-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/depth");

    /* Linear call chain n00 -CALLS-> n01 -> ... -> n17 (18 nodes). */
    int64_t ids[18];
    for (int i = 0; i < 18; i++) {
        char name[8];
        char qn[32];
        snprintf(name, sizeof(name), "n%02d", i);
        snprintf(qn, sizeof(qn), "depth-proj.n%02d", i);
        cbm_node_t n = {.project = proj,
                        .label = "Function",
                        .name = name,
                        .qualified_name = qn,
                        .file_path = "chain.c",
                        .start_line = 1,
                        .end_line = 2};
        ids[i] = cbm_store_upsert_node(st, &n);
    }
    for (int i = 0; i < 17; i++) {
        cbm_edge_t e = {
            .project = proj, .source_id = ids[i], .target_id = ids[i + 1], .type = "CALLS"};
        cbm_store_insert_edge(st, &e);
    }

    char *at_ceiling = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":71,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\",\"arguments\":{\"function_name\":\"n00\","
             "\"project\":\"depth-proj\",\"direction\":\"outbound\",\"depth\":15}}}");
    ASSERT_NOT_NULL(at_ceiling);
    char *ceiling_text = extract_text_content(at_ceiling);
    ASSERT_NOT_NULL(ceiling_text);
    ASSERT_NOT_NULL(strstr(ceiling_text, "n15"));
    free(ceiling_text);
    free(at_ceiling);

    char *above_ceiling = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":72,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\",\"arguments\":{\"function_name\":\"n00\","
             "\"project\":\"depth-proj\",\"direction\":\"outbound\",\"depth\":16}}}");
    ASSERT_NOT_NULL(above_ceiling);
    char *above_text = extract_text_content(above_ceiling);
    ASSERT_NOT_NULL(above_text);
    ASSERT_NOT_NULL(strstr(above_text, "depth must be at most 15"));
    ASSERT_NULL(strstr(above_text, "n15"));
    free(above_text);
    free(above_ceiling);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Reproduce-first (#650, distilled): two GENUINELY-DIFFERENT same-named functions
 * whose bodies differ in length score differently, so the old exact-tie check did
 * not flag them ambiguous — and bfs_union_same_name (#546) then merged the caller
 * sets of both into one confidently-conflated answer (the mirror of #546's under-
 * report). The fix: 2+ real callable defs => ambiguous (disambiguate), never union
 * distinct symbols. RED before the pick_resolved_node real_def_count rule (response
 * merged callerA+callerB), GREEN after (response is ambiguous, no "callers"). */
TEST(tool_trace_call_path_distinct_defs_not_over_unioned) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "ou-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/ou");
    /* two unrelated real definitions of "dupreal", DIFFERENT body spans */
    cbm_node_t da = {.project = proj,
                     .label = "Function",
                     .name = "dupreal",
                     .qualified_name = "ou-proj.a.dupreal",
                     .file_path = "a.c",
                     .start_line = 10,
                     .end_line = 20}; /* span 10 */
    cbm_node_t db = {.project = proj,
                     .label = "Function",
                     .name = "dupreal",
                     .qualified_name = "ou-proj.b.dupreal",
                     .file_path = "b.c",
                     .start_line = 10,
                     .end_line = 40}; /* span 30 (no tie) */
    cbm_node_t ca = {.project = proj,
                     .label = "Function",
                     .name = "callerA",
                     .qualified_name = "ou-proj.a.callerA",
                     .file_path = "a.c",
                     .start_line = 30,
                     .end_line = 40};
    cbm_node_t cb = {.project = proj,
                     .label = "Function",
                     .name = "callerB",
                     .qualified_name = "ou-proj.b.callerB",
                     .file_path = "b.c",
                     .start_line = 50,
                     .end_line = 60};
    int64_t id_da = cbm_store_upsert_node(st, &da);
    int64_t id_db = cbm_store_upsert_node(st, &db);
    int64_t id_ca = cbm_store_upsert_node(st, &ca);
    int64_t id_cb = cbm_store_upsert_node(st, &cb);
    ASSERT_GT(id_da, 0);
    ASSERT_GT(id_db, 0);
    ASSERT_GT(id_ca, 0);
    ASSERT_GT(id_cb, 0);
    cbm_edge_t ea = {.project = proj, .source_id = id_ca, .target_id = id_da, .type = "CALLS"};
    cbm_edge_t eb = {.project = proj, .source_id = id_cb, .target_id = id_db, .type = "CALLS"};
    cbm_store_insert_edge(st, &ea);
    cbm_store_insert_edge(st, &eb);

    char *resp = cbm_mcp_server_handle(
        srv,
        "{\"jsonrpc\":\"2.0\",\"id\":63,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"trace_call_path\",\"arguments\":{\"function_name\":\"dupreal\","
        "\"project\":\"ou-proj\",\"direction\":\"inbound\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    /* distinct symbols must be disambiguated, not merged into one caller set */
    ASSERT_NOT_NULL(strstr(inner, "ambiguous"));
    ASSERT_NOT_NULL(strstr(inner, "suggestions"));
    ASSERT_NULL(strstr(inner, "\"callers\""));
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Guard that the ambiguity gate does NOT regress the #546 fix: a real .ts
 * implementation plus a body-less ambient .d.ts stub is ONE logical symbol
 * (one real callable def + a fragment), so it must stay non-ambiguous and the
 * caller sets from both nodes must be unioned. */
TEST(tool_trace_call_path_dts_stub_unions_with_impl) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "dts-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/dts");
    cbm_node_t impl = {.project = proj,
                       .label = "Function",
                       .name = "sym546",
                       .qualified_name = "dts-proj.impl.sym546",
                       .file_path = "src/sym.ts",
                       .start_line = 10,
                       .end_line = 30}; /* real body */
    cbm_node_t stub = {.project = proj,
                       .label = "Function",
                       .name = "sym546",
                       .qualified_name = "dts-proj.stub.sym546",
                       .file_path = "types/sym.d.ts",
                       .start_line = 5,
                       .end_line = 5}; /* body-less ambient decl */
    cbm_node_t crel = {.project = proj,
                       .label = "Function",
                       .name = "callerRel",
                       .qualified_name = "dts-proj.callerRel",
                       .file_path = "src/rel.ts",
                       .start_line = 1,
                       .end_line = 8};
    cbm_node_t cali = {.project = proj,
                       .label = "Function",
                       .name = "callerAlias",
                       .qualified_name = "dts-proj.callerAlias",
                       .file_path = "src/ali.ts",
                       .start_line = 1,
                       .end_line = 8};
    int64_t id_impl = cbm_store_upsert_node(st, &impl);
    int64_t id_stub = cbm_store_upsert_node(st, &stub);
    int64_t id_crel = cbm_store_upsert_node(st, &crel);
    int64_t id_cali = cbm_store_upsert_node(st, &cali);
    ASSERT_GT(id_impl, 0);
    ASSERT_GT(id_stub, 0);
    ASSERT_GT(id_crel, 0);
    ASSERT_GT(id_cali, 0);
    /* callers split by import style: relative -> impl, path-alias -> stub */
    cbm_edge_t er = {.project = proj, .source_id = id_crel, .target_id = id_impl, .type = "CALLS"};
    cbm_edge_t el = {.project = proj, .source_id = id_cali, .target_id = id_stub, .type = "CALLS"};
    cbm_store_insert_edge(st, &er);
    cbm_store_insert_edge(st, &el);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":64,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"trace_call_path\",\"arguments\":{\"function_name\":\"sym546\","
             "\"project\":\"dts-proj\",\"direction\":\"inbound\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NULL(strstr(inner, "ambiguous"));
    /* union across impl + stub: BOTH callers appear (this is the #546 fix) */
    ASSERT_NOT_NULL(strstr(inner, "callerRel"));
    ASSERT_NOT_NULL(strstr(inner, "callerAlias"));
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_delete_project_not_found) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"delete_project\","
                                   "\"arguments\":{\"project\":\"nonexistent\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "not_found"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_delete_project_mutation_guard_blocks_then_releases) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-mcp-delete-guard-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS();
    }

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-delete-project";
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    cbm_store_t *setup = cbm_store_open_path(db_path);
    ASSERT_NOT_NULL(setup);
    ASSERT_EQ(cbm_store_upsert_project(setup, project, "/tmp/guard-delete-project"), CBM_STORE_OK);
    cbm_store_close(setup);
    ASSERT_TRUE(cbm_file_exists(db_path));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t probe = {.deny_begin_call = 1};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char *resp =
        cbm_mcp_handle_tool(srv, "delete_project", "{\"project\":\"guard-delete-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "blocked"));
    ASSERT_EQ(probe.begin_count, 1);
    ASSERT_EQ(probe.end_count, 0);
    ASSERT_STR_EQ(probe.begin_projects[0], project);
    ASSERT_TRUE(cbm_file_exists(db_path));
    free(resp);

    probe.deny_begin_call = 0;
    resp = cbm_mcp_handle_tool(srv, "delete_project", "{\"project\":\"guard-delete-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "deleted"));
    ASSERT_EQ(probe.begin_count, 2);
    ASSERT_EQ(probe.end_count, 1);
    ASSERT_STR_EQ(probe.begin_projects[1], project);
    ASSERT_STR_EQ(probe.end_projects[0], project);
    ASSERT_FALSE(cbm_file_exists(db_path));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    cbm_rmdir(cache);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    PASS();
}

TEST(tool_index_repository_mutation_guard_blocks_before_local_worker) {
    char root[CBM_SZ_1K];
    (void)snprintf(root, sizeof(root), "%s/cbm-index-guard-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(root));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t probe = {.deny_begin_call = 1};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char args[CBM_SZ_2K];
    (void)snprintf(args, sizeof(args),
                   "{\"repo_path\":\"%s\",\"name\":\"GuardedIndex\","
                   "\"mode\":\"fast\"}",
                   root);
    int spawn_before = cbm_index_supervisor_spawn_count();
    char *response = cbm_mcp_handle_tool(srv, "index_repository", args);
    int spawn_after = cbm_index_supervisor_spawn_count();

    ASSERT_NOT_NULL(response);
    ASSERT_NOT_NULL(strstr(response, "blocked"));
    ASSERT_EQ(probe.begin_count, 1);
    ASSERT_EQ(probe.end_count, 0);
    ASSERT_STR_EQ(probe.begin_projects[0], "GuardedIndex");
    ASSERT_EQ(spawn_after, spawn_before);

    free(response);
    cbm_mcp_server_free(srv);
    (void)th_rmtree(root);
    PASS();
}

TEST(tool_get_architecture_empty) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":24,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"get_architecture\","
                                   "\"arguments\":{\"project\":\"nonexistent\"}}}");
    ASSERT_NOT_NULL(resp);
    /* No store for nonexistent project — should return project error */
    ASSERT_TRUE(strstr(resp, "not found") || strstr(resp, "not indexed"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Regression for #281: handle_get_architecture must actually call
 * cbm_store_get_architecture and surface its sections. Before the fix
 * only label/edge histograms were emitted regardless of which aspects
 * were requested. The store-side arch_entry_points query reads
 * properties.is_entry_point on Function nodes, so we tag one node and
 * assert the resulting JSON surfaces an "entry_points" array containing
 * the tagged function — which is impossible without the wiring. */
TEST(tool_get_architecture_emits_populated_sections) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "arch-test";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/arch-test");

    cbm_node_t main_fn = {0};
    main_fn.project = proj;
    main_fn.label = "Function";
    main_fn.name = "main";
    main_fn.qualified_name = "arch-test.cmd.main";
    main_fn.file_path = "cmd/main.go";
    main_fn.start_line = 1;
    main_fn.end_line = 3;
    main_fn.properties_json = "{\"is_entry_point\":true}";
    ASSERT_GT(cbm_store_upsert_node(st, &main_fn), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":91,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch-test\",\"aspects\":[\"all\"]}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* The handler always emits node/edge counts and schema histograms;
     * those existed before #281. The "entry_points" array only appears
     * when cbm_store_get_architecture is actually called and its result
     * is serialized — which is exactly what #281 wires up. */
    ASSERT_NOT_NULL(strstr(inner, "entry_points:"));
    ASSERT_NOT_NULL(strstr(inner, "main"));
    ASSERT_NOT_NULL(strstr(inner, "entry_points_total: 1"));
    ASSERT_NOT_NULL(strstr(inner, "entry_points_truncated: false"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_get_architecture_reports_entry_point_slice_metadata) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "arch-entry-total";
    cbm_mcp_server_set_project(srv, proj);
    ASSERT_EQ(cbm_store_upsert_project(st, proj, "/tmp/arch-entry-total"), CBM_STORE_OK);
    for (int i = 0; i < 21; i++) {
        char name[32];
        char qn[64];
        char file[64];
        snprintf(name, sizeof(name), "entry_%02d", i);
        snprintf(qn, sizeof(qn), "arch-entry-total.cmd.%s", name);
        snprintf(file, sizeof(file), "cmd/%s.c", name);
        cbm_node_t node = {.project = proj,
                           .label = "Function",
                           .name = name,
                           .qualified_name = qn,
                           .file_path = file,
                           .properties_json = "{\"is_entry_point\":true}"};
        ASSERT_GT(cbm_store_upsert_node(st, &node), 0);
    }

    char *tree_resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":92,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch-entry-total\","
             "\"aspects\":[\"entry_points\"]}}}");
    ASSERT_NOT_NULL(tree_resp);
    char *tree = extract_text_content(tree_resp);
    ASSERT_NOT_NULL(tree);
    ASSERT_NOT_NULL(strstr(tree, "entry_points_total: 21"));
    ASSERT_NOT_NULL(strstr(tree, "entry_points_truncated: true"));
    ASSERT_NOT_NULL(strstr(tree, "entry_points: 20"));
    free(tree);
    free(tree_resp);

    char *json_resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":93,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch-entry-total\","
             "\"aspects\":[\"entry_points\"],\"format\":\"json\"}}}");
    ASSERT_NOT_NULL(json_resp);
    char *json = extract_text_content(json_resp);
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"entry_points_total\":21"));
    ASSERT_NOT_NULL(strstr(json, "\"entry_points_truncated\":true"));
    free(json);
    free(json_resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_get_architecture_reports_route_slice_metadata) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "arch-route-total";
    cbm_mcp_server_set_project(srv, proj);
    ASSERT_EQ(cbm_store_upsert_project(st, proj, "/tmp/arch-route-total"), CBM_STORE_OK);

    /* One CALLS edge so the schema carries a relationship pattern; the routes
     * aspect gate requires rel_pattern_count > 0. */
    cbm_node_t caller = {.project = proj,
                         .label = "Function",
                         .name = "caller",
                         .qualified_name = "arch-route-total.caller",
                         .file_path = "src/lib.rs",
                         .properties_json = "{}"};
    cbm_node_t callee = {.project = proj,
                         .label = "Function",
                         .name = "callee",
                         .qualified_name = "arch-route-total.callee",
                         .file_path = "src/lib.rs",
                         .properties_json = "{}"};
    int64_t id_caller = cbm_store_upsert_node(st, &caller);
    int64_t id_callee = cbm_store_upsert_node(st, &callee);
    ASSERT_GT(id_caller, 0);
    ASSERT_GT(id_callee, 0);
    cbm_edge_t call = {
        .project = proj, .source_id = id_caller, .target_id = id_callee, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &call), 0);

    for (int i = 0; i < 21; i++) {
        char name[32];
        char qn[64];
        char props[128];
        snprintf(name, sizeof(name), "/api/r%02d", i);
        snprintf(qn, sizeof(qn), "arch-route-total.route.%02d", i);
        snprintf(props, sizeof(props),
                 "{\"method\":\"GET\",\"path\":\"%s\",\"handler\":\"h%02d\"}", name, i);
        cbm_node_t node = {.project = proj,
                           .label = "Route",
                           .name = name,
                           .qualified_name = qn,
                           .file_path = "src/routes.rs",
                           .properties_json = props};
        ASSERT_GT(cbm_store_upsert_node(st, &node), 0);
    }

    char *tree_resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":94,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch-route-total\","
             "\"aspects\":[\"routes\"]}}}");
    ASSERT_NOT_NULL(tree_resp);
    char *tree = extract_text_content(tree_resp);
    ASSERT_NOT_NULL(tree);
    ASSERT_NOT_NULL(strstr(tree, "routes_total: 21"));
    ASSERT_NOT_NULL(strstr(tree, "routes_truncated: true"));
    ASSERT_NOT_NULL(strstr(tree, "routes: 20"));
    free(tree);
    free(tree_resp);

    char *json_resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":95,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch-route-total\","
             "\"aspects\":[\"routes\"],\"format\":\"json\"}}}");
    ASSERT_NOT_NULL(json_resp);
    char *json = extract_text_content(json_resp);
    ASSERT_NOT_NULL(json);
    ASSERT_NOT_NULL(strstr(json, "\"routes_total\":21"));
    ASSERT_NOT_NULL(strstr(json, "\"routes_truncated\":true"));
    free(json);
    free(json_resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Distills PR #560 (overview subset): "overview" must expand to a compact
 * subset — every aspect EXCEPT file_tree. Before the fix, "overview" was not
 * registered in either aspect gate (want_aspect in store.c, aspect_wanted in
 * mcp.c), so aspects=["overview"] silently degraded to just
 * {total_nodes,total_edges}. RED on unfixed code: no "entry_points" key. */
TEST(tool_get_architecture_overview_compact_subset_pr560) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "arch560";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/arch560");

    cbm_node_t main_fn = {0};
    main_fn.project = proj;
    main_fn.label = "Function";
    main_fn.name = "main";
    main_fn.qualified_name = "arch560.cmd.main";
    main_fn.file_path = "cmd/main.go";
    main_fn.start_line = 1;
    main_fn.end_line = 3;
    main_fn.properties_json = "{\"is_entry_point\":true}";
    ASSERT_GT(cbm_store_upsert_node(st, &main_fn), 0);

    /* A File node so the file_tree aspect has real content — makes the
     * "overview drops file_tree" assertion below non-vacuous. */
    cbm_node_t file_node = {.project = proj,
                            .label = "File",
                            .name = "main.go",
                            .qualified_name = "arch560.cmd.main.go",
                            .file_path = "cmd/main.go"};
    ASSERT_GT(cbm_store_upsert_node(st, &file_node), 0);

    /* Sanity: with "all", both entry_points and file_tree surface. */
    char *resp_all = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":560,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch560\",\"aspects\":[\"all\"]}}}");
    ASSERT_NOT_NULL(resp_all);
    char *inner_all = extract_text_content(resp_all);
    ASSERT_NOT_NULL(inner_all);
    ASSERT_NOT_NULL(strstr(inner_all, "entry_points:"));
    ASSERT_NOT_NULL(strstr(inner_all, "file_tree:"));
    free(inner_all);
    free(resp_all);

    /* "overview": substantive content (entry_points, node_labels) but NO
     * file_tree section. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":561,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch560\",\"aspects\":[\"overview\"]}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "entry_points:"));
    ASSERT_NOT_NULL(strstr(inner, "node_labels:"));
    ASSERT_NULL(strstr(inner, "file_tree:"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Distills PR #560 (server-side validation): unknown aspect tokens must be
 * rejected with an isError result listing the valid values. Before the fix
 * the JSON-Schema accepted any string and both aspect gates simply never
 * matched, so a typo like "bogus_aspect" produced a silent near-empty payload
 * with isError:false. RED on unfixed code: no isError, no "Unknown aspect". */
TEST(tool_get_architecture_rejects_unknown_aspect_pr560) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "arch560v";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/arch560v");

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":562,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch560v\",\"aspects\":[\"bogus_aspect\"]}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"isError\":true"));
    ASSERT_NOT_NULL(strstr(resp, "Unknown aspect 'bogus_aspect'"));
    /* The error must teach the valid vocabulary, including the new token. */
    ASSERT_NOT_NULL(strstr(resp, "overview"));
    ASSERT_NOT_NULL(strstr(resp, "file_tree"));

    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Reproduce-first for #640: query handlers must accept the `project_name`
 * alias, not only the canonical `project` key. list_projects surfaces the field
 * as "name" and the error hint says "pass the project name", so a caller
 * naturally passes `project_name`. With no alias, the handler reads key
 * "project" -> NULL -> resolve_store bails before opening any .db -> "project
 * not found or not indexed" even though the project is indexed. Mirrors
 * tool_get_architecture_emits_populated_sections but with the alias key. */
TEST(tool_get_architecture_accepts_project_name_alias_issue640) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "alias640";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/alias640");

    cbm_node_t main_fn = {0};
    main_fn.project = proj;
    main_fn.label = "Function";
    main_fn.name = "main";
    main_fn.qualified_name = "alias640.cmd.main";
    main_fn.file_path = "cmd/main.go";
    main_fn.start_line = 1;
    main_fn.end_line = 3;
    main_fn.properties_json = "{\"is_entry_point\":true}";
    ASSERT_GT(cbm_store_upsert_node(st, &main_fn), 0);

    /* Caller passes `project_name` (the natural guess) instead of `project`. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":640,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project_name\":\"alias640\",\"aspects\":[\"all\"]}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* RED before the alias: inner is the "project not found" error.
     * GREEN after: the alias resolves and architecture sections surface. */
    ASSERT_NULL(strstr(inner, "project not found"));
    ASSERT_NOT_NULL(strstr(inner, "entry_points:"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Reproduce-first for #640: the alias must apply across query handlers, not
 * just get_architecture. search_graph with `project_name` must resolve too. */
TEST(tool_search_graph_accepts_project_name_alias_issue640) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "alias640b";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/alias640b");

    cbm_node_t fn = {0};
    fn.project = proj;
    fn.label = "Function";
    fn.name = "WidgetHandler";
    fn.qualified_name = "alias640b.svc.WidgetHandler";
    fn.file_path = "svc/widget.go";
    fn.start_line = 1;
    fn.end_line = 2;
    ASSERT_GT(cbm_store_upsert_node(st, &fn), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":641,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project_name\":\"alias640b\",\"name_pattern\":\"Widget.*\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    ASSERT_NULL(strstr(inner, "project not found"));
    ASSERT_NOT_NULL(strstr(inner, "WidgetHandler"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* #1025: agents pass the repo FOLDER name ("codebase-memory-mcp"), but
 * indexed project names derive from the full path
 * (E:\project\graph\x -> "E-project-graph-x"), so exact lookup fails with
 * "project not found" while list_projects clearly shows the project. A
 * passed name that matches exactly ONE indexed project as a segment-aligned
 * tail ("-<name>" suffix) must resolve to it; zero or several matches keep
 * the existing error. Runs against real cache-dir .db files (the resolution
 * scans filenames), so this test indexes real fixtures under an overridden
 * CBM_CACHE_DIR. */
static void i1025_write_repo(const char *dir, const char *fn_name) {
    char path[CBM_SZ_512];
    snprintf(path, sizeof(path), "%s/mod.py", dir);
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "def %s(x):\n    return x + 1\n", fn_name);
    fclose(f);
}

TEST(tool_project_arg_resolves_unique_tail_issue1025) {
    char repo_a[CBM_SZ_256];
    char repo_b[CBM_SZ_256];
    char repo_c[CBM_SZ_256];
    char cache[CBM_SZ_256];
    snprintf(repo_a, sizeof(repo_a), "/tmp/cbm-i1025a-XXXXXX");
    snprintf(repo_b, sizeof(repo_b), "/tmp/cbm-i1025b-XXXXXX");
    snprintf(repo_c, sizeof(repo_c), "/tmp/cbm-i1025c-XXXXXX");
    snprintf(cache, sizeof(cache), "/tmp/cbm-i1025d-XXXXXX");
    if (!cbm_mkdtemp(repo_a) || !cbm_mkdtemp(repo_b) || !cbm_mkdtemp(repo_c) ||
        !cbm_mkdtemp(cache)) {
        FAIL("mkdtemp failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    i1025_write_repo(repo_a, "unique_tail_target");
    i1025_write_repo(repo_b, "amb_one");
    i1025_write_repo(repo_c, "amb_two");

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[CBM_SZ_1K];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"name\":\"E-project-graph-suffix1025\"}",
             repo_a);
    char *r = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(r);
    free(r);
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"name\":\"F-alpha-amb1025\"}", repo_b);
    r = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(r);
    free(r);
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"name\":\"G-beta-amb1025\"}", repo_c);
    r = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(r);
    free(r);

    /* 1. Unique tail resolves (RED today: "project not found"). */
    r = cbm_mcp_handle_tool(srv, "search_graph",
                            "{\"project\":\"suffix1025\",\"name_pattern\":\".*target.*\"}");
    ASSERT_NOT_NULL(r);
    if (strstr(r, "project not found")) {
        fprintf(stderr, "  [1025] FAIL unique tail did not resolve: %.200s\n", r);
    }
    ASSERT_NULL(strstr(r, "project not found"));
    ASSERT_NOT_NULL(strstr(r, "unique_tail_target"));
    free(r);

    /* 2. Ambiguous tail stays an error (never guess between projects). */
    r = cbm_mcp_handle_tool(srv, "search_graph",
                            "{\"project\":\"amb1025\",\"name_pattern\":\".*\"}");
    ASSERT_NOT_NULL(r);
    ASSERT_NOT_NULL(strstr(r, "project not found"));
    free(r);

    /* 3. Exact full name keeps working unchanged. */
    r = cbm_mcp_handle_tool(srv, "search_graph",
                            "{\"project\":\"E-project-graph-suffix1025\","
                            "\"name_pattern\":\".*target.*\"}");
    ASSERT_NOT_NULL(r);
    ASSERT_NULL(strstr(r, "project not found"));
    free(r);

    cbm_mcp_server_free(srv);
    if (saved_cache_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_cache_copy, 1);
        free(saved_cache_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    th_rmtree(repo_a);
    th_rmtree(repo_b);
    th_rmtree(repo_c);
    th_rmtree(cache);
    PASS();
}

/*
 * A call made INSIDE a Rust `#[cfg(...)]`-gated function must be attributed to
 * that FUNCTION, never to the file's `__file__` node.
 *
 * Mechanism (measured 2026-07-29 on a ~9k-node Rust workspace): the def walk
 * folds the cfg predicate into the definition QN via rust_cfg_qualified_name()
 * (`foo` -> `foo#cfg(unix)]`) so cfg-gated twins stay distinct (#495), but the
 * CALL side computes the enclosing-function QN through cbm_enclosing_func_qn(),
 * which applied no such suffix. The QNs therefore disagreed, the gbuf lookup in
 * calls_find_source() missed, and the call fell back to the `__file__` node.
 *
 * Consequence, and why this is a lying-count defect rather than cosmetics: the
 * File node then appears as a row in trace_path's caller list and is counted in
 * `callers_total`, while the REAL caller is missing from it. 225 of 240
 * File-sourced CALLS in that workspace were such mis-attributions; 194 of them
 * came from this cfg-suffix mismatch.
 *
 * The property under test is attribution parity, asserted in BOTH directions so
 * the test cannot pass by merely dropping the edge: the cfg-gated caller must
 * appear as a caller of the callee, and the file node must not.
 */
static void cfgattr_write_repo(const char *dir) {
    char path[CBM_SZ_512];
    snprintf(path, sizeof(path), "%s/lib.rs", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "pub fn cfgattr_callee() -> u32 { 7 }\n"
               "\n"
               "#[cfg(unix)]\n"
               "pub fn cfgattr_gated_caller() -> u32 {\n"
               "    cfgattr_callee()\n"
               "}\n");
    fclose(f);
}

TEST(tool_trace_cfg_gated_caller_not_attributed_to_file_node) {
    char repo[CBM_SZ_256];
    char cache[CBM_SZ_256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-cfgattr-repo-XXXXXX");
    snprintf(cache, sizeof(cache), "/tmp/cbm-cfgattr-cache-XXXXXX");
    if (!cbm_mkdtemp(repo) || !cbm_mkdtemp(cache)) {
        FAIL("mkdtemp failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    cfgattr_write_repo(repo);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[CBM_SZ_1K];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"name\":\"cfgattr-proj\"}", repo);
    char *r = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(r);
    free(r);

    /* Callers of the callee, tests included so nothing is filtered by policy. */
    r = cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"cfgattr-proj\",\"function_name\":\"cfgattr_callee\","
                            "\"direction\":\"inbound\",\"include_tests\":true}");
    ASSERT_NOT_NULL(r);

    /* The real, cfg-gated call site must be reported as the caller. */
    if (!strstr(r, "cfgattr_gated_caller")) {
        fprintf(stderr, "  [cfgattr] FAIL cfg-gated caller absent from callers: %.400s\n", r);
    }
    ASSERT_NOT_NULL(strstr(r, "cfgattr_gated_caller"));
    /* ...and the File node must NOT stand in for it. */
    if (strstr(r, "__file__")) {
        fprintf(stderr, "  [cfgattr] FAIL __file__ node listed as a caller: %.400s\n", r);
    }
    ASSERT_NULL(strstr(r, "__file__"));
    free(r);

    cbm_mcp_server_free(srv);
    if (saved_cache_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_cache_copy, 1);
        free(saved_cache_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    th_rmtree(repo);
    th_rmtree(cache);
    PASS();
}

/*
 * A call made inside a method of a GENERIC `impl` block must be attributed to
 * that method, never to the file's `__file__` node.
 *
 * Same class of defect as the cfg-suffix case above, different half of the same
 * seam: the DEF walk strips generic arguments from the implementing type
 * (`Holder<T>` -> `Holder`, extract_defs.c extract_rust_impl) so the Method node
 * is `...Holder.run`, but the CALL walk's class-scope QN (compute_class_qn,
 * extract_unified.c) used the `type` field's RAW text, yielding `Holder<T>.run`.
 * calls_find_source() joins those by exact equality, so the lookup missed and
 * the call fell back to the file node — inflating `callers_total` with a
 * non-callable row while losing the real caller.
 *
 * Triggered by angle brackets in the impl's `type` text, which is why the
 * fixture uses `impl<T> Holder<T>`: a bare `impl<T> Trait for Plain` has generic
 * params but no brackets in `type` and was never affected.
 */
static void genattr_write_repo(const char *dir) {
    char path[CBM_SZ_512];
    snprintf(path, sizeof(path), "%s/lib.rs", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "pub fn genattr_callee() -> u32 { 7 }\n"
               "\n"
               "pub struct Holder<T> { pub item: T }\n"
               "\n"
               "impl<T> Holder<T> {\n"
               "    pub fn genattr_generic_caller(&self) -> u32 {\n"
               "        genattr_callee()\n"
               "    }\n"
               "}\n");
    fclose(f);
}

TEST(tool_trace_generic_impl_caller_not_attributed_to_file_node) {
    char repo[CBM_SZ_256];
    char cache[CBM_SZ_256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-genattr-repo-XXXXXX");
    snprintf(cache, sizeof(cache), "/tmp/cbm-genattr-cache-XXXXXX");
    if (!cbm_mkdtemp(repo) || !cbm_mkdtemp(cache)) {
        FAIL("mkdtemp failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    genattr_write_repo(repo);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[CBM_SZ_1K];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"name\":\"genattr-proj\"}", repo);
    char *r = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(r);
    free(r);

    r = cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"genattr-proj\",\"function_name\":\"genattr_callee\","
                            "\"direction\":\"inbound\",\"include_tests\":true}");
    ASSERT_NOT_NULL(r);

    if (!strstr(r, "genattr_generic_caller")) {
        fprintf(stderr, "  [genattr] FAIL generic-impl caller absent from callers: %.400s\n", r);
    }
    ASSERT_NOT_NULL(strstr(r, "genattr_generic_caller"));
    if (strstr(r, "__file__")) {
        fprintf(stderr, "  [genattr] FAIL __file__ node listed as a caller: %.400s\n", r);
    }
    ASSERT_NULL(strstr(r, "__file__"));
    free(r);

    cbm_mcp_server_free(srv);
    if (saved_cache_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_cache_copy, 1);
        free(saved_cache_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    th_rmtree(repo);
    th_rmtree(cache);
    PASS();
}

/*
 * A call made inside a NESTED `fn` (a helper declared in another function's
 * body) must be attributed to the enclosing OUTER function, never to the file's
 * `__file__` node.
 *
 * Third and last variant of the same seam. Here the two walks disagree about
 * which nodes EXIST: the Rust def walk emits a node for a function item and does
 * not descend into its body, so a nested `fn` gets NO node at all — while the
 * call walk pushed a SCOPE_FUNC for it anyway, making the enclosing QN name a
 * function nothing in the graph carries. The lookup in calls_find_source()
 * therefore missed and the call landed on the file node.
 *
 * The remedy is the one already proven for OCaml in push_boundary_scopes: only
 * the outermost function pushes a scope, so attribution matches the def walk's
 * inventory by construction. Attributing to the outer function is not an
 * approximation — it is the only callable node that textually contains the call.
 */
static void nestattr_write_repo(const char *dir) {
    char path[CBM_SZ_512];
    snprintf(path, sizeof(path), "%s/lib.rs", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "pub fn nestattr_callee() -> u32 { 7 }\n"
               "\n"
               "pub fn nestattr_outer() -> u32 {\n"
               "    fn nestattr_inner() -> u32 {\n"
               "        nestattr_callee()\n"
               "    }\n"
               "    nestattr_inner()\n"
               "}\n");
    fclose(f);
}

TEST(tool_trace_nested_fn_caller_not_attributed_to_file_node) {
    char repo[CBM_SZ_256];
    char cache[CBM_SZ_256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-nestattr-repo-XXXXXX");
    snprintf(cache, sizeof(cache), "/tmp/cbm-nestattr-cache-XXXXXX");
    if (!cbm_mkdtemp(repo) || !cbm_mkdtemp(cache)) {
        FAIL("mkdtemp failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    nestattr_write_repo(repo);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[CBM_SZ_1K];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"name\":\"nestattr-proj\"}", repo);
    char *r = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(r);
    free(r);

    r = cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"nestattr-proj\",\"function_name\":\"nestattr_callee\","
                            "\"direction\":\"inbound\",\"depth\":1,\"include_tests\":true}");
    ASSERT_NOT_NULL(r);

    /* The nested function owns the call site. The outer function calls the
     * helper, not the callee directly; collapsing both invents a direct edge. */
    if (!strstr(r, "  nestattr_inner 1")) {
        fprintf(stderr, "  [nestattr] FAIL nested fn absent from callers: %.400s\n", r);
    }
    ASSERT_NOT_NULL(strstr(r, "  nestattr_inner 1"));
    ASSERT_NULL(strstr(r, "  nestattr_outer 1"));
    if (strstr(r, "__file__")) {
        fprintf(stderr, "  [nestattr] FAIL __file__ node listed as a caller: %.400s\n", r);
    }
    ASSERT_NULL(strstr(r, "__file__"));
    free(r);

    cbm_mcp_server_free(srv);
    if (saved_cache_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_cache_copy, 1);
        free(saved_cache_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    th_rmtree(repo);
    th_rmtree(cache);
    PASS();
}

/*
 * A call inside a `#[cfg(...)]`-gated METHOD of an impl block must be attributed
 * to that method, never to the file's `__file__` node.
 *
 * This pins an ASYMMETRY in the def walk that is easy to "tidy" into a bug: a
 * free function's node QN carries the cfg predicate (extract_func_def ->
 * rust_cfg_qualified_name), but a method inside an `impl` block is emitted by
 * extract_rust_impl() as plain `<type_qn>.<name>` with NO predicate. The call
 * walk must therefore suffix the free-function QN and must NOT suffix the
 * method QN. Applying the suffix uniformly looks more consistent and silently
 * re-breaks exactly this case — measured: 9 call sites regressed that way while
 * the free-function fix was landing.
 *
 * Guard both directions at once: the sibling cfg test above fails if the free
 * function stops being suffixed, this one fails if the method starts being.
 */
static void cfgmethod_write_repo(const char *dir) {
    char path[CBM_SZ_512];
    snprintf(path, sizeof(path), "%s/lib.rs", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "pub fn cfgmethod_callee() -> u32 { 7 }\n"
               "\n"
               "pub struct Registry;\n"
               "\n"
               "impl Registry {\n"
               "    #[cfg(feature = \"test-util\")]\n"
               "    pub fn cfgmethod_gated_method(&self) -> u32 {\n"
               "        cfgmethod_callee()\n"
               "    }\n"
               "}\n");
    fclose(f);
}

TEST(tool_trace_cfg_gated_method_not_attributed_to_file_node) {
    char repo[CBM_SZ_256];
    char cache[CBM_SZ_256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-cfgmethod-repo-XXXXXX");
    snprintf(cache, sizeof(cache), "/tmp/cbm-cfgmethod-cache-XXXXXX");
    if (!cbm_mkdtemp(repo) || !cbm_mkdtemp(cache)) {
        FAIL("mkdtemp failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    cfgmethod_write_repo(repo);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[CBM_SZ_1K];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"name\":\"cfgmethod-proj\"}", repo);
    char *r = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(r);
    free(r);

    r = cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"cfgmethod-proj\","
                            "\"function_name\":\"cfgmethod_callee\","
                            "\"direction\":\"inbound\",\"include_tests\":true}");
    ASSERT_NOT_NULL(r);

    if (!strstr(r, "cfgmethod_gated_method")) {
        fprintf(stderr, "  [cfgmethod] FAIL cfg-gated method absent from callers: %.400s\n", r);
    }
    ASSERT_NOT_NULL(strstr(r, "cfgmethod_gated_method"));
    if (strstr(r, "__file__")) {
        fprintf(stderr, "  [cfgmethod] FAIL __file__ node listed as a caller: %.400s\n", r);
    }
    ASSERT_NULL(strstr(r, "__file__"));
    free(r);

    cbm_mcp_server_free(srv);
    if (saved_cache_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_cache_copy, 1);
        free(saved_cache_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    th_rmtree(repo);
    th_rmtree(cache);
    PASS();
}

/*
 * `callers_total` must count only things that can call. A File node cannot.
 *
 * The three extraction fixes above remove the causes of File-sourced CALLS
 * edges that were really mis-attributions, but they cannot remove the category:
 * a call genuinely written at file/module scope (a `const` initialiser, say) has
 * no enclosing function to be attributed to, and the extractor is right to
 * source it at the file. Reporting is therefore the last line — a count is the
 * tool's claim about the world, and `callers_total` claims "this many places
 * call this".
 *
 * This test drives the store directly rather than the indexer, so it pins the
 * REPORTING contract independently of whichever extraction paths currently mint
 * such edges: the row is excluded from `callers` and from `callers_total`, and
 * — because the edge is real evidence a call exists in that file — it is still
 * reported under the directional `unattributed_*` section rather than silently dropped.
 */
TEST(tool_trace_file_and_module_nodes_excluded_from_callers_total) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "filetotal-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/filetotal");

    cbm_node_t callee = {.project = proj,
                         .label = "Function",
                         .name = "ft_callee",
                         .qualified_name = "filetotal-proj.src.lib.ft_callee",
                         .file_path = "src/lib.rs",
                         .start_line = 1,
                         .end_line = 3};
    cbm_node_t real_caller = {.project = proj,
                              .label = "Function",
                              .name = "ft_real_caller",
                              .qualified_name = "filetotal-proj.src.lib.ft_real_caller",
                              .file_path = "src/lib.rs",
                              .start_line = 10,
                              .end_line = 20};
    /* The File node: reachable only because a CALLS edge was sourced at it. */
    cbm_node_t file_node = {.project = proj,
                            .label = "File",
                            .name = "__file__",
                            .qualified_name = "filetotal-proj.src.lib.rs.__file__",
                            .file_path = "src/lib.rs",
                            .start_line = 0,
                            .end_line = 0};
    cbm_node_t module_node = {.project = proj,
                              .label = "Module",
                              .name = "lib",
                              .qualified_name = "filetotal-proj.src.lib",
                              .file_path = "src/lib.rs",
                              .start_line = 1,
                              .end_line = 30};
    int64_t id_callee = cbm_store_upsert_node(st, &callee);
    int64_t id_real = cbm_store_upsert_node(st, &real_caller);
    int64_t id_file = cbm_store_upsert_node(st, &file_node);
    int64_t id_module = cbm_store_upsert_node(st, &module_node);
    ASSERT_GT(id_callee, 0);
    ASSERT_GT(id_real, 0);
    ASSERT_GT(id_file, 0);
    ASSERT_GT(id_module, 0);

    cbm_edge_t e_real = {
        .project = proj, .source_id = id_real, .target_id = id_callee, .type = "CALLS"};
    cbm_edge_t e_file = {
        .project = proj, .source_id = id_file, .target_id = id_callee, .type = "CALLS"};
    cbm_edge_t e_module = {
        .project = proj, .source_id = id_module, .target_id = id_callee, .type = "CALLS"};
    cbm_store_insert_edge(st, &e_real);
    cbm_store_insert_edge(st, &e_file);
    cbm_store_insert_edge(st, &e_module);

    char *resp = cbm_mcp_handle_tool(srv, "trace_call_path",
                                     "{\"project\":\"filetotal-proj\","
                                     "\"function_name\":\"ft_callee\","
                                     "\"direction\":\"inbound\",\"include_tests\":true}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* Exactly one caller — the function. File and Module nodes are not. */
    if (!strstr(inner, "callers_total: 1")) {
        fprintf(stderr, "  [filetotal] FAIL callers_total counts a structural node: %.400s\n",
                inner);
    }
    ASSERT_NOT_NULL(strstr(inner, "callers_total: 1"));
    ASSERT_NOT_NULL(strstr(inner, "ft_real_caller"));
    /* The evidence is kept, under a directional heading that does not claim
     * either structural node is a caller. */
    const char *unattributed = strstr(inner, "unattributed_inbound");
    ASSERT_NOT_NULL(unattributed);
    const char *module_row = strstr(inner, "\n  lib 1\n");
    ASSERT_TRUE(!module_row || module_row > unattributed);
    ASSERT_NOT_NULL(strstr(inner, "unattributed_inbound_total: 2"));

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* The response `limit` is one budget across callable and unattributed rows.
 * Cursor replay must enumerate the mixed canonical row stream exactly once:
 * a structural row cannot repeat on every page or arrive outside the budget.
 *
 * This also exercises the spill ownership path under Linux leak sanitizers.
 * The old in-place split lowered visited_count, so normal traversal cleanup
 * skipped the structural rows' six owned strings. */
TEST(tool_trace_unattributed_pagination_exactly_once_and_budgeted) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "unattr-page-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/unattr-page");

    cbm_node_t callee = {.project = proj,
                         .label = "Function",
                         .name = "up_callee",
                         .qualified_name = "unattr-page-proj.src.up_callee",
                         .file_path = "src/lib.rs",
                         .start_line = 1,
                         .end_line = 3};
    int64_t callee_id = cbm_store_upsert_node(st, &callee);
    ASSERT_GT(callee_id, 0);

    const char *labels[] = {"Function", "File", "Function", "Module"};
    const char *names[] = {"up_real_a", "__file__", "up_real_b", "up_module"};
    const char *qns[] = {"unattr-page-proj.src.up_real_a", "unattr-page-proj.src.lib.rs.__file__",
                         "unattr-page-proj.src.up_real_b", "unattr-page-proj.src.up_module"};
    for (int i = 0; i < 4; i++) {
        cbm_node_t caller = {.project = proj,
                             .label = labels[i],
                             .name = names[i],
                             .qualified_name = qns[i],
                             .file_path = "src/lib.rs",
                             .start_line = i + 10,
                             .end_line = i + 11};
        int64_t caller_id = cbm_store_upsert_node(st, &caller);
        ASSERT_GT(caller_id, 0);
        cbm_edge_t edge = {
            .project = proj, .source_id = caller_id, .target_id = callee_id, .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(st, &edge), 0);
    }

    char pages[4][4096];
    char cursor[192] = "";
    int page_count = 0;
    for (; page_count < 4; page_count++) {
        char args[640];
        if (cursor[0]) {
            snprintf(args, sizeof(args),
                     "{\"project\":\"unattr-page-proj\",\"function_name\":\"up_callee\","
                     "\"direction\":\"inbound\",\"limit\":1,\"include_tests\":true,"
                     "\"risk_labels\":true,\"cursor\":\"%s\"}",
                     cursor);
        } else {
            snprintf(args, sizeof(args),
                     "{\"project\":\"unattr-page-proj\",\"function_name\":\"up_callee\","
                     "\"direction\":\"inbound\",\"limit\":1,\"include_tests\":true,"
                     "\"risk_labels\":true}");
        }
        char *resp = cbm_mcp_handle_tool(srv, "trace_call_path", args);
        ASSERT_NOT_NULL(resp);
        char *inner = extract_text_content(resp);
        free(resp);
        ASSERT_NOT_NULL(inner);
        snprintf(pages[page_count], sizeof(pages[page_count]), "%s", inner);
        ASSERT_NOT_NULL(strstr(inner, "callers_total: 2"));
        ASSERT_NOT_NULL(strstr(inner, "unattributed_inbound_total: 2"));

        const char *unattr = strstr(inner, "unattributed_inbound:");
        if (unattr) {
            const char *line_end = strchr(unattr, '\n');
            const char *risk = strstr(unattr, "risk");
            ASSERT_NOT_NULL(risk);
            ASSERT_TRUE(!line_end || risk < line_end);
        }

        const char *next = strstr(inner, "next: ");
        if (next) {
            const char *end = strchr(next + 6, '\n');
            size_t len = end ? (size_t)(end - (next + 6)) : strlen(next + 6);
            ASSERT_TRUE(len < sizeof(cursor));
            memcpy(cursor, next + 6, len);
            cursor[len] = '\0';
        } else {
            cursor[0] = '\0';
        }
        free(inner);
        if (!cursor[0]) {
            page_count++;
            break;
        }
    }
    ASSERT_EQ(page_count, 4);

    for (int row = 0; row < 4; row++) {
        int seen = 0;
        for (int page = 0; page < page_count; page++) {
            if (strstr(pages[page], qns[row])) {
                seen++;
            }
        }
        ASSERT_EQ(seen, 1);
    }

    /* With limit=1, exactly one of the four row identities is emitted on
     * each page, regardless of which section owns it. */
    for (int page = 0; page < page_count; page++) {
        int rows_on_page = 0;
        for (int row = 0; row < 4; row++) {
            if (strstr(pages[page], qns[row])) {
                rows_on_page++;
            }
        }
        ASSERT_EQ(rows_on_page, 1);
    }

    cbm_mcp_server_free(srv);
    PASS();
}

/* Both trace legs may contain structural CALLS endpoints. Directional keys
 * keep the two evidence sets representable in JSON without duplicate object
 * members and without calling a Module a "file". */
TEST(tool_trace_unattributed_json_has_unique_directional_keys) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "unattr-json-proj";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/unattr-json");

    cbm_node_t hub = {.project = proj,
                      .label = "Function",
                      .name = "uj_hub",
                      .qualified_name = "unattr-json-proj.src.uj_hub",
                      .file_path = "src/lib.rs",
                      .start_line = 1,
                      .end_line = 3};
    cbm_node_t inbound_module = {.project = proj,
                                 .label = "Module",
                                 .name = "uj_inbound_module",
                                 .qualified_name = "unattr-json-proj.src.uj_inbound_module",
                                 .file_path = "src/lib.rs",
                                 .start_line = 5,
                                 .end_line = 8};
    cbm_node_t outbound_file = {.project = proj,
                                .label = "File",
                                .name = "__file__",
                                .qualified_name = "unattr-json-proj.out.rs.__file__",
                                .file_path = "out.rs",
                                .start_line = 0,
                                .end_line = 0};
    int64_t hub_id = cbm_store_upsert_node(st, &hub);
    int64_t inbound_id = cbm_store_upsert_node(st, &inbound_module);
    int64_t outbound_id = cbm_store_upsert_node(st, &outbound_file);
    ASSERT_GT(hub_id, 0);
    ASSERT_GT(inbound_id, 0);
    ASSERT_GT(outbound_id, 0);
    cbm_edge_t inbound = {
        .project = proj, .source_id = inbound_id, .target_id = hub_id, .type = "CALLS"};
    cbm_edge_t outbound = {
        .project = proj, .source_id = hub_id, .target_id = outbound_id, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(st, &inbound), 0);
    ASSERT_GT(cbm_store_insert_edge(st, &outbound), 0);

    char *resp =
        cbm_mcp_handle_tool(srv, "trace_call_path",
                            "{\"project\":\"unattr-json-proj\",\"function_name\":\"uj_hub\","
                            "\"direction\":\"both\",\"include_tests\":true,\"format\":\"json\"}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    free(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\"unattributed_outbound_total\":1"));
    ASSERT_NOT_NULL(strstr(inner, "\"unattributed_inbound_total\":1"));
    ASSERT_NOT_NULL(strstr(inner, "\"unattributed_outbound\""));
    ASSERT_NOT_NULL(strstr(inner, "\"unattributed_inbound\""));
    ASSERT_NULL(strstr(inner, "\"unattributed_total\""));
    ASSERT_NULL(strstr(inner, "\"unattributed_files\""));

    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_TRUE(yyjson_is_obj(root));
    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    PASS();
}

/* Regression for #604: path scopes architecture totals and content. */
TEST(tool_get_architecture_path_scoping) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    const char *proj = "arch-path";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, "/tmp/arch-path");

    cbm_node_t pkg_global = {.project = proj,
                             .label = "Package",
                             .name = "Django",
                             .qualified_name = "arch-path.Django",
                             .file_path = "vendor/django/__init__.py"};
    cbm_store_upsert_node(st, &pkg_global);

    cbm_node_t pkg_local = {.project = proj,
                            .label = "Package",
                            .name = "hoa",
                            .qualified_name = "arch-path.hoa",
                            .file_path = "apps/hoa/main.go"};
    cbm_store_upsert_node(st, &pkg_local);

    cbm_node_t f_hoa = {.project = proj,
                        .label = "File",
                        .name = "main.go",
                        .qualified_name = "arch-path.apps.hoa.main.go",
                        .file_path = "apps/hoa/main.go"};
    cbm_store_upsert_node(st, &f_hoa);

    cbm_node_t f_other = {.project = proj,
                          .label = "File",
                          .name = "other.go",
                          .qualified_name = "arch-path.other.go",
                          .file_path = "lib/other.go"};
    cbm_store_upsert_node(st, &f_other);

    char *resp_root = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":92,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"get_architecture\","
             "\"arguments\":{\"project\":\"arch-path\",\"aspects\":[\"packages\"]}}}");
    ASSERT_NOT_NULL(resp_root);
    char *inner_root = extract_text_content(resp_root);
    ASSERT_NOT_NULL(inner_root);
    ASSERT_NOT_NULL(strstr(inner_root, "Django"));

    char *resp_scoped =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":93,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"get_architecture\","
                                   "\"arguments\":{\"project\":\"arch-path\",\"path\":\"apps/hoa\","
                                   "\"aspects\":[\"packages\"]}}}");
    ASSERT_NOT_NULL(resp_scoped);
    char *inner_scoped = extract_text_content(resp_scoped);
    ASSERT_NOT_NULL(inner_scoped);

    ASSERT_NOT_NULL(strstr(inner_scoped, "root_total_nodes"));
    ASSERT_NOT_NULL(strstr(inner_scoped, "scoped_total_nodes"));
    ASSERT_NOT_NULL(strstr(inner_scoped, "path: "));
    ASSERT_NOT_NULL(strstr(inner_scoped, "hoa"));
    ASSERT_NULL(strstr(inner_scoped, "Django"));

    int root_nodes = 0;
    int scoped_nodes = 0;
    /* TOON scalar form (`key: N`) with JSON fallback for format:"json". */
    const char *rt = strstr(inner_scoped, "root_total_nodes: ");
    const char *stn = strstr(inner_scoped, "scoped_total_nodes: ");
    if (rt) {
        sscanf(rt, "root_total_nodes: %d", &root_nodes);
    } else if ((rt = strstr(inner_scoped, "\"root_total_nodes\":")) != NULL) {
        sscanf(rt, "\"root_total_nodes\":%d", &root_nodes);
    }
    if (stn) {
        sscanf(stn, "scoped_total_nodes: %d", &scoped_nodes);
    } else if ((stn = strstr(inner_scoped, "\"scoped_total_nodes\":")) != NULL) {
        sscanf(stn, "\"scoped_total_nodes\":%d", &scoped_nodes);
    }
    ASSERT_TRUE(root_nodes > scoped_nodes);
    ASSERT_TRUE(scoped_nodes > 0);

    free(inner_scoped);
    free(resp_scoped);
    free(inner_root);
    free(resp_root);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_query_graph_missing_query) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":23,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"query_graph\","
                                   "\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    /* Should return error about missing query */
    ASSERT_NOT_NULL(strstr(resp, "required"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PIPELINE-DEPENDENT TOOL HANDLERS
 * ══════════════════════════════════════════════════════════════════ */

TEST(tool_index_repository_missing_path) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"index_repository\","
                                   "\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "required"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_get_code_snippet_missing_qn) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"get_code_snippet\","
                                   "\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "required"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_get_code_snippet_not_found) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"get_code_snippet\","
                                   "\"arguments\":{\"qualified_name\":\"nonexistent.func\","
                                   "\"project\":\"nonexistent\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "not found"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_search_code_missing_pattern) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":33,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "required"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* #1511 (distilled from @lukiod's #1512): search_code echoed a negative limit
 * back as the result count — "results: -5" — which an agent reads as an answer,
 * not as a rejected argument. Both halves matter: the schema declares the bound
 * so well-behaved clients never send it, and the handler clamps because a
 * schema is a request to the client, never a guarantee to the server. */
TEST(tool_search_code_negative_limit_is_not_echoed_issue1511) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":35,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{\"pattern\":\"func main\","
                                   "\"project\":\"nonexistent\",\"limit\":-5}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NULL(strstr(resp, "results: -5"));
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_search_code_limit_declares_a_minimum_issue1511) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":36,\"method\":\"tools/list\",\"params\":{}}");
    ASSERT_NOT_NULL(resp);

    yyjson_doc *doc = yyjson_read(resp, strlen(resp), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *result = root ? yyjson_obj_get(root, "result") : NULL;
    yyjson_val *tools = result ? yyjson_obj_get(result, "tools") : NULL;
    yyjson_val *minimum = NULL;
    if (tools && yyjson_is_arr(tools)) {
        size_t index, max;
        yyjson_val *tool;
        yyjson_arr_foreach(tools, index, max, tool) {
            yyjson_val *name = yyjson_obj_get(tool, "name");
            if (!name || !yyjson_is_str(name) || strcmp(yyjson_get_str(name), "search_code") != 0) {
                continue;
            }
            yyjson_val *schema = yyjson_obj_get(tool, "inputSchema");
            yyjson_val *props = schema ? yyjson_obj_get(schema, "properties") : NULL;
            yyjson_val *limit = props ? yyjson_obj_get(props, "limit") : NULL;
            minimum = limit ? yyjson_obj_get(limit, "minimum") : NULL;
            break;
        }
    }
    bool declared = minimum && yyjson_is_int(minimum) && yyjson_get_int(minimum) >= 1;
    yyjson_doc_free(doc);
    free(resp);
    cbm_mcp_server_free(srv);

    ASSERT_TRUE(declared);
    PASS();
}

TEST(tool_search_code_no_project) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":34,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{\"pattern\":\"func main\","
                                   "\"project\":\"nonexistent\"}}}");
    ASSERT_NOT_NULL(resp);
    /* No project indexed → error */
    ASSERT_TRUE(strstr(resp, "not found") || strstr(resp, "not indexed") ||
                strstr(resp, "required"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(search_code_multi_word) {
    char tmp[512];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* Multi-word query "HandleRequest error" — should find the line
     * "func HandleRequest() error {" via regex conversion. */
    char req[512];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":90,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_code\","
             "\"arguments\":{\"pattern\":\"HandleRequest error\","
             "\"project\":\"test-project\"}}}");

    char *resp = cbm_mcp_server_handle(srv, req);
    ASSERT_NOT_NULL(resp);
    /* Should find at least one result (not zero) */
    ASSERT_TRUE(strstr(resp, "HandleRequest") != NULL);
    /* Should NOT contain an error about "not found" */
    ASSERT_TRUE(strstr(resp, "\"isError\":true") == NULL);
    free(resp);

    cleanup_snippet_dir(tmp);
    cbm_mcp_server_free(srv);
    PASS();
}

static cbm_mcp_server_t *setup_search_contract_server(char *tmp, size_t tmp_size,
                                                      const char *project) {
    snprintf(tmp, tmp_size, "/tmp/cbm_search_contract_XXXXXX");
    if (!cbm_mkdtemp(tmp)) {
        return NULL;
    }
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv) {
        cbm_rmdir(tmp);
        return NULL;
    }
    cbm_mcp_server_set_project(srv, project);
    if (cbm_store_upsert_project(cbm_mcp_server_store(srv), project, tmp) != CBM_STORE_OK) {
        cbm_mcp_server_free(srv);
        cbm_rmdir(tmp);
        return NULL;
    }
    return srv;
}

static char *call_search_code_json(cbm_mcp_server_t *srv, const char *project,
                                   const char *arguments_tail) {
    char req[2048];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":991,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_code\",\"arguments\":{"
             "\"pattern\":\"needle\",\"project\":\"%s\",\"format\":\"json\"%s}}}",
             project, arguments_tail ? arguments_tail : "");
    char *response = cbm_mcp_server_handle(srv, req);
    char *inner = extract_text_content(response);
    free(response);
    return inner;
}

TEST(search_code_rejects_limit_above_public_maximum) {
    char tmp[512];
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), "search-limit-max");
    ASSERT_NOT_NULL(srv);
    char *response = cbm_mcp_handle_tool(
        srv, "search_code",
        "{\"pattern\":\"needle\",\"project\":\"search-limit-max\",\"limit\":101}");
    ASSERT_NOT_NULL(response);
    ASSERT_NOT_NULL(strstr(response, "limit must be an integer from 1 to 100"));
    ASSERT_NOT_NULL(strstr(response, "\"isError\":true"));
    free(response);
    cbm_mcp_server_free(srv);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_rejects_truncated_grep_command_construction) {
    char tmp[512];
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), "search-command-max");
    ASSERT_NOT_NULL(srv);
    char *oversized = malloc(6000);
    ASSERT_NOT_NULL(oversized);
    int prefix = snprintf(oversized, 6000,
                          "{\"pattern\":\"needle\",\"project\":\"search-command-max\","
                          "\"file_pattern\":\"");
    ASSERT_TRUE(prefix > 0);
    memset(oversized + prefix, 'x', 5000);
    snprintf(oversized + prefix + 5000, 6000 - (size_t)prefix - 5000, "\"}");
    char *response = cbm_mcp_handle_tool(srv, "search_code", oversized);
    ASSERT_NOT_NULL(response);
    ASSERT_NOT_NULL(strstr(response, "grep command exceeds safe size"));
    ASSERT_NOT_NULL(strstr(response, "\"isError\":true"));
    free(response);
    free(oversized);
    cbm_mcp_server_free(srv);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_full_response_is_byte_bounded_with_exact_population) {
    char tmp[512];
    const char *project = "search-byte-bound";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    char path[640];
    snprintf(path, sizeof(path), "%s/many.c", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    for (int i = 0; i < 100; i++) {
        fprintf(fp, "int hit_%03d(void) { /* needle ", i);
        for (int j = 0; j < 900; j++) {
            fputc('x', fp);
        }
        fputs(" */ return 1; }\n", fp);
        char name[32];
        char qn[96];
        snprintf(name, sizeof(name), "hit_%03d", i);
        snprintf(qn, sizeof(qn), "%s.many.%s", project, name);
        cbm_node_t node = {.project = project,
                           .label = "Function",
                           .name = name,
                           .qualified_name = qn,
                           .file_path = "many.c",
                           .start_line = i + 1,
                           .end_line = i + 1};
        ASSERT_GT(cbm_store_upsert_node(store, &node), 0);
    }
    ASSERT_EQ(fclose(fp), 0);

    char *raw = cbm_mcp_handle_tool(
        srv, "search_code",
        "{\"pattern\":\"needle\",\"project\":\"search-byte-bound\",\"mode\":\"full\","
        "\"format\":\"json\",\"limit\":100}");
    ASSERT_NOT_NULL(raw);
    ASSERT_TRUE(strlen(raw) <= 65536);
    char *inner = extract_text_content(raw);
    yyjson_doc *doc = inner ? yyjson_read(inner, strlen(inner), 0) : NULL;
    ASSERT_NOT_NULL(doc);
    yyjson_val *summary = yyjson_obj_get(yyjson_doc_get_root(doc), "summary");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "raw_matches")), 100);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "symbols")), 100);
    ASSERT_TRUE(yyjson_get_int(yyjson_obj_get(summary, "returned")) < 100);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "has_more")));
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "response_byte_truncated")));
    yyjson_doc_free(doc);
    free(inner);
    free(raw);
    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

/* Regression: the old collector stopped after 500 records, so a structurally
 * exact high-value declaration at record 521 was invisible and every reported
 * total described only the prefix sample. */
TEST(search_code_complete_over_500_and_globally_ranks_late_declaration) {
    char tmp[512];
    const char *project = "search-over-500";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);

    char path[640];
    snprintf(path, sizeof(path), "%s/bulk.c", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    for (int i = 0; i < 520; i++) {
        fprintf(fp, "int noise_%d = needle;\n", i);
    }
    fputs("int needleWinner(void) { return needle; }\n", fp);
    fclose(fp);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    cbm_node_t noise = {.project = project,
                        .label = "Function",
                        .name = "noiseBucket",
                        .qualified_name = "search-over-500.bulk.noiseBucket",
                        .file_path = "bulk.c",
                        .start_line = 1,
                        .end_line = 520};
    cbm_node_t winner = {.project = project,
                         .label = "Function",
                         .name = "needleWinner",
                         .qualified_name = "search-over-500.bulk.needleWinner",
                         .file_path = "bulk.c",
                         .start_line = 521,
                         .end_line = 521};
    ASSERT_GT(cbm_store_upsert_node(st, &noise), 0);
    ASSERT_GT(cbm_store_upsert_node(st, &winner), 0);

    char *inner = call_search_code_json(srv, project, ",\"limit\":1");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *summary = yyjson_obj_get(root, "summary");
    ASSERT_TRUE(yyjson_is_obj(summary));
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "scan_complete")));
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "raw_matches")), 521);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "symbols")), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "files")), 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "returned")), 1);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "has_more")));
    yyjson_val *rows = yyjson_obj_get(root, "rows");
    ASSERT_EQ(yyjson_arr_size(rows), 1);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 0), 0)),
                  "search-over-500.bulk.needleWinner");

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_exact_counts_below_500_and_compact_evidence) {
    char tmp[512];
    const char *project = "search-small-exact";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    char path[640];
    snprintf(path, sizeof(path), "%s/small.c", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    fputs("before context\nneedle alpha\nneedle beta\nafter context\n", fp);
    fclose(fp);
    cbm_node_t node = {.project = project,
                       .label = "Function",
                       .name = "small",
                       .qualified_name = "search-small-exact.small.small",
                       .file_path = "small.c",
                       .start_line = 1,
                       .end_line = 4};
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);

    char *inner = call_search_code_json(srv, project, ",\"context\":1,\"limit\":1");
    ASSERT_NOT_NULL(inner);
    ASSERT_LT(strlen(inner), 4096);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *summary = yyjson_obj_get(root, "summary");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "raw_matches")), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "symbols")), 1);
    yyjson_val *cols = yyjson_obj_get(root, "cols");
    ASSERT_EQ(yyjson_arr_size(cols), 7);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(cols, 0)), "qn");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(cols, 4)), "matches");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(cols, 5)), "line");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(cols, 6)), "excerpt");
    yyjson_val *row = yyjson_arr_get(yyjson_obj_get(root, "rows"), 0);
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(row, 4)), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(row, 5)), 2);
    const char *excerpt = yyjson_get_str(yyjson_arr_get(row, 6));
    ASSERT_NOT_NULL(strstr(excerpt, "before context"));
    ASSERT_NOT_NULL(strstr(excerpt, "needle alpha"));
    ASSERT_NOT_NULL(strstr(excerpt, "needle beta"));
    ASSERT_NULL(strstr(inner, "\"in\""));
    ASSERT_NULL(strstr(inner, "\"out\""));

    yyjson_doc_free(doc);
    free(inner);

    char req[1024];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":992,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_code\",\"arguments\":{\"pattern\":\"needle\","
             "\"project\":\"%s\",\"context\":1,\"limit\":1}}}",
             project);
    char *response = cbm_mcp_server_handle(srv, req);
    char *tree = extract_text_content(response);
    ASSERT_NOT_NULL(tree);
    ASSERT_TRUE(strncmp(tree, "summary:", 8) == 0);
    ASSERT_NOT_NULL(strstr(tree, "qn"));
    ASSERT_NOT_NULL(strstr(tree, "excerpt"));
    ASSERT_NOT_NULL(strstr(tree, "search-small-exact.small.small"));
    ASSERT_NULL(strstr(tree, ",in,out"));
    ASSERT_LT(strlen(tree), 4096);
    free(tree);
    free(response);

    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_ranks_source_then_test_then_generated_with_deterministic_ties) {
    char tmp[512];
    const char *project = "search-ranking";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    const char *rels[] = {"src/z.c", "generated/a.c", "tests/a_test.c", "src/a.c"};
    const char *qns[] = {"search-ranking.src.z", "search-ranking.generated.a",
                         "search-ranking.tests.a", "search-ranking.src.a"};
    char dirs[3][640];
    snprintf(dirs[0], sizeof(dirs[0]), "%s/src", tmp);
    snprintf(dirs[1], sizeof(dirs[1]), "%s/generated", tmp);
    snprintf(dirs[2], sizeof(dirs[2]), "%s/tests", tmp);
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(cbm_mkdir(dirs[i]), 0);
    }
    char paths[4][640];
    for (int i = 0; i < 4; i++) {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", tmp, rels[i]);
        FILE *fp = cbm_fopen(paths[i], "wb");
        ASSERT_NOT_NULL(fp);
        fputs("int unrelated(void) { return needle; }\n", fp);
        fclose(fp);
        cbm_node_t node = {.project = project,
                           .label = "Function",
                           .name = "unrelated",
                           .qualified_name = qns[i],
                           .file_path = rels[i],
                           .start_line = 1,
                           .end_line = 1};
        ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);
    }
    char *inner = call_search_code_json(srv, project, ",\"limit\":4");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *rows = yyjson_obj_get(yyjson_doc_get_root(doc), "rows");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 0), 2)), "src/a.c");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 1), 2)), "src/z.c");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 2), 2)), "tests/a_test.c");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 3), 2)), "generated/a.c");

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    for (int i = 0; i < 4; i++) {
        cbm_unlink(paths[i]);
    }
    for (int i = 0; i < 3; i++) {
        cbm_rmdir(dirs[i]);
    }
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_files_mode_aggregates_complete_matches_directly_by_file) {
    char tmp[512];
    const char *project = "search-files-direct";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    char paths[2][640];
    for (int i = 0; i < 2; i++) {
        snprintf(paths[i], sizeof(paths[i]), "%s/%c.txt", tmp, i == 0 ? 'a' : 'b');
        FILE *fp = cbm_fopen(paths[i], "wb");
        ASSERT_NOT_NULL(fp);
        fputs("needle\n", fp);
        if (i == 1) {
            fputs("needle\nneedle\n", fp);
        }
        fclose(fp);
        cbm_node_t file_node = {.project = project,
                                .label = "File",
                                .name = i == 0 ? "a.txt" : "b.txt",
                                .qualified_name =
                                    i == 0 ? "search-files-direct.a" : "search-files-direct.b",
                                .file_path = i == 0 ? "a.txt" : "b.txt",
                                .start_line = 1,
                                .end_line = i == 0 ? 1 : 3};
        ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &file_node), 0);
    }
    char *inner = call_search_code_json(srv, project, ",\"mode\":\"files\",\"limit\":2");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *summary = yyjson_obj_get(root, "summary");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "raw_matches")), 4);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "files")), 2);
    yyjson_val *rows = yyjson_obj_get(root, "rows");
    ASSERT_EQ(yyjson_arr_size(rows), 2);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 0), 0)), "b.txt");
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(yyjson_arr_get(rows, 0), 1)), 3);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 1), 0)), "a.txt");
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(yyjson_arr_get(rows, 1), 1)), 1);

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(paths[0]);
    cbm_unlink(paths[1]);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_equal_span_prefers_symbol_over_structural_file_node) {
    char tmp[512];
    const char *project = "search-equal-span";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);

    char path[640];
    snprintf(path, sizeof(path), "%s/one.c", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    fputs("int needle(void) { return 1; }\n", fp);
    fclose(fp);

    /* Deliberately insert the structural node first. The store query has no
     * ordering contract, so search_code itself must resolve an equal-span tie
     * by semantic specificity rather than row/insertion order. */
    cbm_node_t file = {.project = project,
                       .label = "File",
                       .name = "one.c",
                       .qualified_name = "search-equal-span.one",
                       .file_path = "one.c",
                       .start_line = 1,
                       .end_line = 1};
    cbm_node_t function = {.project = project,
                           .label = "Function",
                           .name = "needle",
                           .qualified_name = "search-equal-span.one.needle",
                           .file_path = "one.c",
                           .start_line = 1,
                           .end_line = 1};
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &file), 0);
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &function), 0);

    char *inner = call_search_code_json(srv, project, ",\"limit\":1");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *summary = yyjson_obj_get(root, "summary");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "raw_matches")), 1);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "symbols")), 1);
    yyjson_val *rows = yyjson_obj_get(root, "rows");
    ASSERT_EQ(yyjson_arr_size(rows), 1);
    yyjson_val *row = yyjson_arr_get(rows, 0);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(row, 0)), "search-equal-span.one.needle");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(row, 1)), "Function");

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_rollups_are_exact_over_unreturned_population) {
    char tmp[512];
    const char *project = "search-rollups-exact";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);

    char dirs[10][640];
    char paths[10][704];
    for (int i = 0; i < 10; i++) {
        snprintf(dirs[i], sizeof(dirs[i]), "%s/area%02d", tmp, i);
        ASSERT_EQ(cbm_mkdir(dirs[i]), 0);
        snprintf(paths[i], sizeof(paths[i]), "%s/file.c", dirs[i]);
        FILE *fp = cbm_fopen(paths[i], "wb");
        ASSERT_NOT_NULL(fp);
        for (int line = 0; line <= i; line++) {
            fputs("needle\n", fp);
        }
        fclose(fp);
        char rel[64];
        char qn[128];
        snprintf(rel, sizeof(rel), "area%02d/file.c", i);
        snprintf(qn, sizeof(qn), "search-rollups-exact.area%02d.file", i);
        cbm_node_t node = {.project = project,
                           .label = "Function",
                           .name = "file",
                           .qualified_name = qn,
                           .file_path = rel,
                           .start_line = 1,
                           .end_line = i + 1};
        ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);
    }

    char *inner = call_search_code_json(srv, project, ",\"limit\":1");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *summary = yyjson_obj_get(root, "summary");
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "raw_matches")), 55);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "symbols")), 10);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "files")), 10);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "returned")), 1);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "has_more")));

    yyjson_val *category_rows = yyjson_obj_get(yyjson_obj_get(root, "categories"), "rows");
    ASSERT_EQ(yyjson_arr_size(category_rows), 4);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(category_rows, 0), 0)), "source");
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(yyjson_arr_get(category_rows, 0), 1)), 55);

    yyjson_val *area_rows = yyjson_obj_get(yyjson_obj_get(root, "areas"), "rows");
    ASSERT_EQ(yyjson_arr_size(area_rows), 9);
    for (int i = 0; i < 8; i++) {
        char expected_area[16];
        snprintf(expected_area, sizeof(expected_area), "area%02d", 9 - i);
        yyjson_val *row = yyjson_arr_get(area_rows, (size_t)i);
        ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(row, 0)), expected_area);
        ASSERT_EQ(yyjson_get_int(yyjson_arr_get(row, 1)), 10 - i);
    }
    yyjson_val *other = yyjson_arr_get(area_rows, 8);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(other, 0)), "other");
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(other, 1)), 3);

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    for (int i = 0; i < 10; i++) {
        cbm_unlink(paths[i]);
        cbm_rmdir(dirs[i]);
    }
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_full_mode_source_is_match_centered_and_bounded) {
    char tmp[512];
    const char *project = "search-full-bounded";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);

    char path[640];
    snprintf(path, sizeof(path), "%s/long.c", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    for (int line = 1; line <= 100; line++) {
        fprintf(fp, "row-%04d%s\n", line, line == 30 ? " needle" : "");
    }
    fclose(fp);
    cbm_node_t node = {.project = project,
                       .label = "Function",
                       .name = "long_function",
                       .qualified_name = "search-full-bounded.long_function",
                       .file_path = "long.c",
                       .start_line = 1,
                       .end_line = 100};
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);

    char *inner = call_search_code_json(srv, project, ",\"mode\":\"full\",\"limit\":1");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *cols = yyjson_obj_get(root, "cols");
    ASSERT_EQ(yyjson_arr_size(cols), 8);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(cols, 7)), "source");
    yyjson_val *row = yyjson_arr_get(yyjson_obj_get(root, "rows"), 0);
    yyjson_val *source_cell = yyjson_arr_get(row, 7);
    ASSERT_TRUE(yyjson_is_obj(source_cell));
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(source_cell, "source_truncated")));
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(source_cell, "source_start")), 25);
    const char *source = yyjson_get_str(yyjson_obj_get(source_cell, "source"));
    ASSERT_NOT_NULL(source);
    ASSERT_NOT_NULL(strstr(source, "row-0030 needle\n"));
    ASSERT_NULL(strstr(source, "row-0024\n"));
    ASSERT_NULL(strstr(source, "row-0085\n"));
    int source_lines = 0;
    for (const char *cursor = source; *cursor; cursor++) {
        source_lines += *cursor == '\n';
    }
    ASSERT_EQ(source_lines, 60);

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_complete_above_4096_keeps_exact_totals_and_late_winner) {
    char tmp[512];
    const char *project = "search-pathological";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    char paths[2][640];
    snprintf(paths[0], sizeof(paths[0]), "%s/pathological.c", tmp);
    snprintf(paths[1], sizeof(paths[1]), "%s/unseen.c", tmp);
    FILE *fp = cbm_fopen(paths[0], "wb");
    ASSERT_NOT_NULL(fp);
    for (int i = 0; i < 4096; i++) {
        fputs("needle\n", fp);
    }
    fclose(fp);
    fp = cbm_fopen(paths[1], "wb");
    ASSERT_NOT_NULL(fp);
    fputs("needle\n", fp);
    fclose(fp);
    cbm_node_t node = {.project = project,
                       .label = "Function",
                       .name = "pathological",
                       .qualified_name = "search-pathological.pathological",
                       .file_path = "pathological.c",
                       .start_line = 1,
                       .end_line = 4096};
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);
    cbm_node_t unseen = {.project = project,
                         .label = "Function",
                         .name = "needleWinner",
                         .qualified_name = "search-pathological.needleWinner",
                         .file_path = "unseen.c",
                         .start_line = 1,
                         .end_line = 1};
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &unseen), 0);
    char *inner = call_search_code_json(srv, project, ",\"limit\":1");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *summary = yyjson_obj_get(root, "summary");
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "scan_complete")));
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "raw_matches")), 4097);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "symbols")), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "files")), 2);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(summary, "returned")), 1);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "ranking_complete")));
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(summary, "has_more")));
    ASSERT_NULL(yyjson_obj_get(summary, "symbols_at_least"));
    ASSERT_NULL(yyjson_obj_get(summary, "files_at_least"));
    yyjson_val *rows = yyjson_obj_get(root, "rows");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 0), 0)),
                  "search-pathological.needleWinner");
    yyjson_val *categories = yyjson_obj_get(root, "categories");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_obj_get(categories, "cols"), 1)), "matches");
    ASSERT_NULL(yyjson_obj_get(root, "warnings"));
    ASSERT_LT(strlen(inner), 4096);

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(paths[0]);
    cbm_unlink(paths[1]);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_centrality_tiebreak_survives_more_than_2046_symbols) {
    char tmp[512];
    const char *project = "search-centrality-batch";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    char path[640];
    snprintf(path, sizeof(path), "%s/many.c", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    int64_t winner_id = 0;
    for (int i = 0; i < 2050; i++) {
        fputs("int unrelated = needle;\n", fp);
        char qn[128];
        snprintf(qn, sizeof(qn), "search-centrality-batch.many.node%04d", i);
        cbm_node_t node = {.project = project,
                           .label = "Function",
                           .name = "unrelated",
                           .qualified_name = qn,
                           .file_path = "many.c",
                           .start_line = i + 1,
                           .end_line = i + 1};
        int64_t id = cbm_store_upsert_node(store, &node);
        ASSERT_GT(id, 0);
        if (i == 2049) {
            winner_id = id;
        }
    }
    fclose(fp);
    cbm_node_t caller = {.project = project,
                         .label = "Function",
                         .name = "caller",
                         .qualified_name = "search-centrality-batch.caller",
                         .file_path = "caller.c",
                         .start_line = 1,
                         .end_line = 1};
    int64_t caller_id = cbm_store_upsert_node(store, &caller);
    ASSERT_GT(caller_id, 0);
    cbm_edge_t edge = {
        .project = project, .source_id = caller_id, .target_id = winner_id, .type = "CALLS"};
    ASSERT_GT(cbm_store_insert_edge(store, &edge), 0);

    char *inner = call_search_code_json(srv, project, ",\"limit\":1");
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *rows = yyjson_obj_get(yyjson_doc_get_root(doc), "rows");
    ASSERT_EQ(yyjson_arr_size(rows), 1);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 0), 0)),
                  "search-centrality-batch.many.node2049");

    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

/* A grep record can exceed the fixed parser buffer when the matching source
 * line is long. Continuation chunks must never be interpreted as fresh
 * "path:line:content" records, and files mode must honor its result limit. */
TEST(search_code_files_mode_long_lines_and_limit) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_srch_long_XXXXXX");
    if (!cbm_mkdtemp(tmp)) {
        FAIL("cbm_mkdtemp failed");
    }

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    const char *proj = "long-line-search";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, tmp);

    char paths[4][640];
    for (int i = 0; i < 4; i++) {
        snprintf(paths[i], sizeof(paths[i]), "%s/file%d.txt", tmp, i);
        FILE *fp = cbm_fopen(paths[i], "wb");
        ASSERT_NOT_NULL(fp);
        fputs("needle ", fp);
        for (int j = 0; j < CBM_SZ_2K + 256; j++) {
            fputc('x', fp);
        }
#ifdef _WIN32
        fputs("\tfabricated_path_fragment\t123\tpayload\n", fp);
#else
        fputs(":fabricated_path_fragment:123:payload\n", fp);
#endif
        fclose(fp);

        char qn[128];
        snprintf(qn, sizeof(qn), "long-line-search.file%d.placeholder", i);
        cbm_node_t n = {.project = proj,
                        .label = "Function",
                        .name = "placeholder",
                        .qualified_name = qn,
                        .file_path = paths[i] + strlen(tmp) + 1,
                        .start_line = 100,
                        .end_line = 100};
        ASSERT_GT(cbm_store_upsert_node(st, &n), 0);
    }

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":95,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{\"pattern\":\"needle\",\"mode\":\"files\","
                                   "\"limit\":2,\"project\":\"long-line-search\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *rows = yyjson_obj_get(yyjson_doc_get_root(doc), "rows");
    ASSERT_TRUE(yyjson_is_arr(rows));
    ASSERT_EQ(yyjson_arr_size(rows), 2);
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        const char *file = yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, i), 0));
        ASSERT_NOT_NULL(file);
        ASSERT_TRUE(strcmp(file, "file0.txt") == 0 || strcmp(file, "file1.txt") == 0 ||
                    strcmp(file, "file2.txt") == 0 || strcmp(file, "file3.txt") == 0);
    }

    yyjson_doc_free(doc);
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    for (int i = 0; i < 4; i++) {
        cbm_unlink(paths[i]);
    }
    cbm_rmdir(tmp);
    PASS();
}

/* A continuation chunk from one overlong grep record can itself contain the
 * platform delimiter and masquerade as another file record. */
TEST(search_code_long_line_continuation_is_not_file) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_srch_cont_XXXXXX");
    if (!cbm_mkdtemp(tmp)) {
        FAIL("cbm_mkdtemp failed");
    }

    char path[640];
    snprintf(path, sizeof(path), "%s/only.txt", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    fputs("needle ", fp);
    for (int j = 0; j < CBM_SZ_2K + 256; j++) {
        fputc('x', fp);
    }
#ifdef _WIN32
    fputs("\tfabricated_path_fragment\t123\tpayload\n", fp);
#else
    fputs(":fabricated_path_fragment:123:payload\n", fp);
#endif
    fclose(fp);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    const char *proj = "long-continuation-search";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, tmp);
    cbm_node_t n = {.project = proj,
                    .label = "Function",
                    .name = "placeholder",
                    .qualified_name = "long-continuation-search.only.placeholder",
                    .file_path = "only.txt",
                    .start_line = 100,
                    .end_line = 100};
    ASSERT_GT(cbm_store_upsert_node(st, &n), 0);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":96,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{\"pattern\":\"needle\",\"mode\":\"files\","
                                   "\"limit\":10,\"project\":\"long-continuation-search\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *rows = yyjson_obj_get(yyjson_doc_get_root(doc), "rows");
    ASSERT_TRUE(yyjson_is_arr(rows));
    ASSERT_EQ(yyjson_arr_size(rows), 1);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 0), 0)), "only.txt");

    yyjson_doc_free(doc);
    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_rejects_non_positive_limit) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    const int invalid_limits[] = {0, -1};
    for (size_t i = 0; i < sizeof(invalid_limits) / sizeof(invalid_limits[0]); i++) {
        char request[512];
        snprintf(request, sizeof(request),
                 "{\"jsonrpc\":\"2.0\",\"id\":97,\"method\":\"tools/call\",\"params\":{"
                 "\"name\":\"search_code\",\"arguments\":{\"pattern\":\"needle\","
                 "\"project\":\"unused\",\"limit\":%d}}}",
                 invalid_limits[i]);
        char *resp = cbm_mcp_server_handle(srv, request);
        ASSERT_NOT_NULL(resp);
        ASSERT_NOT_NULL(strstr(resp, "\"isError\":true"));
        ASSERT_NOT_NULL(strstr(resp, "limit must be an integer from 1 to 100"));
        free(resp);
    }

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(search_code_rejects_empty_and_multiline_patterns) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    const char *patterns[] = {"", "first\\nsecond", "first\\rsecond"};
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        char request[768];
        snprintf(request, sizeof(request),
                 "{\"jsonrpc\":\"2.0\",\"id\":758,\"method\":\"tools/call\",\"params\":{"
                 "\"name\":\"search_code\",\"arguments\":{\"pattern\":\"%s\","
                 "\"project\":\"unused\"}}}",
                 patterns[i]);
        char *response = cbm_mcp_server_handle(srv, request);
        ASSERT_NOT_NULL(response);
        yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
        ASSERT_NOT_NULL(doc);
        yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
        ASSERT_TRUE(yyjson_is_obj(result));
        ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(result, "isError")));
        yyjson_val *content = yyjson_obj_get(result, "content");
        yyjson_val *first = yyjson_arr_get(content, 0);
        ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(first, "text")),
                      "pattern must be a non-empty single line");
        yyjson_doc_free(doc);
        free(response);
    }

    cbm_mcp_server_free(srv);
    PASS();
}

/* Reproduce-first (#687): scoped content search over a repo whose ROOT PATH
 * contains a space. write_scoped_filelist emits "<root>/<file>" records that the
 * Unix pipeline pipes to grep via xargs. With plain `xargs` (newline-split) the
 * space splits one path into several bogus args -> grep finds nothing ->
 * total_grep_matches == 0 (RED on the unfixed code). The fix writes NUL-separated
 * records + uses `xargs -0`, so the path stays a single argument -> match found
 * (GREEN). On Windows the scoped path uses PowerShell Get-Content -LiteralPath,
 * which already handles spaces, so this asserts correct behavior there too. */
TEST(search_code_scoped_path_with_spaces_issue687) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_srch_space_XXXXXX");
    if (!cbm_mkdtemp(tmp)) {
        FAIL("cbm_mkdtemp failed");
    }

    /* Project root deliberately contains a space. */
    char proj_dir[640];
    snprintf(proj_dir, sizeof(proj_dir), "%s/my project", tmp);
    cbm_mkdir(proj_dir);

    char src_path[768];
    snprintf(src_path, sizeof(src_path), "%s/main.go", proj_dir);
    FILE *fp = fopen(src_path, "w");
    if (!fp) {
        rmdir(proj_dir);
        rmdir(tmp);
        FAIL("cannot write source file under spaced path");
    }
    fprintf(fp, "package main\n\nfunc HandleRequest() error {\n\treturn nil\n}\n");
    fclose(fp);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    const char *proj = "space-search";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, proj_dir);

    /* A node so the file is "indexed" (cbm_store_list_files -> scoped grep path)
     * and the grep hit classifies to a result. */
    cbm_node_t n = {.project = proj,
                    .label = "Function",
                    .name = "HandleRequest",
                    .qualified_name = "space-search.main.HandleRequest",
                    .file_path = "main.go",
                    .start_line = 3,
                    .end_line = 5};
    ASSERT_GT(cbm_store_upsert_node(st, &n), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":94,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_code\","
             "\"arguments\":{\"pattern\":\"HandleRequest\",\"project\":\"space-search\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* grep must have found the match despite the space in the root path. */
    int grep_matches = -1;
    const char *g = strstr(inner, "raw_matches=");
    if (g) {
        sscanf(g, "raw_matches=%d", &grep_matches);
    }
    ASSERT_TRUE(grep_matches > 0);

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    unlink(src_path);
    rmdir(proj_dir);
    rmdir(tmp);
    PASS();
}

#ifdef _WIN32
/* Issue #903 follow-up: scoped search_code on Windows writes a UTF-8 filelist
 * containing absolute source paths, then reads it back through PowerShell.
 * Windows PowerShell 5.1 treats UTF-8 without BOM as ANSI unless told
 * otherwise, so a non-ASCII project root can be mojibaked before
 * Select-String sees the LiteralPath. */
TEST(search_code_scoped_path_with_cjk_root_issue903) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s/cbm_srch_cjk_XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(tmp)) {
        FAIL("cbm_mkdtemp failed");
    }

    char proj_dir[640];
    snprintf(proj_dir, sizeof(proj_dir), "%s/%s", tmp,
             "\xE4\xB8\xAD\xE6\x96\x87\xE9\xA1\xB9\xE7\x9B\xAE");
    if (!cbm_mkdir_p(proj_dir, 0755)) {
        cbm_rmdir(tmp);
        FAIL("cannot create CJK project dir");
    }

    char src_path[768];
    snprintf(src_path, sizeof(src_path), "%s/main.go", proj_dir);
    FILE *fp = cbm_fopen(src_path, "wb");
    if (!fp) {
        cbm_rmdir(proj_dir);
        cbm_rmdir(tmp);
        FAIL("cannot write source file under CJK path");
    }
    fprintf(fp, "package main\n\nfunc HandleRequest() error {\n\treturn nil\n}\n");
    fclose(fp);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    const char *proj = "cjk-search";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, proj_dir);

    cbm_node_t n = {.project = proj,
                    .label = "Function",
                    .name = "HandleRequest",
                    .qualified_name = "cjk-search.main.HandleRequest",
                    .file_path = "main.go",
                    .start_line = 3,
                    .end_line = 5};
    ASSERT_GT(cbm_store_upsert_node(st, &n), 0);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":903,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_code\","
             "\"arguments\":{\"pattern\":\"HandleRequest\",\"project\":\"cjk-search\"}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    int grep_matches = -1;
    const char *g = strstr(inner, "raw_matches=");
    if (g) {
        sscanf(g, "raw_matches=%d", &grep_matches);
    }
    ASSERT_TRUE(grep_matches > 0);

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    cbm_unlink(src_path);
    cbm_rmdir(proj_dir);
    cbm_rmdir(tmp);
    PASS();
}
#endif

/* Shared fixture for the path_filter prefilter tests (PR #756 distilled):
 * a project with two indexed files that both contain the search pattern —
 * src/handler.go (inside the filter) and vendor/other.go (outside it). */
static cbm_mcp_server_t *setup_prefilter_server(char *tmp, size_t tmp_sz, char *src_path,
                                                size_t src_sz, char *vendor_path,
                                                size_t vendor_sz) {
    snprintf(tmp, tmp_sz, "/tmp/cbm_srch_pref_XXXXXX");
    if (!cbm_mkdtemp(tmp)) {
        return NULL;
    }
    char dir[640];
    snprintf(dir, sizeof(dir), "%s/src", tmp);
    cbm_mkdir(dir);
    snprintf(dir, sizeof(dir), "%s/vendor", tmp);
    cbm_mkdir(dir);

    snprintf(src_path, src_sz, "%s/src/handler.go", tmp);
    snprintf(vendor_path, vendor_sz, "%s/vendor/other.go", tmp);
    FILE *fp = fopen(src_path, "w");
    if (!fp) {
        return NULL;
    }
    fprintf(fp, "package main\n\nfunc HandleRequest() error {\n\treturn nil\n}\n");
    fclose(fp);
    fp = fopen(vendor_path, "w");
    if (!fp) {
        return NULL;
    }
    fprintf(fp, "package vendored\n\nfunc HandleRequest() error {\n\treturn nil\n}\n");
    fclose(fp);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv) {
        return NULL;
    }
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj = "prefilter-search";
    cbm_mcp_server_set_project(srv, proj);
    cbm_store_upsert_project(st, proj, tmp);

    cbm_node_t n1 = {.project = proj,
                     .label = "Function",
                     .name = "HandleRequest",
                     .qualified_name = "prefilter-search.main.HandleRequest",
                     .file_path = "src/handler.go",
                     .start_line = 3,
                     .end_line = 5};
    cbm_node_t n2 = {.project = proj,
                     .label = "Function",
                     .name = "HandleRequest",
                     .qualified_name = "prefilter-search.vendored.HandleRequest",
                     .file_path = "vendor/other.go",
                     .start_line = 3,
                     .end_line = 5};
    if (cbm_store_upsert_node(st, &n1) <= 0 || cbm_store_upsert_node(st, &n2) <= 0) {
        cbm_mcp_server_free(srv);
        return NULL;
    }
    return srv;
}

static void cleanup_prefilter_dir(const char *tmp, const char *src_path, const char *vendor_path) {
    char dir[640];
    unlink(src_path);
    unlink(vendor_path);
    snprintf(dir, sizeof(dir), "%s/src", tmp);
    rmdir(dir);
    snprintf(dir, sizeof(dir), "%s/vendor", tmp);
    rmdir(dir);
    rmdir(tmp);
}

/* PR #756 (distilled): scoped search_code prefilters the indexed filelist by
 * path_filter before grep runs. POSITIVE invariant guard: a path_filter that
 * matches the file containing the hit must still return that hit (guards
 * against over-filtering — the prefilter predicate must stay IDENTICAL to the
 * post-grep filter in collect_grep_matches), and files outside the filter
 * stay excluded. Green on pre-prefilter main too (the post-grep filter alone
 * produced the same results): the change is results-preserving perf-only. */
TEST(search_code_path_filter_prefilter_keeps_matches) {
    char tmp[512], src_path[768], vendor_path[768];
    cbm_mcp_server_t *srv = setup_prefilter_server(tmp, sizeof(tmp), src_path, sizeof(src_path),
                                                   vendor_path, sizeof(vendor_path));
    ASSERT_NOT_NULL(srv);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":95,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_code\","
             "\"arguments\":{\"pattern\":\"HandleRequest\",\"project\":\"prefilter-search\","
             "\"path_filter\":\"^src/\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "\"isError\":true") == NULL);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    /* The in-filter hit is returned; the out-of-filter file is not. */
    ASSERT_NOT_NULL(strstr(inner, "src/handler.go"));
    ASSERT_TRUE(strstr(inner, "vendor/other.go") == NULL);

    /* Exactly the one in-filter grep match survives (same count before and
     * after the prefilter — predicate identity). */
    int grep_matches = -1;
    const char *g = strstr(inner, "raw_matches=");
    if (g) {
        sscanf(g, "raw_matches=%d", &grep_matches);
    }
    ASSERT_EQ(grep_matches, 1);

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    cleanup_prefilter_dir(tmp, src_path, vendor_path);
    PASS();
}

/* PR #756 (distilled): path_filter matching ZERO indexed files. With the
 * prefilter the scoped filelist has 0 records, and handle_search_code now
 * skips the grep subprocess entirely (xargs on an empty filelist is
 * platform-dependent: GNU execs grep once with no operands, BSD skips) and
 * returns the empty result directly. Must be a clean zero-result response —
 * no error. Green on pre-prefilter main too (there the full filelist is
 * grepped and the post-grep filter drops every hit — an empty filelist is
 * unreachable on main): guards the edge the prefilter introduces. */
TEST(search_code_path_filter_matches_nothing) {
    char tmp[512], src_path[768], vendor_path[768];
    cbm_mcp_server_t *srv = setup_prefilter_server(tmp, sizeof(tmp), src_path, sizeof(src_path),
                                                   vendor_path, sizeof(vendor_path));
    ASSERT_NOT_NULL(srv);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":96,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_code\","
             "\"arguments\":{\"pattern\":\"HandleRequest\",\"project\":\"prefilter-search\","
             "\"path_filter\":\"^no_such_dir/\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "\"isError\":true") == NULL);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);

    int grep_matches = -1;
    const char *g = strstr(inner, "raw_matches=");
    if (g) {
        sscanf(g, "raw_matches=%d", &grep_matches);
    }
    ASSERT_EQ(grep_matches, 0);
    int returned = -1;
    const char *r = strstr(inner, "returned=");
    if (r) {
        sscanf(r, "returned=%d", &returned);
    }
    ASSERT_EQ(returned, 0);
    ASSERT_TRUE(strstr(inner, "handler.go") == NULL);
    ASSERT_TRUE(strstr(inner, "other.go") == NULL);

    free(inner);
    free(resp);
    cbm_mcp_server_free(srv);
    cleanup_prefilter_dir(tmp, src_path, vendor_path);
    PASS();
}

TEST(search_code_invalid_path_filter_is_an_explicit_error) {
    char tmp[512];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *response =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":756,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\",\"arguments\":{"
                                   "\"pattern\":\"HandleRequest\",\"project\":\"test-project\","
                                   "\"path_filter\":\"[unclosed\"}}}");
    ASSERT_NOT_NULL(response);
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    ASSERT_TRUE(yyjson_is_obj(result));
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(result, "isError")));
    yyjson_val *content = yyjson_obj_get(result, "content");
    ASSERT_TRUE(yyjson_is_arr(content));
    yyjson_val *first = yyjson_arr_get(content, 0);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(first, "text")), "invalid path_filter regex");

    yyjson_doc_free(doc);
    free(response);
    cleanup_snippet_dir(tmp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* issue #283: search_code with regex=true and a syntactically invalid pattern
 * must return an explicit error, not an empty result indistinguishable from a
 * legitimate no-match. */
TEST(search_code_invalid_regex_errors_issue283) {
    char tmp[512];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* Unclosed group under regex=true → must be flagged as an error. */
    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":91,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{\"pattern\":\"func(\",\"regex\":true,"
                                   "\"project\":\"test-project\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"isError\":true"));
    ASSERT_NOT_NULL(strstr(resp, "invalid regex"));
    free(resp);

    /* Same pattern as a literal (regex=false) must NOT error. */
    resp = cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":92,\"method\":\"tools/call\","
                                      "\"params\":{\"name\":\"search_code\","
                                      "\"arguments\":{\"pattern\":\"func(\",\"regex\":false,"
                                      "\"project\":\"test-project\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "invalid regex") == NULL);
    free(resp);

    cleanup_snippet_dir(tmp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* issue #282: a literal '|' under regex=false is a silent 0-match trap. It must
 * now be surfaced as a warning (and the result carries elapsed_ms). */
TEST(search_code_literal_pipe_warns_issue282) {
    char tmp[512];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":93,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{\"pattern\":\"HandleRequest|Nope\","
                                   "\"regex\":false,\"project\":\"test-project\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "warning"));    /* surfaced, not silent */
    ASSERT_NOT_NULL(strstr(resp, "regex=true")); /* the hint names the fix */
    ASSERT_NOT_NULL(strstr(resp, "elapsed_ms")); /* timing is reported */
    free(resp);

    cleanup_snippet_dir(tmp);
    cbm_mcp_server_free(srv);
    PASS();
}

/* issue #272: '&' in a path / file_pattern is neutralised by the command's
 * quoting and must no longer be rejected as "invalid characters". */
TEST(search_code_ampersand_accepted_issue272) {
    char tmp[512];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":94,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"search_code\","
                                   "\"arguments\":{\"pattern\":\"HandleRequest\","
                                   "\"file_pattern\":\"*R&D*.go\",\"project\":\"test-project\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "invalid characters") == NULL);
    free(resp);

    cleanup_snippet_dir(tmp);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(search_code_preserves_valid_utf8_and_replaces_only_invalid_sequences) {
    char tmp[512];
    const char *project = "search-utf8";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    char path[640];
    snprintf(path, sizeof(path), "%s/utf8.c", tmp);
    FILE *fp = cbm_fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    const unsigned char line[] = "const char *needle = \"\xE4\xB8\xAD\xE6\x96\x87";
    ASSERT_EQ(fwrite(line, 1, sizeof(line) - 1, fp), sizeof(line) - 1);
    ASSERT_EQ(fputc(0xC0, fp), 0xC0);
    ASSERT_TRUE(fputs("\";\n", fp) >= 0);
    ASSERT_EQ(fclose(fp), 0);
    cbm_node_t node = {.project = project,
                       .label = "Variable",
                       .name = "needle",
                       .qualified_name = "search-utf8.utf8.needle",
                       .file_path = "utf8.c",
                       .start_line = 1,
                       .end_line = 1};
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);

    char *inner = call_search_code_json(srv, project, ",\"mode\":\"full\",\"limit\":1");
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "\xE4\xB8\xAD\xE6\x96\x87"));
    ASSERT_NOT_NULL(strstr(inner, "\xEF\xBF\xBD"));
    ASSERT_NULL(strstr(inner, "??????"));
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(path);
    cbm_rmdir(tmp);
    PASS();
}

TEST(search_code_ordinary_segments_ending_in_gen_remain_source) {
    char tmp[512];
    const char *project = "search-segment-classification";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    const char *dirs[] = {"src", "src/oxygen", "generated"};
    char dir_paths[3][640];
    for (int i = 0; i < 3; i++) {
        snprintf(dir_paths[i], sizeof(dir_paths[i]), "%s/%s", tmp, dirs[i]);
        ASSERT_EQ(cbm_mkdir(dir_paths[i]), 0);
    }
    const char *rels[] = {"src/oxygen/a.c", "generated/b.c"};
    char paths[2][640];
    for (int i = 0; i < 2; i++) {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", tmp, rels[i]);
        FILE *fp = cbm_fopen(paths[i], "wb");
        ASSERT_NOT_NULL(fp);
        fputs("int needle;\n", fp);
        ASSERT_EQ(fclose(fp), 0);
        cbm_node_t node = {.project = project,
                           .label = "Variable",
                           .name = "needle",
                           .qualified_name = i == 0 ? "search-segment.oxygen.needle"
                                                    : "search-segment.generated.needle",
                           .file_path = rels[i],
                           .start_line = 1,
                           .end_line = 1};
        ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);
    }
    char *inner = call_search_code_json(srv, project, ",\"limit\":2");
    yyjson_doc *doc = inner ? yyjson_read(inner, strlen(inner), 0) : NULL;
    ASSERT_NOT_NULL(doc);
    yyjson_val *categories = yyjson_obj_get(yyjson_doc_get_root(doc), "categories");
    yyjson_val *rows = yyjson_obj_get(categories, "rows");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 0), 0)), "source");
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(yyjson_arr_get(rows, 0), 1)), 1);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_arr_get(yyjson_arr_get(rows, 2), 0)), "generated");
    ASSERT_EQ(yyjson_get_int(yyjson_arr_get(yyjson_arr_get(rows, 2), 1)), 1);
    yyjson_doc_free(doc);
    free(inner);
    cbm_mcp_server_free(srv);
    cbm_unlink(paths[0]);
    cbm_unlink(paths[1]);
    cbm_rmdir(dir_paths[1]);
    cbm_rmdir(dir_paths[2]);
    cbm_rmdir(dir_paths[0]);
    cbm_rmdir(tmp);
    PASS();
}

extern bool cbm_search_grep_exit_status_accepted(int status, int *exit_code);

TEST(search_code_grep_exit_status_distinguishes_match_no_match_and_failure) {
    int exit_code = -1;
#ifdef _WIN32
    ASSERT_TRUE(cbm_search_grep_exit_status_accepted(0, &exit_code));
    ASSERT_EQ(exit_code, 0);
    ASSERT_TRUE(cbm_search_grep_exit_status_accepted(1, &exit_code));
    ASSERT_EQ(exit_code, 1);
    ASSERT_FALSE(cbm_search_grep_exit_status_accepted(2, &exit_code));
#else
    ASSERT_TRUE(cbm_search_grep_exit_status_accepted(0 << 8, &exit_code));
    ASSERT_EQ(exit_code, 0);
    ASSERT_TRUE(cbm_search_grep_exit_status_accepted(1 << 8, &exit_code));
    ASSERT_EQ(exit_code, 1);
    ASSERT_FALSE(cbm_search_grep_exit_status_accepted(2 << 8, &exit_code));
#endif
    ASSERT_EQ(exit_code, 2);
    PASS();
}

TEST(search_code_revalidates_indexed_path_after_symlink_replacement) {
#ifdef _WIN32
    PASS();
#else
    char tmp[512];
    const char *project = "search-symlink-revalidation";
    cbm_mcp_server_t *srv = setup_search_contract_server(tmp, sizeof(tmp), project);
    ASSERT_NOT_NULL(srv);
    char indexed[640];
    char parked[640];
    char outside[640];
    snprintf(indexed, sizeof(indexed), "%s/indexed.c", tmp);
    snprintf(parked, sizeof(parked), "%s/indexed.original", tmp);
    snprintf(outside, sizeof(outside), "%s/cbm-search-outside-XXXXXX", cbm_tmpdir());
    int outside_fd = cbm_mkstemp(outside);
    ASSERT_TRUE(outside_fd >= 0);
    ASSERT_EQ(close(outside_fd), 0);
    FILE *fp = cbm_fopen(indexed, "wb");
    ASSERT_NOT_NULL(fp);
    fputs("int harmless;\n", fp);
    ASSERT_EQ(fclose(fp), 0);
    fp = cbm_fopen(outside, "wb");
    ASSERT_NOT_NULL(fp);
    fputs("outside_secret_needle\n", fp);
    ASSERT_EQ(fclose(fp), 0);
    cbm_node_t node = {.project = project,
                       .label = "Variable",
                       .name = "harmless",
                       .qualified_name = "search-symlink-revalidation.indexed.harmless",
                       .file_path = "indexed.c",
                       .start_line = 1,
                       .end_line = 1};
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(srv), &node), 0);
    ASSERT_EQ(rename(indexed, parked), 0);
    ASSERT_EQ(symlink(outside, indexed), 0);
    char *raw = cbm_mcp_handle_tool(
        srv, "search_code",
        "{\"pattern\":\"outside_secret_needle\",\"project\":\"search-symlink-revalidation\","
        "\"format\":\"json\"}");
    ASSERT_NOT_NULL(raw);
    ASSERT_NULL(strstr(raw, "outside_secret_needle"));
    free(raw);
    ASSERT_EQ(cbm_unlink(indexed), 0);
    ASSERT_EQ(rename(parked, indexed), 0);
    cbm_mcp_server_free(srv);
    cbm_unlink(outside);
    cbm_unlink(indexed);
    cbm_rmdir(tmp);
    PASS();
#endif
}

TEST(tool_detect_changes_no_project) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":35,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"detect_changes\","
                                   "\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "missing required argument: project"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_manage_adr_no_project) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":36,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"manage_adr\","
                                   "\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "missing required argument: project"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* Regression test for use-after-free in handle_manage_adr (get path).
 * MUST FAIL before fix: free(buf) is called before yy_doc_to_str serializes doc,
 * so result field is missing or contains garbage. MUST PASS after fix. */
TEST(tool_manage_adr_get_with_existing_adr) {
    /* Create a temp directory with .codebase-memory/adr.md */
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-adr-test-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS(); /* skip if mkdtemp fails */
    }

    char adr_dir[512];
    snprintf(adr_dir, sizeof(adr_dir), "%s/.codebase-memory", tmp_dir);
    cbm_mkdir(adr_dir);

    char adr_path[512];
    snprintf(adr_path, sizeof(adr_path), "%s/adr.md", adr_dir);
    FILE *fp = fopen(adr_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("## PURPOSE\nTest ADR content for regression test.\n\n"
          "## STACK\nC, SQLite.\n\n"
          "## ARCHITECTURE\nMCP server.\n",
          fp);
    fclose(fp);

    /* Create server and register the project */
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    cbm_store_upsert_project(st, "test-adr-uaf", tmp_dir);
    cbm_mcp_server_set_project(srv, "test-adr-uaf");

    /* Call manage_adr via full JSON-RPC path to exercise cbm_jsonrpc_format_response.
     * The bug: free(buf) before yy_doc_to_str causes garbage JSON; format_response
     * then fails to parse the result and omits the "result" field entirely. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"manage_adr\","
             "\"arguments\":{\"project\":\"test-adr-uaf\",\"mode\":\"get\"}}}");
    ASSERT_NOT_NULL(resp);
    /* JSON-RPC response must include a "result" field (absent when use-after-free) */
    ASSERT_NOT_NULL(strstr(resp, "\"result\""));
    /* ADR content must appear in response */
    ASSERT_NOT_NULL(strstr(resp, "PURPOSE"));
    /* Must not be an error */
    ASSERT_NULL(strstr(resp, "\"isError\":true"));
    free(resp);

    /* Clean up */
    cbm_mcp_server_free(srv);
    remove(adr_path);
    rmdir(adr_dir);
    rmdir(tmp_dir);
    PASS();
}

/* issue #256: manage_adr (MCP) and the UI /api/adr endpoints must share ONE
 * backend. A manage_adr(update) write must be readable via cbm_store_adr_get
 * (the exact API the UI's /api/adr GET uses). */
TEST(tool_manage_adr_unified_backend_issue256) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    cbm_store_upsert_project(st, "adr-unify", "/tmp/adr-unify");
    cbm_mcp_server_set_project(srv, "adr-unify");

    /* Write via the MCP tool. */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":120,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"manage_adr\",\"arguments\":{\"project\":\"adr-unify\","
             "\"mode\":\"update\",\"content\":\"## PURPOSE\\nUnified ADR backend.\\n\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "updated"));
    free(resp);

    /* Read DIRECTLY via the store API the UI /api/adr uses — must see it. */
    cbm_adr_t adr;
    memset(&adr, 0, sizeof(adr));
    ASSERT_EQ(cbm_store_adr_get(st, "adr-unify", &adr), CBM_STORE_OK);
    ASSERT_NOT_NULL(adr.content);
    ASSERT_NOT_NULL(strstr(adr.content, "Unified ADR backend."));
    cbm_store_adr_free(&adr);

    /* And manage_adr(get) round-trips the same content. */
    resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":121,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"manage_adr\",\"arguments\":{\"project\":\"adr-unify\","
             "\"mode\":\"get\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "Unified ADR backend."));
    ASSERT_NULL(strstr(resp, "\"isError\":true"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_manage_adr_rejects_removed_sections_argument) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    ASSERT_EQ(cbm_store_upsert_project(st, "adr-sections-guard", "/tmp/adr-sections-guard"),
              CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, "adr-sections-guard");
    ASSERT_EQ(cbm_store_adr_store(st, "adr-sections-guard", "## PURPOSE\nOriginal ADR.\n"),
              CBM_STORE_OK);

    mcp_mutation_guard_probe_t probe = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":122,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"manage_adr\",\"arguments\":{"
             "\"project\":\"adr-sections-guard\",\"mode\":\"update\","
             "\"sections\":[\"PURPOSE\"],\"content\":\"## PURPOSE\\nReplacement ADR.\\n\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "invalid_arguments"));
    ASSERT_NOT_NULL(strstr(resp, "No ADR write was performed"));
    ASSERT_NOT_NULL(strstr(resp, "\"isError\":true"));
    free(resp);
    ASSERT_EQ(probe.begin_count, 0);
    ASSERT_EQ(probe.end_count, 0);

    cbm_adr_t adr;
    memset(&adr, 0, sizeof(adr));
    ASSERT_EQ(cbm_store_adr_get(st, "adr-sections-guard", &adr), CBM_STORE_OK);
    ASSERT_STR_EQ(adr.content, "## PURPOSE\nOriginal ADR.\n");
    cbm_store_adr_free(&adr);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_manage_adr_mutation_guard_balances_success) {
    const char *project = "guard-adr-success";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/guard-adr-success"), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    mcp_mutation_guard_probe_t probe = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char *resp = cbm_mcp_handle_tool(srv, "manage_adr",
                                     "{\"project\":\"guard-adr-success\",\"mode\":\"update\","
                                     "\"content\":\"## PURPOSE\\nGuarded ADR.\\n\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "updated"));
    ASSERT_EQ(probe.begin_count, 1);
    ASSERT_EQ(probe.end_count, 1);
    ASSERT_STR_EQ(probe.begin_projects[0], project);
    ASSERT_STR_EQ(probe.end_projects[0], project);
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* ADR reads must not wait behind the same project's mutation lease. A reindex
 * can be expensive; existing SQLite data is a stable query snapshot, so get
 * and sections must not invoke the blocking guard. */
TEST(tool_manage_adr_read_paths_skip_blocking_mutation_guard) {
    const char *project = "guard-adr-read";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, "/tmp/guard-adr-read"), CBM_STORE_OK);
    ASSERT_EQ(
        cbm_store_adr_store(store, project, "## PURPOSE\nNonblocking read.\n\n## STACK\nC.\n"),
        CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    mcp_mutation_guard_probe_t probe = {.deny_begin_call = 1};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char *get_response =
        cbm_mcp_handle_tool(srv, "manage_adr", "{\"project\":\"guard-adr-read\",\"mode\":\"get\"}");
    char *sections_response = cbm_mcp_handle_tool(
        srv, "manage_adr", "{\"project\":\"guard-adr-read\",\"mode\":\"sections\"}");
    bool get_returned_adr = get_response && strstr(get_response, "Nonblocking read.") &&
                            !strstr(get_response, "\"isError\":true");
    bool sections_returned_adr = sections_response && strstr(sections_response, "## PURPOSE") &&
                                 strstr(sections_response, "## STACK") &&
                                 !strstr(sections_response, "\"isError\":true");

    free(get_response);
    free(sections_response);
    cbm_mcp_server_free(srv);

    ASSERT_TRUE(get_returned_adr);
    ASSERT_TRUE(sections_returned_adr);
    ASSERT_EQ(probe.begin_count, 0);
    ASSERT_EQ(probe.end_count, 0);
    PASS();
}

TEST(tool_manage_adr_read_missing_store_skips_mutation_guard) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-mcp-adr-guard-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS();
    }

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-adr-missing";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t probe = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char *resp = cbm_mcp_handle_tool(srv, "manage_adr",
                                     "{\"project\":\"guard-adr-missing\",\"mode\":\"get\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "not found") || strstr(resp, "not indexed"));
    ASSERT_EQ(probe.begin_count, 0);
    ASSERT_EQ(probe.end_count, 0);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    cbm_rmdir(cache);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    PASS();
}

TEST(tool_manage_adr_legacy_migration_tries_without_blocking) {
    const char *project = "guard-adr-legacy";
    char root[256];
    char cache[256];
    snprintf(root, sizeof(root), "%s/cbm-adr-legacy-XXXXXX", cbm_tmpdir());
    snprintf(cache, sizeof(cache), "%s/cbm-adr-legacy-cache-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(root));
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    ASSERT_EQ(cbm_setenv("CBM_CACHE_DIR", cache, 1), 0);

    char adr_dir[CBM_SZ_1K];
    char adr_path[CBM_SZ_1K];
    snprintf(adr_dir, sizeof(adr_dir), "%s/.codebase-memory", root);
    snprintf(adr_path, sizeof(adr_path), "%s/adr.md", adr_dir);
    ASSERT_EQ(cbm_mkdir(adr_dir), 0);
    FILE *fp = cbm_fopen(adr_path, "w");
    ASSERT_NOT_NULL(fp);
    ASSERT_TRUE(fputs("## PURPOSE\nLegacy ADR.\n", fp) >= 0);
    ASSERT_EQ(fclose(fp), 0);

    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    cbm_store_t *writer = cbm_store_open_path(db_path);
    ASSERT_NOT_NULL(writer);
    ASSERT_EQ(cbm_store_upsert_project(writer, project, root), CBM_STORE_OK);
    cbm_store_close(writer);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    mcp_mutation_guard_probe_t probe = {.deny_try_begin_call = 1};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);
    cbm_mcp_server_set_project_mutation_try_guard(srv, mcp_mutation_guard_probe_try_begin);

    char *busy_response = cbm_mcp_handle_tool(
        srv, "manage_adr", "{\"project\":\"guard-adr-legacy\",\"mode\":\"get\"}");
    char *migrated_response = cbm_mcp_handle_tool(
        srv, "manage_adr", "{\"project\":\"guard-adr-legacy\",\"mode\":\"get\"}");
    /* A successful migration invalidates the request-scoped query store; prove
     * persistence through the next public read instead of retaining its former
     * borrowed test handle. */
    char *persisted_response = cbm_mcp_handle_tool(
        srv, "manage_adr", "{\"project\":\"guard-adr-legacy\",\"mode\":\"get\"}");
    bool busy_read_returned_legacy = busy_response && strstr(busy_response, "Legacy ADR.") &&
                                     !strstr(busy_response, "\"isError\":true");
    bool migrated_read_returned_legacy = migrated_response &&
                                         strstr(migrated_response, "Legacy ADR.") &&
                                         !strstr(migrated_response, "\"isError\":true");
    bool migration_persisted = persisted_response && strstr(persisted_response, "Legacy ADR.") &&
                               !strstr(persisted_response, "\"isError\":true");

    free(busy_response);
    free(migrated_response);
    free(persisted_response);
    cbm_mcp_server_free(srv);
    cbm_unlink(adr_path);
    cbm_rmdir(adr_dir);
    cbm_rmdir(root);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(busy_read_returned_legacy);
    ASSERT_TRUE(migrated_read_returned_legacy);
    ASSERT_TRUE(migration_persisted);
    ASSERT_EQ(probe.begin_count, 0);
    ASSERT_EQ(probe.try_begin_count, 2);
    ASSERT_EQ(probe.end_count, 1);
    ASSERT_STR_EQ(probe.try_begin_projects[0], project);
    ASSERT_STR_EQ(probe.try_begin_projects[1], project);
    ASSERT_STR_EQ(probe.end_projects[0], project);
    PASS();
}

/* A raw cbm_mcp_handle_tool() call is still one request lifetime. Cancellation
 * published from inside a non-pipeline handler must therefore be accepted,
 * observed before the write, and retired at completion so the next raw request
 * on the same server is not poisoned. */
TEST(tool_raw_dispatch_cancel_is_scoped_non_mutating_and_next_request_clean) {
    const char *project = "raw-cancel-adr";
    char root[256];
    snprintf(root, sizeof(root), "%s/cbm-mcp-raw-adr-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(root));
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(cbm_store_upsert_project(store, project, root), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);

    mcp_mutation_guard_probe_t probe = {
        .cancel_on_begin_call = 1,
        .cancel_server = srv,
    };
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char *cancelled_response =
        cbm_mcp_handle_tool(srv, "manage_adr",
                            "{\"project\":\"raw-cancel-adr\",\"mode\":\"update\","
                            "\"content\":\"## PURPOSE\\nMUST NOT COMMIT.\\n\"}");
    bool cancellation_reported = cancelled_response && strstr(cancelled_response, "cancelled") &&
                                 strstr(cancelled_response, "\"isError\":true");

    cbm_adr_t cancelled_adr = {0};
    int cancelled_lookup = cbm_store_adr_get(store, project, &cancelled_adr);
    if (cancelled_lookup == CBM_STORE_OK) {
        cbm_store_adr_free(&cancelled_adr);
    }

    char *next_response =
        cbm_mcp_handle_tool(srv, "manage_adr",
                            "{\"project\":\"raw-cancel-adr\",\"mode\":\"update\","
                            "\"content\":\"## PURPOSE\\nClean next request.\\n\"}");
    bool next_response_clean = next_response && strstr(next_response, "updated") &&
                               !strstr(next_response, "cancelled") &&
                               !strstr(next_response, "\"isError\":true");
    cbm_adr_t next_adr = {0};
    int next_lookup = cbm_store_adr_get(store, project, &next_adr);
    bool next_write_committed = next_lookup == CBM_STORE_OK && next_adr.content &&
                                strstr(next_adr.content, "Clean next request") &&
                                !strstr(next_adr.content, "MUST NOT COMMIT");
    if (next_lookup == CBM_STORE_OK) {
        cbm_store_adr_free(&next_adr);
    }

    free(cancelled_response);
    free(next_response);
    cbm_mcp_server_free(srv);
    (void)cbm_rmdir(root);

    ASSERT_TRUE(probe.cancel_attempted);
    ASSERT_TRUE(probe.cancel_accepted);
    ASSERT_TRUE(cancellation_reported);
    ASSERT_EQ(cancelled_lookup, CBM_STORE_NOT_FOUND);
    ASSERT_TRUE(next_response_clean);
    ASSERT_TRUE(next_write_committed);
    ASSERT_EQ(probe.begin_count, 2);
    ASSERT_EQ(probe.end_count, 2);
    ASSERT_STR_EQ(probe.begin_projects[0], project);
    ASSERT_STR_EQ(probe.end_projects[0], project);
    ASSERT_STR_EQ(probe.begin_projects[1], project);
    ASSERT_STR_EQ(probe.end_projects[1], project);
    PASS();
}

/* The daemon publishes its transport request before entering MCP dispatch. A
 * disconnect in that narrow interval must remain latched through the nested
 * raw tool scope instead of being erased at dispatch entry. */
TEST(tool_outer_request_scope_preserves_predispatch_cancel) {
    const char *project = "outer-scope-cancel-adr";
    char root[256];
    (void)snprintf(root, sizeof(root), "%s/cbm-mcp-outer-cancel-XXXXXX", cbm_tmpdir());
    bool root_created = cbm_mkdtemp(root) != NULL;
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    bool project_ready =
        root_created && store && cbm_store_upsert_project(store, project, root) == CBM_STORE_OK;
    cbm_mcp_server_set_project(srv, project);
    bool outer_scope = project_ready && cbm_mcp_server_request_scope_begin(srv);
    bool cancel_accepted = outer_scope && cbm_mcp_server_cancel_active(srv);
    char *cancelled_response =
        cancel_accepted
            ? cbm_mcp_handle_tool(srv, "manage_adr",
                                  "{\"project\":\"outer-scope-cancel-adr\","
                                  "\"mode\":\"update\",\"content\":\"MUST NOT COMMIT\"}")
            : NULL;
    bool cancellation_reported = cancelled_response && strstr(cancelled_response, "cancelled") &&
                                 strstr(cancelled_response, "\"isError\":true");
    cbm_mcp_server_request_scope_end(srv);

    char *next_response = srv ? cbm_mcp_handle_tool(srv, "ingest_traces", "{\"traces\":[]}") : NULL;
    bool next_response_clean = next_response && strstr(next_response, "accepted") &&
                               !strstr(next_response, "cancelled") &&
                               !strstr(next_response, "\"isError\":true");

    free(cancelled_response);
    free(next_response);
    cbm_mcp_server_free(srv);
    (void)cbm_rmdir(root);

    ASSERT_TRUE(root_created);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(project_ready);
    ASSERT_TRUE(outer_scope);
    ASSERT_TRUE(cancel_accepted);
    ASSERT_TRUE(cancellation_reported);
    ASSERT_TRUE(next_response_clean);
    PASS();
}

/* Publish cancellation from the local index mutation guard: the request scope
 * must already be active, and the cancellation must either stop before
 * pipeline admission or remain set through pipeline binding. No project DB may
 * be published, and the following request must start with a clean token. */
TEST(tool_index_repository_early_raw_cancel_survives_index_entry) {
    char cache[256];
    char repo[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-raw-index-cache-XXXXXX", cbm_tmpdir());
    snprintf(repo, sizeof(repo), "%s/cbm-mcp-raw-index-repo-XXXXXX", cbm_tmpdir());
    bool cache_created = cbm_mkdtemp(cache) != NULL;
    bool repo_created = cbm_mkdtemp(repo) != NULL;

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    if (cache_created) {
        cbm_setenv("CBM_CACHE_DIR", cache, 1);
    }

    char *project = repo_created ? cbm_project_name_from_path(repo) : NULL;
    cbm_mcp_server_t *srv =
        cache_created && repo_created && project ? cbm_mcp_server_new(NULL) : NULL;
    mcp_mutation_guard_probe_t probe = {
        .cancel_on_begin_call = 1,
        .cancel_server = srv,
    };
    if (srv) {
        cbm_mcp_server_set_background_tasks(srv, false);
        cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                                  mcp_mutation_guard_probe_end, &probe);
    }

    char args[CBM_SZ_1K];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", repo);
    char *cancelled_response = srv ? cbm_mcp_handle_tool(srv, "index_repository", args) : NULL;
    bool cancellation_reported = cancelled_response && strstr(cancelled_response, "cancelled") &&
                                 strstr(cancelled_response, "\"isError\":true");

    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project ? project : "missing-project");
    bool no_project_published = !cbm_file_exists(db_path);

    char *next_response = srv ? cbm_mcp_handle_tool(srv, "ingest_traces", "{\"traces\":[]}") : NULL;
    bool next_response_clean = next_response && strstr(next_response, "accepted") &&
                               !strstr(next_response, "cancelled") &&
                               !strstr(next_response, "\"isError\":true");

    free(cancelled_response);
    free(next_response);
    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    if (cache_created) {
        (void)cbm_rmdir(cache);
    }
    if (repo_created) {
        (void)cbm_rmdir(repo);
    }
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    free(project);

    ASSERT_TRUE(cache_created);
    ASSERT_TRUE(repo_created);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(probe.cancel_attempted);
    ASSERT_TRUE(probe.cancel_accepted);
    ASSERT_TRUE(cancellation_reported);
    ASSERT_EQ(probe.begin_count, 1);
    ASSERT_EQ(probe.end_count, 1);
    ASSERT_TRUE(no_project_published);
    ASSERT_TRUE(next_response_clean);
    PASS();
}

static bool mcp_cross_repo_create_project_store(const char *cache, const char *project,
                                                const char *root_path) {
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    cbm_store_t *store = cbm_store_open_path(db_path);
    if (!store) {
        return false;
    }
    bool created = cbm_store_upsert_project(store, project, root_path) == CBM_STORE_OK;
    cbm_store_close(store);
    return created;
}

/* Seed exactly one HTTP route match without invoking the indexing pipeline.
 * This keeps the duplicate-target regression fast and makes a doubled result
 * count observable instead of relying on an empty (zero-edge) scan. */
static bool mcp_cross_repo_seed_http_match(const char *cache, const char *source_project,
                                           const char *target_project, const char *root_path) {
    char source_path[CBM_SZ_1K];
    char target_path[CBM_SZ_1K];
    snprintf(source_path, sizeof(source_path), "%s/%s.db", cache, source_project);
    snprintf(target_path, sizeof(target_path), "%s/%s.db", cache, target_project);

    cbm_store_t *source = cbm_store_open_path(source_path);
    cbm_store_t *target = cbm_store_open_path(target_path);
    if (!source || !target) {
        cbm_store_close(source);
        cbm_store_close(target);
        return false;
    }

    bool ok = cbm_store_upsert_project(source, source_project, root_path) == CBM_STORE_OK &&
              cbm_store_upsert_project(target, target_project, root_path) == CBM_STORE_OK;

    cbm_node_t caller = {.project = source_project,
                         .label = "Function",
                         .name = "call_once",
                         .qualified_name = "cross.source.call_once",
                         .file_path = "client.c",
                         .start_line = 1,
                         .end_line = 2};
    cbm_node_t local_route = {.project = source_project,
                              .label = "Route",
                              .name = "GET /dedupe",
                              .qualified_name = "__route__GET__/dedupe",
                              .file_path = "client.c",
                              .start_line = 3,
                              .end_line = 3};
    int64_t caller_id = ok ? cbm_store_upsert_node(source, &caller) : 0;
    int64_t local_route_id = ok ? cbm_store_upsert_node(source, &local_route) : 0;
    cbm_edge_t http_call = {.project = source_project,
                            .source_id = caller_id,
                            .target_id = local_route_id,
                            .type = "HTTP_CALLS",
                            .properties_json = "{\"url_path\":\"/dedupe\",\"method\":\"GET\"}"};
    ok = ok && caller_id > 0 && local_route_id > 0 && cbm_store_insert_edge(source, &http_call) > 0;

    cbm_node_t target_route = {.project = target_project,
                               .label = "Route",
                               .name = "GET /dedupe",
                               .qualified_name = "__route__GET__/dedupe",
                               .file_path = "server.c",
                               .start_line = 3,
                               .end_line = 3};
    cbm_node_t handler = {.project = target_project,
                          .label = "Function",
                          .name = "handle_once",
                          .qualified_name = "cross.target.handle_once",
                          .file_path = "server.c",
                          .start_line = 1,
                          .end_line = 2};
    int64_t target_route_id = ok ? cbm_store_upsert_node(target, &target_route) : 0;
    int64_t handler_id = ok ? cbm_store_upsert_node(target, &handler) : 0;
    cbm_edge_t handles = {.project = target_project,
                          .source_id = handler_id,
                          .target_id = target_route_id,
                          .type = "HANDLES"};
    ok = ok && target_route_id > 0 && handler_id > 0 && cbm_store_insert_edge(target, &handles) > 0;

    cbm_store_close(source);
    cbm_store_close(target);
    return ok;
}

TEST(tool_cross_repo_mutation_guard_sorts_dedupes_and_unwinds) {
    char repo[256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-mcp-cross-guard-XXXXXX");
    if (!cbm_mkdtemp(repo)) {
        PASS();
    }

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(cbm_mcp_server_set_session_context(srv, repo, NULL));

    mcp_mutation_guard_probe_t probe = {.deny_begin_call = 3};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char args[CBM_SZ_2K];
    snprintf(args, sizeof(args),
             "{\"repo_path\":\"%s\",\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"zzz-target\",\"000-target\",\"zzz-target\"]}",
             repo);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "blocked"));

    /* The source plus two unique targets are acquired in lexical order. The
     * third acquisition is denied, so only the first two are unwound. */
    ASSERT_EQ(probe.begin_count, 3);
    ASSERT_TRUE(strcmp(probe.begin_projects[0], probe.begin_projects[1]) < 0);
    ASSERT_TRUE(strcmp(probe.begin_projects[1], probe.begin_projects[2]) < 0);
    int low_target_count = 0;
    int high_target_count = 0;
    for (int i = 0; i < probe.begin_count; i++) {
        low_target_count += strcmp(probe.begin_projects[i], "000-target") == 0;
        high_target_count += strcmp(probe.begin_projects[i], "zzz-target") == 0;
    }
    ASSERT_EQ(low_target_count, 1);
    ASSERT_EQ(high_target_count, 1);
    ASSERT_EQ(probe.end_count, 2);
    ASSERT_STR_EQ(probe.end_projects[0], probe.begin_projects[1]);
    ASSERT_STR_EQ(probe.end_projects[1], probe.begin_projects[0]);
    free(resp);

    cbm_mcp_server_free(srv);
    cbm_rmdir(repo);
    PASS();
}

static unsigned char mcp_test_ascii_casefold(unsigned char ch) {
    return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + ('a' - 'A')) : ch;
}

static bool mcp_test_project_keys_equivalent(const char *left, const char *right) {
    if (!left || !right) {
        return left == right;
    }
    while (*left && *right) {
        if (mcp_test_ascii_casefold((unsigned char)*left) !=
            mcp_test_ascii_casefold((unsigned char)*right)) {
            return false;
        }
        left++;
        right++;
    }
    return *left == *right;
}

/* Project-lock keys ASCII-fold A-Z, so case aliases must be one lease here too.
 * Otherwise Foo + foo self-deadlocks, and two requests whose raw strcmp order
 * differs can acquire the same OS locks in opposite (ABBA) order. Keep the
 * original spellings: folding is only the comparison key, not a lookup value. */
TEST(tool_cross_repo_mutation_guard_casefolds_aliases_and_order) {
    char repo[256];
    snprintf(repo, sizeof(repo), "/tmp/cbm-mcp-cross-case-guard-XXXXXX");
    if (!cbm_mkdtemp(repo)) {
        PASS();
    }

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(cbm_mcp_server_set_session_context(srv, repo, NULL));

    mcp_mutation_guard_probe_t first = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &first);
    char first_args[CBM_SZ_2K];
    snprintf(first_args, sizeof(first_args),
             "{\"repo_path\":\"%s\",\"name\":\"Zulu\","
             "\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"Foo\",\"foo\",\"Alpha\"]}",
             repo);
    char *first_resp = cbm_mcp_handle_tool(srv, "index_repository", first_args);
    ASSERT_NOT_NULL(first_resp);
    free(first_resp);

    mcp_mutation_guard_probe_t second = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &second);
    char second_args[CBM_SZ_2K];
    snprintf(second_args, sizeof(second_args),
             "{\"repo_path\":\"%s\",\"name\":\"zULU\","
             "\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"foo\",\"ALPHA\",\"FOO\"]}",
             repo);
    char *second_resp = cbm_mcp_handle_tool(srv, "index_repository", second_args);
    ASSERT_NOT_NULL(second_resp);
    free(second_resp);

    ASSERT_EQ(first.begin_count, 3);
    ASSERT_EQ(first.end_count, 3);
    ASSERT_EQ(second.begin_count, 3);
    ASSERT_EQ(second.end_count, 3);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(
            mcp_test_project_keys_equivalent(first.begin_projects[i], second.begin_projects[i]));
        ASSERT_TRUE(
            mcp_test_project_keys_equivalent(first.end_projects[i], first.begin_projects[2 - i]));
        ASSERT_TRUE(
            mcp_test_project_keys_equivalent(second.end_projects[i], second.begin_projects[2 - i]));
    }
    ASSERT_STR_EQ(first.begin_projects[0], "Alpha");
    ASSERT_STR_EQ(first.begin_projects[1], "Foo");
    ASSERT_STR_EQ(first.begin_projects[2], "Zulu");
    ASSERT_STR_EQ(second.begin_projects[0], "ALPHA");
    ASSERT_STR_EQ(second.begin_projects[1], "FOO");
    ASSERT_STR_EQ(second.begin_projects[2], "zULU");

    cbm_mcp_server_free(srv);
    cbm_rmdir(repo);
    PASS();
}

/* A wildcard means "all projects" and therefore cannot be combined with a
 * named target. Accepting the mixed form both obscures caller intent and lets
 * the cross-repo pass create/use a literal "*.db" target on POSIX. Validation
 * must happen before any project mutation lease is acquired. */
TEST(tool_cross_repo_rejects_wildcard_mixed_with_named_targets) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-cross-wildcard-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char *project = cbm_project_name_from_path(cache);
    ASSERT_NOT_NULL(project);
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(cbm_mcp_server_set_session_context(srv, cache, NULL));

    mcp_mutation_guard_probe_t probe = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char args[CBM_SZ_2K];
    snprintf(args, sizeof(args),
             "{\"repo_path\":\"%s\",\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"*\",\"named-target\"]}",
             cache);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    bool rejected = resp && strstr(resp, "\"isError\":true") != NULL;
    bool explained = resp && strstr(resp, "target_projects") && strstr(resp, "*") &&
                     (strstr(resp, "only") || strstr(resp, "combin"));
    int begin_count = probe.begin_count;
    int end_count = probe.end_count;

    free(resp);
    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    cleanup_project_db(cache, "*");
    cleanup_project_db(cache, "named-target");
    free(project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(rejected);
    ASSERT_TRUE(explained);
    ASSERT_EQ(begin_count, 0);
    ASSERT_EQ(end_count, 0);
    PASS();
}

/* Cancellation can arrive while the final mutation lease is being acquired.
 * The cross-repo operation must advertise itself through cancel_active(),
 * observe the pending cancellation before doing cross-project writes, and
 * unwind every lease it acquired. */
TEST(tool_cross_repo_checks_cancellation_after_acquiring_leases) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-cross-cancel-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char *project = cbm_project_name_from_path(cache);
    ASSERT_NOT_NULL(project);
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(cbm_mcp_server_set_session_context(srv, cache, NULL));

    mcp_mutation_guard_probe_t probe = {
        .cancel_on_begin_call = 3,
        .cancel_server = srv,
    };
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char args[CBM_SZ_2K];
    snprintf(args, sizeof(args),
             "{\"repo_path\":\"%s\",\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"000-cancel-target\",\"zzz-cancel-target\"]}",
             cache);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    bool response_cancelled = resp && strstr(resp, "cancelled") != NULL;
    bool cancel_attempted = probe.cancel_attempted;
    bool cancel_accepted = probe.cancel_accepted;
    int begin_count = probe.begin_count;
    int end_count = probe.end_count;
    bool reverse_unwind = begin_count == 3 && end_count == 3 &&
                          strcmp(probe.end_projects[0], probe.begin_projects[2]) == 0 &&
                          strcmp(probe.end_projects[1], probe.begin_projects[1]) == 0 &&
                          strcmp(probe.end_projects[2], probe.begin_projects[0]) == 0;

    free(resp);
    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    cleanup_project_db(cache, "000-cancel-target");
    cleanup_project_db(cache, "zzz-cancel-target");
    free(project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(cancel_attempted);
    ASSERT_TRUE(cancel_accepted);
    ASSERT_TRUE(response_cancelled);
    ASSERT_EQ(begin_count, 3);
    ASSERT_EQ(end_count, 3);
    ASSERT_TRUE(reverse_unwind);
    PASS();
}

/* cbm_store_open_path() creates its path. Cross-repo validation must therefore
 * reject an absent source or named target before the matcher opens either one;
 * otherwise a typo silently becomes a valid-looking empty project database. */
TEST(tool_cross_repo_missing_inputs_fail_without_creating_ghost_databases) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-cross-missing-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char *source_project = cbm_project_name_from_path(cache);
    ASSERT_NOT_NULL(source_project);
    const char *existing_target = "existing-cross-target";
    const char *missing_target = "missing-cross-target";
    ASSERT_TRUE(mcp_cross_repo_create_project_store(cache, existing_target, cache));

    char source_db_path[CBM_SZ_1K];
    char missing_target_db_path[CBM_SZ_1K];
    snprintf(source_db_path, sizeof(source_db_path), "%s/%s.db", cache, source_project);
    snprintf(missing_target_db_path, sizeof(missing_target_db_path), "%s/%s.db", cache,
             missing_target);
    ASSERT_FALSE(cbm_file_exists(source_db_path));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(cbm_mcp_server_set_session_context(srv, cache, NULL));

    char args[CBM_SZ_2K];
    snprintf(args, sizeof(args),
             "{\"repo_path\":\"%s\",\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"%s\"]}",
             cache, existing_target);
    char *source_resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    bool source_failed = source_resp && strstr(source_resp, "\"isError\":true");
    bool source_reported =
        source_resp && (strstr(source_resp, "not indexed") || strstr(source_resp, "not found") ||
                        strstr(source_resp, "missing"));
    bool source_ghost_created = cbm_file_exists(source_db_path);
    free(source_resp);

    cleanup_project_db(cache, source_project);
    ASSERT_TRUE(mcp_cross_repo_create_project_store(cache, source_project, cache));
    ASSERT_FALSE(cbm_file_exists(missing_target_db_path));

    snprintf(args, sizeof(args),
             "{\"repo_path\":\"%s\",\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"%s\"]}",
             cache, missing_target);
    char *target_resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    bool target_failed = target_resp && strstr(target_resp, "\"isError\":true");
    bool target_reported =
        target_resp && (strstr(target_resp, "not indexed") || strstr(target_resp, "not found") ||
                        strstr(target_resp, "missing"));
    bool target_ghost_created = cbm_file_exists(missing_target_db_path);
    free(target_resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, source_project);
    cleanup_project_db(cache, existing_target);
    cleanup_project_db(cache, missing_target);
    free(source_project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(source_failed);
    ASSERT_TRUE(source_reported);
    ASSERT_FALSE(source_ghost_created);
    ASSERT_TRUE(target_failed);
    ASSERT_TRUE(target_reported);
    ASSERT_FALSE(target_ghost_created);
    PASS();
}

/* Named targets are a set, not a work list. A duplicate must be leased,
 * scanned, and counted once; the fixture provides one real edge so the result
 * counters cannot pass vacuously at zero. */
TEST(tool_cross_repo_dedupes_targets_before_scanning_and_counting) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-cross-dedupe-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char *source_project = cbm_project_name_from_path(cache);
    ASSERT_NOT_NULL(source_project);
    const char *target_project = "cross-dedupe-target";
    ASSERT_TRUE(mcp_cross_repo_seed_http_match(cache, source_project, target_project, cache));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(cbm_mcp_server_set_session_context(srv, cache, NULL));

    char args[CBM_SZ_2K];
    snprintf(args, sizeof(args),
             "{\"repo_path\":\"%s\",\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"%s\",\"%s\"]}",
             cache, target_project, target_project);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    bool succeeded = resp && strstr(resp, "\"isError\":true") == NULL;
    bool scanned_once = response_contains_json_fragment(resp, "\"projects_scanned\":1");
    bool counted_once = response_contains_json_fragment(resp, "\"cross_http_calls\":1") &&
                        response_contains_json_fragment(resp, "\"total_cross_edges\":1");

    char source_db_path[CBM_SZ_1K];
    char target_db_path[CBM_SZ_1K];
    snprintf(source_db_path, sizeof(source_db_path), "%s/%s.db", cache, source_project);
    snprintf(target_db_path, sizeof(target_db_path), "%s/%s.db", cache, target_project);
    cbm_store_t *source = cbm_store_open_path_query(source_db_path);
    cbm_store_t *target = cbm_store_open_path_query(target_db_path);
    int source_cross_edges =
        source ? cbm_store_count_edges_by_type(source, source_project, "CROSS_HTTP_CALLS") : -1;
    int target_cross_edges =
        target ? cbm_store_count_edges_by_type(target, target_project, "CROSS_HTTP_CALLS") : -1;
    cbm_store_close(source);
    cbm_store_close(target);

    free(resp);
    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, source_project);
    cleanup_project_db(cache, target_project);
    free(source_project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(succeeded);
    ASSERT_TRUE(scanned_once);
    ASSERT_TRUE(counted_once);
    ASSERT_EQ(source_cross_edges, 1);
    ASSERT_EQ(target_cross_edges, 1);
    PASS();
}

/* `name` is the documented index project-name override and must identify the
 * cross-repo source too. Deriving from repo_path here makes custom-named
 * projects impossible to rescan even though ordinary indexing created them. */
TEST(tool_cross_repo_honors_source_name_override) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-cross-name-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *source_project = "cross-custom-source";
    const char *target_project = "cross-custom-target";
    ASSERT_TRUE(mcp_cross_repo_seed_http_match(cache, source_project, target_project, cache));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_TRUE(cbm_mcp_server_set_session_context(srv, cache, NULL));
    char args[CBM_SZ_2K];
    snprintf(args, sizeof(args),
             "{\"repo_path\":\"%s\",\"name\":\"%s\","
             "\"mode\":\"cross-repo-intelligence\","
             "\"target_projects\":[\"%s\"]}",
             cache, source_project, target_project);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    bool succeeded = resp && !response_contains_json_fragment(resp, "\"isError\":true") &&
                     response_contains_json_fragment(resp, "\"cross_http_calls\":1");

    free(resp);
    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, source_project);
    cleanup_project_db(cache, target_project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(succeeded);
    PASS();
}

/* Corrupt-store quarantine renames/unlinks the project DB and sidecars, so it
 * is a mutation even when resolve_store() was reached by a query tool. Generic
 * queries use a blocking guard for that recovery, while manage_adr reads must
 * use one nonblocking acquisition and never nest a blocking lease. */
TEST(tool_corrupt_store_cleanup_guard_is_balanced_and_not_nested) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-corrupt-guard-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-corrupt-project";
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);

    ASSERT_TRUE(mcp_make_corrupt_project_store(cache, project));
    cbm_mcp_server_t *query_srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(query_srv);
    mcp_mutation_guard_probe_t query_probe = {
        .observed_db_path = db_path,
    };
    cbm_mcp_server_set_project_mutation_guard(query_srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &query_probe);

    char *resp =
        cbm_mcp_handle_tool(query_srv, "search_graph",
                            "{\"project\":\"guard-corrupt-project\",\"name_pattern\":\".*\"}");
    free(resp);
    cbm_mcp_server_free(query_srv);
    char query_backup_path[CBM_SZ_1K];
    int query_backup_count =
        mcp_find_corrupt_backups(cache, project, query_backup_path, sizeof(query_backup_path));
    bool query_quarantined =
        !cbm_file_exists(db_path) && query_backup_count == 1 && query_backup_path[0] != '\0';

    /* Replant the same deterministic corruption to exercise manage_adr's
     * already-held lease independently from the query server above. */
    mcp_cleanup_corrupt_backups(cache, project);
    ASSERT_TRUE(mcp_make_corrupt_project_store(cache, project));
    cbm_mcp_server_t *adr_srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(adr_srv);
    mcp_mutation_guard_probe_t adr_probe = {
        .observed_db_path = db_path,
    };
    cbm_mcp_server_set_project_mutation_guard(adr_srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &adr_probe);
    cbm_mcp_server_set_project_mutation_try_guard(adr_srv, mcp_mutation_guard_probe_try_begin);
    resp = cbm_mcp_handle_tool(adr_srv, "manage_adr",
                               "{\"project\":\"guard-corrupt-project\",\"mode\":\"get\"}");
    free(resp);
    cbm_mcp_server_free(adr_srv);
    char adr_backup_path[CBM_SZ_1K];
    int adr_backup_count =
        mcp_find_corrupt_backups(cache, project, adr_backup_path, sizeof(adr_backup_path));
    bool adr_quarantined =
        !cbm_file_exists(db_path) && adr_backup_count == 1 && adr_backup_path[0] != '\0';

    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(query_quarantined);
    ASSERT_EQ(query_probe.begin_count, 1);
    ASSERT_EQ(query_probe.end_count, 1);
    ASSERT_STR_EQ(query_probe.begin_projects[0], project);
    ASSERT_STR_EQ(query_probe.end_projects[0], project);
    ASSERT_TRUE(query_probe.db_exists_at_begin);
    ASSERT_FALSE(query_probe.db_exists_at_end);
    ASSERT_TRUE(adr_quarantined);
    ASSERT_EQ(adr_probe.begin_count, 0);
    ASSERT_EQ(adr_probe.try_begin_count, 1);
    ASSERT_EQ(adr_probe.end_count, 1);
    ASSERT_STR_EQ(adr_probe.try_begin_projects[0], project);
    ASSERT_STR_EQ(adr_probe.end_projects[0], project);
    ASSERT_TRUE(adr_probe.db_exists_at_begin);
    ASSERT_FALSE(adr_probe.db_exists_at_end);
    PASS();
}

/* Integrity is checked before the lease is requested, but quarantine itself
 * must fail closed when that lease is denied. In particular, a rejected query
 * may not remove either a recoverable DB generation or its committed WAL. */
TEST(tool_corrupt_store_cleanup_guard_denial_preserves_db_and_wal) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-corrupt-denied-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-corrupt-denied";
    char db_path[CBM_SZ_1K];
    char wal_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    snprintf(wal_path, sizeof(wal_path), "%s-wal", db_path);
    cbm_store_t *writer = mcp_open_corrupt_project_store_with_wal(cache, project);
    ASSERT_NOT_NULL(writer);
    ASSERT_TRUE(cbm_file_exists(db_path));
    ASSERT_TRUE(cbm_file_exists(wal_path));

    long db_len = 0;
    long wal_len = 0;
    unsigned char *db_before = mcp_read_file_bytes(db_path, &db_len);
    unsigned char *wal_before = mcp_read_file_bytes(wal_path, &wal_len);
    ASSERT_NOT_NULL(db_before);
    ASSERT_NOT_NULL(wal_before);
    ASSERT_TRUE(db_len > 0);
    ASSERT_TRUE(wal_len > 0);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t probe = {.deny_begin_call = 1};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);
    char *resp = cbm_mcp_handle_tool(
        srv, "search_graph", "{\"project\":\"guard-corrupt-denied\",\"name_pattern\":\".*\"}");

    bool db_unchanged = mcp_file_matches_snapshot(db_path, db_before, db_len);
    bool wal_unchanged = mcp_file_matches_snapshot(wal_path, wal_before, wal_len);
    char unexpected_backup[CBM_SZ_1K];
    int backup_count =
        mcp_find_corrupt_backups(cache, project, unexpected_backup, sizeof(unexpected_backup));
    int artifact_count = mcp_count_corrupt_artifacts(cache, project);
    int begin_count = probe.begin_count;
    int end_count = probe.end_count;
    bool guarded_project = begin_count == 1 && strcmp(probe.begin_projects[0], project) == 0;

    free(resp);
    cbm_mcp_server_free(srv);
    free(db_before);
    free(wal_before);
    cbm_store_close(writer);
    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_EQ(begin_count, 1);
    ASSERT_EQ(end_count, 0);
    ASSERT_TRUE(guarded_project);
    ASSERT_TRUE(db_unchanged);
    ASSERT_TRUE(wal_unchanged);
    ASSERT_EQ(backup_count, 0);
    ASSERT_EQ(artifact_count, 0);
    PASS();
}

/* A read must not wait for corrupt-store recovery. When another process owns
 * that lease, distinguish the retryable busy state from an absent project. */
TEST(tool_manage_adr_corrupt_store_busy_is_retryable) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-adr-corrupt-busy-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-adr-corrupt-busy";
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    ASSERT_TRUE(mcp_make_corrupt_project_store(cache, project));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t probe = {.deny_try_begin_call = 1};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);
    cbm_mcp_server_set_project_mutation_try_guard(srv, mcp_mutation_guard_probe_try_begin);

    char *resp = cbm_mcp_handle_tool(srv, "manage_adr",
                                     "{\"project\":\"guard-adr-corrupt-busy\",\"mode\":\"get\"}");
    bool retryable_busy = resp && strstr(resp, "project is busy; retry after indexing") &&
                          response_contains_json_fragment(resp, "\"isError\":true");
    bool db_preserved = cbm_file_exists(db_path);
    char unexpected_backup[CBM_SZ_1K];
    int backup_count =
        mcp_find_corrupt_backups(cache, project, unexpected_backup, sizeof(unexpected_backup));

    free(resp);
    cbm_mcp_server_free(srv);
    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(retryable_busy);
    ASSERT_EQ(probe.begin_count, 0);
    ASSERT_EQ(probe.try_begin_count, 1);
    ASSERT_EQ(probe.end_count, 0);
    ASSERT_TRUE(db_preserved);
    ASSERT_EQ(backup_count, 0);
    PASS();
}

TEST(tool_manage_adr_corrupt_store_missing_try_guard_reports_configuration) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-adr-corrupt-config-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-adr-corrupt-config";
    char db_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    ASSERT_TRUE(mcp_make_corrupt_project_store(cache, project));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t probe = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);

    char *resp = cbm_mcp_handle_tool(srv, "manage_adr",
                                     "{\"project\":\"guard-adr-corrupt-config\",\"mode\":\"get\"}");
    bool missing_try_guard =
        resp && strstr(resp, "project recovery requires a nonblocking mutation guard") &&
        response_contains_json_fragment(resp, "\"isError\":true");
    bool db_preserved = cbm_file_exists(db_path);
    char unexpected_backup[CBM_SZ_1K];
    int backup_count =
        mcp_find_corrupt_backups(cache, project, unexpected_backup, sizeof(unexpected_backup));

    free(resp);
    cbm_mcp_server_free(srv);
    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(missing_try_guard);
    ASSERT_EQ(probe.begin_count, 0);
    ASSERT_EQ(probe.try_begin_count, 0);
    ASSERT_EQ(probe.end_count, 0);
    ASSERT_TRUE(db_preserved);
    ASSERT_EQ(backup_count, 0);
    PASS();
}

/* Another session may publish a good generation while this query waits for
 * the mutation lease. Cleanup must re-open and re-check the path after lease
 * acquisition; quarantining based on the stale pre-wait handle loses the new
 * generation and returns a false "not indexed" result. */
TEST(tool_corrupt_store_cleanup_rechecks_generation_after_guard_wait) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-corrupt-recheck-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-corrupt-recheck";
    const char *replacement_root = "/tmp/guard-corrupt-replacement";
    char db_path[CBM_SZ_1K];
    char replacement_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    snprintf(replacement_path, sizeof(replacement_path), "%s/%s.replacement.db", cache, project);
    ASSERT_TRUE(mcp_make_corrupt_project_store(cache, project));
    ASSERT_TRUE(mcp_make_valid_project_store_at(replacement_path, project, replacement_root));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_replacing_mutation_guard_t replacement = {
        .replacement_path = replacement_path,
        .live_path = db_path,
    };
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_replacing_mutation_guard_begin,
                                              mcp_replacing_mutation_guard_end, &replacement);
    char *resp = cbm_mcp_handle_tool(
        srv, "search_graph", "{\"project\":\"guard-corrupt-recheck\",\"name_pattern\":\".*\"}");
    bool response_used_replacement =
        resp && !response_contains_json_fragment(resp, "\"isError\":true");
    free(resp);
    cbm_mcp_server_free(srv);

    cbm_store_t *check = cbm_store_open_path_query(db_path);
    bool valid_generation = check && cbm_store_check_integrity(check);
    cbm_project_t stored_project = {0};
    bool replacement_root_visible =
        check && cbm_store_get_project(check, project, &stored_project) == CBM_STORE_OK &&
        stored_project.root_path && strcmp(stored_project.root_path, replacement_root) == 0;
    cbm_project_free_fields(&stored_project);
    cbm_store_close(check);
    char unexpected_backup[CBM_SZ_1K];
    int backup_count =
        mcp_find_corrupt_backups(cache, project, unexpected_backup, sizeof(unexpected_backup));
    bool live_exists = cbm_file_exists(db_path);
    bool replacement_consumed = !cbm_file_exists(replacement_path);
    int begin_count = replacement.guard.begin_count;
    int end_count = replacement.guard.end_count;
    bool guarded_project = begin_count == 1 && end_count == 1 &&
                           strcmp(replacement.guard.begin_projects[0], project) == 0 &&
                           strcmp(replacement.guard.end_projects[0], project) == 0;
    bool replacement_attempted = replacement.replacement_attempted;
    bool replacement_succeeded = replacement.replacement_succeeded;

    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    cbm_unlink(replacement_path);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(replacement_attempted);
    ASSERT_TRUE(replacement_succeeded);
    ASSERT_TRUE(guarded_project);
    ASSERT_TRUE(response_used_replacement);
    ASSERT_TRUE(live_exists);
    ASSERT_TRUE(replacement_consumed);
    ASSERT_TRUE(valid_generation);
    ASSERT_TRUE(replacement_root_visible);
    ASSERT_EQ(backup_count, 0);
    PASS();
}

/* A fixed `.corrupt` destination is itself user recovery data. A later
 * quarantine must retain it byte-for-byte and choose a distinct backup name
 * rather than unlinking the previous incident before rename. */
TEST(tool_corrupt_store_cleanup_preserves_existing_backup_and_uses_unique_name) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-corrupt-unique-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-corrupt-unique";
    char db_path[CBM_SZ_1K];
    char existing_backup_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    snprintf(existing_backup_path, sizeof(existing_backup_path), "%s.corrupt", db_path);
    ASSERT_TRUE(mcp_make_corrupt_project_store(cache, project));
    ASSERT_EQ(th_write_file(existing_backup_path, "previous-backup-must-survive\n"), 0);

    long existing_len = 0;
    unsigned char *existing_before = mcp_read_file_bytes(existing_backup_path, &existing_len);
    ASSERT_NOT_NULL(existing_before);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t probe = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &probe);
    char *resp = cbm_mcp_handle_tool(
        srv, "search_graph", "{\"project\":\"guard-corrupt-unique\",\"name_pattern\":\".*\"}");
    free(resp);
    cbm_mcp_server_free(srv);

    bool existing_unchanged =
        mcp_file_matches_snapshot(existing_backup_path, existing_before, existing_len);
    free(existing_before);
    char unique_backup_path[CBM_SZ_1K];
    int backup_count =
        mcp_find_corrupt_backups(cache, project, unique_backup_path, sizeof(unique_backup_path));
    cbm_store_t *quarantined =
        unique_backup_path[0] ? cbm_store_open_path_query(unique_backup_path) : NULL;
    bool unique_backup_is_corrupt = quarantined && !cbm_store_check_integrity(quarantined);
    cbm_store_close(quarantined);
    bool live_removed = !cbm_file_exists(db_path);
    int begin_count = probe.begin_count;
    int end_count = probe.end_count;
    bool guarded_project = begin_count == 1 && end_count == 1 &&
                           strcmp(probe.begin_projects[0], project) == 0 &&
                           strcmp(probe.end_projects[0], project) == 0;

    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(guarded_project);
    ASSERT_TRUE(existing_unchanged);
    ASSERT_EQ(backup_count, 2);
    ASSERT_TRUE(unique_backup_path[0] != '\0');
    ASSERT_TRUE(unique_backup_is_corrupt);
    ASSERT_TRUE(live_removed);
    PASS();
}

/* Deterministically fail immediately before atomic snapshot publication on
 * every platform. The incomplete pending copy must be removed while the live
 * DB and its committed WAL remain byte-for-byte untouched. */
TEST(tool_corrupt_store_cleanup_publish_failure_preserves_db_and_wal) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-corrupt-publish-fail-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-corrupt-publish-fail";
    char db_path[CBM_SZ_1K];
    char wal_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    snprintf(wal_path, sizeof(wal_path), "%s-wal", db_path);
    cbm_store_t *writer = mcp_open_corrupt_project_store_with_wal(cache, project);
    ASSERT_NOT_NULL(writer);
    ASSERT_TRUE(cbm_file_exists(wal_path));

    long db_len = 0;
    long wal_len = 0;
    unsigned char *db_before = mcp_read_file_bytes(db_path, &db_len);
    unsigned char *wal_before = mcp_read_file_bytes(wal_path, &wal_len);
    ASSERT_NOT_NULL(db_before);
    ASSERT_NOT_NULL(wal_before);
    ASSERT_TRUE(db_len > 0);
    ASSERT_TRUE(wal_len > 0);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t guard = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &guard);
    mcp_quarantine_hook_probe_t hook = {.deny_step = "before_snapshot_publish"};
    cbm_mcp_server_set_quarantine_test_hook(srv, mcp_quarantine_hook_probe, &hook);
    char *resp =
        cbm_mcp_handle_tool(srv, "search_graph",
                            "{\"project\":\"guard-corrupt-publish-fail\",\"name_pattern\":\".*\"}");

    bool db_unchanged = mcp_file_matches_snapshot(db_path, db_before, db_len);
    bool wal_unchanged = mcp_file_matches_snapshot(wal_path, wal_before, wal_len);
    char unexpected_backup[CBM_SZ_1K];
    int backup_count =
        mcp_find_corrupt_backups(cache, project, unexpected_backup, sizeof(unexpected_backup));
    int artifact_count = mcp_count_corrupt_artifacts(cache, project);
    int begin_count = guard.begin_count;
    int end_count = guard.end_count;
    bool guarded_project = begin_count == 1 && end_count == 1 &&
                           strcmp(guard.begin_projects[0], project) == 0 &&
                           strcmp(guard.end_projects[0], project) == 0;
    bool failed_at_publish =
        hook.call_count == 1 && strcmp(hook.steps[0], "before_snapshot_publish") == 0;

    free(resp);
    cbm_mcp_server_free(srv);
    free(db_before);
    free(wal_before);
    cbm_store_close(writer);
    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(failed_at_publish);
    ASSERT_TRUE(guarded_project);
    ASSERT_TRUE(db_unchanged);
    ASSERT_TRUE(wal_unchanged);
    ASSERT_EQ(backup_count, 0);
    ASSERT_EQ(artifact_count, 0);
    PASS();
}

/* Once the recovery snapshot is atomically visible, a crash/failure before
 * deleting the live generation may leave both copies. The live DB/WAL must be
 * unchanged, and the published backup must already contain committed WAL data
 * as one self-contained SQLite database. */
TEST(tool_corrupt_store_cleanup_publishes_complete_wal_snapshot_before_delete) {
    char cache[256];
    snprintf(cache, sizeof(cache), "%s/cbm-mcp-corrupt-after-publish-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(cache));

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    const char *project = "guard-corrupt-after-publish";
    char db_path[CBM_SZ_1K];
    char wal_path[CBM_SZ_1K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    snprintf(wal_path, sizeof(wal_path), "%s-wal", db_path);
    cbm_store_t *writer = mcp_open_corrupt_project_store_with_wal(cache, project);
    ASSERT_NOT_NULL(writer);
    ASSERT_TRUE(cbm_file_exists(wal_path));

    long db_len = 0;
    long wal_len = 0;
    unsigned char *db_before = mcp_read_file_bytes(db_path, &db_len);
    unsigned char *wal_before = mcp_read_file_bytes(wal_path, &wal_len);
    ASSERT_NOT_NULL(db_before);
    ASSERT_NOT_NULL(wal_before);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    mcp_mutation_guard_probe_t guard = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &guard);
    mcp_quarantine_hook_probe_t hook = {.deny_step = "after_snapshot_publish"};
    cbm_mcp_server_set_quarantine_test_hook(srv, mcp_quarantine_hook_probe, &hook);
    char *resp = cbm_mcp_handle_tool(
        srv, "search_graph",
        "{\"project\":\"guard-corrupt-after-publish\",\"name_pattern\":\".*\"}");

    bool db_unchanged = mcp_file_matches_snapshot(db_path, db_before, db_len);
    bool wal_unchanged = mcp_file_matches_snapshot(wal_path, wal_before, wal_len);
    char backup_path[CBM_SZ_1K];
    int backup_count = mcp_find_corrupt_backups(cache, project, backup_path, sizeof(backup_path));
    int artifact_count = mcp_count_corrupt_artifacts(cache, project);
    cbm_store_t *snapshot = backup_path[0] ? cbm_store_open_path_query(backup_path) : NULL;
    cbm_project_t recovered = {0};
    bool recovered_wal_project =
        snapshot && cbm_store_get_project(snapshot, project, &recovered) == CBM_STORE_OK &&
        recovered.root_path && strcmp(recovered.root_path, "826") == 0;
    cbm_project_free_fields(&recovered);
    cbm_store_close(snapshot);
    char backup_wal[CBM_SZ_2K];
    char backup_shm[CBM_SZ_2K];
    snprintf(backup_wal, sizeof(backup_wal), "%s-wal", backup_path);
    snprintf(backup_shm, sizeof(backup_shm), "%s-shm", backup_path);
    bool snapshot_self_contained = !cbm_file_exists(backup_wal) && !cbm_file_exists(backup_shm);
    bool hook_order = hook.call_count == 2 &&
                      strcmp(hook.steps[0], "before_snapshot_publish") == 0 &&
                      strcmp(hook.steps[1], "after_snapshot_publish") == 0;
    bool guard_balanced = guard.begin_count == 1 && guard.end_count == 1 &&
                          strcmp(guard.begin_projects[0], project) == 0 &&
                          strcmp(guard.end_projects[0], project) == 0;

    free(resp);
    cbm_mcp_server_free(srv);
    free(db_before);
    free(wal_before);
    cbm_store_close(writer);
    mcp_cleanup_corrupt_backups(cache, project);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_rmdir(cache);

    ASSERT_TRUE(hook_order);
    ASSERT_TRUE(guard_balanced);
    ASSERT_TRUE(db_unchanged);
    ASSERT_TRUE(wal_unchanged);
    ASSERT_EQ(backup_count, 1);
    ASSERT_EQ(artifact_count, 1);
    ASSERT_TRUE(recovered_wal_project);
    ASSERT_TRUE(snapshot_self_contained);
    PASS();
}

TEST(tool_index_repository_reports_store_backed_adr) {
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-index-adr-test-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-index-adr-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def main():\n    return 'ok'\n", fp);
    fclose(fp);

    char *project = cbm_project_name_from_path(tmp_dir);
    ASSERT_NOT_NULL(project);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[1024];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", tmp_dir);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(resp);
    ASSERT(response_contains_json_fragment(resp, "\"status\":\"indexed\""));
    ASSERT(response_contains_json_fragment(resp, "\"rust_analysis\":{"));
    ASSERT(response_contains_json_fragment(resp, "\"verdict\":\"not_applicable\""));
    free(resp);

    char update_args[2048];
    snprintf(update_args, sizeof(update_args),
             "{\"project\":\"%s\",\"mode\":\"update\",\"content\":\"## PURPOSE\\n"
             "Store-backed ADR metadata.\\n\"}",
             project);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", update_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "updated"));
    free(resp);

    resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(resp);
    ASSERT(response_contains_json_fragment(resp, "\"status\":\"indexed\""));
    ASSERT(response_contains_json_fragment(resp, "\"adr_present\":true"));
    ASSERT_NULL(strstr(resp, "adr_hint"));
    free(resp);

    char get_args[512];
    snprintf(get_args, sizeof(get_args), "{\"project\":\"%s\",\"mode\":\"get\"}", project);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", get_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "Store-backed ADR metadata."));
    ASSERT_NULL(strstr(resp, "no_adr"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    free(project);
    remove(src_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);
    PASS();
}

/* #1211: list_projects only ever advertises the project NAME, never the
 * repo_path, but re-indexing by that same name (the natural next call) used
 * to fall straight to "repo_path is required" because nothing resolved the
 * name back to its stored root_path. Index once by repo_path, then re-index
 * by project name alone and confirm it actually indexes instead of erroring. */
TEST(tool_index_repository_resolves_root_path_from_project_name_issue1211) {
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-index-byname-test-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-index-byname-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def main():\n    return 'ok'\n", fp);
    fclose(fp);

    char *project = cbm_project_name_from_path(tmp_dir);
    ASSERT_NOT_NULL(project);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char index_args[1024];
    snprintf(index_args, sizeof(index_args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", tmp_dir);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", index_args);
    ASSERT_NOT_NULL(resp);
    ASSERT(response_contains_json_fragment(resp, "\"status\":\"indexed\""));
    free(resp);

    char by_name_args[512];
    snprintf(by_name_args, sizeof(by_name_args), "{\"project\":\"%s\",\"mode\":\"fast\"}", project);
    resp = cbm_mcp_handle_tool(srv, "index_repository", by_name_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NULL(strstr(resp, "repo_path is required"));
    ASSERT(response_contains_json_fragment(resp, "\"status\":\"indexed\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    free(project);
    remove(src_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);
    PASS();
}

/* Same gap, opposite outcome: a project name that was never indexed has no
 * stored root_path to resolve, so it must still fail with the same clear
 * "repo_path is required" error rather than a resolver crash or silent
 * no-op. Guards the fallback path the fix above added. */
TEST(tool_index_repository_unknown_project_name_still_requires_repo_path) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-index-byname-unknown-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS();
    }
    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_handle_tool(srv, "index_repository", "{\"project\":\"never-indexed-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "repo_path is required"));
    free(resp);

    cbm_mcp_server_free(srv);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    cbm_rmdir(cache);
    PASS();
}

TEST(tool_index_repository_dot_uses_absolute_project_key_and_preserves_adr) {
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-index-dot-adr-test-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-index-dot-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def main():\n    return helper()\n\ndef helper():\n    return 1\n", fp);
    fclose(fp);

    char old_cwd[CBM_SZ_4K];
    ASSERT_NOT_NULL(cbm_getcwd(old_cwd, sizeof(old_cwd)));

    char *project = cbm_project_name_from_path(tmp_dir);
    ASSERT_NOT_NULL(project);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    ASSERT_EQ(cbm_chdir(tmp_dir), 0);
    char *resp =
        cbm_mcp_handle_tool(srv, "index_repository", "{\"repo_path\":\".\",\"mode\":\"fast\"}");
    ASSERT_EQ(cbm_chdir(old_cwd), 0);
    ASSERT_NOT_NULL(resp);
    if (!response_contains_json_fragment(resp, "\"status\":\"indexed\"")) {
        free(resp);
        cbm_mcp_server_free(srv);
        cleanup_project_db(cache, project);
        restore_cache_dir(saved_copy);
        free(saved_copy);
        free(project);
        remove(src_path);
        cbm_rmdir(cache);
        cbm_rmdir(tmp_dir);
        PASS();
    }
    ASSERT_NOT_NULL(strstr(resp, project));
    ASSERT(!response_contains_json_fragment(resp, "\"project\":\"root\""));
    free(resp);

    char update_args[2048];
    snprintf(update_args, sizeof(update_args),
             "{\"project\":\"%s\",\"mode\":\"update\",\"content\":\"## PURPOSE\\n"
             "Dot-path ADR marker.\\n\"}",
             project);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", update_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "updated"));
    free(resp);

    ASSERT_EQ(cbm_chdir(tmp_dir), 0);
    resp = cbm_mcp_handle_tool(srv, "index_repository", "{\"repo_path\":\".\",\"mode\":\"fast\"}");
    ASSERT_EQ(cbm_chdir(old_cwd), 0);
    ASSERT_NOT_NULL(resp);
    ASSERT(response_contains_json_fragment(resp, "\"status\":\"indexed\""));
    ASSERT_NOT_NULL(strstr(resp, project));
    ASSERT(response_contains_json_fragment(resp, "\"adr_present\":true"));
    ASSERT(!response_contains_json_fragment(resp, "\"project\":\"root\""));
    free(resp);

    char get_args[512];
    snprintf(get_args, sizeof(get_args), "{\"project\":\"%s\",\"mode\":\"get\"}", project);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", get_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "Dot-path ADR marker."));
    ASSERT_NULL(strstr(resp, "no_adr"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    free(project);
    remove(src_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);
    PASS();
}

TEST(tool_manage_adr_not_found_rich_error) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-adr-missing-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS();
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *resp = cbm_mcp_handle_tool(srv, "manage_adr",
                                     "{\"project\":\"cbm-no-such-project-zzz\",\"mode\":\"get\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "or not indexed"));
    ASSERT_NOT_NULL(strstr(resp, "hint"));
    free(resp);

    cbm_mcp_server_free(srv);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    cbm_rmdir(cache);
    PASS();
}

TEST(tool_manage_adr_get_accepts_abs_path) {
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-adr-abspath-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-adr-abspath-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def main():\n    return 'ok'\n", fp);
    fclose(fp);

    char *project = cbm_project_name_from_path(tmp_dir);
    ASSERT_NOT_NULL(project);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[1024];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", tmp_dir);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(resp);
    ASSERT(response_contains_json_fragment(resp, "\"status\":\"indexed\""));
    free(resp);

    char update_args[2048];
    snprintf(update_args, sizeof(update_args),
             "{\"project\":\"%s\",\"mode\":\"update\",\"content\":\"## PURPOSE\\n"
             "Abs-path normalization test.\\n\"}",
             project);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", update_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "updated"));
    free(resp);

    char get_args[512];
    snprintf(get_args, sizeof(get_args), "{\"project\":\"%s\",\"mode\":\"get\"}", tmp_dir);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", get_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "Abs-path normalization test."));
    ASSERT_NULL(strstr(resp, "or not indexed"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    free(project);
    remove(src_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);
    PASS();
}

TEST(tool_manage_adr_get_accepts_symlink_path) {
#ifdef _WIN32
    PASS();
#else
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-adr-realpath-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-adr-realpath-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    char link_path[320];
    snprintf(link_path, sizeof(link_path), "%s-link", tmp_dir);
    (void)unlink(link_path);
    if (symlink(tmp_dir, link_path) != 0) {
        cbm_rmdir(cache);
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def main():\n    return 'ok'\n", fp);
    fclose(fp);

    char *project = cbm_project_name_from_path(tmp_dir);
    ASSERT_NOT_NULL(project);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[1024];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", link_path);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(resp);
    ASSERT(response_contains_json_fragment(resp, "\"status\":\"indexed\""));
    ASSERT_NOT_NULL(strstr(resp, project));
    free(resp);

    char update_args[2048];
    snprintf(update_args, sizeof(update_args),
             "{\"project\":\"%s\",\"mode\":\"update\",\"content\":\"## PURPOSE\\n"
             "Symlink-path normalization test.\\n\"}",
             project);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", update_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "updated"));
    free(resp);

    char get_args[512];
    snprintf(get_args, sizeof(get_args), "{\"project\":\"%s\",\"mode\":\"get\"}", link_path);
    resp = cbm_mcp_handle_tool(srv, "manage_adr", get_args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "Symlink-path normalization test."));
    ASSERT_NULL(strstr(resp, "or not indexed"));
    ASSERT_NULL(strstr(resp, "no_adr"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    free(project);
    remove(src_path);
    unlink(link_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);
    PASS();
#endif
}

TEST(tool_detect_changes_not_found_rich_error) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-detect-missing-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS();
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char *resp =
        cbm_mcp_handle_tool(srv, "detect_changes", "{\"project\":\"cbm-no-such-project-zzz\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "or not indexed"));
    ASSERT_NOT_NULL(strstr(resp, "hint"));
    free(resp);

    cbm_mcp_server_free(srv);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    cbm_rmdir(cache);
    PASS();
}

/* detect_changes owns shell output through regular temporary files. An error
 * after opening that file must use fclose + unlink. The command hook then
 * rejects merge-base only when it reaches the contained subprocess helper, so
 * a raw popen regression bypasses the hook and fails this test. */
TEST(tool_detect_changes_contained_commands_clean_up_error_and_success) {
    char cache[512];
    (void)snprintf(cache, sizeof(cache), "%s/cbm-detect-contained-XXXXXX", cbm_tmpdir());
    bool cache_created = cbm_mkdtemp(cache) != NULL;
    char repo[CBM_SZ_4K];
    (void)snprintf(repo, sizeof(repo), "%s/cbm-detect-repo-XXXXXX", cbm_tmpdir());
    bool repo_created = cbm_mkdtemp(repo) != NULL;
    char empty_template[CBM_SZ_4K];
    char empty_hooks[CBM_SZ_4K];
    char template_argument[CBM_SZ_4K];
    char hooks_config[CBM_SZ_4K];
    char hostile_template[CBM_SZ_4K];
    char hostile_template_hooks[CBM_SZ_4K];
    char hostile_hooks[CBM_SZ_4K];
    char hostile_hook[CBM_SZ_4K];
    char hostile_config[CBM_SZ_4K];
    int template_length =
        snprintf(empty_template, sizeof(empty_template), "%s/.cbm-empty-template", repo);
    int hooks_length = snprintf(empty_hooks, sizeof(empty_hooks), "%s/.cbm-empty-hooks", repo);
    int template_argument_length =
        snprintf(template_argument, sizeof(template_argument), "--template=%s", empty_template);
    int hooks_config_length =
        snprintf(hooks_config, sizeof(hooks_config), "core.hooksPath=%s", empty_hooks);
    int hostile_template_length =
        snprintf(hostile_template, sizeof(hostile_template), "%s/.cbm-hostile-template", repo);
    int hostile_template_hooks_length = snprintf(
        hostile_template_hooks, sizeof(hostile_template_hooks), "%s/hooks", hostile_template);
    int hostile_hooks_length =
        snprintf(hostile_hooks, sizeof(hostile_hooks), "%s/.cbm-hostile-hooks", repo);
    int hostile_hook_length =
        snprintf(hostile_hook, sizeof(hostile_hook), "%s/pre-commit", hostile_hooks);
    int hostile_config_length =
        snprintf(hostile_config, sizeof(hostile_config), "%s/.cbm-hostile-gitconfig", repo);
    bool git_isolation_ready =
        repo_created && template_length > 0 && (size_t)template_length < sizeof(empty_template) &&
        hooks_length > 0 && (size_t)hooks_length < sizeof(empty_hooks) &&
        template_argument_length > 0 &&
        (size_t)template_argument_length < sizeof(template_argument) && hooks_config_length > 0 &&
        (size_t)hooks_config_length < sizeof(hooks_config) && cbm_mkdir(empty_template) == 0 &&
        cbm_mkdir(empty_hooks) == 0;
    bool hostile_paths_ready =
        git_isolation_ready && hostile_template_length > 0 &&
        (size_t)hostile_template_length < sizeof(hostile_template) &&
        hostile_template_hooks_length > 0 &&
        (size_t)hostile_template_hooks_length < sizeof(hostile_template_hooks) &&
        hostile_hooks_length > 0 && (size_t)hostile_hooks_length < sizeof(hostile_hooks) &&
        hostile_hook_length > 0 && (size_t)hostile_hook_length < sizeof(hostile_hook) &&
        hostile_config_length > 0 && (size_t)hostile_config_length < sizeof(hostile_config) &&
        cbm_mkdir(hostile_template) == 0 && cbm_mkdir(hostile_template_hooks) == 0 &&
        cbm_mkdir(hostile_hooks) == 0;
    FILE *hostile_hook_file = hostile_paths_ready ? cbm_fopen(hostile_hook, "wb") : NULL;
    bool hostile_hook_ready = false;
    if (hostile_hook_file) {
        bool hook_written = fputs("#!/bin/sh\nexit 91\n", hostile_hook_file) >= 0;
        bool hook_closed = fclose(hostile_hook_file) == 0;
        hostile_hook_ready = hook_written && hook_closed && chmod(hostile_hook, 0700) == 0;
    }
    FILE *hostile_config_file = hostile_hook_ready ? cbm_fopen(hostile_config, "wb") : NULL;
    bool hostile_config_ready = false;
    if (hostile_config_file) {
        bool config_written =
            fprintf(hostile_config_file, "[init]\n\ttemplateDir = %s\n[core]\n\thooksPath = %s\n",
                    hostile_template, hostile_hooks) > 0;
        bool config_closed = fclose(hostile_config_file) == 0;
        hostile_config_ready = config_written && config_closed;
    }
    mcp_test_env_backup_t ambient_git = {.name = "GIT_CONFIG_GLOBAL"};
    const char *ambient_git_value = getenv(ambient_git.name);
    ambient_git.present = ambient_git_value != NULL;
    ambient_git.value = ambient_git_value ? strdup(ambient_git_value) : NULL;
    bool ambient_git_saved = !ambient_git_value || ambient_git.value;
    bool hostile_environment_ready = hostile_config_ready && ambient_git_saved &&
                                     cbm_setenv("GIT_CONFIG_GLOBAL", hostile_config, 1) == 0;
    const char *const init_args[] = {"init", "-q", template_argument, NULL};
    const char *const commit_args[] = {
        "-c",      "user.name=cbm-test",
        "-c",      "user.email=cbm-test@example.invalid",
        "-c",      "commit.gpgsign=false",
        "-c",      hooks_config,
        "commit",  "--allow-empty",
        "-q",      "-m",
        "fixture", NULL,
    };
    bool repo_ready = hostile_environment_ready && mcp_test_git(repo, init_args) == 0 &&
                      mcp_test_git(repo, commit_args) == 0;
    if (ambient_git_saved) {
        mcp_test_restore_env(&ambient_git, 1U);
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    bool environment_ready = cache_created && cbm_setenv("CBM_CACHE_DIR", cache, 1) == 0;

    const char *project = "detect-contained-project";
    cbm_mcp_server_t *srv = environment_ready && repo_ready ? cbm_mcp_server_new(NULL) : NULL;
    bool server_ready = srv != NULL;
    cbm_store_t *store = srv ? cbm_mcp_server_store(srv) : NULL;
    bool project_ready = store && cbm_store_upsert_project(store, project, repo) == CBM_STORE_OK;
    mcp_command_hook_probe_t command_probe = {.reject_merge_base = true};
    if (project_ready) {
        cbm_mcp_server_set_project(srv, project);
        cbm_mcp_server_set_command_test_hook(srv, mcp_command_hook_probe, &command_probe);
    }

    char *invalid_response =
        project_ready ? cbm_mcp_handle_tool(srv, "detect_changes",
                                            "{\"project\":\"detect-contained-project\","
                                            "\"base_branch\":\"HEAD\",\"scope\":\"files\","
                                            "\"direction\":\"sideways\"}")
                      : NULL;
    bool invalid_rejected = invalid_response && strstr(invalid_response, "invalid direction");
    char logs[640];
    (void)snprintf(logs, sizeof(logs), "%s/logs", cache);
    int artifacts_after_error =
        invalid_response ? mcp_count_directory_entries_with_prefix(logs, ".mcp-command-") : -1;

    char *rejected_response =
        project_ready ? cbm_mcp_handle_tool(srv, "detect_changes",
                                            "{\"project\":\"detect-contained-project\","
                                            "\"base_branch\":\"HEAD\",\"scope\":\"files\"}")
                      : NULL;
    bool containment_rejected =
        rejected_response && strstr(rejected_response, "contained command could not complete");
    int artifacts_after_rejection =
        rejected_response ? mcp_count_directory_entries_with_prefix(logs, ".mcp-command-") : -1;

    command_probe.reject_merge_base = false;
    char *success_response =
        project_ready ? cbm_mcp_handle_tool(srv, "detect_changes",
                                            "{\"project\":\"detect-contained-project\","
                                            "\"base_branch\":\"HEAD\",\"scope\":\"files\"}")
                      : NULL;
    bool merge_base_reported = success_response && strstr(success_response, "merge_base");
    int artifacts_after_success =
        success_response ? mcp_count_directory_entries_with_prefix(logs, ".mcp-command-") : -1;

    free(invalid_response);
    free(rejected_response);
    free(success_response);
    cbm_mcp_server_free(srv);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    bool cleaned = !cache_created || th_rmtree(cache) == 0;
    /* Git for Windows makes loose objects read-only; the shared test cleanup
     * must still remove the entire self-contained fixture. */
    bool repo_cleaned = !repo_created || th_rmtree(repo) == 0;

    ASSERT_TRUE(cache_created);
    ASSERT_TRUE(repo_created);
    ASSERT_TRUE(repo_ready);
    ASSERT_TRUE(environment_ready);
    ASSERT_TRUE(server_ready);
    ASSERT_TRUE(project_ready);
    ASSERT_TRUE(invalid_rejected);
    ASSERT_EQ(artifacts_after_error, 0);
    ASSERT_TRUE(containment_rejected);
    ASSERT_EQ(artifacts_after_rejection, 0);
    ASSERT_TRUE(merge_base_reported);
    ASSERT_EQ(artifacts_after_success, 0);
    ASSERT_EQ(command_probe.diff_calls, 3);
    ASSERT_EQ(command_probe.merge_base_calls, 2);
    ASSERT_TRUE(cleaned);
    ASSERT_TRUE(repo_cleaned);
    PASS();
}

/* The production regression was a 78,531-byte default detect_changes payload
 * that the universal envelope replaced with a generic error after doing all
 * the useful work. This local fixture makes both encodings larger than that
 * without cloning a network corpus: one changed seed has 256 long-named
 * callers spread across distinct module buckets. The response must retain
 * exact accounting and a useful bounded sample, not require callers to know
 * that limit:1 is an accidental workaround. */
TEST(tool_detect_changes_default_adapts_oversized_shape_with_exact_totals) {
    char repo[CBM_SZ_1K];
    snprintf(repo, sizeof(repo), "%s/cbm-detect-output-XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(repo));

    char seed_path[CBM_SZ_2K];
    snprintf(seed_path, sizeof(seed_path), "%s/seed.c", repo);
    ASSERT_EQ(th_write_file(seed_path, "int seed(void) { return 1; }\n"), 0);
    const char *const init_args[] = {"-c", "init.defaultBranch=main", "init", "-q", NULL};
    const char *const add_args[] = {"add", "seed.c", NULL};
    const char *const commit_args[] = {"-c",     "user.name=cbm-test",
                                       "-c",     "user.email=cbm-test@example.invalid",
                                       "-c",     "commit.gpgsign=false",
                                       "commit", "-q",
                                       "-m",     "fixture",
                                       NULL};
    ASSERT_EQ(mcp_test_git(repo, init_args), 0);
    ASSERT_EQ(mcp_test_git(repo, add_args), 0);
    ASSERT_EQ(mcp_test_git(repo, commit_args), 0);
    ASSERT_EQ(th_write_file(seed_path, "int seed(void) { return 2; }\n"), 0);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    const char *project = "detect-output-bounds";
    ASSERT_EQ(cbm_store_upsert_project(store, project, repo), CBM_STORE_OK);
    cbm_mcp_server_set_project(srv, project);
    cbm_node_t seed = {.project = project,
                       .label = "Function",
                       .name = "seed",
                       .qualified_name = "detect-output-bounds.seed",
                       .file_path = "seed.c",
                       .start_line = 1,
                       .end_line = 1};
    int64_t seed_id = cbm_store_upsert_node(store, &seed);
    ASSERT_GT(seed_id, 0);

    enum { IMPACTED = 256 };
    int64_t caller_ids[IMPACTED];
    for (int i = 0; i < IMPACTED; i++) {
        char name[32];
        char qn[800];
        char file[CBM_SZ_256];
        snprintf(name, sizeof(name), "caller_%03d", i);
        int prefix = snprintf(qn, sizeof(qn), "detect-output-bounds.%s.module_%03d.",
                              i < 40 ? "z_near" : "a_far", i);
        ASSERT_GT(prefix, 0);
        memset(qn + prefix, 'q', sizeof(qn) - (size_t)prefix - 16U);
        snprintf(qn + sizeof(qn) - 16U, 16U, ".%s", name);
        memset(file, 'm', 88U);
        file[88] = '/';
        snprintf(file + 89, sizeof(file) - 89U,
                 "module_%03d_with_a_deliberately_long_identity/caller.c", i);
        cbm_node_t caller = {.project = project,
                             .label = "Function",
                             .name = name,
                             .qualified_name = qn,
                             .file_path = file,
                             .start_line = 1,
                             .end_line = 1};
        caller_ids[i] = cbm_store_upsert_node(store, &caller);
        ASSERT_GT(caller_ids[i], 0);
    }
    for (int i = 0; i < IMPACTED; i++) {
        /* Forty nearest callers and 216 two-hop callers make row ordering
         * observable: the bounded default must not replace near evidence with
         * lexically earlier but farther rows. */
        cbm_edge_t edge = {.project = project,
                           .source_id = caller_ids[i],
                           .target_id = i < 40 ? seed_id : caller_ids[0],
                           .type = "CALLS"};
        ASSERT_GT(cbm_store_insert_edge(store, &edge), 0);
    }

    const char *formats[] = {"tree", "json"};
    for (size_t format_index = 0; format_index < 2U; format_index++) {
        char args[512];
        snprintf(args, sizeof(args),
                 "{\"project\":\"%s\",\"since\":\"HEAD\","
                 "\"base_branch\":\"does-not-exist\",\"depth\":5,\"format\":\"%s\"}",
                 project, formats[format_index]);
        char *response = cbm_mcp_handle_tool(srv, "detect_changes", args);
        ASSERT_NOT_NULL(response);
        ASSERT_TRUE(strlen(response) <= CBM_MCP_RESULT_MAX_BYTES);
        ASSERT_NOT_NULL(strstr(response, "refinement_required"));
        ASSERT_NOT_NULL(strstr(response, "output_truncated"));
        ASSERT_NOT_NULL(strstr(response, "continuation_supported"));
        ASSERT_NOT_NULL(strstr(response, "complete_response_bytes"));
        ASSERT_NOT_NULL(strstr(response, "impacted_modules"));
        ASSERT_NOT_NULL(strstr(response, "seed_symbols"));
        ASSERT_NOT_NULL(strstr(response, "impacted_total"));
        ASSERT_NOT_NULL(strstr(response, "depth"));
        ASSERT_NOT_NULL(strstr(response, "HEAD"));
        ASSERT_NULL(strstr(response, "does-not-exist"));
        ASSERT_NOT_NULL(strstr(response, "\"isError\":false"));

        char *inner = extract_text_content(response);
        ASSERT_NOT_NULL(inner);
        if (format_index == 0U) {
            const char *bytes = strstr(inner, "complete_response_bytes: ");
            ASSERT_NOT_NULL(bytes);
            ASSERT_GT(strtoull(bytes + strlen("complete_response_bytes: "), NULL, 10), 78000U);
            ASSERT_NOT_NULL(strstr(inner, "changed_files: 2\n"));
            ASSERT_NOT_NULL(strstr(inner, "seed_symbols: 1\n"));
            ASSERT_NOT_NULL(strstr(inner, "impacted_total: 256\n"));
            ASSERT_NOT_NULL(strstr(inner, "depth: 5\n"));
            char *impacted_rows = strstr(inner, "impacted: (rows:");
            char *module_rows = strstr(inner, "impacted_modules:");
            ASSERT_NOT_NULL(impacted_rows);
            ASSERT_NOT_NULL(module_rows);
            char saved_module = *module_rows;
            *module_rows = '\0';
            ASSERT_NOT_NULL(strstr(impacted_rows, "caller.c 1\n"));
            ASSERT_NULL(strstr(impacted_rows, "caller.c 2\n"));
            *module_rows = saved_module;
        } else {
            yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
            ASSERT_NOT_NULL(doc);
            yyjson_val *root = yyjson_doc_get_root(doc);
            ASSERT_GT(yyjson_get_uint(yyjson_obj_get(root, "complete_response_bytes")), 78000U);
            ASSERT_EQ(yyjson_get_int(yyjson_obj_get(root, "changed_files_total")), 2);
            ASSERT_EQ(yyjson_get_int(yyjson_obj_get(root, "seed_symbols")), 1);
            ASSERT_EQ(yyjson_get_int(yyjson_obj_get(root, "impacted_total")), IMPACTED);
            ASSERT_EQ(yyjson_get_int(yyjson_obj_get(root, "depth")), 5);
            ASSERT_LT(yyjson_get_int(yyjson_obj_get(root, "impacted_shown")), IMPACTED);
            yyjson_val *impacted = yyjson_obj_get(root, "impacted");
            size_t impacted_index, impacted_max;
            yyjson_val *impacted_entry;
            yyjson_arr_foreach(impacted, impacted_index, impacted_max, impacted_entry) {
                ASSERT_EQ(yyjson_get_int(yyjson_obj_get(impacted_entry, "hop")), 1);
            }
            int rollup_total = 0;
            bool found_other = false;
            yyjson_val *rollup = yyjson_obj_get(root, "impacted_modules");
            size_t index, max;
            yyjson_val *entry;
            yyjson_arr_foreach(rollup, index, max, entry) {
                rollup_total += (int)yyjson_get_int(yyjson_obj_get(entry, "count"));
                const char *module = yyjson_get_str(yyjson_obj_get(entry, "module"));
                found_other = found_other || (module && strcmp(module, "(other)") == 0);
            }
            ASSERT_EQ(rollup_total, IMPACTED);
            ASSERT_TRUE(found_other);
            ASSERT_TRUE(yyjson_arr_size(rollup) < IMPACTED);
            yyjson_doc_free(doc);
        }
        free(inner);
        free(response);
    }

    /* Explicit row limits retain the same exact totals without triggering the
     * response-level refinement path. */
    char *response = cbm_mcp_handle_tool(srv, "detect_changes",
                                         "{\"project\":\"detect-output-bounds\",\"since\":\"HEAD\","
                                         "\"depth\":5,\"limit\":1,\"format\":\"json\"}");
    ASSERT_NOT_NULL(response);
    char *inner = extract_text_content(response);
    ASSERT_NOT_NULL(inner);
    yyjson_doc *doc = yyjson_read(inner, strlen(inner), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(root, "impacted_total")), IMPACTED);
    ASSERT_EQ(yyjson_get_int(yyjson_obj_get(root, "impacted_shown")), 1);
    ASSERT_EQ(yyjson_arr_size(yyjson_obj_get(root, "impacted")), 1U);
    int limit_one_rollup_total = 0;
    yyjson_val *limit_one_rollup = yyjson_obj_get(root, "impacted_modules");
    size_t limit_one_index, limit_one_max;
    yyjson_val *limit_one_entry;
    yyjson_arr_foreach(limit_one_rollup, limit_one_index, limit_one_max, limit_one_entry) {
        limit_one_rollup_total += (int)yyjson_get_int(yyjson_obj_get(limit_one_entry, "count"));
    }
    ASSERT_EQ(limit_one_rollup_total, IMPACTED);
    yyjson_doc_free(doc);
    free(inner);
    free(response);

    response = cbm_mcp_handle_tool(
        srv, "detect_changes",
        "{\"project\":\"detect-output-bounds\",\"cursor\":\"stale-worktree-token\"}");
    ASSERT_NOT_NULL(response);
    ASSERT_NOT_NULL(strstr(response, "detect_changes_cursor_unverifiable"));
    ASSERT_NOT_NULL(strstr(response, "live worktree"));
    ASSERT_NOT_NULL(strstr(response, "\"isError\":true"));
    free(response);

    for (size_t format_index = 0; format_index < 2U; format_index++) {
        char allocation_args[256];
        snprintf(allocation_args, sizeof(allocation_args),
                 "{\"project\":\"detect-output-bounds\",\"since\":\"HEAD\","
                 "\"format\":\"%s\"}",
                 formats[format_index]);
        cbm_mcp_test_detect_refinement_fail_rollup_alloc(true);
        response = cbm_mcp_handle_tool(srv, "detect_changes", allocation_args);
        cbm_mcp_test_detect_refinement_fail_rollup_alloc(false);
        ASSERT_NOT_NULL(response);
        ASSERT_NOT_NULL(strstr(response, "detect_changes_refinement_allocation_unavailable"));
        ASSERT_NOT_NULL(strstr(response, "no partial result returned"));
        ASSERT_NOT_NULL(strstr(response, "\"isError\":true"));
        ASSERT_NULL(strstr(response, "caller_000"));
        free(response);
    }

    cbm_mcp_server_free(srv);
    th_rmtree(repo);
    PASS();
}

/* Regression test for issue #1363: detect_changes seeded every definition in
 * a changed file instead of just the ones whose line range overlaps the diff
 * hunk. cbm_detect_node_in_hunks is the overlap primitive; this exercises it
 * directly, independent of the git/subprocess/index plumbing around it. */
TEST(detect_changes_node_in_hunks_overlap_issue1363) {
    cbm_changed_hunk_t hunks[2] = {
        {.path = "pkg/mod.py", .start_line = 10, .end_line = 12},
        {.path = "pkg/other.py", .start_line = 1, .end_line = 1},
    };

    cbm_node_t inside = {.start_line = 8, .end_line = 15};
    ASSERT(cbm_detect_node_in_hunks(&inside, hunks, 2, "pkg/mod.py"));

    cbm_node_t exact = {.start_line = 10, .end_line = 12};
    ASSERT(cbm_detect_node_in_hunks(&exact, hunks, 2, "pkg/mod.py"));

    cbm_node_t touches_edge = {.start_line = 12, .end_line = 20};
    ASSERT(cbm_detect_node_in_hunks(&touches_edge, hunks, 2, "pkg/mod.py"));

    cbm_node_t before = {.start_line = 1, .end_line = 9};
    ASSERT(!cbm_detect_node_in_hunks(&before, hunks, 2, "pkg/mod.py"));

    cbm_node_t after = {.start_line = 13, .end_line = 20};
    ASSERT(!cbm_detect_node_in_hunks(&after, hunks, 2, "pkg/mod.py"));

    /* Same line range, different file — must not match. */
    cbm_node_t wrong_file = {.start_line = 10, .end_line = 12};
    ASSERT(!cbm_detect_node_in_hunks(&wrong_file, hunks, 2, "pkg/unrelated.py"));

    PASS();
}

/* End-to-end regression test for issue #1363: a same-line-count edit inside
 * one function must seed only that function, not every definition in the
 * file. A flat file with two independent top-level functions (no enclosing
 * class) makes this unambiguous — before the fix, editing foo() also seeded
 * bar() because seeding was scoped to the whole changed file. */
TEST(detect_changes_seeds_only_touched_symbol_issue1363) {
    char repo[512];
    snprintf(repo, sizeof(repo), "%s/cbm-detect-seed-scope-XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(repo)) {
        FAIL("cbm_mkdtemp failed");
    }

    char src[600];
    snprintf(src, sizeof(src), "%s/mod.py", repo);
    ASSERT_EQ(th_write_file(src, "def foo():\n"
                                 "    x = 1\n"
                                 "    return x\n"
                                 "\n"
                                 "\n"
                                 "def bar():\n"
                                 "    y = 2\n"
                                 "    return y\n"),
              0);

    /* `git -C` with double quotes, not `cd '<dir>' &&`: single quotes are not
     * quoting characters for cmd.exe, and identity/branch/signing come from -c
     * so the fixture does not depend on the machine's global git config. The
     * assertions below read `base: main`, so pin init.defaultBranch. */
#define DC1363_GITCFG \
    "-c user.name=t -c user.email=t@t.io -c init.defaultBranch=main -c commit.gpgsign=false"
    char cmd[1200];
    const char *steps[] = {"init -q", "add -A", "commit -q -m init"};
    for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" " DC1363_GITCFG " %s", repo, steps[s]);
        if (system(cmd) != 0) {
            th_rmtree(repo);
            FAIL("git fixture setup failed");
        }
    }
#undef DC1363_GITCFG

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char idx_args[700];
    snprintf(idx_args, sizeof(idx_args), "{\"repo_path\":\"%s\",\"mode\":\"full\"}", repo);
    char *idx_resp = cbm_mcp_handle_tool(srv, "index_repository", idx_args);
    ASSERT_NOT_NULL(idx_resp);
    free(idx_resp);

    /* Same-line-count in-place edit inside foo() only; bar() is untouched. */
    ASSERT_EQ(th_write_file(src, "def foo():\n"
                                 "    x = 11\n"
                                 "    return x\n"
                                 "\n"
                                 "\n"
                                 "def bar():\n"
                                 "    y = 2\n"
                                 "    return y\n"),
              0);

    char *project = cbm_project_name_from_path(repo);
    ASSERT_NOT_NULL(project);
    char dc_args[700];
    snprintf(dc_args, sizeof(dc_args), "{\"project\":\"%s\",\"depth\":1}", project);
    char *dc_resp = cbm_mcp_handle_tool(srv, "detect_changes", dc_args);
    ASSERT_NOT_NULL(dc_resp);
    /* cbm_mcp_handle_tool wraps the tree text in a JSON string, so a literal
     * newline in the source becomes the two-character `\n` escape sequence
     * in dc_resp's actual bytes — match that, not a real newline. */
    ASSERT_NOT_NULL(strstr(dc_resp, "seed_symbols: 1\\n"));
    ASSERT_NULL(strstr(dc_resp, "bar"));

    free(dc_resp);
    free(project);
    cbm_mcp_server_free(srv);
    th_rmtree(repo);
    PASS();
}

/* Recall guard for the zero-overlap case (#1363 review): an import-only edit
 * changes lines that lie outside every definition's range. Scoping alone would
 * drop the file from the seed set — worse recall than the whole-file behavior
 * being replaced — so detect_collect_seeds falls back to whole-file seeding
 * when a changed file has hunks but no definition overlapping any of them. */
TEST(detect_changes_zero_overlap_falls_back_issue1363) {
    char repo[512];
    snprintf(repo, sizeof(repo), "%s/cbm-detect-zero-overlap-XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(repo)) {
        FAIL("cbm_mkdtemp failed");
    }

    char src[600];
    snprintf(src, sizeof(src), "%s/mod.py", repo);
    /* Import on line 1 sits above both definitions. */
    ASSERT_EQ(th_write_file(src, "import os\n"
                                 "\n"
                                 "\n"
                                 "def foo():\n"
                                 "    return 1\n"
                                 "\n"
                                 "\n"
                                 "def bar():\n"
                                 "    return 2\n"),
              0);

#define DC1363B_GITCFG \
    "-c user.name=t -c user.email=t@t.io -c init.defaultBranch=main -c commit.gpgsign=false"
    char cmd[1200];
    const char *steps[] = {"init -q", "add -A", "commit -q -m init"};
    for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" " DC1363B_GITCFG " %s", repo, steps[s]);
        if (system(cmd) != 0) {
            th_rmtree(repo);
            FAIL("git fixture setup failed");
        }
    }
#undef DC1363B_GITCFG

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char idx_args[700];
    snprintf(idx_args, sizeof(idx_args), "{\"repo_path\":\"%s\",\"mode\":\"full\"}", repo);
    char *idx_resp = cbm_mcp_handle_tool(srv, "index_repository", idx_args);
    ASSERT_NOT_NULL(idx_resp);
    free(idx_resp);

    /* Edit ONLY the import line — outside every definition's line range. */
    ASSERT_EQ(th_write_file(src, "import os, sys\n"
                                 "\n"
                                 "\n"
                                 "def foo():\n"
                                 "    return 1\n"
                                 "\n"
                                 "\n"
                                 "def bar():\n"
                                 "    return 2\n"),
              0);

    char *project = cbm_project_name_from_path(repo);
    ASSERT_NOT_NULL(project);
    char dc_args[700];
    snprintf(dc_args, sizeof(dc_args), "{\"project\":\"%s\",\"depth\":1}", project);
    char *dc_resp = cbm_mcp_handle_tool(srv, "detect_changes", dc_args);
    ASSERT_NOT_NULL(dc_resp);
    /* Both definitions must survive: zero overlaps means no scoping for this
     * file, not an empty seed set. */
    ASSERT_NOT_NULL(strstr(dc_resp, "seed_symbols: 2\\n"));

    free(dc_resp);
    free(project);
    cbm_mcp_server_free(srv);
    th_rmtree(repo);
    PASS();
}

TEST(tool_ingest_traces_basic) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":37,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"ingest_traces\","
             "\"arguments\":{\"traces\":[{\"caller\":\"a\",\"callee\":\"b\"}]}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "accepted"));
    ASSERT_NOT_NULL(strstr(resp, "traces_received"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(tool_ingest_traces_empty) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":38,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"ingest_traces\","
                                   "\"arguments\":{\"traces\":[]}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "accepted"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  IDLE STORE EVICTION
 * ══════════════════════════════════════════════════════════════════ */

TEST(store_idle_eviction) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_mcp_server_set_project(srv, "test-evict");

    /* Trigger resolve_store via a tool call to set store_last_used */
    char *resp = cbm_mcp_handle_tool(srv, "get_graph_schema", "{\"project\":\"test-evict\"}");
    free(resp);

    ASSERT_TRUE(cbm_mcp_server_has_cached_store(srv));

    /* Evict with 0s timeout → should evict immediately */
    cbm_mcp_server_evict_idle(srv, 0);
    ASSERT_FALSE(cbm_mcp_server_has_cached_store(srv));

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(store_idle_no_eviction_within_timeout) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_mcp_server_set_project(srv, "test-evict");

    char *resp = cbm_mcp_handle_tool(srv, "get_graph_schema", "{\"project\":\"test-evict\"}");
    free(resp);

    ASSERT_TRUE(cbm_mcp_server_has_cached_store(srv));

    /* Evict with large timeout → should NOT evict */
    cbm_mcp_server_evict_idle(srv, 99999);
    ASSERT_TRUE(cbm_mcp_server_has_cached_store(srv));

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(store_idle_evict_protects_initial_store) {
    /* Evicting with NULL server should not crash */
    cbm_mcp_server_evict_idle(NULL, 0);

    /* Evicting server whose store was never accessed via a named project
     * should NOT evict the initial in-memory store (store_last_used == 0). */
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_TRUE(cbm_mcp_server_has_cached_store(srv));
    cbm_mcp_server_evict_idle(srv, 0);
    ASSERT_TRUE(cbm_mcp_server_has_cached_store(srv));

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(store_idle_evict_access_resets_timer) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    cbm_mcp_server_set_project(srv, "test-evict");

    /* First access */
    char *resp = cbm_mcp_handle_tool(srv, "get_graph_schema", "{\"project\":\"test-evict\"}");
    free(resp);

    /* Second access (resets timer) */
    resp = cbm_mcp_handle_tool(srv, "get_graph_schema", "{\"project\":\"test-evict\"}");
    free(resp);

    ASSERT_TRUE(cbm_mcp_server_has_cached_store(srv));

    /* With large timeout, store should survive */
    cbm_mcp_server_evict_idle(srv, 99999);
    ASSERT_TRUE(cbm_mcp_server_has_cached_store(srv));

    /* With 0 timeout, store should be evicted */
    cbm_mcp_server_evict_idle(srv, 0);
    ASSERT_FALSE(cbm_mcp_server_has_cached_store(srv));

    cbm_mcp_server_free(srv);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  URI HELPERS
 * ══════════════════════════════════════════════════════════════════ */

TEST(parse_file_uri_unix) {
    char path[256];
    ASSERT_TRUE(cbm_parse_file_uri("file:///home/user/project", path, sizeof(path)));
    ASSERT_STR_EQ(path, "/home/user/project");

    ASSERT_TRUE(cbm_parse_file_uri("file:///tmp/test", path, sizeof(path)));
    ASSERT_STR_EQ(path, "/tmp/test");

    ASSERT_TRUE(cbm_parse_file_uri("file:///", path, sizeof(path)));
    ASSERT_STR_EQ(path, "/");
    PASS();
}

TEST(parse_file_uri_windows) {
    char path[256];
    /* Windows drive letter — leading / stripped */
    ASSERT_TRUE(cbm_parse_file_uri("file:///C:/Users/project", path, sizeof(path)));
    ASSERT_STR_EQ(path, "C:/Users/project");

    ASSERT_TRUE(cbm_parse_file_uri("file:///D:/Projects/myapp", path, sizeof(path)));
    ASSERT_STR_EQ(path, "D:/Projects/myapp");
    PASS();
}

TEST(parse_file_uri_invalid) {
    char path[256];
    /* Non-file URI */
    ASSERT_FALSE(cbm_parse_file_uri("https://example.com", path, sizeof(path)));
    ASSERT_STR_EQ(path, "");

    /* Empty string */
    ASSERT_FALSE(cbm_parse_file_uri("", path, sizeof(path)));
    ASSERT_STR_EQ(path, "");

    /* NULL */
    ASSERT_FALSE(cbm_parse_file_uri(NULL, path, sizeof(path)));
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  SNIPPET TESTS — Port of internal/tools/snippet_test.go
 * ══════════════════════════════════════════════════════════════════ */

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Create an MCP server pre-populated with nodes/edges matching Go testSnippetServer.
 * Writes a source file to tmp_dir/project/main.go.
 * Caller must free the server with cbm_mcp_server_free and
 * unlink the source file + rmdir manually. */
static cbm_mcp_server_t *setup_snippet_server(char *tmp_dir, size_t tmp_sz) {
    /* Create temp dir */
    snprintf(tmp_dir, tmp_sz, "/tmp/cbm_snippet_test_XXXXXX");
    if (!cbm_mkdtemp(tmp_dir))
        return NULL;

    char proj_dir[512];
    snprintf(proj_dir, sizeof(proj_dir), "%s/project", tmp_dir);
    cbm_mkdir(proj_dir);

    /* Write sample source file */
    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.go", proj_dir);
    FILE *fp = fopen(src_path, "w");
    if (!fp)
        return NULL;
    fprintf(fp, "package main\n"
                "\n"
                "func HandleRequest() error {\n"
                "\treturn nil\n"
                "}\n"
                "\n"
                "func ProcessOrder(id int) {\n"
                "\t// process\n"
                "}\n"
                "\n"
                "func Run() {\n"
                "\t// server\n"
                "}\n");
    fclose(fp);

    /* Create server with in-memory store */
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv)
        return NULL;

    cbm_store_t *st = cbm_mcp_server_store(srv);
    if (!st) {
        cbm_mcp_server_free(srv);
        return NULL;
    }

    const char *proj_name = "test-project";
    cbm_mcp_server_set_project(srv, proj_name);
    cbm_store_upsert_project(st, proj_name, proj_dir);

    /* Create nodes */
    cbm_node_t n_hr = {0};
    n_hr.project = proj_name;
    n_hr.label = "Function";
    n_hr.name = "HandleRequest";
    n_hr.qualified_name = "test-project.cmd.server.main.HandleRequest";
    n_hr.file_path = "main.go";
    n_hr.start_line = 3;
    n_hr.end_line = 5;
    n_hr.properties_json = "{\"signature\":\"func HandleRequest() error\","
                           "\"return_type\":\"error\","
                           "\"is_exported\":true}";
    int64_t id_hr = cbm_store_upsert_node(st, &n_hr);

    cbm_node_t n_po = {0};
    n_po.project = proj_name;
    n_po.label = "Function";
    n_po.name = "ProcessOrder";
    n_po.qualified_name = "test-project.cmd.server.main.ProcessOrder";
    n_po.file_path = "main.go";
    n_po.start_line = 7;
    n_po.end_line = 9;
    n_po.properties_json = "{\"signature\":\"func ProcessOrder(id int)\"}";
    int64_t id_po = cbm_store_upsert_node(st, &n_po);

    cbm_node_t n_run1 = {0};
    n_run1.project = proj_name;
    n_run1.label = "Function";
    n_run1.name = "Run";
    n_run1.qualified_name = "test-project.cmd.server.Run";
    n_run1.file_path = "main.go";
    n_run1.start_line = 11;
    n_run1.end_line = 13;
    int64_t id_run1 = cbm_store_upsert_node(st, &n_run1);

    cbm_node_t n_run2 = {0};
    n_run2.project = proj_name;
    n_run2.label = "Function";
    n_run2.name = "Run";
    n_run2.qualified_name = "test-project.cmd.worker.Run";
    n_run2.file_path = "main.go";
    n_run2.start_line = 11;
    n_run2.end_line = 13;
    cbm_store_upsert_node(st, &n_run2);

    /* Create edges: HandleRequest -> ProcessOrder, HandleRequest -> Run1 */
    cbm_edge_t e1 = {.project = proj_name, .source_id = id_hr, .target_id = id_po, .type = "CALLS"};
    cbm_store_insert_edge(st, &e1);

    cbm_edge_t e2 = {
        .project = proj_name, .source_id = id_hr, .target_id = id_run1, .type = "CALLS"};
    cbm_store_insert_edge(st, &e2);
    (void)id_run1; /* run1 used for edge above */

    return srv;
}

/* Cleanup temp files created by setup_snippet_server */
static void cleanup_snippet_dir(const char *tmp_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/project/main.go", tmp_dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/project", tmp_dir);
    rmdir(path);
    rmdir(tmp_dir);
}

/* Extract the inner "text" value from an MCP tool result JSON.
 * The MCP envelope is: {"content":[{"type":"text","text":"<inner json>"}]}
 * This returns the unescaped inner JSON. Caller must free. */
static char *extract_text_content(const char *mcp_result) {
    if (!mcp_result)
        return NULL;
    yyjson_doc *doc = yyjson_read(mcp_result, strlen(mcp_result), 0);
    if (!doc)
        return strdup(mcp_result); /* fallback */
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *content = yyjson_obj_get(root, "content");
    if (!content) {
        /* Handle JSON-RPC wrapper: {"jsonrpc":...,"result":{"content":[...]}} */
        yyjson_val *rpc_result = yyjson_obj_get(root, "result");
        if (rpc_result) {
            content = yyjson_obj_get(rpc_result, "content");
        }
    }
    if (!content || !yyjson_is_arr(content)) {
        yyjson_doc_free(doc);
        return strdup(mcp_result);
    }
    yyjson_val *item = yyjson_arr_get(content, 0);
    if (!item) {
        yyjson_doc_free(doc);
        return strdup(mcp_result);
    }
    yyjson_val *text = yyjson_obj_get(item, "text");
    const char *str = yyjson_get_str(text);
    char *result = str ? strdup(str) : strdup(mcp_result);
    yyjson_doc_free(doc);
    return result;
}

/* Call get_code_snippet and extract inner text content.
 * Caller must free returned string. */
static char *call_snippet(cbm_mcp_server_t *srv, const char *args_json) {
    char *raw = cbm_mcp_handle_tool(srv, "get_code_snippet", args_json);
    char *text = extract_text_content(raw);
    free(raw);
    return text;
}

static bool is_valid_json_response(const char *json) {
    if (!json) {
        return false;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        return false;
    }
    yyjson_doc_free(doc);
    return true;
}

static bool snippet_source_has_replacement(const char *json) {
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *source = yyjson_obj_get(root, "source");
    const char *source_str = yyjson_get_str(source);
    bool found = source_str && strstr(source_str, "\xEF\xBF\xBD");
    yyjson_doc_free(doc);
    return found;
}

typedef struct {
    cbm_mcp_server_t *srv;
    char tmp[256];
    char source_path[600];
    char *source;
    size_t source_len;
} generated_snippet_t;

static bool generated_snippet_setup(generated_snippet_t *fx, const char *label, const char *qn,
                                    int line_count, int line_width, const char *declaration,
                                    bool escaping_heavy, bool single_line) {
    memset(fx, 0, sizeof(*fx));
    snprintf(fx->tmp, sizeof(fx->tmp), "/tmp/cbm_generated_snippet_XXXXXX");
    if (!cbm_mkdtemp(fx->tmp))
        return false;
    char project_dir[512];
    snprintf(project_dir, sizeof(project_dir), "%s/project", fx->tmp);
    if (cbm_mkdir(project_dir) != 0)
        return false;
    snprintf(fx->source_path, sizeof(fx->source_path), "%s/generated.py", project_dir);
    FILE *fp = fopen(fx->source_path, "wb");
    if (!fp)
        return false;

    size_t cap = (size_t)line_count * (size_t)(line_width + 64) + 64;
    fx->source = malloc(cap);
    if (!fx->source) {
        fclose(fp);
        return false;
    }
    size_t used = 0;
    if (single_line) {
        const char *prefix = declaration ? declaration : "def huge(): ";
        size_t prefix_len = strlen(prefix);
        memcpy(fx->source, prefix, prefix_len);
        used = prefix_len;
        while ((int)used < line_width) {
            fx->source[used] = (used % 3 == 0) ? '\\' : 'x';
            used++;
        }
    } else {
        for (int line = 1; line <= line_count; line++) {
            if (line == 1 && declaration) {
                size_t n = strlen(declaration);
                memcpy(fx->source + used, declaration, n);
                used += n;
            } else {
                int n = snprintf(fx->source + used, cap - used, "line_%04d ", line);
                used += (size_t)n;
                while ((int)(used % (size_t)(line_width + 1)) < line_width && used + 2 < cap) {
                    static const char escaped[] = {'"', '\\', '\t', 'q'};
                    fx->source[used] = escaping_heavy ? escaped[used % 4] : (char)('a' + line % 26);
                    used++;
                }
            }
            fx->source[used++] = '\n';
        }
    }
    fx->source[used] = '\0';
    fx->source_len = used;
    if (fwrite(fx->source, 1, used, fp) != used || fclose(fp) != 0)
        return false;

    fx->srv = cbm_mcp_server_new(NULL);
    if (!fx->srv)
        return false;
    cbm_store_t *store = cbm_mcp_server_store(fx->srv);
    cbm_mcp_server_set_project(fx->srv, "snippet-pages");
    cbm_store_upsert_project(store, "snippet-pages", project_dir);
    const char *name = strrchr(qn, '.');
    cbm_node_t node = {0};
    node.project = "snippet-pages";
    node.label = label;
    node.name = name ? name + 1 : qn;
    node.qualified_name = qn;
    node.file_path = "generated.py";
    node.start_line = 1;
    node.end_line = line_count;
    node.properties_json = declaration ? "{\"signature\":\"def generated():\"}" : NULL;
    return cbm_store_upsert_node(store, &node) > 0;
}

static void generated_snippet_cleanup(generated_snippet_t *fx) {
    if (fx->srv)
        cbm_mcp_server_free(fx->srv);
    free(fx->source);
    th_rmtree(fx->tmp);
}

static char *snippet_json_string(const char *json, const char *key) {
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *value = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), key) : NULL;
    const char *text = value && yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
    char *copy = text ? strdup(text) : NULL;
    yyjson_doc_free(doc);
    return copy;
}

static long long snippet_json_int(const char *json, const char *key, long long fallback) {
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *value = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), key) : NULL;
    long long result = value && yyjson_is_int(value) ? yyjson_get_sint(value) : fallback;
    yyjson_doc_free(doc);
    return result;
}

static bool snippet_json_bool(const char *json, const char *key) {
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *value = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), key) : NULL;
    bool result = value && yyjson_is_bool(value) && yyjson_get_bool(value);
    yyjson_doc_free(doc);
    return result;
}

/* ── TestSnippet_ExactQN ──────────────────────────────────────── */

TEST(snippet_exact_qn) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp =
        call_snippet(srv, "{\"qualified_name\":\"test-project.cmd.server.main.HandleRequest\","
                          "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"HandleRequest\""));
    ASSERT_NOT_NULL(strstr(resp, "\"source\""));
    /* Exact match should NOT have match_method */
    ASSERT_NULL(strstr(resp, "\"match_method\""));
    /* No property-blob spill: the source IS the payload (signature and
     * docstring are literally in it); metrics live behind search_graph
     * fields=[...]. */
    ASSERT_NULL(strstr(resp, "\"signature\""));
    ASSERT_NULL(strstr(resp, "\"return_type\""));
    ASSERT_NULL(strstr(resp, "\"callers\""));
    ASSERT_NULL(strstr(resp, "\"callees\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_QNSuffix ─────────────────────────────────────── */

TEST(snippet_qn_suffix) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp = call_snippet(srv, "{\"qualified_name\":\"main.HandleRequest\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"HandleRequest\""));
    ASSERT_NOT_NULL(strstr(resp, "\"match_method\":\"suffix\""));
    ASSERT_NOT_NULL(strstr(resp, "\"source\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_UniqueShortName ──────────────────────────────── */

TEST(snippet_unique_short_name) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* "ProcessOrder" is unique — suffix tier matches (QN ends with .ProcessOrder) */
    char *resp = call_snippet(srv, "{\"qualified_name\":\"ProcessOrder\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"ProcessOrder\""));
    ASSERT_NOT_NULL(strstr(resp, "\"match_method\":\"suffix\""));
    ASSERT_NOT_NULL(strstr(resp, "\"source\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_NameTier ─────────────────────────────────────── */

TEST(snippet_name_tier) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* "HandleRequest" — suffix tier finds it (QN ends with .HandleRequest) */
    char *resp = call_snippet(srv, "{\"qualified_name\":\"HandleRequest\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"name\":\"HandleRequest\""));
    ASSERT_NOT_NULL(strstr(resp, "\"match_method\":\"suffix\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_AmbiguousShortName ───────────────────────────── */

TEST(snippet_ambiguous_short_name) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* "Run" matches 2 nodes — should return suggestions */
    char *resp = call_snippet(srv, "{\"qualified_name\":\"Run\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"status\":\"ambiguous\""));
    ASSERT_NOT_NULL(strstr(resp, "\"message\""));
    ASSERT_NOT_NULL(strstr(resp, "\"suggestions\""));
    /* Must NOT have "error" key */
    ASSERT_NULL(strstr(resp, "\"error\""));
    /* Must NOT have "source" */
    ASSERT_NULL(strstr(resp, "\"source\""));
    /* Should have at least 2 suggestions with qualified_name */
    ASSERT_NOT_NULL(strstr(resp, "test-project.cmd.server.Run"));
    ASSERT_NOT_NULL(strstr(resp, "test-project.cmd.worker.Run"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_NotFound ─────────────────────────────────────── */

TEST(snippet_not_found) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp = call_snippet(srv, "{\"qualified_name\":\"CompletelyNonexistentFunctionXYZ123\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    /* Should return error or suggestions */
    ASSERT_TRUE(strstr(resp, "not found") || strstr(resp, "suggestions"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_FuzzySuggestions ─────────────────────────────── */

TEST(snippet_fuzzy_suggestions) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* "Handle" is not an exact QN or suffix — should get not-found guidance */
    char *resp = call_snippet(srv, "{\"qualified_name\":\"Handle\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    /* Should guide user to search_graph */
    ASSERT_NOT_NULL(strstr(resp, "search_graph"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_EnrichedProperties ───────────────────────────── */

TEST(snippet_enriched_properties) {
    /* GUARD (inverted since the compact-output change): the snippet response
     * carries the verbatim source plus location/degree/coverage metadata and
     * NOTHING from the node's property blob — no signature/return_type/
     * is_exported duplication, and never the fp/sp/bt similarity internals
     * (41% of the legacy response). */
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp =
        call_snippet(srv, "{\"qualified_name\":\"test-project.cmd.server.main.HandleRequest\","
                          "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"source\""));
    ASSERT_NULL(strstr(resp, "\"signature\""));
    ASSERT_NULL(strstr(resp, "\"return_type\""));
    ASSERT_NULL(strstr(resp, "\"is_exported\""));
    ASSERT_NULL(strstr(resp, "\"fp\""));
    ASSERT_NULL(strstr(resp, "\"bt\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestOutput_OmitsInternalSimilarityFields ─────────────────────
 * The similarity-detection pipeline persists three internal-only fields in a
 * node's properties_json — "fp" (MinHash fingerprint hex), "sp" (AST structural
 * profile vector) and "bt" (body-token bag). These exist purely so the indexing
 * passes can recompute similarity edges from the DB column; they are noise to an
 * MCP consumer and waste its context. Neither tool response may leak them:
 * get_code_snippet emits no property blob at all, and search_graph drops
 * blocked fields (sg_field_blocked) even when explicitly requested via
 * `fields`, while still surfacing genuine public fields. This test injects a
 * node whose properties mix a public field with all three internal blobs and
 * pins that only the public field escapes. */
TEST(tool_output_omits_internal_similarity_fields) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);

    /* A node carrying a public "signature" alongside the internal fp/sp/bt trio,
     * with distinctive blob values so their absence is checkable by value too. */
    cbm_node_t probe = {0};
    probe.project = "test-project";
    probe.label = "Function";
    probe.name = "LeakProbe";
    probe.qualified_name = "test-project.cmd.server.main.LeakProbe";
    probe.file_path = "main.go";
    probe.start_line = 3;
    probe.end_line = 5;
    probe.properties_json = "{\"signature\":\"func LeakProbe() error\","
                            "\"is_exported\":true,"
                            "\"fp\":\"deadbeefcafef00dfp\","
                            "\"sp\":\"11,22,33,44,55\","
                            "\"bt\":\"alphaProbe betaProbe gammaProbe\"}";
    ASSERT_GT(cbm_store_upsert_node(st, &probe), 0);

    /* get_code_snippet path: emits no property blob, so no internal field —
     * key or value — may appear anywhere in the response. */
    char *snip = call_snippet(srv, "{\"qualified_name\":\"test-project.cmd.server.main.LeakProbe\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(snip);
    ASSERT_NULL(strstr(snip, "deadbeefcafef00dfp")); /* internal values gone */
    ASSERT_NULL(strstr(snip, "11,22,33,44,55"));
    ASSERT_NULL(strstr(snip, "alphaProbe"));
    free(snip);

    /* search_graph path (emit_search_results → enrich_node_properties). */
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":77,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\","
             "\"arguments\":{\"project\":\"test-project\",\"label\":\"Function\","
             "\"name_pattern\":\"LeakProbe\",\"limit\":5,"
             "\"fields\":[\"signature\",\"fp\",\"sp\",\"bt\"]}}}");
    ASSERT_NOT_NULL(resp);
    char *inner = extract_text_content(resp);
    ASSERT_NOT_NULL(inner);
    ASSERT_NOT_NULL(strstr(inner, "signature"));      /* public field survives */
    ASSERT_NOT_NULL(strstr(inner, "func LeakProbe")); /* public value survives */
    ASSERT_NULL(strstr(inner, "deadbeefcafef00dfp")); /* internal value gone */
    ASSERT_NULL(strstr(inner, "11,22,33,44,55"));
    ASSERT_NULL(strstr(inner, "alphaProbe"));
    free(inner);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_FuzzyLastSegment ─────────────────────────────── */

TEST(snippet_fuzzy_last_segment) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* "auth.handlers.HandleRequest" — suffix match should find HandleRequest */
    char *resp = call_snippet(srv, "{\"qualified_name\":\"auth.handlers.HandleRequest\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    /* Should either find it via suffix or guide to search_graph */
    ASSERT_TRUE(strstr(resp, "HandleRequest") != NULL || strstr(resp, "search_graph") != NULL);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_AutoResolve_Default ──────────────────────────── */

TEST(snippet_auto_resolve_default) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* "Run" is ambiguous (2 candidates). Without auto_resolve → suggestions */
    char *resp = call_snippet(srv, "{\"qualified_name\":\"Run\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"status\":\"ambiguous\""));
    ASSERT_NULL(strstr(resp, "\"source\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_AutoResolve_Enabled ──────────────────────────── */

TEST(snippet_auto_resolve_enabled) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    /* "Run" — suffix match should find candidates or guide to search */
    char *resp = call_snippet(srv, "{\"qualified_name\":\"Run\","
                                   "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    /* "Run" matches multiple nodes via suffix → should get suggestions or source */
    ASSERT_TRUE(strstr(resp, "Run") != NULL);
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_IncludeNeighbors_Default ─────────────────────── */

TEST(snippet_include_neighbors_default) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp =
        call_snippet(srv, "{\"qualified_name\":\"test-project.cmd.server.main.HandleRequest\","
                          "\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    /* Call-graph data is opt-in: the default source response is graph-free. */
    ASSERT_NULL(strstr(resp, "\"caller_names\""));
    ASSERT_NULL(strstr(resp, "\"callee_names\""));
    ASSERT_NULL(strstr(resp, "\"callers\""));
    ASSERT_NULL(strstr(resp, "\"callees\""));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_IncludeNeighbors_Enabled ─────────────────────── */

TEST(snippet_include_neighbors_enabled) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char *resp =
        call_snippet(srv, "{\"qualified_name\":\"test-project.cmd.server.main.HandleRequest\","
                          "\"include_neighbors\":true,\"project\":\"test-project\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"source\""));
    ASSERT_NOT_NULL(strstr(resp, "\"callers\""));
    ASSERT_NOT_NULL(strstr(resp, "\"callees\""));
    /* HandleRequest has 0 callers → no caller_names array */
    ASSERT_NULL(strstr(resp, "\"caller_names\""));
    /* HandleRequest has 2 callees: ProcessOrder and Run */
    ASSERT_NOT_NULL(strstr(resp, "\"callee_names\""));
    ASSERT_NOT_NULL(strstr(resp, "ProcessOrder"));
    ASSERT_NOT_NULL(strstr(resp, "Run"));
    free(resp);

    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

/* ── TestSnippet_SourceInvalidUtf8 ────────────────────────────── */

TEST(snippet_source_invalid_utf8) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/project/main.go", tmp);
    FILE *fp = fopen(src_path, "wb");
    ASSERT_NOT_NULL(fp);
    const unsigned char source[] = {
        'p',  'a',  'c', 'k', 'a', 'g',  'e',  ' ',  'm',  'a',  'i',  'n', '\n', '\n',
        'f',  'u',  'n', 'c', ' ', 'H',  'a',  'n',  'd',  'l',  'e',  'R', 'e',  'q',
        'u',  'e',  's', 't', '(', ')',  ' ',  'e',  'r',  'r',  'o',  'r', ' ',  '{',
        '\n', '\t', '/', '/', ' ', 0xC0, 0xD4, 0xB7, 0xC2, '\n', '\t', 'r', 'e',  't',
        'u',  'r',  'n', ' ', 'n', 'i',  'l',  '\n', '}',  '\n'};
    ASSERT_EQ(fwrite(source, 1, sizeof(source), fp), sizeof(source));
    ASSERT_EQ(fclose(fp), 0);

    char *raw =
        cbm_mcp_handle_tool(srv, "get_code_snippet",
                            "{\"qualified_name\":\"test-project.cmd.server.main.HandleRequest\","
                            "\"project\":\"test-project\"}");
    ASSERT_TRUE(is_valid_json_response(raw));
    char *resp = extract_text_content(raw);
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(is_valid_json_response(resp));
    ASSERT_NULL(strstr(resp, "\xC0\xD4"));
    ASSERT_NOT_NULL(strstr(resp, "HandleRequest"));
    ASSERT_NOT_NULL(strstr(resp, "return nil"));
    ASSERT_TRUE(snippet_source_has_replacement(resp));

    free(resp);
    free(raw);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(snippet_budget_fitting_501_line_function_is_complete) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.FiveOhOne", 501,
                                        12, "def FiveOhOne():", false, false));
    char *response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "FiveOhOne\"}");
    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(snippet_json_bool(response, "source_complete"));
    ASSERT_EQ(snippet_json_int(response, "symbol_start_line", -1), 1);
    ASSERT_EQ(snippet_json_int(response, "symbol_end_line", -1), 501);
    char *source = snippet_json_string(response, "source");
    ASSERT_NOT_NULL(source);
    ASSERT_STR_EQ(source, fx.source);
    ASSERT_NULL(strstr(response, "next_cursor"));
    free(source);
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_source_that_exactly_fits_serialized_budget_is_complete) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.ExactFit", 40, 80,
                                        "def ExactFit():", false, false));
    const char *wide_args =
        "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
        "ExactFit\",\"max_response_bytes\":65536}";
    char *wide = cbm_mcp_handle_tool(fx.srv, "get_code_snippet", wide_args);
    ASSERT_NOT_NULL(wide);
    size_t exact_budget = strlen(wide);
    ASSERT_TRUE(exact_budget >= 2048 && exact_budget <= 65536);
    free(wide);

    char args[512];
    snprintf(args, sizeof(args),
             "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
             "ExactFit\",\"max_response_bytes\":%zu}",
             exact_budget);
    char *raw = cbm_mcp_handle_tool(fx.srv, "get_code_snippet", args);
    ASSERT_NOT_NULL(raw);
    ASSERT_EQ(strlen(raw), exact_budget);
    char *response = extract_text_content(raw);
    ASSERT_TRUE(snippet_json_bool(response, "source_complete"));
    char *source = snippet_json_string(response, "source");
    ASSERT_NOT_NULL(source);
    ASSERT_STR_EQ(source, fx.source);
    ASSERT_NULL(strstr(response, "next_cursor"));
    free(source);
    free(response);
    free(raw);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_2000_line_function_round_trips_across_byte_pages) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.Huge", 2000, 72,
                                        "def Huge():", false, false));
    char *rebuilt = malloc(fx.source_len + 1);
    ASSERT_NOT_NULL(rebuilt);
    size_t rebuilt_len = 0;
    char *cursor = NULL;
    int pages = 0;
    do {
        char args[1024];
        if (cursor) {
            snprintf(args, sizeof(args),
                     "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages."
                     "generated.Huge\",\"max_response_bytes\":4096,\"cursor\":\"%s\"}",
                     cursor);
        } else {
            snprintf(args, sizeof(args),
                     "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages."
                     "generated.Huge\",\"max_response_bytes\":4096}");
        }
        char *response = call_snippet(fx.srv, args);
        ASSERT_NOT_NULL(response);
        ASSERT_EQ(snippet_json_int(response, "symbol_end_line", -1), 2000);
        char *page = snippet_json_string(response, "source");
        ASSERT_NOT_NULL(page);
        size_t page_len = strlen(page);
        ASSERT_GT(page_len, 0);
        ASSERT_TRUE(rebuilt_len + page_len <= fx.source_len);
        memcpy(rebuilt + rebuilt_len, page, page_len);
        rebuilt_len += page_len;
        bool complete = snippet_json_bool(response, "source_complete");
        char *next = complete ? NULL : snippet_json_string(response, "next_cursor");
        ASSERT_TRUE(complete || next != NULL);
        free(cursor);
        cursor = next;
        free(page);
        free(response);
        pages++;
        ASSERT_TRUE(pages < 200);
    } while (cursor);
    rebuilt[rebuilt_len] = '\0';
    ASSERT_GT(pages, 1);
    ASSERT_EQ(rebuilt_len, fx.source_len);
    ASSERT_TRUE(memcmp(rebuilt, fx.source, fx.source_len) == 0);
    free(rebuilt);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_focus_and_explicit_range_are_symbol_bounded) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.Focus", 300, 120,
                                        "def Focus():", false, false));
    char *response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "Focus\",\"max_response_bytes\":4096,\"focus_line\":120}");
    ASSERT_NOT_NULL(response);
    long long source_start = snippet_json_int(response, "source_start_line", -1);
    long long source_end = snippet_json_int(response, "source_end_line", -1);
    ASSERT_TRUE(source_start <= 120 && source_end >= 120);
    char *source = snippet_json_string(response, "source");
    ASSERT_NOT_NULL(source);
    ASSERT_NOT_NULL(strstr(source, "line_0120"));
    free(source);
    free(response);

    response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "Focus\",\"start_line\":50,\"end_line\":55,\"max_response_bytes\":65536}");
    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(snippet_json_bool(response, "source_complete"));
    ASSERT_EQ(snippet_json_int(response, "symbol_start_line", -1), 1);
    ASSERT_EQ(snippet_json_int(response, "symbol_end_line", -1), 300);
    ASSERT_EQ(snippet_json_int(response, "source_start_line", -1), 50);
    ASSERT_EQ(snippet_json_int(response, "source_end_line", -1), 55);
    source = snippet_json_string(response, "source");
    ASSERT_NOT_NULL(strstr(source, "line_0050"));
    ASSERT_NOT_NULL(strstr(source, "line_0055"));
    ASSERT_NULL(strstr(source, "line_0056"));
    free(source);
    free(response);

    response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "Focus\",\"start_line\":301,\"end_line\":302}");
    ASSERT_NOT_NULL(strstr(response, "outside resolved symbol"));
    free(response);
    response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "Focus\",\"max_response_bytes\":65537}");
    ASSERT_NOT_NULL(strstr(response, "max_response_bytes"));
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_focus_window_contains_focus_when_leading_line_exceeds_budget) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.FocusLead", 30,
                                        40, "def FocusLead():", false, false));
    FILE *fp = fopen(fx.source_path, "wb");
    ASSERT_NOT_NULL(fp);
    for (int line = 1; line <= 30; line++) {
        if (line == 15) {
            ASSERT_EQ(fprintf(fp, "line_0015 "), 10);
            for (int i = 0; i < 7000; i++) {
                ASSERT_TRUE(fputc('x', fp) != EOF);
            }
            ASSERT_TRUE(fputc('\n', fp) != EOF);
        } else {
            ASSERT_GT(fprintf(fp, "line_%04d short\n", line), 0);
        }
    }
    ASSERT_EQ(fclose(fp), 0);

    char *response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "FocusLead\",\"max_response_bytes\":4096,\"focus_line\":20}");
    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(snippet_json_int(response, "source_start_line", -1) <= 20);
    ASSERT_TRUE(snippet_json_int(response, "source_end_line", -1) >= 20);
    char *source = snippet_json_string(response, "source");
    ASSERT_NOT_NULL(source);
    ASSERT_NOT_NULL(strstr(source, "line_0020"));
    free(source);
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_explicit_range_rejects_stale_physical_lines_without_clamping) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.StaleRange", 100,
                                        40, "def StaleRange():", false, false));
    FILE *fp = fopen(fx.source_path, "wb");
    ASSERT_NOT_NULL(fp);
    ASSERT_GT(fputs("def StaleRange():\n    return 1\n", fp), 0);
    ASSERT_EQ(fclose(fp), 0);

    char *response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "StaleRange\",\"start_line\":90,\"end_line\":95}");
    ASSERT_NOT_NULL(response);
    ASSERT_NOT_NULL(strstr(response, "source"));
    ASSERT_NOT_NULL(strstr(response, "stale"));
    ASSERT_NULL(strstr(response, "\"source\":\"\""));
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_cursor_rejects_stale_source_and_mismatched_symbol) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.CursorA", 800,
                                        100, "def CursorA():", false, false));
    char *first = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "CursorA\",\"max_response_bytes\":4096}");
    char *cursor = snippet_json_string(first, "next_cursor");
    ASSERT_NOT_NULL(cursor);
    cbm_node_t other = {0};
    other.project = "snippet-pages";
    other.label = "Function";
    other.name = "CursorB";
    other.qualified_name = "snippet-pages.generated.CursorB";
    other.file_path = "generated.py";
    other.start_line = 1;
    other.end_line = 800;
    ASSERT_GT(cbm_store_upsert_node(cbm_mcp_server_store(fx.srv), &other), 0);
    char args[1024];
    snprintf(args, sizeof(args),
             "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
             "CursorB\",\"max_response_bytes\":4096,\"cursor\":\"%s\"}",
             cursor);
    char *response = call_snippet(fx.srv, args);
    ASSERT_NOT_NULL(strstr(response, "cursor_mismatch"));
    free(response);

    FILE *fp = fopen(fx.source_path, "r+b");
    ASSERT_NOT_NULL(fp);
    ASSERT_EQ(fputc('X', fp), 'X');
    ASSERT_EQ(fclose(fp), 0);
    snprintf(args, sizeof(args),
             "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
             "CursorA\",\"max_response_bytes\":4096,\"cursor\":\"%s\"}",
             cursor);
    response = call_snippet(fx.srv, args);
    ASSERT_NOT_NULL(strstr(response, "stale_cursor"));
    free(response);
    free(cursor);
    free(first);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_cursor_rejects_malformed_and_offset_tampering) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.CursorGuard", 800,
                                        100, "def CursorGuard():", false, false));
    char *first = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "CursorGuard\",\"max_response_bytes\":4096}");
    char *cursor = snippet_json_string(first, "next_cursor");
    ASSERT_NOT_NULL(cursor);

    char *last_colon = strrchr(cursor, ':');
    ASSERT_NOT_NULL(last_colon);
    char *previous_colon = last_colon - 1;
    while (previous_colon > cursor && *previous_colon != ':') {
        previous_colon--;
    }
    ASSERT_TRUE(previous_colon > cursor);
    ASSERT_TRUE(previous_colon + 1 < last_colon);
    previous_colon[1] = previous_colon[1] == '9' ? '8' : (char)(previous_colon[1] + 1);

    char args[1024];
    snprintf(args, sizeof(args),
             "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
             "CursorGuard\",\"max_response_bytes\":4096,\"cursor\":\"%s\"}",
             cursor);
    char *response = call_snippet(fx.srv, args);
    ASSERT_NOT_NULL(response);
    ASSERT_NOT_NULL(strstr(response, "tamper"));
    free(response);

    response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "CursorGuard\",\"cursor\":\"sn2:a\"}");
    ASSERT_NOT_NULL(response);
    ASSERT_NOT_NULL(strstr(response, "invalid_cursor"));
    free(response);
    free(cursor);
    free(first);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_single_physical_line_larger_than_budget_is_fully_retrievable) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.OneLine", 1,
                                        40000, "def OneLine(): ", false, true));
    char *rebuilt = malloc(fx.source_len + 1);
    ASSERT_NOT_NULL(rebuilt);
    size_t used = 0;
    char *cursor = NULL;
    int pages = 0;
    do {
        char args[1024];
        snprintf(args, sizeof(args),
                 cursor ? "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages."
                          "generated.OneLine\",\"max_response_bytes\":4096,\"cursor\":\"%s\"}"
                        : "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages."
                          "generated.OneLine\",\"max_response_bytes\":4096}",
                 cursor ? cursor : "");
        char *response = call_snippet(fx.srv, args);
        char *page = snippet_json_string(response, "source");
        ASSERT_NOT_NULL(page);
        size_t n = strlen(page);
        ASSERT_GT(n, 0);
        memcpy(rebuilt + used, page, n);
        used += n;
        ASSERT_EQ(snippet_json_int(response, "source_start_line", -1), 1);
        ASSERT_EQ(snippet_json_int(response, "source_end_line", -1), 1);
        char *next = snippet_json_bool(response, "source_complete")
                         ? NULL
                         : snippet_json_string(response, "next_cursor");
        free(cursor);
        cursor = next;
        free(page);
        free(response);
        ASSERT_TRUE(++pages < 100);
    } while (cursor);
    ASSERT_EQ(used, fx.source_len);
    ASSERT_TRUE(memcmp(rebuilt, fx.source, used) == 0);
    free(rebuilt);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_serialized_result_never_exceeds_escaping_heavy_budget) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Function", "snippet-pages.generated.Escapes", 600,
                                        100, "def Escapes():", true, false));
    char *cursor = NULL;
    int pages = 0;
    do {
        char args[1024];
        snprintf(args, sizeof(args),
                 cursor ? "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages."
                          "generated.Escapes\",\"max_response_bytes\":4096,\"cursor\":\"%s\"}"
                        : "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages."
                          "generated.Escapes\",\"max_response_bytes\":4096}",
                 cursor ? cursor : "");
        char *raw = cbm_mcp_handle_tool(fx.srv, "get_code_snippet", args);
        ASSERT_NOT_NULL(raw);
        ASSERT_TRUE(strlen(raw) <= 4096);
        char *response = extract_text_content(raw);
        char *next = snippet_json_bool(response, "source_complete")
                         ? NULL
                         : snippet_json_string(response, "next_cursor");
        free(cursor);
        cursor = next;
        free(response);
        free(raw);
        ASSERT_TRUE(++pages < 200);
    } while (cursor);
    ASSERT_GT(pages, 1);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_ambiguous_suggestions_obey_serialized_result_budget) {
    char tmp[256];
    cbm_mcp_server_t *srv = setup_snippet_server(tmp, sizeof(tmp));
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    enum { AMBIGUOUS_MATCHES = 400 };
    char namespace_padding[321];
    memset(namespace_padding, 'x', sizeof(namespace_padding) - 1);
    namespace_padding[sizeof(namespace_padding) - 1] = '\0';
    for (int i = 0; i < AMBIGUOUS_MATCHES; i++) {
        char qn[512];
        snprintf(qn, sizeof(qn), "test-project.%s_%03d.Duplicate", namespace_padding, i);
        cbm_node_t node = {0};
        node.project = "test-project";
        node.label = "Function";
        node.name = "Duplicate";
        node.qualified_name = qn;
        node.file_path = "main.go";
        node.start_line = 1;
        node.end_line = 2;
        ASSERT_GT(cbm_store_upsert_node(store, &node), 0);
    }
    char *raw =
        cbm_mcp_handle_tool(srv, "get_code_snippet",
                            "{\"project\":\"test-project\",\"qualified_name\":\"Duplicate\","
                            "\"max_response_bytes\":2048}");
    ASSERT_NOT_NULL(raw);
    ASSERT_TRUE(strlen(raw) <= 2048);
    char *response = extract_text_content(raw);
    ASSERT_EQ(snippet_json_int(response, "suggestions_total", -1), AMBIGUOUS_MATCHES);
    ASSERT_TRUE(snippet_json_bool(response, "suggestions_truncated"));
    free(response);
    free(raw);
    cbm_mcp_server_free(srv);
    cleanup_snippet_dir(tmp);
    PASS();
}

TEST(snippet_oversized_module_returns_bounded_exact_outline) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Module", "snippet-pages.generated", 1400, 100,
                                        "ARBITRARY_PREFIX_MUST_NOT_ESCAPE", false, false));
    cbm_store_t *store = cbm_mcp_server_store(fx.srv);
    for (int i = 0; i < 100; i++) {
        char name[32];
        char qn[96];
        snprintf(name, sizeof(name), "member_%03d", i);
        snprintf(qn, sizeof(qn), "snippet-pages.generated.%s", name);
        cbm_node_t child = {0};
        child.project = "snippet-pages";
        child.label = "Function";
        child.name = name;
        child.qualified_name = qn;
        child.file_path = "generated.py";
        child.start_line = 10 + i * 10;
        child.end_line = child.start_line + 2;
        ASSERT_GT(cbm_store_upsert_node(store, &child), 0);
    }
    char *raw = cbm_mcp_handle_tool(
        fx.srv, "get_code_snippet",
        "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated\","
        "\"max_response_bytes\":4096}");
    ASSERT_NOT_NULL(raw);
    ASSERT_TRUE(strlen(raw) <= 4096);
    char *response = extract_text_content(raw);
    ASSERT_NOT_NULL(strstr(response, "\"mode\":\"outline\""));
    ASSERT_EQ(snippet_json_int(response, "outline_total", -1), 100);
    ASSERT_TRUE(snippet_json_int(response, "outline_returned", -1) < 100);
    ASSERT_TRUE(snippet_json_bool(response, "outline_truncated"));
    ASSERT_NOT_NULL(strstr(response, "member_000"));
    ASSERT_NULL(strstr(response, "ARBITRARY_PREFIX_MUST_NOT_ESCAPE"));
    ASSERT_NULL(strstr(response, "\"source\""));
    free(response);
    free(raw);

    response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated\","
                "\"start_line\":1,\"end_line\":2,\"max_response_bytes\":65536}");
    ASSERT_NOT_NULL(strstr(response, "\"source\""));
    ASSERT_NOT_NULL(strstr(response, "ARBITRARY_PREFIX_MUST_NOT_ESCAPE"));
    ASSERT_NULL(strstr(response, "\"mode\":\"outline\""));
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_oversized_class_returns_declaration_and_member_outline) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Class", "snippet-pages.generated.LargeClass", 800,
                                        100, "class LargeClass:", false, false));
    cbm_store_t *store = cbm_mcp_server_store(fx.srv);
    for (int i = 0; i < 12; i++) {
        char name[32];
        char qn[128];
        snprintf(name, sizeof(name), "method_%02d", i);
        snprintf(qn, sizeof(qn), "snippet-pages.generated.LargeClass.%s", name);
        cbm_node_t method = {0};
        method.project = "snippet-pages";
        method.label = "Method";
        method.name = name;
        method.qualified_name = qn;
        method.file_path = "generated.py";
        method.start_line = 20 + i * 40;
        method.end_line = method.start_line + 20;
        ASSERT_GT(cbm_store_upsert_node(store, &method), 0);
    }
    char *response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated."
                "LargeClass\"}");
    ASSERT_NOT_NULL(strstr(response, "\"mode\":\"outline\""));
    ASSERT_NOT_NULL(strstr(response, "\"declaration\":\"class LargeClass:\""));
    ASSERT_EQ(snippet_json_int(response, "outline_total", -1), 12);
    ASSERT_NOT_NULL(strstr(response, "method_00"));
    ASSERT_NULL(strstr(response, "line_0799"));
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_partial_coverage_guidance_matches_response_mode) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Module", "snippet-pages.generated", 1400, 100,
                                        "GRAPH_ONLY_MUST_NOT_CLAIM_SOURCE", false, false));
    cbm_coverage_row_t coverage = {
        .rel_path = "generated.py", .kind = "parse_partial", .detail = "20-30"};
    ASSERT_EQ(cbm_store_upsert_file_hash(cbm_mcp_server_store(fx.srv), "snippet-pages",
                                         "generated.py", "fixture", 1, (int64_t)fx.source_len),
              CBM_STORE_OK);
    ASSERT_EQ(
        cbm_store_coverage_replace(cbm_mcp_server_store(fx.srv), "snippet-pages", &coverage, 1),
        CBM_STORE_OK);
    char *response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated\","
                "\"max_response_bytes\":2048}");
    ASSERT_NOT_NULL(strstr(response, "\"mode\":\"outline\""));
    ASSERT_NOT_NULL(strstr(response, "graph-only outline"));
    ASSERT_NOT_NULL(strstr(response, "raw source"));
    ASSERT_NULL(strstr(response, "source above is ground truth"));
    free(response);

    response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated\","
                "\"start_line\":1,\"end_line\":2,\"max_response_bytes\":65536}");
    ASSERT_NOT_NULL(strstr(response, "source above is ground truth"));
    ASSERT_NULL(strstr(response, "graph-only outline"));
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

TEST(snippet_class_outline_excludes_adjacent_top_level_symbol_on_closing_line) {
    generated_snippet_t fx;
    ASSERT_TRUE(generated_snippet_setup(&fx, "Class", "snippet-pages.generated.A", 800, 100,
                                        "class A {}; void outside() {}", false, false));
    cbm_store_t *store = cbm_mcp_server_store(fx.srv);
    cbm_node_t member = {.project = "snippet-pages",
                         .label = "Method",
                         .name = "inside",
                         .qualified_name = "snippet-pages.generated.A.inside",
                         .file_path = "generated.py",
                         .start_line = 10,
                         .end_line = 20};
    cbm_node_t outside = {.project = "snippet-pages",
                          .label = "Function",
                          .name = "outside",
                          .qualified_name = "snippet-pages.generated.outside",
                          .file_path = "generated.py",
                          .start_line = 800,
                          .end_line = 800};
    ASSERT_GT(cbm_store_upsert_node(store, &member), 0);
    ASSERT_GT(cbm_store_upsert_node(store, &outside), 0);
    char *response = call_snippet(
        fx.srv, "{\"project\":\"snippet-pages\",\"qualified_name\":\"snippet-pages.generated.A\","
                "\"max_response_bytes\":2048}");
    ASSERT_NOT_NULL(strstr(response, "\"mode\":\"outline\""));
    ASSERT_NOT_NULL(strstr(response, "inside"));
    ASSERT_NULL(strstr(response, "\"outside\""));
    free(response);
    generated_snippet_cleanup(&fx);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  JSON-RPC PARSING — EDGE CASES
 * ══════════════════════════════════════════════════════════════════ */

TEST(jsonrpc_parse_empty_string) {
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse("", &req);
    ASSERT_EQ(rc, -1);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_missing_jsonrpc_field) {
    /* jsonrpc field absent — parser defaults to "2.0" if method present */
    const char *line = "{\"id\":1,\"method\":\"initialize\",\"params\":{}}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(req.jsonrpc, "2.0");
    ASSERT_STR_EQ(req.method, "initialize");
    ASSERT_TRUE(req.has_id);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_missing_method) {
    /* method is required — should fail */
    const char *line = "{\"jsonrpc\":\"2.0\",\"id\":1,\"params\":{}}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, -1);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_string_id) {
    /* JSON-RPC §4: string and numeric ids are distinct. A string id is
     * preserved verbatim (issue #253), never coerced to a number. */
    const char *line = "{\"jsonrpc\":\"2.0\",\"id\":\"99\",\"method\":\"tools/list\"}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(req.has_id);
    ASSERT_NOT_NULL(req.id_str);
    ASSERT_STR_EQ(req.id_str, "99");
    ASSERT_STR_EQ(req.method, "tools/list");
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_no_params) {
    /* Request with no params field — params_raw should be NULL */
    const char *line = "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/list\"}";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(req.params_raw);
    ASSERT_EQ(req.id, 5);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_extra_whitespace) {
    /* Leading/trailing whitespace and internal spacing in JSON */
    const char *line = "  { \"jsonrpc\" : \"2.0\" , \"id\" : 7 , \"method\" : \"ping\" }  ";
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse(line, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(req.id, 7);
    ASSERT_STR_EQ(req.method, "ping");
    cbm_jsonrpc_request_free(&req);
    PASS();
}

TEST(jsonrpc_parse_array_not_object) {
    /* JSON array at root — not a valid JSON-RPC request */
    cbm_jsonrpc_request_t req = {0};
    int rc = cbm_jsonrpc_parse("[1,2,3]", &req);
    ASSERT_EQ(rc, -1);
    cbm_jsonrpc_request_free(&req);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  ARGUMENT EXTRACTION — EDGE CASES
 * ══════════════════════════════════════════════════════════════════ */

TEST(mcp_get_string_arg_empty_json) {
    /* Empty JSON string — yyjson_read fails → NULL */
    char *val = cbm_mcp_get_string_arg("", "key");
    ASSERT_NULL(val);
    PASS();
}

TEST(mcp_get_string_arg_empty_object) {
    /* Valid JSON with no keys → NULL for any key */
    char *val = cbm_mcp_get_string_arg("{}", "key");
    ASSERT_NULL(val);
    PASS();
}

TEST(mcp_get_string_arg_nested_value) {
    /* Value is an object, not a string → should return NULL */
    const char *args = "{\"config\":{\"nested\":true},\"name\":\"hello\"}";
    char *val = cbm_mcp_get_string_arg(args, "config");
    ASSERT_NULL(val); /* not a string type */
    val = cbm_mcp_get_string_arg(args, "name");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val, "hello");
    free(val);
    PASS();
}

TEST(mcp_get_string_arg_int_value) {
    /* Value is an integer, not a string → NULL */
    char *val = cbm_mcp_get_string_arg("{\"count\":42}", "count");
    ASSERT_NULL(val);
    PASS();
}

TEST(mcp_get_int_arg_empty_json) {
    int val = cbm_mcp_get_int_arg("", "key", 99);
    ASSERT_EQ(val, 99);
    PASS();
}

TEST(mcp_get_int_arg_string_value) {
    /* Value is a string, not int → should return default */
    int val = cbm_mcp_get_int_arg("{\"limit\":\"ten\"}", "limit", 5);
    ASSERT_EQ(val, 5);
    PASS();
}

TEST(mcp_get_int_arg_bool_value) {
    /* Value is a bool, not int → default */
    int val = cbm_mcp_get_int_arg("{\"flag\":true}", "flag", -1);
    ASSERT_EQ(val, -1);
    PASS();
}

TEST(mcp_get_bool_arg_empty_json) {
    bool val = cbm_mcp_get_bool_arg("", "key");
    ASSERT_FALSE(val);
    PASS();
}

TEST(mcp_get_bool_arg_int_value) {
    /* Value is int 1, not bool → should return false */
    bool val = cbm_mcp_get_bool_arg("{\"flag\":1}", "flag");
    ASSERT_FALSE(val);
    PASS();
}

TEST(mcp_get_tool_name_empty_json) {
    char *name = cbm_mcp_get_tool_name("");
    ASSERT_NULL(name);
    PASS();
}

TEST(mcp_get_tool_name_missing_name) {
    char *name = cbm_mcp_get_tool_name("{\"arguments\":{}}");
    ASSERT_NULL(name);
    PASS();
}

TEST(mcp_get_arguments_empty_json) {
    char *args = cbm_mcp_get_arguments("");
    ASSERT_NULL(args);
    PASS();
}

TEST(mcp_get_arguments_no_arguments_key) {
    /* No "arguments" key → returns "{}" */
    char *args = cbm_mcp_get_arguments("{\"name\":\"tool\"}");
    ASSERT_NOT_NULL(args);
    ASSERT_STR_EQ(args, "{}");
    free(args);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  FILE URI PARSING — EDGE CASES
 * ══════════════════════════════════════════════════════════════════ */

TEST(parse_file_uri_http_scheme) {
    char path[256];
    ASSERT_FALSE(cbm_parse_file_uri("http://example.com/path", path, sizeof(path)));
    ASSERT_STR_EQ(path, "");
    PASS();
}

TEST(parse_file_uri_ftp_scheme) {
    char path[256];
    ASSERT_FALSE(cbm_parse_file_uri("ftp://server/file.txt", path, sizeof(path)));
    ASSERT_STR_EQ(path, "");
    PASS();
}

TEST(parse_file_uri_buffer_too_small) {
    char path[5]; /* only 5 bytes — path gets truncated */
    ASSERT_TRUE(cbm_parse_file_uri("file:///usr/local/bin", path, sizeof(path)));
    /* snprintf truncates to 4 chars + NUL */
    ASSERT_EQ(strlen(path), 4);
    ASSERT_STR_EQ(path, "/usr");
    PASS();
}

TEST(parse_file_uri_spaces_in_path) {
    char path[256];
    ASSERT_TRUE(cbm_parse_file_uri("file:///home/user/my%20project", path, sizeof(path)));
    /* Raw percent-encoding is preserved (not decoded) */
    ASSERT_STR_EQ(path, "/home/user/my%20project");
    PASS();
}

TEST(parse_file_uri_null_out_path) {
    /* NULL out_path — should not crash */
    ASSERT_FALSE(cbm_parse_file_uri("file:///tmp", NULL, 256));
    PASS();
}

TEST(parse_file_uri_zero_size) {
    char path[256] = "garbage";
    /* out_size=0 → should fail safely */
    ASSERT_FALSE(cbm_parse_file_uri("file:///tmp", path, 0));
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  SERVER HANDLE — EDGE CASES
 * ══════════════════════════════════════════════════════════════════ */

TEST(server_handle_invalid_json) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    char *resp = cbm_mcp_server_handle(srv, "this is not json at all");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"error\""));
    ASSERT_NOT_NULL(strstr(resp, "-32700")); /* Parse error */
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_empty_object) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    /* Valid JSON but no method field → parse error */
    char *resp = cbm_mcp_server_handle(srv, "{}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "\"error\""));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

TEST(server_handle_tools_call_missing_name) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);

    /* tools/call with no tool name in params */
    char *resp =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":50,\"method\":\"tools/call\","
                                   "\"params\":{\"arguments\":{}}}");
    ASSERT_NOT_NULL(resp);
    /* Should return error about unknown/missing tool */
    ASSERT_NOT_NULL(strstr(resp, "\"id\":50"));
    ASSERT_TRUE(strstr(resp, "error") || strstr(resp, "isError") || strstr(resp, "unknown"));
    free(resp);

    cbm_mcp_server_free(srv);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  POLL/GETLINE FILE* BUFFERING FIX
 * ══════════════════════════════════════════════════════════════════ */

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>

/* Signal handler used by alarm() to abort the test if it hangs */
static void alarm_handler(int sig) {
    (void)sig;
    /* Writing to stderr is async-signal-safe */
    const char msg[] = "FAIL: mcp_server_run_rapid_messages timed out (>5s)\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(1);
}

TEST(mcp_server_run_rapid_messages) {
    /* Simulate a client sending initialize + notifications/initialized +
     * tools/list all at once (no delays), which exercises the FILE*
     * buffering fix: the first getline() over-reads kernel data into the
     * libc buffer; without the fix, subsequent poll() calls block for 60s.
     *
     * We use alarm(5) to abort the test process if the server hangs. */
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    /* Write all 3 messages to the write end in one shot */
    const char *msgs = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                       "\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{}}}\n"
                       "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
                       "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n";
    ssize_t written = write(fds[1], msgs, strlen(msgs));
    ASSERT_TRUE(written > 0);
    close(fds[1]); /* EOF signals end of input to the server */

    FILE *in_fp = fdopen(fds[0], "r");
    ASSERT_NOT_NULL(in_fp);

    FILE *out_fp = tmpfile();
    ASSERT_NOT_NULL(out_fp);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    /* Install alarm to fail the test if cbm_mcp_server_run blocks */
    signal(SIGALRM, alarm_handler);
    alarm(5);

    int rc = cbm_mcp_server_run(srv, in_fp, out_fp);

    alarm(0); /* cancel alarm */
    signal(SIGALRM, SIG_DFL);

    ASSERT_EQ(rc, 0);

    /* Verify both responses are present:
     *   id:1 — initialize response
     *   id:2 — tools/list response (notifications/initialized produces none)
     * and that the tools list payload is included. */
    rewind(out_fp);
    char buf[4096] = {0};
    size_t nread = fread(buf, 1, sizeof(buf) - 1, out_fp);
    ASSERT_TRUE(nread > 0);
    ASSERT_NOT_NULL(strstr(buf, "\"id\":1"));
    ASSERT_NOT_NULL(strstr(buf, "\"id\":2"));
    ASSERT_NOT_NULL(strstr(buf, "tools"));

    cbm_mcp_server_free(srv);
    fclose(out_fp);
    /* in_fp already EOF; fclose cleans up */
    fclose(in_fp);
    PASS();
}
#endif /* !_WIN32 */

/* Issue #235: passing an unrecognised project name to a tool crashed the
 * binary with a buffer overflow while building the "available_projects"
 * error list — collect_db_project_names overflowed projects[CBM_SZ_4K] via
 * an unsigned underflow on (out_sz - offset) once the listed names exceeded
 * the buffer. Fill a temp cache dir with enough long-named .db files to
 * exceed 4 KB, then hit the bad-project path. Under ASan a regression aborts
 * here; the fixed bounds-check keeps it clean and returns a normal error. */
#define ISSUE235_DBNAME(buf, dir, i)                                                         \
    snprintf((buf), sizeof(buf),                                                             \
             "%s/proj_%02d_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.db",                      \
             (dir), (i))
TEST(tool_bad_project_name_no_overflow_issue235) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-badproj-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS(); /* skip if mkdtemp fails */
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    /* 40 * ~120-char names overflows the 4 KB available-projects buffer.
     * collect_db_project_names advertises each db's INTERNAL project name
     * (#704), so the fixture must hold valid dbs with long internal names —
     * not stub files — for the bounds-check path to actually be exercised. */
    enum { ISSUE235_N = 40 };
    for (int i = 0; i < ISSUE235_N; i++) {
        char name[512];
        ISSUE235_DBNAME(name, cache, i);
        char iname[256];
        snprintf(iname, sizeof(iname),
                 "proj_%02d_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                 "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                 i);
        cbm_store_t *st = cbm_store_open_path(name);
        if (st) {
            cbm_store_upsert_project(st, iname, cache);
            cbm_store_close(st);
        }
    }

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":"
             "\"search_graph\",\"arguments\":{\"label\":\"Function\","
             "\"project\":\"definitely-not-a-real-project-xyz\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "not found"));
    free(resp);
    cbm_mcp_server_free(srv);

    if (saved_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_copy, 1);
        free(saved_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    for (int i = 0; i < ISSUE235_N; i++) {
        char name[512];
        ISSUE235_DBNAME(name, cache, i);
        cbm_unlink(name);
        char side[540];
        snprintf(side, sizeof(side), "%s-wal", name);
        cbm_unlink(side);
        snprintf(side, sizeof(side), "%s-shm", name);
        cbm_unlink(side);
    }
    cbm_rmdir(cache);
    PASS();
}
#undef ISSUE235_DBNAME

/* Issue #235 (follow-up): with many long-named projects indexed,
 * collect_db_project_names overflowed projects[CBM_SZ_4K] and truncated the
 * LAST name MID-TOKEN, then clamped offset to out_sz-1 — emitting malformed,
 * unterminated JSON like
 *   ...,"available_projects":["a",...,"vjson_49_bbb],"count":50}
 * (unclosed string + unclosed array). build_project_list_error wrapped that
 * invalid body into the tool error, so a "project not found" reply was NOT
 * valid JSON once enough projects were indexed.
 *
 * Reproduce-first: fill an isolated cache dir with enough long INTERNAL-named
 * dbs to overflow the 4 KB buffer, hit the bad-project path, then assert the
 * ERROR BODY (the inner MCP text content) parses as valid JSON and that
 * available_projects is a JSON array whose length == count. RED on the
 * truncating code (yyjson_read returns NULL on the mid-token cut); GREEN after
 * the element-boundary fix, which only ever writes whole "name" tokens. */
#define BADPROJ_JSON_DBNAME(buf, dir, i)                                                      \
    snprintf((buf), sizeof(buf),                                                              \
             "%s/vjson_%02d_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.db",                       \
             (dir), (i))
TEST(tool_bad_project_error_valid_json_issue235) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-badproj-vjson-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS(); /* skip if mkdtemp fails */
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    /* 50 * ~120-char INTERNAL names >> 4 KB → the available_projects buffer
     * overflows and the last name is cut mid-token on the unfixed code. */
    enum { BADPROJ_N = 50 };
    for (int i = 0; i < BADPROJ_N; i++) {
        char name[512];
        BADPROJ_JSON_DBNAME(name, cache, i);
        char iname[256];
        snprintf(iname, sizeof(iname),
                 "vjson_%02d_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                 "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                 i);
        cbm_store_t *st = cbm_store_open_path(name);
        if (st) {
            cbm_store_upsert_project(st, iname, cache);
            cbm_store_close(st);
        }
    }

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":"
             "\"search_graph\",\"arguments\":{\"label\":\"Function\","
             "\"project\":\"definitely-not-a-real-project-xyz\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "not found"));

    /* The inner MCP text content is the error body built by
     * build_project_list_error. Capture its validity BEFORE cleanup so a RED
     * failure still restores the environment. */
    char *body = extract_text_content(resp);
    bool body_valid = false;
    bool aps_ok = false; /* available_projects is an array whose len == count */
    if (body) {
        yyjson_doc *bdoc = yyjson_read(body, strlen(body), 0);
        if (bdoc) {
            body_valid = true;
            yyjson_val *broot = yyjson_doc_get_root(bdoc);
            yyjson_val *aps = yyjson_obj_get(broot, "available_projects");
            yyjson_val *cnt = yyjson_obj_get(broot, "count");
            if (aps && yyjson_is_arr(aps) && cnt && yyjson_is_int(cnt)) {
                aps_ok = (yyjson_arr_size(aps) == (size_t)yyjson_get_int(cnt));
            }
            yyjson_doc_free(bdoc);
        }
    }
    free(body);
    free(resp);
    cbm_mcp_server_free(srv);

    if (saved_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_copy, 1);
        free(saved_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    for (int i = 0; i < BADPROJ_N; i++) {
        char name[512];
        BADPROJ_JSON_DBNAME(name, cache, i);
        cbm_unlink(name);
        char side[540];
        snprintf(side, sizeof(side), "%s-wal", name);
        cbm_unlink(side);
        snprintf(side, sizeof(side), "%s-shm", name);
        cbm_unlink(side);
    }
    cbm_rmdir(cache);

    /* RED on the unfixed code: mid-token truncation → invalid JSON body. */
    ASSERT_TRUE(body_valid);
    ASSERT_TRUE(aps_ok);
    PASS();
}
#undef BADPROJ_JSON_DBNAME

/* ── #704: project resolution must key on the db's INTERNAL project name ──
 *
 * Issue #704: project resolution is registry-less and filename-addressed.
 * resolve_store() opens <cache>/<passed>.db and then requires the internal
 * `projects.name` row to equal the passed name; list_projects /
 * collect_db_project_names derive the advertised name from the .db FILENAME.
 * When a db's filename != its internal name (a legacy '.'-vs-'-' username
 * twin, or a copied/renamed file) it shows up in list_projects under the
 * filename, but every query returns "project not found" — node rows are
 * tagged with the INTERNAL name, so neither the filename nor the resolve
 * path lines up. The fix makes list + resolve both key on the INTERNAL name.
 *
 * Reproduce-first fixture in an isolated CBM_CACHE_DIR:
 *   - alpha704.db  : filename == internal name "alpha704"   (control / fast path)
 *   - gamma704.db  : internal name "beta704"                (DRIFT: built as
 *                    beta704.db then renamed → filename != internal name)
 *   - ghost704.db  : 0-byte file                            (ghost / unresolvable)
 *
 * RED on buggy code / GREEN on the fix:
 *   A. list_projects advertises "beta704" (internal), NOT "gamma704" (filename),
 *      and NOT "ghost704" (0-byte filtered).
 *   B. search_graph(project="beta704") resolves via the cache-dir scan and
 *      returns the node — not the "project not found" error.
 *   C. control project "alpha704" still resolves on the fast path.
 *   D. the 0-byte ghost is not resolvable.
 *   E. addressing the drifted db by its FILENAME ("gamma704") stays not-found
 *      (we key on the internal name, never the file on disk).
 */

/* Create a file-backed project db at <dir>/<filename> whose INTERNAL project
 * name is `internal` (which may differ from the filename), holding one
 * Function node named `fn`. Returns true on success. */
static bool issue704_make_db(const char *dir, const char *filename, const char *internal,
                             const char *fn) {
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    cbm_store_t *st = cbm_store_open_path(path);
    if (!st) {
        return false;
    }
    bool ok = (cbm_store_upsert_project(st, internal, dir) == CBM_STORE_OK);
    if (ok) {
        char qn[256];
        snprintf(qn, sizeof(qn), "%s.%s", internal, fn);
        cbm_node_t n = {0};
        n.project = internal;
        n.label = "Function";
        n.name = fn;
        n.qualified_name = qn;
        n.file_path = "main.go";
        n.start_line = 1;
        n.end_line = 2;
        ok = (cbm_store_upsert_node(st, &n) > 0);
    }
    cbm_store_close(st);
    return ok;
}

TEST(tool_resolve_store_by_internal_name_issue704) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-issue704-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS(); /* skip if mkdtemp fails — not a #704 signal */
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    /* (1) control: filename == internal name */
    ASSERT_TRUE(issue704_make_db(cache, "alpha704.db", "alpha704", "alphaFunc704"));

    /* (2) DRIFT: build beta704.db (internal "beta704") then rename the file to
     *     gamma704.db, so filename "gamma704" != internal "beta704". */
    ASSERT_TRUE(issue704_make_db(cache, "beta704.db", "beta704", "betaFunc704"));
    char beta_path[700];
    char gamma_path[700];
    snprintf(beta_path, sizeof(beta_path), "%s/beta704.db", cache);
    snprintf(gamma_path, sizeof(gamma_path), "%s/gamma704.db", cache);
    ASSERT_EQ(rename(beta_path, gamma_path), 0);

    /* (3) ghost: 0-byte db file */
    char ghost_path[700];
    snprintf(ghost_path, sizeof(ghost_path), "%s/ghost704.db", cache);
    FILE *gp = fopen(ghost_path, "w");
    ASSERT_NOT_NULL(gp);
    fclose(gp);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    /* ── A: list_projects reports INTERNAL names; filters the ghost ── */
    char *list =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"list_projects\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(list);
    ASSERT_NOT_NULL(strstr(list, "alpha704")); /* control */
    ASSERT_NOT_NULL(strstr(list, "beta704"));  /* internal name of drifted db (RED before) */
    ASSERT_NULL(strstr(list, "gamma704"));     /* filename must NOT be advertised (RED before) */
    ASSERT_NULL(strstr(list, "ghost704"));     /* 0-byte ghost filtered (RED before) */
    free(list);

    /* ── B: the drifted project resolves by its INTERNAL name ──────── */
    char *q_beta = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\",\"arguments\":{"
             "\"project\":\"beta704\",\"name_pattern\":\"betaFunc704\",\"limit\":5}}}");
    ASSERT_NOT_NULL(q_beta);
    ASSERT_NOT_NULL(strstr(q_beta, "betaFunc704")); /* resolved + returned node (RED before) */
    ASSERT_NULL(strstr(q_beta, "not found"));       /* not the not-found error */
    free(q_beta);

    /* ── C: control project still resolves on the fast path ────────── */
    char *q_alpha = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\",\"arguments\":{"
             "\"project\":\"alpha704\",\"name_pattern\":\"alphaFunc704\",\"limit\":5}}}");
    ASSERT_NOT_NULL(q_alpha);
    ASSERT_NOT_NULL(strstr(q_alpha, "alphaFunc704"));
    free(q_alpha);

    /* ── D: the 0-byte ghost is NOT resolvable ─────────────────────── */
    char *q_ghost = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\",\"arguments\":{"
             "\"project\":\"ghost704\",\"name_pattern\":\".*\",\"limit\":5}}}");
    ASSERT_NOT_NULL(q_ghost);
    ASSERT_NOT_NULL(strstr(q_ghost, "not found"));
    free(q_ghost);

    /* ── E: addressing the drifted db by its FILENAME stays not-found ── */
    char *q_gamma = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\",\"arguments\":{"
             "\"project\":\"gamma704\",\"name_pattern\":\".*\",\"limit\":5}}}");
    ASSERT_NOT_NULL(q_gamma);
    ASSERT_NOT_NULL(strstr(q_gamma, "not found"));
    free(q_gamma);

    cbm_mcp_server_free(srv);

    /* ── cleanup ───────────────────────────────────────────────────── */
    if (saved_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_copy, 1);
        free(saved_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    char a_path[700];
    snprintf(a_path, sizeof(a_path), "%s/alpha704.db", cache);
    cbm_unlink(a_path);
    cbm_unlink(gamma_path);
    cbm_unlink(ghost_path);
    mcp_cleanup_corrupt_backups(cache, "ghost704");
    char side[740];
    snprintf(side, sizeof(side), "%s-wal", a_path);
    cbm_unlink(side);
    snprintf(side, sizeof(side), "%s-shm", a_path);
    cbm_unlink(side);
    snprintf(side, sizeof(side), "%s-wal", gamma_path);
    cbm_unlink(side);
    snprintf(side, sizeof(side), "%s-shm", gamma_path);
    cbm_unlink(side);
    cbm_rmdir(cache);
    PASS();
}

/* ── #1044: a "<name>::missed" shadow row must not hide the project ──
 *
 * The miss-graph pass inserts a second `projects` row ("<name>::missed") so
 * its nodes satisfy the FK on nodes.project. db_internal_project_name
 * required the projects table to hold EXACTLY ONE row, so any project with
 * a miss graph vanished from list_projects and the graph UI, and the
 * fallback-scan resolve path failed.
 *
 * RED on buggy code / GREEN on the fix:
 *   A. list_projects still advertises "delta1044" while the shadow row exists.
 *   B. the shadow name itself is never advertised.
 *   C. search_graph(project="delta1044") still resolves and returns the node.
 */
TEST(tool_list_projects_ignores_missed_shadow_issue1044) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-issue1044-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        PASS(); /* skip if mkdtemp fails — not a #1044 signal */
    }

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    ASSERT_TRUE(issue704_make_db(cache, "delta1044.db", "delta1044", "deltaFunc1044"));

    /* Add the shadow row exactly the way the miss-graph pass does. */
    char db_path[700];
    snprintf(db_path, sizeof(db_path), "%s/delta1044.db", cache);
    cbm_store_t *st = cbm_store_open_path(db_path);
    ASSERT_NOT_NULL(st);
    ASSERT_EQ(cbm_store_upsert_project(st, "delta1044::missed", ""), CBM_STORE_OK);
    cbm_store_close(st);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    /* ── A + B: primary advertised, shadow hidden ─────────────────── */
    char *list =
        cbm_mcp_server_handle(srv, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                                   "\"params\":{\"name\":\"list_projects\",\"arguments\":{}}}");
    ASSERT_NOT_NULL(list);
    ASSERT_NOT_NULL(strstr(list, "delta1044")); /* RED before: db skipped as ghost */
    ASSERT_NULL(strstr(list, "::missed"));      /* shadow never advertised */
    free(list);

    /* ── C: the project still resolves and returns its node ───────── */
    char *q = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"search_graph\",\"arguments\":{"
             "\"project\":\"delta1044\",\"name_pattern\":\"deltaFunc1044\",\"limit\":5}}}");
    ASSERT_NOT_NULL(q);
    ASSERT_NOT_NULL(strstr(q, "deltaFunc1044"));
    ASSERT_NULL(strstr(q, "not found"));
    free(q);

    cbm_mcp_server_free(srv);

    /* ── cleanup ───────────────────────────────────────────────────── */
    if (saved_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_copy, 1);
        free(saved_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }
    cbm_unlink(db_path);
    char side1044[740];
    snprintf(side1044, sizeof(side1044), "%s-wal", db_path);
    cbm_unlink(side1044);
    snprintf(side1044, sizeof(side1044), "%s-shm", db_path);
    cbm_unlink(side1044);
    cbm_rmdir(cache);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  QUERY STORE COHERENCE + READ-ONLY  (data-integrity reproductions)
 *
 *  Bug: query tools resolve the project store via resolve_store() ->
 *  cbm_store_open_path_query(), which opens the DB SQLITE_OPEN_READWRITE
 *  and runs configure_pragmas() with the WRITE pragmas
 *  (journal_mode=WAL + wal_checkpoint + synchronous). Two consequences:
 *    (a) read-only query tools MUTATE the on-disk DB (write pragmas), and
 *    (b) query tools FAIL outright on a read-only DB file / filesystem
 *        (the READWRITE open returns CANTOPEN -> resolve_store NULL ->
 *        "project not found").
 *  Both read-only tests below are written reproduce-first and are RED on the
 *  unfixed code, GREEN once query opens are READONLY with read-only
 *  pragmas.
 * ══════════════════════════════════════════════════════════════════ */

/* Reproduce-first: one MCP session caches a query connection to generation A,
 * then the fixture models an independent writer publishing generation B by
 * atomically replacing the project DB at the same cache path. Because
 * resolve_store() keys its cache only by project name, the next query can reuse
 * stale generation A. It must instead return generation B. */
TEST(query_store_reopens_after_database_replacement) {
    static const char project[] = "cbm-store-generation-refresh";
    static const char active_filename[] = "cbm-store-generation-refresh.db";
    static const char staged_filename[] = "cbm-store-generation-next.db";

    char cache[512];
    snprintf(cache, sizeof(cache), "%s/cbm-store-generation-XXXXXX", cbm_tmpdir());
    bool cache_ready = cbm_mkdtemp(cache) != NULL;
    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    if (cache_ready) {
        cbm_setenv("CBM_CACHE_DIR", cache, 1);
    }

    bool generation_a_ready =
        cache_ready && issue704_make_db(cache, active_filename, project, "GenerationA");
    cbm_mcp_server_t *srv = generation_a_ready ? cbm_mcp_server_new(NULL) : NULL;
    bool server_ready = srv != NULL;

    char args[512];
    snprintf(args, sizeof(args),
             "{\"project\":\"%s\",\"name_pattern\":\".*Generation.*\",\"limit\":10}", project);
    char *before = srv ? cbm_mcp_handle_tool(srv, "search_graph", args) : NULL;
    bool saw_generation_a = before && strstr(before, "GenerationA") != NULL;

    bool generation_b_ready =
        cache_ready && issue704_make_db(cache, staged_filename, project, "GenerationB");
    char active_path[700];
    char staged_path[700];
    snprintf(active_path, sizeof(active_path), "%s/%s", cache, active_filename);
    snprintf(staged_path, sizeof(staged_path), "%s/%s", cache, staged_filename);
    bool replaced = generation_b_ready && cbm_rename_replace(staged_path, active_path) == 0;

    char *after = (srv && replaced) ? cbm_mcp_handle_tool(srv, "search_graph", args) : NULL;
    bool saw_generation_b = after && strstr(after, "GenerationB") != NULL;
    bool retained_generation_a = after && strstr(after, "GenerationA") != NULL;

    free(before);
    free(after);
    if (srv) {
        cbm_mcp_server_free(srv);
    }
    if (cache_ready) {
        cleanup_project_db(cache, project);
        cleanup_project_db(cache, "cbm-store-generation-next");
        cbm_rmdir(cache);
    }
    restore_cache_dir(saved_copy);
    free(saved_copy);

    ASSERT_TRUE(cache_ready);
    ASSERT_TRUE(generation_a_ready);
    ASSERT_TRUE(server_ready);
    ASSERT_TRUE(saw_generation_a);
    ASSERT_TRUE(generation_b_ready);
    ASSERT_TRUE(replaced);
    ASSERT_TRUE(saw_generation_b);
    ASSERT_FALSE(retained_generation_a);
    PASS();
}

#define ROQ_PROJECT "cbm-roq-test"

/* Whole-file byte snapshot. Returns malloc'd buffer (caller frees) and
 * writes the length to *out_len. Returns NULL on failure. */
static unsigned char *roq_read_file_bytes(const char *path, long *out_len) {
    *out_len = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    unsigned char *buf = malloc((size_t)sz > 0 ? (size_t)sz : 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    *out_len = sz;
    return buf;
}

static int roq_file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
}

/* ── (a) NO-MUTATION ──────────────────────────────────────────────────
 *
 * readonly_query_does_not_mutate_db
 *
 * Create a real project DB, convert it to rollback (DELETE) journal mode
 * on disk, snapshot its exact bytes, run search_graph through the server,
 * then re-snapshot. The buggy query path runs `PRAGMA journal_mode=WAL`,
 * which rewrites the file header (1,1 -> 2,2) and spawns a -wal sidecar —
 * so the snapshots differ. The fixed READONLY path runs no write pragma,
 * so the file is byte-identical.
 *
 * The DELETE-mode fixture is what makes the mutation OBSERVABLE: on an
 * already-WAL file `journal_mode=WAL` is a silent no-op, so we deliberately
 * stage the DB in rollback mode (the same technique repro_issue557 uses to
 * plant a deterministic trigger).
 *
 * WHY RED on unfixed code:
 *   journal_mode=WAL rewrites the header -> memcmp(before, after) != 0 and
 *   a -wal file is created while the cached store is open. Both assertions
 *   that demand "unchanged" fire.
 * ─────────────────────────────────────────────────────────────────── */
TEST(readonly_query_does_not_mutate_db) {
    char tmp_cache[512];
    snprintf(tmp_cache, sizeof(tmp_cache), "%s/cbm_roq_a_XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(tmp_cache)) {
        ASSERT_NOT_NULL(NULL); /* setup failure */
    }
    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", tmp_cache, 1);

    char db_path[700];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", tmp_cache, ROQ_PROJECT);
    char wal_path[730];
    char shm_path[730];
    snprintf(wal_path, sizeof(wal_path), "%s-wal", db_path);
    snprintf(shm_path, sizeof(shm_path), "%s-shm", db_path);

    /* Build the DB and flip it to rollback journal mode on disk. */
    cbm_store_t *setup = cbm_store_open_path(db_path);
    ASSERT_NOT_NULL(setup);
    ASSERT_EQ(cbm_store_upsert_project(setup, ROQ_PROJECT, "/tmp/roq"), CBM_STORE_OK);
    cbm_node_t node = {.project = ROQ_PROJECT,
                       .label = "Function",
                       .name = "ReadOnlyProbe",
                       .qualified_name = "roq.mod.ReadOnlyProbe",
                       .file_path = "mod.c"};
    ASSERT_TRUE(cbm_store_upsert_node(setup, &node) > 0);
    ASSERT_EQ(cbm_store_exec(setup, "PRAGMA journal_mode=DELETE;"), 0);
    cbm_store_close(setup);

    /* Snapshot BEFORE any query. */
    long before_len = 0;
    unsigned char *before = roq_read_file_bytes(db_path, &before_len);
    ASSERT_NOT_NULL(before);

    /* Run a query tool through the server (the resolve_store path). */
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    char args[512];
    snprintf(args, sizeof(args), "{\"project\":\"%s\",\"name_pattern\":\".*ReadOnlyProbe.*\"}",
             ROQ_PROJECT);
    char *resp = cbm_mcp_handle_tool(srv, "search_graph", args);

    /* Capture sidecar state WHILE the cached store is still open (the buggy
     * RW+WAL open creates -wal here; on close it would be removed again). */
    int wal_while_open = roq_file_exists(wal_path);
    int query_ok = (resp && strstr(resp, "ReadOnlyProbe") != NULL);
    int query_failed = (resp && (strstr(resp, "not found") || strstr(resp, "not indexed")));

    cbm_mcp_server_free(srv); /* closes the store; header change is persisted */

    long after_len = 0;
    unsigned char *after = roq_read_file_bytes(db_path, &after_len);

    int identical = (before && after && before_len == after_len &&
                     memcmp(before, after, (size_t)before_len) == 0);

    if (resp) {
        free(resp);
    }
    free(before);
    free(after);
    cbm_unlink(db_path);
    cbm_unlink(wal_path);
    cbm_unlink(shm_path);
    cbm_rmdir(tmp_cache);
    if (saved_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_copy, 1);
        free(saved_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }

    ASSERT_TRUE(query_ok);        /* read path ran and returned the node */
    ASSERT_FALSE(query_failed);   /* not the "project not found" path */
    ASSERT_TRUE(identical);       /* RED on buggy code: WAL pragma rewrote header */
    ASSERT_FALSE(wal_while_open); /* RED on buggy code: RW+WAL open spawned -wal */
    PASS();
}

/* ── (b) READ-ONLY FILESYSTEM ─────────────────────────────────────────
 *
 * readonly_query_succeeds_on_readonly_fs
 *
 * Create a real project DB (left in WAL journal mode, as the indexer
 * writes it), then chmod the CONTAINING DIRECTORY to 0555 (read-only) to
 * simulate a read-only mount / immutable media, then run search_graph.
 *
 * Note on why the directory (not just the file) must be read-only: SQLite's
 * unix VFS auto-downgrades a failed O_RDWR main-db open to O_RDONLY, so a
 * 0444 *file* alone does NOT surface the bug — the connection silently
 * becomes read-only and, with a writable dir, still creates the WAL -shm
 * and reads. The genuine read-only-FS symptom is the WAL write-pragma
 * (journal_mode=WAL) being unable to create the -shm/-wal sidecars in a
 * read-only directory.
 *
 * WHY RED on unfixed code:
 *   cbm_store_open_path_query() runs configure_pragmas(.., false) which
 *   executes `PRAGMA journal_mode = WAL`. In a read-only directory the WAL
 *   wal-index (-shm) cannot be created, so the pragma errors ->
 *   configure_pragmas fails -> the open returns NULL -> resolve_store()
 *   returns NULL -> the handler emits "project not found or not indexed".
 *
 * GREEN on fixed code:
 *   the READONLY open skips the WAL write-pragma; the plain READONLY open
 *   of a WAL-mode DB in a read-only dir still needs -shm, so it fails and
 *   the immutable-URI fallback (file:..?immutable=1) reads the main DB
 *   file directly and the query returns the node. (This is the test that
 *   exercises the immutable fallback path.)
 * ─────────────────────────────────────────────────────────────────── */
TEST(readonly_query_succeeds_on_readonly_fs) {
    char tmp_cache[512];
    snprintf(tmp_cache, sizeof(tmp_cache), "%s/cbm_roq_b_XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(tmp_cache)) {
        ASSERT_NOT_NULL(NULL); /* setup failure */
    }
    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", tmp_cache, 1);

    char db_path[700];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", tmp_cache, ROQ_PROJECT);
    char wal_path[730];
    char shm_path[730];
    snprintf(wal_path, sizeof(wal_path), "%s-wal", db_path);
    snprintf(shm_path, sizeof(shm_path), "%s-shm", db_path);

    /* Build the DB in its natural WAL journal mode and ensure it is cleanly
     * checkpointed (no -wal frames) so the immutable fallback can read all
     * data from the main file. */
    cbm_store_t *setup = cbm_store_open_path(db_path);
    ASSERT_NOT_NULL(setup);
    ASSERT_EQ(cbm_store_upsert_project(setup, ROQ_PROJECT, "/tmp/roq"), CBM_STORE_OK);
    cbm_node_t node = {.project = ROQ_PROJECT,
                       .label = "Function",
                       .name = "ReadOnlyProbe",
                       .qualified_name = "roq.mod.ReadOnlyProbe",
                       .file_path = "mod.c"};
    ASSERT_TRUE(cbm_store_upsert_node(setup, &node) > 0);
    (void)cbm_store_checkpoint(setup); /* fold WAL frames into the main file */
    cbm_store_close(setup);            /* clean close removes -wal/-shm */

    /* Make the containing directory read-only (simulate a read-only mount).
     * SQLite can still traverse + read files, but cannot create -shm/-wal. */
    ASSERT_EQ(chmod(tmp_cache, 0555), 0);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    char args[512];
    snprintf(args, sizeof(args), "{\"project\":\"%s\",\"name_pattern\":\".*ReadOnlyProbe.*\"}",
             ROQ_PROJECT);
    char *resp = cbm_mcp_handle_tool(srv, "search_graph", args);

    int query_ok = (resp && strstr(resp, "ReadOnlyProbe") != NULL);
    int query_failed = (resp && (strstr(resp, "not found") || strstr(resp, "not indexed")));

    if (resp) {
        free(resp);
    }
    cbm_mcp_server_free(srv);

    /* Restore write permission on the dir BEFORE unlink (cannot remove dir
     * entries while the directory is read-only). */
    chmod(tmp_cache, 0755);
    cbm_unlink(db_path);
    cbm_unlink(wal_path);
    cbm_unlink(shm_path);
    cbm_rmdir(tmp_cache);
    if (saved_copy) {
        cbm_setenv("CBM_CACHE_DIR", saved_copy, 1);
        free(saved_copy);
    } else {
        cbm_unsetenv("CBM_CACHE_DIR");
    }

    ASSERT_FALSE(query_failed); /* RED on buggy code: WAL pragma fails on RO dir */
    ASSERT_TRUE(query_ok);      /* RED on buggy code: no node returned */
    PASS();
}

#undef ROQ_PROJECT

/* ══════════════════════════════════════════════════════════════════
 *  #823 — CLI/supervised index_repository must preserve name override
 * ══════════════════════════════════════════════════════════════════ */

enum {
    IDX823_OK = 0,
    IDX823_NO_SERVER = 61,
    IDX823_NO_RESULT = 62,
    IDX823_NOT_INDEXED = 63,
    IDX823_RESPONSE_NAME_MISSING = 64,
    IDX823_LIST_NAME_MISSING = 65,
    IDX823_SEARCH_FAILED = 66,
    IDX823_PARENT_GUARD_USED = 67,
};

#ifndef _WIN32 /* helper used only by the POSIX fork harness below */
static int idx823_supervised_name_override_check(const char *repo_dir, const char *custom_name) {
    /* Match the real CLI/MCP server state: a marked host with the supervisor
     * enabled. The worker receives the same args JSON the CLI forwards. */
    cbm_index_supervisor_mark_host();
    cbm_unsetenv("CBM_INDEX_SUPERVISOR");
    cbm_setenv("CBM_INDEX_MAX_RESTARTS", "1", 1);
    cbm_setenv("CBM_INDEX_WORKER_TIMEOUT_S", "30", 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv) {
        return IDX823_NO_SERVER;
    }
    /* A supervised local index transfers project-lock ownership to the worker.
     * Denying the parent guard is therefore harmless and proves the parent did
     * not acquire a lease before spawning. RED on the former ordering, which
     * returned "blocked" without ever starting the worker. */
    mcp_mutation_guard_probe_t parent_guard = {.deny_begin_call = 1};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &parent_guard);

    char args[1024];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\",\"name\":\"%s\"}",
             repo_dir, custom_name);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    int code = IDX823_OK;
    if (parent_guard.begin_count != 0 || parent_guard.end_count != 0) {
        code = IDX823_PARENT_GUARD_USED;
    } else if (!resp) {
        code = IDX823_NO_RESULT;
    } else if (!response_contains_json_fragment(resp, "\"status\":\"indexed\"")) {
        code = IDX823_NOT_INDEXED;
    } else {
        char expected[256];
        snprintf(expected, sizeof(expected), "\"project\":\"%s\"", custom_name);
        if (!response_contains_json_fragment(resp, expected)) {
            code = IDX823_RESPONSE_NAME_MISSING;
        }
    }
    free(resp);

    if (code == IDX823_OK) {
        char *projects = cbm_mcp_handle_tool(srv, "list_projects", "{}");
        char expected[256];
        snprintf(expected, sizeof(expected), "\"name\":\"%s\"", custom_name);
        if (!projects || !response_contains_json_fragment(projects, expected)) {
            code = IDX823_LIST_NAME_MISSING;
        }
        free(projects);
    }

    if (code == IDX823_OK) {
        char q[512];
        snprintf(q, sizeof(q),
                 "{\"project\":\"%s\",\"name_pattern\":\"idx823_fn\",\"label\":\"Function\"}",
                 custom_name);
        char *sr = cbm_mcp_handle_tool(srv, "search_graph", q);
        if (!sr || !strstr(sr, "idx823_fn")) {
            code = IDX823_SEARCH_FAILED;
        }
        free(sr);
    }

    cbm_mcp_server_free(srv);
    return code;
}
#endif

TEST(index_repository_cli_name_override_issue823) {
#ifdef _WIN32
    SKIP_PLATFORM("POSIX fork harness required to isolate supervisor host mark");
#else
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-idx823-repo-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        FAIL("cbm_mkdtemp repo failed");
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-idx823-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        th_rmtree(tmp_dir);
        FAIL("cbm_mkdtemp cache failed");
    }

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    ASSERT_EQ(th_write_file(src_path, "def idx823_fn():\n    return 823\n"), 0);

    const char *custom_name = "issue823-custom-project";
    int code = -1;
    bool signalled = false;
    int sig = 0;

    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(60);
        _exit(idx823_supervised_name_override_check(tmp_dir, custom_name));
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        signalled = true;
        sig = WTERMSIG(status);
    }

    char *path_project = cbm_project_name_from_path(tmp_dir);
    cleanup_project_db(cache, custom_name);
    cleanup_project_db(cache, path_project);
    free(path_project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    th_rmtree(cache);
    th_rmtree(tmp_dir);

    if (signalled) {
        printf("    child killed by signal %d (alarm => worker hang)\n", sig);
    } else if (code != IDX823_OK) {
        printf("    child exit code %d (64=response name, 65=list name, "
               "66=search, 67=parent guard used)\n",
               code);
    }
    ASSERT_FALSE(signalled);
    ASSERT_EQ(code, IDX823_OK);
    PASS();
#endif
}

/* ══════════════════════════════════════════════════════════════════
 *  #845 — supervisor gate must not wrap embedders of cbm_mcp_handle_tool
 * ══════════════════════════════════════════════════════════════════ */

TEST(index_supervisor_unsafe_clean_is_never_fallback_or_recovery) {
    char response[] = "{\"status\":\"indexed\"}";
    cbm_index_worker_result_t result = {
        .outcome = CBM_PROC_CLEAN,
        .exit_code = 0,
        .tree_quiesced = true,
        .response = response,
    };
    ASSERT_EQ(cbm_mcp_supervised_result_disposition(0, &result), CBM_MCP_SUPERVISED_RESULT_SUCCESS);

    result.cancellation_requested = true;
    ASSERT_EQ(cbm_mcp_supervised_result_disposition(0, &result),
              CBM_MCP_SUPERVISED_RESULT_UNSAFE_TERMINAL);
    result.cancellation_requested = false;
    result.tree_quiesced = false;
    ASSERT_EQ(cbm_mcp_supervised_result_disposition(0, &result),
              CBM_MCP_SUPERVISED_RESULT_UNSAFE_TERMINAL);
    result.tree_quiesced = true;
    result.supervision_failed = true;
    ASSERT_EQ(cbm_mcp_supervised_result_disposition(0, &result),
              CBM_MCP_SUPERVISED_RESULT_UNSAFE_TERMINAL);

    result.supervision_failed = false;
    result.outcome = CBM_PROC_CRASH;
    result.response = NULL;
    ASSERT_EQ(cbm_mcp_supervised_result_disposition(0, &result),
              CBM_MCP_SUPERVISED_RESULT_CONTAINED_FAILURE);
    ASSERT_EQ(cbm_mcp_supervised_result_disposition(-1, &result),
              CBM_MCP_SUPERVISED_RESULT_FALLBACK);
    PASS();
}

/* Child-side check: index a tiny fixture and verify it ran IN-PROCESS.
 * Distinct exit codes so the parent can report the exact failure mode. */
enum {
    IDX845_OK = 0,
    IDX845_SPAWNED = 41,     /* a worker subprocess was spawned — the #845 bug */
    IDX845_NO_RESULT = 42,   /* handle_tool returned NULL */
    IDX845_NOT_INDEXED = 43, /* response lacks status=indexed */
};

static int idx845_index_inprocess_check(const char *repo_dir) {
    int spawns_before = cbm_index_supervisor_spawn_count();

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv) {
        return IDX845_NO_RESULT;
    }
    char args[1024];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", repo_dir);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);

    int code = IDX845_OK;
    if (cbm_index_supervisor_spawn_count() != spawns_before) {
        code = IDX845_SPAWNED;
    } else if (!resp) {
        code = IDX845_NO_RESULT;
    } else if (!response_contains_json_fragment(resp, "\"status\":\"indexed\"")) {
        code = IDX845_NOT_INDEXED;
    }
    free(resp);
    cbm_mcp_server_free(srv);
    return code;
}

TEST(index_supervisor_gate_requires_marked_host_issue845) {
    /* #845: index_repository via cbm_mcp_handle_tool from an EMBEDDER (this test
     * binary) must index IN-PROCESS even with CBM_INDEX_SUPERVISOR unset. The
     * supervisor gate may only wrap a process that called
     * cbm_index_supervisor_mark_host() — i.e. the real binary's main(). Before
     * the fix, should_wrap() was true for ANY embedder: the gate resolved the
     * CURRENT binary (this test runner!) and spawned
     * '<test-runner> cli --index-worker --index-worker-build …', which a test binary
     * interprets as suite-filter args → it re-runs test suites in the child →
     * recursive spawn chains (observed 11-min hangs; kernel VM-map load during
     * the 2026-07-04 host panics).
     *
     * POSIX: run the call in a forked child under alarm(20) so the pre-fix
     * recursive behaviour cannot hang the runner; the child reports via exit
     * code. Windows: no fork — run in-process (safe once the gate is fixed; the
     * pre-fix redness is demonstrated on POSIX). */
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-idx845-repo-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-idx845-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    /* The point of the guard: NO kill switch. The gate itself must keep an
     * unmarked host in-process. Save + restore the ambient value. */
    const char *saved_sv = getenv("CBM_INDEX_SUPERVISOR");
    char *saved_sv_copy = saved_sv ? strdup(saved_sv) : NULL;
    cbm_unsetenv("CBM_INDEX_SUPERVISOR");

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def main():\n    return 'ok'\n", fp);
    fclose(fp);

    int code = -1;
    bool signalled = false;
    int sig = 0;
#ifdef _WIN32
    code = idx845_index_inprocess_check(tmp_dir);
#else
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(20); /* pre-fix spawn chain must die here, not hang the runner */
        _exit(idx845_index_inprocess_check(tmp_dir));
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        signalled = true;
        sig = WTERMSIG(status);
    }
#endif

    /* Restore env BEFORE asserting so a red run doesn't leak state. */
    if (saved_sv_copy) {
        cbm_setenv("CBM_INDEX_SUPERVISOR", saved_sv_copy, 1);
        free(saved_sv_copy);
    } else {
        cbm_unsetenv("CBM_INDEX_SUPERVISOR");
    }
    char *project = cbm_project_name_from_path(tmp_dir);
    cleanup_project_db(cache, project);
    free(project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    remove(src_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);

    if (signalled) {
        printf("    child killed by signal %d (alarm => recursive spawn chain hang)\n", sig);
    } else if (code != IDX845_OK) {
        printf("    child exit code %d (41=worker spawned, 42=no result, 43=not indexed)\n", code);
    }
    ASSERT_FALSE(signalled);
    ASSERT_EQ(code, IDX845_OK);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  Mandatory supervision must fail closed in real CBM hosts
 * ══════════════════════════════════════════════════════════════════ */

/* A real CBM host must never turn a supervisor refusal into permission to run
 * the native index pipeline in its own long-lived process. The legacy
 * CBM_INDEX_SUPERVISOR=0 switch is a deterministic start-failure seam here: on
 * the buggy path should_wrap() returned false, the parent mutation guard ran,
 * and the project DB was written in-process. The fixed path keeps supervision
 * mandatory, returns an error, and leaves both the guard and filesystem
 * untouched. Host marking is process-lifetime state, so isolate it in a clean
 * re-exec. posix_spawn stays reliable after earlier tests created threads,
 * whereas a late raw fork can fail transiently under sanitizers on macOS. */
enum {
    IDXFAILCLOSED_OK = 0,
    IDXFAILCLOSED_NO_SERVER = 81,
    IDXFAILCLOSED_PARENT_MUTATED = 82,
    IDXFAILCLOSED_NO_RESPONSE = 83,
    IDXFAILCLOSED_INDEXED = 84,
    IDXFAILCLOSED_NOT_ERROR = 85,
};

#ifndef _WIN32
int mcp_test_idxfailclosed_supervisor_start_check(const char *repo_dir, const char *cache_dir) {
    (void)cbm_setenv("CBM_CACHE_DIR", cache_dir, 1);
    cbm_index_supervisor_mark_host();
    (void)cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv) {
        return IDXFAILCLOSED_NO_SERVER;
    }
    mcp_mutation_guard_probe_t parent_guard = {0};
    cbm_mcp_server_set_project_mutation_guard(srv, mcp_mutation_guard_probe_begin,
                                              mcp_mutation_guard_probe_end, &parent_guard);

    char args[CBM_SZ_4K];
    (void)snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"fast\"}", repo_dir);
    char *response = cbm_mcp_handle_tool(srv, "index_repository", args);

    int result = IDXFAILCLOSED_OK;
    if (parent_guard.begin_count != 0 || parent_guard.end_count != 0) {
        result = IDXFAILCLOSED_PARENT_MUTATED;
    } else if (!response) {
        result = IDXFAILCLOSED_NO_RESPONSE;
    } else if (response_contains_json_fragment(response, "\"status\":\"indexed\"")) {
        result = IDXFAILCLOSED_INDEXED;
    } else if (!response_contains_json_fragment(response, "\"status\":\"error\"") ||
               !response_contains_json_fragment(response, "\"outcome\":\"spawn_failed\"")) {
        result = IDXFAILCLOSED_NOT_ERROR;
    }

    free(response);
    cbm_mcp_server_free(srv);
    return result;
}

static bool idxfailclosed_self_path(char out[CBM_SZ_4K]) {
#ifdef __APPLE__
    int length = proc_pidpath(getpid(), out, CBM_SZ_4K);
    bool resolved = length > 0 && length < CBM_SZ_4K;
    if (resolved) {
        out[length] = '\0';
    }
    return resolved;
#elif defined(__linux__)
    ssize_t length = readlink("/proc/self/exe", out, CBM_SZ_4K - 1);
    bool resolved = length > 0 && length < (ssize_t)CBM_SZ_4K - 1;
    if (resolved) {
        out[length] = '\0';
    }
    return resolved;
#else
    (void)out;
    return false;
#endif
}
#endif

TEST(index_supervisor_start_failure_is_fail_closed_in_real_host) {
#ifdef _WIN32
    SKIP_PLATFORM("immutable host mark needs fork isolation (POSIX-only)");
#else
    char repo_dir[CBM_SZ_1K];
    char cache_dir[CBM_SZ_1K];
    (void)snprintf(repo_dir, sizeof(repo_dir), "%s/cbm-idx-failclosed-repo-XXXXXX", cbm_tmpdir());
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/cbm-idx-failclosed-cache-XXXXXX",
                   cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(repo_dir));
    ASSERT_NOT_NULL(cbm_mkdtemp(cache_dir));

    char source_path[CBM_SZ_4K];
    (void)snprintf(source_path, sizeof(source_path), "%s/should_not_index.py", repo_dir);
    FILE *source = cbm_fopen(source_path, "wb");
    ASSERT_NOT_NULL(source);
    ASSERT_TRUE(fputs("def should_not_index():\n    return True\n", source) >= 0);
    ASSERT_EQ(fclose(source), 0);

    char *project = cbm_project_name_from_path(repo_dir);
    ASSERT_NOT_NULL(project);
    char db_path[CBM_SZ_4K];
    (void)snprintf(db_path, sizeof(db_path), "%s/%s.db", cache_dir, project);

    char self_path[CBM_SZ_4K] = {0};
    ASSERT_TRUE(idxfailclosed_self_path(self_path));
    char *const child_argv[] = {
        self_path, "__cbm_mcp_idxfailclosed_probe", repo_dir, cache_dir, NULL,
    };
    (void)fflush(NULL);
    pid_t child = -1;
    ASSERT_EQ(posix_spawn(&child, self_path, NULL, NULL, child_argv, environ), 0);
    ASSERT_TRUE(child > 0);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    bool exited = WIFEXITED(status);
    int child_result = exited ? WEXITSTATUS(status) : -1;
    bool database_absent = !cbm_file_exists(db_path);

    cleanup_project_db(cache_dir, project);
    free(project);
    (void)cbm_unlink(source_path);
    (void)th_rmtree(repo_dir);
    (void)th_rmtree(cache_dir);

    ASSERT_TRUE(exited);
    ASSERT_EQ(child_result, IDXFAILCLOSED_OK);
    ASSERT_TRUE(database_absent);
    PASS();
#endif
}

/* ═══════════════════════════════════════════════════════════
 *  #832 — background auto-index + watcher re-index must run in the
 *         supervised worker SUBPROCESS (RSS isolation)
 * ══════════════════════════════════════════════════════════ */

/* The long-lived server ran the full index pipeline in-process on two background
 * paths (session auto-index in mcp.c, watcher re-index in main.c). Worker-thread
 * mimalloc heaps abandon pages at thread exit and mimalloc v3
 * (page_reclaim_on_free=0) does not reclaim them when the main thread later frees
 * their blocks, so RSS ratchets across re-index cycles (#832). The fix routes both
 * paths through cbm_mcp_index_run_supervised_path() — the SAME supervised worker
 * subprocess the index_repository tool uses — so the child hands 100%% of its RSS
 * back to the OS on exit.
 *
 * This guard proves the ROUTING: on a supervisor-marked host with the kill switch
 * OFF, the shared entry the watcher/auto-index now call must (a) spawn a worker
 * child (cbm_index_supervisor_spawn_count() increases) and (b) actually index the
 * fixture (the worker child writes the Function node). RED on the unfixed
 * in-process routing: it calls cbm_pipeline_run directly, so spawn_count is
 * unchanged → IDX832_NO_SPAWN. */
enum {
    IDX832_OK = 0,
    IDX832_NO_SPAWN = 51,    /* spawn_count unchanged — routed in-process (RED) */
    IDX832_NULL_RESP = 52,   /* supervised entry degraded to NULL */
    IDX832_NOT_INDEXED = 53, /* response/store lacks the indexed Function node */
    IDX832_SERVER_FAIL = 54,
};

#ifndef _WIN32 /* helper used only by the POSIX fork harness below */
static int idx832_supervised_route_check(const char *repo_dir) {
    /* Become a supervisor host with the kill switch OFF — exactly the real MCP
     * server's state. Done in the FORKED CHILD only (see the harness) so the
     * parent test-runner's process-wide host mark stays clear and the #845
     * unmarked-embedder guard is unaffected. Bound the recovery loop + worker
     * quiet-timeout so a stuck child cannot run long under the fork+alarm net. */
    cbm_index_supervisor_mark_host();
    cbm_unsetenv("CBM_INDEX_SUPERVISOR");
    cbm_setenv("CBM_INDEX_MAX_RESTARTS", "1", 1);
    cbm_setenv("CBM_INDEX_WORKER_TIMEOUT_S", "30", 1);

    int spawns_before = cbm_index_supervisor_spawn_count();
    char *resp = cbm_mcp_index_run_supervised_path(repo_dir);
    int spawns_after = cbm_index_supervisor_spawn_count();

    if (spawns_after == spawns_before) {
        free(resp);
        return IDX832_NO_SPAWN; /* the discriminating assertion: RED in-process */
    }
    if (!resp) {
        return IDX832_NULL_RESP;
    }
    bool indexed = response_contains_json_fragment(resp, "\"status\":\"indexed\"");
    free(resp);
    if (!indexed) {
        return IDX832_NOT_INDEXED;
    }

    /* Store-level proof the worker child did real work: the Function node it wrote
     * must be queryable from a fresh server reading the DB the child produced. */
    char *project = cbm_project_name_from_path(repo_dir);
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv) {
        free(project);
        return IDX832_SERVER_FAIL;
    }
    int code = IDX832_OK;
    if (project) {
        char q[512];
        snprintf(q, sizeof(q),
                 "{\"project\":\"%s\",\"name_pattern\":\"idx832_fn\",\"label\":\"Function\"}",
                 project);
        char *sr = cbm_mcp_handle_tool(srv, "search_graph", q);
        if (!sr || !strstr(sr, "idx832_fn")) {
            code = IDX832_NOT_INDEXED;
        }
        free(sr);
    }
    cbm_mcp_server_free(srv);
    free(project);
    return code;
}
#endif /* !_WIN32 */

TEST(index_bg_paths_route_through_supervisor_issue832) {
#ifdef _WIN32
    /* The guard marks the process as a supervisor host, which cannot be undone.
     * POSIX isolates that in a forked child; without fork we would pollute the
     * shared test-runner (breaking the #845 unmarked-embedder guard). The routing
     * logic is platform-independent and covered on POSIX CI; Windows containment
     * is covered by the end-to-end crash-containment test. */
    SKIP_PLATFORM("supervisor-host guard needs fork isolation (POSIX-only)");
#else
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-idx832-repo-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-idx832-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1); /* inherited by the worker child */

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def idx832_fn():\n    return 'ok'\n", fp);
    fclose(fp);

    int code = -1;
    bool signalled = false;
    int sig = 0;
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(60); /* a stuck worker dies here instead of hanging the runner */
        _exit(idx832_supervised_route_check(tmp_dir));
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        signalled = true;
        sig = WTERMSIG(status);
    }

    char *project = cbm_project_name_from_path(tmp_dir);
    cleanup_project_db(cache, project);
    free(project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    remove(src_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);

    if (signalled) {
        printf("    child killed by signal %d (alarm => worker hang)\n", sig);
    } else if (code != IDX832_OK) {
        printf("    child exit code %d (51=no spawn/in-process=RED, 52=null resp, "
               "53=not indexed, 54=server fail)\n",
               code);
    }
    ASSERT_FALSE(signalled);
    ASSERT_EQ(code, IDX832_OK);
    PASS();
#endif
}

/* ══════════════════════════════════════════════════════════════════
 *  Parallel-only crash recovery (ms-typescript cascade fix)
 * ══════════════════════════════════════════════════════════════════ */

/* The old recovery loop re-ran the worker SINGLE-THREADED to keep one exact
 * crash marker. At scale that fell into the sequential crawl, was killed as
 * a hang mid-pass, and the stale marker quarantined FOUR innocent
 * ms-typescript fixtures, one 15-minute retry at a time. The reworked loop
 * re-runs PARALLEL with a marker journal; a file is quarantined only when
 * it is in-flight across two consecutive failed runs.
 *
 * This guard proves the CONTRACT: with an injected crasher among good
 * files, the supervised index must (a) never spawn a single-threaded worker
 * (cbm_index_supervisor_spawn_st_count stays 0 — RED on the old loop),
 * (b) quarantine exactly the crasher, (c) leave the innocents indexed and
 * NOT quarantined. */
enum {
    IDXPAR_OK = 0,
    IDXPAR_ST_SPAWN = 61,      /* single-threaded recovery spawn happened (RED) */
    IDXPAR_NULL_RESP = 62,     /* supervised entry degraded to NULL */
    IDXPAR_NOT_INDEXED = 63,   /* response lacks status indexed */
    IDXPAR_NO_QUARANTINE = 64, /* crasher missing from skipped[] */
    IDXPAR_INNOCENT_HIT = 65,  /* a good file was quarantined/skipped */
    IDXPAR_GOOD_MISSING = 66,  /* good file's Function absent from the store */
};

#ifndef _WIN32
static int idxpar_recovery_check(const char *repo_dir) {
    cbm_index_supervisor_mark_host();
    cbm_unsetenv("CBM_INDEX_SUPERVISOR");
    /* Rounds needed: fail+record, fail+quarantine, clean. Generous cap. */
    cbm_setenv("CBM_INDEX_MAX_RESTARTS", "5", 1);
    cbm_setenv("CBM_INDEX_WORKER_TIMEOUT_S", "30", 1);
    cbm_setenv("CBM_TEST_CRASH_ON", "idxpar_crasher", 1);

    int st_before = cbm_index_supervisor_spawn_st_count();
    char *resp = cbm_mcp_index_run_supervised_path(repo_dir);
    int st_after = cbm_index_supervisor_spawn_st_count();
    cbm_unsetenv("CBM_TEST_CRASH_ON");

    if (st_after != st_before) {
        free(resp);
        return IDXPAR_ST_SPAWN; /* discriminating assertion: RED on the old loop */
    }
    if (!resp) {
        return IDXPAR_NULL_RESP;
    }
    bool indexed = response_contains_json_fragment(resp, "\"status\":\"indexed\"");
    bool crasher_skipped = strstr(resp, "idxpar_crasher.py") != NULL;
    bool innocent_hit =
        strstr(resp, "idxpar_good_a.py") != NULL || strstr(resp, "idxpar_good_b.py") != NULL;
    free(resp);
    if (!indexed) {
        return IDXPAR_NOT_INDEXED;
    }
    if (!crasher_skipped) {
        return IDXPAR_NO_QUARANTINE;
    }
    if (innocent_hit) {
        return IDXPAR_INNOCENT_HIT;
    }

    /* Store proof: an innocent's Function node exists. */
    char *project = cbm_project_name_from_path(repo_dir);
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    int code = IDXPAR_OK;
    if (srv && project) {
        char q[512];
        snprintf(q, sizeof(q),
                 "{\"project\":\"%s\",\"name_pattern\":\"idxpar_good_fn\",\"label\":\"Function\"}",
                 project);
        char *sr = cbm_mcp_handle_tool(srv, "search_graph", q);
        if (!sr || !strstr(sr, "idxpar_good_fn")) {
            code = IDXPAR_GOOD_MISSING;
        }
        free(sr);
    }
    if (srv) {
        cbm_mcp_server_free(srv);
    }
    free(project);
    return code;
}
#endif /* !_WIN32 */

/* #773: SIGABRT (invalid free in ts_stack_delete via
 * cbm_destroy_thread_parser) on the SECOND index_repository in one server
 * process, once both repos take the PARALLEL path (~30+ files). The
 * supervisor masks this on the default MCP path (fresh worker process per
 * index); the in-process pipeline — CBM_INDEX_SUPERVISOR=0, and every
 * embedded/test consumer — dies. Forked child so the abort cannot kill the
 * runner; ASan legs print the exact bad free. */
enum {
    IDX773_OK = 0,
    IDX773_FIRST_FAILED = 71,  /* first index didn't return indexed */
    IDX773_SECOND_FAILED = 72, /* second index didn't return indexed */
};

#ifndef _WIN32
static void idx773_write_py_repo(const char *dir, int files, int variant) {
    for (int i = 0; i < files; i++) {
        char path[CBM_SZ_512];
        snprintf(path, sizeof(path), "%s/mod_%d_%03d.py", dir, variant, i);
        FILE *f = fopen(path, "w");
        if (!f) {
            continue;
        }
        fprintf(f,
                "class Handler%d:\n"
                "    def run(self, x):\n"
                "        return self.helper(x) + %d\n"
                "    def helper(self, x):\n"
                "        for i in range(10):\n"
                "            x += i\n"
                "        return x\n"
                "\n"
                "def main_%d(x):\n"
                "    return Handler%d().run(x)\n",
                i, i, i, i);
        fclose(f);
    }
}

static int idx773_double_index_check(const char *dir_a, const char *dir_b) {
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (!srv) {
        return IDX773_FIRST_FAILED;
    }
    char args[CBM_SZ_512];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"full\"}", dir_a);
    char *r1 = cbm_mcp_handle_tool(srv, "index_repository", args);
    bool ok1 = r1 && strstr(r1, "indexed") != NULL;
    free(r1);
    if (!ok1) {
        cbm_mcp_server_free(srv);
        return IDX773_FIRST_FAILED;
    }
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\",\"mode\":\"full\"}", dir_b);
    char *r2 = cbm_mcp_handle_tool(srv, "index_repository", args); /* SIGABRT here (RED) */
    bool ok2 = r2 && strstr(r2, "indexed") != NULL;
    free(r2);
    cbm_mcp_server_free(srv);
    return ok2 ? IDX773_OK : IDX773_SECOND_FAILED;
}
#endif /* !_WIN32 */

/* #898: the SEQUENTIAL pipeline emitted malformed JSON for brokered
 * ASYNC_CALLS edges ("broker":"bullmq} — missing closing quote) and stored
 * the RAW broker/method string as the synthesized Route node's properties
 * (literally `bullmq` instead of {"broker":"bullmq"}). json_extract over
 * those rows errors, generated-column indexes fail, and PRAGMA quick_check
 * aborts with "malformed JSON" — which since the artifact deep-integrity
 * check also means such caches are refused at import. The parallel path
 * was correct; both pipelines must emit identical, valid JSON. */
TEST(sequential_service_edge_props_are_valid_json_issue898) {
    char tmp[CBM_SZ_256];
    snprintf(tmp, sizeof(tmp), "/tmp/cbm_seq898_XXXXXX");
    if (!cbm_mkdtemp(tmp)) {
        FAIL("mkdtemp failed");
    }
    char cache[CBM_SZ_256];
    snprintf(cache, sizeof(cache), "/tmp/cbm_seq898_cache_XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp);
        FAIL("cache mkdtemp failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char src_path[CBM_SZ_512];
    snprintf(src_path, sizeof(src_path), "%s/queue.py", tmp);
    FILE *f = fopen(src_path, "w");
    ASSERT_NOT_NULL(f);
    /* celery.Celery("tasks") resolves through the import map to a QN the
     * service-pattern table classifies as ASYNC with broker "celery". */
    fputs("import celery\n"
          "\n"
          "def enqueue():\n"
          "    celery.Celery(\"tasks\")\n",
          f);
    fclose(f);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    char args[CBM_SZ_512];
    snprintf(args, sizeof(args), "{\"repo_path\":\"%s\"}", tmp);
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "indexed"));
    free(resp);

    /* File-backed MCP stores are deliberately request-scoped so a sibling
     * process can atomically replace the DB generation (and so Windows does
     * not retain a replacement-blocking handle). Inspect the published DB
     * through an independent query handle instead of relying on srv->store. */
    char *project = cbm_project_name_from_path(tmp);
    ASSERT_NOT_NULL(project);
    char db_path[CBM_SZ_512];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    cbm_store_t *store = cbm_store_open_path_query(db_path);
    ASSERT_NOT_NULL(store);
    struct sqlite3 *db = cbm_store_get_db(store);
    ASSERT_NOT_NULL(db);

    /* Non-vacuous: the fixture must actually produce a brokered edge. */
    sqlite3_stmt *stmt = NULL;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT count(*) FROM edges WHERE type='ASYNC_CALLS';", -1,
                                 &stmt, NULL),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    int async_edges = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    ASSERT_TRUE(async_edges >= 1);

    /* THE BUG: malformed properties on edges (broker quote) and Route nodes
     * (raw string). Every properties blob must be valid JSON. */
    ASSERT_EQ(sqlite3_prepare_v2(db,
                                 "SELECT count(*) FROM edges WHERE properties IS NOT NULL "
                                 "AND properties != '' AND json_valid(properties)=0;",
                                 -1, &stmt, NULL),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    int bad_edges = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    ASSERT_EQ(bad_edges, 0);

    ASSERT_EQ(sqlite3_prepare_v2(db,
                                 "SELECT count(*) FROM nodes WHERE properties IS NOT NULL "
                                 "AND properties != '' AND json_valid(properties)=0;",
                                 -1, &stmt, NULL),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    int bad_nodes = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    ASSERT_EQ(bad_nodes, 0);

    /* Pipeline parity: the broker must be extractable exactly like the
     * parallel path emits it. */
    ASSERT_EQ(sqlite3_prepare_v2(db,
                                 "SELECT count(*) FROM edges WHERE type='ASYNC_CALLS' AND "
                                 "json_extract(properties,'$.broker')='celery';",
                                 -1, &stmt, NULL),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    int brokered = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    ASSERT_TRUE(brokered >= 1);

    cbm_store_close(store);
    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    free(project);
    th_rmtree(cache);
    cbm_unlink(src_path);
    cbm_rmdir(tmp);
    PASS();
}

TEST(index_second_inprocess_run_survives_issue773) {
#ifdef _WIN32
    SKIP_PLATFORM("fork-isolated crash guard (POSIX-only)");
#else
    char dir_a[CBM_SZ_256];
    char dir_b[CBM_SZ_256];
    char cache[CBM_SZ_256];
    snprintf(dir_a, sizeof(dir_a), "/tmp/cbm-idx773a-XXXXXX");
    snprintf(dir_b, sizeof(dir_b), "/tmp/cbm-idx773b-XXXXXX");
    snprintf(cache, sizeof(cache), "/tmp/cbm-idx773c-XXXXXX");
    if (!cbm_mkdtemp(dir_a) || !cbm_mkdtemp(dir_b) || !cbm_mkdtemp(cache)) {
        FAIL("mkdtemp failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    /* Trigger shape: run 1 small enough for the SEQUENTIAL path (parses on
     * the calling thread, mimalloc epoch), run 2 large enough for the
     * PARALLEL path (switches the global ts allocator to the slab). */
    idx773_write_py_repo(dir_a, 5, 0);
    idx773_write_py_repo(dir_b, 60, 1);

    int code = -1;
    bool signalled = false;
    int sig = 0;
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(180); /* generous: two full parallel indexes */
        _exit(idx773_double_index_check(dir_a, dir_b));
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        signalled = true;
        sig = WTERMSIG(status);
    }

    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);

    if (signalled) {
        printf("    child killed by signal %d (SIGABRT = the #773 invalid free)\n", sig);
    } else if (code != IDX773_OK) {
        printf("    child exit code %d (71=first index failed, 72=second failed)\n", code);
    }
    ASSERT_FALSE(signalled);
    ASSERT_EQ(code, IDX773_OK);
    PASS();
#endif
}

TEST(index_recovery_parallel_quarantines_crasher) {
#ifdef _WIN32
    SKIP_PLATFORM("parallel-recovery guard needs fork isolation (POSIX-only)");
#else
    char tmp_dir[CBM_SZ_256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-idxpar-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        FAIL("mkdtemp failed");
    }
    char cache[CBM_SZ_256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-idxpar-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        FAIL("mkdtemp cache failed");
    }
    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? cbm_strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char p1[CBM_SZ_512];
    char p2[CBM_SZ_512];
    char pc[CBM_SZ_512];
    snprintf(p1, sizeof(p1), "%s/idxpar_good_a.py", tmp_dir);
    snprintf(p2, sizeof(p2), "%s/idxpar_good_b.py", tmp_dir);
    snprintf(pc, sizeof(pc), "%s/idxpar_crasher.py", tmp_dir);
    FILE *f = fopen(p1, "w");
    ASSERT_NOT_NULL(f);
    fputs("def idxpar_good_fn():\n    return 'ok'\n", f);
    fclose(f);
    f = fopen(p2, "w");
    ASSERT_NOT_NULL(f);
    fputs("def idxpar_good_fn_b():\n    return 'ok'\n", f);
    fclose(f);
    f = fopen(pc, "w");
    ASSERT_NOT_NULL(f);
    fputs("def idxpar_crash_fn():\n    return 'boom'\n", f);
    fclose(f);
    int code = -1;
    bool signalled = false;
    int sig = 0;
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(120); /* generous: three supervised rounds + clean run */
        _exit(idxpar_recovery_check(tmp_dir));
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        signalled = true;
        sig = WTERMSIG(status);
    }

    char *project = cbm_project_name_from_path(tmp_dir);
    cleanup_project_db(cache, project);
    free(project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    cbm_unlink(p1);
    cbm_unlink(p2);
    cbm_unlink(pc);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);

    if (signalled) {
        printf("    child killed by signal %d (alarm => recovery loop hang)\n", sig);
    } else if (code != IDXPAR_OK) {
        printf("    child exit code %d (61=ST spawn/RED, 62=null resp, 63=not indexed, "
               "64=no quarantine, 65=innocent hit, 66=good missing)\n",
               code);
    }
    ASSERT_FALSE(signalled);
    ASSERT_EQ(code, IDXPAR_OK);
    PASS();
#endif
}

/* ══════════════════════════════════════════════════════════════════
 *  AUTO_WATCH GATE  (distilled from PR #625)
 *
 *  Background watcher registration on session connect is gated by the
 *  `auto_watch` config key (default TRUE = existing behavior).
 * ══════════════════════════════════════════════════════════════════ */

/* Drive the already-indexed connect path (initialize → maybe_auto_index →
 * watcher registration) and return the resulting watch count.
 * auto_watch_value: NULL leaves the key unset (exercises the default),
 * otherwise the key is set to that value before initialize.
 * Returns a negative code on fixture setup failure. */
static int auto_watch_connect_watch_count(const char *auto_watch_value) {
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-autowatch-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        return -1;
    }

    char repodir[512];
    snprintf(repodir, sizeof(repodir), "%s/repo", cache);
    if (th_mkdir_p(repodir) != 0) {
        th_rmtree(cache);
        return -2;
    }

    /* Same derivation detect_session uses on the cwd — realpath-based, so
     * the name matches even where /tmp is a symlink (macOS). */
    char *project = cbm_project_name_from_path(repodir);
    if (!project) {
        th_rmtree(cache);
        return -3;
    }

    /* Pre-create <cache>/<project>.db so maybe_auto_index takes the
     * "already indexed" branch — the watcher-registration site under test. */
    char db_path[1024];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project);
    if (th_write_file(db_path, "") != 0) {
        free(project);
        th_rmtree(cache);
        return -4;
    }
    free(project);

    const char *saved = getenv("CBM_CACHE_DIR");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    char old_cwd[1024];
    if (!cbm_getcwd(old_cwd, sizeof(old_cwd)) || cbm_chdir(repodir) != 0) {
        restore_cache_dir(saved_copy);
        free(saved_copy);
        th_rmtree(cache);
        return -5;
    }

    int count = -6;
    cbm_config_t *cfg = cbm_config_open(cache);
    cbm_store_t *wstore = cbm_store_open_memory();
    cbm_watcher_t *watcher = wstore ? cbm_watcher_new(wstore, NULL, NULL) : NULL;
    if (cfg && watcher) {
        if (auto_watch_value) {
            cbm_config_set(cfg, CBM_CONFIG_AUTO_WATCH, auto_watch_value);
        }

        cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
        if (srv) {
            cbm_mcp_server_set_watcher(srv, watcher);
            cbm_mcp_server_set_config(srv, cfg);
            char *resp = cbm_mcp_server_handle(
                srv, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
            free(resp);
            count = cbm_watcher_watch_count(watcher);
            cbm_mcp_server_free(srv);
        }
    }

    if (watcher) {
        cbm_watcher_free(watcher);
    }
    if (wstore) {
        cbm_store_close(wstore);
    }
    if (cfg) {
        cbm_config_close(cfg);
    }

    (void)cbm_chdir(old_cwd);
    restore_cache_dir(saved_copy);
    free(saved_copy);
    th_rmtree(cache);
    return count;
}

/* Default (key unset) → watcher registered on connect. Guards the
 * no-behavior-change promise of the auto_watch gate: existing users keep
 * background auto-sync without touching config. */
TEST(mcp_auto_watch_default_registers_watcher_on_connect) {
    int count = auto_watch_connect_watch_count(NULL);
    if (count < 0) {
        PASS(); /* fixture setup failed (tmpdir/cwd unavailable) — skip */
    }
    ASSERT_EQ(count, 1);
    PASS();
}

/* auto_watch=false → NO watcher registered on connect. RED on pre-gate code
 * (registration was unconditional and the key did not exist). */
TEST(mcp_auto_watch_false_skips_watcher_on_connect) {
    int count = auto_watch_connect_watch_count("false");
    if (count < 0) {
        PASS(); /* fixture setup failed (tmpdir/cwd unavailable) — skip */
    }
    ASSERT_EQ(count, 0);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  #853 — auto_watch=false must ALSO gate the SUPERVISED fresh-index
 *          watcher registration (keystone × #849 merge interaction)
 * ══════════════════════════════════════════════════════════════════ */

/* #849 routed ALL watcher registration through register_watcher_if_enabled()
 * (auto_watch gate). The #832 keystone then added a SECOND registration site in
 * autoindex_thread's supervised-success branch, but wired it as a DIRECT
 * cbm_watcher_watch() guarded only by `if (srv->watcher)` — srv->watcher is set
 * unconditionally, so that guard does NOT honour `config set auto_watch false`.
 * The above tests only cover the already-indexed on-connect path
 * (register_watcher_if_enabled); this guard covers the fresh-index SUPERVISED
 * autoindex_thread branch that #832 introduced.
 *
 * Drive the real public entry initialize → maybe_auto_index → autoindex_thread on
 * a supervisor-marked host (kill switch off) with a FRESH project (no prior .db)
 * and auto_watch=false. cbm_mcp_server_free() joins the autoindex thread, so the
 * (buggy or gated) registration decision has run before we read the watch count.
 *
 * RED on the unfixed ungated block: the supervised success branch calls
 * cbm_watcher_watch() unconditionally → watch_count == 1 → IDX853_WATCHER_REGISTERED.
 * GREEN once it calls register_watcher_if_enabled() → auto_watch_off skip → 0.
 * spawn_count is asserted to have advanced so the assertion cannot pass vacuously
 * (i.e. green only because the supervised branch was never entered). */
enum {
    IDX853_OK = 0,                  /* watch_count==0, supervised branch ran → GREEN */
    IDX853_WATCHER_REGISTERED = 61, /* watch_count==1 → RED: ungated cbm_watcher_watch */
    IDX853_NO_SPAWN = 62,           /* spawn_count unchanged → supervised path not exercised */
    IDX853_SETUP_FAIL = 63,         /* config/watcher/server/cwd setup failed */
    IDX853_BAD_COUNT = 64,          /* unexpected watch_count (<0 or >1) */
};

#ifndef _WIN32 /* helper used only by the POSIX fork harness below */
static int idx853_supervised_autowatch_check(const char *repo_dir, const char *cache_dir) {
    /* Become a supervisor host with the kill switch OFF — the real prod MCP
     * server's state. Done in the FORKED CHILD only (see harness) so the parent
     * test-runner's process-wide host mark stays clear (#845 invariant). Bound the
     * worker so a stuck spawn cannot run long under the fork+alarm net. */
    cbm_index_supervisor_mark_host();
    cbm_unsetenv("CBM_INDEX_SUPERVISOR");
    cbm_setenv("CBM_INDEX_MAX_RESTARTS", "1", 1);
    cbm_setenv("CBM_INDEX_WORKER_TIMEOUT_S", "30", 1);

    cbm_config_t *cfg = cbm_config_open(cache_dir);
    cbm_store_t *wstore = cbm_store_open_memory();
    cbm_watcher_t *watcher = wstore ? cbm_watcher_new(wstore, NULL, NULL) : NULL;
    if (!cfg || !watcher) {
        if (watcher) {
            cbm_watcher_free(watcher);
        }
        if (wstore) {
            cbm_store_close(wstore);
        }
        if (cfg) {
            cbm_config_close(cfg);
        }
        return IDX853_SETUP_FAIL;
    }
    /* auto_index=true → maybe_auto_index launches autoindex_thread for the fresh
     * project; auto_watch=false → the gate this guard exercises. */
    cbm_config_set(cfg, CBM_CONFIG_AUTO_INDEX, "true");
    cbm_config_set(cfg, CBM_CONFIG_AUTO_WATCH, "false");

    /* detect_session derives session_root/session_project from the cwd. */
    char old_cwd[1024];
    if (!cbm_getcwd(old_cwd, sizeof(old_cwd)) || cbm_chdir(repo_dir) != 0) {
        cbm_watcher_free(watcher);
        cbm_store_close(wstore);
        cbm_config_close(cfg);
        return IDX853_SETUP_FAIL;
    }

    int spawns_before = cbm_index_supervisor_spawn_count();
    int code = IDX853_SETUP_FAIL;

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    if (srv) {
        cbm_mcp_server_set_watcher(srv, watcher);
        cbm_mcp_server_set_config(srv, cfg);
        char *resp = cbm_mcp_server_handle(
            srv, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
        free(resp);
        /* free() joins the autoindex thread → the supervised worker has finished
         * and the registration decision (buggy or gated) has executed. */
        cbm_mcp_server_free(srv);

        int spawns_after = cbm_index_supervisor_spawn_count();
        int watch_count = cbm_watcher_watch_count(watcher);

        if (spawns_after == spawns_before) {
            code = IDX853_NO_SPAWN; /* supervised branch never ran — not a valid probe */
        } else if (watch_count == 1) {
            code = IDX853_WATCHER_REGISTERED; /* the discriminating RED assertion */
        } else if (watch_count == 0) {
            code = IDX853_OK;
        } else {
            code = IDX853_BAD_COUNT;
        }
    }

    (void)cbm_chdir(old_cwd);
    cbm_watcher_free(watcher);
    cbm_store_close(wstore);
    cbm_config_close(cfg);
    return code;
}
#endif /* !_WIN32 */

TEST(mcp_auto_watch_false_skips_supervised_autoindex_issue853) {
#ifdef _WIN32
    /* Marks the process as a supervisor host (irreversible); POSIX isolates that
     * in a forked child. The gate logic is platform-independent and covered on
     * POSIX CI. */
    SKIP_PLATFORM("supervisor-host guard needs fork isolation (POSIX-only)");
#else
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/cbm-idx853-repo-XXXXXX");
    if (!cbm_mkdtemp(tmp_dir)) {
        PASS();
    }
    char cache[256];
    snprintf(cache, sizeof(cache), "/tmp/cbm-idx853-cache-XXXXXX");
    if (!cbm_mkdtemp(cache)) {
        cbm_rmdir(tmp_dir);
        PASS();
    }

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1); /* inherited by the worker child */

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/main.py", tmp_dir);
    FILE *fp = fopen(src_path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("def idx853_fn():\n    return 'ok'\n", fp);
    fclose(fp);

    int code = -1;
    bool signalled = false;
    int sig = 0;
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(60); /* a stuck worker dies here instead of hanging the runner */
        _exit(idx853_supervised_autowatch_check(tmp_dir, cache));
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        signalled = true;
        sig = WTERMSIG(status);
    }

    char *project = cbm_project_name_from_path(tmp_dir);
    cleanup_project_db(cache, project);
    free(project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    remove(src_path);
    cbm_rmdir(cache);
    cbm_rmdir(tmp_dir);

    if (signalled) {
        printf("    child killed by signal %d (alarm => worker hang)\n", sig);
    } else if (code != IDX853_OK) {
        printf("    child exit code %d (61=watcher registered under auto_watch=false=RED, "
               "62=no spawn, 63=setup fail, 64=bad count)\n",
               code);
    }
    ASSERT_FALSE(signalled);
    ASSERT_EQ(code, IDX853_OK);
    PASS();
#endif
}

/* The containment guard both MCP file-read sinks route through
 * (resolve_snippet_source for get_code_snippet, attach_result_source for
 * search_code). A result path that resolves outside the indexed project root
 * — via a `..` segment or a followed symlink/junction — must be rejected so
 * its contents never reach a tool response. */
extern bool cbm_path_within_root(const char *root_path, const char *abs_path);

TEST(mcp_path_within_root_rejects_escape) {
#ifdef _WIN32
    char root[512];
    char outside[512];
    snprintf(root, sizeof(root), "%s/cbm_pwr_root_XXXXXX", cbm_tmpdir());
    snprintf(outside, sizeof(outside), "%s/cbm_pwr_outside_XXXXXX", cbm_tmpdir());
    ASSERT_NOT_NULL(cbm_mkdtemp(root));
    ASSERT_NOT_NULL(cbm_mkdtemp(outside));

    char inside[700];
    char target[700];
    char junction[700];
    char linked_target[900];
    snprintf(inside, sizeof(inside), "%s/inside.c", root);
    snprintf(target, sizeof(target), "%s/outside.c", outside);
    snprintf(junction, sizeof(junction), "%s/escape", root);
    snprintf(linked_target, sizeof(linked_target), "%s/outside.c", junction);
    FILE *fp = cbm_fopen(inside, "w");
    ASSERT_NOT_NULL(fp);
    fputs("int inside;\n", fp);
    fclose(fp);
    fp = cbm_fopen(target, "w");
    ASSERT_NOT_NULL(fp);
    fputs("int outside;\n", fp);
    fclose(fp);

    /* cbm_tmpdir() can expose the MSYS spelling C:/msys64/...; cmd's mklink
     * builtin treats the slash before "msys64" as another option delimiter.
     * Native backslashes are required only at this cmd.exe fixture boundary. */
    char junction_native[sizeof(junction)];
    char outside_native[sizeof(outside)];
    snprintf(junction_native, sizeof(junction_native), "%s", junction);
    snprintf(outside_native, sizeof(outside_native), "%s", outside);
    for (char *cursor = junction_native; *cursor; cursor++) {
        if (*cursor == '/') {
            *cursor = '\\';
        }
    }
    for (char *cursor = outside_native; *cursor; cursor++) {
        if (*cursor == '/') {
            *cursor = '\\';
        }
    }
    const char *junction_argv[] = {"cmd.exe",       "/d",           "/c", "mklink", "/J",
                                   junction_native, outside_native, NULL};
    bool linked = cbm_exec_no_shell(junction_argv) == 0;

    ASSERT_TRUE(linked);
    ASSERT_TRUE(cbm_path_within_root(root, inside));
    ASSERT_FALSE(cbm_path_within_root(root, target));
    ASSERT_FALSE(cbm_path_within_root(root, linked_target));

    char case_alias[sizeof(root)];
    snprintf(case_alias, sizeof(case_alias), "%s", root);
    char *leaf = strrchr(case_alias, '/');
    char *backslash_leaf = strrchr(case_alias, '\\');
    if (!leaf || (backslash_leaf && backslash_leaf > leaf)) {
        leaf = backslash_leaf;
    }
    leaf = leaf ? leaf + 1 : case_alias;
    if (*leaf >= 'a' && *leaf <= 'z') {
        *leaf = (char)(*leaf - 'a' + 'A');
    } else if (*leaf >= 'A' && *leaf <= 'Z') {
        *leaf = (char)(*leaf - 'A' + 'a');
    }
    ASSERT_TRUE(cbm_path_within_root(case_alias, inside));

    char drive_root[] = {root[0], ':', '\\', '\0'};
    ASSERT_TRUE(((root[0] >= 'A' && root[0] <= 'Z') || (root[0] >= 'a' && root[0] <= 'z')) &&
                root[1] == ':');
    ASSERT_TRUE(cbm_path_within_root(drive_root, inside));

    cbm_rmdir(junction);
    cbm_unlink(inside);
    cbm_unlink(target);
    cbm_rmdir(root);
    cbm_rmdir(outside);
    PASS();
#else
    char root[512];
    snprintf(root, sizeof(root), "%s/cbm_pwr_XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(root)) {
        FAIL("cbm_mkdtemp failed");
    }
    char inside[700];
    snprintf(inside, sizeof(inside), "%s/inside.c", root);
    FILE *fp = fopen(inside, "w");
    ASSERT_NOT_NULL(fp);
    fputs("int x;\n", fp);
    fclose(fp);

    /* The abs_path a sink builds for an in-root result stays contained; a `..`
     * escape to an existing outside file (/etc/hosts) resolves out and must be
     * rejected. */
    char escape[900];
    snprintf(escape, sizeof(escape), "%s/../../../../etc/hosts", root);
    ASSERT_TRUE(cbm_path_within_root(root, inside));
    ASSERT_FALSE(cbm_path_within_root(root, escape));
    ASSERT_FALSE(cbm_path_within_root(root, "/etc/hosts"));
    ASSERT_TRUE(cbm_path_within_root("/", "/etc/hosts"));

    remove(inside);
    cbm_rmdir(root);
    PASS();
#endif
}

/* base_branch is spliced into a `git diff --name-only "<base>"...HEAD` command;
 * a value starting with '-' would be taken by git as an option (e.g.
 * --output=<path> writes the diff to an arbitrary file) rather than a ref. It
 * must be rejected up front, alongside the shell-metacharacter check. */
TEST(detect_changes_rejects_option_like_base_branch) {
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char *resp = cbm_mcp_server_handle(
        srv, "{\"jsonrpc\":\"2.0\",\"id\":77,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"detect_changes\","
             "\"arguments\":{\"project\":\"p\",\"base_branch\":\"--output=/tmp/cbm_pwn\"}}}");
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "invalid characters"));
    free(resp);
    cbm_mcp_server_free(srv);
    PASS();
}

TEST(detect_changes_rejects_windows_cmd_metacharacters_in_base_branch) {
#ifdef _WIN32
    const char *const branches[] = {"topic%PATH%", "topic!name!", "topic^name"};
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    for (size_t i = 0; i < sizeof(branches) / sizeof(branches[0]); i++) {
        char request[512];
        snprintf(request, sizeof(request),
                 "{\"jsonrpc\":\"2.0\",\"id\":78,\"method\":\"tools/call\","
                 "\"params\":{\"name\":\"detect_changes\","
                 "\"arguments\":{\"project\":\"p\",\"base_branch\":\"%s\"}}}",
                 branches[i]);
        char *response = cbm_mcp_server_handle(srv, request);
        ASSERT_NOT_NULL(response);
        ASSERT_NOT_NULL(strstr(response, "base_branch contains invalid characters"));
        free(response);
    }
    cbm_mcp_server_free(srv);
    PASS();
#else
    SKIP_PLATFORM("cmd.exe interpolation validation runs on Windows");
#endif
}

TEST(detect_changes_rejects_windows_cmd_metacharacters_in_project_root) {
#ifdef _WIN32
    const char *const roots[] = {"C:\\cbm-root-%PATH%", "C:\\cbm-root-!name!",
                                 "C:\\cbm-root-^name"};
    const char *project = "windows-cmd-root-validation";
    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *store = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(store);
    cbm_mcp_server_set_project(srv, project);
    mcp_command_hook_probe_t command_probe = {0};
    cbm_mcp_server_set_command_test_hook(srv, mcp_command_hook_probe, &command_probe);

    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        ASSERT_EQ(cbm_store_upsert_project(store, project, roots[i]), CBM_STORE_OK);
        char *response = cbm_mcp_handle_tool(
            srv, "detect_changes",
            "{\"project\":\"windows-cmd-root-validation\",\"base_branch\":\"main\"}");
        ASSERT_NOT_NULL(response);
        ASSERT_NOT_NULL(strstr(response, "project path contains invalid characters"));
        free(response);
    }
    ASSERT_EQ(command_probe.diff_calls, 0);
    ASSERT_EQ(command_probe.merge_base_calls, 0);
    cbm_mcp_server_free(srv);
    PASS();
#else
    SKIP_PLATFORM("cmd.exe interpolation validation runs on Windows");
#endif
}

/* With no boundary configured at all, index_repository must still refuse roots
 * that are too broad or too sensitive to index as a unit. This is the part that
 * holds out of the box: the paths the advisories actually demonstrate are refused
 * without anyone setting an environment variable first. */
TEST(index_repository_refuses_overbroad_roots_by_default) {
    const char *saved = getenv("CBM_ALLOWED_ROOT");
    char *saved_copy = saved ? strdup(saved) : NULL;
    cbm_unsetenv("CBM_ALLOWED_ROOT");

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    /* A top-level system tree: refused on breadth, with no configuration. */
    char *resp = cbm_mcp_handle_tool(srv, "index_repository", "{\"repo_path\":\"/etc\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "too broad") != NULL);
    free(resp);

    /* The filesystem root is refused outright and is never overridable. */
    resp = cbm_mcp_handle_tool(srv, "index_repository", "{\"repo_path\":\"/\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "cannot be indexed") != NULL);
    free(resp);

    cbm_mcp_server_free(srv);
    if (saved_copy) {
        cbm_setenv("CBM_ALLOWED_ROOT", saved_copy, 1);
        free(saved_copy);
    }
    PASS();
}

/* Opt-in workspace boundary: when CBM_ALLOWED_ROOT is set, index_repository
 * must refuse a repo_path that resolves outside it. Unset (the default) imposes
 * no restriction. */
TEST(index_repository_honors_allowed_root) {
    char allowed[512];
    snprintf(allowed, sizeof(allowed), "%s/cbm_allowed_XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(allowed)) {
        FAIL("cbm_mkdtemp failed");
    }
    cbm_setenv("CBM_ALLOWED_ROOT", allowed, 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    char args[1024];
    snprintf(args, sizeof(args),
             "{\"jsonrpc\":\"2.0\",\"id\":88,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"index_repository\","
             "\"arguments\":{\"repo_path\":\"%s/../..\"}}}",
             allowed); /* resolves to a parent, outside the allowed root */
    char *resp = cbm_mcp_server_handle(srv, args);
    ASSERT_NOT_NULL(resp);
    ASSERT_NOT_NULL(strstr(resp, "outside the allowed root"));
    free(resp);

    cbm_unsetenv("CBM_ALLOWED_ROOT");
    cbm_mcp_server_free(srv);
    cbm_rmdir(allowed);
    PASS();
}

TEST(index_repository_relative_path_uses_explicit_session_root) {
    char session_root[512];
    char cache[512];
    snprintf(session_root, sizeof(session_root), "%s/cbm_daemon_session_XXXXXX", cbm_tmpdir());
    snprintf(cache, sizeof(cache), "%s/cbm_daemon_cache_XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(session_root) || !cbm_mkdtemp(cache)) {
        th_rmtree(session_root);
        th_rmtree(cache);
        FAIL("cbm_mkdtemp failed");
    }

    char repo[1024];
    char source[1200];
    snprintf(repo, sizeof(repo), "%s/repo", session_root);
    snprintf(source, sizeof(source), "%s/main.py", repo);
    ASSERT_EQ(th_write_file(source, "def main():\n    return 1\n"), 0);

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    const char *saved_supervisor = getenv("CBM_INDEX_SUPERVISOR");
    char *saved_supervisor_copy = saved_supervisor ? strdup(saved_supervisor) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);
    cbm_setenv("CBM_INDEX_SUPERVISOR", "0", 1);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    bool context_set = srv && cbm_mcp_server_set_session_context(srv, session_root, session_root);
    const char request[] = "{\"jsonrpc\":\"2.0\",\"id\":89,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"index_repository\","
                           "\"arguments\":{\"repo_path\":\"repo\",\"mode\":\"fast\"}}}";
    char *response = context_set ? cbm_mcp_server_handle(srv, request) : NULL;
    bool accepted = response && strstr(response, "outside the allowed root") == NULL &&
                    strstr(response, "\"isError\":true") == NULL;

    char *project = cbm_project_name_from_path(repo);
    char db_path[CBM_SZ_4K];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", cache, project ? project : "missing");
    bool indexed_session_repo = project && cbm_file_size(db_path) >= 0;

    free(response);
    cbm_mcp_server_free(srv);
    cleanup_project_db(cache, project);
    free(project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    if (saved_supervisor_copy) {
        cbm_setenv("CBM_INDEX_SUPERVISOR", saved_supervisor_copy, 1);
    } else {
        cbm_unsetenv("CBM_INDEX_SUPERVISOR");
    }
    free(saved_supervisor_copy);
    th_rmtree(session_root);
    th_rmtree(cache);

    ASSERT_TRUE(context_set);
    ASSERT_TRUE(accepted);
    ASSERT_TRUE(indexed_session_repo);
    PASS();
}

/* A daemon-backed session validates repo_path against its own session root, but
 * the supervised worker is a fresh process that inherits the daemon's cwd. A
 * relative path must therefore be resolved once by the session and forwarded to
 * the worker as that same canonical absolute path. The decoy repo makes an
 * unsanitized handoff observable: forwarding the original "repo" indexes the
 * cwd-relative decoy instead of the validated session repo. */
enum {
    IDXCANON_OK = 0,
    IDXCANON_GETCWD_FAILED = 71,
    IDXCANON_CHDIR_FAILED = 72,
    IDXCANON_NO_SERVER = 73,
    IDXCANON_CONTEXT_FAILED = 74,
    IDXCANON_NO_SPAWN = 75,
    IDXCANON_NO_RESULT = 76,
    IDXCANON_NOT_INDEXED = 77,
    IDXCANON_WRONG_PROJECT = 78,
    IDXCANON_DECOY_INDEXED = 79,
    IDXCANON_TARGET_MISSING = 80,
    IDXCANON_CWD_RESTORE_FAILED = 81,
};

#ifndef _WIN32
static int idxcanon_supervised_session_path_check(const char *session_root, const char *decoy_cwd) {
    char saved_cwd[CBM_SZ_4K];
    if (!cbm_getcwd(saved_cwd, sizeof(saved_cwd))) {
        return IDXCANON_GETCWD_FAILED;
    }
    if (cbm_chdir(decoy_cwd) != 0) {
        return IDXCANON_CHDIR_FAILED;
    }

    /* Match a real supervisor host. Environment changes are isolated to this
     * forked child and inherited by its worker; the parent test process keeps
     * its supervisor kill switch and allowed-root environment untouched. */
    cbm_index_supervisor_mark_host();
    cbm_unsetenv("CBM_INDEX_SUPERVISOR");
    cbm_unsetenv("CBM_ALLOWED_ROOT");
    cbm_setenv("CBM_INDEX_MAX_RESTARTS", "1", 1);
    cbm_setenv("CBM_INDEX_WORKER_TIMEOUT_S", "30", 1);

    char session_repo[CBM_SZ_4K];
    char decoy_repo[CBM_SZ_4K];
    snprintf(session_repo, sizeof(session_repo), "%s/repo", session_root);
    snprintf(decoy_repo, sizeof(decoy_repo), "%s/repo", decoy_cwd);
    char *session_project = cbm_project_name_from_path(session_repo);
    char *decoy_project = cbm_project_name_from_path(decoy_repo);

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    int code = IDXCANON_OK;
    if (!srv) {
        code = IDXCANON_NO_SERVER;
    } else if (!cbm_mcp_server_set_session_context(srv, session_root, session_root)) {
        code = IDXCANON_CONTEXT_FAILED;
    }

    int spawns_before = cbm_index_supervisor_spawn_count();
    char *resp = code == IDXCANON_OK
                     ? cbm_mcp_handle_tool(srv, "index_repository",
                                           "{\"repo_path\":\"repo\",\"mode\":\"fast\"}")
                     : NULL;
    int spawns_after = cbm_index_supervisor_spawn_count();
    if (code == IDXCANON_OK && spawns_after == spawns_before) {
        code = IDXCANON_NO_SPAWN;
    } else if (code == IDXCANON_OK && !resp) {
        code = IDXCANON_NO_RESULT;
    } else if (code == IDXCANON_OK &&
               !response_contains_json_fragment(resp, "\"status\":\"indexed\"")) {
        code = IDXCANON_NOT_INDEXED;
    }

    if (code == IDXCANON_OK) {
        char expected[CBM_SZ_4K];
        snprintf(expected, sizeof(expected), "\"project\":\"%s\"",
                 session_project ? session_project : "");
        if (!session_project || !response_contains_json_fragment(resp, expected)) {
            code = IDXCANON_WRONG_PROJECT;
        }
    }
    free(resp);

    /* A raw "repo" handoff is interpreted relative to decoy_cwd by the worker
     * and creates this project DB. Its absence proves the original JSON did not
     * substitute a different path after the parent validated session_repo. */
    if (code == IDXCANON_OK) {
        const char *cache = getenv("CBM_CACHE_DIR");
        char decoy_db[CBM_SZ_4K];
        snprintf(decoy_db, sizeof(decoy_db), "%s/%s.db", cache ? cache : "",
                 decoy_project ? decoy_project : "");
        if (!cache || !decoy_project || cbm_file_size(decoy_db) >= 0) {
            code = IDXCANON_DECOY_INDEXED;
        }
    }

    if (code == IDXCANON_OK) {
        char query[CBM_SZ_4K];
        snprintf(query, sizeof(query),
                 "{\"project\":\"%s\",\"name_pattern\":\"canonical_target_fn\","
                 "\"label\":\"Function\"}",
                 session_project ? session_project : "");
        char *search = cbm_mcp_handle_tool(srv, "search_graph", query);
        if (!session_project || !search || !strstr(search, "canonical_target_fn")) {
            code = IDXCANON_TARGET_MISSING;
        }
        free(search);
    }

    cbm_mcp_server_free(srv);
    free(session_project);
    free(decoy_project);
    if (cbm_chdir(saved_cwd) != 0 && code == IDXCANON_OK) {
        code = IDXCANON_CWD_RESTORE_FAILED;
    }
    return code;
}
#endif

TEST(index_repository_supervisor_uses_canonical_session_path) {
#ifdef _WIN32
    SKIP_PLATFORM("supervisor-host guard needs fork isolation (POSIX-only)");
#else
    char session_root[512];
    char decoy_cwd[512];
    char cache[512];
    snprintf(session_root, sizeof(session_root), "%s/cbm_canonical_session_XXXXXX", cbm_tmpdir());
    snprintf(decoy_cwd, sizeof(decoy_cwd), "%s/cbm_canonical_decoy_XXXXXX", cbm_tmpdir());
    snprintf(cache, sizeof(cache), "%s/cbm_canonical_cache_XXXXXX", cbm_tmpdir());
    if (!cbm_mkdtemp(session_root) || !cbm_mkdtemp(decoy_cwd) || !cbm_mkdtemp(cache)) {
        th_rmtree(session_root);
        th_rmtree(decoy_cwd);
        th_rmtree(cache);
        FAIL("cbm_mkdtemp failed");
    }

    char session_source[CBM_SZ_4K];
    char decoy_source[CBM_SZ_4K];
    snprintf(session_source, sizeof(session_source), "%s/repo/main.py", session_root);
    snprintf(decoy_source, sizeof(decoy_source), "%s/repo/main.py", decoy_cwd);
    ASSERT_EQ(th_write_file(session_source, "def canonical_target_fn():\n    return 1\n"), 0);
    ASSERT_EQ(th_write_file(decoy_source, "def decoy_fn():\n    return 2\n"), 0);

    const char *saved_cache = getenv("CBM_CACHE_DIR");
    char *saved_cache_copy = saved_cache ? strdup(saved_cache) : NULL;
    cbm_setenv("CBM_CACHE_DIR", cache, 1);

    int code = -1;
    bool signalled = false;
    int sig = 0;
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(60);
        _exit(idxcanon_supervised_session_path_check(session_root, decoy_cwd));
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        signalled = true;
        sig = WTERMSIG(status);
    }

    char session_repo[CBM_SZ_4K];
    char decoy_repo[CBM_SZ_4K];
    snprintf(session_repo, sizeof(session_repo), "%s/repo", session_root);
    snprintf(decoy_repo, sizeof(decoy_repo), "%s/repo", decoy_cwd);
    char *session_project = cbm_project_name_from_path(session_repo);
    char *decoy_project = cbm_project_name_from_path(decoy_repo);
    cleanup_project_db(cache, session_project);
    cleanup_project_db(cache, decoy_project);
    free(session_project);
    free(decoy_project);
    restore_cache_dir(saved_cache_copy);
    free(saved_cache_copy);
    th_rmtree(session_root);
    th_rmtree(decoy_cwd);
    th_rmtree(cache);

    if (signalled) {
        printf("    child killed by signal %d (alarm => worker hang)\n", sig);
    } else if (code != IDXCANON_OK) {
        printf("    child exit code %d (75=no spawn, 77=not indexed, 78=wrong project, "
               "79=decoy indexed, 80=target missing)\n",
               code);
    }
    ASSERT_FALSE(signalled);
    ASSERT_EQ(code, IDXCANON_OK);
    PASS();
#endif
}

/* ══════════════════════════════════════════════════════════════════
 *  SUITE
 * ══════════════════════════════════════════════════════════════════ */

SUITE(mcp) {
    RUN_TEST(mcp_path_within_root_rejects_escape);
    RUN_TEST(detect_changes_rejects_option_like_base_branch);
    RUN_TEST(detect_changes_rejects_windows_cmd_metacharacters_in_base_branch);
    RUN_TEST(detect_changes_rejects_windows_cmd_metacharacters_in_project_root);
    RUN_TEST(index_repository_refuses_overbroad_roots_by_default);
    RUN_TEST(index_repository_honors_allowed_root);
    /* JSON-RPC parsing */
    RUN_TEST(jsonrpc_parse_request);
    RUN_TEST(jsonrpc_parse_notification);
    RUN_TEST(jsonrpc_parse_invalid);
    RUN_TEST(tree_cell_sanitizes_control_and_invalid_utf8);
    RUN_TEST(jsonrpc_parse_tools_call);
    RUN_TEST(jsonrpc_parse_string_id_issue253);
    RUN_TEST(jsonrpc_format_response_string_id_issue253);

    /* JSON-RPC parsing — edge cases */
    RUN_TEST(jsonrpc_parse_empty_string);
    RUN_TEST(jsonrpc_parse_missing_jsonrpc_field);
    RUN_TEST(jsonrpc_parse_missing_method);
    RUN_TEST(jsonrpc_parse_string_id);
    RUN_TEST(jsonrpc_parse_no_params);
    RUN_TEST(jsonrpc_parse_extra_whitespace);
    RUN_TEST(jsonrpc_parse_array_not_object);

    /* JSON-RPC formatting */
    RUN_TEST(jsonrpc_format_response);
    RUN_TEST(jsonrpc_format_error);

    /* MCP protocol helpers */
    RUN_TEST(mcp_initialize_response);
    RUN_TEST(mcp_tools_list);
    RUN_TEST(mcp_tools_help_list_matches_registry);
    RUN_TEST(mcp_tools_list_latest_metadata);
    RUN_TEST(mcp_tools_have_behavior_annotations);
    RUN_TEST(mcp_index_repository_declares_name_override_issue571);
    RUN_TEST(mcp_tools_array_schemas_have_items);
    RUN_TEST(mcp_ingest_traces_items_disallow_additional_properties_issue731);
    RUN_TEST(mcp_get_architecture_aspects_schema_enum_pr560);
    RUN_TEST(mcp_text_result);
    RUN_TEST(mcp_text_result_omits_structured_content_for_plain_text);
    RUN_TEST(mcp_every_tool_result_is_duplication_free);
    RUN_TEST(mcp_cancel_matches_request_id);
    RUN_TEST(mcp_text_result_error);
    RUN_TEST(mcp_text_result_complete_envelope_is_hard_bounded);
    RUN_TEST(mcp_text_result_oversized_dynamic_error_is_hard_bounded);

    /* Argument extraction */
    RUN_TEST(mcp_get_tool_name);
    RUN_TEST(mcp_get_arguments);
    RUN_TEST(mcp_get_string_arg);
    RUN_TEST(mcp_get_int_arg);
    RUN_TEST(mcp_get_bool_arg);

    /* Argument extraction — edge cases */
    RUN_TEST(mcp_get_string_arg_empty_json);
    RUN_TEST(mcp_get_string_arg_empty_object);
    RUN_TEST(mcp_get_string_arg_nested_value);
    RUN_TEST(mcp_get_string_arg_int_value);
    RUN_TEST(mcp_get_int_arg_empty_json);
    RUN_TEST(mcp_get_int_arg_string_value);
    RUN_TEST(mcp_get_int_arg_bool_value);
    RUN_TEST(mcp_get_bool_arg_empty_json);
    RUN_TEST(mcp_get_bool_arg_int_value);
    RUN_TEST(mcp_get_tool_name_empty_json);
    RUN_TEST(mcp_get_tool_name_missing_name);
    RUN_TEST(mcp_get_arguments_empty_json);
    RUN_TEST(mcp_get_arguments_no_arguments_key);

    /* Server protocol handling */
    RUN_TEST(server_handle_initialize);
    RUN_TEST(server_handle_initialized_notification);
    RUN_TEST(server_handle_tools_list);
    RUN_TEST(server_handle_tools_list_defaults_to_all_tools_and_accepts_cursor);
    RUN_TEST(server_handle_analysis_profile_filters_and_rejects_mutators);
    RUN_TEST(server_handle_scout_profile_exposes_only_the_fast_tier);
    RUN_TEST(analysis_profile_arguments_fail_closed_and_disable_http);
    RUN_TEST(hook_windows_path_containment_is_case_insensitive_and_segment_safe);
    RUN_TEST(server_handle_prompts_list_workflows);
    RUN_TEST(server_handle_prompts_get_workflows);
    RUN_TEST(server_handle_prompts_get_validates_arguments);
    RUN_TEST(server_handle_logs_request_without_params);
    RUN_TEST(server_handle_unknown_method);

    /* Server handle — edge cases */
    RUN_TEST(server_handle_invalid_json);
    RUN_TEST(server_handle_empty_object);
    RUN_TEST(server_handle_tools_call_missing_name);

    /* Tool handlers */
    RUN_TEST(tool_list_projects_empty);
    RUN_TEST(tool_get_graph_schema_empty);
    RUN_TEST(tool_unknown_tool);
    RUN_TEST(tool_search_graph_basic);
    RUN_TEST(tool_trace_totals_respect_test_filter);
    RUN_TEST(tool_get_architecture_cycles_detects_scc);
    RUN_TEST(tool_get_code_snippet_outlines_whole_file_node);
    RUN_TEST(tool_search_graph_includes_node_properties);
    RUN_TEST(tool_search_graph_detail_ids_hints_dropped_fields);
    RUN_TEST(tool_search_graph_toon_never_leaks_internal_fields);
    RUN_TEST(tool_lean_defaults_schema_and_status);
    RUN_TEST(tool_output_regression_gate);
    RUN_TEST(tool_output_byte_budgets);
    RUN_TEST(tool_search_graph_query_honors_file_pattern_issue552);
    RUN_TEST(mcp_resource_discovery_methods_return_empty_lists);
    RUN_TEST(tool_query_graph_basic);
    RUN_TEST(tool_query_graph_max_rows_reports_truncation);
    RUN_TEST(tool_query_graph_max_rows_exact_match_is_complete);
    RUN_TEST(tool_query_graph_malformed_query_preserves_actionable_error);
    RUN_TEST(tool_index_status_no_project);
    RUN_TEST(tool_index_status_coverage_pagination_is_exact_bounded_and_generation_bound);
    RUN_TEST(tool_index_status_rejects_invalid_pagination_arguments);
    RUN_TEST(tool_index_status_oversized_identity_marker_advances);
    RUN_TEST(tool_check_index_coverage_finds_path_beyond_status_cap);
    RUN_TEST(tool_check_index_coverage_reports_paths_scopes_and_ranges);
    RUN_TEST(tool_check_index_coverage_preserves_multiple_scope_labels);
    RUN_TEST(tool_index_status_syntactic_pages_hold_one_readonly_snapshot);
    RUN_TEST(tool_syntactic_coverage_routes_page_typed_rows_and_fail_closed);
    RUN_TEST(tool_check_index_coverage_rejects_stale_generation);
    RUN_TEST(tool_check_index_coverage_requires_source_when_file_metadata_changed);
    RUN_TEST(tool_check_index_coverage_surfaces_lookup_errors);
    RUN_TEST(tool_rust_analysis_health_verdicts_are_metadata_gated_and_exact);
    RUN_TEST(tool_rust_analysis_evidence_has_independent_16k_budget);
    RUN_TEST(tool_rust_analysis_pages_mixed_corpus_without_gaps_or_syntactic_contamination);
    RUN_TEST(tool_rust_analysis_page_failures_are_unknown_and_use_shared_version_contract);
    RUN_TEST(tool_index_repository_success_reports_large_semantic_health_corpus);
    RUN_TEST(tool_index_status_includes_git_metadata);

    /* Tool handlers with validation */
    RUN_TEST(tool_trace_call_path_not_found);
    RUN_TEST(tool_call_invalid_project_name_leaves_no_corrupt_litter_issue1425);
    RUN_TEST(tool_trace_missing_function_name);
    RUN_TEST(tool_trace_call_path_ambiguous);
    RUN_TEST(tool_trace_rejects_unknown_edge_type);
    RUN_TEST(tool_trace_reports_port_mediated_callers_separately);
    RUN_TEST(tool_trace_port_mediated_rows_are_budgeted_and_structural_rows_separated);
    RUN_TEST(tool_trace_port_mediated_does_not_silently_drop_ninth_port);
    RUN_TEST(tool_trace_port_mediated_enforces_aggregate_safety_ceiling);
    RUN_TEST(tool_trace_port_mediated_excludes_sibling_impls);
    RUN_TEST(tool_trace_sections_do_not_claim_untraversed_relationship);
    RUN_TEST(tool_trace_union_records_min_hop_across_seeds);
    RUN_TEST(tool_trace_pagination_exactly_once);
    RUN_TEST(tool_trace_legacy_wide_stream_requires_explicit_refinement);
    RUN_TEST(tool_trace_rejects_reachable_set_beyond_safety_ceiling);
    RUN_TEST(tool_trace_data_flow_uses_shortest_path_predecessor_edge_args);
    RUN_TEST(tool_trace_rejects_unimplemented_parameter_filter);
    RUN_TEST(tool_trace_call_path_prefers_definition);
    RUN_TEST(trace_evidence_strategy_class_vocabulary_is_closed);
    RUN_TEST(tool_trace_path_evidence_is_opt_in_and_class_mapped);
    RUN_TEST(tool_trace_call_path_depth_above_ceiling_is_rejected);
    RUN_TEST(tool_trace_call_path_distinct_defs_not_over_unioned);
    RUN_TEST(tool_trace_call_path_dts_stub_unions_with_impl);
    RUN_TEST(tool_delete_project_not_found);
    RUN_TEST(tool_get_architecture_empty);
    RUN_TEST(tool_get_architecture_emits_populated_sections);
    RUN_TEST(tool_get_architecture_reports_entry_point_slice_metadata);
    RUN_TEST(tool_get_architecture_reports_route_slice_metadata);
    RUN_TEST(tool_get_architecture_overview_compact_subset_pr560);
    RUN_TEST(tool_get_architecture_rejects_unknown_aspect_pr560);
    RUN_TEST(tool_get_architecture_accepts_project_name_alias_issue640);
    RUN_TEST(tool_search_graph_accepts_project_name_alias_issue640);
    RUN_TEST(tool_project_arg_resolves_unique_tail_issue1025);
    RUN_TEST(tool_trace_cfg_gated_caller_not_attributed_to_file_node);
    RUN_TEST(tool_trace_generic_impl_caller_not_attributed_to_file_node);
    RUN_TEST(tool_trace_nested_fn_caller_not_attributed_to_file_node);
    RUN_TEST(tool_trace_cfg_gated_method_not_attributed_to_file_node);
    RUN_TEST(tool_trace_file_and_module_nodes_excluded_from_callers_total);
    RUN_TEST(tool_trace_unattributed_pagination_exactly_once_and_budgeted);
    RUN_TEST(tool_trace_unattributed_json_has_unique_directional_keys);
    RUN_TEST(tool_get_architecture_path_scoping);
    RUN_TEST(tool_query_graph_missing_query);

    /* Pipeline-dependent tool handlers */
    RUN_TEST(tool_index_repository_missing_path);
    RUN_TEST(tool_get_code_snippet_missing_qn);
    RUN_TEST(tool_get_code_snippet_not_found);
    RUN_TEST(tool_search_code_missing_pattern);
    RUN_TEST(tool_search_code_negative_limit_is_not_echoed_issue1511);
    RUN_TEST(tool_search_code_limit_declares_a_minimum_issue1511);
    RUN_TEST(tool_search_code_no_project);
    RUN_TEST(search_code_multi_word);
    RUN_TEST(search_code_rejects_limit_above_public_maximum);
    RUN_TEST(search_code_rejects_truncated_grep_command_construction);
    RUN_TEST(search_code_full_response_is_byte_bounded_with_exact_population);
    RUN_TEST(search_code_complete_over_500_and_globally_ranks_late_declaration);
    RUN_TEST(search_code_exact_counts_below_500_and_compact_evidence);
    RUN_TEST(search_code_ranks_source_then_test_then_generated_with_deterministic_ties);
    RUN_TEST(search_code_files_mode_aggregates_complete_matches_directly_by_file);
    RUN_TEST(search_code_equal_span_prefers_symbol_over_structural_file_node);
    RUN_TEST(search_code_rollups_are_exact_over_unreturned_population);
    RUN_TEST(search_code_full_mode_source_is_match_centered_and_bounded);
    RUN_TEST(search_code_complete_above_4096_keeps_exact_totals_and_late_winner);
    RUN_TEST(search_code_centrality_tiebreak_survives_more_than_2046_symbols);
    RUN_TEST(search_code_files_mode_long_lines_and_limit);
    RUN_TEST(search_code_long_line_continuation_is_not_file);
    RUN_TEST(search_code_rejects_non_positive_limit);
    RUN_TEST(search_code_rejects_empty_and_multiline_patterns);
    RUN_TEST(search_code_scoped_path_with_spaces_issue687);
#ifdef _WIN32
    RUN_TEST(search_code_scoped_path_with_cjk_root_issue903);
#endif
    RUN_TEST(search_code_path_filter_prefilter_keeps_matches);
    RUN_TEST(search_code_path_filter_matches_nothing);
    RUN_TEST(search_code_invalid_path_filter_is_an_explicit_error);
    RUN_TEST(search_code_invalid_regex_errors_issue283);
    RUN_TEST(search_code_literal_pipe_warns_issue282);
    RUN_TEST(search_code_ampersand_accepted_issue272);
    RUN_TEST(search_code_preserves_valid_utf8_and_replaces_only_invalid_sequences);
    RUN_TEST(search_code_ordinary_segments_ending_in_gen_remain_source);
    RUN_TEST(search_code_grep_exit_status_distinguishes_match_no_match_and_failure);
    RUN_TEST(search_code_revalidates_indexed_path_after_symlink_replacement);
    RUN_TEST(tool_detect_changes_no_project);
    RUN_TEST(tool_manage_adr_no_project);
    RUN_TEST(tool_manage_adr_get_with_existing_adr);
    RUN_TEST(tool_manage_adr_unified_backend_issue256);
    RUN_TEST(tool_manage_adr_rejects_removed_sections_argument);
    RUN_TEST(tool_index_repository_reports_store_backed_adr);
    RUN_TEST(tool_index_repository_resolves_root_path_from_project_name_issue1211);
    RUN_TEST(tool_index_repository_unknown_project_name_still_requires_repo_path);
    RUN_TEST(tool_index_repository_dot_uses_absolute_project_key_and_preserves_adr);
    RUN_TEST(index_repository_relative_path_uses_explicit_session_root);
    RUN_TEST(index_repository_supervisor_uses_canonical_session_path);
    RUN_TEST(index_repository_cli_name_override_issue823);
    RUN_TEST(index_supervisor_unsafe_clean_is_never_fallback_or_recovery);
    RUN_TEST(index_supervisor_gate_requires_marked_host_issue845);
    RUN_TEST(index_supervisor_start_failure_is_fail_closed_in_real_host);
    RUN_TEST(index_bg_paths_route_through_supervisor_issue832);
    RUN_TEST(sequential_service_edge_props_are_valid_json_issue898);
    RUN_TEST(index_second_inprocess_run_survives_issue773);
    RUN_TEST(index_recovery_parallel_quarantines_crasher);
    RUN_TEST(tool_manage_adr_not_found_rich_error);
    RUN_TEST(tool_manage_adr_get_accepts_abs_path);
    RUN_TEST(tool_manage_adr_get_accepts_symlink_path);
    RUN_TEST(tool_detect_changes_not_found_rich_error);
    RUN_TEST(tool_detect_changes_contained_commands_clean_up_error_and_success);
    RUN_TEST(tool_detect_changes_default_adapts_oversized_shape_with_exact_totals);
    RUN_TEST(detect_changes_node_in_hunks_overlap_issue1363);
    RUN_TEST(detect_changes_seeds_only_touched_symbol_issue1363);
    RUN_TEST(detect_changes_zero_overlap_falls_back_issue1363);
    RUN_TEST(tool_ingest_traces_basic);
    RUN_TEST(tool_ingest_traces_empty);

    /* Query store generation freshness */
    RUN_TEST(query_store_reopens_after_database_replacement);
    /* Query store read-only (data integrity) */
    RUN_TEST(readonly_query_does_not_mutate_db);
    RUN_TEST(readonly_query_succeeds_on_readonly_fs);

    /* Idle store eviction */
    RUN_TEST(store_idle_eviction);
    RUN_TEST(store_idle_no_eviction_within_timeout);
    RUN_TEST(store_idle_evict_protects_initial_store);
    RUN_TEST(store_idle_evict_access_resets_timer);

    /* URI helpers */
    RUN_TEST(parse_file_uri_unix);
    RUN_TEST(parse_file_uri_windows);
    RUN_TEST(parse_file_uri_invalid);

    /* URI helpers — edge cases */
    RUN_TEST(parse_file_uri_http_scheme);
    RUN_TEST(parse_file_uri_ftp_scheme);
    RUN_TEST(parse_file_uri_buffer_too_small);
    RUN_TEST(parse_file_uri_spaces_in_path);
    RUN_TEST(parse_file_uri_null_out_path);
    RUN_TEST(parse_file_uri_zero_size);

    /* Poll/getline FILE* buffering fix */
#ifndef _WIN32
    RUN_TEST(mcp_server_run_rapid_messages);
#endif

    /* Snippet resolution (port of snippet_test.go) */
    RUN_TEST(snippet_exact_qn);
    RUN_TEST(snippet_qn_suffix);
    RUN_TEST(snippet_unique_short_name);
    RUN_TEST(snippet_name_tier);
    RUN_TEST(snippet_ambiguous_short_name);
    RUN_TEST(snippet_not_found);
    RUN_TEST(snippet_fuzzy_suggestions);
    RUN_TEST(snippet_enriched_properties);
    RUN_TEST(tool_output_omits_internal_similarity_fields);
    RUN_TEST(snippet_fuzzy_last_segment);
    RUN_TEST(snippet_auto_resolve_default);
    RUN_TEST(snippet_auto_resolve_enabled);
    RUN_TEST(snippet_include_neighbors_default);
    RUN_TEST(snippet_include_neighbors_enabled);
    RUN_TEST(snippet_source_invalid_utf8);
    RUN_TEST(snippet_budget_fitting_501_line_function_is_complete);
    RUN_TEST(snippet_source_that_exactly_fits_serialized_budget_is_complete);
    RUN_TEST(snippet_2000_line_function_round_trips_across_byte_pages);
    RUN_TEST(snippet_focus_and_explicit_range_are_symbol_bounded);
    RUN_TEST(snippet_focus_window_contains_focus_when_leading_line_exceeds_budget);
    RUN_TEST(snippet_explicit_range_rejects_stale_physical_lines_without_clamping);
    RUN_TEST(snippet_cursor_rejects_stale_source_and_mismatched_symbol);
    RUN_TEST(snippet_cursor_rejects_malformed_and_offset_tampering);
    RUN_TEST(snippet_single_physical_line_larger_than_budget_is_fully_retrievable);
    RUN_TEST(snippet_serialized_result_never_exceeds_escaping_heavy_budget);
    RUN_TEST(snippet_ambiguous_suggestions_obey_serialized_result_budget);
    RUN_TEST(snippet_oversized_module_returns_bounded_exact_outline);
    RUN_TEST(snippet_oversized_class_returns_declaration_and_member_outline);
    RUN_TEST(snippet_partial_coverage_guidance_matches_response_mode);
    RUN_TEST(snippet_class_outline_excludes_adjacent_top_level_symbol_on_closing_line);
    RUN_TEST(tool_bad_project_name_no_overflow_issue235);
    RUN_TEST(tool_bad_project_error_valid_json_issue235);
    RUN_TEST(tool_resolve_store_by_internal_name_issue704);
    RUN_TEST(tool_list_projects_ignores_missed_shadow_issue1044);

    /* auto_watch gate (distilled from PR #625) */
    RUN_TEST(mcp_auto_watch_default_registers_watcher_on_connect);
    RUN_TEST(mcp_auto_watch_false_skips_watcher_on_connect);
    RUN_TEST(mcp_auto_watch_false_skips_supervised_autoindex_issue853);
}

/* Kept separate so daemon-coordination regressions can be iterated without
 * running the much larger MCP behavior suite. */
SUITE(mcp_mutation_guard) {
    RUN_TEST(tool_delete_project_mutation_guard_blocks_then_releases);
    RUN_TEST(tool_index_repository_mutation_guard_blocks_before_local_worker);
    RUN_TEST(tool_manage_adr_mutation_guard_balances_success);
    RUN_TEST(tool_manage_adr_read_paths_skip_blocking_mutation_guard);
    RUN_TEST(tool_manage_adr_read_missing_store_skips_mutation_guard);
    RUN_TEST(tool_manage_adr_legacy_migration_tries_without_blocking);
    RUN_TEST(tool_raw_dispatch_cancel_is_scoped_non_mutating_and_next_request_clean);
    RUN_TEST(tool_outer_request_scope_preserves_predispatch_cancel);
    RUN_TEST(tool_index_repository_early_raw_cancel_survives_index_entry);
    RUN_TEST(tool_cross_repo_mutation_guard_sorts_dedupes_and_unwinds);
    RUN_TEST(tool_cross_repo_mutation_guard_casefolds_aliases_and_order);
    RUN_TEST(tool_cross_repo_rejects_wildcard_mixed_with_named_targets);
    RUN_TEST(tool_cross_repo_checks_cancellation_after_acquiring_leases);
    RUN_TEST(tool_cross_repo_missing_inputs_fail_without_creating_ghost_databases);
    RUN_TEST(tool_cross_repo_dedupes_targets_before_scanning_and_counting);
    RUN_TEST(tool_cross_repo_honors_source_name_override);
    RUN_TEST(tool_corrupt_store_cleanup_guard_is_balanced_and_not_nested);
    RUN_TEST(tool_corrupt_store_cleanup_guard_denial_preserves_db_and_wal);
    RUN_TEST(tool_manage_adr_corrupt_store_busy_is_retryable);
    RUN_TEST(tool_manage_adr_corrupt_store_missing_try_guard_reports_configuration);
    RUN_TEST(tool_corrupt_store_cleanup_rechecks_generation_after_guard_wait);
    RUN_TEST(tool_corrupt_store_cleanup_preserves_existing_backup_and_uses_unique_name);
    RUN_TEST(tool_corrupt_store_cleanup_publish_failure_preserves_db_and_wal);
    RUN_TEST(tool_corrupt_store_cleanup_publishes_complete_wal_snapshot_before_delete);
}
