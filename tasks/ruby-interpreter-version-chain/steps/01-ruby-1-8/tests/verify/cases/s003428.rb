class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((100..10).max {|x,y| x <=> y})
    p(('z'..'l').max {|x,y| x <=> y})
    p((5...5).max {|x,y| x <=> y})