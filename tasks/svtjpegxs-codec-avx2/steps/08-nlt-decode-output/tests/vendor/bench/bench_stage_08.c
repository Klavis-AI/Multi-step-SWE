/*
 * codecbench, ladder step 8 -- NLT inverse and output format conversion.  Each
 * slot is reached through the rtcd pointer installed for it:
 *   linear_output_scaling_8bit           (pi, comps, Bw = 20, depth = 8, out)
 *   linear_output_scaling_16bit          (pi, comps, Bw = 20, depth = 10, out)
 *   linear_output_scaling_8bit_line      (in, Bw = 20, depth = 8, out8, w)
 *   linear_output_scaling_16bit_line     (in, Bw = 20, depth = 10, out16, w)
 *   linear_output_scaling_16bit_line_msb (in, Bw = 20, depth = 10, out16, w)
 *
 * Configuration is the one a real decoder runs: 1080p 4:2:2 (luma 1920x1080,
 * chroma 960x1080), Bw = 20, coefficient groups of 4 and significance groups
 * of 8, decom_h = 5, decom_v = 2, 16-line slices; the plane slots' pi_t is
 * built with pi_compute(), as the decoder builds it.
 *
 * Input domain: the inverse-DWT output, in the centred Bw-bit domain the
 * forward NLT produced -- (pixel << (Bw - depth)) - (1 << (Bw - 1)), i.e.
 * |v| < 2^19 for Bw = 20 -- plus the overshoot quantisation can add (2^14).
 *
 * Widths are 1920, 1919, 1904 and 960; every output byte of every plane is hashed.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "Definitions.h"
#include "Pi.h"
#include "decoder_dsp_rtcd.h"

/* ------------------------------------------------------------- geometry --- */
#define S8_LUMA_W 1920
#define S8_H      1080
#define S8_CH_W   (S8_LUMA_W / 2)
#define S8_PAD    64 /* bytes of slack past the end of every plane buffer */
#define S8_PAD_I  (S8_PAD / 4)

#define S8_BW      20
#define S8_DEPTH8  8
#define S8_DEPTH16 10

#define S8_COMP_NUM 3

#define S8_DECOM_H 5
#define S8_DECOM_V 2
#define S8_SLICE_H 16 /* encoder default; must be a multiple of 2^decom_v */

/* Correctness rounds walk the width around the realistic one, chroma following
 * at half width, so aligned, unaligned and short lines all occur. */
static const uint32_t s8_widths[] = {1920, 1919, 1904, 960};
#define S8_NW ((uint32_t)(sizeof(s8_widths) / sizeof(s8_widths[0])))

static uint32_t s8_luma_width(int round) {
    return (round < 0) ? s8_widths[0] : s8_widths[(uint32_t)round % S8_NW];
}

static const uint64_t S8_H0 = 0xCBF29CE484222325ULL;

/* ------------------------------------------------------- shared inputs --- */

/* Centred Bw-bit coefficients as the inverse DWT leaves them, plus the small
 * overshoot quantisation can add. */
#define S8_OVER (1 << 14)

static void s8_fill_coeffs(int32_t *dst, size_t n, uint64_t *s) {
    const int32_t hi = (int32_t)((1 << (S8_BW - 1)) + S8_OVER);
    for (size_t i = 0; i < n; i++)
        dst[i] = bench_rand_range(s, -hi, hi);
}

/* Same picture description the decoder builds in svt_jpeg_xs_dec_init_common(),
 * from the picture header fields an encoder-produced 1080p 4:2:2 stream carries. */
static void s8_build_pi(pi_t *pi, uint32_t luma_w) {
    uint32_t sx[MAX_COMPONENTS_NUM] = {1, 2, 2, 1};
    uint32_t sy[MAX_COMPONENTS_NUM] = {1, 1, 1, 1};

    memset(pi, 0, sizeof(*pi));
    if (pi_compute(pi, 0 /*decoder*/, S8_COMP_NUM, GROUP_SIZE, SIGNIFICANCE_GROUP_SIZE, luma_w, S8_H, S8_DECOM_H,
                   S8_DECOM_V, 0 /*Sd*/, sx, sy, 0 /*Cw, the encoder's default*/, S8_SLICE_H) != SvtJxsErrorNone) {
        /* Unreachable for the parameters above (the self-test exercises every
         * width): leave an empty picture behind so the case measures nothing
         * rather than reading a half-built pi_t. */
        memset(pi, 0, sizeof(*pi));
    }
}

/* ------------------------------------------ linear_output_scaling_8/16bit --- */
/* One context covers both plane slots: only the output plane type differs, the
 * coefficients and the picture description are the same. */
typedef struct {
    pi_t pi;
    int32_t *comps[MAX_COMPONENTS_NUM];
    svt_jpeg_xs_image_buffer_t out;
    int32_t y[S8_LUMA_W * S8_H + S8_PAD_I];
    int32_t cb[S8_CH_W * S8_H + S8_PAD_I];
    int32_t cr[S8_CH_W * S8_H + S8_PAD_I];
    union {
        struct {
            uint8_t y[S8_LUMA_W * S8_H + S8_PAD];
            uint8_t c[2][S8_CH_W * S8_H + S8_PAD];
        } b8;
        struct {
            uint16_t y[S8_LUMA_W * S8_H + S8_PAD];
            uint16_t c[2][S8_CH_W * S8_H + S8_PAD];
        } b16;
    } plane;
} s8_plane_ctx;

static void s8_plane_setup(void *v, uint64_t seed, int round, int sixteen) {
    s8_plane_ctx *c = v;
    uint64_t s = seed;
    int32_t *coeffs[S8_COMP_NUM] = {c->y, c->cb, c->cr};

    s8_build_pi(&c->pi, s8_luma_width(round));

    for (uint32_t i = 0; i < S8_COMP_NUM; i++) {
        uint32_t w = c->pi.components[i].width;
        uint32_t h = c->pi.components[i].height;
        c->comps[i] = coeffs[i];
        s8_fill_coeffs(coeffs[i], (size_t)w * h, &s);
        if (i == 0) {
            c->out.data_yuv[0] = sixteen ? (void *)c->plane.b16.y : (void *)c->plane.b8.y;
        }
        else {
            c->out.data_yuv[i] = sixteen ? (void *)c->plane.b16.c[i - 1] : (void *)c->plane.b8.c[i - 1];
        }
        /* The decoder's own allocation uses stride == component width. */
        c->out.stride[i] = (i == 0) ? S8_LUMA_W : S8_CH_W;
    }
    c->out.alloc_size[0] = sixteen ? (uint32_t)sizeof(c->plane.b16.y) : (uint32_t)sizeof(c->plane.b8.y);
    c->out.alloc_size[1] = c->out.alloc_size[2] = sixteen ? (uint32_t)sizeof(c->plane.b16.c[0])
                                                         : (uint32_t)sizeof(c->plane.b8.c[0]);
    c->out.release_ctx_ptr = NULL;
    c->out.ready_to_release = 0;
}

static void s8_plane_setup8(void *v, uint64_t seed, int round) {
    s8_plane_setup(v, seed, round, 0);
}

static void s8_plane_setup16(void *v, uint64_t seed, int round) {
    s8_plane_setup(v, seed, round, 1);
}

static void s8_plane_call8(void *v) {
    s8_plane_ctx *c = v;
    linear_output_scaling_8bit(&c->pi, c->comps, S8_BW, S8_DEPTH8, &c->out);
}

static void s8_plane_call16(void *v) {
    s8_plane_ctx *c = v;
    linear_output_scaling_16bit(&c->pi, c->comps, S8_BW, S8_DEPTH16, &c->out);
}

/* Everything the call writes: every row of every plane, stride included. */
static void s8_plane_hash8(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    s8_plane_ctx *c = v;
    uint64_t h = S8_H0;
    for (uint32_t i = 0; i < c->pi.comps_num; i++)
        h = bench_fnv1a(c->out.data_yuv[i], (size_t)c->out.stride[i] * c->pi.components[i].height, h);
    bench_hash_finish(h, out);
}

static void s8_plane_hash16(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    s8_plane_ctx *c = v;
    uint64_t h = S8_H0;
    for (uint32_t i = 0; i < c->pi.comps_num; i++)
        h = bench_fnv1a(c->out.data_yuv[i], (size_t)c->out.stride[i] * c->pi.components[i].height * sizeof(uint16_t), h);
    bench_hash_finish(h, out);
}

/* --------------------------- linear_output_scaling_*_line[_msb] --- */
/* The decoder calls these once per component line of the inverse DWT output;
 * the domain is the one above, one line of luma at a time. */
typedef struct {
    uint32_t w;
    int32_t in[S8_LUMA_W + S8_PAD_I];
    uint8_t out8[S8_LUMA_W + S8_PAD];
    uint16_t out16[S8_LUMA_W + S8_PAD];
} s8_line_ctx;

static void s8_line_setup(void *v, uint64_t seed, int round) {
    s8_line_ctx *c = v;
    uint64_t s = seed;
    c->w = s8_luma_width(round);
    s8_fill_coeffs(c->in, c->w, &s);
}

static void s8_line_call8(void *v) {
    s8_line_ctx *c = v;
    linear_output_scaling_8bit_line(c->in, S8_BW, S8_DEPTH8, c->out8, c->w);
}

static void s8_line_hash8(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    s8_line_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->out8, c->w, S8_H0), out);
}

static void s8_line_call16(void *v) {
    s8_line_ctx *c = v;
    linear_output_scaling_16bit_line(c->in, S8_BW, S8_DEPTH16, c->out16, c->w);
}

static void s8_line_hash16(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    s8_line_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->out16, (size_t)c->w * sizeof(uint16_t), S8_H0), out);
}

static void s8_line_msb_call(void *v) {
    s8_line_ctx *c = v;
    linear_output_scaling_16bit_line_msb(c->in, S8_BW, S8_DEPTH16, c->out16, c->w);
}

static const bench_case step8_cases[] = {
    {"linear_output_scaling_8bit", sizeof(s8_plane_ctx), s8_plane_setup8, s8_plane_call8, s8_plane_hash8},
    {"linear_output_scaling_16bit", sizeof(s8_plane_ctx), s8_plane_setup16, s8_plane_call16, s8_plane_hash16},
    {"linear_output_scaling_8bit_line", sizeof(s8_line_ctx), s8_line_setup, s8_line_call8, s8_line_hash8},
    {"linear_output_scaling_16bit_line", sizeof(s8_line_ctx), s8_line_setup, s8_line_call16, s8_line_hash16},
    {"linear_output_scaling_16bit_line_msb", sizeof(s8_line_ctx), s8_line_setup, s8_line_msb_call, s8_line_hash16},
};

void bench_register_step08(void) {
    bench_add_all(step8_cases, sizeof(step8_cases) / sizeof(step8_cases[0]));
}
