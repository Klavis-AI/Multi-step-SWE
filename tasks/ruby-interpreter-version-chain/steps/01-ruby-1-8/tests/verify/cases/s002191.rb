class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((-2).odd?)
      p((-1).odd?)

      p(0.odd?)
      p(1.odd?)
      p(2.odd?)

      p(bignum_value(0).odd?)
      p(bignum_value(1).odd?)

      p((-bignum_value(0)).odd?)
      p((-bignum_value(1)).odd?)