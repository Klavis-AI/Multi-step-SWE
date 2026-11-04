# Test interface — milestone 7 of 11

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

This milestone is SRT **1.4.3**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.4.3:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `4` |
| `SRT_VERSION_PATCH` | `3` |
| `SRT_VERSION_STRING` | `"1.4.3"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.4.3)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.4.3 is acceptable.

This is not bookkeeping at this milestone. The verifier compiles
`fam_live.cpp`, `fam_optget.cpp` conditionally on `SRT_VERSION_VALUE`, so a tree that
implements every behaviour this document asks for and leaves the version
where the previous milestone left it skips those compile-time pins and runs the
drivers' guarded restatements instead. You will not see a compile error, and
nothing about the below-expected branch is a trap: put the constants where
"Constants whose numeric value the verifier pins" says and both branches agree
with your library, whether or not you bump the version.


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
- `recovery` — with too-late packet drop switched off and a latency large enough to cover the path, a message stream whose datagrams are dropped at fixed indexes arrives complete and in order, and srt_bstats on the receiver reports a non-zero pktRcvLossTotal so the loss is known to have happened
- `fec` — with the same FEC filter configured on both ends and datagrams dropped at fixed indexes, the stream arrives complete and in order and srt_bstats on the sender reports a non-zero pktSndFilterExtraTotal
- `encryption` — with a matching passphrase and an explicit key length set on both ends before connect, the stream arrives byte-identical, no plaintext payload appears in the datagrams crossing the relay, and SRTO_RCVKMSTATE on the receiver and SRTO_SNDKMSTATE on the sender both report SRT_KM_S_SECURED

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
| 1 | `latency-defaults` | on a socket that has been created but not bound, SRTO_RCVLATENCY and SRTO_LATENCY report 120 and SRTO_PEERLATENCY reports 0; setting any one of the three before bind makes srt_getsockopt report the value that was set, and setting SRTO_LATENCY sets all three |
| 2 | `bool-width` | srt_getsockopt for SRTO_SENDER, SRTO_TSBPDMODE, SRTO_TLPKTDROP, SRTO_DRIFTTRACER and SRTO_ENFORCEDENCRYPTION writes one byte and sets *optlen to 1 even when a four-byte buffer is offered, and SRTO_TLPKTDROP set to 0 before connect reads back as 0 in that one byte |
| 3 | `config-readback` | SRTO_KMREFRESHRATE, SRTO_KMPREANNOUNCE and SRTO_RETRANSMITALGO are readable on an unbound socket and report the stored 0 rather than failing, and SRTO_MININPUTBW exists, reports 0 into an eight-byte buffer, accepts a set and reports the value that was set |
| 4 | `get-buflen` | srt_getsockopt refuses with SRT_EINVPARAM and leaves every byte of the caller's buffer untouched when the declared length is smaller than the option's own width -- four bytes for the 64-bit SRTO_MAXBW or SRTO_INPUTBW, two bytes for the string SRTO_CONGESTION -- while the same three reads with a large enough buffer succeed and report the value |
| 5 | `clock-type` | the installed shared library exports srt_clock_type with C linkage and calling it returns exactly 4, SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY, the identifier for the clock this build selects when neither the monotonic-clock nor the C++ sync build option is enabled |
| 6 | `value-range` | srt_setsockopt refuses with SRT_EINVPARAM a value below an option's documented minimum -- SRTO_FC at 16, and -1 for SRTO_CONNTIMEO, SRTO_PAYLOADSIZE, SRTO_PEERIDLETIMEO, SRTO_RCVLATENCY, SRTO_PEERLATENCY, SRTO_LATENCY, SRTO_KMREFRESHRATE and SRTO_KMPREANNOUNCE -- and leaves each option reporting the value it held before the refused call rather than clamping the new one into range, while -1 remains acceptable for SRTO_MAXBW and SRTO_SNDDROPDELAY |
| 7 | `filter-config` | SRTO_PACKETFILTER refuses with SRT_EINVPARAM, in the srt_setsockopt call itself and not later at connect time, a configuration that names a key other than cols, rows, layout or arq, that gives cols a value below 2, or that gives layout or arq a value outside its documented set, while fec,cols:10 and fec,cols:10,rows:5 and fec,cols:10,layout:staircase and fec,cols:10,arq:never are accepted |
| 8 | `bind-boundary` | on a socket that is bound but not yet listening or connected, srt_setsockopt accepts SRTO_RCVLATENCY, SRTO_PEERLATENCY, SRTO_LATENCY, SRTO_TSBPDMODE, SRTO_TLPKTDROP and SRTO_NAKREPORT and each new value reads back, while on that same socket the pre-bind option SRTO_IPV6ONLY is refused with SRT_EBOUNDSOCK |
| 9 | `listen-boundary` | on a listening socket srt_setsockopt refuses SRTO_CONNTIMEO, SRTO_STREAMID, SRTO_PAYLOADSIZE, SRTO_PACKETFILTER, SRTO_RETRANSMITALGO, SRTO_FC, SRTO_CONGESTION, SRTO_MESSAGEAPI and SRTO_RCVLATENCY with SRT_ECONNSOCK, while the unrestricted SRTO_RCVSYN and SRTO_RCVTIMEO are still accepted on it |
| 10 | `endtoend` | a live stream of 4000 messages crosses a clean relay and arrives byte-identical, in order and with no gaps, with the receive latency and the too-late-drop setting applied to both sockets after each has been bound and read back from them |

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

**The receive latency reads 120 before the socket is connected.** The option
table's entry for `SRTO_RCVLATENCY` gives the live-mode default as "120 ms
(depicted as 0)", and says that reading the option after the connection is
established gives the negotiated value. It never says what the getter reports
*before* connection, and "depicted as 0" points the wrong way. On an unconnected
socket the getter reports the configuration: 120 for `SRTO_RCVLATENCY`, 120 for
`SRTO_LATENCY`, 0 for `SRTO_PEERLATENCY`, and afterwards whatever was last
accepted. The 120 is the installed `SRT_LIVE_DEF_LATENCY_MS`, and the `optget`
family restates that constant and pins its value in the table of constants
below rather than writing the literal, so a header that does not declare the
name, or numbers it differently, fails to build that family rather than one of
its cases. `SRTO_LATENCY` is documented only as a setter for the other two; its
getter returns the receive-side value, so setting `SRTO_LATENCY` to 300 makes
all three read 300, while setting `SRTO_PEERLATENCY` alone changes only that one.

**A boolean option's getter accepts an oversized buffer and narrows `*optlen`
itself.** The documentation says that the *setting* function accepts both
`int32_t` and `bool` "but this is not so in the case of getting an option
value", which reads as though a four-byte get of a boolean option must fail. It
does not fail. It succeeds, writes exactly one byte at the front of the buffer,
sets `*optlen` to 1, and leaves the remaining bytes as the caller left them. The
options this applies to are the ones the table types as `bool`: `SRTO_SENDER`,
`SRTO_TSBPDMODE`, `SRTO_TLPKTDROP`, `SRTO_DRIFTTRACER` and
`SRTO_ENFORCEDENCRYPTION`. Zero the buffer before the call and compare only
`*optlen` bytes.

**An option the table marks `W` is still readable.** The Dir column records the
direction the documentation describes, not a direction the library enforces.
`SRTO_SENDER`, `SRTO_TSBPDMODE`, `SRTO_ENFORCEDENCRYPTION`, `SRTO_CONGESTION`,
`SRTO_CONNTIMEO`, `SRTO_PAYLOADSIZE`, `SRTO_MESSAGEAPI` and `SRTO_SNDDROPDELAY`
all answer `srt_getsockopt` on an unconnected socket, and groups here read them
to check what a set did. A getter that refuses a `W` option fails those groups.

**Too small a buffer on get is `SRT_EINVPARAM`, and nothing is written.** The
documented error list for `srt_getsockopt` names only `SRT_EINVSOCK` and
`SRT_EINVOP`, and while the text says `*optlen` "should originally contain the
size of the `optval` type provided", it never says what happens when it is
smaller than the option needs. It is refused with `SRT_EINVPARAM`, `*optlen` is
not updated, and not one byte of the caller's buffer is touched — no partial
copy, no truncated string, no overrun past the declared length. Four bytes is
too small for the 64-bit `SRTO_MAXBW`, `SRTO_INPUTBW` and `SRTO_MININPUTBW`.
For a string option the width is the length of the text: `SRTO_CONGESTION` on a
fresh socket returns the four characters `live` and sets `*optlen` to 4, so a
two-byte buffer is refused and an oversized one succeeds — compare exactly
`*optlen` bytes rather than relying on a terminator being present.

**A refused set is a no-op on the value, not a clamp.** The setter's documented
error table explains `SRT_EINVPARAM` only as "option `opt` indicates no valid
option", and nothing in the per-option entries says what a value outside the
stated Range does. It is refused with `SRT_EINVPARAM`, and the option still
reports the value it held before the call. It is not clamped to the nearest
legal value and it is not stored anyway: `SRTO_FC` set to 100000 and then to 16
still reads 100000, and `SRTO_RCVLATENCY` set to 250 and then to -1 still reads
250. Where a Range is written `-1..` the sentinel is genuinely legal, so
`SRTO_MAXBW` and `SRTO_SNDDROPDELAY` must keep accepting -1 while
`SRTO_CONNTIMEO`, `SRTO_PAYLOADSIZE`, `SRTO_PEERIDLETIMEO`, the three latency
options and the two key-refresh options reject it.

**The key-refresh getters report the stored sentinel, not the value it stands
for.** `SRTO_KMREFRESHRATE` and `SRTO_KMPREANNOUNCE` are documented with a
default of "0 - corresponds to 16777216 packets" and "0 - corresponds to 4096
packets". On a fresh socket the getter answers 0, the number that is stored, not
the number it is substituted by. The substitution happens where the value is
used, including in the cross-option comparison between the two.

**The packet-filter configuration is validated in the set call, and an unknown
key is fatal.** The filter documentation lists `cols`, `rows`, `layout` and
`arq`, says `cols` is obligatory and at least 2, and gives `layout` the values
`even` and `staircase` and `arq` the values `always`, `onreq` and `never` — but
it never says what happens to a string that breaks one of those rules, and its
discussion of conflicting configurations points at connection time. Validation
happens in `srt_setsockopt` itself: an element that is not a `key:value` pair, a
key the filter does not define, a `cols` below 2, or a `layout` or `arq` value
outside its set is refused there and then with `SRT_EINVPARAM`, before any
socket is bound or connected. The string is a comma-separated list whose first
element is the filter's name and whose every later element carries a colon, so
`fec,cols` and `fec,cols:10,,rows:5` are both refused for their shape before any
key is looked up, while `fec,cols:10` on its own is a complete configuration;
the other three keys are optional. "In the set call" means whichever setter the
caller reaches, `srt_setsockflag` or `srt_setsockopt`: the verifier calls the
`...flag` pair exclusively, and a tree that validated the string under the
legacy spelling alone would answer acceptances where the groups require the
refusals.

**A bound socket is not a restricted state for a pre-connect option, and the
value reads back from it.** The two restriction classes differ only here: a
`pre-bind` option such as `SRTO_IPV6ONLY` is refused with `SRT_EBOUNDSOCK` the
moment `srt_bind` succeeds, while a `pre` option is refused only once the socket
is listening, connecting or connected — on a socket that is merely bound it is
accepted, and `srt_getsockopt` then reports the value that was just set rather
than a runtime field. Both `endtoend` and `bind-boundary` depend on this: the end-to-end group
configures its receive latency and too-late-drop setting on sockets that are
already bound, and reads them back, so a tree that locks options at bind time
fails both groups.

**The restriction phase is checked before the value.** A call can be wrong in
both ways at once — an out-of-range value offered in a phase where the option is
already refused — and then only one error code comes back. The phase wins: the
restriction is tested first, and the value is never examined, so `SRTO_FC` set
to -1 on a listening socket reports `SRT_ECONNSOCK` and not the `SRT_EINVPARAM`
the same value produces on a fresh socket, and a pre-bind option given a nonsense
value after `srt_bind` reports `SRT_EBOUNDSOCK` and not `SRT_EINVPARAM`. The
option keeps its previous value either way. `listen-boundary` reads the
distinction directly, so a table that validates the range first, or that folds
both refusals into one code, fails it.

**`SRTO_RCVSYN` and `SRTO_SNDSYN` are exempt from every restriction above.**
Their table rows read `pre`, but the released library accepts a set of either of
them at **any** socket state — bound, listening, connecting, connected, and on
the socket `srt_accept` returned — and the new value takes effect for subsequent
calls. A socket returned by `srt_accept` starts with the listener's value, so a
listener opened non-blocking yields a non-blocking accepted socket whose receive
mode you may then switch back. `listen-boundary` asserts the listening-socket
half of this directly (it sets both on a listening socket and reads them back);
the connected and accepted halves are relied on by every group that receives,
and a tree that refuses them loses those groups.

**`srt_clock_type` has to be exported from the shared library.** The group
resolves it with `dlsym` against the candidate's installed `libsrt.so` rather
than calling it directly, so that a candidate that has not declared the function
loses one group instead of failing to compile a whole family. A definition that
is `static`, that exists only as an inline function in the header, or that is
hidden by a visibility setting, will compile fine and still fail. It must be an
exported symbol with C linkage. The group asserts the exact value 4,
`SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY`. Two facts give it, and the documents you
receive carry both: `srt-docs/API/API-functions.md` states that the default is to
use the system clock as the internal SRT clock, and the identifier table in that
same document maps the `gettimeofday` clock to 4. The configuration in §3 enables
neither the monotonic-clock nor the C++-sync build option, so the selection chain
lands there. What no shipped document states outright is the return value itself,
so it is written here: under the configuration named in §3, `srt_clock_type`
returns 4 and nothing else. Derive it from the build options rather than hard-coding a constant
-- a later milestone turns the monotonic default on and asserts a different
identifier from the same call.

**The driver's `SRT_SOCKOPT` numbering has to match the library's.** The
verifier builds the library with `-DENABLE_EXPERIMENTAL_BONDING=ON` but compiles
its own driver against the installed `<srt/srt.h>` *without* defining that
macro, and every group here addresses options by name through that header. The
two translation units must therefore agree on each option's numeric position, so
no option outside the group-connection set may sit at a position that depends on
the macro: give the macro a default definition of 0 in the public header when it
is not already defined, and keep every other option out of the conditional
block. Get this wrong and calls land on a different option than the one written,
with error codes that look like nothing in this document.

**Recovery only completes with too-late drop off and enough latency.** Nothing
in the documentation says the two must be combined. They must: with drop
enabled, a packet recovered by retransmission after its delivery deadline is
discarded before it reaches the application, and the transfer legitimately loses
data however well ARQ worked. `endtoend` and the three `live` groups all set
`SRTO_TLPKTDROP` to 0 on both ends and a latency comfortably larger than the
relay's delay before connecting.

**Key-material state settles on traffic, not on connect.** `SRTO_RCVKMSTATE` and
`SRTO_SNDKMSTATE` are documented by their value set but not by when they reach
it. Immediately after `srt_connect` returns they may still read
`SRT_KM_S_UNSECURED`; read them after the first messages have moved. The
`encryption` group sets `SRTO_PBKEYLEN` explicitly on both ends rather than
relying on the default, and asserts `SRT_KM_S_SECURED` in both directions.

**Documented here or in `srt-docs/`, deliberately out of scope at this
milestone.** Three behaviours the reading list states are carried by no group
here, and they are recorded so their absence is not read as an omission to fix:
the cross-option constraint between `SRTO_KMPREANNOUNCE` and
`SRTO_KMREFRESHRATE` (a pre-announce above the ceiling the rate implies is
documented as refused; the groups exercise only the permissive side, where a
sentinel-shaped value is accepted); `SRTO_PAYLOADSIZE`'s documented ceiling
(a value far above one path MTU is refused at earlier milestones and the range
rule still holds, but no case here sets a too-large payload size); and
`SRTO_E_SIZE` closing the option enumeration (`instruction.md` requires the
enumerator, and no group names it — its presence is invisible to a behavioural
check by design). Implement all three as the documents describe; nothing here
measures them either way.

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
| `SRTO_MININPUTBW` | **required to compile** |
| `SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY` | **required to compile** |
| `srt_clock_type` | **required to compile** |

Two of those three rows are enumerators, and an enumerator is not a
preprocessor macro: the `#ifndef` restatements the drivers carry for them are
inert against your header, so at the version this milestone requires the
driver's `static_assert` on each restatement compiles the bare name and a
header that
omits either enumerator fails to build the whole `optget` family — all five of
its in-scope groups. The `#ifndef` fallbacks exist only so that the *previous*
milestone's tree, which reports an earlier version, still compiles; they are
not a licence to omit the name here.

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
| `SRTO_MININPUTBW` | `38` |
| `SRTO_NAKREPORT` | `33` |
| `SRTO_PACKETFILTER` | `60` |
| `SRTO_PASSPHRASE` | `26` |
| `SRTO_PBKEYLEN` | `27` |
| `SRTO_PEERLATENCY` | `44` |
| `SRTO_RCVKMSTATE` | `41` |
| `SRTO_RCVLATENCY` | `43` |
| `SRTO_SNDKMSTATE` | `40` |
| `SRTO_TLPKTDROP` | `31` |
| `SRTO_TSBPDMODE` | `22` |
| `SRT_KM_S_SECURED` | `2` |
| `SRT_LIVE_DEF_LATENCY_MS` | `120` |
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
