# Test interface — step 11, Path MTU discovery, a rebuilt recovery module, and spec-exact packet numbers

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

One new protocol mechanism, one large internal reorganisation that is visible
from outside, and three smaller corrections.

**Path MTU discovery.** A new `quiche/src/pmtud.rs` carries a `Pmtud` type and
`Config::discover_pmtu(bool)` turns it on. A connection with discovery enabled
starts from the conservative 1200-byte floor every QUIC endpoint is required to
support, and once the handshake is confirmed *and* HANDSHAKE_DONE has been sent
it offers a padded probe the size of `max_send_udp_payload_size`. If the probe
is acknowledged the path estimate rises to the probe size and the connection's
own datagrams are free to use it; if the probe is lost the search halves the
interval between the confirmed size and the failed one and tries again. While
discovery is enabled and unfinished the connection caps its output at the
current estimate rather than at the configured maximum, which is the whole point
of the mechanism: it is conservative until it has evidence. A successful probe
emits the `MtuUpdated` connectivity event step 10 added to the qlog schema.
Probing is a server-side activity at this milestone -- the gate is
`handshake_confirmed && handshake_done_sent`, and only a server sends
HANDSHAKE_DONE.

`quiche-server` grows an `enable_pmtud` argument that it feeds straight to
`Config::discover_pmtu`, and **the flag that would set it is missing from the
binary's own usage string**, so `quiche-server --enable-pmtud` exits with
`Unknown flag: '--enable-pmtud'` and the field is always `false`. That is what
the reference does, verified by running it.

It is written down so that the argument sitting unused in `ServerArgs` reads as
the upstream state of affairs rather than as a feature you forgot, and **it is
deliberately not a requirement of this step**. No group passes the flag, no
group checks that it is refused, and a candidate whose `quiche-server` *does*
declare `--enable-pmtud` in its usage string is not wrong and loses nothing
anywhere in the package. Asserting the refusal would be asserting
bug-compatibility with an upstream oversight: it would fail a candidate for
being better than the release it is tracking, which is the one thing a task
built out of successive releases must never do. What this step does require is
the plumbing -- `ServerArgs::enable_pmtud` reaching `Config::discover_pmtu` --
and `cap-pmtud` checks the feature itself over a surface that can see it.
`quiche-client` has neither the argument nor the flag.

**The recovery module is taken apart and rebuilt.** Everything that was a flat
file under `quiche/src/recovery/` moves into a `congestion` submodule, and the
round-trip estimator becomes a component of its own:

| step 10 | step 11 |
| --- | --- |
| `recovery/reno.rs`, `cubic.rs`, `bbr/`, `bbr2/` | `recovery/congestion/` (same names, one level down) |
| `recovery/pacer.rs`, `prr.rs`, `hystart.rs`, `delivery_rate.rs` | `recovery/congestion/` |
| (round-trip state inside `recovery/mod.rs`) | `recovery/rtt.rs` |

`quiche/src/crypto.rs` is split the same way, into `crypto/mod.rs` with
`crypto/boringssl.rs` and `crypto/openssl_quictls.rs` beside it. None of this
changes the public API by a single name, and all of it changes where a reader
looks -- so it is checked as layout, never as behaviour. The one behavioural
trace it leaves is that `Stats` gains an `acked_bytes` counter, which the
rebuilt acknowledgement path now has a number to report.

**The anti-amplification factor becomes configurable.** The 3x bound itself is
not new -- it has been in the library since well before this milestone, as a
`MAX_AMPLIFICATION_FACTOR` constant -- but `Config::set_max_amplification_factor`
is, and the default stays 3. A server given a factor of 1 may answer a
1200-byte client Initial with exactly 1200 bytes and no more; a server given 0
may not answer at all.

**Wire error codes become public.** `quiche::WireErrorCode` and
`quiche::h3::WireErrorCode` name the numbers RFC 9000 Section 20.1 and RFC 9114
Section 8.1 assign, and the library's own internal error-to-wire mapping is
written in terms of them. `Error` gains `CryptoBufferExceeded` (0xd) to go with
the new `WireErrorCode` entry of the same name.

**Packet-number encoding becomes spec-exact.** RFC 9000 Section 17.1 says the
number of bytes a sender uses for a truncated packet number must cover the range
of packets that could still be in flight -- it is a function of the packet number
*and* the largest acknowledged packet, not of the packet number alone. The
helper that chooses the length is given the second argument, and the helper that
writes the bytes is given the chosen length rather than recomputing it.

Also new, and required to exist rather than checked: a public `h3::grease_value`
and a public `h3::frame` module. No group at this step calls either; they are
listed so that their absence is a contract violation rather than a surprise.

`Connection::qlog_streamer` is new as well and is a weaker requirement still. It
sits behind the crate's optional `qlog` feature, and the verifier never builds
the candidate with that feature on, so **nothing in this package compiles the
method at any step** -- it is required by the release this milestone tracks and
observed by nothing the harness can reach. Turning the feature on from the
verifier's manifest would change the build frame for all fifteen steps to buy
one symbol, which is not a trade this package makes.

`h3::Config::set_additional_settings` is new too and is deliberately **not** in
that list: it is checked, by `cap-h3-additional-settings`, whose contract appears
in full below.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

No public function is removed at this milestone. The breaks are in shapes and in
numbers.

**`Stats` gains a field.** `acked_bytes: u64` joins the struct. Anything that
destructures `Stats` exhaustively, or builds one with a struct literal, stops
compiling. The verifier's own step-10 targets did exactly that and had to be
updated.

**`Error` gains a variant.** `CryptoBufferExceeded` is **appended**, after
`KeyUpdate`, so every earlier variant keeps its position; but the enum is not
`#[non_exhaustive]`, so an exhaustive `match` over `quiche::Error` stops
compiling all the same. `h3::Error` is untouched.

**The number of bytes on the wire for a packet number changes.** At step 10 the
length came from the magnitude of the packet number: below 255 it was one byte,
and from 255 upwards two. At step 11 it comes from how many packets could still
be unacknowledged. A connection whose peer is acknowledging promptly therefore
keeps a one-byte packet number indefinitely, where step 10 switched to two bytes
at packet 255 and never switched back. Any expectation pinned to the old rule is
now wrong, in both directions: a sender that always emits one byte is as wrong as
one that emits two.

**`crypto.rs` and the flat `recovery/*.rs` files no longer exist.** Their
contents are one directory level down. Nothing outside the crate referred to
them by path, but a patch against the old layout will not apply.

## Interface elements the checker depends on

### `Config::discover_pmtu and the probing rules`

```rust
impl Config {
    /// Configures whether to do path MTU discovery.
    ///
    /// The default value is `false`.
    pub fn discover_pmtu(&mut self, discover: bool);
}
```

The module that implements the search is private (`mod pmtud;` beside `mod
recovery;`), so nothing about `Pmtud` itself is part of the contract. What is
part of the contract is what a connection puts on the wire.

*Off* (the default) a connection's datagrams are bounded only by
`max_send_udp_payload_size` and by what it has to send, exactly as at step 10.
A bulk transfer therefore reaches full-size datagrams immediately, with no
discovery of any kind. **A group that checks "large datagrams eventually
appear" is not checking this feature.**

*On*, a connection starts bounded by the 1200-byte floor every QUIC endpoint
must support, and rises only on evidence. The probe is gated on three
conditions together:

* the handshake is confirmed, **and** HANDSHAKE_DONE has been sent -- so at this
  milestone only a server probes, because only a server sends HANDSHAKE_DONE;
* the congestion window can afford a datagram of the probe size -- this gate is
  in the implementation but is not separately checked, since every leg of
  `cap-pmtud` has a window large enough to pass it;
* there is nothing else queued -- the probe goes out on its own, padded to the
  probe size, when the connection would otherwise be idle.

A successful probe raises the estimate to the probe size. A lost probe halves
the interval between the last confirmed size and the failed size and the search
continues. `Config::set_max_send_udp_payload_size` is the ceiling the search
climbs towards; note it floors its argument at 1200, so it can raise the
maximum but never lower it below the QUIC minimum.

The observable consequences, and the only ones checked:

1. with discovery on and both endpoints configured for a 1452-byte payload, the
   server emits at least one datagram of **exactly** the configured size after
   the handshake and with no application data outstanding;
2. with discovery off and everything else identical, **no** server datagram
   exceeds 1200 while the connection is otherwise idle;
3. the client is **clamped, not probing**, and the two configurations differ
   because of it: with discovery *on* the client pushing a transfer is held at
   the 1200-byte floor, because it can never raise the estimate it is capped
   at; with discovery *off* nothing caps it and the same transfer runs at the
   configured maximum. Both directions are asserted -- the on leg as an upper
   bound, the off leg as an existence claim -- so the bound cannot be met by a
   connection that never had a full-size datagram to send;
4. the connection survives all of it and a subsequent transfer arrives intact.

A successful probe also emits the qlog `MtuUpdated` event step 10 added to the
schema. That event is at `QlogLevel::Extra`, and `Connection::set_qlog` selects
`QlogLevel::Base`, so it is reachable only through `set_qlog_with_level` -- and
the `qlog` feature is optional and off by default. Nothing here is checked
through qlog.

### `Config::set_max_amplification_factor`

```rust
impl Config {
    /// Sets the anti-amplification limit factor.
    ///
    /// The default value is `3`.
    pub fn set_max_amplification_factor(&mut self, v: usize);
}
```

The *limit* is not new -- a server has been refusing to send more than three
times what it has received from an unvalidated address since long before this
milestone, and the default stays 3. The setter is new, and it is unclamped in
both directions: 0 and 1 are accepted and both mean something.

The rule is RFC 9000 Section 8: until the peer's address is validated, the
server's send allowance is the number of bytes received times the factor,
decremented by every byte it sends. With a client Initial datagram padded to the
mandatory 1200 bytes:

| factor | what the server may send in reply |
| --- | --- |
| 0 | nothing at all |
| 1 | exactly 1200 bytes |
| 3 (default) | 3600 bytes, which is more than its first flight needs |

At factor 1 the server's first flight is truncated mid-flight rather than
suppressed: the one datagram it does send is a real Initial packet carrying the
beginning of the server's CRYPTO stream. The allowance is a budget, not a gate.

### `quiche::WireErrorCode`

The transport error codes RFC 9000 Section 20.1 assigns become a public enum:

```rust
#[repr(u64)]
pub enum WireErrorCode {
    NoError                 = 0x0,
    InternalError           = 0x1,
    ConnectionRefused       = 0x2,
    FlowControlError        = 0x3,
    StreamLimitError        = 0x4,
    StreamStateError        = 0x5,
    FinalSizeError          = 0x6,
    FrameEncodingError      = 0x7,
    TransportParameterError = 0x8,
    ConnectionIdLimitError  = 0x9,
    ProtocolViolation       = 0xa,
    InvalidToken            = 0xb,
    ApplicationError        = 0xc,
    CryptoBufferExceeded    = 0xd,
    KeyUpdateError          = 0xe,
    AeadLimitReached        = 0xf,
    NoViablePath            = 0x10,
}
```

The numbers are the contract; the names are RFC 9000's own, spelled in
Rust-style camel case. `Error::CryptoBufferExceeded` is added to `quiche::Error`
at the same time and for the same reason -- so the library has a variant for
0xd. The mapping from `Error` to these numbers is an internal detail and is not
callable from outside the crate.

### `quiche::h3::WireErrorCode`

The HTTP/3 error codes RFC 9114 Section 8.1 assigns become a public enum in the
`h3` module:

```rust
#[repr(u64)]
pub enum WireErrorCode {
    NoError                = 0x100,
    GeneralProtocolError   = 0x101,
    InternalError          = 0x102,
    StreamCreationError    = 0x103,
    ClosedCriticalStream   = 0x104,
    FrameUnexpected        = 0x105,
    FrameError             = 0x106,
    ExcessiveLoad          = 0x107,
    IdError                = 0x108,
    SettingsError          = 0x109,
    MissingSettings        = 0x10a,
    RequestRejected        = 0x10b,
    RequestCancelled       = 0x10c,
    RequestIncomplete      = 0x10d,
    MessageError           = 0x10e,
    ConnectError           = 0x10f,
    VersionFallback        = 0x110,
}
```

`h3::Error`'s own variant list does not change at this milestone. What changes is
that the numbers those variants are sent as are now nameable, and the library's
own protocol violations must send them: an endpoint that receives a DATA frame
on its peer's control stream closes the connection with an **application** error
of `WireErrorCode::FrameUnexpected as u64`, and its `h3::Connection::poll`
returns `Err(h3::Error::FrameUnexpected)`.

### `Packet-number length is a function of the largest acknowledged packet`

RFC 9000 Section 17.1 requires a sender to encode enough of the packet number
that the receiver can recover it unambiguously: the truncated number must be
able to represent more than twice the range of packets still in flight. The
library's helper is given the second input it needs.

Before, the length came from the packet number alone -- one byte below 255, two
below 65535, and so on. Now it comes from the distance between the packet number
and the largest packet the peer has acknowledged in that packet-number space,
rounded up to a whole number of bytes, as RFC 9000 Section A.2 describes. The
helper that writes the bytes takes the chosen length as an argument rather than
recomputing it.

Neither of these helpers is reachable from outside the crate, so the contract is
on the wire:

* a connection whose peer acknowledges promptly keeps a **one-byte** packet
  number indefinitely, through packet 255, packet 300 and beyond -- where the old
  rule switched to two bytes at 255 and never switched back;
* a connection that has sent several hundred packets with **nothing**
  acknowledged widens to two bytes, because the unacknowledged range no longer
  fits in one.

A sender that always writes one byte is as wrong as one that always writes two.
Packet-number *decoding* is unchanged and is not part of this.

### `Stats::acked_bytes`

```rust
pub struct Stats {
    // ... everything step 10 had ...
    pub acked_bytes: u64,
}
```

A cumulative count, across all packet-number spaces and all paths, of the bytes
the peer has acknowledged. It starts at zero, stays at zero on a connection that
has sent nothing, and after a completed transfer of *n* bytes it is at least *n*
-- packet overhead and handshake traffic are included, so it is a lower bound
that the payload establishes, never an equality. It never decreases.

The field is added at the end of the struct, and `Stats` is not
`#[non_exhaustive]`, so exhaustive destructuring of `Stats` stops compiling
until the new field is named.

### `h3::Config::set_additional_settings`

```rust
impl h3::Config {
    pub fn set_additional_settings(
        &mut self, additional_settings: Vec<(u64, u64)>,
    ) -> Result<()>;
}
```

Identifier/value pairs to append to the SETTINGS frame this endpoint sends. They
go out in the order given, after the settings the library sends itself, and the
peer reads them through `peer_settings_raw()` -- which is not new, and which
reports every identifier the peer sent, known or not, in received order.

The method validates before it stores, and returns `Err(h3::Error::SettingsError)`
for either of two reasons:

* an identifier the library sends itself: `0x1` (QPACK_MAX_TABLE_CAPACITY),
  `0x6` (MAX_FIELD_SECTION_SIZE), `0x7` (QPACK_BLOCKED_STREAMS), `0x8`
  (ENABLE_CONNECT_PROTOCOL), `0x33` (H3_DATAGRAM) or `0x276` (H3_DATAGRAM_00);
* the same identifier twice in one call.

Anything else is accepted, including values the receiver will later reject. A
valid call returns `Ok(())` and the settings reach the peer.

### `The recovery and crypto modules, rebuilt`

No public name moves, so this is a statement about where the source lives and
nothing else.

`quiche/src/crypto.rs` becomes a directory:

```
quiche/src/crypto/mod.rs
quiche/src/crypto/boringssl.rs
quiche/src/crypto/openssl_quictls.rs
```

`quiche/src/recovery/` keeps `mod.rs`, gains `rtt.rs`, and moves every
congestion-control file one level down into a `congestion/` submodule:

```
quiche/src/recovery/mod.rs
quiche/src/recovery/rtt.rs
quiche/src/recovery/congestion/mod.rs
quiche/src/recovery/congestion/reno.rs
quiche/src/recovery/congestion/cubic.rs
quiche/src/recovery/congestion/pacer.rs
quiche/src/recovery/congestion/prr.rs
quiche/src/recovery/congestion/hystart.rs
quiche/src/recovery/congestion/delivery_rate.rs
quiche/src/recovery/congestion/bbr/
quiche/src/recovery/congestion/bbr2/
```

The flat `quiche/src/recovery/reno.rs`, `cubic.rs`, `pacer.rs`, `prr.rs`,
`hystart.rs`, `delivery_rate.rs`, `bbr/`, `bbr2/` and `quiche/src/crypto.rs` no
longer exist. Every one of these modules is private; `RttStats` and the
congestion-control trait are internal types, not public API. The four
algorithm *names* and their discriminants are unchanged and still required.

### `Smaller additions, required to exist`

Three more public items appear and are not otherwise checked:

```rust
impl Connection {
    pub fn qlog_streamer(&mut self) -> Option<&mut qlog::streamer::QlogStreamer>;
}

pub mod h3 {
    pub mod frame;              // was private
    pub fn grease_value() -> u64;   // was private
}
```

`qlog_streamer` is behind the optional `qlog` feature, as everything qlog is.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-pmtud` | requirement | T3 | Four legs over in-memory `connect`/`accept` pairs, public API only. Both endpoints are configured with `set_max_send_udp_payload_size(1452)` and `set_max_recv_udp_payload_size(1452)` in every leg, so the only thing that differs between the two configurations is `discover_pmtu`. Every leg is a differential between those two connections, which is what stops a datagram budget, a pacing quantum or a buffer size from satisfying any of them. **On -- the server probes.** With `discover_pmtu(true)` on both sides the pair handshakes and settles, and the verifier records the length of every datagram each side produced. At least one **server** datagram is exactly 1452 bytes, and every server datagram is either at or under 1200 or exactly 1452, with nothing in between: ordinary output is held to the path estimate and only a padded probe leaves it. Measured against the reference: `[1200, 358, 439]` followed by nine 1452-byte probes. Neither the count nor the position is asserted. The count is a function of the congestion window at the moment the handshake completes, and the position is not "after the handshake" at all -- the reference emits its first probe *inside* the handshake flight, on the first `send` after HANDSHAKE_DONE. The window the group reads is therefore the whole connection up to the point application data is queued, not the part that follows `handshake()`. **Off -- nothing above the floor.** With `discover_pmtu(false)` on both sides, the same configuration and the same drive loop, **no** server datagram exceeds 1200 at all: `[1200, 358, 439]` and nothing else. This is the leg that makes the first one discriminating. **The client is clamped, not probing.** `Path::should_send_pmtu_probe` is gated on `handshake_confirmed && handshake_done_sent`, and only a server ever sets `handshake_done_sent`, so a client cannot leave the floor however it is configured. The consequence is visible, and it is the reverse of what the plan row guessed: with discovery *on*, a client pushing a 64 KiB transfer is limited to the 1200-byte estimate it can never raise, because `send` caps its output at `min(pmtud.get_current(), buffer)`; with discovery *off* the same client on the same transfer runs at 1452, because nothing caps it. Measured client histograms: `{43: 1, 483: 1, 904: 1, 936: 1, 1050: 1, 1200: 54}` on, and `{43: 1, 483: 1, 932: 1, 962: 1, 1452: 45}` off. Both directions are asserted -- the on leg as an upper bound, the off leg as an existence claim -- so the bound cannot be met by a connection that merely never had 1452 bytes to send. **Survival.** Both configurations carry the 64 KiB body to completion byte-for-byte with the stream finished, and neither end of the on-configuration pair is closed afterwards. A search that raises the estimate and then black-holes the connection is not discovery. The binary-search-on-loss leg is deliberately **absent**. Driving it requires discarding the probe and then advancing the connection until the loss is detected, and on an in-memory pair with no real clock the reference needed more timer ticks than a bounded test can justify before it re-probed at a smaller size -- measured: six `on_timeout` ticks with nine probes dropped produced no smaller probe. An assertion that cannot be driven deterministically against the reference does not belong in a group. `PathStats::pmtu` is **not** read. `Path::stats` reports `recovery.max_datagram_size()` there, which is the configured `max_send_udp_payload_size` from the moment the connection is created -- measured 1452 in *both* configurations before either has sent a packet -- so it cannot tell them apart. A group that asserted on it would be asserting on the configuration it had just set. |
| `cap-pn-encode-exact` | requirement | T2/T3 | The packet-number length field, read out of the candidate's own 1-RTT headers. The client is configured with `Config::log_keys()`, the verifier pulls `CLIENT_TRAFFIC_SECRET_0` out of the resulting NSS keylog, derives the generation-0 `PacketKeys`, and for every short-header datagram the client produces it removes header protection and reads the `len` the unmasked first byte declares. That is the field RFC 9000 Section 17.1 governs and the field this milestone changed, observed as a field rather than inferred from a total. Both sides set `set_initial_congestion_window_packets(600)` so the congestion window is never the thing under test. Before either phase runs, the verifier proves it has the right keys by opening one of the client's packets. If that fails the group says so in those words -- a key-schedule disagreement is not a packet-number fault and must not be reported as one. **Phase A, peer acknowledging.** 300 rounds, each one a small write from the client and the server's reply fed straight back, so acknowledgements keep arriving and the unacknowledged range stays tiny. **Every** client 1-RTT packet in this phase has a packet-number length of **1**, and the phase runs long enough that the client's packet number passes 255 -- which the verifier checks, by reconstructing the number, so "the packet number never got high enough" cannot be mistaken for "the length stayed narrow". Under step 10's magnitude rule the length steps to 2 at packet 255 and never returns, so the group fails against step 10, as it must. **Phase B, peer silent.** 500 further rounds on the *same* connection with the server's output discarded, so `largest_acked` freezes while the packet number keeps climbing. At least one client packet now has a packet-number length of **2**, and at least one still has **1** -- the width follows the unacknowledged range up, which is the whole content of the rule, and it does not simply jump to the maximum. The verifier also asserts that the phase was not blocked by flow control or congestion control, so "no packet was produced" cannot be mistaken for "the packet was narrow". Phase A is what a candidate that keeps the old magnitude rule fails. Phase B is what a candidate that hardcodes a one-byte packet number fails. Both are required and neither is sufficient. Datagram *lengths* were how this was first measured and they agree -- tree 11 gives `{53: 289, 58: 2, 59: 9}` in phase A where tree 10 gives `{53: 244, 54: 45, ...}` -- but the shipped assertion reads the header field, because a datagram length is a sum of many things and only one of them is under test here. Nothing is asserted about decoding. Decoding did not change, and a group that tested it would be checking step 09's work as this step's. |
| `cap-anti-amplification-factor` | requirement | T2 | The verifier owns the socket and is the peer. It builds a client Initial itself, sends it to the candidate's server, and counts the bytes that come back. Getting a *real* ClientHello into that Initial matters, and the group takes the long way round to do it. A stub CRYPTO payload draws only a small ACK from the server, which leaves even a factor of 1 non-binding and the group vacuous. So the verifier first stands up a throwaway `quiche::connect` client, takes its first Initial, decrypts it with `support::decrypt_initial(.., Perspective::Client)`, lifts the CRYPTO bytes out, and rebuilds them into its **own** Initial with `support::ClientInitial` -- same version, same connection IDs, padded to the mandatory 1200 -- which the verifier then encrypts with its own keys. The candidate's library is used to produce a ClientHello and for nothing else; the packet on the wire is the verifier's. One more piece of stage-setting, and it is what makes the group exact. The budget only discriminates when the server has more to send than it is allowed to, and the harness's single small certificate makes a first flight of only 1558 bytes -- under 3 x 1200, so the default and every factor from 2 upwards spend the same amount and the group could not tell 3 from 300. The verifier therefore writes the harness certificate eight times into one chain file and loads that as the server's chain, which takes the unconstrained flight to 7702 bytes. Nothing validates the chain: this group never runs a `quiche` client, and the throwaway one it borrows a ClientHello from sets `verify_peer(false)`. Seven legs against a server the verifier configures per leg, every one of them answering the same 1200-byte Initial: | leg | `set_max_amplification_factor` | required reply | | --- | --- | --- | | 1 | not called (default) | **exactly** 3600 bytes | | 2 | `5` | **exactly** 6000 bytes | | 3 | `4` | **exactly** 4800 bytes | | 4 | `3` | **exactly** 3600 bytes, and the same total as leg 1 | | 5 | `2` | **exactly** 2400 bytes | | 6 | `1` | **exactly** 1200 bytes, in exactly one datagram | | 7 | `0` | **nothing at all** | The right-hand column is not a table of constants baked into the test. It is computed as `min(free, factor * 1200)`, where `free` is measured in a calibration leg with the factor set to 1000 and the test refuses to run unless `free` exceeds `5 * 1200` -- so the legs bracket the default from both sides and a candidate whose certificate flight differs in length from the reference's is still checked against the formula rather than against the reference's numbers. This is the shape of the reference's own unit test, `server_sent == client_sent * FACTOR`, with `client_sent` fixed at 1200 by construction. Measured against the reference over the eight-certificate chain: 7702 bytes in seven datagrams unbounded, then 6000/4800/3600/2400/1200/0 at factors 5/4/3/2/1/0, and 3600 with the setter never called. Legs 1 and 4 together are what pin the default at exactly 3 rather than at "some number of at least 2": the default spends 3600 and stops, while 4800 and 6000 were both available and were both reached when asked for. A further check makes leg 6 a statement about content rather than about a number. The single datagram the server sends under factor 1 is decrypted with the verifier's own Initial keys and parsed: it is a server Initial carrying a CRYPTO frame whose first byte is `0x02`, a ServerHello. So the allowance truncates a real flight mid-stream; it does not suppress the flight, and it does not substitute padding or a CONNECTION_CLOSE. Note what this group does **not** claim. The 3x limit itself is not new at this milestone -- step 10's tree has the same constant, the same enforcement and the same unit assertion. Only the setter is new, so the legs that could pass without it (1 and 4's equality with it) are controls, and the check turns on the five legs that ask for a factor the default would not produce. |
| `cap-recovery-and-crypto-layout` | requirement | T4 | An emitted artefact -- the crate's own directory layout, reached through `testkit::quiche_crate_dir()` -- checked against a fixed expectation table. This is the one toolbox exception to no-source-reading, and it is checked as layout and never as behaviour: not a line of any of these files is read, only whether the path exists. **Must exist:** ``` quiche/src/crypto/mod.rs quiche/src/crypto/boringssl.rs quiche/src/recovery/rtt.rs quiche/src/recovery/congestion/mod.rs quiche/src/recovery/congestion/reno.rs quiche/src/recovery/congestion/cubic.rs quiche/src/recovery/congestion/pacer.rs quiche/src/recovery/congestion/bbr/ quiche/src/recovery/congestion/bbr2/ quiche/src/recovery/congestion/bbr/mod.rs quiche/src/recovery/congestion/bbr2/mod.rs ``` **Must not exist:** ``` quiche/src/crypto.rs quiche/src/recovery/reno.rs quiche/src/recovery/cubic.rs quiche/src/recovery/pacer.rs quiche/src/recovery/bbr/ quiche/src/recovery/bbr2/ ``` `quiche/src/recovery/mod.rs` is required to exist and is on neither list's interesting half -- it exists before and after, and saying so keeps a candidate from reading the "must not exist" list as "delete the directory". The two `bbr/mod.rs` and `bbr2/mod.rs` entries are the ones a reader most often gets wrong: the directories are named in the reorganisation table, but the checked paths are the modules inside them, so a `bbr/` holding only `bbr.rs` is not the layout this group reads as done. `quiche/src/crypto/openssl_quictls.rs` is **not** required. It is a second backend the reference happens to carry, and a candidate that builds only against BoringSSL -- which is what every other group in all fifteen steps requires -- has no reason to have written it. Both halves are needed. The first alone is satisfied by a candidate that copies files into the new places and leaves the old ones behind, which is not a reorganisation; the second alone is satisfied by deleting them. Nothing about `RttStats` or the congestion-control trait is asserted, in this group or anywhere in this step. Both are private, and a public assertion about a private type would be an assertion about the reference's source rather than about a contract. |
| `cap-wire-error-codes` | requirement | T3 | Two legs: a fixed expectation table over the two new public enums, and one live protocol violation that binds the table to the wire. **The table.** Every variant of `quiche::WireErrorCode` and of `quiche::h3::WireErrorCode` is cast with `as u64` and compared against RFC 9000 Section 20.1 and RFC 9114 Section 8.1 -- seventeen values each, `0x0` through `0x10` and `0x100` through `0x110`, asserted in two batches so a wrong value reports which enum it was in. The names are the contract too: the test names every variant explicitly, so a candidate that ships sixteen of seventeen does not compile, which is the correct outcome for a missing public item. `quiche::Error::CryptoBufferExceeded` is named in the same leg for the same reason, and is asserted to be a variant distinct from the long-standing `quiche::Error::CryptoFail`: a connection that buffered more CRYPTO than it can hold has not failed to decrypt anything, and the two must not compare equal. **The wire.** The verifier drives an in-memory pair where the *client* runs `h3::Connection` and the *server* side is raw QUIC that the verifier writes bytes onto directly. On server-initiated unidirectional stream 3 the verifier writes, by hand: the control-stream type varint `0x00`, an empty SETTINGS frame (`0x04 0x00`), and then a DATA frame (`0x00 0x01 0x41`). RFC 9114 Section 7.2.1 does not permit DATA on a control stream. The client's `h3::Connection::poll` must return `Err(h3::Error::FrameUnexpected)`, and its transport connection's `local_error()` must carry `error_code == h3::WireErrorCode::FrameUnexpected as u64` -- that is, `0x105` -- with `is_app == true`. The verifier writes the control stream itself rather than reaching for the candidate's own control stream by number. Which stream id a candidate opens its control stream on is not part of any contract in this package, and a group that assumed 3 would be checking an implementation detail. The `is_app` flag matters as much as the number does: `0x105` sent as a *transport* error would be `0x105` in the wrong namespace, and RFC 9114's codes travel in an application close. The private `Error::to_wire` mapping is not called, because it cannot be. Leg two is what proves the enum is the mapping's source of truth rather than a decorative table beside it. |
| `cap-acked-bytes-stat` | requirement | T3 | `Stats::acked_bytes` over an in-memory pair, in four steps: 1. immediately after `connect`, before a single packet is exchanged, the client's `acked_bytes` is **0**; 2. after the handshake completes it is greater than 0 -- the handshake itself is acknowledged traffic; 3. a 256 KiB body is sent and received byte-for-byte; afterwards the sender's `acked_bytes` is at least the length of the body. A lower bound, never an equality: the counter includes packet overhead and every handshake byte, and pinning a total would pin this build's packetisation; 4. reading it again without exchanging anything further returns the same value, and a further transfer only raises it. The counter is monotone. The receiving endpoint's own `acked_bytes` is then required to stay **below a quarter of the body** -- it acknowledged a handshake and some stream data, not the body -- so a counter aliased onto the peer's traffic fails. The bound is a factor of four below the value a wrong counter would report, not a tight one. The receiving side is checked too: it has acknowledged very little application data of its own, so its `acked_bytes` is far below the body length while the sender's is above it. That asymmetry is what distinguishes the new counter from `sent_bytes` or `recv_bytes`, either of which a candidate might alias it onto. This is the one place the rebuilt recovery module is checked behaviourally, and it is checked through the number it now has to report rather than through where its source lives. |
| `cap-h3-additional-settings` | requirement | T3 | `h3::Config::set_additional_settings` on one endpoint, observed through `peer_settings_raw()` on the other. The observation channel is not new -- `peer_settings_raw` exists at step 10 as well -- and that is deliberate: it means the group checks the new sending capability and not a new accessor. **Validation, before anything reaches a wire.** `set_additional_settings` returns `Err(h3::Error::SettingsError)` for each of the six identifiers the library sends itself -- `0x1`, `0x6`, `0x7`, `0x8`, `0x33`, `0x276` -- each tried in its own call, and for a single call containing the same unknown identifier twice. It returns `Ok(())` for a list of two distinct unknown identifiers. **Delivery.** A client configured with `[(0x4f4f, 0x1234), (0x9a9a, 0x5678)]` completes an HTTP/3 handshake with a server that has no additional settings of its own. The server's `peer_settings_raw()` is `Some`, contains both pairs with both values intact, and contains them in the order they were configured. The list also contains the identifiers the library sends on its own account, which is why membership and relative order are asserted rather than equality against a fixed list -- how many settings a conforming implementation sends unprompted is not this step's business. **The reverse direction** is checked once, with the server carrying `[(0x7f7f, 0x99)]` and the client reading it, so a candidate that wires the new setting into only one of the two roles fails. Nothing is asserted about the HTTP/2-reserved identifiers `0x0` and `0x2` through `0x5`. RFC 9114 requires a *receiver* to reject them, and the reference accepts them in `set_additional_settings` and lets the peer do the rejecting; a group that demanded rejection at the setter would be demanding something the reference does not do. |
| `reg-10-bbr2` | gate | T3 | The four-name accept list with its rejections -- `"BBR2"`, `"bbr2 "`, `"bbr3"` and `""` each `Err(quiche::Error::CongestionControl)` -- the `(0, 1, 2, 3)` discriminant tuple, a positive congestion window on the handshaken connection, and a 256 KiB transfer under each of the two ways of selecting the controller -- `set_cc_algorithm_name("bbr2")` and `set_cc_algorithm(BBR2)` -- completing byte-for-byte. Carried because this step takes every one of those algorithms apart and reassembles it a directory level down behind a new trait. Nothing in the package endangers BBRv2 more than this milestone does, and nothing yet carried step 10 -- this is its first re-run. Step 10's group also required the congestion window to be no smaller after the transfer than before it. That comparison is not carried into the gate. BBRv2 sizes its window from a bandwidth estimate and a minimum RTT taken off the real clock, and an in-memory pair has no propagation delay for either to be about, so both are readings of how promptly this host scheduled the drive loop; under enough load the pair sits long enough between rounds to enter ProbeRTT, whose window is four packets -- below where it started. `cap-bbr2` at step 10 still makes the comparison, where a miss means that group fails. |
| `reg-01-stream-transfer` | gate | T1 | The candidate's own `quiche-server` serves a 512 KiB body, the candidate's own `quiche-client` fetches it with `--http-version HTTP/0.9`, exits zero, and writes the body back byte-for-byte. T1, not T3: step 01 checked this over the process boundary and step 08 re-ran it there, and pinning HTTP/0.9 rather than letting the app negotiate keeps the observation the transport's. Carried because it is the simplest thing in the package that runs every byte through the rebuilt recovery path -- loss detection, the round-trip estimator now in its own file, the pacer now a directory down, and the acknowledgement bookkeeping that had to be re-plumbed to produce `acked_bytes`. If the rebuild broke, a half-megabyte transfer is where it shows. Re-seeded from step 08, the most recent step to carry it, and unchanged from it apart from the body seed. |
| `gate-endtoend` | gate | T1 | The mandatory process-boundary gate. The candidate's own `quiche-server` is started on a loopback port with `--listen`, `--cert`, `--key`, `--root` and `--no-retry`; the candidate's own `quiche-client` fetches a 2 KiB body from it over real UDP with `--no-verify`; the client exits zero and the bytes match what the server was given. The server is **not** given `--enable-pmtud`. The reference reads an `enable_pmtud` argument in `ServerArgs` and passes it to `Config::discover_pmtu`, but never declares the flag in `SERVER_USAGE`, so docopt refuses it -- `quiche-server --enable-pmtud` prints `Unknown flag: '--enable-pmtud'` and exits non-zero. Requiring the flag here would require a candidate to be *better* than the reference, which is not what a gate is for, and the group would fail against the Oracle. Nor is the refusal asserted anywhere else: as the milestone summary says, the flag's absence is recorded as the reference's behaviour and not required of the candidate, so a server that declares it is not required either way. `cap-pmtud` checks the feature over a surface that can see it. Nothing else clever on purpose. If this group fails, every other T1 observation at this step is noise. |

Surfaces: **T1** your client and server as processes over UDP
loopback; **T2** a QUIC endpoint the checker implements itself, reading
and writing real packets; **T3** your library linked into the checker's
own crate, public API only; **T4** a file or layout your code produces;
**T5** an input with exactly one correct answer.

## How this step is checked

The **gate** covers proof of life plus the milestones you already have: your
workspace builds, your binaries complete one end-to-end exchange, and the
earlier capabilities this step disturbs still work. Gate groups add no new
requirement of their own, but a failure in one means the step is not
satisfied, however much of the new work is right.

The **requirement** groups cover this step's own new capability, and every one
of them is in scope. Your step's interface document lists every group with its
tier.

Every case within a group must pass, and there is no separate requirement for
style or for extra work.

Three families of group, named in the report so you can tell them apart:

| prefix | tier | what it checks |
|---|---|---|
| `cap-*` | requirement | the capability this step introduces |
| `reg-NN-*` | gate | a capability step NN introduced, still working |
| `gate-endtoend` | gate | one end-to-end exchange through your own binaries |
| `candidate-workspace-builds` | gate | your workspace builds at all, before any other group runs |

`reg-NN-*` groups are not decoration: every step in this task rewrites
something an earlier step built, and the `reg-` groups are chosen from the
surfaces this step disturbs. (A step with no earlier milestone carries none of
them: this section describes the family, and the group table above is what
says whether a given step has any.) A rewrite that quietly drops an older
behaviour does not fail just one group; it means the step is not satisfied.

The corollary is worth stating plainly: a do-nothing continuation satisfies
none of the new work. A workspace carried forward untouched passes every gate
group — it has not broken anything — and passes no requirement group, because
it implements none of this step's capability.

### What the checker is allowed to look at

Behaviour, and only behaviour:

* your library, linked into a test harness that lives outside `/app` and calls
  your public API;
* your client and server binaries -- whichever names this step's own interface
  section gives them -- run as real processes talking to each other over UDP
  loopback;
* your binaries talking to a QUIC endpoint the checker implements itself, which
  derives its own keys and reads the bytes on the wire;
* files your code emits — qlog, netlog, a generated C header, the workspace
  layout;
* exact answers to inputs designed to have exactly one right answer.

It never **checks** anything by reading your source: no group greps your tree
for an identifier, counts your modules, or checks a log line. One qualification
belongs beside that: anything the checker's own crate *imports or names* has to
exist under that name, because it is a Rust program that compiles against your
crate. Those names are this step's interface section, and where a group builds
a probe of its own, the names that probe imports are written down there too.
Inside your own modules, name things whatever you like. Two plumbing
steps outside the checked path do read source text -- the image picks which
pinned BoringSSL to link by looking at `src/tls.rs`, and where this
milestone's release changed an API's shape the harness picks which dialect of
its own test shim to compile by looking for the new spelling in `src/`. Both
read live code only -- `//` comments are stripped before either check, and the
dialect probe, where this step has one, drops `/* ... */` blocks as well -- and
both key on spellings this step's own documents already tell you to change, so a
tree you have converted is never misread.
Name things whatever you like, put them wherever the manifest says, and write
in whatever style you prefer.

### Reading the report

`reward_details.json` names every group, its tier, whether it
passed, and for a failing group the first line of why. When the gate fails it
says which gate group failed and what it saw — a step reported as unsatisfied
without naming the group responsible would be indistinguishable from a
candidate that did nothing. When a group fails for a reason that is not about
your code — a build that did not finish inside its budget, a harness that
failed to link — it is reported as such rather than silently treated as a
failure.

## The build environment

Written once and generated into every step's copy of this document, so that the
fifteen copies cannot drift apart. Anything in this section that is *not* true
of your step is called out in the step's own section above, never by silently
contradicting it here.

### Toolchain

Rust **1.88** (`rustc 1.88`, `cargo 1.88`), Debian bookworm, x86-64. Also
installed: `clang`, `cmake`, `ninja`, `perl`, `go`, `pkg-config`, `git`,
`python3`, `jq`, `ripgrep`, `xxd`, `iproute2`, `libev`, `uuid-dev`.

### BoringSSL is prebuilt; your build script must link it, not build it

Your workspace does **not** contain BoringSSL and must not try to fetch or
compile it. A prebuilt static BoringSSL is installed in the image, and the
environment variable **`QUICHE_BSSL_PATH`** points at it:

```
$QUICHE_BSSL_PATH/
├── build/
│   ├── libcrypto.a
│   ├── libssl.a
│   ├── crypto/libcrypto.a     (hard link, for the older expected layout)
│   └── ssl/libssl.a           (hard link)
├── libcrypto.a                (copy, for the newest expected layout)
├── libssl.a
├── include/openssl/...
└── COMMIT                     the BoringSSL commit this was built from
```

All three layouts are populated, so a build script that looks in any of them
finds the archives. The archives are built with
`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`, so they link into a `cdylib`.

`QUICHE_BSSL_PATH` is set in the environment in the checker image, and in the
implementation container your step's `.cargo/config.toml` is what sets it —
keep that file, or a build script that reads the variable has nothing to read
and nothing to link. Read it in your build script and emit the
`cargo:rustc-link-search` / `cargo:rustc-link-lib` lines yourself — nothing
does that for you.

The prebuilt BoringSSL that `QUICHE_BSSL_PATH` points at is the one your step
is built against. The measurement runs offline, so it is also the only one available:
you never need to know its commit hash, only to link against that path.

### The measurement runs offline

At measurement time the container has **no network**. `CARGO_NET_OFFLINE=true` is
set and the registry cache holds the crates this milestone's build can reach:
every crate in the `Cargo.lock` that ships in this workdir, the versions this
milestone's own manifests resolve to, the previous release's graph, and the
checker's own harness crate's graph. Nothing else is available. Consequences:

* **Keep `Cargo.lock`.** Do not delete it and do not run `cargo update`. It is
  the contract between your manifest and the offline cache, and a version it
  pins is reachable offline even where the requirement alone would not reach
  it.
* **A dependency you add that is outside that set will not resolve**, here or
  at measurement time, and the build failure is reported as a build failure. If you
  need something, check `Cargo.lock` first.
* **Your resolution is pinned to this milestone's set, here as well as at
  measurement time.** One image serves all fifteen milestones and its registry
  cache is the union of every milestone's crates, so the set above is not what
  the cache holds -- it is what you are allowed to resolve. The step's
  `setup.sh` points cargo's `crates-io` at a single directory source built
  from this milestone's own locks, before your first command runs. A
  dependency outside it therefore fails here too, with the checker's own
  message:

      no matching package named `NAME` found
      location searched: directory source `/opt/vendor/NN`

  A local build that resolves is a build that works here as well. Do not delete or edit
  `$CARGO_HOME/config.toml`; that file is the pin.
* Cargo resolves the *whole* workspace graph, dev-dependencies included, before
  it compiles anything — so even a dev-dependency you invent breaks the build.
* `CARGO_TARGET_DIR` is set to a shared directory whose dependency graph is
  already compiled. Your build pays for your code only.

### What the checker builds and runs

```
cd /app && cargo build --offline          # libraries and binaries
cd /app && cargo build --offline --examples   # best effort; never fatal
cd /app/<app package> && cargo build --offline   # best effort; never fatal
```

Your own tests are **not** compiled by the checker, and a broken test of yours
costs you nothing. Correspondingly, passing your own tests is not required:
the checker links your crate from its own throw-away harness crate outside
`/app`, and runs your binaries as processes.

### Where your crate has to be

The checker locates the transport crate by looking for a `Cargo.toml` whose
package `name` is `quiche`, first at `/app/quiche` and then at `/app`, and uses
the first of the two that exists. Every group that merely needs *the transport
crate* -- to build it, to read its manifest, to look inside its `src/` or its
`include/` -- goes through that search, so either arrangement satisfies all of
them.

The exceptions are the groups for which the layout is the thing being checked,
and there are two kinds. The step that *introduces* the workspace conversion
checks the move itself, against the paths that conversion is defined to
produce. From that step onward, a group that checks **workspace membership**
names the member directories it expects -- `quiche` among them -- because which
crates are members, and where they sit, is that milestone's subject matter
rather than an incidental path. A group of either kind says so in its own
interface section. Outside those groups the layout is never checked.

### The binaries

Several groups run your client and server as real processes over UDP loopback.

**This step's own interface section names the exact binaries it expects, and
that section governs.** Build them with a plain `cargo build` (or let the
best-effort `--examples` pass pick them up). Where they live does not matter:
`examples/`, `tools/apps/` or their own package are all fine. The checker
searches these directories, in this order, for each name before it moves to the
next name:

```
$CARGO_TARGET_DIR/debug/
$CARGO_TARGET_DIR/debug/examples/
$CARGO_TARGET_DIR/release/
$CARGO_TARGET_DIR/release/examples/
```

and then, in case a candidate ignored `CARGO_TARGET_DIR` or a workspace member
carries a target directory of its own, the same four in the same order under
`<workspace root>/target/`, `<workspace root>/quiche/target/` and
`<workspace root>/apps/target/`.

What *does* matter is the name, and it is not a free choice. Each binary the
checker needs is resolved from a short list of accepted names -- this step's own
interface section gives them, most-preferred first -- and the search is
name-major: the first name on that list is looked for across every directory
before the second is tried anywhere. A binary that is correct but named
something outside the list is invisible to the group that needs it, and the
group fails with no diagnostic.

Two consequences worth stating plainly, because both have bitten:

* A single release can ship more than one client, and they are not
  interchangeable — an HTTP/3 group needs the HTTP/3-capable binary
  specifically. Naming a general-purpose client something an HTTP/3 group
  resolves would hand that group a client that cannot speak HTTP/3.
* Upstream renamed these binaries across the fifteen releases. Do not carry a
  name forward from an earlier step because it worked there; read this step's
  interface section.

## Where this step starts

`/app` contains everything step 10 left behind, unchanged. The milestone before this one is modelled on quiche 0.20.0; this one is modelled on 0.22.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

## Out of scope

Stated so you do not spend the budget on them.

* **Interoperability with other QUIC implementations.** No second stack is
  installed and there is no network at measurement time.
* **Performance.** Throughput, CPU, allocation counts and congestion-control
  *efficiency* are never measured. Congestion controllers are checked on
  observable state machine behaviour, not on how fast they go.
* **Anything under `fuzz/`**, and your own test suite: the checker does not
  compile either.
* **Documentation, comments, formatting, clippy.**
* **Platform support other than x86-64 Linux**, and every Windows, macOS, iOS
  or Android code path.
* **The `boringssl-boring-crate` feature** at the releases where it is
  optional. Its crate version is yanked from crates.io and cannot be resolved
  offline; the default feature set is what gets built.

