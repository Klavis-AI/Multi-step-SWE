class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
result = []
    9.upto(13.3) {|i| result << i}
    -5.upto(-1.3) {|i| result << i}
    p(result)