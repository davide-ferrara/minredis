#include "hash.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * hash_count() — conta gli elementi nell'hash table.
 *
 * Scorre tutti i 256 bucket e somma le entry di ogni lista.
 * IMPORTANTE: non acquisisce lock internamente è compito del chiamante
 * proteggere la lettura con i bucket_lock necessari !!!
 * ========================================================================= */
size_t hash_count(HashTable *ht) {
  if (ht == NULL) return 0;

  size_t count = 0;
  for (int i = 0; i < NUM_BUCKETS; i++) {
    HashEntry *cur = ht->buckets[i];
    while (cur != NULL) {
      count++; cur = cur->next;
    }
  }
  return count;
}

/* =========================================================================
 * hash_entry_new() — alloca una nuova HashEntry con copia di chiave e valore.
 *
 * Copia key e value, restituisce il puntatore al nodo allocato,
 * con next = NULL (new_node -> NULL).
 * Restituisce NULL se key e value sono NULL o se malloc fallisce.
 * ========================================================================= */
static HashEntry *hash_entry_new(char *k, char *v) {
  if (k == NULL || v == NULL) return NULL;

  HashEntry *e = malloc(sizeof(HashEntry));
  if (e == NULL) return NULL;

  e->key = malloc(strlen(k) + 1);
  e->value = malloc(strlen(v) + 1);
  strcpy(e->key, k);
  strcpy(e->value, v);
  e->next = NULL;

  return e;
}

/* =========================================================================
 * hash_init() — alloca e inizializza una HashTable.
 *
 * Alloca la struttura con malloc + memset (bucket NULL e lock a zero),
 * poi inizializza uno per uno i (NUM_BUCKETS) pthread_rwlock_t.
 * Se init di un lock fallisce, stampa errore e restituisce NULL.
 * ========================================================================= */
HashTable *hash_init(void) {
  HashTable *ht = malloc(sizeof(HashTable));
  if (ht == NULL) return NULL;
  memset(ht, 0, sizeof(HashTable));
  for (size_t l = 0; l < NUM_BUCKETS; l++) {
    int init = pthread_rwlock_init(&(ht->bucket_lock[l]), NULL);
    if (init < 0) {
      log_error("hash_init: rwlock init fallito!");
      return NULL;
    }
  }
  return ht;
}

/* =========================================================================
 * hash_set() — inserisce (o aggiorna) una chiave nell'hash table.
 *
 * Cerca la chiave k nel bucket idx = hash_fn(k).
 * Se la chiave esiste già, sovrascrive il valore.
 * Se non esiste, alloca una nuova entry e la inserisce in testa
 * alla lista del bucket.
 *
 * IMPORTANTE: questa funzione non acquisisce alcun lock.
 *             Il chiamante DEVE aver già lockato in scrittura.
 *
 * Ritorna:
 *   0  — nuova chiave inserita, chiave già esistente (aggiornata)
 *  -1  — errore (ht NULL)
 * ========================================================================= */
int hash_set(HashTable *ht, char *k, char *v) {
  if (ht == NULL)
    return -1;

  unsigned long idx = hash_fn(k);

  HashEntry *cur = ht->buckets[idx];

  /* Cerco se la chiave esiste già */
  while (cur != NULL) {
    if (strcmp(cur->key, k) == 0) {
      /* Stessa chiave: free vecchio valore, copio il nuovo */
      free(cur->value);
      cur->value = malloc(strlen(v) + 1);
      strcpy(cur->value, v);
      return 0;
    }
    cur = cur->next;
  }

  /* Nuova chiave: alloco entry e la metto in testa alla lista */
  HashEntry *e = hash_entry_new(k, v);
  if (e == NULL) {
    return -1;
  }

  e->next = ht->buckets[idx]; /* la nuova entry punta alla vecchia testa */
  ht->buckets[idx] = e;       /* la testa del bucket diventa la nuova entry */

  return 0;
}

/* =========================================================================
 * hash_get() — recupera il valore associato a una chiave.
 *
 * Cerca k nel bucket idx = hash_fn(k) scorrendo la lista.
 * Non acquisisce lock internamente: il chiamante deve aver lockato
 * in LETTURA (o scrittura se sta anche modificando).
 *
 * Ritorna il puntatore al valore (non copia) o NULL.
 * ========================================================================= */
char *hash_get(HashTable *ht, char *k) {
  if (ht == NULL)
    return NULL;

  unsigned long idx = hash_fn(k);
  HashEntry *cur = ht->buckets[idx];

  while (cur != NULL) {
    if (strcmp(k, cur->key) == 0)
      return cur->value;
    cur = cur->next;
  }

  return NULL;
}

/* =========================================================================
 * hash_del() — rimuove una chiave dall'hash table.
 *
 * Cerca k nel bucket idx = hash_fn(k). Se la trova, la scollega
 * dalla lista, libera il nodo e la chiave, ma NON libera il valore:
 * restituisce il puntatore al valore al chiamante, che dovrà
 * chiamare free() quando non serve più.
 *
 * IMPORTANTE: il chiamante deve aver lockato bucket_lock[idx]
 *             in SCRITTURA prima di invocare questa funzione.
 *
 * Ritorna: puntatore al valore (da liberare) o NULL se non trovato.
 * ========================================================================= */
char *hash_del(HashTable *ht, char *k) {
  if (ht == NULL)
    return NULL;

  unsigned long idx = hash_fn(k);
  HashEntry *cur = ht->buckets[idx];
  HashEntry *prev = NULL;

  while (cur != NULL) {
    if (strcmp(cur->key, k) == 0) {
      /* Scollego il nodo dalla lista */
      if (prev == NULL)
        ht->buckets[idx] = cur->next; /* rimuovo la testa */
      else
        prev->next = cur->next; /* salto il nodo nel mezzo */

      char *old = cur->value;
      free(cur->key);
      free(cur);
      return old;
    }
    prev = cur;
    cur = cur->next;
  }

  return NULL;
}

/* =========================================================================
 * hash_print() — stampa tutte le entry dell'hash table su stdout.
 *
 * Usata solo per debug.
 * IMPORTANTE: non acquisisce lock (non safe thread).
 * ========================================================================= */
void hash_print(HashTable *ht) {
  if (ht == NULL)
    return;

  for (int i = 0; i < NUM_BUCKETS; i++) {
    HashEntry *cur = ht->buckets[i];
    while (cur != NULL) {
      printf("%s -> %s\n", cur->key, cur->value);
      cur = cur->next;
    }
  }
}

/* =========================================================================
 * hash_fn() — funzione di hash DJB2.
 * Algoritmo di Dan Bernstein.
 * Source: https://gist.github.com/MohamedTaha98/ccdf734f13299efb73ff0b12f7ce429f
 *
 * È pubblica (dichiarata in hash.h) perché i comandi in cmd.c
 * devono calcolare l'indice del bucket per acquisire il lock giusto.
 * ========================================================================= */
size_t hash_fn(char *str) {
  unsigned long hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  return hash % NUM_BUCKETS;
}
