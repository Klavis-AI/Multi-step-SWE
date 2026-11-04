# spec/2.6.0 — PDAL 2.6.0 documentation

255 files, about 747 kB. This is upstream PDAL's `doc/` tree at the 2.6.0 release with the site theme, slide decks, images, the workshop and the sphinx plumbing removed, so what remains is the prose. Every page is upstream's text, unedited; three files beside them are not from upstream and are named here so nothing in this corpus is a surprise: `LICENSE.txt`, the licence the pages are under; `dimension-table.csv`, which `dimensions.rst` includes and which upstream generates at doc-build time from `pdal/Dimension.json` rather than keeping in the repository; and this `README.md`, which the file count above includes.

## Layout

| Directory | What it holds |
| --- | --- |
| `stages/` | one page per reader, filter and writer — **the normative pages for almost every scored behaviour** |
| `apps/` | one page per `pdal` subcommand (`info`, `translate`, `pipeline`, …) |
| `pipeline.rst`, `dimensions.rst`, `types.rst` | the pipeline JSON model, the dimension table, the type names |
| `tutorial/` | worked examples; useful, not normative |
| `api/`, `development/` | extending the library in C++; background only |
| `project/`, `about.rst`, `community.rst`, … | project pages; not relevant to any scored behaviour |

## What changed since `spec/2.5.0/`

Only this release's corpus ships -- `spec/2.5.0/` is not beside it, so there is no `diff` to run. The summary below is the exact statement, taken from upstream's own tree at both tags: **4 pages added, 0 removed, 32 changed**, 216 unchanged.

Added:

- `stages/filters.georeference.rst`
- `stages/filters.straighten.rst`
- `stages/readers.arrow.rst`
- `stages/writers.arrow.rst`

Changed (32 pages; those that matter to this step are named in `PROFILE.md` §6.4). Under `stages/`, `apps/` and the root:

- `about.rst`
- `apps/chamfer.rst`
- `apps/delta.rst`
- `apps/density.rst`
- `apps/eval.rst`
- `apps/ground.rst`
- `apps/hausdorff.rst`
- `apps/random.rst`
- `apps/tile.rst`
- `download.rst`
- `index.rst`
- `requirements.txt`
- `stages/filters.assign.rst`
- `stages/filters.chipper.rst`
- `stages/filters.faceraster.rst`
- `stages/filters.range.rst`
- `stages/filters.reprojection.rst`
- `stages/filters.rst`
- `stages/readers.copc.rst`
- `stages/readers.gdal.rst`
- `stages/readers.i3s.rst`
- `stages/readers.rst`
- `stages/readers.slpk.rst`
- `stages/readers.stac.rst`
- `stages/readers.tiledb.rst`
- `stages/readers.tindex.rst`
- `stages/writers.copc.rst`
- `stages/writers.e57.rst`
- `stages/writers.raster.rst`
- `stages/writers.rst`
- `stages/writers.tiledb.rst`

plus 1 under `tutorial/`, `development/`, `api/` and `project/`, none of which is normative.

## Where to start

- `dimensions.rst` — the dimension list — this is where the four new names are normative, through the `dimension-table.csv` it includes; that table is also where `ClassFlags` is recorded as deprecated in favour of the four
- `stages/readers.las.rst` — the LAS reader you are changing — its own options and the extra-bytes / array-datatype rules it applies. **It does not map dimensions to point formats**, and no page in this corpus does; §6.2 is where that is stated
- `apps/info.rst` — the `--schema` switch. The page shows no schema output, so the shape of a schema record (a dimension's `name`, and its type members) is inherited from the tree you were given and is not written down on any page
- `apps/index.rst` — the `--options` switch: `pdal --options <stage>` lists that stage's option **names** on stdout, and a stage that is not registered lists nothing there — this is how a newly registered stage's options are inspected
- `stages/readers.i3s.rst`, `readers.slpk.rst`, `writers.gdal.rst`, `filters.georeference.rst`, `filters.straighten.rst` — the new stages and the new option
