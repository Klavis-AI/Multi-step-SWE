# How to check your work

## Build

```bash
cmake -S /app/svt-jpeg-xs -B /app/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DBUILD_TESTING=ON -DCMAKE_OUTPUT_DIRECTORY=/app/build/out
ninja -C /app/build
```

`-DBUILD_TESTING=ON` also builds `SvtJpegxsUnitTests`; `-DCMAKE_OUTPUT_DIRECTORY`
keeps the binaries out of the source tree. The measurement build uses the same
options.

## Behaviour must not change

The library has to produce exactly the bytes it produced before. All of this is
re-run, with fresh inputs, when your work is checked.

1. **The project's own suite** must gain no new failure:

   ```bash
   LD_LIBRARY_PATH=/app/build/out /app/build/out/SvtJpegxsUnitTests
   ```

   The tree you inherited passes its own suite with zero failures, so a new
   failure is a real regression; only *new* failures are regressions.

2. **Byte-identical encode and decode** against the tree as you found it. JPEG XS
   is a fully specified integer codec, so this is equality, not a tolerance:

   ```bash
   /app/build/out/SvtJpegxsEncApp -i /app/samples/1080p_422_8b.yuv -w 1920 -h 1080 \
        --colour-format yuv422 --input-depth 8 --bpp 3 -n 1 --lp 0 -b /tmp/o.jxs --no-progress 1
   md5sum /tmp/o.jxs
   /app/build/out/SvtJpegxsDecApp -i /tmp/o.jxs -o /tmp/o.yuv -n 1 --lp 0 >/dev/null
   md5sum /tmp/o.yuv
   ```

   For a 10-bit configuration the input has to be a 10-bit picture; the point is
   that the digests match the ones the tree you inherited produced. Save them
   before you start, and do the same for `--colour-format yuv420` and
   `yuv444` — all six combinations are checked.

3. **The functions in this stage** must produce byte-identical output to the
   versions you inherited — see the harness below, which compares them directly
   rather than through the whole codec.

4. **A function must not be fast only because it remembers.** The timed batch
   calls one input repeatedly, so an implementation that caches results keyed on
   its input could read fast while staying byte-exact everywhere. The check
   therefore re-times every scoped function with a freshly drawn input on every
   call and compares the two ratios: a step whose ratio collapses under fresh
   draws — below 0.30 of its measured value, in three consecutive guard sets —
   fails. The measurement itself is unchanged; this is a separate pass,
   and a kernel whose cost does not depend on its input's content never sees it.

## How your work is timed

Your stage's functions are timed **individually**, by calling the implementation
the library installs for each one — not by timing a whole encode. A faster
function therefore shows up at its full value instead of being diluted by the
rest of the pipeline, and every slot this stage covers is timed, so none of your
work goes unnoticed.

The harness is in `/app/tools/bench/` and can be rebuilt against your tree:

```bash
bash /app/tools/build_bench.sh /app/svt-jpeg-xs /app/build /app/tools/bench /app/tools/codecbench
```

`codecbench` takes `<seed> <rounds> <c|avx2> <slot> ...` and prints, per slot,
one `D <slot> <round> <digest>` line per correctness round (each round draws
fresh inputs) and one `T <slot> <ns>` line — the median time of one call, from
repeated batches; the commands below pin it to two cores:

```bash
taskset -c 0,1 /app/tools/codecbench      7 4 avx2 idwt_horizontal_line_lf16_hf16   # your tree
taskset -c 0,1 /app/tools/codecbench-base 7 4 avx2 idwt_horizontal_line_lf16_hf16   # the tree you started from
```

`codecbench` selects the configuration each timed call runs at from the seed you
pass, over the same table of shapes the correctness rounds walk. Your number is
therefore a sample across those shapes — a kernel that is only fast at one width reads
differently at another, which is what the general-case requirement is about.

`/app/tools/codecbench-base` is that same harness linked against the tree as you
received it, and is a fixed comparison point. The speedup for a slot is
taken **inside your own tree's binary**: the time of the portable C reference
your tree installs at the `c` profile, divided by the time of the implementation
it installs at the `avx2` profile, on the same inputs — the build
layout cancels between the two, which is why a slot left on the portable C path
reads 1.0. A separate copy of the base tree is a sabotage guard rather
than the divisor: its reference is substituted for yours only if your own has
moved by more than 2x, and both figures are printed either way. That is also why
the reference must stay as it is. The seeds are only compared within your own
run.

## The in-scope set, and how to enumerate it

This milestone covers 4 names:

```
idwt_horizontal_line_lf16_hf16
idwt_horizontal_line_lf32_hf16
idwt_vertical_line
idwt_vertical_line_recalc
```

A slot you leave on the portable C path reads a speedup of 1.0, so "done" means
every name in that list is covered — check it with the harness above, which
prints them in exactly this order. The measurement inputs are drawn from a
fixed seed over sizes that are not always
multiples of the vector width, so a kernel that only handles whole vectors is
not done.

There is also an **end-to-end guard**: a full encode must not be more than 10%
slower than the tree with none of these stages optimized. That tree is not in
`/app`, but its applications are at `/app/tools/l0/`, so you can time both.
Those binaries were linked against a build directory that no longer exists, so
they need their own directory on the library path — the first command below
does that:

```bash
LD_LIBRARY_PATH=/app/tools/l0 time taskset -c 0,1 /app/tools/l0/SvtJpegxsEncApp -i /app/samples/1080p_422_8b.yuv \
     -w 1920 -h 1080 --colour-format yuv422 --input-depth 8 --bpp 3 -n 8 --lp 0 \
     -b /tmp/a.jxs --no-progress 1
time taskset -c 0,1 /app/build/out/SvtJpegxsEncApp -i /app/samples/1080p_422_8b.yuv \
     -w 1920 -h 1080 --colour-format yuv422 --input-depth 8 --bpp 3 -n 8 --lp 0 \
     -b /tmp/b.jxs --no-progress 1
```

The timed run uses a different picture and takes the best of three runs, but the
comparison is the same one. Treat it as the real-pipeline check: the
per-function numbers are what this milestone covers, but they may not be bought
at the pipeline's expense.

`codecbench` reports a median and the commands above pin it to two cores, so run
it on an otherwise idle machine. Treat your own number as indicative: **the
official measurement is the authoritative one**, and it draws its own inputs.
