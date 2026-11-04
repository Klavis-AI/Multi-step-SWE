# Milestone 04 — tree-sitter 0.21.0

This milestone carries the tool from release `0.20.9` to release **`0.21.0`**.

Everything the foundation documents (`00-overview.md` through `60-runtime.md`)
and every earlier milestone document still holds, **except** where a section
below says otherwise. Where a section changes an earlier statement it says so
explicitly and names what it replaces; nothing here silently overrides
anything.

Five areas change.

## 1. Capture precedence in the highlighter: the last pattern wins

A highlights query routinely captures the same node under more than one name.
The query engine reports all of them — `50-queries.md` §4 is unchanged by this
milestone, and `tree-sitter query` still prints every capture of every match in
both output modes. But a **highlighter** cannot print a node twice: it has to
choose one name per region of source, and this milestone changes which one it
chooses.

> **When several patterns of a `highlights` query capture the same node, the
> name that applies is the one from the pattern that appears *last* in the
> query file.**

Previously the *first* matching pattern won. The rule is now the exact reverse,
and it is a rule about **position in the file**, nothing else:

* it is not about specificity. A broad pattern placed after a narrow one
  overrides the narrow one, just as a narrow pattern placed after a broad one
  overrides the broad one;
* it is not about the order in which matches complete, nor about capture index,
  nor about which pattern a match came from having matched more nodes;
* it applies to every captured node, token nodes included — a keyword captured
  by an early pattern and again by a later one is highlighted under the later
  one's name.

Worked example. For the query

```text
(keyword) @keyword
(identifier) @variable
(declaration (identifier) @function)
```

and the source line

```text
let foo;
```

`foo` is highlighted as **`function`**: both `(identifier) @variable` and
`(declaration (identifier) @function)` capture it, and the second appears
later. Reverse the last two patterns —

```text
(keyword) @keyword
(declaration (identifier) @function)
(identifier) @variable
```

— and `foo` is highlighted as **`variable`**, because now the broad pattern is
the later one. With a third pattern `(identifier) @constant` appended to the
first query, `foo` becomes `constant`. Moving `(declaration (keyword) @operator)`
to the end of a query that already contains `(keyword) @keyword` makes `let`
an `operator`.

**This replaces `40-corpus-tests.md` §7.2**, which states the opposite rule —
that the name from the earliest pattern in the file applies — and marks it as
something a later milestone changes. This is that milestone. Nothing else in
`40-corpus-tests.md` §7 moves: the assertion syntax and the line an assertion
applies to (§7.1), and the `syntax highlighting:` group and its `✓ <file> (N
assertions)` lines (§7.3), are exactly as written there. Only *which name* an
assertion has to name changes.

Nothing else about the highlight layer changes either: `20-cli.md` §4's
description of what `highlight` prints, and how a language is found for a path,
are untouched. The choice is made wherever a highlighter assigns one name to a
node, so it is equally visible through `tree-sitter highlight` and through the
`test/highlight/` directory that `tree-sitter test` exercises — §5.1 below says
what `highlight` then does with the name it chose, and `40-corpus-tests.md` §7
says what `test` does with it.

## 2. Two new grammar-validation errors

`10-grammar-dsl.md` §9 lists the conditions under which `generate` writes a
diagnostic, exits **1**, and leaves no parser behind. Two conditions are added
to that list. Both behave exactly like the ones already there: the diagnostic
goes to stderr, its wording is not an interface, the exit status is 1, and no
`src/parser.c` is written.

### 2.1 An empty string in `extras`

`10-grammar-dsl.md` §5.3 describes `extras` as a list of tokens that may be
skipped at any token boundary. An **empty string literal** in that list is now
rejected:

```js
extras: $ => [/\s/, '']      // rejected from this release
```

An empty extra can be skipped an unbounded number of times without consuming
input, which is why it is no longer allowed. `extras: $ => []` remains legal
and still disables skipping entirely (§5.3); it is an *empty list* that is
fine, and an empty *string inside* the list that is not.

### 2.2 `token` and `token.immediate` take exactly one argument

`10-grammar-dsl.md` §4 writes both combinators as taking a single rule —
`token(rule)` and `token.immediate(rule)`. From this release that arity is
**checked**. Passing more than one argument to either is rejected:

```js
a: $ => token('p', 'q')                        // rejected
a: $ => seq('p', token.immediate('q', 'r'))    // rejected
```

A grammar that meant to join several pieces must say so, as `token(seq('p', 'q'))`.
The single-argument forms are unaffected and keep the meaning §4 gives them.

## 3. Selecting corpus tests by regular expression

`tree-sitter test` gains two options beside the `-f`/`--filter` that
`40-corpus-tests.md` §4 documents:

| option | effect |
| --- | --- |
| `--include <pattern>` | run only tests whose name matches the regular expression `<pattern>` |
| `--exclude <pattern>` | do not run tests whose name matches the regular expression `<pattern>` |

These are **regular expressions**, not substrings, and that is the whole point
of them: `^` and `$` anchor, `|` alternates, `(…)` groups. An unanchored
pattern is a search, so `--include beta` selects a test named `beta two`, while
`--include '^(alpha|gamma)'` selects tests named `alpha one` and `gamma three`
and nothing else.

A test the selection excludes is not run and not printed. Because the pattern
is a search rather than a whole-name match, `--include 'three$'` selects a test
whose name *ends* in `three`. A pattern that matches no test at all is not an
error: nothing runs, nothing is reported, and `test` exits **0** — the
exception `20-cli.md` §0 already names for a filter that selects no tests,
which this milestone does not move.

Everything else in `40-corpus-tests.md` §4 is unchanged: a passing run still
prints `✓ <name>` per test, and `-f`/`--filter` still exists and is still a
plain substring test. One bullet of §3 does move, and §3.1 says how; §3.1 also
names the one sentence of §4 that restates it.

### 3.1 A corpus file's group line is printed only when a test in it ran

`40-corpus-tests.md` §3's last bullet says that groups are printed even when
they contain no test that ran — "an empty group, or one whose every test the
filter excluded, still prints its name line". **This milestone replaces that
bullet, and with it the clause of §4's second paragraph that restates the same
rule — "Excluded tests are not run and not printed; their groups still are" —
for every group that comes from a corpus file.** The qualification below
applies to both places: §4's sentence is to be read with it.

> A group that comes from a **corpus file** prints its name line only when at
> least one test in that file ran. A group that comes from a **directory**
> prints its name line as before.

So a corpus file holding no tests contributes no line at all, and a selection
— by `--include`, by `--exclude` or by `-f`/`--filter` — that leaves a file
with nothing to run prints no line for that file. Nothing else in §3 moves:
the two-space indent per level, the `✓ ` and `✗ ` markers, the failure block
and every row of the exit-status table are exactly as written there.

## 4. Command-line surface

### 4.1 `--apply-all-captures` is not accepted

The previous release accepted an option `--apply-all-captures` on
`tree-sitter test`, which asked the highlight layer to report every capture of
a node rather than choosing one. §1 replaces that behaviour with a rule, so the
option is gone. `tree-sitter test --apply-all-captures` is an unrecognised
option: the invocation is rejected, **no corpus test runs**, nothing is
reported as passing or failing, and the exit status is non-zero — 2, by §4.3.

The same `test` invocation without the option is unaffected and still runs the
repository's corpus normally.

`tree-sitter highlight` accepted the same option at the previous release, and
no longer accepts it either: `highlight --apply-all-captures` is an
unrecognised option on exactly the terms above — nothing is highlighted and
the status is 2. `highlight --check`, introduced beside it, is **not**
affected and is still accepted.

### 4.2 `parse --edit` becomes `--edits`

`60-runtime.md` §3 documents the repeatable edit flag as `-e, --edit`, and
marks the name as something a later milestone changes. This is that milestone,
and it renames the flag **both there and in `20-cli.md` §3's `--edit`
subsection**, which describes the same flag. The flag is now spelled

```text
--edits <START> <DELETED> <TEXT>
```

with **no short form**, and `--edit` is no longer accepted. Everything else in
§3 stands unchanged: the value is still one argument holding three
whitespace-separated parts with the same meanings, edits are still applied in
the order written with a reparse after each, only the final tree is printed,
and the resulting tree must still be exactly the tree a fresh parse of the
edited text would give.

The path may be written before the flag, and that is the form the checks use:

```text
tree-sitter parse a.ext --edits '0 0 X'
```

### 4.3 Exit status 2 for an argument the tool does not recognise

`20-cli.md` §0 gives a two-value exit-status table and marks its last two rows
— the **usage** errors, as opposed to a subcommand that ran and failed — as
something a later milestone changes. **This is that milestone, and it replaces
those two rows and the two sentences of prose that restate them**: the
paragraph under the table ("...asking for a subcommand this release does not
have is a failure of the second kind, so it exits 1 rather than being silently
ignored") and the last sentence of §1 ("With no subcommand at all, the
executable writes usage information to stderr and exits 1"). Wherever those
sentences say `1` for a usage error, read `2`. There is now a third status,
used for exactly one kind of failure — the command line could not be
understood at all:

| situation | status |
| --- | --- |
| the subcommand did what it was asked | `0` |
| the subcommand rejected its input, or the work failed | `1` |
| no subcommand given, an unrecognised subcommand, or an unrecognised flag or option | `2` |

Status 2 means the invocation was never dispatched. A subcommand that *ran* and
then failed — a grammar it rejected, a file it could not find a language for, a
corpus test that did not pass — still exits **1**, exactly as §0 says. The two
exceptions §0 names are also unchanged: `highlight` and `tags` still exit 0
when no language can be found for a path, and `test` still exits 0 when a
filter selects no tests.

As everywhere else, the *wording* of the diagnostic is not an interface; the
status is.

## 5. `--config-path`

`20-cli.md` §6.1 says the tool reads its configuration from `config.json` in
`$TREE_SITTER_DIR`, or in `$HOME/.tree-sitter` when that variable is unset.
A new option names a configuration file directly:

```text
--config-path <path>
```

It is accepted by `parse`, `query`, `test`, `highlight` and `tags` — every
subcommand that needs to find a parser — and **not** by `generate`, which does
not (§6). The file it names is read **instead of** the one §6.1 describes, and
its contents are the same JSON object, with the same `parser-directories` and
`theme` keys.

A relative entry in that file's `parser-directories` is resolved against the
process's current working directory, so a configuration file containing

```json
{"parser-directories": ["."], "theme": {}}
```

makes every immediate `tree-sitter-*` subdirectory of the current directory a
candidate parser repository, exactly as §6.2 describes for any other configured
parser directory.

### 5.1 The theme becomes observable

`20-cli.md` §4 says that the `theme` of §6.1 is the only source of a theme, that
no flag supplies one on the command line, and that the mapping from capture
name to colour is therefore not observable — and marks that paragraph as
something a later milestone changes. **This is that milestone, and this section
replaces that paragraph.** `--config-path` *is* a flag an invocation controls,
and the file it names carries a `theme`, so the mapping is now determined by
the invocation and can be observed:

> A region the highlighter has assigned capture name *N* is rendered using the
> theme's entry for *N*.

The `theme` object maps a capture name to a colour. A colour may be written as
a string `#rrggbb`, and when it is, that is the form it appears in: in `--html`
output the span emitted for the region carries that colour written the same
way, in lower case. In terminal output it is that colour applied as an escape
sequence — which normalisation strips, so terminal output remains the source
text and nothing more.

Everything else in §4 stands. The surrounding markup of the HTML document is
still not compared, and which colour a theme chooses for a name is still
entirely the theme's business: what this section fixes is only that the name
looked up is the one the highlighter assigned. A capture name the theme does
not mention is rendered however the tool likes, and a theme entry that no
region was assigned is not emitted at all — the output carries the colours the
file's regions actually resolved to, and no others.

Together with §1 this makes last-wins visible through `highlight` itself. For
the first query and the source line of §1, and a `cfg.json` reading

```json
{"parser-directories": ["."],
 "theme": {"keyword": "#0000ff", "variable": "#ff00ff", "function": "#00ff00"}}
```

the invocation

```text
tree-sitter highlight --config-path cfg.json --html in.lang
```

renders `foo` in `#00ff00`, the theme's `function` colour, and `#ff00ff` does
not appear at all.

### 5.2 Selecting a language is unchanged

This does not change how a language is then selected. `20-cli.md` §6.3 still
governs: `--scope` first, then the extension claimed through `file-types`, then
— for `parse`, `test` and `query` only — the repository in the current
directory. So with the configuration above in `cfg.json`, and a repository
`tree-sitter-lang/` beside it whose `package.json` claims scope `source.lang`
and the extension `lang`:

* `tree-sitter parse --config-path cfg.json a.lang` finds the language by
  extension and prints the tree;
* `tree-sitter parse --config-path cfg.json --scope source.lang a.unclaimed`
  finds it by scope and prints the tree;
* `tree-sitter parse --config-path cfg.json a.unclaimed`, with no `--scope`,
  finds nothing and exits 1 — the current directory is not itself a parser
  repository, so §6.3 step 3 does not apply;
* `tree-sitter query --config-path cfg.json q.scm a.lang` compiles `q.scm`
  against that language and prints its captures in the form `50-queries.md`
  §2 and §3 fix.
