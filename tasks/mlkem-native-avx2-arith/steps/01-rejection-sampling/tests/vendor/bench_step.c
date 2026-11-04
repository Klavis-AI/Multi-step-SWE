/*
 * bench_step -- per-function harness for one mlkem-native arithmetic ladder
 * step.  Verifier-owned: it is compiled by the verifier against the tree under
 * test and calls that tree's PUBLIC entry point for a function, so a candidate
 * that optimizes the portable C path is measured exactly like one that writes
 * an AVX2 kernel.
 *
 * usage: bench_step <fnid> <seed> dump
 *        bench_step <fnid> <seed> bench
 *
 *   dump   write NREPS consecutive check-images as raw bytes to stdout; the
 *          verifier compares them byte for byte between the step's base build
 *          and the candidate.
 *   bench  print "cycles=<median>" of the timed call.  Every timed call gets
 *          its own input, drawn from <seed>, so nothing can be memoized behind
 *          the entry point.
 *
 * The input is generated here, from <seed>; nothing is read from the tree
 * under test.  Timed regions contain the library call and nothing else --
 * input construction is hoisted out of the measurement loop, and the input
 * pool below is what makes "every call gets its own input" possible without
 * putting construction inside the timed region.
 *
 * Check images and the layout question.  The upstream AVX2 backend evaluates
 * the NTT-domain arithmetic in a permuted coefficient order, and turns that
 * order on only once every primitive that reads or writes NTT coefficients is
 * native.  A raw dump of, say, mlk_poly_ntt is therefore not comparable across
 * two trees that differ in that switch.  For those entry points the check
 * image is instead a composition that returns to the canonical coefficient
 * domain -- invntt_tomont(ntt(x)), frombytes(tobytes(x)), and the multiply
 * round trip -- which is the same value in either layout, so the comparison
 * stays bit-exact.  The timed region is always the bare call.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/compress.h"
#include "src/indcpa.h"
#include "src/poly.h"
#include "src/poly_k.h"
#include "src/sampling.h"
#include "src/kem.h"
#include "src/sys.h"

#if !defined(__x86_64__)
#error "bench_step targets x86_64"
#endif

#define NTESTS 21
#define NREPS 8
/* One timed window is POOL_MAX calls, one prepared input each.  The window is
 * a fixed number of calls -- not "whatever fits in 400k cycles" -- so the two
 * sides of a comparison walk pools of the same size and shape and the ratio
 * carries no difference in working-set size.
 *
 * The width is a compromise, and it is measured rather than guessed: a call
 * fed a fresh input pays for reading it (re-reading one L1-resident buffer
 * would get the input for free), so the wider the pool the more that cost
 * dilutes a fast kernel's ratio -- at 2048 slots a 512-byte
 * input is a megabyte and the fast entry points lose more than half their
 * measured speedup to it.  256 slots keeps the arithmetic pools between 8 KB
 * and 768 KB and the window above 10k cycles even for the fastest entry
 * point.  Both sides use the same width, so the cost is common-mode.
 * (Overridable with -DPOOL_MAX=... so the effect can be re-measured.) */
#ifndef POOL_MAX
#define POOL_MAX 256u
#endif
#ifndef KEM_POOL
#define KEM_POOL 64u
#endif

/* 16 * 4096 covers every polynomial-shaped image; the KEM vector image is
 * smaller than that. */
#define OUTMAX (16 * 4096)

/*
 * The KEM entry points are declared with a context-parameter macro that only
 * expands inside the project's own amalgamated header; declare them here as
 * plain functions under the tree's own namespace prefix.
 */
#undef mlk_kem_keypair_derand
#undef mlk_kem_enc_derand
#undef mlk_kem_dec
int MLK_NAMESPACE_K(keypair_derand)(uint8_t *pk, uint8_t *sk,
                                    const uint8_t *coins);
int MLK_NAMESPACE_K(enc_derand)(uint8_t *ct, uint8_t *ss, const uint8_t *pk,
                                const uint8_t *coins);
int MLK_NAMESPACE_K(dec)(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* ------------------------------------------------------------------ clock */

static uint64_t rdtsc(void)
{
  uint32_t lo, hi;
  __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) : : "%rcx");
  return ((uint64_t)hi << 32) | lo;
}

/* ------------------------------------------------------------------ input */

static uint64_t rs_state;

static void rs_seed(uint64_t s)
{
  rs_state = s ? s : 0x9e3779b97f4a7c15ULL;
  rs_state ^= rs_state << 13;
  rs_state ^= rs_state >> 7;
  rs_state ^= rs_state << 17;
}

static uint64_t rs_next(void)
{
  rs_state ^= rs_state << 13;
  rs_state ^= rs_state >> 7;
  rs_state ^= rs_state << 17;
  return rs_state;
}

static uint8_t rs_byte(void) { return (uint8_t)(rs_next() >> 24); }

static int16_t rs_below(unsigned m) { return (int16_t)((rs_next() >> 20) % m); }

static void rs_fill(uint8_t *p, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++)
  {
    p[i] = rs_byte();
  }
}

static uint8_t kem_pk[KEM_POOL][4096];
static uint8_t kem_sk[KEM_POOL][8192];
static uint8_t kem_ct[KEM_POOL][4096];
static uint8_t kem_ss[MLKEM_SSBYTES];
static uint8_t kem_ss2[MLKEM_SSBYTES];
static uint8_t kem_coins[KEM_POOL][2 * MLKEM_SYMBYTES];

/* The randomized KEM entry points live in the same translation unit as the
 * derandomized ones and reference randombytes; supply a deterministic one so
 * the harness does not depend on the tree's own test support. */
int randombytes(uint8_t *out, size_t outlen)
{
  size_t i;
  for (i = 0; i < outlen; i++)
  {
    out[i] = rs_byte();
  }
  return 0;
}

/* ------------------------------------------------------------ input state */

/*
 * The pool.  Slot k holds the whole input state of one call, and the k-th
 * timed call of a window reads slot k.  A window is POOL_MAX calls and every
 * call in it therefore sees an input nothing has seen before -- inputs are
 * redrawn from <seed> for every window, so no input recurs anywhere in the
 * run either.  That is what makes a cache behind the entry point worthless:
 * a handful of rotating inputs would not, because each of them would then be
 * served from the cache for the rest of the window.
 *
 * Preparing the slots is deliberately outside the timed region; the timed
 * region still contains the library call and nothing else.
 *
 * The arrays are per-op-shaped rather than one struct per slot so that the
 * walk is contiguous for the op in use: a fast entry point would otherwise
 * pay for cache lines its input never touches.
 */
static mlk_poly pool_px[POOL_MAX];        /* in-place poly ops, to/frombytes,
                                           * compress, mulcache, rej_uniform */
static mlk_polyvec pool_vx[POOL_MAX], pool_vy[POOL_MAX];   /* basemul */
static uint8_t pool_seed[POOL_MAX][MLKEM_SYMBYTES + 2];
static uint8_t pool_bytes[POOL_MAX][512];

/* Outputs and scratch: not inputs, so one copy serves every call.  Only the
 * in-place entry points (ntt, invntt, reduce, tomont) write into the pool,
 * where the call has read the line anyway; everything else writes here, so
 * the measurement does not charge every call for touching a fresh output
 * region. */
static mlk_poly out_px;
static mlk_polyvec_mulcache v_cache;
static mlk_poly_mulcache p_cache;
static uint8_t out_bytes[MLKEM_POLYBYTES];

/* Coefficient domains each entry point is defined on.  Handing an
 * implementation an input outside its documented contract would compare
 * undefined behaviour rather than implementations. */
static void gen_canonical(mlk_poly *p)
{
  unsigned i;
  for (i = 0; i < MLKEM_N; i++)
  {
    p->coeffs[i] = rs_below(MLKEM_Q);
  }
}

static void gen_signed(mlk_poly *p, unsigned bound)
{
  unsigned i;
  for (i = 0; i < MLKEM_N; i++)
  {
    p->coeffs[i] = (int16_t)(rs_below(2 * bound + 1) - (int)bound);
  }
}

/* ------------------------------------------------------------- operations */

enum opid
{
  OP_REJ_UNIFORM,
  OP_NTT,
  OP_INTT_TOMONT,
  OP_REDUCE,
  OP_TOMONT,
  OP_MULCACHE,
  OP_BASEMUL,
  OP_TOBYTES,
  OP_FROMBYTES,
  OP_COMPRESS_DU,
  OP_COMPRESS_DV,
  OP_DECOMPRESS_DU,
  OP_DECOMPRESS_DV,
  OP_KEM_KEYPAIR,
  OP_KEM_ENCAPS,
  OP_KEM_DECAPS,
  OP_KEM_VECTORS,
  OP_COUNT
};

static const char *op_names[OP_COUNT] = {
    "rej_uniform",    "ntt",           "invntt_tomont",  "reduce",
    "tomont",         "mulcache_compute", "basemul_acc", "tobytes",
    "frombytes",      "compress_du",   "compress_dv",    "decompress_du",
    "decompress_dv",   "kem_keypair",    "kem_encaps",     "kem_decaps",
    "kem_vectors"};

static int op_parse(const char *s)
{
  int i;
  for (i = 0; i < OP_COUNT; i++)
  {
    if (!strcmp(s, op_names[i]))
    {
      return i;
    }
  }
  fprintf(stderr, "bench_step: unknown fnid '%s'\n", s);
  exit(2);
}

static void op_prepare(int op, unsigned s)
{
  unsigned i;
  switch (op)
  {
    case OP_REJ_UNIFORM:
      rs_fill(pool_seed[s], sizeof(pool_seed[s]));
      memset(&pool_px[s], 0, sizeof(pool_px[s]));
      break;
    case OP_NTT:
    case OP_INTT_TOMONT:
    case OP_TOMONT:
      gen_signed(&pool_px[s], MLKEM_Q - 1);
      break;
    case OP_REDUCE:
      for (i = 0; i < MLKEM_N; i++)
      {
        pool_px[s].coeffs[i] = (int16_t)(rs_next() & 0xFFFF);
      }
      break;
    case OP_MULCACHE:
      gen_signed(&pool_px[s], MLKEM_Q - 1);
      break;
    case OP_BASEMUL:
      for (i = 0; i < MLKEM_K; i++)
      {
        gen_signed(&pool_vx[s].vec[i], 8 * MLKEM_Q);
        gen_signed(&pool_vy[s].vec[i], 8 * MLKEM_Q);
      }
      break;
    case OP_TOBYTES:
      gen_canonical(&pool_px[s]);
      break;
    case OP_FROMBYTES:
      /* A canonical 12-bit encoding: every value below q. */
      memset(pool_bytes[s], 0, 3 * (MLKEM_N / 2));
      for (i = 0; i < MLKEM_N; i += 2)
      {
        uint16_t t0 = (uint16_t)rs_below(MLKEM_Q);
        uint16_t t1 = (uint16_t)rs_below(MLKEM_Q);
        pool_bytes[s][3 * (i / 2) + 0] = (uint8_t)(t0 & 0xFF);
        pool_bytes[s][3 * (i / 2) + 1] =
            (uint8_t)((t0 >> 8) | ((t1 & 0x0F) << 4));
        pool_bytes[s][3 * (i / 2) + 2] = (uint8_t)(t1 >> 4);
      }
      break;
    case OP_COMPRESS_DU:
    case OP_COMPRESS_DV:
      gen_canonical(&pool_px[s]);
      break;
    case OP_DECOMPRESS_DU:
    case OP_DECOMPRESS_DV:
      /* FIPS 203 defines Decompress_d on every d-bit string, so arbitrary
       * bytes drawn from the verifier's own stream are a legal input. */
      rs_fill(pool_bytes[s], sizeof(pool_bytes[s]));
      break;
    case OP_KEM_KEYPAIR:
    case OP_KEM_ENCAPS:
    case OP_KEM_DECAPS:
    case OP_KEM_VECTORS:
      rs_fill(kem_coins[s], sizeof(kem_coins[s]));
      MLK_NAMESPACE_K(keypair_derand)(kem_pk[s], kem_sk[s], kem_coins[s]);
      break;
    default:
      break;
  }
}

/* The timed call: the library entry point, on the input in slot s. */
static void op_call(int op, unsigned s)
{
  switch (op)
  {
    case OP_REJ_UNIFORM:
      mlk_poly_rej_uniform(&out_px, pool_seed[s]);
      break;
    case OP_NTT:
      mlk_poly_ntt(&pool_px[s]);
      break;
    case OP_INTT_TOMONT:
      mlk_poly_invntt_tomont(&pool_px[s]);
      break;
    case OP_REDUCE:
      mlk_poly_reduce(&pool_px[s]);
      break;
    case OP_TOMONT:
      mlk_poly_tomont(&pool_px[s]);
      break;
    case OP_MULCACHE:
      mlk_poly_mulcache_compute(&p_cache, &pool_px[s]);
      break;
    case OP_BASEMUL:
      mlk_polyvec_mulcache_compute(&v_cache, &pool_vy[s]);
      mlk_polyvec_basemul_acc_montgomery_cached(&out_px, &pool_vx[s],
                                                &pool_vy[s], &v_cache);
      break;
    case OP_TOBYTES:
      mlk_poly_tobytes(out_bytes, &pool_px[s]);
      break;
    case OP_FROMBYTES:
      mlk_poly_frombytes(&out_px, pool_bytes[s]);
      break;
    case OP_COMPRESS_DU:
      mlk_poly_compress_du(out_bytes, &pool_px[s]);
      break;
    case OP_COMPRESS_DV:
      mlk_poly_compress_dv(out_bytes, &pool_px[s]);
      break;
    case OP_DECOMPRESS_DU:
      mlk_poly_decompress_du(&out_px, pool_bytes[s]);
      break;
    case OP_DECOMPRESS_DV:
      mlk_poly_decompress_dv(&out_px, pool_bytes[s]);
      break;
    case OP_KEM_KEYPAIR:
      MLK_NAMESPACE_K(keypair_derand)(kem_pk[s], kem_sk[s], kem_coins[s]);
      break;
    case OP_KEM_ENCAPS:
      MLK_NAMESPACE_K(enc_derand)(kem_ct[s], kem_ss, kem_pk[s], kem_coins[s]);
      break;
    default:
      MLK_NAMESPACE_K(dec)(kem_ss2, kem_ct[s], kem_sk[s]);
      break;
  }
}

/* How many distinct inputs an op can use in one window. */
static unsigned op_pool(int op)
{
  return (op >= OP_KEM_KEYPAIR) ? KEM_POOL : POOL_MAX;
}

/*
 * The check image.  Where the tree's NTT-domain layout may differ from the
 * reference's, this returns to the canonical coefficient domain first, so the
 * bytes mean the same thing in either layout.
 */
static size_t op_image(int op, uint8_t *out)
{
  switch (op)
  {
    case OP_REJ_UNIFORM:
      op_call(op, 0);
      memcpy(out, &out_px, sizeof(out_px));
      return sizeof(out_px);
    case OP_NTT:
    case OP_INTT_TOMONT:
      /* invntt_tomont(reduce(ntt(x))) is a fixed multiple of x in the
       * canonical coefficient domain, whichever layout the tree evaluates
       * the NTT in.  The reduce is what keeps every step inside the
       * documented coefficient bounds. */
      mlk_poly_ntt(&pool_px[0]);
      mlk_poly_reduce(&pool_px[0]);
      mlk_poly_invntt_tomont(&pool_px[0]);
      memcpy(out, &pool_px[0], sizeof(pool_px[0]));
      return sizeof(pool_px[0]);
    case OP_REDUCE:
    case OP_TOMONT:
      op_call(op, 0);
      memcpy(out, &pool_px[0], sizeof(pool_px[0]));
      return sizeof(pool_px[0]);
    case OP_DECOMPRESS_DU:
    case OP_DECOMPRESS_DV:
      op_call(op, 0);
      memcpy(out, &out_px, sizeof(out_px));
      return sizeof(out_px);
    case OP_MULCACHE:
    case OP_BASEMUL:
    {
      unsigned i;
      mlk_poly out_poly;
      /* a * b in the NTT domain, transformed back: canonical in any layout */
      for (i = 0; i < MLKEM_K; i++)
      {
        mlk_poly_ntt(&pool_vx[0].vec[i]);
        mlk_poly_reduce(&pool_vx[0].vec[i]);
        mlk_poly_ntt(&pool_vy[0].vec[i]);
        mlk_poly_reduce(&pool_vy[0].vec[i]);
      }
      mlk_polyvec_mulcache_compute(&v_cache, &pool_vy[0]);
      memset(&out_poly, 0, sizeof(out_poly));
      mlk_polyvec_basemul_acc_montgomery_cached(&out_poly, &pool_vx[0],
                                                &pool_vy[0], &v_cache);
      mlk_poly_reduce(&out_poly);
      mlk_poly_invntt_tomont(&out_poly);
      memcpy(out, &out_poly, sizeof(out_poly));
      return sizeof(out_poly);
    }
    case OP_KEM_VECTORS:
    {
      size_t off = 0;
      unsigned c;
      rs_fill(kem_coins[0], sizeof(kem_coins[0]));
      MLK_NAMESPACE_K(keypair_derand)(kem_pk[0], kem_sk[0], kem_coins[0]);
      memcpy(out + off, kem_pk[0], sizeof(kem_pk[0]));
      off += sizeof(kem_pk[0]);
      memcpy(out + off, kem_sk[0], sizeof(kem_sk[0]));
      off += sizeof(kem_sk[0]);
      /* four independent encapsulations, each decapsulated */
      for (c = 0; c < 4; c++)
      {
        rs_fill(kem_coins[0], 32);
        MLK_NAMESPACE_K(enc_derand)(kem_ct[0], kem_ss, kem_pk[0],
                                    kem_coins[0]);
        MLK_NAMESPACE_K(dec)(kem_ss2, kem_ct[0], kem_sk[0]);
        if (memcmp(kem_ss, kem_ss2, MLKEM_SSBYTES) != 0)
        {
          fprintf(stderr, "bench_step: implicit rejection mismatch\n");
          return 0;
        }
        memcpy(out + off, kem_ct[0], sizeof(kem_ct[0]));
        off += sizeof(kem_ct[0]);
        memcpy(out + off, kem_ss, sizeof(kem_ss));
        off += sizeof(kem_ss);
      }
      return off;
    }
    case OP_TOBYTES:
    case OP_FROMBYTES:
      /* frombytes(tobytes(x)) is x reduced into [0,q), in any layout */
      mlk_poly_tobytes(out_bytes, &pool_px[0]);
      mlk_poly_frombytes(&pool_px[0], out_bytes);
      memcpy(out, &pool_px[0], sizeof(pool_px[0]));
      return sizeof(pool_px[0]);
    default:
      op_call(op, 0);
      return MLKEM_POLYCOMPRESSEDBYTES_DU;
  }
}

/* --------------------------------------------------------------- drivers */

static uint8_t obuf[OUTMAX];

static int do_dump(int op, uint64_t seed)
{
  int rep;
  for (rep = 0; rep < NREPS; rep++)
  {
    size_t n;
    rs_seed(seed * 1000003ULL + (uint64_t)rep + 1);
    op_prepare(op, 0);
    n = op_image(op, obuf);
    if (n == 0 || n > OUTMAX)
    {
      fprintf(stderr, "bench_step: bad image length %zu\n", n);
      return 1;
    }
    if (fwrite(obuf, 1, n, stdout) != n)
    {
      fprintf(stderr, "bench_step: write failed\n");
      return 1;
    }
  }
  return 0;
}

static int cmp_u64(const void *x, const void *y)
{
  uint64_t a = *(const uint64_t *)x;
  uint64_t b = *(const uint64_t *)y;
  return (a > b) - (a < b);
}

static int do_bench(int op, uint64_t seed)
{
  uint64_t cyc[NTESTS];
  unsigned nslot = op_pool(op);   /* one prepared input per timed call */
  unsigned i;
  int t;

  /* Warm up on slot 0. */
  rs_seed(seed * 1000003ULL + 1);
  op_prepare(op, 0);
  for (i = 0; i < 64; i++)
  {
    op_call(op, 0);
  }

  for (t = 0; t < NTESTS; t++)
  {
    uint64_t t0, t1, k;
    /* Every window draws its own pool, so no input recurs anywhere in the
     * run -- not within a window, not between windows. */
    rs_seed(seed * 1000003ULL + 17 * (uint64_t)t + 5);
    for (i = 0; i < nslot; i++)
    {
      op_prepare(op, i);
    }
    t0 = rdtsc();
    for (k = 0; k < nslot; k++)
    {
      op_call(op, (unsigned)k);
    }
    t1 = rdtsc();
    cyc[t] = (t1 - t0) / nslot;
  }
  qsort(cyc, NTESTS, sizeof(uint64_t), cmp_u64);
  printf("cycles=%" PRIu64 "\n", cyc[NTESTS >> 1]);
  return 0;
}

int main(int argc, char **argv)
{
  int op;
  uint64_t seed;

  if (argc != 4)
  {
    fprintf(stderr, "usage: %s <fnid> <seed> dump|bench\n", argv[0]);
    return 2;
  }
  op = op_parse(argv[1]);
  seed = strtoull(argv[2], NULL, 10);

  if (!strcmp(argv[3], "dump"))
  {
    return do_dump(op, seed);
  }
  if (!strcmp(argv[3], "bench"))
  {
    return do_bench(op, seed);
  }
  fprintf(stderr, "bench_step: unknown mode '%s'\n", argv[3]);
  return 2;
}
