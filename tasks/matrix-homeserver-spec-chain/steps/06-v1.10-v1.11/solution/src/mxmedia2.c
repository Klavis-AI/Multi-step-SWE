#define _POSIX_C_SOURCE 200809L
/* Authenticated media, the frozen unauthenticated endpoints and the federation
 * multipart download, from specification releases v1.10 and v1.11. */
#include "mxmedia2.h"

#include "mxapi.h"
#include "mxdb.h"
#include "mxfed.h"
#include "mxserver.h"
#include "mxutil.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#define CS1    "/_matrix/client/v1"
#define MEDIA3 "/_matrix/media/v3"
#define MEDIAR "/_matrix/media/r0"
#define FED1   "/_matrix/federation/v1"

/* "The server SHOULD provide a Content-Security-Policy header." */
#define MEDIA_CSP "sandbox; default-src 'none'; script-src 'none'; " \
                  "plugin-types application/pdf; style-src 'unsafe-inline'; " \
                  "object-src 'self';"

#define THUMB_DEFAULT 32
#define THUMB_MAX 2048
#define PREVIEW_MAX 1048576

/* ------------------------------------------------------------- schema ----- */
/* Read once at registration, before any listener is accepting. */
static int64_t g_freeze_at;

static void media2_init(void) {
    db_exec("CREATE TABLE IF NOT EXISTS media_freeze("
            "  id INTEGER PRIMARY KEY CHECK(id=1), frozen_at INTEGER NOT NULL);");
    /* Written once, the first time a server carrying this module starts: media
     * an earlier version accepted keeps working on the deprecated endpoints. */
    db_run("INSERT OR IGNORE INTO media_freeze(id,frozen_at) VALUES(1,?1)",
           "i", now_ms());
    db_int("SELECT frozen_at FROM media_freeze WHERE id=1", &g_freeze_at, "");
}

int64_t media_freeze_instant(void) {
    if (!g_freeze_at)
        db_int("SELECT frozen_at FROM media_freeze WHERE id=1", &g_freeze_at, "");
    return g_freeze_at;
}

/* ------------------------------------------------------------- PNG codec -- */
/* mxapi_media.c has the same codec, but every part of it is static there. */
typedef struct { int w, h; unsigned char *rgb; } image;

static void image_free(image *im) { if (im) { free(im->rgb); im->rgb = NULL; } }

static unsigned rd32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | p[3];
}
static void wr32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* Decodes 8-bit truecolour PNG (colour types 2 and 6) into packed RGB. */
static bool png_decode(const unsigned char *data, size_t n, image *out) {
    static const unsigned char SIG[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    if (n < 8 || memcmp(data, SIG, 8)) return false;
    size_t pos = 8;
    int w = 0, h = 0, depth = 0, color = 0;
    buf idat;
    memset(&idat, 0, sizeof idat);
    while (pos + 8 <= n) {
        unsigned len = rd32(data + pos);
        const unsigned char *tag = data + pos + 4;
        const unsigned char *body = data + pos + 8;
        if (pos + 12 + (size_t)len > n) break;
        if (!memcmp(tag, "IHDR", 4) && len >= 13) {
            w = (int)rd32(body); h = (int)rd32(body + 4);
            depth = body[8]; color = body[9];
            if (body[12] != 0) { buf_free(&idat); return false; }  /* interlaced */
        } else if (!memcmp(tag, "IDAT", 4)) {
            buf_add(&idat, body, len);
        } else if (!memcmp(tag, "IEND", 4)) {
            break;
        }
        pos += 12 + (size_t)len;
    }
    if (w <= 0 || h <= 0 || depth != 8 || (color != 2 && color != 6)) {
        buf_free(&idat);
        return false;
    }
    int bpp = color == 2 ? 3 : 4;
    uLongf raw_len = (uLongf)((size_t)h * ((size_t)w * bpp + 1));
    unsigned char *raw = malloc(raw_len ? raw_len : 1);
    if (!raw || uncompress(raw, &raw_len, (const Bytef *)idat.b,
                           (uLong)idat.n) != Z_OK) {
        buf_free(&idat); free(raw); return false;
    }
    buf_free(&idat);

    size_t stride = (size_t)w * bpp;
    unsigned char *px = calloc((size_t)h * stride, 1);
    if (!px) { free(raw); return false; }
    for (int y = 0; y < h; y++) {
        unsigned char ft = raw[(size_t)y * (stride + 1)];
        const unsigned char *src = raw + (size_t)y * (stride + 1) + 1;
        unsigned char *dst = px + (size_t)y * stride;
        const unsigned char *up = y ? px + (size_t)(y - 1) * stride : NULL;
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? dst[x - bpp] : 0;
            int b = up ? up[x] : 0;
            int c = (up && x >= (size_t)bpp) ? up[x - bpp] : 0;
            int v = src[x];
            switch (ft) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: free(raw); free(px); return false;
            }
            dst[x] = (unsigned char)v;
        }
    }
    free(raw);

    out->w = w; out->h = h;
    out->rgb = malloc((size_t)w * h * 3);
    if (!out->rgb) { free(px); return false; }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        out->rgb[i * 3 + 0] = px[i * bpp + 0];
        out->rgb[i * 3 + 1] = px[i * bpp + 1];
        out->rgb[i * 3 + 2] = px[i * bpp + 2];
    }
    free(px);
    return true;
}

static void png_chunk(buf *b, const char *tag, const unsigned char *data,
                      size_t n) {
    unsigned char hdr[8];
    wr32(hdr, (unsigned)n);
    memcpy(hdr + 4, tag, 4);
    buf_add(b, hdr, 8);
    if (n) buf_add(b, data, n);
    uLong crc = crc32(0, (const Bytef *)tag, 4);
    if (n) crc = crc32(crc, (const Bytef *)data, (uInt)n);
    unsigned char c[4];
    wr32(c, (unsigned)crc);
    buf_add(b, c, 4);
}

static unsigned char *png_encode(const image *im, size_t *out_n) {
    buf b;
    memset(&b, 0, sizeof b);
    static const unsigned char SIG[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    buf_add(&b, SIG, 8);
    unsigned char ihdr[13];
    wr32(ihdr, (unsigned)im->w);
    wr32(ihdr + 4, (unsigned)im->h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(&b, "IHDR", ihdr, sizeof ihdr);

    size_t stride = (size_t)im->w * 3;
    size_t raw_n = (size_t)im->h * (stride + 1);
    unsigned char *raw = malloc(raw_n ? raw_n : 1);
    for (int y = 0; y < im->h; y++) {
        raw[(size_t)y * (stride + 1)] = 0;
        memcpy(raw + (size_t)y * (stride + 1) + 1,
               im->rgb + (size_t)y * stride, stride);
    }
    uLongf comp_n = compressBound((uLong)raw_n);
    unsigned char *comp = malloc(comp_n);
    compress2(comp, &comp_n, raw, (uLong)raw_n, 9);
    free(raw);
    png_chunk(&b, "IDAT", comp, comp_n);
    free(comp);
    png_chunk(&b, "IEND", NULL, 0);
    return (unsigned char *)buf_take(&b, out_n);
}

static void resize_into(const image *src, image *dst) {
    dst->rgb = malloc((size_t)dst->w * dst->h * 3);
    for (int y = 0; y < dst->h; y++) {
        for (int x = 0; x < dst->w; x++) {
            int sx0 = (int)((int64_t)x * src->w / dst->w);
            int sx1 = (int)(((int64_t)x + 1) * src->w / dst->w);
            int sy0 = (int)((int64_t)y * src->h / dst->h);
            int sy1 = (int)(((int64_t)y + 1) * src->h / dst->h);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sx1 > src->w) sx1 = src->w;
            if (sy1 > src->h) sy1 = src->h;
            long acc[3] = {0, 0, 0};
            long cnt = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) {
                    const unsigned char *p =
                        src->rgb + ((size_t)sy * src->w + sx) * 3;
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
                    cnt++;
                }
            unsigned char *o = dst->rgb + ((size_t)y * dst->w + x) * 3;
            for (int k = 0; k < 3; k++) o[k] = (unsigned char)(cnt ? acc[k] / cnt : 0);
        }
    }
}

static void crop_into(const image *src, int x0, int y0, int w, int h, image *dst) {
    dst->w = w; dst->h = h;
    dst->rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        memcpy(dst->rgb + (size_t)y * w * 3,
               src->rgb + ((size_t)(y0 + y) * src->w + x0) * 3, (size_t)w * 3);
}

/* --------------------------------------------------------------- storage -- */
typedef struct {
    char *content_type;
    char *filename;
    unsigned char *data;
    size_t n;
    int64_t created_ts;
} media_item;

static void media_item_free(media_item *m) {
    if (!m) return;
    free(m->content_type);
    free(m->filename);
    free(m->data);
    memset(m, 0, sizeof *m);
}

/* "Homeservers MUST sanitise mxc:// URIs by allowing only alphanumeric
 * (A-Za-z0-9), _ and - characters in the media-id." */
static bool valid_media_id(const char *id) {
    if (!id || !*id) return false;
    for (const char *p = id; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return false;
    return true;
}

static bool media_load(const char *server, const char *media_id, media_item *out) {
    memset(out, 0, sizeof *out);
    if (!valid_media_id(media_id)) return false;
    if (server && !str_eq(server, CFG.server_name)) return false;
    char *path = db_str("SELECT path FROM media WHERE media_id=?1", "s", media_id);
    if (!path) return false;
    out->content_type = db_str("SELECT content_type FROM media WHERE media_id=?1",
                               "s", media_id);
    out->filename = db_str("SELECT filename FROM media WHERE media_id=?1",
                           "s", media_id);
    db_int("SELECT COALESCE(created_ts,0) FROM media WHERE media_id=?1",
           &out->created_ts, "s", media_id);
    FILE *f = fopen(path, "rb");
    free(path);
    if (!f) { media_item_free(out); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    out->data = malloc((size_t)sz ? (size_t)sz : 1);
    out->n = out->data ? fread(out->data, 1, (size_t)sz, f) : 0;
    fclose(f);
    return true;
}

/* --------------------------------------------------------------- headers -- */
/* "The server SHOULD additionally provide Cross-Origin-Resource-Policy:
 * cross-origin when serving content." */
static void media_headers(http_resp *w) {
    resp_set_header(w, "Cross-Origin-Resource-Policy", "cross-origin");
    resp_set_header(w, "Content-Security-Policy", MEDIA_CSP);
}

/* The content types the specification allows to be served inline; everything
 * else has to be an attachment. */
static bool inline_safe(const char *ct) {
    static const char *OK[] = {"text/css", "text/plain", "text/csv",
        "application/json", "application/ld+json", "image/jpeg", "image/gif",
        "image/png", "image/apng", "image/webp", "image/avif", "video/mp4",
        "video/webm", "video/ogg", "video/quicktime", "audio/mp4", "audio/webm",
        "audio/aac", "audio/mpeg", "audio/ogg", "audio/wave", "audio/wav",
        "audio/x-wav", "audio/x-pn-wav", "audio/flac", "audio/x-flac", NULL};
    if (!ct) return false;
    /* compare the media type only, ignoring any parameters */
    size_t n = strcspn(ct, ";");
    while (n && (ct[n - 1] == ' ' || ct[n - 1] == '\t')) n--;
    for (int i = 0; OK[i]; i++)
        if (strlen(OK[i]) == n && !strncasecmp(ct, OK[i], n)) return true;
    return false;
}

static bool ascii_filename(const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p < 0x20 || *p > 0x7e) return false;
    return true;
}

/* `inline` for the allow-listed types and `attachment` for everything else,
 * carrying the file name.  A name that is not ASCII goes in the RFC 5987
 * percent-encoded form instead of the quoted one. */
static char *disposition_value(const char *ctype, const char *filename) {
    buf cd;
    memset(&cd, 0, sizeof cd);
    buf_puts(&cd, inline_safe(ctype) ? "inline" : "attachment");
    if (filename && *filename) {
        if (ascii_filename(filename)) {
            /* RFC 6266: a name that is already a token is written bare; only
             * one that needs it is quoted. */
            bool token = true;
            for (const char *p = filename; *p && token; p++) {
                token = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                        (*p >= '0' && *p <= '9') ||
                        strchr("!#$%&'*+-.^_`|~", *p) != NULL;
            }
            if (token) {
                buf_printf(&cd, "; filename=%s", filename);
            } else {
                buf_puts(&cd, "; filename=\"");
                for (const char *p = filename; *p; p++) {
                    if (*p == '"' || *p == '\\') buf_add(&cd, "\\", 1);
                    buf_add(&cd, p, 1);
                }
                buf_puts(&cd, "\"");
            }
        } else {
            char *enc = url_encode(filename);
            buf_printf(&cd, "; filename*=utf-8''%s", enc);
            free(enc);
        }
    }
    size_t n = 0;
    return buf_take(&cd, &n);
}

static void send_media(http_resp *w, const char *ctype, const char *filename,
                       const void *data, size_t n) {
    char *cd = disposition_value(ctype, filename);
    media_headers(w);
    resp_set_header(w, "Content-Disposition", cd);
    free(cd);
    resp_bytes(w, 200, data, n, ctype && *ctype ? ctype
                                                : "application/octet-stream");
}

static void media_error(http_resp *w, int status, const char *code,
                        const char *msg) {
    media_headers(w);
    resp_error(w, status, code, msg);
}

/* ------------------------------------------------------------ thumbnails -- */
static bool thumbnail_png(const media_item *m, int tw, int th,
                          const char *method, unsigned char **png,
                          size_t *png_n) {
    image src;
    memset(&src, 0, sizeof src);
    if (!png_decode(m->data, m->n, &src)) return false;
    image out;
    memset(&out, 0, sizeof out);
    if (method && str_eq(method, "crop")) {
        double sx = (double)tw / src.w, sy = (double)th / src.h;
        double s = sx > sy ? sx : sy;
        image scaled;
        memset(&scaled, 0, sizeof scaled);
        scaled.w = (int)(src.w * s + 0.5); if (scaled.w < tw) scaled.w = tw;
        scaled.h = (int)(src.h * s + 0.5); if (scaled.h < th) scaled.h = th;
        resize_into(&src, &scaled);
        crop_into(&scaled, (scaled.w - tw) / 2, (scaled.h - th) / 2, tw, th, &out);
        image_free(&scaled);
    } else {
        /* "Servers MUST NOT upscale thumbnails under any circumstance." */
        double sx = (double)tw / src.w, sy = (double)th / src.h;
        double s = sx < sy ? sx : sy;
        if (s > 1.0) s = 1.0;
        out.w = (int)(src.w * s + 0.5); if (out.w < 1) out.w = 1;
        out.h = (int)(src.h * s + 0.5); if (out.h < 1) out.h = 1;
        resize_into(&src, &out);
    }
    image_free(&src);
    *png = png_encode(&out, png_n);
    image_free(&out);
    return *png != NULL;
}

/* Reads width, height, method and animated.  Returns false having written the
 * error. */
static bool thumbnail_params(http_req *r, http_resp *w, bool require_size,
                             int *tw, int *th, const char **method) {
    const char *qw = http_query(r, "width"), *qh = http_query(r, "height");
    if (require_size && (!qw || !qh)) {
        media_error(w, 400, "M_MISSING_PARAM",
                    "width and height are required");
        return false;
    }
    *tw = qw ? atoi(qw) : THUMB_DEFAULT;
    *th = qh ? atoi(qh) : THUMB_DEFAULT;
    if (*tw <= 0 || *th <= 0 || *tw > THUMB_MAX || *th > THUMB_MAX) {
        media_error(w, 400, "M_INVALID_PARAM", "Invalid thumbnail size");
        return false;
    }
    *method = http_query(r, "method");
    if (*method && !str_eq(*method, "crop") && !str_eq(*method, "scale")) {
        media_error(w, 400, "M_INVALID_PARAM", "Unknown thumbnail method");
        return false;
    }
    /* `animated` is honoured as far as this server can: it only produces
     * static PNG thumbnails, so it behaves as though the value were false. */
    const char *animated = http_query(r, "animated");
    if (animated && !str_eq(animated, "true") && !str_eq(animated, "false")) {
        media_error(w, 400, "M_INVALID_PARAM", "animated must be a boolean");
        return false;
    }
    return true;
}

/* ------------------------------------- authenticated client endpoints ----- */
/* GET /_matrix/client/v1/media/download/{serverName}/{mediaId}[/{fileName}] */
static void h_download_authed(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    media_item m;
    if (!media_load(r->part[0], r->part[1], &m)) {
        media_error(w, 404, "M_NOT_FOUND", "Media not found");
        principal_free(&p);
        return;
    }
    /* The three-part form replaces the stored name with the caller's. */
    const char *name = r->nparts > 2 && r->part[2] && *r->part[2]
        ? r->part[2] : m.filename;
    send_media(w, m.content_type, name, m.data, m.n);
    media_item_free(&m);
    principal_free(&p);
}

/* GET /_matrix/client/v1/media/thumbnail/{serverName}/{mediaId} */
static void h_thumbnail_authed(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    int tw = 0, th = 0;
    const char *method = NULL;
    if (!thumbnail_params(r, w, true, &tw, &th, &method)) {
        principal_free(&p);
        return;
    }
    media_item m;
    if (!media_load(r->part[0], r->part[1], &m)) {
        media_error(w, 404, "M_NOT_FOUND", "Media not found");
        principal_free(&p);
        return;
    }
    unsigned char *png = NULL;
    size_t png_n = 0;
    if (!thumbnail_png(&m, tw, th, method, &png, &png_n)) {
        media_error(w, 400, "M_UNKNOWN",
                    "Cannot generate thumbnails for the requested content");
        media_item_free(&m);
        principal_free(&p);
        return;
    }
    media_headers(w);
    resp_set_header(w, "Content-Disposition", "inline");
    resp_bytes(w, 200, png, png_n, "image/png");
    free(png);
    media_item_free(&m);
    principal_free(&p);
}

/* GET /_matrix/client/v1/media/config */
static void h_config_authed(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *b = j_obj();
    j_set(b, "m.upload.size", j_int(CFG.max_upload_bytes));
    media_headers(w);
    resp_json(w, 200, b);
    principal_free(&p);
}

/* --------------------------------------------------------- URL previews -- */
static void html_unescape(buf *out, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '&') { buf_add(out, s + i, 1); continue; }
        size_t j = i + 1;
        while (j < n && j < i + 12 && s[j] != ';') j++;
        if (j >= n || s[j] != ';') { buf_add(out, s + i, 1); continue; }
        size_t len = j - i - 1;
        const char *e = s + i + 1;
        if (len == 3 && !strncmp(e, "amp", 3)) buf_puts(out, "&");
        else if (len == 2 && !strncmp(e, "lt", 2)) buf_puts(out, "<");
        else if (len == 2 && !strncmp(e, "gt", 2)) buf_puts(out, ">");
        else if (len == 4 && !strncmp(e, "quot", 4)) buf_puts(out, "\"");
        else if (len == 4 && !strncmp(e, "apos", 4)) buf_puts(out, "'");
        else if (len == 4 && !strncmp(e, "#39", 3)) buf_puts(out, "'");
        else { buf_add(out, s + i, j - i + 1); }
        i = j;
    }
}

/* Reads one attribute value out of an HTML tag. */
static char *tag_attr(const char *tag, size_t len, const char *name) {
    size_t nlen = strlen(name);
    for (size_t i = 0; i + nlen + 1 < len; i++) {
        if (strncasecmp(tag + i, name, nlen)) continue;
        if (i && (isalnum((unsigned char)tag[i - 1]) || tag[i - 1] == '-' ||
                  tag[i - 1] == '_')) continue;
        size_t j = i + nlen;
        while (j < len && isspace((unsigned char)tag[j])) j++;
        if (j >= len || tag[j] != '=') continue;
        j++;
        while (j < len && isspace((unsigned char)tag[j])) j++;
        if (j >= len) return NULL;
        char quote = (tag[j] == '"' || tag[j] == '\'') ? tag[j] : 0;
        if (quote) j++;
        size_t k = j;
        while (k < len && (quote ? tag[k] != quote
                                 : !isspace((unsigned char)tag[k]) && tag[k] != '>'))
            k++;
        buf out;
        memset(&out, 0, sizeof out);
        html_unescape(&out, tag + j, k - j);
        size_t n = 0;
        return buf_take(&out, &n);
    }
    return NULL;
}

/* Collects the OpenGraph properties an HTML document declares. */
static json *opengraph_of(const char *html, size_t len) {
    json *out = j_obj();
    for (size_t i = 0; i + 5 < len; i++) {
        if (html[i] != '<' || strncasecmp(html + i + 1, "meta", 4)) continue;
        size_t end = i;
        while (end < len && html[end] != '>') end++;
        char *prop = tag_attr(html + i, end - i, "property");
        if (!prop) prop = tag_attr(html + i, end - i, "name");
        char *content = tag_attr(html + i, end - i, "content");
        if (prop && content && !strncmp(prop, "og:", 3) && !j_get(out, prop))
            j_set(out, prop, j_str(content));
        free(prop);
        free(content);
        i = end;
    }
    if (!j_get(out, "og:title")) {
        for (size_t i = 0; i + 7 < len; i++) {
            if (html[i] != '<' || strncasecmp(html + i + 1, "title", 5)) continue;
            size_t open = i;
            while (open < len && html[open] != '>') open++;
            if (open >= len) break;
            open++;
            size_t close = open;
            while (close + 1 < len &&
                   !(html[close] == '<' && html[close + 1] == '/')) close++;
            buf t;
            memset(&t, 0, sizeof t);
            html_unescape(&t, html + open, close - open);
            size_t n = 0;
            char *title = buf_take(&t, &n);
            if (title && *title) j_set(out, "og:title", j_str(title));
            free(title);
            break;
        }
    }
    return out;
}

/* GET /_matrix/client/v1/media/preview_url */
static void h_preview_url(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *url = http_query(r, "url");
    if (!url || !*url) {
        media_error(w, 400, "M_MISSING_PARAM", "url is required");
        principal_free(&p);
        return;
    }
    if (!str_prefix(url, "http://") && !str_prefix(url, "https://")) {
        media_error(w, 400, "M_INVALID_PARAM", "url must be http or https");
        principal_free(&p);
        return;
    }
    const char *hdr[] = {"Accept: text/html"};
    http_client_resp *res = http_get(url, hdr, 1);
    json *b = NULL;
    if (res && res->status >= 200 && res->status < 300 && res->body_len &&
        res->body_len <= PREVIEW_MAX)
        b = opengraph_of(res->body, res->body_len);
    http_client_resp_free(res);
    /* "The OpenGraph data for the URL, which may be empty." */
    media_headers(w);
    resp_json(w, 200, b ? b : j_obj());
    principal_free(&p);
}

/* ------------------------------------------------- federation endpoints --- */
/* "MUST contain a boundary delineating exactly two parts: the first has a
 * Content-Type of application/json and describes the media's metadata ...
 * the second is the bytes of the media itself." */
static void send_multipart(http_resp *w, const char *ctype, const char *filename,
                           const void *data, size_t n) {
    char *rnd = random_string(24);
    char *boundary = str_fmt("mxboundary%s", rnd);
    free(rnd);
    char *cd = disposition_value(ctype, filename);
    buf body;
    memset(&body, 0, sizeof body);
    buf_printf(&body, "--%s\r\nContent-Type: application/json\r\n\r\n{}\r\n",
               boundary);
    buf_printf(&body, "--%s\r\nContent-Type: %s\r\nContent-Disposition: %s\r\n"
                      "\r\n", boundary,
               ctype && *ctype ? ctype : "application/octet-stream", cd);
    free(cd);
    if (data && n) buf_add(&body, data, n);
    buf_printf(&body, "\r\n--%s--\r\n", boundary);

    char *ct = str_fmt("multipart/mixed; boundary=%s", boundary);
    size_t bn = 0;
    char *bytes = buf_take(&body, &bn);
    media_headers(w);
    resp_bytes(w, 200, bytes, bn, ct);
    free(bytes);
    free(ct);
    free(boundary);
}

/* GET /_matrix/federation/v1/media/download/{mediaId} */
static void h_fed_download(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    free(origin);
    media_item m;
    if (!media_load(NULL, r->part[0], &m)) {
        media_error(w, 404, "M_NOT_FOUND", "Media not found");
        return;
    }
    send_multipart(w, m.content_type, m.filename, m.data, m.n);
    media_item_free(&m);
}

/* GET /_matrix/federation/v1/media/thumbnail/{mediaId} */
static void h_fed_thumbnail(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    free(origin);
    int tw = 0, th = 0;
    const char *method = NULL;
    if (!thumbnail_params(r, w, true, &tw, &th, &method)) return;
    media_item m;
    if (!media_load(NULL, r->part[0], &m)) {
        media_error(w, 404, "M_NOT_FOUND", "Media not found");
        return;
    }
    unsigned char *png = NULL;
    size_t png_n = 0;
    if (!thumbnail_png(&m, tw, th, method, &png, &png_n)) {
        media_error(w, 400, "M_UNKNOWN",
                    "Cannot generate thumbnails for the requested content");
        media_item_free(&m);
        return;
    }
    send_multipart(w, "image/png", NULL, png, png_n);
    free(png);
    media_item_free(&m);
}

/* ------------------------------------- the frozen deprecated endpoints ---- */
static bool after_freeze(const media_item *m) {
    int64_t at = media_freeze_instant();
    return at > 0 && m->created_ts >= at;
}

static void refuse_frozen(http_resp *w) {
    media_error(w, 404, "M_NOT_FOUND",
                "Media uploaded after this server froze the unauthenticated "
                "endpoints is only available on "
                "/_matrix/client/v1/media/download");
}

static void h_legacy_download(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    media_item m;
    if (!media_load(r->part[0], r->part[1], &m)) {
        media_error(w, 404, "M_NOT_FOUND", "Media not found");
        return;
    }
    if (after_freeze(&m)) {
        media_item_free(&m);
        refuse_frozen(w);
        return;
    }
    const char *name = r->nparts > 2 && r->part[2] && *r->part[2]
        ? r->part[2] : m.filename;
    send_media(w, m.content_type, name, m.data, m.n);
    media_item_free(&m);
}

static void h_legacy_thumbnail(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    int tw = 0, th = 0;
    const char *method = NULL;
    if (!thumbnail_params(r, w, false, &tw, &th, &method)) return;
    media_item m;
    if (!media_load(r->part[0], r->part[1], &m)) {
        media_error(w, 404, "M_NOT_FOUND", "Media not found");
        return;
    }
    if (after_freeze(&m)) {
        media_item_free(&m);
        refuse_frozen(w);
        return;
    }
    unsigned char *png = NULL;
    size_t png_n = 0;
    if (!thumbnail_png(&m, tw, th, method, &png, &png_n)) {
        media_error(w, 400, "M_UNKNOWN",
                    "Cannot generate thumbnails for the requested content");
        media_item_free(&m);
        return;
    }
    media_headers(w);
    resp_set_header(w, "Content-Disposition", "inline");
    resp_bytes(w, 200, png, png_n, "image/png");
    free(png);
    media_item_free(&m);
}

/* --------------------------------------------- server support discovery -- */
/* GET /.well-known/matrix/support */
static void h_wellknown_support(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    /* "If the server does not wish to advertise support information, it should
     * respond with 404."  This deployment has no administrator to name, and
     * inventing one would be worse than saying nothing. */
    resp_bytes(w, 404, "", 0, "text/plain");
}

/* --------------------------------------------------------------- routing -- */
void api_register_media2(http_server *s) {
    spec_advertise("v1.10");
    spec_advertise("v1.11");
    media2_init();

    /* The authenticated client endpoints of release v1.11. */
    http_route(s, "GET", CS1 "/media/config", h_config_authed);
    http_route(s, "GET", CS1 "/media/download/:/:", h_download_authed);
    http_route(s, "GET", CS1 "/media/download/:/:/:", h_download_authed);
    http_route(s, "GET", CS1 "/media/thumbnail/:/:", h_thumbnail_authed);
    http_route(s, "GET", CS1 "/media/preview_url", h_preview_url);

    /* Their federation counterparts, answered as multipart/mixed. */
    http_route(s, "GET", FED1 "/media/download/:", h_fed_download);
    http_route(s, "GET", FED1 "/media/thumbnail/:", h_fed_thumbnail);

    /* The deprecated endpoints, frozen: see the note in mxmedia2.h about
     * registration order.  Uploads stay on POST /_matrix/media/v3/upload. */
    const char *bases[] = {MEDIA3, MEDIAR};
    for (int i = 0; i < 2; i++) {
        char *d2 = str_fmt("%s/download/:/:", bases[i]);
        char *d3 = str_fmt("%s/download/:/:/:", bases[i]);
        char *t2 = str_fmt("%s/thumbnail/:/:", bases[i]);
        http_route(s, "GET", d2, h_legacy_download);
        http_route(s, "GET", d3, h_legacy_download);
        http_route(s, "GET", t2, h_legacy_thumbnail);
        free(d2); free(d3); free(t2);
    }

    http_route(s, "GET", "/.well-known/matrix/support", h_wellknown_support);
}

/* ------------------------------------------------- unsigned.membership ---
 * "The server SHOULD add a `membership` property to the `unsigned` object of
 * every event it serves to a client, describing that client's own membership
 * of the room at the point of the event."  It is the *reader's* membership,
 * not the sender's, and it is the membership in force when the event happened
 * -- so an event from before an invitation reads `leave`. */
void media2_membership_hook(json *ev, json *un, const char *user_id) {
    if (!ev || !un || !user_id || !*user_id) return;
    const char *room_id = j_gets(ev, "room_id", NULL);
    char *owned = NULL;
    if (!room_id) {
        const char *eid = j_gets(ev, "event_id", NULL);
        if (eid) { owned = event_room(eid); room_id = owned; }
    }
    if (!room_id) return;
    const char *eid = j_gets(ev, "event_id", NULL);
    int64_t at = 0;
    if (eid) {
        db_int("SELECT COALESCE(stream_id, 0) FROM events WHERE event_id=?1",
               &at, "s", eid);
    }
    /* The reader's own m.room.member state at that point in the room. */
    char *text = db_str(
        "SELECT e.json FROM events e WHERE e.room_id=?1 AND "
        "e.type='m.room.member' AND e.state_key=?2 AND e.stream_id<=?3 "
        "ORDER BY e.stream_id DESC LIMIT 1", "ssi", room_id, user_id, at);
    const char *membership = "leave";
    json *member = text ? j_parse(text, strlen(text), NULL) : NULL;
    if (member) {
        const char *m = j_gets(j_geto(member, "content"), "membership", NULL);
        if (m) membership = m;
    }
    j_set(un, "membership", j_str(membership));
    j_free(member);
    free(text);
    free(owned);
}

/* ------------------------------------------------------------- erasure ---
 * "The user may also be erased, in which case the server should remove their
 * content."  What a member could already see, they keep seeing: erasure hides
 * the content from anyone who was not already in the room when it was asked
 * for. */
void media2_erasure_record(const char *user_id) {
    if (!user_id || !*user_id) return;
    db_exec("CREATE TABLE IF NOT EXISTS erased_users("
            "  user_id TEXT PRIMARY KEY, stream_id INTEGER);");
    db_run("INSERT OR REPLACE INTO erased_users(user_id,stream_id) "
           "VALUES(?1,?2)", "si", user_id, stream_current());
}

bool media2_erased_from(const json *ev, const char *viewer) {
    if (!ev || !viewer || !*viewer) return false;
    const char *sender = j_gets((json *)ev, "sender", NULL);
    if (!sender || str_eq(sender, viewer)) return false;
    int64_t erased_at = 0;
    if (!db_int("SELECT stream_id FROM erased_users WHERE user_id=?1",
                &erased_at, "s", sender))
        return false;
    const char *room_id = j_gets((json *)ev, "room_id", NULL);
    char *owned = NULL;
    if (!room_id) {
        const char *eid = j_gets((json *)ev, "event_id", NULL);
        if (eid) { owned = event_room(eid); room_id = owned; }
    }
    if (!room_id) return false;
    /* When did this viewer first join?  A member from before the erasure keeps
     * what they could already see. */
    int64_t joined_at = 0;
    bool have = db_int("SELECT MIN(stream_id) FROM events WHERE room_id=?1 AND "
                       "type='m.room.member' AND state_key=?2", &joined_at,
                       "ss", room_id, viewer);
    free(owned);
    if (!have || joined_at <= 0) return true;
    return joined_at > erased_at;
}
