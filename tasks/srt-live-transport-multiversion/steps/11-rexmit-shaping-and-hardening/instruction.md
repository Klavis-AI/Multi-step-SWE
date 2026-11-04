# Step 11 — A retransmission bandwidth ceiling, and an API that validates its arguments and reports the real reason a call was refused

## 1. Implement this milestone

This milestone gives the sender a ceiling on the bandwidth it spends on
retransmission, and makes the API check what it is handed and say why it
refused.

**A retransmission bandwidth ceiling.** The sender's rate limit so far has
applied to everything it sends. This milestone adds a second, separate limit
that applies only to retransmitted packets, so a lossy link cannot let
recovery traffic crowd out fresh data. It is a read-write `int64_t` option in
bytes per second, settable before or after connect, on sockets, groups and
their defaults. It reads -1 on a fresh socket, meaning no ceiling; 0 forbids
retransmission altogether; any positive value is the ceiling, counted with the
transport header. Anything below -1, and any **non-zero** width other than
eight bytes, is refused as a bad parameter (a zero length is still accepted,
as the next paragraph says). The option is compiled only when
its build option is enabled; this milestone's configure line enables it.

**Arguments are checked before they are used.** Every option setter now
refuses a negative length as a bad parameter before it looks at the value,
for numeric and string options alike. A zero length is still accepted. The
version option can no longer be set at all: it is read-only, and a set is
refused as a bad parameter. Accept checks the caller's address-buffer length
before it takes a connection off the queue, so a refused accept leaves the
connection where it was and a retry with a proper buffer gets it. The event
poll that fills a caller-supplied array never reports more entries than the
array holds, and with a zero-length array it reports zero. The listener and
connect hook installers accept a null hook, which removes any hook installed
before; what they now refuse, as a connected-socket error, is any change of
hook on a socket already listening, connecting or connected.

**The real reason a call was refused.** Listen on a socket that has already
connected, or been accepted, is refused as a connected-socket error rather
than as unbound; listen on an unbound rendezvous socket is refused for being
rendezvous rather than for being unbound. A second startup while the library
is already running reports plain success, not the code reserved for the first
start. A connected file-mode socket reports through the payload-size option
the largest message one packet carries at the negotiated segment size, where
before it reported the zero it was configured with.

**Surface.** The key-material state enumeration gains a terminal size member,
the invalid system-socket value gains a name, and the bad-crypto-mode state
keeps its value.

## 2. What you inherit

The previous milestone gave the transport authenticated encryption and made the
sender refuse a message it could not carry in one packet, on top of everything
the chain had accumulated before it: socket groups over member links, the
main/backup mode, the socket-option restriction matrix, strict encryption with
key-material state reporting, packet filtering, TSBPD with too-late packet
drop, selective-repeat recovery over a lossy channel, and a level-triggered
event poll over SRT and system sockets together. All of it must still work
here. This milestone touches the retransmission path, the option setters, the
listen and accept paths, the hook installers and the event poll that fills a
caller's array, so the two things most easily broken are the ones asserted
again below: a burst the relay drops at fixed indexes is still retransmitted
and delivered byte-identical with the loss visible in the receiver's packet
loss counter, and the event poll that returns sets still reports an SRT
socket readable until its message is consumed, still reports a system socket
readable when a datagram waits on it, and still reports no socket at all and
fails with `SRT_ETIMEOUT` when nothing is ready. The first sits on the very path the new retransmission
ceiling instruments; the second shares its readiness list with the
array-filling poll whose count this milestone corrects.

Two things the previous milestone's tree does are deliberately **not**
inherited, because correcting them is this milestone's work. That tree accepts
a negative option length, counts ready events past the end of the caller's
array, refuses a null hook, reports every non-listenable socket as unbound,
loses a queued connection when accept is given a short address buffer, and
lets the version option be set. None of that is asserted again; a tree that
still behaves the old way does not satisfy this milestone's `sockopt` and
`session` groups. The socket-option restriction matrix, the defaults `srt-docs/` states,
and every statistics field read by any earlier milestone are unchanged and
read exactly as they did before.

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
one a driver references without a fallback guard, so a tree that does not
declare it affects the whole family; a name marked optional is guarded, and
missing it affects only the group that exercises it. Which names fall on which
side is measured from the drivers each time this document is generated, so a
section 5 that marks nothing as required means this milestone introduces no
unguarded name -- not that the distinction does not apply.

Everything is measured through the installed public interface: `/app` is
built with

```
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp -DENABLE_MAXREXMITBW=ON \
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
