class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
result = []
    9.downto(1.3) {|i| result << i}
    3.downto(-1.3) {|i| result << i}
    p(result)