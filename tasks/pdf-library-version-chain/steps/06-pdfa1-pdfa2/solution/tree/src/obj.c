/* obj.c -- object model, accessors, error text and the small encoders.
 *
 * Objects live in the owning document's arena and are never freed one at a
 * time, so containers may point at each other freely (/Parent, /P, /Prev) and
 * an absent value is reported as the one shared immutable null object rather
 * than NULL. Callers can therefore chain accessors without a check at every
 * step.
 */

#include "pdfint.h"
#include <stdint.h>

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

/* --------------------------------------------------------- the null object */

/* Shared by every document, never written to, never freed. */
static pdf_obj the_null = { .kind = PDF_NULL, .u = { .i = 0 } };

pdf_obj *pdf_null_obj(void)
{
    return &the_null;
}

/* ------------------------------------------------------------- predicates */

int pdf_is_null(const pdf_obj *o)   { return !o || o->kind == PDF_NULL; }
int pdf_is_int(const pdf_obj *o)    { return o && o->kind == PDF_INT; }
int pdf_is_num(const pdf_obj *o)    { return o && (o->kind == PDF_INT || o->kind == PDF_REAL); }
int pdf_is_array(const pdf_obj *o)  { return o && o->kind == PDF_ARRAY; }
int pdf_is_string(const pdf_obj *o) { return o && o->kind == PDF_STRING; }
int pdf_is_stream(const pdf_obj *o) { return o && o->kind == PDF_STREAM; }

/* A stream is a dictionary with data bolted on; every dictionary operation
 * works on it. */
int pdf_is_dict(const pdf_obj *o)
{
    return o && (o->kind == PDF_DICT || o->kind == PDF_STREAM);
}

int pdf_is_name(const pdf_obj *o, const char *name)
{
    if (!o || o->kind != PDF_NAME || !o->u.name.p)
        return 0;
    return name == NULL || strcmp(o->u.name.p, name) == 0;
}

/* -------------------------------------------------------------- accessors */

long long pdf_to_int(const pdf_obj *o)
{
    if (!o)
        return 0;
    if (o->kind == PDF_INT)
        return o->u.i;
    if (o->kind == PDF_REAL) {
        double v = o->u.real.v;
        if (!isfinite(v))
            return 0;
        if (v >= 9223372036854775808.0)         /* 2^63 */
            return LLONG_MAX;
        if (v <= -9223372036854775808.0)
            return LLONG_MIN;
        return (long long)v;
    }
    return 0;
}

double pdf_to_real(const pdf_obj *o)
{
    if (!o)
        return 0.0;
    if (o->kind == PDF_REAL)
        return isfinite(o->u.real.v) ? o->u.real.v : 0.0;
    if (o->kind == PDF_INT)
        return (double)o->u.i;
    return 0.0;
}

const char *pdf_to_name(const pdf_obj *o)
{
    if (!o || o->kind != PDF_NAME || !o->u.name.p)
        return "";
    return o->u.name.p;
}

const char *pdf_to_string(const pdf_obj *o, size_t *len)
{
    if (!o || o->kind != PDF_STRING || !o->u.str.p) {
        if (len)
            *len = 0;
        return "";
    }
    if (len)
        *len = o->u.str.n;
    return o->u.str.p;
}

/* ----------------------------------------------------------- constructors */

static pdf_obj *obj_new(pdf_doc *doc, pdf_kind kind)
{
    pdf_obj *o = pdf_alloc(doc, sizeof *o);
    if (!o)
        return NULL;
    memset(o, 0, sizeof *o);
    o->kind = kind;
    return o;
}

pdf_obj *pdf_new_bool(pdf_doc *doc, int v)
{
    pdf_obj *o = obj_new(doc, PDF_BOOL);
    if (!o)
        return pdf_null_obj();
    o->u.b = v ? 1 : 0;
    return o;
}

pdf_obj *pdf_new_int(pdf_doc *doc, long long v)
{
    pdf_obj *o = obj_new(doc, PDF_INT);
    if (!o)
        return pdf_null_obj();
    o->u.i = v;
    return o;
}

pdf_obj *pdf_new_real(pdf_doc *doc, double v)
{
    pdf_obj *o = obj_new(doc, PDF_REAL);
    if (!o)
        return pdf_null_obj();
    o->u.real.v = isfinite(v) ? v : 0.0;
    return o;
}

/* The literal is copied into the arena, so the caller's buffer need not
 * outlive the call.  A copy that cannot be made is simply not remembered: the
 * value is still right, and a printer falls back on formatting it. */
pdf_obj *pdf_new_real_lit(pdf_doc *doc, double v, const char *lit, size_t n)
{
    pdf_obj *o = pdf_new_real(doc, v);

    if (o && o->kind == PDF_REAL && lit != NULL && n > 0)
        o->u.real.lit = pdf_strdup_n(doc, lit, n);
    return o;
}

pdf_obj *pdf_new_string(pdf_doc *doc, const char *p, size_t n)
{
    pdf_obj *o;
    char *copy;

    if (!p)
        n = 0;
    copy = pdf_strdup_n(doc, p, n);         /* NUL terminated for convenience */
    if (!copy)
        return pdf_null_obj();
    o = obj_new(doc, PDF_STRING);
    if (!o)
        return pdf_null_obj();
    o->u.str.p = copy;
    o->u.str.n = n;
    return o;
}

pdf_obj *pdf_new_name(pdf_doc *doc, const char *name)
{
    pdf_obj *o;
    char *copy;

    if (!name)
        name = "";
    copy = pdf_strdup_n(doc, name, strlen(name));
    if (!copy)
        return pdf_null_obj();
    o = obj_new(doc, PDF_NAME);
    if (!o)
        return pdf_null_obj();
    o->u.name.p = copy;
    return o;
}

pdf_obj *pdf_new_array(pdf_doc *doc)
{
    pdf_obj *o = obj_new(doc, PDF_ARRAY);
    return o ? o : pdf_null_obj();
}

pdf_obj *pdf_new_dict(pdf_doc *doc)
{
    pdf_obj *o = obj_new(doc, PDF_DICT);
    return o ? o : pdf_null_obj();
}

pdf_obj *pdf_new_ref(pdf_doc *doc, int num, int gen)
{
    pdf_obj *o = obj_new(doc, PDF_REF);
    if (!o)
        return pdf_null_obj();
    o->u.ref.num = num;
    o->u.ref.gen = gen;
    return o;
}

/* ----------------------------------------------------------------- arrays */

int pdf_array_push(pdf_doc *doc, pdf_obj *arr, pdf_obj *item)
{
    if (!arr || arr->kind != PDF_ARRAY)
        return PDF_E_RANGE;
    if (!item)
        item = pdf_null_obj();

    if (arr->u.arr.n == arr->u.arr.cap) {
        size_t ncap = arr->u.arr.cap ? arr->u.arr.cap * 2 : 8;
        pdf_obj **nv;
        if (ncap < arr->u.arr.cap || ncap > SIZE_MAX / sizeof(pdf_obj *))
            return PDF_E_NOMEM;
        nv = pdf_alloc(doc, ncap * sizeof(pdf_obj *));
        if (!nv)
            return PDF_E_NOMEM;
        if (arr->u.arr.n)
            memcpy(nv, arr->u.arr.v, arr->u.arr.n * sizeof(pdf_obj *));
        arr->u.arr.v = nv;              /* old vector stays in the arena */
        arr->u.arr.cap = ncap;
    }
    arr->u.arr.v[arr->u.arr.n++] = item;
    return 0;
}

size_t pdf_array_len(const pdf_obj *arr)
{
    return (arr && arr->kind == PDF_ARRAY) ? arr->u.arr.n : 0;
}

pdf_obj *pdf_array_get_raw(const pdf_obj *arr, size_t i)
{
    if (!arr || arr->kind != PDF_ARRAY || i >= arr->u.arr.n || !arr->u.arr.v)
        return pdf_null_obj();
    return arr->u.arr.v[i] ? arr->u.arr.v[i] : pdf_null_obj();
}

/* ----------------------------------------------------------- dictionaries */

/* Reach the PDF_DICT behind o, looking inside a stream when needed. */
static pdf_obj *dict_of(const pdf_obj *o)
{
    if (!o)
        return NULL;
    if (o->kind == PDF_DICT)
        return (pdf_obj *)o;
    if (o->kind == PDF_STREAM && o->u.stm.dict && o->u.stm.dict->kind == PDF_DICT)
        return o->u.stm.dict;
    return NULL;
}

static size_t dict_find(const pdf_obj *d, const char *key)
{
    size_t i;
    for (i = 0; i < d->u.dict.n; i++)
        if (d->u.dict.e[i].key && strcmp(d->u.dict.e[i].key, key) == 0)
            return i;
    return (size_t)-1;
}

int pdf_dict_put(pdf_doc *doc, pdf_obj *dict, const char *key, pdf_obj *val)
{
    pdf_obj *d = dict_of(dict);
    size_t at;
    char *k;

    if (!d || !key)
        return PDF_E_RANGE;
    if (!val)
        val = pdf_null_obj();

    at = dict_find(d, key);
    if (at != (size_t)-1) {             /* replace in place, order preserved */
        d->u.dict.e[at].val = val;
        return 0;
    }

    if (d->u.dict.n == d->u.dict.cap) {
        size_t ncap = d->u.dict.cap ? d->u.dict.cap * 2 : 8;
        pdf_dictent *ne;
        if (ncap < d->u.dict.cap || ncap > SIZE_MAX / sizeof(pdf_dictent))
            return PDF_E_NOMEM;
        ne = pdf_alloc(doc, ncap * sizeof(pdf_dictent));
        if (!ne)
            return PDF_E_NOMEM;
        if (d->u.dict.n)
            memcpy(ne, d->u.dict.e, d->u.dict.n * sizeof(pdf_dictent));
        d->u.dict.e = ne;
        d->u.dict.cap = ncap;
    }
    k = pdf_strdup_n(doc, key, strlen(key));
    if (!k)
        return PDF_E_NOMEM;
    d->u.dict.e[d->u.dict.n].key = k;
    d->u.dict.e[d->u.dict.n].val = val;
    d->u.dict.n++;
    return 0;
}

void pdf_dict_del(pdf_obj *dict, const char *key)
{
    pdf_obj *d = dict_of(dict);
    size_t at;

    if (!d || !key)
        return;
    at = dict_find(d, key);
    if (at == (size_t)-1)
        return;
    if (at + 1 < d->u.dict.n)
        memmove(&d->u.dict.e[at], &d->u.dict.e[at + 1],
                (d->u.dict.n - at - 1) * sizeof(pdf_dictent));
    d->u.dict.n--;
}

size_t pdf_dict_len(const pdf_obj *dict)
{
    pdf_obj *d = dict_of(dict);
    return d ? d->u.dict.n : 0;
}

const char *pdf_dict_key(const pdf_obj *dict, size_t i)
{
    pdf_obj *d = dict_of(dict);
    if (!d || i >= d->u.dict.n || !d->u.dict.e)
        return NULL;
    return d->u.dict.e[i].key;
}

pdf_obj *pdf_dict_val_raw(const pdf_obj *dict, size_t i)
{
    pdf_obj *d = dict_of(dict);
    if (!d || i >= d->u.dict.n || !d->u.dict.e || !d->u.dict.e[i].val)
        return pdf_null_obj();
    return d->u.dict.e[i].val;
}

pdf_obj *pdf_dict_get_raw(const pdf_obj *dict, const char *key)
{
    pdf_obj *d = dict_of(dict);
    size_t at;

    if (!d || !key)
        return pdf_null_obj();
    at = dict_find(d, key);
    if (at == (size_t)-1 || !d->u.dict.e[at].val)
        return pdf_null_obj();
    return d->u.dict.e[at].val;
}

/* ------------------------------------------------------------ error state */

void pdf_set_error(pdf_doc *doc, const char *fmt, ...)
{
    va_list ap;

    if (!doc)
        return;
    if (!fmt) {
        doc->err[0] = '\0';
        return;
    }
    va_start(ap, fmt);
    vsnprintf(doc->err, sizeof doc->err, fmt, ap);
    va_end(ap);
    doc->err[sizeof doc->err - 1] = '\0';
}

const char *pdf_error(pdf_doc *doc)
{
    if (!doc)
        return "no document";
    doc->err[sizeof doc->err - 1] = '\0';
    return doc->err;
}

int pdf_damaged(pdf_doc *doc)
{
    return doc ? doc->damaged : 0;
}

/* ------------------------------------------------------------- PDFDocEnc. */

/* ISO 32000-1 Annex D.2. Latin-1 apart from 0x18..0x1F (which hold the
 * accents Latin-1 leaves as C1 controls), 0x80..0x9F (typography, where
 * Latin-1 has C1 controls) and 0xA0 (EURO SIGN, not NO-BREAK SPACE).
 * 0x7F and 0x9F are undefined and become U+FFFD. */
static const unsigned short pdfdocenc[256] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x02d8, 0x02c7, 0x02c6, 0x02d9, 0x02dd, 0x02db, 0x02da, 0x02dc,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0xfffd,
    0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044,
    0x2039, 0x203a, 0x2212, 0x2030, 0x201e, 0x201c, 0x201d, 0x2018,
    0x2019, 0x201a, 0x2122, 0xfb01, 0xfb02, 0x0141, 0x0152, 0x0160,
    0x0178, 0x017d, 0x0131, 0x0142, 0x0153, 0x0161, 0x017e, 0xfffd,
    0x20ac, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
    0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
    0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
    0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff
};

/* ----------------------------------------------------------- UTF-8 output */

int pdf_utf8_put(pdf_buf *b, unsigned int cp)
{
    int rc;

    if (!b)
        return PDF_E_RANGE;
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        cp = 0xFFFDu;                   /* never emit surrogates or > U+10FFFF */

    if (cp < 0x80u)
        return pdf_buf_putc(b, (int)cp);
    if (cp < 0x800u) {
        if ((rc = pdf_buf_putc(b, (int)(0xC0u | (cp >> 6)))) != 0) return rc;
        return pdf_buf_putc(b, (int)(0x80u | (cp & 0x3Fu)));
    }
    if (cp < 0x10000u) {
        if ((rc = pdf_buf_putc(b, (int)(0xE0u | (cp >> 12)))) != 0) return rc;
        if ((rc = pdf_buf_putc(b, (int)(0x80u | ((cp >> 6) & 0x3Fu)))) != 0) return rc;
        return pdf_buf_putc(b, (int)(0x80u | (cp & 0x3Fu)));
    }
    if ((rc = pdf_buf_putc(b, (int)(0xF0u | (cp >> 18)))) != 0) return rc;
    if ((rc = pdf_buf_putc(b, (int)(0x80u | ((cp >> 12) & 0x3Fu)))) != 0) return rc;
    if ((rc = pdf_buf_putc(b, (int)(0x80u | ((cp >> 6) & 0x3Fu)))) != 0) return rc;
    return pdf_buf_putc(b, (int)(0x80u | (cp & 0x3Fu)));
}

/* Re-encode declared-UTF-8 text, replacing anything malformed with U+FFFD so
 * the output is always well formed even when the file is not. */
static int utf8_clean(const unsigned char *s, size_t n, pdf_buf *out)
{
    size_t i = 0;
    int rc;

    while (i < n) {
        unsigned int cp, c = s[i];
        int extra, j, ok = 1;

        if (c < 0x80u)            { cp = c;          extra = 0; }
        else if ((c & 0xE0u) == 0xC0u) { cp = c & 0x1Fu; extra = 1; }
        else if ((c & 0xF0u) == 0xE0u) { cp = c & 0x0Fu; extra = 2; }
        else if ((c & 0xF8u) == 0xF0u) { cp = c & 0x07u; extra = 3; }
        else {
            if ((rc = pdf_utf8_put(out, 0xFFFDu)) != 0) return rc;
            i++;
            continue;
        }
        if (n - i <= (size_t)extra) {               /* truncated at the end */
            if ((rc = pdf_utf8_put(out, 0xFFFDu)) != 0) return rc;
            break;
        }
        for (j = 1; j <= extra; j++) {
            unsigned int cc = s[i + (size_t)j];
            if ((cc & 0xC0u) != 0x80u) { ok = 0; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!ok) {
            if ((rc = pdf_utf8_put(out, 0xFFFDu)) != 0) return rc;
            i++;
            continue;
        }
        if ((extra == 1 && cp < 0x80u) ||           /* overlong encodings */
            (extra == 2 && cp < 0x800u) ||
            (extra == 3 && cp < 0x10000u))
            cp = 0xFFFDu;
        if ((rc = pdf_utf8_put(out, cp)) != 0) return rc;
        i += (size_t)extra + 1;
    }
    return 0;
}

/* ISO 32000-1 7.9.2.2 / ISO 32000-2 7.9.2.2: a text string is UTF-16BE when it
 * starts FE FF, UTF-8 when it starts EF BB BF (PDF 2.0), else PDFDocEncoding.
 * Output is appended to *out. */
int pdf_text_string_to_utf8(const char *s, size_t n, pdf_buf *out)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t i;
    int rc;

    if (!out)
        return PDF_E_RANGE;
    if (!s || n == 0)
        return 0;

    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        for (i = 2; i + 1 < n; i += 2) {
            unsigned int w = ((unsigned int)p[i] << 8) | p[i + 1];
            unsigned int cp = w;
            if (w >= 0xD800u && w <= 0xDBFFu) {             /* high surrogate */
                if (i + 3 < n) {
                    unsigned int w2 = ((unsigned int)p[i + 2] << 8) | p[i + 3];
                    if (w2 >= 0xDC00u && w2 <= 0xDFFFu) {
                        cp = 0x10000u + ((w - 0xD800u) << 10) + (w2 - 0xDC00u);
                        i += 2;
                    } else {
                        cp = 0xFFFDu;           /* unpaired: keep w2 for later */
                    }
                } else {
                    cp = 0xFFFDu;
                }
            } else if (w >= 0xDC00u && w <= 0xDFFFu) {
                cp = 0xFFFDu;                              /* stray low half */
            }
            if ((rc = pdf_utf8_put(out, cp)) != 0)
                return rc;
        }
        return 0;
    }

    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        return utf8_clean(p + 3, n - 3, out);

    for (i = 0; i < n; i++)
        if ((rc = pdf_utf8_put(out, pdfdocenc[p[i]])) != 0)
            return rc;
    return 0;
}

/* ------------------------------------------------------------ real output */

/* PDF has no exponent notation (ISO 32000-1 7.3.3), so print in plain decimal
 * with just enough fraction digits to read back as the same double, then trim.
 * Non-finite values become 0, and so does negative zero. */
int pdf_fmt_real(pdf_buf *b, double v)
{
    char tmp[512];
    int prec, len = 0, i, nonzero = 0;

    if (!b)
        return PDF_E_RANGE;
    if (!isfinite(v))
        v = 0.0;
    if (v == 0.0)                       /* catches -0.0 too */
        return pdf_buf_put(b, "0", 1);

    for (prec = 0; prec <= 9; prec++) {
        len = snprintf(tmp, sizeof tmp, "%.*f", prec, v);
        if (len < 0)
            return PDF_E_RANGE;
        if ((size_t)len >= sizeof tmp) {        /* cannot happen for a double */
            tmp[sizeof tmp - 1] = '\0';
            len = (int)strlen(tmp);
            break;
        }
        if (prec == 9 || strtod(tmp, NULL) == v)
            break;
    }

    for (i = 0; i < len; i++)           /* a comma locale would break the file */
        if (tmp[i] == ',')
            tmp[i] = '.';

    if (memchr(tmp, '.', (size_t)len)) {
        while (len > 0 && tmp[len - 1] == '0')
            len--;
        if (len > 0 && tmp[len - 1] == '.')
            len--;
    }
    for (i = 0; i < len; i++)
        if (tmp[i] >= '1' && tmp[i] <= '9')
            nonzero = 1;
    if (!nonzero) {                     /* "-0", "0.000", ... */
        tmp[0] = '0';
        len = 1;
    }
    return pdf_buf_put(b, tmp, (size_t)len);
}
