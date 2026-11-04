# spec/2.1.0 — PDAL 2.1.0 documentation

219 files, about 627 kB. This is upstream PDAL's `doc/` tree at the 2.1.0 release with the site theme, slide decks, images, the workshop and the sphinx plumbing removed, so what remains is the prose. Every page is upstream's text, unedited; three files beside them are not from upstream and are named here so nothing in this corpus is a surprise: `LICENSE.txt`, the licence the pages are under; `dimension-table.csv`, which `dimensions.rst` includes and which upstream generates at doc-build time from `pdal/Dimension.json` rather than keeping in the repository; and this `README.md`, which the file count above includes.

## Layout

| Directory | What it holds |
| --- | --- |
| `stages/` | one page per reader, filter and writer — **the normative pages for almost every scored behaviour** |
| `apps/` | one page per `pdal` subcommand (`info`, `translate`, `pipeline`, …) |
| `pipeline.rst`, `dimensions.rst`, `types.rst` | the pipeline JSON model, the dimension table, the type names |
| `tutorial/` | worked examples; useful, not normative |
| `api/`, `development/` | extending the library in C++; background only |
| `project/`, `about.rst`, `community.rst`, … | project pages; not relevant to any scored behaviour |

## This step

This is the first corpus you receive. `/app` holds a working PDAL 2.0.1, and this directory documents 2.1.0 — the release you are implementing. The differences that matter are listed in `PROFILE.md` §1; the pages to start from are in `../../steps/…/instruction.md`'s reading table and repeated in `PROFILE.md` §1.4.

## Where to start

- `stages/filters.crop.rst` — the `bounds` grammar and the sentence that makes the box three-dimensional — this is the single highest-value page of the step. One upstream typo to know about: its 3-D note reads `([xmin, xmax, zmax], [ymin, ymax, zmax])`, which the parser does not accept. The form it accepts, and the one every graded case uses, is the one in `PROFILE.md` §1.2: `([xmin,xmax],[ymin,ymax],[zmin,zmax])`
- `stages/filters.hag_nn.rst`, `filters.hag_delaunay.rst`, `filters.hag_dem.rst` — the three height-above-ground methods and the dimension they write
- `stages/filters.voxeldownsize.rst`, `filters.dbscan.rst`, `filters.csf.rst`, `filters.separatescanline.rst`, `filters.skewnessbalancing.rst`, `filters.farthestpointsampling.rst` — the rest of the new cohort
- `stages/writers.las.rst` — `filesource_id`, and the header fields the writer controls
- `stages/filters.randomize.rst` — the stage `seed` is added to. Its option table at this release still reads `None` — upstream did not update the page — so `PROFILE.md` §1.1 is where `seed` is specified, not this page
- `stages/writers.ply.rst` — the accepted `storage_mode` values
- `pipeline.rst`, `apps/translate.rst`, `apps/info.rst` — the pipeline JSON model and the two applications every group drives. You already have a working 2.0.1, so read these only where a group surprises you
