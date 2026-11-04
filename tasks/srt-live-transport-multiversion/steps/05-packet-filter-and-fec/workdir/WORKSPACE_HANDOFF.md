# Workspace handoff — entering milestone 5 of 11

## What `/app` is when this milestone starts

`/app` contains the two IETF drafts under `srt-spec/`, the project's
documentation set under `srt-docs/`, the three contract documents at the top
level, and the complete UDT-derived core with selective-repeat ARQ, TSBPD,
AES-CTR encryption, the HSv5 handshake with streamid, and the BSD-shaped C API
that milestone 04 established. `cmake -S /app -B build && make -C build` produces `libsrt.so`
and installs `<srt/srt.h>`, and live messages cross an intact channel complete
and in order, with encryption working at both key lengths.

## What this milestone must not break

What this milestone adds to the inheritance chain: the packet filtering
framework, the built-in FEC filter, rejection-reason reporting for failed
handshakes, `srt_epoll_uwait`, `srt_epoll_set`, and the
`SRT_EPOLL_ENABLE_EMPTY` flag. The removal of `srt_perfmon` is also inherited
as a negative assertion — a later milestone that restores it fails its own
removal group. What this milestone must not break, and is scored on not breaking:

* live messages cross an intact channel complete and in order at the default
  `SRTO_PAYLOADSIZE` for live mode, and a message larger than the negotiated
  limit is refused rather than truncated or silently accepted;
* a matching passphrase at both key lengths still carries the payload intact,
  with `SRTO_RCVKMSTATE` on the receiver and `SRTO_SNDKMSTATE` on the sender
  reaching `SRT_KM_S_SECURED`; a one-sided or mismatched passphrase still
  delivers nothing to the application while the KM state reports the
  condition.

Those two are the gate. `TEST_INTERFACE.md` section 3 names the groups that
measure them; breaking either scores 0 for this milestone however much of the
new work is finished.

The layout of `/app`, the name and install location of the library and its
public header, and the behaviour those groups assert are what every later
milestone inherits.

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
