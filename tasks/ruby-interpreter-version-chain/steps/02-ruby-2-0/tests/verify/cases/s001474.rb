class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
r = -0.0 % 42
    p(r)
    p((1/r))

    r = -0.0 % Float::INFINITY
    p(r)
    p((1/r))