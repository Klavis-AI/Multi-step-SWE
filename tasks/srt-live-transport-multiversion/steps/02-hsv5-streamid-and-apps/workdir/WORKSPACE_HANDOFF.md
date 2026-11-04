# Workspace handoff — entering milestone 2 of 11

## What `/app` is when this milestone starts

`/app` still contains the two drafts under `srt-spec/`, the documentation set
under `srt-docs/`, and the contract documents at the top level, exactly as
milestone 01 left them. The documents themselves are unchanged — the same
files, the same sha256 in `PROVENANCE.md` — but they are no longer a superset:
they are pinned to the release this milestone targets, so the option table now
describes the surface you are being asked to build rather than one ahead of it.
Read §5 before you trust a row of it.

The library `libsrt.so` and the header `<srt/srt.h>` you built at milestone 01
are installed and working, and the workspace has been validated with milestone
01's verifier: the socket lifecycle, the documented option surface, the error
and epoll surfaces, live transfer over a lossy path, timestamp-based delivery,
statistics and payload encryption all passed their groups.

You are changing that library, not replacing it. The build system, the install
layout, the library and header names and every API element milestone 01 brought
are still there, and the two regression groups below re-run behaviours it
established. Nothing in `/app` is regenerated between milestones: what you do
not touch survives.

## What this milestone must not break

`/app` still holds the drafts, the documentation set and the contract
documents. The library and its build system came from milestone 01 and must
not be renamed or moved; the header stays where it is installed and keeps its
name.

What you build here becomes the baseline for milestone 03. Everything this
milestone adds is inherited by every milestone after it: one handshake rather
than two, with the extension block settled by the time `srt_connect` returns
and `srt_accept` has produced a socket; data in both directions on one
connection, sharing one key and one latency negotiation; the version surface
and the split latency options, reporting the negotiated figure rather than the
configured one; the stream id and its length contract; the transmission types
and the declared payload size, with the message-size refusals that go with
them; the eight entry points; and the two retired key-length options staying
retired.

A later milestone that drops one of those entry points, un-retires one of
those options, restores one-way transmission or changes a default this
milestone establishes fails its own inherited groups, not yours. The
outcome you leave for a passphrase that does not match — a connection that
completes, a keying state that says so, and payload that never decrypts --
survives into the next milestone as a behaviour it can be asked to restore, so
keep that state readable and keep the outcome exactly as it is here.

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
