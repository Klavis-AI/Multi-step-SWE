# Symbol versioning

> The three `.gnu.version*` sections, the records inside them, and how `name@version` and `name@@version` are encoded, requested and resolved.

## Provenance

This restates the symbol-versioning chapter of the Linux Standard Base Core
Specification 5.0.0, Generic Part, §10.7 "Symbol Versioning"
(<https://refspecs.linuxfoundation.org/lsb.shtml>), which is itself the written
form of the GNU extension Ulrich Drepper introduced in glibc 2.1. The structure
layouts were checked field by field against `<elf.h>` as shipped with glibc,
and the constants against the `SHT_GNU_verdef` / `SHT_GNU_verneed` /
`SHT_GNU_versym` definitions there. The version-script syntax that feeds these
sections is `ld.texi`'s "VERSION" node; only the parts that determine the
contents of the three sections appear here, the scoping effect being covered in
*Producing dynamic output* §12.

Deliberately narrowed: the loader's side of versioning — how `ld.so` walks
`.gnu.version_r` at load time, what it does on a version mismatch, and
`dlvsym` — is out of scope. Sun's original `SHT_SUNW_*` spellings are omitted.
Version *filtering* (`DT_FILTER`, `DT_AUXILIARY`) is a different mechanism and
is not covered.

Two notes where the reference implementation constrains what this document can
require:

1. **Consuming versions comes before producing them.** At the milestone this
   document first ships, the reference reads versioned shared objects and emits
   `.gnu.version` plus `.gnu.version_r` — it does not emit `.gnu.version_d`,
   and its version-script parser accepts only an anonymous `{ ... };` node.
   Producing version *definitions* arrives a milestone later. Sections 4 and 7
   therefore define `Elf64_Verdef`/`Elf64_Verdaux` in full but mark emission as
   belonging to that later milestone.
2. **Entry 0 of `.gnu.version`.** The reserved null `.dynsym` entry has no
   version. The reference writes `VER_NDX_GLOBAL` (1) there rather than
   `VER_NDX_LOCAL` (0); GNU `ld` writes 0. Either is accepted, and nothing may
   require one specific value for index 0.

## 1. The problem versioning solves

A shared library that changes the behaviour of an exported function breaks
every program already linked against it. Versioning lets the library keep both
behaviours under one name: the old definition is tagged `foo@VER_1`, the new
one `foo@@VER_2`. A program linked before the change recorded that it wanted
`foo` at version `VER_1` and keeps getting the old one; a program linked after
gets the new one. The two definitions coexist in one `.dynsym` as two entries
with the same name string and different version indices.

Three sections implement it, all `SHF_ALLOC`, all in the read-only part of the
image:

| section | `sh_type` | value | holds |
| --- | --- | --- | --- |
| `.gnu.version` | `SHT_GNU_versym` | `0x6fffffff` | one 2-byte index per `.dynsym` entry |
| `.gnu.version_d` | `SHT_GNU_verdef` | `0x6ffffffd` | the versions **this file defines** |
| `.gnu.version_r` | `SHT_GNU_verneed` | `0x6ffffffe` | the versions **this file requires** of its dependencies |

Version **indices** are per-file small integers. Index 0 and index 1 are
reserved; a file's own definitions and the versions it requests are numbered
from 2 upwards, in one shared numbering space — a file that defines two
versions and requires one from a dependency uses indices 2, 3 and 4, and no
index is ever reused for two different versions within one file.

| index | name | meaning |
| --- | --- | --- |
| 0 | `VER_NDX_LOCAL` | the symbol is local; it is not visible outside this file |
| 1 | `VER_NDX_GLOBAL` | the symbol is global and unversioned — the base, unnamed version |
| 2 … | — | a version named by a `Verdef` or a `Vernaux` record in this file |
| `0xff00` | `VER_NDX_LORESERVE` | start of the reserved range |
| `0xff01` | `VER_NDX_ELIMINATE` | private to the linker: a symbol to be dropped entirely, never written to an output `.gnu.version` |

Bit 15 of a `.gnu.version` entry is a flag, not part of the index:

| constant | value | meaning |
| --- | --- | --- |
| `VERSYM_VERSION` | `0x7fff` | mask that extracts the index |
| `VERSYM_HIDDEN` | `0x8000` | the definition is **not** the default one for its name |

`llvm-readelf -V` renders the hidden bit as an `h` immediately after the
number, as in `2h(VER_1)`.

## 2. `.gnu.version`

`.gnu.version` is an array of `Elf64_Half` (`Elf64_Versym`), 2 bytes each,
**exactly parallel to `.dynsym`**. Entry *i* gives the version of `.dynsym`
entry *i*. This is the whole of its structure; there is no header.

| section-header field | required value |
| --- | --- |
| `sh_type` | `SHT_GNU_versym` (`0x6fffffff`) |
| `sh_flags` | `SHF_ALLOC` |
| `sh_entsize` | 2 |
| `sh_addralign` | 2 |
| `sh_link` | the section index of `.dynsym` |
| `sh_info` | 0 |
| `sh_size` | 2 × the number of `.dynsym` entries |

The parallelism is a hard requirement, and it is the one most easily broken:
the number of entries MUST equal the number of `.dynsym` entries exactly, and
`sh_link` MUST name `.dynsym`. If `.dynsym` is reordered after `.gnu.version`
is built — for instance to satisfy the `.gnu.hash` bucket ordering — the array
MUST be rebuilt, not merely resized.

What goes in each entry:

| the `.dynsym` entry is | entry value |
| --- | --- |
| the reserved index-0 null entry | 0 or 1; nothing depends on which |
| a definition with no version | `VER_NDX_GLOBAL` (1) |
| a definition of `name@@V` (the default for `name`) | the index of `V`, **without** `VERSYM_HIDDEN` |
| a definition of `name@V` (not the default) | the index of `V`, **with** `VERSYM_HIDDEN` set |
| an undefined reference wanting version `V` from a dependency | the index assigned to `V` in this file's `.gnu.version_r` |
| an undefined reference wanting no particular version | `VER_NDX_GLOBAL` (1) |

`VER_NDX_LOCAL` never appears against a symbol that is in `.dynsym` at all,
because a local symbol is not in `.dynsym`. It exists so that tools can mark
the null entry and so that the index space starts where it does.

Input `.gnu.version` values are translated, not copied: an index the input
object used is meaningless in the output, whose numbering is its own. A linker
MUST build a per-input-file mapping from input version index to output version
index, pass values `≤ VER_NDX_GLOBAL` through unchanged, and map everything
else through that table.

## 3. `.gnu.version_r` — versions required

One `Elf64_Verneed` record per shared library from which this file requests at
least one *named* version, each followed by one `Elf64_Vernaux` per version
requested from that library. Records are chained by byte offsets, not indices,
so a reader walks them without knowing how many there are.

### `Elf64_Verneed`, 16 bytes

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 2 | `vn_version` | record format revision; MUST be `VER_NEED_CURRENT` = 1 |
| 2 | 2 | `vn_cnt` | number of `Vernaux` entries that follow this record |
| 4 | 4 | `vn_file` | `.dynstr` offset of the dependency's filename — its `DT_SONAME`, which MUST be the same string this file's `DT_NEEDED` names |
| 8 | 4 | `vn_aux` | byte offset **from the start of this `Verneed`** to its first `Vernaux`; 16 when the auxiliaries follow immediately |
| 12 | 4 | `vn_next` | byte offset from the start of this `Verneed` to the next one; **0 in the last record** |

### `Elf64_Vernaux`, 16 bytes

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `vna_hash` | the ELF hash (section 6) of the version name |
| 4 | 2 | `vna_flags` | `VER_FLG_WEAK` (2) if the dependency on this version is weak, else 0 |
| 6 | 2 | `vna_other` | **the version index this file assigns to this version** — the value that appears in `.gnu.version` for symbols wanting it |
| 8 | 4 | `vna_name` | `.dynstr` offset of the version name, e.g. `VER_1` |
| 12 | 4 | `vna_next` | byte offset from the start of this `Vernaux` to the next one; **0 in the last auxiliary of a record** |

Requirements on the table as a whole:

- Each `Verneed` MUST name **its own** library and carry **only** that
  library's versions. A version defined by `libaa.so.1` may never appear under
  the `Verneed` for `libbb.so.2`.
- `vn_cnt` MUST equal the number of `Vernaux` entries actually chained from
  that record.
- Only versions **actually referenced** may be requested. A dependency that
  defines `A_ONE` and `A_TWO` but from which only `A_ONE` is used yields a
  `Verneed` with `vn_cnt` 1 naming only `A_ONE`. Requesting an unused version
  is a defect, not a harmless extra.
- Every `vna_other` MUST be ≥ 2, and distinct across the whole file — two
  versions, whether from the same library or different ones, never share an
  index.
- Section-header fields: `sh_type` `SHT_GNU_verneed`, `sh_flags` `SHF_ALLOC`,
  `sh_link` = the section index of `.dynstr` (the strings are named by offset
  into it), `sh_info` = the number of `Verneed` records, `sh_entsize` 0,
  `sh_addralign` 8.
- `DT_VERNEEDNUM` MUST equal that same count, and `DT_VERNEED` its address.

A dependency from which only unversioned symbols are used contributes no
`Verneed` record at all.

## 4. `.gnu.version_d` — versions defined

Emitted by a file that defines versions of its own — a shared library built
with a version script that names its nodes. One `Elf64_Verdef` per version,
each followed by one `Elf64_Verdaux` for the version's own name and one more
for each parent it inherits from.

### `Elf64_Verdef`, 20 bytes

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 2 | `vd_version` | record format revision; MUST be `VER_DEF_CURRENT` = 1 |
| 2 | 2 | `vd_flags` | `VER_FLG_BASE` (1) for the file's own base entry, `VER_FLG_WEAK` (2) for a weak version, else 0 |
| 4 | 2 | `vd_ndx` | the version index this definition is given; also what `.gnu.version` stores for symbols defined at this version |
| 6 | 2 | `vd_cnt` | number of `Verdaux` entries chained from this record: 1 for a version with no parent, 1 + *p* with *p* parents |
| 8 | 4 | `vd_hash` | the ELF hash (section 6) of the version name |
| 12 | 4 | `vd_aux` | byte offset from the start of this `Verdef` to its first `Verdaux`; 20 when they follow immediately |
| 16 | 4 | `vd_next` | byte offset from the start of this `Verdef` to the next one; **0 in the last record** |

### `Elf64_Verdaux`, 8 bytes

| offset | size | field | meaning |
| --- | --- | --- | --- |
| 0 | 4 | `vda_name` | `.dynstr` offset of a name: the version's own name in the first auxiliary, a parent version's name in each subsequent one |
| 4 | 4 | `vda_next` | byte offset from the start of this `Verdaux` to the next one; **0 in the last** |

The first record is conventionally the **base** definition: `vd_flags` =
`VER_FLG_BASE`, `vd_ndx` = 1, `vd_cnt` = 1, and its single `Verdaux` names the
file itself — its `DT_SONAME` where one was given, otherwise the output
filename. No symbol is ever tagged with the base index as a *defined* version;
index 1 means "unversioned". The base record exists so that the file's identity
appears in the table.

Subsequent records take indices 2, 3, … in the order the version script
declares them. A node declared as `VER_2 { ... } VER_1;` inherits from `VER_1`:
its `vd_cnt` is 2, its first `Verdaux` names `VER_2` and its second names
`VER_1`.

Section-header fields: `sh_type` `SHT_GNU_verdef`, `sh_flags` `SHF_ALLOC`,
`sh_link` = the section index of `.dynstr`, `sh_info` = the number of `Verdef`
records (including the base), `sh_entsize` 0, `sh_addralign` 8.
`DT_VERDEFNUM` MUST equal that count and `DT_VERDEF` its address.

Emission of this section belongs to the milestone that introduces version
*definitions*; before that, a linker consumes `.gnu.version_d` from its inputs
(it is how it learns which versions a dependency offers) without producing one.

## 5. Dynamic array tags

| tag | value | `d_un` | emitted when |
| --- | --- | --- | --- |
| `DT_VERSYM` | `0x6ffffff0` | `d_ptr` | address of `.gnu.version`; whenever that section exists |
| `DT_VERDEF` | `0x6ffffffc` | `d_ptr` | address of `.gnu.version_d` |
| `DT_VERDEFNUM` | `0x6ffffffd` | `d_val` | number of `Verdef` records |
| `DT_VERNEED` | `0x6ffffffe` | `d_ptr` | address of `.gnu.version_r` |
| `DT_VERNEEDNUM` | `0x6fffffff` | `d_val` | number of `Verneed` records |

`DT_VERDEF` and `DT_VERDEFNUM` MUST be emitted together, and likewise
`DT_VERNEED` and `DT_VERNEEDNUM`; a count without an address, or an address
without a count, is unusable. All four MUST be omitted when the corresponding
section is absent. `DT_VERSYM` MUST be present whenever either of the other
pairs is, because the index array is what connects them to symbols.

## 6. The version-name hash

`vd_hash` and `vna_hash` both hold the classic System V ELF hash of the
version-name string — the same function `.hash`'s bucket index uses, not the
GNU hash. It is computed over the bytes of the name, excluding the NUL:

```c
uint32_t elf_hash(const unsigned char *name)
{
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        g = h & 0xf0000000u;
        if (g)
            h ^= g >> 24;
        h &= ~g;
    }
    return h;
}
```

The result is always ≤ `0x0fffffff`, because the top nibble is cleared on every
iteration. Worked values a linker can check itself against:

| name | `elf_hash` |
| --- | --- |
| `VER_1` | `0x005aa821` |
| `VER_2` | `0x005aa822` |
| `libver.so.1` | `0x0c4779d1` |

The hash is a lookup accelerator for the loader; it MUST be correct, because a
loader that misses on the hash reports the version as missing even though the
record is present.

## 7. `name@version` versus `name@@version`

A source file or assembly stub attaches a version to a symbol with the
`.symver` directive:

```
        .symver getval_v1, getval@VER_1     /* non-default */
        .symver getval_v2, getval@@VER_2    /* default     */
```

The single `@` and the double `@@` mean different things, and both the encoding
and the resolution rules differ.

| | `name@V` | `name@@V` |
| --- | --- | --- |
| role | an older, still-supported definition | the definition `name` resolves to today |
| `.gnu.version` entry, at a definition | index of `V`, `VERSYM_HIDDEN` **set** | index of `V`, `VERSYM_HIDDEN` **clear** |
| matched by a reference to plain `name` | no | yes |
| matched by a reference to `name@V` | yes | yes |
| how many per name | any number | **at most one** |

So the hidden bit is exactly the answer to "may an unversioned reference land
here?". At most one definition of a given name in a file may have it clear;
two default definitions of one name are an error.

On the **reference** side, an object that writes `.symver ref, name@V` produces
an undefined symbol whose name string is `name` and which carries a request for
version `V`. The linker resolves it by searching its dependencies for a
definition of `name` at exactly version `V`; an unversioned reference searches
for the default definition of `name` instead.

Four consequences, all of them requirements:

1. A reference to `name@V1` and a reference to plain `name` that resolves to
   `name@@V2` are **two distinct dynamic symbols**. They MUST get two
   `.dynsym` entries, two different `.gnu.version` indices, and — because they
   may resolve to different addresses at run time — two distinct GOT slots and
   two distinct dynamic relocations. Collapsing them into one entry because the
   name strings are equal defeats the entire mechanism.
2. An unversioned reference MUST bind to the `@@` default and MUST NOT drag in
   any other version. Linking a program that uses only plain `getval` against a
   library defining `getval@VER_1` and `getval@@VER_2` yields one `Verneed`
   with `vn_cnt` 1, naming `VER_2` only.
3. `.dynstr` holds the bare name once; the version is carried by the index
   array, not by the string. Readers such as `llvm-readelf` print
   `getval@VER_1`, but that `@VER_1` is reconstructed from `.gnu.version` and
   `.gnu.version_r` — it is not in the string table. A linker MUST NOT write
   `@` into `st_name`.
4. Versioned and unversioned dependencies coexist. A dependency with no version
   definitions contributes `DT_NEEDED` and nothing else; its symbols get
   `VER_NDX_GLOBAL`.

### Inside a version script

A version script node both names a version and selects the symbols that take
it:

```
VER_1 { global: getval; local: *; };
VER_2 { global: getval; } VER_1;
```

A symbol listed in node *V* and defined in this file is exported at version
*V*. It becomes the **default** for its name — `name@@V` — unless another node
already claimed that role for the same name, in which case it is `name@V`. An
explicit `.symver` in the input overrides the script's choice for that symbol.
A symbol matched by `local:` is not exported at all and gets no version index,
because it is not in `.dynsym`; see *Producing dynamic output* §12.

## 8. When a requested version is not available

If an undefined symbol requests `name@V` and no dependency defines `name` at
version `V` — because the library is older than the object, or because the
version name is simply wrong — there is no correct output. The reference the
program makes cannot be satisfied at load time, and silently binding it to a
different version of `name` would be exactly the ABI break versioning exists to
prevent.

A linker SHOULD therefore treat this as an undefined-symbol error: fail the
link with a non-zero exit status and at least one line on stderr naming the
symbol and the version it asked for, in the spelling `name@V` so a reader can
locate the `.symver`. GNU `ld` reports `undefined reference to
'getval@VER_9'`. Binding the reference to the default definition instead,
or emitting it as an unversioned import, MUST NOT happen: either hides a real
incompatibility.

This is a SHOULD and not a MUST only because the reference implementation does
not diagnose it at the milestone this document first ships — it drops the
reference from `.dynsym` and exits successfully. Nothing grades the diagnostic,
but a linker that implements the check is more correct, not less.

## 9. Build order

Versioning is downstream of symbol resolution and of `.dynsym` ordering, and
upstream of `.dynamic`:

1. Read each dependency's `.gnu.version_d` to learn which versions it defines
   and at which of *its* indices, and its `.gnu.version` to learn which
   definition carries which version and which is the default.
2. Resolve undefined symbols, honouring each reference's requested version.
3. Decide the export set (visibility, then version-script scoping), so
   `.dynsym`'s membership is final.
4. Order and write `.dynsym`, and build `.gnu.hash` from that order.
5. Assign output version indices — the file's own definitions first, then one
   per (library, version) pair actually used — and build `.gnu.version_d` and
   `.gnu.version_r`.
6. Write `.gnu.version` by walking `.dynsym` in its final order and mapping
   each symbol's input version index through the per-file translation table.
7. Emit `DT_VERSYM`, `DT_VERDEF`/`DT_VERDEFNUM`, `DT_VERNEED`/`DT_VERNEEDNUM`.

Steps 4 and 6 MUST NOT be reordered: `.gnu.version` is positional, so it can
only be written once `.dynsym`'s order is frozen.
