class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((2**1000-1).bit_length)
      p((2**1000).bit_length)
      p((2**1000+1).bit_length)

      p((2**10000-1).bit_length)
      p((2**10000).bit_length)
      p((2**10000+1).bit_length)

      p((1 << 100).bit_length)
      p((1 << 100).succ.bit_length)
      p((1 << 100).pred.bit_length)
      p((1 << 10000).bit_length)