# Step 2 — Needleman-Wunsch global alignment, score only

## 1. Implement this milestone

`/app/parasail` is the parasail sequence-alignment library at a pinned upstream commit, with the **vectorized** implementations of one or more alignment families removed.  The library is complete and correct: it builds, it passes its own test suite, and it aligns every pair correctly, because the project's own non-vectorized reference implementation of each family is still in `src/` and the dispatchers fall through to it.  It is simply slow in the places whose optimized code is missing.

**This milestone: make Needleman-Wunsch global alignment faster, without changing a single output value.**

Global alignment initialises both edges with gap penalties, reads the result at the bottom-right corner and never clamps at zero, so both the vector prologue and the saturation bounds differ from the local case.

The family currently has no optimized implementation.  Its dispatchers in `src/` no longer select anything: each one falls straight through to its own non-vectorized reference, so every vectorized entry point of the family answers from the portable C code.  Optimize it however you judge best -- a new kernel in a new file wired into the build and the dispatcher, or the portable C itself.  **You are measured on the entry point the library actually installs for each name**, so any route that makes it faster is acceptable.

Earlier milestones must keep working.  This workspace is cumulative, so work you did in an earlier step is still here and is still part of this step's baseline; a regression there breaks this step too.

## 2. Scope

The in-scope set is every public entry point of this family under the following forms:

| form | what it returns |
| --- | --- |
| `nw_<strategy>_<width>` | score only |
| `nw_table_<strategy>_<width>` | the score table |
| `nw_rowcol_<strategy>_<width>` | the last row and column |

`<strategy>` is `striped`, `scan` or `diag`, and `<width>` is `16`, `32` or `64`.  Not every name those admit is in scope: the `diag` names upstream's own implementation disagrees with upstream's own reference on are outside every milestone, so `/app/SCOPE.txt` -- not the table alone -- defines the in-scope set.

That is **27 entry points** for this milestone.  Every one of them is in scope: a function left on the portable path is not faster, so "done" means every one of them is covered.  They are timed by calling each entry point **individually**, on the same inputs in your tree and in a reference build, so a faster kernel shows up at its full value instead of being diluted by the rest of the library.

**Out of scope for every milestone:**

- the `_profile_` entry points -- parasail has no non-vectorized profile implementation, so there is nothing to compare them against;
- the `_sat` (saturating-retry) wrappers, which are thin retries over the widths below;
- the 8-bit widths -- the reference is 32-bit, and an 8-bit kernel saturates on any sequence long enough to time, so the comparison would be a property of the input rather than of your code;
- the `diag` strategy where upstream's own implementation disagrees with upstream's own reference.  Where it is in scope it is in the table above and in `/app/SCOPE.txt`; where it is not, implementing it has no effect either way.

## 3. What success means

1. **The library must behave exactly as it does now.**  This is an optimization task, not a behaviour-change task: every entry point must return byte-identical results to the tree you started from, for every input -- the score, the end positions, the statistics, the score table, the last row and column and the decoded CIGAR, whichever of them the entry point produces.  An approximation, a fast path that is right "almost always", or a reordering that changes any of them is a failure, however fast it is.
2. **Each entry point you implement keeps its documented solution width.**  `..._16` must answer in 16-bit, `..._32` in 32-bit, `..._64` in 64-bit; a name that advertises one width and answers in another is a failure, and the result the library returns says which width it used.  This is asked about the entry points your own code answers: the tree you started from answers every removed family from its portable reference, and its `nw_striped_16` reports 32 bits by construction, so an entry point you leave where it is is not a width failure -- it is an unimplemented one, and it shows as no speedup instead.
3. **Do not change what the non-vectorized reference implementations return.**  They are `src/<family>.c`, `src/<family>_scan.c` and the `_table` / `_rowcol` / `_trace` variants the build produces from them.  They define the correct output, and the reference build this is measured against is built from the pinned upstream sources, not from yours.
4. **This family must get faster.**  Only this family is in scope.  Optimizing some other part of the library does nothing for this milestone.

5. **This milestone fails if the rest of the library gets more than twice as slow, or if your own entry points are more than twice as slow as the portable reference on a draw shape the in-scope set does not use.**  The measurement harness times your tree against the reference build on a 257x997 draw over the families this milestone does not own and over this milestone's own entry points.  A kernel tuned to one sequence shape is not a passing solution.

## 4. Read these local references

- `/app/BENCHMARK.md` -- the exact build, behaviour-check and timing commands, and how to enumerate the in-scope set.
- `/app/tools/pbench` -- the measurement tool.  It loads whichever `libparasail.so` you point it at and calls the library's own public entry points; `/app/BENCHMARK.md` shows how to use it.
- `/app/WORKSPACE_HANDOFF.md` -- what is in the workspace and how to rebuild it.
- `src/sw.c`, `src/nw.c`, `src/sg.c`, `src/*_stats.c`, `src/*_trace.c`, `src/*_scan.c` -- the non-vectorized references, and the clearest statement of what each entry point must return.
- `src/*_dispatch.c` -- the dispatcher layer.  Each vectorized name has a `parasail_<name>_dispatcher()` that assigns a global `parasail_<name>_pointer` on first call and then calls through it.  That pointer is what "the entry point the library installed" means here.
- `parasail.h` -- the declarations, including the ones for entry points this tree no longer implements.
- `src/meson.build`, `Makefile.am`, `CMakeLists.txt` -- the three build descriptions.  A new source file has to be registered in each you want to keep working; the build this is checked with is meson.

## 5. Meta requirements

- The implementation budget is 7,200 seconds.  `bash /app/timer.sh` prints how much of it is left; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace.  Keep the source tree at `/app/parasail`, keep the build system working (`meson setup /app/build /app/parasail --buildtype=release && ninja -C /app/build`), and preserve the handoff documents.
- The instruction-set ceiling is **AVX2 (256-bit)**.  The reference implementation uses 256-bit registers where it can, and the target host has SSE2, SSE4.1 and AVX2 but no AVX-512 -- code that needs AVX-512 cannot be measured here at all.  Compiler intrinsics are allowed and expected; this is a vectorization milestone.  The ceiling is enforced: the files carrying this milestone's entry points are scanned, and a tree whose own code contains AVX-512 fails, whether or not that code would ever execute here.  The scan is textual and case-insensitive: `_mm512`, `__m512`, `__mmask`, `zmm` and the string `avx512` are each a failure on their own, so a preprocessor guard such as `#ifdef __AVX512F__` counts too.  The comparison is only meaningful on the machine the reference was built for.
- You do not have network access.  **Do not fetch, copy, vendor or transcribe parasail's own optimized implementation of this family, or any other project's, from the internet or a package mirror.**  The optimization must be yours.  Do not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed, the speedup you measured locally, and how to rebuild.

