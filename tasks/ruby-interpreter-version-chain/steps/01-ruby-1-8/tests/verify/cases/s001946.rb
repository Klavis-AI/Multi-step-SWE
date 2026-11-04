class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((3 ^ 5))
      p((-2 ^ -255))
      p((5 ^ bignum_value + 0xffff_ffff))