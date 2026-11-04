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
p((-0x40000000) % 3)
    p((-0x40000000) % (-1))