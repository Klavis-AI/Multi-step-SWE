# Producing dynamic output

> How a linker builds the dynamic half of an ELF image: the dynamic symbol table, the GNU hash table, the dynamic array, and the GOT and PLT that make cross-module references work.

## Provenance

This restates the dynamic-linking chapter of the System V Application Binary
Interface — the generic part (gABI, "Book III: Dynamic Linking", the
`sco.com`/`refspecs.linuxfoundation.org` edition at
<https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html>) — together
with the x86-64 psABI's procedure-linkage-table and thread-pointer chapters
(*System V ABI, AMD64 Architecture Processor Supplement*, version 1.0,
<https://gitlab.com/x86-psABIs/x86-64-ABI>). `DT_GNU_HASH` is not in any of
those; it is a GNU extension whose only normative description is glibc's
`elf/dl-lookup.c` and the binutils implementation, so section 4 below is
reconstructed from the reference implementation and from glibc's lookup code.
The `--version-script` material is the subset of `ld.texi`'s "VERSION" section
that step 2 needs; the full treatment is in *Symbol versioning*.

Deliberately narrowed: the run-time behaviour of `ld.so` (mapping, relocation
processing order, `dlopen`, lazy binding, symbol interposition rules) is out of
scope — it is an interoperability target, not something to implement. Prelink,
`DT_GNU_PRELINKED`, `DT_MOVE`, `DT_SYMINFO`, `DT_AUDIT`/`DT_DEPAUDIT` and the
Solaris-only tags are omitted.

Four places where the reference implementation departs from what the primary
sources describe, which this document follows per the task's ground-truth rule:

1. **The reference linker is always `BIND_NOW`.** It never builds a lazily
   bound PLT. Cross-module calls go through a `.plt.got` entry that jumps
   indirectly through a GOT slot filled by an eager `R_X86_64_GLOB_DAT`
   relocation in `.rela.dyn`, and `DT_FLAGS`/`DT_FLAGS_1` always carry
   `DF_BIND_NOW`/`DF_1_NOW`. Sections 7 and 8 therefore define
   `R_X86_64_JUMP_SLOT`, `DT_JMPREL`, `DT_PLTREL` and `DT_PLTRELSZ` as
   *meanings a linker must know* rather than as required output.
2. **The SysV hash table arrives late, and no milestone grades its absence.**
   At the milestone that introduces shared output the reference emits
   `.gnu.hash` only — there is no `.hash` section and no `DT_HASH` — and it
   answers `--hash-style=sysv` with a diagnostic. That stays true through the
   milestone that introduces `.symver` directives and the Illumos platform.
   From the milestone that introduces LoongArch64 and non-string section
   merging onward the reference emits `.hash` and `DT_HASH` as well,
   by default and alongside
   `.gnu.hash`, and accepts `--hash-style=sysv`. Measured at every release
   tag. Section 3 defines the `.hash` format because a linker must be able to
   read one; whether it must *write* one is settled by each milestone's own
   `TEST_INTERFACE.md`, and until one says so a link MUST NOT fail merely
   because `.hash` is absent.
3. **RELRO is unconditional for one milestone, then becomes conditional.** The
   reference emits no `PT_GNU_RELRO` at all at the milestone that introduces
   shared output. At the next milestone (copy relocations) it emits the
   segment unconditionally: `-z norelro` is accepted but has no effect, and
   this is silent — stderr is empty either way, there is no warning. From the
   milestone after that (script-driven layout) onward, `-z norelro` actually
   suppresses the segment, measured directly against the reference at each
   tag. Section 13.1 states the requirement accordingly: accept the options
   from the first milestone, produce the segment unconditionally from the
   second, and only from the third does honouring `-z norelro` become
   required rather than merely accepted.
4. **The narrow-slot / read-only branch of section 11.1 is a final-milestone
   requirement, not one this task grades from the milestone that introduces
   shared output.** Measured directly against the reference: a `-shared` or
   `-pie` link whose only defect is an absolute relocation narrower than a
   pointer, or one that lands in a read-only section, against a preemptible
   symbol succeeds — exit status 0, no diagnostic, the wrong value silently
   baked into the slot — at every milestone from the one that introduces
   shared output through the one before the last. Only the reference's final
   milestone diagnoses and rejects it. Section 11.1 states the requirement
   accordingly, and no case in this task may require the rejection earlier
   than that.

## 1. When output is dynamic

An output file is *dynamic* when it has a `PT_DYNAMIC` segment and a `.dynamic`
section. A linker MUST produce dynamic output when any of the following holds:

| condition | effect |
| --- | --- |
| `-shared` (`--shared`) | output is `ET_DYN`, no entry point, no `PT_INTERP` |
| `-pie` (`--pie`) | output is `ET_DYN` with an entry point; it gets a `PT_INTERP` only if an interpreter path was requested (§9) |
| any input is a shared object (`ET_DYN`) | the executable becomes dynamic |
| `--dynamic-linker PATH` was given | the executable becomes dynamic |

If none holds, the output is a static `ET_EXEC` with no `.dynamic`,
no `.dynsym` and no `.gnu.hash`; that case is covered by *Static linking*.

`-static` (`--static`) forces the static case. What the reference does with a
shared-object input *under* `-static` is not stable across this task — measured
at every release tag it is silently accepted at some milestones, panics at
another and is diagnosed only at the last — so no milestone grades that
combination, and an implementation may accept or reject it.

Both dash spellings of every option in this document MUST be accepted, and
`--opt=VALUE` MUST be accepted wherever `--opt VALUE` is.

## 2. `.dynsym` and `.dynstr`

`.dynsym` is an `SHT_DYNSYM` section holding `Elf64_Sym` entries, `SHF_ALLOC`,
`sh_entsize` 24, `sh_addralign` 8. `.dynstr` is an `SHT_STRTAB` section,
`SHF_ALLOC`, `sh_addralign` 1. `.dynsym`'s `sh_link` MUST be the section index
of `.dynstr`. `.dynsym`'s `sh_info` MUST be one more than the index of the last
local entry — that is, the index of the first non-local dynamic symbol.

The entry layout is the ordinary `Elf64_Sym`:

| offset | size | field | meaning in `.dynsym` |
| --- | --- | --- | --- |
| 0 | 4 | `st_name` | byte offset into `.dynstr`; MUST be non-zero except for entry 0 |
| 4 | 1 | `st_info` | `(bind << 4) | type` |
| 5 | 1 | `st_other` | visibility in the low 2 bits |
| 6 | 2 | `st_shndx` | defining section index, or `SHN_UNDEF` (0) for an import |
| 8 | 8 | `st_value` | virtual address of the definition, 0 for an import |
| 16 | 8 | `st_size` | object size in bytes, 0 if unknown |

Entry 0 MUST be entirely zero. The remaining entries MUST be ordered so that
the `.gnu.hash` invariants of section 4 hold: every symbol that `.gnu.hash`
indexes comes after every symbol it does not, and the indexed ones are sorted
by hash bucket.

Three rules govern *which* symbols appear:

- A **definition** goes into `.dynsym` when the output is a shared object and
  the symbol is globally or weakly bound with `STV_DEFAULT` or `STV_PROTECTED`
  visibility, and a version script (section 12) has not made it local. Symbols
  with `STV_HIDDEN` or `STV_INTERNAL` visibility MUST NOT appear.
- An **import** — an entry with `st_shndx == SHN_UNDEF` — goes into `.dynsym`
  for every symbol the output references that is defined by, or must be
  resolvable by, some other module at run time.
- In an executable, a definition goes into `.dynsym` only if something needs it
  to be dynamically visible (`--export-dynamic`, or a shared-object input
  referring to it).

`.symtab` is a separate, non-allocated table and is unaffected by all of this.
A symbol may appear in both; the two tables have independent indices.

For a `-shared` link of an object defining `shared_fn` and `shared_data`, both
MUST appear in `.dynsym` as `GLOBAL DEFAULT <n>` entries with a defined
section index, and `.dynsym`'s size MUST be an exact multiple of 24 and greater
than 24 (the null entry alone is not enough).

## 3. The SysV hash table — `.hash`

`.hash` is `SHT_HASH`, `SHF_ALLOC`, `sh_entsize` 4, `sh_link` = index of the
symbol table it indexes. It is an array of `Elf64_Word` (4-byte) values:

| index | contents |
| --- | --- |
| 0 | `nbucket` |
| 1 | `nchain` — MUST equal the number of entries in `.dynsym` |
| 2 .. 2+nbucket | `bucket[nbucket]`, each a `.dynsym` index or `STN_UNDEF` (0) |
| 2+nbucket .. 2+nbucket+nchain | `chain[nchain]`, parallel to `.dynsym` |

Lookup of `name`: compute `h = elf_hash(name)`, start at
`y = bucket[h % nbucket]`, and while `y != STN_UNDEF` compare `.dynsym[y]`'s
name, advancing with `y = chain[y]`.

The hash function is the gABI one, over unsigned bytes, terminated by NUL:

```
uint32_t elf_hash(const unsigned char *name) {
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        if ((g = h & 0xf0000000)) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}
```

`elf_hash` never exceeds 28 significant bits. The same function computes the
`vd_hash` and `vna_hash` fields of the versioning sections; see
*Symbol versioning*.

A linker MAY emit `.hash` and the matching `DT_HASH`; which milestone requires
which answer is settled by that milestone's `TEST_INTERFACE.md`, not here.
`--hash-style=gnu` and `--hash-style=both` MUST be accepted from the milestone
that introduces shared output onward. Through the milestone that introduces
`.symver` directives and the Illumos platform the reference implementation
emits no `.hash` at all and answers `--hash-style=sysv` with a diagnostic;
from the milestone that introduces LoongArch64 and non-string section
merging onward it emits
`.hash` and `DT_HASH` alongside `.gnu.hash` and accepts all three spellings.
Until a milestone's own contract asks for one, no case requires a `.hash`
section to exist and a link MUST NOT fail merely because it is absent.

## 4. The GNU hash table — `.gnu.hash`

`.gnu.hash` is `SHT_GNU_HASH` (`0x6ffffff6`), `SHF_ALLOC`, `sh_addralign` 8,
`sh_link` = index of `.dynsym`, `sh_entsize` 0. Its address is published as
`DT_GNU_HASH`. It indexes **only exported definitions**: undefined symbols and
local symbols are excluded, and they MUST all sit at the front of `.dynsym`,
below the index named by `symoffset`.

### 4.1 Layout

Four 32-bit header words, then three arrays:

| offset | size | name | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `nbuckets` | number of hash buckets; MUST be ≥ 1 |
| 4 | 4 | `symoffset` | index in `.dynsym` of the first hashed symbol |
| 8 | 4 | `bloom_size` | number of 64-bit Bloom words; MUST be a power of two |
| 12 | 4 | `bloom_shift` | second-bit shift for the Bloom filter |
| 16 | 8 × `bloom_size` | `bloom[]` | the Bloom filter, `Elf64_Xword` each |
| 16 + 8·bloom_size | 4 × `nbuckets` | `bucket[]` | first `.dynsym` index of each chain, or 0 |
| 16 + 8·bloom_size + 4·nbuckets | 4 × (`dynsymcount` − `symoffset`) | `chain[]` | one hash word per hashed symbol |

The total section size is therefore
`16 + 8*bloom_size + 4*nbuckets + 4*(dynsymcount - symoffset)` bytes.

### 4.2 The hash function

`.gnu.hash` uses a 32-bit djb2 variant with the multiplier 33 and the seed
5381, all arithmetic wrapping modulo 2³²:

```
uint32_t gnu_hash(const unsigned char *name) {
    uint32_t h = 5381;
    for (; *name; name++)
        h = h * 33 + *name;       /* equivalently h = (h << 5) + h + *name */
    return h;
}
```

Unlike `elf_hash`, all 32 bits are significant.

### 4.3 The Bloom filter

For a symbol with hash `h` and an ELF class word size `C` of 64 bits, the
linker sets two bits in one Bloom word:

```
word = (h / C) % bloom_size
bit1 = 1 << (h % C)
bit2 = 1 << ((h >> bloom_shift) % C)
bloom[word] |= bit1 | bit2
```

The loader performs the same computation and, if either bit is clear, rejects
the symbol without touching the buckets. Any `bloom_size`/`bloom_shift` pair
that the linker uses consistently for setting and that the loader can use for
testing is legal — a filter with every bit set is correct but useless. The
reference implementation uses `bloom_size = 1` and `bloom_shift = 6` for every
link.

### 4.4 Buckets and chains

Hashed symbols MUST be sorted in `.dynsym` by `gnu_hash(name) % nbuckets`, so
that every bucket's symbols are contiguous. Then:

- `bucket[b]` is the `.dynsym` index of the first symbol whose bucket is `b`,
  or `0` if no symbol falls in bucket `b`. (`0` is unambiguous because
  `.dynsym[0]` is the null entry and is never hashed.)
- `chain[i]` corresponds to `.dynsym[symoffset + i]` and holds that symbol's
  hash with **bit 0 forced to the stop bit**: `chain[i] = h & ~1`, except that
  the last symbol of each bucket gets `chain[i] = (h & ~1) | 1`.

Because bit 0 is stolen, a loader compares `chain[i] | 1` against `h | 1`, or
equivalently `(chain[i] ^ h) >> 1 == 0`. Lookup walks from
`bucket[gnu_hash(name) % nbuckets]` forward through `.dynsym` until a chain
word with the stop bit set has been examined.

`nbuckets` MUST be at least 1 even when there is nothing to hash: a table with
`nbuckets = 0` makes the loader divide by zero. The reference implementation
uses `nbuckets = next_power_of_two(num_exported / 2)`, targeting two symbols
per bucket.

`symoffset` MUST equal the `.dynsym` index of the first hashed symbol, which is
also the count of unhashed (undefined and local) dynamic symbols.

## 5. `PT_DYNAMIC` and `.dynamic`

`.dynamic` is `SHT_DYNAMIC`, `SHF_ALLOC | SHF_WRITE`, `sh_entsize` 16,
`sh_addralign` 8, `sh_link` = index of `.dynstr`. A `PT_DYNAMIC` program
header MUST cover it exactly, and a `PT_LOAD` segment MUST also cover it.

Each entry is an `Elf64_Dyn`:

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 8 | `d_tag` | `Elf64_Sxword`, one of the `DT_*` values below |
| 8 | 8 | `d_un` | either `d_val` (an integer) or `d_ptr` (a virtual address), per tag |

The array MUST be terminated by a `DT_NULL` entry whose `d_un` is 0. Anything
after it is ignored. Tags may otherwise appear in any order, except that
`DT_NEEDED` entries MUST appear in the order their objects were named on the
command line, because that order is the dependency search order.

Addresses stored in `.dynamic` are **virtual addresses in the output image**,
not file offsets, and not relative to anything. `DT_STRTAB` must be the address
of `.dynstr`, not its `sh_offset`.

## 6. The `DT_*` tags

| tag | value | `d_un` | meaning and when to emit |
| --- | --- | --- | --- |
| `DT_NULL` | 0 | 0 | end of the array. MUST be the last entry. |
| `DT_NEEDED` | 1 | `d_val` | `.dynstr` offset of a dependency's name. One per shared-object input recorded, by the rule in §6.2. |
| `DT_PLTRELSZ` | 2 | `d_val` | total byte size of the PLT relocation table (`.rela.plt`). |
| `DT_PLTGOT` | 3 | `d_ptr` | address of the GOT (or of the PLT's GOT region). Emit whenever the output is dynamic. |
| `DT_HASH` | 4 | `d_ptr` | address of `.hash`. Emit only if `.hash` was written. |
| `DT_STRTAB` | 5 | `d_ptr` | address of `.dynstr`. MUST be present. |
| `DT_SYMTAB` | 6 | `d_ptr` | address of `.dynsym`. MUST be present. |
| `DT_RELA` | 7 | `d_ptr` | address of `.rela.dyn`. Emitted **iff** `.rela.dyn` has content — see the note below this table. |
| `DT_RELASZ` | 8 | `d_val` | byte size of `.rela.dyn`. Emitted with `DT_RELA`. |
| `DT_RELAENT` | 9 | `d_val` | byte size of one `Elf64_Rela` — always **24**. Emitted with `DT_RELA`. |
| `DT_STRSZ` | 10 | `d_val` | byte size of `.dynstr`, including its leading NUL. |
| `DT_SYMENT` | 11 | `d_val` | byte size of one `Elf64_Sym` — always **24**. |
| `DT_INIT` | 12 | `d_ptr` | address of the `.init` code. Emit iff `.init` has content. |
| `DT_FINI` | 13 | `d_ptr` | address of the `.fini` code. Emit iff `.fini` has content. |
| `DT_SONAME` | 14 | `d_val` | `.dynstr` offset of this object's own name. Emit iff `-soname NAME` was given. |
| `DT_RPATH` | 15 | `d_val` | `.dynstr` offset of a colon-separated search path, consulted *before* `LD_LIBRARY_PATH`. Deprecated. |
| `DT_SYMBOLIC` | 16 | — | (obsolete; use `DF_SYMBOLIC`.) |
| `DT_REL` | 17 | `d_ptr` | address of an `Elf64_Rel` table. Not used on x86-64 or AArch64. |
| `DT_RELSZ` | 18 | `d_val` | byte size of that table. |
| `DT_RELENT` | 19 | `d_val` | byte size of one `Elf64_Rel` — 16. |
| `DT_PLTREL` | 20 | `d_val` | `DT_RELA` (7) or `DT_REL` (17): which form `DT_JMPREL` uses. On x86-64 and AArch64 always `DT_RELA`. |
| `DT_DEBUG` | 21 | `d_ptr` | written as 0 by the linker; the loader stores a pointer to its own debug structure here. Emit for executables (including PIE), **not** for `-shared` output. |
| `DT_TEXTREL` | 22 | — | present, with `d_un` ignored, iff a relocation modifies a non-writable segment. Equivalent to `DF_TEXTREL`. |
| `DT_JMPREL` | 23 | `d_ptr` | address of `.rela.plt`, the relocations for PLT slots. |
| `DT_BIND_NOW` | 24 | — | obsolete spelling of `DF_BIND_NOW`. |
| `DT_INIT_ARRAY` | 25 | `d_ptr` | address of `.init_array`. Emit iff `.init_array` has content. |
| `DT_FINI_ARRAY` | 26 | `d_ptr` | address of `.fini_array`. |
| `DT_INIT_ARRAYSZ` | 27 | `d_val` | byte size of `.init_array`. MUST accompany `DT_INIT_ARRAY`. |
| `DT_FINI_ARRAYSZ` | 28 | `d_val` | byte size of `.fini_array`. MUST accompany `DT_FINI_ARRAY`. |
| `DT_RUNPATH` | 29 | `d_val` | `.dynstr` offset of a colon-separated search path, consulted *after* `LD_LIBRARY_PATH` and not inherited by transitive dependencies. Supersedes `DT_RPATH`. |
| `DT_FLAGS` | 30 | `d_val` | `DF_*` bit set, section 6.1. Emit iff non-zero. |
| `DT_PREINIT_ARRAY` | 32 | `d_ptr` | address of `.preinit_array`. Executables only; a shared object with one is an error. |
| `DT_PREINIT_ARRAYSZ` | 33 | `d_val` | byte size of `.preinit_array`. |
| `DT_GNU_HASH` | `0x6ffffef5` | `d_ptr` | address of `.gnu.hash`. |
| `DT_VERSYM` | `0x6ffffff0` | `d_ptr` | address of `.gnu.version`. |
| `DT_RELACOUNT` | `0x6ffffff9` | `d_val` | number of leading `R_*_RELATIVE` entries in `.rela.dyn`, which the loader may then apply without symbol lookup. Counts relative entries only, whereas `DT_RELASZ` covers the whole section. |
| `DT_FLAGS_1` | `0x6ffffffb` | `d_val` | `DF_1_*` bit set, section 6.1. Emit iff non-zero. |
| `DT_VERDEF` | `0x6ffffffc` | `d_ptr` | address of `.gnu.version_d`. |
| `DT_VERDEFNUM` | `0x6ffffffd` | `d_val` | number of `Elf64_Verdef` records. |
| `DT_VERNEED` | `0x6ffffffe` | `d_ptr` | address of `.gnu.version_r`. |
| `DT_VERNEEDNUM` | `0x6fffffff` | `d_val` | number of `Elf64_Verneed` records. |

**When the `DT_RELA` group is emitted.** `DT_RELA`, `DT_RELASZ` and
`DT_RELAENT` stand or fall together. Through `0.3.0` the reference writes all
three into every dynamic output; from `0.4.0` it writes them **iff** `.rela.dyn`
has content — any entry, relative or general — so a dynamic output whose
`.rela.dyn` is empty carries none of the three. That case is reachable and
graded: once relative relocations can be packed into a table of their own (the
RELR document introduced at a later milestone, §4.4), a link that packs every
relative relocation leaves `.rela.dyn` empty and MUST emit neither `DT_RELA`
nor `DT_RELASZ` nor `DT_RELAENT`. `DT_RELACOUNT` is not part of the group: it
MAY be emitted with the value 0 — the count of relative entries left in
`.rela.dyn` is then 0 — and the reference implementation always emits it.

The five version tags are described in *Symbol versioning*.

`-rpath DIR` / `--rpath DIR` MUST record a search path from the milestone that
introduces shared output onward. The reference implementation, like modern GNU
`ld` defaults, records it as `DT_RUNPATH`; emitting `DT_RPATH` instead is legal
but obsolete. Multiple `--rpath` arguments MUST be joined with `:` into one tag.

Four neighbouring GNU `ld` spellings are **not** available across the whole of
this task. Measured at every release tag:

| spelling | what the reference does |
| --- | --- |
| `-R DIR` (alias of `--rpath DIR`) | rejected as an unrecognised argument until the milestone that introduces the RISC-V backend; accepted from there on |
| `--enable-new-dtags` | rejected as an unrecognised argument until the milestone that introduces copy relocations |
| `--disable-new-dtags` | accepted from the milestone that introduces shared output. Through `0.8.0` it selects nothing — the tag written is `DT_RUNPATH` either way — but from `0.9.0` it selects `DT_RPATH`, and takes `DT_BIND_NOW` with it |
| `-h NAME` (alias of `-soname NAME`) | rejected as an unrecognised argument until the milestone that introduces the RISC-V backend |

So the `--enable-new-dtags`/`--disable-new-dtags` pair is inert in the
reference **until `0.9.0`**, and from `0.9.0` it is the switch its name
suggests: `--enable-new-dtags` (the default) writes `DT_RUNPATH`,
`--disable-new-dtags` writes `DT_RPATH` instead, one tag and not both, and the
last of the two on the command line wins. The primary spellings
`-rpath`/`--rpath` and `-soname` work from the milestone that introduces shared
output onward and are unaffected by the pair.

**Which milestone grades this.** `-R DIR` and `-h NAME` are graded nowhere in
this task: no case at any milestone passes either spelling, so accepting them
where the table says they are accepted is a correctness requirement and not a
scored one. The `--enable-new-dtags`/`--disable-new-dtags` pair is different.
It MUST be accepted at every milestone that carries this document, and the
`DT_RPATH`/`DT_RUNPATH` selection described above is graded at each milestone
whose own `TEST_INTERFACE.md` lists the pair in its graded surface; where a
milestone's contract does not list it, the option must still be accepted but
the tag it selects is not inspected.

### 6.1 `DT_FLAGS` and `DT_FLAGS_1` bits

| flag | value | meaning |
| --- | --- | --- |
| `DF_ORIGIN` | 0x1 | the object may use `$ORIGIN` in its search paths |
| `DF_SYMBOLIC` | 0x2 | the object's own definitions are searched first (`-Bsymbolic`) |
| `DF_TEXTREL` | 0x4 | a relocation modifies a read-only segment |
| `DF_BIND_NOW` | 0x8 | resolve all relocations at load time |
| `DF_STATIC_TLS` | 0x10 | the object uses static-TLS-model relocations and MUST NOT be `dlopen`ed |
| `DF_1_NOW` | 0x1 | `DT_FLAGS_1` spelling of `DF_BIND_NOW` |
| `DF_1_GLOBAL` | 0x2 | `RTLD_GLOBAL` |
| `DF_1_NODELETE` | 0x8 | never unload |
| `DF_1_INITFIRST` | 0x20 | run this object's initialisers first |
| `DF_1_NOOPEN` | 0x40 | `dlopen` of this object is an error (`-z nodlopen`) |
| `DF_1_INTERPOSE` | 0x400 | the object is an explicit interposer (`-z interpose`) |
| `DF_1_ORIGIN` | 0x80 | `$ORIGIN` processing required |
| `DF_1_PIE` | 0x08000000 | the object is a position-independent *executable* |

`-pie` output MUST set `DF_1_PIE`; that bit is how a loader tells a PIE from a
shared library, since both are `ET_DYN`. `-z now` MUST set `DF_BIND_NOW` in
`DT_FLAGS` and `DF_1_NOW` in `DT_FLAGS_1`. Because the reference
implementation binds eagerly unconditionally, it sets both on every dynamic
link, with or without `-z now`; `-z lazy` is accepted and has no effect.

### 6.2 Which inputs get a `DT_NEEDED`

Whether an input shared object that the output never references is recorded
depends on the output kind, and for executables it changed once. Measured:

| output kind | unreferenced `-l` dependency | referenced dependency |
| --- | --- | --- |
| `-shared` | recorded at every milestone | recorded |
| executable | **not** recorded at `0.3.0`; recorded from `0.4.0` on | recorded |

The string recorded is the dependency's `DT_SONAME` when it has one, and
otherwise the name the link was given for it: measured, `-Llibdir -ldep`
against a library built with `-soname libdep.so.3` records `libdep.so.3`, never
the directory or the file name. So "one per shared-object input the output
actually needs" is the executable rule at `0.3.0` only. From `0.4.0` an executable records every shared-object
input named on the command line, needed or not, and a shared object always did.
`--as-needed` is accepted and ignored (`00-compatibility-profile.md` §9), so it
does not change this. The ordering rule in §5 applies to whatever set is
recorded.

## 7. The global offset table

The GOT is an array of 8-byte slots in a writable, allocated section named
`.got` (and, where a linker splits them, `.got.plt` for the PLT's slots).
`DT_PLTGOT` names its address. A GOT slot exists so that code can load a
final address from a fixed, PC-relative location instead of having that address
patched into an instruction stream.

A slot is created for a symbol when an input relocation demands one —
`R_X86_64_GOTPCREL`, `R_X86_64_GOTPCRELX`, `R_X86_64_REX_GOTPCRELX`,
`R_X86_64_GOTTPOFF`, `R_AARCH64_ADR_GOT_PAGE`, and their relatives. Each
distinct `(symbol, kind)` pair MUST get at most one slot: two references to the
same symbol share it. TLS entries (`DTPMOD`/`DTPOFF` pairs, `TPOFF` singletons,
TLS descriptors) live in the same table and are described in
*Thread-local storage*.

How a slot is filled depends on the symbol:

| symbol | slot content at link time | dynamic relocation |
| --- | --- | --- |
| local, or non-preemptible in a non-PIC link | the final absolute address | none |
| local, or non-preemptible, in PIC output | the link-time address | `R_X86_64_RELATIVE` / `R_AARCH64_RELATIVE` with that address as the addend |
| preemptible (imported, or exportable from a shared object) | 0 | `R_X86_64_GLOB_DAT` / `R_AARCH64_GLOB_DAT` naming the symbol |
| an ifunc resolver | the resolver address | `R_X86_64_IRELATIVE` / `R_AARCH64_IRELATIVE` |

In a lazily bound image the first three GOT slots of `.got.plt` are reserved:
slot 0 holds the address of `.dynamic`, slots 1 and 2 are written by the loader
with its own identifier and its resolver entry point. A linker that binds
eagerly does not need them and MUST NOT be assumed to have reserved them; the
reference implementation reserves none and points `DT_PLTGOT` at `.got`.

Two consistency requirements follow. `DT_PLTGOT` MUST equal the address of the
section that holds the PLT's GOT slots — `.got.plt` where one exists, and
otherwise `.got` exactly. And the `r_offset` of every `GLOB_DAT`, `TPOFF64`,
`DTPMOD64`, `DTPOFF64`, `TLSDESC` and `IRELATIVE` entry a linker synthesises
for a GOT slot MUST fall inside that GOT section's address range; a GOT
relocation pointing outside the GOT is always a defect.

## 8. The procedure linkage table

**The reference implementation is always `BIND_NOW`, unconditionally, whether
or not `-z now` is on the command line.** It never builds a lazily bound PLT:
every cross-module call goes through the eager `.plt.got` form of §8.1, and
`DT_FLAGS`/`DT_FLAGS_1` always carry `DF_BIND_NOW`/`DF_1_NOW`. §8.2 describes
the classic lazy-PLT form because a linker must know how to *read* one — it
may be handed a shared object another linker built — but this task's own
output is never graded against it, and no case requires
`R_X86_64_JUMP_SLOT`, `DT_JMPREL`, `DT_PLTREL` or `DT_PLTRELSZ` to be present.
A case may assert their absence instead.

### 8.1 Eagerly bound entries (`.plt.got` form)

When the GOT slot for a function is filled before the first call, a call can
be redirected to a 16-byte stub that jumps indirectly through the slot. This is
the form the reference implementation always produces, in a section named
`.plt.got`, `SHF_ALLOC | SHF_EXECINSTR`, `sh_entsize` 16, `sh_addralign` 16.
The section name is not fixed — `.plt`, `.plt.got` and `.plt.sec` are all in
use by existing linkers and any of them is acceptable — but whatever it is
called, the section MUST be `SHT_PROGBITS`, MUST carry `SHF_ALLOC` and
`SHF_EXECINSTR`, and MUST be at least one entry (16 bytes) long whenever a
stub was synthesised. The entry is:

```
  f3 0f 1e fa                 endbr64
  f2 ff 25 <rel32>            bnd jmp *GOT(n)(%rip)
  0f 1f 44 00 00              nopl 0(%rax,%rax,1)
```

The displacement is written at byte offset 7 of the entry and is computed
against the end of the jump instruction:

```
rel32 = got_slot_address - (plt_entry_address + 0xb)
```

It MUST fit in a signed 32-bit field. The `endbr64` prefix is required for
`IBT`-marked output and harmless otherwise; the trailing `nopl` only pads the
entry to 16 bytes.

On AArch64 the 16-byte entry is four instructions:

```
  adrp x16, page(GOT(n))
  ldr  x17, [x16, #lo12(GOT(n))]
  br   x17
  nop
```

### 8.2 Lazily bound entries

A linker that binds lazily instead emits `.plt` entries of the classic form —
`jmp *GOT(n)(%rip)`, `push $index`, `jmp PLT0` — with `GOT(n)` initialised to
point at the entry's own `push`, plus a `PLT0` header that jumps through
`.got.plt[2]`. That arrangement requires `.rela.plt`, `DT_JMPREL`,
`DT_PLTREL = DT_RELA` and `DT_PLTRELSZ`, and one `R_X86_64_JUMP_SLOT`
(`R_AARCH64_JUMP_SLOT`) per entry whose `r_offset` is the GOT slot. It is
legal, but this task never requires it, and `-z now` MUST disable it.

### 8.3 Where the two relocation types go

`R_X86_64_JUMP_SLOT` (7) and `R_AARCH64_JUMP_SLOT` (1026) belong in
`.rela.plt`, one per PLT entry, in PLT-entry order, because the loader may
process them lazily and indexes them by PLT slot. `R_X86_64_GLOB_DAT` (6) and
`R_AARCH64_GLOB_DAT` (1025) belong in `.rela.dyn` and are always processed at
load time. Both write an absolute `S + A` (with `A` normally 0) into the
8-byte slot named by `r_offset`. A linker that binds eagerly emits `GLOB_DAT`
for what a lazy linker would have made a `JUMP_SLOT`.

`.rela.dyn` is `SHT_RELA`, `SHF_ALLOC`, `sh_entsize` 24, `sh_link` = index of
`.dynsym`. `.rela.plt` has the same shape with `sh_info` = index of the
section it patches. Relative relocations SHOULD be placed first in
`.rela.dyn`, which is what makes `DT_RELACOUNT` useful.

## 9. `PT_INTERP`

A dynamically linked *executable* for which an interpreter path was requested
-- by `--dynamic-linker`, or by an emulation that supplies one -- MUST carry a
`PT_INTERP` program header naming that loader, and a matching `.interp` section
(`SHT_PROGBITS`, `SHF_ALLOC`, `sh_addralign` 1) holding that NUL-terminated
path. The segment MUST be contained in a `PT_LOAD` segment. `p_vaddr` MUST
equal the address of `.interp`, and `p_filesz`, `p_memsz` and `sh_size` MUST
all equal `strlen(path) + 1` — the terminating NUL is included and nothing
else is. The path stored MUST be exactly the one requested, byte for byte; it
is never rewritten, normalised or defaulted when one was given.

The path comes from `--dynamic-linker PATH`. There is **no built-in default**:
with no `--dynamic-linker` and no emulation that supplies one, the output gets
no `PT_INTERP` and no `.interp` at all, at every milestone -- a dynamically
linked executable is produced without an interpreter rather than with a guessed
one. The one emulation that does supply a default is `-m elf_x86_64_sol2`,
which yields `/lib/amd64/ld.so.1` from the milestone that introduces it --
the same milestone adds `13-symver-and-platforms.md`, whose §2.3 governs that
case. `--no-dynamic-linker`
suppresses the segment explicitly, and is the way to state the intent rather
than relying on the absence of the option.
GNU `ld` also spells this `-I PATH`; the reference rejects `-I` as an
unrecognised argument at every milestone of this task, so no milestone grades
that alias — use the long spelling.

A shared object MUST NOT have `PT_INTERP`, and its `e_entry` MUST be 0 unless
an entry point was explicitly requested.

## 10. `-shared`, `-pie` and non-PIC executables

| | `-shared` | `-pie` | default executable |
| --- | --- | --- | --- |
| `e_type` | `ET_DYN` | `ET_DYN` | `ET_EXEC` |
| `e_entry` | 0 | address of `_start` | address of `_start` |
| `PT_INTERP` | absent | present iff a path was requested (§9) | present iff a path was requested (§9) |
| base address | 0 | 0 | a fixed address (`0x400000` on x86-64) |
| `DT_DEBUG` | absent | present | present iff dynamic |
| `DF_1_PIE` | clear | set | clear |
| undefined symbols | allowed by default; become imports | see below | see below |

An unresolved undefined symbol in a `-pie` or default-executable link is an
error **from the milestone that introduces copy relocations onward**; at the
milestone that introduces shared output the reference still links such an
output successfully, so that diagnosis is not graded here. Where it is an
error, `--allow-shlib-undefined` may still permit it for symbols that come
from a shared object.

`-no-pie` / `--no-pie` forces the third column, but the reference rejects both
spellings as unrecognised arguments until the milestone that introduces copy
relocations, so no milestone grades the default-executable column by asking for
it explicitly — omit every mode option instead. `-shared` and `-pie` are
conceptually exclusive, but passing them together is not diagnosed by the
reference at any milestone and no milestone grades that either.
Both `ET_DYN` forms MUST be laid out from address 0 so the loader can choose
the base; every absolute address embedded in the image is therefore
load-base-relative and must be fixed up at load time, which is what section 11
is about.

## 11. `R_X86_64_RELATIVE` and absolute addresses in PIC output

Any 8-byte slot in a writable, allocated section that must hold the
**address of something in this same image** cannot be finalised at link time in
`ET_DYN` output, because the base is unknown. The linker MUST write the
link-time address into the slot and emit

| type | value | `r_offset` | `r_addend` | loader action |
| --- | --- | --- | --- | --- |
| `R_X86_64_RELATIVE` | 8 | address of the slot | link-time value of the target | `*slot = base + addend` |
| `R_AARCH64_RELATIVE` | 1027 | address of the slot | link-time value of the target | `*slot = base + addend` |

This applies to GOT slots for local symbols, to `.data` words holding function
or object addresses, to `.init_array`/`.fini_array` entries, and to vtables.
It does **not** apply to PC-relative references, which need no fixup, nor to
anything in a read-only section that a PIC compiler would have made
PC-relative already.

The addend MUST be the target's link-time virtual address, not zero: the
loader adds the base and does no symbol lookup at all, which is exactly why
`DT_RELACOUNT` lets it batch them. For the same reason the symbol index in
`r_info` MUST be 0.

Exactly **one** `R_X86_64_RELATIVE` MUST be emitted per slot that needs one —
no duplicates, and none for a slot that does not need one. In particular a
word of plain integer data that merely happens to look like an address, and a
PC-relative instruction operand, MUST NOT acquire a relocation, and a
PC-relative reference to a local symbol MUST NOT be rerouted through the GOT
just because the output is position-independent. A `-pie` link of inputs
containing *n* planted pointers and some plain integers must produce exactly
*n* `RELATIVE` entries, at the *n* pointer addresses, and no `GLOB_DAT`,
`JUMP_SLOT`, `COPY` or `R_X86_64_64` alongside them.

`R_X86_64_RELATIVE` MUST NOT be emitted in a static non-PIE link. A case may
assert its absence there. More strongly, a non-PIE link with no dynamic inputs
MUST contain **no dynamic relocations of any kind**: every address is final, so
each pointer slot holds the target's absolute address written directly into the
image. Linking the same objects with `-pie` must leave the plain, non-address
data bit-identical while moving each pointer's value out of the image and into
a relocation addend.

### 11.1 Absolute relocations against preemptible symbols

An absolute-width relocation — `R_X86_64_64`, `R_X86_64_32`, `R_X86_64_32S`,
`R_AARCH64_ABS64` and friends — placed against a **preemptible** symbol in
position-independent output has no correct static answer. The symbol's final
address is whatever module wins the run-time lookup, which is not knowable at
link time, and `R_*_RELATIVE` cannot express it because it performs no lookup.
The linker's options are:

- If the slot is 8 bytes, writable and word-aligned, turn it into a dynamic
  `GLOB_DAT`- or `ABS64`-class relocation naming the symbol. This is what a
  linker does for data pointers, and it is required from the milestone that
  introduces shared output.
- If the slot is narrower than a pointer (`R_X86_64_32`, `R_X86_64_32S`,
  `R_X86_64_PC32` against a preemptible symbol in `-shared`), or lives in a
  read-only section, there is nothing to emit. **This is graded only from the
  final milestone on.** Measured directly against the reference: from the
  milestone that introduces shared output through the one before the last,
  such a link succeeds anyway, with exit status 0 and the wrong value silently
  baked into the slot instead of a dynamic relocation or a failure — there is
  no diagnostic and no case in this task may require one before the final
  milestone. From the final milestone on, the link MUST fail with a non-zero
  exit status and at least one line on stderr that names the relocation type
  and the symbol, and MUST NOT leave a plausible output file behind. No
  prefix, phrasing or particular exit value is graded. GNU `ld` phrases this
  as "relocation R_X86_64_32 against `sym` can not be used when making a
  shared object; recompile with -fPIC", and the same information MUST be
  present here.

Emitting the relocation into a read-only section instead, and setting
`DT_TEXTREL`/`DF_TEXTREL` so the loader makes the text writable, is legal but
is not what this task expects; `-z text` (the default in modern toolchains)
forbids it outright, and `-z notext` permits it.

## 12. `--version-script` at this milestone

`--version-script=FILE` (both dash spellings; `FILE` may also follow as a
separate argument) supplies a script of the form

```
VERSION_NAME {
    global:
        exported_one;
        exported_two;
    local:
        *;
};
```

with an optional `VERSION_NAME` (an anonymous `{ ... };` node is legal) and an
optional trailing `PARENT_NAME` after the closing brace. At step 2 a linker
MUST at minimum honour the **scoping** effect:

- A symbol matched by a pattern under `local:` MUST NOT appear in `.dynsym`,
  and its name MUST NOT appear in `.dynstr` either — a localised symbol leaves
  the dynamic tables entirely, it is not merely demoted within them. It becomes
  `STB_LOCAL` in the output and is not preemptible; references to it from
  inside the image bind directly and need no GOT indirection.
- A symbol matched under `global:` is exported if it is defined.
- `*` matches everything, so `local: *;` reduces the export set to exactly the
  `global:` list. Matching is shell-glob style (`*`, `?`, `[...]`), and a name
  may be quoted.
- `extern "C++" { ... }` blocks are parsed; their contents are demangled-name
  patterns.

Localising is **not** discarding. A symbol hidden by `local:` keeps its
definition, its address and its `.symtab` entry; only its export status
changes. A `global:` pattern that matches nothing is still applied — the result
is a valid `ET_DYN` output with an empty export set (a `.dynsym` holding only
the reserved null entry), never a silently unfiltered one. Conversely, linking
the same objects with no version script at all exports every defined global,
so the difference between the two links is exactly what the script did.

The version *naming* effect — attaching `VERSION_NAME` to each exported symbol
and emitting `.gnu.version`/`.gnu.version_d` — is described in
*Symbol versioning* and is not required at step 2.

Note that making a symbol local this way also removes it from the `.gnu.hash`
index and therefore changes `symoffset`, `nbuckets` and the chain array; the
hash table MUST be built after version-script scoping has been applied, never
before.

## 13. `-z` options

| option | requirement |
| --- | --- |
| `-z now` | set `DF_BIND_NOW` and `DF_1_NOW`; resolve PLT relocations eagerly |
| `-z lazy` | the opposite; MUST be accepted, MAY be ignored |
| `-z relro` | place the GOT, `.dynamic`, `.init_array`, `.fini_array` and other post-relocation-constant data at the end of a writable `PT_LOAD` and cover them with a `PT_GNU_RELRO` program header, so the loader can `mprotect` them read-only after relocation |
| `-z norelro` | suppress `PT_GNU_RELRO` |
| `-z notext` | permit dynamic relocations against read-only sections, setting `DT_TEXTREL` |
| `-z text` | forbid them (the default) |
| `-z noexecstack` | emit `PT_GNU_STACK` with `p_flags = PF_R|PF_W` |
| `-z execstack` | emit `PT_GNU_STACK` with `p_flags = PF_R|PF_W|PF_X` |
| `-z nocopyreloc` | refuse to synthesise copy relocations (section 13.2) |
| `-z origin` | set `DF_ORIGIN` in `DT_FLAGS` and `DF_1_ORIGIN` in `DT_FLAGS_1`, marking the object as needing `$ORIGIN` processing at load time (section 6.1). Accepted at every milestone that accepts `-z` at all; it first has an observable effect at step 4, and is accepted and ignored before that |

This table covers the `-z` options that shape the **dynamic image**, and it is
not the whole `-z` surface. Four further spellings belong to *symbol
resolution policy* rather than to the image and are specified in
`TEST_INTERFACE.md`, in the section for the milestone that grades them, not
here: `-z defs` and `-z undefs` (whether an undefined symbol is fatal in a
`-shared` link), `-z muldefs` (the `--allow-multiple-definition` synonym) and
`-z interpose` (`DF_1_INTERPOSE` in `DT_FLAGS_1`). They are named here only so
that this table is not mistaken for an exhaustive list.

### 13.1 `PT_GNU_RELRO`

RELRO exists because some writable data is only writable during relocation:
once the loader has finished, the GOT, `.dynamic`, `.init_array`,
`.fini_array`, `.preinit_array`, `.got.plt` under `-z now` and `.data.rel.ro`
never change again. Making that region read-only afterwards removes a large
class of attack.

To make that possible the linker MUST **partition the writable segment**:
every post-relocation-constant section is placed contiguously at the *start*
of the writable `PT_LOAD`, every genuinely writable section (`.data`, `.bss`)
after them, and a `PT_GNU_RELRO` header covers the first group.

| field | value |
| --- | --- |
| `p_type` | `PT_GNU_RELRO` = `0x6474e552` |
| `p_vaddr` | start of the writable `PT_LOAD` — which is also the start of the RELRO group |
| `p_offset` | matching file offset |
| `p_filesz` | size of the RELRO group |
| `p_memsz` | the same, or larger if the group ends in `NOBITS` |
| `p_flags` | `PF_R` |
| `p_align` | small; the reference emits 8 |

Three consequences are requirements. The segment MUST be non-empty when it is
emitted at all. It MUST lie entirely inside one `PT_LOAD` that has `PF_W`.
And **no writable section may straddle its end**: a section is either wholly
inside the RELRO window or wholly after it, never split by it, because the
loader protects whole pages and a straddling section would be half frozen.
`.got`, `.got.plt`, `.dynamic`, `.init_array`, `.fini_array` and
`.data.rel.ro` MUST be inside it; `.data` and `.bss` MUST be outside it. Under
`-z now` every GOT slot,
including each one fixed up by a `GLOB_DAT`, MUST fall inside the window.

Note that `p_vaddr` is **not** required to be page-aligned; it starts where
the writable `PT_LOAD` starts. It is the *end* that the loader rounds — down
to a page boundary — which is why the partition, not the alignment, is what
the linker must get right.

At the milestone that introduces shared output, a linker MUST accept `-z relro`
and `-z norelro` without failing but is not required to produce the segment.
From the milestone that introduces copy relocations, a `PT_GNU_RELRO` segment
MUST be present in dynamic output, and at that milestone `-z norelro` MAY be
accepted and ignored: the reference implementation emits the segment
unconditionally there, with no diagnostic on either spelling of the option, so
honouring `-z norelro` is permitted but not required. From the next milestone
(script-driven layout) onward, `-z norelro` MUST actually suppress the
segment — the reference does this too, still without any diagnostic, and a
case is entitled to hold a linker to it from that point on.

### 13.2 Copy relocations and `-z nocopyreloc`

A **non-PIC executable** that references a data object defined by a shared
library has a problem: the reference is a direct, absolute or PC-relative
address baked into read-only text, and the object lives in the library, at an
address the loader picks. The classical answer is a *copy relocation*.

When the linker sees a direct (non-GOT) relocation from a non-writable section
of an executable against an `STT_OBJECT` symbol defined in a shared object, it
MUST:

1. Reserve `st_size` bytes, aligned to the library symbol's alignment, in the
   executable's `.bss` (which MUST remain `SHT_NOBITS`).
2. Redefine the symbol **in the executable** at that address, as
   `STT_OBJECT`, `STB_GLOBAL`, `STV_DEFAULT`, carrying the size taken from
   the library's definition — so the symbol is no longer undefined.
3. Emit the symbol into **both** `.symtab` and `.dynsym`, so that the library's
   own references are preempted by the executable's copy at run time.
4. Emit exactly one `R_X86_64_COPY` (5) / `R_AARCH64_COPY` (1024) whose
   `r_offset` is that `.bss` address and whose symbol is the name. The loader
   copies `st_size` bytes from the library's definition into it at load time.
5. Resolve every reference in the executable, including the one that triggered
   this, against the new local definition — no GOT indirection is needed.

Exactly one copy relocation per symbol, never more. A copy relocation MUST NOT
be created for a function, for output that is `-shared` or `-pie`, or for a
reference from a writable section (which can use a `GLOB_DAT`-class slot
instead).

`-z nocopyreloc` disables step 1 onwards. The same link MUST then fail with a
non-zero exit status and at least one line on stderr naming **the symbol** and
either **the input relocation type** or the fact that copy relocations are
disabled — enough for a reader to find the reference. Without the flag the
identical link MUST succeed.

## 14. Order of operations

The dependencies among the pieces above force this order:

1. Resolve symbols; apply `--version-script` scoping and visibility so the
   export set is final.
2. Decide, per symbol, whether it is preemptible, and therefore which
   references need GOT or PLT entries. Allocate those entries.
3. Lay out sections and assign addresses.
4. Sort `.dynsym`: unhashed entries (null, undefined, local) first, then
   exported definitions sorted by `gnu_hash(name) % nbuckets`. Write `.dynstr`.
5. Build `.gnu.hash` from that ordering.
6. Build `.gnu.version`, `.gnu.version_r` and `.gnu.version_d` — they are
   indexed by `.dynsym` index, so they must follow step 4.
7. Emit `.rela.dyn` (relative entries first) and `.rela.plt`.
8. Write `.dynamic`, whose tag values are the addresses and sizes settled by
   steps 3 to 7.

A `.dynsym` reordering after step 5 invalidates the hash table silently — the
loader will simply fail to find symbols — so the ordering MUST be frozen before
the table is built.
