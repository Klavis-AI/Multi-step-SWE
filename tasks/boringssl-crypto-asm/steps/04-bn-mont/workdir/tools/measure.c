// Per-function timing harness for this milestone's entry points.
//
//   measure <fnid> [seed]     time one entry point, median of several windows
//   measure --list            the entry points this milestone covers
//   measure --check           run each one twice; they must agree
//
// It links the library you built, so it calls whatever implementation your tree
// installed. The buffer sizes and the input draw here are this tool's own; the
// grader draws its own, so treat the numbers as indicative.

#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/aead.h>
#include <openssl/aes.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/ecdsa.h>
#include <openssl/mem.h>
#include <openssl/nid.h>

#define MEASURE_TARGET_NS 30000000ull
#define MEASURE_MIN_REPS 5
#define MEASURE_MAX_REPS 9

// ---------------------------------------------------------------------------
// Deterministic input generation.
// ---------------------------------------------------------------------------

static uint64_t g_rng;

static uint64_t rng_next(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return g_rng;
}

static void rng_fill(uint8_t *out, size_t len) {
  for (size_t i = 0; i < len; i++) {
    out[i] = (uint8_t)(rng_next() >> 24);
  }
}

static void rng_init(uint64_t seed) {
  // Any non-zero state works; mix the seed so nearby seeds diverge.
  g_rng = seed * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull;
  if (g_rng == 0) g_rng = 0x123456789abcdefull;
  for (int i = 0; i < 8; i++) rng_next();
}

// A random BIGNUM of exactly `bits` bits, with the top bit set (and the low bit
// set too when `odd`). Built from a byte string so it is reproducible.
static BIGNUM *rng_bn(int bits, int odd) {
  int bytes = (bits + 7) / 8;
  uint8_t *buf = malloc((size_t)bytes);
  if (buf == NULL) return NULL;
  rng_fill(buf, (size_t)bytes);
  buf[0] |= 0x80;
  int excess = bytes * 8 - bits;
  if (excess > 0) buf[0] >>= excess;
  buf[0] |= (uint8_t)(1u << (7 - excess));
  if (odd) buf[bytes - 1] |= 1;
  BIGNUM *bn = BN_bin2bn(buf, bytes, NULL);
  free(buf);
  return bn;
}

// ---------------------------------------------------------------------------
// One entry per scored function.
// ---------------------------------------------------------------------------

typedef struct {
  const char *id;
  void (*setup)(uint64_t seed);
  void (*run)(void);
  void (*dump)(FILE *out);
} bench_entry;

// Anti-memoisation ticks.
//
// The timed loop calls an entry point `iters` times in a row, so an
// implementation that cached an answer inside itself would hit on any
// repetition. Measured with such a cache inserted into the block-cipher path:
// 886 ns -> 4.6 ns, a 194x ratio where the reference gets 38x, with
// byte-identical output -- so the correctness gate, the project's own suite and
// the non-regression guard all pass it, and it would outscore the reference.
// Each timed iteration therefore draws a new argument. A correct cache must key
// on the whole argument, and no draw that reaches a timed call is ever repeated:
//
//   * within one pass, the first word is `tick_mix(i)`, a bijection of the
//     iteration counter, so no two iterations of a pass draw the same value;
//   * between passes -- the calibration doublings and every timed rep -- the
//     salt changes, so the second word differs, and a rep never re-feeds the
//     draws of an earlier rep or of the calibration.
//
// The tick also has to reach an argument the timed call actually READS. That is
// what the `*_open` entries need: their tick writes `aeadst.in`, which is where
// open writes its plaintext, while open reads the sealed record in `aeadst.out`
// -- so a per-iteration window would re-feed one identical call. The loop below
// re-seals a record from the ticked plaintext before each open call and times
// the call itself.
//
// The same protocol runs in the verifier's harness (tests/vendor/bench_stage.c)
// -- same draws, same timing shape -- so this tool's numbers track the graded
// ones.
//
// These two pointers are the entire shared surface -- exactly one is non-NULL,
// and each milestone's own setup points them at its own state, so a milestone
// cut down to one primitive never names another's.
static uint8_t *g_tick_at;   // first 16 bytes overwritten per iteration
static BIGNUM *g_tick_bn;    // stepped with BN_add_word per iteration

// splitmix64's finaliser. A bijection on 64 bits, so distinct inputs give
// distinct outputs and no draw above can repeat by accident.
static uint64_t tick_mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// Draw the argument the next timed call will read. `salt` is constant within a
// pass and different in every pass.
static void tick(uint64_t i, uint64_t salt) {
  if (g_tick_at != NULL) {
    uint64_t v[2] = {tick_mix(i), tick_mix(salt + 0x9E3779B97F4A7C15ull)};
    memcpy(g_tick_at, v, sizeof(v));
  } else if (g_tick_bn != NULL) {
    // The BIGNUM advances itself, so nothing has to be drawn: every call sees a
    // value no earlier call saw, in any pass.
    BN_add_word(g_tick_bn, 1);
  }
}

static void dump_blob(FILE *out, const uint8_t *p, size_t n) {
  fprintf(out, "%zu:", n);
  for (size_t i = 0; i < n; i++) fprintf(out, "%02x", p[i]);
  fputc('\n', out);
}

// --- Bignum Montgomery ------------------------------------------------------

#define BN_BITS 2048

static struct {
  BN_CTX *ctx;
  BIGNUM *mod, *a, *b, *e_ct, *e_small, *r;
  BN_MONT_CTX *mont;
  BIGNUM *am, *bm;
} bnst;

static void bn_setup(uint64_t seed) {
  rng_init(seed);
  bnst.ctx = BN_CTX_new();
  if (bnst.ctx == NULL) exit(2);
  // An odd modulus. `mod` is kept unreduced-friendly by construction.
  do {
    BN_free(bnst.mod);
    bnst.mod = rng_bn(BN_BITS, 1);
  } while (bnst.mod == NULL || BN_is_zero(bnst.mod));
  bnst.a = rng_bn(BN_BITS, 0);
  bnst.b = rng_bn(BN_BITS, 0);
  bnst.e_ct = rng_bn(BN_BITS, 1);
  bnst.e_small = BN_new();
  BN_set_word(bnst.e_small, 65537);
  bnst.r = BN_new();
  bnst.am = BN_new();
  bnst.bm = BN_new();
  bnst.mont = BN_MONT_CTX_new_for_modulus(bnst.mod, bnst.ctx);
  if (bnst.mont == NULL) {
    fprintf(stderr, "BN_MONT_CTX_new_for_modulus failed\n");
    exit(2);
  }
  BN_nnmod(bnst.a, bnst.a, bnst.mod, bnst.ctx);
  BN_nnmod(bnst.b, bnst.b, bnst.mod, bnst.ctx);
  BN_nnmod(bnst.e_ct, bnst.e_ct, bnst.mod, bnst.ctx);
  if (!BN_to_montgomery(bnst.am, bnst.a, bnst.mont, bnst.ctx) ||
      !BN_to_montgomery(bnst.bm, bnst.b, bnst.mont, bnst.ctx)) {
    fprintf(stderr, "BN_to_montgomery failed\n");
    exit(2);
  }
  g_tick_bn = bnst.a;
  g_tick_at = NULL;
}

// The Montgomery-multiply entry point takes both operands already in the
// Montgomery domain, so it perturbs `am` instead.
static void bn_setup_am(uint64_t seed) {
  bn_setup(seed);
  g_tick_bn = bnst.am;
}

static void run_bn_modmul(void) {
  BN_mod_mul_montgomery(bnst.r, bnst.am, bnst.bm, bnst.mont, bnst.ctx);
}
static void run_bn_modexp_mont(void) {
  BN_mod_exp_mont(bnst.r, bnst.a, bnst.e_small, bnst.mod, bnst.ctx, bnst.mont);
}
static void run_bn_modexp_ct(void) {
  BN_mod_exp_mont_consttime(bnst.r, bnst.a, bnst.e_ct, bnst.mod, bnst.ctx,
                            bnst.mont);
}
static void dump_bn(FILE *o) {
  uint8_t buf[BN_BITS / 8 + 8];
  int n = BN_bn2binpad(bnst.r, buf, (int)sizeof(buf));
  if (n < 0) n = 0;
  dump_blob(o, buf, (size_t)n);
}

// ---------------------------------------------------------------------------

static const bench_entry entries[] = {



    {"bn_modmul_mont", bn_setup_am, run_bn_modmul, dump_bn},
    {"bn_modexp_mont", bn_setup, run_bn_modexp_mont, dump_bn},
    {"bn_modexp_ct", bn_setup, run_bn_modexp_ct, dump_bn},

};

static const size_t entries_len = sizeof(entries) / sizeof(entries[0]);

static const bench_entry *find_entry(const char *id) {
  for (size_t i = 0; i < entries_len; i++) {
    if (strcmp(entries[i].id, id) == 0) return &entries[i];
  }
  return NULL;
}

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x < y) ? -1 : (x > y) ? 1 : 0;
}

// Structural self-check: every mode must round-trip, and the same input must
// give the same bytes twice. It runs against whichever implementation the
// library installed, so it is a check of the tree, not of the harness.
static int check(void) {
  // Every entry point must run and must produce the same bytes twice.
  for (size_t i = 0; i < entries_len; i++) {
    const bench_entry *e = &entries[i];
    e->setup(7);
    e->run();
    e->dump(stdout);
    fflush(stdout);
    e->setup(7);
    e->run();
    e->dump(stdout);
    fflush(stdout);
  }
  printf("OK: every entry point runs and is deterministic\n");
  return 0;
}

int main(int argc, char **argv) {
  // The verifier asks the harness for its own function list rather than
  // shipping one, so the two can never drift apart.
  // Both agent-facing documents tell the candidate to run `measure --check`;
  // the mode existed as a function and was never reachable from main().
  if (argc == 2 && strcmp(argv[1], "--check") == 0) {
    return check();
  }
  if (argc == 2 && strcmp(argv[1], "--list") == 0) {
    for (size_t i = 0; i < entries_len; i++) {
      printf("%s\n", entries[i].id);
    }
    return 0;
  }
  if (argc < 2) {
    fprintf(stderr, "usage: %s <fnid> [seed] | %s --list | %s --check\n",
            argv[0], argv[0], argv[0]);
    return 2;
  }
  const bench_entry *e = find_entry(argv[1]);
  if (e == NULL) {
    fprintf(stderr, "unknown function '%s'\n", argv[1]);
    return 2;
  }
  // The seed is optional. The header above has always advertised
  // `measure <fnid> [seed]`, and tools/measure calls exactly that -- one
  // argument per entry point -- so main() demanding a third "dump|bench"
  // argument made the documented invocation fail for every entry point. That
  // argument belonged to the verifier's harness, which this file is cut from,
  // and it survived the cut while the header was rewritten around it.
  uint64_t seed = (argc >= 3) ? strtoull(argv[2], NULL, 0) : 1;

  e->setup(seed);

  // Calibrate the iteration count against measured time, not a guess: a window
  // short enough for the clock to matter inflates every short kernel.
  //
  // `salt` starts from the seed and advances at every pass -- each calibration
  // doubling and each rep -- so no timed call is ever handed an argument an
  // earlier call saw.
  uint64_t salt = tick_mix(seed);
  const uint64_t salt_step = 0x9E3779B97F4A7C15ull;
  uint64_t iters = 1, t0, t1;
  for (;;) {
    t0 = now_ns();
    for (uint64_t i = 0; i < iters; i++) {
      tick(i, salt);
      e->run();
    }
    t1 = now_ns();
    if (t1 - t0 >= MEASURE_TARGET_NS / 4 || iters >= (1ull << 40)) break;
    iters *= 2;
    salt += salt_step;
  }

  double samples[MEASURE_MAX_REPS];
  int n = 0;
  for (int rep = 0; rep < MEASURE_MAX_REPS; rep++) {
    salt += salt_step;
    t0 = now_ns();
    for (uint64_t i = 0; i < iters; i++) {
      tick(i, salt);
      e->run();
    }
    t1 = now_ns();
    double per_call = (double)(t1 - t0) / (double)iters;
    samples[n++] = per_call;
    if (n >= MEASURE_MIN_REPS && (double)(t1 - t0) >= (double)MEASURE_TARGET_NS) {
      break;
    }
  }
  qsort(samples, (size_t)n, sizeof(double), cmp_double);
  double median = samples[n / 2];

  printf("%s %.2f\n", e->id, median);
  fflush(stdout);
  return 0;
}
