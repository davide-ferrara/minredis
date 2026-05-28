#include "strbuf.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests = 0, passed = 0;

#define TEST(name) tests++; printf("  %-42s", #name)
#define PASS()     passed++; printf("PASS\n")
#define FAIL(m)    printf("FAIL: %s\n", m)

static void test_new_nonnull(void) {
  TEST(new nonnull);
  StringBuffer *sb = strbuf_new(64, "hello");
  assert(sb != NULL);
  assert(sb->buf != NULL);
  assert(sb->cap == 64);
  assert(sb->len == 6);
  assert(strcmp(sb->buf, "hello") == 0);
  strbuf_free(sb);
  PASS();
}

static void test_new_empty(void) {
  TEST(new empty string);
  StringBuffer *sb = strbuf_new(10, "");
  assert(sb->len == 1);
  assert(sb->buf[0] == '\0');
  strbuf_free(sb);
  PASS();
}

static void test_new_null_initial(void) {
  TEST(new NULL initial);
  StringBuffer *sb = strbuf_new(10, NULL);
  assert(sb->len == 1);
  assert(sb->buf[0] == '\0');
  strbuf_free(sb);
  PASS();
}

static void test_new_truncate(void) {
  TEST(new truncate overflow);
  StringBuffer *sb = strbuf_new(4, "hello world");
  assert(sb->len == 4);
  assert(memcmp(sb->buf, "hel", 3) == 0);
  assert(sb->buf[3] == '\0');
  strbuf_free(sb);
  PASS();
}

static void test_append_no_sep(void) {
  TEST(append without separator);
  StringBuffer *sb = strbuf_new(16, "abc");
  int r = strbuf_append(sb, "xyz", NULL);
  assert(r == 0);
  assert(sb->len == 7);
  assert(strcmp(sb->buf, "abcxyz") == 0);
  strbuf_free(sb);
  PASS();
}

static void test_append_with_sep(void) {
  TEST(append with separator);
  StringBuffer *sb = strbuf_new(16, "abc");
  strbuf_append(sb, "def", " ");
  strbuf_append(sb, "ghi", " ");
  assert(sb->len == 12);
  assert(strcmp(sb->buf, "abc def ghi") == 0);
  strbuf_free(sb);
  PASS();
}

static void test_append_sep_ignored(void) {
  TEST(append invalid sep ignored);
  StringBuffer *sb = strbuf_new(16, "abc");
  strbuf_append(sb, "xyz", "");   /* strlen=0, sep ignorato */
  strbuf_append(sb, "uvw", "--"); /* strlen=2, sep ignorato */
  assert(strcmp(sb->buf, "abcxyzuvw") == 0);
  strbuf_free(sb);
  PASS();
}

static void test_append_realloc(void) {
  TEST(append realloc on overflow);
  StringBuffer *sb = strbuf_new(4, "abc");
  int r = strbuf_append(sb, "wxyz", NULL);
  assert(r == 0);
  assert(sb->cap == 8);
  assert(sb->len == 8);
  assert(strcmp(sb->buf, "abcwxyz") == 0);
  r = strbuf_append(sb, "!!", NULL);
  assert(r == 0);
  assert(sb->cap == 16);
  assert(strcmp(sb->buf, "abcwxyz!!") == 0);
  strbuf_free(sb);
  PASS();
}

static void test_append_null_str(void) {
  TEST(append NULL str);
  StringBuffer *sb = strbuf_new(16, "test");
  int r = strbuf_append(sb, NULL, NULL);
  assert(r == -1);
  assert(sb->len == 5);
  strbuf_free(sb);
  PASS();
}

static void test_free_null(void) {
  TEST(free NULL safe);
  strbuf_free(NULL);
  PASS();
}

static void test_noterm(void) {
  TEST(no termination);
  /* Raw byte buffer: niente \0 da strbuf_new */
  StringBuffer *sb = malloc(sizeof(StringBuffer));
  sb->buf = malloc(32);
  memcpy(sb->buf, "GET ", 4);
  sb->len = 4;
  sb->cap = 32;
  strbuf_append_noterm(sb, "name", 4);
  sb->buf[sb->len] = 0;
  assert(strcmp("GET name", sb->buf) == 0);
  free(sb->buf);
  free(sb);
  PASS();
}

int main(void) {
  printf("\n=== strbuf Tests ===\n\n");
  test_new_nonnull();
  test_new_empty();
  test_new_null_initial();
  test_new_truncate();
  test_append_no_sep();
  test_append_with_sep();
  test_append_sep_ignored();
  test_append_realloc();
  test_append_null_str();
  test_noterm();
  test_free_null();
  printf("\n  %d/%d passed\n\n", passed, tests);
  return passed == tests ? 0 : 1;
}
