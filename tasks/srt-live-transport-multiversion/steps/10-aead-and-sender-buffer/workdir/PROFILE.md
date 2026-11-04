# Task profile — what is being built, and what is measured

This file is the same at every milestone except for section 3. It states the
things that do not change: what the product is, what the verifier is allowed to
have an opinion about, and which values the specifications leave open.

Section 3 is the exception, and it is the one to re-read when you move to a new
milestone. The build line there is the one *this* milestone's verifier uses,
and it changes whenever a release changes a configure flag. Building with a
neighbouring milestone's line can renumber the enumerations that section 5 of
`TEST_INTERFACE.md` pins, so a tree that is correct about behaviour can still
fail on values.

Each milestone's own scope is in that step's `instruction.md`. The scored-group
inventory for the milestone is in `TEST_INTERFACE.md`. What the previous
milestone left behind is in `WORKSPACE_HANDOFF.md`.

---

## 1. Product identity

You are implementing **SRT — Secure Reliable Transport** as a C/C++ library
built by CMake from `/app`.

The product is the library `srt` (`libsrt`), exposing the public C API declared
in the installed header `<srt/srt.h>`. The names are not decorative and not
negotiable: every scored group is a C++ program that `#include <srt/srt.h>` and
links `-lsrt`, so a library called anything else, or a header installed
anywhere else, cannot be graded at all. The names, the calling conventions and
the constant values are part of the specification you are implementing, exactly
as they are for any other protocol library.

`srt-spec/draft-sharabayko-srt-01.txt` is the normative wire specification.
`srt-spec/draft-gg-udt-03.txt` is the UDT specification the core derives from:
SRT's sequence numbering, ACK/NAK exchange, congestion-control hooks and
handshake structure are UDT's, and the draft is the authority wherever the SRT
draft says "as in UDT".

`srt-docs/` is the upstream documentation, copied byte for byte from a tagged
release and listed with its provenance in `srt-docs/PROVENANCE.md`. It is the
authority on the **API**: what each function does, what each socket option
means, what each statistic counts, and which error each failure reports —
except where `TEST_INTERFACE.md` §5 names a passage of it as wrong. §5 is
generated from the behaviour the verifier checks, so where a line there
contradicts `srt-docs/`, §5 is what the verifier enforces.

---

## 2. Out of scope for the whole task

These are not implemented and not scored, at any milestone:

- **The sample applications.** `srt-live-transmit`, `srt-file-transmit`,
  `srt-tunnel`, `srt-test-*` and the `apps/` documentation that describes them.
  Every build in this task passes `-DENABLE_APPS=OFF`, so no scored group can
  reach an application binary even if you build one.
- **Platform support other than Linux/x86-64.** No Windows, macOS, iOS,
  Android or BSD path is built or graded. Conditional code for them is neither
  required nor forbidden.
- **Build-system conveniences**: package-manager manifests, pkg-config files
  beyond what CMake's install already produces, ABI-compliance tooling,
  bindings for other languages.
- **Logging output.** The logging API may exist; no scored group reads a log
  line, matches log text, or depends on a log level. Log whatever you like.
- **Anything requiring a real network.** Many groups move no traffic at all;
  where a group does, it runs both endpoints in one process over the loopback
  interface, sometimes through a UDP relay the verifier owns. Nothing dials
  out.

---

## 3. The build contract

The verifier builds your project with exactly this, in a scratch directory
outside `/app`:

```
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp
make -C <build> -j4
make -C <build> install DESTDIR=<dest>
```

Build it that way yourself. `-DENABLE_BONDING=ON`,
`-DENABLE_AEAD_API_PREVIEW=ON`, `-DUSE_ENCLIB=openssl-evp` are part of the
line, not an optional extra: they decide which declarations the configure
step enables, and therefore which numbers the enumerations in `<srt/srt.h>`
take. Section 5 of `TEST_INTERFACE.md` pins numeric values, and the values
it pins are the ones this line produces. A tree that only ever configures
without them can disagree with every one of those numbers while looking
correct to itself.

If that line fails, the verifier retries once with `-DENABLE_BONDING=ON`,
the previous milestone's recipe, and records which of the two built. The
retry exists so that a tree which has not yet learned this milestone's flags
is still measured on its behaviour rather than reported as a build failure.
It is a safety net, not a second supported configuration: the numbers you
are scored against are the first line's.

and then requires:

| Path | Must be |
|---|---|
| `<dest>/usr/local/include/srt/srt.h` | the public C API header |
| `<dest>/usr/local/lib/libsrt.so*` | the shared library; a static `libsrt.a` alone is not enough |

Everything else about the project's internal structure is yours. You may add,
split, rename or delete source files, change the internal class layout, and
choose your own internal headers; none of that is looked at. What is looked at
is the installed prefix above and the behaviour reachable through it.

`-DENABLE_APPS=OFF` must be accepted by your `CMakeLists.txt` even if you build
no applications — a fatal error on an unknown option fails the build and
therefore the whole milestone.

The toolchain is GCC 9 on Ubuntu 20.04 with OpenSSL 1.1.1 (`libssl-dev`), C++14
by default. `tclsh`, `pkg-config` and `python3` are present.

---

## 4. What the verifier is allowed to have an opinion about

Only what is reachable through the installed public interface:

- the functions, types, enumerators and macros declared in `<srt/srt.h>` and in
  any other header your build installs under `include/srt/`;
- the numeric values of public constants where a document or the wire format
  fixes them;
- observable behaviour: return values, `srt_getlasterror()` codes, socket
  states, `srt_bstats()` statistics, epoll readiness, rejection reasons, and
  the bytes that arrive at the far endpoint.

The verifier never includes an internal header, never links an internal symbol,
never reads your source, and never inspects your build tree. A scored group
that could only be satisfied by a particular internal design would be a defect
in this task; if you find one, the specification it cites is the authority.

---

## 5. Values the specifications leave open

The drafts and the API documentation deliberately leave some numbers to the
implementation. Where a scored group would otherwise depend on such a number,
this task pins it, and the pin is stated in agent-visible material: usually
the documentation in `srt-docs/` for that milestone, and otherwise
`TEST_INTERFACE.md` §5, whose tables say in as many words where they are the
only place a value is written. Two rules follow:

1. **A default that `srt-docs/` states for this milestone is binding.** If
   `API-socket-options.md` (or `APISocketOptions.md`, or `API.md`, depending on
   the tag) says an option defaults to a value, reading that option on a fresh
   socket returns that value. Every default in this package is read through
   `srt_getsockflag()`; no group calls `srt_getsockopt()`.
2. **A number no document states is not scored as a number.** Where behaviour
   depends on it, groups assert the behaviour's shape — monotonicity, ordering,
   a bound, a relation between two measurements — never an exact figure.

Concretely, these are **not** asserted as exact values anywhere:

- the ACK period and the light-ACK packet threshold;
- the initial RTT and RTT-variance estimates before the first ACK;
- the exact contents of the pseudo-random material in a handshake cookie, a
  key-material message or an initial sequence number — only their required
  properties (unpredictability is not tested; well-formedness and correct
  echoing are);
- buffer sizing arithmetic beyond the documented option semantics;
- the precise moment a timer fires, beyond a documented tolerance.

---

## 6. Determinism

Every scored group must produce the same verdict on every run of the same
build. That is the verifier's obligation, not yours, and it shapes what is
asserted: loss, duplication, reordering and one-way delay are produced by
dropping and holding **specific datagram indexes** in a relay the verifier
owns, never by probability and never by a real network.

The consequence for you is narrow but real: behaviour that is only correct on
average is not enough. If the specification says a lost packet is retransmitted
and delivered in order, then with that packet dropped exactly once, it is
retransmitted and delivered in order, every time.
