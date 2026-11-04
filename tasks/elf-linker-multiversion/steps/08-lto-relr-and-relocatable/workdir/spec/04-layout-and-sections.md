# Layout: sections, segments and `.eh_frame`

> How input sections become output sections and output sections become loadable segments, including merge and COMDAT deduplication, garbage collection, and the `.eh_frame` / `.eh_frame_hdr` unwind tables.

## Provenance

Section-to-segment assignment, the `p_offset ≡ p_vaddr (mod p_align)`
congruence, the meaning of `SHF_ALLOC` and `SHT_NOBITS`, and the semantics of
`SHF_MERGE`, `SHF_STRINGS` and `SHT_GROUP` restate the object-file and
program-loading chapters of the **System V gABI**
(`https://www.sco.com/developers/gabi/latest/contents.html`).

`.eh_frame` is Call Frame Information in the format of the **DWARF 5**
standard (`https://dwarfstd.org/doc/DWARF5.pdf`, §6.4), as modified by the
**LSB Core Specification 5.0.0** §10.6 ("Exception Frames"), which defines the
`.eh_frame` augmentation strings, the `DW_EH_PE_*` pointer encodings and the
`.eh_frame_hdr` binary-search table. The specific header bytes this benchmark
emits were measured from the reference implementation `wild` 0.2.0
(`wild_lib/src/elf.rs`, `wild_lib/src/elf_writer.rs`).

Garbage collection has no primary source: `--gc-sections` is a GNU `ld`
extension documented informally in `ld.texi`. The root set given in
`00-compatibility-profile.md` §6 is this benchmark's own definition, derived
from the reference implementation's behaviour, and §5 below only restates how
reachability propagates from it.

Deliberately narrowed: linker scripts, `SECTIONS`/`PHDRS` commands and
orphan-section placement belong to a later milestone's document. Output
section *order* is fixed by `00-compatibility-profile.md` §2 and is not
repeated here; this document defines the rules that order must satisfy.

## 1. From input sections to output sections

Linking is, at bottom, concatenation. Each input section that survives §3–§5
is appended to exactly one output section, and every symbol and relocation
that referred to a byte of the input section is adjusted by the offset at
which the input section landed.

The output section an input section joins is chosen by **name** and, where the
name is unrecognised, by **flags and type**. The profile's mapping is:

| input section name | output section |
| --- | --- |
| `.text`, or any name beginning `.text` (`.text.foo`, `.text.unlikely`) | `.text` |
| any name beginning `.rodata` | `.rodata` |
| any name beginning `.data.rel.ro` (`.data.rel.ro.local` and friends) | `.data.rel.ro` |
| any name beginning `.data` | `.data` |
| any name beginning `.bss` | `.bss` |
| any name beginning `.tdata` | `.tdata` |
| any name beginning `.tbss` | `.tbss` |
| any name beginning `.init_array`, and any `.ctors`/`.ctors.*` | `.init_array` |
| any name beginning `.fini_array`, and any `.dtors`/`.dtors.*` | `.fini_array` |
| any name beginning `.gcc_except_table` | `.gcc_except_table` |
| exactly `.init` | `.init` |
| exactly `.fini` | `.fini` |
| exactly `.preinit_array` | `.preinit_array` |
| exactly `.comment` | `.comment` |
| exactly `.eh_frame` | `.eh_frame` (§7) |
| `.rela*`, `.rel*` | discarded — relocations are applied, not copied |
| `.symtab`, `.strtab`, `.shstrtab` | discarded — these are rebuilt |
| `.group` and any `SHT_GROUP` section | discarded (§4) |
| `.note.GNU-stack` | discarded; it is a marker only (`01-elf-container.md` §10) |
| anything else | a **custom** output section with exactly that name |

The prefix rule is what makes `-ffunction-sections` and `-fdata-sections`
work: the compiler emits one `.text.<fn>` per function and the linker folds
them back together, which is also what makes per-function garbage collection
possible.

A custom output section inherits its character from its inputs: it is
executable if any contributing section has `SHF_EXECINSTR`, writable if any
has `SHF_WRITE`, `SHT_NOBITS` if all contributing sections are `SHT_NOBITS`,
and it is placed among the read-only, executable or writable sections
accordingly (`00-compatibility-profile.md` §2). Two input sections with the
same name but incompatible `SHF_WRITE`/`SHF_EXECINSTR`/`SHF_ALLOC`/`SHF_TLS`
bits MUST NOT be combined; a linker MAY diagnose this or MAY emit them
separately, and no graded case constructs it.

For each custom output section whose name is a valid C identifier, the linker
MUST define `__start_<name>` at its first byte and `__stop_<name>` one byte
past its last, but only if something references them. These are `STB_GLOBAL`,
`STV_HIDDEN` and hold the encapsulation idiom together.

### 1.1 Order and alignment within an output section

Contributions are appended in **input order**: the order the objects appear on
the command line, with an archive's extracted members taking the archive's
position, and within one object the order of the section header table.
Nothing in the **default** layout is graded on this ordering, but it MUST be
deterministic — two runs over the same inputs MUST produce the same layout.
Under a `SECTIONS` script the order *is* graded, by the input-section order
rules of the linker-script document introduced at a later milestone; those
rules govern wherever a script is in force and this paragraph does not
override them.

Before appending a contribution, the linker MUST advance the current offset to
a multiple of that input section's `sh_addralign`, treating `0` as `1`, and
MUST fill the gap. The contribution then **occupies** `align_up(sh_size,
sh_addralign)` bytes of the output section — its size rounded up to a
multiple of its own alignment — so the next contribution is appended after
that reservation, not immediately after the contribution's bytes. The two
readings coincide whenever `sh_size` is already a multiple of `sh_addralign`,
which is the common case; when they differ, the reservation is the graded
one: two 8-byte inputs of a section with `sh_addralign` 16 leave that output
section **32** bytes long, not 24, because each contribution keeps a whole
16-byte slot of its own. Padding in an executable section SHOULD be `0x90` (`nop`)
or zero; padding elsewhere MUST be zero unless a `=FILL` expression is in
force under a linker script, in which case the gap takes that fill value
(the linker-script document introduced at a later milestone, §4). The output
section's own
`sh_addralign` MUST be at least the maximum of its contributions' alignments.
An input demanding more than the profile's ceiling of `0x8000`
(`00-compatibility-profile.md` §5) is **not** clamped: measured, the reference
rejects the whole link at `0.2.0`-`0.6.0` (`Unsupported alignment 0x10000`, no
artifact) and honours the alignment from `0.7.0`, where the output section is
placed at an address that is a multiple of the input's `sh_addralign` and the
`p_align` of the `PT_LOAD` that holds it rises to match, exceeding the ceiling.
§5 of the profile is the normative statement; this paragraph does not add to it.

That maximum is a floor, not an equality, and from `0.8.0` the reference
exceeds it for two sections: `.init_array` and `.fini_array` are emitted with
`sh_addralign` `8` even when every contribution has `sh_addralign` `1`.
Measured with a single align-1 `.init_array` input: the output alignment is `1`
at `0.2.0`-`0.7.0` and `8` at `0.8.0`-`0.10.0`. Do not grade output alignment
as *equal* to the input maximum for these two sections.

An output section with no contributions is normally omitted. The exceptions
are the sections the profile keeps unconditionally — the synthesised headers,
`.shstrtab` and `.bss` — which MUST be emitted even when empty.

## 2. `SHF_ALLOC`, and what `SHT_NOBITS` costs

`SHF_ALLOC` divides the output into two worlds that are laid out by different
rules.

**Allocated sections** (`SHF_ALLOC` set) exist at run time. They get a
non-zero `sh_addr`, they are covered by a `PT_LOAD` segment (§6), and their
file offsets are constrained by the congruence rule. `.text`, `.rodata`,
`.data`, `.bss`, `.got`, `.plt`, `.eh_frame`, `.eh_frame_hdr`, `.init_array`
and `.tdata` are allocated.

**Non-allocated sections** (`SHF_ALLOC` clear) exist only in the file. Their
`sh_addr` MUST be `0`, no segment covers them, and they may be placed at any
file offset that respects their `sh_addralign`. `.symtab`, `.strtab`,
`.shstrtab`, `.comment` and every `.debug_*` section are non-allocated.
Because nothing maps them, a linker is free to put them wherever is
convenient — the profile puts them early, which is legal and unusual.

`SHT_NOBITS` is orthogonal and concerns file space. **A `SHT_NOBITS` section
occupies zero bytes in the file.** Its `sh_size` is the number of bytes the
loader must provide, zero-filled, at `sh_addr`; its `sh_offset` is a
conceptual position that MUST NOT cause the linker to reserve `sh_size` bytes
of file. Concretely, for a program with a one-megabyte `.bss`, the linked
output MUST be far smaller than one megabyte — that size difference is the
observable property, and it is graded.

It follows that a `SHT_NOBITS` section MUST be placed at the **end** of the
segment that contains it, after every `SHT_PROGBITS` contribution, because
once file bytes stop the segment can only grow in memory. A writable segment
containing `.bss` therefore has `p_memsz - p_filesz >= sizeof(.bss)`. Two
`SHT_NOBITS` sections may follow one another; a `SHT_PROGBITS` section MUST
NOT follow a `SHT_NOBITS` one inside the same `PT_LOAD`.

`.tbss` is the special case: it is `SHT_NOBITS` *and* `SHF_TLS`, it occupies
no file space, and it also occupies no space in the enclosing `PT_LOAD`'s
memory image, because a TLS block is allocated per thread rather than mapped.
Its size counts towards `PT_TLS`'s `p_memsz` only.

## 3. `SHF_MERGE` — deduplicating constants and strings

A section marked `SHF_MERGE` (`0x10`) tells the linker its contents are an
array of independent elements that may be reordered and deduplicated, because
nothing refers to their positions except through relocations the linker can
rewrite. Two variants exist and `sh_entsize` means something different in
each.

**Fixed-size elements** — `SHF_MERGE` alone. `sh_entsize` MUST be non-zero and
MUST divide `sh_size` exactly; the section is exactly `sh_size / sh_entsize`
elements of `sh_entsize` bytes each. Duplicate elements across all inputs
that map to the same output section MAY be reduced to one copy. `.rodata.cst4`,
`.rodata.cst8` and `.rodata.cst16` are the common instances.

**Strings** — `SHF_MERGE | SHF_STRINGS` (`0x10 | 0x20`). `sh_entsize` is the
size of one character, which is `1` in everything a Linux toolchain produces;
a linker MAY reject any other value. The section is a sequence of
NUL-terminated strings and duplicates of the **same NUL-terminated string MUST
be reduced to one copy** so that two identical literals from separate
translation units resolve to the same address; `--no-string-merge` MUST
disable this and produce distinct copies. `.rodata.str1.1` is the common
instance.

The profile's policy, which a linker MUST implement:

* Merging is **on by default**. `--no-string-merge` disables it and MUST be
  accepted; `-O`, `-O1`, `-O2` do not change it.
* A **string** section is merged if and only if it has `SHF_MERGE`, has
  `SHF_STRINGS`, and has `sh_addralign <= 1`. A string section requesting
  greater alignment is copied whole rather than merged, because merging would
  not be able to honour the alignment of every string.
* Fixed-entry `SHF_MERGE` sections — `SHF_MERGE` *without* `SHF_STRINGS`, with
  element size `sh_entsize` — are a separate case, and the reference changes
  behaviour at `0.8.0`: it concatenates them up to `0.7.0` and deduplicates
  identical ones from `0.8.0`. See `00-compatibility-profile.md` §10 for the
  measured table and for what is graded at which milestone. The "if and only
  if" above is a rule about *string* merging and does not deny that folding.
  The `sh_addralign <= 1` cutoff governs this case too: an input section
  demanding stricter alignment is copied whole whether or not its bytes are
  identical, so two identical 8-byte entries at `sh_addralign` 16 stay two
  contributions and — by §1.1's reservation rule — occupy **32** bytes of the
  output section, where the same bytes at `sh_addralign` 1 fold to a single
  8-byte copy.
* **Tail merging is not required.** `"world"` occurring as the suffix of
  `"hello world"` MAY be stored separately; only whole-string equality is
  required to deduplicate. A linker that implements tail merging is not
  penalised.
* Merging MUST preserve semantics: every relocation that pointed into a merged
  section MUST be retargeted to the surviving copy of the element it pointed
  at. The addend matters — a relocation against a `STT_SECTION` symbol with
  addend `k` designates the element containing byte `k`, and the linker MUST
  map that byte to the corresponding byte of the surviving copy, not to
  offset `k` of the output section.
* A merge section that is only *partly* referenced, or referenced at an offset
  in the middle of an element, MUST still produce a correct result; a linker
  that cannot map such a reference MUST fall back to copying the section whole
  rather than produce a wrong address.
* The output section that receives merged data MUST carry the merge flags and
  the same `sh_entsize` as its contributions, so that a subsequent link of a
  relocatable output can merge again.
* Sections with different `sh_entsize`, different flags, or different names
  MUST NOT be merged with one another.

**Identical string literals MUST resolve to one address** (pointer equality
across objects) and `--no-string-merge` MUST cause them to differ — both are
graded at this step. What is *not* graded is tail merging: a linker MAY leave
`"tail"` as a separate copy even when it is a proper suffix of another retained
string. From the milestone that introduces LoongArch64 and non-string section
merging onward, the fixed-entry case is also graded on its measured result; see
`00-compatibility-profile.md` §10 and that milestone's `TEST_INTERFACE.md`.

## 4. Section groups and COMDAT deduplication

`SHT_GROUP` sections are described structurally in `01-elf-container.md` §8.
Their purpose is to let a compiler emit the same inline function, template
instantiation, virtual table or static-local guard variable into every
translation unit that needs it and have the linker keep exactly one.

The rule a linker MUST implement:

1. Read every `SHT_GROUP` section of every included object. Compute its
   signature: the name of the symbol at index `sh_info` of the symbol table
   named by `sh_link`.
2. If the group's flag word has `GRP_COMDAT` (`0x1`) set and a group with the
   same signature has already been kept, **discard every member section of
   this group**. Otherwise keep it and record the signature.
3. "Already kept" is decided in input order, so the first object presenting a
   given signature wins. A group in an archive member that is never extracted
   does not count.
4. Discarding is total. The member sections contribute no bytes, their
   relocations are not applied, their local symbols are dropped, and their
   `.eh_frame` FDEs go with them (§7). Global symbols defined in a discarded
   member are not definitions — resolution proceeds as though that object had
   not defined them, which is exactly why the surviving group's copy takes
   over (`02-symbols-and-archives.md` §2).
5. A `SHT_GROUP` section is itself never copied to the output, and `SHF_GROUP`
   MUST be cleared from the flags of any section that is copied.

Group membership and symbol resolution are two independent mechanisms that
happen to agree in practice: C++ COMDAT members define their global symbols
weakly, so even a linker that only discards duplicate groups and lets weak
resolution pick the survivor's definition produces a correct program. At tag
0.2.0 the reference implementation takes exactly that route. The graded
property is the observable one: linking two translation units that both
include a header defining an inline function MUST produce one definition of
that function, and calling it from either translation unit MUST reach the same
code.

Where a signature appears both in a `GRP_COMDAT` group and as a plain
non-group definition, the plain definition is an ordinary definition and the
usual resolution rules apply.

## 5. Garbage collection

`00-compatibility-profile.md` §6 fixes the policy — collection is on by
default, `--no-gc-sections` turns it off — and enumerates the root set. This
section defines what "reachable" means.

Reachability is computed over **input sections**, not over symbols and not
over output sections. Starting from the roots, mark a section `T` reachable
whenever some already-reachable section `S` contains a relocation whose
resolved symbol is defined in `T`. Repeat to a fixpoint. Relocations in
sections that are not (yet) reachable contribute nothing, which is the whole
point: an unreferenced function's calls do not keep its callees alive.

A relocation against an undefined symbol, an `SHN_ABS` symbol, or a symbol
that resolves to a synthesised section marks nothing. A relocation against a
symbol in a `SHF_MERGE` section marks that section if the linker traces merge
sections at all; the profile permits retaining all of them instead.

After the fixpoint, an unmarked input section contributes nothing to the
output: no bytes, no output-section size, no `.symtab` entries for symbols it
defined, no relocations, no FDE. Collection therefore happens **before**
addresses are assigned, and a collected section MUST NOT influence alignment
or size.

Collection MUST NOT make a link fail that would otherwise have succeeded, and
MUST NOT cause a still-referenced symbol to become undefined. If the analysis
would drop a section that a reachable relocation needs, the analysis is wrong.

## 6. Addresses, segments and congruence

Once the surviving input sections are known and grouped, the linker assigns
addresses and file offsets and builds the program header table.

### 6.1 The image base and the mapped headers

The image starts at the profile's base address — `0x400000` for a non-PIE
x86-64 executable, `0` for a PIE (`00-compatibility-profile.md` §5 states the
value for each other machine) — and the very first bytes of the image are the
**ELF file header followed immediately by the program header table**. Both MUST
lie inside the first `PT_LOAD` segment: the loader maps them, and
`__ehdr_start`, `AT_PHDR` in the auxiliary vector, and any `PT_PHDR` segment
all depend on them being addressable. Consequently the first `PT_LOAD` MUST
have `p_offset == 0`, and its `p_vaddr` MUST be the image base, so that file
offset `0` maps to the base address.

This creates a chicken-and-egg problem — the size of the program header table
depends on how many segments there are, which depends on the layout, which
depends on where the headers end. A linker MUST resolve it by counting the
segments before assigning offsets; guessing and patching afterwards is
acceptable only if the result satisfies every rule below.

The section header table has no such constraint: it is not loaded and
conventionally goes at the end of the file.

### 6.2 Building `PT_LOAD`

Allocated output sections are partitioned by their permissions, derived from
`sh_flags` exactly as `00-compatibility-profile.md` §4 states, and each part
becomes one `PT_LOAD`. For a static x86-64 executable the profile produces
exactly three, in this order of both file offset and virtual address:

| # | `p_flags` | contents |
| --- | --- | --- |
| 1 | `PF_R` (`4`) | the ELF header, the program headers, and every allocated read-only non-executable section |
| 2 | `PF_R\|PF_X` (`5`) | every allocated executable section |
| 3 | `PF_R\|PF_W` (`6`) | every allocated writable section, with the `SHT_NOBITS` ones last |

Rules that MUST hold for every `PT_LOAD`:

* `p_align` is the machine's page size, `0x1000` on x86-64
  (`00-compatibility-profile.md` §5).
* `p_offset ≡ p_vaddr (mod p_align)`. This is the congruence the loader
  depends on: it maps whole pages, so a byte's offset within its page must be
  the same in the file and in memory. A linker that assigns addresses and
  offsets independently will violate it; the standard technique is to advance
  the address to the next page boundary at each permission change while
  letting file offsets run on, or to advance both together.
* `p_filesz` is the number of file bytes the segment covers; `p_memsz` is
  `p_filesz` plus the `SHT_NOBITS` tail. `p_memsz >= p_filesz` always.
* `p_paddr == p_vaddr`, absent an explicit load address in force under a
  linker script. A script may give an output section a load address distinct
  from its virtual address (the linker-script document's `AT` construct), and
  the `PT_LOAD` that contains such a section then deviates from its `p_vaddr`:
  measured, the segment's `p_paddr` follows the load-address **offset** the
  script establishes, which is not the `AT` address verbatim. Read an LMA back
  through `LOADADDR()`, never through `p_paddr`. With no such construct in
  force the two fields are equal.
* `PT_LOAD` segments MUST appear in the program header table in ascending
  `p_vaddr` order and MUST NOT overlap in memory once rounded down and up to
  page boundaries.
* Every allocated section MUST lie wholly inside exactly one `PT_LOAD`, and
  its `sh_addr` MUST satisfy `sh_addr - p_vaddr == sh_offset - p_offset` for
  that segment (`SHT_NOBITS` sections excepted past the end of `p_filesz`).

Because `p_offset ≡ p_vaddr (mod 0x1000)` and the segments are page-aligned in
memory, consecutive segments normally either share a page in the file or are
padded apart; both are legal. Making the file size grow by a whole page per
segment is wasteful but not wrong.

### 6.3 The other segments

The non-`PT_LOAD` segments a static link emits are derived mechanically:

* `PT_GNU_EH_FRAME` covers `.eh_frame_hdr` exactly — same offset, same
  address, `p_filesz == p_memsz == sh_size` — with `p_flags = PF_R` and
  `p_align = 4` or `8`. Emitted whenever `.eh_frame_hdr` exists.
* `PT_TLS` covers `.tdata` followed by `.tbss`; `p_filesz` counts `.tdata`
  only, `p_memsz` counts both, `p_align` is the maximum alignment of either.
  Emitted only when at least one is non-empty.
* `PT_NOTE` covers the contiguous run of allocated `SHT_NOTE` sections.
* `PT_GNU_STACK` carries permissions and nothing else: all of `p_offset`,
  `p_vaddr`, `p_paddr`, `p_filesz`, `p_memsz` are `0`. `PF_R|PF_W` means a
  non-executable stack. Not required at the static milestone; if emitted it
  MUST NOT claim an executable stack unless an input asked for one via a
  `.note.GNU-stack` section with `SHF_EXECINSTR`.

Every one of these except `PT_GNU_STACK` describes bytes that MUST also lie
inside a `PT_LOAD`.

## 7. `.eh_frame` and `.eh_frame_hdr`

`.eh_frame` carries the unwind tables that C++ exceptions, `_Unwind_Backtrace`
and profilers use to walk the stack. It is an allocated, read-only
`SHT_PROGBITS` section, and it is the one input section a linker cannot treat
as an opaque blob, because its entries must be deduplicated and indexed.

### 7.1 Entry format

`.eh_frame` is a sequence of entries. Each begins with a 4-byte `length`
giving the number of bytes that **follow** the length field itself, so an
entry is `length + 4` bytes long. A `length` of `0xffffffff` introduces an
8-byte extended length; no Linux toolchain emits that, and a linker MAY reject
it. A `length` of `0` is a terminator, and a linker MUST NOT copy terminators
from inputs into the middle of its output `.eh_frame`.

The next 4 bytes discriminate the entry:

| field value | entry kind |
| --- | --- |
| `0` | a **CIE** — Common Information Entry; this field is the CIE id |
| non-zero | an **FDE** — Frame Description Entry; this field is a *CIE pointer*, the number of bytes backwards from the position of this field to the start of the CIE it uses |

Note that the CIE pointer is a **backward self-relative distance**, not a
section offset. It is therefore invalidated whenever an entry moves, and a
linker that reorders or deduplicates entries MUST recompute it.

A **CIE** continues with: a 1-byte version (`1` or `3`); a NUL-terminated
augmentation string; `code_alignment_factor` as ULEB128;
`data_alignment_factor` as SLEB128; `return_address_register`, one byte in
version 1 and ULEB128 in version 3; then, if the augmentation string begins
with `z`, a ULEB128 augmentation-data length followed by that many bytes; then
initial call-frame instructions padded with `DW_CFA_nop` (`0x00`) to the
entry's length.

Inside the augmentation data, the characters after the leading `z` say what is
present, in order: `R` contributes one byte giving the pointer encoding used
for FDE `pc_begin` fields; `P` contributes one encoding byte followed by a
personality routine pointer in that encoding; `L` contributes one byte giving
the encoding of the FDE's LSDA pointer; `S` marks a signal frame and
contributes no bytes. An augmentation string a linker does not understand
means it MUST NOT attempt to rewrite the entry.

An **FDE** continues with: `pc_begin`, the address of the first instruction
the entry describes, in the CIE's `R` encoding; `pc_range`, the number of
bytes of code covered, in the same format without the relative adjustment; an
augmentation-data block if the CIE's string began with `z`; then call-frame
instructions padded with `DW_CFA_nop`.

**`pc_begin` sits at byte offset 8 from the start of the FDE** — 4 bytes of
length plus 4 bytes of CIE pointer — and in a relocatable input it is the
target of a relocation. That relocation is what tells the linker which input
section the FDE describes, and it is the mechanism by which garbage collection
reaches `.eh_frame`: an FDE MUST be kept exactly when the section named by its
`pc_begin` relocation is kept, and MUST be dropped, silently and without
diagnostic, when that section is discarded by garbage collection or COMDAT
deduplication. An FDE whose `pc_begin` has no relocation cannot be attributed
and MUST be dropped or conservatively kept, but MUST NOT crash the linker.

### 7.2 Pointer encodings

One byte. The low nibble gives the format and the high nibble how to apply it.
`0xff` (`DW_EH_PE_omit`) means the pointer is absent.

| low nibble | name | format |
| --- | --- | --- |
| `0x00` | `DW_EH_PE_absptr` | target-width pointer, 8 bytes here |
| `0x01` | `DW_EH_PE_uleb128` | unsigned LEB128 |
| `0x02` | `DW_EH_PE_udata2` | unsigned 2 bytes |
| `0x03` | `DW_EH_PE_udata4` | unsigned 4 bytes |
| `0x04` | `DW_EH_PE_udata8` | unsigned 8 bytes |
| `0x09` | `DW_EH_PE_sleb128` | signed LEB128 |
| `0x0a` | `DW_EH_PE_sdata2` | signed 2 bytes |
| `0x0b` | `DW_EH_PE_sdata4` | signed 4 bytes |
| `0x0c` | `DW_EH_PE_sdata8` | signed 8 bytes |

| high nibble | name | the stored value is relative to |
| --- | --- | --- |
| `0x00` | `DW_EH_PE_absolute` | nothing; it is the value |
| `0x10` | `DW_EH_PE_pcrel` | the address of the field itself |
| `0x20` | `DW_EH_PE_textrel` | the start of the text segment |
| `0x30` | `DW_EH_PE_datarel` | the start of `.eh_frame_hdr` |
| `0x40` | `DW_EH_PE_funcrel` | the start of the enclosing function |
| `0x50` | `DW_EH_PE_aligned` | nothing, but the field is aligned to the pointer size |

`0x80` (`DW_EH_PE_indirect`) may be OR-ed in and means the decoded value is the
address of a slot holding the real pointer.

### 7.3 CIE deduplication

Every translation unit emits its own CIE, and they are almost always
identical. A linker MUST be able to keep one copy per distinct CIE, and MUST
then repoint every FDE's CIE pointer at the survivor.

Two CIEs are duplicates when their bytes are equal *and* the relocations
applying to them resolve to the same values — a personality routine pointer
inside a CIE is relocated, so byte equality alone is not sufficient for a CIE
whose augmentation string contains `P`. A linker MAY restrict deduplication to
CIEs with no relocations, or decline to deduplicate at all; nothing is graded
on the size of `.eh_frame`. What is graded is that the output's `.eh_frame` is
well-formed: every FDE's CIE pointer resolves, within the output section, to a
real CIE.

FDEs MUST be emitted in an order that keeps each CIE before the FDEs that
refer to it, since the CIE pointer is a backwards distance and cannot be
negative.

### 7.4 `.eh_frame_hdr` and `PT_GNU_EH_FRAME`

Walking `.eh_frame` linearly to find the FDE covering an address is
unacceptable at unwind time, so the linker builds a sorted index.
`.eh_frame_hdr` is an allocated, read-only section whose address is published
to the run time through a `PT_GNU_EH_FRAME` segment covering it exactly. The
unwinder finds that segment via `dl_iterate_phdr` and uses the table.

The header is 12 bytes:

| offset | size | field | value this profile writes |
| --- | --- | --- | --- |
| 0 | 1 | `version` | `1` |
| 1 | 1 | `eh_frame_ptr_enc` | `0x1b` = `DW_EH_PE_pcrel \| DW_EH_PE_sdata4` |
| 2 | 1 | `fde_count_enc` | `0x03` = `DW_EH_PE_absolute \| DW_EH_PE_udata4` |
| 3 | 1 | `table_enc` | `0x3b` = `DW_EH_PE_datarel \| DW_EH_PE_sdata4` |
| 4 | 4 | `eh_frame_ptr` | signed 32-bit, the address of `.eh_frame` minus the address of this field (i.e. minus `sh_addr` of `.eh_frame_hdr` plus 4) |
| 8 | 4 | `fde_count` | unsigned 32-bit, the number of entries in the table |

Then `fde_count` entries of 8 bytes each:

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | `initial_location` — signed 32-bit, the FDE's `pc_begin` **minus the address of `.eh_frame_hdr`**, because `table_enc` is `datarel` |
| 4 | 4 | `address` — signed 32-bit, the address of the FDE itself, likewise relative to `.eh_frame_hdr` |

Requirements:

* The array MUST be sorted by ascending `initial_location`. This is what makes
  the table binary-searchable and it is the only reason the section exists; an
  unsorted table produces an unwinder that silently fails to find frames.
  Sorting is by the *final* address, so it can only be done after layout.
* `fde_count` MUST equal the number of entries actually written, and MUST
  count only FDEs that survived garbage collection and deduplication. The
  table MUST list every surviving FDE, and MUST list each `initial_location`
  exactly once: if two surviving FDEs describe the same address, keep one.
  A grader can therefore compare the set of `initial_location` values against
  the set of FDE `pc_begin` values in `.eh_frame` and require them equal.
* Both relative fields are computed against the address of `.eh_frame_hdr`'s
  first byte, except `eh_frame_ptr`, which is `pcrel` and so is relative to
  its own position at `sh_addr + 4`. Getting these two bases confused is the
  classic `.eh_frame_hdr` bug.
* `.eh_frame_hdr` MUST be 4-byte aligned and MUST lie in a read-only
  `PT_LOAD`.
* When there are no FDEs at all, the profile still emits the 12-byte header
  with `fde_count == 0` and still emits `PT_GNU_EH_FRAME`. A linker MAY
  instead omit both; what it MUST NOT do is emit a header whose `fde_count`
  disagrees with the table.
* `--eh-frame-hdr` requests this section; passing it is REQUIRED to get
  `.eh_frame_hdr` and `PT_GNU_EH_FRAME` in the output (see `spec/00 §2` row 6).
  The profile does **not** emit them by default. `--no-eh-frame-hdr` MUST also be
  accepted; it is a no-op when the flag was not given.

`.gcc_except_table`, which holds the LSDA data the FDEs point at, is an
ordinary allocated read-only section: concatenate it and relocate it like any
other, with no special processing.
