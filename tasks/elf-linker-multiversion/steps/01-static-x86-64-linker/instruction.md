# Step 01 — Static x86-64 linker

## Implement this milestone

Build the static core of the linker: read ELF64 relocatable objects and `ar`
archives, resolve symbols across them, apply the `R_X86_64_*` relocation table,
lay the result out into segments, and write a runnable static non-PIE x86-64
executable.

Concretely, this milestone covers ELF64 identification with class and
data-encoding validation -- an input's `EI_OSABI` byte is accepted whatever it
says and is never validated, at this or any later milestone, though the output
always carries `ELFOSABI_NONE`; section-header parsing including `SHT_SYMTAB`,
`SHT_STRTAB`, `SHT_RELA`, `SHT_GROUP`, `SHT_NOBITS` and `SHT_SYMTAB_SHNDX` with
their `sh_link`/`sh_info` semantics; the gABI binding and visibility lattice with
weak and common symbols, resolving multiple strong definitions of the same
symbol by keeping the first one encountered (this milestone does not diagnose
the collision — see `spec/02-symbols-and-archives.md`); archives read through
the symbol index with lazy extraction driven by the undefined set and
`--start-group`/`--end-group` iterated to a fixpoint; the complete `R_X86_64_*`
table with per-type width and signedness handling (overflow diagnostics are a
later milestone); deterministic section-to-segment mapping and `PT_LOAD`
emission; mergeable string and fixed-entsize sections; `--gc-sections` with the
documented root set; `.eh_frame` CIE deduplication with a synthesised
`.eh_frame_hdr` search table and `PT_GNU_EH_FRAME` when `--eh-frame-hdr` is
given (`spec/04-layout-and-sections.md` §7: passing the flag is what asks for
them, and emitting them unasked is permitted but not required); and
`--strip-all`/`--strip-debug` (which at this milestone drop the `.debug_*`
sections and keep `.symtab`/`.strtab`). `--build-id` must be *accepted* here
but emits nothing until the next milestone, so it is not required at this one.

The measure of this milestone is that objects compiled locally and linked by
your `wild` produce an executable the checker can inspect field by field and then
run: correct ELF headers and program headers, correct section addresses and
sizes, no surviving relocations, and the program's own output and exit status.

`/app` starts from an empty Cargo workspace whose only binary target is `wild`.

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
