# Test interface — step 10 (target release 3.8.1)

This file is the contract between your workspace and the automated checker.
Everything stated here is stable for every step of this task. Nothing else
about the checker is published: the cases themselves, their fixtures and their
expected values live outside the workspace and you will never see them.

You are free to restructure the project however you like — move files, rename
internal classes, change the CMake layout — as long as the four statements
below still hold when you are done.

## 1. The build

The checker copies your `/app` into a throwaway container and runs exactly:

```
cmake -S . -B build --no-warn-unused-cli -Wno-dev \
    -DCMAKE_BUILD_TYPE=Release -DFORCE_DOWNLOAD_BINARIES=False -DDOWNLOAD_EXTERNALS=False -DUSE_OSG=False -DUSE_OSI=False -DUSE_SUMO=False -DUSE_GTEST=False -DUSE_IMPLOT=False -DUSE_PROJ=False -DENABLE_SANITIZERS=False -DENABLE_INCLUDE_WHAT_YOU_USE=False
cmake --build build --parallel 4 --target esmini esmini-dyn esminiLib esminiRMLib odrplot dat2csv
```

Notes on that invocation:

- It must succeed **offline**. The image has no package mirror and no external
  archive is fetched. Every optional dependency esmini can download is
  switched off by the flags above, and the checker additionally pre-creates
  empty `externals/OSI/linux`, `externals/OpenSceneGraph/linux`,
  `externals/SUMO/linux`, `externals/googletest/linux`, `externals/osi/linux`,
  `externals/osg/linux`, `externals/sumo/linux`, `externals/proj/linux`,
  `externals/implot/linux` and `resources/models` so that any download guard
  written as `if (NOT EXISTS <dir>)` is satisfied. Do not rely on the network
  in `CMakeLists.txt`.
- `--target` names the six artifacts below. Building `all` is not required and
  is not attempted; a target that exists but is not in this list is never
  built, so it cannot cause the step to fail.
- Unknown `-D` options are tolerated (`--no-warn-unused-cli`). You do not have
  to define a cache variable just because it appears above.
- The build has 2400 seconds for the configure step and 2400 seconds again for
  the compile, so the whole build may take up to 4800 seconds before the
  checker gives up. A build that does not finish in those windows means the
  step is not satisfied.
- **Breaking behaviour you inherited means the step is not satisfied.** This
  step's gate is proof of life plus one end-to-end path through each capability
  the earlier milestones already established; it is pass/fail as a set, so a
  regression in work you were handed means the step is not satisfied no matter
  how complete this step's new work is. The new work is what this step asks you
  to build.
- The C++ standard, the warning set and the optimisation level are yours to
  choose.
- `bin/`, `build/` and `cmake_build/` are **deleted before configure**. Ship
  sources; a binary you committed is not evidence that your tree builds, and it
  will not be there when the checker looks for artifacts.

## 2. The artifacts

After the build, these six must exist somewhere under `/app` (upstream puts
them in `bin/`; the build tree is also searched, `externals/` is not):

| name | kind | what it is |
|---|---|---|
| `esmini` | executable | the scenario player |
| `esmini-dyn` | executable | the player driven through the shared library |
| `odrplot` | executable | OpenDRIVE road-geometry sampler, CSV out |
| `dat2csv` | executable | `.dat` recording to CSV converter |
| `libesminiLib.so` | shared library | the scenario-engine C ABI |
| `libesminiRMLib.so` | shared library | the road-manager C ABI |

The **names** are fixed. The **locations** are not.

On this platform — 64-bit Linux, the toolchain the base image pins — the four
executables must be ELF executables and the two shared libraries must be ELF
shared objects. Both `file -b <artifact>` spellings are what the checker reads.

## 3. The headers and the exported symbols

The checker compiles small programs of its own against your headers and links
them against the two shared libraries:

```
g++ -std=c++17 <driver>.cpp -I<dir containing esminiLib.hpp> \
    -I<dir containing esminiRMLib.hpp> -L<libdir> -lesminiLib -lesminiRMLib
```

`<libdir>` above is a stand-in, not a single path. The checker discovers your
two shared libraries wherever you put them, and if they land in two different
directories it searches **both**: the value it works from is a
**colon-separated list**, in the `PATH` sense, and it is exported to the checks
as `LIBDIR`. Nothing requires you to keep the two libraries together, and
nothing breaks if you do.

The header **file names** `esminiLib.hpp` and `esminiRMLib.hpp` are fixed; the
directories holding them are discovered, and `externals/` is excluded from that
search. Each must be includable on its own and both must be includable
together, from a C++17 translation unit, with no other header included first.

The drivers are C++, not C, and that is deliberate: it is the weaker
requirement. Upstream's headers are not C translation units at this point in
the project's history, so a C driver would be asking you for something the
release you are reproducing does not do.

What is still required is **C linkage**. Every entry point the checker calls
must be exported from the shared library under its own unmangled name — that
is what `extern "C"` around the declarations buys you, and it is checked
directly, by reading the dynamic symbol table:

```
nm -D --defined-only <libdir>/libesminiLib.so
```

A name that appears there as `SE_Init` satisfies the check; one that appears as
`_Z7SE_InitPKciiii` does not, however well the C++ driver compiles against it.

Exported entry points are identified by name. A function the checker calls
must be present with the documented signature — see the step instruction for
which ones this step is about.

The same deferral applies to *values*, and it is worth stating because it is
easy to miss. Where your step's `instruction.md` or `WORKSPACE_HANDOFF.md`
names a literal — a specific error or sentinel return code, the wording of a
diagnostic message, a file-format version number, an output column header, a
log-line layout — that literal is part of the contract and is compared
literally. A value that is merely "an error code" or "a message saying the
same thing" is not a substitute. Those documents are not background reading
alongside this one; for everything step-specific they *are* the contract,
and this file is what does not vary between steps.

## 4. How a check runs

- Apart from the deletions and directory pre-creation described in §1, the
  checker writes to `/app` in exactly one situation: if the third-party
  source upstream carries as a git submodule under `externals/fmt` is needed
  to configure and you have not vendored it yourself, the checker drops in its
  own copy at upstream's pinned commit. That is the only third-party source it
  supplies: the other submodule upstream lists is **not** supplied, so anything
  else the build needs has to be vendored by you from the read-only copies
  `PROFILE.md` names, and a copy you vendor travels forward while a
  checker-supplied one does not. Vendoring is not a capability under test, so
  the build is not failed over the source the checker supplies, and a copy you
  placed yourself is never overwritten. Everything else the checker produces
  goes to its own scratch directory; your sources are not modified.
- Each product invocation gets 40 seconds; each case script gets 90 seconds,
  however many invocations it makes. A scenario that never terminates is a
  failure, not a timeout to be forgiven: `esmini` has no duration option, so
  anything the checker runs headless either has a stop trigger or is stopped
  by the checker.
- Checks read exit status, stdout, stderr, and files you wrote. They do not
  read your source code. The one class of tree file a check may read is a
  **published documentation artifact that a requirement explicitly names** —
  `docs/commands.txt` is the only such file in this package, and the step whose
  requirements cover it states the path in its own instructions. Your `.cpp`,
  `.hpp` and build files are never read, and no check infers anything from how
  you implemented something.
- Checks run against a build of *your* tree only. There is no reference binary
  in the container.

## 5. What is switched off, and what that means for you

`USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT` and `USE_PROJ` are
all `False`. There is no viewer, no OpenSceneGraph, no Open Simulation
Interface, no SUMO coupling, no unit-test binaries and no PROJ. Code behind
those switches is never compiled and never checked — but it must not break the
build when the switch is off. In particular, an `#include` of a generated OSI
header must be inside the matching `#ifdef`.

## 6. How the checker reads your build

Sections 3 and 4 describe the programs the checker compiles and links against
your headers and shared libraries (§3) and what a check is allowed to look at
when it runs (§4). This section states how those programs talk back, because a
value the checker asks for has to reach it through this channel.

- The checker's own programs are **drivers**. Each is a small C++ program built
  the way §3 describes, linked against *your* `libesminiLib.so` and
  `libesminiRMLib.so`, and run as a normal process.
- A driver reports what it observed by **printing line-oriented `key=value`
  records on stdout**, one record to a line, for example `value_a=3.0700`
  or `index_0=7`. The check reads those lines and compares them. A value that
  is only computed and never printed is a value the check cannot see, so every
  documented return value and every piece of state a check asks about has to be
  written out this way by the program that asked for it.
- The keys, the numeric tolerances and the fixture files are the checker's own
  and are not published here; what is published is that this is the format the
  checker reads, and that a *documented* entry point's result has to be
  obtainable by calling it from such a program.
- **A driver that exits non-zero fails its group.** Reaching the end of the
  program with status 0 is part of passing, so a call that crashes the process —
  a null dereference, an abort, an uncaught exception — fails the check even if
  the records printed before it were correct.
- A driver that **cannot be linked or launched at all** is reported **by its
  exit status**, not by a message. If a documented entry point is not exported,
  or is exported with a different signature, the driver does not link and the
  command that would have run it is simply not there: the check observes the
  shell's status for an unlaunchable command, which is `127`, and fails the
  group on that. There is no diagnostic text and none is looked for. The same
  applies to a shared library that will not load at run time.
