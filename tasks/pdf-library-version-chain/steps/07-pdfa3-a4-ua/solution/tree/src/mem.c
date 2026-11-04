/* mem.c -- arena allocator and growable byte buffers.
 *
 * Two unrelated allocation schemes live here:
 *
 *   pdf_alloc/pdf_calloc/pdf_strdup_n bump-allocate from a chain of malloc'd
 *      blocks hanging off pdf_doc.arena. Nothing is ever freed individually;
 *      pdf_arena_free() (called from pdf_close) releases the whole chain. A
 *      NULL document uses a private process-wide scratch arena, which is what
 *      the writer uses before a pdf_doc exists.
 *
 *   pdf_buf_* are ordinary malloc/realloc buffers owned by the caller, growing
 *      by doubling. They hold decoded stream data, which must be freed
 *      promptly, so they deliberately do not touch the arena.
 */

#include "pdfint.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ arena */

#define ARENA_ALIGN      16u                    /* alignment of every handout */
#define ARENA_MIN_BLOCK  (64u * 1024u)          /* first block is 64 KiB      */
#define ARENA_MAX_BLOCK  (4u * 1024u * 1024u)   /* stop doubling here         */

/* Arena used when doc == NULL. Process-wide, not thread safe -- like the rest
 * of the library, which expects one document per thread. */
static pdf_arena_block *pdf_scratch_arena;

static size_t align_up(size_t n)
{
    return (n + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1);
}

/* Add one block to the chain. A general block becomes the new head so later
 * allocations bump through it; an oversized block (one dedicated to a single
 * large request, already full on arrival) is spliced in behind the head
 * instead, so the head keeps its free space and the size doubling keeps
 * tracking general blocks only. */
static void *arena_new_block(pdf_arena_block **head, size_t need, size_t cap)
{
    pdf_arena_block *nb;
    unsigned char *raw;
    size_t pad;
    int oversize = need > cap;

    if (oversize)
        cap = need;
    if (cap > SIZE_MAX - ARENA_ALIGN)
        return NULL;

    nb = malloc(sizeof *nb);
    if (!nb)
        return NULL;
    raw = malloc(cap + ARENA_ALIGN);    /* slack so we can align the payload */
    if (!raw) {
        free(nb);
        return NULL;
    }
    /* malloc() only promises max_align_t alignment (8 bytes on some 32-bit
     * targets), so bias 'used' until the payload starts 16-byte aligned. */
    pad = (size_t)((ARENA_ALIGN - ((uintptr_t)raw % ARENA_ALIGN)) % ARENA_ALIGN);

    nb->mem  = raw;
    nb->cap  = cap + pad;
    nb->used = pad + need;

    if (oversize && *head) {
        nb->next = (*head)->next;
        (*head)->next = nb;
    } else {
        nb->next = *head;
        *head = nb;
    }
    return raw + pad;
}

static void *arena_alloc(pdf_arena_block **head, size_t n)
{
    pdf_arena_block *blk;
    size_t need, cap;

    if (n == 0)
        n = 1;
    if (n > SIZE_MAX - ARENA_ALIGN)
        return NULL;
    need = align_up(n);

    blk = *head;
    if (blk && need <= blk->cap - blk->used) {      /* used <= cap invariant */
        unsigned char *p = blk->mem + blk->used;
        blk->used += need;
        return p;
    }

    cap = ARENA_MIN_BLOCK;
    if (blk) {
        cap = blk->cap;
        if (cap < ARENA_MAX_BLOCK) {
            cap *= 2;
            if (cap > ARENA_MAX_BLOCK)
                cap = ARENA_MAX_BLOCK;
        }
        if (cap < ARENA_MIN_BLOCK)
            cap = ARENA_MIN_BLOCK;
    }
    return arena_new_block(head, need, cap);
}

void *pdf_alloc(pdf_doc *doc, size_t n)
{
    return arena_alloc(doc ? &doc->arena : &pdf_scratch_arena, n);
}

void *pdf_calloc(pdf_doc *doc, size_t count, size_t n)
{
    void *p;
    size_t total;

    if (count == 0 || n == 0)
        total = 1;
    else {
        if (count > SIZE_MAX / n)
            return NULL;
        total = count * n;
    }
    p = pdf_alloc(doc, total);
    if (p)
        memset(p, 0, total);
    return p;
}

char *pdf_strdup_n(pdf_doc *doc, const char *s, size_t n)
{
    char *p;

    if (!s)
        n = 0;
    if (n > SIZE_MAX - 1)
        return NULL;
    p = pdf_alloc(doc, n + 1);
    if (!p)
        return NULL;
    if (n)
        memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* Release a whole block chain. */
void pdf_arena_free_blocks(pdf_arena_block *b)
{
    while (b) {
        pdf_arena_block *next = b->next;
        free(b->mem);
        free(b);
        b = next;
    }
}

/* Free everything ever allocated from doc's arena. Every pdf_obj the document
 * produced dies here, so this must be the last thing pdf_close() does.
 * A NULL document is a deliberate no-op: the scratch arena may still hold live
 * objects belonging to other documents. */
void pdf_arena_free(pdf_doc *doc)
{
    if (!doc)
        return;
    pdf_arena_free_blocks(doc->arena);
    doc->arena = NULL;
}

/* Alternate spelling, for callers that prefer it. */
void pdf_free_arena(pdf_doc *doc)
{
    pdf_arena_free(doc);
}

/* Drop the doc == NULL scratch arena. Only safe at process teardown. */
void pdf_arena_free_scratch(void)
{
    pdf_arena_free_blocks(pdf_scratch_arena);
    pdf_scratch_arena = NULL;
}

/* ---------------------------------------------------------------- buffers */

void pdf_buf_init(pdf_buf *b)
{
    if (!b)
        return;
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}

/* Make room for 'extra' more bytes plus the hidden NUL we keep one past b->n,
 * so (char *)b->p can be handed straight to string functions. */
static int buf_grow(pdf_buf *b, size_t extra)
{
    size_t need, cap;
    unsigned char *np;

    if (extra > SIZE_MAX - 1 - b->n)
        return PDF_E_NOMEM;
    need = b->n + extra + 1;
    if (need <= b->cap)
        return 0;

    cap = b->cap ? b->cap : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    np = realloc(b->p, cap);
    if (!np)
        return PDF_E_NOMEM;
    b->p = np;
    b->cap = cap;
    return 0;
}

int pdf_buf_put(pdf_buf *b, const void *data, size_t n)
{
    int rc;

    if (!b)
        return PDF_E_RANGE;
    if (n == 0)
        return 0;
    if (!data)
        return PDF_E_RANGE;
    if ((rc = buf_grow(b, n)) != 0)
        return rc;
    memcpy(b->p + b->n, data, n);
    b->n += n;
    b->p[b->n] = '\0';
    return 0;
}

int pdf_buf_putc(pdf_buf *b, int c)
{
    int rc;

    if (!b)
        return PDF_E_RANGE;
    if ((rc = buf_grow(b, 1)) != 0)
        return rc;
    b->p[b->n++] = (unsigned char)c;
    b->p[b->n] = '\0';
    return 0;
}

int pdf_buf_printf(pdf_buf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int need, rc;

    if (!b || !fmt)
        return PDF_E_RANGE;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);         /* first pass: how much room? */
    va_end(ap);
    if (need < 0) {
        va_end(ap2);
        return PDF_E_RANGE;
    }
    rc = buf_grow(b, (size_t)need);
    if (rc != 0) {
        va_end(ap2);
        return rc;
    }
    vsnprintf((char *)b->p + b->n, (size_t)need + 1, fmt, ap2);     /* second */
    va_end(ap2);
    b->n += (size_t)need;
    b->p[b->n] = '\0';
    return 0;
}

void pdf_buf_free(pdf_buf *b)
{
    if (!b)
        return;
    free(b->p);
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}
