# TEST_INTERFACE — step 11, channel padding and stability

This is the complete list of public items the step-11 conformance suite checks.
The suite touches nothing that is not named below. Where a signature is given it
is the exact one the driver compiles against; where a value is asserted exactly,
the value is stated here.

Conventions used throughout:

* A path such as `tor_cell::chancell::msg::PaddingNegotiate` is the path the
  driver imports. The item must be *reachable* there from a dependent crate; it
  need not be defined in that module.
* `Debug`, `Clone`, `PartialEq`, `Eq`, `Default`, `Display`, `FromStr`,
  `Serialize` and `Deserialize` are checked on many of the types below. Where a
  rendering or a parse is asserted, the exact text is given.
* Section numbers (`§1`, `§2`, ...) are the ones used by the group map at the
  end of this document.
* Anything reachable in these crates but not named here is **not** checked.
  Where the suite deliberately declines to check something adjacent, that is
  said explicitly rather than left to inference.

Two cargo features must exist and must compile, because the step-11 driver's
own manifest enables them on crates in your workspace:

| Crate | Feature | What it unlocks | Why the suite needs it |
| --- | --- | --- | --- |
| `tor-cell` | `testing` | upstream declares it as `testing = ["experimental-udp"]` in `crates/tor-cell/Cargo.toml`. It gates exactly one item in the whole crate: `PaddingNegotiate::from_raw(command: PaddingNegotiateCmd, ito_low_ms: u16, ito_high_ms: u16) -> Self` (`src/chancell/msg.rs`), a test-only constructor that bypasses the `start`/`stop` invariants. It transitively turns on `experimental-udp` | **Nothing in the suite calls `from_raw`.** The feature is required purely so that cargo metadata resolution of the driver succeeds: the driver's `Cargo.toml` names `tor-cell` with `features = ["testing"]`, and a `tor-cell` whose `[features]` table lacks `testing` fails resolution before any test compiles. Declaring the feature is sufficient; `experimental-udp` may be empty |
| `tor-netdir` | `testing` | upstream declares `testing = ["hex", "hex-literal", "tor-netdoc/build_docs"]` in `crates/tor-netdir/Cargo.toml`. Its one gate in the crate is `#[cfg(any(test, feature = "testing"))] pub mod testnet;` in `src/lib.rs`, which exposes `testnet::construct_netdir() -> PartialNetDir`, `construct_custom_netdir<F>(func: F) -> BuildResult<PartialNetDir>` and `construct_custom_netdir_with_params<F, P, PK>(func: F, params: P) -> BuildResult<PartialNetDir>` where `F: FnMut(usize, &mut NodeBuilders)`, `P: IntoIterator<Item = (PK, i32)>`, `PK: Into<String>` (see §11) | the `netdirids`, `dirparams` and `ccparams` groups build a synthetic consensus with `testnet::construct_netdir()` and `construct_custom_netdir_with_params(...)`; there is no other way to obtain a `NetDir` in a unit test. The `hex`/`hex-literal` deps and `tor-netdoc/build_docs` must be present or `testnet.rs` does not compile |

These are not optional extras, but the consequence of omitting one is narrower
than "the step fails". The driver's manifest names them, so cargo would
normally resolve them *before a single test runs* — but this verifier repairs
exactly that case: it reads each candidate crate's declared features, drops any
request the crate does not declare from its own copy of the driver manifest,
records it, and checks on. So a `tor-cell` without `testing` still runs every
group; the failure lands on the groups that need the gated items and nowhere
else. Two things follow:

- **`tor-cell/testing` gates nothing the suite calls.** No case anywhere under
  the harness names `PaddingNegotiate::from_raw`. Declaring the feature — even
  as an empty entry, since `experimental-udp` may be empty — is cheap insurance
  against a future release of this verifier being stricter, and it is what the
  release does; omitting it costs no case today.
- **`tor-netdir/testing` is load-bearing.** Its gate is
  `pub mod testnet`, and `netdirids`, `dirparams` and `ccparams` build their
  synthetic consensus through it. A crate missing the feature (or the
  `hex`/`hex-literal` deps, or `tor-netdoc/build_docs`, which `testnet.rs`
  needs to compile) fails those groups individually, because the items they
  import never appear.

---

## 1. PADDING_NEGOTIATE cell — tor-cell

Checked by `padnegcell` and used by `padparams` (§3). Everything here
lives in `crates/tor-cell/src/chancell/msg.rs` and is imported by the driver as
`tor_cell::chancell::msg::{Body, ChanMsg, PaddingNegotiate, PaddingNegotiateCmd}`.

**What changed at this milestone.** Before v1.0.0 the cell's command was a bare
`bool` and there was no `PaddingNegotiateCmd` type. The suite asserts on the new
type, so the type-level shape is checked as much as the wire bytes.

### Command type: `PaddingNegotiateCmd`, a `caret_int!` newtype over `u8`

```rust
caret_int! {
    pub struct PaddingNegotiateCmd(u8) {
        START = 2,
        STOP = 1,
    }
}
```

Note the inversion: **START is 2, STOP is 1**. The suite asserts
`u8::from(PaddingNegotiateCmd::START) == 2`, `u8::from(PaddingNegotiateCmd::STOP) == 1`,
`PaddingNegotiateCmd::from(2u8) == PaddingNegotiateCmd::START`,
`PaddingNegotiateCmd::from(1u8) == PaddingNegotiateCmd::STOP`, and
`START != STOP`. The `caret_int!` macro (`crates/caret/src/lib.rs`) supplies
`#[derive(PartialEq, Eq, Copy, Clone)]`, `From<PaddingNegotiateCmd> for u8`,
`From<u8> for PaddingNegotiateCmd`, `Display` (name or number) and `Debug`
(`PaddingNegotiateCmd(START)`). It also generates `pub fn is_recognized(self) -> bool`
and `pub fn from_name(name: &str) -> Option<Self>`, neither of which is checked. The
`get(self) -> u8` accessor the macro generates is **private** to the defining
module; the driver only ever goes through `u8::from(..)`.

### Message body: `PaddingNegotiate`

```rust
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PaddingNegotiate {
    command: PaddingNegotiateCmd,
    ito_low_ms: u16,
    ito_high_ms: u16,
}

impl PaddingNegotiate {
    pub fn start_default() -> Self;   // START, 0, 0
    pub fn start(ito_low: IntegerMilliseconds<u16>, ito_high: IntegerMilliseconds<u16>) -> Self;
    pub fn stop() -> Self;            // STOP, 0, 0
    #[cfg(feature = "testing")]
    pub fn from_raw(command: PaddingNegotiateCmd, ito_low_ms: u16, ito_high_ms: u16) -> Self;
}
impl Default for PaddingNegotiate { fn default() -> Self { Self::start_default() } }
```

`IntegerMilliseconds` is `tor_units::IntegerMilliseconds<T>`; the driver
constructs it with `IntegerMilliseconds::new(1500u16)`. `start` stores
`ito_low.as_millis()` / `ito_high.as_millis()` verbatim. The fields are private;
the suite reads them back only via `PartialEq` and via encoding, so the field
*names* are out of scope but `Eq`/`PartialEq` on the struct is (both
`padnegcell__start_body_round_trips` and `padnegcell__stop_body_round_trips`
compare a decoded value against `PaddingNegotiate::start(..)`/`stop()` with
`assert_eq!`). `from_raw` is never called by the suite (see the cargo-feature
table above).

### Wire format — `Body` and `Readable`

`PaddingNegotiate` implements `Body` and `Readable`. Its body is exactly six
bytes: `version u8 (== 0) | command u8 | ito_low_ms u16 BE | ito_high_ms u16 BE`,
and `into_message()` yields the `ChanMsg::PaddingNegotiate(self)` variant that
`encode_body` writes. Decoding a body whose version byte is not zero fails with
an error whose `Display` contains `"Unrecognized padding negotiation version"`;
a well-formed body decodes back to a value equal to the one that encoded it
(`Eq` is checked through exactly that round trip).

The suite compares encodings byte-for-byte against fixtures whose contents are:

| Constructor | Fixture | Bytes |
| --- | --- | --- |
| `start_default()` and `default()` | `padneg_start_default.bin` | `00 02 00 00 00 00` |
| `start(1500, 9500)` | `padneg_start_1500_9500.bin` | `00 02 05 dc 25 1c` |
| `stop()` | `padneg_stop.bin` | `00 01 00 00 00 00` |

Encoding goes through the driver helper `encode_body(msg: ChanMsg) -> Vec<u8>`, which calls
`ChanMsg::write_body_onto(self, &mut Vec<u8>)` — so `ChanMsg` (not only
`PaddingNegotiate`) must expose `write_body_onto` and `Vec<u8>` must be a `Writer`
(§15). Decoding goes through
`ChanMsg::take(r: &mut Reader<'_>, cmd: ChanCmd) -> tor_bytes::Result<ChanMsg>` with
`ChanCmd::PADDING_NEGOTIATE`, and the result must be the `ChanMsg::PaddingNegotiate(PaddingNegotiate)`
variant. `ChanMsg::cmd(&self) -> ChanCmd` must return `ChanCmd::PADDING_NEGOTIATE` for it.

**Version check, exact string.** The body `01 01 00 00 00 00` (version byte 1)
must fail to decode, and `err.to_string()` must *contain*
`"Unrecognized padding negotiation version"`. Upstream returns
`tor_bytes::Error::BadMessage("Unrecognized padding negotiation version")`, whose
`Display` is `"Bad object: Unrecognized padding negotiation version"` — the suite
checks containment, so the `Bad object:` prefix is free but the quoted text must
appear verbatim. The decode also requires the reader to be truncated to the body:
`padneg_stop_trailing.bin` (a STOP body with an extra `ff`) exists in the fixture
directory but is **not** referenced by any test; trailing-byte behaviour is not
checked.

**Out of scope here:** `PaddingNegotiateCmd::is_recognized`/`from_name`,
`Display` of either type, the `msg_into_cell!(PaddingNegotiate)` conversion into
`ChanCell` (it exists upstream and sets circuit ID 0), and how a live channel
reactor reacts to receiving the cell.

## 2. Channel cell format and command table — tor-cell

Checked by `cellfmt` (a gate group: it adds no requirement of its own, and a failure there means the step is not satisfied). Nothing in this section is new at
v1.0.0; it is checked because the milestone adds a new fixed-length command
(`PADDING_NEGOTIATE = 12`) to the same numbering space, and the natural way to
get that wrong is to renumber or re-classify the cells around it. Source:
`crates/tor-cell/src/chancell.rs` and `chancell/msg.rs`. Driver imports:
`tor_cell::chancell::{ChanCell, ChanCmd, CircId, CELL_DATA_LEN}` and
`tor_cell::chancell::msg::{Body, ChanMsg, Netinfo, Padding, Unrecognized, Versions}`.

### Constants and command numbers

```rust
pub const CELL_DATA_LEN: usize = 509;
pub type RawCellBody = [u8; CELL_DATA_LEN];   // exists upstream; out of scope

caret_int! {
    pub struct ChanCmd(u8) {
        PADDING = 0, CREATE = 1, CREATED = 2, RELAY = 3, DESTROY = 4,
        CREATE_FAST = 5, CREATED_FAST = 6,
        // 7 is VERSIONS, listed with the variable-length cells below
        NETINFO = 8, RELAY_EARLY = 9, CREATE2 = 10, CREATED2 = 11,
        PADDING_NEGOTIATE = 12,
        VERSIONS = 7, VPADDING = 128, CERTS = 129, AUTH_CHALLENGE = 130,
        AUTHENTICATE = 131, AUTHORIZE = 132,
    }
}
```

The suite asserts `u8::from(cmd) == n` for every pair above except `AUTHORIZE`,
and `CELL_DATA_LEN == 509`. `ChanCmd::from(u8)` must accept unrecognized values
(200, 250, 13 are used) and `Display` on `ChanCmd` must exist (it is used in
assertion messages: `"wrong number for {cmd}"`).

### Fixed/variable and circuit-ID classification

```rust
impl ChanCmd {
    pub fn is_var_cell(self) -> bool {
        self == ChanCmd::VERSIONS || self.0 >= 128_u8
    }
    fn allows_circid(self) -> CircIdReq { /* private */ }
    pub fn accepts_circid_val(self, id: CircId) -> bool {
        match (self.allows_circid(), id.is_zero()) {
            (CircIdReq::WantNonZero, true) => false,
            (CircIdReq::WantZero, false) => false,
            (_, _) => true,
        }
    }
}
```

Required facts: `VERSIONS`, `VPADDING`, `CERTS` and `ChanCmd::from(200)` are
var-cells; `PADDING`, `RELAY`, `PADDING_NEGOTIATE` and `ChanCmd::from(13)` are
not. For the circuit-ID rule the private `allows_circid` table must put
`PADDING | NETINFO | PADDING_NEGOTIATE | VERSIONS | VPADDING | CERTS | AUTH_CHALLENGE | AUTHENTICATE`
in the *want-zero* class, `CREATE | CREATED | RELAY | DESTROY | CREATE_FAST | CREATED_FAST | RELAY_EARLY | CREATE2 | CREATED2`
in the *want-non-zero* class, and everything else (`ChanCmd::from(250)` is
tested) in an *any* class that accepts both zero and non-zero. The suite tests
`PADDING, NETINFO, VERSIONS, PADDING_NEGOTIATE, CERTS` against `CircId::from(0)`
(accepted) and `CircId::from(0x1234_5678)` (rejected), and `RELAY, CREATE2,
DESTROY` the other way round.

### Types `CircId` and `ChanCell`

```rust
#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub struct CircId(u32);
impl From<u32> for CircId { .. }
impl From<CircId> for u32 { .. }
impl std::fmt::Display for CircId { .. }
impl CircId { pub fn is_zero(&self) -> bool; }

#[derive(Debug)]
pub struct ChanCell { circid: CircId, msg: msg::ChanMsg }
impl ChanCell {
    pub fn new(circid: CircId, msg: msg::ChanMsg) -> Self;
    pub fn circid(&self) -> CircId;
    pub fn msg(&self) -> &msg::ChanMsg;
    pub fn into_circid_and_msg(self) -> (CircId, msg::ChanMsg);
}
```

The suite builds `ChanCell::new(CircId::from(7), Padding::new().into_message())`
and reads back `u32::from(cell.circid()) == 7`, `cell.msg().cmd() == ChanCmd::PADDING`,
and the same after `into_circid_and_msg()`. A `Padding` body encodes to zero
bytes (`Padding::new()` is `#[derive(Clone, Debug, Default)] #[non_exhaustive] pub struct Padding {}`
and `write_body_onto` writes nothing).

### The `Versions` message

```rust
#[derive(Clone, Debug)]
pub struct Versions { versions: Vec<u16> }
impl Versions {
    pub fn new<B>(vs: B) -> crate::Result<Self> where B: Into<Vec<u16>>;
    pub fn encode_for_handshake(self) -> EncodeResult<Vec<u8>>;
    pub fn best_shared_link_protocol(&self, my_protos: &[u16]) -> Option<u16>;
}
```

`new` errors (`Error::CantEncode("Too many versions")`) only when
`versions.len() >= u16::MAX / 2`; the suite never triggers it. The body is the
bare sequence of big-endian `u16`s: `Versions::new([3u16, 4, 5])` encodes to
fixture `versions_3_4_5.bin` = `00 03 00 04 00 05`, and decoding that with
`ChanMsg::take(&mut r, ChanCmd::VERSIONS)` must give `ChanMsg::Versions(_)`
(`Readable` reads `u16`s `while r.remaining() > 0`).
`best_shared_link_protocol(&[4, 5, 100])` on `{3,4,5}` must be `Some(5)`
(numerically highest value present on both sides), and `&[1, 2]` must be `None`.
`encode_for_handshake` frames with the **two-byte** circuit ID that predates
version negotiation: `write_u16(0)`, `write_u8(ChanCmd::VERSIONS.into())`,
`write_u16(len * 2)`, then the body. The suite asserts the exact result
`[0x00,0x00, 0x07, 0x00,0x06, 0x00,0x03, 0x00,0x04, 0x00,0x05]` for `[3, 4, 5]`.

### The `Netinfo` message

```rust
#[derive(Clone, Debug)]
pub struct Netinfo { timestamp: u32, their_addr: Option<IpAddr>, my_addr: Vec<IpAddr> }
impl Netinfo {
    pub fn for_client(their_addr: Option<IpAddr>) -> Self;   // timestamp 0, my_addr empty
    pub fn timestamp(&self) -> Option<std::time::SystemTime>; // None when the wire value is 0
}
```

`Netinfo::for_client(None)` must encode to fixture `netinfo_client_unspec.bin` =
`00 00 00 00 | 04 04 00 00 00 00 | 00`: a four-byte zero timestamp, then
`their_addr` encoded as `type 0x04, len 4, 0.0.0.0` (a `None` address is written
as `Ipv4Addr::UNSPECIFIED`), then a single zero byte for "number of my
addresses". Decoding that fixture and calling `.timestamp()` must give `None`
(a zero timestamp means *not reported*, never 1970). `Netinfo::for_relay`,
`their_addr()` and `my_addrs()` are out of scope.

### The `Unrecognized` message

```rust
#[derive(Clone, Debug)]
pub struct Unrecognized { cmd: ChanCmd, content: Vec<u8> }
impl Unrecognized {
    pub fn new<B>(cmd: ChanCmd, content: B) -> Self where B: Into<Vec<u8>>;
}
```

`ChanMsg::take(&mut r, ChanCmd::from(200))` over body `de ad be ef` must
*succeed* and yield a message whose `.cmd()` is `ChanCmd::from(200)` and whose
body re-encodes to exactly `de ad be ef`. `ChanMsg::take`'s fall-through arm is
`_ => Unrecognized(unrecognized_with_cmd(cmd, r)?)`, which reads the whole
remainder and stamps the command on it. `Unrecognized::new(cmd, &body[..]).into_message()`
must behave identically.

### The `ChanMsg` and `Body` surface used by every cell test

```rust
pub trait Body: Readable {
    fn into_message(self) -> ChanMsg;
    fn write_body_onto<W: Writer + ?Sized>(self, w: &mut W) -> EncodeResult<()>;
}
#[non_exhaustive] #[derive(Clone, Debug)]
pub enum ChanMsg { Padding(Padding), .., Netinfo(Netinfo), Versions(Versions),
                   PaddingNegotiate(PaddingNegotiate), .., Unrecognized(Unrecognized) }
impl ChanMsg {
    pub fn cmd(&self) -> ChanCmd;
    pub fn write_body_onto<W: Writer + ?Sized>(self, w: &mut W) -> EncodeResult<()>;
    pub fn take(r: &mut Reader<'_>, cmd: ChanCmd) -> tor_bytes::Result<Self>;
}
```

The `Body` trait must be in scope for `.into_message()` to resolve, which is why
every cell test imports it. `ChanMsg::cmd()` must be `Debug`-printable in panics
(`{:?}` on a `ChanCmd`).

## 3. Padding timing parameters — tor-proto

Checked by `padparams`; also the value type carried by §4. Source:
`crates/tor-proto/src/channel/padding.rs`. Driver import:
`tor_proto::channel::padding::Parameters`. The module `channel::padding` must
be `pub` (upstream: `pub mod padding;` in `crates/tor-proto/src/channel.rs`).

**What changed at this milestone.** At v0.6.0 the fields were `low_ms`/`high_ms`
and none of `builder()`, `default_padding()`, `disabled()` or
`padding_negotiate_cell()` existed.

### The `Parameters` struct and its builder

```rust
#[derive(Debug, Copy, Clone, Eq, PartialEq, Builder)]
#[builder(build_fn(error = "tor_error::Bug"))]
pub struct Parameters {
    #[builder(default = "1500.into()")]
    pub(crate) low: IntegerMilliseconds<u32>,
    #[builder(default = "9500.into()")]
    pub(crate) high: IntegerMilliseconds<u32>,
}

impl_standard_builder! { Parameters: !Deserialize + !Builder + !Default }

impl Parameters {
    pub fn padding_negotiate_cell(&self) -> Result<PaddingNegotiate, tor_error::Bug>;
    pub fn default_padding() -> Self;   // builder().build().expect(..)  => 1500 / 9500
    pub fn disabled() -> Self;          // low: 0.into(), high: 0.into()
}
```

The builder is entirely `derive_builder`-generated (there is no hand-written
`ParametersBuilder` in the crate), so its shape is the derive_builder default:

```rust
pub struct ParametersBuilder { .. }   // Default + Clone
impl ParametersBuilder {
    pub fn low(&mut self, value: IntegerMilliseconds<u32>) -> &mut Self;
    pub fn high(&mut self, value: IntegerMilliseconds<u32>) -> &mut Self;
    pub fn build(&self) -> Result<Parameters, tor_error::Bug>;
}
impl Parameters { pub fn builder() -> ParametersBuilder; }   // from impl_standard_builder!
```

`impl_standard_builder!` (`crates/tor-config/src/lib.rs`) with the
`!Deserialize + !Builder + !Default` flags generates *only* `Parameters::builder()`
plus a private test; there is no `Default` for `Parameters` (deliberate: the two
sensible defaults, "spec padding" and "disabled", are named constructors instead).
The driver chains `.low(IntegerMilliseconds::new(2000u32)).high(...).build()` on
the temporary returned by `builder()`, which is why the setters take and return
`&mut Self`.

### What is asserted, exactly

Every case converts through `padding_negotiate_cell()` and then encodes the
resulting `PaddingNegotiate` with §1's `encode_body`, so the in-scope surface is
the six-byte wire form:

| Input | Expected body |
| --- | --- |
| `Parameters::default_padding()` | `00 02 05 DC 25 1C` (START, 1500, 9500) |
| `Parameters::builder().build()` with nothing set | `00 02 05 DC 25 1C` — builder defaults are the spec defaults, **not** zeroes |
| `Parameters::disabled()` | `00 02 00 00 00 00` — still START, both bounds zero |
| `builder().low(2000).high(8000).build()` | byte 0 == `0x00`, byte 1 == `u8::from(PaddingNegotiateCmd::START)`, `u16::from_be_bytes(body[2..4]) == 2000`, `u16::from_be_bytes(body[4..6]) == 8000` |
| `builder().low(1500).high(9500).build().padding_negotiate_cell()` | `== PaddingNegotiate::start(IntegerMilliseconds::new(1500u16), IntegerMilliseconds::new(9500u16))` and `!= PaddingNegotiate::stop()` |

`padding_negotiate_cell` **always produces START**; stopping padding is a
different message and is never derived from a `Parameters`. `disabled()` is a
sentinel meaning "no padding", but it encodes as `START(0,0)`, which on the wire
means "use the consensus defaults" — the caller (§4 / the channel reactor) is
responsible for not sending it literally. That distinction is documented
upstream and the suite checks only the encoding.

### Range check, exact string

The parameters are `u32` milliseconds and the cell fields are `u16`, so
`padding_negotiate_cell()` is fallible: it converts *both* bounds, and a bound
that does not fit in a `u16` is an error rather than a truncation.

`builder().low(70_000).high(90_000).build()` must **succeed** (the builder does
not range-check), and `.padding_negotiate_cell()` on it must return `Err` whose
`to_string()` *contains* `"padding negotiate out of range"`. Upstream's error is
a `tor_error::Bug` built by `into_internal!`, whose `Display` is
`"{kind} at {file:line:col}: {message}"` — i.e. `Internal at ...: padding negotiate out of range`
followed by a backtrace section; only the quoted message is checked. The error
type must be `tor_error::Bug` (the test only calls `to_string()`, so any type
with that `Display` compiles, but the `build_fn(error = "tor_error::Bug")`
annotation is the upstream shape). `IntegerMilliseconds::try_map` is in
`crates/tor-units/src/lib.rs`; it is not directly checked.

**Out of scope:** the `low`/`high` field names (they are `pub(crate)`), the
private test generated by `impl_standard_builder!`, and `Parameters` values
larger than `u16` on only one side.

## 4. Channel padding instructions and updates — tor-proto

Checked by `padinsns` and `padupdates`. Source:
`crates/tor-proto/src/channel/params.rs`; all three types are generated by the
crate-local macro `define_channels_insns_and_automatic_impls!` invoked once in
that file. Driver imports:
`tor_proto::channel::{ChannelPaddingInstructions, ChannelPaddingInstructionsUpdates}`
and `tor_proto::channel::padding::Parameters` (§3). Upstream reaches them via
`pub mod params;` plus `pub use crate::channel::params::*;` in
`crates/tor-proto/src/channel.rs`; `tor_proto::ChannelPaddingInstructions` is
additionally re-exported at the crate root but the driver does not use that path.

**What changed at this milestone.** Neither the updates type nor its builder
existed at v0.6.0; the reactor was reconfigured wholesale rather than by diff.

### The instructions and their default

```rust
#[derive(Debug, Educe, Clone, Eq, PartialEq)]
#[educe(Default)]
pub struct ChannelPaddingInstructions {
    pub(crate) padding_enable: bool,
    #[educe(Default(expression = "padding::Parameters::disabled()"))]
    pub(crate) padding_parameters: padding::Parameters,
    pub(crate) padding_negotiate: PaddingNegotiate,
}
```

The default is `padding_enable: false`, `padding_parameters: Parameters::disabled()`,
`padding_negotiate: PaddingNegotiate::start_default()` (`PaddingNegotiate`'s own
`Default`, §1). The suite relies on these three specific defaults: `padding_enable(true)`
and `padding_parameters(Parameters::default_padding())` must each register as a
change against a default, while `padding_enable(false)` and
`padding_parameters(Parameters::disabled())` must **not**. `Eq` on the struct is
checked (`assert_eq!(a, b)` on two defaults, and `assert_eq!(j, ChannelPaddingInstructions::default())`
after a no-op update).

### The update type

```rust
#[derive(Debug, Default, Clone, Eq, PartialEq)]
pub struct ChannelPaddingInstructionsUpdates {
    pub(crate) padding_enable: Option<bool>,
    pub(crate) padding_parameters: Option<padding::Parameters>,
    pub(crate) padding_negotiate: Option<PaddingNegotiate>,
}

impl ChannelPaddingInstructionsUpdates {
    /// Values from `more` override ones in `self`.
    pub fn combine(&mut self, more: &Self) {
        if let Some(v) = &more.padding_enable     { self.padding_enable = Some(v.clone()); }
        if let Some(v) = &more.padding_parameters { self.padding_parameters = Some(v.clone()); }
        if let Some(v) = &more.padding_negotiate  { self.padding_negotiate = Some(v.clone()); }
    }
    #[cfg(feature = "testing")] pub fn padding_enable(&self) -> Option<&bool>;              // out of scope
    #[cfg(feature = "testing")] pub fn padding_parameters(&self) -> Option<&padding::Parameters>;
    #[cfg(feature = "testing")] pub fn padding_negotiate(&self) -> Option<&PaddingNegotiate>;
}
```

`Default` (all `None`), `Clone` and `Eq` are checked. The driver does not enable
`tor-proto/testing`, so the three per-field accessors are invisible to it and
are out of scope; every observation is made through `PartialEq` between two
independently built updates.

### The builder

```rust
impl ChannelPaddingInstructions {
    pub fn start_update(&mut self) -> ChannelPaddingInstructionsUpdatesBuilder<'_>;
    #[must_use]
    pub fn initial_update(&self) -> Option<ChannelPaddingInstructionsUpdates>;
}

pub struct ChannelPaddingInstructionsUpdatesBuilder<'c> {
    insns: &'c mut ChannelPaddingInstructions,
    update: Option<ChannelPaddingInstructionsUpdates>,   // None => nothing changed yet
    drop_bomb: bool,
}
impl<'c> ChannelPaddingInstructionsUpdatesBuilder<'c> {
    pub fn padding_enable(self, new_value: bool) -> Self;
    pub fn padding_parameters(self, new_value: padding::Parameters) -> Self;
    pub fn padding_negotiate(self, new_value: PaddingNegotiate) -> Self;
    #[must_use]
    pub fn finish(mut self) -> Option<ChannelPaddingInstructionsUpdates>;
}
impl<'c> Drop for ChannelPaddingInstructionsUpdatesBuilder<'c> { /* panics if finish() was not called */ }
```

The setters take `self` by value and return `Self` (the driver chains them on
the temporary from `start_update()`), and each one records a change **only if
`new_value != self.insns.<field>`**:

```rust
if &new_value != &self.insns.$field {
    self.update.get_or_insert_with(Default::default).$field = Some(new_value.clone());
    self.insns.$field = new_value;
}
```

So the builder mutates the instructions *in place* as it goes — after
`i.start_update().padding_enable(true).finish()`, `i` holds `true`, and a second
`i.start_update().padding_enable(false).finish()` is again a change
(`padupdates__combine_overrides_with_later_values` depends on this).
`finish()` clears the drop bomb and returns `self.update.take()`, so **no change
at all yields `None`, never `Some(empty)`**. `Drop` asserts
`!self.drop_bomb` with the message `"ChannelPaddingInstructionsUpdatesBuilder dropped"`;
every driver case calls `finish()`, so the panic itself is out of scope.

`initial_update()` diffs `self` against a fresh default via the same builder:

```rust
let mut supposed = ChannelPaddingInstructions::default();
supposed.start_update()
    .padding_enable(self.padding_enable.clone())
    .padding_parameters(self.padding_parameters.clone())
    .padding_negotiate(self.padding_negotiate.clone())
    .finish()
```

It must be `None` for a default instance and `Some` once any field differs
from the default.

### What is asserted, exactly

* `ChannelPaddingInstructions::default().initial_update().is_none()`.
* After `i.start_update().padding_parameters(Parameters::default_padding()).finish()` is `Some`, `i.initial_update()` is `Some`.
* `i.start_update().finish().is_none()` and `j.start_update().padding_enable(false).finish().is_none()`, and `j` still equals the default afterwards.
* `start_update().padding_enable(true).padding_parameters(Parameters::disabled()).finish()` equals `start_update().padding_enable(true).finish()` (the redundant setter leaves no trace).
* `first.combine(&second)` where `first = {padding_enable: Some(true)}`, `second = {padding_enable: Some(false)}` makes `first == second` and `first != before`.
* `params_update.combine(&enable_update)` equals a single update built with both setters — order of setters is irrelevant, `combine` must not drop the field it does not override.
* `u.combine(&ChannelPaddingInstructionsUpdates::default())` leaves `u` unchanged.

**Out of scope:** `padding_negotiate(..)` as a setter (only `padding_enable` and
`padding_parameters` are exercised), `Channel::reparameterize(Arc<ChannelPaddingInstructionsUpdates>)`,
and anything in the channel reactor.

## 5. Padding level configuration — tor-config

Checked by `padlevel`; also the field type of §6's `ChannelConfig`.
Source: `crates/tor-config/src/misc.rs`, reachable as `tor_config::PaddingLevel`
because `lib.rs` has `mod misc;` (private) and `pub use misc::*;`. The driver
imports only `tor_config::PaddingLevel`; the module path `tor_config::misc` is
**not** public upstream and must not be relied on.

**What changed at this milestone.** At v0.6.0 there was no such enum: the
channel manager had a bare `padding: bool` and `arti` spelled the setting as a
boolean. Every assertion below is a discriminator.

### The enum

```rust
#[derive(Clone, Copy, Hash, Debug, Ord, PartialOrd, Eq, PartialEq)]
#[allow(clippy::exhaustive_enums)]
#[derive(Serialize, Deserialize)]
#[serde(try_from = "PaddingLevelSerde", into = "PaddingLevelSerde")]
#[derive(Display, EnumString, IntoStaticStr)]   // strum
#[strum(serialize_all = "snake_case")]
#[derive(Educe)]
#[educe(Default)]
pub enum PaddingLevel {
    None,
    Reduced,
    #[educe(Default)]
    Normal,
}
```

There are no hand-written `Display`, `FromStr` or `From<&PaddingLevel>` impls;
all three come from the `strum` derives and the snake_case rule. The enum is
deliberately **not** `#[non_exhaustive]` (upstream allows `clippy::exhaustive_enums`
on it), and the driver's `for level in [None, Reduced, Normal]` loop and
`matches!`-free comparisons compile either way.

### What is asserted, exactly

| Property | Assertion |
| --- | --- |
| `Default` | `PaddingLevel::default() == PaddingLevel::Normal` |
| `Ord` | `None < Reduced < Normal` — declaration order, so the variant order above is checked |
| `Display` | `None.to_string() == "none"`, `Reduced.to_string() == "reduced"`, `Normal.to_string() == "normal"` |
| `FromStr` | each `Display` output parses back to its own variant; `from_str("none") == Ok(None)` |
| `FromStr` rejects | `"louder"`, `""` and `"Normal"` (capitalised) are all `Err` — matching is exact and case-sensitive; do not add a case-insensitive `strum(ascii_case_insensitive)` |
| `From<&PaddingLevel> for &'static str` | `<&str>::from(&PaddingLevel::None) == "none"` etc. — this is what `IntoStaticStr` generates; it also generates the by-value `From<PaddingLevel>`, which is out of scope |
| `Serialize` | `serde_json::to_string(&Reduced) == "\"reduced\""` — always a string, never a bool |
| `Deserialize` | `"\"reduced\""` → `Reduced`; **legacy bool** `true` → `Normal`, `false` → `None`; `"\"sideways\""` → `Err` |

The `FromStr::Err` type upstream is `strum::ParseError`; the suite only calls
`.is_err()`/`.unwrap()`/`.expect()` on it, so the error type is out of scope.

### The serde shim

```rust
#[derive(Serialize, Deserialize)]
#[serde(untagged)]
enum PaddingLevelSerde {
    String(Cow<'static, str>),   // always serialised this way
    Bool(bool),
}

impl From<PaddingLevel> for PaddingLevelSerde {
    fn from(pl: PaddingLevel) -> PaddingLevelSerde {
        PaddingLevelSerde::String(<&str>::from(&pl).into())
    }
}

#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
#[error("Invalid padding level")]
struct InvalidPaddingLevel {}

impl TryFrom<PaddingLevelSerde> for PaddingLevel {
    type Error = InvalidPaddingLevel;
    fn try_from(pls: PaddingLevelSerde) -> Result<PaddingLevel, Self::Error> {
        Ok(match pls {
            PaddingLevelSerde::String(s) => s.as_ref().try_into().map_err(|_| InvalidPaddingLevel {})?,
            PaddingLevelSerde::Bool(false) => PaddingLevel::None,
            PaddingLevelSerde::Bool(true) => PaddingLevel::Normal,
        })
    }
}
```

The string arm goes through `TryFrom<&str>` (also from `EnumString`), so the
serde spelling and the `FromStr` spelling are the same three words. The
`"Invalid padding level"` message is not asserted anywhere; only `is_err()` is.

**Out of scope:** `Hash`, `Copy` (they are used implicitly but never asserted),
`serde` round-trips of `None`/`Normal` (only `Reduced` and the two booleans are
exercised), and any TOML-level behaviour — every serde case uses `serde_json`.

## 6. Channel configuration and usage — tor-chanmgr

Checked by `chanconfig` and `chanusage`. Sources:
`crates/tor-chanmgr/src/config.rs` and `crates/tor-chanmgr/src/lib.rs`. Driver
imports: `tor_chanmgr::{ChannelConfig, ChannelConfigBuilder}`,
`tor_chanmgr::{ChannelUsage, Dormancy}`, and `tor_config::PaddingLevel` (§5).
Upstream's `lib.rs` has `pub use config::{ChannelConfig, ChannelConfigBuilder};`
and defines `Dormancy` and `ChannelUsage` directly at the crate root.

**What changed at this milestone.** At v0.6.0 `ChanMgr::new` was `new(runtime)`;
there was no `ChannelConfig`, no `Dormancy` and no `ChannelUsage`.

### The `ChannelConfig` struct and its builder

```rust
use tor_config::impl_standard_builder;
use tor_config::{ConfigBuildError, PaddingLevel};
use derive_builder::Builder;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Builder, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[builder(derive(Debug, Serialize, Deserialize))]
pub struct ChannelConfig {
    #[builder(default)]
    pub(crate) padding: PaddingLevel,
}
impl_standard_builder! { ChannelConfig }

#[cfg(feature = "testing")]
impl ChannelConfig {
    pub fn padding(&self) -> PaddingLevel { self.padding }
}
```

The generated `ChannelConfigBuilder` is therefore
`#[derive(Debug, Serialize, Deserialize)]` (plus derive_builder's own `Default`
and `Clone`), with one setter `pub fn padding(&mut self, value: PaddingLevel) -> &mut Self`
and `pub fn build(&self) -> Result<ChannelConfig, ConfigBuildError>`.
`impl_standard_builder! { ChannelConfig }` with no flags generates all of:

* `ChannelConfig::builder() -> ChannelConfigBuilder` (`Default::default()`);
* `impl Default for ChannelConfig` as `ChannelConfigBuilder::default().build().unwrap()`;
* `impl tor_config::load::Builder for ChannelConfigBuilder { type Built = ChannelConfig; .. }`;
* a private `#[test]` that builds the config from an empty `config::Config` via `try_deserialize()`.

The driver does not enable `tor-chanmgr/testing`, so the `padding()` accessor
is invisible to it and is out of scope; every observation goes through
`PartialEq` on `ChannelConfig` and through serde on the **builder**.

### What is asserted, exactly

* `ChannelConfig::default() == ChannelConfig::builder().build().unwrap()`.
* Two builds with `.padding(PaddingLevel::Reduced)` are `==` to each other and `!=` the default (so the default level must be `Normal`, which follows from `#[builder(default)]` + `PaddingLevel::default()`).
* `serde_json::from_str::<ChannelConfigBuilder>(r#"{"padding":"reduced"}"#)` deserialises, and `.build()` equals a builder-made `Reduced` config. The JSON key is the field name `padding`, unrenamed.
* `serde_json::from_str::<ChannelConfigBuilder>("{}")` deserialises and builds to the default — every builder field is `Option` and missing keys are fine.
* Builder round trip: deserialise `{"padding":"none"}`, `serde_json::to_string(&built)` must **contain** `"\"none\""` (so the builder serialises the level as §5's string, not as a bool or an integer), and deserialising that text again builds to a `None` config.

`ChannelConfig` itself is **not** `Serialize`/`Deserialize` and the suite does
not require it to be; only the builder is. `ConfigBuildError` is never
inspected: every `build()` in the group is expected to succeed.

### Enums `Dormancy` and `ChannelUsage`

```rust
#[non_exhaustive]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Educe)]
#[educe(Default)]
pub enum Dormancy {
    #[educe(Default)]
    Active,
    Dormant,
}

#[derive(Clone, Debug, Copy, Eq, PartialEq)]
#[non_exhaustive]
pub enum ChannelUsage {
    Dir,
    UserTraffic,
    UselessCircuit,
}
```

Asserted: `Dormancy::default() == Dormancy::Active` and `!= Dormant`; the three
`ChannelUsage` variants are pairwise `!=`; both enums are `Copy` (`let copy = used;`
followed by using `used` again) and `Eq`/`Debug` (`seen.contains(&u)`,
`panic!("{:?} appeared twice", u)`); `matches!(state, Dormancy::Dormant)`.
Both enums are `#[non_exhaustive]` upstream; the driver never matches
exhaustively, so that attribute is free either way.

Upstream's consumer of all three types is
`ChanMgr::<R>::new(runtime: R, config: &ChannelConfig, dormancy: Dormancy, netparams: &NetParameters) -> Self`,
which is **out of scope** (constructing a `ChanMgr` needs a runtime and is out of
scope for this step's suite). `ConnStatus`, `ConnBlockage`, `ConnStatusEvents`
and `tor_chanmgr::Error` are likewise out of scope.

## 7. Relay identities — tor-linkspec

Checked by `relayid`; the same types are used by `relayidset` (§8),
`hasrelayids` (§9), `owntarget` (§10) and `netdirids` (§11). Source:
`crates/tor-linkspec/src/ids.rs`, re-exported by `lib.rs` as
`pub use ids::{set::RelayIdSet, RelayId, RelayIdError, RelayIdRef, RelayIdType, RelayIdTypeIter};`.
Driver imports `tor_linkspec::{RelayId, RelayIdError, RelayIdRef, RelayIdType}`.

**What changed at this milestone.** None of these types existed at v0.6.0; code
passed bare `Ed25519Identity`/`RsaIdentity` values around. The suite checks the
*canonical output form* and the *distinction between failure modes*, not merely
that a parse function exists — a parser that is only "close" silently accepts
identities the rest of the network will not.

### The three types

```rust
#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd,
         Display, strum::EnumIter, strum::EnumCount)]
#[non_exhaustive]
pub enum RelayIdType {
    #[display(fmt = "Ed25519")]
    Ed25519,
    #[display(fmt = "RSA (legacy)")]
    Rsa,
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Display, From, Hash)]
#[non_exhaustive]
pub enum RelayId {
    #[display(fmt = "ed25519:{}", _0)]
    Ed25519(Ed25519Identity),
    #[display(fmt = "{}", _0)]
    Rsa(RsaIdentity),
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Display, From, derive_more::TryInto)]
#[non_exhaustive]
pub enum RelayIdRef<'a> {
    #[display(fmt = "ed25519:{}", _0)]
    Ed25519(&'a Ed25519Identity),
    #[display(fmt = "{}", _0)]
    Rsa(&'a RsaIdentity),
}
```

`Display` comes from `derive_more`, and the two variants are asymmetric by
design: an Ed25519 identity is printed with an explicit `ed25519:` prefix, while
an RSA identity is printed by delegating to `RsaIdentity`'s own `Display`, which
already emits a leading `$`. So the canonical forms are
`ed25519:AQEBAQ...` and `$abababab...` — **there is no `rsa:` prefix in output**,
even though `rsa:` is accepted on input. `From` gives
`RelayId::from(Ed25519Identity)`, `RelayId::from(RsaIdentity)` and the `&`-taking
equivalents for `RelayIdRef`, all of which the driver uses.

```rust
impl RelayIdType {
    pub fn all_types() -> RelayIdTypeIter { <RelayIdType as strum::IntoEnumIterator>::iter() }
    pub fn id_len(&self) -> usize { match self { Ed25519 => ED25519_ID_LEN, Rsa => RSA_ID_LEN } }
    pub const COUNT: usize = <RelayIdType as strum::EnumCount>::COUNT;
}
impl RelayId {
    pub fn as_ref(&self) -> RelayIdRef<'_>;
    pub fn from_type_and_bytes(id_type: RelayIdType, id: &[u8]) -> Result<Self, RelayIdError>;
    pub fn id_type(&self) -> RelayIdType;
    pub fn as_bytes(&self) -> &[u8];
}
impl<'a> RelayIdRef<'a> {
    pub fn to_owned(&self) -> RelayId;
    pub fn id_type(&self) -> RelayIdType;
    pub fn as_bytes(&self) -> &[u8];
}
impl<'a> From<&'a RelayId> for RelayIdRef<'a> { .. }
```

Asserted metadata: `RelayIdType::COUNT == 2`;
`RelayIdType::all_types().collect::<Vec<_>>() == vec![Ed25519, Rsa]` — **that
exact order**, which is declaration order under `strum::EnumIter`;
`Ed25519.id_len() == 32`, `Rsa.id_len() == 20`;
`Ed25519.to_string() == "Ed25519"`, `Rsa.to_string() == "RSA (legacy)"` (note
the space and the parenthesised word). `RelayIdTypeIter` must be nameable from
outside the crate because `all_types()` returns it.

### Cross-type `PartialEq` against raw keys

```rust
impl<'a> PartialEq<RsaIdentity>     for RelayIdRef<'a> { .. }
impl      PartialEq<RsaIdentity>     for RelayId       { .. }
impl<'a> PartialEq<Ed25519Identity> for RelayIdRef<'a> { .. }
impl      PartialEq<Ed25519Identity> for RelayId       { .. }
```

Generated by a local `impl_eq_variant!` macro; each compares only within its own
variant (`matches!(self, RelayIdRef::$var(this) if this == &other)`), so a
mismatched type is never equal even if the bytes would match. The driver relies
on this for `assert_eq!(bare, rsa_id(0xab))` and `assert_eq!(r, ed_id(0x02))`.

### Parsing (`FromStr`) — the parse order is checked

```rust
impl std::str::FromStr for RelayIdType {
    type Err = RelayIdError;
    // "rsa" / "ed25519", both eq_ignore_ascii_case; anything else UnrecognizedIdType
}

impl std::str::FromStr for RelayId {
    type Err = RelayIdError;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        use base64ct::{Base64Unpadded, Encoding as _};
        if let Some((alg, key)) = s.split_once(':') {
            let alg: RelayIdType = alg.parse()?;
            let len = alg.id_len();
            let mut v = vec![0_u8; len];
            let bytes = Base64Unpadded::decode(key, &mut v[..])?;
            RelayId::from_type_and_bytes(alg, bytes)
        } else if s.len() == RSA_ID_LEN * 2 || s.starts_with('$') {
            let s = s.trim_start_matches('$');
            let bytes = hex::decode(s).map_err(|_| RelayIdError::BadHex)?;
            RelayId::from_type_and_bytes(RelayIdType::Rsa, &bytes)
        } else {
            let mut v = vec![0_u8; ED25519_ID_LEN];
            let bytes = Base64Unpadded::decode(s, &mut v[..])?;
            RelayId::from_type_and_bytes(RelayIdType::Ed25519, bytes)
        }
    }
}
```

Three arms in this order: `alg:base64` first, then `$`-prefixed **or**
exactly-40-character hex as RSA, then unprefixed base64 as Ed25519. The
base64 flavour is **unpadded** (`base64ct::Base64Unpadded`); the decode buffer
is pre-sized to the target length, which is why a wrong-length base64 body
surfaces as `BadLength` rather than `BadBase64`.

The driver's constants:

```rust
const ED_01_B64: &str = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE";  // 43 chars, [0x01; 32]
const RSA_AB_B64: &str = "q6urq6urq6urq6urq6urq6urq6s";                  // 27 chars, [0xab; 20]
const RSA_AB_HEX: &str = "abababababababababababababababababababab";      // 40 chars
```

| Input | Result |
| --- | --- |
| `RSA_AB_HEX` (bare 40-hex) | `Rsa([0xab; 20])`; `to_string() == "$abab…ab"` |
| `"$" + RSA_AB_HEX` | same value, same `to_string()` |
| `RSA_AB_HEX.to_uppercase()` | same value as lowercase; `to_string()` is **lowercase** `$abab…ab`, and that canonical form re-parses |
| `ED_01_B64` | `Ed25519([0x01; 32])`; `to_string() == "ed25519:" + ED_01_B64` and re-parses |
| `"rsa:" + RSA_AB_B64` | `Rsa([0xab; 20])`, but `to_string()` is the `$`-hex form |
| `"RSA:" + RSA_AB_B64`, `"ED25519:" + ED_01_B64` | algorithm names are case-insensitive |
| `"merkle-hellman:bestavoided"` | `RelayIdError::UnrecognizedIdType` |
| `RelayIdType::from_str("rsa")` / `("Ed25519")` | `Ok(Rsa)` / `Ok(Ed25519)`; `("ed448")` → `UnrecognizedIdType` |
| `"tooshort"` | `BadLength` (no colon, not 40 chars, no `$` ⇒ Ed25519 arm, 8 chars decodes short) |
| `"this_string_is_40_bytes_but_it_isnt_hex!"` | `BadHex` — 40 characters selects the RSA arm, then `hex::decode` fails |
| `"ed25519:" + RSA_AB_B64` | `BadLength` — right algorithm, 20-byte body |
| `"ed25519:" + ED_01_B64[..42] + "*"` (43 chars, illegal char) | `BadBase64` |

`from_type_and_bytes` checks the length against `id_type.id_len()` and returns
`BadLength` on mismatch: `(Rsa, &[0x01; 32])` and `(Ed25519, &[0xab; 20])` are
both errors, while `(Rsa, &[0xab; 20])` equals `RelayId::from_str(RSA_AB_HEX)`
and `RelayId::from(rsa_id(0xab))`.

### Errors

```rust
#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum RelayIdError {
    #[error("Unrecognized type for relay identity")] UnrecognizedIdType,
    #[error("Invalid base64 data")]                  BadBase64,
    #[error("Invalid hexadecimal data")]             BadHex,
    #[error("Invalid length for relay identity")]    BadLength,
}
impl From<base64ct::Error> for RelayIdError {
    // InvalidEncoding => BadBase64, InvalidLength => BadLength
}
```

The suite matches on the **variants** with `matches!`, so all four must exist
under these names; the `#[error]` strings are not asserted in this group (they
reach the driver only indirectly, via serde's `custom(e.to_string())`).

### Serde

`RelayId` serialises by delegating to `RelayIdRef`, which serialises
`self.to_string()`. So:

* `serde_json::to_string(&RelayId::from(ed_id(0x01)))` is `"\"ed25519:AQEB…\""`;
* `serde_json::to_string(&RelayId::from(rsa_id(0xab)))` is `"\"$abab…ab\""` — the `$` **is** included here, unlike `RsaIdentity`'s own `Serialize`, which emits bare hex (see §10);
* deserialisation is `<&str>::deserialize` then `str::parse`, with the error mapped through `D::Error::custom(e.to_string())`; `"\"not-an-identity\""` must fail.

Because deserialisation borrows `&str`, `RelayId` cannot be deserialised from a
format that does not support borrowed strings — irrelevant for `serde_json::from_str`,
which the suite uses throughout.

**Out of scope:** `Ord`/`PartialOrd` on `RelayIdType` (present upstream, never
asserted), `Hash` on `RelayId`, `RelayIdRef`'s `TryInto` derive, and
non-human-readable serde formats (upstream has `TODO`s about both).

## 8. Relay identity sets — tor-linkspec

Checked by `relayidset`. Source: `crates/tor-linkspec/src/ids/set.rs`,
re-exported as `tor_linkspec::RelayIdSet`. Note the re-export flattens the
module: `pub use ids::{set::RelayIdSet, ..}`, so `tor_linkspec::ids::set` is not
a public path.

### The type

```rust
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct RelayIdSet {
    ed25519: HashSet<Ed25519Identity>,
    rsa: HashSet<RsaIdentity>,
}
```

**Two hash sets, not one.** This is the structural fact the whole group checks:
each identity type gets its own slot, so membership of a relay's Ed25519 key
says nothing about its RSA key, `len()` counts *keys* (a relay known by both
identities contributes 2), and the insert/remove booleans report per-key
novelty, never per-relay novelty. Upstream's own comment says it would rather
use a single `HashSet` but cannot, because a flat set cannot be indexed by
`RelayIdRef`.

### Methods

```rust
impl RelayIdSet {
    pub fn new() -> Self;                                                   // == Self::default()
    pub fn insert<T: Into<RelayId>>(&mut self, key: T) -> bool;             // true if newly added
    pub fn remove<'a, T: Into<RelayIdRef<'a>>>(&mut self, key: T) -> bool;  // true if it was present
    pub fn contains<'a, T: Into<RelayIdRef<'a>>>(&self, key: T) -> bool;
    pub fn iter(&self) -> impl Iterator<Item = RelayIdRef<'_>>;             // ed25519 chained with rsa
    pub fn len(&self) -> usize;                                             // ed25519.len() + rsa.len()
    pub fn is_empty(&self) -> bool;
}
impl<ID: Into<RelayId>> Extend<ID> for RelayIdSet { .. }   // just insert()s each item
impl FromIterator<RelayId> for RelayIdSet { .. }
```

The generic bounds matter, because the driver passes four different argument
shapes and every one must resolve through the `From` impls in §7:

* `insert(ed_id(1))` — a bare `Ed25519Identity` (`From<Ed25519Identity> for RelayId`);
* `insert(RelayId::from(ed_id(1)))` — an owned `RelayId` (`impl<T> From<T> for T`);
* `contains(&ed_id(3))` and `contains(&owned_relay_id)` — `&Ed25519Identity` and `&RelayId`, via `From<&'a Ed25519Identity> for RelayIdRef<'a>` and `From<&'a RelayId> for RelayIdRef<'a>`;
* `contains(owned.as_ref())` — a `RelayIdRef` directly;
* `remove(&ed_id(1))` — `&Ed25519Identity`.

`Extend` is generic over `Into<RelayId>`, so `set.extend(vec![ed_id(1), ed_id(1), ed_id(2)])`
(bare keys) and `set.extend(vec![RelayId::from(..), ..])` both compile.
`FromIterator` is **only** over `RelayId`, which is what
`ids.into_iter().collect::<RelayIdSet>()` in the suite needs; a bare-key
`FromIterator` is not required.

### What is asserted, exactly

* Empty: `RelayIdSet::new()` has `is_empty()`, `len() == 0`, `iter().count() == 0`, contains neither `ed_id(1)` nor `rsa_id(1)`, and `== RelayIdSet::default()`.
* `insert(ed_id(1))` → `true`, `len() == 1`; the same insert again → `false`, `len()` still 1; `insert(RelayId::from(ed_id(1)))` → `false`.
* Independence: after `insert(ed_id(7))`, `contains(&ed_id(7))` but `!contains(&rsa_id(7))`; then `insert(rsa_id(7))` → `true` and `len() == 2`.
* `remove(&ed_id(1))` → `true` then `false`; removing an Ed25519 key leaves `rsa_id(2)` present; removing both empties the set.
* Iteration: after inserting `ed_id(1)`, `ed_id(2)`, `rsa_id(3)` and a duplicate `ed_id(1)`, `len() == 3`, `iter().count() == 3`, and the multiset of `r.to_string()` values (**sorted**, because iteration order is explicitly undefined) matches the three canonical strings from §7. `iter()` items are `RelayIdRef`s and `r.to_owned()` must give back the owned `RelayId`.
* `collect()` from a `Vec<RelayId>` with a repeat gives `len() == 2`; `extend` adds only genuinely-new keys; `Clone` + `PartialEq` are used to compare sets (`assert_eq!(set, set.clone())`, `assert_ne!(set, set2)`).

### Serde

```rust
impl serde::Serialize for RelayIdSet {
    fn serialize<S>(&self, s: S) -> Result<S::Ok, S::Error> { s.collect_seq(self.iter()) }
}
impl<'de> serde::Deserialize<'de> for RelayIdSet {
    // a SeqAccess visitor that insert()s each RelayId; expecting() = "a list of relay identities"
}
```

The wire form is a **flat JSON list of canonical identity strings** — not a map
keyed by algorithm, not two separate lists. The suite deserialises the output
both as `Vec<String>` (and compares the sorted strings against
`RelayId::to_string()` output, so `$`-hex for RSA and `ed25519:`-prefixed base64
for Ed25519) and back as a `RelayIdSet` (which must equal the original). A list
containing the same identity twice must deserialise to a set of length 1, and
`["bogus"]` must be an error — that error comes from `RelayId`'s
`Deserialize` (§7), so the two are linked.

**Out of scope:** iteration order, `expecting()`'s message, `Hash` (the set does
not implement it), and any set-algebra operations (there are none upstream).

## 9. Relay identity traits — tor-linkspec

Checked by `hasrelayids`; also load-bearing for `owntarget` (§10) and
`netdirids` (§11), which compare relays through these methods. Source:
`crates/tor-linkspec/src/traits.rs`, re-exported as
`tor_linkspec::{ChanTarget, CircTarget, HasAddrs, HasRelayIds, HasRelayIdsLegacy}`.
Driver imports `tor_linkspec::{HasRelayIds, HasRelayIdsLegacy, RelayId, RelayIdRef, RelayIdType}`
and `tor_llcrypto::pk::{ed25519::Ed25519Identity, rsa::RsaIdentity}`.

**What changed at this milestone.** This is the abstraction that lets the rest
of arti stop assuming every relay description carries exactly one Ed25519 key
and one RSA key. The driver defines its own two implementors — a `Legacy`
struct implementing only `HasRelayIdsLegacy`, and a `Partial` struct with
`Option`al keys implementing only `HasRelayIds::identity` — and checks the
**provided methods**. So the exact default-method bodies below are the
contract, not just the method names.

### The two traits

```rust
pub trait HasRelayIdsLegacy {
    fn ed_identity(&self) -> &pk::ed25519::Ed25519Identity;
    fn rsa_identity(&self) -> &pk::rsa::RsaIdentity;
}

pub trait HasRelayIds {
    fn identity(&self, key_type: RelayIdType) -> Option<RelayIdRef<'_>>;   // the only required method

    fn identities(&self) -> RelayIdIter<'_, Self> {
        RelayIdIter { info: self, next_key: RelayIdType::all_types() }
    }
    fn ed_identity(&self) -> Option<&pk::ed25519::Ed25519Identity> {
        self.identity(RelayIdType::Ed25519).map(RelayIdRef::unwrap_ed25519)
    }
    fn rsa_identity(&self) -> Option<&pk::rsa::RsaIdentity> {
        self.identity(RelayIdType::Rsa).map(RelayIdRef::unwrap_rsa)
    }
    fn has_identity(&self, id: RelayIdRef<'_>) -> bool {
        self.identity(id.id_type()).map(|my_id| my_id == id) == Some(true)
    }
    fn same_relay_ids<T: HasRelayIds + ?Sized>(&self, other: &T) -> bool {
        RelayIdType::all_types().all(|key_type| self.identity(key_type) == other.identity(key_type))
    }
    fn has_all_relay_ids_from<T: HasRelayIds + ?Sized>(&self, other: &T) -> bool {
        RelayIdType::all_types().all(|key_type| {
            match (self.identity(key_type), other.identity(key_type)) {
                (Some(mine), Some(theirs)) if mine == theirs => true,
                (_, Some(_theirs)) => false,
                (_, None) => true,
            }
        })
    }
}

impl<T: HasRelayIdsLegacy> HasRelayIds for T {
    fn identity(&self, key_type: RelayIdType) -> Option<RelayIdRef<'_>> {
        match key_type {
            RelayIdType::Rsa => Some(self.rsa_identity().into()),
            RelayIdType::Ed25519 => Some(self.ed_identity().into()),
        }
    }
}
```

Two consequences a solver must preserve:

1. **The blanket impl.** A type implementing only `HasRelayIdsLegacy` gets the
   whole `HasRelayIds` surface, and reports *both* identities present:
   `l.identities().count() == RelayIdType::COUNT`. Because of the blanket impl
   the legacy trait's `ed_identity()` (returning `&`) and the modern trait's
   `ed_identity()` (returning `Option<&>`) coexist on the same type; the driver
   only calls the `Option` forms on its `Partial` type, so no disambiguation is
   needed in the suite, but both traits must be importable by name.
2. **The two comparison methods are not interchangeable**, and upstream
   deliberately does not make either one `Eq`. `same_relay_ids` is symmetric
   equality over *every* type in `all_types()`, absent slots included
   (`None == None` is true, so two objects missing the same identity are "the
   same"). `has_all_relay_ids_from(other)` is the asymmetric "does `self` carry
   everything `other` demands": a `None` on `other`'s side always passes, a
   `Some` on `other`'s side must be matched exactly on `self`'s side.

### The `RelayIdIter` iterator

```rust
#[derive(Clone)]
pub struct RelayIdIter<'a, T: HasRelayIds + ?Sized> { info: &'a T, next_key: RelayIdTypeIter }
impl<'a, T: HasRelayIds + ?Sized> Iterator for RelayIdIter<'a, T> {
    type Item = RelayIdRef<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        for key_type in &mut self.next_key {
            if let Some(key) = self.info.identity(key_type) { return Some(key); }
        }
        None
    }
}
impl<'a, T: HasRelayIds + ?Sized> FusedIterator for RelayIdIter<'a, T> {}
```

`RelayIdIter` is `pub` in the private `traits` module and is **not**
re-exported from `lib.rs` — it is unnameable from the driver, which only ever
calls `.count()`, `.map()`, `.next()` on it. Iteration order is
`RelayIdType::all_types()` order, i.e. **Ed25519 first, then RSA** (§7); absent
types are skipped, not yielded as a placeholder; and once exhausted `next()`
keeps returning `None` (the suite calls `next()` a third time on a one-element
iterator and requires `None`).

### What is asserted, exactly

* `Legacy{ed_id(1), rsa_id(2)}.identity(Ed25519).map(to_owned) == Some(RelayId::from(ed_id(1)))`, and the RSA analogue; `identities().count() == 2`.
* `Legacy{ed_id(3), rsa_id(4)}.identities().map(to_owned).collect::<Vec<_>>() == vec![RelayId::from(ed_id(3)), RelayId::from(rsa_id(4))]` — order checked.
* `Partial{ed: None, rsa: Some(rsa_id(5))}.identities()` yields exactly `[RelayId::from(rsa_id(5))]`; `Partial{ed: Some(ed_id(6)), rsa: None}` yields one item of type `Ed25519`.
* `ed_identity()`/`rsa_identity()` on `Partial` return `Some(&ed_id(7))`/`Some(&rsa_id(8))`, or `None` when absent — the `Option<&Ed25519Identity>` return type is compared with `assert_eq!` against `Some(&ed_id(7))`, so it must be a reference to the key type, not a `RelayIdRef`.
* `has_identity(RelayIdRef::from(&key))` is true for a matching key of the same type, false for a different key of the same type and false for an absent type.
* `same_relay_ids`: `both(1,2)` vs `both(1,2)` true both ways; vs `both(3,2)` false; vs `{ed: Some(1), rsa: None}` false both ways; `{Some(1), None}` vs `{Some(1), None}` true; `both(1,2)` vs `Legacy{1,2}` true (cross-type, via the generic `T: HasRelayIds + ?Sized` parameter).
* `has_all_relay_ids_from`: `full.has_all_relay_ids_from(&ed_only)` true, reverse false; `full` vs `{ed: Some(0xaa), rsa: None}` false in both directions; equal objects true in both directions.
* Empty object `{None, None}`: `identities().count() == 0`; both `identity(..)` calls `None`; `full.has_all_relay_ids_from(&empty)` true, `empty.has_all_relay_ids_from(&empty)` true, `empty.has_all_relay_ids_from(&full)` false; `empty.same_relay_ids(&full)` false, `empty.same_relay_ids(&empty2)` true.

The driver's `Partial::identity` has a `_ => None` arm after matching both
variants, which is the idiom `#[non_exhaustive]` on `RelayIdType` requires
(§7); if `RelayIdType` were exhaustive the arm would only be an
unreachable-pattern warning, and the driver does not deny warnings.

### The other traits in this file

```rust
pub trait HasAddrs { fn addrs(&self) -> &[SocketAddr]; }
pub trait ChanTarget: HasRelayIds + HasAddrs {}
pub trait CircTarget: ChanTarget {
    fn linkspecs(&self) -> Vec<crate::LinkSpec> {
        let mut result: Vec<_> = self.identities().map(|id| id.to_owned().into()).collect();
        for addr in self.addrs().iter() { result.push(addr.into()); }
        result
    }
    fn ntor_onion_key(&self) -> &pk::curve25519::PublicKey;
    fn protovers(&self) -> &tor_protover::Protocols;
}
```

`HasAddrs` and `CircTarget::linkspecs` are checked through §10, not here.
`RelayIdRef::unwrap_ed25519`/`unwrap_rsa` are `pub(crate)` and panic on the
wrong variant; they are an implementation detail of the two typed accessors
and are not reachable from the driver.

**Out of scope:** `RelayIdIter`'s `Clone`, `HasRelayIdsLegacy` on any upstream
type (the driver supplies its own implementor), and `ChanTarget`/`CircTarget`
being implemented by anything other than `OwnedChanTarget`/`OwnedCircTarget`
(§10) and `tor_netdir::Relay` (§11).

## 10. Owned relay targets — tor-linkspec

Checked by `owntarget`. Source: `crates/tor-linkspec/src/owned.rs`
(plus `ls.rs` for `LinkSpec`), re-exported as
`tor_linkspec::{OwnedChanTarget, OwnedCircTarget, RelayIds}` and
`tor_linkspec::LinkSpec`. Driver imports:
`tor_linkspec::{ChanTarget, CircTarget, HasAddrs, HasRelayIds, LinkSpec, OwnedChanTarget, OwnedCircTarget, RelayId, RelayIdRef, RelayIdType, RelayIds}`
and `tor_llcrypto::pk::{curve25519, ed25519::Ed25519Identity, rsa::RsaIdentity}`.
`hex` is a dev-dependency of the driver and is used here.

### The `RelayIds` struct

```rust
#[derive(Debug, Clone, Eq, PartialEq, Hash, PartialOrd, Ord, Serialize, Deserialize)]
pub struct RelayIds {
    #[serde(rename = "ed25519")]
    ed_identity: Option<pk::ed25519::Ed25519Identity>,
    #[serde(rename = "rsa")]
    rsa_identity: Option<pk::rsa::RsaIdentity>,
}
impl HasRelayIds for RelayIds { /* maps each type to its Option field */ }
impl RelayIds {
    pub fn new(ed_identity: Ed25519Identity, rsa_identity: RsaIdentity) -> Self;  // both Some
    pub fn from_relay_ids<T: HasRelayIds + ?Sized>(other: &T) -> Self;            // copies Options
}
```

Both fields are `Option`, both renamed. The serialised shape is therefore a map
with exactly the keys `ed25519` and `rsa`, and a missing identity is JSON
`null` — that is the whole point: "we do not know this relay's RSA identity"
must survive a round trip. `from_relay_ids` can legitimately produce an
**empty** `RelayIds`.

What is asserted: `RelayIds::new(ed_id(1), rsa_id(2))` has
`ed_identity() == Some(&ed_id(1))` and `rsa_identity() == Some(&rsa_id(2))`
(the §9 provided methods); `serde_json::to_value(&ids)["rsa"] == json!(hex::encode([2u8; 20]))`,
i.e. **bare lowercase hex with no `$`**; `["ed25519"].is_string()`; the value
deserialises back `==` to the original. For a partial source,
`partial.rsa_identity() == None`, `partial != ids`, `to_value(&partial)["rsa"] == Value::Null`,
and that round-trips.

> **The `$` is a `Display`-only artefact.** `RsaIdentity`'s `Display` writes
> `"${}"` with the hex, but its `Serialize` (human-readable branch) writes
> `serializer.serialize_str(&hex::encode(&self.id[..]))` — bare hex.
> `Ed25519Identity`'s `Serialize` writes
> `base64::encode_config(self.id, base64::STANDARD_NO_PAD)`. So `RelayIds`'
> JSON carries the *key* encodings, while §7's `RelayId` JSON carries the
> *canonical identity strings* (`$`-hex, `ed25519:`-base64). Both forms are
> checked, in different groups; do not unify them.

### The `OwnedChanTarget` struct

```rust
#[derive(Debug, Clone)]
pub struct OwnedChanTarget { addrs: Vec<SocketAddr>, ids: RelayIds }

impl HasAddrs      for OwnedChanTarget { fn addrs(&self) -> &[SocketAddr] { &self.addrs[..] } }
impl HasRelayIds   for OwnedChanTarget { fn identity(&self, t: RelayIdType) -> Option<RelayIdRef<'_>> { self.ids.identity(t) } }
impl ChanTarget    for OwnedChanTarget {}

impl OwnedChanTarget {
    pub fn new(addrs: Vec<SocketAddr>, ed_identity: Ed25519Identity, rsa_identity: RsaIdentity) -> Self;
    pub fn from_chan_target<C>(target: &C) -> Self where C: ChanTarget + ?Sized;
    pub fn restrict_addr(&self, addr: &SocketAddr) -> Result<Self, Self>;
}
```

`OwnedChanTarget` derives **only `Debug, Clone`** — no `PartialEq`. That is why
every comparison in the group goes through `same_relay_ids(..)` and `addrs()`
rather than `assert_eq!` on whole targets. Do not "helpfully" add `PartialEq`
expecting the suite to use it; nothing requires it, and `assert_eq!` on targets
does not appear.

`new` stores the address vector **in order** and wraps both keys in
`RelayIds::new`. `from_chan_target` copies `target.addrs().to_vec()` and
`RelayIds::from_relay_ids(target)`, so a source with a missing identity yields a
target with that identity still missing (`identities().count() == 1`) — an
implementation that substituted an all-zero key would make an
unauthenticated relay look fully identified.

`restrict_addr` is `Result<Self, Self>` where **both arms are meaningful**:
`Ok` is a clone narrowed to the single requested address, `Err` is an untouched
clone of the original. Both keep the identities. Asserted: narrowing to
`127.0.0.2:22` gives `addrs() == [127.0.0.2:22]` and `same_relay_ids` with the
original; narrowing to `127.0.0.3:33` gives `Err` whose `addrs()` equals the
original's and which still `same_relay_ids`; re-narrowing the narrowed target to
its own single address is `Ok`, to the other address is `Err`.

### Rendering (`Display`) — the exact log form

```rust
impl Display for OwnedChanTarget {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "[")?;
        match &*self.addrs {
            []      => write!(f, "?")?,
            [a]     => write!(f, "{}", a)?,
            [a, ..] => write!(f, "{}+", a)?,
        };
        for ident in self.identities() { write!(f, " {}", ident)?; }
        write!(f, "]")?;
        Ok(())
    }
}
impl Display for OwnedCircTarget { /* delegates to self.chan_target */ }
```

With `ids = " ed25519:<b64> $<hex>"` (identity order from §9, each rendered by
`RelayIdRef`'s `Display` from §7, each preceded by one space):

| Addresses | `to_string()` |
| --- | --- |
| none | `[?<ids>]` |
| exactly one | `[127.0.0.1:11<ids>]` |
| two or more | `[127.0.0.1:11+<ids>]` — first address only, trailing `+` |

`OwnedCircTarget::to_string()` must equal its channel target's.

### The `OwnedCircTarget` struct

```rust
#[derive(Debug, Clone)]
pub struct OwnedCircTarget {
    chan_target: OwnedChanTarget,
    ntor_onion_key: pk::curve25519::PublicKey,
    protovers: tor_protover::Protocols,
}
impl OwnedCircTarget {
    pub fn new(chan_target: OwnedChanTarget, ntor_onion_key: pk::curve25519::PublicKey,
               protovers: tor_protover::Protocols) -> OwnedCircTarget;
    pub fn from_circ_target<C>(target: &C) -> Self where C: CircTarget + ?Sized;
}
impl HasAddrs    for OwnedCircTarget { /* delegates */ }
impl HasRelayIds for OwnedCircTarget { /* delegates */ }
impl ChanTarget  for OwnedCircTarget {}
impl CircTarget  for OwnedCircTarget {
    fn ntor_onion_key(&self) -> &pk::curve25519::PublicKey { &self.ntor_onion_key }
    fn protovers(&self) -> &tor_protover::Protocols { &self.protovers }
}
```

Asserted: `ct.ntor_onion_key().as_bytes() == &[99u8; 32]` (the driver builds the
key with `curve25519::PublicKey::from([99u8; 32])`, so the `From<[u8; 32]>` and
`as_bytes()` of the re-exported `x25519_dalek` type must be reachable through
`tor_llcrypto::pk::curve25519`); `ct.protovers().to_string() == "FlowCtrl=7"`;
addresses and identities forwarded from the channel target; `to_string()` equal
to the channel target's. `from_circ_target` must carry all four pieces
(it clones the protovers and copies the key).

### Link specifiers (`CircTarget::linkspecs()`) — ordering, and a deliberate divergence

```rust
fn linkspecs(&self) -> Vec<crate::LinkSpec> {
    let mut result: Vec<_> = self.identities().map(|id| id.to_owned().into()).collect();
    for addr in self.addrs().iter() { result.push(addr.into()); }
    result
}
```

```rust
#[non_exhaustive]
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LinkSpec {
    OrPort(IpAddr, u16),
    RsaId(RsaIdentity),
    Ed25519Id(ed25519::Ed25519Identity),
    Unrecognized(u8, Vec<u8>),
}
impl From<&SocketAddr> for LinkSpec { /* OrPort(sa.ip(), sa.port()) */ }
impl From<RelayId>     for LinkSpec { /* Ed25519(k) => Ed25519Id(k), Rsa(k) => RsaId(k) */ }
```

For a target with addresses `127.0.0.1:99`, `[::1]:909` and both identities,
`linkspecs()` must be exactly four elements in this order:

1. `LinkSpec::Ed25519Id(ed_id(1))`
2. `LinkSpec::RsaId(rsa_id(2))`
3. `LinkSpec::OrPort("127.0.0.1".parse().unwrap(), 99)`
4. `LinkSpec::OrPort("::1".parse().unwrap(), 909)`

`LinkSpec` is `PartialEq`, so these are compared directly with `assert_eq!`.
Note `OrPort` holds an `IpAddr` and a `u16`, not a `SocketAddr`, and the IPv6
address is unbracketed inside it.

> **The suite checks arti, not torspec, here.** `tor-spec.txt` §5.1.2 recommends
> that an EXTEND2 cell's link specifiers be emitted in numeric type order —
> `00` (IPv4 ORPort), `02` (RSA ID), `03` (Ed25519 ID), `01` (IPv6 ORPort) —
> which would put an IPv4 address *first*. arti v1.0.0's default `linkspecs()`
> emits all identities first and then the addresses, and this group asserts
> arti's order. This is a real, known divergence (recorded in `SPEC_NOTES.md`
> §8); implement arti's order or the group fails.

**Out of scope:** `LinkSpec::Unrecognized`, `LinkSpec`'s encode/decode
(`LSTYPE_ORPORT_V4 = 0`, `LSTYPE_ORPORT_V6 = 1`, `LSTYPE_RSAID = 2`,
`LSTYPE_ED25519ID = 3` and the `Readable`/`Writeable` impls exist upstream and
are untouched by this group), `RelayIds`' `Hash`/`Ord`, and
`OwnedChanTarget`'s `Debug` output text.

## 11. Directory lookups by identity — tor-netdir

Checked by `netdirids`. Source: `crates/tor-netdir/src/lib.rs`
(`NetDir`, `PartialNetDir`, `Relay`, `UncheckedRelay`) and
`crates/tor-netdir/src/testnet.rs` (the synthetic network, gated by the
`testing` feature — see the cargo-feature table). Driver imports:
`tor_netdir::{testnet, NetDir}`, `tor_linkspec::{HasRelayIds, RelayIdRef, RelayIdType, RelayIds}`,
and the two key types from `tor_llcrypto::pk`.

**What changed at this milestone.** Before v1.0.0 a lookup took a concrete key
type (`by_id(&Ed25519Identity)`, `by_rsa_id`, `by_id_pair`, `id_pair_listed`);
now `by_id` takes anything convertible to a `RelayIdRef` and `by_ids`/`ids_listed`
take any `HasRelayIds`. The old concrete-key helpers survive only as private
functions.

### The test network the driver builds

```rust
// tor_netdir::testnet
pub fn construct_netdir() -> PartialNetDir;
pub fn construct_custom_netdir<F>(func: F) -> BuildResult<PartialNetDir>
where F: FnMut(usize, &mut NodeBuilders);

#[derive(Debug, Clone)] #[non_exhaustive]
pub struct NodeBuilders {
    pub rs: RouterStatusBuilder<MdDigest>,
    pub md: MicrodescBuilder,
    pub omit_md: bool,   // true => no microdescriptor for this relay
    pub omit_rs: bool,
}

// tor_netdir
impl PartialNetDir {
    pub fn unwrap_if_sufficient(self) -> std::result::Result<NetDir, PartialNetDir>;
}
```

`construct_netdir()` builds 40 relays numbered 0..=39; relay `i` has Ed25519
identity `[i; 32]` and RSA identity `[i; 20]` (so the driver's `ed_id(5)` /
`rsa_id(5)` from `common/mod.rs` name relay 5). Every relay has a microdesc and
none carries `NO_ED_CONSENSUS`, so all 40 are usable and
`unwrap_if_sufficient()` is `Ok`. The driver's second network sets
`nb.omit_md = true` for one index via `construct_custom_netdir`, producing a
directory where that relay is *listed* in the consensus but has no
microdescriptor; it must still have enough paths (39 usable relays) for
`unwrap_if_sufficient()` to be `Ok`. `BuildResult` is `tor_netdoc::BuildResult`,
re-exported from `testnet`; the driver only `.expect()`s it.

### The lookup API

```rust
impl NetDir {
    pub fn all_relays(&self) -> impl Iterator<Item = UncheckedRelay<'_>>;   // every consensus entry
    pub fn relays(&self) -> impl Iterator<Item = Relay<'_>>;                // usable ones only

    pub fn by_id<'a, T>(&self, id: T) -> Option<Relay<'_>>
    where T: Into<RelayIdRef<'a>> + ?Sized;

    pub fn by_ids<T>(&self, target: &T) -> Option<Relay<'_>>
    where T: HasRelayIds + ?Sized;

    pub fn ids_listed<T>(&self, target: &T) -> Option<bool>
    where T: HasRelayIds + ?Sized;
}

#[derive(Clone)]
pub struct Relay<'a> { rs: &'a MdConsensusRouterStatus, md: &'a Microdesc }
impl<'a> Relay<'a> {
    pub fn id(&self) -> &Ed25519Identity;        // from the microdesc
    pub fn rsa_id(&self) -> &RsaIdentity;        // from the routerstatus
    pub fn same_relay<'b>(&self, other: &Relay<'b>) -> bool;   // id() == && rsa_id() ==
}
impl<'a> tor_linkspec::HasRelayIdsLegacy for Relay<'a> { /* ed_identity = id(), rsa_identity = rsa_id() */ }

#[derive(Debug)]
pub struct UncheckedRelay<'a> { rs: &'a MdConsensusRouterStatus, md: Option<&'a Microdesc> }
impl<'a> UncheckedRelay<'a> {
    pub fn is_usable(&self) -> bool { self.md.is_some() && self.rs.ed25519_id_is_usable() }
    pub fn into_relay(self) -> Option<Relay<'a>>;
}
impl<'a> HasRelayIds for UncheckedRelay<'a> {
    fn identity(&self, key_type: RelayIdType) -> Option<RelayIdRef<'_>> {
        match key_type {
            RelayIdType::Ed25519 if self.rs.ed25519_id_is_usable() => self.md.map(|m| m.ed25519_id().into()),
            RelayIdType::Rsa => Some(self.rs.rsa_identity().into()),
            _ => None,
        }
    }
}
```

`Relay` gets its `HasRelayIds` surface (including `same_relay_ids`, used by the
driver) through the §9 blanket impl over `HasRelayIdsLegacy`. `UncheckedRelay`
implements `HasRelayIds` directly so that a relay without a microdesc reports
**no** Ed25519 identity but still reports its RSA identity; the driver asserts
`unusable[0].rsa_identity() == Some(&rsa_id(3))` on exactly such a relay.

### Lookup by one identity (`by_id`) — accepts either identity, returns only usable relays

```rust
let id = id.into();
let answer = match id {
    RelayIdRef::Ed25519(ed25519) => {
        let rs_idx = *self.rs_idx_by_ed.get(ed25519)?;
        let rs = self.consensus.relays().get(rs_idx).expect("Corrupt index");
        self.relay_from_rs_and_idx(rs, rs_idx).into_relay()?
    }
    RelayIdRef::Rsa(rsa) => self.by_rsa_id_unchecked(rsa).and_then(UncheckedRelay::into_relay)?,
    other_type => self.relays().find(|r| r.has_identity(other_type))?,
};
assert!(answer.has_identity(id));
Some(answer)
```

The driver passes `&ed_id(5)`, `&rsa_id(5)` and a `RelayIdRef` obtained from
`RelayIds::identity(RelayIdType::Ed25519).unwrap()`, so the `Into<RelayIdRef<'a>>`
bound must be satisfied by `&Ed25519Identity`, `&RsaIdentity` and `RelayIdRef`
itself (§7's `From` impls). Asserted: `by_id(&ed_id(5)).id() == &ed_id(5)`,
`.rsa_id() == &rsa_id(5)`, and the Ed25519 and RSA lookups reach the same relay
(`same_relay` and `same_relay_ids` both true); `by_id(&ed_id(200))`,
`by_id(&rsa_id(200))` and `by_id(&ed_id(40))` are `None` while `by_id(&ed_id(39))`
is `Some`. On the network missing relay 3's microdesc, `by_id(&ed_id(3))` and
`by_id(&rsa_id(3))` are both `None` (unusable is not returned), `by_id(&ed_id(4))`
is `Some`, `all_relays().count() == 40`, `relays().count() == 39`, and exactly
one `UncheckedRelay` has `!is_usable()`.

Upstream keeps two identity indexes, `rs_idx_by_ed` and `rs_idx_by_rsa`; an
implementation that scanned linearly would pass this group but the `rs_idx_by_ed`
map is also what `ids_listed` consults for the Ed25519-only case below, so the
index must at least exist logically.

### Lookup by all identities (`by_ids`) — every requested identity must match

```rust
let mut identities = target.identities();
let first_id = identities.next()?;                    // no identities => None
let candidate = self.by_id(first_id)?;
if identities.all(|wanted_id| candidate.has_identity(wanted_id)) { Some(candidate) } else { None }
```

Asserted: `by_ids(&RelayIds::new(ed_id(6), rsa_id(6)))` finds relay 6;
`by_ids(&RelayIds::new(ed_id(6), rsa_id(7)))` is `None` (a half match is not a
match); a `RelayIds` with only the Ed25519 identity set (built with
`RelayIds::from_relay_ids(&relay)` — note that from a `Relay` this actually
copies **both** identities, since `Relay` is `HasRelayIdsLegacy`; the test
comment says "only one identity" but either way the lookup succeeds) is `Some`;
and `by_ids` of an empty `RelayIds` (from a driver-defined `EmptyIds` whose
`identity()` always returns `None`) is `None`.

### Listing check (`ids_listed`) — tri-state, and the exact decision table

```rust
let rsa_id = target.rsa_identity();
let ed25519_id = target.ed_identity();
const_assert!(RelayIdType::COUNT == 2);
match (rsa_id, ed25519_id) {
    (Some(r), Some(e)) => self.id_pair_listed(e, r),
    (Some(r), None)    => Some(self.rsa_id_is_listed(r)),
    (None, Some(e))    => if self.rs_idx_by_ed.contains_key(e) { Some(true) } else { None },
    (None, None)       => None,
}

fn id_pair_listed(&self, ed_id: &Ed25519Identity, rsa_id: &RsaIdentity) -> Option<bool> {
    match self.by_rsa_id_unchecked(rsa_id) {
        Some(unchecked) => {
            if !unchecked.rs.ed25519_id_is_usable() { return Some(false); }
            unchecked.md.map(|md| md.ed25519_id() == ed_id)   // None if no microdesc yet
        }
        None => Some(false),
    }
}
```

`None` means "cannot tell yet" and must never be conflated with `Some(false)`:
a client that did so would conclude a guard or bridge had been removed from the
network when its microdescriptor merely has not been downloaded. The asserted
table, on the full network unless stated:

| Request | Result | Why |
| --- | --- | --- |
| `RelayIds::new(ed_id(8), rsa_id(8))` | `Some(true)` | RSA found, md present, ed matches |
| `RelayIds::new(ed_id(8), rsa_id(9))` | `Some(false)` | RSA 9 found, md present, ed differs |
| `RelayIds::new(ed_id(200), rsa_id(200))` | `Some(false)` | RSA not in consensus: durable no |
| `EmptyIds` | `None` | nothing was asked about |
| `RelayIds::new(ed_id(3), rsa_id(3))` on the network missing md 3 | `None` | listed, but ed cannot be confirmed |
| `RelayIds::new(ed_id(4), rsa_id(4))` on that same network | `Some(true)` | |
| RSA-only `rsa_id(10)` / `rsa_id(200)` | `Some(true)` / `Some(false)` | consensus is indexed by RSA |
| Ed25519-only `ed_id(10)` | `Some(true)` | present in `rs_idx_by_ed` |
| Ed25519-only `ed_id(200)` | `None` | a microdesc might still introduce it |

The single-identity requests are built with `RelayIds::from_relay_ids(&OneId::..)`,
where `OneId` is a driver-defined enum implementing `HasRelayIds` for exactly
one type, so `from_relay_ids` must faithfully leave the other field `None`
(§10).

**Out of scope:** `NetDir::params()`, weights and path selection (`pick_relay`,
`have_enough_paths` beyond it being true for these networks), `MdReceiver`,
`by_rsa_id`/`by_rsa_id_unchecked`/`rsa_id_is_listed` as public API (they are
private upstream), `Relay`'s exit-policy and `CircTarget` methods, and
`ids_listed` on a relay flagged `NO_ED_CONSENSUS` (the test network never sets
that flag).

## 12. Network parameters and the parameters provider — tor-netdir

Checked by `dirparams`. Source: `crates/tor-netdir/src/params.rs`
(the `NetParameters` block) and `crates/tor-netdir/src/lib.rs` (the provider
trait, `DirEvent`, `Timeliness`, `AsRef<NetParameters> for NetDir`). Driver
imports: `tor_netdir::{params::{NetParameters, CHANNEL_PADDING_TIMEOUT_UPPER_BOUND}, testnet, DirEvent, NetDir, NetDirProvider, Timeliness, UpcastArcNetDirProvider}`
plus `tor_netdir::Result` and `tor_netdir::Error::DirExpired`. Every `NetDir`
in this group comes from
`testnet::construct_custom_netdir_with_params(|_idx, _nb| {}, params.iter().copied())`
followed by `.unwrap_if_sufficient()` (see the `testing` feature note at the top
of this document and §11).

### `NetParameters` — the fields the suite reads

```rust
pub const CHANNEL_PADDING_TIMEOUT_UPPER_BOUND: i32 = 60_000;

#[derive(Clone, Debug)]
#[non_exhaustive]
pub struct NetParameters {
    pub circuit_window: BoundedInt32<100, 1000>,                       // = 1_000  from "circwindow"
    pub extend_by_ed25519_id: BoundedInt32<0, 1>,                      // = 0      from "ExtendByEd25519ID"
    pub cbt_timeout_quantile: Percentage<BoundedInt32<10, 99>>,        // = 80     from "cbtquantile"
    pub nf_ito_low:          IntegerMilliseconds<BoundedInt32<0, CHANNEL_PADDING_TIMEOUT_UPPER_BOUND>>, // = 1500  from "nf_ito_low"
    pub nf_ito_high:         IntegerMilliseconds<BoundedInt32<0, CHANNEL_PADDING_TIMEOUT_UPPER_BOUND>>, // = 9500  from "nf_ito_high"
    pub nf_ito_low_reduced:  IntegerMilliseconds<BoundedInt32<0, CHANNEL_PADDING_TIMEOUT_UPPER_BOUND>>, // = 9000  from "nf_ito_low_reduced"
    pub nf_ito_high_reduced: IntegerMilliseconds<BoundedInt32<0, CHANNEL_PADDING_TIMEOUT_UPPER_BOUND>>, // = 14000 from "nf_ito_high_reduced"
    // ... the remaining upstream fields (see §13 for the cc_* / sendme_* ones)
}
impl Default for NetParameters { /* every field at its documented default */ }
impl AsRef<NetParameters> for NetParameters { fn as_ref(&self) -> &NetParameters { self } }
impl AsRef<NetParameters> for NetDir       { fn as_ref(&self) -> &NetParameters { self.params() } }
impl NetDir { pub fn params(&self) -> &NetParameters; }
```

The four `nf_ito_*` fields are checked here as a group: they carry the padding
timings the netflow-defence work turns on, and the `CHANNEL_PADDING_TIMEOUT_UPPER_BOUND`
constant that bounds all four is lifted from C Tor. (Provenance is not the point
and is not asserted — `nf_ito_low` and its bound already exist at the previous
checkpoint, which is why the field is readable at all.)
Field *names* are checked — the driver reads `p.nf_ito_low`, `p.circuit_window`,
etc. directly — and so are the wrapper types: `IntegerMilliseconds<..>::as_millis()`
and `Percentage<..>::as_percent()` both return the `BoundedInt32`, whose `get()`
yields `i32`. All of these come from `tor_units`.

### Exactly asserted values

| Field | Consensus key | Default | Range | Asserted |
| --- | --- | --- | --- | --- |
| `nf_ito_low` | `nf_ito_low` | 1500 | 0..=60_000 | `100` from consensus; `-5` → `0`; `i32::MAX` → `60_000`; `1234` from consensus |
| `nf_ito_high` | `nf_ito_high` | 9500 | 0..=60_000 | `200` from consensus; `1_000_000` → `60_000`; `i32::MIN` → `0` |
| `nf_ito_low_reduced` | `nf_ito_low_reduced` | 9000 | 0..=60_000 | default only |
| `nf_ito_high_reduced` | `nf_ito_high_reduced` | 14000 | 0..=60_000 | default only (and default preserved when other keys are set) |
| `circuit_window` | `circwindow` | 1000 | 100..=1000 | `500` from consensus; `1` → `100` |
| `extend_by_ed25519_id` | `ExtendByEd25519ID` | 0 | 0..=1 | default only |
| `cbt_timeout_quantile` | `cbtquantile` | 80 | 10..=99 | `1000` → `99` |
| `CHANNEL_PADDING_TIMEOUT_UPPER_BOUND` | — | `60_000` (`i32`) | — | `assert_eq!(.., 60_000)` |

Out-of-range values **saturate** (`BoundedInt32::saturating_new`); they are
never rejected, wrapped, or truncated. Consensus keys are matched **exactly**:
`"NF_ITO_LOW"` and `"a-parameter-from-the-future"` are silently ignored and
must not disturb the recognised keys or make `construct_custom_netdir_with_params`
fail. Upstream implements this via `set_saturating(&mut self, key: &str, val: i32) -> bool`
and the `FromInt32Saturating` trait (`params.rs`); neither is checked directly.

### `NetDirProvider` and friends

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum DirEvent { NewConsensus, NewDescriptors }

#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum Timeliness { Strict, Timely, Unchecked }

pub trait NetDirProvider: UpcastArcNetDirProvider + Send + Sync {
    fn netdir(&self, timeliness: Timeliness) -> Result<Arc<NetDir>>;
    fn timely_netdir(&self) -> Result<Arc<NetDir>> { self.netdir(Timeliness::Timely) }
    fn events(&self) -> BoxStream<'static, DirEvent>;          // futures::stream::BoxStream
    fn params(&self) -> Arc<dyn AsRef<NetParameters>>;
}
impl<T: NetDirProvider> NetDirProvider for Arc<T> { /* forwards all four methods */ }

pub trait UpcastArcNetDirProvider {
    fn upcast_arc<'a>(self: Arc<Self>) -> Arc<dyn NetDirProvider + 'a> where Self: 'a;
}
impl<T: NetDirProvider + Sized> UpcastArcNetDirProvider for T { /* returns self */ }

pub type Result<T> = std::result::Result<T, Error>;
pub enum Error { /* ... */ DirExpired, /* ... */ }   // #[derive(Error, Clone, Debug)]
```

The driver implements `NetDirProvider` on its own struct with **only** `netdir`,
`events` and `params` — so `timely_netdir` must have a default body, and the
blanket `UpcastArcNetDirProvider` impl must exist (the driver does not implement
it by hand). It then asserts:

* `provider.netdir(Timeliness::Strict).is_err()`, `netdir(Timeliness::Unchecked).is_ok()`,
  and `provider.timely_netdir().is_ok()` — the driver's `netdir` refuses only
  `Strict`, so `timely_netdir` **must** pass `Timeliness::Timely`.
* `&Arc<Provider>` coerces to `&dyn NetDirProvider` and `timely_netdir` works through it.
* `Arc::clone(&provider).upcast_arc()` yields `Arc<dyn NetDirProvider>`; its
  `params().as_ref().as_ref().nf_ito_high.as_millis().get() == 9500`.
* `upcast.events().collect::<Vec<_>>()` (via `futures::executor::block_on`) equals
  `vec![DirEvent::NewConsensus, DirEvent::NewDescriptors]` — `DirEvent: PartialEq + Debug`.
* `Arc<NetDir>` coerces to `Arc<dyn AsRef<NetParameters>>` and stays usable after
  the provider is dropped; `dir.as_ref().as_ref()` on an `Arc<NetDir>` yields
  `&NetParameters`.

**Out of scope here:** `NetParameters::from_map`, the `FromInt32Saturating` trait,
`DirEvent`'s `#[non_exhaustive]`-ness, `Timeliness` semantics inside a real
`DirMgr`, `NetDir::params` beyond the fields in the table, `Error`'s `Display`
text or `HasKind` mapping, and every other `NetParameters` field (the `cc_*` and
`sendme_*` fields are §13; guard/cbt fields other than `cbt_timeout_quantile`
are untouched).

## 13. Congestion-control parameters — tor-netdir

Checked by `ccparams`. Source: `crates/tor-netdir/src/params.rs`, the
same `declare_net_parameters!` block as §12. Driver imports:
`tor_netdir::{params::NetParameters, testnet, NetDir}`; every `NetDir` is built
with `testnet::construct_custom_netdir_with_params(|_idx, _nb| {}, params.iter().copied())`
and `.unwrap_if_sufficient()`, and read through `NetDir::params()`.

The five `cc_*` fields are new at v1.0.0. The two `sendme_*` fields predate it
but are checked alongside because they share the "single byte on the wire"
clamping story.

### Fields, keys, defaults, ranges

```rust
pub struct NetParameters {
    // ...
    pub sendme_accept_min_version: SendMeVersion,                      // = 0  from "sendme_accept_min_version"
    pub sendme_emit_min_version:   SendMeVersion,                      // = 0  from "sendme_emit_min_version"
    pub cc_min_sendme_acks:        BoundedInt32<2, 20>,                // = 5  from "cc_bwe_min"
    pub cc_ewma_n_by_sendme_acks:  Percentage<BoundedInt32<1, 255>>,   // = 50 from "cc_ewma_cwnd_pct"
    pub cc_ewma_n_max:             BoundedInt32<2, { i32::MAX }>,      // = 10 from "cc_ewma_max"
    pub cc_sendme_cell_ack_count:  BoundedInt32<1, 255>,               // = 31 from "cc_sendme_inc"
    pub cc_cwnd_inc_rate:          BoundedInt32<1, 250>,               // = 1  from "cc_cwnd_inc_rate"
    // ...
}
```

Supporting `tor_units` API the driver calls (all pre-existing):

```rust
impl<const L: i32, const U: i32> BoundedInt32<L, U> { pub fn get(&self) -> i32; pub fn saturating_new(val: i32) -> Self; }
impl<T: Copy + Into<f64>> Percentage<T> { pub fn as_percent(self) -> T; pub fn as_fraction(self) -> f64; }  // fraction = value/100.0
pub struct SendMeVersion(u8);
impl SendMeVersion { pub fn new(value: u8) -> Self; pub fn get(&self) -> u8; }
```

`SendMeVersion` is filled in from the consensus with **clamping** semantics:
upstream's `impl FromInt32Saturating for SendMeVersion` does
`Self::new(val.clamp(0, 255) as u8)`. `SendMeVersion::get()` returns `u8`, so the
driver compares against integer literals (`255`, `0`, `1`, `2`).

### Exactly asserted values

Rust field name is **not** a consensus key: setting `("cc_min_sendme_acks", 7)`
and `("cc_sendme_cell_ack_count", 17)` must leave both at their defaults (5, 31).

| Field | Key | Default | Range | Low input → | High input → | Other |
| --- | --- | --- | --- | --- | --- | --- |
| `cc_min_sendme_acks` | `cc_bwe_min` | 5 | 2..=20 | `0` → `2` | `1000` → `20` | `7` → `7` |
| `cc_ewma_n_by_sendme_acks` (`.as_percent()`) | `cc_ewma_cwnd_pct` | 50 | 1..=255 | `0` → `1` | `1000` → `255` | `120` → `120`; `200` → `200` |
| `cc_ewma_n_max` | `cc_ewma_max` | 10 | 2..=i32::MAX | `1` → `2` | `i32::MAX` → `i32::MAX` | `40` → `40` |
| `cc_sendme_cell_ack_count` | `cc_sendme_inc` | 31 | 1..=255 | `0` → `1` | `1000` → `255` | `17` → `17` |
| `cc_cwnd_inc_rate` | `cc_cwnd_inc_rate` | 1 | 1..=250 | `0` → `1` | `1000` → `250` | `3` → `3` |
| `sendme_accept_min_version` (`.get()` is `u8`) | `sendme_accept_min_version` | 0 | 0..=255 | — | `256` → `255`; `i32::MAX` → `255` | `1` → `1` |
| `sendme_emit_min_version` (`.get()` is `u8`) | `sendme_emit_min_version` | 0 | 0..=255 | `-1` → `0` | — | `2` → `2` |

`as_fraction()` is asserted with a tolerance of `1e-9`: default → `0.5`;
`cc_ewma_cwnd_pct = 200` → `2.0`. A percentage over 100 is legitimate and must
survive `as_percent()` unchanged (the `Percentage` type is a plain wrapper; the
bound 1..=255 is on the inner `BoundedInt32`, not on 100).

**Out of scope here:** how `tor-proto`/`tor-congestion` consume these values,
`SendMeVersion`'s `Display`/`FromStr`/`Ord` derives, `TryFrom<i32> for SendMeVersion`
(the checked, non-saturating path), `Percentage::new`, and any `cc_*` parameter
that upstream v1.0.0 does not declare (there are exactly five; do not add
`cc_alg`, `cc_cwnd_min` or similar, they are not present at this tag).

## 14. Secret buffers and zeroization — tor-bytes

Checked by `secretbuf`. Source: `crates/tor-bytes/src/secretbuf.rs`,
re-exported from `crates/tor-bytes/src/lib.rs` as `pub use secretbuf::SecretBuf;`.
Driver imports: `tor_bytes::{SecretBuf, Writer}`. This type is new at v1.0.0;
it is the write-only accumulator `tor-proto` feeds to its KDFs so that
intermediate copies of key material never sit in freed heap.

### Type and inherent API

```rust
use zeroize::{Zeroize, ZeroizeOnDrop};

#[derive(Zeroize, ZeroizeOnDrop, Debug, Clone, Eq, PartialEq)]
pub struct SecretBuf(Vec<u8>);

const DEFAULT_CAPACITY: usize = 384;   // private

impl SecretBuf {
    pub fn new() -> Self;                              // == with_capacity(384)
    pub fn with_capacity(capacity: usize) -> Self;     // Vec::with_capacity(capacity)
    pub fn truncate(&mut self, new_len: usize);        // Vec::truncate; never shrinks capacity
    pub fn extend_from_slice(&mut self, slice: &[u8]); // see growth rule below
}
impl From<Vec<u8>> for SecretBuf  { fn from(v: Vec<u8>) -> Self { Self(v) } }   // adopts, no copy
impl Default for SecretBuf        { fn default() -> Self { Self::new() } }
impl AsMut<[u8]> for SecretBuf    { fn as_mut(&mut self) -> &mut [u8] { &mut self.0[..] } }
impl std::ops::Deref for SecretBuf { type Target = Vec<u8>; fn deref(&self) -> &Vec<u8> { &self.0 } }
impl Writer for SecretBuf         { fn write_all(&mut self, b: &[u8]) { self.extend_from_slice(b); } }
```

`Deref<Target = Vec<u8>>` is load-bearing: the driver calls `buf.len()`,
`buf.is_empty()`, `buf.capacity()`, `&buf[..]`, and `buf[..].iter()` without any
explicit conversion, so the target must be `Vec<u8>` (a `[u8]` target would lose
`capacity()`). `PartialEq`/`Eq` must compare **contents only** (the derived impl
on the inner `Vec` does exactly that — capacity and provenance are irrelevant),
and `Debug` + `Clone` are used via `assert_eq!`/`assert_ne!` and `.clone()`.
The `zeroize` derives are the whole point of the type but, being unobservable
from safe Rust, are not asserted; `zeroize` (with the `derive` feature) is
already a dependency of `tor-bytes` upstream.

### The growth rule — exactly asserted

`extend_from_slice` reallocates **when the new length would reach or exceed the
current capacity** (`new_len >= capacity`, not `>`), and the new capacity is
`max(capacity * 2, new_len)`. Upstream builds a fresh `Vec::with_capacity(new_capacity)`,
copies the old contents in, `mem::replace`s, and `zeroize()`s the old vector
before appending. The driver observes the rule through `capacity()`:

| Start | Operation | Asserted `len()` | Asserted `capacity()` |
| --- | --- | --- | --- |
| `new()` / `default()` | none | 0 | `>= 384` (and `default()` == `new()` capacity) |
| `with_capacity(16)` | none | 0 | `== 16` |
| `with_capacity(8)` | `extend_from_slice(&[1,2,3,4])` | 4 | `== 8` (4 < 8, no realloc) |
| ...then | `extend_from_slice(&[5,6,7,8])` | 8 | `== 16` (8 >= 8 triggers; `max(16, 8)`) |
| `with_capacity(8)` | `extend_from_slice(&[0xaa; 100])` | 100 | `== 100` (`max(16, 100)`) |
| `new()` + 10 bytes | `truncate(4)`, `truncate(100)`, `truncate(0)` | 4, 4, 0 | unchanged throughout |

A plain `Vec::with_capacity(8)` filled to exactly 8 keeps capacity 8, so a
pass-through implementation fails the third row. `Vec::with_capacity(n)` is
guaranteed to give capacity `>= n`; in practice for `u8` it is exactly `n`,
which is what the `== 16` / `== 100` assertions rely on — do not round up.

### Content assertions

* Three `extend_from_slice` calls with `b"hello"`, `b", "`, `b"world"` give
  `&buf[..] == b"hello, world"`, `len() == 12`; `extend_from_slice(&[])` is a no-op.
* `truncate(4)` on `b"0123456789"` leaves `b"0123"`; `truncate(100)` is a no-op;
  `truncate(0)` empties; a subsequent `extend_from_slice(b"xy")` gives `b"xy"`.
* `SecretBuf::from(vec![1,2,3])` has `&buf[..] == [1,2,3]`, `len() == 3`, and is
  `==` to a buffer built by `new()` + `extend_from_slice(&[1,2,3])`;
  `SecretBuf::from(Vec::new()) == SecretBuf::new()`; `[1,2,4] != [1,2,3]`.
* `as_mut()` on `b"secret"`: set byte 0 to `b'S'`, `reverse()` → `b"terceS"`,
  `len()` still 6; on an empty buffer `as_mut()` is an empty slice (capacity is
  not exposed as content).
* As a `Writer`, driven through a generic `fn write_sample<W: Writer + ?Sized>(&mut W)`
  calling `write_u8(0x01)`, `write_u16(0x0203)`, `write_u32(0x0405_0607)`,
  `write_u64(0x0809_0a0b_0c0d_0e0f)`, `write_zeros(3)`, `write_all(b"tail")`, the
  result must equal the same sequence into a `Vec<u8>` and be exactly
  `01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 00 00 00 74 61 69 6c`.
  Then `with_capacity(4)` followed by `write_u32(i)` for `i in 0..200` must
  match a `Vec<u8>` byte-for-byte across the many reallocations.

**Out of scope here:** that zeroization actually happens (unobservable from safe
Rust; upstream's own unit test says as much), `ZeroizeOnDrop` behaviour, any
`Writeable`/`write()`/`write_nested_*` calls targeting a `SecretBuf` (only the
six `Writer` methods listed above are used on it), `Display`, `AsRef<[u8]>`
(not implemented upstream; `Deref` is used instead), `DerefMut` (deliberately
absent upstream — mutation goes through `AsMut<[u8]>` only), and how
`tor-proto` uses the buffer for KDF input.

## 15. Reader and Writer primitives — tor-bytes

Checked by `bytesrw` (a gate group: it adds no requirement of its own, and a failure there means the step is not satisfied), and depended on by every encoding test
in this step (§1, §2, §14). `crates/tor-bytes/src/reader.rs` and `writer.rs` are
unchanged at this milestone; the group is a regression guard, because the new
padding cell is encoded and decoded through exactly these calls. Driver imports:
`tor_bytes::{Error, Reader, Writer}`.

### `Reader`

```rust
pub struct Reader<'a> { /* private: slice + offset */ }

impl<'a> Reader<'a> {
    pub fn from_slice(slice: &'a [u8]) -> Self;
    pub fn from_bytes(b: &'a bytes::Bytes) -> Self;     // exists; not used by the suite
    pub fn total_len(&self) -> usize;                   // whole slice, consumed + remaining
    pub fn remaining(&self) -> usize;
    pub fn consumed(&self) -> usize;
    pub fn into_rest(self) -> &'a [u8];
    pub fn advance(&mut self, n: usize) -> Result<()>;
    pub fn should_be_exhausted(&self) -> Result<()>;
    pub fn truncate(&mut self, n: usize);               // relative to the position; no error
    pub fn peek(&self, n: usize) -> Result<&'a [u8]>;
    pub fn take(&mut self, n: usize) -> Result<&'a [u8]>;
    pub fn take_into(&mut self, buf: &mut [u8]) -> Result<()>;
    pub fn take_u8(&mut self) -> Result<u8>;
    pub fn take_u16(&mut self) -> Result<u16>;          // big-endian
    pub fn take_u32(&mut self) -> Result<u32>;          // big-endian
    pub fn take_u64(&mut self) -> Result<u64>;          // big-endian
    pub fn take_u128(&mut self) -> Result<u128>;        // big-endian
    pub fn take_until(&mut self, term: u8) -> Result<&'a [u8]>;
    pub fn take_rest(&mut self) -> &'a [u8];
    pub fn extract<E: Readable>(&mut self) -> Result<E>;
    pub fn extract_n<E: Readable>(&mut self, n: usize) -> Result<Vec<E>>;
    pub fn read_nested_u8len <F, T>(&mut self, f: F) -> Result<T> where F: FnOnce(&mut Reader) -> Result<T>;
    pub fn read_nested_u16len<F, T>(&mut self, f: F) -> Result<T> where F: FnOnce(&mut Reader) -> Result<T>;
    pub fn read_nested_u32len<F, T>(&mut self, f: F) -> Result<T> where F: FnOnce(&mut Reader) -> Result<T>;
}
pub type Result<T> = std::result::Result<T, Error>;   // tor_bytes::Result
```

### `Writer` and `NestedWriter`

```rust
pub trait Writer {
    fn write_all(&mut self, b: &[u8]);                                  // the only required method
    fn write_u8(&mut self, x: u8)      { /* &[x] */ }
    fn write_u16(&mut self, x: u16)    { /* x.to_be_bytes() */ }
    fn write_u32(&mut self, x: u32)    { /* x.to_be_bytes() */ }
    fn write_u64(&mut self, x: u64)    { /* x.to_be_bytes() */ }
    fn write_u128(&mut self, x: u128)  { /* x.to_be_bytes() */ }
    fn write_zeros(&mut self, n: usize);
    fn write<E: Writeable + ?Sized>(&mut self, e: &E) -> EncodeResult<()>;
    fn write_and_consume<E: WriteableOnce>(&mut self, e: E) -> EncodeResult<()>;
    fn write_nested_u8len (&mut self) -> NestedWriter<'_, Self, u8>;
    fn write_nested_u16len(&mut self) -> NestedWriter<'_, Self, u16>;
    fn write_nested_u32len(&mut self) -> NestedWriter<'_, Self, u32>;
}
impl Writer for Vec<u8> { .. }            // required: the driver writes into Vec<u8> everywhere
impl Writer for bytes::BytesMut { .. }    // exists upstream; out of scope

#[derive(Educe)] #[educe(Deref, DerefMut)]   // derefs to its inner Vec<u8>
pub struct NestedWriter<'w, W, L> where W: ?Sized { /* private */ }
impl<'w, W: Writer + ?Sized, L: ..> NestedWriter<'w, W, L> {
    pub fn finish(self) -> Result<(), EncodeError>;
}
```

Because `Writer` has generic methods it is **not** dyn-compatible; the driver
always goes through `fn f<W: Writer + ?Sized>(target: &mut W)`. `NestedWriter`
is used as a writer through its `DerefMut<Target = Vec<u8>>` (the driver calls
`nested.write_all(..)` and `nested.write_zeros(..)` on it), and nothing is
emitted into the outer writer until `finish()` runs.

### Exactly asserted values

| Input | Call | Result |
| --- | --- | --- |
| `01..0f` (15 bytes) | `take_u8`/`u16`/`u32`/`u64` in order | `0x01`, `0x0203`, `0x0405_0607`, `0x0809_0a0b_0c0d_0e0f`; `consumed()` 1, 3, 7; `total_len()` 15; `remaining()` 0; `should_be_exhausted()` Ok |
| `aa bb cc` | `take_u8` then `take_u32` | `0xaa`, then `Err(Error::Truncated)`; `remaining() == 2` (a failed read consumes nothing); then `take_u16() == 0xbbcc` |
| `[]` | `take(1)` | `Err(Error::Truncated)` |
| `00 01 ff` | `take_u16`, `should_be_exhausted`, `take_rest` | Ok, `Err(Error::ExtraneousBytes)`, `&[0xff]` |
| `b"one\0two\0"` | `take_until(0)` twice | `b"one"` (`consumed() == 4`, terminator consumed), `b"two"`, then exhausted |
| `b"no terminator"` | `take_until(0)` | `Err(Error::Truncated)` |
| `1 2 3 4 5 6` | `peek(2)`, `peek(7)`, `advance(2)`, `truncate(2)`, `take_u16`, `advance(1)`, `into_rest` | `&[1,2]` with `consumed() == 0`; `Err(Truncated)`; `consumed() == 2`/`remaining() == 4`; `remaining() == 2`; `0x0304`; `Err(Truncated)`; `&[]` |
| `write_u8(0xff)` + nested u16len `b"abc"` + `write_u8(0xee)` | — | `[0xff, 0x00, 0x03, b'a', b'b', b'c', 0xee]`; `read_nested_u16len` sees `inner.total_len() == 3` and yields `b"abc"`, then `0xee`, then exhausted |
| `write_nested_u8len` + `write_zeros(256)` | `finish()` | `Err(..)` (`EncodeError::BadLengthValue`), and the outer `Vec` is still **empty** |
| `[0x03, b'a', b'b', b'c']` | `read_nested_u8len(|i| i.take_u8())` | `Err(Error::ExtraneousBytes)` — the closure must consume the whole counted region |
| `[0x05, b'a']` | `read_nested_u8len(|i| Ok(i.take_rest().to_vec()))` | `Err(Error::Truncated)` — the counted bytes must be present |

`Error` comparisons are by **value equality** (`assert_eq!(err, Error::Truncated)`),
not string containment, so `impl PartialEq for Error` must be present with the
upstream semantics:

```rust
#[derive(Error, Debug, Clone)] #[non_exhaustive]
pub enum Error {
    #[error("Object truncated (or not fully present)")] Truncated,
    #[error("Extra bytes at end of object")]            ExtraneousBytes,
    #[error("Object length too large to represent as usize")] BadLengthValue,
    #[error("Bad object: {0}")]                         BadMessage(&'static str),
    #[error("Internal error")]                          Bug(#[from] tor_error::Bug),
}
// hand-written PartialEq: Truncated/ExtraneousBytes/BadLengthValue match themselves,
// BadMessage compares its &'static str, and Bug equals nothing -- not even itself.
```

The `Err` from a failed `finish()` is only checked with `expect_err`, so the
`EncodeError` variant is not compared; the `Display` strings above are not
asserted in this group (§1 asserts containment of a `BadMessage` payload).

**Out of scope here:** `take_u128`, `take_into`, `extract`/`extract_n`, the
`Readable`/`Writeable`/`WriteableOnce` traits' own impls for integers, addresses
and key types, `from_bytes`, `write`/`write_and_consume`, `write_nested_u32len`,
`Writer for bytes::BytesMut`, and the `Error::Bug` variant's inequality with
itself.

## 16. Configuration sources — tor-config

Checked by `cfgsource`. Source: `crates/tor-config/src/sources.rs`,
re-exported from `lib.rs` as `pub use sources::{ConfigurationSource, ConfigurationSources};`
with `pub mod sources;` still public (the driver imports `tor_config::sources::MustRead`
by module path) and `pub use config as config_crate;`. Driver imports:
`tor_config::config_crate::Config`, `tor_config::sources::MustRead`,
`tor_config::{ConfigurationSource, ConfigurationSources}`, plus
`fs_mistrust::Mistrust`.

**What changed at this milestone.** A source may now be a *directory* as well as
a file. `ConfigurationSource`, `ConfigurationSources::scan` and `FoundConfigFiles`
are all new; previously the sources were a plain `Vec<(PathBuf, MustRead)>`.

### Public API

```rust
#[derive(Clone, Debug, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum ConfigurationSource { File(PathBuf), Dir(PathBuf) }

impl ConfigurationSource {
    pub fn from_path<P: Into<PathBuf>>(p: P) -> ConfigurationSource;   // syntactic only, no I/O
    pub fn as_path(&self) -> &Path;
}
impl AsRef<PathBuf> for ConfigurationSource { fn as_ref(&self) -> &PathBuf; }

#[derive(Clone, Debug, Copy, Eq, PartialEq)]
pub enum MustRead { TolerateAbsence, MustRead }

#[derive(Clone, Debug, Default)]
pub struct ConfigurationSources { /* files, options, mistrust — all private */ }

impl ConfigurationSources {
    pub fn new_empty() -> Self;                                 // == Default::default()
    pub fn from_cmdline<F: Into<PathBuf>, O: Into<String>>(
        default_config_files: impl IntoIterator<Item = ConfigurationSource>,
        config_files_options: impl IntoIterator<Item = F>,
        cmdline_toml_override_options: impl IntoIterator<Item = O>,
    ) -> Self;
    pub fn push_source(&mut self, src: ConfigurationSource, must_read: MustRead);
    pub fn push_option(&mut self, option: impl Into<String>);
    pub fn set_mistrust(&mut self, mistrust: fs_mistrust::Mistrust);
    pub fn mistrust(&self) -> &fs_mistrust::Mistrust;
    pub fn load(&self) -> Result<config::Config, ConfigError>;   // scan() then FoundConfigFiles::load()
    pub fn scan(&self) -> Result<FoundConfigFiles, ConfigError>;
}

#[derive(Debug)]
pub struct FoundConfigFiles<'srcs> { /* private */ }
impl FoundConfigFiles<'_> {
    pub fn iter(&self) -> impl Iterator<Item = &ConfigurationSource>;
    pub fn load(self) -> Result<config::Config, ConfigError>;
}
```

`ConfigError` is `tor_config::ConfigError`, a `#[derive(Debug, Clone)] pub struct ConfigError(Arc<config::ConfigError>)`
with a hand-written `Display` that forwards to the inner error; the driver only
calls `.to_string()` on it. The returned `Config` is the `config` crate's, and
the driver calls `get_string(&str)` and `get_int(&str)` on it — those names exist
in `config` 0.13 (0.11/0.12 spelled it `get_str`), so the dependency must be
`config = { version = "0.13", features = ["toml"] }` and must be re-exported as
`config_crate`. Every case installs
`Mistrust::new_dangerously_trust_everyone()` via `set_mistrust`, so permission
checking is deliberately out of scope.

### `from_path` classification — exactly asserted

Classification is syntactic (`is_syntactically_directory`, private): the final
path component decides, and a prefix/root/`.`/`..` component or a trailing
separator means a directory.

| Input | Result |
| --- | --- |
| `"/etc/arti/arti.toml"` | `ConfigurationSource::File(PathBuf::from("/etc/arti/arti.toml"))` |
| `"/etc/arti.d/"` | `ConfigurationSource::Dir(PathBuf::from("/etc/arti.d/"))` |
| `"."`, `".."`, `"/"` | `matches!(.., ConfigurationSource::Dir(_))` |
| `from_path("/etc/arti.d/").as_path()` | `Path::new("/etc/arti.d/")` |

`assert_eq!` on the enum requires `PartialEq` **and** `Debug`; the path inside a
`Dir` keeps its trailing separator verbatim (no normalisation).

### `scan` — ordering and existence

For each pushed source, in push order: a `File` is emitted as-is; a `Dir` is
emitted **first**, then each entry whose extension is exactly `toml`, sorted by
full path (`entries.sort()` on `PathBuf`), each with `MustRead::TolerateAbsence`.

| Fixture / source | `found.iter()` yields |
| --- | --- |
| `Dir(fixtures/cfgdir)`, `MustRead` | `Dir(cfgdir)`, `File(cfgdir/a-first.toml)`, `File(cfgdir/b-second.toml)` — `zzz.txt` absent |
| `Dir(fixtures/no-such-dir)`, `TolerateAbsence` | nothing; `scan()` is `Ok` and `iter().count() == 0` |
| `Dir(fixtures/no-such-dir)`, `MustRead` | `scan()` is `Err`; `err.to_string()` **contains** `"unable to access config path"` |

That error string comes from
`config::ConfigError::Message(format!("unable to access config path: {:?}: {}", source.as_path(), e))`;
the check is **containment**, so the `{:?}` path and the io message around it are
free. The missing-directory branch is only tolerated for `io::ErrorKind::NotFound`.

### `load` — merging and errors

| Sources | Assertion |
| --- | --- |
| `File(fixtures/no-such-file.toml)`, `MustRead` | `load()` is `Err`; the message **contains** `"no-such-file"` (it reaches the caller as a `config::ConfigError::Foreign` wrapping `fs_mistrust::Error::NotFound(path)`, whose `Display` is `"File or directory {path} not found"`) |
| `File(fixtures/no-such-file.toml)`, `TolerateAbsence` | `load()` is `Ok`; `cfg.get_string("padding").is_err()` |
| `Dir(fixtures/cfgmerge)`, `MustRead` | `get_string("padding") == "reduced"` (02-override.toml after 01-base.toml) and `get_int("extra.n") == 1` (a key the later file omits survives) |
| same, plus `push_option("padding=\"none\"")` | `get_string("padding") == "none"`, `get_int("extra.n") == 1` — options apply after every file |
| `File(fixtures/cfgdir/zzz.txt)`, `MustRead` | `load()` is `Err` and `!err.to_string().is_empty()` — a named file is parsed as TOML whatever its extension |
| `Dir(fixtures/cfgdir)`, `MustRead` | `Ok`; `get_string("padding") == "normal"` — the same `zzz.txt` is skipped by the scan |

The `*.toml` filter therefore applies to *directory scanning only*; `load` forces
`config::FileFormat::Toml` on every file it was given by name. `Dir` entries are
skipped by `FoundConfigFiles::add_sources` (they exist in the list purely so a
caller can watch them), and the accumulated `push_option` strings are fed through
`CmdLine::push_toml_line` as the last source.

Fixture contents the assertions depend on: `cfgdir/a-first.toml` is
`padding = "normal"`; `cfgdir/b-second.toml` is `[sub]\nunknown = 1`;
`cfgdir/zzz.txt` is `not a toml file`; `cfgmerge/01-base.toml` is
`padding = "normal"` plus `[extra]\nn = 1`; `cfgmerge/02-override.toml` is
`padding = "reduced"`.

**Out of scope here:** `from_cmdline` (upstream has its own unit test; the driver
never calls it), `mistrust()`, `Hash`/`Ord`/`PartialOrd` on `ConfigurationSource`,
`AsRef<PathBuf>` used directly (the driver goes through `as_path()`),
`ConfigError::inner()`, the "invalid escape" hint in `ConfigError`'s `Display`,
`FoundConfigFiles::load` called separately from `ConfigurationSources::load`
(only the convenience path is used), real permission enforcement, and the
directory-watching machinery the retained `Dir` entries exist for.

## 17. Configuration resolution — tor-config

Checked by `cfgresolve`. Source: `crates/tor-config/src/load.rs`, which
is public as `pub mod load;` with `pub use load::{resolve, resolve_ignore_warnings, resolve_return_results};`
at the crate root. Driver imports:
`tor_config::load::{resolve, resolve_ignore_warnings, resolve_return_results, Builder, ConfigResolveError, TopLevel}`,
`tor_config::ConfigBuildError`, and `tor_config::config_crate as config`.

This is the two-stage configuration model the new config directory (§16) feeds:
a `Deserialize`-able builder per consumer, resolved into built settings. The
driver defines its own builder/built pairs, so what is checked is the **traits
and free functions**, not any arti config struct.

### Traits and functions the driver implements against

```rust
pub trait Builder {
    type Built;
    fn build(&self) -> Result<Self::Built, ConfigBuildError>;
}

pub trait TopLevel {
    type Builder: serde::de::DeserializeOwned;
    const DEPRECATED_KEYS: &'static [&'static str] = &[];    // the default is required
}

pub trait Resolvable: Sized {
    fn resolve(input: &mut ResolveContext) -> Result<Self, ConfigResolveError>;
    fn enumerate_deprecated_keys<F>(f: &mut F) where F: FnMut(&'static [&'static str]);
}
impl<T> Resolvable for T where T: TopLevel, T::Builder: Builder<Built = Self> { .. }  // blanket
// plus Resolvable for tuples (A,) .. (A,B,C,D,E) via define_for_tuples!{ A - B C D E }

pub fn resolve<T: Resolvable>(input: config::Config) -> Result<T, ConfigResolveError>;
pub fn resolve_ignore_warnings<T: Resolvable>(input: config::Config) -> Result<T, ConfigResolveError>;
pub fn resolve_return_results<T: Resolvable>(input: config::Config)
    -> Result<ResolutionResults<T>, ConfigResolveError>;

#[derive(Debug, Clone)] #[non_exhaustive]
pub struct ResolutionResults<T> {
    pub value: T,
    pub unrecognized: Vec<DisfavouredKey>,
    pub deprecated: Vec<DisfavouredKey>,
}

#[derive(Debug, Clone, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub struct DisfavouredKey { /* private Vec<PathEntry> */ }
impl std::fmt::Display for DisfavouredKey { .. }    // dotted TOML path

pub struct ResolveContext { /* opaque; appears only in Resolvable::resolve */ }

#[derive(Error, Debug)] #[non_exhaustive]
pub enum ConfigResolveError {
    #[error("Config contents not as expected")]  Deserialize(#[from] crate::ConfigError),
    #[error("Config semantically incorrect")]    Build(#[from] ConfigBuildError),
}
impl From<config::ConfigError> for ConfigResolveError { .. }   // via crate::ConfigError
```

Two bounds matter for compilation. The blanket `Resolvable` impl requires
`T::Builder: Builder<Built = T>` — the impl must be on the **builder type
itself**, which is what the driver writes (`impl Builder for PaddingSettingsBuilder { type Built = PaddingSettings; .. }`),
not on a reference to it. And `TopLevel::DEPRECATED_KEYS` must have a default
value, because the driver's second consumer (`impl TopLevel for LogSettings`)
omits it. `ResolveContext` must be nameable but need not be constructible
outside the module; the driver only ever mentions it via the trait bounds.

`ConfigBuildError` is used by the driver's own `build`:

```rust
#[derive(Debug, Clone, thiserror::Error)] #[non_exhaustive]
pub enum ConfigBuildError {
    #[error("Field was not provided: {field}")]            MissingField { field: String },
    #[error("Value of {field} was incorrect: {problem}")]  Invalid { field: String, problem: String },
    #[error("Fields {fields:?} are inconsistent: {problem}")] Inconsistent { fields: Vec<String>, problem: String },
}
```

The driver constructs `ConfigBuildError::Invalid { field, problem }` with both
fields as `String`, and matches it back out of `ConfigResolveError::Build(..)`,
so the variant name and both field names are checked.

### Exactly asserted values

| Input TOML | Call | Result |
| --- | --- | --- |
| `padding = "reduced"` | `resolve` | built value `{ padding: "reduced", retries: 3, sub_known: 0 }` (defaults come from the driver's `build`) |
| empty string | `resolve` | `padding == "normal"`, `retries == 3` |
| `padding="none"`, `wombat=3`, `[sub] known=7, other=1` | `resolve_return_results` | `Ok`; `value.padding == "none"`, `value.sub_known == 7`; `unrecognized` rendered with `to_string()` is exactly `["sub.other", "wombat"]` |
| `padding = "none"` | `resolve_ignore_warnings` | `Ok`, `padding == "none"` |
| `padding_enabled = true` | `resolve_return_results` | `deprecated` rendered is exactly `["padding_enabled"]` |
| `padding = "none"` | `resolve_return_results` | `deprecated.is_empty()` |
| `retries = 1000` | `resolve` | `Err(ConfigResolveError::Build(ConfigBuildError::Invalid { field, .. }))` with `field == "retries"`; `err.to_string()` **equals** `"Config semantically incorrect"` |
| `retries = "lots"` | `resolve` | `Err(ConfigResolveError::Deserialize(_))`; `err.to_string()` **equals** `"Config contents not as expected"` |
| `padding="reduced"`, `verbosity="debug"`, `wombat=3` | `resolve_return_results::<(PaddingSettings, LogSettings)>` | both built; `unrecognized` rendered is exactly `["wombat"]` |

Both error-string checks are **equality**, not containment, so the `#[error(..)]`
text on `ConfigResolveError` must match character for character.

The `unrecognized` ordering is the `BTreeSet<DisfavouredKey>` order that survives
into the `Vec`: `DisfavouredKey`'s derived `Ord` compares the path component-wise,
so `["sub", "other"]` sorts before `["wombat"]`. `Display` joins map entries with
`.` (quoting with `{:?}` any component that is not `[A-Za-z0-9][A-Za-z0-9_-]*`)
and renders array indices as `[n]`. Keys with an empty path are filtered out
before returning.

Behaviour the assertions pin down in `resolve_inner`:

* Unrecognized keys are collected per consumer with `serde_ignored` and
  **intersected** across consumers, so a key one consumer claims is not reported.
  A deserialization *error* clears the set for that consumer to avoid false
  positives.
* `resolve_ignore_warnings` runs with tracking disabled (`want_disfavoured = false`),
  and must still succeed and build.
* Deprecated keys are detected by probing the input with
  `input.get::<serde::de::IgnoredAny>(key)` for each entry of `DEPRECATED_KEYS`,
  so a declared-but-absent key is not reported.
* `resolve` logs both lists via `tracing::warn!` and returns only the value.

**Out of scope here:** the `impl_standard_builder!` and `derive_builder`
machinery (the driver hand-writes its builders), `ConfigBuildError::MissingField`
/`Inconsistent`/`within`/`HasKind`, `DisfavouredKey`'s `Hash`/`Ord` used
directly, array-index and quoted-key rendering in `Display`, tuples longer than
two, the exact `tracing` output of `resolve`, and `ReconfigureError`.

## 18. Runtime TCP stream Send bound — tor-rtcompat

Checked by `rtsend`. Source: `crates/tor-rtcompat/src/traits.rs`,
re-exported from `lib.rs` as part of
`pub use traits::{BlockOn, CertifiedConn, Runtime, SleepProvider, TcpListener, TcpProvider, TlsProvider, ..};`.
Driver imports: `tor_rtcompat::TcpListener`, plus `async_trait::async_trait`,
`futures::stream::{self, Stream, StreamExt}` and `futures::io::Cursor`.

**What changed at this milestone.** One bound. At v0.6.0 the associated type read
`type Incoming: stream::Stream<Item = IoResult<(Self::TcpStream, SocketAddr)>> + Unpin;`
— at v1.0.0 it gained `+ Send`, so an accept loop's stream can be moved into a
spawned task or boxed as `dyn Stream + Send`. No network is involved: the driver
implements the trait on an in-memory fake.

### The trait, exactly as the driver implements it

```rust
use async_trait::async_trait;
use futures::stream;
use futures::{AsyncRead, AsyncWrite};
use std::io::Result as IoResult;
use std::net::SocketAddr;

#[async_trait]
pub trait TcpListener {
    type TcpStream: AsyncRead + AsyncWrite + Send + Sync + Unpin + 'static;
    type Incoming: stream::Stream<Item = IoResult<(Self::TcpStream, SocketAddr)>> + Send + Unpin;

    async fn accept(&self) -> IoResult<(Self::TcpStream, SocketAddr)>;
    fn incoming(self) -> Self::Incoming;
    fn local_addr(&self) -> IoResult<SocketAddr>;
}
```

The `#[async_trait]` attribute on the trait declaration is required: the driver
writes `#[async_trait] impl TcpListener for FakeListener` with `async fn accept`,
and that only lines up if the trait was declared the same way (`async-trait` is
already a dependency of `tor-rtcompat`). `TcpListener` must **not** gain extra
supertraits or bounds on `Self` — the driver's `FakeListener` is a bare struct
that is neither `Clone` nor `'static`-bounded at the impl site, and `check::<FakeListener>()`
instantiates it with no further obligations.

The driver's fake uses `type TcpStream = futures::io::Cursor<Vec<u8>>` and
`type Incoming = stream::Iter<std::vec::IntoIter<IoResult<(FakeStream, SocketAddr)>>>`,
so both associated types must accept a concrete non-runtime type; nothing in the
bounds may require a runtime-specific trait.

### How the bound is checked

The `Send`/`Unpin` checks are made **generically**, through a type parameter, so a
concrete stream that happens to be `Send` cannot satisfy them by accident:

```rust
fn require_send<T: Send + ?Sized>() {}
fn require_unpin<T: Unpin + ?Sized>() {}

fn check<L: TcpListener>() {
    require_send::<L::Incoming>();    // fails to compile unless the trait declares + Send
    require_unpin::<L::Incoming>();
    require_send::<L::TcpStream>();   // pre-existing bounds, checked to prove Send was added
    require_unpin::<L::TcpStream>();  // rather than moved from TcpStream to Incoming
}
check::<FakeListener>();
```

The second case erases the stream behind a box, spelling out only `'static`:

```rust
fn erase<L: TcpListener>(listener: L)
    -> Box<dyn Stream<Item = IoResult<(L::TcpStream, SocketAddr)>> + Send + Unpin>
where L::TcpStream: 'static, L::Incoming: 'static
{ Box::new(listener.incoming()) }
```

`Send` and `Unpin` are *not* restated in the `where` clause — the trait must
already promise them, or this function does not compile. Note `incoming(self)`
takes the listener by value.

### Exactly asserted values

| Call | Result |
| --- | --- |
| `boxed.next()` (first) | `Some(Ok((stream, peer)))` with `peer.port() == 1234` and `stream.into_inner() == b"hello".to_vec()` |
| `boxed.next()` (second) | `None` |
| `listener.local_addr()` | `"127.0.0.1:9150".parse::<SocketAddr>().unwrap()` |
| `listener.accept()` | `Ok((stream, peer))` with `peer == "127.0.0.1:1234".parse().unwrap()` and `stream.into_inner() == b"hello".to_vec()` |
| `require_send::<futures::future::BoxFuture<'_, IoResult<(FakeStream, SocketAddr)>>>()` | compiles |

All three values come from the driver's own fake, so they check the trait's shape
(by-value `incoming`, `&self` `accept`, `&self` `local_addr`, the
`(stream, addr)` tuple order inside `IoResult`) rather than any upstream
behaviour. Futures are driven with `futures::executor::block_on`; no arti runtime
is constructed.

**Out of scope here:** `TcpProvider`, `Runtime`, `SleepProvider`, `BlockOn`,
`TlsProvider`/`CertifiedConn`, `UdpProvider`/`UdpSocket`, the tokio and async-std
implementations of `TcpListener` (no cargo feature of `tor-rtcompat` is enabled by
the driver's manifest, so the concrete runtimes are not even compiled), and any
real socket behaviour.

## 19. Subprotocol versions — tor-protover

Checked by `protover` (a gate group: it adds no requirement of its own, and a failure there means the step is not satisfied). Source: `crates/tor-protover/src/lib.rs`,
which is the whole crate. Driver imports: `tor_protover::{ParseError, ProtoKind, Protocols}`.
Nothing here changes at v1.0.0; it is checked because the new netdir by-identity
lookups and capability selection keep asking a `Protocols` set what it supports.

Grammar (dir-spec §2.1.1): a space-separated list of `Keyword=Versions`; a
`Versions` is a comma-separated list of numbers or inclusive `lo-hi` ranges;
a version number is `0..=63`.

### `ProtoKind` — a `caret_int!` newtype over `u16`

```rust
use caret::caret_int;
caret_int! {
    #[derive(Hash, Ord, PartialOrd)]
    pub struct ProtoKind(u16) {
        Link = 0, LinkAuth = 1, Relay = 2, DirCache = 3, HSDir = 4, HSIntro = 5,
        HSRend = 6, Desc = 7, MicroDesc = 8, Cons = 9, Padding = 10, FlowCtrl = 11,
    }
}
```

`caret_int!` (`crates/caret/src/lib.rs`) supplies `#[derive(PartialEq, Eq, Copy, Clone)]`,
`From<ProtoKind> for u16`, `From<u16> for ProtoKind`, `Display` (name, or the
number for unrecognized values), `Debug` (`ProtoKind(name)`), and `pub fn is_recognized(self) -> bool`
plus `pub fn from_name(name: &str) -> Option<Self>`. `get(self) -> u16` is
private to the macro's module. `N_RECOGNIZED == 12`.

### `Protocols`

```rust
#[derive(Debug, Clone, Default, Eq, PartialEq, Hash)]
pub struct Protocols {
    recognized: [u64; 12],              // bit i of slot k => ProtoKind(k) version i
    unrecognized: Vec<SubprotocolEntry>,
}

impl Protocols {
    pub fn new() -> Self;                                             // == Default
    pub fn supports_known_subver(&self, proto: ProtoKind, ver: u8) -> bool;
    pub fn supports_subver(&self, proto: &str, ver: u8) -> bool;
}
impl std::str::FromStr for Protocols { type Err = ParseError; fn from_str(s: &str) -> Result<Self, ParseError>; }
impl std::fmt::Display for Protocols { .. }

#[derive(Error, Debug, PartialEq, Eq, Clone)]
#[non_exhaustive]
pub enum ParseError {
    #[error("Protocol version out of range")]   OutOfRange,
    #[error("Duplicate protocol entry")]        Duplicate,
    #[error("Malformed protocol entry")]        Malformed,
}
```

`Protocols` must be `Default + PartialEq + Eq + Debug + Clone` and
`FromStr<Err = ParseError>`; `ParseError` must be `PartialEq + Eq + Debug`.

### Exactly asserted values

| Input / call | Result |
| --- | --- |
| `"Link=1-3 HSDir=2,4-5"` then `supports_known_subver` | `Link` 1,2,3 true; `Link` 0 false; `Link` 4 false; `HSDir` 2,4,5 true; `HSDir` 3 false; `LinkAuth` 1 false |
| `"Link=1-3 Foobar=7"` then `supports_subver` | `("Foobar",7)` true; `("Foobar",6)` false; `("Wombat",7)` false; `("Link",2)` true; `("Link",5)` false |
| `ProtoKind::from_name("Foobar")` | `None` (the test asserts this, so the unknown name is not silently recognized) |
| `"Link=1-3 HSDir=2,4-5".to_string()` | exactly `"HSDir=2,4-5 Link=1-3"` — entries sorted as strings, runs collapsed to ranges |
| reparsing that output | `==` the original set (`Display` is a round trip) |
| `"Link=1,2,3".to_string()` | exactly `"Link=1-3"`, and equal to `"Link=1-3".parse()` |
| `"Link=1-3 Link=4".parse::<Protocols>()` | `Err(ParseError::Duplicate)` |
| `"Link=64".parse::<Protocols>()` | `Err(ParseError::OutOfRange)` |
| `"Link".parse::<Protocols>()` | `Err(ParseError::Malformed)` |
| `"Link=3-1".parse::<Protocols>()` | `Err(ParseError::Malformed)` |
| `Protocols::new()` | `== Protocols::default()`; `.to_string() == ""`; `"".parse() == new()`; supports nothing for `Link`/0, `"Link"`/0, `"Foobar"`/0 |
| `u16::from(ProtoKind::..)` | `Link` 0, `Relay` 2, `DirCache` 3, `Desc` 7, `Padding` 10, `FlowCtrl` 11 |
| `ProtoKind::from_name("HSDir")` | `Some(ProtoKind::HSDir)`; `from_name("hsdir")` is `None` — names are case-sensitive |
| `ProtoKind::Link.is_recognized()` / `ProtoKind::from(999u16).is_recognized()` | `true` / `false` |
| `ProtoKind::Padding.to_string()` | exactly `"Padding"` |

All `ParseError` checks are **value equality** against the enum variant (both
`PartialEq` and `Debug`), so the `#[error(..)]` strings are not directly asserted
— but `Debug` on the error is required by `unwrap_err()`, and `PartialEq` by the
comparison. Behaviour the assertions pin down in the parser:

* An unknown keyword is stored under its own name and looked up case-sensitively;
  a known keyword goes to its numbered slot.
* `Name=` (empty version list) is **not** an error — it produces a zero mask.
  Entries with a zero mask are dropped from `Display`.
* A number with a leading `0` (or any non-digit) is `Malformed`; versions above
  63 are `OutOfRange`; `lo > hi` is `Malformed`; a repeated protocol name or an
  overlapping version within one entry is `Duplicate`.
* Empty input (or empty space-separated fields, which are skipped) yields the
  empty set.
* `Display` builds entries for recognized kinds by ascending discriminant plus
  unrecognized ones, collapses set bits greedily into `lo-hi` (a singleton is
  bare), and sorts the rendered `"Name=versions"` strings before joining with a
  single space.

**Out of scope here:** `ProtocolsUnvalidated`/`Protocols` use by `tor-netdir` or
`tor-netdoc`, `Protocol`/`SubprotocolEntry` (private), `bitrange`/`dumpmask`
(private helpers), `Hash`/`Ord` on either public type, `ParseError`'s `Display`
text, the `is_good_number` leading-zero rule exercised for any input other than
those in the table, and `ProtoKind::from(999u16)`'s rendered number.

## 20. Error kinds and reports — tor-error

Checked by `errkind` (a gate group: it adds no requirement of its own, and a failure there means the step is not satisfied). Source: `crates/tor-error/src/lib.rs`
(`ErrorKind`, `HasKind`), `src/internal.rs` (the `Bug` type and the `internal!`
/ `bad_api_usage!` macros), `src/report.rs` (`Report`). All are re-exported at
the crate root. Driver imports:
`tor_error::{bad_api_usage, internal, ErrorKind, HasKind, Report}`, plus
`std::error::Error` and `std::fmt::{Display, fmt}` for its own error types.

Nothing in this crate changes at v1.0.0; it is checked because the new congestion,
padding and configuration-directory code all report through this vocabulary.

### Public surface

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, derive_more::Display)]
#[non_exhaustive]
pub enum ErrorKind {
    // 44 unit variants at v1.0.0, each with #[display(fmt = "...")]; the three checked here:
    #[display(fmt = "internal error (bug)")]                              Internal,
    #[display(fmt = "bad API usage (bug)")]                               BadApiUsage,
    #[display(fmt = "operation not supported because Arti feature disabled")] FeatureDisabled,
    // ...
}

pub trait HasKind { fn kind(&self) -> ErrorKind; }

#[derive(Debug, Clone)]
pub struct Bug(/* private Box<BugRepr> */);
impl Bug {
    #[track_caller] pub fn new<S: Into<String>>(kind: ErrorKind, message: S) -> Self;
    #[track_caller] pub fn from_error<E, S>(kind: ErrorKind, source: E, message: S) -> Self
        where S: Into<String>, E: std::error::Error + Send + Sync + 'static;
}
impl HasKind for Bug { fn kind(&self) -> ErrorKind; }
impl std::error::Error for Bug { fn source(&self) -> Option<&(dyn std::error::Error + 'static)>; }
impl Display for Bug { .. }

#[macro_export] macro_rules! internal       { { $( $arg:tt )* } => { $crate::Bug::new($crate::ErrorKind::Internal,      format!($($arg)*)) } }
#[macro_export] macro_rules! bad_api_usage  { { $( $arg:tt )* } => { $crate::Bug::new($crate::ErrorKind::BadApiUsage,    format!($($arg)*)) } }
#[macro_export] macro_rules! into_internal      { .. }   // exists; not used by the suite
#[macro_export] macro_rules! into_bad_api_usage { .. }   // exists; not used by the suite

#[derive(Debug, Copy, Clone)]
pub struct Report<E>(pub E) where E: AsRef<dyn std::error::Error>;
impl<E: AsRef<dyn std::error::Error>> Display for Report<E> { .. }
```

**The enum has exactly 44 variants at v1.0.0.** `/app` is cumulative, so every
earlier step's `TEST_INTERFACE.md` is still on disk under `/app/docs/step-NN/`
and still binding, and three of them enumerate this type. Two give a smaller
number and are **superseded** on the count by this section:
`/app/docs/step-06/TEST_INTERFACE.md` §1.1 declares
`pub enum ErrorKind { /* 40 variants */ }` and its §1.1.1 tabulates all 40 under
"Every one of these 40 renderings is checked"; `/app/docs/step-07/TEST_INTERFACE.md`
§12.1 corrects that to **42** and gives a 42-row table. Both were accurate where
they were written — v0.1.0 has 40, v0.2.0 has 42 — and neither is accurate here.
The third, `/app/docs/step-10/TEST_INTERFACE.md`, already states **44** under
"What this document supersedes"; that statement is still true at v1.0.0 and this
section **agrees with it**. Where step 06 or step 07 conflicts with this section
or with step 10's, the later document wins.

The delta from step 06's 40 is four **additions**, with nothing renamed and
nothing removed — every one of step 06's 40 names still resolves at v1.0.0:
`ClockSkew` (`possible clock skew detected`) and `Other` (`unclassified error`),
both of which step 07's table already carries, plus `FsPermissions`
(`problem with filesystem permissions`) and `RemoteHostResolutionFailed`
(`remote hostname lookup failure`), which appear in neither earlier table.

One **relabelling** does invalidate a row in both earlier tables — 39 of step
06's 40 renderings are unchanged, and this is the fortieth:

| Variant | steps 06 and 07 give | true at v1.0.0 |
| --- | --- | --- |
| `RemoteHostNotFound` | `remote hostname lookup failure` | `remote hostname not found` |
| `RemoteHostResolutionFailed` | (did not exist) | `remote hostname lookup failure` |

The string `RemoteHostNotFound` vacated was reassigned to the new
`RemoteHostResolutionFailed`; it did not disappear, it changed owner. Copying
either earlier table forward verbatim and then adding the new variant gives
`RemoteHostNotFound` a label that belongs to another variant *and* leaves two
variants sharing one `Display` string, which contradicts the mutual-distinctness
property step 07 §12.1 states of the enum. Nothing warns you: a renamed variant
would be a compile error, whereas a reassigned label compiles and simply renders
the wrong sentence.

**This is the only rendering that moved.** Every one of step 06's other 39
labels, and both labels step 07 added, are byte-identical at v1.0.0 — so apart
from this single row the earlier tables are safe to copy from.

**No part of this step's checks rides on any of this.** `errkind` asserts exactly three
`Display` strings — `Internal`, `BadApiUsage` and `FeatureDisabled`, the three
written out above — and all three are byte-identical to their v0.1.0 spellings;
the group asserts no variant count and reads no other rendering. An `ErrorKind`
carried forward unchanged from an earlier checkpoint still passes it. The count
and the relabelling are stated because the enum is `#[non_exhaustive]`, because
step 06's table is binding and is written to be copied from, and because a
workspace that reproduces v1.0.0 has 44.

Three details are load-bearing at compile time. `Bug::new` must be `#[track_caller]`
(and so must the macro expansion reach it without an intervening non-tracked
frame), because the driver asserts the rendered location names *its own* file.
`Report`'s bound is `E: AsRef<dyn std::error::Error>`, not the older
`E: std::error::Error` — the driver passes `&Box<dyn StdError>` (`Report(&terse)`),
which satisfies `AsRef` through the blanket `impl<T: ?Sized + AsRef<U>, U: ?Sized> AsRef<U> for &T`.
And `Report`'s field is **public tuple field 0**, since it is constructed as
`Report(&x)`. `ErrorKind`'s `Display` comes from `derive_more::Display` with a
`#[display(fmt = "...")]` attribute per variant, so `derive_more` must be a
dependency with the `Display` derive available.

### Exactly asserted values

| Call | Assertion | Kind of check |
| --- | --- | --- |
| `ErrorKind::Internal.to_string()` | `"internal error (bug)"` | equality |
| `ErrorKind::BadApiUsage.to_string()` | `"bad API usage (bug)"` | equality |
| `ErrorKind::FeatureDisabled.to_string()` | `"operation not supported because Arti feature disabled"` | equality |
| `internal!("no cells left in {}", "the queue").kind()` | `ErrorKind::Internal` | equality |
| its `to_string()` | starts with `"internal error (bug) at "` | **prefix** (`starts_with`) |
| ... | contains `"errkind.rs"` | **containment** |
| ... | contains `"no cells left in the queue"` | **containment** |
| `bad_api_usage!("called {} twice", "connect").kind()` | `ErrorKind::BadApiUsage`, and `!= internal!("x").kind()` | equality |
| its `to_string()` | starts with `"bad API usage (bug) at "`, contains `"called connect twice"` | prefix / containment |
| `Report(&Terse(Inner)).to_string()` | `"error: outer failure: inner cause"` | equality |
| `Report(&Verbose(Inner)).to_string()` | `"error: outer failure - inner cause"` | equality |
| `Report(&Inner).to_string()` | `"error: inner cause"` | equality |

`Bug`'s `Display` is
`write!(f, "{} at {}: {}", self.kind, self.location, self.message)` (with a
trailing newline, after which a backtrace is appended when the `backtrace`
feature is on) — so the prefix is the kind's own `Display`, the location is the
captured call site, and the message is the formatted macro argument. The
location is captured by `panic::Location::caller()` inside `Bug::new_inner`
called from `Bug::new`, which is called by the macro at the driver's call site:
`#[track_caller]` on both.

`Report`'s `Display` writes `"error"` and then, walking the `source()` chain,
appends `": {this}"` for each error whose `to_string()` is **not already
contained** in the previous error's message. This is what makes the `Verbose`
case print one message rather than repeating the inner cause, and it means the
two `Inner`/`Verbose`/`Terse` types in the group are crafted so that the
containment test is decided by whether the parent's text quotes the child.

`ErrorKind` is `Copy` — the driver moves `k` into a function by value and uses
`k` again afterwards, and copies it to `copied` — and `PartialEq`/`Eq` with
distinct variants comparing unequal.

**Out of scope here:** any other `ErrorKind` variant (in particular
`DirectoryExpired`, which §12's provider test returns, is only ever used as an
error *value* there and its `Display` is not asserted), `HasKind` impls for
external types (`std::io::Error`, `SpawnError`, ...), `report_and_exit`,
`RetryTime` / `retriable.rs`, `truncated.rs` (`TruncatedError`),
`into_internal!`/`into_bad_api_usage!`, `Bug::from_error` called directly, the
backtrace portion of `Bug`'s `Display`, and `Bug`'s `source()` method.

## 21. Miscellaneous utilities — tor-basic-utils and fs-mistrust

Checked by `miscutil`. Two small helpers that the new
configuration-directory support needs: `tor_basic_utils::IoErrorExt`, to tell
"you named a file where a directory belongs" apart from every other I/O
failure, and `fs_mistrust::anon_home::PathExt`, so the paths that code reports
do not name the user. Neither is protocol-visible. Sources:
`crates/tor-basic-utils/src/lib.rs` and `crates/fs-mistrust/src/anon_home.rs`.

### Public surface

```rust
// tor-basic-utils: at the crate root, not in a submodule.
/// Implementation of `ErrorKind::NotADirectory` that doesn't require Nightly
pub trait IoErrorExt {
    /// Is this `io::ErrorKind::NotADirectory` ?
    fn is_not_a_directory(&self) -> bool;
}
impl IoErrorExt for std::io::Error { .. }

// fs-mistrust: module `anon_home`, behind the default-on `anon_home` feature.
pub trait PathExt {
    fn anonymize_home(&self) -> AnonHomePath<'_>;
}
impl PathExt for Path { .. }

#[derive(Debug, Clone)]
pub struct AnonHomePath<'a>(/* private &'a Path */);
impl<'a> std::fmt::Display for AnonHomePath<'a> { .. }
```

Both are imported anonymously (`use tor_basic_utils::IoErrorExt as _;`,
`use fs_mistrust::anon_home::PathExt as _;`), so only the method names and the
receiver types matter — but the paths `tor_basic_utils::IoErrorExt` and
`fs_mistrust::anon_home::PathExt` must resolve exactly as written.
`anonymize_home` takes `&self` and returns a borrowing wrapper: the driver uses
the original `PathBuf` again afterwards and asserts it is unchanged, so this
cannot consume or rewrite the path.

### Cargo features

| Crate | Requirement |
| --- | --- |
| `fs-mistrust` | `anon_home` (in `default = ["walkdir", "anon_home"]`; pulls in `once_cell` and `dirs`). The harness depends on `fs-mistrust` with default features, so this is satisfied by not disabling defaults. |
| `tor-basic-utils` | `libc` must be a dependency; `is_not_a_directory` compares against `libc::ENOTDIR` on unix (and a hard-coded `ERROR_DIRECTORY = 267` on windows). |

### Exactly asserted values

The driver pins the home directory with `std::env::set_var("HOME", "/conformance-home")`
before each anonymization case, so the implementation must derive its home
directory from the `HOME` environment variable (upstream: `dirs::home_dir()`
inside a `once_cell::sync::Lazy`). Caching once per process is fine — all five
cases set the same value — but a home directory obtained only from `getpwuid`
would not be observable and the case would fail.

| Input | Expected | Kind of check |
| --- | --- | --- |
| `metadata("fixtures/cfgdir/zzz.txt/inner")` error | `.is_not_a_directory()` is `true` | boolean (ENOTDIR through a plain file) |
| `metadata("fixtures/cfgdir/no-such-file.toml")` error | `.kind() == ErrorKind::NotFound` and `.is_not_a_directory()` is `false` | equality / boolean |
| `io::Error::new(ErrorKind::PermissionDenied, "denied")` | `.is_not_a_directory()` is `false` | boolean |
| `/conformance-home/.config/arti.toml` | `"${HOME}/.config/arti.toml"` | equality |
| `/conformance-home/x` | `"${HOME}/x"` | equality |
| the same `PathBuf` after the call | still `/conformance-home/.config/arti.toml` | equality |
| `/completely/untouchable/arti.toml` | unchanged | equality |
| `/etc/arti/arti.toml` | unchanged | equality |
| `arti.toml` (relative) | `"arti.toml"` | equality |
| `/conformance-home-of-someone-else/arti.toml` | unchanged | equality |
| `/conformance-home` (the home dir itself) | `"${HOME}/"` | equality |

Two of these constrain the implementation beyond "replace a prefix". The
sibling case requires **component-wise** prefix matching — upstream gets this
free from `Path::strip_prefix`, which never matches a partial component, so a
`str`-level `starts_with` on the rendered path would fail this case. The
home-directory-itself case requires the separator to be emitted
unconditionally: `strip_prefix` yields the empty path, and the format is
`"{HOME_SUBSTITUTION}{MAIN_SEPARATOR}{suffix}"`, giving `"${HOME}/"` rather
than `"${HOME}"`. The substitution string is `"${HOME}"` on unix and
`"%UserProfile%"` on windows; the suite runs on unix.

Note that `is_not_a_directory` is a `raw_os_error()` comparison, not a
`kind()` comparison — that is the whole reason the trait exists, since
`std::io::ErrorKind::NotADirectory` is unstable at this toolchain. A synthetic
error built with `io::Error::new` has no raw OS error and therefore answers
`false`, which is exactly what the third case checks.

**Out of scope here:** everything else in `tor-basic-utils` —
`test_rng::testing_rng`, `retry::RetryDelay`, `iter::IteratorExt` /
`FilterCount`, `futures::{SinkExt, PostageWatchSenderExt, DropNotifyWatchSender}`,
`define_accessor_trait!` and `skip_fmt` — and everything else in
`fs-mistrust` except the permissive `Mistrust` used by §16:
`CheckedDir`, `walk::ResolvePath`, `TrustedUser` / `TrustedGroup`,
`GLOBAL_DISABLE_VAR`, and the real permission-checking paths. `AnonHomePath`'s
`Debug` and `Clone` impls are not exercised, and no case anonymizes a path
below a *canonical* home directory (upstream checks several homedir spellings;
only the plain one is checked).

---

## Requirement groups → interface items

Every group this step uses, and the sections above that it exercises. A group is
checked only through items named in this document; anything reachable but
undocumented is out of scope.

| Group | Kind | Items |
|---|---|---|
| `padnegcell` | requirement | §1 |
| `padparams` | requirement | §1, §3 |
| `padinsns` | requirement | §3, §4 |
| `padupdates` | requirement | §3, §4 |
| `padlevel` | requirement | §5 |
| `chanconfig` | requirement | §5, §6 |
| `chanusage` | requirement | §6 |
| `relayid` | requirement | §7 |
| `relayidset` | requirement | §7, §8 |
| `hasrelayids` | requirement | §7, §9 |
| `owntarget` | requirement | §7, §9, §10 |
| `netdirids` | requirement | §7, §9, §11 |
| `dirparams` | requirement | §12 |
| `ccparams` | requirement | §12, §13 |
| `secretbuf` | requirement | §14 |
| `cfgsource` | requirement | §16 |
| `cfgresolve` | requirement | §17 |
| `rtsend` | requirement | §18 |
| `miscutil` | requirement | §21 |
| `cellfmt` | gate | §2 |
| `protover` | gate | §19 |
| `errkind` | gate | §20 |
| `bytesrw` | gate | §15 |

The group names are those in `tests/assets/groups.txt`. The gate groups
(`cellfmt`, `protover`, `errkind`, `bytesrw`) re-check behaviour introduced at
earlier steps against the cumulative step-11 workspace — they are not new work,
but breaking them means the step is not satisfied, just as surely as never
writing the new work would. `miscutil` covers breadth around the new surface
rather than its core.

### Declared here, not driven by any case

The contract above is the surface, not the call list. The items below are named
because the milestone, a later step or a real caller needs them, and no group in
this step reaches them. Implement them as written.

- **`tor-congestion`.** The crate is a workspace member at this release and the
  milestone names it. No group imports it and the driver does not depend on it,
  so a workspace that omits it entirely still satisfies every requirement group.
  What *is* checked is the consumption half: the four `nf_ito_*` parameters
  above, which the padding timings read.
- **Experimental-API gating as a feature name.** The gating is checked only where
  it changes an item's reachability from the driver; no case asserts that a
  particular `experimental-api` feature exists or that a particular item is
  behind it.
- **Zeroization as an observable.** `SecretBuffer`'s zeroization is a stated
  property of the release and cannot be asserted from outside the crate; §14
  says so in place. Implement it.
- **The relay-origin negotiation refusal.** The channel state machine refuses a
  peer that negotiates padding from the relay side; this step checks the *cell*
  type, and the version-byte rejection that makes the refusal possible is
  exercised, but the refusal itself is not.
- **The migration clause** — the client declared stable, the binary API moved
  behind a feature flag. It is a property of the release's manifests and
  documentation, not of any item the driver calls.
