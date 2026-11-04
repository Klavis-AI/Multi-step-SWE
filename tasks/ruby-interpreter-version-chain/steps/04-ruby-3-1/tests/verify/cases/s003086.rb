class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
refinement = Module.new do
      refine Integer do
        def foo; "foo"; end
      end
    end

    result = nil
    Module.new do
      using refinement
      result = 1.foo
    end

    p(result)