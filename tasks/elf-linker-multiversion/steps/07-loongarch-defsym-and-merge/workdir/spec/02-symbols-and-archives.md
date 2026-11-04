# Symbols, resolution and archives

> How a linker interprets binding, type and visibility, which of several definitions of a name wins, and how `ar` archives are searched lazily for the members that satisfy still-undefined references.

## Provenance

The symbol model — bindings, types, visibilities and the rules for combining
them — restates the symbol-table chapter of the **System V gABI**
(`https://www.sco.com/developers/gabi/latest/ch4.symtab.html`) and its
`STV_*` supplement, checked against glibc 2.36's `<elf.h>` for the numeric
values. The precedence rules for weak, common and archive symbols are not
fully specified by the gABI; where it is silent this document follows the
behaviour of the reference implementation, measured directly against
`wild` **0.2.0** (`wild_lib/src/resolution.rs`, `symbol_db.rs`), which in turn
follows GNU `ld` as documented in `ld.texi` §"Options" and §"Orphan Sections".

Two behaviours below were measured rather than read, and both diverge from
GNU `ld`: archive extraction takes its fixpoint over every archive on the
command line at once rather than scanning each archive once, left to right
(§7.2); and a `STV_HIDDEN` or `STV_INTERNAL` symbol keeps its `STB_GLOBAL`
binding in the output `.symtab` rather than being demoted to `STB_LOCAL`
(§1.1). Both were confirmed against `wild` 0.2.0.

The `ar` format has no standard. This restates the **System V / GNU variant**
as implemented by binutils 2.40 (`bfd/archive.c`, `bfd/archive64.c`) and by
`wild` 0.2.0's `wild_lib/src/archive.rs`, which is the only variant the
grader's `llvm-ar-16`/`ar` produce. BSD long names (`#1/<len>`) and the
Windows/ECOFF variants are deliberately out of scope.

Deliberately narrowed: symbol *versioning* (`@`/`@@` names, `SHT_GNU_versym`)
and everything that only matters when producing or consuming shared objects
is described by a later milestone's document, not here. This document covers
the static link completely.

Where this document says a linker MUST diagnose something, the diagnostic
obeys `00-compatibility-profile.md` §8: non-zero exit and the offending name
somewhere on stderr. No prefix or particular exit value is required.

## 1. The three coordinates of a symbol

Every symbol carries three orthogonal properties, packed into `st_info` and
`st_other` as described in `01-elf-container.md` §6. They answer three
different questions and a linker must not conflate them.

**Binding** (`ELF64_ST_BIND(st_info)`) answers *who may see this name and
which definition wins*.

| value | name | meaning to the linker |
| --- | --- | --- |
| `0` | `STB_LOCAL` | Not visible outside its own object file. Never participates in cross-file resolution; several files MAY each have a local symbol of the same name and they remain distinct. Locals MUST precede globals in the symbol table. |
| `1` | `STB_GLOBAL` | Visible to every object being combined. A *strong* definition. |
| `2` | `STB_WEAK` | Visible like a global, but yields to a strong definition of the same name, and, when undefined, is allowed to stay undefined. |
| `10` | `STB_GNU_UNIQUE` | A GNU extension: treated exactly as `STB_GLOBAL` for static linking, with the additional promise that the dynamic loader will keep one instance process-wide. A linker at this milestone MUST accept it and MAY treat it as `STB_GLOBAL`. |

**Type** (`ELF64_ST_TYPE(st_info)`) answers *what kind of thing the address
denotes*.

| value | name | meaning |
| --- | --- | --- |
| `0` | `STT_NOTYPE` | unspecified; `_start` in hand-written assembly is usually this |
| `1` | `STT_OBJECT` | a data object |
| `2` | `STT_FUNC` | a function or other executable code |
| `3` | `STT_SECTION` | names a section; always `STB_LOCAL`, `st_name` usually `0`. Exists so relocations can refer to a section plus an addend. |
| `4` | `STT_FILE` | names the source file; `STB_LOCAL`, `st_shndx == SHN_ABS`, precedes the file's other locals |
| `5` | `STT_COMMON` | an unallocated common block; equivalent to `STT_OBJECT` with `st_shndx == SHN_COMMON` |
| `6` | `STT_TLS` | a thread-local object; `st_value` is an offset within the TLS block, never a plain address |
| `10` | `STT_GNU_IFUNC` | the symbol names a *resolver* function; the real address is what calling it returns. Introduced by a later milestone; a static link must at minimum not corrupt it. |

Type does not affect which definition wins. It does affect what the linker
must build for a reference: `STT_FUNC` may need a PLT entry, `STT_TLS` uses
the TLS relocations, and `STT_GNU_IFUNC` needs an `R_X86_64_IRELATIVE`.

**Visibility** (`ELF64_ST_VISIBILITY(st_other)`) answers *how far the name
escapes once the link is done*. It only ever restricts; it never widens.

| value | name | meaning |
| --- | --- | --- |
| `0` | `STV_DEFAULT` | as the binding says: global and weak symbols are exported and preemptible |
| `1` | `STV_INTERNAL` | processor-specific; on x86-64 it is treated as `STV_HIDDEN` with the extra promise that the symbol is not referenced outside its component |
| `2` | `STV_HIDDEN` | the name is not exported: it MUST NOT appear in `.dynsym` and nothing linked afterwards can reach it. References to it from within the link resolve normally, and the symbol keeps both its binding and its visibility byte in `.symtab`. |
| `3` | `STV_PROTECTED` | exported, but references from within the component MUST resolve to the definition in this component — it cannot be preempted by a definition elsewhere |

### 1.1 How the three combine

The three coordinates interact by exactly two rules.

First, **visibility is monotone across a resolution**: when several symbol
table entries of the same name are combined, the resulting visibility is the
most restrictive of the ones seen, ordered `STV_DEFAULT` < `STV_PROTECTED` <
`STV_HIDDEN` = `STV_INTERNAL`. Two files disagreeing about visibility is not
an error; the restriction wins. This holds even when the restrictive entry is
an *undefined* reference, which is how `__attribute__((visibility("hidden")))`
on a declaration hides the eventual definition.

Second, **a non-default visibility restricts export without touching
binding**. A `STB_GLOBAL`, `STV_HIDDEN` symbol is fully global for the
purposes of resolution inside this link — it can satisfy an undefined
reference from another object, and a duplicate definition of it is still a
duplicate — and it is excluded from `.dynsym` so that nothing linked
afterwards can reach it. In `.symtab` it MUST be written back exactly as
resolved: binding `STB_GLOBAL`, `st_other` visibility `STV_HIDDEN`. The same
holds for `STV_INTERNAL` and `STV_PROTECTED`, and for weak bindings. The gABI
permits a linker to demote a hidden symbol to `STB_LOCAL` when it produces a
*relocatable* output, so that a later link cannot re-export it; this profile
does not do that, and for a final executable the four visibility values MUST
all survive the link unchanged and be visible as such to a reader of
`.symtab`.

An undefined symbol with non-default visibility MUST be satisfied inside this
link: it cannot be left for the dynamic loader. At the static milestone every
undefined symbol must be satisfied anyway.

## 2. What a symbol is, per file

A linker reads only `SHT_SYMTAB` (plus its `SHT_SYMTAB_SHNDX`) from an
`ET_REL` input. Each entry falls into exactly one of five states, determined
by `st_shndx` and binding:

| state | test | |
| --- | --- | --- |
| **local definition** | `STB_LOCAL` and `st_shndx != SHN_UNDEF` | private to the file |
| **strong definition** | `STB_GLOBAL` and `st_shndx` is a real section, `SHN_ABS` | |
| **weak definition** | `STB_WEAK` and `st_shndx` is a real section, `SHN_ABS` | |
| **common** | `st_shndx == SHN_COMMON` | a tentative definition; §4 |
| **undefined** | `st_shndx == SHN_UNDEF` | a reference; strong or weak per binding |

A symbol whose defining section was discarded — by COMDAT group
deduplication, by `--gc-sections`, or because the section was
`SHF_EXCLUDE` — is no longer a definition. If it was the only definition of a
referenced name, the reference becomes undefined and MUST be diagnosed.

## 3. Resolution

Resolution runs over the set of global (non-`STB_LOCAL`) symbols contributed
by every object that is actually included in the link. For each name, the
linker selects exactly one definition by the following rules, which MUST be
applied in this order.

1. **A strong definition wins.** If any included object provides a strong
   definition (`STB_GLOBAL`, defined, not common), that definition is chosen
   and every reference to the name resolves to it — including from objects
   that themselves provide a weak definition or a common.
2. **Otherwise the largest common wins.** If no strong definition exists but
   one or more commons do, the common with the greatest `st_size` is chosen
   and allocated per §4. A tie is broken in favour of the first one
   encountered in command-line order.
3. **Otherwise a weak definition wins.** The first weak definition
   encountered in command-line order is chosen.
4. **Otherwise the symbol is undefined.** If every reference to it is weak,
   this is not an error and §5 applies. If any reference is strong, the
   linker MUST diagnose an undefined symbol and name it.

"Command-line order" means the order in which files are presented after
archive extraction: the objects named on the command line in order, with an
archive's extracted members taking the position of the archive.

**Two strong definitions of the same name are a duplicate-definition error.**
A linker SHOULD diagnose it and name the symbol. Across 0.2.0 through 0.4.0 the
reference implementation silently keeps the first strong definition and does
not diagnose; from 0.5.0 it fails the link instead.

**What is graded therefore changes at that boundary.** Through step 3 only the
*selection* is graded -- the first strong definition MUST be the one every
reference resolves to -- and a linker that selects correctly matches the
reference whether or not it also diagnoses the collision. **From step 4 the
rejection itself is graded**: the link MUST fail, and a linker that silently
keeps the first definition loses the group no matter how it resolved the
symbol. The selection rule still holds wherever a link succeeds. Consult this
milestone's `TEST_INTERFACE.md` section for what its corpus asserts about the
diagnostic. Weak against weak, weak against strong, and
common against anything are never errors.

`STB_GNU_UNIQUE` participates as `STB_GLOBAL`.

### 3.1 What resolution produces

For each selected definition the linker computes a final value:

* a definition in a section: `sh_addr` of the output section the defining
  input section landed in, plus the input section's offset within it, plus
  `st_value`;
* `SHN_ABS`: `st_value` unchanged, and the output symbol keeps
  `st_shndx == SHN_ABS`;
* a common: the address assigned in §4;
* `STT_TLS`: the offset of the symbol within the TLS block, *not* an address.

Local symbols are not resolved — each is copied with its own file's value —
but they are still relocated, and a local symbol whose section was discarded
MUST be dropped rather than emitted with a stale value.

### 3.2 The output symbol table

`.symtab` in the output MUST contain, at minimum, every selected global
definition, with `st_value` set to its final address, `st_shndx` set to the
output section index that contains it (or `SHN_ABS`), and its binding adjusted
for visibility per §1.1. All local symbols MUST precede all non-local ones and
`sh_info` MUST equal the index of the first non-local symbol
(`01-elf-container.md` §3.3). Index 0 MUST be the all-zero entry.

`.symtab` MUST have `sh_type == SHT_SYMTAB`, `sh_entsize == 0x18`, and
`sh_link` pointing at a `SHT_STRTAB` section named `.strtab` that holds its
names. The entry symbol MUST appear in `.symtab` with a value equal to
`e_entry`.

The `SHN_ABS` half of the `st_shndx` rule is scoped: measured, a global whose
definition is absolute (`SHN_ABS` in its input) does not reach the output
`.symtab` at all at `0.2.0`, and appears with its value and `SHN_ABS` from
`0.3.0` onward. Absolute-symbol passthrough is therefore graded from step 2;
at step 1 only section-defined globals are.

Whether local symbols from the inputs are also copied is a quality choice and
is not graded, except through the stripping options, which are:

* `-s` / `--strip-all` MUST omit every `.debug_*` section, and the resulting
  file MUST be smaller than the unstripped one. It MUST *also* omit `.symtab`
  and `.strtab` from **`0.3.0` (step 2)** onward. Measured: `0.2.0` keeps both
  -- `--strip-all` there removes only the debug sections -- so at step 1 the
  presence of `.symtab`/`.strtab` after `--strip-all` is not graded either way,
  and no step-1 case asserts their absence. (The short alias `-s` is itself
  only accepted from `0.4.0`; see the milestone's `TEST_INTERFACE.md`.)
* `-S` / `--strip-debug` MUST omit every `.debug_*` section and MUST **keep**
  `.symtab` and `.strtab` intact, including the entry symbol.
* `-x` / `--discard-all` and `-X` / `--discard-locals` drop local symbols.

Stripping only removes non-allocated content. It MUST NOT change the size or
address of any allocated section, MUST NOT change the number of `PT_LOAD`
segments, and MUST NOT change `e_entry`: a stripped binary and its unstripped
twin run identically.

### 3.3 Eligibility for `.dynsym`

Only relevant once a dynamic output is produced, but the rule belongs with
resolution and does not change later. A symbol goes in `.dynsym` if and only
if **all** of the following hold:

* its binding after resolution is `STB_GLOBAL` or `STB_WEAK` — never
  `STB_LOCAL`;
* its visibility is `STV_DEFAULT` or `STV_PROTECTED` — `STV_HIDDEN` and
  `STV_INTERNAL` are excluded by construction, having been made local;
* it is either a definition that the output means to export, or an undefined
  reference that the output needs the loader to bind;
* it is not a `STT_SECTION` or `STT_FILE` symbol.

In an executable, a defined symbol needs to be exported only if something
requires it — `--export-dynamic`, a `--dynamic-list` entry, or a reference
from a shared object being linked against. In a shared object, every symbol
passing the test above is exported. `.dynsym` carries the same
`Elf64_Sym` layout and the same locals-first ordering requirement as
`.symtab`, with its `sh_link` naming `.dynstr`.

## 4. Common symbols

A common symbol is a *tentative* definition: `st_shndx == SHN_COMMON`,
`st_size` = the number of bytes needed, and — this is the trap —
**`st_value` holds the required alignment, not an offset**. It arises from C
tentative definitions (`int x;` at file scope) when compiled without
`-fno-common`, and from Fortran common blocks.

Rules a linker MUST follow:

1. A common never overrides a definition. If any object supplies a real
   definition of the name, that definition is used and every common of that
   name is dropped, with no diagnostic and no space allocated, even if the
   common is larger than the definition.
2. If only commons exist, the one with the **largest `st_size`** is selected.
   Its `st_value` — the alignment — MUST be honoured; when several commons of
   the same name have different alignments the linker MUST use the largest of
   them, even if it came from a smaller common. An alignment of `0` means `1`.
3. The selected common is allocated in `.bss`, i.e. in an `SHT_NOBITS`,
   `SHF_ALLOC|SHF_WRITE` output section, at an offset satisfying that
   alignment. In the output symbol table the symbol becomes an ordinary
   definition: `st_shndx` = the index of `.bss`, `st_value` = its virtual
   address, `st_size` unchanged. `SHN_COMMON` MUST NOT appear in the output.
4. `--defsym`-style absolute symbols and `SHN_ABS` definitions count as
   definitions for rule 1.

`-d`/`-dc`/`--define-common` forces commons to be allocated even when
producing a relocatable output; it has no effect on a final link and MAY be
accepted and ignored. `--warn-common` is not graded.

## 5. Weak symbols

Two distinct cases, often confused:

**A weak definition** (`STB_WEAK`, defined) is a definition that loses to any
strong definition and wins over nothing. If it is the only definition it is
used exactly as a strong one would be. Several weak definitions of the same
name are legal; the first in command-line order is used.

**A weak undefined reference** (`STB_WEAK`, `st_shndx == SHN_UNDEF`) is a
reference that is *allowed to go unsatisfied*. If no definition is found the
linker MUST NOT diagnose an error. Instead the symbol's value is taken to be
**zero**, and every relocation referring to it is computed with `S = 0`. For
`R_X86_64_64` that writes the addend; for `R_X86_64_PC32` it writes
`A - P`, which is what lets the idiomatic `if (&weak_fn) weak_fn();` test
work. A weak undefined symbol MUST NOT pull a member out of an archive
(§7.3), and MUST NOT by itself cause a PLT or GOT entry to be created in a
static link — though a GOT slot holding zero is a legal way to satisfy a
`GOTPCREL` reference to one.

A weak undefined reference that is later satisfied — because some other
object defines the name, or because an archive member was extracted for an
unrelated strong reference — resolves to that definition like any other
reference. The zero value applies only when nothing defines the name at the
end of the link.

`--undefined=SYMBOL` / `-u SYMBOL` injects a *strong* undefined reference
before the inputs are scanned, which forces archive extraction: a member that
defines `SYMBOL` is pulled in exactly as if an object had referenced it. The
option is first accepted at step 4; earlier milestones reject it as an
unrecognised argument.

Unlike a strong reference written in an object, **a name introduced only by
`-u` that nothing defines is not an error**. The reference implementation
links successfully and exits 0, and the name simply does not appear in the
output. `-u` therefore requests extraction; it does not assert that the
symbol exists (§8).

## 6. The `ar` archive format

An archive is a container, not an ELF file. A linker reads it to find the
object members that satisfy references.

The file begins with the 8-byte magic `!<arch>\n` — that is the seven
characters `!<arch>` followed by a newline, no NUL. Each member then follows,
introduced by a fixed 60-byte header of **space-padded ASCII**, with no
alignment padding before the header itself:

| offset | size | field | contents |
| --- | --- | --- | --- |
| 0 | 16 | identifier | the member name, or `/` or `//` or `/<n>`; see §6.1 |
| 16 | 12 | modification time | decimal seconds since the epoch; ignored by the linker |
| 28 | 6 | owner UID | decimal; ignored |
| 34 | 6 | group GID | decimal; ignored |
| 40 | 8 | file mode | **octal**; ignored |
| 48 | 10 | size | **decimal**, the member's data length in bytes, *not* including this header or the trailing pad |
| 58 | 2 | terminator | the two characters `` `\n `` (backquote, newline) |

Every numeric field is left-justified and padded with spaces; parse it by
trimming trailing spaces and reading the leading digits. The member's data
begins immediately after the 60th byte and runs for `size` bytes. **If `size`
is odd, a single `\n` padding byte follows** so that the next header starts at
an even offset; that pad byte is not counted in `size`. A linker MUST apply
this rule when walking members or it will desynchronise on the first
odd-sized member.

### 6.1 The special members

Two member names are reserved and, when present, MUST appear before any
ordinary member.

**`/`** (a single slash, space-padded) is the **symbol index**, created by
`ar s` / `ranlib`. Its data is: a 4-byte **big-endian** count `n`, then `n`
4-byte **big-endian** file offsets — each the absolute offset in the archive
of the member header defining that symbol — then `n` NUL-terminated symbol
names in the same order. Big-endian here is not a typo; this table is
byte-order-fixed regardless of the target. A variant named `/SYM64/` uses
8-byte big-endian offsets for archives over 4 GiB and MUST be accepted if
encountered.

**`//`** (two slashes) is the **long-name table**: a blob of member names,
each terminated by `/\n` (GNU) and indexed by byte offset. Any member whose
identifier field is `/` followed by decimal digits takes its real name from
that offset in this table. Names that fit in 15 characters are instead stored
inline in the identifier field terminated by a `/`, which is therefore *not*
part of the name — `foo.o/` names `foo.o`. A trailing `/` MUST be stripped
from an inline name and MUST NOT be stripped from a long name (the table's own
terminator is the `/`).

A linker MAY ignore the symbol index entirely and read every member's symbol
table instead; the result is identical and only the speed differs. It MUST NOT
*trust* a stale index over the members' actual contents when the two disagree.

### 6.2 Thin archives

A thin archive begins with the magic `!<thin>\n` instead. Its members carry
headers with a `size` of `0` and no data; the member name — always taken from
the `//` long-name table, which is mandatory in a thin archive — is a path to
the real object file, resolved relative to the directory containing the
archive. A linker MUST open those files when it decides to include a member.
Thin archives are produced by `ar T` and are common in build systems that
avoid copying objects.

## 7. Archive extraction

Archives are not included wholesale. They are searched, and only the members
that are needed are pulled in.

### 7.1 The undefined set

The linker maintains the set of symbol names that are referenced but not yet
defined by anything already included. It starts from `-u`/`--undefined`
symbols and the entry symbol, and grows as each newly included object
contributes its own undefined references.

### 7.2 The scan

An object file named on the command line is always included,
unconditionally, even if nothing references it, and it contributes its
undefined references to the set. An archive is different: its members are
*lazy*, and the linker includes one only when it is needed.

1. Consider every member of every archive on the command line. Include a
   member if and only if it defines a symbol that is currently in the
   undefined set, or supplies a real definition for a name currently satisfied
   only by a common (in which case the common is superseded per §4 rule 1).
2. Each member so included contributes its own definitions — removing names
   from the undefined set — and its own undefined references, adding names to
   it.
3. **Repeat until a whole pass includes nothing new.** This is a fixpoint over
   the undefined set, and the profile takes it over *all* archives on the
   command line at once, not over one archive at a time.

The fixpoint is what this profile requires, and it has three consequences a
linker MUST honour:

* **Member order inside an archive does not matter.** A member that appears
  before the member that references it is still found.
* **Archive order on the command line does not matter.** A chain of
  references spanning three archives resolves however they are ordered, and an
  archive listed *before* the object that needs it is still searched. This is
  a deliberate divergence from GNU `ld`, which scans each archive once as it
  reaches it and fails on exactly those orderings; it was measured against the
  reference implementation and is what the graded cases assume.
* **`--start-group`/`--end-group` therefore changes nothing** except that it
  MUST be accepted (§7.4).

Termination is guaranteed: each pass either includes at least one new member,
and there are finitely many, or it stops.

A member that is not included MUST have no effect on the output whatsoever:
its sections MUST NOT be laid out, its symbols MUST NOT appear in `.symtab`,
and its undefined references MUST NOT be reported. This is the observable
property that distinguishes lazy extraction from concatenation, and it is
graded: after linking an archive with two members where only one is
referenced, a symbol defined solely by the unreferenced member MUST be absent
from the output.

`--whole-archive` suspends this rule: every member of every archive named
after it is included unconditionally, as though the members had been listed
on the command line. `--no-whole-archive` restores the default. Both MUST be
accepted, and they are positional — they affect only the inputs that follow.

`--start-lib` / `--end-lib` bracket a run of *object files* and give them
archive semantics: those objects are included only if needed.

### 7.3 What does and does not pull a member

| reference | pulls a member? |
| --- | --- |
| strong undefined (`STB_GLOBAL`, `SHN_UNDEF`) | **yes** |
| `-u NAME` on the command line | **yes** |
| the entry symbol, when undefined | **yes** |
| weak undefined (`STB_WEAK`, `SHN_UNDEF`) | **no** |
| a common symbol needing its real definition | **yes** |
| a name already defined | no |

The weak rule is the one that surprises: a weak reference to a name that an
archive member defines does **not** cause extraction, and the symbol resolves
to zero unless something else pulls that member in for another reason. A
member's own *weak definitions* do not make it eligible either — only a
definition that is strong, or common, satisfies the undefined set. Archive
extraction is therefore order-sensitive by design, which is why libraries go
after the objects that use them.

### 7.4 `--start-group` / `--end-group`

`--start-group ARCHIVES... --end-group` (also spelled `-(` and `-)`) asks a
GNU-`ld`-compatible linker to repeat its scan over the bracketed inputs until
a complete pass extracts no new member, so that two archives can reference
each other without the caller guessing an order or listing one of them twice.
Both spellings MUST be accepted and MUST link successfully. Because §7.2
already takes the fixpoint over every archive, they impose no additional
requirement here — but a mutually recursive pair of archives inside a group
MUST link, and that is graded.

The brackets do not nest. Objects, not just archives, may appear inside a
group; they are still included unconditionally and contribute their undefined
references like any other object.

### 7.5 `-l` and `-L`

`-lNAME`, spelled either joined or as `-l NAME`, names a library to be found
by searching the directories given by `-L` **in the order the `-L` options
appeared**, for a file called `libNAME.a` — and, from the milestone that
introduces shared libraries, `libNAME.so`, which is preferred over the
archive in the same directory unless `-static` or `-Bstatic` is in force.
The first directory that contains a match wins; later `-L` directories are not
consulted for that name. A `-l` that matches nothing anywhere is a diagnosed
error naming the library. `-l:FILENAME` searches for that exact file name
instead of decorating it. A found library takes the position of its `-l`
option among the inputs; since §7.2 is order-independent, that position only
matters for the order of sections in the output.

## 8. Undefined symbols at the end of the link

When every input has been processed, any name still in the undefined set that
is referenced strongly **by an input object** is an error. The linker MUST
exit non-zero and MUST write the symbol's name somewhere on stderr. Naming the
referencing object file as well is good practice and is never penalised.

A **partial link** -- `-r` / `--relocatable`, introduced by the
relocatable-output milestone -- is not a final link, and this rule does not
apply to it: an undefined strong reference is not diagnosed there. The name
stays in the partial object's undefined set, with the relocations that
reference it, and the link that produces the final image is the one that must
report it (`17-relr-and-relocatable.md` §6.4, §6.5).

**Step scope -- when this is graded.** That rule is the obligation from
**`0.4.0` (step 3)** onward, which is where the reference first implements it
cleanly: `Undefined symbol NAME, referenced by ...`, non-zero, no artifact.
Before that it does not hold and no case asserts it. At `0.2.0` the link exits
1 but still leaves a complete-looking executable at the `-o` path, and the
symbol is named only at the bottom of a nested cause chain rather than in the
first line -- measured identical for `PLT32`, `PC32`, `32`, `64` and
`REX_GOTPCRELX` references, so a case cannot distinguish them; at `0.3.0` the
link exits **0** and prints nothing. See `00-compatibility-profile.md` §8.

A name whose only strong reference came from `-u`/`--undefined=` on the
command line is the documented exception (§5): it is not referenced by any
object, so an unresolved result is not an error and the link exits 0.

`--unresolved-symbols=<method>` and `--allow-undefined` variants adjust this
at later milestones; at the static milestone an unresolved strong reference is
always fatal. `--warn-unresolved-symbols` downgrades the error to a warning
and MAY be accepted.

The entry symbol is subject to the same rule with one wrinkle: if `_start`
(or the `-e` symbol) is not defined anywhere, the reference implementation
treats it as a fatal missing-symbol error. A linker MUST NOT silently emit an
executable with `e_entry == 0`.
