# Step 8 — Non-linear transform (inverse) and output format conversion

## 1. Implement this milestone

`/app/svt-jpeg-xs` is SVT-JPEG-XS at a pinned upstream commit with the
runtime-dispatched x86 implementations of several pipeline stages removed. The
library is complete and correct: it builds, it passes its own test suite, and it
encodes and decodes every picture bit-exactly using the portable C paths. It is
simply slow in the stages whose optimized code is missing.

**This milestone: make the inverse non-linear transform and the output format conversion faster, without changing a single output
byte.**

It covers these dispatch slots — the names are the ones in `Source/Lib/Decoder/Codec/decoder_dsp_rtcd.h`,
where each is a function pointer the library installs at startup:

| slot | what it does |
| --- | --- |
| `linear_output_scaling_8bit` | inverse scale, round and clamp one whole component plane to 8-bit |
| `linear_output_scaling_8bit_line` | the same for a single line |
| `linear_output_scaling_16bit` | the same for a whole component plane at 10/12-bit |
| `linear_output_scaling_16bit_line` | the same for a single line at 16-bit |
| `linear_output_scaling_16bit_line_msb` | the same, writing MSB-aligned 16-bit samples |

Every one of them currently dispatches to its portable C reference in
`Source/Lib/Decoder/Codec/`. Optimize them however you judge best —
hand-written `<immintrin.h>` intrinsics or plain C in a new file under
`Source/Lib/Decoder/ASM_AVX2/` (the directory globs `*.c`, so a reconfigure
picks it up), and install it for the slot. You are measured on the function the
library actually installs for each slot, against the portable C reference the
tree holds for it, so a faster installed implementation is acceptable.

The plane-level slots and the line-level slots are separately in scope: the
decoder uses the plane entry points on the ordinary path and the line entry
points where a slice boundary splits a component, so neither can stand in for
the other.

Earlier milestones must keep working. This workspace is cumulative: whatever is
already optimized here is still part of this step's baseline, and a regression
breaks both the earlier work and this step's baseline.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: for every input and every configuration
   the codec must produce byte-identical output to the tree you started from —
   not merely the cases you happen to test. An approximation, a fast path that
   is right "almost always", a reordering that changes rounding, or a result
   computed once and cached across calls all fail, however fast they are. Do not
   edit the C reference implementations or the headers that declare them; they
   define the correct output and are what your speedup is measured against.
2. **This stage must get faster**, on every slot in the table above. Only this
   stage is in scope. Optimizing some other part of the library is out of scope
   for this milestone, and so is effort that does not show up in the measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks and
the timing — so you can confirm where you stand before finishing.

**The inputs in scope are not disclosed.** They are drawn from an undisclosed
seed, over the same shape of input this stage sees in production: 1080p
pictures, at widths and lengths that are not always multiples of the vector
width. Optimize the stage for the general case rather than for one width or one
alignment: each call is timed at a configuration of its own, with freshly drawn
inputs, so a kernel that is fast only on the shapes you happen to have in front
of you is not a safe bet — and one that produces a wrong tail byte does not
satisfy this milestone.

## 2. Read these local references

**Normative references, all local:**

- /app/svt-jpeg-xs/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.h — the dispatch
  layer. Every slot above is declared here as a function pointer; the library
  installs the portable C reference for it at startup.
- /app/svt-jpeg-xs/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c — the
  installer, `setup_decoder_rtcd_internal(CPU_FLAGS flags)`, and the
  `SET_*` macros that choose a tier. `SET_ONLY_C` is what every slot above uses
  today.
- `/app/svt-jpeg-xs/Source/Lib/Decoder/Codec/NltDec.c` — the C references
  for all five.
- `/app/svt-jpeg-xs/Source/Lib/Common/Codec/SvtType.h` — `pi_t` and the
  image-buffer type two of them take.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands.
- `/app/tools/` — the per-function bench harness, its source, and a build script.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to build
  and measure locally.

The other dispatch slots in the encoder's and decoder's dispatch tables are
**not your concern**. Leave them alone: optimizing them is out of scope here and
risks the byte-identity gate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/svt-jpeg-xs` as the source
  tree, keep the upstream build system working (`cmake -S /app/svt-jpeg-xs -B
  <dir> -G Ninja -DCMAKE_BUILD_TYPE=Release`), and preserve the handoff
  documents.
- **This stage is measured at an AVX2 ceiling: 256-bit registers or narrower.**
  The reference implementations this task is calibrated against are AVX2, and
  the target host has no AVX-512, so AVX-512 code would neither be a fair
  comparison nor execute. SSE/SSE2/SSE4.1/AVX2 are all fine. A milestone that
  puts AVX-512 into a file this step owns is not acceptable — those files' text is
  scanned (the dispatch table that installs the slot included), so AVX-512 in a
  block that never compiles still counts (a mention inside a comment does not).
- The target host provides `avx avx2 bmi2 fma sse4_1 sse4_2`; BMI2 is permitted.
- The public network is available for official documentation and
  ordinary dependency lookup. **Do not fetch, copy, vendor, or transcribe
  SVT-JPEG-XS's own optimized implementation of this stage, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
