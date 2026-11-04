# How to check your work

`/app/parasail` is parasail at a pinned upstream commit with the vectorized
implementations of some alignment families removed.  The non-vectorized
reference for each family is still there, the dispatchers fall through to it,
and the library is complete and correct -- just slow where the optimized code
is missing.

## Build

```bash
meson setup /app/build /app/parasail --buildtype=release
ninja -C /app/build -j"$(nproc)"
```

That produces `/app/build/src/libparasail.so`, which is what everything below
loads.  Meson is the build this work is measured with.  `Makefile.am` and
`CMakeLists.txt` are also in the tree; if you add a source file, register it in
whichever build descriptions you want to keep working.

## Behaviour must not change

Every entry point has to return exactly what it returns now.  All of these are
re-run, on hidden inputs, when your work is checked.

1. **The project's own suite** must gain no new failure.  Upstream declares its
   suite as `test_isa`, `test_basic` and `test_verify`; the rest of the
   programs under `tests/` are built but not run by upstream, and some of them
   report inconsistencies on stderr while still exiting 0.  The meson build
   produces `test_isa` and `test_verify` -- `test_basic` is declared by the
   autotools and CMake builds but not by `tests/meson.build`, so there is no
   binary for it here -- and those two are what is run:

   ```bash
   /app/build/tests/test_isa
   /app/build/tests/test_verify -f /app/parasail/data/test_small_2.fasta
   ```

   `test_verify` walks every implementation in the library and compares it
   against the reference, printing a line for each disagreement.  Lines
   mentioning a name **in this milestone's in-scope set** are the ones that
   matter; a few upstream families disagree with upstream's own reference and
   are out of scope for that reason.

2. **Every in-scope entry point must produce an identical result** to the
   reference build's, on randomized inputs: score, end positions, statistics,
   score table, last row and column, and the CIGAR that
   `parasail_result_get_cigar` reconstructs.  The comparison is
   bit-for-bit -- there is no tolerance.

3. **Each entry point you implement must report the solution width it is named
   for.**  The
   `result->flag` word carries `PARASAIL_FLAG_BITS_8/16/32/64` and
   `PARASAIL_FLAG_LANES_*`; `pbench <lib> width <name> <seed> <n1> <n2>` prints which width an
   entry point answered in.  This is checked on the entry points your own code
   answers: the tree you started from answers every removed family from the
   portable reference and its `sg_striped_16` reports 32 bits by construction, so an
   entry point you leave where it is is an unimplemented one, not a width
   failure.

## The instruction-set ceiling

The ceiling is **AVX2 (256-bit)**, the same machine the reference
implementation uses.  The target host has SSE2, SSE4.1 and AVX2 and no
AVX-512, so AVX-512 code could not run here even if it compiled.  The verifier
reads the files that carry this milestone's entry points and fails the milestone
if they contain code above that ceiling -- reachable or not, because a wider
machine would not be a like-for-like comparison with the reference.  The scan is
textual and case-insensitive: `_mm512`, `__m512`, `__mmask`, `zmm` and the string
`avx512` are each a failure on their own, so a preprocessor guard such as
`#ifdef __AVX512F__` counts too.

## How your work is timed

Each in-scope entry point is timed **individually**, by calling the name through
the library's own public entry point -- so the implementation the library
installed is what is measured, and a faster function shows up at its full
value instead of being diluted by the rest of the library.  Every in-scope entry
point is timed, so none of your work goes unnoticed.

```bash
# one entry point, median of 7 repetitions, each on a freshly drawn pair:
/app/tools/pbench /app/build/src/libparasail.so bench sw_striped_16 12345 400 400 7

# every in-scope entry point of this milestone at once:
/app/tools/pbench /app/build/src/libparasail.so run /app/SCOPE.txt 12345 400 400 7 bench

# is the result what it should be?  compare against your own tree before and
# after a change with `dump`, which prints a digest of everything the entry
# point returned:
/app/tools/pbench /app/build/src/libparasail.so dump sw_striped_16 12345 400 400

# which solution width does it answer in?
/app/tools/pbench /app/build/src/libparasail.so width sw_striped_16 12345 400 400
```

`pbench` takes the **median** of the repetitions, and each repetition draws a
fresh sequence pair from the seed you pass, so a run cannot be won by caching
a result.  Sequences are uniform random over the 20 standard amino acids --
the alphabet of the default `blosum62` substitution matrix -- and every
alignment uses gap-open 10, gap-extend 1.

**The measurement workload is not disclosed.**  It uses the same generator, the same
composition and the same scoring parameters as above, but a different seed,
and it is fixed for every run.  Optimize the family for the general case
rather than for one pair of sequences; a kernel that only wins on one input
will not gain anything from it.

`pbench` measures your tree alone.  The figure your work is measured on is the
ratio of the reference build's time to yours, measured on the same cores in the
same container, so a slower or busier machine moves both numbers together.

There is also an **end-to-end non-regression gate**, which only passes or
fails.  It times your tree and the reference build on a draw
shape this milestone's in-scope set does **not** use -- 257x997 against 400x400 --
over two sets of entry points.  Each must come within a factor of **2.0**:

- **entry points this milestone does not own**, which both builds answer from
  the same portable reference sources.  A difference there is a property of the
  build, so this is what fails a tree compiled well for its own kernels and
  badly for everything else.
- **this milestone's own entry points**: the reference build answers those from
  the portable C, so your kernels have to be no worse than the reference on a
  shape the measurement draw never uses.  A kernel tuned to one sequence shape reads
  as a slow one here.

## The in-scope set

This milestone is **semi-global alignment**.  Its in-scope entry points are:

```
sg_striped_16
sg_striped_32
sg_striped_64
sg_scan_16
sg_scan_32
sg_scan_64
sg_table_striped_16
sg_table_striped_32
sg_table_striped_64
sg_table_scan_16
sg_table_scan_32
sg_table_scan_64
sg_rowcol_striped_16
sg_rowcol_striped_32
sg_rowcol_striped_64
sg_rowcol_scan_16
sg_rowcol_scan_32
sg_rowcol_scan_64
sg_qb_striped_16
sg_qb_striped_32
sg_qb_striped_64
sg_qb_scan_16
sg_qb_scan_32
sg_qb_scan_64
sg_qb_table_striped_16
sg_qb_table_striped_32
sg_qb_table_striped_64
sg_qb_table_scan_16
sg_qb_table_scan_32
sg_qb_table_scan_64
sg_qb_rowcol_striped_16
sg_qb_rowcol_striped_32
sg_qb_rowcol_striped_64
sg_qb_rowcol_scan_16
sg_qb_rowcol_scan_32
sg_qb_rowcol_scan_64
sg_qe_striped_16
sg_qe_striped_32
sg_qe_striped_64
sg_qe_scan_16
sg_qe_scan_32
sg_qe_scan_64
sg_qe_table_striped_16
sg_qe_table_striped_32
sg_qe_table_striped_64
sg_qe_table_scan_16
sg_qe_table_scan_32
sg_qe_table_scan_64
sg_qe_rowcol_striped_16
sg_qe_rowcol_striped_32
sg_qe_rowcol_striped_64
sg_qe_rowcol_scan_16
sg_qe_rowcol_scan_32
sg_qe_rowcol_scan_64
sg_qx_striped_16
sg_qx_striped_32
sg_qx_striped_64
sg_qx_scan_16
sg_qx_scan_32
sg_qx_scan_64
sg_qx_table_striped_16
sg_qx_table_striped_32
sg_qx_table_striped_64
sg_qx_table_scan_16
sg_qx_table_scan_32
sg_qx_table_scan_64
sg_qx_rowcol_striped_16
sg_qx_rowcol_striped_32
sg_qx_rowcol_striped_64
sg_qx_rowcol_scan_16
sg_qx_rowcol_scan_32
sg_qx_rowcol_scan_64
sg_db_striped_16
sg_db_striped_32
sg_db_striped_64
sg_db_scan_16
sg_db_scan_32
sg_db_scan_64
sg_db_table_striped_16
sg_db_table_striped_32
sg_db_table_striped_64
sg_db_table_scan_16
sg_db_table_scan_32
sg_db_table_scan_64
sg_db_rowcol_striped_16
sg_db_rowcol_striped_32
sg_db_rowcol_striped_64
sg_db_rowcol_scan_16
sg_db_rowcol_scan_32
sg_db_rowcol_scan_64
sg_de_striped_16
sg_de_striped_32
sg_de_striped_64
sg_de_scan_16
sg_de_scan_32
sg_de_scan_64
sg_de_table_striped_16
sg_de_table_striped_32
sg_de_table_striped_64
sg_de_table_scan_16
sg_de_table_scan_32
sg_de_table_scan_64
sg_de_rowcol_striped_16
sg_de_rowcol_striped_32
sg_de_rowcol_striped_64
sg_de_rowcol_scan_16
sg_de_rowcol_scan_32
sg_de_rowcol_scan_64
sg_dx_striped_16
sg_dx_striped_32
sg_dx_striped_64
sg_dx_scan_16
sg_dx_scan_32
sg_dx_scan_64
sg_dx_table_striped_16
sg_dx_table_striped_32
sg_dx_table_striped_64
sg_dx_table_scan_16
sg_dx_table_scan_32
sg_dx_table_scan_64
sg_dx_rowcol_striped_16
sg_dx_rowcol_striped_32
sg_dx_rowcol_striped_64
sg_dx_rowcol_scan_16
sg_dx_rowcol_scan_32
sg_dx_rowcol_scan_64
sg_qb_de_striped_16
sg_qb_de_striped_32
sg_qb_de_striped_64
sg_qb_de_scan_16
sg_qb_de_scan_32
sg_qb_de_scan_64
sg_qb_de_table_striped_16
sg_qb_de_table_striped_32
sg_qb_de_table_striped_64
sg_qb_de_table_scan_16
sg_qb_de_table_scan_32
sg_qb_de_table_scan_64
sg_qb_de_rowcol_striped_16
sg_qb_de_rowcol_striped_32
sg_qb_de_rowcol_striped_64
sg_qb_de_rowcol_scan_16
sg_qb_de_rowcol_scan_32
sg_qb_de_rowcol_scan_64
sg_qe_db_striped_16
sg_qe_db_striped_32
sg_qe_db_striped_64
sg_qe_db_scan_16
sg_qe_db_scan_32
sg_qe_db_scan_64
sg_qe_db_table_striped_16
sg_qe_db_table_striped_32
sg_qe_db_table_striped_64
sg_qe_db_table_scan_16
sg_qe_db_table_scan_32
sg_qe_db_table_scan_64
sg_qe_db_rowcol_striped_16
sg_qe_db_rowcol_striped_32
sg_qe_db_rowcol_striped_64
sg_qe_db_rowcol_scan_16
sg_qe_db_rowcol_scan_32
sg_qe_db_rowcol_scan_64
sg_qb_db_striped_16
sg_qb_db_striped_32
sg_qb_db_striped_64
sg_qb_db_scan_16
sg_qb_db_scan_32
sg_qb_db_scan_64
sg_qb_db_table_striped_16
sg_qb_db_table_striped_32
sg_qb_db_table_striped_64
sg_qb_db_table_scan_16
sg_qb_db_table_scan_32
sg_qb_db_table_scan_64
sg_qb_db_rowcol_striped_16
sg_qb_db_rowcol_striped_32
sg_qb_db_rowcol_striped_64
sg_qb_db_rowcol_scan_16
sg_qb_db_rowcol_scan_32
sg_qb_db_rowcol_scan_64
sg_qe_de_striped_16
sg_qe_de_striped_32
sg_qe_de_striped_64
sg_qe_de_scan_16
sg_qe_de_scan_32
sg_qe_de_scan_64
sg_qe_de_table_striped_16
sg_qe_de_table_striped_32
sg_qe_de_table_striped_64
sg_qe_de_table_scan_16
sg_qe_de_table_scan_32
sg_qe_de_table_scan_64
sg_qe_de_rowcol_striped_16
sg_qe_de_rowcol_striped_32
sg_qe_de_rowcol_striped_64
sg_qe_de_rowcol_scan_16
sg_qe_de_rowcol_scan_32
sg_qe_de_rowcol_scan_64
```

198 entry points in all.  The same list is at `/app/SCOPE.txt`.  A function left
alone shows no speedup, so cover all of them.

**Out of scope:** the `_profile_` entry points; the `_sat`
saturating-retry wrappers; the 8-bit widths; and the `diag` strategy wherever
it is not in the list above -- upstream's own `diag` implementation of the
local and semi-global recurrences disagrees with upstream's own reference on
some inputs, so those are excluded from every milestone rather than measured
badly.  Implementing them has no effect either way.
