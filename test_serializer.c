#include "hash.h"
#include "serializer.h"
#include "server.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  /* Creo un server finto con dati di test */
  Server *server = malloc(sizeof(Server));
  assert(server != NULL);
  memset(server, 0, sizeof(Server));

  /* Riempio db 0 */
  hash_set(&server->database.tables[0], "nome", "davide");
  hash_set(&server->database.tables[0], "eta", "22");
  hash_set(&server->database.tables[0], "msg", "ciao come stai");

  /* Riempio db 1 */
  hash_set(&server->database.tables[1], "colore", "rosso");
  hash_set(&server->database.tables[1], "animale", "gatto");

  /* Riempio db 5 */
  hash_set(&server->database.tables[5], "linguaggio", "C");
  hash_set(&server->database.tables[5], "os", "linux");

  size_t count_db0 = hash_count(&server->database.tables[0]);
  size_t count_db1 = hash_count(&server->database.tables[1]);
  size_t count_db5 = hash_count(&server->database.tables[5]);

  printf("db0 keys: %zu (expected 3)\n", count_db0);
  printf("db1 keys: %zu (expected 2)\n", count_db1);
  printf("db5 keys: %zu (expected 2)\n", count_db5);

  assert(count_db0 == 3);
  assert(count_db1 == 2);
  assert(count_db5 == 2);

  /* Salva su file */
  printf("Chiamo rdb_save...\n");
  int rc = rdb_save(server, RDB_PATH);
  printf("rdb_save ha ritornato %d\n", rc);

  /* Ricarica dal file */
  Server *server2 = malloc(sizeof(Server));
  assert(server2 != NULL);
  memset(server2, 0, sizeof(Server));

  printf("Chiamo rdb_load...\n");
  int rc2 = rdb_load(server2, RDB_PATH);
  printf("rdb_load ha ritornato %d\n", rc2);
  assert(rc2 == 0);

  /* Verifica che i dati siano gli stessi */
  char *val = hash_get(&server2->database.tables[0], "nome");
  printf("GET nome su server2: %s (expected davide)\n", val ? val : "(null)");
  assert(val != NULL && strcmp(val, "davide") == 0);

  val = hash_get(&server2->database.tables[0], "msg");
  printf("GET msg su server2: %s (expected 'ciao come stai')\n",
         val ? val : "(null)");
  assert(val != NULL && strcmp(val, "ciao come stai") == 0);

  val = hash_get(&server2->database.tables[0], "eta");
  printf("GET eta su server2: %s (expected 22)\n", val ? val : "(null)");
  assert(val != NULL && strcmp(val, "22") == 0);

  val = hash_get(&server2->database.tables[1], "colore");
  printf("GET colore su server2: %s (expected rosso)\n", val ? val : "(null)");
  assert(val != NULL && strcmp(val, "rosso") == 0);

  val = hash_get(&server2->database.tables[1], "animale");
  printf("GET animale su server2: %s (expected gatto)\n", val ? val : "(null)");
  assert(val != NULL && strcmp(val, "gatto") == 0);

  val = hash_get(&server2->database.tables[5], "linguaggio");
  printf("GET linguaggio su server2: %s (expected C)\n", val ? val : "(null)");
  assert(val != NULL && strcmp(val, "C") == 0);

  val = hash_get(&server2->database.tables[5], "os");
  printf("GET os su server2: %s (expected linux)\n", val ? val : "(null)");
  assert(val != NULL && strcmp(val, "linux") == 0);

  free(server);
  free(server2);

  printf("test_serializer: tutto ok\n");
  return 0;
}
