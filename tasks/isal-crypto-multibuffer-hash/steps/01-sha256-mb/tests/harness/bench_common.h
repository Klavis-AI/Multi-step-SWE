/* Shared scaffolding for the per-step measurement harnesses.
 *
 * Identical in every step's verifier image: it holds no per-step knowledge,
 * only the timing, RNG and reporting primitives the per-step sources use.
 *
 * The harness is compiled twice per step -- once against the step's base tree
 * (the pinned tree with every ladder stage's optimized implementation removed,
 * so its public symbols resolve to the portable C reference) and once against
 * the candidate -- and driven with the same arguments. The verbatim digest
 * files it writes are compared byte for byte, and the timings are divided.
 */
#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>

#define BCLOCK_NS 1000000000ull

/* Base of the timed roles' job content: fixed so every grading run scores the
 * same draw; the draw stays private to the verifier image (this header is
 * compiled into the verifier's harness and never reaches the agent's tree).
 * The call sites add a per-repetition and per-buffer offset to it, so every
 * repetition hashes different bytes and a memoised digest cannot stand in for
 * the work. */
#define BC_SALT_SEED 0x15A1C0DE5EED0001ull

static uint64_t bc_rng_state;

static void
bc_seed(uint64_t s)
{
        bc_rng_state = s ? s : 0x123456789abcdefull;
}

/* splitmix64: enough for buffer filling, no claim of cryptographic quality. */
static uint64_t
bc_rand(void)
{
        uint64_t z = (bc_rng_state += 0x9E3779B97F4A7C15ull);

        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
}

/* The draw's base -- see BC_SALT_SEED.  Only the timed roles call this;
 * `dump` output is deterministic by construction and is unaffected. */
static uint64_t
bc_salt(void)
{
        return BC_SALT_SEED;
}

/* CPU time of the whole process -- every thread of it, plus the CPU time any
 * of its own child processes consumed -- not CLOCK_MONOTONIC, and not the
 * calling thread alone.
 *
 * The grading host is shared, and wall-clock readings of a millisecond-scale
 * region inflate by up to 5x in CFS scheduling quanta when another tenant
 * lands on the same cores -- the same effect that makes an unpinned run-to-run
 * spread of ~13%.  CPU time counts only time
 * this process was actually running, which is what a single-threaded kernel
 * comparison wants, and it cannot be inflated by a co-runner.
 *
 * The whole process, not just the calling thread: the milestone is
 * single-threaded, and work parked on a helper thread while the calling
 * thread blocks must not read as free.  CLOCK_PROCESS_CPUTIME_ID counts every
 * thread of the process; RUSAGE_CHILDREN covers a forked helper reaped while
 * the parent waited on it.  A single-threaded, childless implementation --
 * the reference and every honest candidate -- reads exactly what
 * CLOCK_THREAD_CPUTIME_ID would. */
static uint64_t
bc_now_ns(void)
{
        struct timespec ts;
        struct rusage ru;
        uint64_t ns;

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
        ns = (uint64_t) ts.tv_sec * BCLOCK_NS + (uint64_t) ts.tv_nsec;
        if (getrusage(RUSAGE_CHILDREN, &ru) == 0)
                ns += ((uint64_t) ru.ru_utime.tv_sec + (uint64_t) ru.ru_stime.tv_sec) * BCLOCK_NS
                    + ((uint64_t) ru.ru_utime.tv_usec + (uint64_t) ru.ru_stime.tv_usec) * 1000ull;
        return ns;
}

/* Deterministic fill: identical on both builds for a given seed. */
static void
bc_fill(uint8_t *p, size_t n, uint64_t seed)
{
        size_t i;

        bc_seed(seed);
        for (i = 0; i + 8 <= n; i += 8) {
                uint64_t v = bc_rand();

                memcpy(p + i, &v, 8);
        }
        if (i < n) {
                uint64_t v = bc_rand();

                memcpy(p + i, &v, n - i);
        }
}

static void *
bc_alloc(size_t n)
{
        void *p = NULL;

        if (posix_memalign(&p, 64, n ? n : 64) != 0)
                return NULL;
        memset(p, 0, n);
        return p;
}

static int
bc_cmp_u64(const void *a, const void *b)
{
        uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;

        return (x > y) - (x < y);
}

/* Median of `n` samples.  Median rather than best-of-N: best-of-N suppresses
 * slow outliers, which is what it is for, but it *preserves* spuriously fast
 * ones, and a ratio then reports contention as a speedup. */
static uint64_t
bc_median(uint64_t *v, int n)
{
        qsort(v, n, sizeof(v[0]), bc_cmp_u64);
        return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

/* Write a dump to `path`, or to stdout when path is "-".
 *
 * The verifier passes "-" and captures the stream with its own (root-owned)
 * redirection: a dump written by a child of this process to a shared-writable
 * directory is a file the candidate's own code -- which runs as the same
 * unprivileged identity -- could rewrite afterwards, and the byte-for-byte
 * comparison would then compare bytes the candidate chose.  */
static void
bc_write(const char *path, const void *p, size_t n)
{
        FILE *f;

        if (strcmp(path, "-") == 0) {
                if (n && fwrite(p, 1, n, stdout) != n) {
                        fprintf(stderr, "bench: short write to stdout\n");
                        exit(2);
                }
                if (fflush(stdout) != 0) {
                        fprintf(stderr, "bench: flush of stdout failed\n");
                        exit(2);
                }
                return;
        }
        f = fopen(path, "wb");

        if (!f) {
                fprintf(stderr, "bench: cannot open %s\n", path);
                exit(2);
        }
        if (n && fwrite(p, 1, n, f) != n) {
                fprintf(stderr, "bench: short write to %s\n", path);
                exit(2);
        }
        if (fclose(f) != 0) {
                fprintf(stderr, "bench: close failed for %s\n", path);
                exit(2);
        }
}

/* Emit the digest stream so the two builds' outputs can be compared byte for
 * byte.  Big-endian, so the file is readable in a hexdump too. */
static void
bc_dump_be32(const char *path, const uint32_t *w, size_t n)
{
        uint8_t *out = malloc(n * 4);
        size_t i;

        if (!out) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        for (i = 0; i < n; i++) {
                out[i * 4 + 0] = (uint8_t) (w[i] >> 24);
                out[i * 4 + 1] = (uint8_t) (w[i] >> 16);
                out[i * 4 + 2] = (uint8_t) (w[i] >> 8);
                out[i * 4 + 3] = (uint8_t) w[i];
        }
        bc_write(path, out, n * 4);
        free(out);
}

static void
bc_dump_be64(const char *path, const uint64_t *w, size_t n)
{
        uint8_t *out = malloc(n * 8);
        size_t i;
        int j;

        if (!out) {
                fprintf(stderr, "bench: out of memory\n");
                exit(2);
        }
        for (i = 0; i < n; i++)
                for (j = 0; j < 8; j++)
                        out[i * 8 + j] = (uint8_t) (w[i] >> (56 - 8 * j));
        bc_write(path, out, n * 8);
        free(out);
}

/* --- output fingerprint of a timed role --------------------------------
 *
 * The timings decide the score, and the digest streams the verifier compares
 * byte for byte come from `dump` mode, which drives a wider job stream than
 * the timed roles do.  A tree whose timed path is fast and wrong -- a
 * shortcut keyed on the benchmark's own shape -- would therefore be scored on
 * medians whose outputs nothing ever looked at.
 *
 * Each timed role folds every digest it produced (bc_fp_reset, then bc_fp_add
 * per digest) into one 64-bit value, and bc_report prints it beside the
 * medians.  The verifier compares the base build's fingerprint with the
 * candidate's for the same unit; both sides hash the same bytes with the same
 * salt, so an implementation whose timed path computes the same digests as the
 * reference -- every honest one -- prints the same value, and one that returns
 * something else does not.  Folding happens after the timed region's clock has
 * been read, so it cannot move a sample.  */
static uint64_t bc_fp_state;
static uint32_t bc_fp_words;

static void
bc_fp_reset(void)
{
        bc_fp_state = 0xCBF29CE484222325ull;
        bc_fp_words = 0;
}

static void
bc_fp_add(const uint32_t *w, size_t n)
{
        size_t i;

        for (i = 0; i < n; i++) {
                bc_fp_state ^= (uint64_t) w[i];
                bc_fp_state *= 0x100000001B3ull;
                bc_fp_state ^= bc_fp_state >> 29;
                bc_fp_words++;
        }
}

/* Published arithmetic: one ratio per scoped function, and the step's figure
 * is the geometric mean of them (a function left alone contributes 1.0 and
 * drags the mean down, so breadth is forced).  Printed, not scored, here --
 * the verifier owns the reward.  `fp=` and `fpn=` carry the outputs of the
 * timed region: the verifier compares them between the two builds.  */
static void
bc_report(const char *fnid, const uint64_t *samples, int n)
{
        int i;

        printf("%s %llu", fnid, (unsigned long long) bc_median((uint64_t *) samples, n));
        for (i = 0; i < n; i++)
                printf(" %llu", (unsigned long long) samples[i]);
        printf(" fp=%016llx fpn=%u\n", (unsigned long long) bc_fp_state, bc_fp_words);
        fflush(stdout);
}

#endif /* BENCH_COMMON_H */
