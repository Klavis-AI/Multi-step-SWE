/* Per-function measurement harness for the md5 multi-buffer scheduler.
 *
 * Built twice by the verifier: once against this step's base tree (whose
 * public symbols resolve to the portable C reference) and once against the
 * candidate.  Both runs use the same arguments, so `dump` outputs are compared
 * byte for byte and `bench` medians are divided.
 *
 * Every role drives the PUBLIC entry points, so what is timed is whatever
 * implementation the library installed -- assembly, intrinsics or portable C;
 * the route does not matter, only the installed pointer does.
 *
 *   submit : a stream of 4096 whole-buffer jobs, submitted and then
 *            drained.  The timed region covers the whole operation on both
 *            sides, because the portable reference hashes inside submit while
 *            an ISA manager defers a partial batch to flush; timing submit
 *            alone would compare one side's whole cost against the other's
 *            front half.
 *   flush  : a short batch of 7 jobs, submitted and then drained, so
 *            the partial-lane path -- the one flush exists for -- carries the
 *            work.  Same rule: the timed region is the whole operation.
 *   dump   : one mixed job stream -- varied lengths, whole-buffer and
 *            multi-part submissions, lane-shortage and lane-overflow batches.
 */
#include "bench_common.h"
#include "md5_mb.h"

#define MSG_LEN      4096
#define MSG_LEN_ODD  (4096 - 8)     /* not a multiple of the block size */
#define NMSG        4096
#define NREP        12
#define FLUSH_K     7
#define FLUSH_REPS  512
#define NPOOL       (NMSG + ISAL_MD5_MAX_LANES * 2)

#define NCTX_DUMP    384
#define MAX_DUMP_LEN 65536

static ISAL_MD5_HASH_CTX_MGR *mgr;
static ISAL_MD5_HASH_CTX *ctxpool;
static uint8_t **bufs;

static void
setup(void)
{
        size_t i;

        mgr = bc_alloc(sizeof(*mgr));
        ctxpool = bc_alloc(sizeof(*ctxpool) * NPOOL);
        bufs = bc_alloc(sizeof(*bufs) * NPOOL);
        if (!mgr || !ctxpool || !bufs) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        for (i = 0; i < NPOOL; i++) {
                bufs[i] = bc_alloc(MSG_LEN);
                if (!bufs[i]) {
                        fprintf(stderr, "bench: out of memory\n");
                        exit(2);
                }
                bc_fill(bufs[i], MSG_LEN, 0x5a5a0000ull + i);
                isal_hash_ctx_init(&ctxpool[i]);
                ctxpool[i].user_data = (void *) (uintptr_t) i;
        }
}

static void
drain(void)
{
        ISAL_MD5_HASH_CTX *out = (void *) 1;
        int rc;

        while (out != NULL) {
                out = NULL;
                rc = isal_md5_ctx_mgr_flush(mgr, &out);
                if (rc) {
                        fprintf(stderr, "bench: flush rc=%d\n", rc);
                        exit(3);
                }
        }
}

/* Whole-buffer jobs: the steady-state batch path. */
static uint64_t
role_submit(int reps, uint64_t *samples)
{
        int r;
        uint32_t i;
        uint64_t t0, t1;
        ISAL_MD5_HASH_CTX *out;

        bc_fp_reset();
        for (r = 0; r < reps; r++) {
                /* Fresh content every repetition -- every buffer, not just
                 * the first -- and every process, bc_salt() being part of the
                 * seed.  No buffer the timed region hashes has been hashed
                 * before, so a memoised digest cannot stand in for the work.
                 * Untimed: the fill is outside t0..t1. */
                for (i = 0; i < NMSG; i++)
                        bc_fill(bufs[i], MSG_LEN, bc_salt() + 0x1000 * (uint64_t) (r + 1) + i);
                out = NULL;
                isal_md5_ctx_mgr_init(mgr);
                t0 = bc_now_ns();
                for (i = 0; i < NMSG; i++) {
                        if (isal_md5_ctx_mgr_submit(mgr, &ctxpool[i], &out, bufs[i],
                                                       MSG_LEN, ISAL_HASH_ENTIRE)) {
                                fprintf(stderr, "bench: submit failed\n");
                                exit(3);
                        }
                }
                drain();
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
                /* What this timed region produced: every job's result digest,
                 * folded after t1 so the timed region itself is unchanged.
                 * The verifier compares the fold between the two builds. */
                for (i = 0; i < NMSG; i++)
                        bc_fp_add(ctxpool[i].job.result_digest, ISAL_MD5_DIGEST_NWORDS);
        }
        return bc_median(samples, reps);
}

/* Short batches: the partial-lane path flush exists for.  The job length is
 * deliberately not a block multiple, so the partial-block path is priced too. */
static uint64_t
role_flush(int reps, uint64_t *samples)
{
        int r;
        uint32_t i;
        uint64_t t0, t1;
        ISAL_MD5_HASH_CTX *out;

        bc_fp_reset();
        for (r = 0; r < reps; r++) {
                out = NULL;
                /* Same rule as submit: every buffer in the batch is redrawn
                 * each repetition, so no call in the timed region repeats an
                 * earlier call's input. */
                for (i = 0; i < FLUSH_K; i++)
                        bc_fill(bufs[i], MSG_LEN_ODD, bc_salt() + 0x2000 * (uint64_t) (r + 1) + i);
                isal_md5_ctx_mgr_init(mgr);
                t0 = bc_now_ns();
                for (i = 0; i < FLUSH_K; i++) {
                        if (isal_md5_ctx_mgr_submit(mgr, &ctxpool[i], &out, bufs[i],
                                                       MSG_LEN_ODD, ISAL_HASH_ENTIRE)) {
                                fprintf(stderr, "bench: submit failed\n");
                                exit(3);
                        }
                }
                drain();
                t1 = bc_now_ns();
                samples[r] = t1 - t0;
                /* Same rule as submit: the batch's result digests, folded
                 * after t1, are this role's output fingerprint. */
                for (i = 0; i < FLUSH_K; i++)
                        bc_fp_add(ctxpool[i].job.result_digest, ISAL_MD5_DIGEST_NWORDS);
        }
        return bc_median(samples, reps);
}

/* ------------------------------------------------------------------ */

static const uint32_t dump_lens[] = { 1,    3,    55,   56,   63,   64,    65,
                                      111,  127,  128,  129,  191,  255,   256,
                                      1023, 1024, 4095, 4096, 4097, 16384, 65536 };

static void
mode_dump(const char *path)
{
        ISAL_MD5_HASH_CTX *pool = bc_alloc(sizeof(*pool) * NCTX_DUMP);
        uint8_t *big = bc_alloc(MAX_DUMP_LEN);
        uint32_t *dig;
        uint32_t i, n = 0, nlens = (uint32_t) (sizeof(dump_lens) / sizeof(dump_lens[0]));
        ISAL_MD5_HASH_CTX *out;

        if (!pool || !big) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        bc_fill(big, MAX_DUMP_LEN, 0xfeedface);
        for (i = 0; i < NCTX_DUMP; i++) {
                isal_hash_ctx_init(&pool[i]);
                pool[i].user_data = (void *) (uintptr_t) i;
        }

        out = NULL;
        isal_md5_ctx_mgr_init(mgr);

        /* 1. whole-buffer jobs, one ctx each, every length in the table */
        for (i = 0; i < 192; i++) {
                if (isal_md5_ctx_mgr_submit(mgr, &pool[n], &out, big, dump_lens[i % nlens],
                                               ISAL_HASH_ENTIRE)) {
                        fprintf(stderr, "bench: dump submit failed\n");
                        exit(3);
                }
                n++;
        }
        /* 2. lane shortage: fewer jobs than even the narrowest tier has lanes */
        for (i = 0; i < 3; i++) {
                if (isal_md5_ctx_mgr_submit(mgr, &pool[n], &out, big, dump_lens[n % nlens],
                                               ISAL_HASH_ENTIRE)) {
                        fprintf(stderr, "bench: dump submit failed\n");
                        exit(3);
                }
                n++;
        }
        drain();

        /* 3. multi-part submissions: FIRST / UPDATE* / LAST over one ctx.
         *
         * A job sitting in a multi-buffer lane cannot simply be submitted to
         * again -- the ISA managers reject that with ALREADY_PROCESSING, while
         * the portable C manager accepts it and completes synchronously.  So
         * the next part goes to whatever ctx the manager hands back, and when
         * it hands back nothing (a free lane took the job) a flush forces the
         * lane to give it up again.  One job is in flight at a time, which is
         * what makes that handoff unambiguous on both implementations. */
        for (i = 0; i < 64; i++) {
                uint32_t len = dump_lens[(i * 7) % nlens];
                uint32_t part = 64 * 13;
                uint32_t off, take;
                ISAL_HASH_CTX_FLAG fl;
                ISAL_MD5_HASH_CTX *feed;

                if (len < part)
                        part = len;

                isal_hash_ctx_init(&pool[n]);
                out = NULL;
                if (isal_md5_ctx_mgr_submit(mgr, &pool[n], &out, big, part, ISAL_HASH_FIRST)) {
                        fprintf(stderr, "bench: dump submit failed\n");
                        exit(3);
                }
                off = part;
                while (off < len) {
                        take = len - off;
                        if (take > 64 * 13)
                                take = 64 * 13;
                        fl = (off + take == len) ? ISAL_HASH_LAST : ISAL_HASH_UPDATE;
                        /* Prefer the ctx the manager handed back; if it handed
                         * back none, ask it for one. */
                        feed = out;
                        if (feed == NULL) {
                                if (isal_md5_ctx_mgr_flush(mgr, &out)) {
                                        fprintf(stderr, "bench: dump flush failed\n");
                                        exit(3);
                                }
                                feed = out ? out : &pool[n];
                        }
                        if (isal_md5_ctx_mgr_submit(mgr, feed, &out, big + off, take, fl)) {
                                fprintf(stderr, "bench: dump submit failed\n");
                                exit(3);
                        }
                        off += take;
                }
                drain();
                n++;
        }
        /* 4. let the manager keep working while more jobs arrive */
        for (i = 0; i < 96; i++) {
                if (isal_md5_ctx_mgr_submit(mgr, &pool[n], &out, big, dump_lens[i % nlens],
                                               ISAL_HASH_ENTIRE)) {
                        fprintf(stderr, "bench: dump submit failed\n");
                        exit(3);
                }
                n++;
        }
        drain();

        dig = bc_alloc((size_t) n * ISAL_MD5_DIGEST_NWORDS * sizeof(*dig));
        if (!dig) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        for (i = 0; i < n; i++)
                memcpy(&dig[(size_t) i * ISAL_MD5_DIGEST_NWORDS], pool[i].job.result_digest,
                       ISAL_MD5_DIGEST_NWORDS * sizeof(*dig));
        bc_dump_be32(path, dig, (size_t) n * ISAL_MD5_DIGEST_NWORDS);
        /* A stream written to stdout ("-") is the bytes the verifier compares,
         * so the human-readable summary is only emitted for a named file. */
        if (strcmp(path, "-") != 0) {
                printf("dumped %u digests\n", n);
                fflush(stdout);
        }
}

int
main(int argc, char **argv)
{
        const char *fnid, *mode;
        uint64_t *samples;
        int reps = NREP;

        if (argc < 3) {
                fprintf(stderr, "usage: %s <submit|flush|dump> <bench|dump> [arg] [reps]\n",
                        argv[0]);
                return 2;
        }
        fnid = argv[1];
        mode = argv[2];
        setup();

        if (strcmp(mode, "dump") == 0) {
                if (argc < 4) {
                        fprintf(stderr, "dump needs an output path\n");
                        return 2;
                }
                bc_seed(0xd00dfeed);
                mode_dump(argv[3]);
                return 0;
        }

        samples = bc_alloc(sizeof(*samples) * 8192);
        if (!samples) {
                fprintf(stderr, "bench: out of memory\n");
                return 2;
        }
        if (strcmp(fnid, "submit") == 0) {
                role_submit(reps, samples);
                bc_report("submit", samples, reps);
        } else if (strcmp(fnid, "flush") == 0) {
                role_flush(FLUSH_REPS, samples);
                bc_report("flush", samples, FLUSH_REPS);
        } else {
                fprintf(stderr, "bench: unknown function id '%s'\n", fnid);
                return 2;
        }
        return 0;
}
