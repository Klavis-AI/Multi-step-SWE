# Step 11 — Path MTU discovery, a rebuilt recovery module, and spec-exact packet numbers

## 1. Implement this milestone

One new protocol mechanism, one large internal reorganisation that is
visible from outside, and three smaller corrections.

**Path MTU discovery.** A new `quiche/src/pmtud.rs` carries a `Pmtud` type
and `Config::discover_pmtu(bool)` turns it on. A connection with discovery
enabled starts from the conservative 1200-byte floor every QUIC endpoint is
required to support, and once the handshake is confirmed *and*
HANDSHAKE_DONE has been sent it offers a padded probe the size of
`max_send_udp_payload_size`. If the probe is acknowledged the path estimate
rises to the probe size and the connection's own datagrams are free to use
it; if the probe is lost the search halves the interval between the
confirmed size and the failed one and tries again. While discovery is
enabled and unfinished the connection caps its output at the current
estimate rather than at the configured maximum, which is the whole point of
the mechanism: it is conservative until it has evidence. A successful probe
emits the `MtuUpdated` connectivity event step 10 added to the qlog schema.
Probing is a server-side activity at this milestone -- the gate is
`handshake_confirmed && handshake_done_sent`, and only a server sends
HANDSHAKE_DONE.

`quiche-server` grows an `enable_pmtud` argument that it feeds straight to
`Config::discover_pmtu`, and **the flag that would set it is missing from
the binary's own usage string**, so `quiche-server --enable-pmtud` exits
with `Unknown flag: '--enable-pmtud'` and the field is always `false`. That
is what the reference does, verified by running it.

It is written down so that the argument sitting unused in `ServerArgs` reads
as the upstream state of affairs rather than as a feature you forgot, and
**it is deliberately out of scope for this step**. No group exercises the
flag, no group checks that it is refused, and a candidate whose
`quiche-server` *does* declare `--enable-pmtud` in its usage string is not
wrong anywhere in the package. Asserting the refusal would
be asserting bug-compatibility with an upstream oversight: it would fail a
candidate for being better than the release it is tracking, which is the one
thing a task built out of successive releases must never do. What this step
does require is the plumbing -- `ServerArgs::enable_pmtud` reaching
`Config::discover_pmtu` -- and `cap-pmtud` covers the feature itself over a
surface that can see it. `quiche-client` has neither the argument nor the
flag.

**The recovery module is taken apart and rebuilt.** Everything that was a
flat file under `quiche/src/recovery/` moves into a `congestion` submodule,
and the round-trip estimator becomes a component of its own:

| step 10 | step 11 | | --- | --- | | `recovery/reno.rs`, `cubic.rs`,
`bbr/`, `bbr2/` | `recovery/congestion/` (same names, one level down) | |
`recovery/pacer.rs`, `prr.rs`, `hystart.rs`, `delivery_rate.rs` |
`recovery/congestion/` | | (round-trip state inside `recovery/mod.rs`) |
`recovery/rtt.rs` |

`quiche/src/crypto.rs` is split the same way, into `crypto/mod.rs` with
`crypto/boringssl.rs` and `crypto/openssl_quictls.rs` beside it. None of
this changes the public API by a single name, and all of it changes where a
reader looks -- so it is checked as layout, never as behaviour. The one
behavioural trace it leaves is that `Stats` gains an `acked_bytes` counter,
which the rebuilt acknowledgement path now has a number to report.

**The anti-amplification factor becomes configurable.** The 3x bound itself
is not new -- it has been in the library since well before this milestone,
as a `MAX_AMPLIFICATION_FACTOR` constant -- but
`Config::set_max_amplification_factor` is, and the default stays 3. A server
given a factor of 1 may answer a 1200-byte client Initial with exactly 1200
bytes and no more; a server given 0 may not answer at all.

**Wire error codes become public.** `quiche::WireErrorCode` and
`quiche::h3::WireErrorCode` name the numbers RFC 9000 Section 20.1 and RFC
9114 Section 8.1 assign, and the library's own internal error-to-wire
mapping is written in terms of them. `Error` gains `CryptoBufferExceeded`
(0xd) to go with the new `WireErrorCode` entry of the same name.

**Packet-number encoding becomes spec-exact.** RFC 9000 Section 17.1 says
the number of bytes a sender uses for a truncated packet number must cover
the range of packets that could still be in flight -- it is a function of
the packet number *and* the largest acknowledged packet, not of the packet
number alone. The helper that chooses the length is given the second
argument, and the helper that writes the bytes is given the chosen length
rather than recomputing it.

Also new, and required to exist rather than checked directly: a public
`h3::grease_value` and a public `h3::frame` module. No group at this step
calls either; they are listed so that their absence is a contract violation
rather than a surprise.

`Connection::qlog_streamer` is new as well and is a weaker requirement
still. It sits behind the crate's optional `qlog` feature, and the candidate
is never built with that feature on, so **nothing in this package compiles
the method at any step** -- it is required by the release this milestone
tracks and observed by nothing. Turning the feature on for that build would
change the build frame for all fifteen steps to buy one symbol, which is not
a trade this package makes.

`h3::Config::set_additional_settings` is new too and is deliberately **not**
in that list: it is in scope, through `cap-h3-additional-settings`, whose
contract appears in full below.

No public function is removed at this milestone. The breaks are in shapes
and in numbers.

**`Stats` gains a field.** `acked_bytes: u64` joins the struct. Anything
that destructures `Stats` exhaustively, or builds one with a struct literal,
stops compiling. Code written against step 10 that did exactly that had to be updated.

**`Error` gains a variant.** `CryptoBufferExceeded` is **appended**, after
`KeyUpdate`, so every earlier variant keeps its position; but the enum is
not `#[non_exhaustive]`, so an exhaustive `match` over `quiche::Error` stops
compiling all the same. `h3::Error` is untouched.

**The number of bytes on the wire for a packet number changes.** At step 10
the length came from the magnitude of the packet number: below 255 it was
one byte, and from 255 upwards two. At step 11 it comes from how many
packets could still be unacknowledged. A connection whose peer is
acknowledging promptly therefore keeps a one-byte packet number
indefinitely, where step 10 switched to two bytes at packet 255 and never
switched back. Any expectation pinned to the old rule is now wrong, in both
directions: a sender that always emits one byte is as wrong as one that
emits two.

**`crypto.rs` and the flat `recovery/*.rs` files no longer exist.** Their
contents are one directory level down. Nothing outside the crate referred to
them by path, but a patch against the old layout will not apply.

What is newly in scope here:

* `cap-pmtud` — Four legs over in-memory `connect`/`accept` pairs, public
  API only. Both endpoints are configured with
  `set_max_send_udp_payload_size(1452)` and
  `set_max_recv_udp_payload_size(1452)` in every leg, so the only thing that
  differs between the two configurations is `discover_pmtu`. Every leg is a
  differential between those two connections, which is what stops a datagram
  budget, a pacing quantum or a buffer size from satisfying any of them.

  **On -- the server probes.** With `discover_pmtu(true)` on both sides the
  pair handshakes and settles, and the length of every datagram each side
  produced is recorded. At least one **server** datagram is exactly
  1452 bytes, and every server datagram is either at or under 1200 or
  exactly 1452, with nothing in between: ordinary output is held to the path
  estimate and only a padded probe leaves it. Measured against the
  reference: `[1200, 358, 439]` followed by nine 1452-byte probes. Neither
  the count nor the position is asserted. The count is a function of the
  congestion window at the moment the handshake completes, and the position
  is not "after the handshake" at all -- the reference emits its first probe
  *inside* the handshake flight, on the first `send` after HANDSHAKE_DONE.
  The window the group reads is therefore the whole connection up to the
  point application data is queued, not the part that follows `handshake()`.

  **Off -- nothing above the floor.** With `discover_pmtu(false)` on both
  sides, the same configuration and the same drive loop, **no** server
  datagram exceeds 1200 at all: `[1200, 358, 439]` and nothing else. This is
  the leg that makes the first one discriminating.

  **The client is clamped, not probing.** `Path::should_send_pmtu_probe` is
  gated on `handshake_confirmed && handshake_done_sent`, and only a server
  ever sets `handshake_done_sent`, so a client cannot leave the floor
  however it is configured. The consequence is visible, and it is the
  reverse of what the plan row guessed: with discovery *on*, a client
  pushing a 64 KiB transfer is limited to the 1200-byte estimate it can
  never raise, because `send` caps its output at `min(pmtud.get_current(),
  buffer)`; with discovery *off* the same client on the same transfer runs
  at 1452, because nothing caps it. Measured client histograms: `{43: 1,
  483: 1, 904: 1, 936: 1, 1050: 1, 1200: 54}` on, and `{43: 1, 483: 1, 932:
  1, 962: 1, 1452: 45}` off. Both directions are asserted -- the on leg as
  an upper bound, the off leg as an existence claim -- so the bound cannot
  be met by a connection that merely never had 1452 bytes to send.

  **Survival.** Both configurations carry the 64 KiB body to completion
  byte-for-byte with the stream finished, and neither end of the
  on-configuration pair is closed afterwards. A search that raises the
  estimate and then black-holes the connection is not discovery.

  The binary-search-on-loss leg is deliberately **absent**. Driving it
  requires discarding the probe and then advancing the connection until the
  loss is detected, and on an in-memory pair with no real clock the
  reference needed more timer ticks than a bounded test can justify before
  it re-probed at a smaller size -- measured: six `on_timeout` ticks with
  nine probes dropped produced no smaller probe. An assertion that cannot be
  driven deterministically against the reference does not belong in a group.

  `PathStats::pmtu` is **not** read. `Path::stats` reports
  `recovery.max_datagram_size()` there, which is the configured
  `max_send_udp_payload_size` from the moment the connection is created --
  measured 1452 in *both* configurations before either has sent a packet --
  so it cannot tell them apart. A group that asserted on it would be
  asserting on the configuration it had just set.
* `cap-pn-encode-exact` — The packet-number length field, read out of the
  candidate's own 1-RTT headers. The client is configured with
  `Config::log_keys()`, `CLIENT_TRAFFIC_SECRET_0` is pulled out of the
  resulting NSS keylog, the generation-0 `PacketKeys` are derived from it,
  and for every short-header datagram the client produces, header
  protection is removed and the `len` the unmasked first byte declares is
  read. That is
  the field RFC 9000 Section 17.1 governs and the field this milestone
  changed, observed as a field rather than inferred from a total. Both sides
  set `set_initial_congestion_window_packets(600)` so the congestion window
  is never the thing under test.

  Before either phase runs, the keys are proven right by
  opening one of the client's packets. If that fails the group says so in
  those words -- a key-schedule disagreement is not a packet-number fault
  and must not be reported as one.

  **Phase A, peer acknowledging.** 300 rounds, each one a small write from
  the client and the server's reply fed straight back, so acknowledgements
  keep arriving and the unacknowledged range stays tiny. **Every** client
  1-RTT packet in this phase has a packet-number length of **1**, and the
  phase runs long enough that the client's packet number passes 255 -- which
  is confirmed by reconstructing the number, so "the packet number
  never got high enough" cannot be mistaken for "the length stayed narrow".
  Under step 10's magnitude rule the length steps to 2 at packet 255 and
  never returns, so the group fails against step 10, as it must.

  **Phase B, peer silent.** 500 further rounds on the *same* connection with
  the server's output discarded, so `largest_acked` freezes while the packet
  number keeps climbing. At least one client packet now has a packet-number
  length of **2**, and at least one still has **1** -- the width follows the
  unacknowledged range up, which is the whole content of the rule, and it
  does not simply jump to the maximum. It is also asserted that the
  phase was not blocked by flow control or congestion control, so "no packet
  was produced" cannot be mistaken for "the packet was narrow".

  Phase A is what a candidate that keeps the old magnitude rule fails. Phase
  B is what a candidate that hardcodes a one-byte packet number fails. Both
  are required and neither is sufficient.

  Datagram *lengths* were how this was first measured and they agree -- tree
  11 gives `{53: 289, 58: 2, 59: 9}` in phase A where tree 10 gives `{53:
  244, 54: 45, ...}` -- but the shipped assertion reads the header field,
  because a datagram length is a sum of many things and only one of them is
  under test here.

  Nothing is asserted about decoding. Decoding did not change, and a group
  that tested it would be checking step 09's work as this step's.
* `cap-anti-amplification-factor` — The socket and the peer are both
  independent of the candidate. A client Initial is built independently,
  sent to the candidate's server, and the bytes that come back are counted.

  Getting a *real* ClientHello into that Initial matters, and the group
  takes the long way round to do it. A stub CRYPTO payload draws only a
  small ACK from the server, which leaves even a factor of 1 non-binding and
  the group vacuous. So a throwaway `quiche::connect` client is stood up
  first; its first Initial is decrypted with
  `support::decrypt_initial(.., Perspective::Client)`, and the CRYPTO
  bytes are lifted out and rebuilt into an **independent** Initial with
  `support::ClientInitial` -- same version, same connection IDs, padded to
  the mandatory 1200 -- which is then encrypted with independently derived
  keys. The candidate's library is used to produce a ClientHello and for
  nothing else; the packet on the wire is built independently of it.

  One more piece of stage-setting, and it is what makes the group exact. The
  budget only discriminates when the server has more to send than it is
  allowed to, and the harness's single small certificate makes a first
  flight of only 1558 bytes -- under 3 x 1200, so the default and every
  factor from 2 upwards spend the same amount and the group could not tell 3
  from 300. That certificate is therefore written eight times into one
  chain file and loaded as the server's chain, which
  takes the unconstrained flight to 7702 bytes. Nothing validates the chain:
  this group never runs a `quiche` client, and the throwaway one it borrows
  a ClientHello from sets `verify_peer(false)`.

  Seven legs against a server configured per leg, every one of
  them answering the same 1200-byte Initial:

  | leg | `set_max_amplification_factor` | required reply | | --- | --- |
  --- | | 1 | not called (default) | **exactly** 3600 bytes | | 2 | `5` |
  **exactly** 6000 bytes | | 3 | `4` | **exactly** 4800 bytes | | 4 | `3` |
  **exactly** 3600 bytes, and the same total as leg 1 | | 5 | `2` |
  **exactly** 2400 bytes | | 6 | `1` | **exactly** 1200 bytes, in exactly
  one datagram | | 7 | `0` | **nothing at all** |

  The right-hand column is not a table of constants baked into the test. It
  is computed as `min(free, factor * 1200)`, where `free` is measured in a
  calibration leg with the factor set to 1000 and the test refuses to run
  unless `free` exceeds `5 * 1200` -- so the legs bracket the default from
  both sides and a candidate whose certificate flight differs in length from
  the reference's is still checked against the formula rather than against
  the reference's numbers. This is the shape of the reference's own unit
  test, `server_sent == client_sent * FACTOR`, with `client_sent` fixed at
  1200 by construction. Measured against the reference over the
  eight-certificate chain: 7702 bytes in seven datagrams unbounded, then
  6000/4800/3600/2400/1200/0 at factors 5/4/3/2/1/0, and 3600 with the
  setter never called.

  Legs 1 and 4 together are what pin the default at exactly 3 rather than at
  "some number of at least 2": the default spends 3600 and stops, while 4800
  and 6000 were both available and were both reached when asked for.

  A further check makes leg 6 a statement about content rather than about a
  number. The single datagram the server sends under factor 1 is decrypted
  with independently derived Initial keys and parsed: it is a server Initial
  carrying a CRYPTO frame whose first byte is `0x02`, a ServerHello. So the
  allowance truncates a real flight mid-stream; it does not suppress the
  flight, and it does not substitute padding or a CONNECTION_CLOSE.

  Note what this group does **not** claim. The 3x limit itself is not new at
  this milestone -- step 10's tree has the same constant, the same
  enforcement and the same unit assertion. Only the setter is new, so the
  legs that could hold without it (1 and 4's equality with it) are controls,
  and the discriminating legs are the five that ask for a factor the default
  would not produce.
* `cap-recovery-and-crypto-layout` — An emitted artefact -- the crate's
  own directory layout, reached through `testkit::candidate_root()` --
  checked against a fixed expectation table. This is the one toolbox
  exception to no-source-reading, and it is checked as layout and never as
  behaviour: not a line of any of these files is read, only whether the path
  exists.

  **Must exist:**

  ```
  quiche/src/crypto/mod.rs
  quiche/src/crypto/boringssl.rs
  quiche/src/recovery/rtt.rs
  quiche/src/recovery/congestion/mod.rs
  quiche/src/recovery/congestion/reno.rs
  quiche/src/recovery/congestion/cubic.rs
  quiche/src/recovery/congestion/pacer.rs
  quiche/src/recovery/congestion/bbr/
  quiche/src/recovery/congestion/bbr2/
  quiche/src/recovery/congestion/bbr/mod.rs
  quiche/src/recovery/congestion/bbr2/mod.rs
  ```

  The directories and the modules inside them are both listed because both are
  checked: a `bbr/` holding only `bbr.rs` is not the layout this group reads
  as done.

  **Must not exist:**

  ```
  quiche/src/crypto.rs
  quiche/src/recovery/reno.rs
  quiche/src/recovery/cubic.rs
  quiche/src/recovery/pacer.rs
  quiche/src/recovery/bbr/
  quiche/src/recovery/bbr2/
  ```

  `quiche/src/recovery/mod.rs` is required to exist and is on neither list's
  interesting half -- it exists before and after, and saying so keeps a
  candidate from reading the "must not exist" list as "delete the
  directory".

  `quiche/src/crypto/openssl_quictls.rs` is **not** required. It is a second
  backend the reference happens to carry, and a candidate that builds only
  against BoringSSL -- which is what every other group in all fifteen steps
  requires -- has no reason to have written it.

  Both halves are needed. The first alone is satisfied by a candidate that
  copies files into the new places and leaves the old ones behind, which is
  not a reorganisation; the second alone is satisfied by deleting them.

  Nothing about `RttStats` or the congestion-control trait is asserted, in
  this group or anywhere in this step. Both are private, and a public
  assertion about a private type would be an assertion about the reference's
  source rather than about a contract.
* `cap-wire-error-codes` — Two legs: a fixed expectation table over the
  two new public enums, and one live protocol violation that binds the table
  to the wire.

  **The table.** Every variant of `quiche::WireErrorCode` and of
  `quiche::h3::WireErrorCode` is cast with `as u64` and compared against RFC
  9000 Section 20.1 and RFC 9114 Section 8.1 -- seventeen values each, `0x0`
  through `0x10` and `0x100` through `0x110`, asserted in two batches so a
  wrong value reports which enum it was in. The names are the contract too:
  the test names every variant explicitly, so a candidate that ships sixteen
  of seventeen does not compile, which is the correct outcome for a missing
  public item. `quiche::Error::CryptoBufferExceeded` is named in the same
  leg for the same reason, and is asserted to be a variant distinct from the
  long-standing `quiche::Error::CryptoFail`: a connection that buffered more
  CRYPTO than it can hold has not failed to decrypt anything, and the two
  must not compare equal.

  **The wire.** An in-memory pair is driven where the *client* runs
  `h3::Connection` and the *server* side is raw QUIC with bytes written onto
  it directly. On server-initiated unidirectional stream 3 the following is
  written by hand: the control-stream type varint `0x00`, an
  empty SETTINGS frame (`0x04 0x00`), and then a DATA frame (`0x00 0x01
  0x41`). RFC 9114 Section 7.2.1 does not permit DATA on a control stream.
  The client's `h3::Connection::poll` must return
  `Err(h3::Error::FrameUnexpected)`, and its transport connection's
  `local_error()` must carry `error_code ==
  h3::WireErrorCode::FrameUnexpected as u64` -- that is, `0x105` -- with
  `is_app == true`.

  The control stream is written by hand on the raw side rather than by
  reaching for the candidate's own control stream by number.
  Which stream id a candidate
  opens its control stream on is not part of any contract in this package,
  and a group that assumed 3 would be testing an implementation detail.

  The `is_app` flag matters as much as the number does: `0x105` sent as a
  *transport* error would be `0x105` in the wrong namespace, and RFC 9114's
  codes travel in an application close.

  The private `Error::to_wire` mapping is not called, because it cannot be.
  Leg two is what proves the enum is the mapping's source of truth rather
  than a decorative table beside it.
* `cap-acked-bytes-stat` — `Stats::acked_bytes` over an in-memory pair, in
  four steps:

  1. immediately after `connect`, before a single packet is exchanged, the
  client's `acked_bytes` is **0**; 2. after the handshake completes it is
  greater than 0 -- the handshake itself is acknowledged traffic; 3. a 256
  KiB body is sent and received byte-for-byte; afterwards the sender's
  `acked_bytes` is at least the length of the body. A lower bound, never an
  equality: the counter includes packet overhead and every handshake byte,
  and pinning a total would pin this build's packetisation; 4. reading it
  again without exchanging anything further returns the same value, and a
  further transfer only raises it. The counter is monotone.

  The receiving side is checked too: it has acknowledged very little
  application data of its own, so its `acked_bytes` is far below the body
  length while the sender's is above it. That asymmetry is what
  distinguishes the new counter from `sent_bytes` or `recv_bytes`, either of
  which a candidate might alias it onto.

  This is the one place the rebuilt recovery module is checked behaviourally,
  and it is checked through the number it now has to report rather than
  through where its source lives.
* `cap-h3-additional-settings` — `h3::Config::set_additional_settings` on
  one endpoint, observed through `peer_settings_raw()` on the other. The
  observation channel is not new -- `peer_settings_raw` exists at step 10 as
  well -- and that is deliberate: it means the group covers the new sending
  capability and not a new accessor.

  **Validation, before anything reaches a wire.** `set_additional_settings`
  returns `Err(h3::Error::SettingsError)` for each of the six identifiers
  the library sends itself -- `0x1`, `0x6`, `0x7`, `0x8`, `0x33`, `0x276` --
  each tried in its own call, and for a single call containing the same
  unknown identifier twice. It returns `Ok(())` for a list of two distinct
  unknown identifiers.

  **Delivery.** A client configured with `[(0x4f4f, 0x1234), (0x9a9a,
  0x5678)]` completes an HTTP/3 handshake with a server that has no
  additional settings of its own. The server's `peer_settings_raw()` is
  `Some`, contains both pairs with both values intact, and contains them in
  the order they were configured. The list also contains the identifiers the
  library sends on its own account, which is why membership and relative
  order are asserted rather than equality against a fixed list -- how many
  settings a conforming implementation sends unprompted is not this step's
  business.

  **The reverse direction** is checked once, with the server carrying
  `[(0x7f7f, 0x99)]` and the client reading it, so a candidate that wires
  the new setting into only one of the two roles fails.

  Nothing is asserted about the HTTP/2-reserved identifiers `0x0` and `0x2`
  through `0x5`. RFC 9114 requires a *receiver* to reject them, and the
  reference accepts them in `set_additional_settings` and lets the peer do
  the rejecting; a group that demanded rejection at the setter would be
  demanding something the reference does not do.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 10 left working. 2 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 14 (datagram size), 14.1 (initial datagram
  size), 14.2 (path maximum transmission unit), 14.3 (datagram size during
  and after handshake), 14.4 (sending QUIC PMTU probes), 8 (address
  validation), 8.1 (address validation during connection establishment),
  17.1 (packet number encoding and decoding), A.2 (sample packet number
  encoding algorithm), 20.1 (transport error codes). Section 14 is the whole
  of the first feature. 14.1 is where the 1200-byte floor comes from and why
  a connection with discovery enabled starts there rather than at whatever
  the socket would allow: an endpoint MUST NOT send an initial packet larger
  than the path is known to support, and 1200 is the size every path is
  required to support. 14.2 and 14.3 say the estimate may rise only on
  evidence and must fall back on loss. 14.4 is the probe itself -- padded,
  sent alone, and *not* counted as an ordinary loss when it fails, which is
  exactly why the search can afford to be aggressive.

  Section 8 and 8.1 are the anti-amplification limit: the three-times bound,
  that it is measured in bytes received rather than datagrams, and that a
  server subject to it sends *less* rather than refusing to send. Read
  together with the new setter they are why factor 1 yields a truncated real
  flight rather than silence.

  17.1 and A.2 are the packet-number rule, and A.2 in particular is the
  algorithm the library now implements literally: the number of bits must be
  at least one more than the base-2 logarithm of the number of packets that
  could still be in flight. Read 17.1 for why the old magnitude-based rule
  was not merely inefficient but wrong -- it is the *unacknowledged range*,
  not the absolute value, that determines whether the receiver can
  reconstruct the number.

  20.1 is the table `quiche::WireErrorCode` now names, value for value.
* `/app/spec/rfc9114.txt` — 8.1 (HTTP/3 error codes), 6.2.1 (control
  streams), 7.2.1 (DATA frames), 7.2.4 (SETTINGS frames), 7.2.4.1 (defined
  SETTINGS parameters), 7.2.4.2 (initialization). 8.1 is the table
  `h3::WireErrorCode` names. 6.2.1 with 7.2.1 is the violation that binds
  one entry of that table to real behaviour: DATA frames are not permitted
  on a control stream, and an endpoint that receives one MUST respond with
  H3_FRAME_UNEXPECTED -- 0x105, the number the new enum spells.

  7.2.4 and 7.2.4.1 are the reason `set_additional_settings` refuses the
  identifiers it refuses and accepts everything else: an endpoint MUST
  ignore settings it does not understand, which is what makes sending
  unknown identifiers safe, while sending a *known* identifier twice in one
  frame is a connection error. 7.2.4.2 is why the receiving side can report
  them all faithfully in order.
* `/app/spec/rfc9002.txt` — 2 (conventions and definitions), 5 (estimating
  the round-trip time), 6.2 (probe timeout), 7 (congestion control).
  Background for the module that was rebuilt, and the reason the rebuild is
  checked as layout rather than behaviour. Section 5's round-trip estimator
  is now a component of its own in `recovery/rtt.rs`, and Section 7's
  controllers are now a `congestion` submodule behind one trait -- but RFC
  9002 specifies none of that structure, and it specifies no observable that
  distinguishes the new arrangement from the old. What it does supply is the
  one behavioural trace the rebuild leaves: Section 2's definition of an
  acknowledged packet is what the new `Stats::acked_bytes` counts.

**Additional files:**

* `/app/TEST_INTERFACE.md` — the contract between your code and the
  checker: every name, path and flag it depends on, the group table it
  defines, and what is deliberately out of scope. Where it and your intuition
  disagree, it wins.
* `/app/WORKSPACE_HANDOFF.md` — what the previous step left behind and
  which of its milestones remain in scope here.
* `/app/spec/README.md` — which sections of which document are
  load-bearing for this step, and `SHA256SUMS` proving the corpus is
  byte-identical to the IETF originals.
* `/app/spec/appendix-a-vectors.json` — RFC 9001 Appendix A's worked
  example as named byte strings. The checker carries a byte-identical copy of
  the same file in its own harness; no group at this step reads either one, so
  it is a reference for your own plumbing rather than a check that runs.
* `/app/.cargo/config.toml` — `QUICHE_BSSL_PATH` points at the BoringSSL
  prebuilt into the image. Link it; do not fetch or compile one. The
  build must succeed with no network.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `spec/`) or weaken, filter, rename,
  or disable tests. Run the references' local checks and leave a reproducible
  handoff.
- This milestone is modelled on a real crate. Depending on, vendoring, or
  copying source from `quiche`, `tokio-quiche`, `quinn`, `s2n-quic`, `neqo` or
  any other QUIC or HTTP/3 implementation is a task-specific answer and is out
  of bounds; the tree is built offline and a manifest that names one will not
  resolve. Cryptography, byte-buffer and async-runtime crates are ordinary
  dependencies and are fine.
