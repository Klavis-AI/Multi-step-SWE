/* doc.c -- the document catalog, resolving accessors and the page tree.
 *
 * ISO 32000-1 clause 7.7 (Document Structure):
 *   7.7.2  the document catalog      7.7.3  the page tree
 *   7.7.3.3 page attributes and the inheritable ones (table 30)
 *   14.11.2 the page boundaries (MediaBox, CropBox and the rest)
 *
 * Nothing here trusts the structure it is walking.  A page tree is an
 * arbitrary graph as far as this file is concerned: /Kids may point back up,
 * /Parent may form a ring, /Count may be a work of fiction, and /Type may be
 * missing everywhere.  Every walk is therefore both depth-capped and
 * visited-set guarded, and every one of them has a fallback for the case where
 * the tree yields nothing at all.
 */

#include "pdfint.h"

#include <math.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ limits */

#define DOC_MAX_PAGES    1048576    /* pages we are willing to flatten        */
#define DOC_MAX_DEPTH    256        /* /Kids nesting                          */
#define DOC_MAX_KIDS     65536      /* entries of one /Kids array             */
#define DOC_MAX_INHERIT  64         /* /Parent hops for an inherited value    */
#define DOC_MAX_CONTENTS 8192       /* streams in one /Contents array         */
#define DOC_MAX_SCAN     1048576    /* objects one recovery scan may look at  */

/* US Letter, the default when /MediaBox is missing or unusable. */
#define DOC_DEF_W 612.0
#define DOC_DEF_H 792.0

/* --------------------------------------------------------------- accessors */

pdf_obj *pdf_dict_get(pdf_doc *doc, pdf_obj *dict, const char *key)
{
    return pdf_resolve(doc, pdf_dict_get_raw(dict, key));
}

/* Two spellings of one entry -- /Filter and /F, /DecodeParms and /DP -- with
 * the long form winning when both are present. */
pdf_obj *pdf_dict_geta(pdf_doc *doc, pdf_obj *dict, const char *k1,
                       const char *k2)
{
    pdf_obj *v = pdf_resolve(doc, pdf_dict_get_raw(dict, k1));

    if (!pdf_is_null(v)) return v;
    return pdf_resolve(doc, pdf_dict_get_raw(dict, k2));
}

pdf_obj *pdf_array_get(pdf_doc *doc, pdf_obj *arr, size_t i)
{
    return pdf_resolve(doc, pdf_array_get_raw(arr, i));
}

/* ----------------------------------------------------------------- catalog */

/* Last resort when the trailer has no usable /Root: look for the catalog
 * object itself.  One with a page tree is preferred over one without. */
static pdf_obj *doc_scan_catalog(pdf_doc *doc)
{
    pdf_obj *any = pdf_null_obj();
    long long n, cap;

    if (!doc->xref) return any;
    cap = doc->maxobj < DOC_MAX_SCAN ? doc->maxobj : DOC_MAX_SCAN;
    for (n = 1; n <= cap && n < doc->xrefcap; n++) {
        pdf_obj *o;

        if (doc->xref[n].type == PDF_XREF_FREE) continue;
        o = pdf_load_object(doc, (int)n);
        if (!pdf_is_dict(o) || pdf_is_stream(o)) continue;
        if (!pdf_is_name(pdf_dict_get_raw(o, "Type"), "Catalog")) continue;
        if (pdf_is_dict(pdf_dict_get(doc, o, "Pages"))) return o;
        if (pdf_is_null(any)) any = o;
    }
    return any;
}

pdf_obj *pdf_catalog(pdf_doc *doc)
{
    pdf_obj *root;

    if (!doc) return pdf_null_obj();
    if (doc->catalog) return doc->catalog;      /* a cached failure counts */

    root = pdf_dict_get(doc, doc->trailer, "Root");
    if (!pdf_is_dict(root) || pdf_is_stream(root)) {
        pdf_xref_reconstruct(doc);              /* no-op once already done */
        root = pdf_dict_get(doc, doc->trailer, "Root");
    }
    if (!pdf_is_dict(root) || pdf_is_stream(root))
        root = doc_scan_catalog(doc);

    doc->catalog = (pdf_is_dict(root) && !pdf_is_stream(root))
                       ? root : pdf_null_obj();
    return doc->catalog;
}

/* --------------------------------------------------------------- page tree */

/* Cycle detection walks the current path, not a global visited set: a node is
 * only a cycle if it is its own ancestor.  A file that lists one page twice
 * under a single /Kids is malformed but perfectly readable, and both copies
 * are pages -- a global set would silently drop the second.  The path alone
 * would still allow a deep diamond to take exponential time, so the number of
 * nodes the walk may visit at all is budgeted too. */
typedef struct {
    int  num[DOC_MAX_DEPTH + 2];    /* object numbers from the root down */
    int  n;
    long budget;
} pg_path;

/* pdf_doc carries no capacity field, so the array is grown on the powers of
 * two: its length is 8 while npages < 8, and 2*npages at every power after. */
static int pg_add(pdf_doc *doc, pdf_obj *page)
{
    if (doc->npages >= DOC_MAX_PAGES) return PDF_E_RANGE;
    if (doc->npages == 0 ||
        (doc->npages >= 8 && (doc->npages & (doc->npages - 1)) == 0)) {
        size_t cap = doc->npages ? (size_t)doc->npages * 2 : 8;
        pdf_obj **np = realloc(doc->pages, cap * sizeof *np);
        if (!np) return PDF_E_NOMEM;
        doc->pages = np;
    }
    doc->pages[doc->npages++] = page;
    return 0;
}

static void pg_walk(pdf_doc *doc, pg_path *path, pdf_obj *noderaw, int depth)
{
    pdf_obj *node, *kids;
    const char *type;
    size_t i, n;
    int num = -1, k;

    if (depth > DOC_MAX_DEPTH || doc->npages >= DOC_MAX_PAGES) return;
    if (path->budget-- <= 0) return;
    if (noderaw && noderaw->kind == PDF_REF) {
        num = noderaw->u.ref.num;
        for (k = 0; k < path->n; k++)
            if (path->num[k] == num) return;    /* its own ancestor: a ring */
    }

    node = pdf_resolve(doc, noderaw);
    if (!pdf_is_dict(node) || pdf_is_stream(node)) return;

    type = pdf_to_name(pdf_dict_get(doc, node, "Type"));
    kids = pdf_dict_get(doc, node, "Kids");

    /* /Count is not consulted at all: it is wrong often enough that the kids
     * themselves are the only honest answer.  /Type is advisory too -- an
     * interior node is one with /Kids -- except that /Type /Page wins over a
     * stray /Kids, because a page has no children (7.7.3.3). */
    if (pdf_is_array(kids) && strcmp(type, "Page") != 0) {
        n = pdf_array_len(kids);
        if (n > DOC_MAX_KIDS) n = DOC_MAX_KIDS;
        if (path->n >= DOC_MAX_DEPTH + 2) return;
        path->num[path->n++] = num;             /* -1 for a direct dictionary */
        for (i = 0; i < n; i++)
            pg_walk(doc, path, pdf_array_get_raw(kids, i), depth + 1);
        path->n--;
        return;
    }
    if (strcmp(type, "Pages") == 0) return;     /* an interior node, kidless */
    if (depth == 0 && strcmp(type, "Page") != 0)
        return;                                 /* /Pages is not itself a page */
    (void)pg_add(doc, node);
}

/* When the tree yields nothing, every /Type /Page object in the file is a
 * page, taken in object-number order. */
static void pg_scan_all(pdf_doc *doc)
{
    long long n, cap;

    if (!doc->xref) return;
    cap = doc->maxobj < DOC_MAX_SCAN ? doc->maxobj : DOC_MAX_SCAN;
    for (n = 1; n <= cap && n < doc->xrefcap; n++) {
        pdf_obj *o;

        if (doc->xref[n].type == PDF_XREF_FREE) continue;
        o = pdf_load_object(doc, (int)n);
        if (!pdf_is_dict(o) || pdf_is_stream(o)) continue;
        if (!pdf_is_name(pdf_dict_get_raw(o, "Type"), "Page")) continue;
        if (pg_add(doc, o) != 0) break;
    }
}

int pdf_build_pages(pdf_doc *doc)
{
    pg_path path;
    pdf_obj *cat;

    if (!doc) return PDF_E_RANGE;
    if (doc->pages_built) return 0;

    /* Ahead of the guard: resolving the catalog may rebuild the whole
     * cross-reference table, which resets exactly the fields set below. */
    cat = pdf_catalog(doc);

    doc->pages_built = 1;
    doc->npages      = 0;

    path.n      = 0;
    path.budget = 4L * DOC_MAX_PAGES;

    pg_walk(doc, &path, pdf_dict_get_raw(cat, "Pages"), 0);

    if (doc->npages == 0) pg_scan_all(doc);
    return 0;
}

int pdf_page_count(pdf_doc *doc)
{
    if (!doc) return 0;
    pdf_build_pages(doc);
    return doc->npages;
}

pdf_obj *pdf_page_dict(pdf_doc *doc, int index)
{
    if (!doc) return pdf_null_obj();
    pdf_build_pages(doc);
    if (index < 0 || index >= doc->npages || !doc->pages)
        return pdf_null_obj();
    return doc->pages[index] ? doc->pages[index] : pdf_null_obj();
}

/* --------------------------------------------------------- page attributes */

/* Table 30: /Resources /MediaBox /CropBox and /Rotate are inherited from the
 * page's ancestors.  /Parent rings are common in damaged files, so the numbers
 * already visited are remembered as well as the hops being capped. */
pdf_obj *pdf_page_inherited(pdf_doc *doc, pdf_obj *page, const char *key)
{
    int seen[DOC_MAX_INHERIT];
    pdf_obj *node = page;
    int d, i;

    if (!doc || !key) return pdf_null_obj();

    for (d = 0; d < DOC_MAX_INHERIT; d++) {
        pdf_obj *v, *par;

        if (!pdf_is_dict(node)) break;
        v = pdf_dict_get(doc, node, key);
        if (!pdf_is_null(v)) return v;

        par = pdf_dict_get_raw(node, "Parent");
        seen[d] = -1;
        if (par && par->kind == PDF_REF) {
            for (i = 0; i < d; i++)
                if (seen[i] == par->u.ref.num) return pdf_null_obj();
            seen[d] = par->u.ref.num;
        }
        node = pdf_resolve(doc, par);
    }
    return pdf_null_obj();
}

/* A rectangle is any two opposite corners (7.9.5), so the result is always
 * normalised to x0 < x1 and y0 < y1. */
static int box_from(pdf_doc *doc, pdf_obj *a, pdf_rect *out)
{
    double v[4];
    size_t i;

    if (!pdf_is_array(a) || pdf_array_len(a) < 4) return 0;
    for (i = 0; i < 4; i++) {
        pdf_obj *e = pdf_array_get(doc, a, i);
        if (!pdf_is_num(e)) return 0;
        v[i] = pdf_to_real(e);
        if (!isfinite(v[i]) || v[i] < -1e9 || v[i] > 1e9) return 0;
    }
    out->x0 = v[0] < v[2] ? v[0] : v[2];
    out->x1 = v[0] < v[2] ? v[2] : v[0];
    out->y0 = v[1] < v[3] ? v[1] : v[3];
    out->y1 = v[1] < v[3] ? v[3] : v[1];
    return out->x1 > out->x0 && out->y1 > out->y0;
}

/* Clip a to b, falling back to b when nothing is left: an empty page box is
 * never the useful answer. */
static void box_clip(const pdf_rect *a, const pdf_rect *b, pdf_rect *out)
{
    pdf_rect r;

    r.x0 = a->x0 > b->x0 ? a->x0 : b->x0;
    r.y0 = a->y0 > b->y0 ? a->y0 : b->y0;
    r.x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    r.y1 = a->y1 < b->y1 ? a->y1 : b->y1;
    *out = (r.x1 > r.x0 && r.y1 > r.y0) ? r : *b;
}

int pdf_page_box(pdf_doc *doc, pdf_obj *page, const char *boxname, pdf_rect *out)
{
    const char *want = boxname ? boxname : "MediaBox";
    pdf_rect media, crop, r;

    if (!out) return PDF_E_RANGE;

    media.x0 = 0.0; media.y0 = 0.0;
    media.x1 = DOC_DEF_W; media.y1 = DOC_DEF_H;
    if (box_from(doc, pdf_page_inherited(doc, page, "MediaBox"), &r))
        media = r;
    if (strcmp(want, "MediaBox") == 0) {
        *out = media;
        return 0;
    }

    /* 14.11.2: /CropBox defaults to /MediaBox and is clipped to it. */
    crop = media;
    if (box_from(doc, pdf_page_inherited(doc, page, "CropBox"), &r))
        box_clip(&r, &media, &crop);
    if (strcmp(want, "CropBox") == 0) {
        *out = crop;
        return 0;
    }

    /* /BleedBox, /TrimBox and /ArtBox all default to the crop box.  They are
     * not inheritable, so they are read off the page itself. */
    *out = crop;
    if (box_from(doc, pdf_dict_get(doc, page, want), &r))
        box_clip(&r, &crop, out);
    return 0;
}

int pdf_page_rotate(pdf_doc *doc, pdf_obj *page)
{
    pdf_obj *r = pdf_page_inherited(doc, page, "Rotate");
    long long v;

    if (!pdf_is_num(r)) return 0;
    v = pdf_to_int(r);
    if (v % 90 != 0) return 0;          /* 7.7.3.3: shall be a multiple of 90 */
    v %= 360;                           /* negatives and >360 are legal input */
    if (v < 0) v += 360;
    return (int)v;
}

/* 7.8.2.  /Contents is one stream or an array of them, and the division
 * between the parts of an array may fall anywhere, so a newline is inserted
 * between them: without it the last token of one part and the first of the
 * next would fuse into a third token that is in neither. */
int pdf_page_content(pdf_doc *doc, pdf_obj *page, pdf_buf *out)
{
    pdf_obj *c;
    size_t i, n;

    if (!out) return PDF_E_RANGE;
    if (!doc) return PDF_E_RANGE;

    c = pdf_dict_get(doc, page, "Contents");
    if (pdf_is_stream(c))
        return pdf_stream_data(doc, c, out, 1);
    if (!pdf_is_array(c))
        return 0;                       /* a page with no contents is blank */

    n = pdf_array_len(c);
    if (n > DOC_MAX_CONTENTS) n = DOC_MAX_CONTENTS;
    for (i = 0; i < n; i++) {
        pdf_obj *s = pdf_array_get(doc, c, i);
        if (!pdf_is_stream(s)) continue;
        if (out->n && pdf_buf_putc(out, '\n') != 0) return PDF_E_NOMEM;
        if (pdf_stream_data(doc, s, out, 1) == PDF_E_NOMEM) return PDF_E_NOMEM;
    }
    return 0;
}

/* -------------------------------------------------------------- encryption */

int pdf_is_encrypted(pdf_doc *doc)
{
    return doc ? doc->encrypted : 0;
}

int pdf_is_unlocked(pdf_doc *doc)
{
    return doc ? doc->unlocked : 0;
}

/* Standard security handler lives in crypt.c, which is a later module.  Until
 * it exists the only document that can be unlocked is one that was never
 * locked; an encrypted file still opens and still parses, but its strings and
 * streams stay as they are on disk. */
int pdf_authenticate(pdf_doc *doc, const char *password)
{
    if (!doc) return PDF_E_RANGE;
    if (!doc->encrypted) {
        doc->unlocked = 1;
        return 0;
    }
    pdf_set_error(doc, "this document is encrypted and crypt.c is not built");
    return PDF_E_ENCRYPT;
}
