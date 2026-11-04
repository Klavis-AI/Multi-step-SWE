module.exports = grammar({
  name: 'pjunk',
  extras: $ => [/\s/, ''],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, ';'),
    id: $ => /[a-z]+/,
  }
});
