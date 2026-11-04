class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p({ a: :b }.value?(:a))
    p({ 1 => 2 }.value?(2))
    h = Hash.new(5)
    p(h.value?(5))
    h = Hash.new { 5 }
    p(h.value?(5))