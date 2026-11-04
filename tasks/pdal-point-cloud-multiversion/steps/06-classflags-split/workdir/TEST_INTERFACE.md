# Test interface — how your work is observed

This file is the same in every step. It describes *how* the checker looks at
your work, never *what* the current step asks for; that is in `instruction.md`
and `PROFILE.md`.

Read this once. Nothing here changes between steps, so a habit you form now
keeps paying off.

## 1. The checker runs your program, it does not link against your code

Checking happens in a separate container. Your `/app` tree is copied in, built
from source, and then exercised **only through the `pdal` executable**. No test
includes a PDAL header, links `libpdalcpp`, or calls a C++ symbol. Nothing
inspects your source layout, your class names, your file names, or your commit
history.

That has three consequences worth internalising:

1. **You may restructure internals freely.** Rename classes, move files, change
   headers, swap a data structure. If the command-line behaviour is right, the
   check passes.
2. **The build must work from a clean tree.** The checker configures
   out-of-tree and builds from scratch; it never reuses your build directory.
   If your tree only builds because of a stale artefact in `build/`, it fails.
3. **Anything the CLI cannot express is not part of what is checked** — and
   equally, cannot count against you.

## 2. The exact build the checker performs

```
cmake -S /app -B <fresh dir> -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DWITH_TESTS=OFF \
      -DBUILD_PLUGIN_HDF=OFF -DBUILD_PLUGIN_NITF=OFF \
      -DBUILD_PLUGIN_PGPOINTCLOUD=OFF -DBUILD_PLUGIN_TILEDB=OFF \
      -DBUILD_PLUGIN_DRACO=OFF -DBUILD_PLUGIN_E57=OFF \
      -DBUILD_PLUGIN_ICEBRIDGE=OFF -DBUILD_PLUGIN_TRAJECTORY=OFF
cmake --build <fresh dir> -j"$(nproc)"
```

It then requires `<fresh dir>/bin/pdal` to exist and be executable. **If the
build fails, or that file is missing, the step is not satisfied — every group,
including the proof-of-life gate.** Run this exact command yourself before you
finish. It is the single most common way to fail a whole step.

The container has the same libraries you have: GDAL, GEOS, PROJ, libgeotiff,
libtiff, curl, libxml2, zlib, lzma, zstd, sqlite3, jsoncpp and Eigen. **No LAZ
codec is installed** — neither laz-perf nor LASzip — because the environment
has no network to install one from; the library's source is provided offline
instead, at `/opt/laz-perf` (laz-perf 3.2.0, LGPL-2.1; see its `README.md`),
so LAZ-compressed I/O needs that codec vendored into `/app` first — the
checking container has neither network nor `/opt`. It has **no network**-dependent
build step, so do not add a dependency that has to be downloaded at build time.

## 3. How a case is run

Each case gets its own empty directory. Fixture files named by the case are
copied in, small input files the case declares are written out, and then one
or more `pdal` invocations run **with that directory as the working
directory**.

Two flags can appear in the cases and are worth knowing upfront:

* `--showjson` — makes `pdal --drivers` and `pdal --options <stage>` emit
  their output as JSON instead of the default human-readable text. It is
  not documented in the shipped `apps/` pages but is available from the
  2.0.1 binary onward. It does nothing to `pdal info`, whose `--metadata`
  output is already JSON with or without the flag. With it, `pdal
  --options <stage>` prints a two-element array: the stage name as a
  string, then the list of that stage's options, each an object carrying
  `name` and `description` keys, and a `default` key when the option has
  a default.
* `--nostream` — runs the pipeline in standard (non-streaming) mode. It is
  documented in `apps/pipeline.rst`. A handful of cases require it because
  some pipeline shapes are only supported in standard mode (for example, a
  pipeline that writes multiple output files with a `#` placeholder in the
  filename).

The JSON these emit is the tree's own inherited rendering, unchanged by any step: `pdal info --schema` lists a file's dimensions as
objects carrying a `name` member, and `pdal info --stats --dimensions <D>` reports a statistic under the keys `name`, `count`, `minimum`
and `maximum`.

The case then asserts on:

* the **exit status** of each invocation;
* its **stdout**, parsed as JSON where the command emits JSON;
* its **stderr**, matched against a regular expression when a diagnostic is
  part of the contract;
* the **files produced**, read back with an independent reader.

Point-cloud outputs are read with `laspy` — a third-party LAS/LAZ/COPC library
that has nothing to do with your code. It is checked for header fields (version,
point data record format, point count, scales, offsets, VLR identifiers) and
for dimension values (min, max, mean, exact value lists, per-value counts).

## 4. What is never asserted

These are excluded deliberately, so do not contort your implementation to
control them:

* **Whole-file byte equality of a LAS/LAZ file.** A LAS header carries the
  creation day-of-year and a generating-software string, so two correct runs on
  different days produce different bytes.
* **`pdal info`'s `now`, `filesize`, `filename` or `pdal_version` fields.**
  Wall-clock, size and path are not behaviour.
* **Thread counts, timings, memory use, or log output at verbosity levels the
  case does not set.**
* **Point ordering**, with the narrow exceptions spelled out below. Two check
  kinds can read stored order, and both are used deliberately and sparingly.

  A `las_compare` check is elementwise, so it *does* check order. It is used
  that way in three situations. The first is when order is the documented
  behaviour under test -- a seeded `filters.randomize` is required to reproduce
  its own shuffle, and comparing the two runs point-for-point is the only way to
  say so. The second is a copy or round-trip through a format that stores points
  in the order it received them, with no stage in between that is free to
  reorder: the gate's `las-roundtrip-preserves-dimensions` case is the archetype.
  The third is a per-point filter that computes or assigns a dimension without
  adding, dropping or reordering points, checked to show it leaves the other
  dimensions untouched. In none of the three does any plausible implementation
  reorder.

  Everywhere else -- in particular wherever a stage is free to rebuild a spatial
  index, which `writers.copc` does by construction when its octree subdivides --
  the check carries `"unordered": true`. That checks multiset equality over the
  compared dimensions: the two point sets are first ordered identically by the
  *tuple* of those dimensions, so the same points in a different sequence pass
  and a genuinely different cloud still fails.

  A `las` check's `values` key also reads stored order: it compares the first
  `len(values)` points of the file in the order the file stores them. It is used
  only where the stage under test is what *defines* that order -- a writer
  documented to emit points sorted by a dimension -- and then only on a file
  whose storage layout the case pins alongside it, so that the observation cannot
  turn on a choice you are free to make. A COPC case that checks stored order
  pins the octree's `hierarchy_entries` for exactly that reason: without it,
  whether the case passed would depend on whether the fixture happened to fit in
  the octree's root node, which §4 does not check.
* **Floating-point payloads at full precision.** Where a statistic is checked it
  is checked with an explicit tolerance. The one exception is a
  per-value count (`count_equal`): it counts stored values compared
  exactly, so a case that uses one names a value the arithmetic
  determines exactly.

## 5. Dimension names

Cases name dimensions the way PDAL and the documentation name them —
`Intensity`, `Classification`, `PointSourceId`, `GpsTime`, `ReturnNumber`,
`NumberOfReturns`, `X`, `Y`, `Z`. `X`, `Y` and `Z` always mean the **scaled**
coordinate in the file's own units, not the stored integer.

## 6. Scope

The gate is mandatory: it covers proof of life — the tree builds, `pdal`
runs — and one end-to-end path for each capability the earlier steps
established. It adds no new requirement, but a failure there means the step
is not satisfied, regardless of the new work.

Beyond the gate, the step covers a set of requirement groups: this step's new
capability in bulk, plus a thinner spread over the surrounding surface. The
step's `instruction.md` lists its groups. Every group is in scope, and the
cases within a group must all pass.

Two practical consequences:

* **Do not break the past.** Before you finish, re-run something from the
  previous step. Losing the gate is the easiest way to fail the step.
* **A step you did not work on is not satisfied** — a tree that still behaves
  the way it did when you received it does not pass.

## 7. Diagnostics

When a case fails, the checker prints the case name, every assertion that
failed, and the exit status and last stderr of each invocation. That output is
in the step's log. It is written for you to read.
