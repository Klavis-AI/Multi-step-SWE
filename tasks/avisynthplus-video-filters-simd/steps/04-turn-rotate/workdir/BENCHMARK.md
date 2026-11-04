# How to check your work

## Build

```bash
cmake -S /app/avisynthplus -B /app/build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build /app/build -j"$(nproc)"
```

The library ends up at `/app/build/avs_core/libavisynth.so`.

## Behaviour must not change

Every filter has to produce exactly the bytes it produced before. All of these
are re-run, on fresh configurations, when your work is checked.

1. **The operations this milestone covers** must produce byte-identical output
   to the versions you inherited, checked directly rather than through a whole
   script.
2. **Every pixel format the filter supports** must still work, including the
   16-bit, float and packed formats the same filter handles.
3. **Which formats are checked is not disclosed.** Assume all of them are.

A quick way to see the difference is to render the same script twice, once
with the library as you found it and once with your build, and compare frame
digests. `/app/tools/measure` prints a digest of every frame of the clip it is
given, so a `chain.avs`/`source.avs` pair with the same source on both sides
shows whether anything moved. `TurnLeft()` is a stand-in — write the operator
this milestone covers on that line in `chain.avs`, and leave it out of
`source.avs`:

```bash
cat > /tmp/chain.avs <<'EOF'
SetMaxCPU("avx2")
ColorBars(1280, 720, "YUV420P8").FadeIn(0, 32)
TurnLeft()
last
EOF
cat > /tmp/source.avs <<'EOF'
SetMaxCPU("avx2")
ColorBars(1280, 720, "YUV420P8").FadeIn(0, 32)
last
EOF
taskset -c 2 /app/tools/measure /app/build/avs_core /tmp/chain.avs /tmp/source.avs 32 3
```

The `last` line matters: a bare `SetMaxCPU(...)` or filter call as the final
line makes the script return void, and the driver then reports that the script
did not return a clip.

## What else is checked

Two more checks run with the behaviour and timing checks, and both are binary.

- **The capability envelope.** Every family in this ladder is measured on AVX2 or
  narrower, because the target host has no AVX-512. The files this milestone's
  implementation lives in -- the family's sources under `avs_core/filters/`,
  and any file you add beside them -- are scanned for AVX-512 constructs and
  for AVX-512 named as a compile target, and the flags your tree was compiled
  with are read from the build itself: a translation unit compiled with an
  ISA-raising flag its *filename* does not justify (the project's own
  `*_avx2.cpp` / `*_sse41.cpp` naming convention) fails the milestone, and a
  global `-mavx2` or `-march=native` is the clearest case of that.
- **End to end, with this milestone's family cut away.** Your tree is rebuilt
  with this family's implementation removed and re-times the in-scope
  operations through the result. If your tree **as delivered** is more than
  **35%** slower there than the same tree with the family cut away, the
  milestone fails -- the guard is against winning the isolated measurement at
  the pipeline's expense. It reads what you wrote: a tree that carries no
  implementation of this family has nothing for the cut to remove, and the gate
  reports that it had no input rather than comparing the tree with itself.

## How your work is timed

Each operation is timed **individually**, by running the filter through the
library's public C API and not by timing a whole script, so a faster kernel
shows up at its full value instead of being diluted by the source filter.

The driver builds the same clip twice — once with the operation and once
without it — and reports the **difference** between the two read passes. That
difference is the operation's own cost: the source filter, the frame cache,
frame allocation and graph construction are paid on both sides and cancel.
Every repetition rebuilds both graphs from scratch, because AviSynth caches
frames per filter instance and re-reading a cached clip costs nothing.

`/app/tools/measure` prints, among other things:

```
MIN_DIFF_MS        the fastest of the repetitions' differences, in ms
MEDIAN_DIFF_MS     the median of them
MIN_CHAIN_MS       the fastest read of the clip with the operation, in ms
MIN_SOURCE_MS      the fastest read of the clip without it, in ms
DIGEST_CHAIN       a hash of every plane byte of every frame of the chain clip
DIGEST_SOURCE      the same for the clip with the operation left out
```

The authoritative figure is `MIN_CHAIN_MS - MIN_SOURCE_MS` — the best reading of
each side taken separately, not the smallest of the per-repetition
differences. Both minima are one-sided, and this host is shared and busy, so
contention can only ever *add* time to a pass; a single repetition's
difference is not one-sided, and a stall during the source pass *deflates* it.
Run it pinned (`taskset -c 2 /app/tools/measure ...`) on an otherwise idle
machine, and treat it as indicative: **the final measurement is the
authoritative one**, and it times a fixed, undisclosed draw of this
milestone's operations.

**The measurement configuration is not disclosed** — not the frame sizes,
not the pixel formats, not the filter parameters. It is fixed and
deterministic, so your own numbers are comparable between runs, but you cannot
see which settings are measured and should not try to infer them. Optimize the
filter for the general case.
