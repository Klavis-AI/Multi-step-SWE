# Step 06 — Tree-hash SHA-1, and the SHA-1 + murmur3-x64-128 combination

## 1. Implement this milestone

`/app/isal_crypto` is Intel's isa-l_crypto at a pinned upstream commit with the
optimized implementation of one or more of its multi-buffer hashing surfaces
removed. The library is complete and correct: it builds, passes its own test
suite, and computes every digest correctly through the portable C reference
underneath the public API. It is simply slow in the places whose optimized code
is missing.

**This milestone: make SHA-1 (16-way tree hash) hashing faster through the multi-buffer API,
without changing a single output byte.**

It covers `isal_mh_sha1_update` / `isal_mh_sha1_finalize` and `isal_mh_sha1_murmur3_x64_128_update` / `isal_mh_sha1_murmur3_x64_128_finalize`.

The stage has no optimized implementation in this tree. Its multi-buffer
kernels, its lane-scheduling manager and the `*_multibinary.asm` dispatcher that
selects among instruction-set tiers at load time are gone, and the public
symbols resolve to the scalar C in `mh_sha1/mh_sha1.c` and the `*_base.c`
files beside it, and in `mh_sha1_murmur3_x64_128/mh_sha1_murmur3_x64_128.c`
with its own `*_base.c` files. Optimize it
however you judge best — assembly in new files registered in
`mh_sha1/Makefile.am`, C with intrinsics, or plain portable C of your own. What
is measured is the function the library actually installs, so any route that
makes it faster is acceptable.

Earlier milestones keep working. This workspace is cumulative: optimizations you
made in earlier steps are still here and are still part of this step's baseline,
so a regression breaks this step as well as the earlier one.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: every digest must be byte-identical to
   the tree you started from, for every input length, every submission flag and
   every lane occupancy — not merely for the cases you happen to test. A fast
   path that is right *almost* always is a failure, however fast it is. Do not
   edit the C reference implementations (`mh_sha1/mh_sha1.c`,
   `mh_sha1/sha1_for_mh_sha1.c`, `mh_sha1/mh_sha1_*_base.c`,
   `mh_sha1/mh_sha1_base_aliases.c`,
   `mh_sha1_murmur3_x64_128/mh_sha1_murmur3_x64_128.c`,
   `mh_sha1_murmur3_x64_128/mh_sha1_murmur3_x64_128_*_base.c`,
   `mh_sha1_murmur3_x64_128/murmur3_x64_128_internal.c`,
   `mh_sha1_murmur3_x64_128/mh_sha1_murmur3_x64_128_base_aliases.c`); they
   define the correct output.
2. **This stage must get faster** on the workload described in
   `/app/BENCHMARK.md`. Only this stage is in scope. Optimizing some other part
   of the library does nothing for this milestone, and neither does effort that
   does not show up in the measurement. The measured inputs are drawn privately
   at measurement time and are **not** the ones `/app/tools/measure.sh` uses:
   that tool is a progress signal on a smaller practice draw, and this stage
   covers the general case — fast from a single job to thousands of them, at
   message lengths from a few bytes to megabytes.

## 2. Read these local references

**Normative references, all local:**

- `/app/isal_crypto/include/isa-l_crypto/mh_sha1.h` and
  `/app/isal_crypto/include/isa-l_crypto/mh_sha1_murmur3_x64_128.h` — the
  public API. The deprecated `mh_sha1_*` / `mh_sha1_murmur3_x64_128_*`
  aliases and the `isal_mh_sha1*` / `isal_mh_sha1_murmur3_x64_128_*` names
  are the same functions; the harness drives the `isal_` names.
- `/app/isal_crypto/mh_sha1/` and `/app/isal_crypto/mh_sha1_murmur3_x64_128/`
  — the stage, two directories: the 16-way tree hash and the SHA-1 +
  murmur3-x64-128 combination built on it. The public symbols resolve to each
  directory's scalar C reference — `mh_sha1/mh_sha1.c` with
  `sha1_for_mh_sha1.c` and the `*_base.c` files beside it,
  `mh_sha1_murmur3_x64_128/mh_sha1_murmur3_x64_128.c` with
  `murmur3_x64_128_internal.c` and its `*_base.c` files — through upstream's
  own `*_base_aliases.c` shim. Each directory's own `Makefile.am` is where a
  new source file for it is registered.
- `/app/BENCHMARK.md` — the workload, how it is measured, and the exact
  commands to reproduce a measurement locally.
- `/app/tools/measure.sh` — the practice measurement tool. Run
  `bash /app/tools/measure.sh STAGE` after a build to see your current ratios.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to
  build and measure locally.

The two `_init` entry points are out of scope: they are plain C in every configuration, so there is no faster path to require. They are left free.

## 3. Meta requirements

- The implementation budget is 10,800 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/isal_crypto` as the source
  tree and keep the tree's own build working:
  `cd /app/isal_crypto && make -f Makefile.unx lib -j"$(nproc)"`.
- **This stage is measured on the capability set this target host executes.**
  The host runs SSE2, AVX2 (256-bit registers) and the SHA-1/SHA-256 extensions
  (`sha_ni`). It has no AVX-512, no VAES, no GFNI and no SHA-512 or SM3
  extensions, so code that requires any of those faults at run time and fails
  the run. This is also enforced statically: the C/C++ sources you
  changed under this stage's own directories are scanned for intrinsics,
  register names and `target` attributes that ask for one of those capabilities
  — including in code that never executes — and build flags that ask for one
  are rejected wherever they appear. Assembly is not token-scanned; assembly
  that executes is caught by the fault. The comparison is single-threaded: the
  timed figure is the CPU time of the whole process -- every thread of it, and
  any child process it waits on -- so moving work onto a helper thread or a
  forked child does not take it out of the measurement. The task's reference
  solution is  upstream's own implementation of
  this stage, and its multibinary dispatcher installs the best tier this host
  reports support for; a candidate that uses the same capabilities is its peer,
  and one that reaches for a capability the host does not have is not.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  isa-l_crypto's own optimized implementation of this stage, or any other
  project's, from the internet or from a package mirror.** The optimization
  must be yours. Do not inspect `/tests` or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
