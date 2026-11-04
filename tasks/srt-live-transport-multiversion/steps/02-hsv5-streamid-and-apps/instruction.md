# Step 2 — A single-phase handshake, bidirectional transmission and the stream id

## 1. Implement this milestone

The library you inherit negotiates its SRT extensions in a second exchange that
follows the connection handshake, and carries data in one direction only. This
milestone folds that exchange into the handshake itself and opens the
connection both ways. `/app` already holds a working library and its build;
everything below is a change to it, not a new tree.

**One handshake, not two.** The extension block — protocol version, latency
proposal, encryption flags, stream id — travels inside the connection
handshake rather than as user-defined control packets sent after it. By the
time `srt_connect` returns to the caller, and by the time `srt_accept` has
produced a socket on the listener, both peers have each other's declarations.
The sender flag keeps its old meaning for a peer that still expects the
two-phase exchange, but between two peers that both implement this milestone it
decides nothing about which way data may flow.

**Both directions on one connection.** Either end may send; either end may
receive. Data arriving at a socket that set the sender flag is delivered to its
application instead of breaking the connection. One key, one latency
negotiation and one set of buffers serve both directions.

**The version surface.** A socket reports its own protocol version in the
documented `0xXXYYZZ` form, reports zero for the peer's until it has one, and
reports the peer's once connected. A minimum version may be required of a peer
before connecting. The version *value* you report is yours — the requirements
are that it is non-zero, that it has that shape, that the minimum-version
default does not exceed it, and that two peers of your own library agree.

**Latency splits in two.** A receiver declares the latency it wants for the
data it receives; a sender declares a minimum it asks the far receiver to
apply. The effective figure on a receiver is the larger of its own request and
the peer's proposal, and it is that negotiated figure — not the configured one
— that the latency options read back once connected. The older combined option
remains, setting both and reading back the negotiated value.

**Stream id.** A caller may attach a short string to a socket before
connecting; the listener reads it from the socket `srt_accept` returns. It is
carried in the handshake, so it is not set on the accepting side at all, and a
value beyond the documented length limit is refused.

**Transmission types.** One option selects a whole preset — live or file — and
setting it fixes timestamped delivery, too-late drop, periodic loss reports,
the message-versus-buffer API, the declared payload size and the
congestion-control algorithm together. The declared payload size becomes a
separate option in its own right, defaulting to the live value and bounded by
what one path MTU can carry.

**Message sizes are enforced, not tolerated.** In live mode a send longer than
the declared payload size is refused rather than fragmented, and a receive into
a buffer shorter than that size is refused rather than delivering a truncated
message. A zero-length receive is a bad parameter.

**The application-facing entry points.** A parameterless socket constructor, a
rendezvous connector, file send and receive, a message-control initialiser, a
send-buffer query, a byte-statistics call with an instantaneous mode, and a
connect that forces an initial sequence number. The general socket constructor
stops validating its type and protocol arguments, since every socket is a
datagram socket now.

**Retired options.** The two per-direction key-length options are gone; one
key length serves both directions. An option id this specification does not
define is refused — a get as an unsupported operation, a set as a bad
parameter.

## 2. What you inherit

Everything milestone 01 established. `/app` already holds a library that
builds, installs `<srt/srt.h>` and `libsrt.so`, and carries a live stream over
a lossy path; this milestone changes it rather than replacing it.

Two of that work's behaviours are re-run here as regression guards, chosen
because this milestone's changes reach into both: NAK-driven retransmission
with too-late drop switched off, and AES payload encryption reaching the
secured keying state and delivering nothing when the two passphrases differ.
Folding the extension exchange into the handshake changes when the latency and
the key material are known; opening the connection both ways changes which
buffers and which key a socket uses. Keep both working.

Timestamp-based delivery is **not** re-measured here. It is still required of
your library — milestone 01 established it and nothing in this milestone
licenses you to drop it, and a later milestone does measure it again — but no
group at this milestone measures how long a packet is held or whether a late
one is dropped, because the only way to measure either is to read a clock on a
shared machine. Do not read this as permission to remove it; read it as the
list of what a failure here can be about.

The build contract, the install layout, the library and header names, and the
socket, option, error, epoll and statistics surfaces of milestone 01 are
unchanged except where this milestone's §1 says otherwise. "Unchanged" is the
standard, not the measurement: the three gate groups re-run the retransmission
and encryption paths named above, and no group at this milestone exercises the
epoll surface — keep it working, but a regression there is addressed at a later
milestone, not by this one.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 3 **gate**
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
