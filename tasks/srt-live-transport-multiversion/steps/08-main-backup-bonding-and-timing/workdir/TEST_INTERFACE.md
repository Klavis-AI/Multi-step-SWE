# Test interface — milestone 8 of 11

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
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_EXPERIMENTAL_BONDING=ON \
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
13 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.4.4**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.4.4:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `4` |
| `SRT_VERSION_PATCH` | `4` |
| `SRT_VERSION_STRING` | `"1.4.4"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.4.4)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.4.4 is acceptable.

No verifier translation unit at this milestone compiles conditionally
on `SRT_VERSION_VALUE`, so nothing here turns on the version alone.
The macros are part of the installed interface either way, and the
header must report this milestone's version. Nothing in this step's
groups reads the macros back, either — the version is an inherited,
already-installed obligation rather than behaviour this step covers, so
install it correctly and expect no group to check it.


---

## 2. How the work is checked

There are 4 **gate** groups (§3) and 9 **requirement** groups
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
- `muxer-share` — two sockets binding the same explicit loopback address and port with matching settings share that binding rather than conflicting, `srt_getsockname` on each reports that same address and port, and a caller that reaches the shared endpoint through a relay is accepted on the socket listening there with its stream arriving complete, in order and byte-identical
- `recovery` — datagrams dropped by the relay at fixed indexes are retransmitted and the payload arrives complete and in order once too-late packet drop is off and the latency budget is generous, with the receiver's loss counter showing the loss really happened
- `group-backup` — a main/backup socket group whose two member links are prepared with distinct loopback source addresses connects to one group-enabled listener and carries a live stream that arrives complete, in order and byte-identical, `srt_groupof` on each member returns the group id, and `srt_group_data` reports both members as `SRTS_CONNECTED` with none of them `SRT_GST_BROKEN` and at least one of them `SRT_GST_RUNNING`

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
| 1 | `algo-default` | `SRTO_RETRANSMITALGO` reads 1 on a freshly created socket and still reads 1 on a socket that has been bound, a set of 0 is accepted before connect and reads back as 0, and a set of 1 reads back as 1 |
| 2 | `algo-range` | a set of `SRTO_RETRANSMITALGO` to 2, to 3 or to -1 returns -1 with `SRT_EINVPARAM` and leaves the previously stored value unchanged, while 0 and 1 are both accepted |
| 3 | `algo-transtype` | setting `SRTO_TRANSTYPE` to `SRTT_LIVE` leaves `SRTO_RETRANSMITALGO` reading 1 even when it was explicitly set to 0 beforehand, and setting `SRTO_TRANSTYPE` to `SRTT_FILE` leaves it reading 0 even when it was explicitly set to 1 beforehand |
| 4 | `clock-type` | the installed shared library exports `srt_clock_type` with C linkage and calling it returns exactly 1, `SRT_SYNC_CLOCK_GETTIME_MONOTONIC`, the identifier for the monotonic clock this build selects for itself on a platform whose `clock_gettime` supports it, and not the 4, `SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY`, that the same call reported before this milestone |
| 5 | `conflict-errcode` | `SRT_EBINDCONFLICT` has the numeric value 5015, and `srt_strerror` renders it as a non-empty message that differs both from the message it renders for an unassigned minor code of the same major and from the message it renders for `SRT_EDUPLISTEN` |
| 6 | `wildcard-conflict` | when one socket has bound the IPv4 wildcard address on a port, a second socket's bind of an explicit loopback address on that same port returns -1 with `SRT_EBINDCONFLICT`, the same refusal occurs in the reverse order, and in both cases the socket that bound first keeps its own bound address and can still be listened on |
| 7 | `separate-muxers` | two sockets binding two different explicit loopback addresses on one and the same port both succeed, and `srt_getsockname` on each reports the address that socket itself passed to `srt_bind` together with that port |
| 8 | `settings-conflict` | a second bind of the identical address and port returns -1 with `SRT_EBINDCONFLICT` rather than a system-level bind failure when `SRTO_REUSEADDR` is false on the two sockets, and equally when the two sockets differ in any one UDP channel setting -- a differing `SRTO_UDP_RCVBUF` and a differing `SRTO_IPTTL` are each asserted -- while two sockets whose settings all match still share the binding and both succeed |
| 9 | `endtoend` | a live stream of 4000 messages crosses a clean relay to a listener bound to an explicit loopback address and arrives byte-identical, in order and with no gaps, and a second socket's attempt to bind the wildcard address on that listener's own port while the stream is in flight is refused with `SRT_EBINDCONFLICT` without disturbing the stream |

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

**Where this section and `srt-docs/` disagree, this section is right.** It is
generated from the behaviour the verifier checks, so a disagreement is a
correction rather than an alternative, and the paragraphs above name the
passages they override wherever they can.

**The group declarations are gated on the configure symbol, and the drivers
compile against that symbol.** The verifier configures `/app` with
`-DENABLE_EXPERIMENTAL_BONDING=ON` and then compiles its own programs against
your installed `<srt/srt.h>` with `-DENABLE_EXPERIMENTAL_BONDING=1`. So the
group entities — `SRT_GROUP_TYPE`, `SRT_MEMBERSTATUS`, `SRT_SOCKGROUPDATA`,
`SRT_SOCKGROUPCONFIG`, `srt_create_group`, `srt_groupof`, `srt_group_data`,
`srt_prepare_endpoint`, `srt_connect_group` — must be visible in that
translation unit. Declaring them unconditionally satisfies this; so does
guarding them with `#if ENABLE_EXPERIMENTAL_BONDING` and defining the symbol to
`0` in the header when it is undefined. Guarding them with any *other* spelling
does not: the driver would not see them and the whole `live` family would fail
to compile. No document in your reading list names a configure option or a
preprocessor symbol, which is why it is named here.

**The group options are required at this milestone even though the option table
dates them later.** `srt-docs/API/API-socket-options.md` puts `1.5.0` in the
"Since" column for `SRTO_GROUPCONNECT`, `SRTO_GROUPSTABTIMEO` and
`SRTO_GROUPTYPE`. That column is wrong for the tree you were handed: those
three options, and the whole group API around them, are part of what you
inherit and one group connects through them here. Treat the "Since" column as
advisory throughout; the option enumeration in the header you ship is the
authority for what exists.

**An out-of-range option *value* is refused with `SRT_EINVPARAM`.**
`srt_setsockopt`'s own error table lists `SRT_EINVPARAM` only for an invalid
option *identifier*, and `SRTO_RETRANSMITALGO`'s row gives the range `[0, 1]`
without saying what happens outside it. A group asserts the pairing: a set of
`2`, `3` or `-1` returns `-1` and `srt_getlasterror` reports `SRT_EINVPARAM`.
The same group asserts that a refused set is inert — the value a later
`srt_getsockflag` returns is the one that was stored before the refused call,
not the rejected argument and not the default.

**`srt_clock_type` has to be exported from the shared library, and it has to
report the monotonic clock.** The group resolves the function with `dlsym`
against your installed `libsrt.so` rather than calling it, so that a tree which
never declared it loses this one group instead of failing to compile a whole
family. A definition that is `static`, that exists only as an inline function in
the header, or that a visibility setting hides, compiles perfectly and still
fails: it must be an exported symbol with C linkage. The group then asserts the
exact value **1**, `SRT_SYNC_CLOCK_GETTIME_MONOTONIC` — not a range, and not the
`4`, `SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY`, that the same call reported before
this milestone. This is a build-configuration change, not a code edit. The
internal clock is selected at configure time by the option that enables
`clock_gettime` with `CLOCK_MONOTONIC`, and this milestone is where that
option's default stops being written into the build system as a fixed `OFF` and
starts being decided by probing the platform: on a platform whose
`clock_gettime` supports `CLOCK_MONOTONIC` the option comes on by itself, with
nothing passed on the command line. The verifier configures exactly as §1 shows
and passes nothing clock-related, so derive the reported identifier from the
build configuration you actually produced rather than returning a constant — the
adjacent milestone asserts a *different* identifier from this same call, and a
hardcoded answer is wrong at one end of the chain or the other. That option's
literal spelling is deliberately not given here, and its absence is not an
oversight: it is a line in the `CMakeLists.txt` of the tree you were handed,
finding it is part of the change, and no group asserts the option's name or its
value — the only thing pinned is what the installed library reports.

**Correction: the shipped documentation names the opposite default, and it is
out of date.** `srt-docs/API/API-functions.md` is the only document in your
reading list that mentions the internal clock at all — no build-options document
is shipped — and it gives you half of what you need and contradicts the other
half. Its `srt_clock_type` section carries the full identifier table, so the
mapping `1` = `SRT_SYNC_CLOCK_GETTIME_MONOTONIC`, `4` =
`SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY` is citable and is not repeated here. The
passage a few pages earlier, under the discussion of `srctime`, is not: it names
the two relevant build flags with spellings that exist nowhere
(`ENABLE_MONOTONIC`, `ENABLE_STDXXX_SYNC`) and then states that "the default is
currently to use the system clock as the internal SRT clock". That sentence
describes the milestone *before* this one and was never updated when the default
changed; it is byte-identical to the copy the previous milestone shipped. It is
reproduced unedited because a shipped document is never altered, so read it as
history, and read this paragraph as the correction: under the configuration in
§1, `srt_clock_type` returns 1 and nothing else. Nothing here observes the
explicit override, either: the verifier passes no clock-related flag, so
whether the option can still be forced on or off by hand is untested — keep it
working, as the instruction asks, but no group measures it.

**One more stale table row, in the same direction.** The Restrict column of
`srt-docs/API/API-socket-options.md` gives `SRTO_TRANSTYPE` as `pre`, which the
legend defines as still settable on a merely-bound socket — but the library
treats this option as pre-**bind**: a set after `srt_bind` has succeeded is
refused. No group here sets the transmission type on a bound socket, so either
implementation passes this milestone; the note exists so the table is not what
you port forward.

**`SRTO_RCVSYN` and `SRTO_SNDSYN` may be set at any state, whatever their rows
say.** Both are typed `pre`, and no restriction phase reaches them: the
released library accepts a set of either at **any** socket state — bound,
listening, connecting, connected, and on the socket `srt_accept` returned — and
the new value takes effect for subsequent calls. A socket returned by
`srt_accept` starts with the listener's value, so a listener opened
non-blocking yields a non-blocking accepted socket whose receive mode you may
then switch back. Refusing such a set loses every group that receives on an
accepted socket, gates included.

**The bind-conflict message is never compared against fixed text.** The group
that checks `srt_strerror(SRT_EBINDCONFLICT, 0)` compares it against
`srt_strerror` of an unassigned minor code of the same major, and against
`srt_strerror(SRT_EDUPLISTEN, 0)`, and requires all three to differ and the
first to be non-empty. Word the message however you like. What it may not do is
render identically to a code you have not defined: if your `srt_strerror` falls
back to one string for anything it does not recognise, `SRT_EBINDCONFLICT` must
not be one of those things.

**Two generic sentences in this document are inexact for this step; here is
the correction.** The first is the preamble to the constants table below: "nothing
else in your reading list states a numeric value for any of these names." That
holds for every name in the table except the two clock identifiers —
`srt-docs/API/API-functions.md` does tabulate `1` and `4`, which is why the
paragraph above tells you the mapping is citable from there. The table repeats
them so that what the verifier asserts can be read in one place, not because the
shipped documentation is silent about them. The second is in §6: "both endpoints
of every group are in one process." For this step that describes `endtoend`
and the traffic-carrying gate groups only. No other in-scope group opens a
connection at all — the four `rexmit` groups need no connection (three of them
create and use a socket; the fourth only resolves a symbol and calls it), and
the four `bind` groups create and bind sockets but
never connect one. `scored.txt` says so in its own header, and it is deliberate:
it is what lets the bind-conflict and retransmission work be checked without any
of it depending on a transfer completing.

**`MN_BUSYPORT` is the enumerator that carries the new minor code, and a
driver compiles against it.** The new error is composed from the "operation not
supported" major and a minor code of its own. That minor code's enumerator is
spelled `MN_BUSYPORT`, it belongs to the same enumeration as the minor codes
already defined beside it (`CodeMinor`, the one holding `MN_BUSY`, `MN_XSIZE`,
`MN_EEMPTY`), the constants table below pins its value, and it must be
reachable from the `<srt/srt.h>` you install. A driver names it directly with
no fallback: an implementation that arrives at 5015 by another route — an
integer literal, a differently spelled enumerator, a minor code kept in a
private header — does not fail one assertion, it fails to compile the whole
`bind` family and loses every group in it.

**Which settings must be identical for two sockets to share one binding.**
`srt-docs/API/API-functions.md` gives the three conditions under which
intersecting bindings may share a multiplexer, and the third of them is that
"the UDP-specific settings must be identical" — without saying which settings
those are. They are the ones that configure the UDP channel itself rather than
the SRT connection carried over it: `SRTO_REUSEADDR`, `SRTO_UDP_SNDBUF`,
`SRTO_UDP_RCVBUF`, `SRTO_IPTTL`, `SRTO_IPTOS`, `SRTO_IPV6ONLY`, and on Linux
`SRTO_BINDTODEVICE`. Any one of them differing between an existing binding and
an intersecting request makes the request a conflict rather than a share. A
group asserts two of them — two sockets differing only in `SRTO_UDP_RCVBUF`, and
two differing only in `SRTO_IPTTL` — and no group makes a share-or-conflict
claim about any option outside this list. That leaves four of the seven
(`SRTO_UDP_SNDBUF`, `SRTO_IPTOS`, `SRTO_IPV6ONLY`, `SRTO_BINDTODEVICE`) stated
as sharing conditions and exercised by no group here: compare over the whole
list, so that a difference in any of them is a conflict. (Options this step exercises for other
reasons, such as `SRTO_RETRANSMITALGO` or `SRTO_TRANSTYPE`, are not UDP channel
settings and have no bearing on whether a binding is shared.)

**`srt_getsockname` reports the address the socket itself passed to
`srt_bind`.** `srt-docs/` says it extracts "the address to which the socket was
bound", which is unambiguous only until sockets start sharing a UDP endpoint. A
group binds two sockets to two different explicit loopback addresses on one
port and reads back both; each must report its own address and that port, not
the address of whichever underlying endpoint your implementation chose to carry
it. The same group requires this before any connection exists on either socket.

**A conflicting bind is refused against a live endpoint too.** The end-to-end
check attempts the conflicting bind while a stream is crossing the listener it
conflicts with. The refusal is expected to be the same `SRT_EBINDCONFLICT`, and
the stream is expected to be unaffected — the attempt must not close, rebind or
otherwise disturb the multiplexer it was refused against.

**What the backup-group check does and does not assert.** It asserts that the
group connects, that the stream arrives complete and in order, that
`srt_groupof` on each member returns the group, and that `srt_group_data`
reports both members `SRTS_CONNECTED` with none `SRT_GST_BROKEN` and at least
one `SRT_GST_RUNNING`. It does **not** assert which member is running, does not
assert that the other is idle, and does not break a link and time the switch:
`srt-docs/` has the group silencing a running member back to idle at its own
discretion, and keeps the sub-states of `SRT_GST_RUNNING` inside the sending
algorithm rather than in the status the API reports. Any activation order that
keeps the stream intact passes.

**Loss recovery needs too-late drop switched off.** The verifier's channel drops
datagrams at fixed indexes. The group that requires the payload to arrive
complete sets `SRTO_TLPKTDROP=0` and a generous `SRTO_LATENCY` first and reads
the receiver's loss counter from `srt_bstats` to confirm the loss really
happened; the end-to-end check runs over a clean channel and leaves the defaults
alone. Both options are documented and both defaults are exercised, so neither
is being assumed. Note that the retransmission default this milestone changes
sits underneath that group: the efficient algorithm may retransmit a given loss
less often, so recovery is asserted on the payload arriving, never on a
retransmission count or a deadline.

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
| `MN_BUSYPORT` | **required to compile** |
| `SRT_EBINDCONFLICT` | **required to compile** |

### Constants whose numeric value the verifier pins

A verifier driver in this step restates the constants below and pins
each restatement with a `static_assert` against the `<srt/srt.h>` you
install, so **these values are part of the interface** and an
implementation that numbers them differently fails to compile rather
than failing a behavioural assertion. They are listed here because a
verifier may only assert a numeric value that a document fixes, and
this is the document that fixes them: nothing else in your reading
list states a numeric value for any of these names.

The pins exist because a restated constant that drifts would make the
verifier silently address a *different* option and report a pass for
the wrong reason. Every other constant is yours to number — with one
exception: a constant whose *position* a section above fixes as part of
the contract, because a group reaches it by number rather than by name,
is fixed by that section and not by this table.

| constant | value |
| --- | --- |
| `MN_BUSYPORT` | `15` |
| `SRTO_RETRANSMITALGO` | `61` |
| `SRTO_TRANSTYPE` | `50` |
| `SRTS_INIT` | `1` |
| `SRTS_LISTENING` | `3` |
| `SRTS_OPENED` | `2` |
| `SRT_EBINDCONFLICT` | `5015` |
| `SRT_EDUPLISTEN` | `5011` |
| `SRT_ESOCKFAIL` | `1003` |
| `SRT_SYNC_CLOCK_GETTIME_MONOTONIC` | `1` |
| `SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY` | `4` |

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
