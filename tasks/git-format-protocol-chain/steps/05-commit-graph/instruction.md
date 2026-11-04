# Step 5 — commit-graph, multi-pack-index, and reachability bitmaps

## 1. Implement this milestone

`/app` holds your git implementation in PHP: objects, refs, index v2/v3/v4, the
porcelain, packfiles, the v0/v1/v2 wire protocols, partial clone, bundles and
sparse checkout.

Add the three acceleration structures git keeps beside the object store, and the
invariant that binds them: they change how fast an answer comes, never the
answer. Write and read the **commit-graph** (`git commit-graph write/verify`,
generation numbers v1 and v2, the split form), the **multi-pack-index**
(`git multi-pack-index write/verify/expire/repack`), and **reachability
bitmaps** (`repack --write-bitmap-index`, `multi-pack-index write --bitmap`,
`rev-list --use-bitmap-index`). Real git must read every structure you write and
you must read every one it writes, and `git fsck --strict` stays clean.

The formats, chunk names, commands and required behaviours are in
`/app/SPEC/STEP-05.md`. Read it first.

## 2. Read these local references

**Normative semantics:** git's own documentation at `/opt/git-suite/Documentation/`
— `gitformat-commit-graph.adoc`, `gitformat-pack.adoc` (the multi-pack-index and
bitmap sections), `git-commit-graph.adoc`, `git-multi-pack-index.adoc`,
`git-repack.adoc`, `git-rev-list.adoc`, and `technical/commit-graph.adoc`,
`technical/multi-pack-index.adoc`, `technical/bitmap-format.adoc`.

**Additional files:** `/app/SPEC/STEP-05.md`, the earlier `SPEC/STEP-0{1,2,3,4}.md`
(all still in force), and `/app/WORKSPACE_HANDOFF.md`. Real git builds
are at `/opt/gitpeer/v2.20.1`, `/opt/gitpeer/v2.39.5` and `/opt/gitpeer/v2.51.0`;
git's own suite is at `/opt/git-suite`, and `/app/SPEC/STEP-05.md` §5 names the
files this milestone is about.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents.
  Everything that participates in the build must be PHP source, a `Makefile`, a
  shell wrapper or data: no compiled binaries and no implementation sources in
  another language anywhere in `/app`. `make` must work from
  clean, with `/app/out` deleted beforehand.
- Workspace hygiene: `/app` must survive being copied as a plain tar over a text
  channel, so keep it text-only — experiment in `/tmp`, and leave no `.git`, packfiles,
  commit-graph, midx or bitmap files behind in the workspace.
- Do not cheat by using online solutions or hints specific to this task. Your `git` must do the work itself: `/app/out/bin/git`
  must be a script that runs PHP, and your implementation must never execute
  another git or any of git's own programs (its test helper `test-tool` included).
- `/app` holds no compiled binaries and no source file in another programming
  language (shell scripts and the Makefile aside), test helpers included; and no
  PHP, shell or Makefile source names the real git installations —
  `/opt/gitpeer`, `/opt/git-suite`, `/usr/bin/git` or `/usr/local/bin/git` —
  or git's test helper `test-tool`, not even in a comment. Both are required.
- Run the local checks named in `/app/SPEC/STEP-05.md` §5 and leave a
  reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.
