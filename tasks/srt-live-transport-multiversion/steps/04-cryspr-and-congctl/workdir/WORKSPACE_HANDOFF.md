# Workspace handoff — entering milestone 4 of 11

## What `/app` is when this milestone starts

`/app` holds the library milestone 03 left: the HSv5 handshake with streamid, strict
encryption mode with complete KM-state reporting, the complete set of
documented socket options with their pre/post-connect restrictions, and live
transfer with loss recovery over the UDP relay. The library builds, installs
`<srt/srt.h>` and `libsrt.so`, and every group from milestone 03 passes.

## What this milestone must not break

The socket lifecycle from milestone 01, the encryption and KM-state reporting
from milestones 01 and 03, the HSv5 handshake with streamid from milestone 02,
the socket-option restriction matrix from milestone 02, and live transfer with
loss recovery must all continue to work.

Most of that is carried by this step's own gate groups, which fail the step
rather than merely scoring low: `endtoend`, `transfer`, `recovery`,
`encryption`, `congestion` and `optmatrix`. The restriction matrix is the one
this milestone is most likely to break, because this milestone's work IS the
option enum, and until this round no group in *this* milestone reached it:
`optmatrix` now asserts it at every inherited position it touches. Milestone 03
gates the same matrix, but a milestone is graded only by its own groups, so
that gate does nothing about a regression introduced here. A later milestone that
breaks one of these fails that milestone's own inherited groups; a milestone
that breaks one with no inherited group covering it is not charged anywhere, so
if you carry this list forward, carry the groups with it.

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
