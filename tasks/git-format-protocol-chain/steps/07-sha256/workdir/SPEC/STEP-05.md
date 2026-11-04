# Milestone 5 — commit-graph, multi-pack-index, and reachability bitmaps

`/app` holds a git implementation in PHP with the object database, refs, index
v2/v3/v4, the porcelain, packfiles, the v0/v1/v2 wire protocols, partial clone,
bundles and sparse checkout. This milestone adds the three on-disk acceleration
structures git keeps beside the object store, and the guarantee that they only
ever speed answers up — never change them.

Everything in `SPEC/STEP-01.md` … `SPEC/STEP-04.md` still holds.

## 1. The commit-graph (`gitformat-commit-graph(5)`)

* `git commit-graph write --reachable` writes `objects/info/commit-graph` — the
  `CGPH` file with its `OIDF`, `OIDL`, `CDAT`, and (when needed) `EDGE` chunks.
* `git commit-graph verify` checks it and exits non-zero on damage, reporting
  the mismatch git reports.
* **Generation numbers**: the graph records the topological level and the
  committer-date generation. `git merge-base`, `git rev-list --topo-order` and
  `git rev-list --count` give **identical** output whether or not the graph is
  present — the graph is consulted, never trusted over the objects.
* `core.commitGraph` (default true) turns reading on; `commitGraph.generationVersion`
  and `fetch.writeCommitGraph` behave as documented.

## 2. The multi-pack-index (`gitformat-pack(5)`, the MIDX section)

* `git multi-pack-index write` (with `--bitmap`, `--preferred-pack`) writes
  `objects/pack/multi-pack-index` — the `MIDX` file with `PNAM`, `OIDF`, `OIDL`,
  `OOFF` and (for >2^32) `LOFF` chunks — indexing every pack at once.
* `git multi-pack-index verify`, `expire` and `repack` behave as documented.
* With a midx present, object lookup, `cat-file`, `rev-list --objects`, `fsck`
  and `repack` give the same answers as without it; `git gc` maintains it when
  `core.multiPackIndex` is set.

## 3. Reachability bitmaps (`gitformat-pack(5)`, the bitmap section)

* `git repack -a -d --write-bitmap-index` writes an `EWAH` bitmap beside the
  pack (`.bitmap`, with the `.rev` reverse index): the four type bitmaps plus
  per-commit reachability bitmaps, in the bit order `gitformat-pack(5)` gives,
  so that real git can use it.
* `git multi-pack-index write --bitmap` must succeed and leave a
  multi-pack-index real git verifies, and bitmapped answers must still equal
  walked ones afterwards. A multi-pack bitmap file (`multi-pack-index-*.bitmap`)
  is not required at this milestone.
* `git rev-list --use-bitmap-index --count` equals the walked answer exactly,
  and `git rev-list --test-bitmap` verifies the bitmaps.
* A bitmap that disagrees with the objects is a bug: bitmapped and walked
  answers must be identical on every repository, and `--test-bitmap` must
  pass.

## 4. Interoperability

Against `/opt/gitpeer/{v2.20.1,v2.39.5,v2.51.0}`:

* a commit-graph and multi-pack-index your git wrote are read by the peers and
  pass their `verify`, and a pack bitmap your git wrote passes their
  `rev-list --test-bitmap`;
* every commit-graph/midx/bitmap file is advisory: deleting it changes no
  observable answer, only speed (every command gives the same output with and
  without the files).

## 5. Checking your work
**Repository extensions you must tolerate.** git's own `t/` suite is
unmodified, and `t1006-cat-file.sh` sets up its main repository with three config
writes together -- `core.repositoryformatversion 1`, `extensions.objectFormat`
and `extensions.compatObjectFormat`. Opening such a repository must succeed:
accept both keys as known extensions, AND accept format version 1 on a repository
that carries them. (Repositories *you* create still default to version 0; this is
about not refusing one git's own suite sets up.) You are NOT asked to implement
the SHA-256 object format or the compat mapping here: a repository whose
`extensions.objectFormat` is actually `sha256` is out of scope for this
milestone, and refusing that one is fine. What must not be refused is the version-1 repository carrying
these keys that the suite sets up around SHA-1 objects. Nearly every assertion in
that file runs inside that repository, so refusing it fails all of them at
once.

git's own suite at `/opt/git-suite` under
`GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1`, stdout/stderr/exit
that match git's, and the resource ceilings of `SPEC/STEP-02.md`
§5. `t5318-commit-graph.sh`, `t5324-split-commit-graph.sh`,
`t5319-multi-pack-index.sh`, `t5310-pack-bitmaps.sh`,
`t5326-multi-pack-bitmaps.sh` and `t5327-multi-pack-bitmaps-rev.sh` are the files this milestone is about.

## 6. What this milestone does *not* include

* the reftable ref backend and the SHA-256 object format (out of scope for this milestone);
* changed-path Bloom filters beyond writing/reading the `BDAT`/`BIDX` chunks
  (`--changed-paths` is written and verified, not required to accelerate `log
  -- <path>`);
* `core.repositoryformatversion` changes — these structures live at format 0.
