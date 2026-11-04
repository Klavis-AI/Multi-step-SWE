#ifndef BUF_H
#define BUF_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *data; size_t len, cap; } buf_t;

static inline void buf_init(buf_t *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static inline void buf_free(buf_t *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }
static inline void buf_reserve(buf_t *b, size_t n)
{
    if (b->len + n <= b->cap) return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->len + n) cap *= 2;
    b->data = (uint8_t *)realloc(b->data, cap);
    b->cap = cap;
}
static inline void buf_put(buf_t *b, const void *p, size_t n)
{
    if (!n) return;
    buf_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}
static inline void buf_putc(buf_t *b, uint8_t c) { buf_reserve(b, 1); b->data[b->len++] = c; }
static inline void buf_zeros(buf_t *b, size_t n)
{
    if (!n) return;
    buf_reserve(b, n);
    memset(b->data + b->len, 0, n);
    b->len += n;
}
#endif
