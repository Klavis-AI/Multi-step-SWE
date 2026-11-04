//***************************************************************************/
// gen_fixtures.cpp -- build the codeblock decode fixtures used by
// bench_stage's decode_cb32.
//
//   gen_fixtures <outdir>
//
// Compiled against L0 (the base tree).  For each (w, h, seed) triple a
// 32-bit irreversible codeblock is generated, encoded with the installed
// cb.encode_cb32 pointer and written out as:
//
//   bytes 0..63 : "HB1 <w> <h> <stride> <missing_msbs> <len1> <len2> <np>\n"
//                 NUL padded to offset 64
//   bytes 64..  : len1 + len2 raw coded bytes, exactly as coded_lists::buf
//                 holds them, i.e. starting at buf[0] and NOT at
//                 buf + coded_cb_header::prefix_buf_size
//
// One line per fixture is printed: "<path> <w> <h> <lengths1>"
//***************************************************************************/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <algorithm>

#include "ojph_defs.h"
#include "ojph_mem.h"
#include "ojph_file.h"
#include "ojph_params.h"
#include "ojph_params_local.h"
#include "ojph_codeblock_fun.h"
#include "ojph_codeblock.h"

using namespace ojph;
using namespace ojph::local;

static uint64_t g_rs = 1;
static uint64_t rn()
{
  uint64_t x = g_rs;
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
  g_rs = x;
  return x * 0x2545F4914F6CDD1DULL;
}
static void seed_rng(uint64_t s) { g_rs = s * 2654435761u + 1u; if (g_rs == 0) g_rs = 1; }

struct Triple { ui32 w, h; uint64_t seed; };

int main(int argc, char** argv)
{
  if (argc < 2) { fprintf(stderr, "usage: %s <outdir>\n", argv[0]); return 2; }
  std::string dir = argv[1];
  if (!dir.empty() && dir[dir.size() - 1] == '/') dir.erase(dir.size() - 1);

  static const Triple triples[] = {
    { 64, 64, 1 }, { 64, 64, 2 }, { 32, 32, 3 }, { 128, 16, 4 },
    { 16, 128, 5 }, { 4, 4, 6 }, { 64, 64, 7 }, { 96, 64, 8 }
  };
  const int n = (int)(sizeof(triples) / sizeof(triples[0]));

  codeblock_fun cb;
  cb.init(false);                 // 32-bit / irreversible

  mem_elastic_allocator elastic(4096);
  const ui32 num_passes = 1;

  for (int t = 0; t < n; ++t) {
    ui32 w = triples[t].w, h = triples[t].h, stride = triples[t].w;
    size_t nmemb = (size_t)stride * h;
    seed_rng(triples[t].seed);

    ui32* src = NULL;
    if (posix_memalign((void**)&src, 64, nmemb * 4) != 0) return 3;
    memset(src, 0, nmemb * 4);

    // Same shape as probe_cb.cpp: roughly a quarter zeros and a geometric
    // magnitude spread -- what a wavelet subband presents to the block coder.
    ui32 maxv = 0;
    for (size_t i = 0; i < nmemb; ++i) {
      uint64_t r = rn();
      ui32 mag;
      if ((r & 3u) == 0) mag = 0;
      else {
        ui32 e = (ui32)((r >> 2) & 7u);
        mag = (1u << e) | (ui32)((r >> 5) & ((1u << e) - 1u));
      }
      src[i] = mag;
      if (mag > maxv) maxv = mag;
    }
    // K_max from the coefficient bit length plus 2, missing_msbs = K_max - 1,
    // and the coefficients shifted up so the block is densely significant.
    ui32 bits = 1;
    if (maxv) { while ((maxv >> bits) != 0 && bits < 30) ++bits; }
    ui32 K_max = bits + 2; if (K_max < 3) K_max = 3; if (K_max > 31) K_max = 31;
    ui32 shift = 32u - bits;
    for (size_t i = 0; i < nmemb; ++i) src[i] <<= shift;
    ui32 missing_msbs = K_max - 1;

    elastic.~mem_elastic_allocator();
    new (&elastic) mem_elastic_allocator(4096);
    coded_lists* coded = NULL;
    ui32 lengths[2] = { 0, 0 };

    cb.encode_cb32(src, missing_msbs, num_passes, w, h, stride,
                   lengths, &elastic, coded);

    if (!coded || lengths[0] == 0) {
      fprintf(stderr, "gen_fixtures: %ux%u seed %llu produced no output\n",
              w, h, (unsigned long long)triples[t].seed);
      return 4;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/cb_%ux%u_s%llu.bin", dir.c_str(), w, h,
             (unsigned long long)triples[t].seed);

    ui8 hdr[64];
    memset(hdr, 0, sizeof(hdr));
    int hn = snprintf((char*)hdr, sizeof(hdr),
                      "HB1 %u %u %u %u %u %u %u\n",
                      w, h, stride, missing_msbs, lengths[0], lengths[1],
                      num_passes);
    if (hn <= 0 || hn >= 64) { fprintf(stderr, "gen_fixtures: header overflow\n"); return 4; }

    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "gen_fixtures: cannot write %s\n", path); return 4; }
    size_t nbytes = (size_t)lengths[0] + lengths[1];
    if (fwrite(hdr, 1, 64, f) != 64 ||
        fwrite(coded->buf, 1, nbytes, f) != nbytes) {
      fprintf(stderr, "gen_fixtures: short write on %s\n", path);
      fclose(f); return 4;
    }
    fclose(f);
    printf("%s %u %u %u\n", path, w, h, lengths[0]);

    free(src);
  }
  return 0;
}
