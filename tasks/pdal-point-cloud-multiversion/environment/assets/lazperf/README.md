# laz-perf 3.2.0 — the LAZ codec, provided offline

This directory is the source tree of **laz-perf 3.2.0**, the general-purpose
LAZ compression library (LGPL-2.1). It is in the image because the agent phase
of this task has no general network access, and anything that reads or writes
LAZ-compressed data — `writers.las` with compression, `readers.copc`,
`writers.copc` — needs a LAZ codec.

* `lazperf/` — the library's sources and headers, copied verbatim from the
  3.2.0 release (`cpp/lazperf/` in the release tarball; 3.2.0 differs from
  3.1.0 only in version macros).
* `CMakeLists.txt` — a build file written for this image; it is not part of the
  release. Upstream's own top-level `CMakeLists.txt` cannot be used here: with
  its default `WITH_TESTS=ON` it runs `file(DOWNLOAD ... autzen.laz)` at
  configure time, and that download cannot succeed offline. This one builds the
  library and nothing else and defines one target, `lazperf` (static,
  position-independent, alias `lazperf::lazperf`).
* `COPYING` — the library's licence, LGPL-2.1, unmodified.
* `PROVENANCE.txt` — where the bytes came from, with a sha256 for every file.

## Using it

The grader rebuilds `/app` from source, in a container that has **no network
and no `/opt/laz-perf`** — only what is inside `/app` travels with your work.
So the library has to end up inside `/app`. Copy this tree in, for example

    cp -r /opt/laz-perf /app/vendor/lazperf

and wire it into the build the way `/app` builds its other vendored libraries:
`add_subdirectory(...)` plus `target_link_libraries(...)`. The include path
must expose this directory so that `#include <lazperf/...>` resolves — the
`lazperf` target already carries that include directory as an INTERFACE
property, so linking it is usually enough.

## What this is and is not

laz-perf is a **dependency**, not an answer: a compression codec, not PDAL. It
contains no filter, no reader or writer of this task's milestones, and no COPC
code — the octree, the VLRs, the chunk table and every graded behaviour remain
yours to write. Vendoring this copy is permitted, and it is what the release
you are implementing does upstream (PDAL vendors this same library from 2.4.0
on). The prohibition is on upstream **PDAL's own** source, including its
integration of this library.

The library is compiled here with no changes; if your build wants a different
shape (a shared library, a different target name, a namespace), adapt the
`CMakeLists.txt` you copied, not the library's sources.
