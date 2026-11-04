class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@array = [1, 2, 3, 4]
r = @array.combination(4).to_a
    p(r)
    p(r[0])