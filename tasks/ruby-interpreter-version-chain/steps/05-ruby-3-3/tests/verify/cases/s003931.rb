class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(Set[])
      p(Set[1, 2, 3])
      p(Set[1, 2, 3])
      p(Set["a", :b, ?c])

      p(Set[1, 2, 3])
      p(Set[1, 2, 3])
      p(Set[1, 2, 3])