module.exports = grammar({
  name: 'alxuni',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.word),
    word: $ => /[À-ɏͰ-Ͽa-z]+/,
  }
});
