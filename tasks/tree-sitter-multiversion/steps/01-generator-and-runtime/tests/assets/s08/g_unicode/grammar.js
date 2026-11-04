module.exports = grammar({
  name: 'guni',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.word),
    word: $ => /[À-ɏͰ-Ͽa-z]+/,
  }
});
