# `icechunk-spec/` — the Icechunk documentation corpus

Everything in this directory except `README.md` (this file) and `PROFILE.md` is
copied **verbatim** from the upstream `earth-mover/icechunk` repository.
Upstream's `LICENSE` and a `NOTICE` recording the provenance ship with it.

**There is no implementation here.** These are specifications, guides and design
notes. The Rust surface this milestone must expose is in
`/app/TEST_INTERFACE.md`, and the two documents are meant to be read together.

**This directory accumulates.** Each milestone of the ladder overlays its own
documentation on top of the `/app` tree the previous one left, and the overlay
**merges** rather than replaces: a file the earlier milestone shipped under a
name upstream has since changed survives beside its successor. So the tree you
are reading holds **114 files** — the 79 this milestone ships, plus 35 carried
over from milestones 01 to 09 under older names. The 35 are listed under
"Superseded files" below; none of them is normative here, and `PROFILE.md` says
what replaced each.

`SHA256SUMS` lists a digest for the **76** upstream files this milestone itself
copied (everything except `README.md`, `PROFILE.md` and `SHA256SUMS`). It does
not cover the 35 carried-over files, which earlier milestones' checksum lists
covered when they were written.

114 files:

| path | files | what it is |
| --- | --- | --- |
| `spec.md` | 1 | **the normative specification**, spec version 2.1 |
| `PROFILE.md` | 1 | **task-owned**; resolves what the specification leaves open, and wins where the two conflict |
| `reference/` | 19 | the published reference documentation, including the **changelog** and the earlier spec generations — 18 current, plus a superseded `spec.md` |
| `guide/` | 62 | the prose guides: getting started, how-to, configuration, storage, virtual chunks, versioning, expiration, performance — 29 current, plus 33 superseded |
| `design/` | 19 | upstream's numbered design documents, in the order they were written |
| `flatbuffers/` | 7 | the FlatBuffers schemas for the on-disk format — 6 current and normative, plus a superseded `object_ids.fbs` |
| `archive/` | 1 | the original pre-1.0 specification, kept for history |
| `LICENSE`, `NOTICE`, `SHA256SUMS`, `README.md` | 4 | licence, provenance, digests, this file |

---

## Where to start

Read in this order. The first three are short.

1. **`/app/TEST_INTERFACE.md`** — the public contract: names, signatures,
   argument order, error categories, the command-line grammar and exit statuses,
   and an explicit list of what is *not* scored. Start here; it says which parts
   of the corpus below actually bear on this milestone.
2. **`PROFILE.md`** — the task-owned resolutions. It carries every earlier
   milestone's resolution forward, restated as it holds at **this** release, and
   adds the five this release needs (P21–P25). Where it and a specification
   document disagree, it wins.
3. **`reference/changelog.md`** → `## Python Icechunk Library 2.2.0` — the
   normative statement of what is new in this release: the `inspect_*` header,
   the Hugging Face preset, the three new CLI subcommands, the clock-skew fix,
   the delete no-op, the zero-length rejection, the metadata key rename, the
   S3 endpoint fix and the zero-copy write. Everything else in that file is the
   history of the ten milestones this one inherits.
4. **`spec.md`** — the on-disk format. The sections this milestone leans on are
   `### Binary File Format` (the 39-byte header the new `inspect_*` output
   reports), `#### Repo Info File` (the table of contents), and
   `### Consistency and Optimistic Concurrency` together with
   `### Storage Operations` (the conditional-write contract the Hugging Face
   gateway weakens). `## Changes from spec version 2 to 2.1` is the one-screen
   summary of what the current generation added.

## What is in each directory

### `reference/` — 19 files (18 current + 1 superseded)

| file | why you would open it |
| --- | --- |
| `changelog.md` | what changed at every release, this one included |
| `spec-v2-1.md` | the same document as `spec.md`, under its published name |
| `spec-v2.md`, `spec-v1.md` | the two earlier on-disk generations |
| `spec.md` | **superseded** — the spec-2.0 generation under the name it carried at milestone 09, before it was republished as `spec-v2-1.md`/`spec-v2.md`. `PROFILE.md` P1 resolves it: read `/app/icechunk-spec/spec.md` (the top-level one) instead |
| `icechunk-rust.md` | the Rust crate's own overview |
| `storage.md`, `ops.md`, `session.md`, `config.md`, `credentials.md`, `snapshots.md`, `conflicts.md`, `virtual.md` | per-area reference pages; several are one-line generated stubs |
| `migration.md`, `migration-2.md`, `breaking-changes-2.md` | what moved between major versions |
| `version-policy.md`, `index.md` | the release policy and the index |

### `guide/` — 62 files (29 current + 33 superseded)

`guides-storage.md` carries the **`#### Hugging Face`** section this release
adds — the gateway endpoint, the region, path-style URLs, the bare bucket name
and the lost-response caveat. The other guides you are most likely to want:
`guides-configuration.md`, `guides-virtual.md`, `guides-error-handling.md`,
`understanding-version-control.md`, `understanding-expiration.md`,
`understanding-concepts.md`, `understanding-repository-features.md`,
`guides-performance.md`, `guides-flaky-networks.md`, `guides-async.md`.

Filenames keep upstream's section as a prefix: `getting-started-*`, `guides-*`,
`understanding-*`, plus a few standalone pages (`overview.md`, `index.md`,
`arraylake.md`, `blog-posts.md`, `related-projects.md`, `sample-datasets.md`).

Most guides show the **Python** API. Read them for the behaviour they describe,
not for the names — the Rust names are in `/app/TEST_INTERFACE.md`.

### `design/` — 19 files

Numbered `001`…`018` (there are two `011`s: `011-IC2-gc-and-expiration-consistency.md`
and `011-ref-and-ancestry-entry-point.md`). They are the reasoning behind the
format, not a contract. The ones closest to this milestone:

- `004-binary-format.md` — the 39-byte header and the FlatBuffers framing;
- `007-basic-expiration.md` and `011-IC2-gc-and-expiration-consistency.md` —
  expiration and garbage collection, including the consistency rule the
  clock-skew fix restores;
- `016-expired-transaction-logs.md` — `pruned_ancestor_tx_logs`;
- `017-conditional-put-lost-response.md` — the write-id read-back the Hugging
  Face gateway cannot support;
- `010-notes-towards-an-IC-2.md` and `012-some-more-IC2-format-changes.md` —
  the table of contents;
- `005-manifest-split.md`, `009-tigris-support.md`, `013-feature-flags.md`,
  `018-shift-array-rectilinear-guard.md` — the remaining areas this milestone
  inherits.

### `flatbuffers/` — 7 files (6 current + 1 superseded)

`all.fbs`, `common.fbs`, `snapshot.fbs`, `manifest.fbs`, `transaction_log.fbs`,
`repo.fbs`. These six are normative: `spec.md` quotes them inline, and the
comments inside them are part of the specification.

`object_ids.fbs` is **superseded** and is not one of them. It is the object-id
schema fragment as milestones 04 to 08 shipped it; upstream folded
`ObjectId12` / `ObjectId8` into `common.fbs`, which is where this release's
normative definitions live. It is not quoted by `spec.md` and nothing is scored
on it.

### `archive/` — 1 file

`spec-alpha1.md`, the original specification from the first public release. It
describes JSON snapshots, 16-byte object ids and a `r/ s/ a/ m/ c/` key layout —
**none of which this release implements**. `PROFILE.md` P4–P7 say what replaced
each. Keep it for history; do not implement from it.

---

## Superseded files — 35 of the 114

Upstream reorganised its documentation directory twice while this ladder was
being built: `docs/docs/icechunk-python/*.md` became `docs/docs/*.md` at
`v1.0.0`, and then
`icechunk-python/docs/docs/{getting-started,guides,understanding}/*.md` at
`v2.0.0`. The corpus generator encodes the source directory in the flattened
file name, so the same logical page arrives here under a different name at
different milestones, and the merging overlay keeps every one of them.

**Nothing in this list is normative at this release.** All but two have a
successor in the tables above; where one is not obvious, `PROFILE.md` resolves
it. Read the successor.

`guide/` — 33 superseded files. Thirteen keep the `icechunk-python-` prefix of
the pre-`v1.0.0` layout; strip the prefix to get the middle-generation name, and
then apply the rule below:

```
icechunk-python-concurrency.md    icechunk-python-parallel.md
icechunk-python-configuration.md  icechunk-python-quickstart.md
icechunk-python-dask.md           icechunk-python-storage.md
icechunk-python-developing.md     icechunk-python-version-control.md
icechunk-python-expiration.md     icechunk-python-virtual.md
icechunk-python-faq.md            icechunk-python-xarray.md
icechunk-python-index.md
```

The other twenty are the middle generation, flat and unprefixed:

```
async.md        configuration.md  icechunk-for-git-users.md  performance.md   storage.md
concepts.md     dask.md           icechunk-rust.md           quickstart.md    version-control.md
concurrency.md  expiration.md     migration.md               spec.md          version-policy.md
                faq.md            parallel.md                virtual.md       xarray.md
                howto.md                                                      
```

The rule that maps them to this release's names:

- a page about *doing* something is now `guides-<name>.md` — `storage.md` →
  `guide/guides-storage.md`, and likewise `configuration.md`, `virtual.md`,
  `xarray.md`, `dask.md`, `async.md`, `performance.md`,
  `icechunk-for-git-users.md`;
- a page about *understanding* something is now `understanding-<name>.md` —
  `concepts.md`, `version-control.md`, `expiration.md`, `faq.md`, `parallel.md`;
- `quickstart.md` and `howto.md` are now `getting-started-*.md`;
- `migration.md`, `version-policy.md` and `icechunk-rust.md` moved out of
  `guide/` into `reference/` and are there under the same names;
- `spec.md` under `guide/` is an old copy of the specification: the normative
  one is `/app/icechunk-spec/spec.md` at the top level (`PROFILE.md` P1);
- `concurrency.md` and `icechunk-python-developing.md` are the two with no
  successor at all: the first is a fifteen-line stub of unfilled headings that
  upstream never finished, the second a contributor guide upstream dropped.
  `guide/guides-flaky-networks.md` is **not** a successor to `concurrency.md`:
  it documents storage retry and timeout settings, a different subject. An
  earlier milestone's `README.md` paired the two; this one is the correct
  mapping.

`reference/spec.md` — the spec-**2.0** generation, superseded by the top-level
`spec.md` (spec 2.1) and republished upstream as `reference/spec-v2.md`.
`PROFILE.md` P1 and P2 say what the difference is and why the header byte is
`2` either way.

`flatbuffers/object_ids.fbs` — folded into `common.fbs`, which is the normative
home of `ObjectId12` and `ObjectId8` at this release.

## Provenance

`NOTICE` records the upstream repository, the release and the licence.
`SHA256SUMS` lists a digest for each of the **76** upstream files this milestone
copied, so that part of the corpus can be checked against it; `README.md` and
`PROFILE.md` are task-owned and are not in that list, and neither are the 35
superseded files earlier milestones left behind (see "Superseded files").
