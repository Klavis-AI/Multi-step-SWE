BSIZ = 120
class T012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
  end
p(sprintf("%s", "foo"))
    p(sprintf("%.2s", "foo"))
    p(sprintf("%s", " " * BSIZ))
    p(sprintf("%#{ BSIZ - 1 + 3 }s", "foo"))
    p(sprintf("%#{ BSIZ + 3 }s", "foo"))
    p(sprintf("%-#{ BSIZ + 3 }s", "foo"))