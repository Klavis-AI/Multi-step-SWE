# Compatibility profile

> The benchmark's own choices where the ELF specifications leave a linker free: default layout, page policy, string merging, garbage collection, entry point, option spelling and what a diagnosed failure looks like.

## Provenance

Nothing in this document is restated from a single upstream specification,
because nothing in it is specified anywhere. The System V gABI, the x86-64
psABI and `ld.texi` all leave output section order, segment packing, string
merging, garbage-collection roots and diagnostic wording to the
implementation, and `ld`, `ld.lld` and `mold` differ on every one of them.
`TEST_INTERFACE.md` §4 says as much: byte-identity with another linker is not
graded at any milestone.

This profile therefore records the one set of choices this benchmark grades.
Every rule below was either measured against the reference implementation
(`wild` at tag `0.2.0`, commit `727c26f2`, 2024-03-18, and at later tags where
noted) or is a direct consequence of `TEST_INTERFACE.md`. Where the reference
implementation and common practice disagree, the reference implementation
wins, and the disagreement is called out in the text.

Three measurements are worth knowing up front because they contradict what a
reader familiar with GNU `ld` would assume:

* **The reference implementation garbage-collects by default.** GNU `ld` does
  not. §6 below grades the inverse.
* **The reference implementation's diagnostic prefix and exit value both
  changed across releases, and they changed at different times and by kind of
  error**. Measured across every release: the prefix is `Error: ` up to
  `0.5.0` and `wild: error: ` from `0.6.0`. The exit value is `1` at `0.2.0`;
  for a *link* error it becomes `255` at `0.4.0`, while for an
  *argument-parsing* error it stays `1` until `0.6.0`. At `0.3.0` one link
  error -- an undefined symbol -- exits `0` with empty stderr, so even
  "non-zero on failure" is not true at every release.
  Neither the prefix nor the particular non-zero value is part of the contract,
  and §8 requires neither. What is required, and what is graded, is that a
  failed link exits non-zero and says on stderr what went wrong. Do not write
  code that reproduces a prefix or an exit value; nothing measures it.
* **At tag `0.2.0` long options are accepted in exactly one spelling; from
  `0.3.0` on either one or two dashes is accepted.** `TEST_INTERFACE.md` §2
  fixes the contract as *both*, and that is what §9 requires.

Three options this document requires a linker to accept are rejected by the
earliest tag and appear later: `--no-gc-sections` (accepted from `0.3.0`),
`--whole-archive` (from `0.4.0`) and `--no-eh-frame-hdr` (from `0.9.0`). They
are required here because these documents ship at every milestone and because
accepting an option can never fail a graded case, whereas rejecting one can.

## 1. Scope of the profile

This document fixes defaults. A milestone's own section of
`TEST_INTERFACE.md` may introduce an option that overrides one of them — a
`SECTIONS` linker script replaces §2 wholesale, `-shared` changes §3 and §7 —
and where it does, the milestone wins. Absent such an option, the behaviour
here is the graded behaviour.

Throughout, "the output" means the file named by `-o`. A *static x86-64
executable* means `ET_EXEC`, no `PT_INTERP`, no `PT_DYNAMIC`, no `.dynsym`,
`.dynstr`, `.dynamic` or `.interp` section, and no relocation section left in
the output. That is the shape of every step-1 link.

## 2. Default output section order

Input sections are grouped into output sections by the rules in
`04-layout-and-sections.md` §1. The output sections are then emitted in the
order below. The order is a total order over *kinds*; a kind that receives no
content and is not marked "always" in the table is omitted entirely.

| # | section | when present | segment |
| --- | --- | --- | --- |
| 1 | ELF header + program header table | always | first `PT_LOAD` |
| 2 | `.interp` | dynamic output only | first `PT_LOAD`, `PT_INTERP` |
| 3 | `.note.*` | when note input exists | first `PT_LOAD`, `PT_NOTE` |
| 4 | `.dynsym`, `.dynstr`, `.gnu.hash`, `.gnu.version*`, `.rela.dyn` | dynamic output only | first `PT_LOAD` |
| 5 | `.rodata` and read-only custom sections | when non-empty | first `PT_LOAD` |
| 6 | `.eh_frame_hdr` | when `--eh-frame-hdr` is given | first `PT_LOAD`, `PT_GNU_EH_FRAME` |
| 7 | `.gcc_except_table` | when non-empty | first `PT_LOAD` |
| 8 | `.plt`, `.text`, `.init`, `.fini`, executable custom sections | when non-empty | executable `PT_LOAD` |
| 9 | `.got`, `.got.plt`, `.rela.plt` | when non-empty | writable `PT_LOAD` |
| 10 | `.init_array`, `.fini_array`, `.preinit_array` | when non-empty | writable `PT_LOAD` |
| 11 | `.data`, writable custom sections | when non-empty | writable `PT_LOAD` |
| 12 | `.dynamic` | dynamic output only | writable `PT_LOAD`, `PT_DYNAMIC` |
| 13 | `.tdata`, `.tbss`, and any other `SHF_TLS` section | when non-empty | writable `PT_LOAD`, `PT_TLS` |
| 14 | `.bss` and NOBITS custom sections that are **not** `SHF_TLS` | always | writable `PT_LOAD` |
| 15 | `.comment` | when non-empty | none (not `SHF_ALLOC`) |
| 16 | `.shstrtab`, `.symtab`, `.strtab` | unless `--strip-all` | none |

Two entries carry milestone caveats, both measured against the reference rather
than inferred, and neither is graded:

* `.eh_frame_hdr` is emitted **only when `--eh-frame-hdr` is passed**. 0.2.0
  also synthesises it whenever any input has a non-empty `.eh_frame`, but from
  0.3.0 onward it does not; a case that wants the header must ask for it.
* `.eh_frame` is absent from the table because its placement is not stable and
  therefore not graded: the reference emits it `SHF_ALLOC|SHF_WRITE` inside a
  writable `PT_LOAD` at 0.2.0, and `SHF_ALLOC` only, inside the read-only
  `PT_LOAD`, from 0.3.0 onward. Grade `PT_GNU_EH_FRAME`'s coverage of
  `.eh_frame_hdr` instead of the segment `.eh_frame` lands in.

What is graded, and therefore what a linker MUST satisfy, is the *partition*
this order induces, not the order itself:

1. The output MUST contain, in file order, a `PF_R` segment, then a
   `PF_R|PF_X` segment, then a `PF_R|PF_W` segment, for a static x86-64
   executable with read-only, executable and writable content, absent
   `--no-rosegment` (see §1 and TEST_INTERFACE). **Do not grade
   the number of `PT_LOAD` segments, and do not select a segment by its
   permission bits alone.** The count is not fixed: a release may split
   post-relocation-constant data into a second writable segment for `RELRO`,
   and a section whose `sh_addralign` exceeds the page size forces the
   `PT_LOAD` that holds it to that alignment (measured from `0.7.0`: an input
   section aligned to `0x10000` is placed in the read-only `PT_LOAD`, whose
   `p_align` rises to `0x10000` with it -- no extra segment is created;
   `0.2.0`-`0.6.0` reject such an input instead rather than laying it out
   -- §5). Grade the ordering and the placement rules below instead.
2. Every `SHF_ALLOC` section MUST lie entirely inside a `PT_LOAD` whose
   `p_flags` match its `sh_flags` under the mapping of §4 -- "a", not "the":
   where a release emits two writable segments (rule 1) a writable section is
   inside one of them, and which one is not graded. `.rodata` MUST be
   in the `PF_R` segment and MUST NOT carry `SHF_EXECINSTR` (absent
   `--no-rosegment`); `.text` MUST be
   in the `PF_R|PF_X` segment; `.bss` MUST be in the `PF_R|PF_W` segment.
3. `.bss` and every other `SHT_NOBITS` section MUST be the last thing in the
   writable segment, so that the segment's `p_memsz` can exceed its
   `p_filesz` without a hole.
4. The ELF header and the program header table MUST be covered by the first
   `PT_LOAD`.

Relative order *within* a kind — the order of the input sections merged into
one output section — is the order the inputs were given on the command line,
and is not graded. Padding and `.shstrtab` layout are not graded. Neither is
the content of `.comment`, beyond whatever rule a milestone's own
`TEST_INTERFACE.md` section states for it; no milestone requires any
particular linker's name to appear there.

## 3. Sections the linker synthesises

These sections have no input-section counterpart. A linker MUST create them
when the condition applies and MUST NOT create them otherwise.

| section | `sh_type` | condition |
| --- | --- | --- |
| `.symtab` | `SHT_SYMTAB` | unless `--strip-all`; that omission is measured from `0.3.0` (step 2) onward only (`02-symbols-and-archives.md` §3.2) |
| `.strtab` | `SHT_STRTAB` | the same condition; holds `.symtab` names |
| `.shstrtab` | `SHT_STRTAB` | always; holds section names |
| `.got` | `SHT_PROGBITS` | a GOT-consuming relocation was seen |
| `.plt` | `SHT_PROGBITS` | a PLT-consuming relocation was seen |
| `.eh_frame` | `SHT_PROGBITS` | any input had a non-empty `.eh_frame` |
| `.eh_frame_hdr` | `SHT_PROGBITS` | `--eh-frame-hdr` is given; emitting it unasked is permitted, not required |
| `.comment` | `SHT_PROGBITS` | always; identifies the linker |
| `.bss` | `SHT_NOBITS` | always, even when empty |

`.symtab`, `.strtab`, `.shstrtab` and `.comment` MUST NOT have `SHF_ALLOC`
semantics forced on them by the profile: whether they are covered by a
`PT_LOAD` is not graded, but they MUST NOT be counted when checking rule 2.1's
three-segment partition, and their absence from any segment is always legal.

### 3.1 Symbols the linker synthesises

A linker MUST define each of the following when an input references it, and
MAY define it unconditionally. Each is `STB_GLOBAL` with `STV_HIDDEN`
visibility, so none of them is exported. Where a section named below does not
exist, the symbol is defined to the address the section would have occupied,
so that a start/end pair is always equal rather than undefined.

| symbol | value |
| --- | --- |
| `__ehdr_start` | the address of the ELF file header, i.e. the image base |
| `_GLOBAL_OFFSET_TABLE_` | the address of `.got` |
| `__init_array_start`, `__init_array_end` | the first byte of `.init_array` and the first byte past it |
| `__fini_array_start`, `__fini_array_end` | likewise for `.fini_array` |
| `__preinit_array_start`, `__preinit_array_end` | likewise for `.preinit_array` |
| `__rela_iplt_start`, `__rela_iplt_end` | the bounds of the `R_X86_64_IRELATIVE` entries in `.rela.plt`, which a static binary walks itself to run its own ifunc resolvers |
| `_end` | the first byte past `.bss`, i.e. past the whole image |
| `__bss_start` | the first byte of `.bss` |
| `_DYNAMIC` | the address of `.dynamic`; only in a dynamic output |
| `__start_<name>`, `__stop_<name>` | the bounds of the custom output section `<name>`, for every such section whose name is a valid C identifier (`04-layout-and-sections.md` §1) |

A synthesised symbol MUST NOT override a definition supplied by an input: if
an object defines `_end` itself, that definition wins and the linker
synthesises nothing.

## 4. Section-to-segment mapping

The mapping from `sh_flags` to `p_flags` is:

| `sh_flags` | `p_flags` | segment |
| --- | --- | --- |
| `SHF_ALLOC` | `PF_R` | read-only `PT_LOAD` |
| `SHF_ALLOC \| SHF_EXECINSTR` | `PF_R \| PF_X` | executable `PT_LOAD` |
| `SHF_ALLOC \| SHF_WRITE` | `PF_R \| PF_W` | writable `PT_LOAD` |
| `SHF_ALLOC \| SHF_WRITE \| SHF_EXECINSTR` | `PF_R \| PF_W \| PF_X` | writable `PT_LOAD` |
| `SHF_ALLOC \| SHF_TLS` | `PF_R` | `PT_TLS`, plus the writable `PT_LOAD` |
| no `SHF_ALLOC` | — | no segment |

An output section MUST be assigned to exactly one `PT_LOAD`. A section whose
`sh_flags` lack `SHF_ALLOC` MUST NOT be assigned to any segment and MUST NOT
be given a non-zero `sh_addr`.

`PT_TLS` is not a `PT_LOAD`: `.tdata` and `.tbss` are covered by *both* the
writable `PT_LOAD` and `PT_TLS`. `PT_GNU_EH_FRAME` likewise overlays
`.eh_frame_hdr` inside the read-only `PT_LOAD`.

## 5. Page size, alignment and address policy

| quantity | x86-64 value |
| --- | --- |
| default base address, non-PIE | `0x400000` |
| default base address, PIE / `-shared` | `0` |
| default page size (the usual `p_align` of a `PT_LOAD`) | `0x1000` (4096) |
| maximum input `sh_addralign` a linker must accept, unless a milestone raises it | `0x8000` (32768) |
| `PT_TLS`, `PT_DYNAMIC`, `PT_GNU_EH_FRAME`, `PT_NOTE` `p_align` | the maximum `sh_addralign` of their contents, at least 1 |

The table column is headed x86-64 because that is the first backend. The
values are the benchmark's address policy for every supported machine, but
they are not identical across machines, so each is stated per machine: the
default base address of a non-PIE executable is `0x400000` on x86-64, AArch64
and LoongArch (`14-loongarch-relocations.md` §1 records the LoongArch
measurement), and `0x10000` on RISC-V (`11-riscv-relocations.md` §1); the page
size a `PT_LOAD` is laid out with is `0x1000` on x86-64 and RISC-V and
`0x10000` on AArch64 and LoongArch (`14-loongarch-relocations.md` §1). Where a
rule below names `0x1000` as the page size, that is x86-64's value; each
machine uses its own.

Rules:

* Every `PT_LOAD` MUST have `p_align` equal to the page size, `0x1000` — or,
  when the segment carries a section whose `sh_addralign` is larger than the
  page size, equal to that largest `sh_addralign`. A `PT_LOAD` carrying no
  over-aligned section therefore has `p_align` exactly `0x1000`. A milestone's
  own contract may require a different value for a specific option --
  `--nmagic` requires sub-page `p_align` -- and where it does the milestone
  wins (§1). The benchmark
  does not grade the larger `p_align` values (`0x200000`) that GNU `ld` uses
  for huge-page-friendly layout, and a linker MUST NOT emit them for a segment
  whose contents do not require them.
* Every `PT_LOAD` MUST satisfy the congruence
  **`p_offset ≡ p_vaddr (mod p_align)`**. A linker MAY satisfy it by padding
  the file to a whole page between segments, and MAY instead satisfy it by
  advancing the virtual address by the same amount as the file offset so that
  two segments share a page. Both are graded as correct.
* `p_paddr` MUST equal `p_vaddr`, except under a linker script that puts an
  explicit load address in force for a `PT_LOAD`'s contents (the `AT` construct
  of the linker-script document). Where such a construct is in force the
  segment's `p_paddr` is derived from the **load-address offset** the script
  establishes, so it is generally neither equal to `p_vaddr` nor equal to the
  `AT` address itself — measured, `wild` places `.data : AT(0x508000)` in a
  `PT_LOAD` whose `VirtAddr` is `0x504000` and whose `PhysAddr` is
  `0x504008`, off by the segment's own relative alignment. `LOADADDR()` is the
  channel a case reads an LMA back through, never `p_paddr`; no case in this
  benchmark asserts an unequal pair, and none may be written to. With no such
  construct in force the two fields are equal.
* Every output section's `sh_addr` MUST be a multiple of its `sh_addralign`,
  and `sh_addralign` MUST be the maximum of the `sh_addralign` of the input
  sections merged into it and any minimum the section kind requires
  (`03-x86-64-relocations.md` §4 for `.got`, §4 of this document's companion
  `04-layout-and-sections.md` for the rest).
* An input `sh_addralign` of `0` MUST be treated as `1`. An input
  `sh_addralign` that is not a power of two is an error.
* An input `sh_addralign` greater than the maximum in the table above MAY be
  rejected as an error, and it is **never clamped**. Measured with a single
  `SHF_ALLOC` input section at `sh_addralign` `0x10000`: `0.2.0`-`0.6.0`
  (steps 1-5) reject the link outright -- `Unsupported alignment 0x10000`, no
  artifact -- and `0.7.0`-`0.10.0` (steps 6-9) accept it and honour it, placing
  the section at an address that is a multiple of `0x10000` and raising the
  `p_align` of the `PT_LOAD` that holds it to `0x10000` (§2 rule 1). The
  alignment is honoured by growing the segment that already holds the section,
  not by giving the section a segment of its own: measured, `0.7.0` renders
  `.big` at `0x410000` with file offset `0x10000` inside the read-only
  `PT_LOAD`, which stretches from offset `0` to `0x20000` at `p_align`
  `0x10000`. A case must therefore read the alignment back from the containing
  segment's `p_align` and the section's `sh_addr`, never from a count of
  `PT_LOAD` entries.
  Those are the only two conforming behaviours: a linker MUST NOT silently
  reduce the alignment to the ceiling and place the section anyway, because
  the contents would then be misaligned with nothing said about it. A
  milestone's own section of `TEST_INTERFACE.md` may raise that maximum, and
  where it does the milestone wins (§1).

## 6. Garbage collection and the root set

**Garbage collection is on by default.** This is the point on which this
benchmark differs most sharply from GNU `ld`, and it was measured, not
assumed: the reference implementation discards unreachable input sections at
every tag with no option given.

* `--gc-sections` MUST be accepted; it requests the default behaviour.
* `--no-gc-sections` MUST be accepted; it MUST cause every input section that
  survived group and duplicate-symbol resolution to be emitted, reachable or
  not.

Collection operates on whole input sections. The root set is:

1. The input section containing the entry symbol (§7).
2. Every input section whose `sh_flags` include `SHF_GNU_RETAIN`
   (`0x200000`). This is what `__attribute__((retain))` produces.
3. Every input section merged into `.init`, `.fini`, `.init_array`,
   `.fini_array`, `.preinit_array`, or into an output section named by a
   `.ctors.*` or `.dtors.*` input.
4. Every input section merged into `.comment`.
5. *Not a root.* `-u NAME` / `--undefined=NAME` are accepted from `0.5.0`
   and add the name as an undefined symbol, which is enough to **extract an
   archive member** that defines it. Measured at every release through
   `0.10.0`: the defining section is still collected unless something
   references it or `--no-gc-sections` is given, so naming a symbol with `-u`
   does not by itself keep its section. Nothing grades `-u` as a root.
6. Every input section that defines a symbol which is eligible for `.dynsym`
   (`02-symbols-and-archives.md` §3.3), in a dynamic output.
7. Every input section `S` for which a referenced `__start_S`/`__stop_S`
   encapsulation symbol exists.
8. Every `SHF_MERGE` section. The profile does not require merge sections to
   be traced; a linker MAY retain all of them unconditionally.

From the roots, reachability follows relocations: if a retained section has a
relocation against a symbol defined in section `T`, then `T` is retained.
`.eh_frame` is not itself a root and is not traced; instead each FDE is
attached to the section its `pc_begin` field designates, and an FDE MUST be
dropped exactly when that section is dropped
(`04-layout-and-sections.md` §7).

A collected section contributes nothing: no bytes, no symbols in `.symtab`,
no FDE, no relocation.

## 7. The entry point

The default entry symbol is **`_start`**. The linker MUST resolve it like any
other symbol reference — it is a root of the garbage collector, and an archive
member that defines it MUST be extracted — and MUST set `e_entry` to its final
virtual address.

If no definition of the entry symbol exists after resolution, the link MUST
fail with a diagnostic naming the missing symbol -- through `0.4.0` (step 3)
only: measured, from `0.5.0` the link succeeds and no milestone grades this
condition as a failure (§8's closing note). (Overriding which symbol is
looked up arrives in two stages, measured: `--entry=NAME` is accepted from
`0.5.0`, and the separated form `--entry NAME` together with `-e NAME` only
from `0.6.0` -- so the short form is *not* an alias of the long one at every
release. Whichever symbol is selected, the "must exist, must be a root" rules
above are unchanged.)

## 8. What a diagnosed error looks like

| outcome | exit status | stderr | `-o` path |
| --- | --- | --- | --- |
| success | `0` | may be empty | a complete, usable output |
| failure | non-zero | at least one line naming what went wrong | no file that could be mistaken for a successful output |

This restates `TEST_INTERFACE.md` §2 and adds three obligations:

* A diagnostic MUST name the thing that caused it **where the table below
  says so, at the milestones the table gives**. Where a milestone says a
  diagnostic must name a symbol, a relocation type or an input file, that exact
  token MUST appear somewhere on stderr; the prose around it is free. Do not
  read this as a blanket obligation to name the offending file in every input
  rejection -- and do not read the exception the other way round either. The
  distinction is *could not be opened* versus *opened and badly formed*. A file
  the linker could not open at all is named at **every** release from `0.2.0`
  to `0.10.0` (`Failed to open input file` followed by the path -- the same
  behaviour §9 describes when it says `--build-id sha1` is diagnosed as a
  missing input file named `sha1`), and the table's first row grades that. A
  file that opened but is not an acceptable ELF is the other condition:
  measured, the reference names its *path* only from `0.10.0`, and says just
  `Only 64 bit ELF is currently supported` before that, so no milestone grades
  a token there. What the corpus asserts is the column the table marks graded.
* Diagnostics go to **stderr**. Nothing a linker prints on success is graded,
  but stdout MUST NOT be used for errors.
* Exiting `0` after writing a truncated or unrelocated file is the specific
  failure mode the grader exists to catch. A link that cannot complete MUST
  report failure; removing or never creating the `-o` file is the simplest way
  to satisfy the third column.

Conditions that MUST be diagnosed failures, and the milestone from which the
reference implementation actually diagnoses each one. The arrival column is
measured against `wild 0.2.0`-`0.10.0`, one construct per probe; a milestone's
own `TEST_INTERFACE.md` section states which of these its corpus asserts.

| condition | must name | diagnosed from |
| --- | --- | --- |
| an input file named on the command line cannot be opened | the path | `0.2.0` (step 1) |
| an input is not ELF, or `e_ident` magic is wrong | -- (`Couldn't identify file type`; the path is named only from `0.10.0`) | `0.2.0` (step 1) |
| an input has `e_ident[EI_CLASS] != ELFCLASS64` | -- (`Only 64 bit ELF is currently supported` through `0.9.0`, but `0.10.0` says only `Failed to identify FILE`, so no token holds at every tag) | `0.2.0` (step 1) |
| an input has `e_ident[EI_DATA] != ELFDATA2LSB` | -- (`Only little endian is currently supported` through `0.9.0`; `0.10.0` as above) | `0.2.0` (step 1) |
| the entry symbol has no definition | the symbol | `0.2.0` (step 1) **through `0.4.0` only** -- see the note below |
| an unknown relocation type appears in a retained section | -- (the number is written `250` at `0.2.0`, `0xfa` from `0.4.0`, and not written at all at `0.3.0`) | `0.2.0` (step 1) |
| an option is not recognised | the option | `0.2.0` (step 1) |
| `-o` was not given a value, or the output cannot be created | the path, when a path exists to name | `0.2.0` (step 1), except that `0.3.0` accepts a trailing `-o` silently at rc `0` |
| an input's `e_machine` differs from the output's | the file | **`0.4.0` (step 3)** -- `0.2.0` and `0.3.0` link an AArch64 object into an x86-64 output at rc 0, silently |
| an undefined, non-weak symbol remains after resolution | the symbol | **`0.4.0` (step 3)** -- see the note below |
| a relocation's value does not fit its field (§3 of `03-x86-64-relocations.md`) | the relocation type and the symbol | **`0.7.0` (step 6)** -- `0.2.0` through `0.6.0` truncate the field silently at rc 0 |

One row's obligation *ends* rather than beginning, which is why it is the only
one carrying a closing tag:

* **Missing entry symbol.** Measured across all nine releases: `0.2.0` says
  `Missing _start symbol` and exits non-zero; `0.3.0` and `0.4.0` fail with
  `Failed to activate <prelude>` and name no symbol; and from `0.5.0` the link
  **succeeds** -- `0.5.0`-`0.8.0` exit `0` after printing a note on *stdout*,
  `0.9.0`-`0.10.0` exit `0` after printing ``cannot find entry symbol `_start` ``
  as a *warning* on stderr, and all four leave a complete artifact. Nothing
  from `0.5.0` on may be graded as a diagnosed failure here, and no case that
  has to hold at every tag can assert this condition at all. (The stdout text
  at `0.5.0`-`0.8.0` does not conflict with the routing rule above: the link
  succeeded, so what it printed is not a diagnostic.)

Two further rows break the third column of the table above, and a
milestone that grades them must know it:

* **Undefined symbol.** At `0.2.0` the link exits **1** and still leaves a
  complete-looking `ET_EXEC` at the `-o` path. The symbol *is* named, but only
  at the bottom of a nested cause chain whose first line is `Failed copying
  from OBJ (N) to output file`; the line that names it is the last cause,
  `Reference to undefined symbol` followed by the name in backticks. Measured
  at `0.2.0` for `R_X86_64_PLT32`, `PC32`, `32`,
  `64` and `REX_GOTPCRELX`: all five behave identically, so the message shape
  does not vary with the referencing relocation -- it varies only with whether
  the referencing section is retained at all, since a relocation in a section
  that never reaches the output is never applied and never diagnosed. At
  `0.3.0` the link exits **0** and prints nothing at all. Only from `0.4.0` is
  it the single line `Undefined symbol NAME, referenced by ...`, non-zero, with
  no artifact.
* **Relocation overflow.** From `0.7.0` the link fails and the message names
  both the type and the symbol, but a file is still left at the `-o` path, so a
  case grading overflow must not also assert the artifact's absence.

## 9. Option spelling

Spelling is **not** uniform across releases, and the milestone at which each
form starts working is itself graded, so this section states the measured
transitions rather than a rule of thumb.

**How many dashes a long option takes.** From `0.3.0` both spellings are
interchangeable for every long option the package grades. At `0.2.0` each is
accepted with *exactly one* spelling, and which one it is varies by option:

| option | `0.2.0` | `0.3.0` and later |
| --- | --- | --- |
| `-static`, `-pie` | one dash only | either |
| `--eh-frame-hdr`, `--gc-sections`, `--strip-all`, `--strip-debug`, `--no-string-merge`, `--start-group`, `--end-group`, `--build-id` | two dashes only | either |

A milestone-1 linker MUST therefore accept each graded option in the spelling
its case actually uses. Accepting both is permitted and is never penalised, so
the simplest conforming choice is to accept both everywhere.

**Long options that take a value.** `--opt=VALUE` is accepted at every
milestone for every valued long option, and is the form most cases use. The
separated `--opt VALUE` form is *not* general: `--build-id sha1` and
`--threads 2` are diagnosed as a missing input file named `sha1` or `2` at
**every** release from `0.2.0` to `0.10.0`, because both of those options are
also valid with no value at all and the parser therefore does not consume the
next argument. Any other option that is valid bare behaves the same way.

`--opt=VALUE` is nevertheless **not** the only form required. The graded
corpus passes several valued *long* options in the separated form, and a
conforming linker MUST accept those, spelled that way, from the milestone that
uses them: `--version-script FILE` and `--dynamic-linker PATH` (step 2
onwards), `--script FILE`, `--wrap SYM` and `--exclude-libs ALL` (step 5),
`--retain-symbols-file FILE` (step 6), `--defsym NAME=EXPR` (step 7), and
`--plugin PATH`, `--plugin-opt OPT`,
`--dependency-file FILE` and `--auxiliary NAME` (step 8). Measured, the
reference accepts **both** spellings of each of those from the release at which
the option itself arrives -- `--version-script` and `--dynamic-linker` from
`0.3.0`, `--script`, `--wrap` and `--exclude-libs` from `0.6.0`,
`--retain-symbols-file` from `0.7.0`, `--defsym` from `0.8.0`, `--plugin` from
`0.5.0`, the other three from `0.9.0`.

Do **not** generalise that to every option an option table spells with a
separated placeholder. The tables write `--opt VALUE` as a naming convention,
not as a promise about spelling, and the two do not always agree: measured,
`--entry`, `--script` and `--sysroot` are rejected in their separated form
until `0.6.0` even where the joined form already works (`--entry=NAME` links at
`0.5.0`; `--entry NAME` does not), and `--output` is rejected in **both**
spellings until `0.6.0` (`--output=PATH` is `Unrecognised argument` at
`0.2.0`-`0.5.0`, and `--output PATH` is too; neither has a case before that
release). No case passes any of the four separated before `0.6.0`.
Accepting both spellings wherever the value is mandatory is the simplest
conforming choice and is never penalised. None of this reaches the short options below, whose separated forms
arrive in stages.

**Short options that take a value.** `-L` accepts the joined form (`-Lpath`)
and the separated form (`-L path`) at every milestone. The other two arrive in
stages, measured:

| form | accepted from |
| --- | --- |
| `-o output`, `-Lpath`, `-L path`, `-lfoo` | `0.2.0` |
| `-l foo` (separated) | `0.6.0` |
| `-ooutput` (joined) | `0.9.0` |

Where a milestone's `TEST_INTERFACE.md` section states a spelling rule for an
option, that section governs and this table does not override it.

An option this profile and the milestone sections do not mention is not
graded, but an *unrecognised* option MUST be a diagnosed error under §8, never
a silent success. A linker MAY accept and ignore an option it does not
implement, and the reference implementation does exactly that for a list that
includes `--as-needed`, `--no-as-needed`, `-Bstatic`, `-Bdynamic`,
`-nostdlib`, `-O<n>` and `-plugin-opt=`; that is a compatibility measure, not a
graded behaviour. It does **not** reach an option that a milestone's
`TEST_INTERFACE.md` section states a rule for: such an option is graded as that
section says, whatever this list suggests. Three that a reader is most likely
to assume are inert but are not -- `--start-group`/`--end-group`, where the
archive group is iterated to a fixpoint from step 1; `--hash-style=`, graded
from step 2; and `--build-id`, accepted but inert at step 1 and emitting
`.note.gnu.build-id` from step 2.

Input files are positional. The order of input sections within an output
section follows command-line order (`04-layout-and-sections.md` §1.1), and
`-L` establishes a search path for subsequent `-l` arguments only
(`02-symbols-and-archives.md` §7.5). Archive *extraction*, by contrast, does
not depend on order in this profile.

### 9.1 The output file

`-o PATH` is where the linked image goes, and nowhere else. The linker MUST
write exactly that path; it MUST replace whatever was there rather than
extending or patching it, so that linking over a larger stale file leaves a
file of the new, smaller size; and it MUST leave no other file behind in the
containing directory, which means any temporary must be created and removed,
or never created. The output MUST be created with the execute bit set for the
owner — mode `0777 & ~umask`, which is `0755` under the usual `umask 022` —
whenever the output is an executable or a shared object. With no `-o` the output is
`a.out` from `0.3.0` onward; at `0.2.0` omitting `-o` is instead a diagnosed
error (`Missing required argument -o`, non-zero, nothing written). The grader
passes `-o` at every milestone, so this default is never what is measured.

A failed link MUST NOT leave a plausible output at that path
(`TEST_INTERFACE.md` §2). Removing it, or never creating it until the link is
known to have succeeded, both satisfy this.

### 9.2 Threading and determinism

`--threads=N` and `--threads` MUST be accepted; `-j N` MAY be. However many
threads a linker uses, **the output MUST be byte-identical across runs**: two
links of the same inputs with the same options, and a link with
`--threads=1` against a link with `--threads=8`, MUST produce identical files.
No case passes `--threads` at any milestone, so this is a correctness
requirement that is *not* directly graded; what the corpus does grade is the
determinism it protects, because a case that links twice and compares, or that
asserts an exact address or an exact symbol order, fails outright on a linker
whose output wanders between runs. Anything order-dependent — iteration over
a hash map, the order in which worker threads claim sections, a timestamp, a
random build-id seed — MUST NOT reach the output. Note that this is *not* the
byte-identity that `TEST_INTERFACE.md` §4 excludes: §4 is about identity with
**another linker**, which no milestone grades, whereas what this section
requires is reproducibility against **itself** — an obligation on a conforming
linker, and the property every case that asserts an exact address or an exact
symbol order silently depends on, but never asserted on its own because no
case passes `--threads`.

## 10. String merging

`--no-string-merge` MUST be accepted and MUST disable merging.

By default, an input section is a candidate for string merging exactly when
all three hold:

* `sh_flags` has `SHF_MERGE` (`0x10`), and
* `sh_flags` has `SHF_STRINGS` (`0x20`), and
* `sh_addralign` is `0` or `1`.

Candidates are merged by full-string identity: two byte sequences that are
equal up to and including their terminating NUL share one copy in the output.
**Tail merging is not required and is not graded** — a linker MAY leave
`"tail"` as a separate copy even when it is a suffix of another retained
string. Sections that are `SHF_MERGE` without `SHF_STRINGS` (fixed-size entries
described by `sh_entsize`) do **not** behave uniformly across the release
history, so this profile grades them milestone-relatively.
`04-layout-and-sections.md` §3 permits identical fixed-size elements to be
folded, and a linker that simply concatenates them is equally conforming as a
matter of format. The earlier wording here said they MUST be concatenated *and*
that deduplicating them was legal, which cannot both be true.

Measured at every milestone with two probes -- three 16-byte `aM` sections of
which two are byte-identical, and two objects each contributing one identical
8-byte entry:

| milestone | fixed-entry `SHF_MERGE` | probe sizes |
| --- | --- | --- |
| `0.2.0`-`0.7.0` | concatenated | 48 bytes / 16 bytes |
| `0.8.0`-`0.10.0` | deduplicated | 32 bytes / 8 bytes |

Up to and including `0.7.0`, folding is **not graded either way**. From `0.8.0`
onward, input sections of this kind whose bytes are identical MUST collapse to
a single copy and symbols defined in them MUST resolve to the same address,
while a section whose bytes differ MUST remain a separate copy. Folding is
subject to the alignment cutoff the string rule shares: a contribution whose
`sh_addralign` exceeds `1` is never folded, whatever its bytes — it is copied
whole, one aligned slot per contribution, and is measured that way
(`04-layout-and-sections.md` §3).

A section that is not a candidate MUST be concatenated unchanged, and its
`sh_entsize` MUST NOT be propagated to a merged output section that mixes it
with other content. Relocations against a symbol inside a merged section MUST
resolve to the address of the *surviving* copy of the string that symbol's
`st_value + addend` falls inside.

`04-layout-and-sections.md` §3 gives the mechanics.

## 11. Not graded

Repeating `TEST_INTERFACE.md` §4 for the things a reader of this document is
most likely to over-implement: byte-identity with `ld`, `lld` or `mold`;
padding bytes a linker is free to choose; `.comment` content beyond any rule a
milestone's own `TEST_INTERFACE.md` section states for it; `--build-id`
computation; the choice among legal relaxations; `--map`/`-Map`/`--print-map`
in any form; speed; and the absolute addresses of sections beyond the
constraints in §5.
