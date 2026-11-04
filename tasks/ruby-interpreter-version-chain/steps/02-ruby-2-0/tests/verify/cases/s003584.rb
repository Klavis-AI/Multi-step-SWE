class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0..21).to_s)
    p((-8..0).to_s)
    p((-411..959).to_s)
    p(('A'..'Z').to_s)
    p(('A'...'Z').to_s)
    p((0xfff..0xfffff).to_s)
    p((0.5..2.4).to_s)