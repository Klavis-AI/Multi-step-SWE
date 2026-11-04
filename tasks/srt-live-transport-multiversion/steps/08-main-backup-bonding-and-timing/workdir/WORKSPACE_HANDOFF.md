# Workspace handoff — entering milestone 8 of 11

## What `/app` is when this milestone starts

`/app` contains the previous milestone's tree: the full library source and its
build system, the two IETF drafts under `srt-spec/`, the documentation set
under `srt-docs/`, and the contract documents at the top level. A build already
produces `libsrt.so` and installs `<srt/srt.h>`, and the result already carries
live streams, recovers loss, encrypts, filters and groups. What it does not do
is detect a conflicting binding before it reaches the operating system, or
default the retransmission algorithm to the efficient one.

## What this milestone must not break

Everything earlier milestones installed stays: the layout of `/app`, the name
and install location of the library and its public header, and every option,
error code and call already exported. Add `SRT_EBINDCONFLICT` to the error
enumeration without renumbering anything else — later milestones assert the
numeric values of codes that already exist — and leave the group entities
visible under the same configure switch, since later milestones connect groups
through it.

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
