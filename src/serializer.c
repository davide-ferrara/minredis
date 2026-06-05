#include "serializer.h"
#include "hash.h"
#include "log.h"
#include "server.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/*
 * Formato binario dump.minredis (xxd dump.minredis):
 * ┌───────────────────────────────────────────────────┐
 * │ MINREDIS          8 byte magic (ASCII)            │
 * │ 0x01              1 byte version                  │
 * ├───────────────────────────────────────────────────┤
 * │ 0xFE              1 byte opcode SELECTDB          │
 * │ 0x00              1 byte db number (=0)           │
 * ├───────────────────────────────────────────────────┤
 * │ 0x00              1 byte TYPE_STRING              │
 * │ 00 00 00 03       len=3 (big-endian)              │
 * │ 6d 73 67          "msg"                           │
 * │ 00 00 00 0e       len=14 (big-endian)             │
 * │ 63 69 61 6f ...   "ciao come stai"                │
 * ├───────────────────────────────────────────────────┤
 * │ 0x00              1 byte TYPE_STRING              │
 * │ 00 00 00 04       len=4                           │
 * │ 6e 6f 6d 65       "nome"                          │
 * │ 00 00 00 06       len=6                           │
 * │ 64 61 76 69 64 65 "davide"                        │
 * ├───────────────────────────────────────────────────┤
 * │ 0xFE              1 byte opcode SELECTDB          │
 * │ 0x01              1 byte db number (=1)           │
 * ├───────────────────────────────────────────────────┤
 * │ 0x00              1 byte TYPE_STRING              │
 * │ 00 00 00 08       len=8                           │
 * │ 63 6f 6c 6f 72 65 "colore"                        │
 * │ 00 00 00 05       len=5                           │
 * │ 72 6f 73 73 6f    "rosso"                         │
 * ├───────────────────────────────────────────────────┤
 * │ 0xFF              1 byte EOF                      │
 * └───────────────────────────────────────────────────┘
 */

/* =========================================================================
 * write_len() — scrive una lunghezza come uint32 big-endian.
 *
 * Converte un size_t nei 4 byte in network order e li scrive su fp.
 * L'ordine big-endian garantisce che il file sia leggibile su
 * qualsiasi architettura (x86, ARM, etc.).
 * Non è obbligatorio, fatto per imparare lo shifting...
 *
 * len=3   →  fwrite({0x00, 0x00, 0x00, 0x03})
 * len=14  →  fwrite({0x00, 0x00, 0x00, 0x0E})
 * ========================================================================= */
static void write_len(FILE *fp, size_t len) {
  uint32_t n = (uint32_t)len;
  unsigned char be[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16),
                         (unsigned char)(n >> 8), (unsigned char)(n & 0xFF)};
  fwrite(be, 1, 4, fp);
}

/* =========================================================================
 * rdb_save() — serializza l'intero database su file binario.
 *
 * Scrive un header con magic "MINREDIS" + versione, poi itera tutti
 * i 16 database. Per ogni database non vuoto scrive un opcode SELECTDB
 * seguito dal numero del database e da tutte le entry:
 *
 *   [TYPE_STRING] [key_len:4 BE] [key: ...] [val_len:4 BE] [val: ...]
 *
 * Alla fine scrive OP_EOF e chiude il file.
 *
 * LOCKING: prima di iterare acquisisce TUTTI i rdlock (256 bucket ×
 * 16 db = 4096 lock), garantendo uno snapshot consistente. I rdlock
 * permettono alle GET di proseguire in parallelo durante il salvataggio;
 * solo le scritture sono bloccate dal wrlock sul singolo bucket.
 *
 * Se path è NULL, usa il default RDB_PATH ("dump.minredis").
 * ========================================================================= */
int rdb_save(Server *server, const char *path) {
  if (server == NULL) return -1;

  if (path == NULL) path = RDB_PATH;

  FILE *fp = fopen(path, "wb");
  if (fp == NULL) return -1;

  /* Header: magic 8 byte + version 1 byte */
  fwrite(RDB_MAGIC, 1, 8, fp);
  unsigned char ver = RDB_VERSION;
  fwrite(&ver, 1, 1, fp);

  /* rdlock su tutti i bucket */
  for (int db = 0; db < NUM_DBS; db++) {
    HashTable *ht = &(server->database.tables[db]);
    for (int i = 0; i < NUM_BUCKETS; i++)
      pthread_rwlock_rdlock(&ht->bucket_lock[i]);
  }

  /* Itera su tutti i database */
  for (int curr_db = 0; curr_db < NUM_DBS; curr_db++) {
    HashTable *ht = &(server->database.tables[curr_db]);

    /* Saltiamo database vuoti */
    if (hash_count(ht) == 0) continue;

    /* Scrive OP_SELECTDB + idx database corrente */
    unsigned char op = OP_SELECTDB;
    fwrite(&op, 1, 1, fp);
    unsigned char db_byte = (unsigned char)curr_db;
    fwrite(&db_byte, 1, 1, fp);

    /* Scriviamo ogni entry del database corrente */
    for (int i = 0; i < NUM_BUCKETS; i++) {
      HashEntry *cur = ht->buckets[i];
      while (cur != NULL) {
        unsigned char type = TYPE_STRING;
        fwrite(&type, 1, 1, fp);

        size_t klen = strlen(cur->key);
        write_len(fp, klen);
        fwrite(cur->key, 1, klen, fp);

        size_t vlen = strlen(cur->value);
        write_len(fp, vlen);
        fwrite(cur->value, 1, vlen, fp);

        cur = cur->next;
      }
    }
  }

  /* Sblocchiamo tutti i rdlock */
  for (int db = 0; db < NUM_DBS; db++) {
    HashTable *ht = &(server->database.tables[db]);
    for (int i = 0; i < NUM_BUCKETS; i++)
      pthread_rwlock_unlock(&ht->bucket_lock[i]);
  }

  /* End of file */
  unsigned char eof = OP_EOF;
  fwrite(&eof, 1, 1, fp);

  fclose(fp);

  log_info("rdb_save: salvato %s", path);
  return 0;
}

/* =========================================================================
 * rdb_load() — ripristina lo stato del database da file binario.
 *
 * IMPORTANTE — questa funzione NON acquisisce lock internamente.
 * Il chiamante DEVE aver già lockato tutti i bucket_lock in scrittura:
 *   - all'avvio (server_run): nessun client attivo, safe senza lock
 *   - cmd_load: locka tutti i 4096 bucket_lock prima di chiamarci
 *
 * Legge header (magic + version), poi entra in un loop di opcode:
 *   OP_SELECTDB  → imposta il database corrente per le entry successive
 *   TYPE_STRING  → legge key_len, key, val_len, val da file e chiama
 *                   hash_set() per popolare la hash table
 *   OP_EOF       → fine del dump, esce dal loop
 *
 * Le lunghezze nel file sono in big-endian (network order).
 * Su x86 (little-endian) i byte vanno invertiti con shift aritmetico:
 *
 *   File (big-endian):     00 00 00 0E   (len = 14)
 *   buf[0..3] dopo fread:  00 00 00 0E   (fread scrive in ordine)
 *   Ricostruzione:         buf[0]<<24 | buf[1]<<16 | buf[2]<<8 | buf[3]
 *                          = 0x0000000E = 14
 *
 * Dopo hash_set(), key e value vengono liberati con free() perché
 * hash_set fa una copia interna dei dati (vedi hash_entry_new).
 * ========================================================================= */
int rdb_load(Server *server, const char *path) {
  if (server == NULL) return -1;
  if (path == NULL) path = RDB_PATH;

  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    log_warn("rdb_load: dump non trovato!"); return -1;
  }

  /* === LETTURA HEADER === */

  RdbHeader hdr = {0};
  uint8_t opcode, selected_db;
  size_t n = fread(hdr.magic, 1, 8, fp);
  if (n != 8) {
    log_error("rdb_load: header non valido!");
    fclose(fp);
    return -1;
  }
  if (memcmp(hdr.magic, RDB_MAGIC, 8) != 0) {
    log_error("rdb_load: header non valido!");
    fclose(fp);
    return -1;
  }

  n = fread(&hdr.version, 1, 1, fp);
  if (n != 1) {
    log_error("rdb_load: header non valido!");
    fclose(fp);
    return -1;
  }

  log_debug("Magic: %.8s", hdr.magic);
  log_debug("Version: %d", hdr.version);

  /* === LOOP OPCODE === */

  while (1) {
    n = fread(&opcode, 1, 1, fp);
    if (n != 1) {
      log_error("rdb_load: opcode non valido!");
      fclose(fp);
      return -1;
    }

    switch (opcode) {
    case OP_EOF:
      log_debug("rdb_load: caricamento in memoria completato");
      fclose(fp);
      return 0;
    case OP_SELECTDB:
      n = fread(&selected_db, 1, 1, fp);
      if (n != 1) {
        log_error("rdb_load: selected db non valido!");
        fclose(fp);
        return -1;
      }
      log_debug("rdb_load: reading db %d", selected_db);
      break;
    case TYPE_STRING: {
      unsigned char buf[4];
      uint32_t len;

      /* Leggiamo 4 byte big-endian e li convertiamo a host order */
      fread(buf, 1, 4, fp);
      len = (buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3]);

      /* Chiave: len byte + terminatore */
      char *key = malloc(len + 1);
      fread(key, 1, len, fp);
      key[len] = '\0';

      /* Valore: len byte + terminatore */
      fread(buf, 1, 4, fp);
      len = (buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3]);
      char *value = malloc(len + 1);
      fread(value, 1, len, fp);
      value[len] = '\0';

      log_debug("%s:%s", key, value);

      /* hash_set copia key e value, possiamo liberare gli originali */
      hash_set(&server->database.tables[selected_db], key, value);
      free(key);
      free(value);
      break;
    }
    default:
      /* Opcode sconosciuto: ignoriamo e continuiamo */
      break;
    }
  }
}
