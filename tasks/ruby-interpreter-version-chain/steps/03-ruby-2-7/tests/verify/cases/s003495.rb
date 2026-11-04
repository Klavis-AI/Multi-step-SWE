class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((1.0..16.0).size)
      p((1.0...16.0).size)
      p((1.0..15.9).size)
      p((1.1..16.0).size)
      p((1.1..15.9).size)