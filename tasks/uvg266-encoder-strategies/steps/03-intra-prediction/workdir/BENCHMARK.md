# How to check your work

## Build

```bash
cmake -S /app/uvg266 -B /app/build -DCMAKE_BUILD_TYPE=Release -DUVG_BUILD_TESTS=ON
make -C /app/build -j"$(nproc)"
```

## Behaviour must not change

The encoder has to produce exactly the bytes it produced before. All of these
are re-run, with a fresh workload, when your work is checked.

1. **Upstream's own suite** must gain no new failure:

   ```bash
   /app/build/bin/run-greatest
   ```

   A few tests may already fail in the tree you inherited; only *new* failures
   matter. Record the current list before you start. Note that the
   suite's own speed tests only run when you filter for them (`run-greatest -s
   speed`); the differential tests run by default.

2. **Byte-identical encode** against the tree as you found it. The encoder is
   bit-exact, so this is equality, not a tolerance:

   ```bash
   python3 /app/tools/make_clip.py /tmp/clip.yuv 176 144 6
   /app/build/bin/uvg266 -i /tmp/clip.yuv -o /tmp/out.bin \
       --input-res 176x144 --input-fps 30 --preset medium --threads 0 --owf 0 --frames 6
   md5sum /tmp/out.bin
   ```

   Compare against the same command run on the tree you inherited. Turning the
   runtime dispatch off must change nothing either:

   ```bash
   /app/build/bin/uvg266 -i /tmp/clip.yuv -o /tmp/out2.bin \
       --input-res 176x144 --input-fps 30 --preset medium --threads 0 --owf 0 --frames 6 --no-cpuid
   cmp /tmp/out.bin /tmp/out2.bin && echo "dispatch changes nothing"
   ```

3. **The functions in this milestone** must produce byte-identical output to
   the versions you inherited, checked directly rather than through the whole
   encoder. `/app/tools/bench.c` does this: in `dump` mode it hashes each
   function's output over inputs regenerated from a seed you choose.

## How your work is timed

Each function in this milestone is timed **individually**, by calling the
implementation the encoder installs for it — not by timing a whole encode. A
faster function therefore shows up at its full value instead of being diluted
by the rest of the pipeline.

The in-scope set is exactly the functions the step instruction names. A
function whose installed implementation is the generic C shows no speedup, so
a milestone is "done" when every named function is faster.

Compile the practice harness against your own build; it links your library, so
it has to be rebuilt after every change:

```bash
gcc -O2 -o /tmp/bench /app/tools/bench.c \
    -I /app/uvg266 -I /app/uvg266/src -I /app/build/src -I /app/build \
    /app/build/lib/libuvg266.a -lm -lpthread

/tmp/bench list                          # the entries it can drive, by name
/tmp/bench dump  1 12345 FAMILY/NAME     # hash of one function's output
/tmp/bench bench 1 12345 FAMILY/NAME     # median ns per call
```

The in-scope set is exactly what the step instruction names, and the harness
drives every one of them. Its `list` may show entries beyond that set — they are
out of scope, whether or not they belong to your milestone's family. If `list` is
empty, or drives none of the functions your milestone names, the harness in your
workspace is stale — say so rather than guessing.

The number the measurement divides is not your absolute time: it times the same
function in this milestone's base build, in the same container, on the same two
pinned cores, and divides. Compare candidate against base inside one machine;
do not compare across machines.

**What is drawn, and what it is not.** The input buffers are
regenerated from a seed you are not given, and the seed also picks
the block shapes and sizes. What is published is the family the draw comes
from:

- the intra kernels are fed reference arrays whose samples are in
  `[0, 255]`, on luma blocks of 4, 8, 16 and 32. The directional kernel is
  drawn across the plain angular domain (modes 2 to 66); the wide-angle
  extensions, the multiple-reference lines and the ISP switch are **not**
  drawn, because the two implementations disagree on the wide-angle values and
  the workload stays in the domain where they are comparable.

Each function is called on many different buffers in turn, so an answer
memoized on one input is worthless. The timing is the **median** of many
samples, not the best, so a lucky fast run does not count.

Treat the harness's numbers as indicative: **the official measurement is the
authoritative one**, and it runs the same drivers with a different seed.
