class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(/(?<a>.)(?<b>.)?/.match('0').named_captures(symbolize_names: true))
    p(/(?<a>.)(?<b>.)?/.match('0').named_captures(symbolize_names: "truly"))