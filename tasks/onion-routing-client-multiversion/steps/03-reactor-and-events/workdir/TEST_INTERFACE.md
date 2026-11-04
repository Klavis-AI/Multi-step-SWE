# Test interface — step 03 (`03-reactor-and-events`)

Everything this step is checked through is named below: the crate, the module
path, the exact public signature the conformance driver calls, and the
behaviour it observes. Nothing outside this document is checked.

The driver is an external crate. It reaches your workspace only through
`pub` items on the paths written here — never through `pub(crate)` items,
`#[cfg(test)]` items, private modules, or `#[doc(hidden)]`. Items are grouped
by crate; section 13 maps each requirement group to the items it uses.

Two cargo features must exist and must compile, because the driver enables
them: `tor-proto/traffic-timestamp` and `tor-dirclient/routerdesc`. The driver
also enables `tor-config/expand-paths`. What the checker actually needs is the
*items* those features gate: if a candidate never declares the feature, the
driver's manifest is repaired for it and the group still fails unless the gated
items are present and public. Declaring them is therefore the honest reading of
this paragraph and the safe one; omitting a declaration while still exposing the
items also passes.

**Every future in this step is driven by a plain executor, not a runtime.** The
driver is an external crate with no async runtime of its own: it completes the
event reactor, the channel handshake and the reactor's `run()` with
`futures::executor::block_on`, on a thread it owns. So `EventReactor::run`,
`OutboundClientHandshake::connect`, `finish`, the channel's reactor and every
other future named here must make progress when polled by a caller-supplied
executor with no runtime context — do not spawn internally, do not depend on a
Tokio (or any other) reactor being installed, and do not require a timer or I/O
driver the caller did not provide. A `tokio` *dependency* is fine; a runtime
*requirement* is not.

**How to read the code fences.** What a fence shows is required as shown: its
derives, `#[repr(..)]`, `#[non_exhaustive]`, `#[serde(..)]` and `#[builder(..)]`
attributes are part of the contract, and so is every impl written out. Not every
item named here is reached by a case; the contract is the surface, not the call
list. Checking is behavioural, so a trait shown in a derive list may be
hand-written and one shown as a separate `impl` line may be derived, provided
the observable behaviour matches. What a fence does **not** carry is attributes
on private fields, and the `#[error("…")]` message on each error enum: every
`Display` string that is checked is quoted verbatim in the observable-behaviour bullets
of its own section, and `thiserror` will not compile an error enum whose
messages are missing, so none of them can be lost silently. A cargo feature gate
appears in a fence's leading comment rather than as a `#[cfg]` attribute.

---

## 1. `tor-events` (new crate)

A new workspace crate named `tor-events`, published as library `tor_events`.

### 1.1 `tor_events::events`

A public module (`pub mod events;`).

```rust
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[non_exhaustive]
pub enum TorEvent {
    Empty,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, PartialOrd, Ord, Hash)]
#[repr(usize)]
#[non_exhaustive]
pub enum TorEventKind {
    Empty = 0,
}

impl TorEvent {
    pub fn kind(&self) -> TorEventKind;
}
```

Observable behaviour.

- `TorEvent::Empty.kind() == TorEventKind::Empty`.
- `TorEventKind::Empty as usize == 0`. The kind doubles as an index into the
  subscriber table, so the first discriminant must be zero.
- `TorEvent` serialises through serde as the bare variant name: serde_json
  renders `TorEvent::Empty` as `"Empty"` and parses it back to an equal value.
- `TorEventKind` is `Copy`, totally ordered and hashable: it can be a
  `BTreeSet` and a `HashSet` key, and two copies of one value compare `Equal`
  and deduplicate.
- Both types' `Debug` output is the bare variant name, `Empty`.

### 1.2 `tor_events` (crate root)

```rust
pub static BROADCAST_CAPACITY: usize;

pub struct EventReactor { /* private */ }

impl EventReactor {
    pub fn new() -> Option<Self>;
    pub fn receiver() -> Option<TorEventReceiver>;
    pub async fn run(self);
}

#[derive(Clone, Debug, Error)]
#[non_exhaustive]
pub enum ReceiverError {
    NoSubscriptions,
    ChannelClosed,
}

#[derive(Clone, Debug)]
pub struct TorEventReceiver { /* private */ }

impl TorEventReceiver {
    pub fn subscribe(&mut self, kind: TorEventKind);
    pub fn unsubscribe(&mut self, kind: TorEventKind);
}

impl futures::stream::Stream for TorEventReceiver {
    type Item = TorEvent;
}

impl Drop for TorEventReceiver { /* ... */ }

pub fn event_has_subscribers(kind: TorEventKind) -> bool;
pub fn broadcast(event: TorEvent);
```

`EventReactor::new()` and `EventReactor::receiver()` both return `Option`
because both depend on process-global state that may not have been
initialised:
`new()` yields `None` if a reactor already exists, and `receiver()` yields
`None` until one does. `receiver()` called before any reactor has been created
therefore returns `None` rather than panicking or installing one implicitly.

**`EventReactor` is driven on another thread, so it must be `Send`.** The
fixture moves the reactor into a `std::thread` and runs it there
(`EventReactor::new()` on the calling thread, `run()` on the spawned one), so a
reactor whose internals are `Rc`/`RefCell`-based — semantically conforming
against everything else in this section — fails to compile the whole event
half of the step. The channel-based design described here satisfies it
already; the requirement is stated so that no reader has to discover it from a
compile error.

A `TorEventReceiver` carries **its own** subscription set: the receivers share
one underlying broadcast channel, but each tracks which kinds it wants, and
`poll_next` drops events the individual receiver did not subscribe to. A
receiver with no subscriptions at all yields `None` immediately — it does not
block waiting for events it can never be delivered.

`Clone` copies that subscription set, so a clone starts out subscribed to
exactly what its source was. Note that `subscribe`/`unsubscribe`/`Drop`
maintain a process-wide per-kind subscriber count (what
`event_has_subscribers` reports) while `Clone` does not adjust it, so cloning a
subscribed receiver and dropping both handles decrements that count more often
than it was incremented. **The interaction between `Clone` and the global
count is out of scope**: it is an artefact of this upstream version, and a later
checkpoint may resolve it either way. What is checked is the per-receiver
behaviour described above, and `event_has_subscribers` against
subscribe/unsubscribe/drop on receivers that were never cloned.

Observable behaviour.

- `BROADCAST_CAPACITY == 512`.
- `EventReactor::new()` is a process singleton: the first call returns `Some`,
  every later call in the same process returns `None`.
- `EventReactor::receiver()` returns `Some` once a reactor exists, and may be
  called any number of times; each call yields an independent receiver.
- `run()` must be polled to completion by the caller for events to be
  forwarded. It never returns while the process-wide channels are open.
- A freshly obtained receiver is subscribed to nothing:
  `event_has_subscribers(k)` stays false, and its `Stream` yields `None`
  immediately rather than blocking.
- `subscribe` registers interest, making `event_has_subscribers(kind)` true. It
  is idempotent: N subscribes are undone by one `unsubscribe`.
- `unsubscribe` is idempotent, leaves the receiver usable, and — once no
  subscriptions remain — puts the stream back into the yields-`None` state.
- Dropping a subscribed receiver releases its subscriptions.
- Subscribers are counted, not flagged: with two subscribed receivers,
  dropping one leaves `event_has_subscribers` true; dropping both makes it
  false.
- `broadcast` delivers a copy of the event to every receiver subscribed to
  that kind, in the order broadcast, once the reactor is being polled. Events
  broadcast when nobody is subscribed are discarded without error and without
  creating a subscriber.
- `ReceiverError` renders as `no event subscriptions` and
  `internal event broadcast channel closed` respectively.

Determinism note for implementers: the subscriber counts are process-global.
The driver serialises its event cases behind its own mutex and drives one
reactor per test process; your implementation must not require a particular
test ordering beyond that.

## 2. `arti-config` (new crate)

A new workspace crate named `arti-config`, published as library `arti_config`.

```rust
pub fn load<P: AsRef<Path>>(
    files: &[(P, bool)],
    opts: Vec<String>,
) -> Result<config::Config, config::ConfigError>;

pub fn default_config_file() -> Option<PathBuf>;
```

`load` merges, in increasing order of precedence: the compiled-in defaults,
then each listed file in list order, then the command-line options. The boolean
beside each path says whether the file is required: a missing required file is
an error, a missing optional file is skipped. The `config` crate in question is
`config` 0.11.

`default_config_file()` returns the expansion of `${ARTI_CONFIG}/arti.toml` —
the file `arti` reads when the user names no configuration file at all. It
returns `None` exactly when that expansion fails: it does not fall back to a
hard-coded path and it does not panic. What it returns is already expanded, so
no `${...}` spelling survives in it, and its file name is exactly `arti.toml`.

Observable behaviour of the compiled-in defaults, read from the `config::Config`
that `load(&[], vec![])` returns:

| Key | Value |
| --- | --- |
| `proxy.socks_port` | `9150` |
| `logging.trace_filter` | `"debug"` |
| `logging.journald` | `false` |
| `storage.cache_dir` | `"${ARTI_CACHE}"` |
| `storage.state_dir` | `"${ARTI_LOCAL_DATA}"` |
| `download_schedule.retry_bootstrap.num_retries` | `128` |
| `download_schedule.retry_bootstrap.initial_delay` | `"1 sec"` |
| `download_schedule.retry_consensus.num_retries` | `3` |
| `download_schedule.retry_consensus.initial_delay` | `"1 sec"` |
| `download_schedule.retry_certs.num_retries` | `3` |
| `download_schedule.retry_certs.initial_delay` | `"1 sec"` |
| `download_schedule.retry_microdescs.num_retries` | `3` |
| `download_schedule.retry_microdescs.initial_delay` | `"1 sec"` |
| `download_schedule.retry_microdescs.parallelism` | `4` |
| `path_rules.ipv4_subnet_family_prefix` | `16` |
| `path_rules.ipv6_subnet_family_prefix` | `32` |
| `circuit_timing.max_dirtiness` | `"10 minutes"` |
| `circuit_timing.request_timeout` | `"60 sec"` |
| `circuit_timing.request_max_retries` | `32` |
| `circuit_timing.request_loyalty` | `"50 msec"` |
| `address_filter.allow_local_addrs` | `false` |

The `${ARTI_*}` spellings are the migration: the previous format's `${APP_*}`
names must no longer appear.

### 2.1 `arti_config::ArtiConfig` and its sections

```rust
#[derive(Debug, Clone, Deserialize, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ArtiConfig { /* private fields */ }

impl ArtiConfig {
    pub fn builder() -> ArtiConfigBuilder;
    pub fn logging(&self) -> &LoggingConfig;
    pub fn proxy(&self) -> &ProxyConfig;
    pub fn tor_client_config(&self) -> Result<TorClientConfig, ConfigBuildError>;
}

impl Default for ArtiConfig { /* ... */ }

#[derive(Debug, Clone, Builder, Deserialize, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[non_exhaustive]
#[builder(build_fn(error = "ConfigBuildError"))]
pub struct LoggingConfig {
    pub trace_filter: String,
    pub journald: bool,
}
impl LoggingConfig { pub fn builder() -> LoggingConfigBuilder; }

#[derive(Debug, Clone, Builder, Deserialize, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[builder(build_fn(error = "ConfigBuildError"))]
pub struct ProxyConfig { /* private */ }
impl ProxyConfig {
    pub fn builder() -> ProxyConfigBuilder;
    pub fn socks_port(&self) -> Option<u16>;
}

#[derive(Default, Clone)]
pub struct ArtiConfigBuilder { /* private */ }

impl ArtiConfigBuilder {
    pub fn build(&self) -> Result<ArtiConfig, ConfigBuildError>;
    pub fn proxy(&mut self) -> &mut ProxyConfigBuilder;
    pub fn logging(&mut self) -> &mut LoggingConfigBuilder;
    pub fn tor_network(&mut self) -> &mut dir::NetworkConfigBuilder;
    pub fn storage(&mut self) -> &mut StorageConfigBuilder;
    pub fn download_schedule(&mut self) -> &mut dir::DownloadScheduleConfigBuilder;
    pub fn override_net_params(&mut self) -> &mut HashMap<String, i32>;
    pub fn path_rules(&mut self) -> &mut circ::PathConfigBuilder;
    pub fn circuit_timing(&mut self) -> &mut circ::CircuitTimingBuilder;
    pub fn address_filter(&mut self) -> &mut ClientAddrConfigBuilder;
}

impl From<ArtiConfig> for ArtiConfigBuilder { /* ... */ }
```

**Obtaining an `ArtiConfig`.** There is no conversion trait to write. `load`
returns a `config::Config`, and the `config` crate turns that into an
`ArtiConfig` with its own **inherent** method — `config` 0.11's
`Config::try_into<'de, T: Deserialize<'de>>(self) -> Result<T, config::ConfigError>`
— so `let cfg: ArtiConfig = loaded.try_into()?;` needs nothing from you beyond
the `Deserialize` derive shown above, and that is the call the driver makes.
This release has **no** `impl TryFrom<config::Config> for ArtiConfig`; upstream
adds one several releases later. Writing one here is not an error, but it is
dead code: inherent methods resolve before trait methods, so the `try_into`
above would still bind to `config::Config`'s. What the driver does require is
that the deserialization succeed for the compiled-in defaults and fail for an
unknown section, which is `deny_unknown_fields`, not a conversion impl.

`ArtiConfig` is the one configuration type here that does **not** derive its
builder: `ArtiConfigBuilder` is written by hand, because it holds one *builder*
per section rather than one optional *value* per section, which is not the
shape `derive_builder` produces. So `ArtiConfig` carries no `Builder` derive,
and `ArtiConfigBuilder` carries no `Deserialize` derive — the section builders
it is made of are not themselves deserializable. Every other configuration
struct in this document does derive its builder, and those derives are written
out above.

The per-section builder accessors take `&mut self` and return `&mut` to the
section builder, so a caller configures a section in place
(`bld.proxy().socks_port(Some(9999));`). The section builders' own setters
return `&mut Self` and chain. `LoggingConfigBuilder::trace_filter` takes a
`String`; `ProxyConfigBuilder::socks_port` takes an `Option<u16>`;
`StorageConfigBuilder::{cache_dir, state_dir}` take anything `Into<CfgPath>`.

Observable behaviour.

- The compiled-in defaults deserialize into `ArtiConfig`, and the result equals
  `ArtiConfig::default()`, which equals `ArtiConfig::builder().build().unwrap()`.
- `cfg.proxy().socks_port() == Some(9150)`, `cfg.logging().trace_filter ==
  "debug"`, `cfg.logging().journald == false` for the defaults.
- An unrecognised configuration section (`wombat.quokka=3`) is accepted by
  `load` but rejected when deserialized into `ArtiConfig` — that is what
  `deny_unknown_fields` buys.
- Command-line overrides are visible through the accessors, and sections that
  were not overridden keep their defaults.
- `ArtiConfigBuilder::from(cfg).build()` reproduces `cfg` exactly, for any
  `cfg` a builder produced (round trip is the identity).
- `ArtiConfig::default().tor_client_config()` equals
  `TorClientConfig::default()`; the `storage` and `circuit_timing` sections set
  on an `ArtiConfigBuilder` reappear in the `TorClientConfig` it yields.

### 2.2 `arti_config::CmdLine`

```rust
#[derive(Debug, Clone)]
pub struct CmdLine { /* private */ }

impl Default for CmdLine { /* = new() */ }

impl CmdLine {
    pub fn new() -> Self;
    pub fn push_toml_line(&mut self, line: String);
}

impl config::Source for CmdLine { /* ... */ }
```

Each pushed line is a `key=value` TOML fragment with one convenience: a
right-hand side that is a bareword (alphanumerics and underscores only) is
quoted for the caller, so `a=3` and `bcd=hello` both arrive as strings. Anything
else is handed to the TOML parser unchanged, so `ef="gh i"` keeps its spaces and
`w=[1,2,3]` becomes an array. A dotted key nests: `hello.there.now=x` produces
`hello.there.now` in the resulting configuration.

Observable behaviour.

- `Source::collect()` on a `CmdLine` yields one entry per key, with the values
  described above; `config` coerces the string `"3"` to the integer 3 where an
  integer is wanted.
- Those values carry **no `origin`**. The driver compares them with `==`, which
  compares the origin field too, and its expected values are built without one,
  so a value that remembers where it came from is not equal to the value the
  case names. Converting a parsed `toml::Value` (`config::Value::try_from`, or
  `Value::new(None, ..)`) gives that; handing the line to a named `config`
  source does not -- such a source stamps its own origin onto every value it
  yields.
- A line that is neither a bareword assignment nor valid TOML — `a=b-c`, where
  the hyphen disqualifies the bareword path — makes `collect()` fail, and the
  error message contains the offending line verbatim so the user can find it.

## 3. `tor-config`

```rust
#[derive(Clone, Debug, Deserialize, Eq, PartialEq)]
#[serde(transparent)]
pub struct CfgPath(/* private */);

impl CfgPath {
    pub fn new(s: String) -> Self;
    pub fn from_path<P: AsRef<Path>>(path: P) -> Self;
    pub fn path(&self) -> Result<PathBuf, /* crate-private error type */>;
}

impl std::fmt::Display for CfgPath { /* ... */ }
```

`CfgPath::new` builds a *shell-expandable* path: `${...}` variables in it are
substituted by `path()`. `CfgPath::from_path` builds a *literal* path: `path()`
returns it byte for byte, dollar signs and all.

The recognised variable names are exactly `ARTI_CACHE`, `ARTI_CONFIG`,
`ARTI_SHARED_DATA`, `ARTI_LOCAL_DATA` and `USER_HOME`. The `APP_`-prefixed
spellings of the first four are gone.

Observable behaviour (the driver asserts recognition and message text, never
the expanded location, which depends on the invoking user's directories).

- `CfgPath::new("${NAME}").path()` for an unrecognised `NAME` is an `Err` whose
  `Display` is exactly `unrecognized variable NAME`.
- For each recognised name the result is not that error. (It may legitimately
  be a different error on a machine with no home directory.)
- `CfgPath::new("Hello/world").path()` is `Ok(PathBuf::from("Hello/world"))`
  and its `Display` is `Hello/world`.
- `CfgPath::from_path("${ARTI_CACHE}/literally").path()` is `Ok` of that exact
  string, and its `Display` is `"${ARTI_CACHE}/literally" [exactly]`.

### 3.1 `tor_config::ConfigBuildError`

```rust
#[derive(Debug, Clone, thiserror::Error)]
#[non_exhaustive]
pub enum ConfigBuildError {
    MissingField { field: String },
    Invalid { field: String, problem: String },
    Inconsistent { fields: Vec<String>, problem: String },
}

impl ConfigBuildError {
    pub fn within(&self, prefix: &str) -> Self;
}

impl From<derive_builder::UninitializedFieldError> for ConfigBuildError { /* ... */ }
```

This is the error type the builders of §2.1, §4 and §12 report: each of
those types carries `#[builder(build_fn(error = "ConfigBuildError"))]`, and
the two hand-written builders — `ArtiConfigBuilder` (§2.1) and
`TorClientConfigBuilder` (§4) — return it as well. It is **not** universal.
Exactly two builders in scope take a bare `Builder` derive with *no*
`build_fn(error = …)` attribute, so `derive_builder` generates each of them a
`…BuilderError` of its own and that is what its `build()` returns:
`StreamIsolationBuilder` (§10) and `AuthorityBuilder` (§4).

`within` prefixes each named field with `prefix` and a dot, so a
section builder's error can be re-reported by its parent with the section name
attached; nesting composes, outermost prefix leftmost.

Message formats, exactly:

- `MissingField` → `Field was not provided: {field}`
- `Invalid` → `Value of {field} was incorrect: {problem}`
- `Inconsistent` → `Fields {fields:?} are inconsistent: {problem}`
  (`{fields:?}` is the `Debug` form of the `Vec<String>`, e.g.
  `["mayo", "avocado"]`.)

## 4. `arti-client` configuration

```rust
// arti_client::config
pub use tor_config::ConfigBuildError;

pub mod circ {
    pub use tor_circmgr::{
        CircMgrConfig, CircMgrConfigBuilder, CircuitTiming, CircuitTimingBuilder,
        PathConfig, PathConfigBuilder,
    };
}

pub mod dir {
    pub use tor_dirmgr::{
        Authority, AuthorityBuilder, DirMgrConfig, DirMgrConfigBuilder, DownloadSchedule,
        DownloadScheduleConfig, DownloadScheduleConfigBuilder, FallbackDir,
        FallbackDirBuilder, NetworkConfig, NetworkConfigBuilder,
    };
}

#[derive(Debug, Clone, Builder, Deserialize, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[serde(deny_unknown_fields)]
pub struct ClientAddrConfig { /* private */ }
impl ClientAddrConfig { pub fn builder() -> ClientAddrConfigBuilder; }

#[derive(Debug, Clone, Builder, Deserialize, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[serde(deny_unknown_fields)]
pub struct StorageConfig { /* private */ }
impl StorageConfig { pub fn builder() -> StorageConfigBuilder; }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TorClientConfig { /* private */ }
impl TorClientConfig {
    pub fn builder() -> TorClientConfigBuilder;
}
impl Default for TorClientConfig { /* ... */ }

#[derive(Default, Clone)]
pub struct TorClientConfigBuilder { /* private */ }
impl TorClientConfigBuilder {
    pub fn build(&self) -> Result<TorClientConfig, ConfigBuildError>;
    pub fn from_directories<P: AsRef<Path>, Q: AsRef<Path>>(
        state_dir: P, cache_dir: Q,
    ) -> Self;
    pub fn tor_network(&mut self) -> &mut dir::NetworkConfigBuilder;
    pub fn storage(&mut self) -> &mut StorageConfigBuilder;
    pub fn download_schedule(&mut self) -> &mut dir::DownloadScheduleConfigBuilder;
    pub fn override_net_params(&mut self) -> &mut HashMap<String, i32>;
    pub fn path_rules(&mut self) -> &mut circ::PathConfigBuilder;
    pub fn circuit_timing(&mut self) -> &mut circ::CircuitTimingBuilder;
    pub fn address_filter(&mut self) -> &mut ClientAddrConfigBuilder;
}
impl From<TorClientConfig> for TorClientConfigBuilder { /* ... */ }
```

`TorClientConfig` is what an embedder builds directly; `ArtiConfig` is the file
format that produces one. `TorClientConfig` is additionally re-exported at the
crate root as `arti_client::TorClientConfig`; everything else in this section —
`circ`, `dir`, `ClientAddrConfig`, `StorageConfig`, `TorClientConfigBuilder` and
`ConfigBuildError` — is reached only through `arti_client::config`.

Observable behaviour.

- `TorClientConfig::default() == TorClientConfig::builder().build().unwrap()`,
  and `TorClientConfigBuilder::from(cfg).build()` reproduces `cfg`.
- `from_directories(state, cache)` stores both paths as *literal* paths — the
  same configuration `CfgPath::from_path` would produce, and a different
  configuration from the one `CfgPath::new` on the same strings produces.
- `ClientAddrConfig::default()` equals its empty builder's output and refuses
  local addresses; `allow_local_addrs(true)` yields a different value.
- `circ::CircuitTiming::default()` equals a `CircuitTimingBuilder` set to
  600 s dirtiness, 60 s request timeout, 32 retries and 50 ms loyalty.
- `circ::PathConfig::default()` equals a `PathConfigBuilder` set to IPv4 prefix
  16 and IPv6 prefix 32, and differs from one set to IPv4 prefix 20.
  (`PathConfigBuilder::default()` is the way to obtain one.)
- `dir::DownloadScheduleConfig::default()` equals a builder whose four
  schedules are `DownloadSchedule::new(128, 1s, 1)` for bootstrap and
  `DownloadSchedule::new(3, 1s, N)` for consensus, certs and microdescs with
  `N` of 1, 1 and 4. `pub fn DownloadSchedule::new(attempts: u32,
  initial_delay: Duration, parallelism: u8) -> Self`. The builder's four
  setters are `retry_bootstrap`, `retry_consensus`, `retry_certs` and
  `retry_microdescs`, each taking a `DownloadSchedule` **by value**, and a
  parallelism is read back with `DownloadSchedule::parallelism(&self) -> u8`.
  Step 1's contract, which remains binding at its own checkpoint, instead gives
  `DownloadScheduleConfig` a single flat parallelism —
  `DownloadScheduleConfigBuilder::microdesc_parallelism(&mut self, u8)` with the
  accessor `DownloadScheduleConfig::microdesc_parallelism(&self) -> usize`.
  Step 1's contract names those two method spellings and no configuration key;
  the matching key `download_schedule.microdesc_parallelism` came from step 1's
  release, which carried it in `arti_defaults.toml`. **By
  this tag the name `microdesc_parallelism` is gone from `tor-dirmgr`
  entirely.** Parallelism moved *inside* `DownloadSchedule`, one per schedule,
  and the configuration key is now
  `download_schedule.retry_microdescs.parallelism` (§2's defaults table). Where
  the two disagree, this section supersedes step 1: a `DownloadScheduleConfig`
  that still carries `microdesc_parallelism`, or a
  `DownloadScheduleConfigBuilder` without the four `retry_*` setters, is the
  wrong answer at this checkpoint. The clamping step 1 describes survives, but
  it moved with the field: `DownloadSchedule` stores `num_retries` and
  `parallelism` as `NonZeroU32` and `NonZeroU8`, and `new()` turns a `0`
  argument for either into `1`. The cases in scope construct their schedules with
  non-zero arguments only, so no case feeds `0` here — the clamp is a stated
  requirement with no assertion behind it in this step, not a rule the tests
  settle.
- `dir::Authority::builder().name(..).v3ident(..).build()` succeeds.
  `Authority` is the one type in this section that derives `Builder` **without**
  a `build_fn(error = …)` attribute, so `AuthorityBuilder` is entirely
  `derive_builder`-generated and its `build()` returns
  `Result<Authority, AuthorityBuilderError>`, not `ConfigBuildError` (§3.1).
  Its `name` field carries `#[builder(setter(into))]`, so the generated setter
  takes `impl Into<String>` and a bare `&str` literal is accepted; `v3ident`
  takes an `RsaIdentity` **by value**, and `RsaIdentity` implements
  `From<[u8; 20]>` (§6.3), so a 20-byte array literal converts with `.into()`.
  `AuthorityBuilder::new()` still exists and still means “no fields set”.
  Unlike the two supersessions around it, this one resolves to nothing: the
  builder was already `derive_builder`-generated at the first checkpoint, with
  the same `setter(into)` on `name` and the same generated error type, and this
  release changes `Authority` only by adding `Eq` and `PartialEq` to its derive
  list. Step 1's contract lists the builder as `new`, `name(&str)`,
  `v3ident(RsaIdentity)` and `build()`, and all four still hold here. What step
  1 does not state is what `build()` returns; this section supplies it.
- `dir::NetworkConfig::builder().authorities(vec![auth]).build()` fails with
  `ConfigBuildError::Inconsistent` naming `["authorities", "fallbacks"]` —
  replacing the authority list without also replacing the fallback list is not
  a usable network configuration.
- `dir::FallbackDir::builder()` returns a `FallbackDirBuilder` whose setters are
  `rsa_identity(RsaIdentity)`, `ed_identity(Ed25519Identity)`,
  `orports(Vec<SocketAddr>)` — which replaces the whole ORPort list — and
  `orport(SocketAddr)`, which appends one address to it. Each takes `&mut self`
  and returns `&mut Self`. `build()` with nothing set fails with `MissingField`
  naming `rsa_identity`; setting the ORPort list to an empty `Vec` fails with
  `Invalid` naming the field `orport` and the problem `list was empty`.

`FallbackDir` is also the type the driver hands to `UnverifiedChannel::check`
(§6.2) as the relay it expects the fixture certificate chain to authenticate,
so the `tor_linkspec::ChanTarget` impl this type has carried since the first
checkpoint has to survive its rewrite into a `derive_builder` builder:

```rust
// tor_netdir::fallback, re-exported as arti_client::config::dir::FallbackDir
impl tor_linkspec::ChanTarget for FallbackDir {
    fn addrs(&self) -> &[std::net::SocketAddr];
    fn ed_identity(&self) -> &tor_llcrypto::pk::ed25519::Ed25519Identity;
    fn rsa_identity(&self) -> &tor_llcrypto::pk::rsa::RsaIdentity;
}
```

`addrs()` is the ORPort list the builder was given, in the order it was given;
the two identity accessors return the identities it was given. This impl is not
decoration. The driver's shared fixture module builds a `FallbackDir` and passes
it to `check()`, and that module is compiled into **every one** of this step's
test targets — so a `FallbackDir` that is not a `ChanTarget` fails every group,
not just the ones whose sections name it. `FallbackDir` was already a
`ChanTarget` at the first checkpoint, and this release leaves that trait impl
untouched. The builder was already `derive_builder`-generated there too, with
the same hand-written appending `orport` and the same `validate` hook; what
changes is the error type. The first checkpoint's
`#[builder(build_fn(validate = ...))]` gains `error = "ConfigBuildError"`, so
`build()` now reports a `ConfigBuildError` instead of `derive_builder`'s own
generated error, and the empty-ORPort rejection carries the
`Invalid { field: "orport", problem: "list was empty" }` shape given above in
place of a bare string. Step 1's contract, which remains binding at its own
checkpoint, instead writes `build()` as returning `tor_netdir::Result` and
states that no plural `orports` setter exists; where the two disagree about
`FallbackDirBuilder`, this section governs.

## 5. `tor-proto` — stream parameters

```rust
// tor_proto::stream
#[derive(Clone, Debug, Default)]
pub struct StreamParameters { /* private */ }

impl StreamParameters {
    pub fn new() -> Self;
    pub fn ip_version(&mut self, preference: IpVersionPreference) -> &mut Self;
    pub fn optimistic(&mut self, optimistic: bool) -> &mut Self;
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum IpVersionPreference {
    Ipv4Only, Ipv4Preferred, Ipv6Preferred, Ipv6Only,
}

// Hand-written, not derived: `StreamParameters`' derived `Default` resolves
// through this one, and the wanted variant is not the first one.
impl Default for IpVersionPreference { /* -> Ipv4Preferred */ }
```

The fields are private and have no accessors, so the configured state is read
back through the derived `Debug` representation. That representation must name
each field and its value: the driver requires the string `StreamParameters` at
the start, the variant name of the IP preference (e.g. `Ipv6Only`), and
`optimistic: false` / `optimistic: true`. **These substrings — like every
rendering this document quotes in a fence or in a bullet — are checked as
written, character for character.** They are the renderings of *your* types, so
they exist only if you produce them; where a bullet quotes a string, copy it
rather than paraphrasing it.

Observable behaviour.

- `new()` and `default()` produce the same value; the default prefers IPv4
  (`Ipv4Preferred`) and is *not* optimistic. `new()` is `Self::default()`, and
  that default reaches `Ipv4Preferred` through `IpVersionPreference`'s own
  `Default` impl, above — a `StreamParameters` that derives `Default` without
  it will not compile.
- Both setters record what they were given, chain (`&mut Self`), and can be set
  back: `optimistic(true)` then `optimistic(false)` returns to the default.

**The non-optimistic default is checked; directory-stream optimism is not.**
These are two different things and it is worth being explicit, because reading
them as one produces an implementation that fails. `StreamParameters::default()`
is *not* optimistic, and a case asserts exactly that. Separately, the
directory-stream constructor in §5.2 always opens optimistically — it takes no
parameters and so cannot be told otherwise.

Its *shape* is checked: the driver compiles a call of exactly the signature in
§5.2, so the method name, the `Arc<Self>` receiver, the absence of a parameter
object and the return type all count. Its *optimism* is out of scope, because
observing that would need a built circuit, which the check does not have, and the
accessor the constructor consults is `pub(crate)`. An implementation that
reaches the directory default by making the parameter object's default
optimistic will fail the case above.

### 5.1 `ClientCirc::begin_stream`

```rust
// tor_proto::circuit::ClientCirc
pub async fn begin_stream(
    self: Arc<Self>,
    target: &str,
    port: u16,
    parameters: Option<StreamParameters>,
) -> tor_proto::Result<tor_proto::stream::DataStream>;
```

The fourth parameter replaces the bare IP-version flag of the previous
checkpoint. `None` means the defaults. This is checked at the type level (the
driver coerces a call of exactly this shape); no live relay is involved.

### 5.2 `ClientCirc::begin_dir_stream`

```rust
// tor_proto::circuit::ClientCirc
pub async fn begin_dir_stream(
    self: Arc<Self>,
) -> tor_proto::Result<tor_proto::stream::DataStream>;
```

The directory entry point takes **no** parameter object: a caller can neither
ask for a non-optimistic directory stream nor ask for an optimistic one,
because the constructor always opens optimistically on its own. It yields the
same `DataStream` type as §5.1. Like §5.1 this is checked at the type level —
the driver compiles a call of exactly this shape, so the name, the `Arc<Self>`
receiver, the empty parameter list and the return type are all checked; no live
relay is involved.

## 6. `tor-proto` — the reactor refactor

```rust
// tor_proto::channel
pub const CHANNEL_BUFFER_SIZE: usize = 128;

pub struct Channel { /* private */ }        // Clone + Send + Sync + Debug + 'static
impl Channel {
    pub fn unique_id(&self) -> UniqId;
    pub fn is_closing(&self) -> bool;
    pub fn poll_ready(&mut self, cx: &mut Context<'_>) -> tor_proto::Result<bool>;
    pub fn terminate(&self);
    pub fn close_circuit(&self, circid: tor_cell::chancell::CircId) -> tor_proto::Result<()>;
}
impl futures::sink::Sink<tor_cell::chancell::ChanCell> for Channel {
    type Error = tor_proto::Error;
}
pub use ...::UniqId;   // tor_proto::channel::UniqId

// tor_proto::circuit
pub const CIRCUIT_BUFFER_SIZE: usize = 128;

pub struct ClientCirc { /* private */ }     // Clone + Send + Sync + Debug + 'static
impl ClientCirc {
    pub fn terminate(&self);
    pub fn is_closing(&self) -> bool;
    pub fn unique_id(&self) -> UniqId;      // tor_proto::circuit::UniqId
}

#[derive(Clone, Debug)]
pub struct CircParameters { /* private */ }
impl CircParameters {
    pub fn set_initial_send_window(&mut self, v: u16) -> tor_proto::Result<()>;
    pub fn initial_send_window(&self) -> u16;
    pub fn set_extend_by_ed25519_id(&mut self, v: bool);
    pub fn extend_by_ed25519_id(&self) -> bool;
}
impl Default for CircParameters { /* ... */ }
```

Removing the locks from the data path shows up at the public boundary as a
shape change, and that is what is checked: both handles became cheap clonable
senders into a reactor task, so both are `Clone + Send + Sync`; the shutdown
entry points post a control message instead of taking a lock, so neither
`terminate` is `async` any more; the channel is itself a `Sink` of cells that
the reactor drains; readiness is polled against that queue with `poll_ready`,
which needs `&mut` access; and the bounded queue lengths had to be named.

Observable behaviour.

- `unique_id()` returns a `UniqId` that is stable for the lifetime of the
  handle and distinct between two independently created channels (and likewise
  for circuits). It is an opaque local debugging identity with no protocol
  meaning — it is not the channel's identity key and it is not a `CircId` — so
  the only things asserted are its stability, its distinctness, and that clones
  of one handle agree on it.
- `is_closing()` is `false` for a live handle. `terminate()` is the *request*:
  it takes a shared handle, returns nothing and cannot be awaited, so what it
  does is post to the reactor rather than tear anything down itself. Once the
  reactor has stopped, `is_closing()` is `true`, and **every clone of the
  handle agrees** — they share one flag, not one lock. The driver runs the
  reactor to completion between the two reads, so marking the channel closed
  earlier than that is also acceptable; what is checked is that shutdown is
  reachable without `async` and is visible through every clone.
- `poll_ready()` reports whether the channel's bounded queue can accept another
  cell: `Ok(true)` means a send will not block, `Ok(false)` means the reactor
  has not drained enough yet and the caller was registered for wakeup. It is
  the `Sink` readiness check spelled as an inherent method, which is why it
  needs `&mut` even though the handle is otherwise shared by cloning.
  The queue is really bounded and its length is really `CHANNEL_BUFFER_SIZE`:
  with no reactor draining it, at least `CHANNEL_BUFFER_SIZE` cells can be
  pushed in through the `Sink` and then `poll_ready()` has to start answering
  `Ok(false)`. A little more than `CHANNEL_BUFFER_SIZE` is fine — the slack
  depends on how many senders exist — but an unbounded queue is not.
- `close_circuit()` asks the channel to tear down one circuit by id, leaving
  the channel and its other circuits alive. It returns `Ok(())` for a circuit
  the channel does not know about — closing something already gone is the
  desired end state, not an error.
- `CHANNEL_BUFFER_SIZE == 128` and `CIRCUIT_BUFFER_SIZE == 128`.
- Every signature above coerces to the plain `fn` pointer type written here
  (this is how the driver checks that nothing is `async` and nothing returns a
  guard). For `Channel` that coercion is the *floor*, not the whole check:
  §6.2 says how the driver gets hold of a real channel, and every `Channel`
  behaviour in this list is asserted on one. `ClientCirc` stays at the type
  level — building a circuit needs a relay that can answer a CREATE cell, and
  the check does not have one — so its three methods are checked on their shapes
  alone, and this document does not pretend otherwise.
- `CircParameters::default()` has an initial send window of 1000 and extends by
  ed25519 identity. `set_initial_send_window(500)` is `Ok` and takes effect;
  `set_initial_send_window(1001)` is `Err` and leaves the previous value in
  place. `set_extend_by_ed25519_id(false)` takes effect.

### 6.1 `tor_proto::time_since_last_incoming_traffic`

```rust
// behind feature "traffic-timestamp"
pub fn time_since_last_incoming_traffic() -> std::time::Duration;
```

The reactor records the last time incoming traffic was accepted, without a lock
on the data path. Before any traffic has been accepted in the process, the
answer is exactly `Duration::from_secs(0)` — not an approximation of zero, and
not an error. A handshake that fails before any cell is accepted must leave it
at exactly zero.

### 6.2 Reaching a live `Channel`

```rust
// tor_proto::channel
pub struct UnverifiedChannel<T> { /* private */ }
impl<T: AsyncRead + AsyncWrite + Send + Unpin + 'static> UnverifiedChannel<T> {
    pub fn check<U: tor_linkspec::ChanTarget + ?Sized>(
        self,
        peer: &U,
        peer_cert: &[u8],
        now: Option<std::time::SystemTime>,
    ) -> tor_proto::Result<VerifiedChannel<T>>;
}

pub struct VerifiedChannel<T> { /* private */ }
impl<T: AsyncRead + AsyncWrite + Send + Unpin + 'static> VerifiedChannel<T> {
    // Returns the open channel and its reactor, in that order. The reactor's
    // own type is deliberately not named here: it may live in a private
    // module, as upstream's does. All that is required of it is a
    // `pub async fn run(self) -> tor_proto::Result<()>`.
    pub async fn finish(self) -> tor_proto::Result<(Channel, Reactor)>;
}
```

`connect()` (§7) stops at an `UnverifiedChannel`; there is no `Channel` yet.
Two further steps produce one, and both are checked here, because the driver
has to call them to reach the behaviour §6 describes. Both types date from the
first checkpoint, but no earlier step checked either method, so the signatures
above are the first binding statement of their shape: a carried-forward
`check` or `finish` that differs from them will not compile against this
step's driver.

- `check()` performs the link authentication: the IDENTITY_V_SIGNING and
  SIGNING_V_TLS_CERT ed25519 certificates, the x.509 RSA identity
  certificate and the RSA→Ed25519 crosscert from the peer's CERTS cell. It
  binds that chain to the SHA-256 of `peer_cert`, and it refuses the peer
  unless both identities match the `ChanTarget` it was handed. `now` is
  passed explicitly so certificate expiry is judged against a caller-chosen
  instant and never against the system clock; it is always `Some` in this
  step's cases, so what `None` means is left to you (it is not exercised).
  The refusal direction is a real obligation of the contract even though this
  step's own cases reach only the accept direction: the driver's scripted
  relay always presents the chain that authenticates the `ChanTarget` it names,
  so a `check()` that accepted *any* chain would still pass everything here.
  Implement the check; do not infer from the tests that it is optional.
- `finish()` sends the client's NETINFO cell and hands back the open channel
  together with its reactor. Running that reactor to completion must stop the
  channel (see `is_closing` in §6). A reactor that stops because `terminate()`
  was called returns `Ok(())`: a shutdown that was asked for is not a failure,
  and the case in scope unwraps that result rather than tolerating an `Err`. The
  `Err` side of `run()` is for a genuine protocol or I/O failure — and for
  `run()` being entered on a channel that was already closed.

`SPEC_NOTES.md` section 9 states all four certificate formats and the checks
tor-spec requires of them.

The driver supplies its own scripted relay for this, with a certificate chain
whose signatures genuinely verify, and passes a `ChanTarget` naming exactly
the identities that chain certifies. Nothing is stubbed: an implementation
that accepts an unauthenticated peer, or that fails to authenticate a correct
chain, never obtains a channel, and every §6 behaviour that needs one fails
with it.

### 6.3 Cells and identities the driver builds

Reaching a live `Channel` and then filling its queue means the driver
constructs cells and identities itself, so these constructors are checked
alongside §6 even though the capability they belong to is older. They are
listed here because a carried-forward spelling that differs from them will not
compile against this step's driver.

```rust
// tor_cell::chancell
#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub struct CircId(/* private */);
impl From<u32> for CircId { /* ... */ }
impl From<CircId> for u32 { /* ... */ }

#[derive(Debug)]
pub struct ChanCell { /* private */ }
impl ChanCell {
    pub fn new(circid: CircId, msg: msg::ChanMsg) -> Self;
}

// tor_cell::chancell::msg
#[derive(Clone, Debug)]
pub struct Create2 { /* private */ }
impl Create2 {
    pub fn new<B: Into<Vec<u8>>>(handshake_type: u16, handshake: B) -> Self;
}
impl<B: Body> From<B> for ChanMsg { /* so `create2.into()` is a ChanMsg */ }

// tor_llcrypto::pk::rsa
impl RsaIdentity {
    pub fn from_bytes(bytes: &[u8]) -> Option<Self>;   // None unless 20 bytes
}
impl From<[u8; 20]> for RsaIdentity { /* infallible: length is in the type */ }
```

- A `CircId` is made from a `u32`, not a `u16` — the wire form is two or four
  bytes but the in-memory identifier is one width. `CircId::from(9999u32)` is
  how the driver names a circuit the channel has never heard of.
- `ChanCell::new` pairs a circuit id with a message, in that order, and any
  cell body converts into a `ChanMsg` with `.into()`; the driver uses `Create2`
  because it is a body with a payload it can vary.
- `RsaIdentity::from_bytes` is how the driver turns the fixture relay's
  20-byte identity digest into the `rsa_identity` its `FallbackDir` (§4)
  needs. It answers `None` for any other length.
- `RsaIdentity` additionally has an **infallible** `From<[u8; 20]>`: an array
  of exactly twenty bytes carries its length in its type, so there is nothing
  to reject. That is the conversion §4's `AuthorityBuilder::v3ident` case
  reaches through `.into()`. The two forms are not interchangeable —
  `from_bytes` takes a slice and returns `Option`, `From` takes an array and
  returns the identity.

## 7. `tor-proto` — the channel handshake (preserved)

> **`(preserved)` means carried behaviour.** The sections carrying this marker —
> §7, §8, §10, §11, §12 — are gate groups. They describe surfaces an earlier
> checkpoint delivered, re-checked here against the cumulative workspace. You
> are not asked to build them again; a failure there means the step is not
> satisfied, because a single failing gate group is enough. The new work this
> step requires is in the sixteen requirement groups, which §13 tabulates.

```rust
// tor_proto::channel
pub struct ChannelBuilder { /* private */ }
impl ChannelBuilder {
    pub fn new() -> Self;
    pub fn launch<T: AsyncRead + AsyncWrite + Send + Unpin + 'static>(
        self, tls: T,
    ) -> OutboundClientHandshake<T>;
}

pub struct OutboundClientHandshake<T> { /* private */ }
impl<T> OutboundClientHandshake<T> {
    pub async fn connect(self) -> tor_proto::Result<UnverifiedChannel<T>>;
}
```

The driver runs this against a scripted, in-memory duplex: a finite byte string
of relay cells in, every client byte captured on the way out. No socket, no
clock. The cell framing is stated in `SPEC_NOTES.md` sections 1–4, and the
certificates the CERTS cell carries in section 9.

Observable behaviour.

- The client's first write is exactly the VERSIONS cell for link protocol 4,
  on the pre-negotiation two-byte circuit id:
  `00 00 07 00 02 00 04`. It writes nothing else during negotiation.
- A peer that says nothing, or that opens with something other than a VERSIONS
  cell, is refused; the error's `Display` is
  `channel protocol violation: Doesn't seem to be a tor relay`.
- A peer that offers only link protocols 1, 2 and 3 is refused with
  `channel protocol violation: No shared link protocols`.
- A peer that negotiates version 4 and then sends NETINFO without CERTS is
  refused with `channel protocol violation: Missing certs cell`.
- A peer that sends CERTS and then closes is refused with
  `channel protocol violation: Missing netinfo or closed stream`.
- A second CERTS cell is refused with
  `channel protocol violation: Duplicate certs cell`.
- CERTS followed by NETINFO completes negotiation and yields an unverified
  channel. PADDING and VPADDING cells interleaved anywhere in that exchange are
  ignored, not treated as violations.

## 8. `tor-dirclient` — router descriptor requests (preserved)

```rust
// tor_dirclient::request, behind feature "routerdesc"
#[derive(Debug, Clone)]
pub struct RouterDescRequest { /* private */ }

impl RouterDescRequest {
    pub fn all() -> Self;
    pub fn new() -> Self;
    pub fn push(&mut self, d: RdDigest);          // RdDigest = [u8; 20]
    pub fn digests(&self) -> impl Iterator<Item = &RdDigest>;
}
impl Default for RouterDescRequest { /* same as new() */ }
impl FromIterator<RdDigest> for RouterDescRequest { /* ... */ }

pub trait Requestable {
    fn make_request(&self) -> tor_dirclient::Result<http::Request<()>>;
    fn partial_docs_ok(&self) -> bool;
    fn max_response_len(&self) -> usize;
}
impl Requestable for RouterDescRequest { /* ... */ }
```

URL shapes are in `SPEC_NOTES.md` section 6.

Observable behaviour.

- `all()` produces `GET /tor/server/all.z`, accepts a partial answer, and caps
  the response at `64 * 1024 * 1024` bytes. Pushing digests onto an `all()`
  request changes nothing: `digests()` stays empty and the URL stays
  `/tor/server/all.z`.
- A request holding digests produces
  `GET /tor/server/d/<hex>+<hex>+....z`, with the digests lowercase hex,
  **sorted ascending**, joined by `+`, regardless of the order they were
  pushed.
- `partial_docs_ok()` is true for an `all()` request and for a digest request
  holding more than one digest, and false for everything else — including a
  request for exactly one digest **and an empty request**, neither of which
  has anything partial to salvage. The rule is `digests > 1 || all`; it is
  *not* `digests != 1`, which would answer `true` for the empty request that
  `new()` and `default()` produce.
- `max_response_len()` is `digests * 8 * 1024` for a digest request, so 8 KiB
  for one, 16 KiB for two, and `0` for an empty request.
- `new()` and `default()` agree and hold no digests.
- Collecting an iterator of digests preserves arrival order in `digests()`;
  only the URL is sorted.
- The request carries an `Accept-Encoding` header beginning
  `deflate, identity`.

## 9. `tor-dirclient` — `DirResponse`

```rust
// tor_dirclient (re-exported from a private module)
#[derive(Debug)]
pub struct DirResponse { /* private */ }

impl DirResponse {
    pub fn from_body(body: impl AsRef<[u8]>) -> Self;
    pub fn status_code(&self) -> u16;
    pub fn is_partial(&self) -> bool;
    pub fn error(&self) -> Option<&tor_dirclient::Error>;
    pub fn output(&self) -> &[u8];
    pub fn into_output(self) -> Vec<u8>;
    pub fn source(&self) -> Option<&tor_dirclient::SourceInfo>;
}
```

Observable behaviour.

- `DirResponse` is `Debug` but **not** `Clone`: it can carry a
  `tor_dirclient::Error`, and that error wraps `std::io::Error` among others,
  so the whole response is a move-only value. No case in scope clones one.
- `from_body(b)` is a complete success: status 200, not partial, no error, no
  source information, and `output()` is exactly `b`.
- `into_output()` yields the same bytes `output()` borrowed, for any byte
  content including non-UTF-8 and empty.
- `impl AsRef<[u8]>` accepts at least `&str`, `String`, `&[u8]` and `Vec<u8>`.

## 10. `tor-circmgr` — circuit isolation (preserved)

```rust
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct IsolationToken(/* private */);
impl IsolationToken {
    pub fn new() -> Self;
    pub fn no_isolation() -> Self;
}

// No `#[builder(build_fn(error = ...))]` here — deliberately; see below.
#[derive(Copy, Clone, Eq, Debug, PartialEq, Builder)]
pub struct StreamIsolation { /* private */ }
impl StreamIsolation {
    pub fn no_isolation() -> Self;
    pub fn builder() -> StreamIsolationBuilder;
}
impl StreamIsolationBuilder {
    pub fn new() -> Self;
    pub fn stream_token(&mut self, t: IsolationToken) -> &mut Self;
    pub fn owner_token(&mut self, t: IsolationToken) -> &mut Self;
    pub fn build(&self) -> Result<StreamIsolation, StreamIsolationBuilderError>;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TargetPort { /* private */ }
impl TargetPort {
    pub fn ipv4(port: u16) -> TargetPort;
    pub fn ipv6(port: u16) -> TargetPort;
}
```

Observable behaviour.

- Tokens from `IsolationToken::new()` are pairwise distinct, and distinct from
  `no_isolation()`. A token equals itself and its copies. No particular numeric
  value is checked.
- `no_isolation()` is stable: two calls compare equal.
- `StreamIsolation::no_isolation()`, `StreamIsolation::builder().build()` and
  `StreamIsolationBuilder::new().build()` all produce the same value.
- Two `StreamIsolation`s are equal exactly when both their stream token and
  their owner token agree; changing either makes them unequal.
- `TargetPort::ipv4(p)` compares structurally: equal for equal ports, unequal
  for different ports.
- `StreamIsolation` takes a bare `Builder` derive with no
  `build_fn(error = …)` attribute, so `StreamIsolationBuilder::build()` reports
  `derive_builder`'s own generated `StreamIsolationBuilderError` — **not**
  `ConfigBuildError` (§3.1). That generated type lives in `tor-circmgr`'s
  private `usage` module and is not re-exported, so no external crate can name
  it; it is `Debug + Display + std::error::Error`, which is all the cases in scope
  need in order to `expect` on it. Do not “correct” this to
  `ConfigBuildError`: that would change a public signature this section
  preserves.

## 11. `tor-persist` — persistent state (preserved)

```rust
pub trait StateMgr: Clone {
    fn load<D: DeserializeOwned>(&self, key: &str) -> Result<Option<D>>;
    fn store<S: Serialize>(&self, key: &str, val: &S) -> Result<()>;
    fn can_store(&self) -> bool;
    fn try_lock(&self) -> Result<LockStatus>;
    fn create_handle<T>(self, key: impl Into<String>) -> DynStorageHandle<T>
    where Self: Send + Sync + Sized + 'static,
          T: Serialize + DeserializeOwned + 'static;
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum LockStatus { NoLock, AlreadyHeld, NewlyAcquired }
impl LockStatus { pub fn held(&self) -> bool; }

#[derive(Clone, Debug)]
pub struct FsStateMgr { /* private */ }
impl FsStateMgr { pub fn from_path<P: AsRef<Path>>(path: P) -> Result<Self>; }
impl StateMgr for FsStateMgr { /* ... */ }

pub trait StorageHandle<T: Serialize + DeserializeOwned> {
    fn load(&self) -> Result<Option<T>>;
    fn store(&self, val: &T) -> Result<()>;
    fn can_store(&self) -> bool;
}
pub type DynStorageHandle<T> = Arc<dyn StorageHandle<T> + Send + Sync + 'static>;

#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error { /* includes */ NoLock, /* ... */ }
```

Observable behaviour.

- A fresh `FsStateMgr` over an empty directory does not hold the lock:
  `can_store()` is false, and `store` fails with an error whose `Display` is
  exactly `Storage not locked`, writing nothing.
- `try_lock()` returns `LockStatus::NewlyAcquired` the first time and
  `LockStatus::AlreadyHeld` while the lock is still held; both report
  `held() == true`. `LockStatus::NoLock.held()` is false.
- After the lock is held, `store(key, &v)` then `load(key)` returns
  `Some(v)` for any serde-serialisable `v`; `load` of an absent key is `None`,
  not an error.
- `create_handle::<T>(key)` returns a handle addressing the same key as the
  manager: a value stored through the handle is visible to `load(key)` on the
  manager, and the handle's `can_store()` tracks the manager's lock.

## 12. `tor-guardmgr` — guard request vocabulary (preserved)

```rust
#[derive(Clone, Debug, Eq, PartialEq)]
#[non_exhaustive]
pub enum GuardUsageKind { Data, OneHopDirectory }
impl Default for GuardUsageKind { /* Data */ }

#[derive(Clone, Debug, Default, Builder)]
#[builder(build_fn(error = "tor_config::ConfigBuildError"))]
pub struct GuardUsage { /* private: kind, restriction */ }

impl GuardUsageBuilder {
    pub fn new() -> Self;
    pub fn kind(&mut self, k: GuardUsageKind) -> &mut Self;
    pub fn restriction(&mut self, r: GuardRestriction) -> &mut Self;  // strip_option
    pub fn build(&self) -> Result<GuardUsage, ConfigBuildError>;
}

#[derive(Clone, Debug)]
#[non_exhaustive]
pub enum GuardRestriction {
    AvoidId(tor_llcrypto::pk::ed25519::Ed25519Identity),
}

#[derive(Copy, Clone, Debug)]
#[non_exhaustive]
pub enum GuardStatus { Success, Failure, Indeterminate, AttemptAbandoned }

#[derive(Debug, Clone)]
#[non_exhaustive]
pub enum GuardFilter {
    Unfiltered,
}
impl Default for GuardFilter { /* -> Unfiltered */ }
impl GuardFilter { pub fn unfiltered() -> Self; }

#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum GuardMgrError {
    State(#[from] tor_persist::Error),
    Spawn(#[source] Arc<futures::task::SpawnError>),
}
impl From<futures::task::SpawnError> for GuardMgrError { /* ... */ }
```

`GuardUsage` has no accessors and no `PartialEq`, so what was configured is read
back through its derived `Debug`, which must name its fields: `kind: Data`,
`restriction: None`, and the restriction's variant and payload when set.

Also used: `tor_llcrypto::pk::ed25519::Ed25519Identity::new([u8; 32]) -> Self`
and `::from_bytes(&[u8]) -> Option<Self>` (`None` for any length but 32).

`tor_llcrypto::pk::ed25519` must also keep re-exporting the `ed25519-dalek`
key types, complete and under these names:

```rust
// tor_llcrypto::pk::ed25519
pub use ed25519_dalek::{ExpandedSecretKey, Keypair, PublicKey, SecretKey, Signature};
```

The driver builds the fixture relay's certificate chain for §6.2 out of them:
`SecretKey::from_bytes`, `PublicKey::from(&SecretKey)`, `PublicKey::as_bytes`,
and `ExpandedSecretKey::from(&SecretKey).sign(msg, &pk).to_bytes()`.
That work happens in the shared fixture module every one of this step's test
targets compiles in, so dropping any of the five names — `ExpandedSecretKey`
in particular, which nothing else in this document mentions — breaks every
group and not only this one. The list is unchanged from the first checkpoint.

Observable behaviour.

- `GuardUsageKind::default() == GuardUsageKind::Data`, distinct from
  `OneHopDirectory`.
- `GuardUsage::default()` debugs as kind `Data` with no restriction, and an
  untouched `GuardUsageBuilder` builds an equal value.
- A builder records the kind it was given, and records a restriction including
  the identity it names — the identity is carried, not discarded.
- `GuardStatus`'s four variants debug as their own names. As at step 2 it is
  `Copy` but **not** `PartialEq`, so the derived `Debug` is the only public way
  to tell two statuses apart — which is why the case in scope compares `Debug`
  strings.
- `GuardFilter` is still the `#[non_exhaustive]` enum step 2 introduced, with
  exactly one non-test variant, and it is still not `PartialEq`.
  `GuardFilter::unfiltered()` and `GuardFilter::default()` are both
  `Unfiltered`, which the case in scope checks through `Debug`.
- `GuardMgrError::from(tor_persist::Error::NoLock)` displays as
  `Problem accessing persistent state`; `GuardMgrError::from(SpawnError)`
  displays as `Unable to spawn task`.

## 13. Group-to-item map

Every group below is all-or-nothing: every case in it must pass.

The five `inherited` groups are **gate** groups: they add no requirement of
their own, but a single failure among them means the step is not satisfied,
whatever else passed. The other sixteen groups are **requirement** groups:
they carry the capabilities this step introduces and a thinner spread of
breadth around the milestone.

The group names below are those in `tests/assets/groups.txt`, which is the file
the checker reads.

| Group | Kind | Items (section above) |
|---|---|---|
| `events_kinds` | requirement | 1.1 |
| `events_subscription` | requirement | 1.2 (`receiver`, `subscribe`, `unsubscribe`, `Drop`, `event_has_subscribers`) |
| `events_delivery` | requirement | 1.2 (`broadcast`, `Stream for TorEventReceiver`) |
| `events_reactor` | requirement | 1.2 (`BROADCAST_CAPACITY`, `new`, `receiver`, `run`, `ReceiverError`, broadcast gating) |
| `cfg_defaults` | requirement | 2 (`load`, `default_config_file`, and the defaults table) |
| `cfg_arti_options` | requirement | 2.1 (`ArtiConfig`, `logging`, `proxy`, `tor_client_config`, `deny_unknown_fields`) |
| `cfg_builders` | requirement | 2.1 (`builder`, `ArtiConfigBuilder`, `From<ArtiConfig>`), 3 (`CfgPath::new`) |
| `cfg_cmdline` | requirement | 2.2 |
| `cfg_files` | requirement | 2 (`load`, required/optional, precedence) |
| `cfgpath_vars` | requirement | 3 (`CfgPath`) |
| `cfg_builderr` | requirement | 3.1, 4 (`FallbackDir::builder`) |
| `client_cfg` | requirement | 4 |
| `stream_params` | requirement | 5, 5.1, 5.2 |
| `proto_reactor` | requirement | 6, 6.2, 6.3, and the negotiation prefix of 7 |
| `proto_traffic_ts` | requirement | 6.1, 7 (failed handshake) |
| `dirresp_body` | requirement | 9 |
| `chan_handshake` | gate | 7 |
| `circ_isolation` | gate | 10 |
| `persist_fsstate` | gate | 11 |
| `guard_api` | gate | 12 |
| `dirclient_routerdesc` | gate | 8 |

Two items in the sections above are **not** scoped to the rows that name their
sections. The driver's shared fixture module — the one that scripts the relay
of §6.2 — is compiled into **eleven of this step's twenty-one group targets**
(`chan_handshake`, `events_subscription`, `events_delivery`, `events_reactor`,
`proto_reactor`, `proto_traffic_ts` and the five `cfg_*` groups that load
configuration; the other ten targets build without it). So
`impl tor_linkspec::ChanTarget for FallbackDir` (§4) and the `ed25519-dalek`
re-export list of `tor_llcrypto::pk::ed25519` (§12) are load-bearing well
beyond `client_cfg` and `guard_api`: a mistake in either costs those eleven
groups at once, `chan_handshake` included, and a failure there means the step
is not satisfied. Each is called out in its own section for that reason.

## 14. Explicitly out of scope

- The `tracing` warning emitted when a guard restriction excludes every
  primary guard. It is a log line from a private module with no public
  observable, so no external crate can see it. Emit it anyway — it is in the
  milestone — but nothing here asserts on it.
- Anything requiring a live network, a chutney network, or a C-tor peer.
- TLS implementation details and cryptographic primitives.
- Onion services.
- Individual pluggable transports.
- Throughput or latency measured against a wall clock.
