class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((1..10).step.inspect)
      p((1..10).step(3).inspect)

      p((1...10).step.inspect)
      p((1...10).step(3).inspect)