module.exports = grammar({
  name: 'shp',
  extras: $ => [/\s/],
  inline: $ => [$._wrapper],
  supertypes: $ => [$._statement],
  rules: {
    source_file: $ => repeat($._wrapper),
    _wrapper: $ => choice($._statement, alias('done', $.terminator)),
    _statement: $ => choice($.binding, $.rebinding, $.blank_statement),
    binding: $ => seq('let', field('name', $.identifier), '=', field('value', $._value), ';'),
    rebinding: $ => seq('set', field('name', alias($.identifier, $.target)), '=', field('value', $._value), ';'),
    blank_statement: $ => seq('pass', optional($.identifier), ';'),
    _value: $ => choice($.identifier, $.number),
    identifier: $ => /[a-z]+/,
    number: $ => /[0-9]+/,
  }
});
