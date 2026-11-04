# Step 7 — A declarative socket-option table with per-option width, default, range and restriction phase

## 1. Implement this milestone

`/app` already holds a working transport library. This milestone adds no
protocol feature. It replaces the socket-option layer with one declarative
table, and makes the library behave the way that table says it does.

Every option in `<srt/srt.h>` gains, in one place, four declared properties, and
all four become observable through the public C API:

* a **width**. `srt_getsockopt` reports the option's own width in `*optlen`, not
  the width the caller offered: a boolean option writes one byte and returns 1
  even when four bytes are available. A get whose declared length is smaller
  than the option's width is refused with `SRT_EINVPARAM` and writes nothing at
  all, rather than overrunning the caller's buffer;
* a **default**, and the rule that reading an option back is reading
  configuration, not sampling a runtime field. On an unconnected socket the
  getter reports the declared default before anything is set and the stored
  value afterwards — the receive latency reports its live default, the peer
  latency reports zero. Options that had no getter at all are readable now: the
  key-refresh pair and the retransmission-algorithm selector. One option joins
  the table, a 64-bit minimum input bandwidth, settable after connect,
  defaulting to zero;
* a **range**. A value outside it is refused with `SRT_EINVPARAM` and the option
  keeps the value it already had; nothing is clamped silently into range and no
  negative value is accepted where the range starts at zero. Options whose range
  admits `-1` as a sentinel still take it, and a cross-option constraint is
  checked against the value a sentinel stands for, not the literal argument;
* a **restriction phase**, and the two phases are now genuinely distinct. A
  pre-bind option is refused with `SRT_EBOUNDSOCK` once the socket is bound. A
  pre-connect option is refused with `SRT_ECONNSOCK` once the socket is
  listening, connecting or connected, but stays settable on a socket that is
  merely bound — reversing the previous milestone, which locked both classes at
  bind time.

String-valued options are parsed and validated inside the set call itself —
either entry point, `srt_setsockopt` or `srt_setsockflag` — rather than deferred
to connect: a packet-filter configuration naming a key the
filter does not define, or giving a known key a value outside its allowed set,
fails the set.

Add `srt_clock_type`, reporting which internal clock the library's timing is
built on with the identifier set the documentation lists, and export it from the
shared library.

Nothing is removed. Every option, error code, structure and function already in
`<srt/srt.h>` keeps its name and its meaning, and the enumeration of options
gains a final `SRTO_E_SIZE` marking its end.

Numbers are kept too, with one deliberate exception: `SRTO_BINDTODEVICE` moves
to sit directly after `SRTO_PEERIDLETIMEO`, ahead of the group-connection
options that `ENABLE_EXPERIMENTAL_BONDING` conditions, instead of following
them. Because §3 builds with `-DENABLE_EXPERIMENTAL_BONDING=ON`, that move
renumbers `SRTO_BINDTODEVICE` and the three group options; with the flag off
nothing moves at all. Options written with an explicit value —
`SRTO_PACKETFILTER = 60` and `SRTO_RETRANSMITALGO = 61` among them — are
unaffected either way, and no other option changes position.

## 2. What you inherit

`/app` arrives with the whole transport already working, and it keeps working:
live message transfer with TSBPD and too-late packet drop, ARQ recovery over a
lossy path, AES payload encryption with key-material state reported on both
sides, the packet-filter framework with its built-in FEC filter, level-triggered
`epoll` over SRT and system sockets, reject reasons, the listener access-control
callback and the stream id, the drift tracer, and application-supplied source
timestamps on send.

None of that is re-specified here. But the option layer is being rebuilt
underneath all of it, and everything above is configured through options, so the
three paths most at risk are re-asserted in this milestone's groups: the lossless
recipe (too-late drop off, a latency large enough for the path), the filter
configuration string, and the passphrase with its key length. Each of those can
break while every option call still returns 0. Configure the build with the same
flags as before.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 4 **gate**
groups that check the milestones you inherited are still intact, and 10
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
one a driver references — or `static_assert`s — without a working fallback, so
a tree that does not declare it affects the whole family; a name marked optional
is guarded, and missing it affects only the group that exercises it. Which names
fall on which side is measured from the drivers each time this document is
generated, so a section 5 that marks nothing as required means this milestone
introduces no unguarded name -- not that the distinction does not apply. Watch
this milestone's own table closely: it marks *both* new enumerators required,
not optional, because the fallback guard around them cannot fire against your
header.

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
