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
| `aes_key128` | `AES_set_encrypt_key`, 128-bit |
| `aes_key256` | `AES_set_encrypt_key`, 256-bit |
| `aes_encrypt128` | `AES_encrypt`, 128-bit key |
| `aes_encrypt256` | `AES_encrypt`, 256-bit key |
| `aes_decrypt128` | `AES_decrypt`, 128-bit key |
| `aes_decrypt256` | `AES_decrypt`, 256-bit key |
| `aes_ecb128` | `AES_ecb_encrypt`, a single 16-byte block |
| `aes_cbc128` | `AES_cbc_encrypt` over a buffer |
| `aes_ctr128` | `AES_ctr128_encrypt` over a buffer |

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

The key schedule is measured by what the key does, not by the bytes it leaves in `AES_KEY`: two correct implementations may lay the round keys out differently.
