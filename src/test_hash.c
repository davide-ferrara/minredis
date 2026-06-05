#include "hash.h"
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

static void test_hash_init(void) {
    TEST("hash_init returns non-NULL");
    HashTable *ht = hash_init();
    if (ht != NULL) PASS(); else FAIL("ht is NULL");
}

static void test_hash_set_get(void) {
    HashTable *ht = hash_init();

    TEST("hash_set then hash_get returns value");
    hash_set(ht, "nome", "davide");
    char *val = hash_get(ht, "nome");
    if (val != NULL && strcmp(val, "davide") == 0) PASS(); else FAIL(val);

    TEST("hash_get non-existent key returns NULL");
    val = hash_get(ht, "inesistente");
    if (val == NULL) PASS(); else FAIL("expected NULL");
}

static void test_hash_set_overwrite(void) {
    HashTable *ht = hash_init();

    TEST("hash_set overwrites existing value");
    hash_set(ht, "nome", "davide");
    hash_set(ht, "nome", "martina");
    char *val = hash_get(ht, "nome");
    if (val != NULL && strcmp(val, "martina") == 0) PASS(); else FAIL(val);
}

static void test_hash_del(void) {
    HashTable *ht = hash_init();

    TEST("hash_del returns removed value");
    hash_set(ht, "nome", "davide");
    char *old = hash_del(ht, "nome");
    if (old != NULL && strcmp(old, "davide") == 0) PASS(); else FAIL("wrong value");
    free(old);

    TEST("hash_get returns NULL after hash_del");
    char *val = hash_get(ht, "nome");
    if (val == NULL) PASS(); else FAIL("key still exists");

    TEST("hash_del non-existent key returns NULL");
    old = hash_del(ht, "inesistente");
    if (old == NULL) PASS(); else FAIL("expected NULL");
}

static void test_hash_collision(void) {
    HashTable *ht = hash_init();

    /* Inserisce due chiavi che collidono nello stesso bucket */
    TEST("collision: two keys in same bucket");
    hash_set(ht, "n1", "davide");
    hash_set(ht, "n2", "martina");
    char *v1 = hash_get(ht, "n1");
    char *v2 = hash_get(ht, "n2");
    if (v1 != NULL && v2 != NULL &&
        strcmp(v1, "davide") == 0 && strcmp(v2, "martina") == 0)
        PASS(); else FAIL("collision handling broken");

    TEST("collision: del first keeps second");
    hash_del(ht, "n1");
    v2 = hash_get(ht, "n2");
    if (v2 != NULL && strcmp(v2, "martina") == 0) PASS(); else FAIL("second key lost");

    TEST("collision: del second keeps first");
    hash_set(ht, "n1", "davide");
    hash_del(ht, "n2");
    v1 = hash_get(ht, "n1");
    if (v1 != NULL && strcmp(v1, "davide") == 0) PASS(); else FAIL("first key lost");
}

static void test_hash_many(void) {
    HashTable *ht = hash_init();
    char key[16], val[16];

    TEST("insert and retrieve 1000 keys");
    for (int i = 0; i < 1000; i++) {
        sprintf(key, "key_%d", i);
        sprintf(val, "val_%d", i);
        hash_set(ht, key, val);
    }
    int ok = 1;
    for (int i = 0; i < 1000 && ok; i++) {
        sprintf(key, "key_%d", i);
        sprintf(val, "val_%d", i);
        char *v = hash_get(ht, key);
        if (v == NULL || strcmp(v, val) != 0) ok = 0;
    }
    if (ok) PASS(); else FAIL("mismatch");
}

static void test_hash_null_safety(void) {
    TEST("hash_set with NULL table does not crash");
    hash_set(NULL, "k", "v");
    PASS();

    TEST("hash_get with NULL table returns NULL");
    char *val = hash_get(NULL, "k");
    if (val == NULL) PASS(); else FAIL("expected NULL");

    TEST("hash_del with NULL table returns NULL");
    char *old = hash_del(NULL, "k");
    if (old == NULL) PASS(); else FAIL("expected NULL");
}

int main(void) {
    printf("\n=== Hash Table Tests ===\n\n");

    test_hash_init();
    test_hash_set_get();
    test_hash_set_overwrite();
    test_hash_del();
    test_hash_collision();
    test_hash_many();
    test_hash_null_safety();

    printf("\n  %d/%d passed\n\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}