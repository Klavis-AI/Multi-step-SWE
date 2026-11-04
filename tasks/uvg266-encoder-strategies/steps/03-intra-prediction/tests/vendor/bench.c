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

/* @family dct */
/* ============================================================== transforms */

/* uvg_dct_4x4..32x32, uvg_idct_4x4..32x32.
 *
 * The forward transforms are fed residuals: the encoder hands them the output
 * of uvg_generate_residual, a pixel difference in [-255, 255].  Feeding the
 * full int16 range instead makes the vectorised forward transforms disagree
 * with the C ones, because they accumulate in saturating 16-bit lanes -- a
 * domain error in the harness, not a difference between implementations.  The
 * pool below is filled by calling the tree's own residual generator, so the
 * inputs are exactly the encoder's. */
static int16_t *g_dctin;
#define DCT_IN_VARIANTS 1024

static void build_dctin(void)
{
    g_dctin = xalloc((size_t)DCT_IN_VARIANTS * 1024 * sizeof(int16_t));
    uvg_generate_residual(g_pix, g_pix2, g_dctin, 32, 32, POOL_STRIDE, POOL_STRIDE);
    for (int v = 0; v < DCT_IN_VARIANTS; v++)
        memcpy(g_dctin + (size_t)v * 1024, g_dctin,
               (size_t)1024 * sizeof(int16_t));
    /* successive variants walk the residual pool so consecutive calls differ */
    for (int v = 1; v < DCT_IN_VARIANTS; v++)
        uvg_generate_residual(g_pix + (size_t)v * 32, g_pix2 + (size_t)v * 32,
                              g_dctin + (size_t)v * 1024, 32, 32, POOL_STRIDE, POOL_STRIDE);
}

#define DCT_ENTRY(nm, dim)                                                    \
    static uint64_t run_##nm(uint32_t it, int dump)                           \
    {                                                                         \
        const int n = (dim) * (dim);                                          \
        const int16_t *in = g_dctin + PWI;                                    \
        int16_t *out = g_i16out + (size_t)(it % DCT_IN_VARIANTS) * 1024;      \
        (void)dump;                                                           \
        uvg_##nm(8, in, out);                                                 \
        return hash_bytes(1469598103934665603ULL, out, (size_t)n * sizeof(int16_t)); \
    }

DCT_ENTRY(dct_4x4, 4)
DCT_ENTRY(dct_8x8, 8)
DCT_ENTRY(dct_16x16, 16)
DCT_ENTRY(dct_32x32, 32)
DCT_ENTRY(idct_4x4, 4)
DCT_ENTRY(idct_8x8, 8)
DCT_ENTRY(idct_16x16, 16)
DCT_ENTRY(idct_32x32, 32)

/* The multiple-transform-selection entry points.  They choose between DCT-II,
 * DCT-VIII and DST-VII by block shape and by the transform subset the encoder
 * configured, so the driver walks the legal combinations of mts_type, colour,
 * tr_idx and block size rather than fixing one. */
static cu_info_t g_tu;
static int16_t *g_mtsout;

static void mts_params(uint32_t it, int *dim, int *mts_type, int *color, int *tr_idx)
{
    *dim = 4 << (int)((it / 8) % 4);
    {
        /* UVG_MTS_{OFF,INTER,IMPLICIT}: the encoder's default is `mts off`,
         * and these two functions are only reached with MTS enabled at all. */
        static const int mtss[3] = {UVG_MTS_OFF, UVG_MTS_INTER, UVG_MTS_IMPLICIT};
        *mts_type = mtss[(it / 3) % 3];
    }
    *color = COLOR_Y;
    /* The encoder reaches the MTS entry points with luma blocks; the chroma
     * path of these two functions draws on a different transform subset and is
     * not what the family's work is, so the workload stays luma-only. */
    /* The encoder only ever sets an explicit MTS index from the four
     * DST7/DCT8 combinations, or leaves it at MTS_SKIP; indices outside that
     * set are not reachable and the two implementations do not agree there. */
    {
        static const int trs[5] = {MTS_SKIP, 2, 3, 4, 5};
        *tr_idx = (*dim == 32) ? 5 : trs[(it / 5) % 5];
        /* 32x32 admits only DCT8/DCT8 in explicit MTS, so nothing else is
         * reachable there and the two implementations are free to differ. */
    }
    memset(&g_tu, 0, sizeof(g_tu));
    g_tu.type = CU_INTRA;
    g_tu.tr_idx = (int8_t)*tr_idx;
}

static void build_mtsout(void)
{
    g_mtsout = xalloc(64 * 64 * sizeof(int16_t));
}

#define MTS_ENTRY(nm)                                                         \
    static uint64_t run_##nm(uint32_t it, int dump)                           \
    {                                                                         \
        int dim, mts_type, color, tr_idx;                                     \
        const int16_t *in = g_dctin + PWI;                                    \
        mts_params(it, &dim, &mts_type, &color, &tr_idx);                     \
        (void)dump; (void)tr_idx;                                             \
        memset(g_mtsout, 0, 64 * 64 * sizeof(int16_t));                       \
        uvg_##nm(8, (color_t)color, &g_tu, (int8_t)dim, (int8_t)dim, in,      \
                 g_mtsout, (int8_t)mts_type);                                 \
        return hash_bytes(1469598103934665603ULL, g_mtsout,                   \
                          (size_t)dim * dim * sizeof(int16_t));               \
    }

MTS_ENTRY(mts_dct)
MTS_ENTRY(mts_idct)



/* @endfamily */
/* ================================================================ picture */
/* @family picture */

/* The NxN cost kernels take no stride: they read a tightly packed n*n block,
 * and the AVX2 twins use aligned 256-bit loads, so every block handed to them
 * must be 32-byte aligned.  Every variant is built once, before timing starts,
 * so no entry pays input-construction cost inside the timed loop. */
#define NVARS 64

enum { SLOT_4, SLOT_8, SLOT_16, SLOT_32, SLOT_64, NSLOTS };
static const int g_slot_dim[NSLOTS] = {4, 8, 16, 32, 64};
static uvg_pixel *g_blocks[NSLOTS][2][NVARS];

static void build_blocks(void)
{
    for (int sl = 0; sl < NSLOTS; sl++) {
        const int dim = g_slot_dim[sl];
        const size_t stride = ((size_t)dim * dim + 31) & ~(size_t)31;
        for (int side = 0; side < 2; side++) {
            for (int v = 0; v < NVARS; v++) {
                uvg_pixel *dst = xalloc(stride);
                const uvg_pixel *pool = side ? g_pix2 : g_pix;
                const int ox = (v * 7 + dim + side * 13) % (POOL_STRIDE - dim);
                const int oy = (v * 5 + dim + side * 11) % (POOL_ROWS - dim);
                for (int y = 0; y < dim; y++)
                    memcpy(dst + (size_t)y * dim,
                           pool + (size_t)(oy + y) * POOL_STRIDE + ox, dim);
                g_blocks[sl][side][v] = dst;
            }
        }
    }
}

#define COST_ENTRY(nm, slot)                                                  \
    static uint64_t run_##nm(uint32_t it, int dump)                           \
    {                                                                         \
        (void)it;                                                             \
        unsigned r = uvg_##nm(g_pix + PW, g_pix2 + PW2);                      \
        (void)dump;                                                           \
        return hash_bytes(1469598103934665603ULL, &r, sizeof(r));             \
    }

COST_ENTRY(sad_4x4, SLOT_4)
COST_ENTRY(sad_8x8, SLOT_8)
COST_ENTRY(sad_16x16, SLOT_16)
COST_ENTRY(sad_32x32, SLOT_32)
COST_ENTRY(sad_64x64, SLOT_64)
COST_ENTRY(satd_4x4, SLOT_4)
COST_ENTRY(satd_8x8, SLOT_8)
COST_ENTRY(satd_16x16, SLOT_16)
COST_ENTRY(satd_32x32, SLOT_32)
COST_ENTRY(satd_64x64, SLOT_64)

/* dual cost: preds is a pred_buffer = uvg_pixel (*)[32*32]; the kernel reads
 * preds[m][0 .. n*n-1] for each mode, packed from each mode's own start, so a
 * 64x64 mode legitimately runs past its 32*32 storage and each variant is
 * allocated with room for that. */
#define DUAL_ENTRY(nm, slot)                                                  \
    static uvg_pixel (*g_dp_##nm)[32 * 32];                                   \
    static void build_##nm(void)                                              \
    {                                                                         \
        const int dim = g_slot_dim[slot];                                     \
        g_dp_##nm = xalloc((size_t)NVARS * 16 * 32 * 32);                     \
        for (int v = 0; v < NVARS; v++) {                                     \
            uvg_pixel *base = (uvg_pixel *)(g_dp_##nm + (size_t)v * 16);      \
            for (int m = 0; m < 4; m++)                                       \
                memcpy(base + (size_t)m * 32 * 32,                            \
                       g_blocks[slot][0][(v + m) % NVARS], (size_t)dim * dim);\
        }                                                                     \
    }                                                                         \
    static uint64_t run_##nm(uint32_t it, int dump)                           \
    {                                                                         \
        unsigned costs[4];                                                    \
        memset(costs, 0, sizeof(costs));                                      \
        uvg_##nm((const pred_buffer)(g_pix + PW), g_pix2 + PW2, 2, costs);    \
        (void)it; (void)dump;                                                 \
        return hash_bytes(1469598103934665603ULL, costs, sizeof(costs));      \
    }

DUAL_ENTRY(sad_4x4_dual, SLOT_4)
DUAL_ENTRY(sad_8x8_dual, SLOT_8)
DUAL_ENTRY(sad_16x16_dual, SLOT_16)
DUAL_ENTRY(sad_32x32_dual, SLOT_32)
DUAL_ENTRY(sad_64x64_dual, SLOT_64)
DUAL_ENTRY(satd_4x4_dual, SLOT_4)
DUAL_ENTRY(satd_8x8_dual, SLOT_8)
DUAL_ENTRY(satd_16x16_dual, SLOT_16)
DUAL_ENTRY(satd_32x32_dual, SLOT_32)
DUAL_ENTRY(satd_64x64_dual, SLOT_64)

static void build_duals(void)
{
    build_sad_4x4_dual(); build_sad_8x8_dual(); build_sad_16x16_dual();
    build_sad_32x32_dual(); build_sad_64x64_dual();
    build_satd_4x4_dual(); build_satd_8x8_dual(); build_satd_16x16_dual();
    build_satd_32x32_dual(); build_satd_64x64_dual();
}


/* bipred_average: lcu->rec is written only and never read; the two prediction
 * lists are tightly packed pixel planes.  The mixed px/im forms dereference
 * both lists, so they must all be valid whatever the flags say. */
static lcu_t *g_bip_lcu;
static yuv_t *g_bip_px0, *g_bip_px1;
static yuv_im_t *g_bip_im0, *g_bip_im1;
static uvg_pixel *g_bip_p0, *g_bip_p1;
static int16_t *g_bip_i0, *g_bip_i1;

static void build_bipred(void)
{
    g_bip_lcu = xalloc(sizeof(lcu_t));
    g_bip_px0 = xalloc(sizeof(yuv_t));
    g_bip_px1 = xalloc(sizeof(yuv_t));
    g_bip_im0 = xalloc(sizeof(yuv_im_t));
    g_bip_im1 = xalloc(sizeof(yuv_im_t));
    g_bip_p0 = xalloc(64 * 64);
    g_bip_p1 = xalloc(64 * 64);
    g_bip_i0 = xalloc(64 * 64 * sizeof(int16_t));
    g_bip_i1 = xalloc(64 * 64 * sizeof(int16_t));
    for (int i = 0; i < 64 * 64; i++) {
        g_bip_p0[i] = g_pix[i];
        g_bip_p1[i] = g_pix2[i];
        g_bip_i0[i] = (int16_t)((int)g_pix[i] - 128);
        g_bip_i1[i] = (int16_t)((int)g_pix2[i] - 128);
    }
    memset(g_bip_lcu, 0, sizeof(lcu_t));
    memset(g_bip_px0, 0, sizeof(yuv_t));
    memset(g_bip_px1, 0, sizeof(yuv_t));
    memset(g_bip_im0, 0, sizeof(yuv_im_t));
    memset(g_bip_im1, 0, sizeof(yuv_im_t));
    g_bip_px0->y = g_bip_p0;
    g_bip_px1->y = g_bip_p1;
    g_bip_px0->u = g_bip_p0;
    g_bip_px1->u = g_bip_p1;
    g_bip_px0->v = g_bip_p0;
    g_bip_px1->v = g_bip_p1;
    g_bip_im0->y = g_bip_i0;
    g_bip_im1->y = g_bip_i1;
    g_bip_im0->u = g_bip_i0;
    g_bip_im1->u = g_bip_i1;
    g_bip_im0->v = g_bip_i0;
    g_bip_im1->v = g_bip_i1;
}

static uint64_t run_bipred_average(uint32_t it, int dump)
{
    static const int dims[] = {8, 16, 32, 64};
    const int dim = dims[(it / 4) % 4];
    const int flags = (int)(it % 4);   /* px_px, px_im, im_px, px_px */
    const bool pc = (it % 8) >= 4;
    (void)dump;
    /* both prediction lists walk the pool: same layout, fresh bytes */
    g_bip_px0->y = g_bip_px0->u = g_bip_px0->v = g_pix + PW;
    g_bip_px1->y = g_bip_px1->u = g_bip_px1->v = g_pix2 + PW2;
    g_bip_im0->y = g_bip_im0->u = g_bip_im0->v = g_i16 + PWI;
    g_bip_im1->y = g_bip_im1->u = g_bip_im1->v = g_i16b + PWI2;
    uvg_bipred_average(g_bip_lcu, g_bip_px0, g_bip_px1, g_bip_im0, g_bip_im1, 0, 0, dim, dim,
                       (unsigned)(flags & 1), (unsigned)((flags >> 1) & 1), true, pc);
    return hash_bytes(1469598103934665603ULL, g_bip_lcu->rec.y, 64 * 64);
}

static uint64_t run_reg_sad(uint32_t it, int dump)
{
    const int d = (int)(it % 6);
    unsigned r = uvg_reg_sad(g_pix + PW, g_pix2 + PW2, g_regsad_dims[d][0], g_regsad_dims[d][1],
                             POOL_STRIDE, POOL_STRIDE);
    (void)dump;
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

static uint64_t run_satd_any_size(uint32_t it, int dump)
{
    const int d = (int)(it % 4);
    unsigned r = uvg_satd_any_size(g_anysize_dims[d][0], g_anysize_dims[d][1], g_pix + PW,
                                   POOL_STRIDE, g_pix2 + PW2, POOL_STRIDE);
    (void)dump;
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

/* The encoder calls this as
 *   uvg_satd_any_size_quad(w, h, (const uvg_pixel **)filtered, LCU_WIDTH, orig, stride, 4, costs, valid)
 * so the predicate blocks are strided by LCU_WIDTH and each mode is its own
 * pointer. */
#define LCU_WIDTH_ 64
static uvg_pixel *g_quad[4][NVARS];

static void build_quad(void)
{
    for (int v = 0; v < NVARS; v++)
        for (int m = 0; m < 4; m++) {
            g_quad[m][v] = xalloc(LCU_WIDTH_ * LCU_WIDTH_);
            for (int y = 0; y < 32; y++)
                memcpy(g_quad[m][v] + (size_t)y * LCU_WIDTH_,
                       g_blocks[SLOT_32][0][(v + m) % NVARS] + (size_t)y * 32, 32);
        }
}

static uint64_t run_satd_any_size_quad(uint32_t it, int dump)
{
    unsigned costs[4];
    const uvg_pixel *preds[4];
    static int8_t valid[4];
    (void)it;
    for (int m = 0; m < 4; m++)
        preds[m] = g_pix + PW + (size_t)m * 1024;
    memset(costs, 0, sizeof(costs));
    uvg_satd_any_size_quad(g_quaddim[0], g_quaddim[1], preds, LCU_WIDTH_, g_pix2 + PW2, POOL_STRIDE, 4,
                           costs, valid);
    (void)dump;
    return hash_bytes(1469598103934665603ULL, costs, sizeof(costs));
}

#define PIX_ENTRY(nm, expr)                                                   \
    static uint64_t run_##nm(uint32_t it, int dump)                           \
    {                                                                         \
        const size_t v = PW / WALK_STEP;                                      \
        (void)it; (void)dump;                                                 \
        return (expr);                                                        \
    }

static uint64_t pixhash_u32(uint32_t r)
{
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

PIX_ENTRY(pixels_calc_ssd,
          pixhash_u32(uvg_pixels_calc_ssd(g_pix + (size_t)v * 32, g_rec + (size_t)v * 32,
                                          POOL_STRIDE, POOL_STRIDE, 32, 32)))
PIX_ENTRY(crc32c_4x4, pixhash_u32(uvg_crc32c_4x4(g_pix + (size_t)v * 32, POOL_STRIDE)))
PIX_ENTRY(crc32c_8x8, pixhash_u32(uvg_crc32c_8x8(g_pix + (size_t)v * 32, POOL_STRIDE)))
PIX_ENTRY(ver_sad,
          pixhash_u32(uvg_ver_sad(g_pix + (size_t)v * 32, g_pix2 + (size_t)v * 32, 32, 16,
                                  POOL_STRIDE)))
PIX_ENTRY(hor_sad,
          pixhash_u32(uvg_hor_sad(g_pix + (size_t)v * 32, g_pix2 + (size_t)v * 32, 32, 16,
                                  POOL_STRIDE, POOL_STRIDE, 0, 31)))
PIX_ENTRY(pixel_var, pixhash_u32((uint32_t)uvg_pixel_var(g_pix + (size_t)v * WALK_STEP, 1024)))

static uint64_t run_get_optimized_sad(uint32_t it, int dump)
{
    optimized_sad_func_ptr_t f = uvg_get_optimized_sad((int32_t)(it % 4));
    unsigned r = 0;
    (void)dump;
    if (f)
        r = f(g_pix + PW, g_pix2 + PW2, 16, POOL_STRIDE, POOL_STRIDE);
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

static int16_t *g_res[NVARS];

static void build_res(void)
{
    for (int v = 0; v < NVARS; v++) {
        g_res[v] = xalloc(64 * 64 * sizeof(int16_t));
        uvg_generate_residual(g_pix + (size_t)v * 32, g_pix2 + (size_t)v * 32, g_res[v],
                              g_gentr[0], g_gentr[1], POOL_STRIDE, POOL_STRIDE);
    }
}

static uint64_t run_generate_residual(uint32_t it, int dump)
{
    /* This entry used to hash a buffer that build_res had built once, so
     * the function it names was never called in the timed loop and its ratio
     * was 1.0 by construction.  It is called here, on the walked window. */
    const int v = (int)(it % NVARS);
    (void)dump;
    uvg_generate_residual(g_pix + PW, g_pix2 + PW2, g_res[v], g_gres[0], g_gres[1],
                          POOL_STRIDE, POOL_STRIDE);
    return hash_bytes(1469598103934665603ULL, g_res[v], (size_t)g_gres[0] * g_gres[1] * sizeof(int16_t));
}


/* @endfamily */
/* ============================================================== quantisers */

/* @family ipol */
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

/* @family ipol */
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
/* @endfamily */
/* @endfamily */

/* @family sao */
/* ============================================ sample adaptive offset */
/* Two of the four take no struct argument at all.  sao_reconstruct_color only
 * reaches its encoder on the BAND path, and the AVX2 twin never touches it;
 * sao_band_ddistortion only reaches its state when it falls back to the
 * generic kernel, which needs encoder_control->bitdepth.  A minimally
 * populated encoder_state_t covers both, and the AVX2 band kernel's fast path
 * wants a block width that is a multiple of 32. */
static uvg_pixel *g_sao_orig, *g_sao_rec, *g_sao_new;
static encoder_control_t g_sao_enc;
static encoder_state_t g_sao_state;
static sao_info_t g_sao_info;
static int g_sao_off[NUM_SAO_EDGE_CATEGORIES];
static int g_sao_cat[2][NUM_SAO_EDGE_CATEGORIES];
static int g_sao_bands[4];

/* The edge paths read the block's neighbours, and the encoder only ever hands
 * them interior blocks, so the harness gives the block a margin and passes the
 * interior pointer, exactly as the encoder does. */
#define SAO_PAD 16
#define SAO_STRIDE 128
static void build_sao(void)
{
    g_sao_orig = xalloc((size_t)SAO_STRIDE * 128);
    g_sao_rec = xalloc((size_t)SAO_STRIDE * 128);
    g_sao_new = xalloc((size_t)SAO_STRIDE * 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < SAO_STRIDE; x++) {
            g_sao_orig[y * SAO_STRIDE + x] = g_pix[(y * 3 + x) % (POOL_STRIDE * 64)];
            g_sao_rec[y * SAO_STRIDE + x] = g_pix2[(y * 5 + x) % (POOL_STRIDE * 64)];
        }
    g_sao_orig += SAO_PAD * SAO_STRIDE + SAO_PAD;
    g_sao_rec += SAO_PAD * SAO_STRIDE + SAO_PAD;
    g_sao_new += SAO_PAD * SAO_STRIDE + SAO_PAD;
    memset(&g_sao_enc, 0, sizeof(g_sao_enc));
    memset(&g_sao_state, 0, sizeof(g_sao_state));
    g_sao_enc.bitdepth = 8;
    g_sao_state.encoder_control = &g_sao_enc;
    memset(&g_sao_info, 0, sizeof(g_sao_info));
    g_sao_info.type = SAO_TYPE_EDGE;
    g_sao_info.eo_class = SAO_EO0;
    for (int i = 0; i < NUM_SAO_EDGE_CATEGORIES; i++)
        g_sao_off[i] = (i % 3) - 1;
    for (int i = 0; i < 4; i++)
        g_sao_bands[i] = 1 << (i + 2);
}

static uint64_t run_sao_edge_ddistortion(uint32_t it, int dump)
{
    const int eo = (int)(it % 4);
    (void)dump;
    int r = uvg_sao_edge_ddistortion(g_pix + WALK_PAD + PW, g_pix2 + WALK_PAD + PW2, 64, 64, eo, g_sao_off);
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

static uint64_t run_calc_sao_edge_dir(uint32_t it, int dump)
{
    const int eo = (int)(it % 4);
    memset(g_sao_cat, 0, sizeof(g_sao_cat));
    (void)dump;
    uvg_calc_sao_edge_dir(g_pix + WALK_PAD + PW, g_pix2 + WALK_PAD + PW2, eo, 64, 64, g_sao_cat);
    return hash_bytes(1469598103934665603ULL, g_sao_cat, sizeof(g_sao_cat));
}

static uint64_t run_sao_band_ddistortion(uint32_t it, int dump)
{
    const int bp = (int)(it % 32);
    (void)dump;
    int r = uvg_sao_band_ddistortion(&g_sao_state, g_pix + WALK_PAD + PW, g_pix2 + WALK_PAD + PW2, 64, 64, bp, g_sao_bands);
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

static uint64_t run_sao_reconstruct_color(uint32_t it, int dump)
{
    /* alternate between the two paths: BAND reaches the encoder argument,
     * EDGE does not */
    if (it % 2) {
        g_sao_info.type = SAO_TYPE_EDGE;
        g_sao_info.eo_class = (sao_eo_class)(it % 4);
        for (int i = 0; i < NUM_SAO_EDGE_CATEGORIES; i++)
            g_sao_info.offsets[i] = (i % 5) - 2;
    } else {
        g_sao_info.type = SAO_TYPE_BAND;
        g_sao_info.band_position[0] = (int)(it % 28);
        g_sao_info.band_position[1] = (int)(it % 28);
        for (int i = 0; i < NUM_SAO_EDGE_CATEGORIES; i++)
            g_sao_info.offsets[i] = (i % 5) - 2;
    }
    for (int y = 0; y < 64; y++)
        memset(g_sao_new + y * SAO_STRIDE, 0, 64);
    (void)dump;
    uvg_sao_reconstruct_color(&g_sao_enc, g_pix2 + WALK_PAD + PW2, g_sao_new, &g_sao_info,
                              SAO_STRIDE, SAO_STRIDE, 64, 64, (color_t)(it / 2 % 3));
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < 64; y++)
        h = hash_bytes(h, g_sao_new + y * SAO_STRIDE, 64);
    return h;
}

static void prep_sao(void) { build_sao(); }

/* @family sao */
static void reg_sao(void)
{
    reg("sao/sao_edge_ddistortion", run_sao_edge_ddistortion);
    reg("sao/calc_sao_edge_dir", run_calc_sao_edge_dir);
    reg("sao/sao_reconstruct_color", run_sao_reconstruct_color);
    reg("sao/sao_band_ddistortion", run_sao_band_ddistortion);
}
/* @endfamily */
/* @endfamily */


/* @family intra */
/* ============================================== intra prediction */
/* cu_loc_t is read only for its size fields.  The reference buffers are the
 * full uvg_intra_ref arrays the encoder keeps, with index 0 as the corner
 * sample; mip_predict's generic path writes a whole 32x32 block whatever the
 * requested size, so its destination is that large. */
#define INTRA_MAX 32
static uvg_intra_ref g_iref_above, g_iref_left;
static uvg_intra_references g_irefs;
static uvg_pixel *g_intra_dst;
static cu_loc_t g_culoc;

static void build_intra(void)
{
    g_intra_dst = xalloc(64 * 64);
    for (int i = 0; i < INTRA_REF_LENGTH; i++) {
        g_iref_above.left[i] = g_pix[(i * 3) % (POOL_STRIDE * 64)];
        g_iref_above.top[i] = g_pix[(i * 5 + 7) % (POOL_STRIDE * 64)];
        g_iref_left.left[i] = g_pix2[(i * 3 + 1) % (POOL_STRIDE * 64)];
        g_iref_left.top[i] = g_pix2[(i * 7 + 3) % (POOL_STRIDE * 64)];
        g_irefs.ref.top[i] = g_pix[(i * 11) % (POOL_STRIDE * 64)];
        g_irefs.ref.left[i] = g_pix2[(i * 13) % (POOL_STRIDE * 64)];
    }
    g_irefs.filtered_initialized = false;
}

/* The two entries below take a reference *struct*, so their window cannot be a
 * pointer into the pool: the 2*dim+3 samples these kernels read are rewritten
 * in place on every call instead -- O(dim) against their O(dim*dim) work, and
 * inside the 0-255 range BENCHMARK.md publishes.  A memoized answer keyed on
 * the struct (which never moves) is stale by the next call, and the
 * correctness sweep compares it against this tree's own kernels over the same
 * walk. */
static void intra_ref_walk(size_t step, int dim)
{
    const int want = 2 * dim + 3;
    const int n = INTRA_REF_LENGTH < want ? INTRA_REF_LENGTH : want;
    for (int i = 0; i < n; i++) {
        const unsigned v = (unsigned)(i * 37) + (unsigned)step * 11u;
        g_irefs.ref.top[i] = (uvg_pixel)(v & 0xffu);
        g_irefs.ref.left[i] = (uvg_pixel)((v >> 7) & 0xffu);
    }
}

#define INTRA_LOC(dim)                                                        \
    do {                                                                      \
        memset(&g_culoc, 0, sizeof(g_culoc));                                 \
        g_culoc.width = (uint8_t)(dim);                                       \
        g_culoc.height = (uint8_t)(dim);                                      \
        g_culoc.chroma_width = (uint8_t)(dim);                                \
        g_culoc.chroma_height = (uint8_t)(dim);                               \
    } while (0)

static uint64_t run_angular_pred(uint32_t it, int dump)
{
    /* The plain angular domain only.  The encoder can also reach this kernel
     * with wide-angle corrections outside 2..66, but the generic and vectorised
     * implementations do not agree there, so nothing is measured there. */
    static const int modes[] = {2, 18, 34, 50, 66, 12, 40, 64, 3, 33,
                                35, 65, 4, 28, 44, 60};
    const int nmodes = (int)(sizeof(modes) / sizeof(modes[0]));
    const int color_i = COLOR_Y;
    const int dim = 4 << (int)((it / nmodes) % 4);
    const int mode = modes[it % nmodes];
    memset(&g_culoc, 0, sizeof(g_culoc));
    g_culoc.width = g_culoc.height = (uint8_t)(4 << (int)((it / nmodes) % 4));
    g_culoc.chroma_width = g_culoc.chroma_height = (uint8_t)dim;
    memset(g_intra_dst, 0, 64 * 64);
    (void)dump;
    uvg_angular_pred(&g_culoc, (int_fast8_t)mode, (int_fast8_t)color_i, g_pix + PW,
                     g_pix2 + PW2, g_intra_dst, (uint8_t)0,
                     (uint8_t)0, (int)dim);
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < dim; y++)
        h = hash_bytes(h, g_intra_dst + y * dim, dim);
    return h;
}

static uint64_t run_intra_pred_planar(uint32_t it, int dump)
{
    const int color_i = COLOR_Y;
    const int dim = 4 << (int)((it / 3) % 4);
    INTRA_LOC(dim);
    memset(g_intra_dst, 0, 64 * 64);
    (void)dump;
    uvg_intra_pred_planar(&g_culoc, (color_t)color_i, g_pix + PW,
                          g_pix2 + PW2, g_intra_dst);
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < dim; y++)
        h = hash_bytes(h, g_intra_dst + y * dim, dim);
    return h;
}

static uint64_t run_pdpc_planar_dc(uint32_t it, int dump)
{
    const int dim = 4 << (int)((it / 2) % 4);
    INTRA_LOC(dim);
    memset(g_intra_dst, 0, 64 * 64);
    (void)dump;
    intra_ref_walk(g_step, dim);
    uvg_pdpc_planar_dc((int)(it % 2), &g_culoc, COLOR_Y, &g_irefs.ref, g_intra_dst);
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < dim; y++)
        h = hash_bytes(h, g_intra_dst + y * dim, dim);
    return h;
}

static uint64_t run_mip_predict(uint32_t it, int dump)
{
    static const int dims[] = {4, 8, 16, 32};
    const int dim = dims[(it / 24) % 4];
    int mip_mode = 0;
    if (dim == 4) mip_mode = (int)(it % 16);
    else if (dim == 8) mip_mode = (int)(it % 8);
    else mip_mode = (int)(it % 6);
    memset(g_intra_dst, 0, 64 * 64);
    (void)dump;
    intra_ref_walk(g_step, dim);
    uvg_mip_predict(&g_irefs, (uint16_t)dim, (uint16_t)dim, g_intra_dst, mip_mode,
                    (bool)(it & 1));
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < dim; y++)
        h = hash_bytes(h, g_intra_dst + y * dim, dim);
    return h;
}

static void prep_intra(void) { build_intra(); }

/* @family intra */
static void reg_intra(void)
{
    reg("intra/angular_pred", run_angular_pred);
    reg("intra/intra_pred_planar", run_intra_pred_planar);
    reg("intra/pdpc_planar_dc", run_pdpc_planar_dc);
    reg("intra/mip_predict", run_mip_predict);
}
/* @endfamily */
/* @endfamily */



/* @endfamily */
/* ================================================================ registry */

/* @family dct */
static void prep_dct(void) { build_dctin(); build_mtsout(); }
/* @endfamily */

/* @family picture */
static void prep_picture(void)
{
    build_blocks();
    build_duals();
    build_quad();
    build_res();
    build_bipred();
}
/* @endfamily */

/* @family alf */
/* ================================================ adaptive loop filter */
/* alf_get_blk_stats never touches its state argument (both twins), so the
 * harness passes NULL; the accumulator must be zeroed and the clipping values
 * set.  The two filter kernels read encoder_control->bitdepth, and the luma
 * (7x7) one also walks the frame's per-block classifier. */
#define ALF_NC 25
static alf_covariance *g_alf_cov;
static uvg_pixel *g_alf_org, *g_alf_rec;
static encoder_control_t g_alf_enc;
static encoder_state_t g_alf_state;
static encoder_state_config_tile_t g_alf_tile;
static videoframe_t g_alf_video;
static alf_info_t g_alf_info;
static alf_classifier **g_alf_class;
static alf_classifier *g_alf_class_rows;
static uvg_picture g_alf_pic;
static uvg_pixel *g_alf_picbuf;
static int16_t g_alf_clip[MAX_NUM_CHANNEL_TYPE][MAX_ALF_NUM_CLIPPING_VALUES];
static short g_alf_fset[MAX_NUM_ALF_CLASSES * MAX_NUM_ALF_LUMA_COEFF + 32];
static int16_t g_alf_fclip[MAX_NUM_ALF_CLASSES * MAX_NUM_ALF_LUMA_COEFF + 32];
static uvg_pixel g_alf_dst[64 * 72];

static void build_alf(void)
{
    const int W = 64 + 2 * ALF_PAD_, H = 64 + 2 * ALF_PAD_;
    g_alf_cov = xalloc(ALF_NC * sizeof(alf_covariance));
    g_alf_org = xalloc(64 * 64);
    g_alf_rec = xalloc((size_t)(W) * (H));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            g_alf_rec[y * W + x] = g_pix[(y * 3 + x) % (POOL_STRIDE * 64)];
    for (int i = 0; i < 64 * 64; i++)
        g_alf_org[i] = g_pix2[i % (POOL_STRIDE * 64)];
    for (int c = 0; c < MAX_NUM_CHANNEL_TYPE; c++)
        for (int i = 0; i < MAX_ALF_NUM_CLIPPING_VALUES; i++)
            g_alf_clip[c][i] = (int16_t)(1 << (i + 4));
    for (int i = 0; i < (int)(sizeof(g_alf_fset) / sizeof(g_alf_fset[0])); i++) {
        g_alf_fset[i] = (short)((i % 17) - 8);
        g_alf_fclip[i] = (int16_t)(1 << ((i % 4) + 5));
    }
    memset(&g_alf_enc, 0, sizeof(g_alf_enc));
    memset(&g_alf_state, 0, sizeof(g_alf_state));
    memset(&g_alf_tile, 0, sizeof(g_alf_tile));
    memset(&g_alf_video, 0, sizeof(g_alf_video));
    memset(&g_alf_info, 0, sizeof(g_alf_info));
    memset(&g_alf_pic, 0, sizeof(g_alf_pic));
    g_alf_enc.bitdepth = 8;
    g_alf_state.encoder_control = &g_alf_enc;
    g_alf_state.tile = &g_alf_tile;
    g_alf_tile.frame = &g_alf_video;
    g_alf_picbuf = xalloc((size_t)(W) * (H));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            g_alf_picbuf[y * W + x] = g_pix[(y * 5 + x * 3) % (POOL_STRIDE * 64)];
    g_alf_pic.y = g_alf_picbuf;
    g_alf_pic.stride = W;
    g_alf_video.rec = &g_alf_pic;
    g_alf_class_rows = xalloc((size_t)(H) * W * sizeof(alf_classifier));
    g_alf_class = xalloc((size_t)H * sizeof(alf_classifier *));
    for (int y = 0; y < H; y++) {
        g_alf_class[y] = g_alf_class_rows + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            g_alf_class[y][x].class_idx = (uint8_t)((y * W + x) % ALF_NC);
            g_alf_class[y][x].transpose_idx = (uint8_t)((y + x) % 4);
        }
    }
    g_alf_info.classifier = g_alf_class;
    g_alf_video.alf_info = &g_alf_info;
}

static uint64_t run_alf_get_blk_stats(uint32_t it, int dump)
{
    const uint32_t channel = (uint32_t)(it % 2);
    memset(g_alf_cov, 0, ALF_NC * sizeof(alf_covariance));
    (void)dump;
    uvg_alf_get_blk_stats(NULL, (channel_type)(channel == 0 ? CHANNEL_TYPE_LUMA : CHANNEL_TYPE_CHROMA),
                          g_alf_cov, NULL, g_alf_org, 64, g_alf_rec, 64 + 2 * ALF_PAD_,
                          0, 0, 0, 0, 64, 64, LCU_WIDTH, LCU_WIDTH - 4, g_alf_clip);
    return hash_bytes(1469598103934665603ULL, g_alf_cov,
                      sizeof(alf_covariance) * (channel == 0 ? ALF_NC : 1));
}

static uint64_t run_alf_filter_5x5_blk(uint32_t it, int dump)
{
    clp_rng rng;
    memset(g_alf_dst, 0, sizeof(g_alf_dst));
    rng.min = 0;
    rng.max = 255;
    rng.bd = 8;
    rng.n = 0;
    (void)dump;
    uvg_alf_filter_5x5_blk(&g_alf_state, g_alf_rec, g_alf_dst, 64 + 2 * ALF_PAD_, 64,
                           g_alf_fset, g_alf_fclip, rng, 64, 64, 0, 0, 0, 0,
                           (int)(it % 2), LCU_WIDTH);
    return hash_bytes(1469598103934665603ULL, g_alf_dst, 64 * 64);
}

static uint64_t run_alf_filter_7x7_blk(uint32_t it, int dump)
{
    clp_rng rng;
    memset(g_alf_dst, 0, sizeof(g_alf_dst));
    rng.min = 0;
    rng.max = 255;
    rng.bd = 8;
    rng.n = 0;
    (void)dump;
    uvg_alf_filter_7x7_blk(&g_alf_state, g_alf_rec, g_alf_dst, 64 + 2 * ALF_PAD_, 64,
                           g_alf_fset, g_alf_fclip, rng, 64, 64, 0, 0, 0, 0,
                           (int)(it % 2), LCU_WIDTH);
    return hash_bytes(1469598103934665603ULL, g_alf_dst, 64 * 64);
}

static uint64_t run_alf_derive_classification_blk(uint32_t it, int dump)
{
    (void)dump;
    uvg_alf_derive_classification_blk(&g_alf_state, 12, 32, 32, 4, 4, ALF_PAD_, ALF_PAD_,
                                      LCU_WIDTH, LCU_WIDTH - 4);
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < 32; y++)
        h = hash_bytes(h, g_alf_class[ALF_PAD_ + y] + ALF_PAD_, 32 * sizeof(alf_classifier));
    return h;
}

static void prep_alf(void) { build_alf(); }

/* @family alf */
static void reg_alf(void)
{
    reg("alf/alf_get_blk_stats", run_alf_get_blk_stats);
    reg("alf/alf_filter_5x5_blk", run_alf_filter_5x5_blk);
    reg("alf/alf_filter_7x7_blk", run_alf_filter_7x7_blk);
    reg("alf/alf_derive_classification_blk", run_alf_derive_classification_blk);
}
/* @endfamily */
/* @endfamily */

/* @family quant */
/* ============================== quantization and dequantization */
/* Every one of these reads a minimal slice of encoder_state_t: the bit depth,
 * the QP, the slice type, and a handful of config switches left at zero so the
 * deep optional paths (LMCS reshaping, dependent quantization, RDOQ, scaling
 * lists, LFNST) stay off.  quantize_residual dereferences
 * state->tile->frame->lmcs_aps unconditionally, so that pointer chain is built
 * even though nothing behind it is read. */
static encoder_control_t g_qenc;
static encoder_state_t g_qstate;
static encoder_state_config_frame_t g_qframe;
static encoder_state_config_tile_t g_qtile;
static videoframe_t g_qvideo;
static struct lmcs_aps g_qlmcs;
static cu_info_t g_qcu;
static coeff_t *g_qcoef, *g_qqcoef;
static uvg_pixel *g_qref, *g_qpred, *g_qrec;

static void build_quant(void)
{
    const size_t n = 32 * 32;
    g_qcoef = xalloc(n * sizeof(coeff_t));
    g_qqcoef = xalloc(n * sizeof(coeff_t));
    g_qref = xalloc(64 * 64);
    g_qpred = xalloc(64 * 64);
    g_qrec = xalloc(64 * 64);
    for (size_t i = 0; i < n; i++)
        g_qcoef[i] = (coeff_t)((int)(i * 37 % 2049) - 1024);
    for (size_t i = 0; i < 64 * 64; i++) {
        g_qref[i] = g_pix[i];
        g_qpred[i] = g_pix2[i];
    }
    memset(&g_qenc, 0, sizeof(g_qenc));
    memset(&g_qstate, 0, sizeof(g_qstate));
    memset(&g_qframe, 0, sizeof(g_qframe));
    memset(&g_qtile, 0, sizeof(g_qtile));
    memset(&g_qvideo, 0, sizeof(g_qvideo));
    memset(&g_qlmcs, 0, sizeof(g_qlmcs));
    memset(&g_qcu, 0, sizeof(g_qcu));
    g_qenc.bitdepth = 8;
    g_qstate.encoder_control = &g_qenc;
    g_qstate.qp = 32;
    g_qstate.frame = &g_qframe;
    g_qstate.tile = &g_qtile;
    g_qframe.slicetype = UVG_SLICE_I;
    g_qtile.frame = &g_qvideo;
    g_qvideo.lmcs_aps = &g_qlmcs;
    g_qcu.type = CU_INTRA;
}

static uint64_t run_dequant(uint32_t it, int dump)
{
    const int dim = 4 << (int)((it / 6) % 4);
    memset(g_qqcoef, 0, 32 * 32 * sizeof(coeff_t));
    g_qstate.qp = (int8_t)(16 + (it % 40));
    (void)dump;
    uvg_dequant(&g_qstate, (coeff_t *)(g_i16 + PWI), g_qqcoef, dim, dim,
                (color_t)((it / 24) % 3), (int8_t)(it % 3), 0);
    return hash_bytes(1469598103934665603ULL, g_qqcoef, (size_t)dim * dim * sizeof(coeff_t));
}

static uint64_t run_quant(uint32_t it, int dump)
{
    const int dim = 4 << (int)((it / 6) % 4);
    memset(g_qqcoef, 0, 32 * 32 * sizeof(coeff_t));
    g_qstate.qp = (int8_t)(16 + (it % 40));
    (void)dump;
    uvg_quant(&g_qstate, (coeff_t *)(g_i16 + PWI), g_qqcoef, dim, dim,
              (color_t)((it / 24) % 3), (int8_t)(it % 3), (int8_t)(it % 3), (int8_t)(it % 2), 0);
    return hash_bytes(1469598103934665603ULL, g_qqcoef, (size_t)dim * dim * sizeof(coeff_t));
}

static uint64_t run_quantize_residual(uint32_t it, int dump)
{
    const int dim = 4 << (int)((it / 6) % 4);
    memset(g_qqcoef, 0, 32 * 32 * sizeof(coeff_t));
    memset(g_qrec, 0, 64 * 64);
    g_qstate.qp = (int8_t)(16 + (it % 40));
    (void)dump;
    uvg_quantize_residual(&g_qstate, &g_qcu, dim, dim, COLOR_Y, (coeff_scan_order_t)(it % 3),
                          0, 64, 64, g_pix + PW, g_pix2 + PW2, g_qrec, g_qqcoef, false, 0,
                          UVG_LUMA_T);
    return hash_bytes(1469598103934665603ULL, g_qqcoef, (size_t)dim * dim * sizeof(coeff_t));
}

static uint64_t run_coeff_abs_sum(uint32_t it, int dump)
{
    const coeff_t *c = (const coeff_t *)(g_i16 + PWI);
    uint32_t r = uvg_coeff_abs_sum(c, 16 * 16);
    (void)it; (void)dump;
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

static uint64_t run_fast_coeff_cost(uint32_t it, int dump)
{
    const coeff_t *c = (const coeff_t *)(g_i16 + PWI);
    uint32_t r = uvg_fast_coeff_cost(c, 16, 16, 0x0001000100010001ULL);
    (void)it; (void)dump;
    return hash_bytes(1469598103934665603ULL, &r, sizeof(r));
}

static void prep_quant(void) { build_quant(); }

/* @family quant */
static void reg_quant(void)
{
    reg("quant/quant", run_quant);
    reg("quant/quantize_residual", run_quantize_residual);
    reg("quant/dequant", run_dequant);
    reg("quant/coeff_abs_sum", run_coeff_abs_sum);
    reg("quant/fast_coeff_cost", run_fast_coeff_cost);
}
/* @endfamily */
/* @endfamily */



/* @family dct */
static void reg_dct(void)
{
    reg("dct/dct_4x4", run_dct_4x4);
    reg("dct/dct_8x8", run_dct_8x8);
    reg("dct/dct_16x16", run_dct_16x16);
    reg("dct/dct_32x32", run_dct_32x32);
    reg("dct/idct_4x4", run_idct_4x4);
    reg("dct/idct_8x8", run_idct_8x8);
    reg("dct/idct_16x16", run_idct_16x16);
    reg("dct/idct_32x32", run_idct_32x32);
    reg("dct/mts_dct", run_mts_dct);
    reg("dct/mts_idct", run_mts_idct);
}
/* @endfamily */

/* @family picture */
static void reg_picture(void)
{
    reg("picture/sad_4x4", run_sad_4x4);
    reg("picture/sad_8x8", run_sad_8x8);
    reg("picture/sad_16x16", run_sad_16x16);
    reg("picture/sad_32x32", run_sad_32x32);
    reg("picture/sad_64x64", run_sad_64x64);
    reg("picture/satd_4x4", run_satd_4x4);
    reg("picture/satd_8x8", run_satd_8x8);
    reg("picture/satd_16x16", run_satd_16x16);
    reg("picture/satd_32x32", run_satd_32x32);
    reg("picture/satd_64x64", run_satd_64x64);
    reg("picture/sad_4x4_dual", run_sad_4x4_dual);
    reg("picture/sad_8x8_dual", run_sad_8x8_dual);
    reg("picture/sad_16x16_dual", run_sad_16x16_dual);
    reg("picture/sad_32x32_dual", run_sad_32x32_dual);
    reg("picture/sad_64x64_dual", run_sad_64x64_dual);
    reg("picture/satd_4x4_dual", run_satd_4x4_dual);
    reg("picture/satd_8x8_dual", run_satd_8x8_dual);
    reg("picture/satd_16x16_dual", run_satd_16x16_dual);
    reg("picture/satd_32x32_dual", run_satd_32x32_dual);
    reg("picture/satd_64x64_dual", run_satd_64x64_dual);
    reg("picture/reg_sad", run_reg_sad);
    reg("picture/satd_any_size", run_satd_any_size);
    reg("picture/satd_any_size_quad", run_satd_any_size_quad);
    reg("picture/pixels_calc_ssd", run_pixels_calc_ssd);
    reg("picture/crc32c_4x4", run_crc32c_4x4);
    reg("picture/crc32c_8x8", run_crc32c_8x8);
    reg("picture/ver_sad", run_ver_sad);
    reg("picture/hor_sad", run_hor_sad);
    reg("picture/pixel_var", run_pixel_var);
    reg("picture/generate_residual", run_generate_residual);
    reg("picture/get_optimized_sad", run_get_optimized_sad);
    reg("picture/bipred_average", run_bipred_average);
}
/* @endfamily */

static entry_t *find(const char *key)
{
    for (int i = 0; i < g_nentries; i++)
        if (strcmp(g_entries[i].key, key) == 0)
            return &g_entries[i];
    return NULL;
}

int main(int argc, char **argv)
{
    /* @families-reg */ reg_dct(); reg_picture(); reg_quant(); reg_ipol(); reg_sao(); reg_intra(); reg_alf(); /* @endfamilies-reg */
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
    /* @families-prep */ prep_dct(); prep_picture(); prep_quant(); prep_ipol(); prep_sao(); prep_intra(); prep_alf(); /* @endfamilies-prep */

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
