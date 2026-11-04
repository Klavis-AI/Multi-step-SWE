# RISC-V 64 relocations, relaxation and CREL

> Everything a linker must know about the RV64 psABI: relocation numbers and fields, the HI20/LO12 pairing rule, e_flags merging, the compact CREL relocation format (a container format that is *not* RV64-specific, see §9), and — specified here but deferred to a later milestone, see §5-§6 — linker relaxation with byte deletion.

## Provenance

The relocation numbers, field placements and expressions restate the RISC-V ELF
psABI (`riscv-non-isa/riscv-elf-psabi-doc`, `riscv-elf.adoc`, sections
*Relocations* and *Linker Relaxation*,
<https://github.com/riscv-non-isa/riscv-elf-psabi-doc>) and the base
instruction-format encodings from the RISC-V unprivileged ISA. The CREL section
of this document restates the format description at
<https://maskray.me/blog/2024-03-09-a-compact-relocation-format-for-elf> and
the generic-ABI proposal it accompanies.

Deliberate narrowing: only the relocations the reference implementation
supports are listed as supported, and the supported/unsupported split, the
allowed ranges, the relaxation rewrites and the `e_flags` rules were all read
out of the reference implementation's source (`linker-utils/src/riscv64.rs`,
`libwild/src/elf_riscv64.rs` at tag `0.10.0`) and, where a fixture could be
built, measured with `clang-16`, `llvm-readelf-16`, `llvm-objdump-16` and
`qemu-riscv64-static` on this machine. Where the psABI describes a relocation
the reference implementation rejects, the rejection is the requirement.

Symbols used in expressions: `S` the symbol's value, `A` the addend, `P` the
address of the relocated field, `G` the offset of the symbol's GOT slot from
the GOT base, `GOT` the GOT base address, `L` the address of the symbol's PLT
entry, `TP` the thread pointer, `DTV` a dynamic-thread-vector entry.

As on x86-64 (`03-x86-64-relocations.md` § 1) and AArch64, `L` falls back to
`S` when no PLT entry is needed — a static link, or a definition that cannot
be preempted: the linker resolves `R_RISCV_CALL_PLT` and `R_RISCV_PLT32`
straight to the symbol and synthesises nothing. Measured at `wild-0.6.0` and
`wild-0.10.0`: a static RV64 link of an object carrying `R_RISCV_CALL_PLT`
against a definition in another object emits **no** `.plt` and **no** `.got`,
and the `auipc`/`jalr` pair sums to the callee's own address.

## 1. Target basics

| Property | Value |
| --- | --- |
| `e_machine` | `EM_RISCV` = 243 |
| Class / endianness | ELFCLASS64, ELFDATA2LSB |
| Default image base for a non-PIE executable | `0x10000` |
| Relocation section type in input | `SHT_RELA` (4) or `SHT_CREL` (0x40000014); `SHT_CREL` is not RV64-specific and MUST be accepted on every supported target, see §9 |
| PLT entry size | 16 bytes |
| Common page size | 4096 |

The default load address of `0x10000` is not the x86-64 value; an executable
linked from freestanding objects lands with its first `PT_LOAD` at `0x10000`
and its entry point just above it (measured: `0x11350` for a small fixture).

## 2. Instruction fields

Every RISC-V relocation that patches an instruction writes into one of these
immediate encodings. The linker MUST preserve all bits outside the field.

| Field | Width | Placement in the 32-bit (or 16-bit) instruction | Scaling |
| --- | --- | --- | --- |
| U-type | 20 | `imm[31:12]` → instruction bits 31:12 | value is the *upper* 20 bits, see §3 |
| I-type | 12 | `imm[11:0]` → instruction bits 31:20 | signed, byte units |
| S-type | 12 | `imm[11:5]` → bits 31:25, `imm[4:0]` → bits 11:7 | signed, byte units |
| B-type | 12 | `imm[12]`→31, `imm[10:5]`→30:25, `imm[4:1]`→11:8, `imm[11]`→7 | signed, bit 0 implicit zero |
| J-type | 20 | `imm[20]`→31, `imm[10:1]`→30:21, `imm[11]`→20, `imm[19:12]`→19:12 | signed, bit 0 implicit zero |
| UI-type | 32 | the U-type field of the first instruction plus the I-type field of the next, treated as one 64-bit write window | see `CALL` in §3 |
| CB-type | 8 | compressed branch: `imm[8|4:3]`→12:10, `imm[7:6|2:1|5]`→6:2 | signed, bit 0 implicit zero |
| CJ-type | 11 | compressed jump: `imm[11|4|9:8|10|6|7|3:1|5]`→12:2 | signed, bit 0 implicit zero |
| CLUI-type | 6 | `c.lui` immediate: `imm[17]`→12, `imm[16:12]`→6:2 | signed, non-zero, excludes `rd` ∈ {x0, x2} |

## 3. Relocation table

`Δ` in the overflow column means the computed value before the field is
extracted. A range written `[lo, hi]` is inclusive; `align n` means the value
MUST be a multiple of `n`. "No check" means the implementation MUST NOT reject
on range.

| # | Name | Expression | Field | Overflow check |
| --- | --- | --- | --- | --- |
| 0 | `R_RISCV_NONE` | — | none | none |
| 1 | `R_RISCV_32` | `S + A` | 4 bytes | `[-2^31, 2^32)` |
| 2 | `R_RISCV_64` | `S + A` | 8 bytes | none |
| 16 | `R_RISCV_BRANCH` | `S + A - P` | B-type | `[-2^12, 2^12-1]`, align 2 |
| 17 | `R_RISCV_JAL` | `S + A - P` | J-type | `[-2^20, 2^20-1]`, align 2 |
| 18 | `R_RISCV_CALL` | `L + A - P` | UI-type (64-bit window) | `[-2^31, 2^32)` |
| 19 | `R_RISCV_CALL_PLT` | `L + A - P` | UI-type (64-bit window) | `[-2^31, 2^32)` |
| 20 | `R_RISCV_GOT_HI20` | `GOT + G + A - P` | U-type | `[-2^31, 2^32)` |
| 21 | `R_RISCV_TLS_GOT_HI20` | `GOT + G + A - P`, slot holds `S - TP` | U-type | `[-2^31, 2^32)` |
| 22 | `R_RISCV_TLS_GD_HI20` | `GOT + G + A - P`, slot pair holds `DTV` module and offset | U-type | `[-2^31, 2^32)` |
| 23 | `R_RISCV_PCREL_HI20` | `S + A - P` | U-type | `[-2^31, 2^32)` |
| 24 | `R_RISCV_PCREL_LO12_I` | low 12 of the *paired* HI20's value — see §4 | I-type | `[-2^31, 2^32)` |
| 25 | `R_RISCV_PCREL_LO12_S` | as above | S-type | `[-2^31, 2^32)` |
| 26 | `R_RISCV_HI20` | `S + A` | U-type | `[-2^31, 2^32)` |
| 27 | `R_RISCV_LO12_I` | `S + A` | I-type | `[-2^31, 2^32)` |
| 28 | `R_RISCV_LO12_S` | `S + A` | S-type | `[-2^31, 2^32)` |
| 29 | `R_RISCV_TPREL_HI20` | `S + A - TP` | U-type | `[-2^31, 2^32)` |
| 30 | `R_RISCV_TPREL_LO12_I` | `S + A - TP` | I-type | `[-2^31, 2^32)` |
| 31 | `R_RISCV_TPREL_LO12_S` | `S + A - TP` | S-type | `[-2^31, 2^32)` |
| 32 | `R_RISCV_TPREL_ADD` | — (marker for relaxation) | none | none |
| 33-36 | `R_RISCV_ADD8/16/32/64` | `*field + S + A` | 1/2/4/8 bytes | none |
| 37-40 | `R_RISCV_SUB8/16/32/64` | `*field - (S + A)` | 1/2/4/8 bytes | none |
| 41 | `R_RISCV_GOT32_PCREL` | `GOT + G + A - P` | 4 bytes | `[-2^31, 2^32)` |
| 43 | `R_RISCV_ALIGN` | — (alignment constraint, see §6) | none | none |
| 44 | `R_RISCV_RVC_BRANCH` | `S + A - P` | CB-type | `[-2^8, 2^8-1]`, align 2 |
| 45 | `R_RISCV_RVC_JUMP` | `S + A - P` | CJ-type | `[-2^11, 2^11-1]`, align 2 |
| 51 | `R_RISCV_RELAX` | — (marker, see §5) | none | none |
| 52 | `R_RISCV_SUB6` | low 6 bits of `*field - (S + A)`, upper 2 bits preserved | 1 byte | none |
| 53 | `R_RISCV_SET6` | low 6 bits of `S + A`, upper 2 bits preserved | 1 byte | none |
| 54-56 | `R_RISCV_SET8/16/32` | `S + A` | 1/2/4 bytes | none |
| 57 | `R_RISCV_32_PCREL` | `S + A - P` | 4 bytes | `[-2^31, 2^32)` |
| 59 | `R_RISCV_PLT32` | `L + A - P` | 4 bytes | `[-2^31, 2^32)` |
| 60 | `R_RISCV_SET_ULEB128` | first half of a ULEB pair; see below | 0 (deferred) | none |
| 61 | `R_RISCV_SUB_ULEB128` | `S₂ + A₂ - (S₁ + A₁)` written as ULEB128 in place | up to 8 bytes | none |

The ULEB pair MUST be processed as a unit: `R_RISCV_SET_ULEB128` at an offset
records the minuend and writes nothing; the `R_RISCV_SUB_ULEB128` at the *same*
offset computes the difference and rewrites the existing ULEB128 field in
place, without changing its byte length.

The following are defined by the psABI but MUST be rejected with a diagnostic
naming the relocation: `R_RISCV_RVC_LUI` (46), `R_RISCV_GPREL_I` (47),
`R_RISCV_GPREL_S` (48), `R_RISCV_TPREL_I` (49), `R_RISCV_TPREL_S` (50),
`R_RISCV_IRELATIVE` (58), and the TLS-descriptor family `R_RISCV_TLSDESC` (12),
`R_RISCV_TLSDESC_HI20` (62), `R_RISCV_TLSDESC_LOAD_LO12` (63),
`R_RISCV_TLSDESC_ADD_LO12` (64), `R_RISCV_TLSDESC_CALL` (65). Global-pointer
relaxation and TLS descriptors are out of scope for every graded step.

Dynamic relocation types used in output: `R_RISCV_RELATIVE` (3),
`R_RISCV_COPY` (4), `R_RISCV_JUMP_SLOT` (5), `R_RISCV_TLS_DTPMOD64` (7),
`R_RISCV_TLS_DTPREL64` (9), `R_RISCV_TLS_TPREL64` (11).

## 4. The HI20/LO12 pairing rule

This is the one part of the RISC-V ABI that behaves unlike every other
architecture in this task, and getting it wrong produces silently wrong
addresses.

A PC-relative access is assembled as two instructions:

```
.Lpcrel_hi0:
        auipc   a0, %pcrel_hi(g)     # R_RISCV_PCREL_HI20,  symbol = g
        ld      a0, %pcrel_lo(.Lpcrel_hi0)(a0)
                                     # R_RISCV_PCREL_LO12_I, symbol = .Lpcrel_hi0
```

**The `PCREL_LO12_*` relocation's symbol is not the target.** It names a local
label whose value is the *offset of the `auipc`* — that is, the address `P` of
the paired HI20 relocation. The linker MUST therefore:

1. Resolve the HI20: `H = S_target + A_hi - P_hi`, where `P_hi` is the address
   of the `auipc`. Write `(H + 0x800) >> 12` into the U-type field, truncated
   to 20 bits.
2. For the LO12, look up the relocation whose offset equals the LO12's
   *symbol value* within the same input section, recover that relocation's own
   `S`, `A` and `P`, recompute `H` for it, and write `H & 0xfff` into the
   I-type or S-type field, sign-extended.

The `+ 0x800` in step 1 is the rounding bias that compensates for the LO12
field being sign-extended. The linker MUST NOT apply the bias a second time in
step 2.

The high-part relocations that can be the target of a `PCREL_LO12_*` lookup
are `R_RISCV_HI20`, `R_RISCV_PCREL_HI20`, `R_RISCV_GOT_HI20`,
`R_RISCV_TLS_GOT_HI20`, `R_RISCV_TLS_GD_HI20` and `R_RISCV_TPREL_HI20`. Any
other high part paired with a LO12 MUST be an error.

Two LO12 relocations MAY name the same HI20 label (a load and a store through
one `auipc`). The lookup MUST therefore be by address, not by consuming the
HI20.

Measured in a clang-16 object on this machine, the `.rela.text` entry for the
LO12 reads `R_RISCV_PCREL_LO12_I` against the symbol `.Lpcrel_hi0`, whose
`st_value` is the byte offset of the `auipc` inside `.text`.

## 5. Relaxation

`R_RISCV_RELAX` (51) is emitted at the same offset as another relocation and
means "the preceding relocation's instruction sequence may be shortened". It
carries no value of its own. The linker MAY ignore it entirely, and `--no-relax`
MUST make it a no-op. Performing the rewrites is a **separate capability**: it
belongs to the milestone that introduces RISC-V relaxation and is graded there,
not at the milestone that introduces the RISC-V backend. Measured, so that
nobody implements this ahead of its milestone and nobody assumes the reference
already does it: a relaxable `auipc`+`jalr` pair is left alone with the default
settings at `wild-0.6.0`, `0.7.0` and `0.8.0`, and is rewritten to a single
`jal` at `0.9.0` and `0.10.0`. The milestone that does relax must support at
least these rewrites.

| Precondition | Rewrite | Bytes deleted |
| --- | --- | --- |
| `R_RISCV_CALL`/`CALL_PLT` + `RELAX`, and the target is within J-type range | replace the `auipc`+`jalr` pair with a single `jal rd, target`; opcode word is `0x6f \| (rd << 7)` | 4 |
| `R_RISCV_HI20` + `RELAX` where `(value + 0x800) >> 12` is zero | delete the `lui` and clear `rs1` of the following LO12 instruction to `x0` (mask `0x1f << 15`) | 4 |
| `R_RISCV_HI20` + `RELAX` where `(value + 0x800) >> 12` fits the signed non-zero 6-bit `c.lui` immediate and `rd ∉ {x0, x2}` | replace `lui` with `c.lui`, opcode halfword `0x6001 \| (rd << 7)` with the immediate written into the fields §1's CLUI-type row gives (`imm[17]`→bit 12, `imm[16:12]`→bits 6:2) | 2 |
| a relaxed-away instruction that must keep its slot | write the 4-byte canonical nop `01 00 01 00` | 0 |

Rules the implementation MUST obey when it deletes bytes:

1. Deletion changes the size of the input section's contribution. Every symbol
   value, relocation offset and `.eh_frame`/`.debug_*` reference **after** the
   deletion point in that section MUST be shifted down by the number of bytes
   deleted, and every symbol defined in that section MUST have its **size**
   adjusted to match: both ends of the symbol — `st_value` and
   `st_value + st_size` — are mapped through the same input-to-output delta
   and the new `st_size` is the difference between the mapped ends. A function
   whose instruction sequence was shortened therefore reports the shrunken
   size, not the size it had before relaxation. Measured: relaxing an
   `auipc`+`jalr` call to a `jal` shrinks the caller's `st_size` by 4 bytes
   (18 to 14), and deleting one `lui` while compressing another to a two-byte
   `c.lui` shrinks a function's `st_size` by 6 (18 to 12).
2. When the first relocation of a pair is relaxed away (the `CALL`→`JAL` and
   the `lui`-delete cases), the *next* relocation at the following offset MUST
   be skipped rather than applied.
3. Relaxation MUST be a fixed point: deleting bytes may bring another
   candidate into range, so the pass either iterates or is defined to run once
   and to be conservative. The graded corpus only requires that the output is
   correct and that `R_RISCV_ALIGN` constraints still hold afterwards (§6).

Measured: a fixture whose `_start` contains `call ext_fn` assembles to
`auipc`+`jalr` with `R_RISCV_CALL_PLT` + `R_RISCV_RELAX`; at the tags that
relax, linking with the default settings produces a single `jal` and every
subsequent instruction address is 4 lower than in the `--no-relax` output,
which retains `auipc`+`jalr`.

## 6. `R_RISCV_ALIGN`

`R_RISCV_ALIGN` (43) appears at an offset where the assembler has inserted
`r_addend` bytes of nop padding, and asserts that the location following the
padding is aligned to `r_addend + 1`-rounded-up-to-a-power-of-two — in
practice, `r_addend` is the number of padding bytes emitted for an alignment of
`r_addend + 2` at minimum. Honouring it means:

1. Treating the padding region as deletable slack.
2. **After** all relaxation-driven deletions in the section are known,
   recomputing how many padding bytes are needed so that the address after the
   region satisfies the requested alignment, and deleting or keeping exactly
   that many bytes.
3. Filling any retained padding with valid nops (4-byte `01 00 01 00`, or the
   2-byte compressed nop `01 00` when only two bytes remain).
4. Not leaving the final address under-aligned.

`R_RISCV_ALIGN` therefore has to be resolved *last*, because its answer depends
on how much earlier relaxation removed — which is why it belongs to the same
milestone as relaxation (§5) and is graded there rather than at the milestone
that introduces the backend.

**Before that milestone the reference implementation does not accept the
relocation at all**, so there is nothing for an earlier milestone to grade and
no fixture an earlier milestone can build. Measured: a `clang-16 -march=rv64gc` object whose
`.text` holds one `R_RISCV_ALIGN` (addend 14, the padding for a `.p2align 4`
after a relaxable `call`) fails the link at `wild-0.6.0`, `0.7.0` and `0.8.0`
with `Failed copying from <obj>`, and links at `0.9.0` and `0.10.0`. At those
last two tags the alignment is re-established on the relaxing path only: with
`--no-relax` the label after the padding lands six bytes past its 16-byte
boundary and the link still succeeds, so item 4 is a property of the relaxing
path rather than an invariant of every link. Assemble RISC-V fixtures of your
own with `-mno-relax` if you want neither relocation in them.

## 7. `e_flags` and cross-input compatibility

`e_flags` for `EM_RISCV`:

| Bit(s) | Name | Meaning |
| --- | --- | --- |
| 0x0001 | `EF_RISCV_RVC` | the object contains compressed instructions |
| 0x0006 | `EF_RISCV_FLOAT_ABI` | mask for the float ABI field |
| 0x0000 | `EF_RISCV_FLOAT_ABI_SOFT` | value within that mask |
| 0x0002 | `EF_RISCV_FLOAT_ABI_SINGLE` | value within that mask |
| 0x0004 | `EF_RISCV_FLOAT_ABI_DOUBLE` | value within that mask |
| 0x0006 | `EF_RISCV_FLOAT_ABI_QUAD` | value within that mask |
| 0x0008 | `EF_RISCV_RVE` | reduced (16-register) base ISA |
| 0x0010 | `EF_RISCV_TSO` | total-store-order memory model required |
| 0x0020 | `EF_RISCV_RV64ILP32` | ILP32 on RV64 |

Merging rule the linker MUST implement:

* The output `e_flags` is the bitwise OR of every relocatable input's
  `e_flags`. `EF_RISCV_RVC` and `EF_RISCV_TSO` therefore propagate from any
  single input.
* With no relocatable inputs, the output `e_flags` is 0.
* Three fields MUST instead be *equal* across all inputs, and a mismatch MUST
  fail the link:
  * the `EF_RISCV_FLOAT_ABI` field (reference wording: `Float ABI flag
    mismatch`);
  * the `EF_RISCV_RVE` bit (`RVE flag mismatch`);
  * the `EF_RISCV_RV64ILP32` bit (`RV64ILP32 flag mismatch`).

Measured: clang-16 `-target riscv64-unknown-linux-gnu` with the default
`-march` emits `e_flags = 0x5` (RVC | double-float); linking that against an
object built `-mabi=lp64` (soft float, `e_flags = 0x1`) fails, and stderr names
the float-ABI mismatch.

## 8. TLS on RV64

The thread pointer is `tp` (`x4`). The TLS block for the executable begins at
`TP` with no negative offset region: `S - TP` for a local-exec symbol is the
symbol's offset within the TLS segment, plus the alignment padding the layout
inserts before `.tdata`.

| Model | Relocations | GOT content | Resolution |
| --- | --- | --- | --- |
| Local exec | `TPREL_HI20` / `TPREL_LO12_I` / `TPREL_LO12_S`, with `TPREL_ADD` marking the `add rd, rd, tp` | none | link-time constant `S + A - TP` |
| Initial exec | `TLS_GOT_HI20` + `PCREL_LO12_I` | one 8-byte slot holding `S - TP` | slot filled at link time for an executable, else `R_RISCV_TLS_TPREL64` |
| Global dynamic | `TLS_GD_HI20` + `PCREL_LO12_I`, then `CALL_PLT` to `__tls_get_addr` | two 8-byte slots: module id, then offset | `R_RISCV_TLS_DTPMOD64` and `R_RISCV_TLS_DTPREL64`, or filled at link time when the symbol is local to the executable |

`R_RISCV_TPREL_ADD` has no value and MUST NOT modify any bytes; it exists so a
relaxing linker can find the `add` that folds `tp` in.

## 9. CREL — the compact relocation format

A relocation section MAY have type `SHT_CREL = 0x40000014` instead of
`SHT_RELA`. Its contents are a byte stream, not an array, so `sh_entsize`
does not describe them and a linker MUST NOT derive an entry count from it:
the `count` field of the header in § 9.1 is the only authority. Producers
differ on what they leave in the field — the re-encoder that builds this
benchmark's CREL fixtures writes `sh_entsize = 1` — and a conforming linker
MUST accept the section whatever it holds. Measured against `wild-0.6.0` and
`wild-0.10.0`: the same object re-encoded with `sh_entsize = 1` and with
`sh_entsize = 0` links to byte-identical output, and both are byte-identical
to the `SHT_RELA` original.

Support for CREL is a hard requirement: **an input object whose relocation
sections are CREL MUST produce byte-for-byte the same output as the
equivalent `SHT_RELA` object.** This was verified on this machine by converting
a `SHT_RELA` RISC-V object to `SHT_CREL` with a first-party encoder and
comparing the two linked executables: they were identical, and both ran
correctly under `qemu-riscv64-static`. The comparison was repeated with
`shift` = 0 and `shift` = 3.

**CREL is a container format, not a RISC-V feature.** It is described in this
document because RV64 is where this benchmark first needed it, but nothing in
§§ 9.1-9.3 is architecture-specific: the stream encodes exactly the
`r_offset` / `r_info` / `r_addend` triples `SHT_RELA` encodes, for whatever
machine the object was compiled for. A linker MUST therefore accept
`SHT_CREL` input on **every** target it supports, not only on RV64, and an
implementation that wires CREL decoding into the RISC-V backend alone does not
conform. Measured at `wild-0.6.0` and `wild-0.10.0`: the same object re-encoded
to `SHT_CREL` links byte-identically to its `SHT_RELA` original for x86-64,
AArch64 **and** RV64 inputs. The graded CREL fixtures are **x86-64** objects
(`TEST_INTERFACE.md`, step-5 section) precisely because the format is
target-independent.

### 9.1 Header

The section begins with one ULEB128 value:

```
header = (count << 3) | (addend_present << 2) | shift
```

| Field | Bits | Meaning |
| --- | --- | --- |
| `shift` | 1:0 | log2 of the scale applied to every decoded offset |
| `addend_present` | 2 | 1 → entries carry addends (RELA semantics); 0 → REL semantics, no addends |
| `count` | 63:3 | number of relocation entries that follow |

Derived: `flag_bits = 3` when `addend_present` is 1, otherwise `2`.

### 9.2 Entries

Entries are delta-encoded against four running accumulators, all starting at
zero: `offset`, `symbol_index`, `type`, `addend`. Each entry begins with one
byte that packs the flags and the start of the offset delta:

* the low `flag_bits` bits are the flags: bit 0 = the symbol index changed,
  bit 1 = the type changed, bit 2 = the addend changed (only meaningful when
  `addend_present`);
* bits `flag_bits`..6 are the low bits of the offset delta;
* bit 7 (`0x80`) means the offset delta continues as a ULEB128, whose value is
  shifted left by `7 - flag_bits` and OR-ed in.

Then, for each set flag in order (symbol, type, addend), one SLEB128 delta
follows, which is added to the corresponding accumulator.

The relocation's final `r_offset` is `offset << shift`. Its `r_info` is built
from the accumulated `symbol_index` and `type` exactly as for `SHT_RELA`:
`(symbol_index << 32) | type` for ELF64. Its `r_addend` is the accumulated
addend, or absent under REL semantics.

### 9.3 Decoding algorithm

```
function decode_crel(bytes):
    header      = read_uleb128(bytes)
    count       = header >> 3
    addend_flag = (header >> 2) & 1
    shift       = header & 3
    flag_bits   = addend_flag ? 3 : 2

    offset = 0; symidx = 0; rtype = 0; addend = 0
    out = []

    repeat count times:
        b            = read_u8(bytes)
        flags        = b & ((1 << flag_bits) - 1)
        delta_offset = (b & 0x7f) >> flag_bits
        if b & 0x80:
            delta_offset |= read_uleb128(bytes) << (7 - flag_bits)

        offset += delta_offset
        if flags & 1: symidx += read_sleb128(bytes)
        if flags & 2: rtype  += read_sleb128(bytes)
        if addend_flag and (flags & 4):
                      addend += read_sleb128(bytes)

        out.append({ r_offset: offset << shift,
                     r_sym:    symidx,
                     r_type:   rtype,
                     r_addend: addend_flag ? addend : none })
    return out
```

Notes the implementation MUST honour:

* `symidx`, `rtype` and `addend` are *sticky*: when a flag bit is clear, the
  previous value is reused, it is not zero.
* The deltas are signed, so relocation entries need not be in increasing
  symbol-index or type order. Offsets, however, are produced in
  non-decreasing order by every producer, and the encoded offset delta is
  unsigned.
* All accumulators are 64-bit and wrap. `symidx` and `rtype` are truncated to
  32 bits when forming `r_info`.
* `count` is authoritative; the decoder MUST stop after `count` entries even if
  bytes remain, and MUST fail if the stream runs out early.
* A `SHT_CREL` section's `sh_link` and `sh_info` mean exactly what they mean
  for `SHT_RELA`: the symbol table and the section being relocated.

### 9.4 What the linker does not have to do

The graded corpus never asks the linker to *emit* CREL. Output relocation
sections are `SHT_RELA` (or `SHT_RELR` where that is separately required).
CREL is an input format only.
