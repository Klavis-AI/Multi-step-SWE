# Thread-local storage

> The TLS segment, the four access models, and the relaxations a linker performs between them on x86-64 and AArch64.

## Provenance

This restates Ulrich Drepper's *ELF Handling For Thread-Local Storage*
(version 0.21, <https://akkadia.org/drepper/tls.pdf>), narrowed to what a
**linker** must do, and cross-checked against the TLS chapters of the x86-64
psABI (<https://gitlab.com/x86-psABIs/x86-64-ABI>) and `aaelf64` §5.7.11
(<https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>). The
TLS descriptor (`TLSDESC`) design is from Alexandre Oliva's *Thread-Local
Storage Descriptors for IA32 and AMD64/EM64T*
(<https://www.fsfla.org/~lxoliva/writeups/TLS/RFC-TLSDESC-x86.txt>) and
`aaelf64` §5.7.11.5.

Deliberately narrowed: everything the *loader* and *libc* do — allocating the
static TLS block, growing the DTV, `dlopen`ing a module with TLS, the
`__tls_get_addr` slow path, `dl_iterate_phdr`-based unwinding — is out of
scope. TLS on RISC-V and LoongArch is introduced by the milestone that
introduces those architectures. The i386 variants of every sequence are
omitted; only LP64 x86-64 and AArch64 appear.

One deviation, followed here because the task's ground truth is the reference
implementation: **`PT_TLS`'s `p_filesz`.** The gABI rule is that `p_filesz`
covers the initialised image — that is, `.tdata` only — and `p_memsz` covers
`.tdata` plus `.tbss`. The reference implementation sets `p_filesz` equal to
`p_memsz`, so that the `.tbss` region is also counted as file-backed. Section
2 states the rule and marks exactly what is required.

## 1. What thread-local storage is, to a linker

A thread-local variable has one instance per thread. The linker's job is not
to create those instances — the runtime does that — but to:

1. Collect every `SHF_TLS` input section — `.tdata` and `.tbss` in the
   ordinary case, plus any further `SHF_TLS` section an input or a linker
   script contributes — into a single **TLS initialisation image** and
   describe it with a `PT_TLS` program header, so the runtime knows how
   many bytes to allocate per thread and what to initialise them with.
2. Assign each thread-local symbol an **offset within that image**.
3. Resolve every TLS relocation into either a final offset (when the answer is
   known at link time) or a GOT slot plus a dynamic relocation (when it is
   not).
4. Rewrite code sequences that ask for more indirection than the final link
   actually needs — the *relaxations* of section 7.

A thread-local symbol has `STT_TLS` (type **6**) in `st_info`. Its `st_value`
is **not an address**: it is the symbol's byte offset from the start of its
own module's TLS block. Nothing may treat it as an address; in particular
`R_X86_64_64` against an `STT_TLS` symbol is meaningless and MUST be a
diagnosed error.

## 2. `PT_TLS`, `.tdata` and `.tbss`

| section | type | flags | contents |
| --- | --- | --- | --- |
| `.tdata` | `SHT_PROGBITS` | `SHF_ALLOC | SHF_WRITE | SHF_TLS` | initialised thread-local data |
| `.tbss` | `SHT_NOBITS` | `SHF_ALLOC | SHF_WRITE | SHF_TLS` | zero-initialised thread-local data |

`SHF_TLS` is `0x400`. `.tbss` MUST remain `SHT_NOBITS` in the output: it
occupies no file bytes.

The linker MUST place `.tbss` immediately after `.tdata` in the address space,
and any further `SHF_TLS` section after `.tbss`, so that together they form
one contiguous TLS block, and MUST emit a single `PT_TLS` program header
describing the whole block:

| field | value |
| --- | --- |
| `p_type` | `PT_TLS` = 7 |
| `p_offset` | file offset of `.tdata` |
| `p_vaddr` | virtual address of `.tdata` — the start of the TLS block |
| `p_paddr` | same as `p_vaddr` |
| `p_filesz` | size of the initialised image (see below) |
| `p_memsz` | `end_of(last SHF_TLS section) - address_of(.tdata)` — covers the whole image; with only `.tdata` and `.tbss` present that is `end_of(.tbss) - address_of(.tdata)` |
| `p_flags` | `PF_R` (the block is copied, so it need not be writable as mapped) |
| `p_align` | the maximum `sh_addralign` over **every** `SHF_TLS` section the segment covers, not only over `.tdata` and `.tbss` |

`p_memsz` **MUST** span the whole TLS image — from the start of the first
`SHF_TLS` section to the end of the last — including any padding the
alignment of a later section forced between them. That total is what the
runtime allocates per thread. In the common case the image is exactly
`.tdata` followed by `.tbss` and the value is
`end_of(.tbss) - address_of(.tdata)`; when a further `SHF_TLS` section
exists it is laid out with the other TLS sections — slot 13 of the order
in § 2 of `00-compatibility-profile.md`, i.e. after `.tbss` and before
`.bss` — and `p_memsz` MUST reach its end. Measured on such an image —
`.tdata` 8 bytes at align 8, `.tbss` 0x10 at align 8, a custom `.mytls`
`SHT_NOBITS` of 0x40 at align 0x10 —
`wild-0.6.0` through `wild-0.10.0` all emit `p_memsz = 0x60`, reaching the
end of `.mytls`, and `p_align = 0x10`, taken from `.mytls`.

`p_filesz` is the number of bytes the runtime copies from the image before
zeroing the rest. The gABI rule is that it equals the size of `.tdata`, and
the runtime zeroes the `p_memsz - p_filesz` tail — which is exactly the
`.tbss` region, because `.tbss` is `NOBITS` and has no file bytes to copy.
A linker SHOULD emit that value. What is **required** is only that
`p_filesz ≥ size_of(.tdata)` and `p_filesz ≤ p_memsz`; nothing grades the
strict form, because the reference itself moves inside that range between
releases. Measured on the three-section image above: `p_filesz == p_memsz`
(`0x60`) at `wild-0.6.0` through `wild-0.9.0`, and `p_filesz == 0x8`, the
size of `.tdata`, at `wild-0.10.0`. `p_filesz` MUST NOT be smaller than
`.tdata`, because then the initialised data would be truncated.

The TLS block sits inside a `PT_LOAD` segment like any other allocated data.
`PT_TLS` is descriptive, not loadable in its own right.

If no input contributes any `SHF_TLS` section, no `PT_TLS` MUST be emitted.

## 3. Variant I and variant II

Two incompatible layouts exist for where the per-thread block sits relative
to the thread pointer.

### Variant II — x86-64 (and i386)

The thread control block (TCB), whose first word points to itself, is at the
thread pointer, and the TLS blocks grow **downwards** from there:

```
   lower addresses                                    higher addresses
   ... | module 2 block | module 1 block | TCB(+DTV ptr) | ...
                                          ^
                                          TP  (%fs base)
```

Every offset from TP into the static TLS block is therefore **negative**. For
the executable's own block, whose size is known at link time, the linker
computes

```
TPOFF(S) = S - tls_end_address
```

where `tls_end_address` is the address just past the end of the TLS block --
the end of the last `SHF_TLS` section, which is `.tbss` unless a further
`SHF_TLS` section was laid out after it (§2) -- rounded up to the TLS block's
alignment so that the TCB itself stays aligned. A
variable at the very start of `.tdata` in a 0x34-byte block gets `-0x40` after
that rounding, not `-0x34`.

On x86-64 the thread pointer is read as `%fs:0`, because the `%fs` base
register *is* TP and the self-pointer at `%fs:0` yields its value.

### Variant I — AArch64 (and RISC-V, LoongArch, PowerPC, IA-64)

The TCB is at the thread pointer and the TLS blocks grow **upwards** past it:

```
   lower addresses                                    higher addresses
   ... | TCB (2 reserved words) | module 1 block | module 2 block | ...
       ^
       TP  (TPIDR_EL0)
```

AArch64 reserves **two 64-bit words = 16 bytes** at TP for the TCB. Offsets
from TP are therefore **positive**, and the linker computes

```
TPREL(S) = S - tls_start_address
tls_start_address = align_down(address_of(.tdata) - 16, align_of(.tdata))
```

That is: TP points 16 bytes below the start of `.tdata`, rounded down so the
reserved area is aligned to the TLS block's own alignment. A variable at the
start of `.tdata` gets `+16`, not `+0`. This is why every AArch64 local-exec
relocation field is unsigned and why `R_AARCH64_TLSLE_ADD_TPREL_HI12`
range-checks against `[0, 2^24)`.

On AArch64 the thread pointer is read with `mrs xN, tpidr_el0`.

| | x86-64 | AArch64 |
| --- | --- | --- |
| variant | II | I |
| TP register | `%fs` base | `TPIDR_EL0` |
| reserved at TP | TCB, above TP | 16 bytes, at TP |
| sign of static offsets | negative | positive |
| `TPOFF(S)` | `S - tls_end_address` | `S - align_down(tdata_start - 16)` |

## 4. The DTV and `__tls_get_addr`

A module loaded after the program started — by `dlopen` — cannot be given a
slot in the static TLS block, because that block was sized when the program
started. Such a module's variables are reached through the **dynamic thread
vector**: a per-thread array, pointed to from the TCB, whose element `m` is
the address of module `m`'s block for this thread, allocated lazily.

The interface between compiled code and the runtime is one function, declared
identically on x86-64 and AArch64:

```c
typedef struct {
    unsigned long int ti_module;   /* module id, from DT_ index assigned by the loader */
    unsigned long int ti_offset;   /* offset of the variable within that module's block */
} tls_index;

extern void *__tls_get_addr (tls_index *ti);
```

It returns the address of the variable in the calling thread. The two words
of the `tls_index` are exactly what the linker places in a GOT slot pair:

| GOT word | filled by | with |
| --- | --- | --- |
| slot *n* | `R_X86_64_DTPMOD64` (16) / `R_AARCH64_TLS_DTPMOD` (1028) | the module id of the defining module |
| slot *n+1* | `R_X86_64_DTPOFF64` (17) / `R_AARCH64_TLS_DTPREL` (1029) | the variable's offset in that module's block |

The two slots MUST be **adjacent and in that order**, module id first, so
that the address of slot *n* can be passed directly as the `tls_index *`.

The module id is not knowable at link time for a shared object, so the linker
emits the dynamic relocation and leaves the slot zero. In a **static
executable** the module id is 1 and the offset is final, so the linker MAY
write both words itself and emit no dynamic relocation; an executable that
does this must still provide a `__tls_get_addr` if any unrelaxed call remains.

A linker MUST allocate at most one module-id/offset pair per symbol: two
general-dynamic references to the same variable share one pair. And it MUST
NOT allocate the local-dynamic module descriptor (section 5.2) unless some
input actually uses a local-dynamic sequence — a GOT with a spare
`DTPMOD64` slot that nothing references is a defect.

## 5. The four access models

The compiler chooses a model and emits a fixed code sequence with fixed
relocations. The linker's freedom is only to *replace* a sequence with a
cheaper one (section 7); it may never change the model in the other
direction.

### 5.1 General dynamic (GD)

The most general: works for any variable in any module, including one loaded
by `dlopen`. Costs a call.

**x86-64**, 16 bytes, deliberately padded so the linker has room to rewrite:

```
  66 48 8d 3d <rel32>     data16 lea foo@TLSGD(%rip), %rdi   ; R_X86_64_TLSGD   (19)
  66 66 48 e8 <rel32>     data16 data16 rex.W call __tls_get_addr@PLT
                                                              ; R_X86_64_PLT32   (4)
```

`R_X86_64_TLSGD` names the variable; its value is `GOT_slot_pair - P + A`,
i.e. a PC-relative reference to the first of the two GOT words. The
`__tls_get_addr` call makes the symbol an undefined import in a shared
object's `.dynsym`.

**AArch64**:

```
  adrp x0, :tlsgd:foo      ; R_AARCH64_TLSGD_ADR_PAGE21  (513)
  add  x0, x0, :tlsgd_lo12:foo ; R_AARCH64_TLSGD_ADD_LO12_NC (514)
  bl   __tls_get_addr
```

Linking a GD reference in a `-shared` output MUST produce **exactly one**
`DTPMOD64` and **exactly one** `DTPOFF64` entry for that variable, both naming
it, at GOT addresses 8 bytes apart, and MUST import `__tls_get_addr` as an
undefined dynamic symbol.

### 5.2 Local dynamic (LD)

When several variables in *this* module are accessed and the module might
still be `dlopen`ed, one `__tls_get_addr` call retrieves the module's block
base, and each variable is then reached by adding its constant `DTPOFF`.

**x86-64**:

```
  48 8d 3d <rel32>        lea foo@TLSLD(%rip), %rdi        ; R_X86_64_TLSLD   (20)
  e8 <rel32>              call __tls_get_addr@PLT
  ...                     lea foo@DTPOFF(%rax), %rcx       ; R_X86_64_DTPOFF32 (21)
```

`R_X86_64_TLSLD` refers to a GOT pair whose `DTPMOD64` is this module and
whose `DTPOFF64` word is **zero** — the module descriptor. There is exactly
one per output file, shared by every LD sequence. `R_X86_64_DTPOFF32` and
`R_X86_64_DTPOFF64` (17) store the variable's offset within its module's
block.

**AArch64** uses `R_AARCH64_TLSLD_ADR_PAGE21` (518) /
`R_AARCH64_TLSLD_ADD_LO12_NC` (519) for the descriptor, then the `DTPREL`
family (523–538, 572–573) for each variable.

### 5.3 Initial exec (IE)

For a variable in a module that is guaranteed to be present at program start —
the executable itself, or a `DT_NEEDED` dependency — the offset from TP is a
per-process constant, but not a link-time constant for a shared object. It
goes in a single GOT slot, filled at load time.

**x86-64**:

```
  48 8b 05 <rel32>        mov foo@GOTTPOFF(%rip), %rax     ; R_X86_64_GOTTPOFF (22)
  64 48 03 04 25 00...    add %fs:0, %rax
```

The slot is filled with `S - TP` by `R_X86_64_TPOFF64` (18), or written
directly by the linker in an executable. One slot per variable; no module-id
slot may be created alongside it.

**AArch64**:

```
  adrp x0, :gottprel:foo            ; R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21   (541)
  ldr  x0, [x0, :gottprel_lo12:foo] ; R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC (542)
  mrs  x1, tpidr_el0
  add  x0, x0, x1
```

A module that uses IE relocations cannot be `dlopen`ed safely once the static
TLS surplus is exhausted; a linker that emits them into a shared object SHOULD
set `DF_STATIC_TLS` (`0x10`) in `DT_FLAGS`.

### 5.4 Local exec (LE)

For a variable defined in the executable itself and referenced from the
executable. The offset from TP is a link-time constant and no GOT slot, no
dynamic relocation and no call are needed.

**x86-64**:

```
  64 48 8b 04 25 00 00 00 00   mov %fs:0, %rax
  48 8d 80 <imm32>             lea foo@TPOFF(%rax), %rax    ; R_X86_64_TPOFF32 (23)
```

with `imm32` a **negative** value, `S - tls_end_address`.

**AArch64**:

```
  mrs x0, tpidr_el0
  add x0, x0, #:tprel_hi12:foo, lsl #12  ; R_AARCH64_TLSLE_ADD_TPREL_HI12    (549)
  add x0, x0, #:tprel_lo12_nc:foo        ; R_AARCH64_TLSLE_ADD_TPREL_LO12_NC (551)
```

with a **positive** offset, `S - align_down(tdata_start - 16)`. The
`MOVW_TPREL` group (544–548) and the `LDST*_TPREL_LO12(_NC)` group
(552–559, 570–571) are alternative encodings of the same quantity.

An IE reference and an LE reference to the same variable in the same image
MUST compute the same address. This is the strongest single check on TP
arithmetic: `TP + *GOT_slot` and `TP + TPREL(S)` are the same pointer.

## 6. TLS descriptors (TLSDESC)

TLSDESC replaces the unconditional `__tls_get_addr` call with an indirect call
through a two-word descriptor that the *loader* fills in. When the variable
turns out to live in the static block, the loader installs a trivial resolver
that just returns a constant, making the fast path nearly as cheap as IE
while keeping GD's generality.

The descriptor occupies **two consecutive GOT words**:

| word | contents |
| --- | --- |
| 0 | address of the resolver function, installed by the loader |
| 1 | the resolver's argument |

and is created by one dynamic relocation, `R_X86_64_TLSDESC` (36) or
`R_AARCH64_TLSDESC` (1031), whose `r_offset` is the address of word 0.

The resolver is called with the address of word 0 in `%rax` (x86-64) or `x0`
(AArch64), returns the variable's offset **from TP** in the same register, and
MUST preserve every other register — which is why the call is indirect through
the descriptor rather than through the PLT.

**x86-64** sequence:

```
  48 8d 05 <rel32>        lea foo@TLSDESC(%rip), %rax  ; R_X86_64_GOTPC32_TLSDESC (34)
  ff 10                   call *foo@TLSCALL(%rax)      ; R_X86_64_TLSDESC_CALL    (35)
  64 48 03 04 25 00...    add %fs:0, %rax
```

`R_X86_64_TLSDESC_CALL` writes nothing; it marks the call so the linker can
relax it.

**AArch64** sequence:

```
  adrp x0, :tlsdesc:foo           ; R_AARCH64_TLSDESC_ADR_PAGE21 (562)
  ldr  x1, [x0, :tlsdesc_lo12:foo] ; R_AARCH64_TLSDESC_LD64_LO12  (563)
  add  x0, x0, :tlsdesc_lo12:foo   ; R_AARCH64_TLSDESC_ADD_LO12   (564)
  blr  x1                          ; R_AARCH64_TLSDESC_CALL       (569)
  mrs  x1, tpidr_el0
  add  x0, x0, x1
```

`R_AARCH64_TLSDESC_LDR` (567), `_ADD` (568) and `_CALL` (569) write no field.

A linker MUST allocate at most one descriptor per symbol, MUST place it in the
GOT, and — because `lea foo@TLSDESC(%rip)` points at it directly — MUST make
the `GOTPC32_TLSDESC` displacement land exactly on word 0.

TLSDESC, GD and IE can all appear in one output file at once, each for a
different variable. A shared object containing one of each MUST end up with
one `R_*_TLSDESC`, one `DTPMOD64`/`DTPOFF64` pair and one `TPOFF64`, each
naming its own symbol, and nothing extra.

## 7. Relaxations

A relaxation replaces a code sequence with a cheaper one *and* changes the
relocation that is applied to it. Both halves are mandatory: rewriting the
bytes without switching the relocation type, or vice versa, produces garbage.

The two preconditions that authorise relaxation are:

- **`can_bypass_got`** — the symbol is not preemptible: it is defined in this
  output file, is not exported from a shared object where another module could
  override it, and is not an ifunc. This is what lets GD or IE become LE.
- **`output_kind.is_executable()`** — the output is an executable (`ET_EXEC`
  or a PIE), so the variable, if defined here at all, is in the static TLS
  block. This is what lets GD become IE.

Relaxations MUST NOT be applied in `-shared` output, and MUST NOT be applied
to a relocation in a non-executable section. When the byte pattern at the
relocation site is not the exact sequence the relaxation expects, the linker
MUST leave it alone rather than rewrite blindly.

When a relaxation consumes a two-relocation sequence — the `lea` plus the
`call` of a GD sequence, say — the linker MUST **skip the following
relocation**, because the instruction it referred to no longer exists.

### 7.1 x86-64 byte rewrites

Offsets below are relative to the relocation's `r_offset`, which points at the
4-byte displacement field, not at the start of the instruction.

| from | to | bytes written, and over what range |
| --- | --- | --- |
| `R_X86_64_TLSGD` | LE, reloc becomes `R_X86_64_TPOFF32` | over `[offset-4, offset+8)` write `64 48 8b 04 25 00 00 00 00` (`mov %fs:0,%rax`) then `48 8d 80` (`lea disp32(%rax),%rax`); then `offset += 8` and `addend = 0` |
| `R_X86_64_TLSGD` | IE, reloc becomes `R_X86_64_GOTTPOFF` | over `[offset-4, offset+8)` write `64 48 8b 04 25 00 00 00 00` then `48 03 05` (`add disp32(%rip),%rax`); then `offset += 8` |
| `R_X86_64_TLSLD` | LE, reloc becomes `R_X86_64_NONE` | requires the preceding 3 bytes to be `48 8d 3d` (`lea 0(%rip),%rdi`); over `[offset-3, offset+9)` write `66 66 66 64 48 8b 04 25 00 00 00 00`; then `offset += 5` |
| `R_X86_64_GOTTPOFF` | LE, reloc becomes `R_X86_64_TPOFF32` | requires bytes at `[offset-3, offset-1)` to be `48 8b` or `4c 8b` (`mov *x(%rip),reg`); rewrite the REX byte at `offset-3` as `(rex & ~4) | ((rex & 4) >> 2)`, set `offset-2` to `0xc7`, and set the ModRM byte at `offset-1` to `((modrm >> 3) & 7) | 0xc0`, turning `mov mem,reg` into `mov $imm32,reg`; `addend = 0` |
| `R_X86_64_GOTPC32_TLSDESC` | LE, reloc becomes `R_X86_64_TPOFF32` | requires the preceding 3 bytes to be `48 8d 05`; over `[offset-3, offset+6)` write `48 c7 c0 00 00 00 00` (`mov $imm32,%rax`) then `66 90` (`xchg %ax,%ax`) |
| `R_X86_64_GOTPC32_TLSDESC` | IE, reloc becomes `R_X86_64_GOTTPOFF` | same precondition; over `[offset-3, offset+6)` write `48 8b 05 00 00 00 00` (`mov disp32(%rip),%rax`) then `66 90` |
| `R_X86_64_TLSDESC_CALL` | nothing to call | write `66 90` at `[offset, offset+2)` |

The GD forms are recognised by their padding. The *regular* form has
`66 48 8d 3d` at `[offset-4, offset)` and `66 66 48 e8` at
`[offset+4, offset+8)`; the *large* (large-code-model) form has `48 8d 3d`
before, `48 b8` just after the displacement, and `48 01 d8 ff d0` fourteen
bytes on. The large form relaxes to LE by writing, over
`[offset-3, offset+19)`, `64 48 8b 04 25 00 00 00 00` then
`48 8d 80 00 00 00 00` then `66 0f 1f 44 00 00`, advancing `offset` by 9.

The LD-to-LE relaxation has a 64-bit variant too, recognised by `48 b8` at
`[offset+4, offset+6)`, which writes
`66 66 66 66 2e 0f 1f 84 00 00 00 00 00` followed by
`64 48 8b 04 25 00 00 00 00` over `[offset-3, offset+19)` and advances
`offset` by 15.

### 7.2 AArch64 rewrites

AArch64 instructions are fixed-width, so a relaxation replaces whole
instructions rather than resizing anything. `nop` is `1f 20 03 d5`.

| from | condition | action |
| --- | --- | --- |
| `R_AARCH64_TLSDESC_ADR_PAGE21` | non-preemptible, or executable output | replace the instruction with `nop`; relocation becomes `R_AARCH64_NONE` |
| `R_AARCH64_TLSDESC_LD64_LO12` | non-preemptible, or executable output | replace with `nop`; relocation becomes `R_AARCH64_NONE` |
| `R_AARCH64_TLSDESC_ADD_LO12` | non-preemptible | replace with `movz x0, #imm, lsl #16` (`00 00 a0 d2`); relocation becomes `R_AARCH64_TLSLE_MOVW_TPREL_G1` |
| `R_AARCH64_TLSDESC_CALL` | non-preemptible | replace with `movk x0, #imm` (`00 00 80 f2`); relocation becomes `R_AARCH64_TLSLE_MOVW_TPREL_G0_NC` |
| `R_AARCH64_TLSDESC_ADD_LO12` | executable output, preemptible | replace with `adrp x0, …` (`00 00 00 90`); relocation becomes `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21` |
| `R_AARCH64_TLSDESC_CALL` | executable output, preemptible | replace with `ldr x0, [x0, …]` (`00 00 40 f9`); relocation becomes `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC` |
| `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21` | non-preemptible | replace with `movz xN, #imm, lsl #16`, keeping the original destination register from bits 4:0; relocation becomes `R_AARCH64_TLSLE_MOVW_TPREL_G1` |
| `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC` | non-preemptible | replace with `movk xN, #imm`, keeping the destination register; relocation becomes `R_AARCH64_TLSLE_MOVW_TPREL_G0_NC` |

Two notes on the register-preserving forms: `MovzXnLsl16` reads bits 4:0 of
the existing instruction to recover the destination register, and `MovkXn`
additionally checks that the source register (bits 9:5) equals the
destination, because `movk` reads and writes the same register.

The AArch64 `adrp`-to-`adr` relaxation, used for non-TLS references within
±1 MiB, simply clears bit 31 of the instruction word (the `op` bit that
distinguishes `adrp` from `adr`).

### 7.3 Which relaxations are legal when

| output | symbol preemptible? | GD becomes | LD becomes | IE becomes | TLSDESC becomes |
| --- | --- | --- | --- | --- | --- |
| `-shared` | either | GD (no change) | LD | IE | TLSDESC |
| executable | yes | IE | LE | IE | IE |
| executable | no | LE | LE | LE | LE |

An executable's own thread-locals are never preemptible, so a static
executable relaxes everything to LE and needs no TLS GOT slots, no
`__tls_get_addr` and no dynamic relocations at all.

## 8. Checklist of linker duties

- `.tbss` stays `NOBITS`; `.tdata` and `.tbss` are contiguous; one `PT_TLS`
  covers both with the alignment of the stricter of the two.
- `STT_TLS` symbols carry block-relative offsets in `st_value`, never
  addresses.
- One GOT entry per `(symbol, TLS kind)`: one `DTPMOD64`/`DTPOFF64` pair for
  GD, one slot for IE, one descriptor pair for TLSDESC, one module descriptor
  per output file for LD — and none of these allocated speculatively.
- `__tls_get_addr` is imported only if a GD or LD sequence survives
  relaxation.
- Relaxations rewrite bytes and switch relocation types together, skip the
  consumed follow-on relocation, and never fire in `-shared` output or outside
  `SHF_EXECINSTR` sections.
- TP arithmetic follows the architecture's variant: negative offsets measured
  from the end of the block on x86-64, positive offsets measured from 16 bytes
  below the start on AArch64.
