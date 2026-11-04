# Workspace handoff — entering milestone 10 of 11

## What `/app` is when this milestone starts

`/app` contains the two IETF drafts under `srt-spec/`, the project's
documentation set under `srt-docs/`, the three contract documents at the top
level, and the complete library the previous milestone left: `cmake -S /app -B
build && make -C build` produces `libsrt.so` and installs `<srt/srt.h>`, a live
stream crosses an intact or a lossy channel, encryption, packet filtering and
TSBPD with too-late drop work, a socket group carries a stream over its member
links in broadcast and in main/backup mode, and the receiver buffer gates
readability on delivery time. This milestone is configured with
`-DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp`,
so the EVP encryption backend and the preview cipher-mode API are both in the
build for the first time.

## What this milestone must not break

What this milestone adds to the inheritance chain: the cipher-mode option at
position 62 of `SRT_SOCKOPT` with AES-GCM alongside AES-CTR and the handshake
negotiation between them, the crypto rejection reason at 17, the
bad-crypto-mode key-material state at 5, and the rule that one live message
occupies one packet -- a message needing more than one at the segment size in
force is refused rather than split. All three new identifiers stay behind
`ENABLE_AEAD_API_PREVIEW`. Renumbering anything at or below 61 breaks this
milestone's groups outright; exposing the three unconditionally breaks no group
here -- every driver is compiled with the define, so none of them can see it --
but it is still a change to the public header this milestone hands on, and
`TEST_INTERFACE.md` §5 says so in as many words.

Two corrections are inherited as positive assertions and must not regress: the
interval `byteRcvLoss` reports lost bytes and reads zero on a clean link, and
packets that fail to decrypt are dropped rather than handed up as ciphertext,
never reported readable, and counted with their headers in the interval
`byteRcvDrop`. The stricter argument checks are inherited too -- a zero address
length on connect and an IPv6 wildcard bind with the IPv6-only flag unset both
stay refused. The layout of `/app`, the name and install location of the
library and its public header, and the behaviour asserted by the groups
`TEST_INTERFACE.md` inventories in its §3 and §4 are what every later milestone
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
