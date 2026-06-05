#ifndef CMD_H
#define CMD_H

#include "parser.h"
#include "server.h"

/* Cmd lifecycle */
Cmd *cmd_init(void);
void cmd_free(Cmd *cmd);
void cmd_reset(Cmd *cmd);
int  cmd_parse(Cmd *cmd, StringBuffer *raw_cmd);

/* Command handlers */
int cmd_quit(Server *server, Cmd *cmd, Client *client, ServerReply *r);
int cmd_incr(Server *server, Cmd *cmd, Client *client, ServerReply *r);
int cmd_set(Server *server, Cmd *cmd, Client *client, ServerReply *r);
int cmd_get(Server *server, Cmd *cmd, Client *client, ServerReply *r);
int cmd_del(Server *server, Cmd *cmd, Client *client, ServerReply *r);
int cmd_select(Server *server, Cmd *cmd, Client *client, ServerReply *r);
int cmd_info(Server *server, Cmd *cmd, Client *client, ServerReply *r);
int cmd_save(Cmd *cmd, Server *server, Client *client, ServerReply *r);
int cmd_load(Cmd *cmd, Server *server, Client *client, ServerReply *r);

/* Dispatcher */
int cmd_dispatch(Cmd *cmd, Server *server, Client *client);

#endif
