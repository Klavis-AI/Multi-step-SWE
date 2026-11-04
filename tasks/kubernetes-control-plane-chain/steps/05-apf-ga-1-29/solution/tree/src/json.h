/* Minimal JSON DOM used by every component of the control plane. */
#ifndef KCP_JSON_H
#define KCP_JSON_H
#include <stddef.h>
#include <stdint.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct json json;
typedef struct { char *key; json *val; } jmember;

struct json {
    jtype t;
    int b;                  /* J_BOOL */
    double n;               /* J_NUM */
    int is_int;             /* J_NUM printed without a fraction */
    char *s;                /* J_STR */
    json **items; size_t nitems, citems;          /* J_ARR */
    jmember *mem; size_t nmem, cmem;              /* J_OBJ */
};

json *j_null(void);
json *j_bool(int b);
json *j_int(long long v);
json *j_num(double v);
json *j_str(const char *s);
json *j_strn(const char *s, size_t n);
json *j_arr(void);
json *j_obj(void);
void  j_free(json *v);
json *j_clone(const json *v);

/* object access */
json *j_get(const json *o, const char *key);
const char *j_gets(const json *o, const char *key);
long long j_geti(const json *o, const char *key, long long dflt);
int   j_getb(const json *o, const char *key, int dflt);
void  j_set(json *o, const char *key, json *v);       /* takes ownership */
void  j_del(json *o, const char *key);
json *j_ensure_obj(json *o, const char *key);

/* array access */
void  j_push(json *a, json *v);                       /* takes ownership */
json *j_at(const json *a, size_t i);
void  j_remove_at(json *a, size_t i);

/* path helpers: dotted path with [n] indices, e.g. "spec.containers[0].image" */
json *j_path(const json *root, const char *path);

int   j_equal(const json *a, const json *b);
char *j_dump(const json *v);                          /* compact, caller frees */
char *j_dump_sorted(const json *v);
json *j_parse(const char *text, size_t len);          /* NULL on error */
json *j_parse0(const char *text);

const char *j_str_of(const json *v);
int   j_is(const json *v, jtype t);
#endif
