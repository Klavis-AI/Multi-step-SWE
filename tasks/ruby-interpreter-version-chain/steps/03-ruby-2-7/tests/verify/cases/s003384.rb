class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(Range.new(1, 2))
    p(Range.new(1, nil))
    p(Range.new(nil, 1))