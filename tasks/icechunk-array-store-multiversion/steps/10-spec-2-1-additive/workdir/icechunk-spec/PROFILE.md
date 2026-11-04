# PROFILE.md — reading this corpus for step 10 ("spec 2.1")

Author-owned notes. The documents beside this file are verbatim upstream and
are never edited; where one of them is ambiguous, incomplete for a Rust reader,
or true only of an earlier release, the resolution is written **here** instead.

Resolutions accumulate: everything below still holds at this milestone unless a
later entry says otherwise. The contract these resolutions feed into is
`/app/TEST_INTERFACE.md`.

---

## R1 — The specification is the target; the release is the authority

Every milestone of this task implements one upstream release. Where a
specification sentence and the release that ships alongside it disagree, the
**release's observable behaviour** is what a grader can check, and it is what
`/app/TEST_INTERFACE.md` states. The earliest milestone made this unavoidable:
the first published spec-1 document (`archive/spec-alpha1.md`) specified
16-byte object ids, a `$ROOT/r/ s/ a/ m/ c/` key layout, JSON metadata files and
an attributes file, while the release that shipped with it used 12-byte ids,
`snapshots/ manifests/ chunks/ refs`, msgpack metadata, and no attributes file
at all. Read the spec for intent and the contract for the observable.

## R2 — The metadata file header has been 39 bytes since the binary format landed

```
bytes  0..12   magic, UTF-8 "ICE🧊CHUNK"
bytes 12..36   implementation name, UTF-8, left-aligned, padded to 24 bytes
byte  36       spec version
byte  37       file type   1 Snapshot, 2 Manifest, 4 TransactionLog, 6 RepoInfo
byte  38       compression 0 none, 1 zstd
bytes 39..     the compressed payload
```

The header itself is never compressed, and the padding of the implementation
name has been written as ASCII spaces throughout. A reader must tolerate
trailing NUL padding as well, and must treat everything after byte 38 as opaque.

## R3 — Never assert the implementation-name string

The implementation name is `"ic-" + CARGO_PKG_VERSION`, so it changes at every
release. Assert its **shape** — an `"ic"` application name and a present version
— never its text. The same caution applies to any error message, log line or
`Debug` rendering: they are not part of the contract at any milestone.

## R4 — Spec-version bytes so far, and what a version *name* means

| released as | header byte 36 | what changed |
| --- | --- | --- |
| spec 1 (0.1.x through 1.x) | `1` | the original format |
| "format frozen as 1.0" (the 1.0 release line) | `1` | **nothing on disk**. "1.0" was the Python package version and a compatibility promise, not a new spec version. |
| spec 2 (2.0.x) | `2` | the `repo` table-of-contents object, refs and config moved into it, `num_chunks` per dimension, `moved_nodes` |
| **spec 2.1 (2.1.0)** | **`2`** | one optional field appended to `SnapshotInfo`; nothing else |

The lesson repeats: **a version number in a release name is not a version byte.**
Check `reference/version-policy.md` — `## On-Disk Format` before assuming one
implies the other.

## R5 — Spec 2.1's byte is deliberately unchanged, and what that means for a test

This is the resolution this milestone turns on.

`spec.md` — `## Changes from spec version 2 to 2.1` says, in its own words, that
the on-disk format flags repositories as version `2` for both spec 2 and spec
2.1. `reference/version-policy.md` — `## On-Disk Format` says the header byte
equals the spec **major** version only and that minor-version features are
detected at read time. `design/016-expired-transaction-logs.md` —
`### Non breaking format change` says outright: *we do not bump the spec
version*.

Three consequences, all of which the contract depends on:

1. **`SpecVersionBin` gains no variant.** It is still `V1 = 1, V2 = 2`, and
   `SpecVersionBin::current()` is still `V2`. An implementation that adds a
   `V2_1` variant, or writes a `3` byte, is wrong, not forward-looking.
2. **No test can distinguish spec 2.0 from spec 2.1 by reading a header**, and
   none should pretend to. Asserting "this object is spec 2.1" from byte 36 is
   unanswerable; asserting that byte 36 is `2` *before and after* the 2.1
   feature is used is exactly the documented property.
3. **Compatibility is therefore graded as behaviour, not as a version stamp.**
   The observable difference between a "2.0 object" and a "2.1 object" is
   whether any snapshot carries `pruned_ancestor_tx_logs`. So the two directions
   the documents promise become two ordinary round trips:
   * write with the field present (create, commit, expire), close, reopen, and
     the list reads back unchanged, oldest first;
   * write without it (create, commit, never expire), close, reopen, and every
     snapshot reads as "never expired" — an empty list, no error, no migration,
     and history operations unchanged.

   Both report header spec version `2`, which is the point.

## R6 — Minor-version compatibility runs in both directions

`reference/version-policy.md` — `### Compatibility` promises that any 2.x
library reads and writes any spec 2.y repository. The half this milestone can
demonstrate is the 2.1 library's: it must read 2.0-shaped objects as "never
expired" and keep writing objects a 2.0 library can still read. The other half —
a 2.0.x binary operating on a 2.1 repository — needs a second library version
and is not exercised here; `design/016` — `## Compatibility` records what
happens then (an old GC deletes the referenced logs and the repository degrades
to pre-2.1 behaviour, which is why every consumer handles a missing referenced
log).

## R7 — A missing referenced log is not a uniform failure

`design/016` — `## Compatibility` splits the behaviour deliberately, and the
split is normative:

* `diff` and `inspect` **warn and skip**: an incomplete answer is better than no
  answer for a read-only view.
* `rebase` **errors**: silently skipping a log would hide a conflict, which is a
  correctness hazard, so it must fail loudly with an error that names the log.

## R8 — The documentation is written for Python users

`guide/` and most of `reference/` describe the Python binding. At this release
several documented capabilities have **no Rust counterpart**: the typed,
closable async iterator (`AsyncCloseableIterator` and `aclose()`), the
`DeprecationWarning` for `None` credentials, the module-level credential
singletons, the `atexit` telemetry flush, and the wheel/ABI changes. The Rust
APIs still return a bare `impl Stream`. A documented feature is not evidence of
a Rust surface; `/app/TEST_INTERFACE.md` §11 lists what is deliberately out of
scope.

## R9 — Container url prefixes end in a separator

Settled at the asynchronous-API milestone and still true: a
`VirtualChunkContainer` url prefix **must end in `/`**, and matching is a literal
prefix test on that string. This is a security property, not a formatting
nicety — without the trailing separator, `s3://bucket/authorized/` would also
cover `s3://bucket/authorized-evil/`. This milestone extends the same reasoning
to the rest of the URL: everything except a `file://` path is now stored
verbatim, so userinfo, port, query, fragment and empty path segments are all
part of the identity of a location, and the port is part of the identity of a
container.

## R10 — Some documented policy is implementation policy, not format

The inline-chunk threshold (512 bytes by default), the compression level, cache
sizes, concurrency limits and retry settings are configuration, not
specification: nothing in `spec.md` constrains them and nothing about them is
graded. Likewise, `guide/guides-observability.md` documents a set of environment
variables (`ICECHUNK_LOG`, `ICECHUNK_LOG_TO_STDOUT`, `ICECHUNK_NO_LOGS`,
`ICECHUNK_OTLP_ENDPOINT`, `OTEL_EXPORTER_OTLP_ENDPOINT`,
`ICECHUNK_OTEL_SERVICE_NAME`, `ICECHUNK_OTEL_FILTER`) whose effects are logging
and telemetry only; OTLP export additionally needs the non-default `otel` Cargo
feature and a live collector.

## R11 — Where `pruned_ancestor_tx_logs` actually lives

`spec.md` places the field on `SnapshotInfo` inside the **repo info file**, not
inside a snapshot file. That distinction is easy to miss and it has a visible
consequence: a `SnapshotInfo` reconstructed from a snapshot file alone cannot
know the list and leaves it empty, while the repository's table of contents
carries it. Read `spec.md` — `#### Repo Info File` before assuming a snapshot
file round-trips the whole of a `SnapshotInfo`.

## R12 — Superseded copies are on disk, and none of them is normative

`/app/icechunk-spec/` is **overlaid** by each milestone, not replaced, and
upstream renamed most of its documentation twice while this ladder ran. So the
directory holds **35 files copied at an earlier release**, under names this
release no longer uses: 33 under `guide/`, plus `flatbuffers/object_ids.fbs`
(superseded by `common.fbs`) and `reference/spec.md`.

`reference/spec.md` is the one that matters here. It is the **previous**
milestone's copy of the specification: it presents spec version **2** as the
current one and has no `## Changes from spec version 2 to 2.1` section.
Upstream split that file into `reference/spec-v2-1.md` and
`reference/spec-v2.md` at this release, but a split cannot delete a file from an
overlay, so the old one is still beside them. The same applies to
`guide/spec.md`, which is the spec **1** document from milestones 01-05.

The resolution:

* **The normative specification at this milestone is the top-level `spec.md`**,
  which is byte-identical to `reference/spec-v2-1.md`. `reference/spec-v2.md`
  and `reference/spec-v1.md` are the frozen earlier generations and are correct
  as history.
* **Every file not listed in this milestone's `SHA256SUMS` is non-normative.**
  `SHA256SUMS` covers exactly the 73 files this step laid down; anything else
  in the tree came from an earlier release. `README.md` tabulates the stale names
  against their current successors.
* Nothing in this milestone's contract depends on any superseded copy, and no
  requirement is stated only in one. Where a stale file and a current one
  disagree, the current one wins; where the corpus and `/app/TEST_INTERFACE.md`
  disagree, `TEST_INTERFACE.md` wins (R1).

## R13 — The node-path ordering rules changed, and none of them is graded here

`common.fbs` and `transaction_log.fbs` are, textually, a comment-only change
from the previous milestone — but the comments are normative and two of them say
something new:

* `common.fbs` previously declared node-path sort order to be **component-wise**
  (split on `/`, compare segments; `/a/b < /a-b`). At this release it declares
  the **raw UTF-8 byte order of the whole path**, which reverses that pair
  (`/a-b < /a/b`, because `-` is 0x2D and `/` is 0x2F).
* `transaction_log.fbs` newly declares a **third** order for `moved_nodes`:
  segment-wise, so that a node is immediately followed by its descendants, and
  it says explicitly that this is *not* the byte order used for snapshot nodes.

So "the ordering notes were clarified" would be an understatement, and a reader
who assumes the previous milestone's rule still holds is reading a superseded
statement rather than a stale file (R12's case).

The resolution: **neither ordering is graded at this milestone.** No scored
group asserts the order of nodes inside a snapshot, a manifest or a transaction
log, and `moved_nodes` is only ever written by `Session::move_node`, which
`/app/TEST_INTERFACE.md` §11 puts out of scope here. The rules are recorded so
that an implementation that has to sort can find the current one, not because a
test reads the result.
