# Milestone 09 — `tree-sitter` 0.27.0

This milestone is `tree-sitter` release **0.27.0**. It adds to the seven
foundation documents (`00-overview.md` through `60-runtime.md`) and to the
milestone documents before it. Everything they state still holds except where a
statement below replaces it, and where this document replaces one it says so.

Six things change, and they are independent of one another:

1. a quantified capture no longer absorbs a sibling that interrupts the run it
   is matching, whether that sibling matched a different branch of an
   alternation (§1.1-§1.3) or matched nothing in the pattern at all (§1.4);
2. a supertype whose rule is a single terminal is rejected (§2);
3. `test --update` rewrites **every** corpus file, not only files that
   contained a failing test (§3);
4. generated parsers declare ABI **15** (§4);
5. `generate --abi` rejects a version outside the supported range instead of
   accepting it or aborting (§5);
6. `parse --cst --no-ranges` lays the tree out flush left and keeps a node's
   text on the node's own line (§6).

---

## 1. A quantified capture stops at a break in the run

`50-queries.md` §1.5 allows an alternation to carry a quantifier, and §1.7
gives the quantifier suffixes. This section fixes **where one match ends and
the next begins** when a quantified alternation is matched against a run of
siblings. It does not change how many captures a match reports, nor the format
of a capture line, nor the order captures appear in.

### 1.1 The rule

When an alternation is matched against consecutive siblings, group the siblings
into **maximal runs that matched the same branch**. Each run is one match.

* a run matched by a **quantified** branch (`*`, `+`) is a single match holding
  every node in the run;
* a run matched by an **unquantified** branch is a match of exactly one node;
* a sibling that matches a different branch from the run in progress **ends**
  that match and begins a new one. It is never folded into it;
* a sibling that matches **no** branch likewise ends the run in progress. It is
  not captured, and the next sibling that does match begins a fresh match
  (§1.4).

Matches are reported in the order their runs appear in the source. A break in a
run therefore raises the number of matches; it never merely shortens one.

What this section fixes is the **quantified** branch — the `*` and `+` of
`50-queries.md` §1.7. The optional quantifier `?` is not part of this rule and
is unchanged by this milestone: the matching it performs is the same before and
after. The per-capture view of `--captures` (`50-queries.md` §3) is likewise
unchanged, because it reports one line per capture and carries no per-match
grouping for this rule to move.

### 1.2 Worked example

Take a grammar whose `source_file` is a repetition of `inc` and `comment`
nodes, and the query

```text
[
  (inc)+
  (comment)
] @cap
```

Against a file holding three `inc` nodes and then one `comment`:

```text
#include <foo>
#include <bar>
#include <baz>

// comment
```

`tree-sitter query` reports **two** matches — the run of three, then the
comment on its own:

```text
in.alx
  pattern: 0
    capture: 0 - cap, start: (0, 0), end: (0, 14), text: `#include <foo>`
    capture: 0 - cap, start: (1, 0), end: (1, 14), text: `#include <bar>`
    capture: 0 - cap, start: (2, 0), end: (2, 14), text: `#include <baz>`
  pattern: 0
    capture: 0 - cap, start: (4, 0), end: (4, 10), text: `// comment`
```

Reporting these as **one** match of four captures is the behaviour this
milestone removes.

Against interleaved input the runs are shorter and there are more of them. For

```text
#include <foo>
// one
#include <bar>
#include <baz>
// two
```

the same query reports **four** matches, of one, one, two and one captures
respectively, in that order.

### 1.3 What does not depend on the shape of the query

The rule is a property of matching, not of the query's surface syntax. Against
the interleaved input of §1.2 — where no two comments are adjacent — all of
these group identically, into the same runs:

| query | grouping |
| --- | --- |
| `[ (inc)+ (comment) ] @cap` | by run |
| `[ (inc)* (comment) ] @cap` | by run |
| `[ (inc)+ (comment)+ ] @cap` | by run |
| `[ (inc)+ @inc (comment) @com ]` | by run, each branch reporting its own capture name |

A capture placed on the alternation itself and a capture placed on each branch
differ only in the name reported; the match boundaries are the same.

The one place the second and third rows part company from the first is a run of
**adjacent comments**, and that is §1.1's rule again rather than an exception to
it: an unquantified `(comment)` makes each comment a match of its own, while
`(comment)+` makes a maximal run of adjacent comments a single match. So for

```text
#include <a>
// one
// two
#include <b>
#include <c>
```

`[ (inc)+ (comment)+ ] @cap` reports **three** matches — `<a>` alone, the two
comments together, then `<b>` and `<c>` together — while `[ (inc)+ (comment) ]
@cap` reports four, splitting the comments. Neither reports one.

### 1.4 A run broken by an unmatched sibling

§1.1's last bullet applies even when the pattern holds no alternation at all. A
quantifier matched against a node's children matches a **maximal contiguous
run** of children satisfying it. A child that does not satisfy it ends the run,
and matching resumes at the next child that does — as a **new match**, not as a
continuation of the previous one.

Take the same grammar as §1.2 and the query

```text
(source_file (inc)+ @arg)
```

Against a file whose `inc` nodes are split into two runs by a `comment`:

```text
#include <foo>
#include <bar>
// break
#include <baz>
#include <qux>
```

`tree-sitter query` reports **two** matches, of two captures each. Reporting a
single match holding only the first run — silently dropping the second — is the
behaviour this milestone removes.

The number of matches is the number of runs, whatever their lengths:

| children, in order | matches |
| --- | --- |
| `inc inc comment inc inc` | 2, of 2 and 2 captures |
| `inc comment inc inc comment inc` | 3, of 1, 2 and 1 captures |
| `inc comment inc` | 2, of 1 capture each |

`*` behaves as `+` does here: on the first input above, `(source_file (inc)*
@arg)` also reports two matches of two captures each.

---

## 2. A supertype must not be a terminal

`supertypes` is declared in the grammar and, as milestone 01 states, affects
`src/node-types.json` and nothing else — the supertype gets an entry carrying
its `subtypes` and never appears in a tree. A supertype is therefore only
meaningful when it stands for a **choice between other rules**.

From this release, a rule listed in `supertypes` whose body is a single
terminal is **rejected**. A terminal here means a rule whose body is a regular
expression, a string literal, or a `token(...)` wrapping either — anything that
resolves to one token rather than to a choice of other rules.

The rejection is a `generate` failure in the ordinary sense of `20-cli.md` §0
and §2:

* the exit status is **1**;
* no parser is written — `src/parser.c` does not exist afterwards, exactly as
  for every other rejection listed in `10-grammar-dsl.md` §9;
* the diagnostic names the **offending rule**: the name the grammar author
  gave that rule appears somewhere in the message. Nothing else about the
  message is an interface — not its wording, not its order, and not what else
  it mentions.

All three of these are rejected, and each names the rule it is about:

```js
supertypes: $ => [$.termw],
rules: { source_file: $ => $.termw,             termw:    $ => /[a-z]+/ }

supertypes: $ => [$.kwtok],
rules: { source_file: $ => repeat(seq($.kwtok, ';')),
         kwtok:    $ => token(choice('let', 'var')) }

supertypes: $ => [$.wordterm],
rules: { source_file: $ => repeat($.wordterm),  wordterm: $ => /[a-z]+/ }
```

A supertype over a choice of other rules is unaffected and still generates:

```js
supertypes: $ => [$.item],
rules: { item: $ => choice($.decl, $.expr_item), /* ... */ }
```

---

## 3. `test --update` rewrites every corpus file

`40-corpus-tests.md` §5 states that `-u` rewrites only those files that
contained at least one failing test, and that a file whose every test passed is
left byte for byte as it was. **That restriction is removed, and so is §5's
80-character normalisation** — the paragraph beginning "The rewritten file is
normalised", together with the width of the rules in the example beneath it,
does not apply; §3.1 below replaces it. `-u` now rewrites **every** corpus file
it read, and within each file **every** test it parsed, whether or not anything
failed. A test the run did not parse — one a `:skip` attribute excluded, say —
has no rendering to write and is left as it was.

The exit status is still 0 and a corrected test is still printed with `✓`, as
§5 already describes.

### 3.1 What a rewrite preserves and what it replaces

A rewritten test keeps everything up to and including its divider line exactly
as it was, and replaces only the expected block:

| part of the test | after `-u` |
| --- | --- |
| the two rule-of-`=` header lines | **preserved**, at whatever width they were written |
| the test's name line | preserved |
| the input, and the blank lines around it | preserved verbatim |
| the rule-of-`-` divider line | **preserved**, at whatever width it was written |
| everything after the divider | **replaced**: one blank line, then the rendering below, then one blank line before the next test's header |

The separator between two tests is the one `40-corpus-tests.md` §5 already
fixes and this section does not change: exactly one blank line between the end
of a test's expected block and the next test's first `=` line. Whether the
file ends with a blank line is not an interface — `TEST_INTERFACE.md` drops
trailing blank lines before anything is compared.

So `-u` is not a reformatter for the header and the input — a file written with
five `=` keeps five `=`. What it normalises is the expectation.

### 3.2 The rendering written back

The expected block is replaced by the corpus-test rendering
(`30-tree-output.md` §5) of the tree the parser actually produced, laid out one
node per line with two spaces of indentation per level. A test whose
expectation was already correct but written on one line is therefore rewritten
across several. Given the input `let a;` and a correct flat expectation
`(source_file (item (id)))`, the file afterwards holds

```text
==================
flat expected tree
==================

let a;

---

(source_file
  (item
    (id)))
```

A test whose expectation was **wrong** is corrected to the same rendering, so
after `-u` a file that held one passing and one failing test is indistinguishable
from one that held two passing tests.

---

## 4. Generated parsers declare ABI 15

Milestone 03 raised the default emitted in `src/parser.c` from 13 to 14.
**The default is now 15.**

```c
#define LANGUAGE_VERSION 15
```

This is the version `generate` emits with no ABI option and **with no
`tree-sitter.json` present** in the repository, which is the case this
milestone changes: a repository that configures itself only through
`package.json` now gets ABI 15 as well, not the previous default.

Asking for 15 explicitly, with `generate --abi 15`, writes the same constant.
`--abi 14` keeps writing 14 in the same repository, as milestone 07 §1.3 already
fixes; this milestone changes the default, not the flag.

`07 §1.2` describes a fallback notice printed when a repository without a
`tree-sitter.json` was generated below ABI 15. That case no longer arises here,
because such a repository is no longer below 15, and with this milestone the
presence or absence of a notice on either stream is **not** an interface.

Nothing is dropped by this change: a parser this release generates is an ABI 15
parser, so it carries everything `07 §1.4` requires of one, whether or not a
`tree-sitter.json` was found.

---

## 5. `generate --abi` rejects a version outside the supported range

The versions this release can emit are **14** and **15** — the two rows
`07 §1.3` gives. **This replaces the accepted-values table of `02 §6.1`**, whose
`13` and `latest` rows are the previous release's; `latest` still means the
newest this release emits, so it still selects 15. Any other **version** is
outside the supported range; a value that is not a version at all is still out
of scope, exactly as `02 §6.1` leaves it.

An ABI version this release cannot emit is **rejected**, and rejected in the
ordinary way of `20-cli.md` §0:

* the exit status is **1** — not a crash, not a panic, and not a success;
* no parser is written; `src/parser.c` does not exist afterwards;
* the diagnostic's wording is not an interface.

This holds at both ends of the range. A version below what this release
supports and a version far above it — `--abi 13` and `--abi 99` — are both
rejected the same way. Neither silently produces a parser, and neither aborts
the process with a status other than 1.

---

## 6. `parse --cst --no-ranges`

`--no-ranges` (milestone 06) drops position annotations from the `parse`
rendering. `--cst` selects the line-per-node rendering whose line structure is
a range prefix, then indentation, then the node. Combining the two leaves the
question of what happens to the space the range prefix occupied, and this
milestone fixes the answer.

With `--no-ranges`, the CST rendering drops the range prefix **and the padding
that followed it**:

* the root node begins at **column 0**;
* each level is indented **two spaces** from its parent;
* a childless named node keeps its quoted text **on its own line**, separated
  from the node's kind by one space, exactly as it is when ranges are printed.

For a grammar whose `item` is `seq('let', $.id, ';')` and the input `let a;`:

```text
source_file
  item
    "let"
    id `a`
    ";"
```

and for the input `let alpha;\nlet beta;`:

```text
source_file
  item
    "let"
    id `alpha`
    ";"
  item
    "let"
    id `beta`
    ";"
```

Leaving the block indented as though the range prefix were still there, or
breaking a node's text onto a line of its own, is the behaviour this milestone
removes.
