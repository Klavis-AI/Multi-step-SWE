# Test interface — milestone 9 of 11

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
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_BONDING=ON \
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

This milestone is SRT **1.5.0**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.5.0:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `5` |
| `SRT_VERSION_PATCH` | `0` |
| `SRT_VERSION_STRING` | `"1.5.0"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.5.0)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.5.0 is acceptable.

No verifier translation unit at this milestone compiles conditionally
on `SRT_VERSION_VALUE`, so nothing here turns on the version alone.
The macros are part of the installed interface either way, and the
header must report this milestone's version.


---

## 2. How the work is checked

There are 4 **gate** groups (§3) and 10 **requirement** groups
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
- `recovery` — datagrams the relay drops at fixed indexes are retransmitted and the payload arrives complete, in order and byte-identical once too-late packet drop is off and the latency budget is generous, with the receiver's loss counter confirming that the loss really happened
- `tsbpd` — the receiver buffering latency a connected socket reports is the one the handshake negotiated, the larger of the receiver's own `SRTO_RCVLATENCY` and the peer's `SRTO_PEERLATENCY`, read back from both ends of a link whose two sides asked for different values and in both orders, while the direction neither side configured still reports what it did before the connect and the stream itself arrives complete and in order
- `encryption` — a matching passphrase at both key lengths carries the payload intact and byte-identical with both sides reporting the secured key-material state, and with enforced encryption switched on explicitly on both ends a mismatched passphrase is refused at connect time with `SRT_ECONNREJ`, the caller's rejection reason reading `SRT_REJ_BADSECRET`. The group reads no datagram contents: "no clear payload on the wire" is not asserted here, so take the bullet as a statement about the keying state and the delivered bytes, not about the bytes on the wire

A gate group is deliberately narrow: broad enough that a real regression is
caught, narrow enough that it cannot fail for any reason *except* a regression.
None of them asserts anything this milestone introduces, and none asserts
timing, host load, iteration order or anything else the public contract does
not promise.

---

## 4. The requirement groups

`endtoend` is this milestone's own end-to-end path — it is a requirement
group, not a gate, because it covers work this milestone introduces rather
than inherited behaviour.

| # | Group | What it asserts |
|---|---|---|
| 1 | `minstab-get` | reading the minimum link stability timeout on a fresh unconnected socket succeeds, yields 60 and reports a length of four bytes, instead of being refused with `SRT_EINVOP` |
| 2 | `minstab-set` | setting the minimum link stability timeout on a fresh socket refuses 10 and 59 with `SRT_EINVPARAM`, accepts 60 and accepts 5000 while the peer idle timeout is at its 5000 default but refuses 5001 with `SRT_EINVPARAM`, and reads each accepted value back unchanged |
| 3 | `minstab-pre` | the minimum link stability timeout is still accepted on a socket that is bound but not yet listening, and is refused with `SRT_ECONNSOCK` once `srt_listen` has been called on that same socket |
| 4 | `group-default` | reading the minimum link stability timeout on a group that has no member sockets yet succeeds and yields 60, instead of being refused with `SRT_EINVPARAM` |
| 5 | `group-set` | the group's own setter for the minimum link stability timeout refuses 10 and 59 with `SRT_EINVPARAM` and accepts 60 and 5000, while a memberless group keeps reporting 60 from its getter whatever value was accepted |
| 6 | `group-pruned` | the library exports `srt_create_group` and `srt_connect_group` but resolves `srt_include`, `srt_exclude` and `srt_group_configure` to nothing, so a program that looks all five up at run time finds exactly the first two |
| 7 | `group-types` | the group type enumeration names the undefined, broadcast and backup types as 0, 1 and 2 and its end marker is 3, and `srt_create_group` returns a usable group id for the broadcast type and for the backup type |
| 8 | `rcvloss-bytes` | after messages cross a clean link on which nothing is lost, the interval `byteRcvLoss` reported by `srt_bstats` is non-zero and equal to the interval `byteRecv` of the same sample, while `pktRcvLoss` in that sample is zero, and the same equality holds on a link where the relay removes two packets that are then retransmitted, a sample in which `pktRcvLoss` counts exactly those two and the accumulated `byteRcvLossTotal` is non-zero |
| 9 | `undecrypted` | when the two sides hold different passphrases and enforced encryption is switched off on both, the receiver counts every arrived packet in `pktRcvDrop` and in `pktRcvUndecrypt`, reports zero for the interval `byteRcvDrop` and a non-zero accumulated `byteRcvDropTotal`, and still hands every message to the application as ciphertext that does not match what was sent |
| 10 | `endtoend` | a live stream of 2000 messages is carried over a broadcast socket group whose single member link crosses a clean relay, and arrives byte-identical, in order and with no gaps |

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

Everything below is asserted by one of the groups listed in §3 and §4 and is
not stated by either draft in `srt-spec/` or anywhere in `srt-docs/`. Where a line here contradicts
`srt-docs/`, this section is what the verifier checks.

**Socket options are reached by their numeric position in `SRT_SOCKOPT`, not by
name.** The drivers cast integers, so the enumeration's numbering is part of the
contract and no shipped document gives it. The minimum link stability timeout is
position **58** — the position the previous milestone's stability timeout
occupied, reused under the new name and the new unit. Its neighbours are fixed
by the same requirement: the bind-to-device option is 56, group connection is
57, group type is 59, the packet filter option is 60, and the retransmit
algorithm option is 61. Directly below the ladder, position **55** is the peer
idle timeout — the option the ceiling of the stability timeout is read from —
and a fresh socket reports its 5000 ms default there. Do not insert, remove or
reorder anything at or below 61.

**The minimum link stability timeout is readable.** `srt-docs/` gives its
direction as `W` in the socket-option table; that table is wrong for this
release. `srt_getsockflag` succeeds on a socket and on a group, writes an
`int32_t`, and reports a length of 4.

**It is settable on a plain socket**, although the same table's entity column
lists it as group and group-member only. A socket that is not and never will be
a group member accepts it.

**The refusals are specific and are asserted by error code.** A value below the
60 ms floor is refused with `SRT_EINVPARAM`. A value strictly greater than the
socket's peer idle timeout is refused with `SRT_EINVPARAM`; a value *equal* to
it is accepted, so with the peer idle timeout left at its 5000 ms default, 5000
is a legal setting and 5001 is not. Setting the option on a socket that
`srt_listen` has been called on is refused with `SRT_ECONNSOCK`; on a socket
that is bound but not yet listening it still succeeds. `srt-docs/` documents
the 60 ms floor and the 60 ms default but none of the codes, none of the
ceiling, and not the bound-versus-listening distinction.

**A group with no members reports the default from its getter.** The group
setter validates and accepts, but until a member link exists the getter yields
60 whatever value was stored. This is asserted as-is; do not make the getter
echo the pending value.

**A group id is accepted wherever a socket id is accepted, and what it
reports.** `srt_getsockstate` on a group whose member link has connected
reports `SRTS_CONNECTED` — the same member it would report for a plain socket in
that state. `srt_setsockflag` on a group is accepted for any option, including
ones whose option-table entity column reads socket-only (`SRTO_TSBPDMODE` among
them): a group accepts the set, and members added later inherit the group's
stored value where the option is derived. (The table's entity column is a
legend about the
previous milestone's single sockets, not a restriction on where a group id may
go — `instruction.md` §1 states the general rule this paragraph makes concrete.)

**`SRTO_RCVSYN` and `SRTO_SNDSYN` are exempt from every restriction phase.**
Both are typed `pre`, and the released library accepts a set of either at
**any** state — bound, listening, connecting, connected, and on the socket
`srt_accept` returned — with the new value taking effect for subsequent calls.
A socket returned by `srt_accept` starts with the listener's value, so a
listener opened non-blocking yields a non-blocking accepted socket whose
receive mode you may then switch back. Refusing such a set loses every group
that receives on an accepted socket, gates included.

**The stale name is not a second option.** Several pages of `srt-docs/` still
write `SRTO_GROUPSTABTIMEO` where they mean the minimum link stability timeout.
Both spellings refer to position 58, which is the position every group here
exercises; there is one option, not two. Whether a tree also declares the old
spelling as an alias enumerator is not measured either way: the drivers reach
the option by its number and never name an enumerator, so nothing at this
milestone can observe one.

**Three group functions must not exist.** `srt_include`, `srt_exclude` and
`srt_group_configure` must be absent from the header and unexported from the
library; a program that resolves them at run time must find nothing. Their
removal is not recorded anywhere in `srt-docs/`, which never described them.
`srt_create_group` and `srt_connect_group` are looked up the same way in the
same group and must both resolve.

**The group type enumeration ends at 3.** Undefined is 0, broadcast is 1, backup
is 2, and the end marker is exactly 3 — the two reserved types the previous
milestone declared are gone, and nothing takes their numbers. The group API must
be declared by the public header without the including translation unit defining
any macro first; the verifier compiles against the installed header as the
candidate installs it.

**The verifier configures with `-DENABLE_BONDING=ON`.** That is the only
configuration under which the group API is required to be present, and it is
the exact string §1 of the instructions gives the candidate.

**Interval `byteRcvLoss` reports received bytes.** `srt-docs/` describes it as
the interval form of the accumulated receive-loss byte counter. In this release
the interval field is produced from the receiver's *received* byte count, so on
a link where nothing was lost a `srt_bstats` sample with interval reset has
`byteRcvLoss` equal to `byteRecv` and non-zero while `pktRcvLoss` in the same
sample is zero. The accumulated `byteRcvLossTotal` is unaffected and stays
correct. The equality is asserted tightly; it is the field's definition here.

**Interval `byteRcvDrop` excludes undecrypted bytes while the packet counter and
the accumulated byte counter include them.** After packets arrive that cannot be
decrypted, `pktRcvDrop` counts every one of them and `byteRcvDropTotal` grows by
their bytes, but the interval `byteRcvDrop` in the same sample is zero. The pair
(`pktRcvDrop` non-zero, `byteRcvDrop` zero) is the asserted shape.

**Undecryptable packets are delivered to the application.** `srt-docs/`
states that packets which fail to decrypt are dropped by the too-late-packet-drop
mechanism. They are not. With enforced encryption switched off on both sides and
the passphrases not matching, the receiver counts each arrived packet in
`pktRcvDrop` and in `pktRcvUndecrypt` and *also* returns it from `srt_recvmsg`,
at its full length, holding ciphertext that does not match what was sent. A
group asserts both halves: the counters and the delivery. Do not discard the
packet to make the counters look consistent.

**Three more members of the receiver's trace structure are part of the
interface, and no section above names them.** `SRT_TRACEBSTATS` is read on the
receiver for `byteRcvUndecrypt` and `byteRcvUndecryptTotal`, the interval and
the accumulated byte forms of the counter just described, and for
`pktRcvRetrans`, the interval count of received packets that were
retransmissions. `srt-docs/API/statistics.md` names all three and gives each
one's kind; what no shipped document fixes, and what the groups read, is this:

* `byteRcvUndecrypt` and `byteRcvUndecryptTotal` count the **payload bytes** of
  the packets that could not be decrypted — 1316 bytes for each 1316-byte
  message, header bytes excluded — and they are read on the same convention as
  each other, so on a connection whose only undecryptable packets arrived since
  the counters last reset the interval field and the accumulated field read the
  same number. That number is positive when the key material differs and zero
  when both sides are keyed, and the group asserts both halves. (`srt-docs/`
  describes the accumulated byte counters as counting "payload and all the
  headers"; that convention is the drop counters', not these two, and this
  section is what the verifier checks.)
* `pktRcvRetrans` is read as a **floor**, never as an exact figure: on a
  channel where the sender had to retransmit, the receiver's interval count is
  required to be non-zero, and nothing puts a ceiling over it. Incidental loss
  and the periodic loss report each raise it legitimately, so a counter that
  reports only what the relay was told to drop fails the case.

These are structure *members*, not functions or macros, and the `live` family
that reads them compiles against the installed header at both the previous
release and this one with no `#ifndef` fallback: a `SRT_TRACEBSTATS` carried
forward without any one of the three does not build, which breaks the whole
family — the two requirement groups and the three gate groups that share that
translation unit — and not merely the group that reads it. Declare all three.

**Enforced encryption refuses the connection, and the group switches it on
explicitly.** The inherited encryption group **sets** `SRTO_ENFORCEDENCRYPTION`
to true on both sides before connecting — it does not rely on the option's
default — and asserts that a mismatched passphrase makes `srt_connect` fail
with `SRT_ECONNREJ`, the caller's rejection reason reading
`SRT_REJ_BADSECRET` (10), rather than connecting and delivering nothing. Only
the undecrypted-statistics group above switches enforcement off, and it is the
only place where a mismatch produces a live connection. Whether the option's
own default is true is therefore **not measured** at this milestone; keep it
true, as the documents describe it, but no group will catch you if you do not.

**Lossless recovery still needs its preconditions.** The recovery group and the
end-to-end group switch too-late packet drop off and set a latency budget far
larger than the channel's worst delay before asserting that a dropped burst is
retransmitted and arrives complete. Nothing is drained with a fixed sleep; the
receiver is read until the expected message count arrives or a per-case deadline
expires.

**The negotiated latency is asserted as a number, never as a delay.** The
latency group reads `SRTO_RCVLATENCY` back from a connected receiver and
`SRTO_PEERLATENCY` back from the sender and requires both to report the value
the handshake settled on — the larger of the two sides' requests, as
`srt-docs/` describes it. It does **not** time the TSBPD hold and does not
induce a late packet: no case anywhere in this milestone asserts that a
delivery happened no earlier than some elapsed time, or that a packet arriving
after the budget was counted in `pktRcvDrop` rather than delivered. Both of
those need a wall clock or a timed relay delay to observe, they are load
sensitive on a shared host, and this package removed that shape everywhere. §2
of the instructions still requires the inherited TSBPD hold to go on working;
what this milestone measures is the negotiated number, the completeness and the
order, so a tree that reports the agreed value and delivers the stream intact
passes here whatever its hold does. That is a limit of the measurement, not
permission to drop the hold.

**Nothing here asserts backup-link switching timing.** The minimum link
stability timeout is checked as an accessor — its floor, its ceiling, its
defaults, its state restrictions and its two independent code paths — and not by
timing a failover. A correct implementation is free to consume the value however
it likes, provided a member link that has been idle for less than the configured
interval is not treated as unstable.

**Coverage: documented surface no group here reaches.** `instruction.md` §1
says a group id is accepted wherever a socket id is accepted and names
`srt_sendmsg2`/`srt_recvmsg2`, `srt_bstats` and `srt_epoll_add_usock` among the
places. The groups drive group ids through `srt_getsockstate`,
`srt_sendmsg`/`srt_recv`, `srt_getsockflag`/`srt_setsockflag` and
`srt_connect_group`; the `*msg2` forms, `srt_bstats` and the epoll registration
are **not** exercised with a group id at this milestone — implement the
dispatch for them (the general rule asks for it), but nothing here measures
them. Likewise `srt-docs/` documents `SRT_ECONNLOST` from `srt_connect_group`
when no member could connect, and no case here reaches that path. Both are
recorded so their absence from the verifier is not read as an omission to fix.

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
| `SRTO_GROUPCONNECT` | **required to compile** |
| `SRT_GROUP_TYPE` | **required to compile** |
| `SRT_GTYPE_BACKUP` | **required to compile** |
| `SRT_GTYPE_BROADCAST` | **required to compile** |
| `SRT_GTYPE_E_END` | **required to compile** |
| `SRT_GTYPE_UNDEFINED` | **required to compile** |
| `SRT_SOCKGROUPCONFIG` | **required to compile** |
| `srt_connect_group` | **required to compile** |
| `srt_create_group` | **required to compile** |
| `srt_prepare_endpoint` | **required to compile** |

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
| `SRT_GTYPE_BACKUP` | `2` | fails its group |
| `SRT_GTYPE_BROADCAST` | `1` | fails its group |
| `SRT_GTYPE_E_END` | `3` | fails its group |
| `SRT_GTYPE_UNDEFINED` | `0` | fails its group |

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
