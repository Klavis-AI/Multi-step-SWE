# spec/2.7.1 — PDAL 2.7.1 documentation

258 files, about 757 kB. This is upstream PDAL's `doc/` tree at the 2.7.1 release with the site theme, slide decks, images, the workshop and the sphinx plumbing removed, so what remains is the prose. Every page is upstream's text, unedited; three files beside them are not from upstream and are named here so nothing in this corpus is a surprise: `LICENSE.txt`, the licence the pages are under; `dimension-table.csv`, which `dimensions.rst` includes and which upstream generates at doc-build time from `pdal/Dimension.json` rather than keeping in the repository; and this `README.md`, which the file count above includes.

## Layout

| Directory | What it holds |
| --- | --- |
| `stages/` | one page per reader, filter and writer — **the normative pages for almost every scored behaviour** |
| `apps/` | one page per `pdal` subcommand (`info`, `translate`, `pipeline`, …) |
| `pipeline.rst`, `dimensions.rst`, `types.rst` | the pipeline JSON model, the dimension table, the type names |
| `tutorial/` | worked examples; useful, not normative |
| `api/`, `development/` | extending the library in C++; background only |
| `project/`, `about.rst`, `community.rst`, … | project pages; not relevant to any scored behaviour |

## What changed since `spec/2.6.0/`

Only this release's corpus ships -- `spec/2.6.0/` is not beside it, so there is no `diff` to run. The summary below is the exact statement, taken from upstream's own tree at both tags: **3 pages added, 0 removed, 29 changed**, 223 unchanged.

Added:

- `stages/filters.griddecimation.rst`
- `stages/filters.h3.rst`
- `stages/filters.sparsesurface.rst`

Changed (29 pages; those that matter to this step are named in `PROFILE.md` §7.4). Under `stages/`, `apps/` and the root:

- `apps/hausdorff.rst`
- `download.rst`
- `index.rst`
- `pipeline.rst`
- `requirements.txt`
- `stages/filters.assign.rst`
- `stages/filters.covariancefeatures.rst`
- `stages/filters.csf.rst`
- `stages/filters.decimation.rst`
- `stages/filters.geomdistance.rst`
- `stages/filters.groupby.rst`
- `stages/filters.hexbin.rst`
- `stages/filters.miniball.rst`
- `stages/filters.overlay.rst`
- `stages/filters.planefit.rst`
- `stages/filters.reciprocity.rst`
- `stages/filters.rst`
- `stages/readers.copc.rst`
- `stages/readers.stac.rst`
- `stages/writers.arrow.rst`
- `stages/writers.copc.rst`
- `stages/writers.las.rst`
- `stages/writers.rst`
- `stages/writers.tiledb.rst`

plus 5 under `tutorial/`, `development/`, `api/` and `project/`, none of which is normative.

## Where to start

- `stages/filters.griddecimation.rst`, `filters.sparsesurface.rst` — **new at this release**
- `stages/filters.assign.rst` — dimension creation and the unary functions — read the whole page, it changed substantially
- `stages/filters.decimation.rst` — the `step` type
- `stages/writers.copc.rst` — the new `extra_dims` option
- `stages/readers.las.rst` — the reader this step's other new option is added to — but the option itself, `ignore_missing_vlrs`, is named on no page in this corpus, so §7.1 above is the whole of its specification
