# Workspace handoff — entering milestone 7 of 11

## What `/app` is when this milestone starts

`/app` holds the library as the previous milestone left it: the sources, the
build system, the public header that installs as `<srt/srt.h>`, the two IETF
drafts under `srt-spec/`, the project's documentation set under `srt-docs/`, and
this milestone's contract documents at the top level. `cmake -S /app -B build`
with the flags named in `PROFILE.md` §3, followed by `make -C build`, already
produces `libsrt.so`, and the groups of every earlier milestone already pass
against it. Nothing here is empty and nothing has to be created from scratch:
the work is a replacement of one layer, in place.

## What this milestone must not break

Everything the section above lists, plus what this milestone adds: the option
table's declared widths, defaults, ranges and restriction phases, the
string-config validation in the set call, and the exported `srt_clock_type`. The next milestone extends the
same table, so it inherits all of it — a change that renumbers `SRT_SOCKOPT`,
drops an option, relaxes a range check, moves an option between the pre-bind and
pre-connect classes, or defers a string option's validation back to connect time
fails that milestone's inherited groups rather than this one's.

The install layout is unchanged and must stay so: `libsrt.so` and `<srt/srt.h>`
under the staging prefix, with the option enum numbered identically whether or
not a consumer defines the bonding macro.

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
