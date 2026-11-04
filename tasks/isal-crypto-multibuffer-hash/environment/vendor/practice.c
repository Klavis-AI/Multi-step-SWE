/**********************************************************************
  practice.c -- practice measurement tool for the multi-buffer hashing
  stages of isa-l_crypto.

  This file is installed into the agent image as /app/tools/practice.c
  and driven by /app/tools/measure.sh, which compiles it once per stage:

      gcc -O2 -DPRACTICE_<STAGE_UPPER> practice.c ... bin/isa-l_crypto.a

  Exactly one PRACTICE_<STAGE> macro is defined per build, so only that
  stage's block of code is compiled.

  For the selected stage the tool measures every scored unit and prints
  one line per unit:

      unit   installed_ns   reference_ns   ratio

  "installed_ns" is the time of the implementation the library currently
  exposes through the PUBLIC API (isal_*), "reference_ns" is the time of
  the portable C reference the stage falls back to, and "ratio" is
  reference_ns / installed_ns -- higher is better.  On a tree whose
  optimized code has been removed the two columns measure the same C
  code and the ratio reads ~1.0; on a tree with the optimized code in
  place the ratio is the speedup earned so far.

  Every unit is measured as the median of several repetitions.  The two
  sides are timed side by side within a repetition, into separate
  buffers, and their order alternates between repetitions, so that
  neither a slow stretch of the machine nor a warm cache can favour one
  side.  Buffers are filled from a splitmix64 PRNG with a fixed seed;
  the content changes from repetition to repetition.  Output does not
  depend on the number of CPUs.

  The clock is the CPU time of the whole process -- CLOCK_PROCESS_CPUTIME_ID
  plus the CPU time of any child process it reaps -- not wall time and not
  the calling thread alone.  Two reasons, in that order.  On a machine shared
  with other jobs a wall-clock reading of a millisecond-scale region is
  routinely inflated several-fold by descheduling, and CPU time is the part of
  the cost this code actually controls.  And the grader times exactly this
  quantity: if this tool times the calling thread alone, work parked on a
  helper thread or a forked child reads as free here while the grader charges
  it, so the tool would not be a measurement of what is scored.  A
  single-threaded, childless implementation reads the same number either way,
  and both columns get the same treatment.

  The C reference symbols are non-static and C-linkage, so this file can
  reach them with its own extern declarations.  Several of them carry
  ISAL_DEPRECATED in the public headers (they exist purely as the
  portable fallback and correctness oracle); ISAL_UNIT_TEST is the
  library's own switch for "this is a test translation unit, do not warn
  about the deprecated aliases" and is defined here for that reason.
**********************************************************************/

#define _POSIX_C_SOURCE 200809L
/* getrusage and RUSAGE_CHILDREN (the clock below) are not POSIX declarations;
   naming a feature macro at all would otherwise turn the default set off. */
#define _DEFAULT_SOURCE 1
#define ISAL_UNIT_TEST 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#if !defined(PRACTICE_SHA256_MB) && !defined(PRACTICE_SHA1_MB) && !defined(PRACTICE_SHA512_MB) &&   \
        !defined(PRACTICE_MD5_MB) && !defined(PRACTICE_SM3_MB) && !defined(PRACTICE_MH_SHA1) &&     \
        !defined(PRACTICE_MH_SHA1_MURMUR3_X64_128) && !defined(PRACTICE_MH_SHA256) &&              \
        !defined(PRACTICE_ROLLING_HASH)
#error "practice.c: define exactly one -DPRACTICE_<STAGE> macro"
#endif

/* ------------------------------------------------------------------ *
 *  helpers shared by every stage
 * ------------------------------------------------------------------ */

static void
fail(const char *what)
{
        fprintf(stderr, "practice: %s\n", what);
        exit(1);
}

static void *
xalloc(size_t bytes)
{
        void *p = malloc(bytes);

        if (p == NULL)
                fail("out of memory");
        memset(p, 0, bytes);
        return p;
}

/* The graded figure is the CPU time of the whole process -- every thread of
 * it, plus any child it waits on (see /app/BENCHMARK.md) -- so this tool times
 * the same quantity.  Measuring the calling thread alone would let work on a
 * helper thread read as free here while the grader charges it. */
static uint64_t
now_ns(void)
{
        struct timespec ts;
        struct rusage ru;
        uint64_t ns;

        if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0)
                fail("clock_gettime(CLOCK_PROCESS_CPUTIME_ID) failed");
        ns = (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
        if (getrusage(RUSAGE_CHILDREN, &ru) == 0)
                ns += ((uint64_t) ru.ru_utime.tv_sec + (uint64_t) ru.ru_stime.tv_sec) * 1000000000ull
                    + ((uint64_t) ru.ru_utime.tv_usec + (uint64_t) ru.ru_stime.tv_usec) * 1000ull;
        return ns;
}

/* splitmix64: deterministic, and cheap enough to fill megabytes with. */
static uint64_t
sm64_next(uint64_t *state)
{
        uint64_t z = (*state += 0x9E3779B97F4A7C15ull);

        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
}

static void
fill_bytes(uint8_t *dst, size_t bytes, uint64_t seed)
{
        uint64_t state = seed;
        size_t i = 0;

        for (; i + 8 <= bytes; i += 8) {
                uint64_t word = sm64_next(&state);
                memcpy(dst + i, &word, sizeof(word));
        }
        if (i < bytes) {
                uint64_t word = sm64_next(&state);
                memcpy(dst + i, &word, bytes - i);
        }
}

/* Sorts v in place and returns its median. */
static double
median_of(double *v, int n)
{
        int i, j;

        for (i = 1; i < n; i++) {
                double key = v[i];

                for (j = i - 1; j >= 0 && v[j] > key; j--)
                        v[j + 1] = v[j];
                v[j + 1] = key;
        }
        return v[n / 2];
}

static void
report_head(const char *stage)
{
        printf("stage %s\n", stage);
        printf("%-34s %16s %16s %9s\n", "unit", "installed_ns", "reference_ns", "ratio");
}

static void
report_row(const char *unit, double installed_ns, double reference_ns)
{
        double ratio = (installed_ns > 0.0) ? reference_ns / installed_ns : 0.0;

        printf("%-34s %16.1f %16.1f %9.3f\n", unit, installed_ns, reference_ns, ratio);
}

/* ------------------------------------------------------------------ *
 *  stage body for the five *_mb stages
 *
 *  submit: a stream of whole-buffer ISAL_HASH_ENTIRE jobs, drained.
 *  flush : a short batch of jobs (fewer than the narrowest ISA tier has
 *          lanes, lengths not block multiples), drained.
 *  In both cases the timed region covers the whole operation: every
 *  submit, every flush call and the drain loop.  The C reference hashes
 *  inside submit and its flush is a no-op, so timing only the flush
 *  calls would compare one side's whole cost against the other side's
 *  front half.
 *
 *  Macros consumed from the enclosing stage block:
 *    STAGE_NAME, CTX_T, MGR_T, MGR_INIT, MGR_SUBMIT, MGR_FLUSH,
 *    MGR_INIT_B, MGR_SUBMIT_B, MGR_FLUSH_B, MB_POOL_N,
 *    MB_NJOBS, MB_JOBLEN, MB_NBATCH, MB_BATCHLEN, MB_NREP, MB_SEED
 * ------------------------------------------------------------------ */
#define PRACTICE_MB_BODY                                                                           \
        extern void MGR_INIT_B(MGR_T * mgr);                                                       \
        extern CTX_T *MGR_SUBMIT_B(MGR_T * mgr, CTX_T *ctx, const void *buffer, uint32_t len,      \
                                   ISAL_HASH_CTX_FLAG flags);                                      \
        extern CTX_T *MGR_FLUSH_B(MGR_T * mgr);                                                    \
                                                                                                   \
        static CTX_T practice_ctx[MB_POOL_N];                                                      \
        static MGR_T practice_mgr;                                                                 \
        static uint8_t *practice_buf_installed;                                                    \
        static uint8_t *practice_buf_reference;                                                    \
                                                                                                   \
        /* One whole operation: njobs whole-buffer jobs of len bytes, then a drain. */             \
        static void practice_op_installed(uint32_t njobs, uint32_t len, uint8_t *buf)              \
        {                                                                                          \
                CTX_T *done = NULL;                                                                \
                uint32_t i;                                                                        \
                                                                                                   \
                if (MGR_INIT(&practice_mgr) != 0)                                                  \
                        fail("ctx_mgr_init failed");                                               \
                for (i = 0; i < njobs; i++) {                                                      \
                        if (MGR_SUBMIT(&practice_mgr, &practice_ctx[i], &done,                     \
                                       buf + (size_t) i * len, len, ISAL_HASH_ENTIRE) != 0)        \
                                fail("ctx_mgr_submit failed");                                     \
                }                                                                                  \
                do {                                                                               \
                        if (MGR_FLUSH(&practice_mgr, &done) != 0)                                  \
                                fail("ctx_mgr_flush failed");                                      \
                } while (done != NULL);                                                            \
        }                                                                                          \
                                                                                                   \
        /* The same operation through the portable C reference. */                                 \
        static void practice_op_reference(uint32_t njobs, uint32_t len, uint8_t *buf)              \
        {                                                                                          \
                uint32_t i;                                                                        \
                                                                                                   \
                MGR_INIT_B(&practice_mgr);                                                         \
                for (i = 0; i < njobs; i++)                                                        \
                        (void) MGR_SUBMIT_B(&practice_mgr, &practice_ctx[i],                       \
                                            buf + (size_t) i * len, len, ISAL_HASH_ENTIRE);        \
                (void) MGR_FLUSH_B(&practice_mgr);                                                 \
        }                                                                                          \
                                                                                                   \
        static void practice_unit(const char *unit, uint32_t njobs, uint32_t len, uint64_t seed)   \
        {                                                                                          \
                double installed[MB_NREP], reference[MB_NREP];                                     \
                size_t bytes = (size_t) njobs * len;                                               \
                int rep;                                                                           \
                                                                                                   \
                for (rep = 0; rep < MB_NREP; rep++) {                                              \
                        uint64_t t0, t1;                                                           \
                                                                                                   \
                        fill_bytes(practice_buf_installed, bytes, seed + (uint64_t) rep);          \
                        fill_bytes(practice_buf_reference, bytes, seed + (uint64_t) rep);          \
                        if (rep & 1) {                                                             \
                                t0 = now_ns();                                                     \
                                practice_op_reference(njobs, len, practice_buf_reference);         \
                                t1 = now_ns();                                                     \
                                reference[rep] = (double) (t1 - t0);                               \
                                t0 = now_ns();                                                     \
                                practice_op_installed(njobs, len, practice_buf_installed);         \
                                t1 = now_ns();                                                     \
                                installed[rep] = (double) (t1 - t0);                               \
                        } else {                                                                   \
                                t0 = now_ns();                                                     \
                                practice_op_installed(njobs, len, practice_buf_installed);         \
                                t1 = now_ns();                                                     \
                                installed[rep] = (double) (t1 - t0);                               \
                                t0 = now_ns();                                                     \
                                practice_op_reference(njobs, len, practice_buf_reference);         \
                                t1 = now_ns();                                                     \
                                reference[rep] = (double) (t1 - t0);                               \
                        }                                                                          \
                }                                                                                  \
                report_row(unit, median_of(installed, MB_NREP), median_of(reference, MB_NREP));    \
        }                                                                                          \
                                                                                                   \
        int main(void)                                                                             \
        {                                                                                          \
                size_t bytes = (size_t) MB_NJOBS * MB_JOBLEN;                                      \
                uint32_t i;                                                                        \
                                                                                                   \
                if ((size_t) MB_NBATCH * MB_BATCHLEN > bytes)                                      \
                        bytes = (size_t) MB_NBATCH * MB_BATCHLEN;                                  \
                practice_buf_installed = xalloc(bytes);                                            \
                practice_buf_reference = xalloc(bytes);                                            \
                for (i = 0; i < MB_POOL_N; i++)                                                    \
                        isal_hash_ctx_init(&practice_ctx[i]);                                      \
                                                                                                   \
                report_head(STAGE_NAME);                                                           \
                practice_unit("submit", MB_NJOBS, MB_JOBLEN, MB_SEED);                             \
                practice_unit("flush", MB_NBATCH, MB_BATCHLEN, MB_SEED + 0x1000);                  \
                                                                                                   \
                free(practice_buf_installed);                                                      \
                free(practice_buf_reference);                                                      \
                return 0;                                                                          \
        }

/* ------------------------------------------------------------------ *
 *  stage body for the three mh_* stages
 *
 *  update  : a loop of update calls over a large buffer.
 *  finalize: only the finalize call is timed, on a context fed an
 *            untimed update whose length is deliberately not a multiple
 *            of the stage's frame size, so finalize has a substantial
 *            partial frame to complete and pad.
 *
 *  Macros consumed from the enclosing stage block:
 *    STAGE_NAME, MH_CTX_T, MH_INIT, MH_UP_CALL, MH_FIN_CALL,
 *    MH_UP_B_CALL, MH_FIN_B_CALL, MH_UP_NAME, MH_FIN_NAME,
 *    MH_UP_LEN, MH_UP_CALLS, MH_FEED, MH_NREP, MH_SEED, MH_FSEED
 * ------------------------------------------------------------------ */
#define PRACTICE_MH_BODY                                                                           \
        static MH_CTX_T practice_ctx;                                                              \
        static uint8_t *practice_buf_installed;                                                    \
        static uint8_t *practice_buf_reference;                                                    \
        static uint8_t *practice_feed_installed;                                                   \
        static uint8_t *practice_feed_reference;                                                   \
        static uint32_t practice_digest[16];                                                       \
                                                                                                   \
        int main(void)                                                                             \
        {                                                                                          \
                double installed[MH_NREP], reference[MH_NREP];                                     \
                int rep;                                                                           \
                uint32_t k;                                                                        \
                                                                                                   \
                practice_buf_installed = xalloc(MH_UP_LEN);                                        \
                practice_buf_reference = xalloc(MH_UP_LEN);                                        \
                practice_feed_installed = xalloc(MH_FEED);                                         \
                practice_feed_reference = xalloc(MH_FEED);                                         \
                                                                                                   \
                report_head(STAGE_NAME);                                                           \
                                                                                                   \
                for (rep = 0; rep < MH_NREP; rep++) {                                              \
                        uint64_t t0, t1;                                                           \
                                                                                                   \
                        fill_bytes(practice_buf_installed, MH_UP_LEN, MH_SEED + (uint64_t) rep);   \
                        fill_bytes(practice_buf_reference, MH_UP_LEN, MH_SEED + (uint64_t) rep);   \
                        if (rep & 1) {                                                             \
                                if (MH_INIT(&practice_ctx) != 0)                                   \
                                        fail("init failed");                                       \
                                t0 = now_ns();                                                     \
                                for (k = 0; k < MH_UP_CALLS; k++) {                                \
                                        if (MH_UP_B_CALL(&practice_ctx, practice_buf_reference,    \
                                                         MH_UP_LEN) != 0)                      \
                                                fail("reference update failed");                   \
                                }                                                                  \
                                t1 = now_ns();                                                     \
                                reference[rep] = (double) (t1 - t0);                               \
                                if (MH_INIT(&practice_ctx) != 0)                                   \
                                        fail("init failed");                                       \
                                t0 = now_ns();                                                     \
                                for (k = 0; k < MH_UP_CALLS; k++) {                                \
                                        if (MH_UP_CALL(&practice_ctx, practice_buf_installed,      \
                                                       MH_UP_LEN) != 0)                        \
                                                fail("update failed");                             \
                                }                                                                  \
                                t1 = now_ns();                                                     \
                                installed[rep] = (double) (t1 - t0);                               \
                        } else {                                                                   \
                                if (MH_INIT(&practice_ctx) != 0)                                   \
                                        fail("init failed");                                       \
                                t0 = now_ns();                                                     \
                                for (k = 0; k < MH_UP_CALLS; k++) {                                \
                                        if (MH_UP_CALL(&practice_ctx, practice_buf_installed,      \
                                                       MH_UP_LEN) != 0)                        \
                                                fail("update failed");                             \
                                }                                                                  \
                                t1 = now_ns();                                                     \
                                installed[rep] = (double) (t1 - t0);                               \
                                if (MH_INIT(&practice_ctx) != 0)                                   \
                                        fail("init failed");                                       \
                                t0 = now_ns();                                                     \
                                for (k = 0; k < MH_UP_CALLS; k++) {                                \
                                        if (MH_UP_B_CALL(&practice_ctx, practice_buf_reference,    \
                                                         MH_UP_LEN) != 0)                      \
                                                fail("reference update failed");                   \
                                }                                                                  \
                                t1 = now_ns();                                                     \
                                reference[rep] = (double) (t1 - t0);                               \
                        }                                                                          \
                }                                                                                  \
                report_row(MH_UP_NAME, median_of(installed, MH_NREP),                              \
                           median_of(reference, MH_NREP));                                         \
                                                                                                   \
                for (rep = 0; rep < MH_NREP; rep++) {                                              \
                        uint64_t t0, t1;                                                           \
                                                                                                   \
                        fill_bytes(practice_feed_installed, MH_FEED, MH_FSEED + (uint64_t) rep);   \
                        fill_bytes(practice_feed_reference, MH_FEED, MH_FSEED + (uint64_t) rep);   \
                        if (MH_INIT(&practice_ctx) != 0)                                           \
                                fail("init failed");                                               \
                        if (MH_UP_CALL(&practice_ctx, practice_feed_installed, MH_FEED) != 0)      \
                                fail("update failed");                                             \
                        t0 = now_ns();                                                             \
                        if (MH_FIN_CALL(&practice_ctx, practice_digest,                            \
                                        practice_digest + 8) != 0)                                 \
                                fail("finalize failed");                                           \
                        t1 = now_ns();                                                             \
                        installed[rep] = (double) (t1 - t0);                                       \
                                                                                                   \
                        if (MH_INIT(&practice_ctx) != 0)                                           \
                                fail("init failed");                                               \
                        if (MH_UP_B_CALL(&practice_ctx, practice_feed_reference, MH_FEED) != 0)    \
                                fail("reference update failed");                                   \
                        t0 = now_ns();                                                             \
                        if (MH_FIN_B_CALL(&practice_ctx, practice_digest,                          \
                                          practice_digest + 8) != 0)                               \
                                fail("reference finalize failed");                                 \
                        t1 = now_ns();                                                             \
                        reference[rep] = (double) (t1 - t0);                                       \
                }                                                                                  \
                report_row(MH_FIN_NAME, median_of(installed, MH_NREP),                             \
                           median_of(reference, MH_NREP));                                         \
                                                                                                   \
                free(practice_buf_installed);                                                      \
                free(practice_buf_reference);                                                      \
                free(practice_feed_installed);                                                     \
                free(practice_feed_reference);                                                     \
                return 0;                                                                          \
        }

/* ================================================================== *
 *  sha256_mb
 * ================================================================== */
#if defined(PRACTICE_SHA256_MB)

#include "multi_buffer.h"
#include "sha256_mb.h"

#define STAGE_NAME    "sha256_mb"
#define CTX_T         ISAL_SHA256_HASH_CTX
#define MGR_T         ISAL_SHA256_HASH_CTX_MGR
#define MGR_INIT      isal_sha256_ctx_mgr_init
#define MGR_SUBMIT    isal_sha256_ctx_mgr_submit
#define MGR_FLUSH     isal_sha256_ctx_mgr_flush
#define MGR_INIT_B    _sha256_ctx_mgr_init_base
#define MGR_SUBMIT_B  _sha256_ctx_mgr_submit_base
#define MGR_FLUSH_B   _sha256_ctx_mgr_flush_base
#define MB_POOL_N     (MB_NJOBS > MB_NBATCH ? MB_NJOBS : MB_NBATCH)

/* 40 whole-buffer jobs of 200061 B (not a 64 B multiple) per repetition. */
#define MB_NJOBS      40u
#define MB_JOBLEN     200061u
/* 3 jobs -- the narrowest ISA tier of this stage has 4 lanes -- of 1048589 B. */
#define MB_NBATCH     (ISAL_SHA256_MIN_LANES - 1u)
#define MB_BATCHLEN   1048589u
#define MB_NREP       8
#define MB_SEED       0x5A2564A1ull

PRACTICE_MB_BODY

#endif /* PRACTICE_SHA256_MB */

/* ================================================================== *
 *  sha1_mb
 * ================================================================== */
#if defined(PRACTICE_SHA1_MB)

#include "multi_buffer.h"
#include "sha1_mb.h"

#define STAGE_NAME    "sha1_mb"
#define CTX_T         ISAL_SHA1_HASH_CTX
#define MGR_T         ISAL_SHA1_HASH_CTX_MGR
#define MGR_INIT      isal_sha1_ctx_mgr_init
#define MGR_SUBMIT    isal_sha1_ctx_mgr_submit
#define MGR_FLUSH     isal_sha1_ctx_mgr_flush
#define MGR_INIT_B    _sha1_ctx_mgr_init_base
#define MGR_SUBMIT_B  _sha1_ctx_mgr_submit_base
#define MGR_FLUSH_B   _sha1_ctx_mgr_flush_base
#define MB_POOL_N     (MB_NJOBS > MB_NBATCH ? MB_NJOBS : MB_NBATCH)

/* 36 whole-buffer jobs of 180027 B per repetition. */
#define MB_NJOBS      36u
#define MB_JOBLEN     180027u
/* 3 jobs -- the narrowest ISA tier of this stage has 4 lanes -- of 1048603 B. */
#define MB_NBATCH     (ISAL_SHA1_MIN_LANES - 1u)
#define MB_BATCHLEN   1048603u
#define MB_NREP       8
#define MB_SEED       0x5A1A2B3Cull

PRACTICE_MB_BODY

#endif /* PRACTICE_SHA1_MB */

/* ================================================================== *
 *  sha512_mb
 * ================================================================== */
#if defined(PRACTICE_SHA512_MB)

#include "multi_buffer.h"
#include "sha512_mb.h"

#define STAGE_NAME    "sha512_mb"
#define CTX_T         ISAL_SHA512_HASH_CTX
#define MGR_T         ISAL_SHA512_HASH_CTX_MGR
#define MGR_INIT      isal_sha512_ctx_mgr_init
#define MGR_SUBMIT    isal_sha512_ctx_mgr_submit
#define MGR_FLUSH     isal_sha512_ctx_mgr_flush
#define MGR_INIT_B    _sha512_ctx_mgr_init_base
#define MGR_SUBMIT_B  _sha512_ctx_mgr_submit_base
#define MGR_FLUSH_B   _sha512_ctx_mgr_flush_base
#define MB_POOL_N     (MB_NJOBS > MB_NBATCH ? MB_NJOBS : MB_NBATCH)

/* 24 whole-buffer jobs of 147539 B per repetition. */
#define MB_NJOBS      24u
#define MB_JOBLEN     147539u
/* 1 job -- even the narrowest ISA tier of this stage has 2 lanes -- of 1048619 B. */
#define MB_NBATCH     (ISAL_SHA512_MIN_LANES - 1u)
#define MB_BATCHLEN   1048619u
#define MB_NREP       8
#define MB_SEED       0x5512C0DEull

PRACTICE_MB_BODY

#endif /* PRACTICE_SHA512_MB */

/* ================================================================== *
 *  md5_mb
 * ================================================================== */
#if defined(PRACTICE_MD5_MB)

#include "multi_buffer.h"
#include "md5_mb.h"

#define STAGE_NAME    "md5_mb"
#define CTX_T         ISAL_MD5_HASH_CTX
#define MGR_T         ISAL_MD5_HASH_CTX_MGR
#define MGR_INIT      isal_md5_ctx_mgr_init
#define MGR_SUBMIT    isal_md5_ctx_mgr_submit
#define MGR_FLUSH     isal_md5_ctx_mgr_flush
#define MGR_INIT_B    _md5_ctx_mgr_init_base
#define MGR_SUBMIT_B  _md5_ctx_mgr_submit_base
#define MGR_FLUSH_B   _md5_ctx_mgr_flush_base
#define MB_POOL_N     (MB_NJOBS > MB_NBATCH ? MB_NJOBS : MB_NBATCH)

/* 32 whole-buffer jobs of 262181 B per repetition. */
#define MB_NJOBS      32u
#define MB_JOBLEN     262181u
/* 7 jobs -- the narrowest ISA tier of this stage has 8 lanes -- of 1048633 B. */
#define MB_NBATCH     (ISAL_MD5_MIN_LANES - 1u)
#define MB_BATCHLEN   1048633u
#define MB_NREP       8
#define MB_SEED       0x0D5BC0DEull

PRACTICE_MB_BODY

#endif /* PRACTICE_MD5_MB */

/* ================================================================== *
 *  sm3_mb
 * ================================================================== */
#if defined(PRACTICE_SM3_MB)

#include "multi_buffer.h"
#include "sm3_mb.h"

#define STAGE_NAME    "sm3_mb"
#define CTX_T         ISAL_SM3_HASH_CTX
#define MGR_T         ISAL_SM3_HASH_CTX_MGR
#define MGR_INIT      isal_sm3_ctx_mgr_init
#define MGR_SUBMIT    isal_sm3_ctx_mgr_submit
#define MGR_FLUSH     isal_sm3_ctx_mgr_flush
#define MGR_INIT_B    _sm3_ctx_mgr_init_base
#define MGR_SUBMIT_B  _sm3_ctx_mgr_submit_base
#define MGR_FLUSH_B   _sm3_ctx_mgr_flush_base
#define MB_POOL_N     (MB_NJOBS > MB_NBATCH ? MB_NJOBS : MB_NBATCH)

/* 32 whole-buffer jobs of 163859 B per repetition. */
#define MB_NJOBS      32u
#define MB_JOBLEN     163859u
/* 3 jobs -- the narrowest ISA tier of this stage has 4 lanes -- of 1048647 B. */
#define MB_NBATCH     3u
#define MB_BATCHLEN   1048647u
#define MB_NREP       8
#define MB_SEED       0x5A3B00DAull

PRACTICE_MB_BODY

#endif /* PRACTICE_SM3_MB */

/* ================================================================== *
 *  mh_sha1
 * ================================================================== */
#if defined(PRACTICE_MH_SHA1)

#include "mh_sha1.h"

#define STAGE_NAME    "mh_sha1"
#define MH_CTX_T      struct isal_mh_sha1_ctx
#define MH_INIT(ctx)  isal_mh_sha1_init(ctx)
#define MH_UP_CALL(ctx, buf, len)   isal_mh_sha1_update((ctx), (buf), (len))
#define MH_FIN_CALL(ctx, d1, d2)    isal_mh_sha1_finalize((ctx), (d1))
#define MH_UP_B_CALL(ctx, buf, len) mh_sha1_update_base((ctx), (buf), (len))
#define MH_FIN_B_CALL(ctx, d1, d2)  mh_sha1_finalize_base((ctx), (d1))
#define MH_UP_NAME    "mh_sha1_update"
#define MH_FIN_NAME   "mh_sha1_finalize"

/* 8 updates of 1048613 B each per repetition (not a 1024 B frame multiple). */
#define MH_UP_LEN     1048613u
#define MH_UP_CALLS   8u
/* Untimed feed of the finalize context: 1049445 = 1024 B frames + 869 B. */
#define MH_FEED       1049445u
/* Repetitions per unit.  The finalize region is a single call of ~10-35 us on
   the portable reference, the thinnest unit in the mh stages, and a median over
   8 samples of a region that short wanders by tens of percent run to run (the
   baseline spread in /app/BENCHMARK.md); 32 samples are still cheap because
   each repetition's untimed setup dominates. */
#define MH_NREP       32
#define MH_SEED       0x4D4831A1ull
#define MH_FSEED      0x4D4831F1ull

PRACTICE_MH_BODY

#endif /* PRACTICE_MH_SHA1 */

/* ================================================================== *
 *  mh_sha1_murmur3_x64_128
 * ================================================================== */
#if defined(PRACTICE_MH_SHA1_MURMUR3_X64_128)

#include "mh_sha1_murmur3_x64_128.h"

#define STAGE_NAME    "mh_sha1_murmur3_x64_128"
#define MH_CTX_T      struct isal_mh_sha1_murmur3_x64_128_ctx
/* This variant's init takes a murmur seed; a fixed one keeps runs equal. */
#define MH_INIT(ctx)  isal_mh_sha1_murmur3_x64_128_init((ctx), 0x0123456789abcdefull)
#define MH_UP_CALL(ctx, buf, len)                                                              \
        isal_mh_sha1_murmur3_x64_128_update((ctx), (buf), (len))
#define MH_FIN_CALL(ctx, d1, d2)                                                               \
        isal_mh_sha1_murmur3_x64_128_finalize((ctx), (d1), (d2))
#define MH_UP_B_CALL(ctx, buf, len)                                                            \
        mh_sha1_murmur3_x64_128_update_base((ctx), (buf), (len))
#define MH_FIN_B_CALL(ctx, d1, d2)                                                             \
        mh_sha1_murmur3_x64_128_finalize_base((ctx), (d1), (d2))
#define MH_UP_NAME    "mh_sha1_murmur3_x64_128_update"
#define MH_FIN_NAME   "mh_sha1_murmur3_x64_128_finalize"

/* 8 updates of 1048613 B each per repetition (not a 1024 B frame multiple). */
#define MH_UP_LEN     1048613u
#define MH_UP_CALLS   8u
/* Untimed feed of the finalize context: 1049445 = 1024 B frames + 869 B. */
#define MH_FEED       1049445u
/* Repetitions per unit.  The finalize region is a single call of ~10-35 us on
   the portable reference, the thinnest unit in the mh stages, and a median over
   8 samples of a region that short wanders by tens of percent run to run (the
   baseline spread in /app/BENCHMARK.md); 32 samples are still cheap because
   each repetition's untimed setup dominates. */
#define MH_NREP       32
#define MH_SEED       0x4D4832B1ull
#define MH_FSEED      0x4D4832F1ull

PRACTICE_MH_BODY

#endif /* PRACTICE_MH_SHA1_MURMUR3_X64_128 */

/* ================================================================== *
 *  mh_sha256
 * ================================================================== */
#if defined(PRACTICE_MH_SHA256)

#include "mh_sha256.h"

#define STAGE_NAME    "mh_sha256"
#define MH_CTX_T      struct isal_mh_sha256_ctx
#define MH_INIT(ctx)  isal_mh_sha256_init(ctx)
#define MH_UP_CALL(ctx, buf, len)   isal_mh_sha256_update((ctx), (buf), (len))
#define MH_FIN_CALL(ctx, d1, d2)    isal_mh_sha256_finalize((ctx), (d1))
#define MH_UP_B_CALL(ctx, buf, len) mh_sha256_update_base((ctx), (buf), (len))
#define MH_FIN_B_CALL(ctx, d1, d2)  mh_sha256_finalize_base((ctx), (d1))
#define MH_UP_NAME    "mh_sha256_update"
#define MH_FIN_NAME   "mh_sha256_finalize"

/* 8 updates of 1048613 B each per repetition (not a 1024 B frame multiple). */
#define MH_UP_LEN     1048613u
#define MH_UP_CALLS   8u
/* Untimed feed of the finalize context: 1049445 = 1024 B frames + 869 B. */
#define MH_FEED       1049445u
/* Repetitions per unit.  The finalize region is a single call of ~10-35 us on
   the portable reference, the thinnest unit in the mh stages, and a median over
   8 samples of a region that short wanders by tens of percent run to run (the
   baseline spread in /app/BENCHMARK.md); 32 samples are still cheap because
   each repetition's untimed setup dominates. */
#define MH_NREP       32
#define MH_SEED       0x4D4833C1ull
#define MH_FSEED      0x4D4833F1ull

PRACTICE_MH_BODY

#endif /* PRACTICE_MH_SHA256 */

/* ================================================================== *
 *  rolling_hash
 *
 *  The scored unit is a loop of isal_rolling_hash2_run() calls over the
 *  same buffer, with a mask/trigger pair chosen so that no window ever
 *  hits: every call must scan the whole buffer and report
 *  ISAL_FINGERPRINT_RET_MAX.  The reference is the scalar scan the ISA
 *  kernel replaces, called directly on the same window state -- on both
 *  trees that is exactly the code the installed run() ends up in.
 * ================================================================== */
#if defined(PRACTICE_ROLLING_HASH)

#include "rolling_hashx.h"

#define STAGE_NAME   "rolling_hash"
#define RH_W         32u
#define RH_LEN       1048637u /* 1 MiB + 61 B, arbitrary but odd */
#define RH_CALLS     8u
#define RH_NREP      8
#define RH_SEED      0x5056C101ull
#define RH_MASK      0xFFFFFFFFu
#define RH_TRIGGER   0xA5A5A5A5u

/* Scalar scan of the window; declared here because it lives only in the
 * internal headers. */
extern uint64_t
_rolling_hash2_run_until_base(uint32_t *idx, int max_idx, uint64_t *t1, uint64_t *t2, uint8_t *b1,
                              uint8_t *b2, uint64_t h, uint64_t mask, uint64_t trigger);

static struct isal_rh_state2 practice_state;
static uint8_t *practice_buf_installed;
static uint8_t *practice_buf_reference;

static void
practice_scan_installed(uint8_t *buf)
{
        uint32_t call;

        for (call = 0; call < RH_CALLS; call++) {
                uint32_t offset = 0;
                int match = ISAL_FINGERPRINT_RET_OTHER;

                if (isal_rolling_hash2_run(&practice_state, buf, RH_LEN, RH_MASK, RH_TRIGGER,
                                           &offset, &match) != 0)
                        fail("isal_rolling_hash2_run returned an error");
                if (match != ISAL_FINGERPRINT_RET_MAX)
                        fail("rolling hash reported a trigger hit inside the practice buffer");
        }
}

static void
practice_scan_reference(uint8_t *buf)
{
        uint32_t call;

        for (call = 0; call < RH_CALLS; call++) {
                uint32_t idx = 0;

                (void) _rolling_hash2_run_until_base(&idx, (int) RH_LEN, practice_state.table1,
                                                     practice_state.table2, buf, buf - RH_W,
                                                     practice_state.hash, RH_MASK, RH_TRIGGER);
                if (idx != RH_LEN)
                        fail("rolling hash reference scan stopped inside the practice buffer");
        }
}

int
main(void)
{
        double installed[RH_NREP], reference[RH_NREP];
        int rep;

        /* One extra window of leading bytes: the run functions read the
         * window history from buffer - w. */
        practice_buf_installed = xalloc(RH_LEN + RH_W);
        practice_buf_reference = xalloc(RH_LEN + RH_W);

        if (isal_rolling_hash2_init(&practice_state, RH_W) != 0)
                fail("isal_rolling_hash2_init failed");

        report_head(STAGE_NAME);

        for (rep = 0; rep < RH_NREP; rep++) {
                uint64_t t0, t1;

                fill_bytes(practice_buf_installed, RH_LEN + RH_W, RH_SEED + (uint64_t) rep);
                fill_bytes(practice_buf_reference, RH_LEN + RH_W, RH_SEED + (uint64_t) rep);
                if (rep & 1) {
                        if (isal_rolling_hash2_reset(&practice_state,
                                                     practice_buf_reference + RH_W) != 0)
                                fail("isal_rolling_hash2_reset failed");
                        t0 = now_ns();
                        practice_scan_reference(practice_buf_reference + RH_W);
                        t1 = now_ns();
                        reference[rep] = (double) (t1 - t0);
                        if (isal_rolling_hash2_reset(&practice_state,
                                                     practice_buf_installed + RH_W) != 0)
                                fail("isal_rolling_hash2_reset failed");
                        t0 = now_ns();
                        practice_scan_installed(practice_buf_installed + RH_W);
                        t1 = now_ns();
                        installed[rep] = (double) (t1 - t0);
                } else {
                        if (isal_rolling_hash2_reset(&practice_state,
                                                     practice_buf_installed + RH_W) != 0)
                                fail("isal_rolling_hash2_reset failed");
                        t0 = now_ns();
                        practice_scan_installed(practice_buf_installed + RH_W);
                        t1 = now_ns();
                        installed[rep] = (double) (t1 - t0);
                        if (isal_rolling_hash2_reset(&practice_state,
                                                     practice_buf_reference + RH_W) != 0)
                                fail("isal_rolling_hash2_reset failed");
                        t0 = now_ns();
                        practice_scan_reference(practice_buf_reference + RH_W);
                        t1 = now_ns();
                        reference[rep] = (double) (t1 - t0);
                }
        }

        report_row("rolling_hash2_run", median_of(installed, RH_NREP),
                   median_of(reference, RH_NREP));

        free(practice_buf_installed);
        free(practice_buf_reference);
        return 0;
}

#endif /* PRACTICE_ROLLING_HASH */
