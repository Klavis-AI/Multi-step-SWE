class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((100..10).min {|x,y| x <=> y})
    p(('z'..'l').min {|x,y| x <=> y})
    p((7...7).min {|x,y| x <=> y})