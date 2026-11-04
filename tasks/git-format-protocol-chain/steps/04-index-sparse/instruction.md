# Step 4 — index versions 3 and 4, sparse checkout, and the sparse index

## 1. Implement this milestone

`/app` holds your git implementation in PHP: objects, refs, **index version 2**,
the porcelain, packfiles, the v0/v1 and v2 wire protocols, object filters and
partial clone, and bundles.

Make the index do the rest of its job. Read and write **index version 3** (the
extended flag word that carries `skip-worktree` and `intent-to-add`) and
**version 4** (prefix-compressed paths, no padding), choose the version from
`index.version`, `GIT_INDEX_VERSION` and `git update-index --index-version`,
and keep every extension in the file intact. Then build what uses those bits:
**sparse checkout** in both pattern and cone mode, with the work tree and the
skip-worktree bits kept in step; the **sparse index**, where a directory outside
the cone is one `040000` entry; and the **untracked cache** and **`fsmonitor`**
index extensions.

Real git must read every index you write and you must read every index it
writes — at all three versions, with the extensions in place.

The exact formats, commands and required behaviours are in
`/app/SPEC/STEP-04.md`. Read it first.

`git --exec-path` prints `/app/out/libexec/git-core`, the directory that holds
the `git-<command>` programs, `git-upload-pack` among them.

## 2. Read these local references

**Normative semantics:** git's own documentation at `/opt/git-suite/Documentation/`
— `gitformat-index.adoc` (the whole milestone: versions 2/3/4, `TREE`, `REUC`,
`UNTR`, `FSMN`, the sparse directory entry), `git-sparse-checkout.adoc`,
`git-update-index.adoc`, `git-ls-files.adoc`, `git-status.adoc`,
`gitignore.adoc` (the pattern grammar cone mode restricts),
`git-read-tree.adoc` (the sparse-checkout section) and
`technical/racy-git.adoc`.

**Additional files:** `/app/SPEC/STEP-04.md`, and `SPEC/STEP-01.md`,
`SPEC/STEP-02.md`, `SPEC/STEP-03.md` — all still in force.
`/app/WORKSPACE_HANDOFF.md` is the handoff you maintain. Real git builds are at
`/opt/gitpeer/v2.20.1`, `/opt/gitpeer/v2.39.5` and `/opt/gitpeer/v2.51.0`;
git's own unmodified test suite is at `/opt/git-suite`, and
`/app/SPEC/STEP-04.md` §6 names the files this milestone is about.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents.
  Everything that participates in the build must be PHP source, a `Makefile`,
  a shell wrapper or data: no compiled binaries and no implementation sources in
  another language anywhere in `/app`. `make` must work from
  clean, with `/app/out` deleted beforehand.
- Workspace hygiene: `/app` must survive being copied as a plain tar over a text
  channel, so keep it text-only — experiment in `/tmp`, and do not leave a `.git` directory,
  packfiles, index files or other binary files behind in the workspace.
- Do not cheat by using online solutions or hints specific to this task. Your `git` must do the work itself: `/app/out/bin/git`
  must be a script that runs PHP, and your implementation must never execute
  another git or any of git's own programs (its test helper `test-tool` included).
- `/app` holds no compiled binaries and no source file in another programming
  language (shell scripts and the Makefile aside), test helpers included; and no
  PHP, shell or Makefile source names the real git installations —
  `/opt/gitpeer`, `/opt/git-suite`, `/usr/bin/git` or `/usr/local/bin/git` —
  or git's test helper `test-tool`, not even in a comment. Both are required.
- Run the local checks named in `/app/SPEC/STEP-04.md` §6 and leave a
  reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.
