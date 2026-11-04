# How to check your work

## Build

```bash
cmake -S /app/ojph -B /app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /app/build -j"$(nproc)"
```

## Behaviour must not change

The library has to produce exactly the bytes it produced before. All of these
are re-run, with held-out inputs, when your work is checked.

1. **Upstream's own suite** must gain no new failure:
   `ctest --test-dir /app/build` (build it with `-DOJPH_BUILD_TESTS=ON`; a few
   tests may already fail in the tree you inherited — only *new* failures count
   against you).
2. **Byte-identical round trips** against the tree as you found it:

   ```bash
   C=/app/build/src/apps/ojph_compress/ojph_compress
   E=/app/build/src/apps/ojph_expand/ojph_expand
   $C -i /app/workload/work.ppm -o /tmp/a.j2c -reversible true  -num_decomps 5
   $C -i /app/workload/work.ppm -o /tmp/b.j2c -reversible false -num_decomps 5 -qstep 0.02
   $E -i /tmp/a.j2c -o /tmp/a.ppm
   $E -i /tmp/b.j2c -o /tmp/b.ppm
   md5sum /tmp/a.j2c /tmp/b.j2c /tmp/a.ppm /tmp/b.ppm
   ```

   JPEG 2000 coding is bit-exact here, so this is equality, not a tolerance. An
   approximation, or a reordering that changes rounding, fails.
   The comparison is byte-exact, not a tolerance. Floating-point
   reassociation changes the last bits and fails it: do not enable
   `-mfma` or let FMA contraction into a float kernel.
3. **The function this stage covers** is compared byte for byte against the
   version you inherited, directly rather than through the whole codec, on
   codeblocks the checker generates.

Two further gates, both applied to your changes:

- **Instruction-set envelope: SSE through AVX2, 256-bit at most.** The target
  host has no AVX-512; changes that use it fail.
- **Held-out codeblocks** at awkward shapes — widths and heights that are not
  multiples of a stripe, 4x4, and long thin blocks — which are not in the sample
  workload. A kernel that assumes tidy alignment fails there.

## How your work is timed

This stage's function is timed **individually**, by calling the implementation
the library installs for it — not by timing a whole decode. That means a faster
kernel shows up at its full value instead of being diluted by the rest of the
pipeline.

The timed repetition hands the function the **same input** many times over —
that is what makes the ratio comparable — and an implementation that caches its
result keyed on the input is detected and fails the step: the same measurement
is re-run with the input re-drawn between repetitions, and a cached speedup
collapses there.

There is also an **end-to-end guard**: a full round trip must not get more than
5% slower than the portable baseline the checker builds alongside your tree.

```bash
/app/tools/measure /app/build      # encode, decode and total ms, lower is better
```

**The workload this runs is not disclosed** — not the image, not the encoding
settings, not the figure of merit. It is fixed and deterministic, so the number
is comparable between your own runs, but you cannot see which configuration is
measured and should not try to infer one. Optimize the stage for the general case.

`measure` takes the best of five repetitions, so run it on an otherwise idle
machine. Treat it as indicative: **the verifier's
measurement is the authoritative one**, and it times your stage's function
individually, on inputs you never see.
