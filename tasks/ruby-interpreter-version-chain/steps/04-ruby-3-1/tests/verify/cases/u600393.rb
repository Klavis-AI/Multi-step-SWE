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
(-5).upto(5) {|a|
      (-5).upto(5) {|b|
        next if b == 0
        q, r = a.divmod(b)
        p(b*q+r)
        p(r.abs.send(:<, b.abs))
        if 0 < b
          p(r.send(:>=, 0))
          p(r.send(:<, b))
        else
          p(r.send(:>, b))
          p(r.send(:<=, 0))
        end
        p(a/b)
        p(a.div(b))
        p(a%b)
        p(a.modulo(b))
      }
    }
    begin; 2.divmod(Float::NAN)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end