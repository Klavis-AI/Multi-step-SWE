/* image.c -- the image XObjects of a page, as a reader reports them.
 *
 * ISO 32000-1 clause 8.9 (images), 7.4.7 (JBIG2Decode) and 7.4.9 (JPXDecode).
 *
 * Two jobs, and they are deliberately separate:
 *
 *   pdf_list_images()   enumerates the image XObjects each page DRAWS, with the
 *                       filter each one names.  It decodes nothing: the filter
 *                       is recognised from the /Filter chain, so the answer is
 *                       the same for a payload that decodes and one that does
 *                       not, which is exactly what a listing should report.
 *   pdf_image_samples() decodes one image to 8-bit samples, for the caller that
 *                       is writing a raster file.
 *
 * The enumeration walks the CONTENT STREAM rather than the /XObject resource
 * dictionary, because the question is which images the page draws and at what
 * size -- an XObject that is never invoked is not on the page, one invoked
 * twice is on it twice, and the resolution depends on the CTM in force at the
 * invocation.  That means a small interpreter for q/Q/cm/Do, which is all of
 * the content stream this file needs to understand.
 */

#include "pdfint.h"

#include <math.h>

#define IMG_MAX        65536    /* images one document may report          */
#define IMG_MAX_DEPTH  16       /* nested form XObjects                    */
#define IMG_MAX_GSTACK 64       /* q/Q nesting                             */
#define IMG_MAX_OPS    8        /* operands kept before an operator        */

typedef struct { double a, b, c, d, e, f; } imat;

typedef struct {
    pdf_doc       *doc;
    pdf_imageinfo *v;
    size_t         n, cap;
    int            page;
    int            seen;        /* running document-wide image index */
} ictx;

static imat imat_mul(imat x, imat y)
{
    imat r;

    r.a = x.a * y.a + x.b * y.c;
    r.b = x.a * y.b + x.b * y.d;
    r.c = x.c * y.a + x.d * y.c;
    r.d = x.c * y.b + x.d * y.d;
    r.e = x.e * y.a + x.f * y.c + y.e;
    r.f = x.e * y.b + x.f * y.d + y.f;
    return r;
}

/* ------------------------------------------------------------ colour space */

/* Table 62 and 8.6.6.  The channel count is what the listing calls `comp`;
 * the name is poppler's short spelling of the family. */
static void cs_describe(pdf_doc *doc, pdf_obj *cs, char *out, size_t outn,
                        int *comps, int depth)
{
    const char *nm;

    snprintf(out, outn, "-");
    *comps = 0;
    if (depth > 8)
        return;

    if (pdf_is_name(cs, NULL)) {
        nm = pdf_to_name(cs);
        if (!strcmp(nm, "DeviceRGB")  || !strcmp(nm, "RGB"))  { snprintf(out, outn, "rgb");  *comps = 3; }
        else if (!strcmp(nm, "DeviceGray") || !strcmp(nm, "G")) { snprintf(out, outn, "gray"); *comps = 1; }
        else if (!strcmp(nm, "DeviceCMYK") || !strcmp(nm, "CMYK")) { snprintf(out, outn, "cmyk"); *comps = 4; }
        else if (!strcmp(nm, "Pattern")) { snprintf(out, outn, "pattern"); *comps = 0; }
        return;
    }
    if (!pdf_is_array(cs) || pdf_array_len(cs) == 0)
        return;

    nm = pdf_to_name(pdf_array_get(doc, cs, 0));
    if (!strcmp(nm, "ICCBased")) {
        pdf_obj *s = pdf_array_get(doc, cs, 1);
        int n = (int)pdf_to_int(pdf_dict_get(doc, s, "N"));

        if (n == 1)      { snprintf(out, outn, "gray"); *comps = 1; }
        else if (n == 4) { snprintf(out, outn, "cmyk"); *comps = 4; }
        else             { snprintf(out, outn, "rgb");  *comps = 3; }
        return;
    }
    if (!strcmp(nm, "Indexed") || !strcmp(nm, "I")) {
        snprintf(out, outn, "index");
        *comps = 1;
        return;
    }
    if (!strcmp(nm, "CalRGB"))  { snprintf(out, outn, "rgb");  *comps = 3; return; }
    if (!strcmp(nm, "CalGray")) { snprintf(out, outn, "gray"); *comps = 1; return; }
    if (!strcmp(nm, "Lab"))     { snprintf(out, outn, "lab");  *comps = 3; return; }
    if (!strcmp(nm, "Separation")) { snprintf(out, outn, "sep"); *comps = 1; return; }
    if (!strcmp(nm, "DeviceN")) {
        pdf_obj *names = pdf_array_get(doc, cs, 1);

        snprintf(out, outn, "devn");
        *comps = (int)pdf_array_len(names);
        return;
    }
    if (!strcmp(nm, "Pattern")) { snprintf(out, outn, "pattern"); *comps = 0; return; }
    /* An unknown family is still a colour space; describe it as the base. */
    cs_describe(doc, pdf_array_get(doc, cs, 1), out, outn, comps, depth + 1);
}

/* -------------------------------------------------------------- the filter */

/* 8.9.7 table 12/13.  Which codec the chain NAMES -- the last filter that is a
 * codec, since a codec is always the final stage.  Nothing is decoded to find
 * this out, which is why a corrupt payload reports the same as a good one. */
static void enc_of(pdf_doc *doc, pdf_obj *stm, char *out, size_t outn)
{
    pdf_obj *filter = pdf_dict_geta(doc, stm, "Filter", "F");
    size_t i, n;

    snprintf(out, outn, "image");
    if (pdf_is_name(filter, NULL))
        n = 1;
    else if (pdf_is_array(filter))
        n = pdf_array_len(filter);
    else
        return;

    for (i = 0; i < n; i++) {
        pdf_obj *fo = (n == 1 && !pdf_is_array(filter))
                          ? filter : pdf_array_get(doc, filter, i);
        const char *nm = pdf_to_name(fo);

        if (!strcmp(nm, "DCTDecode")   || !strcmp(nm, "DCT"))
            snprintf(out, outn, "jpeg");
        else if (!strcmp(nm, "JPXDecode"))
            snprintf(out, outn, "jpx");
        else if (!strcmp(nm, "JBIG2Decode"))
            snprintf(out, outn, "jbig2");
        else if (!strcmp(nm, "CCITTFaxDecode") || !strcmp(nm, "CCF"))
            snprintf(out, outn, "ccitt");
    }
}

/* ------------------------------------------------- the JPEG2000 SIZ header */

/* 7.4.9: a JPXDecode stream carries its own dimensions, component count and
 * bit depth, and /ColorSpace is OPTIONAL because of it.  Reading the SIZ
 * marker segment is not decoding -- it is the header -- and it is the only way
 * to fill in the listing's `color`, `comp` and `bpc` for such an image.
 *
 * Both packagings are accepted: a bare codestream starts with SOC immediately,
 * and a JP2 container carries the same codestream inside a 'jp2c' box, so the
 * marker pair is searched for rather than assumed to be at offset zero. */
int pdf_jpx_header(const unsigned char *p, size_t n, int *w, int *h,
                   int *ncomp, int *bpc)
{
    size_t i, lim;

    if (!p || n < 4)
        return -1;
    lim = n > 4096 ? 4096 : n;
    for (i = 0; i + 1 < lim; i++)
        if (p[i] == 0xff && p[i + 1] == 0x4f)
            break;
    if (i + 1 >= lim)
        return -1;
    i += 2;
    /* SOC is immediately followed by SIZ in every conforming codestream. */
    if (i + 40 > n || p[i] != 0xff || p[i + 1] != 0x51)
        return -1;
    i += 4;                                     /* past marker and Lsiz */
    i += 2;                                     /* Rsiz */
    {
        unsigned long xs, ys, xo, yo;
        int cs;

        xs = ((unsigned long)p[i] << 24) | ((unsigned long)p[i + 1] << 16)
           | ((unsigned long)p[i + 2] << 8) | p[i + 3];
        ys = ((unsigned long)p[i + 4] << 24) | ((unsigned long)p[i + 5] << 16)
           | ((unsigned long)p[i + 6] << 8) | p[i + 7];
        xo = ((unsigned long)p[i + 8] << 24) | ((unsigned long)p[i + 9] << 16)
           | ((unsigned long)p[i + 10] << 8) | p[i + 11];
        yo = ((unsigned long)p[i + 12] << 24) | ((unsigned long)p[i + 13] << 16)
           | ((unsigned long)p[i + 14] << 8) | p[i + 15];
        i += 32;                                /* past the eight size fields */
        if (i + 2 > n) return -1;
        cs = (p[i] << 8) | p[i + 1];
        i += 2;
        if (cs <= 0 || cs > 16384 || i >= n) return -1;
        if (w)     *w = (int)(xs - xo);
        if (h)     *h = (int)(ys - yo);
        if (ncomp) *ncomp = cs;
        if (bpc)   *bpc = (p[i] & 0x7f) + 1;
        return 0;
    }
}

/* A codec is always the LAST stage of a chain, but it need not be the only
 * one: [ /FlateDecode /JPXDecode ] stores a compressed codestream, and the
 * codestream's own header is inside the Flate, not in the stored bytes.  This
 * runs the general-purpose stages and stops at the codec, so what comes back
 * is exactly what the codec would be handed.  pdf_apply_filters() cannot be
 * used: it refuses the whole chain the moment it sees a codec it does not
 * apply, which is right for --filtered-stream-data and wrong here. */
static int pre_codec_bytes(pdf_doc *doc, pdf_obj *stm, pdf_buf *out)
{
    pdf_obj *filter = pdf_dict_geta(doc, stm, "Filter", "F");
    pdf_obj *parms  = pdf_dict_geta(doc, stm, "DecodeParms", "DP");
    pdf_buf raw, a, b;
    const unsigned char *in;
    size_t inlen, i, n;
    pdf_buf *dst;
    int rc = 0;

    pdf_buf_init(&raw);
    if (pdf_stream_data(doc, stm, &raw, 0) != 0) {
        pdf_buf_free(&raw);
        return -1;
    }
    if (!pdf_is_array(filter)) {        /* one stage: it IS the codec */
        rc = pdf_buf_put(out, raw.p, raw.n);
        pdf_buf_free(&raw);
        return rc;
    }

    pdf_buf_init(&a);
    pdf_buf_init(&b);
    in = raw.p;
    inlen = raw.n;
    dst = &a;
    for (i = 0, n = pdf_array_len(filter); i < n; i++) {
        const char *nm = pdf_to_name(pdf_array_get(doc, filter, i));
        pdf_obj *po = pdf_is_array(parms) ? pdf_array_get(doc, parms, i)
                    : (pdf_is_dict(parms) ? parms : NULL);
        int isflate;

        if (po && !pdf_is_dict(po)) po = NULL;
        dst->n = 0;
        isflate = (!strcmp(nm, "FlateDecode") || !strcmp(nm, "Fl"));
        if (isflate)
            rc = pdf_filter_flate(doc, in, inlen, po, dst);
        else if (!strcmp(nm, "LZWDecode") || !strcmp(nm, "LZW"))
            rc = pdf_filter_lzw(doc, in, inlen, po, dst);
        else if (!strcmp(nm, "ASCII85Decode") || !strcmp(nm, "A85"))
            rc = pdf_filter_a85(in, inlen, dst);
        else if (!strcmp(nm, "ASCIIHexDecode") || !strcmp(nm, "AHx"))
            rc = pdf_filter_ahx(in, inlen, dst);
        else if (!strcmp(nm, "RunLengthDecode") || !strcmp(nm, "RL"))
            rc = pdf_filter_rl(in, inlen, dst);
        else
            break;                      /* the codec, or something unknown */
        if (rc < 0) break;
        if (isflate || !strcmp(nm, "LZWDecode") || !strcmp(nm, "LZW"))
            if (pdf_predictor(doc, dst, po) < 0) break;
        in = dst->p;
        inlen = dst->n;
        dst = (dst == &a) ? &b : &a;
    }
    rc = pdf_buf_put(out, in, inlen);
    pdf_buf_free(&a);
    pdf_buf_free(&b);
    pdf_buf_free(&raw);
    return rc;
}

/* 7.4.9.  A codestream that was cut short cannot be OPENED, however well its
 * header reads -- the SIZ marker survives a truncation that removes every
 * packet after it.  The end-of-codestream marker is what says the data is all
 * there, and a reader that cannot open the data falls back on describing it as
 * a single 8-bit channel rather than trusting a header it could not act on. */
static int jpx_openable(const unsigned char *p, size_t n)
{
    return n >= 4 && p[n - 2] == 0xff && p[n - 1] == 0xd9;
}

/* --------------------------------------------------------- one image found */

static void img_record(ictx *t, pdf_obj *stm, imat m, int objnum, int objgen)
{
    pdf_imageinfo *r;
    pdf_obj *o;
    double wpt, hpt;

    if (t->n >= IMG_MAX)
        return;
    if (t->n >= t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 32;
        pdf_imageinfo *nv = realloc(t->v, cap * sizeof *nv);

        if (!nv) return;
        t->v = nv;
        t->cap = cap;
    }
    r = &t->v[t->n];
    memset(r, 0, sizeof *r);

    r->page   = t->page;
    r->num    = t->seen++;
    r->objnum = objnum;
    r->objgen = objgen;
    r->width  = (int)pdf_to_int(pdf_dict_geta(t->doc, stm, "Width", "W"));
    r->height = (int)pdf_to_int(pdf_dict_geta(t->doc, stm, "Height", "H"));
    r->bpc    = (int)pdf_to_int(pdf_dict_geta(t->doc, stm, "BitsPerComponent", "BPC"));

    o = pdf_dict_geta(t->doc, stm, "ImageMask", "IM");
    r->is_mask = (o && o->kind == PDF_BOOL && o->u.b);
    o = pdf_dict_geta(t->doc, stm, "Interpolate", "I");
    r->interp = (o && o->kind == PDF_BOOL && o->u.b);

    snprintf(r->type, sizeof r->type, "%s", r->is_mask ? "mask" : "image");
    enc_of(t->doc, stm, r->enc, sizeof r->enc);

    cs_describe(t->doc, pdf_dict_geta(t->doc, stm, "ColorSpace", "CS"),
                r->color, sizeof r->color, &r->comps, 0);

    if (r->is_mask) {
        snprintf(r->color, sizeof r->color, "-");
        r->comps = 1;
        r->bpc = 1;
    }

    /* 7.4.9: with no /ColorSpace the JPEG2000 data's own header answers, and
     * with one present the dictionary wins and the data's is ignored. */
    if (strcmp(r->enc, "jpx") == 0 && (r->comps == 0 || r->bpc == 0)) {
        pdf_buf cs;
        int jw = 0, jh = 0, jc = 0, jb = 0, got = 0;

        pdf_buf_init(&cs);
        if (pre_codec_bytes(t->doc, stm, &cs) == 0 && jpx_openable(cs.p, cs.n)
            && pdf_jpx_header(cs.p, cs.n, &jw, &jh, &jc, &jb) == 0)
            got = 1;
        pdf_buf_free(&cs);

        if (got) {
            if (r->comps == 0) {
                r->comps = jc;
                if (jc == 1)      snprintf(r->color, sizeof r->color, "gray");
                else if (jc == 4) snprintf(r->color, sizeof r->color, "cmyk");
                else              snprintf(r->color, sizeof r->color, "rgb");
            }
            if (r->bpc == 0) r->bpc = jb;
            if (r->width  <= 0) r->width  = jw;
            if (r->height <= 0) r->height = jh;
        } else {
            /* Nothing could be opened: one 8-bit grey channel, which is what
             * the reference reports for a payload it could not read. */
            if (r->comps == 0) {
                r->comps = 1;
                snprintf(r->color, sizeof r->color, "gray");
            }
            if (r->bpc == 0) r->bpc = 8;
        }
    }

    /* The listing's `size` is the STORED length -- the encoded bytes as the
     * file holds them -- which is what makes `ratio` a compression ratio. */
    r->size = -1;
    {
        pdf_buf raw;

        pdf_buf_init(&raw);
        if (pdf_stream_data(t->doc, stm, &raw, 0) == 0)
            r->size = (long long)raw.n;
        pdf_buf_free(&raw);
    }

    /* Resolution is a property of the placement, not of the image: the unit
     * square maps through the CTM onto the page, so the pixels-per-inch is the
     * pixel count over the painted size in inches. */
    wpt = sqrt(m.a * m.a + m.b * m.b);
    hpt = sqrt(m.c * m.c + m.d * m.d);
    r->xppi = (wpt > 1e-9) ? (double)r->width  * 72.0 / wpt : 0.0;
    r->yppi = (hpt > 1e-9) ? (double)r->height * 72.0 / hpt : 0.0;

    t->n++;
}

/* -------------------------------------------------- the content-stream walk */

static void img_run(ictx *t, const unsigned char *buf, size_t len,
                    pdf_obj *res, imat base, int depth);

static void img_do(ictx *t, const char *name, pdf_obj *res, imat ctm, int depth)
{
    pdf_doc *doc = t->doc;
    pdf_obj *xo = pdf_dict_get(doc, pdf_dict_get(doc, res, "XObject"), name);
    pdf_obj *sub;
    int num = 0, gen = 0, k;

    if (!pdf_is_stream(xo))
        return;
    sub = pdf_dict_get(doc, xo, "Subtype");

    /* The object number is wanted for the report, and the loader caches one
     * pdf_obj per object number, so identity recovers it. */
    for (k = 1; k <= pdf_max_objnum(doc); k++) {
        int type = 0, g = 0;
        long long off = 0;

        if (!pdf_xref_entry(doc, k, &type, &off, &g))
            continue;
        if (pdf_get_object(doc, k, g) == xo) { num = k; gen = g; break; }
    }

    if (pdf_is_name(sub, "Image")) {
        img_record(t, xo, ctm, num, gen);
        return;
    }
    if (pdf_is_name(sub, "Form") && depth < IMG_MAX_DEPTH) {
        pdf_obj *m = pdf_dict_get(doc, xo, "Matrix");
        pdf_obj *r2 = pdf_dict_get(doc, xo, "Resources");
        imat fm = { 1, 0, 0, 1, 0, 0 };
        pdf_buf data;

        if (pdf_is_array(m) && pdf_array_len(m) >= 6) {
            double v[6];
            int i;

            for (i = 0; i < 6; i++)
                v[i] = pdf_to_real(pdf_array_get(doc, m, (size_t)i));
            fm.a = v[0]; fm.b = v[1]; fm.c = v[2];
            fm.d = v[3]; fm.e = v[4]; fm.f = v[5];
        }
        pdf_buf_init(&data);
        if (pdf_stream_data(doc, xo, &data, 1) == 0 && data.n > 0)
            img_run(t, data.p, data.n, pdf_is_dict(r2) ? r2 : res,
                    imat_mul(fm, ctm), depth + 1);
        pdf_buf_free(&data);
    }
}

static void img_run(ictx *t, const unsigned char *buf, size_t len,
                    pdf_obj *res, imat base, int depth)
{
    pdf_lexer lx;
    pdf_token tok;
    imat stack[IMG_MAX_GSTACK];
    imat ctm = base;
    double num[IMG_MAX_OPS];
    char lastname[128];
    int nnum = 0, sp = 0, havename = 0;

    lastname[0] = '\0';
    pdf_lex_init(&lx, buf, len, 0);
    while (pdf_lex_next(t->doc, &lx, &tok) == 0 && tok.type != PDF_TOK_EOF) {
        switch (tok.type) {
        case PDF_TOK_INT:
        case PDF_TOK_REAL:
            if (nnum < IMG_MAX_OPS)
                num[nnum++] = (tok.type == PDF_TOK_INT) ? (double)tok.i : tok.r;
            continue;
        case PDF_TOK_NAME:
            if (tok.s) {
                snprintf(lastname, sizeof lastname, "%s", tok.s);
                havename = 1;
            }
            continue;
        case PDF_TOK_KEYWORD:
            break;
        default:
            continue;                   /* arrays, strings, dictionaries */
        }

        if (tok.s == NULL) { nnum = 0; havename = 0; continue; }

        if (strcmp(tok.s, "q") == 0) {
            if (sp < IMG_MAX_GSTACK) stack[sp] = ctm;
            sp++;
        } else if (strcmp(tok.s, "Q") == 0) {
            if (sp > 0) {
                sp--;
                if (sp < IMG_MAX_GSTACK) ctm = stack[sp];
            }
        } else if (strcmp(tok.s, "cm") == 0 && nnum >= 6) {
            imat m;

            m.a = num[nnum - 6]; m.b = num[nnum - 5]; m.c = num[nnum - 4];
            m.d = num[nnum - 3]; m.e = num[nnum - 2]; m.f = num[nnum - 1];
            ctm = imat_mul(m, ctm);
        } else if (strcmp(tok.s, "Do") == 0 && havename) {
            img_do(t, lastname, res, ctm, depth);
        }
        nnum = 0;
        havename = 0;
    }
}

/* 8.9.5.  An inline image is drawn by the content stream itself and has no
 * object number.  It is deliberately NOT reported here: the lexer hands back
 * the BI/ID/EI run as ordinary tokens, the binary payload between ID and EI is
 * not lexable, and a listing that guessed at it would be wrong more often than
 * right.  Every image this milestone is asked about is an XObject. */

int pdf_list_images(pdf_doc *doc, int first, int last,
                    pdf_imageinfo **out, size_t *nout)
{
    ictx t;
    int pg;

    if (!out || !nout)
        return PDF_E_RANGE;
    *out = NULL;
    *nout = 0;
    if (!doc)
        return PDF_E_RANGE;

    memset(&t, 0, sizeof t);
    t.doc = doc;

    for (pg = first; pg <= last; pg++) {
        pdf_obj *page = pdf_page_dict(doc, pg - 1);
        pdf_buf content;
        imat base = { 1, 0, 0, 1, 0, 0 };

        if (!pdf_is_dict(page))
            continue;
        t.page = pg;
        pdf_buf_init(&content);
        if (pdf_page_content(doc, page, &content) == 0 && content.n > 0)
            img_run(&t, content.p, content.n,
                    pdf_page_inherited(doc, page, "Resources"), base, 0);
        pdf_buf_free(&content);
    }

    *out = t.v;
    *nout = t.n;
    return 0;
}
