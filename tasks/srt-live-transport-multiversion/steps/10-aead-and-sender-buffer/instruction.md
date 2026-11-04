# Step 10 — Authenticated encryption with AES-GCM, and a sender that refuses a message it cannot carry in one packet

## 1. Implement this milestone

This milestone gives the live transport authenticated encryption, and makes the
sender refuse a message it cannot carry in a single packet.

**Authenticated encryption.** Encryption so far has been AES in counter mode:
the payload is concealed, but nothing detects a payload altered in flight. This
milestone adds AES-GCM, which authenticates as well as conceals, and a
pre-connect socket option that chooses between the two. The option is a
read-write `int32_t` on sockets, groups and their defaults. It reads 0 on a
fresh socket, accepts 0 (choose automatically), 1 (AES-CTR) and 2 (AES-GCM),
and refuses anything else. AES-GCM authenticates each packet as it becomes
playable, so it is accepted only on a socket whose timestamp-based delivery is
on; selecting it with that switched off is refused when the option is set.

The two sides settle the mode during the handshake, and the rule is **not
symmetric**. A listener left at the automatic setting takes the mode the caller
named, AES-GCM included. A caller left at the automatic setting does not:
against a listener that named AES-GCM the connection is rejected. AES-CTR
results wherever neither side named AES-GCM. Two parties naming different modes
cannot agree either: the connection is rejected, and the reason says the cipher
modes did not match rather than blaming the passphrase. The negotiation table in
`srt-docs/` gives every caller/listener and rendezvous pair, and it is correct
as written -- implement that table, not a symmetric reading of this paragraph.
Once a connection exists, reading the option back on either side reports the
mode actually agreed. The key-material
enumeration gains a value for a peer whose cipher mode is unacceptable; at this
milestone that value is set only on the half-built socket the listener then
rejects, so it is never reachable through `SRTO_KMSTATE`, `SRTO_SNDKMSTATE` or
`SRTO_RCVKMSTATE` on any socket an application holds. Treat it as an
enumeration member, not as a state you can observe. The feature is compiled
only when its build option is enabled, and that option requires the EVP
encryption backend; where it is not enabled the option, the rejection reason
and the key-material state are absent and the enumerations that hold them are
one shorter.

**One message, one packet.** In live mode the transport carries one application
message in one packet. The sender used to check a submitted message only
against a fixed maximum payload, so a message small enough by that measure but
too large for the packet size actually in force was accepted and quietly split
across two packets. The sender now works out how many packets the message needs
at the packet size in force, and refuses with a size error any message needing
more than one while timestamp-based delivery is on.

**Stricter arguments, one relaxed binding.** A connect given a zero address
length, and a bind of the IPv6 wildcard on a socket that has not yet said
whether it wants IPv6 only, are now refused as bad parameters. Binding relaxes
in one direction only: a socket that has expressed no IPv6 preference may share
the multiplexer of one that asked not to be restricted to IPv6, though not the
reverse. A listener hook rejecting a connection without naming a reason leaves
the caller a definite predefined code rather than one suggesting a bad peer.

**Reporting that stops lying.** The interval byte counter for lost packets
reported the received byte count instead; it now reports lost bytes, and reads
zero on a link that lost nothing. Packets that fail to decrypt are no longer
handed up as ciphertext: they are dropped, the socket is never reported
readable on their account, and their bytes are counted among dropped bytes.

## 2. What you inherit

The previous milestone rewrote the receiver buffer under everything the chain
had accumulated: socket groups carrying a live stream over member links, the
main/backup mode with its minimum link stability timeout, the socket-option
restriction matrix, strict encryption with key-material state reporting, packet
filtering, TSBPD with too-late packet drop, and selective-repeat recovery over
a lossy channel. All of it must still work here. This milestone touches the
sending path, the key-material exchange and the receiver's handling of packets
it cannot decrypt, so the three things most easily broken are the ones asserted
again below: an ordinary live stream still crosses an intact channel complete
and in order, a dropped burst is still retransmitted and delivered byte-
identical with the loss visible in the receiver's packet loss counter, and a
matching passphrase still carries the payload intact at each of the three key
lengths while a mismatched one is still refused at connect time.

Two things the previous milestone asserted are deliberately **not** inherited,
because correcting them is this milestone's work. That milestone pinned the
interval `byteRcvLoss` as non-zero and equal to the interval `byteRecv` on a
link that lost nothing, and it pinned undecryptable packets as being handed to
the application as ciphertext with a zero interval `byteRcvDrop`. Both are
wrong and both change here; a tree that still behaves the old way does not
satisfy this milestone's `rcvloss-bytes` and `undecrypted` groups. Nothing else about those
statistics moves: the packet counters `pktRcvDrop` and `pktRcvUndecrypt` and
the accumulated `byteRcvDropTotal` read exactly as they did before.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 5 **gate**
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
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp \
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
