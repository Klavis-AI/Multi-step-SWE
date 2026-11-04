module.exports = grammar({
  name: 'gextras',
  extras: $ => [/\s/, $.comment],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, ';'),
    comment: $ => token(seq('#', /[^\n]*/)),
    id: $ => /[a-z]+/,
  }
});
