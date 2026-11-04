# Step 02 — Dynamic output and debug info

## Implement this milestone

Make the linker produce **dynamic** output, and make it carry debug information
correctly.

This milestone covers `.dynsym` and `.dynstr` (this milestone's reference emits
`.gnu.hash` alone — no SysV `.hash` table and no `DT_HASH`; knowing the `.hash`
format well enough to read one from an input shared object is still worth
having, but writing one is not required here); the `DT_*` tag set (`NEEDED`, `SONAME`,
`RPATH`/`RUNPATH`, `INIT`/`FINI`, `INIT_ARRAY`/`FINI_ARRAY` with their sizes,
`PLTGOT`, `RELA`/`RELASZ`/`RELAENT`, `FLAGS`/`FLAGS_1`); PLT and GOT synthesis
that is always eagerly bound — a `.plt.got`-style stub over a GOT slot filled
by `R_X86_64_GLOB_DAT`, never a lazily-bound classic PLT, so `JMPREL`,
`PLTREL` and `R_X86_64_JUMP_SLOT` are meanings to know how to read, not output
this milestone produces; `PT_INTERP`; `-shared` and `-pie` output with
`R_X86_64_RELATIVE` for absolute address slots; `-z now` and `DF_BIND_NOW`;
and `--version-script` controlling which symbols reach `.dynsym`.

On the debug side: DWARF sections relocated with the correct tombstone value per
source section when the target has been discarded, early dropping of
merged-string debug sections, `SHF_COMPRESSED` input sections, parallel string
merging with a merged-string offset cache, and `.note.gnu.build-id`,
`.note.gnu.property` and `.note.ABI-tag` carried into a correct `PT_NOTE`.

This milestone builds on the worktree the previous step left in `/app`. The
earlier milestone is carried forward as one end-to-end **gate** group, named
`gate-m01`. A gate group is a precondition rather than work of its own: if any
of them fails this step is not satisfied regardless of what else works — see
`TEST_INTERFACE.md` § 3, and the per-milestone sections that follow it for what
each earlier step covers. The cases themselves ship with the
checker and are not in your workspace, so re-check the earlier milestones'
behaviour yourself before you hand the worktree on.

## Read these local references

- `spec/README.md` — the navigation page for this step's specification set, and
  the index of every normative document shipped with it.
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
