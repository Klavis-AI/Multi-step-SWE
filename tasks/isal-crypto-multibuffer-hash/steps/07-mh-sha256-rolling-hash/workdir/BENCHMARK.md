# How this milestone is measured

## Build

```sh
cd /app/isal_crypto
make -f Makefile.unx lib -j"$(nproc)"      # -> bin/isa-l_crypto.a
```

That is the whole build. `make -f Makefile.unx` (no target) also builds the
shared library; `make -f Makefile.unx perfs` builds the project's own
performance drivers and `checks` builds its test binaries.

## What "behaviour must not change" means, as runnable commands

```sh
cd /app/isal_crypto
make -f Makefile.unx checks -j"$(nproc)" && make -f Makefile.unx check
bash /app/tools/measure.sh mh_sha256          # installed vs reference ratio, per unit
```

`make check` is the project's own suite: SHA-256 (16-way tree hash) and rolling_hash2 is covered by randomized
differential tests that compare the public API against the tree's scalar
reference at thousands of random lengths, plus parameter, flush and
update-path tests. All of them pass in the tree you were given and all of them
must still pass. The verifier runs the same suite and fails on any **new**
failure.

The harder check is the one the verifier runs: for every in-scope unit it hashes
a fixed, wide job stream through the public API and compares the whole digest
stream against the same stream hashed by the untouched base build. Lengths of
0, 1, odd values, block-size±1, multi-block and large buffers; whole-buffer and
multi-part submissions; lane-shortage and lane-overflow batches. Byte-exact or
the milestone is not satisfied.

## What is in scope

In scope are the two halves of one content-defined-chunking pipeline: the
16-segment tree hash's streaming path (update) and tail path (finalize), and
the rolling hash scan that feeds it. They are separate units because they are
separate kernels with separate headroom.


The in-scope set for this milestone is exactly:

- `mh_sha256_update`
- `mh_sha256_finalize`
- `rolling_hash2_run`

Every listed unit must be optimized: a unit left alone is not faster. This is
what `/app/tools/measure.sh` reports.

## How speed is measured

For each in-scope unit the verifier builds two trees, this one and a pristine base
with the library's multi-buffer hashing stages' optimized implementations removed, compiles the same
harness against each, and divides the medians:

    speedup(unit) = median_ns_base(unit) / median_ns_candidate(unit)

Both sides are measured in the same container, alternating short runs on the
same two pinned cores, so the ratio does not depend on how fast the host is.

**Both sides call the public API, not a named symbol.** Whatever the library
installs for the stage is what is timed, on both sides — assembly, intrinsics or
portable C. There is no requirement that the optimization be written in any
particular language or file layout.

`/app/tools/measure.sh` measures the same units, but it is a **progress
signal, not a forecast**: two things about it differ from the verifier's run, and
both of them matter.

- **Its inputs are a practice draw, not the measured one.** The measurement job
  stream is drawn privately inside the verifier, and it is a different and
  larger draw: the tool's batch sizes and message lengths are not the
  verifier's. The
  two agree only if your implementation is fast for the general case. One that
  is fast at exactly the tool's batch size and slow at another can read large
  here and small in the verifier's run.
- **Its denominator is the scalar reference inside *your* tree**, not a
  pristine base build. They are the same code, so that cannot flatter you — but
  it is also why the tool cannot show you the effect of moving the numerator's
  own reference, which is the reason the C reference implementations this
  milestone names are off limits.

The tool times the CPU time of the whole process — every thread of it, plus any
child process it waits on — the same quantity the verifier times, so a design
that parks work on a helper thread or a forked child is charged for it in both.

**Treat small differences as noise.** One run of this tool on a unit you have
not touched is not a stable number: on this host, pristine units — code nothing
in the tree changed — have been measured reading anywhere from 0.78 to 1.06
from one run to the next. Re-run before drawing a conclusion, and do not tune a
design to a single reading. The verifier's own figure is much less noisy: it
interleaves five rounds of three repetitions and takes the median of the round
ratios.

What the measurement draw always does: the `update` units are timed over a buffer of
tens to hundreds of kilobytes fed in a few calls; the `finalize` units are
timed as a batch of many finalize calls, each on a context whose untimed update
left it a near-maximal partial frame; and the rolling-hash unit scans a whole
buffer per call. The stage is measured on that shape and is expected to be good
at any shape.

## Reference arithmetic you cannot reproduce locally

- The verifier's denominator is a **separate pristine build**, so nothing you do
  to the reference in your tree can move it — but your tree is the
  *numerator*. Editing the C reference implementations the milestone names is
  therefore not a no-op: a behaviour-preserving edit moves your own side of
  the ratio. That is why they are off limits, and why `/app/tools/measure.sh`
  cannot show you the difference — its denominator is the same reference it
  times, so both of its columns move together and the ratio stays near 1.0.
- Each unit's figure is the **median over interleaved rounds**, not a best-of.
- **A unit measuring below 0.90x of the base build fails the milestone
  outright**, whatever the other units did.
