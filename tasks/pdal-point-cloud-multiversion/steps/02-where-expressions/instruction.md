# Step 2 — The universal `where` / `where_merge` mechanism (PDAL 2.2.0)

## 1. Implement this milestone

`/app` holds the PDAL you delivered for step 1 — upstream's point-cloud library and its `pdal` command-line tool at the **2.1.0** level, built by the one command in `/app/TEST_INTERFACE.md` §2. Continue that implementation in place.

Move it from PDAL 2.1.0 to **PDAL 2.2.0** by implementing exactly what `/app/spec/PROFILE.md` §2 puts in scope:

- **`where` on every filter** — Any filter in a pipeline may carry a `where` option holding a boolean expression over dimension names. Points for which it is false skip that filter. The grammar — comparisons, `&&`, `||`, `!`, parentheses, numeric literals, dimension names — is in the shared expression documentation the release introduces, and it is the same grammar for every filter. `where` is a **filter** option: readers and writers reject it at this release. One upstream file to know about: `stages/writer_opts.rst` documents `where` on writers and is `.. include::`-d into every writer page including `writers.las.rst`; the binary refuses it nevertheless, and the cases used here reflect what the binary does.
- **`where_merge`** — Controls what happens to the points a `where` excluded: the documented values and the documented default.
- **`filters.assign`'s `value` option** — The stage gains the newer spelling of an assignment at this release: `value` carries one assignment expression — `Dimension = ValueExpression`, optionally followed by `WHERE ConditionalExpression` — or a list of them, applied in order. `stages/filters.assign.rst` documents it and `stages/expression.rst` gives the grammar it builds on; the page is not otherwise named at this step, so this is what puts the option in scope, and later steps' in-scope pipelines are built on it (`"value": "Z = Zadj"` copies `Zadj` into `Z`). The `assignment`/`condition` pair the stage has carried all along keeps working unchanged.
- **`filters.faceraster`** — Rasterises a triangulated mesh onto a grid. A cell centre that lies exactly on a triangle edge is inside it (edge-inclusive containment). A cell centre inside the triangulation takes the linear interpolation of the containing triangle's three known values.
- **`filters.optimalneighborhood`** — Computes an optimal k per point, writing `OptimalKNN` and `OptimalRadius`. The k nearest neighbours of a point include the point itself (self-inclusive kNN).
- **`readers.obj`** — Reads OBJ meshes.
- **`writers.raster`** — Writes a raster from a stage that produced one.
- **`filters.litree`** — Individual-tree segmentation, writing a `TreeID` dimension. Its page names the dimension; tree numbering starts at 1.

and by making the changes §2.2 lists — behaviour that this release **changes or removes**, which is in scope in the changed direction:

- **`filters.hag` is removed.** It is absent from `pdal --drivers`, and a pipeline naming it fails with a non-zero exit and a diagnostic naming the stage. Its page `stages/filters.hag.rst` is still in the corpus at this release, headed by a warning that it has been eliminated in favour of `filters.hag_nn`, `filters.hag_dem` and `filters.hag_delaunay` — the warning is the normative sentence, not the body below it.
- **`filters.normal`'s `refine` default changes from `true` to `false`.** The default is what changes: it is visible in the stage's option metadata (`pdal --options filters.normal`, with or without `--showjson`) and this release computes the same normals either way, so nothing about the point data is in scope here.
- **`ClusterID` becomes a signed type** in `pdal info --schema` output, so the dimension can carry a negative value and a `filters.range` limit over a negative interval can select points. The transition is observable on `filters.cluster`, which wrote an unsigned `ClusterID` at 2.1.0; `filters.dbscan`'s `ClusterID` was already signed there.
- **`readers.las` exposes `ScanChannel` and `ClassFlags` only for PDRF 6-10.** For point data record formats 0-5 those dimensions are absent from the schema.

Move the version the tree declares as well — wherever `/app/CMakeLists.txt` now sets it — so that `pdal --version` reports `2.2.0`. That is the handoff convention, not part of the behaviour requirement: the gate only requires that `pdal --version` runs and prints a version in the form the binary already prints (`pdal X.Y.Z` on stdout — change the number, not the format).

Everything step 1 put in scope stays in scope and must keep working. This step's **gate** re-runs one end-to-end path per earlier step; a regression there means the step is not satisfied, regardless of how much new work is complete. `PROFILE.md` is cumulative — §1 is still there and still binding.

This step covers **10 independent behaviour groups**, each one behaviour family, plus the `gate` group that re-checks inherited behaviour. All of them are in scope: the gate must hold, and the new capability is expected across every group, not just one corner of it. `/app/TEST_INTERFACE.md` §6 lists the groups.

| Group |
| --- |
| `gate` |
| `where-on-filters` |
| `where-expression-grammar` |
| `where-merge-views` |
| `mesh-to-raster` |
| `optimalneighborhood` |
| `litree-treeid` |
| `hag-removed` |
| `clusterid-signed` |
| `las-pdrf-dimensions` |
| `normal-refine-default` |

---

## 2. Read these local references

`/app/spec/2.2.0/` is upstream PDAL's documentation at **2.2.0**, verbatim — the release you are implementing. `/app/spec/2.2.0/README.md` says what it contains and lists every page that changed since `spec/2.1.0/` -- that earlier corpus is not shipped, so the README's own change summary is what tells you what moved.

Read these two first — they are short and they bound everything else:

1. `/app/spec/PROFILE.md` — **what is in scope and what is not.** §2 is this step's whole assignment; §1 is what must keep working.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the build command, the binary that is run, what is inspected and how, and what is explicitly out of scope.

Then the normative upstream pages under `/app/spec/2.2.0/`, in the order they pay off:

| Read | For |
| --- | --- |
| `stages/expression.rst` and `stages/filter_opts.rst` | **new at this release.** The whole expression grammar, the per-point semantics, and `where` / `where_merge`. `filter_opts.rst` is included into every filter page, which is why it reads as boilerplate there and is in fact the specification |
| any two `stages/filters.*.rst` pages | to see the `where` block as it appears on an ordinary stage — the include is identical everywhere, which is the point |
| `stages/filters.normal.rst` | the `refine` default — the value the option listing reports, not an effect on the normals |
| `stages/filters.faceraster.rst`, `filters.optimalneighborhood.rst`, `readers.obj.rst`, `writers.raster.rst`, `filters.litree.rst` | the five new stages |
| `stages/filters.hag.rst` | the removal warning at its head |

`where` is one mechanism applied in one place — the filter base class — and then it works everywhere. Build the expression evaluator first and the five new stages afterwards.

**Do not read the whole corpus.** It is a reference, not a reading list: over a hundred pages under `stages/` alone. Read the pages the table names, then open others only when a specific behaviour sends you there.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step, what you must not delete, and a **checkpoint you can run by hand**. Run it at the start, before you change anything.

---

## 3. Meta requirements

- **Budget**: 14400 seconds. `bash /app/timer.sh` shows the remaining time; the configured timeout is authoritative and resets each step. A full rebuild of PDAL takes several minutes at `-j8`; budget for it and build incrementally.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what the next step starts from, so keep it building at every point you might be interrupted. Do not delete or move `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or `timer.sh`. The build directory is not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to PDAL itself.** PDAL is BSD-licensed and every release, patch and test file is one download away. Vendoring, transcribing or reconstructing upstream PDAL source for the 2.1.0 → 2.2.0 difference — in whole or in part, by any route, including from memory — is prohibited and defeats the point of the task. The shipped documentation describes the *behaviour* you must reproduce; the implementation must be yours.
- **Off-limits**: do not inspect `/tests`, later step directories, external test fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — the `TEST_INTERFACE.md` §2 build works offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint passes.
