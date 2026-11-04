class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
str = +"\u{8765}"
    p(str.valid_encoding?)
    str.force_encoding('ascii')
    p(str.valid_encoding?)