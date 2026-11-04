module.exports = grammar({
  name: 'reztok',

  extras: $ => [/\s/],

  word: $ => $.identifier,

  reserved: {
    // Not a token: a `seq` of literals is a hidden rule with two children, so
    // this set names something that cannot be a reserved word (§2.1).
    global: $ => [seq('a', 'b')],
  },

  rules: {
    source_file: $ => repeat($.let_statement),

    let_statement: $ => seq(
      'let',
      $.identifier,
      '=',
      $.identifier,
      ';',
    ),

    identifier: $ => /[a-z_]+/,
  },
});
