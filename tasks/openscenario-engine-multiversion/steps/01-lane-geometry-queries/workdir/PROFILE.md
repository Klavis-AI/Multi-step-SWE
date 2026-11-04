# Environment profile

What you have in this container, and what you do not. Read `TEST_INTERFACE.md`
for the contract the checker holds you to; this file is only about the machine.

## Toolchain

Debian bookworm-slim, with `build-essential` (gcc/g++ 12, GNU make), `cmake`,
`git`, `patch`, `pkg-config`, `python3`, plus `jq`, `ripgrep`, `file`, `gawk`,
`diffutils`, `unzip` and `xz-utils`. The verifier image installs the same
compiler and the same CMake, so a build that works here works there.

## Do not depend on the network

Whether or not this container can reach the outside world is not something a
solution may rely on, and the build certainly may not: nothing that fetches is
available to it. `apt-get install`, `pip install`, `git clone` from a remote,
`FetchContent_Declare(... GIT_REPOSITORY ...)` and CMake's
`file(DOWNLOAD ...)` are all outside what the build can use, and anything it
needs must already be on this filesystem — which is why the sources below are
provided.

This matters because upstream esmini downloads prebuilt third-party binaries
during `cmake` configure unless they are switched off or already present. The
checker runs with every one of those switches off and pre-creates the empty
destination directories; if you add a new download, the build will hang until
its timeout and the step scores zero.

## Vendored third-party sources

`/opt/esmini-vendor` holds the sources upstream carries as git submodules,
checked out at the exact commits upstream pins:

- `/opt/esmini-vendor/fmt` — fmtlib/fmt, MIT
- `/opt/esmini-vendor/spdlog` — gabime/spdlog, MIT

They are read-only reference copies outside the workspace. They are not
graded, and they are not on any include path by default. If the project you
are building expects one of them under `externals/`, copy it in — a copy under
`/app` is part of your workspace and will be collected with it.

## The workspace

`/app` is the workspace and the only thing carried to the next step. Work
there. Files you leave outside `/app` are discarded when the step ends.

`/app/timer.sh` prints how much of the step's wall clock is left. It is
informational; the hard limit is enforced outside the container.

## Parallelism

Four cores' worth of budget. `cmake --build --parallel 4` is what the checker
uses; going wider does not make your build finish sooner and can push it past
the build timeout.
