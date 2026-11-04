class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
(-1.0...1.0).step(0.5) { |x| ScratchPad << x }
        p(ScratchPad.recorded)