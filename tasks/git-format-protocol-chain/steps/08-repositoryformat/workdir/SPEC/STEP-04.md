# Milestone 4 — index versions 3 and 4, sparse checkout, and the sparse index

`/app` holds a git implementation in PHP with the object database, the files ref
backend, **index version 2**, the porcelain, packfiles, the v0/v1 and v2 wire
protocols, filters and partial clone, and bundles.

This milestone is about the **index**: the two newer on-disk versions, the
extensions that ride in it, and the sparse-checkout machinery that uses them.

Everything in `SPEC/STEP-01.md`, `SPEC/STEP-02.md` and `SPEC/STEP-03.md` still
holds.

## 1. Index versions 3 and 4 (`gitformat-index(5)`)

* **Version 3** adds the extended flag word: a second 16-bit field, present only
  when bit 0x4000 of the first is set, carrying `skip-worktree` (0x4000) and
  `intent-to-add` (0x2000). Version 3 is written **only when an entry needs it**
  and the index drops back to version 2 when none does.
* **Version 4** removes the padding and prefix-compresses the path: each entry's
  name is stored as a varint saying how many bytes to strip from the *previous*
  entry's path, then the NUL-terminated remainder. Entry records are no longer
  padded to a multiple of eight.
* `index.version` in the configuration and `GIT_INDEX_VERSION` in the
  environment choose the version a rewrite produces; `git update-index
  --index-version=<n>` converts an existing index in place.
* An index real git wrote at version 4 must be read unchanged, and an index your
  git writes at version 4 must be read by real git unchanged — including the
  `TREE` cache-tree extension and the `REUC` resolve-undo extension when they
  are present.

## 2. Sparse checkout (`git-sparse-checkout(1)`, `gitignore(5)` patterns)

* `core.sparseCheckout` turns it on; `$GIT_DIR/info/sparse-checkout` holds the
  patterns; the **skip-worktree** bit is what keeps a path out of the work tree.
* `git sparse-checkout init`, `list`, `set`, `add`, `reapply` and `disable`,
  with `--cone` / `--no-cone` and `--stdin`.
* In **cone mode** the file holds the generated pattern form — `/*`, `!/*/`, and
  a `/<dir>/` line per directory, with `/<parent>/*` + `!/<parent>/*/` for each
  ancestor — and `list` prints the directories back, not the patterns.
* Applying a specification adds and removes the skip-worktree bit *and* the
  files: a path that leaves the cone is deleted from the work tree, one that
  enters it is written out. `git status` must stay clean across both.
* `git ls-files -t` shows `S` for a skip-worktree entry and `H` for a cached
  one; `git ls-files --sparse` shows a sparse directory as `<dir>/`.

## 3. The sparse index

* `git sparse-checkout init --cone --sparse-index` turns it on by setting
  `index.sparse = true` in the **worktree** configuration
  (`$GIT_DIR/config.worktree`, which the repository config enables with
  `extensions.worktreeConfig = true`). `core.repositoryformatversion` stays `0`
  and no `extensions.sparseIndex` key is written.
* With it on the index stores a directory that is entirely outside the cone as
  **one entry** — a *sparse directory entry*: mode `040000`, the tree's object
  name, the path with a trailing slash, and the skip-worktree bit.
* Anything that needs the real paths expands the index first, and
  `git sparse-checkout disable` expands it permanently.
* `git ls-files --sparse` lists the collapsed form; without it the index is
  expanded and every path is listed.

## 4. The untracked cache and `fsmonitor`

* `git update-index --untracked-cache` / `--no-untracked-cache` /
  `--test-untracked-cache`, and `core.untrackedCache`, maintain the `UNTR`
  index extension; `git status` must produce the same answer with the cache on
  as with it off.
* `core.fsmonitor` names a hook; `git update-index --fsmonitor` maintains the
  `FSMN` extension. A repository whose hook is absent or fails must behave
  exactly as one with no fsmonitor at all.

## 5. Interoperability

Both directions, against `/opt/gitpeer/{v2.20.1,v2.39.5,v2.51.0}`:

* an index your git writes at versions 2, 3 and 4 is read by all three peers,
  which then see the same `ls-files -s`, `status` and `diff` as your git does;
* an index a peer wrote at each version — including one with a sparse directory
  entry and one with an untracked cache — is read by your git unchanged;
* a sparse checkout your git sets up is a sparse checkout to real git, and the
  reverse.

## 6. Checking your work
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

As in the previous milestones: git's own suite at `/opt/git-suite` under
`GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1`, output (stdout,
stderr and exit status) that matches git's, and the resource ceilings of
`SPEC/STEP-02.md` §5. `t1600-index.sh`, `t1091-sparse-checkout-builtin.sh`,
`t7002-mv-sparse-checkout.sh`, `t3705-add-sparse-checkout.sh`,
`t7519-status-fsmonitor.sh` and `t7063-status-untracked-cache.sh` are the files
this milestone is about.

## 7. What this milestone does *not* include

* the split index (`GIT_TEST_SPLIT_INDEX`, `extensions.splitIndex`);
* `core.untrackedCache` reaching across a `git gc`;
* commit-graph, multi-pack-index and reachability bitmaps;
* the reftable backend and the SHA-256 object format.
