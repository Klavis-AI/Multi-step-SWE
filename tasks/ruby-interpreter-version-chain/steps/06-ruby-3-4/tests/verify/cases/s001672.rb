class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
default = {bar: 42}
    h = Hash.new(default)
    p(h.dig(:foo))
    p(h.dig(:foo, :bar))