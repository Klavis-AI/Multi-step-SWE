# Test interface — step 09

Every public item this step's verifier exercises, plus a small number of
neighbouring items that the exercised ones are defined in terms of and that a
solver must therefore implement for the checked ones to compile. The checker
imports these by crate and path; a rename or deletion breaks the test. Where an
item is present for context rather than checked, the sentence introducing it says
so, the group table above repeats it, and "Deliberately out of scope at
this step" (last section) lists the capabilities that have no in-scope surface at
all.

Three cargo features must exist and must compile, because the driver enables them:

| Crate | Feature | Why the driver needs it |
| --- | --- | --- |
| `fs-mistrust` | `walkdir` | gates `Verifier::check_content()`, which the directory-tolerance cases call to recursively check a tree's contents |
| `tor-rtcompat` | `tokio` | selects the Tokio runtime backing the async plumbing this step's cases run under |
| `tor-rtcompat` | `native-tls` | selects the native-tls backend paired with the Tokio runtime |

These are not optional extras. The driver's manifest names them, so cargo
resolves them before a single test runs. A crate that does not DECLARE the
feature - even as an empty entry in its `[features]` table - fails metadata
resolution, and the verifier then fails every group in this step at once,
with no per-group detail to point at the cause.

**The driver's path dependencies are the thirteen crates below, and every one
must exist under `/app/crates/` with its lib target named as written.** Deleting
or renaming any of them makes the whole manifest unresolvable, which is every
group — the gate included:

`arti`, `arti-client`, `fs-mistrust`, `safelog`, `tor-checkable`,
`tor-config`, `tor-dirclient`, `tor-dirmgr`, `tor-error`, `tor-netdoc`,
`tor-persist`, `tor-proto`, `tor-rtcompat`.

Two of them, `arti` and `tor-dirclient`, are resolved by the driver even though
no item this step checks lives in either: they are part of the workspace this
milestone delivers and the manifest names them by path. `WORKSPACE_HANDOFF.md`
states the preserve rule; this list is what makes it checkable.

## What this document supersedes

`/app` is cumulative, so every earlier step's `TEST_INTERFACE.md` is still on
disk under `/app/docs/step-NN/` and still describes behaviour this step
re-checks. Four statements those documents make were true at their own
checkpoint and are **false at this one**. Where they conflict with anything
below, this document wins.

1. **The state lock file moved inside the state directory.** Steps 2 and 8
   say the lockfile is `<p>/state.lock`, a *sibling* of `<p>/state/`. At this
   release `FsStateMgr` creates the checked directory `<p>/state/` first and
   then opens `<p>/state/state.lock` **inside** it, because the lock is now
   opened through the `CheckedDir` rather than beside it. A permission check
   over `<p>/state/` therefore covers the lock file too. Nothing in this step
   asserts the lockfile's path, but a solver who keeps the old layout will
   find the state directory's contents check behaving unexpectedly.
2. **`Mistrust`'s configuration methods moved to `MistrustBuilder`.** Step 8
   documents `trust_group_id`, `trust_no_group_id`, `trust_admin_only`,
   `ignore_prefix` and `dangerously_trust_everyone` as inherent `&mut self`
   methods on `Mistrust` itself. At this release `Mistrust` is immutable once
   built: those methods live on `MistrustBuilder`, reached through
   `Mistrust::builder()`, and the results are compared for equality rather
   than mutated. **`trust_group_id(gid: u32)` no longer exists at all** — it
   is replaced by the generated `setter(into)` setter
   `MistrustBuilder::trust_group(impl Into<TrustedGroup>)`, so the old call
   becomes `.trust_group(gid)` (through `impl From<u32> for TrustedGroup`) or
   `.trust_group(TrustedGroup::Id(gid))` written out. `trust_no_group_id()`
   survives, but only as a hand-written shorthand that assigns
   `TrustedGroup::None`.
3. **`tor_error::ErrorKind` has grown to 43 variants.** Step 6's contract
   writes `pub enum ErrorKind { /* 40 variants */ }` and enumerates them. That
   count was right at its own tag and is **wrong here**: at `arti-v0.4.0` the
   enum has **43**. Nothing was renamed and nothing was removed - the earlier
   forty names all still resolve - so step 6's enumeration stays usable as far
   as it goes; three variants were *added* on top of it:
   `ClockSkew` and `Other`, which appeared at step 7's tag and which no later
   contract has had occasion to name until now, and **`FsPermissions`**, new at
   *this* tag and is the one this step checks: `tor_persist::Error::Permissions`
   must report `ErrorKind::FsPermissions` through `HasKind`, and a solver whose
   `ErrorKind` still has step 6's forty variants has nowhere for that mapping to
   land. The enum remains `#[non_exhaustive]` and `Copy`, and its variants
   remain mutually exclusive, exactly as step 6 describes.
4. **`arti_client::config::dir` re-exports a different list.** Step 4's copy
   predates `DirSkewTolerance`/`DirSkewToleranceBuilder`, which this tag adds.
   It is **not** a superset, and the difference is not only an addition:
   **`DirMgrConfigBuilder` is gone.** Steps 1, 3 and 4 all name it — step 3's
   copy of this very re-export list includes it — and at this release
   `tor_dirmgr` does not define it at all, so `config::dir` cannot re-export
   it. `DirMgrConfig` is constructed by **struct literal** here — its seven
   fields are `pub`, and they are listed below — so a solver that carries step
   4's builder forward and re-exports it does not match this tag. Everything
   else step 4 named is still present. The list written below is the one that
   exists at this tag.

## fs-mistrust

**Module:** `fs_mistrust`

**`Mistrust`** — `#[non_exhaustive]`-free struct, `Clone + Debug + Eq + PartialEq`, built only through `MistrustBuilder`:
- `Mistrust::builder() -> MistrustBuilder`
- `Mistrust::new() -> Self` (equivalent to `Default::default()`)
- `Mistrust::new_dangerously_trust_everyone() -> Self`
- `Mistrust::verifier(&self) -> Verifier<'_>`
- `Mistrust::check_directory<P: AsRef<Path>>(&self, dir: P) -> Result<()>` — shorthand for `verifier().require_directory().check(dir)`
- `Mistrust::make_directory<P: AsRef<Path>>(&self, dir: P) -> Result<()>` — shorthand for `verifier().make_directory(dir)`
- `impl Default for Mistrust` — same as `MistrustBuilder::default().build()`

**`MistrustBuilder`** (derived via `derive_builder::Builder`, `build_fn(error = "Error")`):
- `.ignore_prefix(impl Into<PathBuf>) -> &mut Self` (generated setter; canonicalizes the prefix at `build()`). Canonicalization is not best-effort: a prefix that does not exist on disk makes `build()` itself fail, returning `Err(Error::NotFound(<the prefix as it was given>))` rather than silently leaving the prefix unset.
- `.trust_admin_only(&mut self) -> &mut Self` — unix-only; sets `trust_user = TrustedUser::None`, `trust_group = TrustedGroup::None`
- `.trust_no_group_id(&mut self) -> &mut Self` — unix-only; sets `trust_group = TrustedGroup::None`
- `.dangerously_trust_everyone(&mut self) -> &mut Self`
- `.remove_ignored_prefix(&mut self) -> &mut Self`
- `.trust_user(impl Into<TrustedUser>) -> &mut Self`, `.trust_group(impl Into<TrustedGroup>) -> &mut Self` (unix-only, generated setters)
- `.build(&self) -> Result<Mistrust, Error>`

**`TrustedUser`** — `#[non_exhaustive]` enum, `Clone + Debug + Eq + PartialEq`, `Default` is `Current`:
`None`, `Current` (default), `Id(u32)`, `Name(OsString)`. `From<u32>`, `From<OsString>`, `From<&OsStr>`, `From<String>`, `From<&str>` all implemented.

**`TrustedGroup`** — `#[non_exhaustive]` enum, same derives, `Default` is `SelfNamed`:
`None`, `SelfNamed` (default; trusts the group named identically to the current user, if the process is a member), `Id(u32)`, `Name(OsString)`. Same `From` impls as `TrustedUser`.

**`Verifier<'a>`** (`Clone + Debug`, `#[must_use]`, returned only from `Mistrust::verifier`) — builder-style, each method consumes and returns `Self`:
- `.require_file(self) -> Self`
- `.require_directory(self) -> Self`
- `.permit_all_object_types(self) -> Self`
- `.permit_readable(self) -> Self`
- `.all_errors(self) -> Self` — accumulate every violation into `Error::Multiple` instead of stopping at the first
- `.check_content(self) -> Self` — `#[cfg(feature = "walkdir")]`; implies `require_directory()`; walks the tree and rejects symlinks
- `.check<P: AsRef<Path>>(self, path: P) -> Result<()>`
- `.make_directory<P: AsRef<Path>>(self, path: P) -> Result<()>` — creates with mode `0o700` on unix if `Error::NotFound` was the only problem, then re-checks. Creation is **recursive**: `make_directory("a/b/c")` on a parent that exists but is empty creates all three components, each at `0o700`
- `.secure_dir<P: AsRef<Path>>(self, path: P) -> Result<CheckedDir>` — **implies `require_directory()`**, then wraps: it is `require_directory().check(path)?` followed by the wrap, so a path that is a regular file is rejected with `Error::BadType` even when the caller never asked for a directory
- `.make_secure_dir<P: AsRef<Path>>(self, path: P) -> Result<CheckedDir>` — the same implication, over `require_directory().make_directory(path)?`: creates-if-needed, then wraps

**The permission rule these checks apply** (unix). The verifier resolves the path, then walks it from the first component *not* covered by `ignore_prefix` down to the object itself, judging each step against a mask of **forbidden** bits and reporting `bad_bits = mode & forbidden_bits` when that is non-zero:

| Position on the path | Forbidden bits | In words |
| --- | --- | --- |
| the object you asked about | `0o077` | no group or other access of any kind — not read, not write, not execute |
| an intermediate directory | `0o022` | group/other *write* only; world-readable and world-searchable (`0o755`) is fine |
| an intermediate directory whose sticky bit (`0o1000`) is set | `0o000` | exempt: only an entry's owner may rename or remove it, so a `0o1777` ancestor is tolerated |
| content below a directory checked with `check_content()` | `0o022` | the ancestor rule again: readable content is fine, writable content is not |
| a member reached through a `CheckedDir` accessor | `0o022` | the content rule again |

Three adjustments apply on top of that table:

- `permit_readable()` moves the object you asked about from the first row to the second: its mask becomes `0o022`. So a `0o604` file is accepted and a `0o606` file still fails with `bad_bits == 0o002`. It does not relax write or execute. It does **not** carry the object into the third row either: the sticky-bit exemption is conditioned on the position being an *intermediate* directory, and the object you asked about never is one, however its own sticky bit is set.
- If the trusted group is the group that owns the object, the whole group triad is cleared from the mask (`forbidden_bits &= !0o070`), at every position. A group-writable `0o720` directory therefore fails with `bad_bits == 0o020` under `TrustedGroup::None` and is accepted under `TrustedGroup::Id(<its gid>)`.
- The sticky-bit exemption is for intermediate directories only. The identical `0o1777` mode on the object you asked about is judged by the first row and fails with `bad_bits == 0o077`.

Ownership is examined before the mode, and an object owned by a uid that is neither `0` nor the trusted user yields `Error::BadOwner(path, uid)`.

Symlinks are **resolved** before any of this: the walk follows them and checks the canonical path, so what gets a symlink's own mode and ownership examined is not the resolution step, and an error names the real directory the link resolved into rather than the link. One qualification, so the rule is not read as looser than it is: a symlink's own **owner** *is* checked before the walk follows it, so a link owned by an untrusted uid is a `BadOwner` naming the link. No fixture owns a link as an untrusted user, so no case observes that half; the mode is genuinely not consulted. The one place symlinks are rejected instead of followed is `check_content()`'s walk over a directory's contents — that rejection is a documented obligation, and **no case in this step exercises it**: `pathtolerance` drives the content rule through `check_content().check(..)` on trees whose members are ordinary files, and the `checkeddir` group's symlink case is about `CheckedDir::open`'s `O_NOFOLLOW`, which is a different mechanism. Implement the rejection; do not read its absence from the cases as permission to follow links under `check_content()`.

**`CheckedDir`** (`Clone + Debug`) — obtained only from `Verifier::secure_dir`/`make_secure_dir`. It remembers both the directory that was checked and the `Mistrust` that checked it, and every accessor below re-applies that `Mistrust` to the member it is handed, so holding a `CheckedDir` is not a licence to read an unsafe file inside it:
- `.as_path(&self) -> &Path` — the directory itself, in the canonical form the check resolved it to.
- `.join<P: AsRef<Path>>(&self, path: P) -> Result<PathBuf>` — `as_path()` with `path` appended, after a **lexical** confinement check on `path` alone. Any `RootDir` or `Prefix` component (so every absolute path, e.g. `/etc`) and any `ParentDir` component (`..`) is rejected with `Error::InvalidSubdirectory`; this includes a `..` that would cancel out, so `a/../../sibling` and even `a/../b` are refused. `CurDir` (`.`) and ordinary names are accepted. The check is purely lexical: nothing is opened, the member need not exist, and no permission check happens here — `join` returning `Ok` says only that the name stays inside, never that the member is safe to read.
- `.make_directory<P: AsRef<Path>>(&self, path: P) -> Result<()>` — `join`, then create the subdirectory with mode `0o700` on unix; an escaping `path` fails with `InvalidSubdirectory` and creates nothing.
- `.open<P: AsRef<Path>>(&self, path: P, options: &OpenOptions) -> Result<File>` — `join`, then open. Two overrides are applied to the caller's `OpenOptions` on unix: the creation mode is forced to `0o600` whatever the caller asked for, and `O_NOFOLLOW` is added so a symlink in the final position is an error rather than a redirection. The opened object is then permission-checked as **content** (`0o022` forbidden), and a failing check returns the error instead of the handle.
  **The `O_NOFOLLOW` refusal is an I/O error, and which family it belongs to is checked.** `open`'s failure maps through `Error::io(e, &path)` — the `Io` variant, carrying the link's own path — and *not* through the permission-checked-content path: a refused symlink is "the open failed", not "we looked at the link and disliked it", and not "nothing is there" (both the link and its target exist). So a case asserts `e.path() == Some(<the link>)`, `!e.is_bad_permission()`, and `!matches!(e, Error::NotFound(_))`. Returning `BadType(link)` — a reading the `BadType` sentence below invites, since `open` does check content — fails the `!e.is_bad_permission()` assertion, because `BadType` is one of the three leaf variants `is_bad_permission()` answers `true` for.
- `.read<P: AsRef<Path>>(&self, path: P) -> Result<Vec<u8>>` — `join`, then `open` for reading, so the member is permission-checked before its bytes are returned. Because the member is judged by the content rule, a `0o644` member reads fine and a `0o666` (or `0o622`) member fails with `Error::BadPermission(<the member's path>, 0o022)` — the plain variant, not wrapped in `Content`.
- `.read_to_string<P: AsRef<Path>>(&self, path: P) -> Result<String>` — as `read`, same check, decoding the bytes as UTF-8.
- `.write_and_replace<P: AsRef<Path>, C: AsRef<[u8]>>(&self, path: P, contents: C) -> Result<()>` — writes `contents` to a temporary file beside `path` inside the same checked directory (created `0o600` through `open`, so the replaced file ends up `0o600` too) and renames it over `path`, leaving no temporary behind. A concurrent reader sees either the old contents or the new ones, never a partial write.

**`Error`** — `#[non_exhaustive]`, `thiserror::Error`, `Clone + Debug`. Variants exercised by this step:

- `NotFound(PathBuf)` — nothing exists at that path. The path it carries is the **first missing component of the chain that was being walked**, not the path the caller asked about: checking `P/p/q/r` where `p` exists and `q` does not yields `NotFound(P/p/q)`. `ignore_prefix` is the exception, because its canonicalization is what failed, so it reports the prefix as it was given.
- `BadPermission(PathBuf, u32)` — unix only. The second field is **not** the object's `st_mode`: it is `mode & forbidden_bits`, the *subset* of the mode that this position on the path forbids, so it is always a subset of `0o777` and is never zero. A `0o707` directory asked about directly reports `0o007` (the whole "other" triad is forbidden for the target object), while the same `0o707` directory appearing as an *ancestor* reports `0o002` (only the write bits are forbidden there). See "The permission rule these checks apply" above for the mask per position. `Display` renders as `Incorrect permissions on file or directory {path}:` followed by two spaces and a spelled-out rendering of exactly those bits — `g=`/`o=` groups with `r`/`w`/`x` letters, e.g. `o=rwx` for `0o007` and `o=r` for `0o004` — and a triad whose bits are all permitted does not appear at all.
- `BadOwner(PathBuf, u32)` — unix only; second field is the untrusted UID that owns the object.
- `BadType(PathBuf)` — the object exists but is the wrong kind: a file where `require_directory()` was asked for, a directory where `require_file()` was, or a non-file/non-directory under `check_content()`.
- `Content(Box<Error>)` — the wrapper for a rule that failed on content *below* a directory this check already accepted. `path()` and `is_bad_permission()` both delegate to the inner error, so the wrapper is visible only when you match on the variant.
- `CouldNotInspect(PathBuf, Arc<io::Error>)` — the object could not be examined at all.
- `Multiple(Vec<Box<Error>>)` — produced only by `Verifier::all_errors()`. The wrapped errors are in the order the walk found them, which is **ancestor first and the target last**.
- `InvalidSubdirectory` — a `CheckedDir` member path that would leave the directory. It carries no path.
- `NoSuchGroup(String)` / `NoSuchUser(String)` — `MistrustBuilder::build()` could not resolve a `TrustedGroup::Name`/`TrustedUser::Name` in the host's databases. These are build-time errors, not check-time ones, and they carry the name rather than a path.

Methods:

- `.path(&self) -> Option<&Path>` — the path the error is about. The rule is total over the enum, so it is given in full here rather than only for the variants above:

  | Variant | `.path()` |
  | --- | --- |
  | `NotFound`, `BadPermission`, `BadOwner`, `BadType`, `CouldNotInspect`, `Io`, `NoTempFile` | `Some(_)` — the path the variant carries |
  | `Content(inner)`, `Listing(inner)` | whatever `inner.path()` says — the wrapper is transparent here |
  | `Multiple` | **`None`** — there is no single path; use `.errors()` |
  | `InvalidSubdirectory`, `NoSuchUser`, `NoSuchGroup`, `StepsExceeded`, `CurrentDirectory`, `CreatingDir`, `MissingField` | **`None`** — these carry no path at all |
- `.is_bad_permission(&self) -> bool` — "we looked, and we do not like what we found", as opposed to "we could not look" or "we were misconfigured". It is **true** for exactly three leaf variants:

  | Variant | `is_bad_permission()` |
  | --- | --- |
  | `BadPermission` | `true` |
  | `BadOwner` | `true` — the owner can change the mode at will, so a bad owner is a permission problem |
  | `BadType` | `true` — non-obvious, but a `require_directory()` check that found a file has found the wrong object, which the crate classes with the permission failures |
  | `NotFound`, `CouldNotInspect`, `StepsExceeded`, `CurrentDirectory`, `CreatingDir`, `Listing`, `Io`, `NoTempFile`, `MissingField`, `NoSuchGroup`, `NoSuchUser`, `InvalidSubdirectory` | `false` |
  | `Content(inner)` | whatever `inner` says |
  | `Multiple(v)` | `true` if **any** element is true |

- `.errors(&self) -> impl Iterator<Item = &Error>` — yields the wrapped list for `Multiple`, or the error itself alone (exactly one item) for every other variant, so a caller can write one loop for both shapes.

`pub type Result<T> = std::result::Result<T, Error>` — the crate's result alias.

## tor-persist

**Module:** `tor_persist`

**`StateMgr` trait:**
- `fn load<D>(&self, key: &str) -> Result<Option<D>> where D: DeserializeOwned`
- `fn store<S>(&self, key: &str, val: &S) -> Result<()> where S: Serialize`
- `fn can_store(&self) -> bool`
- `fn try_lock(&self) -> Result<LockStatus>`
- `fn unlock(&self) -> Result<()>`

**`LockStatus`** enum, `Debug + Copy + Clone + Eq + PartialEq` (the equality derives are load-bearing: the harness compares a returned status against a literal variant): `NoLock`, `NewlyAcquired`, `AlreadyHeld` — reports what `try_lock` did. `LockStatus::held(&self) -> bool` returns true exactly for the two acquired states, so `!LockStatus::NoLock.held()`.

**`FsStateMgr`** (`Clone + Debug`), the on-disk `StateMgr` implementation:
- `FsStateMgr::from_path_and_mistrust<P: AsRef<Path>>(path: P, mistrust: &fs_mistrust::Mistrust) -> Result<Self>` — internally calls `mistrust.verifier().check_content().make_secure_dir(statepath)`, so a world-writable state directory fails to construct
- `.path(&self) -> &Path`
- implements `StateMgr`

**`Error`** — `#[non_exhaustive]`, `Debug + Clone` and `thiserror::Error`, implements `tor_error::HasKind`. Exercised variants:

- `Permissions(fs_mistrust::Error)` — a permission/ownership failure on the state directory, carrying the wrapped error through `#[from]`. This type has **no inherent method block**: it offers neither `.path()` nor `.is_bad_permission()` of its own, and nothing is delegated. The wrapped error is reached by matching `Error::Permissions(inner)` and calling `inner.path()` / `inner.is_bad_permission()` on the `fs_mistrust::Error` itself. Its `HasKind::kind()` is **conditional on that inner error**, not fixed: `tor_error::ErrorKind::FsPermissions` when `inner.is_bad_permission()` is true — which is the case for every mode or ownership rejection this step produces — and `ErrorKind::PersistentStateAccessFailed` when it is not.
- `NoLock` — a **unit** variant (matched as `Error::NoLock`, no payload) that `store` returns when the manager does not hold the exclusive lock. A `store` before a successful `try_lock`, or after `unlock`, fails with this rather than writing, and it is the error side of `can_store() == false`.
- the I/O variants a `store`/`load` can raise.

## tor-config

**Module:** `tor_config` (re-exports below are at crate root unless noted)

**`config_crate`** — `pub use config as config_crate;`. This is the re-export of the
underlying `config` crate under a name that does not collide with `tor_config`'s own
`config` module, and it is the **only** way the harness reaches `config`-crate types:
it depends on `tor-config` alone and does not name `config` in its own manifest, so a
`config` that stayed private here would make those items unreachable. Reachable through
it, and used by the harness: `Config`, `ConfigError`, and the source trait and value
enum written below as `Source` / `ValueKind` (`config::Source`, `config::ValueKind`),
whose variants include `ValueKind::{String, Float, …}`.

**`ConfigurationSources`** (`sources` module), `Clone + Debug + Default` (`Default` is equivalent to `new_empty()`), the ordered list of config inputs:
- `ConfigurationSources::new_empty() -> Self`
- `ConfigurationSources::from_cmdline<F, O>(default_config_file: impl Into<PathBuf>, config_files_options: impl IntoIterator<Item = F>, cmdline_toml_override_options: impl IntoIterator<Item = O>) -> Self where F: Into<PathBuf>, O: Into<String>` — **three** parameters, in that order: the single default path to fall back on, the `-c`/`--config-file` arguments, and the `-o`/`--option` arguments. Its behaviour is a replacement, not an addition:
  - if `config_files_options` yields **at least one** entry, every one of them is `push_file`d (so each is *required*, and a missing one makes `load()` fail) and `default_config_file` is **never pushed at all**;
  - only if that iterable is empty is `default_config_file` pushed, and then via `push_optional_file`, so its absence is tolerated;
  - then every `cmdline_toml_override_options` entry is `push_option`ed, after all files, so overrides win over every file regardless of the order the caller assembled them in.
  Nothing else is set: the `Mistrust` is the default strict one until `set_mistrust` is called.
- `.push_file<P: Into<PathBuf>>(&mut self, p: P)`
- `.push_optional_file<P: Into<PathBuf>>(&mut self, p: P)` — missing file is tolerated, not an error
- `.push_option<S: Into<String>>(&mut self, opt: S)`
- `.set_mistrust(&mut self, mistrust: fs_mistrust::Mistrust)`
- `.mistrust(&self) -> &fs_mistrust::Mistrust`
- `.files(&self) -> impl Iterator<Item = &Path>` — the configured paths, in the order they were added, and **nothing else**: the item type is a bare `&Path`, with no "required"/"optional" flag beside it. That flag exists, but only as a private `enum MustRead { TolerateAbsence, MustRead }` that this release does not export, so `push_file` and `push_optional_file` are indistinguishable through this iterator and are told apart only by what `load()` does with a missing file. There is no `ConfigurationSource` type in `tor-config` at this tag — that name, and the richer item type it later gives this method, arrive in a later release.
- `.load(&self) -> Result<config::Config, ConfigError>`

**`load` checks each file it reads through the configured `Mistrust`, and that check is checked.** A file that exists must pass the content rule before its bytes are merged: a world-readable `arti.toml` (`0o644`) is fine — configuration files are not secrets — while a world-writable one (`0o666`) is refused and `load` returns `Err`. The optional/required flag governs **absence only**: a missing `push_optional_file` target is skipped, but an optional file that *does* exist and has unsafe permissions is still an error, because being optional excuses absence, not untrustworthiness. `set_mistrust` decides the checker, so the same sources read the same `0o666` file happily once a `new_dangerously_trust_everyone()` verifier is installed. The three rejection cases — writable refused, readable accepted, optional-but-writable refused — are all driven by `cfgsources`, and `SPEC_NOTES.md` §10 ("Configuration sources and resolution") states the rule normatively.

**`CmdLine`** (`cmdline` module, `Debug + Clone`, re-exported at the crate root as `tor_config::CmdLine`) — accumulates `--option key=value` overrides for the underlying `config` crate's source trait; parses `key=value` pairs, with an extended bareword-quoting mode where whitespace directly around `=` (e.g. a tab) is tolerated but a literal space around `=` is not. Three behaviours the harness depends on:

- **Bareword quoting.** The accumulated lines are joined into one TOML document, but `key=word` is not valid TOML, so a value that is a single unquoted bareword is rewritten to `key="word"` before parsing. Anything that already *is* valid TOML is left alone and keeps its type: `pi=3.5` becomes a float, `nums=[1, 2, 3]` an array, `quoted="a b"` a string. Dotted keys build nested tables, so `storage.cache_dir=somewhere` yields one top-level `storage` table.
- **Error rewriting.** The `config` crate reports parse failures against the assembled document, whose line numbers mean nothing to a user who typed one argument. So every error is rewritten before it escapes: the trailing `" at line N column M"` is stripped off the message, and in its place the text of line `N` — that is, the argument the user actually typed — is substituted. A failing `--option this is not toml` therefore produces a message containing `this is not toml` and containing no `column`. Because the substitution indexes the assembled document with the line the parser named, a duplicate-key error (which TOML reports against the *original* binding) is reported against the line that first bound the key, not the line that collided with it.
- **Late evaluation.** Nothing is parsed by `push_toml_line`; the document is assembled and parsed inside `collect()`, so a malformed argument surfaces there.
- **The underlying parser's own wording reaches the user, and the cases in scope look for it.** The substituted message is the `config` crate's (and, beneath it, the TOML parser's) text with the position clause rewritten, so two checked assertions are on fragments those crates choose: `"quoted string"` for a bareword with a space around `=`, and `"duplicate key"` for a repeated binding. That is why the `config` crate is a **pinned dependency** of this workspace rather than an incidental one: swapping it, or wrapping it behind a parser of your own, changes text the cases read. Rewrite the *position*, keep the parser, and leave the parser's own words alone.

Methods:
- `CmdLine::new() -> Self`
- `.push_toml_line(&mut self, line: String)` — appends one already-assembled TOML line; the harness builds every case through this rather than through the raw constructor
- `.collect() -> Result<HashMap<String, Value>, ConfigError>` (via `impl config_crate::Source for CmdLine`) — renders the assembled document and hands it to the `config` crate; the harness calls it on the trait to read back what the command line contributed

**`load` module** (the three `resolve*` functions are **also re-exported at the crate root**, as `tor_config::{resolve, resolve_return_unrecognized, resolve_ignore_unrecognized}`; the traits and `UnrecognizedKey` are reached through `tor_config::load::`):
- `pub trait Builder { type Built; fn build(&self) -> Result<Self::Built, ConfigBuildError>; }`
- `pub trait TopLevel { type Builder: DeserializeOwned; }`
- `pub trait Resolvable: Sized` — the bound on all three `resolve*` functions: "a thing that can be built out of one whole `config::Config`". Its methods are sealed/internal, so a downstream type never implements it by hand; it is obtained in exactly two ways.
  - A **blanket impl** covers every `TopLevel` whose builder builds it: `impl<T> Resolvable for T where T: TopLevel, T::Builder: Builder<Built = Self>`  (and `TopLevel` already requires `type Builder: DeserializeOwned`). So declaring `Builder` and `TopLevel` for your pair is all that is needed.
  - **Tuple impls** cover tuples of `Resolvable`s, generated for arities **1 through 5** (`(A,)`, `(A, B)`, ... `(A, B, C, D, E)`). Each element deserializes the *whole* configuration tree independently and takes the keys it knows — this is the crate's alternative to `#[serde(flatten)]` — and the unrecognized-key report is the *intersection*: a key is unrecognized only if **no** element recognized it. A single consumer is therefore resolved as a one-element tuple, `let (a,): (AConfig,) = resolve(cfg)?;`, and the trailing comma is load-bearing.
- `pub fn resolve<T: Resolvable>(input: config::Config) -> Result<T, ConfigResolveError>` — logs (does not fail on) unrecognized keys
- `pub fn resolve_return_unrecognized<T: Resolvable>(input: config::Config) -> Result<(T, Vec<UnrecognizedKey>), ConfigResolveError>`
- `pub fn resolve_ignore_unrecognized<T: Resolvable>(input: config::Config) -> Result<T, ConfigResolveError>` — returns the value alone and never reports keys
- `pub struct UnrecognizedKey { .. }` (private fields, `Debug + Clone + Hash + Eq + PartialEq + Ord + PartialOrd` — the total order is what makes the sorted result below well defined) — `impl Display for UnrecognizedKey` renders a TOML path: map entries dotted (`outer.inner`), array elements with a **bracketed index** immediately after the array's name (`list[0].surprise`), and any path segment that is not a bareword quoted. The `Vec` returned by `resolve_return_unrecognized` is **sorted** by that rendering, not left in configuration order, so `{zeta, alpha}` comes back as `["alpha", "zeta"]`. A key is reported at the deepest point some consumer stopped descending: if one consumer ignores all of `outer` and another descends into it and ignores only `outer.inner`, the report names `outer.inner`.
- `pub enum ConfigResolveError { Deserialize(#[from] config::ConfigError), Build(#[from] ConfigBuildError) }` — `#[non_exhaustive]`, `Debug` + `thiserror::Error`, and **not** `Clone` (unlike `ConfigBuildError`, which is `Debug + Clone + thiserror::Error + #[non_exhaustive]`). The two variants are distinguishable by their `Display` renderings, both asserted by this step: `Deserialize` renders `config contents not as expected: {0}` and `Build` renders `config semantically incorrect: {0}`.

**`CfgPath`** (`path` module), `Clone + Debug + Eq + PartialEq + Serialize + Deserialize` with `#[serde(transparent)]`. Only `new_literal` is
exercised by a requirement group (through `StorageConfigBuilder::cache_dir`); the
expanding forms and `CfgPathError` are documented because `StorageConfig`'s
defaults are written in terms of them and a solver must implement them for the
defaults to build, but no case in this step asserts an expansion:
- `CfgPath::new(s: String) -> Self` — a possibly-`~`/variable-expanding path
- `CfgPath::new_literal<P: Into<PathBuf>>(path: P) -> Self` — never expanded
- `.path(&self) -> Result<PathBuf, CfgPathError>`
- `.as_unexpanded_str(&self) -> Option<&str>`
- `.as_literal_path(&self) -> Option<&Path>`
- `impl Display for CfgPath` — a literal path renders as `{:?}` **followed by
  the literal text ` [exactly]`**; a shell path renders as its unexpanded string

**`CfgPathError`** — `#[non_exhaustive]`, `Debug + Clone` and `thiserror::Error`: `UnknownVar(String)`, `NoProjectDirs`, `NoBaseDirs`, `BadUtf8(String)`, `InvalidString(String)`.

## tor-dirmgr

**Module:** `tor_dirmgr`

**`DirSkewTolerance`** — this is the name the type carries **at this tag**; a later release renames it. Derives `Debug + Clone + Eq + PartialEq` (the equality derives matter: every case in this step compares two tolerances rather than reading a field, because the struct's fields are `pub(crate)` and cannot be read from outside the crate). It is built only through `DirSkewToleranceBuilder`:
- `DirSkewTolerance::builder() -> DirSkewToleranceBuilder` — an inherent associated function, equal to `DirSkewToleranceBuilder::default()`
- `impl Default for DirSkewTolerance` — defined as `Self::builder().build().expect(..)`, so `DirSkewTolerance::default() == DirSkewTolerance::builder().build().unwrap()` by construction
- `pre_valid_tolerance: Duration` (`pub(crate)`) — builder default expression is `Duration::from_millis(24 * 60 * 60)`, i.e. 86,400 milliseconds (≈86.4s); the field's own doc comment says "1 day," but that is a documentation error in the source tree, not the compiled default — this step does not assert this exact default value for that reason
- `post_valid_tolerance: Duration` (`pub(crate)`) — builder default `Duration::from_secs(3 * 24 * 60 * 60)`, i.e. exactly 3 days, per Tor Proposal 212
- Both fields are set through `DirSkewToleranceBuilder`'s generated setters, which take `Duration` by value, and read back only by comparing whole values — including by constructing a `DirMgrConfig` and comparing its `.tolerance`

**`DirSkewToleranceBuilder`** — generated by `derive_builder`, with `#[builder(build_fn(error = "ConfigBuildError"))]` and, crucially for this step, `#[builder(derive(Debug, Serialize, Deserialize))]`. That attribute is not decoration: it is what lets an arti configuration file carry a *fragment* of this section, and this step checks it.
- `Debug` — derived on the builder, so `format!("{:?}", DirSkewTolerance::builder())` names `DirSkewToleranceBuilder`.
- `Serialize` — every builder field is an `Option` that is skipped while unset, so an untouched builder serialises to an **empty** document. That is what makes a builder usable as a config fragment: an unmentioned key stays unmentioned and does not overwrite a lower-precedence source with a default.
- Both duration fields additionally carry `#[builder_field_attr(serde(with = "humantime_serde::option"))]`, so they are written and read in **humantime** form rather than as a raw second count: `Duration::from_secs(5)` renders as `pre_valid_tolerance = "5s"` and `Duration::from_secs(77)` as `post_valid_tolerance = "1m 17s"`.
- That `with` attribute also makes serialisation and deserialisation **asymmetric**. A field with a serde `with` attribute loses serde's implicit "a missing `Option` field is `None`", so although the builder writes only the fields that were set, it demands *all* of them back: deserialising a fragment that names only `post_valid_tolerance` fails with a *missing field* error naming `pre_valid_tolerance`. A fragment naming every field round-trips to an equal builder.

**`DirMgrConfig`** — `Debug + Clone` and *nothing else*: no `Eq`/`PartialEq` (so two configurations cannot be compared for equality; this step asserts on fields and on `Debug` instead), and a plain struct with **no builder and no non-test `Default`**, so it is constructed as a struct literal and every field must be named (or filled from another value with `..`). It has 7 public fields, of which `cache_trust` and `tolerance` are the two this step adds:
`cache_path: PathBuf`, `cache_trust: fs_mistrust::Mistrust`, `network: NetworkConfig`, `schedule: DownloadScheduleConfig`, `tolerance: DirSkewTolerance`, `override_net_params: netstatus::NetParams<i32>`, `extensions: DirMgrExtensions`.
- `.authorities(&self) -> &[Authority]` — `self.network.authorities`. A default-built `NetworkConfig` carries the **compiled-in** directory authorities that ship with the source, so this slice is non-empty without any configuration; dir-spec requires a client to know the authority set before it can validate anything, and arti ships at least three.
- `.fallbacks(&self) -> &tor_guardmgr::fallback::FallbackList` — note the return type: a `FallbackList` (`Debug + Clone + Default + Eq + PartialEq`), **not** a `&[FallbackDir]`. It offers `.len()`, `.is_empty()` and `.choose(&mut rng)` rather than slice indexing. Like the authorities, a default-built `NetworkConfig` carries a compiled-in fallback list of at least three entries, which is what a client contacts before it has any cache.
- `DirMgrExtensions` is `Default`-able and (with the `dirfilter` feature off) has no fields, so `extensions: Default::default()` is the normal way to fill it.

**`NetworkConfig`** / **`NetworkConfigBuilder`**, **`DownloadScheduleConfig`** / **`DownloadScheduleConfigBuilder`** — builder-pattern config types, each `Debug + Clone + Eq + PartialEq` with `#[builder(build_fn(error = "ConfigBuildError"))]` and `#[builder(derive(Debug, Serialize, Deserialize))]` (the same builder-derives pattern as `DirSkewToleranceBuilder` above; `NetworkConfigBuilder` additionally carries `#[builder(build_fn(validate = "Self::validate", ...))]`). Unchanged in shape at this step, re-exercised through `DirMgrConfig.network`/`.schedule`. `NetworkConfig::builder().build().unwrap()` and `DownloadScheduleConfig::default()` both yield the compiled-in defaults, including the authority and fallback lists above.

## arti-client

**Module:** `arti_client`

**`config::dir`** — a re-export module, not a wrapper: `pub use tor_dirmgr::{Authority, AuthorityBuilder, DirMgrConfig, DirSkewTolerance, DirSkewToleranceBuilder, DownloadSchedule, DownloadScheduleConfig, DownloadScheduleConfigBuilder, FallbackDir, FallbackDirBuilder, NetworkConfig, NetworkConfigBuilder};` — every name here is the identical `tor_dirmgr` type, so `arti_client::config::dir::DirSkewTolerance` and `tor_dirmgr::DirSkewTolerance` are the same type.

**`config::circ`** — the companion re-export module: `pub use tor_circmgr::{CircMgrConfig, CircuitTiming, CircuitTimingBuilder, PathConfig, PathConfigBuilder, PreemptiveCircuitConfig, PreemptiveCircuitConfigBuilder};` — again the identical `tor_circmgr` types.

**`StorageConfig`** — `#[non_exhaustive]`, `Clone + Debug + Eq + PartialEq`, with `#[builder(derive(Debug, Serialize, Deserialize))]` on its builder, built through `StorageConfigBuilder` (`StorageConfig::builder()` / `Default` come from `impl_standard_builder!`). Three **private** fields: `cache_dir: CfgPath` (builder default `${ARTI_CACHE}`), `state_dir: CfgPath` (builder default `${ARTI_LOCAL_DATA}`), `permissions: fs_mistrust::Mistrust` (a sub-builder). Because the fields are private the harness sets them through `StorageConfigBuilder`'s `.cache_dir(impl Into<CfgPath>)` / `.state_dir(impl Into<CfgPath>)` and reads them back only through `TorClientConfig`'s `Debug`.

**`fs_permissions_checks_disabled_via_env() -> bool`** — a free function in `arti_client::config`, and the **environment escape hatch** this step introduces. It returns `true` exactly when the variable `ARTI_FS_DISABLE_PERMISSION_CHECKS` is *present* in the process environment. Presence is the whole test (`std::env::var_os(..).is_some()`), so **any** value turns the checks off — including the empty string and the string `0` — and only removing the variable re-enables them. It is public rather than internal so that a tool outside `arti-client` can apply the same rule to its own files.

**`default_config_file() -> Result<PathBuf, CfgPathError>`** — expands `${ARTI_CONFIG}/arti.toml`; this is the path `ConfigurationSources::from_cmdline` is normally handed as its first argument.

**`TorClientConfig`** — `#[non_exhaustive]`, `Clone + Debug + Eq + PartialEq + AsRef<..>`, derived `Builder` with `#[builder(build_fn(error = "ConfigBuildError"))]` and `#[builder(derive(Serialize, Deserialize, Debug))]`, and `impl tor_config::load::TopLevel for TorClientConfig { type Builder = TorClientConfigBuilder; }`. It has exactly **10** fields, every one of them private and every one of them a `#[builder(sub_builder)]`. In particular there is **no** field or accessor named `dir`:

| Field | Type |
| --- | --- |
| `tor_network` | `config::dir::NetworkConfig` |
| `storage` | `StorageConfig` |
| `download_schedule` | `config::dir::DownloadScheduleConfig` |
| `download_tolerance` | `config::dir::DirSkewTolerance` |
| `override_net_params` | `tor_netdoc::doc::netstatus::NetParams<i32>` |
| `path_rules` | `config::circ::PathConfig` |
| `preemptive_circuits` | `config::circ::PreemptiveCircuitConfig` |
| `circuit_timing` | `config::circ::CircuitTiming` |
| `address_filter` | `ClientAddrConfig` |
| `stream_timeouts` | `StreamTimeoutConfig` |

- `TorClientConfig::builder() -> TorClientConfigBuilder` and `impl Default for TorClientConfig`, both generated by `impl_standard_builder! { TorClientConfig }`
- `.fs_mistrust(&self) -> &fs_mistrust::Mistrust` — the `Mistrust` held in `storage.permissions`. It is the one field accessor the type offers; the others are reachable only through the builder.
- `TorClientConfigBuilder::from_directories<P: AsRef<Path>, Q: AsRef<Path>>(state_dir: P, cache_dir: Q) -> Self` — a defaulted builder with `storage().state_dir(..)` and `storage().cache_dir(..)` set from `CfgPath::new_literal`, everything else left at its default
- **Sub-builder accessors.** `#[builder(sub_builder)]` replaces the usual `fn field(&mut self, value: T) -> &mut Self` setter with `fn field(&mut self) -> &mut <nested>Builder`: it takes **no argument** and returns a mutable reference to the nested builder, which the caller then sets fields on. This is the only way to reach any of the ten fields, and this step names seven of the accessors:
  - `.storage() -> &mut StorageConfigBuilder`
  - `.download_tolerance() -> &mut DirSkewToleranceBuilder` — the accessor that carries this step's new clock-skew tolerance into a client configuration. Both the field and the accessor are named `download_tolerance` **at this tag**; a later release renames them, so an implementation that follows the later name does not satisfy this step.
  - `.tor_network() -> &mut NetworkConfigBuilder`
  - `.override_net_params() -> &mut HashMap<String, i32>` — here the "sub-builder" is the map itself, declared with `field(type = "HashMap<String, i32>", build = ..)` and converted into a `NetParams<i32>` at `build()`
  - `.path_rules() -> &mut PathConfigBuilder` — whose `.ipv4_subnet_family_prefix(u8)` setter (builder default `16`) is the one this step sets through it
  - `.address_filter() -> &mut ClientAddrConfigBuilder` — whose `.allow_local_addrs(bool)` setter (builder default `false`) is the one this step sets through it. **Both types live at `arti_client::config`**: `ClientAddrConfig` and `ClientAddrConfigBuilder` are re-exported from `arti_client::config` (not from `config::dir`, not from the crate root and not from a private module), and that is the path the driver imports them by, so a definition anywhere else fails `storagecfg` to compile.
  - `.download_schedule() -> &mut DownloadScheduleConfigBuilder`

## tor-proto (inherited from step 8)

**Module:** `tor_proto::util::skew` (re-exported at crate root as `tor_proto::ClockSkew`)

**`ClockSkew`** enum, `Copy + Clone + Debug + Eq + PartialEq` with a hand-written (not derived) `Ord`/`PartialOrd`: `Slow(Duration)`, `None`, `Fast(Duration)`. That custom `Ord` (magnitude-independent ordering: `Slow < None < Fast`, and within `Slow`/`Fast` larger duration is "more skewed").
- `.magnitude(&self) -> Duration`
- `.as_secs_f64(&self) -> f64` (negative for `Slow`, positive for `Fast`, `0.0` for `None`)
- `ClockSkew::from_secs_f64(seconds: f64) -> Option<Self>` — `None` when `seconds`
  is not finite. Step 08's contract states the same signature; where this
  document once said `-> Self` it was simply wrong, not a step-09 change.
- `.if_above(self, min: Duration) -> Self` — **takes and returns `Self` by
  value**, yielding `ClockSkew::None` when the skew does not exceed `min`.
  Not `Option<Self>`, and not by reference.
- `.is_skewed(&self) -> bool`

  `from_secs_f64`, `if_above` and `is_skewed` are stated here so that their
  signatures are not guessed — step 08's contract already required them and they
  must keep compiling — but **no group at this step exercises them**; the
  `skewinfer` row below repeats that exception.

## tor-checkable (inherited from step 1)

**Module:** `tor_checkable`

**`Timebound<T>` trait:**
- `type Error;`
- `fn is_valid_at(&self, t: &SystemTime) -> Result<(), Self::Error>`
- `fn dangerously_assume_timely(self) -> T`
- `fn check_valid_at(self, t: &SystemTime) -> Result<T, Self::Error>` (defaulted)
- `fn check_valid_now(self) -> Result<T, Self::Error>` (defaulted)
- `fn check_valid_at_opt(self, t: Option<SystemTime>) -> Result<T, Self::Error>` (defaulted)

  The last two read the host clock, so **no group exercises them**: every
  timeliness case here passes an explicit instant to `check_valid_at`. They are
  stated because they are part of the trait an implementation must provide.

**`TimeValidityError`** — `#[non_exhaustive]`, `Debug + Clone + Eq + PartialEq` and `thiserror::Error`. The equality derives are load-bearing: the harness compares a returned error against a literal `TimeValidityError::Expired(d)` rather than matching on it. Variants: `NotYetValid(Duration)`, `Expired(Duration)`, `Unspecified`.

**`TimerangeBound<T>`** (`timed` module, private fields; note it derives **nothing** — no `Debug`, no `Clone`, no `PartialEq` — so a case can only observe it by consuming it through the `Timebound` methods below):
- `TimerangeBound::new<U: RangeBounds<SystemTime>>(obj: T, range: U) -> Self`
- `.extend_tolerance(self, d: Duration) -> Self` (`#[must_use]`) — widens the end bound, leaving the start where it was
- `.extend_pre_tolerance(self, d: Duration) -> Self` (`#[must_use]`) — widens the start bound, leaving the end where it was

  These two are the public form of this step's tolerance for an **untimely**
  document: `DirSkewTolerance`'s two widths are defined in terms of them, and
  `skewtol` checks them directly — an expired or not-yet-valid object is
  admitted once the matching width is applied, and still refused outside the
  widened window.
- implements `Timebound<T>` with `Error = TimeValidityError`

## tor-netdoc (inherited from step 1)

**Module:** `tor_netdoc::doc::netstatus`

**`Lifetime`** (`Clone + Debug`, and no equality derives; private fields `valid_after`, `fresh_until`, `valid_until: SystemTime`):
- `Lifetime::new(valid_after: SystemTime, fresh_until: SystemTime, valid_until: SystemTime) -> Result<Self>` — rejects unless `valid_after < fresh_until < valid_until` strictly, returning the crate's own error for that case (a `Result`, never a panic; which error it is and what its message says are out of scope)
- `.valid_after(&self) -> SystemTime`
- `.fresh_until(&self) -> SystemTime`
- `.valid_until(&self) -> SystemTime`
- `.valid_at(&self, when: SystemTime) -> bool` — inclusive at both ends: true exactly when `valid_after <= when <= valid_until`. `skewtol` uses it to observe that a tolerance widened a lifetime, rebuilding the `Lifetime` with `valid_after - pre_valid_tolerance` and `valid_until + post_valid_tolerance` and leaving `fresh_until` alone.

**`NetParams<T>`** — the map of integer network parameters a consensus governs, `Debug + Clone + Default + Eq + PartialEq`. It is the type of `DirMgrConfig::override_net_params`, so this step constructs one (as `NetParams<i32>`) to build that struct and reads a key back out:
- `NetParams::new() -> Self` — an empty map
- `.get<A: AsRef<str>>(&self, key: A) -> Option<&T>` — the value under a key, or `None`
- `.set(&mut self, key: String, value: T)` — insert or replace one key

## safelog (inherited from step 8)

**Module:** `safelog`

**`Sensitive<T>`** — wraps a value so its `Display`/`Debug` render `[scrubbed]` unless unsafe logging is enabled. Its derives all come from one `#[derive(Educe)] #[educe(Clone(bound), Default(bound), Deref, DerefMut, Eq(bound), Hash(bound), Ord(bound), PartialEq(bound), PartialOrd(bound))]` — that is, it forwards `Clone`, `Default`, `Deref`, `DerefMut`, `Eq`, `Hash`, `Ord`, `PartialEq` and `PartialOrd` to `T`, each bounded on `T` implementing the same trait. It deliberately does **not** derive `Debug` or `Display`; those are written by hand so that they can scrub. Under the `serde` feature it is additionally `Serialize + Deserialize` with `#[serde(transparent)]`.
- `Sensitive::new(value: T) -> Self`
- `Sensitive::unwrap(sensitive: Sensitive<T>) -> T` (associated function, not a method)
- `sensitive<T>(value: T) -> Sensitive<T>` (free function, crate root)

**`with_safe_logging_suppressed<F, V>(func: F) -> V where F: FnOnce() -> V`** — runs `func` with a thread-local flag set (via `fluid_let!`) that **suppresses safe logging**, i.e. turns scrubbing OFF for the duration of the call: while the flag is set, `Sensitive<T>`'s `Display`/`Debug` render the wrapped value in the clear instead of `[scrubbed]`. Read the name as "safe logging is suppressed", not "the value is suppressed"; it is the temporary, thread-scoped counterpart of the process-wide
`disable_safe_logging()`.

```rust
let s = safelog::sensitive("visible");
assert_eq!(format!("{}", s), "[scrubbed]");
assert_eq!(safelog::with_safe_logging_suppressed(|| format!("{}", s)), "visible");
```

The flag is thread-local and scoped to the call: it is restored when `func` returns (including on unwind), it is not observed by other threads, and it does not change the process-wide setting that `enforce_safe_logging()` and
`disable_safe_logging()` control (those are the only two public flag setters;
the flag's reader is crate-private). The step-08 contract states the same behaviour, and `SPEC_NOTES.md` §11 records why the crate spells it this way — the suppression is of the *protection*, for a caller that has already decided this one rendering is safe.

---

## Requirement groups → interface items

Every group this step uses, and the items above that it exercises. A group is
checked only through items named in this document; anything reachable but
undocumented is out of scope.

- **Gate** groups add no requirement of their own. Each one must pass
  completely or the step is not satisfied, however much of the new work is
  finished. They are the regression floor: behaviour signed off at an earlier
  milestone, plus proof that this step's artifact builds.
- **Requirement** groups carry this step's new work. Every case in one must
  pass.

A candidate that changes nothing therefore satisfies none of the new work.

| Group | Kind | Items |
|---|---|---|
| `mistrustcfg` | requirement | `fs-mistrust` — `Mistrust`, `MistrustBuilder` (`trust_admin_only`, `trust_no_group_id`, `dangerously_trust_everyone`, `ignore_prefix`, `remove_ignored_prefix`), `Mistrust::{builder, new, new_dangerously_trust_everyone}` |
| `trustedid` | requirement | `fs-mistrust` — `TrustedUser`, `TrustedGroup` variants and their `From` impls |
| `mistrusterr` | requirement | `fs-mistrust` — `Error` variants (`NotFound`, `BadPermission` incl. the forbidden-bit subset and its `Display` rendering, `BadOwner`, `BadType`, `Content`, `Multiple` incl. its ancestor-first order), `.path()`, `.is_bad_permission()` (incl. `BadType → true`), `.errors()` |
| `pathtolerance` | requirement | `fs-mistrust` — "The permission rule these checks apply" in full: the `0o077`/`0o022`/sticky-`0o000` masks, `permit_readable`, `check_content`'s content rule, symlink resolution; plus `Verifier::{require_file, require_directory, check}` and `Mistrust::check_directory` |
| `checkeddir` | requirement | `fs-mistrust` — `Verifier::{secure_dir, make_secure_dir}`, `CheckedDir::{as_path, join, make_directory, open, read, read_to_string, write_and_replace}` with `join`'s lexical escape rule, `open`'s forced `0o600` and its `O_NOFOLLOW` refusal of a symlink member, `write_and_replace` leaving nothing behind but the target, and the content rule applied to members |
| `statemgrperm` | requirement | `tor-persist` — `StateMgr`, `FsStateMgr::from_path_and_mistrust`, `LockStatus::{NoLock, AlreadyHeld, NewlyAcquired}` with `held()`, `tor_persist::Error::{Permissions, NoLock}` (the wrapped `fs_mistrust::Error` reached by matching `Permissions(inner)`, since the type has no inherent accessors of its own), `HasKind`/`ErrorKind::FsPermissions` |
| `cfgsources` | requirement | `tor-config` — `ConfigurationSources` (`new_empty`, the three-argument `from_cmdline` and its replace-the-default rule, `push_file`, `push_optional_file`, `push_option`, `set_mistrust`, `mistrust`, `files` yielding `&Path`, `load`) |
| `cfgresolve` | requirement | `tor-config::load` — `resolve`, `resolve_return_unrecognized`, `resolve_ignore_unrecognized`, `Resolvable` (blanket impl and tuple impls), `Builder`, `TopLevel`, `ConfigResolveError` (both `Display` renderings), `ConfigBuildError::Invalid`, `UnrecognizedKey` (sorted order, dotted and bracketed rendering, deepest-ignore rule), reached through `tor_config::config_crate as config` |
| `cmdlineopts` | requirement | `tor-config` — `CmdLine::{new, push_toml_line}`, the `config_crate::Source` impl (`collect`), bareword quoting, dotted keys, and the error rewriting that replaces `at line N column M` with the argument text; `tor_config::config_crate::{Source, ValueKind}` |
| `skewtol` | requirement | `tor-dirmgr` — `DirSkewTolerance` (`Eq`, `Default`, `builder()`) and `DirSkewToleranceBuilder`'s derived `Debug`/`Serialize`/`Deserialize` with `humantime_serde::option` and its asymmetric round trip; **and what the two widths do**: `tor-checkable`'s `TimerangeBound::{extend_tolerance, extend_pre_tolerance}` admitting an expired / not-yet-valid object at a fixed instant while still refusing one outside the widened window and while leaving the opposite bound alone, and the same arithmetic on a `tor-netdoc` `netstatus::Lifetime` (`valid_after - pre`, `valid_until + post`, `fresh_until` unmoved) observed through `Lifetime::valid_at` |
| `dirmgrcfg` | requirement | `tor-dirmgr` — `DirMgrConfig` (all 7 fields, by struct literal), `.authorities()` (at least three compiled-in), `.fallbacks()` (a compiled-in `FallbackList` of at least three entries), `NetworkConfig`, `DownloadScheduleConfig`, `NetParams`; `arti_client::config::dir` naming the identical types |
| `storagecfg` | requirement | `arti-client` — `TorClientConfig::builder()`, `TorClientConfigBuilder::from_directories`, and the sub-builder accessors `.storage()` (with `StorageConfigBuilder::{cache_dir, state_dir}` and `CfgPath::new_literal`), `.download_tolerance()` (`post_valid_tolerance`), `.override_net_params()` (a map entry reaching `NetParams<i32>`), `.path_rules()` (`ipv4_subnet_family_prefix`), `.address_filter()` (`allow_local_addrs`), plus `.tor_network()` and `.download_schedule()`, whose return types are pinned to the `config::dir` re-exports and whose fields are checked to hold the compiled-in defaults; every one of them read back through `TorClientConfig`'s `Eq`/`Debug`; `config::fs_permissions_checks_disabled_via_env()` |
| `safelogbase` | gate | `safelog` — `Sensitive::{new, unwrap}`, `sensitive()`, `Deref`, and `with_safe_logging_suppressed()` **revealing** the wrapped value (unchanged from step 08) |
| `skewinfer` | gate | `tor-proto` — `ClockSkew`'s three variants and their `Debug`, `.magnitude()`, `.as_secs_f64()` (negative for `Slow`), and the custom `Ord` (unchanged from step 08). `from_secs_f64`, `if_above` and `is_skewed` are documented above but **not** exercised by any group at this step. |
| `timebound` | gate | `tor-checkable` — `Timebound<T>` (`is_valid_at`, `dangerously_assume_timely`, `check_valid_at`, and the defaulted `check_valid_at_opt`), `TimeValidityError::{NotYetValid, Expired}`, `TimerangeBound::{new, extend_tolerance, extend_pre_tolerance}` — unchanged since **step 01**, which is where this crate's surface entered the task and where all of it is stated; step 08's contract mentions `tor-checkable` once only, in an out-of-scope note saying the crate is not in that step's driver dependency set, so do not look for it there. `extend_tolerance`/`extend_pre_tolerance` are **not** exercised by this group; they are checked in `skewtol`, alongside the widths that drive them. |
| `lifetimebase` | gate | `tor-netdoc` — `netstatus::Lifetime::{new, valid_after, fresh_until, valid_until}` and `new`'s strict ordering rule. The whole surface is unchanged since **step 01**, whose contract states `Lifetime::new` (rejecting unless `valid_after < fresh_until < valid_until` strictly) alongside the three accessors; this group is a gate group because none of it is anything step 09 introduces. `valid_at` **is** new here and is **not** exercised by this group; it is checked in `skewtol`, as the observation that a tolerance widened the window. |

---

## Deliberately out of scope at this step

Named here so that neither an implementer nor a later reviewer has to infer it
from silence. Each of these is real behaviour of the release; none of it is
checked at step 09, and the reason is always the same shape — no public item
exists through which a driver could observe it without reaching into crate
internals or standing up a network.

| Capability | Why it is out of scope here |
| --- | --- |
| `DirSkewTolerance`'s **own** two widening methods | `DirSkewTolerance::extend_tolerance` and `extend_lifetime` are `pub(crate)` at this tag and cannot be named from a driver at all. What they compute is not out of scope: `skewtol` checks exactly that arithmetic on the public `TimerangeBound` and `Lifetime` surface they are written in terms of. What is out of scope is a running `DirMgr` reaching for them on a document it downloaded. |
| Timeliness of a **parsed, downloaded** consensus | Consensus parsing is not on this step's public surface, so no driver can produce a real `UncheckedConsensus` to time-check. The timeliness *rule* itself — the default answer to an untimely document is "no", and a configured tolerance is a named, bounded relaxation of that "no" — **is** checked, in `skewtol`, on the `TimerangeBound`/`Lifetime` surface the release itself performs it with. `SPEC_NOTES.md` §§3–5 are the normative source. |
| Refusing an **invalid** consensus | Signature and authority-certificate checking need a directory to check against and are not reachable here. `SPEC_NOTES.md` §6 explains why an invalid document is not merely an untimely one — it is why the tolerance knobs must not leak into validation — but no public item at this step exposes the check. |
| Retry/download **scheduling** alongside tolerance | `DownloadScheduleConfig` is constructed (`dirmgrcfg`) but never driven; exercising a schedule needs a running `DirMgr` and a network. `SPEC_NOTES.md` §8 is background. |
| `CheckedDir` behaviour on **non-unix** platforms | Every permission case is unix-specific by construction (`BadPermission`/`BadOwner` are `#[cfg(unix)]` and the fixtures `chmod`). The verifier runs on Linux. |
| `fs-mistrust`'s **hard-link** limitation | Documented upstream as a known gap in the content rule; no assertion depends on it. |
| The `dirfilter` feature's `DirMgrExtensions::filter` | Off in the driver's feature set, so the struct is empty and filled with `Default::default()`. |
| `Verifier::permit_all_object_types` | **Declared, out of scope.** The signature is above because the method is part of the builder's public surface and a caller composing a `Verifier` may name it; no case calls it, and this document states no behaviour for it. Implement it as the release does — it lifts the file/directory type requirement for the object being checked — and do not look for a case that confirms it. |
| `Verifier::{enforce_safe_logging, disable_safe_logging}` at process scope | The two process-wide toggles in §`safeguard`'s neighbour section. `safeguard` drives the *guard* object and its `Display`; flipping the global switch is observable only through the behaviour of every later `Display` in the process, which a single test thread cannot isolate. Implement them; nothing checks the flip. |
| `default_config_file()` | Called by the client's own startup path, and its answer depends on the host's environment and home directory. No case calls it — asserting anything about it would be a platform test rather than a contract test. |

Everything else that appears above **is** reachable from a requirement group; where
an item is documented for context rather than checked, the sentence introducing
it says so, and the group table above repeats the exception.

**Sub-clauses inside otherwise-covered behaviours that no case isolates.** Each
requirement below is stated above, is implemented by the release, and sits
inside a group whose other cases do exercise the surrounding behaviour — so the
group never *only* checks them, and none of them is separately asserted.
Implement them as written:

- `write_and_replace`'s **atomicity**. Its case asserts the final directory
  listing (no temporary left behind and the target replaced), not that a
  concurrent reader sees the old or the new contents rather than a partial
  write.
- `Verifier::make_directory`'s **recursion past one level**. A single-level
  creation is asserted; `make_directory("a/b/c")` on an empty parent is not, so
  an implementation that creates only the last component passes.
- `CheckedDir::open`'s **post-open content check**. The cases assert that the
  member's mode is forced and that an escaping path is refused; they do not open
  a `0o666` member and expect the content rule to reject the handle.
- `Resolvable`'s **tuple arities 3, 4 and 5**. Arity 1 and 2 are exercised
  (`cfgresolve`); the wider tuples are declared above and undriven.
- `ErrorKind`'s **full variant count**. The step's groups need
  `ErrorKind::FsPermissions` to exist and to be what a permission failure maps
  to; the remaining variants arrive with the taxonomy and are checked at the
  step that introduces them.
