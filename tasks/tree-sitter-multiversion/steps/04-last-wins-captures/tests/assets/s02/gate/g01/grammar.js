module.exports = grammar({
  name: 'gx01',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($._stmt),
    _stmt: $ => seq($._expr, ';'),
    _expr: $ => choice($.num, $.ident, $.bin, $.paren),
    paren: $ => seq('(', $._expr, ')'),
    bin: $ => choice(
      prec.left(1, seq($._expr, '+', $._expr)),
      prec.left(2, seq($._expr, '*', $._expr))
    ),
    num: $ => /[0-9]+/,
    ident: $ => /[a-z]+/,
  }
});
