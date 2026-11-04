# PROFILE.md — how this task reads the Icechunk documents

*This file is written by the task, not copied from upstream. So are `README.md`
and `NOTICE`; every other file in this directory is verbatim upstream
documentation.*

The documents in this directory are a real project's real documentation. They
lag the code in places, they describe a Python binding rather than the Rust
crate in others, and one design document describes a design that was never
built. This file records every such gap that matters to this milestone, and
fixes how the task resolves it.

**Where this file and a copied document disagree, this file wins.** Where this
file is silent, the copied document is normative.

Entries are numbered by the milestone that introduced them. Earlier entries are
carried forward because the behaviour they describe is still current.

---

## Carried forward from earlier milestones

### P1 — from milestone 01 (the spec-1 repository format)

* **P1-1 — Object ids are 12 bytes, not 16.** `archive/spec-alpha1.md` says an
  `ObjectId` is 16 bytes. Every id in the implementation — snapshot, manifest,
  chunk, attributes — is 12 bytes, rendered as 20 Crockford-base32 characters.
  Node ids are 8 bytes. The 12-byte reading is normative for this task.
* **P1-2 — The object layout is the one in `spec.md` §"File Layout", not the
  single-letter layout in the archived spec.** Prefixes are `config.yaml`,
  `refs/`, `snapshots/`, `manifests/`, `transactions/` and `chunks/`. There is
  **no** `attributes/` prefix: attributes were never implemented and the
  constant is commented out in the source.
* **P1-3 — A branch is one mutable file, a tag is one immutable file.** A
  branch lives at `refs/branch.$NAME/ref.json` and is advanced by an atomic
  conditional update. A tag lives at `refs/tag.$NAME/ref.json` and is written
  once; deleting it writes a sibling `ref.json.deleted` marker rather than
  removing the file, so a deleted tag name can never be reused. The archived
  spec's Crockford sequence-number scheme and its `1099511627775` commit
  ceiling do not exist.
* **P1-4 — A ref file's JSON has exactly one member, `snapshot`, whose value is
  the Crockford-base32 rendering of the snapshot id.** The archived spec's
  `properties` and `timestamp` members do not exist.
* **P1-5 — The inline-chunk threshold is implementation policy, not format.**
  `RepositoryConfig::inline_chunk_threshold_bytes` defaults to 512; a chunk at
  or under it is stored inline in the manifest, a larger one becomes its own
  object under `chunks/`. No document states the number.

### P2 — from milestone 02 (sessions, conflicts, virtual chunks)

* **P2-1 — `Repository` hands out sessions; data operations live on
  `Session`.** There is no `Repository::commit`, no `Repository::add_array`
  and no `Repository::set_chunk_ref`. A writer calls
  `Repository::writable_session(branch)`, mutates the session, and commits the
  session. A reader calls `Repository::readonly_session(&VersionInfo)`.
* **P2-2 — A read-only session records edits but can never publish them.**
  `Session::add_group` and friends succeed on a read-only session because they
  only touch the in-memory change set; it is `Session::commit` that fails, with
  `SessionErrorKind::ReadOnlySession`, because the session has no branch.
* **P2-3 — Conflict detection is not conflict resolution.**
  `conflicts::detector::ConflictDetector` reports conflicts and solves none;
  `conflicts::basic_solver::BasicConflictSolver` solves them according to its
  configured version selection.

### P3 — from milestone 03 (expiration, garbage collection, file headers)

* **P3-1 — Expiration never releases the repository root.** `expire` and
  `expire_ref` re-parent the oldest surviving snapshot onto the root snapshot
  and report every skipped snapshot as released — except the root itself, which
  is always kept.
* **P3-2 — Garbage collection is reachability-based and reports counts.**
  `ops::gc::garbage_collect` returns a `GCSummary` counting chunks, manifests,
  snapshots, attributes and transaction logs deleted, plus bytes. Nothing
  reachable from a branch or a tag is ever deleted.
* **P3-3 — Metadata objects are binary, not JSON.** `spec.md` shows JSON
  schemas for snapshot and manifest files. Those are illustrative of the
  *contents*; the objects themselves are a fixed header followed by a
  compressed binary payload. Reference files and `config.yaml` are the only
  text formats.

### P4 — from milestone 04 (the FlatBuffers format)

* **P4-1 — The 39-byte header is exact.** Bytes 0–11 are the UTF-8 magic
  `ICE🧊CHUNK`; bytes 12–35 are the writing client's id, UTF-8, right-padded
  with ASCII spaces to exactly 24 bytes, of the form `"ic-" + crate version`;
  byte 36 is the spec version; byte 37 is the file type (`1` snapshot,
  `2` manifest, `3` attributes, `4` transaction log); byte 38 is the
  compression algorithm (`0` none, `1` zstd). The header is never compressed.
* **P4-2 — The payload is FlatBuffers, not MessagePack.** Bytes 39 onward are a
  zstd frame that decompresses to a bare FlatBuffers root buffer. The schemas
  in `flatbuffers/` are normative for its contents.
* **P4-3 — Chunk objects and reference files carry no header.** A chunk object
  is the bytes the writer produced, unchanged.

### P5 — from milestone 05 (the CLI and expiration accounting)

* **P5-1 — The CLI reads one configuration file, and its location is
  `dirs::config_dir()/icechunk/cli-config.yaml`.** On Linux that is
  `$XDG_CONFIG_HOME/icechunk/cli-config.yaml`, falling back to
  `$HOME/.config/icechunk/cli-config.yaml`.
* **P5-2 — Repository definitions are an externally-tagged enum, so the YAML
  uses a `!Variant` tag,** not a nested mapping:

  ```yaml
  repos:
    local1: !LocalFileSystem
      path: /some/path
      config: {}
  ```

  A nested-mapping spelling parses to an empty repository map and then fails
  with "Repository not found in config". Every `RepositoryConfig` field is
  optional, so `config: {}` is valid.
* **P5-3 — `config init` and `config add` are interactive** and need a
  terminal. Only `repo create`, `snapshot list`, `config list` and the help
  output are scriptable.
* **P5-4 — Exit statuses.** A successful command exits `0`. A usage error —
  no subcommand, an unknown subcommand, an unknown flag — exits `2` with the
  help or error text on stderr. Any other failure exits `1` with a single
  `Error: ` line on stderr.

---

## New at this milestone (06)

### P6-1 — The split-condition grammar the benchmark fixes

`guide/performance.md` §"Configuring splitting" describes the grammar through
its Python spelling. The grammar this task fixes, in Rust terms, is:

```
ManifestSplitCondition ::= AnyArray                  -- selects every array
                         | PathMatches { regex }     -- regex over the array's full path
                         | NameMatches { regex }     -- regex over the array's name
                         | And(Vec<ManifestSplitCondition>)   -- all clauses; empty vec is TRUE
                         | Or (Vec<ManifestSplitCondition>)   -- any clause;  empty vec is FALSE

ManifestSplitDimCondition ::= Axis(usize)            -- axis index equality
                            | DimensionName(String)  -- the String is a REGEX over the name
                            | Any                    -- any REMAINING axis

ManifestSplitDim        ::= { condition: ManifestSplitDimCondition, num_chunks: u32 }
ManifestSplittingConfig ::= { split_sizes: Option<Vec<(ManifestSplitCondition,
                                                       Vec<ManifestSplitDim>)>> }
```

Four things the documents do not say, and which this task fixes:

1. **The regexes are unanchored searches.** `"temp"` selects `/temperature`.
   An exact match needs `^...$`. `guide/performance.md` says this for the
   *preload* conditions only; it holds for the split conditions too.
2. **A regex that does not compile evaluates to `false`, silently.** There is
   no error path and no diagnostic: a rule whose condition cannot compile
   simply never fires. The same is true of a dimension-name regex.
3. **`split_sizes` is an ordered list of rules, not a map, and the first match
   wins per axis** — both across rules and within one rule's list of dimension
   specs. Once an axis has been claimed, no later spec revisits it. This is
   what makes one rule listing three dimension specs and three separate rules
   carrying those same specs in the same order produce the same grid.
4. **A ragged tail is kept as its own manifest.** Splitting an axis of
   `n` chunks every `s` chunks produces the edge list
   `[0, s, 2s, …]` with `n` appended when `s` does not divide `n`. So 25 chunks
   at split size 12 gives `[0, 12, 24, 25]` — three cells, the last holding one
   chunk. A split size at least as large as the axis gives `[0, n]`, a single
   cell.

The grid itself is the cartesian product of the per-axis edge lists, in
row-major order (the last axis varies fastest), so it is a partition: cells
never overlap. A cell with no chunk reference in it produces **no manifest at
all**, so a 25-chunk array split every 3 chunks with one reference written
yields one manifest, not nine.

### P6-2 — There are no `and` / `or` constructor functions in Rust

`guide/performance.md` documents `ManifestSplitCondition.and_conditions(...)`,
`ManifestSplitCondition.or_conditions(...)` and the `&` / `|` operators, and
`guide/performance.md` §"Configuring preloading" documents the same four for
`ManifestPreloadCondition`. **All eight are PyO3 wrapper methods and exist only
in the Python package.** In Rust, `And` and `Or` are plain enum variants taking
a `Vec`, and a caller writes the variant directly:

```rust
ManifestSplitCondition::Or(vec![a, b])
ManifestSplitCondition::And(vec![a, b])
```

This is the single most common way to get the Rust surface wrong. See the
"Python spellings" table below for the rest.

### P6-3 — The Rust spelling of "commit with rebase" is `commit_rebasing`

`guide/version-control.md` §"Conflict Resolution" documents
`Session.commit(message, rebase_with=solver)`. **There is no `rebase_with`
identifier anywhere in the Rust crate**, and there is no `Repository::commit`.
The Rust surface is
`Session::commit_rebasing(solver, rebase_attempts, message, properties,
before_rebase, after_rebase)`; the two callbacks are plain closures and a
caller that does not want them passes `|_| async {}`.

It commits, and on `SessionErrorKind::Conflict` rebases and retries, up to
`rebase_attempts` times, then makes one final commit attempt. So the callbacks
run exactly `rebase_attempts` times when every attempt loses, and the operation
ends with the `Conflict` error from that final attempt.

Also renamed at this release: `Session::merge` now takes another **`Session`**,
not a `ChangeSet`.

### P6-4 — The preload default has an undocumented reference cap, and its
*effect* is out of scope

`guide/performance.md` §"Default preload configuration" says the default
preloads manifests whose array name matches the cf-xarray coordinate-axis
regexes, and that the default `max_total_refs` is `10_000`. It does **not**
say that the default condition is additionally AND-ed with
`NumRefs { from: Unbounded, to: Included(1000) }`. It is: a manifest holding
more than 1000 chunk references is never preloaded by the default policy, no
matter what it is called.

Preloading *itself* — whether a manifest was actually fetched — is **not scored
by this milestone**. The only public instrument for observing it,
`icechunk::storage::logging::LoggingStorage`, has its `new` and
`fetch_operations` methods behind `#[cfg(test)]`, so no code outside the crate
can construct one; and preloading is spawned as a background task, so
observing it would be a race. What is scored is the predicate
(`ManifestPreloadCondition::matches`) and the configuration accessors.

### P6-5 — "Format frozen as 1.0" is a policy, not a byte change

`guide/faq.md` states the backwards-compatibility policy and the changelog
calls this release on-disk-compatible with the whole 0.2.x series. **No
on-disk byte changed.** The spec version written into byte 36 of every metadata
object is still `1`, exactly as at the previous milestone, and the magic bytes,
the metadata keys and the compression byte are unchanged. A candidate must not
bump the spec-version byte, and nothing in this milestone asserts a bump.

Byte 12–35 of the header does change, because it interpolates the crate
version — see P6-8.

### P6-6 — `design/005-manifest-split.md` was never implemented

That design document is byte-identical to the copy shipped one release earlier.
It describes a *different* design: named "manifest sets", `max-manifest-size`,
`arrays-per-manifest`, `overflow-to`, `cardinality`, bin-packing, and rules
keyed on `metadata-chunks` ranges. **None of those identifiers exist in the
released code**, and none of its numbers (`10000`, `50000`, `1_000_000`,
`[0, 500]`, `[0, 5000]`) is the default of anything. What shipped is the
dimension-grid design of P6-1, documented in `guide/performance.md`.

Use `design/005-manifest-split.md` only as motivation — why splitting exists at
all. Never as a source of behaviour. Nothing in this milestone is scored
against it.

### P6-7 — There are no default virtual chunk containers any more

`guide/migration.md` §"Virtual Datasets" is correct and is the normative
statement: every virtual chunk container must now be declared explicitly, and
every one must be authorised explicitly by url prefix when the repository is
opened or created. Concretely:

* `RepositoryConfig::default().virtual_chunk_containers()` yields nothing, and
  `get_virtual_chunk_container` returns `None` for every prefix. Virtual chunk
  resolution that used to work "for free" on the built-in `s3`, `gcs`, `az`,
  `tigris` and `file` containers now fails.
* The `virtual_chunk_containers` map is keyed by **url prefix**, not by a
  container name. A container's `name` survives only so that configurations
  written by older releases still deserialise.
* The repository constructors' third argument is now
  `authorize_virtual_chunk_access: HashMap<String, Option<Credentials>>`, keyed
  by url prefix. A key that is present with value `None` means "authorised, use
  the natural credential for this store"; a prefix that is absent from the map
  is refused, before any I/O, with
  `VirtualReferenceErrorKind::UnauthorizedVirtualChunkContainer`. A url that no
  declared container claims is a different error,
  `VirtualReferenceErrorKind::NoContainerForUrl`.

### P6-8 — The tag is the Python version; the Rust crate is `0.3.0`

This milestone corresponds to the upstream tag `v1.0.0`, which is the version
of the **Python** package. The Rust crate `icechunk` at that tag is version
`0.3.0`, and `ICECHUNK_CLIENT_NAME` — the string written into bytes 12–35 of
every metadata object — is `"ic-"` + the crate version, so it reads
`ic-0.3.0`, not `ic-1.0.0`. Nothing in this milestone asserts the exact version
digits; what is asserted is that the field is the `"ic-"`-prefixed,
space-padded 24-byte field the format requires.

### P6-9 — HTTP virtual chunks are out of scope

`guide/virtual.md` lists HTTP(S) among the supported virtual-chunk backends,
and `ObjectStoreConfig::Http` and `VirtualChunkContainer::new("https://host",
...)` do exist. Exercising that path needs a reachable HTTP server, and this
task's verifiers never use the network. Upstream has no loopback HTTP server
anywhere in its Rust tests either — every `localhost` in them is the MinIO S3
endpoint. Virtual chunks are therefore scored over `file://` containers only.
The `Http` variant must still exist and `VirtualChunkContainer::new` must still
validate an `http(s)://` prefix against it; nothing fetches over it.

### P6-10 — The corpus carries two generations of the guide

Each milestone's corpus is copied **on top of** the previous one, and between
milestones 05 and 06 upstream moved its documentation out of
`icechunk-python/docs/` and renamed every file. So `guide/` holds **33** files
here, not the 20 this milestone copied: the other thirteen are named
`icechunk-python-*.md` and are the milestone 01–05 copies, left under names
nothing overwrites. `README.md` lists all thirteen.

**They are superseded in full and nothing in them is normative at this
milestone.** Where such a file and its unprefixed successor disagree, the
unprefixed file wins — and the successor is always the same name minus the
`icechunk-python-` prefix. The single exception is
`icechunk-python-developing.md`, a contributor guide the project dropped, which
has no successor and states nothing this milestone requires.

`SHA256SUMS` checksums only the files this milestone itself copied, so the
thirteen are not listed in it. That is deliberate, not an omission.

---

## Python spellings with no Rust equivalent

Every row is documented in this corpus, and none of them is a Rust identifier.
None is scored.

| Documented (Python) | Rust |
| --- | --- |
| `ManifestSplitCondition.and_conditions` / `.or_conditions` / `&` / `|` | build the `And(Vec<_>)` / `Or(Vec<_>)` variant |
| `ManifestPreloadCondition.and_conditions` / `.or_conditions` / `&` / `|` | build the `And(Vec<_>)` / `Or(Vec<_>)` variant |
| `ManifestSplittingConfig.from_dict` | construct `ManifestSplittingConfig { split_sizes: Some(vec![...]) }`, or `ManifestSplittingConfig::with_size(n)` |
| `ManifestPreloadCondition.num_refs(a, b)` | `NumRefs { from: Bound<u32>, to: Bound<u32> }` |
| `Session.commit(message, rebase_with=solver)` | `Session::commit_rebasing(...)` — P6-3 |
| `Repository.rewrite_manifests(message, branch=...)` | `icechunk::ops::manifests::rewrite_manifests(&repo, branch, message, properties)`, or `Session::rewrite_manifests(message, properties)` |
| `StorageSettings`, `StorageRetriesSettings` | `icechunk::storage::Settings`, `icechunk::storage::RetriesSettings` |
| `icechunk.set_logs_filter(directive)` | `icechunk::initialize_tracing(Option<&str>)`, behind the `logs` feature |
| `Repository.transaction(...)` context manager | no equivalent; take a session and commit it |
| `Session.fork` / `ForkSession` | no equivalent |
| `s3_refreshable_credentials(..., scatter_initial_credentials=True)` | no equivalent |
| `icechunk.http_store(options={})` | `ObjectStoreConfig::Http(HashMap<String, String>)` — see P6-9 |
