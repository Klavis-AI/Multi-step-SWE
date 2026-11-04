module.exports = grammar({
  name: 's05item',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, ';'),
    id: $ => /[a-z]+/,
  }
});
