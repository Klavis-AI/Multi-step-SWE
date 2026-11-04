//***************************************************************************/
// bench_stage.cpp -- per-function benchmark harness for the OpenJPH
// optimization ladder.  One source, compiled separately against each ladder
// tree (L0 .. L6), so the same measurement code runs on every level and the
// only thing that differs is which kernel the dispatch pointer was installed
// with.
//
//   bench_stage list
//   bench_stage <fnid> <inputarg> dump  <outfile>
//   bench_stage <fnid> <inputarg> bench
//   bench_stage sym <fnid> <inputarg>          (extra: dladdr of the pointers)
//
// <inputarg> is a fixture path for decode_cb32 and a decimal seed for every
// other fnid; the inputs are regenerated deterministically from the seed so
// that every build feeds the identical bytes to the identical call.
//
// Every fnid calls the INSTALLED dispatch pointer (ojph::local::*), never a
// named symbol, so L0 (all SIMD stages stripped) and L6 (all six stages) are
// exercising genuinely different code through the same call site.
//
//   dump      : run once on a freshly restored input, write the function
//               result.  The input is the pristine one -- this is what defines
//               correctness.
//   bench     : restore the input, run once, repeat R times, print the median
//               of the per-call wall times in nanoseconds as a single integer.
//               Every repetition is the identical input; this is the scored
//               measurement, so reference and candidate are compared on the
//               same content.
//   benchvary : the same measurement with the input re-drawn between
//               repetitions (untimed; same shape, same distribution, a
//               different draw for each repetition).  Not scored: the verifier
//               uses it to check that a claimed speedup survives when the
//               repetitions stop being identical, so an implementation that
//               caches results keyed on its input is caught rather than paid.
//               R comes from a pilot measurement that aims for ~50 ms of timed
//               work (clock_gettime(CLOCK_MONOTONIC) around every call),
//               clamped to [64, 250000]; BENCH_TARGET_MS=5..50 overrides the
//               target and BENCH_VERBOSE=1 reports R and the sample spread on
//               stderr.
//***************************************************************************/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <dlfcn.h>

#include "ojph_defs.h"
#include "ojph_mem.h"
#include "ojph_file.h"
#include "ojph_params.h"
#include "ojph_params_local.h"
#include "ojph_transform.h"
#include "ojph_colour.h"
#include "ojph_codeblock_fun.h"
#include "ojph_codeblock.h"

using namespace ojph;
using namespace ojph::local;

//===========================================================================
// deterministic RNG (xorshift64*), written out so no libc rand() involved
//===========================================================================
static uint64_t g_rs = 1;
static uint64_t rn()
{
  uint64_t x = g_rs;
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
  g_rs = x;
  return x * 0x2545F4914F6CDD1DULL;
}
static void seed_rng(uint64_t s) { g_rs = s * 2654435761u + 1u; if (g_rs == 0) g_rs = 1; }

static const char* g_seed_arg = "1";   // argv[2] for every non-fixture fnid
static const char* g_file_arg = NULL;  // argv[2] for decode_cb32

static double now_ns()
{
  timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

//===========================================================================
// geometry -- fixed and identical in every build for a given fnid
//===========================================================================
static const size_t WAVE_W   = 1024;  // samples on a subband line
static const size_t VERT_N   = 1024;  // samples a vertical lifting step runs over
static const size_t COLOUR_N = 1024;  // samples of a colour-transform row
static const size_t TX_COUNT = 64;    // codeblock row width
static const ui32   ENC_W    = 64;    // codeblock used by encode_cb32
static const ui32   ENC_H    = 64;

//===========================================================================
// aligned scratch buffers with head/tail slack and a pristine snapshot
//
// The SIMD kernels load 256-bit vectors from these buffers, so every payload
// pointer must be 32-byte aligned; they also read buf[-1] (symmetric
// extension) and write up to seven elements past the logical line end (the
// vector loops carry no scalar tail), hence the slack on both sides.
//===========================================================================
struct Buf
{
  ui8*   raw;       // allocation base
  ui8*   pristine;  // snapshot of the initial contents
  ui8*   p;         // payload pointer handed to the kernel
  size_t bytes;     // total allocation
  size_t n_ele;     // logical payload elements
  size_t ele_sz;
};

static const int MAX_BUF = 24;
static Buf  g_buf[MAX_BUF];
static int  g_nbuf = 0;

static Buf* make_buf(size_t n_ele, size_t ele_sz, size_t head_ele, size_t tail_ele)
{
  if (g_nbuf >= MAX_BUF) { fprintf(stderr, "bench_stage: too many buffers\n"); exit(3); }
  Buf* b = &g_buf[g_nbuf++];
  size_t head = head_ele * ele_sz, tail = tail_ele * ele_sz;
  b->bytes = head + n_ele * ele_sz + tail;
  b->n_ele = n_ele; b->ele_sz = ele_sz;
  if (posix_memalign((void**)&b->raw, 64, b->bytes) != 0)
  { fprintf(stderr, "bench_stage: %zu byte allocation failed\n", b->bytes); exit(3); }
  memset(b->raw, 0, b->bytes);
  b->pristine = (ui8*)malloc(b->bytes);
  if (!b->pristine) { fprintf(stderr, "bench_stage: allocation failed\n"); exit(3); }
  b->p = b->raw + head;
  return b;
}
static void snapshot_all()
{
  for (int i = 0; i < g_nbuf; ++i)
    memcpy(g_buf[i].pristine, g_buf[i].raw, g_buf[i].bytes);
}
static void restore_all()
{
  for (int i = 0; i < g_nbuf; ++i)
    memcpy(g_buf[i].raw, g_buf[i].pristine, g_buf[i].bytes);
}

// Only the buffers a given fnid actually reads or writes are restored between
// repetitions; that keeps the untimed work per iteration as small as possible
// so the measured region is not diluted by it.
static Buf* g_rst[8];
static int  g_nrst = 0;
static void reg_reset(Buf* b)
{
  if (g_nrst < 8) g_rst[g_nrst++] = b;
}
static void restore_sel()
{
  for (int i = 0; i < g_nrst; ++i)
    memcpy(g_rst[i]->raw, g_rst[i]->pristine, g_rst[i]->bytes);
}

//===========================================================================
// input distributions -- realistic magnitudes/layout per function
//===========================================================================
// integer subband coefficient: ~1/8 zero, geometric magnitude spread, 14 bits
static si32 rand_sub_i32()
{
  uint64_t r = rn();
  if ((r & 7u) == 0) return 0;
  uint32_t e = (uint32_t)((r >> 3) & 13u);
  uint32_t mag = (1u << e) | (uint32_t)((r >> 9) & ((1u << e) - 1u));
  return (r & 0x10000u) ? (si32)mag : -(si32)mag;
}
// float subband coefficient: same shape, with a fractional tail
static float rand_sub_f32()
{
  uint64_t r = rn();
  if ((r & 7u) == 0) return 0.0f;
  uint32_t e = (uint32_t)((r >> 3) & 7u);
  float mag = (float)((1u << e) | (uint32_t)((r >> 9) & ((1u << e) - 1u)));
  mag += (float)((r >> 20) & 0xFFu) / 256.0f;
  return (r & 0x10000u) ? mag : -mag;
}
// 8-bit image sample
static si32 rand_img_i32() { return (si32)(rn() & 0xFFu); }
static float rand_img_f32() { return (float)(rn() & 0x3FFu) * 0.25f; }

//===========================================================================
// dispatch seams
//===========================================================================
static codeblock_fun g_cb32;      // init(false): 32-bit / irreversible
static codeblock_fun g_cb64;      // init(true) : 64-bit / reversible
static param_atk     g_atk_rev, g_atk_irv;
static const param_atk*    g_a53 = NULL;
static const param_atk*    g_a97 = NULL;
static const lifting_step* g_s53 = NULL;
static const lifting_step* g_s97 = NULL;

static void init_seams()
{
  g_cb32.init(false);
  g_cb64.init(true);
  init_wavelet_transform_functions();
  init_colour_transform_functions();
  g_a53 = g_atk_rev.get_atk(1);     // init_rev53(), 5/3, Natk = 2
  g_a97 = g_atk_irv.get_atk(0);     // init_irv97(), 9/7, Natk = 4
  if (g_a53->get_num_steps() < 1 || g_a97->get_num_steps() < 1)
  { fprintf(stderr, "bench_stage: atk init failed\n"); exit(3); }
  g_s53 = g_a53->get_step(0);
  g_s97 = g_a97->get_step(0);
}

//===========================================================================
// dump registry
//===========================================================================
static void*  g_dptr[12];
static size_t g_dlen[12];
static int    g_ndump = 0;
static void reg_dump(void* p, size_t bytes)
{
  if (g_ndump >= 12) { fprintf(stderr, "bench_stage: dump list full\n"); exit(3); }
  g_dptr[g_ndump] = p; g_dlen[g_ndump] = bytes; ++g_ndump;
}
static void zero_dump() { g_ndump = 0; }

//===========================================================================
// job hooks
//===========================================================================
static void (*g_reset)() = NULL;   // non timed: restore inputs
static void (*g_run)()   = NULL;   // timed: one call
static void (*g_post)()  = NULL;   // non timed: copy anything that dies with a reset
static void (*g_symno)() = NULL;   // report resolved kernel symbols

//===========================================================================
// per-repetition input variation (the `benchvary` mode)
//
// A timed loop that hands every repetition the same input is memoizable behind
// the seam: an implementation can cache the result -- keyed on the input, or
// on a pointer and a length -- and be paid for the cache instead of the
// kernel.  The scored `bench` mode deliberately keeps every repetition
// identical, so reference and candidate are compared on the same content; the
// `benchvary` mode is the same measurement with the input re-drawn before each
// timed call -- same shape, same distribution, a different draw each
// repetition, from this step's own seed advanced by the repetition number, so
// a cache of any size misses every repetition.  The verifier compares the two
// medians and fails the step when the repeated-input ratio collapses under
// re-drawing, which is what a memoising implementation looks like.
//
// The redraw happens where the reset happens -- outside the timed region --
// and both trees run the identical sequence (the warm-up and pilot take fixed
// rep numbers; R is the only thing that varies, and it only decides how far
// the timed sequence runs).  The dump comparison never varies: it runs once,
// on the pristine input, which is what defines the correct output.
//
// The fixture-driven family has no generator to re-draw from: its input is a
// compressed codeblock.  It varies the bytes of that buffer that carry
// coefficient magnitudes and nothing structural -- see vary_decode_cb32.
//===========================================================================
static void (*g_vary)(long) = NULL;   // non timed: re-draw the inputs
static uint64_t g_vary_seed = 0;      // this step's verifier-owned seed

// Content for repetition `rep`.  A fixed mix (xorshift-multiply), so both
// trees and both runs derive the same input from the same repetition number.
static void vary_seed(long rep)
{
  seed_rng(g_vary_seed ^ ((uint64_t)(rep + 1) * 0x9E3779B97F4A7C15ULL));
}

//===========================================================================
// line_buf wrappers over the scratch buffers
//===========================================================================
static line_buf g_li[8];   // 32-bit integer lines
static line_buf g_lf[8];   // float lines
static si32* g_ip[8];
static float* g_fp[8];
static Buf*  g_ibuf[8];
static Buf*  g_fbuf[8];

static si32* mk_i32(int slot, size_t n_ele)
{ g_ibuf[slot] = make_buf(n_ele, 4, 32, 64); g_ip[slot] = (si32*)g_ibuf[slot]->p; return g_ip[slot]; }
static float* mk_f32(int slot, size_t n_ele)
{ g_fbuf[slot] = make_buf(n_ele, 4, 32, 64); g_fp[slot] = (float*)g_fbuf[slot]->p; return g_fp[slot]; }
static void wr_i32(int slot, size_t n_ele)
{ g_li[slot].wrap(g_ip[slot], n_ele, 1); }
static void wr_f32(int slot, size_t n_ele)
{ g_lf[slot].wrap(g_fp[slot], n_ele, 1); }

//===========================================================================
// dladdr symbol naming (Itanium mangling carries the identifier length)
//===========================================================================
static const char* sym_at(const void* p)
{
  Dl_info info;
  if (!dladdr(p, &info) || !info.dli_sname) return "?";
  const char* s = info.dli_sname;
  const char* q = strstr(s, "local");
  if (q) s = q + 5;
  size_t len = 0;
  while (*s >= '0' && *s <= '9') { len = len * 10 + (size_t)(*s - '0'); ++s; }
  static char name[128];
  size_t n = 0;
  for (; n < len && *s && n + 1 < sizeof(name); ++n, ++s) name[n] = *s;
  name[n] = 0;
  return n ? name : info.dli_sname;
}
static void say_sym(const char* what, const void* p)
{ printf("%s %s\n", what, sym_at(p)); }

//===========================================================================
// 1. decode_cb32 -- fixture driven
//===========================================================================
static ui32* g_dec = NULL;
static size_t g_dec_words = 0;
static ui8*  g_fix_data = NULL;
static ui32  g_fix_len1 = 0, g_fix_len2 = 0, g_fix_np = 0, g_fix_missing = 0;
static ui32  g_fix_w = 0, g_fix_h = 0, g_fix_stride = 0;

// The timed call decodes whatever the current input is: the fixture's own
// payload for the dump, and a freshly encoded codeblock for each timed
// repetition (see vary_decode_cb32).
static ui8* g_in_data = NULL;
static ui32 g_in_len1 = 0, g_in_len2 = 0;

static void run_decode_cb32()
{
  bool ok = g_cb32.decode_cb32(g_in_data, g_dec, g_fix_missing, g_fix_np,
                               g_in_len1, g_in_len2, g_fix_w, g_fix_h,
                               g_fix_stride, false);
  if (!ok) { fprintf(stderr, "bench_stage: decode_cb32 returned false\n"); exit(4); }
}
static void sym_decode_cb32() { say_sym("decode_cb32", (const void*)g_cb32.decode_cb32); }

// A fresh codeblock per repetition, built from the fixture's own coefficients.
//
// A compressed codeblock cannot be re-drawn by perturbing its bytes: the
// decoder derives control flow (MEL run lengths, VLC codes, the precision
// check U_q > mmsbp2) from bits that a perturbation moves, and a randomly
// perturbed payload fails to decode more often than not.  So the harness goes
// through the coefficient domain instead:
//
//   setup  decode the fixture once (untimed) to recover the coefficient array,
//          and anchor the encoder: re-encoding that array with the fixture's
//          own parameters must reproduce the fixture's payload byte for byte.
//          A tree whose encoder is not byte-exact on this input fails here --
//          the same behaviour requirement the pipeline digests enforce, stated
//          for this input.
//   vary   perturb the coefficient array (each word XORed below the top set
//          bit of the largest magnitude, so the array keeps the bit length the
//          fixture's missing_msbs is valid for) and encode it with the tree's
//          own encoder.  The result is a valid codeblock of the same shape,
//          and it is different on every repetition -- a cache keyed on the
//          input misses every time, whatever its size.
//
// Both trees draw the same coefficients (same fixture, same repetition
// number) and, being byte-exact, produce the same codeblock, so the ratio is
// still like-for-like.  The dump comparison is not affected: it runs once, on
// the fixture itself.
static ui32* g_fix_coef = NULL;       // the fixture's decoded coefficients
static ui32* g_fix_work = NULL;       // this repetition's coefficients
static ui8*  g_fix_payload = NULL;    // this repetition's codeblock
static ui32  g_fix_mask = 0;          // perturbation mask, below the top bit
static ui32  g_fix_cap = 0;
static mem_elastic_allocator* g_fix_elastic = NULL;

static void vary_decode_cb32(long rep)
{
  if (!g_fix_coef || !g_fix_elastic) return;
  uint64_t s = 0x9E3779B97F4A7C15ULL * (uint64_t)(rep + 1) + 1;
  size_t words = (size_t)g_fix_stride * g_fix_h;
  for (size_t i = 0; i < words; ++i) {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    g_fix_work[i] = g_fix_coef[i] ^
                    (ui32)((s * 0x2545F4914F6CDD1DULL) >> 32) & g_fix_mask;
  }
  g_fix_elastic->restart();
  coded_lists* coded = NULL;
  ui32 lengths[2] = { 0, 0 };
  g_cb32.encode_cb32(g_fix_work, g_fix_missing, g_fix_np, g_fix_w, g_fix_h,
                     g_fix_stride, lengths, g_fix_elastic, coded);
  size_t n = coded ? (size_t)lengths[0] + lengths[1] : 0;
  if (!coded || lengths[0] == 0 || n > g_fix_cap) {
    // The encoder refused an input its own fixture is a valid member of, or
    // outgrew the buffer sized from that fixture.  Fall back to the fixture so
    // the repetition is still a legal decode; the redraw works for every tree
    // whose encoder is byte-exact, which the anchor above has already checked.
    fprintf(stderr, "bench_stage: encoder produced no usable codeblock for "
                    "repetition %ld; using the fixture\n", rep);
    g_in_data = g_fix_data; g_in_len1 = g_fix_len1; g_in_len2 = g_fix_len2;
    return;
  }
  memcpy(g_fix_payload, coded->buf, n);
  g_in_data = g_fix_payload; g_in_len1 = lengths[0]; g_in_len2 = lengths[1];
}

static void setup_decode_cb32(const char* path)
{
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "bench_stage: cannot open fixture %s\n", path); exit(2); }
  ui8 hdr[64];
  if (fread(hdr, 1, 64, f) != 64)
  { fprintf(stderr, "bench_stage: fixture %s: short header\n", path); exit(2); }
  char tag[8] = {0};
  if (sscanf((const char*)hdr, "%3s %u %u %u %u %u %u %u", tag, &g_fix_w, &g_fix_h,
             &g_fix_stride, &g_fix_missing, &g_fix_len1, &g_fix_len2,
             &g_fix_np) != 8 || strcmp(tag, "HB1") != 0)
  { fprintf(stderr, "bench_stage: fixture %s: bad header\n", path); exit(2); }
  if (g_fix_w == 0 || g_fix_h == 0 || g_fix_stride < g_fix_w ||
      g_fix_len1 + g_fix_len2 == 0)
  { fprintf(stderr, "bench_stage: fixture %s: implausible header\n", path); exit(2); }

  g_fix_data = (ui8*)malloc(g_fix_len1 + g_fix_len2);
  if (!g_fix_data) { fprintf(stderr, "bench_stage: out of memory\n"); exit(3); }
  if (fread(g_fix_data, 1, g_fix_len1 + g_fix_len2, f) !=
      (size_t)(g_fix_len1 + g_fix_len2))
  { fprintf(stderr, "bench_stage: fixture %s: short payload\n", path); exit(2); }
  fclose(f);

  // the decoder buffer is exactly stride*height words, zeroed before each call
  g_dec_words = (size_t)g_fix_stride * g_fix_h;
  g_dec = (ui32*)make_buf(g_dec_words, 4, 0, 64)->p;

  // --- recover the fixture's coefficients, and anchor the encoder ----------
  size_t words = g_dec_words;
  g_fix_coef = (ui32*)malloc(words * 4 + 256);   // slack: kernels overrun
  g_fix_work = (ui32*)malloc(words * 4 + 256);
  if (!g_fix_coef || !g_fix_work)
  { fprintf(stderr, "bench_stage: out of memory\n"); exit(3); }
  memset(g_fix_coef, 0, words * 4 + 256);
  bool ok = g_cb32.decode_cb32(g_fix_data, g_fix_coef, g_fix_missing,
                               g_fix_np, g_fix_len1, g_fix_len2, g_fix_w,
                               g_fix_h, g_fix_stride, false);
  if (!ok) { fprintf(stderr, "bench_stage: fixture %s does not decode\n", path); exit(4); }

  g_fix_elastic = (mem_elastic_allocator*)malloc(sizeof(mem_elastic_allocator));
  if (!g_fix_elastic) { fprintf(stderr, "bench_stage: out of memory\n"); exit(3); }
  new (g_fix_elastic) mem_elastic_allocator(4096);

  coded_lists* anchor = NULL;
  ui32 alen[2] = { 0, 0 };
  g_cb32.encode_cb32(g_fix_coef, g_fix_missing, g_fix_np, g_fix_w, g_fix_h,
                     g_fix_stride, alen, g_fix_elastic, anchor);
  size_t an = anchor ? (size_t)alen[0] + alen[1] : 0;
  if (!anchor || an != (size_t)(g_fix_len1 + g_fix_len2) ||
      memcmp(anchor->buf, g_fix_data, an) != 0)
  {
    fprintf(stderr, "bench_stage: encoder does not reproduce fixture %s"
                    " byte for byte -- the tree's coding behaviour differs"
                    " from the reference on this input\n", path);
    exit(4);
  }

  // Perturbation stays below the top set bit of the largest magnitude, so
  // every redraw keeps the bit length the fixture's missing_msbs is valid for.
  ui32 maxv = 0;
  for (size_t i = 0; i < words; ++i) if (g_fix_coef[i] > maxv) maxv = g_fix_coef[i];
  ui32 top = 0;
  while ((maxv >> top) != 0 && top < 32) ++top;
  g_fix_mask = (top > 1) ? ((1u << (top - 1)) - 1u) : 0;
  if (g_fix_mask == 0)
    fprintf(stderr, "bench_stage: fixture %s has no magnitude bits to vary;"
                    " repetitions will repeat the same input\n", path);

  g_fix_cap = (ui32)(g_fix_len1 + g_fix_len2) * 2 + 4096;
  g_fix_payload = (ui8*)malloc(g_fix_cap);
  if (!g_fix_payload) { fprintf(stderr, "bench_stage: out of memory\n"); exit(3); }
  g_in_data = g_fix_data; g_in_len1 = g_fix_len1; g_in_len2 = g_fix_len2;

  zero_dump();
  reg_dump(g_dec, g_dec_words * 4);
  reg_reset(&g_buf[g_nbuf - 1]);       // the decoder output buffer
  g_reset = restore_sel;
  g_run   = run_decode_cb32;
  g_post  = NULL;
  g_symno = sym_decode_cb32;
  g_vary  = vary_decode_cb32;
  g_vary_seed = 0;                     // the fixture is the seed
}

//===========================================================================
// 2. encode_cb32 -- seed driven, 64x64 codeblock
//===========================================================================
static ui32* g_enc_src = NULL;
static mem_elastic_allocator* g_elastic = NULL;
static coded_lists* g_coded = NULL;
static ui32 g_enc_lengths[2];
static ui32 g_enc_missing = 0;
static ui8* g_enc_scratch = NULL;

// The codestream keeps one persistent mem_elastic_allocator and calls
// restart() on it between uses (ojph_codestream_local.cpp:109), which recycles
// the stores it already holds instead of freeing and re-allocating them.  Doing
// the same here keeps the payload pages resident, so no page faults land inside
// the timed call.
static void reset_elastic()
{
  if (!g_elastic) return;
  g_elastic->restart();
  g_coded = NULL;
}
static void reset_encode_cb32() { restore_all(); reset_elastic(); }

static void run_encode_cb32()
{
  g_cb32.encode_cb32(g_enc_src, g_enc_missing, 1, ENC_W, ENC_H, ENC_W,
                     g_enc_lengths, g_elastic, g_coded);
}
static void post_encode_cb32()
{
  if (!g_coded || g_enc_lengths[0] == 0)
  { fprintf(stderr, "bench_stage: encode_cb32 produced no output\n"); exit(4); }
  size_t n = g_enc_lengths[0];
  memcpy(g_enc_scratch, g_coded->buf, n);
  ui32 le[2] = { g_enc_lengths[0], g_enc_lengths[1] };
  memcpy(g_enc_scratch + n, le, 8);
  zero_dump();
  reg_dump(g_enc_scratch, n + 8);
}
static void sym_encode_cb32() { say_sym("encode_cb32", (const void*)g_cb32.encode_cb32); }

// Same shape as the pipeline presents to the coder: about a quarter zeros
// and a geometric magnitude spread, then shifted up so that the block is
// densely significant at the K_max the bit-plane budget allows.  Draws from
// whatever the RNG currently holds, so the setup and the per-repetition
// redraw share it.
static void fill_encode_cb32()
{
  size_t nmemb = (size_t)ENC_W * ENC_H;
  ui32 maxv = 0;
  for (size_t i = 0; i < nmemb; ++i) {
    uint64_t r = rn();
    ui32 mag;
    if ((r & 3u) == 0) mag = 0;
    else {
      ui32 e = (ui32)((r >> 2) & 7u);
      mag = (1u << e) | (ui32)((r >> 5) & ((1u << e) - 1u));
    }
    g_enc_src[i] = mag;
    if (mag > maxv) maxv = mag;
  }
  ui32 bits = 1;
  if (maxv) { while ((maxv >> bits) != 0 && bits < 30) ++bits; }
  ui32 K_max = bits + 2; if (K_max < 3) K_max = 3; if (K_max > 31) K_max = 31;
  ui32 shift = 32u - bits;
  for (size_t i = 0; i < nmemb; ++i) g_enc_src[i] <<= shift;
  g_enc_missing = K_max - 1;
}

static void vary_encode_cb32(long rep)
{
  vary_seed(rep);
  fill_encode_cb32();
}

static void setup_encode_cb32(const char* arg)
{
  uint64_t seed = strtoull(arg, NULL, 10);
  seed_rng(seed);

  size_t nmemb = (size_t)ENC_W * ENC_H;
  g_enc_src = (ui32*)make_buf(nmemb, 4, 0, 64)->p;
  fill_encode_cb32();

  g_elastic = (mem_elastic_allocator*)malloc(sizeof(mem_elastic_allocator));
  new (g_elastic) mem_elastic_allocator(4096);

  // payload scratch: one coded_lists chunk plus a generous margin for the
  // 8-byte (lengths[0], lengths[1]) trailer
  g_enc_scratch = (ui8*)malloc(65536 + 16);
  if (!g_enc_scratch) { fprintf(stderr, "bench_stage: out of memory\n"); exit(3); }

  snapshot_all();
  g_reset = reset_encode_cb32;
  g_run   = run_encode_cb32;
  g_post  = post_encode_cb32;
  g_symno = sym_encode_cb32;
  g_vary  = vary_encode_cb32;
  g_vary_seed = seed;
}

//===========================================================================
// 3/4. wavelet dispatch pointers
//===========================================================================
enum { I_IN = 0, I_L, I_H, I_O, I_A, I_B, I_C };
enum { F_IN = 0, F_L, F_H, F_O, F_A, F_B, F_C };
static size_t g_w = WAVE_W, g_lw = 0, g_hw = 0;

static void run_rev_vert_step()
{ rev_vert_step(g_s53, &g_li[I_A], &g_li[I_B], &g_li[I_C], (ui32)VERT_N, false); }
static void run_rev_horz_ana()
{ rev_horz_ana(g_a53, &g_li[I_L], &g_li[I_H], &g_li[I_IN], (ui32)g_w, true); }
static void run_rev_horz_syn()
{ rev_horz_syn(g_a53, &g_li[I_O], &g_li[I_L], &g_li[I_H], (ui32)g_w, true); }
static void run_irv_vert_step()
{ irv_vert_step(g_s97, &g_lf[F_A], &g_lf[F_B], &g_lf[F_C], (ui32)VERT_N, false); }
static void run_irv_vert_times_K()
{ irv_vert_times_K(g_a97->get_K(), &g_lf[F_C], (ui32)VERT_N); }
static void run_irv_horz_ana()
{ irv_horz_ana(g_a97, &g_lf[F_L], &g_lf[F_H], &g_lf[F_IN], (ui32)g_w, true); }
static void run_irv_horz_syn()
{ irv_horz_syn(g_a97, &g_lf[F_O], &g_lf[F_L], &g_lf[F_H], (ui32)g_w, true); }

static void sym_rev_vert_step()    { say_sym("rev_vert_step",    (const void*)rev_vert_step); }
static void sym_rev_horz_ana()     { say_sym("rev_horz_ana",     (const void*)rev_horz_ana); }
static void sym_rev_horz_syn()     { say_sym("rev_horz_syn",     (const void*)rev_horz_syn); }
static void sym_irv_vert_step()    { say_sym("irv_vert_step",    (const void*)irv_vert_step); }
static void sym_irv_vert_times_K() { say_sym("irv_vert_times_K", (const void*)irv_vert_times_K); }
static void sym_irv_horz_ana()     { say_sym("irv_horz_ana",     (const void*)irv_horz_ana); }
static void sym_irv_horz_syn()     { say_sym("irv_horz_syn",     (const void*)irv_horz_syn); }

// One draw of every line a wavelet kernel reads (and of the destination lines'
// initial contents, which the reset restores anyway).  Shared by the setup and
// the per-repetition redraw.
static bool g_wavelet_irv = false;
static void fill_wavelet_inputs()
{
  if (g_wavelet_irv) {
    for (ui32 i = 0; i < g_w; ++i) {
      float v = rand_sub_f32();
      g_fp[F_IN][i] = v;
      g_fp[F_A][i]  = v;
      g_fp[F_B][i]  = rand_sub_f32();
      g_fp[F_L][i]  = rand_sub_f32() * 0.5f;
      g_fp[F_H][i]  = rand_sub_f32() * 0.25f;
      // the augmenting line of a vertical lifting step already holds the
      // accumulated result of the preceding steps, so it is not zero here
      g_fp[F_C][i]  = rand_sub_f32();
    }
    memset(g_fp[F_O], 0, g_w * 4);
  } else {
    for (ui32 i = 0; i < g_w; ++i) {
      si32 v = rand_sub_i32();
      g_ip[I_IN][i] = v;
      g_ip[I_A][i]  = v;
      g_ip[I_B][i]  = rand_sub_i32();
      g_ip[I_L][i]  = rand_sub_i32() >> 1;
      g_ip[I_H][i]  = rand_sub_i32() >> 2;
      g_ip[I_C][i]  = rand_sub_i32();
    }
    memset(g_ip[I_O], 0, g_w * 4);
  }
}

static void vary_wavelet(long rep)
{
  vary_seed(rep);
  fill_wavelet_inputs();
}

static void setup_wavelet(const std::string& fnid)
{
  g_w  = WAVE_W;
  g_lw = (g_w + 1) >> 1;   // analysis with even = true
  g_hw = g_w >> 1;
  uint64_t seed = strtoull(g_seed_arg, NULL, 10);
  seed_rng(seed);

  bool irv = (fnid.compare(0, 3, "irv") == 0);
  g_wavelet_irv = irv;
  if (irv) for (int i = 0; i < 7; ++i) { mk_f32(i, g_w); wr_f32(i, g_w); }
  else     for (int i = 0; i < 7; ++i) { mk_i32(i, g_w); wr_i32(i, g_w); }
  fill_wavelet_inputs();
  // low/high destination lines carry their true logical sizes
  if (irv) { wr_f32(F_L, g_lw); wr_f32(F_H, g_hw); wr_f32(F_C, VERT_N); }
  else     { wr_i32(I_L, g_lw); wr_i32(I_H, g_hw); wr_i32(I_C, VERT_N); }

  zero_dump();
  g_post  = NULL;

  // The signature of every kernel takes its sources as const pointers and only
  // the destination lines change, so restore exactly those: the reset runs
  // untimed between calls, and re-copying the read-only lines would only push
  // the kernel's working set out of L1 between timed calls.
  if (irv) {
    if (fnid == "irv_vert_step" || fnid == "irv_vert_times_K")
      reg_reset(g_fbuf[F_C]);                       // aug line of the step
    else if (fnid == "irv_horz_syn")
      reg_reset(g_fbuf[F_O]);                       // dst
    else
      { reg_reset(g_fbuf[F_L]); reg_reset(g_fbuf[F_H]); }   // ldst + hdst
  } else {
    if (fnid == "rev_vert_step")
      reg_reset(g_ibuf[I_C]);
    else if (fnid == "rev_horz_syn")
      reg_reset(g_ibuf[I_O]);
    else
      { reg_reset(g_ibuf[I_L]); reg_reset(g_ibuf[I_H]); }
  }
  g_reset = restore_sel;

  if (fnid == "rev_vert_step") {
    reg_dump(g_ip[I_C], VERT_N * 4);
    g_run = run_rev_vert_step; g_symno = sym_rev_vert_step;
  } else if (fnid == "rev_horz_ana") {
    reg_dump(g_ip[I_L], g_lw * 4); reg_dump(g_ip[I_H], g_hw * 4);
    g_run = run_rev_horz_ana; g_symno = sym_rev_horz_ana;
  } else if (fnid == "rev_horz_syn") {
    reg_dump(g_ip[I_O], g_w * 4);
    g_run = run_rev_horz_syn; g_symno = sym_rev_horz_syn;
  } else if (fnid == "irv_vert_step") {
    reg_dump(g_fp[F_C], VERT_N * 4);
    g_run = run_irv_vert_step; g_symno = sym_irv_vert_step;
  } else if (fnid == "irv_vert_times_K") {
    reg_dump(g_fp[F_C], VERT_N * 4);
    g_run = run_irv_vert_times_K; g_symno = sym_irv_vert_times_K;
  } else if (fnid == "irv_horz_ana") {
    reg_dump(g_fp[F_L], g_lw * 4); reg_dump(g_fp[F_H], g_hw * 4);
    g_run = run_irv_horz_ana; g_symno = sym_irv_horz_ana;
  } else if (fnid == "irv_horz_syn") {
    reg_dump(g_fp[F_O], g_w * 4);
    g_run = run_irv_horz_syn; g_symno = sym_irv_horz_syn;
  } else {
    fprintf(stderr, "bench_stage: bad wavelet fnid %s\n", fnid.c_str()); exit(2);
  }

  g_vary = vary_wavelet;
  g_vary_seed = seed;
  snapshot_all();
}

//===========================================================================
// 5. colour transform seam
//===========================================================================
static void run_rct_forward()
{ rct_forward(&g_li[0], &g_li[1], &g_li[2], &g_li[3], &g_li[4], &g_li[5], (ui32)COLOUR_N); }
static void run_rct_backward()
{ rct_backward(&g_li[0], &g_li[1], &g_li[2], &g_li[3], &g_li[4], &g_li[5], (ui32)COLOUR_N); }
static void run_ict_forward()
{ ict_forward(g_fp[0], g_fp[1], g_fp[2], g_fp[3], g_fp[4], g_fp[5], (ui32)COLOUR_N); }
static void run_ict_backward()
{ ict_backward(g_fp[0], g_fp[1], g_fp[2], g_fp[3], g_fp[4], g_fp[5], (ui32)COLOUR_N); }
static void run_rev_convert()
{ rev_convert(&g_li[0], 0, &g_li[1], 0, (si64)(1u << 11), (ui32)COLOUR_N); }

static void sym_rct_forward()  { say_sym("rct_forward",  (const void*)rct_forward); }
static void sym_rct_backward() { say_sym("rct_backward", (const void*)rct_backward); }
static void sym_ict_forward()  { say_sym("ict_forward",  (const void*)ict_forward); }
static void sym_ict_backward() { say_sym("ict_backward", (const void*)ict_backward); }
static void sym_rev_convert()  { say_sym("rev_convert",  (const void*)rev_convert); }

// One draw of the source lines (and the destination lines' initial zeros).
// Shared by the setup and the per-repetition redraw.
static bool g_colour_int = false;
static bool g_colour_revconvert = false;
static void fill_colour_inputs()
{
  if (g_colour_int) {
    for (ui32 i = 0; i < COLOUR_N; ++i) {
      if (g_colour_revconvert) {
        // decoded samples of a 12-bit component, before the DC level shift
        g_ip[0][i] = (si32)(rn() & 0xFFFu) - 2048;
        g_ip[1][i] = 0;
      } else {
        // 8-bit image row
        g_ip[0][i] = rand_img_i32();
        g_ip[1][i] = rand_img_i32();
        g_ip[2][i] = rand_img_i32();
        g_ip[3][i] = 0; g_ip[4][i] = 0; g_ip[5][i] = 0;
      }
    }
  } else {
    for (ui32 i = 0; i < COLOUR_N; ++i) {
      g_fp[0][i] = rand_img_f32();
      g_fp[1][i] = rand_img_f32();
      g_fp[2][i] = rand_img_f32();
      g_fp[3][i] = 0; g_fp[4][i] = 0; g_fp[5][i] = 0;
    }
  }
}

static void vary_colour(long rep)
{
  vary_seed(rep);
  fill_colour_inputs();
}

static void setup_colour(const std::string& fnid)
{
  uint64_t seed = strtoull(g_seed_arg, NULL, 10);
  seed_rng(seed);
  bool intmode = (fnid == "rct_forward" || fnid == "rct_backward" ||
                  fnid == "rev_convert");
  zero_dump();
  g_post = NULL;

  if (intmode) {
    for (int i = 0; i < 6; ++i) { mk_i32(i, COLOUR_N); wr_i32(i, COLOUR_N); }
    // rct_forward takes r,g,b (0,1,2) and writes y,cb,cr (3,4,5);
    // rct_backward takes y,cb,cr and writes r,g,b -- the outputs are 3,4,5
    // either way; rev_convert(src 0, dst 1) writes only line 1.
    if (fnid == "rev_convert") reg_reset(g_ibuf[1]);
    else { reg_reset(g_ibuf[3]); reg_reset(g_ibuf[4]); reg_reset(g_ibuf[5]); }
    g_reset = restore_sel;
    g_colour_int = true;
    g_colour_revconvert = (fnid == "rev_convert");
    fill_colour_inputs();
    if (fnid == "rct_forward") {
      reg_dump(g_ip[3], COLOUR_N * 4);
      reg_dump(g_ip[4], COLOUR_N * 4);
      reg_dump(g_ip[5], COLOUR_N * 4);
      g_run = run_rct_forward; g_symno = sym_rct_forward;
    } else if (fnid == "rct_backward") {
      reg_dump(g_ip[3], COLOUR_N * 4);
      reg_dump(g_ip[4], COLOUR_N * 4);
      reg_dump(g_ip[5], COLOUR_N * 4);
      g_run = run_rct_backward; g_symno = sym_rct_backward;
    } else {
      reg_dump(g_ip[1], COLOUR_N * 4);
      g_run = run_rev_convert; g_symno = sym_rev_convert;
    }
  } else {
    for (int i = 0; i < 6; ++i) { mk_f32(i, COLOUR_N); wr_f32(i, COLOUR_N); }
    // ict_* both read 0,1,2 and write 3,4,5
    reg_reset(g_fbuf[3]); reg_reset(g_fbuf[4]); reg_reset(g_fbuf[5]);
    g_reset = restore_sel;
    g_colour_int = false;
    fill_colour_inputs();
    reg_dump(g_fp[3], COLOUR_N * 4);
    reg_dump(g_fp[4], COLOUR_N * 4);
    reg_dump(g_fp[5], COLOUR_N * 4);
    if (fnid == "ict_forward") { g_run = run_ict_forward;  g_symno = sym_ict_forward; }
    else                       { g_run = run_ict_backward; g_symno = sym_ict_backward; }
  }
  g_vary = vary_colour;
  g_vary_seed = seed;
  snapshot_all();
}

//===========================================================================
// 6. sample-format conversion seam
//===========================================================================
static void run_irv_convert_to_integer()
{ irv_convert_to_integer(&g_lf[0], &g_li[0], 0, 8, false, (ui32)COLOUR_N); }
static void run_irv_convert_to_float()
{ irv_convert_to_float(&g_li[0], 0, &g_lf[0], 8, false, (ui32)COLOUR_N); }

static void sym_irv_convert_to_integer()
{ say_sym("irv_convert_to_integer", (const void*)irv_convert_to_integer); }
static void sym_irv_convert_to_float()
{ say_sym("irv_convert_to_float",   (const void*)irv_convert_to_float); }

// Which direction the convert job runs, for the shared redraw.
static bool g_convert_to_integer = false;
static void fill_convert_inputs()
{
  if (g_convert_to_integer) {
    for (ui32 i = 0; i < COLOUR_N; ++i) {
      // 8-bit sample values held as floats in [-0.5, 0.5); the magnitude is an
      // odd multiple of 2^-10 so that scaling by 2^8 never lands exactly on a
      // .5 tie, where round-half-away (generic) and round-half-even (cvtps)
      // would legitimately disagree.
      si32 k = (si32)(rn() & 0x3FFu) - 512;
      k |= 1;
      g_fp[0][i] = (float)k / 1024.0f;
      g_ip[0][i] = 0;
    }
  } else {
    for (ui32 i = 0; i < COLOUR_N; ++i) g_ip[0][i] = (si32)(rn() & 0xFFu);
  }
}

static void vary_convert(long rep)
{
  vary_seed(rep);
  fill_convert_inputs();
}

static void setup_convert(const std::string& fnid)
{
  uint64_t seed = strtoull(g_seed_arg, NULL, 10);
  seed_rng(seed);
  zero_dump();
  g_post = NULL;
  if (fnid == "irv_convert_to_integer") {
    mk_f32(0, COLOUR_N); wr_f32(0, COLOUR_N);
    mk_i32(0, COLOUR_N); wr_i32(0, COLOUR_N);
    reg_reset(g_ibuf[0]); g_reset = restore_sel;
    g_convert_to_integer = true;
    fill_convert_inputs();
    reg_dump(g_ip[0], COLOUR_N * 4);
    g_run = run_irv_convert_to_integer; g_symno = sym_irv_convert_to_integer;
  } else {
    mk_i32(0, COLOUR_N); wr_i32(0, COLOUR_N);
    mk_f32(0, COLOUR_N); wr_f32(0, COLOUR_N);
    reg_reset(g_fbuf[0]); g_reset = restore_sel;
    g_convert_to_integer = false;
    fill_convert_inputs();
    reg_dump(g_fp[0], COLOUR_N * 4);
    g_run = run_irv_convert_to_float; g_symno = sym_irv_convert_to_float;
  }
  g_vary = vary_convert;
  g_vary_seed = seed;
  snapshot_all();
}

//===========================================================================
// 7. subband <-> codeblock transfer seam
//
// The three max_val words the SIMD kernels keep are a 256-bit lane-wise OR
// accumulator, not a scalar: the generic kernel writes the whole OR into
// lane 0 while the vector kernel leaves the OR split across lanes and folds
// it in cb.find_max_val32/64.  The pipeline only ever consumes the folded
// value, so the dump records the folded value (the matched pair the caller
// actually reads) rather than the build-specific lane layout.
//===========================================================================
static ui32 g_mv32[8] __attribute__((aligned(32)));
static ui64 g_mv64[4] __attribute__((aligned(32)));
static ui32 g_tx_kmax = 0;
static float g_tx_delta_inv = 0.0f, g_tx_delta = 0.0f;
static void* g_tx_sp = NULL;
static void* g_tx_dp = NULL;

static void run_tx_to_cb32()
{ g_cb32.tx_to_cb32(g_tx_sp, (ui32*)g_tx_dp, g_tx_kmax, g_tx_delta_inv,
                    (ui32)TX_COUNT, g_mv32); }
static void run_tx_from_cb32()
{ g_cb32.tx_from_cb32((const ui32*)g_tx_sp, g_tx_dp, g_tx_kmax, g_tx_delta,
                      (ui32)TX_COUNT); }
static void run_tx_to_cb64()
{ g_cb64.tx_to_cb64(g_tx_sp, (ui64*)g_tx_dp, g_tx_kmax, g_tx_delta_inv,
                    (ui32)TX_COUNT, g_mv64); }
static void run_tx_from_cb64()
{ g_cb64.tx_from_cb64((const ui64*)g_tx_sp, g_tx_dp, g_tx_kmax, g_tx_delta,
                      (ui32)TX_COUNT); }

static void post_tx_to_cb32()
{
  // The generic kernel ORs every magnitude into lane 0, the vector kernel ORs
  // them lane-wise ("supports up to 256 bits"), so the only build-independent
  // value to dump is the folded one -- and the fold must run over all lanes as
  // the kernel left them, exactly as the pipeline does it, before lane 0 is
  // overwritten with the result.
  ui32 m = g_cb32.find_max_val32(g_mv32);
  memset(g_mv32, 0, sizeof(g_mv32));
  g_mv32[0] = m;
}
static void post_tx_to_cb64()
{
  ui64 m = g_cb64.find_max_val64(g_mv64);
  memset(g_mv64, 0, sizeof(g_mv64));
  g_mv64[0] = m;
}

// max_val is an accumulator: the generic kernel ORs into lane 0, the vector
// kernel ORs lane-wise, so it must start each repetition from zero for the
// timed call to see the same starting state the dumped one saw.
static void reset_tx()
{ restore_sel(); memset(g_mv32, 0, sizeof(g_mv32)); memset(g_mv64, 0, sizeof(g_mv64)); }

// generic (no max_val)
static void sym_tx_to_cb32()   { say_sym("tx_to_cb32",   (const void*)g_cb32.tx_to_cb32); }
static void sym_tx_from_cb32() { say_sym("tx_from_cb32", (const void*)g_cb32.tx_from_cb32); }
static void sym_tx_to_cb64()   { say_sym("tx_to_cb64",   (const void*)g_cb64.tx_to_cb64); }
static void sym_tx_from_cb64() { say_sym("tx_from_cb64", (const void*)g_cb64.tx_from_cb64); }

// Which arm of the transfer seam this job runs, for the shared redraw.
static bool g_tx_is64 = false, g_tx_to = false;
static void fill_tx_inputs()
{
  if (g_tx_is64) {
    if (g_tx_to) {
      si64* sp = (si64*)g_tx_sp;
      ui64* dp = (ui64*)g_tx_dp;
      for (size_t i = 0; i < TX_COUNT; ++i) {
        si64 v = (si64)(rn() & 0x7FFFu);
        sp[i] = (rn() & 1) ? v : -v;
        dp[i] = 0;
      }
    } else {
      ui64* sp = (ui64*)g_tx_sp;
      si64* dp = (si64*)g_tx_dp;
      for (size_t i = 0; i < TX_COUNT; ++i) {
        ui64 v = rn() & 0x7FFFFFFFFFFFFFFFULL;
        sp[i] = (rn() & 1) ? (v | 0x8000000000000000ULL) : v;
        dp[i] = 0;
      }
    }
  } else {
    if (g_tx_to) {
      float* sp = (float*)g_tx_sp;
      ui32*  dp = (ui32*)g_tx_dp;
      for (size_t i = 0; i < TX_COUNT; ++i) {
        float v = rand_sub_f32() * 0.5f;    // |v| < 256 so |v*delta_inv| < 2^K_max
        sp[i] = v; dp[i] = 0;
      }
    } else {
      ui32* sp = (ui32*)g_tx_sp;
      float* dp = (float*)g_tx_dp;
      for (size_t i = 0; i < TX_COUNT; ++i) {
        ui32 v = (ui32)(rn() & 0x3FFu);     // magnitude below 2^K_max
        sp[i] = (rn() & 1) ? (v | 0x80000000u) : v;
        dp[i] = 0.0f;
      }
    }
  }
}

static void vary_tx(long rep)
{
  vary_seed(rep);
  fill_tx_inputs();
}

static void setup_tx(const std::string& fnid)
{
  uint64_t seed = strtoull(g_seed_arg, NULL, 10);
  seed_rng(seed);
  zero_dump();
  g_post  = NULL;
  g_reset = reset_tx;

  bool is64 = (fnid.find("cb64") != std::string::npos);
  bool to   = (fnid.compare(0, 5, "tx_to") == 0);
  g_tx_is64 = is64;
  g_tx_to = to;

  if (is64) {
    g_tx_kmax = 16;
    Buf* bsp = make_buf(TX_COUNT, 8, 0, 32);
    Buf* bdp = make_buf(TX_COUNT, 8, 0, 32);
    reg_reset(bdp);                   // sp is const; only dp is written
    g_tx_sp = bsp->p; g_tx_dp = bdp->p;
    if (to) {
      // reversible: si64 subband samples, magnitudes below 2^K_max
      fill_tx_inputs();
      memset(g_mv64, 0, sizeof(g_mv64));
      reg_dump(bdp->p, TX_COUNT * 8); reg_dump(g_mv64, 8);
      g_run = run_tx_to_cb64; g_post = post_tx_to_cb64; g_symno = sym_tx_to_cb64;
    } else {
      fill_tx_inputs();
      reg_dump(bdp->p, TX_COUNT * 8);
      g_run = run_tx_from_cb64; g_symno = sym_tx_from_cb64;
    }
  } else {
    // irreversible 32-bit: float subband samples on the way in, quantized
    // sign-magnitude words on the way back out
    g_tx_kmax = 10;
    g_tx_delta_inv = 4.0f;
    g_tx_delta = 0.25f;
    Buf* bsp = make_buf(TX_COUNT, 4, 0, 32);
    Buf* bdp = make_buf(TX_COUNT, 4, 0, 32);
    reg_reset(bdp);                   // sp is const; only dp is written
    g_tx_sp = bsp->p; g_tx_dp = bdp->p;
    if (to) {
      fill_tx_inputs();
      memset(g_mv32, 0, sizeof(g_mv32));
      reg_dump(bdp->p, TX_COUNT * 4); reg_dump(g_mv32, 4);
      g_run = run_tx_to_cb32; g_post = post_tx_to_cb32; g_symno = sym_tx_to_cb32;
    } else {
      fill_tx_inputs();
      reg_dump(bdp->p, TX_COUNT * 4);
      g_run = run_tx_from_cb32; g_symno = sym_tx_from_cb32;
    }
  }
  g_vary = vary_tx;
  g_vary_seed = seed;
  snapshot_all();
}

//===========================================================================
// table
//===========================================================================
static const char* FNIDS[] = {
  "decode_cb32", "encode_cb32",
  "rev_vert_step", "rev_horz_ana", "rev_horz_syn",
  "irv_vert_step", "irv_vert_times_K", "irv_horz_ana", "irv_horz_syn",
  "rct_forward", "rct_backward", "ict_forward", "ict_backward",
  "rev_convert", "irv_convert_to_integer", "irv_convert_to_float",
  "tx_to_cb32", "tx_to_cb64", "tx_from_cb32", "tx_from_cb64"
};
static const int NUM_FNIDS = (int)(sizeof(FNIDS) / sizeof(FNIDS[0]));

static void setup_job(const std::string& fnid)
{
  bool known = false;
  for (int i = 0; i < NUM_FNIDS; ++i)
    if (fnid == FNIDS[i]) { known = true; break; }
  if (!known) { fprintf(stderr, "bench_stage: unknown fnid %s\n", fnid.c_str()); exit(2); }

  if (fnid == "decode_cb32")       setup_decode_cb32(g_file_arg);
  else if (fnid == "encode_cb32")  setup_encode_cb32(g_seed_arg);
  else if (fnid == "rct_forward" || fnid == "rct_backward" ||
           fnid == "ict_forward" || fnid == "ict_backward" ||
           fnid == "rev_convert")  setup_colour(fnid);
  else if (fnid == "irv_convert_to_integer" ||
           fnid == "irv_convert_to_float") setup_convert(fnid);
  else if (fnid.compare(0, 3, "tx_") == 0)  setup_tx(fnid);
  else                             setup_wavelet(fnid);

  if (!g_run || !g_reset) { fprintf(stderr, "bench_stage: job not wired\n"); exit(3); }
}

static int cmp_u64(const void* a, const void* b)
{
  uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
  return (x < y) ? -1 : (x > y) ? 1 : 0;
}

int main(int argc, char** argv)
{
  if (argc >= 2 && strcmp(argv[1], "list") == 0) {
    for (int i = 0; i < NUM_FNIDS; ++i) printf("%s\n", FNIDS[i]);
    return 0;
  }
  if (argc >= 4 && strcmp(argv[1], "sym") == 0) {
    std::string fnid = argv[2];
    g_seed_arg = argv[3];
    g_file_arg = argv[3];
    init_seams();
    setup_job(fnid);
    if (g_symno) g_symno(); else fprintf(stderr, "bench_stage: no symbol report\n");
    return 0;
  }
  if (argc < 4) {
    fprintf(stderr,
      "usage: %s list\n"
      "       %s <fnid> <inputarg> dump <outfile>\n"
      "       %s <fnid> <inputarg> bench\n"
      "       %s <fnid> <inputarg> benchvary\n"
      "       %s sym <fnid> <seed>\n", argv[0], argv[0], argv[0], argv[0],
      argv[0]);
    return 2;
  }
  std::string fnid = argv[1];
  g_seed_arg = argv[2];
  g_file_arg = argv[2];
  std::string mode = argv[3];

  init_seams();
  setup_job(fnid);

  if (mode == "dump") {
    if (argc < 5) { fprintf(stderr, "bench_stage: dump needs an outfile\n"); return 2; }
    g_reset();
    g_run();
    if (g_post) g_post();
    FILE* f = fopen(argv[4], "wb");
    if (!f) { fprintf(stderr, "bench_stage: cannot write %s\n", argv[4]); return 2; }
    for (int i = 0; i < g_ndump; ++i)
      if (fwrite(g_dptr[i], 1, g_dlen[i], f) != g_dlen[i])
      { fprintf(stderr, "bench_stage: short write\n"); fclose(f); return 2; }
    fclose(f);
    return 0;
  }
  // Two timing modes over one measurement apparatus:
  //   bench      the scored measurement: the SAME input every repetition, so
  //              reference and candidate are compared on identical content.
  //   benchvary  the same measurement with the input re-drawn between
  //              repetitions (see the per-repetition variation block above).
  //              Its result is not scored; it exists so the verifier can see
  //              whether a claimed speedup survives when the repetitions stop
  //              being identical.  A kernel that memoises behind the entry
  //              point is fast in `bench` and ordinary here; a kernel that
  //              does the work is the same speed in both.
  if (mode != "bench" && mode != "benchvary") {
    fprintf(stderr, "bench_stage: unknown mode %s\n", mode.c_str()); return 2;
  }
  const bool vary_inputs = (mode == "benchvary");

  // ---- timing: pilot to size R, then R timed calls, report the median ----
  // Aim for ~50 ms of measured work: the top of the 5..50 ms range, because on
  // a shared host the longer window averages over more of the neighbours'
  // scheduler slices and the median of the run moves less between repeats.
  double TARGET_NS = 50e6;
  {   // BENCH_TARGET_MS only exists to study sensitivity to the window length
    const char* t = getenv("BENCH_TARGET_MS");
    if (t) { double v = atof(t); if (v >= 5.0 && v <= 50.0) TARGET_NS = v * 1e6; }
  }
  const int RMAX = 250000;            // keeps the fastest fnids inside 50 ms

  // The repetition number drives the input redraw: the warm-up and the pilot
  // consume fixed rep numbers, so both trees feed identical inputs up to the
  // first timed call, whatever R turns out to be.
  long rep = 0;

  g_reset(); if (vary_inputs && g_vary) g_vary(rep++); g_run();   // warm up caches and lazy init

  // Pilot: several blocks, and take the fastest block mean as the estimate of
  // the per-call cost.  On a shared machine the first iterations are the ones
  // most likely to be perturbed, and a single disturbed block would drive R
  // to the floor and leave the median with too few samples to be stable.
  const int NBLK = 3, BLKN = 128;
  double per_call = 1e30;
  for (int b = 0; b < NBLK; ++b) {
    double sum = 0.0;
    for (int i = 0; i < BLKN; ++i) {
      g_reset(); if (vary_inputs && g_vary) g_vary(rep++);
      double t0 = now_ns(); g_run(); double t1 = now_ns();
      sum += t1 - t0;
    }
    double per = sum / BLKN;
    if (per < per_call) per_call = per;
  }
  if (per_call < 1.0) per_call = 1.0;

  long R = (long)(TARGET_NS / per_call);
  if (R < 64) R = 64;
  if (R > RMAX) R = RMAX;
  if (getenv("BENCH_VERBOSE"))
    fprintf(stderr, "R %ld pilot_per_call_ns %.1f\n", R, per_call);

  uint64_t* samples = (uint64_t*)malloc(sizeof(uint64_t) * (size_t)R);
  if (!samples) { fprintf(stderr, "bench_stage: out of memory\n"); return 3; }
  double loop0 = now_ns(), tsum = 0.0;
  for (long i = 0; i < R; ++i) {
    g_reset(); if (vary_inputs && g_vary) g_vary(rep++);
    double t0 = now_ns();
    g_run();
    double t1 = now_ns();
    samples[i] = (uint64_t)(t1 - t0);
    tsum += (double)samples[i];
  }
  if (getenv("BENCH_VERBOSE")) {
    uint64_t p10 = samples[R / 10], p90 = samples[(R * 9) / 10];
    fprintf(stderr, "loop_wall_ms %.1f measured_total_ms %.1f p10 %llu p90 %llu\n",
            (now_ns() - loop0) / 1e6, tsum / 1e6,
            (unsigned long long)p10, (unsigned long long)p90);
  }
  qsort(samples, (size_t)R, sizeof(uint64_t), cmp_u64);
  uint64_t med = (R & 1) ? samples[R / 2]
                         : (samples[R / 2 - 1] + samples[R / 2]) / 2;
  printf("%llu\n", (unsigned long long)med);
  free(samples);
  return 0;
}
