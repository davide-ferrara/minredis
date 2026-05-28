#include "strbuf.h"

#include <stdlib.h>
#include <string.h>

StringBuffer *strbuf_new(size_t cap, const char *initial) {
  StringBuffer *sb = malloc(sizeof(StringBuffer));
  if (sb == NULL)
    return NULL;

  memset(sb, 0, sizeof(StringBuffer));
  sb->cap = cap;
  sb->buf = malloc(cap * sizeof(char));
  if (sb->buf == NULL) {
    free(sb);
    return NULL;
  }

  if (initial != NULL) {
    size_t n = strlen(initial);
    if (n >= cap)
      n = cap - 1;
    memcpy(sb->buf, initial, n);
    sb->buf[n] = '\0';
    sb->len = n + 1;
  } else {
    sb->buf[0] = '\0';
    sb->len = 1;
  }

  return sb;
}

static int ensure_cap(StringBuffer *sb, size_t need) {
  while (need > sb->cap) {
    size_t ncap = sb->cap * 2;
    if (ncap < need)
      ncap = need;
    char *tmp = realloc(sb->buf, ncap * sizeof(char));
    if (tmp == NULL)
      return -1;
    sb->buf = tmp;
    sb->cap = ncap;
  }
  return 0;
}

/* Appende n byte al buffer senza terminatore \0.
 * Il buffer NON e' una C-string: len conta i byte raw, nessun terminatore. */
int strbuf_append_noterm(StringBuffer *sb, const char *data, size_t n) {
  if (sb == NULL || data == NULL)
    return -1;

  if (ensure_cap(sb, sb->len + n) != 0)
    return -1;

  memcpy(sb->buf + sb->len, data, n);
  sb->len += n;
  return 0;
}

int strbuf_append(StringBuffer *sb, char *str, char *sep) {
  if (sb == NULL || str == NULL)
    return -1;

  size_t add = strlen(str);
  int use_sep = (sep != NULL && strlen(sep) == 1);
  size_t need = sb->len + (use_sep ? 1 : 0) + add;

  if (ensure_cap(sb, need) != 0)
    return -1;

  if (use_sep) {
    sb->buf[sb->len - 1] = sep[0];
    sb->len++;
  }

  memcpy(&(sb->buf[sb->len - 1]), str, add);
  sb->len += add;
  sb->buf[sb->len - 1] = '\0';

  return 0;
}

void strbuf_free(StringBuffer *sb) {
  if (sb == NULL)
    return;
  free(sb->buf);
  free(sb);
}
