module.exports = grammar({
  name: 'alxhidden',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($._kw, $.id, $._count, ';'),
    _kw: $ => 'let',
    _count: $ => /[0-9]+/,
    id: $ => /[a-z]+/,
  }
});
