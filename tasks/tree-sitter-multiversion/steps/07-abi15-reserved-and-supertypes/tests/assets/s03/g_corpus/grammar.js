module.exports = grammar({
  name: 'gcorpus',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.id, ';'),
    id: $ => /[a-z]+/,
  }
});
