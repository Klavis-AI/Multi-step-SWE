# Step 06 — public interface contract

Every item below is checked by at least one case in `tests/assets/groups.txt`.
Each entry names the crate, the module path, the exact signature the driver
calls, and the observable the group asserts on.

Path deps are written as `@@APP@@/crates/<crate>`; the verifier substitutes the
application tree root at staging time.

---

Six cargo features must exist and must compile, because the driver enables them:

| Crate | Feature | Why the driver needs it |
| --- | --- | --- |
| `arti-client` | `tokio` | selects the tokio runtime |
| `arti-client` | `native-tls` | selects the native-tls backend |
| `arti-client` | `rustls` | selects the rustls backend; NOT in this crate's default set |
| `tor-rtcompat` | `tokio` | exposes `tor_rtcompat::tokio`; this crate's default feature set is empty |
| `tor-rtcompat` | `native-tls` | exposes the native-tls backend |
| `tor-rtcompat` | `rustls` | exposes the rustls backend |

These are not optional extras. The driver's manifest names them, so cargo
resolves them before a single test runs. A crate that does not DECLARE the
feature - even as an empty entry in its `[features]` table - fails metadata
resolution, and the verifier then fails every group in this step at once,
with no per-group detail to point at the cause.

## 1. `tor-error` (new crate in this step)

Module path: `tor_error` (crate root). All items below are re-exported from the
crate root.

### 1.1 `ErrorKind`

```rust
#[non_exhaustive]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Display)]
pub enum ErrorKind { /* 40 variants */ }
```

Observable: `to_string()` for each variant is a stable lowercase sentence, listed
in full in §1.1.1 below; the enum is `Copy` and its variants are mutually
distinguishable (`Debug` names the variant — `format!("{:?}", ErrorKind::NoExit)`
is exactly `NoExit` — not the display text); `ErrorKind: Send + Sync + 'static`.

### 1.1.1 The variant→rendering table

Every one of these 40 renderings is checked. A variant that is spelled
differently from the left column, or that renders with different capitalisation,
punctuation, or wording from the right column — including the trailing periods
on `DirectoryExpired` and `ArtiShuttingDown`, and the parenthesised qualifiers —
fails its case.

| Variant | `to_string()` |
| --- | --- |
| `TorAccessFailed` | `error connecting to Tor` |
| `BootstrapRequired` | `attempted to use unbootstrapped client` |
| `DirectoryExpired` | `network directory is expired.` |
| `PersistentStateAccessFailed` | `could not read/write persistent state` |
| `PersistentStateCorrupted` | `corrupted data in persistent state` |
| `CacheCorrupted` | `corrupted data in cache` |
| `CacheAccessFailed` | `cache access problem` |
| `ReactorShuttingDown` | `reactor is shutting down` |
| `ArtiShuttingDown` | `Tor client is shutting down.` |
| `RemoteNetworkTimeout` | `operation timed out at exit` |
| `InvalidConfig` | `invalid configuration` |
| `InvalidConfigTransition` | `invalid configuration transition` |
| `NoHomeDirectory` | `could not find a home directory` |
| `NotImplemented` | `operation not implemented` |
| `FeatureDisabled` | `operation not supported because Arti feature disabled` |
| `LocalProtocolViolation` | `local protocol violation (local bug or incompatibility)` |
| `TorProtocolViolation` | `Tor network protocol violation (bug, incompatibility, or attack)` |
| `LocalNetworkError` | `problem with network or connection` |
| `RelayIdMismatch` | `identity mismatch` |
| `CircuitCollapse` | `circuit collapsed` |
| `TorNetworkTimeout` | `tor operation timed out` |
| `TorDirectoryError` | `directory fetch attempt failed` |
| `RemoteStreamClosed` | `remote stream closed` |
| `RemoteStreamReset` | `remote stream reset` |
| `RemoteStreamError` | `remote stream error` |
| `RemoteConnectionRefused` | `remote host refused connection` |
| `ExitPolicyRejected` | `rejected by exit policy` |
| `ExitTimeout` | `timeout at exit relay` |
| `RemoteNetworkFailed` | `network failure at exit` |
| `RemoteHostNotFound` | `remote hostname lookup failure` |
| `RemoteProtocolFailed` | `remote protocol failed` |
| `RelayTooBusy` | `relay too busy` |
| `InvalidStreamTarget` | `target address was invalid` |
| `ForbiddenStreamTarget` | `target address disabled locally` |
| `TransientFailure` | `un-retried transient failure` |
| `BadApiUsage` | `bad API usage (bug)` |
| `CircuitRefused` | `remote host refused our request` |
| `NoPath` | `could not construct a path` |
| `NoExit` | `no exit available for path` |
| `Internal` | `internal error (bug)` |

`ErrorKind` itself implements `Display`, so a `Bug` can interpolate its kind
directly (see §1.3).

### 1.2 `HasKind`

```rust
pub trait HasKind {
    fn kind(&self) -> ErrorKind;
}
```

Observable: implemented for the error types of every crate in the workspace
that reports a classification; `kind()` is a pure function of the value.

`tor-error` also implements `HasKind` for one type it does not own, because
every manager in the tree converts a failed task spawn into its own error and
the classification has to exist at the bottom:

```rust
impl HasKind for futures::task::SpawnError;
// ReactorShuttingDown when `is_shutdown()`, otherwise Internal.
```

`errkind` builds one with `futures::task::SpawnError::shutdown()` and asserts
both `is_shutdown()` and `kind() == ErrorKind::ReactorShuttingDown`. The
`Spawn { cause, .. }` variants of `tor-chanmgr`, `tor-circmgr` and `tor-dirmgr`
delegate to this impl, so it is what makes their rows in §7.1 work.

### 1.3 `Bug`

```rust
#[derive(Clone, Debug)]
pub struct Bug(/* opaque; a `Box<BugRepr>` */);

impl Bug {
    pub fn new<S: Into<String>>(kind: ErrorKind, message: S) -> Self;
    // THREE arguments, in this order: kind, source, message. The message is
    // mandatory and is NOT optional; `from_error` does not take just an error.
    pub fn from_error<E, S>(kind: ErrorKind, source: E, message: S) -> Self
    where
        S: Into<String>,
        E: std::error::Error + Send + Sync + 'static;
}

impl std::error::Error for Bug;   // source() returns the wrapped error
impl std::fmt::Display for Bug;
impl HasKind for Bug;             // kind() returns the `kind` it was built with
```

**`from_error` takes three arguments.** The contract previously implied a
two-argument form; at this version the message is a required third parameter.
`Bug::from_error(kind, source, message)` — the source is second and the message
third.

**`Display` starts with the kind, then the call site, then the message.**

```rust
writeln!(f, "{} at {}: {}", self.0.kind, &self.0.location, &self.0.message)?;
```

so the rendering is `<kind display> at <file>:<line>: <message>` followed by the
rendered backtrace. The exact rendering is therefore:

```
internal error (bug) at <file>:<line>: <message>
<backtrace, possibly empty>
```

A freshly built `Bug` has no source; `from_error` and the `into_*` macros do.
Callers match on the `"… at "` prefix and the substring, never on the whole
string, because the middle segment is a `#[track_caller]` location and the tail
is a backtrace.

### 1.4 Macros

```rust
// Each of these is a `macro_rules!` that forwards every token to `format!`,
// not a function taking a single `$msg:expr`.
tor_error::internal!(...)          -> Bug;   // kind == ErrorKind::Internal
tor_error::bad_api_usage!(...)     -> Bug;   // kind == ErrorKind::BadApiUsage
tor_error::into_internal!(...)     -> impl FnOnce(E) -> Bug;
tor_error::into_bad_api_usage!(...) -> impl FnOnce(E) -> Bug;
```

**The macros are `format!`-style, and the `into_*` forms are closures.**
`internal!` and `bad_api_usage!` expand to `Bug::new(kind, format!(...))`, so
they accept any `format!` argument list (`internal!("bad {}: {:?}", x, y)`) and
their result is a `Bug`, not a `Result`. `into_internal!` and
`into_bad_api_usage!` **return a closure**, not a `Bug` — you pass one to
`map_err`, which is what makes `err.map_err(into_internal!("context"))` work.

### 1.5 `Report<E>`

```rust
// A PUBLIC TUPLE STRUCT with a public field. There is no `Report::new`.
#[derive(Debug, Copy, Clone)]
pub struct Report<E>(pub E)
where
    E: AsRef<dyn std::error::Error>;

impl<E> std::fmt::Display for Report<E>
where
    E: AsRef<dyn std::error::Error>;

// NOTE: no `impl HasKind for Report<E>` and no `impl std::error::Error for
// Report<E>` at this version. Report is a presentation wrapper, nothing more.
```

**`Report` is constructed with its tuple field, not with `new`.** The bound is
`E: AsRef<dyn std::error::Error>`, not `E: Error`, so the idiomatic call is
`Report(&boxed_error)` or `Report(e.as_ref())`. `Report` is `Copy` when `E` is,
which is why `Report<&Box<dyn Error>>` can be copied.

**`Display` writes a literal leading `error`.** The rendering is
`error: <link>: <link>: …` — the word `error` is part of the output, not a
placeholder. Each successive `source()` is appended as `": "` plus its message,
and is **skipped entirely** if the *previous* link's message already contains
it as a substring (the check is `last.contains(&this)`, so it suppresses a
source that a parent already named, as in `verbose - shallow` above `shallow`).

Observable: checked over a three-level chain plus an `io::Error` wrapper. The
module also exposes the process-level reporter the `arti` binary uses:

```rust
pub fn report_and_exit<E, R>(e: E) -> R
where
    E: AsRef<dyn std::error::Error>;
// Prints "<progname>: <Report(e)>" to stderr and calls
// `std::process::exit(127)`. It never returns; the generic `R` exists so it
// can be passed to `unwrap_or_else`.
```

### 1.6 `Truncated`

```rust
// NOT `Copy`. The derive list is exactly these five, and the struct is
// `#[non_exhaustive]`, so code outside the crate cannot write the literal
// `Truncated` — `new()` and `default()` are the only two constructors, and
// both must exist.
#[derive(Clone, Debug, Default, thiserror::Error, derive_more::Display)]
#[display(fmt = "Incomplete data; more input needed")]
#[non_exhaustive]
pub struct Truncated;

impl Truncated {
    pub fn new() -> Self;      // this is the one the driver calls
}

impl std::fmt::Display for Truncated;   // via derive_more
impl std::error::Error for Truncated;   // via thiserror
```

Observable: `Truncated::new()` and `Truncated::default()` render identically and
`Debug` identically; `format!("{:?}", Truncated::new())` is exactly `Truncated`;
`to_string()` is exactly `Incomplete data; more input needed`; as a
`std::error::Error` it has no `source()`. `tor_socksproto::Truncated` is a
re-export of this same type, not a distinct one — `let t: tor_error::Truncated =
tor_socksproto::Truncated::new();` compiles.

---

## 2. `arti-client`

Module path: `arti_client` (crate root) plus `arti_client::status`.

**`ErrorKind` and `HasKind` are re-exported from the crate root.**
`arti_client::{ErrorKind, HasKind}` resolves — the crate does
`pub use tor_error::{ErrorKind, HasKind}` — and four of this step's group
targets import them from there rather than from `tor_error`. `Error` is the
crate's own `pub use err::Error`. A candidate that names these in
`arti_client::err` but does not re-export them at the root fails to compile
those targets: the same items reached by a different path is not enough.

### 2.1 Deferred bootstrap

```rust
// `builder()` is on the NON-generic impl: it can only produce a builder for the
// preferred runtime, so it cannot live in `impl<R: Runtime> TorClient<R>` —
// there `R` would be unconstrained at the call site and `TorClient::builder()`
// would not infer.
impl TorClient<PreferredRuntime> {
    pub fn builder() -> TorClientBuilder<PreferredRuntime>;
}

impl<R: Runtime> TorClient<R> {
    pub fn with_runtime(runtime: R) -> TorClientBuilder<R>;
    pub fn isolated_client(&self) -> TorClient<R>;   // #[must_use]
    pub async fn connect<A: IntoTorAddr>(&self, target: A) -> Result<DataStream, Error>;
    pub async fn resolve(&self, hostname: &str) -> Result<Vec<IpAddr>, Error>;
    pub async fn resolve_ptr(&self, addr: IpAddr) -> Result<Vec<String>, Error>;
}

// The two chained setters carry NO `Runtime` bound; only the two finishers do.
impl<R> TorClientBuilder<R> {
    pub fn config(self, config: TorClientConfig) -> Self;
    pub fn bootstrap_behavior(self, behavior: BootstrapBehavior) -> Self;
}
impl<R: Runtime> TorClientBuilder<R> {
    pub fn create_unbootstrapped(self) -> Result<TorClient<R>, Error>;
    // The bootstrapping finisher is `create_bootstrapped`, and it is `async`.
    // There is NO method called `create` at this version.
    pub async fn create_bootstrapped(self) -> Result<TorClient<R>, Error>;
}

pub enum BootstrapBehavior { OnDemand, Manual }
impl Default for BootstrapBehavior;   // hand-written, not `#[derive(Default)]`
```

`TorClient::with_runtime` is the only entry point the driver uses, and it is a
**builder constructor, not a client constructor**: it takes the runtime by value
and hands back a `TorClientBuilder<R>` on which `config`, `bootstrap_behavior`
and finally `create_unbootstrapped` are chained. It is compiled into every one
of this step's group targets, so a `with_runtime` that returns a client, or a
`TorClientBuilder` without `config`, costs the whole step rather than one group.
`TorClient::create_unbootstrapped` as a *client* method does not exist at this
version — the un-bootstrapped client is produced only by
`TorClientBuilder::create_unbootstrapped(self)`, which consumes the builder.

`TorClientBuilder::create_bootstrapped` is the other finisher: it is `async`,
it calls `create_unbootstrapped` and then `bootstrap()`, and it therefore needs
the network. **It is out of scope for this step** — see the scope note at the
end of §10 — so no group calls it; it is declared here only so that a candidate
does not invent a different name, and so that the `Manual`/`OnDemand` pair below
reads as the complete set of options.

The three client methods above are the ones the deferred-bootstrap groups drive.
`connect` takes anything that implements `arti_client::IntoTorAddr`; the checked
calls pass an owned **`String`** (`client.connect(target.to_string())`), so the
impl list inherited from step 02's contract — `TorAddr`, `&A where A:
IntoTorAddr + Clone`, `&str`, `String`, `(&str, u16)`, `(String, u16)` — is
load-bearing in full, `String` included. An implementation that covers `&str`
and not `String` fails `clienterr` and `hyperkind` at compile time. `resolve_ptr`
takes a `std::net::IpAddr`. Both are `async`, and on a `Manual` client neither opens a
socket: both fail before asking for a circuit (§2.2). `isolated_client` returns a
second handle with a fresh isolation token that shares the same configuration and
the same un-bootstrapped state, so it refuses network work for the same reason
the parent does.

Observable: `create_unbootstrapped` returns `Ok` without any network access, on
a machine with no connectivity. The resulting client reports
`bootstrap_status().ready_for_traffic() == false`. `BootstrapBehavior`
**implements** `Default` with `OnDemand` as the default — the impl is written
out by hand rather than derived, which is observationally the same thing — and
is `Copy + PartialEq + Debug`, with `Debug` naming the variant.

### 2.2 `Error` and error kinds before bootstrap

```rust
pub struct Error { /* opaque */ }

impl Error {
    pub fn kind(&self) -> ErrorKind;   // via HasKind
}
impl std::error::Error for Error;
impl std::fmt::Display for Error;
```

Observable: any operation that needs a circuit on an unbootstrapped client
returns an `Error` whose `kind()` is `ErrorKind::BootstrapRequired`, whose
`Display` begins with `tor: `, and whose message is exactly
`tor: attempted to use unbootstrapped client: cannot launch a circuit with unbootstrapped client`.
The failure is repeatable: a second identical call fails the same way.
`Error` is `Send + Sync + Clone + 'static` and implements `std::error::Error`.

The same `Error` type is used for address rejections. The rendering is always
`tor: <kind display>: <detail display>`, and the address cases are:

| Input | `kind()` | `to_string()` |
| --- | --- | --- |
| `127.0.0.1:80`, `[::1]:80`, `localhost:80` | `ForbiddenStreamTarget` | `tor: target address disabled locally: Cannot connect to a local-only address without enabling allow_local_addrs` |
| `example.com` (no port) | `InvalidStreamTarget` | `tor: target address was invalid: Could not parse target address: No port found in string` |
| `example.com:definitely-not-a-port` | `InvalidStreamTarget` | `tor: target address was invalid: Could not parse target address: Could not parse port` |
| `example.com:0` | `InvalidStreamTarget` | — (port 0 is rejected as a bad port, not accepted as a zero-length connection) |

**`example.com:0` is `InvalidStreamTarget`, not a valid target.** A port of
zero is rejected by the address constructor outright, so it takes the same
`Failed to parse target address` path as an unparsable one; it is never handed
to the client.

`arti_client::Error` is also `Debug` (the `Debug` rendering mentions `Error`),
`Send + Sync + Clone + 'static`, and implements `std::error::Error`. A clone
reports the same `kind()` and the same `to_string()` as the original.

### 2.3 `arti_client::status`

```rust
pub use arti_client::status::{BootstrapStatus, BlockageKind};
pub use arti_client::BootstrapBehavior;
pub use tor_chanmgr::ConnBlockage;   // re-exported by tor-chanmgr, not arti-client

#[derive(Debug, Clone, Default)]
pub struct BootstrapStatus { /* opaque */ }

impl BootstrapStatus {
    pub fn as_frac(&self) -> f32;
    pub fn ready_for_traffic(&self) -> bool;
    pub fn blocked(&self) -> Option<Blockage>;
}
impl std::fmt::Display for BootstrapStatus;

#[non_exhaustive]
pub enum BlockageKind { Offline, Filtering, CantReachTor }
impl std::fmt::Display for BlockageKind;

// Lives in `tor_chanmgr`, NOT in `arti_client::status`; `blockkind` names it
// through `tor_chanmgr::ConnBlockage`.
#[derive(Debug, Clone, Eq, PartialEq, derive_more::Display)]
#[non_exhaustive]
pub enum ConnBlockage { NoTcp, NoHandshake }

impl<R: Runtime> TorClient<R> {
    pub fn bootstrap_status(&self) -> status::BootstrapStatus;
    pub fn bootstrap_events(&self) -> status::BootstrapEvents;
}
```

Observable: `BootstrapStatus::default()` has `as_frac() == 0.0`,
`ready_for_traffic() == false`, `blocked().is_none()`, and a `Display` string
beginning `0%: `. `bootstrap_events()` returns a `Clone`-able
`futures::Stream<Item = BootstrapStatus>`. A `Manual` client that has never run
bootstrap stays at `as_frac() == 0.0`. `ConnBlockage` renders
`unable to connect to the internet` / `our internet connection seems to be filtered`;
`BlockageKind` renders `We seem to be offline` / `Our internet connection seems
filtered` / `Can't reach the Tor network`, and the three are mutually distinct.

---

## 3. `arti-hyper` (new crate in this step)

Module path: `arti_hyper` (crate root). Create it at `/app/crates/arti-hyper`
and add it to the workspace `members`, as `WORKSPACE_HANDOFF.md` describes: the
driver reaches it by path.

**Its `tls-api` version is part of this contract.** `arti-hyper` declares
`tls-api = "0.7"` and `tls-api-native-tls = "0.7.0"` — the versions this
release's dependency graph resolves those two crates to — alongside `hyper`
0.14, `thiserror` 1, `anyhow` 1, `pin-project` 1, and the workspace crates
`arti-client`, `tor-error` and `tor-rtcompat` (feature `tokio`). The version is
load-bearing rather than decoration: two versions of `tls-api` in one dependency
graph are two distinct `TlsConnector` traits that merely share a name, so an
`ArtiHttpConnector` built against another version — 0.8 or 0.9, both of which
are in this environment's crate set — is generic over the wrong trait, and the
driver's connector, a `tls_api_native_tls::TlsConnector` from *its* 0.7, is not
a `TC` it accepts. rustc reports *"multiple different versions of crate
tls_api in the dependency graph"*, and the group does not compile.

```rust
// A PRIVATE alias inside the crate — `use`, not `pub use`. `arti_hyper::TlsConn`
// is not a reachable path; it is only the name this document and the crate's own
// signatures use for `tls_api::TlsConnector`. A consumer names the tls-api trait
// itself, which is what the driver does.
use tls_api::TlsConnector as TlsConn;

pub struct ArtiHttpConnector<R: Runtime, TC: TlsConn> { /* opaque */ }

impl<R: Runtime, TC: TlsConn> ArtiHttpConnector<R, TC> {
    pub fn new(client: TorClient<R>, tls_conn: TC) -> Self;
}
impl<R: Runtime, TC: TlsConn> Clone for ArtiHttpConnector<R, TC>;

#[derive(Error, Clone, Debug)]
#[non_exhaustive]
pub enum ConnectionError {
    UnsupportedUriScheme { uri: Uri },
    MissingHostname { uri: Uri },
    // `#[from]`, so `ConnectionError::from(arti_err)` exists.
    Arti(#[from] arti_client::Error),
    // `anyhow::Error`, NOT `dyn std::error::Error`. The field is `#[source]`,
    // so the anyhow error is the `source()`, not a nested message.
    TLS(#[source] Arc<anyhow::Error>),
}
impl tor_error::HasKind for ConnectionError;
impl std::fmt::Display for ConnectionError;
impl std::error::Error for ConnectionError;   // via thiserror
impl Clone for ConnectionError;

// Written upstream as `hyper::service::Service`, which in hyper 0.14 is
// literally `pub use tower_service::Service` — the same trait reached by a
// different path. `arti-hyper` depends on `hyper`, not on `tower`/`http`
// directly; do not add those dependencies to satisfy this line.
impl<R: Runtime, TC: TlsConn> hyper::service::Service<http::Uri> for ArtiHttpConnector<R, TC> {
    type Response = ArtiHttpConnection<TC>;
    type Error = ConnectionError;
    type Future = Pin<Box<dyn Future<Output = Result<Self::Response, Self::Error>> + Send>>;
    fn poll_ready(&mut self, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>>;
    fn call(&mut self, req: Uri) -> Self::Future;
}
```

**`TLS` carries an `Arc<anyhow::Error>`, and `Arti` is a `#[from]`.** The
contract previously declared `TLS(Arc<dyn std::error::Error + Send + Sync>)`;
the real field is an `anyhow::Error`, which is why the harness builds it as
`ConnectionError::TLS(Arc::new(anyhow::anyhow!("handshake refused")))`.
Because the variant is named `TLS` and not `Tls`, and because `Arti` is a
`#[from]` variant, `ConnectionError::from(client_error)` converts without a
manual `From` impl.

The `Display` strings are **exact**:

| Variant | `to_string()` |
| --- | --- |
| `UnsupportedUriScheme { uri }` | `unsupported URI scheme in <uri:?>` (lowercase `u` in `unsupported`) |
| `MissingHostname { uri }` | `Missing hostname in <uri:?>` (capital `M`) |
| `TLS(_)` | `TLS connection failed` |
| `Arti(_)` | `Tor connection failed` |

Note the inconsistent initial capitalisation between the first two rows: that
is the upstream text, and it is what the group asserts. `Debug` contains
`Arti`. `Arti(_)` exposes `source()` (the `arti_client::Error`); `TLS(_)`
exposes its `anyhow::Error` as `source()`.

Observable: `poll_ready` is `Poll::Ready(Ok(()))` on the first poll and the
connector is immediately usable. `ConnectionError::kind()` maps
`UnsupportedUriScheme` to `NotImplemented`, `MissingHostname` to `BadApiUsage`,
`TLS` to `RemoteProtocolFailed`, and `Arti(e)` to `e.kind()`. `call` with a URI
whose scheme is not `http` or `https` fails with `UnsupportedUriScheme`; **the
scheme is classified before the authority is looked at, so an origin-form URI
such as `/index.html` — which has no scheme at all — is also
`UnsupportedUriScheme`, not `MissingHostname`.** `MissingHostname` is the
verdict only for a URI that has a scheme this connector accepts and no
authority. Every one of these is decided before any socket is opened; the sole
check that reaches the network is a well-formed `http://` URI on a `Manual`
client, which fails with the client's own error (`BootstrapRequired`).

`TlsConn` is not a type this crate defines, and it is not re-exported: it is the
crate-private alias `use tls_api::TlsConnector as TlsConn`, so the driver's `TC`
is a `tls_api::TlsConnector` implementation named through `tls_api` itself. It is a **different trait** from
`tor_rtcompat::tls::TlsConnector`, which is the one a runtime supplies; the
concrete connector the group passes is `tls_api_native_tls::TlsConnector`,
built through the `tls_api::{TlsConnector as _, TlsConnectorBuilder as _}`
trait imports (`TlsConnector::builder()?.build()?`). Both the trait and the
concrete connector come from the **0.7** crates named above, so the driver and
`arti-hyper` must be looking at the same `tls-api`: the driver's own manifest
requires `tls-api = "0.7"`, and a workspace that resolves `tls-api` to a
different version does not compile against it.

---

## 4. `arti-config`

Module path: `arti_config` (crate root).

```rust
#[derive(Clone, Debug, Default)]
pub struct ConfigurationSources { /* opaque */ }

impl ConfigurationSources {
    pub fn new() -> Self;
    // Generic and BY VALUE: `P: AsRef<Path>` taken as `p: P`, exactly as
    // `fs::File::open` takes it, so `push_file(PathBuf)`, `push_file(&Path)`
    // and `push_file("x")` all compile. These return `()`, NOT `Result`.
    pub fn push_file<P: AsRef<Path>>(&mut self, p: P);
    pub fn push_optional_file<P: AsRef<Path>>(&mut self, p: P);
    // Also borrowed, and the owned type is pinned to `String`:
    // `S: ToOwned<Owned = String> + ?Sized`, so `&str` and `&String` both work.
    pub fn push_option<S: ToOwned<Owned = String> + ?Sized>(&mut self, option: &S);
    // The two accessors the driver actually calls.
    pub fn files(&self) -> impl Iterator<Item = &Path>;
    pub fn load(&self) -> Result<config::Config, config::ConfigError>;
}

#[derive(Deserialize, Debug, Clone, Eq, PartialEq, Default)]
#[serde(deny_unknown_fields)]
pub struct ArtiConfig { /* opaque, accessor-only */ }

// NOTE: the source type is `config::Config` -- the output of
// `ConfigurationSources::load()` -- and NOT `ConfigurationSources` itself.
impl std::convert::TryFrom<config::Config> for ArtiConfig {
    type Error = config::ConfigError;
}

impl ArtiConfig {
    pub fn builder() -> ArtiConfigBuilder;
    pub fn application(&self) -> &ApplicationConfig;
    pub fn logging(&self) -> &LoggingConfig;
    pub fn proxy(&self) -> &ProxyConfig;
}

impl ArtiConfigBuilder {
    // `&self`, NOT `self`: the builder is not consumed, so it can be built
    // from more than once and a `&mut` chain can still own it.
    pub fn build(&self) -> Result<ArtiConfig, ConfigBuildError>;
    // Mutator, not a setter: returns `&mut` to the section builder.
    pub fn application(&mut self) -> &mut ApplicationConfigBuilder;
}

// The three section structs are opaque and accessor-only. Their derives are
// load-bearing for `ApplicationConfig`: `cfgwatch` calls
// `ApplicationConfig::default()` and compares two `&ApplicationConfig` with
// `assert_ne!`, so it needs `Default`, `PartialEq` and `Debug`, and `Builder`
// is what generates `ApplicationConfigBuilder`.
#[derive(Deserialize, Debug, Default, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ApplicationConfig { /* opaque, accessor-only */ }

#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[non_exhaustive]
pub struct LoggingConfig { /* opaque, accessor-only */ }

#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ProxyConfig { /* opaque, accessor-only */ }

// Named only because it appears in `LoggingConfig::logfiles`'s return type.
// Nothing in this step inspects one, so its shape is out of scope.
pub struct LogfileConfig { /* opaque */ }

impl ApplicationConfig {
    pub fn watch_configuration(&self) -> bool;
    // Default is false; the setter lives on the *builder*, not here.
}
impl ApplicationConfigBuilder {
    pub fn watch_configuration(&mut self, watch: bool) -> &mut Self;
}

impl LoggingConfig {
    // `console_filter` is the only one of the three this step checks; the
    // other two are named so that the replacement of step 03's `trace_filter`
    // is unambiguous. The two `Option<&str>` accessors map an empty string to
    // `None`, so `console_filter()` is `Some("debug")`, never `Some("")`.
    pub fn logfiles(&self) -> &[LogfileConfig];
    pub fn journald_filter(&self) -> Option<&str>;
    pub fn console_filter(&self) -> Option<&str>;
}

impl ProxyConfig {
    pub fn socks_port(&self) -> Option<u16>;   // `Option<u16>`, not `u16`
}
```

**`set_watch_configuration` does not exist.** `ApplicationConfig` has a
getter only; the flag is set through `ArtiConfigBuilder::application()` and
`ApplicationConfigBuilder::watch_configuration(bool)`, both of which take
`&mut self` and return `&mut` so they chain.

**The `push_*` family returns `()` and cannot fail at push time.** A
required-but-absent file is only discovered by `load()`. `push_file` and
`push_optional_file` take their path **by value** as `P: AsRef<Path>`, so an
owned `PathBuf`, a `&Path` and a `&str` are all accepted; `push_option` is the
one that takes a reference, `&S` where `S: ToOwned<Owned = String> + ?Sized`.
The driver uses both spellings — `sources.push_file(&a)` and
`sources.push_file(dir.path().join("x.toml"))` — so a signature that accepts
only one of them fails to compile.

**`console_filter` replaced step 03's `logging.trace_filter`.** The step-03
contract documented `LoggingConfig::trace_filter`; at this version the console
filter accessor is `console_filter`, and `logfiles()`, `journald_filter()`,
`console_filter()` are the three accessors present.

Observable: a fresh `ConfigurationSources` carries nothing (`files().count()`
is 0, and `ConfigurationSources::default()` is the same thing); `push_file`
records the path in the order pushed; a required file that does not exist makes
`load()` fail; `push_optional_file` on a missing path is accepted; later sources
override earlier ones; a `push_option` (command line) overrides files.
Built-in defaults are the **floor**, not an override: they sit below every
source, so a key no source sets keeps its default, and a key any source sets
takes that source's value. With nothing configured `proxy().socks_port()` is
`Some(9150)` and the console log filter is `debug`; once a file or a
command-line option sets one of them, the configured value wins and the default
is gone (e.g. `proxy.socks_port=3333` yields `Some(3333)`, not `Some(9150)`).
`ApplicationConfig::watch_configuration()` is `false` by default, is turned on
by a configuration file setting, can be turned back off by a later file, and is
turned on by a command-line option that overrides the files. `ArtiConfig` is
`#[serde(deny_unknown_fields)]`: a typo in a key is an error.

---

## 5. `tor-chanmgr`

Module path: `tor_chanmgr` (crate root).

```rust
// The type parameter is the RUNTIME, not a channel factory.
pub struct ChanMgr<R: Runtime> { /* opaque */ }

impl<R: Runtime> ChanMgr<R> {
    pub fn new(runtime: R) -> Self;
    pub fn expire_channels(&self) -> Duration;
}
impl Channel {
    pub fn duration_unused(&self) -> Option<Duration>;
}

pub enum ConnBlockage { NoTcp, NoHandshake, /* … */ }
impl std::fmt::Display for ConnBlockage;
```

**`ChanMgr` is parameterised by `R: Runtime`, not by an `AbstractChannelFactory`.**
This document previously wrote `impl<CF: AbstractChannelFactory> ChanMgr<CF>`,
which contradicts both step 05's contract (`ChanMgr<R: Runtime>::new(runtime)`)
and the harness, which calls `ChanMgr::new(runtime)`. The factory is an internal
detail at this version; nothing the driver touches names it. Where two binding
documents disagree, this one is the current one and step 05's `ChanMgr<R:
Runtime>` is the shape to implement.

**`tor_chanmgr::Error` changed shape at this step.** Step 05's contract froze
`Error { UnusableTarget(String), PendingFailed, ChanTimeout, Proto, Io(Arc),
Internal(&'static str) }`. At this version the shapes are:

```rust
pub enum Error {
    UnusableTarget(#[source] tor_error::Bug),   // was String
    PendingFailed,
    ChanTimeout,
    Proto(#[from] tor_proto::Error),
    Io { peer: SocketAddr, action: &'static str, source: Arc<std::io::Error> },  // was Io(Arc)
    Spawn { spawning: &'static str, #[source] cause: Arc<futures::task::SpawnError> },  // new
    Internal(#[from] tor_error::Bug),           // was &'static str
}
```

Three differences matter to a candidate implementing step 05's document
literally: `UnusableTarget` and `Internal` carry a `tor_error::Bug` rather than a
string, `Io` is a struct variant with named fields rather than a tuple variant,
and `Spawn` is new. A candidate that keeps the step-05 shapes does not compile
against the step-06 group. `Io`'s `Display` is
`Network IO error, or TLS error, in <action>, talking to <peer>`.

Observable: `expire_channels()` on a manager with no channels returns exactly
180 seconds (`Duration::from_secs(180)`, including `subsec_nanos() == 0`); it is
idempotent and does not panic when there is nothing to expire. `duration_unused`
returns `None` for a channel that has never been used and `Some(_)` after use.

---

## 6. `tor-socksproto`

Module path: `tor_socksproto` (crate root).

```rust
pub type TResult<T> = Result<Result<T>, Truncated>;

impl SocksHandshake {
    pub fn handshake(&mut self, input: &[u8]) -> TResult<Action>;
}

#[non_exhaustive]
pub enum Error {
    BadProtocol(u8),
    // Carries a payload; it is not a unit variant.
    AlreadyFinished(tor_error::Bug),
    Bug(tor_error::Bug),
    NotImplemented,
    /* … */
}
impl tor_error::HasKind for Error;
impl std::fmt::Display for Error;
```

`tor_socksproto::Error` derives `#[derive(Error, Debug)]` and is
`#[non_exhaustive]`. It is **not** `Clone`.

**`AlreadyFinished` is a tuple variant, not a unit variant.** It wraps a
`tor_error::Bug` describing the programming error that got us here, and its
`kind()` **delegates to that payload** (`E::AlreadyFinished(e) => e.kind()`)
rather than returning a fixed kind. **The payload is built with `internal!`
(equivalently `Bug::new(ErrorKind::Internal, …)`), so the delegated kind is
`ErrorKind::Internal`** — on both paths that produce it, reusing a completed
handshake and feeding one that has already failed. A candidate that chooses
`BadApiUsage` (a defensible reading of "the caller used the API wrongly", and
the reason this is spelled out) fails the group. The payload carries **no** `#[source]` and
no `#[from]` attribute — so `std::error::Error::source()` on it is `None` even
though `kind()` delegates — whereas the separate `Bug(#[from] tor_error::Bug)`
variant does carry one. `AlreadyFinished`'s `Display` is exactly
`SOCKS handshake was finished; no need to call this again`. A candidate that declares it as a unit
variant cannot be constructed by the poisoning path, and a candidate that maps
it to a constant kind disagrees with the payload's own kind. Match it as
`Error::AlreadyFinished(_)`, and construct it as
`Error::AlreadyFinished(Bug::from_error(...))`.

Observable: the outer `Err(Truncated)` means "more input needed" and is not a
failure; the handshake stays usable after a truncation and can be resumed. The
first bytes of an HTTP request (`C`, `D`, `G`, `H`, `O`, `P`, `T`) each produce
`Error::BadProtocol(byte)` whose `Display` reads
`Unrecognized SOCKS protocol version <byte as decimal>` and whose `kind()` is
`LocalProtocolViolation`; after such a violation the handshake is poisoned and
further input yields `AlreadyFinished`, whose `kind()` follows the wrapped
`Bug`.

**A poisoned handshake is not a finished one.** A `BadProtocol` refusal leaves
`finished()` **false** and `into_request()` **`None`** — the handshake is over
in the sense that it will accept nothing further, but it never produced a
request, so it does not report itself finished and hands nothing back. A
handshake that *completed* is the opposite: `finished()` is true and
`into_request()` yields the request. The two states are both "refuses further
input", and the checked `socksdiag` cases tell them apart by exactly those two
accessors, so an implementation that sets `finished = true` on the poisoning
path fails. SOCKS4 and
SOCKS5 initial bytes are accepted. A no-authentication greeting drains 3 bytes
and replies `[5, 0]`; a username/password greeting drains 4 bytes and replies
`[5, 2]`; an unsupported method yields `NotImplemented`, whose `Display` is
exactly `SOCKS feature not implemented`.

**`Action` carries no request.** It is exactly three public fields:

```rust
#[non_exhaustive]
pub struct Action { pub drain: usize, pub reply: Vec<u8>, pub finished: bool }

impl SocksHandshake {
    pub fn new() -> Self;                      // and `impl Default`
    pub fn finished(&self) -> bool;
    pub fn into_request(self) -> Option<SocksRequest>;
}
```

A complete SOCKS4 CONNECT drains the whole input, replies with nothing, and sets
`finished`; the `SocksRequest` is then taken out of the handshake with
`into_request()`, and its `version()`, `command()`, `port()`, `addr()` and
`auth()` accessors report the submitted values.

---

## 7. Inherited error taxonomies

Each of these crates implements `tor_error::HasKind` for its own error type, and
each is checked by a group that asserts two things: the classification table in
§7.1, and the exact `Display` rendering in §7.3. **The `Display` strings are part
of the required contract, not incidental prose** — this step is a version
conformance step, and the renderings below are the observable a
correctly-implemented taxonomy has to produce. A group asserting a string that
§7.3 does not list would be asserting an implementation detail; every string any
group asserts is listed there.

| crate | module path | type | items of the table checked |
| --- | --- | --- | --- |
| `tor-proto` | `tor_proto::Error` | protocol/channel/circuit errors | I/O classified by site, malformed traffic, closed circuit, caller misuse, remote refusal, `EndReceived` by reason, `From<Error> for io::Error` preserving the kind |
| `tor-cell` | `tor_cell::Error`, `tor_cell::relaycell::msg::EndReason` | cell coding and END reasons | unparsable cell, truncation inside a cell, encode faults, misuse, `EndReason` code points 1–14, `EndReason` classification, unknown reason usable, `caret_int` display |
| `tor-chanmgr` | `tor_chanmgr::Error` | channel-manager setup | unreachable relay, channel I/O reclassified from the proto layer, protocol verdict preserved, own faults, spawn failure |
| `tor-circmgr` | `tor_circmgr::Error` | circuit building | no path, no exit, discarded circuit, timeout, shutdown vs expired directory, wrapped errors delegate |
| `tor-dirclient` | `tor_dirclient::Error` | directory HTTP client | timeout, malformed reply, stream trouble, retire-circuit predicate, delegation through nesting |
| `tor-dirmgr` | `tor_dirmgr::Error` | directory manager | corrupted cache, cache access failure, manager lifecycle and configuration faults |
| `tor-persist` | `tor_persist::Error` | state persistence | unreachable state, save without lock, unreadable state, unwritable state |
| `tor-config` | `tor_config::{ConfigBuildError, ReconfigureError}` | configuration | rejected setting names its field, nested section prefixes the field, change-while-running is distinct from a plain invalid setting |
| `tor-netdoc` | `tor_netdoc::{ParseErrorKind, Pos}` | document parsing | `ParseErrorKind` display table, `ParseErrorKind` is `Copy` and comparable, `Pos` display for a resolvable and for an out-of-range offset |

`tor-netdoc::Error` itself is opaque in this step — it was an enum in step 05
and is a struct with a `ParseErrorKind` here — so only `ParseErrorKind` and
`Pos`, which have public constructors and public `Display`, are checked.


### 7.1 The enum declarations and the variant→kind tables

Each crate below is given twice over: first the **declaration** — its derives,
its `#[non_exhaustive]`, and every variant with its exact payload type — and
then the **variant→kind table**, which has a row for every variant of that
declaration. Both are checked, and the declaration is checked first, because a
`*kind` group *constructs* these variants: it writes
`Error::ChanIoErr(Arc::new(io_err))` and `Error::CircRefused("no such relay")`
as literals in its own source. A payload that is a `std::io::Error` where the
declaration says `Arc<std::io::Error>`, or a `String` where it says
`&'static str`, does not compile — and a group that does not compile cannot
pass, however right its `kind()` mapping is.

The `kind()` mapping is not derivable from the variant names: `E::ChanIoErr` is
`LocalNetworkError` while `E::HandshakeIoErr` is `TorAccessFailed`, and the
near-identical I/O variants of two crates are spelled differently and classified
differently — `tor_dirmgr::Error::IOError` (capital `O`) is `CacheAccessFailed`,
while `tor_persist::Error::IoError` (lowercase `o`) is
`PersistentStateAccessFailed`. Both spellings are real, in different crates.

A variant that is absent from the enum, spelled differently, given a different
payload type, or given a different kind, fails its group.

Three conventions hold throughout the declaration blocks:

- `#[source]` and `#[from]` are the `thiserror` attributes and are **part of the
  declaration**, not decoration. `#[from]` generates a `From` impl for that
  payload and `#[source]` deliberately does not; groups and the crates above
  them use `?` across several of these, so moving one to the other spelling
  breaks callers that never name the variant.
- `// was …` marks a shape that an earlier step's contract states differently.
  Where that appears, **this section governs** and the earlier statement is
  superseded; `instruction.md` §2 points here for exactly that reason.
- `Arc<…>` is never incidental. Every error enum here is `Clone`, and the
  payloads that are not — `std::io::Error`, `serde_json::Error`,
  `rusqlite::Error`, `http::Error`, `signature::Error`,
  `futures::task::SpawnError` — are wrapped in an `Arc` to make that possible.
  That is why `ChanIoErr` cannot take a bare `std::io::Error`.

Every table below covers its whole enum. The one exception is
`tor_netdoc::ParseErrorKind`, which is not an error enum and says so in place.

**`tor_proto::Error`** (`protokind`). Twenty-two variants:

```rust
// `Clone` is not optional: `tor_circmgr::Error` and `tor_dirclient::Error` both
// embed this type by value and are themselves `Clone`.
#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    BytesErr(#[from] tor_bytes::Error),
    ChanIoErr(#[source] Arc<std::io::Error>),        // Arc, not a bare io::Error
    HandshakeIoErr(#[source] Arc<std::io::Error>),   // Arc, not a bare io::Error
    CellErr(#[source] tor_cell::Error),              // #[source], NOT #[from]
    InvalidOutputLength,
    NoSuchHop,
    BadCellAuth,
    BadCircHandshake,
    HandshakeProto(String),
    ChanProto(String),
    CircProto(String),
    ChannelClosed,
    CircuitClosed,
    IdRangeFull,
    CircRefused(&'static str),                       // &'static str, not String
    BadStreamAddress,
    EndReceived(EndReason),                          // tor_cell::relaycell::msg::EndReason
    NotConnected,
    StreamProto(String),
    ChanMismatch(String),
    Bug(#[from] tor_error::Bug),
    ResolveError(String),
}
```

`CircRefused` is the only `&'static str` payload in this enum and the four
`*Proto` variants are all `String`; `CellErr` is `#[source]` while `BytesErr`
and `Bug` are `#[from]`, so `?` lifts a `tor_bytes::Error` or a `Bug` into this
type but not a `tor_cell::Error`.

Variants carrying a source delegate:

| Variant | `kind()` |
| --- | --- |
| `BytesErr(e)` | `e.kind()` when `e` is a `Bug`, else `TorProtocolViolation` |
| `ChanIoErr(_)` | `LocalNetworkError` |
| `HandshakeIoErr(_)` | `TorAccessFailed` |
| `CellErr(e)` | `e.kind()` |
| `InvalidOutputLength` | `Internal` |
| `NoSuchHop`, `IdRangeFull`, `BadStreamAddress`, `NotConnected` | `BadApiUsage` |
| `BadCellAuth`, `BadCircHandshake`, `ChanProto(_)`, `CircProto(_)`, `StreamProto(_)` | `TorProtocolViolation` |
| `HandshakeProto(_)` | `TorAccessFailed` |
| `ChannelClosed`, `CircuitClosed` | `CircuitCollapse` |
| `CircRefused(_)` | `CircuitRefused` |
| `EndReceived(reason)` | `reason.kind()` |
| `ChanMismatch(_)` | `RelayIdMismatch` |
| `ResolveError(_)` | `RemoteHostNotFound` |
| `Bug(e)` | `e.kind()` |

**`impl From<tor_proto::Error> for std::io::Error`** is declared, is total, and
does **not** collapse every protocol failure to `io::ErrorKind::Other`.
`protokind` asserts six exact `io::ErrorKind` values through it, so the whole
arm table is contract rather than illustration. The arms are ordered, and the
guarded `Bug` arm must come before the catch-all row or `BadApiUsage` bugs fall
through to `Other`:

| source variant(s) | resulting `io::ErrorKind` |
| --- | --- |
| `ChanIoErr(e)`, `HandshakeIoErr(e)` | the **inner** error's kind — see below |
| `InvalidOutputLength`, `NoSuchHop`, `BadStreamAddress` | `InvalidInput` |
| `NotConnected` | `NotConnected` |
| `EndReceived(reason)` | `std::io::ErrorKind::from(reason)` — §7.2's second table |
| `ChannelClosed`, `CircuitClosed` | `ConnectionReset` |
| `BytesErr(_)`, `BadCellAuth`, `BadCircHandshake`, `HandshakeProto(_)`, `ChanProto(_)`, `CircProto(_)`, `CellErr(_)`, `ChanMismatch(_)`, `StreamProto(_)` | `InvalidData` |
| `Bug(e)` where `e.kind() == tor_error::ErrorKind::BadApiUsage` | `InvalidData` |
| `IdRangeFull`, `CircRefused(_)`, `ResolveError(_)`, any other `Bug(_)` | `Other` |

Every row but the first builds `std::io::Error::new(kind, err)`, so the original
`tor_proto::Error` survives as the `io::Error`'s payload and `source()`. The two
I/O variants instead **return the inner error**: `Arc::try_unwrap` hands back
the original `std::io::Error` when the `Arc` is unique, and
`std::io::Error::new(arc.kind(), arc)` is built when it is not. Either way the
observed kind is the inner error's own, so
`ChanIoErr(Arc::new(io::Error::new(ErrorKind::BrokenPipe, "…")))` converts to an
`io::Error` whose `.kind()` is `BrokenPipe` — not `Other`, and not anything
derived from the `LocalNetworkError` that the same variant reports to
`tor_error`. This is the one place where the two classifications of a single
variant are computed from different inputs.

**`tor_cell::Error`** (`cellkind`). Five variants:

```rust
#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    BytesErr(#[from] tor_bytes::Error),
    Internal(tor_error::Bug),   // was `InternalError(String)`; plain field, no attribute
    ChanProto(String),
    BadStreamAddress,
    CantEncode,
}
```

Step 04 §10.0 ("Crate root: the crate's own error type") declares this enum with
six variants, `IoErr(#[source] std::sync::Arc<std::io::Error>)` among them and
`InternalError(String)` for the last. Both statements are superseded: at this
tag there is no `IoErr` at all, and the internal variant is named `Internal` and
carries a `tor_error::Bug`. Note that its field bears **neither** `#[from]` nor
`#[source]` — `Bug` does not convert into `tor_cell::Error` with `?`, and the
`Bug` is not reported as `source()`; it is interpolated into the `Display`
instead (§7.3).

`BytesErr` carries `tor_bytes::Error`, which both this crate and `tor-proto`
wrap, so it is declared here once:

```rust
// `tor_bytes::Error`
#[derive(thiserror::Error, Debug, Clone)]     // no `Eq`, and `PartialEq` is not derived
#[non_exhaustive]
pub enum Error {
    Truncated,
    ExtraneousBytes,
    BadMessage(&'static str),
    Bug(#[from] tor_error::Bug),   // was a unit variant `Internal`
}

// Hand-written, not derived: a `Bug` compares equal to nothing, not even to
// itself, and every other pair compares structurally.
impl PartialEq for Error;
```

Two earlier statements are superseded here. Step 01 §2 ("Shared items: the
`tor-bytes` reader and writer") gives the fourth variant as a unit `Internal`;
it is `Bug(#[from] tor_error::Bug)` at this tag, which is how a `tor_error::Bug`
travels up through `tor_cell::Error::BytesErr` and
`tor_proto::Error::BytesErr` to reach the `e.kind()` rows in the tables above.
Step 04 §11 derives `PartialEq, Eq` on it; at this tag `Eq` does not exist and
`PartialEq` is hand-written with the asymmetric `Bug` rule above, so a candidate
that derives either one is wrong.

The `tor_cell::Error` `kind()` match has six arms for five variants, because
`BytesErr` is split by its payload — the rows below are match arms, in order,
and the first `BytesErr` row is a pattern on the same variant as the second:

| Variant | `kind()` |
| --- | --- |
| `BytesErr(tor_bytes::Error::Truncated)` | `Internal` |
| `BytesErr(_)` | `TorProtocolViolation` |
| `Internal(_)` | `Internal` |
| `ChanProto(_)` | `TorProtocolViolation` |
| `BadStreamAddress` | `BadApiUsage` |
| `CantEncode` | `Internal` |

**`tor_chanmgr::Error`** (`chankind`). Seven variants, restated from §5, which
gives the step-05 deltas and the `Io` `Display` string alongside them:

```rust
#[derive(Debug, thiserror::Error, Clone)]
#[non_exhaustive]
pub enum Error {
    UnusableTarget(#[source] tor_error::Bug),
    PendingFailed,
    ChanTimeout,
    Proto(#[from] tor_proto::Error),
    Io { peer: SocketAddr, action: &'static str, #[source] source: Arc<std::io::Error> },
    Spawn { spawning: &'static str, #[source] cause: Arc<futures::task::SpawnError> },
    Internal(#[from] tor_error::Bug),
}
```

`Io` and `Spawn` are struct variants with named fields; `chankind` builds them
field by field, so a tuple variant of the same arity does not compile.

| Variant | `kind()` |
| --- | --- |
| `ChanTimeout` | `TorAccessFailed` |
| `Io { .. }` | `TorAccessFailed` |
| `Proto(tor_proto::Error::ChanIoErr(_))` | `TorAccessFailed` |
| `Proto(e)` (any other) | `e.kind()` |
| `Spawn { cause, .. }` | `cause.kind()` |
| `PendingFailed` | `TorAccessFailed` |
| `UnusableTarget(_)` | `Internal` |
| `Internal(_)` | `Internal` |

**`tor_circmgr::Error`** (`circkind`). Sixteen variants:

```rust
#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    GuardNotUsable,
    PendingCanceled,
    CircCanceled,                                   // one `l`, US spelling
    CircTimeout,
    RequestTimeout,
    NoPath(String),
    NoExit(String),
    GuardMgr(#[source] tor_guardmgr::GuardMgrError),
    Guard(#[from] tor_guardmgr::PickGuardError),
    RequestFailed(retry_error::RetryError<Box<Error>>),
    Channel { peer: tor_linkspec::OwnedChanTarget, #[source] cause: tor_chanmgr::Error },
    Protocol(#[from] tor_proto::Error),
    ExpiredConsensus,
    Spawn { spawning: &'static str, #[source] cause: Arc<futures::task::SpawnError> },
    State(#[from] tor_persist::Error),
    Bug(#[from] tor_error::Bug),
}
```

Step 02 §8.2 calls its fourteen-variant list "the whole variant set at this
release"; it was, at that release, and it is superseded here. Of the names it
gives, `NoRelays(String)`, `NeedConsensus`, `UsageNotSupported(String)`,
`Internal(String)` and `ChanFailed(#[from] tor_chanmgr::Error)` no longer
exist. `Internal(String)` became `Bug(#[from] tor_error::Bug)` and
`ChanFailed` became the struct variant `Channel { peer, cause }`, which carries
the peer alongside the cause — `circkind` builds it with both fields and reads
`cause.kind()` back out. `NoPath`, `NoExit`, `CircCanceled`, `ExpiredConsensus`
and `Spawn` are new since that list. The properties step 02 states about the
type — `Clone` and `Debug`, and neither `PartialEq` nor `Eq` — are still true.

| Variant | `kind()` |
| --- | --- |
| `Channel { cause, .. }` | `cause.kind()` |
| `Bug(e)` | `e.kind()` |
| `NoPath(_)` | `NoPath` |
| `NoExit(_)` | `NoExit` |
| `PendingCanceled` | `ReactorShuttingDown` |
| `CircTimeout`, `RequestTimeout` | `TorNetworkTimeout` |
| `GuardNotUsable`, `CircCanceled` | `TransientFailure` |
| `RequestFailed(e)` | see the note below — the kind of the **most severe** source, or `Internal` when there are none |
| `Protocol(e)`, `State(e)`, `GuardMgr(e)` | `e.kind()` |
| `Guard(_)` | `NoPath` |
| `ExpiredConsensus` | `DirectoryExpired` |
| `Spawn { cause, .. }` | `cause.kind()` |

`RequestFailed` carries a `RetryError<Box<Error>>` — a *collection* of the
errors from the failed attempts, not a single error and not a `Result`. Its kind
is taken from the source with the **highest severity**, where severity is a
private ranking over the enum, and falls back to `Internal` only when the
collection is empty:

```rust
E::RequestFailed(e) => e
    .sources()
    .max_by_key(|e| e.severity())
    .map(|e| e.kind())
    .unwrap_or(EK::Internal),
```

With a single source this is just that source's kind, which is the only case
the group exercises; with two sources of differing severity, taking the first or
an arbitrary one is wrong.

The same group also covers **`tor_dirclient::Error`**, so `dirkind` asserts
both tables. Ten variants:

```rust
#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    DirTimeout,
    TruncatedHeaders,
    ResponseTooLong(usize),                              // usize, not u64
    Utf8Encoding(#[from] std::string::FromUtf8Error),    // String::from_utf8's error
    IoError(#[source] Arc<std::io::Error>),              // lowercase `o`; cf. tor_dirmgr
    Proto(#[from] tor_proto::Error),
    CircMgr(#[from] tor_circmgr::Error),
    HttparseError(#[from] httparse::Error),
    HttpError(#[source] Arc<http::Error>),               // Arc: http::Error is not Clone
    ContentEncoding(String),
}
```


| Variant | `kind()` |
| --- | --- |
| `DirTimeout` | `TorNetworkTimeout` |
| `TruncatedHeaders`, `ResponseTooLong(_)`, `Utf8Encoding(_)`, `HttparseError(_)`, `ContentEncoding(_)` | `TorProtocolViolation` |
| `IoError(_)` | `TorDirectoryError` |
| `Proto(e)`, `CircMgr(e)` | `e.kind()` |
| `HttpError(_)` | `Internal` |

`tor_dirclient::Error` also carries one inherent predicate, which the group
asserts over four different variants:

```rust
impl tor_dirclient::Error {
    pub fn should_retire_circ(&self) -> bool;
}
```

It answers "is the circuit this error came from still reusable?", and at this
version the answer is **unconditionally `true`** for every variant: a directory
circuit that produced any error at all is dropped rather than reused. A
candidate that tries to be clever and returns `false` for, say, a timeout fails
`dirkind`.

**`tor_dirmgr::Error`** (also `dirkind`, the second of its two tables).
Twenty-two variants:

```rust
#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    Unwanted(&'static str),                       // &'static str
    NoDownloadSupport,
    CacheCorruption(&'static str),                // &'static str
    SqliteError(#[source] Arc<rusqlite::Error>),
    UnrecognizedSchema,
    BadNetworkConfig(&'static str),               // &'static str
    DirectoryNotPresent,
    UnrecognizedAuthorities,
    ManagerDropped,
    CantAdvanceState,
    StorageError(String),                         // String — unlike the three above
    ConsensusDiffError(#[from] tor_consdiff::Error),
    BadUtf8FromDirectory(#[source] std::string::FromUtf8Error),  // String::from_utf8
    BadUtf8InCache(#[source] std::str::Utf8Error),               // std::str::from_utf8
    BadHexInCache(#[source] hex::FromHexError),
    NetDocError { source: DocSource, #[source] cause: tor_netdoc::Error },
    DirClientError(#[from] tor_dirclient::Error),
    SignatureError(#[source] Arc<signature::Error>),
    IOError(#[source] Arc<std::io::Error>),       // capital `O`; cf. tor_dirclient
    OfflineMode,
    Spawn { spawning: &'static str, #[source] cause: Arc<futures::task::SpawnError> },
    Bug(#[from] tor_error::Bug),
}

#[derive(Debug, Clone, derive_more::Display)]
#[non_exhaustive]
pub enum DocSource {
    LocalCache,      // Display: `local cache`
    DirServer {},    // Display: `directory server` — a struct variant with NO fields
}
```

The two UTF-8 variants are a trap in both directions: `BadUtf8FromDirectory`
takes `std::string::FromUtf8Error` (what `String::from_utf8` returns, which owns
the bytes) and `BadUtf8InCache` takes `std::str::Utf8Error` (what
`std::str::from_utf8` returns, which does not). They are different types;
swapping them does not compile, and the `kind()` table below gives them
different kinds. `DirServer {}` is a **struct**
variant with an empty field list — `DocSource::DirServer` without the braces is
not a value.

| Variant | `kind()` |
| --- | --- |
| `Unwanted(_)`, `BadUtf8FromDirectory(_)`, `UnrecognizedAuthorities`, `ConsensusDiffError(_)`, `SignatureError(_)` | `TorProtocolViolation` |
| `NoDownloadSupport` | `NotImplemented` |
| `CacheCorruption(_)`, `UnrecognizedSchema`, `BadUtf8InCache(_)`, `BadHexInCache(_)` | `CacheCorrupted` |
| `SqliteError(e)` | by `sqlite_error_kind(e)` |
| `BadNetworkConfig(_)` | `InvalidConfig` |
| `DirectoryNotPresent` | `DirectoryExpired` |
| `ManagerDropped` | `ArtiShuttingDown` |
| `CantAdvanceState` | `TorAccessFailed` |
| `StorageError(_)`, `IOError(_)` | `CacheAccessFailed` |
| `NetDocError { source, .. }` | `CacheCorrupted` for `DocSource::LocalCache`, `TorProtocolViolation` for `DocSource::DirServer { .. }` |
| `DirClientError(e)` | `e.kind()` |
| `OfflineMode` | `BadApiUsage` |
| `Spawn { cause, .. }` | `cause.kind()` |
| `Bug(e)` | `e.kind()` |

**`tor_persist::Error`** (`statekind`). Four variants:

```rust
#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    IoError(#[source] Arc<std::io::Error>),
    NoLock,
    Serialize(#[source] Arc<serde_json::Error>),     // was the single `JsonError`
    Deserialize(#[source] Arc<serde_json::Error>),   // was the single `JsonError`
}

impl From<std::io::Error> for Error;   // -> IoError(Arc::new(e))
// There is NO `impl From<serde_json::Error> for Error` at this tag: the two
// JSON variants are produced by private helpers that know which direction
// failed, so `?` on a `serde_json::Error` does not compile here.
```

Step 02 §1.6 declares a three-variant enum whose JSON variant is
`JsonError(#[source] Arc<serde_json::Error>)`, plus
`impl From<serde_json::Error> for Error`. Both are superseded. The single
`JsonError` has been split into `Serialize` and `Deserialize`, which is the
whole point of the table below — they render identically and are distinguished
only by `kind()` — and the `From` impl is gone along with the variant it
produced. `From<std::io::Error>`, which step 02 also declares, survives
unchanged.

| Variant | `kind()` |
| --- | --- |
| `IoError(..)` | `PersistentStateAccessFailed` |
| `NoLock` | `BadApiUsage` |
| `Serialize(..)` | `Internal` |
| `Deserialize(..)` | `PersistentStateCorrupted` |

Note the two variants whose `Display` is the same string — `Serialize` and
`Deserialize` both render `JSON serialization error` — so `Display` cannot be
used to tell them apart; only the kind can.

**`tor_config`** (also `statekind`, which asserts these kinds alongside
`tor_persist::Error`'s):

```rust
#[derive(Debug, Clone, thiserror::Error)]
#[non_exhaustive]
pub enum ConfigBuildError {
    MissingField { field: String },
    Invalid { field: String, problem: String },
    Inconsistent { fields: Vec<String>, problem: String },   // Vec<String>, not &[str]
}

impl ConfigBuildError {
    #[must_use]
    pub fn within(&self, prefix: &str) -> Self;   // borrows; returns a new value
}

#[derive(Debug, Clone, thiserror::Error)]
#[non_exhaustive]
pub enum ReconfigureError {
    CannotChange { field: String },
}
```

| Type | `kind()` |
| --- | --- |
| `ConfigBuildError`, every variant | `InvalidConfig` |
| `ReconfigureError`, every variant | `InvalidConfigTransition` |

Both impls are constant functions of the type, not of the variant, and the two
kinds are distinct: a setting that is invalid and a setting that merely cannot
be changed while running do not report the same kind. All three
`ConfigBuildError` variants are struct variants; `within` takes `&self` and is
`#[must_use]`, so it does not mutate in place. Their `Display` strings are in
§7.3.

**`tor_netdoc`** (`netdocerr`). `tor_netdoc::Error` is a struct at this
version (it was an enum in step 05); the in-scope surface is `ParseErrorKind`,
which has a public constructor and a public `Display`, and `Pos`.

```rust
// `Copy` and `PartialEq` are asserted by the group and are derives, not impls.
// `Display` comes from `derive_more`, one `#[display(fmt = "…")]` per variant.
#[derive(Copy, Clone, Debug, derive_more::Display, PartialEq)]
#[non_exhaustive]
pub enum ParseErrorKind { /* 30 variants; the four below are the checked ones */ }

impl Pos {
    pub fn from_offset(s: &str, off: usize) -> Pos;   // takes the text AND the offset
}
```

This is the one type in §7.1 whose table is a selection rather than its whole
enum: `ParseErrorKind` has thirty variants, it carries no payloads, it has no
`kind()` at all, and only the four rows below are contract.

| `ParseErrorKind` | `to_string()` |
| --- | --- |
| `Internal` | `internal error` |
| `BadApiUsage` | `bad API usage` |
| `MissingKeyword` | `no keyword for entry` |
| `BadSignature` | `couldn't validate signature` |

`ParseErrorKind` is `Copy` and comparable. `Pos::from_offset(text, off)` takes
the *text* and an offset into it, and its `Display` is the **suffix** of an
error message — note the leading space:

| Argument | `to_string()` |
| --- | --- |
| `from_offset("abc\ndef", 5)` — resolvable | ` on line 2, byte 2` |
| `from_offset("abc", 10)` — past the end | ` at invalid offset at index 10` |

The offset is byte-based and starts at 1 by the time it is rendered (offset 5
into a three-character first line is line 2, byte 2).

### 7.2 `EndReason` — the END-cell reason taxonomy

`tor_cell::relaycell::msg::EndReason` is not an enum. It is a `caret_int!`
newtype over `u8`, so the code-point space is **open**: any byte is a valid
value, conversion in either direction is total and never panics, and there is a
catch-all classification for a reason this version does not recognise.

```rust
caret_int! {
    pub struct EndReason(u8) { MISC = 1, /* … */ NOTDIRECTORY = 14 }
}
impl tor_error::HasKind for EndReason;
impl From<EndReason> for u8;
impl From<u8> for EndReason;
impl From<EndReason> for std::io::ErrorKind;   // the second table below
```

The fourteen code points are fixed by tor-spec (`SPEC_NOTES.md`, tor-spec.txt
§6.3); `protokind` reaches them through `tor_proto::Error::EndReceived(reason)`,
whose `kind()` is `reason.kind()`, and `cellkind` asserts the whole table
directly.

| Name | Code | `kind()` |
| --- | --- | --- |
| `MISC` | 1 | `RemoteStreamError` |
| `RESOLVEFAILED` | 2 | `RemoteHostNotFound` |
| `CONNECTREFUSED` | 3 | `RemoteConnectionRefused` |
| `EXITPOLICY` | 4 | `ExitPolicyRejected` |
| `DESTROY` | 5 | `CircuitCollapse` |
| `DONE` | 6 | `RemoteStreamClosed` |
| `TIMEOUT` | 7 | `ExitTimeout` |
| `NOROUTE` | 8 | `RemoteNetworkFailed` |
| `HIBERNATING` | 9 | `RelayTooBusy` |
| `INTERNAL` | 10 | `TorProtocolViolation` |
| `RESOURCELIMIT` | 11 | `RelayTooBusy` |
| `CONNRESET` | 12 | `RemoteStreamReset` |
| `TORPROTOCOL` | 13 | `TorProtocolViolation` |
| `NOTDIRECTORY` | 14 | `TorProtocolViolation` |
| any other byte | as given | `RemoteStreamError` |

The names are SHOUTING, exactly as written above, because that is how tor-spec
spells them; a candidate that writes `Done` or `ConnRefused` fails `cellkind`.

`EndReason` has a **second, independent** classification:
`impl From<EndReason> for std::io::ErrorKind`. It is what the `EndReceived` arm
of `From<tor_proto::Error> for std::io::Error` (§7.1) calls, and `protokind`
asserts it through that path, so it is checked and it is not derivable from the
`tor_error::ErrorKind` column — five reasons carrying four different
`tor_error::ErrorKind`s all land on `ConnectionRefused` here:

| Name | `tor_error::ErrorKind` (above) | `std::io::ErrorKind` |
| --- | --- | --- |
| `MISC` | `RemoteStreamError` | `Other` |
| `RESOLVEFAILED` | `RemoteHostNotFound` | `NotFound` |
| `CONNECTREFUSED` | `RemoteConnectionRefused` | `ConnectionRefused` |
| `EXITPOLICY` | `ExitPolicyRejected` | `ConnectionRefused` |
| `DESTROY` | `CircuitCollapse` | `ConnectionAborted` |
| `DONE` | `RemoteStreamClosed` | `UnexpectedEof` |
| `TIMEOUT` | `ExitTimeout` | `TimedOut` |
| `NOROUTE` | `RemoteNetworkFailed` | `Other` |
| `HIBERNATING` | `RelayTooBusy` | `ConnectionRefused` |
| `INTERNAL` | `TorProtocolViolation` | `Other` |
| `RESOURCELIMIT` | `RelayTooBusy` | `ConnectionRefused` |
| `CONNRESET` | `RemoteStreamReset` | `ConnectionReset` |
| `TORPROTOCOL` | `TorProtocolViolation` | `InvalidData` |
| `NOTDIRECTORY` | `TorProtocolViolation` | `ConnectionRefused` |
| any other byte | `RemoteStreamError` | `Other` |

`DONE` → `UnexpectedEof` rather than something success-shaped, `NOTDIRECTORY`
→ `ConnectionRefused` rather than `InvalidData`, and `INTERNAL` → `Other`
rather than `InvalidData` are the three rows a plausible guess gets wrong. The
conversion is total, like every other `EndReason` conversion: the catch-all is
`Other`, so no byte panics.

`caret_int!` also fixes the rendering, and the two renderings differ:

| Expression | Result |
| --- | --- |
| `EndReason::EXITPOLICY.to_string()` | `EXITPOLICY` — a recognised value renders by name |
| `EndReason::from(200).to_string()` | `200` — an unrecognised value renders as its decimal number |
| `format!("{:?}", EndReason::DONE)` | `EndReason(DONE)` — `Debug` wraps the name in the type name |
| `u8::from(EndReason::from(200))` | `200` — the round trip is lossless for any byte |

### 7.3 The `Display` strings

Every `Display` string any group asserts is either listed below verbatim or is
an instantiation of one of these templates with the group's own payload
substituted. `{0}`, `{action}`, `{peer}` and `{spawning}` are that payload,
interpolated at exactly that position; everything else is literal, including the
capitalisation, which is inconsistent between crates and is reproduced as-is. A
few rows are listed even though no case asserts them, because they sit next to
one that is. These tables are **not** the whole of each crate's rendered
surface, and they are not meant to be: §7.1 lists every variant of every enum
here, and a variant with no row below still renders — it is simply not asserted,
so its string is not contract.

**`tor_proto::Error`** (`protokind`):

| Variant | `to_string()` |
| --- | --- |
| `ChannelClosed` | `channel closed` |
| `CircuitClosed` | `circuit closed` |
| `NotConnected` | `stream not connected` |
| `BadStreamAddress` | `invalid stream target address` |
| `CircProto(s)` | `circuit protocol violation: {0}` |
| `ChanProto(s)` | `channel protocol violation: {0}` |
| `EndReceived(r)` | `Received an End cell with reason {0}` — `{0}` is §7.2's rendering, so `EXITPOLICY` gives `Received an End cell with reason EXITPOLICY` |

**`tor_cell::Error`** (`cellkind`):

| Variant | `to_string()` |
| --- | --- |
| `BytesErr(e)` | `parsing error: {0}` — e.g. `parsing error: extra bytes at end of object` |
| `ChanProto(s)` | `channel protocol violation: {0}` |
| `BadStreamAddress` | `invalid stream target address` |
| `CantEncode` | `Message can't be represented in a Tor cell.` — capital `M`, and it is the one string here that ends in a full stop |

**`tor_chanmgr::Error`** (`chankind`):

| Variant | `to_string()` |
| --- | --- |
| `ChanTimeout` | `Channel timed out` |
| `PendingFailed` | `Pending channel failed to launch` |
| `Proto(e)` | `Protocol error while opening a channel: {0}` — so `Proto(tor_proto::Error::ChannelClosed)` is `Protocol error while opening a channel: channel closed` |
| `Io { peer, action, .. }` | `Network IO error, or TLS error, in {action}, talking to {peer}` |
| `Spawn { spawning, .. }` | `unable to spawn {spawning}` |

**`tor_circmgr::Error`** (`circkind`):

| Variant | `to_string()` |
| --- | --- |
| `CircTimeout` | `Circuit took too long to build` |
| `RequestTimeout` | `Spent too long waiting for a circuit to build` |
| `CircCanceled` | `Circuit canceled` — one `l`, US spelling |
| `ExpiredConsensus` | `Consensus is expired` |
| `NoExit(s)` | `Can't find exit for circuit: {0}` |
| `Protocol(e)` | `Problem building a circuit: {0}` |
| `Spawn { spawning, .. }` | `unable to spawn {spawning}` |

**`tor_dirclient::Error`** (`dirkind`):

| Variant | `to_string()` |
| --- | --- |
| `DirTimeout` | `directory timed out` — lowercase, unlike the circmgr strings above |
| `ResponseTooLong(n)` | `response too long; gave up after {0} bytes` |

**`tor_dirmgr::Error`** (`dirkind`):

| Variant | `to_string()` |
| --- | --- |
| `DirClientError(e)` | `dirclient error: {0}` — so wrapping a `DirTimeout` gives `dirclient error: directory timed out` |
| `DirectoryNotPresent` | `directory not present or not up-to-date` |
| `CacheCorruption(s)` | `corrupt cache: {0}` |

**`tor_persist::Error`** (`statekind`):

| Variant | `to_string()` |
| --- | --- |
| `IoError(_)` | `IO error` — the I/O error itself is the `source()`, and is **not** interpolated into the message |
| `NoLock` | `Storage not locked` |
| `Serialize(_)`, `Deserialize(_)` | `JSON serialization error` — the same string for both, which is why only `kind()` can tell them apart |

**`tor_socksproto::Error`** (`socksdiag`, `socksres`):

| Variant | `to_string()` |
| --- | --- |
| `BadProtocol(b)` | `Unrecognized SOCKS protocol version {0}` — `{0}` is the byte in decimal, so `G` (0x47) gives `… version 71` |
| `NotImplemented` | `SOCKS feature not implemented` |
| `AlreadyFinished(_)` | `SOCKS handshake was finished; no need to call this again` |
| `Syntax` | `SOCKS protocol syntax violation` |
| `Decode(e)` | `Error decoding message` |

**The `tor_config` error surface is checked through `statekind`.** The three
`ConfigBuildError` variants and their renderings — `MissingField` →
`Field was not provided: <field>`, `Invalid` → `Value of <field> was incorrect:
<problem>`, `Inconsistent` → `Fields ["a", "b"] are inconsistent: <problem>`
(rendered with `{:?}` over the field vector) — plus `within(section)`, which
prefixes each field with the section and a dot, are stated in step 03's
contract and restated here because `statekind` asserts them. `ReconfigureError::CannotChange { field }`
renders `Cannot change <field> on a running client.`

**`tor_config`** (`cfgsources`, `cfgwatch`). There is no dedicated
`tor_config` kind group at this step: the configuration error surface is checked
through the loading and watching groups, which assert that a rejected setting
names its field, that a nested section prefixes the field, and that
change-while-running is distinct from a plain invalid setting.

---

## 8. Inherited TLS backends

Module paths: `tor_rtcompat` (crate root), `tor_rtcompat::tokio`.

```rust
// `Spawn` in this bound list is `futures::task::Spawn` — step 05's contract
// states it, and it is what makes the "accept a task through
// `futures::task::SpawnExt::spawn`" observable below well-formed. The
// backends implement it (`spawn_obj`), and a workspace whose runtimes carry a
// `Spawn` trait of their own instead fails the `tlsbackend` group at the call
// site.
pub trait Runtime: Sync + Send + Spawn + BlockOn + Clone + SleepProvider
                 + TcpProvider + TlsProvider<Self::TcpStream> + 'static {}
pub trait BlockOn { fn block_on<F: Future>(&self, future: F) -> F::Output; }
pub trait SleepProviderExt: SleepProvider {
    fn timeout<F: Future>(&self, d: Duration, f: F) -> Timeout<F>;
}
pub mod tor_rtcompat::tokio {
    pub struct TokioNativeTlsRuntime { /* opaque */ }
    pub struct TokioRustlsRuntime { /* opaque */ }
}
impl TokioNativeTlsRuntime { pub fn create() -> Result<Self>; }
impl TokioRustlsRuntime    { pub fn create() -> Result<Self>; }

// At the crate root. At this version `PreferredRuntime` is a struct of its own
// that wraps the backend runtime; it is NOT the `pub use` alias for
// `tokio::PreferredRuntime` that step 05 declares it to be.
pub struct PreferredRuntime { /* opaque */ }
impl PreferredRuntime {
    pub fn create()  -> std::io::Result<Self>;   // always builds a new runtime
    pub fn current() -> std::io::Result<Self>;   // wraps the one already running
}
```

`PreferredRuntime` is the runtime §2.1's `TorClient::builder()` is bound to and
the one §9's `runtime()` returns, so it is named from both.

**Superseded: the runtime-constructor family.** A runtime is obtained at this
tag *only* through an inherent associated function on a named runtime type. The
free functions and the client-side shortcuts earlier steps declare do not exist,
and this section governs:

| earlier declaration | at this step |
| --- | --- |
| `tor_rtcompat::current_user_runtime()` (step 05, § *tor-rtcompat*) | `PreferredRuntime::create()`, or `::current()` inside a running reactor |
| `tor_rtcompat::create_runtime()` at the crate root (step 05, § *tor-rtcompat*) | `PreferredRuntime::create()` |
| `TorClient::bootstrap_with_tokio(cfg)` (step 05, § *arti-client*) | `TorClient::builder().config(cfg).create_bootstrapped().await` — §2.1 |
| `TorClient::bootstrap_with_async_std(cfg)` (step 05, § *arti-client*) | the same; there is no async-std-specific client constructor |
| `TorClient::bootstrap(runtime, cfg) -> Result<Self>` (step 05, § *arti-client*) | `TorClient::with_runtime(rt)` plus a builder finisher (§2.1). The name survives as `TorClient::bootstrap(&self) -> Result<()>`, which bootstraps a client that already exists — a different function with the same name. |
| `tor_rtcompat::tokio::{create_runtime, current_runtime, test_with_runtime, create_tokio_runtime}` | retired by step 05 already; still absent |
| `TokioRuntimeHandle` as a nameable type | still private to `tor-rtcompat`, as step 05 says |

Everything checked by this step reaches a runtime through exactly three
spellings: `PreferredRuntime::create()`, `TokioNativeTlsRuntime::create()` and
`TokioRustlsRuntime::create()`.

Observable: both concrete backends construct, are `Clone`, run a future to
completion through `block_on`, accept a task through `futures::task::SpawnExt::spawn`,
and compose with `SleepProviderExt::timeout`. Scope decision S4 keeps TLS
*primitives* — handshakes, certificates, cipher suites — out of the checked
surface; what is checked is that the backends still satisfy the runtime contract.

---

## 9. Driver-only support code

`tests/common/mod.rs` is out of scope but is the only place the cases share code:

```rust
pub struct ClientDirs { pub state: tempfile::TempDir, pub cache: tempfile::TempDir }
impl ClientDirs {
    pub fn new() -> Self;
    pub fn config(&self) -> TorClientConfig;
}
pub fn runtime() -> PreferredRuntime;
pub fn manual_client(rt: PreferredRuntime, dirs: &ClientDirs) -> TorClient<PreferredRuntime>;
pub fn write_file(dir: &tempfile::TempDir, name: &str, text: &str) -> PathBuf;
pub const SOCKS5_GREETING_NOAUTH: &[u8];
pub const HTTP_FIRST_BYTES: &[u8];
```

Every client the deferred-bootstrap and configuration groups build is a
`BootstrapBehavior::Manual` client over
two temporary directories, so no case touches the network and no case reads or
writes the real user profile. `bootbehav` also builds an `OnDemand` client, and
does so without using it — the value and its `Default` are what that group
asserts, and §2.2's "bootstrapping on first use" behaviour is not reachable
offline.

---

## 10. Requirement groups → interface items

Every group this step uses, and the sections above that it exercises. A group is
checked only through items named in this document; anything reachable but
undocumented is out of scope.

The shape of this step is a taxonomy: one new crate (`tor-error`) defines a
small set of error *kinds*, and most groups here check that some existing
crate's errors report the right kind. The `*kind` groups are that sweep, one
crate each.

**Declared but deliberately out of scope.** Every item in the table below is named
above because a candidate has to implement it for the rest of the surface to
make sense, but no group asserts anything about it; each row gives the reason it
is unreached. They are stated here so that a later reviewer can check against
that decision rather than counting them as untested coverage:

| Item | Where | Why it is out of scope |
| --- | --- | --- |
| `TorClientBuilder::create_bootstrapped` | §2.1 | Bootstraps, so it needs a live directory and a live network. Only `create_unbootstrapped` is reachable offline. |
| `BootstrapBehavior::OnDemand`'s implicit bootstrap | §2.1 | The *value* and its `Default`-ness are checked by `bootbehav`; the behaviour it selects — bootstrapping on first use — needs the network. Only the `Manual` half of the pair is asserted. |
| Optimistic-data tolerance of a trailing byte | §6 | Requires a live stream to the exit. The SOCKS groups check the handshake state machine, not the data path. |
| `tor_error::report_and_exit` | §1.5 | The signature is given above; it prints to stderr and calls `std::process::exit(127)`, and a case that called it would terminate the test process. Declared for the name and the shape only. |
| `TorClient::resolve` | §2.1 | A forward lookup needs a live exit stream to answer it. `resolve_ptr` is driven instead, which is the path the milestone's DNS work reaches without a network. |
| `TorClient::bootstrap(&self)` | §8 | Needs a live directory and network to succeed. The `Manual` clients never call it. |
| `TorClient::bootstrap_events` in this step | §2.3 | Declared and checked at step 05; this step's driver does not re-open the stream. `bootstrap_status` is what `bootstat` drives here. |
| `PreferredRuntime::current()` | §8 | `create()` is what every case calls; `current()` needs an ambient runtime, and `AsyncStdNativeTlsRuntime::current()` is separately declared unexercised at step 05. |
| `LoggingConfig::{logfiles, journald_filter}` | §4 | Declared out of scope above; file and journald sinks need a filesystem policy and a syslog socket. |
| `tor_bytes::Error`'s hand-written `PartialEq` and absent `Eq` | §7.1 | Asserting the *absence* of a trait impl is not something a driver can do; implement it as stated. |
| `Channel::duration_unused`'s `None`→`Some` behaviour | §5 | The accessor is reached by signature and type only; driving the transition needs a channel that has carried traffic against a live relay. |
| `LogfileConfig` | §4 | Named only as `LoggingConfig::logfiles`'s return type; no case inspects one, so its shape is out of scope. |
| `DocSource` | §7.1 | The payload of `tor_dirmgr::Error::NetDocError`; no case names or constructs it, so its two `Display` strings are contract but unasserted. |

With those listed, the coverage is complete: **every other item this document
names — every type, function, method and impl — is reached by at least one case
in `tests/assets/groups.txt`.** The one surface this claim does not enumerate
is the variant lists of §7.1: a `*kind` group constructs the variants its own
cases name, so a variant no case constructs is unasserted — contract for the
implementation rather than untested coverage to count. The table above is what
makes that a checkable claim rather than an assurance.

| Group | Kind | Items |
|---|---|---|
| `errkind` | requirement | §1.1.1 the 40 variants and their `Display`; §1.2 `HasKind`, including the impl for `futures::task::SpawnError` |
| `errbug` | requirement | §1 `Bug`, `internal!`, `bad_api_usage!`, `into_internal!`, `into_bad_api_usage!` |
| `errreport` | requirement | §1 `Report` — the whole-chain rendering wrapper |
| `errtrunc` | requirement | §1.6 `Truncated`, including `Truncated::new()` |
| `bootdefer` | requirement | §2.1 `connect`, `resolve_ptr`, `isolated_client`; §2.2 use before bootstrap reports `ErrorKind::BootstrapRequired` |
| `bootbehav` | requirement | §2 `BootstrapBehavior::{OnDemand, Manual}`, `TorClient::create_unbootstrapped` |
| `clienterr` | requirement | §2 `arti_client::Error` and its `HasKind` |
| `chanexpire` | requirement | §5 `ChanMgr` channel expiry; `tor_proto::channel::Channel` |
| `cfgwatch` | requirement | §4 `ConfigurationSources`, `ArtiConfig`, `ApplicationConfig` |
| `cfgsources` | requirement | §4 `ConfigurationSources` — source ordering and precedence |
| `hyperkind` | requirement | §3 `arti_hyper::ConnectionError` and its `HasKind` |
| `hyperconn` | requirement | §3 `ArtiHttpConnector`, `Service` impl |
| `socksdiag` | requirement | §6 `tor_socksproto::Error`, `SocksHandshake` diagnostics; §7.3 strings |
| `socksres` | requirement | §6 `Action`, `TResult`, `into_request`, `finished`, the drains; **step 02 §5.1** for `SocksAddr`, `SocksAuth`, `SocksCmd`, `SocksVersion` (this step does not redeclare them); §7.3 strings |
| `protokind` | requirement | §7.1 `tor_proto::Error` → `ErrorKind`; §7.2 `EndReason`; §7.3 strings |
| `cellkind` | requirement | §7.1 `tor_cell::Error` → `ErrorKind`; §7.2 `EndReason`; §7.3 strings |
| `circkind` | requirement | §7.1 `tor_circmgr::Error` → `ErrorKind`; §7.3 strings |
| `dirkind` | requirement | §7.1 `tor_dirmgr::Error`, `tor_dirclient::Error` → `ErrorKind` and `should_retire_circ`; §7.3 strings |
| `statekind` | requirement | §7.1 `tor_persist::Error`, `ConfigBuildError`, `ReconfigureError` → `ErrorKind`; §7.3 strings |
| `chankind` | requirement | §7.1 `tor_chanmgr::Error` → `ErrorKind`; §7.3 strings |
| `netdocerr` | requirement | §7.1 `tor_netdoc::{ParseErrorKind, Pos}` |
| `bootstat` | gate | §2.3 `BootstrapStatus` (`as_frac`, `ready_for_traffic`, `blocked`, `Display`), `TorClient::bootstrap_status`; the `Manual`/`create_unbootstrapped` pairing under §2.1 (from step 05) |
| `tlsbackend` | gate | §8 `TokioNativeTlsRuntime`, `TokioRustlsRuntime`, `Runtime` (from step 05) |
| `blockkind` | gate | §2.3 `BlockageKind` and `tor_chanmgr::ConnBlockage`, and their `Display` strings (from step 05) |
