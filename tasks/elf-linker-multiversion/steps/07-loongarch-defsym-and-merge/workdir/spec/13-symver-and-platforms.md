# `.symver`, non-Linux ELF platforms, and file-supplied arguments

> Symbol versioning that arrives from input objects instead of the command line, the ELF details that differ on Solaris/Illumos targets, and the exact syntax of `--retain-symbols-file` and `@FILE`.

## Provenance

The `.symver` directive and its three spellings restate the GNU `as` manual
(binutils 2.40, *Symbol Names* / `.symver`,
<https://sourceware.org/binutils/docs/as/Symver.html>) and the symbol-versioning
model in the Linux Standard Base ELF specification and in
<https://maskray.me/blog/2020-11-26-all-about-symbol-versioning>. The
`.SUNW_*` section types and `ELFOSABI_SOLARIS` come from the System V gABI
processor-independent chapter as extended by Solaris
(<https://docs.oracle.com/cd/E37838_01/html/E36783/>). `--retain-symbols-file`
and `@FILE` restate the GNU ld manual's option descriptions.

Everything below that is written as a MUST was measured on this machine against
`wild-0.10.0` with `clang-16` and `llvm-readelf-16`, except where the text says
otherwise. Two areas could not be measured because no Illumos system is
available here: the `EI_OSABI` byte a native Illumos linker writes, and the
run-time meaning of the `.SUNW_*` sections. Those are marked as informational,
and the *graded* requirement in each case is the measured one.

## 1. `.symver` in the assembler

```
.symver REAL, ALIAS@VERSION
.symver REAL, ALIAS@@VERSION
.symver REAL, ALIAS@@@VERSION
```

`REAL` is a symbol the object already defines (or references). The directive
creates a second symbol-table entry named `ALIAS@VERSION` with the same value,
size, type and section as `REAL`. `@` is not a normal name character: the
linker MUST split every input symbol name at its **first** `@` and treat the
tail as a version.

| Spelling | `.symtab` of the object afterwards | Version is default? |
| --- | --- | --- |
| `alias@VER` | `REAL` **and** `alias@VER` | no |
| `alias@@VER` | `REAL` **and** `alias@@VER` | yes |
| `alias@@@VER` | `alias@@VER` only — `REAL` is gone | yes |

Measured, from an object built with

```c
int impl_v1(void){return 1;}
int impl_v2(void){return 2;}
int plain(void){return 3;}
__asm__(".symver impl_v1,ver@VERS_1.0");
__asm__(".symver impl_v2,ver@@VERS_2.0");
__asm__(".symver plain,pl@@@VERS_2.0");
```

the object's `.symtab` contains `impl_v1`, `impl_v2`, `ver@VERS_1.0`,
`ver@@VERS_2.0` and `pl@@VERS_2.0` — and no `plain`.

### 1.1 Parsing a versioned name

For each input symbol name the linker MUST compute:

| Result | Rule |
| --- | --- |
| base name | bytes before the first `@` |
| version name | bytes after the first `@`, with one further leading `@` stripped if present |
| `is_default` | true when the name contained `@@`, false when it contained a single `@` |
| unversioned | no `@` at all; `is_default` is irrelevant |

A name ending in a bare `@` with nothing after it (`foo@`) has an *empty*
version. **No milestone grades this spelling**, and either of the two answers
the reference has given is acceptable: measured across the releases, an object
carrying `foo@` is rejected with an undefined-version error at the earlier
tags, and from a later one is instead accepted and given version index 1
(`VER_NDX_GLOBAL`) with the hidden bit set, keeping that index even where a
version script would otherwise claim the name. There is no primary source for
either reading, so no case depends on one.

### 1.2 Default and non-default versions of one name

`ver@VERS_1.0` and `ver@@VERS_2.0` are two distinct dynamic symbols that share
a base name. They MUST both reach `.dynsym`, with:

* the **default** one (`@@`) carrying the version index of its node with the
  hidden bit **clear**;
* every **non-default** one (`@`) carrying its node's index with bit `0x8000`
  **set**.

`.dynstr` stores the base name and the version name as separate strings; the
`@`/`@@` rendering a reader shows comes from the version index and its hidden
bit, never from the string. How many copies of the base name end up in
`.dynstr` is a string-table packing choice and is **not** graded — the
reference writes one copy per versioned symbol rather than sharing them
(measured).

At most one default version may exist per base name. Two `@@` definitions of
the same base name MUST fail the link.

An undefined reference written without a version (`ver`) binds to the default
definition. A reference written `ver@VERS_1.0` binds to that specific one.

Measured output for the object above, linked `-shared -soname libsv.so` with a
script defining `VERS_1.0` and `VERS_2.0`:

```
.dynsym:   pl@@VERS_2.0   ver@VERS_1.0   ver@@VERS_2.0
.gnu.version:   1 (*global*)   3 (VERS_2.0)   2h(VERS_1.0)   3 (VERS_2.0)
.gnu.version_d: 1 BASE libsv.so | 2 VERS_1.0 | 3 VERS_2.0 (parent VERS_1.0)
```

The `h` on index 2 is the hidden bit. `impl_v1` and `impl_v2` do not appear:
the script's `local: *` claimed them.

An executable linking against that library and calling `ver()` and `pl()` gets
a `.gnu.version_r` entry for `libsv.so` listing `VERS_2.0` only — the default
definitions are what the unversioned references bound to. (Measured.)

### 1.3 Combining `.symver` with a version script

When a version script is in effect — `--version-script`, or a `VERSION { }`
block in a `-T` script — every version a `.symver` directive names MUST be
defined by it. If one is not, the link MUST fail, and stderr MUST name both the
symbol and the missing version. Measured: a script that defines `VERS_1.0`
while the object asks for `VERS_9.9` fails with
`Symbol ver has undefined version VERS_9.9`.

A link given **no** version script at all has no set of nodes to check a
`.symver` **definition** against. The reference's answer to that shape changed
across the releases — measured on a `-shared` link of one object carrying
`.symver v1_impl, ver@VERS_1.0`, the alias is accepted and exported at `0.5.0`,
`0.7.0` and `0.8.0` and rejected with the same undefined-version error at
`0.6.0`, `0.9.0` and `0.10.0` — so a `.symver` **definition** with no version
script is **not graded** at any milestone.

A versioned **reference** is a different shape, and it *is* graded here.
`.symver ver_ref, ver@VERS_1.0` applied to an *undefined* symbol needs no node
of its own: the version it names belongs to the shared object that defines it,
and it is recorded in `.gnu.version_r` against that library rather than in a
`.gnu.version_d` node of this output. Such a link MUST succeed with no version
script anywhere on the command line, and the reference MUST bind to the version
it names rather than to the default (§1.2). Measured at `0.6.0` through
`0.10.0`; it is graded from the milestone that introduces this document.

When both are present:

1. The object-supplied version wins for the symbol it names. The version script
   is **not** consulted for the version index of `ver@VERS_1.0`; the directive
   already chose `VERS_1.0`.
2. The version script is still consulted for the base names the directive
   leaves behind (`impl_v1`, `impl_v2`), and for every other symbol.
3. Listing the aliased base name (`ver`) under `global:` in a node is harmless
   and does not move the alias to that node.
4. The set of nodes, their indices and their parent links come entirely from
   the script. `.symver` never creates a version definition.

The result MUST be identical to what an equivalent pure-version-script link
produces for the same set of exported names and indices; the only thing
`.symver` adds is the ability to have two versions of one base name and to mark
one of them non-default.

## 2. Non-Linux ELF platforms

The graded corpus targets Linux and Illumos-flavoured x86-64. The differences a
linker has to account for are small and specific.

### 2.1 `EI_OSABI`

| Constant | Value |
| --- | --- |
| `ELFOSABI_NONE` / `ELFOSABI_SYSV` | 0 |
| `ELFOSABI_HPUX` | 1 |
| `ELFOSABI_NETBSD` | 2 |
| `ELFOSABI_GNU` / `ELFOSABI_LINUX` | 3 |
| `ELFOSABI_SOLARIS` | 6 |
| `ELFOSABI_FREEBSD` | 9 |
| `ELFOSABI_STANDALONE` | 255 |

**Requirement:** `e_ident[EI_OSABI]` in the output MUST be `0`
(`ELFOSABI_NONE`) and `e_ident[EI_ABIVERSION]` MUST be `0`, for every target
and every emulation, including the Solaris emulation. Measured: the reference
implementation writes `ELFOSABI_NONE` unconditionally, and the output of
`-m elf_x86_64_sol2` reports `OS/ABI: UNIX - System V`.

Informationally, a native Illumos `ld` writes `ELFOSABI_SOLARIS` (6). That was
**not** measurable here — no Illumos system is available — and MUST NOT be
implemented, because it would diverge from what is graded.

The linker MUST NOT reject an input object because of its `EI_OSABI` value;
objects marked `ELFOSABI_GNU` (which is what GNU tools emit whenever an object
uses `STB_GNU_UNIQUE` or `STT_GNU_IFUNC`) and objects marked `ELFOSABI_NONE`
MUST both be accepted and MUST be combinable in one link.

### 2.2 `.SUNW_*` section types

| Name | `sh_type` |
| --- | --- |
| `SHT_LOSUNW` | 0x6ffffffa |
| `SHT_SUNW_move` | 0x6ffffffa |
| `SHT_SUNW_COMDAT` | 0x6ffffffb |
| `SHT_SUNW_syminfo` | 0x6ffffffc |
| `SHT_GNU_verdef` | 0x6ffffffd |
| `SHT_GNU_verneed` | 0x6ffffffe |
| `SHT_GNU_versym` | 0x6fffffff |
| `SHT_HISUNW` | 0x6fffffff |

Note the overlap: the three `SHT_GNU_ver*` types occupy numbers inside the
Sun-reserved range, and `SHT_SUNW_move` shares its number with `SHT_LOSUNW`.
A linker that names section types in diagnostics MUST resolve the three
`GNU_ver*` numbers to their GNU meanings, because that is what every object in
the corpus means by them.

`SHT_SUNW_COMDAT` is Solaris's predecessor to `SHF_GROUP`/`SHT_GROUP`
COMDAT: a section of this type is a candidate for de-duplication by name. The
reference implementation recognises the two type numbers for naming purposes
and gives them no special layout or de-duplication behaviour. **Requirement:**
the implementation MUST be able to name these section types in a diagnostic and
MUST NOT crash on an input containing them; it is NOT required to implement
Solaris COMDAT semantics, and the graded corpus does not contain such sections.

`.SUNW_syminfo` (type `SHT_SUNW_syminfo`, entries of 4 bytes: `si_boundto`,
`si_flags`, both `Elf64_Half`) records per-symbol binding hints. It is neither
consumed nor produced.

### 2.3 Emulations and the default dynamic linker

`-m EMULATION` selects the target. The accepted names, and what each implies:

| `-m` name | Architecture | Other effect |
| --- | --- | --- |
| `elf_x86_64` | x86-64 | none |
| `elf_x86_64_sol2` | x86-64 | if no `--dynamic-linker` has been seen yet, set it to `/lib/amd64/ld.so.1` |
| `aarch64elf` | AArch64 | none |
| `aarch64linux` | AArch64 | none |
| `elf64lriscv` | RISC-V 64 | none |
| `elf64loongarch` | LoongArch 64 | none |
| `elf64lppc` | PowerPC64 LE | none |

Any other `-m` value MUST fail the link with the rejected name on stderr.

Rules about the interpreter path:

* There is **no** built-in default. Without `--dynamic-linker` (and without
  `-m elf_x86_64_sol2`), the output has no `PT_INTERP` and no `.interp`, even
  when it is dynamically linked.
* The path is used only when the output is dynamic (`05-dynamic-linking.md`
  §1): a static `ET_EXEC` output gets no `PT_INTERP` and no `.interp`, whatever
  interpreter path `--dynamic-linker` or this emulation set along the way.
* An explicit `--dynamic-linker PATH` wins over the Solaris default regardless
  of whether it appears before or after the `-m` option. (Measured on all four
  orderings.)
* `--no-dynamic-linker` clears any previously set path.

An Illumos clang driver inserts a bare `-C` flag before invoking a non-GNU
linker. On an Illumos host the linker MUST silently ignore `-C`. On other
hosts it need not accept it. (This is a compile-time distinction in the
reference implementation; it cannot be exercised on this Linux machine.)

### 2.4 `DT_*` tags with platform- or option-dependent defaults

| Tag | Value | When it is emitted |
| --- | --- | --- |
| `DT_RUNPATH` | 0x1d | default form of `-rpath`/`--rpath` |
| `DT_RPATH` | 0x0f | instead of `DT_RUNPATH` when `--disable-new-dtags` is in effect |
| `DT_FLAGS` | 0x1e | new-dtags form; carries `DF_BIND_NOW` (0x8), `DF_TEXTREL` (0x4), `DF_SYMBOLIC` (0x2), `DF_ORIGIN` (0x1) |
| `DT_BIND_NOW` | 0x18 | old-dtags form of `DF_BIND_NOW`, emitted under `--disable-new-dtags` |
| `DT_SYMBOLIC` | 0x10 | old-dtags form of `DF_SYMBOLIC` |
| `DT_TEXTREL` | 0x16 | old-dtags form of `DF_TEXTREL` |
| `DT_FLAGS_1` | 0x6ffffffb | always emitted when any `DF_1_*` bit applies; **not** affected by `--disable-new-dtags` |

Measured, `-shared -rpath /a/b -z now`:

* default: `DT_RUNPATH [/a/b]`, `DT_FLAGS BIND_NOW`, `DT_FLAGS_1 NOW`;
* with `--disable-new-dtags`: `DT_RPATH [/a/b]`, `DT_BIND_NOW`, `DT_FLAGS_1 NOW`,
  and no `DT_FLAGS`.

`--enable-new-dtags` restores the default. The two flags are last-wins.

**Which milestone grades this.** `--disable-new-dtags` MUST be accepted at
every milestone that carries this document, but the old-dtags output above is
only graded where a milestone's own `TEST_INTERFACE.md` section lists the
option in its graded surface. At the earlier milestones the reference accepts
it, warns that it is not yet supported, and still writes `DT_RUNPATH` and
`DT_FLAGS`. Accepting the option and leaving the new-dtags output in place is
therefore correct until your milestone says otherwise; what is never correct is
rejecting it.

**Do not require `DF_SYMBOLIC`.** Measured: `-Bsymbolic` on a shared object
produced `DT_FLAGS` containing only `BIND_NOW`; `DF_SYMBOLIC` was not set. The
binding behaviour is implemented without advertising the flag.

## 3. `--retain-symbols-file FILE`

The file is a plain list of symbol names, one per line.

* Lines are split on `\n`, and one trailing `\r` is removed, so a CRLF file
  behaves exactly like an LF one. Nothing else is removed. (Measured:
  `entry\r\n` retains `entry`; `entry\r\r\n` and `entry \n` retain nothing.)
* Empty lines are skipped. There are no comments and no blank-line escape.
* Leading and trailing whitespace is **not** trimmed: a line `"  bar"` retains
  a symbol literally named `  bar`, not `bar`.
* Names are matched **exactly**. Globs are not expanded: a line `foo_*` retains
  nothing unless a symbol is literally called `foo_*`.
* The file must exist and be valid UTF-8; otherwise the link fails naming the
  path.

Effect:

* `.symtab` is reduced to the listed names, **plus** the linker's own defined
  symbols (`__ehdr_start`, `__executable_start`, `etext`, `_etext`, `__etext`,
  `_DYNAMIC`, and the other linker-defined symbols the output needs). Measured:
  a retain file listing `zzz` produced a `.symtab` of exactly those
  linker-defined symbols plus `zzz`; the `FILE` symbol and every unlisted local
  and global were dropped.
* `.dynsym`, `.gnu.version`, `.gnu.version_d` and `.gnu.version_r` are
  **unaffected**. Measured: a library whose retain file lists two of four
  exports still exports all four dynamically.
* The option is a strip mode, so it is mutually exclusive in effect with
  `--strip-all` / `-s` (which removes `.symtab` entirely) and with
  `--strip-debug` / `-S`; the last such option on the command line wins.

## 4. `@FILE` argument files

An argument whose first character is `@` names a file whose contents are
expanded in place, at that position, into the argument list. `@FILE` MUST work
mixed with ordinary arguments in any order, and MUST be processed recursively:
an argument file may itself contain `@OTHERFILE`.

Tokenisation of the file contents:

* Arguments are separated by any whitespace, including newlines. Consecutive
  separators collapse.
* `'` and `"` both start a quoted argument. Inside a quote, whitespace and the
  *other* quote character are literal. The quote ends at a matching quote of
  the same kind.
* A quote may only **start** an argument. `-o"x"` MUST be rejected — the
  message names a missing opening quote.
* A closing quote must be followed by whitespace or end-of-input. `"-o"x` MUST
  be rejected.
* `\` outside a quote escapes the next character, so `sp\ dir/lib.o` is one
  argument containing a space. A `\` at end of input is an error.
* An unterminated quote MUST fail the link, with the quote character named.
* There is no comment syntax and no `#` handling.
* An empty file contributes no arguments.
* The file must exist and be valid UTF-8; otherwise the link fails naming the
  path.

Measured examples that MUST behave as described:

| File contents | Expands to |
| --- | --- |
| `-shared\n"-o" t.so\nlib.o\n--version-script  v.map\n` | `-shared` `-o` `t.so` `lib.o` `--version-script` `v.map` |
| `-shared -o t.so "sp dir/lib.o"` | the fourth argument is `sp dir/lib.o` |
| `-shared -o t.so sp\ dir/lib.o` | same |
| `-o 'it"s.so'` | output file named `it"s.so` |
| `-shared -o t.so "lib.o` | error: missing closing quote |

Note that `@FILE` has no leading hyphen and therefore cannot be written as
`--@FILE` or bundled with other short options.
