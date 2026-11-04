module.exports = grammar({
  name: 'rezplain',

  extras: $ => [/\s/],

  word: $ => $.identifier,

  supertypes: $ => [$._statement],

  reserved: {
    global: $ => ['if'],
  },

  rules: {
    source_file: $ => repeat($._statement),

    _statement: $ => choice(
      $.let_statement,
      $.if_statement,
      $.expression_statement,
    ),

    let_statement: $ => seq('let', $.identifier, '=', $.identifier, ';'),

    if_statement: $ => seq('if', '(', $.identifier, ')', $.block),

    block: $ => seq('{', repeat($._statement), '}'),

    expression_statement: $ => seq($.identifier, ';'),

    identifier: $ => /[a-z_]+/,
  },
});
