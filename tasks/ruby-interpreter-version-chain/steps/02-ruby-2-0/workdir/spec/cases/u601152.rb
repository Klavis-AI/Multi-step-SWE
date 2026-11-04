# frozen_string_literal: false
BSIZ = 120
class T012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
  end
format_str = "wrong constant name %s"
    interpolated_str = "\u3042"
    p(format_str.send(:ascii_only?))
    refute_predicate interpolated_str, :ascii_only?

    str = format_str % interpolated_str
    refute_predicate str, :ascii_only?