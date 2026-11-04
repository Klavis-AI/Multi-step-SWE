class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((-5 | -1))
      p((-3 | -4))
      p((-12 | -13))
      p((-13 | -12))