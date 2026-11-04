# The ELF64 container

> Every structure, field offset and constant of the 64-bit little-endian ELF container a linker must read from its inputs and write to its output.

## Provenance

This restates the object-file chapters of the **System V Application Binary
Interface**, gABI edition, as maintained at
`https://www.sco.com/developers/gabi/latest/contents.html`, together with the
GNU extensions to it that Linux toolchains actually emit — `SHF_GNU_RETAIN`,
`SHF_EXCLUDE`, `SHT_GNU_*`, `PT_GNU_STACK`, `PT_GNU_EH_FRAME`,
`PT_GNU_RELRO`, `PT_GNU_PROPERTY` and the `NT_GNU_*` note types — whose
authoritative machine-readable form is `<elf.h>` as shipped by glibc 2.36
(Debian bookworm), the version installed in the grading image. Numeric values
below were cross-checked against that header. Container facts specific to
x86-64 (`EM_X86_64 = 62`, `ELFCLASS64`, `ELFDATA2LSB`) come from the x86-64
psABI, repository `gitlab.com/x86-psABIs/x86-64-ABI`, master at commit
`e1ce0983` (2025-03-12).

Deliberately narrowed: only **ELFCLASS64 + ELFDATA2LSB** is described.
32-bit ELF and big-endian ELF are out of scope per `TEST_INTERFACE.md` §4 —
but a linker must still *recognise* them well enough to reject them, which is
why `e_ident` is given in full. Core files, `SHT_HASH`, `SHT_SHLIB` and the
OS- and processor-specific ranges are mentioned only where a linker must not
choke on them.

Throughout: all multi-byte integers are **two's-complement little-endian**.
"Offset" means the byte offset of a field from the start of its structure.
A field marked *ignored* may hold any value in an input and MUST be written as
the stated value in an output.

## 1. `e_ident` — the identification bytes

`e_ident` is the first 16 bytes of every ELF file, and is the only part of the
file whose layout does not depend on the class.

| index | name | size | value a linker must accept | value a linker must emit |
| --- | --- | --- | --- | --- |
| 0 | `EI_MAG0` | 1 | `0x7f` | `0x7f` |
| 1 | `EI_MAG1` | 1 | `'E'` (`0x45`) | `0x45` |
| 2 | `EI_MAG2` | 1 | `'L'` (`0x4c`) | `0x4c` |
| 3 | `EI_MAG3` | 1 | `'F'` (`0x46`) | `0x46` |
| 4 | `EI_CLASS` | 1 | `ELFCLASS64 = 2` only | `2` |
| 5 | `EI_DATA` | 1 | `ELFDATA2LSB = 1` only | `1` |
| 6 | `EI_VERSION` | 1 | `EV_CURRENT = 1` | `1` |
| 7 | `EI_OSABI` | 1 | any value; it never decides whether an input can be linked | `0` |
| 8 | `EI_ABIVERSION` | 1 | `0` | `0` |
| 9–15 | `EI_PAD` | 7 | any | all zero |

A linker MUST reject an input whose magic is wrong, whose `EI_CLASS` is not
`2`, or whose `EI_DATA` is not `1`, with a diagnostic per
`00-compatibility-profile.md` §8. `ELFCLASS32 = 1` and `ELFDATA2MSB = 2` are
recognised only so that they can be named in the diagnostic.

`EI_OSABI` of `3` (`ELFOSABI_GNU`, also spelled `ELFOSABI_LINUX`) appears on
inputs that use `STB_GNU_UNIQUE` or `STT_GNU_IFUNC`, and `6`
(`ELFOSABI_SOLARIS`) on Illumos objects. A linker MUST accept an input
whatever its `EI_OSABI` says; no milestone validates that byte on an input, and
no milestone rejects a file because of it.

The output's `EI_OSABI` MUST be `0` (`ELFOSABI_NONE`, which `readelf` prints as
`UNIX - System V`) with `EI_ABIVERSION` `0`, **regardless of the markings on
the inputs** -- an input's value MUST NOT be propagated into the output. This
is graded: measured at all nine milestones, a link of `ELFOSABI_GNU`,
`ELFOSABI_SOLARIS` and `ELFOSABI_NONE` objects, in any mixture, produces `0`.
Step 1's corpus exercises the unmixed half -- every fixture there is built
with `ELFOSABI_NONE` -- and the mixed-input link is exercised from step 6.

## 2. `Elf64_Ehdr` — the file header

64 bytes, at file offset 0.

| offset | size | field | meaning and required value |
| --- | --- | --- | --- |
| 0 | 16 | `e_ident` | §1 |
| 16 | 2 | `e_type` | `ET_REL = 1` for inputs; the output is `ET_EXEC = 2` for a non-PIE executable, `ET_DYN = 3` for PIE and for `-shared`, `ET_REL = 1` for `-r`. `ET_NONE = 0`, `ET_CORE = 4` are not valid linker inputs. |
| 18 | 2 | `e_machine` | `EM_X86_64 = 62`. Every input MUST agree with the output's machine; a mismatch is a diagnosed error. (`EM_AARCH64 = 183`, `EM_RISCV = 243`, `EM_LOONGARCH = 258` are introduced by later milestones.) |
| 20 | 4 | `e_version` | `EV_CURRENT = 1`. Emit `1`. |
| 24 | 8 | `e_entry` | virtual address of the entry point; `0` for an object or a shared object with no entry. For an executable this is the resolved address of the entry symbol. |
| 32 | 8 | `e_phoff` | file offset of the program header table, or `0` if there is none. Inputs (`ET_REL`) have `0`. |
| 40 | 8 | `e_shoff` | file offset of the section header table, or `0` if there is none. |
| 48 | 4 | `e_flags` | processor-specific flags. **`0` on x86-64.** Emit `0`. |
| 52 | 2 | `e_ehsize` | size of this header: **`64`** (`0x40`). |
| 54 | 2 | `e_phentsize` | size of one program header: **`56`** (`0x38`); `0` when `e_phnum` is `0`. |
| 56 | 2 | `e_phnum` | number of program headers. If it would exceed `0xffff` it is written as `PN_XNUM = 0xffff` and the true count lives in `sh_info` of section header 0 — a linker MAY refuse to produce that many segments instead. |
| 58 | 2 | `e_shentsize` | size of one section header: **`64`** (`0x40`). |
| 60 | 2 | `e_shnum` | number of section headers, *including* the null entry at index 0. If it would exceed `0xff00` it MUST be written as `0` and the true count placed in `sh_size` of section header 0. |
| 62 | 2 | `e_shstrndx` | section index of `.shstrtab`. If it would be `≥ SHN_LORESERVE` it MUST be written as `SHN_XINDEX = 0xffff` and the true index placed in `sh_link` of section header 0. `SHN_UNDEF = 0` means there is no section name string table. |

The program header table MUST immediately follow the file header when a
program header table exists; `e_phoff` is then `64`. This is not required by
the gABI but it is what the profile emits and it is what makes `__ehdr_start`
and the `PT_PHDR`-free layout of `00-compatibility-profile.md` §2 work.

## 3. `Elf64_Shdr` — a section header

64 bytes. The section header table has `e_shnum` entries starting at
`e_shoff`. **Index 0 is always the null entry**: every field zero, except
where it carries the escape values for `e_shnum`, `e_shstrndx` and `e_phnum`
described in §2.

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `sh_name` | byte offset into `.shstrtab` of this section's NUL-terminated name |
| 4 | 4 | `sh_type` | §3.1 |
| 8 | 8 | `sh_flags` | §3.2 |
| 16 | 8 | `sh_addr` | virtual address of the section's first byte at execution, or `0` if the section is not `SHF_ALLOC`. In an `ET_REL` input this is `0`. |
| 24 | 8 | `sh_offset` | byte offset of the section's contents in the file. For `SHT_NOBITS` this is a conceptual offset that occupies no bytes: it MUST be a plausible position but the file MUST NOT reserve `sh_size` bytes there. |
| 32 | 8 | `sh_size` | size in bytes of the section's contents |
| 40 | 4 | `sh_link` | §3.3 |
| 44 | 4 | `sh_info` | §3.3 |
| 48 | 8 | `sh_addralign` | required alignment of `sh_addr`, a power of two. `0` and `1` both mean "no constraint"; a linker MUST treat `0` as `1`. `sh_addr % sh_addralign` MUST be `0`. |
| 56 | 8 | `sh_entsize` | size of one fixed-size entry, or `0` if the section does not hold a table of fixed-size entries. §3.3. |

### 3.1 `sh_type`

| value | name | what a linker does with it |
| --- | --- | --- |
| 0 | `SHT_NULL` | inactive. Index 0 only; ignore any other. |
| 1 | `SHT_PROGBITS` | contents come from the file. Copy. |
| 2 | `SHT_SYMTAB` | the static symbol table. §6. At most one per file. |
| 3 | `SHT_STRTAB` | a string table. §5. |
| 4 | `SHT_RELA` | relocations with explicit addends. §7. x86-64 uses these. |
| 5 | `SHT_HASH` | SysV symbol hash table. Never an input to be copied; regenerated if the output needs one. |
| 6 | `SHT_DYNAMIC` | the dynamic table. Only meaningful in `ET_DYN`/`ET_EXEC`. |
| 7 | `SHT_NOTE` | notes. §9. |
| 8 | `SHT_NOBITS` | occupies no file space; `sh_size` bytes of zero at run time. `.bss`, `.tbss`. |
| 9 | `SHT_REL` | relocations without addends. Not produced on x86-64, but a linker MUST be able to read one: the implicit addend is the value already in the field being relocated. |
| 10 | `SHT_SHLIB` | reserved, no defined semantics. Reject or discard. |
| 11 | `SHT_DYNSYM` | the dynamic symbol table. Same entry layout as `SHT_SYMTAB`. |
| 14 | `SHT_INIT_ARRAY` | array of `void (*)()` run at startup. Merged into `.init_array`. |
| 15 | `SHT_FINI_ARRAY` | array of `void (*)()` run at exit. Merged into `.fini_array`. |
| 16 | `SHT_PREINIT_ARRAY` | array of `void (*)()` run before `.init_array`. |
| 17 | `SHT_GROUP` | a COMDAT / section group. §8. |
| 18 | `SHT_SYMTAB_SHNDX` | extended section indices for a symbol table. §6.2. |
| 19 | `SHT_RELR` | compact relative relocations. Introduced by a later milestone. |
| `0x6ffffff5` | `SHT_GNU_ATTRIBUTES` | object attributes. May be discarded. |
| `0x6ffffff6` | `SHT_GNU_HASH` | GNU symbol hash table. Regenerated, never copied. |
| `0x6ffffffd` | `SHT_GNU_verdef` | version definitions. Later milestone. |
| `0x6ffffffe` | `SHT_GNU_verneed` | version requirements. Later milestone. |
| `0x6fffffff` | `SHT_GNU_versym` | per-symbol version indices. Later milestone. |

Ranges: `SHT_LOOS = 0x60000000` … `SHT_HIOS = 0x6fffffff`,
`SHT_LOPROC = 0x70000000` … `SHT_HIPROC = 0x7fffffff`,
`SHT_LOUSER = 0x80000000` … `SHT_HIUSER = 0x8fffffff`. A linker MUST NOT fail
merely because it sees an unknown `sh_type` in one of these ranges; it MAY
discard such a section if nothing references it.

### 3.2 `sh_flags`

| bit | value | name | meaning |
| --- | --- | --- | --- |
| 0 | `0x1` | `SHF_WRITE` | writable at run time |
| 1 | `0x2` | `SHF_ALLOC` | occupies memory at run time |
| 2 | `0x4` | `SHF_EXECINSTR` | executable instructions |
| 4 | `0x10` | `SHF_MERGE` | elements may be merged to eliminate duplicates |
| 5 | `0x20` | `SHF_STRINGS` | elements are NUL-terminated strings |
| 6 | `0x40` | `SHF_INFO_LINK` | `sh_info` holds a section index |
| 7 | `0x80` | `SHF_LINK_ORDER` | output order must follow the section named by `sh_link` |
| 8 | `0x100` | `SHF_OS_NONCONFORMING` | needs OS-specific handling the linker does not have |
| 9 | `0x200` | `SHF_GROUP` | member of a section group (§8) |
| 10 | `0x400` | `SHF_TLS` | thread-local storage |
| 11 | `0x800` | `SHF_COMPRESSED` | contents are compressed; an `Elf64_Chdr` precedes them. Later milestone. |
| 21 | `0x200000` | `SHF_GNU_RETAIN` | must not be garbage-collected (`00-compatibility-profile.md` §6) |
| 30 | `0x40000000` | `SHF_ORDERED` | obsolete Solaris ordering hint; ignore |
| 31 | `0x80000000` | `SHF_EXCLUDE` | exclude from a non-relocatable output |

Masks: `SHF_MASKOS = 0x0ff00000`, `SHF_MASKPROC = 0xf0000000`. Unknown bits in
those masks MUST NOT cause a link failure.

Two input sections MUST NOT be merged into one output section if their
`SHF_WRITE`, `SHF_EXECINSTR`, `SHF_ALLOC` or `SHF_TLS` bits differ, because
`00-compatibility-profile.md` §4 maps those four bits to segment permissions.

### 3.3 `sh_link`, `sh_info` and `sh_entsize` by type

| `sh_type` | `sh_link` | `sh_info` | `sh_entsize` |
| --- | --- | --- | --- |
| `SHT_SYMTAB`, `SHT_DYNSYM` | index of the associated `SHT_STRTAB` | **one greater than the index of the last `STB_LOCAL` symbol**, i.e. the index of the first non-local symbol | `24` (`0x18`) |
| `SHT_RELA` | index of the symbol table the entries' `r_info` symbol indices refer to | index of the section the relocations apply to | `24` (`0x18`) |
| `SHT_REL` | as `SHT_RELA` | as `SHT_RELA` | `16` (`0x10`) |
| `SHT_STRTAB` | `0` | `0` | `0` |
| `SHT_NOTE` | `0` | `0` | `0` |
| `SHT_NOBITS` | `0` | `0` | `0`, or the merge entry size |
| `SHT_GROUP` | index of the symbol table holding the signature symbol | index within that symbol table of the signature symbol | `4` |
| `SHT_SYMTAB_SHNDX` | index of the symbol table it extends | `0` | `4` |
| `SHT_DYNAMIC` | index of the `SHT_STRTAB` holding the names it refers to | `0` | `16` (`0x10`) |
| `SHT_HASH`, `SHT_GNU_HASH` | index of the symbol table it indexes | `0` | `4` / `0` |
| `SHT_GNU_versym` | index of the associated `SHT_DYNSYM` | `0` | `2` |
| `SHT_GNU_verdef`, `SHT_GNU_verneed` | index of the `SHT_STRTAB` holding the names | number of `Verdef`/`Verneed` entries | `0` |
| `SHT_INIT_ARRAY`, `SHT_FINI_ARRAY`, `SHT_PREINIT_ARRAY` | `0` | `0` | `8`, or `0` |
| `SHF_MERGE` `SHT_PROGBITS` | `0` | `0` | size of one element; `1` for a byte-string section |
| anything else | `0` | `0` | `0` |

The `sh_link`/`sh_info` values a linker **emits** must be recomputed for the
output, never copied from an input, because section and symbol indices change.
Getting `.symtab`'s `sh_info` right matters: it is the boundary that lets a
reader find the global symbols, and a symbol table whose `STB_LOCAL` symbols
are not all below `sh_info` is malformed.

For `SHF_MERGE` without `SHF_STRINGS`, `sh_entsize` MUST be non-zero and
MUST divide `sh_size`. For `SHF_MERGE|SHF_STRINGS`, `sh_entsize` is the
character size and is `1` for the byte strings every Linux toolchain emits;
a linker MAY refuse an `sh_entsize` other than `1` on such a section.

## 4. Special section indices

These values appear in `st_shndx` and in `SHT_GROUP` members. They are *not*
section header table indices, and a linker MUST NOT use them to index the
section header table.

| value | name | meaning |
| --- | --- | --- |
| `0` | `SHN_UNDEF` | undefined, missing or irrelevant. A symbol with `st_shndx == SHN_UNDEF` and `st_value == 0` is a reference to be resolved elsewhere. |
| `0xff00` | `SHN_LORESERVE` | start of reserved range |
| `0xff00` | `SHN_LOPROC` / `SHN_BEFORE` | start of processor-specific range; `SHN_BEFORE` orders a section before all others |
| `0xff01` | `SHN_AFTER` | orders a section after all others |
| `0xff1f` | `SHN_HIPROC` | end of processor-specific range |
| `0xff20` | `SHN_LOOS` | start of OS-specific range |
| `0xff3f` | `SHN_HIOS` | end of OS-specific range |
| `0xfff1` | `SHN_ABS` | the symbol's value is absolute and unaffected by relocation |
| `0xfff2` | `SHN_COMMON` | the symbol is an unallocated common block (`02-symbols-and-archives.md` §4) |
| `0xffff` | `SHN_XINDEX` | the real index is in the `SHT_SYMTAB_SHNDX` table (§6.2) |
| `0xffff` | `SHN_HIRESERVE` | end of reserved range |

## 5. String tables (`SHT_STRTAB`)

A string table is an array of bytes containing NUL-terminated strings. A
reference into it is a byte offset. **Offset 0 MUST hold a NUL byte**, so that
offset 0 always denotes the empty string; a `sh_name` or `st_name` of `0`
means "no name". An offset may point into the middle of a string, yielding the
suffix from that point — a linker reading a table MUST support this, and a
linker writing one MAY exploit it but is not graded on doing so. The last byte
of a non-empty string table MUST be NUL.

The linker writes at most three string tables in a static link: `.shstrtab`
(section names, referenced by `sh_name` and by `e_shstrndx`), `.strtab`
(`.symtab` names) and, in a dynamic output, `.dynstr`.

## 6. `Elf64_Sym` — a symbol table entry

24 bytes. The entry at index 0 of every symbol table MUST be entirely zero.

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `st_name` | offset into the associated string table; `0` means unnamed |
| 4 | 1 | `st_info` | binding in the high nibble, type in the low nibble |
| 5 | 1 | `st_other` | visibility in the low two bits; the other six MUST be `0` |
| 6 | 2 | `st_shndx` | section index, or one of §4's special values |
| 8 | 8 | `st_value` | in `ET_REL`: the offset of the symbol from the start of section `st_shndx`, except for `SHN_COMMON` where it is the required **alignment**. In `ET_EXEC`/`ET_DYN`: the virtual address. |
| 16 | 8 | `st_size` | size in bytes of the object the symbol names; `0` if unknown or not applicable. For `SHN_COMMON`, the number of bytes to allocate. |

Accessors:

```
ELF64_ST_BIND(i)        = (i) >> 4
ELF64_ST_TYPE(i)        = (i) & 0xf
ELF64_ST_INFO(b,t)      = ((b) << 4) | ((t) & 0xf)
ELF64_ST_VISIBILITY(o)  = (o) & 0x3
```

Bindings: `STB_LOCAL = 0`, `STB_GLOBAL = 1`, `STB_WEAK = 2`,
`STB_LOOS = STB_GNU_UNIQUE = 10`, `STB_HIOS = 12`, `STB_LOPROC = 13`,
`STB_HIPROC = 15`.

Types: `STT_NOTYPE = 0`, `STT_OBJECT = 1`, `STT_FUNC = 2`, `STT_SECTION = 3`,
`STT_FILE = 4`, `STT_COMMON = 5`, `STT_TLS = 6`,
`STT_LOOS = STT_GNU_IFUNC = 10`, `STT_HIOS = 12`, `STT_LOPROC = 13`,
`STT_HIPROC = 15`.

Visibilities: `STV_DEFAULT = 0`, `STV_INTERNAL = 1`, `STV_HIDDEN = 2`,
`STV_PROTECTED = 3`.

Semantics of the bindings, types and visibilities, and of how a linker
combines conflicting definitions, are in `02-symbols-and-archives.md`.

### 6.1 Ordering requirement

All `STB_LOCAL` symbols MUST precede all non-local symbols in a symbol table,
and `sh_info` MUST equal the index of the first non-local symbol (§3.3). A
`STT_FILE` symbol, if present, MUST be `STB_LOCAL` with `st_shndx == SHN_ABS`
and MUST precede the other local symbols of the file it names.

### 6.2 `SHT_SYMTAB_SHNDX`

When a file has `≥ SHN_LORESERVE` sections, a symbol that lives in section
index `≥ 0xff00` cannot express it in the 16-bit `st_shndx`. Such a symbol
sets `st_shndx = SHN_XINDEX` and its real index is the 32-bit word at the same
entry index in the `SHT_SYMTAB_SHNDX` section whose `sh_link` names the symbol
table. The extended table has one `Elf64_Word` per symbol, including index 0.
A linker MUST read this table when it is present. It need only *write* one if
its own output has that many sections.

## 7. Relocation entries

`Elf64_Rela`, 24 bytes — the only form x86-64 uses in `ET_REL` inputs:

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 8 | `r_offset` | in `ET_REL`: byte offset from the start of the section this `SHT_RELA` applies to (its `sh_info`). In `ET_EXEC`/`ET_DYN`: the virtual address of the storage unit. |
| 8 | 8 | `r_info` | symbol index and relocation type, packed |
| 16 | 8 | `r_addend` | signed constant added to the computed value |

`Elf64_Rel`, 16 bytes — `r_offset` and `r_info` only; the addend is taken from
the bytes already present at `r_offset`, interpreted at the field width of the
relocation type.

Packing:

```
ELF64_R_SYM(i)    = (i) >> 32              /* index into the sh_link symbol table */
ELF64_R_TYPE(i)   = (i) & 0xffffffff       /* processor-specific type */
ELF64_R_INFO(s,t) = ((uint64_t)(s) << 32) | ((t) & 0xffffffff)
```

A symbol index of `0` means `STN_UNDEF` and denotes "no symbol"; the
relocation's `S` term is then `0`. A relocation whose `r_offset` plus the
field width exceeds the size of the target section is malformed and MUST be
diagnosed. The x86-64 type values and their arithmetic are in
`03-x86-64-relocations.md`.

A relocation section applies to the section named by its own `sh_info`. If
that section is discarded — by garbage collection, by group deduplication, or
because it was `SHF_EXCLUDE` — its relocations MUST be discarded with it and
MUST NOT be applied or emitted.

## 8. `SHT_GROUP` — section groups

A `SHT_GROUP` section holds an array of `Elf64_Word` (4 bytes each):

* **Entry 0 is a flag word.** The only defined bit is `GRP_COMDAT = 0x1`.
  `GRP_MASKOS = 0x0ff00000` and `GRP_MASKPROC = 0xf0000000` are reserved.
* **Entries 1…n are section header indices** of the group's members.

The group's *signature* is the name of the symbol at index `sh_info` in the
symbol table named by `sh_link`. Every member section MUST have `SHF_GROUP`
set in its `sh_flags`, and a section MUST belong to at most one group.

For `GRP_COMDAT`, the linker MUST keep exactly one group per signature across
all inputs — conventionally the first encountered in command-line order — and
MUST discard every member section of every later group with the same
signature, along with those sections' relocations and their local symbol
definitions. Discarding is unconditional: it happens before, and independently
of, garbage collection. See `04-layout-and-sections.md` §4.

The `SHT_GROUP` section itself is never copied to the output.

## 9. `Elf64_Phdr` — a program header

56 bytes. The program header table has `e_phnum` entries at `e_phoff`. It
exists only in `ET_EXEC` and `ET_DYN` files; a linker reads it only from
shared-object inputs and writes it for every executable output.

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `p_type` | §9.1 |
| 4 | 4 | `p_flags` | `PF_X = 0x1`, `PF_W = 0x2`, `PF_R = 0x4`; `PF_MASKOS = 0x0ff00000`, `PF_MASKPROC = 0xf0000000` |
| 8 | 8 | `p_offset` | file offset of the segment's first byte |
| 16 | 8 | `p_vaddr` | virtual address of the segment's first byte |
| 24 | 8 | `p_paddr` | physical address; unused on Linux. MUST be written equal to `p_vaddr`, except under a linker script that puts an explicit load address in force for the segment's contents — see `00-compatibility-profile.md` §5 and the linker-script document's `AT`. |
| 32 | 8 | `p_filesz` | bytes the segment occupies in the file; may be `0` |
| 40 | 8 | `p_memsz` | bytes the segment occupies in memory; `≥ p_filesz`. The excess is zero-filled. |
| 48 | 8 | `p_align` | `0` or `1` means no constraint. Otherwise a power of two, and `p_offset ≡ p_vaddr (mod p_align)` MUST hold. |

Note the field order: unlike `Elf32_Phdr`, `p_flags` comes **second**, right
after `p_type`, so that the 64-bit fields stay 8-byte aligned.

### 9.1 `p_type`

| value | name | what the linker emits |
| --- | --- | --- |
| `0` | `PT_NULL` | never |
| `1` | `PT_LOAD` | one per permission class; see `00-compatibility-profile.md` §2 and §5. `PT_LOAD` entries MUST appear in ascending `p_vaddr` order. |
| `2` | `PT_DYNAMIC` | covering `.dynamic`, in a dynamic output |
| `3` | `PT_INTERP` | covering `.interp`, in a dynamically linked executable. MUST precede any `PT_LOAD` if present. |
| `4` | `PT_NOTE` | covering the contiguous run of allocated `SHT_NOTE` sections |
| `5` | `PT_SHLIB` | never; reserved and undefined |
| `6` | `PT_PHDR` | optionally, covering the program header table itself. If present it MUST precede every `PT_LOAD` and the table it describes MUST be inside a `PT_LOAD`. The profile does not require it. |
| `7` | `PT_TLS` | covering `.tdata` then `.tbss`, when either is non-empty. `p_filesz` counts `.tdata` only; `p_memsz` counts both. |
| `0x6474e550` | `PT_GNU_EH_FRAME` | covering `.eh_frame_hdr` exactly, when that section exists |
| `0x6474e551` | `PT_GNU_STACK` | to declare stack permissions: `PF_R\|PF_W` for a non-executable stack, plus `PF_X` otherwise. `p_offset`, `p_vaddr`, `p_filesz`, `p_memsz` are `0`. Not required at the static milestone. |
| `0x6474e552` | `PT_GNU_RELRO` | covering the part of the writable segment to be made read-only after relocation. Introduced by a later milestone. |
| `0x6474e553` | `PT_GNU_PROPERTY` | covering `.note.gnu.property`. Introduced by a later milestone. |

Ranges: `PT_LOOS = 0x60000000` … `PT_HIOS = 0x6fffffff`,
`PT_LOPROC = 0x70000000` … `PT_HIPROC = 0x7fffffff`.

A segment other than `PT_LOAD` that describes file content MUST lie entirely
within some `PT_LOAD`, so the loader has the bytes mapped.

## 10. Note format (`SHT_NOTE`, `PT_NOTE`)

A note section is a sequence of entries. Each entry is:

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | `n_namesz` — length of `name` including its terminating NUL |
| 4 | 4 | `n_descsz` — length of `desc` in bytes |
| 8 | 4 | `n_type` — meaning is defined by the vendor named in `name` |
| 12 | `n_namesz` | `name`, NUL-terminated |
| … | pad | zero bytes to the next 4-byte boundary |
| … | `n_descsz` | `desc` |
| … | pad | zero bytes to the next 4-byte boundary |

The padding shown above is to **4 bytes** for the note types in the table
below, as every Linux toolchain and `llvm-readelf` implement them,
notwithstanding the gABI's text about 8-byte alignment for ELF64.

A note section's `sh_addralign` is **per section, and is not always 4**. It is
`4` for `.note.gnu.build-id` and `.note.ABI-tag`, whose entries pad to 4 as
shown. It is `8` for `.note.gnu.property` on a 64-bit target, whose entries pad
to 8 to match. A `PT_NOTE` segment's `p_align` MUST equal the maximum
`sh_addralign` of the sections it covers, so one `PT_NOTE` spanning both
classes is correct only if it is 8-aligned; the safe construction is one
`PT_NOTE` per alignment class. (`.note.gnu.property` is first emitted at
milestone 2, and from that milestone `06-debug-info-and-notes.md` §7 states
this rule in full and governs.)

Entries are read until the section is exhausted; a truncated entry is
malformed.

Note types a linker encounters, all with `name` = `"GNU"`:

| `n_type` | name | `n_descsz` | content |
| --- | --- | --- | --- |
| `1` | `NT_GNU_ABI_TAG` | `16` | four words: OS (`0` = Linux), then minimum kernel major, minor, sublevel |
| `2` | `NT_GNU_HWCAP` | var | hardware capabilities; pass through |
| `3` | `NT_GNU_BUILD_ID` | var | the build ID bytes, produced by `--build-id` |
| `4` | `NT_GNU_GOLD_VERSION` | var | producer string; pass through |
| `5` | `NT_GNU_PROPERTY_TYPE_0` | var | a sequence of properties; introduced by a later milestone |

`.note.GNU-stack` is special: it is a zero-length `SHT_PROGBITS` (not
`SHT_NOTE`) marker section whose `SHF_EXECINSTR` bit tells the linker whether
the producing object needs an executable stack. It MUST NOT be copied to the
output; its only effect is on `PT_GNU_STACK`.

Allocated note sections MUST be placed contiguously so that one `PT_NOTE` can
cover them, and MUST be within the first `PT_LOAD`. Non-allocated note
sections are copied without a segment, or discarded.
