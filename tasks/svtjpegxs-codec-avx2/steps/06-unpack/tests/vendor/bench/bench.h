/*
 * codecbench -- per-function differential timing harness for the SVT-JPEG-XS
 * ladder.  One source, compiled into whichever tree it is measuring; the
 * verifier builds it twice (this step's base tree and the candidate) and
 * compares the two outputs.
 *
 * Design rules it exists to satisfy:
 *   - every case calls the implementation the library INSTALLED for that slot,
 *     through the rtcd function pointer, so portable-C work is measured like a
 *     hand-written kernel;
 *   - inputs are derived from a seed, and each correctness round draws a fresh
 *     one, so nothing can be memoised behind the seam;
 *   - the ratio is a median of repeated batches normalised to one call, never a
 *     best-of;
 *   - a case whose inputs are invalid for the implementation under test must
 *     report failure loudly, not silently emit a fast number.
 */
#ifndef BENCH_H
#define BENCH_H

/* _GNU_SOURCE comes from the command line */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_DIGEST_BYTES 32

typedef struct bench_case {
    const char *name;
    size_t ctx_size;
    void (*setup)(void *ctx, uint64_t seed, int round); /* build inputs; round<0 = timing draw */
    void (*call)(void *ctx);                 /* invoke the installed method    */
    void (*hash)(void *ctx, uint8_t out[BENCH_DIGEST_BYTES]); /* digest outputs */
} bench_case;

void bench_add(const bench_case *c);
void bench_add_all(const bench_case *c, size_t n);

/* ------------------------------------------------------------------ rng ---
 * splitmix64: deterministic across compilers and platforms (integer only), so
 * the two builds draw byte-identical inputs from the same seed. */
static inline uint64_t bench_rng(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Random value in a closed signed range, without signed-overflow UB. */
static inline int32_t bench_rand_range(uint64_t *s, int32_t lo, int32_t hi) {
    uint32_t span = (uint32_t)((int64_t)hi - (int64_t)lo + 1);
    return (int32_t)((int64_t)lo + (int64_t)(bench_rng(s) % span));
}

static inline uint32_t bench_rand_u32(uint64_t *s, uint32_t mask) {
    return (uint32_t)(bench_rng(s) & mask);
}

/* ----------------------------------------------------------------- hash ---
 * FNV-1a 64 over the output bytes, then folded to a fixed-width hex string.
 * A plain byte sum would hide a single-word difference; this does not. */
static inline uint64_t bench_fnv1a(const void *data, size_t n, uint64_t h) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

static inline void bench_hash_finish(uint64_t h, uint8_t out[BENCH_DIGEST_BYTES]) {
    memset(out, 0, BENCH_DIGEST_BYTES);
    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(h >> 24);
        out[i * 4 + 1] = (uint8_t)(h >> 16);
        out[i * 4 + 2] = (uint8_t)(h >> 8);
        out[i * 4 + 3] = (uint8_t)h;
        h = h * 0x100000001B3ULL + 0xCBF29CE484222325ULL;
    }
}

/* ----------------------------------------------------------------- misc ---
 * Aligned, zeroed scratch.  Alignment is deliberately 64 so the two trees see
 * the same layout; nothing about the buffer is part of the contract. */
static inline void *bench_alloc(size_t bytes) {
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes ? bytes : 64) != 0)
        return NULL;
    memset(p, 0, bytes ? bytes : 64);
    return p;
}

/* CPU time of THIS thread, not wall clock.
 *
 * The host is shared, and wall-clock time charges this harness for every
 * millisecond it spends descheduled -- which on a contended box is most of the
 * run-to-run variance, and it lands on the numerator and the denominator
 * independently.  Measured here: two runs of one binary against byte-identical
 * source gave per-function ratios from 0.38 to 1.47 under wall clock.  Thread
 * CPU time removes that term; what is left is cache and frequency interference,
 * which is far smaller.  Every kernel measured here is single-threaded. */
static inline uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline int bench_cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* Registration entry point of each per-stage case file. */
void bench_register_step01(void);
void bench_register_step02(void);
void bench_register_step03(void);
void bench_register_step04(void);
void bench_register_step05(void);
void bench_register_step06(void);
void bench_register_step07(void);
void bench_register_step08(void);

#endif /* BENCH_H */
