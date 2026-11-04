# Milestone 06 — `tree-sitter` 0.24.0

This document states only what **this** milestone changes. Everything not
mentioned here keeps the behaviour the foundation documents (`00-overview.md`
through `60-runtime.md`) and the earlier milestone documents already give, and
where this document overturns one of them it says so explicitly.

Five things change:

1. `generate` reports single-element `seq` and `choice` rules;
2. a hidden rule whose whole body is a single string literal keeps its own
   token, and `src/node-types.json` marks the start rule;
3. a parser repository configures itself through `tree-sitter.json`, and
   `generate` stops writing language bindings;
4. five command-line options change across `test`, `build` and `parse`, and
   the `build-wasm` subcommand is removed;
5. `parse` can print a tree with no position annotations.

---

## 1. `generate` reports single-element `seq` and `choice` rules

`seq(x)` and `choice(x)` with exactly one argument mean the same thing as `x`
on its own (`10-grammar-dsl.md` §4). The wrapper is dead weight, and from this
release `generate` says so.

For every rule whose body is a `seq(...)` or a `choice(...)` holding
**exactly one** element, where that element is a **string literal** or a
**regular expression**, `generate` writes one diagnostic **to stderr** naming
that rule.

This is a warning, not a rejection. Generation completes, the parser is written
exactly as it would have been without the diagnostic, and the exit status is
still `0`. The *wording* of the diagnostic is not an interface
(`20-cli.md` §0); what is fixed is that it goes to stderr and that each
offending rule's name appears in the output. Whether the names are collected
into one diagnostic or emitted one to a line is not observed at this boundary,
and nothing may depend on it.

The condition is narrow, and both halves of it matter:

| rule body | reported |
| --- | --- |
| `seq('aa')` | yes — one element, a string literal |
| `choice('bb')` | yes |
| `seq(/[A-Z]+/)` | yes — one element, a regular expression |
| `choice(/[0-9]+/)` | yes |
| `seq($.id)` | **no** — the single element is a rule reference |
| `seq('a', 'b')` | no — two elements |
| `repeat1('rr')` | no — not a `seq` or a `choice` |

A rule's visibility is irrelevant: a hidden rule (`10-grammar-dsl.md` §7.1) is
reported on exactly the same terms as a visible one.

---

## 2. A hidden rule whose body is a single string literal keeps its token

This is the milestone's central change and it is worth being precise about,
because it is invisible in the printed tree and visible everywhere else.

Consider

```js
rules: {
  source_file: $ => repeat($.item),
  item:   $ => seq($._kw, $.id, $._count, ';'),
  _kw:    $ => 'let',
  _count: $ => /[0-9]+/,
  id:     $ => /[a-z]+/,
}
```

`_kw` and `_count` are hidden rules whose entire body is a single terminal.
Previously such a rule was folded away: the terminal took on the hidden rule's
identity, so it produced no node type of its own and nothing in the tree or in
a query could name it.

**From this release that folding no longer happens for a string literal.** A
hidden rule whose body is a single string literal contributes an ordinary
**anonymous** token named by the literal's own text, exactly as though the
literal had been written inline at the reference site. So `_kw` above yields
the anonymous node type `let`.

Three consequences, all observable:

* **`src/node-types.json` gains the entry.** For the grammar above it contains
  `{ "type": "let", "named": false }`, alongside the entry for the `;` written
  inline in `item`. Each entry's keys appear in the order shown — `"type"`
  first, then `"named"` — so a reader comparing the file as text sees that
  adjacency.
* **Queries can match it** (`50-queries.md` §1.1), with a quoted pattern,
  anywhere a quoted pattern is legal: nested under its parent,

  ```text
  (item "let" @kw)
  ```

  at the top level of a query,

  ```text
  "let" @kw
  ```

  and alongside a named sibling in one pattern,

  ```text
  (item "let" @kw (id) @name)
  ```

  Each compiles and reports its capture in the usual way.
* **The printed tree is unchanged.** Anonymous nodes are not printed
  (`30-tree-output.md` §1), so `parse` produces the same text as before and
  `(item (id) @name)` still captures what it always did.

### 2.1 The boundary: a regular expression is still folded away

The change applies to a **string literal** only. A hidden rule whose body is a
single **regular expression** is still absorbed into its referrer and still
contributes no node type. In the grammar above:

* `src/node-types.json` has **no** entry whose `"type"` is `[0-9]+`;
* the query `(item "[0-9]+" @n)` names a node type the grammar does not have,
  so it is a compile error and `query` exits `1` (`50-queries.md` §1.10).

Getting this half wrong is as costly as getting the other half wrong. The two
cases differ only in which kind of terminal the hidden rule wraps.

### 2.2 `src/node-types.json` marks the start rule

The entry for the grammar's **start rule** (`10-grammar-dsl.md` §1) now carries
an additional key:

```json
{ "type": "source_file", "named": true, "root": true, "fields": { … } }
```

Exactly one entry in the file carries `"root": true` — the entry whose
`"type"` is the start rule `source_file` — and no other entry gains a `"root"`
key at all, `false` or otherwise. The key sits where the example above puts
it: inside that entry's own object, after `"named"` and before any nested
object such as `"fields"` or `"children"`. Everything else about the file is
as before.

---

## 3. `tree-sitter.json`, and `generate` stops writing bindings

A parser repository describes itself to the tool through a **`tree-sitter.json`**
file at its root, replacing the `"tree-sitter"` key of `package.json`
(`20-cli.md` §6.2).

### 3.1 Shape

```json
{
  "grammars": [
    { "name": "alxmigrate",
      "camelcase": "Alxmigrate",
      "scope": "source.alxmigrate",
      "path": ".",
      "file-types": ["alx"] }
  ],
  "metadata": { "version": "1.2.3" },
  "bindings": { "c": true, "go": false, "node": false,
                "python": false, "rust": false, "swift": false }
}
```

`grammars` is the array that `"tree-sitter"` used to hold and its entries carry
the same keys, with the same meanings, as `20-cli.md` §6.2 gives — `path`,
`scope`, `file-types`, `content-regex`, `first-line-regex`, `injection-regex`,
`highlights`, `injections`, `locals`, `tags` — plus `name`, the grammar's own
name, and `camelcase`. `path` still defaults to `"."`.

`metadata` and `bindings` are additional top-level objects. The file may carry
further keys beyond those shown; nothing here depends on them.

### 3.2 Migration out of `package.json`

When `generate` runs in a repository whose `package.json` carries a
`"tree-sitter"` key and which has no `tree-sitter.json`, it migrates the
configuration:

* it writes `tree-sitter.json`, carrying each configuration from the
  `"tree-sitter"` array across into `grammars`, filling in `name` from the
  grammar's name, `camelcase` from that name in UpperCamelCase (`alxmigrate`
  becomes `Alxmigrate`, the form §3.1's example shows) and `path` from its
  default where the old entry did not give one, and setting `metadata.version`
  from `package.json`'s `"version"`;
* it **removes the `"tree-sitter"` key from `package.json`**, leaving that file
  otherwise intact — `"name"`, `"version"` and every other key it held survive
  unchanged;
* it reports the migration on **stdout**. As with the diagnostic of §1, the
  wording is not an interface — but the report is not entirely prose: it
  **names the configuration file it wrote**, `tree-sitter.json`, and that
  filename is a documented artefact (§3.1), not a choice of phrasing. What is
  fixed is that the report goes to stdout and that the name `tree-sitter.json`
  appears in it; nothing else about the wording is an interface.

### 3.3 `generate` no longer writes bindings — this overturns `20-cli.md` §2

`generate` writes the `src/` directory and `tree-sitter.json`, and nothing
else. It writes **no** language-binding scaffolding of any kind: no
`binding.gyp`, no `Cargo.toml`, no `bindings/` tree in any language.

It also **no longer touches `package.json` except to remove the
`"tree-sitter"` key**. In particular it adds no `"main"` key — overturning the
last paragraph of `20-cli.md` §2 — and it does **not** create a `package.json`
when none exists.

What `generate` writes under `src/` is unchanged, and so are the conditions on
it, including the rule that `src/grammar.json` is written only when no
`grammar-path` argument was given (`20-cli.md` §2).

### 3.4 `tree-sitter.json` alone is a complete configuration

A repository holding a grammar and a `tree-sitter.json` and **no**
`package.json` is a well-formed parser repository. `generate` succeeds in it
and creates no `package.json`; `parse` run inside it finds the parser through
the working directory (`20-cli.md` §6.3, step 3), compiles it (§6.4) and prints
the tree in the ordinary way.

---

## 4. Command-line surface

| subcommand | change |
| --- | --- |
| `test` | `-f`, `--filter` is **removed** |
| `test` | `--overview-only` is **added** |
| `build` | `--internal-build` is **removed** |
| `build` | `-0`, `--debug` is **added** |
| `parse` | `-r`, `--rebuild` is **added** |
| — | the `build-wasm` subcommand is **removed** |

**`test --filter`.** The option no longer exists. Passing it is an
unrecognised option and the invocation fails with exit status **2** — the
status `20-cli.md` §0 marks as superseded by `04-last-wins-captures.md` §4.3,
which is the status that now applies: no corpus test runs, none is reported as
passing or failing. The check asserts only that the exit status is non-zero and
that nothing is reported as passing or failing, so either reading is accepted.
This overturns `40-corpus-tests.md` §4. Selecting a subset of the corpus is
what `--include` and `--exclude` are for, and they are unaffected by this
milestone.

**`test --overview-only`.** Prints the group-and-test overview tree of
`40-corpus-tests.md` §3 — the group lines and the `✓`/`✗` line for each test
that ran — and suppresses the failure detail that would otherwise follow it.
Nothing else changes: the same tests run, they are reported the same way, and
the exit status is exactly what it would have been without the option.

**`build --internal-build`.** The option no longer exists; passing it fails
the invocation and compiles nothing.

**`build --debug`.** Compiles the parser in debug mode. It is otherwise an
ordinary successful `build` — the parser is compiled and the exit status is
`0`.

**`build-wasm`.** The subcommand is gone. `20-cli.md` §7 lists it as one of
the subcommands that exist but are not exercised before milestone 6, and
records that at milestone 6 it is invoked once, only to confirm it is not a
subcommand. This release is that milestone: `tree-sitter build-wasm` is
simply an unrecognised subcommand, with the exit status milestone 04 fixed
for that case. `web-ui` and `init-config` are unaffected.

**`parse --rebuild`.** Recompiles the parser before parsing, instead of reusing
the cached shared library that `20-cli.md` §6.4 describes. The parse itself is
unaffected: the tree printed and the exit status are the same as without the
option.

---

## 5. `parse --no-ranges`

`--no-ranges` prints the tree with every position annotation omitted.
**Only** the annotation is dropped. Everything else in the `parse` rendering
of `30-tree-output.md` §1 is exactly as it is without the option: named nodes
only, one node per line, two spaces of indentation per level of printed depth,
field prefixes written `name: ` before the opening parenthesis, aliases in
place of the aliased type, closing parentheses appended to the last child's
line, and a trailing newline.

A node line is therefore `(`, the node's type, and — if it has children — the
children; a leaf is `(type)`.

For an expression grammar:

```text
(source_file
  (bin
    (num)
    (bin
      (num)
      (ident)))
  (bin
    (paren
      (bin
        (ident)
        (ident)))
    (num)))
```

`ERROR` nodes print as named nodes here too (`30-tree-output.md` §3), so a file
that recovers from junk gives

```text
(source_file
  (item
    (id))
  (ERROR
    (ERROR))
  (item
    (id)))
```

and fields survive, including on aliased children:

```text
(source_file
  (decl
    name: (id))
  (decl
    name: (varname))
  (keyword))
```

The exit status is unchanged by the option: `0` when no tree contains an
`ERROR` or a `MISSING`, `1` when one does (`20-cli.md` §3). The second example
above exits `1`.
