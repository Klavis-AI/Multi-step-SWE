# spec/2.3.0 — PDAL 2.3.0 documentation

239 files, about 696 kB. This is upstream PDAL's `doc/` tree at the 2.3.0 release with the site theme, slide decks, images, the workshop and the sphinx plumbing removed, so what remains is the prose. Every page is upstream's text, unedited; three files beside them are not from upstream and are named here so nothing in this corpus is a surprise: `LICENSE.txt`, the licence the pages are under; `dimension-table.csv`, which `dimensions.rst` includes and which upstream generates at doc-build time from `pdal/Dimension.json` rather than keeping in the repository; and this `README.md`, which the file count above includes.

## Layout

| Directory | What it holds |
| --- | --- |
| `stages/` | one page per reader, filter and writer — **the normative pages for almost every scored behaviour** |
| `apps/` | one page per `pdal` subcommand (`info`, `translate`, `pipeline`, …) |
| `pipeline.rst`, `dimensions.rst`, `types.rst` | the pipeline JSON model, the dimension table, the type names |
| `tutorial/` | worked examples; useful, not normative |
| `api/`, `development/` | extending the library in C++; background only |
| `project/`, `about.rst`, `community.rst`, … | project pages; not relevant to any scored behaviour |

## What changed since `spec/2.2.0/`

Only this release's corpus ships -- `spec/2.2.0/` is not beside it, so there is no `diff` to run. The summary below is the exact statement, taken from upstream's own tree at both tags: **10 pages added, 5 removed, 33 changed**, 193 unchanged.

Added:

- `apps/chamfer.rst`
- `apps/eval.rst`
- `development/release-notes/2.3.0.md`
- `stages/filters.fps.rst`
- `stages/filters.gpstimeconvert.rst`
- `stages/filters.relaxationdartthrowing.rst`
- `stages/filters.teaser.rst`
- `stages/filters.zsmooth.rst`
- `stages/readers.draco.rst`
- `stages/writers.draco.rst`

Removed (the capability is gone, not merely undocumented):

- `stages/filters.farthestpointsampling.rst`
- `stages/readers.geowave.rst`
- `stages/readers.oci.rst`
- `stages/writers.geowave.rst`
- `stages/writers.oci.rst`

Changed (33 pages; those that matter to this step are named in `PROFILE.md` §3.4). Under `stages/`, `apps/` and the root:

- `apps/hausdorff.rst`
- `apps/pipeline.rst`
- `apps/translate.rst`
- `download.rst`
- `index.rst`
- `references.rst`
- `stages/filters.assign.rst`
- `stages/filters.cluster.rst`
- `stages/filters.icp.rst`
- `stages/filters.litree.rst`
- `stages/filters.lof.rst`
- `stages/filters.reprojection.rst`
- `stages/filters.rst`
- `stages/filters.sample.rst`
- `stages/filters.smrf.rst`
- `stages/readers.ept.rst`
- `stages/readers.gdal.rst`
- `stages/readers.i3s.rst`
- `stages/readers.las.rst`
- `stages/readers.mrsid.rst`
- `stages/readers.rst`
- `stages/readers.slpk.rst`
- `stages/writer_opts.rst`
- `stages/writers.gdal.rst`
- `stages/writers.ogr.rst`
- `stages/writers.pcd.rst`
- `stages/writers.raster.rst`
- `stages/writers.rst`

plus 5 under `tutorial/`, `development/`, `api/` and `project/`, none of which is normative.

## Where to start

- `apps/eval.rst` and `apps/chamfer.rst` — **new at this release.** The two subcommands: their arguments, their output shape, and their exit behaviour
- `apps/index.rst` — how a subcommand is registered and listed
- `stages/writers.pcd.rst` — the field types written to the PCD header
- `stages/filters.fps.rst`, `filters.head.rst` — `filters.fps` is now documented under its real name; the `count` behaviours of both
- `stages/filters.gpstimeconvert.rst`, `filters.relaxationdartthrowing.rst` — the two new stages
