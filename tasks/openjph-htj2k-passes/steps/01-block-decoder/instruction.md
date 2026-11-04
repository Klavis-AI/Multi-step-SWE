# Step 1 — the HTJ2K codeblock decoder

## 1. Implement this milestone

`/app/ojph` is OpenJPH at a pinned upstream commit with the optimized
implementation of one or more hot stages of the codec removed. The library is
complete and correct: it builds, it passes upstream's own 109-test suite, and it
encodes and decodes every image correctly through the portable C++ code paths in
`src/core`. It is simply slow in the places whose optimized code is missing.

**This milestone: make the codeblock decoder faster, without changing a single output byte.**

A JPEG 2000 codestream carries its coefficients in *codeblocks*. The decoder
entry point turns one codeblock's compressed bytes back into an array of
coefficient words: it walks three interleaved sub-bitstreams — MEL, VLC and
magnitude/sign — that are read in opposite directions, rebuilds the significance
pattern, and writes the samples. It is the innermost loop of every decode.

The stage currently has no optimized implementation. The dispatch layer
(`src/core/codestream/ojph_codeblock_fun.cpp`) leaves the portable
implementation installed, so the library runs the code in
`src/core/coding/ojph_block_decoder32.cpp`. Optimize it however you judge best:
a new per-instruction-set translation unit registered in the build, or the
portable C++ itself. You are measured on **the implementation the library
actually installs** for this stage, so any route that makes it faster is valid —
including making the portable path itself faster.

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
2. **This stage must get faster.** Only this stage is in scope. Optimizing some other
   part of the library does nothing for this milestone, and neither does effort
   that does not show up in the measurement.

`/app/BENCHMARK.md` gives the commands for both — the behaviour checks and the
timing — so you can confirm where you stand before finishing.

**The workload is fixed and is not disclosed.** The inputs are generated
deterministically by the checker and are identical in every run, so you cannot
choose them — and you are not told their sizes. Optimize the stage for the
general case rather than for one shape: a decoder that only wins on one
codeblock size does not meet this milestone.

## 2. Read these local references

**Normative references, all local:**

- `src/core/codestream/ojph_codeblock_fun.h` — the dispatch seam. One method
  pointer per codec operation; `decode_cb32` is the one this milestone is
  measured on, and the library installs it at start-up.
- `src/core/codestream/ojph_codeblock_fun.cpp` — where it is installed.
- `src/core/coding/ojph_block_decoder32.cpp` — the implementation that runs
  today, and the definition of the correct output. `ojph_block_decoder64.cpp`
  is the same algorithm for the 64-bit (reversible) precision path.
- `src/core/coding/ojph_block_decoder.h` — the declared entry points.
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

**Out of scope — leave alone.** The 64-bit precision *block coder*
(`ojph_decode_codeblock64`, `encode_cb64`) has no optimized implementation
anywhere upstream, at any instruction set, so no milestone can ask for it. The
same goes for `mem_clear` and `find_max_val32/64`: they are memory-bound scans
that upstream's own optimized versions do not beat. Touching them is out of
scope and risks the byte-identity requirement.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/ojph` as the source tree
  and keep the upstream build system working
  (`cmake -S /app/ojph -B /app/build -DCMAKE_BUILD_TYPE=Release`).
- **This stage is measured on SSE through AVX2 — 256-bit registers at most.** The
  target host reports `sse sse2 ssse3 sse4_1 sse4_2 avx avx2` and **no
  AVX-512**; code that needs AVX-512 will not run there and a milestone whose
  changes use it fails. Compiler intrinsics are allowed for this stage
  (upstream's own implementation of it is intrinsics too); so is assembly,
  or portable C.
  Do not widen the whole build with `-march=native` or `-mavx512*`.
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
