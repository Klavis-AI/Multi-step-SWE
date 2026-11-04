class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((1 | 0))
      p((5 | 4))
      p((5 | 6))
      p((248 | 4096))
      p((0xffff | bignum_value + 0xf0f0))