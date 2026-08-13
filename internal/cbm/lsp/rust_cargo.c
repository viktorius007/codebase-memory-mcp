/* rust_cargo.c — Hand-rolled TOML subset parser for Cargo.toml.
 *
 * Recognises only the shapes we need (per RUST_LSP_FOLLOWUP §A3):
 *   - ordinary and array table headers, including target dependency tables
 *   - bare, quoted, and dotted keys
 *   - basic/literal strings (single-line or multiline), arrays, inline tables,
 *     and bare scalar values
 *   - `members = ["a", "b/c"]`
 *
 * Everything else (numbers, dates, deep tables, comments past EOL) is
 * skipped without error.
 */

#include "rust_cargo.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

bool cbm_cargo_add_target(CBMArena *arena, CBMCargoManifest *manifest, CBMCargoTargetKind kind,
                          const char *source_path) {
    return cbm_cargo_add_named_target(arena, manifest, kind, NULL, source_path);
}

bool cbm_cargo_add_named_target(CBMArena *arena, CBMCargoManifest *manifest,
                                CBMCargoTargetKind kind, const char *name,
                                const char *source_path) {
    return cbm_cargo_add_routed_target(arena, manifest, kind, name, NULL, source_path, NULL);
}

bool cbm_cargo_add_routed_target(CBMArena *arena, CBMCargoManifest *manifest,
                                 CBMCargoTargetKind kind, const char *name, const char *package_dir,
                                 const char *source_path, const char *blocker_root) {
    if (!arena || !manifest || ((!source_path || !source_path[0]) && (!name || !name[0])) ||
        (kind < CBM_CARGO_TARGET_LIB || kind > CBM_CARGO_TARGET_BUILD)) {
        if (manifest)
            manifest->targets_complete = false;
        return false;
    }
    if (manifest->target_count == manifest->target_cap) {
        int next_cap = manifest->target_cap > 0 ? manifest->target_cap * 2 : 4;
        CBMCargoTarget *next = cbm_arena_alloc(arena, (size_t)next_cap * sizeof(CBMCargoTarget));
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
    if ((source_path && !owned_path) || (name && !owned_name) || (package_dir && !owned_package) ||
        (blocker_root && !owned_blocker)) {
        manifest->targets_complete = false;
        return false;
    }
    manifest->targets[manifest->target_count++] = (CBMCargoTarget){.kind = kind,
                                                                   .name = owned_name,
                                                                   .package_dir = owned_package,
                                                                   .blocker_root = owned_blocker,
                                                                   .source_path = owned_path};
    return true;
}

/* ── Tiny tokenizer ──────────────────────────────────────────── */

static void cargo_record(CBMCargoManifest *out, CBMRustHealthReason reason, int start, int end) {
    if (!out)
        return;
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

static bool is_horizontal_ws(char c) {
    return c == ' ' || c == '\t';
}

static int skip_horizontal_ws(const char *s, int len, int from) {
    while (from < len && is_horizontal_ws(s[from]))
        from++;
    return from;
}

static int toml_hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool append_utf8(char *dst, int *used, unsigned long cp) {
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return false;
    if (cp <= 0x7f) {
        dst[(*used)++] = (char)cp;
    } else if (cp <= 0x7ff) {
        dst[(*used)++] = (char)(0xc0 | (cp >> 6));
        dst[(*used)++] = (char)(0x80 | (cp & 0x3f));
    } else if (cp <= 0xffff) {
        dst[(*used)++] = (char)(0xe0 | (cp >> 12));
        dst[(*used)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        dst[(*used)++] = (char)(0x80 | (cp & 0x3f));
    } else {
        dst[(*used)++] = (char)(0xf0 | (cp >> 18));
        dst[(*used)++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        dst[(*used)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        dst[(*used)++] = (char)(0x80 | (cp & 0x3f));
    }
    return true;
}

/* Return the byte immediately after one TOML string. Both basic/literal and
 * multiline basic/literal strings are recognized. A single-line string may
 * not cross a newline. */
static int skip_toml_string(const char *s, int len, int from, bool *complete, int *content_start,
                            int *content_end) {
    *complete = false;
    if (from >= len || (s[from] != '"' && s[from] != '\''))
        return from;
    char quote = s[from];
    bool multiline = from + 2 < len && s[from + 1] == quote && s[from + 2] == quote;
    int width = multiline ? 3 : 1;
    int pos = from + width;
    if (content_start)
        *content_start = pos;
    while (pos < len) {
        if (multiline && pos + 2 < len && s[pos] == quote && s[pos + 1] == quote &&
            s[pos + 2] == quote) {
            if (content_end)
                *content_end = pos;
            *complete = true;
            return pos + 3;
        }
        if (!multiline && s[pos] == quote) {
            if (content_end)
                *content_end = pos;
            *complete = true;
            return pos + 1;
        }
        if (!multiline && (s[pos] == '\n' || s[pos] == '\r'))
            return pos;
        if (quote == '"' && s[pos] == '\\' && pos + 1 < len) {
            pos += 2;
        } else {
            pos++;
        }
    }
    return len;
}

/* Decode a syntactically complete TOML string. The decoded byte count cannot
 * exceed the source spelling, so one source-sized arena allocation suffices. */
static const char *decode_toml_string(CBMArena *a, const char *s, int from, int next, bool *valid) {
    *valid = false;
    char quote = s[from];
    bool multiline = from + 2 < next && s[from + 1] == quote && s[from + 2] == quote;
    int width = multiline ? 3 : 1;
    int pos = from + width;
    int end = next - width;
    char *decoded = cbm_arena_alloc(a, (size_t)(next - from + 1));
    if (!decoded)
        return NULL;
    int used = 0;
    if (multiline && pos < end && s[pos] == '\r')
        pos++;
    if (multiline && pos < end && s[pos] == '\n')
        pos++;
    while (pos < end) {
        if (quote == '\'' || s[pos] != '\\') {
            decoded[used++] = s[pos++];
            continue;
        }
        pos++;
        if (multiline && pos < end && (s[pos] == '\n' || s[pos] == '\r')) {
            if (s[pos] == '\r')
                pos++;
            if (pos < end && s[pos] == '\n')
                pos++;
            while (pos < end && (is_horizontal_ws(s[pos]) || s[pos] == '\n' || s[pos] == '\r'))
                pos++;
            continue;
        }
        if (pos >= end)
            return NULL;
        char escape = s[pos++];
        switch (escape) {
        case 'b':
            decoded[used++] = '\b';
            break;
        case 't':
            decoded[used++] = '\t';
            break;
        case 'n':
            decoded[used++] = '\n';
            break;
        case 'f':
            decoded[used++] = '\f';
            break;
        case 'r':
            decoded[used++] = '\r';
            break;
        case 'e':
            decoded[used++] = '\x1b';
            break;
        case '"':
            decoded[used++] = '"';
            break;
        case '\\':
            decoded[used++] = '\\';
            break;
        case 'u':
        case 'U': {
            int digits = escape == 'u' ? 4 : 8;
            if (pos + digits > end)
                return NULL;
            unsigned long cp = 0;
            for (int i = 0; i < digits; i++) {
                int nibble = toml_hex(s[pos + i]);
                if (nibble < 0)
                    return NULL;
                cp = (cp << 4) | (unsigned long)nibble;
            }
            pos += digits;
            if (!append_utf8(decoded, &used, cp))
                return NULL;
            break;
        }
        case 'x': {
            if (pos + 2 > end)
                return NULL;
            int high = toml_hex(s[pos]);
            int low = toml_hex(s[pos + 1]);
            if (high < 0 || low < 0)
                return NULL;
            if (!append_utf8(decoded, &used, (unsigned long)((high << 4) | low)))
                return NULL;
            pos += 2;
            break;
        }
        default:
            return NULL;
        }
    }
    decoded[used] = '\0';
    *valid = true;
    return decoded;
}

static int parse_key_segment(CBMArena *a, const char *s, int len, int from, const char **out) {
    int start = from;
    *out = NULL;
    if (from < len && (s[from] == '"' || s[from] == '\'')) {
        bool complete = false;
        int next = skip_toml_string(s, len, from, &complete, NULL, NULL);
        if (!complete || (next - from >= 6 && s[from + 1] == s[from]))
            return start;
        bool valid = false;
        *out = decode_toml_string(a, s, from, next, &valid);
        return valid ? next : start;
    }
    while (from < len && is_ident_char(s[from])) from++;
    if (from > start)
        *out = cbm_arena_strndup(a, s + start, (size_t)(from - start));
    return from;
}

/* Section routing uses dots as segment separators. Percent-encode dots and
 * percent signs inside a quoted segment so distinct TOML keys stay distinct. */
static void append_section_segment(char *section, int *section_len, const char *segment) {
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)segment; *p; p++) {
        if (*p == '.' || *p == '%') {
            section[(*section_len)++] = '%';
            section[(*section_len)++] = hex[*p >> 4];
            section[(*section_len)++] = hex[*p & 0x0f];
        } else {
            section[(*section_len)++] = (char)*p;
        }
    }
}

static int find_section_close(const char *s, int len, int from) {
    while (from < len && s[from] != '\n' && s[from] != '\r') {
        if (s[from] == '"' || s[from] == '\'') {
            bool complete = false;
            int next = skip_toml_string(s, len, from, &complete, NULL, NULL);
            if (!complete)
                return from;
            from = next;
            continue;
        }
        if (s[from] == ']')
            return from;
        from++;
    }
    return from;
}

/* Parse a TOML key, including quoted and dotted keys. The first segment is
 * returned because Cargo uses `dependency.workspace = true` as the dotted
 * spelling of a dependency declaration; the whole dotted key is consumed. */
static int parse_key(CBMArena *a, const char *s, int len, int from, const char **out) {
    const char *first = NULL;
    bool first_segment = true;
    while (from < len) {
        int start = from;
        const char *segment = NULL;
        from = parse_key_segment(a, s, len, from, &segment);
        if (from == start)
            return start;
        if (first_segment)
            first = segment;
        int after_segment = from;
        while (from < len && is_horizontal_ws(s[from]))
            from++;
        if (from >= len || s[from] != '.') {
            from = after_segment;
            break;
        }
        from++;
        while (from < len && is_horizontal_ws(s[from]))
            from++;
        first_segment = false;
    }
    *out = first;
    return from;
}

/* Parse a string literal (single or double quoted). */
static int parse_string(CBMArena *a, const char *s, int len, int from, const char **out,
                        CBMCargoManifest *manifest) {
    if (from >= len) return from;
    int literal_start = from;
    bool complete = false;
    int content_start = from;
    int content_end = from;
    int next = skip_toml_string(s, len, from, &complete, &content_start, &content_end);
    if (next == from)
        return from;
    if (complete) {
        bool valid = false;
        *out = decode_toml_string(a, s, from, next, &valid);
        if (!valid && cbm_arena_status(a) == CBM_ARENA_STATUS_AVAILABLE) {
            cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, literal_start, next);
        }
    } else {
        cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, literal_start, next);
    }
    return next;
}

/* Skip a value (used for keys we don't care about). Handles strings,
 * arrays, inline tables, bare values. */
static int skip_value(const char *s, int len, int from, CBMCargoManifest *out) {
    from = skip_horizontal_ws(s, len, from);
    if (from >= len) return from;
    int value_start = from;
    char c = s[from];
    if (c == '"' || c == '\'') {
        bool complete = false;
        int next = skip_toml_string(s, len, from, &complete, NULL, NULL);
        if (!complete) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start, next);
        }
        return next;
    }
    if (c == '[' || c == '{') {
        char stack[64];
        int depth = 1;
        stack[0] = c;
        from++;
        while (from < len && depth > 0) {
            char d = s[from];
            if (d == '"' || d == '\'') {
                bool complete = false;
                int next = skip_toml_string(s, len, from, &complete, NULL, NULL);
                if (!complete) {
                    cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, from, next);
                    return next;
                }
                from = next;
                continue;
            }
            if (d == '#') {
                while (from < len && s[from] != '\n')
                    from++;
                continue;
            }
            if (d == '[' || d == '{') {
                if (depth == (int)sizeof(stack)) {
                    cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start,
                                 from + 1);
                    return from + 1;
                }
                stack[depth++] = d;
                from++;
                continue;
            }
            if (d == ']' || d == '}') {
                char expected = stack[depth - 1] == '[' ? ']' : '}';
                if (d != expected) {
                    cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, from, from + 1);
                    return from + 1;
                }
                depth--;
            }
            from++;
        }
        if (depth > 0) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start, len);
        }
        return from;
    }
    /* A bare TOML scalar ends at the surrounding container delimiter as well
     * as EOL. This distinction matters for `optional = true }`. */
    while (from < len && s[from] != '\n' && s[from] != '#' && s[from] != ',' && s[from] != ']' &&
           s[from] != '}')
        from++;
    return from;
}

/* Parse `[section.path]` header — returns the section name as a flat
 * dotted string, e.g. "dependencies" or "workspace.dependencies". */
static int parse_section(CBMArena *a, const char *s, int len, int from, const char **out,
                         CBMCargoManifest *manifest) {
    if (from >= len || s[from] != '[') return from;
    int section_start = from;
    /* Skip leading `[` or `[[`. */
    bool array_of_tables = false;
    from++;
    if (from < len && s[from] == '[') { array_of_tables = true; from++; }
    int header_end = find_section_close(s, len, from);
    size_t header_bytes = (size_t)(header_end - from);
    char *section = cbm_arena_alloc(a, header_bytes * 3 + 1);
    int section_len = 0;
    bool valid = section != NULL;
    bool need_segment = true;
    while (from < len && s[from] != ']' && s[from] != '\n' && s[from] != '\r') {
        while (from < len && is_horizontal_ws(s[from]))
            from++;
        if (from >= len || s[from] == ']' || s[from] == '\n' || s[from] == '\r')
            break;
        if (!need_segment || !valid) {
            valid = false;
            break;
        }
        const char *segment = NULL;
        int next = parse_key_segment(a, s, len, from, &segment);
        if (next == from || !segment) {
            valid = false;
            break;
        }
        if (section_len > 0)
            section[section_len++] = '.';
        append_section_segment(section, &section_len, segment);
        from = next;
        while (from < len && is_horizontal_ws(s[from]))
            from++;
        need_segment = false;
        if (from < len && s[from] == '.') {
            from++;
            need_segment = true;
        }
    }
    if (valid && !need_segment) {
        section[section_len] = '\0';
        *out = section;
    } else {
        *out = NULL;
        if (cbm_arena_status(a) == CBM_ARENA_STATUS_AVAILABLE) {
            cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, section_start,
                         from < len ? from + 1 : len);
        }
    }
    /* Consume closing `]` (or `]]`). */
    if (from < len) {
        if (s[from] == ']') {
            from++;
        } else {
            cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, section_start, from);
        }
    } else {
        cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, section_start, len);
    }
    if (array_of_tables) {
        if (from < len && s[from] == ']') {
            from++;
        } else {
            cargo_record(manifest, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, section_start, from);
        }
    }
    return from;
}

/* For the `[dependencies]` / `[dev-dependencies]` / `[workspace.dependencies]`
 * sections, parse `key = value` lines until the next section. The value
 * may be a string (the version) or an inline table. We capture both
 * shapes — only the key (crate name) and optional `path = "..."` field
 * matter for us. */
static CBMCargoDep *add_dependency(CBMCargoManifest *out, const char *name, const char *path,
                                   int start, int end) {
    if (!name)
        return NULL;
    if (out->dep_count >= CBM_CARGO_MAX_DEPS) {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_DEP_LIMIT, start, end);
        return NULL;
    }
    CBMCargoDep *dep = &out->deps[out->dep_count++];
    dep->name = name;
    dep->path = path;
    return dep;
}

static int parse_dep_entry(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out) {
    from = skip_horizontal_ws(s, len, from);
    if (from >= len || s[from] == '[') return from;
    int entry_start = from;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_horizontal_ws(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_horizontal_ws(s, len, from);
    } else {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, entry_start, from);
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
            from = skip_horizontal_ws(s, len, from);
            if (from < len && s[from] == '=') {
                from++;
                from = skip_horizontal_ws(s, len, from);
            } else {
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, field_start, from);
                from = skip_value(s, len, from, out);
                continue;
            }
            if (sub_key && strcmp(sub_key, "path") == 0) {
                from = parse_string(a, s, len, from, &path_val, out);
            } else {
                from = skip_value(s, len, from, out);
            }
            if (from <= field_start) {
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, field_start,
                             field_start + 1);
                from = field_start + 1;
            }
        }
        if (depth > 0) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, entry_start, len);
        }
    } else {
        from = skip_value(s, len, from, out);
    }
    add_dependency(out, key, path_val, entry_start, from);
    return from;
}

static int parse_dep_table_kv(CBMArena *a, const char *s, int len, int from, CBMCargoManifest *out,
                              CBMCargoDep *dep) {
    from = skip_horizontal_ws(s, len, from);
    if (from >= len || s[from] == '[')
        return from;
    int entry_start = from;
    const char *key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_horizontal_ws(s, len, from);
    if (from >= len || s[from] != '=') {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, entry_start, from);
        return skip_value(s, len, from, out);
    }
    from = skip_horizontal_ws(s, len, from + 1);
    if (key && strcmp(key, "path") == 0) {
        const char *path = NULL;
        int value_start = from;
        from = parse_string(a, s, len, from, &path, out);
        if (from == value_start) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start, value_start + 1);
            from = skip_value(s, len, from, out);
        } else if (dep && path) {
            dep->path = path;
        }
    } else {
        from = skip_value(s, len, from, out);
    }
    return from;
}

/* ── Section dispatcher ──────────────────────────────────────── */

static int parse_package_kv(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out) {
    from = skip_horizontal_ws(s, len, from);
    if (from >= len || s[from] == '[') return from;
    int entry_start = from;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_horizontal_ws(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_horizontal_ws(s, len, from);
    } else {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, entry_start, from);
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
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start,
                             value_start + 1);
                from = skip_value(s, len, from, out);
            } else {
                out->auto_build = false;
            }
        }
    } else if (key && strcmp(key, "name") == 0) {
        int value_start = from;
        from = parse_string(a, s, len, from, &out->package_name, out);
        if (from == value_start) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start, value_start + 1);
            from = skip_value(s, len, from, out);
        }
    } else if (key && strcmp(key, "version") == 0) {
        int value_start = from;
        from = parse_string(a, s, len, from, &out->package_version, out);
        if (from == value_start) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start, value_start + 1);
            from = skip_value(s, len, from, out);
        }
    } else if (key && strcmp(key, "version") != 0) {
        from = skip_value(s, len, from, out);
    }
    return from;
}

static int parse_target_kv(CBMArena *a, const char *s, int len, int from, CBMCargoManifest *out,
                           const char **target_name, const char **target_path) {
    from = skip_horizontal_ws(s, len, from);
    if (from >= len || s[from] == '[')
        return from;
    int entry_start = from;
    const char *key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_horizontal_ws(s, len, from);
    if (from >= len || s[from] != '=') {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, entry_start, from);
        return skip_value(s, len, from, out);
    }
    from = skip_horizontal_ws(s, len, from + 1);
    if (key && (strcmp(key, "path") == 0 || strcmp(key, "name") == 0)) {
        const char **dst = strcmp(key, "path") == 0 ? target_path : target_name;
        int value_start = from;
        from = parse_string(a, s, len, from, dst, out);
        if (from == value_start || !*dst) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, value_start,
                         from > value_start ? from : value_start + 1);
            if (from == value_start)
                from = skip_value(s, len, from, out);
        }
    } else {
        from = skip_value(s, len, from, out);
    }
    return from;
}

static int parse_workspace_kv(CBMArena* a, const char* s, int len, int from,
    CBMCargoManifest* out) {
    from = skip_horizontal_ws(s, len, from);
    if (from >= len || s[from] == '[') return from;
    int entry_start = from;
    out->is_workspace_root = true;
    const char* key = NULL;
    from = parse_key(a, s, len, from, &key);
    from = skip_horizontal_ws(s, len, from);
    if (from < len && s[from] == '=') {
        from++;
        from = skip_horizontal_ws(s, len, from);
    } else {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, entry_start, from);
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
                        const char *last = mem;
                        for (const char *p = mem; *p; p++) {
                            if (*p == '/')
                                last = p + 1;
                        }
                        items[*count].member_name = last;
                        items[*count].member_path = mem;
                        (*count)++;
                    } else {
                        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_MEMBER_LIMIT, member_start,
                                     from);
                    }
                }
            } else if (from < len && s[from] != ']') {
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, from, from + 1);
                while (from < len && s[from] != ',' && s[from] != ']')
                    from++;
            }
            from = skip_ws_and_comment(s, len, from);
            if (from < len && s[from] == ',') from++;
            from = skip_ws_and_comment(s, len, from);
            if (from <= member_item_start && from < len) {
                from = member_item_start + 1;
            }
        }
        if (from < len) {
            from++; /* consume `]` */
        } else {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, entry_start, len);
        }
    } else {
        from = skip_value(s, len, from, out);
    }
    return from;
}

static bool has_suffix(const char *value, const char *suffix) {
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len && strcmp(value + value_len - suffix_len, suffix) == 0;
}

static bool is_dependency_section(const char *section) {
    if (strcmp(section, "dependencies") == 0 || strcmp(section, "dev-dependencies") == 0 ||
        strcmp(section, "build-dependencies") == 0 ||
        strcmp(section, "workspace.dependencies") == 0) {
        return true;
    }
    if (strncmp(section, "target.", 7) != 0)
        return false;
    return has_suffix(section, ".dependencies") || has_suffix(section, ".dev-dependencies") ||
           has_suffix(section, ".build-dependencies");
}

static const char *dependency_table_key_start(const char *section) {
    static const char *prefixes[] = {
        "dependencies.",
        "dev-dependencies.",
        "build-dependencies.",
        "workspace.dependencies.",
    };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t prefix_len = strlen(prefixes[i]);
        if (strncmp(section, prefixes[i], prefix_len) == 0 && section[prefix_len]) {
            return section + prefix_len;
        }
    }
    if (strncmp(section, "target.", 7) != 0)
        return NULL;
    static const char *markers[] = {
        ".dependencies.",
        ".dev-dependencies.",
        ".build-dependencies.",
    };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        const char *marker = strstr(section, markers[i]);
        if (marker) {
            const char *key = marker + strlen(markers[i]);
            if (*key)
                return key;
        }
    }
    return NULL;
}

void cbm_cargo_parse(CBMArena* arena, const char* src, int src_len,
    CBMCargoManifest* out) {
    if (!out)
        return;
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
    const char *target_name = NULL;
    const char *target_path = NULL;
    CBMCargoDep *dependency_table = NULL;

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
            dependency_table = NULL;
            const char* hdr = NULL;
            from = parse_section(arena, src, src_len, from, &hdr, out);
            section = hdr ? hdr : "";
            const char *dependency_key = dependency_table_key_start(section);
            if (dependency_key) {
                const char *dependency_name = NULL;
                parse_key(arena, dependency_key, (int)strlen(dependency_key), 0, &dependency_name);
                dependency_table = add_dependency(out, dependency_name, NULL, item_start, from);
            }
            if (strcmp(section, "lib") == 0) {
                target_kind = CBM_CARGO_TARGET_LIB;
                out->has_lib_table = true;
            }
            if (strcmp(section, "bin") == 0)
                target_kind = CBM_CARGO_TARGET_BIN;
            if (strcmp(section, "example") == 0)
                target_kind = CBM_CARGO_TARGET_EXAMPLE;
            if (strcmp(section, "test") == 0)
                target_kind = CBM_CARGO_TARGET_TEST;
            if (strcmp(section, "bench") == 0)
                target_kind = CBM_CARGO_TARGET_BENCH;
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
        } else if (is_dependency_section(section)) {
            from = parse_dep_entry(arena, src, src_len, from, out);
        } else if (dependency_table) {
            from = parse_dep_table_kv(arena, src, src_len, from, out, dependency_table);
        } else {
            /* Preserve header synchronization while structurally scanning an
             * uninterpreted section; arrays may themselves begin with `[`. */
            const char *ignored_key = NULL;
            int key_start = from;
            from = parse_key(arena, src, src_len, from, &ignored_key);
            (void)ignored_key;
            from = skip_horizontal_ws(src, src_len, from);
            if (from < src_len && src[from] == '=') {
                from = skip_horizontal_ws(src, src_len, from + 1);
                from = skip_value(src, src_len, from, out);
            } else {
                cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, key_start, from);
            }
        }
        if (from <= item_start) {
            cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, item_start, item_start + 1);
            from = item_start + 1;
        }
    }
    if (target_kind != 0 && (target_name || target_path) &&
        !cbm_cargo_add_named_target(arena, out, target_kind, target_name, target_path)) {
        cargo_record(out, CBM_RUST_HEALTH_MANIFEST_PARSE_PARTIAL, src_len, src_len);
    }
    if (cbm_arena_status(arena) == CBM_ARENA_STATUS_ALLOCATION_UNAVAILABLE) {
        out->targets_complete = false;
        cargo_record(out, CBM_RUST_HEALTH_ALLOCATION_UNAVAILABLE, 0, src_len);
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
