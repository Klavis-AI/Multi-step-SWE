# Workspace handoff — entering milestone 9 of 11

## What `/app` is when this milestone starts

`/app` contains the two IETF drafts under `srt-spec/`, the project's
documentation set under `srt-docs/`, the three contract documents at the top
level, and the complete library milestone 08 left: `cmake -S /app -B build &&
make -C build` produces `libsrt.so` and installs `<srt/srt.h>`, a live stream
crosses an intact or lossy channel, encryption and packet filtering work, and
with bonding configured on, a socket group carries a stream over its member
links in broadcast and in main/backup mode.

## What this milestone must not break

What this milestone adds to the inheritance chain: the settled group API, the
minimum link stability timeout at its own position in `SRT_SOCKOPT` with its
60 ms floor and its peer-idle-timeout ceiling, the three-value group type
enumeration, and the receiver buffer that gates readability on delivery time.
Three withdrawals are inherited as negative assertions — `srt_include`,
`srt_exclude` and `srt_group_configure` must stay absent, and a later milestone
that restores any of them fails this milestone's own group. The layout of
`/app`, the name and install location of the library and its public header, and
the behaviour asserted by this milestone's groups -- which are inventoried in
`TEST_INTERFACE.md`, not in this file -- are what every later milestone
inherits.

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
