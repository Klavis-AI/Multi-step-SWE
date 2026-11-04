# Step 08 — LTO, RELR and relocatable output

## Implement this milestone

Add link-time optimisation, relocation packing, relocatable output and the
relaxations that only make sense once several backends exist.

**LTO.** The Gold-compatible linker plugin API — plugin loading, claim and
release of IR files including archive members, symbol-resolution feedback and
the added-file callback — driving link-time optimisation end to end. A `--wrap`
redirection and a `--defsym` alias must both work when the referring code comes
out of bitcode and the definition lives in an ordinary object, and a `--defsym`
target must survive garbage collection even when nothing else refers to it.

**Packing and relocatable output.** RELR-packed relative relocations via
`--pack-dyn-relocs=relr`, its `-z pack-relative-relocs` spelling (which already
parses, and must now actually pack) and `--use-android-relr-tags`;
`--compress-debug-sections=zlib`, `=zstd` and `=none`; and static relocatable (`-r`)
output.

**Relaxation and thunks.** RISC-V call, HI20 and `R_RISCV_ALIGN` relaxation
including `lui` deletion and `c.lui` compression, with byte-reduction
accounting that keeps layout convergent; and AArch64 range-extension thunks for
a branch whose target is pushed past `R_AARCH64_CALL26`'s ±128 MiB reach by
section content. The iterative-convergence sub-requirement (re-running thunk
allocation when inserted thunks push additional branches out of range) is out
of scope here: the reference at tag 0.9.0 uses a conservative single-pass
approach with a reserved thunk budget and does not implement a convergence
loop. (Only the content-driven form is in scope: see `TEST_INTERFACE.md`'s
step-8 section.)

**Script expressions.** `PROVIDE`/`PROVIDE_HIDDEN`, `ASSERT`, `MEMORY` with
`ORIGIN`/`LENGTH`, `LOADADDR`, `SEGMENT_START`, `ALIGNOF`, the bitwise, logical
and unary operators, and `-Ttext`/`-Tdata`/`-Tbss`. Two script forms that
earlier steps deferred to here also land now: the `?` and `[...]`
metacharacters in an input-section pattern, and the numeric address slot in an
output-section header (`.name 0x500000 : { ... }`). The constructs the
specification marks MUST-reject stay rejected; `TEST_INTERFACE.md`'s step-8
section lists which of them are in scope and which are out of scope here.

Also: `--dependency-file`; `--auxiliary`; `--no-identity-comment`; `--nmagic`;
`--wild-experimental-sframe`; `--disable-new-dtags`, which must now actually
select `DT_RPATH` over `DT_RUNPATH` rather than being accepted and ignored; the
section-count escape — when the output needs `0xff00` (65280) sections or more,
`e_shnum` and `e_shstrndx` hold escape values, section header 0 holds the real
count, and a `.symtab_shndx` section is emitted; and the refactoring that puts
every ELF-specific behaviour behind a trait so the core is format-generic.

This milestone builds on the worktree the previous step left in `/app`.
Earlier milestones are carried forward as one end-to-end **gate** group each,
7 of them, named `gate-m01`, `gate-m02` and so on. A gate group is a
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
