class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
o = Set.new([1, 2])
    s = Set.new(o)
    p(s.size)
    p(s)
    p(s)