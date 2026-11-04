/*
 * codecbench, case group: inverse 5/3 reversible DWT (decoder side).
 *
 * Scoped slots, each reached through the rtcd pointer installed for it:
 *   idwt_horizontal_line_lf16_hf16 (in_lf, in_hf, out, len, shift)
 *   idwt_horizontal_line_lf32_hf16 (in_lf, in_hf, out, len, shift)
 *   idwt_vertical_line             (in_lf, in_hf0, in_hf1, out[4], len, first_precinct, last_precinct, height)
 *   idwt_vertical_line_recalc      (in_lf, in_hf0, in_hf1, out[4], len, precinct_line_idx)
 *
 * One call is one line of one component, 1920 wide for 1080p luma and 960 for
 * its chroma.  Lengths are 1920, 1919, 1904, 960, 959, 952 and 480; the vertical
 * slots also walk interior, first and last precincts, heights 1080, 1081, 540, 2.
 *
 * Input domain: hdr_Fq is 8 in the 1080p profile, and the codec bounds a
 * coefficient at fifteen bits of magnitude before that left shift, so a band
 * word is |coeff << Fq| <= 2^23 and every intermediate stays inside int32_t.
 *
 * The vertical slot writes four len-word lines at once and reads out[0], so all
 * four planes are prefilled from the seed; outputs are hashed to len + 8 words.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "decoder_dsp_rtcd.h"

#define STEP7_MAXLEN 1920 /* 1080p 4:2:2 luma line */
/* 32 int32_t = 128 B of slack behind every buffer, twice what the harness needs
 * for, so no call can walk past the end of the allocation. */
#define STEP7_PAD 32
/* ... and the digest covers this many words past the last word the call owns,
 * so a write past the end of a line is caught rather than tolerated. */
#define STEP7_GUARD 8

/* 1920 = a whole 1080p luma line, 1904 = 16 * 119; 960/959/952/480 are the
 * chroma line and the same lines at half resolution, i.e. the second inverse
 * stage's widths. */
static const uint32_t k_lens[] = {1920, 1919, 1904, 960, 959, 952, 480};
#define STEP7_NLENS ((int)(sizeof(k_lens) / sizeof(k_lens[0])))

static uint32_t step7_len(int round) {
    return k_lens[(uint32_t)round % STEP7_NLENS];
}

/* hdr_Fq as the header parser accepts it: 8 with Bw = 20 (the 1080p default,
 * WAVELET_FRACTION_BITS_FQ_DEFAULT), 6 with Bw = 18, or 0. */
static const uint8_t k_shifts[] = {8, 6, 0, 8};
#define STEP7_NSHIFTS ((int)(sizeof(k_shifts) / sizeof(k_shifts[0])))

static uint8_t step7_shift(int round) {
    return k_shifts[(uint32_t)round % STEP7_NSHIFTS];
}

/* 15-bit magnitude, the bound the codec puts on a wavelet coefficient. */
#define STEP7_COEFF16_MAX ((1 << 15) - 1)
/* ... and the same coefficient after the Fq left shift: what the lf32
 * horizontal and both vertical slots are handed. */
#define STEP7_COEFF32_MAX (STEP7_COEFF16_MAX << 8)

static int16_t step7_coeff16(uint64_t *s) {
    return (int16_t)bench_rand_range(s, -STEP7_COEFF16_MAX, STEP7_COEFF16_MAX);
}

static int32_t step7_coeff32(uint64_t *s) {
    return bench_rand_range(s, -STEP7_COEFF32_MAX, STEP7_COEFF32_MAX);
}

static const uint64_t H0 = 0xCBF29CE484222325ULL;

/* Hash `len + STEP7_GUARD` words of an output line. */
static uint64_t hash_line(const int32_t *p, uint32_t len, uint64_t h) {
    return bench_fnv1a(p, (size_t)(len + STEP7_GUARD) * sizeof(int32_t), h);
}

/* ------------------------------------------------------------ horizontal ---
 * in_lf holds ceil(len/2) low-band words, in_hf the same number of high-band
 * ones, out receives len words; the bands are drawn two words longer, so those
 * words come from the seed rather than from the padding. */
static uint32_t band_words(uint32_t len) {
    return (len + 1) / 2 + 2;
}

typedef struct {
    uint32_t len;
    uint8_t shift;
    int16_t in_lf[STEP7_MAXLEN / 2 + 4 + STEP7_PAD];
    int16_t in_hf[STEP7_MAXLEN / 2 + 4 + STEP7_PAD];
    int32_t out[STEP7_MAXLEN + STEP7_PAD];
} h16_ctx;

static void h16_setup(void *v, uint64_t seed, int round) {
    h16_ctx *c = v;
    uint64_t s = seed;
    c->len = step7_len(round);
    c->shift = step7_shift(round);
    for (uint32_t i = 0; i < band_words(c->len); i++) {
        c->in_lf[i] = step7_coeff16(&s);
        c->in_hf[i] = step7_coeff16(&s);
    }
}

static void h16_call(void *v) {
    h16_ctx *c = v;
    idwt_horizontal_line_lf16_hf16(c->in_lf, c->in_hf, c->out, c->len, c->shift);
}

static void h16_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    h16_ctx *c = v;
    bench_hash_finish(hash_line(c->out, c->len, H0), out);
}

/* Same line, but the low band is already 32-bit: the second and later inverse
 * stages hand in the previous stage's output, where the Fq shift is already
 * applied. */
typedef struct {
    uint32_t len;
    uint8_t shift;
    int32_t in_lf[STEP7_MAXLEN / 2 + 4 + STEP7_PAD];
    int16_t in_hf[STEP7_MAXLEN / 2 + 4 + STEP7_PAD];
    int32_t out[STEP7_MAXLEN + STEP7_PAD];
} h32_ctx;

static void h32_setup(void *v, uint64_t seed, int round) {
    h32_ctx *c = v;
    uint64_t s = seed;
    c->len = step7_len(round);
    c->shift = step7_shift(round);
    for (uint32_t i = 0; i < band_words(c->len); i++) {
        c->in_lf[i] = step7_coeff32(&s);
        c->in_hf[i] = step7_coeff16(&s);
    }
}

static void h32_call(void *v) {
    h32_ctx *c = v;
    idwt_horizontal_line_lf32_hf16(c->in_lf, c->in_hf, c->out, c->len, c->shift);
}

static void h32_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    h32_ctx *c = v;
    bench_hash_finish(hash_line(c->out, c->len, H0), out);
}

/* -------------------------------------------------------------- vertical ---
 * Three input lines of len words and four output lines of len words, all
 * separate arrays.  `plane[0]` is also read, as the carry from the previous
 * precinct, so all four planes are prefilled from the seed. */
typedef struct {
    uint32_t len;
    int32_t first_precinct;
    int32_t last_precinct;
    int32_t height;
    uint32_t precinct_line_idx;
    int32_t in_lf[STEP7_MAXLEN + STEP7_PAD];
    int32_t in_hf0[STEP7_MAXLEN + STEP7_PAD];
    int32_t in_hf1[STEP7_MAXLEN + STEP7_PAD];
    int32_t plane[4][STEP7_MAXLEN + STEP7_PAD];
} vline_ctx;

static void vline_fill(vline_ctx *c, uint64_t seed) {
    uint64_t s = seed;
    for (uint32_t i = 0; i < c->len; i++) {
        c->in_lf[i] = step7_coeff32(&s);
        c->in_hf0[i] = step7_coeff32(&s);
        c->in_hf1[i] = step7_coeff32(&s);
        /* Distinct per plane, so a write into a plane this round does not own
         * changes the digest. */
        for (int k = 0; k < 4; k++)
            c->plane[k][i] = step7_coeff32(&s);
    }
}

/* Which precinct position and component height a round runs.  The entries cover
 * an interior precinct of a 1080p component (height 1080, neither first nor
 * last in the frame), the first and last precincts, and two shorter ones. */
typedef struct {
    int32_t first, last, height;
} vcfg_t;

static const vcfg_t k_vcfg[] = {
    {0, 0, 1080}, /* interior precinct */
    {1, 0, 1080}, /* first precinct in the component */
    {0, 1, 1081}, /* last precinct, odd component height */
    {0, 1, 540},  /* last precinct, even component height */
    {0, 0, 2},    /* component height 2 */
};
#define STEP7_NVCFG ((int)(sizeof(k_vcfg) / sizeof(k_vcfg[0])))

static void vline_setup(void *v, uint64_t seed, int round) {
    vline_ctx *c = v;
    const vcfg_t *cfg = &k_vcfg[(uint32_t)round % STEP7_NVCFG];
    c->len = step7_len(round);
    c->first_precinct = cfg->first;
    c->last_precinct = cfg->last;
    c->height = cfg->height;
    vline_fill(c, seed);
}

static void vline_call(void *v) {
    vline_ctx *c = v;
    int32_t *out[4] = {c->plane[0], c->plane[1], c->plane[2], c->plane[3]};
    idwt_vertical_line(
        c->in_lf, c->in_hf0, c->in_hf1, out, c->len, c->first_precinct, c->last_precinct, c->height);
}

/* Every plane is hashed, including the ones a round leaves untouched: those
 * hold the seed's prefill on both profiles, and a stray write to them is a
 * difference worth seeing. */
static void vline_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    vline_ctx *c = v;
    uint64_t h = H0;
    for (int k = 0; k < 4; k++)
        h = hash_line(c->plane[k], c->len, h);
    bench_hash_finish(h, out);
}

/* precinct_line_idx is 2, 3, 0 or 1 across the rounds; in_hf0 is drawn like the
 * other inputs, so it is defined whatever the call reads from it. */
static const uint32_t k_recalc_idx[] = {2, 3, 0, 1};
#define STEP7_NRECALC ((int)(sizeof(k_recalc_idx) / sizeof(k_recalc_idx[0])))

static void recalc_setup(void *v, uint64_t seed, int round) {
    vline_ctx *c = v;
    c->len = step7_len(round);
    c->precinct_line_idx = k_recalc_idx[(uint32_t)round % STEP7_NRECALC];
    vline_fill(c, seed);
}

static void recalc_call(void *v) {
    vline_ctx *c = v;
    int32_t *out[4] = {c->plane[0], c->plane[1], c->plane[2], c->plane[3]};
    idwt_vertical_line_recalc(c->in_lf, c->in_hf0, c->in_hf1, out, c->len, c->precinct_line_idx);
}

static const bench_case step7_cases[] = {
    {"idwt_horizontal_line_lf16_hf16", sizeof(h16_ctx), h16_setup, h16_call, h16_hash},
    {"idwt_horizontal_line_lf32_hf16", sizeof(h32_ctx), h32_setup, h32_call, h32_hash},
    {"idwt_vertical_line", sizeof(vline_ctx), vline_setup, vline_call, vline_hash},
    {"idwt_vertical_line_recalc", sizeof(vline_ctx), recalc_setup, recalc_call, vline_hash},
};

void bench_register_g(void) {
    bench_add_all(step7_cases, sizeof(step7_cases) / sizeof(step7_cases[0]));
}
