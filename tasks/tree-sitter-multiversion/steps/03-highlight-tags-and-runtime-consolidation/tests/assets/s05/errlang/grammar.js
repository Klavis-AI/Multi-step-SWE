module.exports = grammar({
  name: 's05err',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq('let', $.id, ';'),
    id: $ => /[a-z]+/,
  }
});
