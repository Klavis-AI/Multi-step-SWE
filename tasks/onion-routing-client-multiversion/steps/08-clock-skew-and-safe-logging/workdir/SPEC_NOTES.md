# Specification notes — step 08 (`08-clock-skew-and-safe-logging`)

First-party notes on the normative facts this step is graded against. Every
statement below is my own summary of a numbered section of the Tor
specifications; no specification text is reproduced. Read the cited section if
you need the full wording.

All citations are against torspec at the pinned commit
`1db24dddbf2203f2a465747302f133ef3ed6eed4`. The URL pattern is

```
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/<file>#L<line>
```

---

## 1. NETINFO timestamp exchange and clock-skew inference

**tor-spec §4.5, "NETINFO cells"** —
[tor-spec.txt#L939](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L939)

After the TLS handshake and certificate exchange, both parties send a NETINFO
cell carrying each side's view of the peer's address and a timestamp. The
timestamp field is 4 bytes, big-endian unsigned integer, representing seconds
since the Unix epoch —
[tor-spec.txt#L964](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L964).

Clients SHOULD send zero as their timestamp to avoid fingerprinting —
[tor-spec.txt#L966](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L966).

Implementations MAY use the timestamp value to help decide if their clocks are
skewed —
[tor-spec.txt#L969](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L969).
Implementations SHOULD NOT trust these values unconditionally, especially when
they come from non-authorities, since the other party can lie about the time —
[tor-spec.txt#L974](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L974).

**Scope at step 08.** The wire format of the timestamp field is **not graded
here**: encoding and decoding it needs `tor-cell` and `tor-bytes`, which the
step-08 conformance driver does not depend on. It is graded in the cumulative
workspace at **step 01**, group `chanmsg`, which asserts the four zero bytes a
client sends and the big-endian encoding of a relay's value. What step 08 grades
is the *inference* built on top of the timestamp — the `tor_proto::ClockSkew`
value type, its 2-second floor and its 15-minute reporting threshold
(`TEST_INTERFACE.md` §1.1, groups `skewmag`, `skewsecs`, `skeworder`). The
producer that turns two timestamps into a `ClockSkew`,
`ClockSkew::from_handshake_timestamps`, is `pub(crate)` at this tag and has no
public observable; see `TEST_INTERFACE.md` §14.1.

---

## 2. Certificate validation and expired certificates

**tor-spec §4.2, "CERTS cells" (validation subsections)** —
[tor-spec.txt#L688](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L688)

To authenticate the responder as having both RSA and Ed25519 identities, the
initiator MUST check that all X.509 certificates have validAfter and validUntil
dates, and that no X.509 or Ed25519 certificates are expired —
[tor-spec.txt#L727](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L728).

To authenticate the responder as having a given RSA identity only, both Link
and ID certificates must have validAfter and validUntil dates that are not
expired —
[tor-spec.txt#L748](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L748).

When every relay a client can reach presents expired certificates, the client
may infer that its clock is set to the past rather than that every relay on the
network has simultaneously failed to renew.

**Scope at step 08.** Certificate *checking* is **not graded here**: it needs
`tor-cert` and `tor-checkable`, which the step-08 driver does not depend on. It
is graded at step 01 (`edcert`) and at step 10 (`certcheck`, `timebound`,
`timeliness`). What step 08 grades is the inference in the paragraph above:
`tor_chanmgr::ConnBlockage::CertsExpired` converts to
`arti_client::status::BlockageKind::ClockSkewed` rather than to `CantReachTor`
(group `blockage`). See `TEST_INTERFACE.md` §14.3.

---

## 3. Retry schedule randomization

**dir-spec §5.5, "Retrying failed downloads"** —
[dir-spec.txt#L3890](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3890)

When a client or cache fails to download a resource, it waits before retrying.
The wait time is computed using a randomized exponential backoff algorithm,
specifically a variation of the "decorrelated jitter" algorithm. The formula
for the i+1th delay is:

```
Delay_{i+1} = MIN(cap, random_between(lower_bound, upper_bound))
where upper_bound = MAX(lower_bound+1, Delay_i * 3)
      lower_bound = MAX(1, base_delay).
```

The value of `cap` is set to `INT_MAX`; the value of `base_delay` depends on
what is being downloaded, whether the client is fully bootstrapped, and where
it is downloading from —
[dir-spec.txt#L3904](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3904).

**Scope at step 08.** The jitter algorithm itself is **not graded here**. It
lives in `tor_basic_utils::retry::RetryDelay`, which is not in the step-08
driver's dependency set. Note that this is a **scope** decision, not an
impossibility: `RetryDelay` is fully public at this tag and `next_delay` takes
the RNG as a parameter, so a seeded RNG makes it deterministic and assertable
as an exact value. See `TEST_INTERFACE.md` §14.4, which states the reason in
full. What step 08 grades is the public, deterministic
*result* type a caller of a failed operation sees:
`tor_error::{RetryTime, AbsRetryTime, HasRetryTime}`, group `retrytime`. See
`TEST_INTERFACE.md` §14.4.

---

## 4. Guard retry schedule randomization

**guard-spec §4.5, "Retrying guards"** —
[guard-spec.txt#L445](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L445)

When a guard's reachability status is marked `<no>`, we decide whether to
update its status to `<maybe>` based on its last_tried_connect time, its
failing_since time, and a retry schedule. Primary guards and non-primary guards
use different schedules —
[guard-spec.txt#L450](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L450).

**Proposal 336** —
[proposals/336-randomize-guard-retries.md](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/proposals/336-randomize-guard-retries.md)

This proposal changes the fixed retry schedule to a randomized one using the
same decorrelated-jitter algorithm as directory retries. For primary guards,
`base_delay` is 30 seconds and `cap` is 6 hours —
[proposals/336-randomize-guard-retries.md#L48](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/proposals/336-randomize-guard-retries.md#L48).

The upside is that client behavior in the presence of an unreliable network
becomes harder for an attacker to predict, and guards whose clients probe them
in lock-step are staggered —
[proposals/336-randomize-guard-retries.md#L9](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/proposals/336-randomize-guard-retries.md#L12).

**Scope at step 08.** The guard retry schedule is **not graded here**. The
function that produces it, `retry_schedule`, is private to
`tor-guardmgr/src/guard.rs`. At `arti-v0.3.0` it does use the decorrelated
jitter of §3 — it returns a `tor_basic_utils::retry::RetryDelay` built from a
base delay of 30 seconds for a primary guard and 150 seconds for a non-primary
one — but nothing about that is reachable from outside the crate, `RetryDelay`
is in a crate the driver does not depend on. (The `RetryDelay` half is
excluded on scope rather than on impossibility — see `TEST_INTERFACE.md` §14.4;
the `retry_schedule` half really is private.) Note also that the proposal's 6-hour `cap` is **not** implemented at
this tag, so a case written from the proposal's numbers would assert something
the release does not do. What step 08 grades is the retry hint a *caller* can
see when guard selection fails: `tor_guardmgr::PickGuardError::retry_time()`,
together with its `kind()` and `Display`, in group `guardretry`. See
`TEST_INTERFACE.md` §14.4.

---

## 5. Bootstrap blockage diagnosis

When a client cannot make progress bootstrapping, it may diagnose the problem
by inspecting the pattern of failures: inability to establish TCP connections
suggests the client is offline; successful TCP connections but failed TLS
handshakes suggest filtering; and successful handshakes but universal
certificate-expiry errors suggest a skewed clock.

No single specification section mandates this inference, but the combination of
NETINFO timestamp exchange (§1), certificate validation (§2), and retry
scheduling (§3, §4) provides the observable data a client uses to classify the
blockage.

---

## 6. State-directory locking and obsolete-file removal

Arti's state persistence uses a lock file to coordinate multiple processes
accessing the same state directory. When the lock is acquired for the first
time, the state manager removes obsolete files left by earlier versions.

A file is considered obsolete when its extension is `toml` OR its stem is
`default_guards`. Obsolete files whose age (measured from mtime to the current
time) strictly exceeds 4 days are deleted; files dated in the future are kept.

This behavior is an internal implementation detail, not mandated by the
specifications. The graded interface is `FsStateMgr::from_path`,
`StateMgr::try_lock`, and the observable removal logic.

---

## 7. Safe logging

Safe logging is a feature where sensitive values (IP addresses, onion
addresses, user-supplied identifiers) are scrubbed from log output unless the
operator explicitly opts in to logging sensitive information.

At step 08, safe logging becomes the default: logs no longer carry target
addresses unless the operator sets `log_sensitive_information = true` in the
logging configuration.

The graded interface is the `safelog::Sensitive<T>` wrapper, whose formatting
traits output exactly the string `"[scrubbed]"` regardless of format flags, and
the global safe-logging enforcement functions `enforce_safe_logging()` and
`disable_safe_logging()`, which manipulate a global `AtomicIsize` flag and
return a guard that restores the previous state on drop.

---

## 8. Filesystem-permission checking

The `fs-mistrust` crate provides a suite of functions for verifying that files
and directories are readable and writable only by trusted users. This is
critical for state directories, cache directories, and configuration files: if
an attacker can write to these locations, they can compromise the client.

The basic check (`Mistrust::check_directory`) walks the path from the root to
the target, verifying that:

- Every ancestor is a directory owned by a trusted user and writable only by
  trusted users. The forbidden mask for an ancestor is `0o022` — the two write
  bits — so an ancestor may be group- and world-readable and -executable. An
  ancestor directory carrying the sticky bit (`0o1000`) is exempt entirely, on
  the grounds that a sticky directory such as `/tmp` does not let one user
  replace another's entries.
- The target itself is owned by a trusted user and neither readable nor
  writable by untrusted users: its forbidden mask is `0o077` — the whole group
  and other nibbles.
- Two settings widen those masks, and both are coarser than their names
  suggest:
  - `Verifier::permit_readable()` drops the target's mask from `0o077` to
    `0o022`, i.e. to the ancestor rule. Group and world may then **read and
    execute**; only write stays forbidden. Mode 0755 is accepted; mode 0777 is
    still rejected, reporting exactly `0o022`.
  - `Mistrust::trust_group_id(gid)` clears the **whole group nibble** from
    whatever mask applies, `forbidden &= !0o070`, for any object owned by that
    gid. Group read, write *and* execute become acceptable. Mode 0770 is
    accepted; mode 0777 is rejected, reporting exactly `0o007`.
- Only the bits that are both set on the object and present in the mask are
  reported, as `Error::BadPermission(path, bits)`.

The `Mistrust` object is configurable: it can be told to trust specific users
or groups, to ignore certain prefixes (e.g., the system temporary directory),
or to disable all checks.

The `Verifier` builder allows per-check customization: requiring a specific
object type (file vs. directory), permitting readable targets, collecting all
errors instead of stopping at the first, and recursively checking directory
contents.

`Verifier::make_directory` and `Mistrust::make_directory` **create every
missing component** of the path, each at mode 0700, and then re-check the
result. They do this by running the ordinary check first, treating a
`NotFound` outcome as "nothing is there yet" rather than as a failure, and
building recursively. A missing parent is therefore not an error; a parent
that exists with bad permissions or a bad owner is, and is returned before
anything is created.

The `CheckedDir` type represents a directory whose permissions have been
verified. It provides safe accessors for creating subdirectories and opening
files, with the guarantee that every operation enforces the same permission
rules.

`Error` variants represent the specific problems found: `BadPermission` names
the path and the offending permission bits; `BadOwner` names the path and the
untrusted uid; `BadType` indicates a type mismatch; `NotFound` indicates a
missing component; `Multiple` aggregates several errors; `Content` wraps an
error from a directory member; `InvalidSubdirectory` rejects paths that could
escape the checked directory.

This is an internal implementation detail of arti's state and cache management,
not mandated by the specifications. The graded interface is the entire public
API of `fs_mistrust`.

---

## 9. HTTP response headers

**dir-spec §6.1, "HTTP headers"** —
[dir-spec.txt#L3952](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3952)

Servers MAY include an `X-Your-Address-Is:` header, whose value is the apparent
IP address of the client connecting to them (as a dotted quad). For directory
connections tunneled over a BEGIN_DIR stream, servers SHOULD report the IP from
which the circuit carrying the BEGIN_DIR stream reached them —
[dir-spec.txt#L3982](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3982).

This header allows a client to learn which address its connections may be
originating from. It is not graded at step 08, but is mentioned here because
the `X-Your-Address-Is` header is one of the address-learning mechanisms
related to the broader clock-skew and bootstrap-diagnostics theme.

---

## 10. State instances (guard state separation)

**guard-spec §2, "State instances"** —
[guard-spec.txt#L82](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L82)

The guard-selection algorithm uses separate persistent instances of the sampled
guards, confirmed guards, and other derived values when `UseBridges` is set, or
when `EntryNodes` or other options impose a meaningful restriction on the guard
universe —
[guard-spec.txt#L88](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L88).

This is an internal implementation detail. The graded interface at step 08 is
only the `GuardMgr::new` constructor and the `skew_events()` method; the state
instance logic itself is not observable through the driver.
