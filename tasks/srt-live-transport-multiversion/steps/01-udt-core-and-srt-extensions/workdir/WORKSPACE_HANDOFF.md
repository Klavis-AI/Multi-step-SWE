# Workspace handoff — entering milestone 1 of 11

## What `/app` is when this milestone starts

`/app` contains documentation and no source: the two IETF drafts under
`srt-spec/`, the project's documentation set under `srt-docs/`, and the three
contract documents at the top level. There is no build system, no header and
no library yet — creating them is this milestone's work.

## What this milestone must not break

Nothing yet exists to break. What you build here every later milestone
inherits: the layout of `/app`, the name and install location of the library
and its public header, and the behaviour asserted by this milestone's groups
-- which are inventoried in `TEST_INTERFACE.md`, not in this file. A later
milestone that renames `libsrt.so`, moves `<srt/srt.h>` or drops an option
listed in this file fails its own inherited groups, not yours.

## Where things live

| Path | What it is |
|---|---|
| `/app` | the CMake project you build; everything outside the paths below is yours |
| `/app/PROFILE.md` | task-wide: product identity, build contract, scope, determinism |
| `/app/TEST_INTERFACE.md` | this milestone's scored-group inventory and scoring rule |
| `/app/WORKSPACE_HANDOFF.md` | this file |
| `/app/srt-spec/` | the normative IETF drafts for SRT and UDT |
| `/app/srt-docs/` | upstream API documentation, pinned and listed in `PROVENANCE.md` |
| `/app/timer.sh` | prints the time left in this milestone's budget |

The three contract documents and the two corpora are refreshed at the start of
every milestone. They are not yours to edit, and the next milestone expects
them to still be there — but nothing in this milestone scores their contents or
checks that they are present, so a stray edit costs you nothing except the
information in them.

Anything else you leave in `/app` is carried into the next milestone exactly as
you left it, except build products: the snapshot handed to the verifier and to
the next milestone excludes `build/`, `_build/` and `.git/`.
