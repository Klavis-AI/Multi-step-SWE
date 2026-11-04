module.exports = grammar({
  name: 'ext',
  extras: $ => [/\s/],
  externals: $ => [$.percent_string],
  rules: {
    source_file: $ => repeat($.entry),
    entry: $ => seq($.name, '=', choice($.percent_string, $.number), ';'),
    name: $ => /[a-z]+/,
    number: $ => /[0-9]+/,
  }
});
