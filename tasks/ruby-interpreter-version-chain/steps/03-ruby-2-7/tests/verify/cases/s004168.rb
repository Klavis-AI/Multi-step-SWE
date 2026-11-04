class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
three = "\u{8082}"
    p(three.bytesize)
    four = "\u{77082}"
    p(four.bytesize)
    p("#{three}#{four}".chr)