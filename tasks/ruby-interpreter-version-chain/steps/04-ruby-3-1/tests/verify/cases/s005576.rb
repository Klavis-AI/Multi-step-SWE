class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a = 123
    if false
      b = a+1
    elsif false
      b = a+2
    elsif true
      b = a+3
    else
      b = a+4
    end
    p(b)