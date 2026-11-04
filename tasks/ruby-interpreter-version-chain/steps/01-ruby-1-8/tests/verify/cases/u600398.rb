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
p(10.to_s(2))
    p(10.to_s(36))
    begin; 10.to_s(1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end