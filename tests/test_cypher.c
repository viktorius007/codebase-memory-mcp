/*
 * test_cypher.c — Tests for the Cypher query engine.
 *
 * Ported from internal/cypher/cypher_test.go (1016 LOC).
 * Covers lexer, parser, and end-to-end execution.
 */
#include "test_framework.h"
#include "../src/foundation/compat.h"
#include "../src/foundation/compat_thread.h"
#include <cypher/cypher.h>
#include <store/store.h>
#include <foundation/constants.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/wait.h> /* fork/waitpid crash-isolation for the projection-width guard */
#include <unistd.h>
#endif

/* ══════════════════════════════════════════════════════════════════
 *  LEXER TESTS
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_lex_simple_match) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("MATCH (n:Function)", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);

    /* MATCH ( n : Function ) EOF */
    ASSERT_GTE(r.count, 6);
    ASSERT_EQ(r.tokens[0].type, TOK_MATCH);
    ASSERT_EQ(r.tokens[1].type, TOK_LPAREN);
    ASSERT_EQ(r.tokens[2].type, TOK_IDENT);
    ASSERT_STR_EQ(r.tokens[2].text, "n");
    ASSERT_EQ(r.tokens[3].type, TOK_COLON);
    ASSERT_EQ(r.tokens[4].type, TOK_IDENT);
    ASSERT_STR_EQ(r.tokens[4].text, "Function");
    ASSERT_EQ(r.tokens[5].type, TOK_RPAREN);

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_relationship) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("-[:CALLS]->", &r);
    ASSERT_EQ(rc, 0);

    /* - [ : CALLS ] - > EOF */
    ASSERT_GTE(r.count, 7);
    ASSERT_EQ(r.tokens[0].type, TOK_DASH);
    ASSERT_EQ(r.tokens[1].type, TOK_LBRACKET);
    ASSERT_EQ(r.tokens[2].type, TOK_COLON);
    ASSERT_EQ(r.tokens[3].type, TOK_IDENT);
    ASSERT_STR_EQ(r.tokens[3].text, "CALLS");
    ASSERT_EQ(r.tokens[4].type, TOK_RBRACKET);
    ASSERT_EQ(r.tokens[5].type, TOK_DASH);
    ASSERT_EQ(r.tokens[6].type, TOK_GT);

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_string_literal) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("\"hello world\"", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.count, 1);
    ASSERT_EQ(r.tokens[0].type, TOK_STRING);
    ASSERT_STR_EQ(r.tokens[0].text, "hello world");

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_single_quote_string) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("'hello'", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.tokens[0].type, TOK_STRING);
    ASSERT_STR_EQ(r.tokens[0].text, "hello");

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_string_overflow) {
    /* Build a string literal longer than 4096 bytes to verify we don't
     * overflow the stack buffer in lex_string_literal. */
    const int big = 5000;
    /* query: "AAAA...A"  (quotes included) */
    char *query = malloc(big + 3); /* quote + big chars + quote + NUL */
    ASSERT_NOT_NULL(query);
    query[0] = '"';
    memset(query + 1, 'A', big);
    query[big + 1] = '"';
    query[big + 2] = '\0';

    cbm_lex_result_t r = {0};
    int rc = cbm_lex(query, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_GTE(r.count, 1);
    ASSERT_EQ(r.tokens[0].type, TOK_STRING);
    /* The string should be truncated to CBM_SZ_4K - 1 (4095) characters. */
    ASSERT_EQ((int)strlen(r.tokens[0].text), 4095);

    cbm_lex_free(&r);
    free(query);
    PASS();
}

TEST(cypher_lex_number) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("42 3.14", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.count, 2);
    ASSERT_EQ(r.tokens[0].type, TOK_NUMBER);
    ASSERT_STR_EQ(r.tokens[0].text, "42");
    ASSERT_EQ(r.tokens[1].type, TOK_NUMBER);
    ASSERT_STR_EQ(r.tokens[1].text, "3.14");

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_operators) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("= =~ >= <= ..", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.count, 5);
    ASSERT_EQ(r.tokens[0].type, TOK_EQ);
    ASSERT_EQ(r.tokens[1].type, TOK_EQTILDE);
    ASSERT_EQ(r.tokens[2].type, TOK_GTE);
    ASSERT_EQ(r.tokens[3].type, TOK_LTE);
    ASSERT_EQ(r.tokens[4].type, TOK_DOTDOT);

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_keywords_case_insensitive) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("match WHERE Return limit", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.tokens[0].type, TOK_MATCH);
    ASSERT_EQ(r.tokens[1].type, TOK_WHERE);
    ASSERT_EQ(r.tokens[2].type, TOK_RETURN);
    ASSERT_EQ(r.tokens[3].type, TOK_LIMIT);

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_pipe_and_star) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("[:TYPE1|TYPE2*1..3]", &r);
    ASSERT_EQ(rc, 0);

    /* [ : TYPE1 | TYPE2 * 1 .. 3 ] */
    ASSERT_GTE(r.count, 9);
    ASSERT_EQ(r.tokens[3].type, TOK_PIPE);
    ASSERT_EQ(r.tokens[5].type, TOK_STAR);
    ASSERT_EQ(r.tokens[6].type, TOK_NUMBER);
    ASSERT_STR_EQ(r.tokens[6].text, "1");
    ASSERT_EQ(r.tokens[7].type, TOK_DOTDOT);
    ASSERT_EQ(r.tokens[8].type, TOK_NUMBER);
    ASSERT_STR_EQ(r.tokens[8].text, "3");

    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_full_query) {
    const char *q = "MATCH (f:Function)-[:CALLS]->(g:Function) "
                    "WHERE f.name =~ \".*Order.*\" "
                    "RETURN f.name, g.name LIMIT 10";
    cbm_lex_result_t r = {0};
    int rc = cbm_lex(q, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    /* Should have many tokens; just check it doesn't crash */
    ASSERT_GT(r.count, 20);

    cbm_lex_free(&r);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PARSER TESTS
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_parse_simple_node) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function)", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(err);
    ASSERT_NOT_NULL(q);

    ASSERT_EQ(cbm_query_pattern(q).node_count, 1);
    ASSERT_EQ(cbm_query_pattern(q).rel_count, 0);
    ASSERT_STR_EQ(cbm_query_pattern(q).nodes[0].variable, "f");
    ASSERT_STR_EQ(cbm_query_pattern(q).nodes[0].label, "Function");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_relationship_outbound) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function)-[:CALLS]->(g:Function)", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);

    ASSERT_EQ(cbm_query_pattern(q).node_count, 2);
    ASSERT_EQ(cbm_query_pattern(q).rel_count, 1);
    ASSERT_STR_EQ(cbm_query_pattern(q).rels[0].types[0], "CALLS");
    ASSERT_STR_EQ(cbm_query_pattern(q).rels[0].direction, "outbound");
    ASSERT_EQ(cbm_query_pattern(q).rels[0].min_hops, 1);
    ASSERT_EQ(cbm_query_pattern(q).rels[0].max_hops, 1);

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_relationship_inbound) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function)<-[:CALLS]-(g:Function)", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);

    ASSERT_STR_EQ(cbm_query_pattern(q).rels[0].direction, "inbound");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_relationship_any) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function)-[:CALLS]-(g:Function)", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);

    ASSERT_STR_EQ(cbm_query_pattern(q).rels[0].direction, "any");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_variable_length) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function)-[:CALLS*1..3]->(g:Function)", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);

    ASSERT_EQ(cbm_query_pattern(q).rels[0].min_hops, 1);
    ASSERT_EQ(cbm_query_pattern(q).rels[0].max_hops, 3);

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_variable_length_unbounded) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f)-[:CALLS*]->(g)", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);

    ASSERT_EQ(cbm_query_pattern(q).rels[0].min_hops, 1);
    ASSERT_EQ(cbm_query_pattern(q).rels[0].max_hops, 0); /* 0 = unbounded */

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_multiple_edge_types) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f)-[:CALLS|HTTP_CALLS]->(g)", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);

    ASSERT_EQ(cbm_query_pattern(q).rels[0].type_count, 2);
    ASSERT_STR_EQ(cbm_query_pattern(q).rels[0].types[0], "CALLS");
    ASSERT_STR_EQ(cbm_query_pattern(q).rels[0].types[1], "HTTP_CALLS");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_where_clause) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) WHERE f.name = \"Foo\"", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);
    ASSERT_NOT_NULL(q->where);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_CONDITION);
    ASSERT_STR_EQ(q->where->root->cond.variable, "f");
    ASSERT_STR_EQ(q->where->root->cond.property, "name");
    ASSERT_STR_EQ(q->where->root->cond.op, "=");
    ASSERT_STR_EQ(q->where->root->cond.value, "Foo");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_where_regex) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) WHERE f.name =~ \".*Order.*\"", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_CONDITION);
    ASSERT_STR_EQ(q->where->root->cond.op, "=~");
    ASSERT_STR_EQ(q->where->root->cond.value, ".*Order.*");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_where_and) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) WHERE f.name = \"A\" AND f.label = \"Function\"",
                              &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_AND);
    ASSERT_NOT_NULL(q->where->root->left);
    ASSERT_NOT_NULL(q->where->root->right);

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_return_simple) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) RETURN f.name, f.qualified_name", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->ret);
    ASSERT_EQ(q->ret->count, 2);
    ASSERT_STR_EQ(q->ret->items[0].variable, "f");
    ASSERT_STR_EQ(q->ret->items[0].property, "name");
    ASSERT_STR_EQ(q->ret->items[1].property, "qualified_name");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_return_count) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f)-[:CALLS]->(g) RETURN f.name, COUNT(g) AS cnt", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(q->ret->count, 2);
    ASSERT_NOT_NULL(q->ret->items[1].func);
    ASSERT_STR_EQ(q->ret->items[1].func, "COUNT");
    ASSERT_STR_EQ(q->ret->items[1].alias, "cnt");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_return_order_limit) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (f:Function) RETURN f.name ORDER BY f.name DESC LIMIT 5", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->ret->order_by);
    ASSERT_STR_EQ(q->ret->order_dir, "DESC");
    ASSERT_EQ(q->ret->limit, 5);

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_return_distinct) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) RETURN DISTINCT f.label", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT(q->ret->distinct);

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_inline_props) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function {name: \"Foo\"})", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(cbm_query_pattern(q).nodes[0].prop_count, 1);
    ASSERT_STR_EQ(cbm_query_pattern(q).nodes[0].props[0].key, "name");
    ASSERT_STR_EQ(cbm_query_pattern(q).nodes[0].props[0].value, "Foo");

    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_error) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("INVALID QUERY", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    free(err);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  EXECUTION TESTS (end-to-end against store)
 * ══════════════════════════════════════════════════════════════════ */

/* Helper: set up the standard test graph.
 * Nodes: HandleOrder, ValidateOrder, SubmitOrder (Function), main (Module), LogError (Function)
 * Edges: HandleOrder→ValidateOrder (CALLS), ValidateOrder→SubmitOrder (CALLS),
 *        HandleOrder→LogError (CALLS), main→HandleOrder (DEFINES)
 */
static cbm_store_t *setup_cypher_store(void) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "HandleOrder",
                     .qualified_name = "test.HandleOrder",
                     .file_path = "handler.go",
                     .start_line = 10,
                     .end_line = 30};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "ValidateOrder",
                     .qualified_name = "test.ValidateOrder",
                     .file_path = "validate.go",
                     .start_line = 5,
                     .end_line = 15};
    cbm_node_t n3 = {.project = "test",
                     .label = "Function",
                     .name = "SubmitOrder",
                     .qualified_name = "test.SubmitOrder",
                     .file_path = "submit.go"};
    cbm_node_t n4 = {
        .project = "test", .label = "Module", .name = "main", .qualified_name = "test.main"};
    cbm_node_t n5 = {.project = "test",
                     .label = "Function",
                     .name = "LogError",
                     .qualified_name = "test.LogError",
                     .file_path = "log.go"};

    int64_t id1 = cbm_store_upsert_node(s, &n1);
    int64_t id2 = cbm_store_upsert_node(s, &n2);
    int64_t id3 = cbm_store_upsert_node(s, &n3);
    int64_t id4 = cbm_store_upsert_node(s, &n4);
    int64_t id5 = cbm_store_upsert_node(s, &n5);

    cbm_edge_t e1 = {.project = "test", .source_id = id1, .target_id = id2, .type = "CALLS"};
    cbm_edge_t e2 = {.project = "test", .source_id = id2, .target_id = id3, .type = "CALLS"};
    cbm_edge_t e3 = {.project = "test", .source_id = id1, .target_id = id5, .type = "CALLS"};
    cbm_edge_t e4 = {.project = "test", .source_id = id4, .target_id = id1, .type = "DEFINES"};
    cbm_store_insert_edge(s, &e1);
    cbm_store_insert_edge(s, &e2);
    cbm_store_insert_edge(s, &e3);
    cbm_store_insert_edge(s, &e4);

    return s;
}

TEST(cypher_exec_match_all_functions) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s, "MATCH (f:Function)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 4); /* HandleOrder, ValidateOrder, SubmitOrder, LogError */
    ASSERT_GT(r.col_count, 0);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_eq) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\"", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #874: coalesce(var.prop, literal) in WHERE — null-safe numeric filters
 * for audit queries over OPTIONAL graph properties. The parser rejected the
 * call outright ("unexpected operator"); RETURN-side coalesce already
 * worked, so only the WHERE leaf needs it. Semantics: when the property is
 * missing/empty, the literal default is compared instead. */
/* #797: variable-length / repeated-variable path semantics. Fixture:
 * loopy has a SELF-LOOP as one of its outbound CALLS edges plus a real
 * 2-chain loopy->mid->leaf. Correct openCypher semantics:
 *  - a repeated node variable must unify: (a)-[:CALLS]->(a) matches ONLY
 *    the self-loop, not every edge;
 *  - relationship uniqueness within a path: the self-loop cannot be
 *    traversed repeatedly, so no *k..k path exists beyond the real chain;
 *  - the engine hop cap must not fabricate or silently truncate results. */
TEST(cypher_exec_varlength_path_semantics_issue797) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t loopy = {.project = "test",
                        .label = "Function",
                        .name = "loopy",
                        .qualified_name = "test.mod.loopy",
                        .file_path = "mod.go",
                        .start_line = 1,
                        .end_line = 2};
    cbm_node_t mid = {.project = "test",
                      .label = "Function",
                      .name = "mid",
                      .qualified_name = "test.mod.mid",
                      .file_path = "mod.go",
                      .start_line = 3,
                      .end_line = 4};
    cbm_node_t leaf = {.project = "test",
                       .label = "Function",
                       .name = "leaf",
                       .qualified_name = "test.mod.leaf",
                       .file_path = "mod.go",
                       .start_line = 5,
                       .end_line = 6};
    int64_t id_loopy = cbm_store_upsert_node(s, &loopy);
    int64_t id_mid = cbm_store_upsert_node(s, &mid);
    int64_t id_leaf = cbm_store_upsert_node(s, &leaf);
    ASSERT_GT(id_loopy, 0);
    cbm_edge_t self_loop = {
        .project = "test", .source_id = id_loopy, .target_id = id_loopy, .type = "CALLS"};
    cbm_edge_t e1 = {
        .project = "test", .source_id = id_loopy, .target_id = id_mid, .type = "CALLS"};
    cbm_edge_t e2 = {.project = "test", .source_id = id_mid, .target_id = id_leaf, .type = "CALLS"};
    cbm_store_insert_edge(s, &self_loop);
    cbm_store_insert_edge(s, &e1);
    cbm_store_insert_edge(s, &e2);

    /* Bug 1: repeated variable must unify — only the self-loop matches. */
    cbm_cypher_result_t r1 = {0};
    ASSERT_EQ(cbm_cypher_execute(s, "MATCH (a)-[:CALLS]->(a) RETURN a.name", "test", 0, &r1), 0);
    ASSERT_EQ(r1.row_count, 1);
    cbm_cypher_result_free(&r1);

    /* Bug 2: *2..2 from loopy — only the REAL 2-chain (leaf); the self-loop
     * must not be reused to pad paths (relationship uniqueness). */
    cbm_cypher_result_t r2 = {0};
    ASSERT_EQ(cbm_cypher_execute(s,
                                 "MATCH (a {name: \"loopy\"})-[:CALLS*2..2]->(b) "
                                 "RETURN DISTINCT b.name",
                                 "test", 0, &r2),
              0);
    ASSERT_EQ(r2.row_count, 1); /* leaf only */
    cbm_cypher_result_free(&r2);

    /* Bug 2 amplifier: no directed path of length 5 exists at all. */
    cbm_cypher_result_t r3 = {0};
    ASSERT_EQ(cbm_cypher_execute(s,
                                 "MATCH (a {name: \"loopy\"})-[:CALLS*5..5]->(b) "
                                 "RETURN b.name",
                                 "test", 0, &r3),
              0);
    ASSERT_EQ(r3.row_count, 0);
    cbm_cypher_result_free(&r3);

    /* Bug 3: a hop range beyond the engine ceiling must be an ADVERTISED
     * clamp, not silently indistinguishable from "no such path". */
    cbm_cypher_result_t r4 = {0};
    ASSERT_EQ(
        cbm_cypher_execute(s, "MATCH (a)-[:CALLS*150..150]->(b) RETURN b.name", "test", 0, &r4), 0);
    ASSERT_EQ(r4.row_count, 0);
    ASSERT_NOT_NULL(r4.warning);
    ASSERT_NOT_NULL(strstr(r4.warning, "clamped"));
    cbm_cypher_result_free(&r4);

    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_coalesce_issue874) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t a = {.project = "test",
                    .label = "Function",
                    .name = "deep_a",
                    .qualified_name = "test.mod.deep_a",
                    .file_path = "mod.py",
                    .start_line = 1,
                    .end_line = 2,
                    .properties_json = "{\"transitive_loop_depth\":3}"};
    cbm_node_t b = {.project = "test",
                    .label = "Function",
                    .name = "deep_b",
                    .qualified_name = "test.mod.deep_b",
                    .file_path = "mod.py",
                    .start_line = 3,
                    .end_line = 4,
                    .properties_json = "{\"transitive_loop_depth\":1}"};
    cbm_node_t c = {.project = "test",
                    .label = "Function",
                    .name = "plain_c",
                    .qualified_name = "test.mod.plain_c",
                    .file_path = "mod.py",
                    .start_line = 5,
                    .end_line = 6};
    ASSERT_GT(cbm_store_upsert_node(s, &a), 0);
    ASSERT_GT(cbm_store_upsert_node(s, &b), 0);
    ASSERT_GT(cbm_store_upsert_node(s, &c), 0);

    /* Default FAILS the predicate: only the node with depth 3 matches. */
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE "
                                "coalesce(f.transitive_loop_depth, 0) >= 2 "
                                "RETURN f.qualified_name LIMIT 10",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);

    /* Default PASSES: the property-less node is included via the default. */
    cbm_cypher_result_t r2 = {0};
    rc = cbm_cypher_execute(s,
                            "MATCH (f:Function) WHERE "
                            "coalesce(f.transitive_loop_depth, 9) >= 2 "
                            "RETURN f.qualified_name LIMIT 10",
                            "test", 0, &r2);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r2.row_count, 2); /* deep_a (3) + plain_c (default 9) */
    cbm_cypher_result_free(&r2);

    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_regex) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.name =~ \".*Order.*\"", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 3); /* HandleOrder, ValidateOrder, SubmitOrder */

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_contains) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.name CONTAINS \"Order\"", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 3);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_starts_with) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.name STARTS WITH \"Handle\"", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_return_properties) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" "
                                "RETURN f.name, f.qualified_name, f.file_path",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_EQ(r.col_count, 3);
    /* Columns should be f.name, f.qualified_name, f.file_path */
    ASSERT_STR_EQ(r.columns[0], "f.name");
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    ASSERT_STR_EQ(r.rows[0][1], "test.HandleOrder");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* ── Scalar / introspection functions (full-suite Tier 1) ──────── */

TEST(cypher_func_labels) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN labels(f)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "[\"Function\"]");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_func_type) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function)-[r:CALLS]->(g:Function) RETURN type(r) LIMIT 1", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "CALLS");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_func_id) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN id(f)",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    /* id is a non-empty numeric string */
    ASSERT_TRUE(r.rows[0][0][0] >= '0' && r.rows[0][0][0] <= '9');
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_func_keys) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN keys(f)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_TRUE(strstr(r.rows[0][0], "\"name\"") != NULL);
    ASSERT_TRUE(strstr(r.rows[0][0], "\"qualified_name\"") != NULL);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_func_properties) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN properties(f)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_EQ(r.rows[0][0][0], '{'); /* a JSON object */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_func_tointeger_tofloat) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" "
                                "RETURN toInteger(f.start_line), toFloat(f.start_line)",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "10"); /* start_line = 10 */
    ASSERT_STR_EQ(r.rows[0][1], "10");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_func_size_reverse) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"LogError\" "
                                "RETURN size(f.name), length(f.name), reverse(f.name)",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "8"); /* "LogError" has 8 chars */
    ASSERT_STR_EQ(r.rows[0][1], "8");
    ASSERT_STR_EQ(r.rows[0][2], "rorrEgoL");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_func_multiarg) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" "
                                "RETURN substring(f.name, 0, 6), left(f.name, 6), "
                                "right(f.name, 5), replace(f.name, \"Order\", \"Req\"), "
                                "coalesce(f.missing, \"fallback\")",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "Handle");    /* substring("HandleOrder",0,6) */
    ASSERT_STR_EQ(r.rows[0][1], "Handle");    /* left(...,6) */
    ASSERT_STR_EQ(r.rows[0][2], "Order");     /* right("HandleOrder",5) */
    ASSERT_STR_EQ(r.rows[0][3], "HandleReq"); /* replace Order->Req */
    ASSERT_STR_EQ(r.rows[0][4], "fallback");  /* coalesce: f.missing empty -> literal */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* issue #874: coalesce() in WHERE — null-safe numeric filter over an optional
 * JSON property. Exact repro shape from the issue: nodes lacking the property
 * fall back to the literal default instead of failing to parse. */
TEST(cypher_issue874_where_coalesce_numeric) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "DeepLoop",
                     .qualified_name = "test.DeepLoop",
                     .file_path = "deep.go",
                     .properties_json = "{\"transitive_loop_depth\":5}"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "NoMetrics",
                     .qualified_name = "test.NoMetrics",
                     .file_path = "flat.go"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) "
                                "WHERE coalesce(f.transitive_loop_depth, 0) >= 2 "
                                "RETURN f.qualified_name LIMIT 10",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1); /* only DeepLoop; NoMetrics coalesces to 0 */
    ASSERT_STR_EQ(r.rows[0][0], "test.DeepLoop");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* issue #874: coalesce() in WHERE with a string fallback and first-arg-wins. */
TEST(cypher_issue874_where_coalesce_string) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    /* Missing property on every node → fallback literal matches all 4 Functions */
    int rc = cbm_cypher_execute(
        s,
        "MATCH (f:Function) WHERE coalesce(f.missing, \"fallback\") = \"fallback\" "
        "RETURN f.name",
        "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 4);
    cbm_cypher_result_free(&r);

    /* Present first arg wins over the fallback */
    cbm_cypher_result_t r2 = {0};
    rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE coalesce(f.name, \"zz\") = \"HandleOrder\" RETURN f.name",
        "test", 0, &r2);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r2.row_count, 1);
    ASSERT_STR_EQ(r2.rows[0][0], "HandleOrder");
    cbm_cypher_result_free(&r2);

    cbm_store_close(s);
    PASS();
}

/* issue #874: function LHS composes with NOT and AND like any other condition. */
TEST(cypher_issue874_where_coalesce_not_and) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE NOT coalesce(f.missing, \"x\") = \"x\" RETURN f.name", "test",
        0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 0); /* every node coalesces to "x" — NOT filters all */
    cbm_cypher_result_free(&r);

    cbm_cypher_result_t r2 = {0};
    rc = cbm_cypher_execute(s,
                            "MATCH (f:Function) WHERE coalesce(f.missing, \"1\") = \"1\" "
                            "AND f.name CONTAINS \"Order\" RETURN f.name",
                            "test", 0, &r2);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r2.row_count, 3); /* HandleOrder, ValidateOrder, SubmitOrder */
    cbm_cypher_result_free(&r2);

    cbm_store_close(s);
    PASS();
}

/* issue #874: the other multi-arg scalar functions work in WHERE too. */
TEST(cypher_issue874_where_substring) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE substring(f.name, 0, 6) = \"Handle\" RETURN f.name", "test", 0,
        &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* issue #874: an unrecognised function in WHERE must fail loudly with the
 * supported set, not the misleading "unexpected operator". */
TEST(cypher_issue874_where_unsupported_func_error) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (f:Function) WHERE foo(f.name) = \"x\" RETURN f.name", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    ASSERT_TRUE(strstr(err, "unsupported function 'foo'") != NULL);
    free(err);
    PASS();
}

TEST(cypher_exists_no_callers) {
    /* NOT EXISTS { (f)<-[:CALLS]-() } → functions with no CALLS caller.
     * HandleOrder has only an incoming DEFINES edge (not CALLS), so it is the
     * sole match — proving EXISTS is edge-type-specific (in_degree=1 here). */
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE NOT EXISTS { (f)<-[:CALLS]-() } RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exists_has_outgoing_calls) {
    /* EXISTS { (f)-[:CALLS]->() } → functions that call something.
     * HandleOrder (→ValidateOrder, →LogError) and ValidateOrder (→SubmitOrder). */
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE EXISTS { (f)-[:CALLS]->() } RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_calls_relationship) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "RETURN f.name, g.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* HandleOrder→ValidateOrder, HandleOrder→LogError, ValidateOrder→SubmitOrder */
    ASSERT_EQ(r.row_count, 3);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_calls_with_where) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "WHERE f.name = \"HandleOrder\" "
                                "RETURN f.name, g.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2); /* →ValidateOrder, →LogError */

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_inbound) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)<-[:CALLS]-(g:Function) "
                                "WHERE f.name = \"ValidateOrder\" "
                                "RETURN g.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1); /* HandleOrder calls ValidateOrder */
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_count) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "RETURN f.name, COUNT(g) AS cnt",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* HandleOrder→2, ValidateOrder→1 */
    ASSERT_EQ(r.row_count, 2);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_limit) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name LIMIT 2", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_order_by) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name ORDER BY f.name ASC", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 4);
    /* Alphabetical: HandleOrder, LogError, SubmitOrder, ValidateOrder */
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    ASSERT_STR_EQ(r.rows[1][0], "LogError");
    ASSERT_STR_EQ(r.rows[2][0], "SubmitOrder");
    ASSERT_STR_EQ(r.rows[3][0], "ValidateOrder");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_variable_length) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    /* HandleOrder →CALLS→ ValidateOrder →CALLS→ SubmitOrder (2 hops) */
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS*1..3]->(g:Function) "
                                "WHERE f.name = \"HandleOrder\" "
                                "RETURN g.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* Should find: ValidateOrder (1 hop), SubmitOrder (2 hops), LogError (1 hop) */
    ASSERT_GTE(r.row_count, 3);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Reproduce-first (#887): an EXPLICIT variable-length upper bound must still be
 * capped at the engine ceiling (cbm_cypher_max_depth(), default 10). On
 * origin/main, expand_var_length honoured an explicit `*1..N` verbatim (only the
 * unbounded `*` / `*..m` forms were capped), so `[:CALLS*1..N]` passed N straight
 * to cbm_store_bfs — an unbounded traversal (a DoS on cyclic graphs). RED before
 * the clamp: a *1..12 walk over a 13-node chain
 * returns all 12 hops (N01..N12). GREEN after: it stops at the depth-10 ceiling
 * (N01..N10); N11/N12 are never emitted. max_rows=64 keeps the binding-expansion
 * cap (bind_cap*10) well above the hop count, so DEPTH — not the binding cap — is
 * the bound under test. */
TEST(cypher_exec_var_length_explicit_bound_capped) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    /* Linear chain N00 -CALLS-> N01 -> ... -> N12 (13 nodes, one node per hop). */
    int64_t ids[13];
    for (int i = 0; i < 13; i++) {
        char name[8];
        char qn[24];
        snprintf(name, sizeof(name), "N%02d", i);
        snprintf(qn, sizeof(qn), "test.N%02d", i);
        cbm_node_t n = {.project = "test",
                        .label = "Function",
                        .name = name,
                        .qualified_name = qn,
                        .file_path = "chain.go"};
        ids[i] = cbm_store_upsert_node(s, &n);
    }
    for (int i = 0; i < 12; i++) {
        cbm_edge_t e = {
            .project = "test", .source_id = ids[i], .target_id = ids[i + 1], .type = "CALLS"};
        cbm_store_insert_edge(s, &e);
    }

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (a:Function {name: \"N00\"})-[:CALLS*1..12]->"
                                "(x:Function) RETURN x.name",
                                "test", 64, &r);
    ASSERT_EQ(rc, 0);

    /* Capped at 10 hops → exactly N01..N10; N11/N12 are beyond the ceiling. */
    ASSERT_EQ(r.row_count, 10);
    bool saw_n10 = false;
    bool saw_n11 = false;
    bool saw_n12 = false;
    for (int i = 0; i < r.row_count; i++) {
        const char *v = r.rows[i][0];
        if (v && strcmp(v, "N10") == 0) {
            saw_n10 = true;
        }
        if (v && strcmp(v, "N11") == 0) {
            saw_n11 = true;
        }
        if (v && strcmp(v, "N12") == 0) {
            saw_n12 = true;
        }
    }
    ASSERT_TRUE(saw_n10);  /* within the ceiling — proves the traversal really ran */
    ASSERT_FALSE(saw_n11); /* clamped away */
    ASSERT_FALSE(saw_n12);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_defines_edge) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (m:Module)-[:DEFINES]->(f:Function) "
                                "RETURN m.name, f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "main");
    ASSERT_STR_EQ(r.rows[0][1], "HandleOrder");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_no_results) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.name = \"NonExistent\"", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 0);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_numeric) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.start_line > \"8\" "
                                "RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* HandleOrder starts at 10 */
    ASSERT_GTE(r.row_count, 1);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* --- Ported from cypher_test.go: TestExecuteDistinct --- */
TEST(cypher_exec_distinct) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN DISTINCT f.label", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* All 4 Function nodes share label "Function" → 1 distinct row */
    ASSERT_EQ(r.row_count, 1);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* issue #238: WITH DISTINCT must deduplicate projected rows (previously the
 * DISTINCT keyword on WITH was parsed but silently ignored). */
TEST(cypher_exec_with_distinct_issue238) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    /* 4 Function nodes all share label "Function" → WITH DISTINCT collapses to
     * one row; without dedup this returned 4. */
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) WITH DISTINCT f.label AS lbl RETURN lbl",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);

    /* Control: without DISTINCT, all 4 rows flow through. */
    cbm_cypher_result_t r2 = {0};
    rc = cbm_cypher_execute(s, "MATCH (f:Function) WITH f.label AS lbl RETURN lbl", "test", 0, &r2);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r2.row_count, 4);
    cbm_cypher_result_free(&r2);

    cbm_store_close(s);
    PASS();
}

/* issue #241: label tests in WHERE clauses (openCypher `WHERE n:Label`) —
 * previously a parse error. */
TEST(cypher_exec_where_label_test_issue241) {
    cbm_store_t *s = setup_cypher_store();

    /* f:Function is true for all 4 Function nodes. */
    cbm_cypher_result_t r = {0};
    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) WHERE f:Function RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 4);
    cbm_cypher_result_free(&r);

    /* f:Class matches none of the functions. */
    cbm_cypher_result_t r2 = {0};
    rc = cbm_cypher_execute(s, "MATCH (f:Function) WHERE f:Class RETURN f.name", "test", 0, &r2);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r2.row_count, 0);
    cbm_cypher_result_free(&r2);

    /* Negated label test: NOT f:Class is always true. */
    cbm_cypher_result_t r3 = {0};
    rc =
        cbm_cypher_execute(s, "MATCH (f:Function) WHERE NOT f:Class RETURN f.name", "test", 0, &r3);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r3.row_count, 4);
    cbm_cypher_result_free(&r3);

    cbm_store_close(s);
    PASS();
}

/* issue #239: COUNT(DISTINCT x) — previously a parse error. */
TEST(cypher_exec_count_distinct_issue239) {
    cbm_store_t *s = setup_cypher_store();

    /* 4 functions all share label "Function" → COUNT(DISTINCT f.label) = 1. */
    cbm_cypher_result_t r = {0};
    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) RETURN count(DISTINCT f.label)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "1");
    cbm_cypher_result_free(&r);

    /* Non-distinct COUNT counts all 4 occurrences. */
    cbm_cypher_result_t r2 = {0};
    rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN count(f.label)", "test", 0, &r2);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(r2.rows[0][0], "4");
    cbm_cypher_result_free(&r2);

    /* DISTINCT over the 4 unique function names = 4. */
    cbm_cypher_result_t r3 = {0};
    rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN count(DISTINCT f.name)", "test", 0, &r3);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(r3.rows[0][0], "4");
    cbm_cypher_result_free(&r3);

    cbm_store_close(s);
    PASS();
}

/* issue #373: an unsupported computed expression in WITH/RETURN (an unknown
 * function like split(...) or list indexing [..]) must FAIL LOUDLY with a clear
 * "unsupported function" error rather than silently projecting an empty column
 * (which looks like a valid-but-blank result and hides the real problem). */
TEST(cypher_exec_unsupported_func_errors_issue373) {
    cbm_store_t *s = setup_cypher_store();

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WITH split(f.name)[0] AS top, count(*) AS c RETURN top, c", "test",
        0, &r);
    ASSERT_TRUE(rc != 0); /* unsupported function now fails loudly */
    ASSERT_NOT_NULL(r.error);
    ASSERT_TRUE(strstr(r.error, "unsupported") != NULL);
    ASSERT_TRUE(strstr(r.error, "split") != NULL);
    cbm_cypher_result_free(&r);

    cbm_store_close(s);
    PASS();
}

/* A recognised function still works, and an unknown one in plain RETURN errors. */
TEST(cypher_exec_unknown_func_return_errors) {
    cbm_store_t *s = setup_cypher_store();

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN nosuchfunc(f.name)", "test", 0, &r);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(r.error);
    ASSERT_TRUE(strstr(r.error, "unsupported function") != NULL);
    cbm_cypher_result_free(&r);

    cbm_store_close(s);
    PASS();
}

/* issue #242: openCypher label alternation in MATCH — (n:A|B). */
TEST(cypher_exec_label_alternation_issue242) {
    cbm_store_t *s = setup_cypher_store();

    /* Store has 4 Function + 1 Module node → alternation seeds all 5. */
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n:Function|Module) RETURN n.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 5);
    cbm_cypher_result_free(&r);

    /* Alternation with a non-existent label still returns the existing one. */
    cbm_cypher_result_t r2 = {0};
    rc = cbm_cypher_execute(s, "MATCH (n:Function|Class) RETURN n.name", "test", 0, &r2);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r2.row_count, 4);
    cbm_cypher_result_free(&r2);

    cbm_store_close(s);
    PASS();
}

/* --- Ported from cypher_test.go: TestExecuteInlinePropertyFilter --- */
TEST(cypher_exec_inline_props) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function {name: \"SubmitOrder\"}) "
                                "RETURN f.name, f.qualified_name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* --- Ported from cypher_test.go: TestParseWhereStartsWith --- */
TEST(cypher_parse_where_starts_with) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (f:Function) WHERE f.name STARTS WITH \"Send\" RETURN f", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);
    ASSERT_NOT_NULL(q->where);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_CONDITION);
    ASSERT_STR_EQ(q->where->root->cond.op, "STARTS WITH");
    ASSERT_STR_EQ(q->where->root->cond.value, "Send");
    cbm_query_free(q);
    PASS();
}

/* --- Ported from cypher_test.go: TestParseWhereContains --- */
TEST(cypher_parse_where_contains) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (f:Function) WHERE f.name CONTAINS \"Handler\" RETURN f", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);
    ASSERT_NOT_NULL(q->where);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_CONDITION);
    ASSERT_STR_EQ(q->where->root->cond.op, "CONTAINS");
    ASSERT_STR_EQ(q->where->root->cond.value, "Handler");
    cbm_query_free(q);
    PASS();
}

/* --- Ported from cypher_test.go: TestParseWhereNumericComparison --- */
TEST(cypher_parse_where_numeric) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) WHERE f.start_line > 10 RETURN f", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q);
    ASSERT_NOT_NULL(q->where);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_CONDITION);
    ASSERT_STR_EQ(q->where->root->cond.op, ">");
    ASSERT_STR_EQ(q->where->root->cond.value, "10");
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  EDGE PROPERTY TESTS (ported from cypher_test.go Feature 2)
 * ══════════════════════════════════════════════════════════════════ */

/* Helper: set up store with HTTP_CALLS edge having properties.
 * Creates same graph as setup_cypher_store + one HTTP_CALLS edge. */
static cbm_store_t *setup_cypher_http_store(void) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "HandleOrder",
                     .qualified_name = "test.main.HandleOrder",
                     .file_path = "main.go",
                     .start_line = 10,
                     .end_line = 30};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "ValidateOrder",
                     .qualified_name = "test.service.ValidateOrder",
                     .file_path = "service.go",
                     .start_line = 5,
                     .end_line = 20};
    cbm_node_t n3 = {.project = "test",
                     .label = "Function",
                     .name = "SubmitOrder",
                     .qualified_name = "test.service.SubmitOrder",
                     .file_path = "service.go",
                     .start_line = 25,
                     .end_line = 50};

    int64_t id1 = cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    int64_t id3 = cbm_store_upsert_node(s, &n3);

    cbm_edge_t http = {
        .project = "test",
        .source_id = id1,
        .target_id = id3,
        .type = "HTTP_CALLS",
        .properties_json =
            "{\"url_path\":\"/api/orders\",\"confidence\":0.85,\"method\":\"POST\"}"};
    cbm_store_insert_edge(s, &http);

    return s;
}

/* Helper: set up store with TWO HTTP_CALLS edges for filtering tests. */
static cbm_store_t *setup_cypher_multi_edge_store(void) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "testproj", "/tmp/test");

    cbm_node_t n1 = {.project = "testproj",
                     .label = "Function",
                     .name = "SendOrder",
                     .qualified_name = "testproj.caller.SendOrder",
                     .file_path = "caller/client.go"};
    cbm_node_t n2 = {.project = "testproj",
                     .label = "Function",
                     .name = "HandleOrder",
                     .qualified_name = "testproj.handler.HandleOrder",
                     .file_path = "handler/routes.go"};
    cbm_node_t n3 = {.project = "testproj",
                     .label = "Function",
                     .name = "HandleHealth",
                     .qualified_name = "testproj.handler.HandleHealth",
                     .file_path = "handler/health.go"};

    int64_t id1 = cbm_store_upsert_node(s, &n1);
    int64_t id2 = cbm_store_upsert_node(s, &n2);
    int64_t id3 = cbm_store_upsert_node(s, &n3);

    cbm_edge_t e1 = {.project = "testproj",
                     .source_id = id1,
                     .target_id = id2,
                     .type = "HTTP_CALLS",
                     .properties_json =
                         "{\"url_path\":\"/api/orders\",\"confidence\":0.85,\"method\":\"POST\"}"};
    cbm_edge_t e2 = {.project = "testproj",
                     .source_id = id1,
                     .target_id = id3,
                     .type = "HTTP_CALLS",
                     .properties_json = "{\"url_path\":\"/health\",\"confidence\":0.45}"};
    cbm_store_insert_edge(s, &e1);
    cbm_store_insert_edge(s, &e2);

    return s;
}

/* Helper: find a column value in a cypher result row */
static const char *cypher_get_col(const cbm_cypher_result_t *r, int row, const char *col) {
    for (int c = 0; c < r->col_count; c++) {
        if (strcmp(r->columns[c], col) == 0)
            return r->rows[row][c];
    }
    return NULL;
}

/* Helper: check if any row has a column matching a value */
static bool cypher_has_row_with(const cbm_cypher_result_t *r, const char *col, const char *val) {
    int ci = -1;
    for (int c = 0; c < r->col_count; c++) {
        if (strcmp(r->columns[c], col) == 0) {
            ci = c;
            break;
        }
    }
    if (ci < 0)
        return false;
    for (int row = 0; row < r->row_count; row++) {
        if (strcmp(r->rows[row][ci], val) == 0)
            return true;
    }
    return false;
}

TEST(cypher_edge_prop_access) {
    cbm_store_t *s = setup_cypher_http_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a:Function)-[r:HTTP_CALLS]->(b:Function) "
                                "RETURN a.name, b.name, r.url_path, r.confidence",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "a.name"), "HandleOrder");
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "b.name"), "SubmitOrder");
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "r.url_path"), "/api/orders");
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "r.confidence"), "0.85");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

typedef struct {
    atomic_int *ready;
    atomic_int *start;
    bool succeeded;
} cypher_edge_thread_ctx_t;

static void *cypher_edge_props_concurrently(void *opaque) {
    cypher_edge_thread_ctx_t *ctx = opaque;
    cbm_store_t *store = setup_cypher_http_store();
    if (!store) {
        return NULL;
    }

    /* Keep projection busy after the store scan has completed.  A single-edge
     * query can be incidentally ordered by SQLite's internal mutexes, masking
     * the independent Cypher scratch-buffer race from TSan. */
    cbm_node_t source = {.project = "test",
                         .label = "Function",
                         .name = "HandleOrder",
                         .qualified_name = "test.main.HandleOrder",
                         .file_path = "main.go",
                         .start_line = 10,
                         .end_line = 30};
    int64_t source_id = cbm_store_upsert_node(store, &source);
    for (int i = 0; i < 256; i++) {
        char name[64];
        char qualified_name[96];
        snprintf(name, sizeof(name), "ConcurrentTarget%d", i);
        snprintf(qualified_name, sizeof(qualified_name), "test.concurrent.%s", name);
        cbm_node_t target = {.project = "test",
                             .label = "Function",
                             .name = name,
                             .qualified_name = qualified_name,
                             .file_path = "concurrent.go"};
        int64_t target_id = cbm_store_upsert_node(store, &target);
        cbm_edge_t edge = {
            .project = "test",
            .source_id = source_id,
            .target_id = target_id,
            .type = "HTTP_CALLS",
            .properties_json =
                "{\"url_path\":\"/api/orders\",\"confidence\":0.85,\"method\":\"POST\"}"};
        if (source_id < 0 || target_id < 0 || cbm_store_insert_edge(store, &edge) < 0) {
            cbm_store_close(store);
            return NULL;
        }
    }

    atomic_fetch_add_explicit(ctx->ready, 1, memory_order_release);
    while (atomic_load_explicit(ctx->start, memory_order_acquire) == 0) {
        cbm_usleep(1000);
    }
    ctx->succeeded = true;
    for (int i = 0; i < 128; i++) {
        cbm_cypher_result_t result = {0};
        int rc = cbm_cypher_execute(store,
                                    "MATCH (a:Function)-[r:HTTP_CALLS]->(b:Function) "
                                    "RETURN r.url_path, r.confidence, r.method",
                                    "test", 0, &result);
        if (rc != 0 || result.row_count != 257 ||
            strcmp(cypher_get_col(&result, 0, "r.url_path"), "/api/orders") != 0 ||
            strcmp(cypher_get_col(&result, 0, "r.confidence"), "0.85") != 0 ||
            strcmp(cypher_get_col(&result, 0, "r.method"), "POST") != 0) {
            ctx->succeeded = false;
        }
        cbm_cypher_result_free(&result);
        if (!ctx->succeeded) {
            break;
        }
    }
    cbm_store_close(store);
    return NULL;
}

/* Daemon sessions execute independent graph queries concurrently. TSan must
 * see no shared rotating edge-property scratch buffer between those threads. */
TEST(cypher_edge_prop_storage_is_per_thread) {
    atomic_int ready;
    atomic_int start;
    atomic_init(&ready, 0);
    atomic_init(&start, 0);
    cypher_edge_thread_ctx_t ctx[2] = {
        {.ready = &ready, .start = &start},
        {.ready = &ready, .start = &start},
    };
    cbm_thread_t threads[2];
    bool started0 = cbm_thread_create(&threads[0], 0, cypher_edge_props_concurrently, &ctx[0]) == 0;
    bool started1 = cbm_thread_create(&threads[1], 0, cypher_edge_props_concurrently, &ctx[1]) == 0;
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
    ASSERT_TRUE(ctx[0].succeeded);
    ASSERT_TRUE(ctx[1].succeeded);
    PASS();
}

TEST(cypher_edge_prop_in_where) {
    cbm_store_t *s = setup_cypher_http_store();
    cbm_cypher_result_t r = {0};

    /* confidence > 0.8 → should match */
    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.confidence > 0.8 "
                                "RETURN a.name, b.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);

    /* confidence > 0.9 → should NOT match */
    memset(&r, 0, sizeof(r));
    rc = cbm_cypher_execute(s,
                            "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.confidence > 0.9 "
                            "RETURN a.name",
                            "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 0);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_type_prop) {
    cbm_store_t *s = setup_cypher_http_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s, "MATCH (a)-[r:HTTP_CALLS]->(b) RETURN r.type", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "r.type"), "HTTP_CALLS");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_filter_contains) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.url_path CONTAINS 'orders' "
                                "RETURN a.name, b.name, r.url_path",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "a.name"), "SendOrder");
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "b.name"), "HandleOrder");
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "r.url_path"), "/api/orders");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_filter_numeric_gte) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.confidence >= 0.6 "
                                "RETURN a.name, b.name, r.confidence LIMIT 20",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "b.name"), "HandleOrder");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_bare_edge_return_exposes_properties_json) {
    /* `RETURN r` on an edge variable, with no property accessor, should
     * surface the edge's full properties JSON (or "{}"). Before the fix,
     * binding_get_virtual returned an empty string, which made bare edge
     * returns useless for callers that wanted to inspect timestamps,
     * weights, etc. without naming each property up front. */
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s, "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.method = 'POST' RETURN r",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    const char *r_val = cypher_get_col(&r, 0, "r");
    ASSERT_NOT_NULL(r_val);
    /* Expect JSON object content rather than the previous empty string. */
    ASSERT_NOT_NULL(strstr(r_val, "url_path"));
    ASSERT_NOT_NULL(strstr(r_val, "/api/orders"));

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_return_without_filter) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) "
                                "RETURN a.name, b.name, r.url_path, r.confidence LIMIT 20",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.row_count, 2);
    ASSERT(cypher_has_row_with(&r, "r.url_path", "/api/orders"));
    ASSERT(cypher_has_row_with(&r, "r.url_path", "/health"));

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_filter_equals) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.method = 'POST' "
                                "RETURN a.name, b.name",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "b.name"), "HandleOrder");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_filter_starts_with) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.url_path STARTS WITH '/api' "
                                "RETURN a.name, b.name, r.url_path",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "r.url_path"), "/api/orders");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_combined_node_and_edge_filter) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a:Function)-[r:HTTP_CALLS]->(b:Function) "
                                "WHERE a.name = 'SendOrder' AND r.confidence >= 0.6 "
                                "RETURN b.name, r.url_path",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "b.name"), "HandleOrder");
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "r.url_path"), "/api/orders");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_filter_no_match) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    /* No edge has method = 'DELETE' */
    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.method = 'DELETE' "
                                "RETURN a.name",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 0);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_filter_numeric_lt) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    /* Only health edge (0.45) should match confidence < 0.5 */
    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.confidence < 0.5 "
                                "RETURN b.name, r.confidence",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "b.name"), "HandleHealth");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_filter_regex) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r:HTTP_CALLS]->(b) WHERE r.url_path =~ \"/api/.*\" "
                                "RETURN b.name",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(cypher_get_col(&r, 0, "b.name"), "HandleOrder");

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_edge_builtin_type_filter) {
    cbm_store_t *s = setup_cypher_multi_edge_store();
    cbm_cypher_result_t r = {0};

    /* Untyped rel [r] — filter on r.type in WHERE */
    int rc = cbm_cypher_execute(s,
                                "MATCH (a)-[r]->(b) WHERE r.type = 'HTTP_CALLS' "
                                "RETURN a.name, b.name LIMIT 20",
                                "testproj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2); /* Both HTTP_CALLS edges */

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Ported from cypher_test.go: TestApplyLimitRespectsExplicit */
TEST(cypher_apply_limit) {
    /* Create store with many nodes */
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "lim", "/tmp/lim");

    for (int i = 0; i < 50; i++) {
        char name[32], qn[64];
        snprintf(name, sizeof(name), "func%d", i);
        snprintf(qn, sizeof(qn), "lim.func%d", i);
        cbm_node_t n = {.project = "lim",
                        .label = "Function",
                        .name = name,
                        .qualified_name = qn,
                        .file_path = "test.go"};
        cbm_store_upsert_node(s, &n);
    }

    /* LIMIT 5 → 5 rows */
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name LIMIT 5", "lim", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 5);
    cbm_cypher_result_free(&r);

    /* No LIMIT, max_rows=10 → capped at 10 */
    memset(&r, 0, sizeof(r));
    rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name", "lim", 10, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 10);
    cbm_cypher_result_free(&r);

    /* LIMIT above max_rows → explicit limit wins */
    memset(&r, 0, sizeof(r));
    rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name LIMIT 30", "lim", 10, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 30);
    cbm_cypher_result_free(&r);

    cbm_store_close(s);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 1: SIMPLE OPERATORS
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_lex_neq_operators) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("<> !=", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.count, 2);
    ASSERT_EQ(r.tokens[0].type, TOK_NEQ);
    ASSERT_EQ(r.tokens[1].type, TOK_NEQ);
    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_ends_keyword) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("ENDS WITH", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.count, 2);
    ASSERT_EQ(r.tokens[0].type, TOK_ENDS);
    ASSERT_EQ(r.tokens[1].type, TOK_WITH);
    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_lex_in_is_null) {
    cbm_lex_result_t r = {0};
    int rc = cbm_lex("IN IS NULL", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.count, 3);
    ASSERT_EQ(r.tokens[0].type, TOK_IN);
    ASSERT_EQ(r.tokens[1].type, TOK_IS);
    ASSERT_EQ(r.tokens[2].type, TOK_NULL_KW);
    cbm_lex_free(&r);
    PASS();
}

TEST(cypher_exec_where_neq) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name <> \"HandleOrder\" RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 3); /* ValidateOrder, SubmitOrder, LogError */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_neq_bang) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name != \"HandleOrder\" RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 3);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_ends_with) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name ENDS WITH \"Order\" RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* HandleOrder, ValidateOrder, SubmitOrder */
    ASSERT_EQ(r.row_count, 3);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_not) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE NOT f.name = \"HandleOrder\" RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 3);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_in) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f) WHERE f.label IN [\"Function\", \"Module\"] RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 5); /* 4 Functions + 1 Module */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_not_in) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f) WHERE NOT f.label IN [\"Module\"] RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 4); /* 4 Functions only */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_is_null) {
    /* SubmitOrder has no start_line (defaults to 0, so start_line prop = "0") */
    /* But file_path is set for all. Use a node with missing data. */
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "WithFile",
                     .qualified_name = "test.WithFile",
                     .file_path = "a.go"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "NoFile",
                     .qualified_name = "test.NoFile",
                     .file_path = NULL};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.file_path IS NULL RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1); /* NoFile has NULL file_path */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_is_not_null) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "WithFile",
                     .qualified_name = "test.WithFile",
                     .file_path = "a.go"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "NoFile",
                     .qualified_name = "test.NoFile",
                     .file_path = NULL};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) WHERE f.file_path IS NOT NULL RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1); /* WithFile */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_return_star) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN * LIMIT 3", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 3);
    /* Should have columns: f.name, f.qualified_name, f.label, f.file_path */
    ASSERT_EQ(r.col_count, 4);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_parse_neq) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) WHERE f.name <> \"X\"", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->where);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_CONDITION);
    ASSERT_STR_EQ(q->where->root->cond.op, "<>");
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_in) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f) WHERE f.label IN [\"Function\", \"Module\"]", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_CONDITION);
    ASSERT_STR_EQ(q->where->root->cond.op, "IN");
    ASSERT_EQ(q->where->root->cond.in_value_count, 2);
    ASSERT_STR_EQ(q->where->root->cond.in_values[0], "Function");
    ASSERT_STR_EQ(q->where->root->cond.in_values[1], "Module");
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_is_null) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f) WHERE f.file_path IS NULL", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_STR_EQ(q->where->root->cond.op, "IS NULL");
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 2: EXPRESSION TREE
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_exec_where_or) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s,
        "MATCH (f:Function) WHERE f.name = \"HandleOrder\" OR f.name = \"LogError\" RETURN f.name",
        "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_complex_bool) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    /* (name CONTAINS "Order" OR name = "LogError") AND label = "Function" */
    int rc = cbm_cypher_execute(s,
                                "MATCH (f) WHERE (f.name CONTAINS \"Order\" OR f.name = "
                                "\"LogError\") AND f.label = \"Function\" "
                                "RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 4); /* HandleOrder, ValidateOrder, SubmitOrder, LogError */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_xor) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    /* name CONTAINS "Handle" XOR name CONTAINS "Order" → XOR = true when exactly one is true
     * HandleOrder: both true → false
     * ValidateOrder: false, true → true
     * SubmitOrder: false, true → true
     * LogError: false, false → false */
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name CONTAINS \"Handle\" XOR f.name "
                                "CONTAINS \"Order\" RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2); /* ValidateOrder, SubmitOrder */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_where_not_prefix) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE NOT (f.name CONTAINS \"Order\") RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1); /* LogError */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_parse_expr_tree_and_or) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (f) WHERE f.a = \"1\" AND f.b = \"2\" OR f.c = \"3\"", &q, &err);
    ASSERT_EQ(rc, 0);
    /* Precedence: AND binds tighter than OR → root is OR */
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_OR);
    ASSERT_EQ(q->where->root->left->type, EXPR_AND);
    ASSERT_EQ(q->where->root->right->type, EXPR_CONDITION);
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_expr_tree_nested) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (f) WHERE (f.a = \"1\" OR f.b = \"2\") AND f.c = \"3\"", &q, &err);
    ASSERT_EQ(rc, 0);
    /* Parens override precedence: root is AND, left is OR */
    ASSERT_NOT_NULL(q->where->root);
    ASSERT_EQ(q->where->root->type, EXPR_AND);
    ASSERT_EQ(q->where->root->left->type, EXPR_OR);
    ASSERT_EQ(q->where->root->right->type, EXPR_CONDITION);
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 3: UNSUPPORTED KEYWORD ERRORS
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_error_create) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("CREATE (n:Node {name: \"X\"})", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    ASSERT(strstr(err, "CREATE") != NULL);
    free(err);
    PASS();
}

TEST(cypher_error_delete) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("DELETE n", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    ASSERT(strstr(err, "DELETE") != NULL);
    free(err);
    PASS();
}

TEST(cypher_error_set) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("SET n.name = \"X\"", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    ASSERT(strstr(err, "SET") != NULL);
    free(err);
    PASS();
}

TEST(cypher_error_merge) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MERGE (n:Node)", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    ASSERT(strstr(err, "MERGE") != NULL);
    free(err);
    PASS();
}

TEST(cypher_error_call) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("CALL db.labels()", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    ASSERT(strstr(err, "CALL") != NULL);
    free(err);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 4: SKIP + GENERALIZED AGGREGATION
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_exec_skip) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name ORDER BY f.name ASC SKIP 2",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* 4 functions ordered: HandleOrder, LogError, SubmitOrder, ValidateOrder → skip 2 = 2 */
    ASSERT_EQ(r.row_count, 2);
    ASSERT_STR_EQ(r.rows[0][0], "SubmitOrder");
    ASSERT_STR_EQ(r.rows[1][0], "ValidateOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_skip_limit) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) RETURN f.name ORDER BY f.name ASC SKIP 1 LIMIT 2", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2);
    ASSERT_STR_EQ(r.rows[0][0], "LogError");
    ASSERT_STR_EQ(r.rows[1][0], "SubmitOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_sum) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    /* start_lines: HandleOrder=10, ValidateOrder=5, SubmitOrder=0, LogError=0 → sum=15 */
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN SUM(f.start_line) AS total", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "15");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_avg) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    /* start_lines: 10, 5, 0, 0 → avg = 3.75 */
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN AVG(f.start_line) AS avg_line",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "3.75");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_min) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    /* Among functions with nonzero: HandleOrder=10, ValidateOrder=5 → but MIN is 0 from others */
    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) RETURN MIN(f.start_line) AS mn", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "0");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_max) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc =
        cbm_cypher_execute(s, "MATCH (f:Function) RETURN MAX(f.start_line) AS mx", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "10");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_collect) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "WHERE f.name = \"HandleOrder\" "
                                "RETURN f.name, COLLECT(g.name) AS callees",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    /* Should be a JSON array like ["ValidateOrder","LogError"] */
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    ASSERT(strstr(r.rows[0][1], "ValidateOrder") != NULL);
    ASSERT(strstr(r.rows[0][1], "LogError") != NULL);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_count_star) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN COUNT(*) AS n", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "4");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_parse_skip) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f) RETURN f.name SKIP 5 LIMIT 10", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->ret);
    ASSERT_EQ(q->ret->skip, 5);
    ASSERT_EQ(q->ret->limit, 10);
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_sum_avg) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f) RETURN SUM(f.x) AS s, AVG(f.y) AS a", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(q->ret->count, 2);
    ASSERT_STR_EQ(q->ret->items[0].func, "SUM");
    ASSERT_STR_EQ(q->ret->items[0].alias, "s");
    ASSERT_STR_EQ(q->ret->items[1].func, "AVG");
    ASSERT_STR_EQ(q->ret->items[1].alias, "a");
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_collect) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f)-[:CALLS]->(g) RETURN f.name, COLLECT(g.name) AS names", &q,
                              &err);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(q->ret->count, 2);
    ASSERT_STR_EQ(q->ret->items[1].func, "COLLECT");
    ASSERT_STR_EQ(q->ret->items[1].alias, "names");
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 5: STRING FUNCTIONS + CASE
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_exec_tolower) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN toLower(f.name) AS lower_name",
        "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "handleorder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_toupper) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN toUpper(f.name) AS upper_name",
        "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "HANDLEORDER");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_tostring) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN toString(f.start_line) AS sl",
        "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "10");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_case) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s,
        "MATCH (f:Function) WHERE f.name = \"HandleOrder\" "
        "RETURN CASE WHEN f.start_line > \"5\" THEN \"high\" ELSE \"low\" END AS pos",
        "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "high");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_parse_tolower) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f) RETURN toLower(f.name) AS n", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(q->ret->items[0].func, "toLower");
    ASSERT_STR_EQ(q->ret->items[0].alias, "n");
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_case) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse(
        "MATCH (f) RETURN CASE WHEN f.x = \"1\" THEN \"a\" ELSE \"b\" END AS val", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->ret->items[0].kase);
    ASSERT_EQ(q->ret->items[0].kase->branch_count, 1);
    ASSERT_STR_EQ(q->ret->items[0].kase->branches[0].then_val, "a");
    ASSERT_STR_EQ(q->ret->items[0].kase->else_val, "b");
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 6: WITH CLAUSE
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_exec_with_rename) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" "
                                "WITH f.name AS fname RETURN fname",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_with_count) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "WITH f.name AS caller, COUNT(g) AS cnt "
                                "RETURN caller, cnt ORDER BY cnt DESC",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_GTE(r.row_count, 1);
    /* HandleOrder calls 2 (ValidateOrder, LogError), ValidateOrder calls 1 (SubmitOrder) */
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    ASSERT_STR_EQ(r.rows[0][1], "2");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Regression: a bare node group-var carried through WITH aggregation must project
 * its real properties (not blank). Pre-fix, the carried var held only the node
 * name, so RETURN g.file_path returned "". */
TEST(cypher_exec_with_node_groupvar_prop) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "WHERE g.name = \"ValidateOrder\" "
                                "WITH g, COUNT(*) AS c "
                                "RETURN g.file_path, g.name, c",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "validate.go"); /* was "" before the fix */
    ASSERT_STR_EQ(r.rows[0][1], "ValidateOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_with_where) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "WITH f.name AS caller, COUNT(g) AS cnt "
                                "WHERE cnt > \"1\" "
                                "RETURN caller, cnt",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* Only HandleOrder has cnt > 1 (cnt=2) */
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_with_orderby_limit) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "WITH f.name AS caller, COUNT(g) AS cnt "
                                "ORDER BY cnt DESC LIMIT 1 "
                                "RETURN caller, cnt",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "HandleOrder");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_parse_with) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse(
        "MATCH (f)-[:CALLS]->(g) WITH f.name AS caller, COUNT(g) AS cnt RETURN caller, cnt", &q,
        &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->with_clause);
    ASSERT_EQ(q->with_clause->count, 2);
    ASSERT_STR_EQ(q->with_clause->items[0].alias, "caller");
    ASSERT_STR_EQ(q->with_clause->items[1].func, "COUNT");
    ASSERT_NOT_NULL(q->ret);
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_with_where) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f)-[:CALLS]->(g) WITH f.name AS caller, COUNT(g) AS cnt "
                              "WHERE cnt > \"1\" RETURN caller",
                              &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->with_clause);
    ASSERT_NOT_NULL(q->post_with_where);
    ASSERT_NOT_NULL(q->post_with_where->root);
    ASSERT_NOT_NULL(q->ret);
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 7: OPTIONAL MATCH + MULTIPLE MATCH
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_exec_optional_match_no_result) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    /* LogError has no CALLS outbound edges → OPTIONAL MATCH keeps binding with empty target */
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"LogError\" "
                                "OPTIONAL MATCH (f)-[:CALLS]->(g:Function) "
                                "RETURN f.name, g.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "LogError");
    /* g.name should be empty since OPTIONAL MATCH found nothing */
    ASSERT_STR_EQ(r.rows[0][1], "");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_optional_match_has_result) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" "
                                "OPTIONAL MATCH (f)-[:CALLS]->(g:Function) "
                                "RETURN f.name, g.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2); /* ValidateOrder, LogError */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_multi_match) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    /* Two MATCH clauses: first finds a module, second finds functions */
    int rc =
        cbm_cypher_execute(s,
                           "MATCH (m:Module) MATCH (f:Function) WHERE f.name CONTAINS \"Order\" "
                           "RETURN m.name, f.name",
                           "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* 1 module × 3 *Order functions = 3 */
    ASSERT_EQ(r.row_count, 3);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* ──────────────────────────────────────────────────────────────────
 * Regression: node-only cross-join must not overflow / exhaust memory.
 *
 * cross_join_nodes() (src/cypher/cypher.c) sizes its result buffer from
 * `*bind_count * extra_count`. Before the fix this product was computed in
 * `int`: a two-pattern node-only match over a large graph (e.g.
 * `MATCH (a) MATCH (b) RETURN a.name LIMIT 1`) makes both factors the full
 * node count, and their product overflows signed 32-bit `int`, wrapping to a
 * garbage malloc size → tiny/failed allocation → heap out-of-bounds write, or
 * (for a non-overflowing but huge product) a multi-hundred-GB allocation the
 * fill loop then commits → OOM kill. The sibling cross_join_with_rels() was
 * hardened for this class under #627; the node-only path was left unsafe.
 *
 * We drive the exact node-only two-pattern shape over 46341 nodes. With
 * 46341 * 46341 = 2,147,488,281 > INT_MAX (2,147,483,647), the pre-fix `int`
 * product overflows. Crucially this does NOT commit hundreds of GB: the
 * wrapped size makes malloc fail/abort under the ASan+UBSan test build, so the
 * defect surfaces as a child-process crash — not a live OOM of the test host.
 *
 * POSIX: run the query in a forked child (same crash-isolation idiom as
 * tests/repro/repro_issue627.c). The child exits 0 only if the query both
 * succeeds AND returns the single LIMIT-1 row; the parent asserts the child
 * exited cleanly. Pre-fix the child is killed by a signal (overflow →
 * bad-size malloc / heap OOB) → RED. Post-fix the size_t product is clamped to
 * the executor's result ceiling, so the query returns 1 row → GREEN.
 * ────────────────────────────────────────────────────────────────── */
TEST(cypher_exec_cross_join_nodes_no_overflow) {
    cbm_store_t *s = cbm_store_open_memory();
    ASSERT_NOT_NULL(s);
    cbm_store_upsert_project(s, "xj", "/tmp/xj");

    /* 46341 nodes: 46341^2 = 2,147,488,281 > INT_MAX. This is the smallest
     * equal-factor node count whose self cross-product overflows 32-bit int. */
    const int node_count = 46341;
    cbm_store_begin(s);
    for (int i = 0; i < node_count; i++) {
        char name[32], qn[48];
        snprintf(name, sizeof(name), "x%d", i);
        snprintf(qn, sizeof(qn), "xj.x%d", i);
        cbm_node_t n = {.project = "xj",
                        .label = "XJ",
                        .name = name,
                        .qualified_name = qn,
                        .file_path = "xj.c"};
        cbm_store_upsert_node(s, &n);
    }
    cbm_store_commit(s);

    /* Two node-only patterns → cross_join_nodes(bind_count=46341,
     * extra_count=46341). LIMIT 1 keeps the projected result tiny; the danger
     * is entirely in the intermediate cross-join buffer sizing. */
    const char *query = "MATCH (a:XJ) MATCH (b:XJ) RETURN a.name LIMIT 1";

#if !defined(_WIN32)
    /* Crash-isolate the query: an overflow-driven bad malloc / heap OOB kills
     * only the child, which the parent then observes via wait status. */
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        cbm_cypher_result_t cr = {0};
        int crc = cbm_cypher_execute(s, query, "xj", 0, &cr);
        int ok = (crc == 0 && cr.row_count == 1);
        cbm_cypher_result_free(&cr);
        _exit(ok ? 0 : 2);
    }
    int st = 0;
    (void)waitpid(pid, &st, 0);

    /* Pre-fix: WIFSIGNALED(st) — child killed by SIGABRT/SIGSEGV from the
     * overflowed allocation size. Post-fix: clean exit 0. */
    ASSERT_TRUE(WIFEXITED(st));
    ASSERT_EQ(WEXITSTATUS(st), 0);
#else
    /* No fork on Windows: run in-process. On pre-fix code this aborts the
     * runner (the bug's presence is itself the failure signal); post-fix it
     * returns the single row. */
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, query, "xj", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);
#endif

    cbm_store_close(s);
    PASS();
}

TEST(cypher_parse_optional_match) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse(
        "MATCH (f:Function) OPTIONAL MATCH (f)-[:CALLS]->(g) RETURN f.name, g.name", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(q->pattern_count, 2);
    ASSERT(!q->pattern_optional[0]);
    ASSERT(q->pattern_optional[1]);
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_multi_match) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (a:Module) MATCH (b:Function) RETURN a.name, b.name", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(q->pattern_count, 2);
    ASSERT(!q->pattern_optional[0]);
    ASSERT(!q->pattern_optional[1]);
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 8: UNION / UNION ALL
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_exec_union) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN f.name "
                                "UNION "
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* UNION deduplicates → 1 row */
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_exec_union_all) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN f.name "
                                "UNION ALL "
                                "MATCH (f:Function) WHERE f.name = \"HandleOrder\" RETURN f.name",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    /* UNION ALL keeps duplicates → 2 rows */
    ASSERT_EQ(r.row_count, 2);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_parse_union) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("MATCH (f) RETURN f.name UNION ALL MATCH (g) RETURN g.name", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT(q->union_all);
    ASSERT_NOT_NULL(q->union_next);
    cbm_query_free(q);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE 9: UNWIND
 * ══════════════════════════════════════════════════════════════════ */

TEST(cypher_parse_unwind) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc =
        cbm_cypher_parse("UNWIND [\"a\", \"b\", \"c\"] AS x MATCH (f) RETURN f.name", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(q->unwind_expr);
    ASSERT_STR_EQ(q->unwind_alias, "x");
    cbm_query_free(q);
    PASS();
}

TEST(cypher_parse_unwind_var) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("UNWIND items AS item MATCH (f) RETURN f.name", &q, &err);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(q->unwind_expr, "items");
    ASSERT_STR_EQ(q->unwind_alias, "item");
    cbm_query_free(q);
    PASS();
}

/* Regression: an UNWIND literal list whose JSON encoding far exceeds the
 * parser's fixed 2KB scratch buffer must not overflow it. Before the fix,
 * parse_unwind_clause grew its running length `blen` by snprintf's *intended*
 * return value with no clamp, so once `blen` passed the buffer size the
 * `sizeof(buf) - blen` size argument underflowed and `buf[blen++] = ']'` wrote
 * past the 2KB stack buffer -> ASan stack-buffer-overflow / SIGSEGV.
 *
 * Machine-safe: the parse runs in a forked child so a crash on unfixed code is
 * observed as a killed child, not a killed test runner. */
TEST(cypher_unwind_long_list_bounded) {
    char q[CBM_SZ_8K];
    size_t off = 0;
    int n = snprintf(q + off, sizeof(q) - off, "UNWIND [");
    ASSERT_GT(n, 0);
    off += (size_t)n;
    /* 120 forty-char string elements -> ~5KB of JSON, far past the 2KB buf. */
    for (int i = 0; i < 120; i++) {
        n = snprintf(q + off, sizeof(q) - off, "%s\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"",
                     i ? "," : "");
        ASSERT_GT(n, 0);
        off += (size_t)n;
    }
    n = snprintf(q + off, sizeof(q) - off, "] AS x MATCH (nn) RETURN nn.name");
    ASSERT_GT(n, 0);
    off += (size_t)n;
    ASSERT_LT(off, sizeof(q)); /* query fully built, not itself truncated */

#if defined(_WIN32)
    /* No fork on Windows: run inline — the fix must make this safe. */
    cbm_query_t *parsed = NULL;
    char *perr = NULL;
    int rc = cbm_cypher_parse(q, &parsed, &perr);
    (void)rc;
    if (parsed) {
        cbm_query_free(parsed);
    }
    free(perr);
    PASS();
#else
    fflush(NULL);
    pid_t pid = fork();
    ASSERT_GTE(pid, 0);
    if (pid == 0) {
        cbm_query_t *parsed = NULL;
        char *perr = NULL;
        int rc = cbm_cypher_parse(q, &parsed, &perr);
        (void)rc;
        if (parsed) {
            cbm_query_free(parsed);
        }
        free(perr);
        _exit(0);
    }
    int status = 0;
    (void)waitpid(pid, &status, 0);
    /* RED on unfixed code: child dies mid-parse (ASan abort or SIGSEGV) — either
     * a non-zero exit or a signal. GREEN: child parsed safely and exited 0. */
    ASSERT(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    PASS();
#endif
}

/* Regression: a multi-key ORDER BY must still honor a trailing LIMIT. Before the
 * fix, parse_order_by_clause parsed only the FIRST sort key, so the remaining
 * ", f.file_path LIMIT 2" was left unconsumed, the LIMIT was silently dropped
 * (r->limit stayed at the -1 sentinel) and the full unbounded result set was
 * materialized. The observable contract: exactly LIMIT rows come back. */
TEST(cypher_order_by_multikey_honors_limit) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    /* Ground truth: with no LIMIT the fixture yields all 4 Function rows. */
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 4);
    cbm_cypher_result_free(&r);

    /* Two sort keys, then LIMIT 2 — must return exactly 2 rows, not all 4. */
    memset(&r, 0, sizeof(r));
    rc = cbm_cypher_execute(
        s, "MATCH (f:Function) RETURN f.name ORDER BY f.name, f.file_path LIMIT 2", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 2); /* RED on unfixed code: returns 4 */

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Regression: the parser must not silently drop trailing tokens it cannot place.
 * `LIMIT 2 SKIP 1` is not valid grammar (SKIP must precede LIMIT); before the
 * fix cbm_parse never asserted end-of-input, so the trailing `SKIP 1` vanished
 * and the query parsed as success. It must now surface as an error instead. */
TEST(cypher_parse_trailing_tokens_rejected) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) RETURN f.name LIMIT 2 SKIP 1", &q, &err);
    ASSERT_EQ(rc, -1); /* RED on unfixed code: rc == 0, trailing SKIP dropped */
    ASSERT_NOT_NULL(err);
    cbm_query_free(q);
    free(err);
    PASS();
}

/* ── Issue #389 group: Cypher feature reproductions ─────────────────
 * Each asserts the CORRECT behavior; a failure reproduces the bug. */

/* #240: labels() function */
TEST(cypher_issue240_labels_function) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n:Module) RETURN labels(n) AS lbl", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #237: DISTINCT applied before ORDER BY + LIMIT */
TEST(cypher_issue237_distinct_order_limit) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) RETURN DISTINCT f.label AS l ORDER BY l LIMIT 10", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #873: duplicate projected rows must be deduped before ORDER BY + LIMIT */
TEST(cypher_issue873_distinct_order_limit_dedupes_before_limit) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (n) RETURN DISTINCT n.label AS label ORDER BY label LIMIT 2", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    ASSERT_STR_EQ(r.rows[0][0], "Function");
    ASSERT_STR_EQ(r.rows[1][0], "Module");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #873: early LIMIT must not truncate rows before DISTINCT for simple RETURN */
TEST(cypher_issue873_distinct_limit_dedupes_before_limit) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc =
        cbm_cypher_execute(s, "MATCH (n) RETURN DISTINCT n.label AS label LIMIT 2", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #873: SKIP is applied after DISTINCT and ORDER BY, not before dedupe */
TEST(cypher_issue873_distinct_order_skip_limit_dedupes_before_skip) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (n) RETURN DISTINCT n.label AS label ORDER BY label SKIP 1 LIMIT 1", "test", 0,
        &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "Module");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #252: toInteger() */
TEST(cypher_issue252_tointeger) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN toInteger(f.start_line) AS ln",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #305: count(*) + AS alias */
TEST(cypher_issue305_count_star_alias) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) RETURN count(*) AS total", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 1);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Regression: projecting several computed/JSON properties in one row must yield
 * DISTINCT values. node_prop previously returned a single shared static buffer,
 * so every such column aliased the last property read — and because the search
 * key is matched in the JSON, `loop_depth` must not be confused with its suffix
 * `transitive_loop_depth`. Exercises the bottleneck metrics end-to-end. */
TEST(cypher_multi_prop_projection_no_alias) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "Hot",
                    .qualified_name = "test.Hot",
                    .file_path = "hot.go",
                    .start_line = 10,
                    .end_line = 42,
                    .properties_json = "{\"complexity\":3,\"cognitive\":7,\"loop_count\":2,"
                                       "\"loop_depth\":1,\"self_recursive\":false,"
                                       "\"transitive_loop_depth\":5,\"recursive\":true}"};
    cbm_store_upsert_node(s, &n);

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function) RETURN f.loop_depth, f.transitive_loop_depth, "
                                "f.cognitive, f.complexity, f.start_line, f.end_line",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_EQ(r.col_count, 6);
    ASSERT_STR_EQ(r.rows[0][0], "1");  /* loop_depth — NOT the suffix transitive_loop_depth */
    ASSERT_STR_EQ(r.rows[0][1], "5");  /* transitive_loop_depth */
    ASSERT_STR_EQ(r.rows[0][2], "7");  /* cognitive */
    ASSERT_STR_EQ(r.rows[0][3], "3");  /* complexity */
    ASSERT_STR_EQ(r.rows[0][4], "10"); /* start_line (computed) */
    ASSERT_STR_EQ(r.rows[0][5], "42"); /* end_line (computed) — distinct from start_line */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Result projection writes into fixed-width per-row stack arrays
 * (vals[CBM_SZ_32] / func_bufs[CBM_SZ_32][…] in execute_return_simple and its
 * siblings), indexed by the parsed RETURN item count. The parser must bound
 * that count to the array width; an over-wide RETURN has to be rejected, not
 * allowed to run and write past the arrays. Drive a >32-column RETURN in a
 * forked child so a stack overwrite (ASan abort, or a raw segfault) shows up
 * as a killing signal instead of taking down the whole runner; the bounded
 * path returns an ordinary error and the child exits cleanly. */
/* Property projection must return the WHOLE value of composite properties.
 * json_extract_prop() scanned a non-string value up to the first ',' — so an
 * array/object property was truncated at its first INTERNAL comma. Real-world
 * hit: a NestJS handler's decorators
 *   ["@Roles('OWNER', 'ADMIN')","@Get()"]
 * projected as ["@Roles('OWNER'   — unusable for route/authz queries. */
TEST(cypher_exec_prop_array_with_internal_commas) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t n = {.project = "test",
                    .label = "Method",
                    .name = "findAll",
                    .qualified_name = "test.PacienteController.findAll",
                    .file_path = "paciente.controller.ts",
                    .properties_json =
                        "{\"decorators\":[\"@Roles('OWNER', 'ADMIN')\",\"@Get()\"],\"lines\":3}"};
    cbm_store_upsert_node(s, &n);

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (m:Method) RETURN m.decorators, m.lines", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    /* whole array, commas and all — was ["@Roles('OWNER' before the fix */
    ASSERT_STR_EQ(r.rows[0][0], "[\"@Roles('OWNER', 'ADMIN')\",\"@Get()\"]");
    ASSERT_STR_EQ(r.rows[0][1], "3"); /* scalar sibling still parses */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* A string property must not end at an ESCAPED quote: the scan stopped at the
 * first '"' regardless of a preceding backslash, cutting the value short. */
TEST(cypher_exec_prop_string_with_escaped_quote) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "parse",
                    .qualified_name = "test.parse",
                    .file_path = "p.ts",
                    .properties_json = "{\"signature\":\"(sep: \\\"a,b\\\") => void\"}"};
    cbm_store_upsert_node(s, &n);

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.signature", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "(sep: \\\"a,b\\\") => void"); /* was: (sep: \ */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

TEST(cypher_wide_return_projection_bounded) {
#ifdef _WIN32
    SKIP_PLATFORM("fork crash-isolation is POSIX-only; the parse-time bound is platform-agnostic");
#else
    char query[4096];
    int off = snprintf(query, sizeof(query), "MATCH (f:Function) RETURN ");
    for (int i = 0; i < 48; i++) { /* 48 > CBM_SZ_32 (32) fixed columns */
        off += snprintf(query + off, sizeof(query) - (size_t)off, "%sf.p%d", i ? ", " : "", i);
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        cbm_store_t *s = setup_cypher_store();
        cbm_cypher_result_t r = {0};
        (void)cbm_cypher_execute(s, query, "test", 0, &r);
        cbm_cypher_result_free(&r);
        cbm_store_close(s);
        _exit(0); /* reached only when execution did NOT overflow */
    }
    ASSERT_TRUE(pid > 0);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "wide RETURN killed by signal %d — projection stack overflow",
                 WTERMSIG(status));
        FAIL(msg);
    }
    ASSERT_TRUE(WIFEXITED(status));
    PASS();
#endif
}

/* #601: an unbounded whole-graph OPTIONAL MATCH / GROUP BY does
 * O(bindings x groups) work and can run for minutes with no wall-clock guard —
 * the 100k row ceiling never fires because no rows are produced, so query_graph
 * just hangs. With the execution deadline armed to trip immediately (budget 0),
 * the runaway query must abort with a clear error instead of returning a
 * (misleading, possibly partial) result.
 *
 * RED on unfixed code: no deadline exists, so the query completes and returns
 * rc==0 with rows and no error — the assertions below fail. */
TEST(cypher_exec_deadline_aborts_runaway_query_issue601) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    cbm_cypher_test_set_deadline_ms(0); /* trip on the first hot-loop check */
    int rc = cbm_cypher_execute(
        s, "MATCH (a) OPTIONAL MATCH (a)-[:CALLS]->(b) RETURN a.qualified_name, count(b)", "test",
        0, &r);
    cbm_cypher_test_set_deadline_ms(-1); /* restore default before asserting (thread-local) */

    ASSERT_TRUE(rc != 0); /* CBM_NOT_FOUND (-1) — query aborted, not success */
    ASSERT_NOT_NULL(r.error);
    ASSERT_TRUE(strstr(r.error, "time limit") != NULL);
    ASSERT_EQ(r.row_count, 0);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* #601 companion: the default (ample) budget must NOT false-positive on a
 * normal small query — it still returns its rows. */
TEST(cypher_exec_deadline_allows_normal_query_issue601) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};

    int rc = cbm_cypher_execute(
        s, "MATCH (a) OPTIONAL MATCH (a)-[:CALLS]->(b) RETURN a.qualified_name, count(b)", "test",
        0, &r);

    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(r.error == NULL);
    ASSERT_GT(r.row_count, 0);

    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* ── Grouped aggregation with a scalar function as the group key ─────
 * The group key must be the *projected* value of every non-aggregate item,
 * including scalar/introspection functions (type(), labels(), toLower(), ...).
 * Previously any item carrying a `func` — aggregate or not — was excluded from
 * the key AND formatted as an aggregate, so every row collapsed into one group
 * and the row count was emitted in the group column too. Ground truth comes
 * from the fixture graph, not from the engine. */

/* Find the row whose first column equals `key`; NULL if absent. */
static const char **find_row_by_col0(const cbm_cypher_result_t *r, const char *key) {
    for (int i = 0; i < r->row_count; i++) {
        if (strcmp(r->rows[i][0], key) == 0) {
            return r->rows[i];
        }
    }
    return NULL;
}

/* Ground truth: fixture has 3 CALLS edges and 1 DEFINES edge. */
TEST(cypher_agg_group_by_type_func) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (a)-[r]->(b) RETURN type(r), count(r)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **calls = find_row_by_col0(&r, "CALLS");
    ASSERT_NOT_NULL(calls);
    ASSERT_STR_EQ(calls[1], "3");
    const char **defines = find_row_by_col0(&r, "DEFINES");
    ASSERT_NOT_NULL(defines);
    ASSERT_STR_EQ(defines[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Ground truth: fixture has 4 Function nodes and 1 Module node. */
TEST(cypher_agg_group_by_labels_func) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) RETURN labels(n), count(n)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **fn = find_row_by_col0(&r, "[\"Function\"]");
    ASSERT_NOT_NULL(fn);
    ASSERT_STR_EQ(fn[1], "4");
    const char **mod = find_row_by_col0(&r, "[\"Module\"]");
    ASSERT_NOT_NULL(mod);
    ASSERT_STR_EQ(mod[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Same defect class for a value-transforming scalar function (#toLower). */
TEST(cypher_agg_group_by_tolower_func) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) RETURN toLower(n.label) AS l, count(n) AS c", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **fn = find_row_by_col0(&r, "function");
    ASSERT_NOT_NULL(fn);
    ASSERT_STR_EQ(fn[1], "4");
    const char **mod = find_row_by_col0(&r, "module");
    ASSERT_NOT_NULL(mod);
    ASSERT_STR_EQ(mod[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* The WITH-clause aggregation path has the same grouping contract. */
TEST(cypher_with_agg_group_by_type_func) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (a)-[r]->(b) WITH type(r) AS t, count(r) AS c RETURN t, c", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **calls = find_row_by_col0(&r, "CALLS");
    ASSERT_NOT_NULL(calls);
    ASSERT_STR_EQ(calls[1], "3");
    const char **defines = find_row_by_col0(&r, "DEFINES");
    ASSERT_NOT_NULL(defines);
    ASSERT_STR_EQ(defines[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Same defect class for a multi-argument scalar function as the group key. */
TEST(cypher_agg_group_by_multiarg_func) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) RETURN left(n.label, 1) AS i, count(n) AS c", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **fn = find_row_by_col0(&r, "F");
    ASSERT_NOT_NULL(fn);
    ASSERT_STR_EQ(fn[1], "4");
    const char **mod = find_row_by_col0(&r, "M");
    ASSERT_NOT_NULL(mod);
    ASSERT_STR_EQ(mod[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* WITH grouping keyed on a CASE expression: the WITH path used to read the
 * group value with binding_get_virtual, which cannot evaluate a CASE, so the
 * key was the literal variable name "CASE" for every row — one group. */
TEST(cypher_with_agg_group_by_case) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (n) WITH CASE WHEN n.label = \"Function\" THEN \"fn\" "
                                "ELSE \"other\" END AS k, count(n) AS c RETURN k, c",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **fn = find_row_by_col0(&r, "fn");
    ASSERT_NOT_NULL(fn);
    ASSERT_STR_EQ(fn[1], "4");
    const char **other = find_row_by_col0(&r, "other");
    ASSERT_NOT_NULL(other);
    ASSERT_STR_EQ(other[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* A grouping value too long for the group-key buffer must not silently merge
 * two distinct groups into one. Two nodes whose qualified_name differs only
 * past the buffer cut are distinct groups; the engine cannot represent that, so
 * the documented contract requires a loud `unsupported ...` error, never a
 * plausible-looking single group carrying the row count. */
TEST(cypher_agg_group_key_truncation_errors) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    /* Longer than the CBM_SZ_512 per-value projection buffer; the two names
     * share every byte the engine can keep and differ only in the tail. */
    char qn_a[900];
    char qn_b[900];
    memset(qn_a, 'x', sizeof(qn_a));
    qn_a[sizeof(qn_a) - 1] = '\0';
    memcpy(qn_b, qn_a, sizeof(qn_b));
    qn_a[880] = 'A';
    qn_b[880] = 'B';
    cbm_node_t n1 = {.project = "test", .label = "Function", .name = "f1", .qualified_name = qn_a};
    cbm_node_t n2 = {.project = "test", .label = "Function", .name = "f2", .qualified_name = qn_b};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) RETURN n.qualified_name, count(n)", "test", 0, &r);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(r.error);
    ASSERT_NOT_NULL(strstr(r.error, "unsupported"));
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Companion: a grouping value that fits must NOT trip the truncation guard —
 * otherwise the guard would reject ordinary queries and the test above could
 * pass for the wrong reason. */
TEST(cypher_agg_group_key_within_bounds_ok) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");
    char qn_a[400];
    char qn_b[400];
    memset(qn_a, 'x', sizeof(qn_a));
    qn_a[sizeof(qn_a) - 1] = '\0';
    memcpy(qn_b, qn_a, sizeof(qn_b));
    qn_a[380] = 'A';
    qn_b[380] = 'B';
    cbm_node_t n1 = {.project = "test", .label = "Function", .name = "f1", .qualified_name = qn_a};
    cbm_node_t n2 = {.project = "test", .label = "Function", .name = "f2", .qualified_name = qn_b};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);

    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) RETURN n.qualified_name, count(n)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2); /* two distinct long-but-representable groups */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Non-regression: grouping by a bare property must keep working unchanged. */
TEST(cypher_agg_group_by_bare_property) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) RETURN n.label, count(n)", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **fn = find_row_by_col0(&r, "Function");
    ASSERT_NOT_NULL(fn);
    ASSERT_STR_EQ(fn[1], "4");
    const char **mod = find_row_by_col0(&r, "Module");
    ASSERT_NOT_NULL(mod);
    ASSERT_STR_EQ(mod[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Non-regression: WITH grouping by a bare property. */
TEST(cypher_with_agg_group_by_bare_property) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (n) WITH n.label AS l, count(n) AS c RETURN l, c", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    const char **fn = find_row_by_col0(&r, "Function");
    ASSERT_NOT_NULL(fn);
    ASSERT_STR_EQ(fn[1], "4");
    const char **mod = find_row_by_col0(&r, "Module");
    ASSERT_NOT_NULL(mod);
    ASSERT_STR_EQ(mod[1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  ORDER BY RESOLVABILITY  (silent-wrong-answer class)
 *
 *  The engine sorts the MATERIALIZED result table: result_builder_t rows hold
 *  already-projected strings and the bindings that produced them are gone by
 *  then, so the only sort keys it can evaluate are the result's own column
 *  names and aliases. A key outside that set was looked up, missed, and then
 *  silently skipped — the query returned UNSORTED rows with rc==0 and no
 *  error, indistinguishable from a correct answer. Anything the engine cannot
 *  evaluate must now fail loudly instead.
 * ══════════════════════════════════════════════════════════════════ */

/* Fixture whose name order and file_path order DISAGREE (and whose complexity
 * order disagrees with both), so a test asserting a file_path/complexity sort
 * cannot pass by accidentally receiving name order or insertion order. */
static cbm_store_t *setup_order_by_store(void) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "Alpha",
                     .qualified_name = "test.Alpha",
                     .file_path = "z.go",
                     .properties_json = "{\"complexity\":1}"};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "Beta",
                     .qualified_name = "test.Beta",
                     .file_path = "m.go",
                     .properties_json = "{\"complexity\":3}"};
    cbm_node_t n3 = {.project = "test",
                     .label = "Function",
                     .name = "Gamma",
                     .qualified_name = "test.Gamma",
                     .file_path = "a.go",
                     .properties_json = "{\"complexity\":2}"};
    cbm_store_upsert_node(s, &n1);
    cbm_store_upsert_node(s, &n2);
    cbm_store_upsert_node(s, &n3);
    return s;
}

/* An ORDER BY key that names no column, alias, or property must be rejected.
 * RED on unfixed code: rc == 0 and the rows come back in insertion order with
 * r.error NULL — the sort was silently dropped. */
TEST(cypher_order_by_unknown_column_errors) {
    cbm_store_t *s = setup_order_by_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) RETURN f.name ORDER BY f.nonexistent_col DESC", "test", 0, &r);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(r.error);
    ASSERT_TRUE(strstr(r.error, "f.nonexistent_col") != NULL); /* names the real key */
    ASSERT_EQ(r.row_count, 0);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* The boundary case: openCypher permits sorting on an expression absent from
 * RETURN, but this engine sorts the projected table and cannot evaluate one.
 * It must say so rather than return unsorted rows.
 * RED on unfixed code: rc == 0, rows in insertion order, no error. */
TEST(cypher_order_by_unreturned_column_errors) {
    cbm_store_t *s = setup_order_by_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name ORDER BY f.file_path", "test",
                                0, &r);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(r.error);
    ASSERT_TRUE(strstr(r.error, "f.file_path") != NULL);
    ASSERT_TRUE(strstr(r.error, "RETURN") != NULL); /* actionable: add it to RETURN */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* Same defect on the WITH path, which sorts bindings by projected alias.
 * RED on unfixed code: rc == 0 and the unresolvable key sorts nothing. */
TEST(cypher_with_order_by_unknown_alias_errors) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s,
                                "MATCH (f:Function)-[:CALLS]->(g:Function) "
                                "WITH f.name AS caller, COUNT(g) AS cnt "
                                "ORDER BY bogus DESC "
                                "RETURN caller, cnt",
                                "test", 0, &r);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(r.error);
    ASSERT_TRUE(strstr(r.error, "bogus") != NULL);
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* `ORDER BY` with no sort key at all parsed to an empty key that then matched
 * no column and was silently dropped.
 * RED on unfixed code: rc == 0 — a malformed query accepted. */
TEST(cypher_order_by_missing_key_rejected) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (f:Function) RETURN f.name ORDER BY", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    cbm_query_free(q);
    free(err);
    PASS();
}

/* NON-REGRESSION (green in both states): sorting on a returned column that is
 * NOT the first one must still order by that column. The fixture's file_path
 * order differs from its name order, so name order cannot fake a pass. */
TEST(cypher_order_by_returned_second_column_still_sorts) {
    cbm_store_t *s = setup_order_by_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) RETURN f.name, f.file_path ORDER BY f.file_path ASC", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 3);
    ASSERT_STR_EQ(r.rows[0][0], "Gamma"); /* a.go */
    ASSERT_STR_EQ(r.rows[1][0], "Beta");  /* m.go */
    ASSERT_STR_EQ(r.rows[2][0], "Alpha"); /* z.go */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* NON-REGRESSION (green in both states): the live-repro shape — a numeric
 * JSON-derived property, returned and sorted DESC. */
TEST(cypher_order_by_returned_json_metric_still_sorts) {
    cbm_store_t *s = setup_order_by_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (f:Function) RETURN f.name, f.complexity ORDER BY f.complexity DESC", "test", 0,
        &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 3);
    ASSERT_STR_EQ(r.rows[0][0], "Beta");  /* 3 */
    ASSERT_STR_EQ(r.rows[1][0], "Gamma"); /* 2 */
    ASSERT_STR_EQ(r.rows[2][0], "Alpha"); /* 1 */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* NON-REGRESSION (green in both states): the README/skill-documented grouped-
 * aggregate recipe `... RETURN k, count(x) ORDER BY count(x) DESC`. The sort key
 * is an aggregate CALL, not a bare name, so it exercises the aggregate branch of
 * parse_order_by_expr — the branch the `count`-as-a-name fix had to narrow. */
TEST(cypher_order_by_aggregate_call_still_sorts) {
    cbm_store_t *s = setup_cypher_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(
        s, "MATCH (a)-[r]->(b) RETURN type(r), count(r) ORDER BY count(r) DESC", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    ASSERT_STR_EQ(r.rows[0][0], "CALLS"); /* 3 */
    ASSERT_STR_EQ(r.rows[0][1], "3");
    ASSERT_STR_EQ(r.rows[1][0], "DEFINES"); /* 1 */
    ASSERT_STR_EQ(r.rows[1][1], "1");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* NON-REGRESSION (green in both states): `RETURN *` projects var.name /
 * .qualified_name / .label / .file_path, so those ARE resolvable columns and
 * must keep sorting — the boundary is what the result table holds, not whether
 * the key was written out in the RETURN list. */
TEST(cypher_order_by_star_projection_still_sorts) {
    cbm_store_t *s = setup_order_by_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN * ORDER BY f.file_path ASC", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 3);
    ASSERT_STR_EQ(r.rows[0][0], "Gamma"); /* a.go — col 0 is f.name */
    ASSERT_STR_EQ(r.rows[1][0], "Beta");  /* m.go */
    ASSERT_STR_EQ(r.rows[2][0], "Alpha"); /* z.go */
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* NON-REGRESSION (green in both states): sorting on an AS alias. */
TEST(cypher_order_by_alias_still_sorts) {
    cbm_store_t *s = setup_order_by_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (f:Function) RETURN f.name AS n ORDER BY n DESC", "test",
                                0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 3);
    ASSERT_STR_EQ(r.rows[0][0], "Gamma");
    ASSERT_STR_EQ(r.rows[1][0], "Beta");
    ASSERT_STR_EQ(r.rows[2][0], "Alpha");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  KEYWORD-SHAPED PROPERTY NAMES
 *
 *  The lexer maps every keyword unconditionally, so `count` after a dot came
 *  back as TOK_COUNT where a property name was expected. The property was
 *  dropped, the token left unconsumed, and the query died with "unexpected
 *  trailing tokens" — an error naming the wrong problem. In openCypher a
 *  keyword IS a legal property key after `.`; nothing else can appear there.
 * ══════════════════════════════════════════════════════════════════ */

static cbm_store_t *setup_keyword_prop_store(void) {
    cbm_store_t *s = cbm_store_open_memory();
    cbm_store_upsert_project(s, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "File",
                     .name = "a.go",
                     .qualified_name = "test.a.go",
                     .file_path = "a.go",
                     .properties_json = "{\"count\":7,\"end\":\"tail\"}"};
    cbm_node_t n2 = {.project = "test",
                     .label = "File",
                     .name = "b.go",
                     .qualified_name = "test.b.go",
                     .file_path = "b.go",
                     .properties_json = "{\"count\":2,\"end\":\"head\"}"};
    int64_t id1 = cbm_store_upsert_node(s, &n1);
    int64_t id2 = cbm_store_upsert_node(s, &n2);

    cbm_edge_t e1 = {.project = "test",
                     .source_id = id1,
                     .target_id = id2,
                     .type = "FILE_CHANGES_WITH",
                     .properties_json = "{\"count\":5}"};
    cbm_store_insert_edge(s, &e1);
    return s;
}

/* RED on unfixed code: rc == -1 with "unexpected trailing tokens at pos 24". */
TEST(cypher_node_property_named_count_parses) {
    cbm_store_t *s = setup_keyword_prop_store();
    cbm_cypher_result_t r = {0};
    int rc = cbm_cypher_execute(s, "MATCH (a:File) RETURN a.name, a.count ORDER BY a.name ASC",
                                "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 2);
    ASSERT_STR_EQ(r.rows[0][1], "7");
    ASSERT_STR_EQ(r.rows[1][1], "2");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* RED on unfixed code: rc == -1, "unexpected trailing tokens". */
TEST(cypher_edge_property_named_count_parses) {
    cbm_store_t *s = setup_keyword_prop_store();
    cbm_cypher_result_t r = {0};
    int rc =
        cbm_cypher_execute(s, "MATCH (a)-[r:FILE_CHANGES_WITH]->(b) RETURN r.count", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "5");
    cbm_cypher_result_free(&r);
    cbm_store_close(s);
    PASS();
}

/* The same lexer collision hits every dot-property position, not just RETURN:
 * WHERE, and other keyword-shaped names such as `end`.
 * RED on unfixed code: rc == -1 for both queries. */
TEST(cypher_where_property_named_keyword_parses) {
    cbm_store_t *s = setup_keyword_prop_store();

    cbm_cypher_result_t r = {0};
    int rc =
        cbm_cypher_execute(s, "MATCH (a:File) WHERE a.count = \"7\" RETURN a.name", "test", 0, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_NULL(r.error);
    ASSERT_EQ(r.row_count, 1);
    ASSERT_STR_EQ(r.rows[0][0], "a.go");
    cbm_cypher_result_free(&r);

    cbm_cypher_result_t r2 = {0};
    int rc2 =
        cbm_cypher_execute(s, "MATCH (a:File) WHERE a.end = \"head\" RETURN a.end", "test", 0, &r2);
    ASSERT_EQ(rc2, 0);
    ASSERT_NULL(r2.error);
    ASSERT_EQ(r2.row_count, 1);
    ASSERT_STR_EQ(r2.rows[0][0], "head");
    cbm_cypher_result_free(&r2);

    cbm_store_close(s);
    PASS();
}

/* A quoted string in property position is NOT a property name (openCypher uses
 * backticks, not quotes, to escape names). Accepting keywords after `.` must
 * not widen into accepting literals there.
 * Green in both states: pins the accept-set so the fix cannot over-open it. */
TEST(cypher_quoted_string_not_a_property_name) {
    cbm_query_t *q = NULL;
    char *err = NULL;
    int rc = cbm_cypher_parse("MATCH (a:File) RETURN a.\"count\"", &q, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(err);
    cbm_query_free(q);
    free(err);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════ */

SUITE(cypher) {
    /* Lexer */
    RUN_TEST(cypher_lex_simple_match);
    RUN_TEST(cypher_lex_relationship);
    RUN_TEST(cypher_lex_string_literal);
    RUN_TEST(cypher_lex_single_quote_string);
    RUN_TEST(cypher_lex_string_overflow);
    RUN_TEST(cypher_lex_number);
    RUN_TEST(cypher_lex_operators);
    RUN_TEST(cypher_lex_keywords_case_insensitive);
    RUN_TEST(cypher_lex_pipe_and_star);
    RUN_TEST(cypher_lex_full_query);
    /* Parser */
    RUN_TEST(cypher_parse_simple_node);
    RUN_TEST(cypher_parse_relationship_outbound);
    RUN_TEST(cypher_parse_relationship_inbound);
    RUN_TEST(cypher_parse_relationship_any);
    RUN_TEST(cypher_parse_variable_length);
    RUN_TEST(cypher_parse_variable_length_unbounded);
    RUN_TEST(cypher_parse_multiple_edge_types);
    RUN_TEST(cypher_parse_where_clause);
    RUN_TEST(cypher_parse_where_regex);
    RUN_TEST(cypher_parse_where_and);
    RUN_TEST(cypher_parse_return_simple);
    RUN_TEST(cypher_parse_return_count);
    RUN_TEST(cypher_parse_return_order_limit);
    RUN_TEST(cypher_parse_return_distinct);
    RUN_TEST(cypher_parse_inline_props);
    RUN_TEST(cypher_parse_error);
    /* Execution */
    RUN_TEST(cypher_exec_deadline_aborts_runaway_query_issue601);
    RUN_TEST(cypher_exec_deadline_allows_normal_query_issue601);
    RUN_TEST(cypher_exec_match_all_functions);
    RUN_TEST(cypher_issue240_labels_function);
    RUN_TEST(cypher_issue237_distinct_order_limit);
    RUN_TEST(cypher_issue873_distinct_order_limit_dedupes_before_limit);
    RUN_TEST(cypher_issue873_distinct_limit_dedupes_before_limit);
    RUN_TEST(cypher_issue873_distinct_order_skip_limit_dedupes_before_skip);
    RUN_TEST(cypher_issue252_tointeger);
    RUN_TEST(cypher_issue305_count_star_alias);
    RUN_TEST(cypher_exec_where_eq);
    RUN_TEST(cypher_exec_varlength_path_semantics_issue797);
    RUN_TEST(cypher_exec_where_coalesce_issue874);
    RUN_TEST(cypher_exec_where_regex);
    RUN_TEST(cypher_exec_where_contains);
    RUN_TEST(cypher_exec_where_starts_with);
    RUN_TEST(cypher_exec_return_properties);
    RUN_TEST(cypher_func_labels);
    RUN_TEST(cypher_func_type);
    RUN_TEST(cypher_func_id);
    RUN_TEST(cypher_func_keys);
    RUN_TEST(cypher_func_properties);
    RUN_TEST(cypher_func_tointeger_tofloat);
    RUN_TEST(cypher_func_size_reverse);
    RUN_TEST(cypher_func_multiarg);
    RUN_TEST(cypher_issue874_where_coalesce_numeric);
    RUN_TEST(cypher_issue874_where_coalesce_string);
    RUN_TEST(cypher_issue874_where_coalesce_not_and);
    RUN_TEST(cypher_issue874_where_substring);
    RUN_TEST(cypher_issue874_where_unsupported_func_error);
    RUN_TEST(cypher_multi_prop_projection_no_alias);
    RUN_TEST(cypher_exists_no_callers);
    RUN_TEST(cypher_exists_has_outgoing_calls);
    RUN_TEST(cypher_exec_calls_relationship);
    RUN_TEST(cypher_exec_calls_with_where);
    RUN_TEST(cypher_exec_inbound);
    RUN_TEST(cypher_exec_count);
    RUN_TEST(cypher_exec_limit);
    RUN_TEST(cypher_exec_order_by);
    RUN_TEST(cypher_exec_variable_length);
    RUN_TEST(cypher_exec_var_length_explicit_bound_capped);
    RUN_TEST(cypher_exec_defines_edge);
    RUN_TEST(cypher_exec_no_results);
    RUN_TEST(cypher_exec_where_numeric);
    /* Go test ports */
    RUN_TEST(cypher_exec_distinct);
    RUN_TEST(cypher_exec_with_distinct_issue238);
    RUN_TEST(cypher_exec_where_label_test_issue241);
    RUN_TEST(cypher_exec_label_alternation_issue242);
    RUN_TEST(cypher_exec_count_distinct_issue239);
    RUN_TEST(cypher_exec_unsupported_func_errors_issue373);
    RUN_TEST(cypher_exec_unknown_func_return_errors);
    RUN_TEST(cypher_exec_inline_props);
    RUN_TEST(cypher_parse_where_starts_with);
    RUN_TEST(cypher_parse_where_contains);
    RUN_TEST(cypher_parse_where_numeric);
    /* Edge property tests (ported from cypher_test.go Feature 2) */
    RUN_TEST(cypher_edge_prop_access);
    RUN_TEST(cypher_edge_prop_storage_is_per_thread);
    RUN_TEST(cypher_edge_prop_in_where);
    RUN_TEST(cypher_edge_type_prop);
    RUN_TEST(cypher_edge_filter_contains);
    RUN_TEST(cypher_edge_filter_numeric_gte);
    RUN_TEST(cypher_bare_edge_return_exposes_properties_json);
    RUN_TEST(cypher_edge_return_without_filter);
    RUN_TEST(cypher_edge_filter_equals);
    RUN_TEST(cypher_edge_filter_starts_with);
    RUN_TEST(cypher_edge_combined_node_and_edge_filter);
    RUN_TEST(cypher_edge_filter_no_match);
    RUN_TEST(cypher_edge_filter_numeric_lt);
    RUN_TEST(cypher_edge_filter_regex);
    RUN_TEST(cypher_edge_builtin_type_filter);
    RUN_TEST(cypher_apply_limit);
    /* Phase 1: Simple operators */
    RUN_TEST(cypher_lex_neq_operators);
    RUN_TEST(cypher_lex_ends_keyword);
    RUN_TEST(cypher_lex_in_is_null);
    RUN_TEST(cypher_exec_where_neq);
    RUN_TEST(cypher_exec_where_neq_bang);
    RUN_TEST(cypher_exec_where_ends_with);
    RUN_TEST(cypher_exec_where_not);
    RUN_TEST(cypher_exec_where_in);
    RUN_TEST(cypher_exec_where_not_in);
    RUN_TEST(cypher_exec_where_is_null);
    RUN_TEST(cypher_exec_where_is_not_null);
    RUN_TEST(cypher_exec_return_star);
    RUN_TEST(cypher_parse_neq);
    RUN_TEST(cypher_parse_in);
    RUN_TEST(cypher_parse_is_null);
    /* Phase 2: Expression tree */
    RUN_TEST(cypher_exec_where_or);
    RUN_TEST(cypher_exec_where_complex_bool);
    RUN_TEST(cypher_exec_where_xor);
    RUN_TEST(cypher_exec_where_not_prefix);
    RUN_TEST(cypher_parse_expr_tree_and_or);
    RUN_TEST(cypher_parse_expr_tree_nested);
    /* Phase 3: Unsupported keyword errors */
    RUN_TEST(cypher_error_create);
    RUN_TEST(cypher_error_delete);
    RUN_TEST(cypher_error_set);
    RUN_TEST(cypher_error_merge);
    RUN_TEST(cypher_error_call);
    /* Phase 4: SKIP + aggregation */
    RUN_TEST(cypher_exec_skip);
    RUN_TEST(cypher_exec_skip_limit);
    RUN_TEST(cypher_exec_sum);
    RUN_TEST(cypher_exec_avg);
    RUN_TEST(cypher_exec_min);
    RUN_TEST(cypher_exec_max);
    RUN_TEST(cypher_exec_collect);
    RUN_TEST(cypher_exec_count_star);
    RUN_TEST(cypher_parse_skip);
    RUN_TEST(cypher_parse_sum_avg);
    RUN_TEST(cypher_parse_collect);
    /* Phase 5: String functions + CASE */
    RUN_TEST(cypher_exec_tolower);
    RUN_TEST(cypher_exec_toupper);
    RUN_TEST(cypher_exec_tostring);
    RUN_TEST(cypher_exec_case);
    RUN_TEST(cypher_parse_tolower);
    RUN_TEST(cypher_parse_case);
    /* Phase 6: WITH clause */
    RUN_TEST(cypher_exec_with_rename);
    RUN_TEST(cypher_exec_with_count);
    RUN_TEST(cypher_exec_with_node_groupvar_prop);
    RUN_TEST(cypher_exec_with_where);
    RUN_TEST(cypher_exec_with_orderby_limit);
    RUN_TEST(cypher_parse_with);
    RUN_TEST(cypher_parse_with_where);
    /* Phase 7: OPTIONAL MATCH + multiple MATCH */
    RUN_TEST(cypher_exec_optional_match_no_result);
    RUN_TEST(cypher_exec_optional_match_has_result);
    RUN_TEST(cypher_exec_multi_match);
    RUN_TEST(cypher_exec_cross_join_nodes_no_overflow);
    RUN_TEST(cypher_parse_optional_match);
    RUN_TEST(cypher_parse_multi_match);
    /* Phase 8: UNION */
    RUN_TEST(cypher_exec_union);
    RUN_TEST(cypher_exec_union_all);
    RUN_TEST(cypher_parse_union);
    /* Phase 9: UNWIND */
    RUN_TEST(cypher_parse_unwind);
    RUN_TEST(cypher_parse_unwind_var);
    RUN_TEST(cypher_unwind_long_list_bounded);
    /* Phase 10: multi-key ORDER BY + trailing-token honesty */
    RUN_TEST(cypher_order_by_multikey_honors_limit);
    RUN_TEST(cypher_parse_trailing_tokens_rejected);
    RUN_TEST(cypher_wide_return_projection_bounded);
    /* Composite property projection (arrays/objects, escaped quotes) */
    RUN_TEST(cypher_exec_prop_array_with_internal_commas);
    RUN_TEST(cypher_exec_prop_string_with_escaped_quote);
    /* Grouped aggregation keyed on a scalar function */
    RUN_TEST(cypher_agg_group_by_type_func);
    RUN_TEST(cypher_agg_group_by_labels_func);
    RUN_TEST(cypher_agg_group_by_tolower_func);
    RUN_TEST(cypher_agg_group_by_multiarg_func);
    RUN_TEST(cypher_with_agg_group_by_type_func);
    RUN_TEST(cypher_with_agg_group_by_case);
    RUN_TEST(cypher_agg_group_key_truncation_errors);
    RUN_TEST(cypher_agg_group_key_within_bounds_ok);
    RUN_TEST(cypher_agg_group_by_bare_property);
    RUN_TEST(cypher_with_agg_group_by_bare_property);
    /* ORDER BY resolvability: unresolvable sort keys must not be dropped */
    RUN_TEST(cypher_order_by_unknown_column_errors);
    RUN_TEST(cypher_order_by_unreturned_column_errors);
    RUN_TEST(cypher_with_order_by_unknown_alias_errors);
    RUN_TEST(cypher_order_by_missing_key_rejected);
    RUN_TEST(cypher_order_by_returned_second_column_still_sorts);
    RUN_TEST(cypher_order_by_returned_json_metric_still_sorts);
    RUN_TEST(cypher_order_by_aggregate_call_still_sorts);
    RUN_TEST(cypher_order_by_star_projection_still_sorts);
    RUN_TEST(cypher_order_by_alias_still_sorts);
    /* Keyword-shaped property names after '.' */
    RUN_TEST(cypher_node_property_named_count_parses);
    RUN_TEST(cypher_edge_property_named_count_parses);
    RUN_TEST(cypher_where_property_named_keyword_parses);
    RUN_TEST(cypher_quoted_string_not_a_property_name);
}
