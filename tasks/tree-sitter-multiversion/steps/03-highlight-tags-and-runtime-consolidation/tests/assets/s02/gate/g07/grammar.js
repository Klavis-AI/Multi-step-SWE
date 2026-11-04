module.exports = grammar({
  name: 'gx07',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.word),
    word: $ => /[À-ɏͰ-Ͽa-z]+/,
  }
});
