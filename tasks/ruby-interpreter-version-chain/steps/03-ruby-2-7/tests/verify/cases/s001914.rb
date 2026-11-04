class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
h = { a: 9, b: 'a', c: -10, d: nil }
    p(h.values_at)
    p(h.values_at)
    p(h.values_at(:a, :d, :b))
    p(h.values_at(:a, :d, :b))