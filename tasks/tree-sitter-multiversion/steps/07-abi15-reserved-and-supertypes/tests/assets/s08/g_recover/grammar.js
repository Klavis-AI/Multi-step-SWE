module.exports = grammar({
  name: 'grecov',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq('let', $.id, ';'),
    id: $ => /[a-z]+/,
  }
});
