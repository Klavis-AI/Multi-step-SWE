class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(("A".."E").step(2).size)
            p(("A"..."E").step(2).size)
            p(("A".."E").step.size)
            p(("A"..."E").step.size)