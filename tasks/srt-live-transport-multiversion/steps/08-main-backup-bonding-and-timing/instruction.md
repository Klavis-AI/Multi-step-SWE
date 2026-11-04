# Step 8 — Binding-conflict detection across the UDP multiplexer, and the efficient retransmission algorithm by default

## 1. Implement this milestone

This milestone makes the binding layer answer for itself, and makes the efficient
retransmission algorithm the one a socket uses unless it is told otherwise.
`/app` already builds a library that carries live streams; nothing here replaces
that. Configure exactly as §3 shows, including the extra flag that switches
socket groups on — the group declarations and the group implementation are both
part of what this milestone must build.

**Binding conflicts.** Several SRT sockets in one process may share one UDP
endpoint through the multiplexer described in `srt-docs/`. Until now the search
for a reusable multiplexer compared only the port, the address family and the
channel settings, so a bind request was silently satisfied by a multiplexer
bound to a *different* address. That ends here. A bind request and an existing
binding **intersect** when they share a port and their address ranges overlap:
an explicit address covers itself, the IPv4 wildcard covers every IPv4 address,
the IPv6 wildcard covers what `SRTO_IPV6ONLY` says it covers. Intersecting
bindings may share a multiplexer only when all three of the conditions listed
under the new error in `srt-docs/` hold — `SRTO_REUSEADDR` true on both, an
identical address specification, identical UDP channel settings. Otherwise
`srt_bind` returns −1 and the error is `SRT_EBINDCONFLICT`, decided by the
library before it asks the operating system to bind anything: the refusal is
that error and not a system-level bind failure, including where the operating
system would have refused too. Two bindings that do **not** intersect — two
different explicit addresses on one port — both succeed, each socket gets its
own UDP endpoint, and `srt_getsockname` on each reports the address that socket
itself asked for. A refused bind changes nothing for the socket that bound
first, nor for a connection that endpoint is already carrying.
`SRT_EBINDCONFLICT` joins the error enumeration as a new minor code — the
enumerator `MN_BUSYPORT`, alongside the other minor codes of the "operation not
supported" major — and needs a message of its own.

**Retransmission algorithm.** `SRTO_RETRANSMITALGO` now defaults to the value its
row in `srt-docs/` gives rather than to the intensive algorithm, a value outside
its documented range is refused instead of stored, and `SRTO_TRANSTYPE` sets it
as part of each transmission profile — in both directions, so selecting a
profile overwrites whatever the option was set to before.

**Timing.** Which clock the library's own timing runs on is decided when the
project is configured, and that decision stops being frozen into the build
system. The monotonic-clock option's default was a literal `OFF`; it is now the
result of probing the platform for `clock_gettime` with `CLOCK_MONOTONIC`, so a
platform that provides the call builds on the monotonic clock with nothing asked
for on the configure line, and a platform that does not keeps the system clock
and still builds. Passing the option explicitly still overrides the probe in
either direction. `srt_clock_type` goes on reporting the selection through the
identifier set `srt-docs/` lists, and it must go on being exported from the
shared library; what changes here is which identifier it reports.

**Socket groups.** Types, member states and the connect and query calls are
unchanged, but a group must still connect and still carry a stream across the
reworked binding layer, since every member link takes a local endpoint through
it.

## 2. What you inherit

Everything. `/app` arrives with a complete live transport — handshake,
encryption, packet filtering, statistics, socket groups — and with the
declarative option layer that decides, per option, whether a set is legal
before bind, before connect or never, validates the argument and reports
`SRT_EINVPARAM` when it is not, and returns configured values at their declared
widths. None of that changes here. Two things sit close enough to this
milestone's work to be worth re-checking: sockets that share one UDP endpoint
now reach it through a reworked matching path, and loss recovery now runs on a
different retransmission default. A connected socket group exercises both,
since its member links bind through the same path.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 4 **gate**
groups that check the milestones you inherited are still intact, and 9
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
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_EXPERIMENTAL_BONDING=ON \
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
