class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((5..-5).to_a)
    p(('D'..'A').to_a)
    p(('D'...'A').to_a)
    p((0xffff...0xfffd).to_a)