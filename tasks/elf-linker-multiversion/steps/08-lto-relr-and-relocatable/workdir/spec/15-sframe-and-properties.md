# SFrame stack traces and GNU property notes

> How to concatenate, re-sort and re-header `.sframe` input sections, and how to combine `.note.gnu.property` records across input objects into a single output note.

## Provenance

Restated from the SFrame V2 format as the reference implementation parses and
rewrites it (`libwild/src/sframe.rs`, tags `0.8.0`–`0.10.0`), from the GNU
property note encoding in `libwild/src/elf.rs`
(`process_gnu_note_section`, `merge_gnu_property_notes`) and
`libwild/src/elf_writer.rs` (`write_gnu_property_notes`), from the
per-architecture `get_property_class` implementations in
`libwild/src/elf_x86_64.rs`, `elf_aarch64.rs`, `elf_riscv64.rs` and
`elf_loongarch64.rs`, and from the ELF constants in the `object` 0.40 crate
that the reference depends on. Section, segment and note-type numbers were
cross-checked against `readelf` output on binaries this machine produced.
Nothing below is copied from a specification or a header; wording is original,
numbers are measured.

Where the reference implementation disagrees with the SFrame specification or
with this task's other documents, the implementation is normative and the
difference is called out inline:

1. **`--wild-experimental-sframe` does not exist at step 7's Oracle tag.**
   Measured: tags `0.7.0` and `0.8.0` reject the option; `0.9.0` and `0.10.0`
   accept it. `--discard-sframe` is rejected at `0.7.0`, `0.8.0` and `0.9.0`
   and accepted only at `0.10.0`. At the step-7 tag, `.sframe` processing is
   unconditional — there is no switch. The graded contract nevertheless passes
   `--wild-experimental-sframe` from step 8, so §6 requires you to accept it
   from step 8, where it turns `.sframe` processing on: without it, `.sframe`
   inputs are discarded. At step 7 the option does not exist: no step-7 case
   passes it, and `.sframe` is processed unconditionally there.
2. **`SFRAME_F_FDE_FUNC_START_PCREL` (0x4) is not in the published binutils
   2.43 SFrame V2 flag list**, but the reference implements it and its value
   is fixed by the implementation. §2.2 documents it as implementation-sourced.
3. **`GNU_PROPERTY_X86_ISA_1_USED` is not a plain OR property.** It falls in
   the *OR-AND* range and is combined by the OR-AND rule of §8.3, which the
   milestone interface summarises loosely as "union". The combination result
   for the union part is the same; the *retention* rule differs (see §8.4).
4. **`-z x86-64-v2/-v3/-v4` never fails a link.** The reference ORs the
   requested level bit into `GNU_PROPERTY_X86_ISA_1_NEEDED` and never compares
   it against what the inputs declared. There is no error path. §9.
5. **Properties whose descriptor is not exactly 4 bytes are silently dropped
   on input.** `GNU_PROPERTY_STACK_SIZE` and `GNU_PROPERTY_NO_COPY_ON_PROTECTED`
   therefore never reach the output. §7.4.

Not verifiable on this machine: the numeric register codes used inside an FRE
payload, and the FRE row encoding itself. The reference never decodes FRE
bytes — it copies them opaquely — so this document does not state their layout.
The local `as` (binutils 2.40) emits SFrame **version 1**, which the reference
refuses; every V2 measurement below was made against hand-assembled V2 sections.

## 1. Scope

`.sframe` is a compact, unwinder-friendly alternative to `.eh_frame`: instead of
a CFI program per function it stores, per function, a sorted table of
(offset, CFA rule) rows that a signal-safe stack walker can binary-search. A
linker does **not** generate SFrame data. It concatenates the `.sframe` sections
of its inputs, then rewrites the result into a single well-formed section: one
header, one globally sorted FDE array, one FRE blob.

## 2. The `.sframe` output section

### 2.1 Container facts

| property | value |
| --- | --- |
| section name | `.sframe` |
| section type | `SHT_GNU_SFRAME` = `0x6fff_fff4` |
| section flags | `SHF_ALLOC` only — never `SHF_WRITE`, never `SHF_EXECINSTR` |
| minimum alignment | 8 on ELF64 (the address alignment of the class) |
| covering segment | `PT_GNU_SFRAME` = `0x6474_e554`, flags `PF_R` |

You MUST emit the `PT_GNU_SFRAME` program header when a non-empty `.sframe`
output section exists, covering exactly that section, with `p_flags = PF_R`.
The segment's `p_align` follows the section's.

You MUST NOT treat an empty `.sframe` output section as content: if the merged
section is zero bytes, no section and no segment is emitted. (Every other
built-in section counts as content even when empty; `.sframe` is the exception
in the reference.)

`PT_GNU_SFRAME` MUST also be recognised as a linker-script token evaluating to
`0x6474_e554` wherever segment types are named.

### 2.2 Preamble and header

The header is a fixed 28-byte (`0x1c`) little-endian structure, optionally
followed by `aux_len` bytes of auxiliary header. All offsets below are from the
start of the header.

| offset | size | field | notes |
| --- | --- | --- | --- |
| `0x00` | 2 | magic | MUST be `0xdee2` |
| `0x02` | 1 | version | MUST be `2` |
| `0x03` | 1 | flags | see below |
| `0x04` | 1 | ABI/arch code | copied from the first input |
| `0x05` | 1 | fixed FP offset | signed |
| `0x06` | 1 | fixed RA offset | signed |
| `0x07` | 1 | `aux_len` | bytes of auxiliary header following the 28 |
| `0x08` | 4 | `num_fdes` | count of function descriptors |
| `0x0c` | 4 | `num_fres` | count of frame-row entries |
| `0x10` | 4 | `fre_len` | total bytes of FRE data |
| `0x14` | 4 | `fde_start_offset` | from end of header+aux to the FDE array |
| `0x18` | 4 | `fre_start_offset` | from end of header+aux to the FRE blob |

The magic doubles as an endianness probe (a big-endian producer writes the same
two bytes swapped); the graded contract is little-endian ELF64 only, so magic
MUST read back as `0xdee2`.

Flag bits:

| bit | name | meaning |
| --- | --- | --- |
| `0x1` | FDE-sorted | the FDE array is sorted by function start address |
| `0x2` | frame-pointer | every FDE in this section uses a frame pointer |
| `0x4` | func-start-PC-relative | FDE field 0 is relative to the FDE itself, not to the section base — implementation-sourced, see Provenance 2 |
| `0x8` | AArch64 pointer auth | the section carries PAuth-aware rules |

### 2.3 FDE sub-section

Each function descriptor is exactly 20 bytes. Only three fields matter to a
linker:

| offset in FDE | size | field |
| --- | --- | --- |
| `0` | 4 | function start, signed 32-bit (PC-relative or section-relative per flag `0x4`) |
| `4` | 4 | function size in bytes |
| `8` | 4 | offset of this function's first FRE, relative to `fre_start_offset` |

Bytes 12..20 carry the FRE count, FDE type/flags and padding: copy them through
unchanged. You MUST rewrite fields 0 and 8. The FDE array begins at
`header_end + fde_start_offset`, where `header_end = 0x1c + aux_len`.

### 2.4 FRE sub-section

The FRE blob begins at `header_end + fre_start_offset` and is `fre_len` bytes
long. A linker MUST treat FRE bytes as opaque: it moves them, it never rewrites them.
Each FDE owns the byte range from its field-8 offset to the next FDE's field-8
offset (or to the end of the input's FRE blob for the last one). Deriving that
range is the only FRE parsing required.

## 3. Reading an input `.sframe` section

For each contributing input range (an input `.sframe` section's placement in the
merged output buffer):

1. If the range is shorter than 28 bytes, skip it entirely.
2. Parse the header. Magic other than `0xdee2` MUST be a hard link failure.
3. A version other than `2` MUST NOT be a hard failure from step 8 onward: emit
   a warning and abandon SFrame sorting for the whole link, leaving the
   concatenated bytes as they are. (The step-7 tag instead fails the link; the
   later behaviour is the one to implement, because it is what the last Oracle
   does and warnings are not graded.)
4. Compute `header_end = 0x1c + aux_len` with overflow checked.
5. `fde_start = range.start + header_end + fde_start_offset`,
   `fre_start = range.start + header_end + fre_start_offset`.
6. If `fde_start + 20 * num_fdes` runs past the end of the range, the input is
   truncated and the link MUST fail.

### 3.1 Deriving per-FDE FRE extents

Build a vector of every FDE's field-8 value, append the sentinel
`range.end - fre_start`, sort it ascending and deduplicate it. For an FDE whose
field 8 is `o`, its FRE bytes run from `o` to the next larger value in that
vector. An `o` not present in the vector, or a last entry with no successor, is
a malformed input and MUST fail the link. This construction tolerates FDEs
sharing FRE data and FDEs listed out of FRE order.

### 3.2 Computing the absolute function address

Let `section_base` be the virtual address assigned to the merged `.sframe`
output section, `off` the byte offset of this FDE within the merged buffer, and
`start` the signed 32-bit value in FDE field 0.

```
if flags & 0x4:  func_addr = section_base + off + start     # PC-relative
else:            func_addr = section_base + start
```

Do this arithmetic in a type wider than 64 bits (the reference uses `i128`): a
PC-relative negative `start` against a low section base underflows `u64`.

## 4. Merging rule for header fields

Process input ranges in placement order.

- The **first** range that parses supplies `flags`, the ABI/arch code, both
  fixed CFA offsets and `aux_len` for the output.
- For every **later** range: if its flags lack the frame-pointer bit `0x2`,
  clear that bit in the output flags. If its flags carry the AArch64 PAuth bit
  `0x8`, set it in the output flags. All other bits keep the first range's
  values.
- `num_fres` in the output is the **sum of the inputs' `num_fres` header
  fields**, not a recount of emitted rows.
- `num_fdes` is the number of entries actually collected.
- `fre_len` is the sum of the per-FDE FRE byte lengths collected.

You MUST set the FDE-sorted bit `0x1` in the output flags in addition to the
merged bits.

## 5. Writing the merged section

With `N` collected entries sorted ascending by `func_addr` (a stable sort by
address; equal addresses keep input order):

```
header_end        = 0x1c + aux_len
fde_start_offset  = 0                    # written into the header
fre_start_offset  = N * 20               # written into the header
fde_start_idx     = header_end + 0
fre_start_idx     = header_end + N * 20
total_size        = header_end + N * 20 + sum(len(fre_bytes))
```

`total_size` MUST NOT exceed the allocated section size; if it does, fail the
link. Then, walking entries in sorted order with a running `cur = 0`:

```
pos = fde_start_idx + i * 20
if flags & 0x4:  field0 = func_addr - (section_base + pos)
else:            field0 = func_addr - section_base
field0 must fit in a signed 32-bit integer, else fail the link
write field0 at pos+0, write cur at pos+8, copy bytes 4..8 and 12..20 through
copy fre_bytes to fre_start_idx + cur
cur += len(fre_bytes)
```

Finally, **zero-fill** every byte from `total_size` to the end of the allocated
section. The merged section is normally smaller than the sum of its inputs only
when inputs shared FRE data; the tail MUST be zeroed rather than left with stale
input bytes.

Note the consequence of `fde_start_offset = 0` and `fre_start_offset = N*20`:
the output layout is always header, then auxiliary header, then the whole FDE
array, then the whole FRE blob, regardless of how the inputs were laid out.

## 6. Command-line surface

| option | required behaviour |
| --- | --- |
| `--wild-experimental-sframe` | MUST be accepted from step 8. It enables `.sframe` processing. |
| `--discard-sframe` | MUST be accepted from step 9. Input `.sframe` sections are discarded: no output section, no `PT_GNU_SFRAME`. |

From step 8 the reference gates `.sframe` on the experimental flag: without it,
`.sframe` inputs are discarded and neither section nor segment appears. (Step 9
re-expresses the same gate by defaulting an internal "discard" flag to true and
clearing it only when the experimental flag is seen.) Accepting the flag and
processing `.sframe` unconditionally at step 7 satisfies the step-7 Oracle (no
step-7 case passes it); from step 8 the flag is required, and the step-8 cases
pass it. `--discard-sframe` MUST win when both are
given.

## 7. `.note.gnu.property` framing

### 7.1 Note header

A note section is a sequence of notes, each laid out as:

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | `n_namesz` |
| 4 | 4 | `n_descsz` |
| 8 | 4 | `n_type` |
| 12 | `n_namesz` | name, NUL-terminated |
| | pad to 4 | |
| | `n_descsz` | descriptor |

For a GNU property note: name is the 4 bytes `G`, `N`, `U`, `\0`, so
`n_namesz = 4`; `n_type = NT_GNU_PROPERTY_TYPE_0 = 5`. Because the name is
exactly 4 bytes, the descriptor starts at offset 16, which is already 8-byte
aligned on ELF64 — no padding is inserted between name and descriptor.

The output `.note.gnu.property` section is `SHT_NOTE`, `SHF_ALLOC`, aligned 8,
and is covered by `PT_GNU_PROPERTY` = `0x6474_e553` with `p_flags = PF_R`.
The segment's `p_align` follows the section's, as `PT_GNU_SFRAME`'s does.
Emit the section and the segment only when at least one property survives §8.

### 7.2 Descriptor layout

On ELF64 the descriptor is a sequence of 16-byte entries, one per property:

| offset in entry | size | field |
| --- | --- | --- |
| 0 | 4 | `pr_type` |
| 4 | 4 | `pr_datasz` |
| 8 | `pr_datasz` | `pr_data` |
| | pad | to a multiple of 8 |

Every property this document requires has `pr_datasz = 4`, so each entry is
`8 + 4` rounded up to 8 = **16 bytes**, with 4 bytes of zero padding. Hence:

```
section size = 12 (note header) + 4 (name) + 16 * (number of properties)
n_descsz     = 16 * (number of properties)
```

The padding bytes MUST be written as zero.

### 7.3 Property type numbers

| `pr_type` | name | class (§8.1) |
| --- | --- | --- |
| `1` | stack size | — (dropped, §7.4) |
| `2` | no copy on protected | — (dropped, §7.4) |
| `0xb000_8000` | generic "1 needed"; bit 0 = indirect extern access | Or |
| `0xc000_0000` | AArch64 feature-1 AND; bit 0 = BTI, bit 1 = PAC | And |
| `0xc000_0001` | AArch64 PAuth ABI | unclassified on AArch64 → link failure |
| `0xc000_0002` | x86 feature-1 AND; bit 0 = IBT, bit 1 = SHSTK | And |
| `0xc000_8002` | x86 ISA-1 needed | Or |
| `0xc001_0002` | x86 ISA-1 used | AndOr |

x86 ISA-1 bit values, in both the "used" and "needed" words: baseline `1<<0`,
v2 `1<<1`, v3 `1<<2`, v4 `1<<3`.

Classification is by *range*, not by individual type, and is per architecture.
On x86-64 the ranges are, in the order they are tested:

| range | class |
| --- | --- |
| `0xc000_0002` .. `0xc000_7fff` | And |
| `0xc000_8000` .. `0xc000_ffff` | Or |
| `0xc001_0000` .. `0xc001_7fff` | AndOr |
| `0xb000_0000` .. `0xb000_7fff` | And |
| `0xb000_8000` .. `0xb000_ffff` | Or |
| anything else | **unclassified** |

On AArch64 only `0xc000_0000` is classified (And); every other type is
unclassified. On RISC-V and LoongArch **no** type is classified.

An unclassified property type MUST fail the link, with the numeric type named
on stderr. This is a real hazard: any 4-byte GNU property in a RISC-V or
LoongArch input aborts the link at the last Oracle tag.

The x86 feature-2 pair (`0xc001_0001` "used", `0xc000_8001` "needed") is covered
by the AndOr and Or ranges respectively; those two numbers come from the psABI
and were not measured here — no local toolchain emits them.

### 7.4 Input filtering

While reading an input's `.note.gnu.property`, you MUST skip any property whose
`pr_data` is not exactly 4 bytes long. That silently discards stack-size and
no-copy-on-protected properties. Do not error on them, do not propagate them.

Notes in a `SHT_NOTE` section whose type is not `NT_GNU_PROPERTY_TYPE_0` are not
properties; a `.note.gnu.property` section that cannot be iterated as GNU
properties MUST fail the link.

## 8. Combining properties across inputs

### 8.1 The three classes

- **Or** — a bit is set in the output if it is set in any relocatable input.
  Drop the property entirely if the result is zero.
- **And** — a bit is set in the output only if it is set in *every* relocatable
  input. Drop the property entirely if the result is zero.
- **AndOr** — a bit is set in the output if it is set in any input, but the
  property is kept only if it was present in every input; when kept with an
  all-zero value it is still written, to record "zero everywhere".

### 8.2 Two-level fold

The combination is a two-level fold and the levels use different operators.

```
for each input file F:
    file_map = {}
    for each property P in F:            # within one file, always OR
        class = classify(P.type)         # unclassified -> fail the link
        file_map[P.type] |= P.data
    for (t, v) in file_map:              # across files, class-dependent
        if t already in global_map and class(t) == And:
            global_map[t] &= v
        else:
            global_map[t] |= v
```

Note the asymmetry: *within* a single input file, repeated properties of the
same type are always OR-ed, even for And types. Only the across-files step uses
AND, and only for And-class types.

### 8.3 Retention filter

Let `present_in_all(t)` be true when every relocatable input contributed at
least one property of type `t`. After the fold, keep type `t` with value `v`
when:

| class | keep when |
| --- | --- |
| Or | `v != 0` |
| And | `present_in_all(t)` and `v != 0` |
| AndOr | `present_in_all(t)` |

An input with **no** `.note.gnu.property` at all still counts as an input for
`present_in_all`, so linking one IBT-marked object with one unmarked object
drops the x86 feature-1 property from the output. That was confirmed
empirically: IBT + IBT keeps the note, IBT + unmarked produces no note at all.

### 8.4 Output ordering

Emit the surviving properties sorted ascending by `pr_type`. The order MUST be
deterministic and MUST NOT depend on hash iteration order.

## 9. `-z x86-64-*`

`-z x86-64-baseline`, `-z x86-64-v2`, `-z x86-64-v3` and `-z x86-64-v4` set a
requested ISA level, mapping to bits `1<<0`, `1<<1`, `1<<2`, `1<<3`.

If any of them was given, after the two-level fold of §8.2 and **before** the
retention filter of §8.3, insert or update `GNU_PROPERTY_X86_ISA_1_NEEDED`
(`0xc000_8002`) with class Or, OR-ing the requested bit into its value —
creating the entry with value 0 first if no input declared it.

You MUST NOT compare the requested level against any level declared by the
inputs, and you MUST NOT fail the link because an input declares a different or
higher level. There is no diagnostic on this path at all. Because the entry is
forced to class Or, it survives the retention filter whenever the value is
non-zero, even if no input file carried the property. Measured at every level
for both marked and unmarked inputs: the link always succeeds and the output
note always carries the OR-ed value.
