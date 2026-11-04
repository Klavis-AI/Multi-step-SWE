/* util.h -- arena allocator, growable buffers, string map.
 *
 * Part of the CSS layout engine front end.  C11, libc only.
 *
 * Ownership model: every string or object handed back by this layer lives in
 * the arena it was allocated from and stays valid until arena_free().  There
 * are no individual free() calls anywhere in the front end.
 */
#ifndef FRONTEND_UTIL_H
#define FRONTEND_UTIL_H

#include <stdarg.h>
#include <stddef.h>

#if defined(__GNUC__)
#  define FE_PRINTF(fmtarg, firstvararg) \
      __attribute__((format(printf, fmtarg, firstvararg)))
#else
#  define FE_PRINTF(fmtarg, firstvararg)
#endif

/* ------------------------------------------------------------------ arena */

typedef struct arena arena;

arena *arena_new(void);
void  *arena_alloc(arena *a, size_t n);        /* zeroed, 16-byte aligned */
char  *arena_strdup(arena *a, const char *s);
char  *arena_strndup(arena *a, const char *s, size_t n);
char  *arena_printf(arena *a, const char *fmt, ...) FE_PRINTF(2, 3);
void   arena_free(arena *a);                   /* frees everything at once */

/* ----------------------------------------------------------------- strbuf */

typedef struct { char *p; size_t len, cap; arena *a; } strbuf;

void sb_init(strbuf *s, arena *a);
void sb_putc(strbuf *s, char c);
void sb_puts(strbuf *s, const char *t);
void sb_putn(strbuf *s, const char *t, size_t n);
void sb_printf(strbuf *s, const char *fmt, ...) FE_PRINTF(2, 3);
char *sb_done(strbuf *s);                      /* NUL-terminated, arena-owned */

/* -------------------------------------------------------------------- vec */

typedef struct { void **v; size_t len, cap; arena *a; } vec;

void vec_init(vec *x, arena *a);
void vec_push(vec *x, void *p);

/* ----------------------------------------------------------------- strmap */

typedef struct strmap strmap;                  /* char* -> void*, case-sensitive */

strmap *strmap_new(arena *a);
void  strmap_put(strmap *m, const char *k, void *v);
void *strmap_get(strmap *m, const char *k);    /* NULL if absent */

#endif /* FRONTEND_UTIL_H */
