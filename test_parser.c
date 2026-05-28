#include "cmd.h"
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_count = 0;
static int pass_count = 0;

#define TEST(name)                                       \
    do {                                                 \
        test_count++;                                    \
        printf("  %-50s", #name);                        \
    } while (0)

#define PASS()                                           \
    do {                                                 \
        pass_count++;                                    \
        printf("PASS\n");                                \
    } while (0)

#define FAIL(msg)                                        \
    do {                                                 \
        printf("FAIL: %s\n", msg);                       \
    } while (0)

static StringBuffer *make_raw(const char *s) {
    size_t len = strlen(s);
    StringBuffer *sb = malloc(sizeof(StringBuffer));
    memset(sb, 0, sizeof(StringBuffer));
    sb->len = len;
    sb->buf = malloc(len * sizeof(char));
    memcpy(sb->buf, s, len);
    return sb;
}

static void free_raw(StringBuffer *sb) {
    free(sb->buf);
    free(sb);
}

static void test_cmd_init(void) {
    TEST("cmd_init returns non-NULL");
    Cmd *cmd = cmd_init();
    if (cmd != NULL && cmd->tokens != NULL) PASS(); else FAIL("cmd or tokens is NULL");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
}

static void test_parse_get(void) {
    TEST("parse GET sets token count to 2");
    StringBuffer *raw = make_raw("GET abc\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->tokens->count == 2) PASS(); else FAIL("wrong count");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_get_tokens(void) {
    TEST("parse GET token[0] is 'GET'");
    StringBuffer *raw = make_raw("GET abc\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->tokens->count >= 1 &&
        strcmp(cmd->tokens->buf[0], "GET") == 0)
        PASS(); else FAIL("token[0] mismatch");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_get_key(void) {
    TEST("parse GET token[1] is 'abc'");
    StringBuffer *raw = make_raw("GET abc\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->tokens->count >= 2 &&
        strcmp(cmd->tokens->buf[1], "abc") == 0)
        PASS(); else FAIL("token[1] mismatch");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_get_ready(void) {
    TEST("parse GET with CRLF sets ready=1");
    StringBuffer *raw = make_raw("GET abc\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->ready == 1) PASS(); else FAIL("ready not set");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_set(void) {
    TEST("parse SET has 3 tokens");
    StringBuffer *raw = make_raw("SET key val\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->tokens->count == 3) PASS(); else FAIL("wrong count");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_set_tokens(void) {
    TEST("parse SET tokens are [SET, key, val]");
    StringBuffer *raw = make_raw("SET key val\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->tokens->count >= 3 &&
        strcmp(cmd->tokens->buf[0], "SET") == 0 &&
        strcmp(cmd->tokens->buf[1], "key") == 0 &&
        strcmp(cmd->tokens->buf[2], "val") == 0)
        PASS(); else FAIL("token mismatch");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_del(void) {
    TEST("parse DEL has 2 tokens");
    StringBuffer *raw = make_raw("DEL key\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->tokens->count == 2) PASS(); else FAIL("wrong count");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_no_crlf(void) {
    TEST("parse without CRLF sets ready=0");
    StringBuffer *raw = make_raw("GET abc");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->ready == 0) PASS(); else FAIL("ready should be 0");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_empty(void) {
    TEST("parse empty string has 0 tokens");
    StringBuffer *raw = make_raw("\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    if (cmd->tokens->count == 0) PASS(); else FAIL("should be 0");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

static void test_parse_multiple_spaces(void) {
    TEST("parse SET with extra spaces");
    StringBuffer *raw = make_raw("SET  key  val\r\n");
    Cmd *cmd = cmd_init();
    cmd_parse(cmd, raw);
    /* tokeni vuoti tra spazi multipli */
    if (cmd->tokens->count >= 3) PASS(); else FAIL("not enough tokens");
    free(cmd->tokens->buf);
    free(cmd->tokens);
    free(cmd);
    free_raw(raw);
}

int main(void) {
    printf("\n=== Parser Tests ===\n\n");

    test_cmd_init();
    test_parse_get();
    test_parse_get_tokens();
    test_parse_get_key();
    test_parse_get_ready();
    test_parse_set();
    test_parse_set_tokens();
    test_parse_del();
    test_parse_no_crlf();
    test_parse_empty();
    test_parse_multiple_spaces();

    printf("\n  %d/%d passed\n\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}