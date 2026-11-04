class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case (ScratchPad << :foo; 1)
    in 0
      false
    in 1
      true
    end)

    p(ScratchPad.recorded)