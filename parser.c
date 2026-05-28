#include "parser.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_token(Tokens *t, char *ptr);
static size_t tokenize(Cmd *cmd, StringBuffer *raw_cmd);
static void match_command(Cmd *cmd);

/* =========================================================================
 * print_tokens() — stampa i token su stdout (solo per debug).
 * ========================================================================= */
void print_tokens(Tokens *t) {
  printf("Tokens count: %lu\n", t->count);
  for (int i = 0; i < (int)t->count; i++) {
    printf("[%d] %s\n", i, t->buf[i]);
  }
}

/* =========================================================================
 * add_token() — aggiunge un puntatore all'array dei token.
 *
 * Se l'array e' pieno (count >= cap), raddoppia la capacita' con realloc.
 * I token sono validi solo finche' raw_cmd esiste.
 * ========================================================================= */
static void add_token(Tokens *t, char *ptr) {
  if (t->count >= t->cap) {
    size_t new_cap = t->cap * 2;
    char **tmp = realloc(t->buf, new_cap * sizeof(char *));
    if (tmp == NULL) return;
    t->buf = tmp;
    t->cap = new_cap;
  }
  t->buf[t->count++] = ptr;
}

/* =========================================================================
 * tokenize() — estrae i token dal buffer sostituendo i delimitatori.
 *
 * Scansiona raw_cmd->buf carattere per carattere. Spazio, CR e LF
 * diventano \0 (terminatore di stringa) e il testo precedente viene
 * salvato come token via add_token(). Se trova CR+LF imposta
 * cmd->ready = 1 (comando completo, protocollo telnet).
 *
 * Lavora su copie locali di buf e len: raw_cmd->len originale non
 * viene modificato. Restituisce i byte consumati.
 * ========================================================================= */
static size_t tokenize(Cmd *cmd, StringBuffer *raw_cmd) {
  char *buf = raw_cmd->buf;
  size_t len = raw_cmd->len;
  Tokens *tokens = cmd->tokens;

  int i = 0;
  while (i < (int)len) {
    if (buf[i] == SPACE || buf[i] == CR || buf[i] == LF) {

      /* CR+LF consecutivi -> fine comando */
      if (i + 1 < (int)len && buf[i] == CR && buf[i + 1] == LF)
        cmd->ready = 1;

      buf[i] = NUL;

      if (i > 0)
        add_token(tokens, buf);

      buf += i + 1;
      len -= i + 1;
      i = 0;
    } else {
      i++;
    }
  }

  /* Ultimo token senza delimitatore finale (es. "QUIT" senza \r\n) */
  if (i > 0)
    add_token(tokens, buf);

  return raw_cmd->len - len;
}

/* =========================================================================
 * match_command() — identifica il comando dal primo token.
 *
 * Confronta il primo token (tokens->buf[0]) con i comandi noti e
 * imposta cmd->entry. Se non corrisponde a nessun comando, entry
 * resta CMD_UNKNOWN (valore di default).
 *
 * Il match e' per lunghezza + caratteri: evita strcmp() e il costo
 * di scansione della tabella per ogni lookup.
 * ========================================================================= */
static void match_command(Cmd *cmd) {
  if (cmd->tokens->count == 0)
    return;

  char *c = cmd->tokens->buf[0];
  size_t len = strlen(c);
  log_debug("tokenize: len=%zu", len);

  if (len == 3) {
    if      (c[0] == 'G' && c[1] == 'E' && c[2] == 'T')  cmd->entry = CMD_GET;
    else if (c[0] == 'S' && c[1] == 'E' && c[2] == 'T')  cmd->entry = CMD_SET;
    else if (c[0] == 'D' && c[1] == 'E' && c[2] == 'L')  cmd->entry = CMD_DEL;
  } else if (len == 4) {
    if      (c[0] == 'Q' && c[1] == 'U' && c[2] == 'I' && c[3] == 'T') cmd->entry = CMD_QUIT;
    else if (c[0] == 'I' && c[1] == 'N' && c[2] == 'F' && c[3] == 'O') cmd->entry = CMD_INFO;
    else if (c[0] == 'S' && c[1] == 'A' && c[2] == 'V' && c[3] == 'E') cmd->entry = CMD_SAVE;
    else if (c[0] == 'L' && c[1] == 'O' && c[2] == 'A' && c[3] == 'D') cmd->entry = CMD_LOAD;
    else if (c[0] == 'I' && c[1] == 'N' && c[2] == 'C' && c[3] == 'R') cmd->entry = CMD_INCR;
  } else if (len == 6) {
    if (c[0] == 'S' && c[1] == 'E' && c[2] == 'L' &&
        c[3] == 'E' && c[4] == 'C' && c[5] == 'T')                   cmd->entry = CMD_SELECT;
  }
}

/* =========================================================================
 * parse_tokens() — parser completo: tokenize + riconoscimento comando.
 *
 * 1. tokenize() estrae i token dal buffer raw e conta i byte consumati
 * 2. match_command() identifica il comando dal primo token
 *
 * Ritorna il numero di byte consumati.
 * ========================================================================= */
size_t parse_tokens(Cmd *cmd, StringBuffer *raw_cmd) {
  size_t consumed = tokenize(cmd, raw_cmd);
  match_command(cmd);
  return consumed;
}
