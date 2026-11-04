# The `.gdb_index` accelerator section

> Building the debugger index section: CU list, address area, hash table, constant pool.

## Provenance

Restated from the reference at tag `0.10.0` (step 9): `libwild/src/gdb_index.rs`
in full, plus `--gdb-index` / `--no-gdb-index` and `should_write_gdb_index()` in
`libwild/src/args/elf.rs`, and the `.gdb_index` output-section definition and
`SectionRuleOutcome::DebugIndex` routing in `libwild/src/elf.rs`. Every byte
offset, hash value and table in §9 was read out of a `.gdb_index` the reference
produced on this machine and decoded field by field; the version-7 contrast in
§2 came from `ld.gold` 2.40 on the same two objects.

Divergences the implementation makes normative:

1. **The reference emits version 9, not 7.** Its header is **seven** 4-byte
   words (28 bytes), not six (24): word 5 is a *shortcut table* offset that
   version 7 lacks, so `constant_pool_offset` is word 6, not word 5. Version 7
   is what older linkers emit and what most descriptions of the format
   describe, so it is shown in §2 as a contrast and nothing more; §3 onwards
   documents what the reference writes. **Emit version 9.**
2. **The symbol table is built only from `.debug_gnu_pubnames` and
   `.debug_gnu_pubtypes`.** The reference never walks DIEs in `.debug_info` and
   never opens `.debug_abbrev` or `.debug_str` at all. Given objects built
   without `-ggnu-pubnames` the link **succeeds** and `.gdb_index` is written
   with a **zero-slot** symbol table and an empty constant pool; a warning
   advising `-ggnu-pubnames` is permitted, but the reference emits none and it
   is not graded (§7.5). Building the symbol table from DWARF alone is therefore not
   gradeable here: §7 documents the pubnames route the reference takes, §7.5
   sketches the DIE walk if you choose to go further.
3. **The address area is built from section headers, not from DWARF.** The
   reference never reads `.debug_ranges` or `.debug_rnglists` and never looks at
   `DW_AT_low_pc`/`DW_AT_high_pc`/`DW_AT_ranges`. One entry per *input section*
   that is `SHF_ALLOC | SHF_EXECINSTR`, non-zero size, non-zero final address.
4. **The shortcut table is written as two zero words** — the reference does not
   determine `main`'s language; gdb falls back to its own lookup. §8.
5. Diagnostics: a failure MUST exit non-zero with at least one stderr line
   naming what went wrong. No prefix, wording or exit value is graded.

Not verified here: that gdb itself accepts the emitted section — no gdb is
installed. Every claim below is a claim about bytes, checked against bytes.

## 1. Scope and triggering

`.gdb_index` maps a name, or a code address, to the compilation unit describing
it, so a debugger can load one CU's DWARF instead of parsing all of
`.debug_info` at startup. Only the linker can build it: only the linker knows
the final addresses and the final layout of the concatenated `.debug_info`.

- `--gdb-index` enables generation, `--no-gdb-index` disables it; last wins.
- Generation is suppressed when debug info is stripped (`-S`, `--strip-debug`,
  `-s`, `--strip-all`): `--gdb-index -S` MUST link cleanly and emit nothing.
- If no input object has `.debug_info`, the computed size is zero and the
  section MUST be omitted entirely, not emitted empty.
- Under `--gdb-index`, input `.debug_gnu_pubnames` and `.debug_gnu_pubtypes` are
  **consumed**: they feed the index and are not copied to the output. Without
  `--gdb-index` they pass through as ordinary debug sections.

The output section is named `.gdb_index`, type `SHT_PROGBITS` (1), `sh_flags` 0
(**not** `SHF_ALLOC`), `sh_addr` 0, `sh_link` 0, `sh_info` 0, `sh_addralign` 1,
`sh_entsize` 0.

Everything inside is little-endian on a little-endian target and **unaligned**:
there is no padding anywhere, and a 20-byte address entry can and does start at
an offset that is not a multiple of 8. Read and write it as a byte stream.

## 2. Header

Seven 4-byte words, 28 bytes, at offset 0. Every offset is measured **from the
start of the `.gdb_index` section**, not from the start of the file.

| word | offset | field |
| --- | --- | --- |
| 0 | 0x00 | version — **9** |
| 1 | 0x04 | offset of the CU list |
| 2 | 0x08 | offset of the types-CU list |
| 3 | 0x0c | offset of the address area |
| 4 | 0x10 | offset of the symbol table |
| 5 | 0x14 | offset of the shortcut table |
| 6 | 0x18 | offset of the constant pool |

The regions are laid out in exactly that order and are contiguous: each offset
is the end of the previous region, so the CU list's size is
`tu_list_offset - cu_list_offset`, the types list's is
`address_area_offset - tu_list_offset`, and so on; the constant pool runs to the
end of the section. Derive region sizes this way, never by assumption.

**Version 7** — what the prompt and older linkers describe — has the same layout
minus word 5: six words, 24 bytes, `constant_pool_offset` at 0x14. Measured from
`ld.gold` 2.40 on the same inputs: `07 00 00 00 | 18 00 00 00 | 38 00 00 00 |
38 00 00 00 | 60 00 00 00 | 60 20 00 00`, i.e. version 7, CU list at 0x18 = 24.
That shape is reproduced here only so the seven-word header above is not
mistaken for it. **Version 9 with the seven-word header is required**: a
version-7 index is not accepted, and neither is a version-7 word count carrying
a version-9 number in word 0.

Total section size, which MUST be computed before writing anything since the
header offsets depend on it:

```
28
 + 16 * num_cus
 + 20 * num_address_entries
 +  8 * num_hash_slots
 + 8                                  # shortcut table
 + sum over symbols of 4 * (1 + num_cu_vector_entries)
 + sum over symbols of (len(name) + 1)
```

The types-CU list contributes 0 because the reference never emits type units.

## 3. The CU list

An array of 16-byte entries, in output order:

Bytes 0..8 are the CU's offset within the **output** `.debug_info`; bytes 8..16
are its total length including the initial-length field. Indices into this array
are "CU indices" everywhere else in the section.

To build it, walk each input object's `.debug_info` and split it into CUs by
initial length:

```
offset = 0
while offset + 4 <= len(data):
    init = u32(data[offset..])
    if init == 0xFFFF_FFFF:                 # DWARF64
        require offset + 12 <= len(data)
        total = 12 + u64(data[offset+4..])
    else:                                   # DWARF32
        total = 4 + init
    require total > 0 and offset + total <= len(data)
    emit CU at (offset, total)
    offset += total
```

A truncated DWARF64 length, a zero total, or a CU running past the end of the
section MUST be diagnosed, naming `.debug_info`; including the byte
offset is recommended but not graded.

Then rebase. Objects are concatenated into the output `.debug_info` in input
order, so keep a running base equal to the sum of all previous objects'
`.debug_info` lengths and add it to each CU's offset; the base advances by the
sum of that object's CU lengths. Objects with no `.debug_info`, or an empty one,
contribute no CUs and do not advance the base.

Global CU indices come from the same walk: object *k* gets a `cu_base` equal to
the CU count of objects 0..k-1, and its local CU *i* becomes `cu_base + i`.

## 4. The types-CU list

Used for DWARF type units in `.debug_types`. The reference emits **none**, so
this region is empty and `tu_list_offset == address_area_offset`. An empty types
list is correct output; a debugger reads it as "no type units".

## 5. The address area

An array of 20-byte entries, unaligned:

bytes 0..8 low address, bytes 8..16 high address, bytes 16..20 CU index.

The range is **half-open**: `[low, high)`, so `high` is `low + size`, the first
byte *after* the region, not the last byte in it. Off by one here makes the
debugger attribute the next function's first instruction to the wrong CU.
Entries need not be sorted and need not be disjoint; a debugger scans them.

## 6. Building address entries

For each input object that contributed at least one CU, in layout order, for
each of its input sections in section-index order:

1. skip the section unless it was actually loaded into the output;
2. skip it if its size is zero;
3. skip it unless its section header has **both** `SHF_ALLOC` and
   `SHF_EXECINSTR`;
4. skip it if the address assigned to it by the final layout is 0 (a section
   that was garbage-collected or placed at address 0 contributes nothing);
5. emit `low = assigned address`, `high = assigned address + section size`,
   `cu_index = cu_base_of_this_object + local_cu_index`.

`local_cu_index` is 0 when the object has exactly one CU, which is the common
case. When an object has more than one CU — one `.o` built from several
translation units, or `-ffunction-sections` with LTO — build a section-to-CU map
first:

- parse `.debug_info` again for that object to get the local CU boundaries; if
  the boundary count does not match the CU count recorded earlier, give up and
  map everything to CU 0;
- find `.rela.debug_info` (or `.rel.debug_info`);
- for each relocation, take `r_offset` and the referenced symbol's section
  index; find which local CU's `[offset, offset+length)` contains `r_offset`;
  record `section index -> local CU index`;
- **keep only the first mapping recorded for each section index.** A CU
  references the same section many times through line tables and range lists,
  and later references may belong to a different CU; the first one wins.
- any section not in the map maps to local CU 0.

This is deliberately approximate — a wrong CU index costs the debugger a
fallback scan and corrupts nothing. The property that MUST hold is that every
entry's CU index is a valid index into the CU list.

## 7. The symbol table and the constant pool

### 7.1 Symbol table

`num_hash_slots` slots of 8 bytes each:

bytes 0..4 the name offset and bytes 4..8 the CU-vector offset, both relative to
`constant_pool_offset`.

A slot is **empty** when both words are zero. That is why the constant pool must
begin with CU-vector data rather than a name: pool offset 0 is not a usable name
offset. Laying vectors first gets this for free.

Slot count:

```
slots = 0                                     if num_symbols == 0
        next_power_of_two(num_symbols * 4 / 3 + 1)   otherwise
```

with integer division: 1 symbol gives 2 slots, 8 gives 16, 24 gives 64. The
table is never more than 75% full, which bounds probing. With no symbols the
region is zero-sized — zero slots, not one empty slot — and
`shortcut_table_offset == symbol_table_offset`.

### 7.2 The hash function

Over the raw name bytes, **ASCII-case-folded to lower case**, in `u32`
wrapping arithmetic:

```
h = 0
for byte c in name:
    h = h * 67 + tolower(c) - 113
```

All three operations wrap modulo 2^32, including the subtraction, so short names
routinely produce values just below 2^32 (see `long` and `int` in §9). `tolower`
folds only `A`..`Z`; bytes >= 0x80 are untouched. Consequently
`hash("main") == hash("MAIN") == hash("Main")`.

### 7.3 Probing

Open addressing with a hash-derived step:

```
mask = slots - 1                  # slots is a power of two
step = (h * 17 & mask) | 1        # odd, so it generates the whole table
slot = h & mask
while slot is occupied:
    slot = (slot + step) & mask
place symbol at slot
```

`step` is forced odd so repeated addition modulo a power of two visits every
slot, so the probe always terminates at a free slot if one exists. Wrapping back
to the initial slot means the table is full, which cannot happen with the 4/3
sizing and MUST be reported rather than silently dropping a symbol. A reader
looks a name up by computing `h`, starting at `h & mask`, and following the same
step until it finds a matching name or an empty slot.

### 7.4 The constant pool

Two regions, in this order, with no alignment padding:

1. **CU vectors.** For each symbol, a `u32` count followed by that many `u32`
   entries. A symbol's `cu_vector_offset` points at its count word.
2. **Names.** Each symbol's name bytes followed by a single `0`. A symbol's
   `name_offset` points at its first byte.

Both offsets in a hash slot are relative to `constant_pool_offset`, so a name
offset is `total_cu_vector_bytes + (position within the names region)`.

A **CU-vector entry** packs a CU index with two attributes:

| bits | meaning |
| --- | --- |
| 0–23 | CU index (masked with `0x00FF_FFFF`) |
| 24–27 | zero |
| 28–30 | kind |
| 31 | 1 if the symbol is static (file-scope), 0 if global |

Kinds: 0 = none/unknown, 1 = type, 2 = variable, 3 = function, 4 = other.

Entries within one symbol's vector MUST be sorted ascending as unsigned 32-bit
values and MUST NOT contain duplicates. Because the CU index sits in the low
bits, that sort groups a name's occurrences by kind and staticness first, CU
second — that is the required order, not CU order.

A name appearing in several CUs gets one hash slot and several vector entries.
Deduplication is by exact byte equality of the name, **not** by the case-folded
hash: `Foo` and `foo` hash identically but are two distinct symbols that collide
and probe to different slots.

### 7.5 Where symbols come from

The reference reads `.debug_gnu_pubnames` and `.debug_gnu_pubtypes`, which share
an on-disk shape: a sequence of sets, each headed by the `.debug_info` offset of
the unit it describes, then entries of `(offset, attributes byte,
NUL-terminated name)` ending at a zero offset.

Per set, look its `.debug_info` offset up in that object's local CU boundary
table; if it does not match the start of a known CU, **skip the whole set** —
do not guess. Otherwise record `(name, local CU index, attributes byte)` per
entry.

The attributes byte packs its fields differently from the CU-vector entry, and
this conversion is the one that is easy to get backwards:

| attributes byte bits | CU-vector entry bits |
| --- | --- |
| 4–6 = kind | 28–30 |
| 7 = static | 31 |

so `entry = (cu_index & 0x00FF_FFFF) | (kind << 28) | (is_static << 31)`.

If after processing every input there is at least one CU but no symbols, write a
zero-slot symbol table. This MUST NOT fail the link. Measured at `0.10.0`, the
reference emits **no** diagnostic in that case -- linking a `-g`-only object
under `--gdb-index` writes nothing to stderr -- so a warning that the objects
lack `.debug_gnu_pubnames`/`.debug_gnu_pubtypes` and should be built with
`-ggnu-pubnames` is permitted but is not required and is not graded. What is
graded is the zero-slot shape and the successful link.

Building the symbol table from `.debug_info` directly — which the reference does
not do, and which nothing here grades — would mean: read each CU header for its
version, address size and abbrev offset; parse `.debug_abbrev` at that offset
into a code-to-(tag, has-children, attributes) table; walk the DIE tree and, for
each `DW_TAG_subprogram` with `DW_AT_low_pc`, each CU-scope `DW_TAG_variable`
with a location, and each type-defining tag, resolve `DW_AT_name` (following
`DW_FORM_strp` into `.debug_str`, `DW_FORM_line_strp` into `.debug_line_str`,
`DW_FORM_strx*` through `.debug_str_offsets`), map the tag to a kind and the
absence of `DW_AT_external` to static. Strictly more work for the same output,
and the only way to index objects built without `-ggnu-pubnames`.

## 8. The shortcut table

Eight bytes: a `u32` DWARF language code for `main`'s CU, then a `u32` offset of
`main`'s name in the constant pool.

The reference writes `0, 0`. A debugger reads zeros as "unknown" and falls back
to searching the index for `main`, so zeros are correct if unoptimal output.
Emit the eight bytes; omitting the region shifts every later offset.

## 9. Worked example

`gi1.c` defines `int gvarA`, `int funcA(int)`, `static int sfn(int)` and
`int funcB(int)`; `gi2.c` defines `int _startval` and `void _start(void)`
calling `funcA`. Both built `-g -ggnu-pubnames`, linked `--gdb-index`, non-PIE.
Measured: a 352-byte `.gdb_index`. Offsets are from the start of the section.

```
+0x000  09 00 00 00   version 9
+0x004  1c 00 00 00   cu list        @ 0x01c
+0x008  3c 00 00 00   types list     @ 0x03c   (empty)
+0x00c  3c 00 00 00   address area   @ 0x03c
+0x010  64 00 00 00   symbol table   @ 0x064
+0x014  e4 00 00 00   shortcut table @ 0x0e4
+0x018  ec 00 00 00   constant pool  @ 0x0ec
```

Region sizes follow: CU list 0x20 = 32 = 2 entries; types list 0; address area
0x28 = 40 = 2 entries; symbol table 0x80 = 128 = 16 slots; shortcut 8;
constant pool 352 - 0xec = 0x74 = 116.

CU list — the two CUs, rebased into the concatenated output `.debug_info`:

```
+0x01c  offset 0x00  length 0xbf      -> CU 0  (gi1.c)
+0x02c  offset 0xbf  length 0x63      -> CU 1  (gi2.c)
```

Address area — one entry per allocated executable input section, half-open:

```
+0x03c  0x401600 .. 0x40164f  cu 0
+0x050  0x401650 .. 0x40166a  cu 1
```

Symbol table — 8 symbols, so `8 * 4 / 3 + 1 = 11`, rounded up to **16** slots.
Eight slots are non-zero and eight are `00 00 00 00 00 00 00 00`. With
`mask = 15`, every symbol landed on its home slot; no probing was needed:

| name | hash | `h & 15` | slot | name off | CV off | vector |
| --- | --- | --- | --- | --- | --- | --- |
| `gvarA` | 0xf41308c0 | 0 | 0 | 0x4a | 0x08 | `0x20000000` |
| `int` | 0xffff72f2 | 2 | 2 | 0x59 | 0x20 | `0x90000000`, `0x90000001` |
| `_start` | 0x59ecb353 | 3 | 3 | 0x6d | 0x3c | `0x30000001` |
| `long` | 0xffe8e9d4 | 4 | 4 | 0x50 | 0x10 | `0x90000001` |
| `_startval` | 0x6bdc5d79 | 9 | 9 | 0x63 | 0x34 | `0x20000001` |
| `funcA` | 0xf2dbd69c | 12 | 12 | 0x5d | 0x2c | `0x30000000` |
| `funcB` | 0xf2dbd69d | 13 | 13 | 0x44 | 0x00 | `0x30000000` |
| `sfn` | 0x0000202e | 14 | 14 | 0x55 | 0x18 | `0xb0000000` |

`funcA` and `funcB` hash to consecutive values — the differing byte is last, so
the multiplier never touches the difference — and land in adjacent slots without
colliding. Decoding the vector entries:

- `0x20000000` — bit 31 clear, bits 28–30 = 2, low bits 0: global variable in
  CU 0. That is `gvarA`.
- `0x30000000` — kind 3, global: function in CU 0. `funcA` and `funcB`.
- `0x30000001` — function in CU 1: `_start`.
- `0xb0000000` — bit 31 set, bits 28–30 = 3: **static** function in CU 0. That
  is `sfn`, and the static bit is what distinguishes it from `funcA`.
- `0x90000000` / `0x90000001` — bit 31 set, bits 28–30 = 1: static type, in CU 0
  and CU 1. `int` is described by both CUs; `long` only by CU 1.

Constant pool. The first 16 bytes at `+0x0ec` are

```
01 00 00 00 | 00 00 00 30 | 01 00 00 00 | 00 00 00 20
```

which is `funcB`'s vector (count 1, entry `0x30000000`) at pool offset 0x00,
followed immediately by `gvarA`'s (count 1, entry `0x20000000`) at pool offset
0x08. Vector data runs to pool offset 0x44, exactly where the first name offset
points; names then run `funcB\0 gvarA\0 long\0 sfn\0 int\0 funcA\0
_startval\0 _start\0`, 0x30 bytes, ending at pool offset 0x74 and so at section
offset `0xec + 0x74 = 0x160 = 352`. The section ends there with no padding.

Two invariants this pins down, both of which MUST hold in your output:

- The smallest `name_offset` over all occupied slots equals the total size of
  the CU-vector region.
- `constant_pool_offset` plus the pool size equals the section size exactly. Any
  slack means the size was computed by a different rule than the one used to
  write, and the header offsets are already wrong.
