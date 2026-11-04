class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
(-5..).step(2) { |x| break if x > 3; ScratchPad << x }
        p(ScratchPad.recorded)

        ScratchPad.record []
        (-5...).step(2) { |x| break if x > 3; ScratchPad << x }
        p(ScratchPad.recorded)