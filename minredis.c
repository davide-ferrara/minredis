#include "server.h"
#include "log.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
  fprintf(stderr,
          "Uso: %s [--port <n>] [--addr <ip>] [--restore] [--debug]\n"
          "  --port    porta (default %d)\n"
          "  --addr    indirizzo bind (default 0.0.0.0)\n"
          "  --restore carica il dump all'avvio\n"
          "  --debug   abilita i log di debug\n",
          prog, PORT_DEFAULT);
  exit(1);
}

int main(int argc, char **argv) {
  uint16_t port = PORT_DEFAULT;
  uint32_t addr = INADDR_ANY;
  int restore = 0, quiet = 0, debug = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
      port = (uint16_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc)
      addr = inet_addr(argv[++i]);
    else if (strcmp(argv[i], "--restore") == 0)
      restore = 1;
    else if (strcmp(argv[i], "--quiet") == 0)
      quiet = 1;
    else if (strcmp(argv[i], "--debug") == 0)
      debug = 1;
    else
      usage(argv[0]);
  }

  if (addr == (uint32_t)-1) {
    fprintf(stderr, "Indirizzo non valido\n");
    exit(1);
  }

  log_set_level(debug ? LOG_DEBUG : LOG_INFO);
  log_set_quiet(quiet ? 1 : 0);

  Server *server = server_create(port, addr, restore);
  log_info("Server avviato, porta %d", port);
  server_run(server);
  return 0;
}
