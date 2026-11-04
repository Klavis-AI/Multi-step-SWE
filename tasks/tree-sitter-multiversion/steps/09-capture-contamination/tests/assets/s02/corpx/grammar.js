module.exports = grammar({
  name: 'corpx',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.assignment),
    assignment: $ => seq(
      field('left', $.identifier),
      '=',
      field('right', $.number),
      ';'
    ),
    identifier: $ => /[a-z]+/,
    number: $ => /\d+/,
  }
});
