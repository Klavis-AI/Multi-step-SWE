# Test interface — milestone 10 of 11

Everything the verifier for this milestone requires of you, and nothing else.
The task-wide build contract and scope rules are in `PROFILE.md`; what the
previous milestone left behind is in `WORKSPACE_HANDOFF.md`.

This file is generated from the verifier's own group registry. The table in §4
is not a summary of the tests — it *is* the list the verifier iterates.

---

## 1. What is built, and how

The verifier configures, builds and installs `/app` into a staging prefix of
its own:

```
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp \
      -DCMAKE_INSTALL_PREFIX=/usr/local
make -C <build> -j4
make -C <build> install DESTDIR=<dest>
```

It then compiles its own C++ programs against `<dest>/usr/local`, including
only `<srt/srt.h>`, and linking `-lsrt` together with the system thread
and dynamic-loader libraries -- nothing else of yours. If either build fails, every group
below fails at build time; the scorecard says so in `build_note`, so a build
failure is never mistaken for a step's worth of missing capabilities.

Your build gets 900 s; the verifier's own programs get 300 s. Reference
timing on this image, for the released library at this milestone: about
14 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.5.2**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.5.2:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `5` |
| `SRT_VERSION_PATCH` | `2` |
| `SRT_VERSION_STRING` | `"1.5.2"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.5.2)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.5.2 is acceptable.

No verifier translation unit at this milestone compiles conditionally
on `SRT_VERSION_VALUE`, so nothing here turns on the version alone.
The macros are part of the installed interface either way, and the
header must report this milestone's version.


---

## 2. How the work is checked

There are 5 **gate** groups (§3) and 9 **requirement** groups
(§4). The two do different jobs and it is worth being precise about which:

- A **gate** group asks whether the work the earlier milestones already signed
  off is still intact. It adds no new requirement of its own, but a failure
  there means this step is not satisfied, however much of the new milestone
  you finished. `reward_details.json` names the gate group that failed and
  why.
- A **requirement** group covers this milestone's own work. Every group in §4
  is in scope, and every case within a group must pass.

The practical reading: **do not break what you inherited**, and then implement
as much of this milestone as you can. Preserving the inherited behaviour and
implementing nothing new satisfies none of the new work.

Each group is a filtered run of one verifier program. A group passes when that
run reports every one of its cases passing, and when the number of cases it
executed equals the number the verifier declared for it — a filter that matched
nothing is a failure, not an empty pass.

Per-case and per-group limits: each case is killed after 60 s, each group
after 150 s, and all groups together after 1200 s. A hung case fails its own
group and nothing more.

---

## 3. The gate

These pass or fail as a set. They add no new requirement, but a failure there
means the step is not satisfied. They are proof of life plus one end-to-end
path through each milestone this step carries forward.

- `proof-of-life` — the project in `/app` configures, builds and installs with the documented command, and the verifier's own driver tree is intact
- `transfer` — live messages at the negotiated payload size cross an intact channel complete, in order and byte-identical, and the stream keeps flowing at the default segment size where no message exceeds what one packet carries
- `recovery` — datagrams the relay drops at fixed indexes are retransmitted and the payload arrives complete, in order and byte-identical once too-late packet drop is off and the latency budget is generous, with the receiver's packet loss counter and the sender's retransmission counters reporting at least the loss the schedule removed, so the case cannot pass by the loss never having happened
- `encryption` — a matching passphrase at each of the three key lengths carries the payload intact with the receiving side reporting the secured key-material state, while with enforced encryption left at its default a mismatched passphrase is still refused at connect time
- `endtoend` — a live stream of 3000 messages, each within the single-packet payload limit in force, crosses a clean relay and arrives byte-identical, in order and with no gaps, and the same stream does the same over a relay that adds 15 ms each way. Two checks, both in this one group

A gate group is deliberately narrow: broad enough that a real regression is
caught, narrow enough that it cannot fail for any reason *except* a regression.
None of them asserts anything this milestone introduces, and none asserts
timing, host load, iteration order or anything else the public contract does
not promise.

---

## 4. The requirement groups

`endtoend` is **not** in the table below. At this milestone the end-to-end
path is the plain live transport the previous milestone already carried, so
it sits in the gate (§3): a failure there means the step is not satisfied,
but it is not a requirement group. Including it here would cover transport
this milestone inherited rather than work it introduces.

| # | Group | What it asserts |
|---|---|---|
| 1 | `cryptomode` | the cipher-mode option reads 0 on a fresh socket as a four-byte integer, accepts 0, 1 and 2 before connect, refuses -1 and 3 with `SRT_EINVPARAM`, refuses the AES-GCM value on a socket whose TSBPD is switched off, and on a connected socket reports the mode the two sides agreed rather than the one requested |
| 2 | `gcm-session` | with AES-GCM selected on both sides and one matching passphrase the connection is established at each of the three key lengths and carries the payload intact while the receiver reports the secured key-material state, a peer left at the automatic setting adopts AES-GCM from a caller that asked for it, and a caller and listener that name different cipher modes are refused with the crypto-mode rejection reason on the caller |
| 3 | `badsecret` | when the two sides hold different passphrases and the listener has enforced encryption switched off, the caller's rejection reason is the one that says the passphrases differ, not the one that says only one side set a passphrase |
| 4 | `argcheck` | `srt_connect` given a zero address length and `srt_bind` of the IPv6 wildcard on a socket whose IPv6-only flag is left at its -1 default are both refused with `SRT_EINVPARAM`, while the same connect given the true address length, and the same bind with the IPv6-only flag first set to 0 or to 1, still succeed |
| 5 | `muxer-reuse` | a socket whose IPv6-only flag is at its -1 default binds an address and port already bound by a socket that set the flag to 0, and succeeds by sharing the multiplexer, while the same two binds in the opposite order are still refused with `SRT_EBINDCONFLICT` |
| 6 | `reject-hook` | when the listener's `srt_listen_callback` hook returns -1 without setting a rejection reason of its own, the caller's blocking connect fails and reports the first predefined rejection code, and when the hook does set its own reason that reason is reported instead |
| 7 | `onepacket` | with the maximum segment size set to 1000 on both sides and TSBPD on, `srt_sendmsg` of a 1200-byte message on the connected caller is refused with `SRT_ELARGEMSG` instead of being split across two packets, while a message of the largest size that still fits one packet at that segment size is accepted and delivered intact |
| 8 | `rcvloss-bytes` | after messages cross a clean link on which nothing is lost, the interval `byteRcvLoss` reported by `srt_bstats` is zero rather than equal to the interval `byteRecv` of the same sample, and `pktRcvLoss` in that sample is zero as well |
| 9 | `undecrypted` | when the two sides hold different passphrases and enforced encryption is switched off on both, the receiver hands no message at all to the application and never reports the socket readable, while the interval `byteRcvDrop` reported by `srt_bstats` accounts for every arrived packet with its headers and `pktRcvUndecrypt` still counts those packets |

Each group above belongs to this milestone's own work: at the previous
milestone's release it did not pass, and it does not sit in the gate. A group
is therefore one this milestone changes the answer to, which is not the same
as one whose every case is new -- a group may well exercise behaviour an
earlier milestone established on the way to the part that is not. Breaking
what you inherited matters as much as failing a new requirement.

---

## 5. Behaviour the groups rely on

Everything below is behaviour of the release this milestone targets that a
group asserts, and that the specification and API documents in your reading
list do not state — or state in a way that reads as something else. It is here
because a group that asserts a requirement no document gives you fails a
correct implementation, which is a defect in the task, not in your work. If
you find a group asserting something that is neither here nor in the reading
list, that is a bug; the group is wrong, not you.

One qualification to the paragraph above, because this section has two halves.
Most of what follows is behaviour that a group in §3 or §4 asserts. A few lines
instead record what this milestone deliberately **declines** to assert -- the
reject-reason strings, `SRTO_PAYLOADSIZE` as a predictor of the one-packet
limit, the two receiver counters that do not move, how long a rejected connect
takes, and the guarded declarations no group can observe. Every one of those
says so in as many words, so a line here carrying no such note is a line some
group checks. Both halves are here for the same reason: they are things a
correct implementation needs, and the two drafts in `srt-spec/` and the
documents in `srt-docs/` either do not state them at all, or state them in a
way that leaves what the verifier checks open. Where a line here contradicts
`srt-docs/`, this section is what the verifier checks.

**The cipher-mode option is reached by its numeric position in `SRT_SOCKOPT`,
and that position is 62.** The drivers cast integers, so the numbering is part
of the contract and no shipped document gives it. 62 is the next free position
after the retransmit algorithm option at 61; nothing at or below 61 may move.
The option is `int32_t`, pre-connect, readable and writable. Its getter
reports the option's own width and not the caller's: handed an eight-byte
buffer it writes four bytes, leaves the other four untouched, and reports an
`optlen` of 4. `srt-docs/` describes the option's values and its negotiation
matrix correctly -- that part you can follow as written. Of that matrix, the
caller/listener rows are what the groups here exercise; no group at this
milestone connects in rendezvous mode, so the rendezvous rows are yours to
implement from the table and are out of scope at this step.

**Enabling the preview build option lengthens two enumerations, and the
verifier drivers are compiled with it.** With `ENABLE_AEAD_API_PREVIEW`
defined, `SRTO_E_SIZE` is **63**, not 62, and `SRT_REJ_E_SIZE` is **18**, not
17. Without it they are 62 and 17. Both the library and the drivers are built
with the option on -- the library through the `-DENABLE_AEAD_API_PREVIEW=ON`
in the configure line §1 prints, every driver translation unit through a plain
`-DENABLE_AEAD_API_PREVIEW` on its own compile line -- so 63 and 18 are the
values in force. The two additions are guarded declarations: `SRTO_CRYPTOMODE`
(62), `SRT_REJ_CRYPTO` (17) and `SRT_KM_S_BADCRYPTOMODE` (5) must all sit
inside `#ifdef ENABLE_AEAD_API_PREVIEW` in the public header, so that a
translation unit compiled without the define sees the shorter enumerations and
none of the three names. `add_definitions` reaches neither `srt.pc` nor any
generated header, so a program that wants these declarations must pass the
`-D` itself; do not leak the feature into an unguarded header.

**No group observes that guard, and it is required anyway.** Every driver
translation unit is compiled with that same `-D`, so nothing
this verifier runs can see an unguarded declaration: a header that declares the
three names unconditionally passes every group at this milestone. The guard is
an obligation on the header you ship rather than something a group will catch,
and `WORKSPACE_HANDOFF.md` carries it into the next milestone. For the same
reason 63 and 18 are asserted by nothing: those two are compile-time values
that follow the *reader's* `-D`, so a group asserting them would be asserting
its own compile line rather than your library.

**The cipher-mode option refuses by error code, and a refused set stores
nothing.** A value outside [0, 2] -- tested with -1 and with 3 -- is refused
with `SRT_EINVPARAM`. Selecting AES-GCM (2) on a socket whose `SRTO_TSBPDMODE`
is off is refused with `SRT_EINVPARAM` at the moment the option is set, not
later at connect time. `srt-docs/` states the range and states that AES-GCM
requires TSBPD, but gives neither code nor the moment of refusal. Either
refusal must also leave the option's stored value exactly where it was:
validate before you store, so that a socket parked at 1 still reads 1 after a
refused set of 3, and a socket that never set the option still reads 0 after a
refused AES-GCM set. A getter that reports a value the setter rejected fails
this milestone, and this line is the only place that says so.

**Two options `srt-docs/` marks write-only are read back, and must answer.**
The option tables give `SRTO_TSBPDMODE` and `SRTO_ENFORCEDENCRYPTION` a `W`,
but both are readable in the release this milestone targets and groups read
both -- `SRTO_TSBPDMODE` to prove the fixture a cipher-mode refusal needs,
`SRTO_ENFORCEDENCRYPTION` to prove that a relaxation really took. Each is a
one-byte boolean: read it with `srt_getsockflag` into a single `char` with an
`optlen` of 1 and it reports the value last set, or on a fresh socket the
default -- 1 for `SRTO_ENFORCEDENCRYPTION`, and 1 for `SRTO_TSBPDMODE`, whose
`srt-docs/` Default column reads `*` because it follows the transmission type
and the transmission type defaults to live. Writes are accepted in either
width: the drivers set `SRTO_TSBPDMODE` with a four-byte integer and
`SRTO_ENFORCEDENCRYPTION` with a single byte, and a library that took only one
of those widths would fail here. A library that refused these reads, or
answered a one-byte read as a four-byte integer, would fail groups that are
about something else entirely.

**`SRTO_RCVSYN` and `SRTO_SNDSYN` may be set at any state.** Both are typed
`pre`, and no restriction phase reaches them: the released library accepts a set
of either at **any** socket state -- bound, listening, connecting, connected,
and on the socket `srt_accept` returned -- with the new value taking effect for
subsequent calls. A socket returned by `srt_accept` starts with the listener's
value, so a listener opened non-blocking yields a non-blocking accepted socket
whose receive mode you may then switch back. Refusing such a set loses every
group that receives on an accepted socket, gates included.

**Documented surface no group here reaches.** Recorded so that its absence is
not read as an omission to fix: one row of the negotiation matrix in
`srt-docs/API/API-socket-options.md` is a documented **rejection** no case
exercises — `0 (auto)` on the caller against `AES-GCM (2)` on the listener.
The rows the groups do exercise are the caller-GCM-against-auto-listener
adoption (caller 2 / listener 0 → 2), the both-auto control (0/0 → 1) and the
two mismatched refusals (1/2 and 2/1, both rejected with `SRT_REJ_CRYPTO`).
The
listener-callback behaviour the same section describes (reading or forcing
the cipher mode on the connection's socket from inside a listener callback) is
likewise unexercised; and nothing tampers with a GCM datagram or checks a tag,
so the mode's authentication property is stated in the documents and measured
nowhere -- a self-consistent AEAD scheme passes every group here. Implement all
of them as the documents describe; nothing here measures them either way.

**The key-material state is read on the accepted socket, and when it is read
matters.** `SRTO_KMSTATE` is read with `srt_getsockflag` into a four-byte
integer, always on the socket the listener accepted -- never on the listening
socket and never on the caller. The two values the groups require are sampled at different moments, and
the asymmetry is deliberate. `SRT_KM_S_SECURED` is required only of a session
that has already carried its messages: no group samples it at the instant
`srt_connect` returns, so a key exchange that settles as the first traffic
moves is not failed here, and a group that would otherwise move no payload
sends a few messages for the purpose. `SRT_KM_S_BADSECRET` is read earlier --
on the accepted socket as soon as that socket reports `SRTS_CONNECTED`, before
any message has been sent -- because a passphrase mismatch must be settled by
the time the connection is reported rather than discovered on the first packet
that fails to decrypt. `srt-docs/` gives the enumeration and the meaning of
each value; which socket answers and the moment at which it answers are what
this line fixes.

**The key length is readable on the accepted socket.** `SRTO_PBKEYLEN` is set
before the connection on both ends -- 16, 24 and 32, the values `srt-docs/`
documents -- and with the same length configured on both, the socket the
listener accepted reports that length once the connection is up, read as a
four-byte integer. `srt-docs/` gives the option its range and marks it
read-write but says nothing about what the accepted socket answers after a
connection, and that direction is what a group reads.

**A cipher-mode mismatch is reported to the caller as `SRT_REJ_CRYPTO` (17).**
`srt-docs/` lists that reason in a table and describes it in one line, but does
not say which side observes it or when. The caller's `srt_connect` fails and
`srt_getrejectreason` on the caller yields 17, for AES-GCM against an AES-CTR
listener and for AES-CTR against an AES-GCM listener alike. `srt_rejectreason_str(17)`
returns "Crypto mode" in the released build, where before this milestone it
returned the unknown-reason string, and that string table entry is part of what
changes here. **No group asserts that string**: the table is compiled into
`libsrt` and follows the library's own build options, so its text is not a safe
thing to check. The numeric code 17 is what the groups read, and the string is
recorded here only so the change is not a surprise.

**A message that does not fit one packet is refused, and the limit is derived
from `SRTO_MSS`, not from `SRTO_PAYLOADSIZE`.** In message mode with TSBPD on,
the largest payload `srt_sendmsg` accepts on a connected socket is exactly
`min(1316, SRTO_MSS - 44)`, where 44 is 20 bytes of IPv4 header plus 8 of UDP
plus 16 of SRT header. Measured at the tag: an MSS of 700 accepts 656 and
refuses 657, 1000 accepts 956 and refuses 957, 1100 accepts 1056, and 1500
accepts 1316 because the fixed cap binds first. Exceeding that limit while
still at or below 1316 raises **`SRT_ELARGEMSG`**; exceeding 1316 raises
`SRT_EINVALMSGAPI` instead, and always did. `srt-docs/` describes
`SRT_ELARGEMSG` only as the sending buffer being too small for the message,
which is a different and older check against `SRTO_SNDBUF`; the one-packet rule
is not documented at all. Note also that **`SRTO_PAYLOADSIZE` does not predict
this limit**: its getter returns 1316 on a connected socket left at that
default, whatever the MSS, so it cannot be used to compute the boundary. (`srt-docs/` marks that option
write-only in its table; it is in fact readable on a connected socket and reads
1316. **No group reads it back** -- it is named here because it is the
plausible wrong way to compute the boundary, not because it is in scope.) A
refused send must put **nothing on the wire**: the message is rejected before
any datagram leaves the socket, so the count of datagrams the peer sees does
not move. Sending the message split across two packets and reporting an error
afterwards is not an acceptable reading of the rule.

**What `SRTO_PAYLOADSIZE` does do is bound the live segmentation, and it is
written rather than read.** It is settable before the connection, and the
transport must size a live stream to the value it was given. A group that
carries messages smaller than the 1316 default sets the option to that size on
**both** ends before connecting, because a peer left at the default does not
deliver such a stream at all -- measured at the tag, a stream of 1200-byte
messages over a peer at the default reaches the receiver's arrival counters and
is then handed to no application at all, with `srt_recvmsg` timing out on every
call. Honouring the value you were given is therefore part of the contract even
though nothing reads it back.

**A listener hook that rejects without naming a reason yields
`SRT_REJC_PREDEFINED` (1000).** This contradicts `srt-docs/` directly: the
access-control document states that "in a normal situation, the rejection code
is predefined as `SRT_REJ_RESOURCE`", which is 3. That is not what this
milestone does. When a `srt_listen_callback` hook returns -1 without calling
`srt_setrejectreason`, the caller's blocking connect fails and
`srt_getrejectreason` on the caller reports exactly **1000**. If the hook does
call `srt_setrejectreason` first, the value it set is what the caller reports,
and that path is unchanged. The function reference for `srt_listen_callback` is
right about the return value itself -- it states that returning 0 accepts the
connection and that returning -1, or throwing, rejects it -- and says nothing
about which rejection reason the refused caller then reads. That last part is
what this line fixes; the rest of that reference you can follow as written.

**Do not assert how long the rejected connect takes.** The rejection is
detected promptly -- single-digit milliseconds on loopback -- both before and
after this milestone. Only the reported reason distinguishes them.

**Multiplexer sharing across the IPv6-only flag is asymmetric.** A socket left
at the -1 default may bind an IPv4 address and port already bound by a socket
that set `SRTO_IPV6ONLY` to 0, and succeeds by sharing the multiplexer. The
opposite order -- the socket with the flag at 0 binding second, over an
existing socket at the -1 default -- is still refused with
`SRT_EBINDCONFLICT`. The binding table in `srt-docs/` covers only the IPv6
wildcard rows and says nothing about how the flag's value affects sharing for
an ordinary IPv4 address, in either order.

**A bind of the IPv6 wildcard with the flag unset fails with `SRT_EINVPARAM`.**
`srt-docs/` does state that the flag must first be set explicitly to 0 or to 1,
and the group asserts the positive control too: with the flag set to either
value the same bind succeeds.

**`srt_connect` with a zero address length fails with `SRT_EINVPARAM`.** No
shipped document says what a zero `namelen` means. It must be rejected rather
than inferred from `sa_family`, and the socket is left unconnected --
`srt_getsockstate` on it must not report `SRTS_CONNECTED`. Only 0 is asserted;
the group passes the true length as its positive control and never passes a
null address pointer.

**The caller sees `SRT_REJ_BADSECRET` when the passphrases differ.** With a
different passphrase on each side and `SRTO_ENFORCEDENCRYPTION` switched off on
the listener, the caller's connect fails and `srt_getrejectreason` on the
**caller** yields `SRT_REJ_BADSECRET` (10), not `SRT_REJ_UNSECURE` (11).
`srt-docs/` describes what the two reasons mean but not which one arises in
this configuration, and the distinction is exactly what the group turns on.

**The other half of that distinction: a one-sided passphrase still yields
`SRT_REJ_UNSECURE` (11).** In the same fixture -- the listener's
`SRTO_ENFORCEDENCRYPTION` switched off, the caller's left at its default --
where only **one** of the two parties sets a passphrase at all and the other
sets none, the caller's connect fails and `srt_getrejectreason` on the
**caller** yields `SRT_REJ_UNSECURE` (11) and not `SRT_REJ_BADSECRET`. That
holds whichever side supplies the passphrase: the caller alone, and the
listener alone, both report 11 on the caller. `srt-docs/` glosses the reason as
"Only one connection party has set up a password" but does not say which side
observes it, nor that it arises in both directions, and 10 and 11 have to be
distinguishable for either line to mean anything.

**Undecryptable packets are dropped, not delivered, and not signalled.** With a
different passphrase on each side and enforced encryption switched off on both,
the packets arrive and fail to decrypt. The application must receive **nothing**
from them: `srt_recvmsg` returns -1, and a socket registered for `SRT_EPOLL_IN`
must never be reported readable on their account. Their accounting is precise
and the two conventions differ: `pktRcvUndecrypt` and `byteRcvUndecrypt` count
the packets and their **payload only** (1316 bytes per packet for the
1316-byte sends the group uses), while the interval `byteRcvDrop` counts the
same packets **with all headers** (1360 bytes each, payload plus the 44 header
bytes). The interval `byteRcvDrop` is therefore non-zero here. `srt-docs/`
states the header convention for the byte counters, states that undecryptable
packets are dropped by the too-late-drop mechanism, and -- in
`SRTO_ENFORCEDENCRYPTION`'s own section, describing this exact fixture -- says
that the receiver "will not deliver it to the application". What it nowhere
states is the readability consequence, and it nowhere gives the interval
`byteRcvDrop` behaviour that changes at this milestone.

**Two counters in that same sample do not change and must not be treated as
signals.** `pktRcvDrop` reads the same number of packets before and after this
milestone -- previously because undecrypted packets were added into it
explicitly, now because they are genuinely dropped -- and the accumulated
`byteRcvDropTotal` is likewise unchanged. Neither is asserted as a difference
by any group, and an implementation must not alter them to create one.

**Loss and retransmission counters are contractual as floors where the link
lost something, and as an exact zero where it did not.** Where a channel
removes datagrams, the receiver's `pktRcvLoss` and `pktRcvLossTotal` must
report **at least** as many lost packets as were removed, and the sender's
`pktRetrans` and `byteRetransTotal` must report **at least** that many
retransmitted packets and at least the bytes they amount to counting the 44
header bytes with each. Nothing puts a ceiling over those four. Incidental
loss between the two processes, and the periodic loss report that
`SRTO_NAKREPORT` leaves on by default in live mode, each raise the numbers
above the count that was removed without any defect on your side, so do not
build a counter that reports exactly the number of datagrams a channel dropped
and no more. Where nothing was lost the receiver's loss counters must read 0,
and that half **is** asserted exactly. `srt-docs/` gives all four counters
their meanings and gives the byte counters the 44-byte convention; what it
does not say, and what this line fixes, is which direction of each is part of
the contract.

**The interval `byteRcvLoss` is zero on a link that lost nothing.** It reports
lost bytes. It previously reported the received byte count, so on a clean
loopback link it equalled the interval `byteRecv`; that is now wrong. Read it
from the trace fields with `srt_bstats(s, &p, 0)` in the same sample as
`byteRecv` and `pktRcvLoss`. The accumulated `byteRcvLossTotal` was already
correct and stays 0 on such a link. `srt-docs/` defines the interval field only
as "same as the accumulated one, but for a specified interval", which does not
distinguish the two behaviours.

### Public names this milestone must introduce for the verifier to build

The verifier's translation units reference the public names below, and
each of them is new in this milestone -- absent from the header you
inherit, present in the header a correct implementation installs. A
name marked **required to compile** is used with no `#ifndef` fallback:
a header that omits it does not merely fail one group, it fails to
build the whole family, and every group in that family is lost,
including any gate group. This list is generated from the drivers, so
it cannot drift from what they reference. Its scope is the names **new
in this milestone**: a name you inherit from an earlier milestone is not
listed here even where a driver depends on it with no fallback, so
absence from this table is not a promise that nothing references it.

It tells you the spelling and nothing else. What each name means, what
it does and what value it takes are in the sections and documents
above; if a name here is discussed nowhere else, that is a defect in
the task and you should implement whatever the behavioural text asks
for and give the name the obvious declaration.

| name | |
| --- | --- |
| `SRTO_CRYPTOMODE` | **required to compile** |
| `SRT_KM_S_BADCRYPTOMODE` | **required to compile** |
| `SRT_REJ_CRYPTO` | **required to compile** |

### Constants whose numeric value the verifier pins

A verifier driver in this step depends on the numeric value of each
constant below, so **these values are part of the interface**. They
are read at run time and compared against the value below: number
one of those differently and the group that reads it fails, with the
others unaffected.

They are listed here because a verifier may only assert a numeric
value that a document fixes, and this is the document that fixes
them: nothing else in your reading list states a numeric value for
any of these names.

The pins exist because a restated constant that drifts would make the
verifier silently address a *different* option and report a pass for
the wrong reason. Every other constant is yours to number — with one
exception: a constant whose *position* a section above fixes as part of
the contract, because a group reaches it by number rather than by name,
is fixed by that section and not by this table.

| constant | value | if you renumber it |
| --- | --- | --- |
| `SRTO_CRYPTOMODE` | `62` | fails its group |
| `SRT_KM_S_BADCRYPTOMODE` | `5` | fails its group |
| `SRT_REJ_CRYPTO` | `17` | fails its group |

---

## 6. What the verifier never does

- It never reads your sources, your build tree, or any header you did not
  install under `include/srt/`.
- It never links an internal symbol.
- It never runs an application binary: the build always passes
  `-DENABLE_APPS=OFF`.
- It never uses a real network. Not every group opens a connection; where one
  does, both its endpoints are in one process on the loopback interface,
  sometimes through a UDP relay the verifier owns, which drops, duplicates,
  reorders and delays specific datagram **indexes** so that every run of the
  same build reaches the same verdict.
