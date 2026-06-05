#ifndef PARSER_H
#define PARSER_H

#include "strbuf.h"

#include <stddef.h>

#define NUL '\0'
#define LF '\n'
#define CR '\r'
#define SPACE ' '
#define TOKENS_INIT_CAP 64

enum CmdCode {
  CMD_GET,
  CMD_SET,
  CMD_DEL,
  CMD_QUIT,
  CMD_SELECT,
  CMD_INFO,
  CMD_SAVE,
  CMD_LOAD,
  CMD_INCR,
  CMD_UNKNOWN
};

typedef struct {
  char **buf;
  size_t count;
  size_t cap;
} Tokens;

typedef struct Cmd {
  Tokens *tokens;
  enum CmdCode entry;
  int ready;
} Cmd;

/* Parser: spezza un buffer in token, identifica il comando. */
size_t      parse_tokens(Cmd *cmd, StringBuffer *raw_cmd);
void        print_tokens(Tokens *t);
char       *cmd_type_to_str(Cmd *cmd);

#endif
