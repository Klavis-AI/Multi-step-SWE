class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
str = +"\x77"
    p(str.force_encoding('utf-8').valid_encoding?)
    p(str.force_encoding('binary').valid_encoding?)