module.exports = grammar({
  name: 'sprec',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => $._expr,
    _expr: $ => choice($.id, $.add, $.pow, $.neg),
    add: $ => prec.left(1, seq($._expr, '-', $._expr)),
    pow: $ => prec.right(2, seq($._expr, '^', $._expr)),
    neg: $ => prec(3, seq('-', $._expr)),
    id: $ => /[a-z]+/,
  }
});
