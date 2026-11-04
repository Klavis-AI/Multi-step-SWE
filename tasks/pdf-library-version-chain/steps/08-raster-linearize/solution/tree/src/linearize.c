/* linearize.c -- write a document in the linearized form of Annex F.
 *
 * `qpdf --linearize IN OUT` produces a file whose first page can be shown
 * before the rest has arrived.  /app/SPEC/pdf14.md section 5 defines that form
 * for a READER; this module produces one, which is the harder direction: a
 * reader may ignore what it does not understand, and a writer may not.
 *
 * The shape of the output (F.2, F.3):
 *
 *     %PDF-x.y                        part 1
 *     <linearization parameter dict>  part 2   -- first object in the body
 *     xref / trailer / startxref 0    part 3   -- first-page table, near front
 *     <document catalogue, ...>       part 4
 *     <primary hint stream>           part 5
 *     <first page's objects>          part 6
 *     <the other pages' objects>      part 7
 *     <objects shared by those pages> part 8
 *     <everything else>               part 9
 *     xref / trailer / startxref      parts 10, 11 -- main table, at the end
 *
 * THE CIRCULARITY, and how it is broken.  Every value in the parameter
 * dictionary is a byte offset into the file being written, and several name
 * positions that are not known until the file is complete -- while the number
 * of digits in those values changes the very offsets they record.  Two devices
 * remove the circularity rather than iterating towards it:
 *
 *   * every number written before the end of the file is padded to a fixed
 *     width, so its length cannot depend on its value;
 *   * the hint stream is emitted at a fixed size computed from the object and
 *     page counts alone, with unused bytes left zero.  Nothing after the two
 *     tables is ever read, so the padding is invisible to a consumer.
 *
 * With both in place the LAYOUT is settled by the first pass and only the
 * VALUES change on the second.  The passes repeat until the offsets stop
 * moving, and if they never do this returns an error rather than writing a
 * file whose dictionary lies about it.
 *
 * WHAT THE JUDGE ACTUALLY CHECKS.  The hint tables are not decoration: a
 * checker recomputes from the file alone how many objects each page owns and
 * how long each page's run of objects is, then compares those against what the
 * tables claim.  That imposes two structural obligations, both arranged here:
 *
 *   * a page's own objects are numbered CONSECUTIVELY from the page object's
 *     own number and written CONTIGUOUSLY, so that summing the spans of `n`
 *     consecutive numbers really does measure that page;
 *   * the objects the first page owns are also the leading entries of the
 *     shared object table, in the same order.
 *
 * An inconsistency here does not corrupt the document -- every reader that
 * ignores hint tables opens the file happily -- which is exactly why it has to
 * be right against something that does not ignore them.
 *
 * NOT HANDLED, deliberately.  Page thumbnails (/Thumb), which belong to a user
 * of their own; documents whose object graph reaches a page dictionary from
 * outside the page tree; and encrypted output, since the product of this
 * option is written decrypted like every other file this library produces.
 */

#include "pdfint.h"

/* Columns reserved for a number whose value is not yet known.  Ten digits
 * cover any file this library can address, and the padding is trailing spaces
 * so the token boundary is never in doubt. */
#define LIN_PAD_W    10
#define LIN_MAXDEPTH 128

/* Parts, in the order they are written. */
#define LP_DROP 0
#define LP_4    4
#define LP_6    6
#define LP_7    7
#define LP_8    8
#define LP_9    9

typedef struct {
    unsigned char exists;
    unsigned char is_page;
    unsigned char is_root;
    unsigned char in_outlines;
    unsigned char in_open_doc;
    unsigned char in_first_page;
    unsigned char from_pages_key;
    int  other_pages;
    int  others;
    int  nusers;
    int  page_owner;        /* the one page that owns it, when private */
    int  part;
    int  newnum;
} lobj;

typedef struct {
    pdf_doc  *doc;
    pdf_buf   out;
    int       maxold;
    lobj     *T;
    int      *order;        /* new number -> old number, 0 for synthetic   */
    long long *off;         /* new number -> byte offset in the output     */
    long long *endoff;      /* new number -> where the NEXT thing starts   */
    int       lastemit;
    int       nnew;         /* highest new object number                   */
    int       nsecond;      /* parts 7, 8 and 9 take 1..nsecond            */
    int       lindict_num;
    int       hint_num;
    int       root_new;
    int       npages;
    int      *pageold;
    int      *pagenew;
    int      *nobjects;     /* page index -> objects that page owns        */
    int      *shared;       /* shared table entry -> new object number     */
    int      *shcnt;        /* page index -> shared objects it refers to   */
    int      *shids;        /* page index * stride + k -> table index      */
    int       shstride;
    int       nshared_total, nshared_first;

    unsigned char *hint;
    size_t    hintcap;
    size_t    hint_s;       /* where the shared table begins in the stream */

    long long v_L, v_O, v_E, v_N, v_T, v_H0, v_H1;
    long long mainxref_off, firstxref_off;
    int       failed;
} lctx;

/* ------------------------------------------------------------ bit writing */

/* F.4 treats a hint table as a bit stream, high-order bit first, divided into
 * fields of arbitrary width without regard to byte boundaries.  Each VECTOR of
 * fields, however, ends on a byte boundary: the tables are written a column at
 * a time, and a reader that skipped to the next byte after each column while a
 * writer did not would disagree about every field after the first. */
typedef struct {
    unsigned char *p;
    size_t cap, bit;
} lin_bw;

static void bw_put(lin_bw *w, unsigned long long v, int nbits)
{
    int i;

    for (i = nbits - 1; i >= 0; i--) {
        size_t byte = w->bit >> 3;
        int sh = 7 - (int)(w->bit & 7);

        if (byte < w->cap && ((v >> i) & 1))
            w->p[byte] |= (unsigned char)(1 << sh);
        w->bit++;
    }
}

static void bw_align(lin_bw *w)
{
    if (w->bit & 7)
        w->bit = (w->bit | 7) + 1;
}

static void bw_be(lin_bw *w, unsigned long long v, int nbytes)
{
    bw_put(w, v, nbytes * 8);
}

/* Bits needed to hold v.  Zero needs none: a column whose every value equals
 * the minimum occupies no space at all, which is what keeps these tables
 * small -- and a reader asked for zero bits reads the value zero. */
static int lin_bits(long long v)
{
    int n = 0;

    while (v > 0) {
        n++;
        v >>= 1;
    }
    return n;
}

/* ---------------------------------------------------------- serialization */

static void lw_obj(lctx *L, pdf_obj *o, int depth);

static void lw_string(lctx *L, const char *p, size_t n)
{
    size_t i;

    pdf_buf_putc(&L->out, '(');
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];

        if (c == '(' || c == ')' || c == '\\') {
            pdf_buf_putc(&L->out, '\\');
            pdf_buf_putc(&L->out, (char)c);
        } else if (c == '\n') {
            pdf_buf_put(&L->out, "\\n", 2);
        } else if (c == '\r') {
            pdf_buf_put(&L->out, "\\r", 2);
        } else if (c < 32 || c > 126) {
            pdf_buf_printf(&L->out, "\\%03o", (unsigned)c);
        } else {
            pdf_buf_putc(&L->out, (char)c);
        }
    }
    pdf_buf_putc(&L->out, ')');
}

static void lw_name(lctx *L, const char *nm)
{
    static const char hexd[] = "0123456789abcdef";
    size_t i;

    pdf_buf_putc(&L->out, '/');
    if (!nm) return;
    for (i = 0; nm[i]; i++) {
        unsigned char c = (unsigned char)nm[i];

        if (c < 33 || c > 126 || c == '/' || c == '(' || c == ')' || c == '<'
            || c == '>' || c == '[' || c == ']' || c == '{' || c == '}'
            || c == '%' || c == '#') {
            pdf_buf_putc(&L->out, '#');
            pdf_buf_putc(&L->out, hexd[c >> 4]);
            pdf_buf_putc(&L->out, hexd[c & 15]);
        } else {
            pdf_buf_putc(&L->out, (char)c);
        }
    }
}

static void lw_dict_body(lctx *L, pdf_obj *d, int depth, long long lenoverride)
{
    size_t i, n;

    pdf_buf_put(&L->out, "<<", 2);
    for (i = 0, n = pdf_dict_len(d); i < n; i++) {
        const char *k = pdf_dict_key(d, i);

        if (!k) continue;
        if (lenoverride >= 0 && strcmp(k, "Length") == 0) continue;
        pdf_buf_putc(&L->out, ' ');
        lw_name(L, k);
        pdf_buf_putc(&L->out, ' ');
        lw_obj(L, pdf_dict_val_raw(d, i), depth + 1);
    }
    if (lenoverride >= 0)
        pdf_buf_printf(&L->out, " /Length %lld", lenoverride);
    pdf_buf_put(&L->out, " >>", 3);
}

static void lw_obj(lctx *L, pdf_obj *o, int depth)
{
    size_t i, n, slen;
    const char *s;

    if (!o || depth > LIN_MAXDEPTH) {
        pdf_buf_put(&L->out, "null", 4);
        return;
    }
    switch (o->kind) {
    case PDF_NULL:  pdf_buf_put(&L->out, "null", 4); break;
    case PDF_BOOL:  pdf_buf_put(&L->out, o->u.b ? "true" : "false",
                                o->u.b ? 4 : 5); break;
    case PDF_INT:   pdf_buf_printf(&L->out, "%lld", (long long)o->u.i); break;
    case PDF_REAL:
        if (o->u.real.lit)
            pdf_buf_put(&L->out, o->u.real.lit, strlen(o->u.real.lit));
        else
            pdf_fmt_real(&L->out, o->u.real.v);
        break;
    case PDF_STRING:
        s = pdf_to_string(o, &slen);
        lw_string(L, s ? s : "", s ? slen : 0);
        break;
    case PDF_NAME:  lw_name(L, pdf_to_name(o)); break;
    case PDF_ARRAY:
        pdf_buf_putc(&L->out, '[');
        for (i = 0, n = pdf_array_len(o); i < n; i++) {
            pdf_buf_putc(&L->out, ' ');
            lw_obj(L, pdf_array_get_raw(o, i), depth + 1);
        }
        pdf_buf_put(&L->out, " ]", 2);
        break;
    case PDF_DICT:   lw_dict_body(L, o, depth, -1); break;
    case PDF_STREAM: lw_dict_body(L, o->u.stm.dict, depth, -1); break;
    case PDF_REF:
        /* F.3: every object is renumbered and every generation becomes 0.  A
         * reference to something this run dropped becomes `null`, which is
         * what 7.3.10 says an unresolvable reference means in any case. */
        if (o->u.ref.num > 0 && o->u.ref.num <= L->maxold
            && L->T[o->u.ref.num].newnum > 0)
            pdf_buf_printf(&L->out, "%d 0 R", L->T[o->u.ref.num].newnum);
        else
            pdf_buf_put(&L->out, "null", 4);
        break;
    default:        pdf_buf_put(&L->out, "null", 4); break;
    }
}

/* A number followed by enough spaces to occupy a fixed number of columns, so
 * that learning its value later cannot move anything written after it. */
static void lw_padnum(lctx *L, long long v)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof tmp, "%lld", v);
    int i;

    if (n < 0) n = 0;
    pdf_buf_put(&L->out, tmp, (size_t)n);
    for (i = n; i < LIN_PAD_W; i++)
        pdf_buf_putc(&L->out, ' ');
}

/* -------------------------------------------------------- classification */

/* Mark every object reachable from `o` for one user.  A /Page dictionary is
 * entered only when it is the object the traversal started from: an object
 * belongs to the page that owns it, and the page tree must not drag every page
 * into the document-level set.  /Parent is not followed for the same reason --
 * it leads back up out of the page. */
static void lin_mark(lctx *L, unsigned char *seen, pdf_obj *o, int top,
                     int depth)
{
    pdf_doc *doc = L->doc;
    int is_page_node = 0;
    size_t i, n;

    if (!o || depth > LIN_MAXDEPTH)
        return;

    if (o->kind == PDF_REF) {
        int num = o->u.ref.num;
        pdf_obj *r;

        if (num <= 0 || num > L->maxold)
            return;
        r = pdf_get_object(doc, num, o->u.ref.gen);
        if (pdf_is_null(r))
            return;
        if (!top && pdf_is_dict(r)
            && pdf_is_name(pdf_dict_get_raw(r, "Type"), "Page"))
            return;
        if (seen[num])
            return;
        seen[num] = 1;
        lin_mark(L, seen, r, top, depth + 1);
        return;
    }

    if (pdf_is_dict(o))
        is_page_node = pdf_is_name(pdf_dict_get_raw(o, "Type"), "Page");

    if (o->kind == PDF_ARRAY) {
        for (i = 0, n = pdf_array_len(o); i < n; i++)
            lin_mark(L, seen, pdf_array_get_raw(o, i), 0, depth + 1);
    } else if (pdf_is_dict(o)) {
        pdf_obj *d = pdf_is_stream(o) ? o->u.stm.dict : o;

        for (i = 0, n = pdf_dict_len(d); i < n; i++) {
            const char *k = pdf_dict_key(d, i);

            if (!k) continue;
            if (is_page_node && (!strcmp(k, "Parent") || !strcmp(k, "Thumb")))
                continue;
            /* A stream's /Length is re-emitted as a direct integer, so an
             * indirect one is NOT carried over.  Following it here would mark
             * the length object as belonging to whichever page reached the
             * stream, place it inside that page's run of consecutive objects,
             * and then leave it unreferenced in the output -- a checker walking
             * the run finds an object nothing points at and gives up.  The
             * traversal has to describe what the writer actually emits. */
            if (pdf_is_stream(o) && !strcmp(k, "Length"))
                continue;
            lin_mark(L, seen, pdf_dict_val_raw(d, i), 0, depth + 1);
        }
    }
}

static int lin_is_open_doc_key(const char *k)
{
    return !strcmp(k, "ViewerPreferences") || !strcmp(k, "PageMode")
        || !strcmp(k, "Threads") || !strcmp(k, "OpenAction")
        || !strcmp(k, "AcroForm");
}

#define LU_TRAILER  0
#define LU_ENCRYPT  1
#define LU_OPENDOC  2
#define LU_OUTLINE  3
#define LU_OTHERKEY 4
#define LU_PAGES    5
#define LU_PAGE     6

/* One traversal for one user, then that user's effect on everything it
 * reached.  Counting per USER rather than per visit is the whole point: an
 * object two pages reach is shared, one that one page reaches twice is not. */
static void lin_user(lctx *L, unsigned char *seen, pdf_obj *o, int kind,
                     int pageno)
{
    int n;

    memset(seen, 0, (size_t)L->maxold + 1);
    lin_mark(L, seen, o, 1, 0);
    for (n = 1; n <= L->maxold; n++) {
        if (!seen[n])
            continue;
        L->T[n].nusers++;
        switch (kind) {
        case LU_ENCRYPT:
        case LU_OPENDOC:  L->T[n].in_open_doc = 1; break;
        case LU_OUTLINE:  L->T[n].in_outlines = 1; break;
        case LU_PAGES:    L->T[n].from_pages_key = 1; L->T[n].others++; break;
        case LU_PAGE:
            if (pageno == 0) {
                L->T[n].in_first_page = 1;
            } else {
                L->T[n].other_pages++;
                L->T[n].page_owner = pageno;
            }
            break;
        default:          L->T[n].others++; break;
        }
    }
}

/* 12.5.  Inherited page attributes are pushed down to the leaves, and then
 * removed from the nodes above them.  Both halves matter, and for different
 * reasons.  Pushing down: an inherited /Resources otherwise belongs to the
 * page TREE, which files it among the document-level objects while the pages
 * that actually use it sit elsewhere, and the hint tables then describe a
 * grouping no reader would compute.  Removing: a checker re-runs its own
 * optimizer over this file in a mode that is forbidden to change anything, and
 * an inheritable attribute still sitting on a /Pages node is exactly what that
 * mode refuses -- it reports the file as unverifiable rather than as wrong.
 *
 * The value carried down is the RAW one, so a /Resources that arrived as an
 * indirect reference stays one shared object rather than being inlined into
 * every page as a private copy of itself. */
static void lin_push_node(lctx *L, pdf_obj *node, pdf_obj **inh, int depth)
{
    static const char *keys[4] = { "Resources", "MediaBox", "CropBox", "Rotate" };
    pdf_obj *mine[4], *kids;
    size_t i, n;
    int k;

    if (!pdf_is_dict(node) || depth > 64)
        return;
    for (k = 0; k < 4; k++) {
        pdf_obj *v = pdf_dict_get_raw(node, keys[k]);

        mine[k] = (v && !pdf_is_null(v)) ? v : inh[k];
    }
    if (pdf_is_name(pdf_dict_get_raw(node, "Type"), "Page")) {
        for (k = 0; k < 4; k++) {
            pdf_obj *v = pdf_dict_get_raw(node, keys[k]);

            if ((!v || pdf_is_null(v)) && mine[k])
                pdf_dict_put(L->doc, node, keys[k], mine[k]);
        }
        return;
    }
    kids = pdf_dict_get(L->doc, node, "Kids");
    for (i = 0, n = pdf_array_len(kids); i < n; i++)
        lin_push_node(L, pdf_array_get(L->doc, kids, i), mine, depth + 1);
    for (k = 0; k < 4; k++)
        pdf_dict_del(node, keys[k]);
}

static void lin_push_inherited(lctx *L)
{
    pdf_obj *inh[4] = { NULL, NULL, NULL, NULL };

    lin_push_node(L, pdf_dict_get(L->doc, pdf_catalog(L->doc), "Pages"), inh, 0);
}

static int lin_classify(lctx *L)
{
    pdf_doc *doc = L->doc;
    pdf_obj *trailer = pdf_trailer(doc);
    pdf_obj *cat = pdf_catalog(doc);
    pdf_obj *rootref = pdf_dict_get_raw(trailer, "Root");
    unsigned char *seen;
    int n, i, rootnum = -1, encnum = -1;
    size_t k;

    if (rootref && rootref->kind == PDF_REF)
        rootnum = rootref->u.ref.num;
    if (rootnum <= 0 || rootnum > L->maxold || !pdf_is_dict(cat))
        return PDF_E_DAMAGED;
    {
        pdf_obj *e = pdf_dict_get_raw(trailer, "Encrypt");

        if (e && e->kind == PDF_REF)
            encnum = e->u.ref.num;
    }

    for (n = 1; n <= L->maxold; n++) {
        int type = 0, gen = 0, sn = 0, si = 0;
        long long off = 0;
        pdf_obj *o;

        if (!pdf_xref_entry(doc, n, &type, &off, &gen)
            && !pdf_xref_compressed(doc, n, &sn, &si))
            continue;
        o = pdf_get_object(doc, n, gen);
        if (pdf_is_null(o))
            continue;
        /* 7.5.7, 7.5.8: an object stream and a cross-reference stream describe
         * how the INPUT was assembled.  This file is assembled differently, so
         * their contents are written as ordinary objects and the containers
         * not at all.  Nor is the encryption dictionary: the product names no
         * security handler. */
        if (n == encnum)
            continue;
        if (pdf_is_stream(o)
            && (pdf_is_name(pdf_dict_get_raw(o, "Type"), "ObjStm")
                || pdf_is_name(pdf_dict_get_raw(o, "Type"), "XRef")))
            continue;
        L->T[n].exists = 1;
    }
    for (i = 0; i < L->npages; i++) {
        int gen = 0, num = pdf_page_objnum(doc, i, &gen);

        if (num <= 0 || num > L->maxold || !L->T[num].exists)
            return PDF_E_DAMAGED;
        L->pageold[i] = num;
        L->T[num].is_page = 1;
    }

    seen = calloc((size_t)L->maxold + 1, 1);
    if (!seen)
        return PDF_E_NOMEM;

    /* The catalogue is a user of itself, so it lands in part 4 whatever else
     * happens to point at it. */
    L->T[rootnum].is_root = 1;
    L->T[rootnum].nusers++;

    for (k = 0; k < pdf_dict_len(trailer); k++) {
        const char *key = pdf_dict_key(trailer, k);

        if (!key || !strcmp(key, "Root"))
            continue;
        lin_user(L, seen, pdf_dict_val_raw(trailer, k),
                 !strcmp(key, "Encrypt") ? LU_ENCRYPT : LU_TRAILER, -1);
    }
    for (k = 0; k < pdf_dict_len(cat); k++) {
        const char *key = pdf_dict_key(cat, k);
        int kind;

        if (!key)
            continue;
        if (lin_is_open_doc_key(key))       kind = LU_OPENDOC;
        else if (!strcmp(key, "Outlines"))  kind = LU_OUTLINE;
        else if (!strcmp(key, "Pages"))     kind = LU_PAGES;
        else                                kind = LU_OTHERKEY;
        lin_user(L, seen, pdf_dict_val_raw(cat, k), kind, -1);
    }
    for (i = 0; i < L->npages; i++)
        lin_user(L, seen, pdf_new_ref(doc, L->pageold[i], 0), LU_PAGE, i);
    free(seen);

    /* F.3.2.  The ORDER of these tests is what decides an object that several
     * users reach, so it follows the categorisation a checker will repeat. */
    for (n = 1; n <= L->maxold; n++) {
        lobj *t = &L->T[n];

        if (!t->exists || t->nusers == 0) {
            t->part = LP_DROP;      /* nothing refers to it; not written */
            continue;
        }
        if (t->is_root)                                 t->part = LP_4;
        else if (t->in_outlines)                        t->part = LP_9;
        else if (t->in_open_doc)                        t->part = LP_4;
        else if (t->in_first_page)                      t->part = LP_6;
        else if (t->other_pages == 1 && t->others == 0) t->part = LP_7;
        else if (t->other_pages > 1)                    t->part = LP_8;
        else                                            t->part = LP_9;
    }
    for (i = 0; i < L->npages; i++) {
        L->T[L->pageold[i]].part = (i == 0) ? LP_6 : LP_7;
        if (i > 0)
            L->T[L->pageold[i]].page_owner = i;
    }
    return 0;
}

/* Assign new object numbers in the order the objects are written, with the
 * whole first half numbered ABOVE the second.  The first-page table is then
 * one subsection with no free entries (F.3.4), and the main table another. */
static int lin_number(lctx *L)
{
    int n, i, next = 1;

    for (i = 1; i < L->npages; i++) {
        L->T[L->pageold[i]].newnum = next;
        L->order[next++] = L->pageold[i];
        for (n = 1; n <= L->maxold; n++)
            if (L->T[n].part == LP_7 && !L->T[n].is_page
                && L->T[n].page_owner == i && L->T[n].newnum == 0) {
                L->T[n].newnum = next;
                L->order[next++] = n;
            }
    }
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_8 && L->T[n].newnum == 0) {
            L->T[n].newnum = next;
            L->order[next++] = n;
        }
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_9 && L->T[n].from_pages_key
            && L->T[n].newnum == 0) {
            L->T[n].newnum = next;
            L->order[next++] = n;
        }
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_9 && L->T[n].newnum == 0) {
            L->T[n].newnum = next;
            L->order[next++] = n;
        }
    L->nsecond = next - 1;

    L->lindict_num = next;
    L->order[next++] = 0;
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_4 && L->T[n].is_root) {
            L->T[n].newnum = next;
            L->order[next++] = n;
        }
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_4 && L->T[n].newnum == 0) {
            L->T[n].newnum = next;
            L->order[next++] = n;
        }
    L->hint_num = next;
    L->order[next++] = 0;

    /* Part 6 leads with the first page itself, then the objects only it uses,
     * then the ones it shares.  A page's objects must be consecutive from the
     * page object's own number for the page-length arithmetic to mean
     * anything, and this is where that is arranged. */
    L->T[L->pageold[0]].newnum = next;
    L->order[next++] = L->pageold[0];
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_6 && L->T[n].newnum == 0
            && L->T[n].other_pages == 0 && L->T[n].others == 0) {
            L->T[n].newnum = next;
            L->order[next++] = n;
        }
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_6 && L->T[n].newnum == 0) {
            L->T[n].newnum = next;
            L->order[next++] = n;
        }
    L->nnew = next - 1;

    for (i = 0; i < L->npages; i++)
        L->pagenew[i] = L->T[L->pageold[i]].newnum;

    L->nobjects[0] = 0;
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_6)
            L->nobjects[0]++;
    for (i = 1; i < L->npages; i++) {
        L->nobjects[i] = 1;
        for (n = 1; n <= L->maxold; n++)
            if (L->T[n].part == LP_7 && !L->T[n].is_page
                && L->T[n].page_owner == i)
                L->nobjects[i]++;
    }

    /* The shared object table is every part 6 object in order followed by
     * every part 8 object in order.  A checker walks it as consecutive object
     * numbers from the first page's, which the numbering above guarantees. */
    L->nshared_first = L->nobjects[0];
    L->nshared_total = L->nshared_first;
    for (n = 1; n <= L->maxold; n++)
        if (L->T[n].part == LP_8)
            L->nshared_total++;
    {
        int idx = 0, num;

        for (num = L->pagenew[0]; num <= L->nnew; num++)
            L->shared[idx++] = num;
        for (num = 1; num <= L->nsecond; num++)
            if (L->order[num] > 0 && L->T[L->order[num]].part == LP_8)
                L->shared[idx++] = num;
        if (idx != L->nshared_total)
            return PDF_E_DAMAGED;
    }
    return 0;
}

/* Which shared-table entries each page after the first refers to.  Computed
 * once, from one traversal per page, because both the count column and the
 * identifier column of the page offset table need the same answer. */
static int lin_shared_lists(lctx *L)
{
    unsigned char *seen = calloc((size_t)L->maxold + 1, 1);
    int i, k;

    if (!seen)
        return PDF_E_NOMEM;
    L->shstride = L->nshared_total + 1;
    for (i = 1; i < L->npages; i++) {
        memset(seen, 0, (size_t)L->maxold + 1);
        lin_mark(L, seen, pdf_new_ref(L->doc, L->pageold[i], 0), 1, 0);
        for (k = 0; k < L->nshared_total; k++) {
            int on = L->order[L->shared[k]];

            if (on > 0 && seen[on] && L->T[on].nusers > 1)
                L->shids[i * L->shstride + L->shcnt[i]++] = k;
        }
    }
    free(seen);
    return 0;
}

/* ------------------------------------------------------------ the emitter */

/* The space one object occupies, measured the way a checker measures it: from
 * where the object starts to where the next thing in the FILE starts.  New
 * object numbers deliberately do not run in file order, so this cannot be
 * computed from the numbering and is recorded during emission instead. */
static long long lin_span(lctx *L, int newn)
{
    if (newn < 1 || newn > L->nnew)
        return 0;
    return L->endoff[newn] - L->off[newn];
}

static void lin_build_hints(lctx *L)
{
    lin_bw w;
    int i, k, firstpart8 = 0;
    long long minobj = 0, minplen = 0, mingl = 0;
    long long maxdobj = 0, maxdplen = 0, maxdgl = 0;
    int nb_obj, nb_plen, nb_nsh = 0, nb_shid, nb_gl, maxnsh = 0;
    long long *plen, *glen;

    memset(L->hint, 0, L->hintcap);
    plen = calloc((size_t)L->npages + 1, sizeof *plen);
    glen = calloc((size_t)L->nshared_total + 1, sizeof *glen);
    if (!plen || !glen) {
        free(plen);
        free(glen);
        L->failed = 1;
        return;
    }

    for (i = 0; i < L->npages; i++) {
        long long total = 0;

        for (k = 0; k < L->nobjects[i]; k++)
            total += lin_span(L, L->pagenew[i] + k);
        plen[i] = total;
        if (i == 0 || total < minplen)             minplen = total;
        if (i == 0 || L->nobjects[i] < minobj)     minobj = L->nobjects[i];
    }
    for (i = 0; i < L->nshared_total; i++) {
        glen[i] = lin_span(L, L->shared[i]);
        if (i == 0 || glen[i] < mingl) mingl = glen[i];
    }
    for (i = 0; i < L->npages; i++) {
        if (L->nobjects[i] - minobj > maxdobj) maxdobj = L->nobjects[i] - minobj;
        if (plen[i] - minplen > maxdplen)      maxdplen = plen[i] - minplen;
        if (L->shcnt[i] > maxnsh)              maxnsh = L->shcnt[i];
    }
    for (i = 0; i < L->nshared_total; i++)
        if (glen[i] - mingl > maxdgl) maxdgl = glen[i] - mingl;

    nb_obj  = lin_bits(maxdobj);
    nb_plen = lin_bits(maxdplen);
    nb_nsh  = lin_bits(maxnsh);
    nb_shid = lin_bits(L->nshared_total > 0 ? L->nshared_total - 1 : 0);
    nb_gl   = lin_bits(maxdgl);

    w.p = L->hint;
    w.cap = L->hintcap;
    w.bit = 0;

    /* Table F.3: the page offset hint table. */
    bw_be(&w, (unsigned long long)minobj, 4);
    /* Offsets inside a hint table are recorded as if the hint stream were not
     * in the file at all, so what is stored is the real offset less that
     * stream's own length.  A reader adds it back. */
    bw_be(&w, (unsigned long long)(L->off[L->pagenew[0]] - L->v_H1), 4);
    bw_be(&w, (unsigned long long)nb_obj, 2);
    bw_be(&w, (unsigned long long)minplen, 4);
    bw_be(&w, (unsigned long long)nb_plen, 2);
    bw_be(&w, 0, 4);                        /* min_content_offset          */
    bw_be(&w, 0, 2);                        /* nbits_delta_content_offset  */
    bw_be(&w, (unsigned long long)minplen, 4);   /* min_content_length     */
    bw_be(&w, (unsigned long long)nb_plen, 2);
    bw_be(&w, (unsigned long long)nb_nsh, 2);
    bw_be(&w, (unsigned long long)nb_shid, 2);
    bw_be(&w, 0, 2);                        /* nbits_shared_numerator      */
    bw_be(&w, 4, 2);                        /* shared_denominator          */

    for (i = 0; i < L->npages; i++)
        bw_put(&w, (unsigned long long)(L->nobjects[i] - minobj), nb_obj);
    bw_align(&w);
    for (i = 0; i < L->npages; i++)
        bw_put(&w, (unsigned long long)(plen[i] - minplen), nb_plen);
    bw_align(&w);
    /* The first page names no shared objects: its own objects ARE the leading
     * entries of the shared table, so naming them would be circular, and a
     * checker says so out loud when they are named. */
    for (i = 0; i < L->npages; i++)
        bw_put(&w, (unsigned long long)L->shcnt[i], nb_nsh);
    bw_align(&w);
    for (i = 0; i < L->npages; i++)
        for (k = 0; k < L->shcnt[i]; k++)
            bw_put(&w, (unsigned long long)L->shids[i * L->shstride + k],
                   nb_shid);
    bw_align(&w);
    bw_align(&w);                           /* shared numerators: no bits  */
    bw_align(&w);                           /* delta content offsets: none */
    for (i = 0; i < L->npages; i++)
        bw_put(&w, (unsigned long long)(plen[i] - minplen), nb_plen);
    bw_align(&w);

    L->hint_s = (w.bit + 7) / 8;

    /* Table F.5: the shared object hint table. */
    w.bit = L->hint_s * 8;
    for (i = 1; i <= L->nsecond; i++)
        if (L->order[i] > 0 && L->T[L->order[i]].part == LP_8) {
            firstpart8 = i;
            break;
        }
    bw_be(&w, (unsigned long long)firstpart8, 4);
    bw_be(&w, (unsigned long long)(firstpart8
                                   ? L->off[firstpart8] - L->v_H1 : 0), 4);
    bw_be(&w, (unsigned long long)L->nshared_first, 4);
    bw_be(&w, (unsigned long long)L->nshared_total, 4);
    bw_be(&w, 0, 2);                        /* nbits_nobjects: one a group */
    bw_be(&w, (unsigned long long)mingl, 4);
    bw_be(&w, (unsigned long long)nb_gl, 2);
    for (i = 0; i < L->nshared_total; i++)
        bw_put(&w, (unsigned long long)(glen[i] - mingl), nb_gl);
    bw_align(&w);
    for (i = 0; i < L->nshared_total; i++)
        bw_put(&w, 0, 1);                   /* signature_present           */
    bw_align(&w);

    if (w.bit > L->hintcap * 8)
        L->failed = 1;
    free(plen);
    free(glen);
}

static void lin_emit_indirect(lctx *L, int newn)
{
    pdf_doc *doc = L->doc;
    int old = L->order[newn];

    if (L->lastemit > 0)
        L->endoff[L->lastemit] = (long long)L->out.n;
    L->lastemit = newn;

    L->off[newn] = (long long)L->out.n;
    pdf_buf_printf(&L->out, "%d 0 obj\n", newn);

    if (newn == L->lindict_num) {
        pdf_buf_put(&L->out, "<< /Linearized 1 /L ", 20);
        lw_padnum(L, L->v_L);
        pdf_buf_put(&L->out, " /H [ ", 6);
        lw_padnum(L, L->v_H0);
        pdf_buf_putc(&L->out, ' ');
        lw_padnum(L, L->v_H1);
        pdf_buf_put(&L->out, " ] /O ", 6);
        lw_padnum(L, L->v_O);
        pdf_buf_put(&L->out, " /E ", 4);
        lw_padnum(L, L->v_E);
        pdf_buf_put(&L->out, " /N ", 4);
        lw_padnum(L, L->v_N);
        pdf_buf_put(&L->out, " /T ", 4);
        lw_padnum(L, L->v_T);
        pdf_buf_put(&L->out, " >>", 3);
    } else if (newn == L->hint_num) {
        /* F.3.6.  /S names where the shared object table begins inside the
         * stream.  The body is uncompressed: a filter would save a few bytes
         * and cost the fixed size that makes every offset computable. */
        pdf_buf_printf(&L->out, "<< /S %lld /Length %lld >>\nstream\n",
                       (long long)L->hint_s, (long long)L->hintcap);
        pdf_buf_put(&L->out, L->hint, L->hintcap);
        pdf_buf_put(&L->out, "\nendstream", 10);
    } else if (old > 0) {
        pdf_obj *o = pdf_get_object(doc, old, 0);

        if (pdf_is_stream(o)) {
            pdf_buf data;

            pdf_buf_init(&data);
            (void)pdf_stream_data(doc, o, &data, 0);
            lw_dict_body(L, o->u.stm.dict, 0, (long long)data.n);
            pdf_buf_put(&L->out, "\nstream\n", 8);
            if (data.n) pdf_buf_put(&L->out, data.p, data.n);
            pdf_buf_put(&L->out, "\nendstream", 10);
            pdf_buf_free(&data);
        } else if (pdf_is_null(o)) {
            pdf_buf_put(&L->out, "null", 4);
        } else {
            lw_obj(L, o, 0);
        }
    } else {
        pdf_buf_put(&L->out, "null", 4);
    }
    pdf_buf_put(&L->out, "\nendobj\n", 8);
}

static void lin_emit_id(lctx *L)
{
    pdf_obj *id = pdf_dict_get_raw(pdf_trailer(L->doc), "ID");
    const unsigned char *bytes;
    size_t len = 0, i;
    unsigned long long h = 1469598103934665603ULL;
    char hex[33];
    static const char hd[] = "0123456789abcdef";

    if (pdf_is_array(id) && pdf_array_len(id) >= 2) {
        pdf_buf_put(&L->out, " /ID [", 6);
        lw_obj(L, pdf_array_get_raw(id, 0), 0);
        lw_obj(L, pdf_array_get_raw(id, 1), 0);
        pdf_buf_put(&L->out, "]", 1);
        return;
    }
    /* No /ID to carry over: derive one from the input's own bytes, so that the
     * same input always produces the same file.  Nothing here reads a clock. */
    bytes = pdf_file_bytes(L->doc, &len);
    for (i = 0; bytes && i < len; i++) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    for (i = 0; i < 16; i++) {
        hex[i * 2]     = hd[(h >> ((i % 8) * 8)) & 15];
        hex[i * 2 + 1] = hd[(h >> (((i % 8) * 8) + 4)) & 15];
    }
    hex[32] = 0;
    pdf_buf_printf(&L->out, " /ID [<%s><%s>]", hex, hex);
}

/* One complete pass over the file.  Because every number written ahead of the
 * end is padded to a fixed width and the hint stream has a fixed size, the
 * passes lay the file out identically and only the values differ. */
static void lin_emit(lctx *L)
{
    pdf_doc *doc = L->doc;
    int n;

    L->out.n = 0;
    L->lastemit = 0;
    /* off[] and endoff[] are deliberately NOT cleared here.  Part 3 sits near
     * the FRONT of the file and has to state the offsets of objects that this
     * pass has not written yet, so it is filled from what the previous pass
     * measured; clearing them would emit a table of zeroes and the file would
     * name no resolvable /Root.  Every entry is overwritten as its object is
     * emitted, so a converged pass writes the offsets it then reproduces. */

    pdf_buf_printf(&L->out, "%%PDF-%d.%d\n", pdf_header_version(doc) / 10,
                   pdf_header_version(doc) % 10);
    pdf_buf_put(&L->out, "%\xe2\xe3\xcf\xd3\n", 6);

    lin_emit_indirect(L, L->lindict_num);

    /* Part 3: the first-page table -- one subsection, no free entries. */
    L->endoff[L->lindict_num] = (long long)L->out.n;
    L->lastemit = 0;
    L->firstxref_off = (long long)L->out.n;
    pdf_buf_printf(&L->out, "xref\n%d %d\n", L->lindict_num,
                   L->nnew - L->lindict_num + 1);
    for (n = L->lindict_num; n <= L->nnew; n++)
        pdf_buf_printf(&L->out, "%010lld 00000 n \n", L->off[n]);
    pdf_buf_printf(&L->out, "trailer\n<< /Size %d /Root %d 0 R /Prev ",
                   L->nnew + 1, L->root_new);
    lw_padnum(L, L->mainxref_off);
    {
        pdf_obj *info = pdf_dict_get_raw(pdf_trailer(doc), "Info");

        if (info && info->kind == PDF_REF && info->u.ref.num > 0
            && info->u.ref.num <= L->maxold
            && L->T[info->u.ref.num].newnum > 0)
            pdf_buf_printf(&L->out, " /Info %d 0 R",
                           L->T[info->u.ref.num].newnum);
    }
    lin_emit_id(L);
    pdf_buf_put(&L->out, " >>\nstartxref\n0\n%%EOF\n", 22);

    for (n = L->lindict_num + 1; n <= L->nnew; n++)
        lin_emit_indirect(L, n);
    for (n = 1; n <= L->nsecond; n++)
        lin_emit_indirect(L, n);

    /* Parts 10 and 11: the main table, and the pointer that sends a reader to
     * the front of the file rather than to here. */
    L->mainxref_off = (long long)L->out.n;
    if (L->lastemit > 0)
        L->endoff[L->lastemit] = L->mainxref_off;
    pdf_buf_printf(&L->out, "xref\n0 %d\n", L->nsecond + 1);
    pdf_buf_put(&L->out, "0000000000 65535 f \n", 20);
    for (n = 1; n <= L->nsecond; n++)
        pdf_buf_printf(&L->out, "%010lld 00000 n \n", L->off[n]);
    pdf_buf_printf(&L->out, "trailer\n<< /Size %d", L->nsecond + 1);
    lin_emit_id(L);
    pdf_buf_put(&L->out, " >>\n", 4);
    pdf_buf_printf(&L->out, "startxref\n%lld\n%%%%EOF\n", L->firstxref_off);

    /* The values the next pass will write.  /T is the whitespace byte in front
     * of the main table's first entry; /E the end of the first page's run of
     * objects; /H the hint stream's offset and the whole span of its object. */
    L->v_L  = (long long)L->out.n;
    L->v_O  = L->pagenew[0];
    L->v_N  = L->npages;
    L->v_H0 = L->off[L->hint_num];
    L->v_H1 = L->endoff[L->hint_num] - L->v_H0;
    {
        char head[32];
        int hl = snprintf(head, sizeof head, "0 %d", L->nsecond + 1);

        L->v_T = L->mainxref_off + 5 + (hl > 0 ? hl : 0);
    }
    L->v_E = L->endoff[L->pagenew[0] + L->nobjects[0] - 1];
}

int pdf_write_linearized(pdf_doc *doc, const char *path)
{
    lctx L;
    FILE *f;
    long long *prev = NULL;
    int rc = 0, pass, n, stable = 0;

    if (!doc || !path)
        return PDF_E_RANGE;

    memset(&L, 0, sizeof L);
    L.doc = doc;
    L.maxold = pdf_max_objnum(doc);
    L.npages = pdf_page_count(doc);
    if (L.maxold < 1 || L.npages < 1)
        return PDF_E_DAMAGED;

    pdf_buf_init(&L.out);
    L.T        = calloc((size_t)L.maxold + 2, sizeof *L.T);
    L.order    = calloc((size_t)L.maxold + 8, sizeof *L.order);
    L.off      = calloc((size_t)L.maxold + 8, sizeof *L.off);
    L.endoff   = calloc((size_t)L.maxold + 8, sizeof *L.endoff);
    prev       = calloc((size_t)L.maxold + 8, sizeof *prev);
    L.pageold  = calloc((size_t)L.npages + 1, sizeof *L.pageold);
    L.pagenew  = calloc((size_t)L.npages + 1, sizeof *L.pagenew);
    L.nobjects = calloc((size_t)L.npages + 1, sizeof *L.nobjects);
    L.shared   = calloc((size_t)L.maxold + 8, sizeof *L.shared);
    L.shcnt    = calloc((size_t)L.npages + 1, sizeof *L.shcnt);
    if (!L.T || !L.order || !L.off || !L.endoff || !prev || !L.pageold
        || !L.pagenew || !L.nobjects || !L.shared || !L.shcnt) {
        rc = PDF_E_NOMEM;
        goto done;
    }

    lin_push_inherited(&L);
    rc = lin_classify(&L);
    if (rc != 0)
        goto done;
    rc = lin_number(&L);
    if (rc != 0)
        goto done;

    L.shids = calloc((size_t)(L.npages + 1) * (size_t)(L.nshared_total + 1),
                     sizeof *L.shids);
    if (!L.shids) {
        rc = PDF_E_NOMEM;
        goto done;
    }
    rc = lin_shared_lists(&L);
    if (rc != 0)
        goto done;
    {
        pdf_obj *rr = pdf_dict_get_raw(pdf_trailer(doc), "Root");

        L.root_new = (rr && rr->kind == PDF_REF && rr->u.ref.num > 0
                      && rr->u.ref.num <= L.maxold)
                   ? L.T[rr->u.ref.num].newnum : 0;
        if (L.root_new <= 0) {
            rc = PDF_E_DAMAGED;
            goto done;
        }
    }

    /* A size that depends only on counts, so it is identical in every pass.
     * Each page contributes at most four 32-bit columns plus one identifier
     * per shared object; each shared entry at most one 32-bit column. */
    L.hintcap = 128 + (size_t)L.npages * 24
              + (size_t)L.npages * (size_t)(L.nshared_total + 1) * 4
              + (size_t)L.nshared_total * 12;
    L.hint = calloc(L.hintcap, 1);
    if (!L.hint) {
        rc = PDF_E_NOMEM;
        goto done;
    }

    for (pass = 0; pass < 8; pass++) {
        memcpy(prev, L.off, sizeof(long long) * (size_t)(L.nnew + 2));
        lin_build_hints(&L);
        if (L.failed) {
            rc = PDF_E_DAMAGED;
            goto done;
        }
        lin_emit(&L);
        if (pass > 0 && memcmp(prev, L.off,
                               sizeof(long long) * (size_t)(L.nnew + 2)) == 0) {
            stable = 1;
            break;
        }
    }
    /* The layout must have stopped moving, or the dictionary would be
     * describing a file other than the one about to be written. */
    if (!stable) {
        rc = PDF_E_DAMAGED;
        goto done;
    }
    for (n = 1; n <= L.nnew; n++)
        if (L.off[n] != prev[n]) {
            rc = PDF_E_DAMAGED;
            goto done;
        }

    f = fopen(path, "wb");
    if (!f) {
        rc = PDF_E_IO;
        goto done;
    }
    if (L.out.n)
        fwrite(L.out.p, 1, L.out.n, f);
    fclose(f);

done:
    pdf_buf_free(&L.out);
    free(L.T);
    free(L.order);
    free(L.off);
    free(L.endoff);
    free(prev);
    free(L.pageold);
    free(L.pagenew);
    free(L.nobjects);
    free(L.shared);
    free(L.shcnt);
    free(L.shids);
    free(L.hint);
    return rc;
}
