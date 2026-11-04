module.exports = grammar({
  name: 'colx',
  extras: $ => [/\s/],
  externals: $ => [$.at_column_three],
  rules: {
    source_file: $ => repeat(choice($.at_column_three, $.word)),
    word: $ => /[^@\s]+/,
  }
});
