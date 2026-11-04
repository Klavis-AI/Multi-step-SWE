/* JSON values, RFC 8259 parsing, and Matrix canonical JSON serialisation. */
#ifndef MXJSON_H
#define MXJSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    J_NULL = 0, J_BOOL, J_INT, J_DOUBLE, J_STRING, J_ARRAY, J_OBJECT
} jtype;

typedef struct json json;

struct json {
    jtype type;
    /* scalars */
    bool b;
    int64_t i;
    double d;
    char *s;              /* J_STRING: NUL-terminated, UTF-8 */
    size_t slen;
    /* containers */
    json **items;         /* J_ARRAY, J_OBJECT values */
    char **keys;          /* J_OBJECT keys, parallel to items */
    size_t n, cap;
};

/* construction */
json *j_null(void);
json *j_bool(bool v);
json *j_int(int64_t v);
json *j_double(double v);
json *j_str(const char *s);
json *j_strn(const char *s, size_t n);
json *j_arr(void);
json *j_obj(void);
void  j_free(json *v);
json *j_clone(const json *v);

/* containers */
void  j_push(json *arr, json *v);                 /* takes ownership of v */
void  j_set(json *obj, const char *key, json *v); /* takes ownership of v */
void  j_del(json *obj, const char *key);
void  j_remove_at(json *arr, size_t i);           /* array element, by index */
json *j_get(const json *obj, const char *key);    /* borrowed, or NULL */
json *j_at(const json *arr, size_t i);            /* borrowed, or NULL */
size_t j_len(const json *v);
const char *j_key(const json *obj, size_t i);

/* typed access with defaults */
const char *j_gets(const json *obj, const char *key, const char *dflt);
int64_t     j_geti(const json *obj, const char *key, int64_t dflt);
bool        j_getb(const json *obj, const char *key, bool dflt);
json       *j_geto(const json *obj, const char *key); /* object or NULL */
json       *j_geta(const json *obj, const char *key); /* array or NULL */
bool        j_is_str(const json *v);

/* parse / serialise */
json *j_parse(const char *text, size_t len, const char **err);
char *j_dump(const json *v, size_t *out_len);       /* compact, malloc'd */
/* Matrix canonical JSON: keys sorted by unicode codepoint, no whitespace,
 * integers only, shortest-form escaping.  Returns NULL when the value cannot
 * be canonicalised (a float, a NaN, or an infinity). */
char *j_canonical(const json *v, size_t *out_len);
bool  j_has_float(const json *v);
/* An integer outside [-(2**53)+1, 2**53-1], which canonical JSON forbids. */
bool  j_has_unsafe_int(const json *v);

/* convenience */
json *j_deep_get(const json *v, const char *path);  /* "a.b.c", borrowed */
#endif
