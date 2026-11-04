# Workspace handoff — entering step 1

## What you are given

`/app` is a complete, building checkout of the scenario engine: `CMakeLists.txt`
at the root, the C++ sources under `EnvironmentSimulator/`, the scenario and
road-network corpus under `resources/`, and the project's `LICENSE`. It
configures and builds offline with the flags in `TEST_INTERFACE.md`, and the
six published artifacts come out of that build today. Nothing is broken and
nothing is stubbed: this step adds capability to a working product.

Three files in the workspace are task-owned rather than upstream:
`TEST_INTERFACE.md`, `PROFILE.md` and this file. They travel with the
workspace across every step. Leave them in place.

## What the next step receives

Everything under `/app` except `bin/`, `build/`, `cmake_build/`, `.git/` and
the `externals/` subdirectories listed in the task's artifact exclusions. Your
sources, your CMake changes and anything you vendored under `/app` carry
forward; your build output does not, and the next step's checker rebuilds from
source in a fresh container.

Two consequences worth acting on:

- A third-party source you copy into `/app` yourself — for instance a
  dependency from `/opt/esmini-vendor` into `externals/` — is part of the
  workspace from then on. The checker itself supplies none: whatever the build
  needs has to be vendored by you.
- A file you leave behind in `/app` is visible to every later step. Scratch
  work, notes and experiment scripts are not graded, but they are not cleaned
  up for you either.

## What is not in the workspace

The checks, their fixtures and their expected values. The checker mounts its
own corpus at grading time and never reads your source. There is no reference
binary in either container, so nothing in `/app` can be compared against a
"correct" tree — the only thing that is graded is what your build does when it
is run.
