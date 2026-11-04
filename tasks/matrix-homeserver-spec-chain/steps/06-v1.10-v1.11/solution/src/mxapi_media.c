#define _POSIX_C_SOURCE 200809L
/* Content repository: upload, download, thumbnail, config. */
#include "mxapi.h"
#include "mxdb.h"
#include "mxutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#define MEDIA3 "/_matrix/media/v3"
#define MEDIAR "/_matrix/media/r0"

/* ------------------------------------------------------------- PNG codec */
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
    buf idat = {0};
    while (pos + 8 <= n) {
        unsigned len = rd32(data + pos);
        const unsigned char *tag = data + pos + 4;
        const unsigned char *body = data + pos + 8;
        if (pos + 12 + len > n) break;
        if (!memcmp(tag, "IHDR", 4) && len >= 13) {
            w = (int)rd32(body); h = (int)rd32(body + 4);
            depth = body[8]; color = body[9];
            if (body[12] != 0) { buf_free(&idat); return false; }  /* interlaced */
        } else if (!memcmp(tag, "IDAT", 4)) {
            buf_add(&idat, body, len);
        } else if (!memcmp(tag, "IEND", 4)) {
            break;
        }
        pos += 12 + len;
    }
    if (w <= 0 || h <= 0 || depth != 8 || (color != 2 && color != 6)) {
        buf_free(&idat);
        return false;
    }
    int bpp = color == 2 ? 3 : 4;
    uLongf raw_len = (uLongf)((size_t)h * ((size_t)w * bpp + 1));
    unsigned char *raw = malloc(raw_len ? raw_len : 1);
    if (uncompress(raw, &raw_len, (const Bytef *)idat.b, (uLong)idat.n) != Z_OK) {
        buf_free(&idat); free(raw); return false;
    }
    buf_free(&idat);

    size_t stride = (size_t)w * bpp;
    unsigned char *px = calloc((size_t)h * stride, 1);
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
    for (size_t i = 0; i < (size_t)w * h; i++) {
        out->rgb[i * 3 + 0] = px[i * bpp + 0];
        out->rgb[i * 3 + 1] = px[i * bpp + 1];
        out->rgb[i * 3 + 2] = px[i * bpp + 2];
    }
    free(px);
    return true;
}

static void png_chunk(buf *b, const char *tag, const unsigned char *data, size_t n) {
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
    buf b = {0};
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
        memcpy(raw + (size_t)y * (stride + 1) + 1, im->rgb + (size_t)y * stride, stride);
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

/* Box-average resampling, then a centre crop for method=crop. */
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
                    const unsigned char *p = src->rgb + ((size_t)sy * src->w + sx) * 3;
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

/* --------------------------------------------------------------- storage */
static char *media_file_path(const char *media_id) {
    return str_fmt("%s/%s", CFG.media_base_path, media_id);
}

static void h_upload(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if ((int64_t)r->body_len > CFG.max_upload_bytes) {
        json *b = j_obj();
        j_set(b, "errcode", j_str("M_TOO_LARGE"));
        j_set(b, "error", j_str("Upload request body is too large"));
        resp_json(w, 413, b);
        principal_free(&p);
        return;
    }
    const char *ct = http_header(r, "content-type");
    const char *fn = http_query(r, "filename");
    char *media_id = random_string(24);
    char *path = media_file_path(media_id);
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(path); free(media_id);
        resp_error(w, 500, "M_UNKNOWN", "Cannot store media");
        principal_free(&p);
        return;
    }
    fwrite(r->body, 1, r->body_len, f);
    fclose(f);
    db_run("INSERT INTO media(media_id,user_id,content_type,filename,size,path,"
           "created_ts) VALUES(?1,?2,?3,?4,?5,?6,?7)", "ssssisi", media_id,
           p.user_id, ct ? ct : "application/octet-stream", fn,
           (int64_t)r->body_len, path, now_ms());
    json *b = j_obj();
    char *uri = str_fmt("mxc://%s/%s", CFG.server_name, media_id);
    j_set(b, "content_uri", j_str(uri));
    free(uri); free(path); free(media_id);
    resp_json(w, 200, b);
    principal_free(&p);
}

static bool load_media(const char *server, const char *media_id, char **ctype,
                       char **filename, unsigned char **data, size_t *n) {
    if (!str_eq(server, CFG.server_name)) return false;
    char *path = db_str("SELECT path FROM media WHERE media_id=?1", "s", media_id);
    if (!path) return false;
    *ctype = db_str("SELECT content_type FROM media WHERE media_id=?1", "s", media_id);
    *filename = db_str("SELECT filename FROM media WHERE media_id=?1", "s", media_id);
    FILE *f = fopen(path, "rb");
    free(path);
    if (!f) { free(*ctype); free(*filename); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc((size_t)sz ? (size_t)sz : 1);
    *n = fread(*data, 1, (size_t)sz, f);
    fclose(f);
    return true;
}

/* The specification's list of content types that may be served inline. */
static bool inline_safe(const char *ct) {
    static const char *OK[] = {"text/css", "text/plain", "text/csv",
        "application/json", "application/ld+json", "image/jpeg", "image/gif",
        "image/png", "image/apng", "image/webp", "image/avif", "video/mp4",
        "video/webm", "video/ogg", "video/quicktime", "audio/mp4", "audio/webm",
        "audio/aac", "audio/mpeg", "audio/ogg", "audio/wave", "audio/wav",
        "audio/x-wav", "audio/x-pn-wav", "audio/flac", "audio/x-flac", NULL};
    for (int i = 0; OK[i]; i++) if (str_eq(ct, OK[i])) return true;
    return false;
}

static void send_media(http_resp *w, const char *ctype, const char *filename,
                       const unsigned char *data, size_t n) {
    buf cd = {0};
    buf_puts(&cd, inline_safe(ctype) ? "inline" : "attachment");
    if (filename && *filename) {
        buf_printf(&cd, "; filename=%s", filename);
    }
    resp_set_header(w, "Content-Disposition", cd.b ? cd.b : "attachment");
    buf_free(&cd);
    /* "The server SHOULD additionally provide `Cross-Origin-Resource-Policy:
     * cross-origin`" on content it serves, so that a browser client can load
     * it from a different origin. */
    resp_set_header(w, "Cross-Origin-Resource-Policy", "cross-origin");
    resp_bytes(w, 200, data, n, ctype ? ctype : "application/octet-stream");
}

static void h_download(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *ctype = NULL, *filename = NULL;
    unsigned char *data = NULL;
    size_t n = 0;
    if (!load_media(r->part[0], r->part[1], &ctype, &filename, &data, &n)) {
        resp_set_header(w, "Cross-Origin-Resource-Policy", "cross-origin");
        resp_error(w, 404, "M_NOT_FOUND", "Media not found");
        return;
    }
    const char *want = r->nparts > 2 && r->part[2] && *r->part[2]
        ? r->part[2] : filename;
    send_media(w, ctype, want, data, n);
    free(ctype); free(filename); free(data);
}

static void h_thumbnail(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *ctype = NULL, *filename = NULL;
    unsigned char *data = NULL;
    size_t n = 0;
    if (!load_media(r->part[0], r->part[1], &ctype, &filename, &data, &n)) {
        resp_set_header(w, "Cross-Origin-Resource-Policy", "cross-origin");
        resp_error(w, 404, "M_NOT_FOUND", "Media not found");
        return;
    }
    const char *qw = http_query(r, "width"), *qh = http_query(r, "height");
    const char *method = http_query(r, "method");
    if (!qw || !qh || (method && !str_eq(method, "crop") && !str_eq(method, "scale"))) {
        free(ctype); free(filename); free(data);
        resp_error(w, 400, "M_INVALID_PARAM",
                   "width and height are required and method must be crop or scale");
        return;
    }
    int tw = atoi(qw), th = atoi(qh);
    if (tw <= 0 || th <= 0) {
        free(ctype); free(filename); free(data);
        resp_error(w, 400, "M_INVALID_PARAM", "Invalid thumbnail size");
        return;
    }
    image src = {0};
    if (!png_decode(data, n, &src)) {
        free(ctype); free(filename); free(data);
        resp_error(w, 400, "M_UNKNOWN", "Cannot thumbnail this content type");
        return;
    }
    free(data);
    image out = {0};
    if (str_eq(method, "crop")) {
        /* scale so the target box is covered, then centre-crop to the box */
        double sx = (double)tw / src.w, sy = (double)th / src.h;
        double s = sx > sy ? sx : sy;
        image scaled = {0};
        scaled.w = (int)(src.w * s + 0.5); if (scaled.w < tw) scaled.w = tw;
        scaled.h = (int)(src.h * s + 0.5); if (scaled.h < th) scaled.h = th;
        resize_into(&src, &scaled);
        crop_into(&scaled, (scaled.w - tw) / 2, (scaled.h - th) / 2, tw, th, &out);
        image_free(&scaled);
    } else {
        double sx = (double)tw / src.w, sy = (double)th / src.h;
        double s = sx < sy ? sx : sy;
        if (s > 1.0) s = 1.0;
        out.w = (int)(src.w * s + 0.5); if (out.w < 1) out.w = 1;
        out.h = (int)(src.h * s + 0.5); if (out.h < 1) out.h = 1;
        resize_into(&src, &out);
    }
    image_free(&src);
    size_t pn = 0;
    unsigned char *png = png_encode(&out, &pn);
    image_free(&out);
    resp_set_header(w, "Content-Disposition", "inline");
    resp_set_header(w, "Cross-Origin-Resource-Policy", "cross-origin");
    resp_bytes(w, 200, png, pn, "image/png");
    free(png);
    free(ctype); free(filename);
}

static void h_media_config(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *b = j_obj();
    j_set(b, "m.upload.size", j_int(CFG.max_upload_bytes));
    resp_json(w, 200, b);
    principal_free(&p);
}


/* ------------------------------------------------- asynchronous upload ---
 * "Creates a new `mxc://` URI, independently of the content being uploaded."
 * The identifier is reserved now and filled later by `PUT .../upload/{server}/
 * {mediaId}`; until it is filled the row carries no path. */
static void h_media_create(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *media_id = random_string(24);
    int64_t expires = now_ms() + 24 * 60 * 60 * 1000;
    db_run("INSERT INTO media(media_id,user_id,content_type,filename,size,path,"
           "created_ts,unused_expires_at) VALUES(?1,?2,NULL,NULL,0,NULL,?3,?4)",
           "ssii", media_id, p.user_id, now_ms(), expires);
    json *b = j_obj();
    char *uri = str_fmt("mxc://%s/%s", CFG.server_name, media_id);
    j_set(b, "content_uri", j_str(uri));
    j_set(b, "unused_expires_at", j_int(expires));
    free(uri); free(media_id);
    resp_json(w, 200, b);
    principal_free(&p);
}

/* PUT /_matrix/media/v3/upload/{serverName}/{mediaId} */
static void h_upload_into(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *server = r->part[0], *media_id = r->part[1];
    if (!str_eq(server, CFG.server_name)) {
        resp_error(w, 400, "M_INVALID_PARAM",
                   "Cannot upload into another server's media repository");
        principal_free(&p);
        return;
    }
    char *owner = db_str("SELECT user_id FROM media WHERE media_id=?1", "s",
                         media_id);
    if (!owner) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown media identifier");
        principal_free(&p);
        return;
    }
    char *existing = db_str("SELECT path FROM media WHERE media_id=?1", "s",
                            media_id);
    if (existing) {
        free(existing); free(owner);
        resp_error(w, 409, "M_CANNOT_OVERWRITE_MEDIA",
                   "This media identifier has already been filled");
        principal_free(&p);
        return;
    }
    if (!str_eq(owner, p.user_id)) {
        free(owner);
        resp_error(w, 403, "M_FORBIDDEN",
                   "Only the user who reserved this identifier may fill it");
        principal_free(&p);
        return;
    }
    free(owner);
    if ((int64_t)r->body_len > CFG.max_upload_bytes) {
        json *b = j_obj();
        j_set(b, "errcode", j_str("M_TOO_LARGE"));
        j_set(b, "error", j_str("Upload request body is too large"));
        resp_json(w, 413, b);
        principal_free(&p);
        return;
    }
    char *path = media_file_path(media_id);
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(path);
        resp_error(w, 500, "M_UNKNOWN", "Cannot store media");
        principal_free(&p);
        return;
    }
    fwrite(r->body, 1, r->body_len, f);
    fclose(f);
    const char *ct = http_header(r, "content-type");
    const char *fn = http_query(r, "filename");
    db_run("UPDATE media SET content_type=?2, filename=?3, size=?4, path=?5, "
           "unused_expires_at=NULL WHERE media_id=?1", "sssis", media_id,
           ct ? ct : "application/octet-stream", fn, (int64_t)r->body_len, path);
    free(path);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

void api_register_media(http_server *s) {
    const char *bases[] = {MEDIA3, MEDIAR};
    for (int i = 0; i < 2; i++) {
        char *p1 = str_fmt("%s/upload", bases[i]);
        char *p2 = str_fmt("%s/download/:/:", bases[i]);
        char *p3 = str_fmt("%s/download/:/:/:", bases[i]);
        char *p4 = str_fmt("%s/thumbnail/:/:", bases[i]);
        char *p5 = str_fmt("%s/config", bases[i]);
        http_route(s, "POST", p1, h_upload);
        http_route(s, "GET", p2, h_download);
        http_route(s, "GET", p3, h_download);
        http_route(s, "GET", p4, h_thumbnail);
        http_route(s, "GET", p5, h_media_config);
        char *p6 = str_fmt("%s/upload/:/:", bases[i]);
        http_route(s, "PUT", p6, h_upload_into);
        free(p1); free(p2); free(p3); free(p4); free(p5); free(p6);
    }
    /* The identifier is minted on the v1 path, which is where it stays. */
    http_route(s, "POST", "/_matrix/media/v1/create", h_media_create);
}
