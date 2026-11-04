# Test interface — step 07 (`07-dns-isolation-and-dialing`)

Everything this step is checked through is named below: the crate, the module
path, the exact public signature the conformance driver calls, and the
behaviour it observes. Nothing outside this document is checked.

The driver is an external crate. It reaches your workspace only through
`pub` items on the paths written here — never through `pub(crate)` items,
`#[cfg(test)]` items, or private modules. Items are grouped by crate; section
13 maps each requirement group to the items it uses.

One documented exception to “nothing hidden”: `tor_rtcompat`'s test scaffolding
(§9.4) is `pub` but `#[doc(hidden)]`, because upstream marks it as “not for
consumption outside Arti”. The driver uses it anyway — through the public
`test_with_one_runtime!` macro, which expands to a call on it — so it must be
`pub` and it must keep its name and signature. It is written out in §9.4 for
that reason. Nothing else `#[doc(hidden)]` is touched.

Two notes on the boundary, because this step has more `pub(crate)` state than
its predecessors and the tests are written around that fact rather than against
it:

- Several configuration structs in this checkpoint (`NetworkConfig`,
  `ApplicationConfig`, `ArtiConfig`) keep their fields `pub(crate)`. An
  out-of-crate caller therefore reaches their contents only through builders and
  accessors. Where that is so, this document names the accessor, and the tests
  observe the value through it. A field you can see in the source but cannot
  name from outside the crate is not part of this interface.
- `DirMgrExtensions` is `#[non_exhaustive]` and lives at
  `tor_dirmgr::config::DirMgrExtensions`, **not** at the crate root. The
  crate-root re-export does not exist at this version.
- Three crate-root re-exports **do** exist and are named by the cases in scope,
  so both paths must resolve:
  - `arti::{ApplicationConfig, ApplicationConfigBuilder, ArtiConfig,
    ArtiConfigBuilder, ProxyConfig, ProxyConfigBuilder, SystemConfig,
    SystemConfigBuilder}` re-export the same items as `arti::cfg::*`. Sections
    11.1 and 11.2 write the `arti::cfg::` path; a test may use either.
  - `arti_client::IsolationToken` re-exports `tor_circmgr::IsolationToken`.
  - `arti_client::isolation` re-exports the whole `tor_circmgr::isolation`
    module, so `arti_client::isolation::IsolationHelper` names the trait in
    §3.2 without a direct `tor-circmgr` dependency.

---

Two cargo features must exist and must compile, because the driver enables them:

| Crate | Feature | Why the driver needs it |
| --- | --- | --- |
| `tor-rtcompat` | `tokio` | exposes `tor_rtcompat::tokio`; this crate's default feature set is empty |
| `tor-rtcompat` | `native-tls` | exposes the native-tls backend |

These are not optional extras. The driver's manifest names them, so cargo
resolves them before a single test runs. A crate that does not DECLARE the
feature - even as an empty entry in its `[features]` table - fails metadata
resolution, and the verifier then fails every group in this step at once,
with no per-group detail to point at the cause.

## 1. `tor-basic-utils` — retry timing and formatting helpers

### 1.1 `tor_basic_utils::retry::RetryDelay`

```rust
#[derive(Clone, Debug)]
pub struct RetryDelay { /* private */ }

impl RetryDelay {
    pub fn from_msec(base_delay_msec: u32) -> Self;
    pub fn from_duration(d: Duration) -> Self;
    pub fn next_delay<R: Rng>(&mut self, rng: &mut R) -> Duration;
}

impl Default for RetryDelay;   // == from_msec(0), i.e. the 1000ms floor
```

**There is no `reset()`.** This document previously listed
`pub fn reset(&mut self)`; no such method exists at this version, and a solution
implementing it is implementing something the checker never calls. To return a
generator to its just-constructed state, construct a new one — `RetryDelay` is
`Clone`, and `from_msec` with the same base reproduces the initial state
exactly. The `DownloadSchedule::schedule()` migration in §2.1 below is the
reason: the schedule hands out a **fresh** generator per call rather than
resetting a shared one.

**The bounds are exact, and they are the in-scope surface.** `MIN_LOW_BOUND` is
`1000` ms and `MAX_LOW_BOUND` is `u32::MAX - 1`. `next_delay` draws from
`[low_bound_ms, max(low_bound_ms + 1, last_delay_ms * 3))` — a half-open range,
so the upper bound is never attainable — where `last_delay_ms` starts at 0 and
is whatever the previous draw returned. The `last_delay_ms * 3` term
**saturates** rather than wrapping: once three times the previous delay would
exceed `u64::MAX` the upper bound stops growing and the draw stays in
`[low_bound_ms, u64::MAX)`. A case in scope drives 40 consecutive draws with a
seeded RNG and checks each against that saturating bound, so a wrapping
multiply — or a widening that overflows in some other way — fails it.

Observable behaviour:

- `RetryDelay` implements the decorrelated-jitter backoff named in
  dir-spec.txt §5.5. It is a *stateful* generator: each call to `next_delay`
  advances the internal bound, so the sequence a caller sees depends on how
  many times it has drawn, not only on the seed.
- The initial low bound is clamped. `from_msec` clamps its argument into
  `[MIN_LOW_BOUND, MAX_LOW_BOUND]`, so `from_msec(0)`, `from_msec(1)` and
  `from_msec(999)` all yield the same generator, and an absurdly large argument
  is capped rather than honoured.
- `from_duration(d)` is equivalent to `from_msec` of `d` in **whole**
  milliseconds, subject to the same clamping: `Duration::new(1, 500_000_000)` is
  1500 ms, and `Duration::from_micros(500)` is 0 ms and therefore clamps up to
  the 1000 ms floor.
- The first draw from a freshly constructed `RetryDelay` is made over a range of
  size one — `[low, low + 1)` — and is therefore **independent of the RNG**: it
  lands exactly on the clamped initial delay. This is the only draw a test may
  assert exactly without fixing a seed, and it is the property the cases in scope
  use.
- Subsequent draws are over a widening range whose upper bound derives from the
  previous delay, so delays are non-decreasing in expectation but not monotonic
  in any single realisation. Tests assert range membership and the recurrence's
  bounds, not specific later values.
- The randomness comes from the **caller's** `Rng`, never from a private
  thread-local generator: two generators driven by identically seeded RNGs
  produce the same sequence, and two runs of the same generator under the same
  seed agree exactly.
- Delays are genuinely jittered: over a long enough run they take more than one
  distinct value and rise above the base delay.

**Scope decision S3 — the `"network is down"` back-off is out of scope at this
step.** The milestone pairs decorrelated-jitter guard retries with a back-off
that stops retrying primary guards while the local network appears to be down.
The decorrelated-jitter **generator** those retries are built on is
`RetryDelay` — `tor-guardmgr` holds one per guard — and it is checked in full
above. The network-down **policy** layered over it is not, because none of it
crosses the crate boundary. `tor_guardmgr`'s record of the condition is a
`pub(crate)` accessor on a private `PendingRequest`, and the interval it waits
(`internet_down_timeout`, 600 s by default, overridable from the consensus
parameter `guard-internet-likely-down-interval`) is a private field of a
private `GuardParams` struct. No `pub` item at this checkpoint reports either,
so no out-of-crate driver can observe the policy without a live network and a
consensus. A solution is still expected to implement it — the milestone asks for
it — but no group passes or fails on it.

### 1.2 `tor_basic_utils::skip_fmt`

```rust
pub fn skip_fmt<T>(_: &T, f: &mut fmt::Formatter<'_>) -> fmt::Result;
```

Observable behaviour:

- **The marker is exactly two ASCII full stops, `..`, and nothing else.** Not
  three, not `<elided>`, not `...`. `skip_fmt` writes that string to the
  formatter and returns `Ok(())`, whatever value it is handed. The cases in scope
  assert the literal string, so the exact marker is part of the interface:

  ```rust
  struct OnlyElided<T>(T);
  impl<T> fmt::Debug for OnlyElided<T> {
      fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result { skip_fmt(&self.0, f) }
  }
  assert_eq!(format!("{:?}", OnlyElided([0u8; 32])), "..");
  ```

- The marker does **not** vary with the formatter's flags. `{:#?}` (the
  alternate form) produces the same two dots, because there is nothing to
  expand.
- The value is never inspected — `skip_fmt` is generic over `T` with no bounds
  at all — so two values of the same type that differ only in an elided field
  produce identical output, and values of wildly different types produce
  identical output too. That is the point: a test that could recover the value
  from the formatted output would be demonstrating a leak.
- It is used as a `#[educe(Debug(method = "skip_fmt"))]` target so that a
  `Debug` impl can omit a field that is large, secret, or not usefully
  printable. A hand-written `Debug` may call it directly for one field and let
  the siblings print normally:
  `"Elided { visible: 7, secret: .. }"`.
- It is exported at the crate root: `tor_basic_utils::skip_fmt`.

### 1.3 `tor_basic_utils::define_accessor_trait`

```rust
#[macro_export]
macro_rules! define_accessor_trait {
    {
        $( #[ $attr:meta ])*
        $vis:vis trait $Trait:ident {
            $( $accessor:ident: $type:ty, )*
        }
    } => { /* see below */ }
}
```

The macro is `#[macro_export]`ed, so it is named as
`tor_basic_utils::define_accessor_trait` and imported with a plain
`use tor_basic_utils::define_accessor_trait;`.

**The invocation grammar is exact and the cases in scope depend on every part of
it.** The body is a `trait` item, each field is `name: Type` and **each field,
including the last, is followed by a comma**:

```rust
use tor_basic_utils::define_accessor_trait;

define_accessor_trait! {
    pub trait Sizes {
        label: String,
        count: usize,
    }
}
```

Observable behaviour:

- The expansion is a trait declaration and nothing else. For the invocation
  above it is equivalent to:

  ```rust
  pub trait Sizes: AsRef<String> + AsRef<usize> {
      fn label(&self) -> &String { AsRef::as_ref(self) }
      fn count(&self) -> &usize  { AsRef::as_ref(self) }
  }
  ```

- **The generated trait has one `AsRef<FieldType>` supertrait per declared
  field**, and each accessor body is `AsRef::as_ref(self)`. Fields are therefore
  selected **by type**, not by name: two fields of the same type in one
  invocation would be indistinguishable. The invocation in scope uses two
  different types for that reason.
- There is **no blanket impl and no derive**. An implementor writes the
  `AsRef`s itself and then an empty `impl Sizes for MyType {}`; the accessor
  bodies come from the trait's default methods. A type with extra fields the
  trait never named implements it just as well as one with exactly those
  fields.
- The accessors genuinely **borrow**: the reference they return is the
  implementor's own field, so `std::ptr::eq(v.label(), &v.label)` holds.
- The generated trait is object safe and usable as a generic bound: `fn
  f<V: Sizes>(v: &V)` and `let erased: &dyn Sizes = &v;` both reach both
  accessors. That is the reason the macro exists — lower-level code takes any
  implementor without naming the concrete struct.
- `$vis` and the leading attributes are passed through, so `pub trait`,
  private `trait` and a doc-commented trait all work.
- The use in scope is the shape of the generated trait — that the getters exist
  with the declared names, return `&FieldType`, and carry the `AsRef`
  supertrait bounds — not the macro's internal expansion.

---

## 2. `tor-dirmgr` — download scheduling and directory configuration

### 2.1 `tor_dirmgr::DownloadSchedule`

```rust
#[derive(Debug, Copy, Clone, Deserialize, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DownloadSchedule { /* private */ }

impl DownloadSchedule {
    pub fn new(attempts: u32, initial_delay: Duration, parallelism: u8) -> Self;
    pub fn attempts(&self) -> impl Iterator<Item = u32>;
    pub fn n_attempts(&self) -> u32;
    pub fn parallelism(&self) -> u8;
    pub fn schedule(&self) -> RetryDelay;
}

impl Default for DownloadSchedule;   // == new(3, Duration::from_millis(1000), 1)
```

Note the derive list: `Deserialize` but **not** `Serialize`, and `Default` is a
**hand-written impl**, not derived — a derived `Default` would produce a
different value (see below).

Observable behaviour:

- `schedule()` returns a `tor_basic_utils::retry::RetryDelay` seeded from this
  schedule's initial delay. This is the step-07 migration: the download backoff
  is no longer a bespoke type but the shared decorrelated-jitter generator, and
  the returned value is a *fresh* generator each call, not a shared one.
- Consequently the first draw from `sched.schedule()` equals the schedule's
  clamped initial delay, by §1.1. `new(3, 10ms, 1).schedule()` first yields
  **1000 ms** (the `RetryDelay` floor, not the 10 ms that was asked for);
  `new(3, 7s, 1).schedule()` first yields 7000 ms.
- **Both counts are clamped up to a minimum of one.** Internally the struct
  holds `NonZeroU32` / `NonZeroU8` and `new` converts with
  `try_into().unwrap_or(1)`, so `new(0, d, 0)` gives `n_attempts() == 1`,
  `attempts().collect::<Vec<_>>() == vec![0]` and `parallelism() == 1`. Neither
  a zero attempt count nor a zero parallelism is representable: a schedule that
  allowed no attempts would silently disable a download rather than retry it.
- `attempts()` yields `0..n_attempts()`, so its length is always
  `n_attempts()` and is therefore **never empty** — the smallest schedule
  yields exactly `[0]`.
- Values above the minimum are reported back verbatim: `new(5, 2s, 4)` gives
  `n_attempts() == 5`, `attempts() == [0,1,2,3,4]`, `parallelism() == 4`.
- `Default` is `new(3, 1000ms, 1)`: three attempts, a one-second initial delay,
  no parallelism.
- `DownloadSchedule` is `Copy` and compares by value: two schedules built from
  equal arguments are equal, and changing any argument makes them unequal.

### 2.2 `tor_dirmgr::{DownloadScheduleConfig, DownloadScheduleConfigBuilder}`

```rust
#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[builder(derive(Deserialize))]
pub struct DownloadScheduleConfig { /* fields are private */ }

impl DownloadScheduleConfig {
    pub fn builder() -> DownloadScheduleConfigBuilder;
}

impl Default for DownloadScheduleConfig;   // == builder().build().unwrap()

impl DownloadScheduleConfigBuilder {
    pub fn retry_bootstrap(&mut self, v: DownloadSchedule) -> &mut Self;
    pub fn retry_consensus(&mut self, v: DownloadSchedule) -> &mut Self;
    pub fn retry_certs(&mut self, v: DownloadSchedule) -> &mut Self;
    pub fn retry_microdescs(&mut self, v: DownloadSchedule) -> &mut Self;
    pub fn build(&self) -> Result<DownloadScheduleConfig, ConfigBuildError>;
}
```

The four setter names above are also the four TOML keys in the
`[download_schedule]` section, and they are the only route to the fields: the
struct's own fields are private and there are no public getters on
`DownloadScheduleConfig` at this version.

Observable behaviour:

- `Default` is a hand-written impl that runs the empty builder, so
  `DownloadScheduleConfig::default()` and
  `DownloadScheduleConfig::builder().build().unwrap()` are the same value by
  construction, and a builder with every field set explicitly to the documented
  defaults compares equal to both. The defaults are: `retry_bootstrap` =
  `DownloadSchedule::new(128, 1s, 1)`; `retry_microdescs` =
  `DownloadSchedule::new(3, 1s, 4)`; `retry_consensus` and `retry_certs` =
  `DownloadSchedule::default()` (that is, `new(3, 1000ms, 1)` — see §2.1).
- Setting any single field to a value other than its default makes the result
  compare unequal to `Default::default()`, so equality is structural rather
  than nominal.
- The builder's `build()` succeeds with no fields set — every field has a
  default, so this config has no required fields.

### 2.3 `tor_dirmgr::{DirMgrConfig, NetworkConfig, NetworkConfigBuilder}` and `tor_dirmgr::config::DirMgrExtensions`

```rust
// NOT a builder type, and NOT `PartialEq`: every field is `pub` and the struct
// is meant to be written as a literal.  Adding a field here is an API break on
// purpose.
#[derive(Debug, Clone)]
pub struct DirMgrConfig {
    pub cache_path: PathBuf,
    pub network_config: NetworkConfig,
    pub schedule_config: DownloadScheduleConfig,
    pub override_net_params: NetParams<i32>,
    pub extensions: DirMgrExtensions,
}

impl DirMgrConfig {
    pub fn cache_path(&self) -> &Path;
    pub fn authorities(&self) -> &[Authority];
    pub fn fallbacks(&self) -> &FallbackList;
    pub fn schedule(&self) -> &DownloadScheduleConfig;
    pub fn override_net_params(&self) -> &NetParams<i32>;
}

#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[builder(build_fn(validate = "Self::validate", error = "ConfigBuildError"))]
#[builder(derive(Deserialize))]
pub struct NetworkConfig { /* fields are pub(crate) */ }

impl NetworkConfig {
    pub fn builder() -> NetworkConfigBuilder;
    pub fn fallback_caches(&self) -> &FallbackList;
}

impl Default for NetworkConfig;   // hand-written: the shipped authorities and
                                  // the shipped fallback caches

impl NetworkConfigBuilder {
    pub fn authorities(&mut self, v: Vec<Authority>) -> &mut Self;
    // note the setter name: the field is `fallbacks`, the setter and the TOML
    // key are both `fallback_caches`
    pub fn fallback_caches(&mut self, v: impl Into<FallbackList>) -> &mut Self;
    pub fn build(&self) -> Result<NetworkConfig, ConfigBuildError>;
}

#[derive(Debug, Clone, Default)]
#[non_exhaustive]
pub struct DirMgrExtensions { /* ... */ }
```

Three shapes here are easy to get wrong and each one costs a group:

- **`DirMgrConfig` has no builder and no `PartialEq`/`Eq`.** It is written as a
  struct literal with all five fields. Its `Default` exists only under
  `#[cfg(test)]` *inside* `tor-dirmgr`, so an out-of-crate caller cannot call
  `DirMgrConfig::default()` and must supply each field — typically
  `NetworkConfig::default()`, `DownloadScheduleConfig::default()`,
  `NetParams::new()` and `DirMgrExtensions::default()`.
- **`NetworkConfig`'s `Default` is hand-written**, not derived, and it must
  exist: it is how a caller obtains the shipped authority and fallback lists
  without a builder.
- **`DirMgrExtensions` is `Debug + Clone + Default` only** — it is *not*
  `PartialEq`, so it cannot be compared, only constructed and moved.

Observable behaviour:

- `DirMgrConfig`'s own fields are `pub`, so the struct is constructible by
  literal from outside the crate. `NetworkConfig`'s fields are **not**: an
  out-of-crate caller reads the authority list only through
  `DirMgrConfig::authorities`, and the fallback list through either
  `DirMgrConfig::fallbacks` or `NetworkConfig::fallback_caches`. Those two
  routes to the fallback list agree in length.
- The shipped defaults are non-empty: `authorities()` and `fallbacks()` on a
  config built from `NetworkConfig::default()` both return non-empty lists. An
  empty default would leave a client unable to bootstrap at all.
- `override_net_params()` on a config built with `NetParams::new()` is empty.
  `NetParams` has no `is_empty`; emptiness is observable as
  `iter().next().is_none()`.
- `schedule()` returns a reference equal to the `schedule_config` field.
- `NetworkConfigBuilder` enforces an **all-or-nothing** override of the
  directory sources. Setting only `authorities` and calling `build()` fails with
  a `ConfigBuildError` whose rendered message names both `authorities` and
  `fallbacks`; setting both succeeds; setting neither succeeds and yields the
  defaults. The rule exists because a client with custom authorities but stock
  fallbacks would silently contact directories the operator meant to exclude.
  Note that setting `authorities` to the **empty** vector still counts as
  setting it — the validator looks at which fields are present, not at their
  contents.
- `DirMgrExtensions` is `#[non_exhaustive]` and `Default`. It is reachable at
  `tor_dirmgr::config::DirMgrExtensions` only.

### 2.4 `tor_dirmgr::DirProvider`

```rust
/// A Result as returned by this crate.
pub type Result<T> = std::result::Result<T, tor_dirmgr::Error>;

#[async_trait]                          // async_trait 0.1, NOT native AFIT
pub trait DirProvider: NetDirProvider {
    fn reconfigure(
        &self,
        new_config: &DirMgrConfig,
        how: tor_config::Reconfigure,
    ) -> std::result::Result<(), tor_config::ReconfigureError>;

    async fn bootstrap(&self) -> Result<()>;   // i.e. tor_dirmgr::Result<()>

    fn bootstrap_events(&self) -> BoxStream<'static, DirBootstrapStatus>;
}
```

**The `#[async_trait]` attribute is load-bearing and is part of the interface.**
A native `async fn` in trait (stable since rustc 1.75) and an
`#[async_trait]`-desugared trait are *not* interchangeable across a crate
boundary: the macro rewrites `async fn bootstrap` into
`fn bootstrap<'a>(&'a self) -> Pin<Box<dyn Future<Output = Result<()>> + Send + 'a>>`,
and an out-of-crate `#[async_trait] impl DirProvider for MyType` only compiles
against a trait declared the same way. The cases in scope write

```rust
#[async_trait]
impl DirProvider for StubDirProvider {
    fn reconfigure(&self, _: &DirMgrConfig, _: Reconfigure)
        -> std::result::Result<(), tor_config::ReconfigureError> { Ok(()) }
    async fn bootstrap(&self) -> tor_dirmgr::Result<()> { Ok(()) }
    fn bootstrap_events(&self) -> BoxStream<'static, DirBootstrapStatus> {
        Box::pin(futures::stream::empty())
    }
}
```

so `tor-dirmgr` must depend on `async-trait` and apply it here. (Compare §9.1,
where `UdpProvider` and `UdpSocket` carry the same attribute for the same
reason.)

Note also the two different `Result`s in the trait: `reconfigure` returns
`std::result::Result<(), ReconfigureError>` while `bootstrap` returns the
crate's own one-parameter alias `tor_dirmgr::Result<()>` =
`std::result::Result<(), tor_dirmgr::Error>`. The alias must be `pub` at the
crate root, because an out-of-crate implementor writes it.

Observable behaviour:

- `DirProvider` is a **supertrait extension** of `tor_netdir::NetDirProvider`:
  implementing `DirProvider` requires implementing `latest_netdir` and `events`
  too, and any `DirProvider` is usable wherever a `NetDirProvider` is wanted.
- The trait is implementable from outside the crate. The cases in scope define
  their own minimal implementor, which is the check that no private supertrait
  or sealed marker blocks it.
- The trait is **object safe**: `Arc<dyn DirProvider + Send + Sync>` is a
  legal type and the `NetDirProvider` methods are callable through it. This is
  what "replaceable `DirProvider`" means at this checkpoint — `arti-client`
  holds the directory manager behind a trait object rather than as a concrete
  `DirMgr`.
- `reconfigure` takes a `Reconfigure` mode, so a caller can ask whether a change
  *would* be accepted without applying it. Returning `Err(ReconfigureError)`
  must leave the provider unchanged.
- `bootstrap_events()` returns an owned `'static` stream, so a caller may hold
  it after dropping its borrow of the provider. The same terminated-versus-idle
  distinction §7.1 states for `NetDirProvider::events` applies here: a provider
  with nothing further to report **ends** its stream (the next poll resolves to
  `None`) rather than leaving the caller pending forever.

---

## 3. `tor-circmgr` — stream isolation

### 3.1 `tor_circmgr::isolation::Isolation`

```rust
pub trait Isolation:
    seal::Sealed + Downcast + DynClone + Debug + Send + Sync + 'static
{
    fn compatible(&self, other: &dyn Isolation) -> bool;
    fn join(&self, other: &dyn Isolation) -> Option<Box<dyn Isolation>>;
}
```

Observable behaviour:

- `Isolation` is **sealed**. It cannot be implemented outside `tor-circmgr`;
  the supported route to a new isolation type is `IsolationHelper` (§3.2).
  A test that tried to implement `Isolation` directly would not compile, and
  the cases in scope treat the sealing as part of the interface rather than an
  obstacle to work around.
- `compatible` is the predicate that decides circuit sharing: two streams may
  share a circuit if and only if their isolations are compatible. The relation
  is required to be **reflexive** (`a.compatible(a)` is true) and **symmetric**
  (`a.compatible(b) == b.compatible(a)`). Both are checked.
- `compatible` across *different* concrete types is false. Two isolation values
  of unrelated types are never compatible, which is what makes `Box<dyn
  Isolation>` safe to compare without knowing the types involved.
- `join` returns `Some` exactly when the two isolations are compatible, and the
  joined value is compatible with both operands. `join` is consistent with
  `compatible`: `a.join(b).is_some() == a.compatible(b)`.
- The trait is `Downcast` and `DynClone`, so `Box<dyn Isolation>` is cloneable
  and a concrete type can be recovered from it.

### 3.2 `tor_circmgr::isolation::IsolationHelper`

```rust
pub trait IsolationHelper: Sized {
    fn compatible_same_type(&self, other: &Self) -> bool;
    fn join_same_type(&self, other: &Self) -> Option<Self>;
}

// The two blanket impls that make it useful.  Both are required.
impl<T: IsolationHelper + Clone + Debug + Send + Sync + 'static> Isolation for T { /* ... */ }
impl<T: Isolation> From<T> for Box<dyn Isolation> { /* ... */ }

// And the tuple impls, new at this checkpoint.
impl<A: IsolationHelper> IsolationHelper for (A,) { /* ... */ }
impl<A: IsolationHelper, B: IsolationHelper> IsolationHelper for (A, B) { /* ... */ }
// ... and so on, for every tuple arity from 1 through 12.
```

Observable behaviour:

- This is the same-type half of `Isolation`, and the part a downstream crate
  actually writes. `IsolationHelper` is **not** sealed: a caller outside
  `tor-circmgr` implements it for its own type and gets `Isolation` from the
  blanket impl. The extra bounds on that blanket impl (`Clone + Debug + Send +
  Sync + 'static`) are part of the interface, because they are what a caller's
  type must satisfy; `#[derive(Clone, Debug)]` on a plain data type is enough.
- The blanket machinery lifts the same-type half to the `dyn`-safe form:
  a type implementing `IsolationHelper` obtains `Isolation`'s cross-type
  behaviour (incompatibility with foreign types) for free. Two values of
  *different* implementing types are never compatible and never join, in either
  direction, even when each rule on its own is maximally permissive.
- `impl<T: Isolation> From<T> for Box<dyn Isolation>` means a caller writes
  `let boxed: Box<dyn Isolation> = my_rule.into();` and passes it wherever a
  boxed isolation is wanted. It is also what makes `StreamPrefs::set_isolation`
  (§10.1) generic over `Into<Box<dyn Isolation>>` usable with a caller's own
  type directly.
- The same reflexivity and symmetry requirements apply, now stated over `Self`.
- `join_same_type` returns `Some` exactly when `compatible_same_type` is true,
  and the result is compatible with both operands. `join` is an **intersection**
  in general, not an equality test: a rule whose values are ranges may return a
  narrower range than either operand, and the joined value must then be
  compatible with exactly the values both operands accepted.

**Tuples of isolation rules.** `IsolationHelper` is implemented for tuples of
implementors, for every arity from **1 through 12**. This is how a caller
composes independent rules without inventing a combined type, and the checked
cases use arities 2 and 3.

- A tuple is compatible with another tuple of the same type **iff every
  component is compatible with its counterpart** — the conjunction, evaluated
  componentwise and positionally.
- `join_same_type` joins **componentwise** and is `None` if *any* component
  fails to join; otherwise it is the tuple of the component joins. It is not
  enough for some components to agree.
- Tuples of **different arity are different types**, so — by the cross-type
  rule above — a 2-tuple and a 3-tuple are never compatible, even when their
  leading components would be.
- The components may be of different types, and may themselves be library types
  (`IsolationToken`, §3.3) or caller-defined ones, mixed freely.

### 3.3 `tor_circmgr::isolation::IsolationToken`

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct IsolationToken(/* private u64 */);

impl IsolationToken {
    pub fn new() -> Self;
    pub fn no_isolation() -> Self;
}
```

**`PartialOrd` and `Ord` are part of the derive list**, not optional extras.
The token wraps a `u64` drawn from a monotonically increasing process-global
counter, so the ordering is meaningful — a token allocated later compares
greater — and the cases in scope rely on it: they assert `a < b` for two
successive `new()` calls, sort a `Vec<IsolationToken>`, and call `.cmp()`.
A solution that rewrites the struct from an incomplete derive list does not
compile the tests.

Observable behaviour:

- `new()` returns a token **unequal to every other token `new()` has ever
  returned**, in this process, across threads. It draws from a process-global
  counter, so two calls in any order yield distinct tokens.
- `no_isolation()` returns a token **equal to every other `no_isolation()`
  token** and distinct from every `new()` token. It is the "share freely"
  value.
- Compatibility is equality: two `IsolationToken`s are compatible iff they are
  equal. So `new()` tokens isolate from each other and from `no_isolation()`,
  while `no_isolation()` tokens all share.
- `join` of two equal tokens is that token; of two unequal tokens, `None`.
- `IsolationToken` is `Copy`, so passing one does not move it.

### 3.4 `tor_circmgr::isolation::{StreamIsolation, StreamIsolationBuilder}`

```rust
#[derive(Clone, Debug, derive_builder::Builder)]
pub struct StreamIsolation {
    #[builder(default = "Box::new(IsolationToken::no_isolation())")]
    stream_isolation: Box<dyn Isolation>,
    #[builder(default = "IsolationToken::no_isolation()")]
    owner_token: IsolationToken,
}

impl IsolationHelper for StreamIsolation { /* ... */ }

impl StreamIsolation {
    pub fn no_isolation() -> Self;
    pub fn builder() -> StreamIsolationBuilder;
}

impl StreamIsolationBuilder {
    pub fn new() -> Self;
    pub fn stream_isolation(&mut self, v: Box<dyn Isolation>) -> &mut Self;
    pub fn owner_token(&mut self, v: IsolationToken) -> &mut Self;
    pub fn build(&self) -> Result<StreamIsolation, StreamIsolationBuilderError>;
}
```

Observable behaviour:

- `StreamIsolation` carries **two** independent isolations: the stream's own
  (a `Box<dyn Isolation>`, defaulting to `IsolationToken::no_isolation()`) and
  an owner token contributed by whatever object owns the stream — typically a
  `TorClient`.
- Compatibility is the **conjunction**: two `StreamIsolation`s are compatible
  iff their owner tokens are equal *and* their stream isolations are compatible.
  Differing owner tokens therefore isolate absolutely, whatever the stream
  isolations say. This is what lets two `TorClient`s share a circuit manager
  without sharing circuits.
- `join_same_type` returns `None` when the owner tokens differ, and otherwise
  joins the stream isolations while carrying the common owner token through.
- `no_isolation()` is the fully-permissive value: default stream isolation and
  a `no_isolation()` owner token. Two such values are compatible.
- `builder().build()` succeeds with nothing set, because both fields have
  defaults, and `StreamIsolation::no_isolation()` is defined as exactly that.
- **`StreamIsolation` reaches `Isolation` through `IsolationHelper`** (§3.2),
  like any caller-defined rule — it is not special-cased. So a `StreamIsolation`
  is itself usable as a `Box<dyn Isolation>` via the blanket `From`, and — by
  the cross-type rule — a `StreamIsolation` is never compatible with a bare
  `IsolationToken`, even one it contains.
- Owner tokens are compared with `IsolationToken`'s derived `PartialEq` (§3.3),
  i.e. by the underlying counter value, so two separately-allocated tokens never
  match and a `Clone`/`Copy` of one always does.
- `StreamIsolation` is `Clone + Debug` but **not** `PartialEq`: two values are
  compared through `compatible`/`join`, not `==`.

### 3.5 `tor_circmgr::path::TorPath`

```rust
pub struct TorPath<'a> { /* private */ }

impl<'a> TorPath<'a> {
    pub fn new_one_hop(relay: Relay<'a>) -> Self;
    pub fn new_fallback_one_hop(fallback_dir: &'a FallbackDir) -> Self;
    pub fn new_one_hop_owned<T: tor_linkspec::ChanTarget>(target: &T) -> Self;
    pub fn new_multihop(relays: impl IntoIterator<Item = Relay<'a>>) -> Self;
    pub fn len(&self) -> usize;
}
```

`new_one_hop_owned` is new at this checkpoint and is the constructor the checked
cases use most: it takes **any** `ChanTarget` by reference and copies what it
needs, so it does not borrow from a netdir and has no lifetime tie to its
argument. That is what lets a directory request be described by a one-hop path
built from whatever channel target the caller happened to pick — an
`OwnedChanTarget`, a `FallbackDir`, or the `FirstHop` that `FallbackDir::as_guard`
returns (§5.1).

Observable behaviour:

- `len()` is the number of relays in the path. Every one-hop constructor —
  `new_one_hop`, `new_fallback_one_hop`, `new_one_hop_owned` — yields `len() ==
  1`. `new_multihop` yields the length of the iterator it was given, so
  `new_multihop(Vec::<Relay>::new())` yields `len() == 0`.
- `len()` is **the checked observable**. `TorPath` does have a `Display`, but
  its rendering is out of scope here and no case asserts it: the hop count is the
  part a caller outside the crate can rely on at this version.
- The lifetime parameter distinguishes the borrowing constructors from the
  owning one: `new_fallback_one_hop` borrows the `FallbackDir` for `'a`, while
  `new_one_hop_owned` takes `&T` with no lifetime relationship to `'a` at all.

### 3.6 `tor_circmgr::Error`

```rust
#[derive(Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    #[error("Can't build path for circuit: {0}")]
    NoPath(String),
    #[error("Can't find exit for circuit: {0}")]
    NoExit(String),
    #[error("Unable to select a guard relay: {0}")]
    Guard(#[from] tor_guardmgr::PickGuardError),
    #[error("{0}")]
    RequestFailed(retry_error::RetryError<Box<Error>>),
    #[error("Problem with channel to {peer}")]
    Channel {
        peer: tor_linkspec::OwnedChanTarget,
        #[source] cause: tor_chanmgr::Error,
    },
    #[error("Problem building a circuit with {peer:?}")]
    Protocol {
        peer: Option<tor_linkspec::OwnedChanTarget>,
        #[source] error: tor_proto::Error,
    },
    // ... further variants exist; the enum is `#[non_exhaustive]`
}

impl Error {
    pub fn peers(&self) -> Vec<&tor_linkspec::OwnedChanTarget>;
}
```

`peers()` is new at this checkpoint and is the circuit layer's half of
directory-source tracking (§6.1). Only the variants above are named by checked
cases; the enum carries more, and is `#[non_exhaustive]`, so a match on it from
outside the crate needs a wildcard arm.

Observable behaviour:

- **`peers()` returns the relays to "blame" for the failure, and nothing else.**
  It is `vec![peer]` for `Channel`, `vec![peer]` for `Protocol` when the peer is
  `Some`, the **flattened** concatenation of the nested errors' `peers()` for
  `RequestFailed`, and **empty** for every other variant. A failure that never
  reached a relay — `NoPath`, `NoExit`, `Guard(..)` — blames nobody, which is
  the case the cases in scope check first: an empty list is a meaningful answer,
  not a missing one.
- `RequestFailed` wraps a `retry_error::RetryError<Box<Error>>`, so several
  failed attempts are reported as one error, and `peers()` over it is the
  concatenation of the attempts' own blame lists in order. Two nested
  `NoPath`/`NoExit` errors therefore still flatten to an empty list; two nested
  `Channel` errors flatten to two peers.
- `Error: From<PickGuardError>`, so `tor_circmgr::Error::from(pick_error)` is
  the `Guard` variant. It keeps the underlying error's `ErrorKind` (§5.3) and
  its rendered message contains the underlying message verbatim, because the
  variant's format string is `"Unable to select a guard relay: {0}"`.
- `Channel::peer` is an owned `OwnedChanTarget`, not an option, and is rendered
  through `Display` (§4.1); `Protocol::peer` is an `Option` because a protocol
  error cannot always be attributed to one party, and is rendered through
  `Debug`.

---

## 4. `tor-linkspec` — channel targets

### 4.1 `tor_linkspec::{ChanTarget, OwnedChanTarget}`

```rust
pub trait ChanTarget {
    fn addrs(&self) -> &[SocketAddr];
    fn ed_identity(&self) -> &Ed25519Identity;
    fn ed_identity_key(&self) -> Option<ed25519::PublicKey> { /* provided */ }
    fn rsa_identity(&self) -> &RsaIdentity;
}

#[derive(Debug, Clone)]
pub struct OwnedChanTarget { /* private: addrs, ed_identity, rsa_identity */ }

impl OwnedChanTarget {
    pub fn new(addrs: Vec<SocketAddr>, ed_identity: Ed25519Identity, rsa_identity: RsaIdentity) -> Self;
    pub fn from_chan_target<C>(target: &C) -> Self where C: ChanTarget + ?Sized;
}

impl ChanTarget for OwnedChanTarget { /* ... */ }
impl std::fmt::Display for OwnedChanTarget { /* ... */ }
```

`ChanTarget` at this version is a **flat** trait with three required methods and
one provided one; it is not built out of smaller identity/address traits, and
it has no supertraits. Anything that can name a relay implements those three
methods directly.

`OwnedChanTarget` derives **`Debug` and `Clone` only** — in particular there is
**no `Default`** (there is no sensible default identity) and no `PartialEq`. A
test that wants a second copy clones one, and a test that wants to compare two
targets compares their rendered forms or their accessors.

`from_chan_target` takes `C: ChanTarget + ?Sized`, so it accepts an unsized
`&dyn ChanTarget` as well as a concrete borrowed target.

Observable behaviour:

- `OwnedChanTarget` implements `ChanTarget`, so it is the owned form of any
  borrowed target and round-trips through `from_chan_target`: the copy's
  `addrs()`, `ed_identity()` and `rsa_identity()` equal the original's.
- **`Display` has an exact shape, and the cases in scope assert on it.** It is:
  an opening `[`, then a *single* address field, then the ed25519 identity, then
  a closing `]`. The address field is
  - `?` when the target has **no** addresses,
  - the one address, rendered with its own `Display`, when there is exactly one,
  - the **first** address followed by a `+` when there are two or more.

  So a two-address target renders as `[127.0.0.1:99+<ed25519>]`: the second and
  later addresses are deliberately elided, and `+` is the marker that says so.
- **The RSA identity is never rendered.** `Display` prints the ed25519 identity
  only, with the source comment "short enough to print". A rendering that
  included the RSA identity, or that listed every address, is not this one.
- `ChanTarget` is implemented for `Relay<'_>`, for `FallbackDir` (§5.1) and for
  `FirstHop`, so a relay drawn from a netdir and a fallback directory are both
  usable as channel targets through one interface. This is what makes
  `TorPath::new_one_hop_owned` (§3.5) and `ChanMgr::get_or_launch` (§8.1) accept
  all of them.

---

## 5. `tor-guardmgr` — fallback directories and guard selection errors

### 5.1 `tor_guardmgr::fallback::{FallbackDir, FallbackDirBuilder}`

```rust
#[derive(Debug, Clone, Deserialize, Builder, Eq, PartialEq)]
#[builder(build_fn(validate = "FallbackDirBuilder::validate", error = "ConfigBuildError"))]
#[builder(derive(Deserialize))]
pub struct FallbackDir { /* private: rsa_identity, ed_identity, orports */ }

impl tor_linkspec::ChanTarget for FallbackDir { /* ... */ }

impl FallbackDir {
    pub fn builder() -> FallbackDirBuilder;
    pub fn as_guard(&self) -> crate::FirstHop;
}

impl FallbackDirBuilder {
    pub fn new() -> Self;
    pub fn orport(&mut self, orport: SocketAddr) -> &mut Self;
    pub fn orports(&mut self, value: Vec<SocketAddr>) -> &mut Self;
    pub fn ed_identity(&mut self, id: Ed25519Identity) -> &mut Self;
    pub fn rsa_identity(&mut self, id: RsaIdentity) -> &mut Self;
    pub fn build(&self) -> Result<FallbackDir, ConfigBuildError>;
}

// What `as_guard()` returns.  Both names are at the crate root:
// `tor_guardmgr::FirstHop` and `tor_guardmgr::FirstHopId`.
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct FirstHop { /* private: id, orports */ }

impl tor_linkspec::ChanTarget for FirstHop { /* ... */ }

impl FirstHop {
    pub fn id(&self) -> &FirstHopId;
    // Also `get_relay(&self, netdir: &NetDir) -> Option<Relay<'_>>`, which no
    // group in this step calls.
}

/// A unique cryptographic identifier for a selected guard or fallback.
#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub struct FirstHopId(/* private */);
```

Observable behaviour:

- A `FallbackDir` requires **both** identities and at least one ORPort.
  Omitting any of them makes `build()` fail with a `ConfigBuildError` naming
  the missing field. A fallback with no identity could not be authenticated,
  which is the reason the field is required rather than defaulted.
- `orport` is additive: calling it more than once accumulates addresses rather
  than replacing, so a fallback may have both an IPv4 and an IPv6 ORPort.
- There are **two** ORPort setters and they differ. `orport` is written by hand
  and appends (`self.orports.get_or_insert_with(Vec::new).push(orport)`).
  `orports` is generated by `derive_builder` from the `orports: Vec<SocketAddr>`
  field and **replaces** the whole list. The generated one does not appear as an
  `fn` anywhere in the source, so reading the hand-written `impl` block alone will
  not reveal it; `arti-client`'s own configuration code calls `.orports(vec![..])`.
- Passing an empty list is not the same as never setting one. `orports(vec![])`
  leaves the field `Some([])`, and the builder's `validate` rejects an empty list
  with `ConfigBuildError::Invalid { field: "orport", problem: "list was empty" }`
  - note the field name in that error is the SINGULAR `orport` even when the
  plural setter caused it.
- The two names are not interchangeable, and each is checked in the error it
  belongs to. A `FallbackDir` built without ever setting an ORPort at all is a
  **`MissingField`**, and the field it names is the field: the plural
  **`"orports"`**. So `Invalid` carries `"orport"` and `MissingField` carries
  `"orports"`, and swapping them fails a case in scope.
- `as_guard()` produces a `FirstHop` carrying the same identities and
  addresses, which is how a fallback is used as the first hop of a one-hop
  directory circuit. `FirstHop` is `#[derive(Debug, Clone, Eq, PartialEq)]` and
  itself implements `ChanTarget` (§4.1), so the result can be handed straight to
  `TorPath::new_one_hop_owned` or to `ChanMgr::get_or_launch`.
- **A `FirstHop` also carries an identity object, and `id()` is the checked route
  to it.** `FirstHop::id(&self) -> &FirstHopId` returns a *borrow*; there is no
  `into_id`, no public field, and `FirstHopId` is not `Copy`. `FirstHopId` is
  opaque — its one field is private, so from outside `tor-guardmgr` it can be
  neither constructed nor destructured — and it derives
  `Clone, Debug, Eq, PartialEq, Ord, PartialOrd`. Of those, **`Debug` and
  `PartialEq` are load bearing**: the cases in scope compare two `id()`s with
  `assert_eq!` and `assert_ne!`, which need both. An implementation that omitted
  `id()`, returned an owned value instead of a borrow, or left `FirstHopId`
  without `Debug` does not compile against the driver.
- The identity is computed from the fallback's **two cryptographic identities
  only**, never from its ORPorts. So `as_guard()` is deterministic in the
  identities — calling it twice on one `FallbackDir` yields two `FirstHop`s
  whose `id()`s are equal — and two fallbacks with different ed25519/RSA
  identities yield unequal `id()`s. That is how a client tells whether it has
  already tried a relay. (Internally `FirstHopId` also records whether the hop
  came from a guard or from a fallback, but nothing in this step observes that
  distinction.)
- `FallbackDir` also implements `ChanTarget` directly, so it can be used as a
  channel target without going through `as_guard()`.
- Note that `FallbackDir` does **not** carry `#[serde(deny_unknown_fields)]`,
  unlike the configuration structs of §2.2 and §2.3: the fallback format is
  deliberately forward-compatible, so an unknown key in a fallback entry is
  ignored rather than rejected.

Where the type is reachable from. `tor-guardmgr` is the **defining** crate at
this checkpoint — the type moved here from `tor-dirmgr` — and two other crates
re-export it so that existing callers keep compiling:

```rust
// tor-dirmgr/src/lib.rs
pub use tor_guardmgr::fallback::{FallbackDir, FallbackDirBuilder};

// arti-client/src/config.rs
pub mod dir {
    pub use tor_dirmgr::{
        Authority, AuthorityBuilder, DirMgrConfig, DownloadSchedule, DownloadScheduleConfig,
        DownloadScheduleConfigBuilder, FallbackDir, FallbackDirBuilder, NetworkConfig,
        NetworkConfigBuilder,
    };
}
```

These are **`pub use` re-exports, not newtypes**: `tor_guardmgr::fallback::FallbackDir`,
`tor_dirmgr::FallbackDir` and `arti_client::config::dir::FallbackDir` are three
names for one type, and likewise for `FallbackDirBuilder`. A value built under
one name may be passed, by value and without conversion, to a function that
names it under another; a function `fn f(x: tor_dirmgr::FallbackDir) -> tor_guardmgr::fallback::FallbackDir { x }`
must compile. Introducing a wrapper type in either crate, or omitting a
re-export, breaks this.

Note that `tor_dirmgr` re-exports it at the **crate root** (`tor_dirmgr::FallbackDir`),
while `arti_client` places it in the `config::dir` submodule
(`arti_client::config::dir::FallbackDir`) — it is *not* at `arti_client`'s crate
root, nor at `arti_client::config`.

### 5.2 `tor_guardmgr::fallback::FallbackList`

```rust
#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
#[serde(transparent)]
pub struct FallbackList { /* private: fallbacks: Vec<FallbackDir> */ }

impl<T: IntoIterator<Item = FallbackDir>> From<T> for FallbackList { /* ... */ }

impl FallbackList {
    pub fn len(&self) -> usize;
    pub fn is_empty(&self) -> bool;
    pub fn choose<R: rand::Rng>(&self, rng: &mut R) -> Result<&FallbackDir, PickGuardError>;
}
```

Observable behaviour:

- The `From` impl is **generic over `IntoIterator<Item = FallbackDir>`**, not
  just `Vec`. `FallbackList::from(vec![..])` works, and so does
  `FallbackList::from(some_iterator.collect::<Vec<_>>())` or any other owning
  iterator of `FallbackDir`. A narrower `impl From<Vec<FallbackDir>>` would
  silently change which call sites compile.
- Being generic does **not** make an empty literal ambiguous. Writing
  `FallbackList::from(..)` pins `Self = FallbackList`, which selects this impl,
  and the impl's own `Item = FallbackDir` bound then forces the element type: a
  bare `FallbackList::from(Vec::new())` compiles and is exactly
  `FallbackList::from(Vec::<FallbackDir>::new())`. Cases in scope use both
  spellings; the turbofish is for readability, not to satisfy inference.
- Conversion preserves length, and `is_empty()` agrees with `len() == 0`.
  `FallbackList::default()` is the empty list.
- `#[serde(transparent)]` means the list deserializes as a bare **array** of
  fallback entries, with no wrapper key. This is what lets the `fallback_caches`
  TOML key (§2.3) hold a plain array.
- `choose` on a **non-empty** list returns `Ok` with a reference to a member of
  the list. It picks uniformly with `rand`'s `IteratorRandom::choose`, so over
  many draws from a seeded RNG every member is reachable and the choice is not
  pinned to one element.
- `choose` on an **empty** list returns
  `Err(PickGuardError::AllFallbacksDown { retry_at: None })` rather than
  panicking — **`AllFallbacksDown`, not `AllGuardsDown` and not
  `NoCandidatesAvailable`**, and with `retry_at` exactly `None`, since there is
  no future time at which an empty list becomes usable. (The source carries a
  TODO about someday returning `NoCandidatesAvailable` here; at this version it
  does not.) The distinction matters: a client with an empty fallback list is
  misconfigured, and must report that rather than abort.

### 5.3 `tor_guardmgr::PickGuardError`

```rust
#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum PickGuardError {
    #[error("All guards are down")]
    AllGuardsDown { retry_at: Option<Instant> },

    #[error("No running guards were usable for the selected purpose")]
    NoGuardsUsable,

    #[error("All fallback directories are down")]
    AllFallbacksDown { retry_at: Option<Instant> },

    #[error("Tried to pick from an empty list")]
    NoCandidatesAvailable,
}

impl tor_error::HasKind for PickGuardError { /* ... */ }
```

There are **four** variants, and all four are named by cases in scope. The last
two are the fallback-directory half, added alongside `FallbackList::choose`
(§5.2): the guard sampler and the fallback sampler report through one error
type, so the layer above does not have to know which list was consulted.

Observable behaviour:

- The enum is `#[non_exhaustive]`, so a match over it outside the crate needs a
  wildcard arm.
- The variants distinguish *transient* from *structural* failure, which is the
  distinction a caller must act on. `AllGuardsDown` and `AllFallbacksDown` carry
  an optional `retry_at`: when `Some`, it is the earliest instant at which a
  retry could succeed, and a caller should wait rather than reconfigure.
  `NoGuardsUsable` and `NoCandidatesAvailable` carry no time because waiting
  cannot help — the first means everything running was filtered out, the second
  means the list was empty to begin with.
- `Display` renders **exactly** the four messages shown above, with no trailing
  punctuation and no interpolated detail. A case in scope compares the rendering
  to the literal string, so paraphrasing any of them fails.
- `HasKind` maps them in two pairs, and the pairing is by *transience*, not by
  which list was consulted:

  | variant | `kind()` |
  | --- | --- |
  | `AllGuardsDown { .. }` | `ErrorKind::TorAccessFailed` |
  | `AllFallbacksDown { .. }` | `ErrorKind::TorAccessFailed` |
  | `NoGuardsUsable` | `ErrorKind::NoPath` |
  | `NoCandidatesAvailable` | `ErrorKind::NoPath` |

  The kinds are what a caller above the guard layer branches on: "the network
  is unreachable right now" versus "no route can be built from what I have".
- `tor_circmgr::Error: From<PickGuardError>` (§3.6), so a guard-selection
  failure propagates upward as `Error::Guard(..)`, keeping both the message and
  the kind.

---

## 6. `tor-dirclient` — directory responses and their source

### 6.1 `tor_dirclient::{DirResponse, SourceInfo, Error}`

```rust
#[derive(Debug)]
pub struct DirResponse { /* private: status, output, error, source */ }

impl DirResponse {
    pub fn from_body(body: impl AsRef<[u8]>) -> Self;
    pub fn status_code(&self) -> u16;
    pub fn is_partial(&self) -> bool;
    pub fn error(&self) -> Option<&RequestError>;
    pub fn output(&self) -> &[u8];
    pub fn into_output(self) -> Vec<u8>;
    pub fn source(&self) -> Option<&SourceInfo>;
}

#[derive(Debug, Clone)]
pub struct SourceInfo { /* private: circuit, cache_id */ }

impl SourceInfo {
    pub fn unique_circ_id(&self) -> &tor_proto::circuit::UniqId;
    pub fn cache_id(&self) -> &OwnedChanTarget;
}

#[derive(Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    #[error("Error while getting a circuit {0}")]
    CircMgr(#[from] tor_circmgr::Error),

    #[error("Error fetching directory information from {source:?}")]
    RequestFailed {
        source: Option<SourceInfo>,
        #[source] error: RequestError,
    },
}

impl Error {
    pub fn should_retire_circ(&self) -> bool;
    pub fn cache_ids(&self) -> Vec<&OwnedChanTarget>;
}

#[derive(Error, Debug, Clone)]
#[non_exhaustive]
pub enum RequestError {
    #[error("directory timed out")]                          DirTimeout,
    #[error("truncated HTTP headers")]                       TruncatedHeaders,
    #[error("response too long; gave up after {0} bytes")]   ResponseTooLong(usize),
    #[error("Couldn't decode data as UTF-8.")]               Utf8Encoding(#[from] FromUtf8Error),
    #[error("IO error: {0}")]                                IoError(#[source] Arc<std::io::Error>),
    #[error("Protocol error while launching a stream: {0}")] Proto(#[from] tor_proto::Error),
    #[error("Couldn't parse HTTP headers")]                  HttparseError(#[from] httparse::Error),
    #[error("Couldn't create HTTP request")]                 HttpError(#[source] Arc<http::Error>),
    #[error("Unrecognized content encoding: {0:?}")]         ContentEncoding(String),
}

impl RequestError {
    pub fn should_retire_circ(&self) -> bool;
}
```

`DirResponse` derives **`Debug` only** — it is *not* `Clone`, so a case in scope
that needs the bytes twice calls `output()` before `into_output()`, or copies
the slice.

**Constructibility.** `DirResponse::from_body` is the only public constructor;
`DirResponse::new` and `SourceInfo::from_circuit` are both `pub(crate)`. There
is therefore **no way to build a `SourceInfo` from outside the crate at this
version**, and so no way to build the `RequestFailed { source: Some(..), .. }`
case or to call `SourceInfo::unique_circ_id`/`cache_id` on a real value. Those
two accessors are recorded here because they are part of the published surface
and a solution must keep them, but they are **explicitly out of scope for
checking**: every path in scope into `cache_ids()` goes through `CircMgr(..)` or
through `RequestFailed { source: None, .. }`.

Observable behaviour:

- `from_body` builds a synthetic response: status code 200, not partial, no
  error, no source, and `output()` equal to the supplied bytes.
  `into_output()` returns the same bytes `output()` borrows. An empty body is
  fine and yields an empty `output()`, still with status 200.
- `source()` is `None` for a synthetic response. A response that came off a
  circuit carries `Some(SourceInfo)` identifying **which** cache answered and
  **over which circuit** — that pairing is what lets a client blame the right
  cache for a bad document and avoid it next time.
- **`is_partial()` is exactly `error().is_some()`.** The two are not
  independent: at this version "partial" is *defined* as "we stopped early
  because of an error", and a response built by `from_body` is therefore never
  partial. A solution that tracks truncation separately from the error, so that
  the two can disagree, does not match.
- `Error::CircMgr` is a `#[from]` conversion, so a `tor_circmgr::Error` (§3.6)
  reaching the directory client keeps its identity rather than flattening to a
  string, and `?` converts it implicitly. Its `Display` is
  `"Error while getting a circuit "` followed by the inner error's own
  rendering; note the format string has a space before `{0}` and no colon.
- **`cache_ids()` returns the caches to blame, and delegates.** For `CircMgr(e)`
  it is **exactly `e.peers()`** — including the flattening through
  `RequestFailed(RetryError<..>)` described in §3.6, which is how several failed
  circuit attempts come back as several caches. For
  `RequestFailed { source: Some(s), .. }` it is `vec![s.cache_id()]`. For
  `RequestFailed { source: None, .. }` it is **empty**: an error with no known
  source blames nobody. That means a `CircMgr(NoPath(..))` and a
  `RequestFailed { source: None, .. }` both yield an empty list, by two
  different routes, and the cases in scope check both.
- **`should_retire_circ()` is `true` for every value of both types** at this
  version. `RequestError::should_retire_circ` returns `true` unconditionally,
  and `Error::should_retire_circ` returns `true` for `CircMgr` and delegates for
  `RequestFailed`. The source carries a TODO saying this is too aggressive; the
  cases in scope assert the current behaviour, so a "smarter" implementation that
  returns `false` for, say, `DirTimeout` does not match. The method exists so
  that the decision has **one** place to live when it is refined.
- `HasKind` for `Error` delegates in both arms — `CircMgr(e)` to `e.kind()`,
  `RequestFailed { error, .. }` to `error.kind()` — so the directory layer adds
  no kind of its own. `RequestError::DirTimeout` is
  `ErrorKind::TorNetworkTimeout`; the malformed-response variants
  (`TruncatedHeaders`, `ResponseTooLong`, `Utf8Encoding`, `HttparseError`,
  `ContentEncoding`) are `ErrorKind::TorProtocolViolation`; `IoError` is
  `ErrorKind::TorDirectoryError`; `HttpError` is `ErrorKind::Internal`, because
  failing to *construct* our own request is our bug, not the cache's.
- Both enums are `#[non_exhaustive]`.

---

## 7. `tor-netdir` — publishing directory state

### 7.1 `tor_netdir::{NetDirProvider, DirEvent}`

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum DirEvent {
    NewConsensus,
    NewDescriptors,
}

pub trait NetDirProvider {
    fn latest_netdir(&self) -> Option<Arc<NetDir>>;
    fn events(&self) -> BoxStream<'static, DirEvent>;
}

impl<T: NetDirProvider> NetDirProvider for Arc<T> { /* ... */ }
```

Observable behaviour:

- The trait is implementable from outside the crate; the cases in scope define a
  minimal provider and use it.
- `latest_netdir()` returns `None` before any directory is available. That is a
  normal state during bootstrap, not an error, and callers must handle it.
- `events()` returns an owned `'static` stream. Events may be **coalesced**:
  when the stream yields an event, a consumer may conclude only that the event
  occurred *at least once*, never how many times. A test that counted events
  would be asserting something the interface does not promise.
- A consumer must distinguish two situations that call for opposite responses:
  "no events yet, keep waiting" and "no events ever, stop waiting". A
  **terminated** stream resolves to `None` promptly; an **idle** stream does not
  resolve at all. The cases in scope check both halves — that a finished stream
  ends rather than hanging, and that an idle one does not spuriously yield.
- The blanket impl for `Arc<T>` means an `Arc`-wrapped provider is itself a
  provider, so shared ownership needs no wrapper type.

---

## 8. `tor-chanmgr` — dialing

### 8.1 `tor_chanmgr::{ChanMgr, Error}`

```rust
pub struct ChanMgr<R: Runtime> { /* private */ }

impl<R: Runtime> ChanMgr<R> {
    pub fn new(runtime: R) -> Self;
    pub fn launch_background_tasks(self: &Arc<Self>, runtime: &R) -> Result<Vec<TaskHandle>>;
    pub async fn get_or_launch<T: ChanTarget + ?Sized>(&self, target: &T) -> Result<Channel>;
    pub fn bootstrap_events(&self) -> ConnStatusEvents;
    pub fn expire_channels(&self) -> Duration;
}

#[derive(Debug, Error, Clone)]
#[non_exhaustive]
pub enum Error {
    #[error("Target was unusable: {0}")]
    UnusableTarget(#[source] tor_error::Bug),

    #[error("Pending channel failed to launch")]
    PendingFailed,

    #[error("Channel timed out")]
    ChanTimeout,

    #[error("Protocol error while opening a channel: {0}")]
    Proto(#[from] tor_proto::Error),

    #[error("Network IO error, or TLS error, in {action}, talking to {peer}")]
    Io { peer: SocketAddr, action: &'static str, #[source] source: Arc<std::io::Error> },

    #[error("Channel build failed: [(address, error)] = {addresses:?}")]
    ChannelBuild { addresses: Vec<(SocketAddr, Arc<std::io::Error>)> },

    #[error("unable to spawn {spawning}")]
    Spawn { spawning: &'static str, #[source] cause: Arc<SpawnError> },

    #[error("Internal error: {0}")]
    Internal(#[from] tor_error::Bug),
}
```

`ChanMgr` exposes **no accessor for the number of open channels** at this
version — the channel map is entirely private, and `get_or_launch` is the only
way to observe it. Anything a case in scope asserts about a `ChanMgr` is asserted
through the value or the error that `get_or_launch` returns.

`get_or_launch` takes `T: ChanTarget + ?Sized`, so it accepts an
`OwnedChanTarget`, a `FallbackDir`, a `FirstHop` (§4.1, §5.1), or an unsized
`&dyn ChanTarget`.

Observable behaviour:

- **Two addresses are dialed concurrently, not one after the other.** When a
  target has several addresses, connection attempts are **staggered**: address
  *n+1* starts after a fixed `CONNECTION_DELAY` of **150 ms** rather than
  waiting for address *n* to fail or time out, and the whole set is retried up
  to `N_ATTEMPTS` = **2** times. This is the "happy eyeballs" dialing policy
  (RFC 8305, which is not a Tor specification — see `SPEC_NOTES.md`), and it is
  why a relay with an unreachable IPv6 address is not slow to reach over IPv4.
  The two constants are internal, but their *consequences* are checked: see the
  bullets below for what is actually asserted.
- **A target with no addresses fails immediately as `Error::UnusableTarget(_)`,
  whose `ErrorKind` is `ErrorKind::Internal`.** There is nothing to dial, and
  asking to dial nothing is a caller bug rather than a network condition, so it
  is reported as a `tor_error::Bug`. This is the one dialing failure that is not
  `TorAccessFailed`.
- **A failure to reach any address collects one entry per address.** The error
  is `Error::ChannelBuild { addresses }`, and `addresses` is a
  `Vec<(SocketAddr, Arc<std::io::Error>)>` with **one element per address that
  was attempted**, each paired with the `io::Error` that address produced — not
  a single summary error. It is one entry *per address*, even when several
  addresses failed the same way, and never one entry per internal attempt: a
  two-address target that fails everywhere yields `addresses.len() == 2`, not
  one and not four, however many attempts the dialing policy made. Keeping the
  per-address cause
  is the point: it is what distinguishes "IPv6 is not routed here" from "the
  relay is down".
- `ChannelBuild`'s `Display` is the literal prefix
  `Channel build failed: [(address, error)] = ` followed by the `Debug`
  rendering of the vector. That rendering therefore contains every address's
  textual form, which is what a case in scope looks for.
- **`HasKind` for this enum, in full:**

  | variant | `kind()` |
  | --- | --- |
  | `ChanTimeout` | `ErrorKind::TorAccessFailed` |
  | `Io { .. }` | `ErrorKind::TorAccessFailed` |
  | `PendingFailed` | `ErrorKind::TorAccessFailed` |
  | `ChannelBuild { .. }` | `ErrorKind::TorAccessFailed` |
  | `Proto(tor_proto::Error::ChanIoErr(_))` | `ErrorKind::TorAccessFailed` |
  | `Proto(other)` | delegates to `other.kind()` |
  | `Spawn { cause, .. }` | delegates to `cause.kind()` |
  | `UnusableTarget(_)` | `ErrorKind::Internal` |
  | `Internal(_)` | `ErrorKind::Internal` |

  Note the two delegating arms, and note that `Proto` is **split**: an I/O
  failure surfaced by the protocol layer is still "we could not reach the
  relay", so it is pulled out of the delegation and reported as
  `TorAccessFailed` like the other reachability failures. A caller above the
  channel layer branches on the kind, so "could not connect" and "you asked for
  something impossible" must not collapse into one kind.
- A dialing failure is an `Err`, never a panic and never a hang: every address
  is tried, every attempt is bounded, and the call returns.
- The error enum is `#[non_exhaustive]`, so a match on it outside the crate
  needs a wildcard arm.

**Declared here, not driven by any case.** Three items on this type are in the
contract because a real caller needs them, and no group in this step reaches
them:

- `launch_background_tasks` — returns the `TaskHandle`s the manager spawns. A
  case would have to hold them and observe a scheduled effect; the scheduled
  effect the step *does* check is `expire_channels`' return value, which is a
  constant rather than a task.
- `bootstrap_events` — declared and checked at step 05; this step does not
  re-open the stream.
- `expire_channels`' *effect*. Its 180-second return value and the second
  manager agreeing on it are checked; that the call actually drains an idle
  channel is not, because that needs a channel that has been alive for 180
  seconds.

Implement all three; the milestone is what asks for them.

---

## 9. `tor-rtcompat` — UDP and scheduled tasks

### 9.1 `tor_rtcompat::{UdpProvider, UdpSocket}`

```rust
#[async_trait]
pub trait UdpProvider {
    type UdpSocket: UdpSocket + Send + Sync + Unpin + 'static;
    async fn bind(&self, addr: &SocketAddr) -> IoResult<Self::UdpSocket>;
}

#[async_trait]
pub trait UdpSocket {
    async fn recv(&self, buf: &mut [u8]) -> IoResult<(usize, SocketAddr)>;
    async fn send(&self, buf: &[u8], target: &SocketAddr) -> IoResult<usize>;
    fn local_addr(&self) -> IoResult<SocketAddr>;
}
```

Observable behaviour:

- `Runtime` requires `UdpProvider`, so any runtime can bind a UDP socket. This
  is what makes a DNS port possible: SOCKS cannot carry datagram traffic (see
  `SPEC_NOTES.md`), so the DNS front end needs UDP directly from the runtime.
- Binding to port 0 assigns an ephemeral port, and `local_addr()` reports the
  **assigned** port, not 0. A test can therefore bind two sockets and have them
  talk without hard-coding a port.
- A datagram sent to a bound socket is received with the sender's address, and
  `recv` returns the byte count actually written into the buffer.
- These are traits, not concrete types: the cases in scope reach them through
  `TokioNativeTlsRuntime`.

### 9.2 `tor_rtcompat::scheduler::{TaskSchedule, TaskHandle}`

```rust
pub struct TaskSchedule<R: SleepProvider> { /* private */ }

#[derive(Clone)]
pub struct TaskHandle { /* private: an mpsc sender */ }

impl<R: SleepProvider> TaskSchedule<R> {
    pub fn new(rt: R) -> (Self, TaskHandle);
    pub fn fire_in(&mut self, dur: Duration);
    pub fn fire(&mut self);
}

impl<R: SleepProvider> futures::Stream for TaskSchedule<R> {
    type Item = ();
}

impl TaskHandle {
    pub fn fire(&self) -> bool;
    pub fn fire_at(&self, instant: Instant) -> bool;
    pub fn cancel(&self) -> bool;
}
```

Two shape facts the cases in scope depend on:

- **`TaskHandle` is `Clone`.** One schedule can be driven by several handles
  held in different places, which is why `ChanMgr::launch_background_tasks`
  (§8.1) can hand a `Vec<TaskHandle>` to a caller while keeping the schedules.
- **`Stream::Item` is the unit type `()`**, so a firing carries *no payload*.
  Polling a schedule yields `Some(())`, not `Some(Instant)` and not
  `Some(usize)`: the event is the firing itself. A case in scope compares against
  `Some(Some(()))` after `now_or_never()`, which does not compile against any
  other item type.

Observable behaviour:

- `new` returns a paired schedule and handle. The schedule is a `Stream` that
  yields once per firing; the handle is the remote control.
- A freshly constructed `TaskSchedule` fires **immediately** on first poll, so a
  periodic task runs once at startup rather than waiting out its first interval.
  It fires once, not repeatedly: an immediate second poll is `Pending`.
- **The stream ends when every handle has been dropped.** `poll_next` returns
  `Poll::Ready(None)` as soon as the command channel is closed, which happens
  when the last `TaskHandle` clone goes away. So `drop(hdl)` followed by a poll
  yields `None` — *end of stream*, taking priority over the pending immediate
  firing a fresh schedule would otherwise have produced. While at least one
  clone survives, the stream stays alive. This is the shutdown signal for a
  background task: `while sched.next().await.is_some() { .. }` exits when the
  last controller is gone.
- `fire_in(d)` arranges the next yield after `d` has elapsed on the runtime's
  clock. Under a mock or paced runtime the yield does not occur before the sleep
  completes, which is what makes the schedule testable without real waiting.
- `TaskHandle::fire()` causes an immediate yield even if a later firing was
  pending; `fire_at(t)` replaces any pending firing with one at `t` (and a `t`
  already in the past becomes a zero-length sleep, not an error). `cancel()`
  suppresses a pending firing without ending the stream. All three return
  `false` once the paired `TaskSchedule` has been dropped, and `true` while it
  lives, so a caller can detect that the task it was driving is gone rather than
  silently scheduling into nothing.
- Commands are drained before the timer is consulted on each poll, so a `fire()`
  issued while the schedule was sleeping takes effect at the next poll rather
  than being lost.

### 9.3 `tor_rtcompat::SleepProviderExt`

```rust
pub trait SleepProviderExt: SleepProvider {
    fn timeout<F: Future>(&self, duration: Duration, future: F)
        -> Timeout<F, Self::SleepFuture>;
}
```

Observable behaviour:

- Provides `.timeout(d, fut)`, resolving to `Ok(v)` if the future completed and
  `Err(TimeoutError)` if the duration elapsed first. It is an extension trait
  and must be imported by name; it is not in any prelude.

---

### 9.4 Driving a runtime from a synchronous test

Eight of this step's groups need to run an `async` body from an ordinary
`#[test]` fn. They reach a runtime by three different routes, and every name on
those routes is checked implicitly — if one of them is missing or renamed, the
group does not compile and cannot pass.

| Route | Groups |
| --- | --- |
| `tor_rtcompat::test_with_one_runtime!` | `happyeyeballs`, `dirprovider`, `netdirprov` |
| `PreferredRuntime::create()` + `BlockOn::block_on` | `dormant`, `deferboot`, `resolvedefer` |
| `tokio::TokioNativeTlsRuntime::create()` + `BlockOn::block_on` | `udpruntime`, `taskschedule` |

Of the two gate groups, `deferboot` is a runtime-driving group (row two) and
`errtaxonomy` is not — it checks the taxonomy's plain `Display` and `HasKind`
surface, which is why it needs no async body. The `dormant`, `deferboot` and
`resolvedefer` cases build their client through the shared offline-client
helper, which is where `PreferredRuntime::create()` is called for them.

```rust
// the trait that turns a runtime into a synchronous driver
pub trait BlockOn {
    fn block_on<F: Future>(&self, future: F) -> F::Output;
}

// the default runtime, present only when a TLS feature and an async feature are both on
#[derive(Clone)]
pub struct PreferredRuntime { /* private */ }

impl PreferredRuntime {
    pub fn current() -> std::io::Result<Self>;   // wrap the ambient runtime; panics under tokio if there is none
    pub fn create()  -> std::io::Result<Self>;   // always build a fresh one

    #[doc(hidden)]
    pub fn run_test<P, F, O>(func: P) -> O
    where
        P: FnOnce(Self) -> F,
        F: futures::Future<Output = O>;
}

// the concrete tokio + native-tls runtime
pub mod tokio {
    #[derive(Clone)]
    pub struct TokioNativeTlsRuntime { /* private */ }

    impl TokioNativeTlsRuntime {
        pub fn create()  -> std::io::Result<Self>;
        pub fn current() -> std::io::Result<Self>;
        #[doc(hidden)]
        pub fn run_test<P, F, O>(func: P) -> O where P: FnOnce(Self) -> F, F: futures::Future<Output = O>;
    }
}

#[macro_export]
macro_rules! test_with_one_runtime {
    ( $fn:expr ) => {{ $crate::PreferredRuntime::run_test($fn) }};
}
```

- `PreferredRuntime` and `test_with_one_runtime!` are both behind the same
  `#[cfg(all(any(feature = "native-tls", feature = "rustls"), any(feature = "tokio", feature = "async-std")))]`.
  The driver enables `tokio` and `native-tls` (see the feature table at the top
  of this document), so both are present — but a solution that drops the `cfg`
  or narrows it will make `PreferredRuntime` and the macro vanish together, and
  every one of the seven groups below stops compiling.
- `run_test(f)` is `create()` → `clone()` → `block_on(f(runtime))`. It hands the
  closure an owned runtime, so the usual call shape is
  `tor_rtcompat::test_with_one_runtime!(|rt| async move { .. })`. The macro is
  `#[macro_export]`ed, so it is named from the **crate root**
  (`tor_rtcompat::test_with_one_runtime!`) regardless of the module it is
  written in.
- `BlockOn` is a separate trait from `Runtime`; it must be in scope
  (`use tor_rtcompat::BlockOn;`) before `rt.block_on(..)` resolves. Runtimes are
  `Clone`, and the common pattern is `rt.clone().block_on(async move { .. })`
  so that the body can still use `rt`.
- `PreferredRuntime::create()` builds a runtime unconditionally; `current()`
  wraps the ambient one and **panics** under tokio when there is none. A
  synchronous `#[test]` has no ambient runtime, so tests use `create()`.
- `TokioNativeTlsRuntime` lives at `tor_rtcompat::tokio::TokioNativeTlsRuntime`
  — the `tokio` module is public and named exactly that, and it is what makes
  the `tokio` cargo feature observable.

---

## 10. `arti-client` — stream preferences and client lifecycle

### 10.1 `arti_client::StreamPrefs`

```rust
#[derive(Debug, Default, Clone)]
pub struct StreamPrefs { /* private */ }

impl StreamPrefs {
    pub fn new() -> Self;
    pub fn ipv4_only(&mut self) -> &mut Self;
    pub fn ipv4_preferred(&mut self) -> &mut Self;
    pub fn ipv6_only(&mut self) -> &mut Self;
    pub fn ipv6_preferred(&mut self) -> &mut Self;
    pub fn optimistic(&mut self) -> &mut Self;
    pub fn new_isolation_group(&mut self) -> &mut Self;
    pub fn set_isolation<T: Into<Box<dyn Isolation>>>(&mut self, isolation: T) -> &mut Self;
    pub fn isolate_every_stream(&mut self) -> &mut Self;
}
```

**There is no public `isolation()` accessor.** The method that reads the
isolation back out is **private** to `arti-client` at this version (and returns
an `Option<Box<dyn Isolation>>`, not a bare box). A solution must not expose it,
and a caller cannot ask a `StreamPrefs` for its isolation rule.

That is why the *checked* observation route is the **derived `Debug` rendering**.
`StreamPrefs` is `Debug + Default + Clone`, all three derived, and the isolation
field is an internal three-way enum whose variant names appear in that
rendering. The cases in scope therefore compare `format!("{:?}", prefs)` between
two preference objects, and against a default one, rather than calling an
accessor. A solution that keeps the behaviour but renames the struct's fields or
hand-writes a `Debug` that elides the isolation will not satisfy them.

All the setters take `&mut self` and return `&mut Self`, so they chain by
mutation (`prefs.ipv4_only().optimistic();`) and are **not** consuming builders;
`let p = StreamPrefs::new().ipv4_only();` binds a `&mut` that borrows a
temporary and does not compile.

Observable behaviour:

- The address-family setters are mutually overriding, not cumulative: the last
  one called wins. `ipv4_only()` after `ipv6_preferred()` yields IPv4-only, and
  the default is IPv4-*preferred*.
- **Isolation has exactly three internal states**, and every isolation setter
  replaces the state wholesale rather than combining with it:
  1. *none* — the default. Two default `StreamPrefs` are compatible and may
     share a circuit.
  2. *explicit* — set by `set_isolation(rule)` or by `new_isolation_group()`.
  3. *every stream* — set by `isolate_every_stream()`.
- `new_isolation_group()` installs a **fresh** `IsolationToken` (state 2), so
  two `StreamPrefs` that have each called it are mutually incompatible, and a
  `StreamPrefs` that has called it is incompatible with a default one.
- `set_isolation` is generic over `Into<Box<dyn Isolation>>`, which by the
  blanket `From` impl of §3.2 accepts an `IsolationToken`, a **tuple** of
  isolation rules, or any caller-defined `IsolationHelper` implementor. It
  replaces the isolation wholesale; calling it after `new_isolation_group()`
  discards the generated token, and calling it after `isolate_every_stream()`
  undoes that too.
- `isolate_every_stream()` means *no* connection shares a circuit with any
  other: each use yields a brand-new token, so even two preference objects in
  this state are mutually incompatible, and so is such an object with itself on
  two separate uses. It is idempotent as a *setter* — calling it twice leaves
  the same state — but the state it sets is deliberately not self-compatible.
- `arti_client` re-exports `IsolationToken` and the `isolation` module
  (`pub use tor_circmgr::IsolationToken;` and `pub use tor_circmgr::isolation;`),
  so a caller need not depend on `tor-circmgr` directly. `arti_client::isolation::IsolationHelper`
  is the path the cases in scope use to implement a custom rule.

### 10.2 `arti_client::DormantMode`

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum DormantMode {
    Normal,
    Soft,
}
```

Observable behaviour:

- `Normal` runs background tasks periodically; `Soft` suspends them to conserve
  CPU. `Soft` is not a disconnection: using a **bootstrapped** client wakes it
  back up, so a request issued against a dormant client that has a working
  directory succeeds rather than erroring. A *dormant* client that was never
  bootstrapped still refuses network work for the ordinary reason — its requests
  report `ErrorKind::BootstrapRequired` like any other unbootstrapped client's —
  because dormancy governs the background tasks, not the bootstrap gate. A
  case in scope drives exactly that: a dormant `Manual` client's `connect`,
  `resolve` and `resolve_ptr` all still report `BootstrapRequired`, promptly and
  without hanging.
- **What this step checks is the dormancy API, not the task suspension.** The
  receiver, the idempotence rule and the `DormantMode` values are asserted; that
  the background tasks really stop for `Soft` and really resume for `Normal` is
  a property of `TorClient`'s private task set and no case observes it. Build it
  as described — it is the milestone — but do not expect a case to confirm it.
- `TorClient::set_dormant(&self, mode: DormantMode)` is idempotent — setting the
  same mode twice is not an error and does not disturb a bootstrapped client.
  It is listed with the rest of the client's methods in §10.3.
- **The receiver is `&self`, not `&mut self`, and the return type is `()`.**
  Dormancy is flipped through interior mutability, because `TorClient` is
  `Clone` and every clone shares one state, so a `&mut self` receiver could
  never be obtained from a shared handle in the first place; and setting a mode
  cannot fail, so there is nothing to return and no `Result` to ignore. This is
  not cosmetic: the cases in scope call `set_dormant` on a `let client = …`
  binding declared **without `mut`**, so a `&mut self` signature does not
  compile and forfeits the group.
- The enum is `#[non_exhaustive]` and `Copy`.

### 10.3 `arti_client::{TorClient, BootstrapBehavior}` and `arti_client::config::TorClientConfigBuilder`

```rust
#[derive(Debug, Copy, Clone, PartialEq, Eq, Educe)]
#[educe(Default)]
#[non_exhaustive]
pub enum BootstrapBehavior {
    #[educe(Default)]
    OnDemand,
    Manual,
}
```

The default variant is selected with **`educe`**, not with the standard
library's `#[derive(Default)]` + `#[default]` attribute: deriving `Default` on
an enum was not available at the Rust version this checkpoint targets. What is
observable is only that `BootstrapBehavior::default() == BootstrapBehavior::OnDemand`;
the mechanism is recorded so that a solution reproducing it does not reach for
an attribute that will not compile here. (`DormantMode`, §10.2, has no `Default`
at all.)

`BootstrapBehavior` is `Copy`, so it can be handed to a builder by value and
the local still be used afterwards, and `Debug` renders the bare variant name
(`"Manual"`, `"OnDemand"`).

#### Building a client without touching the network

```rust
impl TorClient<PreferredRuntime> {
    pub fn builder() -> TorClientBuilder<PreferredRuntime>;
}

impl<R: Runtime> TorClient<R> {
    pub fn with_runtime(runtime: R) -> TorClientBuilder<R>;
}

#[derive(Clone)]
#[must_use]
pub struct TorClientBuilder<R: Runtime> { /* private */ }

impl<R: Runtime> TorClientBuilder<R> {
    pub fn config(self, config: TorClientConfig) -> Self;
    pub fn bootstrap_behavior(self, bootstrap_behavior: BootstrapBehavior) -> Self;
    pub fn create_unbootstrapped(self) -> Result<TorClient<R>>;
    pub async fn create_bootstrapped(self) -> Result<TorClient<R>>;
}
```

- The builder methods are **consuming** (`self`, returning `Self`), so they
  chain; they are not `&mut self` setters. `TorClientBuilder::new` is
  `pub(crate)`: a builder is obtained only from `TorClient::builder()` or
  `TorClient::with_runtime(rt)`.
- Both fields default if never set: `TorClientConfig::default()` and
  `BootstrapBehavior::default()` (= `OnDemand`).
- `create_unbootstrapped` performs **no network I/O**. It does construct the
  whole machine — an on-disk state manager rooted at the configured state
  directory, a `ChanMgr`, a `CircMgr`, a directory manager, and the periodic
  background tasks for all of them — but it never dials. That is what makes
  every case in this area usable with no network available (scope decision
  S1). Note the consequence: it **does** touch the filesystem, so the state and
  cache directories in the config must be creatable; a config built by
  `from_directories` over two temporary directories satisfies that.
- `create_bootstrapped` is exactly `create_unbootstrapped()?` followed by
  `bootstrap().await?`, and so is not reachable in this environment.
- `TorClient::builder()` is defined only on `TorClient<PreferredRuntime>` and
  **panics** if no such runtime is already current; `TorClient::with_runtime(rt)`
  is the total function and is what a test should use.
- Errors from the builder are `arti_client::Error`, which implements `HasKind`
  (§12.1).

#### `TorClientConfigBuilder`

```rust
impl TorClientConfigBuilder {
    pub fn from_directories<P: AsRef<Path>, Q: AsRef<Path>>(state_dir: P, cache_dir: Q) -> Self;
}
```

- It lives at `arti_client::config::TorClientConfigBuilder` and is **not**
  re-exported at the crate root at this version; `TorClientConfig` is
  (`arti_client::TorClientConfig`).
- `from_directories` starts from `Self::default()` and sets exactly two
  storage fields, `state_dir` and `cache_dir`, each wrapped in a
  `tor_config::CfgPath`; **every other field keeps its compiled-in default**.
  The argument order is `(state_dir, cache_dir)`.
- The resulting builder `build()`s successfully with nothing else supplied —
  there is no further mandatory field — so naming two temporary directories is
  enough to get a usable `TorClientConfig`.

#### What an unbootstrapped client does

```rust
#[derive(Clone)]
pub struct TorClient<R: Runtime> { /* private */ }

impl<R: Runtime> TorClient<R> {
    pub fn runtime(&self) -> &R;
    pub fn bootstrap_status(&self) -> status::BootstrapStatus;
    pub async fn bootstrap(&self) -> Result<()>;
    #[must_use]
    pub fn isolated_client(&self) -> TorClient<R>;
    pub async fn connect<A: IntoTorAddr>(&self, target: A) -> Result<DataStream>;
    pub async fn resolve(&self, hostname: &str) -> Result<Vec<IpAddr>>;
    pub async fn resolve_ptr(&self, addr: IpAddr) -> Result<Vec<String>>;
    pub fn set_dormant(&self, mode: DormantMode);   // §10.2 — `&self`, returns ()
}
```

- **`TorClient` is `Clone`, and cloning shares the underlying handles.** A clone
  is another reference to the same directory manager, circuit manager and
  status channel — not a second client. `isolated_client()` is a clone whose
  *client isolation token* has been replaced with a fresh `IsolationToken`
  (§3.3); everything else is shared. Consequently a clone and an isolated
  handle both observe the same bootstrap state as the original and give the
  same answer to the same question.
- `runtime()` returns a borrow of the client's own runtime, which is how a
  synchronous test drives the client's futures (`runtime().block_on(fut)`, via
  `tor_rtcompat::BlockOn`) without constructing a second runtime.
- Every operation that needs directory material goes through one internal
  gate. Under `BootstrapBehavior::Manual` that gate does **not** start a
  bootstrap; it takes and releases a lock and then asks for the latest network
  directory, and when there is none it fails with
  `ErrorKind::BootstrapRequired`. So `connect`, `resolve` and `resolve_ptr` all
  return — promptly, not blocking and not hanging — an `arti_client::Error`
  whose `kind()` is `ErrorKind::BootstrapRequired`.
  - The kind is specifically **not** `TorAccessFailed`: nothing was attempted
    over the network, so the failure is “you have not bootstrapped” and not
    “the network is unreachable”.
  - `connect` takes `A: IntoTorAddr`, which is implemented for `&str` of the
    form `"host:port"` and for `(host, port)` tuples. `resolve_ptr` takes a
    `std::net::IpAddr`.
- Under `BootstrapBehavior::OnDemand` the same gate runs the bootstrap instead
  of erroring, so the `BootstrapRequired` behaviour is a property of `Manual`,
  not of `TorClient` in general.

#### `status::BootstrapStatus`

```rust
#[derive(Debug, Clone, Default)]
pub struct BootstrapStatus { /* private */ }

impl BootstrapStatus {
    pub fn as_frac(&self) -> f32;
    pub fn ready_for_traffic(&self) -> bool;
    pub fn blocked(&self) -> Option<Blockage>;
}
```

- `arti_client::status` is a public module; `BootstrapStatus` is **not**
  re-exported at the crate root.
- `as_frac()` is a weighted sum of the connection and directory fractions
  (0.15 and 0.85). For a client that has done nothing both are zero, so a
  freshly created unbootstrapped client reports **exactly `0.0`** — an
  `f32` equality comparison against `0.0` is intended here and is exact.
- `ready_for_traffic()` is the conjunction of “connection status usable” and
  “directory usable now”, so it is `false` for such a client. It is a separate
  question from `as_frac() == 0.0` and both are checked.
- `bootstrap_status()` returns an owned snapshot (a clone out of a watch
  channel), so holding one does not block the client.

---

## 11. `arti` — the binary's configuration and SOCKS glue

### 11.1 `arti::cfg::{ArtiConfig, ArtiConfigBuilder, ProxyConfig, ProxyConfigBuilder}`

```rust
#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[builder(derive(Deserialize))]
pub struct ProxyConfig {
    #[serde(default = "default_socks_port")]
    #[builder(default = "default_socks_port()")]
    pub(crate) socks_port: Option<u16>,

    #[serde(default)]
    #[builder(default)]
    pub(crate) dns_port: Option<u16>,
}

fn default_socks_port() -> Option<u16> { Some(9150) }

// Hand-written, NOT derived: it must go through the builder to pick up
// the per-field defaults above.
impl Default for ProxyConfig {
    fn default() -> Self { Self::builder().build().expect("Default builder failed") }
}

impl ProxyConfig {
    pub fn builder() -> ProxyConfigBuilder;
}
```

Three shape facts, each of which a case in scope depends on:

- **`Default` is hand-written, not derived.** A `#[derive(Default)]` would make
  both fields `None`; the real default is `socks_port == Some(9150)`,
  `dns_port == None`. `ProxyConfig::default()` is therefore *not* the all-`None`
  value, and the cases in scope compare `ProxyConfig::default()` against a builder
  result with `socks_port = Some(9150)` and `dns_port = None`.
- **`#[builder(derive(Deserialize))]`** makes `ProxyConfigBuilder` itself
  deserializable, which is what lets the whole config be parsed into a tree of
  *builders* and then built (see §11.2). Without it, `ArtiConfigBuilder` cannot
  be deserialized at all and `ArtiConfig::try_from(config::Config)` does not
  compile.
- **`#[serde(deny_unknown_fields)]` is on the struct, not the builder.** See the
  note in §11.2 about what that does and does not reject in practice.

There are **no `socks_port()`/`dns_port()` accessor methods** on `ProxyConfig`
at this version; the fields are `pub(crate)`. An out-of-crate caller observes
the values by comparing whole `ProxyConfig` values for equality (the type is
`Eq`), which is exactly what the cases in scope do.

Observable behaviour:

- Both ports are `Option<u16>`, but their **defaults differ**: `socks_port`
  defaults to `Some(9150)` — arti's SOCKS port is on unless turned off — while
  `dns_port` defaults to `None`, so the DNS listener is off unless asked for.
  This asymmetry is the point of the step: adding a DNS port must not turn one
  on by default, and must not turn the SOCKS port off.
- A port of `0` is *not* the same as unset: `Some(0)` asks for an ephemeral
  port, `None` disables the listener. A configuration that conflated them could
  not express "no DNS port".
- `dns_port` is the addition at this checkpoint. Setting it leaves `socks_port`
  at its default and vice versa: a `[proxy]` section that names only `dns_port`
  still yields `socks_port == Some(9150)`.
- `ProxyConfig` round-trips through TOML: a config parsed from a `[proxy]`
  section with `dns_port` set equals one built with `ProxyConfigBuilder`, by
  `Eq`.

**Scope decision S2 — the DNS port's *configuration* is checked; its *answers*
are not.** The milestone is "a DNS port answering A, AAAA and PTR over Tor", and
a solution is expected to serve those queries. What this step checks is the
configuration surface above: that `dns_port` exists, that it is `Option<u16>`,
that it defaults to `None` while `socks_port` defaults to `Some(9150)`, and that
each survives the other being set and a TOML round trip. Resolving an A, AAAA or
PTR query end to end needs a bootstrapped client and a live Tor exit, and no
group in this step reaches the public network: the only sockets any case opens
are loopback UDP sockets (§9.1) and TCP connections to closed `127.0.0.1` ports
(§8.1), and every client-level case fails at the bootstrap gate before anything
is dialled. The halves of that path that *can* be observed offline are checked:
`TorClient::resolve` and `resolve_ptr` exist with the signatures in §10.3 and
report `ErrorKind::BootstrapRequired` before bootstrap, and the
SOCKS-request-to-stream-preference mapping is checked in §11.3.

### 11.2 `arti::{ArtiConfig, ApplicationConfig, SystemConfig}`

```rust
// NOT a derive_builder type: ArtiConfigBuilder is hand-written.
#[derive(Debug, Clone, Eq, PartialEq, Default)]
pub struct ArtiConfig {
    application: ApplicationConfig,   // private
    proxy: ProxyConfig,               // private
    logging: LoggingConfig,           // private
    pub(crate) system: SystemConfig,
    tor: TorClientConfig,             // private
}

impl ArtiConfig {
    pub fn tor_client_config(&self) -> Result<TorClientConfig, ConfigBuildError>;
    pub fn builder() -> ArtiConfigBuilder;
    pub fn application(&self) -> &ApplicationConfig;
    pub fn logging(&self) -> &LoggingConfig;
    pub fn proxy(&self) -> &ProxyConfig;
    // NOTE: there is deliberately NO `system()` accessor here.
}

#[derive(Default, Clone, Deserialize)]
pub struct ArtiConfigBuilder {
    #[serde(flatten)] tor: TorClientConfigBuilder,
    #[serde(default)] application: ApplicationConfigBuilder,
    #[serde(default)] proxy: ProxyConfigBuilder,
    #[serde(default)] logging: LoggingConfigBuilder,
    #[serde(default)] system: SystemConfigBuilder,
}

impl ArtiConfigBuilder {
    pub fn build(&self) -> Result<ArtiConfig, ConfigBuildError>;
    // Every section accessor returns a MUTABLE reference to the section BUILDER.
    pub fn application(&mut self) -> &mut ApplicationConfigBuilder;
    pub fn proxy(&mut self) -> &mut ProxyConfigBuilder;
    pub fn logging(&mut self) -> &mut LoggingConfigBuilder;
    pub fn tor(&mut self) -> &mut TorClientConfigBuilder;
    pub fn system(&mut self) -> &mut SystemConfigBuilder;
}

impl TryFrom<config::Config> for ArtiConfig {
    type Error = config::ConfigError;
    // via: let builder: ArtiConfigBuilder = cfg.try_deserialize()?; builder.build()
}

impl From<ArtiConfigBuilder> for TorClientConfigBuilder { /* returns the `tor` section */ }

#[derive(Deserialize, Debug, Default, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[builder(derive(Deserialize))]
pub struct ApplicationConfig {
    #[serde(default)] #[builder(default)]
    pub(crate) watch_configuration: bool,
}

#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[builder(derive(Deserialize))]
#[non_exhaustive]
pub struct SystemConfig {
    #[builder(setter(into), default = "default_max_files()")]
    #[serde(default = "default_max_files")]
    pub(crate) max_files: u64,
}

fn default_max_files() -> u64 { 16384 }
impl Default for SystemConfig { /* hand-written, via the builder */ }
impl SystemConfig { pub fn builder() -> SystemConfigBuilder; }
```

**The crate root re-exports all of these**, so both paths work and a case in scope
may use either:

```rust
pub use cfg::{
    ApplicationConfig, ApplicationConfigBuilder, ArtiConfig, ArtiConfigBuilder,
    ProxyConfig, ProxyConfigBuilder, SystemConfig, SystemConfigBuilder,
};
```

`arti::ProxyConfig` and `arti::cfg::ProxyConfig` are the same type. The modules
`arti::{cfg, dns, exit, logging, process, socks, watch_cfg}` are all `pub`;
`arti::dns` is new at this checkpoint.

**`ArtiConfigBuilder` is written by hand, not generated.** `ArtiConfig` carries
no `Builder` derive; the builder is a separate struct holding one *builder* per
section, and `build()` builds each section in turn, tagging any failure with
`ConfigBuildError::within("application" | "proxy" | "logging" | "system")` so
the error names which section was at fault.

**The asymmetry around `system` is deliberate and is checked.** `ArtiConfig` has
accessors for `application`, `logging` and `proxy` but **not** for `system`;
`ArtiConfigBuilder` has a mutable accessor for all five sections including
`system` and `tor`. So a case in scope reaches `SystemConfig` by building through
`ArtiConfig::builder().system().max_files(..)` and comparing whole `ArtiConfig`
values, not by reading it back off a built config.

Observable behaviour:

- All fields are private or `pub(crate)`, so an out-of-crate caller reaches the
  sections only through `ArtiConfig::builder()`, the read accessors above, and
  the `TryFrom<config::Config>` conversion. Tests parse TOML through that
  conversion.
- `ArtiConfig::default()` **is** derived, and — because each section's own
  `Default` is the hand-written builder-backed one (§11.1) — it equals
  `ArtiConfig::builder().build().unwrap()`. In particular the default config's
  `proxy()` is `ProxyConfig::default()`, i.e. SOCKS on 9150 and no DNS port.
- `#[serde(flatten)]` on the `tor` field means the Tor client's own sections
  appear at the **top level** of the TOML document, not under a `[tor]` heading,
  while `application`, `proxy`, `logging` and `system` are named sections. A
  `[proxy]` table therefore reaches `ProxyConfigBuilder`, and a `[storage]`
  table (say) flows through to `TorClientConfigBuilder`.
- The sections are **independent**: setting a value in one section leaves the
  others at their defaults. In particular `SystemConfig` is arti-only — it has
  no counterpart in `TorClientConfig`, so setting it cannot perturb the client
  configuration.
- `SystemConfig::max_files` defaults to **16384**, and its builder setter takes
  `impl Into<u64>` (`setter(into)`), so `.max_files(1024_u32)` is accepted.
  `SystemConfig` is `#[non_exhaustive]`, so it cannot be constructed by literal
  from outside the crate — only through its builder or `Default`.
- `ApplicationConfig::watch_configuration` defaults to `false` and is the only
  field in its section.
- At this version an unrecognised key is **silently ignored** rather than
  rejected. `#[serde(deny_unknown_fields)]` is present on `ApplicationConfig`,
  `ProxyConfig` and `SystemConfig` themselves, but the `Deserialize` impl that
  `#[builder(derive(Deserialize))]` generates for the *builder* types does not
  inherit it — and the builder is what a config file is deserialized into. A
  misspelled key is therefore swallowed. This behaviour is **out of scope**: it is
  an acknowledged wart that a later checkpoint changes, so checking it here would
  manufacture an inherited case that a correct future solution must fail. It is
  recorded so that the omission is deliberate and visible rather than an
  oversight.

### 11.3 `arti::socks::stream_preference`

```rust
pub fn stream_preference(req: &SocksRequest, addr: &str) -> StreamPrefs;
```

Observable behaviour:

- Maps a SOCKS request and its target address to stream preferences, by this
  precedence:
  1. If `addr` parses as an IPv4 literal → `ipv4_only()`. The caller named an
     IPv4 address, so nothing else can satisfy them.
  2. Else if `addr` parses as an IPv6 literal → `ipv6_only()`.
  3. Else if the request is SOCKS **version 4** → `ipv4_only()`, because
     SOCKS4 and SOCKS4a cannot express an IPv6 answer.
  4. Otherwise → `ipv4_preferred()`.
- The literal checks precede the version check, so a SOCKS5 request for an IPv4
  literal is IPv4-only rather than IPv4-preferred.
- A hostname over SOCKS5 is the only input that yields a *preference* rather
  than a restriction.
- The function builds a fresh `StreamPrefs::new()` and returns it **by value**;
  it never sets isolation, so the result's isolation is the default *none*
  state of §10.1. Isolation for a SOCKS connection is applied separately, from
  the listener index, the client's source address and the SOCKS authentication
  string.
- Because the setters return `&mut Self`, the implementation mutates a local and
  returns it; the signature is `-> StreamPrefs`, not `-> &mut StreamPrefs`.

---

## 12. `tor-error` — the error taxonomy (inherited)

Nothing in §12 is new at this checkpoint: the taxonomy arrived at the previous
one and this step's instruction is to *preserve* it. It is nevertheless written
out in full here rather than cited. The two groups that check this surface —
`errtaxonomy` and `deferboot` — are **both gate groups: they add no requirement
of their own, and a failure in either means the step is not satisfied.** They
are the pair whose failure costs the whole step, which is why each group is
all-or-nothing and a single wrong `Display` string or a single missing
`HasKind` impl is enough to fail it. A cross-document pointer is not a good
enough basis for that, so everything the two groups rely on is stated here, in
this document, in terms of what is observable from outside the crates.

### 12.1 `tor_error::{ErrorKind, HasKind, Bug}`

#### The trait

```rust
pub trait HasKind {
    /// Return the kind of this error.
    fn kind(&self) -> ErrorKind;
}
```

`HasKind` is **object-safe**: `Box<dyn HasKind>` is a legal type, a
`Vec<Box<dyn HasKind>>` may mix errors from unrelated crates, and `kind()`
dispatches dynamically through the vtable. Any change that added a generic
method, a `Self: Sized` bound or an associated type would break this and is
therefore out of bounds.

#### `ErrorKind`

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, Display)]  // Display from `derive_more`
#[non_exhaustive]
pub enum ErrorKind { /* 42 unit variants, listed below */ }
```

Three properties are checked, for every variant:

1. `Debug` renders **exactly the variant name** — `format!("{:?}", ErrorKind::NoExit)`
   is `"NoExit"`. This follows from the standard `Debug` derive on a unit
   variant; do not hand-write `Debug`.
2. `Display` is a **separate, non-empty, lower-case-ish human label** that is
   *not* equal to the variant name. It comes from `derive_more::Display` with a
   per-variant `#[display(fmt = "…")]` attribute.
3. The labels are **distinct across the whole enum** — no two variants share a
   `Display` string.

`ErrorKind` is `Copy` and `Eq`, so a kind can be stashed in a local and compared
later, and two different variants compare unequal.

The variants and their exact labels:

| Variant | `Display` |
|---|---|
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
| `ClockSkew` | `possible clock skew detected` |
| `Internal` | `internal error (bug)` |
| `Other` | `unclassified error` |

The exact wording of a label is not itself asserted anywhere — only
non-emptiness, difference from the variant name, and mutual distinctness are.
The table is given so that a solution reproducing the enum has a known-good set
to copy rather than having to invent 42 distinct strings; **two labels are load
bearing** and are asserted indirectly, through `Bug`'s rendering (next
subsection): `Internal` must render `internal error (bug)` and `BadApiUsage`
must render `bad API usage (bug)`.

`PersistentStateCorrupted`, `CacheAccessFailed`, `RemoteNetworkTimeout`,
`RemoteStreamError`, `ClockSkew` and `Other` exist in the enum but are not
exercised by any group in this step; the other 36 are.

**This table supersedes step 06's, which is still binding and now short by
two.** In `/app/docs/step-06/TEST_INTERFACE.md`, that document's section 1.1
declares `pub enum ErrorKind { /* 40 variants */ }` and its section 1.1.1 says
"Every one of these 40 renderings is checked." Both statements were accurate at
that checkpoint. At
this one the enum has grown by exactly two variants — `ClockSkew`
(`possible clock skew detected`) and `Other` (`unclassified error`) — with no
other variant renamed or relabelled, so the count is **42** and the table above
is the authoritative one. Neither new variant is exercised by any group in this
step, so an `ErrorKind` carried forward unchanged from step 06 still passes
`errtaxonomy`; but the enum is `#[non_exhaustive]`, growing it is the expected
thing for it to do, and a solution that reproduces this checkpoint has 42.

#### `Bug`

```rust
#[derive(Debug, Clone)]
pub struct Bug(/* private, boxed representation */);

impl Bug {
    #[track_caller]
    pub fn new<S: Into<String>>(kind: ErrorKind, message: S) -> Self;

    #[track_caller]
    pub fn from_error<E, S>(kind: ErrorKind, source: E, message: S) -> Self
    where
        S: Into<String>,
        E: std::error::Error + Send + Sync + 'static;
}

impl std::error::Error for Bug { /* source() */ }
impl std::fmt::Display for Bug { /* see below */ }
impl HasKind for Bug { /* returns the kind it was constructed with */ }
```

- The inner representation is **boxed** and private. A `Bug` carries: the
  formatted message, the `&'static panic::Location<'static>` captured by
  `#[track_caller]`, an optional captured backtrace (only under the `backtrace`
  cargo feature; otherwise a zero-sized placeholder that renders as the empty
  string), an optional `Arc<dyn std::error::Error + Send + Sync>` source, and
  the `ErrorKind` — which is always `Internal` or `BadApiUsage`. One `Bug` type
  carrying a kind, rather than two separate error types, is deliberate: it lets
  every crate's error enum hold a single `Bug(#[from] tor_error::Bug)` variant.
- `Display` is
  `writeln!(f, "{} at {}: {}", kind, location, message)` followed by the
  backtrace. So the rendering **begins with the kind's own label, then `" at "`,
  then the call site, then `": "`, then the message**. Concretely:
  - a `Bug` of kind `Internal` renders starting with `"internal error (bug) at "`;
  - a `Bug` of kind `BadApiUsage` renders starting with `"bad API usage (bug) at "`;
  - in both cases the formatted message appears later in the same string.
- `Error::source()` is `None` for a `Bug` built by `Bug::new`, and
  `Some(&underlying)` for one built by `Bug::from_error`; in the latter case the
  source's own `Display` is preserved verbatim.
- `HasKind for Bug` returns the stored kind, so `kind()` is exact rather than a
  classification: `Internal` stays `Internal` and `BadApiUsage` stays
  `BadApiUsage`, and the two are never equal.
- A `Bug` is a real `std::error::Error`, so `&bug as &dyn std::error::Error`
  coerces and still renders the message.

#### The four bug macros

All four are `#[macro_export]`ed, so they are reachable as `tor_error::internal!`
and so on, and all four take `format!`-style arguments. They are macros, not
functions, so that `#[track_caller]` records the *caller's* line:

| Macro | Expands to | Use |
|---|---|---|
| `internal!(fmt, ..)` | `Bug::new(ErrorKind::Internal, format!(..))` | a supposedly impossible state |
| `bad_api_usage!(fmt, ..)` | `Bug::new(ErrorKind::BadApiUsage, format!(..))` | a caller broke a documented precondition |
| `into_internal!(fmt, ..)` | `\|source\| Bug::from_error(ErrorKind::Internal, source, format!(..))` | for `Result::map_err` |
| `into_bad_api_usage!(fmt, ..)` | `\|source\| Bug::from_error(ErrorKind::BadApiUsage, source, format!(..))` | for `Result::map_err` |

The two `into_*` macros expand to a **closure**, not to a `Bug`; that is what
makes `x.map_err(into_internal!("context {}", y))?` work, and it is why the
foreign error survives as `source()`.

`bad_api_usage!` is what §8.1's `Error::UnusableTarget` wraps when a
`ChanTarget` has no addresses.

#### `tor_config::ConfigBuildError`

```rust
#[derive(Debug, Clone, thiserror::Error)]
#[non_exhaustive]
pub enum ConfigBuildError {
    #[error("Field was not provided: {field}")]
    MissingField { field: String },

    #[error("Value of {field} was incorrect: {problem}")]
    Invalid { field: String, problem: String },

    #[error("Fields {fields:?} are inconsistent: {problem}")]
    Inconsistent { fields: Vec<String>, problem: String },
}

impl ConfigBuildError {
    #[must_use]
    pub fn within(&self, prefix: &str) -> Self;
}

impl From<derive_builder::UninitializedFieldError> for ConfigBuildError { .. }
impl HasKind for ConfigBuildError { /* always ErrorKind::InvalidConfig */ }
```

- All three variants are **struct variants with public fields**, so a test (or a
  hand-written `validate` function, as in §2.3) constructs them directly.
- `within(prefix)` returns a **new** error of the same variant with every field
  name re-rooted as `format!("{}.{}", prefix, field)`; for `Inconsistent` the
  mapping is applied to each element of `fields`. The `problem` text is cloned
  unchanged. It takes `&self`, is `#[must_use]`, and does not mutate.
- Because `within` preserves the variant and `kind()` ignores the variant
  entirely, re-rooting never changes the classification: it is `InvalidConfig`
  before and after.
- The `From<UninitializedFieldError>` impl is what makes
  `#[builder(build_fn(error = "ConfigBuildError"))]` produce `MissingField`
  automatically for an unset required field (§2.2, §2.3, §5.1, §11.1).

Worked renderings, exactly:

| Value | `to_string()` |
|---|---|
| `MissingField { field: "socks_port" }` | `Field was not provided: socks_port` |
| the same, `.within("proxy")` | `Field was not provided: proxy.socks_port` |
| `Invalid { field: "socks_port", problem: "out of range" }`, `.within("proxy")` | `Value of proxy.socks_port was incorrect: out of range` |
| `Inconsistent { fields: ["authorities", "fallbacks"], problem: "override both or neither" }`, `.within("tor_network")` | `Fields ["tor_network.authorities", "tor_network.fallbacks"] are inconsistent: override both or neither` |

Note the `{fields:?}` in the `Inconsistent` format string: the field list is
rendered with `Debug`, which is why the names appear inside square brackets and
in double quotes.

#### `tor_config::{Reconfigure, ReconfigureError}`

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

#[derive(Debug, Clone, thiserror::Error)]
#[non_exhaustive]
pub enum ReconfigureError {
    #[error("Cannot change {field} on a running client.")]
    CannotChange { field: String },
}

impl HasKind for ReconfigureError { /* always ErrorKind::InvalidConfigTransition */ }
```

- `cannot_change` takes `self` **by value** (the enum is `Copy`) and its
  argument by `AsRef<str>`, so a `&str` is accepted directly.
- `AllOrNothing` and `CheckAllOrNothing` both return
  `Err(ReconfigureError::CannotChange { field })` with `field` set to the
  argument verbatim. `WarnOnFailures` logs at `warn` level and returns `Ok(())`
  — it does **not** produce an error. That three-way split is the whole
  behaviour of the function.
- `ReconfigureError::CannotChange { field: "storage.cache_path" }` renders
  exactly `Cannot change storage.cache_path on a running client.` — **with the
  trailing full stop**.
- `InvalidConfigTransition` is a *different* kind from `InvalidConfig`: a
  configuration that is well-formed but cannot be applied to an already-running
  client is not the same failure as a configuration that is malformed.

#### `tor_socksproto::Error`

```rust
#[derive(thiserror::Error, Debug)]
#[non_exhaustive]
pub enum Error {
    #[error("SOCKS protocol syntax violation")]
    Syntax,
    #[error("Error decoding message")]
    Decode(#[from] tor_bytes::Error),
    #[error("Unrecognized SOCKS protocol version {0}")]
    BadProtocol(u8),
    #[error("SOCKS feature not implemented")]
    NotImplemented,
    #[error("SOCKS handshake was finished; no need to call this again")]
    AlreadyFinished(tor_error::Bug),
    #[error("Bug while handling SOCKS handshake")]
    Bug(#[from] tor_error::Bug),
}
```

`HasKind` for it:

| Pattern | `ErrorKind` |
|---|---|
| `Decode(tor_bytes::Error::Truncated)` | `Internal` (it should have been converted before a caller saw it) |
| `Syntax`, any other `Decode(_)`, `BadProtocol(_)` | `LocalProtocolViolation` |
| `NotImplemented` | `NotImplemented` |
| `AlreadyFinished(bug)`, `Bug(bug)` | delegates to `bug.kind()` |

A malformed greeting is a *local* protocol violation, never
`TorProtocolViolation`: the peer that spoke badly is the local SOCKS client, not
a relay.

The reachable entry point is the handshake state machine:

```rust
pub type TResult<T> = std::result::Result<Result<T>, Truncated>;  // Result<T> = Result<T, Error>

#[derive(Clone, Debug)]
pub struct SocksHandshake { /* private */ }

impl SocksHandshake {
    pub fn new() -> Self;
    pub fn handshake(&mut self, input: &[u8]) -> TResult<Action>;
    // The two accessors below are not driven by this step's requirement groups, but
    // the driver's shared helper module calls them — `socks5_request` and
    // `socks4a_request` in `common/mod.rs` both end in
    // `hs.into_request().expect(..)` — and that module is compiled into the
    // `deferboot` gate target. A `SocksHandshake` missing either method fails
    // to compile the gate and vetoes the step.
    pub fn finished(&self) -> bool;
    pub fn into_request(self) -> Option<SocksRequest>;
}
```

- The **doubly wrapped** return type is deliberate: the outer `Err(Truncated)`
  means “send me more bytes”, the inner `Err(Error)` means “this handshake has
  failed”. An empty `input` is the one case that yields `Err(Truncated)` up
  front.
- Dispatch is on `(state, input[0])`. From the initial state, a first byte of
  `4` or `5` enters the SOCKS4 / SOCKS5 paths; **any other first byte yields
  `Ok(Err(Error::BadProtocol(byte)))`** — the version is rejected outright
  rather than treated as a truncated message. Three bytes are more than enough
  to reach that decision.

#### Where the kinds in this step come from

`errtaxonomy` checks the taxonomy directly. Every other group that mentions a
kind reaches it through one of these impls:

| Kind | Reported by | Documented in |
|---|---|---|
| `Internal`, `BadApiUsage` | `Bug` | §12.1, §8.1 |
| `InvalidConfig` | `ConfigBuildError` | §12.1, §2.2, §2.3, §5.1, §11.1 |
| `InvalidConfigTransition` | `ReconfigureError` | §12.1 |
| `LocalProtocolViolation`, `NotImplemented` | `tor_socksproto::Error` | §12.1 |
| `TorAccessFailed`, `NoPath` | `tor_guardmgr::PickGuardError` | §5.3 |
| `BootstrapRequired` | an unbootstrapped `arti_client::TorClient` | §10.3 |
| the nine `tor_chanmgr::Error` rows | `tor_chanmgr::Error` | §8.1 |
| the `tor_dirclient` rows | `tor_dirclient::{Error, RequestError}` | §6.1 |

---

## 13. Group → item map

| Group | Kind | Items |
|---|---|---|
| `retrydelay` | requirement | §1.1 |
| `dlschedule` | requirement | §2.1, §1.1 |
| `isotoken` | requirement | §3.1, §3.3 |
| `isohelper` | requirement | §3.1, §3.2, §3.3 |
| `streamisol` | requirement | §3.4, §3.1, §3.3 |
| `prefsisol` | requirement | §10.1, §3.2 |
| `socksprefs` | requirement | §11.3, §10.1 |
| `dnsportcfg` | requirement | §11.1 |
| `udpruntime` | requirement | §9.1, §9.4 |
| `taskschedule` | requirement | §9.2, §9.4 |
| `fallbackdir` | requirement | §5.1, §4.1 |
| `fallbacklist` | requirement | §5.2, §5.1, §5.3 |
| `guardpickerr` | requirement | §5.3, §3.6, §12.1 |
| `dirsource` | requirement | §6.1, §3.6, §4.1 |
| `pathintro` | requirement | §3.5, §4.1 |
| `netdirprov` | requirement | §7.1, §9.4 |
| `dirprovider` | requirement | §2.4, §7.1, §9.3, §9.4 |
| `dirmgrcfg` | requirement | §2.3, §2.2, §1.1 |
| `dormant` | requirement | §10.2, §10.3, §9.4 |
| `happyeyeballs` | requirement | §8.1, §4.1, §9.4 |
| `resolvedefer` | requirement | §10.3, §11.1 |
| `errtaxonomy` | gate | §12.1 |
| `deferboot` | gate | §10.3, §12.1, §9.4 |
| `basicutil` | requirement | §1.2, §1.3 |
| `articfg` | requirement | §11.2, §11.1 |

### Declared here, not driven by any case

The contract above is the surface, not the call list: it names items because
later steps and real callers build on them, and no group in this step reaches
the ones below. They are listed so that a missing case is a decision on the
record rather than a gap a reader has to infer. **Implement every one of
them.**

- `is_partial()` and its rule that it is **exactly** `error().is_some()`, along
  with `DirResponse::{status_code, output, into_output}` — no case in this step
  reads any of the four.
- Eight of the nine `tor_dirclient::RequestError` variants, with their kinds and
  `Display` strings; only `DirTimeout` is constructed.
- The `ChanMgr` trio `launch_background_tasks`, `bootstrap_events` and
  `expire_channels`' effect, as §8.1 now says.
- `TorClient::builder()`'s documented panic when there is no ambient runtime.
- `BootstrapStatus::blocked()` as a client-level call, and
  `StreamPrefs::optimistic()`.
- `TimeoutError`'s name; the `into_bad_api_usage!` macro; and the tuple arities
  of §3.2 other than the ones the isolation cases build.
- The failure arms of `DirProvider::{reconfigure, bootstrap}` — the `Err`
  paths, which the cases in scope do not enter because their stub provider
  returns `Ok`.

Nothing else in this document is out of scope in the sense of "named but
unreachable": every other item is exercised by at least one case in
`tests/assets/groups.txt`. The documents' own *non-requirements* — the
`"network is down"` back-off, DNS answers over a live exit, the accessors §6.1
and §10.3 declare out of scope, and the `#[non_exhaustive]`/privacy shape facts
no driver can observe — are recorded where each is declared, and are not
coverage holes.
