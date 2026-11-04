module.exports = grammar({
  name: 'pex',
  extras: $ => [/\s/],
  conflicts: $ => [[$.call, $.group]],
  rules: {
    source_file: $ => $._expr,
    _expr: $ => choice($.id, $.sum, $.power, $.negate, $.call, $.group),
    sum: $ => prec.left(1, seq($._expr, '-', $._expr)),
    power: $ => prec.right(2, seq($._expr, '^', $._expr)),
    negate: $ => prec(3, seq('-', $._expr)),
    call: $ => prec.dynamic(2, seq($.id, '(', $._expr, ')')),
    group: $ => prec.dynamic(1, seq($.id, seq('(', $._expr, ')'))),
    id: $ => /[a-z]+/,
  }
});
