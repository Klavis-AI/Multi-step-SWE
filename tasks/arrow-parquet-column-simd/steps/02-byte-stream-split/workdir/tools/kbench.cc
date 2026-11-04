// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// ---------------------------------------------------------------------------
// kbench -- scope-restricted build of the per-function measurement harness.
//
// Only the functions in this scope are registered, so `all` selects exactly
// them.
//
//   kbench dump    <seed> <fnid> [<fnid> ...]   ->  "<label> <hex64>"
//   kbench bench   <seed> <fnid> [<fnid> ...]   ->  "<label> <ns_per_unit> <hex64>"
//   kbench samples <seed> <fnid> [<fnid> ...]   ->  "<label> <n> <reps> <sorted samples>"
//
// Every measurement goes through "the implementation the library actually
// installed": the harness calls the library's own entry point (the runtime
// dispatcher, or the public/internal function that takes the `hardware_flags`
// argument) and never a named scalar or SIMD symbol, so a portable
// optimisation is measured exactly like an intrinsics one.
//
// A function whose arm is chosen by an explicit `hardware_flags` argument is
// reported as `<fnid>@simd`, with the flags word the library itself uses on
// this host.  A function that dispatches internally is reported as the bare
// `<fnid>`; running this binary with ARROW_USER_SIMD_LEVEL=NONE set before
// exec reports those as `<fnid>@scalar` instead (the dispatcher resolves the
// level once per process, so the variable cannot be flipped mid-run).
//
// Inputs come from <seed> alone (splitmix64) and are identical in every process
// and in every argument order.  Timing auto-calibrates the inner repetition
// count so one sample is >= 20 ms and reports the MEDIAN of 15 samples; the
// full min/median/max spread goes to stderr.  Output is consumed through a
// volatile sink after a compiler barrier on every iteration, so a call cannot
// be hoisted out of the timed loop, and every line carries a result checksum.
// Every timed call is also handed a fresh byte of input (see `Perturber`), so a
// kernel that memoises on its input or on the buffer address cannot report the
// cache's cost instead of the kernel's; `dump` makes four such calls and
// checksums the last, so a stale answer fails the bit-exactness comparison.
// ---------------------------------------------------------------------------

//
//
// Inputs are generated from <seed> alone (splitmix64), so every fnid sees the
// same logical input in every process and in every argument order.  Timing
// auto-calibrates the inner repetition count so that one sample is >= 20 ms and
// reports the MEDIAN of kSamples samples; the full min/median/max spread goes to
// stderr.  Output is consumed through a volatile sink after a compiler barrier
// on every iteration, so a call cannot be hoisted out of the timed loop.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "arrow/api.h"
#include "arrow/array/builder_binary.h"
#include "arrow/util/bit_util.h"
#include "arrow/util/bitmap_writer.h"
#include "arrow/util/byte_stream_split_internal.h"
#include "arrow/util/cpu_info.h"
#include "arrow/util/macros.h"


namespace {

// ---------------------------------------------------------------------------
// build-time knobs (defaults are the normal measurement settings)
// ---------------------------------------------------------------------------

// Multiplies the batch size of every fixture.  Default 1.  Recompiling with
// -DKBENCH_SIZE_SCALE=2 is how the "ns/unit is stable when the batch doubles"
// anti-hoisting check is run; it is never needed for a normal measurement.
#ifndef KBENCH_SIZE_SCALE
#  define KBENCH_SIZE_SCALE 1
#endif

// Compiling with -DKBENCH_NO_CONSUME removes the post-call compiler barrier and
// the volatile consumption, which is how the "the number collapses when the
// consumption is removed" check is run.  Never build the shipped artifact with
// it.
#if defined(KBENCH_NO_CONSUME)
#  define KBENCH_CONSUME 0
#else
#  define KBENCH_CONSUME 1
#endif

constexpr int kScale = KBENCH_SIZE_SCALE;
constexpr int kLog2Scale = kScale <= 1 ? 0 : kScale <= 2 ? 1 : kScale <= 4 ? 2 : kScale <= 8 ? 3 : 4;

// Fixture sizes.
constexpr int64_t kBssValues = 16384 * kScale;     // byte-stream-split: values per call
constexpr int kSwissLogBlocks = 4;                 // 16 blocks / 128 slots

// Timing.
//
// Both knobs are compile-time overridable so a caller can measure the same
// kernels on the budget a measurement can afford (e.g. 15 samples of ~10 ms):
//
//   -DKBENCH_TARGET_NS=10000000.0 -DKBENCH_SAMPLES=15
//
// The shipped defaults are one sample >= 20 ms, median of 15.
#ifndef KBENCH_TARGET_NS
#  define KBENCH_TARGET_NS 20000000.0
#endif
#ifndef KBENCH_SAMPLES
#  define KBENCH_SAMPLES 15
#endif
constexpr double kTargetSampleNs = KBENCH_TARGET_NS;  // one sample must be >= 20 ms
// At least 7 samples are required and the MEDIAN is what gets printed; 15 is
// used because it makes the median materially more robust when the host is
// oversubscribed (the min/median/max spread is reported on stderr either way).
constexpr int kSamples = KBENCH_SAMPLES;
constexpr int kMaxRecalibrations = 12;
// Calls a `dump` makes per function before checksumming.  More than one so that
// the dump is not a free pass for a kernel that memoises on its input.
constexpr int kDumpCalls = 4;

// ---------------------------------------------------------------------------
// deterministic PRNG
// ---------------------------------------------------------------------------

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}

  uint64_t Next() {  // splitmix64
    uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  uint32_t Next32() { return static_cast<uint32_t>(Next() >> 32); }
  uint64_t Below(uint64_t n) { return n == 0 ? 0 : Next() % n; }
};

// A fnid-dependent seed: the same seed gives the same fixture for a fnid no
// matter which order the fnids appear on the command line.
uint64_t FnidSeed(uint64_t seed, const std::string& fnid) {
  uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a over the fnid
  for (unsigned char c : fnid) {
    h ^= c;
    h *= 0x100000001b3ULL;
  }
  return (seed * 0x9E3779B97F4A7C15ULL) ^ h;
}

// ---------------------------------------------------------------------------
// anti-hoisting: consumption + compiler barrier
// ---------------------------------------------------------------------------

volatile uint64_t g_sink = 0;

// Barrier + consume a memory region.  The barrier has a "memory" clobber, so
// the compiler may neither reorder the kernel's stores before it nor cache the
// loaded words across it; the XOR into the volatile sink makes the loads
// observable.  Together they stop the call from being hoisted out of the timed
// loop or deleted.
inline void ConsumeBytes(const void* p, size_t n) {
#if KBENCH_CONSUME
  asm volatile("" ::: "memory");
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint64_t acc = 0;
  const size_t words = n / 8;
  if (words >= 8) {
    const size_t quarter = words / 4;
    for (int i = 0; i < 4; ++i) {
      uint64_t v;
      std::memcpy(&v, b + static_cast<size_t>(i) * quarter * 8, 8);
      acc ^= v;
    }
    uint64_t v;
    std::memcpy(&v, b + (words - 1) * 8, 8);
    acc ^= v;
  } else {
    for (size_t i = 0; i + 8 <= n; i += 8) {
      uint64_t v;
      std::memcpy(&v, b + i, 8);
      acc ^= v;
    }
  }
  g_sink = g_sink ^ acc;
#else
  (void)p;
  (void)n;
#endif
}

// Barrier + consume a scalar result.  Used for kernels that return a value
// rather than filling a buffer; a local accumulator folded into the volatile
// sink once per call keeps the cost comparable to ConsumeBytes().
inline void ConsumeScalar(uint64_t v) {
#if KBENCH_CONSUME
  asm volatile("" ::: "memory");
  g_sink = g_sink ^ v;
#else
  (void)v;
#endif
}

// ---------------------------------------------------------------------------
// anti-memoisation: fresh input bytes for every timed call
// ---------------------------------------------------------------------------

// The timing loop calls a kernel thousands of times per sample and takes many
// samples per process.  Without this, every one of those calls would process the
// SAME input, so a candidate whose kernel memoises -- a cache keyed on the input
// bytes, or on the buffer address, which never changes within a process -- would
// pay the kernel's cost once and report the cache's cost for every measurement
// after it.  Perturb() is called immediately before every timed call (and before
// every call of a `dump`).
//
// Three bytes change per call: the first byte of the input, and two more at
// cursors that walk the buffer and cycle through its head.  One byte is not
// enough: a cache that probes a cheap fixed prefix of the buffer would see the
// same bytes for thousands of calls and answer from its cache in between.  Two
// leave only a smaller cache; three leave a cache that pays for its probe and
// gains nothing.
//
// The mechanism is cheap enough to sit inside the timed region: three XOR
// stores, two compares and two LCG steps, well under the ConsumeBytes()
// barrier every Run() already pays.  It is applied identically to the base and
// the candidate side of every ratio, and the schedule is a pure function of a
// call counter, so both sides feed their kernels the same sequence of inputs
// and the dump checksums still line up.
//
// Families whose input cannot be perturbed byte-wise (the row decoders read an
// encoded row table whose length and offset fields are structural) perturb a
// payload byte of that table, or of the destination offsets derived from it, at
// the same cursors -- see each bench's Perturb().
//
// The one shape this cannot cover is a probe deliberately built against this
// file: one that reads most of the input but skips the bytes the cursors write.
// It stays bit-exact, so no checksum can see it, and its win is bounded by how
// much of the input it skips, because reading nearly all of it costs about what
// the kernel's own read costs.  No in-process perturbation can close that, and
// this one does not claim to.
struct Perturber {
  static constexpr size_t kHeadBytes = 64;
  size_t pos = 0;   // walks the whole buffer, one byte per call
  size_t head = 0;  // cycles inside [0, min(kHeadBytes, n))
  uint64_t state = 0x243F6A8885A308D3ULL;

  // A byte from a full-period LCG: never the same twice in a row, and the
  // sequence written at any one position does not repeat for 2^64 calls.
  uint8_t Value() {
    state = state * 0x5851F42D4C957F2DULL + 0x14057B7EF767814FULL;
    return static_cast<uint8_t>(state >> 58);
  }

  // The three positions this call perturbs and the bytes to XOR in: `a` is the
  // first element of the input (every call), `b` the walking cursor, `c` the
  // head cursor.  Callers whose elements are wider than a byte, or whose values
  // must stay in range, take these and apply the writes themselves.
  struct Step {
    size_t a, b, c;
    uint8_t va, vb, vc;
  };
  Step Next(size_t n) {
    if (n == 0) return Step{0, 0, 0, 0, 0, 0};
    if (pos >= n) pos = 0;
    const size_t h = head < n ? head : 0;
    const uint8_t va = Value(), vb = Value();
    const Step s = {0, pos, h, va, vb, static_cast<uint8_t>(va ^ vb)};
    if (++pos == n) pos = 0;
    if (++head >= kHeadBytes || head >= n) head = 0;
    return s;
  }

  // Perturb a byte buffer at all three cursors.
  void Walk(uint8_t* data, size_t n) {
    if (n == 0) return;
    const Step s = Next(n);
    data[s.a] ^= s.va;
    data[s.b] ^= s.vb;
    data[s.c] ^= s.vc;
    asm volatile("" ::: "memory");
  }
};

// ---------------------------------------------------------------------------
// checksum (deterministic, order independent, byte exact)
// ---------------------------------------------------------------------------

uint64_t ChecksumBytes(const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint64_t h = 0xcbf29ce484222325ULL ^ (static_cast<uint64_t>(n) * 0x100000001b3ULL);
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t v;
    std::memcpy(&v, b + i, 8);
    h = (h ^ v) * 0x100000001b3ULL;
  }
  uint64_t tail = 0;
  for (size_t j = i; j < n; ++j) {
    tail |= static_cast<uint64_t>(b[j]) << ((j - i) * 8);
  }
  h = (h ^ tail) * 0x100000001b3ULL;
  h ^= h >> 29;
  h *= 0xBF58476D1CE4E5B9ULL;
  h ^= h >> 32;
  return h;
}

// ---------------------------------------------------------------------------
// fixtures
// ---------------------------------------------------------------------------

// One measurement target.  Constructed once (allocation and input generation
// happen here, outside the timed region), then Run() is called repeatedly.
struct Bench {
  virtual ~Bench() = default;
  virtual void Run() = 0;              // one call + consumption
  virtual uint64_t Checksum() = 0;     // output witness, taken after timing
  virtual int64_t units() const = 0;   // work units per call
  virtual double bytes_per_unit() const = 0;  // traffic estimate, < 0 = unknown
  // Hand the kernel input no earlier call has seen; called immediately before
  // every timed call and before every call of a `dump`.  See `Perturber`.
  // The one bench that is not perturbed is the physics anchor, whose whole job
  // is to measure this machine's memcpy rate and which is never measured.
  virtual void Perturb() {}
  Perturber perturber;
};

// --- byte stream split -----------------------------------------------------
// Unit: one value (width bytes in, width bytes out).
struct BssBench : Bench {
  int width;
  bool encode;
  std::vector<uint8_t> raw, enc, out;

  BssBench(uint64_t seed, int w, bool enc_) : width(w), encode(enc_) {
    Rng r(seed);
    const size_t bytes = static_cast<size_t>(kBssValues) * static_cast<size_t>(w);
    raw.resize(bytes + 64);
    enc.resize(bytes + 64);
    out.resize(bytes + 64, 0);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<uint8_t>(r.Next32());
    for (size_t i = 0; i < enc.size(); ++i) enc[i] = static_cast<uint8_t>(r.Next32());
  }

  void Run() override {
    if (encode) {
      arrow::util::internal::ByteStreamSplitEncode(raw.data(), width, kBssValues,
                                                   out.data());
    } else {
      arrow::util::internal::ByteStreamSplitDecode(enc.data(), width, kBssValues,
                                                   /*stride=*/kBssValues, out.data());
    }
    ConsumeBytes(out.data(), static_cast<size_t>(kBssValues) * static_cast<size_t>(width));
  }
  // Fresh bytes of whichever side of the codec this bench reads.
  void Perturb() override {
    if (encode) {
      perturber.Walk(raw.data(), raw.size());
    } else {
      perturber.Walk(enc.data(), enc.size());
    }
  }
  uint64_t Checksum() override {
    return ChecksumBytes(out.data(),
                         static_cast<size_t>(kBssValues) * static_cast<size_t>(width));
  }
  int64_t units() const override { return kBssValues; }
  double bytes_per_unit() const override { return 2.0 * width; }
};

// fnid registry
// ---------------------------------------------------------------------------

enum FnKind {
  kDispatch = 0,  // internally dispatched: one line (installed arm)
  kDualArm = 1,   // explicit hardware_flags: <fnid>@scalar + <fnid>@simd
  kSingle = 2,    // one implementation, one line
  kDropped = 3,   // no reachable entry point
};

struct FnSpec {
  std::string id;
  FnKind kind;
  std::string drop_reason;
};

std::vector<FnSpec> Registry() {
  std::vector<FnSpec> v;
  for (int w : {2, 4, 8}) {
    v.push_back({"bss.enc." + std::to_string(w), kDispatch, ""});
    v.push_back({"bss.dec." + std::to_string(w), kDispatch, ""});
  }
  return v;
}

const FnSpec* FindSpec(const std::vector<FnSpec>& reg, const std::string& id) {
  for (const FnSpec& s : reg) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

std::unique_ptr<Bench> MakeBench(const std::string& id, uint64_t seed, int arm) {
  const uint64_t s = FnidSeed(seed, id);
  // The @simd arm is given the flag word the running library itself would use,
  // not a hardcoded ISA bit: these call sites take the flags from the caller, so
  // "the implementation the library installed" is by definition the one selected
  // by its own hardware_flags() (on a host without AVX2 that word has no AVX2 bit,
  // and crediting the AVX2 arm would be meaningless there).  @scalar stays 0.

  if (id.rfind("bss.", 0) == 0) {
    const bool enc = id.rfind("bss.enc.", 0) == 0;
    const bool dec = id.rfind("bss.dec.", 0) == 0;
    if (!enc && !dec) return nullptr;
    const int w = std::atoi(id.c_str() + 8);
    return std::make_unique<BssBench>(s, w, enc);
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// timing
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

double Ns(const Clock::time_point& a, const Clock::time_point& b) {
  return std::chrono::duration<double, std::nano>(b - a).count();
}

double MeasureSampleNs(Bench& b, int64_t reps) {
  const Clock::time_point t0 = Clock::now();
  for (int64_t i = 0; i < reps; ++i) {
    b.Perturb();  // fresh input for every timed call: see Perturber
    b.Run();
  }
  const Clock::time_point t1 = Clock::now();
  return Ns(t0, t1);
}

struct Timing {
  double ns_per_unit = 0;
  double lo = 0, hi = 0;
  int n = 0;
  int64_t reps = 0;
  std::vector<double> samples;  // sorted, ns/unit; printed by the `samples` mode
};

// Times one or more benches with their samples round-robined.  With two arms in
// the list (a scalar/SIMD pair selected by an explicit `hardware_flags`
// argument) both arms sample the *same* distribution of external interference,
// so the ratio is far less sensitive to a burst of contention landing on one
// arm's window than two sequential measurements would be.  With one arm it is a
// plain sequential measurement.
void TimeArms(const std::vector<Bench*>& benches, std::vector<Timing>* out) {
  const size_t n_arms = benches.size();
  out->assign(n_arms, Timing{});
  std::vector<int64_t> reps(n_arms, 1);
  std::vector<std::vector<double>> samples(n_arms);
  std::vector<int> recalibrations(n_arms, 0);

  for (size_t i = 0; i < n_arms; ++i) {
    Bench& b = *benches[i];
    b.Run();  // warm-up: resolves dispatch, faults in pages, primes caches
    double one = MeasureSampleNs(b, 1);
    if (!(one > 0)) one = 1e3;
    reps[i] = static_cast<int64_t>(std::ceil(kTargetSampleNs / one)) + 1;
    if (reps[i] < 1) reps[i] = 1;
  }

  bool progress = true;
  while (progress) {
    progress = false;
    for (size_t i = 0; i < n_arms; ++i) {
      if (static_cast<int>(samples[i].size()) >= kSamples) continue;
      Bench& b = *benches[i];
      const double total = MeasureSampleNs(b, reps[i]);
      if (total < kTargetSampleNs * 0.9 && recalibrations[i] < kMaxRecalibrations) {
        ++recalibrations[i];
        const double factor = kTargetSampleNs / std::max(total, 1.0) * 1.05;
        int64_t next = static_cast<int64_t>(static_cast<double>(reps[i]) * factor) + 1;
        if (next <= reps[i]) next = reps[i] + 1;
        reps[i] = std::min<int64_t>(next, int64_t{1} << 42);
        progress = true;
        continue;
      }
      samples[i].push_back(total / static_cast<double>(reps[i]) /
                           static_cast<double>(b.units()));
      progress = true;
    }
  }

  for (size_t i = 0; i < n_arms; ++i) {
    std::sort(samples[i].begin(), samples[i].end());
    Timing& t = (*out)[i];
    t.n = static_cast<int>(samples[i].size());
    t.lo = samples[i].front();
    t.hi = samples[i].back();
    t.ns_per_unit = samples[i][samples[i].size() / 2];
    t.reps = reps[i];
    t.samples = samples[i];
  }
}


// ---------------------------------------------------------------------------
// physics anchor
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

bool EnvForcesScalarDispatch() {
  const char* v = std::getenv("ARROW_USER_SIMD_LEVEL");
  if (v == nullptr) return false;
  std::string s(v);
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s == "NONE";
}

void Usage() {
  fprintf(stderr,
          "usage: kbench dump|bench|samples <seed> <fnid> [<fnid> ...]\n"
          "       <seed> is a 64-bit unsigned decimal\n"
          "       <fnid> may be 'all' to select every scoped function\n"
          "       dump    -> '<label> <hex64>'\n"
          "       bench   -> '<label> <ns_per_unit> <hex64>'\n"
          "       samples -> '<label> <n> <reps> <sorted ns_per_unit samples...>'\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    Usage();
    return 2;
  }
  const std::string mode = argv[1];
  if (mode != "dump" && mode != "bench" && mode != "samples") {
    Usage();
    return 2;
  }
  char* end = nullptr;
  const uint64_t seed = std::strtoull(argv[2], &end, 10);
  if (end == argv[2] || *end != '\0') {
    fprintf(stderr, "kbench: bad seed '%s'\n", argv[2]);
    return 2;
  }

  const std::vector<FnSpec> registry = Registry();
  const bool scalar_env = EnvForcesScalarDispatch();

  // Host context, on stderr so the measurement lines stay machine-parseable.
  {
    const auto* cpu = arrow::internal::CpuInfo::GetInstance();
    fprintf(stderr,
            "# host model=\"%.*s\" vendor=%d sse4_2=%d avx2=%d avx512=%d bmi2=%d "
            "efficient_bmi2=%d hardware_flags=0x%llx user_simd=%s dispatch=%s\n",
            static_cast<int>(cpu->model_name().size()), cpu->model_name().data(),
            static_cast<int>(cpu->vendor()), cpu->IsSupported(cpu->SSE4_2) ? 1 : 0,
            cpu->IsSupported(cpu->AVX2) ? 1 : 0, cpu->IsSupported(cpu->AVX512) ? 1 : 0,
            cpu->IsSupported(cpu->BMI2) ? 1 : 0, cpu->HasEfficientBmi2() ? 1 : 0,
            static_cast<unsigned long long>(cpu->hardware_flags()),
            scalar_env ? "NONE" : "(unset)", scalar_env ? "scalar" : "installed");
    fflush(stderr);
  }

  std::vector<std::string> ids;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "all") == 0) {
      for (const FnSpec& s : registry) {
        if (s.kind != kDropped) ids.push_back(s.id);
      }
    } else {
      ids.push_back(argv[i]);
    }
  }


  for (const std::string& id : ids) {
    const FnSpec* spec = FindSpec(registry, id);
    if (spec == nullptr) {
      fprintf(stderr, "# unknown fnid: %s (skipped)\n", id.c_str());
      continue;
    }
    if (spec->kind == kDropped) {
      fprintf(stderr, "# dropped %s: %s\n", id.c_str(), spec->drop_reason.c_str());
      continue;
    }

    struct ArmRun {
      int arm;
      const char* suffix;
    };
    std::vector<ArmRun> arms;
    if (spec->kind == kDualArm) {
      arms = {{1, "@simd"}, {0, "@scalar"}};
    } else if (spec->kind == kDispatch && scalar_env) {
      arms = {{1, "@scalar"}};  // the dispatcher itself resolved to the scalar arm
    } else {
      arms = {{1, ""}};
    }

    // Build every arm of this fnid up front so that a scalar/SIMD pair can be
    // timed with interleaved samples (see TimeArms).
    std::vector<std::string> labels;
    std::vector<std::unique_ptr<Bench>> benches;
    for (const ArmRun& a : arms) {
      std::unique_ptr<Bench> bench = MakeBench(id, seed, a.arm);
      if (bench == nullptr) {
        fprintf(stderr, "# FAILED to construct %s%s (skipped)\n", id.c_str(), a.suffix);
        continue;
      }
      labels.push_back(id + a.suffix);
      benches.push_back(std::move(bench));
    }
    if (benches.empty()) continue;

    if (mode == "dump") {
      for (size_t k = 0; k < benches.size(); ++k) {
        // Several calls with fresh input, checksumming the last: a kernel that
        // memoises on its input, or on the buffer address, answers the later
        // calls with the first call's result -- and that stale result is then
        // compared, byte for byte, with the base tree's, which recomputed.
        for (int64_t j = 0; j < kDumpCalls; ++j) {
          benches[k]->Perturb();
          benches[k]->Run();
        }
        printf("%s %016llx\n", labels[k].c_str(),
               static_cast<unsigned long long>(benches[k]->Checksum()));
        fflush(stdout);
      }
      continue;
    }

    std::vector<Bench*> raw;
    for (auto& b : benches) raw.push_back(b.get());
    std::vector<Timing> timings;
    TimeArms(raw, &timings);
    for (size_t k = 0; k < benches.size(); ++k) {
      const uint64_t cs = benches[k]->Checksum();
      if (mode == "samples") {
        // Every raw sample, sorted, for offline ratio statistics.
        printf("samples %s %d %lld", labels[k].c_str(), timings[k].n,
               (long long)timings[k].reps);
        for (double v : timings[k].samples) printf(" %.6f", v);
        printf("\n");
      } else {
        printf("%s %.6f %016llx\n", labels[k].c_str(), timings[k].ns_per_unit,
               static_cast<unsigned long long>(cs));
      }
      fflush(stdout);
      fprintf(stderr, "# spread %s min=%.6f med=%.6f max=%.6f n=%d reps=%lld\n",
              labels[k].c_str(), timings[k].lo, timings[k].ns_per_unit, timings[k].hi,
              timings[k].n, (long long)timings[k].reps);
    }
  }

  return 0;
}
