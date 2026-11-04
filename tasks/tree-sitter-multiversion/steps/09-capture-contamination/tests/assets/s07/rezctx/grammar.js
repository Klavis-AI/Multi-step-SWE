module.exports = grammar({
  name: 'rezctx',

  extras: $ => [/\s/],

  word: $ => $.identifier,

  reserved: {
    global: $ => ['if'],
    key: $ => ['let'],
  },

  rules: {
    source_file: $ => repeat($.statement),

    statement: $ => choice($.let_statement, $.if_statement),

    let_statement: $ => seq('let', $.identifier, '=', $._value, ';'),

    if_statement: $ => seq('if', '(', $.identifier, ')', $.block),

    block: $ => seq('{', repeat($.statement), '}'),

    _value: $ => choice($.identifier, $.object),

    object: $ => seq('{', repeat($.pair), '}'),

    pair: $ => seq(reserved('key', $.identifier), ':', $.identifier, ','),

    identifier: $ => /[a-z_]+/,
  },
});
