/* xref.c -- file loading, cross-reference tables, object loading, recovery.
 *
 * ISO 32000-1 clause 7.5 (File Structure):
 *   7.5.2  file header            7.5.4  cross-reference table
 *   7.5.5  file trailer
 */
/*   7.5.7  object streams
 *   7.5.8  cross-reference streams (7.5.8.4 hybrid-reference files)
 */
/* Everything here is defensive: the cross-reference machinery is the part of a
 * PDF that real files most often get wrong, so every table read is allowed to
 * fail and fall back on pdf_xref_reconstruct(), which rebuilds the table by
 * scanning the whole file for "N G obj".
 */

#include "pdfint.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ limits */

#define XR_MAX_OBJNUM     8388607   /* ISO 32000-1 Annex C.2 architectural limit */
#define XR_MAX_CHAIN      64        /* /Prev hops before we give up              */
#define XR_MAX_SUBSEC     65536     /* subsections in one classic section         */
#define XR_MAX_W          8         /* entries of /W we are willing to look at    */
#define XR_HDR_WINDOW     1024      /* 7.5.2: header lives in the first 1 KiB     */
#define XR_TAIL_WINDOW    2048      /* 7.5.5: startxref lives near EOF            */
#define XR_MAX_REF_DEPTH  64        /* ref -> ref -> ... chain cap                */
#define XR_ENDSTM_SLACK   96        /* whitespace tolerated before "endstream"    */
#define XR_MAX_OBJSTM_N   1048576   /* objects in one /Type/ObjStm                */
#define XR_PEEK           4096      /* bytes of an object body we sniff           */
#define XR_TRAILER_SEEK   8192      /* forward search for "trailer" after a table */

/* ------------------------------------------------------------- byte helpers */

static int xr_isws(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0;
}

static int xr_isdelim(int c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
           c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

static int xr_isdig(int c) { return c >= '0' && c <= '9'; }

/* memmem() is not in C11; roll our own on top of memchr(). */
static const unsigned char *xr_find(const unsigned char *hay, size_t n,
                                    const char *needle, size_t m)
{
    const unsigned char *p = hay;
    size_t left = n;

    if (m == 0 || m > n) return NULL;
    while (left >= m) {
        const unsigned char *q = memchr(p, (unsigned char)needle[0], left - m + 1);
        if (!q) return NULL;
        if (memcmp(q, needle, m) == 0) return q;
        left -= (size_t)(q - p) + 1;
        p = q + 1;
    }
    return NULL;
}

static const unsigned char *xr_rfind(const unsigned char *hay, size_t n,
                                     const char *needle, size_t m)
{
    size_t i;

    if (m == 0 || m > n) return NULL;
    for (i = n - m + 1; i-- > 0; )
        if (hay[i] == (unsigned char)needle[0] && memcmp(hay + i, needle, m) == 0)
            return hay + i;
    return NULL;
}

static char *xr_dup(const char *s)
{
    size_t n;
    char *d;

    if (!s) return NULL;
    n = strlen(s) + 1;
    d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static void xr_errmsg(char **errmsg, const char *msg)
{
    if (errmsg && !*errmsg) *errmsg = xr_dup(msg);
}

/* ------------------------------------------------------------- mini scanner
 *
 * The xref table is not PDF object syntax, so it is read with a private
 * scanner rather than the shared lexer: that keeps table recovery independent
 * of how forgiving pdf_lex_next() happens to be, and lets us accept 19-, 20-
 * and 21-byte entries plus a missing final EOL without special cases.
 */

typedef struct { const unsigned char *p; size_t n, pos; } xr_scan;

static void xr_skipws(xr_scan *s)
{
    while (s->pos < s->n) {
        unsigned char c = s->p[s->pos];
        if (xr_isws(c)) { s->pos++; continue; }
        if (c == '%') {
            while (s->pos < s->n && s->p[s->pos] != '\n' && s->p[s->pos] != '\r')
                s->pos++;
            continue;
        }
        break;
    }
}

static int xr_int(xr_scan *s, long long *out)
{
    long long v = 0;
    int neg = 0, dig = 0;

    xr_skipws(s);
    if (s->pos < s->n && (s->p[s->pos] == '+' || s->p[s->pos] == '-')) {
        neg = s->p[s->pos] == '-';
        s->pos++;
    }
    while (s->pos < s->n && xr_isdig(s->p[s->pos])) {
        if (v < 100000000000000000LL) v = v * 10 + (s->p[s->pos] - '0');
        s->pos++;
        dig++;
    }
    if (!dig) return 0;
    *out = neg ? -v : v;
    return 1;
}

static int xr_word(xr_scan *s, char *buf, size_t cap)
{
    size_t k = 0;

    xr_skipws(s);
    while (s->pos < s->n && !xr_isws(s->p[s->pos]) && !xr_isdelim(s->p[s->pos])) {
        if (k + 1 < cap) buf[k] = (char)s->p[s->pos];
        k++;
        s->pos++;
    }
    buf[k < cap ? k : cap - 1] = 0;
    return k > 0;
}

/* ------------------------------------------------------------------ header */

/* 7.5.2.  The header need not sit at offset 0; when it does not, every offset
 * in the file is relative to it.  pdf_doc has no field for the header
 * position, so it is recomputed on demand (a memchr over at most 1 KiB). */
static long long xr_hdr_off(pdf_doc *doc)
{
    size_t win = doc->len < XR_HDR_WINDOW ? doc->len : XR_HDR_WINDOW;
    const unsigned char *q = xr_find(doc->data, win, "%PDF-", 5);
    return q ? (long long)(q - doc->data) : 0;
}

static int xr_ver_at(const unsigned char *p, size_t n)
{
    if (n < 8 || !xr_isdig(p[5]) || p[6] != '.' || !xr_isdig(p[7])) return 0;
    return (p[5] - '0') * 10 + (p[7] - '0');
}

/* "1.7" / "2.0" as written in the catalog's /Version name (7.5.2). */
static int xr_ver_name(const char *s)
{
    if (!s || !xr_isdig((unsigned char)s[0]) || s[1] != '.' ||
        !xr_isdig((unsigned char)s[2]))
        return 0;
    return (s[0] - '0') * 10 + (s[2] - '0');
}

/* ---------------------------------------------------------------- xref map */

int pdf_xref_ensure(pdf_doc *doc, int num)
{
    pdf_xrefent *ne;
    int newcap;

    if (!doc) return PDF_E_RANGE;
    if (num < 0 || num > XR_MAX_OBJNUM) return PDF_E_RANGE;
    if (num < doc->xrefcap) return 0;

    newcap = doc->xrefcap ? doc->xrefcap * 2 : 64;
    if (newcap <= num) newcap = num + 1;
    if (newcap > XR_MAX_OBJNUM + 1) newcap = XR_MAX_OBJNUM + 1;

    ne = realloc(doc->xref, (size_t)newcap * sizeof(*ne));
    if (!ne) {
        pdf_set_error(doc, "out of memory growing the xref table");
        return PDF_E_NOMEM;
    }
    memset(ne + doc->xrefcap, 0,
           (size_t)(newcap - doc->xrefcap) * sizeof(*ne));
    doc->xref = ne;
    doc->xrefcap = newcap;
    return 0;
}

/* Record one entry.  Unless "force" is set an occupied slot is never
 * overwritten: sections are visited newest-first, so the first writer wins and
 * older /Prev sections cannot clobber a newer definition.  Free entries are
 * never recorded at all. */
/* That last rule is also what makes hybrid files work: the classic table marks
 * every compressed object free and the /XRefStm has the truth. */
static int xr_set(pdf_doc *doc, long long num, int type, long long off,
                  int gen, int stmnum, int stmidx, int force)
{
    pdf_xrefent *e;

    if (num < 0 || num > XR_MAX_OBJNUM) return 0;
    if (type == PDF_XREF_FREE) return 0;
    if (pdf_xref_ensure(doc, (int)num) < 0) return PDF_E_NOMEM;

    e = &doc->xref[num];
    if (!force && e->type != PDF_XREF_FREE) return 0;

    e->type    = type;
    e->off     = off;
    e->gen     = gen;
    e->stmnum  = stmnum;
    e->stmidx  = stmidx;
    e->cached  = NULL;
    /* e->loading is deliberately left alone: a rebuild may land here while the
     * very object it describes is half-parsed further up the stack, and the
     * in-progress flag is what stops that from recursing. */
    if ((int)num > doc->maxobj) doc->maxobj = (int)num;
    return 0;
}

/* --------------------------------------------------------- extra limits */

#define XR_MAX_FILE     ((size_t)1u << 31)  /* 2 GiB of PDF is already absurd */
#define XR_MAX_ROW      32          /* bytes in one /W row                    */
#define XR_MAX_INDEX    4096        /* /Index pairs we are willing to read    */
#define XR_MAX_STARTXREF 4          /* startxref candidates tried from EOF    */
#define XR_MAX_SCAN_OBJ 4194304     /* objects one reconstruction may record  */

/* xrefent.loading is a bitfield, not a bool. */
#define XR_LOADING      1u          /* this object is being parsed right now  */
#define XR_EXPANDED     2u          /* this /Type/ObjStm has been unpacked    */

/* mem.c.  Not in pdfint.h because nothing outside this file calls it. */
void pdf_arena_free(pdf_doc *doc);

static int      xr_section(pdf_doc *doc, long long off, int depth,
                           long long *seen, int *nseen);
static int      xr_expand_objstm(pdf_doc *doc, int stmnum);
static pdf_obj *xr_parse_at(pdf_doc *doc, long long off, int *numout, int *genout);
static void     xr_merge_trailer(pdf_doc *doc, pdf_obj *tr);

/* ------------------------------------------------------------ file loading */

/* Read the whole file.  st_size is only a hint -- it is wrong for /proc, for
 * files being appended to, and for anything behind a FUSE mount -- so the loop
 * keeps reading until fread() stops producing bytes. */
static int xr_slurp(const char *path, unsigned char **datap, size_t *lenp)
{
    FILE *fp;
    unsigned char *buf, *nb;
    size_t cap = 0, n = 0;
    long end = -1;

    *datap = NULL;
    *lenp  = 0;

    fp = fopen(path, "rb");
    if (!fp) return PDF_E_IO;

    if (fseek(fp, 0, SEEK_END) == 0) {
        end = ftell(fp);
        if (end > 0 && (unsigned long)end < XR_MAX_FILE) cap = (size_t)end;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return PDF_E_IO; }
    if (cap < 4096) cap = 4096;

    buf = malloc(cap + 1);
    if (!buf) { fclose(fp); return PDF_E_NOMEM; }

    for (;;) {
        size_t got;
        if (n == cap) {                         /* the hint was too small */
            if (cap >= XR_MAX_FILE) break;
            cap = cap > XR_MAX_FILE / 2 ? XR_MAX_FILE : cap * 2;
            nb = realloc(buf, cap + 1);
            if (!nb) { free(buf); fclose(fp); return PDF_E_NOMEM; }
            buf = nb;
        }
        got = fread(buf + n, 1, cap - n, fp);
        n += got;
        if (got == 0) break;
    }
    if (ferror(fp)) { free(buf); fclose(fp); return PDF_E_IO; }
    fclose(fp);

    buf[n] = 0;                     /* never read, but keeps debuggers honest */
    *datap = buf;
    *lenp  = n;
    return 0;
}

/* ---------------------------------------------------------- open and close */

pdf_doc *pdf_open(const char *path, char **errmsg)
{
    pdf_doc *doc;
    long long hdr;

    if (errmsg) *errmsg = NULL;
    if (!path) {
        xr_errmsg(errmsg, "no file name given");
        return NULL;
    }

    doc = calloc(1, sizeof *doc);
    if (!doc) {
        xr_errmsg(errmsg, "out of memory");
        return NULL;
    }

    switch (xr_slurp(path, &doc->data, &doc->len)) {
    case 0:
        break;
    case PDF_E_NOMEM:
        xr_errmsg(errmsg, "out of memory reading the file");
        pdf_close(doc);
        return NULL;
    default:
        xr_errmsg(errmsg, "cannot read the file");
        pdf_close(doc);
        return NULL;
    }
    doc->path = xr_dup(path);
    if (doc->len == 0) {
        xr_errmsg(errmsg, "the file is empty");
        pdf_close(doc);
        return NULL;
    }

    /* 7.5.2.  The header may sit anywhere in the first kibibyte; when it is
     * not at zero every stored offset is relative to it, which xr_section()
     * and pdf_load_object() deal with by trying both readings. */
    hdr = xr_hdr_off(doc);
    if (doc->len - (size_t)hdr >= 5 &&
        memcmp(doc->data + hdr, "%PDF-", 5) == 0)
        doc->hdrver = xr_ver_at(doc->data + hdr, doc->len - (size_t)hdr);
    if (doc->hdrver == 0)
        doc->damaged = 1;                       /* no %PDF-x.y anywhere */
    doc->ver = doc->hdrver;

    if (pdf_xref_load(doc) != 0)
        pdf_xref_reconstruct(doc);
    if (!pdf_is_dict(doc->trailer) ||
        pdf_is_null(pdf_dict_get_raw(doc->trailer, "Root")))
        pdf_xref_reconstruct(doc);

    /* 7.6.1.  Without crypt.c we can still see that a file is encrypted, and
     * an encrypted file still parses -- only its strings and streams are
     * unreadable -- so this is not a reason to refuse to open it. */
    doc->encrypted = !pdf_is_null(pdf_dict_get_raw(doc->trailer, "Encrypt"));
    doc->unlocked  = !doc->encrypted;
    /* 7.6.1.  The encryption dictionary is itself never encrypted, and it has
     * already been parsed and cached by the line above with no handler in
     * place, so it stays plaintext no matter what the handler then does.  The
     * trailer's /ID is exempt for the same reason and feeds Algorithm 2. */
    if (doc->encrypted)
        (void)pdf_crypt_init(doc, pdf_dict_get(doc, doc->trailer, "Encrypt"),
                             pdf_dict_get(doc, doc->trailer, "ID"));

    if (doc->maxobj <= 0 && !pdf_is_dict(doc->trailer)) {
        xr_errmsg(errmsg, "not a PDF file: no objects and no trailer");
        pdf_set_error(doc, "not a PDF file");
        pdf_close(doc);
        return NULL;
    }

    /* 7.7.2.  A document IS its catalog: with no /Root there is no page tree,
     * no /Version and nothing a reader can describe.  pdf_catalog() has
     * already had all three of its chances by the time it answers -- the
     * trailer's /Root, a rebuilt cross-reference table, then a scan for a
     * /Type /Catalog object anywhere in the file -- so a null here means the
     * catalog is genuinely absent, and the file does not open.  Opening it
     * anyway is what produced the nonsense of a document with zero pages that
     * then failed on its page range; every independent reader refuses at this
     * point instead.  The message is qpdf's, because qpdf is the tool that
     * prints it. */
    if (pdf_is_null(pdf_catalog(doc))) {
        xr_errmsg(errmsg, "unable to find /Root dictionary");
        pdf_set_error(doc, "unable to find /Root dictionary");
        pdf_close(doc);
        return NULL;
    }
    return doc;
}

void pdf_close(pdf_doc *doc)
{
    if (!doc) return;
    free(doc->xref);
    free(doc->pages);
    free(doc->data);
    free(doc->path);
    pdf_arena_free(doc);            /* every pdf_obj dies here; must be last */
    free(doc);
}

/* --------------------------------------------------------- trivial getters */

long long pdf_file_size(pdf_doc *doc)
{
    return doc ? (long long)doc->len : 0;
}

int pdf_header_version(pdf_doc *doc)
{
    return doc ? doc->hdrver : 0;
}

int pdf_max_objnum(pdf_doc *doc)
{
    return doc ? doc->maxobj : 0;
}

pdf_obj *pdf_trailer(pdf_doc *doc)
{
    if (!doc || !doc->trailer) return pdf_null_obj();
    return doc->trailer;
}

/* 7.5.2: from PDF 1.4 on, a /Version name in the catalog overrides the header.
 * Files in the wild only ever use it to raise the version, so take the larger
 * of the two and never let a catalog talk the version back down. */
int pdf_version(pdf_doc *doc)
{
    int cv;

    if (!doc) return 0;
    cv = xr_ver_name(pdf_to_name(pdf_resolve(doc,
             pdf_dict_get_raw(pdf_catalog(doc), "Version"))));
    if (cv > doc->ver) doc->ver = cv;
    return doc->ver;
}

/* --------------------------------------------------------- trailer merging */

/* Sections are visited newest-first, so a key already present came from a
 * newer section and must not be replaced.  Keys that describe one section
 * rather than the document are never copied: /Type, /Length, /Filter and
 * friends have no business in a trailer. */
/* The same list covers a cross-reference stream, whose own dictionary doubles
 * as the trailer (7.5.8.2) and carries /W, /Index and /XRefStm besides. */
static void xr_merge_trailer(pdf_doc *doc, pdf_obj *tr)
{
    static const char *const skip[] = {
        "Prev",
        "XRefStm",
        "Type", "Length", "Filter", "DecodeParms",
        "W", "Index",
        "F", "DL", NULL
    };
    size_t i, n;
    int j;

    if (!pdf_is_dict(tr)) return;
    if (!pdf_is_dict(doc->trailer)) {
        doc->trailer = pdf_new_dict(doc);
        if (!pdf_is_dict(doc->trailer)) return;
    }
    if (doc->trailer == tr) return;

    n = pdf_dict_len(tr);
    for (i = 0; i < n; i++) {
        const char *k = pdf_dict_key(tr, i);
        if (!k) continue;
        for (j = 0; skip[j]; j++)
            if (strcmp(k, skip[j]) == 0) break;
        if (skip[j]) continue;
        if (!pdf_is_null(pdf_dict_get_raw(doc->trailer, k)))
            continue;                   /* a newer section already said this */
        pdf_dict_put(doc, doc->trailer, k, pdf_dict_val_raw(tr, i));
    }
}

/* ------------------------------------------------------- object at offset */

/* Parse the indirect object whose "N G obj" header begins at `off`.  The
 * header is read with the private scanner so the number can be checked
 * against what the table promised before the body is trusted. */
static pdf_obj *xr_parse_at(pdf_doc *doc, long long off, int *numout, int *genout)
{
    xr_scan s;
    pdf_lexer lx;
    pdf_obj *o;
    long long n, g;
    char kw[16];

    if (numout) *numout = -1;
    if (genout) *genout = 0;
    if (off < 0 || (unsigned long long)off >= (unsigned long long)doc->len)
        return pdf_null_obj();

    s.p = doc->data;
    s.n = doc->len;
    s.pos = (size_t)off;
    if (!xr_int(&s, &n) || !xr_int(&s, &g)) return pdf_null_obj();
    if (!xr_word(&s, kw, sizeof kw) || strcmp(kw, "obj") != 0)
        return pdf_null_obj();
    if (n < 0 || n > XR_MAX_OBJNUM) return pdf_null_obj();
    if (g < 0 || g > 65535) g = 0;

    if (numout) *numout = (int)n;
    if (genout) *genout = (int)g;

    pdf_lex_init(&lx, doc->data, doc->len, s.pos);
    o = pdf_parse_obj(doc, &lx);
    if (!o) return pdf_null_obj();
    if (o->kind == PDF_STREAM) {        /* for decryption, once crypt.c lands */
        o->u.stm.num = (int)n;
        o->u.stm.gen = (int)g;
    }
    /* 7.6.1.  Strings are decrypted here, where the containing object's number
     * and generation are still in hand; stream DATA is decrypted lazily, in
     * pdf_stream_data(), because it is usually never asked for at all. */
    pdf_crypt_decrypt_obj(doc, o, (int)n, (int)g);
    return o;
}

/* ----------------------------------------------------- classic xref table */

/* Does a classic table start at `pos`?  If so *after gets the offset just past
 * the "xref" keyword. */
static int xr_classic_at(pdf_doc *doc, size_t pos, size_t *after)
{
    xr_scan s;

    s.p = doc->data;
    s.n = doc->len;
    s.pos = pos;
    xr_skipws(&s);
    if (s.pos + 4 > doc->len) return 0;
    if (memcmp(doc->data + s.pos, "xref", 4) != 0) return 0;
    if (s.pos + 4 < doc->len && !xr_isws(doc->data[s.pos + 4]) &&
        !xr_isdelim(doc->data[s.pos + 4]))
        return 0;                                   /* a longer word, not "xref" */
    *after = s.pos + 4;
    return 1;
}

/* 7.5.4.  Subsection headers "start count" followed by one entry per object.
 * The entries are read token-wise rather than 20 bytes at a time, which is
 * what makes 19- and 21-byte entries and a missing final EOL work out. */
static int xr_classic(pdf_doc *doc, size_t pos, int depth,
                      long long *seen, int *nseen)
{
    xr_scan s;
    pdf_obj *tr = NULL;
    long long first, count, k;
    int nsub = 0, got = 0;

    s.p = doc->data;
    s.n = doc->len;
    s.pos = pos;

    for (;;) {
        size_t save;

        if (++nsub > XR_MAX_SUBSEC) break;
        xr_skipws(&s);
        save = s.pos;
        if (s.pos >= s.n || !xr_isdig(s.p[s.pos])) break;   /* "trailer"/junk */
        if (!xr_int(&s, &first) || !xr_int(&s, &count)) { s.pos = save; break; }
        if (first < 0) first = 0;
        if (count <= 0 || count > (long long)XR_MAX_OBJNUM + 1) {
            if (count != 0) { s.pos = save; break; }
            continue;                                     /* legal empty run */
        }

        for (k = 0; k < count; k++) {
            size_t before = s.pos;
            long long o, g;
            char kw[8];

            if (!xr_int(&s, &o) || !xr_int(&s, &g) ||
                !xr_word(&s, kw, sizeof kw)) {
                s.pos = before;
                break;
            }
            if (kw[0] == 'n' && kw[1] == 0) {
                if (o >= 0 && first + k <= XR_MAX_OBJNUM) {
                    if (xr_set(doc, first + k, PDF_XREF_NORMAL, o,
                               (g >= 0 && g <= 65535) ? (int)g : 0, 0, 0, 0) < 0)
                        return PDF_E_NOMEM;
                    got = 1;
                }
            } else if (kw[0] != 'f' || kw[1] != 0) {
                s.pos = before;             /* not an entry: end of the table */
                break;
            }
        }
        if (k < count) break;
    }

    /* 7.5.5.  "trailer" follows the last subsection. */
    {
        size_t lim = s.pos + XR_TRAILER_SEEK;
        const unsigned char *q;

        if (lim > doc->len) lim = doc->len;
        if (s.pos < lim) {
            q = xr_find(doc->data + s.pos, lim - s.pos, "trailer", 7);
            if (q) {
                pdf_lexer lx;
                pdf_lex_init(&lx, doc->data, doc->len,
                             (size_t)(q - doc->data) + 7);
                tr = pdf_parse_obj(doc, &lx);
            }
        }
    }
    if (!pdf_is_dict(tr)) return got ? 0 : PDF_E_SYNTAX;
    xr_merge_trailer(doc, tr);

    /* 7.5.8.4.  A hybrid file's classic table marks every compressed object
     * free and hides the truth in /XRefStm.  Reading it *after* the classic
     * entries is what makes the classic ones win, and free entries are never
     * recorded, so the stream fills exactly the holes the table left. */
    {
        pdf_obj *x = pdf_dict_get_raw(tr, "XRefStm");
        if (pdf_is_int(x))
            (void)xr_section(doc, pdf_to_int(x), depth + 1, seen, nseen);
    }
    {
        pdf_obj *p = pdf_dict_get_raw(tr, "Prev");
        if (pdf_is_int(p))
            (void)xr_section(doc, pdf_to_int(p), depth + 1, seen, nseen);
    }
    return 0;
}

/* ---------------------------------------------------- cross-reference stream */

/* One run of rows out of the decoded stream.  A truncated run is not an error:
 * whatever was readable has already been recorded. */
static int xr_rows(pdf_doc *doc, const unsigned char *p, size_t n, size_t *pos,
                   const long long *wid, size_t nw, size_t rowlen,
                   long long start, long long count, long long *nset)
{
    long long k;

    for (k = 0; k < count; k++) {
        unsigned long long f[XR_MAX_W];
        long long num = start + k, f1, f2;
        size_t i, j;
        int type;

        if (*pos > n || n - *pos < rowlen) return 0;    /* out of rows */
        memset(f, 0, sizeof f);
        for (i = 0; i < nw; i++) {
            unsigned long long v = 0;
            for (j = 0; j < (size_t)wid[i]; j++)
                v = (v << 8) | p[*pos + j];
            f[i] = v;
            *pos += (size_t)wid[i];
        }

        /* 7.5.8.2: a /W width of zero means the field is absent and its
         * default applies -- 1 for the type, 0 for everything else. */
        type = (wid[0] == 0) ? 1 : (f[0] > 2 ? -1 : (int)f[0]);
        f1 = (nw > 1 && f[1] <= (unsigned long long)LLONG_MAX) ? (long long)f[1] : -1;
        f2 = (nw > 2 && f[2] <= (unsigned long long)LLONG_MAX) ? (long long)f[2] : 0;

        if (num < 0 || num > XR_MAX_OBJNUM) continue;
        if (type == 1 && f1 >= 0) {
            if (xr_set(doc, num, PDF_XREF_NORMAL, f1,
                       (f2 >= 0 && f2 <= 65535) ? (int)f2 : 0, 0, 0, 0) < 0)
                return PDF_E_NOMEM;
            if (nset) (*nset)++;
        } else if (type == 2 && f1 >= 0 && f1 <= XR_MAX_OBJNUM) {
            if (xr_set(doc, num, PDF_XREF_INSTM, 0, 0, (int)f1,
                       (f2 >= 0 && f2 <= XR_MAX_OBJNUM) ? (int)f2 : 0, 0) < 0)
                return PDF_E_NOMEM;
            if (nset) (*nset)++;
        }
        /* type 0 (free) and any type we do not know are skipped */
    }
    return 0;
}

/* 7.5.8.  The stream dictionary is also the trailer for this section. */
static int xr_read_xrefstm(pdf_doc *doc, pdf_obj *stm, long long secoff,
                           int depth, long long *seen, int *nseen)
{
    pdf_buf data;
    pdf_obj *w, *idx, *o;
    long long wid[XR_MAX_W], size = 0, want = 0, nset = 0;
    size_t nw, i, pos = 0, rowlen = 0, npairs;

    w = pdf_dict_get_raw(stm, "W");                 /* 7.5.8.2: direct objects */
    if (!pdf_is_array(w)) return PDF_E_SYNTAX;
    nw = pdf_array_len(w);
    if (nw < 1) return PDF_E_SYNTAX;
    if (nw > XR_MAX_W) nw = XR_MAX_W;
    for (i = 0; i < nw; i++) {
        pdf_obj *e = pdf_array_get_raw(w, i);
        long long v = pdf_is_int(e) ? pdf_to_int(e) : 0;
        if (v < 0 || v > 8) v = 0;
        wid[i] = v;
        rowlen += (size_t)v;
    }
    if (rowlen == 0 || rowlen > XR_MAX_ROW) return PDF_E_SYNTAX;

    o = pdf_dict_get_raw(stm, "Size");
    if (pdf_is_int(o)) {
        size = pdf_to_int(o);
        if (size < 0 || size > (long long)XR_MAX_OBJNUM + 1)
            size = 0;                                       /* absurd: ignore */
    }

    pdf_buf_init(&data);
    if (pdf_stream_data(doc, stm, &data, 1) < 0 || data.n < rowlen) {
        pdf_buf_free(&data);
        return PDF_E_SYNTAX;
    }

    idx = pdf_dict_get_raw(stm, "Index");
    npairs = pdf_is_array(idx) ? pdf_array_len(idx) / 2 : 0;
    if (npairs > XR_MAX_INDEX) npairs = XR_MAX_INDEX;

    /* 7.5.8.2.  /W and the counts in /Index between them state exactly how many
     * bytes of table there must be.  How much a disagreement matters is decided
     * below, once it is known what the rows actually yielded. */
    {
        size_t k;

        if (npairs == 0) {
            want = (size > 0 ? size : 0) * (long long)rowlen;
        } else {
            for (k = 0; k < npairs; k++) {
                pdf_obj *ct = pdf_array_get_raw(idx, k * 2 + 1);

                if (!pdf_is_int(ct) || pdf_to_int(ct) < 0) { want = 0; break; }
                want += pdf_to_int(ct) * (long long)rowlen;
            }
        }
    }

    if (npairs == 0) {
        /* /Index defaults to [0 Size]; with no usable /Size take every row. */
        long long count = size > 0 ? size : (long long)(data.n / rowlen);
        (void)xr_rows(doc, data.p, data.n, &pos, wid, nw, rowlen, 0, count,
                      &nset);
    } else {
        for (i = 0; i < npairs; i++) {
            pdf_obj *a = pdf_array_get_raw(idx, i * 2);
            pdf_obj *b = pdf_array_get_raw(idx, i * 2 + 1);
            long long st, ct;
            if (!pdf_is_int(a) || !pdf_is_int(b)) break;
            st = pdf_to_int(a);
            ct = pdf_to_int(b);
            if (st < 0 || ct < 0 || st > XR_MAX_OBJNUM) break;
            if (ct > (long long)XR_MAX_OBJNUM + 1) break;
            if (xr_rows(doc, data.p, data.n, &pos, wid, nw, rowlen, st, ct,
                        &nset) < 0)
                break;
        }
    }

    /* 7.5.8.2, the verdict.  A stream carrying more data than /W and /Index
     * account for is a generator that padded: the rows the table describes are
     * still exactly where the widths say, so the extra bytes are noted and the
     * table is used.  A stream that disagreed AND defined no entry at all is
     * the other case -- the rows are not where the widths say they are, so
     * every entry read out of it was fiction and there is no table here.  Both
     * are recorded; only the second is fatal, and neither is acted on inside
     * the loader, which goes on recovering as it always does. */
    if (want > 0 && want != (long long)data.n && !doc->xrefstm_badsize) {
        doc->xrefstm_badsize  = 1;
        doc->xrefstm_off      = secoff;
        doc->xrefstm_exp      = want;
        doc->xrefstm_act      = (long long)data.n;
        doc->xrefstm_unusable = (nset == 0);
    }
    pdf_buf_free(&data);

    xr_merge_trailer(doc, stm);

    o = pdf_dict_get_raw(stm, "Prev");
    if (pdf_is_int(o))
        (void)xr_section(doc, pdf_to_int(o), depth + 1, seen, nseen);
    return 0;
}

static int xr_xrefstm_at(pdf_doc *doc, long long off, int depth,
                         long long *seen, int *nseen)
{
    pdf_obj *stm;
    int num = -1, gen = 0;

    stm = xr_parse_at(doc, off, &num, &gen);
    if (!pdf_is_stream(stm)) return PDF_E_SYNTAX;

    /* The cross-reference stream is a perfectly ordinary indirect object as
     * well, and nothing else will ever record it, so claim its slot now --
     * but only if no newer section has already spoken for that number. */
    if (num >= 0 && num <= XR_MAX_OBJNUM && pdf_xref_ensure(doc, num) == 0 &&
        doc->xref[num].type == PDF_XREF_FREE) {
        doc->xref[num].type   = PDF_XREF_NORMAL;
        doc->xref[num].off    = off;
        doc->xref[num].gen    = gen;
        doc->xref[num].cached = stm;
        if (num > doc->maxobj) doc->maxobj = num;
    }
    return xr_read_xrefstm(doc, stm, off, depth, seen, nseen);
}

/* ------------------------------------------------------------- section walk */

static int xr_section(pdf_doc *doc, long long off, int depth,
                      long long *seen, int *nseen)
{
    long long hdr;
    size_t after;
    int i;

    if (depth >= XR_MAX_CHAIN) return PDF_E_SYNTAX;
    for (i = 0; i < *nseen; i++)
        if (seen[i] == off) return 0;               /* /Prev loop: stop here */
    if (*nseen >= XR_MAX_CHAIN) return PDF_E_SYNTAX;
    seen[(*nseen)++] = off;

    if (off < 0) return PDF_E_SYNTAX;

    if ((unsigned long long)off < (unsigned long long)doc->len) {
        if (xr_classic_at(doc, (size_t)off, &after))
            return xr_classic(doc, after, depth, seen, nseen);
        if (xr_xrefstm_at(doc, off, depth, seen, nseen) == 0)
            return 0;
    }

    /* 7.5.2: with a header that is not at offset 0, stored offsets are
     * relative to it.  Files that get this wrong are common in both
     * directions, so whichever reading lands on a section is the right one. */
    hdr = xr_hdr_off(doc);
    if (hdr > 0 && (unsigned long long)(off + hdr) < (unsigned long long)doc->len) {
        if (xr_classic_at(doc, (size_t)(off + hdr), &after))
            return xr_classic(doc, after, depth, seen, nseen);
        return xr_xrefstm_at(doc, off + hdr, depth, seen, nseen);
    }
    return PDF_E_SYNTAX;
}

int pdf_xref_load(pdf_doc *doc)
{
    long long seen[XR_MAX_CHAIN];
    size_t base, pos;
    int tries;

    if (!doc || !doc->data || doc->len == 0) return PDF_E_RANGE;

    /* 7.5.5: "startxref" is the second-to-last line.  Scan back from EOF, and
     * try a few candidates: the last one is sometimes a decoy left behind by a
     * truncated incremental update. */
    base = doc->len > XR_TAIL_WINDOW ? doc->len - XR_TAIL_WINDOW : 0;
    pos  = doc->len;

    for (tries = 0; tries < XR_MAX_STARTXREF; tries++) {
        const unsigned char *q;
        long long start;
        int nseen = 0;
        xr_scan s;

        if (pos <= base) {
            if (base == 0) break;
            base = 0;                       /* buried deeper than the window */
        }
        q = xr_rfind(doc->data + base, pos - base, "startxref", 9);
        if (!q) {
            if (base == 0) break;
            base = 0;
            q = xr_rfind(doc->data, pos, "startxref", 9);
            if (!q) break;
        }
        pos = (size_t)(q - doc->data);      /* the next round looks earlier */

        s.p = doc->data;
        s.n = doc->len;
        s.pos = pos + 9;
        /* No number after the keyword at all: this is not a decoy offset that
         * an earlier section could stand in for, it is a file whose newest
         * cross-reference section cannot be located.  Reconstruction is the
         * only way to see the newest definition of an object, so stop here
         * rather than walking back into a superseded chain. */
        if (!xr_int(&s, &start)) break;
        if (start < 0) continue;

        if (xr_section(doc, start, 0, seen, &nseen) == 0 &&
            pdf_is_dict(doc->trailer))
            return 0;
    }
    return PDF_E_SYNTAX;
}

/* ------------------------------------------------------------ object streams */

/* 7.5.7.  Unpack a /Type/ObjStm in one go: the pair table has to be read
 * anyway, and every object in the stream is about to be wanted. */
static int xr_expand_objstm(pdf_doc *doc, int stmnum)
{
    pdf_obj *stm;
    pdf_buf data;
    xr_scan s;
    long long n, first, i, pairs_end;

    if (stmnum < 0 || stmnum > XR_MAX_OBJNUM) return PDF_E_RANGE;
    if (pdf_xref_ensure(doc, stmnum) < 0) return PDF_E_NOMEM;
    if (doc->xref[stmnum].loading & XR_EXPANDED) return 0;
    if (doc->xref[stmnum].loading & XR_LOADING) return PDF_E_SYNTAX;
    doc->xref[stmnum].loading |= XR_EXPANDED;       /* one attempt, ever */

    stm = pdf_load_object(doc, stmnum);
    if (!pdf_is_stream(stm)) return PDF_E_SYNTAX;
    /* /Type is not checked.  We only get here because something already said
     * this object is an object stream -- a type-2 entry pointing at it, or a
     * "/ObjStm" sighting during recovery -- and that is better evidence than
     * the dictionary's own /Type, which real files do get wrong.  /N and
     * /First are what actually have to be there. */

    n     = pdf_to_int(pdf_resolve(doc, pdf_dict_get_raw(stm, "N")));
    first = pdf_to_int(pdf_resolve(doc, pdf_dict_get_raw(stm, "First")));
    if (n <= 0 || n > XR_MAX_OBJSTM_N || first < 0) return PDF_E_SYNTAX;

    pdf_buf_init(&data);
    if (pdf_stream_data(doc, stm, &data, 1) < 0 || data.n == 0) {
        pdf_buf_free(&data);
        return PDF_E_SYNTAX;
    }
    if (n > (long long)data.n / 2 + 1) n = (long long)data.n / 2 + 1;

    /* First pass over the "num offset" pairs, only to find where they end:
     * a /First that points into the middle of an object is a common enough
     * breakage that the measured end is the better answer. */
    s.p = data.p; s.n = data.n; s.pos = 0;
    for (i = 0; i < n; i++) {
        long long a, b;
        if (!xr_int(&s, &a) || !xr_int(&s, &b)) break;
    }
    pairs_end = (long long)s.pos;
    if (first < pairs_end || (size_t)first > data.n) first = pairs_end;

    s.pos = 0;
    for (i = 0; i < n; i++) {
        long long onum, ooff;
        pdf_lexer lx;
        pdf_obj *val;
        size_t at;

        if (!xr_int(&s, &onum) || !xr_int(&s, &ooff)) break;
        if (onum < 0 || onum > XR_MAX_OBJNUM || ooff < 0) continue;
        if (ooff > (long long)data.n - first) continue;
        at = (size_t)first + (size_t)ooff;
        if (at >= data.n) continue;
        if (pdf_xref_ensure(doc, (int)onum) < 0) break;

        /* Never displace a definition that is already known to be newer. */
        if (doc->xref[onum].cached) continue;
        if (doc->xref[onum].type == PDF_XREF_NORMAL) continue;
        if (doc->xref[onum].type == PDF_XREF_INSTM &&
            doc->xref[onum].stmnum != stmnum) continue;

        pdf_lex_init(&lx, data.p, data.n, at);
        val = pdf_parse_obj(doc, &lx);
        if (!val || val->kind == PDF_STREAM)
            val = pdf_null_obj();   /* 7.5.7: a stream may not live in here */

        if (doc->xref[onum].type == PDF_XREF_FREE) {    /* recovery */
            doc->xref[onum].type   = PDF_XREF_INSTM;
            doc->xref[onum].stmnum = stmnum;
            doc->xref[onum].stmidx = (int)i;
            if ((int)onum > doc->maxobj) doc->maxobj = (int)onum;
        }
        doc->xref[onum].cached = val;
    }
    pdf_buf_free(&data);
    return 0;
}

/* --------------------------------------------------------------- stream data */

/* 7.3.8.1: the data is followed by an optional EOL and then "endstream".
 * A little whitespace slack absorbs the generators that pad. */
static int xr_endstream_at(pdf_doc *doc, size_t off, size_t len)
{
    size_t p, lim;

    if (len > doc->len || off > doc->len - len) return 0;
    p = off + len;
    lim = p + XR_ENDSTM_SLACK;
    if (lim > doc->len) lim = doc->len;
    while (p < lim && xr_isws(doc->data[p])) p++;
    if (doc->len - p < 9) return 0;
    return memcmp(doc->data + p, "endstream", 9) == 0;
}

/* No usable /Length: the data runs to the next "endstream". */
static size_t xr_scan_len(pdf_doc *doc, size_t off)
{
    const unsigned char *q;
    size_t n;

    if (off >= doc->len) return 0;
    q = xr_find(doc->data + off, doc->len - off, "endstream", 9);
    if (!q) return doc->len - off;                  /* truncated file */
    n = (size_t)(q - (doc->data + off));
    if (n > 0 && doc->data[off + n - 1] == '\n') n--;   /* the EOL before it */
    if (n > 0 && doc->data[off + n - 1] == '\r') n--;
    return n;
}

int pdf_stream_data(pdf_doc *doc, pdf_obj *stm, pdf_buf *out, int decode)
{
    const unsigned char *raw;
    size_t rawlen;

    if (!out) return PDF_E_RANGE;
    if (!doc || !pdf_is_stream(stm)) return PDF_E_RANGE;

    if (stm->u.stm.mem) {
        raw    = stm->u.stm.mem;
        rawlen = stm->u.stm.memlen;
    } else {
        long long off = stm->u.stm.off, len = stm->u.stm.len;

        if (off < 0 || (unsigned long long)off > (unsigned long long)doc->len)
            return PDF_E_SYNTAX;
        if (len < 0) {                          /* /Length was indirect */
            pdf_obj *l = pdf_resolve(doc, pdf_dict_get_raw(stm, "Length"));
            len = pdf_is_num(l) ? pdf_to_int(l) : -1;
        }
        /* Absent, unresolvable or simply wrong: all three look the same from
         * here, and all three are fixed the same way. */
        if (len < 0 ||
            (unsigned long long)len > (unsigned long long)doc->len ||
            (unsigned long long)off + (unsigned long long)len >
                (unsigned long long)doc->len ||
            !xr_endstream_at(doc, (size_t)off, (size_t)len))
            len = (long long)xr_scan_len(doc, (size_t)off);

        raw    = doc->data + off;
        rawlen = (size_t)len;
    }

    /* 7.6.2.  "Stream data is encrypted after all stream encoding filters have
     * been applied", so a reader undoes them in the other order: decrypt, then
     * decode.  The /Length in the dictionary counts the ENCRYPTED bytes, which
     * is why the scan above is done against the stored data and not against
     * the plaintext. */
    {
        pdf_buf plain;
        int rc;

        pdf_buf_init(&plain);
        if (pdf_crypt_stream(doc, stm, raw, rawlen, &plain) == 1) {
            if (decode)
                rc = pdf_apply_filters(doc, stm, plain.p, plain.n, out);
            else
                rc = pdf_buf_put(out, plain.p, plain.n);
            pdf_buf_free(&plain);
            return rc;
        }
        pdf_buf_free(&plain);
    }
    if (!decode) return pdf_buf_put(out, raw, rawlen);
    return pdf_apply_filters(doc, stm, raw, rawlen, out);
}

/* ------------------------------------------------------------ object loading */

pdf_obj *pdf_load_object(pdf_doc *doc, int num)
{
    pdf_obj *o = NULL;
    int gotnum = -1, gotgen = 0, type;

    if (!doc || !doc->data || num < 0 || num > XR_MAX_OBJNUM)
        return pdf_null_obj();

    if (num >= doc->xrefcap || doc->xref[num].type == PDF_XREF_FREE) {
        if (doc->reconstructed) return pdf_null_obj();
        pdf_xref_reconstruct(doc);
        if (num >= doc->xrefcap) return pdf_null_obj();
    }
    if (doc->xref[num].cached) return doc->xref[num].cached;
    /* The /Length of a stream may itself be an indirect reference to an object
     * whose own /Length refers back here.  One flag stops the whole family. */
    if (doc->xref[num].loading & XR_LOADING) return pdf_null_obj();
    type = doc->xref[num].type;
    if (type == PDF_XREF_FREE) return pdf_null_obj();

    doc->xref[num].loading |= XR_LOADING;

    if (type == PDF_XREF_INSTM) {
        int sn = doc->xref[num].stmnum;
        if (sn != num) (void)xr_expand_objstm(doc, sn);
        doc->xref[num].loading &= (unsigned char)~XR_LOADING;
        if (doc->xref[num].cached) return doc->xref[num].cached;
    } else
    {
        long long off = doc->xref[num].off;

        o = xr_parse_at(doc, off, &gotnum, &gotgen);
        if (gotnum != num) {                    /* offsets relative to %PDF- */
            long long hdr = xr_hdr_off(doc);
            if (hdr > 0) o = xr_parse_at(doc, off + hdr, &gotnum, &gotgen);
        }
        doc->xref[num].loading &= (unsigned char)~XR_LOADING;
        if (gotnum == num) {
            doc->xref[num].cached = o ? o : pdf_null_obj();
            return doc->xref[num].cached;
        }
    }

    /* Not where the table said it would be.  Rebuild the table from the file
     * itself, once, and try again; doc->reconstructed bounds the recursion. */
    if (!doc->reconstructed) {
        pdf_xref_reconstruct(doc);
        if (num < doc->xrefcap) {
            if (doc->xref[num].cached) return doc->xref[num].cached;
            if (doc->xref[num].type != PDF_XREF_FREE &&
                !(doc->xref[num].loading & XR_LOADING))
                return pdf_load_object(doc, num);
        }
    }
    return pdf_null_obj();
}

/* A generation mismatch is not worth failing over: every shipping reader
 * ignores it, and files that rewrite an object without bumping /Prev are
 * otherwise unreadable. */
pdf_obj *pdf_get_object(pdf_doc *doc, int num, int gen)
{
    return pdf_load_object(doc, num);
}

pdf_obj *pdf_resolve(pdf_doc *doc, pdf_obj *o)
{
    int depth;

    if (!o) return pdf_null_obj();
    for (depth = 0; o->kind == PDF_REF; depth++) {
        if (depth >= XR_MAX_REF_DEPTH) return pdf_null_obj();
        o = pdf_get_object(doc, o->u.ref.num, o->u.ref.gen);
        if (!o) return pdf_null_obj();
    }
    return o;
}

/* ------------------------------------------------------------ reconstruction */

static long long xr_num_at(pdf_doc *doc, size_t a, size_t b)
{
    long long v = 0;

    if (b <= a || b - a > 10) return -1;        /* 10 digits > XR_MAX_OBJNUM */
    while (a < b) {
        if (!xr_isdig(doc->data[a])) return -1;
        v = v * 10 + (doc->data[a] - '0');
        a++;
    }
    return v;
}

/* Peek at an object's first few kilobytes without parsing it. */
static int xr_peek_has(pdf_doc *doc, int num, const char *needle)
{
    size_t at, win;

    if (num < 0 || num >= doc->xrefcap) return 0;
    if (doc->xref[num].type != PDF_XREF_NORMAL) return 0;
    if (doc->xref[num].off < 0 ||
        (unsigned long long)doc->xref[num].off >= (unsigned long long)doc->len)
        return 0;
    at  = (size_t)doc->xref[num].off;
    win = doc->len - at;
    if (win > XR_PEEK) win = XR_PEEK;
    return xr_find(doc->data + at, win, needle, strlen(needle)) != NULL;
}

/* Rebuild the table by scanning the file for "N G obj".  This is the answer to
 * a missing or lying startxref, a shifted subsection, a truncated update -- and
 * it works with no cross-reference information at all. */
int pdf_xref_reconstruct(pdf_doc *doc)
{
    size_t i;
    long long n;
    int found = 0;

    if (!doc || !doc->data) return PDF_E_RANGE;
    if (doc->reconstructed) return 0;       /* re-entrant calls are no-ops */
    doc->reconstructed = 1;
    doc->damaged       = 1;
    doc->catalog       = NULL;              /* may have come out of the junk */
    doc->pages_built   = 0;
    doc->npages        = 0;

    /* Pass 1: every "N G obj" header, scanning forward so that the last
     * definition of a number -- the newest incremental update -- wins. */
    for (i = 0; i + 3 <= doc->len && found < XR_MAX_SCAN_OBJ; ) {
        const unsigned char *q = xr_find(doc->data + i, doc->len - i, "obj", 3);
        size_t at, p, s1, e1, s2, e2;
        long long num, gen;

        if (!q) break;
        at = (size_t)(q - doc->data);
        i  = at + 3;
        if (at + 3 < doc->len && !xr_isws(doc->data[at + 3]) &&
            !xr_isdelim(doc->data[at + 3]))
            continue;                           /* "objects", not "obj" */

        p = at;
        while (p > 0 && xr_isws(doc->data[p - 1])) p--;
        if (p == at) continue;                  /* "0obj" is not a header */
        e2 = p;
        while (p > 0 && xr_isdig(doc->data[p - 1])) p--;
        s2 = p;
        while (p > 0 && xr_isws(doc->data[p - 1])) p--;
        if (p == s2) continue;                  /* no gap between the numbers */
        e1 = p;
        while (p > 0 && xr_isdig(doc->data[p - 1])) p--;
        s1 = p;
        if (s1 > 0 && !xr_isws(doc->data[s1 - 1]) &&
            !xr_isdelim(doc->data[s1 - 1]))
            continue;                           /* "x12 0 obj" */

        num = xr_num_at(doc, s1, e1);
        gen = xr_num_at(doc, s2, e2);
        if (num < 0 || gen < 0 || num > XR_MAX_OBJNUM) continue;
        if (xr_set(doc, num, PDF_XREF_NORMAL, (long long)s1,
                   gen <= 65535 ? (int)gen : 0, 0, 0, 1) < 0)
            break;
        found++;
    }

    /* Pass 2: trailers, newest first, so the newest /Root wins. */
    {
        size_t pos = doc->len;
        int ntr;

        for (ntr = 0; ntr < XR_MAX_CHAIN && pos > 0; ntr++) {
            const unsigned char *q = xr_rfind(doc->data, pos, "trailer", 7);
            pdf_lexer lx;

            if (!q) break;
            pos = (size_t)(q - doc->data);
            pdf_lex_init(&lx, doc->data, doc->len, pos + 7);
            xr_merge_trailer(doc, pdf_parse_obj(doc, &lx));
        }
    }

    /* Compressed objects are invisible to a byte scan, so every object stream
     * still has to be unpacked by hand before the trailer hunt below. */
    for (n = 1; n <= doc->maxobj && n < doc->xrefcap; n++)
        if (xr_peek_has(doc, (int)n, "/ObjStm"))
            (void)xr_expand_objstm(doc, (int)n);

    /* Pass 3: a trailer of last resort.  A cross-reference stream dictionary
     * is a trailer (7.5.8.2); failing that, any /Type/Catalog will do. */
    if (pdf_is_null(pdf_dict_get_raw(doc->trailer, "Root")))
        for (n = doc->maxobj; n >= 1; n--) {
            pdf_obj *o;
            if (!xr_peek_has(doc, (int)n, "/XRef")) continue;
            o = pdf_load_object(doc, (int)n);
            if (!pdf_is_stream(o)) continue;
            if (!pdf_is_name(pdf_dict_get_raw(o, "Type"), "XRef")) continue;
            if (pdf_is_null(pdf_dict_get_raw(o, "Root"))) continue;
            xr_merge_trailer(doc, o);
            break;
        }

    if (pdf_is_null(pdf_dict_get_raw(doc->trailer, "Root"))) {
        int best = -1, pass;

        for (pass = 0; pass < 2 && best < 0; pass++)
            for (n = 1; n <= doc->maxobj && n < doc->xrefcap; n++) {
                pdf_obj *o;
                if (doc->xref[n].type == PDF_XREF_FREE) continue;
                if (doc->xref[n].type == PDF_XREF_NORMAL &&
                    !xr_peek_has(doc, (int)n, "/Catalog"))
                    continue;
                o = pdf_load_object(doc, (int)n);
                if (!pdf_is_dict(o) || pdf_is_stream(o)) continue;
                if (!pdf_is_name(pdf_dict_get_raw(o, "Type"), "Catalog")) continue;
                /* the first round insists on a usable page tree */
                if (pass == 0 && !pdf_is_dict(pdf_resolve(doc,
                        pdf_dict_get_raw(o, "Pages"))))
                    continue;
                best = (int)n;
                break;
            }
        if (best >= 0) {
            if (!pdf_is_dict(doc->trailer)) doc->trailer = pdf_new_dict(doc);
            if (pdf_is_dict(doc->trailer))
                pdf_dict_put(doc, doc->trailer, "Root",
                             pdf_new_ref(doc, best, doc->xref[best].gen));
        }
    }
    return 0;
}

/* --------------------------------------------------------- introspection */

/* 7.5.4.  What the table says about one object once every section has been
 * merged -- so for an incrementally updated file this is the revision that won,
 * which is the whole reason to be able to ask. */
int pdf_xref_entry(pdf_doc *doc, int num, int *type, long long *off, int *gen)
{
    if (!doc || !doc->xref) return 0;
    if (num < 0 || num >= doc->xrefcap) return 0;
    if (doc->xref[num].type == PDF_XREF_FREE) return 0;
    if (type) *type = doc->xref[num].type;
    if (off)  *off  = doc->xref[num].off;
    if (gen)  *gen  = doc->xref[num].gen;
    return 1;
}

void pdf_xref_force_reconstruct(pdf_doc *doc)
{
    if (!doc) return;
    doc->reconstructed = 0;             /* pdf_open() may already have tried */
    (void)pdf_xref_reconstruct(doc);
}

const unsigned char *pdf_file_bytes(pdf_doc *doc, size_t *len)
{
    if (!doc) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = doc->len;
    return doc->data;
}

/* Is a cross-reference section really at `off`?  Either spelling counts: the
 * classic "xref" keyword, or an indirect object header, which is what a
 * cross-reference stream looks like from here.  Accepting both means the
 * question does not have to be re-asked when 7.5.8 arrives. */
static int xr_section_present(pdf_doc *doc, long long off)
{
    size_t after;
    xr_scan s;
    long long n, g;
    char kw[16];

    if (off < 0 || (unsigned long long)off >= (unsigned long long)doc->len)
        return 0;
    if (xr_classic_at(doc, (size_t)off, &after))
        return 1;

    s.p = doc->data;
    s.n = doc->len;
    s.pos = (size_t)off;
    if (!xr_int(&s, &n) || !xr_int(&s, &g)) return 0;
    if (!xr_word(&s, kw, sizeof kw)) return 0;
    return strcmp(kw, "obj") == 0;
}

/* 7.5.5.  The trailer belonging to the classic table at `off`. */
static pdf_obj *xr_trailer_at(pdf_doc *doc, long long off)
{
    size_t after, lim;
    const unsigned char *q;
    pdf_lexer lx;

    if (off < 0 || (unsigned long long)off >= (unsigned long long)doc->len)
        return pdf_null_obj();
    if (!xr_classic_at(doc, (size_t)off, &after))
        return pdf_null_obj();

    lim = after + XR_TRAILER_SEEK;
    if (lim > doc->len) lim = doc->len;
    if (after >= lim) return pdf_null_obj();

    q = xr_find(doc->data + after, lim - after, "trailer", 7);
    if (!q) return pdf_null_obj();
    pdf_lex_init(&lx, doc->data, doc->len, (size_t)(q - doc->data) + 7);
    return pdf_parse_obj(doc, &lx);
}

/* Walk the chain the file CLAIMS to have, rather than the one pdf_xref_load()
 * managed to read.  xr_classic() deliberately ignores a /Prev that will not
 * load -- recovery beats refusal -- so by the time a document is open the
 * breakage has already been papered over, and this is the only way to see it. */
int pdf_xref_chain_check(pdf_doc *doc, long long *badoff)
{
    const unsigned char *q;
    xr_scan s;
    long long off;
    int hops;

    if (badoff) *badoff = 0;
    if (!doc || !doc->data || doc->len == 0) return 0;

    q = xr_rfind(doc->data, doc->len, "startxref", 9);
    if (!q) return 0;
    s.p = doc->data;
    s.n = doc->len;
    s.pos = (size_t)(q - doc->data) + 9;
    if (!xr_int(&s, &off)) return 0;

    /* 7.5.5.  Offset 0 is where the header is, so a startxref naming it names
     * no cross-reference section at all.  It has to be caught here rather than
     * by the walk below: the table scanner skips "%PDF-1.x" as a comment, so
     * it reads the file's first object as though the section began at 0 and
     * the walk concludes the chain is intact.  The reference tool reports this
     * as a different failure from a hop that lands on nothing, so it is
     * returned as a different answer. */
    if (off <= 0) {
        if (badoff) *badoff = 0;
        return PDF_XREF_CHAIN_NOSTART;
    }

    for (hops = 0; hops < XR_MAX_CHAIN; hops++) {
        pdf_obj *prev;

        if (!xr_section_present(doc, off)) {
            if (badoff) *badoff = off;
            return PDF_XREF_CHAIN_LOST;
        }
        prev = pdf_dict_get_raw(xr_trailer_at(doc, off), "Prev");
        if (!pdf_is_int(prev)) return 0;
        off = pdf_to_int(prev);
    }
    return 0;                       /* a ring; xr_section() already caps depth */
}

/* 7.5.5.  /Prev belongs to the newest section's trailer, and that trailer is
 * the file's trailer -- but xr_merge_trailer() drops /Prev on the way in,
 * because as a document-wide key it would be a lie: it describes where one
 * section's predecessor is, not the document.  Reporting the trailer means
 * reporting what the file wrote, so the value is read back off the file here.
 * A newest section that is not really there has no trailer of its own; what a
 * reader shows then is a reconstructed one, which has no /Prev at all. */
int pdf_xref_trailer_prev(pdf_doc *doc, long long *prev)
{
    const unsigned char *q;
    xr_scan s;
    long long off;
    pdf_obj *p;

    if (prev) *prev = 0;
    if (!doc || !doc->data || doc->len == 0) return 0;

    q = xr_rfind(doc->data, doc->len, "startxref", 9);
    if (!q) return 0;
    s.p = doc->data;
    s.n = doc->len;
    s.pos = (size_t)(q - doc->data) + 9;
    if (!xr_int(&s, &off) || off <= 0) return 0;
    if (!xr_section_present(doc, off)) return 0;

    p = pdf_dict_get_raw(xr_trailer_at(doc, off), "Prev");
    if (!pdf_is_int(p)) return 0;
    if (prev) *prev = pdf_to_int(p);
    return 1;
}

/* 7.5.4.  What one section says about one object: 'n', 'f', or 0 when this
 * section does not mention it at all. */
static int xr_entry_kind_at(pdf_doc *doc, long long off, int num)
{
    xr_scan s;
    size_t after;
    long long first, count, k = 0;
    int nsub = 0;

    if (off < 0 || (unsigned long long)off >= (unsigned long long)doc->len)
        return 0;
    if (!xr_classic_at(doc, (size_t)off, &after))
        return 0;

    s.p = doc->data;
    s.n = doc->len;
    s.pos = after;
    for (;;) {
        size_t save;

        if (++nsub > XR_MAX_SUBSEC) break;
        xr_skipws(&s);
        save = s.pos;
        if (s.pos >= s.n || !xr_isdig(s.p[s.pos])) break;
        if (!xr_int(&s, &first) || !xr_int(&s, &count)) { s.pos = save; break; }
        if (first < 0) first = 0;
        if (count < 0 || count > (long long)XR_MAX_OBJNUM + 1) break;
        if (count == 0) continue;

        for (k = 0; k < count; k++) {
            size_t before = s.pos;
            long long o, g;
            char kw[8];

            if (!xr_int(&s, &o) || !xr_int(&s, &g) ||
                !xr_word(&s, kw, sizeof kw)) {
                s.pos = before;
                break;
            }
            if (kw[1] != 0 || (kw[0] != 'n' && kw[0] != 'f')) {
                s.pos = before;
                break;
            }
            if (first + k == (long long)num) return kw[0];
        }
        if (k < count) break;
    }
    return 0;
}

int pdf_xref_freed_by_newer(pdf_doc *doc, int num)
{
    const unsigned char *q;
    xr_scan s;
    long long off;
    int hops;

    if (!doc || !doc->data || doc->len == 0) return 0;
    q = xr_rfind(doc->data, doc->len, "startxref", 9);
    if (!q) return 0;
    s.p = doc->data;
    s.n = doc->len;
    s.pos = (size_t)(q - doc->data) + 9;
    if (!xr_int(&s, &off)) return 0;

    for (hops = 0; hops < XR_MAX_CHAIN; hops++) {
        pdf_obj *prev;
        int kind = xr_entry_kind_at(doc, off, num);

        if (kind == 'f') return 1;
        if (kind == 'n') return 0;
        prev = pdf_dict_get_raw(xr_trailer_at(doc, off), "Prev");
        if (!pdf_is_int(prev)) return 0;
        off = pdf_to_int(prev);
    }
    return 0;
}

/* ------------------------- cross-reference stream and object stream reports
 *
 * 7.5.8.  A PDF 1.5 file answers three questions differently from a 1.4 one:
 * its newest cross-reference section may be a STREAM, so the section's trailer
 * and its /Prev live in a stream dictionary rather than after a "trailer"
 * keyword; an object may live inside another object rather than at a file
 * offset; and an object stream that cannot be read has to be diagnosed rather
 * than silently recovered from.  Everything below answers one of those, and
 * none of it disturbs the table: these are questions asked about a document
 * that is already open.
 */

/* The indirect object at `off`, if it is a stream: its dictionary. */
static pdf_obj *xr_stream_dict_at(pdf_doc *doc, long long off)
{
    pdf_obj *o;
    int num = -1, gen = 0;

    if (!doc || off < 0 ||
        (unsigned long long)off >= (unsigned long long)doc->len)
        return pdf_null_obj();
    o = xr_parse_at(doc, off, &num, &gen);
    if (num < 0 || !pdf_is_stream(o)) return pdf_null_obj();
    return o->u.stm.dict ? o->u.stm.dict : pdf_null_obj();
}

/* 7.5.5 and 7.5.8.2: whichever spelling the section at `off` uses, the
 * dictionary that serves as its trailer. */
static pdf_obj *xr_any_trailer_at(pdf_doc *doc, long long off)
{
    pdf_obj *t = xr_trailer_at(doc, off);

    if (pdf_is_dict(t)) return t;
    return xr_stream_dict_at(doc, off);
}

int pdf_xref_stream_chain_check(pdf_doc *doc, long long *badoff)
{
    const unsigned char *q;
    xr_scan s;
    long long off;
    int hops;

    if (badoff) *badoff = 0;
    if (!doc || !doc->data || doc->len == 0) return 0;

    q = xr_rfind(doc->data, doc->len, "startxref", 9);
    if (!q) return 0;
    s.p = doc->data;
    s.n = doc->len;
    s.pos = (size_t)(q - doc->data) + 9;
    if (!xr_int(&s, &off)) return 0;
    if (off <= 0) return 0;         /* pdf_xref_chain_check() names that one */

    for (hops = 0; hops < XR_MAX_CHAIN; hops++) {
        pdf_obj *prev;

        if (!xr_section_present(doc, off)) {
            if (badoff) *badoff = off;
            return PDF_XREF_CHAIN_LOST;
        }
        prev = pdf_dict_get_raw(xr_any_trailer_at(doc, off), "Prev");
        if (!pdf_is_int(prev)) return 0;
        off = pdf_to_int(prev);
    }
    return 0;                       /* a ring; xr_section() already caps depth */
}

pdf_obj *pdf_xref_stream_dict(pdf_doc *doc)
{
    const unsigned char *q;
    xr_scan s;
    long long off, hdr;
    pdf_obj *d;

    if (!doc || !doc->data || doc->len == 0) return pdf_null_obj();
    q = xr_rfind(doc->data, doc->len, "startxref", 9);
    if (!q) return pdf_null_obj();
    s.p = doc->data;
    s.n = doc->len;
    s.pos = (size_t)(q - doc->data) + 9;
    if (!xr_int(&s, &off) || off <= 0) return pdf_null_obj();

    d = xr_stream_dict_at(doc, off);
    if (pdf_is_dict(d)) return d;
    /* 7.5.2: the same both-readings rule xr_section() applies to an offset. */
    hdr = xr_hdr_off(doc);
    if (hdr > 0) return xr_stream_dict_at(doc, off + hdr);
    return pdf_null_obj();
}

int pdf_xref_compressed(pdf_doc *doc, int num, int *stmnum, int *stmidx)
{
    if (stmnum) *stmnum = 0;
    if (stmidx) *stmidx = 0;
    if (!doc || !doc->xref) return 0;
    if (num < 0 || num >= doc->xrefcap) return 0;
    if (doc->xref[num].type != PDF_XREF_INSTM) return 0;
    if (stmnum) *stmnum = doc->xref[num].stmnum;
    if (stmidx) *stmidx = doc->xref[num].stmidx;
    return 1;
}

int pdf_xref_stream_size_error(pdf_doc *doc, long long *off,
                               long long *expected, long long *actual)
{
    if (off)      *off      = 0;
    if (expected) *expected = 0;
    if (actual)   *actual   = 0;
    if (!doc || !doc->xrefstm_badsize) return 0;
    if (off)      *off      = doc->xrefstm_off;
    if (expected) *expected = doc->xrefstm_exp;
    if (actual)   *actual   = doc->xrefstm_act;
    return 1;
}

int pdf_xref_stream_unusable(pdf_doc *doc)
{
    return (doc && doc->xrefstm_badsize && doc->xrefstm_unusable) ? 1 : 0;
}

int pdf_objstm_diagnose(pdf_doc *doc, int stmnum, long long *offout, int *genout)
{
    const int gone = PDF_OBJSTM_NULL | PDF_OBJSTM_WRONGTYPE | PDF_OBJSTM_BADKEYS;
    pdf_obj *o, *cnt, *first;
    int num = -1, gen = 0, flags = 0;
    long long off;

    if (offout) *offout = 0;
    if (genout) *genout = 0;
    if (!doc || !doc->xref) return 0;
    if (stmnum < 0 || stmnum >= doc->xrefcap) return gone;
    if (doc->xref[stmnum].type != PDF_XREF_NORMAL) return gone;

    /* Judged from where the TABLE says the stream is, not from whatever the
     * loader later recovered: an object stream the table points past is exactly
     * the breakage worth naming, and a rebuilt table would hide it. */
    off = doc->xref[stmnum].off;
    if (offout) *offout = off;
    o = xr_parse_at(doc, off, &num, &gen);
    if (num != stmnum) {
        long long hdr = xr_hdr_off(doc);

        if (hdr > 0) o = xr_parse_at(doc, off + hdr, &num, &gen);
    }
    if (genout) *genout = (num == stmnum) ? gen : doc->xref[stmnum].gen;
    if (num != stmnum || !pdf_is_stream(o)) return gone;

    if (!pdf_is_name(pdf_dict_get_raw(o, "Type"), "ObjStm"))
        flags |= PDF_OBJSTM_WRONGTYPE;
    cnt   = pdf_dict_get_raw(o, "N");
    first = pdf_dict_get_raw(o, "First");
    if (!pdf_is_int(cnt) || !pdf_is_int(first))
        flags |= PDF_OBJSTM_BADKEYS;
    return flags;
}
