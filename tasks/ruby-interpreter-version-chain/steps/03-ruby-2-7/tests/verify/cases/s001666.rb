class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
h = { foo: { bar: { baz: 1 } } }
    p(h.dig(:foo, :bar, :baz))
    p(h.dig(:foo, :bar, :nope))
    p(h.dig(:foo, :baz))
    p(h.dig(:bar, :baz, :foo))