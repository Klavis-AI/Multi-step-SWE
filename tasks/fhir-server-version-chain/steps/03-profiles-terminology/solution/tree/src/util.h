/* util.h -- arena allocation, growable buffers, string helpers. */
#ifndef FHIR_UTIL_H
#define FHIR_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

typedef struct arena_block arena_block;
typedef struct {
    arena_block *head;
    size_t       total;
} arena;

void  arena_init(arena *a);
void *arena_alloc(arena *a, size_t n);
void *arena_calloc(arena *a, size_t n);
char *arena_strdup(arena *a, const char *s);
char *arena_strndup(arena *a, const char *s, size_t n);
char *arena_printf(arena *a, const char *fmt, ...);
void  arena_free(arena *a);

/* Growable byte buffer (malloc-backed; caller owns). */
typedef struct {
    char  *p;
    size_t n;
    size_t cap;
} buf;

void  buf_init(buf *b);
void  buf_reserve(buf *b, size_t extra);
void  buf_put(buf *b, const void *data, size_t n);
void  buf_puts(buf *b, const char *s);
void  buf_putc(buf *b, char c);
void  buf_printf(buf *b, const char *fmt, ...);
void  buf_free(buf *b);
char *buf_detach(buf *b);

/* Small helpers used everywhere. */
int   str_eq(const char *a, const char *b);
int   str_ieq(const char *a, const char *b);
int   str_starts(const char *s, const char *pre);
int   str_ends(const char *s, const char *suf);
char *str_dup(const char *s);
char *str_ndup(const char *s, size_t n);
char *str_lower(char *s);           /* in place */
void  str_trim(char *s);            /* in place, ASCII space */
int   hex_val(int c);
void  url_decode(char *s);          /* in place, '+' -> ' ' */
void  url_encode_into(buf *b, const char *s);

/* A pointer vector. */
typedef struct {
    void  **v;
    size_t  n;
    size_t  cap;
} vec;

void  vec_init(vec *x);
void  vec_push(vec *x, void *p);
void  vec_free(vec *x);

/* A simple open-addressing string -> pointer map. */
typedef struct {
    const char **keys;
    void       **vals;
    size_t       cap;
    size_t       n;
} smap;

void  smap_init(smap *m, size_t cap);
void  smap_put(smap *m, const char *k, void *v);   /* key not copied */
void *smap_get(const smap *m, const char *k);
void  smap_free(smap *m);
uint64_t str_hash(const char *s);

#endif
