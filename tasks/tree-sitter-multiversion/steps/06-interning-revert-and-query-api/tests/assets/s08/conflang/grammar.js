module.exports = grammar({
  name: 'conflang',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => choice($.form_a, $.form_b),
    form_a: $ => seq($.identifier, ';'),
    form_b: $ => seq($.identifier, ';'),
    identifier: $ => /[a-z]+/,
  }
});
