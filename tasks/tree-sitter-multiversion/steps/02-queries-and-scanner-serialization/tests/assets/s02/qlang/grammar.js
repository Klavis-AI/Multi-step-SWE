module.exports = grammar({
  name: 'qlang',
  extras: $ => [/\s/, $.comment],
  rules: {
    source_file: $ => repeat($._statement),
    _statement: $ => choice($.binding, $.call),
    binding: $ => seq(
      'let',
      field('name', $.identifier),
      optional(seq(':', field('type', $.identifier))),
      '=',
      field('value', $._expression),
      ';'
    ),
    call: $ => seq(
      field('function', $.identifier),
      '(',
      optional(seq($._expression, repeat(seq(',', $._expression)))),
      ')',
      ';'
    ),
    _expression: $ => choice($.identifier, $.number, $.string),
    identifier: $ => /[a-zA-Z_]\w*/,
    number: $ => /\d+/,
    string: $ => token(seq('"', /[^"]*/, '"')),
    comment: $ => token(seq('//', /[^\n]*/)),
  }
});
