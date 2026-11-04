module.exports = grammar({
  name: 'alxalias',
  extras: $ => [/\s/],
  inline: $ => [$._wrapper],
  rules: {
    source_file: $ => repeat($._wrapper),
    _wrapper: $ => choice($.decl, alias($.decl2, $.decl), alias('kw', $.keyword)),
    decl:  $ => seq('let', field('name', $.id), ';'),
    decl2: $ => seq('var', field('name', alias($.id, $.varname)), ';'),
    id: $ => /[a-z]+/,
  }
});
