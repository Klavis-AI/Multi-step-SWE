class DummyNumeric < Numeric
    def to_int
      1
    end
  end
def assert_eql(a, b, mess)
    assert a.eql?(b), "expected #{a} & #{b} to be eql? #{mess}"
  end
@verbose = $VERBOSE
    $VERBOSE = nil
begin; 1 & 1.5
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end