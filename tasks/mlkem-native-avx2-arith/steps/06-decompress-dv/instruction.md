# Step 6 — narrow decompression of a polynomial

## 1. Implement this milestone

`/app/mlkem-native` is mlkem-native at a pinned upstream tag. The aarch64,
ppc64le and riscv64 arithmetic backends were removed, along with the `proofs/`,
`examples/`, `dev/` and `scripts/autogen` trees. **The x86_64 arithmetic backend
is partly restored: earlier milestones in this ladder implemented their own
stages, and their kernels, toggles and prototypes are in the tree you inherit.**
This milestone's entry points are the ones still running portable C. Some in-tree
comments and CI files refer to removed paths; they are inert. The library is complete and correct: it
builds, passes its own functional and known-answer suites in full for all three
parameter sets, and implements FIPS 203 exactly. It is simply slow in the
places whose optimized code is missing: those entry points run the portable C
implementation directly, because no `MLK_USE_NATIVE_*` toggle is defined for
them, so the dispatch is compiled out rather than falling back at run time.

**This milestone: make the decompression of a polynomial from `dv` bits faster, without changing a single output byte.**

It covers:

- `mlk_poly_decompress_dv` — `Decompress_{d_v}(ByteDecode_{d_v}(v))` in FIPS 203's `K-PKE.Decrypt`, `mlkem/src/poly_k.h`

`d_v` is 4 for ML-KEM-512 and ML-KEM-768 and 5 for ML-KEM-1024, so both
`d = 4` and `d = 5` are in scope. This is the approximate inverse of the
corresponding compression, and it is defined on every `d_v`-bit string,
including the ones no compressor would emit.

These entry points currently have no optimized implementation. Their dispatch
hooks in `mlkem/src/native/x86_64/meta.h` — `mlk_poly_decompress_d4_native` and `mlk_poly_decompress_d5_native` —
return `MLK_NATIVE_FUNC_FALLBACK`. **None of this step's `MLK_USE_NATIVE_*`
toggles is defined in the tree you inherit — adding them is part of the step.**
Without a toggle the frontend never calls that hook at all: the portable C code
runs directly, so filling in a hook body alone measures 1.0x with no compile
error to warn you.
Fill them in: hand-written assembly in new files under
`mlkem/src/native/x86_64/src/`, or faster portable C, whichever you judge
better. You are measured on the entry points the library actually installs, so
either route is acceptable. Both must leave the output bit-identical.

Earlier milestones must keep working. This workspace is cumulative: the work
done at earlier milestones is still here and is still part of this milestone's
baseline, so a regression fails this milestone as much as the earlier one — and
because the tree is what carries that work forward, read it to see what is
already in place.

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
checks are re-run against hidden inputs afterwards.

**The measurement inputs are not disclosed.** The measurement harness draws its
own inputs from a seed you never see, so you cannot tune to a particular buffer.
Optimize this stage for the general case.

**A note on where the time goes.** This entry point is a small part of a
whole decapsulation, so a whole-operation timing barely moves. Your
functions are timed individually, so this dilution does not affect your result.

## 2. Read these local references

**Normative references, all local:**

- `mlkem/src/native/x86_64/meta.h` — the dispatch layer for this architecture.
  Each primitive has a compile-time toggle (`MLK_USE_NATIVE_*`) and a
  `static MLK_INLINE` hook that returns `MLK_NATIVE_FUNC_FALLBACK` when no
  optimized implementation is present; the frontend then runs the C code.
- `mlkem/src/native/api.h` — what a backend hook must do: its contract and its
  return values.
- `mlkem/src/native/x86_64/src/arith_native_x86_64.h` — **where kernel
  prototypes go.** It already declares the kernels earlier milestones restored;
  add yours alongside them (the build is `-Wmissing-prototypes -Werror`, so a
  non-`static` kernel needs its prototype somewhere). For the contracts of the
  hooks that call them, read `mlkem/src/native/api.h`.
- `mlkem/src/native/x86_64/README.md` — points at the upstream pq-crystals AVX2
  repository, which the rule below forbids fetching; **do not follow
  it.** The usable in-tree model of a backend's layout is the FIPS-202 one:
  `mlkem/src/fips202/native/x86_64/` (`keccak_f1600_x4_avx2.h` for the hook
  pattern, `src/keccak_f1600_x4_avx2_asm.S` for the assembly conventions).
- `mlkem/src/sys.h` — the runtime CPU capability check the hooks are expected
  to perform before using AVX2.
- `mlkem/src/poly.h`, `mlkem/src/poly_k.h` and `mlkem/src/compress.h` — the
  entry points named above, with their contracts.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands.
- `/app/WORKSPACE_HANDOFF.md` — where you record what you changed, the
  speedup you measured, and how to rebuild. It is scoped to this milestone and
  is regenerated for the next one, so it is a record for whoever inspects this
  result rather than a channel to whoever comes next.

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
- The kernels must stay constant-time. mlkem-native treats secret-independent
  timing as part of correctness: an implementation that branches on a secret
  value, or indexes a table with one, is not acceptable even if its output is
  right.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  mlkem-native's own optimized implementation of this stage, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect the `/tests` mount — an external directory, which is **not** this repository's `test/` — nor later-step directories nor solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you
  changed, the speedup you measured locally, and how to rebuild.

