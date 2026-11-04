/* pdfa.c -- PDF/A (ISO 19005) and PDF/UA (ISO 14289-1) conformance checking.
 *
 * The clauses implemented here are those of /app/SPEC/pdfa1-2.md and
 * /app/SPEC/pdfa3-4-ua.md.  A conformance check is not a repair: everything the
 * earlier milestones do to RECOVER a damaged file is deliberately not done
 * here, because a file a reader can only open by reconstructing its
 * cross-reference table is non-conforming (19005-1 6.1.4) even though the
 * reader must still open it.  Reading damaged files and certifying damaged
 * files are different jobs and this file is the second one.
 *
 * Structure: one pdfa_ctx is built per document, the shared walks fill it in
 * once, and each clause is a small predicate over it.  A clause that cannot be
 * decided from the model is not guessed at -- SPEC 3.12 is explicit that
 * nothing outside the machine-decidable subset is scored, and a checker that
 * reports what a standard MEANS rather than what it TESTS is wrong here.
 */

#include "pdfaint.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* memmem() is a GNU extension and this translation unit is compiled with
 * _POSIX_C_SOURCE alone, so the search is spelled out here rather than the
 * feature-test macro being widened for one call. */
static const char *pdfa_memmem(const void *hay, size_t hayn,
                               const void *needle, size_t nlen)
{
    const char *h = (const char *)hay;
    size_t i;

    if (nlen == 0) return h;
    if (hayn < nlen) return NULL;
    for (i = 0; i + nlen <= hayn; i++)
        if (h[i] == *(const char *)needle && !memcmp(h + i, needle, nlen))
            return h + i;
    return NULL;
}

/* ------------------------------------------------------------- flavours --- */

/* The table is split by milestone rather than nested: step 6's tree carries the
 * five flavours ISO 19005-1 and -2 define, and step 7's adds the later parts'.
 * A flavour absent from the table is rejected with the interface's status 2,
 * so `-f 4` against a step-6 build is an unknown flavour and says so. */
static const pdfa_flavour FLAVOURS[] = {
    { "1b", 19005, 1, 'B' },
    { "1a", 19005, 1, 'A' },
    { "2b", 19005, 2, 'B' },
    { "2a", 19005, 2, 'A' },
    { "2u", 19005, 2, 'U' },
    { "3b", 19005, 3, 'B' },
    { "3a", 19005, 3, 'A' },
    { "3u", 19005, 3, 'U' },
    { "4",  19005, 4, 0   },
    { "4e", 19005, 4, 'E' },
    { "4f", 19005, 4, 'F' },
    { "ua1", 14289, 1, 0  },
};

/* The clause that refused the last document, for the trace below.  Never read
 * by the verdict itself -- a checker reports conformance and nothing else. */
static const char *pdfa_reason = "";

const char *pdfa_last_reason(void) { return pdfa_reason; }

const pdfa_flavour *pdfa_flavour_lookup(const char *name)
{
    size_t i;

    if (!name) return NULL;
    for (i = 0; i < sizeof FLAVOURS / sizeof FLAVOURS[0]; i++)
        if (!strcmp(FLAVOURS[i].name, name)) return &FLAVOURS[i];
    return NULL;
}

/* --------------------------------------------------------- parsability ---- */

/* Whether a MODEL can be built, which is not whether the file is well formed.
 * The corpus contains files whose header, trailer and cross-reference table are
 * each deliberately broken and which a checker still reports a verdict for --
 * they parse, and they fail a clause.  What makes a file unparsable is having
 * no document structure to find at all: no header, or no trailer and no
 * startxref anywhere, so that nothing names a catalogue to judge. */
int pdfa_parsable(const char *path)
{
    FILE *fp;
    unsigned char *buf;
    long n;
    int ok = 0;
    size_t got;

    fp = fopen(path, "rb");
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    n = ftell(fp);
    if (n <= 0) { fclose(fp); return 0; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 0; }
    buf = malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return 0; }
    got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    buf[got] = 0;

    /* The header must be findable near the start.  ISO 19005 requires it at
     * byte zero and a file that puts it elsewhere fails 6.1.2 -- but it still
     * parses, so the window here is generous and the clause is what refuses. */
    {
        size_t lim = got < 1024 ? got : 1024, i;
        int have_header = 0;

        for (i = 0; i + 5 <= lim; i++)
            if (!memcmp(buf + i, "%PDF-", 5)) { have_header = 1; break; }
        if (have_header) {
            /* A file with a header but no cross-reference information and no
             * trailer names no catalogue: there is nothing to build a model
             * from, and that is the truncated-past-use case. */
            if (pdfa_memmem(buf, got, "startxref", 9) ||
                pdfa_memmem(buf, got, "trailer", 7))
                ok = 1;
        }
    }
    free(buf);
    return ok;
}

/* ------------------------------------------------------------------ XMP ---- */

/* A small scanner over the packet rather than a general XML parser: the
 * properties every identification and provenance clause reads are simple
 * scalars, and a packet spells them either as an attribute on an
 * rdf:Description or as a child element.  Both forms are accepted. */

static void xmp_trim(char *s)
{
    size_t n, i = 0;

    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

/* Decodes the five XML predefined entities in place; nothing else appears in
 * the corpus packets and a numeric reference is left alone deliberately, so a
 * value that carries one compares unequal rather than silently equal. */
static void xmp_unescape(char *s)
{
    char *r = s, *w = s;

    while (*r) {
        if (*r == '&') {
            if (!strncmp(r, "&amp;", 5))       { *w++ = '&'; r += 5; continue; }
            if (!strncmp(r, "&lt;", 4))        { *w++ = '<'; r += 4; continue; }
            if (!strncmp(r, "&gt;", 4))        { *w++ = '>'; r += 4; continue; }
            if (!strncmp(r, "&quot;", 6))      { *w++ = '"'; r += 6; continue; }
            if (!strncmp(r, "&apos;", 6))      { *w++ = '\''; r += 6; continue; }
        }
        *w++ = *r++;
    }
    *w = 0;
}

int pdfa_xmp_property(const char *packet, size_t len, const char *qname,
                      char *out, size_t outsz)
{
    const char *p, *end = packet + len;
    size_t qn = strlen(qname);

    if (!packet || !qname || !out || outsz == 0) return 0;
    out[0] = 0;

    /* Attribute form:  pdfaid:part="1"  or  pdfaid:part='1' */
    for (p = packet; p + qn + 2 < end; p++) {
        const char *q;

        if (memcmp(p, qname, qn)) continue;
        if (p > packet && (isalnum((unsigned char)p[-1]) || p[-1] == ':' ||
                           p[-1] == '_' || p[-1] == '-')) continue;
        q = p + qn;
        while (q < end && isspace((unsigned char)*q)) q++;
        if (q >= end || *q != '=') continue;
        q++;
        while (q < end && isspace((unsigned char)*q)) q++;
        if (q < end && (*q == '"' || *q == '\'')) {
            char quote = *q++;
            const char *s = q;

            while (q < end && *q != quote) q++;
            if (q < end) {
                size_t n = (size_t)(q - s);

                if (n >= outsz) n = outsz - 1;
                memcpy(out, s, n);
                out[n] = 0;
                xmp_unescape(out);
                /* NOT trimmed: an attribute value is the value, spaces and all.
                 * The information dictionary keeps its trailing space and the
                 * two are compared byte for byte, so trimming one side here
                 * manufactured a mismatch on every file whose /Producer ends
                 * in a space -- which the whole corpus does. */
                return 1;
            }
        }
    }

    /* Element form:  <pdfaid:part>1</pdfaid:part>, possibly wrapping an
     * rdf:Alt / rdf:Seq whose first rdf:li carries the value. */
    {
        char open[128], close[128];
        const char *s, *e;

        if (qn + 3 >= sizeof open) return 0;
        snprintf(open, sizeof open, "<%s", qname);
        snprintf(close, sizeof close, "</%s>", qname);
        s = pdfa_memmem(packet, len, open, strlen(open));
        if (!s) return 0;
        s += strlen(open);
        /* skip the rest of the start tag */
        while (s < end && *s != '>') s++;
        if (s >= end) return 0;
        s++;
        e = pdfa_memmem(s, (size_t)(end - s), close, strlen(close));
        if (!e) return 0;
        {
            /* An rdf:li inside an Alt/Seq/Bag carries the scalar. */
            const char *li = pdfa_memmem(s, (size_t)(e - s), "<rdf:li", 7);
            const char *vs = s, *ve = e;

            if (li) {
                const char *lend = li;

                while (lend < e && *lend != '>') lend++;
                if (lend < e) {
                    vs = lend + 1;
                    ve = pdfa_memmem(vs, (size_t)(e - vs), "</rdf:li>", 9);
                    if (!ve) ve = e;
                }
            }
            {
                size_t n = (size_t)(ve - vs);

                if (n >= outsz) n = outsz - 1;
                memcpy(out, vs, n);
                out[n] = 0;
                xmp_unescape(out);
                xmp_trim(out);
            }
        }
        return 1;
    }
}


/* An XMP packet may be UTF-8, UTF-16 or UTF-32, and the corpus uses UTF-16 for
 * a whole clause directory.  Every property this file reads is ASCII, so a
 * UTF-16 packet is folded down to its low bytes in place rather than a full
 * decoder being written: the scanners below are byte-oriented and would
 * otherwise find nothing in such a packet at all -- which reads exactly like a
 * file carrying no identification, and wrongly condemns it.
 *
 * Detected by the byte-order mark, or by the NUL padding that a UTF-16 encoding
 * of ASCII always shows. */
static void xmp_to_bytes(pdf_buf *b)
{
    size_t i, n = b->n, zero_even = 0, zero_odd = 0, w;
    unsigned char *p = b->p;
    int be;

    if (n < 4) return;

    /* UTF-32 first: its little-endian mark begins with the UTF-16 one, so a
     * UTF-16 test would claim a UTF-32 packet and fold it into alternating
     * characters and NULs -- which finds no property at all and reads exactly
     * like a file carrying no identification. */
    if ((p[0] == 0xFF && p[1] == 0xFE && p[2] == 0x00 && p[3] == 0x00) ||
        (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0xFE && p[3] == 0xFF) ||
        (n >= 8 && p[1] == 0 && p[2] == 0 && p[3] == 0 && p[5] == 0 &&
         p[6] == 0 && p[7] == 0)) {
        int b32 = (p[0] == 0x00 && p[1] == 0x00);
        size_t w32 = 0;

        i = (p[0] == 0xFF || p[0] == 0x00) &&
            ((p[0] == 0xFF && p[1] == 0xFE && p[2] == 0 && p[3] == 0) ||
             (p[0] == 0 && p[1] == 0 && p[2] == 0xFE && p[3] == 0xFF)) ? 4 : 0;
        for (; i + 3 < n; i += 4) {
            unsigned int u = b32 ? ((unsigned)p[i + 2] << 8 | p[i + 3])
                                 : ((unsigned)p[i + 1] << 8 | p[i]);

            p[w32++] = (unsigned char)(u < 0x80 ? u : '?');
        }
        b->n = w32;
        return;
    }

    if (p[0] == 0xFF && p[1] == 0xFE)      be = 0;
    else if (p[0] == 0xFE && p[1] == 0xFF) be = 1;
    else {
        size_t lim = n < 512 ? n : 512;

        for (i = 0; i + 1 < lim; i += 2) {
            if (p[i] == 0)     zero_even++;
            if (p[i + 1] == 0) zero_odd++;
        }
        if (zero_even > lim / 8)      be = 1;
        else if (zero_odd > lim / 8)  be = 0;
        else return;                  /* already a byte encoding */
        i = 0;
        goto fold;
    }
    i = 2;                            /* skip the mark */
fold:
    for (w = 0; i + 1 < n; i += 2) {
        unsigned int u = be ? ((unsigned)p[i] << 8 | p[i + 1])
                            : ((unsigned)p[i + 1] << 8 | p[i]);

        p[w++] = (unsigned char)(u < 0x80 ? u : '?');
    }
    b->n = w;
}

/* ISO 32000-1 7.9.4 "D:YYYYMMDDHHmmSSOHH'mm'" against XMP's ISO 8601
 * "YYYY-MM-DDTHH:MM:SS+HH:MM".  The two spell one instant differently, so the
 * information dictionary's form is rewritten into the packet's before they are
 * compared -- 6.7.3 t1 and t8 are about the VALUE, not the spelling. */
static void pdfdate_to_iso(const char *in, char *out, size_t outsz)
{
    char d[20];
    size_t i = 0, j = 0;

    out[0] = 0;
    if (!in) return;
    if (in[0] == 'D' && in[1] == ':') in += 2;
    while (in[i] && j < 14) if (in[i] >= '0' && in[i] <= '9') d[j++] = in[i++]; else break;
    if (j < 4) return;
    while (j < 14) d[j++] = (j <= 8) ? '0' : '0';
    d[14] = 0;
    /* Fill an absent month/day with 01, which is what the shorter forms mean. */
    if (d[4] == '0' && d[5] == '0') { d[4] = '0'; d[5] = '1'; }
    if (d[6] == '0' && d[7] == '0') { d[6] = '0'; d[7] = '1'; }
    snprintf(out, outsz, "%.4s-%.2s-%.2sT%.2s:%.2s:%.2s",
             d, d + 4, d + 6, d + 8, d + 10, d + 12);
    {
        const char *z = in + i;
        size_t L = strlen(out);

        if (*z == 'Z' || *z == 0) { if (L + 2 < outsz) { out[L] = 'Z'; out[L+1] = 0; } }
        else if ((*z == '+' || *z == '-') && isdigit((unsigned char)z[1]))
            snprintf(out + L, outsz - L, "%c%.2s:%.2s", *z, z + 1, z + 4);
    }
}

int pdfa_xmp_read(pdf_doc *doc, pdfa_xmp *out)
{
    pdf_obj *cat, *md;
    pdf_buf raw;
    char tmp[64];

    memset(out, 0, sizeof *out);
    cat = pdf_catalog(doc);
    md  = pdf_dict_get(doc, cat, "Metadata");
    if (!pdf_is_stream(md)) return 0;

    out->present = 1;
    /* 6.7.2: the packet shall be recoverable from the bytes of the file
     * directly, so it shall carry no /Filter at all.  The presence of one is
     * the defect; it is recorded rather than worked around. */
    if (!pdf_is_null(pdf_dict_geta(doc, md, "Filter", "F")))
        out->filtered = 1;

    pdf_buf_init(&raw);
    if (pdf_stream_data(doc, md, &raw, 1) != 0 || raw.n == 0) {
        /* No usable /Length -- and a /Metadata stream is not required to have
         * one this reader can trust.  6.7.2 makes the packet recoverable from
         * the bytes of the file directly, so recover it that way: the xpacket
         * wrapper delimits it and nothing else in the file carries one. */
        const unsigned char *fb;
        size_t flen = 0;

        pdf_buf_free(&raw);
        pdf_buf_init(&raw);
        fb = pdf_file_bytes(doc, &flen);
        if (fb) {
            const char *b = pdfa_memmem(fb, flen, "<?xpacket begin", 15);

            if (b) {
                const char *e = pdfa_memmem(b, flen - (size_t)((const unsigned char *)b - fb),
                                            "<?xpacket end", 13);

                if (e) {
                    const char *q = pdfa_memmem(e, flen - (size_t)((const unsigned char *)e - fb),
                                                "?>", 2);

                    if (q) pdf_buf_put(&raw, b, (size_t)(q + 2 - b));
                }
            }
        }
        if (raw.n == 0) { pdf_buf_free(&raw); return 1; }
    }
    xmp_to_bytes(&raw);

    /* Well-formedness, to the depth these clauses need: an xpacket or an
     * x:xmpmeta wrapper and a balanced rdf:RDF element. */
    out->wellformed = (pdfa_memmem(raw.p, raw.n, "<rdf:RDF", 8) &&
                       pdfa_memmem(raw.p, raw.n, "</rdf:RDF>", 10)) ? 1 : 0;

    if (pdfa_xmp_property((const char *)raw.p, raw.n, "pdfaid:part",
                          tmp, sizeof tmp)) {
        char *e;
        long v = strtol(tmp, &e, 10);

        if (e != tmp && *e == 0) { out->have_part = 1; out->part = v; }
        else out->have_part = -1;      /* present but not an integer */
    }
    if (pdfa_xmp_property((const char *)raw.p, raw.n, "pdfaid:rev",
                          tmp, sizeof tmp)) {
        char *e;
        long v = strtol(tmp, &e, 10);

        if (e != tmp && *e == 0) { out->have_rev = 1; out->rev = v; }
        else out->have_rev = -1;
    }
    pdfa_xmp_property((const char *)raw.p, raw.n, "pdfaid:conformance",
                      out->conformance, sizeof out->conformance);
    if (pdfa_xmp_property((const char *)raw.p, raw.n, "pdfuaid:part",
                          tmp, sizeof tmp)) {
        char *e;
        long v = strtol(tmp, &e, 10);

        if (e != tmp && *e == 0) { out->have_ua_part = 1; out->ua_part = v; }
        else out->have_ua_part = -1;
    }

    pdfa_xmp_property((const char *)raw.p, raw.n, "dc:title",
                      out->title, sizeof out->title);
    pdfa_xmp_property((const char *)raw.p, raw.n, "dc:creator",
                      out->creator, sizeof out->creator);
    pdfa_xmp_property((const char *)raw.p, raw.n, "dc:description",
                      out->description, sizeof out->description);
    pdfa_xmp_property((const char *)raw.p, raw.n, "pdf:Keywords",
                      out->keywords, sizeof out->keywords);
    pdfa_xmp_property((const char *)raw.p, raw.n, "xmp:CreatorTool",
                      out->creatortool, sizeof out->creatortool);
    pdfa_xmp_property((const char *)raw.p, raw.n, "pdf:Producer",
                      out->producer, sizeof out->producer);
    pdfa_xmp_property((const char *)raw.p, raw.n, "xmp:CreateDate",
                      out->createdate, sizeof out->createdate);
    pdfa_xmp_property((const char *)raw.p, raw.n, "xmp:ModifyDate",
                      out->moddate, sizeof out->moddate);

    pdf_buf_free(&raw);
    return 1;
}

/* ------------------------------------------------- shared document walks --- */

#define PDFA_MAX_DEPTH 64

/* Prohibited action types (19005-1 6.6.1 / 19005-2 6.5.1 / 19005-4 6.6.1).
 * The prohibition is on PRESENCE, not on reachability: an action dictionary of
 * a forbidden type in an object nothing refers to still makes the file
 * non-conforming, so the walk below is over every object the cross-reference
 * table defines rather than over the reachable graph alone. */
static int action_is_forbidden(const char *s)
{
    static const char *const bad[] = {
        "JavaScript", "Launch", "Movie", "Sound", "ResetForm", "ImportData",
        "SetOCGState", NULL
    };
    int i;

    for (i = 0; bad[i]; i++) if (!strcmp(s, bad[i])) return 1;
    return 0;
}

/* A /Named action shall be one of the four the clause names. */
static int named_action_ok(const char *s)
{
    return !strcmp(s, "NextPage") || !strcmp(s, "PrevPage") ||
           !strcmp(s, "FirstPage") || !strcmp(s, "LastPage");
}

typedef struct {
    pdf_doc *doc;
    int part;           /* 1..4 */
    int level;          /* 'A' | 'B' | 'U' | 'E' | 'F' | 0 */
    int std;            /* 19005 | 14289 */

    int bad_action;     /* a prohibited action type appears              */
    int bad_named;      /* a /Named action outside the four              */
    int has_aa;         /* an additional-actions dictionary appears      */
    int has_js_tree;    /* the catalogue carries a /JavaScript name tree */
    int lzw;            /* an LZWDecode filter appears                   */
    int embedded_files; /* the name tree carries /EmbeddedFiles          */
    int bad_annot;      /* an annotation violates 6.5.3 / 6.3            */
    int transparency;   /* a transparency construct appears              */
    int limit;          /* an implementation limit is exceeded           */
} pdfa_ctx;

static void scan_filters(pdfa_ctx *c, pdf_obj *o)
{
    pdf_doc *doc = c->doc;
    pdf_obj *f = pdf_dict_geta(doc, o, "Filter", "F");
    size_t i, n;

    if (pdf_is_name(f, "LZWDecode")) { c->lzw = 1; return; }
    if (!pdf_is_array(f)) return;
    n = pdf_array_len(f);
    for (i = 0; i < n; i++)
        if (pdf_is_name(pdf_array_get(doc, f, i), "LZWDecode")) c->lzw = 1;
}

/* One object, judged for the clauses whose prohibition is on presence. */
static void scan_object(pdfa_ctx *c, pdf_obj *o, int depth)
{
    pdf_doc *doc = c->doc;
    size_t i, n;

    if (depth > PDFA_MAX_DEPTH || pdf_is_null(o)) return;

    if (pdf_is_array(o)) {
        n = pdf_array_len(o);
        for (i = 0; i < n; i++) scan_object(c, pdf_array_get(doc, o, i),
                                            depth + 1);
        return;
    }
    if (pdf_is_num(o) && !pdf_is_int(o)) {
        double v = pdf_to_real(o);

        /* t2: absolute real value at most 32767.0 */
        if (v < -32767.0 || v > 32767.0) c->limit = 1;
        return;
    }
    if (pdf_is_string(o)) {
        size_t sn = 0;

        /* t3: maximum string length in bytes is 65535 */
        pdf_to_string(o, &sn);
        if (sn > 65535) c->limit = 1;
        return;
    }
    if (pdf_is_name(o, NULL)) {
        /* t4: maximum name length in bytes is 127 */
        if (strlen(pdf_to_name(o)) > 127) c->limit = 1;
        return;
    }
    if (!pdf_is_dict(o)) return;

    if (pdf_is_stream(o)) scan_filters(c, o);

    /* A dictionary's KEYS are name objects and are subject to t4 as well. */
    {
        size_t ki, kn = pdf_dict_len(o);

        for (ki = 0; ki < kn; ki++) {
            const char *k = pdf_dict_key(o, ki);

            if (k && strlen(k) > 127) c->limit = 1;
        }
    }

    /* An action dictionary: /S names the type. */
    {
        pdf_obj *s = pdf_dict_get(doc, o, "S");

        if (pdf_is_name(s, NULL) && pdf_is_name(pdf_dict_get(doc, o, "Type"),
                                                "Action")) {
            const char *t = pdf_to_name(s);

            if (action_is_forbidden(t)) c->bad_action = 1;
            if (!strcmp(t, "Named")) {
                pdf_obj *nm = pdf_dict_get(doc, o, "N");

                if (!pdf_is_name(nm, NULL) || !named_action_ok(pdf_to_name(nm)))
                    c->bad_named = 1;
            }
        } else if (pdf_is_name(s, NULL) && c->part < 4) {
            /* An action reached through /A or /OpenAction need not carry
             * /Type, so for parts 1 to 3 -- where the prohibition is on
             * PRESENCE -- the type name alone condemns it.  Part 4 is
             * reachability-based instead: measured, a part-4 file carrying an
             * unreferenced "<< /JS n 0 R /S /JavaScript >>" among its outline
             * objects is reported compliant with no failed rule at all, so a
             * bare dictionary is not condemned there. */
            const char *t = pdf_to_name(s);

            if (action_is_forbidden(t) &&
                !pdf_is_null(pdf_dict_get(doc, o, "Type")) == 0)
                c->bad_action = 1;
        }
    }

    {
        size_t vi, vn = pdf_dict_len(o);

        for (vi = 0; vi < vn; vi++) {
            pdf_obj *v = pdf_dict_val_raw(o, vi);

            /* Raw, not resolved: a reference is followed by the top-level walk
             * over every object the table defines, so following it here would
             * only revisit it and risk a cycle. */
            if (v && v->kind != PDF_REF) scan_object(c, v, depth + 1);
        }
    }

    /* 19005-1 6.6.2 and 19005-2 6.5.2 forbid the trigger dictionaries
     * outright.  ISO 19005-4 6.6.3 does not: measured, a part-4 file whose
     * /AA holds a /GoTo PASSES, so there the entry is condemned only when what
     * it holds is condemned. */
    if (c->part < 4 && !pdf_is_null(pdf_dict_get_raw(o, "AA"))) c->has_aa = 1;

    /* Transparency: ISO 19005-1 6.4 forbids it outright. */
    {
        pdf_obj *g = pdf_dict_get(doc, o, "Group");

        if (pdf_is_dict(g) && pdf_is_name(pdf_dict_get(doc, g, "S"),
                                          "Transparency"))
            c->transparency = 1;
    }
    if (pdf_is_name(pdf_dict_get(doc, o, "Type"), "ExtGState")) {
        pdf_obj *sm = pdf_dict_get(doc, o, "SMask");
        pdf_obj *bm = pdf_dict_get(doc, o, "BM");
        pdf_obj *ca = pdf_dict_get(doc, o, "CA");
        pdf_obj *cal = pdf_dict_get(doc, o, "ca");

        if (pdf_is_dict(sm)) c->transparency = 1;
        if (pdf_is_name(sm, NULL) && strcmp(pdf_to_name(sm), "None"))
            c->transparency = 1;
        if (pdf_is_name(bm, NULL) && strcmp(pdf_to_name(bm), "Normal") &&
            strcmp(pdf_to_name(bm), "Compatible"))
            c->transparency = 1;
        /* 6.4 t5/t6 compare with a 1e-6 tolerance, so /CA 1.0000001 and
         * /CA 0.9999999 both conform; an exact test refuses them. */
        if (pdf_is_num(ca)) {
            double d = pdf_to_real(ca) - 1.0;

            if (d > 0.000001 || d < -0.000001) c->transparency = 1;
        }
        if (pdf_is_num(cal)) {
            double d = pdf_to_real(cal) - 1.0;

            if (d > 0.000001 || d < -0.000001) c->transparency = 1;
        }
    }
}

/* Every object the table defines, which is what "the prohibition is on
 * presence" requires. */
static void scan_all_objects(pdfa_ctx *c)
{
    pdf_doc *doc = c->doc;
    int n, max = pdf_max_objnum(doc);

    for (n = 1; n <= max; n++) {
        int type = 0, gen = 0;
        long long off = 0;
        pdf_obj *o;

        if (!pdf_xref_entry(doc, n, &type, &off, &gen)) {
            int stmnum, stmidx;

            /* An object living inside an object stream has no file offset, so
             * the entry above reports nothing for it; it is still an object of
             * the document and still subject to every prohibition. */
            if (!pdf_xref_compressed(doc, n, &stmnum, &stmidx)) continue;
        }
        o = pdf_get_object(doc, n, gen);
        scan_object(c, o, 0);
    }
}

/* ------------------------------------------------------------- clauses ----- */

/* 6.1.2: the header shall be at byte zero, and the second line a comment
 * carrying at least four bytes above 127. */
static int clause_header(pdfa_ctx *c)
{
    pdf_doc *doc = c->doc;
    const unsigned char *p;
    size_t len, i, hi = 0;

    p = pdf_file_bytes(doc, &len);
    if (!p || len < 9) return 0;
    if (memcmp(p, "%PDF-", 5)) return 0;

    /* 6.1.2 t1: the %% shall be at byte offset 0 and the line shall BE the
     * eight bytes "%PDF-d.d" -- measured against the reference, which passes a
     * %PDF-2.0 header at 1b and refuses %PDF-1.70.  The clause tests the shape
     * of the header line; which version it names is not its business. */
    {
        size_t k = 0;

        while (k < len && p[k] != '\n' && p[k] != '\r') k++;
        if (k != 8) return 0;
        if (p[5] < '0' || p[5] > '9') return 0;
        if (p[6] != '.') return 0;
        if (p[7] < '0' || p[7] > '9') return 0;
    }
    /* The header pattern is PER PART, measured against the reference: part 1
     * accepts any "%PDF-d.d" (a %PDF-2.0 header PASSes at 1b), parts 2 and 3
     * require "%PDF-1.n", and part 4 requires "%PDF-2.n". */
    if (c->part == 2 || c->part == 3) {
        if (p[5] != '1') return 0;
    }
    /* ISO 19005-4 is built on PDF 2.0: a conforming header is %PDF-2.n, and a
     * file whose header says %PDF-1.7 is not a PDF/A-4 file however well formed
     * its contents.  This is the most mechanical difference between part 4 and
     * every earlier part and it is checked first.  The version is READ here and
     * nowhere else, so it is fetched here too: milestone 6 has no use for it
     * and a variable assigned there would be dead in that tree. */
    {
        int ver = pdf_header_version(doc);

        if (c->part == 4 && (ver < 20 || ver >= 30)) return 0;
    }

    /* The binary comment on the second line. */
    for (i = 0; i < len && p[i] != '\n' && p[i] != '\r'; i++) ;
    while (i < len && (p[i] == '\n' || p[i] == '\r')) i++;
    if (i >= len || p[i] != '%') return 0;
    for (i++; i < len && p[i] != '\n' && p[i] != '\r'; i++)
        if (p[i] > 127) hi++;
    return hi >= 4;
}

/* 6.1.3 / 6.1.4: the trailer shall carry /Root, and /ID from part 2 on; the
 * cross-reference information shall be complete and correct, which is to say
 * the reader shall not have had to reconstruct it. */
static int clause_trailer_xref(pdfa_ctx *c)
{
    pdf_doc *doc = c->doc;
    pdf_obj *tr = pdf_trailer(doc);
    long long bad = 0;

    if (!pdf_is_dict(pdf_catalog(doc))) return 0;
    if (pdf_is_null(pdf_dict_get_raw(tr, "Root"))) return 0;
    if (!pdf_is_null(pdf_dict_get_raw(tr, "Encrypt"))) return 0;
    /* 6.1.3 t1: the trailer shall contain /ID.  This is ISO 19005-1's rule too,
     * not only part 2's. */
    if (pdf_is_null(pdf_dict_get_raw(tr, "ID"))) return 0;
    if (pdf_damaged(doc)) return 0;
    if (pdf_xref_chain_check(doc, &bad)) return 0;
    return 1;
}

/* 6.7.11 / 6.6.4 / 6.7.3: the XMP identification.  A file with no
 * identification claims nothing and is non-conforming at every level, and one
 * whose identification is present but malformed is non-conforming for that
 * reason alone. */
static int clause_identification(pdfa_ctx *c, const pdfa_xmp *x)
{
    if (!x->present || x->filtered || !x->wellformed) return 0;
    if (x->have_part != 1) return 0;
    if (x->part != c->part) return 0;
    if (c->part == 4) {
        /* pdfaid:rev is part 4's and has no counterpart in parts 1 to 3, and
         * conformance is provided ONLY for 4e and 4f: a plain PDF/A-4 file
         * carrying the `B` every earlier part used is non-conforming for that
         * reason alone. */
        if (x->have_rev != 1 || x->rev != 2020) return 0;
        if (c->level == 0) return x->conformance[0] == 0;
        return x->conformance[0] == c->level && x->conformance[1] == 0;
    }
    if (c->level == 0) return 1;
    return x->conformance[0] == c->level && x->conformance[1] == 0;
}

/* 6.7.3 / 6.6.2: where the document information dictionary and the XMP packet
 * both carry a property, the two shall have the same value.  A value present in
 * one and absent from the other is permitted. */
static int info_agrees(pdf_doc *doc, pdf_obj *info, const char *key,
                       const char *xmpval)
{
    pdf_obj *v;
    const char *s;
    size_t n;
    pdf_buf u;
    int ok;

    if (!xmpval || !*xmpval) return 1;
    v = pdf_dict_get(doc, info, key);
    if (!pdf_is_string(v)) return 1;
    s = pdf_to_string(v, &n);
    if (!s) return 1;

    pdf_buf_init(&u);
    if (pdf_text_string_to_utf8(s, n, &u) != 0) { pdf_buf_free(&u); return 1; }
    pdf_buf_putc(&u, 0);
    ok = !strcmp((const char *)u.p, xmpval);
    pdf_buf_free(&u);
    return ok;
}

/* "YYYY-MM-DDThh:mm[:ss][Z|+hh:mm]" reduced to six UTC field values.  Missing
 * seconds are zero and a missing zone is UTC, which is how both spellings in
 * the corpus are meant to be read. */
static int iso_fields(const char *s, int *f)
{
    int y = 0, mo = 1, d = 1, h = 0, mi = 0, se = 0, oh = 0, om = 0, sign = 0;
    int got;

    if (!s) return 0;
    got = sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se);
    if (got < 5) return 0;
    {
        const char *z = s;

        while (*z && *z != 'Z' && !(z > s + 10 && (*z == '+' || *z == '-'))) z++;
        if (*z == '+' || *z == '-') {
            sign = (*z == '-') ? -1 : 1;
            sscanf(z + 1, "%2d:%2d", &oh, &om);
        }
    }
    mi -= sign * om;
    h  -= sign * oh;
    while (mi < 0)  { mi += 60; h--; }
    while (mi > 59) { mi -= 60; h++; }
    while (h < 0)   { h += 24; d--; }
    while (h > 23)  { h -= 24; d++; }
    f[0] = y; f[1] = mo; f[2] = d; f[3] = h; f[4] = mi; f[5] = se;
    return 1;
}

/* As info_agrees(), for the two date properties, whose two spellings of one
 * instant are reconciled before the comparison. */
static int date_agrees(pdf_doc *doc, pdf_obj *info, const char *key,
                       const char *xmpval)
{
    pdf_obj *v;
    const char *s;
    size_t n;
    char iso[64];

    if (!xmpval || !*xmpval) return 1;
    v = pdf_dict_get(doc, info, key);
    if (!pdf_is_string(v)) return 1;
    s = pdf_to_string(v, &n);
    if (!s) return 1;
    {
        char buf[96];
        size_t m = n < sizeof buf - 1 ? n : sizeof buf - 1;

        memcpy(buf, s, m); buf[m] = 0;
        pdfdate_to_iso(buf, iso, sizeof iso);
    }
    if (!iso[0]) return 1;
    /* "+00:00" and "Z" are one instant, and a packet may omit the seconds, so
     * both sides are reduced to UTC field values before they are compared. */
    {
        int a[6], bb[6];

        if (!iso_fields(iso, a) || !iso_fields(xmpval, bb)) return 1;
        return a[0] == bb[0] && a[1] == bb[1] && a[2] == bb[2] &&
               a[3] == bb[3] && a[4] == bb[4] && a[5] == bb[5];
    }
}

static int clause_info_agreement(pdfa_ctx *c, const pdfa_xmp *x)
{
    pdf_doc *doc = c->doc;
    pdf_obj *info = pdf_dict_get(doc, pdf_trailer(doc), "Info");

    if (!pdf_is_dict(info)) return 1;
    /* Measured against the reference: only ISO 19005-1 carries the agreement
     * clause (6.7.3 t1-t8).  Parts 2, 3 and 4 have no such rule at all -- a 2b
     * file whose /Title is "Info title" while its dc:title is "Test title"
     * PASSES -- so applying it there is pure over-strictness. */
    if (c->part != 1) return 1;
    if (!info_agrees(doc, info, "Title",    x->title))       return 0;
    if (!info_agrees(doc, info, "Author",   x->creator))     return 0;
    if (!info_agrees(doc, info, "Subject",  x->description)) return 0;
    if (!info_agrees(doc, info, "Keywords", x->keywords))    return 0;
    if (!info_agrees(doc, info, "Creator",  x->creatortool)) return 0;
    if (!info_agrees(doc, info, "Producer", x->producer))    return 0;
    if (!date_agrees(doc, info, "CreationDate", x->createdate)) return 0;
    if (!date_agrees(doc, info, "ModDate",      x->moddate))    return 0;
    return 1;
}

/* 6.2.2 / 6.2.3: at most one output intent with /S /GTS_PDFA1, and that one
 * shall carry a /DestOutputProfile that is an embedded stream. */
static int clause_output_intent(pdfa_ctx *c, int *have_intent)
{
    pdf_doc *doc = c->doc;
    pdf_obj *ois = pdf_dict_get(doc, pdf_catalog(doc), "OutputIntents");
    size_t i, n;
    pdf_obj *profile = NULL;
    int seen = 0;

    *have_intent = 0;
    if (!pdf_is_array(ois)) return 1;
    n = pdf_array_len(ois);
    for (i = 0; i < n; i++) {
        pdf_obj *oi = pdf_array_get(doc, ois, i), *dop;

        if (!pdf_is_dict(oi)) continue;
        if (!pdf_is_name(pdf_dict_get(doc, oi, "S"), "GTS_PDFA1")) continue;
        seen++;
        dop = pdf_dict_get(doc, oi, "DestOutputProfile");
        if (!pdf_is_stream(dop)) return 0;
        /* Where more than one such intent is present they shall all have the
         * same destination profile. */
        if (profile && profile != dop) return 0;
        profile = dop;
    }
    *have_intent = seen > 0;
    return 1;
}

/* 6.9 / 6.4: /NeedAppearances shall be absent or false, and from part 2 the
 * interactive form dictionary shall carry no /XFA. */
static int clause_forms(pdfa_ctx *c)
{
    pdf_doc *doc = c->doc;
    pdf_obj *acro = pdf_dict_get(doc, pdf_catalog(doc), "AcroForm");
    pdf_obj *na;

    if (!pdf_is_dict(acro)) return 1;
    na = pdf_dict_get(doc, acro, "NeedAppearances");
    if (na->kind == PDF_BOOL && na->u.b) return 0;
    if (c->part >= 2 && !pdf_is_null(pdf_dict_get_raw(acro, "XFA"))) return 0;
    return 1;
}

/* 6.5.3 / 6.3.2: the annotation flags and appearance requirements. */
/* ISO 19005-4 6.3.1 refuses Sound, Screen, Movie, 3D, RichMedia and
 * FileAttachment, which parts 2 and 3 allow.  4e permits the engineering types
 * back and 4f the attachment type -- the whole of what those levels add.
 * Returns 1 permitted, 0 refused, -1 "not mine, use the shared list". */
static int annot_subtype_ok4(int level, const char *s)
{
    if (!strcmp(s, "3D") || !strcmp(s, "RichMedia")) return level == 'E';
    if (!strcmp(s, "FileAttachment"))                return level == 'F';
    if (!strcmp(s, "Sound") || !strcmp(s, "Screen") ||
        !strcmp(s, "Movie") || !strcmp(s, "TrapNet")) return 0;
    return -1;
}

static int annot_subtype_ok(int part, const char *s)
{
    static const char *const common[] = {
        "Text", "Link", "FreeText", "Line", "Square", "Circle", "Highlight",
        "Underline", "Squiggly", "StrikeOut", "Stamp", "Ink", "Popup",
        "Widget", "PrinterMark", "TrapNet", NULL
    };
    int i;

    for (i = 0; common[i]; i++) if (!strcmp(s, common[i])) return 1;
    if (part >= 2) {
        static const char *const later[] = {
            "Polygon", "PolyLine", "Caret", "FileAttachment", "Redact",
            "Watermark", "3D", "Projection", NULL
        };

        for (i = 0; later[i]; i++) if (!strcmp(s, later[i])) return 1;
    }
    return 0;
}

static int clause_annots(pdfa_ctx *c)
{
    pdf_doc *doc = c->doc;
    int np = pdf_page_count(doc), pi;

    for (pi = 0; pi < np; pi++) {
        pdf_obj *page = pdf_page_dict(doc, pi);
        pdf_obj *annots = pdf_dict_get(doc, page, "Annots");
        size_t i, n;

        if (!pdf_is_array(annots)) continue;
        n = pdf_array_len(annots);
        for (i = 0; i < n; i++) {
            pdf_obj *a = pdf_array_get(doc, annots, i), *st, *f, *ap;
            const char *sub;
            long long flags;

            if (!pdf_is_dict(a)) continue;
            st = pdf_dict_get(doc, a, "Subtype");
            if (!pdf_is_name(st, NULL)) continue;
            sub = pdf_to_name(st);
            if (c->part == 4) {
                int d4 = annot_subtype_ok4(c->level, sub);

                if (d4 == 0 || (d4 < 0 && !annot_subtype_ok(c->part, sub))) {
                    pdfa_reason = "annot subtype"; return 0;
                }
            } else if (!annot_subtype_ok(c->part, sub)) {
                pdfa_reason = "annot subtype"; return 0;
            }

            /* 6.3.2: every annotation EXCEPT /Popup shall carry /F.  The flag
             * requirements below are ISO 19005-1's and -2's; part 4 asks only
             * that the entry be present. */
            if (strcmp(sub, "Popup")) {
                f = pdf_dict_get(doc, a, "F");
                if (!pdf_is_int(f)) { pdfa_reason = "annot /F missing"; return 0; }
                flags = pdf_to_int(f);
                if (c->part < 4) {
                    if (!(flags & 4))  { pdfa_reason = "annot Print clear"; return 0; }
                    if (flags & 2)     { pdfa_reason = "annot Hidden"; return 0; }
                    if (flags & 32)    { pdfa_reason = "annot NoView"; return 0; }
                    if (flags & 1)     { pdfa_reason = "annot Invisible"; return 0; }
                }
            }

            /* 6.3.3: every annotation shall have an appearance, with exactly two
             * exceptions -- a degenerate /Rect, and /Popup, /Link or
             * /Projection. */
            if (!strcmp(sub, "Popup") || !strcmp(sub, "Link") ||
                !strcmp(sub, "Projection")) continue;
            {
                pdf_obj *r = pdf_dict_get(doc, a, "Rect");

                if (pdf_is_array(r) && pdf_array_len(r) == 4) {
                    double r1 = pdf_to_real(pdf_array_get(doc, r, 0));
                    double r2 = pdf_to_real(pdf_array_get(doc, r, 1));
                    double r3 = pdf_to_real(pdf_array_get(doc, r, 2));
                    double r4 = pdf_to_real(pdf_array_get(doc, r, 3));

                    if (r1 == r3 && r2 == r4) continue;
                }
            }
            ap = pdf_dict_get(doc, a, "AP");
            if (!pdf_is_dict(ap)) { pdfa_reason = "annot no /AP"; return 0; }
            if (pdf_is_null(pdf_dict_get(doc, ap, "N"))) { pdfa_reason = "annot no /AP /N"; return 0; }
        }
    }
    return 1;
}

/* ------------------------------------------------------------ validate ----- */

int pdfa_validate(pdf_doc *doc, const pdfa_flavour *fl)
{
    pdfa_ctx c;
    pdfa_xmp x;
    int have_intent = 0;

    pdfa_reason = "";
    memset(&c, 0, sizeof c);
    c.doc = doc;
    c.std = fl->std;
    c.part = fl->part;
    c.level = fl->level;

    /* Encryption is prohibited in every part and at every level; there is no
     * permitted algorithm and no empty-user-password exemption, because the
     * prohibition is on the presence of the encryption dictionary rather than
     * on the difficulty of reading through it. */
    if (pdf_is_encrypted(doc)) { pdfa_reason = "encryption"; return PDFA_FAIL; }

    pdfa_xmp_read(doc, &x);
    scan_all_objects(&c);

    if (!pdf_is_null(pdf_dict_get(doc, pdf_catalog(doc), "AA")))
        c.has_aa = 1;
    {
        pdf_obj *names = pdf_dict_get(doc, pdf_catalog(doc), "Names");

        if (pdf_is_dict(names)) {
            if (!pdf_is_null(pdf_dict_get_raw(names, "JavaScript")))
                c.has_js_tree = 1;
            if (!pdf_is_null(pdf_dict_get_raw(names, "EmbeddedFiles")))
                c.embedded_files = 1;
        }
    }

    if (c.std == 14289) {
        /* ISO 14289-1 asks a different question from ISO 19005: not whether the
         * file will survive an archive but whether its content is DESCRIBED.
         * Only the decidable subset SPEC 3.12 fixes is answered here. */
        pdf_obj *cat = pdf_catalog(doc);
        pdf_obj *sr  = pdf_dict_get(doc, cat, "StructTreeRoot");
        pdf_obj *mi, *vp, *dt, *md;

        if (x.have_ua_part != 1) { pdfa_reason = "ua pdfuaid:part"; return PDFA_FAIL; }
        if (!pdf_is_dict(sr)) { pdfa_reason = "ua no StructTreeRoot"; return PDFA_FAIL; }
        if (!pdfa_rolemap_sound(doc, sr)) { pdfa_reason = "ua role map"; return PDFA_FAIL; }

        md = pdf_dict_get(doc, cat, "Metadata");
        if (!pdf_is_stream(md)) { pdfa_reason = "ua no /Metadata"; return PDFA_FAIL; }
        if (!pdf_is_name(pdf_dict_get(doc, md, "Type"), "Metadata"))
            return PDFA_FAIL;
        if (!pdf_is_name(pdf_dict_get(doc, md, "Subtype"), "XML"))
            return PDFA_FAIL;
        /* dc:title shall be there and shall identify the document; that it
         * EXISTS is decidable, that it identifies anything is not. */
        if (!x.title[0]) { pdfa_reason = "ua no dc:title"; return PDFA_FAIL; }

        vp = pdf_dict_get(doc, cat, "ViewerPreferences");
        dt = pdf_is_dict(vp) ? pdf_dict_get(doc, vp, "DisplayDocTitle")
                             : pdf_null_obj();
        if (dt->kind != PDF_BOOL || !dt->u.b) { pdfa_reason = "ua DisplayDocTitle"; return PDFA_FAIL; }

        mi = pdf_dict_get(doc, cat, "MarkInfo");
        if (pdf_is_dict(mi)) {
            pdf_obj *sus = pdf_dict_get(doc, mi, "Suspects");

            if (sus->kind == PDF_BOOL && sus->u.b) return PDFA_FAIL;
        }
        /* The reference checker enforces this although SPEC section 3.11
         * calls it unenforced; the check follows the reference's verdicts. */
        if (c.bad_action || c.bad_named) { pdfa_reason = "ua actions";
                                           return PDFA_FAIL; }
        return PDFA_PASS;
    }

    if (c.part == 4) {
        pdf_obj *tr = pdf_trailer(doc);
        pdf_obj *cat4 = pdf_catalog(doc);
        pdf_obj *vers = pdf_dict_get(doc, cat4, "Version");
        pdf_obj *inf = pdf_dict_get(doc, tr, "Info");

        /* 6.1.12 t1: where /Version is present in the catalogue its value shall
         * be "2.n" -- part 4 is built on PDF 2.0 and says so twice over. */
        if (pdf_is_name(vers, NULL)) {
            const char *v = pdf_to_name(vers);

            if (strlen(v) != 3 || v[0] != '2' || v[1] != '.' ||
                v[2] < '0' || v[2] > '9')
                return PDFA_FAIL;
        }
        /* 6.1.3 t4: /Info shall not be in the trailer unless the catalogue has
         * a /PieceInfo; t5: if a document information dictionary IS present it
         * shall carry ModDate and nothing else. */
        if (pdf_is_dict(inf)) {
            if (pdf_is_null(pdf_dict_get(doc, cat4, "PieceInfo")))
                return PDFA_FAIL;
            if (pdf_dict_len(inf) != 1 ||
                pdf_is_null(pdf_dict_get_raw(inf, "ModDate")))
                return PDFA_FAIL;
        }
    }

    if (c.part >= 3) {
        /* Part 3 is part 2 with one door opened, and part 4 restates the same
         * conditions in its own terms: every embedded file's specification
         * shall carry /AFRelationship, a MIME /Subtype and non-empty /F and
         * /UF.  Plain part 4 keeps part 2's restriction on what may be
         * embedded; 4f announces the permission with /EmbeddedFiles. */
        if (!pdfa_embedded_files_ok(doc, c.part)) { pdfa_reason = "embedded files"; return PDFA_FAIL; }
        if (c.part == 4 && c.level == 'F' && !c.embedded_files)
            return PDFA_FAIL;
    }

    if (!clause_header(&c))            { pdfa_reason = "6.1.2 header";      return PDFA_FAIL; }
    if (!clause_trailer_xref(&c))      { pdfa_reason = "6.1.3/6.1.4 trailer"; return PDFA_FAIL; }
    if (!clause_identification(&c, &x)){ pdfa_reason = "identification";     return PDFA_FAIL; }
    if (!clause_info_agreement(&c, &x)){ pdfa_reason = "info/XMP agreement"; return PDFA_FAIL; }
    if (!clause_output_intent(&c, &have_intent)) { pdfa_reason = "output intent"; return PDFA_FAIL; }
    if (!clause_forms(&c))             { pdfa_reason = "forms";              return PDFA_FAIL; }
    if (c.part >= 2) {
        /* 6.1.12 t1: no key other than UR3 and DocMDP in the permissions
         * dictionary (ISO 32000-1 12.8.4 Table 258). */
        pdf_obj *perms = pdf_dict_get(doc, pdf_catalog(doc), "Perms");

        if (pdf_is_dict(perms)) {
            size_t pi, pn = pdf_dict_len(perms);

            for (pi = 0; pi < pn; pi++) {
                const char *k = pdf_dict_key(perms, pi);

                if (k && strcmp(k, "UR3") && strcmp(k, "DocMDP")) {
                    pdfa_reason = "6.1.12 /Perms keys";
                    return PDFA_FAIL;
                }
            }
        }
    }
    if (!clause_annots(&c))            { if (!pdfa_reason[0]) pdfa_reason = "annotations";
                                         return PDFA_FAIL; }

    if (c.bad_action || c.bad_named)   { pdfa_reason = "actions";            return PDFA_FAIL; }
    if (c.has_aa || c.has_js_tree)     { pdfa_reason = "triggers/JS tree";   return PDFA_FAIL; }
    if (c.lzw)                         { pdfa_reason = "LZWDecode";          return PDFA_FAIL; }
    if (c.limit)                       { pdfa_reason = "implementation limit"; return PDFA_FAIL; }

    /* ISO 19005-1 forbids transparency outright and forbids /EmbeddedFiles in
     * the name tree entirely; ISO 19005-2 permits both, subject to conditions
     * section 7 of the SPEC states.  A validator that shared one rule table between
     * the two parts would get exactly these wrong. */
    if (c.part == 1 && c.transparency)   { pdfa_reason = "transparency"; return PDFA_FAIL; }
    if (c.part == 1 && c.embedded_files) { pdfa_reason = "1: /EmbeddedFiles"; return PDFA_FAIL; }

    return PDFA_PASS;
}


/* --------------------------------------------------- structure tree walks --- */

int pdfa_is_standard_structtype(const char *name)
{
    static const char *const std[] = {
        "Document", "Part", "Art", "Sect", "Div", "BlockQuote", "Caption",
        "TOC", "TOCI", "Index", "NonStruct", "Private", "P", "H", "H1", "H2",
        "H3", "H4", "H5", "H6", "L", "LI", "Lbl", "LBody", "Table", "TR", "TH",
        "TD", "THead", "TBody", "TFoot", "Span", "Quote", "Note", "Reference",
        "BibEntry", "Code", "Link", "Annot", "Ruby", "RB", "RT", "RP",
        "Warichu", "WT", "WP", "Figure", "Formula", "Form", NULL
    };
    int i;

    for (i = 0; std[i]; i++) if (!strcmp(name, std[i])) return 1;
    return 0;
}

/* The mapping may be indirect -- a non-standard type may map to another
 * non-standard type -- but it shall TERMINATE at a standard type, a circular
 * mapping shall not exist, and a standard type shall not be remapped. */
int pdfa_rolemap_sound(pdf_doc *doc, pdf_obj *structroot)
{
    pdf_obj *rm = pdf_dict_get(doc, structroot, "RoleMap");
    size_t i, n;

    if (!pdf_is_dict(rm)) return 1;
    n = pdf_dict_len(rm);
    for (i = 0; i < n; i++) {
        const char *key = pdf_dict_key(rm, i);
        pdf_obj *v = pdf_resolve(doc, pdf_dict_val_raw(rm, i));
        int hops = 0;

        if (!key) continue;
        if (pdfa_is_standard_structtype(key)) return 0;   /* remapped */
        while (pdf_is_name(v, NULL) && hops++ < 64) {
            const char *t = pdf_to_name(v);

            if (pdfa_is_standard_structtype(t)) break;
            v = pdf_dict_get(doc, rm, t);
        }
        if (hops >= 64) return 0;                         /* cycle */
        if (!pdf_is_name(v, NULL) ||
            !pdfa_is_standard_structtype(pdf_to_name(v))) return 0;
    }
    return 1;
}

/* ----------------------------------------------------- associated files ---- */

static int filespec_ok(pdf_doc *doc, pdf_obj *fs, int part)
{
    pdf_obj *ef, *f, *uf, *rel, *sub;
    size_t n;

    if (!pdf_is_dict(fs)) return 1;
    ef = pdf_dict_get(doc, fs, "EF");
    if (!pdf_is_dict(ef)) return 1;      /* not an embedded file spec */

    /* Every embedded file's specification shall contain /AFRelationship.  A
     * name outside the defined set is treated as /Unspecified rather than as an
     * error, but the ENTRY shall be present -- which is the requirement a
     * converter most often omits. */
    rel = pdf_dict_get(doc, fs, "AFRelationship");
    if (!pdf_is_name(rel, NULL)) return 0;

    f  = pdf_dict_get(doc, fs, "F");
    uf = pdf_dict_get(doc, fs, "UF");
    if (!pdf_is_string(f)  || !pdf_to_string(f, &n)  || n == 0) return 0;
    if (!pdf_is_string(uf) || !pdf_to_string(uf, &n) || n == 0) return 0;

    /* The stream dictionary shall carry a MIME type in /Subtype, falling back
     * to application/octet-stream; an absent entry is a defect even where the
     * data's type is genuinely unknown. */
    {
        size_t i, m = pdf_dict_len(ef);

        for (i = 0; i < m; i++) {
            pdf_obj *stm = pdf_resolve(doc, pdf_dict_val_raw(ef, i));

            if (!pdf_is_stream(stm)) continue;
            sub = pdf_dict_get(doc, stm, "Subtype");
            if (!pdf_is_name(sub, NULL)) return 0;
        }
    }
    return 1;
}

static int walk_af(pdf_doc *doc, pdf_obj *holder, int part)
{
    pdf_obj *af = pdf_dict_get(doc, holder, "AF");
    size_t i, n;

    if (!pdf_is_array(af)) return 1;
    n = pdf_array_len(af);
    for (i = 0; i < n; i++)
        if (!filespec_ok(doc, pdf_array_get(doc, af, i), part)) return 0;
    return 1;
}

/* The /EmbeddedFiles name tree, whose leaves are [name filespec] pairs. */
static int walk_name_tree(pdf_doc *doc, pdf_obj *node, int part, int depth)
{
    pdf_obj *names, *kids;
    size_t i, n;

    if (depth > 32 || !pdf_is_dict(node)) return 1;
    names = pdf_dict_get(doc, node, "Names");
    if (pdf_is_array(names)) {
        n = pdf_array_len(names);
        for (i = 1; i < n; i += 2)
            if (!filespec_ok(doc, pdf_array_get(doc, names, i), part)) return 0;
    }
    kids = pdf_dict_get(doc, node, "Kids");
    if (pdf_is_array(kids)) {
        n = pdf_array_len(kids);
        for (i = 0; i < n; i++)
            if (!walk_name_tree(doc, pdf_array_get(doc, kids, i), part,
                                depth + 1)) return 0;
    }
    return 1;
}

int pdfa_embedded_files_ok(pdf_doc *doc, int part)
{
    pdf_obj *cat = pdf_catalog(doc);
    pdf_obj *names = pdf_dict_get(doc, cat, "Names");
    int np, pi;

    if (!walk_af(doc, cat, part)) return 0;
    if (pdf_is_dict(names) &&
        !walk_name_tree(doc, pdf_dict_get(doc, names, "EmbeddedFiles"),
                        part, 0))
        return 0;

    np = pdf_page_count(doc);
    for (pi = 0; pi < np; pi++)
        if (!walk_af(doc, pdf_page_dict(doc, pi), part)) return 0;
    return 1;
}
