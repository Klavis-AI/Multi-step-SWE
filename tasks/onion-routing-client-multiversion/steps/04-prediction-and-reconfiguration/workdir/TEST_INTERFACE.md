# Test interface — step 04 (`04-prediction-and-reconfiguration`)

Everything this step is checked through is named below: the crate, the module
path, the exact public signature the conformance driver calls, and the
behaviour it observes. Nothing outside this document is checked.

The driver is an external crate. It reaches your workspace only through `pub`
items on the paths written here — never through `pub(crate)` items,
`#[cfg(test)]` items, private modules, or `#[doc(hidden)]`. Items are grouped
by crate; section 15 maps each requirement group to the items it uses.

Four cargo features must exist and must compile, because the driver enables
them:

| Crate | Feature | Why the driver needs it |
| --- | --- | --- |
| `tor-netdir` | `testing` | exposes `tor_netdir::testnet` |
| `tor-netdoc` | `build_docs` | exposes `MicrodescBuilder`, needed by `testnet` |
| `tor-persist` | `testing` | exposes `tor_persist::TestingStateMgr` |
| `tor-rtcompat` | `tokio` | exposes `tor_rtcompat::tokio` |

The driver also depends on `async-trait 0.1.2`, `futures 0.3`, `hex 0.4`,
`rand 0.8`, `rand_chacha 0.3`, `serde_json 1` and `toml 0.5`, and on these
workspace crates by name: `arti-client`, `tor-bytes`, `tor-cell`,
`tor-chanmgr`, `tor-circmgr`, `tor-config`, `tor-dirmgr`, `tor-guardmgr`,
`tor-linkspec`, `tor-llcrypto`, `tor-netdir`, `tor-netdoc`, `tor-persist`,
`tor-rtcompat`, `tor-rtmock`.

Where a type is built with `derive_builder`, the generated builder must be
public under the name given, its setters must take the field type by value (or
`impl Into<_>` where noted), and `build()` must return
`Result<T, tor_config::ConfigBuildError>`.

---

## 1. `tor-circmgr` — configuration

### 1.1 `tor_circmgr::PathConfig`

```rust
#[derive(Debug, Clone, Builder, Deserialize, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[serde(deny_unknown_fields)]
pub struct PathConfig { /* private fields */ }

impl PathConfig {
    pub fn builder(&self) -> PathConfigBuilder;
    pub fn subnet_config(&self) -> tor_netdir::SubnetConfig;
}
impl Default for PathConfig { .. }
impl From<PathConfig> for PathConfigBuilder { .. }
```

Builder setters: `ipv4_subnet_family_prefix(u8)`,
`ipv6_subnet_family_prefix(u8)`.

Observable behaviour.

- Defaults are `ipv4_subnet_family_prefix = 16` and
  `ipv6_subnet_family_prefix = 32`. `PathConfig::default()` equals an untouched
  builder's `build()`.
- `subnet_config()` returns the `SubnetConfig` those two prefixes describe, so
  `PathConfig::default().subnet_config() == SubnetConfig::default()`.
- `From<PathConfig>` round-trips: taking a config into a builder and building
  it again yields an equal config.
- Note the upstream quirk: `PathConfig::builder` takes `&self`, unlike the
  other `builder()` constructors in this crate. It ignores the receiver and
  returns a default builder.

### 1.2 `tor_circmgr::PreemptiveCircuitConfig` — new this step

```rust
#[derive(Debug, Clone, Builder, Deserialize, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[serde(deny_unknown_fields)]
pub struct PreemptiveCircuitConfig { /* private fields */ }

impl PreemptiveCircuitConfig {
    pub fn builder() -> PreemptiveCircuitConfigBuilder;
}
impl Default for PreemptiveCircuitConfig { .. }
impl From<PreemptiveCircuitConfig> for PreemptiveCircuitConfigBuilder { .. }
```

Builder setters, with defaults:

| Setter | Type | Default |
| --- | --- | --- |
| `disable_at_threshold` | `usize` | `12` |
| `initial_predicted_ports` | `Vec<u16>` | `vec![80, 443]` |
| `prediction_lifetime` | `Duration` | 1 hour (`3600s`) |
| `min_exit_circs_for_port` | `usize` | `2` |

Observable behaviour.

- Every field has a default, so an untouched builder builds, and the result
  equals `PreemptiveCircuitConfig::default()`.
- Each setter is independent: setting one leaves the other three at their
  defaults.
- Fields are accepted at their extremes without validation error: an empty
  `initial_predicted_ports`, a zero `disable_at_threshold`, a zero
  `min_exit_circs_for_port` and a zero `prediction_lifetime` all build.
- `initial_predicted_ports` preserves the given order and duplicates verbatim;
  it is not sorted or deduplicated.
- `Deserialize` accepts a TOML table with any subset of the four keys, filling
  the rest from defaults, and `deny_unknown_fields` makes an unrecognized key
  an error. `prediction_lifetime` deserializes from a humantime string
  (`"1h"`, `"90s"`).
- `Eq` compares all four fields; `Debug` names the type.
- `From<PreemptiveCircuitConfig>` round-trips through the builder.

**What is out of scope here, and why.** The predictor these settings drive is
`pub(crate)` upstream and stays that way: no public item launches a
preemptive circuit on demand, reports how many are outstanding, or exposes a
predicted port ageing out after `prediction_lifetime`. Prediction is therefore
checked as the configuration surface above, plus `TargetPort` (§2.1) and
`ExitPathBuilder::from_target_ports` (§4.3) — the inputs and the path-selection
consequence, not the timer. Do not spend budget trying to make the predictor
observable from outside the crate; a contract-conforming implementation keeps
it private.

### 1.3 `tor_circmgr::CircuitTiming`

```rust
#[derive(Debug, Clone, Builder, Deserialize, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[serde(deny_unknown_fields)]
pub struct CircuitTiming { /* private fields */ }

impl CircuitTiming { pub fn builder() -> CircuitTimingBuilder; }
impl Default for CircuitTiming { .. }
impl From<CircuitTiming> for CircuitTimingBuilder { .. }
```

Builder setters and defaults: `max_dirtiness(Duration)` = 10 minutes,
`request_timeout(Duration)` = 60s, `request_max_retries(u32)` = 32,
`request_loyalty(Duration)` = 50ms.

### 1.4 `tor_circmgr::CircMgrConfig`

```rust
#[derive(Debug, Clone, Builder, Default, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
pub struct CircMgrConfig { /* private fields */ }

impl CircMgrConfig { pub fn builder() -> CircMgrConfigBuilder; }
```

Builder setters: `path_rules(PathConfig)`, `circuit_timing(CircuitTiming)`,
`preemptive_circuits(PreemptiveCircuitConfig)`.

Observable behaviour.

- The config has exactly these three sections. The preemptive section is new
  this step; a `CircMgrConfig` without it does not satisfy this contract.
- All three default, so `CircMgrConfigBuilder::default().build()` succeeds and
  equals `CircMgrConfig::default()`, which equals a builder with all three set
  to their own defaults.
- Setting one section leaves the other two at their defaults, and two configs
  that differ only in the preemptive section compare unequal.
- `Clone` is deep in the sense that matters: a clone compares equal to its
  source.

## 2. `tor-circmgr` — port prediction

### 2.1 `tor_circmgr::TargetPort`

```rust
#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Hash, PartialOrd, Ord, Serialize)]
pub struct TargetPort { /* private fields: ipv6: bool, port: u16 */ }

impl TargetPort {
    pub fn ipv4(port: u16) -> TargetPort;
    pub fn ipv6(port: u16) -> TargetPort;
    pub fn is_supported_by(&self, r: &tor_netdir::Relay<'_>) -> bool;
}
```

Observable behaviour.

- A port is identified by the pair (address family, port number).
  `TargetPort::ipv4(p) != TargetPort::ipv6(p)`, and two calls with the same
  arguments are equal.
- `Copy`: passing one by value leaves the original usable.
- `Hash` + `Eq` agree, so a `HashSet` deduplicates equal predictions and keeps
  the v4/v6 pair distinct. `Ord` is a total order, so a `BTreeSet` of
  predictions is deterministic; because the `ipv6` flag is declared before
  `port`, all IPv4 entries sort before all IPv6 entries, and within a family
  entries sort by ascending port.
- `Serialize`/`Deserialize` name both components as the fields `ipv6` (bool)
  and `port` (integer), so a stored prediction round-trips and the family flag
  survives.
- `is_supported_by(r)` delegates to the relay's declared exit policy for the
  matching family: `r.supports_exit_port_ipv4(port)` for an IPv4 target,
  `r.supports_exit_port_ipv6(port)` for an IPv6 target. A relay that allows a
  port on one family only supports the target for that family.

## 3. `tor-circmgr` — the manager

### 3.1 `tor_circmgr::CircMgr`

```rust
pub struct CircMgr<R: Runtime> { /* private */ }

impl<R: Runtime> CircMgr<R> {
    pub fn new<SM>(
        config: CircMgrConfig,
        storage: SM,
        runtime: &R,
        chanmgr: Arc<tor_chanmgr::ChanMgr<R>>,
    ) -> Result<Arc<Self>>
    where SM: tor_persist::StateMgr + Send + Sync + 'static;

    pub fn reconfigure(
        &self,
        new_config: &CircMgrConfig,
        how: tor_config::Reconfigure,
    ) -> std::result::Result<(), tor_config::ReconfigureError>;

    pub fn netdir_is_sufficient(&self, netdir: &tor_netdir::NetDir) -> bool;
    pub fn update_network(&self, netdir: &tor_netdir::NetDir);
    pub fn update_network_parameters(&self, p: &tor_netdir::params::NetParameters);
}
```

Observable behaviour.

- `new` takes ownership of the config by value, borrows the runtime, and
  returns `Arc<Self>`. It succeeds against a `TestingStateMgr` and a
  `ChanMgr::new(runtime)` without any network access.
- `reconfigure` is the live-reconfiguration entry point, and it is the object
  that decides which settings may change on a running client:
  - Changing `preemptive_circuits.initial_predicted_ports` is **refused**,
    because those ports were only the startup seed and changing them later has
    no effect. Under `Reconfigure::AllOrNothing` or
    `Reconfigure::CheckAllOrNothing` this is
    `Err(ReconfigureError::CannotChange { field })` with
    `field == "preemptive_circuits.initial_predicted_ports"`; under
    `Reconfigure::WarnOnFailures` it is `Ok(())`.
  - The other three preemptive settings (`disable_at_threshold`,
    `prediction_lifetime`, `min_exit_circs_for_port`) may change on a running
    manager, under any `Reconfigure` mode.
  - Reconfiguring to an equal config is always `Ok(())`, under every mode.
  - `Reconfigure::CheckAllOrNothing` must not apply anything: after a
    successful check, a subsequent `AllOrNothing` reconfigure to the *original*
    config still succeeds, which is only true if the check left the manager
    alone.
- `netdir_is_sufficient(netdir)` returns whether this directory has enough
  information for the manager to use. It defers to the guard manager's
  primary-guard microdescriptor test (§5.1), so it is `false` while any
  microdescriptor for a primary guard is missing and `true` once they have all
  arrived. It never blocks and never fetches.
- `update_network(netdir)` hands a new directory to the guard manager. It
  takes `&self`, so it can be called on a shared `Arc<CircMgr<_>>`, and it is
  idempotent: calling it twice with the same directory does not change what
  `netdir_is_sufficient` reports.
- `update_network_parameters(p)` is the narrower of the two: it is for a
  change to the consensus parameters alone, where `update_network` is for any
  change to the network. It also takes `&self` and never blocks. At this
  checkpoint the only thing it feeds is circuit-timeout estimation, and the
  estimator's state is `pub(crate)`, so **nothing public observes its effect**
  and no case in scope calls it. It is listed here because it is part of the
  public shape this step's `CircMgr` must have and is named in the signature
  block above, but it is **declared out of scope**: a correct implementation that
  routes the parameters into timeout estimation and one that accepts them and
  does nothing are indistinguishable through this contract, so no case tries to
  tell them apart. Parameter *overrides* are checked, on `NetDir`
  (`replace_overridden_parameters`, §7.1) and on `PartialNetDir::new` (§7.2),
  which is a different mechanism.

### 3.2 `tor_circmgr::DirInfo`

```rust
#[derive(Debug, Copy, Clone)]
#[non_exhaustive]
pub enum DirInfo<'a> {
    Fallbacks(&'a [tor_netdir::fallback::FallbackDir]),
    Directory(&'a tor_netdir::NetDir),
}

impl<'a> From<&'a [tor_netdir::fallback::FallbackDir]> for DirInfo<'a> { .. }
impl<'a> From<&'a tor_netdir::NetDir> for DirInfo<'a> { .. }
```

`(&netdir).into()` yields `DirInfo::Directory`.

Note the crate the fallback type comes from. `DirInfo` lives in
`tor-circmgr`, and `tor-dirmgr` depends on `tor-circmgr`, so this enum
cannot name `tor_dirmgr::FallbackDir` — that is a dependency cycle and a
workspace-wide build failure. The type is `tor_netdir::fallback::FallbackDir`,
which `tor-dirmgr` re-exports under its own name (§14.1 lists that re-export,
and that re-export is the right path for an `arti-client` caller; it is not
the right path here).

### 3.3 `tor_circmgr::Error`

```rust
#[derive(Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    NoRelays(String),
    NeedConsensus,
    Guard(#[from] tor_guardmgr::PickGuardError),
    /* ... other variants ... */
}
```

Observable behaviour.

- `NoRelays(String)` is returned when no relay in the directory can fill a
  required position. Its `Display` begins `no relays for circuit: ` and
  carries the explanation.
- `NeedConsensus` is returned when an operation needs a full consensus and was
  only given fallback directories. Its `Display` mentions a consensus
  directory.
- `Guard(_)` wraps a `tor_guardmgr::PickGuardError` and converts from it via
  `#[from]`, so a guard-selection failure surfaces from path building without
  being flattened into a string.

## 4. `tor-circmgr` — path construction

### 4.1 `tor_circmgr::path`

```rust
pub mod path {
    pub mod dirpath;
    pub mod exitpath;

    #[derive(Debug, Clone)]
    pub struct TorPath<'a> { /* private */ }

    impl<'a> TorPath<'a> {
        pub fn new_one_hop(relay: tor_netdir::Relay<'a>) -> Self;
        pub fn new_fallback_one_hop(fallback_dir: &'a tor_netdir::fallback::FallbackDir) -> Self;
        pub fn new_multihop(relays: impl IntoIterator<Item = tor_netdir::Relay<'a>>) -> Self;
        pub fn len(&self) -> usize;
        pub fn is_empty(&self) -> bool;
    }
}
```

Observable behaviour.

- `new_one_hop(relay).len() == 1`.
- `new_multihop(iter).len()` is the number of relays the iterator yielded, in
  order; a three-relay path reports 3.
- `len()` is the hop count, not a byte count, and `is_empty()` agrees with
  `len() == 0`.
- `new_multihop` takes the relays **by value**, and `Relay<'a>` is `Clone`
  (§7.3), so a caller may keep its own copy of the list.
- `new_fallback_one_hop` names `tor_netdir::fallback::FallbackDir`, **not**
  `tor_dirmgr::FallbackDir`. Both names denote the same type — `tor-dirmgr`
  re-exports it — but `tor-dirmgr` already depends on `tor-circmgr`, so
  reaching the type through `tor_dirmgr` from inside `tor-circmgr` is a cargo
  dependency cycle and the whole workspace stops building. This is the same
  layering rule §3.2 gives for `DirInfo::Fallbacks`, and it governs every
  mention of `FallbackDir` inside `tor-circmgr`, this section included.

### 4.2 `tor_circmgr::path::dirpath::DirPathBuilder`

```rust
#[derive(Debug, Clone, Default)]
pub struct DirPathBuilder {}

impl DirPathBuilder {
    pub fn new() -> Self;
    pub fn pick_path<'a, R: Rng, RT: Runtime>(
        &self,
        rng: &mut R,
        netdir: DirInfo<'a>,
        guards: Option<&tor_guardmgr::GuardMgr<RT>>,
    ) -> Result<(TorPath<'a>, Option<GuardMonitor>, Option<GuardUsable>)>;
}
```

Observable behaviour.

- With `guards: None` and a `DirInfo::Directory`, it returns a one-hop path to
  a relay that is a directory cache. The path length is 1, and the chosen
  relay satisfies `Relay::is_dir_cache`.
- Selection is driven only by the supplied `rng`: two calls with identically
  seeded RNGs over the same directory return the same relay. Nothing is read
  from the wall clock or from an ambient RNG.
- If the directory contains no directory cache at all, the result is
  `Err(Error::NoRelays(_))`.

### 4.3 `tor_circmgr::path::exitpath::ExitPathBuilder`

```rust
pub struct ExitPathBuilder<'a> { /* private */ }

impl<'a> ExitPathBuilder<'a> {
    pub fn from_target_ports(wantports: impl IntoIterator<Item = TargetPort>) -> Self;
    pub fn from_chosen_exit(exit_relay: tor_netdir::Relay<'a>) -> Self;
    pub fn for_any_exit() -> Self;
    pub fn pick_path<R: Rng, RT: Runtime>(
        &self,
        rng: &mut R,
        netdir: DirInfo<'a>,
        guards: Option<&tor_guardmgr::GuardMgr<RT>>,
        config: &PathConfig,
    ) -> Result<(TorPath<'a>, Option<GuardMonitor>, Option<GuardUsable>)>;
}
```

Observable behaviour.

- Three ways to say what the exit must be: a set of predicted target ports
  (`from_target_ports`, the constructor preemptive circuits use), a specific
  relay (`from_chosen_exit`), or no constraint beyond being an exit
  (`for_any_exit`).
- A successfully built path has length 3. The last hop is the exit, the first
  hop is a guard when `guards` is `Some`.
- `from_target_ports`: every port in the set is supported by the chosen exit
  (`TargetPort::is_supported_by` is true for each). If no relay in the
  directory supports all of them, the result is `Err(Error::NoRelays(_))`.
- `from_chosen_exit(r)`: the last hop of the returned path is `r`.
- `pick_path` takes the path rules as an explicit `&PathConfig` argument, so
  subnet-family strictness is a per-call input, not global state.
- The family and subnet predicates of §7.3 are enforced here: no two hops of a
  returned path are in the same declared family, and no two are within the
  same subnet under the supplied `PathConfig`.
- **Which error a violated constraint produces depends on which position it
  emptied, and the two are not interchangeable.** When `guards` is `Some`, the
  exit's family is not filtered out by the path builder itself: it is turned
  into a `GuardRestriction::AvoidAllIds` and pushed onto the guard usage
  (§5.3), so the guard manager is the component that runs out of candidates.
  A `from_chosen_exit` build in which every usable guard is in the exit's
  family therefore fails with `Err(Error::Guard(_))` — the `PickGuardError`
  of §5.4 wrapped by the `#[from]` conversion of §3.3 — and **not** with
  `Error::NoRelays`. `Err(Error::NoRelays(_))` is for the other case: no relay
  in the directory can fill a required position at all, as when no exit
  supports the requested ports.
- **`pick_path` refreshes the guard manager from the directory it is handed.**
  Before it selects, and whatever the caller's `GuardMgr` state, it passes the
  `DirInfo`'s `NetDir` to `update_network` (§5.1) and then asks the manager for
  a guard. A `GuardMgr` constructed with `GuardMgr::new` and never separately
  given a network therefore still yields a usable guard on a directory that
  has one: the sample is populated from the directory `pick_path` was handed,
  not from whatever the caller had loaded into the manager beforehand. This is
  why the restriction of the bullet above is what runs out of candidates.
- Given a `DirInfo::Fallbacks`, the result is `Err(Error::NeedConsensus)`.
- Selection is a pure function of the supplied `rng` and directory: identical
  seeds give identical paths.

## 5. `tor-guardmgr`

### 5.1 `tor_guardmgr::GuardMgr`

```rust
pub struct GuardMgr<R: Runtime> { /* private */ }

impl<R: Runtime> GuardMgr<R> {
    pub fn new<S>(runtime: R, state_mgr: S) -> Result<Self, GuardMgrError>
    where S: tor_persist::StateMgr + Send + Sync + 'static;

    pub fn update_network(&self, netdir: &tor_netdir::NetDir);
    pub fn netdir_is_sufficient(&self, netdir: &tor_netdir::NetDir) -> bool;
    pub fn select_guard(
        &self,
        usage: GuardUsage,
        netdir: Option<&tor_netdir::NetDir>,
    ) -> Result<(Guard, GuardMonitor, GuardUsable), PickGuardError>;
}
```

Observable behaviour.

- `new` takes the runtime by value and a state manager; it succeeds with a
  `TestingStateMgr` and spawns its own background tasks. A fresh manager has
  no guards until `update_network` is called.
- `netdir_is_sufficient(netdir)` is the "directory is incomplete until the
  primary guards' microdescriptors arrive" test that is new this step. It
  returns `true` exactly when the number of the manager's primary guards whose
  microdescriptor is missing from `netdir` is zero.
  - On a complete directory it is `true`.
  - Given a `NetDir` built from the same consensus but with the
    microdescriptors of the manager's primary guards omitted, it is `false`.
  - Omitting microdescriptors of relays that are *not* primary guards does not
    make it `false`.
  - **A primary guard whose consensus entry is gone does not make it `false`
    either.** "Missing microdescriptor" means *the consensus lists this relay
    and its microdescriptor has not arrived* (§7.1's `missing_descriptor_for`);
    a relay the consensus no longer lists is a definite answer — the manager no
    longer has that guard to wait for — not an outstanding download. A directory
    built from the same consensus with the primary guards removed outright is
    therefore still sufficient, and a case in scope asserts it.
  - It is a pure query: calling it does not change the manager's state, and
    calling it repeatedly gives the same answer.
  - It must be answerable without a network fetch and without blocking.
- `update_network(netdir)` takes `&self` and refreshes the sample from the
  directory. After it has been given a directory containing usable guards,
  `select_guard` can succeed.
- `select_guard(usage, netdir)` returns a `Guard` plus a monitor and a
  usability handle, or a `PickGuardError`. The `netdir` argument is optional
  because the manager only consults it when all known guards are down.

### 5.2 `tor_guardmgr::GuardUsage`, `GuardUsageBuilder`, `GuardUsageKind`

```rust
#[derive(Clone, Debug, Default, Builder)]
#[builder(build_fn(error = "tor_config::ConfigBuildError"))]
pub struct GuardUsage { /* private */ }

impl GuardUsageBuilder {
    pub fn new() -> Self;
    pub fn kind(&mut self, kind: GuardUsageKind) -> &mut Self;
    pub fn push_restriction(&mut self, restriction: GuardRestriction) -> &mut Self;
    pub fn build(&self) -> Result<GuardUsage, tor_config::ConfigBuildError>;
}

#[derive(Clone, Debug, Eq, PartialEq)]
#[non_exhaustive]
pub enum GuardUsageKind { Data, OneHopDirectory }

impl Default for GuardUsageKind { /* -> Data */ }
```

Observable behaviour.

- `GuardUsageBuilder::new().build()` succeeds: both fields default. The
  default kind is `Data` and the default restriction list is empty.
- `push_restriction` appends rather than replacing, so calling it twice leaves
  two restrictions in force, and both are respected by `select_guard`.
- Setters are chainable and return `&mut Self`.

### 5.3 `tor_guardmgr::GuardRestriction`

```rust
#[derive(Clone, Debug)]
#[non_exhaustive]
pub enum GuardRestriction {
    AvoidId(tor_llcrypto::pk::ed25519::Ed25519Identity),
    AvoidAllIds(HashSet<tor_llcrypto::pk::ed25519::Ed25519Identity>),
}
```

Observable behaviour.

- `AvoidId(id)` forbids one relay by Ed25519 identity: a `select_guard` whose
  usage carries it never returns a `Guard` whose relay has that identity.
- `AvoidAllIds(set)` forbids every identity in the set at once, and is
  equivalent to pushing one `AvoidId` per member.
- This is the mechanism by which the exit is kept out of the guard position
  (§4.3): the caller collects the identities that must not be the guard —
  the exit and its family, per §7.1's `known_family_members` — and passes
  them as a restriction.
- Restricting away *every* usable guard is an error, not a silent
  unrestricted pick: `select_guard` returns
  `Err(PickGuardError::EveryoneIsDown)`.

### 5.4 `tor_guardmgr::PickGuardError`

```rust
#[derive(Clone, Debug, Error)]
#[non_exhaustive]
pub enum PickGuardError {
    EveryoneIsDown,
    SampleIsEmpty,
}
```

`EveryoneIsDown` is the variant produced when all candidates are excluded or
unreachable; its `Display` says everybody is down or pending. It is also what a
guard manager with an *empty* sample produces when it has no directory to
expand the sample from, so `SampleIsEmpty` has to exist in the enum but no
input reaches it at this checkpoint, and nothing here checks it.

### 5.5 `tor_guardmgr::Guard`

```rust
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct Guard { /* private */ }

impl Guard {
    pub fn id(&self) -> &GuardId;
    pub fn get_relay<'a>(&self, netdir: &'a tor_netdir::NetDir) -> Option<tor_netdir::Relay<'a>>;
}

impl tor_linkspec::ChanTarget for Guard { .. }
```

`get_relay(netdir)` looks the guard up by its (Ed25519, RSA) identity pair and
returns `None` when the directory does not list that pair.

## 6. `tor-config`

### 6.1 `tor_config::MutCfg`

```rust
pub struct MutCfg<T> { /* private: RwLock<Arc<T>> */ }

impl<T> MutCfg<T> {
    pub fn new(config: T) -> Self;
    pub fn get(&self) -> Arc<T>;
    pub fn check_and_replace(&self, old_config: &Arc<T>, new_config: T) -> bool;
    pub fn replace(&self, new_config: T);
    pub fn map_and_replace<F>(&self, func: F) where F: FnOnce(&Arc<T>) -> T;
}

impl<T: Default> Default for MutCfg<T> { .. }
impl<T> From<T> for MutCfg<T> { .. }
```

Observable behaviour.

- The holder for a config that can be swapped on a running client. All methods
  take `&self`; no `&mut self` is required to replace the contents.
- `get()` hands out an `Arc<T>` snapshot. A snapshot taken before a `replace`
  keeps its old value: readers are never mutated underneath.
- `replace(new)` installs `new` unconditionally; the next `get()` observes it.
- `check_and_replace(&old, new)` is compare-and-swap by pointer identity. It
  returns `true` and installs `new` when the stored `Arc` is the same
  allocation as `old` (as by `Arc::ptr_eq`); otherwise it returns `false` and
  leaves the stored value alone. A stale snapshot therefore cannot clobber a
  newer config, and an equal-but-distinct `Arc` does not count as a match.
- `map_and_replace(f)` calls `f` with the current value and stores what it
  returns, so a field can be edited in place without a lost update.
- `MutCfg::<T>::default()` holds `T::default()`, and `T::into()` holds that
  value.

### 6.2 `tor_config::Reconfigure`

```rust
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[non_exhaustive]
pub enum Reconfigure {
    AllOrNothing,
    WarnOnFailures,
    CheckAllOrNothing,
}

impl Reconfigure {
    pub fn cannot_change<S: AsRef<str>>(self, field: S) -> Result<(), ReconfigureError>;
}
```

Observable behaviour.

- The three modes are the policy a subsystem consults when it meets a setting
  it cannot change while running.
- `cannot_change(field)`:
  - `AllOrNothing` → `Err(ReconfigureError::CannotChange { field })` with
    `field` equal to `field.as_ref().to_owned()`.
  - `CheckAllOrNothing` → the same error. A dry run reports the same failures
    the real run would.
  - `WarnOnFailures` → `Ok(())`, with the problem logged rather than returned.
- `field` is generic over `AsRef<str>`, so `&str`, `String` and `&String` all
  work, and the stored field name is the exact string passed, including dotted
  paths such as `"preemptive_circuits.initial_predicted_ports"`.
- `Copy`, so a mode can be passed by value repeatedly.

### 6.3 `tor_config::ReconfigureError`

```rust
#[derive(Debug, Clone, Error)]
#[non_exhaustive]
pub enum ReconfigureError {
    CannotChange { field: String },
}
```

`Display` is exactly `Cannot change <field> on a running client.` — the field
name, then that sentence, with the trailing period.

## 7. `tor-netdir`

### 7.1 `tor_netdir::NetDir`

```rust
pub struct NetDir { /* private */ }

impl NetDir {
    pub fn lifetime(&self) -> &netstatus::Lifetime;
    pub fn params(&self) -> &params::NetParameters;
    pub fn replace_overridden_parameters(&mut self, new_replacement: &netstatus::NetParams<i32>);
    pub fn all_relays(&self) -> impl Iterator<Item = UncheckedRelay<'_>>;
    pub fn relays(&self) -> impl Iterator<Item = Relay<'_>>;
    pub fn by_id(&self, id: &Ed25519Identity) -> Option<Relay<'_>>;
    pub fn by_id_pair(&self, ed_id: &Ed25519Identity, rsa_id: &RsaIdentity) -> Option<Relay<'_>>;
    pub fn by_rsa_id(&self, rsa_id: &RsaIdentity) -> Option<Relay<'_>>;
    pub fn by_rsa_id_unchecked(&self, rsa_id: &RsaIdentity) -> Option<UncheckedRelay<'_>>;
    pub fn id_pair_listed(&self, ed_id: &Ed25519Identity, rsa_id: &RsaIdentity) -> Option<bool>;
    pub fn rsa_id_is_listed(&self, rsa_id: &RsaIdentity) -> bool;
    pub fn missing_descriptor_for(&self, rsa_id: &RsaIdentity) -> bool;
    pub fn known_family_members<'a>(&'a self, relay: &'a Relay<'a>)
        -> impl Iterator<Item = Relay<'a>>;
}

impl MdReceiver for NetDir { .. }
```

Observable behaviour.

- `relays()` yields only usable relays; `all_relays()` yields every listed
  relay as an `UncheckedRelay`, usable or not. Every relay from `relays()`
  appears in `all_relays()`, and the relays from `relays()` are exactly the
  `all_relays()` entries whose `is_usable()` is true.
- Lookups agree with iteration: for each relay from `relays()`,
  `by_id(r.id())` returns a relay with the same identity, and `by_rsa_id`
  likewise. An identity not in the directory gives `None`.
- `id_pair_listed(ed, rsa)` distinguishes three answers, and the third one is
  the state this step's headline query waits on:
  - `Some(true)` — that exact pair is listed: the consensus has a router status
    for the RSA identity, that status carries a usable Ed25519 identity, and the
    microdescriptor in hand names `ed`.
  - `Some(false)` — **definitely not that pair**: either the RSA identity has no
    router status in the consensus at all, or its status carries no usable
    Ed25519 identity, or the microdescriptor in hand names a *different* Ed25519
    identity.
  - `None` — **the RSA identity is listed and we cannot yet tell**: its router
    status is present and does carry a usable Ed25519 identity, but that relay's
    microdescriptor has **not arrived**, so the pair cannot be checked. This is
    the "outstanding download" state, and it is *not* "not listed".
  `rsa_id_is_listed` is the boolean shorthand for "has a router status in this
  consensus", independent of any microdescriptor.
- `missing_descriptor_for(rsa_id)` is true when the consensus lists that relay
  but its microdescriptor has not arrived, and false both when the descriptor
  is present and when the relay is not listed.
- `params()` returns the effective parameters (§7.4).
- `replace_overridden_parameters(p)` recomputes the parameters from the
  consensus and then applies `p` as the only override set. It takes `&mut
  self`. Overrides from a previous call are **discarded**, not merged: a
  parameter that the old override set changed and the new one does not returns
  to its consensus (or default) value. An unrecognized key in `p` is ignored
  with a warning, not an error.
- `known_family_members(relay)` yields the relays that `relay` names in its
  family **and** that name `relay` back — family membership is mutual. A relay
  named in the family whose own descriptor does not reciprocate is omitted, as
  is one for which no `Relay` exists in this directory. The relation is
  symmetric: if `b` appears in `known_family_members(a)`, then `a` appears in
  `known_family_members(b)`.

### 7.2 `tor_netdir::PartialNetDir` and `tor_netdir::MdReceiver`

```rust
// Defined in `tor_netdoc::doc::netstatus` (§12) and taken here by value.
pub type MdConsensus = Consensus<MdConsensusRouterStatus>;

#[derive(Debug, Clone)]
pub struct Consensus<RS> { /* private */ }

pub struct PartialNetDir { /* private */ }

impl PartialNetDir {
    pub fn new(
        consensus: MdConsensus,
        replacement_params: Option<&netstatus::NetParams<i32>>,
    ) -> Self;
    pub fn lifetime(&self) -> &netstatus::Lifetime;
    pub fn fill_from_previous_netdir<'a>(&mut self, prev: &'a NetDir) -> Vec<&'a MdDigest>;
    pub fn have_enough_paths(&self) -> bool;
    pub fn unwrap_if_sufficient(self) -> std::result::Result<NetDir, PartialNetDir>;
    pub fn missing_descriptor_for(&self, rsa_id: &RsaIdentity) -> bool;
}

impl MdReceiver for PartialNetDir { .. }

pub trait MdReceiver {
    fn missing_microdescs(&self) -> Box<dyn Iterator<Item = &MdDigest> + '_>;
    fn add_microdesc(&mut self, md: Microdesc) -> bool;
}
```

Observable behaviour.

- `PartialNetDir::new` takes the consensus **by value**, and
  `Consensus<RS>` — hence `MdConsensus` — derives `Debug` and `Clone`. One
  consensus can therefore be cloned and handed to two `PartialNetDir`s, which
  is how a case builds two directories that want exactly the same digests.
  An implementation that omits the `Clone` derive does not satisfy this
  contract even though every method signature matches.
- `PartialNetDir::new(consensus, None)` starts with no microdescriptors: its
  `missing_microdescs()` count equals the number of relays in the consensus.
- `add_microdesc(md)` returns `true` when the descriptor was wanted and
  `false` otherwise. Adding a wanted descriptor lowers the
  `missing_microdescs()` count by exactly one; adding the *same* descriptor a
  second time returns `false` and leaves the count unchanged.
- `missing_microdescs()` yields digests, and an added descriptor's digest is no
  longer among them.
- `have_enough_paths()` is false while too few descriptors have arrived and
  true once enough have. `unwrap_if_sufficient()` agrees with it exactly: it
  returns `Ok(NetDir)` when `have_enough_paths()` is true and gives the
  `PartialNetDir` back as `Err` otherwise, so nothing is lost on failure.
- The second argument of `new` is the parameter override set, applied on top of
  the consensus parameters (§7.4). `None` means no overrides.
- `fill_from_previous_netdir(prev)` carries microdescriptors forward instead of
  re-downloading them. It walks the descriptors `prev` already holds, keeps the
  ones whose digest this consensus still asks for, and returns those digests.
  A descriptor taken this way counts exactly as one that arrived through
  `add_microdesc`: it leaves `missing_microdescs()`, so filling a fresh
  `PartialNetDir` from a `NetDir` built on the same consensus makes it complete
  with no further additions. The match is on the digest, not on the relay:
  descriptors `prev` does not hold are not invented, so a `prev` built on an
  unrelated consensus carries nothing over, returns an empty list, and leaves
  every digest missing.

### 7.3 `tor_netdir::Relay`, `UncheckedRelay`, `SubnetConfig`

```rust
#[derive(Clone)]
pub struct Relay<'a> { /* private */ }

impl<'a> Relay<'a> {
    pub fn id(&self) -> &Ed25519Identity;
    pub fn rsa_id(&self) -> &RsaIdentity;
    pub fn same_relay<'b>(&self, other: &Relay<'b>) -> bool;
    pub fn supports_exit_port_ipv4(&self, port: u16) -> bool;
    pub fn supports_exit_port_ipv6(&self, port: u16) -> bool;
    pub fn is_dir_cache(&self) -> bool;
    pub fn is_flagged_guard(&self) -> bool;
    pub fn in_same_subnet<'b>(&self, other: &Relay<'b>, subnet_config: &SubnetConfig) -> bool;
    pub fn in_same_family<'b>(&self, other: &Relay<'b>) -> bool;
    pub fn policies_allow_some_port(&self) -> bool;
    pub fn ipv4_policy(&self) -> Arc<PortPolicy>;
    pub fn ipv6_policy(&self) -> Arc<PortPolicy>;
    pub fn ipv4_declared_policy(&self) -> &Arc<PortPolicy>;
    pub fn ipv6_declared_policy(&self) -> &Arc<PortPolicy>;
    pub fn md(&self) -> &Microdesc;
    pub fn rs(&self) -> &netstatus::MdConsensusRouterStatus;
}

pub struct UncheckedRelay<'a> { /* private */ }

impl<'a> UncheckedRelay<'a> {
    pub fn is_usable(&self) -> bool;
    pub fn into_relay(self) -> Option<Relay<'a>>;
    pub fn is_flagged_guard(&self) -> bool;
    pub fn is_dir_cache(&self) -> bool;
    pub fn id(&self) -> &Ed25519Identity;
    pub fn rsa_id(&self) -> &RsaIdentity;
}

#[derive(Deserialize, Debug, Clone, Copy)]
#[serde(deny_unknown_fields)]
pub struct SubnetConfig { /* private */ }

impl SubnetConfig {
    pub fn new(subnets_family_v4: u8, subnets_family_v6: u8) -> Self;
}
impl Default for SubnetConfig { /* -> new(16, 32) */ }

impl<'a> tor_linkspec::ChanTarget for Relay<'a> { .. }
impl<'a> tor_linkspec::CircTarget for Relay<'a> { .. }
```

Observable behaviour.

- `in_same_family(other)` is true only when **both** relays name each other in
  their family lines. A one-sided claim does not create a family. The relation
  is symmetric and a relay is in its own family with itself.
- `in_same_subnet(other, cfg)` compares address prefixes under `cfg`. With
  `SubnetConfig::default()` two relays in the same IPv4 /16 are in the same
  subnet and two in different /16s are not. A wider prefix (e.g. `new(8, 16)`)
  puts strictly more pairs in the same subnet; `new(0, 0)` puts every pair with
  addresses of that family together; a prefix above the address width
  (`new(33, 129)`) puts no pair together.
- `supports_exit_port_ipv4(port)` agrees with `ipv4_policy().allows_port(port)`
  for a usable exit, and likewise for IPv6. Both are false for a relay whose
  policy rejects everything.
- `policies_allow_some_port()` is true when either family's policy allows at
  least one port, and false when both reject everything.
- `id()`/`ed_identity()` and `rsa_id()`/`rsa_identity()` return the same
  identities through both the inherent methods and the `ChanTarget` trait.
- `ipv4_policy()` returns the policy that applies and
  `ipv4_declared_policy()` returns what the descriptor said. The two differ for
  exactly one reason: a relay the consensus flags **BadExit** has an effective
  policy that rejects every port, whatever its microdescriptor declared. For a
  relay without that flag the two agree port for port, and
  `supports_exit_port_ipv4` follows the effective one.
- `same_relay(other)` compares the two relays' identities — both the Ed25519
  and the RSA identity — and not the objects. Two `Relay` values obtained from
  the same directory by different lookups are the same relay; two different
  listed relays are not.
- `UncheckedRelay::into_relay()` returns `Some` exactly when `is_usable()` is
  true and `None` otherwise, and the `Relay` it yields carries the same two
  identities the `UncheckedRelay` reported.
- `Relay<'a>` is **`Clone`**. It is a pair of borrowed views into the
  directory, so a clone is cheap and copies no directory data — but the derive
  has to be there, because the path builders take a relay by value and a case
  still needs its own handle afterwards: `TorPath::new_multihop`
  (§4.1) and `ExitPathBuilder::from_chosen_exit` (§4.3) both consume what they
  are given. `UncheckedRelay` is not required to be `Clone`.
- **This section supersedes `/app/docs/step-01/TEST_INTERFACE.md` on the
  policy accessors.** Step 01 declares `ipv4_policy(&self) -> &Arc<PortPolicy>`
  and `ipv6_policy(&self) -> &Arc<PortPolicy>`. At this checkpoint both return
  `Arc<PortPolicy>` **by value**, as written above, because a BadExit relay's
  effective policy is built on demand and there is no stored value to borrow.
  Only the `_declared_` pair still returns a reference. Where the two binding
  documents disagree, this one governs.

### 7.4 `tor_netdir::params`

```rust
pub mod params {
    #[derive(Clone, Debug)]
    #[non_exhaustive]
    pub struct NetParameters {
        pub bw_weight_scale: BoundedInt32<1, { i32::MAX }>,   // "bwweightscale", default 10_000
        pub circuit_window: BoundedInt32<100, 1000>,          // "circwindow",    default 1_000
        /* ... many more ... */
    }
}
```

`BoundedInt32<LOW, HIGH>` exposes `pub fn get(&self) -> i32`.

Observable behaviour.

- Each parameter is addressed on the wire by its consensus keyword and in Rust
  by a snake-case field. `bwweightscale` → `bw_weight_scale`, `circwindow` →
  `circuit_window`.
- Values are clamped into the declared bounds, never rejected and never
  wrapped. `circwindow` is bounded to `100..=1000`: an override of `50` yields
  `100`, an override of `5000` yields `1000`, `222` yields `222`.
  `bwweightscale` has a lower bound of `1`: an override of `0` or a negative
  value yields `1`.
- An unset parameter keeps its documented default, so a directory built from a
  consensus with no `params` line reports `circuit_window == 1000` and
  `bw_weight_scale == 10000`.
- Unrecognized keywords are ignored; they do not make the update fail and do
  not disturb recognized parameters set in the same batch.

### 7.5 `tor_netdir::testnet` (feature `testing`)

```rust
pub mod testnet {
    pub struct NodeBuilders {
        pub rs: tor_netdoc::doc::netstatus::RouterStatusBuilder<MdDigest>,
        pub md: tor_netdoc::doc::microdesc::MicrodescBuilder,
        pub omit_md: bool,
        pub omit_rs: bool,
    }

    pub fn construct_netdir() -> Result<PartialNetDir>;
    pub fn construct_custom_netdir<F>(func: F) -> Result<PartialNetDir>
        where F: FnMut(usize, &mut NodeBuilders);
    pub fn construct_network() -> Result<(MdConsensus, Vec<Microdesc>)>;
    pub fn construct_custom_network<F>(func: F) -> Result<(MdConsensus, Vec<Microdesc>)>
        where F: FnMut(usize, &mut NodeBuilders);
}
```

Observable behaviour.

- These build a deterministic synthetic directory with no I/O and no clock
  dependence beyond the consensus lifetime the builders set.
- The callback is invoked once per node with the node's index and mutable
  access to both the router-status builder and the microdescriptor builder, so
  a caller can set families, exit policies, flags and addresses per node.
- Setting `omit_md = true` for a node produces a consensus that lists the node
  while the returned microdescriptor vector omits its descriptor, which is how
  an "incomplete directory" is constructed. `omit_rs = true` omits the node
  from the consensus instead.
- `construct_custom_network` returns the consensus and the microdescriptors
  separately, so the caller controls which descriptors are fed into a
  `PartialNetDir`.

#### The synthetic network

The network these functions build is fixed, and the conformance driver asserts
its composition directly — eleven of the twenty-three groups in §15 read
relays out of it by index and expect the properties below. It is part of the
contract, not an implementation choice. `construct_network()` and
`construct_netdir()` build exactly this; the `_custom_` forms build it and hand
each node to the callback before the node is added.

- **Forty nodes**, indexed `0..40`. That index is the callback's first
  argument, and `NetDir::relays().count()` over the unmodified network is 40.
- **Identities.** Node *i* has ed25519 identity `[i; 32]` and RSA identity
  `[i; 20]` — every byte equal to the index. Node 0 is therefore `0x00…00` and
  node 39 is `0x27…27`, and a relay can be looked up by either identity.
- **Onion keys** are present but are unusable placeholders. The ntor key is
  literally the 32 bytes `b"----nothing in dirmgr uses this-"`, the same for
  every node.
- **Addresses.** Exactly one OR address per node, `(i % 5).0.0.3:9001`, so the
  five addresses `0.0.0.3:9001` through `4.0.0.3:9001` repeat every five nodes
  and nodes *i* and *i*+5 share one. With one address and two identities, a
  node has exactly three link specifiers (§14.2).
- **Flags.** Every node is `Running`, `Valid` and `V2Dir`. Beyond that the
  flags go by decade: nodes 0–9 carry nothing further, 10–19 are `Exit`,
  20–29 are `Guard`, and 30–39 are both `Exit` and `Guard`.
- **Exit policy.** A node with the `Exit` flag and an **odd** index accepts
  `80,443`; one with the `Exit` flag and an **even** index accepts `1-65535`.
  Every node without the flag rejects `1-65535`. No node declares an IPv6
  policy, so `supports_exit_port_ipv6` is false throughout.
- **Families.** Node *i* declares exactly one family member, `[i ^ 1; 20]`.
  Nodes `2k` and `2k+1` therefore name each other, every declaration is
  mutual, and `known_family_members` (§7.1) for node *i* is the single node
  `i ^ 1`.
- **Directory caches.** Even-numbered nodes advertise `DirCache=2`;
  odd-numbered nodes advertise no subprotocols at all. `Relay::is_dir_cache()`
  is true exactly for the even ones. Guard sampling only considers directory
  caches, so overriding this with the callback is how a test pins down which
  nodes can become guards.
- **Bandwidth.** Node *i* has measured weight `1000 * (i % 10 + 1)`, so each
  decade runs 1000, 2000, … 10000. No flag-based bandwidth weights are voted.
- **Consensus.** Method 34. The lifetime starts at the current `SystemTime`,
  is fresh for half a day and valid for one day, so nothing here depends on a
  clock beyond "now". The one voted parameter is `bwweightscale=1`.

Consequences the driver relies on: the complete network has enough paths, so
`PartialNetDir::unwrap_if_sufficient` (§7.2) succeeds on it; and omitting the
microdescriptors of nodes 0–34 leaves it without enough paths, so
`have_enough_paths()` is false and `unwrap_if_sufficient` returns `Err`.

#### The builder types the callback receives

The callback mutates the two builders in `NodeBuilders`, so their setters are
part of this step's in-scope surface even though the builders belong to
`tor-netdoc`. At minimum these must exist and be public:

```rust
// tor_netdoc::doc::netstatus (feature "build_docs")
impl<D: Clone> RouterStatusBuilder<D> {
    pub fn identity(&mut self, identity: RsaIdentity) -> &mut Self;
    pub fn add_or_port(&mut self, addr: SocketAddr) -> &mut Self;
    pub fn protos(&mut self, protos: tor_protover::Protocols) -> &mut Self;
    pub fn set_flags(&mut self, flags: RelayFlags) -> &mut Self;
    pub fn add_flags(&mut self, flags: RelayFlags) -> &mut Self;
    pub fn weight(&mut self, weight: RelayWeight) -> &mut Self;
}

// tor_netdoc::doc::microdesc (feature "build_docs")
impl MicrodescBuilder {
    pub fn ntor_key(&mut self, key: curve25519::PublicKey) -> &mut Self;
    pub fn ed25519_id(&mut self, key: ed25519::Ed25519Identity) -> &mut Self;
    pub fn family(&mut self, family: tor_netdoc::types::family::RelayFamily) -> &mut Self;
    pub fn parse_ipv4_policy(&mut self, policy: &str) -> Result<&mut Self>;
}
```

- Each setter takes `&mut self` and returns `&mut Self`, so calls chain. The
  distinction the names make is real: `add_or_port` **appends** to the address
  list and `add_flags` **unions** its argument into the flags already voted,
  while `identity`, `protos`, `set_flags`, `weight`, `ntor_key`,
  `ed25519_id`, `family` and `parse_ipv4_policy` each **replace** whatever the
  default network already set. `RelayFlags` is the bitflag set from the
  consensus; `RelayFlags::BAD_EXIT` is the one the driver adds, to a node the
  default network already flagged `Exit`, so that node keeps its declared exit
  policy and its `Exit` flag while becoming a bad exit (§7.3). Calling `protos` or `family` from the callback
  therefore overwrites the node's protocol list or family rather than adding to
  it, and that is how the family and dir-cache groups build their directories.
- The two argument types are built from strings, and the driver builds them
  that way: `tor_protover::Protocols` and
  `tor_netdoc::types::family::RelayFamily` both implement `FromStr`. `""`
  parses as the empty protocol list and `"DirCache=2"` as the dir-cache
  advertisement; a `RelayFamily` parses from a space-separated list of
  hex-encoded RSA identities, and the empty string parses as the empty family.

## 8. `tor-rtcompat`

### 8.1 `tor_rtcompat::TcpProvider` and `TcpListener`

```rust
#[async_trait]
pub trait TcpProvider {
    type TcpStream: AsyncRead + AsyncWrite + Send + Sync + Unpin + 'static;
    type TcpListener: TcpListener<TcpStream = Self::TcpStream> + Send + Sync + Unpin + 'static;

    async fn connect(&self, addr: &SocketAddr) -> IoResult<Self::TcpStream>;
    async fn listen(&self, addr: &SocketAddr) -> IoResult<Self::TcpListener>;
}

#[async_trait]
pub trait TcpListener {
    type TcpStream: AsyncRead + AsyncWrite + Send + Sync + Unpin + 'static;
    type Incoming: stream::Stream<Item = IoResult<(Self::TcpStream, SocketAddr)>> + Unpin;

    async fn accept(&self) -> IoResult<(Self::TcpStream, SocketAddr)>;
    fn incoming(self) -> Self::Incoming;
    fn local_addr(&self) -> IoResult<SocketAddr>;
}
```

Observable behaviour.

- `connect` takes exactly one `&SocketAddr`; it must not accept a hostname or
  anything that could trigger a DNS lookup.
- The traits are implementable **outside** this crate: an external crate can
  define its own type, pick its own `TcpStream`/`TcpListener` associated types,
  and be used wherever a `TcpProvider` is expected. Nothing in the trait
  refers to a concrete runtime.
- `TcpListener::local_addr()` reports the address the listener is actually
  bound to, which is how a caller learns the port when it asked for port 0.
- `accept()` takes `&self`, so a listener can be shared.

### 8.2 `tor_rtcompat::TlsProvider`, `tls::TlsConnector`, `tls::CertifiedConn`

```rust
pub trait TlsProvider<S> {
    type Connector: TlsConnector<S, Conn = Self::TlsStream> + Send + Sync + Unpin;
    type TlsStream: AsyncRead + AsyncWrite + CertifiedConn + Unpin + Send + 'static;

    fn tls_connector(&self) -> Self::Connector;
}

#[async_trait]
pub trait TlsConnector<S> {
    type Conn: AsyncRead + AsyncWrite + CertifiedConn + Unpin + Send + 'static;
    async fn negotiate_unvalidated(&self, stream: S, sni_hostname: &str) -> IoResult<Self::Conn>;
}

pub trait CertifiedConn {
    fn peer_certificate(&self) -> IoResult<Option<Vec<u8>>>;
}
```

These are also re-exported as `tor_rtcompat::tls::{TlsConnector,
CertifiedConn}`.

Observable behaviour.

- `TlsProvider` is generic over the underlying stream type `S`. That is the
  change this step requires: a TLS provider is no longer tied to one TCP
  stream type, so one type may implement `TlsProvider<A>` and
  `TlsProvider<B>` with different connectors, and an implementation is
  selected by the stream type at the call site.
- The traits are implementable outside the crate. An external crate can
  provide a `TcpProvider` over in-memory pipes and a `TlsProvider` over those
  pipes, and both must compose with the generic code that consumes them
  without touching a real socket.
- `negotiate_unvalidated(stream, sni_hostname)` consumes the stream and
  declares `sni_hostname` as the desired name but does **not** check the
  certificate against it. Identity is established later, by the channel
  handshake, from the bytes `peer_certificate()` returns, so an arbitrary SNI
  string must be accepted.
- `peer_certificate()` returns `Ok(Some(der))` with the DER bytes the peer
  presented, or `Ok(None)` when there is no certificate. The bytes handed back
  are exactly the bytes the server was configured with.
- The negotiated connection is still an `AsyncRead + AsyncWrite`: data written
  after negotiation arrives at the peer, and a closed write side is observed as
  end-of-stream (a read of zero bytes), not as an error.

### 8.3 `tor_rtcompat::Runtime` and `tor_rtcompat::tokio`

```rust
pub trait Runtime:
    Sync + Send + Spawn + SpawnBlocking + Clone
    + SleepProvider + TcpProvider + TlsProvider<Self::TcpStream> + 'static
{}

impl<T> Runtime for T where T: /* the same bounds */ {}

pub mod tokio {
    // Re-exported from the crate's private `impls::tokio`; it implements
    // `Runtime`, so it is usable wherever a `Runtime` type parameter is.
    pub struct TokioRuntimeHandle { /* private */ }
    impl TokioRuntimeHandle {
        // `tokio_crate` is how `tor-rtcompat` spells its renamed `tokio`
        // dependency; the argument is `tokio::runtime::Handle`.
        pub fn new(handle: tokio_crate::runtime::Handle) -> Self;
    }

    pub fn create_runtime() -> std::io::Result<impl Runtime>;
    pub fn current_runtime() -> std::io::Result<impl Runtime>;
    pub fn test_with_runtime<P, F, O>(func: P) -> O
        where P: FnOnce(async_executors::TokioTp) -> F,
              F: futures::Future<Output = O>;
}
```

Observable behaviour.

- `Runtime` is a blanket-implemented alias: any type meeting the bounds is a
  `Runtime` without an explicit `impl`. Note the TLS bound is
  `TlsProvider<Self::TcpStream>` — a runtime must provide TLS over *its own*
  TCP stream type, which is what makes §8.2's genericity load-bearing.
- **The `Spawn` in that bound list is `futures::task::Spawn`** — the trait
  `futures::task::SpawnExt` is blanket-implemented over — with the one method
  `fn spawn_obj(&self, future: FutureObj<'static, ()>) -> Result<(),
  futures::task::SpawnError>`. It is not a trait this workspace declares: a
  runtime that implements a crate-local `Spawn` instead, however similar its
  shape, is one the driver cannot spawn onto, because its
  `futures::task::SpawnExt::spawn` call does not apply. Every `Runtime` here
  satisfies `futures::task::Spawn`.
- A type that satisfies the bounds can be written outside this crate and used
  as a `Runtime`.
- `test_with_runtime(f)` runs an async closure to completion on a freshly
  built runtime and returns its output; it is the driver's only way to run
  async code.
- The two constructors return **`impl Runtime`**, not a named type:
  `create_runtime()` and `current_runtime()` are both
  `-> std::io::Result<impl Runtime>`. `TokioRuntimeHandle` is a real type and
  is correctly named here — it is what a type position such as
  `Option<&GuardMgr<TokioRuntimeHandle>>` is instantiated with — but it is not
  the return type of either function, and writing it as one narrows the
  contract.
- `test_with_runtime`'s closure parameter is `async_executors::TokioTp`, the
  concrete runtime `create_runtime` builds, not `TokioRuntimeHandle`. Cases
  pass an un-annotated closure (`test_with_runtime(|rt| async move { … })`)
  and let the parameter type be inferred, so any type meeting the `Runtime`
  bounds compiles at the call site — but the declared bound is the one above.
- All three items are the shape this checkpoint's tag has. A later checkpoint
  that reorganises the tokio entry points must say so in its own contract
  rather than leaving this section to be read forward unchanged.

## 9. `tor-rtmock`

```rust
pub mod io {
    pub struct LocalStream { /* private */ }
    pub fn stream_pair() -> (LocalStream, LocalStream);
}

pub mod net {
    pub struct MockNetwork { /* private */ }
    pub struct MockNetProvider { /* private */ }
    pub struct MockNetListener { /* private */ }
    pub struct ProviderBuilder { /* private */ }

    impl MockNetwork {
        pub fn new() -> Arc<Self>;
        pub fn builder(self: &Arc<Self>) -> ProviderBuilder;
    }

    impl ProviderBuilder {
        pub fn add_address(&mut self, addr: IpAddr) -> &mut Self;
        pub fn provider(&self) -> MockNetProvider;
        pub fn runtime<R: Runtime>(&self, runtime: R) -> crate::MockNetRuntime<R>;
    }

    impl MockNetProvider {
        pub fn listen_tls(&self, addr: &SocketAddr, tls_cert: Vec<u8>) -> IoResult<MockNetListener>;
    }

    impl TcpProvider for MockNetProvider { type TcpStream = LocalStream; .. }
    impl TlsProvider<LocalStream> for MockNetProvider { .. }
}

pub struct MockNetRuntime<R: Runtime> { /* private */ }

impl<R: Runtime> MockNetRuntime<R> {
    pub fn new(runtime: R, net: MockNetProvider) -> Self;
    pub fn inner(&self) -> &R;
    pub fn mock_net(&self) -> &MockNetProvider;
}
```

Observable behaviour.

- `stream_pair()` returns two connected in-memory streams; bytes written to one
  are read from the other, and dropping or closing one is seen as
  end-of-stream by the other.
- `MockNetwork::new()` returns an `Arc`, and `builder()` is called on the
  `Arc`. Each `add_address` gives the client being built one address on the
  simulated network; `provider()` yields a `MockNetProvider` and `runtime(rt)`
  wraps a real runtime so that only its network view is replaced.
- `MockNetProvider` implements both `TcpProvider` (with
  `TcpStream = LocalStream`) and `TlsProvider<LocalStream>`, so it is the
  concrete demonstration that §8's traits are satisfiable over a non-socket
  transport.
- `listen_tls(addr, cert)` starts a listener that reports `cert` as its peer
  certificate. A client that connects and calls `negotiate_unvalidated`
  observes exactly those bytes from `peer_certificate()`, for any SNI string.
  No cryptography is performed.
- **The certificate belongs to the client's end of the connection.** The stream
  a client gets back from `connect` to a TLS listener carries that listener's
  certificate, and using it as a plain byte stream is a programming error
  rather than a silent shortcut: reading from or writing to it *before* it has
  been wrapped fails with `ErrorKind::Other`. That is so for the raw stream
  `connect` handed back, not only for streams already passed through the
  connector. The stream the *server* takes from `accept()`, on the other hand,
  is an ordinary cert-less stream — a listener's certificate is a property of
  the connection's client end — so a server that reads it plainly gets the
  bytes, and wrapping it with the connector is the cert-less case below.
- Symmetrically, `negotiate_unvalidated` over a stream that carries no
  certificate fails with `ErrorKind::Other` rather than succeeding with an
  empty certificate. A stream from `stream_pair()`, one accepted by a plain
  (non-TLS) `listen()`, and one accepted by a `listen_tls` listener all take
  this path.
- `MockNetListener` implements the `TcpListener` trait of §8.1 — that is where
  its `accept()`, `local_addr()` and `Incoming` come from; it has no inherent
  methods of its own. `MockNetProvider::listen` (the `TcpProvider` method) and
  `listen_tls` both yield one.
- A listener asked for port 0 gets a concrete port, reported by
  `local_addr()`.
- Connecting to an address nobody is listening on is an error, with
  `ErrorKind::ConnectionRefused`.
- `MockNetRuntime<R>` is itself a `Runtime`: it delegates spawning and sleeping
  to `inner()` and networking to `mock_net()`.

## 10. `tor-cell`

### 10.0 Crate root: the crate's own error type

```rust
// at the root of `tor-cell`
#[derive(Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    BytesErr(#[from] tor_bytes::Error),
    IoErr(#[source] std::sync::Arc<std::io::Error>),
    InternalError(String),
    ChanProto(String),
    BadStreamAddress,
    CantEncode,
}

pub type Result<T> = std::result::Result<T, Error>;
```

This alias is what `crate::Result` names from inside `tor-cell`, and what
`tor_cell::Result` names from outside it. It is **not** the same type as
`tor_bytes::Result` (§11), and §10.2 says which of the two each `relaycell`
entry point returns; read that before writing a bare `Result` anywhere in this
crate.

### 10.1 `tor_cell::chancell`

```rust
pub mod chancell {
    #[derive(Debug)]
    pub struct ChanCell { /* private */ }
    impl ChanCell {
        pub fn new(circid: CircId, msg: msg::ChanMsg) -> Self;
        pub fn circid(&self) -> CircId;
        pub fn msg(&self) -> &msg::ChanMsg;
        pub fn into_circid_and_msg(self) -> (CircId, msg::ChanMsg);
    }

    #[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
    pub struct CircId(u32);
    impl From<u32> for CircId { .. }
    impl From<CircId> for u32 { .. }
    impl CircId { pub fn is_zero(&self) -> bool; }

    // caret_int: PartialEq, Eq, Copy, Clone + Display + Debug
    pub struct ChanCmd(u8);
    impl ChanCmd {
        pub const PADDING: Self;        // 0
        pub const CREATE: Self;         // 1
        pub const CREATED: Self;        // 2
        pub const RELAY: Self;          // 3
        pub const DESTROY: Self;        // 4
        pub const CREATE_FAST: Self;    // 5
        pub const CREATED_FAST: Self;   // 6
        pub const VERSIONS: Self;       // 7
        pub const NETINFO: Self;        // 8
        pub const RELAY_EARLY: Self;    // 9
        pub const CREATE2: Self;        // 10
        pub const CREATED2: Self;       // 11
        pub const PADDING_NEGOTIATE: Self; // 12
        pub const VPADDING: Self;       // 128
        pub const CERTS: Self;          // 129
        pub const AUTH_CHALLENGE: Self; // 130
        pub const AUTHENTICATE: Self;   // 131
        pub const AUTHORIZE: Self;      // 132
        pub fn is_recognized(self) -> bool;
    }
    impl From<u8> for ChanCmd { .. }
    impl From<ChanCmd> for u8 { .. }

    pub mod msg {
        #[derive(Clone, Debug)]
        #[non_exhaustive]
        pub enum ChanMsg { Destroy(Destroy), /* ... */ }
        impl ChanMsg { pub fn cmd(&self) -> super::ChanCmd; }

        #[derive(Clone, Debug)]
        pub struct Destroy { /* private */ }
        impl Destroy {
            pub fn new(reason: DestroyReason) -> Self;
            pub fn reason(&self) -> DestroyReason;
        }

        // caret_int: PartialEq, Eq, Copy, Clone + Display + Debug
        pub struct DestroyReason(u8);
        impl DestroyReason {
            pub const NONE: Self;           // 0
            pub const PROTOCOL: Self;       // 1
            pub const INTERNAL: Self;       // 2
            pub const REQUESTED: Self;      // 3
            pub const HIBERNATING: Self;    // 4
            pub const RESOURCELIMIT: Self;  // 5
            pub const CONNECTFAILED: Self;  // 6
            pub const OR_IDENTITY: Self;    // 7
            pub const CHANNEL_CLOSED: Self; // 8
            pub const FINISHED: Self;       // 9
            pub const TIMEOUT: Self;        // 10
            pub const DESTROYED: Self;      // 11
            pub const NOSUCHSERVICE: Self;  // 12
            pub fn is_recognized(self) -> bool;
            pub fn human_str(&self) -> &'static str;
        }
        impl From<u8> for DestroyReason { .. }
        impl From<DestroyReason> for u8 { .. }
    }
}
```

Observable behaviour.

- The `// caret_int` marker means the type comes from `caret::caret_int!`
  (the `caret` crate is part of this workspace). That macro derives
  `PartialEq`, `Eq`, `Copy` and `Clone`, and writes a `Display` that prints
  the constant's own name — or the bare number when the value is unassigned —
  and a `Debug` of the form `TypeName(NAME)`. `Copy` is load-bearing rather
  than incidental: a case reads a reason out of a cell, converts it to `u8`,
  and then asks the *same* value for `is_recognized()` (which takes `self` by
  value) and `human_str()`. The same applies to `RelayCmd` in §10.2.
- `ChanCmd` and `DestroyReason` are one-byte newtypes over the wire numbers
  written as comments beside the constants above. `u8::from(ChanCmd::RELAY) ==
  3`, and `ChanCmd::from(3) == ChanCmd::RELAY`; the mapping is a bijection on
  the assigned numbers.
- `is_recognized()` is true for every assigned code and false for an
  unassigned one. Codes 0 through 12 inclusive are all assigned destroy
  reasons; 13 and above are not.
- `human_str()` returns a `&'static str`, and its return value is the whole of
  its observable behaviour, so the strings are specified here exactly. They
  are compared character for character, including capitalisation and the
  absence of a trailing period.

  | Reason | Code | `human_str()` |
  | --- | --- | --- |
  | `NONE` | 0 | `No reason` |
  | `PROTOCOL` | 1 | `Protocol violation` |
  | `INTERNAL` | 2 | `Internal error` |
  | `REQUESTED` | 3 | `Client sent a TRUNCATE command` |
  | `HIBERNATING` | 4 | `Relay is hibernating and not accepting requests` |
  | `RESOURCELIMIT` | 5 | `Relay ran out of resources` |
  | `CONNECTFAILED` | 6 | `Couldn't connect to relay` |
  | `OR_IDENTITY` | 7 | `Connected to relay with different OR identity` |
  | `CHANNEL_CLOSED` | 8 | `The OR channels carrying this circuit died` |
  | `FINISHED` | 9 | `Circuit expired for being too dirty or old` |
  | `TIMEOUT` | 10 | `Circuit construction took too long` |
  | `DESTROYED` | 11 | `Circuit was destroyed without client truncate` |
  | `NOSUCHSERVICE` | 12 | `No such onion service` |

  Every other code gives the single string `"Unrecognized reason"`. No two
  assigned reasons share a string, so a thirteen-way fallthrough to one
  description does not satisfy this.
- An unrecognized reason code still round-trips through `From`/`Into`: the
  byte is preserved, not normalised to zero, so a code this version does not
  know can still be propagated.
- `ChanCell::new(id, msg)` preserves both parts:
  `cell.circid()` is the id given, `cell.msg().cmd()` is the message's command,
  and `into_circid_and_msg()` returns them unchanged.

### 10.2 `tor_cell::relaycell`

```rust
pub mod relaycell {
    #[derive(Debug)]
    pub struct RelayCell { /* private */ }
    impl RelayCell {
        pub fn new(streamid: StreamId, msg: msg::RelayMsg) -> Self;
        pub fn cmd(&self) -> RelayCmd;
        pub fn stream_id(&self) -> StreamId;
        pub fn msg(&self) -> &msg::RelayMsg;
        pub fn into_streamid_and_msg(self) -> (StreamId, msg::RelayMsg);
        pub fn encode<R: Rng + CryptoRng>(self, rng: &mut R) -> crate::Result<RawCellBody>;
        pub fn decode(body: RawCellBody) -> tor_bytes::Result<Self>;
        pub fn decode_from_reader(r: &mut tor_bytes::Reader<'_>) -> tor_bytes::Result<Self>;
    }

    #[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
    pub struct StreamId(u16);
    impl From<u16> for StreamId { .. }
    impl From<StreamId> for u16 { .. }
    impl StreamId { pub fn is_zero(&self) -> bool; }
    impl std::fmt::Display for StreamId { .. }

    // caret_int: PartialEq, Eq, Copy, Clone + Display + Debug
    pub struct RelayCmd(u8);
    impl RelayCmd {
        pub const BEGIN: Self;      // 1
        pub const DATA: Self;       // 2
        pub const END: Self;        // 3
        pub const CONNECTED: Self;  // 4
        pub const SENDME: Self;     // 5
        pub const EXTEND: Self;     // 6
        pub const EXTENDED: Self;   // 7
        pub const TRUNCATE: Self;   // 8
        pub const TRUNCATED: Self;  // 9
        pub const DROP: Self;       // 10
        pub const RESOLVE: Self;    // 11
        pub const RESOLVED: Self;   // 12
        pub const BEGIN_DIR: Self;  // 13
        pub const EXTEND2: Self;    // 14
        pub const EXTENDED2: Self;  // 15
        pub fn is_recognized(self) -> bool;
    }
    impl From<u8> for RelayCmd { .. }
    impl From<RelayCmd> for u8 { .. }

    pub mod msg {
        #[derive(Debug, Clone)]
        #[non_exhaustive]
        pub enum RelayMsg {
            Begin(Begin), Data(Data), Sendme(Sendme), Truncated(Truncated), /* ... */
        }
        impl RelayMsg { pub fn cmd(&self) -> super::RelayCmd; }

        // bitflags; `BeginFlags::from(0u32)` is "no flags".
        pub struct BeginFlags { /* private */ }
        impl From<u32> for BeginFlags { .. }

        #[derive(Debug, Clone)]
        pub struct Begin { /* private */ }
        impl Begin {
            pub fn new<F>(addr: &str, port: u16, flags: F) -> crate::Result<Self>
                where F: Into<BeginFlags>;
        }

        #[derive(Debug, Clone)]
        pub struct Data { /* private */ }
        impl AsRef<[u8]> for Data { .. }

        #[derive(Debug, Clone)]
        pub struct Sendme { /* private */ }
        impl Sendme {
            pub fn new_empty() -> Self;
            pub fn new_tag(x: [u8; 20]) -> Self;
            pub fn into_tag(self) -> Option<Vec<u8>>;
        }

        #[derive(Debug, Clone)]
        pub struct Truncated { /* private */ }
        impl Truncated {
            pub fn new(reason: super::super::chancell::msg::DestroyReason) -> Self;
            pub fn reason(self) -> super::super::chancell::msg::DestroyReason;
        }
    }
}
```

`RawCellBody` is `[u8; 509]`.

**Which `Result` is which — `decode` and `encode` do not share an error type.**
`tor-cell`'s `relaycell` module opens with `use tor_bytes::{Error, Result};`,
so inside that module the bare name `Result` is `tor_bytes::Result` and not the
crate's own. The two are genuinely different types and the split is
deliberate, so the signatures above spell both out in full rather than writing
a bare `Result`:

- `RelayCell::encode` returns **`crate::Result<RawCellBody>`**, that is
  `std::result::Result<RawCellBody, tor_cell::Error>`. `tor_cell::Error` is
  the enum `tor-cell` defines for itself and re-exports at the crate root
  (`BytesErr(#[from] tor_bytes::Error)`, `IoErr`, `InternalError`,
  `ChanProto`, `BadStreamAddress`, `CantEncode`), and `tor_cell::Result<T>` is
  its alias `std::result::Result<T, tor_cell::Error>`. Encoding fails for
  reasons that are not parse failures, which is why it needs the wider enum.
  `Begin::new` returns the same `crate::Result`.
- `RelayCell::decode` and `RelayCell::decode_from_reader` return
  **`tor_bytes::Result<Self>`** (§11), that is
  `std::result::Result<Self, tor_bytes::Error>`. The SENDME bullets below name
  `tor_bytes::Error::BadMessage(_)` and `tor_bytes::Error::Truncated`, and a
  caller matches on exactly those. **Returning `tor_cell::Result` from either
  `decode` is not equivalent and does not satisfy this contract**, even though
  `tor_cell::Error` has a `From<tor_bytes::Error>`: a `From` impl converts,
  it does not make the two types interchangeable in a `match`.
- `/app/docs/step-01/TEST_INTERFACE.md` writes these two returns as a bare
  `Result<Self>` under the same shadowing import. That is the *same* type this
  section now spells out, so the two binding documents agree; where a reader
  is unsure which `Result` a bare name refers to inside `tor-cell`, the fully
  qualified spellings here govern.

**Derives.** `RelayCell` is `Debug`; `RelayMsg`, `Begin`, `Data`, `Sendme` and
`Truncated` are each `Debug + Clone`. `Sendme: Clone` in particular is
required, not decorative: `into_tag()` consumes the message, so reading a tag
out of a `&RelayMsg` obtained from `RelayCell::msg()` means cloning the
`Sendme` first. `RelayCell: Debug` is likewise required, because a decode
result is formatted with `{:?}` when it is not the expected variant.

Observable behaviour — layout.

- `encode` produces a 509-byte body laid out as `SPEC_NOTES.md` describes
  under "Streams on a circuit": command at byte 0,
  `recognized` at bytes 1..3, stream id at 3..5 (big-endian), digest at 5..9,
  length at 9..11 (big-endian), payload from byte 11.
- The command field is one byte wide: rewriting byte 0 of an otherwise
  untouched body, and nothing else, makes the same payload decode as a
  different message.
- The stream-id field is two bytes wide and big-endian: every `u16`, including
  `0`, `255`, `256`, `32768` and `u16::MAX`, appears at bytes 3..5 as its
  big-endian encoding and decodes back to the same `StreamId`.
- The length field reports the number of real payload bytes and nothing more;
  the decoder hands back exactly that many.
- `decode` refuses a body whose length field points past the end of the cell,
  rather than truncating it. A body is always 509 bytes; the bytes after the
  payload are padding.
- `stream_id()` is new this step: it reads the cell's stream id without
  consuming the cell, and agrees with the first element of
  `into_streamid_and_msg()`.
- `StreamId::is_zero()` is true exactly for `StreamId::from(0)`, which is the
  circuit-level ("control") stream id described in `SPEC_NOTES.md` under
  "Streams on a circuit".
- `decode_from_reader(&mut Reader)` decodes the same bytes as `decode` when
  given a reader over a 509-byte body.
- `encode` takes an `Rng` because the trailing padding is randomised; the
  header and payload bytes are not. Cases that check bytes therefore either
  seed the RNG or inspect only the header and payload.

Observable behaviour — SENDME (the fix this step makes).

- `Sendme::new_empty().into_tag() == None`; `Sendme::new_tag(t).into_tag() ==
  Some(t.to_vec())` with all twenty bytes preserved.
- An empty SENDME encodes to a relay cell with a length field of 0.
- A tagged SENDME encodes as version byte `1`, a two-byte big-endian tag
  length of `20`, then the twenty tag bytes — a length field of 23 — and
  decodes back to the same tag.
- Version `0` is a recognized version meaning "no tag": such a cell decodes,
  yields `into_tag() == None`, and any bytes after the version byte are
  ignored.
- An **unrecognized** version is an error, not a silent success. A body whose
  version byte is `2` fails to decode with `tor_bytes::Error::BadMessage(_)`.
  The variant is checked; the string inside it is **not** — write whatever
  diagnostic you like. (The predecessor accepted such a cell and dropped the
  tag; that behaviour no longer satisfies this contract.)
- A version-1 SENDME whose declared tag length runs past the end of the relay
  payload fails with `tor_bytes::Error::Truncated`, rather than being
  shortened to whatever fits.

## 11. `tor-bytes`

```rust
pub struct Reader<'a> { /* private */ }
impl<'a> Reader<'a> { pub fn from_slice(slice: &'a [u8]) -> Self; /* ... */ }

#[derive(Error, Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum Error {
    Truncated,
    BadMessage(&'static str),
    /* ... */
}

pub type Result<T> = std::result::Result<T, Error>;
```

`Truncated` means the input ended before a well-formed item did;
`BadMessage(_)` means the bytes were present but not meaningful. Both are
matchable by the driver, so the distinction must be preserved rather than
collapsed into one variant.

## 12. `tor-netdoc`

### 12.1 `tor_netdoc::doc::microdesc`

```rust
pub mod doc {
    pub mod microdesc {
        #[derive(Clone, Debug)]
        pub struct Microdesc { /* private */ }
        impl Microdesc {
            pub fn parse(s: &str) -> Result<Microdesc>;
            pub fn digest(&self) -> &MdDigest;               // [u8; 32]
            pub fn ntor_key(&self) -> &curve25519::PublicKey;
            pub fn ed25519_id(&self) -> &ed25519::Ed25519Identity;
            pub fn ipv4_policy(&self) -> &Arc<PortPolicy>;
            pub fn ipv6_policy(&self) -> &Arc<PortPolicy>;
            pub fn family(&self) -> &RelayFamily;
            pub fn builder() -> MicrodescBuilder;            // feature "build_docs"
        }

        #[derive(Clone, Debug)]
        pub struct AnnotatedMicrodesc { /* private */ }
        impl AnnotatedMicrodesc {
            pub fn md(&self) -> &Microdesc;
            pub fn into_microdesc(self) -> Microdesc;
            pub fn within<'a>(&self, s: &'a str) -> Option<&'a str>;
        }

        pub struct MicrodescReader<'a> { /* private */ }
        impl<'a> MicrodescReader<'a> {
            pub fn new(s: &'a str, allow: &crate::AllowAnnotations) -> Self;
        }
        impl<'a> Iterator for MicrodescReader<'a> {
            type Item = Result<AnnotatedMicrodesc>;
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum AllowAnnotations { AnnotationsAllowed, AnnotationsNotAllowed }
```

Also used: `tor_netdoc::types::policy::PortPolicy::allows_port(&self, u16) ->
bool` and `tor_netdoc::types::family::RelayFamily::{members(&self) -> impl
Iterator<Item = &RsaIdentity>, contains(&self, &RsaIdentity) -> bool}`, both
reachable through `Microdesc`'s accessors.

Two further items the gate cases reach through this section, and which are
stated in the **earlier steps' documents** rather than repeated here — those
documents remain binding (see §2 of `instruction.md`, and `/app/docs/step-01/`,
`/app/docs/step-02/` in the workspace):

- `RelayFamily::contains` is part of the type's `FromStr` contract at step 01
  (`TorVersion`/`RelayFamily` observables), alongside `members()`; a
  `RelayFamily` that parses tokens but cannot answer `contains` fails this gate.
- `Ed25519Identity`'s `Display` is the **unpadded standard base64** of its 32
  bytes — the same string as its serialised form under `serde_json` — and a
  gate case compares it character for character against the identity text in
  `fixtures/microdesc_expect.txt`. The rule is stated in step 02's §2.6; a
  `Display` that pads, or that renders hex, fails here.

Observable behaviour.

- `Microdesc::parse` accepts a **bare** microdescriptor: the document must
  begin with `onion-key`. It does **not** accept a leading annotation such as
  `@last-listed`; a document with one fails with
  `Error::MissingToken("onion-key")`. Annotations are the reader's business,
  not `parse`'s.
- `ntor_key()` returns the 32 bytes that the base64 `ntor-onion-key` line
  decodes to, unchanged.
- `ipv4_policy()` reflects the `p` line and `ipv6_policy()` the `p6` line. A
  descriptor with no `p6` line rejects every IPv6 port.
- `family()` lists the identities named on the `family` line, in the order
  given. A descriptor with no `family` line has an empty family.
- `digest()` is 32 bytes, is stable across repeated parses of the same text,
  and differs between two different descriptors. It is the identifier the
  consensus refers to a microdescriptor by, so it must be computed over the
  descriptor's bytes and not over an in-memory representation.
- Errors are specific, not generic:
  - A required element that is absent anywhere in the document gives
    `Error::MissingToken(kwd)` with the missing keyword — for instance
    `MissingToken("onion-key")` when the onion key block is deleted.
  - A document whose required elements are all present but whose **first**
    item is not `onion-key` gives `Error::WrongStartingToken(kwd, _)` with
    `kwd` the keyword that wrongly came first. Note the ordering: the
    missing-token check runs before the first-item check, so deleting
    `onion-key` gives `MissingToken`, not `WrongStartingToken`.
- `MicrodescReader::new(s, &AllowAnnotations::AnnotationsAllowed)` iterates
  annotated descriptors: a document of two descriptors yields two items, each
  `Ok(AnnotatedMicrodesc)`, and the leading `@last-listed` annotation is
  consumed as part of the first item rather than being reported as an error.
- With `AllowAnnotations::AnnotationsNotAllowed`, the same annotated document
  yields an `Err`.
- `md()` gives the descriptor inside an annotated item, and the descriptors the
  reader yields have the same digests as parsing the bare descriptors
  individually.
- `within(s)` returns the slice of the original string `s` that this item
  occupied, so a caller can recover the exact bytes a descriptor was parsed
  from. The returned slice is a subslice of `s`, begins with the item's first
  keyword, and is `None` for a string the item did not come from.

### 12.2 `tor_netdoc::doc::netstatus::NetParams`

```rust
#[derive(Debug, Clone, Default, Eq, PartialEq)]
pub struct NetParams<T> { /* private */ }

impl<T> NetParams<T> {
    pub fn get<A: AsRef<str>>(&self, v: A) -> Option<&T>;
    pub fn iter(&self) -> impl Iterator<Item = (&String, &T)>;
    pub fn set(&mut self, k: String, v: T);
}

impl<T> std::str::FromStr for NetParams<T>
where
    T: std::str::FromStr,
    T::Err: std::error::Error,
{
    type Err = tor_netdoc::Error;
}
```

A keyword→`i32` map, addressed by consensus keyword (`"circwindow"`,
`"bwweightscale"`). `set` overwrites an existing key; `get` returns `None` for
an absent one. This is the type an override set is expressed in, both for
`PartialNetDir::new`, `NetDir::replace_overridden_parameters` and
`DirMgrConfigBuilder::override_net_params`.

Observable behaviour.

- The way to obtain an empty map from outside the crate is `Default` —
  `NetParams::default()`. There is deliberately no `pub fn new()` in the list
  above: upstream's `new()` is crate-internal, nothing outside the crate calls
  it, and nothing here checks whether you have one.
- `FromStr` is how the driver builds one, so it must be public and it must be
  the generic impl written above — the driver parses into `NetParams<i32>`.
  The syntax is the consensus `params` line's: space-separated `key=value`
  pairs, the value parsed with `T`'s own `FromStr`. Empty fields between
  separators are skipped, so `""` parses to an empty map. A field with no `=`
  is an error, as is a value `T` refuses; both surface as `tor_netdoc::Error`,
  whose variant is out of scope.
- Parsing and `set` agree: `"circwindow=250 bwweightscale=2".parse()` equals a
  default map with those two keys `set`.

## 13. `tor-dirmgr`

```rust
#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash)]
#[non_exhaustive]
pub enum DirEvent { NewConsensus, NewDescriptors }

#[derive(Debug, Clone, Builder, Eq, PartialEq)]
#[builder(build_fn(error = "ConfigBuildError"))]
pub struct DirMgrConfig { /* private */ }

impl DirMgrConfig { pub fn builder() -> DirMgrConfigBuilder; }

impl DirMgrConfigBuilder {
    pub fn cache_path(&mut self, v: impl Into<PathBuf>) -> &mut Self;
    pub fn network_config(&mut self, v: NetworkConfig) -> &mut Self;
    pub fn schedule_config(&mut self, v: DownloadScheduleConfig) -> &mut Self;
    pub fn override_net_params(&mut self, v: netstatus::NetParams<i32>) -> &mut Self;
    pub fn override_net_param(&mut self, param: String, value: i32) -> &mut Self;
    pub fn build(&self) -> Result<DirMgrConfig, ConfigBuildError>;
}
```

Observable behaviour.

- `DirEvent` has exactly these two variants, and they are distinct: a new
  consensus and new descriptors for the current consensus are separate events,
  because a client that has a consensus may still be unable to build circuits
  until descriptors arrive — that is exactly the `netdir_is_sufficient` gate of
  §3.1. `Copy`, `Eq` and `Hash` let a consumer deduplicate them.
- `DirMgrConfigBuilder` requires `cache_path` — building without it fails with
  a `ConfigBuildError` naming the field. The other three sections default.
- `cache_path` accepts anything `Into<PathBuf>` (`&str`, `String`, `PathBuf`).
- `override_net_params` sets the whole override map; `override_net_param(k, v)`
  adds one entry, and repeated calls accumulate rather than replace.
- `Eq` compares all four sections, so two configs differing only in
  `override_net_params` or `schedule_config` are unequal.

## 14. `arti-client`, `tor-linkspec`, `tor-chanmgr`, `tor-persist`

### 14.1 `arti_client::config`

```rust
pub mod config {
    pub use tor_config::ConfigBuildError;

    pub mod circ {
        pub use tor_circmgr::{
            CircMgrConfig, CircMgrConfigBuilder, CircuitTiming, CircuitTimingBuilder,
            PathConfig, PathConfigBuilder, PreemptiveCircuitConfig,
            PreemptiveCircuitConfigBuilder,
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
    #[non_exhaustive]
    pub struct StreamTimeoutConfig { /* private */ }

    impl StreamTimeoutConfig { pub fn builder() -> StreamTimeoutConfigBuilder; }
    impl Default for StreamTimeoutConfig { .. }
    impl From<StreamTimeoutConfig> for StreamTimeoutConfigBuilder { .. }

    #[derive(Clone, Debug, Eq, PartialEq)]   // note: NOT Deserialize
    pub struct TorClientConfig { /* private */ }

    impl TorClientConfig { pub fn builder() -> TorClientConfigBuilder; }
    impl Default for TorClientConfig { .. }

    #[derive(Clone, Default)]
    pub struct TorClientConfigBuilder { /* private */ }

    impl TorClientConfigBuilder {
        pub fn build(&self) -> Result<TorClientConfig, ConfigBuildError>;
        pub fn from_directories<P, Q>(state_dir: P, cache_dir: Q) -> Self
            where P: AsRef<Path>, Q: AsRef<Path>;
        pub fn tor_network(&mut self) -> &mut dir::NetworkConfigBuilder;
        pub fn storage(&mut self) -> &mut StorageConfigBuilder;
        pub fn download_schedule(&mut self) -> &mut dir::DownloadScheduleConfigBuilder;
        pub fn override_net_params(&mut self) -> &mut HashMap<String, i32>;
        pub fn path_rules(&mut self) -> &mut circ::PathConfigBuilder;
        pub fn preemptive_circuits(&mut self) -> &mut circ::PreemptiveCircuitConfigBuilder;
        pub fn circuit_timing(&mut self) -> &mut circ::CircuitTimingBuilder;
        pub fn address_filter(&mut self) -> &mut ClientAddrConfigBuilder;
        pub fn stream_timeouts(&mut self) -> &mut StreamTimeoutConfigBuilder;
    }

    impl From<TorClientConfig> for TorClientConfigBuilder { .. }
}
```

`StreamTimeoutConfigBuilder` setters and defaults: `connect_timeout(Duration)`
= 10s, `resolve_timeout(Duration)` = 10s, `resolve_ptr_timeout(Duration)` =
10s. All three deserialize from humantime strings, the same convention §1.2
states for `prediction_lifetime`: `"30s"`, `"1m"` and `"500ms"` are all
accepted, and a bare integer is not.

`TorClientConfig` itself is deliberately **not** `Deserialize`. The sections
deserialize individually and the whole is assembled through
`TorClientConfigBuilder`; nothing in this step deserializes a whole
`TorClientConfig`, and nothing checks an attempt to.

Observable behaviour.

- The client config is section-shaped, and the builder exposes **one inner
  builder per section** rather than flat setters. Every accessor above returns
  `&mut` to the section's own builder, so a caller reaches a setting by naming
  its section and then the field.
- Two sections are new or newly reachable this step: `preemptive_circuits()`
  (returning `circ::PreemptiveCircuitConfigBuilder`) and `stream_timeouts()`
  (returning `StreamTimeoutConfigBuilder`).
- Every section defaults, so `TorClientConfig::builder().build()` succeeds and
  equals `TorClientConfig::default()`, which equals
  `TorClientConfigBuilder::default().build()`.
- A build error from a section is reported inside that section:
  `ConfigBuildError`'s message names the section (e.g. `preemptive_circuits`,
  `stream_timeouts`) as well as the field.
- `From<TorClientConfig> for TorClientConfigBuilder` round-trips: a config
  taken into a builder and rebuilt equals the original, including a
  non-default preemptive section.
- `config::circ` re-exports the circuit configuration types named in §1, so a
  caller that only depends on `arti-client` can name
  `circ::PreemptiveCircuitConfig` and `circ::PreemptiveCircuitConfigBuilder`.
- Settings reached through different sections are independent: changing a
  stream timeout does not disturb the preemptive settings, and vice versa.

### 14.2 `tor_linkspec`

```rust
pub trait ChanTarget {
    fn addrs(&self) -> &[SocketAddr];
    fn ed_identity(&self) -> &pk::ed25519::Ed25519Identity;
    fn ed_identity_key(&self) -> Option<pk::ed25519::PublicKey> { .. }
    fn rsa_identity(&self) -> &pk::rsa::RsaIdentity;
}

pub trait CircTarget: ChanTarget {
    fn linkspecs(&self) -> Vec<crate::LinkSpec> { /* provided */ }
    fn ntor_onion_key(&self) -> &pk::curve25519::PublicKey;
    fn protovers(&self) -> &tor_protover::Protocols;
}
```

Observable behaviour.

- `tor_netdir::Relay` implements both, so a relay taken from a directory can be
  used directly as a channel target and as a circuit target.
- The identities returned through the traits are the same ones the inherent
  `Relay::id`/`Relay::rsa_id` return.
- `CircTarget: ChanTarget`, so a `CircTarget` value also answers `addrs()`.
- The provided `linkspecs()` is derived from the target's own data: it contains
  the Ed25519 identity, the RSA identity, and one entry per socket address, so
  its length is `2 + addrs().len()`.
- `ntor_onion_key()` is the key from the descriptor's `ntor-onion-key` line and
  matches `Microdesc::ntor_key` for the same relay.

### 14.3 `tor_chanmgr::ChanMgr` and `tor_persist::TestingStateMgr`

```rust
impl<R: Runtime> ChanMgr<R> { pub fn new(runtime: R) -> Self; }

// feature "testing"
impl TestingStateMgr {
    pub fn new() -> Self;
    pub fn new_manager(&self) -> Self;
}
impl StateMgr for TestingStateMgr { .. }
```

`ChanMgr::new` takes the runtime by value and does no I/O.
`TestingStateMgr::new()` is an in-memory `StateMgr`, suitable for constructing
a `CircMgr` or `GuardMgr` without touching the filesystem; it starts with the
lock available.

---

## 15. Requirement groups and the items they use

The three `inherited` groups are **gate** groups: they add no requirement of
their own, but a single failure among them means the step is not satisfied,
whatever else passed. The other twenty groups are **requirement** groups: they
carry the behaviour this checkpoint introduces, plus supporting surface. The
group names are those in `tests/assets/groups.txt`, which is the file the
checker reads.

| Group | Kind | Items |
|---|---|---|
| `preempt_config` | requirement | §1.2 |
| `circmgr_cfg` | requirement | §1.1, §1.3, §1.4 |
| `target_port` | requirement | §2.1, §7.3 (`supports_exit_port_*`), §7.5 |
| `mut_cfg` | requirement | §6.1, §1.2 |
| `reconfig_rules` | requirement | §6.2, §6.3 |
| `circmgr_live` | requirement | §3.1, §1.2, §1.4, §6.2, §6.3, §7.5, §14.3 |
| `family_members` | requirement | §7.1 (`known_family_members`), §7.3, §7.5 |
| `param_override` | requirement | §7.1 (`params`, `replace_overridden_parameters`), §7.2, §7.4, §12.2, §7.5 |
| `guard_restrict` | requirement | §5.1, §5.2, §5.3, §5.4, §5.5, §7.5, §14.3 |
| `exit_family` | requirement | §4.3, §1.1, §3.3, §5.1, §5.3, §7.1, §7.3, §7.5, §14.3 |
| `guard_sufficient` | requirement | §5.1 (`netdir_is_sufficient`), §7.5, §14.3 |
| `tls_generic` | requirement | §8.1, §8.2, §8.3, §9 |
| `mock_tls_net` | requirement | §9, §8.1, §8.2 |
| `destroy_reason` | requirement | §10.1 (`DestroyReason`, `Destroy`), §10.2 (`Truncated`) |
| `relay_streamid` | requirement | §10.2 (`stream_id`, `StreamId`, `decode_from_reader`), §11 |
| `sendme_version` | requirement | §10.2 (`Sendme`), §11 |
| `client_cfg` | requirement | §14.1, §1.2 |
| `dir_events` | requirement | §13, §12.2 |
| `cellfmt` | gate | §10.1, §10.2 |
| `netdoc_md` | gate | §12.1, §7.2, §7.5 |
| `netdir_basics` | gate | §7.1, §7.2, §7.3, §7.5 |
| `circ_paths` | requirement | §4.1, §4.2, §4.3, §1.1, §2.1, §3.2, §3.3, §8.3, §7.5 |
| `family_targets` | requirement | §14.2, §7.3 (`in_same_family`, `in_same_subnet`, `SubnetConfig`), §7.5 |

Two things are worth reading off that table before you start.

The `inherited` rows are gate groups: none of them carries a requirement of its
own. The requirement rows cover behaviour this step is responsible for.

More importantly, **§7.5 appears in eleven rows**: `target_port`, `circmgr_live`, `family_members`,
`param_override`, `guard_restrict`, `exit_family`, `guard_sufficient`,
`netdoc_md`, `netdir_basics`, `circ_paths` and `family_targets` all build their
directory out of the synthetic network. (Two of those eleven, `netdoc_md` and
`netdir_basics`, are gate rows; they are listed
because a wrong synthetic network fails them along with the rest.)
Its composition is specified in §7.5
down to the per-node identities, flags, policies, families and addresses,
because a plausible-but-different synthetic network fails all eleven at once.
It is the single highest-leverage item in this document.

### Items that are declared but that no case drives

The contract above is the surface, not the call list — several items it names
are there because they are part of the public boundary later steps build on,
and this step's groups never touch them. They are listed here so that a reader
does not go looking for a case that does not exist, and so that omitting one is
a visible choice rather than a silent gap. **Implement them; none is optional
merely because no case reaches it.**

- `PathConfig::subnet_config()` and `impl From<PathConfig> for PathConfigBuilder`
  (§1.1).
- `DirInfo`'s `From` impls (§3.2), and the layering rule that `DirInfo::Fallbacks`
  carries `tor_netdir::fallback::FallbackDir` rather than a lookalike: the one
  call site passes an empty slice, so the element type is inferred rather than
  asserted.
- `TorPath::{is_empty, new_fallback_one_hop}` (§4.1).
- `Relay::{rs, ipv6_declared_policy}` (§7.3) — no case reaches these two. The
  accessors beside them are checked: the *effective* `ipv4_policy`/`ipv6_policy`
  are by value and **are** checked (they are the supersession of step 01), and
  `netdir_basics` drives `ipv4_declared_policy` against them for the BadExit
  divergence; only the `_declared_` pair still returns a reference.
- `NetParams::set` (§12.2) — the setter exists for callers; every map in scope is
  built by parsing document text instead.
- `StreamId`'s `Display` (§10.2).
- `tor_rtcompat::tokio::{create_runtime, current_runtime}` (§8.3).
- `TorClientConfigBuilder::from_directories` (§14.1) and
  `TestingStateMgr::new_manager` (§14.3).
- The port-0 rule of §9's mock listener: binding port 0 yields a concrete bound
  port in the mock network, and no case binds port 0.

Two documented rejections are likewise exercised only partially, and the gap is
named rather than left implicit: `#[serde(deny_unknown_fields)]` is stated for
`PathConfig` (§1.1), `CircuitTiming` (§1.3) and `SubnetConfig` (§7.3) but the
unknown-key case in scope drives `PreemptiveCircuitConfig` only; and
`NetParams`'s documented parse error (§12.2) is never fed a malformed field —
every map a case builds parses cleanly. Both are stated requirements; the
implementation is what a reader should follow.
