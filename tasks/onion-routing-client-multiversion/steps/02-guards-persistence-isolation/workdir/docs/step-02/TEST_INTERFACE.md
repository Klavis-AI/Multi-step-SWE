# Test interface — step 02 (`02-guards-persistence-isolation`)

Everything this step is checked through is named below: the crate, the module
path, the exact public signature the conformance driver calls, and the
behaviour it observes. Nothing outside this document is checked.

The driver is an external crate. It reaches your workspace only through
`pub` items on the paths written here — never through `pub(crate)` items,
`#[cfg(test)]` items, private modules, or `#[doc(hidden)]`. Items are grouped
by crate; the final section maps each requirement group to the items it uses.

This checkpoint adds two crates that did not exist at step 01 — `tor-persist`
and `tor-guardmgr` — and the two are deliberately coupled: guards are the first
thing a client has to remember across restarts, and remembering it is what
`tor-persist` is for. The inherited groups re-check step 01's cell, SOCKS and
netdoc surfaces against the carried `/app` workspace.

---

## 0. Cargo features the driver enables

**This section supersedes step 01's contract for this checkpoint.** Step 01's
`TEST_INTERFACE.md` says the driver enables no features and that no non-default
feature is on. That was true of step 01. It is *not* true here: the driver
manifest for step 02 names four features, and they are resolved by cargo before
a single test runs.

| Crate | Feature | Must enable | Why the driver needs it |
| --- | --- | --- | --- |
| `tor-netdir` | `testing` | the optional `hex` and `hex-literal` dependencies, and `tor-netdoc/build_docs` | exposes `tor_netdir::testnet`, which every network fixture is built from |
| `tor-netdoc` | `build_docs` | the optional `rand` dependency | exposes the document *builders* (`MicrodescBuilder`, `RouterStatusBuilder`, `MdConsensus::builder`) that `testnet` needs |
| `tor-persist` | `testing` | nothing (it is an empty feature) | gates the `testing` module, exposing `tor_persist::TestingStateMgr` |
| `tor-rtcompat` | `tokio` | the tokio backend, including `async_executors/tokio_tp` | exposes the tokio runtime; **this crate's default feature set is empty**, so without naming `tokio` there is no runtime at all |

These are not optional extras, and a crate that merely *declares* the feature is
not enough on its own — but declaring it is the part whose absence is fatal in a
way you cannot debug. A crate that does not DECLARE the feature, even as an
empty entry in its `[features]` table, fails cargo metadata resolution, and the
verifier then fails **every group in this step at once** with no per-group
detail to point at the cause.

Two consequences of the table above are worth stating outright, because they are
what the fixtures actually depend on:

- `tor-netdir`'s `testing` feature must make `tor_netdir::testnet` a public
  module. **Its full contents are specified in §4.3** — the four constructor
  functions, the `NodeBuilders` struct handed to the customization callback,
  and the exact composition of the forty-relay network, every value of which is
  checked. Seven groups read that network directly, so §4.3 is not background
  material; read it before writing the module. The fixtures reach a `NetDir`
  by calling `PartialNetDir::unwrap_if_sufficient()` on what `testnet` returns,
  and that method's error arm hands the `PartialNetDir` back
  (`Result<NetDir, PartialNetDir>`, not a crate error).
- `tor-netdoc` has **no `default` feature key at all** in this release. Its
  `build_docs` feature is therefore the only way the builders become visible.

### 0.1 The runtime the driver builds

`GuardMgr` is generic over `R: tor_rtcompat::Runtime` (§2.1) and the driver
instantiates it at one concrete type, spelled out in the fixtures as

```rust
// Both of these live in the driver, not in your workspace. They are shown
// because the names and types they compose are yours to provide.
pub type TestRt = tor_rtmock::MockSleepRuntime<async_executors::TokioTp>;

let rt: TestRt =
    tor_rtmock::MockSleepRuntime::new(tor_rtcompat::tokio::create_tokio_runtime()?);
```

so three things below are checked by *type*, not merely by name. A mismatch here
is a compile error in the driver, which fails every group that builds a
`GuardMgr` — `guardsel`, `guardpersist`, `guardusage`, `guardreport` and
`pathbuild` — plus `subnets`, which writes the type out in an annotation in
order to pass `None` for the "no guard manager" argument. `pathbuild` does
both: it constructs a manager *and* names the type in an annotation.

```rust
// tor-rtcompat, with the `tokio` feature on (see the table above)
pub mod tokio {
    pub fn create_tokio_runtime() -> std::io::Result<async_executors::TokioTp>;
    pub fn create_runtime() -> std::io::Result<impl Runtime>;
    pub fn current_runtime() -> std::io::Result<impl Runtime>;
    // plus TokioRuntimeHandle and test_with_runtime
}

// tor-rtmock (no features; the crate is always built)
pub struct MockSleepRuntime<R: Runtime> { /* private */ }

impl<R: Runtime> MockSleepRuntime<R> {
    pub fn new(runtime: R) -> Self;     // the only one the driver calls
    pub fn inner(&self) -> &R;
    // upstream also offers a handle on the mock clock and helpers to advance
    // it; nothing in this step calls them, so their names are yours.
}
```

- **`create_tokio_runtime` returns the concrete `async_executors::TokioTp`**,
  not `impl Runtime`. This module exports both spellings and they differ:
  `tor_rtcompat::tokio::create_runtime()` is the erased one and
  `create_tokio_runtime` is the concrete one, and the driver writes
  `MockSleepRuntime<async_executors::TokioTp>` as a type alias, which an
  opaque return type cannot satisfy.
- **`MockSleepRuntime<R>` must itself be a `Runtime`.** `Runtime` is the
  blanket-implemented marker over `Sync + Send + Spawn + SpawnBlocking + Clone
  + SleepProvider + TcpProvider + TlsProvider + 'static`, so the wrapper
  satisfies it by delegating each of those to `R` and serving the time-related
  half from its own mock clock. `GuardMgr<MockSleepRuntime<TokioTp>>` is not
  nameable otherwise.
- **`Spawn`, wherever this contract writes it unqualified, is
  `futures::task::Spawn`** — the trait whose one method is
  `fn spawn_obj(&self, future: FutureObj<'static, ()>) -> Result<(),
  futures::task::SpawnError>`. It is not a trait this workspace declares: the
  driver launches a task with `futures::task::SpawnExt::spawn`, `SpawnExt` is
  blanket-implemented over `futures::task::Spawn` and nothing else, and a type
  that implements a crate-local `Spawn` of its own instead — however similar in
  shape — is a type `SpawnExt::spawn` does not apply to, so the driver does not
  compile against it. Every type here that is a `Runtime` therefore implements
  `futures::task::Spawn` and forwards `spawn_obj` to the runtime it wraps.
- **`new` takes the wrapped runtime by value and is the only method the driver
  calls.** What the fixtures rely on beyond the type is one behavioural
  property: the wrapper's simulated clock **does not advance on its own**. That
  is why this runtime is used rather than the bare tokio one — the guard
  manager spawns a periodic maintenance task, and under a mock clock that never
  moves, that task stays parked and cannot perturb a observation between
  two `select_guard` calls. A `MockSleepRuntime` whose `SleepProvider` simply
  forwarded to the wrapped runtime's real clock would make the guard groups
  timing-dependent.

## 1. `tor-persist` — durable client state

### 1.1 `tor_persist::StateMgr`

```rust
pub trait StateMgr: Clone {
    fn load<D>(&self, key: &str) -> Result<Option<D>> where D: DeserializeOwned;
    fn store<S>(&self, key: &str, val: &S) -> Result<()> where S: Serialize;
    fn can_store(&self) -> bool;
    fn try_lock(&self) -> Result<LockStatus>;
    fn create_handle<T>(self, key: impl Into<String>) -> DynStorageHandle<T>
    where
        Self: Send + Sync + Sized + 'static,
        T: Serialize + DeserializeOwned + 'static;
}
```

Observable behaviour:

- `load` returns `Ok(None)` for a key that was never stored. A missing key is a
  normal state, not an error — a client starting for the first time has no
  saved state and must not treat that as a failure.
- `store` replaces any previous value at the key; there is no append.
- A state manager is **read-only until it holds the lock**. `can_store()` is
  false before a successful `try_lock()`, and `store` called while `can_store()`
  is false fails with `Error::NoLock` rather than writing or panicking.
- `try_lock()` returning `Ok(_)` does **not** imply the lock was acquired: the
  caller must inspect the returned `LockStatus`, or ask `can_store()`. This is
  the trap the cases in scope check, because a caller that treats `Ok` as success
  will silently discard every later write.
- `StateMgr` requires `Clone`, and clones share the same underlying store: a
  value stored through one clone is visible through another.
- `create_handle` is a provided method, so implementors get it for free.

### 1.2 `tor_persist::LockStatus`

```rust
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum LockStatus {
    NoLock,
    AlreadyHeld,
    NewlyAcquired,
}

impl LockStatus {
    pub fn held(&self) -> bool;
}
```

Observable behaviour:

- The enum is **exhaustive** (`#[allow(clippy::exhaustive_enums)]`), so a match
  over it outside the crate needs no wildcard arm.
- The three values separate the two successful outcomes from the failure, and
  separate them from each other: `NewlyAcquired` on the first `try_lock()`,
  `AlreadyHeld` on a second call by the same holder, `NoLock` when another
  holder has it. A caller that only needed "may I write?" would ask
  `can_store()`; the distinction between the two success values exists so a
  caller can tell whether it just took ownership.
- `LockStatus` is `Copy` and compares by value.
- `held()` collapses the two successes: it is `true` for `NewlyAcquired` and
  `AlreadyHeld` and `false` for `NoLock`. It takes `&self`, not `self`. A checked
  case checks all three.

### 1.3 `tor_persist::{FsStateMgr, TestingStateMgr}`

```rust
#[derive(Clone, Debug)]
pub struct FsStateMgr { /* private */ }

impl FsStateMgr {
    pub fn from_path<P: AsRef<Path>>(path: P) -> Result<Self>;
}

#[derive(Clone, Default, Debug)]
pub struct TestingStateMgr { /* private */ }   // feature `testing`

impl TestingStateMgr {
    pub fn new() -> Self;
    pub fn block_lock_attempts(&self);
    pub fn unblock_lock_attempts(&self);
}
```

`from_path` is the *only* public inherent method on `FsStateMgr`; the three
above are the only public inherent methods on `TestingStateMgr`. Neither type
has a `new_manager`.

**On-disk layout of `FsStateMgr` (checked by `persistfs`).** Given the root
directory `P` handed to `from_path`:

| Path | What it is |
| --- | --- |
| `P/state/` | the directory every stored value lives in; created recursively if absent |
| `P/state.lock` | the lockfile, a **sibling of** `P/state/`, not a file inside it; opened but *not* acquired by `from_path` |
| `P/state/<key>.json` | one file per key |

- The state directory is created with mode `0o700` on unix, so `mode & 0o077 ==
  0`. This is not decoration: the guard sample names the relays a user's traffic
  enters the network through, and that is a profile of the user.
- The per-key filename is the key passed through filename sanitisation with
  `".json"` appended. Sanitisation is what keeps a key from escaping the state
  directory — `store("../../escape", ..)` must write exactly one file, inside
  `P/state/`, whose name ends in `.json` and contains no path separator. Keys
  are **not** rejected and sanitisation is **not** collision-free; it is a
  containment measure, not a namespacing one.
- Stored documents are **pretty-printed** JSON (multi-line, indented), because a
  human has to be able to read and repair a corrupted state file. The case in scope
  checks the stored text for a newline and for a **two-space** indent, which is
  what `serde_json`'s own pretty printer emits; a dense single-line encoding
  fails, and so does an indent of any other width.
- A store is written to a temporary file in the same directory and then renamed
  over the target, so a crash mid-write cannot leave a truncated document.
- A refused store (see below) leaves the state directory **empty**: the refusal
  happens before any file is touched.

**Behaviour shared by both managers:**

- Both implement `StateMgr` with the semantics in §1.1, and the cases in scope
  exercise the trait's contract through both and expect agreement.
- `from_path` succeeds on a directory that already holds state, and the manager
  it returns is **unlocked** — `can_store()` is false until `try_lock()`
  succeeds. Reopening a directory therefore yields a read-only manager that can
  still `load` what a previous run stored.
- `TestingStateMgr` is an in-memory store. Its lock is *not* released by any
  method; `block_lock_attempts()` simulates another process holding the lock, so
  the next `try_lock()` returns `NoLock` and `can_store()` stays false, and
  `unblock_lock_attempts()` reverses that. `block_lock_attempts()` **panics** if
  this manager already holds the lock.
- Both are `Clone`, and a clone shares one underlying store and one lock: a
  value stored through one clone is visible through another, and a clone of a
  locked manager is itself writable. This is the shape `StateMgr: Clone` in
  §1.1 exists to guarantee.

### 1.4 `tor_persist::{StorageHandle, DynStorageHandle}`

```rust
pub trait StorageHandle<T: Serialize + DeserializeOwned> {
    fn load(&self) -> Result<Option<T>>;
    fn store(&self, val: &T) -> Result<()>;
    fn can_store(&self) -> bool;
}

pub type DynStorageHandle<T> = Arc<dyn StorageHandle<T> + Send + Sync + 'static>;
```

Observable behaviour:

- A handle is a `StateMgr` plus a fixed key and a fixed type, so its `load` and
  `store` take no key. It exists so that most callers need not be generic over
  `StateMgr`.
- A handle's `load`/`store`/`can_store` agree with the same operations issued
  against its parent manager at the same key: a value stored through the handle
  is loadable through the manager, and vice versa.
- `DynStorageHandle<T>` is an `Arc<dyn ...>`, so handles are cheaply cloneable
  and shareable across threads.

### 1.5 `tor_persist::{Futureproof, JsonValue}`

```rust
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(untagged)]
pub enum Futureproof<T> {
    Understandable(T),
    Unknown(JsonValue),
}

impl<T> Futureproof<T> {
    pub fn into_option(self) -> Option<T>;
}

impl<T> From<T> for Futureproof<T> { /* -> Understandable */ }
```

Observable behaviour:

- This is the forward-compatibility mechanism for persisted state, and the
  reason it exists is a real operational hazard: a user who runs a newer client,
  then an older one, must not have their state destroyed by the older client
  failing to understand it.
- `#[serde(untagged)]` means deserialisation **tries `T` first** and falls back
  to `Unknown(JsonValue)` when that fails. A value that does not parse as `T` is
  therefore preserved verbatim rather than rejected.
- Round-tripping an `Unknown` re-serialises the original JSON unchanged, so an
  old client can load, hold, and re-store state it does not understand without
  corrupting it.
- `into_option()` discards an `Unknown`, returning `None`; `From<T>` always
  produces `Understandable`.
- The enum is exhaustive and compares by value.

### 1.6 `tor_persist::Error`

```rust
#[derive(thiserror::Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    IoError(#[source] Arc<std::io::Error>),
    NoLock,
    JsonError(#[source] Arc<serde_json::Error>),
}

impl From<std::io::Error> for Error;      // -> IoError
impl From<serde_json::Error> for Error;   // -> JsonError
```

Observable behaviour:

- Three variants, and the cases in scope **match on the variant, not on the
  message**. No error's *wording* in this step is checked; the phrasing is yours.
  What a few cases do assert about a rendering is only that it is not empty —
  an error that renders as the empty string has dropped the cause it is
  required to carry — which is the general rule stated at §10.
- `Error::NoLock` is returned when a write is attempted without the lock (§1.1).
- `Error::JsonError` is returned when a stored document cannot be deserialised
  into the type the caller asked for — loading a key as the wrong type is a JSON
  failure, not a missing key and not a panic.
- Both payload-carrying variants wrap their cause in `Arc` and attach it as a
  `#[source]`. The `Arc` is why the enum can be `Clone`, and `Clone` is why a
  single failure can be reported to several waiters; neither `std::io::Error`
  nor `serde_json::Error` is `Clone` on its own.
- The enum is `#[non_exhaustive]`, so a match over it outside the crate needs a
  wildcard arm.

---

## 2. `tor-guardmgr` — entry guard selection and persistence

### 2.1 `tor_guardmgr::{GuardMgr, GuardMgrError, Guard, GuardId}`

```rust
#[derive(Clone)]
pub struct GuardMgr<R: Runtime> { /* private */ }

impl<R: Runtime> GuardMgr<R> {
    pub fn new<S>(runtime: R, state_mgr: S) -> Result<Self, GuardMgrError>
    where
        S: StateMgr + Send + Sync + 'static;

    pub fn update_network(&self, netdir: &NetDir);
    pub fn set_filter(&self, filter: GuardFilter, netdir: &NetDir);
    pub fn note_internet_activity(&self);

    pub fn select_guard(
        &self,
        usage: GuardUsage,
        netdir: Option<&NetDir>,
    ) -> Result<(Guard, GuardMonitor, GuardUsable), PickGuardError>;

    pub fn store_persistent_state(&self) -> Result<(), GuardMgrError>;
    pub fn reload_persistent_state(&self) -> Result<(), GuardMgrError>;
    pub fn upgrade_to_owned_persistent_state(&self) -> Result<(), GuardMgrError>;
}
```

`new` takes **exactly two arguments**. There is no fallback-directory parameter
and no `FallbackList` type at this checkpoint: the fallback list a client uses
lives in `tor_netdir::fallback` as `FallbackDir`, and the guard manager never
sees it. Anything a later release passes here is out of scope for step 02.

`select_guard` returns a **`Guard`**, not a `FirstHop`; `FirstHop` does not
exist anywhere in this release.

`note_internet_activity` is **declared, out of scope**: it exists on the type and
takes no arguments, but it only rearms the manager's private "the internet is
probably down" timer, which no public item reports (§10), and no case calls it.

```rust
/// Returned by `GuardMgr::select_guard`.
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct Guard { /* private */ }

impl Guard {
    pub fn id(&self) -> &GuardId;
    pub fn get_relay<'a>(&self, netdir: &'a NetDir) -> Option<Relay<'a>>;
}

/// The cryptographic identity of a selected guard: an ed25519 key and an
/// RSA fingerprint together.
#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq, Hash)]
pub struct GuardId { /* private */ }

impl GuardId {
    pub fn get_relay<'a>(&self, netdir: &'a NetDir) -> Option<Relay<'a>>;
}

impl tor_linkspec::ChanTarget for Guard { /* addrs, ed_identity, rsa_identity */ }
```

- `Guard` and `GuardId` have **no public constructors** and no public fields:
  `GuardId::new` and `GuardId::from_relay` are not part of the public surface.
  A `Guard` can only come from `select_guard`. A `GuardId` comes from
  `Guard::id()` — or from serde, since `GuardId` derives `Deserialize`, as the
  block above shows. No case in scope builds either by hand.
- A `Guard`'s addresses and identities are reached through
  `tor_linkspec::ChanTarget`, which the caller must have in scope. In this
  release `ChanTarget` itself declares `addrs()`, `ed_identity()` and
  `rsa_identity()`; there is no `HasAddrs` or `HasRelayIds` trait to split them
  across, and none is expected until a later step.

Observable behaviour:

- A `GuardMgr` is constructed over a `StateMgr` (§1.1), which is how guard
  choices outlive the process. Construction succeeds against an empty store and
  does **not** require the lock.
- `select_guard` fails, rather than inventing a guard, when it has **no guard to
  choose from** — the state of a manager that has never been given a network
  directory and has not loaded a sample from its store either. The failure is a
  `PickGuardError` (§2.5), not a panic. It is the *empty sample* that is refused,
  not the absent directory: once a sample exists — because `update_network` was
  called, because the directory was passed as `select_guard`'s second argument,
  or because `reload_persistent_state` / `upgrade_to_owned_persistent_state`
  adopted one another manager wrote — `select_guard` succeeds with that second
  argument `None`, and a case in scope drives selection that way. Passing the
  directory as `select_guard`'s second argument is an alternative to
  `update_network` and seeds the sample the same way.
- After `update_network`, `select_guard` returns a guard drawn from the
  directory, together with a monitor and a usability signal (§2.4). The guard is
  always a relay that is still in the directory and carries the `Guard` flag,
  and `guard.id().get_relay(&netdir)` names that same relay.
- **Guard selection is sticky**: repeated `select_guard` calls with the same
  usage against an unchanged directory keep returning guards from a small
  primary set rather than sampling the whole directory afresh. That stability is
  the entire security property guards exist for (see `SPEC_NOTES.md`), so it is
  checked directly rather than as a statistical tendency.
- `store_persistent_state` writes the current guard set through the `StateMgr`;
  `reload_persistent_state` reads it back; `upgrade_to_owned_persistent_state`
  is the transition a manager makes when it acquires the lock after starting
  read-only. A new `GuardMgr` built over a store that has been written to starts
  from that sample rather than resampling.
- `store_persistent_state` on a manager whose `StateMgr` does not hold the lock
  fails with `GuardMgrError::State(tor_persist::Error::NoLock)` — the underlying
  state error is **carried through, not flattened** — and writes nothing.

```rust
#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum GuardMgrError {
    State(#[from] tor_persist::Error),
    Spawn(#[source] Arc<futures::task::SpawnError>),
}

impl From<futures::task::SpawnError> for GuardMgrError;   // -> Spawn(Arc::new(e))
```

Two variants. `State` carries `#[from]`, so `?` on a `tor_persist::Error` lands
there directly; `Spawn` wraps in `Arc` through a hand-written `From` so the enum
stays `Clone`. As everywhere in this step, the cases in scope match on the variant
and never on the message.

### 2.2 `tor_guardmgr::{GuardUsage, GuardUsageBuilder, GuardUsageKind}`

```rust
#[derive(Clone, Debug, Eq, PartialEq)]
#[non_exhaustive]
pub enum GuardUsageKind {
    Data,
    OneHopDirectory,
}

impl Default for GuardUsageKind {
    fn default() -> GuardUsageKind { GuardUsageKind::Data }
}

#[derive(Clone, Debug, Default, derive_builder::Builder)]
pub struct GuardUsage {
    #[builder(default)]
    kind: GuardUsageKind,
    #[builder(default, setter(strip_option))]
    restriction: Option<GuardRestriction>,
}

impl GuardUsageBuilder {
    pub fn new() -> Self;
    // generated by `derive_builder`:
    pub fn kind(&mut self, value: GuardUsageKind) -> &mut Self;
    pub fn restriction(&mut self, value: GuardRestriction) -> &mut Self;
    pub fn build(&self) -> Result<GuardUsage, GuardUsageBuilderError>;
}
```

Observable behaviour:

- `GuardUsage::default()` comes from the **derived `Default` impl**, not from an
  inherent method of that name. `GuardUsageKind`'s `Default` is hand-written and
  returns `Data`; it is not derived and `Data` is not merely "the first
  variant". `GuardUsageKind` is **not** `Copy`.
- `GuardUsageKind::Data` covers every anonymous use; `OneHopDirectory` is the
  non-anonymous directory-fetch case, which the algorithm allows to proceed with
  more parallelism because it is not protecting a circuit's anonymity. Both
  usages draw from the same sample.
- Both fields are **private with no accessors**, so the only public way to
  observe what a builder recorded is the derived `Debug` rendering. The checked
  cases therefore assert on `format!("{:?}", usage)` containing the variant name
  and, for the restriction, `Some`. Deriving `Debug` on a struct with these two
  field names is part of the contract for that reason.
- `restriction` carries `setter(strip_option)`, so the generated setter takes a
  **bare `GuardRestriction`**, not an `Option`. Both fields carry
  `#[builder(default)]`, so `GuardUsageBuilder::new().build()` succeeds with
  nothing set and yields the same value as `GuardUsage::default()`.
- The builder's error type is the one `derive_builder` generates for this
  struct. It is never inspected by a case in scope: `build()` is only ever
  expected to succeed.
- `GuardUsageKind` is `#[non_exhaustive]`; matches need a wildcard arm.

### 2.3 `tor_guardmgr::GuardStatus`

```rust
#[derive(Copy, Clone, Debug)]
#[non_exhaustive]
pub enum GuardStatus {
    Success,
    Failure,
    Indeterminate,
    AttemptAbandoned,
}
```

`GuardStatus` is `Copy` but **not** `PartialEq`. The derived `Debug` rendering
is consequently the only public way to tell two statuses apart, which is why the
case in scope compares the four `Debug` strings and requires them to be four
distinct values spelled exactly as above.

Observable behaviour:

- The four values separate outcomes that must be treated differently when
  deciding whether a guard is misbehaving. `Success` and `Failure` are evidence
  about the guard. `Indeterminate` records a circuit failure that *might* be the
  guard's fault but cannot be proven so, and `AttemptAbandoned` records that the
  attempt never got far enough to be evidence at all.
- Collapsing the last two into `Failure` would let ordinary local conditions
  accumulate into a false verdict against an honest guard, which is why they are
  distinct variants rather than a boolean.

### 2.4 `tor_guardmgr::{GuardMonitor, GuardUsable}`

```rust
#[must_use = "You need to report the status of any guard that you asked for"]
pub struct GuardMonitor { /* private */ }

impl GuardMonitor {
    pub fn succeeded(self);            // reports GuardStatus::Success
    pub fn failed(self);               // reports GuardStatus::Failure
    pub fn attempt_abandoned(self);    // reports GuardStatus::AttemptAbandoned
    pub fn report(self, msg: GuardStatus);
    pub fn commit(self);
    pub fn pending_status(&mut self, status: GuardStatus);
    pub fn ignore_indeterminate_status(&mut self);
}

pub struct GuardUsable { /* private */ }

impl Future for GuardUsable {
    type Output = Result<bool, futures::channel::oneshot::Canceled>;
}
```

Observable behaviour:

- `select_guard` hands back a monitor and a usability future alongside the
  guard. The monitor is how the caller reports a `GuardStatus` back; the
  `GuardUsable` resolves when the manager has decided whether this guard may
  actually carry traffic.
- `GuardUsable` has **no public inherent methods at all**. It is used only by
  awaiting it, and it yields `Ok(true)` for a primary guard **immediately**,
  without any round trip through the manager — a caller must not have to block
  on the manager's daemon task to use a guard it already trusts.
- `GuardMonitor` is `#[must_use]`, and every method that reports a status
  consumes `self` — all five of `succeeded`, `failed`, `attempt_abandoned`,
  `report` and `commit` — so a status can be reported exactly once. The two
  methods that take `&mut self`, `pending_status` and
  `ignore_indeterminate_status`, do not report.
- Dropping a `GuardMonitor` without reporting sends its *pending* status, which
  starts as `AttemptAbandoned`. A caller that panics mid-circuit therefore does
  not poison the guard's reputation. `pending_status` changes what a drop will
  report; `commit` sends the pending status explicitly.
- `ignore_indeterminate_status` downgrades a later `Indeterminate` report to
  `AttemptAbandoned`, which is how a caller says "I already know this failure
  was mine, do not count it against the guard".

**Declared, out of scope.** The signatures above are the contract, and every one of
them must exist, but a case in scope drives only `succeeded` and
`attempt_abandoned` (plus the drop). `failed`, `report`, `commit`,
`pending_status` and `ignore_indeterminate_status` are neither called nor
observed by any case: their effects land in the private path-bias state that §10
declares out of scope, so no public item can read them back. Implement them to the
semantics above — the drop path in particular is written in terms of
`pending_status` — but do not expect a case to confirm them, and do not read
their absence from the work as a defect in your work.

### 2.5 `tor_guardmgr::PickGuardError`

```rust
#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum PickGuardError {
    EveryoneIsDown,
    SampleIsEmpty,
}
```

Observable behaviour:

- Returned by `select_guard` when no guard can be chosen — including the
  no-directory case and the case where every candidate has been excluded.
- **Both variants are checked at this step**, by name, through `matches!`. The
  step-02 groups `guardsel`, `guardusage` and `errvariants` all depend on
  `EveryoneIsDown` being spelled that way and on it being the variant produced
  when the sample exists but nothing in it may be handed out — including when a
  `GuardRestriction` (§2.7) has excluded the only candidate. Both names are
  still spelled this way through step 06; step 07 replaces the enum outright
  and neither name survives into it. Check against the two names above, not
  against a later release's.
- `SampleIsEmpty` is part of the public surface and must exist. No case in scope
  requires any particular code path to produce it.
- As everywhere in this step, no `Display` text is checked.

### 2.6 The persisted guard document

The guard manager's sample is shared on-disk state, so its serialised form is
part of this checkpoint's contract rather than an internal detail: a second
manager over the same store has to read what the first one wrote.

- The sample is stored through `StateMgr::create_handle` under the key
  **`"default_guards"`**, and under no other key. `store_persistent_state`
  writes it; a fresh `GuardMgr` over a store that already holds it starts from
  it.
- The stored value is a **JSON object** with two recognised keys:

| Key | JSON type | Contents |
| --- | --- | --- |
| `guards` | array | one object per sampled guard, in sample order |
| `confirmed` | array | the identities of the confirmed guards, in confirmation order; **empty** until a guard has actually been used successfully |

  Unrecognised top-level keys are preserved across a load/store round trip
  rather than dropped — same forward-compatibility reasoning as §1.5.

- Each element of `guards` is an object with at least these keys:

| Key | JSON type | Notes |
| --- | --- | --- |
| `id` | object | `{"ed25519": <string>, "rsa": <string>}` — the two identities, under exactly those names |
| `orports` | array | the guard's OR addresses; non-empty for every guard in the test network, and the harness asserts it — though the field is a plain `Vec<SocketAddr>` copied from `Relay::addrs()`, so the type does admit an empty one |
| `added_at` | string | when the guard entered the sample; a timestamp string, never null |
| `added_by` | object or null | which crate and version added it |
| `disabled` | object or null | why the guard is permanently disabled, if it is |
| `confirmed_at` | string or null | **null** for a guard that has never been used successfully |
| `unlisted_since` | string or null | **null** for a guard that is currently *listed as a guard* in the consensus; set to the consensus's `valid_after` once the relay is no longer in the consensus **or is still in it but has lost the `Guard` flag**. "Listed" here means listed as a guard, not merely present: a relay that stays in the consensus with its flag cleared is unlisted for this purpose, and a case in scope asserts exactly that. |

  Fields that only make sense within one process run — retry deadlines,
  reachability, in-flight circuit counts — are **not** serialised at all. They
  are not optional-and-null; they are absent.

**The two identity strings have a specified encoding.** This is the one place
in this step where a serialised string is compared against a rendering the
driver produces itself, so it cannot be left to the implementation:

| Field | Encoding |
| --- | --- |
| `id.ed25519` | the 32 identity bytes in **base64, standard alphabet, no padding** (`base64::STANDARD_NO_PAD`) — 43 characters, no trailing `=` |
| `id.rsa` | the 20 identity bytes as **40 lowercase hex digits**, with no prefix and no separators |

`Ed25519Identity`'s `Display` prints exactly that same base64 string, so
`format!("{}", id)` and the serialised `ed25519` field are byte-for-byte equal.
Two cases depend on it: `guardpersist` and `guardreport` both find a guard in
the stored document by formatting the identity of test-network relay 38 (§4.3)
and comparing it with `doc["guards"][…]["id"]["ed25519"]`. That identity is the
byte `0x26` repeated 32 times, so the string is
`JiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiYmJiY`. An `Ed25519Identity` whose
serialisation and whose `Display` disagree — hex in one and base64 in the
other, or padded base64 in one — fails both groups even though every other
field of the document is right.

`RsaIdentity` is the deliberate asymmetry, and it is called out so that neither
rendering gets written in terms of the other: its `Display` prepends a `$`
(`$0f0f…`), its serialised form does **not** (`0f0f…`). Only that the `rsa`
field is a string is checked here; nothing in this step compares its content
against a literal. (The `$`-prefixed `Display` is checked elsewhere — see §10.)

Both types serialise as **bytes rather than strings** when the format is not
human-readable. Everything above describes the human-readable arm, which is
what `serde_json` selects and therefore what the persisted document contains.

### 2.7 `tor_guardmgr::{GuardFilter, GuardRestriction}`

```rust
#[derive(Debug, Clone)]
#[non_exhaustive]
pub enum GuardFilter {
    Unfiltered,
}

impl Default for GuardFilter { /* -> Unfiltered */ }

impl GuardFilter {
    pub fn unfiltered() -> Self;
}

#[derive(Clone, Debug)]
#[non_exhaustive]
pub enum GuardRestriction {
    AvoidId(tor_llcrypto::pk::ed25519::Ed25519Identity),
}
```

Observable behaviour:

- These are the two ways a caller narrows what the manager may hand back, and
  they differ in lifetime: a `GuardFilter` is installed on the manager with
  `set_filter` and persists; a `GuardRestriction` rides on one `GuardUsage`
  (§2.2) and applies to that request only.
- `GuardFilter` has exactly one non-test variant at this release. Both
  `GuardFilter::default()` and `GuardFilter::unfiltered()` produce `Unfiltered`,
  and under it every guard in the directory stays selectable. The type exists
  now so the manager's plumbing is in place before real filters arrive.
- `GuardRestriction::AvoidId` excludes one ed25519 identity. If it excludes the
  only candidate, `select_guard` returns `PickGuardError::EveryoneIsDown` (§2.5)
  — it does **not** fall back to the excluded guard, and does not panic.
- Neither enum is `PartialEq`; both are `#[non_exhaustive]`.

---

## 3. `tor-circmgr` — isolation, paths and configuration

### 3.1 `tor_circmgr::{IsolationToken, StreamIsolation, StreamIsolationBuilder}`

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct IsolationToken(u64);          // field private

impl IsolationToken {
    pub fn new() -> Self;                // no `Default` impl
    pub fn no_isolation() -> Self;
}

#[derive(Copy, Clone, Eq, Debug, PartialEq, derive_builder::Builder)]
pub struct StreamIsolation {
    #[builder(default = "IsolationToken::no_isolation()")]
    stream_token: IsolationToken,
    #[builder(default = "IsolationToken::no_isolation()")]
    owner_token: IsolationToken,
}

impl StreamIsolation {
    pub fn no_isolation() -> Self;
    pub fn builder() -> StreamIsolationBuilder;
}

impl StreamIsolationBuilder {
    pub fn new() -> Self;
    // generated by `derive_builder`:
    pub fn stream_token(&mut self, value: IsolationToken) -> &mut Self;
    pub fn owner_token(&mut self, value: IsolationToken) -> &mut Self;
    pub fn build(&self) -> Result<StreamIsolation, StreamIsolationBuilderError>;
}
```

Observable behaviour:

- At this version these live at the **crate root** (`tor_circmgr::IsolationToken`),
  not in an `isolation` submodule. There is no `isolation` module and no
  `Isolation` trait at this checkpoint; both arrive at step 07.
- `new()` returns a token distinct from every other token `new()` has produced.
  The counter starts at 1 and is process-wide, so distinctness holds across
  threads and across every type that mints tokens.
- `no_isolation()` returns the token whose value is **0**: equal to every other
  `no_isolation()` token and distinct from every `new()` token, at this release
  and at every later one.
- `IsolationToken` is `Copy`, so a copy names the same isolation group.
  `IsolationToken` deliberately has **no `Default` impl** — "the default
  isolation" would be ambiguous between "fresh" and "shared", and the ambiguity
  is resolved by making the caller name one.
- Whether two streams may share a circuit is decided by **`PartialEq` on
  `StreamIsolation`**, not by any method: there is no `may_share_circuit`,
  `compatible_with` or similar at this release. Two `StreamIsolation`s are
  interchangeable exactly when both tokens are equal, so the two fields isolate
  independently and setting both narrows further than setting either.
- Both fields are private with no accessors; equality is the whole public
  observation channel, which is why the cases in scope are written as `assert_eq!`
  and `assert_ne!` between built values.
- `StreamIsolationBuilder::build()` succeeds with nothing set, and the result
  equals `StreamIsolation::no_isolation()`. `StreamIsolationBuilder::new()` and
  `StreamIsolationBuilder::default()` are the same builder. The build error is
  the `derive_builder`-generated `StreamIsolationBuilderError`; no case in scope
  requires it to occur.

`arti_client::ConnectPrefs` is the caller-facing side of the same mechanism and
is checked in the same group:

```rust
#[derive(Debug, Clone, Default)]
pub struct ConnectPrefs { /* private */ }

impl ConnectPrefs {
    pub fn new() -> Self;
    pub fn ipv6_preferred(&mut self) -> &mut Self;
    pub fn ipv6_only(&mut self) -> &mut Self;
    pub fn ipv4_preferred(&mut self) -> &mut Self;
    pub fn ipv4_only(&mut self) -> &mut Self;
    pub fn set_isolation_group(&mut self, isolation_group: IsolationToken) -> &mut Self;
}
```

- Every setter takes `&mut self` and returns `&mut Self`, so they chain.
- `set_isolation_group` **records** the token. The reader (`isolation_group()`)
  and the IP-preference reader (`begin_flags()`) are **private**, so the derived
  `Debug` is the only public way to see what was recorded. A case in scope
  therefore compares `Debug` renderings: two preference objects carrying
  different tokens must render differently, the same token must render
  identically, and a fresh `ConnectPrefs::new()` must differ from one that has
  had a token set. That is a statement about `ConnectPrefs` keeping the token,
  not about any particular wording.
- `ConnectPrefs::new()` and `ConnectPrefs::default()` agree.

### 3.2 `tor_circmgr::path::{TorPath, dirpath::DirPathBuilder, exitpath::ExitPathBuilder}`

```rust
pub enum DirInfo<'a> {                 // at the crate root; #[non_exhaustive]
    Fallbacks(&'a [tor_netdir::fallback::FallbackDir]),
    Directory(&'a tor_netdir::NetDir),
}

pub struct TorPath<'a> { /* private */ }
impl<'a> TorPath<'a> {
    pub fn len(&self) -> usize;
}

pub struct TargetPort { /* private */ }   // at the crate root
impl TargetPort {
    pub fn ipv4(port: u16) -> TargetPort;
    pub fn ipv6(port: u16) -> TargetPort;
    pub fn is_supported_by(&self, r: &tor_netdir::Relay<'_>) -> bool;
}

#[non_exhaustive]
pub struct DirPathBuilder {}
impl Default for DirPathBuilder { /* = new() */ }
impl DirPathBuilder {
    pub fn new() -> Self;

    pub fn pick_path<'a, R: Rng, RT: Runtime>(
        &self,
        rng: &mut R,
        netdir: DirInfo<'a>,
        guards: Option<&GuardMgr<RT>>,
    ) -> Result<(TorPath<'a>, Option<GuardMonitor>, Option<GuardUsable>)>;
}

pub struct ExitPathBuilder<'a> { /* private */ }
impl<'a> ExitPathBuilder<'a> {
    pub fn from_target_ports(wantports: impl IntoIterator<Item = TargetPort>) -> Self;
    pub fn from_chosen_exit(exit_relay: Relay<'a>) -> Self;
    pub fn for_any_exit() -> Self;

    pub fn pick_path<R: Rng, RT: Runtime>(
        &self,
        rng: &mut R,
        netdir: DirInfo<'a>,
        guards: Option<&GuardMgr<RT>>,
        config: &PathConfig,
    ) -> Result<(TorPath<'a>, Option<GuardMonitor>, Option<GuardUsable>)>;
}
```

The two `pick_path` signatures are **not the same**, and the difference is
checked:

| | `DirPathBuilder::pick_path` | `ExitPathBuilder::pick_path` |
| --- | --- | --- |
| arity | 3 (`rng`, `netdir`, `guards`) | 4 (`rng`, `netdir`, `guards`, `config`) |
| takes a `&PathConfig` | **no** | **yes** |
| hops in the returned path | 1 | 3 |
| over `DirInfo::Fallbacks` | picks a fallback, or `Error::NoRelays` if the slice is empty | always `Error::NeedConsensus` |

A directory path is a single non-anonymous hop, so no subnet rule applies to it
and it has no configuration to take. An exit path is anonymous and must honour
`PathConfig`. Giving `DirPathBuilder` a `config` parameter it cannot use would
be the wrong contract, and a caller written against a 4-argument
`DirPathBuilder::pick_path` will not compile.

Observable behaviour:

- `DirInfo::Fallbacks` holds a **plain slice** `&[FallbackDir]`. There is no
  `FallbackList` type anywhere in this release; an empty fallback set is an
  empty slice. `DirInfo` also has `From<&[FallbackDir]>` and `From<&NetDir>`.
- Both builders return the triple `(TorPath, Option<GuardMonitor>,
  Option<GuardUsable>)`. The two `Option`s are `Some` exactly when a guard
  manager actually chose the hop, and `None` whenever none was consulted —
  there is nothing to report a status to. That is `guards: None` for either
  builder, and **also** `DirPathBuilder` over `DirInfo::Fallbacks(_)`, where
  the fallback is drawn straight from the slice and the `guards` argument is
  ignored: that arm yields `None`, `None` even when the caller passed
  `Some(guards)`. A caller must be able to tell those apart without inspecting
  the path.
- `TorPath::len()` is the hop count: 1 for either one-hop constructor, and the
  number of relays for a multi-hop path. There is **no `is_empty()`** — `len`
  carries `#[allow(clippy::len_without_is_empty)]`. No path either builder
  returns is empty, but the type does admit one: `new_multihop` accepts an
  empty iterator, and the result has `len() == 0`.
- `ExitPathBuilder::from_target_ports(Vec::new())` is the same request as
  `for_any_exit()`. A port list that no relay in the directory can serve is
  refused with `Error::NoRelays` rather than downgraded to some other exit.
- `PathConfig`'s subnet rule (§3.3, §4.2) is enforced across the whole exit
  path: if every relay in the directory shares one subnet under the configured
  masks, no three-hop path exists and `pick_path` fails with `Error::NoRelays`.
  Widening the mask to full length makes those relays distinct again and a path
  appears. The same directory, two configurations, two outcomes — that is what
  makes the rule observable from outside.
- `tor_circmgr::Error` is checked **by variant**: `NeedConsensus` (unit) and
  `NoRelays(String)`. The `String` is a human-readable explanation and is **not**
  checked — no case asserts its contents, and no `Display` text of any
  `tor_circmgr::Error` is checked.

### 3.3 `tor_circmgr::{RequestTiming, CircuitTiming, PathConfig, CircMgrConfig}` and builders

Each type below carries `derive_builder::Builder`, and the harness constructs every one of
them through the builder it generates: `<Type>Builder::default().build()` is called directly,
by that exact name, from the same crate's public surface. The generated names are therefore
part of the required contract, not an implementation detail — `PathConfigBuilder` in particular
is what makes an empty `PathConfig` observable from outside the crate, since the struct's
fields are `pub(crate)`. The `#[builder(default)]` and `#[builder(default = "…")]` attributes
below decide the values a default-built instance carries, and those values are asserted.

```rust
#[derive(Debug, Clone, derive_builder::Builder, serde::Deserialize)]
pub struct RequestTiming {
    #[builder(default = "Duration::from_secs(60)")]
    #[serde(with = "humantime_serde")]
    pub(crate) request_timeout: Duration,
    #[builder(default = "32")]
    pub(crate) request_max_retries: u32,
    #[builder(default = "Duration::from_millis(50)")]
    #[serde(with = "humantime_serde")]
    pub(crate) request_loyalty: Duration,
}
impl Default for RequestTiming { /* = RequestTimingBuilder::default().build().unwrap() */ }

#[derive(Debug, Clone, derive_builder::Builder, serde::Deserialize, Default)]
pub struct PathConfig {
    #[builder(default)]
    pub(crate) enforce_distance: tor_netdir::SubnetConfig,
}

#[derive(Debug, Clone, derive_builder::Builder, serde::Deserialize)]
pub struct CircuitTiming {
    #[builder(default = "Duration::from_secs(60 * 10)")]
    #[serde(with = "humantime_serde")]
    pub(crate) max_dirtiness: Duration,
}
impl Default for CircuitTiming { /* = CircuitTimingBuilder::default().build().unwrap() */ }

#[derive(Debug, Clone, derive_builder::Builder, Default)]   // NOT Deserialize
pub struct CircMgrConfig {
    #[builder(default)] pub(crate) path_config: PathConfig,
    #[builder(default)] pub(crate) request_timing: RequestTiming,
    #[builder(default)] pub(crate) circuit_timing: CircuitTiming,
}
```

Defaults, exactly:

| Type | Field | Default | Renders in `Debug` as |
| --- | --- | --- | --- |
| `RequestTiming` | `request_timeout` | `Duration::from_secs(60)` | `60s` |
| `RequestTiming` | `request_max_retries` | `32` | `32` |
| `RequestTiming` | `request_loyalty` | `Duration::from_millis(50)` | `50ms` |
| `CircuitTiming` | `max_dirtiness` | `Duration::from_secs(600)` | `600s` |
| `PathConfig` | `enforce_distance` | `SubnetConfig::default()` (§4.2) | `16` and `32` |
| `CircMgrConfig` | all three | each part's own default | all of the above |

Observable behaviour:

- Every field is **`pub(crate)`**, and there are no accessors. The derived
  `Debug` is therefore the only way an external crate can see a configured
  value, which is why the cases in scope assert that a rendering **contains** the
  value they set. They assert containment of the value, never the layout of the
  rendering: the field names, spacing and ordering are yours. `Duration`'s own
  `Debug` is what produces `60s` / `50ms` / `600s`, so those spellings follow
  from storing a `Duration` rather than from anything this crate writes.
- A builder with nothing set builds successfully and equals the type's
  `Default`. Each setter is independent: setting one leaves the others at their
  defaults, and a case in scope checks the negative direction too — after
  overriding `request_timeout` to 90s, `60s` must no longer appear.
- `CircMgrConfig`'s parts are replaced wholesale, not field by field:
  `CircMgrConfigBuilder::request_timing(rt)` installs a complete
  `RequestTiming`, so a `RequestTimingBuilder` that set only
  `request_max_retries` contributes its own defaults for the other two fields.
- `RequestTiming`, `PathConfig` and `CircuitTiming` derive `Deserialize` and
  must round-trip from their field names as spelled above. The three `Duration`
  fields use `humantime_serde`, so `"90s"`, `"250ms"`, `"45s"` parse and an
  unparsable string such as `"whenever"` is an **error**, not a zero — a
  mistyped timeout must not silently become "no timeout at all".
- `CircMgrConfig` itself does **not** derive `Deserialize` at this release —
  only its three parts do. It is constructed through `CircMgrConfigBuilder`.
  No case in scope deserializes a `CircMgrConfig`.
- The generated builder types are exported and reachable by name:
  `RequestTimingBuilder`, `CircuitTimingBuilder`, `PathConfigBuilder` and
  `CircMgrConfigBuilder` all live in `tor_circmgr`'s public surface alongside
  the types they build. Each is constructible with `::default()` at every
  release this step is checked against, including before any field is set, and
  `.build()` on an empty builder succeeds because every field carries a
  `#[builder(default…)]` attribute.

---

## 4. `tor-netdir` — relay families and consensus parameters

### 4.1 `tor_netdir::params::NetParameters`

**This section supersedes step 01's `netparams` section.** That section, still
readable at `/app/docs/step-01/TEST_INTERFACE.md`, lists **seven** fields and
spells two of them `send_me_accept_min_version` and `send_me_emit_min_version`.
At this checkpoint there are **thirty-four**, and those two Rust fields are
respelled `sendme_accept_min_version` and `sendme_emit_min_version` — a rename
of the *field*, not of the consensus key, which was already `sendme_…` and does
not change. Where the two documents disagree on a name, a default or a count,
the table below governs. The five fields step 01 named that are unchanged
(`bw_weight_scale`, `circuit_window`, `circuit_priority_half_life`,
`extend_by_ed25519_id`, `min_circuit_path_threshold`) keep their spellings and
their defaults, and they appear in the table below like every other field.

```rust
#[derive(Clone, Debug)]
#[non_exhaustive]
pub struct NetParameters {
    pub bw_weight_scale: BoundedInt32<0, { i32::MAX }>,
    // ... one `pub` field per row of the table below ...
}

impl Default for NetParameters { /* every field at its built-in default */ }

impl NetParameters {
    pub fn from_map(p: &tor_netdoc::doc::netstatus::NetParams<i32>) -> Self;
}
```

`NetParams<T>` itself is the type step 01 introduced, and at this checkpoint it
must carry one impl step 01's contract did not name: **`impl<'de, T>
serde::Deserialize<'de> for NetParams<T> where T: Deserialize<'de>`**, reading a
flat JSON object of parameter name to value. The checked `netparams` cases build
every map they hand to `from_map` with
`serde_json::from_str::<NetParams<i32>>(r#"{"circwindow": 1}"#)`, so a
`NetParams` that is constructible only through `set`/`Default` — which is all
step 01's contract for the type required — fails the whole `netparams` group to
compile. This is an interface obligation, not a behaviour in scope: nothing
asserts *how* the impl works, only that the impl exists and that a flat object
deserialises into it.

The type lives at **`tor_netdir::params::NetParameters`**. `params` is a public
module; the type is **not** re-exported at the crate root, so `use
tor_netdir::NetParameters` does not compile. `NetDir::params()` returns
`&NetParameters`.

Every field is `pub` and is read by **direct field access**, not through a
getter. Each field then exposes its own value through the `tor-units` accessor
for its type (§8.1): `.get()` on a `BoundedInt32` or `SendMeVersion`,
`.as_percent().get()` and `.as_fraction()` on a `Percentage`, and
`TryInto<Duration>` on `IntegerMilliseconds` / `IntegerSeconds` / `IntegerDays`.
The wrapper type is what carries the unit, so a caller cannot read a
milliseconds parameter as if it were seconds.

These are the thirty-four recognised parameters, with the exact default a
client uses when the consensus is silent and the exact consensus key that
overrides it. Both columns are checked.

| Field | Type | Default | Consensus key |
| --- | --- | --- | --- |
| `bw_weight_scale` | `BoundedInt32<0, {i32::MAX}>` | `10_000` | `bwweightscale` |
| `cbt_learning_disabled` | `BoundedInt32<0, 1>` | `0` | `cbtdisabled` |
| `cbt_num_xm_modes` | `BoundedInt32<1, 20>` | `10` | `cbtnummodes` |
| `cbt_success_count` | `BoundedInt32<3, 1_000>` | `20` | `cbtrecentcount` |
| `cbt_max_timeouts` | `BoundedInt32<3, 10_000>` | `18` | `cbtmaxtimeouts` |
| `cbt_min_circs_for_estimate` | `BoundedInt32<1, 10_000>` | `100` | `cbtmincircs` |
| `cbt_timeout_quantile` | `Percentage<BoundedInt32<10, 99>>` | `80` | `cbtquantile` |
| `cbt_abandon_quantile` | `Percentage<BoundedInt32<10, 99>>` | `99` | `cbtclosequantile` |
| `cbt_min_timeout` | `IntegerMilliseconds<BoundedInt32<10, {i32::MAX}>>` | `10` | `cbtmintimeout` |
| `cbt_initial_timeout` | `IntegerMilliseconds<BoundedInt32<10, {i32::MAX}>>` | `60_000` | `cbtinitialtimeout` |
| `cbt_testing_delay` | `IntegerSeconds<BoundedInt32<1, {i32::MAX}>>` | `10` | `cbttestfreq` |
| `cbt_max_open_circuits_for_testing` | `BoundedInt32<0, 14>` | `10` | `cbtmaxopencircs` |
| `circuit_window` | `BoundedInt32<100, 1000>` | `1_000` | `circwindow` |
| `circuit_priority_half_life` | `IntegerMilliseconds<BoundedInt32<1, {i32::MAX}>>` | `30_000` | `CircuitPriorityHalflifeMsec` |
| `extend_by_ed25519_id` | `BoundedInt32<0, 1>` | `0` | `ExtendByEd25519ID` |
| `guard_meaningful_restriction` | `Percentage<BoundedInt32<1, 100>>` | `20` | `guard-meaningful-restriction-percent` |
| `guard_extreme_restriction` | `Percentage<BoundedInt32<1, 100>>` | `1` | `guard-extreme-restriction-percent` |
| `guard_lifetime_unconfirmed` | `IntegerDays<BoundedInt32<1, 3650>>` | `120` | `guard-lifetime-days` |
| `guard_lifetime_confirmed` | `IntegerDays<BoundedInt32<1, 3650>>` | `60` | `guard-confirmed-min-lifetime-days` |
| `guard_internet_likely_down` | `IntegerSeconds<BoundedInt32<1, {i32::MAX}>>` | `600` | `guard-internet-likely-down-interval` |
| `guard_max_sample_size` | `BoundedInt32<1, {i32::MAX}>` | `60` | `guard-max-sample-size` |
| `guard_max_sample_threshold` | `Percentage<BoundedInt32<1, 100>>` | `20` | `guard-max-sample-threshold` |
| `guard_filtered_min_sample_size` | `BoundedInt32<1, {i32::MAX}>` | `20` | `guard-min-filtered-sample-size` |
| `guard_n_primary` | `BoundedInt32<1, {i32::MAX}>` | `3` | `guard-n-primary-guards` |
| `guard_use_parallelism` | `BoundedInt32<1, {i32::MAX}>` | `1` | `guard-n-primary-guards-to-use` |
| `guard_dir_use_parallelism` | `BoundedInt32<1, {i32::MAX}>` | `3` | `guard-n-primary-dir-guards-to-use` |
| `guard_nonprimary_connect_timeout` | `IntegerSeconds<BoundedInt32<1, {i32::MAX}>>` | `15` | `guard-nonprimary-guard-connect-timeout` |
| `guard_nonprimary_idle_timeout` | `IntegerSeconds<BoundedInt32<1, {i32::MAX}>>` | `600` | `guard-nonprimary-guard-idle-timeout` |
| `guard_remove_unlisted_after` | `IntegerDays<BoundedInt32<1, 3650>>` | `20` | `guard-remove-unlisted-guards-after-days` |
| `min_circuit_path_threshold` | `Percentage<BoundedInt32<25, 95>>` | `60` | `min_paths_for_circs_pct` |
| `sendme_accept_min_version` | `SendMeVersion` | `0` | `sendme_accept_min_version` |
| `sendme_emit_min_version` | `SendMeVersion` | `0` | `sendme_emit_min_version` |
| `unused_client_circ_timeout` | `IntegerSeconds<BoundedInt32<60, 86_400>>` | `1800` (`30*60`) | `nf_conntimeout_clients` |
| `unused_client_circ_timeout_while_learning_cbt` | `IntegerSeconds<BoundedInt32<10, 60_000>>` | `180` (`3*60`) | `cbtlearntimeout` |

Observable behaviour:

- `from_map` is **infallible**. It starts from `NetParameters::default()` and
  applies each recognised key in turn. There is no fallible constructor and no
  `Result` at the boundary.
- A key this client does not recognise is **ignored**: it is neither an error
  nor a reason to discard the keys that *were* recognised. A future consensus
  will carry parameters this release has never heard of, and a client that
  refused such a consensus would be unbootstrappable by the next authority vote.
- A value outside a parameter's permitted range is **clamped** to the nearest
  end of the range — `circwindow: 1` becomes 100, `circwindow: 100000` becomes
  1000, `cbtmaxopencircs: -5` becomes 0, `cbtmaxopencircs: 99` becomes 14 — and
  the rest of the parameter set is still applied. Clamping, not rejection: one
  malformed field must not deny service.
- A parameter absent from the consensus keeps its default from the table above.
  Applying a map that mentions three keys changes those three and nothing else.
- Note the key names are **not** uniform in style: some are lowercase runs
  (`bwweightscale`), some are CamelCase (`ExtendByEd25519ID`,
  `CircuitPriorityHalflifeMsec`), some are hyphenated (`guard-n-primary-guards`),
  some are underscored (`min_paths_for_circs_pct`, `sendme_accept_min_version`).
  They are what the directory authorities publish and must be spelled exactly;
  they are not derived from the field name.
- The two `sendme_*` parameters were named `send_me_*` at the previous
  checkpoint. See `instruction.md`.

### 4.2 `tor_netdir::{SubnetConfig, NetDir, Relay}`

```rust
#[derive(serde::Deserialize, Debug, Clone)]     // NOT Copy, NOT PartialEq
#[serde(deny_unknown_fields)]
pub struct SubnetConfig {
    subnets_family_v4: u8,      // private
    subnets_family_v6: u8,      // private
}

impl Default for SubnetConfig {
    // subnets_family_v4 = 16, subnets_family_v6 = 32
}
```

`SubnetConfig` has **no public constructor and no public methods at all** at
this release — no `new`, no accessors, no `in_same_subnet`. The two ways to make
one are `Default::default()` and deserialization, and the only thing you can do
with one is hand it to `Relay::in_same_subnet` or store it in a `PathConfig`
(§3.3). Do not add a `new(u8, u8)`: the cases in scope build every non-default
configuration through serde, and adding an inherent constructor would not make
them pass.

- The two field names `subnets_family_v4` and `subnets_family_v6` are the
  serialized names and are checked.
- `#[serde(deny_unknown_fields)]` and the absence of `#[serde(default)]` are
  both checked, and they pull in opposite directions from convenience: an unknown
  field is an **error**, and so is an object that omits either width. A typo in
  a torrc must not silently disable the distinct-subnets check, which is exactly
  what a lenient deserializer would do.

The relay side:

```rust
impl<'a> Relay<'a> {
    pub fn id(&self) -> &Ed25519Identity;
    pub fn rsa_id(&self) -> &RsaIdentity;
    pub fn same_relay<'b>(&self, other: &Relay<'b>) -> bool;
    pub fn is_flagged_guard(&self) -> bool;
    pub fn is_dir_cache(&self) -> bool;
    pub fn supports_exit_port_ipv4(&self, port: u16) -> bool;
    pub fn supports_exit_port_ipv6(&self, port: u16) -> bool;
    pub fn policies_allow_some_port(&self) -> bool;
    pub fn in_same_subnet<'b>(&self, other: &Relay<'b>, subnet_config: &SubnetConfig) -> bool;
    pub fn in_same_family<'b>(&self, other: &Relay<'b>) -> bool;
    // plus ipv4_policy / ipv6_policy / ipv4_declared_policy / ipv6_declared_policy / rs / md
}
```

`Relay` also implements `tor_linkspec::ChanTarget`, which is where `addrs()`
comes from; `Relay` has no inherent `addrs` method. There is no `HasAddrs` or
`HasRelayIds` trait at this release (see §2.1).

Observable behaviour:

- `in_same_subnet` compares the **leading `subnets_family_v4` bits** of every
  pair of IPv4 OR addresses, and the leading `subnets_family_v6` bits of every
  pair of IPv6 ones. Two relays are in the same subnet if **any** such pair
  agrees. An IPv4 address and an IPv6 address are never in the same subnet.
- The default `(16, 32)` groups two relays that share a /16. Narrowing to
  `(32, 128)` makes only identical addresses match; widening to `(7, 32)` merges
  addresses that differ in the **lowest** bit of the first octet — in the test
  network, first octets 0 with 1 and 2 with 3, leaving 4 on its own.
- A width **longer than the address** — more than 32 for IPv4, more than 128 for
  IPv6 — yields `false` for every comparison, including a relay against itself.
  It does not panic and does not wrap around. A misconfiguration degrades to
  "nothing is in the same subnet", and the v4 and v6 widths are independent: an
  absurd v6 width leaves the v4 comparison working.
- `in_same_family` is **mutual**: a relay is in its own family, and two distinct
  relays are family only if each one's microdescriptor family names the other's
  RSA identity. A one-sided claim does not count, otherwise any relay could
  claim kinship with an honest one and shrink the path space.
- `NetDir` reaches relays by identity through `by_id`, `by_rsa_id`,
  `by_id_pair`, `by_chantarget` and `relays()`, and exposes its consensus
  parameters through `params()`.

### 4.3 `tor_netdir::testnet` — the synthetic network every fixture is built from

```rust
// in tor-netdir's lib.rs
#[cfg(any(test, feature = "testing"))]
pub mod testnet;
```

Not a convenience. **No requirement group in this step builds a network by hand.**
Seven of them — `guardsel`, `guardpersist`, `guardusage`, `guardreport`,
`subnets`, `pathbuild` and `netparams` — obtain their `NetDir` from this module
and nowhere else, so the *contents* of the network described below are checked
exactly as directly as any signature in this document. Cases ask for "relay 38"
by its Ed25519 identity, assert that relays 0 and 5 share an OR address and
that relays 0 and 1 do not, and read a consensus parameter that only this
network sets. A module that exports the right names over a differently
populated network fails those groups without ever failing to compile.

> **Nothing before this section describes `testnet`, and what a step-01-era
> `testnet` would look like is wrong here.** Step 01's contract — still binding
> at this checkpoint, at `/app/docs/step-01/TEST_INTERFACE.md` — does not
> mention `tor_netdir::testnet`, `construct_netdir`, `NodeBuilders` or the
> `testing` feature anywhere; step 01's driver never enabled the feature and no
> step-01 group used the module. So none of §4.3 overrides an inherited
> statement: this is the first and only description of the module, and it is
> binding in full.
>
> The warning is for the *implementation* rather than the documents. If your
> `tor-netdir` already carries a `testnet` written against the previous
> release's shape, three things about it are now wrong: every relay had the
> single OR address `127.0.0.1:9001`, there was no customization hook (no
> `NodeBuilders`, no `construct_custom_*`), and neither constructor was
> fallible — `construct_netdir()` did exist, but it returned a bare `NetDir`,
> already unwrapped rather than a `PartialNetDir`, and `construct_network()`
> returned a bare tuple rather than a `Result`. The
> address change is the one that fails silently: a network in which all forty
> relays share one address still compiles, still has forty relays with the
> right flags and identities, and then fails `subnets` and `pathbuild`, because
> it has no distinct-subnet structure to observe and no legal three-hop path
> under the default `PathConfig`.

#### 4.3.1 Module surface

```rust
pub fn construct_netdir() -> Result<PartialNetDir>;

pub fn construct_custom_netdir<F>(func: F) -> Result<PartialNetDir>
where
    F: FnMut(usize, &mut NodeBuilders);

pub fn construct_network() -> Result<(MdConsensus, Vec<Microdesc>)>;

pub fn construct_custom_network<F>(func: F) -> Result<(MdConsensus, Vec<Microdesc>)>
where
    F: FnMut(usize, &mut NodeBuilders);

/// The pair of half-built documents for one relay, handed to the callback.
#[derive(Debug, Clone)]
#[non_exhaustive]
pub struct NodeBuilders {
    pub rs: tor_netdoc::doc::netstatus::RouterStatusBuilder<tor_netdoc::doc::microdesc::MdDigest>,
    pub md: tor_netdoc::doc::microdesc::MicrodescBuilder,
    pub omit_md: bool,
    pub omit_rs: bool,
}
```

`Result<T>` here is `tor_netdir::Result<T>` — i.e. `Result<T, tor_netdir::Error>`
— and all four functions are fallible at this release. `MdConsensus` and
`Microdesc` are `tor_netdoc::doc::netstatus::MdConsensus` and
`tor_netdoc::doc::microdesc::Microdesc`.

- `construct_custom_network` builds relay *i* for every *i* in `0..40`, in
  ascending order. For each one it fills a `NodeBuilders` with the defaults
  tabulated in §4.3.2, calls `func(i, &mut nb)` **exactly once**, and only then
  finishes the microdescriptor and folds the routerstatus into the consensus.
  The index the callback receives is a `usize`, and the callback may mutate
  anything reachable through `&mut NodeBuilders`. (The the fixtures rely on
  the call happening once per relay and on the mutation being visible in the
  finished network; `guardsel`'s "exactly one guard" network and `subnets`'
  "one big subnet" network are both built this way.)
- Setting `omit_rs` drops that relay's routerstatus from the consensus and
  setting `omit_md` drops its microdescriptor from the returned vector. The
  fixtures in this step leave both `false`, but they are part of the struct and
  `NodeBuilders` is `#[non_exhaustive]`, so it cannot be built by a struct
  literal from outside the crate in any case.
- `construct_network()` and `construct_netdir()` are the same two functions
  called with a callback that does nothing.
- `construct_custom_netdir` additionally wraps the consensus with
  `PartialNetDir::new(consensus, None)` and adds **every** returned
  microdescriptor before returning, so the value it hands back is immediately
  ready for `unwrap_if_sufficient()` (§0) and that call succeeds on the stock
  network.

#### 4.3.2 The forty relays

Relay *i* runs over `i` in `0..40`. Most values below are observable through
`NetDir`, and at least one of those is asserted by a case in scope. Two are not:
the TAP onion key, for which `Microdesc` has no accessor and whose only route
would be `Relay::md()`, gated behind `experimental-api` (which the driver does
not enable, §0); and the consensus method, which no `NetDir` accessor exposes.
Both are listed because the fixture still has to supply them.

| Property | Value for relay *i* |
| --- | --- |
| count | 40, indices 0..=39 |
| Ed25519 identity | the 32 bytes `[i; 32]` — `NetDir::by_id` is keyed on this |
| RSA identity | the 20 bytes `[i; 20]` |
| flags | `RUNNING \| VALID \| V2DIR` for all 40, plus `EXIT` for 10..=19, `GUARD` for 20..=29, and `EXIT \| GUARD` for 30..=39 |
| OR address | exactly one: `SocketAddr` `((i % 5), 0, 0, 3) : 9001`, i.e. `0.0.0.3:9001` through `4.0.0.3:9001` |
| IPv4 exit policy | `accept 80,443` if the relay is an exit and *i* is odd; `accept 1-65535` if it is an exit and *i* is even; `reject 1-65535` if it is not an exit |
| IPv6 exit policy | reject-all, for every relay without exception |
| family | one entry, the hex encoding of the 20 bytes `[i ^ 1; 20]` |
| subprotocols | `DirCache=2` if *i* is even, the empty protocol list if *i* is odd |
| bandwidth weight | `RelayWeight::Measured(1000 * (i % 10 + 1))`, i.e. 1000…10000 within each block of ten |
| TAP onion key | a fixed 1024-bit RSA key, the same one for all 40 |
| ntor onion key | the 32 ASCII bytes `----nothing in dirmgr uses this-` |

Consensus-level, set once for the whole document:

| Property | Value |
| --- | --- |
| consensus method | 34 |
| lifetime | valid from `SystemTime::now()`, fresh-until `+12h`, valid-until `+24h` |
| voted parameters | exactly one: `bwweightscale` = **1** |
| bandwidth weights | the empty weight set (`"".parse()`) — no flag-based weighting |

Five consequences are what the cases in scope actually observe, and each one is
the reason a value above is what it is:

- **Addresses collide in a fixed pattern.** Relays *i* and *i*+5 share an OR
  address outright, and there are five distinct /16s across the network. Under
  the default `SubnetConfig` (§4.2) relays 0 and 5 are one subnet and relays 0
  and 1 are not; under a 7-bit prefix `0.0.0.3` and `1.0.0.3` merge. Having
  more than one /16 is also what lets a three-hop exit path exist at all: the
  default `PathConfig` enforces distinct subnets between hops, so a network
  whose relays all share a single /16 yields no path — which is exactly the
  network `subnets` builds in order to observe the refusal.
- **Families are adjacent pairs and are mutual.** Relay *i* names `[i ^ 1; 20]`
  and relay `i ^ 1` names `[i; 20]`, so `Relay::in_same_family` is true for
  (0,1), (2,3), … in both directions and false for (0,2). Nothing else in the
  network is family with anything.
- **Only even relays are directory caches.** `Relay::is_dir_cache()` is
  `V2DIR` **and** `DirCache=2`; every relay has the flag but only even relays
  advertise the subprotocol. A fixture that needs the network's sole guard to
  also be usable as a one-hop directory hop therefore has to pick an
  even-numbered relay — the the fixtures use 36 and 38.
- **Every exit accepts 443 over IPv4 and none exits over IPv6.** Both exit
  policies above allow 443; the IPv6 policy is reject-all for all 40. A request
  for an IPv6 exit must be refused rather than silently answered over IPv4.
- **`bwweightscale` is 1 in this consensus, not its 10 000 default.** That is
  how `netparams` tells a `NetParameters` that was actually populated from a
  consensus apart from one left at its defaults (§4.1).

#### 4.3.3 What the callback reaches through `NodeBuilders`

The the fixtures touch two methods on `nb.rs`, and both are
`tor_netdoc::doc::netstatus::RouterStatusBuilder<D>` methods available when the
`build_docs` feature is on:

```rust
impl<D: Clone> RouterStatusBuilder<D> {
    pub fn identity(&mut self, identity: RsaIdentity) -> &mut Self;
    pub fn add_or_port(&mut self, addr: std::net::SocketAddr) -> &mut Self;
    pub fn doc_digest(&mut self, doc_digest: D) -> &mut Self;
    pub fn set_flags(&mut self, flags: RelayFlags) -> &mut Self;
    pub fn add_flags(&mut self, flags: RelayFlags) -> &mut Self;
    pub fn protos(&mut self, protos: tor_protover::Protocols) -> &mut Self;
    pub fn weight(&mut self, weight: RelayWeight) -> &mut Self;
    // also nickname(String), published(SystemTime), dir_port(u16), version(String)
}
```

Every setter takes `&mut self` and returns `&mut Self`, so a callback that
receives `&mut NodeBuilders` can call them in a chain without moving anything.
Two distinctions are checked because a fixture depends on each:

- `set_flags` **replaces** the whole flag set; `add_flags` unions into it. The
  one-guard fixture calls `set_flags` and relies on the replacement, so a
  `set_flags` that unions turns every relay in `20..=29` into a guard and the
  guard groups stop being deterministic.
- `add_or_port` **appends** to the relay's address list rather than replacing
  it. `subnets` builds its "one big subnet" network by adding a second address
  in `10.0.0.0/16` to every relay while the first address stays; if
  `add_or_port` replaced, the network would keep five distinct /16s and a path
  would still be found.

`RelayFlags` is a `bitflags` type at `tor_netdoc::doc::netstatus::RelayFlags`,
public and constructible by the driver. The constants a fixture names are
`RUNNING`, `VALID`, `V2DIR`, `GUARD` and `EXIT`; the fixtures compose them with
`|` and hand the result to `set_flags`. No case in this step calls
`RelayFlags::contains`. `RelayWeight` is
`tor_netdoc::doc::netstatus::RelayWeight`, with the two variants
`Unmeasured(u32)` and `Measured(u32)`.

`nb.md` is a `tor_netdoc::doc::microdesc::MicrodescBuilder` with the same
`&mut self -> &mut Self` shape (`tap_key`, `ntor_key`, `ed25519_id`, `family`,
`ipv4_policy`, `ipv6_policy`, and the `parse_*` variants that return
`Result<&mut Self>`). No case in scope in this step mutates it; it is named here
because it is a public field of a struct the driver receives.

---

## 5. `tor-socksproto` — name lookup over SOCKS

### 5.1 `tor_socksproto::{SocksHandshake, Action, SocksRequest, SocksVersion, SocksCmd, SocksStatus, SocksAddr, SocksHostname, SocksAuth, Error}`

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum SocksVersion { V4, V5 }
// u8::from(SocksVersion) -> 4 / 5 ; TryFrom<u8> -> Err(Error::BadProtocol(v))

caret::caret_int! {
    pub struct SocksCmd(u8) {
        CONNECT = 1,
        BIND = 2,
        UDP_ASSOCIATE = 3,
        RESOLVE = 0xF0,          // Tor only
        RESOLVE_PTR = 0xF1,      // Tor only
    }
}

caret::caret_int! {
    pub struct SocksStatus(u8) {
        SUCCEEDED = 0x00, GENERAL_FAILURE = 0x01, NOT_ALLOWED = 0x02,
        NETWORK_UNREACHABLE = 0x03, HOST_UNREACHABLE = 0x04,
        CONNECTION_REFUSED = 0x05, TTL_EXPIRED = 0x06,
        COMMAND_NOT_SUPPORTED = 0x07, ADDRTYPE_NOT_SUPPORTED = 0x08,
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
#[allow(clippy::exhaustive_enums)]
pub enum SocksAddr { Hostname(SocksHostname), Ip(std::net::IpAddr) }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SocksHostname(String);        // field private; TryFrom<String>, AsRef<str>, Into<String>

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum SocksAuth { NoAuth, Socks4(Vec<u8>), Username(Vec<u8>, Vec<u8>) }

#[derive(Clone, Debug)]
#[non_exhaustive]
pub struct Action { pub drain: usize, pub reply: Vec<u8>, pub finished: bool }

#[derive(Clone, Debug)]
pub struct SocksHandshake { /* private */ }
impl SocksHandshake {
    pub fn new() -> Self;
    pub fn handshake(&mut self, input: &[u8]) -> Result<Action, Error>;
    pub fn finished(&self) -> bool;
    pub fn into_request(self) -> Option<SocksRequest>;
}

#[derive(Clone, Debug)]
pub struct SocksRequest { /* private */ }
impl SocksRequest {
    pub fn version(&self) -> SocksVersion;
    pub fn command(&self) -> SocksCmd;
    pub fn auth(&self) -> &SocksAuth;
    pub fn port(&self) -> u16;
    pub fn addr(&self) -> &SocksAddr;
    pub fn reply(&self, status: SocksStatus, addr: Option<&SocksAddr>) -> Vec<u8>;
}

#[derive(thiserror::Error, Debug)]
#[non_exhaustive]
pub enum Error {
    Truncated, Syntax, BadProtocol(u8), NoSupport, AlreadyFinished, Internal,
}
```

Observable behaviour:

- **The step's substantive change is the port rule.** `SocksRequest`'s
  constructor refuses `port == 0` only for the commands that *require* a port —
  `CONNECT`, `BIND`, `UDP_ASSOCIATE`. `RESOLVE` and `RESOLVE_PTR` carry no port
  at all, so port 0 is their normal case. A handshake that insists on a nonzero
  port for every command rejects every name lookup, which is the regression this
  group is here to catch. `CONNECT` to port 0 must still be `Error::Syntax`.
- `RESOLVE` (0xF0) and `RESOLVE_PTR` (0xF1) are Tor extensions rather than part
  of SOCKS (see `SPEC_NOTES.md`). Both command numbers are checked, and both
  arrive on **SOCKS4a and SOCKS5**. An unrecognised command number is
  `Error::NoSupport`; `BIND` and `UDP_ASSOCIATE` are recognised names but are
  also `Error::NoSupport`, because naming a command is not implementing it.
- `SocksCmd` and `SocksStatus` come from `caret_int!`, which gives each an
  exhaustive `u8` round trip in both directions (`u8::from(cmd)`,
  `SocksCmd::from(byte)` — total, since any `u8` is a possible command on the
  wire), plus a `Display` that prints the **constant's own name**. This is a
  canonical rendering of a protocol constant, not a diagnostic message:
  `SocksCmd::RESOLVE.to_string()` is `"RESOLVE"` and
  `SocksCmd::RESOLVE_PTR.to_string()` is `"RESOLVE_PTR"`, and those two
  spellings are checked. (`Error`'s `Display` is a diagnostic and is **not**
  checked, here or anywhere in this step.)
- `SocksVersion` converts to `u8` infallibly and back through `TryFrom`, where
  any byte other than 4 or 5 yields `Error::BadProtocol(byte)` **carrying the
  offending byte**. The same error reaches `SocksHandshake::handshake` when a
  connection opens with a bad version byte, which is how a proxy tells "this is
  not SOCKS" from "this is malformed SOCKS".
- `handshake` returns an `Action` whose three `pub` fields are the whole
  protocol contract for one step: `drain` is how many input bytes were consumed,
  `reply` is what to write back (**empty** when there is nothing to say yet),
  and `finished` says whether the handshake is over. A SOCKS5 greeting of
  `[5, 1, 0]` drains 3, replies `[5, 0]` — version 5, method 0, "no
  authentication" — and is **not** finished. The request that follows drains its
  own length, replies with nothing, and **is** finished.
- After `finished()`, `into_request()` yields the `SocksRequest`. Calling
  `handshake` again is `Error::AlreadyFinished`.
- **Three boundary rules of the state machine are checked, and each is decided by
  the byte stream rather than by the RFC's prose.** None of them is a message
  check on your own diagnostics:

  1. **The reserved byte is ignored, not checked.** The third byte of a SOCKS5
     request (`VER, CMD, RSV, ATYP, …`) is reserved in RFC 1928 and "must be
     0x00", but the case in scope sends `0xAB` there and requires the request to
     be **accepted**. A handshake that refuses a nonzero reserved byte fails the
     gate.
  2. **Auth sub-negotiation has exactly one version.** Once a SOCKS5 greeting
     selects method 2 (username/password), the next message's first byte is the
     sub-negotiation version and only version 1 is defined, so a message whose
     first byte is anything else is **not a credentials message at all** and is
     `Error::Syntax`. Sending the *request* before the credentials is equally
     out of order and is also `Error::Syntax`. A greeting that offers no method
     this implementation supports is refused outright with `Error::NoSupport`.
  3. **A failure is terminal, and it does not discard the request.** A handshake
     that has returned an error is dead: `finished()` is false, and every later
     `handshake` call returns `Error::AlreadyFinished` rather than giving the
     peer a second attempt. The same holds for input that arrives after a
     *completed* handshake — that is `AlreadyFinished` too, and it also clears
     `finished()` — but the `SocksRequest` already negotiated survives both:
     `into_request()` still returns it after the failure. Only a zero-length
     read is exempt: an empty slice is `Error::Truncated` and leaves the
     handshake exactly as it was, so a *completed* handshake stays
     `finished()` after one.
- `SocksRequest::reply` frames the answer, and the framing differs by version:

| Version | Success | Failure | Shape |
| --- | --- | --- | --- |
| SOCKS5 | status byte `0x00` | the `SocksStatus` byte itself | `[5, status, 0, atype, addr..., port_hi, port_lo]` |
| SOCKS4 | `0x5A` | `0x5B` (every non-success status) | `[0, code, port_hi, port_lo, a, b, c, d]`; the checked RESOLVE case carries port 0, so those two bytes come out `0, 0` |

  With `addr: None` a SOCKS5 reply names the unspecified IPv4 address
  (`0.0.0.0`, address type 1). SOCKS4 has only the two outcomes, so every
  failure status collapses to `0x5B`; a reply that tried to carry SOCKS5's
  richer status set over SOCKS4 would be unparsable by a SOCKS4 client.
- `SocksAddr` may be a hostname or an IP address, and the handshake round-trips
  each form. `SocksAddr`'s `Display` is the address itself — the hostname's text
  or the `IpAddr`'s own rendering, with no decoration — which is again a
  canonical rendering rather than a diagnostic, and is checked.
- `SocksHostname` wraps a `String` whose length is limited to **255 bytes**,
  because that is the largest length the SOCKS5 wire format can express.
  `TryFrom<String>` is the only public constructor and refuses a longer name
  with `Error::Syntax`.
- `SocksAuth::Socks4` carries the SOCKS4 username, which is the isolation signal
  (§3.1 and `SPEC_NOTES.md`): two requests with different usernames must not be
  treated as interchangeable. A SOCKS4a request with an empty username still
  parses; the username field is present-but-empty, not absent.
- `Error::Truncated` is **recoverable** — the caller should read more bytes and
  retry — and is distinguished from `Error::Syntax`, which is not. Conflating
  them would make a proxy drop connections that merely arrived in two packets.
  `tor_bytes::Error::Truncated` maps to `Error::Truncated` and every other
  `tor_bytes::Error` maps to `Error::Syntax`.
- The inherited `socksconnect` group re-checks step 01's CONNECT path against
  the carried workspace.

---

## 6. `tor-cell` — RESOLVE and RESOLVED cells

### 6.1 `tor_cell::relaycell::msg::{Resolve, Resolved, ResolvedVal}`

```rust
#[derive(Debug, Clone)]
pub struct Resolve { /* private: query: Vec<u8> */ }
impl Resolve {
    pub fn new(s: &str) -> Self;
    pub fn new_reverse(addr: &std::net::IpAddr) -> Self;
}

#[derive(Debug, Clone, Eq, PartialEq)]
#[non_exhaustive]
pub enum ResolvedVal {
    Ip(IpAddr),
    Hostname(Vec<u8>),
    TransientError,
    NontransientError,
    Unrecognized(u8, Vec<u8>),
}
// impl Readable for ResolvedVal { fn take_from(&mut Reader) -> Result<Self> }
// impl Writeable for ResolvedVal { fn write_onto<B: Writer + ?Sized>(&self, &mut B) }

#[derive(Debug, Clone)]
pub struct Resolved { /* private: answers: Vec<(ResolvedVal, u32)> */ }
impl Resolved {
    pub fn new_empty() -> Self;
    pub fn new_err(transient: bool, ttl: u32) -> Self;
    pub fn add_answer(&mut self, answer: ResolvedVal, ttl: u32);
    pub fn into_answers(self) -> Vec<(ResolvedVal, u32)>;
}
```

Both implement `Body`, so both have `into_message()`, `decode_from_reader()`
and `encode_onto()`, and both travel inside a `RelayCell` (§6.2).

**The RESOLVE query.** `Resolve` puts the query on the wire as raw bytes
followed by a single **NUL terminator**, and nothing after it. `Resolve::new`
takes the name as given. `Resolve::new_reverse` builds the PTR query itself,
and the construction is checked because a client that gets it wrong asks the
network a different question than the user did:

| Address | Query |
| --- | --- |
| `1.2.3.4` | `4.3.2.1.in-addr.arpa` — the octets in **reverse** order, decimal |
| an IPv6 address | 32 hex nybbles under `ip6.arpa`, **least significant first**, each its own label: for each octet from last to first, the low nybble then the high nybble |

  For `2001:db8::42` the last octet is `0x42`, so the query begins `2.4.0.0.`
  and is exactly `32 * 2 + len("ip6.arpa")` bytes long.

**The RESOLVED answer encoding.** Each answer is `[type, length, payload...]`:

| `ResolvedVal` | Type byte | Length byte | Payload |
| --- | --- | --- | --- |
| `Hostname(h)` | `0x00` | `h.len()` | the name's bytes |
| `Ip(V4)` | `0x04` | `4` | four octets |
| `Ip(V6)` | `0x06` | `16` | sixteen octets |
| `TransientError` | `0xF0` | `0` | none |
| `NontransientError` | `0xF1` | `0` | none |
| `Unrecognized(t, v)` | `t` | `v.len()` | `v` verbatim |

Observable behaviour:

- `Resolved` carries **zero or more** `(ResolvedVal, u32)` pairs, in the order
  they were added, each with its own TTL in seconds. The order and the TTLs are
  checked: answers are not a set.
- A `Resolved` with zero answers encodes and decodes successfully — it is the
  "no such name" answer, not a malformed cell.
- `Resolved::new_err(transient, ttl)` produces a **one-element** answer list
  holding `TransientError` or `NontransientError` with that TTL. An error is an
  answer, not the absence of one.
- The two error values are distinct because the spec makes them distinct
  (tor-spec §6.4, answer types `0xF0` and `0xF1`): one says "retry could
  succeed", the other says "it never will".
- `Unrecognized(u8, Vec<u8>)` preserves an answer type this version does not
  know, rather than discarding it or failing the whole cell. A client that
  rejected unknown answer types could not survive a protocol extension. Only
  types `0`, `4`, `6`, `0xF0` and `0xF1` are recognised; everything else,
  including a type whose payload is empty, becomes `Unrecognized`.
- **Only the two IP types have a fixed length.** When decoding, a type-4 answer
  whose length byte is not 4, or a type-6 answer whose length byte is not 16, is
  rejected with `tor_bytes::Error::BadMessage`. Types `0`, `0xF0`, `0xF1` and
  any unrecognised type take their length from the length byte, so there is
  nothing to disagree with. The case in scope matches on the **variant**; the
  `&'static str` inside `BadMessage` is a diagnostic and is out of scope.
- `ResolvedVal` is `Eq`/`PartialEq`, so the cases in scope compare decoded answers
  to expected values directly rather than through any rendering.
- The inherited `resolvequery` group re-checks step 01's relay-message surface
  against the carried workspace. **`datacell` is a requirement group at this
  step**, not a gate group: §6.3 is the reason.

### 6.2 `tor_cell::relaycell::{RelayCell, RelayCmd, StreamId}`

```rust
#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub struct StreamId(u16);                      // field private
// From<u16>, From<StreamId> for u16, Display (prints the number)
impl StreamId {
    pub fn is_zero(&self) -> bool;
}

#[derive(Debug)]
pub struct RelayCell { /* private */ }
impl RelayCell {
    pub fn new(streamid: StreamId, msg: msg::RelayMsg) -> Self;
    pub fn into_streamid_and_msg(self) -> (StreamId, msg::RelayMsg);
    pub fn cmd(&self) -> RelayCmd;
    pub fn msg(&self) -> &msg::RelayMsg;
    pub fn encode<R: Rng + CryptoRng>(self, rng: &mut R) -> crate::Result<RawCellBody>;
    pub fn decode(body: RawCellBody) -> Result<Self>;
    pub fn decode_from_reader(r: &mut Reader<'_>) -> Result<Self>;
}

impl RelayCmd {                                 // a caret_int! over u8
    pub fn accepts_streamid_val(self, id: StreamId) -> bool;
}
```

Observable behaviour:

- A `RelayCell` is exactly a `(StreamId, RelayMsg)` pair. `cmd()` is the
  message's own command; `into_streamid_and_msg()` takes it apart again, and
  both halves must survive an `encode`/`decode` round trip unchanged.
- `encode` produces a **509-byte** padded body: an 11-byte relay header
  (command, two "recognized" bytes, two stream-ID bytes, four digest bytes, two
  length bytes) followed by the message body, then **four zero bytes**, then
  random padding from the supplied `Rng`. The four zeroes before the padding
  are checked; padding that started immediately after the body would make the
  body's end guessable from the ciphertext.
- **Those four zeroes come out of the padding, not out of the body budget.**
  The 509 bytes are a fixed-size buffer that starts zeroed; the encoder writes
  header-plus-body into it and then fills from `body_end + 4` to the end with
  random bytes, skipping the fill entirely when four *or fewer* bytes are left
  (the guard is `enc_len < 509 - 4`).
  So the largest body a relay cell can carry is `509 - 11 = 498`, **not**
  `509 - 11 - 4`, and a maximal body simply leaves no padding at all. That 498
  is what §6.3 publishes as `Data::MAXLEN`; the two sections agree.
- `accepts_streamid_val` is the **only public** stream-ID predicate on
  `RelayCmd`. It classifies each command as wanting a nonzero ID, wanting zero,
  or accepting either, and applies that classification to a concrete
  `StreamId`. Whatever helper computes the classification is **private**: there
  is no public `expects_streamid` and no public `StreamIdReq` type at this
  release, and the driver never names either. Both `RESOLVE` and `RESOLVED` are
  classified as **wanting a nonzero ID**, and a RESOLVE/RESOLVED exchange
  carries the same nonzero ID in both directions (tor-spec §6.4). The predicate
  itself is checked by step 01's `relaycmd` group, whose contract remains
  binding; no step-02 case calls it.
- **`RelayCell::encode` and `RelayCell::decode` do not consult that
  classification.** At this release the check is available to callers as a
  predicate and is applied by the circuit reactor, not by the codec: building a
  `RelayCell` with a mismatched stream ID and encoding it succeeds. That is
  what the `resolvedcell` group relies on — it round-trips a `RESOLVED` on
  stream ID 0 and requires the encode to succeed. This is a real and deliberate
  division of labour: the codec's job is framing, the reactor's job is protocol
  policy. A client that made `encode` or `decode` reject the pairing — by
  consulting `accepts_streamid_val` inside the codec — would fail the checked
  round trips.
- `StreamId` is a transparent `u16` in both directions, `Copy`, `Hash`, and
  `Display`s as the bare number. `is_zero()` is the circuit-wide-message test.

### 6.3 `tor_cell::relaycell::msg::Data`

```rust
#[derive(Debug, Clone)]
pub struct Data { /* private: body: Vec<u8> */ }

impl Data {
    pub const MAXLEN: usize = 498;            // CELL_DATA_LEN (509) - 11
    pub fn new(inp: &[u8]) -> crate::Result<Self>;
    pub fn split_from(inp: &[u8]) -> (Self, &[u8]);
}
impl From<Data> for Vec<u8> { /* moves the body out */ }
impl AsRef<[u8]> for Data { /* borrows the body */ }
impl Body for Data { /* into_message / decode_from_reader / encode_onto */ }
```

`datacell` is a **requirement** group: the milestone's requirement is that an
oversized body be *reported* rather than *asserted*, and that is a behavioural
change at this checkpoint, not a re-check of step 01.

Observable behaviour:

- `Data::MAXLEN` is **498** — the 509-byte relay cell payload minus its 11-byte
  header — and is a public `const`, so a caller can size its own buffers without
  rediscovering the arithmetic.
- `Data::new` accepts a body of **exactly** `MAXLEN` bytes and refuses one byte
  more with **`Err(tor_cell::Error::CantEncode)`**. It must not panic, must not
  assert, and must not silently truncate: the input comes from an application's
  `write()` and is entirely outside the client's control, so a length check that
  aborted the process would be a remote denial of service. A 5000-byte input
  behaves the same way as a 499-byte one. No `Display` text of `tor_cell::Error`
  is checked.
- `Data::split_from` **never fails**. It takes `min(len, MAXLEN)` bytes into one
  message and returns the untouched remainder, so a caller drains a large buffer
  by looping. Splitting 1200 bytes yields 498 + 498 + 204 and an empty
  remainder; splitting a body of `MAXLEN` or shorter — **including an empty
  one** — yields the whole input and an empty remainder.
- `Vec::<u8>::from(data)` and `data.as_ref()` see the same bytes; the first
  consumes, the second borrows. Those are the two the cases in scope use — not
  the only two that exist, since the public `Body::encode_onto` writes the same
  bytes out.
- A `Data` inside a `RelayCell` has `cmd() == RelayCmd::DATA` (command byte 2)
  and its encoded length field is the body length: 5 for `b"hello"`, `0x01F2`
  (498) for a full body.

---

## 7. `arti-client` — local address policy and configuration loading

### 7.1 `arti_client::{TorAddr, TorAddrError, IntoTorAddr, DangerouslyIntoTorAddr, Error}` and `arti_client::config::{ClientAddrConfig, ClientAddrConfigBuilder}`

A client run as a SOCKS proxy is handed target addresses by untrusted local
programs. It must distinguish a name from a literal address, never resolve the
name itself, and refuse to reach back into the local network unless explicitly
configured to. That is the whole of this group.

```rust
pub trait IntoTorAddr {
    fn into_tor_addr(self) -> Result<TorAddr, TorAddrError>;
}
pub trait DangerouslyIntoTorAddr {
    fn into_tor_addr_dangerously(self) -> Result<TorAddr, TorAddrError>;
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct TorAddr { /* private: host, port */ }

impl TorAddr {
    pub fn from<A: IntoTorAddr>(addr: A) -> Result<Self, TorAddrError>;
    pub fn dangerously_from<A: DangerouslyIntoTorAddr>(addr: A) -> Result<Self, TorAddrError>;
    pub fn is_ip_address(&self) -> bool;
}
impl std::str::FromStr for TorAddr { type Err = TorAddrError; }
impl std::fmt::Display for TorAddr { /* see below */ }

#[derive(Debug, thiserror::Error, Clone)]
#[non_exhaustive]
pub enum TorAddrError { InvalidHostname, NoPort, BadPort }
```

`TorAddr` has exactly **three** public methods. Everything else about it is
reached through the two traits, `FromStr` and `Display`. In particular the
predicate that applies the policy to a parsed address — `enforce_config` — is
**`pub(crate)`** at this checkpoint, as is `into_string_and_port`. What is
checked here is the public half: the parse, the policy object, and the refusals
the policy produces.

Implementors, exactly:

| Trait | Implemented for |
| --- | --- |
| `IntoTorAddr` | `TorAddr`, `&A where A: IntoTorAddr + Clone`, `&str`, `String`, `(&str, u16)`, `(String, u16)` |
| `DangerouslyIntoTorAddr` | `&T where T: DangerouslyIntoTorAddr + Clone`, `(IpAddr, u16)`, `(Ipv4Addr, u16)`, `(Ipv6Addr, u16)`, `SocketAddr`, `SocketAddrV4`, `SocketAddrV6` |

The split is the point. The **safe** trait is implemented only for things that
still carry a name, so a caller who has a name keeps it and the exit resolves
it. The **dangerous** trait is implemented only for things that are already
numeric — a caller who has one of those has already resolved the name locally,
leaking the lookup to the local resolver, which is why the method says so in
its name. `TorAddr::from` and `TorAddr::dangerously_from` are the same two
doors named as constructors, and `IntoTorAddr for TorAddr` is the identity.

Observable behaviour:

- **Parsing a string.** The split is on the **last colon**. The text before it
  is the host; the text after it is the port. If it parses as an `IpAddr` the
  address is a literal (`is_ip_address()` is true), otherwise it is a hostname.
- The parse **judges nothing else**. An empty host parses (`":443"` is a
  hostname address). A hostname that could never be valid still parses — at
  this release `TorAddrError::InvalidHostname` is **never produced by parsing
  at all**; it exists in the enum and is reachable only by constructing it. The
  validity check lives behind `enforce_config` and surfaces as
  `arti_client::Error::InvalidHostname` instead. Do not make the parser reject
  hostnames: a case in scope parses `"example.com:443"` and several odd-looking
  hosts and expects success.
- Only the port can make a parse fail, and the two ways it can fail are
  distinct: **no colon at all** is `TorAddrError::NoPort`, and a colon followed
  by something that is not a `u16` is `TorAddrError::BadPort`. An **empty**
  port, a non-numeric port (`"example.com:http"`), and a port above 65535 are
  all `BadPort` — the port was found, it was just not a port. A bracketed IPv6
  literal with a bad port is likewise `BadPort` and not a host error, because
  the split happens before the host is examined. Checked by **variant**; the
  `Display` strings are diagnostics and are out of scope.
- **`Display` brackets IPv6 and nothing else.** `TorAddr`'s rendering is
  `host:port`, except that an IPv6 *literal* is written `[addr]:port`. The
  brackets are mandatory there and forbidden elsewhere: an IPv6 address
  contains colons of its own, so without them the rendering could not be parsed
  back. `"[2001:db8::42]:80"` round-trips to itself; a hostname or an IPv4
  literal renders with no brackets. This is a canonical rendering, not a
  diagnostic message, and it **is** checked.
- A host and a port supplied separately name the same target as the
  colon-joined string, and an unbracketed IPv6 literal in the tuple form is
  still recognised as a literal and rendered back **with** brackets.
- `TorAddr` is `Eq`/`PartialEq`, so equality — not a rendering — is what the
  cases in scope use to compare two routes to the same address.

```rust
#[derive(Debug, Clone, derive_builder::Builder, serde::Deserialize)]
pub struct ClientAddrConfig {
    #[builder(default)]
    pub(crate) allow_local_addrs: bool,        // default false
}
impl Default for ClientAddrConfig { /* = ClientAddrConfigBuilder::default().build().unwrap() */ }
```

- `allow_local_addrs` defaults to **false**. Connections to local addresses are
  refused by default because exits reject them anyway, and because permitting
  them turns the client into a probe of the operator's own network.
- The field is `pub(crate)` with no accessor, so the derived `Debug` is the only
  public observation channel. Every case in scope asserts that the rendering
  **contains** `allow_local_addrs: false` (or `true`), and nothing more: the
  field name and the value it carries are pinned; the struct's name, the brace
  spacing the derive produces, and any further private field you keep are not.
  The same containment check is applied to a whole `TorClientConfig` built with
  `TorClientConfig::with_directories(state, cache)`, so the safe default has to
  survive composition into the client-wide config.

```rust
impl TorClientConfig {
    pub fn with_directories<P, Q>(state_dir: P, cache_dir: Q) -> Result<Self>
    where
        P: Into<std::path::PathBuf>,
        Q: Into<std::path::PathBuf>;
}
// `Result` here is `arti_client`'s own alias, i.e.
// `std::result::Result<TorClientConfig, arti_client::Error>`.
```

- `TorClientConfig` is reachable at **`arti_client::TorClientConfig`** (the
  config module's type, re-exported at the crate root) and it is the **root**
  path the driver imports — `arti_client::config::TorClientConfig` is not named
  by any case. Every other configuration option keeps its default, which is why
  the case can assert the address policy on a config built this way.
- `with_directories` is **fallible**: it returns a `Result`, because building
  the directory-manager half from the cache path can fail. It takes the two
  directories by value as any `Into<PathBuf>`, not by reference, so
  `&Path`-typed parameters and an infallible `-> Self` both fail the whole
  `localaddr` group to compile. The call in scope is
  `with_directories(dir.path().join("state"), dir.path().join("cache")).expect(…)`
  — two owned `PathBuf`s, the result unwrapped.
- `ClientAddrConfigBuilder::default().build()` succeeds with nothing set and
  agrees with `ClientAddrConfig::default()`. Naming the option is the only way
  to turn it on.

```rust
#[derive(thiserror::Error, Debug)]
#[non_exhaustive]
pub enum Error {
    CircMgr(#[from] tor_circmgr::Error),
    DirMgr(#[from] tor_dirmgr::Error),
    Proto(#[from] tor_proto::Error),
    Persist(#[from] tor_persist::Error),
    Timeout,
    OnionAddressNotSupported,
    Address(#[from] crate::address::TorAddrError),
    InvalidHostname,
    LocalAddress,
    Internal(&'static str),
    Configuration(String),
}
```

- The three address refusals are **separate variants**, and that separation is
  what is checked: `LocalAddress` for a target inside the local network,
  `InvalidHostname` for a name that can never be a hostname, and
  `OnionAddressNotSupported` for a `.onion` name — refused as *unsupported*
  rather than as *malformed*, so a caller can tell "not yet" from "never".
  Collapsing any two of them into one variant is the defect this case catches.
- A parse failure is **carried, not flattened**: `TorAddrError` converts into
  `Error::Address(_)` through `#[from]`, keeping the original variant inside.
- "Local" means loopback or RFC-1918 private for IPv4, loopback for IPv6, and
  the name `localhost` compared case-insensitively.
- No `Display` text of `arti_client::Error` is checked.

### 7.2 `tor_config::{load, CmdLine}`

```rust
pub fn load<P: AsRef<Path>>(
    cfg: &mut config::Config,
    files: &[(P, bool)],          // (path, required)
    opts: Vec<String>,            // command-line TOML lines
) -> Result<(), config::ConfigError>;

#[derive(Debug, Clone)]
pub struct CmdLine { /* private */ }
impl Default for CmdLine { /* = new() */ }
impl CmdLine {
    pub fn new() -> Self;
    pub fn push_toml_line(&mut self, line: String);
}
impl config::Source for CmdLine { /* so `Config::merge(cmdline)` works */ }

pub fn default_config_file() -> Option<PathBuf>;
```

Observable behaviour:

- `load` **mutates** the `config::Config` it is given and returns `Result<(),
  _>`; it does not return a config. Every file is merged in **the order given**,
  then a `CmdLine` built from `opts` is merged last. The last source to mention
  a key wins, so the precedence is command line over later file over earlier
  file, and a key that a later source is silent about keeps the earlier value.
- The `bool` beside each path is **`required`**. A missing **required** file is
  an `Err`, and the error names the path — a user who mistypes `--config` must
  not be told the client started with defaults. A missing **optional** file
  contributes nothing and is not an error. Note the direction: `true` means
  required.
- When `load` returns `Err`, the caller's `Config` is left without the values
  it failed to merge.
- Each command-line option is a **line of TOML**, so `hello.world=x` sets a
  nested key. As a convenience, an unquoted bareword on the right of `=` is
  quoted before parsing, which is why `hello.world=overridden` works without
  shell quoting. A digit-only bareword is still a bareword: `hello.friends=12345`
  is rewritten to `hello.friends="12345"` and stored as a **string**, and the
  case in scope reads `12345` back only because `get_int` coerces it. Values the
  bareword pattern cannot match — quoted strings, arrays, anything with a `.`
  or `-` on the right-hand side — are passed through as-is.
- An option that is not TOML at all is an error whose message **quotes the
  offending option**, rather than reporting a line and column in a file that
  does not exist. Only that containment is checked, not the wording around it.
- `CmdLine` is the building block `load` uses and is usable on its own: it is a
  `config::Source`, so `cfg.merge(cmdline)` works, and an empty `CmdLine`
  merges successfully and contributes nothing.
- `default_config_file` is **declared, out of scope**: it exists at the module
  root with that signature, but the path it returns is a property of the host —
  environment variables and the user's home directory — so asserting anything
  about it would be a platform test rather than a contract test, and no case
  calls it. (The same reasoning is why nothing in this step contacts the
  network; see §10.)

---

## 8. `tor-units` and `retry-error`

Both crates are small, but they are the mechanism behind §4.1's clamping and
behind the way a failed circuit request reports several causes rather than one.
They are requirement groups because nothing in this step's milestone
is *about* them; they are in scope because a wrong bound or a wrong time
scale is silent everywhere else.

### 8.1 `tor_units`

Everything here is in the crate root: `tor_units::{BoundedInt32, Percentage,
IntegerMilliseconds, IntegerSeconds, IntegerDays, SendMeVersion, Error}`.

```rust
#[derive(Debug, Clone, Copy)]
pub struct BoundedInt32<const LOWER: i32, const UPPER: i32> { /* private */ }

impl<const LOWER: i32, const UPPER: i32> BoundedInt32<LOWER, UPPER> {
    pub const LOWER: i32 = LOWER;
    pub const UPPER: i32 = UPPER;
    pub fn get(&self) -> i32;
    pub fn saturating_new(val: i32) -> Self;
    pub fn checked_new(val: i32) -> Result<Self, Error>;
    pub fn saturating_from(val: i32) -> Self;
    pub fn saturating_from_str(s: &str) -> Result<Self, Error>;
}
```

- The bounds are **const generics**, not runtime fields, and they are re-exposed
  as the associated constants `LOWER` and `UPPER`. `BoundedInt32::<10, 20>::LOWER`
  is `10`. A case in scope reads both.
- **Two construction policies, and they are not the same function.**
  `saturating_new` **clamps**: `saturating_new(5)` on a `<10, 20>` is `10`, and
  `saturating_new(25)` is `20`. `checked_new` **refuses**, returning
  `Err(Error::BelowLowerBound(value, bound))` or
  `Err(Error::AboveUpperBound(value, bound))`. Both tuple fields are checked: the
  error carries the **value that was offered and the bound it missed**, in that
  order, not just the bound.
- `saturating_from_str` parses and then clamps, so `"3"` on a `<10, 20>` is
  `10` rather than an error. A string that is not an `i32` at all is
  `Err(Error::Unrepresentable)` — parsing is *not* clamped, only the value is.
  This asymmetry is deliberate and is checked.
- `TryFrom<i32>` is `checked_new`, not `saturating_new`. A type that implements
  it by clamping will pass §4.1 and fail here.
- The narrowing conversions each have their own impl:

| conversion | behaviour |
|---|---|
| `From<BoundedInt32<L, U>> for i32` | infallible, the inner value |
| `From<BoundedInt32<L, U>> for f64` | infallible |
| `From<BoundedInt32<0, 1>> for bool` | infallible; `1` is `true`, anything else `false` |
| `From<BoundedInt32<0, 255>> for u8` | infallible |
| `TryFrom<BoundedInt32<L, U>> for u64` | `Err(Error::Negative)` for a negative value |
| `TryFrom<BoundedInt32<L, U>> for usize` | same |

  The `bool` and `u8` impls are `From`, **not** `TryFrom`, and they exist only
  for those two exact bound pairs. This is what makes `<0, 1>` the canonical
  "consensus boolean".
- `Display` for `BoundedInt32` is the inner integer, and `FromStr` is
  `checked_new` (it rejects rather than clamps). Neither is checked here.

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct Percentage<T: Copy + Into<f64>> { /* private */ }

impl<T: Copy + Into<f64>> Percentage<T> {
    pub fn new(value: T) -> Self;
    pub fn as_fraction(self) -> f64;
    pub fn as_percent(self) -> T;
}
impl<const H: i32, const L: i32> TryFrom<i32> for Percentage<BoundedInt32<H, L>>;
```

- **The const parameters are read positionally, not by name.** `BoundedInt32`
  declares them `<LOWER, UPPER>`; the `TryFrom<i32>` impls above — and the ones
  on `IntegerMilliseconds`, `IntegerSeconds` and `IntegerDays` below — spell the
  same two parameters `<H, L>`. Both spellings are upstream's and they are
  reproduced here as upstream writes them. In every case the **first** argument
  is the lower bound and the second is the upper: `BoundedInt32<10, 99>` is
  10 through 99 under either name. Nothing is checked on the parameter names.
- `as_percent` gives the number back in the units it was written in; the two
  differ by a factor of 100 and a case in scope checks both of a single value.
- `TryFrom<i32>` runs the **inner type's** bound check first, so
  `Percentage<BoundedInt32<10, 99>>::try_from(5)` is an error. A percentage is
  not automatically `0..=100`; the range is whatever the inner type says.

```rust
pub struct IntegerMilliseconds<T> { /* private */ }
pub struct IntegerSeconds<T>      { /* private */ }
pub struct IntegerDays<T>         { /* private */ }

impl<T> Integer*<T>          { pub fn new(value: T) -> Self; }
impl<T: TryInto<u64>> TryFrom<Integer*<T>> for Duration;
impl<const H: i32, const L: i32> TryFrom<i32> for Integer*<BoundedInt32<H, L>>;
```

- These are **three distinct types, not three names for a number**. The
  conversion to `Duration` is where the scale lives: milliseconds multiply by
  `1`, seconds by `1_000`, days by `86_400` seconds. A case in scope builds
  `IntegerMilliseconds` and `IntegerSeconds` around the *same* inner value and
  requires the two `Duration`s to differ.
- **A day is exactly 86400 seconds** here. There is no calendar arithmetic.
- The conversion to `Duration` is `TryFrom`, not `From`, because the inner type
  only promises `TryInto<u64>`. For `IntegerMilliseconds` and `IntegerSeconds`
  the error type *is* the inner `TryInto<u64>` error, so a negative value fails
  with `Error::Negative`. `IntegerDays` instead declares `type Error = Error`
  and maps every inner failure to `Error::Overflow`, so a negative
  `IntegerDays` fails with `Overflow`, not `Negative`.
- `IntegerMilliseconds` and `IntegerSeconds` derive `Add`, `Mul`, `Div`, `From`,
  `FromStr`, `Display`, `Debug`, `PartialEq`, `Eq`, `Ord`, `PartialOrd`;
  `IntegerDays` derives that same list without `Add`, `Mul` and `Div`.
  None of that is checked; only `new` and the `Duration` conversion are.

```rust
#[derive(Clone, Copy, From, FromStr, Display, Debug, PartialEq, Eq, Ord, PartialOrd)]
pub struct SendMeVersion(u8);
impl SendMeVersion { pub fn new(value: u8) -> Self; pub fn get(&self) -> u8; }
impl TryFrom<i32> for SendMeVersion;
```

- The inner type is `u8`, so `new` cannot fail, and `TryFrom<i32>` is the
  fallible door the checking uses: `256` is refused, `255` is accepted, and a
  case in scope checks the boundary in both directions. (The derived `FromStr` is
  fallible too; nothing checks it.)

```rust
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
#[non_exhaustive]
pub enum Error {
    BelowLowerBound(i32, i32),
    AboveUpperBound(i32, i32),
    Negative,
    Unrepresentable,
    Overflow,
    Uninhabited,
}
```

- Six variants; the first two carry `(value, bound)`. `Uninhabited` is what a
  `BoundedInt32<U, L>` with `U > L` produces — the type is constructible in the
  type system but no value satisfies it.
- `tor_units::Error` **is** `PartialEq`, unlike most error types in this step,
  so a case in scope may compare two of them directly. Nothing checks its
  `Display` text.

### 8.2 `retry_error::RetryError` and `tor_circmgr::Error`

```rust
#[derive(Debug)]
pub struct RetryError<E> { /* private */ }

impl<E> RetryError<E> {
    pub fn in_attempt_to<T: Into<String>>(doing: T) -> Self;
    pub fn push<T: Into<E>>(&mut self, err: T);
    pub fn sources(&self) -> impl Iterator<Item = &E>;
    pub fn len(&self) -> usize;
    pub fn is_empty(&self) -> bool;
    pub fn dedup_by<F: Fn(&E, &E) -> bool>(&mut self, same_err: F);
    pub fn dedup(&mut self) where E: PartialEq;
}
impl<E: Clone> Clone for RetryError<E>;
impl<E, T: Into<E>> Extend<T> for RetryError<E>;
impl<E> IntoIterator for RetryError<E> { type Item = E; }
impl<E: Debug + Display> std::error::Error for RetryError<E>;
```

- There is **no public constructor other than `in_attempt_to`**, and the string
  it takes is the *thing being attempted*, not an error message. It reappears in
  the rendering, which is why a case in scope can look for the text it supplied
  without checking any English the library wrote.
- `sources()` yields the errors **in the order they were pushed**, and `len()`
  counts the entries currently held. A fresh `RetryError` is `is_empty()` and
  `len() == 0`.
- `dedup_by` groups **consecutive** entries the predicate calls equal, and after
  it runs `len()` shrinks while the *count of attempts* does not — three
  identical failures collapse to one entry that still reports three attempts.
  That distinction between "entries" and "attempts" is the checked behaviour.
- `dedup()` is the same thing with `PartialEq`, and it is **unusable for
  `tor_circmgr::Error`**, which is not `PartialEq`. A case in scope therefore uses
  `dedup_by` with `std::mem::discriminant`. Do not add a `PartialEq` derive to
  `tor_circmgr::Error` to make `dedup` work; nothing requires it.
- `Attempt`, the index type inside, is **private**. There is no way to ask which
  attempt an error came from except by reading the rendering.
- `Display` has **three branches**, keyed on the number of attempts: zero
  attempts, exactly one, and more than one. They are three different shapes, not
  one shape with a number substituted; a case in scope distinguishes the empty
  case from the single case, and checks that the many-case mentions the count
  and contains each source's own rendering. **The English is out of scope** — only
  the count, the supplied `doing` string, and the containment.

`tor_circmgr::Error` is the error the previous sections return, and this is its
whole variant set at this release:

```rust
#[derive(Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    NoRelays(String),
    NeedConsensus,
    PendingFailed,
    CircTimeout,
    GuardNotUsable,
    UsageNotSupported(String),
    RequestTimeout,
    RequestFailed(RetryError<Box<Error>>),
    Internal(String),
    ChanFailed(#[from] tor_chanmgr::Error),
    Protocol(#[from] tor_proto::Error),
    State(#[from] tor_persist::Error),
    GuardMgr(#[source] tor_guardmgr::GuardMgrError),
    Guard(#[from] tor_guardmgr::PickGuardError),
}
```

- It is `Clone` and `Debug` but **not** `PartialEq` and **not** `Eq`. Two unit
  variants are told apart by `Debug`, which is what the cases in scope do.
- `RequestFailed` is **recursive**: it holds a `RetryError<Box<Error>>` whose
  members are themselves `tor_circmgr::Error`s. This is the only variant that
  holds more than one cause.
- The `#[from]` conversions are automatic; `GuardMgr` is `#[source]` only,
  because it is produced by a **hand-written** `From<GuardMgrError>` that
  splits:

```rust
impl From<tor_guardmgr::GuardMgrError> for Error {
    fn from(err: tor_guardmgr::GuardMgrError) -> Error {
        match err {
            tor_guardmgr::GuardMgrError::State(e) => Error::State(e),
            _ => Error::GuardMgr(err),
        }
    }
}
```

  A guard-manager failure that is *really* a state failure is lifted to
  `Error::State` and loses its `GuardMgrError` wrapper; anything else keeps it.
  A case in scope checks both halves of that match.
- `From<futures::channel::oneshot::Canceled>` gives `PendingFailed` and
  `From<futures::task::SpawnError>` gives `Internal(_)`. Neither preserves the
  original.
- **Interpolation is not uniform, and the difference is observable.**
  `Error::State` and `Error::GuardMgr` each interpolate their source into their
  own rendering, so the wrapper's text contains the source's text.
  `GuardMgrError::State` does **not** — it renders a fixed phrase and drops its
  `tor_persist::Error`. A case in scope relies on the containment where it holds
  and does not assert it where it does not. Nothing checks the phrases
  themselves.

---

## 9. Requirement groups → interface items

| Group | Kind | Items |
|---|---|---|
| `persistfs` | requirement | §1.1, §1.2, §1.3 `FsStateMgr` and the on-disk layout table, §1.6 |
| `persiststate` | requirement | §1.1, §1.2 incl. `held()`, §1.3 `TestingStateMgr`, §1.6 `JsonError` |
| `persisthandle` | requirement | §1.4, §1.1, §1.3 |
| `futureproof` | requirement | §1.5 |
| `guardsel` | requirement | §2.1 `select_guard`, §2.2, §2.5, §4.3 the test network, §0.1 the runtime |
| `guardpersist` | requirement | §2.6 the persisted document incl. the identity encodings, §2.1 `store_persistent_state`/`reload_persistent_state`/`upgrade_to_owned_persistent_state`, §1.1, §4.3 the test network, §0.1 the runtime |
| `guardusage` | requirement | §2.2, §2.7 `GuardFilter`/`GuardRestriction`, §2.5, §2.1, §4.3 the test network, §0.1 the runtime |
| `guardreport` | requirement | §2.3, §2.4 `GuardMonitor`/`GuardUsable`, §2.6 incl. the identity encodings, §2.1, §4.3 the test network, §0.1 the runtime |
| `isolation` | requirement | §3.1 incl. `StreamIsolationBuilder` and `ConnectPrefs` |
| `subnets` | requirement | §4.2 `SubnetConfig`, §3.2 `ExitPathBuilder::pick_path`, §3.3 `PathConfig`, §4.3 the test network and `add_or_port`, §0.1 the runtime |
| `pathbuild` | requirement | §3.2, §3.3, §4.3 the test network, §0.1 the runtime |
| `circcfg` | requirement | §3.3 incl. `RequestTiming`/`RequestTimingBuilder` and the defaults table |
| `netparams` | requirement | §4.1, the 34-row parameter table and `from_map`, §4.3 the test consensus's `bwweightscale` |
| `socksresolve` | requirement | §5.1 `RESOLVE`/`RESOLVE_PTR` |
| `resolvedcell` | requirement | §6.1, §6.2 framing |
| `datacell` | requirement | §6.3 `Data`, §6.2 RELAY framing |
| `localaddr` | requirement | §7.1 incl. the two `IntoTorAddr` traits and `ClientAddrConfig` |
| `cfgload` | requirement | §7.2 `load` and `CmdLine` |
| `cellcodec` | gate | step 01 §`cellcodec` — channel cell framing, re-checked on the carried workspace |
| `socksconnect` | gate | §5.1 CONNECT path, from step 01 |
| `netdocs` | gate | step 01 §`mdparse` (microdescriptor parsing) and §`ndargs` (`RelayFamily`, `TorVersion`, port policies), re-checked; §10 for the two expected renderings |
| `resolvequery` | gate | §6.1, §6.2 `Resolve` encoding, from step 01 |
| `unitscale` | requirement | §8.1 |
| `errvariants` | requirement | §8.2 `RetryError` and the `tor_circmgr::Error` variant set |

A row that cites **"step 01 §*name*"** means the section so headed in
`/app/docs/step-01/TEST_INTERFACE.md`, which `/app` still carries: step 02's
`workdir` replaced the bare `/app/TEST_INTERFACE.md`, not the step-scoped copy.
Those sections remain binding except where a section of *this* document says it
supersedes them, which **§0, §4.1 and §5.1** do by name.

Section 5.1's supersession is a **narrowing**, and it is the only one of the
three that changes a rule rather than a name or a count. Step 01's `socks`
observables put the whole command/port rule in one sentence — "`RESOLVE` and
`RESOLVE_PTR` are accepted, `BIND` and `UDP_ASSOCIATE` are `Error::NoSupport`,
port 0 is `Error::Syntax`" — and at step 01 that was accurate, because port 0
was refused for every command. §5.1 above splits it: port 0 is refused only for
the commands that *require* a port (`CONNECT`, `BIND`, `UDP_ASSOCIATE`) and is
the normal case for `RESOLVE`/`RESOLVE_PTR`, which carry no port at all. Where
the two sentences disagree, **§5.1 governs** — the blanket port-0 refusal is the
regression `socksresolve` exists to catch, not a rule to preserve.

Every row above points at a section that names the items the group imports. Where
a group also depends on a fact stated as prose rather than as a signature — the
on-disk layout for `persistfs`, the persisted JSON document for `guardpersist`,
the parameter table for `netparams`, the defaults table for `circcfg`, **the
composition of the test network for the seven groups that cite §4.3** — the row
says so, because those are the facts an agent cannot derive.

The four gate rows are **re-checked behaviour, not carried files**: this
step's harness re-authors its own cases for cell framing, SOCKS CONNECT,
microdescriptor parsing and the RESOLVE query rather than executing step 01's,
so the obligation is that the behaviour survives on the carried workspace, not
that any particular step-01 test file still exists. Where such a case pins more
than step 01's document stated, the extra rule is stated in this document — §5.1
for the SOCKS boundary rules, and the step-01 sections cited above for the rest.

---

## 10. Named in the milestone and deliberately out of scope

`instruction.md` section 1 lists this step's milestone. Four of the things it
names are **out of scope anywhere in this step**, and you should budget
accordingly. They are listed here so that the silence is a decision you were
told about rather than one you discover at the end.

| Milestone item | Why it is out of scope |
|---|---|
| **Circuit-build-timeout inference** | The estimator (`tor_circmgr::timeouts`) is a private module at this release with no public constructor and no public accessor for an inferred timeout. The only public surface it has is the eleven `cbt_*` fields of `NetParameters` plus `unused_client_circ_timeout_while_learning_cbt` (consensus key `cbtlearntimeout`), and those twelve *are* checked, in §4.1. The inference itself is unobservable from outside the crate, so no case asserts it. |
| **Configurable logging, and the `log` to `tracing` migration** | Logging is a side effect on a global subscriber, not a return value. Nothing in the in-scope surface observes a log line, and no case installs a subscriber. Migrate the crates if you like — nothing checks either way. |
| **The binary's subcommand CLI** | `arti` is a binary, not a library. The conformance driver links library crates and calls public items; it never spawns a process. No case runs `arti` with any argument. §7.2 checks `tor_config::CmdLine`, which is the *library* half of option handling, and that is the whole of what is checked here. |
| **"Lightweight path bias"** | Path-bias accounting lives behind `GuardMgr`'s private sample state. The observable consequences that *are* checked are guard stickiness (`guardsel`), the persisted `confirmed` list (§2.6, `guardpersist`) and the status-reporting contract of §2.3/§2.4 (`guardreport`). The bias counters themselves have no accessor and no case reads them — which is also why five of `GuardMonitor`'s seven methods (`failed`, `report`, `commit`, `pending_status`, `ignore_indeterminate_status`) and `GuardMgr::note_internet_activity` are declared in §2.1/§2.4 but not driven by any case: their whole effect is a write to that state. |

Two further scope statements, so they are in one place:

- **Nothing in this step contacts the network.** Every case in scope runs against
  in-process fixtures built from `tor_netdir::testnet`. A correct implementation
  that only works against the live Tor network does not satisfy this step, and
  that is intended.
- **No diagnostic English written by a library is checked.** Error *variants*
  are public API and are checked by identity — see §1.6, §2.5, §3.2, §8.1 and
  §8.2. The wording inside `#[error("...")]` is not, and no case asserts a
  phrase this document does not put there. Three kinds of rendering assertion do
  exist, and none of them is about the library's prose:

  1. **A rendering must not be empty.** Several cases call `to_string()` on an
     error and require a non-empty result — `guardsel` on `PickGuardError`,
     `localaddr` on three `arti_client::Error` variants, `errvariants` on the
     five unit `tor_circmgr::Error` variants, `pathbuild` on `NoRelays`. That
     is an assertion that the error carries something at all, not an assertion
     about what it says: render it in your own words, or with no words, but do
     not render it as nothing.

  2. **An error must not swallow what it was given.** Several cases render an
     error and look for a substring — but the substring is always either data
     the case itself supplied or another error's own rendering, never wording
     the library chose. `cellcodec` checks that a rejected cell's report names
     the circuit id the case built (`5`); `cfgload` checks that a missing-file
     error names the path it was handed and that a bad `--option` error quotes
     the option text; `errvariants` checks that the `String` payloads of
     `NoRelays`, `UsageNotSupported` and `Internal` survive into the rendering,
     that `RetryError::in_attempt_to`'s own label and attempt count survive,
     and that a wrapping error's text contains its source's text (§8.2). Change
     the English around them as you like; keep the data.
  3. **Canonical renderings are checked, and every one of them is specified
     above.** These are protocol constants, addresses, identities and document
     forms, not diagnostics:

| Type | Expected rendering | Where it is specified | Groups |
| --- | --- | --- | --- |
| `SocksCmd` | the constant's own name, e.g. `"RESOLVE"`, `"RESOLVE_PTR"` | §5.1 | `socksresolve` |
| `RelayCmd` | likewise the constant's own name ("`Display` names a known command") | step 01 §`relaycmd`; the type is in §6.2 | `resolvequery` |
| `SocksAddr` | the address itself; a hostname renders as its text | §5.1 | `socksconnect`, `socksresolve` |
| `TorAddr` | `host:port`, with IPv6 bracketed | §7.1 | `localaddr` |
| `TorVersion` | its document form, e.g. `"0.4.6.8-rc"` | step 01 §`ndargs` | `netdocs` |
| a port policy | its document form, e.g. `"accept 80,443"`, `"reject 1-65535"` | step 01 §`ndargs` | `netdocs` |
| `Ed25519Identity` | base64, standard alphabet, no padding — identical to its serialised form | §2.6 | `guardpersist`, `guardreport` |
| `RsaIdentity` | `$` followed by 40 lowercase hex digits — **not** the same as its serialised form, which omits the `$` | §2.6 | `netdocs`, via `RelayFamily::members()` |

  Eight rows, and nothing else. `tor-units` is deliberately absent: §8.1 says
  `BoundedInt32`'s `Display` and `FromStr` are out of scope, `unitscale` renders
  nothing, and that remains true.

- **`Debug` is a separate question, and it is checked in six places.** It is not
  diagnostic English: several types in this step have private fields, no
  accessors and no `PartialEq`, so the derived `Debug` is the only observation
  channel an external crate has. Where a case relies on it, the section says so
  and says exactly what is pinned — §2.2 (`GuardUsageBuilder`), §2.3
  (`GuardStatus`, the only place a whole rendering must match a variant name),
  §3.1 (`ConnectPrefs` and `StreamIsolation`), §3.3 (the configuration
  defaults), §7.1 (`ClientAddrConfig`) and §8.2 (`tor_circmgr::Error`'s unit
  variants). §2.3 is the **only** place a whole `Debug` string is compared with
  a literal. Everywhere else a case either checks that the rendering
  **contains** a value or a variant name it already knows, or compares two
  renderings with each other (`assert_eq!` / `assert_ne!` between two objects,
  never against text). So the field names you choose, their order and the
  spacing the derive produces are not asserted anywhere outside §2.3 — but a
  `Debug` that hides a field, or one that renders two distinguishable values
  identically, fails.
