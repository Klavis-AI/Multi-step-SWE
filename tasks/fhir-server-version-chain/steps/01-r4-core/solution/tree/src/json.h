/* json.h -- order-preserving JSON DOM with exact decimal lexemes (FHIR needs both). */
#ifndef FHIR_JSON_H
#define FHIR_JSON_H

#include "util.h"

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jkind;

typedef struct jval jval;
typedef struct jmem jmem;

struct jmem {
    const char *key;   /* arena-owned */
    jval       *val;
    jmem       *next;
};

struct jval {
    jkind kind;
    union {
        int         b;
        const char *num;    /* verbatim lexeme, so 1.20 stays 1.20 */
        const char *str;
        struct { jval **items; size_t n, cap; } arr;
        struct { jmem *first, *last; size_t n; } obj;
    } u;
};

/* Construction (all values arena-allocated). */
jval *j_null(arena *a);
jval *j_bool(arena *a, int b);
jval *j_num(arena *a, const char *lexeme);
jval *j_int(arena *a, long long v);
jval *j_str(arena *a, const char *s);
jval *j_arr(arena *a);
jval *j_obj(arena *a);
void  j_push(arena *a, jval *arr, jval *v);
void  j_set(arena *a, jval *obj, const char *key, jval *v);   /* replaces, keeps position */
void  j_del(jval *obj, const char *key);

/* Access. */
jval       *j_get(const jval *obj, const char *key);
const char *j_gets(const jval *obj, const char *key);   /* string value or NULL */
const char *j_num_of(const jval *v);
int         j_len(const jval *v);
jval       *j_at(const jval *arr, int i);
int         j_is(const jval *v, jkind k);
const char *j_str_of(const jval *v);

/* Parse / serialise.  On failure returns NULL and fills err (static-ish buffer). */
jval *j_parse(arena *a, const char *text, size_t len, char *err, size_t errlen);
void  j_write(buf *b, const jval *v, int pretty);
jval *j_clone(arena *a, const jval *v);
int   j_equal(const jval *a, const jval *b);

/* Iteration helper. */
#define J_FOREACH(m, obj) for (jmem *m = ((obj) && (obj)->kind == J_OBJ) ? (obj)->u.obj.first : NULL; m; m = m->next)

#endif
