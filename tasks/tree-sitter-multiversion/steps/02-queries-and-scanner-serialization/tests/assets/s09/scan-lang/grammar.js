module.exports = grammar({
  name: 's9scan',
  externals: $ => [$.heredoc_start, $.heredoc_body],
  extras: $ => [/[ \t\n]/],
  rules: {
    source_file: $ => repeat($.heredoc),
    heredoc: $ => seq($.heredoc_start, $.heredoc_body),
  }
});
