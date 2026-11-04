class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0...5))
    p(('A'...'B'))
    p((0.5...2.4))
    p((0xfffd...0xffff))
    p(Range.new(0, 1, true))