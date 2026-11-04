# Step 6 — the codeblock pack/unpack

## 1. Implement this milestone

`/app/ojph` is OpenJPH at a pinned upstream commit with the optimized
implementation of one or more hot stages of the codec removed. The library is
complete and correct: it builds, it passes upstream's own 109-test suite, and it
encodes and decodes every image correctly through the portable C++ code paths in
`src/core`. It is simply slow in the places whose optimized code is missing.

**This milestone: make the codeblock pack and unpack faster, without changing a single output byte.**

Between the wavelet transform and the block coder there is a repacking step:
a subband line's samples are converted to sign-magnitude codeblock words on the
way in, and a decoded codeblock is converted back to signed samples on the way
out, with the bit-plane interleave and the quantization scaling applied in the
same pass. It runs once per codeblock line, in both directions, and it is the
seam between the transform and the entropy coder.

The stage currently has no optimized implementation. The dispatch layer leaves
the portable implementation installed, so the code in `src/core/codestream/ojph_codestream_gen.cpp`
(`gen_rev_tx_to_cb32`, `gen_irv_tx_to_cb32`, `gen_rev_tx_from_cb32`,
`gen_irv_tx_from_cb32`, `gen_rev_tx_to_cb64`, `gen_rev_tx_from_cb64`) runs. Optimize it however
you judge best: a new per-instruction-set translation unit registered in
`src/core/CMakeLists.txt`, or the portable C++ itself. You are measured on **the
implementation the library actually installs** for each function, so any route
that makes them faster is valid — including making the portable path itself
faster.

Earlier milestones must keep working. This workspace is cumulative: work you did
in earlier steps is still here and is still part of this step's baseline, so a
regression there fails this milestone as well as undoing the earlier work.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: encoding and decoding must produce
   byte-identical output to the tree you started from, for every input and every
   configuration — not merely the cases you happen to test. An approximation, a
   fast path that is right "almost always", or a reordering that changes
   rounding is a failure, however fast it is. The comparison is
   byte-exact, not a tolerance: reassociating floating-point arithmetic — for
   example by letting FMA contraction into a float kernel — changes the last
   bits and fails it. The checker builds its own reference from the pinned
   commit, so it is the *behaviour* that must match, not the file: tune the
   portable C++ if that is the road you take, but nothing it computes may
   change.
2. **The transfer functions must get faster.** Only these three pointer slots are in scope. Optimizing some other
   part of the library does nothing for this milestone, and neither does effort
   that does not show up in the measurement.

`/app/BENCHMARK.md` gives the commands for both — the behaviour checks and the
timing — so you can confirm where you stand before finishing.

**The workload is fixed and is not disclosed.** The inputs are generated
deterministically by the checker and are identical in every run, so you cannot
choose them — and you are not told their sizes. Optimize for the general case
rather than for one shape: the behaviour checks run this stage at every shape
the pipeline produces, and a kernel that only handles one of them fails.

## 2. Read these local references

**Normative references, all local:**

- `src/core/codestream/ojph_codeblock_fun.h` — the dispatch seam. This
  milestone is measured on three of its pointers: `tx_from_cb32` (the
  irreversible 32-bit path), `tx_to_cb64` and `tx_from_cb64` (the reversible
  64-bit path). Each pointer is the one the library *installs*, so whichever
  translation unit ends up behind it is what gets timed.
- `src/core/codestream/ojph_codeblock_fun.cpp` — where they are installed.
- `src/core/codestream/ojph_codestream_gen.cpp` — the portable implementations
  that define the correct output.
- `src/core/CMakeLists.txt` — how per-instruction-set translation units are
  registered and what flags they are compiled with.
- `/app/workload/work.ppm` — a sample image for the behaviour checks. It is not
  the workload this milestone is measured on.
- `/app/BENCHMARK.md` — the behaviour and timing commands.
- `/app/tools/measure` — end-to-end timing harness. Run it as
  `/app/tools/measure /app/build`; it prints the encode, decode and total
  milliseconds, lower is better.
- `/app/WORKSPACE_HANDOFF.md` — what earlier steps changed, and how to build and
  measure locally.

**Out of scope — leave alone.** `find_max_val32/64` and `mem_clear` are the same dispatch
struct's other entries but are not part of this milestone: they are
memory-bound scans whose optimized versions measure no faster than the portable
ones. This stage has no 64-bit *irreversible* transfer function — `tx_to_cb64`
is `NULL` on that path upstream. **`tx_to_cb32` is also not measured**: the
portable kernel truncates where the optimized one rounds to nearest, so the two
disagree on inputs outside the range the pipeline itself produces, and no
equality check can cover it. Making it faster is still a correct thing to do —
the encode round trip is checked end to end and it must stay byte-identical —
it is simply out of scope here. Touching them is out of scope and
risks the byte-identity requirement.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/ojph` as the source tree
  and keep the upstream build system working
  (`cmake -S /app/ojph -B /app/build -DCMAKE_BUILD_TYPE=Release`).
- **This stage is measured on SSE through AVX2 — 256-bit registers at most.** The
  target host reports `sse sse2 ssse3 sse4_1 sse4_2 avx avx2` and **no
  AVX-512**; code that needs AVX-512 will not run there and a milestone whose
  changes use it fails. Compiler intrinsics are allowed (upstream's own
  implementation of this stage is intrinsics too); so is assembly, or portable
  C. Do not widen the whole build with `-march=native` or `-mavx512*`.
- **The reference implementation is your peer on this stage, not a floor:
  upstream ships its AVX2 (256-bit) kernels for it, the widest this host
  executes.** Beating it with better kernel code is a permitted and expected
  outcome, not a measurement error.
- **The interface the harness compiles against is frozen.** The
  measurement harness is built against the headers this step names in §2 and
  linked against your library, and it calls the installed pointers through the
  types those headers declare. Add files, kernels and dispatch levels freely
  behind the pointers; do not change the signatures or the types the harness
  uses (`codeblock_fun`, `param_atk` and its `get_atk`/`get_step`/`get_K`,
  `line_buf`, `mem_elastic_allocator`, the dispatch entry points and their
  parameters) — a step whose harness no longer builds is not satisfied.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  OpenJPH's own optimized implementation of this stage, or any other project's,
  from the internet or a package mirror.** The optimization must be yours. Do
  not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
