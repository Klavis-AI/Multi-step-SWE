# Step 5 — Packet filtering framework and FEC implementation

## 1. Implement this milestone

Add the packet filtering framework and its first built-in filter, FEC. The
previous milestone established the UDT-derived core, selective-repeat ARQ,
TSBPD, AES-CTR encryption and the HSv5 handshake with streamid. This one adds
a mechanism for injecting processing at send and receive boundaries, with one
implementation.

This milestone produces **version 1.4.0**, and the version is part of what you
install: `<srt/version.h>` and the `SRT_VERSION` in the top-level
`CMakeLists.txt` must both say 1.4.0. `/app/TEST_INTERFACE.md` §1 gives the
exact macros and why leaving them where the previous milestone left them
changes what is measured.

The packet filter framework:

* `SRTO_PACKETFILTER`: a pre-connect socket option, **numbered 60**, that
  accepts a configuration string naming the filter and its parameters; the
  getter retrieves the stored string. The number is fixed rather than left to
  you: the previous milestone's enumeration ends at `SRTO_PEERIDLETIMEO` (55)
  and slots 56-59 are deliberately left free for options that arrive later in
  the chain, so the first option this milestone adds is 60;
* a filter may inject extra packets into the channel (such as FEC control
  packets) ahead of the next waiting data packet, and may rebuild lost packets
  on the receiver;
* when a filter that requires header space is configured, the framework
  compares the payload size **already stored** against the maximum the filter
  leaves available and silently lowers it to that maximum if it is over — it
  does not reject the call, and it does not touch a stored value that is
  already within budget;
* `srt_bstats` gains four counter pairs (total and interval) for
  filter-injected control packets sent, filter control packets received,
  packets the filter rebuilt, and losses the filter could not cover.

The built-in `"fec"` filter:

* implements forward error correction following the column-and-row arrangement
  of SMPTE 2022-1, with user-configured matrix dimensions;
* configuration syntax `"fec,cols:N,rows:M,layout:even|staircase"` where `cols`
  is mandatory, `rows` defaults to 1, and `layout` defaults to `even`;
* an unparseable string or an unknown filter name is rejected with
  `SRT_EINVPARAM`;
* the FEC filter needs **4 extra bytes** of payload budget — two for the flag
  spec and two for the length clip — so the largest payload size it leaves
  available is 4 bytes below the live-mode maximum;
* ARQ and FEC run in parallel by default; lost packets are recovered by
  whichever mechanism completes first.

Rejection reasons for failed handshakes:

* `srt_getrejectreason`: returns a `SRT_REJECT_REASON` code for a socket that
  `srt_connect` reported as `SRT_ECONNREJ`, allowing the caller to distinguish
  the causes that the previous milestone delivered only as a generic refusal;
* `srt_rejectreason_str` and the `srt_rejectreason_msg[]` array: map reason
  codes to English strings;
* the enumeration includes `SRT_REJ_UNSECURE` (password required or
  unexpected), `SRT_REJ_BADSECRET` (mismatched passphrase),
  `SRT_REJ_MESSAGEAPI` (message/stream API collision), `SRT_REJ_CONGESTION`
  (incompatible congestion-controller type), and `SRT_REJ_FILTER` (incompatible
  packet filter), plus system, resource, and protocol-error codes.
  `SRT_REJ_FILTER` is part of the enumeration's order, but do not turn it into
  a rejection this milestone performs: a caller and a listener configured with
  *different* filters are **merged**, not refused, and `TEST_INTERFACE.md` §5
  states exactly what the groups assert about that case;
* on a fresh socket or an unknown socket id, `srt_getrejectreason` returns
  `SRT_REJ_UNKNOWN` (0).

Epoll enhancements:

* `srt_epoll_uwait`: a simpler alternative to `srt_epoll_wait` that returns
  ready sockets as an array of `SRT_EPOLL_EVENT` structures with `fd` and
  `events` fields, usable only with SRT sockets (no system sockets). Where
  `srt_epoll_wait` reports "nothing ready" as `−1` with `SRT_ETIMEOUT`,
  `srt_epoll_uwait` reports it as **0**. It carries the same empty-EID guard as
  `srt_epoll_wait`, however: on an EID with no subscribed sockets and without
  `SRT_EPOLL_ENABLE_EMPTY`, it fails with `SRT_EINVPARAM` exactly as
  `srt_epoll_wait` does, whatever the timeout. The two differ on *nothing
  ready*, not on *nothing subscribed*;
* `srt_epoll_set`: sets or clears flags on an EID; `SRT_EPOLL_ENABLE_EMPTY`
  allows `srt_epoll_wait` and `srt_epoll_uwait` to be called on an EID with no
  subscribed sockets. With the flag set and nothing ready, each reports the
  outcome in its own way — `srt_epoll_wait` waits out the timeout and fails
  with `SRT_ETIMEOUT`, `srt_epoll_uwait` returns 0 — instead of the immediate
  `SRT_EINVPARAM` both give without it. The function returns the **previous**
  flag value, so setting it on a fresh EID returns 0, setting it again returns
  1, and clearing it returns 1;
* `srt_epoll_wait` on an empty EID without `SRT_EPOLL_ENABLE_EMPTY` now fails
  immediately with `SRT_EINVPARAM` regardless of the timeout, whereas the
  previous milestone only rejected it when the timeout was negative;
* `srt_epoll_add_usock` and `srt_epoll_update_usock` now reject an event mask
  containing only `SRT_EPOLL_ET` (the edge-triggered bit) with `SRT_EINVPARAM`,
  requiring at least one of `IN`, `OUT`, or `ERR` to accompany it;
* `srt_epoll_update_usock` with a zero event mask removes the subscription,
  leaving the EID empty if it was the last socket, so a subsequent wait without
  `SRT_EPOLL_ENABLE_EMPTY` fails.

Function removal:

* `srt_perfmon` is deleted; the definition and the declaration are both gone,
  and code written against the previous milestone that calls it fails to link;
* `srt_bstats` remains and is the only performance-tracking function from this
  milestone forward; `SRT_TRACEINFO` and `CPerfMon` survive as type aliases.

## 2. What you inherit

Milestone 04 established the UDT-derived core, selective-repeat ARQ, TSBPD,
AES-CTR encryption with key material exchanged in the handshake and its KM
state readable through `SRTO_RCVKMSTATE` and `SRTO_SNDKMSTATE`, the HSv5
handshake with streamid, the BSD-shaped C API, and the epoll mechanism over
both SRT and system sockets. This milestone must keep all of that working: live messages
still cross an intact channel complete and in order, encryption with a matching
passphrase at both key lengths still carries the payload intact with
`SRTO_RCVKMSTATE` on the receiver and `SRTO_SNDKMSTATE` on the sender reaching
`SRT_KM_S_SECURED`, and a one-sided or mismatched passphrase still delivers
nothing to the application while the KM state reports the condition.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 3 **gate**
groups that check the milestones you inherited are still intact, and 11
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
