class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(Set[].to_s)
    p(Set[nil, false, true].to_s)
    p(Set[1, 2, 3].to_s)
    p(Set["1", "2", "3"].to_s)
    p(Set[:a, "b", Set[?c]].to_s)