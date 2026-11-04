class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module SymbolSpecs
  class MyRange < Range; end
end

:symbol[/(sy)(mb)(ol)/, 0]
        p($~[0])
        p($~[1])
        p($~[2])
        p($~[3])