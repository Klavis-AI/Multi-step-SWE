# LoongArch64 relocations

> The LoongArch64 ELF ABI as a linker sees it: relocation numbers and instruction fields, `pcalau12i` page arithmetic, GOT and TLS families, and `e_flags` compatibility.

## Provenance

Relocation numbers, names and semantics restate the LoongArch ELF ABI
specification (`loongson/la-abi-specs`, `laelf.adoc`, *Relocation types*,
<https://github.com/loongson/la-abi-specs/blob/release/laelf.adoc>) and the
instruction encodings restate the LoongArch Reference Manual, Volume 1
(<https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html>).
`e_flags` bit values were taken from the ABI's *ELF header* section and
cross-checked against the `object` crate's `elf` constants at version 0.38.1.

Everything written below as a MUST was verified on this machine: the supported
relocation set, the field placements, the bias values and the `e_flags` rule
were read from the reference implementation
(`linker-utils/src/loongarch64.rs`, `libwild/src/elf_loongarch64.rs`,
`libwild/src/elf.rs` and `libwild/src/elf_writer.rs` at tag `0.10.0`), and the
`PCALA_HI20`/`PCALA_LO12` arithmetic was confirmed by linking fixtures with
`clang-16 -target loongarch64-unknown-linux-gnu` and disassembling the result
with `llvm-objdump-16`.

**There is no second linker to compare against on this architecture.** Measured
fact, recorded here because it changes how a disagreement must be resolved:
`ld.lld-16` cannot link LoongArch objects at all. It does not know the
`elf64loongarch` emulation (`ld.lld-16: error: unknown emulation:
elf64loongarch`), and when invoked without `-m` it rejects the relocations
outright, one line per relocation, for example:

```
ld.lld-16: error: la.o:(function entry: .text+0x8): unknown relocation (71) against symbol g
ld.lld-16: error: la.o:(function entry: .text+0xc): unknown relocation (72) against symbol g
ld.lld-16: error: lamain.o:(.text+0x0): unknown relocation (66) against symbol entry
```

Types 66, 71 and 72 are `R_LARCH_B26`, `R_LARCH_PCALA_HI20` and
`R_LARCH_PCALA_LO12` — the three relocations every ordinary LoongArch function
uses. GNU `ld` 2.40 on this machine is likewise not configured for LoongArch.
Every LoongArch expectation in this document therefore comes from the ABI
specification plus the reference implementation, and the reference
implementation is the tie-breaker.

Symbols in expressions: `S` the symbol value, `A` the addend, `P` the address
of the relocated field, `GE` the address of the symbol's GOT entry, `TP` the
thread pointer.

## 1. Target basics

| Property | Value |
| --- | --- |
| `e_machine` | `EM_LOONGARCH` = 258 |
| Class / endianness | ELFCLASS64, ELFDATA2LSB |
| Default image base for a non-PIE executable | `0x400000` |
| Maximum page size used for segment alignment | `0x10000` |
| Common page size used in page arithmetic | `0x1000` |
| Relocation format in input | `SHT_RELA` only (implicit addends are not used) |
| PLT entry size | 16 bytes |
| GOT entry size | 8 bytes |
| `DTV` offset applied to `R_LARCH_TLS_DTPREL*` | 0 (unlike x86-64's 0x800 convention) |

Measured: a small executable placed its read-only `PT_LOAD` at `0x400000`, its
executable `PT_LOAD` at `0x410000` and its writable one at `0x430000`, all
`p_align` 0x10000.

## 2. Instruction field forms

All base instructions are 32 bits, little-endian. Field forms, named after the
shift the linker applies:

| Form | Bits written | Preserved-bit mask | Width |
| --- | --- | --- | --- |
| **Shift5** (`j20`) | 24:5 | `0xFE00_001F` | 20 |
| **Shift10** (`k12`) | 21:10 | `0xFFC0_03FF` | 12 |
| **Branch21** (`d5k16`) | low 16 bits of the value → 25:10; high 5 bits → 4:0 | `0xFC00_03E0` | 21 |
| **Branch26** (`d10k16`) | low 16 bits → 25:10; high 10 bits → 9:0 | `0xFC00_0000` | 26 |
| **Call36** | a *pair* of instructions, 8 bytes: `pcaddu18i` then `jirl` | low word `0xFE00_001F`, high word `0xFC00_03FF` | 36 |
| **Call30** | a pair of instructions, 8 bytes | low word `0xFE00_001F`, high word `0xFFF8_03FF` | 30 |

The "preserved-bit mask" is what the linker MUST AND the existing instruction
with before OR-ing in the new field: every bit outside the immediate survives.

Branch21 and Branch26 split the immediate: the *low* half goes into the high
bit positions. This is not a typo in the ABI; it follows the `d10k16`/`d5k16`
assembly formats where the 16-bit `k` field is closer to the opcode.

**Call36** writes across two instructions:

```
high20 = (imm36 + 0x8000) >> 16      → pcaddu18i, bits 24:5
low16  =  imm36 & 0xffff             → jirl,      bits 25:10
```

where `imm36 = (S + A - P) >> 2`. The `+ 0x8000` is required because `jirl`'s
16-bit displacement is signed; without it a target in the upper half of an
18-bit window is computed one step too low.

**Call30** writes `imm & 0x1ff` into bits 18:10 of the second instruction and
the remaining high bits into bits 24:5 of the first.

### 2.1 Field extraction

For a relocation whose size is written `bits [a, b)`, the linker computes the
full 64-bit value `V` first, then writes

```
extracted = (V >> a) & ((1 << (b - a)) - 1)
```

into the named instruction form. An `alignment` of *n* means `V` MUST be a
multiple of *n*; a range `signed(k)` means `V` MUST fit in a signed *k*-bit
field.

## 3. Relocation table

Relocations reachable from `clang-16` output, plus those the reference
implementation accepts. "Bias" is a constant added to `S + A` before any page
mask is applied.

### 3.1 Data and arithmetic

| # | Name | Value | Field | Checks |
| --- | --- | --- | --- | --- |
| 0 | `R_LARCH_NONE` | — | none | none |
| 1 | `R_LARCH_32` | `S + A` | 4 bytes | `[-2^31, 2^32)` |
| 2 | `R_LARCH_64` | `S + A` | 8 bytes | none |
| 47-51 | `R_LARCH_ADD8/16/24/32/64` | `*field + S + A` | 1/2/3/4/8 bytes | none |
| 52-56 | `R_LARCH_SUB8/16/24/32/64` | `*field - (S + A)` | 1/2/3/4/8 bytes | none |
| 105 | `R_LARCH_ADD6` | low 6 bits of `*field + S + A`; upper 2 bits preserved | 1 byte | none |
| 106 | `R_LARCH_SUB6` | low 6 bits of `*field - (S + A)`; upper 2 bits preserved | 1 byte | none |
| 107 | `R_LARCH_ADD_ULEB128` | records the minuend; writes nothing | — | none |
| 108 | `R_LARCH_SUB_ULEB128` | `(S₁+A₁) - (S₂+A₂)` rewritten as ULEB128 in place, same byte length | ≤8 bytes | none |

The ULEB pair is processed as a unit at the `SUB_ULEB128` relocation; the
`ADD_ULEB128` at the same offset MUST NOT write anything on its own.

### 3.2 Absolute address materialisation

A full 64-bit absolute address is built from four instructions, each patched by
one relocation. They carry disjoint bit slices of the *same* value `S + A`.

| # | Name | Bits of `S + A` | Field |
| --- | --- | --- | --- |
| 67 | `R_LARCH_ABS_HI20` | [12, 32) | Shift5 (`lu12i.w`) |
| 68 | `R_LARCH_ABS_LO12` | [0, 12) | Shift10 (`ori`) |
| 69 | `R_LARCH_ABS64_LO20` | [32, 52) | Shift5 (`lu32i.d`) |
| 70 | `R_LARCH_ABS64_HI12` | [52, 64) | Shift10 (`lu52i.d`) |

None of the four is range-checked: together they cover the whole 64-bit value.

### 3.3 PC-relative page addressing

| # | Name | Value | Field | Bias |
| --- | --- | --- | --- | --- |
| 71 | `R_LARCH_PCALA_HI20` | `page(S + A + 0x800) - page(P)`, bits [12, 32) | Shift5 (`pcalau12i`) | 0x800 |
| 72 | `R_LARCH_PCALA_LO12` | `(S + A)` bits [0, 12) | Shift10 (`addi.d`, `ld.*`, `st.*`) | 0 |
| 73 | `R_LARCH_PCALA64_LO20` | high-part extension, bits [32, 52) | Shift5 (`lu32i.d`) | see below |
| 74 | `R_LARCH_PCALA64_HI12` | high-part extension, bits [52, 64) | Shift10 (`lu52i.d`) | see below |
| 99 | `R_LARCH_32_PCREL` | `S + A - P` | 4 bytes | signed 32 |
| 109 | `R_LARCH_64_PCREL` | `S + A - P` | 8 bytes | none |
| 103 | `R_LARCH_PCREL20_S2` | `(S + A - P)` bits [2, 22) | Shift5 (`pcaddi`) | signed 22, align 4 |
| 128 | `R_LARCH_PCADD_HI20` | `S + A + 0x800 - P`, bits [12, 32) | Shift5 | 0x800 |

Here `page(x)` means `x & ~0xfff`.

**This is the rule that matters most.** `pcalau12i rd, imm20` computes

```
rd = (PC + sign_extend(imm20 << 12)) & ~0xfff
```

— it lands on a page boundary. The companion `addi.d`/`ld.d` immediate patched
by `PCALA_LO12` is a **signed** 12-bit field, range [-2048, 2047]. The HI20
must therefore be rounded **to the nearest page**, which is what the `+ 0x800`
bias does, and the LO12 field is the sign-extended low 12 bits of the target.

Contrast AArch64: `adrp` pairs with an `add` whose 12-bit immediate is
*unsigned*, so `R_AARCH64_ADR_PREL_PG_HI21` uses plain truncation,
`page(S + A) - page(P)`, with no bias. Copying the AArch64 formula to
LoongArch produces addresses that are correct only when the target's low 12
bits are below 0x800, which is most of the time — an implementation that gets
this wrong passes casual testing and fails on half its inputs.

Measured on this machine. A link placing `g` at `0x450ff8` with the
`pcalau12i` at `P = 0x4203d0` produced:

```
4203d0:  pcalau12i  $a0, 49
4203d4:  addi.d     $a0, $a0, -8
```

Check: `page(0x450ff8 + 0x800) - page(0x4203d0) = 0x451000 - 0x420000 =
0x31000`; `0x31000 >> 12 = 0x31 = 49`. The LO12 field is `0x450ff8 & 0xfff =
0xff8`, sign-extended to `-8`. The runtime result is
`((0x4203d0 + 49*4096) & ~0xfff) - 8 = 0x451000 - 8 = 0x450ff8` — exactly `g`.
With AArch64-style truncation the HI20 would have been 48 and the LO12 field
would have needed the unrepresentable value 4088.

A second measurement with `g` at `0x430460` and `P = 0x410418` gave
`pcalau12i $a0, 32` and `addi.d $a0, $a0, 1120`, which is the case where the
bias makes no difference.

`R_LARCH_PCALA64_LO20` and `R_LARCH_PCALA64_HI12` extend the same address to a
full 64-bit range using a four-instruction sequence. Their value is computed
with a different page rule, because the `pcalau12i` they extend sits four
instructions earlier:

```
V = ((S + A + 0x8000_0000 + (if (S + A) & 0x800 then 0x1000 - 0x1_0000_0000 else 0))
     & ~0xfff)
    - ((P - 8) & ~0xfff)
```

The `P - 8` accounts for the two instructions between the `pcalau12i` and the
`lu32i.d`. The `0x8000_0000` term and the conditional correction reproduce the
carry that the signed LO12 and the signed HI20 introduce. These two
relocations are not emitted by the default `clang-16` code model and are not
exercised by the graded corpus, but they MUST be accepted rather than
rejected.

### 3.4 Branches and calls

| # | Name | Value | Field | Checks |
| --- | --- | --- | --- | --- |
| 64 | `R_LARCH_B16` | `(S + A - P)` bits [2, 18) | Shift10 | signed 18, align 4 |
| 65 | `R_LARCH_B21` | `(S + A - P)` bits [2, 23) | Branch21 | signed 23, align 4 |
| 66 | `R_LARCH_B26` | `(L + A - P)` bits [2, 28) | Branch26 (`b`, `bl`) | signed 28, align 4 |
| 110 | `R_LARCH_CALL36` | `(L + A - P)` bits [2, 38) | Call36 (`pcaddu18i`+`jirl`) | signed 38, align 4 |
| 127 | `R_LARCH_CALL30` | `(S + A - P)` bits [2, 19) | Call30 | none |

`B26` and `CALL36` resolve through the PLT (`L`) when the symbol is
interposable, and directly to the symbol otherwise. `B16`, `B21` and `CALL30`
always target the symbol itself.

A `bl` to a symbol beyond ±128 MiB MUST fail the link naming the symbol and the
relocation; there is no range-extension thunk requirement at any graded step.

### 3.5 GOT

| # | Name | Value | Field | Bias |
| --- | --- | --- | --- | --- |
| 75 | `R_LARCH_GOT_PC_HI20` | `page(GE + 0x800) - page(P)`, bits [12, 32) | Shift5 (`pcalau12i`) | 0x800 |
| 76 | `R_LARCH_GOT_PC_LO12` | `GE` bits [0, 12) | Shift10 (`ld.d`) | 0 |
| 77 | `R_LARCH_GOT64_PC_LO20` | high-part extension of `GE`, bits [32, 52) | Shift5 | see §3.3 |
| 78 | `R_LARCH_GOT64_PC_HI12` | high-part extension of `GE`, bits [52, 64) | Shift10 | see §3.3 |

Note that the GOT pair's page arithmetic masks the **GOT entry address** and
the **place**, not the symbol: the `pcalau12i` names the page containing the
GOT slot.

The non-PC-relative GOT family (`R_LARCH_GOT_HI20` 79, `R_LARCH_GOT_LO12` 80,
`R_LARCH_GOT64_LO20` 81, `R_LARCH_GOT64_HI12` 82) is **not** supported and MUST
be rejected with the relocation named.

### 3.6 TLS

The thread pointer register is `$tp` (`$r2`). `tp` points at the start of the
aligned TLS block; the DTV offset applied to `R_LARCH_TLS_DTPREL*` is 0.

| # | Name | Model | Value | Field | Bias |
| --- | --- | --- | --- | --- | --- |
| 83 | `R_LARCH_TLS_LE_HI20` | LE | `(S + A - TP)` [12, 32) | Shift5 | 0 |
| 84 | `R_LARCH_TLS_LE_LO12` | LE | `(S + A - TP)` [0, 12) | Shift10 | 0 |
| 85 | `R_LARCH_TLS_LE64_LO20` | LE | `(S + A - TP)` [32, 52) | Shift5 | 0 |
| 86 | `R_LARCH_TLS_LE64_HI12` | LE | `(S + A - TP)` [52, 64) | Shift10 | 0 |
| 121 | `R_LARCH_TLS_LE_HI20_R` | LE, relaxable | as 83 | Shift5 | 0x800 |
| 122 | `R_LARCH_TLS_LE_ADD_R` | LE, relaxable | marker, writes nothing | — | — |
| 123 | `R_LARCH_TLS_LE_LO12_R` | LE, relaxable | as 84 | Shift10 | 0 |
| 87 | `R_LARCH_TLS_IE_PC_HI20` | IE | `page(GE + 0x800) - page(P)` [12, 32) | Shift5 | 0x800 |
| 88 | `R_LARCH_TLS_IE_PC_LO12` | IE | `GE` [0, 12) | Shift10 | 0 |
| 89 | `R_LARCH_TLS_IE64_PC_LO20` | IE | high-part extension | Shift5 | — |
| 90 | `R_LARCH_TLS_IE64_PC_HI12` | IE | high-part extension | Shift10 | — |
| 91-94 | `R_LARCH_TLS_IE_HI20` / `IE_LO12` / `IE64_LO20` / `IE64_HI12` | IE, absolute GOT addressing | GOT-slot address slices | Shift5 / Shift10 | 0 |
| 95 | `R_LARCH_TLS_LD_PC_HI20` | LD | `page(GE + 0x800) - page(P)` [12, 32) | Shift5 | 0x800 |
| 96 | `R_LARCH_TLS_LD_HI20` | LD | GOT-slot address [12, 32) | Shift5 | 0 |
| 97 | `R_LARCH_TLS_GD_PC_HI20` | GD | `page(GE + 0x800) - page(P)` [12, 32) | Shift5 | 0x800 |
| 98 | `R_LARCH_TLS_GD_HI20` | GD | GOT-slot address [12, 32) | Shift5 | 0 |
| 111 | `R_LARCH_TLS_DESC_PC_HI20` | TLSDESC | `page(GE + 0x800) - page(P)` [12, 32) | Shift5 | 0x800 |
| 112 | `R_LARCH_TLS_DESC_PC_LO12` | TLSDESC | `GE` [0, 12) | Shift10 | 0 |
| 113 | `R_LARCH_TLS_DESC64_PC_LO20` | TLSDESC | high-part extension | Shift5 | — |
| 114 | `R_LARCH_TLS_DESC64_PC_HI12` | TLSDESC | high-part extension | Shift10 | — |
| 119 | `R_LARCH_TLS_DESC_LD` | TLSDESC | marker, writes nothing | — | — |
| 120 | `R_LARCH_TLS_DESC_CALL` | TLSDESC | the descriptor call site | — | — |
| 8 | `R_LARCH_TLS_DTPREL32` | debug | `S + A` relative to the module's TLS block | 4 bytes | — |
| 9 | `R_LARCH_TLS_DTPREL64` | debug | same | 8 bytes | — |

The ABI's local-dynamic model is known to be under-specified; `TLS_LD_*` is
handled as if it were general dynamic, which is what the reference
implementation does. A general-dynamic pair consumes two consecutive GOT slots
(module id, then offset).

`R_LARCH_TLS_LD_PCREL20_S2` (124), `R_LARCH_TLS_GD_PCREL20_S2` (125) and
`R_LARCH_TLS_DESC_PCREL20_S2` (126) are defined by the ABI but are unused; they
MUST be rejected.

Dynamic relocation types used in output: `R_LARCH_64` (2),
`R_LARCH_RELATIVE` (3), `R_LARCH_COPY` (4), `R_LARCH_JUMP_SLOT` (5),
`R_LARCH_TLS_DTPMOD64` (7), `R_LARCH_TLS_DTPREL64` (9),
`R_LARCH_TLS_TPREL64` (11), `R_LARCH_IRELATIVE` (12),
`R_LARCH_TLS_DESC64` (14). `R_LARCH_64` is the absolute 64-bit form: it is
what a GOT slot for an undefined symbol, or a writable `.data` word holding
the address of a preemptible symbol, is relocated by in a dynamic link.

### 3.7 Markers and unsupported families

| # | Name | Treatment |
| --- | --- | --- |
| 100 | `R_LARCH_RELAX` | accepted; writes nothing. It marks a relaxable site |
| 102 | `R_LARCH_ALIGN` | accepted; writes nothing. Unlike RISC-V, **no bytes are deleted**, so the padding the assembler emitted stays as-is |
| 20-46 | `R_LARCH_MARK_*` and the `R_LARCH_SOP_*` stack-machine family | MUST be rejected — these are the obsolete pre-2.0 relocation scheme |
| 57, 58 | `R_LARCH_GNU_VTINHERIT`, `R_LARCH_GNU_VTENTRY` | MUST be rejected |
| 101 | `R_LARCH_DELETE` | MUST be rejected |
| 104 | `R_LARCH_CFA` | MUST be rejected |
| 79-82 | `R_LARCH_GOT_HI20` family | MUST be rejected |
| 115-118 | `R_LARCH_TLS_DESC_HI20` family (non-PC-relative) | MUST be rejected |
| 129-139 | `R_LARCH_PCADD_LO12` and the `*_PCADD_*` families | MUST be rejected |

Rejection means the link fails. The wording is not prescribed and none of
these families is graded at this milestone: measured, the reference at this
step does not produce a clean diagnostic for any of them -- it aborts with a
panic (exit status 101) from its relocation dispatch -- so a case that asserted
a diagnosed error here would be grading a crash. No case in this step's corpus
links an object carrying one of these relocation types, and none should be
added until the reference diagnoses them.

## 4. Relaxation

LoongArch relaxation in the reference implementation never deletes bytes. The
only transformations are:

| Precondition | Effect |
| --- | --- |
| `R_LARCH_B26` against a non-interposable symbol | resolve directly to the symbol instead of through the PLT; the instruction encoding is unchanged |
| `R_LARCH_CALL36` against a non-interposable symbol | the same |

Both only apply inside a section with `SHF_EXECINSTR`. There is also a
"replace with nop" primitive that writes the 4-byte canonical nop
`03 40 00 00`, used where an instruction becomes dead.

Because no bytes are deleted, `R_LARCH_ALIGN` needs no recomputation and
symbol values never shift. An implementation MAY treat `R_LARCH_RELAX` as a
complete no-op and still be correct.

## 5. PLT

Each PLT entry is 16 bytes:

```
pcalau12i  $t3, %pc_hi20(&got_plt[n])     # patched: page(GOT+0x800) - page(PLT)
ld.d       $t3, $t3, %pc_lo12(&got_plt[n])# patched: GOT & 0xfff
jirl       $t1, $t3, 0
break      0
```

The first instruction's Shift5 field receives
`((GOT_entry + 0x800) & ~0xfff) - (PLT_entry & ~0xfff)` shifted right by 12;
the second's Shift10 field receives `GOT_entry & 0xfff`. The trailing `break`
pads the entry to 16 bytes and traps if control ever falls through.

## 6. `e_flags`

| Bit(s) | Name | Meaning |
| --- | --- | --- |
| 0x0007 | `EF_LARCH_ABI_MODIFIER_MASK` | mask for the base ABI field |
| 0x0001 | `EF_LARCH_ABI_SOFT_FLOAT` | value in that field |
| 0x0002 | `EF_LARCH_ABI_SINGLE_FLOAT` | value in that field |
| 0x0003 | `EF_LARCH_ABI_DOUBLE_FLOAT` | value in that field |
| 0x0040 | `EF_LARCH_OBJABI_V1` | object-file ABI version 1 (the current relocation scheme) |

**Merging rule: every relocatable input's `e_flags` MUST be identical.** This is
strictly stronger than the RISC-V rule (which ORs most bits). With no
relocatable inputs the output `e_flags` is 0; otherwise it is the single common
value. A mismatch MUST fail the link. The diagnostic's wording is not
prescribed and is not graded: whether it names the two differing values is the
implementation's choice.

Measured: `clang-16 -target loongarch64-unknown-linux-gnu` emits `e_flags =
0x43` (`EF_LARCH_ABI_DOUBLE_FLOAT | EF_LARCH_OBJABI_V1`). Linking that against
an otherwise identical object whose `e_flags` was patched to `0x41`
(soft-float) fails with a non-zero status. The reference's own message changed
across releases -- at this milestone's reference it is `non-unique e_flags`
with no values, and the two values (`67, 65` in decimal) are appended only in a
later release -- which is exactly why only the failure, and not the text, is
required. Note that `clang-16` on this machine ignores `-mabi=lp64s` for this
target and still emits `0x43`, so the mismatch fixture has to be produced by
editing the `e_flags` word at file offset `0x30` directly.

The output executable's `e_flags` equals the inputs' common value — measured
`0x43` in and `0x43` out.

## 7. Command-line requirements

* `-m elf64loongarch` selects this target. Without it, a LoongArch input is
  rejected for having the wrong architecture, and the diagnostic names both the
  input's architecture and the expected one. (Measured: `has incompatible
  architecture: loongarch64, expecting x86_64`.)
* The `-z x86-64-v2` / `-v3` / `-v4` options are accepted on every target. They
  simply OR the requested level into `GNU_PROPERTY_X86_ISA_1_NEEDED`; there is
  no comparison against the inputs' ISA level and no error path. Do not make
  them fail on LoongArch.
* `--relax` / `--no-relax` are accepted; on this target the difference is not
  observable in the output bytes.
