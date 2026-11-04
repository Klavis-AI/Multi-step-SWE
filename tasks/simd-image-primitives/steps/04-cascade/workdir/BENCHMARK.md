# BENCHMARK — how this milestone is measured

## Build

```sh
cmake -G Ninja -S /app/Simd/prj/cmake -B /app/build \
    -DCMAKE_BUILD_TYPE=Release -DSIMD_TEST=ON -DSIMD_PYTHON=OFF \
    -DSIMD_SYNET=OFF -DSIMD_AVX512=OFF -DSIMD_AVX512VNNI=OFF
cmake --build /app/build -j4
```

`SIMD_SYNET=OFF` drops the neural-network backend, which is not part of this
task.  `SIMD_AVX512=OFF` / `SIMD_AVX512VNNI=OFF` cap the build at the measurement
host's ceiling: it has SSE4.1 and AVX2 and no AVX-512, so an AVX-512 kernel
would never be selected and never be timed.  Build the same way the measurement
does, or your local numbers are not the measured ones.

## What must not change

The library must produce byte-identical output. The project's own framework has
an auto-check mode that compares every implementation against the entry point
the library dispatches to, with exact equality, at several sizes and
alignments, and it stops the run on the first mismatch:

```sh
./build/Test -m=a -tt=1 -r=/app/Simd -w=320 -h=240 -c=8 -mt=1 -fi=DetectionHaarDetect32fi -fi=DetectionHaarDetect32fp -fi=DetectionLbpDetect16ii -fi=DetectionLbpDetect16ip -fi=DetectionLbpDetect32fi -fi=DetectionLbpDetect32fp -fi=Integral
echo $?   # 0 means no mismatch anywhere in this family
```

Run it with no `-fi` at all for the whole library. `-r` is the project root the
framework reads `data/` from; keep it pointing at `/app/Simd`.

## How speed is measured

The same run times every implementation and writes a table:

```sh
./build/Test -m=a -tt=1 -wt=1 -r=/app/Simd -w=640 -h=480 -c=8 -mt=50 \
    -fi=DetectionHaarDetect32fi -fi=DetectionHaarDetect32fp -fi=DetectionLbpDetect16ii -fi=DetectionLbpDetect16ip -fi=DetectionLbpDetect32fi -fi=DetectionLbpDetect32fp -fi=Integral -ot=report.txt
```

Read `report.txt`: one row per function (or per function-and-configuration for
the class-based APIs), and a column per implementation.

- **`API`** — the entry point the library dispatches to, e.g. `SimdSobelDx`.
  This is your work.
- **`Base`** — the scalar reference, `Simd::Base::SobelDx`. Both are timed by
  the same harness, in the same process, on the same input.
- **The measured speedup of a function is `Base / API`.** 1.0 means no change.

The geometry above is a *suggested* practice geometry and it is deliberately not
the measurement one. The measurement uses a geometry of its own choosing and does
not disclose it; the ratio is a property of the kernel rather than of the size, so
it moves little between sizes. Do not tune to one size — and in particular do
not assume the numbers above are the ones you will be measured on.

## The in-scope set

This milestone covers **7 functions**. A function counts once for each row it
reports — for a class-based API such as the resizer, each distinct
configuration is its own row.

    DetectionHaarDetect32fi                   
    DetectionHaarDetect32fp                   
    DetectionLbpDetect16ii                    
    DetectionLbpDetect16ip                    
    DetectionLbpDetect32fi                    
    DetectionLbpDetect32fp                    
    Integral                                  

A function with no faster implementation contributes no speedup, so "done"
means the whole list is covered, not most of it.

## What is frozen

`src/Test/` (the measuring instrument), `src/Simd/SimdBase*` (the scalar
reference the ratios divide by) and `prj/` (the build system) are rebuilt from
the pinned release before your tree is built. Your work lives in `src/Simd/`.
New implementation files are picked up automatically — the CMake source list is
a recursive glob on `src/Simd/Simd<Prefix>*.cpp`.
