/*
 * End-to-end sws_scale driver, used for the task's end-to-end output and
 * non-regression checks.
 *
 * Prints, per configuration, one line:
 *     <name> <digest> <best_ms>
 * where <digest> is a 64-bit FNV-1a over every destination plane of every
 * timed repetition (so a timed run is also a checked run), and <best_ms> is
 * the best wall-clock time over REPS repetitions after one warmup.
 *
 * Input frames are synthesised from a fixed seed, so no fixture files.
 *
 * Usage: measure_sws <config-index|all> [reps]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>

static uint64_t h;

static void mix(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint64_t x = h;
    while (n--) { x = (x ^ *b++) * 0x100000001b3ULL; }
    h = x;
}

/* Deterministic source planes. */
static void fill(uint8_t *p, int stride, int w, int hgt, int bytes_pp, uint32_t seed)
{
    uint32_t s = seed | 1u;
    for (int y = 0; y < hgt; y++) {
        uint8_t *row = p + (size_t)y * stride;
        for (int x = 0; x < w * bytes_pp; x++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            row[x] = (uint8_t)(s >> 7);
        }
    }
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

struct cfg {
    const char *name;
    int         src_w, src_h, dst_w, dst_h;
    enum AVPixelFormat src_fmt, dst_fmt;
    int         flags;
};

static const struct cfg cfgs[] = {
    { "rgb24_to_yuv420p_downscale",  1280,  720,  640,  360, AV_PIX_FMT_RGB24,      AV_PIX_FMT_YUV420P,     SWS_BILINEAR },
    { "yuv420p_to_rgb24_upscale",     960,  540, 1920, 1080, AV_PIX_FMT_YUV420P,    AV_PIX_FMT_RGB24,       SWS_BILINEAR },
    { "yuv420p_to_yuv420p_scale",    1920, 1080,  854,  480, AV_PIX_FMT_YUV420P,    AV_PIX_FMT_YUV420P,     SWS_BICUBIC  },
    { "yuv420p_to_yuv420p10le",      1280,  720, 1280,  720, AV_PIX_FMT_YUV420P,    AV_PIX_FMT_YUV420P10LE, SWS_BICUBIC },
    { "rgba_to_yuv420p",             1280,  720,  720,  576, AV_PIX_FMT_RGBA,       AV_PIX_FMT_YUV420P,     SWS_BILINEAR },
    { "yuv444p16le_to_yuv420p",      1280,  720, 1280,  720, AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUV420P,    SWS_BILINEAR },
    { "yuv420p_to_rgb565",           1280,  720,  640,  360, AV_PIX_FMT_YUV420P,    AV_PIX_FMT_RGB565LE,    SWS_BILINEAR },
    { "yuv420p_to_bgr24_unscaled",   1024,  768, 1024,  768, AV_PIX_FMT_YUV420P,    AV_PIX_FMT_BGR24,       SWS_BILINEAR },
    { "nv12_to_yuv420p",             1280,  720,  640,  360, AV_PIX_FMT_NV12,       AV_PIX_FMT_YUV420P,     SWS_BILINEAR },
    { "gbrp16le_to_yuv420p10le",      640,  480, 1280,  960, AV_PIX_FMT_GBRP16LE,   AV_PIX_FMT_YUV420P10LE, SWS_BICUBIC  },
};
#define NCFG (int)(sizeof cfgs / sizeof cfgs[0])

static int run_one(int ci, int reps, uint64_t *out_digest, double *out_ms)
{
    const struct cfg *c = &cfgs[ci];
    struct SwsContext *sws;
    uint8_t *src[4] = {0}, *dst[4] = {0};
    int      srcs[4] = {0}, dsts[4] = {0};
    int      ret = 1;
    double   best = 1e18;
    uint64_t digest = 0;

    if (av_image_alloc(src, srcs, c->src_w, c->src_h, c->src_fmt, 64) < 0) return 1;
    if (av_image_alloc(dst, dsts, c->dst_w, c->dst_h, c->dst_fmt, 64) < 0) goto done;
    {
        const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(c->src_fmt);
        for (int i = 0; i < 4 && src[i]; i++) {
            int sub = (i == 1 || i == 2);
            int w  = AV_CEIL_RSHIFT(c->src_w, sub ? d->log2_chroma_w : 0);
            int hh = AV_CEIL_RSHIFT(c->src_h, sub ? d->log2_chroma_h : 0);
            fill(src[i], srcs[i], w, hh, d->comp[i].depth > 8 ? 2 : 1,
                 0x1234567u + i * 7919u);
        }
    }

    sws = sws_getContext(c->src_w, c->src_h, c->src_fmt,
                         c->dst_w, c->dst_h, c->dst_fmt, c->flags, NULL, NULL, NULL);
    if (!sws) goto done;

    for (int rep = -1; rep < reps; rep++) {
        double t0 = now_ms();
        int r = sws_scale(sws, (const uint8_t *const *)src, srcs, 0, c->src_h, dst, dsts);
        double dt = now_ms() - t0;
        if (r <= 0) goto done_sws;
        const AVPixFmtDescriptor *dd = av_pix_fmt_desc_get(c->dst_fmt);
        h = 0xcbf29ce484222325ULL;
        mix(&c->dst_w, sizeof c->dst_w);
        mix(&c->dst_h, sizeof c->dst_h);
        for (int i = 0; i < 4 && dst[i]; i++) {
            int sub = (i == 1 || i == 2);
            int w  = AV_CEIL_RSHIFT(c->dst_w, sub ? dd->log2_chroma_w : 0) *
                     (dd->comp[i].depth > 8 ? 2 : 1);
            int hh = AV_CEIL_RSHIFT(c->dst_h, sub ? dd->log2_chroma_h : 0);
            for (int y = 0; y < hh; y++) mix(dst[i] + (size_t)y * dsts[i], w);
        }
        if (rep < 0) { digest = h; continue; }
        if (h != digest) { fprintf(stderr, "%s: nondeterministic\n", c->name); goto done_sws; }
        if (dt < best) best = dt;
    }
    ret = 0;
done_sws:
    sws_freeContext(sws);
done:
    av_freep(&src[0]); av_freep(&dst[0]);
    if (!ret) { *out_digest = digest; *out_ms = best; }
    return ret;
}

/* Dump the destination planes of one configuration, raw, for a cross-build
 * comparison: measure_sws <config> dump <outfile>. */
static int dump_one(int ci, const char *path)
{
    const struct cfg *c = &cfgs[ci];
    struct SwsContext *sws;
    uint8_t *src[4] = {0}, *dst[4] = {0};
    int srcs[4] = {0}, dsts[4] = {0};
    FILE *f = NULL;
    int ret = 1;
    const AVPixFmtDescriptor *d, *dd;

    if (av_image_alloc(src, srcs, c->src_w, c->src_h, c->src_fmt, 64) < 0) return 1;
    if (av_image_alloc(dst, dsts, c->dst_w, c->dst_h, c->dst_fmt, 64) < 0) goto done;
    d = av_pix_fmt_desc_get(c->src_fmt);
    for (int i = 0; i < 4 && src[i]; i++) {
        int sub = (i == 1 || i == 2);
        int w  = AV_CEIL_RSHIFT(c->src_w, sub ? d->log2_chroma_w : 0);
        int hh = AV_CEIL_RSHIFT(c->src_h, sub ? d->log2_chroma_h : 0);
        fill(src[i], srcs[i], w, hh, d->comp[i].depth > 8 ? 2 : 1, 0x1234567u + i * 7919u);
    }
    sws = sws_getContext(c->src_w, c->src_h, c->src_fmt,
                         c->dst_w, c->dst_h, c->dst_fmt, c->flags, NULL, NULL, NULL);
    if (!sws) goto done;
    if (sws_scale(sws, (const uint8_t *const *)src, srcs, 0, c->src_h, dst, dsts) <= 0)
        goto done_sws;
    f = fopen(path, "wb");
    if (!f) goto done_sws;
    dd = av_pix_fmt_desc_get(c->dst_fmt);
    for (int i = 0; i < 4 && dst[i]; i++) {
        int sub = (i == 1 || i == 2);
        int w  = AV_CEIL_RSHIFT(c->dst_w, sub ? dd->log2_chroma_w : 0) *
                 (dd->comp[i].depth > 8 ? 2 : 1);
        int hh = AV_CEIL_RSHIFT(c->dst_h, sub ? dd->log2_chroma_h : 0);
        for (int y = 0; y < hh; y++)
            fwrite(dst[i] + (size_t)y * dsts[i], 1, w, f);
    }
    ret = 0;
done_sws:
    if (f) fclose(f);
    sws_freeContext(sws);
done:
    av_freep(&src[0]); av_freep(&dst[0]);
    return ret;
}

int main(int argc, char **argv)
{
    int idx = -1, reps = 3;
    if (argc < 2) { fprintf(stderr, "usage: %s <config|all> [reps] | <config> dump <outfile>\n", argv[0]); return 2; }
    for (int i = 0; i < NCFG; i++) if (!strcmp(argv[1], cfgs[i].name)) idx = i;
    if (argc >= 4 && !strcmp(argv[2], "dump")) {
        if (idx < 0) { fprintf(stderr, "unknown config %s\n", argv[1]); return 2; }
        return dump_one(idx, argv[3]);
    }
    if (strcmp(argv[1], "all") && idx < 0) {
        for (int i = 0; i < NCFG; i++) fprintf(stderr, "  %d %s\n", i, cfgs[i].name);
        return 2;
    }
    if (argc > 2) reps = atoi(argv[2]);
    for (int i = 0; i < NCFG; i++) {
        if (idx >= 0 && i != idx) continue;
        uint64_t d; double ms;
        if (run_one(i, reps, &d, &ms)) { fprintf(stderr, "%s: FAILED\n", cfgs[i].name); return 1; }
        printf("%-32s %016llx %10.3f\n", cfgs[i].name, (unsigned long long)d, ms);
        fflush(stdout);
    }
    return 0;
}
