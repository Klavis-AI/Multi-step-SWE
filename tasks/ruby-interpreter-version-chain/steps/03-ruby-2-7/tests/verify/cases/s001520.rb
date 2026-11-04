class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(5.5.round(0))
    p(5.7.round(1))
    p(1.2345678.round(2))
    p(123456.78.round(-2)) # rounded up
    p(-123456.78.round(-2))
    p(12.345678.round(3.999))