/* write.c -- write a document back out.
 *
 * Three options produce a FILE rather than a report: --decrypt,
 * --generate-appearances and --flatten-annotations.  What they have in common
 * is this serializer; what differs is the transformation each applies to the
 * in-memory document first.
 *
 * Object numbering, object order, and whether anything is compressed are all
 * free -- nothing downstream compares the bytes.  Two things are NOT free and
 * both are honoured here:
 *
 *   * the header version is the INPUT's.  A reader compares "PDF version", so
 *     a product that rewrote the header would fail on a document it had
 *     otherwise handled perfectly;
 *   * generation numbers are preserved.  The references inside the objects say
 *     "N G R", and renumbering only the objects would break every one of them.
 *
 * Everything is written uncompressed and unencrypted: pdf_get_object() has
 * already decrypted the strings and pdf_stream_data() the stream bodies, so
 * what is serialized is plaintext by the time it arrives here.
 */

#include "pdfint.h"

#include <math.h>

#define W_MAX_EXTRA  8192       /* objects this run may invent */
#define W_MAX_DEPTH  256

typedef struct {
    pdf_doc  *doc;
    pdf_buf   out;
    long long *off;             /* file offset of each object, by number */
    int       *gen;
    int        maxobj;
    pdf_obj  **extra;           /* objects created by a transformation */
    int        nextra;
    int        firstextra;      /* object number of extra[0] */
} wctx;

/* ----------------------------------------------------------- serialization */

static void w_obj(wctx *w, pdf_obj *o, int depth);

static void w_string(wctx *w, const char *p, size_t n)
{
    size_t i;

    pdf_buf_putc(&w->out, '(');
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];

        if (c == '(' || c == ')' || c == '\\') {
            pdf_buf_putc(&w->out, '\\');
            pdf_buf_putc(&w->out, (char)c);
        } else if (c == '\n') {
            pdf_buf_put(&w->out, "\\n", 2);
        } else if (c == '\r') {
            pdf_buf_put(&w->out, "\\r", 2);
        } else if (c < 32 || c > 126) {
            pdf_buf_printf(&w->out, "\\%03o", (unsigned)c);
        } else {
            pdf_buf_putc(&w->out, (char)c);
        }
    }
    pdf_buf_putc(&w->out, ')');
}

static void w_name(wctx *w, const char *nm)
{
    static const char hexd[] = "0123456789abcdef";
    size_t i;

    pdf_buf_putc(&w->out, '/');
    if (!nm) return;
    for (i = 0; nm[i]; i++) {
        unsigned char c = (unsigned char)nm[i];

        if (c < 33 || c > 126 || c == '/' || c == '(' || c == ')' || c == '<'
            || c == '>' || c == '[' || c == ']' || c == '{' || c == '}'
            || c == '%' || c == '#') {
            pdf_buf_putc(&w->out, '#');
            pdf_buf_putc(&w->out, hexd[c >> 4]);
            pdf_buf_putc(&w->out, hexd[c & 15]);
        } else {
            pdf_buf_putc(&w->out, (char)c);
        }
    }
}

static void w_dict_body(wctx *w, pdf_obj *d, int depth, long long lenoverride)
{
    size_t i, n;

    pdf_buf_put(&w->out, "<<", 2);
    for (i = 0, n = pdf_dict_len(d); i < n; i++) {
        const char *k = pdf_dict_key(d, i);

        if (!k) continue;
        if (lenoverride >= 0 && strcmp(k, "Length") == 0) continue;
        pdf_buf_putc(&w->out, ' ');
        w_name(w, k);
        pdf_buf_putc(&w->out, ' ');
        w_obj(w, pdf_dict_val_raw(d, i), depth + 1);
    }
    if (lenoverride >= 0)
        pdf_buf_printf(&w->out, " /Length %lld", lenoverride);
    pdf_buf_put(&w->out, " >>", 3);
}

static void w_obj(wctx *w, pdf_obj *o, int depth)
{
    size_t i, n, slen;
    const char *s;

    if (!o || depth > W_MAX_DEPTH) {
        pdf_buf_put(&w->out, "null", 4);
        return;
    }
    switch (o->kind) {
    case PDF_NULL:  pdf_buf_put(&w->out, "null", 4); break;
    case PDF_BOOL:  pdf_buf_put(&w->out, o->u.b ? "true" : "false",
                                o->u.b ? 4 : 5); break;
    case PDF_INT:   pdf_buf_printf(&w->out, "%lld", (long long)o->u.i); break;
    case PDF_REAL:
        if (o->u.real.lit)
            pdf_buf_put(&w->out, o->u.real.lit, strlen(o->u.real.lit));
        else
            pdf_fmt_real(&w->out, o->u.real.v);
        break;
    case PDF_STRING:
        s = pdf_to_string(o, &slen);
        w_string(w, s ? s : "", s ? slen : 0);
        break;
    case PDF_NAME:  w_name(w, pdf_to_name(o)); break;
    case PDF_ARRAY:
        pdf_buf_putc(&w->out, '[');
        for (i = 0, n = pdf_array_len(o); i < n; i++) {
            pdf_buf_putc(&w->out, ' ');
            w_obj(w, pdf_array_get_raw(o, i), depth + 1);
        }
        pdf_buf_put(&w->out, " ]", 2);
        break;
    case PDF_DICT:   w_dict_body(w, o, depth, -1); break;
    case PDF_STREAM: w_dict_body(w, o->u.stm.dict, depth, -1); break;
    case PDF_REF:    pdf_buf_printf(&w->out, "%d %d R", o->u.ref.num,
                                    o->u.ref.gen); break;
    default:         pdf_buf_put(&w->out, "null", 4); break;
    }
}

/* One indirect object, with its offset recorded for the table. */
static void w_indirect(wctx *w, int num, int gen, pdf_obj *o)
{
    if (num <= 0 || num > w->maxobj + w->nextra)
        return;
    w->off[num] = (long long)w->out.n;
    w->gen[num] = gen;
    pdf_buf_printf(&w->out, "%d %d obj\n", num, gen);

    if (pdf_is_stream(o)) {
        pdf_buf data;

        pdf_buf_init(&data);
        /* decode == 0: the stored bytes AFTER decryption, so the /Filter chain
         * in the dictionary still describes them exactly. */
        (void)pdf_stream_data(w->doc, o, &data, 0);
        w_dict_body(w, o->u.stm.dict, 0, (long long)data.n);
        pdf_buf_put(&w->out, "\nstream\n", 8);
        if (data.n) pdf_buf_put(&w->out, data.p, data.n);
        pdf_buf_put(&w->out, "\nendstream", 10);
        pdf_buf_free(&data);
    } else {
        w_obj(w, o, 0);
    }
    pdf_buf_put(&w->out, "\nendobj\n", 8);
}

/* 7.5.7 and 7.5.8: an object stream and a cross-reference stream describe how
 * the FILE was put together, and this file is being put together differently.
 * Their contents are written as ordinary objects, so the containers would be
 * stale duplicates. */
static int w_skip(pdf_doc *doc, pdf_obj *o, int num, int encnum)
{
    if (num == encnum)
        return 1;
    if (!pdf_is_stream(o))
        return 0;
    return pdf_is_name(pdf_dict_get_raw(o, "Type"), "ObjStm")
        || pdf_is_name(pdf_dict_get_raw(o, "Type"), "XRef");
}

/* ------------------------------------------------------------ new objects */

static int w_add(wctx *w, pdf_obj *o)
{
    int num;

    if (w->nextra >= W_MAX_EXTRA)
        return -1;
    num = w->firstextra + w->nextra;

    /* One transformation may reference an object another has just created:
     * --flatten-annotations draws the appearance --generate-appearances built
     * a moment earlier, and reaches it through the /AP /N reference like any
     * other.  So a new object has to be resolvable THROUGH THE DOCUMENT, not
     * merely present in this writer's list -- otherwise that reference dangles
     * and the appearance silently fails to be drawn. */
    if (pdf_xref_ensure(w->doc, num) >= 0 && num < w->doc->xrefcap) {
        w->doc->xref[num].type   = PDF_XREF_NORMAL;
        w->doc->xref[num].gen    = 0;
        w->doc->xref[num].off    = 0;
        w->doc->xref[num].cached = o;
        if (num > w->doc->maxobj)
            w->doc->maxobj = num;
    }
    w->extra[w->nextra] = o;
    return w->firstextra + w->nextra++;
}

/* A stream this run built: no /Filter, body held in memory. */
static pdf_obj *w_new_stream(pdf_doc *doc, pdf_obj *dict,
                             const unsigned char *p, size_t n)
{
    pdf_obj *s = pdf_alloc(doc, sizeof *s);
    unsigned char *mem;

    if (!s) return NULL;
    memset(s, 0, sizeof *s);
    s->kind = PDF_STREAM;
    s->u.stm.dict = dict;
    s->u.stm.off = -1;
    s->u.stm.len = (long long)n;
    s->u.stm.num = 0;           /* never decrypted: it was never encrypted */
    s->u.stm.gen = 0;
    mem = pdf_alloc(doc, n ? n : 1);
    if (!mem) return NULL;
    if (n) memcpy(mem, p, n);
    s->u.stm.mem = mem;
    s->u.stm.memlen = n;
    return s;
}

/* ------------------------------------------------ transformation: appearances */

int pdf_form_need_appearances(pdf_doc *doc)
{
    pdf_obj *acro = pdf_dict_get(doc, pdf_catalog(doc), "AcroForm");
    pdf_obj *na = pdf_dict_get(doc, acro, "NeedAppearances");

    return (na && na->kind == PDF_BOOL && na->u.b);
}

/* 12.7.3.3.  Give every field that needs one a /N appearance built from its
 * value, then clear /NeedAppearances: the appearances are current now, which
 * is the whole of what this option claims. */
static void w_generate_appearances(wctx *w)
{
    pdf_doc *doc = w->doc;
    pdf_obj *acro = pdf_dict_get(doc, pdf_catalog(doc), "AcroForm");
    int npages = pdf_page_count(doc), pg;

    /* Measured against the reference tool: this option acts only when the
     * document ITSELF says its appearances are out of date.  On a file without
     * /NeedAppearances nothing changes -- which is why flattening such a file
     * discards a value that has no appearance stream, rather than rescuing it.
     * Note this is deliberately NOT the reader's rule: a reader shows a value
     * that has no appearance, because 12.7.3.3 tells it to construct one. */
    if (!pdf_is_dict(acro) || !pdf_form_need_appearances(doc))
        return;

    for (pg = 0; pg < npages; pg++) {
        pdf_obj *page = pdf_page_dict(doc, pg);
        pdf_obj *annots = pdf_dict_get(doc, page, "Annots");
        size_t i, n;

        for (i = 0, n = pdf_array_len(annots); i < n; i++) {
            pdf_obj *a = pdf_array_get(doc, annots, i);
            pdf_obj *ap, *apd, *bbox, *res, *stm;
            pdf_buf body;
            double bw = 0, bh = 0;
            int num;

            if (!pdf_is_dict(a))
                continue;
            ap = pdf_dict_get(doc, a, "AP");
            if (!pdf_field_wants_appearance(doc, a, pdf_is_dict(ap)))
                continue;

            pdf_buf_init(&body);
            if (pdf_field_appearance(doc, a, &body, &bw, &bh) != 0 || body.n == 0) {
                pdf_buf_free(&body);
                continue;
            }

            /* Table 168: the appearance is a form XObject whose /BBox is the
             * widget's own size, so 12.5.5's mapping is the identity. */
            apd = pdf_new_dict(doc);
            pdf_dict_put(doc, apd, "Type", pdf_new_name(doc, "XObject"));
            pdf_dict_put(doc, apd, "Subtype", pdf_new_name(doc, "Form"));
            bbox = pdf_new_array(doc);
            pdf_array_push(doc, bbox, pdf_new_int(doc, 0));
            pdf_array_push(doc, bbox, pdf_new_int(doc, 0));
            pdf_array_push(doc, bbox, pdf_new_real(doc, bw));
            pdf_array_push(doc, bbox, pdf_new_real(doc, bh));
            pdf_dict_put(doc, apd, "BBox", bbox);
            res = pdf_dict_get(doc, acro, "DR");
            if (pdf_is_dict(res))
                pdf_dict_put(doc, apd, "Resources", res);

            stm = w_new_stream(doc, apd, body.p, body.n);
            pdf_buf_free(&body);
            if (!stm) continue;
            num = w_add(w, stm);
            if (num < 0) continue;

            {
                pdf_obj *nd = pdf_new_dict(doc);

                pdf_dict_put(doc, nd, "N", pdf_new_ref(doc, num, 0));
                pdf_dict_put(doc, a, "AP", nd);
            }
        }
    }
    pdf_dict_put(doc, acro, "NeedAppearances", pdf_new_bool(doc, 0));
}

/* -------------------------------------------------- transformation: flatten */

/* Table 165: bit 3 Print, bit 2 Hidden, bit 6 NoView. */
#define ANN_HIDDEN 2
#define ANN_PRINT  4
#define ANN_NOVIEW 32

static pdf_obj *w_ap_stream(pdf_doc *doc, pdf_obj *annot)
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

static int w_rect(pdf_doc *doc, pdf_obj *arr, double *v)
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

/* 12.5.5, algorithm 8.1: the /Matrix-transformed /BBox is mapped corner to
 * corner onto /Rect.  Do applies /Matrix itself, so the operator written into
 * the page is only the scale-and-translate half. */
static void w_flatten(wctx *w, int flags)
{
    pdf_doc *doc = w->doc;
    int npages = pdf_page_count(doc), pg, seq = 0;

    for (pg = 0; pg < npages; pg++) {
        pdf_obj *page = pdf_page_dict(doc, pg);
        pdf_obj *annots = pdf_dict_get(doc, page, "Annots");
        pdf_obj *res, *xod, *newc;
        pdf_buf content;
        size_t i, n;
        int drew = 0;

        n = pdf_array_len(annots);
        if (!pdf_is_array(annots))
            continue;

        pdf_buf_init(&content);
        if (pdf_page_content(doc, page, &content) != 0)
            content.n = 0;

        res = pdf_page_inherited(doc, page, "Resources");
        if (!pdf_is_dict(res)) {
            res = pdf_new_dict(doc);
            pdf_dict_put(doc, page, "Resources", res);
        }
        xod = pdf_dict_get(doc, res, "XObject");
        if (!pdf_is_dict(xod)) {
            xod = pdf_new_dict(doc);
            pdf_dict_put(doc, res, "XObject", xod);
        }

        for (i = 0; i < n; i++) {
            pdf_obj *a = pdf_array_get(doc, annots, i);
            pdf_obj *ap;
            double rect[4], bbox[4] = { 0, 0, 1, 1 };
            double sx = 1.0, sy = 1.0;
            long long af;
            char nm[64];
            int num;

            if (!pdf_is_dict(a)) continue;
            af = pdf_to_int(pdf_dict_get(doc, a, "F"));
            if (af & (ANN_HIDDEN | ANN_NOVIEW)) continue;
            if ((flags & PDF_WRITE_PRINT_ONLY) && !(af & ANN_PRINT)) continue;
            if ((flags & PDF_WRITE_SCREEN_ONLY) && (af & ANN_PRINT)) continue;

            ap = w_ap_stream(doc, a);
            if (!ap) continue;
            if (w_rect(doc, pdf_dict_get(doc, a, "Rect"), rect) != 0) continue;
            if (w_rect(doc, pdf_dict_get(doc, ap, "BBox"), bbox) != 0) {
                bbox[0] = 0; bbox[1] = 0; bbox[2] = 1; bbox[3] = 1;
            }
            if (bbox[2] - bbox[0] > 1e-9) sx = (rect[2] - rect[0]) / (bbox[2] - bbox[0]);
            if (bbox[3] - bbox[1] > 1e-9) sy = (rect[3] - rect[1]) / (bbox[3] - bbox[1]);

            num = w_add(w, ap);
            if (num < 0) continue;
            snprintf(nm, sizeof nm, "FlatAnnot%d", ++seq);
            pdf_dict_put(doc, xod, nm, pdf_new_ref(doc, num, 0));

            if (content.n && content.p[content.n - 1] != '\n')
                pdf_buf_putc(&content, '\n');
            pdf_buf_printf(&content, "q %.6f 0 0 %.6f %.6f %.6f cm /%s Do Q\n",
                           sx, sy, rect[0] - bbox[0] * sx, rect[1] - bbox[1] * sy,
                           nm);
            drew = 1;
        }

        /* The annotations are gone whether or not any of them drew: flattening
         * a field whose appearance was not trustworthy DISCARDS it, which is
         * the correct outcome of an incorrect document. */
        pdf_dict_del(page, "Annots");

        if (drew || content.n) {
            newc = w_new_stream(doc, pdf_new_dict(doc), content.p, content.n);
            if (newc) {
                int num = w_add(w, newc);

                if (num >= 0)
                    pdf_dict_put(doc, page, "Contents",
                                 pdf_new_ref(doc, num, 0));
            }
        }
        pdf_buf_free(&content);
    }

    /* 12.7.2: with no field left anywhere the catalogue no longer describes a
     * form, and a reader reports the document as having none. */
    pdf_dict_del(pdf_catalog(doc), "AcroForm");
}

/* -------------------------------------------------------------- the driver */

int pdf_write_document(pdf_doc *doc, const char *path, int flags, int *declined)
{
    wctx w;
    FILE *f;
    pdf_obj *tr;
    long long xrefoff;
    int n, encnum = -1, rc = 0, total;
    size_t i;

    if (declined) *declined = 0;
    if (!doc || !path) return PDF_E_RANGE;

    memset(&w, 0, sizeof w);
    w.doc = doc;
    w.maxobj = pdf_max_objnum(doc);
    if (w.maxobj < 1) w.maxobj = 1;
    w.firstextra = w.maxobj + 1;
    pdf_buf_init(&w.out);
    total = w.maxobj + W_MAX_EXTRA + 2;
    w.off = calloc((size_t)total, sizeof *w.off);
    w.gen = calloc((size_t)total, sizeof *w.gen);
    w.extra = calloc(W_MAX_EXTRA, sizeof *w.extra);
    if (!w.off || !w.gen || !w.extra) { rc = PDF_E_NOMEM; goto done; }

    {
        pdf_obj *e = pdf_dict_get_raw(pdf_trailer(doc), "Encrypt");
        if (e && e->kind == PDF_REF) encnum = e->u.ref.num;
    }

    if (flags & PDF_WRITE_APPEARANCES)
        w_generate_appearances(&w);
    if (flags & PDF_WRITE_FLATTEN) {
        /* The refusal: appearances the document itself says are out of date
         * are not drawn into the page, and the caller is told so. */
        if (pdf_form_need_appearances(doc)) {
            if (declined) *declined = 1;
        } else {
            w_flatten(&w, flags);
        }
    }

    /* 7.5.2: the header version is the input's. */
    pdf_buf_printf(&w.out, "%%PDF-%d.%d\n", pdf_header_version(doc) / 10,
                   pdf_header_version(doc) % 10);
    pdf_buf_put(&w.out, "%\xe2\xe3\xcf\xd3\n", 6);

    for (n = 1; n <= w.maxobj; n++) {
        int type = 0, gen = 0;
        long long off = 0;
        pdf_obj *o;

        if (!pdf_xref_entry(doc, n, &type, &off, &gen)) {
            int sn = 0, si = 0;

            if (!pdf_xref_compressed(doc, n, &sn, &si))
                continue;
            gen = 0;
        }
        o = pdf_get_object(doc, n, gen);
        if (pdf_is_null(o)) continue;
        if (w_skip(doc, o, n, encnum)) continue;
        w_indirect(&w, n, gen, o);
    }
    for (i = 0; i < (size_t)w.nextra; i++)
        w_indirect(&w, w.firstextra + (int)i, 0, w.extra[i]);

    /* 7.5.4: one classic subsection covering every object number. */
    xrefoff = (long long)w.out.n;
    {
        int last = w.maxobj + w.nextra;

        pdf_buf_printf(&w.out, "xref\n0 %d\n", last + 1);
        pdf_buf_printf(&w.out, "0000000000 65535 f \n");
        for (n = 1; n <= last; n++) {
            if (w.off[n] > 0)
                pdf_buf_printf(&w.out, "%010lld %05d n \n", w.off[n], w.gen[n]);
            else
                pdf_buf_printf(&w.out, "0000000000 65535 f \n");
        }

        tr = pdf_trailer(doc);
        pdf_buf_printf(&w.out, "trailer\n<< /Size %d", last + 1);
        for (i = 0; i < pdf_dict_len(tr); i++) {
            const char *k = pdf_dict_key(tr, i);

            if (!k) continue;
            if (!strcmp(k, "Size") || !strcmp(k, "Prev") || !strcmp(k, "XRefStm")
                || !strcmp(k, "Type") || !strcmp(k, "W") || !strcmp(k, "Index")
                || !strcmp(k, "Length") || !strcmp(k, "Filter")
                || !strcmp(k, "DecodeParms"))
                continue;
            /* 7.6: the product is not encrypted, so it names no handler. */
            if (!strcmp(k, "Encrypt")) continue;
            pdf_buf_putc(&w.out, ' ');
            w_name(&w, k);
            pdf_buf_putc(&w.out, ' ');
            w_obj(&w, pdf_dict_val_raw(tr, i), 0);
        }
        pdf_buf_put(&w.out, " >>\n", 4);
        pdf_buf_printf(&w.out, "startxref\n%lld\n%%%%EOF\n", xrefoff);
    }

    if (strcmp(path, "-") == 0) {
        if (w.out.n) fwrite(w.out.p, 1, w.out.n, stdout);
    } else {
        f = fopen(path, "wb");
        if (!f) { rc = PDF_E_IO; goto done; }
        if (w.out.n) fwrite(w.out.p, 1, w.out.n, f);
        fclose(f);
    }

done:
    pdf_buf_free(&w.out);
    free(w.off);
    free(w.gen);
    free(w.extra);
    return rc;
}
