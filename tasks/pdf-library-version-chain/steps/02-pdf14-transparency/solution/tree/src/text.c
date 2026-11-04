/* text.c -- the content-stream text machine (ISO 32000-1:2008 clause 9).
 *
 * Runs the operators of 9.4 over a page's content, keeping the graphics state
 * stack of 8.4 and the text state of 9.3, and records one pdf_glyph per shown
 * character code.  Glyph positions come out in poppler's convention: origin at
 * the top-left of the CropBox with y increasing downwards, after /Rotate.
 *
 * Note on the coordinate system: pdf.h's comment on pdf_glyph says "y up".
 * The layout this module produces is top-left based instead, which is what
 * makes reading-order reconstruction and a pdftotext-compatible front end
 * work; x grows right and y grows down, matching poppler's device space.
 */

#include "fontint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FORM_DEPTH   12
#define MAX_GSTACK       64
#define MAX_OPS          32
#define MAX_TJ           8192
#define MAX_GLYPHS       4000000

/* Spacing heuristics, all relative to the glyph's own size.  GAP_SPACE is
 * poppler's minWordBreakSpace: the inter-character gap, measured along the
 * baseline, that ends a word.  It is a fraction of the device font size and
 * not of the glyph's advance, so a condensed font (a small Tz) breaks at the
 * same absolute gap as an unscaled one. */
#define GAP_SPACE        0.10
#define LINE_BREAK       0.50
#define LINE_TOL         0.35

/* Uniformly letter-spaced text -- a whole line on which every single character
 * stands apart, which is what a Tc wider than GAP_SPACE does -- is not a line
 * of one-letter words.  poppler's TextLine::coalesce spots it by every pair on
 * the line having broken, and then measures a word space against the line's
 * own narrowest gap instead of against the font size, so the even letter gaps
 * stay inside their words and only a wider gap still reads as a space. */
#define LETTER_SPACE     1.50

/* ------------------------------------------------------------- matrices */

typedef struct { double a, b, c, d, e, f; } mat;

static mat mat_id(void)
{
    mat m = { 1, 0, 0, 1, 0, 0 };
    return m;
}

/* Row-vector convention: [x y 1] * M.  mat_mul(x, y) applies x, then y. */
static mat mat_mul(mat x, mat y)
{
    mat r;
    r.a = x.a * y.a + x.b * y.c;
    r.b = x.a * y.b + x.b * y.d;
    r.c = x.c * y.a + x.d * y.c;
    r.d = x.c * y.b + x.d * y.d;
    r.e = x.e * y.a + x.f * y.c + y.e;
    r.f = x.e * y.b + x.f * y.d + y.f;
    return r;
}

/* ---------------------------------------------------------------- state */

typedef struct {
    mat       ctm;
    pdf_font *font;
    double    fs;     /* Tfs */
    double    tc, tw; /* character and word spacing */
    double    th;     /* horizontal scaling, Tz/100 */
    double    tl;     /* leading */
    double    ts;     /* rise */
    int       tr;     /* render mode */
} gstate;

typedef struct {
    pdf_doc       *doc;
    pdf_glyphs    *out;
    pdf_fontcache *fc;

    gstate  gs[MAX_GSTACK];
    int     ngs;

    mat     tm, tlm;
    int     in_text;

    pdf_obj *xstack[MAX_FORM_DEPTH];
    int      nx;

    double   pw, ph;    /* page box in device units, after /Rotate */

    unsigned char *grot;   /* reading rotation of out->g[i] */
    size_t         grotcap;

    int      overflow;
} tctx;

/* Resource dictionaries nest: a form XObject's own /Resources shadows the
 * enclosing one but does not replace it, so a name it does not define is
 * still looked up outwards (poppler's GfxResources chain). */
typedef struct resnode {
    pdf_obj              *res;
    const struct resnode *next;
} resnode;

static pdf_obj *res_get(pdf_doc *doc, const resnode *rn, const char *cat,
                        const char *name)
{
    for (; rn; rn = rn->next) {
        pdf_obj *d = pdf_dict_get(doc, rn->res, cat);
        if (pdf_is_dict(d)) {
            pdf_obj *v = pdf_dict_get(doc, d, name);
            if (!pdf_is_null(v)) return v;
        }
    }
    return pdf_null_obj();
}

/* ------------------------------------------------------------ operands */

enum { OP_NUM = 0, OP_STR, OP_NAME, OP_ARR };

typedef struct {
    int         kind;
    double      num;
    const char *s;
    size_t      slen;
} oper;

typedef struct {
    int         is_str;
    double      num;
    const char *s;
    size_t      slen;
} tjelem;

static double op_num(const oper *st, int n, int i)
{
    /* i counts from the end: 0 is the last operand. */
    int k = n - 1 - i;
    if (k < 0 || st[k].kind != OP_NUM) return 0.0;
    return st[k].num;
}

/* --------------------------------------------------------- glyph output */

static int glyphs_push(pdf_glyphs *g, const pdf_glyph *gl)
{
    if (g->n + 1 > g->cap) {
        size_t nc = g->cap ? g->cap * 2 : 256;
        pdf_glyph *p;
        if (nc > MAX_GLYPHS) return -1;
        p = realloc(g->g, nc * sizeof *p);
        if (!p) return -1;
        g->g = p;
        g->cap = nc;
    }
    g->g[g->n++] = *gl;
    return 0;
}

/* Classify a device-space writing direction into poppler's four text
 * rotations: 0 runs right, 1 down, 2 left, 3 up (device y grows downwards). */
static int dir_rot(double dx, double dy)
{
    if (fabs(dx) >= fabs(dy)) return dx < 0 ? 2 : 0;
    return dy > 0 ? 1 : 3;
}

/* The rotation of glyph i, kept beside the glyph because pdf_glyph has no room
 * for it and pdf.h is not ours to change. */
static int rot_push(tctx *t, int rot)
{
    size_t need = t->out->n;            /* the glyph has just been appended */

    if (need > t->grotcap) {
        size_t nc = t->grotcap ? t->grotcap * 2 : 256;
        unsigned char *p;
        while (nc < need) nc *= 2;
        p = realloc(t->grot, nc);
        if (!p) return -1;
        t->grot = p;
        t->grotcap = nc;
    }
    t->grot[need - 1] = (unsigned char)rot;
    return 0;
}

/* Emit one glyph and advance the text matrix (9.4.4). */
static void show_code(tctx *t, gstate *g, unsigned int code, int nbytes)
{
    pdf_font *f = g->font;
    mat m, param, trm;
    pdf_glyph gl;
    double w0, tx, ty, wordsp, ax, ay, w1, h1, dirx, diry;
    int vert, rot;

    m = mat_mul(t->tm, g->ctm);

    param.a = g->fs * g->th; param.b = 0;
    param.c = 0;             param.d = g->fs;
    param.e = 0;             param.f = g->ts;
    trm = mat_mul(param, m);

    w0 = pdf_font_width(f, code);
    vert = pdf_font_vertical(f);

    /* Word spacing applies to a single-byte code 32 and to nothing else -- in
     * particular never to a two-byte CID whose value happens to be 32 (9.3.3). */
    wordsp = (nbytes == 1 && code == 32) ? g->tw : 0.0;

    if (vert) {
        /* Vertical writing: the default position vector and w1 of 9.7.4.3 are
         * approximated by the /DW2 default of [880 -1000]. */
        ty = (-w0 * g->fs) + g->tc + wordsp;
        tx = 0.0;
        ax = 0.0;
        ay = -w0 * g->fs;
        dirx = -trm.c;                  /* the advance runs down the text y axis */
        diry = -trm.d;
    } else {
        tx = (w0 * g->fs + g->tc + wordsp) * g->th;
        ty = 0.0;
        ax = w0 * g->fs * g->th;
        ay = 0.0;
        dirx = trm.a;
        diry = trm.b;
    }

    /* poppler's TextPage::addChar throws away a glyph whose origin falls off
     * the page, or whose advance is wider than the page; the test uses the
     * glyph advance with Tc/Tw taken back out.  The text matrix still moves
     * on, because that happens in Gfx, before the output device sees it. */
    w1 = ax * m.a + ay * m.c;
    h1 = ax * m.b + ay * m.d;

    if (!isnan(trm.e) && !isnan(trm.f) && !isnan(w1) && !isnan(h1) &&
        trm.e + w1 >= 0 && trm.e <= t->pw &&
        trm.f + h1 >= 0 && trm.f <= t->ph &&
        w1 <= t->pw && h1 <= t->ph) {
        memset(&gl, 0, sizeof gl);
        gl.nucs = pdf_font_unicode(f, code, gl.ucs, 8);
        if (gl.nucs > 8) gl.nucs = 8;

        /* Store the glyph in reading-order axes rather than raw device axes:
         * x grows along the writing direction and y across it, whatever the
         * page /Rotate or the text matrix did.  Line grouping, the spacing
         * test and the left-to-right sort then work unchanged for all four
         * rotations instead of only for upright text. */
        rot = dir_rot(dirx, diry);
        switch (rot) {
        case 1:  gl.x =  trm.f; gl.y = -trm.e; break;
        case 2:  gl.x = -trm.e; gl.y = -trm.f; break;
        case 3:  gl.x = -trm.f; gl.y =  trm.e; break;
        default: gl.x =  trm.e; gl.y =  trm.f; break;
        }
        gl.h = g->fs * hypot(m.c, m.d);
        /* The advance stored with the glyph is the glyph's own, with Tc and Tw
         * taken back out again -- poppler's TextPage::addChar subtracts them
         * before transforming the delta.  They belong to the space between
         * this glyph and the next, not to the glyph itself, so leaving them in
         * would hide exactly the gap they open up: a word break driven by a
         * wide Tc or Tw would measure as no gap at all and the two words would
         * be run together. */
        gl.w = vert ? fabs(ay) * hypot(m.c, m.d) : ax * hypot(m.a, m.b);

        /* Render modes 3 (invisible) and 7 (clip) still contribute text: both
         * poppler and pdfium extract them. */
        if (glyphs_push(t->out, &gl) != 0 || rot_push(t, rot) != 0)
            t->overflow = 1;
    }

    {
        mat tr = mat_id();
        tr.e = tx;
        tr.f = ty;
        t->tm = mat_mul(tr, t->tm);
    }
}

static void show_string(tctx *t, gstate *g, const unsigned char *s, size_t n)
{
    size_t i = 0;
    if (!g->font || !s) return;
    while (i < n && !t->overflow) {
        unsigned int code = 0;
        int nb = 1;
        if (pdf_font_next_code(g->font, s + i, n - i, &code, &nb) != 0) break;
        if (nb <= 0) nb = 1;
        if ((size_t)nb > n - i) nb = (int)(n - i);
        show_code(t, g, code, nb);
        i += (size_t)nb;
    }
}

static void text_newline(tctx *t, const gstate *g)
{
    mat tr = mat_id();
    tr.f = -g->tl;
    t->tlm = mat_mul(tr, t->tlm);
    t->tm = t->tlm;
}

/* ----------------------------------------------------------- inline images */

/* BI <dict> ID <binary> EI (8.9.7): the binary payload must never reach the
 * lexer, so the whole of it has to be stepped over in one go.
 *
 * Sample data is free to spell "EI" itself, whitespace-delimited and followed
 * by something operator-shaped, so scanning for the first such pair is wrong
 * in both directions at once: it stops inside an image that happens to contain
 * one, and it runs past the real one when the samples end differently than
 * expected.  The payload length is therefore computed whenever the image
 * dictionary allows it -- from /L, from the end-of-data marker of the outermost
 * filter, or from /W /H /BPC /CS -- and the scan is kept only for the images
 * where nothing can be computed. */

static int iim_ws(unsigned char c)
{
    return c == ' ' || c == '\n' || c == '\r' ||
           c == '\t' || c == '\f' || c == '\0';
}

static int iim_delim(unsigned char c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
           c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

/* Colour components of an inline image's colour space, by the full names and
 * by the abbreviations of table 93.  0 means "cannot tell", which covers a
 * space named in the page's /ColorSpace resource and anything unrecognised. */
static int iim_ncomp(const char *cs)
{
    if (!cs) return 0;
    if (!strcmp(cs, "G") || !strcmp(cs, "DeviceGray") || !strcmp(cs, "CalGray"))
        return 1;
    if (!strcmp(cs, "RGB") || !strcmp(cs, "DeviceRGB") ||
        !strcmp(cs, "CalRGB") || !strcmp(cs, "Lab"))
        return 3;
    if (!strcmp(cs, "CMYK") || !strcmp(cs, "DeviceCMYK"))
        return 4;
    if (!strcmp(cs, "I") || !strcmp(cs, "Indexed"))
        return 1;                       /* one index per sample */
    return 0;
}

/* The two filters whose encoded form carries its own end-of-data marker: '>'
 * for ASCIIHexDecode (7.4.2) and '~>' for ASCII85Decode (7.4.3).  Returns the
 * offset just past the marker, or 0 for a filter that has none. */
static size_t iim_filter_eod(const char *f, const unsigned char *b,
                             size_t from, size_t len)
{
    size_t i;

    if (!f) return 0;
    if (!strcmp(f, "AHx") || !strcmp(f, "ASCIIHexDecode")) {
        for (i = from; i < len; i++)
            if (b[i] == '>') return i + 1;
        return 0;
    }
    if (!strcmp(f, "A85") || !strcmp(f, "ASCII85Decode")) {
        for (i = from; i + 1 < len; i++)
            if (b[i] == '~' && b[i + 1] == '>') return i + 2;
        return 0;
    }
    return 0;
}

/* Is the byte run after a candidate EI the start of a content stream again?
 * A genuine EI is a token, so a delimiter or whitespace has to follow it, and
 * what comes next is operators: text.  Sample data that happens to spell EI
 * almost never continues that way, which is what tells the two apart. */
static int iim_tail_ok(const unsigned char *b, size_t p, size_t len)
{
    size_t i;

    if (p >= len) return 1;                     /* EI at the end of the stream */
    if (!iim_ws(b[p]) && !iim_delim(b[p])) return 0;
    for (i = p; i < len && i < p + 16; i++)
        if (!iim_ws(b[i]) && (b[i] < 0x20 || b[i] > 0x7e)) return 0;
    return 1;
}

static void skip_inline_image(pdf_doc *doc, pdf_lexer *lx)
{
    pdf_token tok;
    const char *key = NULL, *cs = NULL, *filt = NULL;
    long long len = -1, w = -1, h = -1, bpc = -1;
    int mask = 0, filtered = 0, have_end = 0;
    size_t data, end = 0, p, q;

    /* The image dictionary runs from here to the ID, without the << >> a real
     * dictionary would carry, so walk it as bare key/value pairs. */
    for (;;) {
        if (pdf_lex_next(doc, lx, &tok) != 0 || tok.type == PDF_TOK_EOF) goto eof;
        if (tok.type == PDF_TOK_KEYWORD && tok.s && !strcmp(tok.s, "ID")) break;
        if (tok.type != PDF_TOK_NAME || !tok.s) continue;   /* resync on a key */
        key = tok.s;                        /* arena memory, stays valid */

        if (pdf_lex_next(doc, lx, &tok) != 0 || tok.type == PDF_TOK_EOF) goto eof;
        if (tok.type == PDF_TOK_KEYWORD && tok.s && !strcmp(tok.s, "ID")) break;

        if (tok.type == PDF_TOK_ARRAY_OPEN) {
            /* /Filter and /ColorSpace may be arrays.  The first filter is the
             * one the bytes in the stream are actually encoded with, and the
             * first name of a colour space array is its family. */
            const char *first = NULL;
            int depth = 1;

            while (depth > 0) {
                if (pdf_lex_next(doc, lx, &tok) != 0 || tok.type == PDF_TOK_EOF)
                    goto eof;
                if (tok.type == PDF_TOK_ARRAY_OPEN) depth++;
                else if (tok.type == PDF_TOK_ARRAY_CLOSE) depth--;
                else if (tok.type == PDF_TOK_NAME && !first) first = tok.s;
            }
            if (!strcmp(key, "F") || !strcmp(key, "Filter")) {
                filtered = (first != NULL);     /* an empty array filters nothing */
                filt = first;
            } else if (!strcmp(key, "CS") || !strcmp(key, "ColorSpace")) {
                cs = first;
            }
            continue;
        }

        if (tok.type == PDF_TOK_DICT_OPEN) {            /* /DP, /DecodeParms */
            int depth = 1;

            while (depth > 0) {
                if (pdf_lex_next(doc, lx, &tok) != 0 || tok.type == PDF_TOK_EOF)
                    goto eof;
                if (tok.type == PDF_TOK_DICT_OPEN) depth++;
                else if (tok.type == PDF_TOK_DICT_CLOSE) depth--;
            }
            continue;
        }

        if (tok.type == PDF_TOK_INT) {
            if (!strcmp(key, "L") || !strcmp(key, "Length")) len = tok.i;
            else if (!strcmp(key, "W") || !strcmp(key, "Width")) w = tok.i;
            else if (!strcmp(key, "H") || !strcmp(key, "Height")) h = tok.i;
            else if (!strcmp(key, "BPC") || !strcmp(key, "BitsPerComponent"))
                bpc = tok.i;
        } else if (tok.type == PDF_TOK_NAME) {
            if (!strcmp(key, "CS") || !strcmp(key, "ColorSpace")) cs = tok.s;
            else if (!strcmp(key, "F") || !strcmp(key, "Filter")) {
                filtered = 1;
                filt = tok.s;
            }
        } else if (tok.type == PDF_TOK_KEYWORD && tok.s) {
            if ((!strcmp(key, "IM") || !strcmp(key, "ImageMask")) &&
                !strcmp(tok.s, "true"))
                mask = 1;
        }
    }

    /* Exactly one whitespace byte separates the ID from the first sample. */
    p = lx->pos;
    if (p < lx->len && iim_ws(lx->buf[p])) p++;
    data = p;

    if (len >= 0) {
        if ((size_t)len <= lx->len - data) {
            end = data + (size_t)len;
            have_end = 1;
        }
    } else if (filtered) {
        end = iim_filter_eod(filt, lx->buf, data, lx->len);
        have_end = (end > 0);
    } else {
        /* An image mask is one bit per sample whatever else it says (8.9.6.2). */
        int nc = mask ? 1 : iim_ncomp(cs);
        long long bits = mask ? 1 : (bpc > 0 ? bpc : 8);

        if (w > 0 && w <= (1 << 24) && h > 0 && h <= (1 << 24) &&
            nc > 0 && bits > 0 && bits <= 32) {
            unsigned long long row = ((unsigned long long)w *
                                      (unsigned long long)nc *
                                      (unsigned long long)bits + 7) / 8;
            unsigned long long tot = row * (unsigned long long)h;

            if (tot <= (unsigned long long)(lx->len - data)) {
                end = data + (size_t)tot;
                have_end = 1;
            }
        }
    }

    /* A computed length is believed only when the EI it predicts is really
     * there; if it is not, the dictionary lied or was not understood, and the
     * scan below is still the best that can be done. */
    if (have_end) {
        q = end;
        while (q < lx->len && iim_ws(lx->buf[q])) q++;
        if (q >= lx->len) goto eof;             /* the samples ran to the end */
        if (q + 1 < lx->len && lx->buf[q] == 'E' && lx->buf[q + 1] == 'I' &&
            iim_tail_ok(lx->buf, q + 2, lx->len)) {
            lx->pos = q + 2;
            return;
        }
    }

    for (p = data; p + 1 < lx->len; p++) {
        if (lx->buf[p] != 'E' || lx->buf[p + 1] != 'I') continue;
        if (p == 0 || !iim_ws(lx->buf[p - 1])) continue;
        if (!iim_tail_ok(lx->buf, p + 2, lx->len)) continue;
        lx->pos = p + 2;
        return;
    }

eof:
    lx->pos = lx->len;
}

/* ----------------------------------------------------------- the machine */

static void run_content(tctx *t, const unsigned char *buf, size_t len,
                        const resnode *res, int depth);

static void do_xobject(tctx *t, const char *name, const resnode *res, int depth)
{
    pdf_doc *doc = t->doc;
    pdf_obj *xo, *sub, *mtx, *xres;
    pdf_buf data;
    resnode rn;
    gstate *g;
    int i;

    if (depth + 1 > MAX_FORM_DEPTH || t->ngs + 1 >= MAX_GSTACK) return;

    xo = res_get(doc, res, "XObject", name);
    if (!pdf_is_stream(xo)) return;
    sub = pdf_dict_get(doc, xo, "Subtype");
    if (strcmp(pdf_to_name(sub), "Form") != 0) return;

    for (i = 0; i < t->nx; i++)
        if (t->xstack[i] == xo) return;             /* cycle */
    if (t->nx >= MAX_FORM_DEPTH) return;
    t->xstack[t->nx++] = xo;

    /* The form runs with its own graphics state and /Matrix (8.10.2). */
    t->gs[t->ngs + 1] = t->gs[t->ngs];
    t->ngs++;
    g = &t->gs[t->ngs];

    mtx = pdf_dict_get(doc, xo, "Matrix");
    if (pdf_is_array(mtx) && pdf_array_len(mtx) >= 6) {
        mat fm;
        double v[6];
        for (i = 0; i < 6; i++) {
            pdf_obj *e = pdf_array_get(doc, mtx, (size_t)i);
            v[i] = pdf_is_num(e) ? pdf_to_real(e) : (i == 0 || i == 3 ? 1.0 : 0.0);
        }
        fm.a = v[0]; fm.b = v[1]; fm.c = v[2];
        fm.d = v[3]; fm.e = v[4]; fm.f = v[5];
        g->ctm = mat_mul(fm, g->ctm);
    }

    /* The form's own /Resources are pushed in front of the enclosing ones
     * rather than replacing them (8.10.2 plus poppler's Gfx::pushResources),
     * so a font or XObject it leaves out is still found further out. */
    xres = pdf_dict_get(doc, xo, "Resources");
    rn.res = xres;
    rn.next = res;

    pdf_buf_init(&data);
    if (pdf_stream_data(doc, xo, &data, 1) == 0 && data.n > 0) {
        mat save_tm = t->tm, save_tlm = t->tlm;
        int save_in = t->in_text;
        run_content(t, data.p, data.n, pdf_is_dict(xres) ? &rn : res, depth + 1);
        t->tm = save_tm;
        t->tlm = save_tlm;
        t->in_text = save_in;
    }
    pdf_buf_free(&data);

    t->ngs--;
    t->nx--;
}

static void run_content(tctx *t, const unsigned char *buf, size_t len,
                        const resnode *res, int depth)
{
    pdf_doc *doc = t->doc;
    pdf_lexer lx;
    pdf_token tok;
    oper st[MAX_OPS];
    int nst = 0;
    tjelem *tj = NULL;
    int ntj = 0;

    if (depth > MAX_FORM_DEPTH) return;
    tj = calloc(MAX_TJ, sizeof *tj);
    if (!tj) return;

    pdf_lex_init(&lx, buf, len, 0);
    while (!t->overflow && pdf_lex_next(doc, &lx, &tok) == 0) {
        gstate *g = &t->gs[t->ngs];

        if (tok.type == PDF_TOK_EOF) break;
        if (tok.type == PDF_TOK_ERROR) break;

        switch (tok.type) {
        case PDF_TOK_INT:
        case PDF_TOK_REAL:
            if (nst < MAX_OPS) {
                st[nst].kind = OP_NUM;
                st[nst].num = (tok.type == PDF_TOK_INT) ? (double)tok.i : tok.r;
                nst++;
            }
            continue;
        case PDF_TOK_STRING:
            if (nst < MAX_OPS) {
                st[nst].kind = OP_STR;
                st[nst].s = tok.s;
                st[nst].slen = tok.slen;
                nst++;
            }
            continue;
        case PDF_TOK_NAME:
            if (nst < MAX_OPS) {
                st[nst].kind = OP_NAME;
                st[nst].s = tok.s;
                st[nst].slen = tok.slen;
                nst++;
            }
            continue;
        case PDF_TOK_ARRAY_OPEN: {
            /* Only TJ takes an array; collect its numbers and strings. */
            ntj = 0;
            for (;;) {
                if (pdf_lex_next(doc, &lx, &tok) != 0 || tok.type == PDF_TOK_EOF)
                    break;
                if (tok.type == PDF_TOK_ARRAY_CLOSE) break;
                if (ntj >= MAX_TJ) continue;
                if (tok.type == PDF_TOK_INT || tok.type == PDF_TOK_REAL) {
                    tj[ntj].is_str = 0;
                    tj[ntj].num = (tok.type == PDF_TOK_INT) ? (double)tok.i : tok.r;
                    ntj++;
                } else if (tok.type == PDF_TOK_STRING) {
                    tj[ntj].is_str = 1;
                    tj[ntj].s = tok.s;
                    tj[ntj].slen = tok.slen;
                    ntj++;
                }
            }
            if (nst < MAX_OPS) { st[nst].kind = OP_ARR; nst++; }
            continue;
        }
        case PDF_TOK_DICT_OPEN: {
            /* BDC/DP operands: skip the dictionary wholesale. */
            int level = 1;
            while (level > 0) {
                if (pdf_lex_next(doc, &lx, &tok) != 0 || tok.type == PDF_TOK_EOF)
                    break;
                if (tok.type == PDF_TOK_DICT_OPEN) level++;
                else if (tok.type == PDF_TOK_DICT_CLOSE) level--;
            }
            continue;
        }
        case PDF_TOK_KEYWORD:
            break;                       /* handled below */
        default:
            continue;
        }

        if (!tok.s) { nst = 0; continue; }

        /* ---- graphics state ---- */
        if (!strcmp(tok.s, "q")) {
            if (t->ngs + 1 < MAX_GSTACK) {
                t->gs[t->ngs + 1] = t->gs[t->ngs];
                t->ngs++;
            }
        } else if (!strcmp(tok.s, "Q")) {
            if (t->ngs > 0) t->ngs--;
        } else if (!strcmp(tok.s, "cm")) {
            if (nst >= 6) {
                mat m;
                m.a = op_num(st, nst, 5); m.b = op_num(st, nst, 4);
                m.c = op_num(st, nst, 3); m.d = op_num(st, nst, 2);
                m.e = op_num(st, nst, 1); m.f = op_num(st, nst, 0);
                g->ctm = mat_mul(m, g->ctm);
            }
        }
        /* ---- text objects ---- */
        else if (!strcmp(tok.s, "BT")) {
            t->tm = t->tlm = mat_id();
            t->in_text = 1;
        } else if (!strcmp(tok.s, "ET")) {
            t->in_text = 0;
        }
        /* ---- text state ---- */
        else if (!strcmp(tok.s, "Tc")) {
            if (nst >= 1) g->tc = op_num(st, nst, 0);
        } else if (!strcmp(tok.s, "Tw")) {
            if (nst >= 1) g->tw = op_num(st, nst, 0);
        } else if (!strcmp(tok.s, "Tz")) {
            if (nst >= 1) g->th = op_num(st, nst, 0) / 100.0;
        } else if (!strcmp(tok.s, "TL")) {
            if (nst >= 1) g->tl = op_num(st, nst, 0);
        } else if (!strcmp(tok.s, "Ts")) {
            if (nst >= 1) g->ts = op_num(st, nst, 0);
        } else if (!strcmp(tok.s, "Tr")) {
            if (nst >= 1) g->tr = (int)op_num(st, nst, 0);
        } else if (!strcmp(tok.s, "Tf")) {
            if (nst >= 2 && st[nst - 2].kind == OP_NAME) {
                pdf_obj *fd = res_get(doc, res, "Font", st[nst - 2].s);
                g->font = pdf_is_dict(fd) ? pdf_fontcache_get(t->fc, fd) : NULL;
                g->fs = op_num(st, nst, 0);
            }
        }
        /* ---- text positioning ---- */
        else if (!strcmp(tok.s, "Td")) {
            if (nst >= 2) {
                mat tr = mat_id();
                tr.e = op_num(st, nst, 1);
                tr.f = op_num(st, nst, 0);
                t->tlm = mat_mul(tr, t->tlm);
                t->tm = t->tlm;
            }
        } else if (!strcmp(tok.s, "TD")) {
            if (nst >= 2) {
                mat tr = mat_id();
                tr.e = op_num(st, nst, 1);
                tr.f = op_num(st, nst, 0);
                g->tl = -tr.f;
                t->tlm = mat_mul(tr, t->tlm);
                t->tm = t->tlm;
            }
        } else if (!strcmp(tok.s, "Tm")) {
            if (nst >= 6) {
                mat m;
                m.a = op_num(st, nst, 5); m.b = op_num(st, nst, 4);
                m.c = op_num(st, nst, 3); m.d = op_num(st, nst, 2);
                m.e = op_num(st, nst, 1); m.f = op_num(st, nst, 0);
                t->tlm = m;
                t->tm = m;
            }
        } else if (!strcmp(tok.s, "T*")) {
            text_newline(t, g);
        }
        /* ---- text showing ---- */
        else if (!strcmp(tok.s, "Tj")) {
            if (nst >= 1 && st[nst - 1].kind == OP_STR)
                show_string(t, g, (const unsigned char *)st[nst - 1].s,
                            st[nst - 1].slen);
        } else if (!strcmp(tok.s, "'")) {
            text_newline(t, g);
            if (nst >= 1 && st[nst - 1].kind == OP_STR)
                show_string(t, g, (const unsigned char *)st[nst - 1].s,
                            st[nst - 1].slen);
        } else if (!strcmp(tok.s, "\"")) {
            if (nst >= 3 && st[nst - 1].kind == OP_STR) {
                g->tw = op_num(st, nst, 2);
                g->tc = op_num(st, nst, 1);
                text_newline(t, g);
                show_string(t, g, (const unsigned char *)st[nst - 1].s,
                            st[nst - 1].slen);
            }
        } else if (!strcmp(tok.s, "TJ")) {
            int i;
            for (i = 0; i < ntj && !t->overflow; i++) {
                if (tj[i].is_str) {
                    show_string(t, g, (const unsigned char *)tj[i].s, tj[i].slen);
                } else {
                    /* Adjust by -n/1000 of the font size, scaled horizontally. */
                    mat tr = mat_id();
                    double d = -tj[i].num / 1000.0 * g->fs * g->th;
                    if (pdf_font_vertical(g->font)) tr.f = -(-tj[i].num / 1000.0 * g->fs);
                    else                            tr.e = d;
                    t->tm = mat_mul(tr, t->tm);
                }
            }
            ntj = 0;
        }
        /* ---- XObjects and inline images ---- */
        else if (!strcmp(tok.s, "Do")) {
            if (nst >= 1 && st[nst - 1].kind == OP_NAME)
                do_xobject(t, st[nst - 1].s, res, depth);
        } else if (!strcmp(tok.s, "BI")) {
            skip_inline_image(doc, &lx);
        }

        nst = 0;
    }
    free(tj);
}

/* ------------------------------------------------- annotation appearances */

/* pdftotext draws annotation appearance streams along with the page content,
 * so their text shows up in the output; this reproduces that. */

#define ANNOT_HIDDEN  2
#define ANNOT_NOVIEW  32

static pdf_obj *annot_ap_stream(pdf_doc *doc, pdf_obj *annot)
{
    pdf_obj *ap = pdf_dict_get(doc, annot, "AP");
    pdf_obj *n;

    if (!pdf_is_dict(ap)) return NULL;
    n = pdf_dict_get(doc, ap, "N");
    if (pdf_is_stream(n)) return n;
    if (pdf_is_dict(n)) {
        pdf_obj *as = pdf_dict_get(doc, annot, "AS");
        if (pdf_is_name(as, NULL)) {
            pdf_obj *sel = pdf_dict_get(doc, n, pdf_to_name(as));
            if (pdf_is_stream(sel)) return sel;
        } else if (pdf_dict_len(n) == 1) {
            pdf_obj *sel = pdf_resolve(doc, pdf_dict_val_raw(n, 0));
            if (pdf_is_stream(sel)) return sel;
        }
    }
    return NULL;
}

static int rect_of(pdf_doc *doc, pdf_obj *arr, double *v)
{
    int i;
    if (!pdf_is_array(arr) || pdf_array_len(arr) < 4) return -1;
    for (i = 0; i < 4; i++) {
        pdf_obj *e = pdf_array_get(doc, arr, (size_t)i);
        if (!pdf_is_num(e)) return -1;
        v[i] = pdf_to_real(e);
    }
    if (v[0] > v[2]) { double t = v[0]; v[0] = v[2]; v[2] = t; }
    if (v[1] > v[3]) { double t = v[1]; v[1] = v[3]; v[3] = t; }
    return 0;
}

/* Algorithm 8.1 of ISO 32000-1 12.5.5: map the /Matrix-transformed /BBox onto
 * the annotation's /Rect. */
static void run_annot(tctx *t, pdf_obj *annot, mat base, const resnode *pageres)
{
    pdf_doc *doc = t->doc;
    pdf_obj *ap, *res;
    pdf_buf data;
    resnode rn;
    double rect[4], bbox[4] = { 0, 0, 1, 1 };
    double xs[4], ys[4], tx0, ty0, tx1, ty1, sx = 1.0, sy = 1.0;
    mat fm = mat_id(), aa = mat_id();
    long long flags;
    int i;

    if (!pdf_is_dict(annot)) return;
    if (!strcmp(pdf_to_name(pdf_dict_get(doc, annot, "Subtype")), "Popup")) return;

    flags = pdf_to_int(pdf_dict_get(doc, annot, "F"));
    if (flags & (ANNOT_HIDDEN | ANNOT_NOVIEW)) return;

    ap = annot_ap_stream(doc, annot);
    if (!ap) return;
    if (rect_of(doc, pdf_dict_get(doc, annot, "Rect"), rect) != 0) return;
    if (rect_of(doc, pdf_dict_get(doc, ap, "BBox"), bbox) != 0) {
        bbox[0] = 0; bbox[1] = 0; bbox[2] = 1; bbox[3] = 1;
    }

    {
        pdf_obj *m = pdf_dict_get(doc, ap, "Matrix");
        if (pdf_is_array(m) && pdf_array_len(m) >= 6) {
            double v[6];
            for (i = 0; i < 6; i++) {
                pdf_obj *e = pdf_array_get(doc, m, (size_t)i);
                v[i] = pdf_is_num(e) ? pdf_to_real(e)
                                     : ((i == 0 || i == 3) ? 1.0 : 0.0);
            }
            fm.a = v[0]; fm.b = v[1]; fm.c = v[2];
            fm.d = v[3]; fm.e = v[4]; fm.f = v[5];
        }
    }

    /* Transformed appearance box. */
    xs[0] = fm.a * bbox[0] + fm.c * bbox[1] + fm.e;
    ys[0] = fm.b * bbox[0] + fm.d * bbox[1] + fm.f;
    xs[1] = fm.a * bbox[2] + fm.c * bbox[1] + fm.e;
    ys[1] = fm.b * bbox[2] + fm.d * bbox[1] + fm.f;
    xs[2] = fm.a * bbox[2] + fm.c * bbox[3] + fm.e;
    ys[2] = fm.b * bbox[2] + fm.d * bbox[3] + fm.f;
    xs[3] = fm.a * bbox[0] + fm.c * bbox[3] + fm.e;
    ys[3] = fm.b * bbox[0] + fm.d * bbox[3] + fm.f;
    tx0 = tx1 = xs[0];
    ty0 = ty1 = ys[0];
    for (i = 1; i < 4; i++) {
        if (xs[i] < tx0) tx0 = xs[i];
        if (xs[i] > tx1) tx1 = xs[i];
        if (ys[i] < ty0) ty0 = ys[i];
        if (ys[i] > ty1) ty1 = ys[i];
    }
    if (tx1 - tx0 > 1e-9) sx = (rect[2] - rect[0]) / (tx1 - tx0);
    if (ty1 - ty0 > 1e-9) sy = (rect[3] - rect[1]) / (ty1 - ty0);

    aa.a = sx; aa.d = sy;
    aa.e = rect[0] - tx0 * sx;
    aa.f = rect[1] - ty0 * sy;

    t->ngs = 0;
    memset(&t->gs[0], 0, sizeof t->gs[0]);
    t->gs[0].th = 1.0;
    t->gs[0].ctm = mat_mul(fm, mat_mul(aa, base));
    t->tm = t->tlm = mat_id();

    res = pdf_dict_get(doc, ap, "Resources");
    rn.res = res;
    rn.next = pageres;
    pdf_buf_init(&data);
    if (pdf_stream_data(doc, ap, &data, 1) == 0 && data.n > 0)
        run_content(t, data.p, data.n, pdf_is_dict(res) ? &rn : pageres, 1);
    pdf_buf_free(&data);
}

/* ------------------------------------------------------- page transform */

/* Matches poppler's GfxState constructor at 72dpi with upsideDown set, so the
 * result is top-left based after /Rotate. */
static mat page_base(pdf_doc *doc, pdf_obj *page, double *pw, double *ph)
{
    pdf_rect b;
    mat m = mat_id();
    double x1, y1, x2, y2, tmp;
    int rot;

    /* pdftotext asks for the media box (displayPages(..., useMediaBox=true)),
     * so that -- not the crop box -- is what the device space is built on and
     * what getPageWidth()/getPageHeight() report. */
    if (pdf_page_box(doc, page, "MediaBox", &b) != 0 &&
        pdf_page_box(doc, page, "CropBox", &b) != 0) {
        b.x0 = 0; b.y0 = 0; b.x1 = 612; b.y1 = 792;
    }
    x1 = b.x0; y1 = b.y0; x2 = b.x1; y2 = b.y1;
    if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
    if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }

    rot = pdf_page_rotate(doc, page);
    if (pw && ph) {
        *pw = (rot == 90 || rot == 270) ? y2 - y1 : x2 - x1;
        *ph = (rot == 90 || rot == 270) ? x2 - x1 : y2 - y1;
    }
    if (rot == 90) {
        m.a = 0;  m.b = 1;  m.c = 1;  m.d = 0;  m.e = -y1;  m.f = -x1;
    } else if (rot == 180) {
        m.a = -1; m.b = 0;  m.c = 0;  m.d = 1;  m.e = x2;   m.f = -y1;
    } else if (rot == 270) {
        m.a = 0;  m.b = -1; m.c = -1; m.d = 0;  m.e = y2;   m.f = x2;
    } else {
        m.a = 1;  m.b = 0;  m.c = 0;  m.d = -1; m.e = -x1;  m.f = y2;
    }
    return m;
}

/* ------------------------------------------------- rotation block ordering
 *
 * A page can carry text at several rotations at once -- a rotated stamp over
 * upright body text, say.  Each run has to be read along its own baseline, so
 * the glyphs of one rotation must never interleave with another's; but the
 * runs themselves are read in the order the page's dominant rotation would
 * put them, which is what poppler does with its primary-rotation block sort.
 *
 * Glyph coordinates are already in each glyph's own reading axes, so the two
 * requirements are met at once by shifting each rotation class along the
 * cross axis by more than the whole page: the classes then sort into the
 * chosen order and can never share a line. */

/* Undo the per-glyph axis mapping of show_code() to get device coordinates. */
static void device_of(const pdf_glyph *g, int rot, double *e, double *f)
{
    switch (rot) {
    case 1:  *e = -g->y; *f =  g->x; break;
    case 2:  *e = -g->x; *f = -g->y; break;
    case 3:  *e =  g->y; *f = -g->x; break;
    default: *e =  g->x; *f =  g->y; break;
    }
}

/* Where a device point falls on the cross axis of rotation `rot`. */
static double cross_in(int rot, double e, double f)
{
    switch (rot) {
    case 1:  return -e;
    case 2:  return -f;
    case 3:  return e;
    default: return f;
    }
}

static void order_rot_classes(tctx *t, pdf_glyphs *out)
{
    size_t count[4], i;
    double key[4], span_lo, span_hi, stride, maxh = 0;
    int rank[4], rot, k, present = 0, primary = 0;

    if (!t->grot || out->n < 2) return;

    for (k = 0; k < 4; k++) { count[k] = 0; key[k] = 0; rank[k] = 0; }
    for (i = 0; i < out->n; i++) count[t->grot[i] & 3]++;
    for (k = 0; k < 4; k++) {
        if (!count[k]) continue;
        present++;
        if (count[k] > count[primary]) primary = k;
    }
    if (present < 2) return;

    /* Each class is placed by how far up the dominant rotation's page its
     * topmost glyph sits. */
    for (k = 0; k < 4; k++) rank[k] = -1;
    for (i = 0; i < out->n; i++) {
        double e, f, c;

        rot = t->grot[i] & 3;
        device_of(&out->g[i], rot, &e, &f);
        c = cross_in(primary, e, f);
        if (rank[rot] < 0 || c < key[rot]) key[rot] = c;
        rank[rot] = 0;
        if (out->g[i].h > maxh) maxh = out->g[i].h;
    }

    /* rank[] becomes the class's position in reading order. */
    for (k = 0; k < 4; k++) {
        int r = 0, j;
        if (rank[k] < 0) continue;
        for (j = 0; j < 4; j++) {
            if (j == k || rank[j] < 0) continue;
            if (key[j] < key[k] || (key[j] == key[k] && j < k)) r++;
        }
        rank[k] = r;
    }

    span_lo = span_hi = out->g[0].y;
    for (i = 1; i < out->n; i++) {
        if (out->g[i].y < span_lo) span_lo = out->g[i].y;
        if (out->g[i].y > span_hi) span_hi = out->g[i].y;
    }
    stride = (span_hi - span_lo) + 2.0 * maxh + 1.0;

    for (i = 0; i < out->n; i++)
        out->g[i].y += rank[t->grot[i] & 3] * stride;
}

/* --------------------------------------------------------------- public */

int pdf_extract_glyphs(pdf_doc *doc, pdf_obj *page, pdf_glyphs *out)
{
    tctx t;
    pdf_buf content;
    pdf_obj *res;
    resnode pageres;
    int rc = 0;

    if (!out) return PDF_E_RANGE;
    out->g = NULL;
    out->n = out->cap = 0;
    if (!doc || !pdf_is_dict(page)) return PDF_E_RANGE;

    memset(&t, 0, sizeof t);
    t.doc = doc;
    t.out = out;
    t.fc = pdf_fontcache_new(doc);
    t.ngs = 0;
    t.gs[0].ctm = page_base(doc, page, &t.pw, &t.ph);
    t.gs[0].th = 1.0;
    t.gs[0].fs = 0.0;
    t.tm = t.tlm = mat_id();

    res = pdf_page_inherited(doc, page, "Resources");
    pageres.res = res;
    pageres.next = NULL;

    pdf_buf_init(&content);
    if (pdf_page_content(doc, page, &content) == 0 && content.n > 0)
        run_content(&t, content.p, content.n, &pageres, 0);
    else
        rc = 0;                          /* an empty page is not an error */
    pdf_buf_free(&content);

    /* Annotation appearances contribute text too. */
    {
        pdf_obj *annots = pdf_dict_get(doc, page, "Annots");
        if (pdf_is_array(annots)) {
            size_t i, na = pdf_array_len(annots);
            mat base = t.gs[0].ctm;
            for (i = 0; i < na && !t.overflow; i++)
                run_annot(&t, pdf_array_get(doc, annots, i), base, &pageres);
        }
    }

    order_rot_classes(&t, out);
    free(t.grot);

    /* Mark the gaps in content order; a caller that keeps this order can use
     * space_after directly. */
    if (out->n > 1) {
        size_t i;
        for (i = 0; i + 1 < out->n; i++) {
            pdf_glyph *a = &out->g[i], *b = &out->g[i + 1];
            double sz = a->h > 0 ? a->h : b->h;
            double gap = b->x - (a->x + a->w);
            a->space_after = (fabs(b->y - a->y) < LINE_BREAK * sz &&
                              sz > 0 && gap > GAP_SPACE * sz);
        }
    }
    if (t.overflow && rc == 0) rc = PDF_E_RANGE;
    return rc;
}

void pdf_glyphs_free(pdf_glyphs *g)
{
    if (!g) return;
    free(g->g);
    g->g = NULL;
    g->n = g->cap = 0;
}

/* ------------------------------------------------------ text assembly */

typedef struct {
    double y, x;
    size_t idx;
} sortent;

static int cmp_yx(const void *a, const void *b)
{
    const sortent *p = a, *q = b;
    if (p->y < q->y) return -1;
    if (p->y > q->y) return 1;
    if (p->x < q->x) return -1;
    if (p->x > q->x) return 1;
    if (p->idx < q->idx) return -1;
    if (p->idx > q->idx) return 1;
    return 0;
}

static int cmp_x(const void *a, const void *b)
{
    const sortent *p = a, *q = b;
    if (p->x < q->x) return -1;
    if (p->x > q->x) return 1;
    if (p->idx < q->idx) return -1;
    if (p->idx > q->idx) return 1;
    return 0;
}

static int put_glyph(pdf_buf *out, const pdf_glyph *g)
{
    int i, rc = 0;
    for (i = 0; i < g->nucs && rc == 0; i++)
        rc = pdf_utf8_put(out, g->ucs[i]);
    return rc;
}

static int glyph_is_space(const pdf_glyph *g)
{
    return g->nucs == 1 && (g->ucs[0] == 0x20 || g->ucs[0] == 0xA0);
}

int pdf_glyphs_to_text(pdf_glyphs *g, int raw, pdf_buf *out)
{
    size_t i;
    sortent *se;

    if (!g || !out) return PDF_E_RANGE;
    if (g->n == 0) return 0;

    /* ---- content order ---- */
    if (raw) {
        const pdf_glyph *prev = NULL;
        for (i = 0; i < g->n; i++) {
            const pdf_glyph *c = &g->g[i];
            if (prev) {
                double sz = prev->h > 0 ? prev->h : c->h;
                double gap = c->x - (prev->x + prev->w);
                if (sz > 0 && fabs(c->y - prev->y) > LINE_BREAK * sz) {
                    if (pdf_buf_putc(out, '\n') != 0) return PDF_E_NOMEM;
                } else if (sz > 0 && gap > GAP_SPACE * sz &&
                           !glyph_is_space(c) && !glyph_is_space(prev)) {
                    if (pdf_buf_putc(out, ' ') != 0) return PDF_E_NOMEM;
                }
            }
            if (c->nucs > 0 && put_glyph(out, c) != 0) return PDF_E_NOMEM;
            prev = c;
        }
        if (pdf_buf_putc(out, '\n') != 0) return PDF_E_NOMEM;
        return 0;
    }

    /* ---- reading order: group into lines, then left to right ---- */
    se = malloc(g->n * sizeof *se);
    if (!se) return PDF_E_NOMEM;
    for (i = 0; i < g->n; i++) {
        se[i].y = g->g[i].y;
        se[i].x = g->g[i].x;
        se[i].idx = i;
    }
    qsort(se, g->n, sizeof *se, cmp_yx);

    i = 0;
    while (i < g->n) {
        size_t j = i, k;
        double base = se[i].y;
        double sz = g->g[se[i].idx].h;
        const pdf_glyph *prev = NULL;
        double minbrk = 0.0;
        size_t nbrk = 0;
        int spaced;

        if (sz <= 0) sz = 1.0;
        /* A glyph joins the line while its baseline stays within a fraction of
         * the running font size. */
        while (j < g->n) {
            double hs = g->g[se[j].idx].h;
            if (hs <= 0) hs = sz;
            if (fabs(se[j].y - base) > LINE_TOL * (hs > sz ? hs : sz)) break;
            j++;
        }
        spaced = (j - i > 1);
        qsort(se + i, j - i, sizeof *se, cmp_x);

        /* Every pair on the line broke, so there are no words left to put a
         * space between: the line is letter-spaced, and the gap that has to be
         * beaten is its own narrowest one rather than a share of the size. */
        {
            const pdf_glyph *q = NULL;
            for (k = i; k < j; k++) {
                const pdf_glyph *c = &g->g[se[k].idx];
                if (q) {
                    double s2 = q->h > 0 ? q->h : c->h;
                    double gap = c->x - (q->x + q->w);
                    if (s2 > 0 && gap > GAP_SPACE * s2) {
                        if (!nbrk || gap < minbrk) minbrk = gap;
                        nbrk++;
                    } else {
                        spaced = 0;
                    }
                }
                q = c;
            }
        }

        for (k = i; k < j; k++) {
            const pdf_glyph *c = &g->g[se[k].idx];
            if (prev) {
                double s2 = prev->h > 0 ? prev->h : c->h;
                int sp = (s2 > 0 && c->x - (prev->x + prev->w) > GAP_SPACE * s2);
                if (sp && spaced)
                    sp = (c->x - (prev->x + prev->w) > LETTER_SPACE * minbrk);
                if (sp && !glyph_is_space(c) && !glyph_is_space(prev)) {
                    if (pdf_buf_putc(out, ' ') != 0) { free(se); return PDF_E_NOMEM; }
                }
            }
            if (c->nucs > 0 && put_glyph(out, c) != 0) { free(se); return PDF_E_NOMEM; }
            prev = c;
        }
        if (pdf_buf_putc(out, '\n') != 0) { free(se); return PDF_E_NOMEM; }
        i = j;
    }
    free(se);
    return 0;
}
