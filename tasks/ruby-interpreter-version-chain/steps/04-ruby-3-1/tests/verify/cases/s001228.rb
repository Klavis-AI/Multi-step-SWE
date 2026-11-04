class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
enum = Enumerator.product(1.., ["A", "B"])
    p(enum.take(5))
    p(enum.size)