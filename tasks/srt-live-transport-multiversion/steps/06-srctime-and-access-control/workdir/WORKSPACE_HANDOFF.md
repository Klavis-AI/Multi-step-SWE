# Workspace handoff — entering milestone 6 of 11

## What `/app` is when this milestone starts

`/app` contains the two IETF drafts under `srt-spec/`, the project's
documentation set under `srt-docs/`, the three contract documents at the top
level, and a working transport: the UDT-derived core with selective-repeat ARQ,
TSBPD with too-late packet drop, AES payload encryption with key-material state
reported on both sides, the packet-filter framework with its built-in FEC
filter, reject-reason reporting for refused handshakes, the second-generation
epoll entry points, and the BSD-shaped C API that milestones 01 through 05
established. `cmake -S /app -B build && make -C build` produces `libsrt.so` and
installs `<srt/srt.h>`, and a live stream crosses both a clean and a lossy
channel complete and in order.

Two things about this milestone's build change: it is configured with an extra
flag, named in the build line of the test interface, which turns on a group of
entry points that are otherwise absent; and the milestone installs a second
public header next to the first. Neither is optional — a build that omits the
flag, or installs only one header, fails groups that have nothing else wrong
with them.

## What this milestone must not break

What this milestone adds to the inheritance chain: the listener access-control
callback with the reject code the application chooses and the caller-visible
reason it produces, per-message source timestamps written back into the
caller's control block on send, the send-side accounting counters and the
reorder-tolerance report, the message-control defaults, and the group entry
points and their public header. The deprecated bind-by-peer spelling becoming a
header-only forwarder is inherited as a negative assertion — a later milestone
that exports it again from the shared library fails its own group.

One thing here is deliberately not permanent: the acceptance window for the
TSBPD, latency, too-late-drop and loss-report options is at its narrowest in
this milestone, and the next one widens it again. Later milestones assert their
own boundary; do not carry this one forward as if it were settled.

The layout of `/app`, the name and install location of the library and both
public headers, and the behaviour asserted by this milestone's groups -- which
are inventoried in `TEST_INTERFACE.md`, not in this file -- are what every
later milestone inherits.

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
