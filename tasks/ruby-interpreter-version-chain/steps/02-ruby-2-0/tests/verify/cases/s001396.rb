class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(1.2.coerce(1))
    p(5.28.coerce(1.0))
    p(1.0.coerce(1))
    p(1.0.coerce("2.5"))
    p(1.0.coerce(3.14))

    a, b = -0.0.coerce(bignum_value)
    p(a)
    p(b)
    a, b = 1.0.coerce(bignum_value)
    p(a)
    p(b)