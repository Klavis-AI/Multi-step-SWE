class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(-> *x { }.parameters.first.first)
    p(-> x, *y { }.parameters.last.first)
    p(proc {|*x| }.parameters.first.first)
    p(proc {|x,*y| }.parameters.last.first)