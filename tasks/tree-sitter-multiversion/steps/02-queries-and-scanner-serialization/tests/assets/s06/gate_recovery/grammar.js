module.exports = grammar({
  name: 'alxrec',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq('let', $.id, ';'),
    id: $ => /[a-z]+/,
  }
});
