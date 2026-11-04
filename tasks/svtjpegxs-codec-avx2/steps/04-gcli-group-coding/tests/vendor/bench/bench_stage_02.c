/*
 * codecbench, ladder step 2 -- forward 5/3 reversible DWT (encoder side).  One
 * call is one line of one component, 1920 wide for 1080p luma and 960 for its
 * chroma; each slot is reached through the rtcd pointer installed for it:
 *   dwt_horizontal_line                             (out_lf, out_hf, in, len)
 *   transform_V1_Hx_precinct_recalc_HF_prev         (width, out_hf, in0, in1, in2)
 *   transform_vertical_loop_hf_line_0               (width, out_hf, in0, in1)
 *   transform_vertical_loop_lf_line_0               (width, out_lf, in0, in1)
 *   transform_vertical_loop_lf_hf_line_0            (width, out_lf, out_hf, in0, in1, hf_prev)
 *   transform_vertical_loop_lf_hf_line_x_prev       (width, out_lf, out_hf, in0..in4)
 *   transform_vertical_loop_lf_hf_hf_line_x         (width, out_lf, out_hf, in0, in1, hf_prev, hf)
 *   transform_vertical_loop_lf_hf_hf_line_last_even (width, out_lf, out_hf, in0, in1, in2)
 *
 * Input domain: the centred Bw-bit range the forward NLT produces with Bw = 20,
 * a pixel mapping to (pixel << 12) - 2^19, so a line lies in [-2^19, 2^19 - 1]
 * and one 5/3 lifting level keeps every drawn value well inside int32_t.
 *
 * Widths are 1920, 1919, 1904, 960, 1917 and 1915; each output line is hashed
 * over width + 8 words, the horizontal slot's over (len + 1) / 2 per band.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "encoder_dsp_rtcd.h"

#define STEP2_MAXW 1920 /* 1080p 4:2:2 luma line */
/* 32 int32_t = 128 B of slack behind every buffer, twice what the harness needs
 * for, so a call cannot walk past the end of the allocation. */
#define STEP2_PAD 32
/* ... and the digest covers this many words past the last word the call owns,
 * so a write past the end of a line is caught rather than tolerated. */
#define STEP2_GUARD 8

/* 1920 = a whole 1080p luma line, 1904 = 16 * 119 = the width of a real
 * horizontal band, 960 = the 4:2:2 chroma line; 1919, 1917 and 1915 are the
 * same line moved by a few samples. */
static const uint32_t k_widths[] = {1920, 1919, 1904, 960, 1917, 1915};
#define STEP2_NWIDTHS ((int)(sizeof(k_widths) / sizeof(k_widths[0])))

static uint32_t step2_width(int round) {
    return (round < 0) ? k_widths[0] : k_widths[round % STEP2_NWIDTHS];
}

#define STEP2_LINE_MAX (1 << 19)

/* A line of the 20-bit nlt-scaled input domain, signed: a real one is
 * [-(1 << 19), (1 << 19) - 1].  Drawn away from the extremes of the type, so
 * no index or sum can go wrong. */
static int32_t step2_line(uint64_t *s) {
    return bench_rand_range(s, -STEP2_LINE_MAX, STEP2_LINE_MAX - 1);
}

static const uint64_t H0 = 0xCBF29CE484222325ULL;

/* Hash `width + STEP2_GUARD` words of an output line. */
static uint64_t hash_line(const int32_t *p, uint32_t width, uint64_t h) {
    return bench_fnv1a(p, (size_t)(width + STEP2_GUARD) * sizeof(int32_t), h);
}

/* ------------------------------------------------- dwt_horizontal_line ---
 * One line, split into len/2 (round up) low and high coefficients.  out_lf and
 * out_hf each hold one word per pair, rounded up; both are hashed over
 * (len + 1) / 2 words, which covers every word either array holds. */
typedef struct {
    uint32_t len;
    int32_t in[STEP2_MAXW + STEP2_PAD];
    int32_t out_lf[STEP2_MAXW / 2 + 2 + STEP2_PAD];
    int32_t out_hf[STEP2_MAXW / 2 + 2 + STEP2_PAD];
} hline_ctx;

static void hline_setup(void *v, uint64_t seed, int round) {
    hline_ctx *c = v;
    uint64_t s = seed;
    c->len = step2_width(round);
    for (uint32_t i = 0; i < c->len; i++)
        c->in[i] = step2_line(&s);
}

static void hline_call(void *v) {
    hline_ctx *c = v;
    dwt_horizontal_line(c->out_lf, c->out_hf, c->in, c->len);
}

static void hline_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    hline_ctx *c = v;
    const uint32_t n = (c->len + 1) / 2;
    uint64_t h = hash_line(c->out_lf, n, H0);
    h = hash_line(c->out_hf, n, h);
    bench_hash_finish(h, out);
}

/* --------------------------------------- the seven vertical slots ---
 * These share one context type: every call takes the plane width and some of
 * `line[0]`..`line[6]` (all prefilled from the seed) and writes out_lf and
 * out_hf, or the carried HF word line for the recalc slot.  The call above
 * each case shows which named array goes to which argument. */
typedef struct {
    uint32_t width;
    int32_t line[7][STEP2_MAXW + STEP2_PAD];
    int32_t out_lf[STEP2_MAXW + STEP2_PAD];
    int32_t out_hf[STEP2_MAXW + STEP2_PAD];
    int32_t tmp[STEP2_MAXW + STEP2_PAD]; /* out_tmp_line_HF_next */
} vloop_ctx;

static void vloop_setup(void *v, uint64_t seed, int round) {
    vloop_ctx *c = v;
    uint64_t s = seed;
    c->width = step2_width(round);
    for (int k = 0; k < 7; k++)
        for (uint32_t i = 0; i < c->width; i++)
            c->line[k][i] = step2_line(&s);
}

static void hash_lf(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    vloop_ctx *c = v;
    bench_hash_finish(hash_line(c->out_lf, c->width, H0), out);
}

static void hash_hf(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    vloop_ctx *c = v;
    bench_hash_finish(hash_line(c->out_hf, c->width, H0), out);
}

static void hash_lf_hf(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    vloop_ctx *c = v;
    uint64_t h = hash_line(c->out_lf, c->width, H0);
    h = hash_line(c->out_hf, c->width, h);
    bench_hash_finish(h, out);
}

static void recalc_call(void *v) {
    vloop_ctx *c = v;
    transform_V1_Hx_precinct_recalc_HF_prev(c->width, c->tmp, c->line[0], c->line[1], c->line[2]);
}

static void recalc_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    vloop_ctx *c = v;
    bench_hash_finish(hash_line(c->tmp, c->width, H0), out);
}

static void hf_line_0_call(void *v) {
    vloop_ctx *c = v;
    transform_vertical_loop_hf_line_0(c->width, c->out_hf, c->line[0], c->line[1]);
}

static void lf_line_0_call(void *v) {
    vloop_ctx *c = v;
    transform_vertical_loop_lf_line_0(c->width, c->out_lf, c->line[0], c->line[1]);
}

static void lf_hf_line_0_call(void *v) {
    vloop_ctx *c = v;
    transform_vertical_loop_lf_hf_line_0(c->width, c->out_lf, c->out_hf, c->line[0], c->line[1], c->line[2]);
}

static void lf_hf_line_x_prev_call(void *v) {
    vloop_ctx *c = v;
    transform_vertical_loop_lf_hf_line_x_prev(
        c->width, c->out_lf, c->out_hf, c->line[6], c->line[5], c->line[4], c->line[3], c->line[2]);
}

static void lf_hf_hf_line_x_call(void *v) {
    vloop_ctx *c = v;
    transform_vertical_loop_lf_hf_hf_line_x(
        c->width, c->out_lf, c->out_hf, c->line[0], c->line[1], c->line[2], c->line[3]);
}

static void lf_hf_hf_line_last_even_call(void *v) {
    vloop_ctx *c = v;
    transform_vertical_loop_lf_hf_hf_line_last_even(c->width, c->out_lf, c->out_hf, c->line[0], c->line[1], c->line[2]);
}

static const bench_case step2_cases[] = {
    {"dwt_horizontal_line", sizeof(hline_ctx), hline_setup, hline_call, hline_hash},
    {"transform_V1_Hx_precinct_recalc_HF_prev", sizeof(vloop_ctx), vloop_setup, recalc_call, recalc_hash},
    {"transform_vertical_loop_hf_line_0", sizeof(vloop_ctx), vloop_setup, hf_line_0_call, hash_hf},
    {"transform_vertical_loop_lf_line_0", sizeof(vloop_ctx), vloop_setup, lf_line_0_call, hash_lf},
    {"transform_vertical_loop_lf_hf_line_0", sizeof(vloop_ctx), vloop_setup, lf_hf_line_0_call, hash_lf_hf},
    {"transform_vertical_loop_lf_hf_line_x_prev", sizeof(vloop_ctx), vloop_setup, lf_hf_line_x_prev_call, hash_lf_hf},
    {"transform_vertical_loop_lf_hf_hf_line_x", sizeof(vloop_ctx), vloop_setup, lf_hf_hf_line_x_call, hash_lf_hf},
    {"transform_vertical_loop_lf_hf_hf_line_last_even", sizeof(vloop_ctx), vloop_setup, lf_hf_hf_line_last_even_call,
     hash_lf_hf},
};

void bench_register_step02(void) {
    bench_add_all(step2_cases, sizeof(step2_cases) / sizeof(step2_cases[0]));
}
