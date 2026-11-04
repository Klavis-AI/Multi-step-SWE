class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((-3 >> 2))
      p((-7 >> 3))
      p((-127 >> 7))

      # To make sure the exponent is not truncated
      p((-7 >> 32))
      p((-7 >> 64))