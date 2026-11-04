module.exports = grammar({
  name: 'salt',
  extras: $ => [/\s/],
  rules: {
    source_file: $ => repeat(choice($.inc, $.comment)),
    inc: $ => token(seq('#include <', /[a-z]+/, '>')),
    comment: $ => token(seq('//', /[^\n]*/)),
  }
});
