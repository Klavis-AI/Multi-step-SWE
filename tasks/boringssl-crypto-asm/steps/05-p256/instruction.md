# Step 5 — P-256 scalar multiplication

## 1. Implement this milestone

`/app/boringssl` is BoringSSL at a pinned upstream commit with the optimized
implementation of one or more primitive groups removed. The library is complete
and correct: it builds, it passes its own test suite, and every operation it
exposes produces exactly the right answer using the portable code paths. It is
simply slow in the places whose optimized code is missing.

**This milestone: make P-256 faster, without changing a single output byte.**

It covers scalar multiplication on NIST P-256, through
`EC_POINT_mul`, `ECDSA_sign`, `ECDSA_verify` and `ECDH_compute_key`. On this
curve the scalar multiplication and the modular inversion inside signing are
what an ECDSA signature or an ECDH exchange costs.

The entry points this milestone is measured on are:

| entry point | what it is |
| --- | --- |
| `p256_point_mul` | `EC_POINT_mul` of a 256-bit scalar times a point |
| `p256_ecdsa_sign` | `ECDSA_sign` with a P-256 key |
| `p256_ecdsa_verify` | `ECDSA_verify` with a P-256 key |
| `p256_ecdh` | `ECDH_compute_key` between two P-256 keys |

The primitive group currently has no optimized implementation. `crypto/fipsmodule/ec/ec.cc.inc` installs the portable method -- `EC_GFp_nistp256_method()`, backed by the formally verified fiat-crypto field arithmetic in `third_party/fiat/` -- instead of the assembly one. Optimize it however you judge best: hand-written assembly in a
new file wired into `gen/sources.cmake`, perlasm (Perl is installed, and its
output is committed under `gen/` like the tree's own), C with the compiler's
vector extensions, or a better portable C. You are measured on the
functions the library actually ends up installing, so any route that makes them
faster is acceptable.

Earlier milestones must keep working. This workspace is cumulative -- but the
baseline this milestone is measured against is the tree you were handed here,
rebuilt from a clean checkout, not the tree you deliver: the per-function
ratios are measured against that checkpoint, so work you did in an earlier
milestone neither helps nor hurts at this one, and every function is measured
once, in its own milestone. A delivered tree that is overall more than
5% slower than
the checkpoint this ladder starts from -- that checkpoint with every optimized
implementation removed -- is rejected as an overall regression;
and every function left slower than the baseline it is measured against is a
regression, not a speedup.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: every one of those entry points must
   produce byte-identical output to the tree you started from, for every input
   and every configuration -- not merely for the cases you happen to test. An
   approximation, a fast path that is right "almost always", or a reordering
   that changes a rounding is a failure, however fast it is.
2. **These functions must get faster.** Only this primitive group is in scope.
   Optimizing some other part of the library is out of scope for this
   milestone, and so is effort that does not show up in the measurement.

`/app/BENCHMARK.md` gives the exact commands for both -- the behaviour checks and
the timing -- so you can confirm where you stand before finishing. The same
checks are re-run against hidden inputs afterwards.

**The inputs are not disclosed, and the workload's shape is fixed.** The
measurement harness generates the keys, messages and scalars from a random draw
it does not publish, so there is no fixed input value to look at and nothing to
special-case in the data: every timed call gets an argument no earlier call
saw. The sizes are not secret and do not vary -- the same buffer lengths,
operand sizes and alignment every run, exactly the ones `/app/tools/measure`
uses. Optimize for the general case: a kernel that is fast only for one
particular input value is not fast across the workload, and a correct one has
to handle every value anyway.

## 2. Read these local references

**Normative references, all local:**

- `crypto/fipsmodule/ec/p256.cc.inc` — the portable method running now, and the definition of the correct output.
- `third_party/fiat/p256_64.h` — the formally verified field arithmetic it is built on.
- `crypto/fipsmodule/ec/ec.cc.inc` — where the method is chosen.
- `gen/sources.cmake` — how assembly files are registered in the build.

- `/app/BENCHMARK.md` — the exact behaviour and timing commands.
- `/app/tools/measure` — per-function timing harness for this milestone. Run it
  as `/app/tools/measure /app/build`; it prints one line per entry point with a
  nanosecond figure, lower is better.
- `/app/WORKSPACE_HANDOFF.md` — what the workspace contains and how to build.

`crypto/fipsmodule/ec/asm/` is empty: it holds no optimized kernel for this
primitive group -- for any architecture -- and `gen/bcm/` none of the output
generated from one. Nothing else in the library is affected.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/boringssl` as the source
  tree, keep the upstream build system working
  (`cmake -S /app/boringssl -B /app/build -G Ninja -DCMAKE_BUILD_TYPE=Release`),
  and preserve the handoff documents.
- **This milestone is measured on AVX2 or narrower, single-threaded.** The
  reference implementation is upstream's own hand-written assembly, and on this
  machine it tops out at 256-bit AVX2: the target host reports `aes`,
  `pclmulqdq`, `ssse3`, `sse4_1`, `sse4_2`, `avx`, `avx2`, `bmi1`, `bmi2`, `fma`
  and `adx`, and it has **no AVX-512, no VAES and no VPCLMULQDQ**. Code written
  for those could not run here, and it is not accepted: every code file
  you changed under `crypto/`, `third_party/` or `gen/` is scanned for AVX-512
  registers and EVEX-only mnemonics and for the VAES / VPCLMULQDQ mnemonics
  and intrinsics, and the milestone fails if they are found -- including
  behind a runtime capability check that would keep them from executing, and
  including in new files. The build configuration is frozen --
  `CMakeLists.txt` and everything under `cmake/` are compared against the tree
  you were given, so a wider `-march` or a different optimization level is not
  available to you.
- Keep the tree self-contained. The build that measures your work runs on the
  clean base image, which installs nothing on your behalf, so the only
  dependencies available are the ones this tree already vendors: do not add
  a new one -- neither a library to link nor a tool the build shells out to.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  BoringSSL's own optimized implementation of this primitive group, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect `/tests`, later-milestone directories, or solution
  material.
- Do not weaken, filter, rename, or disable tests. The project's suite must gain
  no failure it did not already have.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
