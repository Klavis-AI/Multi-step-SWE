# Workspace handoff — entering milestone 3 of 11

## What `/app` is when this milestone starts

`/app` still contains the drafts under `srt-spec/`, the documentation set under `srt-docs/`, and the contract documents at the top level — but not byte-for-byte as milestone 02 left them. The three contract documents and the two corpora are refreshed at the start of every milestone, and `srt-docs/` is re-pinned to the release this milestone targets: `API.md` and `live-streaming.md` have both grown since milestone 02, and `API-functions.md` is new here. `srt-docs/PROVENANCE.md` records which release each file comes from, and it is worth re-reading rather than assuming the pages say what they said last milestone. Everything else in `/app` — your sources, your build system, whatever you left behind — is exactly as milestone 02 left it. The library `libsrt.so` and header `<srt/srt.h>` you built at milestone 02 are installed and working, and the workspace has been validated with milestone 02's verifier: HSv5, bidirectional mode, streamid passthrough, and the live/file transmission-type selection all passed their groups.

You are extending that library, not replacing it. The build system, the installed header location, and every API element milestone 02 brought are still there and must keep working.

## What this milestone must not break

`/app` still holds the drafts, documentation and contract. The library and its build system are in place from milestone 02 and must not be renamed or moved. What you build here becomes the baseline for milestone 04: the layout of `/app`, the library's public API surface, and the behaviour this milestone's own groups assert are inherited by every later milestone in this chain. A later milestone that removes an option this milestone added or changes this milestone's documented default fails its own inherited groups, not yours.

What *this* milestone is scored on not breaking is the gate, and `TEST_INTERFACE.md` §3 names it group by group. In substance it is four things:

* the project in `/app` still configures, builds and installs with the documented command, and produces an installed header and shared library;
* encryption with a matching passphrase and `SRTO_PBKEYLEN=32` on both sides still carries the payload intact, and the payload still does not appear in clear in datagrams on the wire;
* NAK-driven retransmission still recovers datagrams dropped at fixed indexes and delivers the payload intact and in order when too-late drop is disabled;
* the five options milestone 02 brought — `SRTO_STREAMID`, `SRTO_MINVERSION`, `SRTO_TRANSTYPE`, `SRTO_MESSAGEAPI`, `SRTO_PAYLOADSIZE` — are still settable at the documented restriction level and still readable at it, which for the write-only `SRTO_TRANSTYPE` means a get of it is still refused with `SRT_EINVOP`.

The gate earns nothing and can only veto: if any one of those fails, this milestone scores `0` however much of the new work you finished.

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
