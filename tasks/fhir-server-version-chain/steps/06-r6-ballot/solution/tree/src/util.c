#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define ARENA_BLOCK (1u << 20)

struct arena_block {
    arena_block *next;
    size_t       used, cap;
    char         data[];
};

void arena_init(arena *a) { a->head = NULL; a->total = 0; }

void *arena_alloc(arena *a, size_t n) {
    n = (n + 15u) & ~((size_t)15u);
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = n > ARENA_BLOCK ? n : ARENA_BLOCK;
        arena_block *b = malloc(sizeof(arena_block) + cap);
        if (!b) { fprintf(stderr, "oom\n"); exit(70); }
        b->next = a->head; b->used = 0; b->cap = cap;
        a->head = b;
    }
    void *p = a->head->data + a->head->used;
    a->head->used += n;
    a->total += n;
    return p;
}

void *arena_calloc(arena *a, size_t n) { void *p = arena_alloc(a, n); memset(p, 0, n); return p; }

char *arena_strdup(arena *a, const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *arena_strndup(arena *a, const char *s, size_t n) {
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n); p[n] = 0;
    return p;
}

char *arena_printf(arena *a, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *p = arena_alloc(a, (size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return p;
}

void arena_free(arena *a) {
    arena_block *b = a->head;
    while (b) { arena_block *nx = b->next; free(b); b = nx; }
    a->head = NULL; a->total = 0;
}

void buf_init(buf *b) { b->p = NULL; b->n = 0; b->cap = 0; }

void buf_reserve(buf *b, size_t extra) {
    if (b->n + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->n + extra + 1) cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) { fprintf(stderr, "oom\n"); exit(70); }
    b->p = p; b->cap = cap;
}

void buf_put(buf *b, const void *data, size_t n) {
    buf_reserve(b, n);
    memcpy(b->p + b->n, data, n);
    b->n += n; b->p[b->n] = 0;
}

void buf_puts(buf *b, const char *s) { if (s) buf_put(b, s, strlen(s)); }
void buf_putc(buf *b, char c) { buf_reserve(b, 1); b->p[b->n++] = c; b->p[b->n] = 0; }

void buf_printf(buf *b, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    buf_reserve(b, (size_t)n);
    vsnprintf(b->p + b->n, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->n += (size_t)n;
}

void buf_free(buf *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

char *buf_detach(buf *b) { char *p = b->p ? b->p : str_dup(""); b->p = NULL; b->n = b->cap = 0; return p; }

int str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

int str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; a++; b++; }
    return *a == *b;
}

int str_starts(const char *s, const char *pre) {
    if (!s || !pre) return 0;
    size_t n = strlen(pre);
    return strncmp(s, pre, n) == 0;
}

int str_ends(const char *s, const char *suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lf = strlen(suf);
    return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) { fprintf(stderr, "oom\n"); exit(70); }
    memcpy(p, s, n);
    return p;
}

char *str_ndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) { fprintf(stderr, "oom\n"); exit(70); }
    memcpy(p, s, n); p[n] = 0;
    return p;
}

char *str_lower(char *s) { for (char *p = s; p && *p; p++) *p = (char)tolower((unsigned char)*p); return s; }

void str_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
}

int hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void url_decode(char *s) {
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') { *o++ = ' '; }
        else if (*p == '%' && hex_val((unsigned char)p[1]) >= 0 && hex_val((unsigned char)p[2]) >= 0) {
            *o++ = (char)(hex_val((unsigned char)p[1]) * 16 + hex_val((unsigned char)p[2]));
            p += 2;
        } else *o++ = *p;
    }
    *o = 0;
}

void url_encode_into(buf *b, const char *s) {
    static const char *hex = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') buf_putc(b, (char)*p);
        else { buf_putc(b, '%'); buf_putc(b, hex[*p >> 4]); buf_putc(b, hex[*p & 15]); }
    }
}

void vec_init(vec *x) { x->v = NULL; x->n = x->cap = 0; }

void vec_push(vec *x, void *p) {
    if (x->n == x->cap) {
        x->cap = x->cap ? x->cap * 2 : 8;
        void **nv = realloc(x->v, x->cap * sizeof(void *));
        if (!nv) { fprintf(stderr, "oom\n"); exit(70); }
        x->v = nv;
    }
    x->v[x->n++] = p;
}

void vec_free(vec *x) { free(x->v); x->v = NULL; x->n = x->cap = 0; }

uint64_t str_hash(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        h ^= *p; h *= 1099511628211ull;
    }
    return h;
}

static void smap_grow(smap *m);

void smap_init(smap *m, size_t cap) {
    size_t c = 16;
    while (c < cap * 2) c *= 2;
    m->cap = c; m->n = 0;
    m->keys = calloc(c, sizeof(char *));
    m->vals = calloc(c, sizeof(void *));
    if (!m->keys || !m->vals) { fprintf(stderr, "oom\n"); exit(70); }
}

void smap_put(smap *m, const char *k, void *v) {
    if (!m->cap) smap_init(m, 16);
    if ((m->n + 1) * 2 > m->cap) smap_grow(m);
    size_t i = (size_t)(str_hash(k) & (m->cap - 1));
    while (m->keys[i]) {
        if (strcmp(m->keys[i], k) == 0) { m->vals[i] = v; return; }
        i = (i + 1) & (m->cap - 1);
    }
    m->keys[i] = k; m->vals[i] = v; m->n++;
}

void *smap_get(const smap *m, const char *k) {
    if (!m->cap || !k) return NULL;
    size_t i = (size_t)(str_hash(k) & (m->cap - 1));
    while (m->keys[i]) {
        if (strcmp(m->keys[i], k) == 0) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return NULL;
}

static void smap_grow(smap *m) {
    smap nm;
    smap_init(&nm, m->cap);
    for (size_t i = 0; i < m->cap; i++) if (m->keys[i]) smap_put(&nm, m->keys[i], m->vals[i]);
    free(m->keys); free(m->vals);
    *m = nm;
}

void smap_free(smap *m) { free(m->keys); free(m->vals); m->keys = NULL; m->vals = NULL; m->cap = m->n = 0; }
