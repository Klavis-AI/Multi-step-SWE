class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ModuleSpecs
  module Modules
    A = "Module"
    B = 1
    C = nil
    D = true
    E = false
  end

  module Anonymous
  end
end

module ModuleSpecsKeywordWithoutSemicolon end
    p(ModuleSpecsKeywordWithoutSemicolon)