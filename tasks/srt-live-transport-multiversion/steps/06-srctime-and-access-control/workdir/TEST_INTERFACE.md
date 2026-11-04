# Test interface — milestone 6 of 11

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
`<srt/srt.h>` and `<srt/access_control.h>`, and linking `-lsrt` together with the system thread
and dynamic-loader libraries -- nothing else of yours. If either build fails, every group
below fails at build time; the scorecard says so in `build_note`, so a build
failure is never mistaken for a step's worth of missing capabilities.

Your build gets 900 s; the verifier's own programs get 300 s. Reference
timing on this image, for the released library at this milestone: about
12 s, estimated from the neighbouring milestone's idle reference rather than timed directly, because every sample taken of this step was taken under load. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.4.2**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.4.2:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `4` |
| `SRT_VERSION_PATCH` | `2` |
| `SRT_VERSION_STRING` | `"1.4.2"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.4.2)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.4.2 is acceptable.

This is not bookkeeping at this milestone. The verifier compiles
`fam_access.cpp`, `fam_live.cpp`, `fam_msgctrl.cpp` conditionally on `SRT_VERSION_VALUE`, so a tree that
implements every behaviour this document asks for and leaves the version
where the previous milestone left it skips those compile-time pins and runs the
drivers' guarded restatements instead. You will not see a compile error, and
nothing about the below-expected branch is a trap: put the constants where
"Constants whose numeric value the verifier pins" says and both branches agree
with your library, whether or not you bump the version.


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
- `recovery` — with too-late packet drop switched off before bind and a receive latency large enough to cover the path, a message stream whose datagrams are dropped at fixed indexes arrives complete and in order, and the statistics call reports a non-zero cumulative receive-loss count on the receiver and a non-zero cumulative retransmitted-packet count on the sender, so the loss is known to have happened rather than to have been absent
- `tsbpd` — with timestamp-based delivery enabled and the same receive latency configured on both sides before bind, the receiver reports that same figure as the delay actually in force, and the stream still arrives complete and in order
- `fec` — with the same well-formed packet-filter configuration string set on both ends before bind and datagrams dropped at fixed indexes, the stream arrives complete and in order without any retransmission being required, and the statistics call reports a non-zero cumulative count of extra packets generated by the filter on the sender

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
| 1 | `msgctrl-defaults` | the message-control initialiser leaves the packet-sequence and message-number fields at -1 rather than 0, leaves the time-to-live field at the infinite-lifetime value, the in-order flag clear and the boundary field at the subsequent-packet value, and does all of this without a socket existing |
| 2 | `srctime-writeback` | a message sent through the control-block send call on a connected live socket with the source-time field left at 0 returns the payload length and comes back with a non-zero source time and a non-negative packet sequence number written into the caller's own control block, so a sender can learn the timestamp and sequence the library assigned to the message it just handed over |
| 3 | `srctime-bound` | the same send call refuses with the message-API error, and transmits nothing, when the caller supplies a source time earlier than the moment the connection was established, while a source time taken at or after that moment is accepted and the payload arrives intact |
| 4 | `send-accounting` | with a reorder tolerance configured before bind and a fixed number of live messages carried over a clean loopback connection, the statistics call reports that configured tolerance as the receiver's current tolerance, reports the listener's configured tolerance on a socket accepted from it while the caller that configured none reports zero, and reports a count of originally-sent packets equal to the number of messages sent, distinct from the total sent count that also includes retransmissions |
| 5 | `hook-reject-reason` | a listener whose registered access-control hook returns a negative value refuses the connection, the caller's connect fails, and the caller's rejection reason reads as unknown rather than as whatever value that socket's rejection reason held before the attempt, so the refusal is reported through the hook path and not left at a stale value |
| 6 | `app-reject-reason` | a hook that sets a rejection code in the application-defined range on the socket being accepted and then returns a negative value causes the caller to read back that exact code rather than a code clamped into the library's own small set, and the call that sets it refuses a code below the application range as a bad parameter and an invalid socket as a bad socket, while accepting the lowest code in that range |
| 7 | `connect-timeout` | a caller with a connect timeout configured that connects to a loopback port nothing is listening on fails no sooner than that timeout, fails with the no-server error, and reports a rejection reason that names the timeout specifically rather than the unknown reason, and raising the configured timeout lengthens the wait by a comparable amount, so an internal interval that ignores the option is caught |
| 8 | `entry-points` | the installed library resolves by name, at run time, the four entry points that create a socket group, connect it, read its member data and report a socket's group, and resolves the renamed call that binds a socket to an existing system socket, while the previous milestone's name for that same call is no longer an exported symbol, and the access-control header naming the application rejection codes is installed alongside the main header |
| 9 | `endtoend` | a live stream of 4000 messages configured entirely before bind -- timestamp-based delivery on, a receive latency, too-late drop off and periodic loss reports on -- crosses a clean relay and arrives byte-identical, in order, with no gaps and both sockets still connected, and on a separate socket that has been bound but neither connected nor listening all six of the options section 5 lists as refused after bind -- timestamp-based delivery, the overall latency, the receive latency, the peer latency, too-late drop and periodic loss reports -- are refused with SRT_EBOUNDSOCK and each leaves the option reporting the value it held before the refused call |

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

**A missing name this milestone introduces breaks the whole family that uses
it, not one group.** The verifier is not one program but one per *family* of groups,
and each family is a single translation unit compiled once; every group in §4
belongs to exactly one of them. If a name the list below marks **required to
compile** is absent from the headers you install, that program does not build
and every group it carries fails -- more than the one group that wanted the
name. The statement in §2, and in `instruction.md` §3, that one group's
failure does not affect another describes a group that *runs* and fails; it is
not a promise about a family that never compiles. So declare the names below even
where you cannot implement the behaviour behind them: a correctly spelled
declaration with nothing behind it turns a whole-family loss into a single
failed group.

**A missing name can fail a requirement group; it can never fail the gate.** Three of
§3's four gate entries — `recovery`, `tsbpd` and `fec` — are carried by driver
code, and all three sit in a single family; the fourth, `proof-of-life`, is the
build-and-install check and compiles none of this. That family's translation
unit is deliberately written to compile against the **previous** milestone's
headers: it names none of the identifiers listed as required to compile below,
and restates none of them. `srt_connection_time()` would have
been the natural way to write one of its cases and is reached through a
run-time symbol lookup with a bounded fallback instead, precisely so that the
name never enters that family's link line. A header missing one of the names
below therefore fails the groups of the family that wanted it, and cannot
touch the gate.

Where this section and `srt-docs/` disagree, this section is right. The
documents under `srt-docs/` are pinned to this milestone and they are mostly
accurate, but on six points a reading faithful to their text produces a
failing implementation: five of them describe the behaviour this milestone is
replacing rather than the behaviour the groups assert, and the sixth describes
a different clock from the one a caller reads back. Those six are listed
first.

### The pinned documents are wrong in six places

**1. `srt_bind_peerof` is not a function of the library.** `API.md`, in the
connection-setup synopsis, still lists

    int srt_bind_peerof(SRTSOCKET u, UDPSOCKET udpsock);

alongside `srt_bind`. In this milestone that name must not be an exported
symbol of the shared library. It survives only as a deprecated inline
forwarder declared in the public header, so code that calls it still compiles
and still works, while a run-time lookup of the name in the loaded library
finds nothing. `srt_bind_acquire`, with the same signature, is the exported
function and `API-functions.md` documents it correctly. A group opens the
installed library and requires the lookup of the old name to fail and the
lookup of the new one to succeed.

**2. `pktseq` is not "unused for sending".** `API-functions.md` describes the
`SRT_MSGCTRL` field as "Receiver only. Reports the sequence number for the
packet carrying out the payload being returned", the struct listing comments it
"(unused for sending)", and `API.md`'s field list for `srt_sendmsg2` says
"`pktseq`: unused". All three are wrong here. On a successful
`srt_sendmsg2` the library writes back into the caller's block:

* `srctime` — the microsecond timestamp the message was actually stamped with,
  which is non-zero even when the caller passed zero, and which is measured
  from the **UNIX epoch** (see point 6 below);
* `pktseq` — the sequence number of the first packet of the message, a
  non-negative value.

The block is an in/out parameter on send. `msgno` is also written back, but it
was written back before this milestone too, so it is not the thing being
asserted — do not treat `msgno` as evidence either way.

**3. The pre-set rejection code is not `SRT_REJ_RESOURCE`.** `AccessControl.md`
says "In a normal situation, the rejection code is predefined as
`SRT_REJ_RESOURCE`." In this milestone the accepted socket is pre-seeded with
`SRT_REJ_UNKNOWN` (0) before the listener hook runs, and whatever the hook
leaves there is what is sent to the calling peer. So a hook that returns −1
without calling `srt_setrejectreason` refuses the connection carrying the code
**0**, and never 3. This is the single most commonly mis-implemented point in
this milestone and two groups depend on it.

What the *caller* then reads back is a separate question, and the answer is not
0. The refusal travels in the handshake's request-type field as
`1000 + code`, and the caller treats that field as carrying a reason only when
it is **strictly greater** than 1000 — so a code of exactly 0 encodes to 1000
and is not recognised as a reason at all. The caller falls through to its
handshake-extension check instead, finds a rejection response with no
extension block, and settles on `SRT_REJ_ROGUE` (4). Every code an application
actually sets is ≥ 1000, encodes above the threshold, and is read back exactly;
0 is the one value the encoding cannot carry.

The group that covers this therefore requires what is stable and behavioural:
the caller's connect fails, and the reason it reads is **not**
`SRT_REJ_RESOURCE` (3) and not the value that socket's reason held before the
attempt. Reading back either 0 or 4 satisfies it. Do not implement 4
deliberately — it is an artefact of the encoding above, not a requirement — and
do not implement 3.

**4. `SRT_REJ_UNKNOWN` is not only "no connection rejected".**
`API-functions.md` describes it as "A fallback value for cases when there was
no connection rejected". For the same reason as the point above, 0 is also what
a genuine refusal carries when the access-control hook named no code, so on the
refusing side the value no longer distinguishes "nothing happened" from
"refused without a reason". On a socket that connected successfully it still
means exactly what the document says, and a group asserts that: a connected
caller's rejection reason reads `SRT_REJ_UNKNOWN`.

**5. The reorder-tolerance decay rate is 50, not 10.** `statistics.md` says
"After 10 consecutive original (not retransmitted) packets come in order, the
reorder distance is decreased by 1 for every such packet." The required
behaviour is: a counter of consecutive in-order original deliveries is
incremented per delivery; when it reaches **50** the counter resets to zero and
the tolerance is decremented by one, if it is above zero. So the tolerance
drops by at most one per fifty in-order deliveries, not by one per delivery
after the tenth. The decay also only runs when the peer advertised the
retransmission flag in the handshake and the packet was delivered in order.

A group reads `pktReorderTolerance` after a short clean transfer and requires
it to equal the configured `SRTO_LOSSMAXTTL`. Implementing the documented rate
instead of the required one makes that read come back low and fails the group.

**6. "SRT internal clock (steady or monotonic)" is not the epoch the stamp you
read back is counted from.** Three places in the pinned corpus describe the
timestamp scale, and all three invite a stamp counted from an arbitrary origin
such as boot:

* `API-functions.md:1231` comments the field itself — `int64_t srctime; //
  source time (microseconds since SRT internal clock epoch)`;
* `API-functions.md:2513` and `:2524` say `srt_time_now` and
  `srt_connection_time` return "microseconds elapsed since epoch using SRT
  internal clock (steady or monotonic clock)";
* `API.md:244` calls the same quantity "the number of usec (since epoch) in
  local SRT clock time".

Those sentences describe the library's *internal* timestamping clock and the
options that select it, not the scale a caller sees. The verifier configures
exactly as §1 shows and passes nothing clock-related, and under that
configuration this milestone's internal clock is the system clock — so the
epoch is the **UNIX epoch**. Required here: `SRT_MSGCTRL.srctime`, the value
`srt_connection_time()` returns, and the host's own wall clock are all
microseconds since 1970-01-01T00:00:00 UTC, and are directly comparable
without conversion. An in-scope group sends with `srctime = 0` and requires the
stamp written back to be **within one hour of the host wall clock read
microseconds later**; another requires the host wall clock to be *after*
`srt_connection_time()` on a socket that has just connected. An
implementation that stamps from a boot-relative steady clock satisfies every
other property of the write-back and still fails both, by an amount equal to
the machine's uptime. Do not read "steady or monotonic" as permission to move
the origin.

### What the pinned documents never mention at all

**How often the listener hook runs.** `AccessControl.md` and
`API-functions.md` describe the callback's job and its arguments and give no
count. The hook is called on the connection's **own socket** — the one
`srt_accept` is about to return — once per incoming connection: not once per
handshake packet, and not again on a repeated induction request. A group counts
the invocations and requires exactly one per accepted connection, and requires
the socket handed to the hook on the accepting path to be the socket
`srt_accept` returns.

**The message-control defaults.** `API-functions.md` says `srt_msgctrl_init`
"fills the object with default values" and `srt_msgctrl_default` "is a constant
object and can be used as a source for assignment", and then never says what
those values are. They are required to be, in declaration order:

    flags        = 0
    msgttl       = SRT_MSGTTL_INF   (-1)
    inorder      = false            (0)
    boundary     = 0                (subsequent packet)
    srctime      = 0
    pktseq       = SRT_SEQNO_NONE   (-1)
    msgno        = SRT_MSGNO_NONE   (-1)
    grpdata      = NULL
    grpdata_size = 0

`srt_msgctrl_default` holds exactly the same contents and is an exported
constant object of type `const SRT_MSGCTRL`.

Note that `pktseq` and `msgno` default to −1 and **not** to 0. Zero is a valid
sequence number, which is why the sentinel is −1; a block cleared with `memset`
is therefore not equivalent to an initialised one, and an implementation that
zero-fills instead of writing the sentinels fails the defaults group. The
sentinel names `SRT_SEQNO_NONE` and `SRT_MSGNO_NONE` must be declared in the
public header with the value −1, and `SRT_MSGTTL_INF` with the value −1.

**The last two fields of `SRT_MSGCTRL`.** The struct listing in
`API-functions.md` stops at `msgno`. In this milestone's configuration the
structure ends with a `SRT_SOCKGROUPDATA*` pointer and a `size_t` count, and
both are part of the defaults above. Code that brace-initialises the structure
with seven values is not initialising it.

These two fields, and the `SRT_SOCKGROUPDATA` typedef their pointer needs, must
be declared **unconditionally** — outside whatever preprocessor guard the
grouped-connection API is placed behind. A translation unit that includes only
`<srt/srt.h>`, defines no build-time feature macro of its own, and names
`grpdata` and `grpdata_size` has to compile. This is the one part of the
grouped-connection surface that is not optional, because it is reachable
through `SRT_MSGCTRL`, which every message-control caller uses. The pointer's
*value* is all that is required to be readable; nothing dereferences it.

**The source-time lower bound, exactly.** `API-functions.md` says the time
value "MUST equal or exceed the connection start time
(`srt_connection_time(..)`)" and lists `SRT_EINVALMSGAPI` among the errors of
`srt_sendmsg2` for "wrong source time". It does not state the three things a
group needs:

* the comparison is strict — a `srctime` *equal* to `srt_connection_time()` on
  the sending socket is accepted, only a strictly smaller one is refused;
* `srctime == 0` is exempt from the check entirely; it is the "stamp it now"
  request and must never be refused for being in the past;
* the refusal is total. `srt_sendmsg2` returns −1, `srt_getlasterror` reports
  `SRT_EINVALMSGAPI` (5009), and nothing is queued or transmitted — the peer
  must receive nothing and the connection must remain usable for the next,
  valid, send.

**Where the access-control codes live, and what the boundaries are.**
`AccessControl.md` mentions `access_control.h` once, in passing. This milestone
must install it as a second public header in the same directory as the main
one. Including only the main header must still compile everything that does not
name an `SRT_REJX_*` constant; including the second one must not require any
particular order. The numbers are the contract:

* `SRT_REJC_PREDEFINED` = 1000 — the first code an application may set;
* `SRT_REJC_USERDEFINED` = 2000 — the first code no catalogue will ever claim;
* `SRT_REJC_INTERNAL` = 0 — the start of the band below `SRT_REJC_PREDEFINED`,
  which belongs to the protocol's own reason enumeration and is not the
  application's to set;
* `SRT_REJX_FORBIDDEN` = 1403, one of the catalogue codes a group uses;
* `SRT_REJX_FALLBACK` = 1000, the lowest code in the catalogue. A group opens
  the second header **as installed** and requires its text to contain both this
  spelling and `SRT_REJX_FORBIDDEN`, so defining the catalogue somewhere else
  and forwarding to it is not enough: these two names must appear in the
  installed `access_control.h` itself.

The *main* header also carries the macro that turns a code into its category,
`SRT_REJC_VALUE(code)`, which rounds a code down to the start of its
1000-wide band: `SRT_REJC_VALUE(1403)` is 1000 and `SRT_REJC_VALUE(2999)` is
2000. It lives in the **main** header beside the two category constants, not
in `access_control.h`, which carries only the `SRT_REJX_*` catalogue.

`srt_setrejectreason(sock, value)` returns 0 on success and −1 with
`SRT_EINVPARAM` (5003) when `value < SRT_REJC_PREDEFINED`; exactly 1000 is
accepted. An invalid socket id gives `SRT_EINVSOCK` (5004). A refused set
stores nothing: after either error the socket's rejection reason still reads
what it read before the call, because the argument is checked before any value
is stored. A value the hook
sets is carried to the calling peer **unclamped** — no ceiling, no mapping into
the protocol's own reason enumeration, no substitution of `SRT_REJ_UNKNOWN`.
The caller reads the value back verbatim from `srt_getrejectreason`.

One inherited identifier is referenced with no fallback, and it is named here
because the shipped corpus spells the same thing differently: the driver reads
`SRT_REJ__SIZE` — the terminator of the protocol's own reason enumeration,
spelled with the double underscore — while the pinned `API-functions.md` names
only `SRT_REJ_E_SIZE`. The release keeps `SRT_REJ__SIZE` as an alias of the
newer spelling; keep both names declared in your header. It is absent from the
name table below only because that table lists names new at this milestone.

**The timeout reason.** `API-functions.md` mentions `SRT_REJ_TIMEOUT` twice but
never says when it appears. Required: after `srt_connect` to an address with
nothing listening fails on expiry of `SRTO_CONNTIMEO`, `srt_connect` reports
`SRT_ENOSERVER` and `srt_getrejectreason` on that same socket reports
`SRT_REJ_TIMEOUT` (16). The socket must survive the failed connect well enough
to answer that query. A group sets `SRTO_CONNTIMEO` and requires the reason, the error, and that the
configured value **governs** the wait: the call may not return appreciably
sooner than it was told to wait, and raising the option must lengthen the wait
by a comparable amount. A hard-coded internal interval that ignores the option
fails the second of those, which is where the group's discrimination lives.
There is deliberately no upper bound on how long the wait may take: the
requirement is that the option is honoured, not that the library is punctual on
a loaded host.

**Where the reorder tolerance starts.** `statistics.md` is *right* about this
and is worth reading closely rather than correcting: "SRT starts from tolerance
value set in `SRTO_LOSSMAXTTL` (initial tolerance is set to 0 in SRT v1.4.0 and
prior versions)". The parenthetical scopes the zero to the releases before this
one, so the main clause is this milestone's rule — the tolerance starts at the
configured `SRTO_LOSSMAXTTL` and not at the built-in default, which
`APISocketOptions.md` gives as 0.

What no document states is the second half: an **accepted** socket starts at
the value configured on the listener it was accepted from, rather than at its
own default. Required here. A group configures `SRTO_LOSSMAXTTL` before bind on
the listener only, leaves it untouched on the caller, and reads
`pktReorderTolerance` on both — the accepted socket must report the listener's
value and the caller, having set no maximum for the tolerance to rise to, must
report 0. Note which socket is asked: `statistics.md` scopes
`pktReorderTolerance` to the "Receiver side", and the group reads it there.

**The unique-transmission counters.** `statistics.md` lists
`pktSentUniqueTotal`, `pktRecvUniqueTotal`, `byteSentUniqueTotal` and
`byteRecvUniqueTotal`. All four must exist and move with traffic. The first of
them the document does pin, exactly -- "`pktSentUniqueTotal = pktSentTotal –
pktRetransTotal`", with a packet-filter variant beside it -- and that formula is
the rule rather than an illustration: the two **send-side** counters count
original transmissions only, excluding retransmissions and excluding
packet-filter control packets. On a clean loopback connection over which the
application sent exactly N messages, each small enough for one packet,
`pktSentUniqueTotal` must equal N; `byteSentUniqueTotal` must be at least the
payload those N messages carried and must never exceed `byteSentTotal`.

Do **not** carry that rule across to the receive side. `statistics.md` scopes
`pktRecvUniqueTotal` differently and is right to: it counts packets "first
arrived", whether original, retransmitted or recovered by the packet filter, so
a receive-side unique counter that excluded retransmissions would be wrong.
**Scope at this milestone:** only the two send-side counters are read by a
group. `pktRecvUniqueTotal` and `byteRecvUniqueTotal` are required of the
implementation and are deliberately not verified here, so nothing you do to
them changes how this step is checked.

**The group entry points and the build flag.** The grouped-connection entry
points — creating a group, connecting one, reading member data, and asking
which group a socket belongs to — together with the `SRT_GTYPE_*` and
`SRT_GST_*` enumerations, are present only when the library is configured with
the extra flag on this milestone's build line. They must be declared in the
public header and exported from the shared library under that configuration.
Nothing in this milestone asserts how a group *behaves* at run time; only that
the entry points exist and resolve.

`SRT_GTYPE_*` has five members in this release and they are named, in order,
`SRT_GTYPE_UNDEFINED`, `SRT_GTYPE_BROADCAST`, `SRT_GTYPE_BACKUP`,
`SRT_GTYPE_BALANCING` and `SRT_GTYPE_MULTICAST`, followed by the terminator
`SRT_GTYPE_E_END`, which is one past the last real type and is not a group
type itself. `SRT_GST_*` has four: `SRT_GST_PENDING`, `SRT_GST_IDLE`,
`SRT_GST_RUNNING`, `SRT_GST_BROKEN`. The numbering is yours -- nothing fixes
it -- but the members must be distinct from one another, every real type must
differ from `SRT_GTYPE_UNDEFINED`, and `SRT_GTYPE_E_END` must sort above it.
The last three of the five types have no behaviour in this milestone; only
broadcast is ever created. They are named here because a group compiles
against all six of these identifiers in one translation unit, so a header
that declares four of them and stops does not build.

### Behaviour of this release the groups depend on

**Which options are refused after bind, and what "after bind" means.**
`APISocketOptions.md` marks `SRTO_TSBPDMODE`, `SRTO_RCVLATENCY`,
`SRTO_TLPKTDROP`, `SRTO_NAKREPORT` and `SRTO_LOSSMAXTTL` as `pre`, and its
legend says a `pre` option "must be set prior to calling `srt_connect()` or
`srt_bind()`". What it does not say is what happens if you ignore that, and
the answer changed in this milestone. For `SRTO_TSBPDMODE`, `SRTO_LATENCY`,
`SRTO_RCVLATENCY`, `SRTO_PEERLATENCY`, `SRTO_TLPKTDROP` and `SRTO_NAKREPORT`:

* the refusal is triggered by the socket having been *opened*, which a plain
  `srt_bind` to `127.0.0.1:0` does. A socket that has been bound and neither
  connected nor set listening is already past the boundary;
* the error is `SRT_EBOUNDSOCK` (5001), not `SRT_EINVOP` and not
  `SRT_ECONNSOCK`;
* the check runs *before* the value is stored, so `srt_getsockopt` after a
  refused `srt_setsockopt` must return the value the option held before the
  refused call, unchanged. A guard placed after the assignment returns the
  right error and still fails the group.

This boundary is at its narrowest in this milestone, and its narrowness is
itself asserted. Do not generalise it to every `pre` option:
**`SRTO_LOSSMAXTTL` must remain settable on a socket that has already been
bound.** An in-scope group binds a socket, requires each of the six options above
to be refused on it, and then — on that same bound socket — sets
`SRTO_LOSSMAXTTL` to 7, requires the setter to succeed, and requires
`srt_getsockflag` to read 7 back. Only that option's connected-state reset is
guarded, and this milestone did not move it.

So the `pre` legend quoted above states a convention, not this release's
enforcement. Implement the post-bind refusal for the six options listed here;
nothing at this milestone measures a seventh — the release itself also guards
`SRTO_TRANSTYPE` after bind, and since no group here sets that option on a
bound socket, implementing that guard as well is harmless, and so is omitting
it. What you may not do is generalise the six to every `pre` option (a
candidate who takes the legend at face value and refuses every `pre` option
after bind fails an in-scope group) or drop the whole set (a candidate who
refuses none of the six fails the same group from the other side).

**`SRTO_RCVSYN` and `SRTO_SNDSYN` are outside all of the boundaries above.**
However the `pre` legend reads, the released library accepts a set of either of
these two at **any** socket state — including on a connected socket and on the
socket `srt_accept` returned — and the new value takes effect for subsequent
calls. A socket returned by `srt_accept` starts with the listener's value, so a
listener opened non-blocking yields a non-blocking accepted socket whose
receive mode you may then switch back. Treating the legend as universal for
these two and refusing the set loses every group that receives on an accepted
socket, including gates.

**`SRTO_TSBPDMODE` can be read back, whatever the legend says.**
`APISocketOptions.md` gives its **Dir** column as `W`, and the legend defines
that as "W if can be set, R if can be retrieved, RW if both" — so read
literally, the table says the option is write-only. It is not, at this release:
`srt_getsockflag(s, SRTO_TSBPDMODE, …)` succeeds and yields the configured
value as an `int32_t`. A group relies on that read, so an implementation whose
getter refuses the option, or answers a stale default, fails it even though the
pinned table appears to excuse both. This is listed here rather than among the
document errors above because the table is not describing the behaviour of an
earlier release — it is a legend that was never accurate for this option.

**`SRTO_PACKETFILTER` reads back too, and the same legend is wrong about it.**
`APISocketOptions.md` gives this option a **Dir** column of `W` as well. It is
no more write-only than `SRTO_TSBPDMODE`: on a socket the option has been set
on, `srt_getsockflag(s, SRTO_PACKETFILTER, …)` must succeed and return the
configured filter string **byte for byte**, with a non-zero length — not an
empty string, not a truncation, and not `SRT_EINVOP`. Unset, it succeeds and
yields an empty string rather than refusing. One of the inherited stream
groups of §3 configures a filter, reads it straight back and compares the
whole string, so a getter that refuses this option fails that group even
though the pinned table appears to excuse the refusal. This one is worth
re-reading if you are porting from the previous milestone's tree: the pinned
`APISocketOptions.md` is **new to this milestone's reading list**, so a
candidate who never saw that `W` at the previous milestone can meet it for the
first time here and implement the refusal it seems to license.

**The sender's filter accounting has exactly one spelling.** The same group
reads it out of `SRT_TRACEBSTATS` as **`pktSndFilterExtraTotal`** — the
cumulative count of extra packets the filter itself generated on the sender,
counted on top of the application's own payload. Required: it is greater than
zero once an FEC filter has carried traffic, and exactly zero on a connection
configured with no filter at all. A field that never moves and a field that is
non-zero without a filter both fail. This is an inherited spelling rather than
one this milestone introduces — it arrived with the packet filter at the
previous milestone — so it is not in the list of names below that this
milestone requires you to declare; it is listed here because the group names
the member directly and nothing else in your reading list now fixes how it is
spelled.

**The lossless configuration the inherited groups need.** The stream groups
configure `SRTO_TLPKTDROP = 0` on both sockets together with an
`SRTO_RCVLATENCY` comfortably larger than the relay's worst delay, all of it
before bind, and then require every message to arrive, byte-identical and in
order, with no gap in the sequence and both sockets still connected at the end.
One group does this for four thousand messages. The receiver drains until the
expected count arrives rather than sleeping a fixed interval, so a correct
implementation is not failed for being slow, but a message that is dropped
rather than delayed is fatal.

**Reading the delay actually in force.** The only place the negotiated
receiver delay is observable is `msRcvTsbPdDelay` in the statistics structure
returned by `srt_bstats`. A group configures a latency and reads it back there,
so that field must reflect the value in force on a connected socket rather than
staying at a default.

`statistics.md` scopes that field to the "Receiver side". That describes where
the delay is applied, not the only socket that may be asked for it, and the
distinction is load-bearing here. Required: the field is filled from the
**reading** socket's own timestamp-based-delivery delay whenever that mode is
enabled on it, so when both ends are in timestamp-based mode at the same
configured latency, **both** ends report that same figure — the receiver that
was given `SRTO_RCVLATENCY` and the sender that was given `SRTO_LATENCY` alike.
A group reads it at both ends at two different configured latencies and
requires both readings to track the configured value, which a library
reporting a constant cannot satisfy and a library that fills the field only on
the accepted socket cannot satisfy either.

**Error codes are numbers.** Every group compares `srt_getlasterror(NULL)`
against a numeric constant. Never assume a group matches on the text from
`srt_strerror`: the string form composes a major and a minor code into one
sentence, so two different errors can read almost identically and one error can
read like two. Getting the string wrong is harmless; getting the number wrong
fails the group.

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
| `SRT_GROUP_TYPE` | **required to compile** |
| `SRT_GST_BROKEN` | **required to compile** |
| `SRT_GST_IDLE` | **required to compile** |
| `SRT_GST_PENDING` | **required to compile** |
| `SRT_GST_RUNNING` | **required to compile** |
| `SRT_GTYPE_BACKUP` | **required to compile** |
| `SRT_GTYPE_BALANCING` | **required to compile** |
| `SRT_GTYPE_BROADCAST` | **required to compile** |
| `SRT_GTYPE_E_END` | **required to compile** |
| `SRT_GTYPE_MULTICAST` | **required to compile** |
| `SRT_GTYPE_UNDEFINED` | **required to compile** |
| `SRT_MSGNO_NONE` | optional (guarded) |
| `SRT_MSGTTL_INF` | optional (guarded) |
| `SRT_REJC_INTERNAL` | **required to compile** |
| `SRT_REJC_PREDEFINED` | **required to compile** |
| `SRT_REJC_USERDEFINED` | **required to compile** |
| `SRT_REJC_VALUE` | **required to compile** |
| `SRT_REJX_FORBIDDEN` | **required to compile** |
| `SRT_REJ_TIMEOUT` | **required to compile** |
| `SRT_SEQNO_NONE` | optional (guarded) |
| `SRT_SOCKGROUPCONFIG` | **required to compile** |
| `SRT_SOCKGROUPDATA` | **required to compile** |
| `byteSentUniqueTotal` | **required to compile** |
| `grpdata` | **required to compile** |
| `grpdata_size` | **required to compile** |
| `pktReorderTolerance` | **required to compile** |
| `pktSentUniqueTotal` | **required to compile** |
| `srt_connection_time` | **required to compile** |

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
| `SRTO_LATENCY` | `23` |
| `SRTO_LOSSMAXTTL` | `42` |
| `SRTO_NAKREPORT` | `33` |
| `SRTO_PACKETFILTER` | `60` |
| `SRTO_PEERLATENCY` | `44` |
| `SRTO_RCVLATENCY` | `43` |
| `SRTO_TLPKTDROP` | `31` |
| `SRTO_TSBPDMODE` | `22` |
| `SRT_EBOUNDSOCK` | `5001` |
| `SRT_MSGNO_NONE` | `-1` |
| `SRT_MSGTTL_INF` | `-1` |
| `SRT_REJC_INTERNAL` | `0` |
| `SRT_REJC_PREDEFINED` | `1000` |
| `SRT_REJC_USERDEFINED` | `2000` |
| `SRT_REJX_FORBIDDEN` | `1403` |
| `SRT_REJ_ROGUE` | `4` |
| `SRT_REJ_TIMEOUT` | `16` |
| `SRT_REJ_UNKNOWN` | `0` |
| `SRT_SEQNO_NONE` | `-1` |

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
