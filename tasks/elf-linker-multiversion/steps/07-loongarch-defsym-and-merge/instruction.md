# Step 07 — LoongArch64, `--defsym` and non-string section merging

## Implement this milestone

Add a fourth architecture and expression support in `--defsym`.

**LoongArch64.** A fourth backend with its own relocation table.

**Not SFrame.** `spec/15-sframe-and-properties.md` describes the `.sframe`
format because a later milestone builds on it; at this milestone
`--wild-experimental-sframe` is not an accepted option and nothing about
`.sframe` is in scope at this milestone. Only that document's
`.note.gnu.property` half is in scope here. See `TEST_INTERFACE.md`.

**Properties.** `PT_GNU_PROPERTY` with GNU property notes combined per type —
the `*_FEATURE_1_AND` types intersected across input files, the
`X86_ISA_1_USED`/`_NEEDED` types unioned — and `-z x86-64-v2`/`-v3`/`-v4`
contributing the requested level to `GNU_PROPERTY_X86_ISA_1_NEEDED`.

Also in scope: `--defsym=symbol=expression` supporting a literal, a symbol
alias, and `SYMBOL±N`/chained `+`/`-` arithmetic, and the same expression forms
in a **top-level** linker-script assignment — `NAME = expr;` written outside any
output-section description, before or after the `SECTIONS` block. (The fuller
expression grammar, and symbol assignment *inside* an output-section body to
anything other than the location counter `.`, are not part of this milestone —
see `TEST_INTERFACE.md`.)
deduplication of non-string mergeable sections; `--hash-style={sysv,gnu,both}`
as a diagnosed **success** with the requested table(s) present and the others
absent; negation (`^`) in version-script globs; `--section-start=.sec=addr`;
`-z max-page-size`; `-z stack-size`; `.init_array`/`.fini_array` ordering by
priority suffix, storing **ascending** priority order in both arrays (the
linker does not reverse `.fini_array`; see `TEST_INTERFACE.md`); a
section-specific `sh_addralign >= 8` floor on `.init_array`/`.fini_array`; and
the `variant_pcs`/`.variant_cc` symbol marking with its `DT_AARCH64_VARIANT_PCS`
/ `DT_RISCV_VARIANT_CC` dynamic tags.

This milestone builds on the worktree the previous step left in `/app`. Each
earlier milestone is carried forward as one end-to-end **gate** group, six of
them: a static x86-64 executable links and runs; `-shared` yields `ET_DYN` with
a coherent dynamic table; the AArch64 backend links a runnable executable; a
`SECTIONS` script drives output layout; the RISC-V backend links a runnable
executable; and `.symver` default/non-default aliases resolve. The checker also
checks that the workspace builds and that the `wild` it builds starts. These
groups live with the checker and are not in this workspace to run, so re-check
those behaviours yourself before you hand the worktree on: they are not work of
their own, but if any of them fails this step is not satisfied regardless of
what else works — see `TEST_INTERFACE.md` § 3.

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
