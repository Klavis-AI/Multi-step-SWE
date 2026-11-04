/* Per-function measurement harness for the mh_sha1 + mh_sha1_murmur3_x64_128
 * step.
 *
 * Built twice by the verifier: once against this step's base tree (whose
 * public symbols resolve to the portable C reference) and once against the
 * candidate.  Both runs use the same arguments, so `dump` outputs are
 * compared byte for byte and `bench` medians are divided.
 *
 * Every role drives the PUBLIC entry points (isal_mh_sha1_init/update/finalize,
 * isal_mh_sha1_murmur3_x64_128_init/update/finalize), never the internal
 * *_base symbols, so what is timed is whatever implementation the library
 * installed -- assembly, intrinsics or portable C; the route does not matter,
 * only the installed pointer does.
 *
 * Scoped function ids (one timed role each):
 *
 *   mh_sha1_update              : many update() calls on one ctx over a large
 *                                 whole-block buffer; the ctx re-init between
 *                                 repetitions is untimed
 *   mh_sha1_finalize            : the timed region holds FIN_CALLS finalize()
 *                                 calls, each on a ctx whose init+update ran
 *                                 UNTIMED and whose total length leaves a
 *                                 maximal partial block behind
 *   mh_sha1_murmur3_x64_128_update
 *   mh_sha1_murmur3_x64_128_finalize
 *                               : the same two roles over the stitched
 *                                 SHA1+murmur3 context
 *
 * `dump` walks a mixed job stream per function id -- whole-buffer lengths from
 * 0 to 256 KiB, streaming update sequences, the block-boundary tail sweep and
 * a role-specific extra pass -- and writes every digest it produces, in
 * submission order, as one contiguous big-endian word array.
 */
#include "bench_common.h"
#include "mh_sha1.h"
#include "mh_sha1_murmur3_x64_128.h"

#define BLOCK    ISAL_MH_SHA1_BLOCK_SIZE               /* 1024 */
#define SHA1W    ISAL_SHA1_DIGEST_WORDS                /* 5    */
#define MURW     ISAL_MURMUR3_x64_128_DIGEST_WORDS     /* 4    */
#define MURSEED  0x0123456789abcdefull

/* update() role: UPD_ITERS calls of UPD_LEN bytes per timed region.  UPD_LEN is
 * a whole multiple of BLOCK so every call takes update()'s num_blocks path and
 * the timed region is pure block throughput, with no partial-block tail.
 * Sizing: measured on the stripped (portable C) tree mh_sha1 costs ~5.4 ns/byte
 * and the stitched murmur version ~5.3 ns/byte, so 64 KiB x 4 = 256 KiB puts the
 * region at ~1.4 ms -- middle of the 0.3-3 ms band, with 4 calls in it and a
 * ~25 ns clock read that is 1e-5 of the region. */
#define UPD_LEN   65536
#define UPD_ITERS 4

/* finalize() role: each timed region holds FIN_CALLS finalize() calls, one per
 * pre-initialised ctx, and nothing else.
 *
 * FIN_LEN = BLOCK*8 + (BLOCK - 8) = 9208 leaves total_length % BLOCK == 1016,
 * the largest partial block for which mh_sha1_tail still has to emit the length
 * field inside the same 1024-byte block.  Because 1016 + 1 > BLOCK - 8, the
 * padding needs TWO block-function passes instead of one, so this is the
 * heaviest possible finalize for the least input; a partial block of BLOCK-1
 * would be *shorter*, since it fits the 0x80 and the length in one block.
 *
 * Sizing: one such finalize costs ~11.6 us on the portable C reference, so 64
 * calls put the region at ~0.74 ms -- inside the band, while the per-ctx
 * init+update setup (which is ~4x the timed work) stays outside it. */
#define FIN_LEN   (BLOCK * 8 + (BLOCK - 8))
#define FIN_CALLS 64

#define NREP      11
#define MAXREPS   4096

/* Dump buffer: covers the 256 KiB length plus the streaming chunk table. */
#define DUMP_MAX  (1 << 20)
/* Word budget for the digest stream.  Worst case below is 2 seeds x 104 digests
 * x (SHA1W + MURW) = 1872 words, so this is 2x headroom; the sink fails loudly
 * rather than truncating if that ever stops being true. */
#define DCAP      4096

static uint8_t *big;    /* UPD_LEN bytes, the bench buffer  */
static uint8_t *dbuf;   /* DUMP_MAX bytes, the dump buffer */

/* ------------------------------------------------------------------ */
/* digest sink: one contiguous array, appended in a fixed order         */

struct sink {
        uint32_t *w;
        size_t n, cap;
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

static void
setup(void)
{
        big = bc_alloc(UPD_LEN);
        dbuf = bc_alloc(DUMP_MAX);
        if (!big || !dbuf) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        bc_fill(big, UPD_LEN, 0x5a5a0000ull);
        bc_fill(dbuf, DUMP_MAX, 0xfeedfaceull);
}

/* ------------------------------------------------------------------ */
/* timed roles                                                          */

static uint64_t
role_sha1_update(int reps, uint64_t *samples)
{
        struct isal_mh_sha1_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA1W];
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
                if (isal_mh_sha1_init(ctx)) {
                        fprintf(stderr, "bench: mh_sha1 init failed\n");
                        exit(3);
                }
                t0 = bc_now_ns();
                for (i = 0; i < UPD_ITERS; i++) {
                        if (isal_mh_sha1_update(ctx, big, UPD_LEN)) {
                                fprintf(stderr, "bench: mh_sha1 update failed\n");
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
        if (isal_mh_sha1_finalize(ctx, dig)) {
                fprintf(stderr, "bench: mh_sha1 finalize failed\n");
                exit(3);
        }
        bc_fp_add(dig, SHA1W);
        free(ctx);
        return bc_median(samples, reps);
}

static uint64_t
role_sha1_finalize(int reps, uint64_t *samples)
{
        struct isal_mh_sha1_ctx *ctx = bc_alloc(sizeof(*ctx) * FIN_CALLS);
        uint32_t dig[FIN_CALLS * SHA1W];
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
                         * long, still inside the allocation), so the 64
                         * finalize() calls in the timed region are 64
                         * distinct contexts, not one context 64 times. */
                        if (isal_mh_sha1_init(&ctx[k]) ||
                            isal_mh_sha1_update(&ctx[k], big + k, FIN_LEN)) {
                                fprintf(stderr, "bench: mh_sha1 setup failed\n");
                                exit(3);
                        }
                }
                t0 = bc_now_ns();
                for (k = 0; k < FIN_CALLS; k++) {
                        if (isal_mh_sha1_finalize(&ctx[k], &dig[(size_t) k * SHA1W])) {
                                fprintf(stderr, "bench: mh_sha1 finalize failed\n");
                                exit(3);
                        }
                }
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
                /* Every digest the timed region produced, folded after t1: the
                 * verifier compares the fold between the two builds. */
                for (k = 0; k < FIN_CALLS; k++)
                        bc_fp_add(&dig[(size_t) k * SHA1W], SHA1W);
        }
        free(ctx);
        return bc_median(samples, reps);
}

static uint64_t
role_mur_update(int reps, uint64_t *samples)
{
        struct isal_mh_sha1_murmur3_x64_128_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA1W + MURW];
        uint32_t i;
        int r;
        uint64_t t0, t1;

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        bc_fp_reset();
        for (r = 0; r < reps; r++) {
                /* Same rule as mh_sha1_update. */
                bc_fill(big, UPD_LEN, bc_salt() + 0x5000 * (uint64_t) (r + 1));
                if (isal_mh_sha1_murmur3_x64_128_init(ctx, MURSEED)) {
                        fprintf(stderr, "bench: murmur init failed\n");
                        exit(3);
                }
                t0 = bc_now_ns();
                for (i = 0; i < UPD_ITERS; i++) {
                        if (isal_mh_sha1_murmur3_x64_128_update(ctx, big, UPD_LEN)) {
                                fprintf(stderr, "bench: murmur update failed\n");
                                exit(3);
                        }
                }
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
        }
        /* Same rule as mh_sha1_update: the timed path's output is checked by
         * finishing the last repetition's context (untimed) and folding the
         * digest pair the verifier's base build also produces. */
        if (isal_mh_sha1_murmur3_x64_128_finalize(ctx, dig, dig + SHA1W)) {
                fprintf(stderr, "bench: murmur finalize failed\n");
                exit(3);
        }
        bc_fp_add(dig, SHA1W + MURW);
        free(ctx);
        return bc_median(samples, reps);
}

static uint64_t
role_mur_finalize(int reps, uint64_t *samples)
{
        struct isal_mh_sha1_murmur3_x64_128_ctx *ctx = bc_alloc(sizeof(*ctx) * FIN_CALLS);
        uint32_t dig[FIN_CALLS * (SHA1W + MURW)];
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
                        /* Same rule as the sha1 finalize role. */
                        if (isal_mh_sha1_murmur3_x64_128_init(&ctx[k], MURSEED) ||
                            isal_mh_sha1_murmur3_x64_128_update(&ctx[k], big + k, FIN_LEN)) {
                                fprintf(stderr, "bench: murmur setup failed\n");
                                exit(3);
                        }
                }
                t0 = bc_now_ns();
                for (k = 0; k < FIN_CALLS; k++) {
                        if (isal_mh_sha1_murmur3_x64_128_finalize(
                                &ctx[k], &dig[(size_t) k * (SHA1W + MURW)],
                                &dig[(size_t) k * (SHA1W + MURW)] + SHA1W)) {
                                fprintf(stderr, "bench: murmur finalize failed\n");
                                exit(3);
                        }
                }
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
                /* Every digest pair the timed region produced, folded after t1. */
                for (k = 0; k < FIN_CALLS; k++)
                        bc_fp_add(&dig[(size_t) k * (SHA1W + MURW)], SHA1W + MURW);
        }
        free(ctx);
        return bc_median(samples, reps);
}

/* ------------------------------------------------------------------ */
/* dump stream                                                          */

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
 * matter for finalize -- 0 and 1 (trivial), 8 (length field inside the first
 * murmur block), BLOCK/2, and the BLOCK-9/-8/-7/-1 neighbourhood where the
 * padding flips between one and two block-function passes. */
static const uint32_t tail_p[] = { 0, 1, 8, 512, 1015, 1016, 1017, 1023 };
static const uint32_t tail_k[] = { 0, 1, 8, 16 };

/* update()-heavy extra: a long burst of sub-block updates, so the
 * partial-block accumulation path dominates rather than whole-block
 * throughput. */
static const uint32_t small_lens[] = { 1024, 4096, 65536, 262144 };
static const uint32_t small_chunks[] = { 1, 1, 2, 1, 3, 1, 5, 1, 8, 1, 13, 1 };
#define NSMALL (sizeof(small_chunks) / sizeof(small_chunks[0]))

/* finalize()-heavy extra: every one of these lengths ends on the two-block
 * padding path (total_length % BLOCK == BLOCK-8), driven by a different
 * streaming split so the partial buffer is built up differently. */
static const uint32_t tail_split[] = { 1, 64, 1024, 9208 };
#define NTAILSPLIT (sizeof(tail_split) / sizeof(tail_split[0]))

static void
stream_sha1(struct isal_mh_sha1_ctx *ctx, const uint8_t *p, uint32_t len, uint32_t step)
{
        uint32_t off = 0, ci = 0;

        while (off < len) {
                uint32_t c = step ? step : chunks[ci++ % NCHUNK];

                if (c > len - off)
                        c = len - off;
                if (isal_mh_sha1_update(ctx, p + off, c)) {
                        fprintf(stderr, "bench: dump update failed\n");
                        exit(3);
                }
                off += c;
                step = 0;
        }
}

static void
stream_mur(struct isal_mh_sha1_murmur3_x64_128_ctx *ctx, const uint8_t *p, uint32_t len,
           uint32_t step)
{
        uint32_t off = 0, ci = 0;

        while (off < len) {
                uint32_t c = step ? step : chunks[ci++ % NCHUNK];

                if (c > len - off)
                        c = len - off;
                if (isal_mh_sha1_murmur3_x64_128_update(ctx, p + off, c)) {
                        fprintf(stderr, "bench: dump update failed\n");
                        exit(3);
                }
                off += c;
                step = 0;
        }
}

/* One whole-buffer job: init, a single update of the whole length, finalize. */
static void
one_sha1(struct sink *s, uint32_t len)
{
        struct isal_mh_sha1_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA1W];

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha1_init(ctx) || isal_mh_sha1_update(ctx, dbuf, len) ||
            isal_mh_sha1_finalize(ctx, dig)) {
                fprintf(stderr, "bench: dump job failed at len %u\n", len);
                exit(3);
        }
        sink_put(s, dig, SHA1W);
        free(ctx);
}

static void
one_mur(struct sink *s, uint32_t len, uint64_t seed, uint32_t step)
{
        struct isal_mh_sha1_murmur3_x64_128_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA1W + MURW];

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha1_murmur3_x64_128_init(ctx, seed)) {
                fprintf(stderr, "bench: dump init failed at len %u\n", len);
                exit(3);
        }
        if (step)
                stream_mur(ctx, dbuf, len, step);
        else if (isal_mh_sha1_murmur3_x64_128_update(ctx, dbuf, len)) {
                fprintf(stderr, "bench: dump update failed at len %u\n", len);
                exit(3);
        }
        if (isal_mh_sha1_murmur3_x64_128_finalize(ctx, dig, dig + SHA1W)) {
                fprintf(stderr, "bench: dump finalize failed at len %u\n", len);
                exit(3);
        }
        sink_put(s, dig, SHA1W + MURW);
        free(ctx);
}

static void
one_stream_sha1(struct sink *s, uint32_t len)
{
        struct isal_mh_sha1_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA1W];

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha1_init(ctx)) {
                fprintf(stderr, "bench: dump init failed at len %u\n", len);
                exit(3);
        }
        stream_sha1(ctx, dbuf, len, 0);
        if (isal_mh_sha1_finalize(ctx, dig)) {
                fprintf(stderr, "bench: dump finalize failed at len %u\n", len);
                exit(3);
        }
        sink_put(s, dig, SHA1W);
        free(ctx);
}

/* A burst of small chunked updates over one ctx, then finalize. */
static void
one_small_sha1(struct sink *s, uint32_t len)
{
        struct isal_mh_sha1_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA1W];
        uint32_t off = 0, ci = 0;

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha1_init(ctx)) {
                fprintf(stderr, "bench: dump init failed at len %u\n", len);
                exit(3);
        }
        while (off < len) {
                uint32_t c = small_chunks[ci++ % NSMALL];

                if (c > len - off)
                        c = len - off;
                if (isal_mh_sha1_update(ctx, dbuf + off, c)) {
                        fprintf(stderr, "bench: dump update failed at len %u\n", len);
                        exit(3);
                }
                off += c;
        }
        if (isal_mh_sha1_finalize(ctx, dig)) {
                fprintf(stderr, "bench: dump finalize failed at len %u\n", len);
                exit(3);
        }
        sink_put(s, dig, SHA1W);
        free(ctx);
}

/* A streaming split right up to BLOCK*k + BLOCK-8, then the two-block padding. */
static void
one_tail_split_sha1(struct sink *s, uint32_t step)
{
        uint32_t len = BLOCK * 8 + (BLOCK - 8);
        struct isal_mh_sha1_ctx *ctx = bc_alloc(sizeof(*ctx));
        uint32_t dig[SHA1W];

        if (!ctx) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        if (isal_mh_sha1_init(ctx)) {
                fprintf(stderr, "bench: dump init failed\n");
                exit(3);
        }
        stream_sha1(ctx, dbuf, len, step);
        if (isal_mh_sha1_finalize(ctx, dig)) {
                fprintf(stderr, "bench: dump finalize failed\n");
                exit(3);
        }
        sink_put(s, dig, SHA1W);
        free(ctx);
}

static void
pass_sha1(struct sink *s, int is_update)
{
        uint32_t i, nt;

        nt = (uint32_t) (sizeof(dump_lens) / sizeof(dump_lens[0]));
        for (i = 0; i < nt; i++)
                one_sha1(s, dump_lens[i]);
        nt = (uint32_t) (sizeof(stream_lens) / sizeof(stream_lens[0]));
        for (i = 0; i < nt; i++)
                one_stream_sha1(s, stream_lens[i]);
        nt = (uint32_t) (sizeof(tail_p) / sizeof(tail_p[0]));
        for (i = 0; i < nt; i++) {
                uint32_t j, nk = (uint32_t) (sizeof(tail_k) / sizeof(tail_k[0]));

                for (j = 0; j < nk; j++)
                        one_sha1(s, BLOCK * tail_k[j] + tail_p[i]);
        }
        if (is_update) {
                nt = (uint32_t) (sizeof(small_lens) / sizeof(small_lens[0]));
                for (i = 0; i < nt; i++)
                        one_small_sha1(s, small_lens[i]);
        } else {
                for (i = 0; i < (uint32_t) NTAILSPLIT; i++)
                        one_tail_split_sha1(s, tail_split[i]);
        }
}

static void
pass_mur(struct sink *s, uint64_t seed, int is_update)
{
        uint32_t i, nt;

        nt = (uint32_t) (sizeof(dump_lens) / sizeof(dump_lens[0]));
        for (i = 0; i < nt; i++)
                one_mur(s, dump_lens[i], seed, 0);
        nt = (uint32_t) (sizeof(stream_lens) / sizeof(stream_lens[0]));
        for (i = 0; i < nt; i++)
                one_mur(s, stream_lens[i], seed, 1);
        nt = (uint32_t) (sizeof(tail_p) / sizeof(tail_p[0]));
        for (i = 0; i < nt; i++) {
                uint32_t j, nk = (uint32_t) (sizeof(tail_k) / sizeof(tail_k[0]));

                for (j = 0; j < nk; j++)
                        one_mur(s, BLOCK * tail_k[j] + tail_p[i], seed, 0);
        }
        if (is_update) {
                nt = (uint32_t) (sizeof(small_lens) / sizeof(small_lens[0]));
                for (i = 0; i < nt; i++) {
                        /* burst of small chunked updates on one ctx */
                        struct isal_mh_sha1_murmur3_x64_128_ctx *ctx =
                                bc_alloc(sizeof(*ctx));
                        uint32_t dig[SHA1W + MURW];
                        uint32_t len = small_lens[i], off = 0, ci = 0;

                        if (!ctx) {
                                fprintf(stderr, "bench: out of memory\n");
                                exit(2);
                        }
                        if (isal_mh_sha1_murmur3_x64_128_init(ctx, seed)) {
                                fprintf(stderr, "bench: dump init failed\n");
                                exit(3);
                        }
                        while (off < len) {
                                uint32_t c = small_chunks[ci++ % NSMALL];

                                if (c > len - off)
                                        c = len - off;
                                if (isal_mh_sha1_murmur3_x64_128_update(ctx, dbuf + off, c)) {
                                        fprintf(stderr, "bench: dump update failed\n");
                                        exit(3);
                                }
                                off += c;
                        }
                        if (isal_mh_sha1_murmur3_x64_128_finalize(ctx, dig, dig + SHA1W)) {
                                fprintf(stderr, "bench: dump finalize failed\n");
                                exit(3);
                        }
                        sink_put(s, dig, SHA1W + MURW);
                        free(ctx);
                }
        } else {
                for (i = 0; i < (uint32_t) NTAILSPLIT; i++)
                        one_mur(s, BLOCK * 8 + (BLOCK - 8), seed, tail_split[i]);
        }
}

static void
mode_dump(const char *fnid, const char *path)
{
        struct sink s;
        int is_mur = strncmp(fnid, "mh_sha1_murmur3", 15) == 0;
        int is_update = strstr(fnid, "_update") != NULL;

        s.w = bc_alloc(DCAP * sizeof(*s.w));
        if (!s.w) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        s.n = 0;
        s.cap = DCAP;

        if (is_mur) {
                /* Two murmur seeds, so the seed plumbing and the murmur
                 * digest words are both covered. */
                pass_mur(&s, 0, is_update);
                pass_mur(&s, MURSEED, is_update);
        } else {
                pass_sha1(&s, is_update);
        }

        bc_dump_be32(path, s.w, s.n);
        /* A stream written to stdout ("-") is the bytes the verifier compares,
         * so the human-readable summary is only emitted for a named file. */
        if (strcmp(path, "-") != 0) {
                printf("dumped %u digests\n", (unsigned) (s.n / (is_mur ? (SHA1W + MURW) : SHA1W)));
                fflush(stdout);
        }
        free(s.w);
}

/* ------------------------------------------------------------------ */

static int
known(const char *fnid)
{
        return strcmp(fnid, "mh_sha1_update") == 0 ||
               strcmp(fnid, "mh_sha1_finalize") == 0 ||
               strcmp(fnid, "mh_sha1_murmur3_x64_128_update") == 0 ||
               strcmp(fnid, "mh_sha1_murmur3_x64_128_finalize") == 0;
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

        if (strcmp(fnid, "mh_sha1_update") == 0)
                role_sha1_update(reps, samples);
        else if (strcmp(fnid, "mh_sha1_finalize") == 0)
                role_sha1_finalize(reps, samples);
        else if (strcmp(fnid, "mh_sha1_murmur3_x64_128_update") == 0)
                role_mur_update(reps, samples);
        else
                role_mur_finalize(reps, samples);

        bc_report(fnid, samples, reps);
        return 0;
}
