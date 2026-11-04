/*
 * codecbench, case group: the two single-slot cases.  Each is a single scoped
 * rtcd slot, so both live in this one file and are registered together;
 * bench_cases_e.c is left an empty stub (two definitions would collide).
 *
 *   quantization                        (coeff_16bit, size, gclis, group_size, gtli, quant_type)
 *   rate_control_calc_vpred_cost_nosigf (gcli_width, gcli_top, gcli, vpred_bits_pack, gtli, gtli_max)
 *
 * One quantization case per QUANT_TYPE, since the encoder chooses between the
 * two.  group_size is pinned at 4 (GROUP_SIZE, the value the codec uses); the
 * sizes walk 4096, 4095, 4090 and 4084 and the vpred widths 480, 479, 484, 472.
 *
 * Input domain: the encoder maintains that every coefficient of a group is
 * smaller in magnitude than 2^gclis[group].  Coefficients are sign-magnitude in
 * sixteen bits with the sign at bit 15, gtli and every gcli lie in
 * [0, TRUNCATION_MAX = 15], no -0 occurs, and gcli_max >= gtli for the vpred slot.
 *
 * Digests: coeff (size uint16_t) and gclis (groups bytes) for the quant cases;
 * the returned count and vpred_bits_pack for the vpred one.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "encoder_dsp_rtcd.h"

/* TRUNCATION_MAX: the largest legal gcli/gtli for this codec. */
#define STEP3_TRUNCATION_MAX 15
/* GROUP_SIZE: one gcli per this many coefficients. */
#define STEP3_GROUP_SIZE 4

#define STEP3_PAD 64

/* ------------------------------------------------------------ quantization ---
 * size is a line of a 1080p 4:2:2 precinct band: a few thousand coefficients.
 * The four correctness rounds use 4096, 4095, 4090 and 4084, i.e. sizes that
 * are and are not a whole number of groups. */
static const uint32_t k_quant_size[] = {4096, 4095, 4090, 4084};
#define NQUANT ((int)(sizeof(k_quant_size) / sizeof(k_quant_size[0])))

/* Largest entry of k_quant_size, spelled out because the bounds of the arrays
 * in the ctx struct below cannot be a runtime value. */
#define STEP3_MAX_SIZE 4096u
#define STEP3_MAX_GROUPS ((STEP3_MAX_SIZE + STEP3_GROUP_SIZE - 1) / STEP3_GROUP_SIZE)

static uint32_t step3_size(int round) {
    return k_quant_size[(uint32_t)round % NQUANT];
}

typedef struct {
    uint32_t size;
    uint32_t groups;
    uint8_t gtli;
    uint16_t coeff[STEP3_MAX_SIZE + STEP3_PAD];
    uint8_t gclis[STEP3_MAX_GROUPS + STEP3_PAD];
} quant_ctx;

/* Both quant cases share this draw, so their digests are comparable. */
static void quant_setup(void *v, uint64_t seed, int round) {
    quant_ctx *c = v;
    uint64_t s = seed;
    c->size = step3_size(round);
    c->groups = (c->size + STEP3_GROUP_SIZE - 1) / STEP3_GROUP_SIZE;
    /* A realistic operating point: the encoder truncates somewhere in the
     * middle of the significance range, so some groups quantize and others drop
     * to zero.  Both ends are excluded, for two different reasons:
     *   gtli == TRUNCATION_MAX: no group can be above the truncation point;
     *   gtli == 0: the two quant types coincide on this domain, so the round
     *     would compare the same function with itself.
     * Between the two ends every round has a live mix of both outcomes. */
    c->gtli = (uint8_t)bench_rand_range(&s, 1, STEP3_TRUNCATION_MAX - 1);

    for (uint32_t g = 0; g < c->groups; g++)
        c->gclis[g] = (uint8_t)bench_rand_range(&s, 0, STEP3_TRUNCATION_MAX);

    for (uint32_t i = 0; i < c->size; i++) {
        uint8_t gcli = c->gclis[i / STEP3_GROUP_SIZE];
        /* Magnitude strictly below 2^gcli, the invariant above.  The draw is
         * uniform over the group's legal range, which straddles 2^gtli, so
         * coefficients both above and below the truncation point occur. */
        int32_t mag = (gcli == 0) ? 0 : bench_rand_range(&s, 0, (int32_t)((1u << gcli) - 1u));
        uint16_t sign = (mag != 0 && (bench_rand_u32(&s, 1) != 0)) ? (uint16_t)0x8000 : (uint16_t)0;
        c->coeff[i] = (uint16_t)(sign | (uint16_t)mag);
    }
}

static void quant_deadzone_call(void *v) {
    quant_ctx *c = v;
    quantization(c->coeff, c->size, c->gclis, STEP3_GROUP_SIZE, c->gtli, QUANT_TYPE_DEADZONE);
}

static void quant_uniform_call(void *v) {
    quant_ctx *c = v;
    quantization(c->coeff, c->size, c->gclis, STEP3_GROUP_SIZE, c->gtli, QUANT_TYPE_UNIFORM);
}

/* Every word the call rewrites, plus gclis, so a call that changes the gcli
 * bytes fails here. */
static void quant_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    quant_ctx *c = v;
    uint64_t h = bench_fnv1a(c->coeff, (size_t)c->size * sizeof(uint16_t), 0xCBF29CE484222325ULL);
    h = bench_fnv1a(c->gclis, c->groups, h);
    bench_hash_finish(h, out);
}

/* ------------------------------------------- rate_control_calc_vpred_cost ---
 * gcli_width is the number of significance bytes on a band line of a 1080p
 * frame: a few hundred.  The four rounds use 480, 479, 484 and 472, so widths
 * that are and are not a whole number of significance groups occur. */
static const uint32_t k_vpred_width[] = {480, 479, 484, 472};
#define NVPRED ((int)(sizeof(k_vpred_width) / sizeof(k_vpred_width[0])))

/* Largest entry of k_vpred_width; see STEP3_MAX_SIZE. */
#define STEP5_MAX_WIDTH 480u

static uint32_t step5_width(int round) {
    return k_vpred_width[(uint32_t)round % NVPRED];
}

typedef struct {
    uint32_t gcli_width;
    uint8_t gtli;
    uint8_t gtli_max;
    uint8_t gcli_top[STEP5_MAX_WIDTH + STEP3_PAD];
    uint8_t gcli[STEP5_MAX_WIDTH + STEP3_PAD];
    uint8_t vpred_bits_pack[STEP5_MAX_WIDTH + STEP3_PAD];
    uint32_t ret;
} vpred_ctx;

static void vpred_setup(void *v, uint64_t seed, int round) {
    vpred_ctx *c = v;
    uint64_t s = seed;
    c->gcli_width = step5_width(round);
    /* The encoder's rate control sets gtli_max = MAX(gtli, gtli of the line
     * above), with both truncations in [0, TRUNCATION_MAX]. */
    c->gtli = (uint8_t)bench_rand_range(&s, 0, STEP3_TRUNCATION_MAX);
    c->gtli_max = (uint8_t)bench_rand_range(&s, c->gtli, STEP3_TRUNCATION_MAX);
    for (uint32_t i = 0; i < c->gcli_width; i++) {
        c->gcli_top[i] = (uint8_t)bench_rand_range(&s, 0, STEP3_TRUNCATION_MAX);
        c->gcli[i] = (uint8_t)bench_rand_range(&s, 0, STEP3_TRUNCATION_MAX);
    }
}

static void vpred_call(void *v) {
    vpred_ctx *c = v;
    c->ret = rate_control_calc_vpred_cost_nosigf(
        c->gcli_width, c->gcli_top, c->gcli, c->vpred_bits_pack, c->gtli, c->gtli_max);
}

static void vpred_hash(void *v, uint8_t out[BENCH_DIGEST_BYTES]) {
    vpred_ctx *c = v;
    uint64_t h = bench_fnv1a(c->vpred_bits_pack, c->gcli_width, 0xCBF29CE484222325ULL);
    h = bench_fnv1a(&c->ret, sizeof(c->ret), h);
    bench_hash_finish(h, out);
}

static const bench_case step3_cases[] = {
    {"quantization_deadzone", sizeof(quant_ctx), quant_setup, quant_deadzone_call, quant_hash},
    {"quantization_uniform", sizeof(quant_ctx), quant_setup, quant_uniform_call, quant_hash},
    {"rate_control_calc_vpred_cost_nosigf", sizeof(vpred_ctx), vpred_setup, vpred_call, vpred_hash},
};

void bench_register_c(void) {
    bench_add_all(step3_cases, sizeof(step3_cases) / sizeof(step3_cases[0]));
}
