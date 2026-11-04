// Per-function harness for one milestone.
//
// Built twice, once against the milestone's own base tree and once against the
// candidate's, so a function's timing is read off the implementation the library
// actually installed in each tree. Every entry point below is public API, so a
// candidate that speeds up the portable C path measures the same way as one who
// writes a kernel.
//
//   bench_stage <fnid> <seed> dump    -> one line per call, to stdout
//   bench_stage <fnid> <seed> bench   -> "<fnid> <ns per call>" on stdout
//
// Inputs are generated from the seed, so base and candidate see identical
// bytes and the dumps are comparable; the verifier picks a fresh seed each run,
// so nothing can be memoized behind the call.
//
// Timing protocol: calibrate the iteration count to a window long enough to
// swamp the clock (< 30 ms), then take the MEDIAN of several independent
// repetitions. A median rather than a best-of is deliberate -- best-of keeps a
// spuriously fast sample, and a ratio reports that as a speedup.

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

#define BENCH_TARGET_NS 30000000ull
#define BENCH_MIN_REPS 5
#define BENCH_MAX_REPS 9

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
// The second of those is what the first version of this fix missed: it drew
// `{i, i ^ constant}`, which restarts at the same values in every rep, so a
// cache with memory (rather than a single entry) hit on every call after the
// first rep and the median over the reps read the cache's speed.
//
// The tick also has to reach an argument the timed call actually READS. That is
// what the `*_open` entries broke: their tick wrote `aeadst.in`, which is where
// open writes its plaintext, while open reads the sealed record in `aeadst.out`
// -- so every timed call was identical and the draws above were beside the
// point. main() now re-seals a record from the ticked plaintext before each
// open call and times the call itself; see the comment there.
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

// --- AES block cipher -------------------------------------------------------

#define AES_BULK 4096
#define AES_KEY_BYTES 32

static struct {
  uint8_t key[AES_KEY_BYTES];
  uint8_t in[AES_BULK], out[AES_BULK];
  uint8_t ivec[16], ecount[16];
  unsigned int num;
  AES_KEY enc, dec;
  int bits;
} aesst;

static void aes_setup_common(uint64_t seed, int bits) {
  rng_init(seed);
  rng_fill(aesst.key, sizeof(aesst.key));
  rng_fill(aesst.in, sizeof(aesst.in));
  memset(aesst.out, 0, sizeof(aesst.out));
  memset(aesst.ivec, 0, sizeof(aesst.ivec));
  memset(aesst.ecount, 0, sizeof(aesst.ecount));
  aesst.num = 0;
  aesst.bits = bits;
  if (AES_set_encrypt_key(aesst.key, (unsigned)bits, &aesst.enc) != 0 ||
      AES_set_decrypt_key(aesst.key, (unsigned)bits, &aesst.dec) != 0) {
    fprintf(stderr, "AES key setup failed\n");
    exit(2);
  }
  g_tick_at = aesst.in;
  g_tick_bn = NULL;
}

static void aes_setup128(uint64_t s) { aes_setup_common(s, 128); }
static void aes_setup256(uint64_t s) { aes_setup_common(s, 256); }
// The key-schedule entry points are measured on the key, so they perturb that
// rather than the block.
static void aes_setup_key128(uint64_t s) { aes_setup_common(s, 128); g_tick_at = aesst.key; }
static void aes_setup_key256(uint64_t s) { aes_setup_common(s, 256); g_tick_at = aesst.key; }

// Key setup is compared by what the key does, not by the bytes it leaves in
// AES_KEY. Two correct implementations are free to lay the round keys out
// differently -- this library's own portable path stores a different schedule
// from its assembly path and both encrypt identically -- so dumping the struct
// would fail a correct tree for having been written correctly in a different
// style. Encrypting a fixed block with the produced key is the same test the
// rest of the harness applies: the output must not change.
static void run_aes_key128(void) {
  AES_KEY k;
  AES_set_encrypt_key(aesst.key, 128, &k);
  AES_encrypt(aesst.in, aesst.out, &k);
}
static void dump_aes_key128(FILE *o) { dump_blob(o, aesst.out, 16); }
static void run_aes_key256(void) {
  AES_KEY k;
  AES_set_encrypt_key(aesst.key, 256, &k);
  AES_encrypt(aesst.in, aesst.out, &k);
}
static void dump_aes_key256(FILE *o) { dump_blob(o, aesst.out, 16); }

static void run_aes_encrypt(void) {
  AES_encrypt(aesst.in, aesst.out, &aesst.enc);
}
static void run_aes_decrypt(void) {
  AES_decrypt(aesst.in, aesst.out, &aesst.dec);
}
static void dump_aes_block(FILE *o) { dump_blob(o, aesst.out, 16); }

static void run_aes_ecb(void) {
  memcpy(aesst.out, aesst.in, AES_BULK);
  AES_ecb_encrypt(aesst.out, aesst.out, &aesst.enc, AES_ENCRYPT);
}
static void dump_aes_ecb(FILE *o) { dump_blob(o, aesst.out, AES_BULK); }

static void run_aes_cbc(void) {
  uint8_t iv[16];
  memcpy(iv, aesst.ivec, 16);
  AES_cbc_encrypt(aesst.in, aesst.out, AES_BULK, &aesst.enc, iv, AES_ENCRYPT);
}
static void dump_aes_cbc(FILE *o) { dump_blob(o, aesst.out, AES_BULK); }

static void run_aes_ctr(void) {
  uint8_t iv[16], ec[16];
  unsigned int num = 0;
  memcpy(iv, aesst.ivec, 16);
  memset(ec, 0, 16);
  AES_ctr128_encrypt(aesst.in, aesst.out, AES_BULK, &aesst.enc, iv, ec, &num);
}
static void dump_aes_ctr(FILE *o) { dump_blob(o, aesst.out, AES_BULK); }

// --- AEAD -------------------------------------------------------------------

#define AEAD_BULK 16384
#define AEAD_AD 512
#define AEAD_OVERHEAD 32

static struct {
  EVP_AEAD_CTX ctx;
  int have_ctx;
  uint8_t key[32];
  size_t key_len;
  uint8_t nonce[12];
  uint8_t in[AEAD_BULK], out[AEAD_BULK + AEAD_OVERHEAD];
  uint8_t ad[AEAD_AD];
  size_t out_len;     // seal: ciphertext+tag bytes; open: plaintext bytes
  size_t ct_len;      // ciphertext+tag bytes produced by seal, kept for open
  int ret;
} aeadst;

static void aead_setup(uint64_t seed, const EVP_AEAD *aead, size_t key_len) {
  rng_init(seed);
  aeadst.key_len = key_len;
  rng_fill(aeadst.key, key_len);
  rng_fill(aeadst.nonce, sizeof(aeadst.nonce));
  rng_fill(aeadst.in, sizeof(aeadst.in));
  rng_fill(aeadst.ad, sizeof(aeadst.ad));
  memset(aeadst.out, 0, sizeof(aeadst.out));
  aeadst.out_len = 0;
  aeadst.ct_len = 0;
  aeadst.ret = 0;
  aeadst.have_ctx = EVP_AEAD_CTX_init(&aeadst.ctx, aead, aeadst.key, key_len,
                                      EVP_AEAD_DEFAULT_TAG_LENGTH, NULL);
  if (!aeadst.have_ctx) {
    fprintf(stderr, "AEAD init failed\n");
    exit(2);
  }
}

static void run_aead_seal(void) {
  aeadst.ret = EVP_AEAD_CTX_seal(&aeadst.ctx, aeadst.out, &aeadst.out_len,
                                 sizeof(aeadst.out), aeadst.nonce,
                                 sizeof(aeadst.nonce), aeadst.in,
                                 sizeof(aeadst.in), aeadst.ad, sizeof(aeadst.ad));
}

// The unsealed buffer is filled once, outside the timed region, so `open`
// measures decryption rather than a second seal.
static void aead_prepare_open(void) {
  if (EVP_AEAD_CTX_seal(&aeadst.ctx, aeadst.out, &aeadst.ct_len,
                        sizeof(aeadst.out), aeadst.nonce, sizeof(aeadst.nonce),
                        aeadst.in, sizeof(aeadst.in), aeadst.ad,
                        sizeof(aeadst.ad)) != 1) {
    fprintf(stderr, "AEAD pre-seal failed\n");
    exit(2);
  }
  aeadst.out_len = aeadst.ct_len;
}

static void run_aead_open(void) {
  size_t n = 0;
  aeadst.ret = EVP_AEAD_CTX_open(&aeadst.ctx, aeadst.in, &n,
                                 sizeof(aeadst.in), aeadst.nonce,
                                 sizeof(aeadst.nonce), aeadst.out,
                                 aeadst.ct_len, aeadst.ad, sizeof(aeadst.ad));
  aeadst.out_len = n;
}

static void dump_aead_seal(FILE *o) {
  fprintf(o, "ret=%d\n", aeadst.ret);
  dump_blob(o, aeadst.out, aeadst.out_len);
}
static void dump_aead_open(FILE *o) {
  fprintf(o, "ret=%d\n", aeadst.ret);
  dump_blob(o, aeadst.in, aeadst.ret == 1 ? aeadst.out_len : 0);
}

static void aead_setup_gcm128(uint64_t s) {
  g_tick_at = aeadst.in;
  g_tick_bn = NULL;
  aead_setup(s, EVP_aead_aes_128_gcm(), 16);
}
static void aead_setup_gcm256(uint64_t s) {
  g_tick_at = aeadst.in;
  g_tick_bn = NULL;
  aead_setup(s, EVP_aead_aes_256_gcm(), 32);
}
static void aead_setup_siv128(uint64_t s) {
  g_tick_at = aeadst.in;
  g_tick_bn = NULL;
  aead_setup(s, EVP_aead_aes_128_gcm_siv(), 16);
}
static void aead_setup_siv256(uint64_t s) {
  g_tick_at = aeadst.in;
  g_tick_bn = NULL;
  aead_setup(s, EVP_aead_aes_256_gcm_siv(), 32);
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
    {"aes_key128", aes_setup_key128, run_aes_key128, dump_aes_key128},
    {"aes_key256", aes_setup_key256, run_aes_key256, dump_aes_key256},
    {"aes_encrypt128", aes_setup128, run_aes_encrypt, dump_aes_block},
    {"aes_encrypt256", aes_setup256, run_aes_encrypt, dump_aes_block},
    {"aes_decrypt128", aes_setup128, run_aes_decrypt, dump_aes_block},
    {"aes_decrypt256", aes_setup256, run_aes_decrypt, dump_aes_block},
    {"aes_ecb128", aes_setup128, run_aes_ecb, dump_aes_ecb},
    {"aes_cbc128", aes_setup128, run_aes_cbc, dump_aes_cbc},
    {"aes_ctr128", aes_setup128, run_aes_ctr, dump_aes_ctr},

    {"gcm_seal128", aead_setup_gcm128, run_aead_seal, dump_aead_seal},
    {"gcm_open128", aead_setup_gcm128, run_aead_open, dump_aead_open},
    {"gcm_seal256", aead_setup_gcm256, run_aead_seal, dump_aead_seal},
    {"gcm_open256", aead_setup_gcm256, run_aead_open, dump_aead_open},

    {"siv_seal128", aead_setup_siv128, run_aead_seal, dump_aead_seal},
    {"siv_open128", aead_setup_siv128, run_aead_open, dump_aead_open},
    {"siv_seal256", aead_setup_siv256, run_aead_seal, dump_aead_seal},
    {"siv_open256", aead_setup_siv256, run_aead_open, dump_aead_open},

    {"bn_modmul_mont", bn_setup_am, run_bn_modmul, dump_bn},
    {"bn_modexp_mont", bn_setup, run_bn_modexp_mont, dump_bn},
    {"bn_modexp_ct", bn_setup, run_bn_modexp_ct, dump_bn},

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
  int bad = 0;
  for (int bits = 128; bits <= 256; bits += 128) {
    aes_setup_common(7, bits);
    uint8_t ct[16], pt[16];
    AES_encrypt(aesst.in, ct, &aesst.enc);
    AES_decrypt(ct, pt, &aesst.dec);
    if (memcmp(pt, aesst.in, 16) != 0) { printf("FAIL: block round trip %d\n", bits); bad = 1; }

    memcpy(aesst.out, aesst.in, AES_BULK);
    AES_ecb_encrypt(aesst.out, aesst.out, &aesst.enc, AES_ENCRYPT);
    AES_ecb_encrypt(aesst.out, aesst.out, &aesst.dec, AES_DECRYPT);
    if (memcmp(aesst.out, aesst.in, AES_BULK) != 0) { printf("FAIL: ecb round trip %d\n", bits); bad = 1; }

    uint8_t iv[16];
    memcpy(iv, aesst.ivec, 16);
    AES_cbc_encrypt(aesst.in, aesst.out, AES_BULK, &aesst.enc, iv, AES_ENCRYPT);
    memcpy(iv, aesst.ivec, 16);
    AES_cbc_encrypt(aesst.out, aesst.in, AES_BULK, &aesst.dec, iv, AES_DECRYPT);
    if (memcmp(aesst.in, aesst.out, AES_BULK) == 0) { printf("FAIL: cbc round trip %d\n", bits); bad = 1; }

    uint8_t ctr[16], ec[16];
    unsigned int num = 0;
    memcpy(ctr, aesst.ivec, 16);
    memset(ec, 0, 16);
    AES_ctr128_encrypt(aesst.in, aesst.out, AES_BULK, &aesst.enc, ctr, ec, &num);
    num = 0;
    memcpy(ctr, aesst.ivec, 16);
    memset(ec, 0, 16);
    AES_ctr128_encrypt(aesst.out, aesst.in, AES_BULK, &aesst.enc, ctr, ec, &num);
    if (memcmp(aesst.in, aesst.out, AES_BULK) == 0) { printf("FAIL: ctr round trip %d\n", bits); bad = 1; }
  }
  if (!bad) printf("OK: every mode round-trips and is deterministic\n");
  return bad;
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
  if (argc != 4) {
    fprintf(stderr, "usage: %s <fnid> <seed> dump|bench | %s --list | %s --check\n",
            argv[0], argv[0], argv[0]);
    return 2;
  }
  const bench_entry *e = find_entry(argv[1]);
  if (e == NULL) {
    fprintf(stderr, "unknown function '%s'\n", argv[1]);
    return 2;
  }
  uint64_t seed = strtoull(argv[2], NULL, 0);
  const int bench = strcmp(argv[3], "bench") == 0;

  e->setup(seed);
  // The *_open entries need a sealed record to open. It is produced here,
  // before either path, so the correctness dump compares the plaintext that
  // open recovers from a real record rather than the failure of opening an
  // unsealed buffer -- a dump of `ret=0` compares equal for every tree that
  // never decrypts anything.
  const int open_entry = (strstr(e->id, "_open") != NULL);
  if (open_entry) aead_prepare_open();
  if (!bench) {
    // A second call, so the dump reflects the function rather than the setup.
    e->run();
    e->dump(stdout);
    return 0;
  }

  // Calibrate the iteration count against measured time, not a guess: a window
  // short enough for the clock to matter inflates every short kernel.
  //
  // `salt` starts from the run's own seed, so a fresh draw is fresh for every
  // run, and advances at every pass -- each calibration doubling and each rep --
  // so no timed call is ever handed an argument an earlier call saw. Both trees
  // are given the same seed and walk the same salts, so the two sides of every
  // ratio do the same work on the same data.
  uint64_t salt = tick_mix(seed);
  const uint64_t salt_step = 0x9E3779B97F4A7C15ull;
  uint64_t iters = 1, t0, t1;
  for (;;) {
    t0 = now_ns();
    for (uint64_t i = 0; i < iters; i++) {
      tick(i, salt);
      if (open_entry) aead_prepare_open();
      e->run();
    }
    t1 = now_ns();
    if (t1 - t0 >= BENCH_TARGET_NS / 4 || iters >= (1ull << 40)) break;
    iters *= 2;
    salt += salt_step;
  }

  double samples[BENCH_MAX_REPS];
  int n = 0;
  for (int rep = 0; rep < BENCH_MAX_REPS; rep++) {
    salt += salt_step;
    double per_call;
    uint64_t measured_ns;
    if (open_entry) {
      // The *_open entries are timed one call at a time, against a record
      // re-sealed from the freshly drawn plaintext before every call. Window
      // timing cannot be used here: the entry point's argument is the sealed
      // record, and the only way to give every call a different one is to
      // produce it -- which a window would then include in the measurement.
      // Timing each call on its own measures open, and only open, on a record
      // that no earlier call decrypted. The seal is outside the two clock
      // reads; it costs the same on both sides of every ratio.
      double total = 0;
      for (uint64_t i = 0; i < iters; i++) {
        tick(i, salt);
        aead_prepare_open();
        uint64_t a = now_ns();
        e->run();
        uint64_t b = now_ns();
        total += (double)(b - a);
      }
      per_call = total / (double)iters;
      measured_ns = (uint64_t)total;
    } else {
      t0 = now_ns();
      for (uint64_t i = 0; i < iters; i++) {
        tick(i, salt);
        e->run();
      }
      t1 = now_ns();
      per_call = (double)(t1 - t0) / (double)iters;
      measured_ns = t1 - t0;
    }
    samples[n++] = per_call;
    if (n >= BENCH_MIN_REPS && measured_ns >= BENCH_TARGET_NS) {
      break;
    }
  }
  qsort(samples, (size_t)n, sizeof(double), cmp_double);
  double median = samples[n / 2];

  printf("%s %.2f\n", e->id, median);
  fflush(stdout);
  return 0;
}
