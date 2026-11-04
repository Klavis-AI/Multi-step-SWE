/*
 * codecbench, case group: GCLI group coding and significance.
 *
 * Scoped slots, each reached through the rtcd pointer installed for it:
 *   gc_precinct_stage_scalar      (gcli, coeff, group_size = 4, width)
 *   gc_precinct_stage_scalar_loop (groups, coeff, gcli)
 *   gc_histogram_16               (data, width, hist)
 *   gc_precinct_sigflags_max      (sig, gcli, group_sign_size = 8, gcli_width)
 *
 * Geometry is the encoder's own: a band line of W coefficients is coded as
 * DIV_ROUND_UP(W, 4) GCLI bytes grouped by 8 into DIV_ROUND_UP(gcli_width, 8)
 * significance flags; the realistic line is W = 7680 -> 1920 GCLI -> 240 flags.
 *
 * Input domain: coefficients are sign-magnitude in sixteen bits with the sign
 * at bit 15, and the encoder keeps their magnitude under fifteen bits, so a
 * GCLI -- the OR of a group with the sign bit shifted out -- fits in
 * [0, TRUNCATION_MAX = 15].  A zero magnitude is written as +0, never -0.
 *
 * Digests: the GCLI line, the 16 histogram bins, or the significance flags.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "encoder_dsp_rtcd.h"

#define STEP4_MAX_GCLI 1921u               /* widest GCLI line, = ceil(7684 / 4) */
#define STEP4_MAX_LINE 1952u               /* widest byte line fed to the histogram */
#define STEP4_MAX_COEFF (4u * STEP4_MAX_GCLI)
#define STEP4_MAX_SIG ((STEP4_MAX_LINE + 7u) / 8u)
#define STEP4_PAD 64u

#define STEP4_GROUP 4u
#define STEP4_SIG_GROUP 8u

static const uint64_t H0 = 0xCBF29CE484222325ULL;

/* A magnitude whose top set bit is at `msb` (msb == 0 is the all-zero
 * coefficient), carried as sign-magnitude with the sign at bit 15.  Magnitude
 * 1 << (msb - 1) plus noise below it keeps the top bit exact and the result
 * inside fifteen bits for every msb <= 15. */
static uint16_t step4_coeff(uint64_t *s, int msb) {
    if (msb <= 0)
        return 0;
    uint32_t v = 1u << (msb - 1);
    v += (v - 1) & bench_rand_u32(s, 0xFFFF);
    if (bench_rand_u32(s, 1))
        v |= 0x8000u; /* BITSTREAM_MASK_SIGN: -0 cannot occur, magnitude >= 1 */
    return (uint16_t)v;
}

/* GCLI is spatially correlated and mostly zero inside a real band, and a large
 * magnitude is rare: draw the group magnitude geometrically, so that a group
 * of four coefficients is all-zero about three times out of four. */
static int step4_group_msb(uint64_t *s) {
    int msb = 0;
    while (msb < 15 && bench_rand_u32(s, 3) == 0)
        msb++;
    return msb;
}

/* Fill `width` coefficients, `width` not necessarily a multiple of the group
 * size (the last group is then short, as the encoder's own lines are). */
static void step4_fill_coeffs(uint64_t *s, uint16_t *coeff, uint32_t width) {
    for (uint32_t base = 0; base < width; base += STEP4_GROUP) {
        uint32_t n = width - base;
        if (n > STEP4_GROUP)
            n = STEP4_GROUP;
        int msb = step4_group_msb(s);
        for (uint32_t i = 0; i < n; i++)
            coeff[base + i] = step4_coeff(s, msb);
    }
}

/* One GCLI value: zero about half the time (a band line is spatially
 * correlated), otherwise spread over the whole 1..TRUNCATION_MAX range so that
 * every bin of the histogram and every significance value is exercised. */
static uint8_t step4_gcli_value(uint64_t *s) {
    if (bench_rand_u32(s, 0xFF) < 0x80)
        return 0;
    return (uint8_t)bench_rand_range(s, 1, 15);
}

static void step4_fill_gcli(uint64_t *s, uint8_t *data, uint32_t width) {
    for (uint32_t i = 0; i < width; i++)
        data[i] = step4_gcli_value(s);
}

/* ------------------------------------------- gc_precinct_stage_scalar --- */
#define STEP4_NCOEFFW ((int)(sizeof(k_coeff_widths) / sizeof(k_coeff_widths[0])))
/* 7680 = 1920 whole groups; the others move the width by a few coefficients
 * and 120 is a line far shorter than a band. */
static const uint32_t k_coeff_widths[] = {7680, 7684, 7676, 7678, 7679, 7681, 7677, 120};

static uint32_t step4_coeff_width(int round) {
    return k_coeff_widths[(uint32_t)round % STEP4_NCOEFFW];
}

typedef struct {
    uint32_t width;  /* coefficients */
    uint32_t groups; /* gcli bytes actually written */
    uint16_t coeff[STEP4_MAX_COEFF + STEP4_PAD];
    uint8_t gcli[STEP4_MAX_LINE + STEP4_PAD];
} stage_ctx;

static void stage_setup(void *v, uint64_t seed, int round) {
    stage_ctx *c = v;
    uint64_t s = seed;
    c->width = step4_coeff_width(round);
    c->groups = (c->width + STEP4_GROUP - 1) / STEP4_GROUP;
    step4_fill_coeffs(&s, c->coeff, c->width);
}

static void stage_call(void *v) {
    stage_ctx *c = v;
    gc_precinct_stage_scalar(c->gcli, c->coeff, STEP4_GROUP, c->width);
}

static void stage_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    stage_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->gcli, c->groups, H0), out);
}

/* -------------------------------------- gc_precinct_stage_scalar_loop --- */
#define STEP4_NGROUPN ((int)(sizeof(k_group_nums) / sizeof(k_group_nums[0])))
/* 1920 = a whole band line's groups; the rest move that count by a few. */
static const uint32_t k_group_nums[] = {1920, 1921, 1919, 1916, 1917, 1915, 1922, 30};

static uint32_t step4_group_num(int round) {
    return k_group_nums[(uint32_t)round % STEP4_NGROUPN];
}

typedef struct {
    uint32_t groups;
    uint16_t coeff[STEP4_MAX_COEFF + STEP4_PAD];
    uint8_t gcli[STEP4_MAX_LINE + STEP4_PAD];
} loop_ctx;

static void loop_setup(void *v, uint64_t seed, int round) {
    loop_ctx *c = v;
    uint64_t s = seed;
    c->groups = step4_group_num(round);
    step4_fill_coeffs(&s, c->coeff, c->groups * STEP4_GROUP);
}

static void loop_call(void *v) {
    loop_ctx *c = v;
    gc_precinct_stage_scalar_loop(c->groups, c->coeff, c->gcli);
}

static void loop_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    loop_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->gcli, c->groups, H0), out);
}

/* --------------------------------------------------- gc_histogram_16 --- */
#define STEP4_NHISTW ((int)(sizeof(k_hist_widths) / sizeof(k_hist_widths[0])))
/* The widths move around 1920 by a few bytes; 33 is a line far shorter than a
 * band, and 1904/1936 are whole multiples of 16 bytes. */
static const uint32_t k_hist_widths[] = {1920, 1921, 1919, 1936, 1904, 1905, 1931, 33};

static uint32_t step4_hist_width(int round) {
    return k_hist_widths[(uint32_t)round % STEP4_NHISTW];
}

typedef struct {
    uint32_t width;
    uint8_t data[STEP4_MAX_LINE + STEP4_PAD];
    uint16_t hist[16]; /* TRUNCATION_MAX + 1 bins, all of them written */
} hist_ctx;

static void hist_setup(void *v, uint64_t seed, int round) {
    hist_ctx *c = v;
    uint64_t s = seed;
    c->width = step4_hist_width(round);
    step4_fill_gcli(&s, c->data, c->width);
}

static void hist_call(void *v) {
    hist_ctx *c = v;
    gc_histogram_16(c->data, c->width, c->hist);
}

static void hist_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    hist_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->hist, sizeof(c->hist), H0), out);
}

/* ----------------------------------------------- gc_precinct_sigflags_max --- */
#define STEP4_NSIGW ((int)(sizeof(k_sig_widths) / sizeof(k_sig_widths[0])))
/* gcli_width is 1920, 1921, 1919, 1912, 1918, 1917, 1936 and 33, i.e. 240 to
 * 242 whole groups, plus a 33-byte line with four whole groups. */
static const uint32_t k_sig_widths[] = {1920, 1921, 1919, 1912, 1918, 1917, 1936, 33};

static uint32_t step4_sig_width(int round) {
    return k_sig_widths[(uint32_t)round % STEP4_NSIGW];
}

typedef struct {
    uint32_t gcli_width;
    uint32_t sig_width; /* significance flags actually written */
    uint8_t gcli[STEP4_MAX_LINE + STEP4_PAD];
    uint8_t sig[STEP4_MAX_SIG + STEP4_PAD];
} sig_ctx;

static void sig_setup(void *v, uint64_t seed, int round) {
    sig_ctx *c = v;
    uint64_t s = seed;
    c->gcli_width = step4_sig_width(round);
    c->sig_width = (c->gcli_width + STEP4_SIG_GROUP - 1) / STEP4_SIG_GROUP;
    step4_fill_gcli(&s, c->gcli, c->gcli_width);
}

static void sig_call(void *v) {
    sig_ctx *c = v;
    gc_precinct_sigflags_max(c->sig, c->gcli, STEP4_SIG_GROUP, c->gcli_width);
}

static void sig_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    sig_ctx *c = v;
    bench_hash_finish(bench_fnv1a(c->sig, c->sig_width, H0), out);
}

static const bench_case step4_cases[] = {
    {"gc_precinct_stage_scalar", sizeof(stage_ctx), stage_setup, stage_call, stage_hash},
    {"gc_precinct_stage_scalar_loop", sizeof(loop_ctx), loop_setup, loop_call, loop_hash},
    {"gc_histogram_16", sizeof(hist_ctx), hist_setup, hist_call, hist_hash},
    {"gc_precinct_sigflags_max", sizeof(sig_ctx), sig_setup, sig_call, sig_hash},
};

void bench_register_d(void) {
    bench_add_all(step4_cases, sizeof(step4_cases) / sizeof(step4_cases[0]));
}
