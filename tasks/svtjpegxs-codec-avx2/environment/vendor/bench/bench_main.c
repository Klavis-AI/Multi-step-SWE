/*
 * codecbench -- framework.  See bench.h.
 *
 * Usage:  codecbench <seed> <rounds> <profile> <func...>
 *
 * Emits two kinds of line, one per requested function:
 *   D <name> <round> <digest-hex>   one per correctness round (fresh draw)
 *   T <name> <ns-per-call>          median of adaptive batches
 *
 * BENCH_PERTURB=1 selects the anti-memoisation guard pass instead of the
 * scored timing pass (see guard_time_case).  The default, unset, is the
 * scored pass, unchanged for every existing consumer of this binary.
 *
 * `profile` selects the rtcd tier the harness asks the library to install:
 *   "c"     -> CPU_FLAGS_C        (portable reference only)
 *   "avx2"  -> CPU_FLAGS_AVX2     (what every step is measured at)
 * The tier is masked by the host's own flags inside the library, so the same
 * binary is honest on a host without AVX2.
 */
#include "bench.h"

#include "SvtJpegxs.h"
#include "encoder_dsp_rtcd.h"
#include "decoder_dsp_rtcd.h"
#include "common_dsp_rtcd.h"

#define BENCH_MAX_CASES 128

static bench_case g_cases[BENCH_MAX_CASES];
static size_t g_ncases = 0;

void bench_add(const bench_case *c) {
    if (g_ncases < BENCH_MAX_CASES)
        g_cases[g_ncases++] = *c;
}

void bench_add_all(const bench_case *c, size_t n) {
    for (size_t i = 0; i < n; i++)
        bench_add(&c[i]);
}

static const bench_case *bench_find(const char *name) {
    for (size_t i = 0; i < g_ncases; i++)
        if (strcmp(g_cases[i].name, name) == 0)
            return &g_cases[i];
    return NULL;
}

static void hex32(const uint8_t d[BENCH_DIGEST_BYTES], char out[2 * BENCH_DIGEST_BYTES + 1]) {
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < BENCH_DIGEST_BYTES; i++) {
        out[2 * i] = H[d[i] >> 4];
        out[2 * i + 1] = H[d[i] & 15];
    }
    out[2 * BENCH_DIGEST_BYTES] = 0;
}

/* Median time per call, in nanoseconds.
 *
 * One call is far below the clock's resolution, so a sample times a batch of
 * `inner` calls; `inner` is grown until a batch runs for at least ~1.5 ms.  The
 * reported figure is the median over `reps` batches, never a best-of: a ratio
 * built from a best-of preserves spuriously fast outliers as an apparent
 * speedup, which is the failure this harness exists to avoid. */
#define BENCH_TARGET_NS 3000000ULL
#define BENCH_REPS 9
#define BENCH_MAX_INNER 200000u

static uint64_t time_case(const bench_case *c, void *ctx, uint64_t *out_checksum) {
    uint64_t t0 = bench_now_ns();
    c->call(ctx);
    uint64_t one = bench_now_ns() - t0;
    if (one == 0)
        one = 1;

    uint32_t inner = 1;
    while (inner < BENCH_MAX_INNER && (uint64_t)inner * one < BENCH_TARGET_NS)
        inner *= 2;

    for (int w = 0; w < 3; w++)
        for (uint32_t i = 0; i < inner; i++)
            c->call(ctx); /* warm caches; result ignored on purpose */

    uint64_t samples[BENCH_REPS];
    uint64_t sink = 0;
    for (int r = 0; r < BENCH_REPS; r++) {
        uint64_t a = bench_now_ns();
        for (uint32_t i = 0; i < inner; i++)
            c->call(ctx);
        samples[r] = (bench_now_ns() - a) / inner;
        sink += samples[r];
    }
    qsort(samples, BENCH_REPS, sizeof(uint64_t), bench_cmp_u64);
    *out_checksum = sink;
    return samples[BENCH_REPS / 2];
}

/* The configuration the TIMED call runs at.
 *
 * A case's setup() selects its configuration from the round argument, and the
 * correctness rounds walk the case's table in order.  This build draws the
 * timed round from the seed rather than pinning it to one entry, so no single
 * configuration of a case is the one the timing pass runs at: the number
 * printed for a case is a sample of the same table its correctness rounds
 * walk, and a kernel that is only fast at one width shows it.  Any
 * non-negative index is legal -- each case reduces it modulo its own table.
 */
static int bench_timed_round(uint64_t seed) {
    uint64_t s = seed ^ 0x9E3779B97F4A7C15ULL;
    return (int)(bench_rng(&s) % 8u);
}

/* Guard timing: ns per call with EVERY call on a freshly drawn input.
 *
 * The scored pass (time_case above) re-feeds one drawn input for all of its
 * batches -- that is its design, and it is also what a sound memoiser scores
 * on: cache the input, replay the output.  This pass denies the cache
 * anything to re-use: every call is preceded by a fresh draw from the same
 * seeded generator, into the SAME context, so the memory footprint and the
 * access pattern are the scored pass's (one hot buffer) and only the CONTENT
 * changes.  A content-keyed cache misses on every call and its cost becomes
 * the miss path; a kernel whose cost does not depend on the input's content
 * reads as the scored pass reads.
 *
 * The figure is the median over GUARD_ROUNDS single-call samples with the
 * measured cost of the two clock reads subtracted -- a single fast call is
 * short enough that the clock overhead is a real fraction of it, and leaving
 * it in would bias every ratio toward 1 by a slot-dependent amount.  Same
 * units and same line format as the scored T line, so the verifier can
 * compare the two ratios per function and fail a step whose ratio collapses.
 *
 * EARLIER DESIGN, REJECTED BY MEASUREMENT: a group of up to eight contexts
 * cycled per round, to amortise the clock cost.  Cycling eight buffers (a
 * working set up to ~180 KB) pushed fast AVX2 kernels out of L1 and inflated
 * the installed side of the ratio by 1.4x-2.6x where the C side moved by
 * 0.9x-1.6x -- an honest oracle tree read guard/scored 0.50-0.71 at every
 * slot, inside the memoiser's own band.  The footprint must match the scored
 * run; only the content may change.
 */
#define GUARD_ROUNDS 101
#define GUARD_OVH_SAMPLES 65

static uint64_t guard_time_case(const bench_case *c, void *ctx,
                                uint64_t seed, uint64_t *out_sink) {
    /* The clock reads' own cost, measured the same way the samples are. */
    uint64_t ovh[GUARD_OVH_SAMPLES];
    for (int m = 0; m < GUARD_OVH_SAMPLES; m++) {
        uint64_t a = bench_now_ns();
        uint64_t b = bench_now_ns();
        ovh[m] = b > a ? b - a : 0;
    }
    qsort(ovh, GUARD_OVH_SAMPLES, sizeof(uint64_t), bench_cmp_u64);
    uint64_t overhead = ovh[GUARD_OVH_SAMPLES / 2];

    uint64_t k = 0;
    for (int w = 0; w < 2; w++) {
        c->setup(ctx, seed ^ (0xD1B54A32D192ED03ULL * (k + 1)), bench_timed_round(seed));
        c->call(ctx);
        k++;
    }

    uint64_t samples[GUARD_ROUNDS];
    uint64_t sink = 0;
    for (int r = 0; r < GUARD_ROUNDS; r++) {
        c->setup(ctx, seed ^ (0xD1B54A32D192ED03ULL * (k + 1)), bench_timed_round(seed));
        k++;
        uint64_t a = bench_now_ns();
        c->call(ctx);
        uint64_t dt = bench_now_ns() - a;
        if (dt > overhead)
            dt -= overhead;
        else
            dt = 1;
        samples[r] = dt;
        sink += dt;
    }
    qsort(samples, GUARD_ROUNDS, sizeof(uint64_t), bench_cmp_u64);
    *out_sink = sink;
    return samples[GUARD_ROUNDS / 2];
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <seed> <rounds> <c|avx2> <func...>\n", argv[0]);
        return 2;
    }
    uint64_t seed = strtoull(argv[1], NULL, 0);
    int rounds = atoi(argv[2]);
    const char *profile = argv[3];
    if (rounds < 1 || rounds > 64) {
        fprintf(stderr, "bad rounds\n");
        return 2;
    }

    CPU_FLAGS flags;
    if (strcmp(profile, "c") == 0)
        flags = CPU_FLAGS_C;
    else if (strcmp(profile, "avx2") == 0)
        flags = CPU_FLAGS_AVX2;
    else {
        fprintf(stderr, "bad profile '%s'\n", profile);
        return 2;
    }

    /* The anti-memoisation pass is asked for through the environment; every
     * other consumer of this binary leaves it unset and gets the scored pass,
     * unchanged. */
    const char *perturb = getenv("BENCH_PERTURB");
    int guard_pass = (perturb != NULL && perturb[0] != '\0' && perturb[0] != '0');

    bench_register_a();
    bench_register_b();
    bench_register_c();
    bench_register_d();
    bench_register_e();
    bench_register_f();
    bench_register_g();
    bench_register_h();

    setup_common_rtcd_internal(flags);
    setup_encoder_rtcd_internal(flags);
    setup_decoder_rtcd_internal(flags);

    for (int i = 4; i < argc; i++) {
        const bench_case *c = bench_find(argv[i]);
        if (c == NULL) {
            printf("E %s unknown_case\n", argv[i]);
            continue;
        }
        if (guard_pass) {
            void *ctx = bench_alloc(c->ctx_size);
            if (ctx == NULL) {
                printf("E %s alloc_failed\n", c->name);
                continue;
            }
            uint64_t sink = 0;
            uint64_t ns = guard_time_case(c, ctx, seed, &sink);
            free(ctx);
            printf("T %s %llu\n", c->name, (unsigned long long)ns);
            fflush(stdout);
            continue;
        }
        void *ctx = bench_alloc(c->ctx_size);
        if (ctx == NULL) {
            printf("E %s alloc_failed\n", c->name);
            continue;
        }
        char hx[2 * BENCH_DIGEST_BYTES + 1];
        for (int r = 0; r < rounds; r++) {
            uint64_t s = seed ^ (0xA5A5A5A5A5A5A5A5ULL * (uint64_t)(r + 1));
            memset(ctx, 0, c->ctx_size);
            c->setup(ctx, s, r);
            c->call(ctx);
            uint8_t d[BENCH_DIGEST_BYTES];
            c->hash(ctx, d);
            hex32(d, hx);
            printf("D %s %d %s\n", c->name, r, hx);
        }
        uint64_t sink = 0;
        memset(ctx, 0, c->ctx_size);
        c->setup(ctx, seed ^ 0x5EED5EED5EED5EEDULL, bench_timed_round(seed));
        uint64_t ns = time_case(c, ctx, &sink);
        printf("T %s %llu\n", c->name, (unsigned long long)ns);
        free(ctx);
        fflush(stdout);
    }
    return 0;
}
