# frozen_string_literal: false
BSIZ = 120
class T012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
  end
inf = 1.0 / 0.0
    p(sprintf("%#{ BSIZ + 3 }.1f", inf))
    p(sprintf("%+-f", inf))
    p(sprintf("%#{ BSIZ + 3 }.1f", 1.0))