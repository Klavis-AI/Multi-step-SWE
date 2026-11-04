module.exports = grammar({
  name: 'rcv',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.statement),
    statement: $ => seq('let', field('name', $.identifier), '=', $._value, ';'),
    _value: $ => choice($.identifier, $.number),
    identifier: $ => /[a-z]+/,
    number: $ => /[0-9]+/,
  }
});
