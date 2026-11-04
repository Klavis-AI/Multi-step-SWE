class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
(-2.0..).step { |x| break if x > 1.5; ScratchPad << x }
        p(ScratchPad.recorded)

        ScratchPad.record []
        (-2.0...).step { |x| break if x > 1.5; ScratchPad << x }
        p(ScratchPad.recorded)