module.exports = grammar({
  name: 'bare',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.word),
    word: $ => /[a-z]+/,
  }
});
