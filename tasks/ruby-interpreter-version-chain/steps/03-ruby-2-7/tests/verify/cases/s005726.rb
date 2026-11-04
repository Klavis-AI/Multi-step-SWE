class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
block = proc { [_1, _2, _3, _4, _5, _6, _7, _8, _9] }
    result = block.call(1, 2, 3, 4, 5, 6, 7, 8, 9)
    p(result)