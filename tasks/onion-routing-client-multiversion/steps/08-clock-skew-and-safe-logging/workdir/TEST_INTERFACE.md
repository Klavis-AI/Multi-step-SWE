# Test interface — step 08 (`08-clock-skew-and-safe-logging`)

Everything this step is checked through is named below: the crate, the module
path, the exact public signature the conformance driver calls, and the
behaviour it observes. Nothing outside this document is checked.

The driver is an external crate. It reaches your workspace only through
`pub` items on the paths written here — never through `pub(crate)` items,
`#[cfg(test)]` items, private modules, or `#[doc(hidden)]`. Items are grouped
by crate; section 13 maps each requirement group to the items it uses, and
section 14 records what is deliberately **not** checked here.

---

Two cargo features must exist and must compile, because the driver enables them:

| Crate | Feature | Why the driver needs it |
| --- | --- | --- |
| `tor-rtcompat` | `tokio` | exposes `tor_rtcompat::tokio`; this crate's default feature set is empty |
| `tor-rtcompat` | `native-tls` | exposes the native-tls backend |

These are not optional extras. The driver's manifest names them, so cargo
resolves them before a single test runs. A crate that does not DECLARE the
feature - even as an empty entry in its `[features]` table - makes the driver's
manifest unresolvable. What the verifier does with that is *not* to fail the
step: it repairs the driver's manifest by dropping the undeclared feature
request and checks on, recording the dropped feature in its report — and then
the groups that need that feature's items fail on their own, because the items
they import are behind a feature gate that never got enabled. Either way the
features have to exist; the difference is that the failure surfaces per group
rather than all at once.

## 1. `tor-proto` — `ClockSkew`

### 1.1 `tor_proto::ClockSkew`

```rust
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[allow(clippy::exhaustive_enums)]
pub enum ClockSkew {
    Slow(Duration),
    None,
    Fast(Duration),
}

// `Ord` and `PartialOrd` are HAND-WRITTEN, not derived, and the enum is
// EXHAUSTIVE, not `#[non_exhaustive]`. Both matter:
//
//   * A derived `Ord` orders `Slow(a) < Slow(b)` when `a < b`. The required
//     order is the opposite — see the `Ord` bullet at the end of this section —
//     so `#[derive(PartialOrd, Ord)]` compiles and then gives the wrong answer.
//   * The enum carries no `#[non_exhaustive]`, so an out-of-crate `match` over
//     the three variants needs no wildcard arm.
impl Ord for ClockSkew { /* ... */ }
impl PartialOrd for ClockSkew { /* ... */ }

impl ClockSkew {
    pub fn magnitude(&self) -> Duration;
    pub fn as_secs_f64(&self) -> f64;
    pub fn from_secs_f64(f: f64) -> Option<Self>;
    pub fn if_above(self, min: Duration) -> Self;
    pub fn is_skewed(&self) -> bool;
}
```

The floor below which a difference is not worth reporting is a private
module-level constant in `tor_proto::util::skew` — `const MIN: Duration =
Duration::from_secs(2);` — and is deliberately **not** exported. It is not an
associated constant on `ClockSkew`, so `ClockSkew::MIN` does not exist and any
code that names it fails to compile. `MIN`'s value is observable through
`from_secs_f64`, which applies it.

Observable behaviour:

- `magnitude()` returns the wrapped `Duration` from `Slow` or `Fast`, or
  `Duration::ZERO` for `None`.
- `as_secs_f64()` returns the magnitude in seconds as a signed float: negative
  for `Slow`, zero for `None`, positive for `Fast`.
- `from_secs_f64(f)` returns `None` when `f` is NaN. When `f` is zero or
  subnormal, it returns `Some(ClockSkew::None)`. When `f` is ±infinity, it
  returns `Some(ClockSkew::Slow/Fast(Duration::MAX))`. Otherwise, it converts
  `f` to a signed `Duration` and applies `if_above(MIN)`, so a magnitude at or
  below two seconds comes back as `Some(ClockSkew::None)` and a larger one as
  `Some` of the corresponding `Slow`/`Fast`.
- `if_above(self, min)` **takes and returns `Self`, not `Option<Self>`**. It
  returns `self` when the magnitude is strictly greater than `min`, and
  `ClockSkew::None` otherwise. There is no `Option` in the signature: an
  unskewed result is represented by the `None` *variant*, and the input is
  consumed by value. It compares the unsigned magnitude, so a `Slow` value
  larger than `min` survives even though it is negative on the signed scale,
  and `ClockSkew::None` never becomes skewed.
- `is_skewed()` returns `true` for `Slow` and `Fast`, `false` for `None`.
- `ClockSkew` is `Copy`, totally ordered: `Slow(d1) < Slow(d2)` when
  `d1 > d2`; `Slow(_) < None < Fast(_)`; `Fast(d1) < Fast(d2)` when
  `d1 < d2`.

---

## 2. `tor-guardmgr` — clock-skew observation

### 2.1 `tor_guardmgr::{SkewEstimate, ClockSkewEvents}`

```rust
#[derive(Clone, Debug)]
pub struct SkewEstimate {
    // All three fields are PRIVATE, and there is no public constructor.
    estimate: ClockSkew,
    n_observations: usize,
    confidence: Confidence,   // private enum; not nameable out of crate
}

impl SkewEstimate {
    pub fn skew(&self) -> ClockSkew;
    pub fn noteworthy(&self) -> bool;
    // `estimate_skew(..)` — the function that BUILDS one — is `pub(crate)`,
    // as is the `SkewObservation` type it consumes.
}

// NOT CHECKED. `SkewEstimate` has no public constructor, so no case can reach
// this impl, and no render string is specified for it anywhere in this
// document. Implement whatever you like; nothing observes it.
impl Display for SkewEstimate { /* out of scope */ }

#[derive(Clone)]
// `ClockSkewEvents` is also `Debug`. Upstream obtains that through `educe`
// (`#[derive(Clone, Educe)] #[educe(Debug)]`) rather than `#[derive(Debug)]`,
// because the watch receiver it wraps is not `Debug`. How you get `Debug` is
// your choice; no case at this step formats one, so it is out of scope.
pub struct ClockSkewEvents { /* one private field */ }

impl ClockSkewEvents {
    pub fn get(&self) -> Option<SkewEstimate>;
}

impl Stream for ClockSkewEvents {
    type Item = Option<SkewEstimate>;
    // `poll_next` is the whole of the rest of the trait; there is no third item.
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>)
        -> Poll<Option<Self::Item>>;
}
```

Re-exported by `tor-circmgr` at crate root: `pub use tor_guardmgr::ClockSkewEvents;`

Observable behaviour:

- `GuardMgr::skew_events()` returns a `ClockSkewEvents` whose `get()` returns
  `None` on a fresh manager that has never attempted a guard connection.
- The `Stream` impl and the `Clone` impl are both public.
- `SkewEstimate` exposes `skew()` and `noteworthy()` and a `Display`. It has no
  public constructor: the only function that builds one is `pub(crate)`, it
  consumes a `pub(crate)` observation type, and it returns `None` until at
  least **8** observations newer than one hour have accumulated. The published
  magnitude is `ClockSkew::from_secs_f64(mean).if_above(FIFTEEN_MINUTES)`, and
  `noteworthy()` is false whenever that collapses to `ClockSkew::None`.
- **The estimator itself is therefore out of scope at this step, and cannot be.**
  An out-of-crate driver cannot reach a non-`None` `SkewEstimate` without a
  live guard or fallback connection. What *is* checked is the value type the
  estimator publishes and the two predicates it applies to it — the 2-second
  floor inside `from_secs_f64` and the 15-minute reporting threshold — through
  `tor_proto::ClockSkew` in §1.1. See §14.

### 2.2 `tor_guardmgr::GuardMgr`

```rust
// The impl block is GENERIC and BOUNDED. `R` is a `tor_rtcompat::Runtime`;
// writing `impl GuardMgr<R>` without the parameter and the bound does not
// compile. §11 is the runtime the driver instantiates `R` with.
impl<R: tor_rtcompat::Runtime> GuardMgr<R> {
    pub fn new<S>(
        runtime: R,
        state_mgr: S,
        fallbacks: FallbackList,
    ) -> Result<Self, GuardMgrError>
    where
        S: tor_persist::StateMgr + Send + Sync + 'static;

    pub fn skew_events(&self) -> ClockSkewEvents;
}

// The error half of `new`'s return type. Public at `tor_guardmgr::GuardMgrError`
// (declared in `tor-guardmgr`'s `err` module and re-exported at the crate root).
#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum GuardMgrError {
    State(#[from] tor_persist::Error),
    Spawn {
        spawning: &'static str,
        #[source]
        cause: Arc<futures::task::SpawnError>,
    },
}
```

Observable behaviour:

- `new` accepts a runtime, a state manager and a fallback list, returning a
  fresh `GuardMgr` or an error.
- `GuardMgrError` must exist and must be nameable, because it is half of a
  signature the driver calls. Its variants, its `Display` strings and its
  `HasKind` mapping are **not** checked: `skewevents.rs` only `.expect()`s the
  `Result`, so all the driver needs is that the type exist and be `Debug`.
- `skew_events()` returns a `ClockSkewEvents` channel whose `get()` is `None`
  on a manager that has not yet attempted a guard connection.

### 2.3 `tor_guardmgr::fallback::FallbackList`

```rust
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct FallbackList { /* private */ }

impl FallbackList {
    pub fn len(&self) -> usize;
    pub fn is_empty(&self) -> bool;
    pub fn choose<R: rand::Rng>(&self, rng: &mut R)
        -> Result<&FallbackDir, PickGuardError>;
}

// `Default` is DERIVED above (empty list). It is written out here only to
// name the method the driver calls; do not read this as a requirement that it
// be hand-written.
impl Default for FallbackList {
    fn default() -> Self;
}
```

Observable behaviour:

- `FallbackList::default()` returns an empty list: `len() == 0` and
  `is_empty() == true`.
- `choose(&mut rng)` on a non-empty list returns `Ok(&FallbackDir)`, a
  **borrow** of a member of the list. It does not clone: binding the result to
  an owned `FallbackDir` does not compile.
- `choose(&mut rng)` on an empty list returns
  `Err(PickGuardError::AllFallbacksDown { retry_at: None })`.

---

## 3. `arti-client` — bootstrap blockage

### 3.1 `arti_client::status::{BootstrapStatus, Blockage, BlockageKind}`

```rust
// Neither of these is `#[non_exhaustive]` upstream. It would make no
// observable difference — every field is private — but the declaration below
// is what the release actually writes.
#[derive(Clone, Debug, Default)]
pub struct BootstrapStatus { /* private */ }

// `Blockage` renders as "{kind} ({message})".
#[derive(Clone, Debug, Display)]
pub struct Blockage { /* private */ }

#[derive(Clone, Debug)]
#[non_exhaustive]
pub enum BlockageKind {
    Offline,
    Filtering,
    CantReachTor,
    ClockSkewed,
}

// Upstream derives `Display` with `derive_more`; a hand-written `impl` is
// equally acceptable, because only the rendered strings are checked.
impl Display for BlockageKind { /* ... */ }
//
// This enum does NOT derive `Copy`, `Eq` or `PartialEq`, and nothing checked
// needs them: `blockage` discriminates with `matches!`, never `assert_eq!`.
// Deriving them anyway compiles (the variants are fieldless) but is not part
// of the contract.
```

Observable behaviour (tested only through `From<ConnBlockage> for BlockageKind`
and the exact `Display` strings, since `BootstrapStatus` has private fields):

- `BlockageKind::Offline.to_string() == "We seem to be offline"`
- `BlockageKind::Filtering.to_string() == "Our internet connection seems filtered"`
- `BlockageKind::CantReachTor.to_string() == "Can't reach the Tor network"`
- `BlockageKind::ClockSkewed.to_string() == "Clock is skewed."`

### 3.2 `tor_chanmgr::ConnBlockage`

```rust
// No `Copy` upstream. `Debug + PartialEq` ARE required: `blockage` calls
// `assert_ne!` on two variants, which needs both.
#[derive(Debug, Clone, Eq, PartialEq)]
#[non_exhaustive]
pub enum ConnBlockage {
    NoTcp,
    NoHandshake,
    CertsExpired,
}

impl Display for ConnBlockage { /* ... */ }
```

The conversion to `BlockageKind` is **not** in `tor-chanmgr`. It belongs to
`arti-client`, beside `BlockageKind` itself:

```rust
// crate `arti-client`, module `status`
impl From<tor_chanmgr::ConnBlockage> for BlockageKind { /* ... */ }
```

Which crate holds it is forced by the dependency graph, not by style:
`arti-client` depends on `tor-chanmgr`, and `tor-chanmgr` must not depend on
`arti-client`. Writing the impl in `tor-chanmgr` would add that edge and cargo
would refuse the resulting cycle, so the whole workspace would stop building —
not just this group. The driver only ever calls it as
`let kind: BlockageKind = ConnBlockage::NoTcp.into();`, which resolves the same
way from either side, so the placement is invisible to the assertion and fatal
to the build.

Observable behaviour:

- `ConnBlockage::NoTcp.to_string() == "unable to connect to the internet"`
- `ConnBlockage::NoHandshake.to_string() == "our internet connection seems to be filtered"`
- `ConnBlockage::CertsExpired.to_string() == "relays all seem to be using expired certificates"`
- `From<ConnBlockage> for BlockageKind`:
  - `NoTcp` → `Offline`
  - `NoHandshake` → `Filtering`
  - `CertsExpired` → `ClockSkewed`
  - (wildcard match) → `CantReachTor`

---

## 4. `tor-persist` — state-directory locking and obsolete-file removal

### 4.1 `tor_persist::{FsStateMgr, StateMgr, LockStatus}`

```rust
// THE `Result<T>` IN EVERY SIGNATURE BELOW.
//
// `tor-persist` writes its own signatures against a crate-private alias:
//
//     type Result<T> = std::result::Result<T, crate::Error>;   // NOT `pub`
//
// It is reproduced here unqualified so that the signatures match the crate's
// own source line for line. It is *not* nameable from outside the crate: an
// out-of-crate caller — including this step's driver — spells the same types
// `std::result::Result<T, tor_persist::Error>`. Declaring the alias `pub` is
// also acceptable; nothing at this step observes which you choose.
//
// `tor_persist::Error` is a public `#[non_exhaustive]` enum deriving
// `thiserror::Error, Debug, Clone`. No case at this step inspects its variants
// or its `Display`, so its shape is out of scope — only that the type is public
// and `Debug` (the driver `.expect()`s these results).

pub struct FsStateMgr { /* private */ }

// The trait has EXACTLY these six methods and the `Clone` supertrait. There is
// no elided tail. Two of them — `load` and `can_store` — are called directly by
// `stateclean`, so both are checked; `store`, `unlock` and `create_handle` are
// public but out of scope at this step.
pub trait StateMgr: Clone {
    fn load<D>(&self, key: &str) -> Result<Option<D>>
    where
        D: serde::de::DeserializeOwned;
    fn store<S>(&self, key: &str, val: &S) -> Result<()>
    where
        S: serde::Serialize;
    fn can_store(&self) -> bool;
    fn try_lock(&self) -> Result<LockStatus>;
    fn unlock(&self) -> Result<()>;
    // Has a default body upstream; a caller only needs it to exist.
    fn create_handle<T>(self, key: impl Into<String>) -> DynStorageHandle<T>
    where
        Self: Send + Sync + Sized + 'static,
        T: serde::Serialize + serde::de::DeserializeOwned + 'static;
}

// Named by `create_handle`'s return type, so declared here rather than left
// dangling. Neither is checked at this step.
pub trait StorageHandle<T: serde::Serialize + serde::de::DeserializeOwned> {
    fn load(&self) -> Result<Option<T>>;
    fn store(&self, val: &T) -> Result<()>;
    fn can_store(&self) -> bool;
}
pub type DynStorageHandle<T> = Arc<dyn StorageHandle<T> + Send + Sync + 'static>;

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[allow(clippy::exhaustive_enums)]
pub enum LockStatus {
    NoLock,
    AlreadyHeld,
    NewlyAcquired,
}

impl LockStatus {
    pub fn held(&self) -> bool;
}

impl FsStateMgr {
    pub fn from_path<P: AsRef<Path>>(path: P) -> Result<Self>;
    pub fn path(&self) -> &Path;
}
```

Observable behaviour:

- `load::<D>(key)` reads the object stored under `key`, returning
  `Ok(None)` when no such object exists. It does **not** require the lock:
  `stateclean` calls `mgr.load("guards")` on a freshly opened, unlocked manager
  and expects `Ok(None)`, and that call must not delete anything. The type
  parameter is inferred from the binding — the driver binds
  `let loaded: Option<serde_json::Value>` — so `D` must be a free generic
  bounded by `DeserializeOwned`, not a fixed type.
- `can_store()` reports whether this manager holds the write lock. It is
  **`false` on a manager that has not yet called `try_lock()`** and true once
  the lock is held. `store` on a manager whose `can_store()` is false fails
  with the `NoLock` error.
- `from_path(p)` creates `<p>/state/` (mode 0700) and `<p>/state.lock`. On
  success, returns a manager. Does not lock the directory yet.
- `path()` returns the parent of the state directory (the `p` given to
  `from_path`).
- `try_lock()` acquires an exclusive lock on `<statedir>.lock` and returns
  `NewlyAcquired` on first acquisition, `AlreadyHeld` on subsequent calls by
  the same manager. Only the first call to `try_lock()` triggers the
  obsolete-file removal logic.
- `LockStatus` has **three** variants and is exhaustive (no
  `#[non_exhaustive]`). `held()` is `true` for `AlreadyHeld` and
  `NewlyAcquired` and `false` for `NoLock` — i.e. it is
  `!matches!(self, LockStatus::NoLock)`.
- Files are considered obsolete when their extension is `toml` OR their stem is
  `default_guards`.
- Obsolete files whose age strictly exceeds 4 days (measured from mtime to
  `SystemTime::now()` at removal time) are deleted. Files dated in the future
  are kept. The cases in scope probe the ages 60 s, 3 days, 5 days and 10 days,
  so they pin the cutoff to the interval (3 d, 5 d] but not its exact
  endpoint — implement "strictly greater than 4 days" as written; the boundary
  itself is not separately asserted.

---

## 5. `safelog` — sensitive-value wrapping and display scrubbing

### 5.1 `safelog::Sensitive<T>`

```rust
#[derive(Educe)]
#[educe(
    Clone(bound),
    Default(bound),
    Deref,
    DerefMut,
    Eq(bound),
    Hash(bound),
    Ord(bound),
    PartialEq(bound),
    PartialOrd(bound)
)]
pub struct Sensitive<T>(T);   // one private positional field

impl<T> Sensitive<T> {
    pub fn new(val: T) -> Self;
    pub fn unwrap(s: Self) -> T;   // associated fn, NOT a method
}

pub fn sensitive<T>(t: T) -> Sensitive<T> { Sensitive::new(t) }

impl<T> From<T> for Sensitive<T> { /* ... */ }
```

**Every one of those nine derives is checked**, so none of them is optional and
the list is not illustrative. In particular:

- `Deref<Target = T>` and `DerefMut` are what make `s.len()`, `*s`, `s.iter()`,
  `s.as_str()` and `s.push(..)` compile on a `Sensitive<T>`. They are derived
  **without** `(bound)` — a `Sensitive<T>` derefs for every `T`.
- `Clone`, `Default`, `PartialEq`, `Eq`, `PartialOrd`, `Ord` and `Hash` are
  derived **with** `(bound)`: `Sensitive<T>` gets each one exactly when `T` has
  it, rather than whenever `T: Clone` in the blanket way `#[derive]` would
  produce. `educe`'s `(bound)` is what expresses that.
- The consequences the driver relies on: `Sensitive::<Vec<u32>>::default()`
  works; two `Sensitive<T>` values compare and sort against each other with
  `==`, `<` and `.sort()`; and a `Sensitive<T>` can be a `HashSet` member or a
  `HashMap` key.
- Whether `Serialize`/`Deserialize` are derived is gated on the crate's
  optional `serde` feature, which is **off** by default
  (`[features] default = []`). Nothing in this step enables it, so the serde
  impls are neither required nor checked.

Observable behaviour:

- `Sensitive::new(x)` wraps `x`. `Sensitive::unwrap(s)` retrieves it; it is an
  associated function taking the wrapper by value, so it is called as
  `Sensitive::unwrap(s)` and never as `s.unwrap()`.
- `From<T>` is equivalent to `new`.
- The wrapped value stays fully usable in code. Scrubbing (§5.2) is a
  *formatting* decision only; `Deref` still hands out the real value.

### 5.2 `safelog::Sensitive` — formatting

`Sensitive<T>` implements exactly these nine formatting traits — `Display`,
`Debug`, `Binary`, `Octal`, `LowerHex`, `UpperHex`, `LowerExp`, `UpperExp`,
`Pointer` — and each one is **conditional on the safe-logging state of §5.3**,
not unconditional:

```rust
// the shape every one of the nine shares
fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
    if unsafe_logging_enabled() {
        <T as Display /* or the trait in question */>::fmt(&self.0, f)
    } else {
        write!(f, "[scrubbed]")
    }
}
```

- **Default state** — no guard held, not inside
  `with_safe_logging_suppressed` — every one of the nine writes exactly
  `"[scrubbed]"`, ignoring every format flag (width, precision, fill,
  alignment, sign, `#`). The replacement is written literally so that no length
  information leaks: `format!("{:>20}", s)` is `"[scrubbed]"`, not a padded
  version of it, and `format!("{:#?}", s)` is `"[scrubbed]"` too.
- **When unsafe logging is enabled** — either because the process-global flag
  of §5.3 is negative, or because the current thread is inside a
  `with_safe_logging_suppressed` closure — each trait **delegates to the inner
  `T`'s impl of the same trait**, flags and all. `format!("{:x}", sensitive(0x0ed19a_u32))`
  is then `"ed19a"`, and `format!("{:?}", Sensitive::new("a"))` is `"\"a\""`.
- A solution that hard-codes `"[scrubbed]"` passes `safescrub` and then fails
  both `safesupp` and `safeguard`. The condition is the load-bearing part.

### 5.3 `safelog` — global safe-logging enforcement

```rust
pub fn with_safe_logging_suppressed<F, T>(f: F) -> T
where
    F: FnOnce() -> T;

pub fn enforce_safe_logging() -> Result<Guard, Error>;
pub fn disable_safe_logging() -> Result<Guard, Error>;

#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum Error {
    AlreadySafe,
    AlreadyUnsafe,
    Overflow,
}

impl Display for Error { /* ... */ }
```

Observable behaviour:

- `with_safe_logging_suppressed(|| ...)` temporarily disables safe logging on
  the current thread for the duration of the closure, via a thread-local. It
  does not consult or alter the global flag below.
- Both counters live in one global `AtomicIsize`, `LOGGING_STATE`. A safe
  guard adds 1 while it exists, an unsafe guard subtracts 1, and dropping a
  guard undoes its own increment. Zero therefore means "safe logging is on and
  nothing has been enforced".
- `enforce_safe_logging()` returns `Ok(Guard)` and adds 1; `disable_safe_logging()`
  returns `Ok(Guard)` and subtracts 1.
- Guards of the **same** kind nest: the flag goes to 2 for two safe guards and
  to -2 for two unsafe guards, and both calls succeed. There is no error for a
  repeated call of the same kind — only a guard that conflicts with the
  *current sign* is refused.
- The refusal is decided by the sign of the flag at the moment of the call:
  `enforce_safe_logging()` fails with `Err(Error::AlreadyUnsafe)` when the flag
  is negative, and `disable_safe_logging()` fails with `Err(Error::AlreadySafe)`
  when the flag is positive. Note the crossed names — `AlreadyUnsafe` is the
  error from *enforce*, and `AlreadySafe` is the error from *disable*.
- Either call returns `Err(Error::Overflow)` if the add would overflow an
  `isize`.
- The three Display strings are exactly:
  - `Error::AlreadySafe.to_string() == "Cannot enable unsafe logging: safe logging is already enforced."`
  - `Error::AlreadyUnsafe.to_string() == "Cannot enforce safe logging: unsafe logging is already enabled."`
  - `Error::Overflow.to_string() == "Too many calls to enforce or disable safe logging."`

---

## 6. `fs-mistrust` — filesystem-permission checking

**The `Result<T>` used by every signature in §6.1, §6.2 and §6.3** is
`fs-mistrust`'s own alias, and unlike `tor-persist`'s it is **public**:

```rust
pub type Result<T> = std::result::Result<T, Error>;
```

`Error` is the enum declared in §6.4. Out-of-crate callers name the alias
`fs_mistrust::Result<T>`, so it has to be public — the declared signatures
below are written in terms of it, and a private alias would leave them
unnameable from outside the crate.

### 6.1 `fs_mistrust::Mistrust`

```rust
pub struct Mistrust { /* private */ }

impl Mistrust {
    pub fn new() -> Self;
    pub fn ignore_prefix<P: AsRef<Path>>(&mut self, directory: P)
        -> Result<&mut Self>;
    pub fn trust_admin_only(&mut self) -> &mut Self;
    pub fn trust_no_group_id(&mut self) -> &mut Self;
    pub fn trust_group_id(&mut self, gid: u32) -> &mut Self;
    pub fn dangerously_trust_everyone(&mut self) -> &mut Self;
    pub fn verifier(&self) -> Verifier<'_>;
    pub fn check_directory<P: AsRef<Path>>(&self, dir: P) -> Result<()>;
    pub fn make_directory<P: AsRef<Path>>(&self, dir: P) -> Result<()>;
}

// `default()` and `new()` are the same value upstream (`new()` is
// `Self::default()`). No case at this step calls `Mistrust::default()`, so only
// `new()`'s behaviour, described below, is checked.
impl Default for Mistrust { /* == new() */ }
```

Observable behaviour:

- `new()` returns a `Mistrust` that trusts root (uid 0) and the current
  effective user (from `libc::getuid()`). By default it also trusts the group
  (if any) with the same name as the current user, if the user is a member of
  that group; this is machine-dependent.
- `ignore_prefix(dir)` canonicalizes `dir` and treats every ancestor at or
  above that canonical path as acceptable. Returns `Err(Error::NotFound)` if
  `dir` does not exist.
- `trust_admin_only()` clears the default uid and gid trust: only root is
  trusted.
- `trust_no_group_id()` clears the default gid trust: no group is trusted.
- `trust_group_id(gid)` sets a trusted gid. For an object owned by that gid the
  **entire group nibble is cleared from the forbidden mask**
  (`forbidden_bits &= !0o070`), so group read, group write *and* group execute
  are all permitted. It is not limited to read and write. A directory at mode
  0770 owned by a trusted gid therefore passes; the same directory at 0777
  still fails, and the reported bad bits are exactly `0o007` — the *other*
  nibble only. Only one gid can be trusted at a time; the last call wins.
- `dangerously_trust_everyone()` disables all permission and ownership checks;
  only file-type checks remain.
- `verifier()` returns a builder for more elaborate checks.
- `check_directory(p)` verifies that `p` is a directory and that only trusted
  users can access it. Equivalent to `verifier().require_directory().check(p)`.
- `make_directory(p)` creates `p` (recursively, mode 0700 on Unix) if it does
  not exist, then checks it. Equivalent to `verifier().make_directory(p)`.

### 6.2 `fs_mistrust::Verifier`

```rust
pub struct Verifier<'a> { /* private */ }

impl<'a> Verifier<'a> {
    pub fn require_file(self) -> Self;
    pub fn require_directory(self) -> Self;
    pub fn permit_all_object_types(self) -> Self;
    pub fn permit_readable(self) -> Self;
    pub fn all_errors(self) -> Self;
    pub fn check_content(self) -> Self;  // compiled in by default
    pub fn check<P: AsRef<Path>>(self, path: P) -> Result<()>;
    pub fn make_directory<P: AsRef<Path>>(self, path: P) -> Result<()>;
    pub fn secure_dir<P: AsRef<Path>>(self, path: P) -> Result<CheckedDir>;
    pub fn make_secure_dir<P: AsRef<Path>>(self, path: P)
        -> Result<CheckedDir>;
}
```

Observable behaviour:

- `require_file()` / `require_directory()` enforce the object type; last call
  wins.
- `permit_all_object_types()` accepts anything; last call wins.
- `permit_readable()` changes the **forbidden mask applied to the target**, and
  the change is coarser than its name suggests. The mask is chosen per path
  component:

  | component | `readable_okay` | forbidden mask |
  | --- | --- | --- |
  | the target itself (and, under `check_content()`, each member) | `false` (default) | `0o077` |
  | the target itself (and each checked member) | `true` | `0o022` |
  | any ancestor directory | either | `0o022` |
  | an ancestor directory with the sticky bit (`0o1000`) set | either | `0o000` |

  So `permit_readable()` makes the target behave like an ancestor: group and
  world may **read and execute**, but still not write. Execute is permitted
  too, because `0o022` masks only the two write bits. A target at mode 0755 is
  accepted with `permit_readable()` and rejected without it, reporting bad bits
  `0o055` — read *and* execute. A target at 0777 is rejected either way, and
  with `permit_readable()` the reported bad bits are exactly `0o022`, not
  `0o033`.
- Ancestors are never subject to the read restriction, with or without
  `permit_readable()`: a world-readable parent directory has always been fine.
- `all_errors()` collects every problem on the path into an
  `Error::Multiple(vec)` instead of returning only the first.
- `check_content()` recursively checks every member of the target directory.
  Implies `require_directory()`. It is gated on the `walkdir` feature, which
  this crate enables **by default** (`fs-mistrust/Cargo.toml` has
  `default = ["walkdir"]`, with `walkdir` an optional dependency of version
  2), so it is available and callable unless a dependent explicitly opts out
  of default features. The harness calls it unconditionally.
- `check(p)` verifies that `p` conforms to the configuration. Returns
  `Ok(())` or the first (or all, if `all_errors()`) problems.
- `make_directory(p)` checks `p` first; if that check reports
  `Error::NotFound`, it **swallows the error and builds the path** with
  `DirBuilder::recursive(true)` at mode 0700, then re-checks. So **a missing
  parent is not an error** — every missing component of `p` is created, each at
  0700, and `make_directory("a/b/c")` on an empty directory creates all three.
  It never returns `Err(Error::NotFound)` for a path it was able to create.
  Any *other* error from the initial check — a bad permission or a bad owner on
  an existing ancestor — is returned immediately and nothing is created. It
  also forces `require_directory()`, so an existing non-directory at `p` fails
  on type.
- `secure_dir(p)` / `make_secure_dir(p)` check (or create+check) `p` as a
  directory, then return a `CheckedDir` handle for safely accessing its
  members.

### 6.3 `fs_mistrust::CheckedDir`

```rust
pub struct CheckedDir { /* private */ }

impl CheckedDir {
    pub fn make_directory<P: AsRef<Path>>(&self, path: P) -> Result<()>;
    pub fn open<P: AsRef<Path>>(&self, path: P, options: &OpenOptions)
        -> Result<File>;
    pub fn as_path(&self) -> &Path;
}
```

Observable behaviour:

- `make_directory(rel)` creates `<base>/<rel>` (mode 0700) if needed, where
  `<base>` is the directory this `CheckedDir` was created for. Returns
  `Err(Error::InvalidSubdirectory)` if `rel` is absolute or contains `..`.
- `open(rel, opts)` opens `<base>/<rel>` with the given options. On Unix,
  forces mode 0600 for created files and adds `O_NOFOLLOW` to prevent symlink
  traversal. **The object it opened is then permission-checked as a
  *member*** — the `0o077` mask the table in §6.2 gives a member under
  `check_content()`, with the same ownership rule — so an existing file is not
  opened merely because the caller asked for it: a member already on disk at
  mode `0o666` is refused with `Error::BadPermission(<the member's own path>,
  0o066)`, the *forbidden subset* of its mode and not the whole mode, and no
  handle is returned. A file this call itself created is not refused, because
  the mode it was created with is `0o600`. Returns
  `Err(Error::InvalidSubdirectory)` if `rel` is absolute or contains `..`.
- `as_path()` returns the directory's path.

### 6.4 `fs_mistrust::Error`

```rust
#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum Error {
    NotFound(PathBuf),
    BadPermission(PathBuf, u32),
    BadOwner(PathBuf, u32),
    BadType(PathBuf),
    CouldNotInspect(PathBuf, #[source] Arc<IoError>),
    Multiple(Vec<Box<Error>>),
    StepsExceeded,
    CurrentDirectory(#[source] Arc<IoError>),
    CreatingDir(#[source] Arc<IoError>),
    Content(#[source] Box<Error>),
    #[cfg(feature = "walkdir")]
    Listing(#[source] Arc<walkdir::Error>),
    InvalidSubdirectory,
}

impl Error {
    pub fn path(&self) -> Option<&Path>;
    pub fn errors<'a>(&'a self) -> impl Iterator<Item = &Error> + 'a;
}

impl FromIterator<Error> for Option<Error> { /* ... */ }
```

Observable behaviour:

- **Which path `NotFound` carries is checked.** It is the path of the
  **component that could not be inspected** — the first missing ancestor in the
  chain, not the path the caller asked for. Checking `P/p/q/r` where `p` exists
  and `q` does not yields `NotFound(P/p/q)`, and `Error::path()` returns the
  same path. `ignore_prefix` is the exception that proves the rule: when its
  argument does not exist, canonicalization is what failed, so the payload is
  the path **as given**, uncanonicalized.
- Every variant renders through `thiserror`, with the strings below. **Six of
  the twelve — `BadOwner`, `BadPermission`, `BadType`, `Multiple`, `Content`
  and `InvalidSubdirectory` — are asserted verbatim by a case in scope**; the
  other six are contract text that no case renders, so implement them exactly
  as written rather than expecting a case to confirm them.
  - `NotFound(p)` → `"File or directory <p> not found"`
  - `BadPermission(p, bits)` → `"Incorrect permissions on file or directory <p>:  <rendered>"` where
    `<rendered>` spells out the offending bits: `0o022` →
    `"Incorrect permissions on file or directory <p>:  g=w o=w"` (two spaces
    after the colon); `0o777` → `"u=rwx g=rwx o=rwx"`; `0` → `""` (empty
    suffix).
  - `BadOwner(p, uid)` → `"Bad owner (UID <uid>) on file or directory <p>"` —
    note the UID comes **first** and the path second.
  - `BadType(p)` → `"Wrong type of file at <p>"`
  - `CouldNotInspect(p, _)` → `"Unable to access <p>"`
  - `Multiple(_)` → `"Multiple errors found"`
  - `StepsExceeded` → `"Too many steps taken or planned: Possible symlink loop?"`
  - `CurrentDirectory(_)` → `"Problem finding current directory"`
  - `CreatingDir(_)` → `"Problem creating directory"`
  - `Content(_)` → `"Invalid directory content"`
  - `Listing(_)` → `"Unable to list directory"`
  - `InvalidSubdirectory` → `"Path was not valid for use with CheckedDir."`
- `Content(e)` and `Listing(e)` are `#[source]` errors, so `source()` is `Some`
  for both and `None` for the variants that carry no source.
- `path()` returns the path from singleton errors, `None` from `Multiple` /
  `StepsExceeded` / `CurrentDirectory` / `CreatingDir` / `InvalidSubdirectory`;
  `Content(e)` delegates to `e.path()`, and so does `Listing(e)` — a
  `walkdir::Error` carries its own `path()`, which may itself be `None`. Those
  two are the only delegating arms; every other variant either carries a
  `PathBuf` or returns `None` outright.
- `errors()` returns an iterator over the error(s): one item for a singleton,
  `n` items for `Multiple(vec)` where `vec.len() == n`.
- `FromIterator<Error> for Option<Error>`: 0 errors → `None`; 1 error → that
  error; ≥2 → `Error::Multiple(vec)`.

---

## 7. `tor-error` — retry time and error kinds

### 7.1 `tor_error::{RetryTime, AbsRetryTime, HasRetryTime}`

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[non_exhaustive]
pub enum RetryTime {
    Immediate,
    AfterWaiting,
    After(Duration),
    At(Instant),
    Never,
}

// `Ord` and `PartialOrd` are DERIVED here, and the derived order is
// load-bearing. It is declaration order — `Immediate < At(_) < Never` — with
// two `At`s compared by their `Instant`. `earliest_absolute` is literally
// `.min()` over that order, so a hand-rolled comparison that gets `Immediate`
// or `Never` wrong still satisfies the `At`-only cases and then fails
// `retrytime`. Note the contrast with `ClockSkew` in section 1.1, where the
// derived order is the WRONG one and must be hand-written; here it is the
// right one and must not be.
//
// Also unlike every other enum in this document, `AbsRetryTime` is NOT
// `#[non_exhaustive]`, so an out-of-crate `match` over its three variants
// needs no wildcard arm.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd)]
#[allow(clippy::exhaustive_enums)]
pub enum AbsRetryTime {
    Immediate,
    At(Instant),
    Never,
}

pub trait HasRetryTime {
    fn retry_time(&self) -> RetryTime;
    fn abs_retry_time<F>(&self, now: Instant, choose_delay: F) -> AbsRetryTime
    where
        F: FnOnce() -> Duration;
}

impl RetryTime {
    pub fn absolute<F>(self, now: Instant, choose_delay: F) -> AbsRetryTime
    where
        F: FnOnce() -> Duration;

    pub fn earliest_absolute<I, F>(iter: I, now: Instant, choose_delay: F)
        -> Option<AbsRetryTime>
    where
        I: Iterator<Item = RetryTime>,
        F: FnOnce() -> Duration;

    pub fn earliest_approx<I>(iter: I) -> Option<RetryTime>
    where
        I: Iterator<Item = RetryTime>;

    pub fn loose_cmp(&self, other: &Self) -> std::cmp::Ordering;
}
```

Observable behaviour:

- `absolute(now, f)` converts to `AbsRetryTime`: `Immediate` → `Immediate`;
  `After(d)` → `At(now + d)` (checked_add; overflow → `Never`); `At(t)` →
  `At(t)`; `Never` → `Never`; `AfterWaiting` calls `f()` once and uses the
  returned duration.
- `earliest_absolute(iter, now, f)` converts every item and returns the
  smallest under `AbsRetryTime`'s own `Ord` — `Immediate < At(_) < Never`,
  two `At`s by instant. So `Immediate` beats every dated alternative, `Never`
  loses to all of them, and an `After(d)` competes as `At(now + d)`. Calls `f`
  at most once, and only if at least one item is `AfterWaiting`. Returns
  `None` if `iter` is empty.
- `earliest_approx(iter)` uses `loose_cmp` to find the earliest item without
  absolute times. Returns `None` if `iter` is empty.
- `loose_cmp` orders by discriminant: `Immediate < AfterWaiting < After <
  At < Never`; `After(d1)` vs `After(d2)` by duration; `At(t1)` vs `At(t2)` by
  instant.
- `RetryTime` itself deliberately has **no** `Ord`/`PartialOrd`: an `After(d)`
  and an `At(t)` are not comparable without knowing `now`. `loose_cmp` is an
  inherent method, not an `Ord` impl, and `earliest_approx` is
  `min_by(loose_cmp)`. Only `AbsRetryTime` is `Ord`.

### 7.2 `tor_error::{ErrorKind, HasKind}`

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq, Display)]
#[non_exhaustive]
pub enum ErrorKind {
    TorAccessFailed,
    NoPath,
    // 40 further variants at `arti-v0.3.0` (42 in total), none of them checked
    // at this step and none of their `Display` strings asserted anywhere. The
    // two above are the only ones any case names, via §7.3's `HasKind`
    // mappings. The list is elided because it is long, not because it is
    // uncertain.
}

pub trait HasKind {
    fn kind(&self) -> ErrorKind;
}
```

Observable behaviour:

- `ErrorKind` derives `Debug, Clone, Copy, PartialEq, Eq, Display`.
- `guardretry` compares `ErrorKind` values with `==` and never calls
  `to_string()` on one, so the `Display` *rendering* of any variant is
  out of scope; only the derive's presence matters.

### 7.3 `tor_guardmgr::PickGuardError`

```rust
#[derive(Clone, Debug, thiserror::Error)]
#[non_exhaustive]
pub enum PickGuardError {
    AllGuardsDown { retry_at: Option<Instant> },
    NoGuardsUsable,
    AllFallbacksDown { retry_at: Option<Instant> },
    NoCandidatesAvailable,
    // THAT IS THE COMPLETE LIST. `PickGuardError` has exactly these four
    // variants at `arti-v0.3.0`. `#[non_exhaustive]` stops out-of-crate code
    // matching without a wildcard arm; it does not mean there is a fifth
    // variant to discover.
}

impl Display for PickGuardError { /* ... */ }
impl HasRetryTime for PickGuardError { /* ... */ }
impl HasKind for PickGuardError { /* ... */ }
```

Observable behaviour:

- `AllGuardsDown { retry_at: Some(t) }.retry_time() == RetryTime::At(t)`
- `AllGuardsDown { retry_at: None }.retry_time() == RetryTime::AfterWaiting`
- `NoGuardsUsable.retry_time() == RetryTime::Never`
- `AllFallbacksDown { retry_at: Some(t) }.retry_time() == RetryTime::At(t)`
- `AllFallbacksDown { retry_at: None }.retry_time() == RetryTime::AfterWaiting`
- `NoCandidatesAvailable.retry_time() == RetryTime::Never`
- `AllGuardsDown { .. }.kind() == ErrorKind::TorAccessFailed`
- `AllFallbacksDown { .. }.kind() == ErrorKind::TorAccessFailed`
- `NoGuardsUsable.kind() == ErrorKind::NoPath`
- `NoCandidatesAvailable.kind() == ErrorKind::NoPath`
- `AllGuardsDown { .. }.to_string() == "All guards are down"`
- `NoGuardsUsable.to_string() == "No running guards were usable for the selected purpose"`
- `AllFallbacksDown { .. }.to_string() == "All fallback directories are down"`
- `NoCandidatesAvailable.to_string() == "Tried to pick from an empty list"`

---

## 8. `arti` — logging configuration

### 8.1 `arti::{LoggingConfig, LoggingConfigBuilder}`

**This section supersedes step 06 and step 07 for `LoggingConfig`.** Step 06's
contract declares it as
`#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)] #[serde(deny_unknown_fields)] #[non_exhaustive]`
with the accessors `logfiles()`, `journald_filter()` and `console_filter()`. At
this checkpoint the **base struct carries no `Deserialize` and no
`#[serde(deny_unknown_fields)]`** — serde moved onto the *builder*, through
`#[builder(derive(Debug, Serialize, Deserialize))]` — and the three accessors
are gone. Where the two disagree, this section is what is checked at step 08.

```rust
use derive_builder::Builder;   // = derive_builder_fork_arti 0.11
use tor_config::ConfigBuildError;

#[derive(Debug, Clone, Builder, Eq, PartialEq)]
#[non_exhaustive]
#[builder(build_fn(error = "ConfigBuildError"))]
#[builder(derive(Debug, Serialize, Deserialize))]
pub struct LoggingConfig {
    // Four fields, all PRIVATE, and there are NO accessors for them at this
    // checkpoint. The only way to observe a value from outside the crate is
    // `==` against another `LoggingConfig`, or `Debug`.
    #[builder(default = "default_console_filter()", setter(into, strip_option))]
    console: Option<String>,

    #[builder(default, setter(into, strip_option))]
    journald: Option<String>,

    #[builder_field_attr(serde(default))]
    #[builder(sub_builder, setter(custom))]
    files: Vec<LogfileConfig>,

    #[builder_field_attr(serde(default))]
    #[builder(default)]
    log_sensitive_information: bool,
}

fn default_console_filter() -> Option<String> { Some("debug".to_owned()) }

// HAND-WRITTEN, not derived: it goes through the builder, so the builder's
// per-field defaults are the single source of truth for the default config.
impl Default for LoggingConfig {
    fn default() -> Self { Self::builder().build().expect("Default builder failed") }
}

impl LoggingConfig {
    pub fn builder() -> LoggingConfigBuilder;   // = LoggingConfigBuilder::default()
}

// Generated by the `Builder` derive, and re-exported from the crate root
// alongside `LoggingConfig`:
//     pub use logging::{LoggingConfig, LoggingConfigBuilder};
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct LoggingConfigBuilder { /* one Option per field */ }

impl LoggingConfigBuilder {
    pub fn console<S: Into<String>>(&mut self, v: S) -> &mut Self;
    pub fn journald<S: Into<String>>(&mut self, v: S) -> &mut Self;
    pub fn log_sensitive_information(&mut self, v: bool) -> &mut Self;
    pub fn build(&self) -> Result<LoggingConfig, ConfigBuildError>;
}
```

Observable behaviour:

- `arti::LoggingConfig` and `arti::LoggingConfigBuilder` both resolve at the
  crate root; `arti::logging::{..}` is the same pair of types.
- `LoggingConfig` derives `Debug`, `Clone`, `Eq` and `PartialEq`, and is
  `#[non_exhaustive]`. It derives **neither `Default` nor `Deserialize`**:
  `Default` is hand-written via the builder, and deserialization happens on the
  builder.
- `LoggingConfig::default()` exists and equals
  `LoggingConfig::builder().build().unwrap()`, which equals
  `LoggingConfig::builder().log_sensitive_information(false).build().unwrap()`.
- `LoggingConfigBuilder` derives `Debug`, `Serialize` and `Deserialize`, and
  `Default` (as every `derive_builder` builder does). It is what a
  configuration file deserializes into.
- Deserializing `{}` into a `LoggingConfigBuilder` and building it yields
  `LoggingConfig::default()`; every field's default is supplied by the builder.
- `log_sensitive_information` defaults to `false` — safe logging on. The key is
  spelled exactly `log_sensitive_information`, it is a JSON/TOML **boolean**,
  and a string such as `"yes"` is a deserialization error rather than a truthy
  value.
- `console` defaults to `Some("debug")`. The case in scope asserts that the
  default differs from `"info"` rather than asserting the string, so a
  different default of your own would pass — the value above is still the
  contract, and a client's console output is expected to be `debug`-level.
  `setter(into, strip_option)` means the
  setter takes `impl Into<String>` and wraps it, so it is called
  `.console("info")` and never `.console(Some("info".to_string()))`.
- `journald` defaults to `None`; `files` defaults to the empty list, and its
  setter is `custom`, so it is not a plain `.files(..)` call.
- The four fields are independent: changing `console` does not change
  `log_sensitive_information`, so
  `builder().console("info").log_sensitive_information(false).build()` is
  unequal to `LoggingConfig::default()` *and* unequal to the same value with
  the flag set.
- `build()` returns `Result<_, ConfigBuildError>`; it cannot fail for this
  struct because every field has a default, but the signature is fallible.

---

## 9. `arti::ProxyConfig` (inherited from step 07)

### 9.1 `arti::{ProxyConfig, ProxyConfigBuilder}`

```rust
// `Builder`, `Eq` and `PartialEq` are all load-bearing here, not decoration:
// `dnsport` calls `ProxyConfig::builder()`, and compares whole configurations
// with `assert_eq!` / `assert_ne!` against `ProxyConfig::default()`. A
// `ProxyConfig` that is only `Debug + Clone + Deserialize` does not compile
// against the cases in scope.
#[derive(Deserialize, Debug, Clone, Builder, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
#[builder(build_fn(error = "ConfigBuildError"))]
#[builder(derive(Debug, Serialize, Deserialize))]
pub struct ProxyConfig {
    #[serde(default = "default_socks_port")]
    #[builder(default = "default_socks_port()")]
    pub(crate) socks_port: Option<u16>,
    #[serde(default)]
    #[builder(default)]
    pub(crate) dns_port: Option<u16>,
}

fn default_socks_port() -> Option<u16> {
    Some(9150)
}

// `Default` is HAND-WRITTEN, not derived. It goes through the builder, so the
// SOCKS default of `Some(9150)` reaches `ProxyConfig::default()` as well as
// `ProxyConfig::builder().build()`. A `#[derive(Default)]` would give
// `socks_port: None` and fail every comparison in `dnsport`.
impl Default for ProxyConfig {
    fn default() -> Self {
        Self::builder().build().expect("Default builder failed")
    }
}

impl ProxyConfig {
    pub fn builder() -> ProxyConfigBuilder;
}

// Generated by `#[derive(Builder)]`; the `#[builder(derive(..))]` above is
// what puts `Debug`, `Serialize` and `Deserialize` on the builder itself.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ProxyConfigBuilder { /* one Option per field */ }

impl ProxyConfigBuilder {
    // No `setter(into)` and no `setter(strip_option)`: the argument is the
    // whole `Option`, so these are called `.dns_port(Some(53))` and
    // `.dns_port(None)`, never `.dns_port(53)`.
    pub fn socks_port(&mut self, value: Option<u16>) -> &mut Self;
    pub fn dns_port(&mut self, value: Option<u16>) -> &mut Self;
    pub fn build(&self) -> Result<ProxyConfig, ConfigBuildError>;
}
```

Observable behaviour:

- `socks_port` defaults to `Some(9150)`; `dns_port` defaults to `None`. So
  `ProxyConfig::default() == ProxyConfig::builder().socks_port(Some(9150))
  .build().unwrap()`, and naming any other SOCKS port makes it unequal.
- Deserialization is `#[serde(deny_unknown_fields)]`: `{"dnsport": 53}` and
  `{"dns": 53}` are **errors**, not ignored keys. The two accepted keys are
  exactly `dns_port` and `socks_port`, and they may appear together.
- An explicit `null` means "disabled", and is not the same as absent for
  `socks_port`: `{}` deserializes to `ProxyConfig::default()` (SOCKS on 9150),
  while `{"socks_port": null}` does not.
- `build()` returns `Result<_, ConfigBuildError>` and cannot fail here,
  because both fields have defaults.

`ProxyConfig` and `ProxyConfigBuilder` are re-exported at the crate root under
this step, so `arti::ProxyConfig` reaches the same type as step 07's
`arti::cfg::ProxyConfig`. Step 07's §11.1 covers the same type in its own
context, but **this section is the binding statement for step 08** and is
self-contained: everything `dnsport` exercises is above. Where the two differ,
step 08's Oracle tree (`arti-v0.3.0`) wins — it widens
`#[builder(derive(Deserialize))]` to `#[builder(derive(Debug, Serialize,
Deserialize))]`. The interface is otherwise unchanged from step 07; only the
filesystem layout of the checks that exercise it differs.

---

## 10. `tor-circmgr::isolation` (inherited from step 07)

### 10.1 Re-exported items

```rust
pub use tor_circmgr::isolation::{
    Isolation,
    IsolationHelper,
    IsolationToken,
    StreamIsolation,
    StreamIsolationBuilder,
};
```

The items in §10.1 and their observable behaviour are unchanged from step 07,
and step 07's §3.1–§3.4 state that behaviour in full; they remain binding here.
The requirement groups re-exercise it on the cumulative workspace under the names
`isotuple`, `isostream` and `isotoken`. Those groups are **not** the step-07
files of the same purpose under a new label: their case names, their helpers and
their coverage differ, and `isotoken` in particular no longer exercises
`IsolationToken`'s `Ord` impl while it now calls
`IsolationHelper::{compatible_same_type, join_same_type}` directly. What is
checked is therefore the behaviour listed below, not any particular case list.

Per item reached by a requirement group:

- `Isolation` is a sealed trait. Core: `tor-circmgr::isolation.rs`.
- `IsolationHelper::{compatible_same_type, join_same_type}` are public, and are
  what `isotuple` (tuple impls) and `isotoken` (a concrete rule) call.
- `IsolationToken` is `#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd,
  Ord)] pub struct IsolationToken(u64);`. `new()` and `no_isolation()` are
  public, and compatibility is equality. Of those derives, **`Copy`, `Debug`
  and `PartialEq` are load-bearing here**: `isotuple` writes
  `assert_eq!((a, b).join_same_type(&(a, b)), Some((a, b)))`, which needs
  `Debug` to report a failure and `PartialEq` to decide one, and every helper
  passes tokens by value. `Ord` is derived but no case at this step uses it.
- `StreamIsolation::{no_isolation, builder}` are public.
- `StreamIsolationBuilder::new()` plus setters `owner_token(IsolationToken)`
  and `stream_isolation(Box<dyn Isolation>)`, and `build()`, which returns a
  `Result`; `isostream` calls `build()` and `.expect(...)`s it.
- Tuple impls for arities 1–12 exist and are public.

---

## 11. `tor-rtcompat` (new dependency for `skew_events`)

The conformance driver depends on `tor-rtcompat` with features `tokio` and
`native-tls` in order to construct a runtime for `GuardMgr::new`. This
dependency is already pulled in by `arti-client`.

```rust
// At the crate root this is a STRUCT, not a type alias: `tor-rtcompat` wraps
// the selected backend runtime in a newtype so that the backend does not leak
// into the public type. (`tor_rtcompat::tokio::PreferredRuntime`, a different
// path, *is* a re-export alias; the driver uses the crate-root type.)
#[derive(Clone)]
pub struct PreferredRuntime { /* private: the wrapped backend runtime */ }

impl PreferredRuntime {
    pub fn create() -> std::io::Result<Self>;
}
```

Observable behaviour:

- `PreferredRuntime::create()` returns a usable runtime for `GuardMgr::new`.
- It must satisfy `tor_rtcompat::Runtime`, because `skewevents` names the type
  `GuardMgr<PreferredRuntime>` and §2.2's impl block is bounded by `Runtime`.
- Whether you implement it as a newtype or as an alias is not itself checked —
  both are nameable — but the shape above is what `arti-v0.3.0` has.

---

## 12. Citation

All normative facts this step is checked against are stated in
`/app/SPEC_NOTES.md`, with precise citations to the pinned torspec commit
`1db24dddbf2203f2a465747302f133ef3ed6eed4`.

---

## 13. Requirement groups → interface items

The group names are those in `tests/assets/groups.txt`.

- **Gate** groups add no requirement of their own. Each must pass completely
  or the step is not satisfied, however much of the new work is finished. They
  are behaviour signed off at an earlier milestone, carried forward as a
  regression floor.
- **Requirement** groups carry this step's new work. Every case in one must
  pass.

| Group | Kind | Items |
|---|---|---|
| `skewmag` | requirement | §1 `ClockSkew::{magnitude, is_skewed, if_above}`, including the 15-minute reporting threshold §2.1 names |
| `skewsecs` | requirement | §1 `ClockSkew::{as_secs_f64, from_secs_f64}` and the `MIN` floor §1.1 states |
| `skeworder` | requirement | §1 `ClockSkew` `Ord` impl |
| `skewevents` | requirement | §2.1 `ClockSkewEvents::{get, Clone}`; §2.2 `GuardMgr::{new, skew_events}`. The `Stream` impl is declared in §2.1 but **no case polls it** — see the note under the table |
| `blockage` | requirement | §3.1 `BlockageKind` `Display` and `From<ConnBlockage>` (which lives in `arti-client`); §3.2 `ConnBlockage` `Display` |
| `stateclean` | requirement | §4.1 `FsStateMgr::{from_path, path}`, `StateMgr::try_lock`, `LockStatus::{variants, held}` |
| `safescrub` | requirement | §5.2 `Sensitive` formatting traits in the default (scrubbing) state |
| `safewrap` | requirement | §5.1 `Sensitive::{new, unwrap, From}` plus its `Deref`, `DerefMut`, `Default`, `Clone`, `PartialEq`/`Eq`, `PartialOrd`/`Ord` and `Hash` impls |
| `safesupp` | requirement | §5.3 `with_safe_logging_suppressed`; §5.2's delegating branch |
| `safeguard` | requirement | §5.3 `enforce_safe_logging`, `disable_safe_logging`, `Error`, `Display`; §5.2's delegating branch |
| `logcfg` | requirement | §8.1 `LoggingConfig::{default, builder}`, `LoggingConfigBuilder::{console, log_sensitive_information, build}` and its `Deserialize` |
| `fsmcheck` | requirement | §6.1 `Mistrust::{new, check_directory}`; §6.4 `Error` |
| `fsmverify` | requirement | §6.1 `Mistrust::verifier`; §6.2 `Verifier` builder methods |
| `fsmtrust` | requirement | §6.1 `Mistrust::{trust_no_group_id, trust_group_id, trust_admin_only, ignore_prefix, dangerously_trust_everyone}` |
| `fsmdir` | requirement | §6.1 `Mistrust::make_directory`; §6.2 `Verifier::{make_directory, secure_dir}`; §6.3 `CheckedDir` |
| `fsmerrs` | requirement | §6.4 `Error::{Display, path, errors, FromIterator}` |
| `retrytime` | requirement | §7.1 `RetryTime`, `AbsRetryTime`, `HasRetryTime` |
| `guardretry` | requirement | §7.3 `PickGuardError::{retry_time, kind, Display}`; §2.3 `FallbackList::{default, choose}` |
| `isotoken` | gate | §10.1 `IsolationToken::{new, no_isolation}`, `IsolationHelper` |
| `isostream` | gate | §10.1 `StreamIsolation`, `StreamIsolationBuilder` |
| `isotuple` | gate | §10.1 tuple `Isolation` impls |
| `dnsport` | gate | §9.1 `ProxyConfig`, `ProxyConfigBuilder::dns_port` |

### Declared here, not driven by any case

As everywhere in this package, the contract is the surface and not the call
list. The items below are named because later steps or real callers need them,
and no group in this step reaches them:

- `impl Stream for ClockSkewEvents` (§2.1). The type is declared, cloned and
  read through `get()`; nothing polls it, so a `Stream` impl that never yields
  passes. Implement it — the milestone asks for the event stream — but do not
  expect a case to confirm it.
- `SkewEstimate::{skew, noteworthy}` and its `Display` (§2.1). The section
  already says a non-`None` `SkewEstimate` is unreachable from an out-of-crate
  driver; the two accessors are part of the type's public shape and nothing
  asserts them.
- `ClockSkewEvents: Debug` (§2.1) — unlike its §4.1 analogue in step 04, no case
  renders it.
- `Error::Overflow`'s rejection in `safelog` (§5.3). It is reached by a guard
  whose count exceeds the type's range, which cannot happen in a single process
  run; the section states the behaviour and no case can reach it.
- `LoggingConfig`'s `journald` and `files` options (§8.1). They are declared and
  serialisable, and no case feeds them; `logcfg` drives `console` and
  `log_sensitive_information`.

Two consequences of the one real coverage gap this step has are worth stating
plainly, because §11 already writes the first one down: the *call sites* of
`sensitive()` inside `arti`/`arti-client`, and the `store`-without-the-lock,
`Sensitive`-serde-gating and unexported-`MIN` negatives, are documented
requirements with no assertion behind them. Implement them; the
`logcfg`/`safeguard` groups measure the mechanism, not its adoption.
---

## 14. What is deliberately out of scope at this step

**Five** things this step's milestone, its Migration note, or the
`SPEC_NOTES.md` sections `instruction.md` §2 makes normative are **out of scope
here** — none of them overlooked.

An earlier revision of this section justified all of them with one blanket
sentence: "each because the behaviour has no public observable at
`arti-v0.3.0`". **That sentence was false**, and not marginally: it is wrong
for §14.2, §14.3, §14.4 and §14.5, whose subjects are all public API at this
tag, and it is imprecise for §14.1, whose real barrier is stronger than the one
it named. A false universal in a preamble taints every item beneath it, so the
blanket claim is gone and each entry now carries the reason that actually
applies to it:

| reason | what it means | entries |
| --- | --- | --- |
| **structural** | the driver's dependency set, or a `pub(crate)` / `#[cfg(test)]` boundary, makes the observation *uncompilable* from an external crate. Nothing short of editing `harness/Cargo.toml` or upstream visibility would change that, and no runtime environment affects it | §14.1 |
| **checked elsewhere** | the behaviour is public and is checked — in another step of this same cumulative package, at a tag where it is in that driver's dependency set. Repeating it here would add no information | §14.2, §14.3 |
| **process-global** | the API is public, reachable and would compile, but exercising it mutates process-wide state that a single-binary run can only set up once | §14.5 |
| **scope** | the API is public, reachable and deterministic. It is excluded because it belongs to a different milestone — not because it cannot be tested | §14.4 |

A scope decision is a legitimate reason standing on its own. Writing
"impossible" where "out of scope" is the truth makes a binding document
unfalsifiable from the inside, which is a worse defect than the omission the
overstatement was covering.

Each entry says what stands in for it, so that a later reviewer can check
against this decision rather than re-finding the gap.

### 14.1 The clock-skew estimator itself

`tor_guardmgr::SkewEstimate` is a public type with no public way to build one.
`SkewEstimate::estimate_skew` is `pub(crate)`; the `SkewObservation` values it
consumes are `pub(crate)`; all three of its fields are private; and
`tor_proto::ClockSkew::from_handshake_timestamps` — the only producer of a raw
observation — is `pub(crate)` too. The single public route to a non-`None`
estimate is a *live* guard or fallback connection: `GuardMgr::select_guard()`
hands back a `GuardMonitor`, `GuardMonitor::skew()` records a value,
`GuardMonitor::report()` posts it to a background task, and the task republishes
through a watch channel.

**Reason: structural.** An earlier wording of this paragraph said that route
needs "a network". That is not the barrier and overstates the case:
`GuardMgr::select_guard(usage, netdir)` takes `netdir: Option<&NetDir>` and
accepts `None`, so the call itself contacts nothing, and `GuardUsage` is
publicly constructible. Two harder barriers apply instead, in this order:

1. **The driver cannot build a candidate for `select_guard` to return.** It can
   only hand back a `GuardMonitor` if there is a guard or a fallback to hand
   back. A `NetDir` comes from `tor-netdir`; a `FallbackDir` is built through
   `FallbackDirBuilder`, whose `rsa_identity` and `ed_identity` setters take
   `tor_llcrypto::pk::rsa::RsaIdentity` and
   `tor_llcrypto::pk::ed25519::Ed25519Identity`. **None of `tor-netdir`,
   `tor-llcrypto` or `tor-linkspec` is in this driver's dependency set**, so
   the setup code does not compile at all. That is a stronger and more durable
   justification than "we would need a network", because it holds regardless of
   the environment the verifier runs in. `GuardMonitor::new` is `pub(crate)`,
   so there is no side door.
2. **Even given a candidate, the republication is not observable on demand.**
   `report()` posts to a background task that republishes through a watch
   channel. The synchronisation point upstream uses for exactly this in its own
   tests, `GuardMgr::flush_msg_queue`, is `#[cfg(test)]` and therefore invisible
   to an external crate. What is left is a poll race, and checking is
   all-or-nothing per group.

What is checked instead, in §1.1 and `skewmag` / `skewsecs` / `skeworder`, is the
value type the estimator publishes together with the two predicates it applies:
the 2-second floor that `from_secs_f64` enforces, and the 15-minute reporting
threshold that decides whether an estimate is worth telling the user about. The
estimator computes exactly
`ClockSkew::from_secs_f64(mean).if_above(FIFTEEN_MINUTES)`, and `noteworthy()`
is false whenever that yields `ClockSkew::None`, so those cases exercise the
estimator's arithmetic on the surface where it is reachable. `skewevents`
checks the publication channel — `GuardMgr::skew_events()`, `get()`, `Clone`,
and the `tor-circmgr` re-export — in its empty state.

### 14.2 The NETINFO timestamp on the wire

**Reason: checked elsewhere.** `SPEC_NOTES.md` §1 is about the 4-byte
big-endian NETINFO timestamp and the client's obligation to send zero. This is
a public observable — `tor-cell` is a public crate — so the exclusion is not an
impossibility claim. Encoding and decoding that field needs `tor-cell` and
`tor-bytes`, which are not in *this* driver's dependency set, and the
requirement is already in scope elsewhere in the same cumulative package. The
requirement is checked in the **cumulative workspace at step 01**, group
`chanmsg` (`chanmsg__netinfo_for_client_omits_our_own_addresses` asserts the
four zero bytes; `chanmsg__netinfo_for_relay_lists_its_own_addresses` asserts a
non-zero big-endian value), and step 01's contract is still binding here because
`/app` is cumulative.

### 14.3 Certificate expiry checking

**Reason: checked elsewhere.** `SPEC_NOTES.md` §2 is about validAfter/validUntil
checking on link and identity certificates. This too is public API at this tag,
so the exclusion is not an impossibility claim. It needs `tor-cert` and
`tor-checkable`, neither of which is in *this* driver's dependency set; the
requirement is checked at step 01 (`edcert`) and again at step 10 (`certcheck`,
`timebound`, `timeliness`). What step 08 adds
on top — the *inference* a client draws when every relay's certificates look
expired — is checked here, by `blockage`: `ConnBlockage::CertsExpired` maps to
`BlockageKind::ClockSkewed`, not to `CantReachTor`.

### 14.4 The decorrelated-jitter retry algorithm

`SPEC_NOTES.md` §3 and §4 describe randomized exponential backoff for directory
downloads and for guard retries.

**Reason: scope — and the honest version of it, because the convenient version
would be false.** `tor_basic_utils::retry::RetryDelay` is **fully public** at
`arti-v0.3.0`: `from_msec`, `from_duration`, `next_delay<R: Rng>` and `reset`
are all `pub`, and `next_delay` takes the RNG *as a parameter*. Seeded with a
fixed `StdRng` it is therefore **deterministic and assertable as an exact
value**, not merely as a bound — and `rand = "0.8"` is already a dev-dependency
of this driver. One line added to `harness/Cargo.toml` would make a
`retrydelay` group possible. It is not the case that this behaviour has no
public observable at this tag.

It is excluded anyway, on scope. The jitter algorithm belongs to the
directory-download and guard-retry milestones; checking it here would add
coverage for a clause `instruction.md` §1 does not contain, in a step whose milestone
is clock skew and safe logging. That is a sufficient reason by itself and does
not need an impossibility propping it up.

The *guard-side* half of `SPEC_NOTES.md` §4 is a separate matter and is closer
to genuinely unreachable: the schedule that consumes `RetryDelay`
(`retry_schedule`, in `tor-guardmgr/src/guard.rs`) is a private function, and
`SPEC_NOTES.md` §4 already records that proposal 336's 6-hour cap is not
implemented at this tag, so a case written from the proposal's numbers would
assert something the release does not do.

What is checked is the part of the retry story that *is* public and
deterministic: `tor_error::{RetryTime, AbsRetryTime, HasRetryTime}` in
`retrytime`, and `tor_guardmgr::PickGuardError::retry_time()` in `guardretry` —
the values a caller sees, not the jitter that produced them.

### 14.5 The Migration clause's wiring: `setup_logging`, and the `sensitive()` call sites

`instruction.md`'s Migration note reads: *"Logs stop carrying target addresses
unless sensitive logging is explicitly enabled."* This step checks the
**mechanism on both sides** and the **edge between them not at all**. That is a
real coverage gap, and the consequence is uncomfortable enough to be worth
writing down rather than leaving for a reader to discover:

> A solver can implement `safelog` exactly as §5 specifies, never call
> `sensitive()` on anything anywhere in `arti` or `arti-client`, never wire
> `log_sensitive_information` to `disable_safe_logging()`, and still satisfy
> **every requirement group on this step**. The
> Migration sentence would be false of their tree and no
> group would notice.

**What is checked** — the mechanism:

- `Sensitive<T>`, and the nine formatting traits scrubbing to the literal
  `[scrubbed]` with no guard held: `safescrub`, `safewrap`, `safesupp`
  (§5.1, §5.2).
- `enforce_safe_logging` / `disable_safe_logging` and the guard arithmetic:
  `safeguard` (§5.3).
- `LoggingConfig::log_sensitive_information` existing and defaulting to
  `false`: `logcfg` (§8.1).

**What is out of scope**, with its public Oracle anchor:

| half of the Migration sentence | anchor at `arti-v0.3.0` | why it is out of scope |
| --- | --- | --- |
| "logs stop carrying target addresses" | `sensitive(&addr)` inside an `info!`, at `arti/src/socks.rs:166`, `arti/src/socks.rs:206` and `arti-client/src/client.rs:679`. Those are the only three uses of `safelog` outside the `safelog` crate that wrap a value | the wrap happens inside a `tracing` macro that evaluates to `()`. There is no value for an external crate to inspect, and the enclosing functions are async connection handlers that need a live SOCKS connection and a bootstrapped client to enter. `safescrub` checks `Sensitive<T>` generically, over `IpAddr` and `&str` fixtures of the harness's own making — never over an address that `arti` itself produced |
| "unless sensitive logging is explicitly enabled" | `arti::logging::setup_logging(config: &LoggingConfig, cli: Option<&str>)` at `arti/src/logging.rs:286` — **`pub`**, in a `pub mod` (`arti/src/lib.rs:120`) — which at `logging.rs:305-306` does `if config.log_sensitive_information { safelog::disable_safe_logging() }` and parks the resulting `safelog::Guard` in `LogGuards::safelog_guard` (`logging.rs:279`) | **process-global.** `setup_logging` calls `registry().init()` at `logging.rs:303`, installing the process-wide `tracing` subscriber. That succeeds at most once per process and panics thereafter, and this step's driver is a single binary running every case. A group that called it would take every later case in the same binary down with it, and each group is all-or-nothing |

**Reason: process-global on the half that is reachable, shape on the other.**
Neither row is a scope decision in disguise. The first has genuinely no value to
assert on. The second is a real, public, single-argument entry point that a
driver could call — exactly once — and it is excluded because "exactly once per
process" and "every case in one binary" cannot both hold, not because anything
is private.

**What stands in for it: nothing directly, and that is the honest answer.**
`logcfg` checks the flag, `safeguard` checks the function the flag would call,
and a reader holding both can see that the edge between them is a single `if`.
Closing it would need either a per-group test binary or one group spent on a
dedicated target that does nothing but call `setup_logging` once. Step 08 does
neither, and a later reviewer should check against this decision rather than
re-derive it.
