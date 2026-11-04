/* cmap.c -- CMap parsing: /ToUnicode CMaps and embedded or predefined
 * encoding CMaps (ISO 32000-1:2008 9.7.5 and 9.10.3).
 *
 * One structure serves both jobs.  A CMap carries
 *   - codespace ranges, which say how many bytes the next code occupies,
 *   - cidrange / cidchar entries, mapping a code to a CID,
 *   - bfrange / bfchar entries, mapping a code to a Unicode string.
 * A /UseCMap parent is chained rather than copied, so a child's entries always
 * win and the parent supplies whatever the child does not define.
 *
 * Everything is allocated from the document arena and never freed individually.
 */

#include "fontint.h"

#include <string.h>
#include <stdlib.h>

#define CS_MAX      64
#define UNI_MAX     8
#define RANGE_MAX   0x10000     /* cap on the codes materialised for one range */

typedef struct {
    unsigned char n;
    unsigned int  u[UNI_MAX];
} uniseq;

typedef struct {
    unsigned int lo, hi;
    int          nbytes;
} codespace;

typedef struct {
    unsigned int lo, hi;
    int          cid;
} cidrange;

typedef struct {
    unsigned int   lo, hi;
    unsigned char  dst[32];     /* destination bytes for code == lo */
    unsigned char  dstlen;      /* 0 when "list" is used instead */
    uniseq        *list;        /* array destination: hi-lo+1 entries */
} bfrange;

struct pdf_cmap {
    pdf_doc *doc;
    char     name[96];
    char     usename[96];
    int      vertical;
    int      exact;
    int      defbytes;
    const struct pdf_cmap *parent;

    codespace cs[CS_MAX];
    int       ncs;

    cidrange *cid;
    size_t    ncid, cidcap;

    bfrange  *bf;
    size_t    nbf, bfcap;
};

/* ------------------------------------------------------------- utilities */

/* The arena has no realloc, so growth copies.  Ranges are small and this runs
 * once per font, so the waste is irrelevant. */
static void *grow(pdf_doc *doc, void *old, size_t elem, size_t *cap, size_t need)
{
    size_t nc = *cap ? *cap : 16;
    void *p;
    while (nc < need) {
        if (nc > (size_t)-1 / 2) return NULL;
        nc *= 2;
    }
    p = pdf_calloc(doc, nc, elem);
    if (!p) return NULL;
    if (old && *cap) memcpy(p, old, *cap * elem);
    *cap = nc;
    return p;
}

static void str_copy(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap) n = cap - 1;
    if (n && src) memcpy(dst, src, n);
    dst[n] = '\0';
}

/* A hex string token becomes a big-endian code of slen bytes. */
static int tok_code(const pdf_token *t, unsigned int *code, int *nb)
{
    size_t i;
    unsigned int v = 0;
    if (t->type != PDF_TOK_STRING || t->slen == 0 || t->slen > 4) return 0;
    for (i = 0; i < t->slen; i++)
        v = (v << 8) | (unsigned char)t->s[i];
    *code = v;
    *nb = (int)t->slen;
    return 1;
}

static int decode_utf16be(const unsigned char *b, size_t n,
                          unsigned int *out, int max)
{
    int k = 0;
    size_t i = 0;
    if (max <= 0) return 0;
    if (n == 1) { out[0] = b[0]; return 1; }
    while (i + 1 < n && k < max) {
        unsigned int w = (unsigned int)((b[i] << 8) | b[i + 1]);
        i += 2;
        if (w >= 0xD800u && w <= 0xDBFFu && i + 1 < n) {
            unsigned int w2 = (unsigned int)((b[i] << 8) | b[i + 1]);
            if (w2 >= 0xDC00u && w2 <= 0xDFFFu) {
                out[k++] = 0x10000u + ((w - 0xD800u) << 10) + (w2 - 0xDC00u);
                i += 2;
                continue;
            }
        }
        out[k++] = w;
    }
    return k;
}

static void seq_from_bytes(uniseq *q, const unsigned char *b, size_t n)
{
    unsigned int tmp[UNI_MAX];
    int k = decode_utf16be(b, n, tmp, UNI_MAX);
    int i;
    q->n = (unsigned char)(k < 0 ? 0 : k);
    for (i = 0; i < k; i++) q->u[i] = tmp[i];
}

static void seq_from_name(uniseq *q, const char *name)
{
    unsigned int tmp[UNI_MAX];
    int k = pdf_glyph_to_unicode(name, tmp, UNI_MAX);
    int i;
    q->n = (unsigned char)(k < 0 ? 0 : k);
    for (i = 0; i < k; i++) q->u[i] = tmp[i];
}

/* ------------------------------------------------------------ construction */

static pdf_cmap *cmap_new(pdf_doc *doc)
{
    pdf_cmap *cm = pdf_calloc(doc, 1, sizeof *cm);
    if (!cm) return NULL;
    cm->doc = doc;
    cm->exact = 1;
    cm->defbytes = 1;
    return cm;
}

static void add_codespace(pdf_cmap *cm, unsigned int lo, unsigned int hi, int nb)
{
    if (cm->ncs >= CS_MAX || nb < 1 || nb > 4) return;
    if (hi < lo) { unsigned int t = lo; lo = hi; hi = t; }
    cm->cs[cm->ncs].lo = lo;
    cm->cs[cm->ncs].hi = hi;
    cm->cs[cm->ncs].nbytes = nb;
    cm->ncs++;
}

static void add_cid(pdf_cmap *cm, unsigned int lo, unsigned int hi, int cid)
{
    if (hi < lo) { unsigned int t = lo; lo = hi; hi = t; }
    if (cm->ncid + 1 > cm->cidcap) {
        void *p = grow(cm->doc, cm->cid, sizeof(cidrange), &cm->cidcap, cm->ncid + 1);
        if (!p) return;
        cm->cid = p;
    }
    cm->cid[cm->ncid].lo = lo;
    cm->cid[cm->ncid].hi = hi;
    cm->cid[cm->ncid].cid = cid;
    cm->ncid++;
}

static bfrange *add_bf(pdf_cmap *cm, unsigned int lo, unsigned int hi)
{
    bfrange *r;
    if (hi < lo) { unsigned int t = lo; lo = hi; hi = t; }
    if (hi - lo >= RANGE_MAX) hi = lo + RANGE_MAX - 1;
    if (cm->nbf + 1 > cm->bfcap) {
        void *p = grow(cm->doc, cm->bf, sizeof(bfrange), &cm->bfcap, cm->nbf + 1);
        if (!p) return NULL;
        cm->bf = p;
    }
    r = &cm->bf[cm->nbf++];
    memset(r, 0, sizeof *r);
    r->lo = lo;
    r->hi = hi;
    return r;
}

pdf_cmap *pdf_cmap_identity(pdf_doc *doc, int vertical)
{
    pdf_cmap *cm = cmap_new(doc);
    if (!cm) return NULL;
    str_copy(cm->name, sizeof cm->name,
             vertical ? "Identity-V" : "Identity-H", vertical ? 10 : 10);
    cm->vertical = vertical ? 1 : 0;
    cm->defbytes = 2;
    add_codespace(cm, 0x0000, 0xFFFF, 2);
    add_cid(cm, 0x0000, 0xFFFF, 0);
    return cm;
}

/* ---------------------------------------------------------------- parsing */

typedef struct {
    int         type;
    long long   i;
    char        s[128];
    size_t      slen;
} opnd;

static void push_opnd(opnd *ring, int *nr, const pdf_token *t)
{
    opnd *o;
    if (*nr == 4) {
        memmove(ring, ring + 1, 3 * sizeof *ring);
        *nr = 3;
    }
    o = &ring[(*nr)++];
    o->type = t->type;
    o->i = t->i;
    o->slen = t->slen;
    str_copy(o->s, sizeof o->s, t->s, t->slen ? t->slen : (t->s ? strlen(t->s) : 0));
}

static void parse_codespace(pdf_doc *doc, pdf_cmap *cm, pdf_lexer *lx)
{
    pdf_token t;
    for (;;) {
        unsigned int lo, hi;
        int nb1, nb2;
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &lo, &nb1)) return;          /* endcodespacerange */
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &hi, &nb2)) return;
        add_codespace(cm, lo, hi, nb1 > nb2 ? nb1 : nb2);
    }
}

static void parse_cidrange(pdf_doc *doc, pdf_cmap *cm, pdf_lexer *lx)
{
    pdf_token t;
    for (;;) {
        unsigned int lo, hi;
        int nb;
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &lo, &nb)) return;            /* endcidrange */
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &hi, &nb)) return;
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type != PDF_TOK_INT) return;
        add_cid(cm, lo, hi, (int)t.i);
    }
}

static void parse_cidchar(pdf_doc *doc, pdf_cmap *cm, pdf_lexer *lx)
{
    pdf_token t;
    for (;;) {
        unsigned int c;
        int nb;
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &c, &nb)) return;             /* endcidchar */
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type != PDF_TOK_INT) return;
        add_cid(cm, c, c, (int)t.i);
    }
}

static void parse_bfchar(pdf_doc *doc, pdf_cmap *cm, pdf_lexer *lx)
{
    pdf_token t;
    for (;;) {
        unsigned int c;
        int nb;
        bfrange *r;
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &c, &nb)) return;             /* endbfchar */
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;

        r = add_bf(cm, c, c);
        if (!r) return;
        r->list = pdf_calloc(cm->doc, 1, sizeof(uniseq));
        if (!r->list) { cm->nbf--; return; }
        if (t.type == PDF_TOK_STRING)
            seq_from_bytes(r->list, (const unsigned char *)t.s, t.slen);
        else if (t.type == PDF_TOK_NAME)
            seq_from_name(r->list, t.s ? t.s : "");
        else
            return;
    }
}

static void parse_bfrange(pdf_doc *doc, pdf_cmap *cm, pdf_lexer *lx)
{
    pdf_token t;
    for (;;) {
        unsigned int lo, hi;
        int nb;
        bfrange *r;

        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &lo, &nb)) return;            /* endbfrange */
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
        if (!tok_code(&t, &hi, &nb)) return;
        if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;

        if (t.type == PDF_TOK_ARRAY_OPEN) {
            /* One destination per code in the range. */
            size_t count, k = 0;
            r = add_bf(cm, lo, hi);
            if (!r) return;
            count = (size_t)(r->hi - r->lo) + 1;
            r->list = pdf_calloc(cm->doc, count, sizeof(uniseq));
            if (!r->list) { cm->nbf--; return; }
            for (;;) {
                if (pdf_lex_next(doc, lx, &t) != 0 || t.type == PDF_TOK_EOF) return;
                if (t.type == PDF_TOK_ARRAY_CLOSE) break;
                if (k >= count) continue;              /* extra entries ignored */
                if (t.type == PDF_TOK_STRING)
                    seq_from_bytes(&r->list[k], (const unsigned char *)t.s, t.slen);
                else if (t.type == PDF_TOK_NAME)
                    seq_from_name(&r->list[k], t.s ? t.s : "");
                k++;
            }
        } else if (t.type == PDF_TOK_STRING) {
            /* A single destination that increments in its last byte. */
            size_t n = t.slen;
            r = add_bf(cm, lo, hi);
            if (!r) return;
            if (n > sizeof r->dst) n = sizeof r->dst;
            memcpy(r->dst, t.s, n);
            r->dstlen = (unsigned char)n;
        } else if (t.type == PDF_TOK_NAME) {
            r = add_bf(cm, lo, hi);
            if (!r) return;
            r->list = pdf_calloc(cm->doc, 1, sizeof(uniseq));
            if (!r->list) { cm->nbf--; return; }
            seq_from_name(r->list, t.s ? t.s : "");
            r->hi = r->lo;                              /* a name cannot increment */
        } else {
            return;
        }
    }
}

static void do_def(pdf_cmap *cm, const opnd *ring, int nr)
{
    const opnd *k, *v;
    if (nr < 2) return;
    k = &ring[nr - 2];
    v = &ring[nr - 1];
    if (k->type != PDF_TOK_NAME) return;
    if (!strcmp(k->s, "WMode") && v->type == PDF_TOK_INT)
        cm->vertical = (v->i != 0);
    else if (!strcmp(k->s, "CMapName") && v->type == PDF_TOK_NAME)
        str_copy(cm->name, sizeof cm->name, v->s, strlen(v->s));
}

static void do_usecmap(pdf_doc *doc, pdf_cmap *cm, const opnd *ring, int nr)
{
    const opnd *n;
    if (nr < 1) return;
    n = &ring[nr - 1];
    if (n->type != PDF_TOK_NAME || !n->s[0]) return;
    str_copy(cm->usename, sizeof cm->usename, n->s, strlen(n->s));
    if (!cm->parent) {
        pdf_cmap *p = pdf_cmap_predefined(doc, n->s);
        if (p) {
            cm->parent = p;
            if (!p->exact) cm->exact = 0;
        }
    }
}

pdf_cmap *pdf_cmap_parse(pdf_doc *doc, const unsigned char *src, size_t len)
{
    pdf_lexer lx;
    pdf_token tok;
    pdf_cmap *cm;
    opnd ring[4];
    int nr = 0;

    if (!src) return NULL;
    cm = cmap_new(doc);
    if (!cm) return NULL;

    pdf_lex_init(&lx, src, len, 0);
    while (pdf_lex_next(doc, &lx, &tok) == 0) {
        if (tok.type == PDF_TOK_EOF || tok.type == PDF_TOK_ERROR) break;
        if (tok.type == PDF_TOK_KEYWORD && tok.s) {
            const char *k = tok.s;
            if (!strcmp(k, "begincodespacerange"))   parse_codespace(doc, cm, &lx);
            else if (!strcmp(k, "begincidrange"))    parse_cidrange(doc, cm, &lx);
            else if (!strcmp(k, "begincidchar"))     parse_cidchar(doc, cm, &lx);
            else if (!strcmp(k, "beginbfrange"))     parse_bfrange(doc, cm, &lx);
            else if (!strcmp(k, "beginbfchar"))      parse_bfchar(doc, cm, &lx);
            else if (!strcmp(k, "usecmap"))          do_usecmap(doc, cm, ring, nr);
            else if (!strcmp(k, "def"))              do_def(cm, ring, nr);
            nr = 0;
        } else {
            push_opnd(ring, &nr, &tok);
        }
    }

    /* A CMap with no codespace range at all is almost always a single-byte
     * /ToUnicode map; infer the width from the entries we did see. */
    if (cm->ncs == 0 && cm->nbf > 0) {
        unsigned int top = 0;
        size_t i;
        for (i = 0; i < cm->nbf; i++) if (cm->bf[i].hi > top) top = cm->bf[i].hi;
        cm->defbytes = top > 0xFF ? 2 : 1;
    }
    return cm;
}

/* ------------------------------------------------ predefined encoding CMaps
 *
 * The predefined CMaps of ISO 32000-1 9.7.5.2 are ordinary Adobe resource
 * files.  The ones a document here names by hand are built in from their
 * published contents; anything else still falls back to the two-byte identity
 * further down, flagged inexact.  A CMap's notdefrange section is not
 * modelled -- it only ever hands a substitute CID to control codes. */

typedef struct { unsigned int lo, hi; int nbytes; } pd_cs;
typedef struct { unsigned int lo, hi; int cid; } pd_cidrange;

/* codespace: 4 ranges */
static const pd_cs cs_90ms_rksj[] = {
    { 0x0000, 0x0080, 1 },
    { 0x8140, 0x9FFC, 2 },
    { 0x00A0, 0x00DF, 1 },
    { 0xE040, 0xFCFC, 2 },
};
/* cidrange: 171 entries, notdef: [(0, 31, 231)] */
static const pd_cidrange cid_90ms_rksj[] = {
    { 0x0020, 0x007D,   231 },
    { 0x007E, 0x007E,   631 },
    { 0x8140, 0x817E,   633 },
    { 0x8180, 0x81AC,   696 },
    { 0x81B8, 0x81BF,   741 },
    { 0x81C8, 0x81CE,   749 },
    { 0x81DA, 0x81E8,   756 },
    { 0x81F0, 0x81F7,   771 },
    { 0x81FC, 0x81FC,   779 },
    { 0x824F, 0x8258,   780 },
    { 0x8260, 0x8279,   790 },
    { 0x8281, 0x829A,   816 },
    { 0x829F, 0x82F1,   842 },
    { 0x8340, 0x837E,   925 },
    { 0x8380, 0x8396,   988 },
    { 0x839F, 0x83B6,  1011 },
    { 0x83BF, 0x83D6,  1035 },
    { 0x8440, 0x8460,  1059 },
    { 0x8470, 0x847E,  1092 },
    { 0x8480, 0x8491,  1107 },
    { 0x849F, 0x849F,  7479 },
    { 0x84A0, 0x84A0,  7481 },
    { 0x84A1, 0x84A1,  7491 },
    { 0x84A2, 0x84A2,  7495 },
    { 0x84A3, 0x84A3,  7503 },
    { 0x84A4, 0x84A4,  7499 },
    { 0x84A5, 0x84A5,  7507 },
    { 0x84A6, 0x84A6,  7523 },
    { 0x84A7, 0x84A7,  7515 },
    { 0x84A8, 0x84A8,  7531 },
    { 0x84A9, 0x84A9,  7539 },
    { 0x84AA, 0x84AA,  7480 },
    { 0x84AB, 0x84AB,  7482 },
    { 0x84AC, 0x84AC,  7494 },
    { 0x84AD, 0x84AD,  7498 },
    { 0x84AE, 0x84AE,  7506 },
    { 0x84AF, 0x84AF,  7502 },
    { 0x84B0, 0x84B0,  7514 },
    { 0x84B1, 0x84B1,  7530 },
    { 0x84B2, 0x84B2,  7522 },
    { 0x84B3, 0x84B3,  7538 },
    { 0x84B4, 0x84B4,  7554 },
    { 0x84B5, 0x84B5,  7511 },
    { 0x84B6, 0x84B6,  7526 },
    { 0x84B7, 0x84B7,  7519 },
    { 0x84B8, 0x84B8,  7534 },
    { 0x84B9, 0x84B9,  7542 },
    { 0x84BA, 0x84BA,  7508 },
    { 0x84BB, 0x84BB,  7527 },
    { 0x84BC, 0x84BC,  7516 },
    { 0x84BD, 0x84BD,  7535 },
    { 0x84BE, 0x84BE,  7545 },
    { 0x8740, 0x875D,  7555 },
    { 0x875F, 0x8760,  7585 },
    { 0x8761, 0x8761,  8038 },
    { 0x8762, 0x8762,  7588 },
    { 0x8763, 0x8763,  8040 },
    { 0x8764, 0x8764,  7590 },
    { 0x8765, 0x8765,  8042 },
    { 0x8766, 0x8767,  7592 },
    { 0x8768, 0x8768,  8044 },
    { 0x8769, 0x876A,  7595 },
    { 0x876B, 0x876B,  8043 },
    { 0x876C, 0x876D,  7598 },
    { 0x876E, 0x876E,  8047 },
    { 0x876F, 0x8775,  7601 },
    { 0x877E, 0x877E,  8323 },
    { 0x8780, 0x8783,  7608 },
    { 0x8784, 0x8784,  8055 },
    { 0x8785, 0x878F,  7613 },
    { 0x8790, 0x8790,   762 },
    { 0x8791, 0x8791,   761 },
    { 0x8792, 0x8792,   769 },
    { 0x8793, 0x8799,  7624 },
    { 0x879A, 0x879A,   768 },
    { 0x879B, 0x879C,  7631 },
    { 0x889F, 0x88FC,  1125 },
    { 0x8940, 0x897E,  1219 },
    { 0x8980, 0x89FC,  1282 },
    { 0x8A40, 0x8A7E,  1407 },
    { 0x8A80, 0x8AFC,  1470 },
    { 0x8B40, 0x8B7E,  1595 },
    { 0x8B80, 0x8BFC,  1658 },
    { 0x8C40, 0x8C7E,  1783 },
    { 0x8C80, 0x8CFC,  1846 },
    { 0x8D40, 0x8D7E,  1971 },
    { 0x8D80, 0x8DFC,  2034 },
    { 0x8E40, 0x8E7E,  2159 },
    { 0x8E80, 0x8EFC,  2222 },
    { 0x8F40, 0x8F7E,  2347 },
    { 0x8F80, 0x8FFC,  2410 },
    { 0x9040, 0x907E,  2535 },
    { 0x9080, 0x90FC,  2598 },
    { 0x9140, 0x917E,  2723 },
    { 0x9180, 0x91FC,  2786 },
    { 0x9240, 0x927E,  2911 },
    { 0x9280, 0x92FC,  2974 },
    { 0x9340, 0x937E,  3099 },
    { 0x9380, 0x93FC,  3162 },
    { 0x9440, 0x947E,  3287 },
    { 0x9480, 0x94FC,  3350 },
    { 0x9540, 0x957E,  3475 },
    { 0x9580, 0x95FC,  3538 },
    { 0x9640, 0x967E,  3663 },
    { 0x9680, 0x96FC,  3726 },
    { 0x9740, 0x977E,  3851 },
    { 0x9780, 0x97FC,  3914 },
    { 0x9840, 0x9872,  4039 },
    { 0x989F, 0x98FC,  4090 },
    { 0x9940, 0x997E,  4184 },
    { 0x9980, 0x99FC,  4247 },
    { 0x9A40, 0x9A7E,  4372 },
    { 0x9A80, 0x9AFC,  4435 },
    { 0x9B40, 0x9B7E,  4560 },
    { 0x9B80, 0x9BFC,  4623 },
    { 0x9C40, 0x9C7E,  4748 },
    { 0x9C80, 0x9CFC,  4811 },
    { 0x9D40, 0x9D7E,  4936 },
    { 0x9D80, 0x9DFC,  4999 },
    { 0x9E40, 0x9E7E,  5124 },
    { 0x9E80, 0x9EFC,  5187 },
    { 0x9F40, 0x9F7E,  5312 },
    { 0x9F80, 0x9FFC,  5375 },
    { 0x00A0, 0x00DF,   326 },
    { 0xE040, 0xE07E,  5500 },
    { 0xE080, 0xE0FC,  5563 },
    { 0xE140, 0xE17E,  5688 },
    { 0xE180, 0xE1FC,  5751 },
    { 0xE240, 0xE27E,  5876 },
    { 0xE280, 0xE2FC,  5939 },
    { 0xE340, 0xE37E,  6064 },
    { 0xE380, 0xE3FC,  6127 },
    { 0xE440, 0xE47E,  6252 },
    { 0xE480, 0xE4FC,  6315 },
    { 0xE540, 0xE57E,  6440 },
    { 0xE580, 0xE5FC,  6503 },
    { 0xE640, 0xE67E,  6628 },
    { 0xE680, 0xE6FC,  6691 },
    { 0xE740, 0xE77E,  6816 },
    { 0xE780, 0xE7FC,  6879 },
    { 0xE840, 0xE87E,  7004 },
    { 0xE880, 0xE8FC,  7067 },
    { 0xE940, 0xE97E,  7192 },
    { 0xE980, 0xE9FC,  7255 },
    { 0xEA40, 0xEA7E,  7380 },
    { 0xEA80, 0xEAA2,  7443 },
    { 0xEAA3, 0xEAA4,  8284 },
    { 0xED40, 0xED7E,  8359 },
    { 0xED80, 0xEDB3,  8422 },
    { 0xEDB4, 0xEDB4,  1993 },
    { 0xEDB5, 0xEDFC,  8474 },
    { 0xEE40, 0xEE7E,  8546 },
    { 0xEE80, 0xEEEC,  8609 },
    { 0xEEEF, 0xEEF8,  8092 },
    { 0xEEF9, 0xEEF9,   751 },
    { 0xEEFA, 0xEEFC,  8005 },
    { 0xFA40, 0xFA49,  8092 },
    { 0xFA4A, 0xFA53,  7575 },
    { 0xFA54, 0xFA54,   751 },
    { 0xFA55, 0xFA57,  8005 },
    { 0xFA58, 0xFA58,  7618 },
    { 0xFA59, 0xFA59,  7610 },
    { 0xFA5A, 0xFA5A,  8055 },
    { 0xFA5B, 0xFA5B,   768 },
    { 0xFA5C, 0xFA7E,  8359 },
    { 0xFA80, 0xFACF,  8394 },
    { 0xFAD0, 0xFAD0,  1993 },
    { 0xFAD1, 0xFAFC,  8474 },
    { 0xFB40, 0xFB7E,  8518 },
    { 0xFB80, 0xFBFC,  8581 },
    { 0xFC40, 0xFC4B,  8706 },
};

typedef struct {
    const char        *name;
    int                vertical;
    const pd_cs       *cs;
    size_t             ncs;
    const pd_cidrange *cid;
    size_t             ncid;
} pd_cmap;

#define PD_N(a) (sizeof (a) / sizeof (a)[0])

static const pd_cmap g_predefined[] = {
    { "90ms-RKSJ-H", 0, cs_90ms_rksj, PD_N(cs_90ms_rksj),
      cid_90ms_rksj, PD_N(cid_90ms_rksj) }
};

pdf_cmap *pdf_cmap_predefined(pdf_doc *doc, const char *name)
{
    size_t n;
    pdf_cmap *cm;

    if (!name || !*name) return NULL;
    if (!strcmp(name, "Identity-H")) return pdf_cmap_identity(doc, 0);
    if (!strcmp(name, "Identity-V")) return pdf_cmap_identity(doc, 1);

    {
        size_t k;
        for (k = 0; k < PD_N(g_predefined); k++) {
            const pd_cmap *pd = &g_predefined[k];
            size_t j;

            if (strcmp(name, pd->name) != 0) continue;
            cm = cmap_new(doc);
            if (!cm) return NULL;
            str_copy(cm->name, sizeof cm->name, name, strlen(name));
            cm->vertical = pd->vertical;
            for (j = 0; j < pd->ncs; j++)
                add_codespace(cm, pd->cs[j].lo, pd->cs[j].hi, pd->cs[j].nbytes);
            for (j = 0; j < pd->ncid; j++)
                add_cid(cm, pd->cid[j].lo, pd->cid[j].hi, pd->cid[j].cid);
            return cm;
        }
    }

    /* Every other predefined CMap needs the Adobe character-collection data,
     * which this library does not ship.  Two-byte identity is the closest
     * approximation; the font is flagged inexact so callers can say so. */
    n = strlen(name);
    cm = pdf_cmap_identity(doc, (n >= 2 && !strcmp(name + n - 2, "-V")));
    if (!cm) return NULL;
    str_copy(cm->name, sizeof cm->name, name, n);
    cm->exact = 0;
    return cm;
}

void pdf_cmap_set_parent(pdf_cmap *cm, const pdf_cmap *parent)
{
    if (!cm || !parent || cm == parent) return;
    cm->parent = parent;
    if (!parent->exact) cm->exact = 0;
}

const char *pdf_cmap_usecmap_name(const pdf_cmap *cm)
{
    return (cm && cm->usename[0]) ? cm->usename : NULL;
}

/* ---------------------------------------------------------------- lookups */

int pdf_cmap_vertical(const pdf_cmap *cm) { return cm ? cm->vertical : 0; }
int pdf_cmap_exact(const pdf_cmap *cm)    { return cm ? cm->exact : 0; }

const char *pdf_cmap_name(const pdf_cmap *cm)
{
    return (cm && cm->name[0]) ? cm->name : NULL;
}

int pdf_cmap_next(const pdf_cmap *cm, const unsigned char *s, size_t n,
                  unsigned int *code, int *nbytes)
{
    const pdf_cmap *c;
    int take = 0, shortest = 0, i;
    unsigned int v = 0;

    if (!s || n == 0) return -1;
    if (!cm) {
        *code = s[0];
        *nbytes = 1;
        return 0;
    }

    /* ISO 32000-1 9.7.6.2: the first byte selects the codespace range, and the
     * range's length decides how many bytes the code occupies. */
    for (c = cm; c && !take; c = c->parent) {
        for (i = 0; i < c->ncs; i++) {
            int nb = c->cs[i].nbytes;
            unsigned int lob = (c->cs[i].lo >> ((nb - 1) * 8)) & 0xFFu;
            unsigned int hib = (c->cs[i].hi >> ((nb - 1) * 8)) & 0xFFu;
            if (!shortest || nb < shortest) shortest = nb;
            if (s[0] >= lob && s[0] <= hib && (!take || nb < take)) take = nb;
        }
    }
    if (!take) take = shortest ? shortest : cm->defbytes;
    if (take < 1) take = 1;
    if ((size_t)take > n) take = (int)n;

    for (i = 0; i < take; i++) v = (v << 8) | s[i];
    *code = v;
    *nbytes = take;
    return 0;
}

int pdf_cmap_cid(const pdf_cmap *cm, unsigned int code)
{
    const pdf_cmap *c;
    for (c = cm; c; c = c->parent) {
        size_t i = c->ncid;
        while (i-- > 0) {       /* later entries override earlier ones */
            if (code >= c->cid[i].lo && code <= c->cid[i].hi)
                return c->cid[i].cid + (int)(code - c->cid[i].lo);
        }
    }
    return -1;
}

int pdf_cmap_has_unicode(const pdf_cmap *cm)
{
    const pdf_cmap *c;
    for (c = cm; c; c = c->parent)
        if (c->nbf > 0) return 1;
    return 0;
}

int pdf_cmap_unicode(const pdf_cmap *cm, unsigned int code,
                     unsigned int *out, int max)
{
    const pdf_cmap *c;

    if (!out || max <= 0) return 0;
    for (c = cm; c; c = c->parent) {
        size_t i = c->nbf;
        while (i-- > 0) {
            const bfrange *r = &c->bf[i];
            int k;
            if (code < r->lo || code > r->hi) continue;

            if (r->list) {
                const uniseq *q = &r->list[code - r->lo];
                int nu = q->n;
                if (nu > max) nu = max;
                for (k = 0; k < nu; k++) out[k] = q->u[k];
                if (nu) return nu;
                continue;       /* an empty slot is not a mapping */
            }
            if (r->dstlen) {
                unsigned char tmp[32];
                unsigned int delta = code - r->lo;
                int j;
                memcpy(tmp, r->dst, r->dstlen);
                /* The destination increments in its last byte (9.10.3); carry
                 * upward so ranges wider than 256 still behave. */
                for (j = (int)r->dstlen - 1; j >= 0 && delta; j--) {
                    unsigned int sum = (unsigned int)tmp[j] + (delta & 0xFFu);
                    tmp[j] = (unsigned char)(sum & 0xFFu);
                    delta = (delta >> 8) + (sum >> 8);
                }
                return decode_utf16be(tmp, r->dstlen, out, max);
            }
        }
    }
    return 0;
}
