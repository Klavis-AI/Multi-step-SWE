module.exports = grammar({
  name: 'cstlang',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.binding),
    binding: $ => seq(
      'let',
      field('name', $.identifier),
      '=',
      field('value', $.number),
      ';'
    ),
    identifier: $ => /[a-z]+/,
    number: $ => /\d+/,
  }
});
