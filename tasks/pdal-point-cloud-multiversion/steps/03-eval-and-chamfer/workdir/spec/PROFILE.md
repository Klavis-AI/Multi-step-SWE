# PROFILE — what is in scope, step by step

This file is **cumulative**. Section N is step N's assignment; every earlier section is still binding at step N, because the workspace is cumulative and each step's verifier re-exercises what came before. Anything named in neither an in-scope list nor an out-of-scope list is out of scope.

Each section names the pages that are normative for it. The pages are upstream PDAL's own documentation, verbatim; this file only says which of them a step is graded against. **Only one corpus ships at a time** -- the one for this step's own release. Earlier sections name the release their pages were first normative at, but the copy you read is the one in the corpus you have.


## 1. Step 1 — The 2.1.0 stage cohort and three-dimensional crop (PDAL 2.1.0)

PDAL 2.1.0 is a stage-addition release. Nine stages appear in `pdal --drivers`, `filters.crop` starts honouring the Z range it always advertised, `writers.ply` narrows the `storage_mode` values it accepts, and `filters.pmf` stops leaving points unclassified. Nothing about the pipeline model, the point model or the command surface moves.


### 1.1 New capability

- **Height-above-ground stages.** `filters.hag_nn`, `filters.hag_delaunay` and `filters.hag_dem` each compute a `HeightAboveGround` dimension by a different method. Each has its own page under `stages/`. The neighbour search in `filters.hag_nn` and `filters.hag_delaunay` operates in 2-D (horizontal distance only; Z is ignored when selecting ground neighbours).
- **`filters.voxeldownsize`.** Voxel-grid downsampling with a `cell` size and a `mode` of `first` or `center`. **Where the lattice sits is part of the behaviour, and the page does not say it, so it is stated here:** the grid is anchored on the first point of the view, positioned so that point falls at the centre of its own voxel. The origin is `first - cell/2`, taken per axis from that same first point, so a coordinate `v` falls in voxel `k = floor((v - first + cell/2) / cell)` and the voxel centre that `mode: center` writes back is `first + k * cell`. On a 10x10x10 integer lattice read in ascending order at `cell` 2 that leaves six voxels per axis, not five.
- **`filters.dbscan`.** Density-based clustering writing a `ClusterID` dimension, with `min_points`, `eps` and `dimensions`.
- **`filters.csf`.** Cloth-simulation ground filter, setting `Classification` 2 on ground **and 1 on the points it processes that are not ground** — a point it processes never keeps the classification it arrived with, so an input that arrives at 0 finishes with only the `ignore`-excluded points still at 0. The page does not say this, so it is stated here and it is graded; the `filters.pmf` change in §1.2 relabels the same way.
- **`filters.separatescanline`.** Splits a point view into one view per scan line. A point whose `EdgeOfFlightLine` is non-zero is the **last** point of its scan line: a line ends at each flagged point, and any points after the final flag form one further line. `groupby` is how many consecutive lines go into each output view.
- **`filters.skewnessbalancing`.** Skewness-balancing ground classification.
- **`filters.fps`.** Farthest-point sampling. With `count` set to at most the number of input points the output holds exactly that many of them, and every one is a point of the input. Its page at this release is `stages/filters.farthestpointsampling.rst`; the stage name the binary registers, and the one a pipeline must use, is `filters.fps`. Which points a given `count` selects is not graded, only how many.
- **`writers.las` `filesource_id`.** The LAS header's File Source ID becomes settable, and reads back through `pdal info --metadata` (the JSON path is `metadata.filesource_id`).
- **`filters.randomize` `seed`.** `filters.randomize` gains an integer `seed` that initialises its random number generator. It appears in `pdal --options filters.randomize`, and two runs with the same seed over the same input produce the same output order. Which permutation a seed produces is yours to choose and is not graded; that a seed reproduces its own permutation is. Upstream did not update `stages/filters.randomize.rst` at this release - its option table still reads `None` - so this paragraph is the statement of the option, and `development/release-notes/2.1.0.md` corroborates it.
- **Refusing what the pages do not allow.** Where a page gives a closed list of values for an option, a value outside the list is an error and not a fallback; where a page states a precondition, the stage does not run without it. Graded at this step: `filters.voxeldownsize` `mode`, `filters.csf` `returns`, `writers.ply` `storage_mode` and a `writers.las` `filesource_id` greater than 65535 are each refused (negative values wrap silently as an unsigned 16-bit integer and are not refused); `filters.separatescanline` refuses an input that has no `EdgeOfFlightLine` dimension. In each of those the run exits non-zero, its diagnostic names the stage that refused, and no output file is written. The wording of the diagnostic is not graded.
  `filters.hag_nn` and `filters.hag_delaunay` refuse an input with no ground-classified points the same way at **this** release, and that is graded here. Note for later steps: this carve-out applies differently to the two stages. `filters.hag_nn` stops refusing from 2.2.0 onward (it logs an error and continues instead, as recorded in `development/release-notes/2.2.0.md`). `filters.hag_delaunay` continues to terminate abnormally on a ground-less input through at least 2.7.1; the paragraph above still binds it in those later steps.

### 1.2 Changed and removed behaviour

- **`filters.crop` bounds are three-dimensional.** A `bounds` string of the form `([xmin,xmax],[ymin,ymax],[zmin,zmax])` now constrains Z as well; at 2.0.1 the Z range was parsed and ignored. Two-dimensional `([xmin,xmax],[ymin,ymax])` is unchanged. The comparison is the same inclusive one the 2-D box has always used: a point on a face of the box is inside it on every axis, Z included (`min <= v <= max` per axis), and the graded counts depend on that.
- **`writers.ply` rejects `storage_mode: "default"`.** The accepted values are the ones `writers.ply.rst` lists.
- **`filters.pmf` no longer leaves points at `Classification` 0.** Every point the filter *processes* leaves it classified: ground returns are 2 and the rest are 1, where 2.0.1 left the non-ground ones at the input value 0. Points that the `ignore` option excludes are not processed at all - they pass through untouched and keep the classification they arrived with, so running an unclassified input through `filters.pmf` with `ignore` set leaves exactly the ignored points at 0. `ignore` reads the same way on `filters.csf`.

### 1.3 Out of scope at this step

- Every stage whose page is absent from `spec/2.1.0/stages/`.
- Every plugin stage. The build command in `TEST_INTERFACE.md` §2 builds no plugins, so stages whose pages exist but which are plugins upstream — `readers.sqlite`, `writers.sqlite`, `readers.oci`, `readers.mrsid`, `readers.fbx`, `readers.nitf`, `readers.hdf`, `readers.tiledb`, `readers.e57`, `readers.draco` and the like — are neither present nor scored at any step — with one exception: `readers.obj` still carries that marker on its page, but the release builds it as a core stage and §2.1 puts it in scope from step 2 on, so the marker is stale. `pdal --drivers` on the tree you were given is the authoritative list of what is built.
- The Python and Java bindings, the `pdal` C API, and anything under `spec/2.1.0/api/` or `spec/2.1.0/development/` — those pages are shipped as background for extending the library and raise no scored requirement of their own. Where §1.1 cites one (the `filters.randomize` `seed` note), §1.1 is the statement of the requirement and the page is only corroboration.
- **Two defaults these pages state are not graded, because the release does not honour them.** `writers.las.rst` gives `filesource_id` `[Default: 0]`, but a 2.1.0 file written without the option carries an uninitialised value; the hag pages give `allow_extrapolation` `[Default: false]`, but the release behaves as though it were true. Only explicitly-set values of either option are scored, so do not spend time making an unset default match its page.
- Timing, thread counts and memory behaviour of any stage.

### 1.4 Normative pages

First normative at PDAL 2.1.0; read them in `spec/2.3.0/`, which is the corpus this step ships (`spec/2.1.0/` itself is not shipped here):

- `stages/filters.crop.rst` — the `bounds` grammar and the sentence that makes the box three-dimensional — this is the single highest-value page of the step. One upstream typo to know about: its 3-D note reads `([xmin, xmax, zmax], [ymin, ymax, zmax])`, which the parser does not accept. The form it accepts, and the one every graded case uses, is the one in `PROFILE.md` §1.2: `([xmin,xmax],[ymin,ymax],[zmin,zmax])`
- `stages/filters.hag_nn.rst`, `filters.hag_delaunay.rst`, `filters.hag_dem.rst` — the three height-above-ground methods and the dimension they write
- `stages/filters.voxeldownsize.rst`, `filters.dbscan.rst`, `filters.csf.rst`, `filters.separatescanline.rst`, `filters.skewnessbalancing.rst`, `filters.farthestpointsampling.rst` — the rest of the new cohort
- `stages/writers.las.rst` — `filesource_id`, and the header fields the writer controls
- `stages/filters.randomize.rst` — the stage `seed` is added to. Its option table at this release still reads `None` — upstream did not update the page — so `PROFILE.md` §1.1 is where `seed` is specified, not this page
- `stages/writers.ply.rst` — the accepted `storage_mode` values
- `pipeline.rst`, `apps/translate.rst`, `apps/info.rst` — the pipeline JSON model and the two applications every group drives. You already have a working 2.0.1, so read these only where a group surprises you

## 2. Step 2 — The universal `where` / `where_merge` mechanism (PDAL 2.2.0)

PDAL 2.2.0 adds the largest single surface in this task: an expression language that every filter accepts, evaluated per point, plus five new stages. It also removes the deprecated `filters.hag` and flips one documented default.


### 2.1 New capability

- **`where` on every filter.** Any filter in a pipeline may carry a `where` option holding a boolean expression over dimension names. Points for which it is false skip that filter. The grammar — comparisons, `&&`, `||`, `!`, parentheses, numeric literals, dimension names — is in the shared expression documentation the release introduces, and it is the same grammar for every filter. `where` is a **filter** option: readers and writers reject it at this release. One upstream file to know about: `stages/writer_opts.rst` documents `where` on writers and is `.. include::`-d into every writer page including `writers.las.rst`; the binary refuses it nevertheless, and the graded cases reflect what the binary does.
- **`where_merge`.** Controls what happens to the points a `where` excluded: the documented values and the documented default.
- **`filters.assign`'s `value` option.** The stage gains the newer spelling of an
  assignment at this release: `value` carries one assignment expression —
  `Dimension = ValueExpression`, optionally followed by `WHERE ConditionalExpression`
  — or a list of them, applied in order. `stages/filters.assign.rst` documents it and
  `stages/expression.rst` gives the grammar it builds on; the page is not otherwise
  named at this step, so this paragraph is what puts the option in scope, and later
  steps' graded pipelines are built on it (`"value": "Z = Zadj"` copies `Zadj` into
  `Z`). The `assignment`/`condition` pair the stage has carried all along keeps
  working unchanged.
- **`filters.faceraster`.** Rasterises a triangulated mesh onto a grid. A cell centre that lies exactly on a triangle edge is inside it (edge-inclusive containment). A cell centre inside the triangulation takes the linear interpolation of the containing triangle's three known values. A cell centre inside the triangulation takes the linear interpolation of the containing triangle's three known values.
- **`filters.optimalneighborhood`.** Computes an optimal k per point, writing `OptimalKNN` and `OptimalRadius`. The k nearest neighbours of a point include the point itself (self-inclusive kNN).
- **`readers.obj`.** Reads OBJ meshes.
- **`writers.raster`.** Writes a raster from a stage that produced one.
- **`filters.litree`.** Individual-tree segmentation, writing a `TreeID` dimension. Its page names the dimension; tree numbering starts at 1.

### 2.2 Changed and removed behaviour

- **`filters.hag` is removed.** It is absent from `pdal --drivers`, and a pipeline naming it fails with a non-zero exit and a diagnostic naming the stage. Its page `stages/filters.hag.rst` is still in the corpus at this release, headed by a warning that it has been eliminated in favour of `filters.hag_nn`, `filters.hag_dem` and `filters.hag_delaunay` — the warning is the normative sentence, not the body below it.
- **`filters.normal`'s `refine` default changes from `true` to `false`.** The default is what changes: it is visible in the stage's option metadata (`pdal --options filters.normal`, with or without `--showjson`) and this release computes the same normals either way, so nothing about the point data is graded here.
- **`ClusterID` becomes a signed type** in `pdal info --schema` output, so the dimension can carry a negative value and a `filters.range` limit over a negative interval can select points. The transition is observable on `filters.cluster`, which wrote an unsigned `ClusterID` at 2.1.0; `filters.dbscan`'s `ClusterID` was already signed there.
- **`readers.las` exposes `ScanChannel` and `ClassFlags` only for PDRF 6-10.** For point data record formats 0-5 those dimensions are absent from the schema.

### 2.3 Out of scope at this step

- Everything step 01 put out of scope, unchanged.
- Expression features the shipped pages do not state. If the grammar page does not say an operator exists, it is not scored.
- `filters.lloydkmeans`. The release registers the stage and ships its page, but no behaviour of it is asserted anywhere in this step — do not spend budget on it.

### 2.4 Normative pages

First normative at PDAL 2.2.0; read them in `spec/2.3.0/`, which is the corpus this step ships (`spec/2.2.0/` itself is not shipped here):

- `stages/expression.rst` and `stages/filter_opts.rst` — **new at this release.** The whole expression grammar, the per-point semantics, and `where` / `where_merge`. `filter_opts.rst` is included into every filter page, which is why it reads as boilerplate there and is in fact the specification
- any two `stages/filters.*.rst` pages — to see the `where` block as it appears on an ordinary stage — the include is identical everywhere, which is the point
- `stages/filters.normal.rst` — the `refine` default — the value the option listing reports, not an effect on the normals
- `stages/filters.faceraster.rst`, `filters.optimalneighborhood.rst`, `readers.obj.rst`, `writers.raster.rst`, `filters.litree.rst` — the five new stages
- `stages/filters.hag.rst` — the removal warning at its head

## 3. Step 3 — The `eval` and `chamfer` subcommands (PDAL 2.3.0)

PDAL 2.3.0 is the only release in this window that widens the command surface: two new `pdal` subcommands, each with its own page under `apps/`. It also adds two stages and changes what `writers.pcd` puts on disk.


### 3.1 New capability

- **`pdal eval`.** A new subcommand comparing a classified point cloud against a truth cloud and reporting a confusion matrix. Its page is `apps/eval.rst`, whose worked example shows the JSON it prints. **Four conventions the page does not state are stated here, and they are graded.** The summary `f1_score` is `2 * mean_intersection_over_union / (1 + mean_intersection_over_union)`. A label named in `--labels` that is absent from the data keeps its entry in `labels` with support 0 and 0.0 for every per-label metric, and it still counts in the `mean_intersection_over_union` denominator. Each `labels[]` entry's `label` is the class value as it was passed to `--labels`, not a 1-based index — where the page's example prints `"label": "1"` and `"2"` beside `--labels 2,4`, the example is stale and the graded values are `"2"` and `"4"`. And `--labels` is required: a run that omits it is refused, with a non-zero exit and the option named in the diagnostic.
- **`pdal chamfer`.** A new subcommand reporting the Chamfer distance between two clouds. Its page is `apps/chamfer.rst`. The value is a float and is graded only with an explicit tolerance. The JSON's `filenames` — and `eval`'s `predicted_file` and `truth_file` — echo the arguments as they were given; a bare name stays bare, and the `chamfer` page's example output showing full paths for a bare-name invocation is stale on that point. It is the page's formula — the sum of the squared nearest-neighbour distances, both directions — and the `development/` release note that calls it a "mean" square distance is background prose (§3.4), not the definition.
- **`filters.gpstimeconvert`.** Converts between GPS time conventions. The relationship between GPS time (gt) and GPS standard time (gst) is: gst = gt - 1,000,000,000 s. Equivalently, gt = gst + 1,000,000,000 s. The constant 1,000,000,000 seconds (1e9 s) is the GPS standard time epoch offset. **The week arithmetic is not on the page either, so it is stated here and it is graded:** GPS time counts seconds from the GPS epoch, 1980-01-06T00:00:00Z; a GPS week is 604800 s and week 0 begins at that epoch. "GPS week seconds" are the seconds elapsed since the start of the containing week, so the standard epoch sits 265600 s into a week rather than on a week boundary. `gws2gt` and `gws2gst` resolve which week from `start_date` — the GPS week containing that date — and add the input week seconds to that week's start.
- **`filters.relaxationdartthrowing`.** Poisson-disk style thinning.

### 3.2 Changed and removed behaviour

- **`writers.pcd` writes X, Y and Z as 32-bit floats.** The PCD header reads `SIZE 4 4 4` and `TYPE F F F` for those fields, where 2.2.0 wrote doubles. **An untyped `order` entry does not widen them:** the per-dimension type annotation on `order` is optional, and an entry with no annotation keeps the dimension's written type — four bytes for X, Y and Z, eight for every other dimension. The page's sentence that an untyped entry's "default dimension type is double precision float" does not apply to X, Y and Z; only an explicit annotation such as `=Double` widens a field to eight bytes.
- **`filters.head` applies `count` per `PointView`,** not once across the whole pipeline.
- **`filters.fps` returns every point when `count` exceeds the input size,** rather than failing.

### 3.3 Out of scope at this step

- Everything earlier steps put out of scope.
- **The wording of a refusal at this step, beyond the name it carries.** §1.1's rule still holds, and the graded refusals here name what is wrong or missing: the option (`labels`, `start_date`), the argument (`candidate`), or the stage (`filters.gpstimeconvert`). Beyond that name, say what you like on stderr.
- The exact floating-point text of a `chamfer` or `eval` number. Both are graded through a parsed value with a stated tolerance.
- `filters.relaxationdartthrowing` when `count` is at least the size of the input. Only the thinning case — `count` smaller than the input — is graded; what the filter does when it cannot thin that far is not.
- Which particular points `filters.fps` selects for a given `count`. Only how many survive is graded. For `filters.relaxationdartthrowing` the graded cases run with `shuffle` false on fixtures where the documented walk order fixes the selection, so the selection the rule implies is graded there; which of two points a tie leaves is not.

### 3.4 Normative pages

Under `spec/2.3.0/`, the corpus shipped with this step:

- `apps/eval.rst` and `apps/chamfer.rst` — **new at this release.** The two subcommands: their arguments, their output shape, and their exit behaviour
- `apps/index.rst` — how a subcommand is registered and listed
- `stages/writers.pcd.rst` — the field types written to the PCD header
- `stages/filters.fps.rst`, `filters.head.rst` — `filters.fps` is now documented under its real name, and `filters.head.rst` carries its per-view `count` behaviour; `filters.fps`'s own page carries only its option table, so the exceeds-input change is §3.2 above, not the page
- `stages/filters.gpstimeconvert.rst`, `filters.relaxationdartthrowing.rst` — the two new stages
