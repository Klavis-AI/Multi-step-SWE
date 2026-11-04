# Test interface — milestone 3 of 11

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
11 s, measured with the machine otherwise idle. Under contention the same
build can take a third longer, which is still far inside the budget above --
the figure is there to tell you whether your build is in the right
neighbourhood, not to be raced against.

### The version this milestone produces

This milestone is SRT **1.3.2**, and the version is part of what you
install rather than decoration. `<srt/version.h>` is a public installed
header, reached through `<srt/srt.h>`, and at this milestone it must report
1.3.2:

| macro | value |
| --- | --- |
| `SRT_VERSION_MAJOR` | `1` |
| `SRT_VERSION_MINOR` | `3` |
| `SRT_VERSION_PATCH` | `2` |
| `SRT_VERSION_STRING` | `"1.3.2"` |
| `SRT_MAKE_VERSION(major, minor, patch)` | `patch + minor*0x100 + major*0x10000` |
| `SRT_MAKE_VERSION_VALUE` | an alias of `SRT_MAKE_VERSION` |
| `SRT_VERSION_VALUE` | `SRT_MAKE_VERSION_VALUE` of the three numbers above |

Upstream produces that header from `set (SRT_VERSION 1.3.2)` at
the start of a line in `CMakeLists.txt`, which is the ordinary way to
make the macros above report it. Nothing here reads `CMakeLists.txt`:
what is checked is the installed header, so any build that makes it
report 1.3.2 is acceptable.

This is not bookkeeping at this milestone. The verifier compiles
`fam_api.cpp`, `fam_kmx.cpp` conditionally on `SRT_VERSION_VALUE`, so a tree that
implements every behaviour this document asks for and leaves the version
where the previous milestone left it skips those compile-time pins and runs the
drivers' guarded restatements instead. You will not see a compile error, and
nothing about the below-expected branch is a trap: put the constants where
"Constants whose numeric value the verifier pins" says and both branches agree
with your library, whether or not you bump the version.


---

## 2. How the work is checked

There are 4 **gate** groups (§3) and 7 **requirement** groups
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
- `keylen32` — with a matching passphrase and `SRTO_PBKEYLEN=32` on both sides, the payload is carried intact and does not appear in clear in datagrams on the wire
- `recovery` — datagrams dropped at fixed indexes are retransmitted and the payload is delivered intact when too-late drop is disabled
- `sockopt` — the options brought by milestone 02 (`SRTO_STREAMID`, `SRTO_MINVERSION`, `SRTO_TRANSTYPE`, `SRTO_MESSAGEAPI`, `SRTO_PAYLOADSIZE`) are settable at the documented restriction level and readable at it, with `SRTO_TRANSTYPE` write-only as its `[SET]` row says — a get of it is refused with `SRT_EINVOP`, as §5 states

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
| 1 | `strictenc` | `SRTO_STRICTENC` is readable at its default of 1, accepts 0 and 1 before connect, refuses the set after connect, and setting it to 0 restores the v1.3.0 outcome where a passphrase mismatch connects and delivers `BADSECRET` KM state |
| 2 | `caller-pw` | with strict encryption enabled, when the caller sets a passphrase and the listener does not, `srt_connect` returns −1 with `SRT_ECONNREJ` and the caller socket falls back to `SRTS_OPENED`, and that refusal spends the socket: a second `srt_connect` on it is refused with `SRT_ECONNSOCK` while it still reads `SRTS_OPENED`, so a corrected retry succeeds only on a fresh socket |
| 3 | `listener-pw` | with strict encryption enabled, when the listener sets a passphrase and the caller does not, `srt_connect` returns −1 with `SRT_ECONNREJ` and the caller socket falls back to `SRTS_OPENED` |
| 4 | `mismatch` | with strict encryption enabled, two different passphrases are rejected at connect time with `SRT_ECONNREJ` rather than connecting and reporting `BADSECRET` KM state |
| 5 | `snddropdelay` | `SRTO_SNDDROPDELAY` accepts values before and after connect, reads back the set value, and defaults to a documented value distinct from the absence of the option at v1.3.0 |
| 6 | `kmrefresh` | `SRTO_KMREFRESHRATE` and `SRTO_KMPREANNOUNCE` accept values before connect only and refuse the set after connect, `srt_getsockflag` refuses both with `SRT_EINVOP`, and setting `KMPREANNOUNCE` above `(KMREFRESHRATE−1)/2` is refused with `SRT_EINVPARAM` |
| 7 | `endtoend` | an encrypted live stream with strict enforcement enabled crosses a clean relay, arrives byte-identical and in order, and a passphrase mismatch is refused at connect time |

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

**`SRTO_STRICTENC` defaults to 1 (enabled), not 0.** The header comment shipped with this option does not document this default, and the shipped `srt-docs/` does not document `SRTO_STRICTENC` at all — the option shipped ahead of its documentation. A fresh socket reads back `1` before any explicit set. A group asserting the default must not rely on the shipped documents alone.

**How wide each option reads: 4 bytes for this milestone's readable options, 1 byte for the inherited `SRTO_MESSAGEAPI`.** "Boolean" does not by itself settle how many bytes `srt_getsockflag` writes, and both conventions live side by side in this header, so the widths the groups assert are stated here. `SRTO_STRICTENC` is held and returned as an `int32_t`: a read of it succeeds and reports `sizeof(int)` — 4 — in `*optlen`, even though its only meaningful values are `0` and `1`, and a set takes an `int`. `SRTO_SNDDROPDELAY` is likewise a 4-byte integer. The inherited `SRTO_MESSAGEAPI` is the counter-example: it is held as a `bool` and reports `sizeof(bool)` — 1 — which is the width its entry in `srt-docs/API.md` types it as. An implementation that registers `SRTO_STRICTENC` the way `SRTO_MESSAGEAPI` is registered returns the right value in the wrong number of bytes, and the in-scope group reads the width as well as the value.

**The four new options' numeric values are part of the interface.** `SRTO_SNDDROPDELAY`, `SRTO_KMREFRESHRATE`, `SRTO_KMPREANNOUNCE` and `SRTO_STRICTENC` enter the `SRT_SOCKOPT` enumeration at fixed numbers, listed under "Constants whose numeric value the verifier pins" at the end of this section and restated inline in `instruction.md` §1. The verifier's own programs restate each number and cross-check the restatement against the `<srt/srt.h>` you install, at compile time, so a header that numbers them differently fails to build rather than failing an assertion. That is deliberate: a restatement allowed to drift would leave the verifier addressing a *different* option and reporting a pass for the wrong reason. Nothing in the shipped `srt-docs/` states these numbers — the table below is the document that fixes them, and every constant not in it is yours to number, with the one exception that a constant whose *position* a section above fixes as part of the contract, because a group reaches it by number rather than by name, is fixed by that section.

**The strict enforcement mechanism rejects at handshake time, not on first send.** When `SRTO_STRICTENC` is enabled (the default), a passphrase mismatch or a one-sided passphrase causes `srt_connect` to return `−1` with `SRT_ECONNREJ`, and the handshake never completes. The caller socket falls back to `SRTS_OPENED`, not `SRTS_BROKEN`, but that state is a report and not an invitation: the refusal spends the socket, and a second `srt_connect` on it is refused with `SRT_ECONNSOCK` while it still reads back `SRTS_OPENED`, so a corrected retry needs a fresh socket. (The library's internal connecting flag is cleared only on a completed handshake, and the public state query does not consult it.) The refusal is symmetric on the other side too: the listener's `srt_accept` yields **no socket at all** for the refused attempt — a rejected handshake must leave no connection on either end — and the listener goes on to accept the next, valid connection. (The pre-HSv5 path surfaced a broken accepted socket to the listener; that shape fails four groups here.) This is distinct from the previous milestone's outcome, where the connection succeeded, reached `SRTS_CONNECTED`, and the failure was reported only through KM state and through payload that never decrypted.

**`SRTO_SNDDROPDELAY` defaults to 0, and is settable after connect.** Unlike most transmission options, `SRTO_SNDDROPDELAY` accepts a set both before and after `srt_connect` completes. The default is `0`; the header comment describes it as "Extra delay towards latency for sender TLPKTDROP decision (−1 to off)" but does not state the default, and `−1` is the explicit *off* sentinel rather than the default. Groups asserting the default or the post-connect acceptance must not assume the documents state either.

**`SRTO_KMREFRESHRATE` and `SRTO_KMPREANNOUNCE` are write-only.** Both options accept a set before connect and refuse it after connect with `SRT_ECONNSOCK` — and so does `SRTO_STRICTENC`. Three of the four options this milestone introduces are pre-connect, and the code is the same for all three: `srt_setsockflag` of `SRTO_STRICTENC`, `SRTO_KMREFRESHRATE` or `SRTO_KMPREANNOUNCE` on a connected socket — the caller, or the socket `srt_accept` returned — returns `−1` with `SRT_ECONNSOCK`, and leaves the value the socket already had. (`SRTO_SNDDROPDELAY` is this milestone's one exception to that, described above.) But `srt_getsockflag` refuses the two KM options with `SRT_EINVOP` at every socket state, so they cannot be read back even before connect. The validation is asymmetric: setting `KMPREANNOUNCE` to a value exceeding `(KMREFRESHRATE−1)/2` is refused with `SRT_EINVPARAM`, and setting `KMREFRESHRATE` automatically clamps `KMPREANNOUNCE` to that ceiling if it was zero or already too large. Neither option is documented in the shipped `srt-docs/`.

**The same codes apply to every inherited option whose row is `pre`.** The rule generalises beyond the three new options: a `srt_setsockflag` on a connected socket of a pre-connect option the earlier milestones introduced — the groups here do it with `SRTO_LATENCY` and `SRTO_PAYLOADSIZE` on the caller, and with `SRTO_STREAMID`, `SRTO_MINVERSION`, `SRTO_TRANSTYPE`, `SRTO_MESSAGEAPI` and `SRTO_PAYLOADSIZE` on either end — returns `−1` with `SRT_ECONNSOCK`, on the socket that called `srt_connect` and on the socket `srt_accept` returned alike. A value outside a documented range, or above a documented ceiling, is refused with `SRT_EINVPARAM` and stored nowhere — the payload size keeps its previous value after a refused set, the stream id keeps its string. Do not answer `SRT_EINVOP` for a defined option that is merely in the wrong phase: `SRT_EINVOP` means the option id itself is unknown, and one group here is a gate. The one **defined** option where `SRT_EINVOP` is the required answer is the one that has no getter at all: `srt_getsockflag(SRTO_TRANSTYPE)` returns `−1` with `SRT_EINVOP` on a fresh socket and on a socket that has already accepted a set alike — its `[SET]`-only row implies the refusal, milestone 02's document states it, and this milestone's `sockopt` gate asserts it. That is the option refusing the *operation*, not the id being unknown, and the distinction runs both ways: a tree that answers that get, or refuses it with a different code, loses the same gate a tree that answers `SRT_EINVOP` to a wrong-phase set does.

**`SRTO_RCVSYN` and `SRTO_SNDSYN` are exempt from all of it.** `srt-docs/API.md` types both as `pre`, and none of the refusal rules above reach them: the released library accepts a set of either at **any** state — including on a connected socket and on the socket `srt_accept` returned — and the new value takes effect for subsequent calls. A socket returned by `srt_accept` starts with the listener's value, so a listener opened non-blocking yields a non-blocking accepted socket whose receive mode you may then switch back. Treating the `pre` legend as universal for these two and refusing the set loses every group that receives on an accepted socket.

**An explicit `SRTO_PBKEYLEN` fixes the key length; it does not switch encryption on.** `SRTO_PBKEYLEN` still reads back `0` on a fresh socket, unchanged from milestone 01 — but at this release `0` does not mean "transmits in clear". A sender that sets `SRTO_PASSPHRASE` and leaves the key length at `0` falls back to a 16-byte key: key material is exchanged, the payload is encrypted, and the keying state on both sides reads `SRT_KM_S_SECURED` rather than staying `SRT_KM_S_UNSECURED`. What an explicit `SRTO_PBKEYLEN` of 16 or 32 on the sending side buys is a **stated** key length, and that is why every group asserting a particular length sets it explicitly and then reads it back from the connected socket, where `srt_getsockflag(SRTO_PBKEYLEN)` reports the running key length rather than the configured one.

**The key material travels in the handshake, so a matching-passphrase link is keyed before any payload moves.** At this release the key material is carried in the HSv5 handshake rather than announced from a sender-side timer once the connection is up — which is what makes refusing a mismatch *at handshake time*, the subject of this whole milestone, possible at all. With a matching passphrase on both sides, `srt_connect` returns with `SRTO_SNDKMSTATE` and `SRTO_RCVKMSTATE` already reading `SRT_KM_S_SECURED` on the caller and on the socket `srt_accept` returned, before a single message has been sent; the first message sent decrypts, and a group here asserts `pktRcvUndecryptTotal == 0` across a whole stream rather than sending warm-up traffic and skipping it. With no passphrase at either end the state stays `SRT_KM_S_UNSECURED`. A group that needs a settled state nevertheless reads the *observed* state and polls while it reads `SRT_KM_S_SECURING`, rather than sleeping a fixed interval, so an implementation that settles a moment later than this one is not punished for it: what is required is that it settle, not that it settle at a particular instant. Nothing at this step asserts what the keying state does over an elapsed interval, in either direction.

**Setting `SRTO_STRICTENC=0` restores the previous milestone's outcome exactly.** When strict encryption is explicitly disabled, a passphrase mismatch or one-sided passphrase connects successfully, the socket reaches `SRTS_CONNECTED`, the KM state reports `SRT_KM_S_BADSECRET` or `SRT_KM_S_NOSECRET`, and encrypted payload never decrypts (the `pktRcvUndecryptTotal` counter climbs). This is byte-for-byte the milestone 02 behaviour, preserved as an escape hatch.

**`SRTO_MINVERSION` is readable, and its default is not the `0` the option table gives — that one row is stale.** `srt-docs/API.md`'s row for `SRTO_MINVERSION` marks it **[SET]** and gives its default as `0`. Neither reading holds at this release: `srt_getsockflag(SRTO_MINVERSION)` succeeds and reports 4 bytes, and on a fresh socket it returns `0x010000` — a peer-version floor of 1.0.0, not zero. `PROFILE.md` §5 rule 1 makes a default that `srt-docs/` states for this milestone binding; this is the one option at this milestone where that rule does not hold, and this paragraph is the carve-out, so a candidate who reproduces the release rather than the table is not punished for it. Nothing here checks the figure itself: the group that reads the option asserts that the read succeeds and that the default floor does not exclude an HSv5-capable peer — that the value lies between `0` and `SRT_VERSION_FEAT_HSv5` (`0x010300`) inclusive — which both the table's `0` and the release's `0x010000` satisfy. Every other default in `srt-docs/` remains binding under rule 1.

 The macro `SRT_LOGFA_HAICRYPT`, with value `6`, belongs in the installed `<srt/srt.h>` at this milestone; it was absent at the previous one. Nothing here checks it, and nothing here can: `srt_addlogfa(6)` succeeds whether or not the macro exists — binding an unused log family is not an error — so the only possible check is a compile-time one, and every verifier program in this step also carries a gate group that must compile against the milestone-02 header, where the macro does not exist. A `static_assert` on it would turn an inherited-regression group into a build failure at the previous release, which is precisely what a gate group must never be. It is stated here because it is part of the surface this milestone installs and a later milestone does check it, not because this one does.

**Two inherited names and one structure member are referenced with no fallback.** The verifier's `api` family names `SRT_LIVE_DEF_PLSIZE` and `SRT_LIVE_MAX_PLSIZE` unguarded, and they are `static const int` declarations rather than preprocessor macros, so there is nothing to `#ifndef` against: a translation unit compiled against a header that lacks either does not build, and the groups that family carries are lost. Both names exist in the tree you inherit from milestone 02 — the 1.3.0→1.3.2 transition neither defines nor removes them — and the values behind them are documented in `API.md`: the live default payload size, 1316, and the live maximum, usually 1456. They are absent from the name table below only because that table lists names **new** at this milestone.

The `live` family reads `SRT_TRACEBSTATS::pktSndDropTotal` the same way — the sender's too-late-drop counter, which `g08_recovery`'s `a_clean_channel_needs_no_retransmission` and `datagrams_dropped_at_fixed_indexes_are_retransmitted` both assert stays at zero with `SRTO_TLPKTDROP` off — and no document names that member before this milestone, so a structure carried forward without it fails to build the family. It is a member of the released performance structure, alongside the members milestone 01's section 5 table lists; declare it if your structure does not already carry it. None of the three is in the name table below, which lists names **new** at this milestone.

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
| `SRTO_KMPREANNOUNCE` | optional (guarded) |
| `SRTO_KMREFRESHRATE` | optional (guarded) |
| `SRTO_SNDDROPDELAY` | optional (guarded) |
| `SRTO_STRICTENC` | optional (guarded) |

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
| `SRTO_KMPREANNOUNCE` | `52` |
| `SRTO_KMREFRESHRATE` | `51` |
| `SRTO_SNDDROPDELAY` | `32` |
| `SRTO_STRICTENC` | `53` |

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
