module.exports = grammar({
  name: 'alxsingle',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat($.item),
    item: $ => seq($.seq_of_string, $.choice_of_pattern,
                   $.choice_of_string, $.seq_of_pattern, $._plain_wrap, ';'),
    seq_of_string: $ => seq('aa'),
    choice_of_pattern: $ => choice(/[0-9]+/),
    choice_of_string: $ => choice('bb'),
    seq_of_pattern: $ => seq(/[A-Z]+/),
    _plain_wrap: $ => seq($.id),
    id: $ => /[a-z]+/,
  }
});
