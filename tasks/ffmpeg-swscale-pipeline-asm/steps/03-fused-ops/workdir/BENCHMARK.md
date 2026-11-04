# Measuring this milestone

## Build

```sh
/app/build.sh              # configures /app/ffmpeg/build and builds it
```

The flag set is fixed and the task's verifier builds with the same one. The only
thing that changes between milestones is which optimized kernels are present.
`build.sh` also builds `tests/checkasm/checkasm`, which is both the correctness
harness and the benchmark.

## Is the behaviour still the same?

```sh
cd /app/ffmpeg/build
./tests/checkasm/checkasm --test='sw_*' 1        # exits non-zero on any failure
```

checkasm compares the implementation the library installs against the portable C
implementation on randomized inputs, at every CPU feature level the
machine has, and prints one line per group. It is upstream's own differential
suite for exactly these kernels, so a failure here is a real difference, not a
stylistic one.

The end-to-end behaviour is checked by the driver in `/app/tools`:

```sh
/app/tools/build_measure.sh                      # rebuild it after every change
/app/tools/measure_sws all 3                     # <config> <digest> <best ms>
```

`measure_sws` scales and converts ten format pairs end to end and prints a hash
of every destination plane as well as the time. The verifier does not require
the hash to be identical: it compares the planes against the base's, sample by
sample, within the declared per-configuration budget below -- the optimized
kernels are not all bit-identical to C, so a difference of a code value or two
is expected and passes. What fails is output that differs by more than the
budget, which is wrong output rather than a last-bit difference. The budgets
are the reference's own measured deviations from the base, so a correct tree is
expected to show `differing=0` on every line:

| configuration | budget (code values) |
| --- | ---: |
| `rgb24_to_yuv420p_downscale` | 2 |
| `yuv420p_to_rgb24_upscale` | 1 |
| `yuv420p_to_yuv420p_scale` | 2 |
| `yuv420p_to_yuv420p10le` | 1 |
| `rgba_to_yuv420p` | 2 |
| `yuv444p16le_to_yuv420p` | 2 |
| `yuv420p_to_rgb565` | 1 |
| `yuv420p_to_bgr24_unscaled` | 4 |
| `nv12_to_yuv420p` | 2 |
| `gbrp16le_to_yuv420p10le` | 1 |

Two further guards are the verifier's own, pass/fail, and have no command here:
it runs the pipeline **twice** on the same build and requires the two runs to
agree exactly -- a kernel that reads uninitialised or stale state can survive a
single comparison and fails this one -- and it times the pipeline end to end
against the tree this task started from, under "What is out of scope" below.

## How is speed measured?

Per function, by checkasm's own benchmark, on the implementation the library
installed:

```sh
cd /app/ffmpeg/build
./tests/checkasm/checkasm --bench --test='sw_*' 11
```

The table has one row per (function, implementation). The first row of each
function is the portable C reference, suffixed `_c`; every later row is an
implementation the library installed as the CPU feature level rose, and the last
one is what a real call would use on this machine. This module tags its rows
with the backend that answered, not with the CPU level alone:
`<function>_memcpy_c` is upstream's memcpy backend, present at every level and
not an implementation this milestone installs, and your backend's rows read
`<function>_x86_<cpu level>` -- the last row for a function is the one your tree
is timed on. Each in-scope function is measured on its own ratio, of

```
cycles(_c) / cycles(installed)
```

so it is measured on identical inputs, in the same process, with no dilution by
the rest of the pipeline -- a 4x kernel reads 4x rather than the fraction of a
frame it is. Neither whole-program time nor a count of rows is what is measured, and
the numerator is capped at the base build's own `_c` reading for the same
function, so a deliberately slow baseline cannot inflate anything.

`SCORED_FUNCTIONS.txt`, in this workspace, is the in-scope set: one function name
per line, and nothing outside it is in scope. To see how much of it you have covered,
compare the names that print an `_x86_` row against that list --
`--list-functions` prints every function checkasm knows about, which is far more
than this milestone's set, so it is not the enumeration to use.

## What is out of scope

The `measure_sws` end-to-end timings. They are a guard: a change that wins on
checkasm's buffers and loses on real strides fails the milestone. Both trees are
timed interleaved, in the same rounds, and the milestone fails when the
candidate/base ratio over the ten configurations exceeds **1.75** (lower is
better; the geometric mean of the per-configuration ratios, best of five rounds
each). It is checked against the tree this task started from rather than against
your own last state.
