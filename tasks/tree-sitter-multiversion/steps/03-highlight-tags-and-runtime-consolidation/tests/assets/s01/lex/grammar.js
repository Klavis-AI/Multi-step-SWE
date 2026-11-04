module.exports = grammar({
  name: 'lex',
  word: $ => $.identifier,
  extras: $ => [/\s/, $.comment],
  rules: {
    source_file: $ => repeat($._item),
    _item: $ => choice($.declaration, $.label, $.text),
    declaration: $ => seq('let', $.identifier, ';'),
    label: $ => seq($.identifier, token.immediate(':'), $.identifier, ';'),
    text: $ => seq('say', $.letters, ';'),
    letters: $ => /\p{L}+/,
    identifier: $ => /[a-z_][a-z_0-9]*/,
    comment: $ => token(seq('#', /[^\n]*/)),
  }
});
