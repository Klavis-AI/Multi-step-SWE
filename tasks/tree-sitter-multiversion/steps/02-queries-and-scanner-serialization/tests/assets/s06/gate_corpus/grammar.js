module.exports = grammar({
  name: 'alxcorpus',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, ';'),
    id: $ => /[a-z]+/,
  }
});
