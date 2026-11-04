module.exports = grammar({
  name: 'gflag',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq(field('name', $.id), ';'),
    id: $ => /[a-z]+/,
  }
});
