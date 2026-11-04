# Step 1 — a git implementation in PHP: objects, refs, index v2, porcelain core

## 1. Implement this milestone

`/app` holds this ticket's reference documents — `SPEC/STEP-01.md`,
`WORKSPACE_HANDOFF.md` and `timer.sh` — and no implementation. Build **git**
there, in **PHP**: the object database, the files ref backend, the version 2
index, and the porcelain that drives them, so that the result is a git
implementation real git can interoperate with in both directions.

Concretely: running `make` with `/app` as the working directory must produce an
executable **`/app/out/bin/git`** that implements git's documented command line
over git's documented on-disk formats — SHA-1 object names, zlib-compressed
loose objects, `refs/`, `packed-refs` and reflogs, a `DIRC` version 2 index, and
`core.repositoryformatversion = 0` repositories. Repositories your binary writes
must satisfy `git fsck --strict` and be clonable and readable by real git;
repositories real git writes must be readable by your binary unchanged.

The exact command surface, the formats, and the required behaviours are
listed in `/app/SPEC/STEP-01.md`. Read it first.

## 2. Read these local references

**Normative semantics:** git's own documentation, installed at
`/opt/git-suite/Documentation/` — `git.adoc`, the `git-<command>.adoc` manual
pages for every command named in the spec, `gitrepository-layout.adoc`,
`gitformat-index.adoc`, `gitformat-pack.adoc` (object storage),
`gitrevisions.adoc`, `gitignore.adoc`, `gitattributes.adoc`, `gitglossary.adoc`
and `technical/racy-git.adoc`. These documents define what "correct" means here;
nothing else does.

**Additional files:** `/app/SPEC/STEP-01.md` (this milestone's requirement list
and how to check your work) and `/app/WORKSPACE_HANDOFF.md` (the handoff you maintain
for the next session). Real git builds you can compare against are at
`/opt/gitpeer/v2.20.1`, `/opt/gitpeer/v2.39.5` and `/opt/gitpeer/v2.51.0`, and
git's own unmodified test suite is at `/opt/git-suite` — `/app/SPEC/STEP-01.md`
§6 shows the invocation that runs a test file against your binary.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents.
  Everything that participates in the build must be PHP source, a `Makefile`,
  a shell wrapper or data: no compiled binaries and no implementation sources in
  another language anywhere in `/app`.
- Workspace hygiene: `/app` must survive being copied as a plain tar over a text
  channel, so keep it text-only — put the repositories you experiment on in `/tmp` rather
  than inside `/app`, and do not leave a `.git` directory, packfiles or other
  binary files behind in the workspace. Anything binary under `/app` may be lost
  in transit, taking your work with it.
- Do not cheat by using online solutions or hints specific to this task. Your `git` must do the work itself: `/app/out/bin/git`
  must be a script that runs PHP, `/app` must contain no compiled binaries and
  no implementation sources in another language, and your implementation must
  never execute another git or any of git's own programs (its test helper `test-tool` included).
- `/app` holds no compiled binaries and no source file in another programming
  language (shell scripts and the Makefile aside), test helpers included; and no
  PHP, shell or Makefile source names the real git installations —
  `/opt/gitpeer`, `/opt/git-suite`, `/usr/bin/git` or `/usr/local/bin/git` —
  or git's test helper `test-tool`, not even in a comment. Both are required.
- Run the local checks named in `/app/SPEC/STEP-01.md` §6 and leave a
  reproducible handoff in `/app/WORKSPACE_HANDOFF.md` for the next fresh session.
