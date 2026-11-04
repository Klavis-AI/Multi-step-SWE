class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(('A'..'Z').inspect)
    p(('A'...'Z').inspect)

    p((0..21).inspect)
    p((-8..0).inspect)
    p((-411..959).inspect)
    p((0xfff..0xfffff).inspect)
    p((0.5..2.4).inspect)