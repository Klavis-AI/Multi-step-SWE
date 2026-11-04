# Test interface — milestone 2 of 11

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
10 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.3.0**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.3.0:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `3` |
| `SRT_VERSION_PATCH` | `0` |
| `SRT_VERSION_STRING` | `"1.3.0"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.3.0)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.3.0 is acceptable.

No verifier translation unit at this milestone compiles conditionally
on `SRT_VERSION_VALUE`, so nothing here turns on the version alone.
The macros are part of the installed interface either way, and the
header must report this milestone's version.


---

## 2. How the work is checked

There are 3 **gate** groups (§3) and 10 **requirement** groups
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
- `recovery` — datagrams dropped at fixed indexes are retransmitted and the payload is delivered complete and in order when too-late drop is disabled, with the sender's retransmission counter and the receiver's loss counter both showing that the loss really happened
- `encryption` — with a matching passphrase and an explicitly configured key length the payload is carried intact at both key lengths and does not appear in clear in the datagrams on the wire, while two different passphrases deliver nothing to the application, and the receiver's keying state reaches the secured value in the matching case

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
| 1 | `version` | a fresh socket reports its own protocol version in the documented three-byte form and zero for the peer's, accepts a set of that option while unconnected and refuses it once connected, reports the minimum-peer-version option at a non-zero default no greater than its own version, and reports the peer's version as equal to its own the moment `srt_connect` returns, before any payload has crossed |
| 2 | `latency` | the receive latency and the peer latency are separately settable on an unconnected socket and both read back zero there, and after a connection in which one side asks for a receive latency and the other proposes a larger peer latency the receiving side reads back the larger of the two rather than the value it configured, with the combined latency option reporting that same negotiated figure |
| 3 | `streamid` | a stream id set on a caller before connect reads back on that socket with its byte length reported through the option length, reads back with the same bytes on the socket `srt_accept` returns, reads back empty on the listener that accepted it, and a value longer than the documented limit is refused rather than truncated |
| 4 | `entrypoints` | each of the eight entry points this milestone adds is resolvable in the installed library, the parameterless socket constructor among them yields a socket that binds, listens, accepts and carries a message end to end, and the general socket constructor accepts a transmission-type argument it does not use instead of rejecting it |
| 5 | `transtype` | selecting the file transmission type on an unconnected socket turns off timestamp-based delivery, too-late packet drop and periodic loss reports, takes the socket off the message API, sets the declared payload size to zero and the congestion-control name to the file one, and selecting the live type again restores every one of those to its live value |
| 6 | `payloadsize` | the declared payload size reads back at its live default, accepts any value up to the largest payload that fits in one path MTU, refuses a larger one as a bad parameter, and refuses any set once the socket is connected |
| 7 | `msgsizing` | in live mode a send longer than the declared payload size and a receive into a buffer shorter than it are both refused with the same message-API error rather than accepted or truncated, a receive given a zero-length buffer is refused as a bad parameter, and a send and a receive that respect the declared size still carry the payload intact |
| 8 | `optslots` | a get of an option id this specification does not define is refused as an unsupported operation while a set of that same id is refused as a bad parameter, the two option ids the previous milestone defined and this one removes outright answer both calls the same way, and a get of either of the two per-direction key-length options that the previous milestone defined is refused as an unsupported operation |
| 9 | `stats` | the byte-statistics call this milestone adds reports, with its instantaneous flag clear, the same cumulative packet and byte counters as the established statistics call does for the same connection, and both report the maximum bandwidth in force at the default bandwidth setting as the documented infinite-bandwidth ceiling rather than as a negative number |
| 10 | `endtoend` | a live stream of 4000 messages crosses a clean relay and arrives byte-identical, in order, with no gaps, and a second stream of the same length sent back down that same connection -- from the side that accepted it to the side that declared itself the sender -- arrives byte-identical and in order as well, leaving both sockets connected |

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

`srt-docs/` is the same pinned tree milestone 01 shipped, and `PROVENANCE.md`
still publishes a 16-hex prefix of each file's sha256 — but it is pinned to *this*
milestone's tag, so its option table now describes this milestone's surface
rather than a superset of the one before it. That makes it far more useful
here and also more dangerous: several rows describe options this milestone
introduces, and several of those rows are wrong. Everything below was checked
against the release this milestone targets. **Where this section and
`srt-docs/` disagree, this section is right.**

The four printing errors §5 corrected at milestone 01 are still in the same
document, unedited, and this milestone's programs still depend on the
corrections: `srt_close` returns `int`; `srt_sendmsg` takes five parameters
with an `int inorder` and no `srctime`; the four option accessors return `int`
and take `int* optlen`, never a C++ reference; the epoll event bits are
`SRT_EPOLL_IN`/`SRT_EPOLL_OUT`/`SRT_EPOLL_ERR`. One usage error is new with
this milestone's text: the `SRT_MSGCTRL` example spells the object
`srt_msgctl_default`, without the `r`, in both places it appears — the
released header, and the name table below, spell it `srt_msgctrl_default`, and
a snippet copied from the example does not compile. A name the verifier cannot
resolve is a failed build, not a failed group.

### The option table is wrong in five places

**`SRTO_MINVERSION` is readable, and its default is not zero.** The table marks
it `(writeonly)` with a default of `0`. Both are wrong. A get succeeds at every
socket state and a fresh socket reports `0x010000` — the major-version floor,
not zero and not the socket's own version. `version` asserts that the
default is non-zero and does not exceed what `SRTO_VERSION` reports, so a
candidate that follows the table fails twice: once for refusing the get and
once for the value. What a *set* of `SRTO_MINVERSION` reads back afterwards is
not specified here and no group asserts it: `version` asserts only the
non-zero default, that it does not exceed `SRTO_VERSION`, and that the get
succeeds -- including on a connected socket.

**`SRTO_VERSION` accepts a set before connect.** The table marks it `(r)` and
says only that "set could eventually be supported to test". It is supported
here: a set succeeds while the socket is unconnected and is refused with
`SRT_ECONNSOCK` once it is connected, like every other pre-connect option.

**A qualification to that last sentence — `SRTO_RCVSYN` and `SRTO_SNDSYN` are
the exception to that refusal.**
`srt-docs/` types both as `pre`, but the released library accepts a set of
either at **any** state — including on a connected socket and on the socket
`srt_accept` returned — and the new value takes effect for subsequent calls. A
socket returned by `srt_accept` starts with the listener's value, so a listener
opened non-blocking yields a non-blocking accepted socket whose receive mode
you may then switch back. Refusing the set for these two, on a reading of the
`pre` legend that leaves no exception, loses every group that receives on an
accepted socket.

**`SRTO_VERSION` does not switch to the peer's version after connect.** The
table says it reports "the highest local version supported if not connected, or
the highest version supported by the peer if connected". It reports the local
version at every state; the peer's belongs to `SRTO_PEERVERSION` alone.

**`SRTO_SMOOTHER`, `SRTO_MESSAGEAPI` and `SRTO_PAYLOADSIZE` are readable
despite their `(w)` marking.** All three are marked write-only in the table and
all three answer a get. `transtype` and `payloadsize` read all three;
`SRTO_SMOOTHER` answers with the selected algorithm name as a NUL-terminated
string, writing its length — not counting the terminator — back through
`optlen`.

**`SRTO_TRANSTYPE` is the fourth `(w)` row, and the one that really is
write-only.** It sets the six parameters listed below by selecting a type, but
there is no getter for it at all: `srt_getsockflag(SRTO_TRANSTYPE)` returns
`−1` with `SRT_EINVOP` on a fresh socket and on a socket that has already
accepted a set alike, and it never reports the type in force. That `SRT_EINVOP`
is the getter refusing an operation the option does not have — not a report
that the option id is undefined. A later milestone reads the selected type back
through its effects — it sets the type and reads the parameters the type
drives — and never through this option, so a get of `SRTO_TRANSTYPE` stays
refused for the rest of the chain. The three options above are the whole of the
`(w)` exception to the table's marker: do not extend it to this one.

**`SRTO_TSBPDMODE`'s Default cell reads `false`; the flag is on by default.**
The row at `API.md` line 490 gives the Default as `false` in its structured
cell while the same row's Description says the flag is set to *true* by default
and as a default flag set in live mode. The Description is the true one: on a
fresh socket, and by the live preset on a bound one, the getter answers `true`.
`transtype` reads it back as the first of the six live values, so a reader who
follows the Default cell loses that group. Read the cell as one more printing
error in the same table, not as a default to implement.

### What the latency options actually report

**The latency getters report the value in force, never the value you
configured.** This is the single behaviour most likely to be implemented the
obvious way and be wrong. `SRTO_RCVLATENCY` and `SRTO_PEERLATENCY` set one pair
of fields and read a different pair, and the read pair is refreshed from the
configured pair only when the socket is bound, and replaced by the negotiated
figures when it connects. In full:

| socket state | `SRTO_RCVLATENCY` reports | `SRTO_PEERLATENCY` reports |
|---|---|---|
| created, never bound | `0` | `0` |
| bound, not connected | the configured receive latency | `0` |
| connected | max(own receive latency, peer's proposal) | max(own proposal, peer's receive latency) |

So a fresh socket reads back zero from both however you configure it, and the
value you set becomes visible only once the socket is bound. `latency`
reads the zeros on a socket that has been created and not bound, and reads the
negotiated figures after connect; no group reads either option on a bound
unconnected socket. The max rule itself is documented under `SRTO_RCVLATENCY`;
that these options are where the result surfaces is not.

**`SRTO_LATENCY` is an alias for the pair, in both directions.** A set assigns
both the receive latency and the proposal; a get answers exactly as
`SRTO_RCVLATENCY` answers, including the negotiated value after connect. The
table's `SRTO_LATENCY` row describes only the set.

**The live preset's `SRTO_RCVLATENCY = 120` is not observable as a read-back.**
`srt-docs/` lists that line among the parameters `SRTT_LIVE` sets, and it is
true of the configuration, but on the fresh socket where a group would look for
it the getter still answers `0` by the rule above. No group asserts a latency
value through the transmission-type round trip, and none may.

### Reading a transmission type back

**`SRTO_TLPKTDROP` reports the value in force, refreshed at bind.** Like the
latency pair, the too-late-drop getter reads a field that is copied from the
configured one when the socket is bound; on a created-and-never-bound socket it
answers the live default no matter what transmission type you selected.
`transtype` therefore selects the type on a fresh socket, **binds it**, and
only then reads the six options back, and it uses a separate socket for each
direction of the round trip, because a socket cannot be bound twice. The other
five — `SRTO_TSBPDMODE`, `SRTO_NAKREPORT`, `SRTO_MESSAGEAPI`,
`SRTO_PAYLOADSIZE`, `SRTO_SMOOTHER` — answer from the configured value
immediately.

**Boolean options do not agree on their width.** Some answer a get by writing
four bytes and reporting `optlen` 4; others write one byte and report `optlen`
1. The verifier does not assert which: for every boolean option it zero-fills a
four-byte buffer, passes `optlen` 4, tests the result for non-zero, and accepts
a written length of either 1 or 4. A set is accepted at either width. Choose
whichever you like and be consistent.

### The stream id getter has a length contract

`SRTO_STREAMID` is documented as a string of at most 512 characters, set before
connect and retrieved by the listener from the socket `srt_accept` returns.
What the getter does with `optlen` is not documented and `streamid` asserts
all of it. On the way in, the set takes exactly `optlen` bytes and is refused
with `SRT_EINVPARAM` when `optlen` exceeds 512 — refused, not truncated — and
refused with `SRT_ECONNSOCK` once connected -- though a set on a *listening*
socket, one that is bound and listening but not connected, succeeds and returns
`0` at this milestone, a path no group asserts in either direction. On the way out, the caller must
supply a buffer at least one byte longer than the stored string; a shorter one
is `SRT_EINVPARAM`. The getter writes the string with a terminating NUL and
reports through `optlen` the length **without** it. An unset stream id reads
back as an empty string with `optlen` `0`, which is what the listening socket
itself reports after it has accepted a connection carrying one: the value rides
the handshake to the accepted socket and is never stored on the listener.

### The eight entry points

Four of these appear nowhere in `srt-docs/` and four are named without a
declaration. `entrypoints` requires every one of them to be present and
callable in the installed library, and requires `srt_create_socket` to produce
a socket that binds, listens, accepts and carries a message. Nothing else here
is asserted about their behaviour, but all eight must compile and link exactly
as written:

    SRTSOCKET srt_create_socket(void);
    int       srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);
    int       srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name,  int local_namelen,
                                          const struct sockaddr* remote_name, int remote_namelen);
    int64_t   srt_sendfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
    int64_t   srt_recvfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
    void      srt_msgctrl_init(SRT_MSGCTRL* mctrl);
    int       srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS* perf, int clear, int instantaneous);
    int       srt_getsndbuffer(SRTSOCKET sock, size_t* blocks, size_t* bytes);

`srt_create_socket` takes no arguments and chooses the address family itself —
in this release `AF_INET`, the family every example in `srt-docs/` uses, so a
socket it returns binds an ordinary IPv4 `sockaddr_in` with no further setup.
`srt_msgctrl_init` fills its argument with the same defaults as the
`srt_msgctrl_default` object, which must also exist: `msgttl` `-1`, everything
else zero. `entrypoints` asserts both — it calls `srt_msgctrl_init` on a
structure it has filled with a non-zero pattern first, so a function that
writes nothing fails, and it reads the fields of `srt_msgctrl_default` itself.
`srt_msgctrl_default` is therefore a **link** dependency of the whole `appapi`
family, exactly as the eight functions above are: it has to be an object with
external linkage, declared in `<srt/srt.h>` and defined in `libsrt.so`, not a
macro and not a header-only constant. The field names the assertions use are
`srt-docs/API.md`'s own — `flags`, `msgttl`, `inorder`, `boundary`, `srctime`,
`pktseq`, `msgno` — and nothing here depends on the structure's padding or on
the order the fields are declared in. `srt_getsndbuffer` is the one entry
point here that does not follow
the `0`/`SRT_ERROR` convention — it accepts `NULL` for either output pointer
and returns the send buffer's timespan in milliseconds, or `-1` on a socket
that has no send buffer — and no group depends on the distinction.

`srt_socket`'s own type and protocol arguments being ignored *is* documented;
`entrypoints` asserts it, and the documentation is right about it.

### Message sizing refusals

`SRT_EINVALMSGAPI` is named in no shipped document and `msgsizing` asserts
it by identity. It is the refusal the live transmission type raises when a
transfer call does not match the declared payload size. Under the live type it
is the *only* size refusal there is:

| call | outcome |
|---|---|
| `srt_sendmsg` with `len` above the declared payload size | `SRT_ERROR` / `SRT_EINVALMSGAPI` |
| `srt_recvmsg` with `len` below the declared payload size | `SRT_ERROR` / `SRT_EINVALMSGAPI` |
| `srt_recvmsg` with `len` of `0` | `SRT_ERROR` / `SRT_EINVPARAM` |
| `srt_sendmsg` with `len` of `0` | returns `0`; not an error |

**There is no size above which a different code appears.** `SRT_ELARGEMSG`
(5012) is declared in the public header at this milestone and no live-mode send
reaches it: the live smoother's size check refuses the message with
`SRT_EINVALMSGAPI` before any buffer-size guard can fire, at every magnitude a
probe uses. (`srt-docs/API.md`'s own prose does document a buffer-size refusal
for a message above the whole send buffer, and the release does keep that guard
— it simply sits behind the live-mode check and no group can reach it.) A live
`srt_sendmsg`
of 1400, 5000, 100000 or even 10000000 bytes is refused with
`SRT_EINVALMSGAPI` (5009), exactly like a one-byte overshoot. Do not implement
a second threshold: a candidate that returns 5012 for a very large send is
wrong about this milestone, and `msgsizing` compares the number.

**The names behind those numbers.** The public header declares the live
payload constants, the live default latency and the transmission-type
enumeration, and no shipped document names any of them:
`SRT_LIVE_DEF_PLSIZE` = 1316 (the live `SRTO_PAYLOADSIZE` default),
`SRT_LIVE_MAX_PLSIZE` = 1456 (MTU 1500 - UDP 28 - SRT 16) and
`SRT_LIVE_DEF_LATENCY_MS` = 120 (the live latency the preset sets), all three
`static const int`; and `SRT_TRANSTYPE`, a `typedef enum` whose members are
`SRTT_LIVE`, `SRTT_FILE` and `SRTT_INVALID`, unnumbered -- the two profiles are
the `0` and `1` of the transmission-type exception the constants section below
names, and `SRTT_INVALID` is a third member, distinct from both. No group at
this milestone requires you to expose any of them -- the verifiers here use the
numbers and pass the transmission type as an `int` -- but a header that omits
them is not this milestone's header, and two later milestones name them with no
fallback: milestone 07 pins `SRT_LIVE_DEF_LATENCY_MS` at 120 and compares the
latency getters against it, and milestone 08 pins the three enumerators as
distinct from one another. The numbers are given here so you need not infer
them from the option table.

### One error name changed spelling, and the old one is gone

`SRT_ESUCCESS` -- the zero of milestone 01's error enumeration, which step 01's
own section 5 publishes -- **does not exist at this milestone**. It is spelled
`SRT_SUCCESS` here, defined as `MJ_SUCCESS`, and the old name is *removed*
rather than kept as an alias. No group names either symbol and nothing checks
the spelling, so keeping `SRT_ESUCCESS` as an additional alias is
harmless. It is recorded because it is the one identifier in this transition
that a straight carry-forward of your milestone-01 tree keeps under a name this
milestone deleted.

**Nine error names the three families spell are named by no document.** Each of
this step's families carries a local error-name helper, so that a failure line
prints `SRT_ECONNLOST` rather than a bare number, and each helper spells its
codes with the library's own enumerators and with no `#ifndef` fallback: a
`<srt/srt.h>` that does not declare every name one of them spells fails to
build that family, and every group in it is lost, gates included. Most of what
the three helpers spell is already required of your header: nine of the names
are codes milestone 01's section 5 table lists, two are named in this section
(`SRT_SUCCESS` and `SRT_EINVALMSGAPI`), and one, `SRT_ECONGEST`, is named in
passing by `srt-docs/API.md`. These nine are named by no document at either
milestone:

`SRT_EBOUNDSOCK`, `SRT_EASYNCRCV`, `SRT_EASYNCSND`, `SRT_ECONNFAIL`,
`SRT_ECONNLOST`, `SRT_ECONNREJ`, `SRT_ECONNSETUP`, `SRT_ENOSERVER`,
`SRT_EPEERERR`.

Your header must declare all nine, spelled exactly so. They are members of the
released error enumeration -- the bound-socket refusal, the non-blocking send
and receive outcomes, and the connection-setup, no-server, rejection,
connection-failure, connection-loss and peer-error codes -- and this paragraph
is the only place their spelling is written down: no group at this milestone
asserts any of their values, and no behavioural requirement attaches to them
here beyond the declaration. One thing
the helpers do impose on the values, by construction: two of the three are
`switch` statements, so two of these names that share a value inside one
helper's list are a compile error for that family rather than a case failure.
Give each a value distinct from the other names in its helper's list.

A receive buffer exactly equal to the declared payload size is accepted; the
comparison is strict. All four refusals are raised only on a connected socket
and only under the live transmission type — the file type imposes no size
restriction in either direction — and the size they compare against is the
declared payload size that was in force when the connection was made.

### Option slots this specification does not define

**The refusal is asymmetric, and no document says so.** A get of an option id
this specification does not define is refused as an unsupported operation,
`SRT_EINVOP`; a set of the same id is refused as a bad parameter,
`SRT_EINVPARAM`. `optslots` probes the literal id `99`, cast to the option
type — your enumeration must leave `99` unassigned.

**Two option ids the previous milestone defined are removed outright.** Slots
`32` and `37` were live set-and-get slots at milestone 01, where all four calls
succeeded. At this milestone they are gone, and they answer exactly like a slot
this specification never defined: a get is `SRT_EINVOP`, a set is
`SRT_EINVPARAM`. `optslots` probes both by number, cast to the option type,
for the same reason it probes `99` that way — what is asserted is the behaviour
of the slot and not the spelling of any constant. The released header does keep
deprecated aliases at those two numbers, but **no group requires you to declare
them**, and nothing here compiles a name for either slot.

**The two retired per-direction key-length options must still be nameable.**
`SRTO_SNDPBKEYLEN` and `SRTO_RCVPBKEYLEN` were readable at milestone 01 and are
not readable here, but a program that names them must still compile: they have
to remain declared constants of the option type, and a reference to one must
not be an error even if you mark it deprecated. `optslots` asserts only
that a **get** of either is refused with `SRT_EINVOP`. What a set of either
does is deliberately unasserted — the released library accepts one and refuses
the other — so implement whichever you prefer.

### Statistics

**`srt_bistats` is `srt_bstats` with one extra argument, and the cumulative
counters are the same numbers.** Both fill the same structure from the same
connection state. The `instantaneous` flag changes only the buffer-occupancy
fields, which are otherwise moving averages; it does not touch any `*Total`
counter. `stats` asserts both halves of that on a quiesced connection: with
the flag **clear**, `srt_bistats` reports the same four cumulative counters as
`srt_bstats`, and with the flag **set** it reports those same four numbers
again. What the flag does move is deliberately not asserted — those fields are
averages, and no document fixes a value for them — so the only obligation the
flag carries is that it leaves the cumulative counters alone. A non-zero
`clear` still resets the interval counters and leaves the cumulative ones
alone, exactly as at milestone 01.

**`mbpsMaxBW` reports the ceiling in force, and at the default bandwidth
setting that ceiling is 30.** The field appears in no shipped document. With
the maximum-bandwidth option at its `-1` default, a *connected* socket reports
`30.0` — the infinite-bandwidth ceiling, 30 Mbps expressed in Mbps — and not a
negative number and not zero. On an unconnected socket it reports `0`, because
no congestion control is in force yet; `stats` reads it only on a connected
socket. With the option set to a positive value the field reports that value
converted to Mbps.

### Both directions on one connection

`srt-docs/` says the sender flag is superfluous between two peers that both
implement this milestone, and that the extension exchange happens inside the
handshake. It does not say what happens to data arriving at a socket that set
the flag, and `endtoend` asserts it: the data is delivered to the
application and the connection stays up. That group sends its forward stream from
the caller, which set the sender flag, then sends a second stream of the same
length from the accepted socket back to that same caller, on the same
connection, with no further configuration on either side — no second connect,
no second passphrase, no second latency setting. Both sockets must still be
connected when the reverse stream has been read.

**`SRTO_PEERVERSION` is filled in by the time `srt_connect` returns.** The
table says the option reports `0` when not connected or when the handshake has
not been performed, which leaves open when it stops being `0`. It stops when
the connection completes: `version` reads it on the caller the instant
`srt_connect` returns success, before any payload has crossed in either
direction, and requires the peer's version there. Learning the peer's version
from the first data exchange instead is a milestone-01 behaviour and fails
here.

### Carried over from milestone 01

**A passphrase alone now encrypts, and at milestone 01 it did not.** This is
the one item in this section that did *not* carry over; it is listed here
because an implementation that carries milestone 01's rule forward by inertia
gets it wrong. At milestone 01 the sender built a crypto context only when the
key length was already greater than zero, so a passphrase with `SRTO_PBKEYLEN`
left at its default of `0` encrypted nothing at all. Here, an initiator that
has a passphrase and has left `SRTO_PBKEYLEN` at `0` is given a **16-byte key
by default**: the zero is substituted at connection setup rather than read as
"encryption off". No group checks that substitution, so it is not a requirement
to chase -- it is here so that you do not write a guard that refuses to encrypt
when the length is zero. `encryption` sets the key length explicitly to 16
and to 32 on the sending side because it asserts both lengths, not because the
passphrase would otherwise be inert.

**The key exchange is still driven by traffic, not by the handshake.** Folding
the extension exchange into the handshake does not move the key material into
it. On an idle connection the keying state never leaves its unsecured value
however long you wait, and the first messages after connect are sent before the
exchange settles. `encryption` drives it with tagged warm-up traffic, skips
that traffic in the receive loop, and waits on the observed state rather than
on a timer.

**Loss recovery still needs too-late drop switched off.** `recovery` sets
it off and a generous latency before asking for a complete payload, and reads
the sender's retransmission counter and the receiver's loss counter to confirm
the loss happened.

**The negotiated delay is now readable in a second place.** At milestone 01
the figure actually in force appeared only in `SRT_TRACEBSTATS::msRcvTsbPdDelay`
on the receiver, and it still does. What this milestone adds is
`SRTO_RCVLATENCY` on a connected socket, by the table above. That read-back is
genuinely required here, and `latency` is the group that asserts it: the
receiving side must report the LARGER of the two figures -- the one it asked
for and the peer latency the other side proposed -- rather than the value it
configured itself, with the combined `SRTO_LATENCY` reporting that same
negotiated number.

Note that nothing in this milestone checks `msRcvTsbPdDelay`. Reporting a
plausible figure there does not substitute for the option read-back, and the
statistic is not a requirement here.

### Stated in `srt-docs/` but out of scope at this milestone

Three behaviours the documents state are carried by no group here. They are
recorded so that their absence from the verifier is not read as an omission to
fix, and so that you are not misled into thinking something is measured:

* the minimum-version **rejection** — `srt-docs/` says a connection to a peer
  that does not satisfy `SRTO_MINVERSION` is refused; no group configures a
  mismatched minimum version, and what a *set* of `SRTO_MINVERSION` reads back
  is likewise unasserted;
* the smoother **mismatch rejection** — `srt-docs/` says the smoother type must
  match on both peers or the connection is rejected; no group connects two
  peers with different smoothers;
* the post-connect refusal of `SRTO_TRANSTYPE` — its `pre` row implies one, and
  no group sets the transmission type on a connected socket.

Two more surfaces the documents describe are carried by no group here: the
installed **version macros** of §1 (`<srt/version.h>`, reachable through
`<srt/srt.h>`) are required of the header you install, and no translation unit
reads them back, so a tree that installs the wrong value there is not
measured at this milestone; and `SRTO_ISN`, listed in the option table as new
at this release, is not touched by any group.

Implement them as the documents describe if you like; nothing checks either the
behaviour or its absence at this milestone.

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
| `SRTO_MESSAGEAPI` | **required to compile** |
| `SRTO_MINVERSION` | **required to compile** |
| `SRTO_PAYLOADSIZE` | **required to compile** |
| `SRTO_PEERLATENCY` | **required to compile** |
| `SRTO_RCVLATENCY` | **required to compile** |
| `SRTO_SMOOTHER` | **required to compile** |
| `SRTO_STREAMID` | **required to compile** |
| `SRTO_TRANSTYPE` | **required to compile** |
| `SRT_EINVALMSGAPI` | **required to compile** |
| `SRT_SUCCESS` | **required to compile** |
| `srt_bistats` | **required to compile** |
| `srt_connect_debug` | **required to compile** |
| `srt_create_socket` | **required to compile** |
| `srt_getsndbuffer` | **required to compile** |
| `srt_msgctrl_default` | **required to compile** |
| `srt_msgctrl_init` | **required to compile** |
| `srt_recvfile` | **required to compile** |
| `srt_rendezvous` | **required to compile** |
| `srt_sendfile` | **required to compile** |

### Constants whose numeric value the verifier pins

No group in this step asserts the numeric value of any public
constant, so you may number the enumeration as you like — subject to the two
exceptions the sections above state: the transmission type reaches the verifier
as the plain ints `0` and `1`, and the option slot `99` must stay unassigned
while slots `32` and `37` keep answering as undefined slots do. Every other
number is yours.

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
