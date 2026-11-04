/* filter.c -- stream filters and the /Filter chain.
 *
 * ISO 32000-1:2008 clause 7.4 "Filters", tables 6-12:
 *
 *   7.4.1  the /Filter and /DecodeParms chain          pdf_apply_filters()
 *   7.4.2  ASCIIHexDecode                              pdf_filter_ahx()
 *   7.4.3  ASCII85Decode                               pdf_filter_a85()
 *   7.4.4  LZWDecode                                   pdf_filter_lzw()
 *   7.4.4  FlateDecode                                 pdf_filter_flate()
 *   7.4.4.4 the /Predictor group (TIFF 2 and PNG 10-15) pdf_predictor()
 *   7.4.5  RunLengthDecode                             pdf_filter_rl()
 *   7.4.6  CCITTFaxDecode (ITU-T T.4 / T.6)            pdf_filter_ccitt()
 *
 * Every decoder here is written for hostile input: no loop is unbounded, no
 * read leaves the source buffer, and the total decoded size is capped (see
 * FILT_MAX_OUT).  A damaged stream is not an error -- the decoders return
 * whatever they recovered together with 0, which is what every real reader
 * does, because a truncated content stream still draws most of a page.
 */

#include "pdfint.h"

#include <zlib.h>
#include <limits.h>
#include <stdint.h>

/* Hard ceiling on anything this module produces.  A few kilobytes of hostile
 * input must not turn into unbounded memory growth, so every producer stops
 * cleanly (not with an error) once the output reaches this size. */
#define FILT_MAX_OUT ((size_t)256u << 20)      /* 256 MiB */

/* PDF white space, 7.2.2 table 1 (NUL included). */
#define IS_WS(c) ((c) == ' '  || (c) == '\t' || (c) == '\r' || \
                  (c) == '\n' || (c) == '\f' || (c) == '\0')

/* ------------------------------------------------------------------ output */

/* Append to `out` under the global cap.  Returns 0 when everything was
 * written, 1 when the cap was reached and the caller must stop, or a negative
 * PDF_E_* on allocation failure. */
static int out_write(pdf_buf *out, const void *p, size_t n)
{
    int full = 0;

    if (out->n >= FILT_MAX_OUT)
        return 1;
    if (n > FILT_MAX_OUT - out->n) {
        n = FILT_MAX_OUT - out->n;
        full = 1;
    }
    if (n && pdf_buf_put(out, p, n) < 0)
        return PDF_E_NOMEM;
    return full;
}

static int out_byte(pdf_buf *out, int c)
{
    if (out->n >= FILT_MAX_OUT)
        return 1;
    if (pdf_buf_putc(out, c) < 0)
        return PDF_E_NOMEM;
    return 0;
}

static int out_fill(pdf_buf *out, int c, size_t n)
{
    unsigned char blk[512];
    int rc;

    memset(blk, c, sizeof blk);
    while (n) {
        size_t k = n < sizeof blk ? n : sizeof blk;
        if ((rc = out_write(out, blk, k)) != 0)
            return rc;
        n -= k;
    }
    return 0;
}

/* Collapse the tri-state of out_write()/out_byte()/out_fill() into the value a
 * filter entry point should return: a cap hit is a clean stop. */
#define OUT_RET(rc) ((rc) < 0 ? (rc) : 0)

/* -------------------------------------------------------------- parameters */

static pdf_obj *parm_get(pdf_doc *doc, pdf_obj *parms, const char *key)
{
    if (!parms || !pdf_is_dict(parms))
        return pdf_null_obj();
    return pdf_dict_get(doc, parms, key);
}

static long long parm_int(pdf_doc *doc, pdf_obj *parms, const char *key,
                          long long dflt)
{
    pdf_obj *v = parm_get(doc, parms, key);
    return pdf_is_num(v) ? pdf_to_int(v) : dflt;
}

static int parm_bool(pdf_doc *doc, pdf_obj *parms, const char *key, int dflt)
{
    pdf_obj *v = parm_get(doc, parms, key);

    if (v->kind == PDF_BOOL)
        return v->u.b != 0;
    if (pdf_is_num(v))              /* 0/1 in place of false/true happens */
        return pdf_to_int(v) != 0;
    return dflt;
}

/* ============================================================ FlateDecode */

/* A zlib stream starts with CMF/FLG, 7.4.4.  Validate before trusting it:
 * plenty of files store raw deflate, or a few bytes of junk, instead. */
static int zlib_header_ok(const unsigned char *p)
{
    unsigned cmf = p[0], flg = p[1];

    if ((cmf & 0x0f) != 8)                  return 0;   /* not deflate */
    if (((cmf >> 4) & 0x0f) > 7)            return 0;   /* window > 32 KiB */
    if (((cmf << 8) | flg) % 31 != 0)       return 0;   /* header check */
    if (flg & 0x20)                         return 0;   /* preset dictionary */
    return 1;
}

/* One inflate attempt.  Appends whatever inflated -- possibly nothing, when
 * the stream is not deflate data at all -- and returns 0, or PDF_E_NOMEM. */
static int flate_pass(const unsigned char *src, size_t srclen, int wbits,
                      pdf_buf *out)
{
    z_stream zs;
    unsigned char tmp[32768];
    size_t fed = 0;
    int zr, rc, stalled = 0;

    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, wbits) != Z_OK)
        return PDF_E_NOMEM;

    for (;;) {
        uInt had_in = zs.avail_in;

        if (zs.avail_in == 0) {
            size_t left = srclen - fed;
            if (left == 0)
                break;                      /* truncated: keep what we have */
            if (left > 0x20000000u)
                left = 0x20000000u;         /* zlib takes a uInt at a time */
            zs.next_in  = (Bytef *)(uintptr_t)(src + fed);
            zs.avail_in = (uInt)left;
            had_in = zs.avail_in;
            fed += left;
        }
        zs.next_out  = tmp;
        zs.avail_out = (uInt)sizeof tmp;

        zr = inflate(&zs, Z_NO_FLUSH);

        if (zs.avail_out < sizeof tmp) {
            rc = out_write(out, tmp, sizeof tmp - zs.avail_out);
            if (rc < 0) { inflateEnd(&zs); return rc; }
            if (rc > 0) break;              /* hit the global cap */
            stalled = 0;
        } else if (zs.avail_in == had_in) {
            if (++stalled > 2)              /* no progress either way */
                break;
        }

        if (zr == Z_STREAM_END)
            break;
        if (zr == Z_MEM_ERROR) { inflateEnd(&zs); return PDF_E_NOMEM; }
        if (zr != Z_OK)
            break;                          /* damaged: keep what we have */
    }
    inflateEnd(&zs);
    return 0;
}

int pdf_filter_flate(pdf_doc *doc, const unsigned char *src, size_t srclen,
                     pdf_obj *parms, pdf_buf *out)
{
    size_t start, skip = 0, i, limit;
    int rc;

    (void)doc; (void)parms;     /* the predictor is a separate stage */

    if (!src || srclen == 0)
        return 0;
    start = out->n;

    /* Leading white space between "stream" and the data is common. */
    while (skip < srclen && IS_WS(src[skip]))
        skip++;
    if (skip >= srclen)
        return 0;

    /* 1. a header that validates: ordinary zlib data. */
    if (srclen - skip >= 2 && zlib_header_ok(src + skip)) {
        rc = flate_pass(src + skip, srclen - skip, 15, out);
        if (rc < 0) return rc;
        if (out->n > start) return 0;
        out->n = start;
    }
    /* 2. no usable header: raw deflate with a 32 KiB window. */
    rc = flate_pass(src + skip, srclen - skip, -15, out);
    if (rc < 0) return rc;
    if (out->n > start) return 0;
    out->n = start;

    /* 3. try the wrapper anyway; only the 2 header bytes may be corrupt. */
    if (srclen - skip >= 2) {
        rc = flate_pass(src + skip, srclen - skip, 15, out);
        if (rc < 0) return rc;
        if (out->n > start) return 0;
        out->n = start;
    }
    /* 4. junk in front of the real stream: hunt for a plausible header. */
    limit = srclen ? srclen - 1 : 0;
    if (limit > skip + 1024)
        limit = skip + 1024;
    for (i = skip + 1; i < limit; i++) {
        if (!zlib_header_ok(src + i))
            continue;
        rc = flate_pass(src + i, srclen - i, 15, out);
        if (rc < 0) return rc;
        if (out->n > start) return 0;
        out->n = start;
    }
    return 0;   /* nothing inflated; an empty result is not an error */
}

/* ============================================================== LZWDecode */

/* Walk an entry back to its root, leaving the string reversed in `stack`.
 * prev_of[] is strictly decreasing by construction, so this terminates. */
static int lzw_expand(const unsigned short *prev_of, const unsigned char *byte_of,
                      int code, unsigned char *stack)
{
    int n = 0;

    while (n < 4096) {
        stack[n++] = byte_of[code];
        if (code < 256)
            break;
        code = prev_of[code];
    }
    return n;
}

int pdf_filter_lzw(pdf_doc *doc, const unsigned char *src, size_t srclen,
                   pdf_obj *parms, pdf_buf *out)
{
    unsigned short prev_of[4096];
    unsigned char  byte_of[4096];
    unsigned char  stack[4096];
    unsigned char  emit[4097];
    unsigned long  bitbuf = 0;
    int i, bits = 0, width = 9, next = 258, prev = -1, early, rc;
    size_t pos = 0;

    if (!src || srclen == 0)
        return 0;

    /* 7.4.4.3: /EarlyChange selects whether the code width grows one code
     * early (1, the default) or exactly when the table fills (0). */
    early = parm_int(doc, parms, "EarlyChange", 1) != 0;

    for (i = 0; i < 256; i++) {
        prev_of[i] = 0;
        byte_of[i] = (unsigned char)i;
    }

    for (;;) {
        int code, first = 0, m, kwk = 0, j;

        while (bits < width) {
            if (pos >= srclen)
                return 0;               /* out of codes: normal for real files */
            bitbuf = (bitbuf << 8) | src[pos++];
            bits += 8;
        }
        code = (int)((bitbuf >> (bits - width)) & ((1UL << width) - 1UL));
        bits -= width;

        if (code == 256) {              /* clear table */
            next = 258; width = 9; prev = -1;
            continue;
        }
        if (code == 257)                /* end of data */
            return 0;

        if (code < 256) {
            stack[0] = (unsigned char)code;
            m = 1;
            first = code;
        } else if (code >= 258 && code < next) {
            m = lzw_expand(prev_of, byte_of, code, stack);
            first = stack[m - 1];
        } else if (code == next && prev >= 0) {
            /* KwKwK: the code names the entry we are about to create. */
            m = lzw_expand(prev_of, byte_of, prev, stack);
            first = stack[m - 1];
            kwk = 1;
        } else {
            return 0;                   /* damaged table reference */
        }

        for (j = 0; j < m; j++)
            emit[j] = stack[m - 1 - j];
        if (kwk)
            emit[m++] = (unsigned char)first;
        if ((rc = out_write(out, emit, (size_t)m)) != 0)
            return OUT_RET(rc);

        if (prev >= 0 && next < 4096) {
            prev_of[next] = (unsigned short)prev;
            byte_of[next] = (unsigned char)first;
            next++;
        }
        prev = code;

        /* 7.4.4.2: 10-bit codes once entry 511 exists, 11 at 1023, 12 at 2047
         * (one lower at each step when /EarlyChange is 0). */
        if (width < 12 && next + early >= (1 << width))
            width++;
    }
}

/* =========================================================== ASCII85Decode */

int pdf_filter_a85(const unsigned char *src, size_t srclen, pdf_buf *out)
{
    unsigned long long tuple = 0;
    unsigned char quad[4];
    unsigned long v;
    int count = 0, rc, j;
    size_t i = 0;

    if (!src)
        return 0;
    if (srclen >= 2 && src[0] == '<' && src[1] == '~')
        i = 2;                                  /* the optional "<~" prefix */

    for (; i < srclen; i++) {
        int c = src[i];

        if (IS_WS(c))
            continue;
        if (c == '~')
            break;                              /* "~>" end of data */
        if (c == 'z' && count == 0) {           /* shorthand for 4 zero bytes */
            quad[0] = quad[1] = quad[2] = quad[3] = 0;
            if ((rc = out_write(out, quad, 4)) != 0)
                return OUT_RET(rc);
            continue;
        }
        if (c < '!' || c > 'u')
            continue;                           /* ignore anything else */

        tuple = tuple * 85 + (unsigned)(c - '!');
        if (++count == 5) {
            v = (unsigned long)(tuple & 0xffffffffUL);
            quad[0] = (unsigned char)(v >> 24);
            quad[1] = (unsigned char)(v >> 16);
            quad[2] = (unsigned char)(v >> 8);
            quad[3] = (unsigned char)v;
            if ((rc = out_write(out, quad, 4)) != 0)
                return OUT_RET(rc);
            tuple = 0;
            count = 0;
        }
    }
    if (count > 1) {
        /* Partial final group: pad with 'u' and keep count-1 bytes. */
        for (j = count; j < 5; j++)
            tuple = tuple * 85 + 84;
        v = (unsigned long)(tuple & 0xffffffffUL);
        quad[0] = (unsigned char)(v >> 24);
        quad[1] = (unsigned char)(v >> 16);
        quad[2] = (unsigned char)(v >> 8);
        quad[3] = (unsigned char)v;
        if ((rc = out_write(out, quad, (size_t)(count - 1))) != 0)
            return OUT_RET(rc);
    }
    return 0;
}

/* ========================================================= ASCIIHexDecode */

int pdf_filter_ahx(const unsigned char *src, size_t srclen, pdf_buf *out)
{
    int hi = -1, rc;
    size_t i;

    if (!src)
        return 0;
    for (i = 0; i < srclen; i++) {
        int c = src[i], d;

        if (c == '>')
            break;                              /* end of data */
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else continue;                          /* white space and junk */

        if (hi < 0) {
            hi = d;
        } else {
            if ((rc = out_byte(out, (hi << 4) | d)) != 0)
                return OUT_RET(rc);
            hi = -1;
        }
    }
    if (hi >= 0) {                              /* odd digit: trailing zero */
        if ((rc = out_byte(out, hi << 4)) != 0)
            return OUT_RET(rc);
    }
    return 0;
}

/* ======================================================== RunLengthDecode */

int pdf_filter_rl(const unsigned char *src, size_t srclen, pdf_buf *out)
{
    size_t i = 0;
    int rc;

    if (!src)
        return 0;
    while (i < srclen) {
        unsigned len = src[i++];

        if (len == 128)
            break;                              /* end of data */
        if (len < 128) {                        /* n+1 literal bytes */
            size_t n = (size_t)len + 1;
            if (n > srclen - i)
                n = srclen - i;                 /* truncated */
            if (n == 0)
                break;
            if ((rc = out_write(out, src + i, n)) != 0)
                return OUT_RET(rc);
            i += n;
        } else {                                /* 257-n copies of one byte */
            size_t n = 257u - len;
            if (i >= srclen)
                break;
            if ((rc = out_fill(out, src[i++], n)) != 0)
                return OUT_RET(rc);
        }
    }
    return 0;
}

/* =============================================================== predictor */

static unsigned bits_get(const unsigned char *p, size_t bitoff, int nbits)
{
    unsigned v = 0;
    int i;

    for (i = 0; i < nbits; i++) {
        size_t b = bitoff + (size_t)i;
        v = (v << 1) | ((unsigned)(p[b >> 3] >> (7 - (b & 7))) & 1u);
    }
    return v;
}

static void bits_set(unsigned char *p, size_t bitoff, int nbits, unsigned v)
{
    int i;

    for (i = 0; i < nbits; i++) {
        size_t b = bitoff + (size_t)i;
        unsigned char m = (unsigned char)(0x80u >> (b & 7));
        if ((v >> (nbits - 1 - i)) & 1u) p[b >> 3] |= m;
        else                             p[b >> 3] &= (unsigned char)~m;
    }
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

static void png_unfilter(int ft, unsigned char *cur, const unsigned char *prev,
                         size_t len, size_t bpp)
{
    size_t i;

    switch (ft) {
    case 0:                                             /* None */
        break;
    case 1:                                             /* Sub */
        for (i = bpp; i < len; i++)
            cur[i] = (unsigned char)(cur[i] + cur[i - bpp]);
        break;
    case 2:                                             /* Up */
        for (i = 0; i < len; i++)
            cur[i] = (unsigned char)(cur[i] + prev[i]);
        break;
    case 3:                                             /* Average */
        for (i = 0; i < len; i++) {
            unsigned a = (i >= bpp) ? cur[i - bpp] : 0u;
            cur[i] = (unsigned char)(cur[i] + ((a + prev[i]) >> 1));
        }
        break;
    case 4:                                             /* Paeth */
        for (i = 0; i < len; i++) {
            int a = (i >= bpp) ? cur[i - bpp] : 0;
            int c = (i >= bpp) ? prev[i - bpp] : 0;
            cur[i] = (unsigned char)(cur[i] + paeth(a, prev[i], c));
        }
        break;
    default:                                            /* unknown: leave it */
        break;
    }
}

int pdf_predictor(pdf_doc *doc, pdf_buf *buf, pdf_obj *parms)
{
    long long pred, colors, bpc, columns;
    size_t rowlen, bpp;

    if (!buf || buf->n == 0)
        return 0;

    pred = parm_int(doc, parms, "Predictor", 1);
    if (pred <= 1)
        return 0;                       /* 1 (and 0) mean "no prediction" */

    colors  = parm_int(doc, parms, "Colors", 1);
    bpc     = parm_int(doc, parms, "BitsPerComponent", 8);
    columns = parm_int(doc, parms, "Columns", 1);

    if (colors  < 1) colors  = 1;
    if (columns < 1) columns = 1;
    if (colors > 64 || columns > (1LL << 24))
        return PDF_E_RANGE;
    if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16)
        return PDF_E_RANGE;
    if (pred != 2 && pred < 10)
        return PDF_E_RANGE;             /* 3..9 are not defined */

    rowlen = (size_t)((columns * colors * bpc + 7) / 8);
    bpp    = (size_t)((colors * bpc + 7) / 8);
    if (bpp == 0)
        bpp = 1;
    if (rowlen == 0)
        return 0;

    if (pred == 2) {
        /* TIFF predictor 2: horizontal differencing, component by component,
         * against the previous pixel of the same row.  In place, no reflow. */
        size_t nrows = buf->n / rowlen, r;

        for (r = 0; r < nrows; r++) {
            unsigned char *p = buf->p + r * rowlen;
            size_t i;

            if (bpc == 8) {
                for (i = (size_t)colors; i < rowlen; i++)
                    p[i] = (unsigned char)(p[i] + p[i - (size_t)colors]);
            } else if (bpc == 16) {
                size_t step = (size_t)colors * 2;
                for (i = step; i + 1 < rowlen; i += 2) {
                    unsigned v = (unsigned)((p[i] << 8) | p[i + 1]);
                    unsigned w = (unsigned)((p[i - step] << 8) | p[i - step + 1]);
                    v = (v + w) & 0xffffu;
                    p[i]     = (unsigned char)(v >> 8);
                    p[i + 1] = (unsigned char)v;
                }
            } else {
                unsigned mask = (1u << bpc) - 1u;
                long long col, k;
                for (col = 1; col < columns; col++) {
                    for (k = 0; k < colors; k++) {
                        size_t idx = (size_t)((col * colors + k) * bpc);
                        size_t pid = (size_t)(((col - 1) * colors + k) * bpc);
                        unsigned v = bits_get(p, idx, (int)bpc);
                        unsigned w = bits_get(p, pid, (int)bpc);
                        bits_set(p, idx, (int)bpc, (v + w) & mask);
                    }
                }
            }
        }
        return 0;
    }

    /* PNG predictors 10..15: every row carries a leading filter-type byte,
     * which this stage consumes, so the buffer shrinks by one byte per row.
     * The PDF /Predictor value only states the encoder's *preferred* filter;
     * the per-row byte is authoritative (7.4.4.4). */
    {
        unsigned char *zero, *dst = buf->p;
        const unsigned char *prev;
        size_t pos = 0, outn = 0;

        zero = (unsigned char *)calloc(1, rowlen);
        if (!zero)
            return PDF_E_NOMEM;
        prev = zero;

        while (pos < buf->n) {
            size_t avail, len;
            int ft = buf->p[pos++];

            avail = buf->n - pos;
            len = avail < rowlen ? avail : rowlen;   /* last row may be short */
            if (len == 0)
                break;
            memmove(dst, buf->p + pos, len);
            pos += len;
            png_unfilter(ft, dst, prev, len, bpp);
            prev = dst;
            dst += len;
            outn += len;
            if (len < rowlen)
                break;                               /* truncated tail row */
        }
        free(zero);
        buf->n = outn;
    }
    return 0;
}

/* ========================================================== CCITTFaxDecode */

/* ITU-T T.4 tables 2/3 (terminating and make-up codes) plus table 4 (extended
 * make-up codes 1792..2560, common to both colours).  Sorted by code length so
 * cc_lookup() peeks the source once per distinct length.  Both tables were
 * checked to be prefix-free and not to collide with EOL. */
typedef struct { unsigned short code; unsigned char len; short run; } cc_code;

static const cc_code cc_white[104] = {
    {0x007, 4,   2}, {0x008, 4,   3}, {0x00b, 4,   4}, {0x00c, 4,   5},
    {0x00e, 4,   6}, {0x00f, 4,   7}, {0x007, 5,  10}, {0x008, 5,  11},
    {0x012, 5, 128}, {0x013, 5,   8}, {0x014, 5,   9}, {0x01b, 5,  64},
    {0x003, 6,  13}, {0x007, 6,   1}, {0x008, 6,  12}, {0x017, 6, 192},
    {0x018, 6,1664}, {0x02a, 6,  16}, {0x02b, 6,  17}, {0x034, 6,  14},
    {0x035, 6,  15}, {0x003, 7,  22}, {0x004, 7,  23}, {0x008, 7,  20},
    {0x00c, 7,  19}, {0x013, 7,  26}, {0x017, 7,  21}, {0x018, 7,  28},
    {0x024, 7,  27}, {0x027, 7,  18}, {0x028, 7,  24}, {0x02b, 7,  25},
    {0x037, 7, 256}, {0x002, 8,  29}, {0x003, 8,  30}, {0x004, 8,  45},
    {0x005, 8,  46}, {0x00a, 8,  47}, {0x00b, 8,  48}, {0x012, 8,  33},
    {0x013, 8,  34}, {0x014, 8,  35}, {0x015, 8,  36}, {0x016, 8,  37},
    {0x017, 8,  38}, {0x01a, 8,  31}, {0x01b, 8,  32}, {0x024, 8,  53},
    {0x025, 8,  54}, {0x028, 8,  39}, {0x029, 8,  40}, {0x02a, 8,  41},
    {0x02b, 8,  42}, {0x02c, 8,  43}, {0x02d, 8,  44}, {0x032, 8,  61},
    {0x033, 8,  62}, {0x034, 8,  63}, {0x035, 8,   0}, {0x036, 8, 320},
    {0x037, 8, 384}, {0x04a, 8,  59}, {0x04b, 8,  60}, {0x052, 8,  49},
    {0x053, 8,  50}, {0x054, 8,  51}, {0x055, 8,  52}, {0x058, 8,  55},
    {0x059, 8,  56}, {0x05a, 8,  57}, {0x05b, 8,  58}, {0x064, 8, 448},
    {0x065, 8, 512}, {0x067, 8, 640}, {0x068, 8, 576}, {0x098, 9,1472},
    {0x099, 9,1536}, {0x09a, 9,1600}, {0x09b, 9,1728}, {0x0cc, 9, 704},
    {0x0cd, 9, 768}, {0x0d2, 9, 832}, {0x0d3, 9, 896}, {0x0d4, 9, 960},
    {0x0d5, 9,1024}, {0x0d6, 9,1088}, {0x0d7, 9,1152}, {0x0d8, 9,1216},
    {0x0d9, 9,1280}, {0x0da, 9,1344}, {0x0db, 9,1408}, {0x008,11,1792},
    {0x00c,11,1856}, {0x00d,11,1920}, {0x012,12,1984}, {0x013,12,2048},
    {0x014,12,2112}, {0x015,12,2176}, {0x016,12,2240}, {0x017,12,2304},
    {0x01c,12,2368}, {0x01d,12,2432}, {0x01e,12,2496}, {0x01f,12,2560},
};

static const cc_code cc_black[104] = {
    {0x002, 2,   3}, {0x003, 2,   2}, {0x002, 3,   1}, {0x003, 3,   4},
    {0x002, 4,   6}, {0x003, 4,   5}, {0x003, 5,   7}, {0x004, 6,   9},
    {0x005, 6,   8}, {0x004, 7,  10}, {0x005, 7,  11}, {0x007, 7,  12},
    {0x004, 8,  13}, {0x007, 8,  14}, {0x018, 9,  15}, {0x008,10,  18},
    {0x00f,10,  64}, {0x017,10,  16}, {0x018,10,  17}, {0x037,10,   0},
    {0x008,11,1792}, {0x00c,11,1856}, {0x00d,11,1920}, {0x017,11,  24},
    {0x018,11,  25}, {0x028,11,  23}, {0x037,11,  22}, {0x067,11,  19},
    {0x068,11,  20}, {0x06c,11,  21}, {0x012,12,1984}, {0x013,12,2048},
    {0x014,12,2112}, {0x015,12,2176}, {0x016,12,2240}, {0x017,12,2304},
    {0x01c,12,2368}, {0x01d,12,2432}, {0x01e,12,2496}, {0x01f,12,2560},
    {0x024,12,  52}, {0x027,12,  55}, {0x028,12,  56}, {0x02b,12,  59},
    {0x02c,12,  60}, {0x033,12, 320}, {0x034,12, 384}, {0x035,12, 448},
    {0x037,12,  53}, {0x038,12,  54}, {0x052,12,  50}, {0x053,12,  51},
    {0x054,12,  44}, {0x055,12,  45}, {0x056,12,  46}, {0x057,12,  47},
    {0x058,12,  57}, {0x059,12,  58}, {0x05a,12,  61}, {0x05b,12, 256},
    {0x064,12,  48}, {0x065,12,  49}, {0x066,12,  62}, {0x067,12,  63},
    {0x068,12,  30}, {0x069,12,  31}, {0x06a,12,  32}, {0x06b,12,  33},
    {0x06c,12,  40}, {0x06d,12,  41}, {0x0c8,12, 128}, {0x0c9,12, 192},
    {0x0ca,12,  26}, {0x0cb,12,  27}, {0x0cc,12,  28}, {0x0cd,12,  29},
    {0x0d2,12,  34}, {0x0d3,12,  35}, {0x0d4,12,  36}, {0x0d5,12,  37},
    {0x0d6,12,  38}, {0x0d7,12,  39}, {0x0da,12,  42}, {0x0db,12,  43},
    {0x04a,13, 640}, {0x04b,13, 704}, {0x04c,13, 768}, {0x04d,13, 832},
    {0x052,13,1280}, {0x053,13,1344}, {0x054,13,1408}, {0x055,13,1472},
    {0x05a,13,1536}, {0x05b,13,1600}, {0x064,13,1664}, {0x065,13,1728},
    {0x06c,13, 512}, {0x06d,13, 576}, {0x072,13, 896}, {0x073,13, 960},
    {0x074,13,1024}, {0x075,13,1088}, {0x076,13,1152}, {0x077,13,1216},
};

#define CC_NWHITE ((int)(sizeof cc_white / sizeof cc_white[0]))
#define CC_NBLACK ((int)(sizeof cc_black / sizeof cc_black[0]))

/* T.4 table 4 / T.6 2.2: two-dimensional mode codes. */
enum {
    CC_M_PASS = 0, CC_M_HORIZ, CC_M_V0, CC_M_VR1, CC_M_VR2, CC_M_VR3,
    CC_M_VL1, CC_M_VL2, CC_M_VL3, CC_M_EXT, CC_M_EOL
};

static const cc_code cc_mode[] = {
    {0x001,  1, CC_M_V0},       /* 1            */
    {0x001,  3, CC_M_HORIZ},    /* 001          */
    {0x002,  3, CC_M_VL1},      /* 010          */
    {0x003,  3, CC_M_VR1},      /* 011          */
    {0x001,  4, CC_M_PASS},     /* 0001         */
    {0x002,  6, CC_M_VL2},      /* 000010       */
    {0x003,  6, CC_M_VR2},      /* 000011       */
    {0x001,  7, CC_M_EXT},      /* 0000001      */
    {0x002,  7, CC_M_VL3},      /* 0000010      */
    {0x003,  7, CC_M_VR3},      /* 0000011      */
    {0x001, 12, CC_M_EOL}       /* 000000000001 */
};

/* Vertical-mode offsets from b1, indexed by mode. */
static const int cc_vdelta[] = { 0, 0, 0, 1, 2, 3, -1, -2, -3, 0, 0 };

typedef struct {
    const unsigned char *p;
    size_t nbyte, nbits, pos;
} cc_bits;

/* Peek up to 24 bits, MSB first, zero-padded past the end of the source. */
static unsigned cc_peek(const cc_bits *b, int n)
{
    unsigned long long v = 0;
    size_t byte = b->pos >> 3;
    int off = (int)(b->pos & 7), i;

    for (i = 0; i < 5; i++)
        v = (v << 8) | ((byte + (size_t)i < b->nbyte) ? b->p[byte + (size_t)i]
                                                      : 0ull);
    return (unsigned)((v >> (40 - off - n)) & ((1ull << n) - 1ull));
}

static int  cc_eod(const cc_bits *b)      { return b->pos >= b->nbits; }
static void cc_skip(cc_bits *b, int n)    { b->pos += (size_t)n; }
static void cc_align(cc_bits *b)          { b->pos = (b->pos + 7u) & ~(size_t)7u; }

/* Longest-prefix match against a length-sorted table.  Returns the entry's
 * run/mode, or -1 when nothing matches or the source is exhausted. */
static int cc_lookup(cc_bits *b, const cc_code *tab, int n)
{
    unsigned v = 0;
    int i, len = -1;

    if (cc_eod(b))
        return -1;
    for (i = 0; i < n; i++) {
        if ((int)tab[i].len != len) {
            len = tab[i].len;
            v = cc_peek(b, len);
        }
        if (tab[i].code == v) {
            cc_skip(b, len);
            return tab[i].run;
        }
    }
    return -1;
}

/* A run is zero or more make-up codes (>= 64) closed by one terminating code
 * (< 64); T.4 lets the 2560 make-up repeat for runs longer than 2623. */
static int cc_read_run(cc_bits *b, int black)
{
    int total = 0, i;

    for (i = 0; i < 64; i++) {
        int r = cc_lookup(b, black ? cc_black : cc_white,
                             black ? CC_NBLACK : CC_NWHITE);
        if (r < 0)
            return -1;
        total += r;
        if (r < 64)
            return total;
    }
    return -1;                      /* make-up codes without a terminator */
}

/* Consume zero fill followed by EOL (000000000001) when that is what comes
 * next; otherwise leave the reader exactly where it was. */
static int cc_try_eol(cc_bits *b)
{
    size_t save = b->pos;
    int zeros = 0;

    while (!cc_eod(b)) {
        if (cc_peek(b, 1) == 0) {
            cc_skip(b, 1);
            if (++zeros > 4096)
                break;
            continue;
        }
        if (zeros >= 11) {
            cc_skip(b, 1);
            return 1;
        }
        break;
    }
    b->pos = save;
    return 0;
}

static void cc_paint(unsigned char *row, int from, int to, int blackis1)
{
    int x;

    for (x = from; x < to; x++) {
        unsigned char m = (unsigned char)(0x80u >> (x & 7));
        if (blackis1) row[x >> 3] |= m;
        else          row[x >> 3] &= (unsigned char)~m;
    }
}

/* Pack one row of changing elements.  A row starts white; cur[0] is where it
 * turns black, cur[1] where it turns white again, and so on.  Table 11: with
 * the default /BlackIs1 false, black pixels are 0 bits. */
static void cc_emit_row(unsigned char *row, size_t rowbytes, const int *cur,
                        int n, int columns, int blackis1)
{
    int i, pos = 0, color = 0;

    memset(row, blackis1 ? 0x00 : 0xff, rowbytes);      /* all white */
    for (i = 0; i < n && pos < columns; i++) {
        int end = cur[i];
        if (end > columns) end = columns;
        if (end < pos)     end = pos;
        if (color)
            cc_paint(row, pos, end, blackis1);
        pos = end;
        color ^= 1;
    }
    if (pos < columns && color)
        cc_paint(row, pos, columns, blackis1);
}

/* Group 3 one-dimensional row, T.4 4.1.2: alternating white and black runs.
 * Returns 0 for a complete row, 1 for a short one, -1 for damage. */
static int cc_row_1d(cc_bits *b, int *cur, int maxn, int columns, int *nout)
{
    int a0 = 0, color = 0, n = 0;

    while (a0 < columns) {
        int run = cc_read_run(b, color);
        if (run < 0) { *nout = n; return n ? 1 : -1; }
        a0 += run;
        if (a0 > columns) a0 = columns;
        if (n >= maxn)    { *nout = n; return -1; }
        cur[n++] = a0;
        color ^= 1;
    }
    *nout = n;
    return 0;
}

/* Group 4 (and G3 2-D) row, T.6 2.2: pass, horizontal and vertical modes
 * relative to the changing elements of the row above. */
static int cc_row_2d(cc_bits *b, const int *ref, int refn, int *cur, int maxn,
                     int columns, int *nout)
{
    int a0 = -1, color = 0, n = 0, guard = 0;

    while (a0 < columns) {
        int b1, b2, i, mode, a1, a2;

        if (++guard > 2 * columns + 64) { *nout = n; return -1; }

        /* b1 is the first changing element on the reference line right of a0
         * whose colour is the opposite of the current colour.  Elements
         * alternate starting with a white-to-black change, so ref[i] changes
         * to black exactly when i is even -- that is, we need (i & 1)==color. */
        i = 0;
        while (i < refn && ref[i] <= a0)
            i++;
        if (((unsigned)i & 1u) != (unsigned)color)
            i++;
        b1 = (i     < refn) ? ref[i]     : columns;
        b2 = (i + 1 < refn) ? ref[i + 1] : columns;
        if (b1 > columns) b1 = columns;
        if (b2 > columns) b2 = columns;
        if (b2 < b1)      b2 = b1;

        mode = cc_lookup(b, cc_mode, (int)(sizeof cc_mode / sizeof cc_mode[0]));
        switch (mode) {
        case CC_M_PASS:
            a0 = b2;                        /* the run continues past b2 */
            break;

        case CC_M_HORIZ: {
            int s  = (a0 < 0) ? 0 : a0;
            int r1 = cc_read_run(b, color);
            int r2 = (r1 < 0) ? -1 : cc_read_run(b, color ^ 1);

            if (r1 < 0 || r2 < 0) { *nout = n; return -1; }
            if (n + 2 > maxn)     { *nout = n; return -1; }
            a1 = s  + r1; if (a1 > columns) a1 = columns;
            a2 = a1 + r2; if (a2 > columns) a2 = columns;
            if (n && a1 < cur[n - 1]) a1 = cur[n - 1];
            cur[n++] = a1;
            if (a2 < a1) a2 = a1;
            cur[n++] = a2;
            a0 = a2;                        /* colour unchanged */
            break;
        }

        case CC_M_V0:  case CC_M_VR1: case CC_M_VR2: case CC_M_VR3:
        case CC_M_VL1: case CC_M_VL2: case CC_M_VL3:
            a1 = b1 + cc_vdelta[mode];
            if (a1 < 0)       a1 = 0;
            if (a1 > columns) a1 = columns;
            if (n >= maxn)    { *nout = n; return -1; }
            if (n && a1 < cur[n - 1]) a1 = cur[n - 1];
            cur[n++] = a1;
            a0 = a1;
            color ^= 1;
            break;

        case CC_M_EOL:
            *nout = n;
            return 1;                       /* EOL / EOFB ends the block */

        default:                            /* extension code or garbage */
            *nout = n;
            return -1;
        }
    }
    *nout = n;
    return 0;
}

int pdf_filter_ccitt(pdf_doc *doc, const unsigned char *src, size_t srclen,
                     pdf_obj *parms, pdf_buf *out)
{
    long long k, columns, rows, produced = 0;
    int blackis1, balign, twod, refn, maxn, rc = 0;
    int *ref = NULL, *cur = NULL;
    unsigned char *row = NULL;
    size_t rowbytes;
    cc_bits bits;

    k        = parm_int(doc, parms, "K", 0);
    columns  = parm_int(doc, parms, "Columns", 1728);
    rows     = parm_int(doc, parms, "Rows", 0);
    blackis1 = parm_bool(doc, parms, "BlackIs1", 0);
    balign   = parm_bool(doc, parms, "EncodedByteAlign", 0);

    if (k > 0)
        return PDF_E_UNSUP;                 /* Group 3 two-dimensional */
    if (columns < 1 || columns > (1LL << 20))
        return PDF_E_RANGE;
    if (rows < 0)
        rows = 0;
    if (!src || srclen == 0)
        return 0;

    twod     = (k < 0);
    rowbytes = (size_t)((columns + 7) / 8);
    maxn     = (int)(2 * columns + 8);

    ref = (int *)calloc((size_t)maxn + 2, sizeof *ref);
    cur = (int *)calloc((size_t)maxn + 2, sizeof *cur);
    row = (unsigned char *)malloc(rowbytes);
    if (!ref || !cur || !row) { rc = PDF_E_NOMEM; goto done; }

    bits.p     = src;
    bits.nbyte = srclen;
    bits.nbits = srclen * 8;
    bits.pos   = 0;

    /* The reference line for row 0 is an imaginary all-white line. */
    ref[0] = ref[1] = (int)columns;
    refn = 2;

    for (;;) {
        size_t before;
        int n = 0, r, w;

        if (rows > 0 && produced >= rows) break;
        if (out->n >= FILT_MAX_OUT)        break;
        if (balign) cc_align(&bits);

        if (cc_try_eol(&bits)) {
            if (twod)
                break;                      /* T.6 EOFB closes the block */
            while (cc_try_eol(&bits))       /* T.4 RTC is six EOLs */
                ;
        }
        if (cc_eod(&bits)) break;
        before = bits.pos;

        if (twod) r = cc_row_2d(&bits, ref, refn, cur, maxn, (int)columns, &n);
        else      r = cc_row_1d(&bits, cur, maxn, (int)columns, &n);

        if (n == 0 && (r < 0 || bits.pos == before))
            break;                          /* nothing usable left */

        cc_emit_row(row, rowbytes, cur, n, (int)columns, blackis1);
        if ((w = out_write(out, row, rowbytes)) != 0) {
            if (w < 0) { rc = w; goto done; }
            break;
        }
        produced++;

        /* The row just decoded becomes the next reference line, with two
         * sentinels at `columns` so b1/b2 fall off the right edge cleanly. */
        { int *t = ref; ref = cur; cur = t; }
        refn = n;
        if (refn > maxn) refn = maxn;
        ref[refn] = ref[refn + 1] = (int)columns;
        refn += 2;

        if (r != 0)
            break;                          /* short or damaged row was last */
    }

    /* /Rows is the image height: pad a truncated stream out with white so the
     * consumer still gets the rows its /Height promises. */
    if (rows > 0 && produced > 0 && produced < rows) {
        memset(row, blackis1 ? 0x00 : 0xff, rowbytes);
        while (produced < rows) {
            int w = out_write(out, row, rowbytes);
            if (w < 0) { rc = w; goto done; }
            if (w > 0) break;
            produced++;
        }
    }

done:
    free(ref);
    free(cur);
    free(row);
    return rc;
}

/* ======================================================== the filter chain */

enum {
    FILT_NONE = 0,      /* unknown: stop and hand back what we have */
    FILT_FLATE, FILT_LZW, FILT_A85, FILT_AHX, FILT_RL, FILT_CCITT,
    FILT_PASS,          /* image codec decoded elsewhere: leave the bytes */
    FILT_REFUSE,        /* 7.4.9: the whole chain is refused, not passed on */
    FILT_NOOP           /* /Crypt: handled by the decryption stage */
};

/* Table 6 names, plus the abbreviations legal in inline images (table 93). */
static int filter_id(const char *name)
{
    if (!name || !*name) return FILT_NONE;

    if (!strcmp(name, "FlateDecode")     || !strcmp(name, "Fl"))  return FILT_FLATE;
    if (!strcmp(name, "LZWDecode")       || !strcmp(name, "LZW")) return FILT_LZW;
    if (!strcmp(name, "ASCII85Decode")   || !strcmp(name, "A85")) return FILT_A85;
    if (!strcmp(name, "ASCIIHexDecode")  || !strcmp(name, "AHx")) return FILT_AHX;
    if (!strcmp(name, "RunLengthDecode") || !strcmp(name, "RL"))  return FILT_RL;
    if (!strcmp(name, "CCITTFaxDecode")  || !strcmp(name, "CCF")) return FILT_CCITT;

    /* 7.4.9.  JPXDecode is not applied here and, unlike the other image
     * codecs, its bytes may not be handed back half-decoded either: a request
     * for the decoded contents of such a stream is refused outright.  The
     * stored bytes stay available in full -- pdf_stream_data(decode == 0)
     * never reaches this table.  The pass-through below still answers for
     * DCTDecode and JBIG2Decode, which are not refused. */
    if (!strcmp(name, "JPXDecode")) return FILT_REFUSE;
    /* 7.4.7.  JBIG2Decode joins it at this milestone.  Applying the codec is
     * pdfimages' job now, not this option's, and handing back a half-decoded
     * chain here would be a WRONG answer rather than a partial one -- so the
     * refusal covers JBIG2 exactly as it already covers JPX. */
    if (!strcmp(name, "JBIG2Decode")) return FILT_REFUSE;
    if (!strcmp(name, "DCTDecode")   || !strcmp(name, "DCT"))     return FILT_PASS;
    if (!strcmp(name, "JPXDecode")   || !strcmp(name, "JBIG2Decode")) return FILT_PASS;

    if (!strcmp(name, "Crypt")) return FILT_NOOP;
    return FILT_NONE;
}

int pdf_apply_filters(pdf_doc *doc, pdf_obj *stmdict, const unsigned char *src,
                      size_t srclen, pdf_buf *out)
{
    const unsigned char *in = src;
    size_t inlen = srclen, nf, i;
    pdf_obj *filter, *dparms;
    pdf_buf a, b, *dst;
    int rc = 0, isarray;

    if (!src) { inlen = srclen = 0; in = src = (const unsigned char *)""; }

    /* /F and /DP are the inline-image spellings of /Filter and /DecodeParms.
     * A stream dictionary's /F is a file specification (a string or a dict),
     * never a name or array, so it cannot be mistaken for a filter list. */
    filter = pdf_dict_geta(doc, stmdict, "Filter", "F");
    dparms = pdf_dict_geta(doc, stmdict, "DecodeParms", "DP");

    isarray = pdf_is_array(filter);
    if (isarray)                        nf = pdf_array_len(filter);
    else if (pdf_is_name(filter, NULL)) nf = 1;
    else                                nf = 0;

    if (nf == 0)
        return OUT_RET(out_write(out, src, srclen));

    pdf_buf_init(&a);
    pdf_buf_init(&b);
    dst = &a;

    for (i = 0; i < nf; i++) {
        pdf_obj *fo = isarray ? pdf_array_get(doc, filter, i) : filter;
        pdf_obj *po;
        int id = filter_id(pdf_to_name(fo));

        /* An array of /DecodeParms runs parallel to /Filter.  A lone
         * dictionary is offered to every filter: only Flate, LZW and CCITT
         * read one, so a dictionary meant for the last filter still lands. */
        if      (pdf_is_array(dparms)) po = pdf_array_get(doc, dparms, i);
        else if (pdf_is_dict(dparms))  po = dparms;
        else                           po = NULL;
        if (po && !pdf_is_dict(po)) po = NULL;

        if (id == FILT_NOOP)
            continue;
        /* The refusal covers the chain, not the stage: handing back what the
         * earlier filters produced would be a partial decode, which is a
         * wrong answer rather than a partial one.  `out` is never written. */
        if (id == FILT_REFUSE) {
            rc = PDF_E_UNSUP;
            goto done;
        }
        if (id == FILT_NONE || id == FILT_PASS)
            break;                      /* hand the encoded bytes back as-is */

        dst->n = 0;                     /* never the buffer `in` points into */
        switch (id) {
        case FILT_FLATE: rc = pdf_filter_flate(doc, in, inlen, po, dst); break;
        case FILT_LZW:   rc = pdf_filter_lzw(doc, in, inlen, po, dst);   break;
        case FILT_A85:   rc = pdf_filter_a85(in, inlen, dst);            break;
        case FILT_AHX:   rc = pdf_filter_ahx(in, inlen, dst);            break;
        case FILT_RL:    rc = pdf_filter_rl(in, inlen, dst);             break;
        default:         rc = pdf_filter_ccitt(doc, in, inlen, po, dst); break;
        }
        if (rc < 0) goto done;

        /* 7.4.4.4: only Flate and LZW take a predictor. */
        if (id == FILT_FLATE || id == FILT_LZW) {
            rc = pdf_predictor(doc, dst, po);
            if (rc < 0) goto done;
        }

        in    = dst->p;
        inlen = dst->n;
        dst   = (dst == &a) ? &b : &a;
    }
    rc = OUT_RET(out_write(out, in, inlen));

done:
    pdf_buf_free(&a);
    pdf_buf_free(&b);
    return rc;
}
