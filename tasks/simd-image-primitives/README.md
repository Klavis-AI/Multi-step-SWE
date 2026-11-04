# simd-image-primitives

Optimize the [Simd](https://github.com/ermig1979/Simd) computer-vision
primitive library one algorithm family at a time: portable C++ in
`src/Simd/SimdBase*.cpp` on one side, hand-written SSE4.1/AVX2 kernels in
`src/Simd/Simd{Sse41,Avx2}*.cpp` on the other. Eight cumulative steps, each
measured by per-function speedup gated on bit-exact output.

- **Repository**: `https://github.com/ermig1979/Simd`, MIT.
- **Pin**: release `v7.2.165` = `0dc87019c7030e438df1b750ce7d26f356b23cd1`.
- **Shape**: sibling partition. The eight families are independent file sets,
  they are reorderable, and together they are the library's classical-CV core.
  The order below is the seed's; it is not a dependency chain.

## The ladder

`L8` is the pinned release; `L0` is the same tree with all eight families
removed. Each `Lk` restores family *k* only, so step *k*'s oracle is
`git diff L(k-1) Lk` — a patch that can neither conflict with its predecessor
nor revert it, because both endpoints exist as built, tested trees.

| step | family | entry points in scope | oracle churn (insertions) |
| --- | --- | -: | -: |
| 01-resizer | Resizer (byte/float/int16 bilinear, area, bicubic, nearest) | 1 | 10,419 |
| 02-blur | GaussianBlur, GaussianBlur3x3, RecursiveBilateralFilter, MedianFilter×4 | 7 | 9,857 |
| 03-edges | Sobel×6, Laplace×3, Contour×3, Texture×4 | 16 | 6,391 |
| 04-cascade | Detection Haar/LBP ×6, Integral | 7 | 4,276 |
| 05-hog | HogDeinterleave, HogDirectionHistograms, HogExtractFeatures, HogFilterSeparable | 4 | 4,000 |
| 06-warp | ShiftBilinear, TransformImage | 2 | 7,328 |
| 07-stats | Statistic, Conditional, Histogram, SquaredDifferenceSum, AbsDifference | 28 | 12,756 |
| 08-background | Background, Binarization, Segmentation, Fill, Operation | 21 | 9,084 |

The scope of each step is **derived from its oracle patch, not written by
hand**: a public entry point is in scope exactly when the patch rewires its
dispatch ladder to call an `Avx2::`/`Sse41::` body, and it is measured under the
test group of the project's own framework that calls it. A name-similarity map assigns
`SimdSquaredDifferenceSum16f` to the statistics family, but its dispatch branch
is never rewired — the symbol is also defined by `SimdAvx2Float16.cpp`, an
out-of-scope family — so the statistics patch does not implement it and it is
not measured.

## The instrument

Measurement and correctness come from the same run of the project's own test
framework, in its auto-check mode:

```sh
Test -m=a -tt=1 -wt=1 -r=<tree> -w=<W> -h=<H> -c=8 -mt=200 -fi=<group>... -ot=report.txt
```

`-ot=` writes a table with one row per measured function (or per
function-and-configuration, for the class-based APIs) and one column per
implementation:

- **`API`** — the entry point the library *dispatches* to, `SimdSobelDx`.
- **`Base`** — the scalar reference `Simd::Base::SobelDx`.

Both are timed by the same harness, in the same process, on the same input, so
the measured ratio `Base/API` compares the candidate's installed implementation
against the reference that is sitting beside it in the candidate's own tree.
That choice matters twice over:

- **The denominator cannot be moved.** `src/Simd/SimdBase*.cpp`, `src/Test/`
  and `prj/` are restored from the pin before the tree is built, so the scalar
  reference is pristine no matter what the candidate did to it, and the
  "install a slow reference at feature level zero" exploit has nothing to bite.
- **There is only one build.** Nothing is compared across two compiles, so
  code-layout noise does not enter, and the build uses exactly the
  candidate's tree.

The measurement runs **three times and takes the per-row
median**, which discards a host-contention spike in one run instead of
rectifying it upward; `-mt=200` gives each reported number ~200 ms of
repetitions to average over. `W` must be a multiple of `SIMD_ALIGN` (32 in an
AVX2 build, 64 with AVX-512) or the framework records the measurement under
its unaligned key and the row reads zero.

`-m=a` is also the correctness gate: before timing each pair it compares the
installed implementation against the scalar reference with exact equality
(`differenceMax = 0`) at several sizes and alignments, and terminates the run
on the first mismatch. A failing run writes no report at all, so a mismatch
yields no measurement rather than a plausible number.

## Correctness, and what is frozen

`correctness` is binary and gates the measurement. It requires: the family
run completes with no mismatch, the project's whole suite — every other
group, same instrument — runs to completion with no mismatch, and the code this
step's candidate changed stays inside the capability envelope below. The second
is a broad guard against collateral damage, not evidence that the step did its
job; the third is a fairness rule, not a measurement.

The candidate owns `src/Simd/`. `src/Test/**`,
`src/Simd/SimdBase*.{cpp,h}` and `prj/**` are restored from the pinned release. New
implementation files need no registration: the CMake source list is a
`GLOB_RECURSE` on `src/Simd/Simd<Prefix>*.cpp`.

The autotest hooks for **all eight families** are stripped from the restored
`src/Test`, not just the ones the current step's base lacks. The per-ISA hooks
call symbols by name, and a candidate is allowed to implement one ISA and not
another; a hook naming an ISA it did not write would fail a legitimate
solution at compile time. The framework's per-family hook — installed
implementation against scalar reference, bit for bit — is untouched and is
what the gate uses.

**The one leak the strip cannot close.** A family-private header is included by
`src/Simd/SimdLib.cpp`, the dispatch file, so the stripper's header pass leaves
the header whole: it cannot gut a header the surviving dispatch file is compiled
against. Three are known from the rollout's own reads —
`SimdResizer.h` (step 1), `SimdGaussianBlur.h` and
`SimdRecursiveBilateralFilter.h` (step 2) — and the same shape may hold for
other family headers a restored source includes (`SimdWarpAffine.h`,
`SimdDetection.h`, `SimdIntegral.h`); whichever of them carries ISA
declarations keeps them. What such a header keeps is the
in-envelope half — the `namespace Sse41` / `namespace Avx2` class declarations
the family's own implementations, i.e. the restored tree the oracle records, are
compiled against — and that is an accepted residue rather than a defect to
remove: deleting them would break the tree that restores the family (the step-1
oracle's `new ResizerNearest(param)` and `ResizerByteBilinear::EstimateParams()`
are compiled against exactly these declarations, `solution/oracle.patch`).
Everything the measurement build does **not** compile — `namespace Avx512bw`,
`Avx512vnni`, `AmxBf16`, `Neon`, `Sve2`, `Hvx` in those same headers — is removed
even there, because nothing on this host needs it (their own files are not in the
build and every reference to them is under an undefined `SIMD_<ISA>_ENABLE`
guard). The stripper prints what it removed and what it left, and the residue is
declarations only — signatures and member layout, no kernels.

## Capability envelope

The measurement host executes SSE4.1 and AVX2 and **no AVX-512**, so both the
environment and the measurement build with `-DSIMD_AVX512=OFF
-DSIMD_AVX512VNNI=OFF`: an AVX-512 body would never be selected on this CPU and
is not compiled. `SIMD_SYNET=OFF` drops the neural-network backend, which is
out of scope. The per-ISA file groups carry the project's own compile flags
(`prj/cmake/x86.cmake`), and the build system is restored from the pin, so the
envelope is the reference's own. `v7.2.165` ships SSE4.1 *and* AVX2 for every
scoped family except two step-7 functions — `SimdChangeColors` and
`SimdNormalizeHistogram` have only AVX-512BW/AMX-BF16 bodies upstream, so on
this host they fall through to the scalar reference and no in-envelope
implementation of them exists to divide against. They are exercised by the
family's behaviour check but are **not** measured (see *Known limitations*).
Everywhere else the AVX2 body is the top tier the library can select, so the
oracle sits at the envelope's ceiling.

**Carried but not measured**: each oracle patch also restores the family's
AVX-512BW bodies (and, for the resizer, its NEON/SVE2 ones). They are part of
the public implementation, they are not compiled, and they are not in any
step's in-scope set. Likewise, oracle-implemented functions that the project's
framework never times — `SimdFill` is one, no dispatch rewiring; the general
`SimdGaussianBlur` is another — are not measured, and are not claimed as covered.
The step-06 factory row `WarpAffineInit` is out of scope for a different reason:
at the measurement geometry the framework's row for it reads 0.14x in *every* tree,
the no-op and the oracle included, so it measures the instrument rather than
the candidate's work (see *Known limitations*).

**Enforced by the measurement, scoped to what the step owns.**
`tests/vendor/envelope_scan.py` reads the step's file patterns
(`tests/SCOPE.stepNN`) and its envelope data (`tests/ENVELOPE.stepNN`) and holds
every file the step owns — plus `src/Simd/SimdLib.cpp`, the dispatch file its
branches go back into — to the ceiling, on the lines the candidate added or
changed relative to the base tree the measurement passes it: the pinned release
with all eight families removed (the ladder's L0), which is the only base level
the measurement container carries. A file the candidate left alone is
reported as unchanged and not scanned, so a tree that changed nothing of this
step's passes the gate visibly rather than silently:

```
== envelope: SSE4.1/AVX2 ceiling, scoped to what this step changed ==
  envelope: 8 of 8 scoped files unchanged by the candidate
  envelope: every file this step owns is unchanged — envelope check skipped, ceiling SSE4.1/AVX2
```

Two properties matter as much as the check itself. It is **scoped to the step**
by file: a file the candidate left alone is skipped whole, so pre-existing
out-of-envelope code in a shared header (the out-of-scope Synet permute kernels
live in `SimdTransform.h`) is never a finding, and the lines it inspects are
the ones the diff against L0 attributes to the candidate. One consequence is
worth stating because it is not the same claim: L0 is not L(k-1), so in the
files this step shares with earlier steps — `SimdLib.cpp` is in every step's
scope — the scanned lines also carry the candidate's own earlier steps. That
cannot fail a tree that passed those steps, because a line that is out of
envelope there would already have failed the step that wrote it, under this
same ceiling and this same file list. And it **ignores dead back ends**: the
reference's own AVX-512BW files are not compiled under `SIMD_AVX512=OFF`, so a
candidate's are not compiled either and are reported as ignored — a claim the
gate asserts against
the measurement build's own source list (`build.ninja`) instead of trusting it. A
violation — an AVX-512/AMX intrinsic, vector or mask type, register name,
target pragma or wider compiler flag, in a file this step owns that the measurement
build compiles — fails the milestone and is printed with file and line.

## Known limitations

- **The harness re-runs each case on the same buffers.** A candidate that
  memoizes by input identity would pass the bit-exactness gate (a careful
  version would) and read absurdly fast. The instruction forbids it explicitly,
  and no gate detects it. This is inherited from the project's own framework;
  a per-repetition input refresh is not expressible through `-m=a` without
  editing the instrument.
- **The noise floor is measured, not zero.** The two sides are separately
  timed, so their ratio is not identically 1.0 and `max(1, ·)` rectifies the
  upward half of the noise into a small positive floor. A no-op agent run over all eight steps, in the measurement container, measured
  0.0004 – 0.059 (mean 0.017); the same commands run locally on a host at load
  45–55 measured 0.000 – 0.164, which is what contention does to a ratio of two
  separate timings. Every step's oracle margin is quoted against that no-op
  figure.
- **Per-step figures reproduce to about ±30% on a contended host**; the average
  across steps is much tighter because it covers every in-scope function.
- **Two step-7 functions have no AVX2 body upstream** (`ChangeColors`,
  `NormalizeHistogram`); their only implementations are AVX-512BW/AMX-BF16,
  which this build does not compile. They are not in the measured set because
  every in-scope name must have a reference implementation inside the
  envelope; the family's behaviour check still runs them (they stay in
  `tests/FILTERS.step07`), and a candidate that writes an AVX2 body for them
  changes no measurement.
- **Two further step-7 entry points have no in-envelope body either.**
  `SimdGetColSums` and `SimdGetAbsDxColSums` are rewired upstream only through
  back ends this build does not compile (HVX here; the NEON branches both
  ladders already carried are the same dead code), so no in-envelope implementation of
  them exists to divide against and they are in neither `FUNCS.step07` nor
  `FILTERS.step07`. They are ordinary library members: the step does not name
  them, the instruction does not ask for them, and the whole-library
  non-regression run exercises them like everything else — so the sentence
  above about the two names dropped from the measured set should not be read as
  a complete list of the family's no-in-envelope-body entry points. A candidate
  that writes an AVX2 body for these two changes no measurement either.
- **The step-06 factory row `WarpAffineInit` is out of scope.** At the measurement
  geometry the framework reports `api=1.450 ms base=0.200 ms 0.14x` for it in
  the no-op tree, the oracle tree and a model's tree alike — three different
  trees, the same two numbers — so the row is not the like-for-like ratio the
  harness documents (`tests/test.sh:34-38`) and moves with no candidate's work. The two numbers are geometry-dependent
  and are quoted at the measurement geometry: a rollout measuring the same row at
  its own 320x240 practice geometry read `0.287/0.201` (0.70x) for the same
  group, which is the same property seen from a different size — the row moves
  with the instrument, not with the candidate. It was
  dropped from `tests/FUNCS.step06`; the group
  `WarpAffine` stays in `tests/FILTERS.step06`, so the affine warp is still
  exercised for bit-exactness.

## Running it

```sh
harbor run --job-name simd-<label> --jobs-dir jobs/simd-<label> \
  --n-concurrent 1 --agent <agent> --model <model> \
  --path tasks/optimization/simd-image-primitives \
  --artifact /app --no-delete --yes
```

`--agent oracle` applies each step's `solution/oracle.patch`; `--agent nop`
does nothing and measures the floor.
