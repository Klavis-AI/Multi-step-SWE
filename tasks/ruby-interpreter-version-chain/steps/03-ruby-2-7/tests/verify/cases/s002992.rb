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

p(Module.new.singleton_class.name)
    p(String.singleton_class.name)
    p(Object.new.singleton_class.name)