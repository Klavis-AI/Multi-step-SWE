/* Per-function measurement harness for the mh_sha256 + rolling_hash step.
 *
 * Built twice by the verifier: once against this step's base tree (whose
 * public symbols resolve to the portable C reference) and once against the
 * candidate.  Both runs use the same arguments, so `dump` outputs are
 * compared byte for byte and `bench` medians are divided.
 *
 * Every role drives the PUBLIC entry points, never the internal *_base or
 * _rolling_hash2_run_until symbols, so what is timed is whatever
 * implementation the library installed -- assembly, intrinsics or portable C;
 * the route does not matter, only the installed pointer does.
 *
 * Scoped function ids (one timed role each):
 *
 *   mh_sha256_update : many isal_mh_sha256_update calls on one ctx over a large
 *                      whole-block buffer; the ctx re-init between repetitions
 *                      is untimed
 *   mh_sha256_finalize
 *                    : the timed region holds FIN_CALLS isal_mh_sha256_finalize
 *                      calls, each on a ctx whose init+update ran UNTIMED and
 *                      whose total length leaves a maximal partial block
 *   rolling_hash2_run
 *                    : one isal_rh_state2 initialised and reset UNTIMED, then a
 *                      loop of isal_rolling_hash2_run calls that each scan a
 *                      whole 256 KiB buffer with no trigger hit
 *
 * `dump` walks a mixed job stream per function id and writes every produced
 * digest, in the exact order it was produced, as one contiguous big-endian
 * word array.
 */
#include "bench_common.h"
#include "mh_sha256.h"
#include "rolling_hashx.h"

#define BLOCK    ISAL_MH_SHA256_BLOCK_SIZE      /* 1024 */
#define SHA256W  ISAL_SHA256_DIGEST_WORDS       /* 8    */

/* update() role: UPD_ITERS calls of UPD_LEN bytes per timed region.  UPD_LEN is
 * a whole multiple of BLOCK so every call takes update()'s num_blocks path and
 * the timed region is pure block throughput, with no partial-block tail.
 * Sizing: measured on the stripped (portable C) tree mh_sha256 costs ~13.1
 * ns/byte, so 64 KiB x 2 = 128 KiB puts the region at ~1.7 ms -- middle of the
 * 0.3-3 ms band, with 2 calls in it and a ~25 ns clock read that is 1e-5 of the
 * region. */
#define UPD_LEN   65536
#define UPD_ITERS 2

/* finalize() role: each timed region holds FIN_CALLS finalize() calls, one per
 * pre-initialised ctx, and nothing else.
 *
 * FIN_LEN = BLOCK*8 + (BLOCK - 8) = 9208 leaves total_length % BLOCK == 1016,
 * so after the 0x80 the padding no longer fits in the current block and
 * mh_sha256_tail has to run the block function on a full padding block AND on
 * the length block -- two block passes, the heaviest finalize for the least
 * input.  A partial block of BLOCK-1 would be *shorter*, since 1024 bytes still
 * fit the 0x80 and the 8-byte length in one block.
 *
 * Sizing: one such finalize costs ~34.5 us on the portable C reference, so 32
 * calls put the region at ~1.1 ms -- middle of the band. */
#define FIN_LEN   (BLOCK * 8 + (BLOCK - 8))
#define FIN_CALLS 32

/* rolling_hash2_run role: RH_W = 32 is the upstream perf driver's window.
 * mask = 0xffffffff with trigger = 0x123 is also the upstream perf driver's
 * pair: (hash & 0xffffffff) == 0x123 needs a full 32-bit match, so the scan
 * runs to max_len instead of stopping early -- that "walk every byte" loop is
 * exactly the one the SSE/AVX2 _rolling_hash2_run_until kernels accelerate.
 * The pair is not merely assumed: role_rh and the dump both assert the
 * returned status is ISAL_FINGERPRINT_RET_MAX and abort otherwise.
 *
 * Sizing: measured on the stripped tree the portable C scan costs ~2.44 ns/byte,
 * so 128 KiB x 4 = 512 KiB puts the region at ~1.3 ms -- middle of the band,
 * with 4 calls of ~320 us each, i.e. ~1e-4 of a call is the clock read. */
#define RH_W        32
#define RH_LEN      131072
#define RH_ITERS    4
#define RH_MASK     0xffffffffu
#define RH_TRIGGER  0x123u

#define NREP      11
#define MAXREPS   4096

/* One bench buffer, long enough for the largest role LEN. */
#define BIG_LEN   (UPD_LEN > RH_LEN ? UPD_LEN : RH_LEN)

#define DUMP_MAX  (1 << 20)
/* Word budget for the digest stream.  Measured: 832 words for the mh_sha256
 * stream, 1267 for the rolling-hash stream (35 generated masks + 14 windows x
 * (14 run records x 6 + 4 generated masks)); this is >3x headroom, and the sink
 * fails loudly rather than truncating if that ever stops being true. */
#define DCAP      4096

static uint8_t *big;    /* BIG_LEN bytes, the bench buffer */
static uint8_t *dbuf;   /* DUMP_MAX bytes, the dump buffer */

/* ------------------------------------------------------------------ */
/* digest sink: one contiguous array, appended in a fixed order         */

struct sink {
        uint32_t *w;
        size_t n, cap;
        size_t ndig;    /* logged results, for the "dumped N digests" line */
};

static void
sink_put(struct sink *s, const uint32_t *v, size_t k)
{
        if (s->n + k > s->cap) {
                fprintf(stderr, "bench: digest stream overflow\n");
                exit(2);
        }
        memcpy(s->w + s->n, v, k * sizeof(*v));
        s->n += k;
}

/* Append one logged result.  Generated-mask words go in through sink_put
 * directly: they are stream padding, not a digest, so they do not count. */
static void
sink_dig(struct sink *s, const uint32_t *v, size_t k)
{
        sink_put(s, v, k);
        s->ndig++;
}

static void
setup(void)
{
        big = bc_alloc(BIG_LEN);
        dbuf = bc_alloc(DUMP_MAX);
        if (!big || !dbuf) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        bc_fill(big, BIG_LEN, 0x5a5a0000ull);
        bc_fill(dbuf, DUMP_MAX, 0xfeedfaceull);
}

/* ------------------------------------------------------------------ */
/* timed roles                                                          */

static uint64_t
role_sha256_update(int reps, uint64_t *samples)
{
        struct isal_mh_sha256_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA256W];
        uint32_t i;
        int r;
        uint64_t t0, t1;

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        bc_fp_reset();
        for (r = 0; r < reps; r++) {
                /* Fresh content every repetition, from a per-process salt, so
                 * the ctx re-init below starts from a state no earlier timed
                 * region has produced and a memoised update cannot stand in
                 * for the work.  Untimed. */
                bc_fill(big, UPD_LEN, bc_salt() + 0x3000 * (uint64_t) (r + 1));
                if (isal_mh_sha256_init(ctx)) {
                        fprintf(stderr, "bench: mh_sha256 init failed\n");
                        exit(3);
                }
                t0 = bc_now_ns();
                for (i = 0; i < UPD_ITERS; i++) {
                        if (isal_mh_sha256_update(ctx, big, UPD_LEN)) {
                                fprintf(stderr, "bench: mh_sha256 update failed\n");
                                exit(3);
                        }
                }
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
        }
        /* The update role produces no digest of its own, so the timed path's
         * output is checked by finishing the last repetition's context, which
         * is untimed: the digest is a function of everything the timed updates
         * consumed.  It is folded into the fingerprint the verifier compares
         * between the two builds. */
        if (isal_mh_sha256_finalize(ctx, dig)) {
                fprintf(stderr, "bench: mh_sha256 finalize failed\n");
                exit(3);
        }
        bc_fp_add(dig, SHA256W);
        free(ctx);
        return bc_median(samples, reps);
}

static uint64_t
role_sha256_finalize(int reps, uint64_t *samples)
{
        struct isal_mh_sha256_ctx *ctx = bc_alloc(sizeof(*ctx) * FIN_CALLS);
        uint32_t dig[FIN_CALLS * SHA256W];
        uint32_t k;
        int r;
        uint64_t t0, t1;

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        bc_fp_reset();
        for (r = 0; r < reps; r++) {
                /* Fresh content every repetition (untimed), plus one window
                 * of it per ctx below. */
                bc_fill(big, FIN_LEN + FIN_CALLS, bc_salt() + 0x4000 * (uint64_t) (r + 1));
                for (k = 0; k < FIN_CALLS; k++) {
                        /* Each ctx is fed a different window of this
                         * repetition's fresh content (big + k, FIN_LEN bytes
                         * long, still inside the allocation), so the 32
                         * finalize() calls in the timed region are 32
                         * distinct contexts, not one context 32 times. */
                        if (isal_mh_sha256_init(&ctx[k]) ||
                            isal_mh_sha256_update(&ctx[k], big + k, FIN_LEN)) {
                                fprintf(stderr, "bench: mh_sha256 setup failed\n");
                                exit(3);
                        }
                }
                t0 = bc_now_ns();
                for (k = 0; k < FIN_CALLS; k++) {
                        if (isal_mh_sha256_finalize(&ctx[k], &dig[(size_t) k * SHA256W])) {
                                fprintf(stderr, "bench: mh_sha256 finalize failed\n");
                                exit(3);
                        }
                }
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
                /* Every digest the timed region produced, folded after t1: the
                 * verifier compares the fold between the two builds. */
                for (k = 0; k < FIN_CALLS; k++)
                        bc_fp_add(&dig[(size_t) k * SHA256W], SHA256W);
        }
        free(ctx);
        return bc_median(samples, reps);
}

/* One state, re-initialised and reset outside the timed region on the fresh
 * content of each repetition; the region itself is RH_ITERS full scans and
 * nothing else.  Every call is checked, so a run that stops early (a trigger
 * hit) aborts instead of quietly timing a short scan. */
static uint64_t
role_rh(int reps, uint64_t *samples)
{
        struct isal_rh_state2 *st = bc_alloc(sizeof(*st));
        int r, match;
        uint32_t i, offset;
        uint32_t hv[2];
        uint64_t t0, t1;

        if (!st) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_rolling_hash2_init(st, RH_W)) {
                fprintf(stderr, "bench: rolling hash init failed\n");
                exit(3);
        }
        bc_fp_reset();
        for (r = 0; r < reps; r++) {
                /* Fresh content every repetition, from a per-process salt.
                 * The state has to be reset on the content it describes, so
                 * the reset moves inside this loop -- still outside the timed
                 * region, and it is the same O(w) reset every repetition. */
                bc_fill(big, BIG_LEN, bc_salt() + 0x6000 * (uint64_t) (r + 1));
                if (isal_rolling_hash2_reset(st, big)) {
                        fprintf(stderr, "bench: rolling hash reset failed\n");
                        exit(3);
                }
                t0 = bc_now_ns();
                for (i = 0; i < RH_ITERS; i++) {
                        offset = 0;
                        match = -1;
                        if (isal_rolling_hash2_run(st, big, RH_LEN, RH_MASK, RH_TRIGGER,
                                                   &offset, &match)) {
                                fprintf(stderr, "bench: rolling hash run failed\n");
                                exit(3);
                        }
                        if (match != ISAL_FINGERPRINT_RET_MAX) {
                                fprintf(stderr,
                                        "bench: rolling hash stopped early "
                                        "(match=%d offset=%u) -- not a full scan\n",
                                        match, offset);
                                exit(3);
                        }
                }
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
                /* The scan's own outputs -- the offset the last call reported
                 * and the window state it ended on, the same two values the
                 * dump stream records -- folded after t1, so the timed region
                 * is unchanged.  The fold is compared between the two builds. */
                hv[0] = (uint32_t) st->hash;
                hv[1] = (uint32_t) (st->hash >> 32);
                bc_fp_add(&offset, 1);
                bc_fp_add(hv, 2);
        }
        free(st);
        return bc_median(samples, reps);
}

/* ------------------------------------------------------------------ */
/* mh_sha256 dump stream                                                */

/* Whole-buffer sweep: 0, 1, odd, block-1, block, block+1, multi-block and a
 * >= 256 KiB buffer, so every partial_block_len branch of update() and both
 * padding branches of finalize() are visited at least once. */
static const uint32_t dump_lens[] = {
        0,     1,     2,     3,     7,     8,     9,     15,    16,    17,
        31,    32,    33,    55,    56,    57,    63,    64,    65,    127,
        128,   129,   255,   256,   257,   511,   512,   513,   1023,  1024,
        1025,  2047,  2048,  2049,  4095,  4096,  4097,  8191,  8192,  8193,
        16383, 16384, 16385, 32767, 32768, 65535, 65536, 262144,
};

/* Streaming sweep: the same lengths, but fed as MANY update() calls instead of
 * one whole-buffer call. */
static const uint32_t stream_lens[] = {
        0,   1,    2,    63,   64,   65,   1023, 1024,  1025,  2047,
        2048, 4095, 4096, 8191, 8192, 9208, 16384, 32768, 65536, 262144,
};

/* Chunk sizes cycled through by the streaming passes.  Includes 0 and 1 so the
 * len==0 early-out and the byte-at-a-time accumulation path both run, plus
 * sizes straddling BLOCK so block-crossing partial blocks are exercised. */
static const uint32_t chunks[] = { 0, 1, 3, 1023, 1024, 1025, 7, 4096, 2, 1024 };
#define NCHUNK (sizeof(chunks) / sizeof(chunks[0]))

/* Tail sweep: partial_block_len = BLOCK*k + p, p walked over the values that
 * matter for finalize -- 0 and 1 (trivial) and the BLOCK-9/-8/-7/-1
 * neighbourhood where the padding flips between one and two block passes. */
static const uint32_t tail_p[] = { 0, 1, 8, 512, 1015, 1016, 1017, 1023 };
static const uint32_t tail_k[] = { 0, 1, 8, 16 };

/* update()-heavy extra: a burst of sub-block updates, so the partial-block
 * accumulation path dominates rather than whole-block throughput. */
static const uint32_t small_lens[] = { 1024, 4096, 65536, 262144 };
static const uint32_t small_chunks[] = { 1, 1, 2, 1, 3, 1, 5, 1, 8, 1, 13, 1 };
#define NSMALL (sizeof(small_chunks) / sizeof(small_chunks[0]))

/* finalize()-heavy extra: these lengths all end on the two-block padding path
 * (total_length % BLOCK == BLOCK-8), driven by a different streaming split so
 * the partial buffer is built up differently. */
static const uint32_t tail_split[] = { 1, 64, 1024, 9208 };
#define NTAILSPLIT (sizeof(tail_split) / sizeof(tail_split[0]))

static void
stream_sha256(struct isal_mh_sha256_ctx *ctx, const uint8_t *p, uint32_t len, uint32_t step)
{
        uint32_t off = 0, ci = 0;

        while (off < len) {
                uint32_t c = step ? step : chunks[ci++ % (uint32_t) NCHUNK];

                if (c > len - off)
                        c = len - off;
                if (isal_mh_sha256_update(ctx, p + off, c)) {
                        fprintf(stderr, "bench: dump update failed\n");
                        exit(3);
                }
                off += c;
                step = 0;
        }
}

static void
one_sha256(struct sink *s, uint32_t len)
{
        struct isal_mh_sha256_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA256W];

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha256_init(ctx) || isal_mh_sha256_update(ctx, dbuf, len) ||
            isal_mh_sha256_finalize(ctx, dig)) {
                fprintf(stderr, "bench: dump job failed at len %u\n", len);
                exit(3);
        }
        sink_dig(s, dig, SHA256W);
        free(ctx);
}

static void
one_stream_sha256(struct sink *s, uint32_t len, uint32_t step)
{
        struct isal_mh_sha256_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA256W];

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha256_init(ctx)) {
                fprintf(stderr, "bench: dump init failed at len %u\n", len);
                exit(3);
        }
        stream_sha256(ctx, dbuf, len, step);
        if (isal_mh_sha256_finalize(ctx, dig)) {
                fprintf(stderr, "bench: dump finalize failed at len %u\n", len);
                exit(3);
        }
        sink_dig(s, dig, SHA256W);
        free(ctx);
}

static void
one_small_sha256(struct sink *s, uint32_t len)
{
        struct isal_mh_sha256_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA256W];
        uint32_t off = 0, ci = 0;

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha256_init(ctx)) {
                fprintf(stderr, "bench: dump init failed at len %u\n", len);
                exit(3);
        }
        while (off < len) {
                uint32_t c = small_chunks[ci++ % (uint32_t) NSMALL];

                if (c > len - off)
                        c = len - off;
                if (isal_mh_sha256_update(ctx, dbuf + off, c)) {
                        fprintf(stderr, "bench: dump update failed at len %u\n", len);
                        exit(3);
                }
                off += c;
        }
        if (isal_mh_sha256_finalize(ctx, dig)) {
                fprintf(stderr, "bench: dump finalize failed at len %u\n", len);
                exit(3);
        }
        sink_dig(s, dig, SHA256W);
        free(ctx);
}

static void
pass_sha256(struct sink *s, int is_update)
{
        uint32_t i, nt;

        nt = (uint32_t) (sizeof(dump_lens) / sizeof(dump_lens[0]));
        for (i = 0; i < nt; i++)
                one_sha256(s, dump_lens[i]);
        nt = (uint32_t) (sizeof(stream_lens) / sizeof(stream_lens[0]));
        for (i = 0; i < nt; i++)
                one_stream_sha256(s, stream_lens[i], 0);
        nt = (uint32_t) (sizeof(tail_p) / sizeof(tail_p[0]));
        for (i = 0; i < nt; i++) {
                uint32_t j, nk = (uint32_t) (sizeof(tail_k) / sizeof(tail_k[0]));

                for (j = 0; j < nk; j++)
                        one_sha256(s, BLOCK * tail_k[j] + tail_p[i]);
        }
        if (is_update) {
                nt = (uint32_t) (sizeof(small_lens) / sizeof(small_lens[0]));
                for (i = 0; i < nt; i++)
                        one_small_sha256(s, small_lens[i]);
        } else {
                for (i = 0; i < (uint32_t) NTAILSPLIT; i++)
                        one_stream_sha256(s, BLOCK * 8 + (BLOCK - 8), tail_split[i]);
        }
}

/* ------------------------------------------------------------------ */
/* rolling-hash dump stream                                             */

/* Windows: every width from 1 to 8 (the range where the window-vs-buffer
 * interaction changes shape) plus a spread up to the documented maximum 32. */
static const uint32_t rh_wins[] = { 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 17, 24, 31, 32 };

/* A generated mask goes into the stream as one word, a run case as six:
 *   [0] match   ISAL_FINGERPRINT_RET_HIT / _MAX as returned
 *   [1] offset  the offset the run reported
 *   [2] hash    low  32 bits of state->hash after the call
 *   [3] hash    high 32 bits of state->hash after the call
 *   [4] mask    the mask the run was given
 *   [5] trigger the trigger the run was given
 * state->hash is a public struct member and encodes the whole scanned window,
 * so any divergence between two implementations of the same contract shows up
 * here even when the return status and offset agree. */
static uint32_t
maskgen(struct sink *s, uint32_t mean, uint32_t shift)
{
        uint32_t m = 0;

        if (isal_rolling_hashx_mask_gen(mean, shift, &m)) {
                fprintf(stderr, "bench: mask gen failed (mean=%u shift=%u)\n", mean, shift);
                exit(3);
        }
        sink_put(s, &m, 1);
        return m;
}

static void
rh_case(struct sink *s, uint32_t w, uint32_t max_len, uint32_t mask, uint32_t trigger)
{
        struct isal_rh_state2 *st = bc_alloc(sizeof(*st));
        uint32_t rec[6];
        int match = -1;
        uint32_t offset = 0;

        if (!st) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_rolling_hash2_init(st, w)) {
                fprintf(stderr, "bench: rolling hash init failed (w=%u)\n", w);
                exit(3);
        }
        /* dbuf always holds at least w bytes, so reset() can read a full window
         * even when the run below is limited to fewer than w bytes. */
        if (isal_rolling_hash2_reset(st, dbuf)) {
                fprintf(stderr, "bench: rolling hash reset failed (w=%u)\n", w);
                exit(3);
        }
        if (isal_rolling_hash2_run(st, dbuf, max_len, mask, trigger, &offset, &match)) {
                fprintf(stderr, "bench: rolling hash run failed (w=%u len=%u)\n", w, max_len);
                exit(3);
        }
        if (match != ISAL_FINGERPRINT_RET_HIT && match != ISAL_FINGERPRINT_RET_MAX) {
                fprintf(stderr, "bench: rolling hash bad status %d (w=%u len=%u)\n",
                        match, w, max_len);
                exit(3);
        }
        rec[0] = (uint32_t) match;
        rec[1] = offset;
        rec[2] = (uint32_t) st->hash;
        rec[3] = (uint32_t) (st->hash >> 32);
        rec[4] = mask;
        rec[5] = trigger;
        sink_dig(s, rec, 6);
        free(st);
}

static void
pass_rh(struct sink *s)
{
        static const uint32_t means[] = { 2, 4, 16, 64, 256, 1024, 65536 };
        static const uint32_t shifts[] = { 0, 1, 7, 13, 31 };
        uint32_t i, j, n;
        uint32_t long_len = 65536;      /* >> any window: a full 16-block scan */

        /* 1. the mask generator itself, over a spread of means and shifts */
        for (i = 0; i < (uint32_t) (sizeof(means) / sizeof(means[0])); i++)
                for (j = 0; j < (uint32_t) (sizeof(shifts) / sizeof(shifts[0])); j++)
                        maskgen(s, means[i], shifts[j]);

        /* 2. run cases, per window */
        for (i = 0; i < (uint32_t) (sizeof(rh_wins) / sizeof(rh_wins[0])); i++) {
                uint32_t w = rh_wins[i];
                uint32_t m;

                /* empty, sub-window, exactly-window and window+1: these walk the
                 * "buffer shorter than the window" branch of the run loop */
                rh_case(s, w, 0, RH_MASK, RH_TRIGGER);
                rh_case(s, w, w > 1 ? w - 1 : 0, RH_MASK, RH_TRIGGER);
                rh_case(s, w, w, RH_MASK, RH_TRIGGER);
                rh_case(s, w, w + 1, RH_MASK, RH_TRIGGER);

                /* unreachable trigger: scans all 64 KiB and must report MAX */
                rh_case(s, w, long_len, RH_MASK, RH_TRIGGER);
                /* 2 KiB longer than the dump buffer's short cases */
                rh_case(s, w, 262144, RH_MASK, RH_TRIGGER);

                /* guaranteed hit: (hash & 0) == 0 is true at the first byte */
                rh_case(s, w, long_len, 0, 0);

                /* realistic chunker masks: a mean-sized mask with a trigger
                 * drawn from it, a mask used as its own trigger (fires whenever
                 * the selected bits are all set), and a coarse mask with a zero
                 * trigger.
                 *
                 * Every trigger below is a SUBSET of its mask, i.e.
                 * trigger & ~mask == 0.  That is the API's contract, not a
                 * convenience: the SSE4.1/AVX2 _rolling_hash2_run_until kernels
                 * test the trigger with `pext trigger, trigger, mask` against
                 * `pext hash, mask`, which agrees with the portable C
                 * `(hash & mask) == trigger` only while the trigger carries no
                 * bits outside the mask.  Upstream's own caller obeys it too
                 * (rolling_hash/chunking_with_mb_hash.c does
                 * `trigger = rand() & mask`).  A trigger with stray high bits
                 * makes the asm stop early and the C wrapper then report
                 * ISAL_FINGERPRINT_RET_MAX with a truncated offset -- verified
                 * on this pin -- so such a pair is out of contract and is
                 * deliberately not in the stream. */
                m = maskgen(s, 256, 0);
                rh_case(s, w, long_len, m, m & RH_TRIGGER);
                m = maskgen(s, 256, 11);
                rh_case(s, w, long_len, m, m & RH_TRIGGER);
                m = maskgen(s, 1024, 7);
                rh_case(s, w, long_len, m, m);
                m = maskgen(s, 64, 13);
                rh_case(s, w, long_len, m, 0);

                /* all-ones mask against a zero trigger: only an exactly-zero
                 * 32-bit hash fires, so this normally reaches MAX too */
                rh_case(s, w, long_len, RH_MASK, 0);

                /* two different windows on the same bytes, so the w-dependent
                 * table rotation is compared and not just the window length */
                rh_case(s, w, long_len, 0x03ffu, 0x0123u);
                n = (w < 16) ? 16 : 1;
                rh_case(s, n, long_len, 0x00ffu, 0x0042u);
        }
}

/* ------------------------------------------------------------------ */

static void
mode_dump(const char *fnid, const char *path)
{
        struct sink s;
        int is_rh = strcmp(fnid, "rolling_hash2_run") == 0;

        s.w = bc_alloc(DCAP * sizeof(*s.w));
        if (!s.w) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        s.n = 0;
        s.cap = DCAP;
        s.ndig = 0;

        if (is_rh)
                pass_rh(&s);
        else
                pass_sha256(&s, strstr(fnid, "_update") != NULL);

        bc_dump_be32(path, s.w, s.n);
        /* A stream written to stdout ("-") is the bytes the verifier compares,
         * so the human-readable summary is only emitted for a named file. */
        if (strcmp(path, "-") != 0) {
                printf("dumped %u digests\n", (unsigned) s.ndig);
                fflush(stdout);
        }
        free(s.w);
}

static int
known(const char *fnid)
{
        return strcmp(fnid, "mh_sha256_update") == 0 ||
               strcmp(fnid, "mh_sha256_finalize") == 0 ||
               strcmp(fnid, "rolling_hash2_run") == 0;
}

int
main(int argc, char **argv)
{
        const char *fnid, *mode;
        uint64_t *samples;
        int reps = NREP;

        if (argc < 3) {
                fprintf(stderr, "usage: %s <fnid> <dump|bench> [arg] [reps]\n", argv[0]);
                return 2;
        }
        fnid = argv[1];
        mode = argv[2];
        if (!known(fnid)) {
                fprintf(stderr, "bench: unknown function id '%s'\n", fnid);
                return 2;
        }
        setup();

        if (strcmp(mode, "dump") == 0) {
                if (argc < 4) {
                        fprintf(stderr, "dump needs an output path\n");
                        return 2;
                }
                bc_seed(0xd00dfeed);
                mode_dump(fnid, argv[3]);
                return 0;
        }
        if (strcmp(mode, "bench") != 0) {
                fprintf(stderr, "bench: unknown mode '%s'\n", mode);
                return 2;
        }
        if (argc >= 4) {
                reps = atoi(argv[3]);
                if (reps < 1 || reps > MAXREPS) {
                        fprintf(stderr, "bench: bad repetition count %d\n", reps);
                        return 2;
                }
        }
        samples = bc_alloc(sizeof(*samples) * reps);
        if (!samples) {
                fprintf(stderr, "bench: out of memory\n");
                return 2;
        }

        if (strcmp(fnid, "mh_sha256_update") == 0)
                role_sha256_update(reps, samples);
        else if (strcmp(fnid, "mh_sha256_finalize") == 0)
                role_sha256_finalize(reps, samples);
        else
                role_rh(reps, samples);

        bc_report(fnid, samples, reps);
        return 0;
}
