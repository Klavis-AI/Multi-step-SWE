# Specification set for step 09 — Debug indexes, the full SECTIONS grammar and in-memory linking

These are the normative documents for this milestone and every milestone
it inherits. Implement from them: a behaviour that is graded is a behaviour
described here or in `../TEST_INTERFACE.md`.

| document | subject |
| --- | --- |
| [`00-compatibility-profile.md`](00-compatibility-profile.md) | The benchmark's own choices where the ELF specifications leave a linker free: default layout, page policy, string merging, garbage collection, entry point, option spelling and what a diagnosed failure looks like. |
| [`01-elf-container.md`](01-elf-container.md) | Every structure, field offset and constant of the 64-bit little-endian ELF container a linker must read from its inputs and write to its output. |
| [`02-symbols-and-archives.md`](02-symbols-and-archives.md) | How a linker interprets binding, type and visibility, which of several definitions of a name wins, and how `ar` archives are searched lazily for the members that satisfy still-undefined references. |
| [`03-x86-64-relocations.md`](03-x86-64-relocations.md) | The complete `R_X86_64_*` relocation table with numbers, arithmetic, field widths and overflow conditions, and the instruction rewrites permitted for the GOT-relative relaxation relocations. |
| [`04-layout-and-sections.md`](04-layout-and-sections.md) | How input sections become output sections and output sections become loadable segments, including merge and COMDAT deduplication, garbage collection, and the `.eh_frame` / `.eh_frame_hdr` unwind tables. |
| [`05-dynamic-linking.md`](05-dynamic-linking.md) | How a linker builds the dynamic half of an ELF image: the dynamic symbol table, the GNU hash table, the dynamic array, and the GOT and PLT that make cross-module references work. |
| [`06-debug-info-and-notes.md`](06-debug-info-and-notes.md) | What a linker owes DWARF sections — relocation, tombstoning, string merging and compression — and how it combines the GNU note sections. |
| [`07-aarch64-relocations.md`](07-aarch64-relocations.md) | The AArch64 ELF relocation set: what each type computes, which instruction field it writes, and when it overflows. |
| [`08-thread-local-storage.md`](08-thread-local-storage.md) | The TLS segment, the four access models, and the relaxations a linker performs between them on x86-64 and AArch64. |
| [`09-symbol-versioning.md`](09-symbol-versioning.md) | The three `.gnu.version*` sections, the records inside them, and how `name@version` and `name@@version` are encoded, requested and resolved. |
| [`10-linker-scripts.md`](10-linker-scripts.md) | The subset of the GNU ld script language this linker must accept, with the exact expression grammar, built-in functions and layout semantics that are graded. |
| [`11-riscv-relocations.md`](11-riscv-relocations.md) | Everything a linker must know about the RV64 psABI: relocation numbers and fields, the HI20/LO12 pairing rule, linker relaxation with byte deletion, e_flags merging, and the compact CREL relocation format (a container format that is *not* RV64-specific, see §9). |
| [`12-version-scripts.md`](12-version-scripts.md) | Grammar of version scripts, the pattern-matching precedence that decides which node claims a symbol, and the effect on `.dynsym`, `.symtab` and the version sections. |
| [`13-symver-and-platforms.md`](13-symver-and-platforms.md) | Symbol versioning that arrives from input objects instead of the command line, the ELF details that differ on Solaris/Illumos targets, and the exact syntax of `--retain-symbols-file` and `@FILE`. |
| [`14-loongarch-relocations.md`](14-loongarch-relocations.md) | The LoongArch64 ELF ABI as a linker sees it: relocation numbers and instruction fields, `pcalau12i` page arithmetic, GOT and TLS families, and `e_flags` compatibility. |
| [`15-sframe-and-properties.md`](15-sframe-and-properties.md) | How to concatenate, re-sort and re-header `.sframe` input sections, and how to combine `.note.gnu.property` records across input objects into a single output note. |
| [`16-lto-plugin-api.md`](16-lto-plugin-api.md) | What a linker must expose so that the GCC and LLVM LTO plugins can claim IR inputs, ask about symbol resolution, and hand back real object files. |
| [`17-relr-and-relocatable.md`](17-relr-and-relocatable.md) | Encoding relative relocations as a RELR bitmap, and producing an `ET_REL` partial object that can be linked again. |
| [`18-gdb-index.md`](18-gdb-index.md) | Building the debugger index section: CU list, address area, hash table, constant pool. |

Two notes on what is graded in this set. `15-sframe-and-properties.md` arrived
two milestones ago with only half of it graded; both halves are graded here, as
they were at the previous milestone — the `.note.gnu.property` half, and the
`.sframe` half, where `--wild-experimental-sframe` produces a section a case
inspects and `--discard-sframe`, new here, must suppress it. And
`18-gdb-index.md` is new at this milestone and is graded in full except for the
DIE-walking route its §7.5 sketches, which is explicitly out of scope. See
`../TEST_INTERFACE.md` for both.

## Licences and provenance

See [`LICENSES.md`](LICENSES.md) for where each document comes from, which
edition it is pinned to, and the licence it is redistributed under.
