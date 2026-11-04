# WORKSPACE_HANDOFF

## What this workspace is

`/app/Simd` is the Simd computer-vision library at a pinned upstream release
with one or more of its image-processing primitive families removed: for those
families the per-ISA implementation files and the dispatch branches that select
them are gone, so the library falls through to the portable scalar code in
`src/Simd/SimdBase*.cpp`. Everything else in the library is upstream and intact,
and the library builds and passes its own test suite in this state.

## What this milestone is

Make one family of image primitives faster, without changing a single output
byte. The family is the edge operators: the Sobel and Laplace stencils, their absolute-value and sum variants, the contour metrics and anchors, and the boosted-texture operators.

`/app/BENCHMARK.md` names the exact functions in scope for this milestone, how
to build, how to check that behaviour has not changed, and how speed is measured.

## How to rebuild and measure locally

```sh
cmake -G Ninja -S /app/Simd/prj/cmake -B /app/build \
    -DCMAKE_BUILD_TYPE=Release -DSIMD_TEST=ON -DSIMD_PYTHON=OFF \
    -DSIMD_SYNET=OFF -DSIMD_AVX512=OFF -DSIMD_AVX512VNNI=OFF
cmake --build /app/build -j4
```

Build output goes in a directory of your own; `/app/build` is a reasonable
choice and is excluded from what is carried forward. The build is large
(~600 translation units on the first run and much less afterwards); `ccache` is
installed and `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`
will make rebuilds cheap.

The measurement lives in the project's own test framework, which is already in
the tree at `src/Test/`. It is the same instrument the measurement uses.

## Where to work

`src/Simd/` is yours. `src/Test/`, `src/Simd/SimdBase*.cpp` and `src/Simd/SimdBase*.h`
and `prj/` are restored from the pinned release before measurement, so changes there
are discarded — and `SimdBase*` is the reference the measurement divides by.
