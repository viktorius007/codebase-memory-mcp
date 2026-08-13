/* rust_cargo.c — Hand-rolled TOML subset parser for Cargo.toml.
 *
 * Recognises only the shapes we need (per RUST_LSP_FOLLOWUP §A3):
 *   - `[section]`, `[a.b.c]`, `[workspace]`, `[dependencies]`
 *   - `key = "string"`, `key = 'string'`, `key = [...]`,
 *     `key = { ... }`
 *   - `members = ["a", "b/c"]`
 *
 * Everything else (numbers, dates, deep tables, comments past EOL) is
 * skipped without error.
 */

#include "rust_cargo.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

bool cbm_cargo_add_target(CBMArena* arena, CBMCargoManifest* manifest,
    CBMCargoTargetKind kind, const char* source_path) {
    return cbm_cargo_add_named_target(arena, manifest, kind, NULL, source_path);
}

bool cbm_cargo_add_named_target(CBMArena* arena, CBMCargoManifest* manifest,
    CBMCargoTargetKind kind, const char* name, const char* source_path) {
    return cbm_cargo_add_routed_target(arena, manifest, kind, name, NULL, source_path, NULL);
}

bool cbm_cargo_add_routed_target(CBMArena* arena, CBMCargoManifest* manifest,
    CBMCargoTargetKind kind, const char* name, const char* package_dir,
    const char* source_path, const char* blocker_root) {
    if (!arena || !manifest || ((!source_path || !source_path[0]) && (!name || !name[0])) ||
        (kind < CBM_CARGO_TARGET_LIB || kind > CBM_CARGO_TARGET_BUILD)) {
        if (manifest) manifest->targets_complete = false;
        return false;
    }
    if (manifest->target_count == manifest->target_cap) {
        int next_cap = manifest->target_cap > 0 ? manifest->target_cap * 2 : 4;
        CBMCargoTarget* next = cbm_arena_alloc(
            arena, (size_t)next_cap * sizeof(CBMCargoTarget));
        if (!next) {
            manifest->targets_complete = false;
            return false;
        }
        if (manifest->targets && manifest->target_count > 0) {
            memcpy(next, manifest->targets,
                   (size_t)manifest->target_count * sizeof(CBMCargoTarget));
        }
        manifest->targets = next;
        manifest->target_cap = next_cap;
    }
    const char *owned_path = source_path ? cbm_arena_strdup(arena, source_path) : NULL;
    const char *owned_name = name ? cbm_arena_strdup(arena, name) : NULL;
    const char *owned_package = package_dir ? cbm_arena_strdup(arena, package_dir) : NULL;
    const char *owned_blocker = blocker_root ? cbm_arena_strdup(arena, blocker_root) : NULL;
    if ((source_path && !owned_path) || (name && !owned_name) ||
        (package_dir && !owned_package) || (blocker_root && !owned_blocker)) {
        manifest->targets_complete = false;
        return false;
    }
    manifest->targets[manifest->target_count++] =
        (CBMCargoTarget){.kind = kind, .name = owned_name, .package_dir = owned_package,
                         .blocker_root = owned_blocker, .source_path = owned_path};
    return true;
}

/* ── Tiny tokenizer ──────────────────────────────────────────── */

static void cargo_record(CBMCargoManifest* out, CBMRustHealthReason reason,
    int start, int end) {
    if (!out) return;
    uint32_t first = start > 0 ? (uint32_t)start : 0;
    uint32_t last = end > start ? (uint32_t)end : first;
    cbm_rust_health_record(&out->health, reason, first, last);
}

static int skip_ws_and_comment(const char* s, int len, int from) {
    while (from < len) {
        char c = s[from];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { from++; continue; }
        if (c == '#') {
            while (from < len && s[from] != '\n') from++;
            continue;
        }
        break;
    }
    return from;
}

static bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/* Parse a bare key (ident-like). Stores arena-allocated copy in *out. */
static int parse_key(CBMArena* a, const char* s, int len, int from,
    const char** out) {
    int start = from;
    if (from < len && s[from] == '"') {
        from++;
        start = from;
        while (from < len && s[from] != '"') {
            if (s[from] == '\\' && from + 1 < len) from += 2;
            else from++;
        }
        *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
        if (from < len && s[from] == '"') from++;
        return from;
    }
    while (from < len && is_ident_char(s[from])) from++;
    if (from > start) {
        *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
    }
    return from;
}

/* Parse a string literal (single or double quoted). */
static int parse_string(CBMArena* a, const char* s, int len, int from,
    const char** out, CBMCargoManifest* manifest) {
    if (from >= len) return from;
    int literal_start = from;
    char q = s[from];
    if (q != '"' && q != '\'') return from;
    from++;
    int start = from;
    while (from < len && s[from] != q) {
        if (s[from] == '\\' && from + 1 < len) from += 2;
        else from++;
    }
    *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
    if (from < len) {
        from++;
    } else {
        cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                     literal_start, len);
    }
    return from;
}

/* Skip a value (used for keys we don't care about). Handles strings,
 * arrays, inline tables, bare values. */
static int skip_value(const char* s, int len, int from, CBMCargoManifest* out) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len) return from;
    int value_start = from;
    char c = s[from];
    if (c == '"' || c == '\'') {
        from++;
        while (from < len && s[from] != c) {
            if (s[from] == '\\' && from + 1 < len) from += 2;
            else from++;
        }
        if (from < len) {
            from++;
        } else {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         value_start, len);
        }
        return from;
    }
    if (c == '[' || c == '{') {
        char open = c, close = (c == '[' ? ']' : '}');
        int depth = 1;
        from++;
        while (from < len && depth > 0) {
            char d = s[from];
            if (d == '"' || d == '\'') {
                from++;
                while (from < len && s[from] != d) {
                    if (s[from] == '\\' && from + 1 < len) from += 2;
                    else from++;
                }
                if (from < len) from++;
                continue;
            }
            if (d == open) depth++;
            else if (d == close) depth--;
            from++;
        }
        if (depth > 0) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         value_start, len);
        }
        return from;
    }
    /* Bare value: skip to end of line. */
    while (from < len && s[from] != '\n' && s[from] != '#') from++;
    return from;
}

/* Parse `[section.path]` header — returns the section name as a flat
 * dotted string, e.g. "dependencies" or "workspace.dependencies". */
static int parse_section(CBMArena* a, const char* s, int len, int from,
    const char** out, CBMCargoManifest* manifest) {
    if (from >= len || s[from] != '[') return from;
    int section_start = from;
    /* Skip leading `[` or `[[`. */
    bool array_of_tables = false;
    from++;
    if (from < len && s[from] == '[') { array_of_tables = true; from++; }
    int start = from;
    while (from < len && s[from] != ']') from++;
    *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
    /* Consume closing `]` (or `]]`). */
    if (from < len) {
        from++;
    } else {
        cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                     section_start, len);
    }
    if (array_of_tables) {
        if (from < len && s[from] == ']') {
            from++;
        } else {
            cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         section_start, from);
        }
    }
    return from;
}

/* For the `[dependencies]` / `[dev-dependencies]` / `[workspace.dependencies]`
 * sections, parse `key = value` lines until the next section. The value
 * may be a string (the version) or an inline table. We capture both
 * shapes — only the key (crate name) and optional `path = "..."` field
 * matter for us. */
static int parse_dep_entry(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[') return from;
    int entry_start = from;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    } else {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                     entry_start, from);
        return skip_value(s, len, from, out);
    }
    const char* path_val = NULL;
    if (from < len && s[from] == '{') {
        /* Inline table — scan for `path = "..."`. */
        int depth = 1;
        from++;
        while (from < len && depth > 0) {
            int field_start = from;
            from = skip_ws_and_comment(s, len, from);
            if (from >= len) break;
            char c = s[from];
            if (c == '}') { depth--; from++; continue; }
            if (c == ',') { from++; continue; }
            /* sub-key */
            const char* sub_key = NULL;
            from = parse_key(a, s, len, from, &sub_key);
            from = skip_ws_and_comment(s, len, from);
            if (from < len && s[from] == '=') {
                from++;
                from = skip_ws_and_comment(s, len, from);
            }
            if (sub_key && strcmp(sub_key, "path") == 0) {
                from = parse_string(a, s, len, from, &path_val, out);
            } else {
                from = skip_value(s, len, from, out);
            }
            if (from <= field_start) {
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                             field_start, field_start + 1);
                from = field_start + 1;
            }
        }
        if (depth > 0) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         entry_start, len);
        }
    } else {
        from = skip_value(s, len, from, out);
    }
    if (key) {
        if (out->dep_count < CBM_CARGO_MAX_DEPS) {
            out->deps[out->dep_count].name = key;
            out->deps[out->dep_count].path = path_val;
            out->dep_count++;
        } else {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_DEP_LIMIT,
                         entry_start, from);
        }
    }
    return from;
}

/* ── Section dispatcher ──────────────────────────────────────── */

static int parse_package_kv(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[') return from;
    int entry_start = from;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    } else {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                     entry_start, from);
        return skip_value(s, len, from, out);
    }
    if (key && (strcmp(key, "autolib") == 0 || strcmp(key, "autobins") == 0)) {
        bool *flag = strcmp(key, "autolib") == 0 ? &out->autolib : &out->autobins;
        if (from + 4 <= len && strncmp(s + from, "true", 4) == 0) {
            *flag = true;
            from += 4;
        } else if (from + 5 <= len && strncmp(s + from, "false", 5) == 0) {
            *flag = false;
            from += 5;
        } else {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, from, from + 1);
            from = skip_value(s, len, from, out);
        }
    } else if (key && strcmp(key, "build") == 0) {
        if (from + 5 <= len && strncmp(s + from, "false", 5) == 0) {
            out->auto_build = false;
            from += 5;
        } else {
            int value_start = from;
            from = parse_string(a, s, len, from, &out->build_path, out);
            if (from == value_start) {
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                             value_start, value_start + 1);
                from = skip_value(s, len, from, out);
            } else {
                out->auto_build = false;
            }
        }
    } else if (key && strcmp(key, "name") == 0) {
        int value_start = from;
        from = parse_string(a, s, len, from, &out->package_name, out);
        if (from == value_start) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         value_start, value_start + 1);
            from = skip_value(s, len, from, out);
        }
    } else if (key && strcmp(key, "version") == 0) {
        int value_start = from;
        from = parse_string(a, s, len, from, &out->package_version, out);
        if (from == value_start) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         value_start, value_start + 1);
            from = skip_value(s, len, from, out);
        }
    } else if (key && strcmp(key, "version") != 0) {
        from = skip_value(s, len, from, out);
    }
    return from;
}

static int parse_target_kv(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out, const char** target_name, const char** target_path) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[') return from;
    int entry_start = from;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] != '=') {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                     entry_start, from);
        return skip_value(s, len, from, out);
    }
    from = skip_ws_and_comment(s, len, from + 1);
    if (key && (strcmp(key, "path") == 0 || strcmp(key, "name") == 0)) {
        const char** dst = strcmp(key, "path") == 0 ? target_path : target_name;
        int value_start = from;
        from = parse_string(a, s, len, from, dst, out);
        if (from == value_start || !*dst) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         value_start, from > value_start ? from : value_start + 1);
            if (from == value_start) from = skip_value(s, len, from, out);
        }
    } else {
        from = skip_value(s, len, from, out);
    }
    return from;
}

static int parse_workspace_kv(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[') return from;
    int entry_start = from;
    out->is_workspace_root = true;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    } else {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                     entry_start, from);
        return skip_value(s, len, from, out);
    }
    bool is_members = key && strcmp(key, "members") == 0;
    bool is_exclude = key && strcmp(key, "exclude") == 0;
    if ((is_members || is_exclude) && from < len && s[from] == '[') {
        from++;
        while (from < len && s[from] != ']') {
            int member_item_start = from;
            from = skip_ws_and_comment(s, len, from);
            if (from < len && (s[from] == '"' || s[from] == '\'')) {
                int member_start = from;
                const char* mem = NULL;
                from = parse_string(a, s, len, from, &mem, out);
                if (mem) {
                    int *count = is_members ? &out->member_count : &out->exclude_count;
                    CBMCargoMember *items = is_members ? out->members : out->excludes;
                    if (*count < CBM_CARGO_MAX_MEMBERS) {
                        /* Derive a member NAME from the path's last segment. */
                        const char* last = mem;
                        for (const char* p = mem; *p; p++) {
                            if (*p == '/') last = p + 1;
                        }
                        items[*count].member_name = last;
                        items[*count].member_path = mem;
                        (*count)++;
                    } else {
                        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_MEMBER_LIMIT,
                                     member_start, from);
                    }
                }
            } else if (from < len && s[from] != ']') {
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                             from, from + 1);
                while (from < len && s[from] != ',' && s[from] != ']') from++;
            }
            from = skip_ws_and_comment(s, len, from);
            if (from < len && s[from] == ',') from++;
            from = skip_ws_and_comment(s, len, from);
            if (from <= member_item_start && from < len) {
                from = member_item_start + 1;
            }
        }
        if (from < len) {
            from++;  /* consume `]` */
        } else {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         entry_start, len);
        }
    } else {
        from = skip_value(s, len, from, out);
    }
    return from;
}

void cbm_cargo_parse(CBMArena* arena, const char* src, int src_len,
    CBMCargoManifest* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->autolib = true;
    out->autobins = true;
    out->auto_build = true;
    out->targets_complete = true;
    if (!arena || !src) {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, 0, 0);
        return;
    }
    if (src_len <= 0) src_len = (int)strlen(src);

    int from = 0;
    /* Default: pre-header content treated as [package]. */
    const char* section = "package";
    CBMCargoTargetKind target_kind = 0;
    const char* target_name = NULL;
    const char* target_path = NULL;

    while (from < src_len) {
        from = skip_ws_and_comment(src, src_len, from);
        if (from >= src_len) break;
        int item_start = from;
        if (src[from] == '[') {
            if (target_kind != 0 && (target_name || target_path)) {
                if (!cbm_cargo_add_named_target(arena, out, target_kind, target_name,
                                                target_path)) {
                    cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, item_start,
                                 item_start + 1);
                }
            }
            target_kind = 0;
            target_name = NULL;
            target_path = NULL;
            const char* hdr = NULL;
            from = parse_section(arena, src, src_len, from, &hdr, out);
            section = hdr ? hdr : "";
            if (strcmp(section, "lib") == 0) {
                target_kind = CBM_CARGO_TARGET_LIB;
                out->has_lib_table = true;
            }
            if (strcmp(section, "bin") == 0) target_kind = CBM_CARGO_TARGET_BIN;
            if (strcmp(section, "example") == 0) target_kind = CBM_CARGO_TARGET_EXAMPLE;
            if (strcmp(section, "test") == 0) target_kind = CBM_CARGO_TARGET_TEST;
            if (strcmp(section, "bench") == 0) target_kind = CBM_CARGO_TARGET_BENCH;
        } else if (!section) {
            from = skip_value(src, src_len, from, out);
        } else if (strcmp(section, "package") == 0) {
            from = parse_package_kv(arena, src, src_len, from, out);
        } else if (strcmp(section, "lib") == 0) {
            from = parse_target_kv(arena, src, src_len, from, out, &target_name, &target_path);
        } else if (strcmp(section, "bin") == 0 || strcmp(section, "example") == 0 ||
                   strcmp(section, "test") == 0 || strcmp(section, "bench") == 0) {
            from = parse_target_kv(arena, src, src_len, from, out, &target_name, &target_path);
        } else if (strcmp(section, "workspace") == 0) {
            from = parse_workspace_kv(arena, src, src_len, from, out);
        } else if (strcmp(section, "dependencies") == 0 ||
                   strcmp(section, "dev-dependencies") == 0 ||
                   strcmp(section, "build-dependencies") == 0 ||
                   strcmp(section, "workspace.dependencies") == 0) {
            from = parse_dep_entry(arena, src, src_len, from, out);
        } else {
            /* Section we don't care about — skip the line. */
            while (from < src_len && src[from] != '\n' && src[from] != '[') {
                from++;
            }
        }
        if (from <= item_start) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL,
                         item_start, item_start + 1);
            from = item_start + 1;
        }
    }
    if (target_kind != 0 && (target_name || target_path) &&
        !cbm_cargo_add_named_target(arena, out, target_kind, target_name, target_path)) {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, src_len, src_len);
    }
}

bool cbm_cargo_is_known_dep(const CBMCargoManifest* m, const char* head) {
    if (!m || !head) return false;
    for (int i = 0; i < m->dep_count; i++) {
        if (m->deps[i].name && strcmp(m->deps[i].name, head) == 0) {
            return true;
        }
    }
    for (int i = 0; i < m->member_count; i++) {
        if (m->members[i].member_name &&
            strcmp(m->members[i].member_name, head) == 0) {
            return true;
        }
    }
    return false;
}

const CBMCargoMember* cbm_cargo_find_member(const CBMCargoManifest* m,
    const char* name) {
    if (!m || !name) return NULL;
    for (int i = 0; i < m->member_count; i++) {
        if (m->members[i].member_name &&
            strcmp(m->members[i].member_name, name) == 0) {
            return &m->members[i];
        }
    }
    return NULL;
}
