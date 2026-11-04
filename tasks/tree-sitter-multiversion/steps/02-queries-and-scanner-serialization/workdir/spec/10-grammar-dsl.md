# The grammar definition language

A grammar is a JavaScript file, conventionally `grammar.js`, that assigns the
result of calling `grammar()` to `module.exports`. The generator evaluates it
with `node`, with the DSL's functions in scope as globals, and takes the
exported object as the grammar.

```js
module.exports = grammar({
  name: 'example',
  extras: $ => [/\s/, $.comment],
  word: $ => $.identifier,
  rules: {
    source_file: $ => repeat($._statement),
    _statement: $ => choice($.assignment, $.call_expression),
    assignment: $ => seq(field('left', $.identifier), '=',
                         field('right', $._expression), ';'),
    identifier: $ => /[a-zA-Z_]\w*/,
    comment: $ => token(seq('//', /.*/)),
  }
});
```

The evaluation contract is the important part, because it is what makes the DSL
implementable without embedding a JavaScript interpreter of your own:

* the grammar file is evaluated by an external `node` process;
* the globals the file may use are exactly the names documented in this
  document — `grammar`, `seq`, `choice`, `repeat`, `repeat1`, `optional`,
  `prec`, `token`, `alias`, `field`, `sym`, `blank`;
* the result of `grammar()` is a plain JSON-serialisable object, the **grammar
  JSON** described in §10;
* a grammar may equivalently be supplied *as* that JSON. A file named
  `grammar.json` is read directly and no `node` process is involved. Only `.js`
  and `.json` grammar files are accepted; any other extension is rejected with
  exit status 1.

Because the DSL runs as ordinary JavaScript, a grammar file may compute rules
with loops, helper functions and array spreads. Nothing about that is special:
whatever the file exports is the grammar.

## 1. `grammar(options)`

`options` is an object. `name` and `rules` are required; everything else is
optional.

| key | type | meaning |
| --- | --- | --- |
| `name` | string | the language's name |
| `rules` | object | rule name → rule; property order is significant |
| `extras` | `$ => [rule, …]` | tokens allowed anywhere (§7.1) |
| `conflicts` | `$ => [[rule, …], …]` | ambiguities to resolve at parse time (§6.2) |
| `externals` | `$ => [rule, …]` | tokens supplied by an external scanner (§8) |
| `inline` | `$ => [rule, …]` | rules to substitute at their use sites (§7.4) |
| `supertypes` | `$ => [rule, …]` | hidden rules to record as abstract types (§7.5) |
| `precedences` | `$ => [[name, …], …]` | named precedence orderings (§6.1) |
| `word` | `$ => rule` | the token used for keyword extraction (§5.4) |

`name` must be a string matching `[a-zA-Z_]\w*` — it becomes part of C
identifiers in the generated parser, so a name containing a hyphen, a space or
a leading digit is rejected. `rules` must contain at least one rule. Each
violation is reported and the process exits 1.

The value of each optional function-valued key is called with the rule-reference
object `$` and must return the shape in the table. The functions exist so that a
grammar can refer to its own rules (`$.comment`) before they are defined.

### The start rule

The **first** property of `rules` is the start rule: the whole input must match
it, and the root of every tree is a node of its type. The start rule must be
visible, so a grammar whose first rule is named with a leading `_` is rejected
with exit status 1. Property order in `rules` has no other significance.

### Extending a grammar

`grammar(baseGrammar, options)` produces a new grammar from an existing one.
Rules in `options.rules` that share a name with a base rule replace them, and
such a rule's function receives a second argument holding the base rule, so it
can be wrapped rather than discarded:

```js
module.exports = grammar(base, {
  name: 'dialect',
  rules: { expression: ($, original) => choice(original, $.my_extension) }
});
```

Rules the new grammar does not mention are inherited unchanged. The other
options are inherited unless overridden.

## 2. Referring to rules

Inside a rule function, `$.name` is a reference to the rule `name`. Forward
references are fine; a reference to a rule that is neither defined in `rules`
nor listed in `externals` is an error and generation exits 1.

`sym('name')` is the same reference written as a string. It is useful when the
rule name is computed.

## 3. Terminals

### String literals

A JavaScript string is a token matching itself, exactly, byte for byte. It
produces an **anonymous** node whose type is the string's own text, so `';'`
produces a node of type `;`. The empty string is not a useful token and should
not be used.

### Regular expressions

A JavaScript regular expression literal is a token matching that pattern. Only
the pattern's *source* is used:

> **Regex flags are accepted and silently ignored.** `/abc/i` is compiled
> exactly as `/abc/` and does not match `ABC`. This is not an error and no
> diagnostic is produced.

The generator compiles patterns into a deterministic lexer, so the supported
subset is the subset a finite automaton can express:

| construct | status |
| --- | --- |
| literal characters, including non-ASCII | supported |
| `.` | supported — any character except `\n` |
| `[...]`, `[^...]`, ranges `a-z` | supported |
| `\d` `\D` `\s` `\S` `\w` `\W` | supported |
| `\p{...}` Unicode property classes | supported |
| `\n` `\r` `\t` `\0` `\xNN` `\uNNNN` and escaped metacharacters | supported |
| `*` `+` `?` `{n}` `{n,}` `{n,m}` | supported |
| `(...)`, `(?:...)`, `\|` | supported |
| `^` `$` `\b` `\B` and other anchors | **rejected** |
| `(?=...)` `(?!...)` lookaround | **rejected** |
| `\1` backreferences | **rejected** |
| set operations inside a class, e.g. `[a-z&&[^aeiou]]` | **rejected** |

A rejected construct aborts generation with exit status 1.

The Perl classes expand to these exact sets: `\d` is `[0-9]`; `\w` is
`[_A-Za-z0-9]`; `\s` is space, `\t`, `\r` and `\n`. Their negations are the
complements.

Two further details:

* Non-greedy markers parse but have no effect. The lexer always takes the
  longest match, so `/a+?b/` matches all of `aaab`.
* Escaping a character that does not need escaping is accepted for some
  punctuation and **rejected** for the rest, with no rule worth memorising:
  `\!`, `\/`, `\-`, `\#`, `\~` and `\&` generate, while `\%`, `\@`, `\<`,
  `\=`, `\:`, `\,`, `\;`, `\_` and `\ ` are errors. Do not escape a
  character that does not need it; a grammar that avoids redundant escapes
  entirely is unaffected by any of this.

  > Which characters fall on which side is the behaviour of the
  > first milestone; a later milestone changes it.

A regular expression that is not bound to a rule name of its own produces an
anonymous node whose type is the pattern's source text. Grammars therefore
normally give every pattern a named rule.

## 4. Rule combinators

Each of these returns a rule, and rules nest freely.

### `seq(a, b, …)`

Matches its arguments in order. `seq()` with no arguments is `blank()`.

### `choice(a, b, …)`

Matches exactly one of its arguments. Order does not express preference —
ambiguity between alternatives is resolved by the rules in §6, not by
position. `choice()` with no arguments matches nothing and makes the enclosing
rule unmatchable.

### `repeat(rule)`

Matches `rule` zero or more times. Equivalent to `choice(repeat1(rule),
blank())`.

### `repeat1(rule)`

Matches `rule` one or more times.

Repetition does not introduce a level of nesting in the tree. `repeat($.item)`
matching three items yields three `item` children of the enclosing node, not a
nested list. The generator does create an internal helper rule for each
repetition — it appears in the generated C as an `aux_sym_…_repeat1` symbol —
but that symbol is hidden and never reaches the tree.

### `optional(rule)`

Matches `rule` or nothing. Equivalent to `choice(rule, blank())`.

### `blank()`

Matches the empty string. Rarely written directly; it is what `optional` and
`repeat` are built from.

### `token(rule)`

Compiles the whole of `rule` into a **single terminal**. Everything inside
becomes part of one atomic lexer match, so:

* no `extras` may appear inside it — `token(seq('//', /.*/))` cannot have
  whitespace between the `//` and the rest;
* the internal structure is erased. The result is one leaf node, with no
  children, however many literals `rule` contained;
* only lexical constructs may appear inside: literals, patterns, and the
  combinators over them. A reference to another rule is an error **whatever
  that rule contains** -- `token($.y)` is rejected even when `y` is itself
  purely lexical.

This is how comments and multi-part literals are written.

### `token.immediate(rule)`

Like `token(rule)`, but the token matches only when it begins at the byte where
the previous token ended — that is, only when no `extras` were skipped
immediately before it. It is how a grammar distinguishes `#foo` from `# foo`:

```js
tagged: $ => seq('#', token.immediate(/[a-z]+/)),
```

With this rule, `#foo` is a `tagged` node and `# foo` is not a `tagged` node at
all; the parser fails to match and error recovery takes over.

### `prec(number, rule)` and friends

Five forms, all covered in §6:

| form | effect |
| --- | --- |
| `prec(p, rule)` | precedence `p`, no associativity |
| `prec.left(rule)` / `prec.left(p, rule)` | left-associative; `p` defaults to 0 |
| `prec.right(rule)` / `prec.right(p, rule)` | right-associative; `p` defaults to 0 |
| `prec.dynamic(p, rule)` | precedence applied at parse time, not generation time |

`p` is an integer or a string naming an entry in `precedences`. Any other type
— a float, for instance — is rejected and generation exits 1.

### `field(name, rule)`

Labels the node matched by `rule` with the field name `name`, as seen from the
enclosing node. `name` is a string. Fields appear in the printed tree
(`30-tree-output.md` §3), in `src/node-types.json`, and are matchable by
queries (`50-queries.md` §2.4).

A field labels *one* child position. `field('arg', repeat($.expr))` labels
every `expr` matched by the repetition with `arg`, which is the idiomatic way
to express a list of arguments.

### `alias(rule, name)`

Makes the node matched by `rule` appear under a different type in this position
only. The node's extent and children are unchanged.

| written as | result |
| --- | --- |
| `alias($.inner, $.renamed)` | a **named** node of type `renamed` |
| `alias($.inner, 'LITERAL')` | an **anonymous** node of type `LITERAL` |

The distinction matters: only the first form appears in the printed tree, and
only the second is matched by a quoted query pattern. An alias may also be
applied to a hidden rule, which is the standard way to make a helper rule
visible under a name of the caller's choosing.

## 5. Lexical analysis

### 5.1 Longest match

At each position the lexer considers every token the parser could accept in its
current state and takes the **longest** match. Length is compared in bytes.

### 5.2 Precedence between tokens of equal length

When two acceptable tokens match the same number of bytes, lexical precedence
decides. Each token has one, derived as follows:

| token | precedence |
| --- | --- |
| a string literal | 2 |
| a regular expression | 0 |
| anything inside `token(...)` or `token.immediate(...)` | 0, unless set explicitly |
| `token(prec(n, …))` or `prec(n, …)` applied to a terminal | `n` |
| `token.immediate(...)` | one more than it would otherwise have |

The higher precedence wins. This is why a keyword written as the literal
`'if'` beats an `identifier` pattern that also matches `if`: equal length,
precedence 2 against 0.

If length and precedence are both equal, the conflict is genuine and generation
fails with exit status 1.

### 5.3 `extras`

`extras` lists tokens that may appear between any two other tokens without
appearing in any rule. The default, when the key is omitted, is whitespace:
the single pattern `/\s/`.

Consequences to implement exactly:

* an extra may be skipped at any token boundary, any number of times;
* an extra never influences which production is chosen — it cannot disambiguate
  and cannot be required;
* an extra that produces a **visible** node appears in the tree, as a child of
  whichever node's extent contains it. A grammar that lists `$.comment` in
  `extras` gets comment nodes in its trees at their natural positions;
* an extra that produces no visible node — the `/\s/` pattern, typically —
  leaves no trace except in the byte ranges of its neighbours;
* extras are not skipped before a `token.immediate`, which is exactly what that
  combinator means.

Supplying `extras: $ => []` disables skipping entirely, so every byte of the
input must be accounted for by a rule.

### 5.4 `word`

`word` names a single token — in practice the identifier token — and enables
**keyword extraction**. Without it, a keyword literal is lexed as a keyword
wherever a keyword is acceptable, even when it is the prefix of a longer word.
With it, the lexer first matches the word token, then checks whether the text
it matched is *exactly* equal to a keyword literal, and only then treats it as
that keyword.

The difference is observable. With

```js
rules: { source_file: $ => seq('if', $.identifier, ';'),
         identifier:  $ => /[a-zA-Z_]+/ }
```

the input `iffy;` **parses successfully** without `word` — the lexer takes `if`
as the keyword and `fy` as the identifier — and **fails** with `word: $ =>
$.identifier`, because `iffy` is lexed whole, is not the keyword `if`, and an
identifier is not acceptable at that position.

Beyond correctness, keyword extraction is what keeps the generated lexer small
for languages with many keywords: one automaton recognises the word token and a
lookup decides which keyword, if any, it is.

## 6. Ambiguity

An LR parse table can reach a state in which two actions are possible: shift
the next token, or reduce a completed production (a **shift/reduce** conflict);
or reduce either of two completed productions (a **reduce/reduce** conflict).
The generator must resolve every such conflict or refuse to produce a parser.

### 6.1 Precedence and associativity

Every production carries a precedence and an associativity, taken from the
innermost `prec*` wrapper enclosing it. The resolution procedure is:

1. **Compare precedences.** If the two are comparable and unequal, the higher
   wins: in a shift/reduce conflict a higher-precedence shift shifts and a
   higher-precedence reduction reduces; in a reduce/reduce conflict the
   higher-precedence production is reduced.
2. **Otherwise consult associativity** — the associativity of the production
   that would be *reduced*. Left-associative means reduce; right-associative
   means shift.
3. **Otherwise the conflict is unresolved.** Generation fails with exit status
   1 and reports the symbol sequence at which the conflict arose, unless the
   conflict is declared (§6.2).

Step 2 applies both when the precedences are equal *and* when they are not
comparable, and the difference is observable:

* `prec.left(1, …)` for `+` and `prec.left(2, …)` for `*` gives `1 + (2 * 3)`;
* `prec.left` on both with equal precedence gives `(1 + 2) * 3`, because the
  left-associative reduction wins;
* `prec.right` on both with equal precedence gives `1 + (2 * 3)`;
* `prec(1, …)` on both — precedence but no associativity — is an **unresolved
  conflict** and generation fails.

Two precedences are comparable when they are both integers, or when they are
both names appearing in the *same* entry of `precedences`. A name is **not**
comparable with an integer, and two names from different entries are not
comparable with each other; in both cases resolution falls through to step 2.

**Named precedences.** `precedences` is a list of orderings, each ordering a
list of names **from highest to lowest**:

```js
precedences: $ => [ ['unary', 'multiplicative', 'additive'] ],
```

Written this way, `prec.left('additive', …)` and
`prec.left('multiplicative', …)` compare as integers 1 and 2 would, with
`multiplicative` the higher. Reversing the list reverses the parse: with
`[['add', 'mul']]` the input `1+2*3` yields `(1+2)*3`, and with
`[['mul', 'add']]` it yields `1+(2*3)`.

### 6.2 `conflicts` and the GLR split

Some ambiguities cannot be resolved with a fixed lookahead of one token because
the deciding evidence is arbitrarily far to the right. For those, list the
conflicting rules in `conflicts` and the generator emits a **parse-time split**
instead of failing:

```js
conflicts: $ => [ [$.a, $.x_thing] ],
```

Each entry is an array of the rules that may be in progress simultaneously.
When the parser reaches the conflicting state it forks: it pursues every listed
interpretation in parallel, in lock-step, and discards a branch as soon as the
input rules it out. If exactly one branch survives, its tree is the result. The
declaration does not tell the parser *which* interpretation to prefer; it tells
the generator that more than one is legitimate and must be kept alive.

With the grammar

```js
rules: { source_file: $ => choice($.a, $.b),
         a:        $ => seq('x', 'y'),
         b:        $ => seq($.x_thing, 'y'),
         x_thing:  $ => 'x' }
```

generation **fails** without the `conflicts` declaration, because on seeing `x`
the parser cannot know whether to reduce it to `x_thing`. With the declaration,
generation succeeds and `xy` parses.

Declaring a conflict costs work at parse time, and declaring one that is not
needed hides real grammar problems, so a `conflicts` entry is a deliberate
statement, not a way to silence a diagnostic.

### 6.3 `prec.dynamic`

When a split leaves **more than one** surviving interpretation of the same
input, dynamic precedence chooses. `prec.dynamic(n, rule)` attaches `n` to a
production; among surviving branches the one whose productions carry the
greatest total dynamic precedence wins.

Continuing the example above, adding `prec.dynamic(-1, …)` to rule `a` changes
the tree `xy` produces from

```text
(source_file (a))
```

to

```text
(source_file (b (x_thing)))
```

Dynamic precedence is inert unless a split actually occurs: it plays no part in
building the table, and it does not resolve a conflict that `conflicts` has not
declared.

## 7. Controlling the shape of the tree

### 7.1 Hidden rules

A rule whose name begins with `_` produces no node. Its children attach to its
parent, at its position. Hidden rules are how a grammar factors out an
alternation — `_expression`, `_statement` — without adding a layer to every
tree. Hiding is purely about the tree: a hidden rule is an ordinary
nonterminal for every other purpose, including precedence and conflicts.

### 7.2 Fields

See §4, `field`. Fields are the stable way for a consumer to address a
particular child, and `src/node-types.json` records, for each node type, which
fields it can have, whether each is required, whether it can repeat, and which
node types can fill it.

### 7.3 Aliases

See §4, `alias`.

### 7.4 `inline`

Rules listed in `inline` are substituted into every rule that references them
and then removed from the grammar. They are not merely hidden — they cease to
exist as symbols, and the generated parser contains no symbol for them at all,
whereas a hidden rule does have one.

Inlining is a table-construction tool. Because each use site gets its own copy
of the rule's body, a conflict that arises from two different contexts sharing
one symbol can disappear when that symbol is inlined. The cost is table size.

### 7.5 `supertypes`

`supertypes` lists hidden rules that should be recorded as abstract categories.
The effect is confined to `src/node-types.json`: a supertype gets its own entry
carrying a `subtypes` list of its alternatives, and every position that accepts
it is described as accepting the supertype rather than as accepting each
alternative separately.

```json
{ "type": "_expression", "named": true,
  "subtypes": [ { "type": "identifier", "named": true },
                { "type": "number", "named": true } ] }
```

Declaring a supertype does not make it visible; it remains hidden and does not
appear in any tree. It stays a symbol in the generated parser.

## 8. `externals`

`externals` declares tokens that the generated lexer cannot recognise, to be
produced instead by a hand-written scanner compiled alongside the parser:

```js
externals: $ => [$.raw_text, $._eof_marker],
```

Rules named here need no definition in `rules`. A name may appear in both, and
that is accepted rather than rejected. The `_` convention applies as usual, so
`$._eof_marker` is a hidden external token.

The declaration has two effects on generation. It reserves a symbol for each
external token, and it records, for each parse state, which external tokens are
acceptable there — the scanner is told this on every call. `generate` succeeds
whether or not a scanner exists; the scanner is only needed when the parser is
compiled and run. `60-runtime.md` §4 specifies the scanner's interface and
`20-cli.md` §6 how it is compiled in.

## 9. Diagnostics and rejection

Every condition below causes `generate` to write a diagnostic and exit with
status 1, leaving no parser behind:

* `name` missing, not a string, or not matching `[a-zA-Z_]\w*`;
* `rules` missing or empty;
* a start rule that is not visible;
* a reference to an undefined rule;
* a precedence value that is neither an integer nor a string;
* a regular expression using an unsupported construct (§3);
* two tokens that are indistinguishable by length and precedence (§5.2);
* an unresolved LR conflict that `conflicts` does not declare (§6.1);
* a `token()` that is not purely lexical -- in particular one containing a
  reference to another rule, whatever that rule contains (§4);
* a supertype whose alternatives do not each have a single visible child.

The *wording* of any of these diagnostics is not an interface and nothing
depends on it. What is fixed is that the input is rejected and that the exit
status is 1.

## 10. The grammar JSON

`generate` normalises the grammar to JSON and, under the conditions in
`20-cli.md` §2, writes it to `src/grammar.json`. It is also the form in which a
grammar may be supplied directly. The top-level object has these keys, in this
order:

```text
name  word  rules  extras  conflicts  precedences  externals  inline  supertypes
```

`word` is present only when the grammar sets it; the other eight keys are
always present, with an empty array or object when the grammar did not supply
them. `extras` defaults to a single `PATTERN` of `\s`, not to an empty
array — omitting the key and passing `[]` are different grammars.

`rules` maps each rule name to a rule object; `extras`, `externals` and
`precedences` hold rule objects and name lists; `conflicts`, `inline` and
`supertypes` hold arrays of rule-name strings. Every rule object has a `type`
and the fields that type implies:

| `type` | fields | from |
| --- | --- | --- |
| `BLANK` | — | `blank()` |
| `STRING` | `value` | a string literal |
| `PATTERN` | `value` | a regular expression |
| `SYMBOL` | `name` | `$.rule` or `sym('rule')` |
| `SEQ` | `members` | `seq(…)` |
| `CHOICE` | `members` | `choice(…)`, `optional(…)` |
| `REPEAT` | `content` | `repeat(…)` |
| `REPEAT1` | `content` | `repeat1(…)` |
| `TOKEN` | `content` | `token(…)` |
| `IMMEDIATE_TOKEN` | `content` | `token.immediate(…)` |
| `FIELD` | `name`, `content` | `field(…)` |
| `ALIAS` | `content`, `named`, `value` | `alias(…)` |
| `PREC` | `value`, `content` | `prec(…)` |
| `PREC_LEFT` | `value`, `content` | `prec.left(…)` |
| `PREC_RIGHT` | `value`, `content` | `prec.right(…)` |
| `PREC_DYNAMIC` | `value`, `content` | `prec.dynamic(…)` |

`optional(x)` normalises to `{"type": "CHOICE", "members": [x, {"type":
"BLANK"}]}` — the equivalence in §4 is realised literally. `repeat` and
`repeat1`, by contrast, survive as their own types rather than being expanded.
A `value` on a `PREC*` object is either a number or a string, matching whatever
the grammar wrote.

The file is a generated artifact and its formatting is not compared, but its
*content* is: it is the record of what the grammar meant, and it is how a
parser's name is discovered when the parser is later loaded
(`20-cli.md` §6).
