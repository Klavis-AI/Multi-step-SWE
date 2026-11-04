module.exports = grammar({
  name: 's9hl',
  extras: $ => [/\s/, $.comment],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.kw, $.id, ';'),
    kw: $ => 'let',
    id: $ => /[a-z_]+/,
    comment: $ => token(seq('//', /[^\n]*/)),
  }
});
