/* font.c -- building an internal font from a /Font resource dictionary, and
 * the pdffonts-style enumeration over a page range.
 *
 * Covers the simple fonts of ISO 32000-1:2008 9.6 (Type1, MMType1, TrueType,
 * Type3) and the composite fonts of 9.7 (Type0 with a CIDFont descendant).
 *
 * The "type" and "encoding" strings deliberately reproduce poppler's spelling
 * so that a pdffonts front end matches byte for byte; see the notes on
 * name_of_type() and pick_encoding() for where they come from.
 */

#include "fontint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Same order as poppler's fontTypeNames[] in utils/pdffonts.cc. */
enum {
    FT_UNKNOWN = 0, FT_TYPE1, FT_TYPE1C, FT_TYPE1C_OT, FT_TYPE3,
    FT_TRUETYPE, FT_TRUETYPE_OT, FT_CID_TYPE0, FT_CID_TYPE0C,
    FT_CID_TYPE0C_OT, FT_CID_TRUETYPE, FT_CID_TRUETYPE_OT
};

static const char *const g_typename[] = {
    "unknown", "Type 1", "Type 1C", "Type 1C (OT)", "Type 3", "TrueType",
    "TrueType (OT)", "CID Type 0", "CID Type 0C", "CID Type 0C (OT)",
    "CID TrueType", "CID TrueType (OT)"
};

/* /FontDescriptor /Flags bits (ISO 32000-1 table 123). */
#define FLAG_FIXEDPITCH  (1 << 0)
#define FLAG_SERIF       (1 << 1)
#define FLAG_SYMBOLIC    (1 << 2)
#define FLAG_ITALIC      (1 << 6)
#define FLAG_FORCEBOLD   (1 << 18)

typedef struct {
    unsigned int lo, hi;
    double       w;
} wrange;

struct pdf_font {
    pdf_doc *doc;
    pdf_obj *dict;

    int   ftype;
    int   is_type0, is_type3;
    int   std14;          /* real standard-14 match, or -1 */
    int   subst;          /* metric substitute used when /Widths is absent */
    char  basefont[192];

    /* --- simple fonts --- */
    const char *enc[256];
    double        w[256];
    unsigned char has_w[256];
    double        missing_w;
    int           symbolic;

    /* --- Type 3 --- */
    double   matrix[6];
    pdf_obj *charprocs;
    pdf_obj *t3res;

    /* --- Type 0 --- */
    pdf_cmap *encmap;
    double    dw;
    wrange   *wr;
    size_t    nwr, wrcap;
    int       vertical;
    int       cid_is_gid;
    pdf_obj  *cid2gid;

    /* --- extraction --- */
    pdf_cmap *tounicode;
    int       aj1;        /* descendant is an Adobe-Japan1 CIDFont */

    /* --- pdffonts columns --- */
    char typestr[40];
    char encstr[64];
    int  emb, sub, uni;
};

/* ------------------------------------------------------------- utilities */

static void str_copy(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!src) src = "";
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void *grow(pdf_doc *doc, void *old, size_t elem, size_t *cap, size_t need)
{
    size_t nc = *cap ? *cap : 32;
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

/* ------------------------------------------------- descriptor / font file */

typedef struct {
    int    embedded;
    int    ff3;        /* 0 none, 1 Type1C, 2 CIDFontType0C, 3 OpenType, 4 other */
    int    flags;
    double missing_w;
} descinfo;

static void read_desc(pdf_doc *doc, pdf_obj *desc, descinfo *di)
{
    pdf_obj *ff;

    memset(di, 0, sizeof *di);
    if (!pdf_is_dict(desc)) return;

    di->flags = (int)pdf_to_int(pdf_dict_get(doc, desc, "Flags"));
    if (pdf_is_num(pdf_dict_get(doc, desc, "MissingWidth")))
        di->missing_w = pdf_to_real(pdf_dict_get(doc, desc, "MissingWidth"));

    if (pdf_is_stream(pdf_dict_get(doc, desc, "FontFile")))  di->embedded = 1;
    if (pdf_is_stream(pdf_dict_get(doc, desc, "FontFile2"))) di->embedded = 1;

    ff = pdf_dict_get(doc, desc, "FontFile3");
    if (pdf_is_stream(ff)) {
        const char *st = pdf_to_name(pdf_dict_get(doc, ff, "Subtype"));
        di->embedded = 1;
        if (!strcmp(st, "Type1C"))              di->ff3 = 1;
        else if (!strcmp(st, "CIDFontType0C"))  di->ff3 = 2;
        else if (!strcmp(st, "OpenType"))       di->ff3 = 3;
        else                                    di->ff3 = 4;
    }
}

/* Poppler derives the type from /Subtype and refines it with the /FontFile3
 * subtype; a non-embedded font keeps the plain name. */
static int type_of(const char *subtype, const char *cidsubtype, const descinfo *di)
{
    if (!strcmp(subtype, "Type3")) return FT_TYPE3;

    if (!strcmp(subtype, "Type0")) {
        if (!strcmp(cidsubtype, "CIDFontType2"))
            return di->ff3 == 3 ? FT_CID_TRUETYPE_OT : FT_CID_TRUETYPE;
        if (di->ff3 == 2) return FT_CID_TYPE0C;
        if (di->ff3 == 3) return FT_CID_TYPE0C_OT;
        return FT_CID_TYPE0;
    }
    if (!strcmp(subtype, "TrueType"))
        return di->ff3 == 3 ? FT_TRUETYPE_OT : FT_TRUETYPE;
    if (!strcmp(subtype, "Type1") || !strcmp(subtype, "MMType1")) {
        if (di->ff3 == 1) return FT_TYPE1C;
        if (di->ff3 == 3) return FT_TYPE1C_OT;
        return FT_TYPE1;
    }
    /* No or unknown /Subtype: guess from what is embedded. */
    if (di->ff3 == 1) return FT_TYPE1C;
    if (di->ff3 == 3) return FT_TYPE1C_OT;
    return di->embedded ? FT_TYPE1 : FT_UNKNOWN;
}

/* --------------------------------------------------------- simple encoding */

/* Choose a metric stand-in when the font is not one of the 14 and carries no
 * /Widths.  Poppler substitutes a real system font here; the closest we can do
 * is pick the standard-14 face its descriptor flags describe. */
static int substitute_index(const char *basefont, int flags)
{
    int fam, bold, ital;
    char low[192];
    size_t i;

    for (i = 0; basefont[i] && i < sizeof low - 1; i++) {
        unsigned char c = (unsigned char)basefont[i];
        low[i] = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    }
    low[i] = '\0';

    if (flags & FLAG_FIXEDPITCH)   fam = PDF_STD14_COURIER;
    else if (flags & FLAG_SERIF)   fam = PDF_STD14_TIMES;
    else                           fam = PDF_STD14_HELVETICA;

    bold = (flags & FLAG_FORCEBOLD) || strstr(low, "bold") != NULL;
    ital = (flags & FLAG_ITALIC)    || strstr(low, "italic") != NULL ||
           strstr(low, "oblique") != NULL;
    return fam + (bold ? 1 : 0) + (ital ? 2 : 0);
}

/* Reproduces GfxFont::GfxFont() in poppler 22.12: the base encoding, then the
 * column label, then /Differences forcing "Custom". */
static void build_simple_enc(pdf_doc *doc, pdf_font *f, pdf_obj *encobj,
                             int embedded)
{
    const char *const *base = NULL;
    const char *basename = NULL;
    int i, has_entry = 0;

    if (pdf_is_name(encobj, NULL)) {
        basename = pdf_to_name(encobj);
        base = pdf_base_encoding(basename);
        has_entry = 1;
    } else if (pdf_is_dict(encobj)) {
        pdf_obj *be = pdf_dict_get(doc, encobj, "BaseEncoding");
        has_entry = 1;
        if (pdf_is_name(be, NULL)) {
            basename = pdf_to_name(be);
            base = pdf_base_encoding(basename);
        }
    }

    if (!base) {
        if (f->std14 >= 0 && !embedded)
            base = pdf_std14_builtin_enc(f->std14);
        else if (f->ftype == FT_TRUETYPE || f->ftype == FT_TRUETYPE_OT)
            base = pdf_base_encoding("WinAnsiEncoding");
        else
            base = pdf_base_encoding("StandardEncoding");
    }

    /* The column label.  We cannot read embedded font programs, so the one
     * case we infer rather than observe is "Builtin": poppler prints it when
     * the base encoding came out of the font file, which for an embedded
     * Type1-family font with no /Encoding entry is exactly what happens. */
    if (embedded && !has_entry &&
        (f->ftype == FT_TYPE1 || f->ftype == FT_TYPE1C || f->ftype == FT_TYPE1C_OT))
        str_copy(f->encstr, sizeof f->encstr, "Builtin");
    else if (base == pdf_base_encoding("WinAnsiEncoding"))
        str_copy(f->encstr, sizeof f->encstr, "WinAnsi");
    else if (base == pdf_base_encoding("MacRomanEncoding"))
        str_copy(f->encstr, sizeof f->encstr, "MacRoman");
    else if (base == pdf_base_encoding("MacExpertEncoding"))
        str_copy(f->encstr, sizeof f->encstr, "MacExpert");
    else if (base == pdf_base_encoding("Symbol"))
        str_copy(f->encstr, sizeof f->encstr, "Symbol");
    else if (base == pdf_base_encoding("ZapfDingbats"))
        str_copy(f->encstr, sizeof f->encstr, "ZapfDingbats");
    else
        str_copy(f->encstr, sizeof f->encstr, "Standard");

    for (i = 0; i < 256; i++)
        f->enc[i] = (base && base[i] && base[i][0]) ? base[i] : NULL;

    /* /Differences overrides the label and patches individual codes. */
    if (pdf_is_dict(encobj)) {
        pdf_obj *diff = pdf_dict_get(doc, encobj, "Differences");
        if (pdf_is_array(diff)) {
            size_t k, n = pdf_array_len(diff);
            long long code = 0;
            str_copy(f->encstr, sizeof f->encstr, "Custom");
            for (k = 0; k < n; k++) {
                pdf_obj *it = pdf_array_get(doc, diff, k);
                if (pdf_is_int(it)) {
                    code = pdf_to_int(it);
                } else if (pdf_is_name(it, NULL)) {
                    if (code >= 0 && code < 256)
                        f->enc[code] = pdf_to_name(it);
                    code++;
                }
            }
        }
    }
}

/* --------------------------------------------------------- simple widths */

static void build_simple_widths(pdf_doc *doc, pdf_font *f, pdf_obj *fontdict,
                                const descinfo *di)
{
    pdf_obj *warr = pdf_dict_get(doc, fontdict, "Widths");
    int first = (int)pdf_to_int(pdf_dict_get(doc, fontdict, "FirstChar"));
    int i;

    f->missing_w = di->missing_w;

    if (pdf_is_array(warr) && pdf_array_len(warr) > 0) {
        size_t k, n = pdf_array_len(warr);
        for (k = 0; k < n; k++) {
            pdf_obj *wv = pdf_array_get(doc, warr, k);
            long long c = first + (long long)k;
            if (c < 0 || c > 255 || !pdf_is_num(wv)) continue;
            f->w[c] = pdf_to_real(wv);
            f->has_w[c] = 1;
        }
        return;
    }

    /* No /Widths: fall back to the standard-14 metrics for this face, keyed by
     * the glyph name so /Differences stays honoured (ISO 32000-1 9.6.2.2). */
    f->subst = (f->std14 >= 0) ? f->std14
                               : substitute_index(f->basefont, di->flags);
    for (i = 0; i < 256; i++) {
        int wv = f->enc[i] ? pdf_std14_width_name(f->subst, f->enc[i]) : -1;
        if (wv >= 0) {
            f->w[i] = wv;
            f->has_w[i] = 1;
        }
    }
}

/* ------------------------------------------------------------ Type0 /W */

static void add_wrange(pdf_font *f, unsigned int lo, unsigned int hi, double w)
{
    if (hi < lo) { unsigned int t = lo; lo = hi; hi = t; }
    if (f->nwr + 1 > f->wrcap) {
        void *p = grow(f->doc, f->wr, sizeof(wrange), &f->wrcap, f->nwr + 1);
        if (!p) return;
        f->wr = p;
    }
    f->wr[f->nwr].lo = lo;
    f->wr[f->nwr].hi = hi;
    f->wr[f->nwr].w = w;
    f->nwr++;
}

static int wr_cmp(const void *a, const void *b)
{
    const wrange *x = a, *y = b;
    if (x->lo < y->lo) return -1;
    if (x->lo > y->lo) return 1;
    return 0;
}

/* /W is a sequence of "c [w1 w2 ...]" and "cFirst cLast w" (9.7.4.3). */
static void parse_w(pdf_doc *doc, pdf_font *f, pdf_obj *warr)
{
    size_t i = 0, n;
    if (!pdf_is_array(warr)) return;
    n = pdf_array_len(warr);
    while (i < n) {
        pdf_obj *a = pdf_array_get(doc, warr, i);
        pdf_obj *b = (i + 1 < n) ? pdf_array_get(doc, warr, i + 1) : NULL;
        if (!pdf_is_num(a)) { i++; continue; }

        if (b && pdf_is_array(b)) {
            unsigned int c = (unsigned int)pdf_to_int(a);
            size_t k, m = pdf_array_len(b);
            for (k = 0; k < m; k++) {
                pdf_obj *wv = pdf_array_get(doc, b, k);
                if (pdf_is_num(wv))
                    add_wrange(f, c + (unsigned int)k, c + (unsigned int)k,
                               pdf_to_real(wv));
            }
            i += 2;
            continue;
        }
        if (b && pdf_is_num(b) && i + 2 < n) {
            pdf_obj *wv = pdf_array_get(doc, warr, i + 2);
            if (pdf_is_num(wv)) {
                add_wrange(f, (unsigned int)pdf_to_int(a),
                           (unsigned int)pdf_to_int(b), pdf_to_real(wv));
                i += 3;
                continue;
            }
        }
        i++;
    }
    if (f->nwr > 1) qsort(f->wr, f->nwr, sizeof(wrange), wr_cmp);
}

static double cid_width(const pdf_font *f, unsigned int cid)
{
    size_t lo = 0, hi = f->nwr, mid;
    int steps;

    if (f->nwr == 0) return f->dw;
    while (lo < hi) {                   /* rightmost range with lo <= cid */
        mid = lo + (hi - lo) / 2;
        if (f->wr[mid].lo <= cid) lo = mid + 1; else hi = mid;
    }
    for (steps = 0; lo > 0 && steps < 64; steps++) {
        const wrange *r = &f->wr[--lo];
        if (cid >= r->lo && cid <= r->hi) return r->w;
        if (r->lo + 0xFFFF < cid) break;
    }
    return f->dw;
}

/* ------------------------------------------------------------- /ToUnicode */

static pdf_cmap *load_cmap_stream(pdf_doc *doc, pdf_obj *stm, int depth)
{
    pdf_buf b;
    pdf_cmap *cm;

    if (!pdf_is_stream(stm) || depth > 4) return NULL;
    pdf_buf_init(&b);
    if (pdf_stream_data(doc, stm, &b, 1) != 0 || b.n == 0) {
        pdf_buf_free(&b);
        return NULL;
    }
    cm = pdf_cmap_parse(doc, b.p, b.n);
    pdf_buf_free(&b);
    if (!cm) return NULL;

    /* A CMap stream may name its parent in the stream dictionary. */
    {
        pdf_obj *use = pdf_dict_get(doc, stm, "UseCMap");
        if (pdf_is_stream(use)) {
            pdf_cmap *p = load_cmap_stream(doc, use, depth + 1);
            if (p) pdf_cmap_set_parent(cm, p);
        } else if (pdf_is_name(use, NULL)) {
            pdf_cmap *p = pdf_cmap_predefined(doc, pdf_to_name(use));
            if (p) pdf_cmap_set_parent(cm, p);
        }
    }
    return cm;
}

/* ------------------------------------------------------------- the loader */

pdf_font *pdf_font_load(pdf_doc *doc, pdf_obj *fontdict)
{
    pdf_font *f;
    pdf_obj *desc = NULL, *tou, *sub;
    const char *subtype, *cidsubtype = "";
    descinfo di;

    if (!pdf_is_dict(fontdict)) return NULL;
    f = pdf_calloc(doc, 1, sizeof *f);
    if (!f) return NULL;

    f->doc = doc;
    f->dict = fontdict;
    f->std14 = -1;
    f->subst = -1;
    f->dw = 1000.0;
    f->matrix[0] = 0.001; f->matrix[3] = 0.001;

    subtype = pdf_to_name(pdf_dict_get(doc, fontdict, "Subtype"));
    str_copy(f->basefont, sizeof f->basefont,
             pdf_to_name(pdf_dict_get(doc, fontdict, "BaseFont")));

    f->is_type0 = !strcmp(subtype, "Type0");
    f->is_type3 = !strcmp(subtype, "Type3");

    if (f->is_type0) {
        pdf_obj *df = pdf_dict_get(doc, fontdict, "DescendantFonts");
        sub = pdf_array_get(doc, df, 0);
        if (!pdf_is_dict(sub)) sub = pdf_dict_get(doc, fontdict, "DescendantFonts");
        if (pdf_is_dict(sub)) {
            cidsubtype = pdf_to_name(pdf_dict_get(doc, sub, "Subtype"));
            desc = pdf_dict_get(doc, sub, "FontDescriptor");
        }
    } else {
        sub = NULL;
        desc = pdf_dict_get(doc, fontdict, "FontDescriptor");
    }

    read_desc(doc, desc, &di);
    f->ftype = type_of(subtype, cidsubtype, &di);
    f->symbolic = (di.flags & FLAG_SYMBOLIC) != 0;
    /* A Type 3 font carries its glyphs in /CharProcs, so it is always
     * embedded; poppler reports it that way. */
    f->emb = di.embedded || f->is_type3;
    f->sub = pdf_has_subset_tag(f->basefont);
    str_copy(f->typestr, sizeof f->typestr, g_typename[f->ftype]);

    if (!f->is_type0)
        f->std14 = pdf_std14_index(f->basefont);

    if (f->is_type0 && pdf_is_dict(sub)) {
        /* ---- composite font (9.7) ---- */
        pdf_obj *encobj = pdf_dict_get(doc, fontdict, "Encoding");
        pdf_obj *c2g;

        int named = 0;
        if (pdf_is_name(encobj, NULL)) {
            f->encmap = pdf_cmap_predefined(doc, pdf_to_name(encobj));
            named = 1;
        } else if (pdf_is_stream(encobj)) {
            f->encmap = load_cmap_stream(doc, encobj, 0);
        }
        if (!f->encmap) f->encmap = pdf_cmap_identity(doc, 0);

        /* Only a /Encoding given as a name shows that name; an embedded CMap
         * stream reads as "Custom" even when it defines a /CMapName. */
        str_copy(f->encstr, sizeof f->encstr,
                 (named && pdf_cmap_name(f->encmap)) ? pdf_cmap_name(f->encmap)
                                                     : "Custom");
        f->vertical = pdf_cmap_vertical(f->encmap);

        if (pdf_is_num(pdf_dict_get(doc, sub, "DW")))
            f->dw = pdf_to_real(pdf_dict_get(doc, sub, "DW"));
        parse_w(doc, f, pdf_dict_get(doc, sub, "W"));

        /* The character collection decides how a CID maps to Unicode when the
         * font carries no /ToUnicode (poppler's cidToUnicode tables). */
        {
            pdf_obj *csi = pdf_dict_get(doc, sub, "CIDSystemInfo");
            pdf_obj *reg = pdf_dict_get(doc, csi, "Registry");
            pdf_obj *ord = pdf_dict_get(doc, csi, "Ordering");
            size_t rn = 0, on = 0;
            const char *rs = pdf_is_string(reg) ? pdf_to_string(reg, &rn) : NULL;
            const char *os = pdf_is_string(ord) ? pdf_to_string(ord, &on) : NULL;

            f->aj1 = (rs && os && rn == 5 && memcmp(rs, "Adobe", 5) == 0 &&
                      on == 6 && memcmp(os, "Japan1", 6) == 0);
        }

        c2g = pdf_dict_get(doc, sub, "CIDToGIDMap");
        if (pdf_is_stream(c2g))      { f->cid2gid = c2g; f->cid_is_gid = 0; }
        else                         { f->cid_is_gid = 1; }
    } else if (f->is_type3) {
        /* ---- Type 3 (9.6.5) ---- */
        pdf_obj *m = pdf_dict_get(doc, fontdict, "FontMatrix");
        if (pdf_is_array(m) && pdf_array_len(m) >= 6) {
            int i;
            for (i = 0; i < 6; i++) {
                pdf_obj *v = pdf_array_get(doc, m, (size_t)i);
                f->matrix[i] = pdf_is_num(v) ? pdf_to_real(v) : 0.0;
            }
        }
        f->charprocs = pdf_dict_get(doc, fontdict, "CharProcs");
        f->t3res = pdf_dict_get(doc, fontdict, "Resources");
        build_simple_enc(doc, f, pdf_dict_get(doc, fontdict, "Encoding"), di.embedded);
        /* A Type 3 font's /Widths are in glyph space; never fall back to the
         * standard-14 tables, which are in 1/1000 text space. */
        {
            pdf_obj *warr = pdf_dict_get(doc, fontdict, "Widths");
            int first = (int)pdf_to_int(pdf_dict_get(doc, fontdict, "FirstChar"));
            size_t k, n = pdf_is_array(warr) ? pdf_array_len(warr) : 0;
            for (k = 0; k < n; k++) {
                pdf_obj *wv = pdf_array_get(doc, warr, k);
                long long c = first + (long long)k;
                if (c < 0 || c > 255 || !pdf_is_num(wv)) continue;
                f->w[c] = pdf_to_real(wv);
                f->has_w[c] = 1;
            }
        }
    } else {
        /* ---- simple font (9.6) ---- */
        build_simple_enc(doc, f, pdf_dict_get(doc, fontdict, "Encoding"), di.embedded);
        build_simple_widths(doc, f, fontdict, &di);
    }

    /* /ToUnicode overrides everything for extraction (9.10.3). */
    tou = pdf_dict_get(doc, fontdict, "ToUnicode");
    if (pdf_is_stream(tou)) {
        f->tounicode = load_cmap_stream(doc, tou, 0);
        if (f->tounicode && pdf_cmap_has_unicode(f->tounicode)) f->uni = 1;
        else f->tounicode = NULL;
    }
    return f;
}

/* ------------------------------------------------------- glyph ligatures */

/* poppler's parseCharName() splits the five Latin f-ligature glyph names into
 * their component letters before it consults the Adobe Glyph List, so text
 * extraction yields "fi" and not the precomposed U+FB01.  The rule is the
 * glyph name's, not Unicode's: a /ToUnicode CMap that names U+FB01 outright
 * still extracts as U+FB01. */
static int ligature_unicode(const char *glyph, unsigned int *out, int max)
{
    static const char *const lig[5] = { "ff", "ffi", "ffl", "fi", "fl" };
    size_t k;
    int i, n;

    if (!glyph) return 0;
    for (k = 0; k < sizeof lig / sizeof lig[0]; k++) {
        if (strcmp(glyph, lig[k]) != 0) continue;
        n = (int)strlen(lig[k]);
        if (n > max) return 0;
        for (i = 0; i < n; i++) out[i] = (unsigned char)lig[k][i];
        return n;
    }
    return 0;
}

/* ------------------------------------------------ CID to Unicode by collection */

/* Adobe-Japan1 CID -> Unicode, CIDs 0..391 (the Latin, symbol and
 * half-width katakana blocks reachable through one-byte codes). */
#define AJ1_UNI_MAX 392
static const unsigned short aj1_uni[AJ1_UNI_MAX] = {
    0x0000, 0x00a0, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a,
    0x002b, 0x002c, 0x002d, 0x002e, 0x002f, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036,
    0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f, 0x0040, 0x0041, 0x0042,
    0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e,
    0x004f, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a,
    0x005b, 0x00a5, 0x005d, 0x005e, 0x0332, 0x0300, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066,
    0x0067, 0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 0x0070, 0x0071, 0x0072,
    0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007a, 0x007b, 0x00a6, 0x007d, 0x02dc,
    0x02bc, 0x005c, 0x02bb, 0x007c, 0x007e, 0x00a1, 0x00a2, 0x00a3, 0x2044, 0x0192, 0x0000, 0x00a4,
    0x201c, 0x00ab, 0x2039, 0x203a, 0xfb01, 0xfb02, 0x2012, 0x0000, 0x0000, 0x00b7, 0x0000, 0x2022,
    0x201a, 0x201e, 0x201d, 0x00bb, 0x0000, 0x0000, 0x00bf, 0x0301, 0x02c6, 0x00af, 0x0306, 0x0307,
    0x0308, 0x02da, 0x00b8, 0x030b, 0x0328, 0x030c, 0x0336, 0x00c6, 0x00aa, 0x0141, 0x00d8, 0x0152,
    0x00ba, 0x00e6, 0x0131, 0x0142, 0x00f8, 0x0153, 0x00df, 0x00ad, 0x00a9, 0x00ac, 0x00ae, 0x0000,
    0x0000, 0x00b2, 0x00b3, 0x00b5, 0x00b9, 0x00bc, 0x00bd, 0x00be, 0x00c0, 0x00c1, 0x00c2, 0x00c3,
    0x00c4, 0x00c5, 0x00c7, 0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf, 0x00d0,
    0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x0000, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd,
    0x00de, 0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e7, 0x00e8, 0x00e9, 0x00ea, 0x00eb,
    0x00ec, 0x00ed, 0x00ee, 0x00ef, 0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x0000,
    0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff, 0x0160, 0x0178, 0x017d, 0x0305, 0x0161,
    0x2122, 0x017e, 0x0000, 0x00a0, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028,
    0x0029, 0x002a, 0x002b, 0x002c, 0x2011, 0x002e, 0x002f, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034,
    0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f, 0x0040,
    0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 0x004c,
    0x004d, 0x004e, 0x004f, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058,
    0x0059, 0x005a, 0x005b, 0x00a5, 0x005d, 0x005e, 0x0332, 0x0300, 0x0061, 0x0062, 0x0063, 0x0064,
    0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 0x0070,
    0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007a, 0x007b, 0xffe8,
    0x007d, 0x0000, 0x0000, 0xff61, 0xff62, 0xff63, 0xff64, 0xff65, 0xff66, 0xff67, 0xff68, 0xff69,
    0xff6a, 0xff6b, 0xff6c, 0xff6d, 0xff6e, 0xff6f, 0xff70, 0xff71, 0xff72, 0xff73, 0xff74, 0xff75,
    0xff76, 0xff77, 0xff78, 0xff79, 0xff7a, 0xff7b, 0xff7c, 0xff7d, 0xff7e, 0xff7f, 0xff80, 0xff81,
    0xff82, 0xff83, 0xff84, 0xff85, 0xff86, 0xff87, 0xff88, 0xff89, 0xff8a, 0xff8b, 0xff8c, 0xff8d,
    0xff8e, 0xff8f, 0xff90, 0xff91, 0xff92, 0xff93, 0xff94, 0xff95, 0xff96, 0xff97, 0xff98, 0xff99,
    0xff9a, 0xff9b, 0xff9c, 0xff9d, 0xff9e, 0xff9f, 0x0000, 0x0000,
};

/* -------------------------------------------------------------- accessors */

int pdf_font_is_type3(const pdf_font *f) { return f && f->is_type3; }
int pdf_font_vertical(const pdf_font *f) { return f && f->vertical; }
const double *pdf_font_matrix(const pdf_font *f) { return f ? f->matrix : NULL; }
pdf_obj *pdf_font_t3_resources(const pdf_font *f) { return f ? f->t3res : NULL; }

int pdf_font_next_code(const pdf_font *f, const unsigned char *s, size_t n,
                       unsigned int *code, int *nbytes)
{
    if (!s || n == 0) return -1;
    if (f && f->is_type0 && f->encmap)
        return pdf_cmap_next(f->encmap, s, n, code, nbytes);
    *code = s[0];
    *nbytes = 1;
    return 0;
}

/* Returns w0 in text-space units, so the caller can use it directly in
 * tx = (w0 * Tfs + Tc + Tw) * Th  (ISO 32000-1 9.4.4). */
double pdf_font_width(const pdf_font *f, unsigned int code)
{
    if (!f) return 0.0;

    if (f->is_type0) {
        int cid = f->encmap ? pdf_cmap_cid(f->encmap, code) : (int)code;
        if (cid < 0) cid = (int)code;
        return cid_width(f, (unsigned int)cid) / 1000.0;
    }
    if (f->is_type3) {
        double w = (code < 256 && f->has_w[code]) ? f->w[code] : 0.0;
        /* Glyph space -> text space through /FontMatrix. */
        return w * f->matrix[0];
    }
    if (code < 256 && f->has_w[code]) return f->w[code] / 1000.0;
    return f->missing_w / 1000.0;
}

int pdf_font_unicode(const pdf_font *f, unsigned int code,
                     unsigned int *out, int max)
{
    if (!f || !out || max <= 0) return 0;

    /* 1. /ToUnicode wins, and for a composite font it is indexed by the code
     *    rather than by the CID (poppler GfxCIDFont::getNextChar). */
    if (f->tounicode) {
        int n = pdf_cmap_unicode(f->tounicode, code, out, max);
        if (n > 0) return n;
    }

    /* 2. Simple fonts: through the encoding's glyph name. */
    if (!f->is_type0) {
        if (code < 256 && f->enc[code]) {
            int n = ligature_unicode(f->enc[code], out, max);
            if (n > 0) return n;
            n = pdf_glyph_to_unicode(f->enc[code], out, max);
            if (n > 0) return n;
        }
        /* 3. Last resort: treat the code as Latin-1, which is what the
         *    standard encodings do for the ASCII range anyway. */
        if (code >= 0x20 && code < 0x7f) { out[0] = code; return 1; }
        return 0;
    }

    /* 4. A CIDFont without /ToUnicode goes through its character collection.
     *    Only the Adobe-Japan1 Latin, symbol and half-width katakana blocks --
     *    everything a one-byte code in a RKSJ-style CMap can reach -- are
     *    built in; other collections still extract nothing. */
    if (f->aj1) {
        int cid = f->encmap ? pdf_cmap_cid(f->encmap, code) : (int)code;

        if (cid < 0) cid = (int)code;
        if (cid >= 0 && cid < AJ1_UNI_MAX && aj1_uni[cid] != 0) {
            out[0] = aj1_uni[cid];
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------- font cache */

struct pdf_fontcache {
    pdf_doc *doc;
    struct { pdf_obj *dict; pdf_font *font; } *e;
    size_t n, cap;
};

pdf_fontcache *pdf_fontcache_new(pdf_doc *doc)
{
    pdf_fontcache *fc = pdf_calloc(doc, 1, sizeof *fc);
    if (fc) fc->doc = doc;
    return fc;
}

pdf_font *pdf_fontcache_get(pdf_fontcache *fc, pdf_obj *fontdict)
{
    size_t i;
    pdf_font *f;

    if (!fc || !pdf_is_dict(fontdict)) return NULL;
    for (i = 0; i < fc->n; i++)
        if (fc->e[i].dict == fontdict) return fc->e[i].font;

    f = pdf_font_load(fc->doc, fontdict);
    if (fc->n + 1 > fc->cap) {
        void *p = grow(fc->doc, fc->e, sizeof *fc->e, &fc->cap, fc->n + 1);
        if (!p) return f;
        fc->e = p;
    }
    fc->e[fc->n].dict = fontdict;
    fc->e[fc->n].font = f;
    fc->n++;
    return f;
}

/* --------------------------------------------------------- pdf_list_fonts */

void pdf_font_info(pdf_doc *doc, pdf_obj *fontdict, int num, int gen,
                   pdf_fontinfo *fi)
{
    pdf_font *f;

    memset(fi, 0, sizeof *fi);
    fi->objnum = num;
    fi->objgen = gen;

    f = pdf_font_load(doc, fontdict);
    if (!f) {
        str_copy(fi->name, sizeof fi->name, "[none]");
        str_copy(fi->type, sizeof fi->type, g_typename[FT_UNKNOWN]);
        return;
    }
    str_copy(fi->name, sizeof fi->name,
             f->basefont[0] ? f->basefont : "[none]");
    str_copy(fi->type, sizeof fi->type, f->typestr);
    str_copy(fi->encoding, sizeof fi->encoding, f->encstr);
    fi->emb = f->emb;
    fi->sub = f->sub;
    fi->uni = f->uni;
}

typedef struct {
    pdf_doc      *doc;
    pdf_fontinfo *v;
    size_t        n, cap;

    int      *emitted;      /* font object numbers already listed */
    size_t    nem, emcap;
    int      *visited;      /* object numbers already walked */
    size_t    nvi, vicap;
    pdf_obj **direct;       /* font dicts written inline, deduped by identity */
    size_t    ndi, dicap;
} scanner;

/* Returns 1 when num was already present. */
static int num_seen(int **arr, size_t *n, size_t *cap, int num)
{
    size_t i;
    for (i = 0; i < *n; i++)
        if ((*arr)[i] == num) return 1;
    if (*n + 1 > *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        int *p = realloc(*arr, nc * sizeof *p);
        if (!p) return 1;
        *arr = p;
        *cap = nc;
    }
    (*arr)[(*n)++] = num;
    return 0;
}

static int ptr_seen(pdf_obj ***arr, size_t *n, size_t *cap, pdf_obj *o)
{
    size_t i;
    for (i = 0; i < *n; i++)
        if ((*arr)[i] == o) return 1;
    if (*n + 1 > *cap) {
        size_t nc = *cap ? *cap * 2 : 32;
        pdf_obj **p = realloc(*arr, nc * sizeof *p);
        if (!p) return 1;
        *arr = p;
        *cap = nc;
    }
    (*arr)[(*n)++] = o;
    return 0;
}

static void emit_font(scanner *sc, pdf_obj *raw, pdf_obj *fd)
{
    int num = 0, gen = 0;

    if (raw && raw->kind == PDF_REF) {
        num = raw->u.ref.num;
        gen = raw->u.ref.gen;
        /* Poppler keys its "already seen" set on the object number alone. */
        if (num_seen(&sc->emitted, &sc->nem, &sc->emcap, num)) return;
    } else {
        if (ptr_seen(&sc->direct, &sc->ndi, &sc->dicap, fd)) return;
    }

    if (sc->n + 1 > sc->cap) {
        size_t nc = sc->cap ? sc->cap * 2 : 16;
        pdf_fontinfo *p = realloc(sc->v, nc * sizeof *p);
        if (!p) return;
        sc->v = p;
        sc->cap = nc;
    }
    pdf_font_info(sc->doc, fd, num, gen, &sc->v[sc->n]);
    sc->n++;
}

static void scan_res(scanner *sc, pdf_obj *res, int depth)
{
    pdf_doc *doc = sc->doc;
    static const char *const restypes[2] = { "XObject", "Pattern" };
    pdf_obj *fonts;
    size_t i, n;
    int r;

    if (depth > 16 || !pdf_is_dict(res)) return;

    fonts = pdf_dict_get(doc, res, "Font");
    if (pdf_is_dict(fonts)) {
        n = pdf_dict_len(fonts);
        for (i = 0; i < n; i++) {
            pdf_obj *raw = pdf_dict_val_raw(fonts, i);
            pdf_obj *fd = pdf_resolve(doc, raw);
            if (pdf_is_dict(fd)) emit_font(sc, raw, fd);
        }
    }

    /* pdffonts recurses through every stream that carries its own resource
     * dictionary, under both /XObject and /Pattern. */
    for (r = 0; r < 2; r++) {
        pdf_obj *cat = pdf_dict_get(doc, res, restypes[r]);
        if (!pdf_is_dict(cat)) continue;
        n = pdf_dict_len(cat);
        for (i = 0; i < n; i++) {
            pdf_obj *raw = pdf_dict_val_raw(cat, i);
            pdf_obj *xo, *rawres, *xres;

            if (raw && raw->kind == PDF_REF &&
                num_seen(&sc->visited, &sc->nvi, &sc->vicap, raw->u.ref.num))
                continue;

            xo = pdf_resolve(doc, raw);
            if (!pdf_is_stream(xo)) continue;

            rawres = pdf_dict_get_raw(xo, "Resources");
            if (rawres && rawres->kind == PDF_REF &&
                num_seen(&sc->visited, &sc->nvi, &sc->vicap, rawres->u.ref.num))
                continue;

            xres = pdf_resolve(doc, rawres);
            if (pdf_is_dict(xres) && xres != res)
                scan_res(sc, xres, depth + 1);
        }
    }
}

/* The appearance stream poppler would actually use: /AP /N, narrowed by /AS
 * when that entry is a sub-dictionary of states. */
static pdf_obj *annot_appearance(pdf_doc *doc, pdf_obj *annot)
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

int pdf_list_fonts(pdf_doc *doc, int first, int last,
                   pdf_fontinfo **out, size_t *nout)
{
    scanner sc;
    int np, p;

    if (!out || !nout) return PDF_E_RANGE;
    *out = NULL;
    *nout = 0;
    if (!doc) return PDF_E_RANGE;

    np = pdf_page_count(doc);
    if (np <= 0) return 0;
    if (first < 1) first = 1;
    if (last < 1 || last > np) last = np;
    if (first > last) return 0;

    memset(&sc, 0, sizeof sc);
    sc.doc = doc;

    for (p = first; p <= last; p++) {
        pdf_obj *page = pdf_page_dict(doc, p - 1);
        pdf_obj *annots;

        if (!pdf_is_dict(page)) continue;

        /* The visited/emitted sets span the whole scan, exactly as poppler's
         * FontInfoScanner members do. */
        scan_res(&sc, pdf_page_inherited(doc, page, "Resources"), 0);

        annots = pdf_dict_get(doc, page, "Annots");
        if (pdf_is_array(annots)) {
            size_t i, na = pdf_array_len(annots);
            for (i = 0; i < na; i++) {
                pdf_obj *an = pdf_array_get(doc, annots, i);
                pdf_obj *app;
                if (!pdf_is_dict(an)) continue;
                app = annot_appearance(doc, an);
                if (app) scan_res(&sc, pdf_dict_get(doc, app, "Resources"), 0);
            }
        }
    }

    free(sc.emitted);
    free(sc.visited);
    free(sc.direct);
    *out = sc.v;
    *nout = sc.n;
    return 0;
}
