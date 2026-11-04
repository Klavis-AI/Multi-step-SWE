class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((Set[] <=> Set[]))
    p((Set[:a, :b, :c] <=> Set[:a, :b, :c]))