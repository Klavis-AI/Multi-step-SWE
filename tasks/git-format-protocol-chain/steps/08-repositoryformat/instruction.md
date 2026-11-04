# Step 08 — repository format version 1, extensions, and refusal semantics

## 1. Implement this milestone

`/app` holds your git implementation in PHP with everything from the earlier
milestones. Add: the core.repositoryformatversion gate: version 0 vs 1, which extensions are honoured at each version, and the fatal refusal for an unknown extension **at version 1** (at version 0 an unknown extension is ignored) or for a version above 1 -- applied before anything touches the object or ref store.

Repositories that the pinned real gits wrote must be read correctly by your
git, and `git fsck` stays clean. (This milestone covers that
reading direction and git's own suite; it does not require a peer to read what
your git writes.)
Everything the earlier milestones did must keep working unchanged.

The exact formats, commands and required behaviours are in
`/app/SPEC/STEP-08.md`. Read it first.

## 2. Read these local references

**Normative semantics:** git's own documentation at `/opt/git-suite/Documentation/`
— the manual pages and `technical/` documents named in `/app/SPEC/STEP-08.md`.

**Additional files:** `/app/SPEC/STEP-08.md` and the earlier `SPEC/STEP-0*.md`
(all still in force), and `/app/WORKSPACE_HANDOFF.md`. Real git
builds are at `/opt/gitpeer/v2.20.1`, `/opt/gitpeer/v2.39.5` and
`/opt/gitpeer/v2.51.0`; git's own suite is at `/opt/git-suite`, and
`/app/SPEC/STEP-08.md` §5 names the files this milestone is about.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents.
  Everything that participates in the build must be PHP source, a `Makefile`, a
  shell wrapper or data: no compiled binaries and no implementation sources in
  another language anywhere in `/app`. `make` must work from
  clean, with `/app/out` deleted beforehand.
- Workspace hygiene: `/app` must survive being copied as a plain tar over a text
  channel, so keep it text-only — experiment in `/tmp`, and leave no `.git` directory or
  other binary files behind in the workspace.
- Do not cheat by using online solutions or hints specific to this task. Your `git` must do the work itself: `/app/out/bin/git`
  must be a script that runs PHP, and your implementation must never execute
  another git or any of git's own programs (its test helper `test-tool` included).
- `/app` holds no compiled binaries and no source file in another programming
  language (shell scripts and the Makefile aside), test helpers included; and no
  PHP, shell or Makefile source names the real git installations —
  `/opt/gitpeer`, `/opt/git-suite`, `/usr/bin/git` or `/usr/local/bin/git` —
  or git's test helper `test-tool`, not even in a comment. Both are required.
- Run the local checks named in `/app/SPEC/STEP-08.md` §5 and leave a
  reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.
