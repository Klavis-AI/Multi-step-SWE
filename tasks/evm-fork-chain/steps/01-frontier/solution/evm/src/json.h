#ifndef JSON_H
#define JSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "buf.h"

typedef enum { JNULL, JBOOL, JNUM, JSTR, JARR, JOBJ } jtype;

typedef struct jval jval;
struct jval {
    jtype type;
    bool b;
    double num;
    char *str;            /* JSTR: NUL-terminated, unescaped.  JNUM: raw text. */
    jval **items;         /* JARR/JOBJ values */
    char **keys;          /* JOBJ keys */
    size_t n, cap;
};

jval *json_parse(const char *text, size_t len);   /* NULL on error */
void  json_free(jval *v);
jval *json_get(const jval *obj, const char *key); /* NULL if absent */
jval *json_at(const jval *arr, size_t i);
const char *json_str(const jval *v);              /* NULL unless JSTR */

/* --- writing --- */
void json_wr_raw(buf_t *o, const char *s);
void json_wr_str(buf_t *o, const char *s);
void json_wr_key(buf_t *o, const char *k);
#endif
