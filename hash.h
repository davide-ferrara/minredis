#ifndef HASH_H
#define HASH_H

#include "log.h"
#include <pthread.h>
#include <stddef.h>

#define NUM_BUCKETS 256

typedef struct HashEntry {
  char *key;
  char *value;
  struct HashEntry *next;
} HashEntry;

typedef struct {
  HashEntry *buckets[NUM_BUCKETS];
  pthread_rwlock_t bucket_lock[NUM_BUCKETS];
} HashTable;

HashTable *hash_init(void);
int hash_set(HashTable *ht, char *k, char *v);
char *hash_get(HashTable *ht, char *k);
char *hash_del(HashTable *ht, char *k);
size_t hash_count(HashTable *ht);
void hash_print(HashTable *ht);

size_t hash_fn(char *str);

#endif
