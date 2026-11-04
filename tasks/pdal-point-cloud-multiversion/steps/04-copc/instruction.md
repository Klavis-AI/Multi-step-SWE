# Step 4 — COPC read and write (PDAL 2.4.0)

## 1. Implement this milestone

`/app` holds the PDAL you delivered for step 3 — upstream's point-cloud library and its `pdal` command-line tool at the **2.3.0** level, built by the one command in `/app/TEST_INTERFACE.md` §2. Continue that implementation in place.

Move it from PDAL 2.3.0 to **PDAL 2.4.0** by implementing exactly what `/app/spec/PROFILE.md` §4 puts in scope:

- **`readers.copc` and `writers.copc`** — COPC is a LAZ file with a defined octree VLR layout. A file your `writers.copc` produces is opened by a third-party COPC reader, so the bytes have to be right, not merely round-trippable through your own code.
- **`readers.smrmsg`** — Reads Applanix SMRMSG files.

and by making the changes §4.2 lists — behaviour that this release **changes or removes**, which is in scope in the changed direction:

- **The LASzip back-end is gone.** `compression` remains an accepted option on `writers.las`, but lazperf is always what compresses; no value of `compression` selects LASzip and no value is an error.
- **Multiple leaf nodes in a pipeline log an explicit error** instead of silently using one.
- **`//` comments are accepted in pipeline JSON.** At 2.3.0 a `//` line was a parse failure.
- **A zero-point LAS header carries zero extents** — min and max are forced to 0 rather than left at the initialised sentinel — and writing a zero-point LAZ succeeds instead of failing.

Move the version the tree declares as well — wherever `/app/CMakeLists.txt` now sets it — so that `pdal --version` reports `2.4.0`. That is the handoff convention, not part of the behaviour requirement: the gate only requires that `pdal --version` runs and prints a version in the form the binary already prints (`pdal X.Y.Z` on stdout — change the number, not the format).

Everything steps 1–3 put in scope stays in scope and must keep working. This step's **gate** re-runs one end-to-end path per earlier step **and** carries this release's own `compression` acceptances (`bogus` and `false` must both be accepted and write a readable LAZ) and the CLI's failure paths, so skipping `compression-backend` also fails the gate; a regression there means the step is not satisfied, regardless of how much new work is complete. `PROFILE.md` is cumulative — §1 through §3 are still there and still binding.

This step covers **8 independent behaviour groups**, each one behaviour family, plus the `gate` group that re-checks inherited behaviour. All of them are in scope: the gate must hold, and the new capability is expected across every group, not just one corner of it. `/app/TEST_INTERFACE.md` §6 lists the groups.

| Group |
| --- |
| `gate` |
| `copc-write` |
| `copc-read` |
| `copc-roundtrip` |
| `compression-backend` |
| `leaf-node-error` |
| `json-comments` |
| `zero-point-header` |
| `stage-registry` |

---

## 2. Read these local references

`/app/spec/2.4.0/` is upstream PDAL's documentation at **2.4.0**, verbatim — the release you are implementing. `/app/spec/2.4.0/README.md` says what it contains and lists every page that changed since `spec/2.3.0/` -- that earlier corpus is not shipped, so the README's own change summary is what tells you what moved.

Read these two first — they are short and they bound everything else:

1. `/app/spec/PROFILE.md` — **what is in scope and what is not.** §4 is this step's whole assignment; §1–§3 is what must keep working.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the build command, the binary that is run, what is inspected and how, and what is explicitly out of scope.

Then the normative upstream pages under `/app/spec/2.4.0/`, in the order they pay off:

| Read | For |
| --- | --- |
| `stages/writers.copc.rst` and `stages/readers.copc.rst` | **new at this release.** The format, the options, and what a conforming file contains. Two caveats about the shipped pages, both upstream's and neither edited: `readers.copc.rst` lists an option `las` that the 2.4.0 binary registers as `vlr` (only `vlr` is accepted), and the COPC format itself is reachable only as the link `https://copc.io/`, so where this profile and those pages are silent about octree layout the tests are silent too — see §4.3. |
| `stages/writers.las.rst` | what `compression` now means. One caveat, upstream's and not edited: the page still describes `laszip`/`true` as selecting the LasZip compressor and says the build must have support for the requested compressor — §4.2's statement (lazperf always compresses; no value is an error) governs, and the cases used here follow it |
| `pipeline.rst` | the pipeline JSON model. It does **not** carry the `//` comment syntax or the multiple-leaf-node rule, though the shipped `spec/2.4.0/README.md` lists it among the changed pages: both rules are stated in this profile, §4.2, and that is where to read them. |

COPC is the whole step and it is a format, not an option. Budget for getting the octree VLR and the chunk table exactly right; an independent reader is unforgiving.

**Do not read the whole corpus.** It is a reference, not a reading list: over a hundred pages under `stages/` alone. Read the pages the table names, then open others only when a specific behaviour sends you there.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step, what you must not delete, and a **checkpoint you can run by hand**. Run it at the start, before you change anything.

---

## 3. Meta requirements

- **Budget**: 14400 seconds. `bash /app/timer.sh` shows the remaining time; the configured timeout is authoritative and resets each step. A full rebuild of PDAL takes several minutes at `-j8`; budget for it and build incrementally.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what the next step starts from, so keep it building at every point you might be interrupted. Do not delete or move `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` or `timer.sh`. The build directory is not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to PDAL itself.** PDAL is BSD-licensed and every release, patch and test file is one download away. Vendoring, transcribing or reconstructing upstream PDAL source for the 2.3.0 → 2.4.0 difference — in whole or in part, by any route, including from memory — is prohibited and defeats the point of the task. The shipped documentation describes the *behaviour* you must reproduce; the implementation must be yours.
- **Compression libraries are dependencies, not answers.** No LAZ codec is installed — neither laz-perf nor LASzip — and `cmake/lazperf.cmake` only looks for an external one, so anything that reads or writes LAZ-compressed data needs one brought into the tree. This environment has no network to obtain one from, so the library's source is provided offline instead: **laz-perf 3.2.0, the general-purpose LAZ codec, at `/opt/laz-perf`** (LGPL-2.1; its `README.md` and `PROVENANCE.txt` say what it is and where it came from). Using it is permitted and is not the 2.3.0 → 2.4.0 answer: it is a dependency, not PDAL, and the prohibition above is about PDAL's own source. It has to end up inside `/app` — vendor it, for example `cp -r /opt/laz-perf /app/vendor/lazperf` — because the build that checks the work has no network and no `/opt`, so a library installed only into the system during your session is gone by the time the build is checked.
- **Off-limits**: do not inspect `/tests`, later step directories, external test fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — the `TEST_INTERFACE.md` §2 build works offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint passes.
