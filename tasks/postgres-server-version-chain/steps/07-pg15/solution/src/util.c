/* util.c - allocation and byte-buffer helpers. */
#include "pgvc.h"

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "out of memory\n"); abort(); }
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void buf_init(Buf *b) { b->data = NULL; b->len = b->cap = 0; }
void buf_free(Buf *b) { free(b->data); buf_init(b); }
void buf_reset(Buf *b) { b->len = 0; }

static void buf_grow(Buf *b, size_t need)
{
    if (b->len + need + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->len + need + 1) cap *= 2;
    b->data = xrealloc(b->data, cap);
    b->cap = cap;
}

void buf_add(Buf *b, const void *p, size_t n)
{
    buf_grow(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void buf_addc(Buf *b, char c) { buf_grow(b, 1); b->data[b->len++] = c; b->data[b->len] = '\0'; }
void buf_adds(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        size_t avail = b->cap > b->len ? b->cap - b->len : 0;
        va_start(ap, fmt);
        int n = vsnprintf(b->data ? b->data + b->len : NULL, avail, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if ((size_t) n < avail) { b->len += (size_t) n; return; }
        buf_grow(b, (size_t) n + 1);
    }
}

char *buf_steal(Buf *b)
{
    char *p = b->data ? b->data : xstrdup("");
    buf_init(b);
    return p;
}
