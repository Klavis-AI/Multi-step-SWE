# x86-64 relocations

> The complete `R_X86_64_*` relocation table with numbers, arithmetic, field widths and overflow conditions, and the instruction rewrites permitted for the GOT-relative relaxation relocations.

## Provenance

This restates the relocation chapter and the linker-optimization appendix of
the **System V ABI, AMD64 Architecture Processor Supplement** (the x86-64
psABI), maintained at `https://gitlab.com/x86-psABIs/x86-64-ABI`, pinned to
`master` at commit `e1ce0983` (2025-03-12) — the revision that first carries
the `CODE_4_*`, `CODE_5_*` and `CODE_6_*` relocations. Every number in the
table below was read out of that revision rather than recalled, and
cross-checked against the `R_X86_64_*` macros in glibc 2.36's `<elf.h>`.

Where the psABI describes a permitted instruction rewrite without fixing the
exact bytes, this document says so and gives the encoding the reference
implementation (`wild` 0.2.0, `wild_lib/src/relaxation.rs`) emits, which is
one of the legal choices. `TEST_INTERFACE.md` §4 states that the choice among
legal relaxations is not graded.

Deliberately narrowed: the *meaning* of the thread-local relocations is stated
here only as far as their arithmetic; the TLS models, the layout of the TLS
block and the `TP`/`DTV` runtime contract are the subject of the
thread-local-storage document introduced by a later milestone. The ILP32
("x32") subset is out of scope, and relocations marked LP64-only below are the
only ones this benchmark ever sees.

## 1. Notation

Every relocation computes a value from some subset of these terms. They are
the psABI's own letters and this document uses no others.

| term | meaning |
| --- | --- |
| `A` | the addend. For `SHT_RELA` this is `r_addend`; for `SHT_REL` it is the value already stored in the field being relocated, read at the field's width and sign convention. |
| `B` | the base address at which the object was loaded — the difference between the virtual addresses in the file and the addresses actually used. Zero for a non-PIE executable, which is why `R_X86_64_RELATIVE` never appears in one. |
| `G` | the offset **into the GOT** of the slot holding this symbol's address. Not an address: `GOT + G` is the address of the slot. |
| `GOT` | the address of the global offset table, i.e. the address `_GLOBAL_OFFSET_TABLE_` denotes. |
| `L` | the address of the PLT entry for the symbol. When no PLT entry is needed — a static link, or a definition that cannot be preempted — `L` is the symbol's own address and nothing is synthesised. |
| `P` | the **place**: the address of the storage unit being relocated, i.e. the output address corresponding to `r_offset`. Since x86-64 displacement fields are relative to the *end* of the instruction, `A` is normally negative (`-4` for a 4-byte displacement at the end of an instruction) to make `S + A - P` come out right. |
| `S` | the value of the symbol named by `ELF64_R_SYM(r_info)`, after resolution. For a weak undefined symbol this is `0` (`02-symbols-and-archives.md` §5). For symbol index `0` it is `0`. |
| `Z` | the size of that symbol, i.e. its `st_size`. |
| `TP` | the thread pointer, the value in `%fs:0` at run time. |
| `DTV` | the dynamic thread vector maintained by the loader; `DTPMOD`/`DTPOFF` pairs index into it. |

Field widths are named as the psABI names them:

| name | bytes | stored as |
| --- | --- | --- |
| `word8` | 1 | one byte |
| `word16` | 2 | little-endian |
| `word32` | 4 | little-endian |
| `word64` | 8 | little-endian |
| `wordclass` | 8 | little-endian (this is LP64; it would be 4 for ILP32) |
| `none` | 0 | the relocation modifies no bytes |

**Position.** Every x86-64 relocation writes its field starting at the byte
addressed by `r_offset`, occupying the next `width` bytes, least-significant
byte first, with no bit offset and no shift. There are no bitfield
relocations on x86-64. A relocation whose `r_offset + width` exceeds the
containing section's `sh_size` is malformed and MUST be diagnosed.

## 2. The relocation table

`ELF64_R_TYPE(r_info)` yields these values. Entries marked *deprecated* were
assigned and withdrawn; a linker MUST NOT emit them and SHOULD diagnose them
on input.

| № | name | field | calculation | overflow condition |
| --- | --- | --- | --- | --- |
| 0 | `R_X86_64_NONE` | `none` | — | never |
| 1 | `R_X86_64_64` | `word64` | `S + A` | never |
| 2 | `R_X86_64_PC32` | `word32` | `S + A - P` | value does not fit a signed 32-bit integer |
| 3 | `R_X86_64_GOT32` | `word32` | `G + A` | does not fit signed 32-bit |
| 4 | `R_X86_64_PLT32` | `word32` | `L + A - P` | does not fit signed 32-bit |
| 5 | `R_X86_64_COPY` | `none` | — | never; dynamic only |
| 6 | `R_X86_64_GLOB_DAT` | `wordclass` | `S` | never; dynamic only |
| 7 | `R_X86_64_JUMP_SLOT` | `wordclass` | `S` | never; dynamic only |
| 8 | `R_X86_64_RELATIVE` | `wordclass` | `B + A` | never; dynamic only |
| 9 | `R_X86_64_GOTPCREL` | `word32` | `G + GOT + A - P` | does not fit signed 32-bit |
| 10 | `R_X86_64_32` | `word32` | `S + A` | the 64-bit value does not **zero**-extend from the written 32 bits |
| 11 | `R_X86_64_32S` | `word32` | `S + A` | the 64-bit value does not **sign**-extend from the written 32 bits |
| 12 | `R_X86_64_16` | `word16` | `S + A` | does not fit unsigned 16-bit |
| 13 | `R_X86_64_PC16` | `word16` | `S + A - P` | does not fit signed 16-bit |
| 14 | `R_X86_64_8` | `word8` | `S + A` | does not fit unsigned 8-bit |
| 15 | `R_X86_64_PC8` | `word8` | `S + A - P` | does not fit signed 8-bit |
| 16 | `R_X86_64_DTPMOD64` | `word64` | module ID of the module defining `S`, written by the loader | never |
| 17 | `R_X86_64_DTPOFF64` | `word64` | offset of `S` within its module's TLS block | never |
| 18 | `R_X86_64_TPOFF64` | `word64` | offset of `S` from the thread pointer, i.e. `S - TP` | never |
| 19 | `R_X86_64_TLSGD` | `word32` | PC-relative displacement to the two-word general-dynamic GOT pair for `S` | does not fit signed 32-bit |
| 20 | `R_X86_64_TLSLD` | `word32` | PC-relative displacement to the local-dynamic GOT pair for the module | does not fit signed 32-bit |
| 21 | `R_X86_64_DTPOFF32` | `word32` | offset of `S` within its module's TLS block | does not fit signed 32-bit |
| 22 | `R_X86_64_GOTTPOFF` | `word32` | PC-relative displacement to the GOT slot holding `S`'s `TPOFF64` | does not fit signed 32-bit |
| 23 | `R_X86_64_TPOFF32` | `word32` | `S - TP`, resolved at link time in an executable | does not fit signed 32-bit |
| 24 | `R_X86_64_PC64` | `word64` | `S + A - P` | never (LP64 only) |
| 25 | `R_X86_64_GOTOFF64` | `word64` | `S + A - GOT` | never (LP64 only) |
| 26 | `R_X86_64_GOTPC32` | `word32` | `GOT + A - P` | does not fit signed 32-bit |
| 27 | `R_X86_64_GOT64` | `word64` | `G + A` | never |
| 28 | `R_X86_64_GOTPCREL64` | `word64` | `G + GOT - P + A` | never |
| 29 | `R_X86_64_GOTPC64` | `word64` | `GOT - P + A` | never |
| 30 | *deprecated* (was `GOTPLT64`) | — | — | — |
| 31 | `R_X86_64_PLTOFF64` | `word64` | `L - GOT + A` | never |
| 32 | `R_X86_64_SIZE32` | `word32` | `Z + A` | does not fit unsigned 32-bit |
| 33 | `R_X86_64_SIZE64` | `word64` | `Z + A` | never (LP64 only) |
| 34 | `R_X86_64_GOTPC32_TLSDESC` | `word32` | PC-relative displacement to the symbol's TLS descriptor in the GOT | does not fit signed 32-bit |
| 35 | `R_X86_64_TLSDESC_CALL` | `none` | marks the indirect call through the descriptor; relaxation target only | never |
| 36 | `R_X86_64_TLSDESC` | two `word64` | the 16-byte TLS descriptor: resolver function then argument | never |
| 37 | `R_X86_64_IRELATIVE` | `wordclass` | `indirect (B + A)` — the loader calls the resolver at `B + A` and stores what it returns | never |
| 38 | `R_X86_64_RELATIVE64` | `word64` | `B + A` | never; ILP32 only, MUST NOT appear in an LP64 object |
| 39 | *deprecated* (was `PC32_BND`) | — | — | — |
| 40 | *deprecated* (was `PLT32_BND`) | — | — | — |
| 41 | `R_X86_64_GOTPCRELX` | `word32` | `G + GOT + A - P` | does not fit signed 32-bit |
| 42 | `R_X86_64_REX_GOTPCRELX` | `word32` | `G + GOT + A - P` | does not fit signed 32-bit |
| 43 | `R_X86_64_CODE_4_GOTPCRELX` | `word32` | `G + GOT + A - P` | does not fit signed 32-bit |
| 44 | `R_X86_64_CODE_4_GOTTPOFF` | `word32` | as `GOTTPOFF`, on an instruction with a 4-byte prefix | does not fit signed 32-bit |
| 45 | `R_X86_64_CODE_4_GOTPC32_TLSDESC` | `word32` | as `GOTPC32_TLSDESC`, 4-byte prefix | does not fit signed 32-bit |
| 46 | `R_X86_64_CODE_5_GOTPCRELX` | `word32` | as `GOTPCRELX`, 5-byte prefix | does not fit signed 32-bit |
| 47 | `R_X86_64_CODE_5_GOTTPOFF` | `word32` | as `GOTTPOFF`, 5-byte prefix | does not fit signed 32-bit |
| 48 | `R_X86_64_CODE_5_GOTPC32_TLSDESC` | `word32` | as `GOTPC32_TLSDESC`, 5-byte prefix | does not fit signed 32-bit |
| 49 | `R_X86_64_CODE_6_GOTPCRELX` | `word32` | as `GOTPCRELX`, 6-byte prefix | does not fit signed 32-bit |
| 50 | `R_X86_64_CODE_6_GOTTPOFF` | `word32` | as `GOTTPOFF`, 6-byte prefix | does not fit signed 32-bit |
| 51 | `R_X86_64_CODE_6_GOTPC32_TLSDESC` | `word32` | as `GOTPC32_TLSDESC`, 6-byte prefix | does not fit signed 32-bit |

`R_X86_64_8`, `R_X86_64_16`, `R_X86_64_PC16` and `R_X86_64_PC8` are declared
non-conformant by the psABI: no conforming *compiler* emits them. The
*assembler* does, for a `.byte` or `.short` directive whose value is a symbolic
expression, so they reach a linker from hand-written assembly. A linker MUST
implement `R_X86_64_8` and `R_X86_64_16` — computing `S + A` and storing the
low 8 or 16 bits — and SHOULD implement `R_X86_64_PC8` and `R_X86_64_PC16`
the same way. None of them may be silently skipped.

### 2.1 Which of these appear where

In an `ET_REL` input from `clang-16` or `gcc` targeting x86-64 you will see
types 1, 2, 4, 9, 10, 11, 19–23, 26, 32, 33, 34, 35, 41, 42 and, with recent
assemblers, 43–51. Types 5, 6, 7, 8 and 37 are *output* relocation types: they
are written into `.rela.dyn` and `.rela.plt` for the dynamic loader and MUST
NOT be expected in an input. Type 0 may appear anywhere and does nothing.

### 2.2 Overflow checking

The psABI explicitly requires the linker to verify that
`R_X86_64_32` zero-extends and `R_X86_64_32S` sign-extends back to the full
64-bit computed value; a linker MUST perform those two checks and MUST
diagnose a failure, naming the relocation type and the symbol
(`00-compatibility-profile.md` §8). This is the check that catches a non-PIE
absolute address above 2 GiB and is the reason the default non-PIE image base
is `0x400000`.

**Step scope -- when this is graded.** Measured at all nine tags with a
`R_X86_64_32` reference to a symbol at `0x7fffffff0000`: the reference
implementation performs no overflow check at all from `0.2.0` through `0.6.0`
(steps 1-5). It writes the truncated field, exits 0 and says nothing.
Detection arrives at **`0.7.0`** (step 6), which fails the link and names both
the type and the symbol. So the MUST above is the obligation from step 6
onward; at steps 1-5 the check is **not graded either way**, a conforming
linker MAY perform it early, and no case at those milestones asserts either
behaviour. Note that even from `0.7.0` the failed link still leaves a file at
the `-o` path.

For every other bounded relocation in the table, a linker SHOULD verify the
range before truncating and diagnose a failure the same way. Silently
truncating a `PC32` displacement produces a binary that jumps into nowhere,
which is worse than a failed link.

`R_X86_64_SIZE32`'s `Z + A` is compared as an unsigned quantity; a negative
addend that makes the sum negative is an overflow.

### 2.3 Relocations against discarded sections

When the section a symbol was defined in has been discarded — COMDAT
deduplication, `--gc-sections`, `SHF_EXCLUDE` — a relocation referring to that
symbol from a *kept* section, which happens in debug sections, MUST NOT be
computed from stale addresses. Write a *tombstone*: `0` for
`.debug_*` address fields is acceptable and is what the reference
implementation does at this milestone. Relocations in a section that is
itself discarded are discarded with it and are never applied
(`01-elf-container.md` §7).

## 3. The GOT and the PLT in a static link

`G` and `L` are only meaningful if the linker builds the tables they index.

The **global offset table** is the output section `.got`. The linker allocates
one 8-byte slot per symbol that is the target of a GOT-based relocation, in
whatever order it likes, and `G` is that slot's byte offset from the start of
the GOT. `_GLOBAL_OFFSET_TABLE_` MUST be defined to the address of the GOT so
that `R_X86_64_GOTPC32` and `R_X86_64_GOTOFF64` work. In a static non-PIE
link the slot's contents are simply `S`, written at link time; no dynamic
relocation is produced. Two relocations against the same symbol MUST share one
slot.

The **procedure linkage table** is the output section `.plt`, 16 bytes per
entry. In a static link nothing can be preempted, so `R_X86_64_PLT32` MUST be
resolved as though it were `R_X86_64_PC32` — `L` becomes `S` — and no PLT
entry need be created at all. A linker MAY still create one; correctness is
what is graded. A PLT entry does become necessary for `STT_GNU_IFUNC` symbols,
whose resolver must run before the address is known.

The GOT lives in the writable segment; see `00-compatibility-profile.md` §2.

## 4. The `GOTPCRELX` family and its relaxations

`R_X86_64_GOTPCRELX` (41), `R_X86_64_REX_GOTPCRELX` (42) and
`R_X86_64_CODE_4_GOTPCRELX` (43) — plus the `CODE_5` and `CODE_6` variants
(46, 49) — carry exactly the same arithmetic as `R_X86_64_GOTPCREL`:
`G + GOT + A - P`. They differ only in that the assembler guarantees the
instruction containing the field has a known, rewritable shape, which lets the
linker replace an indirect memory access through the GOT with a direct one.

**Therefore the baseline requirement is simple and MUST be met: treat every
member of this family exactly as `R_X86_64_GOTPCREL` — allocate a GOT slot,
fill it with the symbol's address, and write the PC-relative displacement to
the slot.** That is always correct. Relaxation is an optimisation a linker MAY
perform, and `TEST_INTERFACE.md` §4 states that which legal relaxation is
chosen is not graded. What *is* graded is that a linker that does relax
produces a working binary.

### 4.1 Where the instruction starts

A relaxing linker must find the first byte of the instruction, which is at a
fixed negative displacement from `r_offset`:

| relocation | instruction start | what sits there |
| --- | --- | --- |
| `GOTPCRELX` (41) | `r_offset - 2` | a two-byte opcode such as `ff 15` or `ff 25` |
| `REX_GOTPCRELX` (42) | `r_offset - 3` | a REX prefix (`40`–`4f`, typically `48`) then opcode then ModRM |
| `CODE_4_GOTPCRELX` (43) | `r_offset - 4` | a 4-byte prefix whose first byte is `d5` (REX2) |
| `CODE_5_GOTPCRELX` (46) | `r_offset - 5` | a 5-byte prefix |
| `CODE_6_GOTPCRELX` (49) | `r_offset - 6` | a 6-byte prefix (EVEX) |
| `GOTTPOFF` (22) | `r_offset - 3` | REX prefix, opcode, ModRM |
| `GOTPC32_TLSDESC` (34) | `r_offset - 3` | REX prefix, opcode, ModRM |

A linker that intends to rewrite MUST verify the bytes it finds there against
the pattern it expects and MUST leave the instruction alone if they do not
match. The reference implementation checks, for example, that
`bytes[r_offset - 3] == 0x48` before treating a `REX_GOTPCRELX` as a REX-`mov`.

### 4.2 When relaxation is permitted

All of the following MUST hold before any rewrite:

1. The addend is exactly **`-4`**. Any other addend means the displacement
   does not end the instruction and the rewrite would be wrong.
2. The symbol is **defined in this link and not preemptible** — a local
   definition, or a global one in an output that cannot be interposed
   (a non-PIE executable, or a `STV_HIDDEN`/`STV_PROTECTED` symbol).
   A symbol that will bind at run time, and a `STT_GNU_IFUNC` symbol, MUST NOT
   be relaxed.
3. The resulting direct reference must itself fit its field: the rewritten
   `PC32` or `32` value must satisfy the overflow condition in §2.

**Instruction length MUST be preserved.** Every rewrite below keeps the byte
count identical, padding with a `nop` (`0x90`) or an `addr32` prefix
(`0x67`) where the direct form is shorter. Shortening an instruction would
invalidate every other relocation in the section.

At most three things change: the opcode byte, the ModRM byte, and the
relocation's own type and addend. The GOT slot that the relaxed reference no
longer uses MAY then be dropped, but keeping it is harmless.

### 4.3 The permitted rewrites

Writing `foo` for the symbol and `reg` for the register the ModRM byte
selects:

| before | after | new relocation |
| --- | --- | --- |
| `mov foo@GOTPCREL(%rip), %reg` | `lea foo(%rip), %reg` | `R_X86_64_PC32`, addend unchanged (`-4`) |
| `mov foo@GOTPCREL(%rip), %reg` (non-PIC output, address fits 32 bits) | `mov $foo, %reg` | `R_X86_64_32`, addend `0` |
| `call *foo@GOTPCREL(%rip)` | `call foo` padded to 6 bytes | `R_X86_64_PC32`, addend `0` |
| `jmp *foo@GOTPCREL(%rip)` | `jmp foo` padded to 6 bytes | `R_X86_64_PC32`, addend `0` |
| `test %reg, foo@GOTPCREL(%rip)` (non-PIC) | `test $foo, %reg` | `R_X86_64_32`, addend `0` |
| `binop foo@GOTPCREL(%rip), %reg` (non-PIC), where `binop` is one of `adc add and cmp or sbb sub xor` | `binop $foo, %reg` | `R_X86_64_32`, addend `0` |

The byte-level forms the reference implementation emits, which a linker MAY
copy verbatim:

* **`mov` to `lea`.** The instruction is `48 8b /r disp32` at `r_offset - 3`.
  Set the opcode byte at `r_offset - 2` from `0x8b` to `0x8d`. The REX prefix
  and the ModRM byte are unchanged, the relocation becomes `R_X86_64_PC32`
  and the addend stays `-4`. Example: `48 8b ae` becomes `48 8d ae`.
* **`mov` to an immediate.** Set the opcode byte at `r_offset - 2` to `0xc7`
  and rewrite the ModRM byte `m` to `((m >> 3) & 0x7) | 0xc0`, which moves the
  register out of the `reg` field into the `r/m` field and selects register
  direct mode. The relocation becomes `R_X86_64_32` with addend `0`.
  Example: `48 8b ae` becomes `48 c7 c5`. This form is only valid when the
  output is not position-independent, because it materialises an absolute
  address.
* **indirect `call` to direct.** The two bytes at `r_offset - 2` are
  `ff 15`; replace them with `67 e8`, an `addr32` prefix followed by a
  five-byte `call rel32`. The relocation becomes `R_X86_64_PC32` with addend
  `0`. The `0x67` prefix is architecturally meaningless here and exists only
  to keep the instruction six bytes long; GNU `ld` instead emits
  `2e e8` (a `cs` prefix) or moves the `nop`, and all three are legal.
* **indirect `jmp` to direct.** The two bytes at `r_offset - 2` are `ff 25`;
  the direct form is `e9 rel32` plus one padding byte. A linker that performs
  this rewrite MUST keep the total at six bytes and MUST place the `rel32`
  where the new relocation's `r_offset` says it is.

`R_X86_64_CODE_4_GOTPCRELX` with a REX2 prefix relaxes the same way, operating
on the opcode and ModRM bytes that follow the four-byte prefix; the `CODE_5`
and `CODE_6` variants likewise. A linker MAY decline to relax any of them.

### 4.4 The TLS relaxation relocations

`R_X86_64_GOTTPOFF` (22) admits the analogous rewrite: when the symbol's
thread-pointer offset is known at link time, `mov foo@GOTTPOFF(%rip), %reg`
(bytes `48 8b` at `r_offset - 3`) becomes a move of an immediate, with the
opcode and ModRM rewritten exactly as in the `mov`-to-immediate case above.
`R_X86_64_TLSDESC_CALL` (35) modifies no bytes and exists solely so the linker
can find and rewrite the indirect call when it collapses a TLS descriptor.
The initial-exec and local-exec transitions these enable, and the conditions
under which each is legal, belong to the thread-local-storage document; a
static x86-64 linker at this milestone MUST NOT be expected to perform them,
and treating `GOTTPOFF` as a plain GOT reference to a slot holding
`S - TP` is correct.

## 5. Applying a relocation

For each kept input section, in order, for each relocation in each
`SHT_RELA` section whose `sh_info` names it:

1. Look up the symbol at `ELF64_R_SYM(r_info)` in the symbol table named by
   the relocation section's `sh_link`. Index `0` means no symbol and `S = 0`.
2. Determine `S` from resolution (`02-symbols-and-archives.md` §3.1), and `P`
   as the output address of `r_offset` — that is, the output section's
   `sh_addr` plus the input section's offset within it plus `r_offset`.
3. Compute the value from the table in §2.
4. Check the overflow condition. Diagnose, naming the relocation type and the
   symbol, if it fails a check the linker performs.
5. Write the low `width` bytes of the value at the place, little-endian.
   Bytes outside the field MUST NOT be modified, except by a relaxation
   permitted in §4.

A relocation type the linker does not implement MUST be a diagnosed error
naming the type — silently skipping it produces a binary that misbehaves at
run time, which `TEST_INTERFACE.md` §2 identifies as the failure mode that
matters most.
