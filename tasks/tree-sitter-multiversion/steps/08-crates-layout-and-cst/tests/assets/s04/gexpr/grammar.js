module.exports = grammar({
  name: 'gexpr',
  extras: $ => [/\s/],
  conflicts: $ => [[$.call, $.group]],
  rules: {
    source_file: $ => repeat($._stmt),
    _stmt: $ => seq($._expr, ';'),
    _expr: $ => choice($.num, $.ident, $.bin, $.paren, $.call, $.group),
    paren: $ => seq('(', $._expr, ')'),
    bin: $ => choice(
      prec.left(1, seq($._expr, '+', $._expr)),
      prec.left(2, seq($._expr, '*', $._expr))
    ),
    call:  $ => prec.dynamic(1, seq($.ident, '[', $.ident, ']')),
    group: $ => prec.dynamic(0, seq($.ident, seq('[', $.ident, ']'))),
    num: $ => /[0-9]+/,
    ident: $ => /[a-z]+/,
  }
});
