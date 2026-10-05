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

/* ── Tiny tokenizer ──────────────────────────────────────────── */

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
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
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
    const char** out) {
    if (from >= len) return from;
    char q = s[from];
    if (q != '"' && q != '\'') return from;
    from++;
    int start = from;
    while (from < len && s[from] != q) {
        if (s[from] == '\\' && from + 1 < len) from += 2;
        else from++;
    }
    *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
    if (from < len) from++;
    return from;
}

static int parse_bool(const char *s, int len, int from, bool *out) {
    if (from + 4 <= len && strncmp(s + from, "true", 4) == 0) {
        *out = true;
        return from + 4;
    }
    if (from + 5 <= len && strncmp(s + from, "false", 5) == 0) {
        *out = false;
        return from + 5;
    }
    return from;
}

/* Skip a value (used for keys we don't care about). Handles strings,
 * arrays, inline tables, bare values. */
static int skip_value(const char* s, int len, int from) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len) return from;
    char c = s[from];
    if (c == '"' || c == '\'') {
        from++;
        while (from < len && s[from] != c) {
            if (s[from] == '\\' && from + 1 < len) from += 2;
            else from++;
        }
        if (from < len) from++;
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
        return from;
    }
    /* Bare value (`true`, `1`): ends at the line, or at the delimiter of the
     * inline table or array holding it. */
    while (from < len && s[from] != '\n' && s[from] != '#' && s[from] != ',' &&
           s[from] != '}' && s[from] != ']')
        from++;
    return from;
}

/* Parse `[section.path]` header — returns the section name as a flat
 * dotted string, e.g. "dependencies" or "workspace.dependencies". */
static int parse_section(CBMArena* a, const char* s, int len, int from,
    const char** out) {
    if (from >= len || s[from] != '[') return from;
    /* Skip leading `[` or `[[`. */
    bool array_of_tables = false;
    from++;
    if (from < len && s[from] == '[') { array_of_tables = true; from++; }
    int start = from;
    while (from < len && s[from] != ']') from++;
    *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
    /* Consume closing `]` (or `]]`). */
    if (from < len) from++;
    if (array_of_tables && from < len && s[from] == ']') from++;
    return from;
}

static CBMCargoTarget *append_target(CBMArena *arena, CBMCargoManifest *manifest,
                                     CBMCargoTargetKind kind) {
    if (manifest->target_count == manifest->target_capacity) {
        int capacity = manifest->target_capacity ? manifest->target_capacity * 2 : 8;
        CBMCargoTarget *targets = (CBMCargoTarget *)cbm_arena_alloc(
            arena, (size_t)capacity * sizeof(*targets));
        if (!targets)
            return NULL;
        if (manifest->target_count > 0) {
            memcpy(targets, manifest->targets,
                   (size_t)manifest->target_count * sizeof(*targets));
        }
        manifest->targets = targets;
        manifest->target_capacity = capacity;
    }
    CBMCargoTarget *target = &manifest->targets[manifest->target_count++];
    memset(target, 0, sizeof(*target));
    target->kind = kind;
    return target;
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
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    }
    const char* path_val = NULL;
    if (from < len && s[from] == '{') {
        /* Inline table — scan for `path = "..."`. */
        int depth = 1;
        from++;
        while (from < len && depth > 0) {
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
                from = parse_string(a, s, len, from, &path_val);
            } else {
                from = skip_value(s, len, from);
            }
        }
    } else {
        from = skip_value(s, len, from);
    }
    if (key && out->dep_count < CBM_CARGO_MAX_DEPS) {
        out->deps[out->dep_count].name = key;
        out->deps[out->dep_count].path = path_val;
        out->dep_count++;
    }
    return from;
}

/* ── Section dispatcher ──────────────────────────────────────── */

static int parse_package_kv(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[') return from;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    }
    if (key && strcmp(key, "name") == 0) {
        from = parse_string(a, s, len, from, &out->package_name);
    } else if (key && strcmp(key, "version") == 0) {
        from = parse_string(a, s, len, from, &out->package_version);
    } else if (key && strcmp(key, "edition") == 0) {
        from = parse_string(a, s, len, from, &out->edition);
    } else if (key && strcmp(key, "edition.workspace") == 0) {
        from = parse_bool(s, len, from, &out->edition_inherited);
    } else {
        static const char *const auto_keys[] = {
            "autolib", "autobins", "autotests", "autoexamples", "autobenches",
        };
        int auto_index = -1;
        for (int i = 0; i < 5; i++) {
            if (key && strcmp(key, auto_keys[i]) == 0) {
                auto_index = i;
                break;
            }
        }
        int parsed = auto_index >= 0 ? parse_bool(s, len, from, &out->auto_targets[auto_index])
                                     : from;
        if (parsed != from) {
            out->auto_target_overrides |= 1u << auto_index;
            from = parsed;
        } else {
            from = skip_value(s, len, from);
        }
    }
    return from;
}

static int parse_target_kv(CBMArena *a, const char *s, int len, int from,
                           CBMCargoManifest *out, CBMCargoTarget *target) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[')
        return from;
    const char *key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    }
    if (key && strcmp(key, "name") == 0) {
        from = parse_string(a, s, len, from, &target->name);
        if (target->kind == CBM_CARGO_TARGET_LIB)
            out->lib_name = target->name;
    } else if (key && strcmp(key, "path") == 0) {
        from = parse_string(a, s, len, from, &target->path);
    } else {
        from = skip_value(s, len, from);
    }
    return from;
}

static int parse_workspace_package_kv(CBMArena *a, const char *s, int len, int from,
                                      CBMCargoManifest *out) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[')
        return from;
    const char *key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    }
    if (key && strcmp(key, "edition") == 0)
        return parse_string(a, s, len, from, &out->workspace_edition);
    return skip_value(s, len, from);
}

static int parse_workspace_kv(CBMArena *a, const char *s, int len, int from,
                              CBMCargoManifest *out) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[') return from;
    out->is_workspace_root = true;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    }
    if (key && strcmp(key, "members") == 0 && from < len && s[from] == '[') {
        from++;
        while (from < len && s[from] != ']') {
            from = skip_ws_and_comment(s, len, from);
            if (from < len && (s[from] == '"' || s[from] == '\'')) {
                const char* mem = NULL;
                from = parse_string(a, s, len, from, &mem);
                if (mem && out->member_count < CBM_CARGO_MAX_MEMBERS) {
                    /* Derive a member NAME from the path's last segment. */
                    const char* last = mem;
                    for (const char* p = mem; *p; p++) {
                        if (*p == '/') last = p + 1;
                    }
                    out->members[out->member_count].member_name = last;
                    out->members[out->member_count].member_path = mem;
                    out->member_count++;
                }
            }
            from = skip_ws_and_comment(s, len, from);
            if (from < len && s[from] == ',') from++;
            from = skip_ws_and_comment(s, len, from);
        }
        if (from < len) from++;  /* consume `]` */
    } else {
        from = skip_value(s, len, from);
    }
    return from;
}

/* `[dependencies]`, `[dev-dependencies]`, `[build-dependencies]`, their
 * `[target.<cfg>.…]` forms, and `[workspace.dependencies]`. */
static bool cargo_is_dependency_section(const char *section) {
    static const char *const kinds[] = {"dependencies", "dev-dependencies", "build-dependencies"};
    const char *tail = section;
    if (strncmp(section, "target.", 7) == 0) {
        tail = strrchr(section, '.');
        tail = tail ? tail + 1 : section;
    }
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (strcmp(tail, kinds[i]) == 0) {
            return true;
        }
    }
    return strcmp(section, "workspace.dependencies") == 0;
}

/* The last component of a dependency table is the declared crate alias,
 * rather than an entry key. Its parent uses the same section rules as an
 * inline dependency, including target and workspace sections. */
static const char *cargo_dependency_table_name(CBMArena *arena, const char *section) {
    const char *dot = strrchr(section, '.');
    if (!dot || !dot[1]) return NULL;
    const char *parent = cbm_arena_strndup(arena, section, (size_t)(dot - section));
    if (!cargo_is_dependency_section(parent)) return NULL;
    const char *name = NULL;
    int length = (int)strlen(dot + 1);
    return parse_key(arena, dot + 1, length, 0, &name) == length ? name : NULL;
}

static int parse_dependency_table_field(CBMArena *arena, const char *source, int length,
                                         int from, CBMCargoDep *dep) {
    const char *key = NULL;
    from = parse_key(arena, source, length, from, &key);
    from = skip_ws_and_comment(source, length, from);
    if (from < length && source[from] == '=') from++;
    from = skip_ws_and_comment(source, length, from);
    return key && strcmp(key, "path") == 0
        ? parse_string(arena, source, length, from, &dep->path)
        : skip_value(source, length, from);
}

void cbm_cargo_parse(CBMArena* arena, const char* src, int src_len,
    CBMCargoManifest* out) {
    if (!arena || !src || !out) return;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 5; i++)
        out->auto_targets[i] = true;
    if (src_len <= 0) src_len = (int)strlen(src);

    int from = 0;
    /* Default: pre-header content treated as [package]. */
    const char* section = "package";
    CBMCargoTarget *target = NULL;
    CBMCargoDep *dependency_table = NULL;

    while (from < src_len) {
        from = skip_ws_and_comment(src, src_len, from);
        if (from >= src_len) break;
        if (src[from] == '[') {
            const char* hdr = NULL;
            from = parse_section(arena, src, src_len, from, &hdr);
            section = hdr ? hdr : "";
            target = NULL;
            dependency_table = NULL;
            const char *dependency_name = cargo_dependency_table_name(arena, section);
            if (dependency_name && out->dep_count < CBM_CARGO_MAX_DEPS) {
                dependency_table = &out->deps[out->dep_count++];
                dependency_table->name = dependency_name;
            }
            if (strcmp(section, "lib") == 0)
                target = append_target(arena, out, CBM_CARGO_TARGET_LIB);
            else if (strcmp(section, "bin") == 0)
                target = append_target(arena, out, CBM_CARGO_TARGET_BIN);
            else if (strcmp(section, "test") == 0)
                target = append_target(arena, out, CBM_CARGO_TARGET_TEST);
            else if (strcmp(section, "example") == 0)
                target = append_target(arena, out, CBM_CARGO_TARGET_EXAMPLE);
            else if (strcmp(section, "bench") == 0)
                target = append_target(arena, out, CBM_CARGO_TARGET_BENCH);
            continue;
        }
        if (!section) {
            from = skip_value(src, src_len, from);
            continue;
        }
        if (strcmp(section, "package") == 0) {
            from = parse_package_kv(arena, src, src_len, from, out);
        } else if (target) {
            from = parse_target_kv(arena, src, src_len, from, out, target);
        } else if (strcmp(section, "workspace") == 0) {
            from = parse_workspace_kv(arena, src, src_len, from, out);
        } else if (strcmp(section, "workspace.package") == 0) {
            from = parse_workspace_package_kv(arena, src, src_len, from, out);
        } else if (dependency_table) {
            from = parse_dependency_table_field(arena, src, src_len, from, dependency_table);
        } else if (cargo_is_dependency_section(section)) {
            from = parse_dep_entry(arena, src, src_len, from, out);
        } else {
            /* Section we don't care about — skip the line. */
            while (from < src_len && src[from] != '\n' && src[from] != '[') {
                from++;
            }
        }
    }
}

static bool target_auto_enabled(const CBMCargoManifest *manifest, CBMCargoTargetKind kind,
                                const char *inherited_edition) {
    if (manifest->auto_target_overrides & (1u << kind))
        return manifest->auto_targets[kind];
    const char *edition = manifest->edition_inherited ? inherited_edition : manifest->edition;
    bool modern = edition && strcmp(edition, "2018") >= 0;
    return modern || manifest->target_count == 0;
}

void cbm_cargo_record_discovered_target(CBMArena *arena, CBMCargoManifest *manifest,
                                        CBMCargoTargetKind kind, const char *name,
                                        const char *path, const char *inherited_edition) {
    if (!arena || !manifest || !path)
        return;
    for (int i = 0; i < manifest->target_count; i++) {
        CBMCargoTarget *target = &manifest->targets[i];
        bool same_name = target->name && name && strcmp(target->name, name) == 0;
        if (target->kind == kind && !target->path &&
            (kind == CBM_CARGO_TARGET_LIB || same_name)) {
            target->path = cbm_arena_strdup(arena, path);
            return;
        }
        if (target->kind == kind && target->path && strcmp(target->path, path) == 0)
            return;
    }
    if (!target_auto_enabled(manifest, kind, inherited_edition))
        return;
    CBMCargoTarget *target = append_target(arena, manifest, kind);
    if (!target)
        return;
    target->name = name ? cbm_arena_strdup(arena, name) : NULL;
    target->path = cbm_arena_strdup(arena, path);
}

bool cbm_cargo_is_known_dep(const CBMCargoManifest* m, const char* head) {
    if (!m || !head) return false;
    for (int i = 0; i < m->dep_count; i++) {
        if (m->deps[i].name && strcmp(m->deps[i].name, head) == 0) {
            return true;
        }
    }
    return cbm_cargo_find_member(m, head) != NULL;
}

/* Cargo exposes a package `a-b` to Rust code as `a_b`. */
static bool cargo_name_eq(const char *declared, const char *head) {
    if (!declared || !head)
        return false;
    for (; *declared && *head; declared++, head++) {
        char d = *declared == '-' ? '_' : *declared;
        char h = *head == '-' ? '_' : *head;
        if (d != h)
            return false;
    }
    return *declared == '\0' && *head == '\0';
}

const CBMCargoMember* cbm_cargo_find_member(const CBMCargoManifest* m,
    const char* name) {
    if (!m || !name) return NULL;
    for (int i = 0; i < m->member_count; i++) {
        const CBMCargoMember *member = &m->members[i];
        if (cargo_name_eq(member->crate_name, name) || cargo_name_eq(member->lib_name, name)) {
            return member;
        }
    }
    for (int i = 0; i < m->member_count; i++) {
        if (m->members[i].member_name && strcmp(m->members[i].member_name, name) == 0) {
            return &m->members[i];
        }
    }
    return NULL;
}

void cbm_cargo_set_member_names(CBMCargoMember *member, const char *crate_name,
                                const char *lib_name, const CBMCargoTarget *targets,
                                int target_count) {
    if (!member)
        return;
    member->crate_name = crate_name;
    member->lib_name = lib_name;
    member->targets = targets;
    member->target_count = target_count;
}
