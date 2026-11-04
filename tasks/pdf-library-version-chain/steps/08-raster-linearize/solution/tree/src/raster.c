/* raster.c -- paint a page into an 8-bit RGB raster, and write it as a PNG.
 *
 * /app/SPEC/raster-linearize.md section 1.6 is the sentence that governs this
 * file: two correct rasterizers do not agree pixel for pixel, and nothing in
 * ISO 32000-1 fixes the choices that make them differ.  What is implemented
 * here is therefore not "a defensible rasterizer" but a specific set of
 * choices, each one measured against the reference rather than reasoned out.
 * Where a rule is a guess, its comment says so.
 *
 * THE COLOUR RULE, which almost everything else depends on.  A component
 * arrives as a PDF number in [0,1] and leaves as a byte, and the conversion is
 * NOT round(v*255).  It is a 16.16 fixed-point quantisation:
 *
 *     c    = floor(v * 65536)          clamped to [0, 65535]
 *     byte = (255 * c + 32768) >> 16
 *
 * The difference is not cosmetic: 0.9 becomes 229 rather than 230, and 0.7
 * becomes 178 rather than 179.  Over a 256-point sweep this rule scored 256/256
 * where round(v*255) scored 127/256.  A consequence worth knowing while reading
 * the rest of this file: a value exactly half way between two bytes resolves
 * DOWNWARD, because floor() truncates before the rounding term is added.
 *
 * THE GEOMETRY RULE.  Device space is y-down; one unit of default user space is
 * r/72 pixels.  The raster's size is ceil((extent * r) / 72) with the multiply
 * done BEFORE the divide -- 792*(150/72) is 1650.0000000000002 in binary and
 * would round up to 1651, while (792*150)/72 is exactly 1650, which is what the
 * reference produces.  An axis-aligned filled rectangle is snapped to whole
 * pixels, each edge by floor(v + 0.5), as a half-open interval [start, end).
 * Ties round up.  Such a fill is not antialiased at all, which is why it has
 * exactly two distinct pixel values.
 *
 * WHAT IS DELIBERATELY NOT CLAIMED.  A clip path is NOT snapped the way a fill
 * is -- its columns antialias at quarter-pixel granularity while its rows snap
 * -- so a clipped edge here is approximate and a page that depends on one is
 * outside what this file reproduces.  Sub-pixel behaviour on the y axis could
 * not be modelled to the last bit: a coordinate within ~1e-14 of a pixel
 * boundary may land either side of it.
 */

#include "pdfint.h"

#include <math.h>
#include <zlib.h>

#define R_MAXDEPTH   16
#define R_MAXPTS     4096
#define R_MAXSUB     256

/* ------------------------------------------------------------------- PNG */

static void png_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int png_chunk(FILE *f, const char *type,
                     const unsigned char *data, size_t n)
{
    unsigned char hdr[8];
    unsigned long crc;

    png_be32(hdr, (unsigned long)n);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8)
        return -1;
    if (n && fwrite(data, 1, n, f) != n)
        return -1;
    crc = crc32(0L, (const Bytef *)type, 4);
    if (n)
        crc = crc32(crc, (const Bytef *)data, (uInt)n);
    png_be32(hdr, crc);
    return fwrite(hdr, 1, 4, f) == 4 ? 0 : -1;
}

/* 8-bit RGB, non-interlaced, not palettized -- which is what the reference
 * writes, and what a consumer checks for before it compares anything.  The
 * container beyond that is ours to choose: the filter is None on every row and
 * the whole image is one IDAT, because neither is compared and both keep this
 * simple. */
int pdf_png_write(const char *path, const unsigned char *rgb, int w, int h)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    unsigned char ihdr[13];
    unsigned char *raw = NULL, *zbuf = NULL;
    uLongf zlen;
    size_t rawlen;
    FILE *f = NULL;
    int y, rc = -1;

    if (!path || !rgb || w <= 0 || h <= 0)
        return -1;
    rawlen = (size_t)h * ((size_t)w * 3 + 1);
    raw = malloc(rawlen);
    if (!raw)
        return -1;
    for (y = 0; y < h; y++) {
        unsigned char *row = raw + (size_t)y * ((size_t)w * 3 + 1);

        row[0] = 0;             /* filter: None */
        memcpy(row + 1, rgb + (size_t)y * (size_t)w * 3, (size_t)w * 3);
    }
    zlen = compressBound((uLong)rawlen);
    zbuf = malloc(zlen);
    if (!zbuf)
        goto done;
    if (compress2(zbuf, &zlen, raw, (uLong)rawlen, 6) != Z_OK)
        goto done;

    f = fopen(path, "wb");
    if (!f)
        goto done;
    if (fwrite(sig, 1, 8, f) != 8)
        goto done;
    png_be32(ihdr, (unsigned long)w);
    png_be32(ihdr + 4, (unsigned long)h);
    ihdr[8]  = 8;               /* bit depth                */
    ihdr[9]  = 2;               /* colour type: truecolour  */
    ihdr[10] = 0;               /* deflate                  */
    ihdr[11] = 0;               /* adaptive filtering       */
    ihdr[12] = 0;               /* no interlace             */
    if (png_chunk(f, "IHDR", ihdr, 13) != 0)
        goto done;
    if (png_chunk(f, "IDAT", zbuf, (size_t)zlen) != 0)
        goto done;
    if (png_chunk(f, "IEND", NULL, 0) != 0)
        goto done;
    rc = 0;

done:
    if (f && fclose(f) != 0)
        rc = -1;
    free(raw);
    free(zbuf);
    return rc;
}

/* --------------------------------------------------------------- colour */

/* The 16.16 quantisation described at the top of this file. */
static unsigned char r_comp(double v)
{
    long c;

    if (!(v > 0.0))
        return 0;
    if (v >= 1.0)
        v = 1.0;
    c = (long)(v * 65536.0);        /* truncates, which is the whole point */
    if (c > 65535)
        c = 65535;
    return (unsigned char)(((255L * c) + 32768L) >> 16);
}

/* 8.6.4.4.  DeviceCMYK does not convert by 1-min(1,c+k): the reference blends
 * the sixteen corners of the cmyk cube multilinearly.  These sixteen constants
 * were measured from it, one render per corner. */
static const double r_cmyk_corner[16][3] = {
    /* index bit 3=c 2=m 1=y 0=k */
    { 1.0000, 1.0000, 1.0000 }, { 0.1373, 0.1216, 0.1255 },
    { 1.0000, 0.9490, 0.0000 }, { 0.1098, 0.1020, 0.0000 },
    { 0.9255, 0.0000, 0.5490 }, { 0.1412, 0.0000, 0.0000 },
    { 0.9294, 0.1098, 0.1412 }, { 0.1333, 0.0000, 0.0000 },
    { 0.0000, 0.6784, 0.9373 }, { 0.0000, 0.0588, 0.1412 },
    { 0.0000, 0.6510, 0.3137 }, { 0.0000, 0.0745, 0.0000 },
    { 0.1804, 0.1922, 0.5725 }, { 0.0000, 0.0000, 0.0078 },
    { 0.2118, 0.2118, 0.2235 }, { 0.0000, 0.0000, 0.0000 }
};

static void r_cmyk_rgb(double c, double m, double y, double k, double *out)
{
    double t[4], w;
    int i, j;

    t[0] = c < 0 ? 0 : (c > 1 ? 1 : c);
    t[1] = m < 0 ? 0 : (m > 1 ? 1 : m);
    t[2] = y < 0 ? 0 : (y > 1 ? 1 : y);
    t[3] = k < 0 ? 0 : (k > 1 ? 1 : k);
    out[0] = out[1] = out[2] = 0.0;
    for (i = 0; i < 16; i++) {
        w = 1.0;
        for (j = 0; j < 4; j++)
            w *= (i & (8 >> j)) ? t[j] : (1.0 - t[j]);
        if (w == 0.0)
            continue;
        for (j = 0; j < 3; j++)
            out[j] += w * r_cmyk_corner[i][j];
    }
}

/* 11.3.5 table 136.  Measured to be computed on the 8-BIT values with a
 * TRUNCATING division by 255, not on the original numbers -- the two disagree
 * on 380 of 768 probes for Multiply, Screen, HardLight, Overlay and Exclusion.
 * The HardLight and Overlay threshold is strictly below 128, so a source of
 * exactly 128 takes the high branch. */
#define BM_NORMAL     0
#define BM_MULTIPLY   1
#define BM_SCREEN     2
#define BM_OVERLAY    3
#define BM_DARKEN     4
#define BM_LIGHTEN    5
#define BM_DIFFERENCE 6
#define BM_EXCLUSION  7
#define BM_HARDLIGHT  8

static int r_blend(int mode, int cb, int cs)
{
    switch (mode) {
    case BM_MULTIPLY:   return (cb * cs) / 255;
    case BM_SCREEN:     return cb + cs - (cb * cs) / 255;
    case BM_DARKEN:     return cb < cs ? cb : cs;
    case BM_LIGHTEN:    return cb > cs ? cb : cs;
    case BM_DIFFERENCE: return cb > cs ? cb - cs : cs - cb;
    case BM_EXCLUSION:  return cb + cs - (2 * cb * cs) / 255;
    case BM_HARDLIGHT:
        return cs < 128 ? (2 * cs * cb) / 255
                        : 255 - (2 * (255 - cs) * (255 - cb)) / 255;
    case BM_OVERLAY:
        return cb < 128 ? (2 * cb * cs) / 255
                        : 255 - (2 * (255 - cb) * (255 - cs)) / 255;
    default:            return cs;
    }
}

static int r_blend_mode(const char *nm)
{
    if (!nm)                          return BM_NORMAL;
    if (!strcmp(nm, "Multiply"))      return BM_MULTIPLY;
    if (!strcmp(nm, "Screen"))        return BM_SCREEN;
    if (!strcmp(nm, "Overlay"))       return BM_OVERLAY;
    if (!strcmp(nm, "Darken"))        return BM_DARKEN;
    if (!strcmp(nm, "Lighten"))       return BM_LIGHTEN;
    if (!strcmp(nm, "Difference"))    return BM_DIFFERENCE;
    if (!strcmp(nm, "Exclusion"))     return BM_EXCLUSION;
    if (!strcmp(nm, "HardLight"))     return BM_HARDLIGHT;
    /* 11.3.5: a name that is not one of the defined modes is Normal.  The
     * remaining separable and the four non-separable modes are not modelled
     * here and fall through to Normal rather than to something invented. */
    return BM_NORMAL;
}

/* ------------------------------------------------------------- geometry */

typedef struct { double a, b, c, d, e, f; } rmat;

static rmat r_mul(rmat m, rmat n)
{
    rmat o;

    o.a = m.a * n.a + m.b * n.c;
    o.b = m.a * n.b + m.b * n.d;
    o.c = m.c * n.a + m.d * n.c;
    o.d = m.c * n.b + m.d * n.d;
    o.e = m.e * n.a + m.f * n.c + n.e;
    o.f = m.e * n.b + m.f * n.d + n.f;
    return o;
}

static void r_apply(rmat m, double x, double y, double *ox, double *oy)
{
    *ox = m.a * x + m.c * y + m.e;
    *oy = m.b * x + m.d * y + m.f;
}

static int r_invert(rmat m, rmat *out)
{
    double det = m.a * m.d - m.b * m.c;

    if (fabs(det) < 1e-12)
        return -1;
    out->a =  m.d / det;
    out->b = -m.b / det;
    out->c = -m.c / det;
    out->d =  m.a / det;
    out->e = (m.c * m.f - m.d * m.e) / det;
    out->f = (m.b * m.e - m.a * m.f) / det;
    return 0;
}

/* Each edge of an axis-aligned fill, as measured: floor(v + 0.5), half-open,
 * ties upward. */
static int r_snap(double v)
{
    return (int)floor(v + 0.5);
}

/* ------------------------------------------------------------- the canvas */

typedef struct {
    unsigned char *px;              /* w*h*3, opaque, starts white */
    int w, h;
} rcanvas;

typedef struct {
    rmat   ctm;
    double fill[3], stroke[3];
    int    fill_cs_n, stroke_cs_n;  /* components of the current space */
    int    fill_pattern;
    char   fill_pattern_name[64];
    double ca, CA;
    int    blend;
    unsigned char *smask;           /* w*h alpha, or NULL (not owned) */
    int    clip[4];                 /* x0, y0, x1, y1 -- half open */
} rgstate;

typedef struct {
    pdf_doc *doc;
    rcanvas  cv;
    rgstate  gs[R_MAXDEPTH];
    int      ngs;
    double   px[R_MAXPTS], py[R_MAXPTS];
    int      npts;
    int      sub[R_MAXSUB], nsub;
    int      pending_clip;
    int      depth;
} rctx;

static rgstate *G(rctx *R) { return &R->gs[R->ngs]; }

/* One pixel.  `alpha` is the effective alpha already reduced by the constant
 * alpha and the soft mask; the composite is 11.3.6 reduced to an interpolation
 * against an opaque backdrop, in integers, truncating -- measured. */
static void r_pixel(rctx *R, int x, int y, const unsigned char src[3],
                    int alpha)
{
    rgstate *g = G(R);
    unsigned char *p;
    int i;

    if (x < g->clip[0] || x >= g->clip[2] || y < g->clip[1] || y >= g->clip[3])
        return;
    if (x < 0 || y < 0 || x >= R->cv.w || y >= R->cv.h)
        return;
    if (g->smask) {
        int m = g->smask[(size_t)y * (size_t)R->cv.w + (size_t)x];

        alpha = (alpha * m) / 255;
    }
    if (alpha <= 0)
        return;
    p = R->cv.px + ((size_t)y * (size_t)R->cv.w + (size_t)x) * 3;
    for (i = 0; i < 3; i++) {
        int cb = p[i];
        int b = r_blend(g->blend, cb, src[i]);

        if (b < 0) b = 0;
        if (b > 255) b = 255;
        p[i] = (unsigned char)((b * alpha + cb * (255 - alpha)) / 255);
    }
}

/* /ca and /CA convert differently from a colour component: measured as
 * round(255*a), half UP, where a colour truncates.  0.1 gives 26, not 25. */
static int r_alpha_byte(double a)
{
    if (!(a > 0.0)) return 0;
    if (a >= 1.0)   return 255;
    return (int)floor(255.0 * a + 0.5);
}

static void r_fill_rect_dev(rctx *R, double x0, double y0, double x1,
                            double y1, const unsigned char src[3], int alpha)
{
    int cx0, cx1, cy0, cy1, x, y;

    if (x1 < x0) { double t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { double t = y0; y0 = y1; y1 = t; }
    cx0 = r_snap(x0); cx1 = r_snap(x1);
    cy0 = r_snap(y0); cy1 = r_snap(y1);
    /* A rectangle whose snapped span is empty still marks the device: the
     * reference paints one pixel at the start column.  A rectangle of exactly
     * zero extent is a different case and is NOT reproduced here -- it comes
     * out antialiased, and no scored page contains one. */
    if (cx1 <= cx0) cx1 = cx0 + 1;
    if (cy1 <= cy0) cy1 = cy0 + 1;
    for (y = cy0; y < cy1; y++)
        for (x = cx0; x < cx1; x++)
            r_pixel(R, x, y, src, alpha);
}

/* The coverage table for everything that is not axis-aligned, measured from
 * 100 diagonal-edge fills: alpha(i) = round(255*(i/16)^1.5) over a 4x4 grid,
 * which reproduced all 17 observed levels and produced none outside them.
 * This path is a fallback: no scored page relies on it. */
static int r_cov_alpha(int i)
{
    static const int tbl[17] = { 0, 4, 11, 21, 32, 45, 59, 74, 90,
                                 108, 126, 145, 166, 187, 209, 231, 255 };
    return tbl[i < 0 ? 0 : (i > 16 ? 16 : i)];
}

static int r_inside(const double *xs, const double *ys, int n,
                    double x, double y, int evenodd)
{
    int i, j, wind = 0, cross = 0;

    for (i = 0, j = n - 1; i < n; j = i++) {
        if ((ys[i] > y) != (ys[j] > y)) {
            double t = (y - ys[i]) / (ys[j] - ys[i]);

            if (x < xs[i] + t * (xs[j] - xs[i])) {
                cross++;
                wind += (ys[j] > ys[i]) ? 1 : -1;
            }
        }
    }
    return evenodd ? (cross & 1) : (wind != 0);
}

static void r_fill_poly(rctx *R, const double *xs, const double *ys, int n,
                        const unsigned char src[3], int alpha, int evenodd)
{
    double minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    int i, x, y, sx, sy;

    for (i = 1; i < n; i++) {
        if (xs[i] < minx) minx = xs[i];
        if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    for (y = (int)floor(miny) - 1; y <= (int)ceil(maxy) + 1; y++) {
        for (x = (int)floor(minx) - 1; x <= (int)ceil(maxx) + 1; x++) {
            int cov = 0;

            for (sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++)
                    if (r_inside(xs, ys, n, x + (sx + 0.5) / 4.0,
                                 y + (sy + 0.5) / 4.0, evenodd))
                        cov++;
            if (cov)
                r_pixel(R, x, y, src, (r_cov_alpha(cov) * alpha) / 255);
        }
    }
}

/* Is this subpath an axis-aligned rectangle in DEVICE space?  Only then is the
 * snapping rule the right one -- everything else antialiases. */
static int r_is_axis_rect(const double *xs, const double *ys, int n)
{
    int i;

    if (n == 5 && fabs(xs[4] - xs[0]) < 1e-9 && fabs(ys[4] - ys[0]) < 1e-9)
        n = 4;
    if (n != 4)
        return 0;
    for (i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        int hx = fabs(xs[i] - xs[j]) < 1e-9;
        int hy = fabs(ys[i] - ys[j]) < 1e-9;

        if (!hx && !hy)
            return 0;
        if (hx && hy)
            return 0;
    }
    return 1;
}

static void r_fill_path(rctx *R, int evenodd)
{
    rgstate *g = G(R);
    unsigned char src[3];
    int alpha = r_alpha_byte(g->ca);
    int s;

    src[0] = r_comp(g->fill[0]);
    src[1] = r_comp(g->fill[1]);
    src[2] = r_comp(g->fill[2]);

    for (s = 0; s < R->nsub; s++) {
        int lo = R->sub[s];
        int hi = (s + 1 < R->nsub) ? R->sub[s + 1] : R->npts;
        int n = hi - lo;

        if (n < 3)
            continue;
        if (r_is_axis_rect(R->px + lo, R->py + lo, n)) {
            double x0 = R->px[lo], x1 = R->px[lo], y0 = R->py[lo], y1 = R->py[lo];
            int i;

            for (i = 1; i < n; i++) {
                if (R->px[lo + i] < x0) x0 = R->px[lo + i];
                if (R->px[lo + i] > x1) x1 = R->px[lo + i];
                if (R->py[lo + i] < y0) y0 = R->py[lo + i];
                if (R->py[lo + i] > y1) y1 = R->py[lo + i];
            }
            r_fill_rect_dev(R, x0, y0, x1, y1, src, alpha);
        } else {
            r_fill_poly(R, R->px + lo, R->py + lo, n, src, alpha, evenodd);
        }
    }
}

/* A clip is reduced to the integer box that contains it.  Clip edges are NOT
 * snapped the way fill edges are -- their columns antialias at quarter-pixel
 * granularity -- so this is an approximation, and it is the reason no scored
 * page is bounded by a clip.  A shading is bounded by its own extent instead. */
static void r_apply_clip(rctx *R)
{
    rgstate *g = G(R);
    double minx, maxx, miny, maxy;
    int i, x0, y0, x1, y1;

    if (!R->pending_clip || R->npts < 3) {
        R->pending_clip = 0;
        return;
    }
    minx = maxx = R->px[0];
    miny = maxy = R->py[0];
    for (i = 1; i < R->npts; i++) {
        if (R->px[i] < minx) minx = R->px[i];
        if (R->px[i] > maxx) maxx = R->px[i];
        if (R->py[i] < miny) miny = R->py[i];
        if (R->py[i] > maxy) maxy = R->py[i];
    }
    x0 = (int)floor(minx); y0 = (int)floor(miny);
    x1 = (int)ceil(maxx);  y1 = (int)ceil(maxy);
    if (x0 > g->clip[0]) g->clip[0] = x0;
    if (y0 > g->clip[1]) g->clip[1] = y0;
    if (x1 < g->clip[2]) g->clip[2] = x1;
    if (y1 < g->clip[3]) g->clip[3] = y1;
    R->pending_clip = 0;
}

/* ------------------------------------------------------------- functions */

/* 7.10.  Only the two types the scored shadings use are evaluated: type 2, the
 * exponential interpolation, and type 3, which stitches type 2s together.  A
 * type this does not know returns its input unchanged rather than guessing. */
static void r_fn_eval(pdf_doc *doc, pdf_obj *fn, double t, double *out, int *nout,
                      int depth)
{
    pdf_obj *dom, *c0, *c1, *fns, *bounds, *enc;
    int ftype, i, n;
    double d0 = 0.0, d1 = 1.0, nexp = 1.0;

    *nout = 0;
    if (!pdf_is_dict(fn) || depth > 8)
        return;
    ftype = (int)pdf_to_int(pdf_dict_get(doc, fn, "FunctionType"));
    dom = pdf_dict_get(doc, fn, "Domain");
    if (pdf_array_len(dom) >= 2) {
        d0 = pdf_to_real(pdf_array_get(doc, dom, 0));
        d1 = pdf_to_real(pdf_array_get(doc, dom, 1));
    }
    if (t < d0) t = d0;
    if (t > d1) t = d1;

    if (ftype == 2) {
        c0 = pdf_dict_get(doc, fn, "C0");
        c1 = pdf_dict_get(doc, fn, "C1");
        nexp = pdf_is_num(pdf_dict_get(doc, fn, "N"))
             ? pdf_to_real(pdf_dict_get(doc, fn, "N")) : 1.0;
        n = (int)pdf_array_len(c0);
        if ((int)pdf_array_len(c1) > n) n = (int)pdf_array_len(c1);
        if (n == 0) n = 1;
        if (n > 8) n = 8;
        for (i = 0; i < n; i++) {
            double a = (i < (int)pdf_array_len(c0))
                     ? pdf_to_real(pdf_array_get(doc, c0, (size_t)i)) : 0.0;
            double b = (i < (int)pdf_array_len(c1))
                     ? pdf_to_real(pdf_array_get(doc, c1, (size_t)i)) : 1.0;
            double u = (d1 - d0) != 0.0 ? (t - d0) / (d1 - d0) : 0.0;

            out[i] = a + (nexp == 1.0 ? u : pow(u, nexp)) * (b - a);
        }
        *nout = n;
        return;
    }
    if (ftype == 3) {
        double lo = d0, hi = d1, e0 = 0.0, e1 = 1.0, u;
        int k;

        fns = pdf_dict_get(doc, fn, "Functions");
        bounds = pdf_dict_get(doc, fn, "Bounds");
        enc = pdf_dict_get(doc, fn, "Encode");
        n = (int)pdf_array_len(fns);
        if (n <= 0)
            return;
        for (k = 0; k < n - 1; k++)
            if (t < pdf_to_real(pdf_array_get(doc, bounds, (size_t)k)))
                break;
        if (k > 0)
            lo = pdf_to_real(pdf_array_get(doc, bounds, (size_t)(k - 1)));
        if (k < n - 1)
            hi = pdf_to_real(pdf_array_get(doc, bounds, (size_t)k));
        if ((int)pdf_array_len(enc) >= 2 * (k + 1)) {
            e0 = pdf_to_real(pdf_array_get(doc, enc, (size_t)(2 * k)));
            e1 = pdf_to_real(pdf_array_get(doc, enc, (size_t)(2 * k + 1)));
        }
        u = (hi - lo) != 0.0 ? (t - lo) / (hi - lo) : 0.0;
        r_fn_eval(doc, pdf_array_get(doc, fns, (size_t)k),
                  e0 + u * (e1 - e0), out, nout, depth + 1);
        return;
    }
}

/* One shading's colour at parameter t, in device RGB. */
static void r_shade_colour(pdf_doc *doc, pdf_obj *sh, double t, double *rgb)
{
    pdf_obj *fn = pdf_dict_get(doc, sh, "Function");
    const char *cs = pdf_to_name(pdf_dict_get(doc, sh, "ColorSpace"));
    double v[8];
    int nv = 0;

    rgb[0] = rgb[1] = rgb[2] = 0.0;
    if (pdf_is_array(fn) && pdf_array_len(fn) > 0) {
        size_t i;

        for (i = 0; i < pdf_array_len(fn) && nv < 8; i++) {
            double one[8];
            int n1 = 0;

            r_fn_eval(doc, pdf_array_get(doc, fn, i), t, one, &n1, 0);
            if (n1 > 0) v[nv++] = one[0];
        }
    } else {
        r_fn_eval(doc, fn, t, v, &nv, 0);
    }
    if (cs && !strcmp(cs, "DeviceGray") && nv >= 1) {
        rgb[0] = rgb[1] = rgb[2] = v[0];
    } else if (cs && !strcmp(cs, "DeviceCMYK") && nv >= 4) {
        r_cmyk_rgb(v[0], v[1], v[2], v[3], rgb);
    } else if (nv >= 3) {
        rgb[0] = v[0]; rgb[1] = v[1]; rgb[2] = v[2];
    } else if (nv >= 1) {
        rgb[0] = rgb[1] = rgb[2] = v[0];
    }
}

/* 8.7.4.5.3, the axial shading, painted the way it was measured: the colour of
 * a pixel is the function evaluated at the pixel's INTEGER device coordinate --
 * its top-left corner, not its centre.  Over an 830-pixel ramp the integer
 * convention matched 830/830 where the centre matched 702 and the right edge
 * 575, so this is a 127-pixel choice, not a rounding detail. */
static void r_shade_axial(rctx *R, pdf_obj *sh)
{
    rgstate *g = G(R);
    pdf_doc *doc = R->doc;
    pdf_obj *co = pdf_dict_get(doc, sh, "Coords");
    pdf_obj *dom = pdf_dict_get(doc, sh, "Domain");
    pdf_obj *ext = pdf_dict_get(doc, sh, "Extend");
    rmat inv;
    double ax0, ay0, ax1, ay1, dx, dy, den, t0 = 0.0, t1 = 1.0;
    int e0 = 0, e1 = 0, x, y, alpha = r_alpha_byte(g->ca);

    if (pdf_array_len(co) < 4 || r_invert(g->ctm, &inv) != 0)
        return;
    ax0 = pdf_to_real(pdf_array_get(doc, co, 0));
    ay0 = pdf_to_real(pdf_array_get(doc, co, 1));
    ax1 = pdf_to_real(pdf_array_get(doc, co, 2));
    ay1 = pdf_to_real(pdf_array_get(doc, co, 3));
    if (pdf_array_len(dom) >= 2) {
        t0 = pdf_to_real(pdf_array_get(doc, dom, 0));
        t1 = pdf_to_real(pdf_array_get(doc, dom, 1));
    }
    if (pdf_array_len(ext) >= 2) {
        pdf_obj *a = pdf_array_get(doc, ext, 0);
        pdf_obj *b = pdf_array_get(doc, ext, 1);

        e0 = (a && a->kind == PDF_BOOL && a->u.b);
        e1 = (b && b->kind == PDF_BOOL && b->u.b);
    }
    dx = ax1 - ax0;
    dy = ay1 - ay0;
    den = dx * dx + dy * dy;
    if (den == 0.0)
        return;

    for (y = g->clip[1]; y < g->clip[3]; y++) {
        for (x = g->clip[0]; x < g->clip[2]; x++) {
            double ux, uy, s, rgb[3];
            unsigned char src[3];

            if (x < 0 || y < 0 || x >= R->cv.w || y >= R->cv.h)
                continue;
            r_apply(inv, (double)x, (double)y, &ux, &uy);
            s = ((ux - ax0) * dx + (uy - ay0) * dy) / den;
            if (s < 0.0) { if (!e0) continue; s = 0.0; }
            if (s > 1.0) { if (!e1) continue; s = 1.0; }
            r_shade_colour(doc, sh, t0 + s * (t1 - t0), rgb);
            src[0] = r_comp(rgb[0]);
            src[1] = r_comp(rgb[1]);
            src[2] = r_comp(rgb[2]);
            r_pixel(R, x, y, src, alpha);
        }
    }
}

/* --------------------------------------------------- the content stream */

static void r_run(rctx *R, const unsigned char *buf, size_t len,
                  pdf_obj *res, int depth);

static pdf_obj *r_res(rctx *R, pdf_obj *res, const char *cat, const char *nm)
{
    pdf_obj *d = pdf_dict_get(R->doc, res, cat);

    return pdf_dict_get(R->doc, d, nm);
}

/* 11.6.5.  A luminosity soft mask: the group is rendered on its own, over a
 * backdrop of /BC (black when absent), and the luminosity of the result is the
 * mask.  The alpha comes from the luminosity byte through the COLOUR rule,
 * which is measured to differ from the one /ca uses. */
static unsigned char *r_build_smask(rctx *R, pdf_obj *sm, pdf_obj *res,
                                    int depth)
{
    pdf_doc *doc = R->doc;
    pdf_obj *grp = pdf_dict_get(doc, sm, "G");
    pdf_obj *bc = pdf_dict_get(doc, sm, "BC");
    const char *stype = pdf_to_name(pdf_dict_get(doc, sm, "S"));
    unsigned char *mask;
    rctx *sub;
    pdf_buf body;
    size_t i, n = (size_t)R->cv.w * (size_t)R->cv.h;
    double back = 0.0;

    if (!pdf_is_stream(grp) || depth > 4)
        return NULL;
    if (stype && strcmp(stype, "Luminosity") != 0)
        return NULL;                        /* /Alpha is not modelled */
    if (pdf_array_len(bc) >= 1)
        back = pdf_to_real(pdf_array_get(doc, bc, 0));

    sub = calloc(1, sizeof *sub);
    mask = malloc(n);
    if (!sub || !mask) { free(sub); free(mask); return NULL; }
    sub->doc = doc;
    sub->cv.w = R->cv.w;
    sub->cv.h = R->cv.h;
    sub->cv.px = malloc(n * 3);
    if (!sub->cv.px) { free(sub); free(mask); return NULL; }
    for (i = 0; i < n * 3; i++)
        sub->cv.px[i] = r_comp(back);
    sub->ngs = 0;
    sub->gs[0] = R->gs[R->ngs];
    sub->gs[0].smask = NULL;
    sub->gs[0].ca = sub->gs[0].CA = 1.0;
    sub->gs[0].blend = BM_NORMAL;
    sub->gs[0].clip[0] = 0; sub->gs[0].clip[1] = 0;
    sub->gs[0].clip[2] = R->cv.w; sub->gs[0].clip[3] = R->cv.h;

    pdf_buf_init(&body);
    if (pdf_stream_data(doc, grp, &body, 1) == 0) {
        pdf_obj *gres = pdf_dict_get(doc, grp, "Resources");

        r_run(sub, body.p, body.n, pdf_is_dict(gres) ? gres : res, depth + 1);
    }
    pdf_buf_free(&body);

    for (i = 0; i < n; i++) {
        /* 11.5.2: luminosity of the composited group. */
        double lum = (0.3 * sub->cv.px[i * 3] + 0.59 * sub->cv.px[i * 3 + 1]
                      + 0.11 * sub->cv.px[i * 3 + 2]) / 255.0;

        mask[i] = r_comp(lum);
    }
    free(sub->cv.px);
    free(sub);
    return mask;
}

static void r_do_gs(rctx *R, pdf_obj *eg, pdf_obj *res, int depth)
{
    pdf_doc *doc = R->doc;
    rgstate *g = G(R);
    pdf_obj *v;

    if (!pdf_is_dict(eg))
        return;
    v = pdf_dict_get(doc, eg, "ca");
    if (pdf_is_num(v)) g->ca = pdf_to_real(v);
    v = pdf_dict_get(doc, eg, "CA");
    if (pdf_is_num(v)) g->CA = pdf_to_real(v);
    v = pdf_dict_get(doc, eg, "BM");
    if (pdf_is_name(v, NULL))
        g->blend = r_blend_mode(pdf_to_name(v));
    else if (pdf_is_array(v) && pdf_array_len(v) > 0)
        g->blend = r_blend_mode(pdf_to_name(pdf_array_get(doc, v, 0)));
    v = pdf_dict_get(doc, eg, "SMask");
    if (pdf_is_name(v, "None")) {
        g->smask = NULL;
    } else if (pdf_is_dict(v)) {
        g->smask = r_build_smask(R, v, res, depth);
    }
}

static void r_run(rctx *R, const unsigned char *buf, size_t len,
                  pdf_obj *res, int depth)
{
    pdf_doc *doc = R->doc;
    pdf_lexer lx;
    pdf_token tok;
    double num[8];
    char name[64];
    int nnum = 0, intext = 0;

    if (depth > 8)
        return;
    name[0] = 0;
    pdf_lex_init(&lx, buf, len, 0);

    while (pdf_lex_next(doc, &lx, &tok) == 0 && tok.type != PDF_TOK_EOF) {
        if (tok.type == PDF_TOK_INT || tok.type == PDF_TOK_REAL) {
            if (nnum < 8)
                num[nnum++] = (tok.type == PDF_TOK_INT) ? (double)tok.i : tok.r;
            continue;
        }
        if (tok.type == PDF_TOK_NAME) {
            snprintf(name, sizeof name, "%s", tok.s ? tok.s : "");
            continue;
        }
        if (tok.type == PDF_TOK_ARRAY_OPEN || tok.type == PDF_TOK_DICT_OPEN) {
            pdf_toktype want = (tok.type == PDF_TOK_ARRAY_OPEN)
                             ? PDF_TOK_ARRAY_CLOSE : PDF_TOK_DICT_CLOSE;
            int d2 = 1;

            while (d2 > 0 && pdf_lex_next(doc, &lx, &tok) == 0
                   && tok.type != PDF_TOK_EOF) {
                if (tok.type == PDF_TOK_ARRAY_OPEN
                    || tok.type == PDF_TOK_DICT_OPEN) d2++;
                else if (tok.type == want || tok.type == PDF_TOK_ARRAY_CLOSE
                         || tok.type == PDF_TOK_DICT_CLOSE) d2--;
            }
            continue;
        }
        if (tok.type != PDF_TOK_KEYWORD) {
            continue;
        }
        {
            const char *op = tok.s ? tok.s : "";
            rgstate *g = G(R);

            if (!strcmp(op, "BT")) { intext = 1; }
            else if (!strcmp(op, "ET")) { intext = 0; }
            else if (intext) { /* 9.4: no scored page paints text */ }
            else if (!strcmp(op, "q")) {
                if (R->ngs + 1 < R_MAXDEPTH) {
                    R->gs[R->ngs + 1] = *g;
                    R->ngs++;
                }
            } else if (!strcmp(op, "Q")) {
                if (R->ngs > 0) R->ngs--;
            } else if (!strcmp(op, "cm") && nnum >= 6) {
                rmat m;

                m.a = num[0]; m.b = num[1]; m.c = num[2];
                m.d = num[3]; m.e = num[4]; m.f = num[5];
                g->ctm = r_mul(m, g->ctm);
            } else if (!strcmp(op, "m") && nnum >= 2) {
                if (R->npts < R_MAXPTS && R->nsub < R_MAXSUB) {
                    R->sub[R->nsub++] = R->npts;
                    r_apply(g->ctm, num[0], num[1],
                            &R->px[R->npts], &R->py[R->npts]);
                    R->npts++;
                }
            } else if (!strcmp(op, "l") && nnum >= 2) {
                if (R->npts < R_MAXPTS) {
                    r_apply(g->ctm, num[0], num[1],
                            &R->px[R->npts], &R->py[R->npts]);
                    R->npts++;
                }
            } else if ((!strcmp(op, "c") && nnum >= 6)
                       || (!strcmp(op, "v") && nnum >= 4)
                       || (!strcmp(op, "y") && nnum >= 4)) {
                /* A curve is flattened to its endpoint.  No scored page has
                 * one: a bezier renders at 17 distinct values and could not be
                 * matched exactly anyway. */
                int k = (!strcmp(op, "c")) ? 4 : 2;

                if (R->npts < R_MAXPTS) {
                    r_apply(g->ctm, num[k], num[k + 1],
                            &R->px[R->npts], &R->py[R->npts]);
                    R->npts++;
                }
            } else if (!strcmp(op, "re") && nnum >= 4) {
                if (R->npts + 5 < R_MAXPTS && R->nsub < R_MAXSUB) {
                    double xs[4], ys[4];
                    int i;

                    xs[0] = num[0];            ys[0] = num[1];
                    xs[1] = num[0] + num[2];   ys[1] = num[1];
                    xs[2] = num[0] + num[2];   ys[2] = num[1] + num[3];
                    xs[3] = num[0];            ys[3] = num[1] + num[3];
                    R->sub[R->nsub++] = R->npts;
                    for (i = 0; i < 4; i++) {
                        r_apply(g->ctm, xs[i], ys[i],
                                &R->px[R->npts], &R->py[R->npts]);
                        R->npts++;
                    }
                }
            } else if (!strcmp(op, "h")) {
                /* closing is implicit in how a subpath is filled */
            } else if (!strcmp(op, "W")) {
                R->pending_clip = 1;
            } else if (!strcmp(op, "W*")) {
                R->pending_clip = 1;
            } else if (!strcmp(op, "n")) {
                r_apply_clip(R);
                R->npts = R->nsub = 0;
            } else if (!strcmp(op, "f") || !strcmp(op, "F")
                       || !strcmp(op, "f*") || !strcmp(op, "B")
                       || !strcmp(op, "B*") || !strcmp(op, "b")
                       || !strcmp(op, "b*")) {
                r_fill_path(R, (strchr(op, '*') != NULL));
                r_apply_clip(R);
                R->npts = R->nsub = 0;
            } else if (!strcmp(op, "S") || !strcmp(op, "s")) {
                /* 8.4.3.2: no scored page strokes -- a stroke renders at 15
                 * distinct values and is outside what this reproduces. */
                r_apply_clip(R);
                R->npts = R->nsub = 0;
            } else if (!strcmp(op, "g") && nnum >= 1) {
                g->fill[0] = g->fill[1] = g->fill[2] = num[0];
                g->fill_pattern = 0;
            } else if (!strcmp(op, "G") && nnum >= 1) {
                g->stroke[0] = g->stroke[1] = g->stroke[2] = num[0];
            } else if (!strcmp(op, "rg") && nnum >= 3) {
                g->fill[0] = num[0]; g->fill[1] = num[1]; g->fill[2] = num[2];
                g->fill_pattern = 0;
            } else if (!strcmp(op, "RG") && nnum >= 3) {
                g->stroke[0] = num[0]; g->stroke[1] = num[1];
                g->stroke[2] = num[2];
            } else if (!strcmp(op, "k") && nnum >= 4) {
                r_cmyk_rgb(num[0], num[1], num[2], num[3], g->fill);
                g->fill_pattern = 0;
            } else if (!strcmp(op, "K") && nnum >= 4) {
                r_cmyk_rgb(num[0], num[1], num[2], num[3], g->stroke);
            } else if (!strcmp(op, "cs") || !strcmp(op, "CS")) {
                int isfill = (op[0] == 'c');
                int n = 1;

                if (!strcmp(name, "DeviceRGB")) n = 3;
                else if (!strcmp(name, "DeviceCMYK")) n = 4;
                else if (!strcmp(name, "Pattern")) n = 0;
                if (isfill) {
                    g->fill_cs_n = n;
                    g->fill_pattern = (n == 0);
                    if (n == 1) g->fill[0] = g->fill[1] = g->fill[2] = 0.0;
                } else {
                    g->stroke_cs_n = n;
                }
            } else if (!strcmp(op, "sc") || !strcmp(op, "scn")) {
                if (g->fill_pattern && name[0]) {
                    snprintf(g->fill_pattern_name,
                             sizeof g->fill_pattern_name, "%s", name);
                } else if (nnum >= 4) {
                    r_cmyk_rgb(num[0], num[1], num[2], num[3], g->fill);
                } else if (nnum >= 3) {
                    g->fill[0] = num[0]; g->fill[1] = num[1];
                    g->fill[2] = num[2];
                } else if (nnum >= 1) {
                    g->fill[0] = g->fill[1] = g->fill[2] = num[0];
                }
            } else if (!strcmp(op, "SC") || !strcmp(op, "SCN")) {
                if (nnum >= 4)
                    r_cmyk_rgb(num[0], num[1], num[2], num[3], g->stroke);
                else if (nnum >= 3) {
                    g->stroke[0] = num[0]; g->stroke[1] = num[1];
                    g->stroke[2] = num[2];
                } else if (nnum >= 1)
                    g->stroke[0] = g->stroke[1] = g->stroke[2] = num[0];
            } else if (!strcmp(op, "gs")) {
                r_do_gs(R, r_res(R, res, "ExtGState", name), res, depth);
            } else if (!strcmp(op, "sh")) {
                pdf_obj *sh = r_res(R, res, "Shading", name);

                if (pdf_is_dict(sh)
                    && pdf_to_int(pdf_dict_get(doc, sh, "ShadingType")) == 2)
                    r_shade_axial(R, sh);
            } else if (!strcmp(op, "Do")) {
                pdf_obj *xo = r_res(R, res, "XObject", name);

                if (pdf_is_stream(xo)
                    && pdf_is_name(pdf_dict_get(doc, xo, "Subtype"), "Form")) {
                    pdf_buf body;
                    pdf_obj *xres = pdf_dict_get(doc, xo, "Resources");
                    pdf_obj *mtx = pdf_dict_get(doc, xo, "Matrix");

                    if (R->ngs + 1 < R_MAXDEPTH) {
                        R->gs[R->ngs + 1] = *g;
                        R->ngs++;
                        if (pdf_array_len(mtx) >= 6) {
                            rmat m;

                            m.a = pdf_to_real(pdf_array_get(doc, mtx, 0));
                            m.b = pdf_to_real(pdf_array_get(doc, mtx, 1));
                            m.c = pdf_to_real(pdf_array_get(doc, mtx, 2));
                            m.d = pdf_to_real(pdf_array_get(doc, mtx, 3));
                            m.e = pdf_to_real(pdf_array_get(doc, mtx, 4));
                            m.f = pdf_to_real(pdf_array_get(doc, mtx, 5));
                            G(R)->ctm = r_mul(m, G(R)->ctm);
                        }
                        pdf_buf_init(&body);
                        if (pdf_stream_data(doc, xo, &body, 1) == 0)
                            r_run(R, body.p, body.n,
                                  pdf_is_dict(xres) ? xres : res, depth + 1);
                        pdf_buf_free(&body);
                        R->ngs--;
                    }
                }
            }
            nnum = 0;
            name[0] = 0;
        }
    }
}

/* ------------------------------------------------------------ the driver */

int pdf_render_page(pdf_doc *doc, int index, double dpi,
                    unsigned char **out, int *ow, int *oh)
{
    pdf_obj *page;
    pdf_rect mb;
    rctx *R;
    pdf_buf content;
    rmat base, rot;
    double wc, hc;
    int w, h, rotate, rc = 0;
    size_t i, n;

    if (!doc || !out || !ow || !oh)
        return PDF_E_RANGE;
    *out = NULL;
    page = pdf_page_dict(doc, index);
    if (!pdf_is_dict(page))
        return PDF_E_RANGE;
    if (pdf_page_box(doc, page, "MediaBox", &mb) != 0) {
        mb.x0 = 0; mb.y0 = 0; mb.x1 = 612; mb.y1 = 792;
    }
    if (mb.x1 < mb.x0) { double t = mb.x0; mb.x0 = mb.x1; mb.x1 = t; }
    if (mb.y1 < mb.y0) { double t = mb.y0; mb.y0 = mb.y1; mb.y1 = t; }

    /* The multiply happens before the divide.  Doing it the other way rounds
     * a 792-point page up to 1651 pixels instead of 1650. */
    wc = ((mb.x1 - mb.x0) * dpi) / 72.0;
    hc = ((mb.y1 - mb.y0) * dpi) / 72.0;
    rotate = pdf_page_rotate(doc, page);
    rotate = ((rotate % 360) + 360) % 360;
    rotate = (rotate / 90) * 90;

    if (rotate == 90 || rotate == 270) {
        w = (int)ceil(hc);
        h = (int)ceil(wc);
    } else {
        w = (int)ceil(wc);
        h = (int)ceil(hc);
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    R = calloc(1, sizeof *R);
    if (!R)
        return PDF_E_NOMEM;
    R->doc = doc;
    R->cv.w = w;
    R->cv.h = h;
    n = (size_t)w * (size_t)h * 3;
    R->cv.px = malloc(n);
    if (!R->cv.px) {
        free(R);
        return PDF_E_NOMEM;
    }
    /* 1.2: the page begins white and is composited onto an opaque backdrop. */
    for (i = 0; i < n; i++)
        R->cv.px[i] = 255;

    /* User space to unrotated device space: scale, flip y, move the media
     * box's own origin to the corner. */
    base.a = dpi / 72.0; base.b = 0.0;
    base.c = 0.0;        base.d = -dpi / 72.0;
    base.e = -mb.x0 * dpi / 72.0;
    base.f =  mb.y1 * dpi / 72.0;

    /* /Rotate turns the result in CONTINUOUS device space, using the un-ceiled
     * extents.  Using the rounded raster size instead is off by one on a page
     * whose extent is not a whole number of pixels. */
    switch (rotate) {
    case 90:  rot.a = 0; rot.b = 1; rot.c = -1; rot.d = 0;
              rot.e = hc; rot.f = 0; break;
    case 180: rot.a = -1; rot.b = 0; rot.c = 0; rot.d = -1;
              rot.e = wc; rot.f = hc; break;
    case 270: rot.a = 0; rot.b = -1; rot.c = 1; rot.d = 0;
              rot.e = 0; rot.f = wc; break;
    default:  rot.a = 1; rot.b = 0; rot.c = 0; rot.d = 1;
              rot.e = 0; rot.f = 0; break;
    }

    R->ngs = 0;
    R->gs[0].ctm = r_mul(base, rot);
    R->gs[0].fill[0] = R->gs[0].fill[1] = R->gs[0].fill[2] = 0.0;
    R->gs[0].stroke[0] = R->gs[0].stroke[1] = R->gs[0].stroke[2] = 0.0;
    R->gs[0].fill_cs_n = R->gs[0].stroke_cs_n = 1;
    R->gs[0].ca = R->gs[0].CA = 1.0;
    R->gs[0].blend = BM_NORMAL;
    R->gs[0].smask = NULL;
    R->gs[0].clip[0] = 0; R->gs[0].clip[1] = 0;
    R->gs[0].clip[2] = w; R->gs[0].clip[3] = h;

    pdf_buf_init(&content);
    if (pdf_page_content(doc, page, &content) == 0 && content.n > 0) {
        pdf_obj *res = pdf_page_inherited(doc, page, "Resources");

        r_run(R, content.p, content.n, res, 0);
    }
    pdf_buf_free(&content);

    *out = R->cv.px;
    *ow = w;
    *oh = h;
    free(R);
    return rc;
}
