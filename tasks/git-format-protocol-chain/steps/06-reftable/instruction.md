# Step 6 — the reftable ref backend, and migration from files

## 1. Implement this milestone

`/app` holds your git implementation in PHP: objects, refs (the `files`
backend), index v2/v3/v4, the porcelain, packfiles, all wire-protocol versions,
partial clone, bundles, sparse checkout, commit-graph, multi-pack-index and
reachability bitmaps.

Add the **reftable** ref backend (`technical/reftable.adoc`): the binary table
stack under `$GIT_DIR/reftable/`, atomic transactional writes, compaction, and
every ref operation working identically on it and on `files`. Add
`git init --ref-format=reftable` and `git refs migrate` in both directions.
Real git v2.51.0 -- the only pinned build new enough to implement the backend,
which arrived in Git 2.45 -- must read a reftable stack you wrote, and you must
read one it wrote. The older builds must **refuse** a reftable repository the way
git refuses a repository carrying an unknown extension, and `git fsck --strict`
stays clean.

The format, block types, commands and required behaviours are in
`/app/SPEC/STEP-06.md`. Read it first.

## 2. Read these local references

**Normative semantics:** git's own documentation at `/opt/git-suite/Documentation/`
— `git-refs.adoc`, `git-init.adoc`,
`git-update-ref.adoc`, `git-pack-refs.adoc`, `git-for-each-ref.adoc`,
`git-show-ref.adoc`, `gitrepository-layout.adoc`, and
`technical/reftable.adoc`.

**Additional files:** `/app/SPEC/STEP-06.md`, the earlier
`SPEC/STEP-0{1..5}.md` (all still in force), and
`/app/WORKSPACE_HANDOFF.md`. Real git builds are at `/opt/gitpeer/v2.20.1`
(files only), `/opt/gitpeer/v2.39.5` (files only -- it predates reftable) and
`/opt/gitpeer/v2.51.0`; git's own suite
is at `/opt/git-suite`, and `/app/SPEC/STEP-06.md` §5 names the files this
milestone is about.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents.
  Everything that participates in the build must be PHP source, a `Makefile`, a
  shell wrapper or data: no compiled binaries and no implementation sources in
  another language anywhere in `/app`. `make` must work from
  clean, with `/app/out` deleted beforehand.
- Workspace hygiene: `/app` must survive being copied as a plain tar over a text
  channel, so keep it text-only — experiment in `/tmp`, and leave no `.git`, reftable
  stacks, packfiles or other binary files behind in the workspace.
- Do not cheat by using online solutions or hints specific to this task. Your `git` must do the work itself: `/app/out/bin/git`
  must be a script that runs PHP, and your implementation must never execute
  another git or any of git's own programs (its test helper `test-tool` included).
- `/app` holds no compiled binaries and no source file in another programming
  language (shell scripts and the Makefile aside), test helpers included; and no
  PHP, shell or Makefile source names the real git installations —
  `/opt/gitpeer`, `/opt/git-suite`, `/usr/bin/git` or `/usr/local/bin/git` —
  or git's test helper `test-tool`, not even in a comment. Both are required.
- Run the local checks named in `/app/SPEC/STEP-06.md` §5 and leave a
  reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.
