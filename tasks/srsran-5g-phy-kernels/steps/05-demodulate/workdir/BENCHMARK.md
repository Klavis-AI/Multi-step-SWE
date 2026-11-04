# How to check your work

## Build

```bash
cmake -S /app/srsran -B /app/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DMARCH=x86-64-v3 -DENABLE_WERROR=OFF -DENABLE_MKL=OFF -DENABLE_ARMPL=OFF \
      -DENABLE_UHD=OFF -DENABLE_ZEROMQ=OFF -DENABLE_BACKWARD=OFF -DENABLE_PLUGINS=OFF
cmake --build /app/build -j4 --target srsran_ldpc srsran_channel_coding \
      srsran_crc_calculator srsran_polar srsran_short_block srsran_channel_modulation \
      srsran_channel_precoder srsran_ofh_compression log_likelihood_ratio srsvec srslog \
      srsran_support srsran_ran
```

The tree in `/app` is prebuilt, so this is incremental. A full rebuild from
scratch takes a few minutes at `-j4`.

## Behaviour must not change

The kernel has to produce exactly the bytes it produced before. JPEG-style
"looks the same" is not the standard here: this is integer and fixed-point DSP,
so the gate is **equality**, not a tolerance.

1. **The project's own suite** must gain no new failure:

   ```bash
   cmake --build /app/build -j4 --target channel_precoder_test dft_processor_test \
         dft_processor_ci16_test evm_calculator_test
   ctest --test-dir /app/build -R 'channel_precoder_test|dft_processor_test|dft_processor_ci16_test|evm_calculator_test'
   ```

   Some of upstream's PHY tests are not built here: they need test-vector files
   that are not published with the repository. A few of the tests that *are*
   built may already fail in the tree you inherited; only *new* failures are
   regressions.

2. **The scoped functions** must produce byte-identical output to the versions
   you inherited, checked directly rather than through the whole library. The
   harness does this: `dump` mode writes the raw output of one call, so you can
   diff your tree against a stashed copy of the tree you started from.

## How your work is timed

Your kernel's functions are timed **individually**, by calling the
implementation the library installs for each one — not by timing a whole slot.
That means a faster kernel shows up at its full value instead of being diluted
by the rest of the pipeline, and every function this milestone covers is timed,
so none of your work goes unnoticed. A function left unimproved is simply not
faster.

```bash
/app/tools/build_harness.sh /app/build
while read -r fn; do /app/tools/bench_phy "$fn" 12345 bench 101; done <<'EOF'
demod_qpsk
demod_qam16
demod_qam64
demod_qam256
EOF
```

`bench` prints `<fnid> <median_ns> <min_ns> <max_ns> <reps>`. **The median of
the per-repetition times is the reported figure**, not the minimum: a minimum
keeps spuriously fast outliers, and a ratio built on those reports a speedup
that was not earned. The harness pins nothing itself — run it under
`taskset -c 0,1` and on an otherwise idle machine.

The in-scope set for this milestone is exactly the 4 function ids
listed above -- the harness benches any id you give it, and these are yours.
That is the whole checkable target: when all of them are faster and all of
them still produce the bytes they used to, this milestone is done.

**The workload is not disclosed.** The harness generates its inputs from a seed
you can pass on the command line — different seeds give different draws from the
same family — and the checker uses seeds of its own. The family is fixed and is
the one the kernel is specified over: the block sizes, modulation orders and bit
widths named in the milestone are all it will ever be asked to handle. Optimize
the kernel for the general case; a version that only wins on a single
configuration does not cover the requirement.

Read the harness source at `/app/tools/bench_phy.cpp` to see exactly how each
function is driven. **The verifier's measurement is the authoritative one**: it
runs the same harness from its own copy, with seeds you do not see, against a
build of the tree you started from.

## Envelope

You are measured on AVX2 or narrower, single-threaded. Your milestone
instruction says the same; `/app/srsran` is not built with `-mavx512` and the
target host does not execute AVX-512. Threads are out for the same reason: the comparison
pins one core on both sides.
