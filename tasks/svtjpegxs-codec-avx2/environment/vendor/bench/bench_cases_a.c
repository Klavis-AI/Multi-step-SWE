/*
 * codecbench, case group: NLT forward and input format conversion.
 *
 * Scoped slots, each reached through the rtcd pointer installed for it:
 *   image_shift                         image_shift(out, in, w, 6, 1 << 13)
 *   linear_input_scaling_line_8bit      linear_input_scaling_line_8bit(src, dst, w, 12, 1 << 19)
 *   linear_input_scaling_line_16bit     linear_input_scaling_line_16bit(src, dst, w, 10, 1 << 19, 10)
 *   linear_input_scaling_line_16bit_msb linear_input_scaling_line_16bit_msb(src, dst, w, 4, 1 << 19, 10)
 *   convert_packed_to_planar_rgb_8bit   convert_packed_to_planar_rgb_8bit(in, c1, c2, c3, w)
 *   convert_packed_to_planar_rgb_16bit  convert_packed_to_planar_rgb_16bit(in, c1, c2, c3, w)
 *
 * Parameters are the ones a 1080p encode uses: Bw = 20, so shift = Bw -
 * bit_depth and offset = 1 << (Bw - 1) for the NLT slots.  Inputs are one
 * 8-bit sample per pixel in [0, 255], or [0, 1023] LSB-aligned / MSB-aligned
 * (v << 6), and one packed RGB triple per pixel in the same ranges.
 *
 * The correctness rounds walk the widths 1920, 1919, 1920 and 1904 in order;
 * the timed call draws its own entry from the same table.  Digests: out
 * (w uint16_t), dst (w int32_t) and the three planar components end to end.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "encoder_dsp_rtcd.h"

#define STEP1_MAXW 1920
#define STEP1_PAD 64

static const uint32_t k_widths[] = {1920, 1919, 1920, 1904};
#define NWIDTHS ((int)(sizeof(k_widths) / sizeof(k_widths[0])))

static uint32_t step1_width(int round) {
    return k_widths[(uint32_t)round % NWIDTHS];
}

static const uint64_t H0 = 0xCBF29CE484222325ULL;

/* --------------------------------------------------------- image_shift --- */
typedef struct {
    uint32_t w;
    int32_t in[STEP1_MAXW + STEP1_PAD];
    uint16_t out[STEP1_MAXW + STEP1_PAD];
} shift_ctx;

static void shift_setup(void *v, uint64_t seed, int round) {
    shift_ctx *c = v;
    uint64_t s = seed;
    c->w = step1_width(round);
    /* |coeff| < 2^19: the centred Bw-bit range a 20-bit encode produces, inside
     * the fifteen bits of magnitude the codec's coefficients carry. */
    for (uint32_t i = 0; i < c->w; i++)
        c->in[i] = bench_rand_range(&s, -(1 << 18), 1 << 18);
}

static void shift_call(void *v) {
    shift_ctx *c = v;
    image_shift(c->out, c->in, c->w, 6, 1 << 13);
}

static void shift_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    shift_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->out, c->w * sizeof(uint16_t), H0), out);
}

/* ------------------------------------- linear_input_scaling_line_8bit --- */
typedef struct {
    uint32_t w;
    uint8_t src[STEP1_MAXW + STEP1_PAD];
    int32_t dst[STEP1_MAXW + STEP1_PAD];
} scale8_ctx;

static void scale8_setup(void *v, uint64_t seed, int round) {
    scale8_ctx *c = v;
    uint64_t s = seed;
    c->w = step1_width(round);
    for (uint32_t i = 0; i < c->w; i++)
        c->src[i] = (uint8_t)bench_rand_u32(&s, 0xFF);
}

static void scale8_call(void *v) {
    scale8_ctx *c = v;
    linear_input_scaling_line_8bit(c->src, c->dst, c->w, 12, 1 << 19);
}

static void scale8_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    scale8_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->dst, c->w * sizeof(int32_t), H0), out);
}

/* -------------------------- linear_input_scaling_line_16bit[._msb] --- */
typedef struct {
    uint32_t w;
    uint16_t src[STEP1_MAXW + STEP1_PAD];
    int32_t dst[STEP1_MAXW + STEP1_PAD];
} scale16_ctx;

static void scale16_setup(void *v, uint64_t seed, int round) {
    scale16_ctx *c = v;
    uint64_t s = seed;
    c->w = step1_width(round);
    for (uint32_t i = 0; i < c->w; i++)
        c->src[i] = (uint16_t)bench_rand_u32(&s, 0x3FF); /* 10-bit LSB-aligned */
}

static void scale16_call(void *v) {
    scale16_ctx *c = v;
    linear_input_scaling_line_16bit(c->src, c->dst, c->w, 10, 1 << 19, 10);
}

static void scale16_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    scale16_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->dst, c->w * sizeof(int32_t), H0), out);
}

static void scale16_msb_setup(void *v, uint64_t seed, int round) {
    scale16_ctx *c = v;
    uint64_t s = seed;
    c->w = step1_width(round);
    /* MSB-aligned: the 10-bit sample occupies the top bits of the word. */
    for (uint32_t i = 0; i < c->w; i++)
        c->src[i] = (uint16_t)(bench_rand_u32(&s, 0x3FF) << 6);
}

static void scale16_msb_call(void *v) {
    scale16_ctx *c = v;
    linear_input_scaling_line_16bit_msb(c->src, c->dst, c->w, 4, 1 << 19, 10);
}

static void scale16_msb_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    scale16_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->dst, c->w * sizeof(int32_t), H0), out);
}

/* ------------------------------ convert_packed_to_planar_rgb_{8,16}bit --- */
typedef struct {
    uint32_t w;
    uint8_t in8[3 * STEP1_MAXW + STEP1_PAD];
    uint16_t in16[3 * STEP1_MAXW + STEP1_PAD];
    uint8_t c1[STEP1_MAXW * 2 + STEP1_PAD];
    uint8_t c2[STEP1_MAXW * 2 + STEP1_PAD];
    uint8_t c3[STEP1_MAXW * 2 + STEP1_PAD];
} planar_ctx;

static void planar_setup(void *v, uint64_t seed, int round) {
    planar_ctx *c = v;
    uint64_t s = seed;
    c->w = step1_width(round);
    for (uint32_t i = 0; i < c->w; i++) {
        for (int k = 0; k < 3; k++) {
            c->in8[3 * i + k] = (uint8_t)bench_rand_u32(&s, 0xFF);
            c->in16[3 * i + k] = (uint16_t)bench_rand_u32(&s, 0x3FF);
        }
    }
}

static void planar8_call(void *v) {
    planar_ctx *c = v;
    convert_packed_to_planar_rgb_8bit(c->in8, c->c1, c->c2, c->c3, c->w);
}

static void planar16_call(void *v) {
    planar_ctx *c = v;
    convert_packed_to_planar_rgb_16bit(c->in16, c->c1, c->c2, c->c3, c->w);
}

static void planar8_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    planar_ctx *c = v;
    uint64_t h = bench_fnv1a(c->c1, c->w, H0);
    h = bench_fnv1a(c->c2, c->w, h);
    h = bench_fnv1a(c->c3, c->w, h);
    bench_hash_finish(h, out);
}

static void planar16_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    planar_ctx *c = v;
    uint64_t h = bench_fnv1a(c->c1, c->w * 2, H0);
    h = bench_fnv1a(c->c2, c->w * 2, h);
    h = bench_fnv1a(c->c3, c->w * 2, h);
    bench_hash_finish(h, out);
}

static const bench_case step1_cases[] = {
    {"image_shift", sizeof(shift_ctx), shift_setup, shift_call, shift_hash},
    {"linear_input_scaling_line_8bit", sizeof(scale8_ctx), scale8_setup, scale8_call, scale8_hash},
    {"linear_input_scaling_line_16bit", sizeof(scale16_ctx), scale16_setup, scale16_call, scale16_hash},
    {"linear_input_scaling_line_16bit_msb", sizeof(scale16_ctx), scale16_msb_setup, scale16_msb_call,
     scale16_msb_hash},
    {"convert_packed_to_planar_rgb_8bit", sizeof(planar_ctx), planar_setup, planar8_call, planar8_hash},
    {"convert_packed_to_planar_rgb_16bit", sizeof(planar_ctx), planar_setup, planar16_call, planar16_hash},
};

void bench_register_a(void) {
    bench_add_all(step1_cases, sizeof(step1_cases) / sizeof(step1_cases[0]));
}
