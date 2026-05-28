#ifndef REPLY_H
#define REPLY_H

#include <sys/socket.h>

typedef struct {
  char *msg;
  int error;
} ServerReply;

typedef struct Client Client;
typedef struct Cmd Cmd;

ServerReply *reply_new(void);
void        reply_free(ServerReply *r);
int reply_send(Client *client, ServerReply *r);
int reply_simple_str(ServerReply *r, Cmd *cmd, Client *client, char *msg);
int reply_bulk_str(ServerReply *r, Cmd *cmd, Client *client, char *data);
int reply_integer(ServerReply *r, Cmd *cmd, Client *client, int n);
int reply_err(ServerReply *r, Cmd *cmd, Client *client, char *msg);
int reply_ok(ServerReply *r, Cmd *cmd, Client *client);
int reply_nil(ServerReply *r, Cmd *cmd, Client *client);

#endif
