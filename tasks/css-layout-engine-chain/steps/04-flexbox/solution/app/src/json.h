/* Minimal JSON reader and writer: enough of RFC 8259 for WebDriver payloads. */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

/* ---------------------------------------------------------------- parsing */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type;

typedef struct json_value json_value;

/* Parses exactly `len` bytes; trailing content other than whitespace is an
   error. Returns NULL on any syntax error. Caller owns the result. */
json_value *json_parse(const char *text, size_t len);
void json_free(json_value *v);

json_type   json_typeof(const json_value *v);   /* JSON_NULL when v is NULL */
int         json_is(const json_value *v, json_type t);

/* Accessors; each yields NULL/0 when `v` has the wrong type. */
const char *json_str(const json_value *v);      /* NUL-terminated */
int         json_num(const json_value *v, double *out);
int         json_bool(const json_value *v, int *out);

size_t             json_arr_len(const json_value *v);
const json_value  *json_arr_get(const json_value *v, size_t i);

size_t             json_obj_len(const json_value *v);
const char        *json_obj_key(const json_value *v, size_t i);
const json_value  *json_obj_val(const json_value *v, size_t i);
const json_value  *json_obj_get(const json_value *v, const char *key);

/* ---------------------------------------------------------------- writing */

#define JW_MAX_DEPTH 32

typedef struct {
    char   *data;
    size_t  len, cap;
    int     err;                        /* sticky allocation/overflow flag */
    int     depth;
    int     pending;                    /* a key was emitted, value follows */
    unsigned char comma[JW_MAX_DEPTH];  /* per level: has a member been emitted */
} json_writer;

void        jw_init(json_writer *w);
void        jw_free(json_writer *w);
const char *jw_text(const json_writer *w);   /* NUL-terminated; "" on error */
size_t      jw_len(const json_writer *w);

void jw_obj_open(json_writer *w);
void jw_obj_close(json_writer *w);
void jw_arr_open(json_writer *w);
void jw_arr_close(json_writer *w);
void jw_key(json_writer *w, const char *key);
void jw_str(json_writer *w, const char *s);
void jw_num(json_writer *w, double d);
void jw_bool(json_writer *w, int b);
void jw_null(json_writer *w);
void jw_val(json_writer *w, const json_value *v);   /* re-emit a parsed value */

#endif
