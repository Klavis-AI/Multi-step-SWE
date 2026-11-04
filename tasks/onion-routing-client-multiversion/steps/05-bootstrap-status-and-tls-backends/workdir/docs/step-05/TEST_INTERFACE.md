# Test interface — step 05

Every public item this step's verifier exercises. The checker imports these by
crate and path; a rename or deletion breaks the test.

The driver depends on ten crates by path — `arti-client`, `tor-chanmgr`,
`tor-circmgr`, `tor-config`, `tor-dirmgr`, `tor-linkspec`, `tor-llcrypto`,
`tor-proto`, `tor-rtcompat`, `tor-rtmock` — and two cargo features must exist
and must compile, because the driver's manifest names them:

| Crate | Feature | Why the driver needs it |
| --- | --- | --- |
| `arti-client` | `async-std` | selects the async-std runtime, which this step's backend cases exercise |
| `tor-config` | `expand-paths` | enables path expansion (`shellexpand`/`directories`) |

These are not optional extras. The driver's manifest names them, so cargo
resolves them before a single test runs. A crate that does not DECLARE the
feature - even as an empty entry in its `[features]` table - fails metadata
resolution, and the verifier then fails every group in this step at once,
with no per-group detail to point at the cause.

The driver names no feature of `tor-rtcompat` itself. It reaches
`tor_rtcompat::tokio`, `tor_rtcompat::async_std` and `PreferredRuntime`
through `arti-client`'s own default feature set (tokio + native-tls) plus the
`async-std` feature above.

## What this document overrides

`/app` is cumulative: every earlier step's contract is still on disk at
`/app/docs/step-01/` … `/app/docs/step-04/`, and everything they say still
binds **except where this document contradicts them**. Upstream renamed and
reshaped four things at exactly this release, so where the two disagree, this
document governs:

| Earlier contract says | Step 05 requires instead |
| --- | --- |
| step 04 §8.3 — `Runtime: … + SpawnBlocking + …` | the trait is renamed `BlockOn`; `SpawnBlocking` no longer exists |
| step 04 §8.3 — the free functions `tor_rtcompat::tokio::{create_runtime, current_runtime, test_with_runtime}` and the `pub struct TokioRuntimeHandle` declared beside them | inherent `TokioNativeTlsRuntime::{create, current, run_test}`; all three free functions are gone, and `TokioRuntimeHandle` is no longer nameable outside `tor-rtcompat` |
| step 02 §0.1 — `tor_rtcompat::tokio::create_tokio_runtime()` returning a concrete `async_executors::TokioTp` | the name no longer exists at all; build a runtime with `TokioNativeTlsRuntime::create()` |
| step 02 §3.1 — `arti_client::ConnectPrefs`, whose only isolation setter is `set_isolation_group` and whose recorded token is observable only through `Debug` | the type is renamed `StreamPrefs`, gains `new_isolation_group()` and `isolate_every_stream()`, and what `Debug` shows is a non-`Option` three-state `isolation` enum |

Two statements that earlier contracts already get right are **not** changed
here, and this document repeats them only so that the two agree: step 04's
`MutCfg::map_and_replace` closure bound (`FnOnce(&Arc<T>) -> T`) and step 04's
placement of `listen_tls` on `impl MockNetProvider`.

No earlier contract states `tor_chanmgr::Error`'s variants or their `Display`
strings, and none states the `ChanMgr` bootstrap-threshold rule; the sections
below are the first and only binding statement of both, so there is nothing to
override there.

Everything else in this document is either new at this step or a restatement
that does not conflict with an earlier one.

## arti-client

**Module:** `arti_client::status`

```rust
pub struct BootstrapStatus

impl BootstrapStatus {
    pub fn as_frac(&self) -> f32
    pub fn ready_for_traffic(&self) -> bool
    pub fn blocked(&self) -> Option<Blockage>
}

impl Default for BootstrapStatus
impl Clone for BootstrapStatus
impl Debug for BootstrapStatus
impl Display for BootstrapStatus
// BootstrapStatus is Send + Sync + 'static.

pub struct Blockage

impl Clone for Blockage
impl Debug for Blockage
impl Display for Blockage

#[non_exhaustive]
pub enum BlockageKind {
    Offline,
    Filtering,
    CantReachTor,
}

impl Display for BlockageKind
impl Clone for BlockageKind
impl Debug for BlockageKind

impl From<ConnBlockage> for BlockageKind

pub struct BootstrapEvents

impl Clone for BootstrapEvents
impl Debug for BootstrapEvents
impl Stream for BootstrapEvents {
    type Item = BootstrapStatus;
}
```

`Blockage` has no public constructor: it is handed out only by
`BootstrapStatus::blocked`. Its whole required contract is that it is a
cloneable, `Debug`-printable, `Display`-printable diagnosis, so all three
impls must be present even though no case builds one directly.

`BlockageKind` is `#[non_exhaustive]` and carries no equality impl; it is
compared through its rendered description, never by variant.

**Module:** `arti_client`

```rust
pub struct StreamPrefs

impl StreamPrefs {
    pub fn new() -> Self
    pub fn ipv6_preferred(&mut self) -> &mut Self
    pub fn ipv6_only(&mut self) -> &mut Self
    pub fn ipv4_preferred(&mut self) -> &mut Self
    pub fn ipv4_only(&mut self) -> &mut Self
    pub fn optimistic(&mut self) -> &mut Self
    pub fn set_isolation_group(&mut self, isolation_group: IsolationToken) -> &mut Self
    pub fn new_isolation_group(&mut self) -> &mut Self
    pub fn isolate_every_stream(&mut self) -> &mut Self
}

impl Default for StreamPrefs
impl Clone for StreamPrefs
impl Debug for StreamPrefs

pub struct TorClient<R: Runtime>

impl TorClient<PreferredTokioRuntime> {
    pub async fn bootstrap_with_tokio(config: TorClientConfig) -> Result<Self>
}

impl TorClient<PreferredAsyncStdRuntime> {
    pub async fn bootstrap_with_async_std(config: TorClientConfig) -> Result<Self>
}

impl<R: Runtime> TorClient<R> {
    pub async fn bootstrap(runtime: R, config: TorClientConfig) -> Result<Self>
    pub fn clone_with_prefs(&self, connect_prefs: StreamPrefs) -> Self
    pub fn isolated_client(&self) -> Self
    pub fn bootstrap_status(&self) -> status::BootstrapStatus
    pub fn bootstrap_events(&self) -> status::BootstrapEvents
}

impl<R: Runtime> Clone for TorClient<R>
// TorClient<R> is Send + Sync + 'static for every Runtime R.

pub struct TorClientConfig
impl Default for TorClientConfig
impl Clone for TorClientConfig

pub type Result<T> = std::result::Result<T, Error>;

pub use tor_circmgr::IsolationToken;
```

The two runtime-specific constructors are pinned by the *type* of the future
they return, not by being awaited. `TorClient::bootstrap_with_tokio(cfg)` must
produce a future whose `Output` is
`arti_client::Result<TorClient<tor_rtcompat::tokio::PreferredRuntime>>`, and
`bootstrap_with_async_std(cfg)` one whose `Output` is
`arti_client::Result<TorClient<tor_rtcompat::async_std::PreferredRuntime>>`.
The general `bootstrap(runtime, cfg)` returns
`arti_client::Result<TorClient<R>>` for the runtime `R` it was handed.
Constructing these futures performs no work, so the checker never touches the
network.

### StreamPrefs Debug rendering

`StreamPrefs` exposes no getters. Its recorded state is observed only through
its derived `Debug`, so that rendering is part of the test interface. The
default is:

```
StreamPrefs { ip_ver_pref: Ipv4Preferred, isolation: None, optimistic_stream: false }
```

`ip_ver_pref` renders one of four names — `Ipv4Preferred` (the default),
`Ipv6Preferred`, `Ipv6Only`, `Ipv4Only` — and each is selected by the setter
of the matching name. The setters are last-wins: calling `ipv4_preferred()`
after `ipv4_only()` restores the default rendering exactly, rather than being
a no-op.

`isolation` is a plain three-state enum, **not** an `Option`. `Some(..)` never
appears in this field. Its renderings are:

| after | `isolation` renders as |
| --- | --- |
| nothing (the default) | `None` |
| `isolate_every_stream()` | `EveryStream` |
| `set_isolation_group(tok)` | `Explicit(IsolationToken(<n>))` |
| `new_isolation_group()` | `Explicit(IsolationToken(<n>))`, with a fresh token |

The substrings the checker looks for are literally `isolation: EveryStream`
and `isolation: Explicit(`.

The last of the three setters called wins: `isolate_every_stream()` overrides
an explicit group, and `new_isolation_group()` overrides every-stream mode.
Token numbers come from a process-global counter, so no case asserts on `<n>`
— only on which of the three shapes appears.

> **Overrides step 02.** Step 02's contract (§3.1) documents this type under
> its old name `arti_client::ConnectPrefs`, with `set_isolation_group` as its
> only isolation setter, no every-stream mode, and the derived `Debug` named as
> the only public way to observe the recorded token. The natural field behind
> that contract — and the one the step-02 workspace carries forward — is
> `isolation_group: Option<IsolationToken>`, which renders
> `isolation_group: Some(..)`. That document is still on disk at
> `/app/docs/step-02/TEST_INTERFACE.md` and still binds for everything else it
> says, but at this release the type is `StreamPrefs`, the field is
> `isolation`, it is **not** an `Option`, and it has three states rather than
> two. Keeping step 02's shape fails the `isolation` and `streamprefs` groups
> on the `Debug` substrings above. Where the two documents disagree, this one
> governs.

`optimistic_stream` renders `false` by default and `true` after `optimistic()`.
Every setter returns `&mut Self` so the calls chain, and the value cloned from
a `StreamPrefs` is independent of the original. Three further renderings are
asserted verbatim:

```
StreamPrefs { ip_ver_pref: Ipv6Preferred, isolation: None, optimistic_stream: false }
StreamPrefs { ip_ver_pref: Ipv4Preferred, isolation: None, optimistic_stream: true }
StreamPrefs { ip_ver_pref: Ipv6Only, isolation: None, optimistic_stream: true }
```

### Isolation tokens

```rust
pub struct IsolationToken(u64)   // re-exported from tor_circmgr

impl IsolationToken {
    pub fn new() -> Self
    pub fn no_isolation() -> Self
}

impl Copy for IsolationToken
impl Clone for IsolationToken
impl Debug for IsolationToken
impl PartialEq for IsolationToken
impl Eq for IsolationToken
impl PartialOrd for IsolationToken
impl Ord for IsolationToken
```

- Every `IsolationToken::new()` is distinct from every other, and equal to
  itself.
- `IsolationToken::no_isolation()` is a fixed sentinel: two calls compare
  equal to each other, and neither equals any token from `new()`.

## tor-chanmgr

```rust
pub struct ConnStatus {
    online: Option<bool>,     // private
    tls_works: Option<bool>,  // private
}

impl ConnStatus {
    pub fn usable(&self) -> bool
    pub fn frac(&self) -> f32
    pub fn blockage(&self) -> Option<ConnBlockage>
}

impl Default for ConnStatus
impl Clone for ConnStatus
impl Debug for ConnStatus
impl Display for ConnStatus

#[non_exhaustive]
pub enum ConnBlockage {
    NoTcp,
    NoHandshake,
}

impl Display for ConnBlockage
impl Clone for ConnBlockage
impl Debug for ConnBlockage
impl PartialEq for ConnBlockage
impl Eq for ConnBlockage

pub struct ConnStatusEvents

impl Clone for ConnStatusEvents
impl Debug for ConnStatusEvents
impl Stream for ConnStatusEvents {
    type Item = ConnStatus;
}

pub struct ChanMgr<R: Runtime>

impl<R: Runtime> ChanMgr<R> {
    pub fn new(runtime: R) -> Self
    pub async fn get_or_launch<T: ChanTarget + ?Sized>(&self, target: &T) -> Result<Channel>
    pub fn bootstrap_events(&self) -> ConnStatusEvents
}

#[non_exhaustive]
pub enum Error {
    UnusableTarget(String),
    PendingFailed,
    ChanTimeout,
    Proto(tor_proto::Error),       // #[from]
    Io(Arc<std::io::Error>),       // #[source]
    Internal(&'static str),
}

impl Display for Error
impl Clone for Error
impl Debug for Error
impl std::error::Error for Error

impl From<std::io::Error> for Error          // wraps into Error::Io(Arc::new(e))
impl From<tor_proto::Error> for Error
impl From<futures::task::SpawnError> for Error
impl From<tor_rtcompat::TimeoutError> for Error
impl<T> From<std::sync::PoisonError<T>> for Error

pub type Result<T> = std::result::Result<T, Error>;
```

`ChanMgr::new` is the only constructor at this step; there is no builder.

### `tor_chanmgr::Error` Display strings

These are the strings; `{0}` is the wrapped value's own `Display`. **Five of
the six rows are asserted as strings** — `UnusableTarget`, `PendingFailed`,
`ChanTimeout`, `Io` and `Internal` — and the sixth, `Proto(e)`, is checked by
variant only: no case in this step renders it, because no case can reach it
without a live relay. Implement it exactly as written all the same; it is part
of the contract and later steps render it.

| Variant | Rendered as |
| --- | --- |
| `UnusableTarget(s)` | `Target was unusable: {s}` |
| `PendingFailed` | `Pending channel failed to launch` |
| `ChanTimeout` | `Channel timed out` |
| `Proto(e)` | `Protocol error while opening a channel: {e}` |
| `Io(e)` | `I/O error while opening a channel: {e}` |
| `Internal(s)` | `Internal error: {s}` |

So `Error::from(std::io::Error::new(ErrorKind::Other, "nope")).to_string()` is
`"I/O error while opening a channel: nope"`, and an `UnusableTarget` raised
for a relay with no addresses renders
`"Target was unusable: No addresses for chosen relay"`.

The three `From` impls that are not simple wrappers convert as follows:
`futures::task::SpawnError` becomes
`Error::Internal("Couldn't spawn channel reactor")`, a
`tor_rtcompat::TimeoutError` becomes `Error::ChanTimeout`, and a poisoned
lock becomes `Error::Internal("Thread failed while holding lock")`.

> **Forward note (out of scope here).** `tor_chanmgr::Error` is restructured at
> step 06: `UnusableTarget` and `Internal` come to carry a `tor_error::Bug`
> instead of a string, `Io` becomes a struct variant with `peer`, `action` and
> `source` fields, and a `Spawn` variant is added. Nothing in step 05
> constructs those shapes, and step 05 checks only the shapes above.

Observable Debug rendering for `ConnStatusEvents`:
```
ConnStatusEvents { .. }
```

## tor-dirmgr

```rust
pub use event::{DirBootstrapEvents, DirBootstrapStatus, DirEvent, DirStatus};

pub struct DirBootstrapStatus

impl DirBootstrapStatus {
    pub fn frac_at(&self, when: SystemTime) -> f32
    pub fn usable_at(&self, now: SystemTime) -> bool
}

impl Default for DirBootstrapStatus
impl Clone for DirBootstrapStatus
impl Debug for DirBootstrapStatus
impl Display for DirBootstrapStatus

pub struct DirStatus

impl Default for DirStatus
impl Clone for DirStatus
impl Debug for DirStatus
impl Display for DirStatus

#[non_exhaustive]
pub enum DirEvent {
    NewConsensus,
    NewDescriptors,
}

impl Copy for DirEvent
impl Clone for DirEvent
impl Debug for DirEvent
impl PartialEq for DirEvent
impl Eq for DirEvent
// DirEvent is Send + Sync + 'static.

pub struct DirBootstrapEvents

impl Clone for DirBootstrapEvents
impl Debug for DirBootstrapEvents
impl Stream for DirBootstrapEvents {
    type Item = DirBootstrapStatus;
}
// DirBootstrapEvents is Send + Sync + 'static.

pub struct DirMgr<R: Runtime>

impl<R: Runtime> DirMgr<R> {
    pub fn bootstrap_events(&self) -> DirBootstrapEvents
}
```

`DirBootstrapEvents` carries `DirBootstrapStatus`, not `DirEvent`. The two
types live side by side: `DirEvent` is the coarse "something changed"
vocabulary, while the stream published by `DirMgr::bootstrap_events` reports
progress. The checker pins the association both by a `Stream<Item = _>` bound
and by comparing `type_name`s, so an implementation that streams `DirEvent`
fails even though it compiles.

`DirMgr` cannot be constructed without a store and a network, so
`bootstrap_events` is pinned by coercing it to
`fn(&DirMgr<TokioNativeTlsRuntime>) -> DirBootstrapEvents` rather than by
being called. Its receiver and return type must match exactly.

## tor-circmgr

```rust
pub struct PreemptiveCircuitConfig

impl PreemptiveCircuitConfig {
    pub fn builder() -> PreemptiveCircuitConfigBuilder
}

impl Default for PreemptiveCircuitConfig
impl Clone for PreemptiveCircuitConfig
impl Debug for PreemptiveCircuitConfig
impl PartialEq for PreemptiveCircuitConfig
impl Eq for PreemptiveCircuitConfig
impl<'de> Deserialize<'de> for PreemptiveCircuitConfig   // #[serde(deny_unknown_fields)]

pub struct PreemptiveCircuitConfigBuilder

impl Default for PreemptiveCircuitConfigBuilder
impl PreemptiveCircuitConfigBuilder {
    pub fn build(&self) -> Result<PreemptiveCircuitConfig, ConfigBuildError>
    pub fn disable_at_threshold(&mut self, val: usize) -> &mut Self
    pub fn initial_predicted_ports(&mut self, val: Vec<u16>) -> &mut Self
    pub fn prediction_lifetime(&mut self, val: Duration) -> &mut Self
    pub fn min_exit_circs_for_port(&mut self, val: usize) -> &mut Self
}

impl From<PreemptiveCircuitConfig> for PreemptiveCircuitConfigBuilder

pub struct CircuitTiming

impl CircuitTiming {
    pub fn builder() -> CircuitTimingBuilder
}

impl Default for CircuitTiming
impl Clone for CircuitTiming
impl Debug for CircuitTiming
impl PartialEq for CircuitTiming
impl Eq for CircuitTiming
impl<'de> Deserialize<'de> for CircuitTiming              // #[serde(deny_unknown_fields)]

pub struct CircuitTimingBuilder

impl Default for CircuitTimingBuilder
impl CircuitTimingBuilder {
    pub fn build(&self) -> Result<CircuitTiming, ConfigBuildError>
    pub fn max_dirtiness(&mut self, val: Duration) -> &mut Self
    pub fn request_timeout(&mut self, val: Duration) -> &mut Self
    pub fn request_max_retries(&mut self, val: u32) -> &mut Self
    pub fn request_loyalty(&mut self, val: Duration) -> &mut Self
}

impl From<CircuitTiming> for CircuitTimingBuilder
```

**`build()` is fallible.** Both builders are `derive_builder` builders carrying
`#[builder(build_fn(error = "ConfigBuildError"))]`, so `build()` returns
`Result<_, tor_config::ConfigBuildError>` and the checker writes
`.build().expect(..)`. A `build()` that returns the bare struct does not
compile against the tests. Every field has a `#[builder(default = ..)]`, so
`Builder::default().build()` succeeds and equals the type's `Default`.

Both types deserialize from TOML with `#[serde(deny_unknown_fields)]`: an
omitted key falls back to the documented default, and a misspelled key is an
error whose `Display` names the offending key. Every `Duration` field uses
`humantime_serde`, so TOML writes them the way a human does — `"1 hour"`,
`"10 minutes"`, `"50ms"`, `"90s"` — not as integers.

Observable `PreemptiveCircuitConfig::default()` Debug rendering:
```
PreemptiveCircuitConfig { disable_at_threshold: 12, initial_predicted_ports: [80, 443], prediction_lifetime: 3600s, min_exit_circs_for_port: 2 }
```

Observable `CircuitTiming::default()` Debug rendering:
```
CircuitTiming { max_dirtiness: 600s, request_timeout: 60s, request_max_retries: 32, request_loyalty: 50ms }
```

## tor-config

```rust
pub struct ConfigBuildError      // the derive_builder error type for the two builders above
impl Display for ConfigBuildError
impl Debug for ConfigBuildError
impl std::error::Error for ConfigBuildError

#[non_exhaustive]
pub enum Reconfigure {
    AllOrNothing,
    WarnOnFailures,
    CheckAllOrNothing,
}

impl Reconfigure {
    pub fn cannot_change<S: AsRef<str>>(self, field: S) -> Result<(), ReconfigureError>
}

impl Copy for Reconfigure
impl Clone for Reconfigure
impl Debug for Reconfigure
impl PartialEq for Reconfigure

#[non_exhaustive]
pub enum ReconfigureError {
    CannotChange { field: String },
}

impl Display for ReconfigureError
impl Debug for ReconfigureError

pub struct MutCfg<T>

impl<T> MutCfg<T> {
    pub fn new(config: T) -> Self
    pub fn get(&self) -> Arc<T>
    pub fn replace(&self, new_config: T)
    pub fn check_and_replace(&self, old_config: &Arc<T>, new_config: T) -> bool
    pub fn map_and_replace<F>(&self, func: F)
    where
        F: FnOnce(&Arc<T>) -> T
}

impl<T> Default for MutCfg<T> where T: Default
impl<T> From<T> for MutCfg<T>
impl<T> Debug for MutCfg<T>
```

**`map_and_replace` hands its closure an `&Arc<T>`, not an `&T`.** The closure
bound is `FnOnce(&Arc<T>) -> T`, so a caller reaching the value writes `**old`
for a `MutCfg<u32>`. A signature taking `&T` does not compile against the
tests.

## tor-rtcompat

**Module:** `tor_rtcompat` (crate root)

```rust
pub use traits::{
    BlockOn, CertifiedConn, Runtime, SleepProvider, TcpListener, TcpProvider, TlsProvider,
};
pub use timer::{SleepProviderExt, Timeout, TimeoutError};
pub use compound::CompoundRuntime;
pub use preferred_backend_mod::PreferredRuntime;   // == tokio::PreferredRuntime

pub trait BlockOn {
    fn block_on<F: Future>(&self, future: F) -> F::Output;
}

pub trait Runtime:
    Sync + Send + Spawn + BlockOn + Clone + SleepProvider + TcpProvider
    + TlsProvider<Self::TcpStream> + 'static
{
}

// `Spawn` above and below — wherever this document writes it without a path —
// is `futures::task::Spawn`, the trait `futures::task::SpawnExt` is
// blanket-implemented over. Its one method is
// `fn spawn_obj(&self, future: FutureObj<'static, ()>) -> Result<(),
// futures::task::SpawnError>`, and `futures::task::SpawnExt::spawn` — the call
// the driver makes on a runtime — applies to a value if and only if its type
// implements *that* trait. A workspace that declares a `Spawn` trait of its own
// and implements that for its runtimes instead is one the driver's `spawn` call
// does not compile against. It is not among the names re-exported above, and
// deliberately so: `Spawn` is reached through `futures::task`, and
// `tor_rtcompat` neither declares nor re-exports it.

// A blanket impl gives `Runtime` to every type that satisfies those bounds;
// no implementor writes `impl Runtime for _` by hand. The `Self` inside
// `TlsProvider<Self::TcpStream>` in the where-clause below resolves to `T`;
// that is how upstream writes it and it compiles, so do not "fix" it to `T`.
impl<T> Runtime for T where T: Sync + Send + Spawn + BlockOn + Clone + SleepProvider
    + TcpProvider + TlsProvider<Self::TcpStream> + 'static {}

pub trait SleepProvider {
    type SleepFuture: Future<Output = ()> + Send + 'static;
    fn sleep(&self, duration: Duration) -> Self::SleepFuture;
    fn now(&self) -> Instant { .. }
    fn wallclock(&self) -> SystemTime { .. }
    fn block_advance<T: Into<String>>(&self, _reason: T) {}
    fn release_advance<T: Into<String>>(&self, _reason: T) {}
    fn allow_one_advance(&self, _dur: Duration) {}
}

#[async_trait]
pub trait TcpProvider {
    type TcpStream: AsyncRead + AsyncWrite + Send + Sync + Unpin + 'static;
    type TcpListener: TcpListener<TcpStream = Self::TcpStream> + Send + Sync + Unpin + 'static;
    async fn connect(&self, addr: &SocketAddr) -> io::Result<Self::TcpStream>;
    async fn listen(&self, addr: &SocketAddr) -> io::Result<Self::TcpListener>;
}

#[async_trait]
pub trait TcpListener {
    type TcpStream: AsyncRead + AsyncWrite + Send + Sync + Unpin + 'static;
    type Incoming: stream::Stream<Item = io::Result<(Self::TcpStream, SocketAddr)>> + Unpin;
    async fn accept(&self) -> io::Result<(Self::TcpStream, SocketAddr)>;
    fn incoming(self) -> Self::Incoming;
    fn local_addr(&self) -> io::Result<SocketAddr>;
}

pub trait CertifiedConn {
    fn peer_certificate(&self) -> io::Result<Option<Vec<u8>>>;
}

pub trait TlsProvider<S> {
    type Connector: TlsConnector<S, Conn = Self::TlsStream> + Send + Sync + Unpin;
    type TlsStream: AsyncRead + AsyncWrite + CertifiedConn + Unpin + Send + 'static;
    fn tls_connector(&self) -> Self::Connector;
}

pub struct CompoundRuntime<SpawnR, SleepR, TcpR, TlsR>

impl<SpawnR, SleepR, TcpR, TlsR> CompoundRuntime<SpawnR, SleepR, TcpR, TlsR> {
    pub fn new(spawn: SpawnR, sleep: SleepR, tcp: TcpR, tls: TlsR) -> Self
}

impl<..> Clone for CompoundRuntime<..>
impl<..> Debug for CompoundRuntime<..>
impl<..> futures::task::Spawn for CompoundRuntime<..> where SpawnR: Spawn
impl<..> BlockOn    for CompoundRuntime<..> where SpawnR: BlockOn
impl<..> SleepProvider for CompoundRuntime<..> where SleepR: SleepProvider

#[async_trait]
impl<SpawnR, SleepR, TcpR, TlsR> TcpProvider
    for CompoundRuntime<SpawnR, SleepR, TcpR, TlsR>
where
    TcpR: TcpProvider,
    SpawnR: Send + Sync + 'static,
    SleepR: Send + Sync + 'static,
    TcpR: Send + Sync + 'static,
    TlsR: Send + Sync + 'static,
{
    type TcpStream = TcpR::TcpStream;
    type TcpListener = TcpR::TcpListener;
}

impl<SpawnR, SleepR, TcpR, TlsR> TlsProvider<TcpR::TcpStream>
    for CompoundRuntime<SpawnR, SleepR, TcpR, TlsR>
where TcpR: TcpProvider, TlsR: TlsProvider<TcpR::TcpStream>
{
    type Connector = TlsR::Connector;
    type TlsStream = TlsR::TlsStream;
}

pub fn current_user_runtime() -> std::io::Result<impl Runtime>
pub fn create_runtime()       -> std::io::Result<impl Runtime>

pub mod tls {
    pub use crate::traits::{CertifiedConn, TlsConnector};

    pub trait TlsConnector<S> {
        type Conn: AsyncRead + AsyncWrite + CertifiedConn + Unpin + Send + 'static;
        async fn negotiate_unvalidated(&self, stream: S, sni_hostname: &str)
            -> io::Result<Self::Conn>;      // #[async_trait]
    }
}
```

`TcpProvider` is the only one of `CompoundRuntime`'s seven trait impls that
is `#[async_trait]`, and its four `Send + Sync + 'static` component bounds are
load-bearing, not decoration: `#[async_trait]` rewrites `connect` and `listen`
into methods returning a boxed `Send` future that borrows `&self`, which
requires `Self: Sync + 'static`, and a `CompoundRuntime` is `Sync + 'static`
only when all four of its components are. Writing the impl with
`where TcpR: TcpProvider` alone does not compile: rustc reports an unsatisfied
bound (`E0277`) on the future `#[async_trait]` generates. The other six impls
(`Clone`, `Debug`, `futures::task::Spawn`, `BlockOn`, `SleepProvider` and
`TlsProvider`) are **not** `#[async_trait]` and carry exactly the bounds shown
above and no more; do not add `Send + Sync + 'static` to those.

**A `CompoundRuntime` is spawnable when its spawn half is, and the driver spawns
onto it.** It calls `composed.spawn(async move { … })` — that is
`futures::task::SpawnExt::spawn`, which applies to `CompoundRuntime` through the
impl above and through nothing else — and then observes the task's effect on the
other side of a `block_on`. Two things follow and both are checked: the composed
runtime implements `futures::task::Spawn` whenever `SpawnR` does (that is what
the `where SpawnR: Spawn` clause on that impl is for), and its `spawn_obj`
forwards to the **spawn component**, so the task is handed to `SpawnR`'s own
executor rather than run inline or dropped. A `CompoundRuntime` that composes
`spawn`, `sleep`, `tcp` and `tls` but does not implement
`futures::task::Spawn` — or implements a same-named trait of its own — compiles
here and fails the driver's call site (`E0599`/`E0034`).

`TlsProvider<S>` and `TlsConnector<S>` put **no bound on `S`**; the bounds
live on the associated types. `CertifiedConn` and `TlsConnector` are reachable
through `tor_rtcompat::tls::{..}`, and `CertifiedConn` is additionally
re-exported at the crate root.

Both free functions return `std::io::Result<impl Runtime>` — an opaque
runtime, not a named type. Code that needs a named type calls a constructor
below instead.

### Runtime constructors

**This is the vocabulary that changed at this step, and it is the single most
common way to get step 05 wrong — because the document that teaches the losing
answer is still on disk and still binding for everything else it says.**

Two inherited documents teach the pre-v0.0.4 spelling. Step 04's contract, at
`/app/docs/step-04/TEST_INTERFACE.md` §8.3, gives a `Runtime` supertrait list
containing `SpawnBlocking`, and three module-level *free functions* in
`tor_rtcompat::tokio` — `create_runtime`, `current_runtime` and
`test_with_runtime` — beside a `pub struct TokioRuntimeHandle`. Step 02's
contract, at `/app/docs/step-02/TEST_INTERFACE.md` §0.1, adds a fourth
function: `tor_rtcompat::tokio::create_tokio_runtime()`, returning a concrete
`async_executors::TokioTp`. All of it goes:

```rust
// The inherited spelling — SUPERSEDED AT THIS RELEASE. None of these names is
// reachable from outside `tor-rtcompat` in a correct step-05 tree; do not
// carry them forward.
//
// Provenance: everything but the last line is copied verbatim from step 04
// §8.3, minus its explanatory comments and minus its `impl<T> Runtime for T`
// blanket-impl line — that line is NOT superseded and survives at this
// release. The last line is copied verbatim from step 02 §0.1.
//
// What goes away here is the *names*, not a return type, and within the
// `Runtime` supertrait list only `SpawnBlocking` changes; the remaining bounds
// survive and are restated in the `## tor-rtcompat` block above. Note in
// particular that step 04 does NOT type the free functions as returning
// `TokioRuntimeHandle` — it writes `impl Runtime`, and §8.3 says in so many
// words that `TokioRuntimeHandle` "is not the return type of either function,
// and writing it as one narrows the contract". That reading of step 04 was
// never correct and is not what is being superseded here.
pub trait Runtime:
    Sync + Send + Spawn + SpawnBlocking + Clone
    + SleepProvider + TcpProvider + TlsProvider<Self::TcpStream> + 'static
{}

pub mod tokio {
    pub struct TokioRuntimeHandle { /* private */ }
    impl TokioRuntimeHandle {
        pub fn new(handle: tokio_crate::runtime::Handle) -> Self;
    }

    pub fn create_runtime() -> std::io::Result<impl Runtime>;
    pub fn current_runtime() -> std::io::Result<impl Runtime>;
    pub fn test_with_runtime<P, F, O>(func: P) -> O
        where P: FnOnce(async_executors::TokioTp) -> F,
              F: futures::Future<Output = O>;
    pub fn create_tokio_runtime() -> std::io::Result<async_executors::TokioTp>;  // step 02 §0.1
}
```

**Where step 04 §8.3, step 02 §0.1 and this section disagree, this section
governs.** The `SpawnBlocking` trait is renamed `BlockOn` — `SpawnBlocking` is
not declared anywhere in a step-05 tree, and this step's `blockon` group
asserts the new name. `create_tokio_runtime` is gone outright; the free
functions in `tor_rtcompat::tokio` are gone; and `TokioRuntimeHandle` survives
only as a private implementation detail of the crate (the module holding it is
`pub(crate)`), so no caller outside `tor-rtcompat` can name it. Replaced as
follows:

| inherited name (step 04 §8.3 or step 02 §0.1) | step 05 |
| --- | --- |
| `trait SpawnBlocking` | `trait BlockOn` (same single method) |
| `tokio::create_tokio_runtime()` | `TokioNativeTlsRuntime::create()` |
| `tokio::create_runtime()` | `TokioNativeTlsRuntime::create()` |
| `tokio::current_runtime()` | `TokioNativeTlsRuntime::current()` |
| `tokio::test_with_runtime(f)` | `TokioNativeTlsRuntime::run_test(f)` |
| `struct TokioRuntimeHandle` | `struct TokioNativeTlsRuntime` (and `AsyncStdNativeTlsRuntime`) |

Note that `tor_rtcompat::current_user_runtime()` and
`tor_rtcompat::create_runtime()` at the **crate root** are unaffected: they
survive unchanged and are listed above.

The constructors become **inherent associated functions on a named runtime
type**, one type per (async backend, TLS backend) pair:

```rust
pub mod tokio {
    pub struct TokioNativeTlsRuntime { .. }

    impl TokioNativeTlsRuntime {
        pub fn create() -> std::io::Result<Self>
        pub fn current() -> std::io::Result<Self>
        pub fn run_test<P, F, O>(func: P) -> O
        where
            P: FnOnce(Self) -> F,
            F: futures::Future<Output = O>;
    }

    impl From<tokio::runtime::Handle> for TokioNativeTlsRuntime
    impl Clone for TokioNativeTlsRuntime
    impl Debug for TokioNativeTlsRuntime
    // Runtime is obtained through the blanket impl, via Spawn + BlockOn +
    // SleepProvider + TcpProvider + TlsProvider. `Spawn` there is
    // `futures::task::Spawn`, so `TokioNativeTlsRuntime` is spawnable — and
    // `futures::task::SpawnExt::spawn` applies to it — exactly as it is for a
    // `CompoundRuntime`.

    pub use TokioNativeTlsRuntime as PreferredRuntime;   // with feature `native-tls`
}

pub mod async_std {
    pub struct AsyncStdNativeTlsRuntime { .. }

    impl AsyncStdNativeTlsRuntime {
        pub fn create() -> std::io::Result<Self>
        pub fn current() -> std::io::Result<Self>
        pub fn run_test<P, F, O>(func: P) -> O
        where
            P: FnOnce(Self) -> F,
            F: futures::Future<Output = O>;
    }

    impl Clone for AsyncStdNativeTlsRuntime
    impl Debug for AsyncStdNativeTlsRuntime

    pub use AsyncStdNativeTlsRuntime as PreferredRuntime;   // with feature `native-tls`
}
```

Semantics the checker relies on:

- `create()` builds and **owns** a fresh backend runtime; the underlying
  runtime is dropped when the last clone of the handle is. It succeeds with
  no ambient runtime installed.
- `current()` wraps the **currently running** backend runtime. For
  `TokioNativeTlsRuntime` this means: called with no ambient runtime it
  returns `Err`; called from inside one — for example inside a `block_on`
  driven by a runtime from `create()` — it returns `Ok`. That `Err` is the
  tokio constructor's rule only. `AsyncStdNativeTlsRuntime::current()` has no
  ambient executor to look for, because async-std's is global; it delegates
  straight to `create()` and therefore always returns `Ok`. No case exercises
  it, and the rejection list below names only `TokioNativeTlsRuntime::current()`
  for that reason.
- `run_test(func)` is the test helper: it creates a runtime, hands a clone to
  `func`, and `block_on`s the returned future, returning the future's output.
  It **panics** rather than returning a `Result` if the runtime cannot be
  created. Most of this step's asynchronous cases are written as
  `TokioNativeTlsRuntime::run_test(|rt| async move { .. })`.
- `tor_rtcompat::current_user_runtime()` follows the same ambient rule as
  `current()`: `Err` outside a runtime, `Ok` inside one.
- `tor_rtcompat::PreferredRuntime` is `tokio::PreferredRuntime`, i.e.
  `TokioNativeTlsRuntime`, whenever the tokio feature is on; the two backend
  modules must name *different* preferred runtimes.

The crate also declares a `rustls` cargo feature, under which
`tokio::TokioRustlsRuntime` and `async_std::AsyncStdRustlsRuntime` appear with
the same `create`/`current`/`run_test` surface and become `PreferredRuntime`
when `native-tls` is off. **The driver does not enable `rustls`, and no group
in this step exercises it**, so the rustls types are out of scope here: the
required behaviour is that the TLS backend is *selectable by runtime type*
rather than hard-wired, which the native-tls pair plus `CompoundRuntime`
already demonstrates.

Observable Debug renderings:
```
TokioNativeTlsRuntime { .. }
AsyncStdNativeTlsRuntime { .. }
CompoundRuntime { .. }
```

## tor-rtmock

**Module:** `tor_rtmock` (crate root)

```rust
pub mod io;
pub mod net;
pub mod time;

pub use net_runtime::MockNetRuntime;
pub use sleep_runtime::MockSleepRuntime;

pub struct MockNetRuntime<R: Runtime>

impl<R: Runtime> MockNetRuntime<R> {
    pub fn new(runtime: R, net: MockNetProvider) -> Self
    pub fn inner(&self) -> &R
    pub fn mock_net(&self) -> &MockNetProvider
}

impl<R: Runtime> futures::task::Spawn for MockNetRuntime<R>
impl<R: Runtime> BlockOn        for MockNetRuntime<R>
impl<R: Runtime> SleepProvider  for MockNetRuntime<R>
impl<R: Runtime> TcpProvider    for MockNetRuntime<R>
impl<R: Runtime> TlsProvider<LocalStream> for MockNetRuntime<R>
impl<R: Runtime> Clone          for MockNetRuntime<R>
```

`MockNetRuntime` is exported from the **crate root**, not from
`tor_rtmock::net`; the checker writes `use tor_rtmock::MockNetRuntime;`. Because
it implements the whole provider set it satisfies the `Runtime` bound through
the blanket impl, so it can be driven by `BlockOn::block_on` like any other
runtime. The `Spawn` impl is `futures::task::Spawn`, as everywhere else in this
document: `MockSleepRuntime` and `MockNetRuntime` are the two runtimes the
driver's `SpawnExt::spawn` calls reach, so both forward `spawn_obj` to the
runtime they wrap. A mock runtime that implements a crate-local `Spawn` instead
does not compile against the driver.

**Module:** `tor_rtmock::net`

```rust
pub struct MockNetwork

impl MockNetwork {
    pub fn new() -> Arc<Self>
    pub fn builder(self: &Arc<Self>) -> ProviderBuilder
}

pub struct ProviderBuilder

impl ProviderBuilder {
    pub fn add_address(&mut self, addr: IpAddr) -> &mut Self
    pub fn runtime<R: Runtime>(&self, runtime: R) -> MockNetRuntime<R>
    pub fn provider(&self) -> MockNetProvider
}

pub struct MockNetProvider

impl MockNetProvider {
    pub fn listen_tls(&self, addr: &SocketAddr, tls_cert: Vec<u8>)
        -> io::Result<MockNetListener>
}

impl TcpProvider for MockNetProvider {
    type TcpStream = LocalStream;
    type TcpListener = MockNetListener;
}
impl TlsProvider<LocalStream> for MockNetProvider {
    type Connector = MockTlsConnector;
    type TlsStream = MockTlsStream;
}
impl Clone for MockNetProvider

pub struct MockNetListener
impl TcpListener for MockNetListener
impl Stream     for MockNetListener

pub struct MockTlsConnector
impl TlsConnector<LocalStream> for MockTlsConnector { type Conn = MockTlsStream; }
impl Clone for MockTlsConnector

pub struct MockTlsStream
impl CertifiedConn for MockTlsStream
impl AsyncRead     for MockTlsStream
impl AsyncWrite    for MockTlsStream

#[non_exhaustive]
pub enum MockNetError {
    BadOp,     // Display: "Invalid operation on mock network"
}
impl Clone for MockNetError
impl Debug for MockNetError
impl Display for MockNetError
impl std::error::Error for MockNetError
```

**`listen_tls` is a method on `MockNetProvider`, not on `MockNetwork`.** A
`MockNetwork` hands out providers through `builder()`; a provider owns the
addresses it may bind, and every call site in this step reads
`provider.listen_tls(&addr, cert)`. Plain (non-TLS) listening is
`TcpProvider::listen` on the same provider.

**Module:** `tor_rtmock::io`

```rust
pub struct LocalStream

impl AsyncRead  for LocalStream
impl AsyncWrite for LocalStream

pub fn stream_pair() -> (LocalStream, LocalStream)
```

A `LocalStream` produced by the plain (non-TLS) path is a working in-memory
byte pipe: bytes written to one end with `AsyncWriteExt::write_all` are read
from the other with `AsyncReadExt::read_exact`, in order.

## tor-linkspec

```rust
pub trait ChanTarget {
    fn addrs(&self) -> &[SocketAddr];
    fn ed_identity(&self) -> &Ed25519Identity;
    fn rsa_identity(&self) -> &RsaIdentity;
}

pub struct OwnedChanTarget

impl OwnedChanTarget {
    pub fn new(
        addrs: Vec<SocketAddr>,
        ed_identity: tor_llcrypto::pk::ed25519::Ed25519Identity,
        rsa_identity: tor_llcrypto::pk::rsa::RsaIdentity,
    ) -> Self
}

impl ChanTarget for OwnedChanTarget
impl Clone for OwnedChanTarget
impl Debug for OwnedChanTarget
```

`OwnedChanTarget::new` takes its three parts positionally and in that order.
It is the only way the checker builds something to hand to
`ChanMgr::get_or_launch`, including the empty-address case that must produce
`Error::UnusableTarget`.

## tor-llcrypto

```rust
pub mod pk {
    pub mod ed25519 {
        pub struct Ed25519Identity
        impl From<[u8; 32]> for Ed25519Identity
        impl Clone for Ed25519Identity
        impl Debug for Ed25519Identity
        impl PartialEq for Ed25519Identity
    }
    pub mod rsa {
        pub struct RsaIdentity
        impl From<[u8; 20]> for RsaIdentity
        impl Clone for RsaIdentity
        impl Debug for RsaIdentity
        impl PartialEq for RsaIdentity
    }
}
```

The identity widths are fixed: 32 bytes for ed25519, 20 for RSA. The checker
builds test identities with `Ed25519Identity::from([0x21; 32])` and
`RsaIdentity::from([0x43; 20])`, so both `From<[u8; N]>` impls must exist at
exactly those widths.

## tor-proto

```rust
pub mod channel {
    pub struct Channel
}
```

## Behaviour

**Bootstrap status composition:**
- `BootstrapStatus::as_frac() = conn_status.frac() * 0.15 + dir_status.frac_at(now) * 0.85`
- `BootstrapStatus::ready_for_traffic() = conn_status.usable() && dir_status.usable_at(now)`
- `BootstrapStatus::blocked()` returns `Some(Blockage)` wrapping
  `BlockageKind::from(conn_status.blockage()?)` if the connection status
  reports blockage; `None` otherwise.
- `BootstrapStatus::default()` therefore has `as_frac() == 0.0`,
  `ready_for_traffic() == false` and `blocked() == None`, and its `Debug`
  rendering names the type.

**Connection status fractions:**
- `frac()` is `0.0` when neither `online` nor `tls_works` is `Some(true)`,
  `0.5` when only `online` is `Some(true)`, and `1.0` when both are.
- `usable()` is true **only when both are `Some(true)`** — it is the same
  condition `frac() == 1.0` names. A connection that has opened but not
  completed its Tor channel handshake is *not* usable, which is why the
  half-open state below is a state a caller can be told about rather than one
  it can build on. `ready_for_traffic()` composes this with the directory
  half, so "usable" here means "this half of the bootstrap is done".
- `ConnStatus::default()` is `online: None, tls_works: None`, so
  `frac() == 0.0`, `usable() == false` and `blockage() == None`. "No progress
  yet" and "blocked" are distinguishable states.
- A `ChanMgr` that has completed one TCP connection but no channel handshake
  publishes `frac() == 0.5` and `usable() == false`. That is the state this
  step's channel cases observe, and a case in scope asserts the `usable()`
  answer there as well as the rendering.

**Connection status display strings.** `ConnStatus`'s fields are private, so
only the default is directly constructible; the rest are reached through a
`ChanMgr`. All five renderings are part of the implementation:

| state | `Display` |
| --- | --- |
| `online: None` | `connecting to the internet` |
| `online: Some(false)` | `unable to connect to the internet` |
| `online: Some(true), tls_works: None` | `handshaking with Tor relays` |
| `online: Some(true), tls_works: Some(false)` | `unable to handshake with Tor relays` |
| `online: Some(true), tls_works: Some(true)` | `connecting successfully` |

Rows one and three are asserted as strings at this step. Row one is asserted
directly by `connstatus` and `connevents`, by `chanevents` for a status whose
connection attempt was refused, and as the leading clause of the composed
default rendering by `bootstat` and `clientapi`. Row three is asserted by
`chanevents` for the state described two paragraphs above — one completed TCP
connection, no channel handshake, `frac() == 0.5`. The remaining three
renderings, and `frac() == 1.0`, describe states this step's cases cannot
reach through the public API and are **not separately checked here**. (Row
two's string is also `ConnBlockage::NoTcp`'s `Display` and is checked in that
role by `connstatus`; it is not asserted as a `ConnStatus` rendering.)

**Connection blockage display strings:**
- `ConnBlockage::NoTcp`: `"unable to connect to the internet"`
- `ConnBlockage::NoHandshake`: `"our internet connection seems to be filtered"`
- The two variants compare unequal, clone, and `Debug`-render as their bare
  variant names (`"NoHandshake"`).

**Blockage kind display strings:**
- `BlockageKind::Offline`: `"We seem to be offline"`
- `BlockageKind::Filtering`: `"Our internet connection seems filtered"`
- `BlockageKind::CantReachTor`: `"Can't reach the Tor network"`
- All three are distinct, and `Debug` renders the bare variant name
  (`"CantReachTor"`).
- `BlockageKind::from(ConnBlockage::NoTcp)` is `Offline`;
  `BlockageKind::from(ConnBlockage::NoHandshake)` is `Filtering`. The
  conversion is a public `From` impl, so `Into` works too.

**Bootstrap status default display:**
```
0%: connecting to the internet; directory is fetching a consensus
```

**Directory status displays.** `DirStatus::default()` renders
`fetching a consensus`; `DirBootstrapStatus::default()` renders
`directory is fetching a consensus`. A freshly created `DirBootstrapStatus`
has no next directory in flight, so its rendering must not contain the
substring `next directory`, and its `Debug` rendering names the type.

`DirBootstrapStatus::frac_at(when)` and `usable_at(now)` are functions of the
caller-supplied `SystemTime`, never of the wall clock: a default value must
answer `0.0` and `false` at every instant.

**Event streams:**
- `ConnStatusEvents` is backed by a `postage::watch::Receiver<ConnStatus>`. A
  freshly cloned receiver yields `TryRecv::Ready(current_value)` on its first
  poll (deterministic, no timeout).
- `BootstrapEvents` is backed by a `postage::watch::Receiver<BootstrapStatus>`.
  Same first-poll semantics.
- `DirBootstrapEvents` is backed by a
  `postage::watch::Receiver<DirBootstrapStatus>`. Same first-poll semantics.
- A watch channel coalesces: an observer that polls after several updates sees
  the latest value, not every intermediate one.

**ChanMgr failure thresholds.** `ChanMgr` refuses to report *failure* until it
has evidence. Until at least 6 connection attempts have been launched **and**
at least 60 seconds have passed since startup, the manager is in its "early"
period, and in that period a *lack* of success is reported as `None` —
"we don't know yet" — rather than as `Some(false)`. The early flag suppresses
only negative conclusions:

- a success is reported as `Some(true)` immediately, early or not;
- a lack of success is `None` while early, and `Some(false)` once both
  thresholds are past.

So a manager that has just completed one TCP connection reports
`online: Some(true), tls_works: None` at once — `frac() == 0.5` — even at
t ≈ 0 with a single attempt recorded. A manager that has done nothing reports
`ConnStatus::default()`.

**Rejections the checker asserts:**
- `ChanMgr::get_or_launch` on an `OwnedChanTarget` with an empty address list
  fails with `Error::UnusableTarget`, rendering
  `"Target was unusable: No addresses for chosen relay"`.
- `TokioNativeTlsRuntime::current()` and `tor_rtcompat::current_user_runtime()`
  return `Err` when no runtime is running.
- Deserializing either circuit-configuration type from TOML containing an
  unrecognised key fails, and the error names that key.
- Connecting through a `MockNetProvider` to an address nothing is listening on
  fails with an `io::Error` whose kind is `ConnectionRefused` and whose source
  renders `"Invalid operation on mock network"`. Every `MockNetwork` error is
  wrapped the same way, so that string is the mock network's single failure
  description.
- **A connection originates from the connecting provider's own address**, and
  that is the address the far side's `accept()` reports. A provider built by
  `ProviderBuilder::add_address(a)` may connect out to any family-`a` address
  and does so *from* `a` on an arbitrary high port; `MockNetListener::accept()`
  yields `(stream, source)` with that `a` as `source.ip()`. So a listener bound
  through a `MockNetProvider` whose caller registered only `192.0.2.3` sees
  `192.0.2.3` as the peer, and a case in scope asserts exactly that — it is how
  the `compound` group tells "the clone shares the network component" from
  "the clone got a fresh one".

**Mock TLS negotiation:**
- `MockTlsConnector::negotiate_unvalidated` returns
  `io::Error::new(ErrorKind::Other, "attempted to wrap non-TLS stream!")` when
  the mock stream carries no TLS certificate — that is, when the peer listened
  with `TcpProvider::listen` rather than `listen_tls`.
- `MockTlsStream::peer_certificate()` returns `Ok(Some(cert_bytes))` with
  exactly the bytes the listener was created with via `listen_tls` — raw DER,
  neither re-encoded nor unwrapped, so the first octet of a certificate handed
  in as a DER `SEQUENCE` is still `0x30` when it comes back out.
- The mock connector ignores the `sni_hostname` argument; it is part of the
  trait signature, not of the mock's behaviour.
- A `ChanMgr` running over a composed runtime whose TLS half is the mock
  network therefore fails with
  `"I/O error while opening a channel: attempted to wrap non-TLS stream!"` when
  the relay is listening without a certificate.

**PreemptiveCircuitConfig default values:**
- `disable_at_threshold: 12`
- `initial_predicted_ports: [80, 443]`
- `prediction_lifetime: 3600s`
- `min_exit_circs_for_port: 2`

**CircuitTiming default values:**
- `max_dirtiness: 600s`
- `request_timeout: 60s`
- `request_max_retries: 32`
- `request_loyalty: 50ms`

**Reconfiguration error:**
- `Reconfigure::AllOrNothing.cannot_change("field_name")` returns
  `Err(ReconfigureError::CannotChange { field: "field_name".to_string() })`.
- Display: `"Cannot change field_name on a running client."`
- `Reconfigure::CheckAllOrNothing` returns **the same refusal** from the same
  call — a dry run reports exactly what the real run would, and a check that
  answered `Ok(())` would be worthless. `Reconfigure::WarnOnFailures` is the one
  mode that returns `Ok(())`, logging the problem instead of returning it.
  Step 04's contract states this correctly and is not overridden here; the
  inherited `tor-config` implements it, and the `reconfig` group re-checks it
  against the carried workspace.

**MutCfg semantics:**
- `get()` returns an `Arc` handle; `replace(new)` installs a new value
  unconditionally, which makes any previously returned handle stale.
- `check_and_replace(&old, new)` returns `true` and replaces if `old` is still
  the current handle (`Arc::ptr_eq`); `false` and leaves the value alone
  otherwise.
- `map_and_replace(f)` unconditionally applies `f` to the current `&Arc<T>`
  and installs the returned value.

**CompoundRuntime composition:**
- `CompoundRuntime::new(spawn, sleep, tcp, tls)` delegates each capability to
  the corresponding component, so
  `<CompoundRuntime<_, _, TcpR, _> as TcpProvider>::TcpStream` is
  `TcpR::TcpStream` and
  `<CompoundRuntime<_, SleepR, _, _> as SleepProvider>::SleepFuture` is
  `SleepR::SleepFuture`.
- A composed runtime is `Clone`, and the clone shares the underlying state:
  a listener bound through one handle accepts a connection made through
  another.
- Composing a real async backend for spawn and sleep with the mock network for
  TCP and TLS yields a value that satisfies the whole `Runtime` bound. That is
  what makes the TLS half independently replaceable.

---

## Requirement groups → interface items

Every group this step uses, and the items above that it exercises. A group is
checked only through items named in this document; anything reachable but
undocumented is out of scope.

- **Gate** groups add no requirement of their own.
  Each one must pass completely or the step is not satisfied, however much of
  the new work is finished. They are the regression floor: behaviour signed off
  at an earlier milestone, re-checked here against the carried workspace. There
  is no separate build-proof group at this step — the build is enforced by
  every group failing when the tree does not compile.
- **Requirement** groups carry this step's new work and supporting surface.
  Every case in one must pass.

A candidate that changes nothing therefore satisfies none of the new work.

| Group | Kind | Items |
|---|---|---|
| `connstatus` | requirement | `tor-chanmgr` — `ConnStatus::{usable, frac, blockage}` and its default `Display`; `ConnBlockage` variants, their `Display`, equality, `Clone` and `Debug` |
| `connevents` | requirement | `tor-chanmgr` — `ConnStatusEvents` (`Stream::Item`, `Clone`, `Debug` shape), `ChanMgr::{new, bootstrap_events}`; the watch-channel first-poll rule and independent subscriptions |
| `chanevents` | requirement | `tor-chanmgr` — `ChanMgr::{new, get_or_launch}`, `ConnStatus` transitions and the early-period rule, `Error` Display; `tor-linkspec` — `OwnedChanTarget::new`; `tor-llcrypto` — `Ed25519Identity`, `RsaIdentity`; `tor-rtcompat` — `TokioNativeTlsRuntime::run_test`, `TcpProvider`; `tor-rtmock` — `MockNetwork`, `MockNetProvider` |
| `dirstatus` | requirement | `tor-dirmgr` — `DirStatus` and `DirBootstrapStatus`: defaults, `Display`, `frac_at`/`usable_at` and their time parameterisation |
| `direvents` | requirement | `tor-dirmgr` — `DirEvent`, `DirBootstrapEvents` (`Item = DirBootstrapStatus`, `Clone`, `Debug`, `Send + Sync`), `DirMgr::bootstrap_events` |
| `bootstat` | requirement | `arti-client` — `BootstrapStatus::{as_frac, ready_for_traffic, blocked}`, its `Display`/`Debug`/`Clone`, `BootstrapEvents::Item` |
| `blockage` | requirement | `arti-client` — `Blockage` (`Clone`/`Debug`/`Display`), `BlockageKind` and its `Display`; `tor-chanmgr` — `ConnBlockage` and the `From` conversion between them |
| `clientapi` | requirement | `arti-client` — `TorClient::{clone_with_prefs, isolated_client, bootstrap_status, bootstrap_events}`, `TorClient: Clone + Send + Sync`, `BootstrapEvents` |
| `clientboot` | requirement | `arti-client` — `TorClient::{bootstrap_with_tokio, bootstrap_with_async_std, bootstrap}`, `TorClientConfig::default`, `arti_client::Result`; `tor-rtcompat` — the two `PreferredRuntime` aliases and the crate-root one |
| `streamprefs` | requirement | `arti-client` — `StreamPrefs` address-family setters, `optimistic`, chaining, `Clone`, and the `Debug` rendering they are observed through |
| `isolation` | requirement | `arti-client` — `StreamPrefs::{isolate_every_stream, set_isolation_group, new_isolation_group}` and the three `isolation` renderings; `IsolationToken::{new, no_isolation}` |
| `compound` | requirement | `tor-rtcompat` — `CompoundRuntime`, `BlockOn`, `Runtime`, `SleepProvider`, `TcpProvider`, `TcpListener`; `tor-rtmock` — `MockNetProvider`, `MockNetwork`, `MockNetError`'s `Display`, `LocalStream` read/write |
| `tlscompose` | requirement | `tor-rtcompat` — `CompoundRuntime` with a mock TLS half, `TlsProvider`, `tls::{CertifiedConn, TlsConnector}`; `tor-rtmock` — `MockNetProvider::listen_tls`; `tor-chanmgr` — `ChanMgr` over that runtime and its `Error` `Display` |
| `tlsruntimes` | requirement | `tor-rtcompat` — `TokioNativeTlsRuntime::{create, current, run_test}`, `AsyncStdNativeTlsRuntime::{create, run_test}`, their `Debug`, `create_runtime()` |
| `blockon` | requirement | `tor-rtcompat` — `BlockOn::block_on` through the trait and through the `Runtime` bound, `current_user_runtime()`; `tor-rtmock` — `MockNetRuntime` from the crate root and `ProviderBuilder::runtime` |
| `preemptive` | gate | `tor-circmgr` — `PreemptiveCircuitConfig`, `PreemptiveCircuitConfigBuilder`, fallible `build()`, TOML with `deny_unknown_fields` (unchanged from step 04) |
| `reconfig` | gate | `tor-config` — `MutCfg` (including `map_and_replace`'s `&Arc<T>` closure), `Reconfigure`, `ReconfigureError` (unchanged from step 04) |
| `circtiming` | gate | `tor-circmgr` — `CircuitTiming`, `CircuitTimingBuilder`, fallible `build()`, humantime TOML durations (unchanged from step 04) |
| `tlstraits` | requirement | `tor-rtcompat` — `tls::{CertifiedConn, TlsConnector}`, `TlsProvider`, `TcpProvider` associated types; `tor-rtmock` — `MockTlsStream::peer_certificate`, `LocalStream` |
| `chanmgrapi` | requirement | `tor-chanmgr` — `ChanMgr::{new, get_or_launch}` signatures, every `Error` `Display` string and `From<std::io::Error>`, `ConnStatusEvents`'s `Debug` shape |

### Declared here, not exercised by any case

The contract above is the surface, not the call list. Several items are named
because later steps and real callers build on them, and this step's groups
never drive them. They are listed so that a reader does not go looking for a
case that does not exist — **not** as an invitation to skip them. Each is a
requirement; implement it.

- The `as_frac` composition itself: `conn.frac() * 0.15 + dir.frac_at(now) *
  0.85`. Only the *default* status (`0.0`) is observed, so no case reaches a
  non-zero composition.
- Three of the five `ConnStatus` Display rows — `online: Some(false)`,
  `tls_works: Some(false)`, and both-true — plus `frac() == 1.0`. These are
  states the public API of this step cannot reach; the paragraph above says so.
- The `Proto(e)` rendering (§`tor_chanmgr::Error`).
- `From<SpawnError>`, `From<TimeoutError>` and `From<PoisonError<T>>`. Only the
  `From<std::io::Error>` conversion is driven.
- The three non-wrapping `From` conversions' *behaviour* is therefore stated
  but unobserved; the conversions themselves are part of the boundary.
- `AsyncStdNativeTlsRuntime::current()` — it always returns `Ok`, as the
  `current()` rule in the `tor_rtcompat` section above says, and no case drives
  it.
- The `rustls` feature and the two rustls runtime types: `instruction.md`
  requires the feature to exist and to compile, and no case imports anything
  behind it.
- `SleepProvider`'s method set beyond the `SleepFuture` type identity, and the
  watch streams' coalescing rule.
- The mock connector's ignoring of `sni_hostname` — stated above, asserted
  nowhere.
- `MockNetRuntime::{inner, mock_net}`, `MockSleepRuntime`,
  `MockNetListener`'s `Stream` impl and `MockTlsStream`'s `AsyncRead`/
  `AsyncWrite` impls, beyond the read/write path `compound` and `tlstraits`
  drive.
- The `#[non_exhaustive]` attributes on `tor_chanmgr::Error` and the
  `tor-config` types: stated, and unenforceable from an external crate that
  always matches with a wildcard arm.

Two of the items above are **forward-looking rather than unreachable**: the
`Proto(e)` rendering and the three wrapper `From` conversions become
load-bearing at step 06, which restructures this error type and does render it.
