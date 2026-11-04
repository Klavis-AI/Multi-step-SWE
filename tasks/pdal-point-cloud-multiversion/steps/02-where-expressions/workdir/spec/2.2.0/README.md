# spec/2.2.0 — PDAL 2.2.0 documentation

234 files, about 678 kB. This is upstream PDAL's `doc/` tree at the 2.2.0 release with the site theme, slide decks, images, the workshop and the sphinx plumbing removed, so what remains is the prose. Every page is upstream's text, unedited; three files beside them are not from upstream and are named here so nothing in this corpus is a surprise: `LICENSE.txt`, the licence the pages are under; `dimension-table.csv`, which `dimensions.rst` includes and which upstream generates at doc-build time from `pdal/Dimension.json` rather than keeping in the repository; and this `README.md`, which the file count above includes.

## Layout

| Directory | What it holds |
| --- | --- |
| `stages/` | one page per reader, filter and writer — **the normative pages for almost every scored behaviour** |
| `apps/` | one page per `pdal` subcommand (`info`, `translate`, `pipeline`, …) |
| `pipeline.rst`, `dimensions.rst`, `types.rst` | the pipeline JSON model, the dimension table, the type names |
| `tutorial/` | worked examples; useful, not normative |
| `api/`, `development/` | extending the library in C++; background only |
| `project/`, `about.rst`, `community.rst`, … | project pages; not relevant to any scored behaviour |

## What changed since `spec/2.1.0/`

Only this release's corpus ships -- `spec/2.1.0/` is not beside it, so there is no `diff` to run. The summary below is the exact statement, taken from upstream's own tree at both tags: **15 pages added, 0 removed, 120 changed**, 96 unchanged.

Added:

- `development/release-notes/2.2.0.md`
- `stages/expression.rst`
- `stages/filter_opts.rst`
- `stages/filters.faceraster.rst`
- `stages/filters.julia.rst`
- `stages/filters.litree.rst`
- `stages/filters.lloydkmeans.rst`
- `stages/filters.optimalneighborhood.rst`
- `stages/readers.obj.rst`
- `stages/stages.rst`
- `stages/writer_opts.rst`
- `stages/writers.raster.rst`
- `tutorial/grid-shift.rst`
- `tutorial/lvd_to_nzvd2016.py`
- `tutorial/nz_reproject.json`

Changed (120 pages; those that matter to this step are named in `PROFILE.md` §2.4). Under `stages/`, `apps/` and the root:

- `about.rst`
- `faq.rst`
- `index.rst`
- `java.rst`
- `pipeline.rst`
- `python.rst`
- `references.rst`
- `stages/filters.approximatecoplanar.rst`
- `stages/filters.assign.rst`
- `stages/filters.chipper.rst`
- `stages/filters.cluster.rst`
- `stages/filters.colorinterp.rst`
- `stages/filters.colorization.rst`
- `stages/filters.covariancefeatures.rst`
- `stages/filters.cpd.rst`
- `stages/filters.crop.rst`
- `stages/filters.csf.rst`
- `stages/filters.dbscan.rst`
- `stages/filters.decimation.rst`
- `stages/filters.delaunay.rst`
- `stages/filters.dem.rst`
- `stages/filters.divider.rst`
- `stages/filters.eigenvalues.rst`
- `stages/filters.elm.rst`
- `stages/filters.estimaterank.rst`
- `stages/filters.farthestpointsampling.rst`
- `stages/filters.ferry.rst`
- `stages/filters.greedyprojection.rst`
- `stages/filters.groupby.rst`
- `stages/filters.hag.rst`
- `stages/filters.hag_delaunay.rst`
- `stages/filters.hag_dem.rst`
- `stages/filters.hag_nn.rst`
- `stages/filters.head.rst`
- `stages/filters.hexbin.rst`
- `stages/filters.icp.rst`
- `stages/filters.info.rst`
- `stages/filters.iqr.rst`
- `stages/filters.locate.rst`
- `stages/filters.lof.rst`
- `stages/filters.mad.rst`
- `stages/filters.matlab.rst`
- `stages/filters.merge.rst`
- `stages/filters.miniball.rst`
- `stages/filters.mongo.rst`
- `stages/filters.mortonorder.rst`
- `stages/filters.neighborclassifier.rst`
- `stages/filters.nndistance.rst`
- `stages/filters.normal.rst`
- `stages/filters.outlier.rst`
- `stages/filters.overlay.rst`
- `stages/filters.planefit.rst`
- `stages/filters.pmf.rst`
- `stages/filters.poisson.rst`
- `stages/filters.projpipeline.rst`
- `stages/filters.python.rst`
- `stages/filters.radialdensity.rst`
- `stages/filters.randomize.rst`
- `stages/filters.range.rst`
- `stages/filters.reciprocity.rst`
- `stages/filters.reprojection.rst`
- `stages/filters.returns.rst`
- `stages/filters.rst`
- `stages/filters.sample.rst`
- `stages/filters.separatescanline.rst`
- `stages/filters.shell.rst`
- `stages/filters.skewnessbalancing.rst`
- `stages/filters.smrf.rst`
- `stages/filters.sort.rst`
- `stages/filters.splitter.rst`
- `stages/filters.stats.rst`
- `stages/filters.streamcallback.rst`
- `stages/filters.tail.rst`
- `stages/filters.transformation.rst`
- `stages/filters.voxelcenternearestneighbor.rst`
- `stages/filters.voxelcentroidnearestneighbor.rst`
- `stages/filters.voxeldownsize.rst`
- `stages/reader_opts.rst`
- `stages/readers.bpf.rst`
- `stages/readers.ept.rst`
- `stages/readers.gdal.rst`
- `stages/readers.geowave.rst`
- `stages/readers.i3s.rst`
- `stages/readers.las.rst`
- `stages/readers.oci.rst`
- `stages/readers.pcd.rst`
- `stages/readers.rst`
- `stages/readers.sbet.rst`
- `stages/readers.slpk.rst`
- `stages/readers.terrasolid.rst`
- `stages/readers.text.rst`
- `stages/writers.bpf.rst`
- `stages/writers.e57.rst`
- `stages/writers.ept_addon.rst`
- `stages/writers.fbx.rst`
- `stages/writers.gdal.rst`
- `stages/writers.geowave.rst`
- `stages/writers.gltf.rst`
- `stages/writers.las.rst`
- `stages/writers.matlab.rst`
- `stages/writers.nitf.rst`
- `stages/writers.oci.rst`
- `stages/writers.ogr.rst`
- `stages/writers.pcd.rst`
- `stages/writers.pgpointcloud.rst`
- `stages/writers.ply.rst`
- `stages/writers.rst`
- `stages/writers.sbet.rst`
- `stages/writers.sqlite.rst`
- `stages/writers.text.rst`
- `stages/writers.tiledb.rst`

plus 9 under `tutorial/`, `development/`, `api/` and `project/`, none of which is normative.

## Where to start

- `stages/expression.rst` and `stages/filter_opts.rst` — **new at this release.** The whole expression grammar, the per-point semantics, and `where` / `where_merge`. `filter_opts.rst` is included into every filter page, which is why it reads as boilerplate there and is in fact the specification
- any two `stages/filters.*.rst` pages — to see the `where` block as it appears on an ordinary stage — the include is identical everywhere, which is the point
- `stages/filters.normal.rst` — the `refine` default — the value the option listing reports, not an effect on the normals
- `stages/filters.faceraster.rst`, `filters.optimalneighborhood.rst`, `readers.obj.rst`, `writers.raster.rst`, `filters.litree.rst` — the five new stages
- `stages/filters.hag.rst` — the removal warning at its head
