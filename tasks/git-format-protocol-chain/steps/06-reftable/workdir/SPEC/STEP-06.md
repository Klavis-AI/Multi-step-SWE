# Milestone 6 — the reftable ref backend, and migration from files

`/app` holds a git implementation in PHP with the object database, refs (the
`files` backend), index v2/v3/v4, the porcelain, packfiles, all wire-protocol
versions, partial clone, bundles, sparse checkout, commit-graph, multi-pack-index
and reachability bitmaps. This milestone adds the **reftable** ref backend and
the ability to migrate a repository between `files` and `reftable`.

Everything in `SPEC/STEP-01.md` … `SPEC/STEP-05.md` still holds.

## 1. The reftable format (`technical/reftable.adoc`)

* A reftable stack lives under `$GIT_DIR/reftable/`: one or more
  `*.ref` table files listed in `reftable/tables.list`. Each table is the
  reftable binary format — the `'REFT'` magic, the 24-byte header
  (version 1, block size, min/max update index, hash id), blocks of type
  `'r'` (ref), `'o'` (obj), `'i'` (index), `'g'` (log), each with the block
  header, restart table and padding the spec describes, and the footer with the
  CRC32.
* Ref records carry the value type (0 deletion, 1 direct oid, 2 direct + peeled,
  3 symref), prefix-compressed refnames, and the varint update index.
* Log records carry the reflog: old/new oid, committer identity, timestamp,
  message, in reverse-chronological block order.

## 2. The backend

* `git init --ref-format=reftable` creates a reftable repository
  (`extensions.refStorage = reftable`, `core.repositoryformatversion = 1`);
  `git init` with no option keeps `files`.
* Every ref operation works identically on both backends: `update-ref`
  (including `--stdin` transactions), `symbolic-ref`, `show-ref`,
  `for-each-ref`, `branch`, `tag`, `pack-refs`, `reflog`, `rev-parse`, and the
  ref updates that `commit`, `merge`, `fetch`, `push` and `clone` perform.
* A reftable write is atomic: a failed transaction leaves `tables.list` and the
  stack unchanged. Compaction (`git pack-refs`, auto-compaction) merges tables
  and updates `tables.list` without changing any ref value.

## 3. Migration

* `git refs migrate --ref-format=reftable` converts a `files` repository to
  reftable, preserving every ref, symref and reflog entry, and rewrites
  `extensions.refStorage`; `--ref-format=files` converts back.
* After migration `show-ref`, `for-each-ref --format=...`, `rev-parse` and
  `reflog` give identical output to before, and the loose/packed refs (or the
  reftable stack) of the source backend are gone.

## 4. Interoperability

Both directions. `/opt/gitpeer/v2.51.0` is the only pinned peer that implements
the reftable backend — it arrived in Git 2.45 — so it is the peer that reads the
stacks your git writes. `/opt/gitpeer/v2.20.1` and `/opt/gitpeer/v2.39.5` predate
it: they serve the `files` backend, and a reftable repository your git writes
must be **refused** by them exactly as git refuses a repository carrying an
unknown extension. That refusal is required as well:

* a reftable stack your git wrote is read by the peers — `show-ref`,
  `for-each-ref`, `rev-parse`, `log`, `clone` and `fsck --strict` all agree with
  your git;
* a reftable stack a peer wrote is read by your git unchanged;
* a repository your git migrated to reftable clones and fetches correctly with a
  peer, and one a peer migrated is read by your git.

## 5. Checking your work

git's own suite at `/opt/git-suite` under
`GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1`, and the resource
ceilings of `SPEC/STEP-02.md` §5. `t0610-reftable-basics.sh`, `t1460-refs-migrate.sh` and
`t0601-reffiles-pack-refs.sh` are the files this milestone is centrally
about.

## 6. What this milestone does *not* include

* the SHA-256 object format and repository-format-version negotiation (out of
  scope for this milestone);
* reftable geometric auto-compaction tuning beyond producing a valid, readable
  stack;
* the `git-refs(1)` verbs beyond `migrate`.
