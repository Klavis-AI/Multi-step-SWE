# AArch64 relocations

> The AArch64 ELF relocation set: what each type computes, which instruction field it writes, and when it overflows.

## Provenance

This restates *ELF for the Arm 64-bit Architecture (AArch64)* — `aaelf64`,
document `IHI0056`, the 2024Q3 edition at
<https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst> —
narrowed to the LP64 ABI, little-endian, `ET_REL` input and `ET_EXEC`/`ET_DYN`
output. Instruction-field positions are from the *Arm Architecture Reference
Manual for A-profile* (`DDI 0487`), chapter C6, at the section numbers cited
in the tables. Relocation numbers were checked against the operating system's
`<elf.h>`.

Deliberately narrowed: ILP32 (`R_AARCH64_P32_*`, numbers 1–188) is out of
scope, as is big-endian AArch64, `R_AARCH64_NONE`-as-`R_AARCH64_WITHDRAWN`
history, the memory-tagging (`MTE`) relocations and program headers, and
`R_AARCH64_AUTH_*` (`PAuth` ABI). The TLS relocations are listed here with
their fields; what the *models* mean and how they relax is in
*Thread-local storage*.

## 1. Notation

| symbol | meaning |
| --- | --- |
| `S` | the run-time address of the symbol named by the relocation |
| `A` | the relocation's `r_addend` |
| `P` | the address of the place being relocated (`r_offset` plus the output address of its section) |
| `GOT` | the address of the global offset table |
| `G(x)` | the address of the GOT slot allocated for `x` |
| `L` | the address of the PLT entry for `S`. When no PLT entry is needed — a static link, or a definition that cannot be preempted — `L` is `S` itself and nothing is synthesised (same rule as `03-x86-64-relocations.md` § 1). |
| `TP` | the value the thread pointer (`TPIDR_EL0`) will have |
| `DTV` | the dynamic thread vector; `DTPREL(S)` is `S`'s offset inside its module's TLS block |
| `Page(x)` | `x & ~0xfff` — the 4 KiB page containing `x` |
| `GDAT(x)` | the GOT slot pair holding the general-dynamic descriptor for `x` |
| `TLSDESC(x)` | the GOT slot pair holding the TLS descriptor for `x` |

All AArch64 relocations are `Elf64_Rela`: the addend is in the relocation
entry and the field in the instruction is *overwritten*, never added to.
A linker MUST NOT read the existing immediate and add to it.

Every AArch64 instruction is exactly 4 bytes, little-endian. "Field position"
below is the bit range within that 32-bit word.

## 2. Instruction field encodings

Several relocations share an immediate-field shape. These are the shapes:

| shape | instruction | immediate | placement |
| --- | --- | --- | --- |
| `Adr` | `adr`, `adrp` (C6.2.12, C6.2.13) | 21 bits | bits 1:0 of the immediate go to instruction bits 30:29 (`immlo`); bits 20:2 go to instruction bits 23:5 (`immhi`) |
| `Movkz` | `movz`, `movk` (C6.2.252, C6.2.254) | 16 bits | instruction bits 20:5 |
| `Movnz` | `movn`/`movz` pair (C6.2.253) | 16 bits | instruction bits 20:5; bit 30 selects `movz` (set) or `movn` (clear), and for a negative value the linker writes the one's complement with `movn` |
| `Ldr` | `ldr` literal (C6.2.192) | 19 bits | instruction bits 23:5 |
| `LdrRegister` | `ldr` register-offset (unsigned imm12) | 12 bits | instruction bits 21:10 |
| `Add` | `add` immediate (C6.2.5) | 12 bits | instruction bits 21:10 |
| `LdSt` | `ldr`/`str` unsigned offset (C6.2.208, C6.2.383) | 12 bits | instruction bits 21:10 |
| `TstBr` | `tbz`, `tbnz` (C6.2.438) | 14 bits | instruction bits 18:5 |
| `Bcond` | `b.cond`, `cbz`, `cbnz` (C6.2.34) | 19 bits | instruction bits 23:5 |
| `JumpCall` | `b`, `bl` (C6.2.33) | 26 bits | instruction bits 25:0 |

The "field" column in the tables below is written `[hi:lo]`, meaning bits `lo`
through `hi` **of the computed value** are the ones that get encoded. So
`[32:12]` means "take value bits 32 down to 12, giving a 21-bit quantity, and
encode it with the `Adr` shape"; `[11:3]` for an `LdSt` relocation means "take
value bits 11 down to 3, giving 9 bits, and encode them in imm12", which is
the same thing as scaling the low 12 bits by the 8-byte access size.

## 3. Overflow

A relocation whose `AllowedRange` column below is not "none" MUST have its
computed value checked *before* the field is extracted. If the value falls
outside the half-open interval given, the link MUST fail with a non-zero exit
status and at least one line on stderr that names **the relocation type** and
**the symbol**, and says the value is out of range — for instance by printing
the offending value or the limit. No prefix, phrasing or particular exit value
is graded, only that the status is non-zero and those tokens are present. A
truncated field written silently is the worst possible outcome and MUST NOT
happen.

Types whose names end in `_NC` ("no check") are the deliberately unchecked
halves of a pair: the checked partner validates the whole value and the `_NC`
member contributes low-order bits. Those MUST NOT be range-checked.

## 4. Data relocations

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 0 | `R_AARCH64_NONE` | — | none | none |
| 257 | `R_AARCH64_ABS64` | `S + A` | whole 8-byte word | none |
| 258 | `R_AARCH64_ABS32` | `S + A` | whole 4-byte word | value ∉ `[-2^31, 2^32)` |
| 259 | `R_AARCH64_ABS16` | `S + A` | whole 2-byte word | value ∉ `[-2^15, 2^16)` |
| 260 | `R_AARCH64_PREL64` | `S + A - P` | whole 8-byte word | none |
| 261 | `R_AARCH64_PREL32` | `S + A - P` | whole 4-byte word | value ∉ `[-2^31, 2^32)` |
| 262 | `R_AARCH64_PREL16` | `S + A - P` | whole 2-byte word | value ∉ `[-2^15, 2^16)` |
| 314 | `R_AARCH64_PLT32` | `L + A - P` | whole 4-byte word | value ∉ `[-2^31, 2^31)` |

`R_AARCH64_ABS64` in a `.data` word MUST store the final 64-bit address of the
target **with the addend included**, so `.quad sym + 8` produces
`address_of(sym) + 8`. In a static, non-PIE executable this is the whole story:
the word is final, and the output MUST contain no `R_AARCH64_RELATIVE` entry
for it. In `ET_DYN` output the rules of *Producing dynamic output* apply
instead.

## 5. Immediate-generation (`MOVW`) relocations

These build a 16-, 32-, 48- or 64-bit constant a quarter at a time with
`movz`/`movk`, or a signed one with `movn`/`movz`.

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 263 | `R_AARCH64_MOVW_UABS_G0` | `S + A` | `[15:0]`, `Movkz` | value ∉ `[0, 2^16)` |
| 264 | `R_AARCH64_MOVW_UABS_G0_NC` | `S + A` | `[15:0]`, `Movkz` | none |
| 265 | `R_AARCH64_MOVW_UABS_G1` | `S + A` | `[31:16]`, `Movkz` | value ∉ `[0, 2^32)` |
| 266 | `R_AARCH64_MOVW_UABS_G1_NC` | `S + A` | `[31:16]`, `Movkz` | none |
| 267 | `R_AARCH64_MOVW_UABS_G2` | `S + A` | `[47:32]`, `Movkz` | value ∉ `[0, 2^48)` |
| 268 | `R_AARCH64_MOVW_UABS_G2_NC` | `S + A` | `[47:32]`, `Movkz` | none |
| 269 | `R_AARCH64_MOVW_UABS_G3` | `S + A` | `[63:48]`, `Movkz` | none |
| 270 | `R_AARCH64_MOVW_SABS_G0` | `S + A` | `[15:0]`, `Movnz` | value ∉ `(-2^16, 2^16)` |
| 271 | `R_AARCH64_MOVW_SABS_G1` | `S + A` | `[31:16]`, `Movnz` | value ∉ `(-2^32, 2^32)` |
| 272 | `R_AARCH64_MOVW_SABS_G2` | `S + A` | `[47:32]`, `Movnz` | value ∉ `(-2^48, 2^48)` |
| 287 | `R_AARCH64_MOVW_PREL_G0` | `S + A - P` | `[15:0]`, `Movnz` | none |
| 288 | `R_AARCH64_MOVW_PREL_G0_NC` | `S + A - P` | `[15:0]`, `Movkz` | none |
| 289 | `R_AARCH64_MOVW_PREL_G1` | `S + A - P` | `[31:16]`, `Movnz` | none |
| 290 | `R_AARCH64_MOVW_PREL_G1_NC` | `S + A - P` | `[31:16]`, `Movkz` | none |
| 291 | `R_AARCH64_MOVW_PREL_G2` | `S + A - P` | `[47:32]`, `Movnz` | none |
| 292 | `R_AARCH64_MOVW_PREL_G2_NC` | `S + A - P` | `[47:32]`, `Movkz` | none |
| 293 | `R_AARCH64_MOVW_PREL_G3` | `S + A - P` | `[63:48]`, `Movnz` | none |

For a `Movnz` field with a negative value the linker writes the one's
complement of the group into the immediate and leaves instruction bit 30
clear, turning the instruction into `movn`; for a non-negative value it writes
the group as-is and sets bit 30, making it `movz`.

## 6. PC-relative address generation

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 273 | `R_AARCH64_LD_PREL_LO19` | `S + A - P` | `[20:2]`, `Ldr` | value ∉ `[-2^20, 2^20)` |
| 274 | `R_AARCH64_ADR_PREL_LO21` | `S + A - P` | `[20:0]`, `Adr` | value ∉ `[-2^20, 2^20)` |
| 275 | `R_AARCH64_ADR_PREL_PG_HI21` | `Page(S + A) - Page(P)` | `[32:12]`, `Adr` | value ∉ `[-2^32, 2^32)` |
| 276 | `R_AARCH64_ADR_PREL_PG_HI21_NC` | `Page(S + A) - Page(P)` | `[32:12]`, `Adr` | none |
| 277 | `R_AARCH64_ADD_ABS_LO12_NC` | `S + A` | `[11:0]`, `Add` | none |

`R_AARCH64_ADR_PREL_PG_HI21` is the AArch64 addressing idiom. `adrp`
computes `Page(PC) + (imm21 << 12)`, so the relocation must supply the
*difference between pages*, not between addresses: `Page(S + A) - Page(P)`.
Masking off the low 12 bits of **both** operands is essential — computing
`Page(S + A - P)` is wrong whenever `S + A` and `P` sit at different offsets
within their pages, and produces an address one page off roughly half the
time.

`R_AARCH64_ADD_ABS_LO12_NC` then supplies the low 12 bits of the *address*,
`(S + A) & 0xfff`, into the `add`'s imm12. The pair

```
  adrp x0, sym          // R_AARCH64_ADR_PREL_PG_HI21, sym
  add  x0, x0, :lo12:sym // R_AARCH64_ADD_ABS_LO12_NC, sym
```

MUST together reconstruct exactly `S + A`, for every `S` — including symbols
that are not page-aligned. The `_NC` on the `add` is what makes this work: the
`adrp` carries the checked range and the `add` contributes the residue.

## 7. Load/store immediate-offset relocations

The `ldr`/`str` unsigned-offset form encodes its offset **scaled by the access
size**: `ldr x0, [x1, #imm12]` addresses `x1 + imm12 * 8`. The relocation must
therefore drop the low bits of the address that the hardware supplies
implicitly. This is the scaling rule, and it is the single most common
AArch64 relocation bug.

| № | name | access size | expression | field | overflow |
| --- | --- | --- | --- | --- | --- |
| 278 | `R_AARCH64_LDST8_ABS_LO12_NC` | 1 byte | `S + A` | `[11:0]`, `LdSt` | none |
| 284 | `R_AARCH64_LDST16_ABS_LO12_NC` | 2 bytes | `S + A` | `[11:1]`, `LdSt` | none |
| 285 | `R_AARCH64_LDST32_ABS_LO12_NC` | 4 bytes | `S + A` | `[11:2]`, `LdSt` | none |
| 286 | `R_AARCH64_LDST64_ABS_LO12_NC` | 8 bytes | `S + A` | `[11:3]`, `LdSt` | none |
| 299 | `R_AARCH64_LDST128_ABS_LO12_NC` | 16 bytes | `S + A` | `[11:4]`, `LdSt` | none |

Concretely, with `V = (S + A) & 0xfff`, the imm12 written is `V >> 0`,
`V >> 1`, `V >> 2`, `V >> 3` and `V >> 4` respectively. A target whose address
is not a multiple of the access size cannot be encoded; the low bits are
simply lost, which is why these are all `_NC` and why the compiler only emits
them against sufficiently aligned symbols.

## 8. Control-flow relocations

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 279 | `R_AARCH64_TSTBR14` | `S + A - P` | `[16:2]`, `TstBr` | value ∉ `[-2^15, 2^15)` |
| 280 | `R_AARCH64_CONDBR19` | `S + A - P` | `[20:2]`, `Bcond` | value ∉ `[-2^20, 2^20)` |
| 282 | `R_AARCH64_JUMP26` | `S + A - P` | `[27:2]`, `JumpCall` | value ∉ `[-2^27, 2^27)` |
| 283 | `R_AARCH64_CALL26` | `S + A - P` | `[27:2]`, `JumpCall` | value ∉ `[-2^27, 2^27)` |

All four are word-scaled: the encoded immediate is the byte displacement
divided by 4, so a 26-bit field spans ±2²⁷ bytes = **±128 MiB**, a 19-bit
field ±1 MiB, and a 14-bit field ±32 KiB.

`R_AARCH64_JUMP26` (`b`) and `R_AARCH64_CALL26` (`bl`) differ only in the
instruction they patch; the linker treats them identically. Both are
*PLT-relative*: when the target is preemptible or otherwise needs indirection,
the value used is `L + A - P`, the address of the target's PLT entry, not the
symbol's own address.

### 8.1 Range extension

A `CALL26` or `JUMP26` whose target is further than ±128 MiB away cannot be
encoded. The correct long-term answer is a **range-extension thunk**: a small
stub placed within reach of the call site that performs an unrestricted
indirect branch to the real target, with the branch redirected to the stub.
Building thunks was a later milestone; from milestone 8 (`wild` 0.9.0)
range-extension thunks are implemented and graded — see that milestone's
`TEST_INTERFACE.md` for its scope and constraints.

What a thunk is built *for* is narrower than "any branch that cannot reach",
and the difference is measured. A distance that arises from **section
content** — enough code or padding between the caller and the callee — is
bridged: from milestone 8 the linker synthesises a stub and retargets the
branch at it, and that is the form the corpus grades. A distance that arises
from **explicit placement by a linker script** that pins the two sections is
*not* bridged, even at milestones 8 and 9. Measured at `0.9.0`, it is refused
with

    Branch relocation out of range by <N> for symbol <name> but no thunk
    allocated.

`--section-start` on its own does **not** produce such a distance, and a case
written that way would prove nothing: the reference lays the remaining
sections out adjacent to the pinned one, so the branch stays in range and the
link succeeds. Only a script that pins both sections separates them for real.

Wherever a thunk is not allocated, a linker MUST still perform the range check
and MUST fail the link with a diagnostic obeying section 3: it names the
relocation type (`R_AARCH64_CALL26` or `R_AARCH64_JUMP26`) and the symbol, and
states that the value is out of range — either in words, or by printing the
offending displacement or the bound `-134217728`/`134217727`. Emitting a
truncated branch is never acceptable. At milestones before thunks exist that
covers every out-of-range branch — measured at milestone 7 (`0.8.0`), the
refusal reads `Relocation <N> outside of bounds [-134217728, 134217728)` —
and from milestone 8 it covers the cases the reference still refuses. Each
milestone's `TEST_INTERFACE.md` says which of the two forms it grades. The
check is never optional: an unencodable branch either becomes a thunk or
becomes an error, but it is never silently wrong.

## 9. GOT relocations

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 300 | `R_AARCH64_MOVW_GOTOFF_G0` | `G(S) - GOT` | `[15:0]`, `Movnz` | none |
| 301 | `R_AARCH64_MOVW_GOTOFF_G0_NC` | `G(S) - GOT` | `[15:0]`, `Movkz` | none |
| 302 | `R_AARCH64_MOVW_GOTOFF_G1` | `G(S) - GOT` | `[31:16]`, `Movnz` | none |
| 303 | `R_AARCH64_MOVW_GOTOFF_G1_NC` | `G(S) - GOT` | `[31:16]`, `Movkz` | none |
| 304 | `R_AARCH64_MOVW_GOTOFF_G2` | `G(S) - GOT` | `[47:32]`, `Movnz` | none |
| 305 | `R_AARCH64_MOVW_GOTOFF_G2_NC` | `G(S) - GOT` | `[47:32]`, `Movkz` | none |
| 306 | `R_AARCH64_MOVW_GOTOFF_G3` | `G(S) - GOT` | `[63:48]`, `Movnz` | none |
| 307 | `R_AARCH64_GOTREL64` | `S + A - GOT` | whole 8-byte word | none |
| 308 | `R_AARCH64_GOTREL32` | `S + A - GOT` | whole 4-byte word | value ∉ `[-2^31, 2^31)` |
| 309 | `R_AARCH64_GOT_LD_PREL19` | `G(S) + A - P` | `[20:2]`, `Ldr` | value ∉ `[-2^20, 2^20)` |
| 310 | `R_AARCH64_LD64_GOTOFF_LO15` | `G(S) - GOT` | `[14:3]`, `LdSt` | value ∉ `[0, 2^15)` |
| 311 | `R_AARCH64_ADR_GOT_PAGE` | `Page(G(S) + A) - Page(P)` | `[32:12]`, `Adr` | value ∉ `[-2^32, 2^32)` |
| 312 | `R_AARCH64_LD64_GOT_LO12_NC` | `G(S) + A` | `[11:3]`, `LdSt` | none |
| 313 | `R_AARCH64_LD64_GOTPAGE_LO15` | `G(S) + A - Page(GOT)` | `[14:3]`, `LdSt` | value ∉ `[0, 2^15)` |

The ordinary GOT idiom is the pair

```
  adrp x0, :got:sym           // R_AARCH64_ADR_GOT_PAGE
  ldr  x0, [x0, :got_lo12:sym] // R_AARCH64_LD64_GOT_LO12_NC
```

which loads the GOT slot's *contents*. `R_AARCH64_ADR_GOT_PAGE` masks the
pages of the **GOT slot address** and the place; `R_AARCH64_LD64_GOT_LO12_NC`
supplies `G(S) & 0xfff` scaled by 8, exactly like `LDST64_ABS_LO12_NC` but
against the slot rather than the symbol. Both of these allocate a GOT slot for
`S` if one does not exist.

`R_AARCH64_GOT_LD_PREL19` is the single-instruction form, `ldr x0, :got:sym`,
which loads the slot's contents with a PC-relative literal load and so needs
the GOT within ±1 MiB.

## 10. Dynamic relocations

These appear in `.rela.dyn` and `.rela.plt` of a dynamic image, never in an
input object.

| № | name | computed by the loader as | placed in |
| --- | --- | --- | --- |
| 1024 | `R_AARCH64_COPY` | copy `st_size` bytes from the shared object's definition to `r_offset` | `.rela.dyn` |
| 1025 | `R_AARCH64_GLOB_DAT` | `*r_offset = S + A` | `.rela.dyn` |
| 1026 | `R_AARCH64_JUMP_SLOT` | `*r_offset = S + A` | `.rela.plt` |
| 1027 | `R_AARCH64_RELATIVE` | `*r_offset = Delta + A`, where `Delta` is the load bias | `.rela.dyn` |
| 1028 | `R_AARCH64_TLS_DTPMOD` | `*r_offset = module id of S's module` | `.rela.dyn` |
| 1029 | `R_AARCH64_TLS_DTPREL` | `*r_offset = S's offset in its module's TLS block + A` | `.rela.dyn` |
| 1030 | `R_AARCH64_TLS_TPREL` | `*r_offset = S + A - TP` | `.rela.dyn` |
| 1031 | `R_AARCH64_TLSDESC` | fills a two-word TLS descriptor at `r_offset` | `.rela.dyn` |
| 1032 | `R_AARCH64_IRELATIVE` | call the resolver at `Delta + A`, store its result at `r_offset` | `.rela.dyn` or `.rela.plt` |

`R_AARCH64_COPY` exists for the case where a non-PIE executable takes the
address of a data object defined in a shared library: space is reserved in the
executable's `.bss`, the loader copies the initial value into it, and every
other module's reference is redirected there. Emitting copy relocations **for
this architecture** is not required by this task; an AArch64 link that refuses
them must still be able to read them. This carve-out is about AArch64 only —
on x86-64 copy relocations are required, and exactly when, is stated in
`spec/05-dynamic-linking.md` §13.2.

All dynamic relocation types are 8 bytes wide except `R_AARCH64_COPY`, which
has no field of its own.

A **static, non-PIE** AArch64 executable MUST contain none of these. In
particular no `R_AARCH64_RELATIVE` may appear merely because a `.data` word
was initialised with `R_AARCH64_ABS64`.

## 11. TLS relocations

The models and their code sequences are in *Thread-local storage*. Their
fields are:

### 11.1 General dynamic

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 512 | `R_AARCH64_TLSGD_ADR_PREL21` | `GDAT(S + A) - P` | `[20:0]`, `Adr` | value ∉ `[-2^20, 2^20)` |
| 513 | `R_AARCH64_TLSGD_ADR_PAGE21` | `Page(GDAT(S + A)) - Page(P)` | `[32:12]`, `Adr` | value ∉ `[-2^32, 2^32)` |
| 514 | `R_AARCH64_TLSGD_ADD_LO12_NC` | `GDAT(S + A)` | `[11:0]`, `Add` | none |
| 515 | `R_AARCH64_TLSGD_MOVW_G1` | `GDAT(S + A) - GOT` | `[32:16]`, `Movnz` | none |
| 516 | `R_AARCH64_TLSGD_MOVW_G0_NC` | `GDAT(S + A) - GOT` | `[15:0]`, `Movkz` | none |

### 11.2 Local dynamic

Numbers 517–521 (`TLSLD_ADR_PREL21`, `TLSLD_ADR_PAGE21`,
`TLSLD_ADD_LO12_NC`, `TLSLD_MOVW_G1`, `TLSLD_MOVW_G0_NC`) and 522
(`TLSLD_LD_PREL19`) address the module's own descriptor and have the same
fields as their `TLSGD` counterparts, with `GLDM(S)` — the module-id
descriptor — in place of `GDAT(S + A)`. `TLSLD_LD_PREL19` uses the `Ldr`
shape at `[20:0]` with range `[-2^20, 2^20)`.

Numbers 523–538 and 572–573 are the `DTPREL` group, which computes the
symbol's offset within its module's TLS block:

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 523 | `R_AARCH64_TLSLD_MOVW_DTPREL_G2` | `DTPREL(S + A)` | `[47:32]`, `Movnz` | none |
| 524 | `R_AARCH64_TLSLD_MOVW_DTPREL_G1` | `DTPREL(S + A)` | `[31:16]`, `Movnz` | none |
| 525 | `R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC` | `DTPREL(S + A)` | `[31:16]`, `Movkz` | none |
| 526 | `R_AARCH64_TLSLD_MOVW_DTPREL_G0` | `DTPREL(S + A)` | `[15:0]`, `Movnz` | none |
| 527 | `R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC` | `DTPREL(S + A)` | `[15:0]`, `Movkz` | none |
| 528 | `R_AARCH64_TLSLD_ADD_DTPREL_HI12` | `DTPREL(S + A)` | `[23:12]`, `Add` | value ∉ `[0, 2^24)` |
| 529 | `R_AARCH64_TLSLD_ADD_DTPREL_LO12` | `DTPREL(S + A)` | `[11:0]`, `Add` | value ∉ `[0, 2^12)` |
| 530 | `R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC` | `DTPREL(S + A)` | `[11:0]`, `Add` | none |
| 531 / 532 | `R_AARCH64_TLSLD_LDST8_DTPREL_LO12(_NC)` | `DTPREL(S + A)` | `[11:0]`, `LdSt` | `[0, 2^12)` / none |
| 533 / 534 | `R_AARCH64_TLSLD_LDST16_DTPREL_LO12(_NC)` | `DTPREL(S + A)` | `[11:1]`, `LdSt` | `[0, 2^12)` / none |
| 535 / 536 | `R_AARCH64_TLSLD_LDST32_DTPREL_LO12(_NC)` | `DTPREL(S + A)` | `[11:2]`, `LdSt` | `[0, 2^12)` / none |
| 537 / 538 | `R_AARCH64_TLSLD_LDST64_DTPREL_LO12(_NC)` | `DTPREL(S + A)` | `[11:3]`, `LdSt` | `[0, 2^12)` / none |
| 572 / 573 | `R_AARCH64_TLSLD_LDST128_DTPREL_LO12(_NC)` | `DTPREL(S + A)` | `[11:4]`, `LdSt` | `[0, 2^12)` / none |

### 11.3 Initial exec

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 539 | `R_AARCH64_TLSIE_MOVW_GOTTPREL_G1` | `G(TPREL(S + A)) - GOT` | `[31:16]`, `Movnz` | none |
| 540 | `R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC` | `G(TPREL(S + A)) - GOT` | `[15:0]`, `Movkz` | none |
| 541 | `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21` | `Page(G(TPREL(S + A))) - Page(P)` | `[32:12]`, `Adr` | value ∉ `[-2^32, 2^32)` |
| 542 | `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC` | `G(TPREL(S + A))` | `[11:3]`, `LdrRegister` | none |
| 543 | `R_AARCH64_TLSIE_LD_GOTTPREL_PREL19` | `G(TPREL(S + A)) - P` | `[20:2]`, `Ldr` | value ∉ `[-2^20, 2^20)` |

The GOT slot these name holds `S - TP`, written at load time by
`R_AARCH64_TLS_TPREL` or, when the value is known statically, by the linker.

### 11.4 Local exec

All compute `TPREL(S + A)`, the symbol's offset from the thread pointer.

| № | name | field | overflow |
| --- | --- | --- | --- |
| 544 | `R_AARCH64_TLSLE_MOVW_TPREL_G2` | `[47:32]`, `Movnz` | none |
| 545 | `R_AARCH64_TLSLE_MOVW_TPREL_G1` | `[31:16]`, `Movnz` | none |
| 546 | `R_AARCH64_TLSLE_MOVW_TPREL_G1_NC` | `[31:16]`, `Movkz` | none |
| 547 | `R_AARCH64_TLSLE_MOVW_TPREL_G0` | `[15:0]`, `Movnz` | none |
| 548 | `R_AARCH64_TLSLE_MOVW_TPREL_G0_NC` | `[15:0]`, `Movkz` | none |
| 549 | `R_AARCH64_TLSLE_ADD_TPREL_HI12` | `[23:12]`, `Add` | value ∉ `[0, 2^24)` |
| 550 | `R_AARCH64_TLSLE_ADD_TPREL_LO12` | `[11:0]`, `Add` | value ∉ `[0, 2^12)` |
| 551 | `R_AARCH64_TLSLE_ADD_TPREL_LO12_NC` | `[11:0]`, `Add` | none |
| 552 / 553 | `R_AARCH64_TLSLE_LDST8_TPREL_LO12(_NC)` | `[11:0]`, `LdSt` | `[0, 2^12)` / none |
| 554 / 555 | `R_AARCH64_TLSLE_LDST16_TPREL_LO12(_NC)` | `[11:1]`, `LdSt` | `[0, 2^12)` / none |
| 556 / 557 | `R_AARCH64_TLSLE_LDST32_TPREL_LO12(_NC)` | `[11:2]`, `LdSt` | `[0, 2^12)` / none |
| 558 / 559 | `R_AARCH64_TLSLE_LDST64_TPREL_LO12(_NC)` | `[11:3]`, `LdSt` | `[0, 2^12)` / none |
| 570 / 571 | `R_AARCH64_TLSLE_LDST128_TPREL_LO12(_NC)` | `[11:4]`, `LdSt` | `[0, 2^12)` / none |

The `HI12` + `LO12_NC` pair is the standard local-exec sequence:

```
  mrs x0, tpidr_el0
  add x0, x0, #:tprel_hi12:v, lsl #12   // R_AARCH64_TLSLE_ADD_TPREL_HI12
  add x0, x0, #:tprel_lo12_nc:v         // R_AARCH64_TLSLE_ADD_TPREL_LO12_NC
```

and MUST reconstruct `TP + TPREL(v)` exactly. Because AArch64 uses TLS
variant I, `TPREL(v)` is non-negative, which is why these fields are unsigned
and why `HI12` range-checks against `[0, 2^24)`.

### 11.5 TLS descriptors

| № | name | expression | field | overflow |
| --- | --- | --- | --- | --- |
| 560 | `R_AARCH64_TLSDESC_LD_PREL19` | `TLSDESC(S + A) - P` | `[20:2]`, `Ldr` | value ∉ `[-2^20, 2^20)` |
| 561 | `R_AARCH64_TLSDESC_ADR_PREL21` | `TLSDESC(S + A) - P` | `[20:0]`, `Adr` | value ∉ `[-2^20, 2^20)` |
| 562 | `R_AARCH64_TLSDESC_ADR_PAGE21` | `Page(TLSDESC(S + A)) - Page(P)` | `[32:12]`, `Adr` | value ∉ `[-2^32, 2^32)` |
| 563 | `R_AARCH64_TLSDESC_LD64_LO12` | `TLSDESC(S + A)` | `[11:3]`, `LdrRegister` | none |
| 564 | `R_AARCH64_TLSDESC_ADD_LO12` | `TLSDESC(S + A)` | `[11:0]`, `Add` | none |
| 565 | `R_AARCH64_TLSDESC_OFF_G1` | `TLSDESC(S + A) - GOT` | `[31:16]`, `Movnz` | value ∉ `[-2^32, 2^32)` |
| 566 | `R_AARCH64_TLSDESC_OFF_G0_NC` | `TLSDESC(S + A) - GOT` | `[15:0]`, `Movkz` | none |
| 567 | `R_AARCH64_TLSDESC_LDR` | — (marker on the `ldr`) | no field written | none |
| 568 | `R_AARCH64_TLSDESC_ADD` | — (marker on the `add`) | no field written | none |
| 569 | `R_AARCH64_TLSDESC_CALL` | — (marker on the `blr`) | no field written | none |

The last three write nothing. They exist so the linker can recognise the
descriptor call sequence and relax it; a linker that does not relax MUST still
accept them and leave the instruction unchanged.

## 12. Architecture-level output properties

### 12.1 `e_machine` and `e_flags`

AArch64 output MUST have `e_machine = EM_AARCH64` (**183**), `EI_CLASS =
ELFCLASS64`, `EI_DATA = ELFDATA2LSB`.

`e_flags` for the AArch64 LP64 ABI has **no defined bits**: it MUST be written
as 0, and a linker MUST accept inputs whose `e_flags` is 0. (`aaelf64`
reserves `EF_AARCH64_BTI_PLT` and similar names only for the ILP32 and
platform-specific variants, and the base ABI leaves the word zero. Nothing in
this task depends on a non-zero value.)

Mixing inputs of different machines MUST be a diagnosed error naming the
offending file.

### 12.2 `STO_AARCH64_VARIANT_PCS` and `.variant_pcs`

A symbol whose function uses a non-standard procedure call standard — the SVE
vector PCS is the motivating case, where call-clobbered register sets differ
from the base PCS — is marked by the assembler directive

```
  .variant_pcs my_vector_fn
```

which sets bit **`0x80`** (`STO_AARCH64_VARIANT_PCS`) in the symbol's
`st_other`. A linker MUST:

- preserve that bit when copying the symbol into `.symtab` and `.dynsym`;
- if any symbol that has the bit set ends up with a PLT entry in dynamic
  output, emit the dynamic tag `DT_AARCH64_VARIANT_PCS`
  (`DT_LOPROC + 5` = `0x70000005`, `d_un` unused). The loader reads it to
  learn that lazy PLT resolution would clobber registers the callee expects to
  survive, and binds those entries eagerly.

The related tags are `DT_AARCH64_BTI_PLT` (`0x70000001`) and
`DT_AARCH64_PAC_PLT` (`0x70000003`), which say the PLT entries were generated
with BTI landing pads and with pointer-authentication respectively. Emitting
them is optional; a linker that always binds eagerly and generates a plain PLT
does not need any of the three, and this task does not grade them.

The corresponding note property is `GNU_PROPERTY_AARCH64_FEATURE_1_AND`
(`0xc0000000`) with bits `BTI` = 0x1, `PAC` = 0x2 and `GCS` = 0x4; its
combining rule is in *Debug information and notes*.
