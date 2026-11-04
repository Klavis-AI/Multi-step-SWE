/* util.c -- arena allocator, byte strings, growable buffers, diagnostics.
 * These are the primitives declared by pb.h that every later stage uses. */
#include "pb.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* =================================================================== arena */

#define ARENA_ALIGN 16u
#define ARENA_HDR 32u             /* multiple of ARENA_ALIGN */
#define ARENA_BLOCK (256u * 1024u)

typedef struct ABlock {
  struct ABlock *next;
  size_t used, cap;
} ABlock;

struct Arena {
  ABlock *head;
};

_Static_assert(sizeof(ABlock) <= ARENA_HDR, "arena header too small");

static void oom(void) {
  fputs("pb: out of memory\n", stderr);
  abort();
}

static char *ablk_data(ABlock *b) { return (char *)b + ARENA_HDR; }

static ABlock *ablk_new(size_t cap) {
  ABlock *b = (ABlock *)malloc(ARENA_HDR + cap);
  if (b == NULL) oom();
  b->next = NULL;
  b->used = 0;
  b->cap = cap;
  return b;
}

Arena *arena_new(void) {
  Arena *a = (Arena *)malloc(sizeof(Arena));
  if (a == NULL) oom();
  a->head = ablk_new(ARENA_BLOCK);
  return a;
}

void *arena_alloc(Arena *a, size_t n) {
  ABlock *h;
  char *p;

  n = (n + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1);
  if (n == 0) n = ARENA_ALIGN;

  h = a->head;
  if (h->cap - h->used < n) {
    if (n > ARENA_BLOCK / 2) {
      /* Oversized: give it a dedicated block but keep the current block
       * active so the remaining space in it is not wasted. */
      ABlock *b = ablk_new(n);
      b->used = n;
      b->next = h->next;
      h->next = b;
      return ablk_data(b);
    }
    h = ablk_new(ARENA_BLOCK);
    h->next = a->head;
    a->head = h;
  }
  p = ablk_data(h) + h->used;
  h->used += n;
  return p;
}

void *arena_calloc(Arena *a, size_t n) {
  void *p = arena_alloc(a, n);
  memset(p, 0, n);
  return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
  char *p = (char *)arena_alloc(a, n + 1);
  if (n != 0) memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

char *arena_strdup(Arena *a, const char *s) {
  return arena_strndup(a, s, strlen(s));
}

char *arena_printf(Arena *a, const char *fmt, ...) {
  va_list ap;
  int need;
  char *p;

  va_start(ap, fmt);
  need = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (need < 0) need = 0;

  p = (char *)arena_alloc(a, (size_t)need + 1);
  va_start(ap, fmt);
  vsnprintf(p, (size_t)need + 1, fmt, ap);
  va_end(ap);
  return p;
}

void arena_free(Arena *a) {
  ABlock *b;
  if (a == NULL) return;
  b = a->head;
  while (b != NULL) {
    ABlock *next = b->next;
    free(b);
    b = next;
  }
  free(a);
}

/* =================================================================== bytes */

Str str_cstr(const char *s) {
  Str r;
  r.p = s;
  r.n = (s == NULL) ? 0 : strlen(s);
  return r;
}

bool str_eq(Str a, Str b) {
  if (a.n != b.n) return false;
  if (a.n == 0) return true;
  return memcmp(a.p, b.p, a.n) == 0;
}

bool str_eq_c(Str a, const char *b) { return str_eq(a, str_cstr(b)); }

/* ================================================================= buffers */

void buf_init(Buf *b, Arena *a) {
  b->p = NULL;
  b->n = 0;
  b->cap = 0;
  b->a = a;
}

static void buf_reserve(Buf *b, size_t extra) {
  size_t want, cap;
  char *p;

  want = b->n + extra;
  if (want <= b->cap) return;
  cap = (b->cap != 0) ? b->cap : 64;
  while (cap < want) cap *= 2;
  p = (char *)arena_alloc(b->a, cap);
  if (b->n != 0) memcpy(p, b->p, b->n);
  b->p = p;
  b->cap = cap;
}

void buf_put(Buf *b, const void *p, size_t n) {
  if (n == 0) return;
  buf_reserve(b, n);
  memcpy(b->p + b->n, p, n);
  b->n += n;
}

void buf_byte(Buf *b, uint8_t c) {
  buf_reserve(b, 1);
  b->p[b->n++] = (char)c;
}

void buf_cstr(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }

void buf_printf(Buf *b, const char *fmt, ...) {
  va_list ap;
  int need;

  va_start(ap, fmt);
  need = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (need <= 0) return;

  buf_reserve(b, (size_t)need + 1);
  va_start(ap, fmt);
  vsnprintf(b->p + b->n, (size_t)need + 1, fmt, ap);
  va_end(ap);
  b->n += (size_t)need;
}

Str buf_str(const Buf *b) {
  Str s;
  s.p = b->p;
  s.n = b->n;
  return s;
}

/* ============================================================ diagnostics */

void diags_init(Diags *d, Arena *a) {
  d->head = NULL;
  d->tail = &d->head;
  d->a = a;
  d->n = 0;
}

void diag_add(Diags *d, const char *file, int line, int col, const char *fmt,
              ...) {
  va_list ap;
  int need;
  char *msg;
  Diag *e;

  va_start(ap, fmt);
  need = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (need < 0) need = 0;

  msg = (char *)arena_alloc(d->a, (size_t)need + 1);
  va_start(ap, fmt);
  vsnprintf(msg, (size_t)need + 1, fmt, ap);
  va_end(ap);

  e = (Diag *)arena_alloc(d->a, sizeof(Diag));
  e->file = file;
  e->line = line;
  e->col = col;
  e->msg = msg;
  e->next = NULL;

  *d->tail = e;
  d->tail = &e->next;
  d->n++;
}
