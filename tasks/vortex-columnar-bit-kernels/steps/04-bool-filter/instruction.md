# Step 4 — Boolean filter via PEXT

## 1. Implement this milestone

`/app/vortex` is the Vortex columnar format library at a pinned upstream commit
with the optimized implementation of some of its bit-level kernels removed. The
library is complete and correct: it builds, it computes what it should on every
input, using the portable code paths it falls back to, and it does not pass its
own suite clean: the failures it starts with are pre-existing. It is
simply slow in the places whose optimized code is missing.

**This milestone: make filtering a boolean array by a bitmap mask faster,
without changing a single output bit.**

It covers:

1. `ArrayRef::filter(&self, mask: vortex_mask::Mask)` — applied to a `BoolArray`

Above a density threshold it bottoms out in
`vortex_array::arrays::bool::compute::filter::filter_bitbuffer_by_mask`, which
installs only the portable compaction (`filter_pext_fallback`): it compacts the
selected bits of each 64-bit word through a software parallel-bit-extract with
a byte lookup table.

Optimize it however you judge best — vector intrinsics, a better scalar loop,
whatever wins — as long as the result is bit-identical for every input. You are measured on the implementation the library actually installs for
this path, so any route that makes it faster is acceptable.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: every entry point must return byte-identical
   output to the tree you started from, for every input and every configuration —
   not merely for the cases you happen to test. An approximation, a fast path that
   is right "almost always", or a reordering that changes a result is a failure,
   however fast it is. Do not edit the portable implementation to make it agree
   with a new one; it defines the correct answer.
2. **This stage must get faster** on the measured set named in
   `/app/BENCHMARK.md`. Only this stage is in scope. Optimizing some other part of the
   library does nothing for this milestone, and neither does effort that does not
   show up in the measurement.

## 2. Read these local references

**Normative references, all local:**

- `/app/vortex/vortex-array/src/arrays/bool/compute/filter.rs` — the stage. `filter_bitbuffer_by_mask` is the dispatched entry point,
  `filter_inner` is the shared per-word loop, and `filter_pext_fallback` beside
  them is the portable implementation that defines the correct answer.
- `/app/vortex/vortex-array/src/arrays/bool/mod.rs` — `BoolArray`, and the `FilterReduce` implementation that routes a dense mask
  here and a sparse one to the set-bit path.
- `/app/vortex/vortex-buffer/src/dispatch.rs` — `CpuKernel`, the crate's one-time CPU-feature dispatch primitive, with the
  contract this codebase uses for a runtime-selected kernel.
- `/app/BENCHMARK.md` — how to build, how behaviour is checked, how speed is
  measured, and the exact benchmark set.
- `/app/WORKSPACE_HANDOFF.md` — how to build and measure locally, and what the
  starting checkpoint is.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/vortex` as the source tree
  and keep the upstream build system working
  (`cargo build --release -p vortex-buffer -p vortex-mask -p vortex-array`).
  Keep the crate names and the public API of the entry points above intact: the
  checker links a binary against this tree and calls them.
- **No new dependencies.** The workspace's dependency set is fixed; the build
  runs offline against the registry the tree shipped with, so a crate that was
  not already in `Cargo.lock` cannot resolve and the step fails to build.
- **This stage is measured single-threaded, on the target host's instruction
  set.** It provides SSE2, AVX2, BMI2 and POPCNT, and has no AVX-512.
  The reference implementation for this stage uses a 64-bit BMI2 instruction and
  no vector width above it. An implementation that requires AVX-512 will never be
  selected on this host and is out of scope, and so is one that spawns
  threads: the metric is wall-clock time for one call. Both are checked rather
  than assumed. Your source is scanned -- this milestone's own files that
  you changed, and any source file you add under `/app/vortex`'s `src/` trees,
  which is where a kernel can go instead of into a file this milestone names (a
  tree that changed and added nothing cannot be out of envelope, and is not
  scanned) -- for an AVX-512 feature or intrinsic with no runtime feature probe
  beside it in the same file, and for a thread spawn. It reads code, not bytes:
  a mention in a comment, a string or a `#[cfg(test)]` module is not an
  implementation, and none of the three fails the scan. A real one does.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  Vortex's own optimized implementation of this stage, or any other project's,
  from the internet or a package mirror.** The optimization must be yours. Do not
  inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
