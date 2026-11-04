module.exports = grammar({
  name: 'gerror',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq('let', $.id, ';'),
    id: $ => /[a-z]+/,
  }
});
