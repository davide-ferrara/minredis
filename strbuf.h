#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} StringBuffer;

StringBuffer *strbuf_new(size_t cap, const char *initial);
int strbuf_append(StringBuffer *sb, char *str, char *sep);
int strbuf_append_noterm(StringBuffer *sb, const char *data, size_t n);
void strbuf_free(StringBuffer *sb);

#endif
