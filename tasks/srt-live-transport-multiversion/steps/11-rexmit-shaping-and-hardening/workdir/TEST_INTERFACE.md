# Test interface — milestone 11 of 11

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
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_BONDING=ON -DENABLE_AEAD_API_PREVIEW=ON -DUSE_ENCLIB=openssl-evp -DENABLE_MAXREXMITBW=ON \
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
15 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.5.7**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.5.7:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `5` |
| `SRT_VERSION_PATCH` | `7` |
| `SRT_VERSION_STRING` | `"1.5.7"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.5.7)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.5.7 is acceptable.

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
- `loss-recovery` — datagrams the relay drops at fixed indexes are retransmitted and the payload arrives complete, in order and byte-identical once too-late packet drop is off and the latency budget is generous, with the receiver's packet loss counter confirming that the loss really happened
- `epoll-leveltrg` — an epoll id holding SRT sockets and a system socket together reports an SRT socket readable until its pending message is consumed and not afterwards, reports the system socket readable when a datagram is waiting on it, and a wait on the id with nothing ready reports no socket in any of its four arrays and fails with SRT_ETIMEOUT
- `endtoend` — a live stream of 3000 messages, each within the single-packet payload limit in force, crosses a clean relay and arrives byte-identical, in order and with no gaps, and the same stream does the same over a relay that adds 15 ms each way. Two checks, both in this one group

A gate group is deliberately narrow: broad enough that a real regression is
caught, narrow enough that it cannot fail for any reason *except* a regression.
None of them asserts anything this milestone introduces, and none asserts
timing, host load, iteration order or anything else the public contract does
not promise. The inherited checkpoint they carry is the **paths this milestone
edits** — the sender's rate accounting and the epoll fill path — plus proof of
life; the wider inherited surface the instruction lists (socket groups,
main/backup, the restriction matrix, packet filtering, TSBPD) must keep
working, but this step's groups do not re-measure it.

---

## 4. The requirement groups

`endtoend` is **not** in the table below. At this milestone the end-to-end
path is the plain live transport the previous milestone already carried, so
it sits in the gate (§3): a failure there means the step is not satisfied,
but it is not a requirement group. Including it here would cover transport
this milestone inherited rather than work it introduces.

| # | Group | What it asserts |
|---|---|---|
| 1 | `optlen-guard` | `srt_setsockflag` of `SRTO_MSS` with `optlen` -1 and of `SRTO_CONGESTION` with the string "live" and `optlen` -1 are both refused with `SRT_EINVPARAM` and leave the option's value unchanged, while the same `SRTO_MSS` set with `optlen` 0 and with `optlen` 4, and the same `SRTO_CONGESTION` set with `optlen` 4, all succeed and are read back as set |
| 2 | `version-readonly` | `srt_setsockflag` of `SRTO_VERSION` with a four-byte value is refused with `SRT_EINVPARAM`, and `srt_getsockflag` of `SRTO_VERSION` on the same socket afterwards still succeeds, writes four bytes and returns the value `srt_getversion` reports |
| 3 | `payloadsize-connected` | with `SRTO_TRANSTYPE` set to `SRTT_FILE` and `SRTO_MSS` set to 1500 on both sides before connect, `srt_getsockflag` of `SRTO_PAYLOADSIZE` reports 1456 on the connected caller and on the accepted socket, while on a file-mode socket that has not connected it reports 0 and on a fresh live-mode socket it reports 1316 |
| 4 | `listen-status-codes` | `srt_listen` is refused with `SRT_ECONNSOCK` on a connected caller and on an accepted socket, with `SRT_ERDVNOSERV` on an unbound socket whose `SRTO_RENDEZVOUS` is true and on a bound one, with `SRT_EUNBOUNDSOCK` on an unbound socket whose `SRTO_RENDEZVOUS` is false, and with `SRT_EINVPARAM` for a backlog of 0 on a bound socket, and a second `srt_listen` on a socket already listening succeeds |
| 5 | `accept-addrlen-guard` | on a non-blocking listener holding one pending connection, `srt_accept` with `addrlen` initialised to 4 is refused with `SRT_EINVPARAM`, and an immediate retry with `addrlen` initialised to `sizeof(sockaddr_in)` succeeds, writes `sizeof(sockaddr_in)` back and returns a socket over which one message crosses in each direction |
| 6 | `epoll-uwait-cap` | with two connected sockets each holding one unread message subscribed for `SRT_EPOLL_IN` in one epoll id, `srt_epoll_uwait` with a one-slot array and `fdsSize` 1 returns 1 and fills only that slot with one of the two ids, then with a four-slot array returns 2 and fills exactly two slots with the two ids, then with a null array and `fdsSize` 0 returns 0, and every reported entry carries `SRT_EPOLL_IN` |
| 7 | `startup-repeat` | with the library already started, a second and a third `srt_startup` each return 0, `srt_getversion` still reports the running version between them, and each of the two balancing `srt_cleanup` calls returns 0 while the library keeps serving the sockets created before them |
| 8 | `null-hook-allowed` | `srt_listen_callback` with a null hook on a bound but not yet listening socket returns 0 and removes a hook installed just before it, so that hook is never invoked when a caller later connects, `srt_connect_callback` with a null hook on a fresh socket returns 0, and installing either hook on a socket that is already listening, connected or accepted is refused with `SRT_ECONNSOCK` |
| 9 | `constants-compile` | with the driver compiled under this milestone's DRV_DEFS, `SRT_KM_S_E_SIZE` is an enumerator of `SRT_KM_STATE` equal to 6, `SYSSOCKET_INVALID` is an integral constant equal to -1, and `SRT_KM_S_BADCRYPTOMODE` is an enumerator equal to 5, each pinned by a static_assert so that a header lacking any one of them fails this family's compile |
| 10 | `maxrexmitbw` | `srt_getsockflag` of `SRTO_MAXREXMITBW` on a fresh socket writes eight bytes holding -1, `srt_setsockflag` of -1, 0 and 1000000 as eight-byte values each succeed and read back as set, a value of -2 and any value given with `optlen` 4 are refused with `SRT_EINVPARAM`, a get into a four-byte buffer is refused with `SRT_EINVPARAM`, and the set still succeeds on a connected socket and is read back there |

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

Everything below is asserted by a group in §2 and is not stated by either draft
in `srt-spec/` or anywhere in `srt-docs/`. Where a line here contradicts
`srt-docs/`, this section is what the verifier checks.

**Two preview build options are on here, and the drivers are compiled with
both.** `CMAKE_EXTRA` passes `-DENABLE_AEAD_API_PREVIEW=ON
-DENABLE_MAXREXMITBW=ON`; `DRV_DEFS` passes the same two defines to every
driver. Both take effect through `add_definitions`, which reaches neither
`srt.pc` nor any generated header, so a program that wants the declarations
they unlock must pass the `-D` itself; do not leak either feature into an
unguarded header. The AEAD option was already in force when the previous
milestone was built; the retransmission-ceiling option is new to the build
here. With both defined, `SRTO_E_SIZE` is **64**: `SRTO_CRYPTOMODE` is 62
under `ENABLE_AEAD_API_PREVIEW` and `SRTO_MAXREXMITBW` is **63** under
`ENABLE_MAXREXMITBW`, each inside its own `#ifdef` in `SRT_SOCKOPT`, and
`SRTO_E_SIZE` follows. No group asserts `SRTO_E_SIZE` — its value is a
function of which options the build admits, and pinning it would check the
configuration rather than the behaviour — but nothing at or below 62 may
move, and `SRTO_CRYPTOMODE` must stay at 62 with `SRTO_MAXREXMITBW` above it
rather than the other way round.

**`SRTO_MAXREXMITBW` is 63, an `int64_t`, and its getter refuses a short
buffer.** `srt-docs/` gives the type, the default of -1, the range `-1..` and
the meanings of -1, 0 and a positive value correctly; follow that as written.
What it does not say: the setter refuses any value below -1 with
`SRT_EINVPARAM`; the setter refuses any **non-zero** `optlen` that is not 8
with `SRT_EINVPARAM` (a four-byte `int` is not silently widened, while
`optlen == 0` means "trust the pointer's width" here as it does for every
other numeric option -- see the paragraph below); and the getter
refuses an `optlen` below 8 with `SRT_EINVPARAM` rather than truncating. On a
fresh socket the getter writes 8 bytes holding -1 and reports 8 as the written
length. The option is `post`-restricted: a set on a connected socket succeeds
and the new value reads back on that socket. The declaration must sit inside
`#ifdef ENABLE_MAXREXMITBW` in the public header. No group asserts anything
about the rate actually achieved on a lossy link; the group asserts the
option's contract only.

**A negative `optlen` is refused by every setter, and a zero `optlen` is
not.** `srt_setsockopt` and `srt_setsockflag` both refuse `optlen < 0` with
`SRT_EINVPARAM` before dispatching on the option, for every option. The
refusal leaves the option's stored value unchanged. `optlen == 0` is **not**
refused: for a numeric option it means "trust the pointer's width", so
`srt_setsockflag(s, SRTO_MSS, &v, 0)` with `int v = 1400` succeeds and
`SRTO_MSS` reads back 1400; and a non-zero `optlen` that is not `sizeof(int)`
is still refused for a numeric option as before. For a string option the
previous milestone treated -1 as "NUL-terminated" and this one refuses it:
`srt_setsockflag(s, SRTO_CONGESTION, "live", -1)` fails with `SRT_EINVPARAM`
and `srt_setsockflag(s, SRTO_CONGESTION, "live", 4)` succeeds. The getters
are untouched: `srt_getsockflag` with `*optlen == -1` on `SRTO_MSS` still
succeeds and writes 4, and no group asserts on it. `srt-docs/` states none of
this for either accessor. `SRTO_CONGESTION` is readable despite its `Dir: W`
row: the getter copies the string, NUL-terminates it and reports the string's
length without the NUL as the written length, so the group reads it back
through a 16-byte buffer and expects "live" with length 4 after the refused
set and "file" with length 4 after the accepted one.

**`SRTO_VERSION` has no setter, and a set is refused as a bad parameter.**
`srt-docs/` marks the option `R` and lists it under "not supported" for
groups; neither says what a set does. `srt_setsockflag(s, SRTO_VERSION, &v, 4)`
returns `SRT_ERROR` with `SRT_EINVPARAM` (5003) -- not `SRT_EINVOP`, not
`SRT_EBOUNDSOCK`. The getter still writes 4 bytes holding the value
`srt_getversion()` returns on an unconnected socket, which at this milestone
is `0x010507`. The group compares the two calls' results to each other, not to
a literal.

**`SRTO_PAYLOADSIZE` is readable, and on a connected file-mode socket it
reports the per-packet maximum, not the configured zero.** `srt-docs/` marks
the option `W` and says the file-mode default is 0. The getter exists and
writes 4 bytes at both milestones; what changes here is what it reports. On a
socket that is not connected it reports the configured value: 0 for a
file-mode socket, 1316 for a live-mode socket. On a **connected** socket whose
configured value is 0 it reports the largest payload one packet carries at the
negotiated segment size, which is `MSS - 44` over IPv4 -- **1456** at
`SRTO_MSS` 1500, on both the caller and the accepted socket. On a connected
socket whose configured value is non-zero it reports that configured value.
The group pins `SRTO_MSS` to 1500 on both sides before connect and asserts
1456; `srt-docs/` itself gives 1456 as the live-mode ceiling, and that number
is the same arithmetic.

**`srt_listen` tests rendezvous before it tests bind state, and dispatches by
socket state.** `srt-docs/` lists the six error codes `srt_listen` can return
but not their precedence or which state yields which. The order is: backlog
`<= 0` yields `SRT_EINVPARAM`; an invalid id yields `SRT_EINVSOCK`; then, on a
valid socket, `SRTO_RENDEZVOUS` true yields `SRT_ERDVNOSERV` (5007)
**regardless of bind state**; then the state: never bound yields
`SRT_EUNBOUNDSOCK` (5005); bound yields success and listening; already
listening yields success and updates the backlog; connecting or connected --
which includes a caller after `srt_connect` returned and a socket
`srt_accept` returned -- yields `SRT_ECONNSOCK` (5002); broken, closing or
closed yields `SRT_ESCLOSED` (1005). The previous milestone tested bind state
first and mapped every non-bound state to `SRT_EUNBOUNDSOCK`, so its tree
reports 5005 where this one reports 5007 and 5002. The group asserts 5002 on a
connected caller and on an accepted socket, 5007 on an unbound and on a bound
rendezvous socket, 5005 on an unbound plain socket, 5003 for backlog 0, and 0
for a repeated listen. It does not reach the broken state and does not assert
1005.

**`SRTO_RCVSYN` and `SRTO_SNDSYN` are exempt from every restriction phase.**
Both are typed `pre`, and the released library accepts a set of either at
**any** socket state -- bound, listening, connecting, connected, and on the
socket `srt_accept` returned -- with the new value taking effect for subsequent
calls. A socket returned by `srt_accept` starts with the listener's value, so a
non-blocking listener yields a non-blocking accepted socket whose receive mode
you may then switch back; this group's own non-blocking accept path depends on
exactly that. Refusing the set for these two loses every group that receives on
an accepted socket, gates included.

**A short address buffer refuses `srt_accept` and leaves the connection
queued.** `srt-docs/` lists `SRT_EINVPARAM` for `srt_accept` only for a null
`addrlen` with a non-null `addr`. With a non-blocking listener (`SRTO_RCVSYN`
false on the listener) holding one pending connection, `srt_accept(lsn, &sa,
&len)` with `len == 4` returns `SRT_INVALID_SOCK` with `SRT_EINVPARAM`
(5003) **and does not dequeue the connection**: an immediate retry with `len
== sizeof(sockaddr_in)` returns the accepted socket, writes 16 back into
`len`, and that socket carries a message each way. The previous milestone
dequeued first and checked the length afterwards, so its retry reports
`SRT_EASYNCRCV` (6002); a tree that does that fails the group. The comparison
is against the peer address's own size, so for an IPv4 listener the threshold
is 16; the group uses IPv4 only.

**`srt_epoll_uwait` never reports more than `fdsSize`, and the shipped
`fdsSize + 1` contract is not this milestone's.** `srt-docs/` says the
function returns `fdsSize + 1` when the array was too small. That describes
the previous milestone's tree, which counted every ready socket and stopped
writing at the array end, so two ready sockets with `fdsSize == 1` returned 2
and with `fdsSize == 0` returned 1. At this milestone the return value is the
number of entries **written**, bounded by `fdsSize`: two ready sockets return
2 with a four-slot array, 1 with a one-slot array, and **0** with `fdsSize ==
0` and a null array even though two sockets are ready. Entries beyond the
returned count are never touched. The entries that are written are correct:
each carries an `fd` that is one of the ready sockets and `events` with
`SRT_EPOLL_IN` set, and the two entries of the four-slot call are the two
different ids. The group runs the three calls in the order 1, 4, 0 on the same
epoll id without consuming any message in between, so each call sees both
sockets still ready; level-triggered subscriptions do not clear on report. A
`msTimeOut` of 500 is generous; the ready calls return at once. The
`SRT_EINVPARAM` rules `srt-docs/` gives for a negative `fdsSize` and a null
array with a positive `fdsSize` are unchanged, and an epoll id with **no**
subscriptions still fails with `SRT_EPOLLEMPTY` (5014) at both milestones --
the group subscribes two sockets before it calls.

**A second `srt_startup` returns 0, and so does a third.** `srt-docs/` says
`srt_startup` returns 0 for "successfully run, or already started" and 1 for
"first startup, but the GC thread is already running". The previous
milestone's tree returned 1 on every call after the first; this one returns 0
on the second and every later call while the library is running, and 1 only
in the first-call-with-GC-already-running case, which the group never
reaches. Each balancing `srt_cleanup` returns 0. The instance count is
balanced: after two extra startups and two extra cleanups the library is still
running, and a socket created before the extra cleanups still serves a
message afterwards. The driver's own harness calls `srt_startup` once before
any case runs and `srt_cleanup` once after; the group's calls are in addition
to those, inside one forked case.

**A null hook is accepted and clears the hook; a hook on a busy socket is
refused with `SRT_ECONNSOCK`.** `srt-docs/` says `hook_fn` may be "NULL to
remove the callback" for both installers, and lists `SRT_ECONNSOCK` for
`srt_listen_callback` as "can't be modified in a connected socket". The
previous milestone's tree refused a null hook with `SRT_EINVPARAM` at the C
API and installed a non-null hook on any socket in any state; this one does
what the document says. Precisely: `srt_listen_callback(s, NULL, NULL)` and
`srt_connect_callback(s, NULL, NULL)` return 0 on a socket that is not
listening, not connecting, not connected and not broken; the null install
removes a hook installed before it, so a listener whose hook was installed
and then removed before `srt_listen` accepts a caller without invoking the
removed hook; and either installer on a socket that **is** listening,
connecting, connected, accepted or broken returns `SRT_ERROR` with
`SRT_ECONNSOCK` (5002), null hook or not. The refusal on a listening socket
is the part `srt-docs/` does not say: it lists the connected case only. The
group installs the listen hook on the bound socket before `srt_listen`, never
after.

**`SRT_KM_S_E_SIZE` and `SYSSOCKET_INVALID` exist, unguarded.** No shipped
document names either. `SRT_KM_STATE` gains a terminal member
`SRT_KM_S_E_SIZE` whose value is **6**: the enumeration is `UNSECURED` 0,
`SECURING` 1, `SECURED` 2, `NOSECRET` 3, `BADSECRET` 4, `BADCRYPTOMODE` 5,
`E_SIZE` 6, with no `#ifdef` around any member. That last point is a change
from the previous milestone, where `SRT_KM_S_BADCRYPTOMODE` sat inside
`#ifdef ENABLE_AEAD_API_PREVIEW`: here it is declared **unconditionally** and
keeps the value 5, and `srt-docs/` still documents it as 5. `SYSSOCKET_INVALID`
is `static const int SYSSOCKET_INVALID = -1;` on POSIX, declared next to the
`SYSSOCKET` typedef at namespace scope in `srt.h`, usable in a
`static_assert`. The `surface` driver pins all three with `static_assert` and
is compiled with `DRV_DEFS`, so a header that guards `SRT_KM_S_BADCRYPTOMODE`
still compiles there; a header that omits `SRT_KM_S_E_SIZE` or
`SYSSOCKET_INVALID` does not, and one compile error fails both `surface`
groups. That is why they share a family and nothing else does.

**The inherited guards read the same fields as before.** `loss-recovery`
reads `pktRcvLoss` from `srt_bstats` with `clear` set, asserting it non-zero
after the scripted drops, exactly as the previous milestone's loss guard did;
`epoll-leveltrg` uses `srt_epoll_wait` with the four-array form and a system
UDP socket registered with `srt_epoll_add_ssock`, as the epoll guard has since
the first milestone. Neither reads any field or calls any function this
milestone adds.

**Documented surface no group here reaches.** Recorded so its absence is not
read as an omission to fix: the ceiling option's documented entity set is
`socket, group and group default` (`srt-docs/API/API-socket-options.md`), and
`g12` exercises sockets only — a tree that never reaches the group-default
configuration loses nothing here; the installed header's version macros are
required but read by no translation unit, so a tree that never bumps
`SRT_VERSION` passes every group (the version's canary is the *adjacent*
milestone, whose `srt_clock_type` reads a different identifier); and the
`SRTO_PAYLOADSIZE` getter's third state — a connected socket with a non-zero
configured payload size reports that value back — is stated and unasserted (the
two cases here stay inside the zero-configured branch). Implement them as the
documents describe; nothing here measures them either way.

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
| `SRTO_MAXREXMITBW` | **required to compile** |
| `SRT_KM_S_E_SIZE` | **required to compile** |
| `SYSSOCKET_INVALID` | **required to compile** |

### Constants whose numeric value the verifier pins

A verifier driver in this step depends on the numeric value of each
constant below, so **these values are part of the interface**. Some
are restated in a driver and pinned with a `static_assert` against
the `<srt/srt.h>` you install: number one of those differently and
the verifier does not build at all. The rest are read at run time
and compared against the value below: number one of those
differently and the group that reads it fails, with the others
unaffected.

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
| `SRTO_MAXREXMITBW` | `63` | fails its group |
| `SRT_KM_S_BADCRYPTOMODE` | `5` | fails to compile |
| `SRT_KM_S_BADSECRET` | `4` | fails to compile |
| `SRT_KM_S_E_SIZE` | `6` | fails to compile |
| `SRT_KM_S_NOSECRET` | `3` | fails to compile |
| `SRT_KM_S_SECURED` | `2` | fails to compile |
| `SRT_KM_S_SECURING` | `1` | fails to compile |
| `SRT_KM_S_UNSECURED` | `0` | fails to compile |
| `SYSSOCKET_INVALID` | `-1` | fails to compile |

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
