# Milestone 8 — repository format version 1, extensions, and refusal semantics

`/app` holds a git implementation in PHP with everything from the earlier
milestones: SHA-1 and SHA-256 object formats, the files and reftable ref
backends, packfiles, all wire protocols, the acceleration structures, index
v2/v3/v4 and sparse checkout. This milestone makes the **repository format
version** the gate that ties them together (`gitrepository-layout(5)`,
`repository-version.adoc`).

Everything in `SPEC/STEP-01.md` … `SPEC/STEP-07.md` still holds.

## 1. `core.repositoryformatversion`

* Version **0** is the default; version **1** is required whenever an
  `extensions.*` key is set. `git init` writes 0 unless an option forces 1
  (`--object-format=sha256`, `--ref-format=reftable`, a partial clone).
* Reading a repository, your git parses `core.repositoryformatversion` and the
  `[extensions]` section and applies the rules below **before** any other
  operation touches the object or ref store.

## 2. The extensions and their gate

git sorts `extensions.*` into two classes, and the format version decides what
happens to each. (Match git's exact sets and wording; this is the milestone's
core.)

* **v1-only extensions** — `compatObjectFormat`, `noop-v1`, `objectFormat`, `refStorage`, `relativeWorktrees`. They are
  honoured at version 1. At version **0** each one is fatal: `repo version is 0,
  but v1-only extension found:` followed by a tab-indented line naming the
  offending extension (lowercased).
* **Extensions defined for version 0 as well** — `noop`, `partialClone`, `preciousObjects`, `worktreeConfig`.
  Honoured at both versions.
* An **unknown** extension is **ignored at version 0** — the repository opens
  normally and the command exits 0 — and is a **fatal refusal at version 1**:
  `unknown repository extension found:` — with the header pluralised to
  `unknown repository extensions found:` when more than one is unknown —
  followed by one tab-indented line naming each.
* A repository version greater than 1 is refused outright: `Expected git repo
  version <= 1, found <n>`.

## 3. Transitions

* `git config core.repositoryformatversion 1` plus setting an extension, and the
  reverse, behave as git does; setting `objectFormat`/`refStorage` is done by
  the milestone-7/6 commands, and this milestone requires the *gate* around
  them to be exactly git's.

## 4. What the gate must match

This milestone is about the gate itself, not about new peer traffic: for every
(version, extension) combination, your git's stdout, stderr and exit status must
match the pinned reference git's, and repositories that real git built must
still open exactly as before.
Every earlier milestone's repository — format 0, and the format-1 ones from
partial clone, reftable and SHA-256 — must keep working.

## 5. Checking your work

git's own suite at `/opt/git-suite` under
`GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1`, and the resource
ceilings of `SPEC/STEP-02.md` §5. `t1302-repo-version.sh` is the file this
milestone is centrally about, plus the `extensions.*` handling exercised across
the object-format, ref-format and partial-clone tests.

## 6. What this milestone does *not* include

* new object/ref formats (they are milestones 6-7); this milestone is only the
  version/extension gate and its refusal semantics.
