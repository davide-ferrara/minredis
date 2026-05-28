#include "server.h"
#include "cmd.h"
#include "log.h"
#include "parser.h"
#include "serializer.h"
#include "strbuf.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

volatile int running = 1;

/* =========================================================================
 * handle_sigint() — gestore del segnale SIGINT (Ctrl+C).
 * ========================================================================= */
static void handle_sigint(int sig) {
  (void)sig;
  running = 0;
}

/* =========================================================================
 * worker_save() — thread di background per SAVE periodico.
 *
 * Esegue sleep(60) e poi chiama rdb_save() che acquisisce tutti i
 * rdlock dei bucket per un snapshot consistente.
 *
 * Il thread è detached (pthread_detach in server_run): le sue risorse
 * vengono liberate automaticamente dal kernel alla terminazione.
 * Non c'è modo di fermarlo esplicitamente: termina quando il processo
 * principale esce (SIGINT → graceful_exit → exit).
 *
 * rdb_save() usa il path di default (NULL → "dump.minredis").
 * ========================================================================= */
static void *worker_save(void *arg) {
  Server *server = (Server *)arg;

  while (1) {
    sleep(60);
    rdb_save(server, NULL);
  }
  return NULL;
}

/* =========================================================================
 * server_create() — alloca e inizializza il server.
 *
 * Crea il socket TCP con setsockopt(SO_REUSEADDR) per riusare la porta
 * subito dopo un riavvio.
 *
 * Il server non è ancora in ascolto: bisogna chiamare server_run().
 *
 * Parametri:
 *   port    — porta TCP (es. 5050), già in host byte order
 *   addr    — indirizzo IPv4 in network byte order (INADDR_ANY = 0.0.0.0)
 *   restore — se 1, carica dump.minredis all'avvio
 *
 * In caso di errore fatale chiama exit(-1).
 * ========================================================================= */
Server *server_create(uint16_t port, uint32_t addr, int restore) {
  Server *server = malloc(sizeof(Server));
  if (server == NULL) {
    log_fatal("Impossibile allocare il server: %s", strerror(errno));
    exit(-1);
  }
  memset(server, 0, sizeof(Server));
  server->port = port;
  server->addr = addr;
  server->restore = restore;

  struct sockaddr_in saddr = {0};
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(server->port);
  saddr.sin_addr.s_addr = server->addr;

  server->sock = socket(AF_INET, SOCK_STREAM, 0);
  if (server->sock == -1) {
    log_fatal("Socket fallita: %s", strerror(errno));
    exit(-1);
  }

  int opt = 1;
  setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (bind(server->sock, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
    log_fatal("Bind fallito: %s", strerror(errno));
    exit(-1);
  }

  return server;
}

static void *worker_handle_client(void *arg);
static int gracefully_exit(Server *server);

/* =========================================================================
 * server_setup() — inizializzazione pre-accept.
 *
 * listen(), restore opzionale, spawn del thread SAVE periodico,
 * installazione gestore SIGINT.
 * ========================================================================= */
static void server_setup(Server *server) {
  if (listen(server->sock, MAX_CONN) == -1) {
    log_fatal("Listen fallita: %s", strerror(errno));
    exit(-1);
  }

  log_info("Pronto per connessioni sulla porta %d", server->port);

  if (server->restore) {
    log_info("Caricamento dump all'avvio...");
    int rc = rdb_load(server, NULL);
    if (rc == 0)
      log_info("Dump caricato con successo");
  }

  pthread_t bg_tid;
  pthread_create(&bg_tid, NULL, worker_save, (void *)server);
  pthread_detach(bg_tid);

  struct sigaction sa = {0};
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
}

/* =========================================================================
 * server_accept_client() — accetta e registra una connessione.
 *
 * accept() + popolamento Client + spawn worker_handle_client.
 * Restituisce 0 in caso di successo, -1 se accept fallisce (EINTR = SIGINT).
 * ========================================================================= */
static int server_accept_client(Server *server) {
  struct sockaddr_in client_addr = {0};
  socklen_t client_len = sizeof(client_addr);

  int fd = accept(server->sock, (struct sockaddr *)&client_addr, &client_len);
  if (fd == -1) {
    if (errno == EINTR) return -1;
    log_error("Accept fallita: %s", strerror(errno));
    return 0;
  }

  if (server->clients.count >= MAX_CONN) {
    log_info("Connessione rifiutata, limite massimo (%d)", MAX_CONN);
    close(fd);
    return 0;
  }

  Client *client = &server->clients.array[server->clients.count];
  client->sock = fd;

  inet_ntop(AF_INET, &client_addr.sin_addr, client->ip, sizeof(client->ip));
  client->port = ntohs(client_addr.sin_port);
  log_info("Connesso %s:%d", client->ip, client->port);

  struct {
    Server *server;
    Client *client;
  } *arg = malloc(sizeof(*arg));
  arg->server = server;
  arg->client = client;

  pthread_create(&server->clients.threads[server->clients.count], NULL,
                 worker_handle_client, (void *)arg);

  pthread_rwlock_wrlock(&server->rwlock);
  server->clients.count++;
  pthread_rwlock_unlock(&server->rwlock);

  return 0;
}

/* =========================================================================
 * server_run() — loop principale del server.
 *
 * 1. server_setup()   → listen, restore, SAVE bg, SIGINT handler
 * 2. accept loop      → server_accept_client() per ogni connessione
 * 3. gracefully_exit()→ shutdown pulito dopo SIGINT
 * ========================================================================= */
int server_run(Server *server) {
  server_setup(server);

  while (running) {
    if (server_accept_client(server) == -1)
      break; /* SIGINT */
  }

  log_info("Ricevuto SIGINT, arresto...");
  gracefully_exit(server);
  return 0;
}

/* =========================================================================
 * find_crlf() — cerca la prima occorrenza di \r\n nel buffer.
 *
 * Restituisce la posizione del carattere successivo a \r\n (cioe'
 * l'indice del primo byte DOPO la coppia CRLF), oppure 0 se non trovata.
 * ========================================================================= */
static size_t find_crlf(const char *buf, size_t len) {
  for (size_t i = 0; i + 1 < len; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n')
      return i + 2; /* posizione subito dopo \r\n */
  }
  return 0;
}

/* =========================================================================
 * worker_handle_client() — thread che gestisce una connessione client.
 *
 * Ogni client viene servito da un thread dedicato (thread-per-client).
 * Il loop e' diviso in tre fasi:
 *
 *   1. recv  — accumula dati dal socket in querybuf
 *   2. delim — cerca \r\n nel buffer (fine comando telnet)
 *   3. exec  — quando trova un comando completo, lo copia, lo parsa e lo esegue
 *
 * Il querybuf viene compattato con memmove dopo ogni comando eseguito.
 * Se \r\n non e' ancora arrivato, i dati restano nel buffer e il loop
 * torna in recv() per accumulare altri byte.
 *
 * Esempio con \r\n spezzato su due recv:
 *
 *   recv 1: "SET na\r"        → querybuf = "SET na\r" (7 byte)
 *   delim:  nessun \r\n       → resta in attesa
 *
 *   recv 2: "me davide\r\n"   → querybuf = "SET na\rme davide\r\n" (19)
 *   delim:  \r\n a pos 18     → copia "SET name davide\r\n", parse, esegui
 *   memmove: rimuove i 19 byte consumati, querybuf resta vuoto
 *
 * I token del parser puntano dentro la copia temporanea, non direttamente
 * nel querybuf, quindi il memmove non li invalida.
 * ========================================================================= */
static void *worker_handle_client(void *arg) {
  struct {
    Server *server;
    Client *client;
  } *wrap = arg;
  Server *server = wrap->server;
  Client *client = wrap->client;
  free(wrap);

  /* Buffer di accumulo (querybuf di Redis) */
  StringBuffer querybuf = {.buf = malloc(BUFSIZ), .len = 0, .cap = BUFSIZ};
  if (querybuf.buf == NULL) {
    log_error("Impossibile allocare querybuf: %s", strerror(errno));
    return NULL;
  }

  Cmd *cmd = cmd_init();
  if (cmd == NULL) {
    log_error("Impossibile allocare Cmd: %s", strerror(errno));
    free(querybuf.buf);
    return NULL;
  }

  int running = 1;

  while (running) {
    /* === FASE 1: leggi dal socket e accumula === */
    char iobuf[BUFSIZ];
    ssize_t n = recv(client->sock, iobuf, sizeof(iobuf), 0);
    if (n == -1) {
      log_error("Recv fallita da %s:%d: %s", client->ip, client->port,
                strerror(errno));
      break;
    }
    if (n == 0) {
      log_info("Disconnesso %s:%d", client->ip, client->port);
      break;
    }

    log_debug("%zd byte <- %s:%d", n, client->ip, client->port);
    strbuf_append_noterm(&querybuf, iobuf, (size_t)n);

    /* === FASE 2+3: estrai ed esegui tutti i comandi completi === */
    while (running) {
      size_t end = find_crlf(querybuf.buf, querybuf.len);
      if (end == 0)
        break; /* nessun \r\n completo: aspetta altri dati */

      /* Copia il comando completo in un buffer temporaneo per il parser.
       * Il parser modifica il buffer in-place (sostituisce i delimitatori
       * con \0): non vogliamo che modifichi il querybuf originale. */
      size_t cmd_len = end;
      char *cmd_buf = malloc(cmd_len + 1);
      if (cmd_buf == NULL) {
        log_error("Impossibile allocare cmd_buf: %s", strerror(errno));
        running = 0;
        break;
      }
      memcpy(cmd_buf, querybuf.buf, cmd_len);
      cmd_buf[cmd_len] = '\0';

      StringBuffer tmp = {.buf = cmd_buf, .len = cmd_len, .cap = cmd_len + 1};
      cmd_parse(cmd, &tmp);
      cmd_dispatch(cmd, server, client);

      free(cmd_buf);

      /* Rimuovi il comando consumato dal querybuf */
      querybuf.len -= cmd_len;
      if (querybuf.len > 0)
        memmove(querybuf.buf, querybuf.buf + cmd_len, querybuf.len);

      if (client->sock == -1) {
        running = 0;
        break;
      }
    }
  }

  cmd_free(cmd);
  free(querybuf.buf);
  return NULL;
}

/* =========================================================================
 * gracefully_exit() — arresto pulito del server.
 *
 * Chiamata dopo SIGINT. La procedura:
 *   1. Legge clients.count sotto rwlock (ultimo utilizzo)
 *   2. shutdown(SHUT_RDWR) su tutti i client: sblocca i recv() pendenti
 *      nei thread worker, che usciranno dal loop
 *   3. pthread_join() su tutti i thread: aspetta che finiscano
 *   4. close() su tutti i socket client
 *   5. close() sul socket di ascolto del server
 * ========================================================================= */
static int gracefully_exit(Server *server) {
  pthread_rwlock_rdlock(&(server->rwlock));
  int n = server->clients.count;
  pthread_rwlock_unlock(&(server->rwlock));

  log_info("Arresto in corso... (client connessi: %d)", n);

  /* Sblocchiamo tutti i recv() bloccanti */
  for (int i = 0; i < n; i++)
    shutdown(server->clients.array[i].sock, SHUT_RDWR);

  /* Aspettiamo che tutti i thread worker terminino */
  for (int i = 0; i < n; i++)
    pthread_join(server->clients.threads[i], NULL);

  /* Chiudiamo i socket (ormai i thread sono fermi) */
  for (int i = 0; i < n; i++) {
    close(server->clients.array[i].sock);
    server->clients.array[i].sock = -1;
  }

  close(server->sock);

  log_info("Server arrestato");
  return 0;
}
