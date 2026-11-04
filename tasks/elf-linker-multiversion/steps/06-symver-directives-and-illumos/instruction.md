# Step 06 — Assembler-directed versioning and a second platform

## Implement this milestone

Move versioning out of the command line and into the inputs, and support a
second platform.

This milestone covers `.symver` directives consumed from input objects rather
than only from scripts, in all three spellings; the `VERSION` command inside a
linker script; `--retain-symbols-file` and its exact file syntax; `@filename`
argument files mixed freely with regular arguments, with the quoting,
escaping and recursion rules that go with them; mergeable string sections,
including a reference that lands part way into a merged string; a maximum
supported input alignment of 2^16; `.note.stapsdt` probes preserved, with or
without `--gc-sections`; overflow checking on the 32-bit relocation types;
double-quoted tokens in the script grammar; an entry point permitted on a
shared object, with an absolute symbol accepted as one; and the Illumos
flavour of x86-64 selected by `-m elf_x86_64_sol2`, which differs from the
Linux one in the default dynamic linker it supplies and in nothing else that
is in scope here.

This milestone builds on the worktree the previous step left in `/app`.
Earlier milestones are carried forward as one end-to-end **gate** group each,
5 of them, named `gate-m01`, `gate-m02` and so on. A gate group is a
precondition rather than work of its own: if any of them fails this step is not
satisfied regardless of what else works — see `TEST_INTERFACE.md` § 3, and the
per-milestone sections that follow it for what each earlier step covers. The
cases themselves ship with the checker and are not in your workspace, so
re-check the earlier milestones' behaviour yourself before you hand the worktree
on.

## Read these local references

- `spec/README.md` — the navigation page for this step's specification set, and
  the index of every normative document shipped with it.
- `spec/13-symver-and-platforms.md` — the document that is new at this
  milestone: `.symver`, the `-m` emulations and the interpreter rules, and the
  syntax of `--retain-symbols-file` and `@FILE`. Sections 3 and 4 are
  transcribable tables; the checker compares them literally.
- `TEST_INTERFACE.md` — the public contract this step is checked against: what
  gets built, how the linker is invoked, what the checker inspects, the time
  budgets, and an explicit list of what is out of scope.
- `WORKSPACE_HANDOFF.md` — what must survive into the next step, and how to pick
  this work up from a cold start.

## Meta requirements

- `cargo build --release` in `/app` must produce a binary target named `wild`.
  The checker builds with `cargo build --release --bin wild` and runs the
  resulting executable; a workspace that does not yield `wild` does not satisfy
  the milestone regardless of what else is in it.
- Do not vendor, wrap, shell out to or otherwise delegate to another linker
  (`ld`, `ld.lld`, `mold`, `gold`) at link time. The checker inspects the ELF you
  produce, and delegating is detectable and does not satisfy it.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route.
- You have about 2 hours of wall clock for this step; `./timer.sh` prints
  what is left. Leave the workspace buildable when you stop — the next step
  starts from exactly what you leave behind.
