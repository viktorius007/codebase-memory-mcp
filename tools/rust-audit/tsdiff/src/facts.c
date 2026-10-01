/* Emit comparable syntax facts from a tree-sitter-rust tree. One fact per line. */
#include "tree_sitter/api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

const TSLanguage *tree_sitter_rust(void);

typedef struct { char *p; size_t n, cap; } Buf;
static void bputs(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->cap + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void bprintf(Buf *b, const char *fmt, ...) {
    char tmp[512]; va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n > (int)sizeof tmp - 1) n = sizeof tmp - 1;
    bputs(b, tmp, (size_t)n);
}
static int S(TSNode n) { return ts_node_is_null(n) ? -1 : (int)ts_node_start_byte(n); }
static int E(TSNode n) { return ts_node_is_null(n) ? -1 : (int)ts_node_end_byte(n); }
static TSNode F(TSNode n, const char *f) { return ts_node_child_by_field_name(n, f, (uint32_t)strlen(f)); }
static int is(TSNode n, const char *t) { return !ts_node_is_null(n) && strcmp(ts_node_type(n), t) == 0; }
static int is_comment(TSNode n) { return is(n, "line_comment") || is(n, "block_comment"); }
static TSNode first_named(TSNode n) {
    uint32_t c = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < c; i++) { TSNode k = ts_node_named_child(n, i); if (!is_comment(k)) return k; }
    TSNode z; memset(&z, 0, sizeof z); return z;
}
static TSNode first_child_any(TSNode n) {
    uint32_t c = ts_node_is_null(n) ? 0 : ts_node_child_count(n);
    for (uint32_t i = 0; i < c; i++) { TSNode k = ts_node_child(n, i); if (!is_comment(k)) return k; }
    TSNode z; memset(&z, 0, sizeof z); return z;
}
static TSNode child_of_type(TSNode n, const char *t) {
    uint32_t c = ts_node_child_count(n);
    for (uint32_t i = 0; i < c; i++) { TSNode k = ts_node_child(n, i); if (is(k, t)) return k; }
    TSNode z; memset(&z, 0, sizeof z); return z;
}
/* pattern start, widened over a directly preceding `mut` child */
static int pat_start(TSNode owner, TSNode pat) {
    TSNode m = child_of_type(owner, "mutable_specifier");
    if (!ts_node_is_null(m) && !ts_node_is_null(pat) && ts_node_end_byte(m) <= ts_node_start_byte(pat)) return S(m);
    return S(pat);
}
static void use_clause(Buf *b, TSNode n) {
    if (is(n, "use_as_clause")) { TSNode p = F(n, "path"), a = F(n, "alias"); bprintf(b, "USE %d %d A %d %d\n", S(p), E(p), S(a), E(a)); }
    else if (is(n, "use_list")) bprintf(b, "USE -1 -1 L -1 -1\n");
    else if (is(n, "scoped_use_list")) { TSNode p = F(n, "path"); bprintf(b, "USE %d %d L -1 -1\n", S(p), E(p)); }
    else if (is(n, "use_wildcard")) { TSNode p = first_named(n); bprintf(b, "USE %d %d S -1 -1\n", S(p), E(p)); }
    else bprintf(b, "USE %d %d P -1 -1\n", S(n), E(n));
}
static void def(Buf *b, const char *k, TSNode n) { TSNode nm = F(n, "name"); bprintf(b, "DEF %s %d %d\n", k, S(nm), E(nm)); }

static void visit(Buf *b, TSNode n, const char *src) {
    const char *t = ts_node_type(n);
    if (!ts_node_is_named(n)) return;
    if (ts_node_is_missing(n)) return;
    switch (t[0]) {
    case 'f':
        if (!strcmp(t, "function_item") || !strcmp(t, "function_signature_item")) {
            def(b, "FN", n); TSNode r = F(n, "return_type"); if (!ts_node_is_null(r)) bprintf(b, "RET %d %d\n", S(r), E(r));
        } else if (!strcmp(t, "field_declaration")) {
            def(b, "FIELD_DEF", n); TSNode ty = F(n, "type"); bprintf(b, "FTY %d %d\n", S(ty), E(ty));
        } else if (!strcmp(t, "function_type")) {
            TSNode r = F(n, "return_type"); if (!ts_node_is_null(r)) bprintf(b, "RET %d %d\n", S(r), E(r));
        } else if (!strcmp(t, "field_expression")) {
            TSNode p = ts_node_parent(n); int callee = 0;
            TSNode fld = F(n, "field");
            if (is(fld, "field_identifier") && !ts_node_is_null(p)) {
                TSNode q = n;
                if (is(p, "generic_function") && ts_node_eq(F(p, "function"), n)) { q = p; p = ts_node_parent(p); }
                if (is(p, "call_expression") && ts_node_eq(F(p, "function"), q)) callee = 1;
            }
            if (!callee) { TSNode v = F(n, "value"); bprintf(b, "FIELD %d %d %d %d\n", S(v), E(v), S(fld), E(fld)); }
        } else if (!strcmp(t, "for_lifetimes")) {
            /* rust-analyzer models `for<'a>` as a generic parameter list */
            if (ts_node_child_count(n) >= 2) bprintf(b, "TPARAMS %d %d\n", S(ts_node_child(n, 1)), E(n));
        } else if (!strcmp(t, "for_expression")) {
            TSNode p = F(n, "pattern"), v = F(n, "value"); bprintf(b, "FOR %d %d %d %d %d %d\n", S(n), E(n), S(p), E(p), S(v), E(v));
        } else if (!strcmp(t, "field_initializer")) {
            TSNode f = F(n, "field"), v = F(n, "value"); bprintf(b, "SFIELD %d %d %d %d\n", S(f), E(f), S(v), E(v));
        }
        break;
    case 's':
        if (!strcmp(t, "struct_item")) def(b, "STRUCT", n);
        else if (!strcmp(t, "static_item")) def(b, "STATIC", n);
        else if (!strcmp(t, "struct_expression")) { TSNode p = F(n, "name"); bprintf(b, "SLIT %d %d\n", S(p), E(p)); }
        else if (!strcmp(t, "struct_pattern")) { TSNode p = F(n, "type"); bprintf(b, "SPAT %d %d\n", S(p), E(p)); }
        else if (!strcmp(t, "shorthand_field_initializer")) { TSNode p = child_of_type(n, "identifier"); bprintf(b, "SFIELD %d %d -1 -1\n", S(p), E(p)); }
        break;
    case 'e':
        if (!strcmp(t, "enum_item")) def(b, "ENUM", n);
        else if (!strcmp(t, "enum_variant")) def(b, "VARIANT", n);
        break;
    case 'u':
        if (!strcmp(t, "union_item")) def(b, "UNION", n);
        else if (!strcmp(t, "use_declaration")) use_clause(b, F(n, "argument"));
        else if (!strcmp(t, "use_list")) {
            uint32_t c = ts_node_named_child_count(n);
            for (uint32_t i = 0; i < c; i++) { TSNode k = ts_node_named_child(n, i); if (!is_comment(k)) use_clause(b, k); }
        } else if (!strcmp(t, "unary_expression")) bprintf(b, "UNARY %d %d\n", S(n), E(n));
        break;
    case 't':
        if (!strcmp(t, "trait_item")) def(b, "TRAIT", n);
        else if (!strcmp(t, "type_item")) def(b, "TYPE", n);
        else if (!strcmp(t, "type_arguments")) bprintf(b, "GARGS %d %d\n", S(n), E(n));
        else if (!strcmp(t, "type_parameters")) bprintf(b, "TPARAMS %d %d\n", S(n), E(n));
        else if (!strcmp(t, "type_cast_expression")) { TSNode v = F(n, "value"), ty = F(n, "type"); bprintf(b, "CAST %d %d %d %d\n", S(v), E(v), S(ty), E(ty)); }
        else if (!strcmp(t, "try_expression")) bprintf(b, "TRY %d %d\n", S(n), E(n));
        else if (!strcmp(t, "tuple_struct_pattern")) { TSNode p = F(n, "type"); bprintf(b, "TSPAT %d %d\n", S(p), E(p)); }
        break;
    case 'a':
        if (!strcmp(t, "associated_type")) def(b, "TYPE", n);
        else if (!strcmp(t, "assignment_expression")) { TSNode l = F(n, "left"); bprintf(b, "BIN %d %d %d =\n", S(n), E(n), E(l)); }
        else if (!strcmp(t, "await_expression")) bprintf(b, "AWAIT %d %d\n", S(n), E(n));
        break;
    case 'c':
        if (!strcmp(t, "const_item")) def(b, "CONST", n);
        else if (!strcmp(t, "call_expression")) {
            TSNode f = F(n, "function"), a = F(n, "arguments"), g = f;
            if (is(g, "generic_function")) g = F(g, "function");
            if (is(g, "field_expression") && is(F(g, "field"), "field_identifier")) {
                TSNode r = F(g, "value"), nm = F(g, "field");
                bprintf(b, "MCALL %d %d %d %d %d %d\n", S(r), E(r), S(nm), E(nm), S(a), E(a));
            } else bprintf(b, "CALL %d %d %d %d\n", S(f), E(f), S(a), E(a));
        } else if (!strcmp(t, "compound_assignment_expr")) {
            TSNode l = F(n, "left"), o = F(n, "operator");
            bprintf(b, "BIN %d %d %d %.*s\n", S(n), E(n), E(l), E(o) - S(o), src + S(o));
        } else if (!strcmp(t, "closure_expression")) {
            TSNode body = F(n, "body"); bprintf(b, "CLOSURE %d %d %d %d\n", S(n), E(n), S(body), E(body));
            TSNode r = F(n, "return_type"); if (!ts_node_is_null(r)) bprintf(b, "RET %d %d\n", S(r), E(r));
        } else if (!strcmp(t, "closure_parameters")) {
            uint32_t c = ts_node_child_count(n);
            for (uint32_t i = 0; i < c; i++) {
                TSNode k = ts_node_child(n, i);
                if (ts_node_is_named(k)) { if (is_comment(k) || is(k, "parameter") || is(k, "attribute_item")) continue; }
                else if (strcmp(ts_node_type(k), "_")) continue;
                bprintf(b, "PARAM %d %d -1 -1\n", S(k), E(k));
            }
        }
        break;
    case 'm':
        if (!strcmp(t, "mod_item")) def(b, "MOD", n);
        else if (!strcmp(t, "macro_definition")) def(b, "MACRO_RULES", n);
        else if (!strcmp(t, "macro_invocation")) { TSNode p = F(n, "macro"), tt = child_of_type(n, "token_tree"); bprintf(b, "MACRO %d %d %d %d\n", S(p), E(p), S(tt), E(tt)); }
        else if (!strcmp(t, "match_expression")) { TSNode v = F(n, "value"); bprintf(b, "MATCH %d %d %d %d\n", S(n), E(n), S(v), E(v)); }
        else if (!strcmp(t, "match_arm")) {
            TSNode mp = F(n, "pattern"), v = F(n, "value"), p = first_child_any(mp), g = F(mp, "condition");
            bprintf(b, "ARM %d %d %d %d %d %d\n", S(p), E(p), S(v), E(v), S(g), E(g));
        }
        break;
    case 'i':
        if (!strcmp(t, "impl_item")) { TSNode ty = F(n, "type"), tr = F(n, "trait"); bprintf(b, "IMPL %d %d %d %d\n", S(ty), E(ty), S(tr), E(tr)); }
        else if (!strcmp(t, "if_expression")) { TSNode c = F(n, "condition"); bprintf(b, "IF %d %d %d %d\n", S(n), E(n), S(c), E(c)); }
        else if (!strcmp(t, "index_expression")) bprintf(b, "INDEX %d %d\n", S(n), E(n));
        break;
    case 'b':
        if (!strcmp(t, "binary_expression")) {
            TSNode l = F(n, "left"), o = F(n, "operator");
            bprintf(b, "BIN %d %d %d %.*s\n", S(n), E(n), E(l), E(o) - S(o), src + S(o));
        }
        break;
    case 'r':
        if (!strcmp(t, "range_expression")) bprintf(b, "RANGE %d %d\n", S(n), E(n));
        else if (!strcmp(t, "reference_expression")) bprintf(b, "REF %d %d\n", S(n), E(n));
        else if (!strcmp(t, "return_expression")) bprintf(b, "RETURN %d %d\n", S(n), E(n));
        break;
    case 'l':
        if (!strcmp(t, "let_declaration")) {
            TSNode p = F(n, "pattern"), ty = F(n, "type"), v = F(n, "value");
            bprintf(b, "LET %d %d %d %d %d %d\n", pat_start(n, p), E(p), S(ty), E(ty), S(v), E(v));
        }
        break;
    case 'p':
        if (!strcmp(t, "parameter")) {
            TSNode p = F(n, "pattern"), ty = F(n, "type");
            if (!is(p, "self")) bprintf(b, "PARAM %d %d %d %d\n", pat_start(n, p), E(p), S(ty), E(ty));
        } else if (!strcmp(t, "parameters")) {
            TSNode par = ts_node_parent(n);
            int sugar = is(par, "function_type") && !ts_node_is_null(F(par, "trait"));
            uint32_t c = ts_node_child_count(n);
            for (uint32_t i = 0; !sugar && i < c; i++) {
                TSNode k = ts_node_child(n, i);
                if (ts_node_is_named(k)) {
                    if (is_comment(k) || is(k, "parameter") || is(k, "self_parameter") || is(k, "variadic_parameter") || is(k, "attribute_item")) continue;
                } else if (strcmp(ts_node_type(k), "_")) continue;
                bprintf(b, "PARAM -1 -1 %d %d\n", S(k), E(k));
            }
        }
        break;
    case 'o':
        if (!strcmp(t, "ordered_field_declaration_list")) {
            uint32_t c = ts_node_child_count(n);
            for (uint32_t i = 0; i < c; i++) {
                const char *fn = ts_node_field_name_for_child(n, i);
                if (fn && !strcmp(fn, "type")) { TSNode k = ts_node_child(n, i); bprintf(b, "FTY %d %d\n", S(k), E(k)); }
            }
        }
        break;
    case 'w':
        if (!strcmp(t, "while_expression")) { TSNode c = F(n, "condition"); bprintf(b, "WHILE %d %d %d %d\n", S(n), E(n), S(c), E(c)); }
        break;
    }
}

static _Thread_local TSParser *g_parser;

/* Returns malloc'd text: fact lines, then lines starting with '!' describing ERROR/MISSING nodes. */
char *ts_facts(const char *src, uint32_t len, uint32_t *out_len, uint32_t *n_err, uint32_t *n_missing) {
    if (!g_parser) { g_parser = ts_parser_new(); ts_parser_set_language(g_parser, tree_sitter_rust()); }
    ts_parser_reset(g_parser);
    TSTree *tree = ts_parser_parse_string(g_parser, NULL, src, len);
    Buf b = {0}; bputs(&b, "", 0);
    *n_err = 0; *n_missing = 0;
    TSTreeCursor cur = ts_tree_cursor_new(ts_tree_root_node(tree));
    for (;;) {
        TSNode n = ts_tree_cursor_current_node(&cur);
        int descend = 1;
        if (ts_node_is_missing(n)) { (*n_missing)++; bprintf(&b, "!MISSING %d %d %s\n", S(n), E(n), ts_node_type(n)); }
        else if (ts_node_is_error(n)) {
            (*n_err)++; TSNode p = ts_node_parent(n);
            bprintf(&b, "!ERROR %d %d %s\n", S(n), E(n), ts_node_is_null(p) ? "-" : ts_node_type(p));
        } else visit(&b, n, src);
        if (descend && ts_tree_cursor_goto_first_child(&cur)) continue;
        while (!ts_tree_cursor_goto_next_sibling(&cur)) { if (!ts_tree_cursor_goto_parent(&cur)) goto done; }
    }
done:
    ts_tree_cursor_delete(&cur);
    ts_tree_delete(tree);
    *out_len = (uint32_t)b.n;
    return b.p;
}
void ts_facts_free(char *p) { free(p); }
