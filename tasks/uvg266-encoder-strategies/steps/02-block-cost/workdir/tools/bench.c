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


/* ============================================================== quantisers */







/* ================================================================ registry */


static void prep_picture(void)
{
    build_blocks();
    build_duals();
    build_quad();
    build_res();
    build_bipred();
}






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

static entry_t *find(const char *key)
{
    for (int i = 0; i < g_nentries; i++)
        if (strcmp(g_entries[i].key, key) == 0)
            return &g_entries[i];
    return NULL;
}

int main(int argc, char **argv)
{
    reg_picture();
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
    prep_picture();

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
