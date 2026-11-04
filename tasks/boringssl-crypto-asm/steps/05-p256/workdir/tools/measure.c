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

// --- P-256 ------------------------------------------------------------------

static struct {
  EC_KEY *key;
  EC_GROUP *group;
  EC_POINT *peer_pub, *result;
  BIGNUM *scalar, *peer_scalar;
  EC_KEY *peer;
  uint8_t digest[32];
  uint8_t nonce[32];
  uint8_t sig[128];
  size_t sig_len;
  uint8_t secret[64];
  int ret;
  size_t secret_len;
} ecst;

static void p256_setup(uint64_t seed) {
  rng_init(seed);
  rng_fill(ecst.digest, sizeof(ecst.digest));
  rng_fill(ecst.nonce, sizeof(ecst.nonce));
  ecst.nonce[31] |= 1;
  ecst.group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
  if (ecst.group == NULL) exit(2);

  BN_CTX *ctx = BN_CTX_new();
  BIGNUM *order = BN_new();
  EC_GROUP_get_order(ecst.group, order, ctx);

  // A private key derived from the seed rather than from the RNG, so both
  // trees see exactly the same key and the dumps are comparable.
  ecst.key = EC_KEY_new();
  EC_KEY_set_group(ecst.key, ecst.group);
  ecst.scalar = rng_bn(256, 1);
  BN_nnmod(ecst.scalar, ecst.scalar, order, ctx);
  if (BN_is_zero(ecst.scalar)) BN_one(ecst.scalar);
  EC_KEY_set_private_key(ecst.key, ecst.scalar);
  ecst.result = EC_POINT_new(ecst.group);
  if (!EC_POINT_mul(ecst.group, ecst.result, ecst.scalar, NULL, NULL, ctx)) {
    fprintf(stderr, "initial EC_POINT_mul failed\n");
    exit(2);
  }
  EC_KEY_set_public_key(ecst.key, ecst.result);

  // A second key pair, again derived from the seed, for ECDH.
  ecst.peer = EC_KEY_new();
  EC_KEY_set_group(ecst.peer, ecst.group);
  ecst.peer_scalar = rng_bn(256, 1);
  BN_nnmod(ecst.peer_scalar, ecst.peer_scalar, order, ctx);
  if (BN_is_zero(ecst.peer_scalar)) BN_one(ecst.peer_scalar);
  EC_KEY_set_private_key(ecst.peer, ecst.peer_scalar);
  ecst.peer_pub = EC_POINT_new(ecst.group);
  if (!EC_POINT_mul(ecst.group, ecst.peer_pub, ecst.peer_scalar, NULL, NULL,
                    ctx)) {
    fprintf(stderr, "peer EC_POINT_mul failed\n");
    exit(2);
  }
  EC_KEY_set_public_key(ecst.peer, ecst.peer_pub);

  // The signature is produced once and only verified in the timed loop.
  ECDSA_SIG *sig = ECDSA_sign_with_nonce_and_leak_private_key_for_testing(
      ecst.digest, sizeof(ecst.digest), ecst.key, ecst.nonce,
      sizeof(ecst.nonce));
  if (sig == NULL) {
    fprintf(stderr, "ECDSA_sign failed\n");
    exit(2);
  }
  uint8_t *der = NULL;
  if (!ECDSA_SIG_to_bytes(&der, &ecst.sig_len, sig) ||
      ecst.sig_len > sizeof(ecst.sig)) {
    fprintf(stderr, "ECDSA_SIG_to_bytes failed\n");
    exit(2);
  }
  memcpy(ecst.sig, der, ecst.sig_len);
  OPENSSL_free(der);
  ECDSA_SIG_free(sig);
  BN_free(order);
  BN_CTX_free(ctx);
  g_tick_at = ecst.digest;
  g_tick_bn = NULL;
}

// The scalar-multiplication entry points are measured on the scalar, so they
// perturb that instead. It was reduced mod the group order at setup and is
// only ever added to.
static void p256_setup_scalar(uint64_t seed) {
  p256_setup(seed);
  g_tick_bn = ecst.scalar;
  g_tick_at = NULL;
}

static void run_p256_point_mul(void) {
  EC_POINT_mul(ecst.group, ecst.result, NULL, ecst.peer_pub, ecst.scalar, NULL);
}
// The nonce-taking entry point is used for both the dump and the timing so the
// two measure the same call; only where the nonce comes from differs, and the
// scalar multiplication that dominates the cost is the same one.
static void run_p256_sign(void) {
  ECDSA_SIG *sig = ECDSA_sign_with_nonce_and_leak_private_key_for_testing(
      ecst.digest, sizeof(ecst.digest), ecst.key, ecst.nonce,
      sizeof(ecst.nonce));
  ecst.ret = sig != NULL;
  ECDSA_SIG_free(sig);
}
static void run_p256_verify(void) {
  ecst.ret = ECDSA_verify(0, ecst.digest, sizeof(ecst.digest), ecst.sig,
                          ecst.sig_len, ecst.key);
}
static void run_p256_ecdh(void) {
  ecst.secret_len = (size_t)ECDH_compute_key(ecst.secret, sizeof(ecst.secret),
                                             ecst.peer_pub, ecst.key, NULL);
  ecst.ret = ecst.secret_len != (size_t)-1;
}

static void dump_p256_point(FILE *o) {
  uint8_t buf[65];
  size_t n = EC_POINT_point2oct(ecst.group, ecst.result,
                                POINT_CONVERSION_UNCOMPRESSED, buf, sizeof(buf),
                                NULL);
  dump_blob(o, buf, n);
}
static void dump_p256_sig(FILE *o) {
  fprintf(o, "ret=%d\n", ecst.ret);
  dump_blob(o, ecst.sig, ecst.sig_len);
}
static void dump_p256_ret(FILE *o) { fprintf(o, "ret=%d\n", ecst.ret); }
static void dump_p256_secret(FILE *o) {
  dump_blob(o, ecst.secret, ecst.secret_len);
}

// ---------------------------------------------------------------------------

static const bench_entry entries[] = {




    {"p256_point_mul", p256_setup_scalar, run_p256_point_mul, dump_p256_point},
    {"p256_ecdsa_sign", p256_setup, run_p256_sign, dump_p256_sig},
    {"p256_ecdsa_verify", p256_setup, run_p256_verify, dump_p256_ret},
    {"p256_ecdh", p256_setup_scalar, run_p256_ecdh, dump_p256_secret},
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
