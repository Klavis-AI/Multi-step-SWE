# Step 1 — UDT-derived core and the original SRT extensions

## 1. Implement this milestone

Build the transport library from nothing. `/app` holds the two IETF drafts, the
project's own documentation set and this milestone's contract; it holds no
source. When you are done, `cmake -S /app -B build && make -C build` must
produce `libsrt.so` and install `<srt/srt.h>`, and a program linked against it
must be able to carry a live stream over a lossy UDP path.

The milestone is the UDT-derived core plus the original SRT extensions:

* the common packet header and the data-packet layout, and the control set
  (`HANDSHAKE`, `KEEPALIVE`, `ACK`, `NAK`/`LOSSREPORT`, `SHUTDOWN`, `ACKACK`,
  `DROPREQ`, `PEERERROR`, and the SRT-specific user-defined types);
* one UDP channel multiplexed across sockets, with a socket-id demultiplexer;
* selective-repeat ARQ: a sender loss list, a receiver loss list, full and
  light ACKs, NAK-driven retransmission, and RTT estimated from the
  ACK–ACKACK exchange;
* TSBPD: a latency negotiated in the handshake, a delivery clock derived from
  the peer's start time, and too-late packet drop above it;
* rate control for live and for file transfer, including the input-bandwidth
  estimate and the overhead allowance;
* AES-CTR payload encryption with the key material exchanged in the handshake
  and regenerated as the sequence space is consumed;
* the HSv4 handshake, in which the SRT extensions travel as user-defined
  control packets after the UDT handshake completes;
* a level-triggered `epoll` over both SRT and system sockets;
* the BSD-shaped C API of `<srt/srt.h>`.

`srt-docs/` is the authority for every name, default and documented constant.
The drafts are the authority for the wire format. Where neither speaks, you
may choose — only the shape of those values is required, never the value
itself; `/app/PROFILE.md` §5 lists them.

## 2. What you inherit

Nothing. This is the first milestone, so `/app` starts with documentation only
and every line of the library is yours to write.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 1 **gate**
group that checks the milestones you inherited are still intact, and 11
**behaviour** groups covering this milestone's own work. All of them are in
scope.

The gate must hold: break something an earlier milestone established and this
step is not satisfied, however much of the new work you finished. Preserving
what you inherited and implementing nothing new does not satisfy this step
either. Every behaviour group is required; a group you do not reach is a gap in
the step.

One thing qualifies that, and it is worth knowing before you plan the work: the
drivers are built as one test binary per driver *family*, and every requirement in
a family depends on the family compiling — a family that does not compile
affects every group in it, not only the group whose capability is missing. That
is why section 5 of `TEST_INTERFACE.md` marks every public name
it lists either **required to compile** or optional. A name marked required is
one a driver references without a fallback guard, so a tree that does not
declare it affects the whole family; a name marked optional is guarded, and
missing it affects only the group that exercises it. Which names fall on which
side is measured from the drivers each time this document is generated, so a
section 5 that marks nothing as required means this milestone introduces no
unguarded name -- not that the distinction does not apply.

Everything is measured through the installed public interface: `/app` is
built with

```
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF  \
      -DCMAKE_INSTALL_PREFIX=/usr/local && make -C <build> -j4 \
  && make -C <build> install DESTDIR=<dest>
```

and then separate programs are compiled against `<dest>/usr/local`, including
only `<srt/srt.h>`, and linked with `-lsrt` together with the system thread and
dynamic-loader libraries -- nothing else of yours. If that build or that install does not
produce `include/srt/srt.h` and `lib/libsrt.so*`, no group can run.

`/app/PROFILE.md` §2 lists what is out of scope for the whole task, §5 lists
the values the specifications leave open and what this task does about them,
and §6 states the determinism requirement.

## 4. Documentation

- `/app/srt-spec/draft-sharabayko-srt-01.txt` — the normative SRT wire specification.
- `/app/srt-spec/draft-gg-udt-03.txt` — the UDT specification the core derives from.
- `/app/srt-docs/` — upstream API documentation pinned to this milestone; see
  `srt-docs/PROVENANCE.md` for which release each file comes from.

## 5. Time

Run `/app/timer.sh` to see how much of this milestone's budget is left.

## 6. Constraints

This environment has no network access, and everything the build needs is already
installed. The upstream project's source, its releases, mirrors and package
registries are unreachable, so a copy cannot be fetched by any route.
