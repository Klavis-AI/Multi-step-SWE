class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(0b101001101[4..1])
        p(0b101001101[4..2])
        p(0b101001101[-4..-5])