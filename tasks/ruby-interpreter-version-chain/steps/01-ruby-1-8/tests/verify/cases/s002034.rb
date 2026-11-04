class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(1.div(0.2))
      p(1.div(0.16))
      p(1.div(0.169))
      p(-1.div(50.4))
      p(1.div(bignum_value))
      p(1.div(Rational(1, 5)))