class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
(-Float::INFINITY..).step(2) { |x| ScratchPad << x; break if ScratchPad.recorded.size == 3 }
        p(ScratchPad.recorded)

        ScratchPad.record []
        (-Float::INFINITY...).step(2) { |x| ScratchPad << x; break if ScratchPad.recorded.size == 3 }
        p(ScratchPad.recorded)