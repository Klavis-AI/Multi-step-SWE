# Test interface — milestone 5 of 11

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
8 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.4.0**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.4.0:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `4` |
| `SRT_VERSION_PATCH` | `0` |
| `SRT_VERSION_STRING` | `"1.4.0"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.4.0)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.4.0 is acceptable.

This is not bookkeeping at this milestone. The verifier compiles
`fam_epoll.cpp`, `fam_filter.cpp`, `fam_reject.cpp` conditionally on `SRT_VERSION_VALUE`, so a tree that
implements every behaviour this document asks for and leaves the version
where the previous milestone left it skips those compile-time pins and runs the
drivers' guarded restatements instead. You will not see a compile error, and
nothing about the below-expected branch is a trap: put the constants where
"Constants whose numeric value the verifier pins" says and both branches agree
with your library, whether or not you bump the version.


---

## 2. How the work is checked

There are 3 **gate** groups (§3) and 11 **requirement** groups
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
- `transfer` — live messages cross an intact relay complete and in order at the default SRTO_PAYLOADSIZE for live mode, and a message larger than the negotiated limit is refused
- `encryption` — a matching passphrase at both key lengths carries the payload intact, SRTO_RCVKMSTATE on the receiver and SRTO_SNDKMSTATE on the sender reach SRT_KM_S_SECURED, and a one-sided or mismatched passphrase delivers nothing to the application while the KM state reports the condition

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
| 1 | `filter-config` | SRTO_PACKETFILTER accepts a well-formed FEC configuration string, rejects an unknown filter name and unparseable syntax with EINVPARAM, and the getter retrieves the stored string or an empty string when unset |
| 2 | `filter-payloadsize` | setting SRTO_PACKETFILTER after SRTO_PAYLOADSIZE silently clamps the payload size down by the filter's extra-size requirement, while setting the filter first then the payload size leaves the payload size at the requested value |
| 3 | `filter-stats` | with FEC configured on both ends and traffic crossing the channel, srt_bstats reports non-zero pktSndFilterExtraTotal on the sender, all eight new filter counter fields (four totals and their four interval counterparts) are members of SRT_TRACEBSTATS, and no total is below its own interval counterpart |
| 4 | `rejectreason-api` | srt_getrejectreason exists and returns SRT_REJ_UNKNOWN on a fresh socket and on an unknown socket id, srt_rejectreason_str and the srt_rejectreason_msg array exist, and SRT_REJ__SIZE equals the documented count |
| 5 | `rejectreason-values` | when SRTO_ENFORCEDENCRYPTION is at its default and the handshake is rejected, srt_getrejectreason reports SRT_REJ_UNSECURE for one-sided passphrase, SRT_REJ_BADSECRET for mismatched passphrases, SRT_REJ_MESSAGEAPI for message/stream mismatch, SRT_REJ_CONGESTION for congestion-controller mismatch; a caller-listener packet-filter disagreement is merged rather than rejected; and with the enforcement option turned off on both sides a mismatched passphrase connects, delivers nothing, and reports no rejection reason |
| 6 | `epoll-empty` | srt_epoll_wait on an empty EID with a non-negative timeout fails immediately with EINVPARAM, srt_epoll_set with SRT_EPOLL_ENABLE_EMPTY returns the previous flag value and makes the wait revert to ETIMEOUT, and clearing the flag restores the immediate EINVPARAM behavior |
| 7 | `epoll-et` | srt_epoll_add_usock and srt_epoll_update_usock with only the SRT_EPOLL_ET bit set are both rejected with EINVPARAM, while SRT_EPOLL_IN combined with SRT_EPOLL_ET is accepted by both |
| 8 | `epoll-update` | srt_epoll_update_usock with a zero event mask succeeds and removes the subscription, leaving the EID empty, so a subsequent wait without SRT_EPOLL_ENABLE_EMPTY set fails with EINVPARAM |
| 9 | `epoll-uwait` | srt_epoll_uwait exists and returns the count of ready sockets as SRT_EPOLL_EVENT structures with fd and events fields, it fails with EINVPARAM on an empty EID without SRT_EPOLL_ENABLE_EMPTY exactly as srt_epoll_wait does whatever the timeout, and on an EID carrying SRT_EPOLL_ENABLE_EMPTY with nothing ready it returns zero where srt_epoll_wait returns -1 with SRT_ETIMEOUT |
| 10 | `api-removal` | dlsym for srt_perfmon returns NULL while srt_bstats remains defined, the SRT_TRACEINFO alias for struct CPerfMon survives the function's removal, and code that called srt_perfmon at the previous milestone fails to link |
| 11 | `endtoend` | with the FEC filter configured on both ends, a live stream of 60 messages crosses a relay that drops datagrams at fixed indexes, the filter injects control packets into the channel, and the payload still arrives complete and in order |

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

**A missing name this milestone introduces breaks the whole family that uses
it, not one group.** The verifier is not one program but one per *family* of groups,
and each family is a single translation unit compiled once; every group in §4
belongs to exactly one of them. If a name the list below marks **required to
compile** is absent from the `<srt/srt.h>` you install, that program does not
build, and every group it carries fails — more than the one group that wanted
the name. The statement in §2, and in `instruction.md` §3, that one group's
failure does not affect another describes a group that *runs* and fails; it
is not a promise about a family that never compiles. Two things follow. First,
declare the names below even where you cannot implement the behaviour behind
them: a correctly spelled declaration with nothing behind it turns a
whole-family loss into a single failed group. Second, the gate groups (§3) are
compiled in a family of their own, whose translation unit names no identifier
**this milestone introduces** and restates none — so nothing you leave
undeclared from this milestone's surface can fail the gate. That family is
not free of inherited surface, though, and the one inherited member it reads is
named here because no other section does: the encryption gate reads
`SRT_TRACEBSTATS::pktRcvUndecryptTotal` — spelled exactly like that, a member
you inherit from milestone 01 — to tell "arrived because it was decrypted" from
"arrived because it was never encrypted". A header in which that member has
been deleted or renamed does not build the gate family, and the step is not
satisfied. A missing name from *this* milestone can fail a requirement group;
that one inherited member can fail a gate group.

**`SRTO_PACKETFILTER` getter returns an empty string when unset, not EINVOP.**
At the previous milestone the getter failed with `SRT_EINVOP` — no such
operation. Here it succeeds and returns an empty string when no filter has been
configured. The groups read the string length and the return code, not a
precomputed default. The length reported through `optlen` excludes the
terminating NUL: an unset filter reports length 0, and a configured one reports
`strlen` of the stored string — the same convention the inherited string
options (`SRTO_STREAMID` among them) already use.

**`SRTO_PACKETFILTER` and `SRTO_PAYLOADSIZE` interaction is order-dependent.**
When `SRTO_PAYLOADSIZE` is set to 1456 and then `SRTO_PACKETFILTER` is set to
`"fec,cols:10"`, the filter set succeeds and `SRTO_PAYLOADSIZE` is silently
rewritten from 1456 to 1452 (1456 minus the FEC `EXTRA_SIZE` of 4 bytes),
with no error reported. The reverse order — setting the filter first, then
`SRTO_PAYLOADSIZE = 1456` — returns 0 for the `SRTO_PAYLOADSIZE` set and
leaves the payload size at 1456, because the payload-size setter compares the
value already stored against the filter's budget rather than the incoming one. The groups test both orders to assert the exact behaviour, not a
simplified "always clamps" rule.

**Default `SRTO_PAYLOADSIZE` without a filter is 1316 in live mode, not 1456.**
The header defines `SRT_LIVE_MAX_PLSIZE` as 1456, but that is the
maximum, not the default. A fresh socket in live mode reports 1316 from the
getter before any filter is set. When a filter is then configured, the
adjustment is computed from the stored value, so the default does not jump to
1456 merely because a filter exists.

**Unparseable filter configuration syntax is rejected the same as an unknown
filter name.** Both `"nosuchfilter,x:1"` (unknown filter) and `"fec;cols=10"`
(semicolon instead of comma) fail the set with `SRT_EINVPARAM`. The groups do
not distinguish between the two failure causes; both are asserted as refusals
rather than as different error codes.

**`srt_getrejectreason` on an unknown socket id returns `SRT_REJ_UNKNOWN` (0),
not an error.** The function does not throw or set `SRT_EINVSOCK`; it returns
the same fallback code as for a fresh socket that has never attempted a
connection. The groups read both a valid fresh socket and a fabricated id that
was never allocated, and both must report 0.

**`srt_rejectreason_str` for an out-of-range code returns the
`SRT_REJ_UNKNOWN` string, not NULL.** The function clamps invalid inputs to the
documented fallback rather than leaving them undefined. The groups call it with
a code above `SRT_REJ__SIZE` and assert that a non-NULL string is returned.

**`SRT_REJ__SIZE` equals 15 at this release.** The enumeration runs from
`SRT_REJ_UNKNOWN` (0) through `SRT_REJ_FILTER` (14), and `SRT_REJ__SIZE` is
one past the last valid code. The groups read `SRT_REJ__SIZE` and compare it
against the documented count, not against a hardcoded constant.

**Rejection reasons are reported only while the encryption-enforcement option
is at its default (`true`).** That option is the one occupying **slot 53**,
which the previous milestone spells `SRTO_STRICTENC`; renaming it is not
something this milestone covers, and no group names either spelling —
one group reaches it by its number, and that number is what must not move.
When it is explicitly set to `false` on both sides, a passphrase mismatch or a
one-sided passphrase does not reject the handshake at all: the connection
reaches `SRTS_CONNECTED`, `srt_getrejectreason` on the caller reports
`SRT_REJ_UNKNOWN` (0) because nothing was rejected, and the failure surfaces
only through the KM state and through payload that never arrives. A library
that refuses a mismatched passphrase whatever this option says fails that
group. At the default the handshake **is** refused, `srt_connect` reports
`SRT_ECONNREJ`, and `srt_getrejectreason` reports the specific cause.

**A caller-listener packet filter disagreement is merged, not rejected.**
`SRT_REJ_FILTER` (14) is a member of the enumeration and must be reachable
through `srt_rejectreason_str` and `srt_rejectreason_msg[]`, but on a
caller-listener handshake a differing `SRTO_PACKETFILTER` configuration does
**not** refuse the connection: a listener with no filter adopts the caller's,
a listener with one keeps its own parameters and merges in only the keys it
did not set, and the caller takes the listener's version. The groups set
`"fec,cols:10"` on the listener and `"fec,cols:20,rows:9"` on the caller and
assert that the handshake **connects**, and that the getter still answers on
both sides afterwards. Nothing about the *content* of that post-connect
readback is asserted: the serialised form of a negotiated configuration is
yours to choose.

**Rejection-reason strings are per-code English text, not fixed wording.**
Every code from `SRT_REJ_UNKNOWN` (0) through `SRT_REJ_FILTER` (14) must map
to a non-NULL, non-empty string, and the six codes this milestone's groups
name must map to six *distinct* strings, so that a caller can tell the reasons
apart through the table. The exact phrasing is yours: no group compares a
message against a literal.

**`srt_epoll_uwait` reports "nothing ready" as 0, where `srt_epoll_wait`
reports it as `SRT_ETIMEOUT`.** Both functions reject an *empty* EID the same
way: without `SRT_EPOLL_ENABLE_EMPTY` each fails with `EINVPARAM`, because
`uwait` carries the identical guard. The observable difference appears once the
EID carries `SRT_EPOLL_ENABLE_EMPTY` and nothing is ready — then `uwait`
returns 0 and `wait` returns −1 with `SRT_ETIMEOUT`. That pair is what the
group asserts.

**`srt_epoll_set` returns the previous flag value as `int32_t`.** Setting
`SRT_EPOLL_ENABLE_EMPTY` on a fresh EID returns 0 (no flags were set before).
Setting it again returns 1 (the flag was already set). Clearing it with
`srt_epoll_set(eid, 0)` returns 1 (the flag was set before the clear). The
groups read the return value and assert its correctness at each step, not just
the side-effect on subsequent waits.

**`srt_epoll_update_usock` with a zero event mask succeeds and removes the
subscription.** The call returns 0, not an error. The socket is erased from all
event sets, and if it was the last socket in the EID, the EID becomes empty.
The groups then call `srt_epoll_wait` without `SRT_EPOLL_ENABLE_EMPTY` and
assert `EINVPARAM`, proving that the update did make the EID empty.

**`srt_perfmon` must be gone.** `dlsym(RTLD_DEFAULT, "srt_perfmon")` must
return NULL: the definition, the declaration in `<srt/srt.h>` and the exported
symbol are all removed at this milestone, so code written against the previous
one that calls it will no longer link. The absence is proved with `dlsym` rather
than a link-time assertion, because the verifier runs one binary against several
milestones.

**`srt_bstats` remains defined, and so does the `SRT_TRACEINFO` alias.** The
removal above affects `srt_perfmon` alone; `srt_bstats` and the `SRT_TRACEINFO`
type survive unchanged. Both the call and the exported symbol are checked,
against the removal of the other. The alias is checked at compile time — the
header must still declare `typedef struct CPerfMon SRT_TRACEINFO;`, with
`CPerfMon` left incomplete exactly as you inherited it, so deleting either name
or retyping the alias onto something else stops that family building rather
than failing one assertion. No group completes, sizes or dereferences
`CPerfMon`.

**Stated in the documents, deliberately out of scope at this milestone.** The
`pre` binding of `SRTO_PACKETFILTER` implies a refusal on a connected socket,
and no group sets it there; the `-1` query form of `srt_epoll_set` and the
`fdsSize + 1` truncation return of `srt_epoll_uwait` are both documented in
`srt-docs/` and deliberately unasserted (asserting them would pin the release's
incidental behaviour, not a requirement); whether the setter insists on a
`cols` key (the grammar says it is mandatory, and a bare `"fec"` is accepted by
the release) is likewise unasserted; and the `arq` configuration key is
documented and neither required nor forbidden. They are recorded so that their
absence from the verifier is not read as an omission to fix.

**Eight filter counter fields join `SRT_TRACEBSTATS`, spelled exactly like
this.** Four cumulative totals —

* `pktSndFilterExtraTotal` — filter-injected control packets sent;
* `pktRcvFilterExtraTotal` — filter control packets received;
* `pktRcvFilterSupplyTotal` — packets the filter rebuilt;
* `pktRcvFilterLossTotal` — losses the filter could not cover;

and their four interval counterparts, spelled by dropping the `Total` suffix:
`pktSndFilterExtra`, `pktRcvFilterExtra`, `pktRcvFilterSupply`,
`pktRcvFilterLoss`. A driver looks each of the eight up by name and reads it
if it is there; a field that is missing, or spelled differently, reads as
absent and fails this group. It does not break the translation unit -- the
lookup is a compile-time member test, not a direct reference -- so a tree that
declares only some of them loses this group and keeps the rest of the step.
The struct is therefore strictly larger than it was at the
previous milestone — the existing fields keep their meaning and none is
removed. With a filter configured and traffic crossing the channel,
`pktSndFilterExtraTotal` must become greater than zero, and no total may read
below its own interval counterpart. No exact count is required beyond that.

**`SRTO_RCVSYN` and `SRTO_SNDSYN` may be set at any state, despite their `pre`
rows.** The shipped `API.md` types both as `pre`, and a `pre` option is refused
once the socket is connected — these two are the exception the released library
has always made. `srt_setsockflag` accepts either at **any** state, including
on a connected socket and on the socket `srt_accept` returned, and the new
value takes effect for subsequent calls; a socket returned by `srt_accept`
starts with the listener's value, so a listener opened non-blocking yields a
non-blocking accepted socket whose receive mode you may then switch back.
Refusing such a set loses every group that receives on an accepted socket.

**The FEC filter's extra size is 4 bytes, despite what
`srt-docs/packet-filtering-and-fec.md` says.** That file's `extraSize`
paragraph describes the value as "the count of 32-bit data. The current FEC
implementation returns 4 here", which taken literally would be 16 bytes and
would clamp 1456 to 1440. It is a byte-for-byte upstream copy and is wrong on
this point. **4 bytes** — two for the flag spec, two for the length clip — is
what the implementation returns and what this milestone requires; the clamp a
group asserts is 1456 → 1452.

### Public names this milestone must introduce for the verifier to build

The verifier's translation units reference the public names below, and
each of them is new in this milestone -- absent from the header you
inherit, present in the header a correct implementation installs. A
name marked **required to compile** is used with no `#ifndef` fallback:
a header that omits it does not merely fail one group, it fails to
build the whole family, and every group in that family is lost,
including any gate group. A name marked **member-tested** is *looked
up* by a compile-time member test rather than referenced directly: an
absent or misspelled member reads as absent and fails exactly the group
that reads it, not the family. This list is generated from the drivers, so
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
| `SRTO_PACKETFILTER` | **required to compile** |
| `SRT_EPOLL_ENABLE_EMPTY` | **required to compile** |
| `SRT_EPOLL_ET` | **required to compile** |
| `SRT_REJ_BADSECRET` | **required to compile** |
| `SRT_REJ_CONGESTION` | **required to compile** |
| `SRT_REJ_FILTER` | **required to compile** |
| `SRT_REJ_MESSAGEAPI` | **required to compile** |
| `SRT_REJ_UNKNOWN` | **required to compile** |
| `SRT_REJ_UNSECURE` | **required to compile** |
| `SRT_REJ__SIZE` | **required to compile** |
| `pktRcvFilterExtra` | **member-tested** |
| `pktRcvFilterExtraTotal` | **member-tested** |
| `pktRcvFilterLoss` | **member-tested** |
| `pktRcvFilterLossTotal` | **member-tested** |
| `pktRcvFilterSupply` | **member-tested** |
| `pktRcvFilterSupplyTotal` | **member-tested** |
| `pktSndFilterExtra` | **member-tested** |
| `pktSndFilterExtraTotal` | **member-tested** |

### Constants whose numeric value the verifier pins

A verifier driver in this step restates the constants below and pins
each restatement with a `static_assert` against the `<srt/srt.h>` you
install, so **these values are part of the interface** and an
implementation that numbers them differently fails to compile rather
than failing a behavioural assertion. They are listed here because a
verifier may only assert a numeric value that a document fixes, and
this is the document that fixes them: no other document states a value
for every one of these names — `instruction.md` gives the packet-filter
slot, and the reject-reason list in `srt-docs/API-functions.md` fixes the
order the `SRT_REJ_*` values follow — and where a value is stated
anywhere else, the two agree.

The pins exist because a restated constant that drifts would make the
verifier silently address a *different* option and report a pass for
the wrong reason. Every other constant is yours to number — with one
exception: a constant whose *position* a section above fixes as part of
the contract, because a group reaches it by number rather than by name,
is fixed by that section and not by this table.

| constant | value |
| --- | --- |
| `SRTO_PACKETFILTER` | `60` |
| `SRT_EPOLL_ENABLE_EMPTY` | `1` |
| `SRT_EPOLL_ET` | `2147483648` |
| `SRT_REJ_BADSECRET` | `10` |
| `SRT_REJ_CONGESTION` | `13` |
| `SRT_REJ_FILTER` | `14` |
| `SRT_REJ_MESSAGEAPI` | `12` |
| `SRT_REJ_UNKNOWN` | `0` |
| `SRT_REJ_UNSECURE` | `11` |
| `SRT_REJ__SIZE` | `15` |

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
