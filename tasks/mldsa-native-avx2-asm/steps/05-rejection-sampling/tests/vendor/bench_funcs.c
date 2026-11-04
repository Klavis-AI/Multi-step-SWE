/*
 * Per-function measurement harness for one mldsa-native tree.
 *
 * Verifier-owned. Compiled once against the noop reference tree and once
 * against the candidate's tree, with that tree's own headers and static
 * library, so each binary times the implementation that tree installs.
 *
 * Every entry calls a library entry point from mldsa/src/poly.h,
 * mldsa/src/poly_kl.h, mldsa/src/polyvec.h or mldsa/src/polyvec_lazy.h.  Those
 * are the dispatch points the library itself uses: internally each one calls
 * the backend method installed for that operation when there is one, and falls
 * back to the portable C implementation when there is not.  Timing them
 * therefore times "whatever this tree installs", whether that is assembly, C,
 * or a fallback -- no symbol of any particular backend is named here.
 *
 *   bench_funcs <id> bench <seed>       median cycles per call, NTESTS runs
 *   bench_funcs <id> dump  <seed> <out> deterministic outputs for 8 draws
 *   bench_funcs list                    the entry ids, one per line
 *
 * Inputs are generated here from <seed>, never read from the workspace.  Every
 * timed call is handed a draw of its own: a block's MLD_BENCHMARK_NITERATIONS
 * calls run on MLD_BENCHMARK_NITERATIONS distinct draws, made in small chunks
 * whose writes happen between the timed spans (see MLD_BENCH_CHUNK_BYTES), and
 * the sweep is redrawn in each of the NTESTS blocks.  No draw in a bench run is
 * ever repeated, so the measured number is the cost of *computing* on fresh
 * input and a tree that answers from a cache keyed on what it was called with
 * has nowhere to score.  Every draw is inside the documented contract of the
 * entry it feeds, so two correct implementations must agree bit for bit.
 */

#define _POSIX_C_SOURCE 200112L

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mldsa/src/poly.h"
#include "mldsa/src/poly_kl.h"
#include "mldsa/src/polyvec.h"
#include "mldsa/src/polyvec_lazy.h"
#include "hal.h"

#ifndef MLD_BENCHMARK_NWARMUP
#define MLD_BENCHMARK_NWARMUP 50
#endif
#ifndef MLD_BENCHMARK_NITERATIONS
#define MLD_BENCHMARK_NITERATIONS 300
#endif
#ifndef MLD_BENCHMARK_NTESTS
#define MLD_BENCHMARK_NTESTS 20
#endif

/* Bytes of draw buffer a timed block is split into.  A chunk's draws are
 * written immediately before that chunk is timed, so 16 KB keeps them in this
 * core's L1 when the kernel reads them and the write-back of the previous
 * chunk lands outside the timed region.  (Measured: raising this to 256 KB
 * pushes the big-record entries to L2 and reads 1.6-1.9x where the same trees
 * read 2.0-2.9x here.) */
#ifndef MLD_BENCH_CHUNK_BYTES
#define MLD_BENCH_CHUNK_BYTES 16384
#endif

#define NDUMP 8

/* Draw numbering: the eight draws the differential dump uses are reps 0..7,
 * the warm-up draws sit just above them, and the timed draws start above the
 * warm-up range, so a bench run never re-feeds a draw any other path made. */
#define REP_DUMP_BASE 0u
#define REP_WARM_BASE (NDUMP)
#define REP_BENCH_BASE (NDUMP + MLD_BENCHMARK_NTESTS + 1u)

/* ------------------------------------------------------------------ draws */
/*
 * One draw: the entire input state a single call consumes.  An entry reads
 * only its own member -- a draw is created for one entry and fed to that
 * entry -- and the flat buffer that holds a block's draws is strided by that
 * member's size (see stride_of), so the timed loop walks a compact array the
 * hardware prefetcher can follow.  The entries that mutate their input
 * (the transforms, the pointwise products, decompose, use_hint) mutate their
 * own draw and nothing else, and the dumps stay one call per draw.
 */
typedef union
{
  struct
  {
    mld_poly p0;
  } one;
  struct
  {
    mld_poly p0;
    uint8_t seed[MLD_ALIGN_UP(MLDSA_SEEDBYTES + 2)];
  } uni;
  struct
  {
    mld_poly p0, p1;
  } two;
  struct
  {
    mld_polyvecl v0, v1;
    mld_poly out;
  } acc;
  struct
  {
    mld_polymat mat;
    mld_polyvecl v;
    mld_poly out;
  } matrow;
  struct
  {
    mld_poly p0, p1, p2, p3;
    uint8_t seeds[4][MLD_ALIGN_UP(MLDSA_SEEDBYTES + 2)];
  } u4;
  struct
  {
    mld_poly p0, p1, p2, p3;
    uint8_t eta_seed[MLD_ALIGN_UP(MLDSA_CRHBYTES)];
  } eta4;
  struct
  {
    mld_poly p0;
    uint8_t packed[MLDSA_POLYZ_PACKEDBYTES];
  } unpack;
} draw_t;

static uint8_t outbuf[8 * 1024];
static size_t outlen;
static int32_t norm_B;
static uint32_t chknorm_ret;

/* deterministic input generator: splitmix64, seeded by (seed, repetition) */
static uint64_t rs;
static uint64_t next64(void)
{
  uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}
static uint32_t below(uint32_t m)
{
  return (uint32_t)(next64() % (uint64_t)m);
}

static void seed_rng(uint64_t base, unsigned rep)
{
  rs = base ^ (0xC0FFEEULL * (rep + 1));
  (void)next64();
}

static void gen_poly(mld_poly *p, int32_t lo, int32_t hi)
{
  int i;
  uint32_t span = (uint32_t)(hi - lo);
  for (i = 0; i < MLDSA_N; i++)
  {
    p->coeffs[i] = lo + (int32_t)below(span);
  }
}

static void gen_bytes(uint8_t *b, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++)
  {
    b[i] = (uint8_t)below(256);
  }
}

static void capture(const void *p, size_t n)
{
  if (outlen + n <= sizeof(outbuf))
  {
    memcpy(outbuf + outlen, p, n);
    outlen += n;
  }
}

/* stride_of <id>: bytes between consecutive draws of this entry's block. */
static size_t stride_of(const char *id)
{
  size_t n;
  if (strcmp(id, "poly_ntt") == 0 || strcmp(id, "poly_invntt_tomont") == 0 ||
      strcmp(id, "poly_caddq") == 0 || strcmp(id, "poly_chknorm") == 0)
  {
    n = sizeof(((draw_t *)0)->one);
  }
  else if (strcmp(id, "poly_uniform") == 0)
  {
    n = sizeof(((draw_t *)0)->uni);
  }
  else if (strcmp(id, "poly_pointwise_montgomery") == 0 ||
           strcmp(id, "poly_decompose") == 0 ||
           strcmp(id, "poly_use_hint") == 0)
  {
    n = sizeof(((draw_t *)0)->two);
  }
  else if (strcmp(id, "polyvecl_pointwise_acc_montgomery") == 0)
  {
    n = sizeof(((draw_t *)0)->acc);
  }
  else if (strcmp(id, "polyvec_matrix_pointwise_montgomery_row") == 0)
  {
    n = sizeof(((draw_t *)0)->matrow);
  }
  else if (strcmp(id, "poly_uniform_4x") == 0)
  {
    n = sizeof(((draw_t *)0)->u4);
  }
  else if (strcmp(id, "poly_uniform_eta_4x") == 0)
  {
    n = sizeof(((draw_t *)0)->eta4);
  }
  else if (strcmp(id, "polyz_unpack") == 0)
  {
    n = sizeof(((draw_t *)0)->unpack);
  }
  else
  {
    fprintf(stderr, "unknown id %s\n", id);
    exit(2);
  }
  /* mld_poly is 32-byte aligned; round up so every record keeps that. */
  return (n + 63u) & ~(size_t)63u;
}

static draw_t *draw_at(draw_t *base, size_t stride, unsigned j)
{
  return (draw_t *)(void *)((unsigned char *)base + (size_t)j * stride);
}

/* ---------------------------------------------------------------- entries */

/* One setup per entry: fills the draw the entry consumes.  `which` selects
 * the entry.  Kept in one switch so bench and dump always see the same input
 * for a given (id, rep). */
static void setup_draw(draw_t *d, const char *id, unsigned rep, uint64_t seed)
{
  seed_rng(seed, rep);
  outlen = 0;
  if (strcmp(id, "poly_ntt") == 0 || strcmp(id, "poly_invntt_tomont") == 0 ||
      strcmp(id, "poly_caddq") == 0)
  {
    /* standard representatives, and signed values for caddq */
    if (strcmp(id, "poly_caddq") == 0)
    {
      gen_poly(&d->one.p0, 1 - MLDSA_Q, MLDSA_Q);
    }
    else
    {
      gen_poly(&d->one.p0, 0, MLDSA_Q);
    }
  }
  else if (strcmp(id, "poly_chknorm") == 0)
  {
    /* the case signing actually hits: every coefficient inside the bound, so
     * the reference scan visits all 256 entries */
    norm_B = MLDSA_GAMMA1 - MLDSA_BETA;
    gen_poly(&d->one.p0, 1 - norm_B, norm_B);
  }
  else if (strcmp(id, "poly_pointwise_montgomery") == 0)
  {
    /* the contract bounds both operands by MLD_NTT_BOUND */
    gen_poly(&d->two.p0, -MLD_NTT_BOUND + 1, MLD_NTT_BOUND);
    gen_poly(&d->two.p1, -MLD_NTT_BOUND + 1, MLD_NTT_BOUND);
  }
  else if (strcmp(id, "polyvecl_pointwise_acc_montgomery") == 0)
  {
    int i;
    for (i = 0; i < MLDSA_L; i++)
    {
      gen_poly(&d->acc.v0.vec[i], 0, MLDSA_Q);
      gen_poly(&d->acc.v1.vec[i], 0, MLDSA_Q);
    }
  }
  else if (strcmp(id, "polyvec_matrix_pointwise_montgomery_row") == 0)
  {
    int i, j;
    for (i = 0; i < MLDSA_K; i++)
    {
      for (j = 0; j < MLDSA_L; j++)
      {
        gen_poly(&d->matrow.mat.vec[i].vec[j], 0, MLDSA_Q);
      }
    }
    for (j = 0; j < MLDSA_L; j++)
    {
      gen_poly(&d->matrow.v.vec[j], 0, MLDSA_Q);
    }
  }
  else if (strcmp(id, "poly_decompose") == 0)
  {
    gen_poly(&d->two.p0, 0, MLDSA_Q);
  }
  else if (strcmp(id, "poly_use_hint") == 0)
  {
    gen_poly(&d->two.p0, 0, MLDSA_Q);
    gen_poly(&d->two.p1, 0, 2); /* a hint is a 0/1 polynomial */
  }
  else if (strcmp(id, "poly_uniform") == 0)
  {
    gen_bytes(d->uni.seed, MLDSA_SEEDBYTES + 2);
  }
  else if (strcmp(id, "poly_uniform_4x") == 0)
  {
    int i;
    for (i = 0; i < 4; i++)
    {
      gen_bytes(d->u4.seeds[i], MLDSA_SEEDBYTES + 2);
    }
  }
  else if (strcmp(id, "poly_uniform_eta_4x") == 0)
  {
    gen_bytes(d->eta4.eta_seed, MLDSA_CRHBYTES);
  }
  else if (strcmp(id, "polyz_unpack") == 0)
  {
    gen_bytes(d->unpack.packed, MLDSA_POLYZ_PACKEDBYTES);
  }
  else
  {
    fprintf(stderr, "unknown id %s\n", id);
    exit(2);
  }
}

static void run_once(const char *id, draw_t *d)
{
  if (strcmp(id, "poly_ntt") == 0)
  {
    mld_poly_ntt(&d->one.p0);
  }
  else if (strcmp(id, "poly_invntt_tomont") == 0)
  {
    mld_poly_invntt_tomont(&d->one.p0);
  }
  else if (strcmp(id, "poly_pointwise_montgomery") == 0)
  {
    mld_poly_pointwise_montgomery(&d->two.p0, &d->two.p1);
  }
  else if (strcmp(id, "polyvecl_pointwise_acc_montgomery") == 0)
  {
    mld_polyvecl_pointwise_acc_montgomery(&d->acc.out, &d->acc.v0, &d->acc.v1);
  }
  else if (strcmp(id, "polyvec_matrix_pointwise_montgomery_row") == 0)
  {
    mld_polyvec_matrix_pointwise_montgomery_row(&d->matrow.out, &d->matrow.mat,
                                                &d->matrow.v, 0);
  }
  else if (strcmp(id, "poly_caddq") == 0)
  {
    mld_poly_caddq(&d->one.p0);
  }
  else if (strcmp(id, "poly_chknorm") == 0)
  {
    chknorm_ret = (uint32_t)mld_poly_chknorm(&d->one.p0, norm_B);
  }
  else if (strcmp(id, "poly_decompose") == 0)
  {
    mld_poly_decompose(&d->two.p1, &d->two.p0);
  }
  else if (strcmp(id, "poly_use_hint") == 0)
  {
    mld_poly_use_hint(&d->two.p0, &d->two.p1);
  }
  else if (strcmp(id, "poly_uniform") == 0)
  {
    mld_poly_uniform(&d->uni.p0, d->uni.seed);
  }
  else if (strcmp(id, "poly_uniform_4x") == 0)
  {
    mld_poly_uniform_4x(&d->u4.p0, &d->u4.p1, &d->u4.p2, &d->u4.p3,
                        d->u4.seeds);
  }
  else if (strcmp(id, "poly_uniform_eta_4x") == 0)
  {
    mld_poly_uniform_eta_4x(&d->eta4.p0, &d->eta4.p1, &d->eta4.p2, &d->eta4.p3,
                            d->eta4.eta_seed, 0, 1, 2, 3);
  }
  else if (strcmp(id, "polyz_unpack") == 0)
  {
    mld_polyz_unpack(&d->unpack.p0, d->unpack.packed);
  }
  else
  {
    fprintf(stderr, "unknown id %s\n", id);
    exit(2);
  }
}

static int cmp_i32(const void *a, const void *b)
{
  int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

/* what the entry produced, for the differential comparison.
 *
 * Two entries need care.  The forward transform's coefficient *order* is the
 * backend's business: the AVX2 backend emits its own coefficient order and the
 * tree permutes where that matters, so two correct trees need not agree
 * position by position.  A forward transform is compared as the multiset of
 * its coefficients -- the values it computed -- and the ordering itself is
 * checked end-to-end by the project's functional and KAT suites.  The inverse
 * transform's input order is the same freedom read backwards, so it is
 * compared through the round trip, which is order-independent.
 */
static void emit(const char *id, const draw_t *d)
{
  if (strcmp(id, "poly_ntt") == 0)
  {
    mld_poly s = d->one.p0;
    qsort(s.coeffs, MLDSA_N, sizeof(int32_t), cmp_i32);
    capture(&s, sizeof(s));
  }
  else if (strcmp(id, "poly_invntt_tomont") == 0)
  {
    capture(&d->one.p0, sizeof(mld_poly));
  }
  else if (strcmp(id, "poly_caddq") == 0)
  {
    capture(&d->one.p0, sizeof(mld_poly));
  }
  else if (strcmp(id, "poly_pointwise_montgomery") == 0)
  {
    capture(&d->two.p0, sizeof(mld_poly));
  }
  else if (strcmp(id, "polyvecl_pointwise_acc_montgomery") == 0)
  {
    capture(&d->acc.out, sizeof(mld_poly));
  }
  else if (strcmp(id, "polyvec_matrix_pointwise_montgomery_row") == 0)
  {
    capture(&d->matrow.out, sizeof(mld_poly));
  }
  else if (strcmp(id, "poly_chknorm") == 0)
  {
    capture(&chknorm_ret, sizeof(chknorm_ret));
  }
  else if (strcmp(id, "poly_decompose") == 0)
  {
    capture(&d->two.p0, sizeof(mld_poly));
    capture(&d->two.p1, sizeof(mld_poly));
  }
  else if (strcmp(id, "poly_use_hint") == 0)
  {
    capture(&d->two.p0, sizeof(mld_poly));
  }
  else if (strcmp(id, "poly_uniform") == 0)
  {
    capture(&d->uni.p0, sizeof(mld_poly));
  }
  else if (strcmp(id, "poly_uniform_4x") == 0)
  {
    capture(&d->u4.p0, sizeof(mld_poly));
    capture(&d->u4.p1, sizeof(mld_poly));
    capture(&d->u4.p2, sizeof(mld_poly));
    capture(&d->u4.p3, sizeof(mld_poly));
  }
  else if (strcmp(id, "poly_uniform_eta_4x") == 0)
  {
    capture(&d->eta4.p0, sizeof(mld_poly));
    capture(&d->eta4.p1, sizeof(mld_poly));
    capture(&d->eta4.p2, sizeof(mld_poly));
    capture(&d->eta4.p3, sizeof(mld_poly));
  }
  else if (strcmp(id, "polyz_unpack") == 0)
  {
    capture(&d->unpack.p0, sizeof(mld_poly));
  }
}

static const char *IDS[] = {
    "poly_ntt",
    "poly_invntt_tomont",
    "poly_pointwise_montgomery",
    "polyvecl_pointwise_acc_montgomery",
    "polyvec_matrix_pointwise_montgomery_row",
    "poly_caddq",
    "poly_chknorm",
    "poly_decompose",
    "poly_use_hint",
    "poly_uniform",
    "poly_uniform_4x",
    "poly_uniform_eta_4x",
    "polyz_unpack",
};

/* -------------------------------------------------------------- the modes */

static int cmp_u64(const void *a, const void *b)
{
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

static int do_bench(const char *id, uint64_t seed)
{
  static uint64_t cyc[MLD_BENCHMARK_NTESTS];
  static draw_t warm;
  unsigned i, j;
  size_t stride = stride_of(id);
  /* The block's draws are made in chunks that fit in cache: each chunk is
   * written, then immediately timed, so the reads see freshly written data
   * and the write-back of the previous chunk lands outside the timed region.
   * Every timed call still gets a draw of its own. */
  const size_t chunk_bytes = MLD_BENCH_CHUNK_BYTES;
  unsigned chunk = (unsigned)(chunk_bytes / stride);
  size_t bytes;
  draw_t *draws = NULL;

  if (chunk < 1)
  {
    chunk = 1;
  }
  if (chunk > MLD_BENCHMARK_NITERATIONS)
  {
    chunk = MLD_BENCHMARK_NITERATIONS;
  }
  bytes = stride * (size_t)chunk;

  if (posix_memalign((void **)&draws, 64, bytes) != 0 || draws == NULL)
  {
    fprintf(stderr, "cannot allocate %zu bytes for one chunk of draws\n", bytes);
    return 2;
  }

  enable_cyclecounter();
  for (i = 0; i < MLD_BENCHMARK_NTESTS; i++)
  {
    uint64_t total = 0;
    unsigned done = 0;
    /* warm up once per block, on a draw of its own, so the timed draws are
     * never re-fed */
    setup_draw(&warm, id, REP_WARM_BASE + i, seed);
    for (j = 0; j < MLD_BENCHMARK_NWARMUP; j++)
    {
      run_once(id, &warm);
    }
    while (done < MLD_BENCHMARK_NITERATIONS)
    {
      unsigned n = chunk;
      uint64_t t0, t1;
      if (done + n > MLD_BENCHMARK_NITERATIONS)
      {
        n = MLD_BENCHMARK_NITERATIONS - done;
      }
      for (j = 0; j < n; j++)
      {
        setup_draw(draw_at(draws, stride, j), id,
                   REP_BENCH_BASE + i * MLD_BENCHMARK_NITERATIONS + done + j,
                   seed);
      }
      t0 = get_cyclecounter();
      for (j = 0; j < n; j++)
      {
        run_once(id, draw_at(draws, stride, j));
      }
      t1 = get_cyclecounter();
      total += t1 - t0;
      done += n;
    }
    cyc[i] = total;
  }
  disable_cyclecounter();
  free(draws);
  qsort(cyc, MLD_BENCHMARK_NTESTS, sizeof(uint64_t), cmp_u64);
  printf("%s cycles=%" PRIu64 "\n", id,
         cyc[MLD_BENCHMARK_NTESTS >> 1] / MLD_BENCHMARK_NITERATIONS);
  return 0;
}

static int do_dump(const char *id, uint64_t seed, const char *path)
{
  unsigned i;
  FILE *f;
  static draw_t d; /* one draw, not a block: keep it off the stack */
  for (i = 0; i < NDUMP; i++)
  {
    setup_draw(&d, id, REP_DUMP_BASE + i, seed);
    if (strcmp(id, "poly_invntt_tomont") == 0)
    {
      /* compare the round trip: order-independent in both directions */
      mld_poly_ntt(&d.one.p0);
    }
    run_once(id, &d);
    emit(id, &d);
  }
  f = fopen(path, "wb");
  if (f == NULL)
  {
    perror("dump");
    return 1;
  }
  if (fwrite(outbuf, 1, outlen, f) != outlen)
  {
    perror("write");
    fclose(f);
    return 1;
  }
  fclose(f);
  return 0;
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s <id> bench|dump <seed> [outfile]\n", argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "list") == 0)
  {
    size_t i;
    for (i = 0; i < sizeof(IDS) / sizeof(IDS[0]); i++)
    {
      printf("%s\n", IDS[i]);
    }
    return 0;
  }
  if (argc < 4)
  {
    fprintf(stderr, "usage: %s <id> bench|dump <seed> [outfile]\n", argv[0]);
    return 2;
  }
  if (strcmp(argv[2], "bench") == 0)
  {
    return do_bench(argv[1], strtoull(argv[3], NULL, 0));
  }
  if (strcmp(argv[2], "dump") == 0 && argc >= 5)
  {
    return do_dump(argv[1], strtoull(argv[3], NULL, 0), argv[4]);
  }
  fprintf(stderr, "unknown mode %s\n", argv[2]);
  return 2;
}
