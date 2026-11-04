class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(0.pred)
    p(-1.pred)
    p(bignum_value.pred)
    p(fixnum_min.pred)
    p(20.pred)