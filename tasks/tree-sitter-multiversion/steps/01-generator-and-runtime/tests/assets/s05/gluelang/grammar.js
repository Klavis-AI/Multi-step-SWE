module.exports = grammar({
  name: 's05glue',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => choice($.glued, $.loose),
    glued: $ => seq($.id, token.immediate('!')),
    loose: $ => seq($.id, '?'),
    id: $ => /[a-z]+/,
  }
});
