/* util.c -- arena allocator, growable buffers, string map. */

#include "util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ arena */

#define ARENA_ALIGN        ((size_t)16)
#define ARENA_FIRST_BLOCK  ((size_t)64 * 1024)
#define ARENA_MAX_BLOCK    ((size_t)8 * 1024 * 1024)

typedef struct arena_block {
    struct arena_block *next;
    unsigned char      *data;   /* aligned payload start */
    size_t              cap;
    size_t              used;
} arena_block;

struct arena {
    arena_block *bump;       /* list of bump blocks; head is the live one */
    arena_block *big;        /* list of dedicated oversize blocks */
    size_t       next_size;  /* size of the next bump block */
};

static void util_oom(void)
{
    fputs("frontend: out of memory\n", stderr);
    abort();
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p)
        util_oom();
    return p;
}

static size_t align_up(size_t n, size_t a)
{
    if (n > SIZE_MAX - (a - 1))
        util_oom();
    return (n + (a - 1)) & ~(a - 1);
}

static arena_block *block_new(size_t cap)
{
    size_t hdr = align_up(sizeof(arena_block), ARENA_ALIGN);
    unsigned char *raw, *d;
    arena_block *b;
    size_t off;

    if (cap > SIZE_MAX - hdr - ARENA_ALIGN)
        util_oom();
    raw = (unsigned char *)xmalloc(hdr + cap + ARENA_ALIGN);
    b = (arena_block *)(void *)raw;
    d = raw + hdr;
    off = (size_t)((uintptr_t)d % ARENA_ALIGN);
    if (off != 0)
        d += ARENA_ALIGN - off;
    b->next = NULL;
    b->data = d;
    b->cap  = cap;
    b->used = 0;
    return b;
}

arena *arena_new(void)
{
    arena *a = (arena *)xmalloc(sizeof *a);
    a->bump = NULL;
    a->big = NULL;
    a->next_size = ARENA_FIRST_BLOCK;
    return a;
}

void *arena_alloc(arena *a, size_t n)
{
    size_t need;
    arena_block *b;

    if (!a)
        return NULL;
    if (n == 0)
        n = 1;
    need = align_up(n, ARENA_ALIGN);

    b = a->bump;
    if (b != NULL && b->cap - b->used >= need) {
        unsigned char *p = b->data + b->used;
        b->used += need;
        memset(p, 0, need);
        return p;
    }

    if (need > a->next_size) {
        /* Oversize request: give it a block of its own so that the current
         * bump block keeps its remaining space for later small allocations. */
        arena_block *nb = block_new(need);
        nb->next = a->big;
        a->big = nb;
        nb->used = need;
        memset(nb->data, 0, need);
        return nb->data;
    }

    b = block_new(a->next_size);
    b->next = a->bump;
    a->bump = b;
    if (a->next_size < ARENA_MAX_BLOCK) {
        a->next_size *= 2;
        if (a->next_size > ARENA_MAX_BLOCK)
            a->next_size = ARENA_MAX_BLOCK;
    }
    b->used = need;
    memset(b->data, 0, need);
    return b->data;
}

char *arena_strndup(arena *a, const char *s, size_t n)
{
    const char *nul;
    size_t len;
    char *out;

    if (!s)
        return NULL;
    nul = (const char *)memchr(s, '\0', n);
    len = nul ? (size_t)(nul - s) : n;
    out = (char *)arena_alloc(a, len + 1);
    if (len != 0)
        memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

char *arena_strdup(arena *a, const char *s)
{
    if (!s)
        return NULL;
    return arena_strndup(a, s, strlen(s));
}

char *arena_printf(arena *a, const char *fmt, ...)
{
    va_list ap, ap2;
    int n;
    char *buf;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);   /* size first, never a fixed buffer */
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return arena_strdup(a, "");
    }
    buf = (char *)arena_alloc(a, (size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return buf;
}

void arena_free(arena *a)
{
    arena_block *b, *nb;

    if (!a)
        return;
    for (b = a->bump; b != NULL; b = nb) {
        nb = b->next;
        free(b);
    }
    for (b = a->big; b != NULL; b = nb) {
        nb = b->next;
        free(b);
    }
    free(a);
}

/* ----------------------------------------------------------------- strbuf */

#define SB_MIN_CAP ((size_t)64)

void sb_init(strbuf *s, arena *a)
{
    s->p = NULL;
    s->len = 0;
    s->cap = 0;
    s->a = a;
}

/* Make room for `extra` more bytes plus the terminating NUL. */
static void sb_reserve(strbuf *s, size_t extra)
{
    size_t need, ncap;
    char *np;

    if (extra > SIZE_MAX - 1 - s->len)
        util_oom();
    need = s->len + extra + 1;
    if (need <= s->cap)
        return;
    ncap = (s->cap != 0) ? s->cap : SB_MIN_CAP;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2)
            util_oom();
        ncap *= 2;
    }
    np = (char *)arena_alloc(s->a, ncap);
    if (s->len != 0)
        memcpy(np, s->p, s->len);
    s->p = np;
    s->cap = ncap;
}

void sb_putc(strbuf *s, char c)
{
    sb_reserve(s, 1);
    s->p[s->len++] = c;
}

void sb_putn(strbuf *s, const char *t, size_t n)
{
    if (!t || n == 0)
        return;
    sb_reserve(s, n);
    memcpy(s->p + s->len, t, n);
    s->len += n;
}

void sb_puts(strbuf *s, const char *t)
{
    if (!t)
        return;
    sb_putn(s, t, strlen(t));
}

void sb_printf(strbuf *s, const char *fmt, ...)
{
    va_list ap, ap2;
    size_t avail;
    int n;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    avail = (s->cap > s->len) ? s->cap - s->len : 0;
    n = vsnprintf(avail ? s->p + s->len : NULL, avail, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    if ((size_t)n + 1 <= avail) {          /* fit on the first try */
        s->len += (size_t)n;
        va_end(ap2);
        return;
    }
    sb_reserve(s, (size_t)n);               /* grow and retry */
    vsnprintf(s->p + s->len, s->cap - s->len, fmt, ap2);
    va_end(ap2);
    s->len += (size_t)n;
}

char *sb_done(strbuf *s)
{
    sb_reserve(s, 0);
    s->p[s->len] = '\0';
    return s->p;
}

/* -------------------------------------------------------------------- vec */

#define VEC_MIN_CAP ((size_t)8)

void vec_init(vec *x, arena *a)
{
    x->v = NULL;
    x->len = 0;
    x->cap = 0;
    x->a = a;
}

void vec_push(vec *x, void *p)
{
    if (x->len == x->cap) {
        size_t ncap = (x->cap != 0) ? x->cap : VEC_MIN_CAP;
        void **nv;
        if (ncap > SIZE_MAX / (2 * sizeof(void *)))
            util_oom();
        if (x->cap != 0)
            ncap *= 2;
        nv = (void **)arena_alloc(x->a, ncap * sizeof(void *));
        if (x->len != 0)
            memcpy(nv, x->v, x->len * sizeof(void *));
        x->v = nv;
        x->cap = ncap;
    }
    x->v[x->len++] = p;
}

/* ----------------------------------------------------------------- strmap */

#define STRMAP_MIN_CAP ((size_t)16)

typedef struct {
    char     *key;     /* NULL == empty slot */
    void     *val;
    uint64_t  hash;
} strmap_slot;

struct strmap {
    arena       *a;
    strmap_slot *tab;
    size_t       cap;   /* power of two */
    size_t       len;
};

static uint64_t fnv1a(const char *s)
{
    uint64_t h = UINT64_C(1469598103934665603);
    const unsigned char *p = (const unsigned char *)s;
    while (*p != '\0') {
        h ^= (uint64_t)*p++;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

strmap *strmap_new(arena *a)
{
    strmap *m = (strmap *)arena_alloc(a, sizeof *m);
    m->a = a;
    m->cap = STRMAP_MIN_CAP;
    m->len = 0;
    m->tab = (strmap_slot *)arena_alloc(a, m->cap * sizeof(strmap_slot));
    return m;
}

/* Linear probing; cap is a power of two so the mask is cap-1. */
static strmap_slot *strmap_find(strmap_slot *tab, size_t cap,
                                const char *k, uint64_t h)
{
    size_t i = (size_t)h & (cap - 1);
    for (;;) {
        strmap_slot *s = &tab[i];
        if (s->key == NULL)
            return s;
        if (s->hash == h && strcmp(s->key, k) == 0)
            return s;
        i = (i + 1) & (cap - 1);
    }
}

static void strmap_grow(strmap *m)
{
    size_t ncap = m->cap * 2;
    strmap_slot *ntab;
    size_t i;

    if (ncap > SIZE_MAX / sizeof(strmap_slot))
        util_oom();
    ntab = (strmap_slot *)arena_alloc(m->a, ncap * sizeof(strmap_slot));
    for (i = 0; i < m->cap; i++) {
        if (m->tab[i].key != NULL) {
            strmap_slot *d = strmap_find(ntab, ncap, m->tab[i].key,
                                         m->tab[i].hash);
            *d = m->tab[i];
        }
    }
    m->tab = ntab;
    m->cap = ncap;
}

void strmap_put(strmap *m, const char *k, void *v)
{
    uint64_t h;
    strmap_slot *s;

    if (!m || !k)
        return;
    if ((m->len + 1) * 10 >= m->cap * 7)   /* keep load factor under 0.7 */
        strmap_grow(m);
    h = fnv1a(k);
    s = strmap_find(m->tab, m->cap, k, h);
    if (s->key == NULL) {
        s->key = arena_strdup(m->a, k);
        s->hash = h;
        m->len++;
    }
    s->val = v;
}

void *strmap_get(strmap *m, const char *k)
{
    strmap_slot *s;

    if (!m || !k)
        return NULL;
    s = strmap_find(m->tab, m->cap, k, fnv1a(k));
    return (s->key != NULL) ? s->val : NULL;
}
