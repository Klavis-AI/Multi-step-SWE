# Step 5 — The overwrite guard and COPC GpsTime ordering (PDAL 2.5.0)

## 1. Implement this milestone

`/app` holds the PDAL you delivered for step 4 — upstream's point-cloud library and its `pdal` command-line tool at the **2.4.0** level, built by the one command in `/app/TEST_INTERFACE.md` §2. Continue that implementation in place.

Move it from PDAL 2.4.0 to **PDAL 2.5.0** by implementing exactly what `/app/spec/PROFILE.md` §5 puts in scope:

- **`--overwrite` on `pdal translate`** — A flag that restores the previous permissive behaviour.
- **COPC output ordered by `GpsTime`** — Points in a written COPC file come out in GPS-time order: `writers.copc` stores its points sorted by `GpsTime` ascending, whatever order they reach it in.
  **That ordering is within an octree node, and where it is checked the file is a single node — that is stated here, and it is in scope.** A COPC file keeps each node's points contiguously, so on a cloud spread over several nodes the stored sequence runs node by node and GPS-time order holds inside a node rather than across the whole file. §4.3 leaves the node layout to the writer and this does not take that back: a cloud small and coarse enough to sit in one node comes out wholly in GPS-time order, and the fixture these cases read is of that size — a thousand points in a ten-unit cube, and a half-sized subset of it. The tests confirm the single node through an independent COPC reader before they look at the order, so a writer that chooses to subdivide a cloud that small is told exactly that, instead of failing an ordering assertion it cannot explain.
- **`filters.zsmooth`, `filters.expression`, `filters.geomdistance`** — Three new filters: Z smoothing, a stand-alone expression filter over the step-02 grammar, and distance-to-geometry. **`filters.expression` drops the points that fail its expression** — it is a filter, not `where`: the points it excludes are gone from its output, and nothing re-merges them. The option line on its page reuses the wording of `where`'s shared block; the page's own introduction and example describe a stage that filters.
- **`readers.ptx`, `readers.stac`, `readers.fbi` / `writers.fbi`** — New readers and a new writer. `readers.stac` is a remote catalogue reader and is out of scope.
  **`readers.ptx` takes the fourth column of a PTX row as a reflectance in [0,1] and scales it into `Intensity` by 4096, not by 65535: reflectance 0.5 reads back as `Intensity` 2048.** `readers.ptx.rst` states only that the reader ‘infers dimensions from points stored in a text file’ and names no scale, so the factor is stated here and it is required. It is upstream’s own constant, not a convention invented for this task; mapping the column onto the full `uint16` range instead does not conform. **The file's own shape is not on the page either, so it is stated here and it is required:** a PTX file opens with ten header lines — a column count, a row count and the sixteen numbers of a 4x4 registration matrix, one number per line — followed by `x y z reflectance` rows, which the reader emits in file order; the `0 0 0` rows that `discard_missing_points` skips are those rows' x, y and z.
- **Locale-independent numeric output** — Numbers in `pdal info` and in text writers use `.` as the decimal separator whatever `LC_NUMERIC` says. **Stated here, out of scope:** every case runs with `LC_ALL=C` and the image carries no other locale, so no case can observe a difference. Implement it if you cover it anyway; it is not required and nothing fails without it.
- **PROJJSON in SRS metadata** — `pdal info --metadata`'s `srs` object grows a member **named `json`** — the JSON path is `metadata.srs.json`, one member of `srs`, not a member called `srs.json` — carrying the PROJJSON description of the file's coordinate system. At this release it is an escaped JSON string; it becomes a nested object later.
- **`filters.assign`'s `value` option, which this step's pipelines are built on** — Not new here — `PROFILE.md` §2.1 puts it in scope at 2.2.0 — but every in-scope `filters.zsmooth` and `filters.geomdistance` pipeline stages its input through `{"type":"filters.assign","value":…}`, so a tree without the option fails both groups whatever the new filters do. §2.1 is the specification.

and by making the changes §5.2 lists — behaviour that this release **changes or removes**, which is in scope in the changed direction:

- **`pdal translate f.las f.las` now refuses.** Exit status 255, the diagnostic `PDAL: Input and output filenames are equal and no --overwrite option was provided!`, and the input file left byte-for-byte untouched. `--overwrite` restores the old behaviour. This one is quoted exactly because it was observed exactly, on real 2.4.0 and 2.5.0 binaries.
  **The comparison is a literal string comparison of the two positional arguments, and the corpus states it nowhere, so it is stated here.** Two spellings that name the same file on disk are *not* equal to the guard: `translate ./f.las f.las`, `translate f.las ./f.las` and `translate /app/f.las f.las` all run and exit 0 on 2.5.0, and each overwrites the file. A symlink against its own target is likewise unequal. Only the two arguments as given, compared as strings, decide the refusal; do not normalise, do not resolve, and do not stat. The cases used here pass both arguments in the same spelling.

Move the version the tree declares as well — wherever `/app/CMakeLists.txt` now sets it — so that `pdal --version` reports `2.5.0`. That is the handoff convention, not part of the behaviour requirement: the gate only requires that `pdal --version` runs and prints a version in the form the binary already prints (`pdal X.Y.Z` on stdout — change the number, not the format).

Everything steps 1–4 put in scope stays in scope and must keep working. This step's **gate** re-runs one end-to-end path per earlier step; a regression there means the step is not satisfied, regardless of how much new work is complete. `PROFILE.md` is cumulative — §1 through §4 are still there and still binding.

This step covers **9 independent behaviour groups**, each one behaviour family, plus the `gate` group that re-checks inherited behaviour. All of them are in scope: the gate must hold, and the new capability is expected across every group, not just one corner of it. `/app/TEST_INTERFACE.md` §6 lists the groups.

| Group |
| --- |
| `gate` |
| `overwrite-guard` |
| `copc-gpstime-order` |
| `filters-expression` |
| `filters-zsmooth` |
| `filters-geomdistance` |
| `readers-ptx` |
| `fbi-reader-writer` |
| `srs-projjson` |
| `stage-registry` |

---

## 2. Read these local references

`/app/spec/2.5.0/` is upstream PDAL's documentation at **2.5.0**, verbatim — the release you are implementing. `/app/spec/2.5.0/README.md` says what it contains and lists every page that changed since `spec/2.4.0/` -- that earlier corpus is not shipped, so the README's own change summary is what tells you what moved.

Read these two first — they are short and they bound everything else:

1. `/app/spec/PROFILE.md` — **what is in scope and what is not.** §5 is this step's whole assignment; §1–§4 is what must keep working.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the build command, the binary that is run, what is inspected and how, and what is explicitly out of scope.

Then the normative upstream pages under `/app/spec/2.5.0/`, in the order they pay off:

| Read | For |
| --- | --- |
| `apps/translate.rst` | the command surface the guard protects — which arguments `translate` takes and that `--input`/`--output` are required. **The flag and the guard are not on this page at 2.5.0:** `--overwrite` appears on no upstream page in the shipped corpus, and the page's `-o` is `--output`. They are specified in `PROFILE.md` §5.2, which is where to read them. |
| `stages/filters.zsmooth.rst`, `filters.expression.rst`, `filters.geomdistance.rst`, `readers.ptx.rst`, `readers.fbi.rst`, `writers.fbi.rst` | the new stages. **Two things these pages do not carry, both specified in `PROFILE.md`:** the factor `readers.ptx` applies to a row's fourth column when it fills `Intensity` (§5.1 — `readers.ptx.rst` names no scale, and guessing the full `uint16` range instead does not conform), and the fact that no rejection at this step depends on the wording of its diagnostic, `filters.zsmooth` refusing a `dim` of `Z` included (§5.3). |

Small step by line count, exact by assertion. The guard is checked on its status, its message and the input file being untouched.

**Do not read the whole corpus.** It is a reference, not a reading list: over a hundred pages under `stages/` alone. Read the pages the table names, then open others only when a specific behaviour sends you there.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step, what you must not delete, and a **checkpoint you can run by hand**. Run it at the start, before you change anything.

---

## 3. Meta requirements

- **Budget**: 14400 seconds. `bash /app/timer.sh` shows the remaining time; the configured timeout is authoritative and resets each step. A full rebuild of PDAL takes several minutes at `-j8`; budget for it and build incrementally.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what the next step starts from, so keep it building at every point you might be interrupted. Do not delete or move `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or `timer.sh`. The build directory is not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to PDAL itself.** PDAL is BSD-licensed and every release, patch and test file is one download away. Vendoring, transcribing or reconstructing upstream PDAL source for the 2.4.0 → 2.5.0 difference — in whole or in part, by any route, including from memory — is prohibited and defeats the point of the task. The shipped documentation describes the *behaviour* you must reproduce; the implementation must be yours.
- **Compression libraries are dependencies, not answers.** No LAZ codec is installed — neither laz-perf nor LASzip — and `cmake/lazperf.cmake` only looks for an external one, so anything that reads or writes LAZ-compressed data needs one brought into the tree. This environment has no network to obtain one from, so the library's source is provided offline instead: **laz-perf 3.2.0, the general-purpose LAZ codec, at `/opt/laz-perf`** (LGPL-2.1; its `README.md` and `PROVENANCE.txt` say what it is and where it came from). Using it is permitted and is not the 2.4.0 → 2.5.0 answer: it is a dependency, not PDAL, and the prohibition above is about PDAL's own source. It has to end up inside `/app` — vendor it, for example `cp -r /opt/laz-perf /app/vendor/lazperf` — because the build that checks the work has no network and no `/opt`, so a library installed only into the system during your session is gone by the time the build is checked.
- **Off-limits**: do not inspect `/tests`, later step directories, external test fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — the `TEST_INTERFACE.md` §2 build works offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint passes.
