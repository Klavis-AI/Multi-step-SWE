module.exports = grammar({
  name: 'suni',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.word),
    word: $ => /[À-ɏͰ-Ͽa-z]+/,
  }
});
