class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((1.5 <=> 5))
    p((2.45 <=> 2.45))
    p(((bignum_value*1.1) <=> bignum_value))