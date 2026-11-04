# Step 03 — AArch64, TLS and symbol versioning

## Implement this milestone

Add a second architecture, thread-local storage, and symbol versioning — the
three things that force the relocation engine behind an interface.

**AArch64.** The `aaelf64` relocation table, including the
`ADR_PREL_PG_HI21`/`ADD_ABS_LO12_NC` pair, `CALL26`/`JUMP26` with their range
checks, and the `LDST*_ABS_LO12_NC` family with per-width scaling — behind a
backend interface that leaves the x86-64 results unchanged.

**TLS.** `PT_TLS` with `.tdata` and `.tbss` and a correct static TLS block; the
general dynamic, local dynamic, initial exec and local exec models; TLSDESC; and
the GD→IE, GD→LE and LD→LE relaxations. The relaxations are checked by their
*effect* — the address the program computes — and not by the instruction bytes
you emit, which `spec/08` §7.1 and §7.2 describe but leave you more than one
legal way to satisfy.

**Versioning.** `.gnu.version` index arrays and `.gnu.version_r` records with
correct `vna_hash` values, and the `name@version` versus `name@@version`
distinction. You *consume* the `.gnu.version_d` your inputs carry; **emitting**
a `.gnu.version_d` of your own belongs to a later milestone (`spec/09` §4) and
is not required here, and diagnosing a request for a version no dependency
defines is a SHOULD that is out of scope here (`spec/09` §8).

Also in scope: copy relocations, with the copied symbol emitted into both
`.symtab` and `.dynsym`; `-z relro` with `PT_GNU_RELRO` covering the right
slice; `--whole-archive`; `--sysroot=DIR`; `-z nocopyreloc`;
`__start_`/`__stop_` section retention; and hard errors for undefined symbols
and wrong-architecture inputs.

This milestone builds on the worktree the previous step left in `/app`. Each
earlier milestone is carried forward as one end-to-end **gate** group, named
`gate-m01`, `gate-m02` and so on. A gate group is a precondition rather than
work of its own: if any of them fails this step is not satisfied regardless of
what else works — see `TEST_INTERFACE.md` § 3, and the per-milestone sections
that follow it for what each earlier step covers. The cases themselves ship
with the checker and are not in your workspace, so re-check the earlier
milestones' behaviour yourself before you hand the worktree on.

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
