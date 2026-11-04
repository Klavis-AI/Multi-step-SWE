class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
rex = /this (?<is>is) [aA] (?<pat>pate?rn)/
    rex.named_captures.values.each do |value|
      p(value)
    end