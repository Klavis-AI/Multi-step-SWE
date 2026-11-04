class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(5.55.round(1, half: nil))
    p(5.55.round(1, half: :up))
    p(5.55.round(1, half: :down))
    p(5.55.round(1, half: :even))
    p(-5.55.round(1, half: nil))
    p(-5.55.round(1, half: :up))
    p(-5.55.round(1, half: :down))
    p(-5.55.round(1, half: :even))