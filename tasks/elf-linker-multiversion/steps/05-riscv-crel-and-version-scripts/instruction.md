# Step 05 — RISC-V, CREL and the version-script grammar

## Implement this milestone

Add a third architecture and finish the version-script grammar.

**RISC-V.** A third backend with its own relocation table and `e_flags`
compatibility rules, plus CREL compact relocation decoding.

**Version scripts.** `global:` and `local:` blocks, glob patterns,
`extern "C"` and `extern "C++"` demangled matching, and correct lookup across a
collection of versions.

Also in scope: `--wrap`; `--allow-multiple-definition` and `-z muldefs`;
`--unresolved-symbols=` with `--warn-unresolved-symbols` and
`--error-unresolved-symbols`; `-z defs`, `-z undefs`, `-z interpose`;
`-Bsymbolic`, `-Bsymbolic-functions`, `-Bsymbolic-non-weak`; `--exclude-libs
ALL`; TLS addresses computed from segment rather than section layout, including
custom `SHT_NOBITS` TLS sections; and `.dynsym` written in parallel with
versions computed during layout.

This milestone also picks up GNU-make jobserver participation (honouring the
job slots `MAKEFLAGS` hands out instead of sizing the thread pool blindly).
It is out of scope — no requirement group observes it and `TEST_INTERFACE.md`
does not describe it — so implement it only once the work in scope is done.

This milestone builds on the worktree the previous step left in `/app`.
Earlier milestones are carried forward as one end-to-end **gate** group each,
4 of them, named `gate-m01`, `gate-m02` and so on. A gate group is a
precondition rather than work of its own: if any of them fails this step is not
satisfied regardless of what else works — see `TEST_INTERFACE.md` § 3, and the
per-milestone sections that follow it for what each earlier step covers. The
cases themselves ship with the checker and are not in your workspace, so
re-check the earlier milestones' behaviour yourself before you hand the worktree
on.

## Read these local references

- `spec/README.md` — the navigation page for this step's specification set, and
  the index of every normative document shipped with it.
- `TEST_INTERFACE.md` — the public contract this step is checked against: what
  gets built, how the linker is invoked, what the checker inspects, the time
  budgets, and an explicit list of what is out of scope.
- `WORKSPACE_HANDOFF.md` — what must survive into the next step, and how to pick
  this work up from a cold start.
- `spec/11-riscv-relocations.md` — new at this step: the `R_RISCV_*` table, the
  HI20/LO12 pairing rule (§4), relaxation (§5) — which this step does **not**
  perform, but whose `--no-relax` option the checker passes and which must
  therefore be accepted as a no-op — `e_flags` merging and mismatch (§7), and
  the CREL decoding algorithm (§9), which is the format you must read but are
  never asked to write, and which is *not* RISC-V-specific: the CREL objects
  the checks use are x86-64.
- `spec/12-version-scripts.md` — new at this step: the version-script grammar
  (§2), pattern syntax and `extern` blocks (§3), the three-phase matching
  precedence that decides which node claims a symbol when several could (§4,
  with worked examples in §5), the effect on `.dynsym` and `.symtab` (§6), and
  the malformed scripts that must be rejected (§8).

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
