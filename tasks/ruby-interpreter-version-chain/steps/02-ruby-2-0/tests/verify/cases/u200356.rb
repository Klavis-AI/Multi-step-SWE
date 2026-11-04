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
(-5).upto(5) {|a|
      (-5).upto(5) {|b|
        next if b == 0
        q, r = a.divmod(b)
        p(b*q+r)
        p(r.abs < b.abs)
        p(0 < b ? (0 <= r && r < b) : (b < r && r <= 0))
        p(a/b)
        p(a.div(b))
        p(a%b)
        p(a.modulo(b))
      }
    }