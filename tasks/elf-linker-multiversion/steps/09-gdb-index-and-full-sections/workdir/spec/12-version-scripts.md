# Version scripts

> Grammar of version scripts, the pattern-matching precedence that decides which node claims a symbol, and the effect on `.dynsym`, `.symtab` and the version sections.

## Provenance

The grammar restates the `VERSION` chapter of the GNU ld manual
(`ld.texi`, binutils 2.40, <https://sourceware.org/binutils/docs/ld/VERSION.html>)
and the symbol-versioning behaviour described in the Linux Standard Base ELF
extensions. The three-phase matching precedence restates the algorithm as
implemented by LLD and mold and documented at
<https://maskray.me/blog/2020-11-26-all-about-symbol-versioning>; the reference
implementation follows that description deliberately, and every ordering rule
below was re-measured against it on this machine with
`wild-0.10.0` and `llvm-readelf-16`.

Deliberate narrowings, all measured rather than assumed: only `global:` and
`local:` section keywords exist (there is no `protected:`); a node's parent is
a single name; `extern "C++"` matches demangled names via the same glob engine;
and a second `--version-script` replaces the first rather than merging with it.
Where GNU ld 2.40 and the reference implementation differ, the reference
implementation is normative here and the difference is called out in the text.

## 1. How a version script reaches the linker

Three routes, all equivalent once parsed:

| Route | Notes |
| --- | --- |
| `--version-script FILE` | the ordinary route |
| `VERSION { ... }` inside a `-T` linker script | the braces after `VERSION` hold exactly the same node list as a standalone script file. Measured: identical `.dynsym`, `.gnu.version` and `.gnu.version_d` to the `--version-script` form |
| a version script named as the only `-T` script | the file is parsed as a linker script, so a bare node list must be wrapped in `VERSION { ... }` |

If `--version-script` is given more than once, the **last** one takes effect
and the earlier ones are discarded. (Measured: passing a script defining node
`A` and then a script defining node `B` produces only `B`'s nodes.)

A version script has an effect on `.dynsym` only when the output has a dynamic
symbol table — a shared object, or an executable built with
`--export-dynamic`/`-E` or `-pie` where symbols are exported. For a plain
static executable the script still governs which symbols are demoted in
`.symtab`.

## 2. Grammar

```
script      := node+ | anonymous_node
anonymous_node := '{' body '}' ';'
node        := NAME '{' body '}' [ PARENT ] ';'
body        := ( section_kw | matcher ';' )*
section_kw  := 'global:' | 'local:'
matcher     := pattern
             | 'extern' '"C"'   '{' pattern ';' ... '}' ';'
             | 'extern' '"C++"' '{' pattern ';' ... '}' ';'
pattern     := bare_token | '"' quoted_token '"'
```

Rules:

* Whitespace is insignificant. `/* ... */` comments are skipped anywhere
  whitespace is allowed, and MUST be skipped inside node bodies as well as
  between nodes.
* Before the first `global:` or `local:` keyword in a body, matchers default to
  `global:`. A keyword stays in force until the next one.
* Each matcher MUST be terminated by `;`, except that the last matcher inside
  an `extern` block MAY omit it.
* A pattern containing spaces, parentheses, `:` or other punctuation MUST be
  written inside double quotes. This is normal for `extern "C++"` patterns such
  as `"ns::f(int)"`.
* `PARENT` is a bare version name that MUST have been defined by an earlier
  node in the same script. Naming an unknown parent MUST fail the link, with a
  diagnostic on stderr. No particular wording is required and nothing requires
  the unknown version to be named — see § 8, which governs every version-script
  rejection.
* A script consisting of a single unnamed node (`{ ... };`) is *anonymous*: it
  filters exports but defines no versions. Measured: an anonymous script
  produces no `.gnu.version`, `.gnu.version_d` or `DT_VERDEF` at all, and the
  exported symbols appear in `.dynsym` with no `@` suffix.

### 2.1 Version indices

Version indices in `.gnu.version` are 16-bit:

| Index | Meaning |
| --- | --- |
| 0 (`VER_NDX_LOCAL`) | the symbol is local; it does not appear in `.dynsym` |
| 1 (`VER_NDX_GLOBAL`) | global, unversioned; in a `.gnu.version_d` this is the BASE entry naming the output `soname` |
| 2, 3, … | named nodes, in the order the script defines them |
| bit 0x8000 | the *hidden* bit; see §6 |

The linker MUST assign index 2 to the first named node, 3 to the second, and so
on, regardless of the alphabetical order of the names. It MUST emit a BASE
`Verdef` at index 1 whose name is the output file's soname (or the output file
name if no `-soname` was given) whenever any named node exists.

`DT_VERDEFNUM` counts the BASE entry plus every named node.
A node with a parent produces a `Verdef` whose `vd_cnt` is 2: its own name
first, then the parent's name as a `Verdaux`. A node without a parent has
`vd_cnt` 1.

## 3. Pattern syntax

A pattern is matched against the whole symbol name; there is no implicit
anchoring to fix and no path-separator special-casing.

| Construct | Meaning |
| --- | --- |
| `*` | any sequence of characters, including the empty sequence |
| `?` | exactly one character |
| `[abc]` | one character from the set |
| `[a-z]` | one character from the range |
| `[!abc]` | one character **not** in the set |
| `[^abc]` | same as `[!abc]` — an accepted extension |
| `\c` | a literal `c`, cancelling any special meaning |

The `[^...]` spelling is an extension over POSIX glob, which uses `!`. The
implementation MUST accept both and treat them identically. Measured:
`foo_[^n]*` exports `foo_old` and not `foo_new`.

A pattern is classified once, at parse time, into exactly one of five kinds.
The classification drives the matching precedence in §4, so it MUST be computed
the same way:

| Kind | Condition |
| --- | --- |
| **MatchesAll** | the pattern is exactly `*` |
| **Star** | the pattern contains an unescaped `*` (scanning left to right, a `\` consumes the next character) |
| **NonStar** | the pattern contains no unescaped `*` but does contain `?`, `[` or `]` |
| **EscapedExact** | the pattern contains `\` but no `*`, `?`, `[` or `]`; the backslashes are stripped and it becomes an exact name |
| **Exact** | none of `*`, `?`, `\`, `[`, `]` occur |

Note the scan order: a `*` anywhere unescaped makes the pattern Star even if a
`?` appeared first.

### 3.1 `extern "C"` and `extern "C++"`

`extern "C" { ... }` is a no-op wrapper: the patterns inside behave exactly as
if written at the same place in the body.

`extern "C++" { ... }` changes what the pattern is matched *against*. Each
candidate symbol's name is demangled first (Itanium C++ ABI mangling), and the
pattern is applied to the demangled spelling, including parameter types and
namespace qualifiers. A symbol whose name does not demangle MUST NOT match any
C++ pattern.

Measured with `namespace ns { int f(int); int f(double); } int g(int);`:

| Pattern | Matches |
| --- | --- |
| `"ns::f(int)"` | `_ZN2ns1fEi` only |
| `ns::*` | `_ZN2ns1fEi` and `_ZN2ns1fEd` |
| `*` in the C++ block | every symbol that demangles |

A node may contain both plain patterns and `extern "C++"` blocks; they are kept
in separate rule sets and, within a single phase of §4, the plain set is tried
before the C++ set (demangling is expensive and only attempted when needed).
The result is the same as trying them together, because a name cannot be both
mangled and not.

Nested `extern` blocks MUST be rejected, and an `extern "…"` with a language
other than `C` or `C++` MUST be rejected. Both are diagnosed on stderr under
the rules of § 8: naming the offending language, or the nested block, is good
practice and is never penalised, but no case requires it.

## 4. Matching precedence

A symbol may be matched by patterns in several nodes and in both the `global:`
and `local:` sections. Exactly one (node, section) pair wins. The linker MUST
select it with this three-phase search:

**Phase 1 — exact patterns, first node wins.**
Iterate nodes in declaration order, lowest index first. In each node, test, in
this order: plain `global:` exacts, plain `local:` exacts, C++ `global:`
exacts, C++ `local:` exacts. The first hit wins and the search stops.

**Phase 2 — glob patterns, last node wins, non-star before star.**
Run the whole node list twice: first considering only **NonStar** patterns,
then only **Star** patterns (excluding the bare `*`). Within each pass, iterate
nodes in **reverse** declaration order. In each node test `global:` (plain then
C++) before `local:` (plain then C++). The first hit wins.

**Phase 3 — the catch-all `*`.**
Iterate nodes in reverse declaration order; the first node whose `global:`
section contains `*` wins, else the first whose `local:` section contains `*`.

If no phase matches, the symbol is left unversioned and keeps its input
binding.

Consequences the implementation MUST reproduce:

* An exact name in a *later* node beats a glob in an *earlier* node, and also
  beats a glob in a later node.
* Between two globs of the same kind in different nodes, the later node wins —
  the opposite of the exact-pattern rule.
* A `?`/`[...]` pattern beats a `*` pattern even when the `*` pattern is in a
  later node.
* `global:` beats `local:` within the same node and the same phase.
* A version script may therefore make a symbol local that an earlier node made
  global, purely by declaration order.

The winning node's index becomes the symbol's version index, unless the node is
the anonymous base (index 0 internally), in which case the symbol stays
unversioned.

## 5. Worked example

Input object defines: `foo_old`, `foo_new`, `bar`, `barbaz`,
`internal_helper`, `other` — all `GLOBAL`, `FUNC`.

```
VERS_1.0 {
  global:
    bar*;
  local:
    *;
};
VERS_2.0 {
  global:
    bar;
    foo_*;
} VERS_1.0;
```

Version indices: `VERS_1.0` = 2, `VERS_2.0` = 3, BASE = 1.

| Symbol | Phase that decides it | Winning rule | `.dynsym` name | Version index | `.symtab` binding |
| --- | --- | --- | --- | --- | --- |
| `bar` | 1 (exact) | `bar` in `VERS_2.0` | `bar@@VERS_2.0` | 3 | GLOBAL |
| `barbaz` | 2 (star glob, reverse order) | `bar*` in `VERS_1.0` | `barbaz@@VERS_1.0` | 2 | GLOBAL |
| `foo_old` | 2 (star glob) | `foo_*` in `VERS_2.0` | `foo_old@@VERS_2.0` | 3 | GLOBAL |
| `foo_new` | 2 (star glob) | `foo_*` in `VERS_2.0` | `foo_new@@VERS_2.0` | 3 | GLOBAL |
| `internal_helper` | 3 (catch-all) | `*` in `VERS_1.0` `local:` | absent | 0 | LOCAL |
| `other` | 3 (catch-all) | `*` in `VERS_1.0` `local:` | absent | 0 | LOCAL |

Note that `bar` is claimed by `VERS_2.0` even though `VERS_1.0`'s `bar*` also
matches and `VERS_1.0` is declared first: the exact pattern wins in phase 1.
This is the exact output measured from the reference implementation.

A second, smaller example isolating phase 2:

```
A { global: foo*;    local: *; };
B { global: foo_ol?;              };
```

`foo_old` matches the NonStar pattern in `B` and the Star pattern in `A`;
NonStar is tried first, so it lands in `B`. `foo_new` matches only `foo*`, so
it lands in `A`. Measured: `foo_old@@B`, `foo_new@@A`.

Replacing `B`'s pattern with `foo*` makes both patterns Star, and then the
later node wins for both: `foo_old@@B`, `foo_new@@B`.

## 6. Effect on the output tables

### `.dynsym`

* A symbol whose winning section is `local:` MUST NOT appear in `.dynsym` at
  all. It is not demoted there; it is removed.
* The matching of § 4 applies to symbols that come from **input files**.
  A symbol *defined by a linker script* (`sym = expr;`, `PROVIDE(...)`,
  `10-linker-scripts.md` § 7) is created by the link itself and is **not**
  subject to version-script filtering: it keeps the `STB_GLOBAL` binding
  document 10 § 7 gives it and is still exported, even when `local: *;` — or
  an explicit `local: sym;` — would otherwise match its name. Measured at
  `wild-0.6.0` and `wild-0.10.0`: a link that takes `vmark = .;` from its `-T`
  script and `{ global: foo[a-c]; local: *; };` from its `--version-script`
  exports `fooa`, `foob`, `fooc` **and** `vmark`.
* A symbol whose winning section is `global:` in a named node appears with its
  plain name, and its `.gnu.version` entry holds the node index.
* `readelf` renders a defined symbol at index *n* as `name@@VERSION` when the
  version is the *default* one for that name, and `name@VERSION` when it is
  not. The `@`/`@@` distinction is carried by the hidden bit, not by the
  string: `.dynstr` stores only `name`.
* Bit `0x8000` of the `.gnu.version` entry is the hidden bit. A defined symbol
  placed in a version by a version script alone is always default, so the bit
  is clear. It is set only for non-default aliases, which arrive from `.symver`
  (see document 13).

### `.symtab`

* A symbol made local by the script MUST be written to `.symtab` with binding
  `STB_LOCAL`, and MUST be placed in the local region of `.symtab` so that
  `sh_info` still equals the index of the first non-local symbol.
* Symbols exported with a version are written to `.symtab` under their plain
  name with no `@` suffix and their original binding. Measured: a library whose
  `.dynsym` reads `foo_old@@VERS_1.0` has `.symtab` entry `foo_old`,
  `GLOBAL`, with the same value.
* `--strip-all` removes `.symtab` entirely and has no effect on `.dynsym` or on
  the version sections.

### Undefined symbols

An undefined symbol is versioned by the *definition it binds to* in a shared
library, not by the local version script. The script's `local:` rules MUST NOT
be applied to undefined symbols to the point of dropping them from `.dynsym`:
an undefined reference that must be resolved at run time has to stay.

## 7. Interaction with other options

| Option | Interaction |
| --- | --- |
| `--retain-symbols-file FILE` | affects `.symtab` only. `.dynsym`, `.gnu.version` and `.gnu.version_d` are untouched. Measured: with a retain file listing `foo_old` and `bar`, `.symtab` holds those two plus the linker-defined symbols (`__ehdr_start`, `_etext`, `etext`, `__etext`, `__executable_start`, `_DYNAMIC`) and nothing else — the `FILE` symbol and every other local is dropped — while `.dynsym` still carries all four versioned exports |
| `--exclude-libs LIST` | takes precedence over the version script. A symbol supplied by a named archive is removed from `.dynsym` even when a `global:` rule explicitly names it. Measured: with `--exclude-libs libar.a` plus a script listing `foo_old` under `global:`, `.dynsym` ends up with only its null entry |
| `--exclude-libs ALL` | same, for every archive on the command line |
| `-Bsymbolic` | binds defined references internally; it does **not** change which symbols the version script exports, and in the reference implementation it does **not** set `DF_SYMBOLIC` in `DT_FLAGS` (measured: `DT_FLAGS` held only `BIND_NOW`). Do not require `DF_SYMBOLIC` |
| `-Bsymbolic-functions`, `-Bsymbolic-non-weak` | narrower forms of the same; orthogonal to versioning |
| `--gc-sections` | runs before export filtering; a symbol whose section was collected is gone regardless of the script |
| `--undefined SYM`, `-u SYM` | forces the symbol to be defined, but the script still decides whether it is exported |
| `--soname NAME` | supplies the string for the BASE `Verdef` entry |

## 8. Errors

The link MUST fail, with a diagnostic on stderr, when:

* a node names a parent version that no earlier node defined;
* an `extern` block declares a language other than `"C"` or `"C++"`;
* an `extern` block is nested inside another `extern` block;
* a pattern is not valid glob syntax (for example an unterminated `[`);
* a brace, or the final `;` after a node, is missing.

No particular wording, prefix or exit value is required — only a non-zero exit
and a diagnostic on stderr. This is not a courtesy: for the rejections listed
above, **no graded case requires any particular word, token or message to
appear** — the cases check that the link failed and that the linker wrote
something to stderr, and nothing more. Naming the offending construct is good
practice and is never penalised.

Measured at `wild-0.6.0` and `wild-0.10.0` (rc 255, everything on **stderr**,
stdout empty): every rejection above begins `wild: error: Failed to parse
version script:` followed by `parse error at line N, column M` and an excerpt
of the offending source line under a caret. Three of them add one more line
after the caret — `Invalid glob pattern`, `Unexpected extern "C++" in parsing`,
and `Unknown parent version` — while the bad `extern` language and the missing
brace add nothing after the caret, so the only place a word such as `Pascal`
appears is inside the echoed source line. A missing `;` after a node is
reported as `Unknown parent version` too, because the grammar reads the next
node's name as a parent. Requiring any of this would be requiring the
reference's diagnostic rendering; do not do it.

A pattern that is not valid UTF-8 is deliberately **not** on this list, even
though earlier drafts of this document and the GNU ld manual both suggest it
should fail the same way. Measured against `wild-0.6.0` through `wild-0.10.0`:
a quoted pattern containing raw non-UTF-8 bytes (`"pub\xff\xfe"`) is accepted,
the link succeeds, and the pattern simply never matches any symbol name (every
name in this ABI is valid UTF-8, so the byte-for-byte pattern comparison always
misses). The release wins over the restated manual text here — do not test
this as a rejection; there is nothing at any tag through 0.10.0 for such a case
to pass against.
