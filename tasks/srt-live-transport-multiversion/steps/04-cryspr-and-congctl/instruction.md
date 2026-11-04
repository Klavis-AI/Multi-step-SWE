# Step 4 — Listen callback, peer idle timeout, IPv6-only option, and congestion control with 1 Gbps BW_INFINITE

## 1. Implement this milestone

Extend the library with three new socket options and a connection accept hook.
The milestone also reworks two internals — the cipher interface the encryption
layer sits on, and the congestion controller — but neither rework is in scope
directly; only the two behavioural consequences named below are.

The milestone adds:

* `srt_listen_callback(lsn, hook_fn, opaque)` installs a hook that fires before
  the listener accepts an incoming connection. The hook receives the peer
  address, the handshake version, and the streamid the caller set, and it can
  inspect them to decide whether to accept or reject. A return value of exactly
  −1 rejects the connection: the caller's `srt_connect` fails with
  `SRT_ECONNREJ` and `srt_accept` yields nothing. Every other return value
  accepts it. Passing a NULL hook pointer returns `SRT_EINVPARAM`. The hook
  fires in the receiver worker thread before the handshake response is sent, so
  an option set inside the hook — a passphrase, for example — takes effect
  before key material is processed. The hook is not a veto on the handshake
  itself; it is an early exit from `acceptAndRespond`.

* `SRTO_PEERIDLETIMEO` (slot 55): the peer idle timeout in milliseconds. It
  defaults to 5000 on a fresh socket. A set is accepted before connect and
  refused with `SRT_ECONNSOCK` after. The accepted socket inherits the
  listener's value. This option names the silence budget: when no control or
  data packet is received from the peer for longer than this, the connection is
  considered lost and is broken. The default of 5000 ms is fixed by this
  specification, not chosen by the implementation.

* `SRTO_IPV6ONLY` (slot 54): the `IPV6_V6ONLY` mode. It defaults to −1, meaning
  the system default. A set accepts −1, 0, or 1, and goes on accepting
  them after the socket is bound; the one state that refuses a set is a
  connected socket, which fails with `SRT_ECONNSOCK`. When 0, a listener bound to an IPv6 address also
  accepts IPv4 clients whose addresses are formatted as IPv4-mapped IPv6. When
  1, only IPv6 clients are accepted. The option drives the system socket option
  of the same name on the underlying UDP socket.

* `SRTO_CONGESTION` (slot 47, the old `SRTO_SMOOTHER`): the congestion control
  algorithm selection. It reads `"live"` or `"file"` on a fresh socket, accepts
  a set to either, and the getter returns the string the setter was given.
  `SRTO_SMOOTHER` remains as a working deprecated alias: both names compile,
  both can be set and read, and they behave identically. The name change is
  visible in the header and in the option's documented description, but it is
  not a behavioural discriminator — the slot number and the semantics are
  unchanged.

* **The unlimited-bandwidth ceiling rises from 30 Mbps to 1 Gbps.** This is
  the rate the congestion controller assumes when `SRTO_MAXBW` is left at its
  −1 default or set to 0 — the figure that stands in for "no limit". It is
  fixed by this specification rather than configured: `srt_bstats()` must
  report it as `mbpsMaxBW`, and as the corresponding send period in
  `usPktSndPeriod`. The move is a factor of 33, from 30.0 Mbps and a send
  period around 362 µs to 1000.0 Mbps and around 10 µs. The previous
  milestone's 30 Mbps is no longer correct here.

* `srt_epoll_wait` accepts the writable-system-socket array as the **sole**
  non-NULL output set when the timeout is −1. The previous milestone's
  argument check rejected that combination with `SRT_EINVPARAM` before polling
  anything; here the call proceeds and returns the ready system sockets. A
  call with every output array NULL is still refused with `SRT_EINVPARAM` —
  the check is narrowed, not removed.

* **The installed `<srt/srt.h>` stops defining `NO_BUSY_WAITING`.** That name
  is a private build-configuration switch of the internal reworks, and the
  previous milestone leaked it into every translation unit that included the
  public header. Here it does not: a program whose only include is
  `<srt/srt.h>` must compile with `NO_BUSY_WAITING` undefined, while the
  header's own macros — `SRT_API`, the logging functional-area ids — are
  still defined and still usable in that same translation unit. It is the one
  part of the internal reworks a caller can observe, and it is observable at
  compile time only.

Those seven behaviours are the whole of the milestone's observable surface. Beyond
them the two internal reworks have no effect a caller can see: this milestone
does not require you to match any particular internal file layout, class name
or private detail — only the behaviour above, reachable through
`<srt/srt.h>`.

## 2. What you inherit

Milestone 03 established strict encryption mode, the complete KM-state reporting
through `SRTO_RCVKMSTATE` and `SRTO_SNDKMSTATE`, the HSv5 handshake with
streamid, and the complete set of documented socket options with their
pre/post-connect restrictions. This milestone must preserve all of that: the
encryption groups from milestone 01 still pass, the socket-option restriction
matrix holds, and a live stream crosses an intact or lossy channel exactly as
before.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 7 **gate**
groups that check the milestones you inherited are still intact, and 8
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
