module.exports = grammar({
  name: 'alxmigrate',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, ';'),
    id: $ => /[a-z]+/,
  }
});
