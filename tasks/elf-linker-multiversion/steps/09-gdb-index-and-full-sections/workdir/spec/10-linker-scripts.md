# Linker scripts

> The subset of the GNU ld script language this linker must accept, with the exact expression grammar, built-in functions and layout semantics that are graded.

## Provenance

This restates the GNU `ld` script language as described in `ld.texi` (binutils
`binutils-2_47` branch, <https://sourceware.org/git/binutils-gdb.git>,
`ld/ld.texi`, sections *Scripts*, *Script Commands*, *SECTIONS*, *MEMORY*,
*PHDRS*, *Expressions*), reorganised around what the linker has to do rather
than around what a user may write.

It is **deliberately narrowed** to what the reference implementation accepts and
to what the graded corpus exercises. Where the GNU manual describes a construct
that the reference implementation rejects, that fact is recorded here as a
rejection requirement, because the graded tests run against the reference
implementation's behaviour. Every acceptance, rejection, numeric result and
error-message shape in this document was measured on this machine against
`wild-0.10.0` (and, where a step is named, against the tag binary for that
step). Where GNU `ld` 2.40 and the reference implementation differ, the
reference implementation wins and the difference is called out in the text.

Terminology: *VMA* is the virtual address a section is linked to run at; *LMA*
is the address it is loaded at. `.` is the location counter.

## 1. Where scripts come from, and which ones matter

A linker script reaches the linker in exactly three ways.

| Route | Effect |
| --- | --- |
| `-T FILE`, `--script=FILE`, `-script=FILE` | FILE is parsed as a full linker script and **replaces** the built-in layout |
| An input file on the command line that is not an ELF object or archive | parsed as a linker script; typically a `GROUP(...)`/`INPUT(...)` stub such as a libc `.so` text file |
| `--version-script FILE` | parsed by the *version script* grammar, not this one (see *Producing dynamic output* §12 for the scoping effect and *Symbol versioning* §7 for the `VERSION_NAME { ... }` node grammar) |

The attached short form `-TFILE` and the space-separated long form
`--script FILE` are **not** accepted at step 4; they arrive at step 5 (see
§13). At step 4 they are unrecognised arguments, which *Compatibility profile* §9 makes a diagnosed error, so a script supplied that way is not read
at all. `-T FILE`, `--script=FILE` and `-script=FILE` work at every milestone
that reads scripts.

The linker MUST detect route 2 by content, not by extension: an input file whose
first bytes are not `\x7fELF` and not an archive magic MUST be handed to the
script parser. A script supplied by route 2 MUST NOT replace the built-in
layout; only its `GROUP`/`INPUT`/`AS_NEEDED`/`OUTPUT_FORMAT`/`ENTRY` effects
apply.

`VERSION { ... }` MAY appear inside a `-T` script; it means the same thing as a
`--version-script` file's contents.

## 2. Lexical structure

* Whitespace (space, tab, newline, carriage return) separates tokens and is
  otherwise insignificant.
* `/*` … `*/` is a comment. It does not nest.
* `#` begins a comment that runs to the end of the line.
* A **token** is either a double-quoted string (`"..."`, taken literally, with
  no escape processing) or an unquoted run of bytes that stops at any of
  space, tab, newline, `(`, `)`, `{`, `}`, `;`, `,`.
* Section names, file names and wildcard patterns are tokens. A section name
  containing a character that would end a token MUST be quoted; `"/DISCARD/"`
  and `/DISCARD/` are both legal spellings.
* Numbers are decimal, or hexadecimal when prefixed `0x` or `0X`. A trailing
  `K` or `k` multiplies by 1024; a trailing `M` or `m` multiplies by 1048576.
  There is no octal and no `G` suffix.
* An identifier in an expression starts with an ASCII letter, `_` or `.` and
  continues with ASCII alphanumerics, `_` or `.`. The identifier `.` alone is
  the location counter.

## 3. Top-level commands

The parser reads top-level commands until end of file. The following are
recognised.

| Command | Requirement |
| --- | --- |
| `ENTRY(sym)` | sets the entry symbol. A `-e`/`--entry` command-line option MUST take precedence over the script. |
| `SECTIONS { ... }` | see §4. At most one is graded; a second one extends the first. |
| `MEMORY { ... }` | see §9. |
| `PHDRS { ... }` | see §10. |
| `VERSION { ... }` | version-script body; the scoping effect is in *Producing dynamic output* §12, the node grammar (`VERSION_NAME { global: ...; local: ...; };`) is in *Symbol versioning* §7. |
| `GROUP(a b c)` / `INPUT(a b c)` | adds input files, separated by whitespace or commas. `GROUP` additionally means "re-scan until no new definitions", which for this corpus is indistinguishable from `INPUT`. |
| `AS_NEEDED(a b)` | legal inside `GROUP`/`INPUT`; marks the enclosed shared objects as needed only if referenced. |
| `OUTPUT_FORMAT(fmt)` or `OUTPUT_FORMAT(def, big, little)` | parsed. If the named format does not match the target being produced, the link MUST fail and stderr MUST name either `OUTPUT_FORMAT` or the offending format string -- the reference does one or the other depending on the format, and no case requires a particular one. A matching format is a no-op. |
| `OUTPUT_ARCH(arch)` | parsed and ignored. |
| `PROVIDE(sym = expr)` / `PROVIDE_HIDDEN(sym = expr)` / `ASSERT(expr, "msg")` | legal at top level as well as inside `SECTIONS`; see §7 and §11. |
| `name = expr;` and `name op= expr;` | a symbol assignment (§7). Any token followed by `=` is an assignment. |
| anything else | treated as the name of an input file. |

That last rule has a measurable consequence the implementation MUST reproduce:
`INCLUDE other.ld` is **not** supported, and because `INCLUDE` is an unknown
token it is taken as a filename, so the failure is a missing-input-file error naming `INCLUDE`, not a parse
error.

`SEARCH_DIR(...)` and `STARTUP(...)` MUST be rejected as parse errors. (GNU `ld`
accepts both; the reference implementation does not, and the graded corpus
never uses them.)

## 4. `SECTIONS` and output-section descriptions

```
SECTIONS {
  statement ...
}
```

A statement inside `SECTIONS` is one of: a symbol assignment (§7), a
`PROVIDE`/`PROVIDE_HIDDEN`/`ASSERT`, or an **output-section description**:

```
NAME [address-expr] [(TYPE)] : [AT(lma-expr)] [ALIGN(dec)]
{
    input-statement ...
}  [>REGION] [:PHDR ...] [=FILL]
```

Requirements:

* `NAME` is a token (quotable). `/DISCARD/` is special (§6).
* `address-expr`, when present between the name and the `:`, sets the output
  section's VMA. Writing `.text 0x200000 : { ... }` and writing
  `. = 0x200000; .text : { ... }` MUST produce the same VMA for `.text`.
  **This slot arrives at step 8** (§13): before that milestone anything between
  the name and the `:` is a parse error, and `. = ADDRESS;` ahead of the
  description is the only way to set an explicit address. At step 8 the slot
  accepts a numeric literal only — a non-literal expression there is the error
  `Only numeric output section start expressions are currently supported` —
  and the full expression grammar of §8 becomes available at step 9.
* `ALIGN(n)` sets the output section's alignment. In the header it is written
  **after the colon** — `.text : ALIGN(64) { ... }` — and takes a **decimal
  integer literal only**: `ALIGN(0x40)` there MUST be a parse error, and so
  must `ALIGN(expr)` for any non-literal expression. (Inside an expression,
  `ALIGN` is the ordinary built-in of §8 and does accept hex.) The slot
  *before* the colon is the `address-expr` above and never an alignment, so
  `.text ALIGN(64) : { ... }` does not mean what the GNU `ld` manual's
  equivalent spelling means: it is a parse error until step 8 and an invalid
  address expression at step 8.
* `AT(expr)` sets the section's LMA to `expr`; it arrives at step 9 (§13) and
  is a parse error before that. Without `AT`, LMA equals VMA.
  `LOADADDR(name)` then returns `expr`. Note that the reference implementation
  sets the enclosing `PT_LOAD`'s `p_paddr` from its own accumulated LMA offset,
  so `p_paddr` is generally **not** equal to the `AT` address even though
  `LOADADDR` is; assertions MUST be written against `LOADADDR`, not `p_paddr`.
* `>REGION` places the section in a memory region (§9).
* `:PHDR` assigns the section to a declared program header (§10). The suffix
  may be repeated to place one section in several segments.
* `=FILL` sets the fill pattern for gaps inside the section. The value is
  evaluated to an integer and the low 32 bits are written **big-endian** as a
  4-byte repeating pattern. `=0x90909090` therefore fills with `0x90` bytes;
  `=0x0000ffff` fills with the repeating byte sequence `00 00 ff ff`.
* `(TYPE)` — the parenthesised output-section type between the name and the
  `:` — arrives at step 9 and is a parse error before that milestone.
* The three trailing suffixes MAY appear in any order and any combination.

## 5. Input-section descriptions

An input statement inside an output section is one of:

| Form | Meaning |
| --- | --- |
| `*(pat ...)` | every input file; each `pat` is a section-name wildcard |
| `FILE(pat ...)` | only input files whose path matches `FILE` (itself a wildcard). **Step 8 and later**: at steps 4-7 this form is a parse error, not an empty match -- see the scoping note below and §13 |
| `KEEP(...)` | as above, but the matched sections are roots for `--gc-sections` |
| `SORT(pat)` / `SORT_BY_NAME(pat)` | matched sections are contributed in ascending section-name order |
| `SORT_BY_ALIGNMENT(pat)` | accepted; alignment sorting is **not** performed |
| `sym = expr;` | a symbol assignment evaluated at this point in the section |
| `. = expr;` | moves the location counter within the section |
| `CONSTRUCTORS` | accepted; contributes nothing (§12) |

Wildcard syntax for both file and section patterns is glob: `*` matches any run
of characters including none, `?` matches one character, `[abc]` and `[a-z]`
match a character class. **Only `*` is required before step 8**: at steps 4-7 a
pattern containing `?` or a `[...]` class matches nothing at all -- it is not a
parse error, so an output section described only by such a pattern is simply
empty and is dropped -- and the three metacharacters start matching at step 8.
The **per-file** input statement `FILE(pat ...)` is scoped the same way but
fails harder: at steps 4-7 both `a.o(.text)` and `*a.o(.text)` are rejected
with a parse error at the opening parenthesis, so before step 8 `*(pat ...)`
is the only form available for naming input sections, and a per-file
restriction has to be expressed by arranging the inputs instead.
Patterns of the form `prefix*` and explicit multi-name lists such as
`*(.text .text.*)` work at every milestone from step 4. A pattern list inside one `*( ... )` is
whitespace-separated: `*(.text .text.*)` matches both.

Order rules the implementation MUST follow:

1. Input sections are contributed in the order the patterns are written.
2. Within one pattern, input sections are contributed in input-file order
   unless the pattern is wrapped in `SORT`/`SORT_BY_NAME`.
3. A given input section is placed by the **first** pattern that matches it;
   it MUST NOT be duplicated into a second output section.

These three rules govern order *within* one output section only. The order of
the output sections themselves — whether the script's own listing order
survives across sections with different permissions — is governed by the
RO/X/RELRO/W bucket rule in §10, not by anything here; see §10 before writing
or debugging a test about section order.

`EXCLUDE_FILE(...)`, `SORT_BY_INIT_PRIORITY(...)` and nested sort commands such
as `SORT_BY_NAME(SORT_BY_ALIGNMENT(.text*))` are outside the grammar you are asked
to support: the reference rejects all three with a parse error, and so may you.
**None of the three is graded** -- no scored case supplies one, in this milestone
or any other, so you gain nothing by accepting them and lose nothing by refusing.
They are listed only because GNU `ld` supports all three and a linker script
copied from a GNU build may contain one.

## 6. `/DISCARD/`

```
/DISCARD/ : { *(.comment) *(.note.*) }
```

Every input section matched by a `/DISCARD/` output section MUST be dropped:
it contributes no bytes, no symbols and no relocations, and no output section
is created for it. `/DISCARD/` MUST accept only input-section matchers; a
symbol assignment or a location-counter statement inside it MUST fail the link.
The reference implementation's wording is `Illegal use of /DISCARD/ section`;
what is graded is only that the link fails and that stderr names `/DISCARD/`.

## 7. The location counter and symbol assignment

`.` reads as the current output address. Before the first output-section
description it is `0`, so a top-level expression evaluated there (a `PROVIDE`
or an `ASSERT`, say) sees `0`. Its semantics per output section are:

* At the start of an output-section description, `.` is the section's VMA after
  the header's address expression and alignment have been applied.
* `. = expr;` inside a section moves the counter relative to the section's
  start; the section's size grows to cover the new position, and the skipped
  bytes are filled with the section's fill pattern (default zero).
* `. = expr;` between output-section descriptions sets the address at which the
  next section will be placed.
* Moving `.` backwards MUST be an error: the link fails and no output file is
  written. The reference implementation's wording is `Cannot move location
  counter backwards (from 0x{from:x} to 0x{to:x})`; what is graded is the
  failure, never the wording. This check arrives at the milestone §13 names for
  it; at earlier milestones a backwards move is honoured silently and the
  following output section is placed at the lower address. One measured trap
  for anyone reasoning about this rule: the counter only moves if the output
  section after the assignment actually survives garbage collection, so
  `. = LOW;` followed by a section whose every input is collected is not an
  error at any milestone.
* Assigning to `.` MUST NOT create a symbol named `.`.

Symbol assignment forms, all terminated by `;`:

| Form | Meaning |
| --- | --- |
| `sym = expr;` | define `sym` unconditionally, overriding any object definition |
| `sym += expr;` and the other compound forms `-= *= /= <<= >>= &= |=` | read-modify-write |
| `PROVIDE(sym = expr);` | define `sym` **only if no input object defines it** |
| `PROVIDE_HIDDEN(sym = expr);` | as `PROVIDE`, and the resulting symbol has `STB_LOCAL` binding in the output |
| `HIDDEN(sym = expr);` | MUST be rejected as a parse error |

Measured semantics that the implementation MUST match:

* `PROVIDE(newsym = 0x1234)` where nothing defines `newsym` emits `newsym` with
  value `0x1234` and `STB_GLOBAL`, **even when nothing references it**. GNU
  `ld` 2.40 omits an unreferenced `PROVIDE`d symbol; the reference
  implementation emits it, and the graded corpus follows the reference
  implementation.
* `PROVIDE(_start = 0x99)` where an object defines `_start` MUST leave the
  object's value in place.
* `PROVIDE_HIDDEN(h = expr)` where `h` is unreferenced emits nothing. Where `h`
  is referenced it emits a `STB_LOCAL` symbol.
* A symbol assigned an expression that does not involve `.` is absolute: its
  section index is `SHN_ABS`. This holds however the expression was built --
  a literal, arithmetic, or a function such as `ADDR` or `LOADADDR` that reads
  the layout back. A symbol assigned from `.` is not absolute; it takes the
  address and the section index of the current position.

`--defsym sym=expr` on the command line accepts the expression grammar of §8
with the exception of `ABSOLUTE`, and is staged across milestones on the same
schedule §13 gives §8 itself. Where an operator has not yet arrived the
expression is a *link error*, not a silent zero: `--defsym x=3*4` fails at row
7's milestone (`Symbol '3*4' referenced by --defsym does not exist`) and
succeeds from row 8's. Arithmetic is 64-bit and wraps:
`--defsym x=-1` yields `0xffffffffffffffff`. `--defsym x=othersym` copies both
the value and the defining section index of `othersym`.

## 8. Expressions

All arithmetic is on 64-bit integers and wraps on overflow. Division and
modulo by zero MUST be an error.

### 8.1 Precedence

Listed loosest first. Every level is left-associative except the ternary, which
is right-associative.

| Level | Operators |
| --- | --- |
| 1 | `? :` |
| 2 | `\|\|` |
| 3 | `&&` |
| 4 | `==` `!=` `<` `<=` `>` `>=` |
| 5 | `\|` |
| 6 | `^` |
| 7 | `&` |
| 8 | `<<` `>>` |
| 9 | `+` `-` |
| 10 | `*` `/` `%` |
| 11 | unary `-` `!` `~` |
| 12 | `(expr)`, number, identifier, built-in call, `.` |

Two consequences the implementation MUST reproduce exactly:

* A comparison **does not chain**: at most one relational operator is accepted
  per comparison level, so `a < b < c` is a parse error rather than
  `(a < b) < c`.
* The bitwise operators bind **tighter** than the comparisons, unlike C. `a ==
  b | c` parses as `a == (b | c)`. Scripts that rely on C precedence here will
  produce different values; the graded corpus uses parentheses, and so should
  any generated script.

A comparison or logical operator yields `1` or `0`.

### 8.2 Built-in functions

| Call | Result |
| --- | --- |
| `ALIGN(n)` | `.` rounded up to the next multiple of `n` |
| `ALIGN(expr, n)` | MUST be rejected — the two-argument form is not supported |
| `ALIGNOF(section)` | the output section's alignment, or 0 if it does not exist |
| `ADDR(section)` | the output section's VMA |
| `LOADADDR(section)` | the output section's LMA |
| `SIZEOF(section)` | the output section's size in bytes, or 0 if it does not exist |
| `SIZEOF_HEADERS` | `64 + 56 * e_phnum`, i.e. the ELF header plus the program-header table |
| `MAX(a, b)`, `MIN(a, b)` | as named |
| `DEFINED(sym)` | 1 if `sym` is defined at this point, else 0 |
| `SEGMENT_START("name", default)` | the value of `-Ttext`/`-Tdata`/`-Tbss`-style overrides for the named segment, else `default` |
| `ORIGIN(region)`, `LENGTH(region)` | the declared origin and length of a `MEMORY` region |
| `ABSOLUTE(expr)` | MUST be rejected — not supported |
| `CONSTANT(MAXPAGESIZE)` and friends | MUST be rejected — not supported |

`SIZEOF_HEADERS` is circular by nature: the number of program headers depends
on the layout, which depends on `SIZEOF_HEADERS`. The implementation MUST
resolve this by computing the program-header count first and then evaluating
the expression. Measured values on x86-64: `0x1c8` for a script with no
`PHDRS` block that yields 7 program headers (`64 + 7*56 = 456 = 0x1c8`), and
`0xe8` for a `PHDRS` block declaring 3 segments (`64 + 3*56 = 232 = 0xe8`).

## 9. `MEMORY`

```
MEMORY {
  NAME : ORIGIN = expr, LENGTH = expr
}
```

`ORIGIN` may be spelled `org` or `o`; `LENGTH` may be spelled `len` or `l`.
Region attribute flags — the `NAME (rwx) :` form of GNU `ld` — MUST be rejected
as a parse error.

An output section carrying `>REGION` is placed at the region's current
allocation point, starting at `ORIGIN(REGION)`. From the milestone at which
`>REGION` placement is graded (step 9, §13), each of the following MUST fail
the link, with the named region or section appearing on stderr:

| Condition | Reference implementation's wording |
| --- | --- |
| duplicate region declaration | `region '{name}' already defined` |
| `>REGION` names an unknown region | `Memory region '{name}' not declared` |
| an explicit address places a section outside its region | `address 0x{addr:x} of section '{sec}' is not within region '{region}'` |
| the region's contents exceed `LENGTH` | `region '{name}' overflowed by {n} bytes` |

## 10. `PHDRS`

```
PHDRS {
  name TYPE [FLAGS(expr)] ;
  ...
}
```

Accepted segment types: `PT_NULL`, `PT_LOAD`, `PT_DYNAMIC`, `PT_INTERP`,
`PT_NOTE`, `PT_SHLIB`, `PT_PHDR`, `PT_TLS`, `PT_GNU_EH_FRAME`, `PT_GNU_STACK`,
`PT_GNU_RELRO`, `PT_GNU_PROPERTY`, `PT_GNU_SFRAME`. `FLAGS(n)` sets `p_flags`
directly (1 = X, 2 = W, 4 = R). The `FILEHDR` and `PHDRS` keywords inside a
segment declaration are not supported; a declared `PT_PHDR` is emitted with
zeroed fields.

**Declaring `PHDRS` changes output-section ordering, and this is the single
most consequential layout rule in this document.**

* Without a `PHDRS` block, the linker assigns each output section to a segment
  by its flags and then emits segments in a fixed order: non-writable
  non-executable allocated sections, then executable, then RELRO, then
  writable. Script order is preserved **only within one of those buckets**. A
  script listing `.beta .rodata .alpha .text .data` produces exactly that
  order, because the first three share a bucket; a script listing `.text
  .alpha .rodata .data .beta` produces `.alpha .rodata .beta .text .data`,
  because `.text` is moved into the executable segment and `.beta` joins the
  other read-only sections. Tests that need a specific order across buckets
  MUST use a `PHDRS` block.
* With a `PHDRS` block, the script's order is honoured exactly, and **only the
  declared segments are emitted** — no `PT_PHDR`, `PT_GNU_STACK` or
  `PT_GNU_RELRO` is synthesised.

## 11. `ASSERT`

`ASSERT(expr, "message")` evaluates `expr` after layout. A zero result MUST
fail the link: the process exits non-zero and stderr carries at least one line
that contains the script's path, the 1-based line number of the `ASSERT` within
it, and the message text. The reference implementation writes them as
`<script-path>:<line>: <message>`; only the three tokens are graded, not the
punctuation or any prefix. A non-zero result yields the value of `expr`, so
`ASSERT` may be used inside a larger expression.

## 12. `CONSTRUCTORS`

`CONSTRUCTORS` MUST parse and contribute nothing. It exists for a.out-era
compatibility; ELF init/fini arrays are handled by `.init_array`/`.fini_array`
input matching instead.

## 13. What each step needs

The graded surface grows monotonically. A step's requirement set includes every
earlier step's.

| Step | Script constructs that MUST work |
| --- | --- |
| 4 | Core `SECTIONS`: output-section descriptions with a header `ALIGN(dec)` written after the colon (`.name : ALIGN(64) { ... }`); input wildcards -- explicit names, multi-pattern `*(.text .text.*)` and trailing `*`, but not `?` or `[...]` (§5); `KEEP`; `ENTRY`; `. = ADDR;` between output-section descriptions and `sym = .;` inside one; an output-section order that differs from the built-in one (subject to §10's bucket rule). The only input-statement form required here is `*( ... )`: the per-file `FILE(pat)` description of §5 is a parse error at steps 4-7 and arrives at step 8. Assignment forms beyond these two — any right-hand side other than a bare `.`, and `. = expr;` *inside* a description — belong to later milestones |
| 5 | Everything from step 4; the attached `-TFILE` and space-separated `--script FILE` spellings of §1; `SECTIONS` combined with version scripts and with the RISC-V target |
| 6 | Everything from step 5; double-quoted tokens (§2) wherever a name is expected -- an output-section name (`"code.sec" : { ... }`), the name on the left of a symbol assignment (`"code start" = .;`, including one holding a space or a backslash) and `ENTRY`'s argument (`ENTRY("_start")`), with the quotes taken as grammar rather than as part of the name; and the top-level `VERSION { ... }` command (§3), which behaves exactly as `--version-script` (*Version scripts* §1) and may sit in the same script as `ENTRY` and `SECTIONS` |
| 7 | Everything from step 6; `--defsym sym=expr`, and symbol assignment at the script's **top level** (before or after the `SECTIONS` block). Both evaluate only the first slice of §8's grammar at this milestone: integer literals, symbol references, `+`, `-` and unary `-`. The remaining operators and the built-in functions arrive at steps 8 and 9 |
| 8 | Everything from step 7; the rest of §8's binary operators — `*`, `/`, `\|`, `^`, `&`, `<<`, `>>` — unary `~` and `!`, `&&` and `\|\|`, `MIN`/`MAX`, one-argument `ALIGN(expr)` **in an expression** (the two-argument `ALIGN(expr, align)` is rejected at every milestone, and an output-section header's post-colon alignment slot stays `ALIGN(dec)` at every milestone -- §4), `ADDR`, `LOADADDR`, `SIZEOF`, `ALIGNOF`; `PROVIDE`, `PROVIDE_HIDDEN`, `ASSERT`, `SEGMENT_START`; a `MEMORY` block declaring regions as `NAME : ORIGIN = expr, LENGTH = expr` -- including the short spellings `org`/`o` and `len`/`l` of §9 and `K`/`M` size suffixes -- together with the `ORIGIN(region)`/`LENGTH(region)` read-backs of §8, which are usable in any expression including an `ASSERT` whose failure MUST report its message. Placing a section **into** a region with `>REGION` is step 9, and a `MEMORY` block at this milestone declares regions without placing anything in them; the `?` and `[...]` glob metacharacters of §5, and the per-file input-section description `FILE(pat)` of §5 (`a.o(.text)`, `*a.o(.text)`) -- which, unlike those metacharacters, is a parse error at steps 4-7 rather than a pattern that matches nothing; the in-header `address-expr` slot of §4 (`.name 0x200000 : { ... }`), restricted to a numeric literal |
| 9 | Everything from step 8; `%`, the ternary operator, `DEFINED`, `SIZEOF_HEADERS`; symbol assignment with a general expression both inside and between output-section descriptions, and `. = expr;` moving the counter inside one; the backwards-`.` error of §7; `PHDRS` with `:phdr` suffixes and `FLAGS`, `AT(expr)` and LMA/`AT` combined with regions, `>REGION` placement of an output section into a `MEMORY` region -- the `MEMORY` block itself and its `ORIGIN()`/`LENGTH()` read-backs are graded from step 8, and only `>REGION` is a parse error until this milestone -- for which the attribute-less `NAME : ORIGIN = ..., LENGTH = ...` declaration is the only accepted form and the GNU `NAME (rwx) :` spelling MUST be rejected, together with §9's MUST-fail conditions, the `(TYPE)` header slot, §4's in-header `address-expr` slot widened from a numeric literal to a general expression (`.name (0x600000 + SIZEOF_HEADERS) : { ... }`; an expression that reads `.` is **not** graded there -- the counter is not yet set in that slot and evaluates to zero, so `ALIGN(dec)` before the colon fails the link at every milestone), `SORT`/`SORT_BY_NAME`/`SORT_BY_ALIGNMENT`, `CONSTRUCTORS`, `=FILL`, `/DISCARD/` |

## 14. Worked example

Input object `a.o` contains `.text` (0x24 bytes, alignment 4, defining
`_start`), `.rodata` (3 bytes `hi\n`), `.data` (8 bytes) and `.bss` (8 bytes).

```
ENTRY(_start)

MEMORY {
  ROM : ORIGIN = 0x200000, LENGTH = 1M
}

PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS {
  . = 0x200000;

  .text : {
      *(.text)
      . = . + 16;
      *(.rodata)
  } =0x90909090 :text

  text_size = SIZEOF(.text);
  text_align = ALIGNOF(.text);

  .data : AT(0x900000) {
      *(.data)
  } :data

  data_lma = LOADADDR(.data);
  data_vma = ADDR(.data);
  hdrs     = SIZEOF_HEADERS;
  pick     = (text_size > 4) ? 11 : 22;
  rem      = 17 % 5;
  have     = DEFINED(_start) ? 1 : 2;
  absent   = DEFINED(nosuch) ? 1 : 2;

  PROVIDE(newsym = 0x1234);
  PROVIDE(_start = 0x99);

  ASSERT(text_size > 0, "text is empty");

  /DISCARD/ : { *(.comment) }
}
```

The layout this MUST produce:

* `.text` and `.data` appear in that order and in no other, because a `PHDRS`
  block is present. Exactly two program headers are emitted, `R E` then `RW`.
* The 16 bytes between `.text`'s object contribution and `.rodata`'s contents
  are `0x90` repeated, because `=0x90909090` is written big-endian as the
  4-byte pattern `90 90 90 90`.
* `text_size` = 0x24 + 16 + 3 = 0x37; `text_align` = 4.
* `data_lma` = 0x900000 exactly. `data_vma` is the ordinary VMA and differs.
  The `PT_LOAD` containing `.data` does **not** report `p_paddr == 0x900000`.
* `hdrs` = 64 + 2\*56 = 176 = 0xb0.
* `pick` = 11, `rem` = 2, `have` = 1, `absent` = 2.
* `newsym` = 0x1234 and is present in `.symtab` even though nothing references
  it. `_start` keeps the value from `a.o`, not 0x99.
* No `.comment` section exists in the output.
* The link succeeds, since `text_size` is non-zero. Replacing the assertion
  with `ASSERT(SIZEOF(.text) > 0x1000, "text too small")` MUST instead fail
  the link, with the script path, the assertion's line number and the text
  `text too small` all appearing on stderr.

## 15. Interactions with other options

* `--gc-sections` is on by default in the reference implementation. A script
  that names sections which nothing references will appear to lose them unless
  `--no-gc-sections` is passed or the pattern is wrapped in `KEEP`.
  Implementations and tests MUST account for this; `KEEP(*(.init_array))` is
  the intended mechanism and MUST keep its matches alive.
* `-e`/`--entry` overrides `ENTRY`.
* `-Ttext`, `-Tdata`, `-Tbss` feed `SEGMENT_START`.
* A script supplied with `-T` replaces the built-in layout entirely; sections
  the script does not mention still need somewhere to go, and MUST be placed by
  the built-in rules after the script's own sections.
