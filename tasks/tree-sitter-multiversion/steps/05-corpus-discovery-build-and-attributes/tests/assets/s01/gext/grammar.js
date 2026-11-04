// A base grammar, and an extension of it built with the two-argument
// `grammar(baseGrammar, options)` form of `10-grammar-dsl.md` §1
// ("Extending a grammar").
//
//   * `source_file` is overridden, and its function takes the `($, original)`
//     second argument holding the base rule, so the base body is wrapped
//     rather than discarded;
//   * `boolean` is a rule the base does not have;
//   * `word`, `number` and `_item` are not mentioned and are inherited
//     unchanged;
//   * `extras` is not mentioned and is inherited from the base.
const base = grammar({
  name: 'gextbase',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($._item),
    _item: $ => choice($.word, $.number),
    word: $ => /[a-z]+/,
    number: $ => /[0-9]+/,
  }
});

module.exports = grammar(base, {
  name: 'gext',
  rules: {
    source_file: ($, original) => seq(optional($.boolean), original),
    boolean: $ => choice('true', 'false'),
  }
});
