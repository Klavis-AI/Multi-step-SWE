class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
r = /(?<hay>hay)(?<dot>.)(?<hay>tack)/
    p('haystack'.match(r).names)