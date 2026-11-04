def nan_test(x,y)
    extend Test::Unit::Assertions
    assert(x != y)
    assert_equal(false, (x < y))
    assert_equal(false, (x > y))
    assert_equal(false, (x <= y))
    assert_equal(false, (x >= y))
  end
a = Float("0")
    p(a.abs < Float::EPSILON)
    a = Float("0.0")
    p(a.abs < Float::EPSILON)
    a = Float("+0.0")
    p(a.abs < Float::EPSILON)
    a = Float("-0.0")
    p(a.abs < Float::EPSILON)
    a = Float("0.0000000000000000001")
    p(a != 0.0)
    a = Float("+0.0000000000000000001")
    p(a != 0.0)
    a = Float("-0.0000000000000000001")
    p(a != 0.0)
    a = Float(".0")
    p(a.abs < Float::EPSILON)
    a = Float("+.0")
    p(a.abs < Float::EPSILON)
    a = Float("-.0")
    p(a.abs < Float::EPSILON)
    p(a.abs < Float::EPSILON)
    begin; Float(".")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Float("+")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Float("+.")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Float("-")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Float("-.")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Float("1e")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end