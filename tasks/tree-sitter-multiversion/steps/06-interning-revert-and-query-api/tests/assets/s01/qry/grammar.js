module.exports = grammar({
  name: 'qry',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($._statement),
    _statement: $ => choice($.assignment, $.call),
    assignment: $ => seq(field('left', $.identifier), '=', field('right', $._expression), ';'),
    call: $ => seq(field('function', $.identifier), '(', optional($.identifier), ')', ';'),
    _expression: $ => choice($.identifier, $.number, $.string),
    identifier: $ => /[a-z_]+/,
    number: $ => /[0-9]+/,
    string: $ => /"[^"]*"/,
  }
});
