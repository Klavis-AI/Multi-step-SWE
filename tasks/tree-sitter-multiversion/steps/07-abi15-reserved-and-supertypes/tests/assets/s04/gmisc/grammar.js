module.exports = grammar({
  name: 'gmisc',
  extras: $ => [/\s/, $.comment],
  inline: $ => [$._entry],
  rules: {
    source_file: $ => repeat($._entry),
    _entry: $ => choice($.decl, alias($.decl2, $.decl), $.glued, $.word),
    decl: $ => seq('let', field('name', $.id), ';'),
    decl2: $ => seq('var', field('name', alias($.id, $.varname)), ';'),
    glued: $ => seq($.id, token.immediate('!')),
    word: $ => /[À-ɏͰ-Ͽ]+/,
    id: $ => /[a-z]+/,
    comment: $ => token(seq('#', /[^\n]*/)),
  }
});
