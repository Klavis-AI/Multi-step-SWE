def fact(n)
    return 1 if n == 0
    f = 1
    while n>0
      f *= n
      n -= 1
    end
    return f
  end
def shift_test(a)
    b = a / (2 ** 32)
    c = a >> 32
    assert_equal(b, c)

    b = a * (2 ** 32)
    c = a << 32
    assert_equal(b, c)
  end
inf = 1 / 0.0
    p((1  << 65536).to_f)
    p((-1 << 65536).to_f)