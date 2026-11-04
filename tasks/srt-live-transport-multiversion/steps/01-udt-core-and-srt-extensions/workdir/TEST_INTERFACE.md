# Test interface — milestone 1 of 11

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
9 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.2.0**. It installs no
`<srt/version.h>`: that header and the `SRT_VERSION_*` macros arrive
with the next milestone, and no verifier translation unit here refers
to them. What the version means at this milestone is the build:
upstream sets it with `set (SRT_VERSION 1.2.0)` at the start of a
line in `CMakeLists.txt`. No group at this milestone reads a version
macro, so nothing here asserts the value.


---

## 2. How the work is checked

There is 1 **gate** group (§3) and 11 **requirement** groups
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
| 1 | `lifecycle` | socket creation, bind, listen, accept, connect and close drive `srt_getsockstate` through the documented states, and operations invalid for a state are refused |
| 2 | `sockopt` | the documented socket options are readable at their documented defaults, accept values of the documented type and width, and refuse a set that the documentation places before connect once the socket is connected |
| 3 | `errors` | a failed call sets a documented error code that `srt_getlasterror` reports, `srt_strerror` renders and `srt_clearlasterror` resets, and a successful call leaves it alone |
| 4 | `epoll` | `srt_epoll_wait` reports readability and writability level-triggered, over SRT sockets and system sockets in the same set, and honours its timeout |
| 5 | `transfer` | live messages of the documented payload size cross an intact channel complete and in order, and a message larger than the negotiated limit is refused rather than truncated |
| 6 | `recovery` | datagrams dropped at fixed indexes are retransmitted and the payload is delivered intact when too-late drop is disabled, with the receiver's loss counter showing the loss really happened |
| 7 | `tsbpd` | with TSBPD on, delivery is held until the negotiated latency has elapsed, the latency is what the handshake agreed, and packets later than it are dropped instead of delivered late |
| 8 | `stats` | `srt_bstats` and `srt_perfmon` report counters that agree with what crossed the channel and are monotone across successive calls |
| 9 | `encryption` | with a matching passphrase on both sides the payload is carried intact at both key lengths and does not appear in clear in the datagrams on the wire, while a passphrase on the sender only, or two different passphrases, delivers nothing to the application |
| 10 | `kmstate` | `SRTO_RCVKMSTATE` and `SRTO_SNDPEERKMSTATE` reach the documented KM state on both sides for the unsecured, secured, no-secret and bad-secret cases, the receiver's `pktRcvUndecrypt` interval counter is non-zero in the no-secret and bad-secret cases and zero in the secured one, and `SRTO_KMSTATE` never contradicts them |
| 11 | `endtoend` | a live stream of 4000 messages crosses a clean relay and arrives byte-identical, in order, with no gaps -- and the same 4000 messages do the same over a relay adding 15 ms each way. Two checks, both in this one group |

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

The first three parts below are about *spelling*: the exact names, signatures
and constants the verifier's own programs compile and link against. They are
here because your reading list either gives them wrongly or never gives them
at all. This matters more than one group's worth: the verifier builds all of
its programs before it runs any of them, so a single name it cannot resolve is
not one failed group, it is a failed build and every group with it.

Everything stated here was checked against the header of the release this
milestone targets. **Where this section and `srt-docs/` disagree, this section
is right.**

### The pinned documents are wrong in seven places

`srt-docs/` is pinned byte for byte, and `srt-docs/PROVENANCE.md` publishes the
first 16 hex digits of each file's sha256 (enough to identify a revision, not
the whole digest). It describes a **later** state of this protocol than
the one you are asked to build here — no contemporaneous document set exists for
this milestone — so parts of it run ahead of your work. Pinned also means its own
mistakes are reproduced unedited rather than quietly corrected, so that the
documents stay verifiably the documents. Seven of those
mistakes would send you the wrong way here, and this is the correction.

**`srt_close` returns `int`, not `void`.** `API.md` line 37 prints
`void srt_close(SRTSOCKET s);`. The declaration this milestone must present is

    int srt_close(SRTSOCKET u);

returning `0` on success and `SRT_ERROR` on failure. The verifier checks the
result of closes it expects to succeed, so a `void` return does not compile
against it.

**`srt_sendmsg` takes five parameters, not six.** `API.md` line 228 prints

    int srt_sendmsg(SRTSOCKET s, const char* buf, int len, int msgttl, bool inorder, uint64_t srctime);

The declaration at this milestone is

    int srt_sendmsg(SRTSOCKET u, const char* buf, int len, int ttl, int inorder);

— five parameters, and `inorder` is an `int`, not a `bool`. There is no
`srctime` parameter and no default arguments: every one of the verifier's
sixteen `srt_sendmsg` call sites — fifteen in the group drivers and one in the
shared helper the drivers include — passes exactly five arguments. Per-message
source time belongs to `srt_sendmsg2` and `SRT_MSGCTRL`, and to a later
milestone.

**The option accessors return `int` and take `int* optlen`.** `API.md`
lines 363–364 and 368–369 print a `void` return and a C++ reference parameter
(`int& optlen`) in what is a C header. The four declarations are

    int srt_getsockflag(SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen);
    int srt_setsockflag(SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);
    int srt_getsockopt(SRTSOCKET u, int level, SRT_SOCKOPT opt, void* optval, int* optlen);
    int srt_setsockopt(SRTSOCKET u, int level, SRT_SOCKOPT opt, const void* optval, int optlen);

all returning `0` or `SRT_ERROR`. `API.md` calls the `...flag` pair the "new
version" and the `...opt` pair "legacy"; the verifier uses the `...flag` pair
exclusively, and reads the written length back out of the getter's `int*`, so
that pointer must be updated with the number of bytes stored. `PROFILE.md` §5
rule 1 states the binding-defaults rule in terms of "`srt_getsockopt()` on a
fresh socket"; read that as naming the operation, not the spelling — every
default in this milestone is asserted through `srt_getsockflag`, and no group
calls `srt_getsockopt` at all. Declare both pairs; only the `...flag` pair is
exercised.

**The epoll event bits are `SRT_EPOLL_IN` and `SRT_EPOLL_OUT`.** `API.md`
line 343, and the `SRTO_EVENT` row at line 406, write `EPOLLIN`, `EPOLLOUT`
and `EPOLLERR` — the Linux kernel's names, which SRT does not use. The
enumeration is `SRT_EPOLL_OPT` and its members are

    SRT_EPOLL_IN  = 0x1
    SRT_EPOLL_OUT = 0x4
    SRT_EPOLL_ERR = 0x8

The verifier passes `SRT_EPOLL_IN` and `SRT_EPOLL_OUT` by those names and
relies on those values. No group asserts anything about `SRT_EPOLL_ERR`.

**`SRTO_REUSEADDR` is a `bool`, and getting it stores one byte.** The Type
cell of the `SRTO_REUSEADDR` row at `API.md` line 466 is **empty**: the row
gives a default of `true` and no type at all. The option is typed exactly like
`SRTO_SNDSYN` and `SRTO_RCVSYN`, and the row should read

    | `SRTO_REUSEADDR` | | pre | `bool` | | true | true | false | ... |

This is load-bearing rather than cosmetic. `API.md` lines 374–378 say that a
*set* may pass either an `int` or a `bool` but that "this is not so in the case
of getting an option value", so on a **get** the width is part of the contract —
and `sockopt` reads `SRTO_REUSEADDR` into a `bool`, requiring the getter to
store exactly `sizeof(bool)`, one byte, and to report `1` through `optlen`. Do
not carry the `` `int32_t` (bool?) `` of the neighbouring rows across to this
one.

Since the blank cell is not the only place the table is vague about width, here
is every option a group reads, and what your getter must store for it:

| options a group gets | bytes stored | asserted through `optlen`? |
|---|---|---|
| `SRTO_SNDSYN`, `SRTO_RCVSYN`, `SRTO_REUSEADDR`, `SRTO_NAKREPORT`, `SRTO_RENDEZVOUS` | `sizeof(bool)` — one | yes |
| `SRTO_MSS`, `SRTO_FC`, `SRTO_CONNTIMEO`, `SRTO_SNDTIMEO`, `SRTO_RCVTIMEO`, `SRTO_SNDBUF`, `SRTO_RCVBUF`, `SRTO_UDP_SNDBUF`, `SRTO_UDP_RCVBUF`, `SRTO_PBKEYLEN`, `SRTO_SENDER`, `SRTO_LATENCY`, `SRTO_RCVKMSTATE`, `SRTO_SNDPEERKMSTATE`, `SRTO_SNDPBKEYLEN`, `SRTO_RCVPBKEYLEN` | `sizeof(int32_t)` — four | yes |
| `SRTO_STATE`, `SRTO_EVENT`, `SRTO_RCVDATA`, `SRTO_KMSTATE` | four; the verifier supplies a four-byte buffer and does not check `optlen` back, but a getter that writes more overruns it | no |
| `SRTO_MAXBW` | `sizeof(int64_t)` — eight | yes |
| `SRTO_LINGER` | `sizeof(struct linger)` | yes |

That also settles the `` bool? `` question mark `API.md` leaves on
`SRTO_SENDER` (line 468), `SRTO_TSBPDMODE` (line 490) and `SRTO_TLPKTDROP`
(line 486): all three are `int32_t` — four bytes on a get, either width on a
set. No option a group reads is `bool` on a get except the five in the first
row above.

**`SRTO_TSBPDMODE` is `true` by default, not `false`.** The row at `API.md`
line 490 contradicts itself: the Default cell says `false` while the row's own
Description says "This flag is set to _true_ by default and as a default flag
set in live mode", and the "Transmission method: Live" block further down says
the default `SRTO_TRANSTYPE` of `SRTT_LIVE` turns it on. The Description is
right for this release: a fresh socket reads `SRTO_TSBPDMODE` as `true`. No
in-scope case reads this default — every case that depends on TSBPD sets the
option explicitly on both sides, and `sockopt` deliberately does not read
it — so the correction is here to stop `PROFILE.md` §5 rule 1 binding you to a
self-contradictory cell, not because a group turns on it.

**`SRTO_LATENCY` defaults to 120 ms, not `0`.** The Default cell of the
`SRTO_LATENCY` row at `API.md` line 425 says `0`; the release this milestone
targets initialises the TSBPD delay to **120 ms** — which is also what
`API.md` says everywhere except that cell: line 279 ("a small delay (default
120 ms)"), the "Transmission method: Live" block at line 551
(`SRTO_RCVLATENCY` = 120) and line 564 ("the receiver uses 120 ms of latency").
A fresh socket reads `120`. As with `SRTO_TSBPDMODE`, no in-scope case reads this
default: `sockopt` sets `SRTO_LATENCY` to 350 and reads 350 back, and every
`tsbpd` case that asserts a negotiated figure configures both sides first.
One case does rely on the effective latency being *small* relative to a
150 ms-each-way relay — `packets_later_than_the_latency_are_dropped`, which
switches TSBPD on and leaves the latency alone — and both `0` and `120` satisfy
it, so either reading passes. It is documented here so that the reliance is
stated rather than assumed.

### What the pinned documents never mention at all

`srt-docs/` describes the protocol, the transmission modes and the socket
option table. It does not name a single one of the entry points, types or
constants below, and the verifier calls or references all of them. This list
is complete: it is every SRT symbol the verifier's programs use that does not
appear anywhere in `srt-docs/`. If a name is neither in `srt-docs/` nor here,
no group uses it.

**Library startup and shutdown.** `API.md` names both of these in prose and
gives neither a signature. Both are `int`-returning and take no arguments:

    int srt_startup(void);
    int srt_cleanup(void);

`srt_startup` returns `SRT_ERROR` on failure; any non-negative value means
success, and the verifier does not distinguish them. Every one of the
verifier's programs calls `srt_startup` before its first socket call and
`srt_cleanup` after its last, so a declaration that returns `void` is a
**compile** error in every driver family rather than one failed group.

**The two `-1` sentinels.**

    static const int       SRT_ERROR        = -1;
    static const SRTSOCKET SRT_INVALID_SOCK = -1;

**Most** `int`-returning entry points return `0` on success and `SRT_ERROR` on
failure, and the verifier compares against the names rather than against `-1`.
**Six entry points do not follow that rule**, and a group asserts the return of
every one of them. Read this table as overriding the sentence above wherever
the two could be read against each other:

| entry point | returns on success | returns on failure |
|---|---|---|
| `srt_socket`, `srt_accept` | the new socket id | `SRT_INVALID_SOCK` |
| `srt_sendmsg` | the number of bytes accepted — for a message the connection can carry, exactly the `len` passed in | `SRT_ERROR` |
| `srt_recvmsg` | the number of bytes delivered, i.e. the length of the one message returned | `SRT_ERROR` |
| `srt_epoll_create` | a non-negative epoll id, distinct from every other id still live | `SRT_ERROR` |
| `srt_epoll_wait` | the number of ids it stored across its four output sets: a **positive** count | `SRT_ERROR`, with `SRT_ETIMEOUT` when the timeout expired |

Nothing else about these six is unusual: `0` is simply not among their success
values. Every one of the verifier's `srt_sendmsg` call sites is written
`rc == (int)len` and every `srt_recvmsg` call site takes the return as the
length of the message it just received, so a `0` return ends a receive loop and
a short return fails the case. Every `srt_epoll_create` result is required to
be `>= 0` and to keep working after a *second* epoll set has been created, so a
constant cannot serve. Every successful `srt_epoll_wait` is required to be
`> 0` while every expected failure is required to be exactly `SRT_ERROR`, which
leaves `0` meaning neither — see "What `srt_epoll_wait` does when its timeout
expires" below.

**Reporting the last error.**

    int         srt_getlasterror(int* errno_loc);
    const char* srt_getlasterror_str(void);
    const char* srt_strerror(int code, int errnoval);
    void        srt_clearlasterror(void);

`srt_getlasterror` returns the `SRT_ERRNO` code and accepts `NULL` for
`errno_loc`. `srt_getlasterror_str` is called from the verifier's own failure
messages, so it must always return a printable C string and never `NULL`.

**No group asserts the wording of any message.** `errors` requires that
`srt_strerror` returns a non-empty string for each code below and a
*different* string for each of them, and that `srt_getlasterror_str()` returns
the same text as `srt_strerror(code, 0)` for the code currently set. The
wording itself is yours.

**Socket state.**

    SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u);

`API.md` names neither the function nor the type. `enum SRT_SOCKSTATUS` has
nine members, in this order and starting at 1: `SRTS_INIT`, `SRTS_OPENED`,
`SRTS_LISTENING`, `SRTS_CONNECTING`, `SRTS_CONNECTED`, `SRTS_BROKEN`,
`SRTS_CLOSING`, `SRTS_CLOSED`, `SRTS_NONEXIST`. The verifier switches over all
nine to render a state name, so all nine must exist even though `SRTS_BROKEN`
and `SRTS_CLOSING` are not reached by any in-scope case.

**Which operation yields which state.** No document in your reading list gives
the mapping, and the names are not as self-evident as they look — `SRTS_INIT`
and `SRTS_OPENED` in particular can be read either way round. This is the
mapping `lifecycle` asserts, and every entry is read on the statement after
the call returns:

| after | `srt_getsockstate` reports |
|---|---|
| `srt_socket` — created, not yet bound | `SRTS_INIT` |
| `srt_bind` | `SRTS_OPENED` |
| `srt_listen` | `SRTS_LISTENING`, and it stays `SRTS_LISTENING` across an `srt_accept` |
| `srt_connect` that completed, on the caller | `SRTS_CONNECTED` |
| `srt_accept`, on the socket it returned | `SRTS_CONNECTED` |
| `srt_connect` still in flight (see below) | `SRTS_CONNECTING` |
| `srt_close`, on the id just closed | `SRTS_CLOSED` |
| an id the library never issued | `SRTS_NONEXIST` |

**A closed id reports `SRTS_CLOSED`, not `SRTS_NONEXIST`.** The two are not
interchangeable and the group distinguishes them: `SRTS_CLOSED` is an id this
process was given and has closed, `SRTS_NONEXIST` is an id it was never given.
The GC note at `API.md` lines 53–57 — "the true life of the socket does not end
exactly after this function exits, some details are being finished in a
separate SRT GC thread" — is about reclaiming the socket's resources, not about
what its id reports. `srt_getsockstate` must answer `SRTS_CLOSED` on the
statement after `srt_close` returns, for an unbound socket and for a connected
one alike. Nothing asserts how long it goes on answering that, so when the id is
eventually forgotten and starts reporting `SRTS_NONEXIST` is yours.

**A non-blocking `srt_connect` succeeds immediately and leaves the socket
`SRTS_CONNECTING`.** `API.md` lines 305–315 say that in non-blocking mode "the
function always exits immediately, and in case of lack of resource
availability, it returns an error with appropriate code", and that "SNDSYN
defines blocking for `srt_connect`". A handshake that has not completed yet is
*not* a lack of resource availability, and it is not an error:

* with non-blocking mode in effect, `srt_connect` returns **success** — not
  `SRT_ERROR`, and in particular not `SRT_ERROR` with `SRT_ENOCONN` — as soon
  as it has sent the handshake request;
* the socket then reports `SRTS_CONNECTING` until the handshake completes
  (`SRTS_CONNECTED`) or fails (`SRTS_BROKEN`).

`lifecycle` provokes this by clearing **both** `SRTO_SNDSYN` and
`SRTO_RCVSYN` and connecting to a bound UDP port that never answers, then
reading the state on the next statement; clearing both means the case holds
whichever of the two flags your implementation treats as the one governing
`srt_connect`.

**Error codes.** `API.md` names the type `SRT_ERRNO` once, at line 529, and
mentions three of its members in passing while discussing blocking sends. It
never lists the enumeration. These are the codes a group compares against by
identity, and what the verifier does to provoke each:

| code | provoked by |
|---|---|
| `SRT_ESUCCESS` | nothing; it is the value after `srt_clearlasterror` |
| `SRT_EUNBOUNDSOCK` | `srt_listen` on a socket that was never bound |
| `SRT_ENOLISTEN` | `srt_accept` on a bound socket that is not listening |
| `SRT_ECONNSOCK` | `srt_connect` on a listening socket; a pre-connect option set on a connected socket |
| `SRT_ENOCONN` | `srt_sendmsg` on a socket that is not connected |
| `SRT_EINVSOCK` | any call on a socket id that was never issued |
| `SRT_EINVPARAM` | `SRTO_PBKEYLEN` set to a value outside `{0, 16, 24, 32}` |
| `SRT_EINVPOLLID` | an epoll call on an epoll id that was released |
| `SRT_ETIMEOUT` | `srt_epoll_wait` expiring with nothing ready |
| `SRT_ELARGEMSG` | `srt_sendmsg` with a buffer larger than the connection can carry |
| `SRT_EINVOP` | see "`SRTO_KMSTATE` is left open" below |

Two refusals are asserted *without* asserting a code, because the code the
released library reports for them is not the one the name suggests and is not
worth pinning: binding a socket twice, and calling `srt_listen` on a connected
socket. Both must fail with `SRT_ERROR` and leave the socket's state alone;
which code you report is yours.

**`SRTO_RCVSYN` and `SRTO_SNDSYN` are exempt from the post-connect refusal.**
`API.md` types both as `pre`, and the table above gives `SRT_ECONNSOCK` for "a
pre-connect option set on a connected socket" — but these two are the
exception the released library has always made, and the one a group depends
on. `srt_setsockflag` accepts either of them at **any** state, including on a
connected socket and on the socket `srt_accept` returned, and the new value
takes effect for subsequent calls. A socket returned by `srt_accept` starts
with the listener's value (the derivation `API.md` lines 391–393 describe), so
a listener opened non-blocking yields a non-blocking accepted socket whose
receive mode you may then switch back. Treating the `pre` legend as universal
for these two and refusing the set loses every group that receives on an
accepted socket.

**epoll.**

    int srt_epoll_create(void);
    int srt_epoll_add_usock(int eid, SRTSOCKET u, const int* events);
    int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int* events);
    int srt_epoll_remove_usock(int eid, SRTSOCKET u);
    int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events);
    int srt_epoll_release(int eid);
    int srt_epoll_wait(int eid,
                       SRTSOCKET* readfds,  int* rnum,
                       SRTSOCKET* writefds, int* wnum,
                       int64_t msTimeOut,
                       SYSSOCKET* lrfds, int* lrnum,
                       SYSSOCKET* lwfds, int* lwnum);

`API.md` §epoll shows only `srt_epoll_update_{u|s}sock` and refers to the
others in prose as `srt_epoll_add_{u|s}sock()` and `epoll_remove_{u|s}sock()`,
never as declarations. `srt_epoll_wait` takes **ten** parameters; the verifier
passes `NULL` for a set it is not interested in and for that set's count, and
passes real arrays for the rest. On success the count pointers are overwritten
with how many ids were stored.

**What `srt_epoll_wait` does when its timeout expires.** It returns
`SRT_ERROR` and sets the last error to `SRT_ETIMEOUT`. It does **not** return
`0`. §3's row for `epoll` says the call "honours its timeout", and "returns
without an event when nothing is ready" means this same contract, which is the
one the group asserts. The group also asserts that
the wait does not come back *early*, with 50 ms of tolerance.

**What counts as ready.** `epoll` pairs every positive with a negative, so
an implementation that reports everything always ready fails it just as surely
as one that reports nothing. The four rules it asserts:

* a connected socket with room in its send buffer is `SRT_EPOLL_OUT`-ready,
  and is reported again on every subsequent wait because nothing consumed it;
* a socket that has been created but **never connected** is *not*
  `SRT_EPOLL_OUT`-ready. A wait over a set holding only that socket must expire
  — `SRT_ERROR` with `SRT_ETIMEOUT` — rather than report it writable. Writable
  means "a send would be accepted now", and on an unconnected socket it would
  not be;
* a socket holding an undelivered message is `SRT_EPOLL_IN`-ready, is reported
  on every wait until the message is actually received, and stops being
  reported once it has been. The same holds for a system socket in the same
  set, in both directions;
* a socket removed with `srt_epoll_remove_usock` stops being reported at once,
  even though nothing about the socket itself changed.

**Statistics.**

    int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS* perf, int clear);
    int srt_perfmon(SRTSOCKET u, SRT_TRACEINFO* perf, int clear);

Neither function, neither type, nor any field name appears in `srt-docs/`.
`SRT_TRACEINFO` is the UDT-derived structure and `SRT_TRACEBSTATS` is SRT's
extension of it; they are distinct types. A non-zero `clear` resets the
interval counters and leaves the cumulative `*Total` counters alone. It reads
the interval before it resets it: a `clear` sample reports the interval that
just ended — the counts of the messages that moved since the previous
`clear`, not zeros — and zeroes them as it returns, so `clear` is an atomic
read-and-reset and not a reset the caller must follow with a second call. A
clearing call that answered all-zero would fail the `stats` group, which reads
the interval it cleared.

The fields the groups read, all of them cumulative except where noted:

| field | on | meaning the group relies on |
|---|---|---|
| `pktSentTotal` | both | data packets sent, retransmissions included |
| `pktRecvTotal` | both | packets received |
| `pktRetransTotal` | both | packets retransmitted by this sender |
| `pktRcvLossTotal` | both | packets this receiver detected as lost |
| `pktRcvDropTotal` | `SRT_TRACEBSTATS` | packets dropped for arriving too late to play |
| `byteSentTotal` | `SRT_TRACEBSTATS` | bytes sent |
| `byteRecvTotal` | `SRT_TRACEBSTATS` | bytes received |
| `pktSent`, `pktRecv` | `SRT_TRACEBSTATS` | the same two counts *for the interval*, reset by `clear` |
| `pktRcvUndecrypt` | `SRT_TRACEBSTATS` | packets that arrived and could not be decrypted, *for the interval* |
| `msRcvTsbPdDelay` | `SRT_TRACEBSTATS` | the TSBPD delay actually in force on this receiver, in ms |

**The undecryptable-packet counter is what separates "arrived but could not be
decrypted" from "never arrived", and the groups read it as such.** Where the
key material does not match — the sender holds a passphrase and the receiver
does not, or the two hold different ones — the encrypted packets still cross
the channel and reach the receiver, and the receiver counts every one of them
in `pktRcvUndecrypt`: the interval counter climbs with the messages the sender
pushes, so after such a stream it stands at least as high as the number of
messages sent. `kmstate` asserts a non-zero count in its no-secret and
bad-secret cases and a **zero** count in its secured cases, where the same
stream decrypts; `encryption`'s two failing-key cases assert the same floor
after clearing the counter. A receiver that discards what it cannot decrypt
without charging the counter fails those cases, however correctly it reports
the KM state: the state alone does not replace the count. Nothing here says the
packets are *delivered* — at this milestone they are counted and not handed to
the application.

**What the handshake agrees the latency to be.** The effective latency is the
**larger** of the two sides' configured values, whichever side the larger one
is on. `API.md` does say this, but only in the `SRTO_RCVLATENCY` row at line
458 — a row marked `Since 1.3.0` for an option that row itself says "in
pre-1.3.0 version is available only as `SRTO_LATENCY`", so it reads as
belonging to a later release than this one. The SRT draft states it twice: generally in
§4.4 "SRT Buffer Latency" — "The latency for a connection will be established
as the maximum value of latencies proposed by the initiator and responder" —
which is the TSBPD section this milestone implements outright, and again inside
the HSv5 conclusion text as "The value for latency is always agreed to be the
greater of those reported by each party". It holds here,
and `tsbpd` pins it in both directions: a case with the larger value on the
receiver (1200 against 700) and a case with the larger value on the **sender**
(1300 against 700), where a receiver that simply kept its own setting would
report 700 and fail.

**Where the negotiated latency is observable.** `srt_getsockflag(SRTO_LATENCY)`
reports back the value *this* side configured, not the value the two sides
settled on in the handshake. At this release the only place the negotiated
figure appears is `SRT_TRACEBSTATS::msRcvTsbPdDelay`, read on the receiver.
"The latency actually in force" therefore has exactly one documented
observable at this milestone; that is where `tsbpd` reads it, and your
implementation has to put it there. With TSBPD switched off on both sides no
latency is negotiated at all and `msRcvTsbPdDelay` reads `0`.

**The local address of a bound socket.**

    int srt_getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen);

The verifier's shared connection helper binds every listener to port `0` and
then asks `srt_getsockname` which port the system actually chose, so every
group that opens a link depends on this one working.

**Negotiated key length is readable per direction.** `SRTO_SNDPBKEYLEN` and
`SRTO_RCVPBKEYLEN` are absent from `API.md`'s option table, which lists only
`SRTO_PBKEYLEN`. After a successful key exchange the sender reports its own
configured length through `SRTO_SNDPBKEYLEN` and the receiver reports *the
sender's* length through `SRTO_RCVPBKEYLEN` — the receiver learns it from the
exchange rather than from its own configuration.

**Setting a read-only option is refused.** The options `API.md` marks `(r)` —
`SRTO_STATE`, `SRTO_EVENT`, `SRTO_RCVDATA` among them — must fail a set with
`SRT_ERROR` rather than accept it. The group asserts the refusal, not the
code.

### Behaviour of this release the groups depend on

**A passphrase alone does not encrypt anything.** `SRTO_PBKEYLEN` defaults to
`0` at this release, and `0` means no encryption — despite the header comment
claiming a 128-bit default. A sender that sets `SRTO_PASSPHRASE` and nothing
else transmits in clear and every KM state stays `SRT_KM_S_UNSECURED`. The
encryption groups always set the key length explicitly, to 16 or to 32, on the
sending side.

**The key exchange is driven by traffic, not by the handshake.** KMREQ is sent
from the sender's periodic timer, so on an idle connection the KM state never
leaves `SRT_KM_S_UNSECURED` however long you wait, and the first few messages
after connect are sent before KMRSP comes back and cannot be decrypted. The
groups therefore drive the exchange with warm-up traffic, tag it, skip it in
the receive loop, and wait on the *observed* state rather than on a timer.
Your implementation is not required to settle within any particular number of
messages; it is required to settle. The warm-up loop is a hang guard, not a
deadline: it allows several thousand paced messages (about eight seconds at
the pacing it uses) before it gives up, which no sane cadence approaches.

**Which side reports the KM state.** `SRTO_RCVKMSTATE` and
`SRTO_SNDPEERKMSTATE` are both readable on both peers and, at this release,
report the same value on both. The groups read both, on both sides.

**Both KM options are readable before there is a connection.** `API.md` marks
`SRTO_RCVKMSTATE` and `SRTO_SNDPEERKMSTATE` `post`, which describes when they
become *meaningful*, not when they become *legal*: a get on a socket that has
been created and never bound, never connected and never given a passphrase
must **succeed**, storing four bytes, and report `SRT_KM_S_UNSECURED` — the
state `API.md` glosses as "data not encrypted". `kmstate` reads
`SRTO_RCVKMSTATE` on exactly such a socket, and tries `SRTO_KMSTATE` on it,
before it reads either on any connection. A getter that refuses a KM option
until the handshake has run loses the group.

**`SRTO_KMSTATE` is left open.** There is a defensible reading on which
`srt_getsockflag` refuses `SRTO_KMSTATE` with `SRT_EINVOP`, because the getter
need carry no arm for it at all; but no document in your reading list says that,
and the header lists the option without qualification. The group therefore
accepts **either** behaviour — refused with `SRT_EINVOP`, or reporting the
same value as `SRTO_RCVKMSTATE` — and fails only if `SRTO_KMSTATE` succeeds
and contradicts `SRTO_RCVKMSTATE`. Implement whichever you prefer.

**A mismatched passphrase does not refuse the connection at this milestone.**
The handshake completes, the socket reaches `SRTS_CONNECTED`, and the failure
is reported only through the KM state and through payload that never arrives.
Refusing the connection is a later milestone's behaviour and fails here.

**Passphrase length is not validated at this milestone.** Lengths outside the
documented 10–79 range are accepted. No group asserts either the acceptance or
a rejection, so either choice passes.

**Loss recovery needs too-late drop switched off.** The verifier's channel
drops datagrams at fixed indexes. A group that requires the payload to arrive
complete sets `SRTO_TLPKTDROP=0` and a generous `SRTO_LATENCY` first. A group
that wants to observe the drop instead leaves both options alone and relies on
their defaults — `SRTO_TLPKTDROP` true, as `API.md` line 486 documents it, and
`SRTO_LATENCY` at the 120 ms corrected above. That reliance is deliberate, and
it is one-sided: the case asserts only that *some* messages went missing and
that `pktRcvDropTotal` rose, against a relay adding 150 ms each way, so it
holds for any small default latency. Nothing anywhere reads either default
back through `srt_getsockflag`.

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
| `SRTO_CONNTIMEO` | **required to compile** |
| `SRTO_EVENT` | **required to compile** |
| `SRTO_FC` | **required to compile** |
| `SRTO_KMSTATE` | **required to compile** |
| `SRTO_LATENCY` | **required to compile** |
| `SRTO_LINGER` | **required to compile** |
| `SRTO_MAXBW` | **required to compile** |
| `SRTO_MSS` | **required to compile** |
| `SRTO_NAKREPORT` | **required to compile** |
| `SRTO_PASSPHRASE` | **required to compile** |
| `SRTO_PBKEYLEN` | **required to compile** |
| `SRTO_RCVBUF` | **required to compile** |
| `SRTO_RCVDATA` | **required to compile** |
| `SRTO_RCVKMSTATE` | **required to compile** |
| `SRTO_RCVPBKEYLEN` | **required to compile** |
| `SRTO_RCVSYN` | **required to compile** |
| `SRTO_RCVTIMEO` | **required to compile** |
| `SRTO_RENDEZVOUS` | **required to compile** |
| `SRTO_REUSEADDR` | **required to compile** |
| `SRTO_SENDER` | **required to compile** |
| `SRTO_SNDBUF` | **required to compile** |
| `SRTO_SNDPBKEYLEN` | **required to compile** |
| `SRTO_SNDPEERKMSTATE` | **required to compile** |
| `SRTO_SNDSYN` | **required to compile** |
| `SRTO_SNDTIMEO` | **required to compile** |
| `SRTO_STATE` | **required to compile** |
| `SRTO_TLPKTDROP` | **required to compile** |
| `SRTO_TSBPDMODE` | **required to compile** |
| `SRTO_UDP_RCVBUF` | **required to compile** |
| `SRTO_UDP_SNDBUF` | **required to compile** |
| `SRTS_BROKEN` | **required to compile** |
| `SRTS_CLOSED` | **required to compile** |
| `SRTS_CLOSING` | **required to compile** |
| `SRTS_CONNECTED` | **required to compile** |
| `SRTS_CONNECTING` | **required to compile** |
| `SRTS_INIT` | **required to compile** |
| `SRTS_LISTENING` | **required to compile** |
| `SRTS_NONEXIST` | **required to compile** |
| `SRTS_OPENED` | **required to compile** |
| `SRT_ECONNSOCK` | **required to compile** |
| `SRT_EINVOP` | **required to compile** |
| `SRT_EINVPARAM` | **required to compile** |
| `SRT_EINVPOLLID` | **required to compile** |
| `SRT_EINVSOCK` | **required to compile** |
| `SRT_ELARGEMSG` | **required to compile** |
| `SRT_ENOCONN` | **required to compile** |
| `SRT_ENOLISTEN` | **required to compile** |
| `SRT_EPOLL_IN` | **required to compile** |
| `SRT_EPOLL_OUT` | **required to compile** |
| `SRT_ERROR` | **required to compile** |
| `SRT_ESUCCESS` | **required to compile** |
| `SRT_ETIMEOUT` | **required to compile** |
| `SRT_EUNBOUNDSOCK` | **required to compile** |
| `SRT_INVALID_SOCK` | **required to compile** |
| `SRT_KM_S_BADSECRET` | **required to compile** |
| `SRT_KM_S_NOSECRET` | **required to compile** |
| `SRT_KM_S_SECURED` | **required to compile** |
| `SRT_KM_S_SECURING` | **required to compile** |
| `SRT_KM_S_UNSECURED` | **required to compile** |
| `SRT_SOCKOPT` | **required to compile** |
| `SRT_SOCKSTATUS` | **required to compile** |
| `SRT_TRACEBSTATS` | **required to compile** |
| `SRT_TRACEINFO` | **required to compile** |
| `byteRecvTotal` | **required to compile** |
| `byteSentTotal` | **required to compile** |
| `msRcvTsbPdDelay` | **required to compile** |
| `pktRcvDropTotal` | **required to compile** |
| `pktRcvLossTotal` | **required to compile** |
| `pktRcvUndecrypt` | **required to compile** |
| `pktRecv` | **required to compile** |
| `pktRecvTotal` | **required to compile** |
| `pktRetransTotal` | **required to compile** |
| `pktSent` | **required to compile** |
| `pktSentTotal` | **required to compile** |
| `srt_accept` | **required to compile** |
| `srt_bind` | **required to compile** |
| `srt_bstats` | **required to compile** |
| `srt_clearlasterror` | **required to compile** |
| `srt_close` | **required to compile** |
| `srt_connect` | **required to compile** |
| `srt_epoll_add_ssock` | **required to compile** |
| `srt_epoll_add_usock` | **required to compile** |
| `srt_epoll_create` | **required to compile** |
| `srt_epoll_release` | **required to compile** |
| `srt_epoll_remove_usock` | **required to compile** |
| `srt_epoll_update_ssock` | **required to compile** |
| `srt_epoll_wait` | **required to compile** |
| `srt_getlasterror` | **required to compile** |
| `srt_getlasterror_str` | **required to compile** |
| `srt_getsockflag` | **required to compile** |
| `srt_getsockname` | **required to compile** |
| `srt_getsockstate` | **required to compile** |
| `srt_listen` | **required to compile** |
| `srt_perfmon` | **required to compile** |
| `srt_recvmsg` | **required to compile** |
| `srt_sendmsg` | **required to compile** |
| `srt_setsockflag` | **required to compile** |
| `srt_strerror` | **required to compile** |

### Constants whose numeric value the verifier pins

No group in this step asserts the numeric value of any public
constant, so you may number the enumeration as you like.

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
