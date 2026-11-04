# Step 3 — Strict encryption enforcement and key-material state reporting

## 1. Implement this milestone

Extend milestone 02's library with strict encryption enforcement and enhanced key-material state reporting. The workspace still holds `/app` with the drafts, documentation and contract; you add to the library you built at milestone 02.

This milestone brings **strict encryption mode** and three new options governing key exchange timing. All four are new members of the `SRT_SOCKOPT` enumeration, and their numeric values are part of this milestone's interface rather than an implementation choice: `SRTO_SNDDROPDELAY` is **32**, `SRTO_KMREFRESHRATE` is **51**, `SRTO_KMPREANNOUNCE` is **52**, and `SRTO_STRICTENC` is **53**. Those four numbers are restated and each is checked against the `<srt/srt.h>` you install, at compile time, so a library that numbers them differently fails to build rather than quietly failing a behavioural assertion; `TEST_INTERFACE.md` §5 repeats the table. Every other constant you add is yours to number.

* **`SRTO_STRICTENC`** (`53`) — a boolean-valued option, readable and settable before connect, that defaults to **1** (strict mode enabled). It is carried as a 4-byte integer and not as a one-byte `bool`: `srt_setsockflag` takes an `int`, and `srt_getsockflag` writes an `int32_t` and reports `sizeof(int)` in `*optlen`. When enabled, a connection is refused at handshake time rather than established with degraded security if one peer sets a passphrase and the other does not, or if both set passphrases but they differ. The refusal is reported as `SRT_ECONNREJ` from `srt_connect`, and the caller socket falls back to `SRTS_OPENED` rather than `SRTS_BROKEN` — but that state is a report, not an invitation: the refusal spends the socket, and a second `srt_connect` on it is refused with `SRT_ECONNSOCK` while `srt_getsockstate` still reads `SRTS_OPENED`, so a corrected retry needs a fresh socket. Setting `SRTO_STRICTENC` to `0` restores the previous milestone's outcome: a passphrase mismatch connects, the KM state reaches `BADSECRET` or `NOSECRET`, and encrypted payload never decrypts.

* **`SRTO_SNDDROPDELAY`** (`32`) — an integer option controlling sender-side too-late packet drop timing, measured in milliseconds. It is settable both before and after connect (an exception to the usual pre-connect-only restriction for transmission options), reads back the value set, and **defaults to `0`**. The header comment for the option reads *"Extra delay towards latency for sender TLPKTDROP decision (−1 to off)"*: `−1` is the explicit *off* sentinel, which must read back like any other value set, and it is not the default. The option did not exist at the previous milestone; a library carried forward unchanged will not have it.

* **`SRTO_KMREFRESHRATE`** (`51`) and **`SRTO_KMPREANNOUNCE`** (`52`) — two integer options governing when the sender regenerates the AES key and how many packets in advance it announces the new key. Both are **write-only** (settable before connect, but `srt_getsockflag` refuses them with `SRT_EINVOP`) and validate their relationship: setting `KMPREANNOUNCE` to a value exceeding `(KMREFRESHRATE−1)/2` is refused with `SRT_EINVPARAM`. When `KMREFRESHRATE` is set, `KMPREANNOUNCE` is automatically clamped to the maximum if it was zero or too large.

The behaviour you implemented at milestone 02 — where a passphrase mismatch connects, KM state shows `BADSECRET` or `NOSECRET`, and payload is never decrypted — is preserved exactly when `SRTO_STRICTENC` is explicitly set to `0`. The default is the opposite: strict enforcement rejects the connection before any payload is exchanged.

`SRTO_PBKEYLEN` still reads back `0` on a fresh socket, as it did at milestone 01, and setting it to 16 or 32 on the sending side is what fixes the key length the two peers agree on — on a connected socket, `srt_getsockflag(SRTO_PBKEYLEN)` reports the running key length rather than the configured one. Do not read that `0` as *no encryption*: at this release a sender that sets `SRTO_PASSPHRASE` and leaves the key length at `0` falls back to a 16-byte key and the connection is encrypted. An explicit `SRTO_PBKEYLEN` buys a **stated** key length, not encryption itself.

The key material travels in the HSv5 handshake rather than waiting for traffic, which is what lets a mismatch be refused before the connection is established at all. With a matching passphrase on both sides, `SRTO_SNDKMSTATE` and `SRTO_RCVKMSTATE` already read `SRT_KM_S_SECURED` on both sockets when `srt_connect` returns, before any message is sent, and the first message sent decrypts. With no passphrase at either end the state stays `SRT_KM_S_UNSECURED`. The observed state is read by polling while it reads `SRT_KM_S_SECURING`, so that an implementation which settles a moment later is not failed for it; there is never a fixed-interval sleep, and nothing is asserted about what an idle connection does over one.

## 2. What you inherit

Milestone 02 brought HSv5, `SRTO_STREAMID`, bidirectional mode, and the options that let file and live mode coexist. All of that must keep working: the HSv5 handshake, the streamid passthrough, the `SRTO_MINVERSION` floor, live and file transmission types, message-API vs. stream-API selection, and the negotiated payload size. A regression in any of those would break a capability this chain has already established.

Encryption with a matching passphrase at both key lengths (16 and 32 bytes) must still work exactly as milestone 01 defined it, and loss recovery with too-late drop disabled must still deliver the payload intact when the relay drops datagrams at fixed indexes.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 4 **gate**
groups that check the milestones you inherited are still intact, and 7
**behaviour** groups covering this milestone's own work. All of them are in
scope.

The gate must hold: break something an earlier milestone established and this
step is not satisfied, however much of the new work you finished. Preserving
what you inherited and implementing nothing new does not satisfy this step
either. Every behaviour group is required; a group you do not reach is a gap in
the step.

One thing qualifies that, and it is worth knowing before you plan the work: the
drivers are built as one test binary per driver *family*, and every requirement in
a family depends on the family compiling — a family that does not compile
affects every group in it, not only the group whose capability is missing. That
is why section 5 of `TEST_INTERFACE.md` marks every public name
it lists either **required to compile** or optional. A name marked required is
one a driver references without a fallback guard, so a tree that does not
declare it affects the whole family; a name marked optional is guarded, and
missing it affects only the group that exercises it. Which names fall on which
side is measured from the drivers each time this document is generated, so a
section 5 that marks nothing as required means this milestone introduces no
unguarded name -- not that the distinction does not apply.

Everything is measured through the installed public interface: `/app` is
built with

```
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF  \
      -DCMAKE_INSTALL_PREFIX=/usr/local && make -C <build> -j4 \
  && make -C <build> install DESTDIR=<dest>
```

and then separate programs are compiled against `<dest>/usr/local`, including
only `<srt/srt.h>`, and linked with `-lsrt` together with the system thread and
dynamic-loader libraries -- nothing else of yours. If that build or that install does not
produce `include/srt/srt.h` and `lib/libsrt.so*`, no group can run.

`/app/PROFILE.md` §2 lists what is out of scope for the whole task, §5 lists
the values the specifications leave open and what this task does about them,
and §6 states the determinism requirement.

## 4. Documentation

- `/app/srt-spec/draft-sharabayko-srt-01.txt` — the normative SRT wire specification.
- `/app/srt-spec/draft-gg-udt-03.txt` — the UDT specification the core derives from.
- `/app/srt-docs/` — upstream API documentation pinned to this milestone; see
  `srt-docs/PROVENANCE.md` for which release each file comes from.

## 5. Time

Run `/app/timer.sh` to see how much of this milestone's budget is left.

## 6. Constraints

This environment has no network access, and everything the build needs is already
installed. The upstream project's source, its releases, mirrors and package
registries are unreachable, so a copy cannot be fetched by any route.
