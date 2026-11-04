module.exports = grammar({
  name: 'gunicode',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.word),
    word: $ => /[À-ɏͰ-Ͽa-z]+/,
  }
});
