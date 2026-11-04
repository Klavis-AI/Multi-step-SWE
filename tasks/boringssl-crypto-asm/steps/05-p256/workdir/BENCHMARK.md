# How to check your work

## Build

```bash
cmake -S /app/boringssl -B /app/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /app/build -j"$(nproc)" --target crypto crypto_test
```

A build of the whole tree is already in `/app/build`, so this is incremental.

## Behaviour must not change

The library has to produce exactly the bytes it produced before. All of these
are re-run, on hidden inputs, when your work is checked.

1. **The project's own suite** must gain no new failure:

   ```bash
   (cd /app/boringssl && /app/build/crypto_test)
   ```

   `crypto_test` opens its test-vector files through paths relative to the
   source tree, so it has to run from `/app/boringssl`. From anywhere else it
   aborts on the first data file -- and it aborts *without* reporting a failed
   test, which reads exactly like a clean run. Measured in this task's own
   environment: run from `/app`, `/app/build/crypto_test` exits 134 with
   `Could not open './crypto/fipsmodule/aes/aes_tests.txt'` and zero failed
   test lines; run from `/app/boringssl` it exits 0 and reports the tests it
   ran. The verifier has always run it from the source tree for this reason.

   A few tests may already fail in the tree you inherited — the optimized
   implementations they expect are the ones that are missing. Only *new*
   failures matter. The verifier runs the suites this milestone can
   reach: `AES*:AEADTest*:CipherTest*:GCMTest*:BNTest*:RSATest*:ECDSATest*:ECDSAP1363Test*:ECDHTest*:ECTest*`.

   The verifier also compares the set of tests that *ran* against the checkpoint
   this ladder starts from, the one with every optimized implementation
   removed: a test that checkpoint runs must still run here, so deleting,
   disabling or filtering out a test is a failure even though it reports
   nothing. Running *more* tests than that checkpoint is expected as stages are
   restored and is always acceptable.

2. **Every entry point must produce identical output**, checked by calling it
   rather than through the suite:

   ```bash
   /app/tools/measure /app/build --check
   ```

3. **Every entry point this task scopes** -- this milestone's and every
   earlier milestone's -- must produce byte-identical output to the versions
   you inherited. The verifier rebuilds the checkpoint this milestone starts
   from (the tree you were handed here), builds your tree beside it, and
   compares the two on the same random inputs. That checkpoint is the
   baseline, not the tree you deliver: work from an earlier milestone is not
   part of it. These primitives are bit-exact, so this is equality, not a
   tolerance. An approximation, or a reordering that changes a rounding,
   fails.

The verifier also compares `CMakeLists.txt` and everything under `cmake/`
against the tree you were given. The build configuration is frozen: this
milestone is about the code, not about the compiler flags.

## How your work is timed

Each entry point is timed **individually**, by calling the implementation the
library installs for it — not by timing a whole program. So a faster function
shows up at its full value instead of being diluted by everything around it, and
every function this milestone covers is timed, so none of your work goes
unnoticed.

**The in-scope set is exactly these entry points**, and this is how you
enumerate it:

```bash
/app/tools/measure /app/build --list
```

| entry point | what it is |
| --- | --- |
| `p256_point_mul` | `EC_POINT_mul` of a 256-bit scalar times a point |
| `p256_ecdsa_sign` | `ECDSA_sign` with a P-256 key |
| `p256_ecdsa_verify` | `ECDSA_verify` with a P-256 key |
| `p256_ecdh` | `ECDH_compute_key` between two P-256 keys |

A function left at its inherited speed is measured at 1.0, so "done" means all
of them are covered, not the easy ones.

Time them all with:

```bash
/app/tools/measure /app/build
```

`measure` pins its own core and takes the median of several windows, so run it
on an otherwise idle machine. Treat it as indicative: **the verifier's
measurement is the authoritative one**, and it draws its own inputs, so a kernel
tuned to one particular input value will not show a gain.

There is also an **end-to-end non-regression guard**: averaged over the timed
set of the whole workspace — including anything earlier milestones delivered —
the delivered tree may not be more than 5% slower than the checkpoint this
ladder starts from, the workspace with every optimized implementation removed.
That floor, and not the tree you were given, is the guard's baseline, so a tree
that has changed nothing passes it by construction. A microbenchmark win paid
for by a real slowdown elsewhere does not pay: a function left at or below the
speed it is measured against shows no speedup.

## What is not disclosed

The input draw. The verifier generates keys, messages and scalars from a random
seed it does not publish, so there is no fixed vector to look at and nothing
to special-case in the data. The workload's shape is not a secret and does not
vary: the same buffer lengths, operand sizes and alignment as
`/app/tools/measure` uses, every run.

The scalar multiplication is on the secret scalar, so it must stay constant-time in the same sense it is now: no branch and no memory index that depends on the scalar. The verifier does not test this -- it compares output bytes and runs the project's suite, and neither can see timing -- but the requirement stands: the constant-time property is part of what you inherited, and a version that branches on the scalar is a different implementation however fast it is.
