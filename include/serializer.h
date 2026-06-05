#ifndef SERIALIZER_H
#define SERIALIZER_H

#include "hash.h"
#include "server.h"
#include <stdint.h>
#include <stdio.h>

#define RDB_MAGIC "MINREDIS"
#define RDB_VERSION 0x01
#define RDB_PATH "dump.minredis"

/* Opcode binari */
#define OP_SELECTDB 0xFE
#define OP_EOF 0xFF

/* Tipo del valore */
#define TYPE_STRING 0

typedef struct {
  char magic[8];
  uint8_t version;
} RdbHeader;

int rdb_save(Server *server, const char *path);
int rdb_load(Server *server, const char *path);

#endif
