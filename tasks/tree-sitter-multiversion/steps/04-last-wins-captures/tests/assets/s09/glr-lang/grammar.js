module.exports = grammar({
  name: 'sglr',
  extras: $ => [/\s/],
  conflicts: $ => [[$.call, $.paren_expr]],
  rules: {
    source_file: $ => repeat($._item),
    _item: $ => choice($.call, $.paren_expr),
    call: $ => prec.dynamic(1, seq($.id, '(', $.id, ')')),
    paren_expr: $ => prec.dynamic(0, seq($.id, seq('(', $.id, ')'))),
    id: $ => /[a-z]+/,
  }
});
