/* qpdf.c -- show a single PDF object, matching qpdf 11.3.0's --show-object.
 *
 *   qpdf --show-object=N[,G] [--filtered-stream-data|--raw-stream-data] FILE
 *
 * Serialization follows qpdf's unparse() methods (libqpdf/QPDF_*.cc):
 *   dict   "<< " + entries + ">>", keys sorted, entries that resolve to null
 *          are omitted entirely (this includes dangling indirect references)
 *   array  "[ " + each element + " " + "]"      (nulls are kept in arrays)
 *   ref    "N G R"          bool "true"/"false"          null "null"
 *   name   "/" + chars, escaping <33, >126 and ()<>[]{}/% as lowercase #xx
 *   string literal "(...)" unless QPDF_String::useHexString() says otherwise,
 *          in which case lowercase "<hex>"
 * Exit status: 0 on success, 2 on error.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "pdf.h"

#define MAX_DEPTH 500

static const char hexdigits[] = "0123456789abcdef";

static void emit_obj(pdf_doc *doc, pdf_obj *o, pdf_buf *b, int depth);

static void usage_error(const char *msg, const char *arg)
{
    fprintf(stderr, "\nqpdf: %s%s\n\n", msg, arg != NULL ? arg : "");
    fprintf(stderr, "For help:\n");
    fprintf(stderr, "  qpdf --help=usage       usage information\n");
    fprintf(stderr, "  qpdf --help=topic       help on a topic\n");
    fprintf(stderr, "  qpdf --help=--option    help on an option\n");
    fprintf(stderr, "  qpdf --help             general help and a topic list\n");
}

/* QPDF_String::useHexString().  Note qpdf iterates over `char`, which is
 * signed on the platforms we care about: bytes >= 128 therefore compare as
 * negative and are counted through the `ch < 0` arm. */
static int str_use_hex(const char *p, size_t n)
{
    size_t i;
    size_t non_ascii = 0;

    for (i = 0; i < n; i++) {
        signed char ch = (signed char)p[i];

        if (ch > 126)
            non_ascii++;
        else if (ch >= 32)
            continue;
        else if (ch < 0 || ch >= 24)
            non_ascii++;
        else if (!(ch == '\n' || ch == '\r' || ch == '\t' || ch == '\b'
                   || ch == '\f'))
            return 1;
    }
    return 5 * non_ascii > n;
}

/* qpdf's is_iso_latin1_printable(). */
static int latin1_printable(char ch)
{
    return ((ch >= 32 && ch <= 126) || (unsigned char)ch >= 160);
}

static void emit_string(const char *p, size_t n, pdf_buf *b)
{
    size_t i;

    if (str_use_hex(p, n)) {
        pdf_buf_putc(b, '<');
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)p[i];
            pdf_buf_putc(b, hexdigits[c >> 4]);
            pdf_buf_putc(b, hexdigits[c & 0x0f]);
        }
        pdf_buf_putc(b, '>');
        return;
    }

    pdf_buf_putc(b, '(');
    for (i = 0; i < n; i++) {
        char ch = p[i];

        switch (ch) {
        case '\n': pdf_buf_put(b, "\\n", 2); break;
        case '\r': pdf_buf_put(b, "\\r", 2); break;
        case '\t': pdf_buf_put(b, "\\t", 2); break;
        case '\b': pdf_buf_put(b, "\\b", 2); break;
        case '\f': pdf_buf_put(b, "\\f", 2); break;
        case '(':  pdf_buf_put(b, "\\(", 2); break;
        case ')':  pdf_buf_put(b, "\\)", 2); break;
        case '\\': pdf_buf_put(b, "\\\\", 2); break;
        default:
            if (latin1_printable(ch))
                pdf_buf_putc(b, ch);
            else
                pdf_buf_printf(b, "\\%03o", (unsigned)(unsigned char)ch);
            break;
        }
    }
    pdf_buf_putc(b, ')');
}

/* QPDF_Name::normalizeName(), applied to the name without its leading '/'. */
static void emit_name(const char *nm, pdf_buf *b)
{
    size_t i;

    pdf_buf_putc(b, '/');
    if (nm == NULL)
        return;
    for (i = 0; nm[i] != '\0'; i++) {
        unsigned char c = (unsigned char)nm[i];

        if (c < 33 || c > 126 || c == '/' || c == '(' || c == ')'
            || c == '{' || c == '}' || c == '<' || c == '>' || c == '['
            || c == ']' || c == '%') {
            pdf_buf_putc(b, '#');
            pdf_buf_putc(b, hexdigits[c >> 4]);
            pdf_buf_putc(b, hexdigits[c & 0x0f]);
        } else {
            pdf_buf_putc(b, (char)c);
        }
    }
}

typedef struct { const char *k; pdf_obj *v; } dict_ent;

/* std::map<std::string,...> ordering: unsigned byte-wise comparison. */
static int dict_ent_cmp(const void *A, const void *B)
{
    const unsigned char *p = (const unsigned char *)((const dict_ent *)A)->k;
    const unsigned char *q = (const unsigned char *)((const dict_ent *)B)->k;

    while (*p != '\0' && *p == *q) {
        p++;
        q++;
    }
    return (int)*p - (int)*q;
}

static void emit_dict(pdf_doc *doc, pdf_obj *d, pdf_buf *b, int depth)
{
    size_t n;
    size_t i;
    size_t m = 0;
    dict_ent *ents;

    pdf_buf_put(b, "<< ", 3);
    n = pdf_dict_len(d);
    if (n > 0 && (ents = malloc(n * sizeof *ents)) != NULL) {
        for (i = 0; i < n; i++) {
            pdf_obj *v = pdf_dict_val_raw(d, i);
            const char *k = pdf_dict_key(d, i);

            /* qpdf checks isNull() on the handle, which resolves indirect
             * references first; a reference to a missing or null object is
             * therefore dropped along with a direct null. */
            if (k == NULL || pdf_is_null(pdf_resolve(doc, v)))
                continue;
            ents[m].k = k;
            ents[m].v = v;
            m++;
        }
        qsort(ents, m, sizeof *ents, dict_ent_cmp);
        for (i = 0; i < m; i++) {
            emit_name(ents[i].k, b);
            pdf_buf_putc(b, ' ');
            emit_obj(doc, ents[i].v, b, depth + 1);
            pdf_buf_putc(b, ' ');
        }
        free(ents);
    }
    pdf_buf_put(b, ">>", 2);
}

static void emit_obj(pdf_doc *doc, pdf_obj *o, pdf_buf *b, int depth)
{
    const char *s;
    size_t slen;
    size_t i;
    size_t n;

    if (o == NULL || depth > MAX_DEPTH) {
        pdf_buf_put(b, "null", 4);
        return;
    }

    switch (o->kind) {
    case PDF_NULL:
        pdf_buf_put(b, "null", 4);
        break;
    case PDF_BOOL:
        if (o->u.b)
            pdf_buf_put(b, "true", 4);
        else
            pdf_buf_put(b, "false", 5);
        break;
    case PDF_INT:
        pdf_buf_printf(b, "%lld", (long long)o->u.i);
        break;
    case PDF_REAL:
        /* QPDF_Real keeps the string it was parsed from and unparse() hands
         * that string straight back, so a real is reproduced exactly as the
         * file spelled it: ".25" stays ".25", "2.50" keeps its trailing zero
         * and "1.0" does not collapse to "1".  Only a real this library built
         * itself has no spelling to reproduce; that one is formatted. */
        if (o->u.real.lit != NULL)
            pdf_buf_put(b, o->u.real.lit, strlen(o->u.real.lit));
        else
            pdf_fmt_real(b, o->u.real.v);
        break;
    case PDF_STRING:
        s = pdf_to_string(o, &slen);
        emit_string(s != NULL ? s : "", s != NULL ? slen : 0, b);
        break;
    case PDF_NAME:
        emit_name(pdf_to_name(o), b);
        break;
    case PDF_ARRAY:
        pdf_buf_put(b, "[ ", 2);
        n = pdf_array_len(o);
        for (i = 0; i < n; i++) {
            emit_obj(doc, pdf_array_get_raw(o, i), b, depth + 1);
            pdf_buf_putc(b, ' ');
        }
        pdf_buf_putc(b, ']');
        break;
    case PDF_DICT:
        emit_dict(doc, o, b, depth);
        break;
    case PDF_STREAM:
        emit_dict(doc, o->u.stm.dict, b, depth);
        break;
    case PDF_REF:
        pdf_buf_printf(b, "%d %d R", o->u.ref.num, o->u.ref.gen);
        break;
    default:
        pdf_buf_put(b, "null", 4);
        break;
    }
}

/* ------------------------------------------------------------- PDF 1.4 modes
 *
 * qpdf 11.3.0's --check, --show-xref, --show-pages and --show-linearization.
 * Three conventions hold across all four, taken from the reference tool's own
 * output rather than from its source:
 *
 *   - the report goes to standard output; every diagnostic is a "WARNING: "
 *     line on standard error followed by one "qpdf: operation succeeded with
 *     warnings", and the exit status is then 3.  Exit 0 means silence.
 *   - a file is named in a diagnostic exactly as it was named on the command
 *     line, never in a canonical form.
 *   - nothing is written to stderr until the report is complete, so the two
 *     streams are never interleaved.
 */

#define MODE_SHOW_OBJECT 0
#define MODE_CHECK       1
#define MODE_SHOW_XREF   2
#define MODE_SHOW_PAGES  3
#define MODE_SHOW_LIN    4

#define XREF_UNCOMPRESSED 1     /* pdf_xref_entry()'s *type for a plain object */
#define MAX_WARN          32
#define LIN_MAX_BITS      32    /* a field width a hint table may ask us for   */

static char warnings[MAX_WARN][512];
static int  nwarnings;

static void add_warning(const char *fmt, ...)
{
    va_list ap;

    if (nwarnings >= MAX_WARN)
        return;
    va_start(ap, fmt);
    vsnprintf(warnings[nwarnings], sizeof warnings[0], fmt, ap);
    va_end(ap);
    nwarnings++;
}

static int flush_warnings(void)
{
    int i;

    for (i = 0; i < nwarnings; i++)
        fprintf(stderr, "WARNING: %s\n", warnings[i]);
    if (nwarnings == 0)
        return 0;
    fprintf(stderr, "qpdf: operation succeeded with warnings\n");
    return 3;
}

/* 7.5.5.  A /Prev that points nowhere is papered over by the loader, which
 * prefers recovery to refusal, so the three warnings below are the only report
 * that the chain was broken at all.  Reconstruction is then forced so that the
 * rest of the command describes the file that is really there. */
static int chain_damaged;

static void report_damage(pdf_doc *doc, const char *path)
{
    long long badoff = 0;
    int how = pdf_xref_chain_check(doc, &badoff);

    if (!how)
        return;
    chain_damaged = 1;
    add_warning("%s: file is damaged", path);
    /* Two ways to have no usable chain, and the reference tool names them
     * apart: a hop that lands on nothing is reported against the offset that
     * was promised, while a startxref pointing at offset 0 is reported as
     * having no startxref at all -- there is no offset worth quoting. */
    if (how == PDF_XREF_CHAIN_NOSTART)
        add_warning("%s: can't find startxref", path);
    else
        add_warning("%s (offset %lld): xref not found", path, badoff);
    add_warning("%s: Attempting to reconstruct cross-reference table", path);
    pdf_xref_force_reconstruct(doc);
}

/* ------------------------------------------------------------ linearization */

/* Annex F.  The parameter dictionary is the first object in the file, and a
 * reader must disbelieve it unless /L is the real file length.  That is the
 * whole of the cheap test, and it is why a file whose /L is wrong is not
 * "broken" but simply indistinguishable from one that was never linearized. */
typedef struct {
    int       found;            /* a first object carrying /Linearized        */
    int       valid;            /* ... whose /L matches the file length       */
    int       objnum;
    long long L, O, E, N, T, P, H0, H1;
} lin_data;

static int lin_first_objnum(pdf_doc *doc)
{
    long long best_off = 0, off = 0;
    int n, best = -1, type = 0, gen = 0;

    for (n = 1; n <= pdf_max_objnum(doc); n++) {
        if (!pdf_xref_entry(doc, n, &type, &off, &gen))
            continue;
        if (type != XREF_UNCOMPRESSED)
            continue;
        if (best < 0 || off < best_off) {
            best = n;
            best_off = off;
        }
    }
    return best;
}

static void lin_read(pdf_doc *doc, lin_data *L)
{
    pdf_obj *o, *h;

    memset(L, 0, sizeof *L);
    L->objnum = lin_first_objnum(doc);
    if (L->objnum < 0)
        return;
    o = pdf_get_object(doc, L->objnum, 0);
    if (!pdf_is_dict(o) || pdf_is_stream(o))
        return;
    if (pdf_is_null(pdf_dict_get(doc, o, "Linearized")))
        return;

    L->found = 1;
    L->L = pdf_to_int(pdf_dict_get(doc, o, "L"));
    L->O = pdf_to_int(pdf_dict_get(doc, o, "O"));
    L->E = pdf_to_int(pdf_dict_get(doc, o, "E"));
    L->N = pdf_to_int(pdf_dict_get(doc, o, "N"));
    L->T = pdf_to_int(pdf_dict_get(doc, o, "T"));
    L->P = pdf_to_int(pdf_dict_get(doc, o, "P"));
    h = pdf_dict_get(doc, o, "H");
    if (pdf_is_array(h) && pdf_array_len(h) >= 2) {
        L->H0 = pdf_to_int(pdf_array_get(doc, h, 0));
        L->H1 = pdf_to_int(pdf_array_get(doc, h, 1));
    }
    L->valid = (L->L == pdf_file_size(doc));
}

/* The hint stream is named by byte offset, so it is checked as bytes: anything
 * that is not an object header exactly there fails.  An /H offset that is one
 * byte late lands on the space after the object number, which is precisely the
 * breakage worth catching. */
static int lin_hint_objnum(pdf_doc *doc, long long off, int *gen)
{
    const unsigned char *p;
    size_t len, i;
    long long num = 0, g = 0;

    if (gen) *gen = 0;
    p = pdf_file_bytes(doc, &len);
    if (!p || off < 0 || (unsigned long long)off >= (unsigned long long)len)
        return -1;

    i = (size_t)off;
    if (p[i] < '0' || p[i] > '9')
        return -1;
    while (i < len && p[i] >= '0' && p[i] <= '9')
        num = num * 10 + (p[i++] - '0');
    if (i >= len || !(p[i] == ' ' || p[i] == '\r' || p[i] == '\n' || p[i] == '\t'))
        return -1;
    while (i < len && (p[i] == ' ' || p[i] == '\r' || p[i] == '\n' || p[i] == '\t'))
        i++;
    if (i >= len || p[i] < '0' || p[i] > '9')
        return -1;
    while (i < len && p[i] >= '0' && p[i] <= '9')
        g = g * 10 + (p[i++] - '0');
    while (i < len && (p[i] == ' ' || p[i] == '\r' || p[i] == '\n' || p[i] == '\t'))
        i++;
    if (len - i < 3 || memcmp(p + i, "obj", 3) != 0)
        return -1;
    if (num <= 0 || num > 8388607)
        return -1;
    if (gen) *gen = (int)g;
    return (int)num;
}

/* qpdf reports the end of the first-page section as a RANGE, because the last
 * object of that section is followed by whitespace before the next one starts.
 * The upper bound is the offset of the object that begins the rest of the file;
 * the lower bound is the last non-blank byte before it.  qpdf's linearizer
 * renumbers the second half below the first page's own object number, so that
 * boundary is the first object numbered below /O that lies after it. */
static int lin_first_page_end(pdf_doc *doc, const lin_data *L,
                              long long *lo, long long *hi)
{
    const unsigned char *p;
    size_t len;
    long long o_off = 0, best = 0, off = 0;
    int n, type = 0, gen = 0, have = 0;

    p = pdf_file_bytes(doc, &len);
    if (!p || L->O <= 0)
        return 0;
    if (!pdf_xref_entry(doc, (int)L->O, &type, &o_off, &gen))
        return 0;

    for (n = 1; n <= pdf_max_objnum(doc); n++) {
        if ((long long)n >= L->O)
            continue;
        if (!pdf_xref_entry(doc, n, &type, &off, &gen))
            continue;
        if (off <= o_off)
            continue;
        if (!have || off < best) {
            best = off;
            have = 1;
        }
    }
    if (!have)
        return 0;

    *hi = best;
    off = best;
    while (off > 0 && (unsigned long long)off <= (unsigned long long)len) {
        unsigned char c = p[off - 1];

        if (c != ' ' && c != '\r' && c != '\n' && c != '\t' && c != '\f')
            break;
        off--;
    }
    *lo = off;
    return 1;
}

/* Where qpdf says it was reading when /N overran the pages that exist: the
 * object just past the last real page. */
static long long lin_overrun_offset(pdf_doc *doc)
{
    long long off = 0;
    int npages = pdf_page_count(doc), num, type = 0, gen = 0;

    if (npages <= 0)
        return 0;
    num = pdf_page_objnum(doc, npages - 1, &gen);
    if (num < 0)
        return 0;
    if (!pdf_xref_entry(doc, num + 1, &type, &off, &gen))
        return 0;
    return off;
}

/* Annex F.2.5, in the order the reference tool stops at: an unreadable hint
 * stream hides everything behind it, and only one warning is ever produced. */
static void lin_check(pdf_doc *doc, const lin_data *L, const char *path,
                      const char *prefix)
{
    long long lo = 0, hi = 0;
    int gen = 0, first;

    if (lin_hint_objnum(doc, L->H0, &gen) < 0) {
        add_warning("%s: %s%s (linearization hint stream, offset %lld): "
                    "expected n n obj", path, prefix, path, L->H0);
        return;
    }
    if (L->N != (long long)pdf_page_count(doc)) {
        add_warning("%s: %s%s (linearization hint table, offset %lld): "
                    "/N does not match number of pages",
                    path, prefix, path, lin_overrun_offset(doc));
        return;
    }
    first = pdf_page_objnum(doc, 0, &gen);
    if (first >= 0 && (long long)first != L->O) {
        add_warning("%s: first page object (/O) mismatch", path);
        return;
    }
    if (lin_first_page_end(doc, L, &lo, &hi) && (L->E < lo || L->E > hi))
        add_warning("%s: end of first page section (/E) mismatch: "
                    "/E = %lld; computed = %lld..%lld", path, L->E, lo, hi);
}

/* --------------------------------------------------------- hint table dump */

typedef struct { const unsigned char *p; size_t n, bit; } lin_bits;

static long long lin_bits_get(lin_bits *b, long long nbits)
{
    long long v = 0, i;

    if (nbits <= 0 || nbits > LIN_MAX_BITS)
        return 0;
    for (i = 0; i < nbits; i++) {
        size_t byte = b->bit >> 3;
        int sh = 7 - (int)(b->bit & 7);

        v = (v << 1) | (long long)((byte < b->n) ? ((b->p[byte] >> sh) & 1) : 0);
        b->bit++;
    }
    return v;
}

static long long lin_be(const unsigned char *p, size_t n, size_t off, int width)
{
    long long v = 0;
    int i;

    for (i = 0; i < width; i++)
        v = (v << 8) | (long long)(off + (size_t)i < n ? p[off + (size_t)i] : 0);
    return v;
}

/* Table F.3 and Table F.5.  The two table headers are fixed-width big-endian
 * fields; everything after them is a bit stream whose field widths the header
 * itself supplies. */
static void lin_show_hints(pdf_doc *doc, const lin_data *L, int hintnum,
                           int hintgen)
{
    pdf_obj *stm = pdf_get_object(doc, hintnum, hintgen);
    pdf_buf d;
    lin_bits b;
    const unsigned char *po, *sh;
    size_t pon, shn, S;
    long long minobj, fpoff, nb_dobj, minplen, nb_dplen, mincoff, nb_dcoff;
    long long minclen, nb_dclen, nb_nsh, nb_shid, nb_shnum, shden;
    long long fsobj, fsoff, nsfp, nstot, nb_nobj, mingl, nb_dgl;
    int i, k, npages;

    if (!pdf_is_stream(stm))
        return;
    pdf_buf_init(&d);
    if (pdf_stream_data(doc, stm, &d, 1) < 0 || d.n < 36) {
        pdf_buf_free(&d);
        return;
    }
    S = (size_t)pdf_to_int(pdf_dict_get(doc, stm, "S"));
    if (S > d.n)
        S = d.n;
    po = d.p;
    pon = S;
    sh = d.p + S;
    shn = d.n - S;

    minobj   = lin_be(po, pon,  0, 4);
    fpoff    = lin_be(po, pon,  4, 4);
    nb_dobj  = lin_be(po, pon,  8, 2);
    minplen  = lin_be(po, pon, 10, 4);
    nb_dplen = lin_be(po, pon, 14, 2);
    mincoff  = lin_be(po, pon, 16, 4);
    nb_dcoff = lin_be(po, pon, 20, 2);
    minclen  = lin_be(po, pon, 22, 4);
    nb_dclen = lin_be(po, pon, 26, 2);
    nb_nsh   = lin_be(po, pon, 28, 2);
    nb_shid  = lin_be(po, pon, 30, 2);
    nb_shnum = lin_be(po, pon, 32, 2);
    shden    = lin_be(po, pon, 34, 2);

    printf("\nPage Offsets Hint Table\n\n");
    printf("min_nobjects: %lld\n", minobj);
    /* The stored value counts from before the hint stream, which is inserted
     * ahead of the first page; the reference tool reports where the first page
     * really begins. */
    printf("first_page_offset: %lld\n", fpoff + L->H1);
    printf("nbits_delta_nobjects: %lld\n", nb_dobj);
    printf("min_page_length: %lld\n", minplen);
    printf("nbits_delta_page_length: %lld\n", nb_dplen);
    printf("min_content_offset: %lld\n", mincoff);
    printf("nbits_delta_content_offset: %lld\n", nb_dcoff);
    printf("min_content_length: %lld\n", minclen);
    printf("nbits_delta_content_length: %lld\n", nb_dclen);
    printf("nbits_nshared_objects: %lld\n", nb_nsh);
    printf("nbits_shared_identifier: %lld\n", nb_shid);
    printf("nbits_shared_numerator: %lld\n", nb_shnum);
    printf("shared_denominator: %lld\n", shden);

    b.p = po + 36;
    b.n = pon > 36 ? pon - 36 : 0;
    b.bit = 0;
    npages = (int)L->N;
    for (i = 0; i < npages; i++) {
        long long nobj, plen, nsh, clen, coff;
        long long ids[64], nums[64];

        nobj = minobj  + lin_bits_get(&b, nb_dobj);
        plen = minplen + lin_bits_get(&b, nb_dplen);
        nsh  = lin_bits_get(&b, nb_nsh);
        if (nsh < 0 || nsh > 64)
            nsh = 0;
        for (k = 0; k < (int)nsh; k++)
            ids[k] = lin_bits_get(&b, nb_shid);
        for (k = 0; k < (int)nsh; k++)
            nums[k] = lin_bits_get(&b, nb_shnum);
        coff = mincoff + lin_bits_get(&b, nb_dcoff);
        clen = minclen + lin_bits_get(&b, nb_dclen);

        printf("Page %d:\n", i);
        printf("  nobjects: %lld\n", nobj);
        printf("  length: %lld\n", plen);
        printf("  content_offset: %lld\n", coff);
        printf("  content_length: %lld\n", clen);
        printf("  nshared_objects: %lld\n", nsh);
        for (k = 0; k < (int)nsh; k++) {
            printf("    identifier %d: %lld\n", k, ids[k]);
            printf("    numerator %d: %lld\n", k, nums[k]);
        }
    }

    if (shn >= 24) {
        fsobj   = lin_be(sh, shn,  0, 4);
        fsoff   = lin_be(sh, shn,  4, 4);
        nsfp    = lin_be(sh, shn,  8, 4);
        nstot   = lin_be(sh, shn, 12, 4);
        nb_nobj = lin_be(sh, shn, 16, 2);
        mingl   = lin_be(sh, shn, 18, 4);
        nb_dgl  = lin_be(sh, shn, 22, 2);

        printf("\nShared Objects Hint Table\n\n");
        printf("first_shared_obj: %lld\n", fsobj);
        printf("first_shared_offset: %lld\n", fsoff);
        printf("nshared_first_page: %lld\n", nsfp);
        printf("nshared_total: %lld\n", nstot);
        printf("nbits_nobjects: %lld\n", nb_nobj);
        printf("min_group_length: %lld\n", mingl);
        printf("nbits_delta_group_length: %lld\n", nb_dgl);

        b.p = sh + 24;
        b.n = shn - 24;
        b.bit = 0;
        for (i = 0; i < (int)nstot && i < 65536; i++) {
            long long glen = mingl + lin_bits_get(&b, nb_dgl);

            (void)lin_bits_get(&b, nb_nobj);
            printf("Shared Object %d:\n", i);
            printf("  group length: %lld\n", glen);
        }
    }
    pdf_buf_free(&d);
}

/* ------------------------------------------------------------------ modes */

static int run_check(pdf_doc *doc, const char *path)
{
    lin_data L;

    report_damage(doc, path);

    printf("checking %s\n", path);
    printf("PDF Version: %d.%d\n", pdf_version(doc) / 10, pdf_version(doc) % 10);
    printf("File is %sencrypted\n", pdf_is_encrypted(doc) ? "" : "not ");

    lin_read(doc, &L);
    printf("File is %slinearized\n", (L.found && L.valid) ? "" : "not ");
    if (L.found && L.valid)
        lin_check(doc, &L, path,
                  "error encountered while checking linearization data: ");

    if (nwarnings == 0)
        printf("No syntax or stream encoding errors found; the file may still "
               "contain\nerrors that qpdf cannot detect\n");
    return flush_warnings();
}

static int run_show_xref(pdf_doc *doc, const char *path)
{
    long long off = 0;
    int n, type = 0, gen = 0;

    report_damage(doc, path);
    for (n = 1; n <= pdf_max_objnum(doc); n++) {
        if (!pdf_xref_entry(doc, n, &type, &off, &gen))
            continue;
        if (type != XREF_UNCOMPRESSED)
            continue;
        if (!chain_damaged && pdf_xref_freed_by_newer(doc, n))
            continue;
        printf("%d/%d: uncompressed; offset = %lld\n", n, gen, off);
    }
    return flush_warnings();
}

/* A content stream that the update freed is simply absent: the "content:"
 * heading is still printed, with nothing under it. */
static void show_content(pdf_doc *doc, pdf_obj *ref)
{
    if (!ref || ref->kind != PDF_REF)
        return;
    if (!chain_damaged && pdf_xref_freed_by_newer(doc, ref->u.ref.num))
        return;
    if (!pdf_is_stream(pdf_resolve(doc, ref)))
        return;
    printf("    %d %d R\n", ref->u.ref.num, ref->u.ref.gen);
}

static int run_show_pages(pdf_doc *doc, const char *path)
{
    int i, npages;

    report_damage(doc, path);
    npages = pdf_page_count(doc);
    for (i = 0; i < npages; i++) {
        pdf_obj *page = pdf_page_dict(doc, i), *c;
        int gen = 0, num = pdf_page_objnum(doc, i, &gen);

        printf("page %d: %d %d R\n", i + 1, num, gen);
        printf("  content:\n");
        c = pdf_dict_get_raw(page, "Contents");
        if (c && c->kind == PDF_REF) {
            show_content(doc, c);
        } else {
            pdf_obj *arr = pdf_resolve(doc, c);
            size_t k, m;

            for (k = 0, m = pdf_array_len(arr); k < m; k++)
                show_content(doc, pdf_array_get_raw(arr, k));
        }
    }
    return flush_warnings();
}

static int run_show_lin(pdf_doc *doc, const char *path)
{
    lin_data L;
    int hintnum, gen = 0;

    report_damage(doc, path);
    lin_read(doc, &L);
    if (!L.found || !L.valid) {
        printf("%s is not linearized\n", path);
        return flush_warnings();
    }

    /* Unlike --check, this mode names the failure without the "error
     * encountered while checking linearization data: " lead-in. */
    lin_check(doc, &L, path, "");
    hintnum = lin_hint_objnum(doc, L.H0, &gen);
    if (hintnum < 0 || L.N != (long long)pdf_page_count(doc))
        return flush_warnings();    /* nothing readable to dump */

    printf("%s: linearization data:\n\n", path);
    printf("file_size: %lld\n", L.L);
    printf("first_page_object: %lld\n", L.O);
    printf("first_page_end: %lld\n", L.E);
    printf("npages: %lld\n", L.N);
    printf("xref_zero_offset: %lld\n", L.T);
    printf("first_page: %lld\n", L.P);
    printf("H_offset: %lld\n", L.H0);
    printf("H_length: %lld\n", L.H1);
    lin_show_hints(doc, &L, hintnum, gen);
    return flush_warnings();
}

static int run_mode(const char *path, int mode)
{
    char *errmsg = NULL;
    pdf_doc *doc;
    FILE *probe;
    int rc;

    probe = fopen(path, "rb");
    if (probe == NULL) {
        fprintf(stderr, "qpdf: open %s: %s\n", path, strerror(errno));
        return 2;
    }
    fclose(probe);

    doc = pdf_open(path, &errmsg);
    if (doc == NULL) {
        fprintf(stderr, "qpdf: %s: %s\n", path,
                errmsg != NULL ? errmsg : "unable to read the PDF file");
        return 2;
    }
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)
        && pdf_authenticate(doc, "") < 0) {
        fprintf(stderr, "qpdf: %s: invalid password\n", path);
        pdf_close(doc);
        return 2;
    }

    switch (mode) {
    case MODE_CHECK:      rc = run_check(doc, path);      break;
    case MODE_SHOW_XREF:  rc = run_show_xref(doc, path);  break;
    case MODE_SHOW_PAGES: rc = run_show_pages(doc, path); break;
    default:              rc = run_show_lin(doc, path);   break;
    }
    pdf_close(doc);
    return rc;
}

static void write_buf(const pdf_buf *b)
{
    if (b->n > 0)
        fwrite(b->p, 1, b->n, stdout);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *spec = NULL;
    int filtered = 0;
    int rawdata = 0;
    int argi;
    int mode = MODE_SHOW_OBJECT;
    char *errmsg = NULL;
    pdf_doc *doc;
    pdf_obj *obj;
    pdf_buf out;
    FILE *probe;
    long num = 0;
    long gen = 0;

    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];

        if (strncmp(a, "--show-object=", 14) == 0) {
            spec = a + 14;
        } else if (strcmp(a, "--check") == 0) {
            mode = MODE_CHECK;
        } else if (strcmp(a, "--show-xref") == 0) {
            mode = MODE_SHOW_XREF;
        } else if (strcmp(a, "--show-pages") == 0) {
            mode = MODE_SHOW_PAGES;
        } else if (strcmp(a, "--show-linearization") == 0) {
            mode = MODE_SHOW_LIN;
        } else if (strcmp(a, "--filtered-stream-data") == 0) {
            filtered = 1;
            rawdata = 0;
        } else if (strcmp(a, "--raw-stream-data") == 0) {
            rawdata = 1;
            filtered = 0;
        } else if (a[0] == '-' && a[1] != '\0') {
            usage_error("unrecognized argument ", a);
            return 2;
        } else if (path == NULL) {
            path = a;
        } else {
            usage_error("unrecognized argument ", a);
            return 2;
        }
    }

    if (path == NULL) {
        usage_error("an input file name is required", NULL);
        return 2;
    }
    if (mode != MODE_SHOW_OBJECT)
        return run_mode(path, mode);
    if (spec == NULL) {
        usage_error("an output file name is required; use - for standard output",
                    NULL);
        return 2;
    }

    probe = fopen(path, "rb");
    if (probe == NULL) {
        fprintf(stderr, "qpdf: open %s: %s\n", path, strerror(errno));
        return 2;
    }
    fclose(probe);

    doc = pdf_open(path, &errmsg);
    if (doc == NULL) {
        fprintf(stderr, "qpdf: %s: %s\n", path,
                errmsg != NULL ? errmsg : "unable to read the PDF file");
        return 2;
    }
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)
        && pdf_authenticate(doc, "") < 0) {
        fprintf(stderr, "qpdf: %s: invalid password\n", path);
        pdf_close(doc);
        return 2;
    }

    if (strcmp(spec, "trailer") == 0) {
        obj = pdf_trailer(doc);
        /* The loader's trailer is the sections merged into one, and the merge
         * drops /Prev because it describes a section rather than the document.
         * What is asked for here is the file's trailer, which does carry it,
         * so it is put back -- on the way out, on a document that is about to
         * be closed, so nothing downstream inherits the key. */
        {
            long long prev = 0;

            if (pdf_xref_trailer_prev(doc, &prev))
                pdf_dict_put(doc, obj, "Prev", pdf_new_int(doc, prev));
        }
    } else {
        char *end;

        errno = 0;
        num = strtol(spec, &end, 10);
        if (end == spec || num <= 0) {
            /* qpdf prints nothing at all for object 0 or an unparsable
             * object id, and still exits 0. */
            pdf_close(doc);
            return 0;
        }
        if (*end == ',') {
            const char *g = end + 1;
            gen = strtol(g, &end, 10);
            if (end == g)
                gen = 0;
        }
        obj = pdf_get_object(doc, (int)num, (int)gen);
    }

    pdf_buf_init(&out);

    if (pdf_is_stream(obj) && (filtered || rawdata)) {
        if (pdf_stream_data(doc, obj, &out, filtered ? 1 : 0) < 0) {
            fprintf(stderr, "qpdf: %s: unable to filter stream data\n", path);
            pdf_buf_free(&out);
            pdf_close(doc);
            return 2;
        }
        write_buf(&out);
    } else if (pdf_is_stream(obj)) {
        printf("Object is stream.  Dictionary:\n");
        emit_dict(doc, obj->u.stm.dict, &out, 0);
        pdf_buf_putc(&out, '\n');
        write_buf(&out);
    } else {
        emit_obj(doc, obj, &out, 0);
        pdf_buf_putc(&out, '\n');
        write_buf(&out);
    }

    pdf_buf_free(&out);
    pdf_close(doc);
    return 0;
}
