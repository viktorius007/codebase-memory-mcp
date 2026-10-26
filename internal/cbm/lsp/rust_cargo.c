/* rust_cargo.c — Cargo manifest reader. TOML package fields accept string,
 * dotted-key, inline-table and subtable spellings, including TOML 1.1.
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
#include <stdint.h>

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
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/* Cargo uses TOML strings and tables, including quoted/dotted keys and
 * multiline strings: https://doc.rust-lang.org/cargo/reference/manifest.html */
static const char *cargo_toml_string(CBMArena *arena, const char *s, size_t len) {
    if (len < 2 || (s[0] != '\'' && s[0] != '"'))
        return NULL;
    char quote = s[0];
    size_t delimiter = len >= 6 && s[1] == quote && s[2] == quote ? 3 : 1;
    size_t end = len - delimiter, pos = delimiter, used = 0;
    char *out = cbm_arena_alloc(arena, len + 1);
    if (!out)
        return NULL;
    if (delimiter == 3) {
        if (pos < end && s[pos] == '\r')
            pos++;
        if (pos < end && s[pos] == '\n')
            pos++;
    }
    while (pos < end) {
        unsigned char c = (unsigned char)s[pos++];
        if (c == '\r' && pos < end && s[pos] == '\n')
            continue;
        if (c != '\\' || quote == '\'') {
            out[used++] = (char)c;
            continue;
        }
        if (pos >= end)
            return NULL;
        c = (unsigned char)s[pos++];
        if (delimiter == 3 && isspace(c)) {
            while (pos < end && isspace((unsigned char)s[pos]))
                pos++;
            continue;
        }
        const char *escapes = "btnfre\"\\";
        const char *values = "\b\t\n\f\r\x1b\"\\";
        const char *escape = strchr(escapes, c);
        if (escape) {
            out[used++] = values[escape - escapes];
            continue;
        }
        int digits = c == 'x' ? 2 : c == 'u' ? 4 : c == 'U' ? 8 : 0;
        if (!digits || pos + (size_t)digits > end)
            return NULL;
        uint32_t cp = 0;
        for (int i = 0; i < digits; i++) {
            unsigned char h = (unsigned char)s[pos++];
            int d = h >= '0' && h <= '9'   ? h - '0'
                    : h >= 'a' && h <= 'f' ? h - 'a' + 10
                    : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                           : -1;
            if (d < 0)
                return NULL;
            cp = (cp << 4) | (uint32_t)d;
        }
        if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return NULL;
        if (cp < 0x80)
            out[used++] = (char)cp;
        else {
            if (cp >= 0x10000)
                out[used++] = (char)(0xf0 | (cp >> 18));
            else if (cp >= 0x800)
                out[used++] = (char)(0xe0 | (cp >> 12));
            else
                out[used++] = (char)(0xc0 | (cp >> 6));
            if (cp >= 0x10000)
                out[used++] = (char)(0x80 | ((cp >> 12) & 0x3f));
            if (cp >= 0x800)
                out[used++] = (char)(0x80 | ((cp >> 6) & 0x3f));
            out[used++] = (char)(0x80 | (cp & 0x3f));
        }
    }
    out[used] = '\0';
    return out;
}

static int cargo_string_end(const char *s, int len, int from) {
    char quote = s[from];
    bool multiline = from + 2 < len && s[from + 1] == quote && s[from + 2] == quote;
    int delimiter = multiline ? 3 : 1;
    from += delimiter;
    while (from < len) {
        if (!multiline && (s[from] == '\n' || s[from] == '\r')) return -1;
        if (quote == '"' && s[from] == '\\') {
            from += from + 1 < len ? 2 : 1;
            continue;
        }
        if (s[from] == quote) {
            int end = from;
            while (end < len && s[end] == quote) end++;
            if (end - from >= delimiter) return multiline ? end : from + 1;
            from = end;
        } else from++;
    }
    return -1;
}

static int parse_string(CBMArena *arena, const char *s, int len, int from,
                          const char **out) {
    if (from >= len || (s[from] != '"' && s[from] != '\'')) return from;
    int end = cargo_string_end(s, len, from);
    *out = end >= 0 ? cargo_toml_string(arena, s + from, (size_t)(end - from)) : NULL;
    return end >= 0 ? end : len;
}

/* Parse a bare key (ident-like). Stores arena-allocated copy in *out. */
static int parse_key(CBMArena* a, const char* s, int len, int from,
    const char** out) {
    int start = from;
    if (from < len && (s[from] == '"' || s[from] == '\'')) {
        return parse_string(a, s, len, from, out);
    }
    while (from < len && is_ident_char(s[from])) from++;
    if (from > start) {
        *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
    }
    return from;
}

static int parse_bool(const char *s, int len, int from, bool *out) {
    int size = from + 4 <= len && strncmp(s + from, "true", 4) == 0 ? 4 :
               from + 5 <= len && strncmp(s + from, "false", 5) == 0 ? 5 : 0;
    if (!size || (from + size < len && !strchr(" \t\r\n#,}]", s[from + size])))
        return from;
    *out = size == 4;
    return from + size;
}

/* Skip a value (used for keys we don't care about). Handles strings,
 * arrays, inline tables, bare values. */
static int skip_value(const char* s, int len, int from) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len) return from;
    char c = s[from];
    if (c == '"' || c == '\'') {
        int end = cargo_string_end(s, len, from);
        return end >= 0 ? end : len;
    }
    if (c == '[' || c == '{') {
        char open = c, close = (c == '[' ? ']' : '}');
        int depth = 1;
        from++;
        while (from < len && depth > 0) {
            char d = s[from];
            if (d == '"' || d == '\'') {
                int end = cargo_string_end(s, len, from);
                from = end >= 0 ? end : len;
                continue;
            }
            if (d == '#') {
                while (from < len && s[from] != '\n') from++;
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

/* Quoted "edition.workspace" is one key, unlike edition.workspace. */
static int cargo_key_path(CBMArena *arena, const char *s, int len, int from,
                            const char **out) {
    const char *path = "";
    while (from < len) {
        while (from < len && (s[from] == ' ' || s[from] == '\t')) from++;
        const char *part = NULL;
        int start = from;
        if (from < len && (s[from] == '"' || s[from] == '\''))
            from = parse_string(arena, s, len, from, &part);
        else {
            while (from < len && is_ident_char(s[from]) && s[from] != '.') from++;
            if (from > start) part = cbm_arena_strndup(arena, s + start, (size_t)(from - start));
        }
        if (!part || !*part || strchr(part, '\x1f')) return start;
        path = cbm_arena_sprintf(arena, "%s%s%s", path, *path ? "\x1f" : "", part);
        while (from < len && (s[from] == ' ' || s[from] == '\t')) from++;
        if (from >= len || s[from] != '.') break;
        from++;
    }
    *out = path;
    return from;
}

/* The section a workspace root's inheritable dependencies are declared in. */
#define CARGO_WORKSPACE_DEPS "workspace\x1f" "dependencies"

/* One field of a dependency, in any of its spellings (`dep.path = ...`,
 * `dep = { path = ... }`, a `[dependencies.dep]` table): `path` (a local
 * crate) and `workspace = true` (inherited from the workspace root) are kept,
 * any other value skipped. */
/* A TOML array of strings into `*values` (arena, `*count` entries). A value
 * that is not such an array is skipped and leaves `*count` at 0. */
static int parse_string_array(CBMArena *arena, const char *s, int len, int from,
                              const char ***values, int *count) {
    *count = 0;
    if (from >= len || s[from] != '[')
        return skip_value(s, len, from);
    int capacity = 0;
    for (int at = from + 1; at < len && s[at] != ']'; at++)
        capacity += s[at] == '"' || s[at] == '\'' ? 1 : 0;
    const char **out = (const char **)cbm_arena_alloc(arena, (size_t)(capacity + 1) * sizeof(*out));
    if (!out)
        return skip_value(s, len, from);
    from++;
    while (from < len && s[from] != ']') {
        from = skip_ws_and_comment(s, len, from);
        if (from < len && (s[from] == '"' || s[from] == '\'')) {
            const char *value = NULL;
            from = parse_string(arena, s, len, from, &value);
            if (value && *count < capacity)
                out[(*count)++] = value;
        } else if (from < len && s[from] != ']' && s[from] != ',') {
            *count = 0;
            return skip_value(s, len, from);
        }
        from = skip_ws_and_comment(s, len, from);
        if (from < len && s[from] == ',')
            from++;
        from = skip_ws_and_comment(s, len, from);
    }
    *values = out;
    return from < len ? from + 1 : from;
}

static int parse_dep_field(CBMArena *a, const char *s, int len, int from, const char *field,
                           CBMCargoDep *dep) {
    if (dep && field && strcmp(field, "path") == 0) return parse_string(a, s, len, from, &dep->path);
    if (dep && field && strcmp(field, "optional") == 0) {
        int end = parse_bool(s, len, from, &dep->optional);
        if (end > from)
            return end;
    }
    if (dep && field && strcmp(field, "features") == 0)
        return parse_string_array(a, s, len, from, &dep->features, &dep->feature_count);
    if (dep && field && strcmp(field, "workspace") == 0) {
        bool inherited = false;
        int end = parse_bool(s, len, from, &inherited);
        if (end > from) {
            dep->workspace = inherited;
            return end;
        }
    }
    return skip_value(s, len, from);
}

/* For the `[dependencies]` / `[dev-dependencies]` / `[workspace.dependencies]`
 * sections, parse `key = value` lines until the next section. The value
 * may be a string (the version) or an inline table. We capture both
 * shapes — only the key (crate name), its `path = "..."` and
 * `workspace = true` matter for us. */
static int parse_dep_entry(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out, bool workspace_table) {
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] == '[') return from;
    const char* key = NULL;
    from = cargo_key_path(a, s, len, from, &key);
    from = skip_ws_and_comment(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_ws_and_comment(s, len, from);
    }
    const char *field = key ? strchr(key, '\x1f') : NULL;
    if (field) {
        const char *name = cbm_arena_strndup(a, key, (size_t)(field - key));
        CBMCargoDep *dep = NULL;
        /* A root's own dependency and its [workspace.dependencies] entry of
         * the same name are separate records. */
        for (int i = 0; i < out->dep_count; i++)
            if (strcmp(out->deps[i].name, name) == 0 &&
                out->deps[i].workspace_table == workspace_table)
                dep = &out->deps[i];
        if (!dep && out->dep_count < CBM_CARGO_MAX_DEPS) {
            dep = &out->deps[out->dep_count++];
            dep->name = name;
            dep->workspace_table = workspace_table;
        }
        return parse_dep_field(a, s, len, from, field + 1, dep);
    }
    CBMCargoDep fields = {0};
    if (from < len && s[from] == '{') {
        /* Inline table — scan for `path = "..."` and `workspace = true`. */
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
            from = parse_dep_field(a, s, len, from, sub_key, &fields);
        }
    } else {
        from = skip_value(s, len, from);
    }
    if (key && out->dep_count < CBM_CARGO_MAX_DEPS) {
        fields.name = key;
        fields.workspace_table = workspace_table;
        out->deps[out->dep_count++] = fields;
    }
    return from;
}

static int cargo_package_value(CBMArena *arena, const char *s, int len, int from,
                                const char *key, CBMCargoManifest *out) {
    const char *string = NULL;
    int end = parse_string(arena, s, len, from, &string);
    bool flag = false;
    int boolean_end = parse_bool(s, len, from, &flag);
    bool boolean = boolean_end > from;
    if (strncmp(key, "package\x1f" "edition\x1f", 16) == 0)
        out->edition_unreadable = true;
    if (strcmp(key, "package\x1f" "name") == 0) out->package_name = string;
    else if (strcmp(key, "package\x1f" "version") == 0) out->package_version = string;
    else if (strcmp(key, "package\x1f" "edition") == 0) {
        out->edition = string;
        out->edition_unreadable = !string;
    } else if (strcmp(key, "package\x1f" "edition\x1f" "workspace") == 0) {
        out->edition_inherited = boolean && flag;
        out->edition_unreadable = !out->edition_inherited;
    } else if (strcmp(key, "package\x1f" "version\x1f" "workspace") == 0)
        out->version_inherited = boolean && flag;
    else if (strcmp(key, "workspace\x1f" "package\x1f" "edition") == 0)
        out->workspace_edition = string;
    else if (strcmp(key, "workspace\x1f" "package\x1f" "version") == 0)
        out->workspace_version = string;
    else {
        static const char *const keys[] = {"autolib", "autobins", "autotests", "autoexamples", "autobenches"};
        for (int i = 0; i < 5; i++) {
            if (boolean && strncmp(key, "package\x1f", 8) == 0 && strcmp(key + 8, keys[i]) == 0) {
                out->auto_targets[i] = flag;
                out->auto_target_overrides |= 1u << i;
            }
        }
    }
    return end > from ? end : skip_value(s, len, from);
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

/* Package paths are a bounded Cargo schema, not a general TOML object model. */
static int cargo_package_entry(CBMArena *arena, const char *s, int len, int from,
                                const char *prefix, CBMCargoManifest *out, int depth) {
    const char *key = NULL;
    int start = from;
    from = cargo_key_path(arena, s, len, from, &key);
    if (!key || from >= len || s[from] != '=') {
        out->edition_unreadable = true;
        return skip_value(s, len, start);
    }
    const char *path = cbm_arena_sprintf(arena, "%s%s%s", prefix, *prefix ? "\x1f" : "", key);
    if (strcmp(path, "workspace") == 0) out->is_workspace_root = true;
    if (strcmp(path, "workspace\x1f" "members") == 0)
        return parse_workspace_kv(arena, s, len, start, out);
    from = skip_ws_and_comment(s, len, from + 1);
    if (from >= len || s[from] != '{') return cargo_package_value(arena, s, len, from, path, out);
    if (strcmp(path, "package\x1f" "edition") == 0) out->edition_unreadable = true;
    if (depth >= 4) return skip_value(s, len, from);
    from++;
    while (from < len) {
        from = skip_ws_and_comment(s, len, from);
        if (from >= len) break;
        if (s[from] == '}') return from + 1;
        if (s[from] == ',') { from++; continue; }
        int next = cargo_package_entry(arena, s, len, from, path, out, depth + 1);
        if (next <= from) break;
        from = next;
    }
    out->edition_unreadable = true;
    return len;
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

/* `[dependencies]`, `[dev-dependencies]`, `[build-dependencies]`, their
 * `[target.<cfg>.…]` forms, and `[workspace.dependencies]`. */
static bool cargo_is_dependency_section(const char *section) {
    static const char *const kinds[] = {"dependencies", "dev-dependencies", "build-dependencies"};
    const char *tail = section;
    if (strncmp(section, "target\x1f", 7) == 0) {
        tail = strrchr(section, '\x1f');
        tail = tail ? tail + 1 : section;
    }
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (strcmp(tail, kinds[i]) == 0) {
            return true;
        }
    }
    return strcmp(section, CARGO_WORKSPACE_DEPS) == 0;
}

/* True for a `[build-dependencies]` section, target-specific ones included. */
static bool cargo_is_build_dependency_section(const char *section) {
    const char *tail = strrchr(section, '\x1f');
    return strcmp(tail ? tail + 1 : section, "build-dependencies") == 0;
}

/* The last component of a dependency table is the declared crate alias,
 * rather than an entry key. Its parent uses the same section rules as an
 * inline dependency, including target and workspace sections. */
static const char *cargo_dependency_table_name(CBMArena *arena, const char *section) {
    const char *separator = strrchr(section, '\x1f');
    if (!separator || !separator[1]) return NULL;
    const char *parent = cbm_arena_strndup(arena, section, (size_t)(separator - section));
    return cargo_is_dependency_section(parent) ? separator + 1 : NULL;
}

static int parse_dependency_table_field(CBMArena *arena, const char *source, int length,
                                         int from, CBMCargoDep *dep) {
    const char *key = NULL;
    from = parse_key(arena, source, length, from, &key);
    from = skip_ws_and_comment(source, length, from);
    if (from < length && source[from] == '=') from++;
    from = skip_ws_and_comment(source, length, from);
    return parse_dep_field(arena, source, length, from, key, dep);
}

/* One `[features]` line, `name = ["a", "b/c", "dep:d"]`. A value that is
 * not an array of strings is skipped and records nothing. */
static int parse_feature_entry(CBMArena *arena, const char *s, int len, int from,
                               CBMCargoManifest *out) {
    const char *name = NULL;
    from = parse_key(arena, s, len, from, &name);
    from = skip_ws_and_comment(s, len, from);
    if (from >= len || s[from] != '=')
        return from;
    from = skip_ws_and_comment(s, len, from + 1);
    if (!name || from >= len || s[from] != '[')
        return skip_value(s, len, from);
    if (out->feature_count == out->feature_capacity) {
        int capacity = out->feature_capacity ? out->feature_capacity * 2 : 16;
        CBMCargoFeature *grown =
            (CBMCargoFeature *)cbm_arena_alloc(arena, (size_t)capacity * sizeof(*grown));
        if (!grown)
            return skip_value(s, len, from);
        if (out->feature_count)
            memcpy(grown, out->features, (size_t)out->feature_count * sizeof(*grown));
        out->features = grown;
        out->feature_capacity = capacity;
    }
    int value_count = 0;
    for (int at = from + 1; at < len && s[at] != ']'; at++)
        value_count += s[at] == '"' || s[at] == '\'' ? 1 : 0;
    const char **values =
        (const char **)cbm_arena_alloc(arena, (size_t)(value_count + 1) * sizeof(*values));
    if (!values)
        return skip_value(s, len, from);
    CBMCargoFeature *feature = &out->features[out->feature_count++];
    *feature = (CBMCargoFeature){.name = name, .values = values};
    from++;
    while (from < len && s[from] != ']') {
        from = skip_ws_and_comment(s, len, from);
        if (from < len && (s[from] == '"' || s[from] == '\'')) {
            const char *value = NULL;
            from = parse_string(arena, s, len, from, &value);
            if (value && feature->value_count < value_count)
                values[feature->value_count++] = value;
        } else if (from < len && s[from] != ']' && s[from] != ',') {
            return skip_value(s, len, from);
        }
        from = skip_ws_and_comment(s, len, from);
        if (from < len && s[from] == ',')
            from++;
        from = skip_ws_and_comment(s, len, from);
    }
    return from < len ? from + 1 : from;
}

void cbm_cargo_parse(CBMArena *arena, const char *src, int src_len, CBMCargoManifest *out) {
    if (!arena || !src || !out) return;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 5; i++) out->auto_targets[i] = true;
    if (src_len <= 0) src_len = (int)strlen(src);
    int from = 0;
    const char *section = "";
    const char *package_prefix = "";
    CBMCargoTarget *target = NULL;
    CBMCargoDep *dependency_table = NULL;
    while (from < src_len) {
        from = skip_ws_and_comment(src, src_len, from);
        if (from >= src_len) break;
        if (src[from] == '[') {
            bool array = from + 1 < src_len && src[from + 1] == '[';
            from += array ? 2 : 1;
            const char *path = NULL;
            from = cargo_key_path(arena, src, src_len, from, &path);
            if (!path || from >= src_len || src[from] != ']') {
                out->edition_unreadable = true;
                break;
            }
            from++;
            if (array && from < src_len && src[from] == ']') from++;
            package_prefix = path;
            section = path;
            target = NULL;
            dependency_table = NULL;
            const char *dependency_name = cargo_dependency_table_name(arena, section);
            if (dependency_name && out->dep_count < CBM_CARGO_MAX_DEPS) {
                dependency_table = &out->deps[out->dep_count++];
                dependency_table->name = dependency_name;
                dependency_table->workspace_table =
                    strncmp(section, CARGO_WORKSPACE_DEPS "\x1f", sizeof(CARGO_WORKSPACE_DEPS)) == 0;
                dependency_table->build = cargo_is_build_dependency_section(
                    cbm_arena_strndup(arena, section, (size_t)(strrchr(section, '\x1f') - section)));
            }
            static const char *const kinds[] = {"lib", "bin", "test", "example", "bench"};
            for (int t = 0; t < 5; t++)
                if (strcmp(section, kinds[t]) == 0) target = append_target(arena, out, (CBMCargoTargetKind)t);
            if (strcmp(section, "workspace") == 0) out->is_workspace_root = true;
            if (strcmp(path, "package\x1f" "edition") == 0 ||
                strncmp(path, "package\x1f" "edition\x1f", 16) == 0)
                out->edition_unreadable = true;
            continue;
        }
        int start = from;
        if (!*section || strcmp(section, "package") == 0 || strncmp(package_prefix, "package\x1f", 8) == 0 ||
            strcmp(section, "workspace\x1f" "package") == 0) {
            from = cargo_package_entry(arena, src, src_len, from, package_prefix, out, 0);
        } else if (target) from = parse_target_kv(arena, src, src_len, from, out, target);
        else if (strcmp(section, "workspace") == 0) {
            /* workspace.package may itself be dotted or an inline table. */
            const char *key = NULL;
            cargo_key_path(arena, src, src_len, from, &key);
            if (key && (strcmp(key, "package") == 0 || strncmp(key, "package\x1f", 8) == 0))
                from = cargo_package_entry(arena, src, src_len, from, package_prefix, out, 0);
            else from = parse_workspace_kv(arena, src, src_len, from, out);
        } else if (dependency_table) {
            from = parse_dependency_table_field(arena, src, src_len, from, dependency_table);
        } else if (cargo_is_dependency_section(section)) {
            int first_new = out->dep_count;
            from = parse_dep_entry(arena, src, src_len, from, out,
                                   strcmp(section, CARGO_WORKSPACE_DEPS) == 0);
            for (int d = first_new; d < out->dep_count; d++)
                out->deps[d].build = cargo_is_build_dependency_section(section);
        }
        else if (strcmp(section, "features") == 0) from = parse_feature_entry(arena, src, src_len, from, out);
        else {
            /* Every other table is skipped, `[patch.<source>]` included: a patch
             * replaces only a dependency from that source whose requirement its
             * version satisfies, under its `package` name, and only from the
             * root manifest. None of that is read here, so a dependency a patch
             * may replace stays external. */
            const char *key = NULL;
            from = cargo_key_path(arena, src, src_len, from, &key);
            if (from < src_len && src[from] == '=') from = skip_value(src, src_len, from + 1);
        }
        if (from <= start) {
            out->edition_unreadable = true;
            from = start + 1;
        }
    }
}

const char *cbm_cargo_effective_edition(const CBMCargoManifest *manifest,
                                        const char *inherited_edition) {
    if (!manifest || manifest->edition_unreadable)
        return NULL;
    const char *edition = manifest->edition_inherited ? inherited_edition : manifest->edition;
    if (!edition && !manifest->edition_inherited)
        return "2015";
    if (edition && (strcmp(edition, "2015") == 0 || strcmp(edition, "2018") == 0 ||
                    strcmp(edition, "2021") == 0 || strcmp(edition, "2024") == 0))
        return edition;
    return NULL;
}

static bool target_auto_enabled(const CBMCargoManifest *manifest, CBMCargoTargetKind kind,
                                const char *inherited_edition) {
    if (manifest->auto_target_overrides & (1u << kind))
        return manifest->auto_targets[kind];
    const char *edition = cbm_cargo_effective_edition(manifest, inherited_edition);
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
