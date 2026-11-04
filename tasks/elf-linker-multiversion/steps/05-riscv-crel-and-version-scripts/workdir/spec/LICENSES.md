# Provenance and licences for this specification set

Every document under `spec/` was **written for this benchmark**. None of them is
a copy of an upstream document: the primary sources are variously unlicensed,
GFDL, CC BY-SA or GPL, and redistributing them here is not available to us. What
is reproduced from them is fact rather than expression — structure layouts,
field offsets, relocation numbers, arithmetic expressions and constants — which
is what a linker actually has to agree with.

Each document names its own sources in its `## Provenance` section. The table
below covers the **complete** specification set. Each milestone ships only the
documents its work needs, so a row naming a document that is not in this
milestone's `spec/` directory describes one that arrives later; it is not a
missing file.

| primary source | edition pinned | where it is restated |
| --- | --- | --- |
| System V ABI, generic part (ELF gABI) | current text at <https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html> | `01-elf-container.md`, `02-symbols-and-archives.md`, `04-layout-and-sections.md` |
| System V ABI, AMD64 Architecture Processor Supplement (x86-64 psABI) v1.0 | <https://gitlab.com/x86-psABIs/x86-64-ABI> at `e1ce0983` (2025-03-12) | `03-x86-64-relocations.md` |
| "ELF Handling For Thread-Local Storage", Ulrich Drepper, v0.21 (2013-08-22) | <https://akkadia.org/drepper/tls.pdf> | `08-thread-local-storage.md` |
| Linux Standard Base Core Specification, Generic Part, 5.0.0 | <https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic.html> | `05-dynamic-linking.md`, `09-symbol-versioning.md` |
| GNU `ld` documentation, `ld/ld.texi` | `binutils-2_47` | `10-linker-scripts.md` and its extensions |
| ELF for the Arm 64-bit Architecture (`aaelf64`), ARM ABI release 2025Q4, CC BY-SA 4.0 | <https://github.com/ARM-software/abi-aa> | `07-aarch64-relocations.md` |
| RISC-V ELF psABI | <https://github.com/riscv-non-isa/riscv-elf-psabi-doc> | `11-riscv-relocations.md` |
| LoongArch ELF ABI | <https://github.com/loongson/la-abi-specs> | `14-loongarch-relocations.md` |
| GNU/Gold linker plugin API, `plugin-api.h` | `binutils-2_47` | `16-lto-plugin-api.md` |
| CREL compact relocation format | <https://maskray.me/blog/2024-03-09-a-compact-relocation-format-for-elf> | `11-riscv-relocations.md` |
| SFrame stack-trace format | <https://sourceware.org/binutils/docs/sframe-spec.html> | `15-sframe-and-properties.md` |
| RELR relative-relocation packing | generic-abi thread, <https://groups.google.com/g/generic-abi/c/bX460iggiKg> | `17-relr-and-relocatable.md` |

Where a primary source and the behaviour a real linker exhibits disagree, these
documents follow the behaviour that is actually graded and say so in the
document's `## Provenance` section. `00-compatibility-profile.md` collects the
choices that no specification makes for us — section order, page size, merge
policy, the garbage-collection root set — because output byte-identity across
linkers is unspecified and something has to fix it.
