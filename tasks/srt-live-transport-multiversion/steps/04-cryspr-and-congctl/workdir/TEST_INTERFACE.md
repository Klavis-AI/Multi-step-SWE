# Test interface — milestone 4 of 11

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
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF  \
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
7 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.3.3**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.3.3:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `3` |
| `SRT_VERSION_PATCH` | `3` |
| `SRT_VERSION_STRING` | `"1.3.3"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.3.3)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.3.3 is acceptable.

This is not bookkeeping at this milestone. The verifier compiles
`fam_api.cpp` and `fam_cong.cpp` conditionally on `SRT_VERSION_VALUE`: on a tree
reporting 1.3.3 or later the new options' numeric pins are enforced by
`static_assert` at compile time, and on a tree that leaves the version where
the previous milestone left it those pins are skipped and the same positions
are checked **at run time** instead. Bumping the version is neither required
nor measured, and nothing about the below-expected branch is a trap — the
section "What a wrong number does depends on the version your header
reports" below spells out exactly what stays checked on each branch. Put the
options at the numbers this document gives and both branches pass.


---

## 2. How the work is checked

There are 7 **gate** groups (§3) and 8 **requirement** groups
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
- `congestion` — the congestion control option reads "live" on a fresh socket, accepts "live" and "file" with the name readable back, refuses an unknown name with SRT_EINVPARAM, and answers to both its current and its deprecated spelling because they are one option
- `transfer` — live messages sent over an intact channel arrive complete, in order and byte-identical on the receiving side
- `recovery` — messages whose datagrams the relay drops at fixed indexes are retransmitted and arrive complete and in order once too-late packet drop is off and the latency budget is generous
- `encryption` — a matching passphrase and explicit key length on both sides carry the payload intact while no clear payload appears in the datagrams on the wire, and both sides report the documented secured key material state
- `optmatrix` — the inherited socket-option restriction matrix still holds across this milestone's changes to the option enum: a connected socket refuses a set of the inherited pre-connect options with SRT_ECONNSOCK on both the calling and the accepted socket, an undefined option id is refused with SRT_EINVOP for a get and SRT_EINVPARAM for a set, and the declared payload size keeps its one-MTU default and ceiling and its previous value after a refused set
- `endtoend` — a live stream of 4000 messages crosses a clean relay and arrives byte-identical, in order, with no gaps

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
| 1 | `peeridletimeo` | the peer idle timeout option reads 5000 on a fresh socket, accepts a set before connect and reads that value back, refuses a set on a connected socket with SRT_ECONNSOCK, and is derived from the listener by the accepted socket |
| 2 | `ipv6only` | the IPv6-only option reads -1 on a fresh socket, accepts a set of -1, 0 or 1 with each value readable back, still accepts a set once the socket is bound, and refuses a set only on a connected socket, with SRT_ECONNSOCK |
| 3 | `epoll-lwfds` | srt_epoll_wait with an infinite timeout and only the writable-system-socket array supplied returns the one registered ready system socket in that array instead of failing with SRT_EINVPARAM, while the same call with every array NULL still fails with SRT_EINVPARAM |
| 4 | `header-macros` | a translation unit whose only include is <srt/srt.h> compiles with NO_BUSY_WAITING undefined, while the header's own macros such as SRT_API and SRT_LOGFA_HAICRYPT are still defined in that same translation unit |
| 5 | `listen-install` | installing an accept hook on a listening socket returns 0 while a NULL hook pointer returns -1 with SRT_EINVPARAM, and the installed hook then fires exactly once per incoming connection with the caller's address, the caller's stream id, handshake version 5 and the opaque pointer supplied at install |
| 6 | `listen-reject` | an accept hook that returns -1 makes the caller's srt_connect fail with SRT_ECONNREJ and leaves srt_accept with nothing to return, and the same listener still accepts a later connection whose hook returns 0 |
| 7 | `listen-retval` | an accept hook returning 0, 1 or -2 accepts the connection and srt_accept yields the socket, so only the exact value -1 rejects |
| 8 | `bstats-bw` | srt_bstats on a freshly connected socket whose bandwidth cap is at its default reports mbpsMaxBW above 500 and usPktSndPeriod below 100 microseconds, reports the same pair of bounds after the cap is set to 0, and tracks an explicitly set positive cap of 10 MB/s as mbpsMaxBW near 80 with usPktSndPeriod above 100 |

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

**Socket options are reached by their numeric position in `SRT_SOCKOPT`, not
by name.** The `api` family's programs cast an integer to `SRT_SOCKOPT` rather
than spelling the new constants, so that a tree which is missing one new name
still compiles and the other option groups still pass. That makes the
numbering part of the contract, and no shipped document gives it. Congestion
control is position **47** — the position the older spelling of that same
option already occupied, which is why the two spellings are one option and not
two. IPv6-only is **54** and peer idle timeout is **55**, in that order, after
the last option the previous milestone defined. Every position from 0 to 53
keeps the value the release this milestone targets gives it; nothing is
renumbered and nothing is removed. That includes the four inherited options the
restriction matrix below reaches **by number**, and because it reaches them
that way their positions are part of the interface rather than a description of
the release: **minimum peer version 45, declared payload size 49, transmission
type 50, strict encryption 53.** A tree that carries one of the four at a
number of its own choosing fails `optmatrix` — a gate — however correctly the
option behaves at the number it was given. A tree that appends the two new
options in the other order, or that gives congestion control a fresh position
instead of reusing the old one, fails `peeridletimeo`, `ipv6only` and
`congestion` while compiling perfectly.

**Peer idle timeout is derived by the accepted socket from the listener.** The
option table states the 5000 ms default, so a fresh socket reading 5000 is
documented ground. The derivation is not: a listener set to 7000 before
`srt_bind` must produce an accepted socket that reads 7000, not 5000. A tree
that gives every accepted socket the default fails `peeridletimeo`.

**Peer idle timeout refuses a late set with `SRT_ECONNSOCK`.** The table's
restriction column says `pre`, but not which error a late set returns. It is
`SRT_ECONNSOCK` (5002) on a connected socket — not `SRT_EINVPARAM` and not a
silently ignored set that returns 0. A set before `srt_bind` and a set after
bind but before connect both succeed and both read back.

**IPv6-only reads −1 on a fresh socket.** The option table's default column
says "(platform default)" rather than a number, and its version column names a
later milestone than this one — the option is required now regardless. The
number behind "platform default" is **−1**, the sentinel that means "leave the
system's own `IPV6_V6ONLY` default alone". A tree that starts the option at 0
or 1 fails `ipv6only`. The group sets only −1, 0 and 1, requires each to be
accepted before `srt_bind` and to read back exactly as given, and asserts
nothing about values outside that range: this milestone does not range-check
the setter, and 2 or −7 are accepted and read back verbatim. Do not add a
range check to make the option look tidier — nothing tests for one, and
nothing tests against one either.

**IPv6-only is refused only once the socket is connected, and the error is
`SRT_ECONNSOCK`.** The option drives a system socket option on the underlying
UDP socket, but at this milestone the setter's only guard is the connected
state. A set on a socket that has been bound, or bound and then put into
listening, is accepted: it returns 0 and the value reads back. Do not add a
bind-time guard to make the option look tidier — the group sets the option
after `srt_bind` and requires that set to succeed. `SRT_ECONNSOCK` is the
error this option raises, and it raises it only on a connected socket.

**`srt_epoll_wait` must not refuse a wait that supplies only the writable
system-socket array.** The deadlock guard exists to reject a call that can
never be satisfied — every array NULL and an infinite timeout. A call that
supplies only the writable system-socket array is satisfiable, so it must
proceed and return the registered ready system socket in that array. The guard this milestone inherits reads, in effect,
`if (!readfds && !writefds && !lrfds && lwfds && (msTimeOut < 0))` — the
negation in front of `lwfds` is missing, so it fires on exactly the satisfiable
case and lets the unsatisfiable one through. Both halves are asserted: the satisfiable call returns 1 with the socket, and the call with
all four arrays NULL and an infinite timeout still fails with `SRT_EINVPARAM`.
Fixing the first half by deleting the guard fails the second.

The four arrays are, in parameter order, `readfds` and `writefds` — `SRTSOCKET`
arrays — then `lrfds` and `lwfds` — `SYSSOCKET` arrays. The prototype in
`<srt/srt.h>` is the authority and it is unambiguous:

```
int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum,
                   SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                   SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
```

so `lrfds` is the read-ready system-socket array and `lwfds` the write-ready
one. Take it from there rather than from the prose in `API-functions.md`, which
prints its `lwfds` bullet twice and never names `lrfds` at all. The **fourth**
array, the last of the four, is the writable system-socket set, and it is the
one the group supplies alone.

The count pointers are output parameters, not input lengths: on a successful
wait each of `rnum`, `wnum`, `lrnum` and `lwnum` for an array the caller
supplied is overwritten with the number of ids stored in that array — the
group that supplies only `lwfds` asserts `lwnum == 1` alongside the returned
value.

**The installed `<srt/srt.h>` must not define `NO_BUSY_WAITING`.** That name is
a private build-configuration switch, not part of the interface a caller
includes, and this milestone stops leaking it into every translation unit that
includes the header. The group compiles a source file whose only include is
`<srt/srt.h>`, fails if `NO_BUSY_WAITING` is defined there, and — as its
positive control, so that an empty or broken header cannot pass by accident —
also fails if the header's own macros `SRT_API` and `SRT_LOGFA_HAICRYPT` are
missing from the same translation unit.

**"No bandwidth limit" now means a far larger number, and `srt_bstats` shows
it.** With the maximum-bandwidth option at its −1 default, or explicitly set to
0, the internal ceiling that stands in for "unlimited" is raised by a factor of
33 — from the previous milestone's ~30 Mbps to ~1 Gbps, the figure the shipped
draft recommends (`srt-spec/draft-sharabayko-srt-01.txt` §5.1.1: "The
recommended default value is 1 Gbps"). The corpus carries only the old figure,
and no document states the new ceiling as a hard value, so the group asserts
bounds rather than an exact value: on a freshly connected
socket, `mbpsMaxBW` is **above 500** and `usPktSndPeriod` is **below 100**
microseconds, both read before any payload is sent, and the same two bounds
hold after the cap is explicitly set to 0. The previous milestone reports about
30 and about 362 for that pair, so the bounds separate the two milestones
without pinning a number the documents never state. Both fields must already be
populated when the connection completes; a tree that leaves `usPktSndPeriod` at
0 until the first packet goes out fails the group.

**`mbpsMaxBW` reports the cap that is configured, and it reports it in
megabits per second.** The maximum-bandwidth option is set in **bytes per
second**; the statistics field is in **megabits per second**; the conversion is
therefore `mbpsMaxBW = bytes_per_second × 8 ÷ 1 000 000`. The seeded
`API-functions.md` says only that the field is "usually" the setting from the
option, which is not enough to build against, so the contract states it: with
the cap set to an explicit positive value the field must report **that** value
converted, not a negotiated or measured rate. The group's third case sets
10 MB/s and requires `mbpsMaxBW` to be **80 ± 10 %** and `usPktSndPeriod` to be
**above 100** microseconds — the same pair of quantities as the two unlimited
cases, moving in the opposite direction, so a tree that hardcodes a large
constant into either field passes the first two cases and fails this one.

**The accept hook's return value is a single reject signal, not a boolean.**
The documentation says the hook "shall return 0, if the connection is to be
accepted" and that returning −1 requests rejection. Only the exact value −1
rejects. 0, 1, −2 and every other integer accept, and `srt_accept` yields the
socket. A tree that treats any non-zero return as a rejection fails
`listen-retval`.

**Installing the hook is idempotent, and a NULL hook pointer is the only
refusal.** `srt_listen_callback` returns 0 when it installs a hook; it returns
0 again when it is called a second time on the same listener, with the new hook
and its opaque replacing the previous ones — and the hook then observes the
re-installed opaque. A NULL **hook pointer** is refused with `SRT_EINVPARAM`,
which is also the error the function's own documentation names; a NULL
**opaque** is not an error, and installing a real hook with a null opaque
returns 0. Do not add defensive validation beyond the one documented failure:
`listen-install` asserts each of these.

**A rejected connection reports `SRT_ECONNREJ` to the caller and leaves the
listener intact.** When the hook returns −1 the caller's `srt_connect` fails
with `SRT_ECONNREJ` (1002) — not a timeout, not `SRT_ECONNLOST` — and
`srt_accept` on the listener never produces a socket for that attempt. The
listener itself is not broken by the rejection: the group then makes a second
connection whose hook returns 0 and requires it to be accepted on the same
listening socket. A tree that closes or invalidates the listener when a hook
rejects fails `listen-reject`.

**The hook fires once per incoming connection, before `srt_accept` returns,
with the caller's own handshake data.** It runs on the receiver's worker
thread, not on the thread that called `srt_accept`, and it is called while the
handshake is still in flight, so an option set inside it still takes effect
for that connection. The group asserts the arguments it is handed: the peer
address is the caller's address, the stream id is the string the caller set on
its own socket (and an empty string when the caller set none), the handshake
version is 5, and the opaque pointer is the one passed at install, unchanged.
The hit count is exactly one per accepted connection — a tree that invokes the
hook twice, or once per handshake packet, fails `listen-install`.

**The congestion control option refuses an unknown name.** The option table
gives `"live"` and `"file"` as the accepted names and `"live"` as the default,
but not what happens to a third string. It is refused with `SRT_EINVPARAM` and
the option keeps its previous value. Both spellings of the option — the current
one and the deprecated one kept for source compatibility — are the same
position and behave identically; the deprecated spelling must still compile.

That last clause is the one compile dependency in this step that is not on a
name this milestone introduces, so it is spelled out. The group proving it
lives alone in its own driver family, and that family names the deprecated
spelling directly — it cannot do otherwise, because the name is an enumerator
before the rename and a constant after it, never a macro, so no `#ifndef` can
stand in for it. A tree that renames the option and does **not** keep the old
spelling declared therefore fails to build that one family, and loses that one
group: `congestion` is a gate group, so the step is not satisfied, but it is
the only group affected and no requirement group depends on the name. Keep the old
spelling declared and pointing at the same position, as the release does.

**Loss recovery still needs too-late packet drop switched off.** Carried
forward from milestone 01 and unchanged here: a group that drops datagrams at
fixed relay indexes and then requires the payload to arrive complete sets
`SRTO_TLPKTDROP=0` and a generous `SRTO_LATENCY` on both sides first, because
the default live behaviour is to discard packets that arrive later than the
latency budget. Both options exist at every milestone in this chain, so a tree
that changes either default breaks `recovery`, `transfer` and `endtoend`
at once.

**The accept hook's type has five parameters, in this order.** The seeded
`API-functions.md` prints the typedef with a comma missing after the handshake
version, so it does not compile as printed; the order it gives is right and the
shape is this:

```
typedef int srt_listen_callback_fn(void* opaque, SRTSOCKET ns, int hs_version,
                                   const struct sockaddr* peeraddr,
                                   const char* streamid);
```

The verifier installs a hook of exactly that type through
`srt_listen_callback(lsn, fn, opaque)` and takes the arguments in that order. A
header that declares the parameters in another order, or that omits the opaque
pointer, fails to build the `hook` family.

**`srt_listen_callback` is a hard compile dependency, and the generator's table
marks it so.** It is the second of this step's two unguarded references to a
name the milestone introduces — the driver calls it directly, with no `#ifndef`
fallback and no `dlsym`, because a function cannot be stood in for the way a
constant can. A header that does not declare `srt_listen_callback` and
`srt_listen_callback_fn` does not fail one assertion; it fails to compile the
`hook` family, and all three of its groups are lost. The table under "Public
names this milestone must introduce" lists it as **required to compile**
alongside the three constants, and the reason it is also called out here is
that its cost differs from a constant's: there is no guarded form of a
function declaration that could soften it.

**String options report their length without the terminator.** Reading a string
option with `srt_getsockflag` leaves the length as `strlen` of the value, not
`strlen + 1`, and a set is given `strlen`. This is the convention the previous
milestone established with the stream-id option and it does not change here,
but no shipped document states it either time, so it is stated here: the groups
that read a string option build their comparison from the reported length, and
a tree that reports the NUL as part of the value fails them.

**The version macros are read, never required.** The verifier's compile-time
checks on the option numbering are guarded on `SRT_VERSION_VALUE` and
`SRT_MAKE_VERSION_VALUE` from `<srt/version.h>`: where the header reports this
milestone's version or later the numbering is checked at compile time, and
where it reports something earlier, or ships no version header at all, those
checks are skipped and nothing is lost. Bumping the version macro is not part
of this milestone and is neither required nor measured — the behaviour is what
is measured. The option numbering itself is asserted either way, because every
option a group exercises is reached by its numeric position at run time.

**Where a seeded document disagrees with this section, this section is the
contract.** The documents under `srt-docs/` are the upstream project's own,
reproduced unedited at the pinned commit. They are not part of this task's
authorship and nothing in this package alters a shipped document — so where one
of them is wrong, it stays wrong on the page and the correction is made here,
with the file and line named so you can check both. Four such places matter at
this milestone, and all four are load-bearing for a group:

- **`srt-docs/API.md:875-877`** shows the congestion-control option with a
  **[SET]** marker only. That is wrong: the option is readable as well, and the
  groups read it — the getter returns the string the setter was given, and
  `"live"` on a socket that has had no setter.
- **`srt-docs/API.md:593-594`** annotates the maximum-bandwidth option's `-1`
  as "infinite (CSRTCC limit is 30mbps)" and its `0` as "relative to input
  rate". Both describe the *previous* milestone. At this one, `-1` and `0`
  alike mean the raised ceiling described above, which is what `bstats-bw`
  asserts.
- **`srt-docs/API-functions.md:343`** prints the accept hook's typedef with the
  comma missing after `int hs_version`, so the declaration as printed does not
  compile. The parameter order it gives is right; the corrected form is above.
- **`srt-docs/API-functions.md:1143-1144`** prints the `lwfds` bullet twice, so
  the read-ready system-socket array is never named and `lrfds` appears in the
  signature at `:1126` and nowhere in the prose. The corrected four-array order
  is above. This is the distinction the `epoll-lwfds` group turns on, so the
  duplicated bullet is the one seeded-doc defect that could fail an in-scope
  group by itself.

Do not repair the copies. They are quoted for provenance, and a diff against
upstream is how that provenance is checked.

**The inherited option restriction matrix still holds, and it is checked.**
This is the step whose work is surgery on the option enum — three members added
and a fourth renamed — so the behaviour *behind* the inherited positions is
restated here rather than assumed, and a gate group asserts it. No shipped
document states this matrix; it was measured against both releases and the two
agree exactly.

- A **connected** socket refuses a set of the inherited pre-connect options
  with `SRT_ECONNSOCK`. The minimum peer version, the declared payload size,
  the transmission type and strict encryption — **45, 49, 50 and 53** — are all
  settable on a fresh socket and all refused once the socket is connected: on
  the socket that called `srt_connect` *and* on the socket `srt_accept`
  returned. The case reaches all four **by number**, so those positions are
  part of the contract and not merely a description of the release; a tree that
  numbered one of them differently fails this gate even though the option
  behaves correctly where it put it. A guard written into the connect path
  alone fails the second half.
- An **undefined** option id is refused in both directions, but not with the
  same error: a *get* fails with `SRT_EINVOP` and a *set* of the same id fails
  with `SRT_EINVPARAM`. Collapsing the two onto one code fails the group.
  The group reaches that id *by number*, so this is a requirement and not
  merely a description of today's behaviour: **option ids at and above 99 must
  remain undefined.** 99 and above is reserved space the gate reads, and it
  stays reserved for the rest of this chain — the highest id any later
  milestone here defines is 63. Ids below 99 that no group reaches by number
  are yours to place, but the four inherited positions the case above reads —
  45, 49, 50 and 53 — are not: they keep the values named there, and the three
  options this milestone adds take 47, 54 and 55. Together with the 99 rule,
  that is the whole qualification on "every other constant is yours to number"
  under "Constants whose numeric value the verifier pins" below.
- The declared payload size keeps its inherited default — a positive size that
  fits within one path MTU — accepts a smaller value and reads it back, refuses
  a value far above one MTU with `SRT_EINVPARAM`, and **keeps its previous
  value** after that refusal. A setter that validates after assigning fails the
  last clause.
- `SRTO_RCVSYN` and `SRTO_SNDSYN` are **not** in that set, despite their `pre`
  rows: the library accepts a set of either at any state — including on a
  connected socket and on the socket `srt_accept` returned — and the new value
  takes effect for subsequent calls. A socket returned by `srt_accept` starts
  with the listener's value, so a listener opened non-blocking yields a
  non-blocking accepted socket whose receive mode you may then switch back. A
  universal reading of the `pre` legend that refuses these two loses every
  group that receives on an accepted socket.

None of this is new work: it is the matrix milestone 02 established, and every
assertion passes unchanged on a tree that carries the inherited positions this
section names — the four the gate reads by number included. If an earlier
milestone left one of them at a number of your own choosing, place it where
this section gives it; the behaviour behind the option does not change and
nothing else about it is asked for. It is asserted because
it is the thing most easily broken by the enum surgery this milestone asks for,
and because no other group in **this** milestone reaches it — the two option
groups above restrict only the options this milestone *adds*. Milestone 02
established the matrix and milestone 03 gates it, but each milestone is
checked solely by its own groups, so an earlier milestone's gate does nothing
about a regression introduced here. `optmatrix` is a gate: regressing it means
the step is not satisfied, while passing it adds no requirement of its own.

**Three documented behaviours of this milestone's own surface are out of scope
here.** They are recorded so that their absence from the verifier is not read
as an omission to fix. The IPv6-only *effect* on connections: the option's
default, its accepted values and its refusals are asserted, but whether a
listener with it enabled actually rejects IPv4 clients is not — the two
listener scenario depends on the host's IPv6 configuration, and the driver
records the exclusion deliberately. The peer-idle *breaking* semantics: the
default and the late-set refusal are asserted; no group waits out a silence
timeout to watch a connection break. The hook's in-flight property: the
arguments and the hit count are asserted, but not that an option set **inside**
the hook takes effect for the connection being accepted. Implement all three as
the documents describe them; nothing here measures them either way.

**What a wrong number does depends on the version your header reports.**
The table under "Constants whose numeric value the verifier pins" below says
that an implementation numbering one of them differently "fails to compile".
That is the whole story only for a tree whose `<srt/version.h>` reports 1.3.3
or later, because every `static_assert` behind that promise sits inside
`#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 3, 3)` — and, as above,
bumping the version is neither required nor measured. On a tree reporting an
earlier version those compile-time pins are skipped, and what remains is:

- `SRTO_CONGESTION` (47), `SRTO_IPV6ONLY` (54) and `SRTO_PEERIDLETIMEO` (55)
  are still reached **by their numeric position** at run time, so a wrong
  number fails the group that reads that slot instead of failing the build.
  Nothing about them goes unchecked; only the *way* you are told changes.
- `SRTO_SNDKMSTATE` (40) and `SRTO_STREAMID` (46) are there as anchors for
  "the inherited numbering did not move", and no group reads them at run
  time, so on such a tree they are checked only by the skipped pins.

Put the options at the numbers this section gives and the distinction never
arises. It is spelled out because the table below states the compile-time half
only, and a reader who plans around "it will not build" deserves to know the
other half exists.

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
| `SRTO_CONGESTION` | optional (guarded) |
| `SRTO_IPV6ONLY` | optional (guarded) |
| `SRTO_PEERIDLETIMEO` | optional (guarded) |
| `srt_listen_callback` | **required to compile** |

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
the wrong reason. Every other constant is yours to number — except the
positions the restriction matrix above reads by number, 45, 49, 50 and
53, and the reserved space at 99 and above. Those are fixed by the
sections above rather than by this table, and they could not be pinned
here: the gate that reads them spells no `SRT_SOCKOPT` enumerator, so
there is nothing for a `static_assert` to restate.

| constant | value |
| --- | --- |
| `SRTO_CONGESTION` | `47` |
| `SRTO_IPV6ONLY` | `54` |
| `SRTO_PEERIDLETIMEO` | `55` |
| `SRTO_SNDKMSTATE` | `40` |
| `SRTO_STREAMID` | `46` |

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
