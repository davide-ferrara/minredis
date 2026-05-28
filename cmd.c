#include "cmd.h"
#include "hash.h"
#include "log.h"
#include "reply.h"
#include "serializer.h"
#include "strbuf.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define CMD_ERR_MSG_UNKNOWN "unknown command"

/* =========================================================================
 * cmd_init() — alloca e inizializza una struttura Cmd.
 *
 * Alloca Cmd e il suo array Tokens con capacita' iniziale TOKENS_INIT_CAP.
 * Il campo entry parte come CMD_UNKNOWN: sara' parse_tokens() a impostare
 * il codice comando dopo aver estratto il primo token.
 * ========================================================================= */
Cmd *cmd_init(void) {
  Cmd *cmd = malloc(sizeof(Cmd));
  if (cmd == NULL) return NULL;
  memset(cmd, 0, sizeof(Cmd));
  cmd->tokens = malloc(sizeof(Tokens));
  if (cmd->tokens == NULL) { free(cmd); return NULL; }
  memset(cmd->tokens, 0, sizeof(Tokens));
  cmd->tokens->buf = malloc(TOKENS_INIT_CAP * sizeof(char *));
  if (cmd->tokens->buf == NULL) { free(cmd->tokens); free(cmd); return NULL; }
  memset(cmd->tokens->buf, 0, TOKENS_INIT_CAP * sizeof(char *));
  cmd->tokens->cap = TOKENS_INIT_CAP;
  cmd->entry = CMD_UNKNOWN;
  return cmd;
}

/* =========================================================================
 * cmd_reset() — azzera il comando per il riuso nel loop principale.
 * ========================================================================= */
void cmd_reset(Cmd *cmd) {
  cmd->tokens->count = 0;
  cmd->ready = 0;
  cmd->entry = CMD_UNKNOWN;
}

/* =========================================================================
 * cmd_free() — dealloca un Cmd e tutti i suoi token.
 * ========================================================================= */
void cmd_free(Cmd *cmd) {
  if (cmd == NULL) return;
  if (cmd->tokens != NULL) {
    free(cmd->tokens->buf);
    free(cmd->tokens);
  }
  free(cmd);
}

/* =========================================================================
 * cmd_parse() — punto d'ingresso del parser.
 *
 * Delega a parse_tokens() la scansione del buffer e il popolamento
 * di cmd->tokens e cmd->entry. Ritorna il numero di byte consumati.
 * ========================================================================= */
int cmd_parse(Cmd *cmd, StringBuffer *raw_cmd) {
  if (cmd == NULL || raw_cmd == NULL) {
    log_error("cmd_parse: cmd or raw_cmd are NULLs");
    return 0;
  }
  cmd_reset(cmd);
  return (int)parse_tokens(cmd, raw_cmd);
}

/* =========================================================================
 * cmd_quit() — chiude la connessione del client.
 *
 * Se il client socket == -1 (già chiuso), esce subito.
 * Se ci sono argomenti extra oltre a QUIT, risponde con errore.
 * Altrimenti manda OK, chiude il socket, decrementa clients.count
 * (protetto da server->rwlock) e logga l'uscita.
 *
 *   QUIT          → chiude la connessione
 *   QUIT x        → ERR wrong number of arguments
 *
 * Ritorna 0 ok, -1 errore interno (server NULL, close fallita).
 * ========================================================================= */
int cmd_quit(Server *server, Cmd *cmd, Client *client, ServerReply *r) {
  if (server == NULL) {
    log_error("close_client: server NULL"); return -1;
  }

  if (client->sock == -1) {
    log_error("close_client: fd non valido"); return -1;
  }

  if (cmd->tokens->count > 1) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"QUIT\" errati");
    return 0;
  }

  log_info("Chiusura connessione %s:%d", client->ip, client->port);

  reply_ok(r, cmd, client);
  if (close(client->sock) == -1) {
    log_error("Chiusura fallita per %s:%d: %s", client->ip, client->port,
              strerror(errno));
    return -1;
  }
  client->sock = -1;

  /* Decremento atomico del contatore client attivi */
  pthread_rwlock_wrlock(&(server->rwlock));
  server->clients.count--;
  pthread_rwlock_unlock(&(server->rwlock));

  log_info("Connessione chiusa %s:%d, numero di client: %d", client->ip, 
           client->port, server->clients.count);
  return 0;
}

/* =========================================================================
 * cmd_incr() — incrementa di 1 il valore di una chiave.
 *
 * Acquisisce il bucket_lock in SCRITTURA perché il valore viene
 * letto e riscritto (read-modify-write atomico).
 *
 * Se la chiave non esiste, risponde 0 senza crearla.
 *
 * TODO:
 * Il valore viene convertito con atoi: se non è un intero valido
 * (Da usare solo su stringhe numerica attualemnte non è il massimo!)
 *
 * INCR n   → 42  (valore incrementato e restituito)
 * INCR n   → 43
 * ========================================================================= */
int cmd_incr(Server *server, Cmd *cmd, Client *client, ServerReply *r) {
  if (server == NULL) {
    log_error("close_client: server NULL"); return -1;
  }

  if (client->sock == -1) {
    log_error("close_client: fd non valido"); return -1;
  }

  if (cmd->tokens->count != 2) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"INCR\" errati");
    return 0;
  }

  char **key = &(cmd->tokens->buf[1]);
  char *val;

  log_debug("INCR %s, da %s:%d", *key, client->ip, client->port);

  HashTable *ht = &(server->database.tables[client->db]);
  size_t idx = hash_fn(*key);

  /* Lock in scrittura: get + set atomici sullo stesso bucket.
   * Altrimenti tra il rilascio del lock dopo get e l'aquisizione del lock
   * con set un altro thread potrebbe scrivere.
   * */
  pthread_rwlock_wrlock(&ht->bucket_lock[idx]);
  val = hash_get(ht, *key);
  if (val == NULL) {
    pthread_rwlock_unlock(&ht->bucket_lock[idx]);
    reply_integer(r, cmd, client, 0);
    return 0;
  }

  /* Conversione (non fail proof) */
  ssize_t n = atoi(val);
  char buf[32];
  snprintf(buf, sizeof(buf), "%ld", ++n);

  hash_set(ht, *key, buf);
  pthread_rwlock_unlock(&ht->bucket_lock[idx]);

  reply_integer(r, cmd, client, n);
  log_info("INCR %s -> %ld, da %s:%d", *key, n, client->ip, client->port);
  return 0;
}

/* =========================================================================
 * cmd_set() — scrive una chiave con un valore.
 *
 * Il valore può contenere spazi (es. "SET msg ciao mondo").
 * I token dal terzo in poi vengono ricostruiti in un unico string buffer
 * unendoli con uno spazio come separatore.
 *
 * Acquisisce il bucket_lock in SCRITTURA sul bucket idx = hash_fn(key).
 * Se la chiave esiste già, il vecchio valore viene sovrascritto.
 *
 *   SET nome Davide          → OK
 *   SET frase ciao a tutti   → OK  (valore = "ciao a tutti")
 * ========================================================================= */
int cmd_set(Server *server, Cmd *cmd, Client *client, ServerReply *r) {
  if (server == NULL) {
    log_error("close_client: server NULL"); return -1;
  }

  if (client->sock == -1) {
    log_error("close_client: fd non valido"); return -1;
  }

  if (cmd->tokens->count < 3) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"SET\" errati");
    return 0;
  }

  size_t len = 0;
  for (size_t t = 2; t < cmd->tokens->count; t++) {
    len += strlen(cmd->tokens->buf[t]);
  }

  /* Ricostruisco il valore unendo i token rimanenti con spazi */
  StringBuffer *str = strbuf_new(len + cmd->tokens->count, cmd->tokens->buf[2]);
  for (size_t t = 3; t < cmd->tokens->count; t++) {
    strbuf_append(str, cmd->tokens->buf[t], " ");
  }

  char **key = &(cmd->tokens->buf[1]);
  char **value = &(str->buf);

  log_debug("SET %s:%s, da %s:%d", *key, *value, client->ip, client->port);

  HashTable *ht = &(server->database.tables[client->db]);
  size_t idx = hash_fn(*key);

  /* Scrittura atomica */
  pthread_rwlock_wrlock(&ht->bucket_lock[idx]);
  hash_set(ht, *key, *value); // Critica
  pthread_rwlock_unlock(&ht->bucket_lock[idx]);

  reply_ok(r, cmd, client);
  log_info("SET %s, da %s:%d", *key, client->ip, client->port);
  return 0;
}

/* =========================================================================
 * cmd_get() — recupera il valore di una chiave.
 *
 * Acquisisce il bucket_lock in LETTURA. Più GET sullo stesso bucket
 * possono procedere in parallelo; solo una scrittura le blocca.
 *
 *   GET nome         → $6\r\nDavide\r\n   (bulk string)
 *   GET inesistente  → $-1\r\n            (null bulk)
 * ========================================================================= */
int cmd_get(Server *server, Cmd *cmd, Client *client, ServerReply *r) {
  if (server == NULL) {
    log_error("close_client: server NULL"); return -1;
  }

  if (client->sock == -1) {
    log_error("close_client: fd non valido"); return -1;
  }

  if (cmd->tokens->count != 2) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"GET\" errati");
    return 0;
  }

  char **key = &(cmd->tokens->buf[1]);

  HashTable *ht = &(server->database.tables[client->db]);
  size_t idx = hash_fn(*key);

  /* Lettura in parallelo, solo una scrittura puó bloccarlo */
  pthread_rwlock_rdlock(&ht->bucket_lock[idx]);
  char *value = hash_get(ht, *key); // Critica solo se un thread scrive
  pthread_rwlock_unlock(&ht->bucket_lock[idx]);

  reply_bulk_str(r, cmd, client, value);
  log_info("GET %s, da %s:%d", *key, client->ip, client->port);
  return 0;
}

/* =========================================================================
 * cmd_del() — cancella una chiave dal database.
 *
 * Acquisisce il bucket_lock in SCRITTURA. Se la chiave viene trovata
 * e rimossa, risponde :1 (integer), altrimenti :0.
 *
 * DEL foo   → :1
 * DEL bar   → :0   (chiave inesistente)
 * ========================================================================= */
int cmd_del(Server *server, Cmd *cmd, Client *client, ServerReply *r) {
  if (server == NULL) {
    log_error("close_client: server NULL"); return -1;
  }
  if (client->sock == -1) {
    log_error("close_client: fd non valido"); return -1;
  }

  if (cmd->tokens->count != 2) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"DEL\" errati");
    return 0;
  }

  HashTable *ht = &(server->database.tables[client->db]);
  char **key = &(cmd->tokens->buf[1]);
  size_t idx = hash_fn(*key);

  /* Lock di lettura e scrittura */
  pthread_rwlock_wrlock(&ht->bucket_lock[idx]);
  char *deleted = hash_del(ht, *key); // Critico sempre
  pthread_rwlock_unlock(&ht->bucket_lock[idx]);

  if (deleted == NULL) {
    reply_integer(r, cmd, client, 0);
    log_info("DEL %s (non trovata), da %s:%d", *key, client->ip, client->port);
    return 0;
  }

  /* hash_del restituisce il valore, dobbiamo liberarlo noi */
  free(deleted);
  reply_integer(r, cmd, client, 1);
  log_info("DEL %s, da %s:%d", *key, client->ip, client->port);
  return 0;
}

/* =========================================================================
 * cmd_select() — seleziona il database per la connessione corrente.
 *
 * Il database scelto viene salvato in client->db ed è valido per
 * tutte le operazioni successive di questa connessione.
 * Ogni client ha il proprio db indipendente (niente lock necessario).
 *
 * SELECT 0   → OK   (database 0)
 * SELECT 15  → OK   (database 15, massimo)
 * SELECT 16  → ERR  (fuori range 0..MAX_DB)
 * ========================================================================= */
int cmd_select(Server *server, Cmd *cmd, Client *client, ServerReply *r) {
  if (server == NULL) {
    log_error("close_client: server NULL"); return -1;
  }
  if (client->sock == -1) {
    log_error("close_client: fd non valido"); return -1;
  }

  if (cmd->tokens->count != 2) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"SELECT\" errati");
    return 0;
  }

  int sel = atoi(cmd->tokens->buf[1]);
  log_debug("cmd_select: selected db %d", sel);
  if (sel < 0 || sel > MAX_DB) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"SELECT\" errati");
    r->error = 1;
    return 0;
  }

  /* Assegnazione per-client: nessun lock richiesto, diverso per ogni client */
  client->db = sel;

  reply_ok(r, cmd, client);
  log_info("SELECT db=%d, da %s:%d", sel, client->ip, client->port);
  return 0;
}

/* =========================================================================
 * cmd_info() — restituisce statistiche del server (solo debug).
 *
 * Stampa porta, numero client connessi, database selezionato e
 * numero di chiavi nel database corrente.
 * IMPORTANTE: non è thread safe (solo per debug)
 *
 * INFO   → $58\r\n# Server\r\nport:5050\r\nclients:1\r\n...
 * ========================================================================= */
int cmd_info(Server *server, Cmd *cmd, Client *client, ServerReply *r) {
  if (server == NULL) {
    log_error("cmd_info: server NULL"); return -1;
  }
  if (client->sock == -1) {
    log_error("cmd_info: fd non valido"); return -1;
  }

  if (cmd->tokens->count > 1) {
    reply_err(r, cmd, client,
              "numero di argomenti per il comando \"INFO\" errati");
    return 0;
  }

  size_t sel = client->db;
  size_t keys = hash_count(&server->database.tables[sel]);

  char *fmt =
      "# Server\r\nport:%d\r\nclients:%d\r\nselected_db:%zu\r\nkeys:%zu";
  size_t buf_size =
      snprintf(NULL, 0, fmt, server->port, server->clients.count, sel, keys) +
      1;

  char *info = malloc(buf_size);
  if (info == NULL) {
    log_error("Impossibile allocare info: %s", strerror(errno));
    return -1;
  }
  snprintf(info, buf_size, fmt, server->port, server->clients.count, sel, keys);

  reply_bulk_str(r, cmd, client, info);
  log_info("INFO (db=%zu keys=%zu), da %s:%d", sel, keys, client->ip, client->port);
  free(info);
  return 0;
}

/* =========================================================================
 * cmd_save() — serializza tutti i database su file.
 *
 * Se non viene passato un percorso, usa il default "dump.minredis".
 * La scrittura avviene tramite rdb_save() che acquisisce tutti
 * i rdlock dei bucket per garantire uno snapshot consistente.
 *
 * SAVE dump.minredis    → OK
 * SAVE                  → OK  (usa dump.minredis)
 * ========================================================================= */
int cmd_save(Cmd *cmd, Server *server, Client *client, ServerReply *r) {
  char **path = &(cmd->tokens->buf[1]);
  if (*path == NULL) log_info("Nessun nome del file, default: dump.minredis\n");

  rdb_save(server, *path);

  reply_ok(r, cmd, client);
  log_info("SAVE completato, da %s:%d", client->ip, client->port);
  return 0;
}

/* =========================================================================
 * cmd_load() — carica lo stato da file RDB.
 *
 * Acquisisce TUTTI i bucket_lock in scrittura prima di chiamare rdb_load().
 * Questo blocca ogni operazione di lettura e scrittura sul server 
 * durante il caricamento.
 *
 * LOAD dump.minredis   → OK
 * LOAD                 → OK  (usa dump.minredis)
 * ========================================================================= */
int cmd_load(Cmd *cmd, Server *server, Client *client, ServerReply *r) {
  char **path = &(cmd->tokens->buf[1]);
  if (*path == NULL) log_info("Nessun nome del file, default: dump.minredis\n");

  /* Blocco globale in scrittura: nessuno può leggere/scrivere durante */
  for (int db = 0; db < NUM_DBS; db++) {
    HashTable *ht = &(server->database.tables[db]);
    for (int i = 0; i < NUM_BUCKETS; i++)
      pthread_rwlock_wrlock(&ht->bucket_lock[i]);
  }

  rdb_load(server, *path);

  /* Rilascio tutti i lock dei bucket */
  for (int db = 0; db < NUM_DBS; db++) {
    HashTable *ht = &(server->database.tables[db]);
    for (int i = 0; i < NUM_BUCKETS; i++)
      pthread_rwlock_unlock(&ht->bucket_lock[i]);
  }

  reply_ok(r, cmd, client);
  log_info("LOAD completato, da %s:%d", client->ip, client->port);
  return 0;
}

/* =========================================================================
 * cmd_dispatch() — smista il comando in base a cmd->entry.
 *
 * È il dispatcher centrale chiamato dal loop di ricezione in server.c.
 * Controlla che il comando sia pronto (cmd->ready == 1): se il parser
 * non ha ancora ricevuto il CR+LF finale, il comando è incompleto e
 * viene ignorato (ritorna 2).
 *
 * Ritorna:
 *   0  — comando eseguito (con o senza errori di protocollo)
 *   1  — comando incompleto (non ancora pronto)
 *  -1  — cmd NULL (errore interno)
 * ========================================================================= */
int cmd_dispatch(Cmd *cmd, Server *server, Client *client) {
  if (cmd == NULL) {
    log_error("cmd_dispatch: cmd NULL");return -1;
  }
  if (!cmd->ready) {
    log_debug("Comando incompleto da %s:%d", client->ip, client->port); return 1;
  }

  ServerReply *r = reply_new();

  switch (cmd->entry) {
  case CMD_GET:
    cmd_get(server, cmd, client, r);
    break;
  case CMD_SET:
    cmd_set(server, cmd, client, r);
    break;
  case CMD_DEL:
    cmd_del(server, cmd, client, r);
    break;
  case CMD_QUIT:
    cmd_quit(server, cmd, client, r);
    break;
  case CMD_SELECT:
    cmd_select(server, cmd, client, r);
    break;
  case CMD_SAVE:
    cmd_save(cmd, server, client, r);
    break;
  case CMD_LOAD:
    cmd_load(cmd, server, client, r);
    break;
  case CMD_INFO:
    cmd_info(server, cmd, client, r);
    break;
  case CMD_INCR:
    cmd_incr(server, cmd, client, r);
    break;
  default:
    log_info("Comando sconosciuto, da %s:%d", client->ip, client->port);
    reply_err(r, cmd, client, CMD_ERR_MSG_UNKNOWN);
    break;
  }

  reply_free(r);
  return 0;
}
