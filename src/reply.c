#include "reply.h"
#include "log.h"
#include "parser.h"
#include "server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * SET nome davide    →  +OK
 * GET nome           →  "davide"
 * GET xyz            →  $-1
 * DEL nome           →  :1
 * DEL xyz            →  :0
 * QUIT               →  +OK  (poi chiude)
 * FOOBAR             →  -ERR unknown command
 * SET key            →  -ERR wrong number of arguments for 'SET' command
 * I prefissi del protocollo RESP:
 * -
 * + simple string (OK, errori)
 * -
 * $ bulk string ($-1 = nil/null, $5\r\nhello\r\n = stringa di 5 byte)
 * -
 * : integer (count di DEL)
 * -
 * - error (-ERR unknown command)
 * Negli esempi sopra ho omesso \r\n per leggibilità, ma nella realtà ogni
 * risposta finisce con CRLF.
 * */

ServerReply *reply_new(void) {
  ServerReply *r = malloc(sizeof(ServerReply));
  if (r == NULL) {
    log_error("Impossibile allocare reply: %s", strerror(errno));
    return NULL;
  }
  memset(r, 0, sizeof(ServerReply));
  r->error = 1;
  return r;
}

void reply_free(ServerReply *r) {
  if (r == NULL) return;
  free(r->msg);
  r->msg = NULL;
  free(r);
}

int reply_send(Client *client, ServerReply *r) {
  if (r == NULL || r->msg == NULL) {
    log_error("reply_send: reply NULL per %s:%d", client->ip, client->port);
    return -1;
  }
  ssize_t sent = send(client->sock, r->msg, strlen(r->msg), 0);
  if (sent == -1) {
    log_error("Invio fallito verso %s:%d: %s", client->ip, client->port,
              strerror(errno));
    return -1;
  }
  log_debug("%zd byte -> %s:%d", sent, client->ip, client->port);
  return 0;
}

/*
 * "$6\r\ndavide\r\n"
 * */
int reply_bulk_str(ServerReply *r, Cmd *cmd, Client *client, char *data) {
  if (r == NULL) {
    log_error("reply_bulk_str: reply NULL");
    return -1;
  }

  if (cmd == NULL) {
    log_error("reply_bulk_str: cmd NULL");
    return -1;
  }

  if (data == NULL) {
    reply_nil(r, cmd, client);
    return 0;
  }

  size_t data_len = strlen(data);
  size_t buf_size = snprintf(NULL, 0, "$%zu\r\n%s\r\n", data_len, data) + 1;

  r->msg = malloc(buf_size);
  if (r->msg == NULL) {
    log_error("Impossibile allocare messaggio reply: %s", strerror(errno));
    return -1;
  }

  snprintf(r->msg, buf_size, "$%zu\r\n%s\r\n", strlen(data), data);
  r->error = 0;
  reply_send(client, r);

  return 0;
}

int reply_nil(ServerReply *r, Cmd *cmd, Client *client) {
  if (r == NULL) {
    log_error("reply_nil: reply NULL");
    return -1;
  }

  if (cmd == NULL) {
    log_error("reply_nil: cmd NULL");
    return -1;
  }

  size_t buf_size = snprintf(NULL, 0, "$-1\r\n") + 1;
  r->msg = malloc(buf_size);
  if (r->msg == NULL) {
    log_error("Impossibile allocare messaggio reply: %s", strerror(errno));
    return -1;
  }
  snprintf(r->msg, buf_size, "$-1\r\n");
  reply_send(client, r);
  return 0;
}

/*
 * "+OK\r\n"
 * */
int reply_simple_str(ServerReply *r, Cmd *cmd, Client *client, char *msg) {
  (void)cmd;
  if (r == NULL) {
    log_error("reply_err: reply NULL");
    return -1;
  }

  size_t buf_size = snprintf(NULL, 0, "+%s\r\n", msg) + 1;

  r->msg = malloc(buf_size);
  if (r->msg == NULL) {
    log_error("Impossibile allocare messaggio reply: %s", strerror(errno));
    return -1;
  }

  snprintf(r->msg, buf_size, "+%s\r\n", msg);
  r->error = 0;
  reply_send(client, r);

  return 0;
}

int reply_integer(ServerReply *r, Cmd *cmd, Client *client, int n) {
  (void)cmd;
  if (r == NULL) {
    log_error("reply_err: reply NULL");
    return -1;
  }

  size_t buf_size = snprintf(NULL, 0, ":%d\r\n", n) + 1;

  r->msg = malloc(buf_size);
  if (r->msg == NULL) {
    log_error("Impossibile allocare messaggio reply: %s", strerror(errno));
    return -1;
  }

  snprintf(r->msg, buf_size, ":%d\r\n", n);
  r->error = 0;
  reply_send(client, r);
  return 0;
}

int reply_ok(ServerReply *r, Cmd *cmd, Client *client) {
  reply_simple_str(r, cmd, client, "OK");
  return 0;
}

int reply_err(ServerReply *r, Cmd *cmd, Client *client, char *msg) {
  (void)cmd;
  if (r == NULL) {
    log_error("reply_err: reply NULL");
    return -1;
  }

  size_t buf_size = snprintf(NULL, 0, "-ERR %s\r\n", msg) + 1;

  r->msg = malloc(buf_size);
  if (r->msg == NULL) {
    log_error("Impossibile allocare messaggio reply: %s", strerror(errno));
    return -1;
  }

  snprintf(r->msg, buf_size, "-ERR %s\r\n", msg);
  r->error = 0;
  reply_send(client, r);
  return 0;
}
