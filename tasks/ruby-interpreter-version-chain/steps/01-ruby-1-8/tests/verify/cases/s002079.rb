class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(0b000001[-1, 4])
        p(0b000001[-2, 4])
        p(0b000001[-3, 4])