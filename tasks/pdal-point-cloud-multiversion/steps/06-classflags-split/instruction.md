# Step 6 — The `ClassFlags` split into four dimensions (PDAL 2.6.0)

## 1. Implement this milestone

`/app` holds the PDAL you delivered for step 5 — upstream's point-cloud library and its `pdal` command-line tool at the **2.5.0** level, built by the one command in `/app/TEST_INTERFACE.md` §2. Continue that implementation in place.

Move it from PDAL 2.5.0 to **PDAL 2.6.0** by implementing exactly what `/app/spec/PROFILE.md` §6 puts in scope:

- **`readers.i3s` and `readers.slpk` always present** — The two stages are in the default build and appear in `pdal --drivers`.
- **`writers.gdal` `allow_empty`** — Writing a raster from an empty point set becomes an option rather than an error. It is a boolean and its default is `false`.
  **Both directions are in scope, on a point set that really is empty.** With `allow_empty` **true** and the raster’s grid given explicitly — `origin_x`, `origin_y`, `width` and `height`, all four on `writers.gdal.rst` — the pipeline succeeds and the raster file is written. With `allow_empty` **false** — or with the option left out altogether, since `false` is its default — and the raster sized from the points alone (`resolution` and no explicit grid), the writer still refuses: non-zero exit, no output file, and the diagnostic `Unable to write GDAL data with no points`.
  **That diagnostic is inherited, not new** — the 2.5.0 tree you start from already emits it on this path — and it is the one message this step pins, as an explicit addition to §5.3’s rule that the wording of a rejection is not part of the requirement. §5.3 still holds everywhere else here; only the new option changes what this path does.
- **`filters.georeference` and `filters.straighten`** — Two new filters. All four new stages — the two readers and these two filters — appear in `pdal --drivers`.
- **`filters.assign`'s `value` option, which this step's bit-order case is built on** — Not new here — `PROFILE.md` §2.1 puts it in scope at 2.2.0 — but `flag-bits-map-to-the-four-dimensions` sets each flag through `{"type":"filters.assign","value":"Withheld = 1"}` and the like, so a tree without the option fails that case whatever the split does. §2.1 is the specification.

and by making the changes §6.2 lists — behaviour that this release **changes or removes**, which is in scope in the changed direction:

- **`ClassFlags` splits into `Synthetic`, `KeyPoint`, `Withheld` and `Overlap`.** `pdal info --schema` lists the four, and `ClassFlags` is no longer among them.
  **The four belong to PDAL’s point model, not to a LAS point format.** `pdal info --schema` reports all four for **every** point data record format — including LAS 1.2 PDRF 3 files, which never carried a packed class-flags byte at all — and a PDRF 6 file written back out through `writers.las`, which re-encodes it as LAS 1.2 PDRF 3, still reports all four. No upstream page in the corpus says this: `stages/readers.las.rst` maps no dimension to a point format and `dimension-table.csv` names the four without qualifying them by format, so it is stated here and it is required. Registering the four only where the byte they replace lived — LAS 1.4 — does not conform.
  **The four report the bits the packed classification-flags byte carried, in that order:** bit 0 `Synthetic`, bit 1 `KeyPoint`, bit 2 `Withheld`, bit 3 `Overlap`. A point whose packed byte was 4 therefore reports `Withheld` 1 and the other three 0, and a file written back out through `writers.las` as LAS 1.4 PDRF 6 re-encodes the four into that same byte in that same order. No upstream page in the corpus says this either — `dimension-table.csv` names the four without giving bit positions — so it is stated here and it is required: registering the four and leaving them zero does not conform.
- **PROJJSON is serialised as a nested JSON object,** not as a string containing escaped JSON. The member is `srs.json` in `pdal info --metadata` output.

Move the version the tree declares as well — wherever `/app/CMakeLists.txt` now sets it — so that `pdal --version` reports `2.6.0`. That is the handoff convention, not part of the behaviour requirement: the gate only requires that `pdal --version` runs and prints a version in the form the binary already prints (`pdal X.Y.Z` on stdout — change the number, not the format).

Everything steps 1–5 put in scope stays in scope and must keep working. This step's **gate** re-runs one end-to-end path per earlier step; a regression there means the step is not satisfied, regardless of how much new work is complete. `PROFILE.md` is cumulative — §1 through §5 are still there and still binding.

This step covers **5 independent behaviour groups**, each one behaviour family, plus the `gate` group that re-checks inherited behaviour. All of them are in scope: the gate must hold, and the new capability is expected across every group, not just one corner of it. `/app/TEST_INTERFACE.md` §6 lists the groups.

| Group |
| --- |
| `gate` |
| `classflags-split` |
| `new-stage-options` |
| `projjson-object` |
| `new-stage-registry` |
| `gdal-allow-empty` |

---

## 2. Read these local references

`/app/spec/2.6.0/` is upstream PDAL's documentation at **2.6.0**, verbatim — the release you are implementing. `/app/spec/2.6.0/README.md` says what it contains and lists every page that changed since `spec/2.5.0/` -- that earlier corpus is not shipped, so the README's own change summary is what tells you what moved.

Read these two first — they are short and they bound everything else:

1. `/app/spec/PROFILE.md` — **what is in scope and what is not.** §6 is this step's whole assignment; §1–§5 is what must keep working.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the build command, the binary that is run, what is inspected and how, and what is explicitly out of scope.

Then the normative upstream pages under `/app/spec/2.6.0/`, in the order they pay off:

| Read | For |
| --- | --- |
| `dimensions.rst` | the dimension list — this is where the four new names are normative, through the `dimension-table.csv` it includes; that table is also where `ClassFlags` is recorded as deprecated in favour of the four |
| `stages/readers.las.rst` | the LAS reader you are changing — its own options and the extra-bytes / array-datatype rules it applies. **It does not map dimensions to point formats**, and no page in this corpus does; §6.2 is where that is stated |
| `apps/info.rst` | the `--schema` switch. The page shows no schema output, so the shape of a schema record (a dimension's `name`, and its type members) is inherited from the tree you were given and is not written down on any page |
| `apps/index.rst` | the `--options` switch: `pdal --options <stage>` lists that stage's option **names** on stdout, and a stage that is not registered lists nothing there — this is how a newly registered stage's options are inspected |
| `stages/readers.i3s.rst`, `readers.slpk.rst`, `writers.gdal.rst`, `filters.georeference.rst`, `filters.straighten.rst` | the new stages and the new option |

The split touches the reader, the writer, the schema report and every stage that names the old dimension. Find them all before you start.

**Do not read the whole corpus.** It is a reference, not a reading list: over a hundred pages under `stages/` alone. Read the pages the table names, then open others only when a specific behaviour sends you there.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step, what you must not delete, and a **checkpoint you can run by hand**. Run it at the start, before you change anything.

---

## 3. Meta requirements

- **Budget**: 14400 seconds. `bash /app/timer.sh` shows the remaining time; the configured timeout is authoritative and resets each step. A full rebuild of PDAL takes several minutes at `-j8`; budget for it and build incrementally.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what the next step starts from, so keep it building at every point you might be interrupted. Do not delete or move `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or `timer.sh`. The build directory is not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to PDAL itself.** PDAL is BSD-licensed and every release, patch and test file is one download away. Vendoring, transcribing or reconstructing upstream PDAL source for the 2.5.0 → 2.6.0 difference — in whole or in part, by any route, including from memory — is prohibited and defeats the point of the task. The shipped documentation describes the *behaviour* you must reproduce; the implementation must be yours.
- **Compression libraries are dependencies, not answers.** No LAZ codec is installed — neither laz-perf nor LASzip — and `cmake/lazperf.cmake` only looks for an external one, so anything that reads or writes LAZ-compressed data needs one brought into the tree. This environment has no network to obtain one from, so the library's source is provided offline instead: **laz-perf 3.2.0, the general-purpose LAZ codec, at `/opt/laz-perf`** (LGPL-2.1; its `README.md` and `PROVENANCE.txt` say what it is and where it came from). Using it is permitted and is not the 2.5.0 → 2.6.0 answer: it is a dependency, not PDAL, and the prohibition above is about PDAL's own source. It has to end up inside `/app` — vendor it, for example `cp -r /opt/laz-perf /app/vendor/lazperf` — because the build that checks the work has no network and no `/opt`, so a library installed only into the system during your session is gone by the time the build is checked.
- **Off-limits**: do not inspect `/tests`, later step directories, external test fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — the `TEST_INTERFACE.md` §2 build works offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint passes.
