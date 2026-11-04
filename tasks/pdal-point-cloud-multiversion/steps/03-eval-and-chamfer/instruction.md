# Step 3 — The `eval` and `chamfer` subcommands (PDAL 2.3.0)

## 1. Implement this milestone

`/app` holds the PDAL you delivered for step 2 — upstream's point-cloud library and its `pdal` command-line tool at the **2.2.0** level, built by the one command in `/app/TEST_INTERFACE.md` §2. Continue that implementation in place.

Move it from PDAL 2.2.0 to **PDAL 2.3.0** by implementing exactly what `/app/spec/PROFILE.md` §3 puts in scope:

- **`pdal eval`** — A new subcommand comparing a classified point cloud against a truth cloud and reporting a confusion matrix. Its page is `apps/eval.rst`, whose worked example shows the JSON it prints. **Four conventions the page does not state are stated here, and they are required.** The summary `f1_score` is `2 * mean_intersection_over_union / (1 + mean_intersection_over_union)`. A label named in `--labels` that is absent from the data keeps its entry in `labels` with support 0 and 0.0 for every per-label metric, and it still counts in the `mean_intersection_over_union` denominator. Each `labels[]` entry's `label` is the class value as it was passed to `--labels`, not a 1-based index — where the page's example prints `"label": "1"` and `"2"` beside `--labels 2,4`, the example is stale and the required values are `"2"` and `"4"`. And `--labels` is required: a run that omits it is refused, with a non-zero exit and the option named in the diagnostic.
- **`pdal chamfer`** — A new subcommand reporting the Chamfer distance between two clouds. Its page is `apps/chamfer.rst`. The value is a float and is compared only with an explicit tolerance.
- **`filters.gpstimeconvert`** — Converts between GPS time conventions. The relationship between GPS time (gt) and GPS standard time (gst) is: gst = gt - 1,000,000,000 s. Equivalently, gt = gst + 1,000,000,000 s. The constant 1,000,000,000 seconds (1e9 s) is the GPS standard time epoch offset. **The week arithmetic is not on the page either, so it is stated here and it is required:** GPS time counts seconds from the GPS epoch, 1980-01-06T00:00:00Z; a GPS week is 604800 s and week 0 begins at that epoch. "GPS week seconds" are the seconds elapsed since the start of the containing week, so the standard epoch sits 265600 s into a week rather than on a week boundary. `gws2gt` and `gws2gst` resolve which week from `start_date` — the GPS week containing that date — and add the input week seconds to that week's start.
- **`filters.relaxationdartthrowing`** — Poisson-disk style thinning.

and by making the changes §3.2 lists — behaviour that this release **changes or removes**, which is in scope in the changed direction:

- **`writers.pcd` writes X, Y and Z as 32-bit floats.** The PCD header reads `SIZE 4 4 4` and `TYPE F F F` for those fields, where 2.2.0 wrote doubles. **An untyped `order` entry does not widen them:** the per-dimension type annotation on `order` is optional, and an entry with no annotation keeps the dimension's written type — four bytes for X, Y and Z, eight for every other dimension. The page's sentence that an untyped entry's "default dimension type is double precision float" does not apply to X, Y and Z; only an explicit annotation such as `=Double` widens a field to eight bytes.
- **`filters.head` applies `count` per `PointView`,** not once across the whole pipeline.
- **`filters.fps` returns every point when `count` exceeds the input size,** rather than failing.

Move the version the tree declares as well — wherever `/app/CMakeLists.txt` now sets it — so that `pdal --version` reports `2.3.0`. That is the handoff convention, not part of the behaviour requirement: the gate only requires that `pdal --version` runs and prints a version in the form the binary already prints (`pdal X.Y.Z` on stdout — change the number, not the format).

Everything steps 1–2 put in scope stays in scope and must keep working. This step's **gate** re-runs one end-to-end path per earlier step; a regression there means the step is not satisfied, regardless of how much new work is complete. `PROFILE.md` is cumulative — §1 through §2 are still there and still binding.

This step covers **9 independent behaviour groups**, each one behaviour family, plus the `gate` group that re-checks inherited behaviour. All of them are in scope: the gate must hold, and the new capability is expected across every group, not just one corner of it. `/app/TEST_INTERFACE.md` §6 lists the groups.

| Group |
| --- |
| `gate` |
| `eval-command` |
| `chamfer-command` |
| `subcommand-surface` |
| `gpstimeconvert` |
| `relaxationdartthrowing` |
| `new-stage-registry` |
| `pcd-single-precision` |
| `head-per-pointview` |
| `fps-count-exceeds-input` |

---

## 2. Read these local references

`/app/spec/2.3.0/` is upstream PDAL's documentation at **2.3.0**, verbatim — the release you are implementing. `/app/spec/2.3.0/README.md` says what it contains and lists every page that changed since `spec/2.2.0/` -- that earlier corpus is not shipped, so the README's own change summary is what tells you what moved.

Read these two first — they are short and they bound everything else:

1. `/app/spec/PROFILE.md` — **what is in scope and what is not.** §3 is this step's whole assignment; §1–§2 is what must keep working.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the build command, the binary that is run, what is inspected and how, and what is explicitly out of scope.

Then the normative upstream pages under `/app/spec/2.3.0/`, in the order they pay off:

| Read | For |
| --- | --- |
| `apps/eval.rst` and `apps/chamfer.rst` | **new at this release.** The two subcommands: their arguments, their output shape, and their exit behaviour |
| `apps/index.rst` | how a subcommand is registered and listed |
| `stages/writers.pcd.rst` | the field types written to the PCD header |
| `stages/filters.fps.rst`, `filters.head.rst` | `filters.fps` is now documented under its real name; the `count` behaviours of both |
| `stages/filters.gpstimeconvert.rst`, `filters.relaxationdartthrowing.rst` | the two new stages |

The two subcommands are the step. They are small programs over machinery you already have, but they are a new dispatch path.

**Do not read the whole corpus.** It is a reference, not a reading list: over a hundred pages under `stages/` alone. Read the pages the table names, then open others only when a specific behaviour sends you there.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step, what you must not delete, and a **checkpoint you can run by hand**. Run it at the start, before you change anything.

---

## 3. Meta requirements

- **Budget**: 14400 seconds. `bash /app/timer.sh` shows the remaining time; the configured timeout is authoritative and resets each step. A full rebuild of PDAL takes several minutes at `-j8`; budget for it and build incrementally.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what the next step starts from, so keep it building at every point you might be interrupted. Do not delete or move `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or `timer.sh`. The build directory is not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to PDAL itself.** PDAL is BSD-licensed and every release, patch and test file is one download away. Vendoring, transcribing or reconstructing upstream PDAL source for the 2.2.0 → 2.3.0 difference — in whole or in part, by any route, including from memory — is prohibited and defeats the point of the task. The shipped documentation describes the *behaviour* you must reproduce; the implementation must be yours.
- **Off-limits**: do not inspect `/tests`, later step directories, external test fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — the `TEST_INTERFACE.md` §2 build works offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint passes.
