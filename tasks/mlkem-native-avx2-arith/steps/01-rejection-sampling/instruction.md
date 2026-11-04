# Step 1 — rejection sampling of matrix entries

## 1. Implement this milestone

`/app/mlkem-native` is mlkem-native at a pinned upstream tag with **all four
architecture backends' optimized arithmetic removed** (x86_64, aarch64, ppc64le,
riscv64), along with the `proofs/`, `examples/`, `dev/` and `scripts/autogen`
trees. Some in-tree comments and CI files still refer to those removed paths;
they are inert and not part of your work. The library is complete and correct: it
builds, passes its own functional and known-answer suites in full for all three
parameter sets, and implements FIPS 203 exactly. It is simply slow in the
places whose optimized code is missing: those entry points run the portable C
implementation directly, because no `MLK_USE_NATIVE_*` toggle is defined for
them, so the dispatch is compiled out rather than falling back at run time.

**This milestone: make rejection sampling of matrix entries faster, without changing a single output byte.**

It covers the entry point `mlk_poly_rej_uniform` (in `mlkem/src/sampling.h`),
which draws a polynomial of uniform coefficients in `Z_q` from a SHAKE-128
stream by rejecting 12-bit candidates outside `[0, q)`.

On the default key-generation path the matrix is built in bulk by
`mlk_poly_rej_uniform_x4`, which shares the same inner sampler. This
single-entry-point form is reached only for the tail of the matrix, and only for
ML-KEM-768, where the matrix has an entry left over; for ML-KEM-512 and -1024
the bulk call consumes every entry. **`mlk_poly_rej_uniform` is nonetheless the
entry point that is timed**, so it is the one to optimise — and because the
two share an inner loop, work on the sampler reaches both.

The stage currently has no optimized implementation. Its dispatch hook,
`mlk_rej_uniform_native()` in `mlkem/src/native/x86_64/meta.h`, is a `static MLK_INLINE`
stub that returns `MLK_NATIVE_FUNC_FALLBACK`. There is no
`MLK_USE_NATIVE_REJ_UNIFORM` define anywhere in the tree you inherit — **adding
it is part of this step**, at the top of that same `meta.h`, above the
`#include "../api.h"` on line 16 and inside the `MLK_SYS_X86_64_AVX2` guard.
Without that toggle the frontend never calls the hook at all and the portable C
code in
`mlkem/src/sampling.c` runs directly, so filling in the hook body alone measures
1.0x with no compile error. Fill it in — hand-written assembly in a new file under
`mlkem/src/native/x86_64/src/`, or faster portable C, whichever you judge
better. You are measured on the entry point the library actually installs, so
either route is acceptable. Both must leave the output bit-identical.

**This is the first milestone**, so there is no earlier work in the workspace to
preserve — but the workspace is cumulative from here, and later milestones build
on this one. Leave the tree in a state you would be happy to inherit: the
correctness checks below are the same ones every later step re-runs.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: key generation, encapsulation and
   decapsulation must produce byte-identical output to the tree you started
   from, for every input and every parameter set — not merely for the cases you
   happen to test. An approximation, a fast path that is right "almost always",
   or a reordering that changes rounding is a failure, however fast it is.

2. **This stage must get faster.** Only this stage is in scope. Optimizing
   some other part of the library does not satisfy this milestone, and neither
   does effort that does not show up in the measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks and
the timing — so you can confirm where you stand before finishing. The same
checks are re-run against hidden fixtures afterwards.

**The measurement inputs are not disclosed.** The measurement harness draws its
own inputs from a seed you never see, so you cannot tune to a particular buffer.
Optimize this stage for the general case.

**A note on where the time goes.** `mlk_poly_rej_uniform()` squeezes the SHAKE
stream itself, and the SHAKE permutation is a fixed cost that this milestone
does not touch; the sampled buffer is also re-squeezed block by block until
enough candidates are accepted. That fixed cost sits inside the very entry
point the measurement harness times, so the ratio this milestone can show is
the sampler's own speedup diluted by it, not the speedup of the kernel alone.
Your function is still timed on its own, so the whole-operation dilution is not
yours to carry — but do not read the diluted ceiling as "there is nothing here".

## 2. Read these local references

**Normative references, all local:**

- `mlkem/src/native/x86_64/meta.h` — the dispatch layer for this architecture.
  Each primitive has a compile-time toggle (`MLK_USE_NATIVE_*`) and a
  `static MLK_INLINE` hook that returns `MLK_NATIVE_FUNC_FALLBACK` when no
  optimized implementation is present; the frontend then runs the C code.
- `mlkem/src/native/api.h` — what a backend hook must do: its contract and its
  return values.
- `mlkem/src/sampling.c` — the portable C sampler this replaces, and its call
  site (`mlk_poly_rej_uniform`), which is what the measurement harness calls.
- `mlkem/src/sys.h` — the runtime CPU capability check the hook is expected to
  perform before using AVX2.
- `mlkem/src/native/x86_64/README.md` — points at the upstream pq-crystals AVX2
  repository, which the rule below forbids fetching; **do not follow
  it.** The usable in-tree model of a backend's layout is the FIPS-202 one:
  `mlkem/src/fips202/native/x86_64/` (`keccak_f1600_x4_avx2.h` for the hook
  pattern, `src/keccak_f1600_x4_avx2_asm.S` for the assembly conventions).
- `mlkem/src/native/x86_64/src/arith_native_x86_64.h` — **where kernel
  prototypes go.** It ships empty: the kernels it would declare are the ones this
  task removes. Add yours here (the build is `-Wmissing-prototypes -Werror`, so a
  non-`static` kernel needs its prototype somewhere). For the contracts of the
  hooks that call them, read `mlkem/src/native/api.h`.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands.
- `/app/WORKSPACE_HANDOFF.md` — where you record what you changed, the
  speedup you measured, and how to rebuild. It is regenerated for each
  milestone and is not carried forward, so write it as a record of this
  milestone's result.

FIPS 203 is the specification for what every one of these values must be; a
copy is not shipped, but the C implementations in this tree are the operative
statement of the contract.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/mlkem-native` as the
  source tree, keep the upstream build system working
  (`make lib OPT=1 AUTO=1 BUILD_DIR=<dir>` from that directory), and preserve
  the handoff documents.
- **This stage is measured on AVX2 or narrower, on one thread.** The reference
  implementation uses 256-bit registers here and runs single-threaded, so you
  may do the same. The target host provides SSE2 and AVX2 and has no AVX-512;
  code that needs a wider machine cannot run and is not measured. Keep the build's
  own flags — do not add `-march=native`, an `-mavx512*` option, or a tuning
  flag such as `-mtune=native` / `-mcpu=native`: an option that widens the
  compiler's reach or tunes it for this host is outside the envelope. Do not
  spawn threads or use OpenMP from a kernel. This is enforced: an
  out-of-envelope construct in this stage's own files, in a file you add, or in
  a build file you change fails this milestone's correctness checks.
- The kernel must stay constant-time. mlkem-native treats secret-independent
  timing as part of correctness: a sampler that branches on a candidate value,
  or indexes a table with one, is not an acceptable implementation even if its
  output is right.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  mlkem-native's own optimized implementation of this stage, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect the `/tests` mount — an external directory, which is **not** this repository's `test/` — nor later-step directories nor solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you
  changed, the speedup you measured locally, and how to rebuild.
