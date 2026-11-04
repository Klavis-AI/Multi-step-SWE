# Step 09 — Debug indexes, the full SECTIONS grammar and in-memory linking

## Implement this milestone

Close out the linker: debug indexes, the rest of the `SECTIONS` grammar, and
linking as a library over in-memory inputs.

**`--gdb-index`.** CU list, address area, the symbol hash table and the
constant pool. The symbol table is built from the inputs'
`.debug_gnu_pubnames`/`.debug_gnu_pubtypes` — the sections `-ggnu-pubnames`
produces — which are consumed and do not reappear in the output.
`spec/18-gdb-index.md` describes the whole section byte by byte.

**The rest of the script grammar.** Custom `PHDRS` with several program headers
per output section; `AT`/LMA and `LOADADDR`; memory-region assignment;
`SORT`/`SORT_BY_NAME`/`SORT_BY_ALIGNMENT` and `--sort-section`; ternary and
modulo expressions; `DEFINED`; `CONSTRUCTORS`; fill patterns; `/DISCARD/`;
`SIZEOF_HEADERS`; `OUTPUT_FORMAT`/`OUTPUT_ARCH`; top-level symbol definitions
inside `SECTIONS`; and multiple relative location counters.

Also in scope: GOT-based RELR bitmap packing over consecutive relocations;
`--rosegment`/`--no-rosegment`, of which `--rosegment` is the default;
`--discard-sframe` as an explicit switch (the `.sframe` default did not change
at this milestone — input `.sframe` sections have been dropped since the
previous one); the `__bss_start` built-in symbol; and `.tbss` kept `SHT_NOBITS`
so uninitialised TLS occupies no file space of its own.

The release also introduces a `FileSystem` abstraction that lets the whole
linker run as a library over in-memory inputs and outputs. Build it if it suits
your structure, but budget accordingly: **it is out of scope.** Every case
drives the linker as a process over files on disk. See `TEST_INTERFACE.md`.

This milestone builds on the worktree the previous step left in `/app`.
Earlier milestones are carried forward as one end-to-end **gate** group each,
8 of them, named `gate-m01`, `gate-m02` and so on. A gate group is a
precondition rather than work of its own: if any of them fails this step is not
satisfied regardless of what else works — see `TEST_INTERFACE.md` § 3, and the
per-milestone sections that follow it for what each earlier step covers. The
cases themselves ship with the checker and are not in your workspace, so
re-check the earlier milestones' behaviour yourself before you hand the worktree
on.

## Read these local references

- `spec/README.md` — the navigation page for this step's specification set, and
  the index of every normative document shipped with it.
- `spec/18-gdb-index.md` — new at this milestone, and the only complete
  description of the `.gdb_index` format here: header, CU list, address area,
  hash table, constant pool, with a worked example decoded byte by byte.
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
