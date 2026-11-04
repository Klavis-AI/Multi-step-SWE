// `sym()` — `10-grammar-dsl.md` §2 — and `blank()` — §4.
//
//   * the start rule reaches every other rule through `sym()`, with each name
//     held in a JavaScript constant, rather than through `$.name`; the two
//     spellings must produce the same rule reference and the same tree, which
//     is what makes the computed-name case in §2 work at all;
//   * the same rule is reachable by both spellings, so a name resolved by
//     `sym()` cannot be a distinct anonymous rule.
const WORD = 'word';
const TAIL = 'tail';

module.exports = grammar({
  name: 'alt',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => choice(repeat1($.pair), blank()),
    pair: $ => seq(sym(WORD), sym(TAIL), ';'),
    tail: $ => /[0-9]+/,
    word: $ => /[a-z]+/,
  }
});
