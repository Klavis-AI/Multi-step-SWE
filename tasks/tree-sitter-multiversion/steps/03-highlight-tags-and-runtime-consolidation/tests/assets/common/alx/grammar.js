module.exports = grammar({
  name: 'alx',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.expression),
    expression: $ => choice($.sum, $.product, $.number),
    sum: $ => prec.left(1, seq($.expression, '+', $.expression)),
    product: $ => prec.left(2, seq($.expression, '*', $.expression)),
    number: $ => /\d+/,
  }
});
