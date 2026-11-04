# Step 6 — Per-message source time, a listener's access-control hook, and a reject reason the application chooses

## 1. Implement this milestone

Give the application control over two things it could previously only observe:
the timestamp each message carries, and the decision to admit or refuse an
incoming connection. The previous milestone added the packet-filter framework,
FEC, reject-reason reporting and the second-generation epoll entry points.
This one adds per-message source time, an access-control hook with a reject
code the listener chooses, and the accounting that makes both visible.

This milestone produces **version 1.4.2**, and the version is part of what you
install: `<srt/version.h>` and the `SRT_VERSION` in the top-level
`CMakeLists.txt` must both say 1.4.2. `/app/TEST_INTERFACE.md` §1 gives the
exact macros and why leaving them where the previous milestone left them
changes what is measured.

Per-message source time:

* the message-control block is initialised to a defined state by its helper:
  the packet sequence number and the message number are both set to the
  "none" sentinel rather than to zero, the time-to-live is infinite, the
  in-order flag is clear and the boundary flag is "subsequent";
* a send call that passes a zero source time means "stamp it now", and the
  time actually stamped is written back into the caller's block, together with
  the sequence number the payload went out under — the block is an
  input/output parameter on send, not input only;
* a send call that passes a source time earlier than the connection's own
  start time is refused with `SRT_EINVALMSGAPI` and transmits nothing, so an
  application that reuses a stale timestamp finds out immediately instead of
  producing a stream the receiver will schedule into the past;
* a source time exactly equal to the connection start time is accepted.

The listener's access-control hook:

* the hook a listener registers may refuse a connection, and when it refuses
  without naming a code the caller now reads the "unknown" reason rather than
  the generic resource-exhaustion reason the earlier behaviour reported;
* `srt_setrejectreason` lets the hook name the code itself, which reaches the
  calling peer through the handshake response and is readable there verbatim;
* codes below `SRT_REJC_PREDEFINED` (1000) belong to the protocol and are
  refused to the application with `SRT_EINVPARAM`; the range from 1000 up is
  the application's, with a predefined catalogue of HTTP-shaped codes such as
  `SRT_REJX_FORBIDDEN`, and the range from `SRT_REJC_USERDEFINED` (2000) up is
  entirely private to the application;
* a code the hook sets is delivered to the caller unchanged instead of being
  clamped to zero, and an invalid socket id makes the setter fail with
  `SRT_EINVSOCK`;
* a connection that times out rather than being refused reports the timeout as
  its own reject reason, so "nobody answered" and "someone said no" are now
  distinguishable without timing the call;
* the access-control codes live in a second public header, installed alongside
  the main one, which an application includes only if it uses them.

Send-side accounting:

* statistics gain counters for packets and bytes sent that were original
  rather than retransmitted, the matching receive-side unique counters, and a
  report of the reorder tolerance currently in force;
* the reorder tolerance starts at the configured loss-report delay rather than
  at zero, is derived by an accepted socket from the listener it came from,
  and decays towards zero as ordered traffic accumulates.

Option acceptance:

* the options that configure timestamp-based delivery, latency, too-late drop
  and periodic loss reports are refused once the socket has a local address,
  not merely once it is connected: after a bind they fail with
  `SRT_EBOUNDSOCK` and the stored value is left exactly as it was.

Connection groups:

* the grouped-connection entry points — creating a group, connecting it,
  reading its member data, and finding the group a socket belongs to — are
  built and exported in this milestone's configuration;
* the deprecated spelling of bind-by-peer is no longer a function of the
  shared library; it survives only as a header-level forwarder to its
  replacement.

## 2. What you inherit

Milestone 05 added the packet-filter framework and its built-in FEC filter,
reject-reason reporting for refused handshakes, and the second-generation epoll
entry points, on top of the UDT-derived core, selective-repeat ARQ, TSBPD with
too-late packet drop, and AES payload encryption that the milestones before it
established. All of that must keep working here, and three paths are asserted
directly by this milestone's groups:

* ARQ recovery — a live stream crossing a channel that loses specific packets
  still arrives complete and in order, with the loss visible in the receiver's
  statistics;
* TSBPD — the receiver holds each message until its scheduled play time, the
  configured delay is readable back from the socket while the connection is up,
  and the stream is delivered in order;
* FEC — a filter configured on both sides rebuilds packets the channel dropped
  without ARQ having to retransmit them, and the rebuild is visible in the
  filter counters.

Those three run against the same library build as everything new in this
milestone, and they are configured entirely through socket options — which is
exactly the layer this milestone tightens. Read §5 of the test interface before
you touch the option guards: the set of options those three groups configure is
the same set whose acceptance window is narrowed here, and narrowing it too far
breaks them while every new group still passes.

Configure the build with the same flags as before, plus the one named in the
build line of the test interface.

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
`<srt/srt.h>` and `<srt/access_control.h>`, and linked with `-lsrt` together with the system thread and
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
