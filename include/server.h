#ifndef SERVER_H
#define SERVER_H

#include "hash.h"
#include "reply.h"

#include "log.h"
#include <netinet/in.h>
#include <pthread.h>

#define PORT_DEFAULT 5050
#define BACKLOG 5
#define BUF_LEN 256
#define MAX_CONN 512
#define NUM_DBS 16
#define MAX_DB (NUM_DBS - 1)

typedef struct Client {
  int sock;
  char ip[INET6_ADDRSTRLEN];
  uint16_t port;
  size_t db;
} Client;

typedef struct {
  int sock;
  in_addr_t addr;
  uint16_t port;

  struct {
    Client array[MAX_CONN];
    pthread_t threads[MAX_CONN];
    int count;
  } clients;

  struct {
    HashTable tables[NUM_DBS];
  } database;

  int restore;
  pthread_rwlock_t rwlock;
} Server;

Server *server_create(uint16_t port, uint32_t addr, int restore);
int server_run(Server *srv);

#endif
