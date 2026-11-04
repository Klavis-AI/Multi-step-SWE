# spec/2.5.0 — PDAL 2.5.0 documentation

251 files, about 720 kB. This is upstream PDAL's `doc/` tree at the 2.5.0 release with the site theme, slide decks, images, the workshop and the sphinx plumbing removed, so what remains is the prose. Every page is upstream's text, unedited; three files beside them are not from upstream and are named here so nothing in this corpus is a surprise: `LICENSE.txt`, the licence the pages are under; `dimension-table.csv`, which `dimensions.rst` includes and which upstream generates at doc-build time from `pdal/Dimension.json` rather than keeping in the repository; and this `README.md`, which the file count above includes.

## Layout

| Directory | What it holds |
| --- | --- |
| `stages/` | one page per reader, filter and writer — **the normative pages for almost every scored behaviour** |
| `apps/` | one page per `pdal` subcommand (`info`, `translate`, `pipeline`, …) |
| `pipeline.rst`, `dimensions.rst`, `types.rst` | the pipeline JSON model, the dimension table, the type names |
| `tutorial/` | worked examples; useful, not normative |
| `api/`, `development/` | extending the library in C++; background only |
| `project/`, `about.rst`, `community.rst`, … | project pages; not relevant to any scored behaviour |

## What changed since `spec/2.4.0/`

Only this release's corpus ships -- `spec/2.4.0/` is not beside it, so there is no `diff` to run. The summary below is the exact statement, taken from upstream's own tree at both tags: **12 pages added, 1 removed, 27 changed**, 209 unchanged.

Added:

- `api/cpp/columnpointtable.rst`
- `api/cpp/rowpointtable.rst`
- `development/release-notes/2.4.0.md`
- `development/release-notes/2.4.1.md`
- `requirements.txt`
- `stages/filters.expression.rst`
- `stages/filters.geomdistance.rst`
- `stages/filters.trajectory.rst`
- `stages/readers.fbi.rst`
- `stages/readers.ptx.rst`
- `stages/readers.stac.rst`
- `stages/writers.fbi.rst`

Removed (the capability is gone, not merely undocumented):

- `stages/readers.mrsid.rst`

Changed (27 pages; those that matter to this step are named in `PROFILE.md` §5.4). Under `stages/`, `apps/` and the root:

- `about.rst`
- `apps/tile.rst`
- `download.rst`
- `faq.rst`
- `references.rst`
- `stages/filters.overlay.rst`
- `stages/filters.poisson.rst`
- `stages/filters.rst`
- `stages/readers.copc.rst`
- `stages/readers.ept.rst`
- `stages/readers.las.rst`
- `stages/readers.memoryview.rst`
- `stages/readers.rst`
- `stages/readers.tiledb.rst`
- `stages/references.bib`
- `stages/writers.copc.rst`
- `stages/writers.gdal.rst`
- `stages/writers.ogr.rst`
- `stages/writers.rst`
- `stages/writers.sbet.rst`
- `stages/writers.tiledb.rst`

plus 6 under `tutorial/`, `development/`, `api/` and `project/`, none of which is normative.

## Where to start

- `apps/translate.rst` — the command surface the guard protects — which arguments `translate` takes and that `--input`/`--output` are required. **The flag and the guard are not on this page at 2.5.0:** `--overwrite` appears on no upstream page in the shipped corpus, and the page's `-o` is `--output`. They are specified in `PROFILE.md` §5.2, which is where to read them.
- `stages/filters.zsmooth.rst`, `filters.expression.rst`, `filters.geomdistance.rst`, `readers.ptx.rst`, `readers.fbi.rst`, `writers.fbi.rst` — the new stages. **Two things these pages do not carry, both specified in `PROFILE.md`:** the factor `readers.ptx` applies to a row's fourth column when it fills `Intensity` (§5.1 — `readers.ptx.rst` names no scale, and guessing the full `uint16` range instead does not conform), and the fact that no rejection at this step is graded by the wording of its diagnostic, `filters.zsmooth` refusing a `dim` of `Z` included (§5.3).
