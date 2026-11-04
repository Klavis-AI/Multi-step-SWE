class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
h = { 0 => false, a: 1 }
    p(h.dig(:a))
    p(h.dig(0))
    p(h.dig(1))