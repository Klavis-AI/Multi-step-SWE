class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(0.bit_length)
      p(1.bit_length)
      p(2.bit_length)
      p(3.bit_length)
      p(4.bit_length)
      n = fixnum_max.bit_length
      p(fixnum_max[n])
      p(fixnum_max[n - 1])

      p(0.bit_length)
      p(1.bit_length)
      p(0xff.bit_length)
      p(0x100.bit_length)
      p((2**12 - 1).bit_length)
      p((2**12).bit_length)
      p((2**12 + 1).bit_length)