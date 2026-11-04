// End-to-end gate: drive the whole transcoding contract through the public API
// and hand back a digest per stage, plus one wall-clock figure for the lot.
//
//   pipeline <corpus-dir> digest   one "stageN <hex>" line per operation
//   pipeline <corpus-dir> time     one number, milliseconds, lower is better
//
// Everything goes through simdutf's free functions, which route at whichever
// implementation the dispatcher installed, so this checks glue, dispatch and
// cross-family state that a per-function harness cannot see.  A digest is a
// non-cryptographic FNV-1a over the output bytes and the returned result
// structs; equality here is the same equality the correctness gate uses.

#include <simdutf.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
  const unsigned char *b = (const unsigned char *)p;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 1099511628211ULL;
  }
  return h;
}

static std::vector<char> slurp(const std::string &path) {
  std::vector<char> v;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return v;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    v.insert(v.end(), buf, buf + n);
  fclose(f);
  return v;
}

struct Sheet {
  std::vector<uint64_t> rows;
  void add(uint64_t h) { rows.push_back(h); }
  void add_res(const simdutf::result &r) {
    uint64_t h = 1469598103934665603ULL;
    h = fnv(h, &r.error, sizeof r.error);
    h = fnv(h, &r.count, sizeof r.count);
    add(h);
  }
};

static void run(const std::string &dir, Sheet &sh) {
  using namespace simdutf;
  std::vector<char> u8 = slurp(dir + "/utf8.txt");
  std::vector<char> u8e = slurp(dir + "/utf8err.txt");
  std::vector<char> u8l = slurp(dir + "/utf8latin1.txt");
  std::vector<char> a16b = slurp(dir + "/utf16le.bin");
  std::vector<char> a16be = slurp(dir + "/utf16be.bin");
  std::vector<char> u16bad = slurp(dir + "/utf16bad.bin");
  std::vector<char> u32 = slurp(dir + "/utf32le.bin");
  std::vector<char> lat = slurp(dir + "/latin1.bin");
  std::vector<char> asc = slurp(dir + "/ascii.txt");
  std::vector<char> bin = slurp(dir + "/bin.bin");
  std::vector<char> b64 = slurp(dir + "/base64.txt");
  std::vector<char> ndl = slurp(dir + "/findneedle.txt");

  size_t big = u8.size() + u32.size() + 4096;
  std::vector<char> o8(big * 4 + 64);
  std::vector<char16_t> o16(big + 64);
  std::vector<char32_t> o32(big + 64);

  const char *s8 = u8.data();
  size_t n8 = u8.size();
  const char16_t *p16 = (const char16_t *)a16b.data();
  size_t n16 = a16b.size() / 2;
  const char16_t *q16 = (const char16_t *)a16be.data();
  const char32_t *p32 = (const char32_t *)u32.data();
  size_t n32 = u32.size() / 4;

  // 1 — UTF-8 validation and the UTF-8 -> UTF-16/32 surface
  sh.add(validate_utf8(s8, n8) ? 1 : 0);
  sh.add_res(validate_utf8_with_errors(s8, n8));
  sh.add_res(validate_utf8_with_errors(u8e.data(), u8e.size()));
  sh.add(count_utf8(s8, n8));
  sh.add(utf16_length_from_utf8(s8, n8));
  sh.add(utf32_length_from_utf8(s8, n8));
  {
    size_t k = convert_utf8_to_utf16le(s8, n8, o16.data());
    sh.add(fnv(1469598103934665603ULL, o16.data(), k * 2));
    sh.add_res(convert_utf8_to_utf16le_with_errors(s8, n8, o16.data()));
    k = convert_utf8_to_utf16be(s8, n8, o16.data());
    sh.add(fnv(1469598103934665603ULL, o16.data(), k * 2));
    k = convert_valid_utf8_to_utf16le(s8, n8, o16.data());
    sh.add(fnv(1469598103934665603ULL, o16.data(), k * 2));
    k = convert_utf8_to_utf32(s8, n8, o32.data());
    sh.add(fnv(1469598103934665603ULL, o32.data(), k * 4));
    sh.add_res(convert_utf8_to_utf32_with_errors(s8, n8, o32.data()));
    k = convert_valid_utf8_to_utf32(s8, n8, o32.data());
    sh.add(fnv(1469598103934665603ULL, o32.data(), k * 4));
  }

  // 2 — UTF-16 -> UTF-8, both endiannesses
  {
    size_t k = convert_utf16le_to_utf8(p16, n16, o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    sh.add_res(convert_utf16le_to_utf8_with_errors(p16, n16, o8.data()));
    k = convert_valid_utf16le_to_utf8(p16, n16, o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    k = convert_utf16le_to_utf8_with_replacement(p16, n16, o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    sh.add(utf8_length_from_utf16le(p16, n16));
    sh.add_res(utf8_length_from_utf16le_with_replacement(p16, n16));
  }

  // 3 — UTF-32 -> UTF-8
  {
    size_t k = convert_utf32_to_utf8(p32, n32, o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    k = convert_valid_utf32_to_utf8(p32, n32, o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    sh.add(utf8_length_from_utf32(p32, n32));
  }

  // 4 — UTF-16 <-> UTF-32
  {
    size_t k = convert_utf16le_to_utf32(p16, n16, o32.data());
    sh.add(fnv(1469598103934665603ULL, o32.data(), k * 4));
    k = convert_utf32_to_utf16be(p32, n32, o16.data());
    sh.add(fnv(1469598103934665603ULL, o16.data(), k * 2));
    k = convert_utf32_to_utf16le(p32, n32, o16.data());
    sh.add(fnv(1469598103934665603ULL, o16.data(), k * 2));
    sh.add(utf32_length_from_utf16le(p16, n16));
    sh.add(utf16_length_from_utf32(p32, n32));
  }

  // 5 — Latin-1, all six directions
  {
    size_t k = convert_latin1_to_utf8(lat.data(), lat.size(), o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    k = convert_latin1_to_utf16le(lat.data(), lat.size(), o16.data());
    sh.add(fnv(1469598103934665603ULL, o16.data(), k * 2));
    k = convert_latin1_to_utf32(lat.data(), lat.size(), o32.data());
    sh.add(fnv(1469598103934665603ULL, o32.data(), k * 4));
    k = convert_utf8_to_latin1(u8l.data(), u8l.size(), o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    sh.add_res(convert_utf8_to_latin1_with_errors(u8l.data(), u8l.size(), o8.data()));
    k = convert_utf16le_to_latin1(p16, n16, o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    k = convert_utf32_to_latin1(p32, n32, o8.data());
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    sh.add(latin1_length_from_utf8(u8l.data(), u8l.size()));
    sh.add(utf8_length_from_latin1(lat.data(), lat.size()));
  }

  // 6 — base64, both directions
  {
    size_t k = binary_to_base64(bin.data(), bin.size(), o8.data(), base64_default);
    sh.add(fnv(1469598103934665603ULL, o8.data(), k));
    sh.add_res(base64_to_binary(b64.data(), b64.size(), o8.data(), base64_default));
    sh.add(binary_length_from_base64(b64.data(), b64.size()));
  }

  // 7 — validation, repair and search
  {
    sh.add(validate_ascii(asc.data(), asc.size()) ? 1 : 0);
    sh.add_res(validate_ascii_with_errors(asc.data(), asc.size()));
    sh.add(validate_utf16le(p16, n16) ? 1 : 0);
    sh.add(validate_utf16be(q16, a16be.size() / 2) ? 1 : 0);
    sh.add_res(validate_utf16le_with_errors(p16, n16));
    sh.add(validate_utf32(p32, n32) ? 1 : 0);
    sh.add_res(validate_utf32_with_errors(p32, n32));
    to_well_formed_utf16le((const char16_t *)u16bad.data(), u16bad.size() / 2,
                           o16.data());
    sh.add(fnv(1469598103934665603ULL, o16.data(), u16bad.size()));
    sh.add(count_utf16le(p16, n16));
    sh.add(count_utf16be(q16, a16be.size() / 2));
    const char *hit = simdutf::find(ndl.data(), ndl.data() + ndl.size(), '\x01');
    sh.add((uint64_t)(hit - ndl.data()));
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: pipeline <corpus-dir> digest|time [reps]\n");
    return 2;
  }
  std::string dir = argv[1], mode = argv[2];
  if (mode == "digest") {
    Sheet sh;
    run(dir, sh);
    for (size_t i = 0; i < sh.rows.size(); i++)
      printf("stage%02zu %016llx\n", i + 1, (unsigned long long)sh.rows[i]);
    return 0;
  }
  int reps = (argc > 3) ? atoi(argv[3]) : 3;
  Sheet sh;
  run(dir, sh); // warm
  std::vector<double> t;
  for (int r = 0; r < reps; r++) {
    auto a = std::chrono::steady_clock::now();
    Sheet s2;
    run(dir, s2);
    auto b = std::chrono::steady_clock::now();
    t.push_back(std::chrono::duration<double, std::milli>(b - a).count());
  }
  std::sort(t.begin(), t.end());
  printf("%.3f\n", t[t.size() / 2]);
  return 0;
}
