module.exports = grammar({
  name: 's05scan',
  extras: $ => [/\s/],
  externals: $ => [$.bang_run],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, $.bang_run, ';'),
    id: $ => /[a-z]+/,
  }
});
