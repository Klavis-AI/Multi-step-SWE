module.exports = grammar({
  name: 'gx08',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq('let', $.id, ';'),
    id: $ => /[a-z]+/,
  }
});
