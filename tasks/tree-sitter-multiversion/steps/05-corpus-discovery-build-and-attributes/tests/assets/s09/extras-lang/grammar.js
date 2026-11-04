module.exports = grammar({
  name: 'sxtras',
  extras: $ => [/\s/, $.comment],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, ';'),
    comment: $ => token(seq('#', /[^\n]*/)),
    id: $ => /[a-z]+/,
  }
});
