# frozen_string_literal: false
class DummyNumeric < Numeric
    def to_int
      1
    end
  end
def assert_eql(a, b, mess)
    assert a.eql?(b), "expected #{a} & #{b} to be eql? #{mess}"
  end
@verbose = $VERBOSE
bug5713 = '[ruby-core:41494]'
    big = 1 << 66
    begin; 0 ** -big
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 0 ** Rational(-2,3)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end