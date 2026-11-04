# This milestone — Decompose and UseHint at gamma2 = (q-1)/88

## 1. Implement this milestone

`/app/mldsa-native` is the mldsa-native post-quantum signature library at a
pinned upstream commit, with the optimized implementation of one or more of its
arithmetic backend stages removed. The library is complete and correct: it
builds, it passes its own functional, differential-unit and KAT suites for all
three parameter sets, and it signs and verifies correctly using the portable C
code paths. It is simply slow in the places whose optimized code is missing.

**This milestone: make the coefficient decomposition and hint application faster at gamma2 = (q-1)/88, without changing a single output byte.**

The same two operations as the previous milestone, at the ML-DSA-44 modulus
split `gamma2 = (q-1)/88`. The divisor is different, so the reciprocal-multiply
constants and the boundary correction are different, and an implementation that
works for one is not an implementation of the other.

They are reached through `mld_poly_decompose` and `mld_poly_use_hint` in the
parameter sets where the library dispatches to this split: ML-DSA-44. The other
split is a different divisor and is not this milestone's concern.

Implement them however you judge best — assembly in a new file registered in
the build, the backend hooks in `mldsa/src/native/x86_64/meta.h`, compiler
intrinsics, or the portable C itself. You are measured on the entry point the
library actually ends up dispatching to, so any route that makes it faster is
acceptable. Earlier milestones must keep working: this workspace is cumulative, so
anything already optimized here is still part of this step's baseline, and a
regression fails this milestone as much as the earlier one.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: for every parameter set and every input,
   key generation, signing and verification must produce byte-identical output
   to the tree you started from, and the published KAT digests must still
   match. An approximation, a fast path that is right "almost always", or a
   reordering that changes rounding is a failure, however fast it is.
2. **This stage must get faster** on the entry points named in
   `/app/BENCHMARK.md`. Only this stage is in scope. Optimizing some other part of
   the library does not satisfy this milestone, and neither does effort that
   does not show up in the measurement.
3. **The implementation stays constant-time.** ML-DSA's secrets must not
   influence the instruction stream or the memory access pattern. The portable
   C is written that way and the bar is that yours is too.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks
and the timing — so you can confirm where you stand before finishing. The same
checks are re-run against hidden fixtures afterwards.

**The measurement inputs are not disclosed.** They are generated deterministically
by the measurement harness from its own seed and are not visible here, and every
timed call is made on a draw of its own — no draw is reused inside a measurement.
So an implementation that returns a memoised answer for an input it has already
seen instead of computing one is not measured. Optimize for the general case.

## 2. Read these local references

**Normative references, all local:**

- `/app/mldsa-native/mldsa/src/native/api.h` — the backend interface. Every
  hook this library can dispatch to is declared and documented here, with its
  contract: what it may assume about its inputs, what it must produce, and what
  it returns when the platform cannot run it.
- `/app/mldsa-native/mldsa/src/native/x86_64/meta.h` — this platform's backend
  header, where the hooks are installed. This is the file you add your
  implementation to.
- `/app/mldsa-native/mldsa/src/rounding.h` — the decomposition's
  definition and its boundary case.
- `/app/mldsa-native/mldsa/src/poly.c`, `poly_kl.c`, `polyvec.c`,
  `polyvec_lazy.c` — the callers. Each one calls the hook and falls through to
  the portable C when the hook reports `MLD_NATIVE_FUNC_FALLBACK`. The portable
  C is the definition of correct output; do not change what it computes.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands, and the
  entry points this milestone covers.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to
  build and measure locally.

The project's own `SOUNDNESS.md`, `API-CONVENTIONS.md` and `BUILDING.md` ship
with the tree and are worth reading.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/mldsa-native` as the
  source tree, keep the upstream build system working
  (`make -C /app/mldsa-native lib OPT=1 AUTO=1 BUILD_DIR=<dir>`), and preserve
  the handoff documents.
- **This stage is measured on AVX2 or narrower.** The reference implementation
  uses 256-bit registers and the target host provides SSE2 and AVX2; it has no
  AVX-512. Reaching for wider registers cannot pay off here and is not what is
  being measured, and it fails the milestone: AVX-512 instructions, the `zmm`
  register file, `_mm512_*`/`__m512` intrinsics, and `-mavx512…` or
  `-march=…avx512…` build flags are outside this milestone's envelope wherever
  its code touches them, even where that code would never run on this host.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  mldsa-native's own optimized implementation of this stage, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you
  changed, the speedup you measured locally, and how to rebuild.
