class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a = [ [ 1, [2, '3'] ] ]
    p(a.dig(0, 0))
    p(a.dig(0, 1, 1))
    p(a.dig(0, -1, 0))