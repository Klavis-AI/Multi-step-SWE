# Step 3 — wire protocol v2, partial clone, and bundles

## 1. Implement this milestone

`/app` holds your git implementation in PHP: the object database, refs, index
v2, the porcelain, packfiles with delta compression, and the version 0/1 pack
protocol as client and server over local paths, `file://`, `git://` and smart
HTTP.

Add **protocol version 2** — `ls-refs` and `fetch` as `gitprotocol-v2(5)`
defines them, negotiated through `GIT_PROTOCOL`, the `git://` request line and
the `Git-Protocol` HTTP header — and the two things that ride on it: **object
filters and partial clone** (`--filter=blob:none` and friends, promisor
remotes, on-demand fetching of omitted objects), and **bundles**
(`gitformat-bundle(5)` v2 and v3, `git bundle` in all four modes, and the
`bundle-uri` command).

Version 0 and 1 must keep working byte for byte: a peer that does not ask for
v2 gets exactly the advertisement it got before, and `-c protocol.version=0`
still produces the old exchange.

The exact argument lists, section order, capability strings and required
behaviours are in `/app/SPEC/STEP-03.md`. Read it first.

`git --exec-path` prints `/app/out/libexec/git-core`, the directory that holds
the `git-<command>` programs, `git-upload-pack` among them.

## 2. Read these local references

**Normative semantics:** git's own documentation at `/opt/git-suite/Documentation/`
— `gitprotocol-v2.adoc` (the whole milestone), `gitprotocol-common.adoc`,
`gitprotocol-http.adoc`, `gitformat-bundle.adoc`, `rev-list-options.adoc` (the
`--filter=` grammar), `partial-clone.adoc` and `bundle-uri.adoc` under
`technical/`, and the manual pages `git-ls-remote.adoc`, `git-clone.adoc`,
`git-fetch.adoc`, `git-rev-list.adoc`, `git-pack-objects.adoc`,
`git-upload-pack.adoc` and `git-bundle.adoc`.

**Additional files:** `/app/SPEC/STEP-03.md` (this milestone), and
`/app/SPEC/STEP-01.md` and `/app/SPEC/STEP-02.md` (the earlier requirements, all
of which still hold). `/app/WORKSPACE_HANDOFF.md` is the
handoff you maintain. Real git builds are at `/opt/gitpeer/v2.20.1`,
`/opt/gitpeer/v2.39.5` and `/opt/gitpeer/v2.51.0`; git's own unmodified test
suite is at `/opt/git-suite`, and `/app/SPEC/STEP-03.md` §8 names the files this
milestone is about.

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
  packfiles, bundles or other binary files behind in the workspace.
- Do not cheat by using online solutions or hints specific to this task. Your `git` must do the work itself: `/app/out/bin/git`
  must be a script that runs PHP, and your implementation must never execute
  another git or any of git's own programs (its test helper `test-tool` included).
- `/app` holds no compiled binaries and no source file in another programming
  language (shell scripts and the Makefile aside), test helpers included; and no
  PHP, shell or Makefile source names the real git installations —
  `/opt/gitpeer`, `/opt/git-suite`, `/usr/bin/git` or `/usr/local/bin/git` —
  or git's test helper `test-tool`, not even in a comment. Both are required.
- Run the local checks named in `/app/SPEC/STEP-03.md` §8 and leave a
  reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.
