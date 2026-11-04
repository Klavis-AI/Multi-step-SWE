module.exports = grammar({
  name: 'rezfld',

  extras: $ => [/\s/],

  rules: {
    source_file: $ => repeat($._statement),

    _statement: $ => choice(
      $.let_statement,
      $.expression_statement,
    ),

    let_statement: $ => seq(
      'let',
      field('name', $.identifier),
      '=',
      field('value', $.number),
      ';',
    ),

    expression_statement: $ => seq($.identifier, ';'),

    identifier: $ => /[a-z_]+/,

    number: $ => /[0-9]+/,
  },
});
