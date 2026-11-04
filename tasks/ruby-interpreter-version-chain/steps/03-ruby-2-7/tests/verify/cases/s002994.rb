class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ModuleSpecs
  module Anonymous
    module Child
    end

    SameChild = Child
  end
end

m = Module.new
    n = Module.new

    suppress_warning do
      ModuleSpecs::Anonymous::SameName = m
      ModuleSpecs::Anonymous::SameName = n
    end

    p(m.name)
    p(n.name)