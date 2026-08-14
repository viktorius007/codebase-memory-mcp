#include "pipeline/definition_properties.h"

#include "foundation/constants.h"
#include "simhash/minhash.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JSON_FIELD_OVERHEAD = 6, JSON_ESCAPE_WIDTH = 2 };

#ifdef CBM_ENABLE_TEST_SEAMS
static atomic_bool g_fail_allocation_once = false;

void cbm_def_properties_test_fail_allocation_once(void) {
    atomic_store(&g_fail_allocation_once, true);
}
#endif

static bool checked_add(size_t *total, size_t amount) {
    if (!total || amount > SIZE_MAX - *total) {
        return false;
    }
    *total += amount;
    return true;
}

static bool escaped_length(const char *value, size_t *out) {
    size_t length = 0;
    if (!value || !out) {
        return false;
    }
    for (const char *p = value; *p; p++) {
        size_t width = (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t')
                           ? JSON_ESCAPE_WIDTH
                           : SKIP_ONE;
        if (!checked_add(&length, width)) {
            return false;
        }
    }
    *out = length;
    return true;
}

static bool add_string_size(size_t *total, const char *key, const char *value) {
    if (!value || value[0] == '\0') {
        return true;
    }
    size_t escaped = 0;
    return escaped_length(value, &escaped) && checked_add(total, strlen(key)) &&
           checked_add(total, escaped) && checked_add(total, JSON_FIELD_OVERHEAD);
}

static bool add_array_size(size_t *total, const char *key, const char *const *values) {
    if (!values || !values[0]) {
        return true;
    }
    if (!checked_add(total, strlen(key)) || !checked_add(total, JSON_FIELD_OVERHEAD)) {
        return false;
    }
    for (int i = 0; values[i]; i++) {
        size_t escaped = 0;
        if (!escaped_length(values[i], &escaped) || !checked_add(total, escaped) ||
            !checked_add(total, JSON_ESCAPE_WIDTH) || (i > 0 && !checked_add(total, SKIP_ONE))) {
            return false;
        }
    }
    return true;
}

static char *append_escaped(char *dst, const char *value) {
    for (const char *p = value; *p; p++) {
        switch (*p) {
        case '"':
            *dst++ = '\\';
            *dst++ = '"';
            break;
        case '\\':
            *dst++ = '\\';
            *dst++ = '\\';
            break;
        case '\n':
            *dst++ = '\\';
            *dst++ = 'n';
            break;
        case '\r':
            *dst++ = '\\';
            *dst++ = 'r';
            break;
        case '\t':
            *dst++ = '\\';
            *dst++ = 't';
            break;
        default:
            *dst++ = (unsigned char)*p < 0x20 ? ' ' : *p;
            break;
        }
    }
    return dst;
}

static char *append_string(char *dst, const char *key, const char *value) {
    if (!value || value[0] == '\0') {
        return dst;
    }
    *dst++ = ',';
    *dst++ = '"';
    size_t key_length = strlen(key);
    memcpy(dst, key, key_length);
    dst += key_length;
    *dst++ = '"';
    *dst++ = ':';
    *dst++ = '"';
    dst = append_escaped(dst, value);
    *dst++ = '"';
    return dst;
}

static char *append_array(char *dst, const char *key, const char *const *values) {
    if (!values || !values[0]) {
        return dst;
    }
    *dst++ = ',';
    *dst++ = '"';
    size_t key_length = strlen(key);
    memcpy(dst, key, key_length);
    dst += key_length;
    *dst++ = '"';
    *dst++ = ':';
    *dst++ = '[';
    for (int i = 0; values[i]; i++) {
        if (i > 0) {
            *dst++ = ',';
        }
        *dst++ = '"';
        dst = append_escaped(dst, values[i]);
        *dst++ = '"';
    }
    *dst++ = ']';
    return dst;
}

cbm_def_properties_status_t cbm_def_properties_build(const CBMDefinition *def,
                                                     cbm_def_properties_t *out) {
    if (!def || !out) {
        return CBM_DEF_PROPERTIES_INVALID;
    }
    out->json = NULL;
    out->length = 0;

    const bool is_function =
        def->label && (strcmp(def->label, "Function") == 0 || strcmp(def->label, "Method") == 0);
    const char *function_format =
        "{\"complexity\":%d,\"cognitive\":%d,\"loop_count\":%d,\"loop_depth\":%d,"
        "\"self_recursive\":%s,\"param_count\":%d,\"max_access_depth\":%d,"
        "\"linear_scan_in_loop\":%d,\"alloc_in_loop\":%d,\"recursion_in_loop\":%s,"
        "\"unguarded_recursion\":%s,\"lines\":%d,\"is_exported\":%s,\"is_test\":%s,"
        "\"is_entry_point\":%s";
    const char *other_format = "{\"complexity\":%d,\"lines\":%d,\"is_exported\":%s,\"is_test\":%s,"
                               "\"is_entry_point\":%s";
    int prefix_length =
        is_function
            ? snprintf(NULL, 0, function_format, def->complexity, def->cognitive, def->loop_count,
                       def->loop_depth, def->is_recursive ? "true" : "false", def->param_count,
                       def->max_access_depth, def->linear_scan_in_loop, def->alloc_in_loop,
                       def->recursion_in_loop ? "true" : "false",
                       def->unguarded_recursion ? "true" : "false", def->lines,
                       def->is_exported ? "true" : "false", def->is_test ? "true" : "false",
                       def->is_entry_point ? "true" : "false")
            : snprintf(NULL, 0, other_format, def->complexity, def->lines,
                       def->is_exported ? "true" : "false", def->is_test ? "true" : "false",
                       def->is_entry_point ? "true" : "false");
    if (prefix_length < 0) {
        return CBM_DEF_PROPERTIES_INVALID;
    }

    char fingerprint[CBM_MINHASH_HEX_BUF] = {0};
    if (def->fingerprint && def->fingerprint_k > 0) {
        cbm_minhash_to_hex((const cbm_minhash_t *)def->fingerprint, fingerprint,
                           sizeof(fingerprint));
    }

    size_t required = (size_t)prefix_length;
    bool sized = add_string_size(&required, "docstring", def->docstring) &&
                 add_string_size(&required, "signature", def->signature) &&
                 add_string_size(&required, "return_type", def->return_type) &&
                 add_string_size(&required, "parent_class", def->parent_class) &&
                 add_array_size(&required, "decorators", def->decorators) &&
                 add_array_size(&required, "base_classes", def->base_classes) &&
                 add_array_size(&required, "param_names", def->param_names) &&
                 add_array_size(&required, "param_types", def->param_types) &&
                 add_string_size(&required, "route_path", def->route_path) &&
                 add_string_size(&required, "route_method", def->route_method) &&
                 add_string_size(&required, "fp", fingerprint) &&
                 add_string_size(&required, "sp", def->structural_profile);
    /* Content-derived semantic inputs are mandatory as a group: the bound is
     * checked after sizing, never used to choose which field survives. */
    const bool preserve_body_tokens = true;
    sized = sized &&
            add_string_size(&required, "bt", preserve_body_tokens ? def->body_tokens : NULL) &&
            checked_add(&required, 1);
    if (!sized || required + 1 > CBM_DEF_PROPERTIES_MAX_BYTES) {
        return CBM_DEF_PROPERTIES_OVERSIZE;
    }

#ifdef CBM_ENABLE_TEST_SEAMS
    if (atomic_exchange(&g_fail_allocation_once, false)) {
        return CBM_DEF_PROPERTIES_ALLOCATION_UNAVAILABLE;
    }
#endif
    char *json = malloc(required + 1);
    if (!json) {
        return CBM_DEF_PROPERTIES_ALLOCATION_UNAVAILABLE;
    }
    int written =
        is_function
            ? snprintf(json, required + 1, function_format, def->complexity, def->cognitive,
                       def->loop_count, def->loop_depth, def->is_recursive ? "true" : "false",
                       def->param_count, def->max_access_depth, def->linear_scan_in_loop,
                       def->alloc_in_loop, def->recursion_in_loop ? "true" : "false",
                       def->unguarded_recursion ? "true" : "false", def->lines,
                       def->is_exported ? "true" : "false", def->is_test ? "true" : "false",
                       def->is_entry_point ? "true" : "false")
            : snprintf(json, required + 1, other_format, def->complexity, def->lines,
                       def->is_exported ? "true" : "false", def->is_test ? "true" : "false",
                       def->is_entry_point ? "true" : "false");
    if (written != prefix_length) {
        free(json);
        return CBM_DEF_PROPERTIES_INVALID;
    }
    char *cursor = json + written;
    cursor = append_string(cursor, "docstring", def->docstring);
    cursor = append_string(cursor, "signature", def->signature);
    cursor = append_string(cursor, "return_type", def->return_type);
    cursor = append_string(cursor, "parent_class", def->parent_class);
    cursor = append_array(cursor, "decorators", def->decorators);
    cursor = append_array(cursor, "base_classes", def->base_classes);
    cursor = append_array(cursor, "param_names", def->param_names);
    cursor = append_array(cursor, "param_types", def->param_types);
    cursor = append_string(cursor, "route_path", def->route_path);
    cursor = append_string(cursor, "route_method", def->route_method);
    cursor = append_string(cursor, "fp", fingerprint);
    cursor = append_string(cursor, "sp", def->structural_profile);
    cursor = append_string(cursor, "bt", preserve_body_tokens ? def->body_tokens : NULL);
    *cursor++ = '}';
    *cursor = '\0';
    size_t length = (size_t)(cursor - json);
    if (length != required) {
        free(json);
        return CBM_DEF_PROPERTIES_INVALID;
    }
    out->json = json;
    out->length = length;
    return CBM_DEF_PROPERTIES_OK;
}

void cbm_def_properties_destroy(cbm_def_properties_t *properties) {
    if (!properties) {
        return;
    }
    free(properties->json);
    properties->json = NULL;
    properties->length = 0;
}

const char *cbm_def_properties_status_name(cbm_def_properties_status_t status) {
    switch (status) {
    case CBM_DEF_PROPERTIES_OK:
        return "ok";
    case CBM_DEF_PROPERTIES_OVERSIZE:
        return "oversize";
    case CBM_DEF_PROPERTIES_ALLOCATION_UNAVAILABLE:
        return "allocation_unavailable";
    case CBM_DEF_PROPERTIES_INVALID:
        return "invalid";
    }
    return "invalid";
}
