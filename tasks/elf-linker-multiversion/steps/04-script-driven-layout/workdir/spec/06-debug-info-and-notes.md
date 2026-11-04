# Debug information and notes

> What a linker owes DWARF sections — relocation, tombstoning, string merging and compression — and how it combines the GNU note sections.

## Provenance

The DWARF half restates *DWARF Debugging Information Format* version 5
(<https://dwarfstd.org/dwarf5std.html>) only where the linker is involved:
section names, the `SHF_MERGE`/`SHF_STRINGS` string tables, and the
tombstone question, which is DWARF issue 200609.1
(<https://dwarfstd.org/issues/200609.1.html>) plus the de-facto behaviour of
GNU `ld`, `lld` and `gdb`. `SHF_COMPRESSED` and `Elf64_Chdr` are from the
System V gABI section-header chapter
(<https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.sheader.html>). The
notes half restates the GNU note conventions: the note header from the gABI
"Note Section" chapter, `NT_GNU_BUILD_ID` and `NT_GNU_ABI_TAG` from binutils'
`include/elf/common.h`, and the program-property format from the *Linux
Extensions to gABI* document
(<https://gitlab.com/x86-psABIs/Linux-ABI>) and the x86-64 psABI's
"Program Property" chapter.

Deliberately narrowed: nothing about *rewriting* DWARF is here. Building a
`.gdb_index` or `.debug_names`, splitting or recombining `.dwo` files,
rewriting `DW_AT_ranges` forms and `DWARF` package files are separate
milestones. `.eh_frame` and `.eh_frame_hdr` are unwinding data, not debug
data, and are covered by *Static linking*.

Two deviations from the primary sources that this document follows because the
reference implementation does:

1. **Tombstone values are 0 and 1**, not the −1 / −2 that the DWARF issue
   above proposes. The reference implementation carries an explicit note that
   the new values are premature because consumers do not handle them.
2. **`--compress-debug-sections` is not accepted** until a later release.
   Reading `SHF_COMPRESSED` *input* works from the first dynamic milestone;
   writing compressed *output* does not. Section 6 marks which is which.

## 1. Which sections are debug sections

Every input section whose name begins with `.debug_` is debug information.
The common ones and what they contain, as far as a linker cares:

| section | contains | relocated? |
| --- | --- | --- |
| `.debug_info` | the DIE tree | yes — many address-sized and offset-sized fields |
| `.debug_abbrev` | abbreviation tables | usually no relocations |
| `.debug_line` | the line-number program | yes — `DW_LNE_set_address` operands |
| `.debug_line_str` | strings for `.debug_line` (DWARF 5) | merged, not relocated |
| `.debug_str` | strings for `.debug_info` | merged, not relocated |
| `.debug_str_offsets` | offsets into `.debug_str` (DWARF 5) | section-relative |
| `.debug_loc` | location lists (DWARF ≤ 4) | yes — address pairs |
| `.debug_loclists` | location lists (DWARF 5) | yes |
| `.debug_ranges` | range lists (DWARF ≤ 4) | yes — address pairs |
| `.debug_rnglists` | range lists (DWARF 5) | yes |
| `.debug_addr` | the address pool (DWARF 5) | yes |
| `.debug_frame` | call-frame information | yes |
| `.debug_aranges` | address-range accelerator | yes |
| `.debug_pubnames`, `.debug_pubtypes` | name accelerators (DWARF ≤ 4) | section-relative |
| `.debug_macro`, `.debug_macinfo` | macro information | mostly section-relative |
| `.debug_types` | type units (DWARF 4) | yes |

None of these carries `SHF_ALLOC`. They MUST therefore be written to the
output file but MUST NOT be assigned a virtual address, MUST NOT appear in any
`PT_LOAD` segment, and MUST NOT influence the address assignment of allocated
sections. Their `sh_addr` MUST be 0. A `.debug_*` section is not a candidate
for garbage collection: it reaches the output whether or not anything
references it, and only the strip options below remove one.

Input debug sections with the same name are concatenated in input order, in
the same way as any other non-allocated section. Their `sh_addralign` is
preserved by aligning each contribution.

`--strip-debug` (`-S`) drops every `.debug_*` section from the output.
`--strip-all` (`-s`) drops `.debug_*`, `.symtab` and `.strtab`. Both MUST be
accepted with either dash spelling.

## 2. Relocating debug sections

Relocations targeting a debug section are applied exactly like any other
relocation, with three differences:

1. Only two relocation kinds are meaningful: **absolute** (`R_X86_64_64`,
   `R_X86_64_32`, `R_X86_64_32S`, `R_AARCH64_ABS64`, `R_AARCH64_ABS32`), which
   store `S + A`, and **DTP-offset** (`R_X86_64_DTPOFF64`,
   `R_X86_64_DTPOFF32`, `R_AARCH64_TLSLD_*`), which store the target's offset
   within its module's TLS block so a debugger can find a thread-local. Any
   other kind in a debug section MUST be a diagnosed error.
2. The result is written into the file but nothing is ever emitted to
   `.rela.dyn`: a non-allocated section cannot be relocated at run time.
   In particular, a debug reference to a symbol that would be preemptible at
   run time is resolved statically to the local definition, not turned into a
   `GLOB_DAT`.
3. When the target no longer exists, the tombstone rule of section 3 applies
   instead of an error.

The width written is the relocation's own field width — 8 bytes for
`R_X86_64_64`, 4 for `R_X86_64_32`. A relocation whose field would run past
the end of the section MUST be a diagnosed error.

## 3. Tombstones

A relocation in a debug section may name a symbol in a section that no longer
exists in the output, because `--gc-sections` collected it, because it was a
COMDAT duplicate that lost, or because `/DISCARD/` removed it. There is no
address to write. The linker MUST NOT fail; it MUST write a **tombstone** — a
value the debug-info consumer will recognise as "this entry describes code
that is not here".

| target section | tombstone value |
| --- | --- |
| `.debug_loc` | **1** |
| `.debug_ranges` | **1** |
| every other `.debug_*` section | **0** |

The value is written at the relocation's own width (so 4 bytes of value 0 for
a 32-bit field, 8 for a 64-bit one). The addend is discarded: the tombstone
replaces the whole computation, it is not added to.

**Why the two sections differ.** `.debug_loc` and `.debug_ranges` are lists of
address *pairs*, and the list is terminated by a pair of zeros. Writing 0 into
a discarded entry of such a list would forge an end-of-list marker in the
middle of it, silently truncating every remaining entry — including entries
that describe live code. Writing 1 into both halves produces the degenerate
half-open interval `[1, 1)`, which is empty, is not the terminator, and is
skipped by every consumer. Sections without a zero-terminated list convention
have no such hazard, so 0 — the most obviously invalid address — is used.

DWARF issue 200609.1 later standardised −1 (all bits set) as the tombstone
for most sections and −2 for the `.debug_loc`/`.debug_ranges` base-address
case. Emitting those is a legal alternative but is *not* what this task
expects: consumers in the field do not all handle them, and the reference
implementation writes 0 and 1. Nothing grades the tombstone in
`.debug_loc`/`.debug_ranges` specifically, because the reference
implementation's name comparison for those two sections is misspelled before
its 0.5.0 release and so writes 0 there as well; a linker SHOULD write 1 and
MUST write 0 everywhere else.

A tombstone applies only when the *target section* is gone. An undefined
symbol referenced from a debug section is still an undefined-symbol error
under the usual rules.

## 4. String merging

`.debug_str` and `.debug_line_str` are emitted by compilers as
`SHF_MERGE | SHF_STRINGS` with `sh_entsize` 1 and `sh_addralign` 1. That flag
pair is a promise from the producer: the section is a concatenation of
NUL-terminated strings, every reference to it names a string start, and the
linker may deduplicate freely.

A linker MUST merge a section when all of the following hold:

- `SHF_MERGE` is set, and
- `SHF_STRINGS` is set, and
- the section's alignment is at most 1.

(Sections with `SHF_MERGE` but not `SHF_STRINGS` hold fixed-size entries of
`sh_entsize` bytes and may be merged by exact entry comparison; that is
optional. A `SHF_MERGE|SHF_STRINGS` section with alignment greater than 1
MAY be treated as unmergeable and simply concatenated — in that case, and
only that case, concatenating is still correct output.)

Merging is by exact byte content including the terminating NUL. Tail merging
— storing `"bar"` as a suffix of `"foobar"` — is permitted but not required.
The output section keeps `SHF_MERGE | SHF_STRINGS` and `sh_entsize` 1.

Relocations against a merged section need special resolution. In an input
object a reference to a string is usually encoded as a relocation against the
*section symbol* with an addend equal to the string's offset in the input
section. After merging, that offset is meaningless. The linker MUST map
`(input section, input offset)` to the offset of the same string in the merged
output section and use that. An addend that does not land on a string start
MUST be handled by finding the string that contains it and adding the residual
offset.

Because `.debug_str` is not allocated, the "address" of a merged string is
just its offset in the output section. The same machinery serves both
allocated merge sections (`.rodata.str1.1`) and debug ones.

`--no-string-merge` disables the whole pass; the output is then a plain
concatenation and remains correct.

### 4.1 Dropping merged debug strings early

When `--strip-debug` or `--strip-all` is in effect, a `.debug_*` section will
not be in the output, so the linker MUST NOT spend the string-merge pass on
it. The decision has to be made at the point where an input section is first
classified — before string-merge state is built for it and before it is
assigned an output section — not after merging. Recognising the name prefix
`.debug_` and discarding the section there is what makes `--strip-debug`
cheap; discarding it at write time instead produces the same file but pays for
merging a table nobody will read.

## 5. Compressed input sections

An input section may carry `SHF_COMPRESSED` (`0x800`). Its contents are then a
compression header followed by the compressed stream, and `sh_size` is the
size of *that*, not of the decompressed data. `sh_addralign` describes the
decompressed data's alignment requirement.

`Elf64_Chdr`, 24 bytes, at offset 0 of the section contents:

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `ch_type` | compression algorithm |
| 4 | 4 | `ch_reserved` | MUST be 0 |
| 8 | 8 | `ch_size` | size of the decompressed data in bytes |
| 16 | 8 | `ch_addralign` | required alignment of the decompressed data |

(`Elf32_Chdr` is 12 bytes with no reserved word: `ch_type`, `ch_size`,
`ch_addralign`, each 4 bytes. This task produces 64-bit output only.)

| `ch_type` | value | algorithm |
| --- | --- | --- |
| `ELFCOMPRESS_ZLIB` | 1 | zlib / DEFLATE, RFC 1950 (a zlib stream, with its 2-byte header, not raw DEFLATE) |
| `ELFCOMPRESS_ZSTD` | 2 | Zstandard, RFC 8878 |
| `ELFCOMPRESS_LOOS` | `0x60000000` | start of the OS-specific range |
| `ELFCOMPRESS_HIOS` | `0x6fffffff` | end of it |
| `ELFCOMPRESS_LOPROC` | `0x70000000` | start of the processor-specific range |
| `ELFCOMPRESS_HIPROC` | `0x7fffffff` | end of it |

A linker MUST decompress such a section before doing anything else with it —
before string merging, before applying relocations, before computing its
contribution to the output size. The decompressed size is exactly `ch_size`;
a stream that produces fewer or more bytes MUST be a diagnosed error. An
unrecognised `ch_type` MUST be a diagnosed error naming the value.

The output section that receives a decompressed contribution MUST NOT carry
`SHF_COMPRESSED` unless the linker is recompressing it (section 6), and its
`sh_size` MUST be the decompressed size.

Relocation offsets in the accompanying `.rela` section are offsets into the
**decompressed** bytes — the compressed stream has no relocatable structure.
A relocation whose `r_offset` is *n* therefore patches byte *n* of the
inflated contents, and the value it stores must be the same one it would have
stored had the input never been compressed.

The older convention of a section literally named `.zdebug_info` whose
contents begin with the ASCII bytes `ZLIB` followed by a big-endian 8-byte
size is obsolete. A linker MAY support it; nothing in this task requires it.

## 6. Compressed output

`--compress-debug-sections=FORMAT` requests that `.debug_*` output sections be
written compressed, with `FORMAT` one of `none`, `zlib` and `zstd`. Any other
value MUST be diagnosed. (`zlib-gabi` is a GNU `ld` synonym of `zlib`; the
reference rejects it, and nothing here grades either behaviour.) When compression is applied to a section, the linker MUST:

- set `SHF_COMPRESSED` in the output section's `sh_flags`;
- write an `Elf64_Chdr` with the algorithm, the uncompressed size in
  `ch_size`, and the uncompressed data's alignment in `ch_addralign`;
- set the output `sh_size` to the compressed size including the 24-byte
  header, and `sh_addralign` to 1 for the compressed bytes;
- apply all relocations *before* compressing, never after.

A section MUST be left uncompressed if compressing would not make it smaller,
and `SHF_COMPRESSED` MUST NOT be set in that case. `none` disables the whole
feature.

This option is introduced by a later milestone. Before that milestone a linker
is not required to accept it, and the reference implementation rejects it as
an unrecognised argument — which is the correct behaviour for an option it
does not implement, since an unrecognised option must be a diagnosed error
rather than a silent success.

## 7. The note format

Every note section (`SHT_NOTE`) is a sequence of notes. Each note is:

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `n_namesz` | length of the name **including** its NUL |
| 4 | 4 | `n_descsz` | length of the descriptor in bytes |
| 8 | 4 | `n_type` | note type, interpreted relative to the name |
| 12 | `n_namesz` | name | NUL-terminated owner string, e.g. `GNU\0` |
| … | pad | | to a multiple of 4 |
| … | `n_descsz` | desc | the payload |
| … | pad | | to a multiple of 4 |

In ELF64 the gABI says the padding is to 8 bytes, but in practice — and in
every GNU-produced note except `.note.gnu.property` — it is 4. A linker MUST
pad name and descriptor to 4 bytes for ordinary notes, and MUST use the
section's own `sh_addralign` to decide: a note section with `sh_addralign` 8
pads to 8, one with `sh_addralign` 4 pads to 4. The note headers themselves
are always three 4-byte words in this order — `n_namesz`, `n_descsz`,
`n_type` — regardless of the padding width. `.note.gnu.property` on a
64-bit target has `sh_addralign` 8; `.note.gnu.build-id` and `.note.ABI-tag`
have `sh_addralign` 4.

Allocated note sections MUST be covered by a `PT_NOTE` program header. If
several allocated note sections with different alignments exist, they MUST NOT
share one `PT_NOTE` unless the resulting `p_align` is correct for all of them;
the safe construction is one `PT_NOTE` per alignment class. `p_align` MUST
equal the maximum `sh_addralign` of the sections it covers. `p_filesz` MUST
equal `p_memsz`: notes are never zero-filled. Taken together the `PT_NOTE`
headers MUST cover every byte of every allocated note section and no other
bytes, so that summing their `p_filesz` gives exactly the total size of the
allocated note sections. `PT_NOTE` is not loadable by itself, so the sections
it covers MUST also be inside a `PT_LOAD`. An output containing no `SHT_NOTE`
section MUST NOT carry a `PT_NOTE` header at all: the header exists only to
cover note content.

Non-allocated note sections (`.note.stapsdt` and friends) get no address, and
the `p_filesz` sum above is stated over *allocated* note sections only. Whether
such a section reaches the output at all — and whether a `PT_NOTE` covers it if
it does — changed across the release range; §11 gives the measured contract.

## 8. `.note.gnu.build-id`

`--build-id[=STYLE]` requests a build ID: a `SHT_NOTE` section named
`.note.gnu.build-id`, `SHF_ALLOC`, `sh_addralign` 4, containing exactly one
note with name `GNU` (`n_namesz` 4) and `n_type = NT_GNU_BUILD_ID` (**3**).
The descriptor is the ID bytes, so the section is
`12 + 4 + round_up(n_descsz, 4)` bytes long.

| style | descriptor |
| --- | --- |
| `none` | no note at all; the section is not emitted |
| `fast` | a fast hash of the output contents |
| `md5` | conventionally a 16-byte MD5 of the output |
| `sha1` | conventionally a 20-byte SHA-1 of the output |
| `uuid` | 16 random bytes |
| `0x<hex>` | exactly those bytes, taken literally |

`--build-id` with no `=STYLE` means `fast`. **No build ID is emitted unless
the option is given** — the default is `none`.

The one property that matters is *stability*: two links of identical inputs
with identical options MUST produce the same ID, and a link whose output
differs SHOULD produce a different one. The exact digest is not graded, and
the interface explicitly excludes build-id computation from byte-identity
requirements — so `md5` and `sha1` MAY be satisfied with any stable hash of
whatever length the linker prefers, and the reference implementation answers
all three of `fast`, `md5` and `sha1` with the same 32-byte hash. `0x<hex>`
is the exception: the descriptor MUST be exactly the decoded bytes, and
`n_descsz` MUST be their count. An odd-length or non-hex value MUST be a
diagnosed error, as MUST an unrecognised style name.

Because the note lives inside the file being hashed, a linker computing a
content hash MUST lay the section out first, hash the image with the
descriptor bytes in a defined state, and then overwrite them.

## 9. `.note.ABI-tag`

`.note.ABI-tag` is produced by C runtimes, not by the linker. Its single note
has name `GNU`, `n_type = NT_GNU_ABI_TAG` (**1**) and a 16-byte descriptor of
four 4-byte words: an OS identifier (`ELF_NOTE_OS_LINUX` = 0,
`ELF_NOTE_OS_GNU` = 1, `ELF_NOTE_OS_SOLARIS2` = 2, `ELF_NOTE_OS_FREEBSD` = 3)
followed by the minimum required kernel major, minor and sub-minor version.

A linker MUST pass it through: copy it to an output section named
`.note.ABI-tag` with `SHF_ALLOC` and `sh_addralign` 4, and cover it with a
`PT_NOTE`. Merging several identical copies into one is permitted; combining
non-identical ones is not required.

## 10. `.note.gnu.property`

`.note.gnu.property` is a single note with name `GNU`,
`n_type = NT_GNU_PROPERTY_TYPE_0` (**5**), whose descriptor is an array of
**program properties**. On a 64-bit target the section's `sh_addralign` is 8
and each property is padded to 8 bytes, which for the property types in use
means each entry occupies exactly 16 bytes:

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `pr_type` | which property |
| 4 | 4 | `pr_datasz` | size of `pr_data` in bytes |
| 8 | `pr_datasz` | `pr_data` | the value |
| … | pad | | to a multiple of 8 |

Properties MUST be written in ascending `pr_type` order.

| `pr_type` | value | contents |
| --- | --- | --- |
| `GNU_PROPERTY_STACK_SIZE` | 1 | required stack size, pointer-sized |
| `GNU_PROPERTY_NO_COPY_ON_PROTECTED` | 2 | `pr_datasz` 0; no copy relocations against protected symbols |
| `GNU_PROPERTY_X86_FEATURE_1_AND` | `0xc0000002` | 4-byte bitmap: `IBT` = 0x1, `SHSTK` = 0x2, `LAM_U48` = 0x4, `LAM_U57` = 0x8 |
| `GNU_PROPERTY_AARCH64_FEATURE_1_AND` | `0xc0000000` | 4-byte bitmap: `BTI` = 0x1, `PAC` = 0x2, `GCS` = 0x4 |

The `pr_type` value also encodes how the property combines, by range:

| range | class | combining rule |
| --- | --- | --- |
| `GNU_PROPERTY_UINT32_AND_LO` = `0xb0000000` … `GNU_PROPERTY_UINT32_AND_HI` = `0xb0007fff` | And | bitwise AND |
| `GNU_PROPERTY_UINT32_OR_LO` = `0xb0008000` … `GNU_PROPERTY_UINT32_OR_HI` = `0xb000ffff` | Or | bitwise OR |
| `GNU_PROPERTY_X86_UINT32_AND_LO` = `0xc0000002` … `GNU_PROPERTY_X86_UINT32_AND_HI` = `0xc0007fff` | And | bitwise AND |
| `GNU_PROPERTY_X86_UINT32_OR_LO` = `0xc0008000` … `GNU_PROPERTY_X86_UINT32_OR_HI` = `0xc000ffff` | Or | bitwise OR |
| `GNU_PROPERTY_X86_UINT32_OR_AND_LO` = `0xc0010000` … `GNU_PROPERTY_X86_UINT32_OR_AND_HI` = `0xc0017fff` | AndOr | OR the values, but keep the property only if every file had it |
| `GNU_PROPERTY_AARCH64_FEATURE_1_AND` = `0xc0000000` | And | bitwise AND |

### 10.1 The combining rule

Combining happens in two stages, and getting the order wrong changes the
answer:

1. **Within one input file**, a property that appears more than once — because
   the file has several `.note.gnu.property` sections, or one section with
   repeated `pr_type` values — is combined by **OR**ing the values together.
   This is a union: the file uses every feature any of its parts declares.
2. **Across input files**, an `And`-class property is combined by **AND**ing
   the per-file values. A feature bit survives only if *every* relocatable
   input declared it. An `Or`-class property is ORed across files instead.

Then, for emission:

| class | emit the property iff |
| --- | --- |
| Or | the combined value is non-zero |
| And | every input file contained this `pr_type` **and** the combined value is non-zero |
| AndOr | every input file contained this `pr_type` (the value may be zero) |

The consequence that surprises people is correct and MUST be preserved: if a
single input object — a hand-written assembly stub, say — lacks
`.note.gnu.property` entirely, then `GNU_PROPERTY_X86_FEATURE_1_AND` is
absent from the output and the image is not marked IBT-enabled. That is the
point of an And-class property: the guarantee is only as strong as the weakest
input. A linker MUST NOT "helpfully" carry the note through in that case.

Shared-object inputs do not participate: only relocatable objects contribute
properties and only they count toward "every input file".

The output note MUST be a single `.note.gnu.property` section,
`SHF_ALLOC`, `sh_addralign` 8, containing one note with `n_namesz` 4,
`n_descsz` equal to 16 times the number of emitted properties, and
`n_type` 5. If nothing survives combining, the section MUST NOT be emitted.

`-z ibt`, `-z shstk` and `-z force-bti` force the corresponding bit on
regardless of the inputs; `-z cet-report=warning|error` and
`-z bti-report=...` control diagnosing inputs that lack it. All of these MUST
be accepted; none is required to do anything at this milestone.

## 11. `.note.stapsdt` and other non-allocated notes

`.note.stapsdt` holds SystemTap probe descriptors. It is `SHT_NOTE` and
**not** `SHF_ALLOC`. Like any other non-allocated section, it MUST keep
`sh_addr` 0 and `sh_addralign` 4 if it is emitted at all. Its contents contain
relocatable addresses — the probe location, the semaphore address and a base
address — which MUST be relocated normally.

Whether such a section is emitted at all changed during the release range, and
the change is graded. Measured at every tag with default options: through
milestone 5 (`0.6.0`) the reference drops a non-allocated note from the output;
**from milestone 6 (`0.7.0`) it keeps it**, at `sh_addr` 0 with `sh_addralign`
4. Milestone 6 grades the kept shape — the section's presence, its address, its
alignment, its note framing and its relocated probe address, including under
`--gc-sections` — so a submission at milestone 6 or later MUST emit it. No
milestone before 6 asserts either way, and dropping it there is conforming.

Whether a `PT_NOTE` covers a non-allocated note is **deliberately left open at
every milestone**. The reference does emit one from `0.7.0`, at virtual address
0, but no case asserts its presence or its absence, so either is conforming.

Note sections are subject to `--gc-sections` like anything else, but which
notes survive it changed at the milestone that introduces `.symver` and the
Illumos flavour. Garbage collection is on by default across the whole range.
Measured at every release tag with an otherwise unreferenced note section:
through milestone 5 an input `SHT_NOTE` section is collected unless the linker
recognises it by name — `.note.ABI-tag` survives, `.note.stapsdt` and an
unknown `.note.*` do not, whether or not they are `SHF_ALLOC` — so at those
milestones a probe attached to a garbage-collected function goes with it.
**From milestone 6 (`0.7.0`) every `SHT_NOTE` input section is itself a
garbage-collection root**, extending the root set of
`00-compatibility-profile.md` §6 from that milestone on: the note survives on
its own, and a section reached only by one of the note's relocations is
retained with it, so the probe site stays in the output even when nothing
allocated refers to it. A linker MUST NOT special-case the name in either
direction from milestone 6 on: what decides is the section type, not the
string `.note.stapsdt`.

`--no-gc-sections` is **not** a reliable way to keep a non-allocated note
before milestone 6, and no case requires it to be. Measured: the option does
not exist at milestone 1; it keeps the note at milestones 2 and 3; and at
milestones 4 and 5 it makes the link *fail outright* on an input carrying one
(`Missing memory offset for section N (.note.stapsdt) present in a program
segment`) — an upstream defect that milestone 6 fixes along with the root-set
change. From milestone 6 on the note survives in every mode: with
`--gc-sections`, with `--no-gc-sections`, and with neither. A submission is
graded only on the milestone-6-and-later behaviour; before that, any of these
outcomes is conforming.

Unknown note sections are handled by the same general rule and MUST NOT
provoke an error: a note whose owner and type the linker does not recognise is
just bytes to be concatenated.
