module.exports = grammar({
  name: 'ghl',
  extras: $ => [/\s/, $.comment],
  rules: {
    source_file: $ => repeat($.statement),
    statement: $ => seq($.keyword, $.identifier, ';'),
    keyword: $ => choice('let', 'const', 'var'),
    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,
    comment: $ => token(seq('//', /[^\n]*/)),
  }
});
