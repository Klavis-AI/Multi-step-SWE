// Per-function harness for one simdutf milestone.
//
// It calls the method the library INSTALLED -- get_active_implementation()->
// <method>(...) -- in whichever tree it was compiled against, on identical
// input bytes, so timing the same function in two builds gives a like-for-like
// ratio and an agent who speeds up the portable C is measured like one who
// writes a kernel.
//
//   bench_stage <fnid> <input-file> dump <out-file>   write the result
//   bench_stage <fnid> <input-file> bench             print median ns/call
//
// dump writes the output bytes followed by the returned length and, where the
// method returns a simdutf::result, its error code and count, so the verifier
// can compare a candidate against the base build with cmp(1).

#include <simdutf.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const char *MAGIC = "SDUFDUMP";

static std::vector<char> slurp(const char *path) {
  std::vector<char> v;
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(2);
  }
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    v.insert(v.end(), buf, buf + n);
  fclose(f);
  return v;
}

// ---------------------------------------------------------------------------
// Result capture
// ---------------------------------------------------------------------------
struct Outcome {
  std::string bytes;
  uint64_t len = 0;
  uint64_t e0 = 0, e1 = 0; // result.error, result.count (0 when not applicable)
  bool has_result = false;
};

// Scratch buffers live for the lifetime of the process: allocating and
// freeing them inside the timed region would add a constant to both the
// numerator and the denominator and compress the ratio being measured.
static std::vector<char16_t> g_b16;
static std::vector<char32_t> g_b32;
static std::vector<char> g_b8;
static bool g_capture = false;

static void reserve_buffers(size_t n) {
  g_b16.resize(n + 4096);
  g_b32.resize(n + 4096);
  g_b8.resize(4 * n + 4096);
}

// Copying the output is only done when a dump is asked for, never while
// timing.
template <typename T>
static void emit_bytes(Outcome &o, const T *p, size_t n) {
  if (!g_capture)
    return;
  o.bytes.assign(reinterpret_cast<const char *>(p), n * sizeof(T));
}

// The installed implementation for this build.  Every method the harness
// calls is a const member of simdutf::implementation, so a const pointer is
// all that is needed -- and it is exactly the pointer the library installed,
// not a symbol named in this file.
static const simdutf::implementation *IMP() {
  return simdutf::get_active_implementation();
}

// ---------------------------------------------------------------------------
// One call = one switch arm.  Everything is sized generously: the harness is
// not what is being timed.
// ---------------------------------------------------------------------------
static Outcome run_once(const std::string &fn, const std::vector<char> &in) {
  Outcome o;
  const char *s = in.data();
  size_t n = in.size();
  const char16_t *u16 = reinterpret_cast<const char16_t *>(s);
  const char32_t *u32 = reinterpret_cast<const char32_t *>(s);
  size_t n16 = n / 2, n32 = n / 4;

  char16_t *b16 = g_b16.data();
  char32_t *b32 = g_b32.data();
  char *b8 = g_b8.data();

  using namespace simdutf;
  const implementation *im = IMP();

  o.has_result = true;
  if (fn == "validate_utf8") {
    o.e0 = im->validate_utf8(s, n) ? 1 : 0;
    o.has_result = false;
  } else if (fn == "validate_utf8_with_errors") {
    result r = im->validate_utf8_with_errors(s, n);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
  } else if (fn == "count_utf8") {
    o.e0 = im->count_utf8(s, n);
    o.has_result = false;
  } else if (fn == "utf16_length_from_utf8") {
    o.e0 = im->utf16_length_from_utf8(s, n);
    o.has_result = false;
  } else if (fn == "utf32_length_from_utf8") {
    o.e0 = im->utf32_length_from_utf8(s, n);
    o.has_result = false;
  } else if (fn == "convert_utf8_to_utf16le") {
    o.e0 = im->convert_utf8_to_utf16le(s, n, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf8_to_utf16be") {
    o.e0 = im->convert_utf8_to_utf16be(s, n, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf8_to_utf16le_with_errors") {
    result r = im->convert_utf8_to_utf16le_with_errors(s, n, b16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b16, r.count);
  } else if (fn == "convert_utf8_to_utf16be_with_errors") {
    result r = im->convert_utf8_to_utf16be_with_errors(s, n, b16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b16, r.count);
  } else if (fn == "convert_valid_utf8_to_utf16le") {
    o.e0 = im->convert_valid_utf8_to_utf16le(s, n, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_valid_utf8_to_utf16be") {
    o.e0 = im->convert_valid_utf8_to_utf16be(s, n, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf8_to_utf32") {
    o.e0 = im->convert_utf8_to_utf32(s, n, b32);
    emit_bytes(o, b32, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf8_to_utf32_with_errors") {
    result r = im->convert_utf8_to_utf32_with_errors(s, n, b32);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b32, r.count);
  } else if (fn == "convert_valid_utf8_to_utf32") {
    o.e0 = im->convert_valid_utf8_to_utf32(s, n, b32);
    emit_bytes(o, b32, o.e0);
    o.has_result = false;
  }
  else if (fn == "convert_utf16le_to_utf8") {
    o.e0 = im->convert_utf16le_to_utf8(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16be_to_utf8") {
    o.e0 = im->convert_utf16be_to_utf8(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16le_to_utf8_with_errors") {
    result r = im->convert_utf16le_to_utf8_with_errors(u16, n16, b8);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "convert_utf16be_to_utf8_with_errors") {
    result r = im->convert_utf16be_to_utf8_with_errors(u16, n16, b8);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "convert_valid_utf16le_to_utf8") {
    o.e0 = im->convert_valid_utf16le_to_utf8(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_valid_utf16be_to_utf8") {
    o.e0 = im->convert_valid_utf16be_to_utf8(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16le_to_utf8_with_replacement") {
    o.e0 = im->convert_utf16le_to_utf8_with_replacement(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16be_to_utf8_with_replacement") {
    o.e0 = im->convert_utf16be_to_utf8_with_replacement(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "utf8_length_from_utf16le") {
    o.e0 = im->utf8_length_from_utf16le(u16, n16);
    o.has_result = false;
  } else if (fn == "utf8_length_from_utf16be") {
    o.e0 = im->utf8_length_from_utf16be(u16, n16);
    o.has_result = false;
  } else if (fn == "utf8_length_from_utf16le_with_replacement") {
    result r = im->utf8_length_from_utf16le_with_replacement(u16, n16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
  } else if (fn == "utf8_length_from_utf16be_with_replacement") {
    result r = im->utf8_length_from_utf16be_with_replacement(u16, n16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
  }
  else if (fn == "convert_utf32_to_utf8") {
    o.e0 = im->convert_utf32_to_utf8(u32, n32, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf32_to_utf8_with_errors") {
    result r = im->convert_utf32_to_utf8_with_errors(u32, n32, b8);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "convert_valid_utf32_to_utf8") {
    o.e0 = im->convert_valid_utf32_to_utf8(u32, n32, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "utf8_length_from_utf32") {
    o.e0 = im->utf8_length_from_utf32(u32, n32);
    o.has_result = false;
  }
  else if (fn == "convert_utf16le_to_utf32") {
    o.e0 = im->convert_utf16le_to_utf32(u16, n16, b32);
    emit_bytes(o, b32, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16be_to_utf32") {
    o.e0 = im->convert_utf16be_to_utf32(u16, n16, b32);
    emit_bytes(o, b32, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16le_to_utf32_with_errors") {
    result r = im->convert_utf16le_to_utf32_with_errors(u16, n16, b32);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b32, r.count);
  } else if (fn == "convert_utf16be_to_utf32_with_errors") {
    result r = im->convert_utf16be_to_utf32_with_errors(u16, n16, b32);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b32, r.count);
  } else if (fn == "convert_valid_utf16le_to_utf32") {
    o.e0 = im->convert_valid_utf16le_to_utf32(u16, n16, b32);
    emit_bytes(o, b32, o.e0);
    o.has_result = false;
  } else if (fn == "convert_valid_utf16be_to_utf32") {
    o.e0 = im->convert_valid_utf16be_to_utf32(u16, n16, b32);
    emit_bytes(o, b32, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf32_to_utf16le") {
    o.e0 = im->convert_utf32_to_utf16le(u32, n32, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf32_to_utf16be") {
    o.e0 = im->convert_utf32_to_utf16be(u32, n32, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf32_to_utf16le_with_errors") {
    result r = im->convert_utf32_to_utf16le_with_errors(u32, n32, b16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b16, r.count);
  } else if (fn == "convert_utf32_to_utf16be_with_errors") {
    result r = im->convert_utf32_to_utf16be_with_errors(u32, n32, b16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b16, r.count);
  } else if (fn == "convert_valid_utf32_to_utf16le") {
    o.e0 = im->convert_valid_utf32_to_utf16le(u32, n32, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_valid_utf32_to_utf16be") {
    o.e0 = im->convert_valid_utf32_to_utf16be(u32, n32, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "utf32_length_from_utf16le") {
    o.e0 = im->utf32_length_from_utf16le(u16, n16);
    o.has_result = false;
  } else if (fn == "utf32_length_from_utf16be") {
    o.e0 = im->utf32_length_from_utf16be(u16, n16);
    o.has_result = false;
  } else if (fn == "utf16_length_from_utf32") {
    o.e0 = im->utf16_length_from_utf32(u32, n32);
    o.has_result = false;
  } else if (fn == "change_endianness_utf16") {
    im->change_endianness_utf16(u16, n16, b16);
    o.e0 = n16;
    emit_bytes(o, b16, n16);
    o.has_result = false;
  }
  else if (fn == "convert_latin1_to_utf8") {
    o.e0 = im->convert_latin1_to_utf8(s, n, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_latin1_to_utf16le") {
    o.e0 = im->convert_latin1_to_utf16le(s, n, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_latin1_to_utf16be") {
    o.e0 = im->convert_latin1_to_utf16be(s, n, b16);
    emit_bytes(o, b16, o.e0);
    o.has_result = false;
  } else if (fn == "convert_latin1_to_utf32") {
    o.e0 = im->convert_latin1_to_utf32(s, n, b32);
    emit_bytes(o, b32, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf8_to_latin1") {
    o.e0 = im->convert_utf8_to_latin1(s, n, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf8_to_latin1_with_errors") {
    result r = im->convert_utf8_to_latin1_with_errors(s, n, b8);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "convert_valid_utf8_to_latin1") {
    o.e0 = im->convert_valid_utf8_to_latin1(s, n, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16le_to_latin1") {
    o.e0 = im->convert_utf16le_to_latin1(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16be_to_latin1") {
    o.e0 = im->convert_utf16be_to_latin1(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf16le_to_latin1_with_errors") {
    result r = im->convert_utf16le_to_latin1_with_errors(u16, n16, b8);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "convert_utf16be_to_latin1_with_errors") {
    result r = im->convert_utf16be_to_latin1_with_errors(u16, n16, b8);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "convert_valid_utf16le_to_latin1") {
    o.e0 = im->convert_valid_utf16le_to_latin1(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_valid_utf16be_to_latin1") {
    o.e0 = im->convert_valid_utf16be_to_latin1(u16, n16, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf32_to_latin1") {
    o.e0 = im->convert_utf32_to_latin1(u32, n32, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "convert_utf32_to_latin1_with_errors") {
    result r = im->convert_utf32_to_latin1_with_errors(u32, n32, b8);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "convert_valid_utf32_to_latin1") {
    o.e0 = im->convert_valid_utf32_to_latin1(u32, n32, b8);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "latin1_length_from_utf8") {
    o.e0 = im->latin1_length_from_utf8(s, n);
    o.has_result = false;
  } else if (fn == "utf8_length_from_latin1") {
    o.e0 = im->utf8_length_from_latin1(s, n);
    o.has_result = false;
  }
  else if (fn == "base64_to_binary") {
    result r = im->base64_to_binary(s, n, b8, base64_default);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "base64_to_binary_details") {
    full_result r = im->base64_to_binary_details(s, n, b8, base64_default);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.output_count;
    emit_bytes(o, b8, r.output_count);
  } else if (fn == "base64_to_binary_utf16") {
    result r = im->base64_to_binary(u16, n16, b8, base64_default);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
    emit_bytes(o, b8, r.count);
  } else if (fn == "binary_to_base64") {
    o.e0 = im->binary_to_base64(s, n, b8, base64_default);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "binary_to_base64_with_lines") {
    o.e0 = im->binary_to_base64_with_lines(s, n, b8, 72, base64_default);
    emit_bytes(o, b8, o.e0);
    o.has_result = false;
  } else if (fn == "binary_length_from_base64") {
    o.e0 = im->binary_length_from_base64(s, n);
    o.has_result = false;
  }
  else if (fn == "validate_ascii") {
    o.e0 = im->validate_ascii(s, n) ? 1 : 0;
    o.has_result = false;
  } else if (fn == "validate_ascii_with_errors") {
    result r = im->validate_ascii_with_errors(s, n);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
  } else if (fn == "validate_utf16le") {
    o.e0 = im->validate_utf16le(u16, n16) ? 1 : 0;
    o.has_result = false;
  } else if (fn == "validate_utf16be") {
    o.e0 = im->validate_utf16be(u16, n16) ? 1 : 0;
    o.has_result = false;
  } else if (fn == "validate_utf16le_with_errors") {
    result r = im->validate_utf16le_with_errors(u16, n16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
  } else if (fn == "validate_utf16be_with_errors") {
    result r = im->validate_utf16be_with_errors(u16, n16);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
  } else if (fn == "to_well_formed_utf16le") {
    im->to_well_formed_utf16le(u16, n16, b16);
    o.e0 = n16;
    emit_bytes(o, b16, n16);
    o.has_result = false;
  } else if (fn == "to_well_formed_utf16be") {
    im->to_well_formed_utf16be(u16, n16, b16);
    o.e0 = n16;
    emit_bytes(o, b16, n16);
    o.has_result = false;
  } else if (fn == "validate_utf32") {
    o.e0 = im->validate_utf32(u32, n32) ? 1 : 0;
    o.has_result = false;
  } else if (fn == "validate_utf32_with_errors") {
    result r = im->validate_utf32_with_errors(u32, n32);
    o.e0 = (uint64_t)r.error;
    o.e1 = r.count;
  } else if (fn == "count_utf16le") {
    o.e0 = im->count_utf16le(u16, n16);
    o.has_result = false;
  } else if (fn == "count_utf16be") {
    o.e0 = im->count_utf16be(u16, n16);
    o.has_result = false;
  } else if (fn == "find") {
    const char *p = im->find(s, s + n, '\x01');
    o.e0 = (uint64_t)(p - s);
    o.has_result = false;
  } else {
    fprintf(stderr, "unknown fnid %s\n", fn.c_str());
    exit(3);
  }
  o.len = o.e0;
  return o;
}

static void write_dump(const Outcome &o, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "cannot write %s\n", path);
    exit(2);
  }
  uint64_t hdr[3] = {o.len, o.e0, o.e1};
  fwrite(MAGIC, 1, 8, f);
  fwrite(hdr, sizeof hdr, 1, f);
  fwrite(o.bytes.data(), 1, o.bytes.size(), f);
  fclose(f);
}

int main(int argc, char **argv) {
  // The mechanistic check: the name the library reports for the
  // implementation it installed, so a run that silently fell back to the
  // portable path is visible rather than merely slow.
  if (argc == 2 && std::string(argv[1]) == "impl") {
    std::string nm(IMP()->name());
    printf("%s\n", nm.c_str());
    return 0;
  }
  if (argc < 4) {
    fprintf(stderr, "usage: %s <fnid> <input> dump <out> | bench [reps]\n", argv[0]);
    return 2;
  }
  std::string fn = argv[1];
  std::vector<char> in = slurp(argv[2]);
  std::string mode = argv[3];

  reserve_buffers(in.size());

  if (mode == "dump") {
    if (argc < 5)
      return 2;
    g_capture = true;
    write_dump(run_once(fn, in), argv[4]);
    return 0;
  }

  // bench1: exactly one call, no warm-up.  The verifier uses this so that a
  // function is never timed on input it has already been given in this
  // container -- a memo behind the seam has nothing to hit.
  if (mode == "bench1") {
    auto a = std::chrono::steady_clock::now();
    Outcome o = run_once(fn, in);
    auto b = std::chrono::steady_clock::now();
    printf("%.0f\n", std::chrono::duration<double, std::nano>(b - a).count());
    (void)o;
    return 0;
  }

  int reps = (argc > 4) ? atoi(argv[4]) : 9;
  for (int i = 0; i < 2; i++)
    run_once(fn, in); // warm up

  std::vector<double> t;
  volatile uint64_t sink = 0;
  for (int r = 0; r < reps; r++) {
    auto a = std::chrono::steady_clock::now();
    Outcome o = run_once(fn, in);
    auto b = std::chrono::steady_clock::now();
    sink += o.e0 + o.len + o.e1 + (uint64_t)o.bytes.size();
    t.push_back(std::chrono::duration<double, std::nano>(b - a).count());
  }
  std::sort(t.begin(), t.end());
  printf("%.0f\n", t[t.size() / 2]);
  (void)sink;
  return 0;
}
