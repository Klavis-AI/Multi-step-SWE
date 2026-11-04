# Workspace handoff — entering milestone 11 of 11

## What `/app` is when this milestone starts

`/app` contains the two IETF drafts under `srt-spec/`, the project's
documentation set under `srt-docs/`, the three contract documents at the top
level, and the complete library the previous milestone left: `cmake -S /app -B
build && make -C build` produces `libsrt.so` and installs `<srt/srt.h>`, a live
stream crosses an intact or a lossy channel, encryption including AES-GCM and
the cipher-mode option works, packet filtering and TSBPD with too-late drop
work, a socket group carries a stream over its member links in broadcast and
in main/backup mode, and a live message that does not fit one packet is
refused rather than split.

This milestone is configured with `-DENABLE_BONDING=ON
-DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp -DENABLE_MAXREXMITBW=ON`.
The last of those four is new here: it admits the retransmission-bandwidth
ceiling option to the build for the first time. The other three were already in
force when the previous milestone was built, so nothing they govern changes
underneath you.

## What this milestone must not break

What this milestone adds to the inheritance chain: a retransmission bandwidth
ceiling at position 63 of `SRT_SOCKOPT`, an `int64_t` in bytes per second
reading -1 on a fresh socket, settable before and after connect, refusing any
value below -1 and any non-zero width other than eight bytes; a terminal `SRT_KM_S_E_SIZE`
of 6 on the key-material state enumeration with `SRT_KM_S_BADCRYPTOMODE` now
declared unconditionally at 5; and a named `SYSSOCKET_INVALID` of -1. The
ceiling option stays behind `ENABLE_MAXREXMITBW` and the cipher-mode option
and crypto rejection reason stay behind `ENABLE_AEAD_API_PREVIEW`; a later
milestone that exposes either unconditionally, or that renumbers anything at
or below 63, breaks this milestone's groups.

The hardening is inherited as positive assertions and must not regress: every
option setter refuses a negative `optlen` with `SRT_EINVPARAM` and still
accepts zero; `SRTO_VERSION` has no setter; `srt_epoll_uwait` returns the
number of entries written and never more than `fdsSize`, zero included;
`srt_accept` checks the address length before it dequeues, so a short buffer
leaves the connection for a retry; `srt_listen` tests rendezvous before bind
state and reports `SRT_ECONNSOCK` on a connected or accepted socket; a null
hook is accepted and clears the hook, and any hook on a listening, connecting,
connected or broken socket is refused with `SRT_ECONNSOCK`; a second
`srt_startup` returns 0; and a connected file-mode socket reports the
per-packet maximum through `SRTO_PAYLOADSIZE`. Loss recovery with too-late
drop off and the level-triggered poll over SRT and system sockets are inherited
unchanged. The layout of `/app`, the name and install location of the library
and its public header, and the behaviour the groups above assert are what
every later milestone inherits.

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
