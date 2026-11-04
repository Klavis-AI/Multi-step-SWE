class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ModuleSpecs
  module SetTemporaryNameSpec
  end
end

m = Module.new
    p(m.name)

    m.set_temporary_name("fake_name")
    p(m.name)

    m.set_temporary_name(nil)
    p(m.name)