class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((1.0 == 1))
    p((2.71828 == 1.428))
    p((-4.2 == 4.2))