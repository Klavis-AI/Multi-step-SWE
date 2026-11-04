# Step 04 — Script-driven layout

## Implement this milestone

Make layout **script-driven**: parse and evaluate the core of the `SECTIONS`
grammar and let it override the built-in layout algorithm.

This milestone covers output-section descriptions, input-section wildcards,
symbol assignments relative to sections, explicit output addresses (written
`. = ADDR;` ahead of the description) and output-section alignment,
`KEEP`, `ENTRY`, and output-section attributes inherited from the corresponding
input sections; defined behaviour when a script conflicts with a command-line
layout option; and `--entry`.

Alongside the script work: version definitions emitted into shared objects;
`--start-lib`/`--end-lib` as an archive-free alternative to `ar`; thin archives;
`-z norelro`; `-z origin` setting `DF_ORIGIN`/`DF_1_ORIGIN`; correct linking
against protected symbols in shared objects; COMDAT groups that define the same
symbol from several members; `-l:name.ext`; and `--undefined`.

This milestone builds on the worktree the previous step left in `/app`. Each
earlier milestone is carried forward as one end-to-end **gate** case: a static
x86-64 executable that links and runs (milestone 1), `-shared` yielding an
`ET_DYN` with a coherent dynamic table (milestone 2), and the AArch64 backend
linking a runnable executable (milestone 3). A gate case is a precondition
rather than work of its own: if any of them fails this step is not satisfied
regardless of what else works — see `TEST_INTERFACE.md` § 3.

**The gate cases are part of the checker and are not in your workspace**, so
there is no file for you to run. Reproduce them yourself before you hand the
worktree on: the three links above are the whole of it, and a regression in
any one of them leaves the step unsatisfied.

## Read these local references

- `spec/README.md` — the navigation page for this step's specification set, and
  the index of every normative document shipped with it.
- `TEST_INTERFACE.md` — the public contract this step is checked against: what
  gets built, how the linker is invoked, what the checker inspects, the
  requirement groups, the time budgets, and an explicit list of what is out of
  scope. Read the step-4 section of `TEST_INTERFACE.md` before you start:
  several script constructs that look in scope are explicitly deferred to later
  milestones.
- `spec/10-linker-scripts.md` — the script grammar. §4 gives the exact
  output-section header, including which slot `ALIGN(dec)` goes in and which
  header constructs are parse errors at this milestone; §5 is the
  input-statement table, whose per-file `FILE(pat)` form is a parse error
  until step 8; §13 is the per-milestone table of what must work when; and
  §15 collects the option interactions -- `--gc-sections` being on by
  default, `--entry` overriding a script's `ENTRY`, and the placement of
  sections a `-T` script never mentions.
- `spec/02-symbols-and-archives.md` §7 (archive extraction) and §8 (names left
  undefined at the end of the link). The `-u`/`--undefined=` rule is stated at
  the end of §5, and the difference between it and a reference written in an
  object decides whether an unresolved `-u` is an error.
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
