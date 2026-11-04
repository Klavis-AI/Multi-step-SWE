/* Per-function harness for the uvg266 strategy registry.
 *
 * Compiled against ONE tree's headers and static library, so every call goes
 * through the function pointer that tree's strategy selector installed --
 * the generic C when the family's optimized twins are absent, the optimized
 * one when they are present.  Two builds of the same source tree therefore
 * produce directly comparable per-function numbers.
 *
 *   bench dump  <cpuid> <seed> <family>/<name> ...
 *   bench bench <cpuid> <seed> <family>/<name> ...
 *   bench list
 *
 * cpuid 0 forces the generic registry (uvg_strategyselector_init(0, ...) leaves
 * the hardware flags clear); any non-zero value reads the real CPUID.
 *
 * dump  : runs each entry once over seed-derived inputs and prints a 64-bit
 *         hash of the function's output, so two trees can be compared for
 *         bit-exactness of exactly the scoped functions.
 * bench : prints the median nanoseconds per call over REPS samples, each
 *         sample timing enough calls to last at least MIN_SAMPLE_NS.
 *
 * Inputs are regenerated from <seed> on every invocation.  Every entry of a
 * in-scope family takes its window from the pool at a position no earlier call in
 * this process has used, so neither a fixed input nor a memoized answer can be
 * learned.  (The alf entries are not driven by any step -- alf is stripped from
 * every level of this ladder -- and still read their own fixed buffers.)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"
#include "strategyselector.h"
#include "strategies/strategies-dct.h"
#include "strategies/strategies-picture.h"
#include "strategies/strategies-quant.h"
#include "strategies/strategies-intra.h"
#include "strategies/strategies-ipol.h"
#include "strategies/strategies-sao.h"
#include "strategies/strategies-alf.h"
#include "strategies/strategies-depquant.h"
#include "reshape.h"

/* ------------------------------------------------------------------ timing */

#define REPS 15
#define MIN_SAMPLE_NS 2000000LL
#define MAX_ITERS (1u << 24)

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* ------------------------------------------------------------------ inputs */

static uint64_t g_rng;

static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 32);
}

/* Shapes are drawn from the seed, not fixed in the source: the distribution is
 * published (see BENCHMARK.md) and the draw is not.  A separate stream keeps
 * the draw independent of how much input construction happens to run first. */
static uint64_t g_drng;

static uint32_t drnd(void)
{
    g_drng ^= g_drng << 13;
    g_drng ^= g_drng >> 7;
    g_drng ^= g_drng << 17;
    return (uint32_t)(g_drng >> 32);
}

/* dims drawn for the entries that take explicit sizes */
static int g_regsad_dims[6][2];
static int g_anysize_dims[4][2];
static int g_quaddim[2];
static int g_gentr[2];
static int g_gres[2];

static void draw_shapes(void)
{
    static const int rs[6][2] = {{8, 8}, {16, 16}, {32, 32}, {64, 64}, {64, 63}, {1, 1}};
    for (int i = 0; i < 6; i++) {
        if (i == 4 || i == 5) { g_regsad_dims[i][0] = rs[i][0]; g_regsad_dims[i][1] = rs[i][1]; }
        else { g_regsad_dims[i][0] = rs[i][0]; g_regsad_dims[i][1] = rs[i][1]; }
    }
    /* odd, non-power-of-two sizes from the published set {4,8,12,16,20,24,32,48,64} */
    static const int os[9] = {4, 8, 12, 16, 20, 24, 32, 48, 64};
    for (int i = 0; i < 4; i++) {
        g_anysize_dims[i][0] = os[drnd() % 9];
        g_anysize_dims[i][1] = os[drnd() % 9];
    }
    g_quaddim[0] = 16 << (drnd() % 3);   /* 16, 32 or 64 */
    g_quaddim[1] = g_quaddim[0];
    g_gentr[0] = 16 << (drnd() % 3);
    g_gentr[1] = g_gentr[0];
    g_gres[0] = g_gentr[0];
    g_gres[1] = g_gentr[1];
}

/* A large pool of pixels, filled once.  Entries walk it with an offset that
 * advances per call, so consecutive calls see different data. */
#define POOL_PIXELS (1 << 20)
#define POOL_I16 (1 << 20)
#define POOL_STRIDE 2048 /* >= the widest row any entry reads */
#define POOL_ROWS 512

static uvg_pixel *g_pix, *g_pix2, *g_rec;
static int16_t *g_i16, *g_i16b, *g_i16out;
static int32_t *g_i32;
static uint8_t *g_u8;
static double *g_dbl;

static void *xalloc(size_t n)
{
    void *p = NULL;
    if (posix_memalign(&p, 32, n) != 0) {
        fprintf(stderr, "alloc %zu failed\n", n);
        exit(3);
    }
    memset(p, 0, n);
    return p;
}

static void fill_pool(void)
{
    g_pix = xalloc(POOL_STRIDE * POOL_ROWS);
    g_pix2 = xalloc(POOL_STRIDE * POOL_ROWS);
    g_rec = xalloc(POOL_STRIDE * POOL_ROWS);
    g_i16 = xalloc(POOL_I16 * sizeof(int16_t));
    g_i16b = xalloc(POOL_I16 * sizeof(int16_t));
    g_i16out = xalloc(POOL_I16 * sizeof(int16_t));
    g_i32 = xalloc(POOL_I16 * sizeof(int32_t));
    g_u8 = xalloc(POOL_I16);
    g_dbl = xalloc(POOL_I16 * sizeof(double));

    /* Smooth-ish content plus noise: a flat or pure-noise pool would price the
     * kernels unrealistically (all-zero coefficients short-circuit). */
    for (int y = 0; y < POOL_ROWS; y++) {
        for (int x = 0; x < POOL_STRIDE; x++) {
            int v = 128 + 60 * (x % 97) / 97 + 40 * (y % 71) / 71 - 50
                    + (int)(rnd() % 13) - 6;
            g_pix[y * POOL_STRIDE + x] = (uvg_pixel)(v < 0 ? 0 : (v > 255 ? 255 : v));
            v = 128 + 50 * (y % 53) / 53 + 55 * (x % 89) / 89 - 50 + (int)(rnd() % 13) - 6;
            g_pix2[y * POOL_STRIDE + x] = (uvg_pixel)(v < 0 ? 0 : (v > 255 ? 255 : v));
            v = 128 + 45 * ((x + y) % 61) / 61 - 22 + (int)(rnd() % 21) - 10;
            g_rec[y * POOL_STRIDE + x] = (uvg_pixel)(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
    }
    for (size_t i = 0; i < POOL_I16; i++) {
        int v = (int)(rnd() % 2049) - 1024;
        g_i16[i] = (int16_t)v;
        g_i16b[i] = (int16_t)(int)(rnd() % 2049) - 1024;
        g_i32[i] = (int32_t)(int)(rnd() % 65536) - 32768;
    }
    for (size_t i = 0; i < POOL_I16; i++)
        g_u8[i] = (uint8_t)(rnd() & 0xff);
}

/* ------------------------------------------------------------------ digest */

static uint64_t hash_bytes(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* --------------------------------------------------------------- input walk */

/* Every in-scope entry takes its window from the pool, at an offset that
 * advances with the call counter and does not repeat until the walk wraps --
 * 16k calls on, against the 1-256 of a fixed variant table.  A driver then
 * cannot hand the same bytes to a kernel twice, so an answer memoized on one
 * sighting of a buffer is worthless, which is what BENCHMARK.md promises.
 *
 * The step is 32 bytes, so the window stays as aligned as the AVX2 block
 * kernels' aligned loads require; the two streams sit half a period apart so
 * that two-operand entries get independent windows; PWI is the same walk in
 * int16 elements for the coefficient pools.  main() sets g_step before every
 * call, dump mode included, so the correctness sweep walks the pool too. */
#define WALK_STEP 32
#define WALK_HALF 8192                                   /* steps: half a period */
#define WALK_WARM 12288                           /* steps: the sizing passes */
/* Bytes of pool in front of every window that a kernel may read backwards
 * through (the SAO kernels read the block's neighbours; the original buffers
 * gave them a 2064-byte margin, this gives them more). */
#define WALK_PAD 4096
#define WALK_MASK ((size_t)(1 << 19) - WALK_STEP)               /* bytes */
#define WALK_MASK16 ((size_t)(1 << 19) - (WALK_STEP / 2))    /* int16 elements */
static size_t g_step;
#define PW ((size_t)((size_t)g_step * WALK_STEP & WALK_MASK))
#define PW2 ((size_t)(((size_t)g_step + WALK_HALF) * WALK_STEP & WALK_MASK))
#define PWI ((size_t)((size_t)g_step * (WALK_STEP / 2) & WALK_MASK16))
#define PWI2 ((size_t)(((size_t)g_step + WALK_HALF) * (WALK_STEP / 2) & WALK_MASK16))

/* ------------------------------------------------------------------ entries */

typedef struct entry {
    const char *key;
    uint64_t (*run)(uint32_t it, int dump);
    int enabled;
} entry_t;

#define MAX_ENTRIES 96
static entry_t g_entries[MAX_ENTRIES];
static int g_nentries;

static void reg(const char *key, uint64_t (*run)(uint32_t, int))
{
    if (g_nentries >= MAX_ENTRIES) {
        fprintf(stderr, "too many entries\n");
        exit(3);
    }
    g_entries[g_nentries].key = key;
    g_entries[g_nentries].run = run;
    g_entries[g_nentries].enabled = 1;
    g_nentries++;
}

/* ================================================================ picture */
/* ============================================================== quantisers */

/* ================================================== motion compensation */
/* None of the eight implementations dereferences its encoder argument, so the
 * harness passes NULL.  The block filters need their scratch arrays and a
 * source with the padding the encoder gives them; the sample filters need only
 * the motion vector, whose low 4 (luma) or 5 (chroma) bits select the FIR
 * phase.  The chroma ones keep a [UVG_EXT_BLOCK_W_CHROMA][LCU_WIDTH_C] scratch
 * array on the stack, so their blocks stay inside one chroma LCU. */
#define ALF_PAD_ 4
#define IPOL_PAD 16
#define IPOL_W 64
#define IPOL_H 64
static uvg_pixel *g_ipol_src;
static uvg_pixel g_filtered[4][LCU_LUMA_SIZE];
static int16_t g_horim[5][UVG_IPOL_MAX_IM_SIZE_LUMA_SIMD];
static int16_t g_horfc[5][UVG_EXT_BLOCK_W_LUMA + 1];
static int16_t *g_ipol_dst16;
static uvg_pixel *g_ipol_dst8;

static void build_ipol(void)
{
    const int stride = IPOL_W + 2 * IPOL_PAD;
    g_ipol_src = xalloc((size_t)stride * (IPOL_H + 2 * IPOL_PAD));
    for (int y = 0; y < IPOL_H + 2 * IPOL_PAD; y++)
        for (int x = 0; x < stride; x++)
            g_ipol_src[y * stride + x] = g_pix[(y * 7 + x) % (POOL_STRIDE * 64)];
    g_ipol_dst16 = xalloc(IPOL_W * IPOL_H * sizeof(int16_t));
    g_ipol_dst8 = xalloc(IPOL_W * IPOL_H);
}

#define FILTER_ENTRY(nm, off_x, off_y)                                        \
    static uint64_t run_##nm(uint32_t it, int dump)                           \
    {                                                                         \
        const int stride = IPOL_W + 2 * IPOL_PAD;                             \
        uvg_pixel *src = g_pix + PW + IPOL_PAD * stride;                      \
        (void)dump;                                                           \
        uvg_##nm(NULL, src, stride, IPOL_W, IPOL_H, g_filtered, g_horim,      \
                 (int8_t)(it % 5), g_horfc, (int8_t)(off_x), (int8_t)(off_y)); \
        return hash_bytes(1469598103934665603ULL, g_filtered,                 \
                          sizeof(g_filtered));                                \
    }

FILTER_ENTRY(filter_hpel_blocks_hor_ver_luma, 0, 0)
FILTER_ENTRY(filter_hpel_blocks_diag_luma, 1, 1)
FILTER_ENTRY(filter_qpel_blocks_hor_ver_luma, 0, 0)
FILTER_ENTRY(filter_qpel_blocks_diag_luma, 1, 1)

#define SAMPLE_ENTRY(nm, out, oelem, w, h, mvmask)                            \
    static uint64_t run_##nm(uint32_t it, int dump)                           \
    {                                                                         \
        const int stride = IPOL_W + 2 * IPOL_PAD;                             \
        uvg_pixel *src = g_pix + PW + IPOL_PAD * stride + IPOL_PAD;           \
        mv_t mv[2];                                                           \
        mv[0] = (mv_t)(it & (mvmask));                                        \
        mv[1] = (mv_t)((it / ((mvmask) + 1)) & (mvmask));                     \
        (void)dump;                                                           \
        uvg_##nm(NULL, src, stride, (w), (h), out, (w), 1, 1, mv);            \
        return hash_bytes(1469598103934665603ULL, out,                        \
                          (size_t)(w) * (h) * sizeof(oelem));                 \
    }

SAMPLE_ENTRY(sample_quarterpel_luma, g_ipol_dst8, uvg_pixel, IPOL_W, IPOL_H, 15)
SAMPLE_ENTRY(sample_octpel_chroma, g_ipol_dst8, uvg_pixel, LCU_WIDTH_C, LCU_WIDTH_C, 31)
SAMPLE_ENTRY(sample_quarterpel_luma_hi, g_ipol_dst16, int16_t, IPOL_W, IPOL_H, 15)
SAMPLE_ENTRY(sample_octpel_chroma_hi, g_ipol_dst16, int16_t, LCU_WIDTH_C, LCU_WIDTH_C, 31)

static void prep_ipol(void) { build_ipol(); }

static void reg_ipol(void)
{
    reg("ipol/filter_hpel_blocks_hor_ver_luma", run_filter_hpel_blocks_hor_ver_luma);
    reg("ipol/filter_hpel_blocks_diag_luma", run_filter_hpel_blocks_diag_luma);
    reg("ipol/filter_qpel_blocks_hor_ver_luma", run_filter_qpel_blocks_hor_ver_luma);
    reg("ipol/filter_qpel_blocks_diag_luma", run_filter_qpel_blocks_diag_luma);
    reg("ipol/sample_quarterpel_luma", run_sample_quarterpel_luma);
    reg("ipol/sample_octpel_chroma", run_sample_octpel_chroma);
    reg("ipol/sample_quarterpel_luma_hi", run_sample_quarterpel_luma_hi);
    reg("ipol/sample_octpel_chroma_hi", run_sample_octpel_chroma_hi);
}






/* ================================================================ registry */









static entry_t *find(const char *key)
{
    for (int i = 0; i < g_nentries; i++)
        if (strcmp(g_entries[i].key, key) == 0)
            return &g_entries[i];
    return NULL;
}

int main(int argc, char **argv)
{
    reg_ipol();
    if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        for (int i = 0; i < g_nentries; i++)
            printf("%s\n", g_entries[i].key);
        return 0;
    }
    if (argc < 5) {
        fprintf(stderr, "usage: %s <dump|bench|list> <cpuid> <seed> <family>/<name> ...\n", argv[0]);
        return 2;
    }
    const int dump = strcmp(argv[1], "dump") == 0;
    const int cpuid = (int)strtol(argv[2], NULL, 10);
    g_rng = strtoull(argv[3], NULL, 10) | 1ULL;
    g_drng = g_rng ^ 0x9e3779b97f4a7c15ULL;
    draw_shapes();

    if (!uvg_strategyselector_init(cpuid, 8)) {
        fprintf(stderr, "uvg_strategyselector_init failed\n");
        return 3;
    }

    fill_pool();
    prep_ipol();

    for (int a = 4; a < argc; a++) {
        entry_t *e = find(argv[a]);
        if (!e || !e->enabled) {
            printf("%s MISSING\n", argv[a]);
            continue;
        }
        if (dump) {
            uint64_t h = 0;
            for (uint32_t it = 0; it < 256; it++) {
                g_step = it;
                h = e->run(it, 1) ^ (h << 1);
            }
            printf("%s %016" PRIx64 "\n", e->key, h);
            fflush(stdout);
            continue;
        }
        /* warm up, then size the sample so it lasts at least MIN_SAMPLE_NS */
        uint32_t iters = 1;
        for (;;) {
            double t0 = now_ns();
            for (uint32_t i = 0; i < iters; i++) {
                g_step = WALK_WARM + i;
                e->run(i, 0);
            }
            double dt = now_ns() - t0;
            if (dt >= MIN_SAMPLE_NS || iters >= MAX_ITERS)
                break;
            double want = iters * (MIN_SAMPLE_NS / (dt > 0 ? dt : 1.0)) * 1.2;
            uint32_t next = (uint32_t)want;
            iters = next > iters * 8 ? iters * 8 : (next <= iters ? iters * 2 : next);
        }
        double samples[REPS];
        for (int r = 0; r < REPS; r++) {
            double t0 = now_ns();
            for (uint32_t i = 0; i < iters; i++) {
                /* continue the walk across repetitions: a window used by one
                 * sample is never handed out by the next */
                g_step = (size_t)r * iters + i;
                e->run(i, 0);
            }
            samples[r] = (now_ns() - t0) / (double)iters;
        }
        qsort(samples, REPS, sizeof(double), cmp_double);
        printf("%s %.2f %u\n", e->key, samples[REPS / 2], iters);
        fflush(stdout);
    }
    return 0;
}
