class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(5.5.round)
    p(0.4.round)
    p(0.6.round)
    p(-1.4.round)
    p(-2.8.round)
    p(0.0.round)