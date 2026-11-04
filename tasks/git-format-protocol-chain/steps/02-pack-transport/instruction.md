# Step 2 — packfiles v2 and the v0/v1 wire protocol

## 1. Implement this milestone

`/app` holds a git implementation written in PHP: the object database with
loose objects, the files ref backend, index v2 and the porcelain core. It
cannot pack objects and it cannot talk to another repository.

Add both. Give it **packfiles**: pack version 2 with delta compression
(`OBJ_OFS_DELTA` and `OBJ_REF_DELTA`), `.idx` version 2, `gc`, `repack`,
`pack-objects`, `index-pack`, `unpack-objects`, `verify-pack`, `prune` — so that
after a `git gc` the loose objects are gone and everything the repository could
do before still works. And give it the **pack protocol**: `upload-pack` and
`receive-pack` on one side, `clone`, `fetch`, `push` and `ls-remote` on the
other, over local paths and `file://`, over `git://` with your own `git daemon`,
and over smart HTTP with your own `git http-backend` CGI — at protocol version
0 and 1.

The bar is interoperability with real git, both ways: real git must clone,
fetch and push against your repositories and your servers and find them
`fsck --strict` clean, and your git must do the same against repositories and
servers built by real git, including packs it deltified.

The exact formats, capability sets, output shapes and required behaviours
are in `/app/SPEC/STEP-02.md`. Read it first.

## 2. Read these local references

**Normative semantics:** git's own documentation at `/opt/git-suite/Documentation/`
— `gitformat-pack.adoc` (pack and index format, delta encoding),
`gitprotocol-pack.adoc`, `gitprotocol-common.adoc`,
`gitprotocol-capabilities.adoc`, `gitprotocol-http.adoc`, and the manual pages
`git-gc.adoc`, `git-repack.adoc`, `git-pack-objects.adoc`, `git-index-pack.adoc`,
`git-unpack-objects.adoc`, `git-verify-pack.adoc`, `git-prune.adoc`,
`git-count-objects.adoc`, `git-upload-pack.adoc`, `git-receive-pack.adoc`,
`git-clone.adoc`, `git-fetch.adoc`, `git-push.adoc`, `git-ls-remote.adoc`,
`git-daemon.adoc` and `git-http-backend.adoc`.

**Additional files:** `/app/SPEC/STEP-02.md` (this milestone's requirement list
and how to check your work), `/app/SPEC/STEP-01.md` (the earlier requirements, all of
which still hold) and `/app/WORKSPACE_HANDOFF.md` (the handoff you maintain).
Real git builds to compare against are at `/opt/gitpeer/v2.20.1`,
`/opt/gitpeer/v2.39.5` and `/opt/gitpeer/v2.51.0`; git's own unmodified test
suite is at `/opt/git-suite` and `/app/SPEC/STEP-02.md` §5 shows the invocation.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents.
  Everything that participates in the build must be PHP source, a `Makefile`,
  a shell wrapper or data: no compiled binaries and no implementation sources in
  another language anywhere in `/app`. `make` must work from clean, with
  `/app/out` deleted beforehand, so anything your build does incrementally must
  also be correct from scratch.
- Workspace hygiene: `/app` must survive being copied as a plain tar over a text
  channel, so keep it text-only — put the repositories you experiment on in `/tmp` rather
  than inside `/app`, and do not leave a `.git` directory, packfiles or other
  binary files behind in the workspace.
- Do not cheat by using online solutions or hints specific to this task. Your `git` must do the work itself: `/app/out/bin/git`
  must be a script that runs PHP, and your implementation must never execute
  another git or any of git's own programs (its test helper `test-tool` included).
- `/app` holds no compiled binaries and no source file in another programming
  language (shell scripts and the Makefile aside), test helpers included; and no
  PHP, shell or Makefile source names the real git installations —
  `/opt/gitpeer`, `/opt/git-suite`, `/usr/bin/git` or `/usr/local/bin/git` —
  or git's test helper `test-tool`, not even in a comment. Both are required.
- Run the local checks named in `/app/SPEC/STEP-02.md` §5 and leave a
  reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.
