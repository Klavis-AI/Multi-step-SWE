class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
result = nil

    Module.new do
      klass = Class.new do
        def foo; "foo"; end
      end

      refinement = Module.new do
        refine klass do
          def foo; "foo from refinement"; end
        end
      end

      obj = klass.new
      using refinement
      result = obj.foo
    end

    p(result)