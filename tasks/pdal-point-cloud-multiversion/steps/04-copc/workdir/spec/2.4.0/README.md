# spec/2.4.0 — PDAL 2.4.0 documentation

240 files, about 701 kB. This is upstream PDAL's `doc/` tree at the 2.4.0 release with the site theme, slide decks, images, the workshop and the sphinx plumbing removed, so what remains is the prose. Every page is upstream's text, unedited; three files beside them are not from upstream and are named here so nothing in this corpus is a surprise: `LICENSE.txt`, the licence the pages are under; `dimension-table.csv`, which `dimensions.rst` includes and which upstream generates at doc-build time from `pdal/Dimension.json` rather than keeping in the repository; and this `README.md`, which the file count above includes.

## Layout

| Directory | What it holds |
| --- | --- |
| `stages/` | one page per reader, filter and writer — **the normative pages for almost every scored behaviour** |
| `apps/` | one page per `pdal` subcommand (`info`, `translate`, `pipeline`, …) |
| `pipeline.rst`, `dimensions.rst`, `types.rst` | the pipeline JSON model, the dimension table, the type names |
| `tutorial/` | worked examples; useful, not normative |
| `api/`, `development/` | extending the library in C++; background only |
| `project/`, `about.rst`, `community.rst`, … | project pages; not relevant to any scored behaviour |

## What changed since `spec/2.3.0/`

Only this release's corpus ships -- `spec/2.3.0/` is not beside it, so there is no `diff` to run. The summary below is the exact statement, taken from upstream's own tree at both tags: **4 pages added, 3 removed, 45 changed**, 188 unchanged.

Added:

- `development/plugins.rst`
- `stages/readers.copc.rst`
- `stages/readers.smrmsg.rst`
- `stages/writers.copc.rst`

Removed (the capability is gone, not merely undocumented):

- `stages/filters.hag.rst`
- `stages/readers.sqlite.rst`
- `stages/writers.sqlite.rst`

Changed (45 pages; those that matter to this step are named in `PROFILE.md` §4.4). Under `stages/`, `apps/` and the root:

- `apps/index.rst`
- `download.rst`
- `faq.rst`
- `index.rst`
- `java.rst`
- `pipeline.rst`
- `references.rst`
- `stages/expression.rst`
- `stages/filters.crop.rst`
- `stages/filters.decimation.rst`
- `stages/filters.faceraster.rst`
- `stages/filters.litree.rst`
- `stages/filters.normal.rst`
- `stages/filters.optimalneighborhood.rst`
- `stages/filters.poisson.rst`
- `stages/filters.python.rst`
- `stages/filters.rst`
- `stages/filters.sample.rst`
- `stages/filters.shell.rst`
- `stages/filters.transformation.rst`
- `stages/filters.voxelcenternearestneighbor.rst`
- `stages/filters.zsmooth.rst`
- `stages/reader_opts.rst`
- `stages/readers.draco.rst`
- `stages/readers.ept.rst`
- `stages/readers.numpy.rst`
- `stages/readers.rst`
- `stages/readers.rxp.rst`
- `stages/readers.text.rst`
- `stages/readers.tiledb.rst`
- `stages/references.bib`
- `stages/writers.draco.rst`
- `stages/writers.fbx.rst`
- `stages/writers.gdal.rst`
- `stages/writers.gltf.rst`
- `stages/writers.las.rst`
- `stages/writers.rst`
- `stages/writers.tiledb.rst`

plus 7 under `tutorial/`, `development/`, `api/` and `project/`, none of which is normative.

## Where to start

- `stages/writers.copc.rst` and `stages/readers.copc.rst` — **new at this release.** The format, the options, and what a conforming file contains. Two caveats about the shipped pages, both upstream's and neither edited: `readers.copc.rst` lists an option `las` that the 2.4.0 binary registers as `vlr` (only `vlr` is accepted), and the COPC format itself is reachable only as the link `https://copc.io/`, so where this profile and those pages are silent about octree layout the tests are silent too — see §4.3.
- `stages/writers.las.rst` — what `compression` now means
- `pipeline.rst` — the pipeline JSON model. It does **not** carry the `//` comment syntax or the multiple-leaf-node rule, though the shipped `spec/2.4.0/README.md` lists it among the changed pages: both rules are stated in this profile, §4.2, and that is where to read them.
