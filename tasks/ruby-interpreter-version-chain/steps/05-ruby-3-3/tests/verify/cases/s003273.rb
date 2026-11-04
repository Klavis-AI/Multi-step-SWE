class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(proc {|x| }.parameters(lambda: true).first.first)
    p(proc {|y,*x| }.parameters(lambda: true).first.first)