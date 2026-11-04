class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(/(?<a>.)(?<b>.)?/.match('02').named_captures(symbolize_names: false))
    p(/(?<a>.)(?<b>.)?/.match('02').named_captures(symbolize_names: nil))