def nan_test(x,y)
    extend Test::Unit::Assertions
    assert(x != y)
    assert_equal(false, (x < y))
    assert_equal(false, (x > y))
    assert_equal(false, (x <= y))
    assert_equal(false, (x >= y))
  end
p(11.5.divmod(4))
    p(11.5.divmod(-4))
    p((-11.5).divmod(4))
    p((-11.5).divmod(-4))