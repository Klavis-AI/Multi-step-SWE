class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((256 & 16))
      p((2010 & 5))
      p((65535 & 1))
      p((0xffff & bignum_value + 0xffff_ffff))