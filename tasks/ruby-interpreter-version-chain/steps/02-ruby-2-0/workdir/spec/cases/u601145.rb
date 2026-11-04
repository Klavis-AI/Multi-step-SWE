# frozen_string_literal: false
BSIZ = 120
class T012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
  end
p(sprintf("%c", 97))
    p(sprintf("%c", ?a))
    p(sprintf("%c", "a"))
    p(sprintf("%c", sprintf("%c%c", ?a, ?a)))
    p(sprintf(" " * (BSIZ - 1) + "%c", ?a))
    p(sprintf(" " * (BSIZ - 1) + "%-1c", ?a))
    p(sprintf("%#{ BSIZ + 1 }c", ?a))
    p(sprintf("%-#{ BSIZ + 1 }c", ?a))
    begin; sprintf("%c", -1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    s = sprintf("%c".encode(Encoding::US_ASCII), 0x80)
    p(s)
    p(s.send(:valid_encoding?))