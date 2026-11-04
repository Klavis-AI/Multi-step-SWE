class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p((1 in 2))

      p((1 in Float))

      p(([1, 2] in [2, e]))

      p(({k: 1} in {k: 2}))