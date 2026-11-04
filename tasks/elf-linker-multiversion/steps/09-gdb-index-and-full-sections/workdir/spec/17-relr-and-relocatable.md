# RELR-packed relocations and relocatable (`-r`) output

> Encoding relative relocations as a RELR bitmap, and producing an `ET_REL` partial object that can be linked again.

## Provenance

Restated from the reference at tags `0.9.0` (step 8) and `0.10.0` (step 9):
`libwild/src/elf.rs` (`RelrBitmap`, `RelrEncoder`, `relr_bitmap_slots`,
`is_got_relr_eligible`, `requires_symtab_shndx`, `lookup_for_partial_link`,
`will_emit_section_symbol_for_partial_objects`), `libwild/src/elf_writer.rs`
(`populate_file_header`, `write_section_headers`, `write_rela_sections`,
`build_sym_index_map`, `write_section_symbols`, the dynamic-entry table),
`libwild/src/compression.rs`, `libwild/src/args/elf.rs`. Numeric constants come
from the `object` 0.40 crate the reference depends on. Every RELR byte sequence
and `-r` claim below was reproduced here with the reference binary; §4.6 is a
dump of one such run.

Divergences the implementation makes normative:

1. **The option is `--pack-dyn-relocs=VALUE`**, not `--pack-dynamic-relocs=`.
   The latter spelling is rejected by the reference as unrecognised at every
   tag, and no case passes either spelling of it. Accept `--pack-dyn-relocs=relr` and
   `-z pack-relative-relocs`; also accepting the longer spelling is harmless and
   is the safe choice. §3.
2. **`-r` discards `SHT_GROUP` sections entirely.** `.group` is on the same
   discard list as `.symtab`, `.strtab`, `.shstrtab`, `.symtab_shndx`. Measured
   on partial output: `readelf -g` reports no section groups, and `SHF_GROUP`
   appears on no output section. COMDAT elimination happens at partial-link time
   rather than being deferred. §6.3.
3. **`--pack-dyn-relocs=android` and `android+relr` are accepted with a
   warning** that Android packed relocations are unsupported; only the RELR half
   of `android+relr` takes effect.
4. Diagnostics: a failure MUST exit non-zero with at least one stderr line
   naming what went wrong. No prefix, wording or exit value is graded.

Not verified here: RELR output consumed by a loader older than glibc 2.36 (none
installed).

# Part A — RELR

## 1. Why

A PIE or shared object holds one `R_X86_64_RELATIVE` entry per absolute pointer
needing the load bias. Each is a 24-byte `Elf64_Rela` whose symbol index is 0,
whose type is constant, and whose addend the loader recomputes anyway; in a
large C++ binary these dominate `.rela.dyn`. RELR replaces that array with a
run-length bitmap, collapsing densely packed ordered entries — vtables,
`.data.rel.ro`, GOT slots — from 24 bytes each to roughly one bit each. The
loader's whole job becomes: for each flagged 8-byte slot, add the load bias to
the word already stored there. Nothing else is encoded, which is why only
*relative* relocations with an implicit addend can be packed.

## 2. Container constants

| thing | value |
| --- | --- |
| section name | `.relr.dyn` |
| section type | `SHT_RELR` = **19** |
| section flags | `SHF_ALLOC`; alignment 8; `sh_entsize` 8 |
| entry size on ELF64 | 8 bytes |
| `DT_RELR` | **36** — address of `.relr.dyn` |
| `DT_RELRSZ` | **35** — size of `.relr.dyn` in bytes |
| `DT_RELRENT` | **37** — bytes per entry, 8 |
| Android section type | `SHT_ANDROID_RELR` = **0x6fff_ff00** |
| `DT_ANDROID_RELR` | **0x6fff_e000** |
| `DT_ANDROID_RELRSZ` | **0x6fff_e001** |
| `DT_ANDROID_RELRENT` | **0x6fff_e003** |

`DT_RELRSZ` is numerically *below* `DT_RELR`; not a typo. There is no
`0x6fff_e002` in the Android set — the three tags are 0, 1 and 3.

`--use-android-relr-tags` switches **both** the dynamic tags and the section
type to the Android spellings; `--no-use-android-relr-tags` switches back. Emit
exactly one of the two triples, never both. Measured: with the flag `readelf -d`
shows `0x6fffe000 / 0x6fffe001 / 0x6fffe003` and the section type reads
`LOOS+0xfffff00`; without it, `RELR / RELRSZ / RELRENT` and type `RELR`.

## 3. Enabling

| spelling | effect |
| --- | --- |
| `--pack-dyn-relocs=none` | off (default) |
| `--pack-dyn-relocs=relr` | on |
| `--pack-dyn-relocs=android` | warn "unsupported", stays off |
| `--pack-dyn-relocs=android+relr` | warn "unsupported", RELR on |
| `-z pack-relative-relocs` | on |
| `-z nopack-relative-relocs` | off |
| any other value | MUST be diagnosed as an unsupported option |

When RELR is enabled and the link references the symbol `GLIBC_ABI_DT_RELR`
from a shared object, mark that symbol's version as needed so the resulting
`DT_VERNEED` records the dependency. That is how a glibc-linked binary refuses
to start on a loader too old to understand `DT_RELR` instead of silently running
with unrelocated pointers.

## 4. Encoding

### 4.1 The rule

`.relr.dyn` is an array of 64-bit words. A word is read as:

- **even** (bit 0 clear) — an *address*: it is itself relocated, and it becomes
  the base for any bitmap words that follow.
- **odd** (bit 0 set) — a *bitmap*. Bits 1..63 each cover one 8-byte slot; bit
  `j` means "relocate the word at `window_base + (j-1)*8`". Bit 0 is only the
  odd-marker and never selects a slot.

One bitmap word covers 63 slots, i.e. `63 * 8 = 504` bytes on ELF64. In general,
with entry size `E`, a bitmap covers `E * 8 - 1` slots spanning `(E*8-1) * E`
bytes. After an address entry at `A` the first window starts at `A + 8`; after a
window starting at `W` the next consecutive window starts at `W + 504`.

### 4.2 Decoder

```
addresses = []; window_base = 0
for word in relr_entries:
    if word & 1 == 0:
        addresses.append(word); window_base = word + 8
    else:
        for j in 1..=63:
            if (word >> j) & 1: addresses.append(window_base + (j-1)*8)
        window_base += 63 * 8
```

The reference sets bits as `encoded |= 1 << (offset / 8 + 1)` with
`offset = address - window_base`, the same mapping read the other way.

### 4.3 Encoder

Feed addresses in **ascending order**. Three states; `NEW` appends a word,
`UPDATE` rewrites the word just appended. `window_after(a)` is the half-open
range `a+8 .. a+8+504` with `encoded = 1`; `window.next()` is the same range
advanced by 504; `insert(addr)` returns false unless `addr` is inside the window
*and* `addr - window.start` is a multiple of 8, otherwise it sets the bit and
returns true.

```
state = NO_RUN
for address in sorted_addresses:
    if state is NO_RUN or not window.insert(address):
        nxt = window.next() if state is WITH_BITMAP else NONE
        if nxt and nxt.insert(address):
            window = nxt; emit(window.encoded, NEW)   # state stays WITH_BITMAP
        else:
            emit(address, NEW)
            state = ADDRESS_ONLY; window = window_after(address)
    elif state is ADDRESS_ONLY:          # window.insert(address) succeeded
        emit(window.encoded, NEW); state = WITH_BITMAP
    else:                                # WITH_BITMAP, insert succeeded
        emit(window.encoded, UPDATE)
```

Two properties MUST hold. The encoder only ever advances to the **next
consecutive** window, never skipping windows to chase a distant address — when
the gap exceeds one window it starts a fresh address entry, which is what keeps
the output from filling with near-empty bitmap words. And size accounting runs
the *same* state machine as byte emission: allocation counts one entry per `NEW`
and zero per `UPDATE`, and writing replays it. If the two disagree the section
is mis-sized.

### 4.4 Eligibility

A relative relocation qualifies for RELR only if **all** of:

- the output is position-independent (PIE or shared object);
- the relocation is absolute and resolves to an address — `R_X86_64_64` against
  a non-preemptible target, the class that would otherwise become
  `R_X86_64_RELATIVE`;
- the target section is **writable**; a relative relocation into read-only,
  non-debug data is a hard error and MUST be diagnosed naming the relocation
  type, with the usual advice to rebuild with `-fPIC`;
- **the offset is even.** Bit 0 of an address entry is the bitmap marker, so an
  odd `r_offset` cannot be encoded — fall back to `.rela.dyn`.

Everything ineligible stays in `.rela.dyn`, so a link can emit both sections —
and when nothing is ineligible every relative relocation is packed, `.rela.dyn`
is left empty, and with it the generic `DT_RELA`/`DT_RELASZ`/`DT_RELAENT` tags
go too (`05-dynamic-linking.md` §6 gives the rule).
`DT_RELACOUNT` counts only the relative entries left in `.rela.dyn`.

GOT slots have an extra restriction: packable only when RELR is enabled, the
symbol is not an IFUNC, the symbol has no dynamic-symbol-table entry, the value
is an address, the symbol was not downgraded to local by a version script, and
the output is position-independent. RELR entries needed for `n` consecutive
packable GOT slots:

```
0                              if n == 0
1 + ceil((n - 1) / 63)         otherwise
```

— one address entry plus one bitmap word per 63 following slots.

### 4.5 Runs do not cross input sections

Reset the encoder state at each input-section boundary. Input sections are laid
out in parallel, so a run spanning two of them would make output depend on
thread scheduling. This costs a few extra address entries and buys determinism:
the output MUST be byte-identical across runs. The byte-identical clause is
stated but not graded: cross-run determinism is unreproducible on a loaded
build host (TEST_INTERFACE §4).

### 4.6 Worked example

A shared object with seven 8-byte pointers in `.data`, each needing a relative
relocation, linked with `--pack-dyn-relocs=relr`. Measured output:

```
.data      addr 0x3970  size 0x38   (7 pointers, 0x3970 .. 0x39a0)
.relr.dyn  addr 0x0670  size 0x10   type RELR  entsize 8  align 8
raw bytes at file offset 0x670:
  70 39 00 00 00 00 00 00      -> 0x0000000000003970
  7f 00 00 00 00 00 00 00      -> 0x000000000000007f
dynamic: DT_RELR 0x670, DT_RELRSZ 16, DT_RELRENT 8
```

No generic `DT_RELA` tags accompany that triple: every relative relocation was
packed, so `.rela.dyn` is empty and §4.4's rule suppresses them.

Decoding: `0x3970` is even, so it is an address — relocate `0x3970`, window
becomes `0x3978 .. 0x3b70`. `0x7f` is odd, and bits 1..6 are set (`0b1111111`;
bit 0 is only the marker), giving

```
bit 1 -> 0x3978    bit 4 -> 0x3990
bit 2 -> 0x3980    bit 5 -> 0x3998
bit 3 -> 0x3988    bit 6 -> 0x39a0
```

Seven relocated addresses in 16 bytes, against 7 x 24 = 168 bytes of `.rela.dyn`
for the same link without packing. Repeating the link with
`--use-android-relr-tags` produced byte-identical section contents, with the
dynamic tags `0x6fffe000 = 0x670`, `0x6fffe001 = 0x10`, `0x6fffe003 = 8` and the
section type shown by `readelf` as `LOOS+0xfffff00`.

## 5. `PT_GNU_RELRO`

RELR does not change what goes into the RELRO region, but the two must be
ordered correctly. `.relr.dyn` is read-only allocated data that the loader only
reads, so it MUST NOT be placed inside `PT_GNU_RELRO`. The *targets* of RELR
relocations are overwhelmingly in `.data.rel.ro` and `.got`, exactly what
`PT_GNU_RELRO` covers, and the loader applies RELR relocations **before** it
write-protects the region — any layout that protects first is broken. `-z relro`
therefore does not disable RELR and RELR does not disable RELRO. `-r` turns
RELRO off (a partial object has no segments at all); `--nmagic` turns it off
because the segment must be page-aligned.

# Part B — relocatable output

## 6. `-r` / `--relocatable`

`-r` produces a *partial object*: one `ET_REL` file, the concatenation of the
inputs, still carrying relocations, that can be fed into another link. `-r`
implies, and MUST imply, all of:

output is a partial object, not an executable; `--gc-sections` off; RELRO off;
string and section merging off; no linker-identity comment written.

### 6.1 The file header

`e_type` = `ET_REL`. `e_entry` = 0 — never resolve `_start`, never warn about
a missing entry symbol. `e_phoff` = `e_phentsize` = `e_phnum` = 0. `e_shoff` =
the file-header size plus zero program-header bytes. `e_shstrndx` normal unless
§7 applies.

MUST NOT write any program headers. Every section header gets `sh_addr = 0` — a
partial object has no addresses.

### 6.2 What must be preserved

- Every allocatable input section's contents under its own name. Custom sections
  keep their identity: `.text.foo` is not folded into `.text`, merging is off.
- Every `SHT_RELA` (and CREL) input section, rewritten as `SHT_RELA`, one output
  `.relaNAME` per output section `NAME`.
- `.symtab`, `.strtab`, `.shstrtab`, rebuilt; `.note.gnu.property`, merged by
  document 15's rules; RISC-V attributes, merged.
- `.debug_*`, `.comment`, `.note.ABI-tag` and every other non-alloc section.

### 6.3 What must NOT be synthesised

No PLT, GOT, `.plt.got`, `.rela.plt`, `.dynamic`, `.dynsym`, `.dynstr`,
`.gnu.hash`, `.hash`, version sections, `PT_LOAD`, `PT_GNU_RELRO`,
`PT_GNU_STACK`, interpreter, or `.eh_frame_hdr`. A GOT-generating relocation in
the input stays a relocation in the output; it gets no GOT slot here.

Input `.group` sections are **discarded**, along with the inputs' `.symtab`,
`.strtab`, `.shstrtab` and `.symtab_shndx`. COMDAT deduplication is resolved
now: the surviving group's members are kept, duplicates dropped, and the output
carries no `SHT_GROUP` section and no `SHF_GROUP` flag. A second link over this
output sees ordinary sections.

### 6.4 Symbols

Build the output `.symtab` as:

1. index 0 — the null symbol;
2. one `STT_SECTION`/`STB_LOCAL` symbol per output section that qualifies (see
   below), in output-section order, with value 0 and size 0;
3. the surviving local symbols from the inputs, grouped per input;
4. the global symbols, including undefined ones that no input defined.

A partial link does not diagnose undefined symbols: names that no input
defines -- even ones another input references strongly -- stay undefined here,
and the end-of-link error (`02-symbols-and-archives.md` §8) is the obligation
of the link that produces the final image.

A section symbol is emitted for an output section when: the section is emitted
at all; it is not the null section nor the synthetic file-header, program-header
or section-header "sections"; its type is not a relocation, symbol-table or
string-table type; and its target segment type is `PT_LOAD`. That last clause
excludes `.note.gnu.property`, `.eh_frame_hdr`, `.sframe`, `.dynamic` and
friends.

Build a map from every input symbol to its output `.symtab` index while writing.
Two rules make it correct:

- An input `STT_SECTION` symbol maps to the **output section symbol** for the
  output section its input section landed in, not to a copied symbol.
- A global symbol maps through its *canonical* definition, so references from
  files that only saw an undefined version still index the one output entry.

Locals land in per-group runs from each group's local base; globals start after
all locals. `.symtab`'s `sh_info` MUST be the index of the first non-local
symbol, i.e. the total local count.

### 6.5 Relocations

For each input `.relaNAME` section whose `sh_info` names a kept target section:

```
r_offset  = (address assigned to the target output section) + original r_offset
r_info    = (new symbol index from the map, or 0) | original relocation type
r_addend  = original addend
            + (if the referenced symbol is STT_SECTION:
                 the address assigned to that section's output section)
```

Under `-r` the "address assigned to an output section" is the offset of the
input section's contribution within the output section, since `sh_addr` is 0.
Folding the section base into the addend keeps a section-relative reference
pointing at the right byte after concatenation. A symbol with no entry in the
map MUST be written as symbol index 0, not dropped — the relocation count is
fixed by the input.

Output relocation section headers MUST have `sh_type = SHT_RELA`,
`sh_entsize = 24`, alignment 8, `sh_link` = the output `.symtab` index, and
`sh_info` = the index of the output section named by stripping the `.rela`
prefix from this section's name. CREL input is converted to RELA on output.

### 6.6 Relinkable

The output MUST link again: `-r a.o b.o -o part.o` followed by
`part.o c.o -o prog` must produce a working executable. Measured end to end with
the reference — the two-stage link produced a binary whose exit status matched
the one-stage link.

## 7. More than 65279 sections

`e_shnum`, `e_shstrndx` and `st_shndx` are 16-bit, and values from
`SHN_LORESERVE` (**0xff00**) up are reserved, so an output with `SHN_LORESERVE`
or more sections needs the escape hatch. `SHN_XINDEX` is **0xffff**.

Apply all three rules together.

| condition | what to write |
| --- | --- |
| section count >= 0xff00 | `e_shnum = 0`; section 0's `sh_size` = the real count |
| `.shstrtab` index >= 0xff00 | `e_shstrndx = 0xffff`; section 0's `sh_link` = the real index |
| any symbol's section index >= 0xff00 | that symbol's `st_shndx = 0xffff`; the real index goes in `.symtab_shndx` |

The threshold is `count >= 0xff00`, i.e. 65280 sections; a file with 65279
sections uses no escape.

`.symtab_shndx` is:

- type `SHT_SYMTAB_SHNDX` = **18**, `sh_entsize` 4, alignment 4, no flags;
- `sh_link` = the index of `.symtab`;
- exactly as many 4-byte entries as `.symtab` has symbols, in the same order;
- entry `i` holds the real section index for symbol `i`, or 0 when symbol `i`'s
  `st_shndx` is not `SHN_XINDEX`.

Allocate it whenever the output section count reaches `SHN_LORESERVE`, even if
every symbol turns out to fit in 16 bits; an all-zero table is valid. Input
`.symtab_shndx` sections are never copied through — they are rebuilt.

## 8. `--compress-debug-sections`

Accepted values: `none`, `zlib`, `zstd`. Any other value MUST be diagnosed.
(`zlib-gabi` is a GNU `ld` synonym of `zlib`; the reference rejects it, and no
case passes it either way.)

### 8.1 Which sections

Compress an output section when all of: the name starts with `.debug_`; the file
size is greater than zero; it is **not** `SHF_ALLOC`. Nothing else, ever.

### 8.2 The header

Compressed content is preceded by a 24-byte `Elf64_Chdr`:

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | `ch_type` |
| 4 | 4 | reserved, MUST be 0 |
| 8 | 8 | `ch_size` — the **uncompressed** byte count |
| 16 | 8 | `ch_addralign` — the uncompressed section's alignment |

`ch_type`: `ELFCOMPRESS_ZLIB` = **1**, `ELFCOMPRESS_ZSTD` = **2**
(`0x6000_0000`..`0x6fff_ffff` OS-reserved, `0x7000_0000`..`0x7fff_ffff`
processor-reserved; neither used here).

Set `SHF_COMPRESSED` (**0x800**) in the section's `sh_flags`, and clear it on
sections you did not compress — an input that arrived already compressed must
not leave a stale flag behind. `sh_size` becomes the compressed size *including*
the 24-byte header. The compressed section's `sh_addralign` is the alignment of
the compressed stream; the original alignment lives only in `ch_addralign`.

### 8.3 Bail-out rule

If `24 + compressed_size >= uncompressed_size`, **do not compress that
section** — leave it uncompressed, with no header and no `SHF_COMPRESSED`. Per
section; this is what stops small debug sections from growing.

### 8.4 Sharding (optional, but worth matching)

The reference compresses large sections in parallel shards that concatenate into
one valid zlib stream, so output does not depend on thread count. Shard size is
`max(65536, len / 128)`; a section at or below one shard is one ordinary zlib
stream. Otherwise: zlib header `78 9C`, each shard as raw deflate terminated by
a sync-flush, the empty final block `03 00`, then the big-endian Adler-32 of the
whole uncompressed section (combined from per-shard values). A decoder sees one
normal stream; zstd shards are consecutive frames and likewise concatenate. Any
conforming stream is acceptable — the graded property is that the section
decompresses to the original bytes with `ch_size` equal to their length.

### 8.5 Relocations and ordering

Debug sections MUST be fully relocated **before** compression: build in memory,
apply relocations, then compress. Recompute section file offsets afterwards,
since every compressed section shrank. Compression MUST NOT move any segment —
a `PT_LOAD` shifted by offset recomputation is a bug and MUST be reported rather
than written out.
