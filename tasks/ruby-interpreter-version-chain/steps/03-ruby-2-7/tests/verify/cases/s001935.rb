class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(-1.bit_length)
      p(-2.bit_length)
      p(-3.bit_length)
      p(-4.bit_length)
      p(-5.bit_length)
      n = fixnum_min.bit_length
      p(fixnum_min[n])
      p(fixnum_min[n - 1])

      p((-2**12 - 1).bit_length)
      p((-2**12).bit_length)
      p((-2**12 + 1).bit_length)
      p(-0x101.bit_length)
      p(-0x100.bit_length)
      p(-0xff.bit_length)
      p(-2.bit_length)
      p(-1.bit_length)