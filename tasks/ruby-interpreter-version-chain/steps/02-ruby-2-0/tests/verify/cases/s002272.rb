class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(255.to_s)
        p(3.to_s)
        p(0.to_s)
        p(-9002.to_s)