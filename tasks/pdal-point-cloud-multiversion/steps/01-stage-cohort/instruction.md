# Step 1 — The 2.1.0 stage cohort and three-dimensional crop (PDAL 2.1.0)

## 1. Implement this milestone

`/app` holds the complete source tree of **PDAL 2.0.1**, upstream's point-cloud library and its `pdal` command-line tool, with the `doc/` and `test/` trees removed. It configures and builds as it stands with the one command in `/app/TEST_INTERFACE.md` §2 — run the `/app/WORKSPACE_HANDOFF.md` checkpoint first and see it pass before you change a line.

Move it from PDAL 2.0.1 to **PDAL 2.1.0** by implementing exactly what `/app/spec/PROFILE.md` §1 puts in scope:

- **Height-above-ground stages** — `filters.hag_nn`, `filters.hag_delaunay` and `filters.hag_dem` each compute a `HeightAboveGround` dimension by a different method. Each has its own page under `stages/`. The neighbour search in `filters.hag_nn` and `filters.hag_delaunay` operates in 2-D (horizontal distance only; Z is ignored when selecting ground neighbours).
- **`filters.voxeldownsize`** — Voxel-grid downsampling with a `cell` size and a `mode` of `first` or `center`. **Where the lattice sits is part of the behaviour, and the page does not say it, so it is stated here:** the grid is anchored on the first point of the view, positioned so that point falls at the centre of its own voxel. The origin is `first - cell/2`, taken per axis from that same first point, so a coordinate `v` falls in voxel `k = floor((v - first + cell/2) / cell)` and the voxel centre that `mode: center` writes back is `first + k * cell`. On a 10x10x10 integer lattice read in ascending order at `cell` 2 that leaves six voxels per axis, not five.
- **`filters.dbscan`** — Density-based clustering writing a `ClusterID` dimension, with `min_points`, `eps` and `dimensions`.
- **`filters.csf`** — Cloth-simulation ground filter, setting `Classification` 2 on ground **and 1 on the points it processes that are not ground** — a point it processes never keeps the classification it arrived with, so an input that arrives at 0 finishes with only the `ignore`-excluded points still at 0. The page does not say this, so it is stated here and it is required; the `filters.pmf` change below relabels the same way.
- **`filters.separatescanline`** — Splits a point view into one view per scan line. A point whose `EdgeOfFlightLine` is non-zero is the **last** point of its scan line: a line ends at each flagged point, and any points after the final flag form one further line. `groupby` is how many consecutive lines go into each output view.
- **`filters.skewnessbalancing`** — Skewness-balancing ground classification.
- **`filters.fps`** — Farthest-point sampling. With `count` set to at most the number of input points the output holds exactly that many of them, and every one is a point of the input. Its page at this release is `stages/filters.farthestpointsampling.rst`; the stage name the binary registers, and the one a pipeline must use, is `filters.fps`. Which points a given `count` selects is yours to choose; only how many is required.
- **`writers.las` `filesource_id`** — The LAS header's File Source ID becomes settable, and reads back through `pdal info --metadata`.
- **`filters.randomize` `seed`** — `filters.randomize` gains an integer `seed` that initialises its random number generator. It appears in `pdal --options filters.randomize`, and two runs with the same seed over the same input produce the same output order. Which permutation a seed produces is yours to choose; that a seed reproduces its own permutation is required. Upstream did not update `stages/filters.randomize.rst` at this release - its option table still reads `None` - so this paragraph is the statement of the option, and `development/release-notes/2.1.0.md` corroborates it.
- **Refusing what the pages do not allow** — Where a page gives a closed list of values for an option, a value outside the list is an error and not a fallback; where a page states a precondition, the stage does not run without it. Required at this step: `filters.voxeldownsize` `mode`, `filters.csf` `returns`, `writers.ply` `storage_mode` and a `writers.las` `filesource_id` greater than 65535 are each refused (negative values wrap silently as an unsigned 16-bit integer and are not refused); `filters.separatescanline` refuses an input that has no `EdgeOfFlightLine` dimension. In each of those the run exits non-zero, its diagnostic names the stage that refused, and no output file is written. The wording of the diagnostic is not part of the requirement.
  `filters.hag_nn` and `filters.hag_delaunay` refuse an input with no ground-classified points the same way at **this** release, and that is in scope here. Note for later steps: this carve-out applies differently to the two stages. `filters.hag_nn` stops refusing from 2.2.0 onward (it logs an error and continues instead, as recorded in `development/release-notes/2.2.0.md`). `filters.hag_delaunay` continues to terminate abnormally on a ground-less input through at least 2.7.1; the paragraph above still binds it in those later steps.

and by making the changes §1.2 lists — behaviour that this release **changes or removes**, which is in scope in the changed direction:

- **`filters.crop` bounds are three-dimensional.** A `bounds` string of the form `([xmin,xmax],[ymin,ymax],[zmin,zmax])` now constrains Z as well; at 2.0.1 the Z range was parsed and ignored. The comparison is the same inclusive one the 2-D box has always used: a point on a face of the box is inside it on every axis, Z included (`min <= v <= max` per axis), and the observed counts depend on that. Two-dimensional `([xmin,xmax],[ymin,ymax])` is unchanged.
- **`writers.ply` rejects `storage_mode: "default"`.** The accepted values are the ones `writers.ply.rst` lists.
- **`filters.pmf` no longer leaves points at `Classification` 0.** Every point the filter *processes* leaves it classified: ground returns are 2 and the rest are 1, where 2.0.1 left the non-ground ones at the input value 0. Points that the `ignore` option excludes are not processed at all - they pass through untouched and keep the classification they arrived with, so running an unclassified input through `filters.pmf` with `ignore` set leaves exactly the ignored points at 0. `ignore` reads the same way on `filters.csf`.

Move the version the tree declares as well — in the tree you were given that is the `PDAL_VERSION_STRING` cache variable near the top of `/app/CMakeLists.txt` — so that `pdal --version` reports `2.1.0`. That is the handoff convention, not part of the behaviour requirement: the gate only requires that `pdal --version` runs and prints a version in the form the binary already prints (`pdal X.Y.Z` on stdout — change the number, not the format).

This step covers **11 independent behaviour groups**, each one behaviour family, plus the `gate` group that re-checks inherited behaviour. All of them are in scope: the gate must hold, and the new capability is expected across every group, not just one corner of it. `/app/TEST_INTERFACE.md` §6 lists the groups.

| Group |
| --- |
| `gate` |
| `hag-filters` |
| `voxeldownsize` |
| `dbscan` |
| `ground-classifiers` |
| `separatescanline` |
| `stage-registry` |
| `las-filesource-id` |
| `crop-3d-bounds` |
| `ply-storage-mode` |
| `pmf-classification` |
| `fps-and-seed` |

---

## 2. Read these local references

`/app/spec/2.1.0/` is upstream PDAL's documentation at **2.1.0**, verbatim — the release you are implementing. `/app/spec/2.1.0/README.md` says what it contains.

Read these two first — they are short and they bound everything else:

1. `/app/spec/PROFILE.md` — **what is in scope and what is not.** §1 is this step's whole assignment.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the build command, the binary that is run, what is inspected and how, and what is explicitly out of scope.

Then the normative upstream pages under `/app/spec/2.1.0/`, in the order they pay off:

| Read | For |
| --- | --- |
| `stages/filters.crop.rst` | the `bounds` grammar and the sentence that makes the box three-dimensional — this is the single highest-value page of the step. One upstream typo to know about: its 3-D note reads `([xmin, xmax, zmax], [ymin, ymax, zmax])`, which the parser does not accept. The form it accepts, and the one every case uses, is the one in `PROFILE.md` §1.2: `([xmin,xmax],[ymin,ymax],[zmin,zmax])` |
| `stages/filters.hag_nn.rst`, `filters.hag_delaunay.rst`, `filters.hag_dem.rst` | the three height-above-ground methods and the dimension they write |
| `stages/filters.voxeldownsize.rst`, `filters.dbscan.rst`, `filters.csf.rst`, `filters.separatescanline.rst`, `filters.skewnessbalancing.rst`, `filters.farthestpointsampling.rst` | the rest of the new cohort |
| `stages/writers.las.rst` | `filesource_id`, and the header fields the writer controls |
| `stages/filters.randomize.rst` | the stage `seed` is added to. Its option table at this release still reads `None` — upstream did not update the page — so `PROFILE.md` §1.1 is where `seed` is specified, not this page |
| `stages/writers.ply.rst` | the accepted `storage_mode` values |
| `pipeline.rst`, `apps/translate.rst`, `apps/info.rst` | the pipeline JSON model and the two applications every group drives. You already have a working 2.0.1, so read these only where a group surprises you |

Most of the budget belongs in the nine new stages. The crop change is one comparison in one file.

**Do not read the whole corpus.** It is a reference, not a reading list: over a hundred pages under `stages/` alone. Read the pages the table names, then open others only when a specific behaviour sends you there.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step, what you must not delete, and a **checkpoint you can run by hand**. Run it at the start, before you change anything.

---

## 3. Meta requirements

- **Budget**: 14400 seconds. `bash /app/timer.sh` shows the remaining time; the configured timeout is authoritative and resets each step. A full rebuild of PDAL takes several minutes at `-j8`; budget for it and build incrementally.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what the next step starts from, so keep it building at every point you might be interrupted. Do not delete or move `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or `timer.sh`. The build directory is not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to PDAL itself.** PDAL is BSD-licensed and every release, patch and test file is one download away. Vendoring, transcribing or reconstructing upstream PDAL source for the 2.0.1 → 2.1.0 difference — in whole or in part, by any route, including from memory — is prohibited and defeats the point of the task. The shipped documentation describes the *behaviour* you must reproduce; the implementation must be yours.
- **Off-limits**: do not inspect `/tests`, later step directories, external test fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — the `TEST_INTERFACE.md` §2 build works offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint passes.
