# Step 7 — Grid decimation, sparse surface, and a creating `assign` (PDAL 2.7.1)

## 1. Implement this milestone

`/app` holds the PDAL you delivered for step 6 — upstream's point-cloud library and its `pdal` command-line tool at the **2.6.0** level, built by the one command in `/app/TEST_INTERFACE.md` §2. Continue that implementation in place.

Move it from PDAL 2.6.0 to **PDAL 2.7.1** by implementing exactly what `/app/spec/PROFILE.md` §7 puts in scope:

- **`filters.gridDecimation`** — Keeps one point per grid cell and rewrites that point in place — it marks, it does not delete. The page is `stages/filters.griddecimation.rst`; the name the binary registers is `filters.gridDecimation`, with that capital D. `resolution` is the cell size in X/Y. `output_type` is `max` or `min` by default `max`, and the page prints that default as `false` — trust the binary's, which `pdal --options filters.gridDecimation` reports. Any other `output_type` value is refused the way §1.1's closed-list options are (non-zero exit, no output file). **The cell lattice, the tie rule and what `value` rewrites are not on the page, so they are stated here.** The grid is anchored on the bounding box of the points this filter is given — after `where` has already dropped the rest — so with origin `(minx, miny)` a point at `(x, y)` falls in cell `(int((x-minx)/resolution), int((y-miny)/resolution))`, corrected so that each cell is the half-open square `[minx + w*resolution, minx + (w+1)*resolution)` in x and likewise in y: a point exactly on a cell's upper x or y edge goes to the next cell up or right, but `maxx` and `maxy` themselves stay in the last occupied cell. A cell whose only occupants tie for the extreme Z keeps the one that appears first in the view, because the comparison is strict. `value` is a list of assignment expressions applied in order — and only to the points the filter selected; every other point is emitted untouched. On a 10x10x10 integer lattice 0..9 at `resolution` 2 that is five cells per axis, and the `min` direction is the Z=0 row of each, not the Z=2 row.
- **`filters.sparsesurface`** — Splits a view into ground and low point by sampling at a radius. The page names only `radius`, and it is the only option this step requires. **The classes it writes, how it picks them, and how far this task constrains the result are not on the page, so they are stated here.** Every input point is kept — nothing is dropped, so the output holds exactly as many points as the input. The filter reassigns `Classification` across the whole view: points it picks as ground become `2`, every point it masks becomes `7`, and no other `Classification` value survives. It walks the view in ascending Z; the first still-unselected point it reaches is ground, and every still-unselected point *strictly* within `radius` of it *horizontally* — X and Y only, Z is not part of the distance — is a low point, then it moves on. A point at horizontal distance *exactly* `radius` is **not** masked. `radius` therefore sets how sparse the ground sample is: larger radius, fewer ground points. **How far the count is fixed.** Points that share a Z value may be walked in any order, so once `radius` is wide enough for neighbourhoods to overlap, the ground count is not determined by the rule above. This task therefore requires `radius` 1.0 exactly — on the 1652-point `terrain.las` that is 1600 ground and 52 low points, which no walk order can change, because at that radius the only points strictly within 1.0 of each other are the horizontally coincident ones — and specifies a coarser radius as a range rather than a number. The total never changes at any radius, because the filter only classifies.
- **`writers.copc` `extra_dims`** — Extra dimensions can be carried into a COPC file. **The option is opt-in and its unset state is not on the page, so it is stated here and it is required:** with the option unset nothing beyond the point format's own dimensions is carried — a dimension the point format cannot hold is dropped from the written file rather than carried automatically — and naming it in `extra_dims` (or covering it with `all`) is what carries it.
- **`readers.las` `ignore_missing_vlrs`** — Lets the reader carry on past a VLR it cannot use instead of refusing the file. **`stages/readers.las.rst` does not mention this option — it is named on no page of this corpus — so what follows is its whole specification.** By default `readers.las` refuses a file whose VLR block it cannot walk to the end — a record header that runs off the end of the file, or a record whose declared payload length runs past the start of the point data — exiting non-zero and writing no output. With the option set the reader stops reading VLRs at that record, keeps the ones it already read, and returns the file's points normally. It does **not** relax any check made after the VLR block: in particular a LAS 1.4 file whose GlobalEncoding WKT bit is clear is refused with the option exactly as without it (see §7.3).

and by making the changes §7.2 lists — behaviour that this release **changes or removes**, which is in scope in the changed direction:

- **`filters.assign` creates a dimension that does not exist** instead of failing.
- **`filters.assign` accepts unary maths** in an assignment expression. The page lists only the arithmetic operators, so the accepted function names are named here, because they are what the binary parses and an unknown one is refused outright (`Invalid function name '<name>'`): `floor`, `ceil`, `round`, `abs`, `fabs`, `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `log` (natural), `log2`, `log10`, `exp`, `exp2`. They nest, they may read any dimension, and `WHERE` still gates the assignment.
- **`filters.decimation` accepts a non-integer `step`.** A `step` below 1.0 is still refused (`Option step must be >= 1.0`), but any value at or above it is honoured as written rather than truncated to an integer. **How a fractional `step` selects is not on the page, so it is stated here.** With `offset` (default 0) and `limit`, point `i` of the input is kept exactly when `i == offset + round(k * step)` for some whole `k`, and the count is `round((min(limit, n) - offset) / step)` for an `n`-point view — so `step` 1.5 over 1000 points keeps 667, one for each `k` from 0 to 666, and `step` 2.5 over ten keeps four, at indexes 0, 3, 5 and 8.

Move the version the tree declares as well — wherever `/app/CMakeLists.txt` now sets it — so that `pdal --version` reports `2.7.1`. That is the handoff convention, not part of the behaviour requirement: the gate only requires that `pdal --version` runs and prints a version in the form the binary already prints (`pdal X.Y.Z` on stdout — change the number, not the format).

Everything steps 1–6 put in scope stays in scope and must keep working. This step's **gate** re-runs one end-to-end path per earlier step; a regression there means the step is not satisfied, regardless of how much new work is complete. `PROFILE.md` is cumulative — §1 through §6 are still there and still binding.

This step covers **9 independent behaviour groups**, each one behaviour family, plus the `gate` group that re-checks inherited behaviour. All of them are in scope: the gate must hold, and the new capability is expected across every group, not just one corner of it. `/app/TEST_INTERFACE.md` §6 lists the groups.

| Group |
| --- |
| `gate` |
| `griddecimation` |
| `assign-creates-dimension` |
| `assign-unary-math` |
| `sparsesurface` |
| `copc-extra-dims` |
| `decimation-float-step` |
| `las-ignore-missing-vlrs` |
| `readers-las-option` |
| `stage-registry` |

---

## 2. Read these local references

`/app/spec/2.7.1/` is upstream PDAL's documentation at **2.7.1**, verbatim — the release you are implementing. `/app/spec/2.7.1/README.md` says what it contains and lists every page that changed since `spec/2.6.0/` -- that earlier corpus is not shipped, so the README's own change summary is what tells you what moved.

Read these two first — they are short and they bound everything else:

1. `/app/spec/PROFILE.md` — **what is in scope and what is not.** §7 is this step's whole assignment; §1–§6 is what must keep working.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the build command, the binary that is run, what is inspected and how, and what is explicitly out of scope.

Then the normative upstream pages under `/app/spec/2.7.1/`, in the order they pay off:

| Read | For |
| --- | --- |
| `stages/filters.griddecimation.rst`, `filters.sparsesurface.rst` | **new at this release** |
| `stages/filters.assign.rst` | dimension creation and the unary functions — read the whole page, it changed substantially |
| `stages/filters.decimation.rst` | the `step` type |
| `stages/writers.copc.rst` | the new `extra_dims` option |
| `stages/readers.las.rst` | the reader this step's other new option is added to — but the option itself, `ignore_missing_vlrs`, is named on no page in this corpus, so §7.1 above is the whole of its specification |

`filters.assign` is the deepest change here — it is an expression evaluator gaining functions and a dimension table gaining writes. The two new stages are ordinary stages.

**Do not read the whole corpus.** It is a reference, not a reading list: over a hundred pages under `stages/` alone. Read the pages the table names, then open others only when a specific behaviour sends you there.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step, what you must not delete, and a **checkpoint you can run by hand**. Run it at the start, before you change anything.

---

## 3. Meta requirements

- **Budget**: 14400 seconds. `bash /app/timer.sh` shows the remaining time; the configured timeout is authoritative and resets each step. A full rebuild of PDAL takes several minutes at `-j8`; budget for it and build incrementally.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what the next step starts from, so keep it building at every point you might be interrupted. Do not delete or move `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or `timer.sh`. The build directory is not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to PDAL itself.** PDAL is BSD-licensed and every release, patch and test file is one download away. Vendoring, transcribing or reconstructing upstream PDAL source for the 2.6.0 → 2.7.1 difference — in whole or in part, by any route, including from memory — is prohibited and defeats the point of the task. The shipped documentation describes the *behaviour* you must reproduce; the implementation must be yours.
- **Compression libraries are dependencies, not answers.** No LAZ codec is installed — neither laz-perf nor LASzip — and `cmake/lazperf.cmake` only looks for an external one, so anything that reads or writes LAZ-compressed data needs one brought into the tree. This environment has no network to obtain one from, so the library's source is provided offline instead: **laz-perf 3.2.0, the general-purpose LAZ codec, at `/opt/laz-perf`** (LGPL-2.1; its `README.md` and `PROVENANCE.txt` say what it is and where it came from). Using it is permitted and is not the 2.6.0 → 2.7.1 answer: it is a dependency, not PDAL, and the prohibition above is about PDAL's own source. It has to end up inside `/app` — vendor it, for example `cp -r /opt/laz-perf /app/vendor/lazperf` — because the build that checks the work has no network and no `/opt`, so a library installed only into the system during your session is gone by the time the build is checked.
- **Off-limits**: do not inspect `/tests`, later step directories, external test fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — the `TEST_INTERFACE.md` §2 build works offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint passes.
